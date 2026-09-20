# Fluent 自绘 MessageBox 调查报告

> 调查日期：2026-09-01
> 状态：仅调查与规划，尚未执行 `task.py start`，尚未修改产品代码。

## 1. 结论摘要

1. 该组件不能直接成为现有 `Inkeys.Window` / `RenderPipeline` 的普通成员。主程序有提示框早于日志、DPI、窗口服务和渲染管线初始化，崩溃处理器也会直接显示提示；依赖现有 UI 服务会失去“故障时仍可提示”的目标。
2. 可行边界是一个仅依赖 Win32、GDI/GDI+ 和自身消息循环的独立组件。每次调用创建临时 UI 线程、HWND 与全部绘图对象；用户作出选择后先隐藏窗口，再销毁 HWND、绘图对象和线程，不保留隐藏窗口等待下次调用。
3. 任何进程内自绘方案都不能承诺在堆破坏、栈破坏、loader lock、资源耗尽或 UI 线程死锁后可靠运行。系统 `MessageBoxW` 必须作为组件创建失败、重入和严重崩溃场景的最后回退；否则可靠性会低于当前实现。
4. 窗口外部阴影和 Windows 11 圆角应交给 DWM，客户区只绘制统一的 `1 DIP` 内边框。不能使用 layered-window 逐像素透明、窗口区域或额外阴影窗口；这些做法会破坏 DWM 的系统圆角判定，也是历史上四边粗细和阴影异常最容易复发的路径。
5. WinUI 3 `ContentDialog` 没有独立 Win32 标题栏；参考图实际混合了 ContentDialog 内容布局和顶层窗口关闭能力。因此“完全一致”应定义为复用官方 Fluent 资源值、排版和状态语义，而不是声称 GDI+ 输出与 XAML/DirectWrite 像素完全相同。
6. 仓库正式兼容目标包含 Windows 7 SP1 + KB2670838。Windows 11 的 DWM 圆角 API 最低要求 build 22000，Windows 7 只能保留 Fluent 客户区并退化为该系统可提供的方角/阴影，不能在不引入自制阴影窗口的前提下获得 Windows 11 完全同款外框。
7. 用户已确认首版不包含独立 `Timeout` 工程。组件只需进入主 `Inkeys.vcxproj`，无需为了 Timeout 降为 C++17 共享边界。

## 2. 现有提示框盘点

### 2.1 主工程已编译的产品调用点

| 场景 | 证据 | 当前行为 | 对新组件的约束 |
| --- | --- | --- | --- |
| 启动目录权限、文件名、重复实例 | `Inkeys/IdtMain.cpp:169,174,182,263` | ownerless、`MB_SYSTEMMODAL`、同步 | 发生在窗口服务与 RenderPipeline 之前；不能依赖 Bar、日志、Display 或 D2D |
| PPT 结束放映确认 | `Inkeys/IdtPlug-in.cpp:803` | Bar owner、OK/Cancel、同步 | 调用方需要立即取得结果；适合同步 API |
| Bar 右键退出确认 | `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3294` | Bar owner、OK/Cancel、同步 | 不能在 Bar 的 HWND 所属线程中运行嵌套服务命令；专用对话框线程更稳妥 |
| Setting 信息/重启确认 | `Inkeys/Inkeys/UI/Setting/Setting.cpp:205,208` | ownerless、同步运行于 Setting business worker | 应避免阻塞共享渲染线程；同步调用仍可阻塞业务 worker |
| 覆盖层窗口启动失败 | `Inkeys/Inkeys/Window/Window.Legacy.cpp:105` | ownerless、系统模态 | 故障正位于窗口服务本身，必须允许系统回退 |
| 未处理异常后的重启提示 | `Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp:435` | ownerless、同步 | 只能 best effort；不能承诺进程状态损坏后自绘仍可靠 |
| SuperTop 失败 | `Inkeys/SuperTop/IdtSuperTop.cpp:205` | ownerless、系统模态 | 与主程序同一工程，可复用普通组件入口 |

主工程另有 `Inkeys/IdtMain.cpp:1685-1705` 的调试辅助 MessageBox；它们不是产品迁移验收范围，除非实现阶段另行确认。

