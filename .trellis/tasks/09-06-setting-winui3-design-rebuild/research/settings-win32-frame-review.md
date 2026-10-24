# 设置 Win32 外框独立审查（2026-10-24）

Active task: `.trellis/tasks/09-06-setting-winui3-design-rebuild`。只读审查当前外框修改，仅写入本研究记录；未执行编译、程序、GUI/Computer Use 或 commit/push。上轮已提交的缩放/开屏策略、既有两张 CSO 和未跟踪日志不属于本轮发现范围。父任务继续 `in_progress`。

## 结论

本次静态审查未发现有充分代码/第一方证据支持的阻断问题。不能把本结论或现代 ARM64 编译、无窗口几何回归视为 Win7 Aero 实机通过。图标菜单在 modal 跟踪期间的真实单/双击序列、系统键盘交互及 captionless Aero 最终外观保留实机验收边界。

## 已核对的实现

1. **现代主题边框**：`ApplySettingFrameTheme` 将 `DWMWA_BORDER_COLOR` 恢复 `DWMWA_COLOR_DEFAULT`，不再用固定灰色覆盖系统策略；原有主题/合成消息继续更新主题序号。设置深色 caption/text 与原生材质未被扩大修改。
2. **版本与 API**：`UsesLegacySettingFrame` 动态解析并缓存 `RtlGetVersion`，仅 Windows 6.1 及更早进入 legacy，查询失败保守保留完整 frame；没有新增 Win7 缺失的静态 API 导入。`TrackPopupMenuEx`、GetSystemMenu、EnableMenuItem、Get/SetWindowLongPtr、GetProcAddress 等所用入口在 Win7 之前已存在。不能因现代 DWM 枚举值而误判新增入口缺失，旧系统属性失败仍沿用原有回退。
3. **创建与几何**：Setting 创建分支强制使用不带 WS_VISIBLE 的标准样式，首次显示前建立 HWND 的系统菜单副本、只清除 WS_CAPTION。SYSMENU、THICKFRAME、MIN/MAXBOX 等其他位保留；GetSystemMenu/SetWindowLongPtr 失败走既有 RollbackCreation。去 caption 后复用首次 FRAMECHANGED、实测 outer/client 校正，再执行显示；现代分支不改变 caption 样式。
4. **旧 Aero 与现代顶部**：创建、NCCALCSIZE、最小尺寸共用 `QuerySettingClientFrameInsets`。legacy 不再把顶部压成一像素，而保留四边 sizing inset；最大化及无合成模式依旧保持原 frame。实际 resize 命中仍使用同一 outer/client 边界及原 sizing metric，未删除八方向 resize。
5. **菜单所有权**：使用 HWND 自身 `GetSystemMenu(FALSE)` 副本，没有另造资源、DestroyMenu 或 `GetSystemMenu(TRUE)` 重置；菜单随原窗口生命周期。每次展示前以及 `WM_INITMENU` 都按 normal/maximized/minimized 和实际 style capabilities 刷新条目状态。
6. **键盘与鼠标路由**：仅 legacy 处理 Alt+Space/SC_KEYMENU、自绘 HTSYSMENU 单击、标题栏/图标右键；显式选择通过 WM_SYSCOMMAND 送回原路径，SC_CLOSE 仍转 Hide。HTSYSMENU 双击分支也送 SC_CLOSE；实际 modal 鼠标序列仍待 Win7 验证，本文不宣称仅有此分支即可证明双击交互已实测。
7. **线程/锁**：新增 ShowLegacySettingSystemMenu 的入口位于 WndProc 取得 settingImguiMutex 之前；几何 helper 的 mutex 在返回 geometry 时已释放，TrackPopupMenuEx 不跨 modal 调用持有该锁或 ImGui 锁。SetForegroundWindow 和菜单通知的同步重入不落在新增的长临界区内。内部菜单循环和 Window Service 的异步命令调度仍需实机覆盖，未额外引入同步等待渲染线程。
8. **回归边界**：SessionState 测试覆盖 normal/max/min/缺少 capabilities，几何测试验证 legacy frame 的 round-trip/min extent/top client 边界。真实 HWND 的条件样式/菜单存在检查已写入 window_tests，但本轮只允许编译，不得执行该 GUI 测试。测试结果由主会话统一登记。

## 第一方依据

- [DWM window attributes](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)：默认边框色交还系统的依据；不自行推导用户关闭系统着色偏好时一定出现强调色。
- [GetSystemMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmenu)：FALSE 返回 HWND 当前使用的菜单副本，TRUE 才重置并销毁旧副本；应用可在 WM_INITMENU 中自行更新标准条目启用状态。
- [TrackPopupMenuEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenuex)：RETURNCMD 返回用户选中 ID，取消/失败返回 0；采用屏幕坐标、原生菜单 owner HWND；API 最低支持客户端早于 Win7。
- [WM_SYSCOMMAND](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-syscommand)：标准命令应走 DefWindowProc，低四位须屏蔽；SC_KEYMENU 的 lParam 是 Alt 组合字符。现有代码继续对 SC_CLOSE 特别处理，保留设置 Hide 语义。
- [WM_NCLBUTTONDBLCLK](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nclbuttondblclk)：非客户区双击不要求 CS_DBLCLKS；但鼠标已被捕获时不会投递这一消息，故菜单 modal 期间图标双击仍须实机核实，不能按 class 未设 CS_DBLCLKS 报假问题。
- [Microsoft menu hot-tracking documentation](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/Controls/cc-faq-iemenubar.yml)：TrackPopupMenu 的 modal 跟踪并不将所有低层鼠标/键盘消息直接交回应用。它支持上述验收边界，不单独证明本项目系统菜单双击已失效。
- [Window styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-styles) 与 [SetWindowLongPtrW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowlongptrw)：caption、sizing、系统菜单的样式边界及 style cache 需 FRAMECHANGED 的依据。先建立系统菜单再去 caption、显式展示保留副本的最终 Win7 行为仍按实机验收处理。

## 待实机验收

Win7 Aero/非Aero、获焦/失焦、主题/合成切换、还原/最大化/最小化、多显示器与 DPI；Alt+Space、Esc、菜单 Move/Size/Restore/Minimize/Maximize/Close、标题/图标右键、图标快速双击、caption 双击最大化和拖动/Snap、菜单打开期间 Hide/Show 及退出。当前未运行任何这些 GUI 场景。
