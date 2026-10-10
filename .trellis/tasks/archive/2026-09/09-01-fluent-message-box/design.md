# 技术设计：全环境 Fluent 自绘 MessageBox

## 1. Architecture And Invariants

新增模块使用 `Inkeys.UI.MessageBox`，目录为 `Inkeys/Inkeys/UI/MessageBox/`，公开命名空间为 `Inkeys::UI::MessageBox`。仓库已有 `Inkeys.Message` 消息队列模块，新名称不得与其合并或复用其生命周期。

组件边界：

```text
product caller
  -> validate Request
  -> process-wide admission gate
  -> temporary MessageBox UI thread
       -> private GDI+ runtime
       -> copy-backed decode/layout preflight (no HWND)
       -> private opaque 32-bit DIB + memory DC
       -> one fixed-size top-level HWND
       -> local message loop
  -> Result

any rejected/failed path
  -> one non-recursive MessageBoxW fallback
```

核心不变量：

- 模块只依赖 Win32、DWM、GDI/GDI+、资源 API 和 C++ 标准库，不导入 `Inkeys.Window`、`Inkeys.Display`、`Inkeys.UI.RenderPipeline`、Bar、Setting、Draw3 或产品日志模块。
- 调用方负责把 owner HWND 与故障策略传入；组件不主动查询产品单例或全局 HWND。
- 每次自绘显示只创建一个 HWND、一套按次绘图对象和一个临时 UI 线程；完成时先隐藏，再销毁，不复用隐藏窗口。
- 全进程同时最多存在一个自绘 MessageBox。普通调用串行，崩溃/重入路径不等待现有对话框。
- 自绘入口与系统回退严格分层；fallback 函数不得再次调用公开 `Show()`。
- 自绘窗口始终为不透明普通顶层窗口，不使用 `WS_CHILD`、layered-window 逐像素透明、`SetWindowRgn` 或辅助阴影 HWND。

建议文件边界：

- `MessageBox.cppm`：公开枚举、请求视图和同步 `Show()`；同一 interface unit 内放置不导出的 `Detail` 声明，供本模块 implementation units 共享。
- `MessageBox.cpp`：请求规范化、并发门、fallback 映射、线程协调与公开入口。
- `MessageBox.Layout.cpp`：无 HWND 的 token、测量、像素取整、按钮槽位与命中区域。
- `MessageBox.Window.cpp`：窗口类、消息循环、输入状态、DWM、GDI+ 与 DIB 绘制。
- `Inkeys/src/message_box/error.png`：崩溃提示内置透明 PNG；在 `Inkeys.rc` / `resource.h` 注册。
- `InkeysHeadlessTests/message_box_test.rc`：测试 EXE 引用同一 PNG 和共享资源 ID，使 `BuiltInError` 在测试进程中可用，不复制资产或加载 Inkeys 主程序。

三个 `.cpp` 均作为 `Inkeys.UI.MessageBox` implementation unit，复用 interface unit 中未导出的声明，不新增跨 module 私有头。测试专用声明只在测试工程宏下导出，不形成生产 API。

## 2. Public Contract

公开合同保持同步、无异常外泄，并避免暴露 Win32 按钮 ID：

```cpp
enum class Result { Ok, Cancel, Yes, No, Dismissed, Failed };
enum class Buttons { Ok, OkCancel, YesNo };
enum class Reliability { Normal, CriticalNoWait };
enum class SystemModality { Application, System };
enum class SystemIcon { None, Error };

struct FallbackPolicy {
    HWND owner{};
    SystemModality modality{SystemModality::Application};
    SystemIcon icon{SystemIcon::None};
};

struct Request {
    const wchar_t* title{};
    const wchar_t* body{};
    HWND owner{};                         // 仅为自绘 owned 顶层窗口
    bool requireOwner{false};             // owner 缺失时直接进入 fallback
    Buttons buttons{Buttons::Ok};
    Result defaultResult{Result::Ok};
    bool dismissEnabled{true};
    Result dismissResult{Result::Ok};
    bool showCloseButton{true};
    IconSource icon{};
    bool ownerlessTopmostAtCreation{false};
    Reliability reliability{Reliability::Normal};
    FallbackPolicy fallback{};            // 独立保留原 MessageBoxW 语义
};

[[nodiscard]] Result Show(const Request& request) noexcept;
```

以上是合同形状而非要求逐字采用的声明；实现时可按现有 module/include 约束调整 POD 排列，但不得改变以下语义：

