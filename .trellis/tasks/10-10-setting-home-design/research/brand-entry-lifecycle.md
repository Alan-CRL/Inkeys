# Research: 设置窗口品牌书写入场生命周期

- Query: 仅设置窗口隐藏到显示时，六组笔画并行书写，完整字标停留 300 ms 后纯色覆盖层淡出，字标沿非线性路径快速落到主页横幅；关闭设置重置主页及顶部；空间不足直接静态显示。
- Scope: internal
- Date: 2026-10-11

## Findings

### 文件与现有接入点

| 文件 | 用途及准确接入点 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h` | `SessionState::SetVisible` / `ShowSerial`，现有显隐事件与资源决策，约 157–162 行。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | `RunSettingSession` 常驻 coroutine；2041–2089 行持有目的地、导航、主页动效及 `navigateTo`；2286–2293 行消费 ShowSerial；2305/2319 行建立外层滚动与自适应页面；3838 行内容 alpha；3865 行导航；3956 行 `RenderSettingFrame`；4226/4238 行 `Show` / `Hide`。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.h` | `HomeMotionState` 77 行；`ConfigureHomeMotion` 92 行。现有规则同时把返回主页与 ShowSerial 改变当作进入，需按新要求取代。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp` | `DrawHomeBrand` 136 行记录目标原点、宽度、可见性并处理视差；`RenderHome` 324 行拥有真实字标、标语、按钮及横幅尺寸；`ApplyContentAlpha` 230 行递归调制内容 alpha。 |
| `Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h` | `Build` 48 行生成六组笔画压力宽度、彩虹及 AA；`Geometry` 229 行静态缓存；`Prepare` 322 行缓存视差；`Draw` 344 行提交原最终网格。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp` | `ScrollbarStyleScope` 249 行与 `BeginDragScrolling` 378 行维护真实 ScrollTarget 和 `##drag-scroll-*` storage；`BeginScrollChild` 436 行。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Design.h` | `NavigationState` 105 行拥有浮层、焦点、选择条及内容显露；`ResolveShellGeometry` 242 行按真实客户区 DIP/侧栏宽度解析正文 viewport。 |
| `InkeysHeadlessTests/setting_session_state_tests.cpp` | 408–415 行已覆盖 ShowSerial 重复 Show、Hide→Show、尺寸/遮挡/资源变更。 |
| `InkeysHeadlessTests/setting_design_tests.cpp` | `CheckHomeBrandDesign` 2888 行附近三语言/真实链接/输入；`CheckHomeMotion` 3088 行；`CheckHomeScrollFollowup` 2710 行。 |
| `InkeysHeadlessTests/setting_home_warp_tests.cpp` | `Offscreen` 70 行、`Frame` 141 行、`Benchmark` 459 行，真实 DX11 后端及 FL11.0 WARP 像素读取与性能测量。 |

### 显示、隐藏、导航与时钟

- `SessionState::SetVisible(true)` 仅在此前 hidden 时递增 ShowSerial；重复可见 Show（例如窗口未聚焦后重新激活）不增加。`Show` 在 mutex 下发布显隐，再异步投递 HWND Show 和现有 Settings 请求。正确触发边界已存在，无需连续帧缺席推断。
- 当前 `Hide` 只停止可见并投递 HideWindow，不重置主页；coroutine 和 ImGui context 均常驻。WM_CLOSE / SC_CLOSE 分别在 `Setting.cpp:1323` / `1199` 统一走 Hide。最小化 WM_SIZE 不走 Hide（1303 行），恢复不应当作新的窗口打开。
- 新的关闭重置应发布独立 hide/reset serial 或 pending，再由唯一渲染线程消费：目的地 Home、settingTab=0、取消主页入场/视差、关闭 overlay/flyout、复位选择条与 contentReveal，清除旧控件 ActiveId/滚动抓取。不得让 HWND/业务线程直接修改 coroutine 局部或 ImGui window。
- 隐藏路径不会继续 Resume coroutine（`RenderSettingFrame:4087` 前返回 Idle），不能为了关闭重置额外 Resume 去做一帧 DX11 渲染。可把 reset 作为会话状态请求，在下次真实可呈现帧、排布/输入前消费；若要求隐藏时内部立即完成，须提供仅渲染线程运行、与绘制分离的 resident 状态重置入口，不能依赖无 RTV 的隐藏绘制。
- 外层 `##setting-content` 被 page 与 section PushID 区分；主页真正的滚动宿主是它，内层 `##setting-page-content` 使用 AutoResizeY。必须重置主页宿主 Scroll/ScrollTarget 与 `##drag-scroll-position`、`##drag-scroll-frame`、`##drag-scroll-rebase`，并保证第一次 RenderHome 就以顶部坐标度量，不能先绘制旧滚动坐标再 SetScrollY 留一帧错误落点。BeginScrollChild 在 Begin 前会消费旧 drag storage，单独清 Scroll 不足。
- 原 `ConfigureHomeMotion` 的 `!state.home` 分支会在内部返回主页重播，必须移除该触发；最新人类要求也覆盖预览中“内部导航播短版”的提议。尺寸跳过、禁用动画、取消后该 ShowSerial 仍应被消费；其后放大、开动画或返回主页均不补播。
- `Setting.cpp:2152` 用 settingNavigationClockIdle 在隐藏/移动/遮挡/未渲染后的首个实际帧令 dt=0；`RenderSettingFrame:3959` 各跳过分支统一重基准。新状态继续用这个 delta 和已有总开关/速率，使用 [0,.05] 限幅及 [.1,5] rate，不引入线程、计时器或额外调度器。中途关动画需同帧完全静态；速率变化继续当前阶段而非重播。

