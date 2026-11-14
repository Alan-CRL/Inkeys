> 后续收尾说明（2026-11-13）：用户已确认当前机惯性合适，并授权清理旧实现、验证后归档提交；过程与最终结果见 [收尾验证](../cleanup-validation-2026-11-13.md)。下文“只读/保持in_progress/不归档”是首次调查阶段的范围记录。本文P0–P4、尺度适配和触控板仍未实施；尺度适配留给下一个独立Trellis任务，归档不代表这些规划完成。

# 未来输入后端规划接纳记录（2026-11-13）

## 状态与优先级

- 本文按用户本次要求记录未来路线；本轮不实现触控板、WM_POINTER 后端、DM 或任何产品代码。
- 原文已完整转存为 [用户路线图快照](input-backend-roadmap-2026-11-12.md)，讨论日期为 2026-11-12。原文件来自 `D:/Personal/Downloads/Inkeys3_Input_Backend_Roadmap_2026-11-12.md`；原始文件 SHA256：82a855a391ca45e337447147e8b4d5421648078174a3316f61e33c62c4ff3567。
- 原文是产品策略、技术规划及待验证假设，不是已实现能力清单。其步骤不构成本轮实施、GUI、提交、推送或解除 quarantine 的授权。
- 本记录和原文优先于 [此前触控板候选集](touchpad-future-options.md) 中的方案推荐；旧文件的 RTS/DM 冲突证据、失败路径和测试门槛继续有效。
- 当前基线为 `241f1f44d23175e05fc1da4ecd1e29da67722414`。两项拖动任务均保留 `in_progress`；不另建任务、不归档。未来实现应先重新核对 HEAD、API 和硬件，再按对应阶段规划、评审。

## 已记录的未来产品策略（尚未实现）

| 系统/场景 | 拟定绘制后端 | 拟定导航策略 |
| --- | --- | --- |
| Windows 7 SP1 | RTS + 既有 Win32 鼠标回退；无 Pointer/DM 选项 | 兼容输入；通用滚轮导航若要新增仍须独立实现 |
| Windows 8.0 教师大屏 / 笔记本 | 默认分别 RTS / WM_POINTER；用户可切换 | 可研究触摸屏 DM；不承诺精确式触控板 DM |
| Windows 8.1/10/11 教师大屏 | 默认 RTS；用户可选 Pointer | RTS 优先；按后端/系统能力决定，不能假定 RTS+DM 共存 |
| Windows 8.1/10/11 笔记本 | 默认 WM_POINTER；可选 RTS | Pointer + 实际 PTP 能力时研究 DM，传统设备回退兼容输入 |
| Windows 10 及以下 + RTS | 保留 RTS | 按产品策略不启用 DM |
| Windows 11 + RTS | 保留 RTS | 新触摸板 Pointer API 为实验项；未通过共存实测前不承诺 |

设置将区分“设备场景（自动/教师大屏/笔记本）”与“绘制输入后端（自动/RTS/Windows Pointer）”，显示实际后端及回退原因；导航方式按能力选择。不得把 `SpeedEraser::DeviceMode` 复用为后端选择。Win7 不支持的选项应禁用，新 API 必须避免加载期破坏兼容。

上述平台组合按上传文档记录为未来目标；本轮未重新验证各版本 SDK/API 合同，也未获得任何新增平台的运行验收证据。

## 分阶段待办（全部未实现）

### P0：选择、统一契约与兼容设计

- [ ] 独立定义 Auto/RTS/Pointer 偏好、生效状态、场景默认矩阵和失败回退。
- [ ] 以当前 `ContactSnapshot`/`ContactInputCoordinator` 为唯一输出契约，明确身份、单位、QPC、来源、可靠终态及取消。
- [ ] 保证同一接触链只有一个正式绘制 producer；切换时关闭准入、停产、Drain/Cancel、证明旧 producer 退休、重置代次，再启新 producer，旧事件不得跨代复活。
- [ ] 核对 Win7 动态入口/加载兼容；保留独立 Draw3 帧循环、Ink Stroke Modeler 与 L0/L1/L2。

完成标准：选择/回退/切换和字段语义可评审且有无歧义合同，不靠静态新 API 导入或不受支持的配置启动失败。

### P1：WM_POINTER 绘制后端 MVP（未来首要开发目标）