### 2.2 独立 Timeout 工程

`Timeout/InkeysTimeout/TimeoutMain.cpp:325,360,432,443` 还有四个提示框，覆盖播放失败、退出确认、窗口类注册失败和窗口创建失败。该工程：

- 单独使用 `Timeout/InkeysTimeout.sln`；
- 采用 C++17；
- 仅配置 Win32/x64，没有 ARM64；
- 未被主解决方案、README 和当前主构建流程引用；由于用户已明确排除 Timeout，其发布状态无需在本任务内确认。

用户已确认本任务不包含 Timeout；以上盘点只用于解释范围边界，不进入实现和验收。

### 2.3 历史源码

`Inkeys/IdtFloating.cpp`、`Inkeys/IdtWindow.cpp` 中也存在 MessageBox，但它们在当前 `Inkeys.vcxproj` 中为 `None`，不应为了“全量搜索命中”而迁移历史路径。

## 3. 生命周期、线程与所有者调查

### 3.1 为什么不挂入 Window Service

- `Inkeys.Window` 当前只管理 Magnifier、Freeze、Drawpad、PPT、Bar、Setting 和 DisplayObserver 等固定角色。
- 服务的动态创建仍会同步投递到 overlay/setting 所属线程，并依赖服务已经启动；启动早期和窗口服务故障路径无法使用。
- 普通 UI popup 的动态 owner 由服务固定指向 Drawpad，不等于本需求要求的 Bar owner。
- 若在 overlay HWND 所属线程内运行模态循环，可能停止该线程继续处理窗口服务命令；这与“故障隔离”目标相反。

### 3.2 已确认的调用模型

- 首版只提供同步结果 API，因为当前所有产品调用点都立即分支处理返回值。
- 每次显示创建一个临时 UI 线程；线程创建 HWND、运行局部消息循环并拥有全部 GDI/GDI+ 对象。
- 调用线程只等待结果，不拥有 HWND，也不运行绘图；这样启动主线程、PPT business queue、Setting business worker 和 Bar 交互路径可以使用同一入口。
- 若同步调用线程正是 owner HWND 所在线程，裸等待可能与 owned window 的跨线程激活/owner 通知互锁；设计需使用只服务 `QS_SENDMESSAGE` 的受限等待，不分派普通 posted/input 命令，也不运行完整嵌套业务消息泵。
- 首版全进程最多一个自绘 MessageBox。普通并发调用排队；异常处理器、组件重入或创建失败不等待队列，直接走系统回退，避免死锁。
- 关闭流程固定为：确定结果 -> `ShowWindow(SW_HIDE)` -> 恢复 owner 可用性/焦点 -> 销毁窗口及图形资源 -> 退出 UI 线程 -> 唤醒调用方。

### 3.3 owner 与统一置顶

当前 overlay owner 链为 `Magnifier -> Freeze -> Drawpad -> PPT/Bar`。置顶刷新只对链根调用一次 `HWND_TOPMOST/HWND_NOTOPMOST`，owned popup 由 Win32 传播 topmost band。

已确认策略：

- Setting 发起时，以 Setting HWND 为 owner，创建 owned 顶层窗口而不是 `WS_CHILD`，并将 Setting 作为模态禁用/恢复目标。
- 其他正常运行期调用在 Bar HWND 有效且属于当前进程时，以 Bar 为 owner，不自行设置或周期刷新 `WS_EX_TOPMOST`；这样跟随现有统一置顶和 Whiteboard 强制非置顶策略。
- 启动早期、窗口服务失败、SuperTop 与崩溃提示不依赖 owner 链，使用 ownerless 顶层窗口，并且只在创建时进入 topmost band 一次，不参与 overlay 周期置顶。
- 自绘入口无法满足上述 owner 条件或初始化失败时，进入非递归 `MessageBoxW` 回退，并保持对应现有调用的模态与置顶语义。
- 跨线程模态禁用必须在所有正常退出分支恢复；崩溃路径不跨线程等待或禁用可能卡死的 owner。

## 4. 绘图方案调查

### 4.1 现有 DibSurface 不宜直接复用

