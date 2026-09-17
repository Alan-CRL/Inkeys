# 全环境 Fluent 自绘 MessageBox

## Goal

为 Inkeys 各运行环境提供一套进程内、可复用、视觉符合 WinUI 3 / Fluent 2 深色规范的自绘 MessageBox，替代无法统一外观或依赖现有 UI 渲染设备的系统提示框；在常规提示以及主 UI 部分失效时，仍能独立创建并显示关键消息。

## User Value

- 各环境获得一致、清晰的消息提示体验。
- MessageBox 不依赖 Bar、Draw3 或其他 UI 的渲染设备与生命周期，降低错误提示被关联故障拖垮的风险。
- 首版 API 和窗口边界为未来扩展更多内容与按钮组合保留空间，但不提前实现未需要的能力。

## Confirmed Requirements

### Lifecycle and independence

- MessageBox 使用独立的普通 Win32 顶层窗口，不嵌入其他 UI 窗口。
- 窗口仅在需要显示时创建；一次消息结束后先隐藏，再销毁 HWND、按次绘图资源和临时 UI 线程，不保留隐藏 HWND 或 DIB 供下一次复用。仅 Windows 7 下私有 GDI+ runtime token 可保留至进程退出。
- 绘图设备按 MessageBox 自身生命周期加载和释放，不能复用 Bar、Draw3、Setting 或其他 UI 的渲染设备。
- 首版覆盖主 Inkeys 的正常运行期、启动早期、窗口服务失败、SuperTop 和 `UnhandledExceptionHandler` 提示路径。
- 自绘入口失败、异常重入或严重故障路径必须具备不递归的 `MessageBoxW` 回退；回退属于正式可靠性合同，不要求外观一致。

### MVP content

- 标题文本。
- 可选的标题栏关闭按钮。
- 正文文本，支持合理的多行排版。
- MVP 不实现正文滚动；布局前测量正文，若完整内容无法同时满足 `756 DIP` 和当前显示器 work area 的高度限制，则不创建自绘窗口，改向 `MessageBoxW` 回退入口传递完整原文。
- 正文左侧可选图标；核心输入为组件立即复制并自行持有的 `32-bit premultiplied BGRA` 像素、尺寸与 stride。
- 提供嵌入式透明 PNG 的资源适配入口，负责从指定模块/resource ID 解码后复制为核心像素格式；崩溃提示使用组件内置的错误 PNG。
- 图标解码失败时省略图标并继续显示完整标题、正文和按钮，不因装饰资源失败而放弃自绘提示。
- 底部独立按钮区域，首版至少支持单按钮确认和双按钮 Yes/No 组合。
- 为迁移主工程现有产品调用，首版按钮组合还需包含 OK/Cancel。
- 标准按钮文字不依赖产品 i18n；首版按 Win32 UI language 提供简体中文、繁体中文和英文三组，其他语言回退英文。
- 标题栏关闭、`Esc`、`Alt+F4`、system-menu Close 与 `WM_CLOSE` 统一走同一个 close-command resolver；X 可见时使用配置的 dismiss 结果，X 隐藏时按按钮组合选择安全退出结果，dismiss 禁用时全部无操作。
- `OK` 预设的 dismiss 结果为 `OK`，`OK/Cancel` 为 `Cancel`；`Yes/No` 默认不允许 dismiss，调用方明确启用时返回独立 `Dismissed`，不得映射为 `No`。
- `Enter` 激活调用方配置的默认按钮；返回值使用组件枚举，不直接向调用方泄漏 Win32 `IDOK`、`IDCANCEL` 等常量。
- 未来内容类型与按钮组合扩展不属于首版实现，但首版接口不得把布局硬编码为只能支持单一文案。

### Window behavior

- 背景不透明，不使用 Mica、Acrylic 等系统材质。
- 窗口不可调整大小，但可以拖动。
- 使用自绘标题栏，并保持四边边框粗细一致、圆角与阴影行为正常。
- 外部阴影与系统圆角由 DWM 负责；客户区绘制统一 `1 DIP` 内边框。不使用 layered-window 逐像素透明、窗口区域或额外阴影窗口。
- Windows 11 使用 DWM 圆角与阴影；Windows 7/10 接受系统可提供的外框退化，仅保证 Fluent 客户区一致，不为旧系统自制圆角或阴影。
- Setting 发起的 MessageBox 以 Setting HWND 为 owner，作为 owned 顶层窗口显示（不使用 `WS_CHILD`），并以 Setting 为模态禁用/恢复目标。
- 其他正常运行期 MessageBox 在 Bar HWND 有效时以 Bar 为 owner，继承现有统一置顶策略，不单独设置或周期刷新 topmost。
- 启动早期、窗口服务失败、SuperTop 与崩溃提示使用 ownerless 顶层窗口，仅在创建时设置一次 topmost，不参与现有 overlay 的周期置顶刷新。
- 若进入系统 `MessageBoxW` 回退，则保持对应现有调用的模态与置顶语义。
- 关闭按钮是否出现由调用方配置；按钮区组合由调用方配置。

