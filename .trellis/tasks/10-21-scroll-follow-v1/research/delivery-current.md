# 通用滚动批注初版交付记录（2026-10-21）

基线为 `feature/move` / `3ace726b`。本轮继续完成正式取帧、视觉追踪、产品集成及真实文件回归代码。所有验证仅无窗口或全程不可见 HWND，没有 Computer Use、可见内容、前台或置顶操作，没有新 commit/push。完整功能保留实际桌面验收门槛；不能把软件测试通过写成实屏滚动批注已通过。

最终软件检查已完成：四阶段顺序独立 check 通过，ARM64/x64 完整 solution 和相关短回归通过，最终 PE、i18n 与格式检查通过。已按本轮权限停止在必要实际画面/系统窗口/硬件验收之前，父任务保留未完成门槛。

## 已实现的功能

- 继续共用原定格 Mag 的初始化、窗口和寿命设计；原 MagnifierThread 串行协调正式采样、更新与 `PrintWindow(child, ..., 2)`。正式路径不依赖 scaling callback，没有第二套 Mag，也没有引入其它截图技术。
- 异步捕获 Start/Stop、会话和帧序号、固定主屏/workArea、BGRA 借用缓冲、质量检查、超时监督。停止只撤销发布，不强杀同步系统调用；未返回的调用继续独占资源并阻止重启。
- 独立 `VisualMotionTracker`：纯图像输入、二维平移、空间一致性与 FB 校验、固定出生锚点的特征接力、可信度与暂停原因。拒绝帧不吞掉中间位移，暂停后不自动恢复。
- Desktop 视图调整会话与主栏 More 按钮，三语文案/SVG、旧布局仅补缺。初始化先得到有效底图并完成原辅助呈现握手，然后开启穿透。工具、编辑和定格操作先完成退出；工作区、文档/页、显示几何和设备变化退休旧会话。
- Draw3 用单槽保存最新的绝对累计目标，唯一 owner 按“启动视口减去累计内容位移”更新现有 Viewport。复用原热前像失效、Canvas-local 合成缓存、可信 L2 和预算内 Tile 恢复，不修改笔划原点、Undo/Redo 或渲染参数。
- 退出屏障封存最后目标，真实 final present 和输出/窗口确认完成后恢复绘制。旧会话、重复序号、错误场景及旧 Host 凭据不能操作新代。
- 沿用 UInk v10 和原 Desktop 清空/退出保存链，不逐帧保存。新增私有、严格不可见的真实文件与编辑回归，不扩展通用冷载产品能力。
- 复用唯一诊断会话与有界队列；普通启动没有新控制台。`--scroll-follow-console` 输出状态、错误和每秒摘要，`--scroll-follow-verbose` 再输出逐帧记录。

## 最终参数与数字的含义

| 项目 | 当前值与含义 |
| --- | --- |
| 捕获频率 | 目标 20 次/秒，每 50ms 一个周期；超时跳过周期，不堆积图像。这是目标，没有正式 worker 实屏持续 FPS 结果 |
| 首帧 | 预热约 800ms，第一张有效帧的总期限 5 秒；没有有效结果时可重试 |
| 运行超时 | 超过 1 秒没有有效结果就暂停；相同静止图像仍可有效 |
| 捕获质量 | 64×64 采样；RGB 最大值≤2 的比例≥99.5% 判近黑，方差<2 判低信息；API/几何/缓冲/超时另有原因 |
| 图像与角点 | 半分辨率灰度、8×6 网格，最多 320 角点；GFTT quality=.01、minDistance=8，可信帧补点 |
| LK | 21×21 窗口、3 层、30 次/epsilon=.01；FB≤1 个处理像素 |
| 平移门槛 | 有效/内点至少 40、空间内点比例≥65%、一致半径 1.5 个处理像素，并检查覆盖、存活、跳变、变形与次流 |
| 暂停 | 严重无效捕获立即暂停；普通视觉低可信连续 3 帧暂停；保持最后可信位置，不自动恢复 |
| 静止误差 | 离线连续 300 帧实测漂移为 0，带微噪声约 .006 像素；单位均为原图像素 |
| 长滚动误差 | 12 屏亚像素序列最大累计误差约 .92 像素；14 组共 6918 个合法帧的每个前缀均满足原严格误差门槛 |

Tracker 完整参数、每组单步/累计误差和已测边界见追踪阶段 `research/check-current.md` 与 `scroll-follow-tracking.md`。固定区域占主要画面时，少数中部区域 1/2 原图像素运动可能合入零主模式，这是已记录的本版模型边界；不能承诺自动选择任意小区域。

## 软件验证证据与边界

