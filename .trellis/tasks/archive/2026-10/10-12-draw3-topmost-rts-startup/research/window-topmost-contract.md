# Research: Window Service 置顶契约与启动门禁

- Query: 当前根置顶请求的同步性、owner 链、实际结果核验、白板例外，以及启动 RTS 前的最小前置门禁和验证范围。
- Scope: mixed（当前仓库源码与 Microsoft Win32 官方文档）
- Date: 2026-10-12

## Findings

### 文件与已有规范

- `Inkeys/Inkeys/Window/Window.cpp`：Window Service 线程、同步命令队列、owner 链创建与根置顶事务。
- `Inkeys/Inkeys/Window/Window.cppm`：公开的 Handle/OverlayRoot/RequestTopmostRefresh/OverlayTopmost/WhiteboardWindowMode；WindowSpec.visible 默认 false。
- `Inkeys/Inkeys/Window/Window.Legacy.cpp`：既有周期刷新失败/恢复诊断。
- `InkeysHeadlessTests/window_tests.cpp`：真实 Win32 owner 树和隐藏竞争窗测试，部分后续步骤显示窗口。
- `InkeysHeadlessTests/animation_tests.cpp`：默认运行窗口测试，可用 --no-window 排除 HWND 测试。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1639-1650`：HWND 所属线程修改、根唯一置顶、白板 NOTOPMOST、PageControl 业务显隐契约。
- 同规范 `:1704-1717` 左右：隐藏外部竞争窗的完整 owner 树层级测试；不得把传播所得 TOPMOST 位误读为独立 SetWindowPos 证据。

### 已直接确认的执行关系

1. `RequestTopmostRefresh()` (`Window.cpp:480-505`) 调用 Submit；成功后才同步执行 observer 回调。它并非仅投递异步刷新。
2. `Submit(Command)` (`Window.cpp:1297-1320`) 对 owner thread 直接 Execute，其他线程入队并 `future.get()` 等待执行结果。当前等待无 timeout；本轮不扩大为队列超时重构。
3. 根选择 (`Window.cpp:327-340`) 顺序为 MagnifierHost、Freeze、Drawpad、Bar。正常完整产品必须是 Mag root；Mag optional 缺失时 Freeze root。后两个仅为不完整配置的既有兜底，不适合放行完整 Draw3 产品启动。
4. 创建 owner (`Window.cpp:906-967`) 是 Mag -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar。Presentation 必须依赖 Freeze、Drawpad 必须依赖 Presentation。窗口初始显隐默认隐藏 (`Window.cppm:100`)。
5. RefreshTopmost (`Window.cpp:1773-1791`) 只操作根，参数 `SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE`，不含 SHOWWINDOW/NOZORDER/ASYNCWINDOWPOS。目标由 `overlayTopmost_ && !whiteboardWindowMode_` 决定。Whiteboard 必须保持 NOTOPMOST。
6. 当前 `true` 只说明根 SetWindowPos BOOL 成功，没有 GetWindowLongPtr 或 owner 链核验，也没有保证屏幕视觉效果。`overlayTopmost_` 初始化为 true (`Window.cpp:718`) 是策略变量，不是任何 HWND 当前已拥有 TOPMOST 的证明。
7. 失败日志 (`Window.cpp:648-716`) 对 RefreshTopmost 去重并在恢复时记录；含目标根 HWND、owner、visible、topmost、bounds，写 debug output/stderr。成功且先前未失败不记录；该日志不含整个链逐节点事实。

### 最小规划建议

由主会话负责主流程两个 StartProduct 和回退调查，本文件不重复 Host 时序。

- 在每代主 Drawpad HWND 创建成功后、每次调用生产 StartProduct 前执行私有启动准备 helper。复用 Window Service `RequestTopmostRefresh()` 完成唯一根 HWND_TOPMOST 请求。保留首帧后的既有刷新，不把初始化门禁等同运行时刷新。
- 启动前确认 `OverlayTopmost()==true` 且 `WhiteboardWindowMode()==false`；异常策略不应通过强行 SetOverlayTopmost(true) 或逐窗 TOPMOST 修正。白板正常运行时仍遵守原策略。
- 请求成功后，读取本代 Freeze、DrawpadPresentation、Drawpad 和 optional MagnifierHost；要求必需窗口有效，根为 Mag（存在时）或 Freeze，根和上述必需画布链节点均具有 WS_EX_TOPMOST，GW_OWNER 的直接链接符合创建链。根应无 owner；无 Mag 时 Freeze 应无 owner。GetWindowLongPtr 前清 last error，区分返回零和失败。
- 验证完成前复核 service.Handle 与已捕获的 HWND 一致，避免新旧句柄混用。只需核对必需画布链，不把尚未就绪的 Bar/PPT/Setting 作为 RTS 启动必要条件。
- helper 放主流程内部，用已存在公开 Handle/OverlayRoot 读取事实，不新增公共 Window API、不修改周期 RefreshTopmost 成功语义。当前启动阶段 HWND 无并发重建的串行前提应由主流程核实；如果无法成立，才考虑 owner-thread 专用事务核验。
- BOOL 失败、真实 style/owner 不匹配、句柄失效/切代都不能继续该次 RTS 初始化。将失败阶段、角色、实际/期望 owner、style 和 Win32 error 纳入启动诊断。不要仅凭后置成功、RtsReady 或首帧成功放行。
- 保持 hidden/noactivate/矩形，无需 ShowWindow。不能通过给 Bar 单独 TOPMOST 补偿画布，不能用 NOZORDER 配 HWND_TOPMOST。

### 验证建议与限制

- 现有 `window_tests.cpp:448-496` 建立隐藏竞争 TOPMOST 窗并检查 root style 与完整 owner 树抬升；可补充各必要节点 TOPMOST 读回，以及无 Mag/Freeze root 的相同断言。这是生产根事务行为的有效覆盖，不需要 RTS 真硬件。
- 现有完整 RunWindowTests 有 Show(Setting) (`:443`)、Show whiteboard group (`:552-560`) 等，因此其名字虽为 Headless，不适合默认无 GUI 验证。`animation_tests.cpp:1641` 的 --no-window 可运行纯逻辑集，但不能证明 Z 序或真实 RTS 顺序。
- 最小新增隐藏专项入口应仅创建 Window Service 必需隐藏链，使用真实 RequestTopmostRefresh，并在模拟启动回调入口断言 TOPMOST/owner 已满足；首次创建与重建代分别执行。若不提取生产 helper 供专项入口复用，假回调测试只能证明测试自己的排序，不能证明生产 StartProduct 调用顺序。
- 私有 helper 不宜为测试扩大公共 module API；可先采用生产两个 StartProduct 分支静态审查 + 完整 solution Debug|ARM64 编译，真实 HWND 窗口/RTS 回归放用户明确授权的 GUI 阶段。后续有稳定测试 seam 时覆盖：prepare failure 阻止启动、成功先准备后启动、第二代重新准备。无 HWND 纯公式测试不能证明系统置顶问题已修复。

### 外部参考

- Microsoft Learn, SetWindowPos（Windows 2000+，当前官方页面查阅于 2026-10-12）：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos 。将 non-topmost owner 设为 topmost 会传播给 owned windows；只提高 owned 窗口不会提高其 owner。API 非零表示该调用成功，并未定义为后续全生命周期或屏幕可见效果保证。SWP_NOACTIVATE 不激活，SWP_ASYNCWINDOWPOS 才描述异步投递。

## Caveats / Not Found

- 没有现场 Z 序、扩展样式和 RTS 前后对比，本研究不能证明 Win32/RTS 内部机制为何产生“成功但被遮挡”。顺序合同回归和系统内因应分别表述。
- 现有根刷新虽同步，但调用返回后可被后续窗口事务改变；本次私有 gate 的读回仍只证明 RTS 前关键瞬间事实。
- 历史提交、会话日志、IdtMain/Host 启动入口由其他研究责任区核对，本文件不补造历史结论。
- 本研究未执行 build、GUI、git 或修改产品代码；写入范围仅此任务 research。