- 模块提供 `MakeOkRequest`、`MakeOkCancelRequest`、`MakeYesNoRequest` 等命名 factory，一次生成相互一致的 buttons/default/dismiss；产品调用点使用 factory 后只补 owner、icon、reliability 与 fallback policy，避免仅修改 `buttons` 留下错误默认值。

- `title`、`body` 为 NUL 结尾字符串，在同步调用返回前有效。正常路径在启动 UI 线程前复制；若复制失败，fallback 仍可直接使用原始字符串。
- `owner` 与 `fallback.owner` 分离。Setting 的自绘 owner 是 Setting，但当前系统回退仍是 ownerless `MB_SYSTEMMODAL`；单一 owner 字段无法同时满足二者。
- Bar/Setting 等必须保持 owner 关系的调用设置 `requireOwner`；句柄为空时不得静默创建 ownerless 自绘窗口，而是在创建 HWND 前进入该调用点的系统 fallback。
- `owner != nullptr` 时禁止 `ownerlessTopmostAtCreation`；owner 无效、非本进程 HWND 或组合不合法时不创建自绘 HWND，直接 fallback。
- `Reliability::CriticalNoWait` 只接受 ownerless 请求，不执行 owner 健康探测、禁用或焦点恢复；带 owner 的 Critical 请求直接 fallback。
- `defaultResult` 必须属于当前按钮组合。`dismissEnabled == false` 时关闭按钮强制隐藏，`Esc`、`Alt+F4` 和 `WM_CLOSE` 均不结束窗口。
- `showCloseButton == false` 只隐藏 glyph；若 dismiss 已启用，`Esc`/`Alt+F4` 等 close command 仍可工作，但按按钮组合解析为已批准的安全结果，不盲目采用自定义 `dismissResult`。
- 内置按钮文字不依赖产品 i18n：通过 Win32 thread/user UI language 在 `zh-CN`（确定/取消/是/否）、`zh-TW`（確定/取消/是/否）和英文（OK/Cancel/Yes/No）三组中选择，其他语言回退英文；自定义按钮文字和更多 locale 留待后续扩展。
- 像素图标入口在 `Show()` 内完成受检复制。资源图标使用“模块句柄 + PNG resource type/name(ID)”描述，调用方不传文件路径或需托管的 `HBITMAP`；任何 icon-only 校验/复制失败都按无图标继续，不把装饰错误升级为整框 fallback。
- 若自绘与 `MessageBoxW` 都失败，返回 `Result::Failed`。

默认组合：

| Buttons | 左到右槽位 | 默认按钮 | 默认 dismiss |
| --- | --- | --- | --- |
| `Ok` | Primary=`OK` | `Ok` | `Ok` |
| `OkCancel` | Primary=`OK`, Secondary=`Cancel` | `Ok` | `Cancel` |
| `YesNo` | Primary=`Yes`, Secondary=`No` | `Yes` | 禁止；显式启用时为 `Dismissed` |

## 3. Request Normalization And Admission

`Show()` 按固定顺序执行，保证失败决策可测试且不会先创建半成品窗口：

1. 只做不分配绘图资源的结构校验：文本指针、按钮/default/dismiss 组合、owner 进程归属和图标尺寸/stride 溢出。
2. 按 `Normal`/`CriticalNoWait` 规则取得进程级 admission gate；无法取得时直接走 fallback。
3. 在门内复制标题、正文、像素图标或 PNG resource bytes；此阶段不创建 GDI+ 对象。
4. 启动临时 UI 线程。该线程确定目标 monitor/work area/DPI，初始化私有 GDI+，解码 PNG 并用最终字体完成布局测量，但仍不创建 HWND。
5. UI 线程向调用线程报告 `PreflightReady` 或明确 fallback reason。标题超过两行、正文/整体高度超限或绘图初始化失败时，线程清理后退出；已取得门的协调器在门内完成系统回退，确保普通调用仍串行。
6. 只有 `PreflightReady` 后才执行 owner 模态准备并允许 UI 线程创建 HWND；所有后续失败路径同样先恢复 owner、清理线程，再由协调器调用 fallback，最后释放门。

并发规则：

- `Normal` 使用进程内互斥门串行等待；等待期间尚未创建第二个自绘 HWND。
- 线程局部 reentry guard 在同一线程再次进入 `Show()` 时直接 fallback。
- `CriticalNoWait` 只尝试取得门；已有对话框、锁异常或协调器状态不完整时立即 fallback，不等待也不抢占现有窗口。
- `FallbackToSystem()` 本身从不获取或释放 admission gate。已获准的 Normal/Critical 请求由外层协调器持门直到系统 fallback 返回，从而保持普通调用串行；因 busy/reentry 而从未取得门的 Critical 旁路可直接 fallback。