`Inkeys/Inkeys/Graphics/Surface.cpp:19-39,101-105` 的 `DibSurface` 本身是 RAII GDI DIB，但它使用函数静态 `GdiplusLifetime`，首次调用后直到进程退出才 shutdown。直接复用会把 MessageBox 接到现有 Graphics 模块的共享生命周期上，也无法证明故障隔离。

### 4.2 方案对比

| 方案 | 优点 | 主要问题 | 调查结论 |
| --- | --- | --- | --- |
| 纯 GDI | 初始化最少、Win7 覆盖广、资源容易局部 RAII | 圆角和矢量边缘抗锯齿较弱；透明 PNG 需要额外解码 | 可作最低级回退，不足以单独承担“WinUI 3 一致”视觉目标 |
| GDI + 独立 GDI+ | 文字、圆角、状态面和 32-bit alpha 位图实现直接；无需 D3D/D2D device | GDI+ 文本度量不会与 WinUI/DirectWrite 像素一致；runtime 生命周期受 Win7 限制 | 首选实现方向，但 runtime token 策略必须接受兼容性约束 |
| D2D/DWrite | 最接近 WinUI 的文字/几何栈 | 初始化与故障面更大，并会重新引入用户明确不希望的 D2D 依赖 | 首版排除 |

微软允许在每个使用 GDI+ 的函数中成对调用 `GdiplusStartup/GdiplusShutdown`，但也明确警告：Windows 7 及更早版本在字体族创建失败并退回 generic Sans Serif 时，shutdown 后缓存指针可能导致系统崩溃。因此推荐：

- MessageBox 拥有自己的私有 GDI+ runtime，不复用 `Graphics.Surface`；
- Win8+ 可按一次显示启动/关闭；
- 为正式 Win7 目标，私有 token 应惰性初始化后保留到进程退出，或首版统一采用该策略；
- HDC、32-bit DIB、`Graphics`、字体、画笔、位图与所有命中/布局状态仍全部按一次显示创建和销毁；GDI+ token 不视为绘图 device；
- 若用户把“即时销毁”也严格要求到 GDI+ token，则必须在 Win7 兼容、纯 GDI 降级或接受微软记录的风险之间作选择。

### 4.3 图标所有权

首版已确认的核心输入不是文件路径或 caller-owned `HBITMAP`，而是由组件在调用入口立即复制的 32-bit premultiplied BGRA 像素、尺寸和 stride；其上提供“指定模块 + 嵌入 PNG/resource ID -> 解码并复制”的适配入口。主工程 `.rc` 已大量使用 `PNG` 资源，`Graphics.Surface` 也证明资源字节复制后交给 GDI+ 解码可行，但 MessageBox 仍使用自己的私有 runtime 与资源对象。

- 复制后不依赖调用方句柄寿命。
- 不访问外部文件，启动/故障路径更稳定。
- 解码失败时省略图标并继续显示正文，而不是让整框失败。
- 崩溃提示由组件提供内置错误 PNG；其他当前迁移点默认无图标，调用方可按需传入资源或像素。
- SVG、网络图片、通用 codec、磁盘路径与 caller-owned `HBITMAP` 不进入 MVP。

## 5. Fluent 2 深色视觉基线

用户提供的原始视觉依据已原样保存为 [浅色 ContentDialog](references/01-light-content-dialog.png)、[深色 ContentDialog](references/02-dark-content-dialog.png) 和 [标题/图标/关闭布局](references/03-title-icon-close.png)。浅色图只用于几何比较，MVP 仍只实现深色。

以下数值来自当前 Microsoft WinUI XAML 官方资源，而不是对参考图目测：

| 项目 | 基线 |
| --- | --- |
| 对话框宽度 | `320-548 DIP` |
| 对话框高度 | `184-756 DIP` |
| 内容区/按钮区 padding | `24 DIP` |
| 标题与正文间距 | `12 DIP` |
| 按钮间距 | `8 DIP` |
| 对话框边框 | `1 DIP` |
| 对话框圆角 | `8 DIP` (`OverlayCornerRadius`) |
| 按钮圆角 | `4 DIP` (`ControlCornerRadius`) |
| 标题 | `20 DIP`, Semibold, 最多两行 |
| 正文/按钮 | `14 DIP`, Segoe UI 系列，正文换行 |
| 正文/按钮布局 | 上部内容区与下部 command area 分离；Primary、Secondary、Close 等宽排列 |

