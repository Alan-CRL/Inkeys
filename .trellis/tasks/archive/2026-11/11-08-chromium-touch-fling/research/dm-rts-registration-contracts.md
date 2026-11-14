# Research: DM / RTS 注册合同与真实上游接线

- Query: CreateViewport / Activate / Deactivate 是否改变 WM_POINTER / WM_TOUCH / RTS 递送；Activate 引用计数、RTS 同 HWND 绑定，以及 E_ACCESSDENIED 的公开可验证来源。
- Scope: mixed（Microsoft 官方 API、官方样例，Chromium / Flutter 上游实现，当前 owner 生命周期；不改产品代码、不编译、不 GUI）
- Date: 2026-11-12

## Findings

### 1. 三层独立控制不能当作一个可互换的开关

Microsoft 公开合同能直接确认的边界：

| 层 | 公开 API 与合同 | 不能外推的内容 |
| --- | --- | --- |
| Viewport 创建 / 关联 | CreateViewport 创建 viewport 并关联主 app HWND | 文档没有公开其内部 USER32 注册步骤，未说明会停用 RTS |
| HWND 的 DM 处理 | Manager Activate / Deactivate 开启 / 关闭指定 HWND 的 DM 输入与 callback；两者引用计数 | Deactivate 文档未承诺恢复已有 RTS 的注册或 callback；不能把 Disable viewport 当作 Deactivate |
| 单个 viewport | Enable / Disable 控制该 viewport 输入；Disable 立即停止变换并移到最终位置 | 不承诺改变 manager 对 HWND 的 activation 计数，更不承诺重建 RTS |

来源：[CreateViewport](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-createviewport)、[Activate](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-activate)、[Deactivate](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-deactivate)、[Disable](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-disable)。

当前 `Draw3.WindowControl.cpp:399–400` 在一个 owner 成功初始化时 Activate 一次，并记录 managerActivated；`:473` 在 Shutdown 时按该标记 Deactivate，成功才清标记（`Draw3.TouchpadNavigation.h:43–46`）。通常离开移动模式在 `Draw3.WindowControl.cpp:421–435` 只 Disable viewport。故“manager 仍激活”是确定生命周期事实；“它因此停掉 RTS”仍需系统调用链或对照证据。

当前源码没有每帧调用 Activate 的路径，不能把文档的引用计数警告误写成已发现 Activate 泄漏。若错误发生在 Activate 之前，当前 managerActivated=false，Shutdown 不会调用 Deactivate；公开合同不足以判断此前 CreateViewport 的内部注册是否需要额外操作才能恢复 RTS。若失败 Activate 本身部分改变系统状态，现有最终 HRESULT 同样无法判定；不可凭猜测重复 Deactivate 或重建 RTS 当成已证实修复。

### 2. Microsoft 官方样例把 mouse-in-pointer 作为另一个显式选择