| 验证 | 当前结果 |
| --- | --- |
| 正式取帧状态/寿命 | 独立 check 通过，含真实监督线程 idle/active 各 64 次停止/join；fake backend 不代表实屏像素 |
| Tracker | 独立 check 通过；修复 4px 中部次流误接受，6918 个合法运动帧结果保持，静止/接力/反向/水平/干扰/变形/失败暂停通过 |
| Business 与 Draw3 | 实际 Tick/Frame/真实 Tracker 的控制矩阵通过；外部 Capture/Host/ACK 为明确接缝。另有真实不可见 Host、contact、跨帧 End、Tile 恢复和跨 Host 凭据回归通过 |
| 配置/快捷键 | 实际 ViewAdjust 配置用例、StateMode/shortcut 生产业务用例、i18n 426/426 通过；实际主栏/系统交接仍待现场 |
| 完整无窗口 | 产品 check 的 `InkeysHeadlessTests --no-window` exit0，约 157.97 秒；原八项无 GUI 退出失败接管 exit0，约 95.77 秒 |
| 原保存基线 | 原 UInk file、presentation codec、Desktop autosave、Presentation autosave 四组 CLI 全通过；PPT 旧 fixture 使用短私有 TEMP 避开既有深层路径预算 |
| 新真实保存 | Shape/Pen 各 viewport-only/edit，共四场景独立 check 通过，约 4.19 秒；8 个实际 UInk，原 A/codec 逐值精确相等 |
| ARM64 | 最终独立 check 完整 solution exit0，约 8.14 秒；协议/业务/状态/配置/全隐藏/保存及相关 Headless 短回归通过 |
| x64 | 完整 solution exit0；首次测试 PDB 损坏，留档后原配置重建成功，随后 5 项纯 CLI 全通过 |
| PE/格式 | 47 个源/工程文本编码/BOM/CRLF/EOF、4XML、diff、i18n426/426通过；两架构 Mag 均延迟加载，无 OpenCV/WGC 动态依赖；旧系统仍需实机 |

新保存场景先让 A 通过原 Clear 真提交，等 Controller 消费匹配 completion 并释放 CPU fallback，再 Undo 由原 worker 读盘安装。四场景均 `diskInstalled=1`、`memoryRestored=0`；最终独立 check 的普通笔 A 各为 23 个实际建模点，所有点/宽/style/工作区及页 GUID 与保存基准相等。仅视口场景最后 Publish 后立即 Stop、不预等 Applied，真实 Exit 文件仍得到 `(144,-64,1)`。编辑场景 B 的 Down `(80,100)` 加新视口得到 world `(224,36)`；原 Undo/Redo、已有 Redo 保留和新 B 清 Redo 均有断言。橡皮以原有序 mask Stroke 保存，实际文件几何覆盖 B，未将其称为屏幕擦除像素验收。

这组证据分别证明文件 codec 保存 Viewport 和原 Clear Undo 的真实磁盘恢复。现有 Host 没有任意外部 Desktop 文件冷载/替换 API；不能据此宣称跨进程 Host 冷载的旧视口已测。

构建期间本机原增量链接器曾在 `IncrBuildImage` 报 LNK1000，仅清理核实的生成 `Inkeys.ilk` 后原配置重链成功。x64 首次还在测试 PDB 报 LNK1285，核实并将损坏的生成 PDB 留档后同配置重建成功；没有改 SDK、依赖、项目或链接选项。原始错误、恢复步骤、最终产物 hash 与 PE 具体范围见 `final-build-and-pe.md`。

## 必要现场门槛与停止点

本轮不可见权限无法证明以下实际画面行为，因此软件检查完成后停止，不标父任务或全功能完成：

1. 在正式 MagnifierThread 路径验证无 callback 首帧、连续变化、静止、排除、重复启停、Freeze 互斥、实际截图耗时/FPS/长期资源。上轮原 owner 的成功 PrintWindow 图像仅是基础证据。
2. 验证实际双窗显隐/穿透、墨迹持续可见、底层滚动输入、最后位移交接、工具/快捷键/清空/定格操作和 Paused 手动退出。不可见测试中的窗口 ACK 是明确接缝。
3. 专用页面后验证 Edge/Chrome、可用 Word/PDF 的上下/水平/反向/惯性/边界、十屏与特征全部离屏；多区域、缩放/旋转不属于本版跟随能力。
4. 真实笔/触控、显示/DPI/主屏变更、设备恢复、锁屏与旧 Windows/WOW64 分别补测。静态可选能力门禁和导入审查不能代替旧系统实机。

完整操作与控制台命令见 `usage-and-live-acceptance.md`；每阶段源码、测试原始日志和边界见对应 `research/*current.md`、捕获 `production-capture-check.md`，总状态见 `validation-matrix.md`。