深色语义色基线：

- 按钮区/对话框基底：`#202020` (`SolidBackgroundFillColorBase`)；
- 上部内容区在基底上叠加 `#0DFFFFFF` (`LayerFillColorAlt`)，不透明合成结果约为 `#2B2B2B`，与深色参考图的上下分区一致；
- 主文本：`#FFFFFF`；次文本：`#C5FFFFFF`；禁用文本：`#5DFFFFFF`；
- 普通按钮默认填充：`#0FFFFFFF`，hover：`#15FFFFFF`，pressed：`#08FFFFFF`；
- 默认表面边框使用 `#66757575` (`SurfaceStrokeColorDefault`)，内容/按钮分隔使用 `#19000000` (`CardStrokeColorDefault`)；
- 主按钮允许固定青色 `#60CDFF`，深色主题上的主按钮文字使用黑色；hover/pressed 按 Fluent accent secondary/tertiary 层级降低亮度/不透明度。

参考图与官方 ContentDialog 的差异需要在设计阶段显式解决：ContentDialog 本身没有独立 Win32 caption。建议把关闭 glyph 放入顶部 `32 DIP` 命中区，未被按钮/文本占用的顶部区域返回 `HTCAPTION`；标题仍属于 `24 DIP` 内容网格。关闭 glyph 只在调用契约存在可取消结果时默认显示，避免出现“可以关闭但没有返回语义”的状态。

## 6. 边框、阴影与非客户区

建议外框合同：

1. 保留能让 DWM 识别顶层窗口的 caption/frame 样式或 `1 px` 非客户区；通过 `WM_NCCALCSIZE` 扩展客户区，而不是改用无框 layered window。
2. `WM_NCHITTEST` 先交给 `DwmDefWindowProc`，再只为关闭按钮、客户区和拖动区返回明确结果；永不返回八个 resize hit-test 值。
3. 固定 `WM_GETMINMAXINFO` 的最小/最大 track size，双重保证不可调整大小。
4. Windows 11 尝试 `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`；这是 hint，不是跨系统保证。
5. 客户区在像素对齐后的同一条闭合几何上绘制 `1 DIP` 内边框，四边不分别计算，避免不同 DPI 下出现 1/2 像素混合。
6. 不创建额外阴影 HWND，不用 `SetWindowRgn`，不使用 per-pixel alpha layered window。DWM 负责外部阴影；Windows 7/10 按系统能力降级。

## 7. DPI、文本与布局

- 主 `Inkeys.vcxproj` 的 Win32/x64/ARM64、Debug/Release 六个配置都通过 manifest 声明 `PerMonitorHighDPIAware`，因此主进程在进入 `wWinMain` 前已经具备 manifest 级 DPI awareness；`IdtMain.cpp:841-869` 的进程级 API 调用是后续兼容路径，不是早期提示框唯一的 DPI 前提。
- 启动早期仍不能使用 `Inkeys.Display`。组件应查询当前线程/窗口 DPI，动态解析新 API 并提供 Win7 回退，不应自行再次改变整个进程的 DPI awareness。
- 所有公开尺寸以 DIP 表达，每次显示选定 monitor 与 DPI 后一次性换算；处理 `WM_DPICHANGED` 时使用建议矩形重新布局。
- 默认以 owner 所在 monitor 的 work area 居中；无 owner 时依次使用当前前台窗口、光标、主显示器所属 monitor，并把最终矩形约束在 work area 内。
- 文本宽度应先在 `320-548 DIP` 中测量，标题最多两行；正文不能静默截断。
- 官方 ContentDialog 高度上限为 `756 DIP`，但其模板中的垂直滚动条默认 disabled。MVP 不实现正文滚动；布局测量若表明完整正文无法同时满足 `756 DIP` 和目标 monitor work area，则在创建自绘 HWND 前把完整原文交给系统回退。
- 当前已定位的待迁移硬编码正文最多包含四个显式文本行；Setting 的动态正文入口当前只由语言重启警告和两处固定更新提示使用。现有调用不要求长文滚动，但公共入口未来可能收到更长文本。