[TouchInputDirectManipulation / AppWindow.cpp](https://raw.githubusercontent.com/microsoft/Windows-classic-samples/main/Samples/TouchInputDirectManipulation/cpp/AppWindow.cpp) 的实际初始化在源行49先显式 EnableMouseInPointer(TRUE)，然后创建窗口；源行379起才创建 DM manager 和 viewport，源行523 Activate。源行244–270对键盘 / 滚轮使用特殊 contact 标识、ProcessInput，再 ReleaseContact。

该样例不包含 RTS。它证明上游对 Pointer 输入模式作了独立选择，不能证明“只调 DM Activate 即把普通鼠标整体切成 Pointer”，也不能证明 DM 与 RTS 可共存。

[EnableMouseInPointer](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enablemouseinpointer) 的实际合同是进程级一次性选择，普通桌面进程通常默认关闭；有 mshtml.dll 等特定例外。读取 IsMouseInPointerEnabled 可验证状态，不能为调查随意调用 EnableMouseInPointer，因为那会改变进程状态，且不能当场反转。

### 3. Flutter 两代实际接线证明的是窗口消息共存，没有证明 RTS 共存

本轮优先核对当前 `flutter/flutter` 主仓，而非仅沿用已归档 `flutter/engine` 或生成 API 网页：

- [当前 FlutterWindow](https://raw.githubusercontent.com/flutter/flutter/master/engine/src/flutter/shell/platform/windows/flutter_window.cc)：源行567–630直接把 WM_POINTERDOWN / UPDATE / UP / LEAVE 转成 Touch / Pen 等实际业务输入；源行632起保留传统 Mouse 业务入口；源行768–777只将 PT_TOUCHPAD 命中交给 DM。
- [已归档 engine/main FlutterWindow](https://raw.githubusercontent.com/flutter/engine/main/shell/platform/windows/flutter_window.cc)：源行484 RegisterTouchWindow；源行518–546用 WM_TOUCH 创建 Touch 业务输入；Mouse 另走 WM_MOUSE；源行663–673关联 PTP 给 DM。它与当前主仓不是同一代代码，引用时必须区分。
- [DM owner](https://api.flutter.dev/windows-embedder/direct__manipulation_8cc_source.html)：Init 含 CreateViewport / Activate / Enable，Destroy 单独执行 viewport Disable / Abandon 与 manager Deactivate；它不是 RTS producer。

这给出重要兼容边界：Flutter 成功的 DM 方案依赖自己消费系统窗口消息建立正常 contact，而 Inkeys 的正常 RTS 档将窗口消息只用于光标/触觉，真实 drawing contact 仍依赖 RTS。直接搬入 fake viewport 不能把后半 contact producer 的可达性一起搬过来。

### 4. Chromium 也没有给 Inkeys 一个 RTS 共存保证

[当前 DirectManipulationHelper](https://chromium.googlesource.com/chromium/src/+/main/content/browser/renderer_host/direct_manipulation_helper_win.cc) 当前 blob `c47ef8b241a4ef038794b02a793fbd6843ffc6db`：CreateInstanceImpl 源行76–130在 CreateViewport 后建立错误清理，再 Activate / Enable；Activate 成功后建立独立 Deactivate 错误清理。OnPointerHitTest 只关联 PT_TOUCHPAD；Destroy 明确 Abandon 后 Deactivate。

[Chromium LegacyRenderWidgetHostHWND 真实集成](https://chromium.googlesource.com/chromium/src/+/e6d81f47c87/content/browser/renderer_host/legacy_render_widget_host_win.cc) 在 Init 根据 WM_POINTER / WM_TOUCH 方案决定是否 RegisterTouchWindow，再建立 DM helper。这与 Flutter 一样是窗口输入体系，不能替 RTS 收包提供保证。

可从上游确认的部分是 owner 生命周期、单独清理 activation 及 PTP 选择；不能推导公开文档未声明的 DM / RTS 同 HWND 注册合同。

### 5. RTS 同 HWND 公开合同只覆盖 RTS 自己的范围和重叠规则

[IRealTimeStylus::put_HWND](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_hwnd) 用于指定收墨窗口，没有写 DM 互操作承诺。[put_Enabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_enabled) 明确多个 RTS 可以绑定同 HWND，但重叠输入矩形同时启用的行为不确定。这里说的是 **多个 RTS**，不能把它当 DM 占据另一个 RTS 矩形的证据。

[GetTabletContextIdFromTablet](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-gettabletcontextidfromtablet) 说明 context ID 的有效期依赖 RTS enable 代次，Disable / Enable 后可能改变。它提示任何未来“重启 RTS”试验都必须遵守现有 decoder / contact 生命周期；本轮不执行这种变更。

Microsoft [WPF 关闭 RTS](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/disable-the-realtimestylus-for-wpf-applications) 只明确 RTS 与 WM_TOUCH 的同 HWND 互斥。全网 primary 检索没有找到可把它扩成“DM Activate 必然切断 RTS”的合同；也没有找到微软明确保证两者可同时收同 HWND 接触的合同。结论必须保持缺证据，不能双向过度推断。

### 6. E_ACCESSDENIED 在相关 USER32 API 中有明确含义，但必须先证明 DM 实际走到该调用

本轮找到两个公开有具体 access-denied 条件的注册 API：

| API | 官方明确条件 | 与当前失败的关系 |
| --- | --- | --- |
| [RegisterTouchWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registertouchwindow) | 调用线程不拥有 HWND 时 ERROR_ACCESS_DENIED | 必须先证明实际调用、目标 HWND 和线程；不能仅凭 final HRESULT 归到这里 |
| [RegisterPointerInputTarget](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerpointerinputtarget) | 缺 UIAccess、调用线程不拥有 HWND，或同桌面同 type 已存在全局重定向目标 | 这是全桌面重定向，不是普通窗口选择 Pointer 消息；即使 DLL 有导入也不证明当前 Init 经过此路径 |

[RegisterPointerInputTargetEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerpointerinputtargetex) 已标不支持；不作为方案推荐。[RegisterPointerDeviceNotifications](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerpointerdevicenotifications) 只是设备增减/范围通知注册，不等于接触输入路由注册。

CreateViewport / Activate 官方文档没有列出 E_ACCESSDENIED 的专属触发条件。当前源码只有最终 HRESULT，故仍不可确认具体失败 API；`0x80070005` 也不自动意味着需要进程提权、改文件 ACL 或改 COM 安全配置。本轮未做这类系统变更。

### 7. 主会话补充的匹配系统二进制证据：Activate 确实进入 HWND 输入委托层

主会话对本机 `DirectManipulation.dll 10.0.26100.9818` 与微软匹配 PDB 的静态分析已确认以下调用链（本代理未重复执行）：

- `CManagerImpl::Activate`，RVA `0xDFE0`，调用 `DelegateInput`（RVA `0xE278`，经 thunk `0x73498`），参数包括 `w4=0x1000`、`x3=HWND`；委托失败走 `ResultFromKnownLastError`。
- `CManagerImpl::_ClearWindow`，RVA `0xFC98`，调用 `UndelegateInput`（RVA `0xFCB0`）。
- CreateViewport 没有直接 DelegateInput；已检查的 `_Initialize` 仅涉及 input / update manager。
- 该 DLL 的 private USER32 导入没有 RegisterPointerInputTarget。因此前节该公开 API 的 UIAccess / 全局重定向权限条件 **不应用作本次失败的成因推断**。

这比只看导入表更强：已把当前高优先候选具体化到 **Manager Activate 的 HWND 输入委托操作**，其失败也确实可能从 Win32 last error 映射为 HRESULT。它仍没有证明本次 0x80070005 一定出自该调用，也未证明委托对同 HWND 的 RTS callback 有何副作用；`0x1000` 的确切语义在当前证据中未解出。不得将 private 参数命名为猜测的“禁 RTS 标记”。

## Files Found / Code Patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:378`：真实 DM 初始化链与最终 HRESULT；`:399` activation；`:421` viewport gate；`:466` Shutdown。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadNavigation.h:43`：独立 managerActivated 退休责任。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:2706`：RTS put_HWND；`:2718` all-tablets；`:2861` Enable。
- 先前本任务 [drag-failure-frontend-investigation.md](drag-failure-frontend-investigation.md)：新现场日志时间关系、Pointer 光标独立路径；本文件只补注册合同研究，不重写 flags 说明。

## Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：RTS 唯一 producer 与窗口光标路径的职责。
- `.trellis/spec/native-desktop/canvas-pan.md`：PTP 系统接线、COM owner 生命周期与设备实测边界。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：成功初始化与 partial-init 停产责任区分。

## Caveats / Not Found

- 没有公开 Microsoft DM 实现源可直接引用内部 HWND 注册与 legacy stylus 递送切换；Wine 的同名实现包含 stub，不能当 Windows 实现证据。
- 本轮未取得明确的 DM / RTS 同 HWND 互斥或共存合同；主会话已锁定 Activate→DelegateInput，但确切失败点与 RTS 影响仍需后续证据。
- 没有 GUI、系统注册设置变更、输入注入、编译或产品代码改动；只新建本研究文件。系统 DLL 导入/反汇编由主会话负责，本代理未重复执行。
