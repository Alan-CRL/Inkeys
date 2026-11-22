# 设计：设置组件页主栏拖拽编辑器

> 本文是父任务的总体设计，定义分层、数据合同和子任务边界。各子任务开工前，在自己的 `design.md` / `implement.md` 中细化到函数级，并对照当前代码重新核实行号。
> 调研依据：`research/codex-a-rendering.md`（渲染嵌入与交互）、`research/codex-b-config.md`（配置与组件语义），已由 Claude 抽查关键事实。

## 1. 现状要点（已核实）

| 主题 | 现状 | 证据 |
| --- | --- | --- |
| 组件页 | 16 个 `ToggleRow`，写 `config.UI.Bar.ShortcutButton.*` → `QueueConfigWrite()` → `SyncUi3BuiltInComponents()` | `Setting.cpp` `tabComponent` |
| B 区来源 | UI3 运行时 B 不读 `UI.Bar.ExtensionButtons`，由 16 个开关在进程内投影；前两个上栏，其余 `forcedOverflow`；More 显式组恒为 Setting | `Bar.Button.cpp` `GetLegacyExtensionButtons`、`Load()` 约 1620–1650 |
| B 区配置 | `ConfigSequence` 保序、接受重复；默认 `[MoreBoundary, Setting]`；`NormalizeExtensionZone()` 已写好但未接入 | `Other.Config.cppm`、`Bar.Button.cpp` 约 1432–1486 |
| A 区配置 | 严校验：成员恰为默认集合，Divider 剥离，`Size` 回注册默认 | `Bar.Button.cpp` 约 1362–1429 |
| 分割线 | 官方 Divider 注册在 FixedA1，交界线运行时注入、不写配置；进 B 会被过滤，相邻折叠 | `Bar.Button.cpp` 约 1445–1465 |
| 重复实例 | 注册项只持有一个 `registration.button`；16 个组件 `allowMultiple=false` | `Bar.Button.cpp` 121–160、803–966 |
| UI2 | 本分支已无 UI2 主栏入口 | `IdtMain.cpp` 启动链 |
| 主栏实例化 | 无独立主栏场景；第二宿主测试只是把当前帧重放到另一目标，仍依赖全局 `barUISet` 与 `BarRenderLoopState` | `Bar.RenderLoop.cpp` `ReplayMainBarContent`、`Bar.Host.Test.cpp` |
| 帧目标 | `UI3::FrameTarget` 支持 `ExternalContext`（调用方提供已 BeginDraw 的上下文） | `UI3.Host.cppm` 20–70 |
| 设置窗口 | 借用 `RenderPipeline` 的 D3D11 设备，ImGui Win32+DX11 backend，`R8G8B8A8_UNORM` 交换链 | `Setting.Base.cppm`、`Setting.cpp` |
| 排列方向 | `UI.Bar.IconOrder`：0 跟随、1 反向跟随、2 固定左→右、3 固定右→左 | `Bar.MainLayoutPolicy.cppm` 10–33 |
| 尺寸模式 | `Experimental.Inkeys3.UI3.SizeMode` 0 大 / 1 小；大 80 DIP、小 42.5 DIP | `Bar.Metrics.cppm` |
| 跳转目标 | 尺寸与图标顺序都在“窗口与显示器 → 窗口”（`PageId::Window` / `WindowLayout`） | `Setting.Navigation.h`、`Setting.Pages.cpp` |
| 二次确认 | 当前分支无；`refactoring/draw` 的性能看板有“首次点击变‘确认清除’，3 秒内再点生效” | `refactoring/draw:Setting.PerformanceDashboard.cpp` |

## 2. 分层