## 4. Temporary Thread And Modal Owner Handshake

临时 UI 线程拥有窗口类实例、HWND、消息循环、DIB、GDI+ 对象和交互状态。公开调用线程只负责 admission、owner 模态握手和等待最终结果。

有 owner 的正常路径在 UI 线程完成无 HWND preflight 后开始：

1. 再次校验 HWND 有效、属于当前进程且未销毁；记录 owner thread/process、原始 enabled/foreground 状态。恢复前再次比对，避免对已销毁后复用的 HWND 操作。
2. owner 与调用线程不同时，先用短时有界 `SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG | SMTO_BLOCK)` 健康探测。超时则不跨线程禁用 owner，通知 UI 线程取消并在其退出后 fallback。
3. 在公开调用线程禁用 owner；owner 本来已禁用时不负责重新启用。成功后发出 `OwnerPrepared`，UI 线程才创建 owned 顶层窗口。
4. UI 线程创建窗口并运行消息循环；调用线程等待“窗口已隐藏/结果已确定”事件。若调用线程正是 owner thread，不使用裸 `WaitForSingleObject`，而以 `MsgWaitForMultipleObjectsEx(..., QS_SENDMESSAGE, ...)` 配合 `PeekMessage(PM_NOREMOVE)` 只服务跨线程 sent-message，避免激活/owner 通知互锁；不得取出和分派普通 posted/input 命令。
5. 用户完成操作后，UI 线程写入结果并以 `SetWindowPos(..., SWP_HIDEWINDOW)` 隐藏；create-time topmost 的 ownerless 窗口先用独立 `HWND_NOTOPMOST` 调用退出 topmost band。随后发出隐藏事件，等待 owner 恢复确认。
6. 调用线程按记录恢复 owner enabled/foreground/focus 状态并发出确认；UI 线程等待该确认时也只服务 `QS_SENDMESSAGE`，避免调用线程的激活恢复同步回调 UI 线程而形成反向互锁，然后销毁 HWND 和图形资源。
7. UI 线程退出，调用线程 join 后返回结果。

此双阶段握手和 sent-message-aware wait 共同避免 Bar 的 HWND 所在线程阻塞在同步 `Show()` 时，由对话框线程的激活/owner 通知或同步 `EnableWindow` 形成反向死锁；受限等待不构成完整嵌套消息泵。Setting business worker 属于跨线程 owner 场景，健康探测与所有退出分支的恢复守卫必须同时存在。

ownerless 路径不执行 owner 禁用、健康探测或恢复握手。它在揭示前记录原前台 HWND；仅当本框确实取得前台、提交时仍是前台且原 HWND 仍有效时，隐藏成功后 best effort 归还前台，用户已切换到其他窗口时不得抢回。崩溃路径一律使用 ownerless + `CriticalNoWait`，不探测或等待可能已失效的 UI 线程。

线程创建、窗口创建或结果提交前的消息循环异常：若 HWND 已出现，先隐藏并 best effort 清理；若 owner 已禁用，先恢复；随后进入 fallback。若用户结果已经原子提交，后续隐藏/清理错误只能记录并返回该首个结果，不得再弹系统框或覆盖选择。公开边界捕获 C++ 异常并返回/回退，但不宣称可从堆破坏、栈破坏、loader lock 或任意访问冲突中恢复。

## 5. Private GDI+ And Icon Pipeline

GDI+ 生命周期完全属于 MessageBox 模块：

- Windows 8 及以上每次显示使用独立 `GdiplusStartup`/`GdiplusShutdown` RAII。
- Windows 7 或版本探测失败时，使用模块私有 `INIT_ONCE` token，首次需要时初始化并保留到进程退出，不在两次对话框之间循环 shutdown；不复用 `Graphics.Surface` 的 token。
- HDC、memory DC、top-down 32-bit DIB、`Gdiplus::Graphics`、Font/Brush/Pen/Bitmap、资源字节和布局状态都按次销毁。
- 所有句柄采用局部 RAII，销毁顺序为 GDI+ 对象 -> 恢复 DC selected object -> DIB/DC -> HWND/线程状态。

图标管线：

