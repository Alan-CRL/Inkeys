# Design: Win7 主画布恢复

## Scope and Evidence Boundary

只检查生产 Draw3 主画布，不把定格窗、Bar、StartupPreview 或旧 Draw2 路径当作同一 surface。主链路为：

```text
IdtMain WindowSpec / Window Service
  -> Drawpad + DrawpadPresentation HWND 创建、owner、style、bounds、可见性
  -> Draw3 Host 附着主窗口
  -> Hardware D3D11 -> WARP 回退、FL11 设备资源
  -> TransparentPresentationController 选择现有 DComp / ULW 模式
  -> DrawingController 清屏、首帧提交、后续内容提交
  -> ReconcileDraw3PresentationState 选择 Primary / Presentation / Hidden
  -> Window Service 在 owner 线程成对应用 bounds / visibility
```

以当前代码和附件为基线，先区分下列可证伪假设：

| 假设 | 已知证据 | 必须补到的判别证据 |
| --- | --- | --- |
| 主画布窗口被隐藏、销毁、错误尺寸或层级异常 | Window Service 管理 `Drawpad` 与 `DrawpadPresentation` 两个 HWND；状态机有 Primary/Presentation/Hidden 三态 | role 对应的 HWND、owner、style/ex-style、有效/可见、bounds、Z 序，以及每次显隐/销毁请求和读回结果 |
| 目标或首帧门控未满足 | `IdtMain` 正常继续前检查 `ProductFirstFrameReady`；状态协调器会等待运行态 target revision；空白选择页可合法隐藏两窗 | Host 各启动 stage、主目标首帧 attempt/result、当前 workspace/selection/page-content/target/revision、Retry/等待原因及唤醒事件 |
| Draw3 渲染/透明提交失败 | 附件的 ULW 采样是 Bar；普通 waitable 失败已成功回退，不能单独定因 | main-vs-selection target、presenter mode、swapchain 尺寸/格式/flags/swap effect、脏区与 ULW destination/size/blend、UpdateLayeredWindow 返回值/错误，或 Present HRESULT |
| API 接受提交但用户仍看不到画布 | 首帧/API 成功不是桌面光学可见证据 | 提交后对应 HWND 的可见性、bounds、alpha/dirty 摘要与层级；Win7 实机视觉验收仍是最终证据 |

另有一条重要状态约束：选择模式在空白页且辅助全帧已清洁时会有意隐藏两窗以实现穿透；在有内容时走 `DrawpadPresentation` ULW。书写模式应走 Primary。诊断须记下该决策，不能把合理的选择模式隐藏误报为呈现故障，也不能用显示主窗破坏穿透。

## Minimal Change Strategy

1. 修复前先用当前源码/既有自动检查尝试复现可疑状态，并保存失败输出。若已证明单一状态或调用错误导致窗体不可见，只改其 owner、尺寸、目标或呈现错误路径中直接相关的最小分支。
2. 若本环境不能重现，则只增加针对 Draw3 主画布的诊断点：
   - 每次进程一次的可比对 build identifier（日志值须和交付清单中的源码 SHA、EXE SHA、架构/配置对应）；
   - Hardware / WARP 的 D3D11CreateDevice 阶段、feature-level 列表和 HRESULT；
   - Drawpad/DrawpadPresentation 窗口创建与销毁，首次绑定/首帧，显隐或尺寸改变，以及操作后的读回状态；
   - 首帧及恢复后的主/选择目标提交结果、失败时 HRESULT/GetLastError 和最终呈现参数；
   - 状态机仅在首次等待、状态变化、失败/恢复时记录 gate 输入与等待/唤醒原因，避免逐帧日志。
3. 复用现有 startup milestones、Window Service 错误日志、`TransparentPresentationController` 和 `HostRuntimeSnapshot`；不扩建共享遥测、Scheduler 或 UI3 诊断设施。现有成功默认不代表硬件/像素成功。
4. 新 Win7 候选包单独记录完整源码身份、架构、配置、EXE SHA-256 和诊断 build identifier。现有 `G:\Inkeys` 测试包保持只读；它与本机旧 x64 Release 文件相同，但当前证据不足以证明基于本轮 HEAD。

## Compatibility and Non-Goals

- Windows 7 SP1 + KB2670838；保留 Hardware FL11 与 WARP、既有 DComp/ULW、普通 swapchain 回退及 FLIP_SEQUENTIAL。
- 不启用两个被禁 DWM 模式，不以系统版本直接替换 presenter，不改变输入/选择穿透合同。
- 保持 HTTP/HTTPS、HTTP fallback、旧 `智绘教.exe` 兼容、PPT 数据保护及功能开关。
- 不修改动画、光影、质量等级或四个生成 `.cso`；PPT 回归限于发现本修复影响的连接点。

## Rollback and Risks

- 若诊断证明只是空白选择页的既定隐藏状态，应保持实现，只按当前工作模式重做用户验收并记录预期；不得为表面上“始终显示”牺牲选择模式穿透。
- 日志必须限为启动、状态变化、首个结果及失败/恢复事件；避免逐帧格式化和 file I/O。
- 如果源码、HWND 状态与 API 结果都正常，仍需 Win7 用户可见画布步骤和新包日志才能继续，不能以 Win11/WARP 或 Bar 采样代替。
- 构建复用现有完整 Solution 和 ARM64 Debug 配置；开始前核查 `OutDir`、PptCOM copy target 与 shader 输出位置，避免覆盖测试包、既有产物或未提交文件。