### Visual design

- 首版仅实现深色主题。
- 外观以用户提供的三张 WinUI 3 / Fluent 2 参考图为视觉基准；原图已保存在 `research/references/`，避免依赖临时剪贴板路径。
- 灰阶、分隔线、边框、文本层级、按钮状态与间距遵循 Fluent 2 深色语义。
- 主操作高亮色允许使用青色；次要按钮使用 Fluent 2 深色中性色。
- 交互状态至少应包含默认、悬停、按下、禁用与键盘焦点中实际适用的状态。
- command-area 始终保存一个逻辑焦点按钮，初值为 `defaultResult` 对应按钮；默认按钮的 Accent 外观保持不变，但窗口刚出现、仅由鼠标/触摸操作或仅获得 HWND 焦点时不得绘制白色键盘焦点框。
- 第一次使用 `Tab`、`Shift+Tab`、`Left` 或 `Right` 进行按钮导航时切换为键盘焦点视觉；`Tab/Shift+Tab` 在可用按钮间首尾循环，`Left/Right` 按从左到右的空间顺序移动且到边界停止。标题 close 不进入 Tab 或方向键顺序。
- `Enter` 与 `Space` 都激活当前逻辑焦点按钮；若用户从未移动焦点，该按钮自然就是配置的默认按钮。数字小键盘 Enter 与普通 Enter 使用同一 `VK_RETURN` 语义。
- 鼠标按下按钮可更新逻辑焦点但使用 pointer focus，不显示白色焦点框；单纯 hover 不清除已经由键盘建立的焦点视觉。窗口实际失去键盘焦点时不绘制焦点框。
- `Esc`、`Alt+F4`、system-menu Close、`WM_CLOSE` 与自绘标题 close 必须进入同一个 close-command resolver 并保持结果单次提交；无 X 时优先采用安全、非破坏性动作，具体 OK-only / Yes-No 路由见本轮已批准表。
- `Space`、`Esc`、`Alt+F4` 与 `Alt+Space` 系统菜单属于保留的附加键盘行为；MVP 不新增 `Up/Down`、`Home/End`、`F6`、`Ctrl+W` 或本地化 access key。`Alt+Tab` 等系统级切换仍由 Windows 处理。
- MVP 不实现完整 UI Automation provider，不承诺讲述人可获得正文、按钮与 Dialog 的完整语义；该限制必须在验收结果中明确披露。
- 视觉数值以 Microsoft WinUI XAML 官方 `ContentDialog`、Button、CornerRadius 与 Common theme resources 为基线，不以截图目测替代 token。

### Investigated technical baseline

- 对话框尺寸 `320-548 DIP` × `184-756 DIP`；内容区和按钮区 padding `24 DIP`；标题间距 `12 DIP`；按钮间距 `8 DIP`。
- 外圆角 `8 DIP`，按钮圆角 `4 DIP`，边框 `1 DIP`；标题 `20 DIP Semibold`，正文和按钮 `14 DIP`。
- 深色按钮区基底为 `#202020`，上部内容区叠加 `#0DFFFFFF` 后约为 `#2B2B2B`；主文本 `#FFFFFF`；普通按钮使用 Fluent 半透明中性色状态；MVP Accent 使用青色 `#60CDFF`。
- 启动早期不能依赖 `Inkeys.Window`、`Inkeys.Display`、日志或 `RenderPipeline`；组件必须拥有独立消息循环和 DPI API 回退。主工程已有 manifest 级 `PerMonitorHighDPIAware`。
- 当前产品调用都同步取得结果；首版只提供同步结果 API，由按次临时 UI 线程承载窗口消息循环。全进程最多显示一个自绘 MessageBox；普通并发调用串行等待，异常处理器、组件重入或创建失败不得等待或抢占该队列，直接进入系统回退。
- 按钮从左到右按 WinUI ContentDialog 的 Primary、Secondary、Close 槽位排列；OK/Yes 为默认 Primary，Cancel/No 为 Secondary，单 OK 使用 Primary 槽位。
- 有 owner 时在 owner 所在 monitor 的 work area 居中；ownerless 时依次使用当前前台窗口、光标、主显示器所属 monitor，并保证最终窗口完整落在 work area 内。
- 当前 overlay 只对 owner 链根统一切换 topmost；Bar-owned MessageBox 不单独设置 topmost。Setting-owned MessageBox 跟随 Setting；指定的 ownerless 故障路径只在创建时置顶一次。
- 项目兼容目标包含 Windows 7 SP1 + KB2670838；MessageBox 使用私有 GDI+ runtime，Win7 下 token 惰性初始化后保留至进程退出，HDC、DIB、Graphics、Font、Brush、Bitmap 等按次资源仍在每次关闭时销毁。
- 详细证据、方案对比与风险见 `research/investigation.md`。