- 像素入口只接受 32-bit premultiplied BGRA、正 stride，执行 `width * 4`、`stride * height` 与总字节数溢出检查后复制；MVP 将输入限制在 `512 x 512`、总像素缓冲不超过 `4 MiB`，超限时省略图标。
- PNG 资源使用 `FindResourceW/LoadResource/LockResource` 取得字节，复制到组件自己的 movable `HGLOBAL`，通过 `CreateStreamOnHGlobal` 交给私有 GDI+；stream 保持到临时 Bitmap 销毁，再立即转存为 `PixelFormat32bppPARGB` 自有像素。只复用已验证的 Win32 解码方式，不复用 `Graphics.Surface` 对象或 runtime。
- 解码后的图标按比例装入 `40 DIP x 40 DIP` 槽位，使用高质量缩放，不改变正文测量语义。
- 解码/复制失败只清空 icon；标题、正文、按钮继续自绘。内置错误 PNG 失败时同样无图标降级。

## 6. Fixed Win32 Frame And DWM

窗口保留 DWM 可识别的标准顶层 frame style（`WS_CAPTION | WS_SYSMENU | WS_THICKFRAME`），不设置 minimize/maximize box；自绘通过 `WM_NCCALCSIZE` 扩展客户区。`WS_THICKFRAME` 仅用于稳定保留 DWM 外框资格，用户行为仍严格固定尺寸：

- `WM_NCHITTEST` 先调用 `DwmDefWindowProc`，再按自绘关闭按钮、标题拖动区和客户区覆盖结果；永不返回八个 resize hit-test。
- `WM_GETMINMAXINFO` 把 min/max track size 固定为当前布局尺寸。
- `WM_SYSCOMMAND` 拦截 `SC_SIZE`、`SC_MINIMIZE`、`SC_MAXIMIZE`；caption 双击不改变尺寸。
- `SC_CLOSE` 与 `WM_CLOSE` 都转入统一 `TryCloseCommand()`；dismiss 禁用时同步灰掉 system menu 的 Close，避免出现可点击但无效的系统入口。
- 顶部未落入 close hitbox 的拖动区域返回 `HTCAPTION`，正文、图标和按钮区域返回 `HTCLIENT`。
- Windows 11 动态设置 immersive dark mode 与 `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`；API/attribute 不可用时静默退化。
- DWM 负责真实外部阴影与系统圆角；客户区只在同一条像素对齐的闭合路径内绘制 `1 DIP` 边框，四边不分别计算。

首个实现优先采用保留 style bits、全客户区自绘的方案；若 Windows 11 可见验证发现 full-client `WM_NCCALCSIZE` 使 DWM 阴影消失，唯一允许的调整是保留最小非客户区 inset 并重新校准内边框。不得改用 layered window、region 或额外阴影 HWND。

窗口 extended style：

- 所有模式使用 `WS_EX_TOOLWINDOW`，不创建任务栏入口，也不使用 `WS_EX_NOACTIVATE`。
- owned 模式不设置 `WS_EX_TOPMOST`，依赖 owner 根的统一 topmost band。
- 指定 ownerless 故障模式仅在 `CreateWindowExW` 时附加 `WS_EX_TOPMOST`，后续不启动定时刷新或调用产品 TopWindow。

### 首帧可见时序

`CreateWindowExW` 后的 HWND 必须继续保持隐藏，直到 DWM 属性、system menu、最终窗口矩形和首帧 DIB 都准备完成。不能使用 `ShowWindow(hwnd, SW_SHOWNORMAL)` 揭示窗口：当它是进程首次 `ShowWindow` 时，Windows 允许启动器通过 `STARTUPINFO.wShowWindow` 覆盖该参数，可能把固定尺寸对话框瞬时最大化，而 DIB 仍只有预检尺寸，未覆盖客户区会暴露原始 frame 或异常底色。

揭示顺序固定为：

1. 应用 dark mode、圆角、DWM frame 与 Close system-menu 状态；支持 `DWMWA_CLOAK` 时先 cloak，旧系统不支持时静默退化。
2. 使用带最终 `x/y/width/height` 的 `SetWindowPos(..., SWP_SHOWWINDOW)` 建立固定可见矩形，绕过首次 `ShowWindow` 的 `STARTUPINFO` 语义；支持 cloak 时该 HWND 虽有 `WS_VISIBLE`，DWM 仍不向用户显示。
3. 使用 `RedrawWindow(..., RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME)` 同步触发首次 `WM_PAINT`，且只有完整 DIB `BitBlt` 成功后才能继续；现代 DWM 路径随后解除 cloak，旧系统无 cloak 时在同一调用栈内立即完成绘制。
4. 任一步失败时保持或立即恢复隐藏，完成 owner 恢复、HWND/绘图资源清理后进入既有非递归系统 fallback。所有关闭路径同样使用 `SetWindowPos(..., SWP_HIDEWINDOW)`，避免首次 `ShowWindow(SW_HIDE)` 再次消费启动状态；ownerless topmost 在隐藏前先用独立 `HWND_NOTOPMOST` 调用退出 topmost band。