### 透明字标、并行书写与全客户区覆盖层

- 当前字标是透明矢量网格，既无 PNG，也无产品专用字标 Shader。Build 中六组原始路径已经包含连笔、倾斜、按速度压感及全局弧长彩虹。复用原采样数据即可；不要从预览截图生成产品资产。
- Build 的 sampled Point.time 为曲率速度时间，Point.distance 为全球累计弧长。首次建立几何时可为每组笔画增加局部标准化进度、截面和索引范围；并行播放每组自己的进度，保留原最终 mesh 和配色。现有索引按“连续主体截面，然后端头”组织，简单截断全局 index prefix 会把端头/转折圆接头提前或遗漏；必须记录每段、接头、首尾圆头的提交范围。
- 推荐一次缓存绘制段及圆头模版，播放时提交已完成段及每组最多一个活动笔头，避免逐帧曲线采样、堆分配或重建全部原始几何。活动笔头只对外轮廓 AA，尤其 k 首尾重合处应保留已修复的共用截面/宽度与最后端头约定（HomeBrand.h:127–144、173–208）；完整阶段直接使用原 Draw，保证最终像素精确一致。
- 已批准 brand-entry-revision.md 明确覆盖层只覆盖 titlebar 32 DIP 之外的客户内容区（导航+页面）。应在 Setting.cpp 内容及导航之后、ImGui Render 之前提交（约 3879–3887 行），使用前景 draw list 与客户内容区 clip，保持标题栏和窗口外不变；caption 关闭/minimize 行为保留。
- RenderHome 仍测量并占位目标字标，只在入场 active 时抑制其原位提交；覆盖层提交唯一字标。最终位置必须来自真实 `motion.brandOrigin`、brandWidth，不能另写一套 DIP 落点公式。入场期间禁用字标视差，完成后再恢复现有真实鼠标规则，避免正在降落的目标被鼠标改变。
- 顺序约定：并行书写完成 → **300 ms 完整停留（覆盖层依旧不透明、字标留中心）** → 覆盖层透明淡出与非线性弧线路径归位同时开始 → 微小单次落定 → 静态原网格。书写后的 300 ms 与起笔等待是不同阶段，不能在停留期提前透出界面或开始位移。
- 非线性路径需同时表达位置曲线与时间曲线：例如有限弯曲的 cubic Bezier P0→P3，以快加速/末段快速落点和小 settle 控制节奏；仅给直线插值加 easing 不满足“路径动画”。所有中间包络应有限、终点精确等于生产位置，不循环弹跳、不拉扁字形。
- 交互取消/阻止底层动作必须明确：覆盖层是绘制命令本身不拦输入。可在 render 前检查真实按键/滚轮/鼠标按下取消入场并正常显示，或对页面与导航使用既有 Disabled/NoInputs 屏障；取消这一帧不得误触底层外链，caption 关闭仍需可用。不是另建控件命中或执行业务的机会。

### 尺寸门禁