## 8. API 与交互基线

- MVP 按钮组合至少包含 `OK`、`OK/Cancel`、`Yes/No`；前两类是主工程现有迁移所需，`Yes/No` 是用户确认的首版组合。
- 返回独立枚举，不直接泄漏 `IDOK/IDCANCEL/...`；系统回退负责映射。
- 默认按钮使用青色 Accent 样式；其余按钮使用 Fluent neutral 样式。
- `Enter` 激活默认按钮，`Tab/Shift+Tab` 循环焦点，`Space` 激活焦点按钮。
- 标题栏关闭、`Esc` 与 `Alt+F4` 统一走可配置的 dismiss 路径；未配置时隐藏关闭按钮并拦截另外两种关闭方式。`OK` 映射为 `OK`，`OK/Cancel` 映射为 `Cancel`，`Yes/No` 默认禁止 dismiss；若调用方明确启用则返回独立 `Dismissed`，不得伪装为 `No`。
- 按钮需要 normal/hover/pressed/disabled/focused 状态，鼠标 capture 丢失时必须清 pressed，避免卡住状态。
- 首版确认不实现 UI Automation provider；必须保证 Tab/Shift+Tab、Enter/Space、已批准的 Esc 规则、正确初始焦点、可见焦点框与 Fluent 2 对比度，但屏幕阅读器不会获得完整原生 Button/Dialog 语义，验收时不得宣称与 WinUI 可访问性等价。

## 9. 失败与回退矩阵

| 失败场景 | 推荐行为 |
| --- | --- |
| UI 线程/HWND/GDI+ 初始化失败 | 使用同文案、同按钮语义调用 `MessageBoxW` |
| 正文测量超过自绘可用高度 | 不创建自绘 HWND；把完整原文和同按钮语义交给 `MessageBoxW` |
| 图标解码失败 | 省略图标，继续显示自绘窗口 |
| 正常运行期要求的 Bar/Setting owner 无效 | 使用对应现有文案、按钮、模态与置顶语义进入系统回退 |
| 已有自绘框，普通线程再次调用 | 串行排队 |
| 已有自绘框时进入崩溃处理器 | 不等待、不抢锁，直接系统回退 |
| 对话框自身 WndProc/GDI+ 抛出或返回失败 | 隐藏并清理后系统回退；禁止递归调用自绘入口 |
| owner 线程无响应 | 不跨线程等待禁用 owner；严重路径走 ownerless 系统回退 |

## 10. 后续验证边界

无需可见窗口的自动化：

- 96/120/144/192 DPI 的布局、像素取整、等宽按钮和边框闭合几何；
- 无图标/有图标、标题一/两行、正文多行、OK/OK-Cancel/Yes-No；
- hover/pressed/focus/disabled 状态与颜色 token；
- Enter/Esc/Alt+F4/关闭按钮结果映射；
- owner/topmost/回退决策纯逻辑；
- 并发串行、异常重入旁路和资源 RAII；
- 隐藏 HWND 的 style、owner、hit-test、DPI 消息和销毁顺序（若测试环境允许创建隐藏窗口）。

必须由可见窗口或用户设备验收：

- DWM 真实阴影、圆角、四边边框在 Windows 11 ARM64 上的观感；
- 多显示器、不同 DPI、任务栏和前后台激活行为；
- Bar 统一置顶、Whiteboard 非置顶以及 Setting 前台场景；
- Windows 7 SP1 + KB2670838 的退化外框和 GDI+ 字体行为；
- 与三张参考图的最终视觉对照。

本轮规划不启动可见窗口。用户已明确授权在实现阶段启动短时专用测试窗并截取其窗口区域，但禁止使用 Computer Use 或操作无关应用；测试入口必须自行驱动状态、捕获并关闭窗口。

## 11. 决策状态

### 已确认