该时序只使用标准 HWND、DWM 与 GDI backing surface；不引入 layered window、window region、辅助阴影窗口或手工阴影。生产 reveal 路径不得在 cloak 状态调用 `DwmFlush`，因为此时没有可保证到达的 Present，故障提示不能为合成等待而无限阻塞。Windows 7 不支持 cloak 时仍保留“固定矩形 SetWindowPos + 同步首帧”的兼容路径。

## 7. DPI, Placement And Layout

组件动态解析 DPI API：有 owner 时优先 `GetDpiForWindow`；ownerless monitor 使用动态加载的 `GetDpiForMonitor(MDT_EFFECTIVE_DPI)`，再回退 `GetDpiForSystem` 或 monitor DC 的 `LOGPIXELSX/Y`。不调用 `SetProcessDpiAwareness*`；必要时只在临时 UI 线程上使用可用的 thread DPI context，并在退出前恢复。

monitor 选择：

1. 有 owner：`MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)`。
2. ownerless：当前前台窗口所在 monitor。
3. 无有效前台窗口：光标所在 monitor。
4. 仍失败：主显示器。

最终矩形在对应 work area 居中并 clamp。`WM_DPICHANGED` 使用 suggested rect 所在 monitor 重新测量、重建 DIB 并整体换算命中区域；固定格式元素都用 DIP 保存、按统一规则转整数像素。

布局合同：

- 外框 `320-548 DIP` 宽、`184-756 DIP` 高；若 work area 连最小尺寸也无法容纳，则 fallback。
- 上部内容区与下部 command area 均为不嵌套的平面区域；padding `24 DIP`。
- 标题 `20 DIP Semibold`，最多两行；标题字体依次尝试 `Segoe UI Variable Display Semibold`、`Segoe UI Semibold`、`Segoe UI Bold`。正文和按钮 `14 DIP`，依次尝试 `Segoe UI Variable Text`、`Segoe UI`，CJK 依赖系统 font fallback。关闭 glyph 可见时，标题测量宽度预留完整 `32 DIP` hitbox 和相邻间距，不允许两者重叠。
- 标题与正文间距 `12 DIP`；可选图标只位于正文左侧，槽位 `40 DIP`，与正文间距 `16 DIP`。
- command button 高 `32 DIP`、圆角 `4 DIP`，按钮间距 `8 DIP`；当前组合内按钮等宽填满可用行。
- 外圆角 `8 DIP`、内边框 `1 DIP`。标题关闭按钮使用同一个整数顶部/右侧外间距：`round(titleTop + titleFontHeight / 2 - closeSize / 2)`，在 96 DPI 默认字体下取整前约为 `19.6 DIP`；命中区固定 `32 DIP x 32 DIP`，glyph 固定为 `10 DIP x 10 DIP`、`1 DIP` 平头线并在命中区内水平、垂直居中。glyph 不参与标题文本测量。
- 先在最小宽度测量，再按换行收益扩展到最大宽度；任何文本不得 ellipsis 或裁切。最终绘制边界还需检查不可断开的长 token/单词，无法在 `548 DIP` 内完整容纳时同样 fallback。标题超过两行或整体超过 `min(756 DIP, work-area available height)` 时，在创建 HWND 前 fallback。

## 8. Fluent Dark Rendering

每帧绘到不透明 top-down 32-bit DIB，再由 `WM_PAINT` 一次 `BitBlt` 到窗口 DC。GDI+ 只在该内存表面上使用；禁止透明客户区或跨窗口 alpha 合成。

固定 token：

| Element | Value |
| --- | --- |
| command background | `#202020` |
| content background | 约 `#2B2B2B`（`#202020` 上叠加 `#0DFFFFFF`） |
| primary text | `#FFFFFF` |
| secondary text | `#C5FFFFFF` |
| disabled text | `#5DFFFFFF` |
| neutral default/hover/pressed overlay | `#0FFFFFFF` / `#15FFFFFF` / `#08FFFFFF` |
| surface border | `#66757575` |
| section separator | `#19000000` |
| accent | `#60CDFF` |