- [ ] HWND 消息线程当场读取 Pointer/Pen/Touch 信息并复制为值；不能把 ID 延后交渲染线程查询，不在 WndProc 做模型/GPU/格式化日志。
- [ ] 接通 Pen/Touch Down/Move/Up/Cancelled/Hover；捕获变化、取消及异常需可靠闭合，hover 只更新光标。
- [ ] 压力检查有效掩码并归一化；XY tilt 转换/方向验证，rotation 不当作 azimuth；笔尾及橡皮标志分别核对。
- [ ] 独立解码 Pointer 接触矩形：屏幕像素转换到画布像素，保留原始值/有效性；不套用 RTS digitizer 长度单位、不伪造缺失面积。
- [ ] 使用稳定 `(sourceDevice,pointerId,backendGeneration)` 身份映射，不截断 HANDLE；初版用接收时 QPC，单 contact 时间单调。
- [ ] 统一仲裁已有 UI Pointer、Draw3 光标/触觉、正式 producer 和提升鼠标消息，避免双重 Down/Move/Up。
- [ ] 验证笔压/倾斜/笔尾、多点与手掌橡皮、DPI/多屏/窗口偏移、页面/白板/PPT/捕获终态。

完成标准：复用同一协调器/模型/渲染链实现完整绘制和可靠终态；不声称仅有 UI Pointer 即已有 Draw3 Pointer 后端。

### P2：DM 导航与触控板适配

- [ ] 按路线图区分 Win8 触摸屏 DM 与 Win8.1+ PTP 能力，真实检测后选择；传统设备兼容回退不伪称 PTP。
- [ ] Pointer 模式下将 DM 平移/惯性适配到现有 `CanvasNavigation`；输出导航变换，不产生笔迹，不再叠加应用 fling。
- [ ] 验证 DM 接管、CaptureChanged/Cancelled、Pen/Touch 并发及来源仲裁、惯性开关和模式/页/工作区/尺寸/退出隔离。
- [ ] 通用滚轮/水平滚轮回退若要新增，明确其来源不可区分边界。
- [ ] 缩放另行设计比例、中心、坐标变换、笔迹和缓存/脏区；不能以 DM 接通宣称缩放完成。

完成标准：实际支持的 Win8.1+/Win10/Win11 PTP + Pointer 组合有设备测试证据，无误笔迹、重复运动、永久 Down 或旧惯性复活。

### P3：Windows 11 + RTS 新触摸板 API 实验

- [ ] 动态解析并研究 RegisterTouchpadCapableWindow、GetPointerTouchpadInfo/FrameHistory 等入口。
- [ ] 先证明同 HWND 的 RTS 接触与触摸板消息/手势可共存；独立 HWND、置顶、穿透、DComp、焦点与捕获分别验证。
- [ ] 处理设备相对坐标/输出单位，不能当屏幕触摸坐标；失败安全回退并保持 RTS 绘制。
- [ ] 只有可复现兼容报告和稳定性通过后，才考虑正式启用。

### P4：时延与轨迹质量专项

- [ ] 同机同负载比较回调/消息等待、Move 新鲜度、帧消费年龄、模型成本及真实呈现延迟。
- [ ] 实测 Pointer PerformanceCount 与接收 QPC 的统一时钟语义，不直接混用。
- [ ] 评估重要转折点保留；仅在证据支持时考虑独立输入 HWND 线程等优化。

## 与当前代码衔接时必须保留的差异

1. 上传文档按 `215184d4...` 审阅，当前为 `241f1f44...`。RTS 仍每次 Packets 回调只解码批末点，QPC 仍为 callback arrival；尚无批内全样本/硬件采样时间。
2. **绘制 Move 仍是每触点 latest，导航已有独立 2048 条可靠有界事件历史**（`Draw3.ContactInput.cppm:162`；`.cpp:78,812,842,866,930`）。不能因原文“每帧消费最新状态”在新后端中删掉导航历史、回退为每帧一个估速样本。应复用现有 Publish* 同时维护两种消费语义。导航溢出/Stop/准入水位、可靠终态与唤醒合同仍适用。
3. 现产品已支持移动模式 Pen/PenTail；本模式 hover 不急停，实际 Down 及设备互斥控制接管。保留现有尾段释放、向量摩擦、残余投影 boost、每来源惯性设置和页面视角，不能恢复旧 Chromium/摩擦耦合合同。
4. 当前 PTP 在所有平台隔离；`WindowControl.cpp:380-386` 在任何 DM COM/Activate 前返回。保留旧 DM 代码/偏好不代表 P2 已完成，不能只关门禁重新启用。旧文档“按模式切换 RTS/DM”只保留为历史替代方案，未来主线是本文双绘制后端选择。
5. 双指缩放、屏幕物理尺度/profile、RTS 批内样本/原生时间戳均未因此实现，按各自范围单独设计。

## 未来验收入口

按原文 §9 完整保留教师一体机、Surface、Win7/8.0/8.1/10/11、PTP/传统触摸板、多屏/DPI/旋转、多源手掌和消息线程繁忙矩阵。尤其保留当前故障复现：RTS/DM 正反初始化顺序、真实接触后续绘制、完整停产/退休与失败恢复。合成输入、屏外测试、ARM64 构建不能外推全部系统或光学手感。每个阶段应重建当前原生 Solution 并跑适用无窗口测试，设备/GUI 实测按用户授权或由用户操作。