### Planning gate

- 本轮只调查代码库并完成需求、设计与实施约束，不修改产品代码。
- 在用户批注并明确批准最终规划摘要前，不运行 `task.py start`，不进入实现阶段。

### Validation boundary

- 实现阶段获准启动短时、专用的可见 MessageBox 测试窗口，并截取该窗口区域，用于核对 Windows 11 ARM64 上的 DWM 阴影、圆角、四边边框、焦点和按钮状态；测试完成后立即关闭。
- 可见验证不得使用 Computer Use，也不得操作或截图无关应用；窗口启动、状态驱动、区域捕获和关闭应由测试程序或明确的 Win32/命令行测试入口完成。
- Windows 7/10 未实际运行时只能报告静态兼容设计和构建覆盖，不得写成真实视觉验证通过。

## Acceptance Criteria

- [x] 在目标环境中可按统一调用契约显示标题、正文、可选透明位图图标和指定按钮组合。
- [x] 图标像素在调用入口内完成复制，不依赖 caller-owned 句柄或缓冲区寿命；嵌入 PNG 解码失败时无图标降级而不丢失消息。
- [x] OK、OK/Cancel 与 Yes/No 流程均返回明确结果；标题栏关闭、Esc、Alt+F4 和 Enter 严格遵守已确认的 dismiss/default-button 映射，Yes/No 的关闭不得被误报为 No。
- [x] MessageBox 的创建、显示、隐藏及绘图资源生命周期独立于其他 UI 渲染设备。
- [x] 自绘初始化失败、异常重入和获批的严重故障场景可无递归地退回系统 MessageBox，并保持按钮结果语义。
- [x] 窗口不可缩放但可拖动；Setting、Bar 与 ownerless 场景分别遵守已确认的 owner、模态和置顶策略。
- [x] 深色视觉与参考图的布局、灰阶、圆角、边框、分隔、阴影和按钮状态保持一致的 Fluent 2 观感。
- [x] 仅用键盘即可遍历并激活所有 command-area 按钮，标题 close 具备 Esc/Alt+F4 等价路径；初始焦点、焦点框和 Esc 行为可预测，且不会形成键盘焦点陷阱。
- [x] Windows 11 使用 DWM 外框；Windows 7/10 不出现手工阴影造成的重复、裁切或四边不均，并按系统能力稳定退化。
- [x] 在目标 DPI、文本长度与图标有无等组合下，正文、按钮和边框不重叠、不截断，窗口边缘无粗细不均或异常阴影。
- [x] 超过自绘可用高度的正文不会被静默裁切或显示残缺，而是在创建 HWND 前携带完整原文进入系统回退。
- [x] 无窗口测试覆盖布局、状态映射和结果语义；隐藏 HWND 测试覆盖 style、owner、DPI 与销毁顺序；获准的可见测试生成限定窗口区域的截图并完成 DWM/视觉核对。
- [x] Windows 11 ARM64 上完成 DPI、owner、统一置顶与获批的可见视觉验证；Windows 7/10 若未实际运行，只报告静态兼容设计和构建覆盖，不得写成真实验证通过。

## Out of Scope for MVP

