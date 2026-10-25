# 设置 Win32 外框兼容研究（2026-10-24）

本轮由父任务委派研究与旧系统几何修复。不 build、GUI、Computer Use、commit/push；不修改原有两张 CSO。静态与现代主机上的 headless 结果不能代替 Win7 Aero 实机验证。

## 已确认的本地实现

- `Window.cppm::SettingWindowStyle` 为 `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN`，保留原生 caption、系统菜单、缩放及 min/max 能力。
- `Window.cpp::CreateWindowFor` 以自绘标题栏属于客户区计算创建几何，创建后已执行 `SWP_FRAMECHANGED` 并根据实际 outer/client 纠偏。
- `Setting.cpp::WM_NCCALCSIZE` 自行内缩四周 frame；旧代码只要 composition 启用就将恢复态顶部压到系统细边框宽度，因此把现代 DWM 合同错误用于 Win7 Aero。
- `WM_NCPAINT` 与 `WM_NCACTIVATE` 目前直接交 `DefWindowProc`；只修改 NCCALCSIZE 并不能从源码审计推导原生 caption 不再被绘制。
- `ApplySettingFrameTheme` 明确设置灰色 `DWMWA_BORDER_COLOR`，会覆盖系统默认边框策略；由主会话负责改为默认色及主题消息刷新。

## 官方依据与不应混淆的接口

- [WTNCA flags](https://learn.microsoft.com/en-us/windows/win32/controls/wtnca)：`NODRAWCAPTION`、`NODRAWICON` 分别控制 caption/图标；`NOSYSMENU` 禁止系统图标菜单。文档未保证这些标志可单独隐藏 min/max/close；尤其不能把 NOSYSMENU 当作无副作用绘制开关。
- [SetWindowThemeAttribute](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-setwindowthemeattribute)：Vista 起可用，WTA_NONCLIENT 接受 WTA_OPTIONS。它不是 Win7 缺失 API，但本轮不因名称推测其按钮行为，也不增加该导入。
- [Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-styles)：WS_CAPTION 表示标题栏，WS_THICKFRAME 表示 sizing border；官方同时要求 WS_SYSMENU 搭配 WS_CAPTION。仅删 WS_CAPTION 虽常见于自绘实现，不能据此保证原生 Alt+Space/系统菜单合同保持。
- [WM_NCCALCSIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize)：返回0且保持整窗RECT可把标准frame/caption移除；使用 DwmExtendFrameIntoClientArea 的扩展frame另有生命周期。现有项目需要保留外框，不能无条件全窗客户区化。
- [WM_NCACTIVATE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate)：官方记载传 lParam=-1 给 DefWindowProc 可禁止本次默认非客户区重绘。
- [.NET WPF WindowChromeWorker 第一方源码](https://github.com/dotnet/wpf/blob/main/src/Microsoft.DotNet.Wpf/src/PresentationFramework/System/Windows/Shell/WindowChromeWorker.cs)：其 activation 处理使用上述 -1；SETTEXT/SETICON 临时撤销 WS_VISIBLE 来避免默认caption重绘；UseAeroCaptionButtons 主要决定是否交 DwmDefWindowProc 命中处理，不等于隐藏按钮绘制。其 NonClientFrameEdges 实现及注释专门保留了 Win7 frame/多屏最大化边界。这是可借鉴的重绘控制证据，不能视为本项目 Win7 实机已通过。
- [DWM rendering policy](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmncrenderingpolicy)：禁用整个非客户区渲染会丢失所需系统外框，故本轮不使用 DWMNCRP_DISABLED 或关闭 composition 处理重复按钮。
- [DwmExtendFrameIntoClientArea](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea)：composition 切换后需要重应用；现有 WM_DWMCOMPOSITIONCHANGED 的 frame 重算与主题序号刷新应保留。
- [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)：BORDER_COLOR 等现代颜色属性在老系统不受支持；现有 DwmSetWindowAttribute 函数本身 Vista 已存在，老系统失败回退即可，不能把现代枚举值等同新增 Win7 缺失 DLL 入口。

## 已实施的兼容部分

1. `Window.cpp/.cppm` 导出 `UsesLegacySettingFrame()`：动态查询并缓存 ntdll 的 RtlGetVersion，Windows 6.1 及更早为旧 frame；查询失败保守沿用原生完整 frame。不依赖未初始化 hardwareInfo 或版本 manifest，不新增运行时 DLL 导入。
2. `Setting.Layout.h::ResolveVisibleWindowFrameInsets` 增加末尾可选 `useThinTopBorder=true`；现代调用保持原逻辑，旧系统保留四周原 sizing inset，最大化与经典 frame 逻辑保持。
3. `QuerySettingClientFrameInsets` 统一供创建、NCCALCSIZE 和最小尺寸计算消费，并传入平台模式，避免创建与运行期使用两套几何。
4. `setting_session_state_tests.cpp` 增加随原生 frame 宽度变化的旧 Aero round-trip、最小 outer extent 与 top resize/client 边界回归，非只断言样式常量。
5. 按主会话随后确定的方案补充 legacy caption 样式处理：以标准 WS_OVERLAPPEDWINDOW 创建隐藏 HWND，先 GetSystemMenu(FALSE) 建立窗口持有的菜单副本，再仅从当前 GWL_STYLE 清除 WS_CAPTION，保留其余 sizing/min/max/sysmenu 位；复用创建流程已有的 SWP_FRAMECHANGED 与尺寸校正，最后才允许显示。现代系统完全不进入该分支。
6. GetSystemMenu 或 SetWindowLongPtr 失败会记录错误码到现有调试输出/stderr，并走既有 RollbackCreation，不静默沿用未知样式或销毁全局 DWM 状态。
7. 现有 window_tests.cpp 的真实 HWND 样式检查按当前 legacy 策略适配，并确认原生菜单句柄有效、SC_CLOSE/MINIMIZE/MAXIMIZE 条目仍存在；该 native 测试本轮未执行，不把其存在当作已验证。
8. 主会话负责为 captionless legacy 的 Alt+Space、标题右键、图标菜单提供显式 TrackPopupMenuEx/WM_SYSCOMMAND 路径，并维护菜单项启用状态。这样不依赖 DefWindowProc 在无 caption 样式下是否自动弹菜单，不使用 NOSYSMENU，不另建菜单资源。
9. 这些修改保留 UTF-8 无 BOM、CRLF，git diff --check 通过；编译/测试由主会话执行。

## 明确保留的验证边界

- 原生按钮的隐藏最终采用旧系统显式去 WS_CAPTION，而非未经证实的 WTNCA 组合。首次创建仍满足原生菜单初始化的 WS_CAPTION 条件，随后由主会话显式展示既有菜单。
- [GetSystemMenu 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmenu)说明 FALSE 取得当前窗口持有的菜单副本，TRUE 才重置并销毁该副本；本轮不调用 TRUE。保留句柄并明确跟踪菜单命令，仍需 Win7 实机确认最终交互。
- [SetWindowLongPtr 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowlongptrw)说明修改 cached style 后须配合 SetWindowPos；项目现有首次 SWP_FRAMECHANGED 正好处于去 caption 后、显示前。
- 另一种形态是保留原生 Aero caption+按钮而隐藏 ImGui 的重复按钮，但主会话未采用此外观方向。
- 需要实际 Win7 Aero/非Aero、激活失活、主题/合成切换、还原/最大化、多屏/DPI、Alt+Space 和三个系统按钮的实机证据；本轮未运行任何 GUI。