- **首版覆盖边界与失败回退**：覆盖主 Inkeys 正常运行期、启动早期、窗口服务失败、SuperTop 和 `UnhandledExceptionHandler`；所有路径保留 `MessageBoxW` 最后回退。
- **Timeout**：独立 Timeout 工程不在本任务范围内。
- **旧系统外框**：Windows 11 使用 DWM 圆角/阴影；Windows 7/10 接受系统能力退化，只保持 Fluent 客户区一致，不引入手工阴影或窗口区域。
- **GDI+ runtime**：MessageBox 私有 token 在 Win7 下保留至进程退出；HDC、DIB、Graphics、Font、Brush、Bitmap 等真正绘图资源仍按次销毁。
- **owner 与置顶**：Setting 发起时归属 Setting；其他正常运行期调用归属有效 Bar 并跟随统一置顶；启动早期、窗口服务失败、SuperTop 与崩溃提示使用 ownerless 窗口且只在创建时置顶一次；系统回退保留原模态/置顶语义。
- **dismiss 与默认按钮**：关闭按钮、Esc、Alt+F4 共用可配置 dismiss；OK 映射 OK，OK/Cancel 映射 Cancel，Yes/No 默认禁止关闭、显式启用时返回独立 Dismissed；Enter 激活默认按钮。
- **超长正文**：MVP 不实现滚动；正文超过 `756 DIP` 或 monitor work area 可用高度时，在创建自绘 HWND 前携带完整原文进入系统回退，不能静默裁切。
- **图标输入**：组件立即复制 premultiplied BGRA 像素，并提供嵌入透明 PNG 的资源适配器及内置错误图标；不接收磁盘路径、SVG 或 caller-owned HBITMAP，解码失败时无图标继续显示。
- **可访问性**：MVP 保证完整键盘操作、正确初始焦点、可见焦点框与颜色对比；不实现 UI Automation provider，不承诺讲述人语义等价。
- **调用模型与布局顺序**：首版只提供同步 API 并由临时 UI 线程显示，全进程单框串行，崩溃/重入旁路回退；按钮按 Primary/Secondary/Close 排列；窗口优先在 owner 所在 monitor 居中。
- **可见验收**：实现阶段可启动短时专用测试窗并截取限定窗口区域；不使用 Computer Use，不操作无关应用，测试后自动关闭。

## 12. 外部依据

- Microsoft WinUI XAML `ContentDialog` 官方资源：<https://github.com/microsoft/microsoft-ui-xaml/blob/main/controls/dev/CommonStyles/ContentDialog_themeresources.xaml>
- Microsoft WinUI XAML 深色语义色：<https://github.com/microsoft/microsoft-ui-xaml/blob/main/controls/dev/CommonStyles/Common_themeresources_any.xaml>
- Microsoft WinUI XAML 按钮资源：<https://github.com/microsoft/microsoft-ui-xaml/blob/main/controls/dev/CommonStyles/Button_themeresources.xaml>
- Microsoft WinUI XAML 圆角资源：<https://github.com/microsoft/microsoft-ui-xaml/blob/main/controls/dev/CommonStyles/CornerRadius_themeresources.xaml>
- DWM 自定义窗口框架：<https://learn.microsoft.com/en-us/windows/win32/dwm/customframe>
- Windows 11 桌面窗口圆角：<https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners>
- `GdiplusStartup`：<https://learn.microsoft.com/en-us/windows/win32/api/gdiplusinit/nf-gdiplusinit-gdiplusstartup>
- `GdiplusShutdown` 的 Windows 7 字体缓存警告：<https://learn.microsoft.com/en-us/windows/win32/api/gdiplusinit/nf-gdiplusinit-gdiplusshutdown>
- `MessageBoxW` owner、按钮与 modality 语义：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-messageboxw>
- Fluent 2 色彩与状态原则：<https://fluent2.microsoft.design/color>

## 13. 2026-09-02 键盘操控跟进调查

### 13.1 实现前代码证据

