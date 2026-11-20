## 1) 关键代码路径

- [事实] `SelectionUsesAuxiliaryOutput` 目前只看选择态和工作区。四处生产调用分别决定帧循环的 `expectedOutputTarget`、`DrawpadPresentationReady`、工作区 ready 和交接尺寸。Host 首帧另有独立判断，只看选择态。`Bridge.h:191`、`DrawingController.cpp:21855`、`PresentationState.h:20`、`IdtState.cpp:714,4708`、`Host.cpp:2414`
- [事实] `requestedOutputTarget` 先分流到 `selectionUlwPresenter`，再按实际后端走 `primaryUlwPresenter`。Host 用成功全帧登记目标 ready，仅对 `SelectionUlw` 更新辅助清屏标记。`ResolveDrawpadPresentationSurface` 据此把空选择页判为 `Hidden`。`TransparentPresentation.cpp:2413–2438`、`Host.cpp:1082–1091,1137–1163`、`PresentationState.h:79–86`
- [事实] `SetClickThrough` 已用于两窗。辅助窗恒穿透，稳定态主窗跟随选择意图，工作区准备及故障释放也会开启穿透。Drawpad 创建时清掉两位，ULW 初始化在 `ConfigureWindow` 加回 `WS_EX_LAYERED`。`ApplyCanvasClickThrough` 只改 `WS_EX_TRANSPARENT`，以 `SWP_FRAMECHANGED` 提交并读回。分层窗的鼠标穿透可行。`IdtState.cpp:804–809,1038–1045,1114–1121`、`Window.cpp:3171–3177,4227–4262`、`Window.CanvasPolicy.h:27–54`、`IdtMain.cpp:3770–3782`、`TransparentPresentation.cpp:180–189,1782,1990`。[Win32 文档](https://learn.microsoft.com/windows/win32/winmsg/window-features)

## 2) 推荐改动点

- [推测] 四处调用及 Host 首帧统一按**实际后端**选目标。ULW 始终 `PrimaryDrawpad`，关闭辅助清屏等待。`IdtState.cpp:1182,4633` 同步改常规显隐和工作区空页条件。复用 `SetDrawpadSurfaceVisibility(Primary)`，并同步改 `Window.cpp:3276,3735` 的工作区显隐和安全放行角色。保留故障、最小化的隐藏分支。
- [推测] `InitializePresenter` 在主 ULW 成功后直接返回。保留辅助 HWND，ULW 时隐藏且零像素。省建会碰到固定 owner 链、双窗显隐、初始化和恢复的非空校验，以及夹具的 owner 断言。`IdtMain.cpp:3831–3842`、`Window.cpp:87,1459–1485,4488,4696–4709`、`TransparentPresentation.cpp:1910–1940,2187,2281`、`HiddenWindowTest.cpp:1289`
- [推测] `EnsureCanvasPolicy` 按窗缓存实际底行策略。非白板主窗在 **ULW 选择态或 R3 开关开启**时裁底行并置 NonRude，辅助窗维持非白板裁剪。同步改准备回执校验。同窗切换先收尾输入，进选择先开穿透再裁剪，回绘制先恢复区域再关穿透。沿用现有位图和正常脏区，避开双窗先隐藏的事务。`Window.cpp:2656,2722,2794,2819,3158`、`Window.CanvasPolicy.h:57–86,187–204`
- [推测] `PublishInformation` 的 ULW 分支改显示“由画布承担”。`rawOutputTarget` 记录物理表面，单窗选择归 `PrimaryDrawpad`。看板另按非白板选择态排除样本。`TransparentPresentation.cpp:1681,1698,1707`、`RuntimeMetrics.cpp:281–298`、`DrawingController.cpp:24029–24034`

## 3) 风险与陷阱

- [事实] 垫底为黑色 α=1/255，RGB 不改，源 α≤127 时加 1。`committedFullFrameClean` 看源像素。`ReadbackPayloadBytes` 计 staging 和 DIB，`needsFullReadback` 在资源失效后补整帧。`TransparentPresentation.cpp:268–281,389–418,632–658,765–767,991–993`
- [推测] 保留垫底只带来约 0.4% 的背景压暗，样式切换本身不使脏区失效。拆开 `outputTargetChanged` 的模式边沿与目标变化，保留光标清除等真实脏区。否则仍会整帧，空页也可能误等辅助验证。`DrawingController.cpp:21860,21917,22523–22531`
- [事实] 现有切换刷新光标，离开移动态取消触控板导航，选择态拒收新笔触并解绑连续触觉。鼠标捕获释放及部分清理由隐藏触发，Pointer 另有取消消息，RTS 有生产者接触重置。`WindowControl.cpp:1686–1695,2613–2624,2959–3001,3037–3044,3470–3497`、`Window.cpp:4499`、`DrawingController.cpp:18622–18627,22347–22366`、`RealtimeStylus.cpp:1780–1789`
- [推测] 同窗穿透前在窗口线程显式收尾捕获、Pointer、RTS 和导航，避免按住切换后续笔或残留悬停。恢复后要在**同一轮**重算目标，等新表面首帧成功再切显隐，并用实际后端约束旧回执。DComp 可降 ULW，设备丢失可能按偏好重试回 DComp。手选 Win7Dwm 恢复直接走 ULW，强制模式只重建指定后端。`TransparentPresentation.cpp:107–117,2281–2369`、`DrawingController.cpp:21853–21942`

## 4) 测试

- [事实] HiddenWindowTest 的 PPT 翻页、初始及往返选择、捕获、辅助资源生命周期断言旧行为。headless 的呈现规划断言辅助目标和空页隐藏。Controller 的生产自测注入 `SelectionUlw` 验证输出身份，仍适用于双窗。`HiddenWindowTest.cpp:1063–1075,1293–1353,2927–2949,3012–3052`、`draw3_bridge_tests.cpp:838–921`、`DrawingController.cpp:14015–14049`
- [推测] 补测 ULW 空页及有墨往返、辅助零分配、仅样式切换无整帧、按住切换、回退时旧回执失效，以及底行策略缓存和失败重试。

## 5) 尚需确认

- [待实测] 验证静止鼠标、RTS、Pointer 与触控板的穿透和光标归属，以及任务栏升起、末行恢复和切换闪烁。确认 R3 开关覆盖 PPT。若默认全屏仍挡不住边缘手势，再扩展 `ResolveTargets` 的主窗标记。`Window.ShellFullscreen.h:47–52`

[事实] 已读取当前分支 PRD 并静态核对源码。本次未改文件、未构建或运行测试。`.git/HEAD:1`、`prd.md` R3、R4。