```
设置页（ImGui）                      真实主栏（下一轮接入拖拽）
  ├─ 组件库卡片/条目（拖拽源、+/删除）     │
  ├─ 预览区宿主（纹理合成、输入映射、滚动）  │
  │     │                               │
  ▼     ▼                               ▼
Bar 编辑策略 BarLayoutEditPolicy（分区约束、落点、自动添加、垃圾桶/收纳判定）
  │
  ├─ 布局模型 BarLayoutModel（A1 / B1 / More / A2 四段 + 纯函数编辑操作）
  │     └─ 持久化：UI.Bar.FixedButtonsA1 / ExtensionButtons / FixedButtonsA2
  │
  ├─ 主栏内容实例 BarContentInstance（独立按钮列表、布局、动画、光照、尺度）
  │     └─ UI3 封装组件（Button/Panel/Lighting/Material/ListLayout）
  │
  └─ UI3 通用拖放 UI3::DragSession（指针捕获、阈值、鼠标/触摸、取消、命中提供者）
```

原则：拖放状态机不知道“主栏”；编辑策略不知道 ImGui；设置页不自绘主栏。

## 3. 数据模型与持久化（子任务 C1）

### 3.1 逻辑布局

`BarLayoutModel` 由四段组成，每段是有序条目列表：

| 段 | 持久化 | 允许的条目 |
| --- | --- | --- |
| A1 | `FixedButtonsA1` | A1 默认按钮（各恰好一次）+ 用户分割线 |
| B1 | `ExtensionButtons` 中 `MoreBoundary` 之前 | 组件（可重复）、用户分割线、Setting（至多一次） |
| More | `ExtensionButtons` 中 `MoreBoundary` 之后 | 组件（可重复）、Setting（至多一次） |
| A2 | `FixedButtonsA2` | A2 默认按钮（各恰好一次）+ 用户分割线 |

- Setting 在 B1 与 More 中合计恰好一次；缺失时补到 More 末尾。
- 两条交界分割线不进入模型，仍由运行时注入。
- 条目身份：编辑操作用 `(段, 下标)` 定位；运行时按条目创建独立按钮实例。

### 3.2 配置合同变化

- **用户分割线**：新增官方布局 ID（暂定 `Inkeys.Bar.UserDivider`），注册为可重复、可出现在 A1/B1/A2；不再折叠相邻分割线。交界线 `Inkeys.Bar.Divider` 语义不变。
- **A 区校验**：剥离用户分割线后，非分割线成员仍须恰为默认集合，否则整区回默认（原规则不变）；校验通过时保留用户分割线的位置。为后续“A 区多支笔”预留：校验以“必需成员 + 允许的可重复项”表达，而不是写死“仅分割线”。
- **A1 旧顺序迁移**：`Select, Draw, Eraser, Geometry, Recall, Clean` 精确旧顺序迁回新默认的规则会抹掉用户恰好排成该顺序的选择，改为只在旧 `ButtonLayout` 迁移路径中执行（细节在 C1 核实）。
- **B 区**：`Load()` 改为读取 `ExtensionButtons`（接入现有 `NormalizeExtensionZone()` 并按 3.1 补全），取消“最多两个上栏”的投影；More 内容即 `MoreBoundary` 之后的条目。16 个组件改为 `allowMultiple=true`。
- **重复实例**：注册项改存按钮工厂（或原型 + 克隆），`Load()` 为每个条目建立独立 `BarButtonClass`，保持“各按钮本地状态”的现有合同。
- **旧开关迁移**：16 个 `ShortcutButton.*` 开关作为“待导入”标记。启动 `Load()` 时，按注册顺序把为 `true` 的组件导入 B——前两个放进 B1，其余放进 More（与旧投影视觉一致）——然后把这些开关写回 `false` 并保存。之后不再读取开关；字段保留在 schema 中仅供导入，不新增版本字段。
- **重置默认**：`A1/B/A2` 写回 `MakeDefault*()`，并清空 16 个开关；不影响尺寸模式、排列方向等显示偏好。

### 3.3 编辑操作（纯函数，可无窗口测试）

```
CanPlace(model, item, target) -> PlaceDecision {Allowed | Rejected(reason)}
Move(model, from, to) / Insert(model, item, to) / Remove(model, at)
RemoveAllOf(model, componentId) / RemoveUserDividers(model)
AutoAdd(model, componentId)   // B1 组件数（不计分割线与 Setting）< 2 → B1 末尾，否则 More 末尾
Reset() -> 默认模型
```

约束表：