- `MessageBox.Window.cpp` 的 `DialogSession::focusedButton` 在 `BuildLayout()` 中初始化为 `defaultResult` 对应索引；`DrawButton()` 只判断该索引是否相等，因此未发生任何键盘输入时就会绘制白色双层焦点框。
- `WM_KEYDOWN` 当前只实现 Tab、Enter、Space、Escape：Tab/Shift+Tab 循环索引；Enter 固定提交 `request->defaultResult`；Space 提交 `focusedButton`；没有 Left/Right。
- pointer down 会把 `focusedButton` 改为被按按钮，但当前没有 pointer/keyboard focus-state 区分，所以 pointer 操作也会产生白框。
- Esc、自绘 X、Alt+F4、`SC_CLOSE` 与 `WM_CLOSE` 当时均进入 `TryDismiss()`，而 `showCloseButton=false` 只隐藏 glyph；只要 `dismissEnabled=true`，无 X 时仍返回 `dismissResult`。
- factory/校验合同仍是 OK -> Ok、OK/Cancel -> Cancel、Yes/No 默认禁止 dismiss；Yes/No 显式 dismiss 只能返回独立 `Dismissed`，测试已禁止把它映射为 No。

### 13.2 官方行为依据

- Microsoft 的 WinUI 键盘指南规定：焦点视觉在元素通过键盘或手柄获得焦点时显示；Button 获得焦点后，Space 和 Enter 都用于调用命令。相同指南明确把 ContentDialog 按钮列为方向键导航组，并指出每个按钮仍是独立 Tab stop。
- Microsoft WinUI 焦点设计说明区分 `FocusState::Pointer` 和 `FocusState::Keyboard`：pointer focus 不画 focus rectangle，keyboard focus 才画。这支持把逻辑焦点与焦点框可见性分离，而不是清空默认按钮。
- ContentDialog 官方指南把 CloseButton 定义为安全、非破坏性退出，并把 Esc 绑定到该动作；DefaultButton 负责在没有其他控件处理 Enter 时响应 Enter。当前自绘框仅有 command buttons 可聚焦，因此 Enter 应先服从实际逻辑焦点，初始索引自然提供 default fallback。
- Microsoft 的 ContentDialog 集成测试表明水平方向导航到最左/最右按钮后继续同方向不会 wrap；Tab 顺序则应保持对话框内循环，避免焦点离开模态窗口。
- Win32 文档确认 Alt+F4 和标题 Close 都会形成关闭请求/`WM_CLOSE`，Alt+Space 打开标准 Window menu；`WM_GETDLGCODE` 提供 `DLGC_WANTARROWS` 与 `DLGC_WANTTAB` 来声明自定义键盘处理。

### 13.3 收敛结论与批准结果

- 用户批准状态模型为：默认按钮始终拥有初始逻辑焦点，但首帧为 pointer/programmatic 模态且不画白框；Tab、Shift+Tab、Left、Right 首次导航后显示；pointer down 隐藏，单纯 hover 不隐藏，HWND 失焦时不画。
- 用户批准 Tab/Shift+Tab 首尾循环，Left/Right 按 LTR 空间顺序到边界停止；Up/Down、Home/End、F6、Ctrl+W 和 access key 不纳入本轮。
- 用户批准 Enter/Space 始终激活逻辑焦点索引；因此未导航时是 default，导航后是当前按钮，不需要维护第二套“是否选择过”结果状态。
- 用户批准所有关闭入口进入一个 resolver：有 X -> dismissResult；无 X 的 OK/Cancel -> Cancel；无 X 的 OK-only -> Ok；Yes/No 默认 -> 无操作；Yes/No 显式 dismiss -> Dismissed；`dismissEnabled=false` 始终最高优先级并禁止关闭。

新增官方依据：

- WinUI keyboard interactions：<https://learn.microsoft.com/en-us/windows/apps/develop/input/keyboard-interactions>
- WinUI dialog controls：<https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/dialogs-and-flyouts/dialogs>
- WinUI focus design notes：<https://github.com/microsoft/microsoft-ui-xaml/blob/main/docs/design-notes/focus.md>
- Win32 `WM_GETDLGCODE`：<https://learn.microsoft.com/en-us/windows/win32/dlgbox/wm-getdlgcode>
- Win32 close sequence：<https://learn.microsoft.com/en-us/windows/win32/learnwin32/closing-the-window>
- Win32 system accelerators：<https://learn.microsoft.com/en-us/windows/win32/menurc/about-keyboard-accelerators>
