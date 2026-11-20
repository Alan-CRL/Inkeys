已读取 PRD，在 `refactoring/draw` 上逐行核对 R1–R3。仅做静态调查，未运行构建或测试。

## 1. 关键代码路径

- [事实] R1：`StartPointerInput` 检查 API、置 `pointerStartRequested_`，再走 `kMouseInputOwnerMessage`。跨线程投递给 HWND owner 并等确认。start 在设备通知注册成功后置 `pointerInputSelected_`，stop 和 `WM_DESTROY` 清除。`AttachExternal` 安装旧 Tablet 属性，`DetachExternal` 在关闭消息转发后调用 `RestoreTabletInputProperty`。Host 在回退 RTS 前确认停产。`Draw3.WindowControl.cpp:899,919,953,1440,1469,2786,2980`，`Draw3.Host.cpp:2575,2724`
- [事实] R2：性能页底层设置调用 `RenderDraw3InputMode`。按保存的偏好在下拉行前展示 RTS 蓝条或 Pointer 不支持警告。`Setting.cpp:1375,4802`，`Setting.Controls.h:197`，`Inkeys/src/i18n/zh-CN.jsonc:1066`
- [事实] R3：`EnsureCanvasPolicy` 遍历两个画布角色。`CanvasBottomExclusion` 计算底部物理行交集，`BuildCanvasRegion` 从原区域扣除，`CanvasNonRudeValue` 按白板状态决定属性。创建、白板切换、显示前和画布重定位都会应用。画布收到 `WM_DISPLAYCHANGE` 时 Host 只标记输入尺度待更新。`Window.CanvasPolicy.h:13,78,125`，`Window.cpp:1752,2785,2842,4496,4603`，`Draw3.Host.cpp:3280`

## 2. 推荐改动点

- [推测] R1：在 `HandleWindowMessage` 的成功 start 分支仅为实际 Pointer 关闭 PRD 指定的 11 项反馈，记录成功项。owner stop 和销毁时只恢复这些项。独立脱离若需兜底，在关闭转发前走 owner 屏障。复用 `DrawingPointerApi` 动态解析，缺失静默跳过。`Draw3.WindowControl.cpp:205,919,2786,2980`
- [事实] SDK `10.0.26100.0/um/winuser.h:6777,6809` 和文档均未声明线程限制，无法据此确认跨线程保证。owner 调用可与现有启停串行。参数用 `flags=0` 和 `BOOL FALSE`，重置用 `size=0, NULL`。最低支持 Win8。[Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowfeedbacksetting)
- [推测] R2：在现有条件分支增 `InputPreference::MouseCompatibility`，沿用默认信息级别。拟增 `SettingsUI.Performance.InputMode.MouseCompatibilityNotice`，同步 20 语及生成键。`Draw3.RtsStartup.h:12`，`Setting.cpp:1388`，`Scripts/i18n.ps1:708`
- [推测] R3：`EnsureCanvasPolicy` 按角色、白板和开关解析是否挖底及设属性。非白板主窗默认还原原区域和属性，辅助窗保留现状。把有效策略纳入缓存键及 `WorkspaceNativePolicyMatches` 校验。`RenderWindowBehavior` 加无说明 `ToggleRow`，拟用 `SettingsUI.Regular.Behavior.DesktopCanvasNonFullscreen`。`Other.Config.cppm` 的 `UI` 组加带中文注释、默认 `false` 的 `NoUpload` 布尔项。`IdtMain.cpp` 启动时注入窗口服务，`Setting.cpp` 变更时写盘并提交 owner 命令立即重算。`Window.cpp:2794,3142`，`Setting.Pages.cpp:1607,1630`，`Setting.cpp:3342,3607`，`Other.Config.cppm:389`，`IdtMain.cpp:3913`
- [推测] R3 第二步：若首步实测不足，在 `ResolveTargets` 增独立的非白板主窗条件。普通工作区提交会清 `overlayFullscreen_`。`ApplyOverlayFullscreen` 保留关闭、最小化、安全隐藏和实际可见性门控，沿用 `ApplyDrawpadSurfaceVisibility` 显隐后发布及 COM worker。切选择态或开启兼容开关后发布 FALSE。`Window.ShellFullscreen.h:47,168`，`Window.cpp:3304,4481,4670`

## 3. 风险／陷阱

- [事实] `canvasPolicyDirty_` 会连带重做激活样式，并改变失败回滚的显隐行为。[推测] 正常开关变更靠有效策略键触发，保留事务重试，并使在途工作区校验失效。纯显示器拓扑变化若没有后续窗口操作，需验证是否补投 owner 重算。`Window.cpp:2667,2818,2836,3203`

## 4. 测试

- [事实] `window_tests.cpp:365,601` 仍假定桌面/PPT 两窗都裁底并设属性，应改为角色×开关矩阵，保留原区域及负坐标断言。仅第二步扩充 `ResolveTargets` 的默认、隐藏和撤销用例。`window_tests.cpp:381,406,1259`
- [事实] `setting_design_tests` 的性能页命中是导航测试，蓝条断言属于常规页，未发现性能页首条定位依赖。`setting_design_tests.cpp:4432,4452,5167` [推测] 新增按 ID 验证鼠标条及行为页开关，补配置缺键、类型、往返和重置，以及隐藏窗 Pointer 停止、脱离和 API 缺失用例。`config_metadata_tests.cpp:954`，`Draw3.HiddenWindowTest.cpp:333`

## 5. 尚需确认

- [待实测] Win10/11 触摸圈、拖尾及笔反馈，Win7 启动。验证桌面绘制／选择切换、自动隐藏任务栏、免打扰和换屏，先以首步结果决定是否加显式全屏标记。
- [推测] 开关覆盖桌面和 PPT，按 PRD 待定项请用户确认。`.trellis/tasks/11-20-draw3-canvas-gesture-fullscreen/prd.md:64`