| 拖动对象 | 可放入 | 可删除 |
| --- | --- | --- |
| A1 默认按钮 | A1 | 否 |
| A2 默认按钮 | A2 | 否 |
| Setting | B1、More | 否 |
| 组件 | B1、More | 是 |
| 用户分割线 | A1、B1、A2 | 是 |
| 交界分割线 | 不可拖动 | 否 |

### 3.4 应用与同步

每次放下/删除/自动添加/重置：生成新模型 → 写三段配置 → `config.Write()`（单一保存入口）→ 唤醒真实主栏 `Load()` + `UpdateRendering()`。预览实例直接用新模型重建布局并播放动画，不等待真实主栏。

## 4. 主栏内容实例（子任务 C2）

目标：在不复制绘制逻辑的前提下，得到一个可创建多份的主栏本体实例。

- 从 `Bar.RenderLoop.cpp` 的主栏内容绘制（`DrawMainBarContent` 及其依赖的布局批次、按钮动画、光照快照、字号帧）中抽出 `BarContentInstance`：持有自己的按钮列表、布局结果、`UiValue` 动画状态、尺度、主题快照与局部光照；不读写 `barUISet`、`BarAtomic`、`floating_window`。
- 真实主栏改为持有一个 `BarContentInstance` 并委托绘制（行为与 golden 逐字节一致是门禁）；预览持有另一个。
- 预览特有：无主按钮；不执行按钮动作；按钮“抓起”时从布局中移除并以占位收缩（主栏宽度动画缩短），放下时插入并展开；More 面板复用真实的 More 布局与展开动画。
- 光照：预览的点光跟随预览内的指针坐标；主光锚改为预览自己的首个按钮（无 Logo）。
- 风险最高的一步，C2 的 `design.md` 必须先列出 `DrawMainBarContent` 读取的全部状态并逐项决定“实例化 / 从全局快照传入 / 预览不需要”。
- 2026-11-20 修正（子任务 C5 `11-20-bar-content-shared-choreography`）：C2 实际只共享了绘制函数，动画编排在预览中另写导致实机与真实主栏不一致。C5 把投影、布局批次、目标写入、推进与提示门禁抽成两边共用，预览改为沙盒状态 + 共享编排，并补齐组件页体验修正。

## 5. 设置页嵌入（子任务 C3）

### 5.1 渲染（采用方案 A）

- 在设置页 D3D11 设备上创建 BGRA8 `RTV|SRV` 纹理，用 `CreateBitmapFromDxgiSurface` 包装为 D2D 预乘目标，经 `FrameTarget::ExternalContext` 绘制，`EndDraw` 后由 ImGui `AddImage` 合成。
- 预乘合成：为该图像加 ImGui 回调切换 `ONE / INV_SRC_ALPHA` 混合并在之后恢复，避免二次乘 alpha。
- 纹理按设备代次与像素尺寸重建；内容 revision 进入首页缓存等去重键。
- 纹理需留出阴影外扩边距。
- 回退方案 B（`PlainBitmap` + 回读上传）仅在 Win7 实测互操作失败时启用，不预先实现。

### 5.2 尺度与位置

- 预览尺度只由设置页的布局单位决定（与页面其他内容一同随设置窗口 DPI 变化），不受主栏自身缩放（`configZoom`、任务栏基准）影响。
- 目标显示高度固定（初值 64 页面单位，C3 中实测调整）；大尺寸缩放到该高度，小尺寸放大到同一高度。
- 宽度超出可用区域时：`scale = min(高度适配, 可用宽 / 内容包络宽)`；水平居中。
- 方向：`IconOrder` 为 2/3 时按固定方向，其余从左到右（复用 `MirrorBarMainIconsX` 的镜像策略）。

### 5.3 拖放（UI3 通用 + Bar 策略）