Accent hover/pressed 使用同一青色的 Fluent secondary/tertiary alpha 层级，不引入第二套主题色。Accent 文字为黑色，neutral 文字为白色。禁用、hover、pressed、focused 状态由单一按钮状态机决定，不通过重叠绘图对象累积颜色。

文字使用 opaque surface 上的 ClearType/GridFit；边框和分隔线先做 DIP 到像素的确定性对齐。键盘焦点视觉在 accent 与 neutral 按钮上统一使用 WinUI 高可见双层闭合 stroke：`2 DIP` 白色外框与 `1 DIP` 的 `#B3000000` 深色内框，整体完全位于按钮边界外并外扩 `3 DIP`；焦点视觉不改变按钮布局、命中矩形或相邻 `8 DIP` 间距。

MVP 不运行动画 timer；hover/pressed/focus 状态在事件后即时重绘，关闭时立即隐藏，以减少崩溃提示路径的状态和线程依赖。

## 9. Input And Result State Machine

WndProc 只修改 UI 线程私有状态：

- 鼠标移动更新 hover；按下后 `SetCapture`；仅在同一按钮内释放才激活。
- `WM_CAPTURECHANGED`、失焦或取消操作清除 pressed，避免卡住。
- 状态拆为 `focusedButton`（逻辑焦点）、`keyboardFocusVisible`（输入模态）和 HWND 实际 focus。`focusedButton` 初值仍为 `defaultResult` 对应索引；首帧 `keyboardFocusVisible=false`，renderer 仅在该值为 true 且 HWND 当前拥有键盘焦点时绘制双层白色焦点框。
- `Tab/Shift+Tab` 调用同一个 enabled-button 导航 helper 并首尾循环；`Left/Right` 复用相同的跳过禁用项规则，但按 LTR 空间方向移动并在两端停止，不 wrap。即使单按钮或已在边界，一次有效导航键也会进入 keyboard focus visual。`Up/Down` 不参与水平 command group。
- `Enter` 与 `Space` 都提交 `focusedButton` 对应结果。由于逻辑焦点在创建时已指向默认按钮，所以未发生导航时仍激活默认按钮；一旦 Tab、方向键或 pointer focus 改变逻辑焦点，Enter 不得绕回 `request->defaultResult`。
- pointer down 到 command button 时更新 `focusedButton` 并切回 pointer focus visual；仅移动鼠标/hover 不清除已存在的 keyboard focus visual。`WM_KILLFOCUS` 使焦点框不可见但保留逻辑索引，重新获得实际焦点后按最后输入模态恢复；pointer 激活仍不显示键盘框。
- 标题 close 按 caption command 处理，不作为 Tab/方向键 stop。`Esc`、`Alt+F4`、`SC_CLOSE`、`WM_CLOSE` 和自绘 close glyph 全部调用一个 `ResolveCloseCommand()`；resolver 只返回“提交哪个 Result / 无操作”，最终仍经 `CommitResult()` 的单次原子门。
- 结果只允许从 `Pending` 原子地转换一次；重复点击、重复 close、销毁消息或提交后的清理失败不得覆盖首个结果，也不得触发第二个 fallback 对话框。

用户已批准并由实现采用的 close-command 表：

| 条件（按优先级） | `ResolveCloseCommand()` |
| --- | --- |
| `dismissEnabled == false` | 无操作；关闭 glyph 和 system-menu Close 保持禁用 |
| `showCloseButton == true` | 提交现有 `dismissResult`，与点击 X 相同 |
| X 隐藏且 `Buttons::OkCancel` | 提交 `Cancel`，优先安全按钮而不是自定义 dismissResult |
| X 隐藏且 `Buttons::Ok` | 提交 `Ok`，视为确认已读 |
| X 隐藏且 `Buttons::YesNo`、dismiss 显式启用 | 提交唯一合法的 `Dismissed` |
| X 隐藏且 `Buttons::YesNo`、dismiss 默认禁用 | 无操作，绝不合成为 `No` |

其他键盘入口保持明确边界：

- `Alt+Space` 继续由标准 `WS_SYSMENU` 打开窗口菜单；其中 Close 最终仍进入 `SC_CLOSE -> ResolveCloseCommand()`。禁用 close 时菜单项同步灰显。
- 数字小键盘 Enter 与主 Enter 都表现为 `VK_RETURN`；key repeat 沿用 Win32 重复消息，结果提交门保证激活只生效一次。
- 不新增 `Up/Down`、`Home/End`、`PageUp/PageDown`、`F6`、`Ctrl+W`/`Ctrl+F4` 或 access key；本地化按钮没有 mnemonic 元数据，不能凭英文首字母伪造快捷键。
- `Alt+Tab`、`Alt+Esc`、Print Screen 等系统级组合交还 Windows，不由 MessageBox 截获。