- 浅色主题与运行时主题切换。
- Mica、Acrylic 或透明窗口背景。
- 输入框、复选框、进度条、超链接、富文本、自定义内容插槽等扩展内容。
- 完整 UI Automation provider、屏幕阅读器语义等价及相关自动化事件。
- 异步结果 API，以及同时显示多个自绘 MessageBox。
- 任意数量与任意排列的按钮。
- SVG 图标解析、通用矢量资源管线、磁盘/网络图标路径及 caller-owned `HBITMAP` 输入。
- 正文滚动条、滚轮滚动与超长内容浏览；超限正文按正式回退合同处理。
- 可调整大小、最大化、最小化以及任务栏入口。
- 入场、退场、按钮过渡等时间动画；MVP 状态切换即时重绘。
- 独立 `Timeout` 工程及其 `TimeoutMain.cpp` 中的四个系统提示框。
- `IdtMain.cpp:1685-1705` 的调试辅助 MessageBox。

## 2026-09-02 Keyboard Interaction Follow-up

用户已批准以下 close-command 路由并授权实现；`dismissEnabled == false` 始终优先并使所有 close command 无操作：

| 可见 X / 按钮组合 | 已批准 close-command 结果 | 理由 |
| --- | --- | --- |
| X 可见 | 现有 `dismissResult` | 与点击 X 完全同义 |
| X 隐藏 + OK/Cancel | `Cancel` | 选择明确的安全退出按钮，即使调用方把 dismissResult 改成其他值 |
| X 隐藏 + OK-only | `OK` | 唯一按钮只是确认已读，不存在取消结果 |
| X 隐藏 + Yes/No，默认 dismiss 禁用 | 无操作 | 不得把关闭伪装为 `No` |
| X 隐藏 + Yes/No，调用方显式启用 dismiss | `Dismissed` | 保留此前已确认的独立关闭结果 |

新增验收项：

- [x] 首帧和纯 pointer 操作不出现白色焦点框；键盘导航后出现，窗口失焦时不绘制。
- [x] `Tab/Shift+Tab` 循环，`Left/Right` 不越过水平边界；单按钮场景稳定且不会除零或形成焦点陷阱。
- [x] `Enter/Space` 激活逻辑焦点按钮；未导航时为默认按钮，导航后不得回跳到默认按钮。
- [x] X、Esc、Alt+F4、system-menu Close 与 `WM_CLOSE` 遵循获批路由，Yes/No 关闭永不误报为 No。
- [x] 初始无焦点框和键盘导航焦点框均有定向像素/截图回归；既有 owner、DPI、首帧、结果单次提交与资源清理合同无回退。

## 2026-09-02 Localization Follow-up

用户已批准把现有双语 MessageBox 文案收敛为按当前 Inkeys 语言显示的单语文案，并统一产品名称为 `Inkeys`：

- i18n 尚未加载时可能出现的启动目录、文件名、重复启动和 SuperTop 提示，固定使用英文标题、英文正文和英文按钮，不读取产品 i18n 状态。
- i18n 加载完成后的结束放映、窗口创建失败、更新模块未启动、语言切换重启、退出确认和崩溃提示使用正式 i18n key，提供 `en-US`、`zh-CN`、`zh-TW` 三份完整翻译。
- MessageBox 核心继续不依赖 `IdtI18n`；调用方在同步 `Show()` 前提供已解析并保持有效的标题、正文和可选按钮文字，核心立即复制。
- Request 默认语言为英文。正常运行期由调用方传入当前产品语言；系统 fallback 使用同一语言 ID，不能在启动早期重新采用系统中文按钮。
- 自绘字体按请求语言选择系统 UI 字体：英文沿用 Segoe UI，简中优先 Microsoft YaHei UI，繁中优先 Microsoft JhengHei UI；候选不可用时退回既有 Segoe UI 路径。MessageBox 不依赖主 UI 的共享字体集合。
- 崩溃处理允许在 i18n 尚未就绪或无法无阻塞读取时退回完整英文快照，不因翻译运行时状态阻塞故障提示。
- 本轮只做本地化与字体覆盖，不改变按钮组合、default/dismiss 结果、X 行为、owner/topmost、布局尺寸或窗口生命周期。

新增验收项：

- [x] 启动早期五类提示（四条启动提示和 SuperTop）只包含英文且应用名统一为 `Inkeys`。
- [x] 六类运行期提示按 `en-US`、`zh-CN`、`zh-TW` 显示单一语言，标题、正文和按钮语言一致，不保留中英拼接。
- [x] 自绘和系统 fallback 均携带请求语言；默认请求不读取 OS/thread UI language 而稳定使用英文。
- [x] 繁中标题、正文与按钮可由系统繁中 UI 字体完整测量和绘制；字体缺失时安全回退而不丢失文字。
- [x] i18n `sync` / `check`、MessageBox 无窗口测试、完整 ARM64 Solution 构建均通过。