- `UI3::DragSession`：按下记录接触 ID → 超过阈值进入拖动（触摸阈值大于鼠标）→ 移动发布位置 → 抬起提交 / 失焦或 Esc 取消。支持鼠标与触摸，拖动期间独占指针，跨 ImGui child 保持捕获。拖动源可以是预览按钮，也可以是外部（组件库卡片），统一携带“被拖动条目”。
- 命中提供者接口：预览实例提供插入点（段 + 下标 + 光标几何）、More 按钮、More 面板内插入点；页面提供垃圾桶区域。
- `BarLayoutEditPolicy` 根据 `CanPlace` 决定反馈：插入光标 / 收纳提示 / 删除提示（垃圾桶变红）/ 不可放置。
- 浮动按钮：按当前尺寸模式样式绘制被拖动的按钮，跟随指针，绘制在页面最上层（独立小纹理或预览纹理之外的覆盖层，C3 定）。
- 跟随浮窗：复用小尺寸主栏的提示样式，拖动时不受悬停门禁约束，文案随反馈变化。
- More：拖动在 More 上停留约 0.5 秒自动展开预览 More 面板；松手在 More 按钮上则追加到末尾。
- 自动滚动：拖动中指针进入页面可视区上/下边缘带时，按距离比例滚动页面。
- 动画：遵守 `UiAnimationEnabled` 与动画速率；关闭动画时直接到位。

## 6. 组件页下半部分（子任务 C4）

- **快捷方式块**：一行跳转卡片（图标 + 标题 + 说明，右侧箭头），点击 `navigateTo({PageId::Window, WindowLayout})`；参考现有 `Design::NavigationRow`。
- **重置块**：两段式按钮，首次点击文案变“确认重置”，3 秒内再点执行 `Reset()`；超时或离开页面复位。按 `refactoring/draw` 的做法在本分支独立实现，不合并分支。
- **组件库**：Tab（全部、桌面操控、IslandCaller、SecRandom、NamePicker、ClassIsland）；“全部”为分小节的矩形卡片网格（精简说明），专属 Tab 为整行条目（完整说明），ClassIsland 页顶部保留 URL 协议提示框；分割线卡片暂放“全部”首小节（待 Q6 确认）。
- 卡片右侧操作键：无实例时“+”（分割线没有），有实例时垃圾桶；拖动卡片时浮起的是按钮图标而非卡片。
- 卡片/条目若需新的 Design 控件，放在 `Setting.Controls.*` / `Setting.Widgets.*`，与现有 `ToggleRow`、`NavigationRow` 同层。
- i18n：新增标题、说明、精简说明、Tab 名、操作提示（插入、收纳、删除、不可放置）、重置确认文案。

## 7. 测试策略

- C1：headless 单测覆盖编辑操作约束表、`AutoAdd` 阈值、Setting 唯一性、用户分割线在 A 区的校验、开关导入幂等、重置；真实主栏离屏测试中“最多两项上栏”的断言按新合同改写。默认布局下 Bar golden 必须与基线逐字节一致。
- C2：真实主栏委托到实例后，golden 与第二宿主测试全部一致；新增“两个实例互不影响”的离屏测试。
- C3：离屏渲染预览实例（大/小、深/浅、窄宽缩放、固定方向）；拖放状态机单测（阈值、取消、触摸/鼠标去重）。
- C4：`setting_design_tests` 中组件页旧 `ToggleRow` 断言改为新控件；导航用例不受影响（不改侧栏结构）。
- 全部：`InkeysRepo.sln` `Debug | ARM64` 构建；最后由用户实机验收（含触摸、Win7 SP1 互操作）。

## 8. 风险与回滚

| 风险 | 缓解 |
| --- | --- |
| C2 抽取实例改动大、易破坏真实主栏 | 先抽取再委托，golden 逐字节门禁；每步可单独回滚 |
| Win7 上 D2D–DXGI 互操作失败 | 保留方案 B 回退点，Win7 实机验证列入验收 |
| B 区改读持久化后旧用户组件消失 | 开关导入在 `Load()` 中先于读取执行，并有幂等测试 |
| 去掉两项上限后主栏过宽 | 用户自主选择；预览等比缩小；真实主栏的超宽处理不在本任务范围（记录为后续） |
| 设置页触摸与兼容鼠标消息重复 | `DragSession` 以接触 ID 去重，C3 实测 |