- 门禁依据真实有效 DPI/用户倍率、客户区、导航状态、真实字体的 RenderHome 度量。不要仅用 960×700 像素阈值；同一像素窗口在 1 与 1.875 倍内容差别很大。
- 推荐解释“显示不下”为中心字标及整个目标渐变横幅可完整容纳于当前客户区/正文 viewport（不要求全主页后续内容无滚动）。检查目标 hero 与 brand（含下伸和 14 DIP 视差包络）完整包含在外层实际 clip，中心起始框与曲线路径最大包络包含在标题栏之外客户内容区安全边界；按钮窄窗换行及英文标语应计入 heroHeight。
- `RenderHome:332–356` 已实际计算宽度/字号/标题/托管说明/按钮折行以及 heroHeight，避免重复算法。Root shellGeometry.content 是稳定 viewport，但 AutoResizeY 内层新建首帧可能给空 clip；应在真实布局可用后作一次 gate，不能把冷启动临时零高误当小窗，也不能先消费时间让 logo 跳过部分书写。尺寸 gate 失败在当前 ShowSerial 永久静态，不等放大后重播。
- 动画途中 resize/DPI/布局导致包络不再可容纳，应立即取消并静态归位；增大/主题变化不重播。如在用户输入中取消，应与这个终止路径共享稳定收尾，确保没有两个字标或残留覆盖层。

### 无窗口验证与性能

- `setting_session_state_tests.cpp:408–415` 保留 ShowSerial 契约，增加 hide/reset serial 的重复 Hide/并发快速 Hide→Show、Show 可见激活与最小化差异。检查内部 Home→other→Home 不重播，关闭后首次显示目的地/外层滚动为 Home/0。
- 修改 `CheckHomeMotion` 原返回主页重播和反光断言（3187–3203 行）；保留视差、输入来源、按钮 hover、固定命中和滚动回归。新增可注入 delta 的书写首/中/末、严格 300 ms hold、起点/弧线中点/精确终点、取消、disable/rate、size skip 与 resize 取消。
- 既有 WARP `Offscreen::Initialize` 显式要求 FL11.0、不创建 HWND/swap chain，并用生产 imgui_impl_dx11 与嵌入 CSO。`Submit:117–138` CopyResource + Map 等待真实提交完成，支持 RGBA 审阅；当前 1000×1100、简化页面 Frame 需扩展真正 root+导航+前景入场，以验证覆盖范围/透明显露及最终与静态主页一致。
- `CheckTransparentProductionInk:293` 直接从 RenderHome DrawData 提取真实网格验证 k 圆头和 AA；应继续在完整阶段验证 1/1.875/4 倍无孔、字形/配色/透明外缘和 y 下伸，不能用临时另画字标代替生产提交。
- `Benchmark:459` 现有 20 帧预热 +100 帧 CPU 与完成提交 median/p95、顶点/批次、ImGui 分配和 HomeBrand capacity 计数可复用；新增 writing / hold / reveal+dock / settle / stable 分段同机对照。全客户区 alpha 覆盖必然增加像素混合，WARP 成本应实际测量；不能据静态索引数量或 CPU 断言推导帧率。ImGui allocation hook 不统计 std::vector/string 与驱动，额外动态缓存须独立 capacity/growth 验证。
- Full solution `InkeysRepo.sln Debug|ARM64`，动态定位 ARM64 原生 MSBuild；同一 PowerShell invocation 应用 PATH workaround，允许至少 5 分钟。产物 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 中 `animation_tests.cpp:1695–1696` 会调用 session/design tests，设计测试在首次字体 atlas 模拟之前调用 WARP。禁止交互 GUI/Computer Use，不 commit/push。

### Related specs / external references

- `.trellis/spec/native-desktop/rendering-and-ui.md:1451`：共享 WARP、resident 生命周期、渲染/owner 线程与业务 FIFO 合同。
- 同文 `:1938`：WinUI 全页真实字体、DIP、滚动与输入规则；`:2090`：当前品牌几何、AA、动作及证据等级。旧 900 ms 反光及内部回主页重播由最新人类要求替代，需主会话更新该段，不因旧 spec 阻止授权实施。
- `.trellis/spec/native-desktop/build-and-compatibility.md:5`、根 AGENTS：完整 solution + ARM64 Native host + PATH workaround。
- External references: 本次为内部代码研究，未查询外部资料；继续沿用原 D3D11 ImGui 管线，不引入 Win7 基线之外 API/FL11.1。

## Caveats / Not Found

- 此研究只读代码，未编译、未运行 WARP、未创建 GUI；当前 Win11 FL11.0 WARP 与静态 API 基线不能代表 Win7 SP1+KB2670838 实机验证。
- 现有 headless 没有执行完整 Setting.cpp coroutine 的显示/导航闭环；如新增独立 reset helper，须产品和测试共用真实 helper，不能在测试复制 reset 算法声称已覆盖实际关闭行为。
- 初次字体/AutoResizeY 的可见性预热是已有坑；门禁和时钟都应等正确布局，不能靠固定几帧缺席触发或“播放过晚”补进度。
- 按角色隔离没有读取 implement.jsonl/check.jsonl；研究输出写入本 Active task 下，未改产品/规范/计划、未执行任何 Git 操作。