MVP 不注册 UI Automation provider。键盘焦点、焦点框、颜色对比与完整键盘可达性属于验收项，但验收报告必须明确屏幕阅读器语义不等价于 WinUI 3 ContentDialog。

## 10. System Fallback

`FallbackToSystem()` 是唯一系统入口，直接调用 `MessageBoxW` 并映射：

- `Buttons::Ok` -> `MB_OK` -> `Result::Ok`。
- `Buttons::OkCancel` -> `MB_OKCANCEL` -> `Ok/Cancel`。
- `Buttons::YesNo` -> `MB_YESNO` -> `Yes/No`。
- `SystemModality::System` 附加 `MB_SYSTEMMODAL`；`SystemIcon::Error` 附加 `MB_ICONERROR`。

系统回退精确保留文案、按钮结果、指定 owner、系统模态和 error icon；它不保证 Fluent 外观。原生 `MessageBoxW` 无法表达所有自绘 dismiss 组合：例如 Yes/No 的显式 `Dismissed` 不通过增加第三个 Cancel 按钮模拟，仍回退为系统 Yes/No 并保持系统关闭限制；可靠性优先于扩展 fallback UI。

fallback 使用原始 NUL 文本指针，独立于自绘副本；返回 0 或未知 ID 时返回 `Failed`。它不读取组件窗口状态、不自行获取自绘门、不创建第二个自绘请求，因此初始化失败、重入和异常处理器路径均不会递归。

调用 fallback 前重新校验 `FallbackPolicy::owner`；句柄已失效或不再属于当前进程时归一为 `nullptr`，其余按钮、`MB_SYSTEMMODAL` 与 error icon flags 保持不变。

## 11. Product Migration Matrix

迁移只替换当前已编译的产品调用，保留原文案与分支结果：

| Call site | Custom policy | Fallback policy |
| --- | --- | --- |
| `IdtMain.cpp:169,174,182,263` | ownerless、创建时 topmost、OK、Normal | ownerless、`MB_SYSTEMMODAL | MB_OK` |
| `IdtPlug-in.cpp:803` | 有效 Bar owner、OK/Cancel | 原 Bar owner、`MB_SYSTEMMODAL | MB_OKCANCEL` |
| `Bar.Interaction.cpp:3294` | 有效 Bar owner、OK/Cancel | 原 Bar owner、`MB_SYSTEMMODAL | MB_OKCANCEL` |
| `Setting.cpp:205` | Setting owner、OK | ownerless、`MB_SYSTEMMODAL | MB_OK` |
| `Setting.cpp:208` | Setting owner、OK/Cancel | ownerless、`MB_SYSTEMMODAL | MB_OKCANCEL` |
| `Window.Legacy.cpp:105` | ownerless、创建时 topmost、OK | ownerless、`MB_SYSTEMMODAL | MB_OK` |
| `Helper.CrashHandler.cpp:435` | ownerless、创建时 topmost、OK、内置 error icon、CriticalNoWait | ownerless、`MB_OK | MB_ICONERROR` |
| `SuperTop/IdtSuperTop.cpp:205` | ownerless、创建时 topmost、OK | ownerless、`MB_SYSTEMMODAL | MB_OK` |

Bar/Setting owner 在调用点取得并传入；组件自身不新增对 Window Service 的依赖。owner 无效时使用该行 fallback，不悄悄改为另一棵 owner 链。

排除 `Timeout/InkeysTimeout`、历史未编译源码和 `IdtMain.cpp:1685-1705` 调试辅助调用。

## 12. Testability And Diagnostics

`InkeysHeadlessTests` 直接编译 MessageBox 源文件，并只在测试工程定义 `INKEYS_MESSAGE_BOX_TESTING`。该宏可开放窄测试钩子（布局快照、状态事件注入、隐藏/可见 presentation mode、当前资源计数），生产 `Inkeys.vcxproj` 不导出这些入口。测试工程的 `message_box_test.rc` 只嵌入同一 error PNG/ID，保证资源适配器与可见 icon case 运行在测试 EXE 自身。

自动化层次：

- 纯逻辑：96/120/144/192 DPI、标题一/两行、正文换行、图标有无、三种按钮、default/dismiss 合法性、等宽按钮、闭合边框坐标、monitor clamp、超限 fallback。
- 资源：非法 stride/溢出、PNG 成功与失败、透明 premultiplied BGRA、重复显示前后 GDI handle/内部 live-object 计数回到基线。
- 隐藏 HWND：style/extended style、owner、不可 resize hit-test、`WM_GETMINMAXINFO`、`WM_DPICHANGED`、结果只提交一次、hide-before-destroy、owner 恢复顺序。
- 并发：普通调用串行；CriticalNoWait 和 thread-local reentry 不等待并进入 mockable system-fallback decision。

实现阶段已获准的可见测试使用专用入口，例如：

```powershell
.\Build\ARM64\Debug\InkeysHeadlessTests.exe --message-box-visual-test <output-dir>
```

该入口创建本进程专用的纯色 backdrop/owner，再显示 MessageBox；通过向本进程 HWND 发送消息驱动 focus/hover/pressed，不控制全局鼠标键盘。截图使用 Win32 屏幕 DC 只捕获“对话框矩形 + 阴影 margin”，且该矩形完全落在专用 backdrop 内，避免包含无关应用；编码为 PNG 后立即关闭窗口。至少生成默认 OK、Yes/No 键盘焦点、error icon 三种截图，并做非空像素、尺寸、边界与关键色覆盖检查，再对本地图片作视觉核对。

不得使用 Computer Use。不得启动 Inkeys 主程序或截取无关桌面区域。Windows 7/10 未实际运行时，只报告静态 API fallback、工程兼容构建和代码审查覆盖。

诊断不依赖产品日志。Debug/test 可使用 `OutputDebugStringW` 记录阶段和 Win32 error；Release 正常路径不逐帧输出，崩溃路径不做复杂格式化。

## 13. Compatibility, Trade-offs And Rollback

- GDI+ 文字不会与 WinUI/DirectWrite 像素完全相同；目标是 token、层级、间距与交互状态一致，而不是声称逐像素等价。
- Windows 7/10 外部圆角和阴影按 DWM 能力退化；不以自制阴影换取截图相似度。
- temporary UI thread 增加一次线程创建成本，但隔离现有渲染线程与启动顺序，且消息框不是高频路径。
- owner 模态握手比直接跨线程 `EnableWindow` 复杂，但能覆盖 Bar 调用线程阻塞与 Setting 跨线程 owner 两种真实路径。
- crash handler 只能 best effort；系统 fallback 是最后路径，不构成对任意进程破坏的可靠性承诺。

## 14. Localization Follow-up

本轮在既有 `Request` 上增加纯值语言 ID 和可选按钮文字视图。默认语言固定为 `en-US`；标题、正文和按钮指针仍只要求在同步 `Show()` 返回前有效，并在 admission gate 内复制到 `OwnedRequest`。未提供或为空的按钮文字按请求语言回退到组件内置的英文、简中或繁中标准标签。

数据流保持单向：

```text
IdtI18n + generated keys
  -> product caller resolves title/body/button strings and LANGID
  -> MessageBox Request copies the snapshot
  -> private GDI+ layout/render or MessageBoxExW fallback
```

MessageBox module 不 include/import `IdtI18n`、Setting 或 RenderPipeline。启动早期与 SuperTop 调用继续使用字面量英文和 Request 的默认 `en-US`；正常调用使用 `I18n::getWOr` 与当前语言 ID。生成键根节点使用 `Dialogs`，避免被 `<Windows.h>` 的 `MessageBox` 宏改写。CrashHandler 使用 non-blocking i18n lookup，锁忙、未加载、缺 key 或转换失败均采用英文常量。

系统 fallback 改用 `MessageBoxExW` 并传入 Request 语言 ID，使启动早期 fallback 仍请求英文系统按钮，运行期 fallback 请求产品语言。fallback 仍不接受自定义按钮文字，可靠性语义和结果映射不变。

字体不接入共享 DWrite collection，也不新增 GDI+ process font 安装。布局按 Request 语言设置候选：`zh-CN` 优先 `Microsoft YaHei UI` / `Microsoft YaHei`，`zh-TW` 优先 `Microsoft JhengHei UI` / `Microsoft JhengHei`，然后回到既有 Segoe UI 候选；英文直接使用既有 Segoe UI。这样可覆盖繁中 glyph，同时保留启动早期和崩溃路径的设备独立性。

回滚按边界进行：先恢复各调用点原 `MessageBoxW`，即可停止使用新模块；模块、资源和测试随后可整体移除，不涉及持久化数据、配置迁移或现有 Window Service ABI。若仅 DWM frame 出现兼容问题，可回滚该窗口样式调整而不改变公开请求/结果合同。
