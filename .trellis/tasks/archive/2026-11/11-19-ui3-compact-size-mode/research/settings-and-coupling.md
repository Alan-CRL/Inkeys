# 调研：设置页"外观"与配置 + 小尺寸改造外围耦合

> 范围：只读调研（2026-11-19，分支 `refactoring/drawpad-ui`）。行号以当前工作树为准。
> 标注约定：【确认】= 读代码确认；【推断】= 依据代码合理推断；【待确认】= 需要用户/后续验证。

---

## A. 设置页"外观与动画"与配置链

### A1. 现状机制

**导航**
- 用户口中的"主题和外观"在代码里是 通用 → `Section::Appearance`，侧栏/页标题键 `SettingsUI/Design/GeneralAppearance`（zh-CN "外观与动画"）。【确认】`Inkeys/Inkeys/UI/Setting/Setting.Navigation.h:25`、`:49`；`Inkeys/src/i18n/zh-CN.jsonc:348`。
- 页面渲染 `RenderGeneralAppearance(GeneralDraft&, GeneralEvents&)`，由 `RenderGeneral` 按 section 分派。【确认】`Setting.Pages.cpp:1429`、`:1890`。

**外观页现有内容（自上而下）**【确认】`Setting.Pages.cpp:1429-1489`

| 分区标题 key | 行 id | 控件 | 绑定 |
| --- | --- | --- | --- |
| `Design.ThemeSection`（"主题"） | `##主栏主题` | `ComboRow`（下拉，深色/浅色） | `draft.themeMode` → `events.themeModeChanged` |
| 同上 | `##设置主题` | `ComboRow`（同选项） | `draft.settingThemeMode` |
| `Design.AnimationSection` | `animation` | `ToggleRow` | `draft.animation` |
| 同上 | `startup-preview` | `ToggleRow` + 内联 `InlineCombo("startup-effect")`，附 note "下次启动生效" | `draft.startupPreview` / `startupEffect` |
| 同上 | `setting-entry-animation` | `ComboRow`（三项） | `draft.settingEntryAnimation` |
| `Design.LightingSection` | `edge-lighting` / `dynamic-edge-lighting`（ConditionalBlock） | `ToggleRow` | |
| `Design.OpacitySection` | `bar-background-opacity` / `bar-content-opacity` | `SettingRow` + `SliderResetAction`（滑块+恢复默认） | |

- 没有现成的"分段按钮"用于主题；但控件库有 `ButtonGroupRow`（见 `settings-attention-notes.md` §2）。主栏主题用的是下拉 `ComboRow`。
- 缩放（`bar-zoom`）、任务栏缩放基准不在外观页，而在 窗口与显示器 → `RenderWindowLayout`（`Setting.Pages.cpp:1491-1506`）。

**草稿/事件结构**【确认】`Setting.Pages.h:319-360`（`GeneralDraft`）、`:361-392`（`GeneralEvents`）
- `GeneralDraft` 前 6 个字段无默认值，Setting.cpp 与 headless 测试都用**位置聚合初始化**（`Setting.cpp:3333-3342`、`setting_design_tests.cpp:4293`）。新字段必须带默认值并追加在已有默认字段区（例如紧跟 `themeMode` 之后），不能插入前 10 个位置参数之内。
- `themeMode` 注释："0=深色，1=浅色；配置值映射在 Setting.cpp 完成"（`Setting.Pages.h:342`）。

**每帧草稿装载 / 事件回写**【确认】`Setting.cpp`
- 会话级缓存：`int ThemeMode = NormalizeThemeMode(config...ThemeMode)`（`Setting.cpp:2340-2341`），透明度同理（`:2342-2345`）。
- 每帧：`draft.themeMode = ThemeMode - ThemeModeDark;`（`:3359`）。
- 回写（只在 `destination.section == Design::Section::Appearance` 时）：`Setting.cpp:3487-3499`
  ```cpp
  ThemeMode = Inkeys::NormalizeThemeMode(draft.themeMode + Inkeys::ThemeModeDark);
  if (Inkeys::config.Experimental.Inkeys3.UI3.ThemeMode != ThemeMode) {
      Inkeys::config.Experimental.Inkeys3.UI3.ThemeMode = ThemeMode;   // 1. 先改 config
      Inkeys::UI::Bar::SetThemeMode(ThemeMode);                         // 2. 发布到 Bar
      QueueConfigWrite();                                               // 3. FIFO 写盘
  }
  ```
- `QueueConfigWrite()` = `QueueBusiness({ SettingBusinessKind::WriteConfig })`，worker 执行时调用 `Inkeys::config.Write()` 写最新完整配置，不在入队时冻结快照（`Setting.cpp:1374-1378`；spec `configuration-i18n-and-assets.md:16`）。

**主栏主题如何通知 Bar 生效（热切换参照）**【确认】
- `Bar::SetThemeMode(int)`：设置线程只 `exchange` 原子目标 `barUISet.barStyle.requestedDarkStyle`，变化时 `barUISet.UpdateRendering(false)` 唤醒（`Bar.Main.cpp:498-505`；字段 `Bar.State.cppm:115-125`）。
- 渲染线程帧首 `SubmitTargetsAndLayout` 比较 `requestedDarkStyle != state.barStyle.darkStyle` 后应用（`Bar.RenderLoop.cpp:2549-2552`）。
- 透明度同理：`SetResidentOpacity` 交换 `requested*Opacity` 后额外唤醒 `PptPageMask()` 与 `SelectionOverlay`（`Bar.Main.cpp:507-519`）。
- **改几何**的设置参照缩放：`SetConfigZoom` → `ApplyConfigZoom(barUISet, zoom, true)` + `PageControl::NotifyLayoutChanged()`（`Bar.Zoom.cppm:237-244`）；`RefreshScaleBase` 也会 `NotifyLayoutChanged()`（`:230-235`）。`NotifyLayoutChanged` 只递增 `publishedRevision` 并 `RequestAll()`（`PageControl.cpp:6651-6658`）。注意 `ApplyConfigZoom` 是在设置线程上直接调用的（`Bar.Zoom.cppm:242`），而主题是"发布原子 → 渲染线程帧首应用"。小尺寸切换会重建大量几何，【推断】应走主题式"原子发布 + 渲染线程帧首应用"，避免设置线程直接改 `barUISet` 几何。

**启动顺序**【确认】`IdtMain.cpp:3344-3360`
- `ReadAll()` 后归一 `ThemeMode`/透明度写回内存 → `config.Write()` → `Bar::SetThemeMode()`、`SetResidentOpacity()`、`SetAnimationOptions()`……
- Bar 初始化时读 `requestedDarkStyle`（`Bar.Initialization.cpp:317`），因此小尺寸模式也必须**在 Bar 初始化之前**发布，否则首帧按大尺寸构建（主按钮 80×80、主栏 80 高写死在 `Bar.Initialization.cpp:336-337`、`:358`、`:378`）。

**配置 schema**【确认】`Inkeys/Inkeys/Other/Other.Config.cppm`
- 唯一用户配置 `Inkeys::config`（main.json），新字段只能加入 `INKEYS_CONFIG_SCHEMA`（spec `configuration-i18n-and-assets.md:9,14`）。
- 主题/透明度位于 `GROUP(Experimental) > GROUP(Inkeys3) > GROUP(UI3)`：`Other.Config.cppm:591`。注意该行是**一个物理行里三个 `X(...)`**（ThemeMode / BackgroundOpacity / ContentOpacity 之间只有 Tab，没有 `\`+换行），属既有格式怪异，新增字段时不要顺手重排（Minimal Diff）。
- 常量与归一函数 `ThemeModeDark=1/Light=2`、`NormalizeThemeMode`、`ThemeModeUsesDarkStyle` 在 `Other.Config.cppm:25-37`；透明度归一 `:39-56`。
- 读取钩子：需要范围校验的整数在 `Other.Config.cpp` 按路径校验，越界回默认（如 `UI.Bar.UseTaskbarHeightAsScaleBase` 在 `Other.Config.cpp:669-680`，`StartupPreview.Effect` 在 `:641-647`）。ThemeMode 没有读取钩子，而是在 `IdtMain` 启动后用 `NormalizeThemeMode` 归一。
- `Other.Config.cppm` 已 `import Inkeys.UI.Bar.Metrics;`（`:19`），用于 `StartupPreviewCachedWidthDefaultDip` 默认值（`:596`）。

**i18n 生成链**【确认】spec `configuration-i18n-and-assets.md:342-357`、`Scripts/i18n.ps1:1-18`
- 基准 `Inkeys/src/i18n/zh-CN.jsonc`；其余 19 个语言文件（de-DE、en-GB、en-US、es-ES、es-MX、fr-FR、id-ID、it-IT、ja-JP、ko-KR、pl-PL、pt-BR、ro-RO、ru-RU、tr-TR、uk-UA、vi-VN、zh-HK、zh-TW）由脚本自动枚举。
- 流程：改 zh-CN.jsonc → `Scripts/i18n.ps1 sync`（生成 `Inkeys/IdtI18nKeys.g.h`、`Scripts/i18n.zh-CN.snapshot.jsonc`，其他语言插入 `[[Need translation: ...]]` 标记）→ 逐语言翻译替换标记 → `i18n.ps1 check`。不得手改 `IdtI18nKeys.g.h`。脚本需 pwsh（用户记忆 settings-nav-test-coupling）。
- 外观现有键：`SettingsUI/Regular/Appearance/{Theme/{N,E,Dark,Light}, SettingTheme, BackgroundOpacity, ContentOpacity, ...}`（`zh-CN.jsonc:654-694`）；分区标题 `SettingsUI/Design/{ThemeSection,AnimationSection,LightingSection,OpacitySection}`（`:361-365`）。

**相关 headless 测试**【确认】`InkeysHeadlessTests/`
- `setting_design_tests.cpp::CheckGeneralSections`（`:4287` 起）：真实 `RenderGeneral` 绘制每个子页，`rowSeen("<row id>")` 判断行归属（`:4318-4333`），`noEdits()` 判定空闲无编辑（`:4348-4364`），断言 draft 值跨页不变（`:4387-4392`）。**现状 `noEdits` 未包含 `themeModeChanged`**；新增 `sizeModeChanged` 时建议加入，并为新行加 `rowSeen` 归属断言。
- `CheckAppearanceIconSet`（`:7701-7713`）：外观页所有图标码位必须存在于内置图标字体；若新行用新码位需加入 `codes` 数组。
- `CheckHierarchicalNavigation`（`:4856`）/ `CheckNavigationTree`（`:4809`）：只在改导航结构时受影响；单纯加一行不改导航表则不受影响。
- `bar_theme_tests.cpp::TestThemeModeConfigPolicy`（`:370-395`）：主题 schema 默认、归一、Config 拷贝"快照"保持；可仿写 `TestSizeModeConfigPolicy`。
- `config_metadata_tests.cpp:522-557`：外观字段经 `ReadMini`/`ReadAll`/`Write` 链往返；若小尺寸进入 StartupPreview 的 Mini 读取列表，应把新路径加入该用例。

### A2. 新增"界面尺寸：大尺寸 / 小尺寸"的切入点

1. **配置字段**（建议）：`Experimental.Inkeys3.UI3.SizeMode : IdtAtomic<int>`，默认 `0`（大尺寸），`1` = 小尺寸。理由：与 `ThemeMode`/透明度同组，StartupPreview 已经 Mini 读取该组（`IdtMain.cpp:3017-3025`）。
   - 在 `Other.Config.cppm` 增加 `SizeModeLarge/SizeModeCompact` 常量和 `NormalizeSizeMode()`（非 0/1 回 0），风格同 `NormalizeThemeMode`。
   - 读取钩子：可在 `Other.Config.cpp` 按 `UseTaskbarHeightAsScaleBase` 方式加整数类型/范围校验，或像 ThemeMode 一样启动后归一（二选一，【待确认】偏好；spec 推荐前者）。
   - 是否改用 `UI.Bar.*` 组：路径命名【待确认】。
2. **启动**：`IdtMain.cpp:3344-3360` 归一后、Bar 初始化前调用新的 `Inkeys::UI::Bar::SetSizeMode(mode)`；StartupPreview Mini 读取列表 `IdtMain.cpp:3017-3025` 增加该路径（见 B4）。
3. **Bar 发布接口**：`Bar.Main.cppm:1715` 旁 `export void SetSizeMode(int) noexcept;`，实现仿 `SetThemeMode`：`barStyle.requestedSizeMode.exchange(...)` → `UpdateRendering(false)`；渲染线程帧首（`Bar.RenderLoop.cpp:2549` 附近）检测变化后：解析当前尺寸 metrics、重排主栏/属性/面板、`Inkeys::UI::PageControl::NotifyLayoutChanged()`、`RenderPipeline::Request(PptPageMask())`。【推断】PageControl 也需要在其自身线程读取同一原子/快照决定分页几何。
4. **设置页**：
   - `GeneralDraft` 增 `int sizeMode = 0;`（放在 `themeMode` 附近，带默认值）；`GeneralEvents` 增 `bool sizeModeChanged = false;`。
   - `RenderGeneralAppearance` 在 `ThemeSection` 下（或新分区，【待确认】）加 `events.sizeModeChanged = ComboRow("##界面尺寸", IA(...UiSize.N), IA(...UiSize.E), "<glyph>", draft.sizeMode, {Large, Small});`。与主题同为下拉，保持一致。
   - `Setting.cpp`：会话缓存 `int SizeMode`（仿 `:2340`）、每帧 `draft.sizeMode = ...`（仿 `:3359`）、Appearance 分支内回写 `config → Bar::SetSizeMode → QueueConfigWrite()`（仿 `:3489-3499`）。
5. **i18n**（建议键，【待确认】命名）：`SettingsUI/Regular/Appearance/UiSize/{N,E,Large,Small}`；zh-CN：N="界面尺寸"，Large="大尺寸"，Small="小尺寸"，E 描述主栏与翻页控件变化。若要求"下次启动生效"，用 `note` 参数复用 `SettingsUI/Design/NextStartupRequired`，不要写进 E（`settings-attention-notes.md` §3）。新增后须 20 语言 sync/翻译/check。
6. **测试**：`CheckGeneralSections` 增 `rowSeen("##界面尺寸")` 只在 Appearance 出现、`noEdits` 包含 `sizeModeChanged`、键盘选择一次只产生一次事件（参照 `:4441-4444` 的 setting-entry-animation 用例）；`bar_theme_tests` 或新文件加 schema 默认/归一/往返测试；必要时 `CheckAppearanceIconSet` 加码位。

---

## B. 小尺寸改造的外围耦合

### B1. 尺寸常量体系与"constexpr → 运行时"

**单一来源**【确认】
- `UI3.Metrics.cppm:11-13`：`ButtonGapDip=5`、`ButtonOneSideDip=32.5`、`ButtonTwoSideDip=OneSide*2+Gap`；`static_assert(ButtonTwoSideDip == 70.0)`（`:33`）。`ResolveButtonVisualMetrics(kind)` 的 `StandardOneOne/TwoOne/TwoTwo/PageHorizontal/PageVertical` 均引用这些常量（`:88-170`）。
- `Bar.Metrics.cppm`：
  - 旧 compact 开关 `BarPptCompactPaginationEnabled=false`（`:12`），`BarPptCompactLongSideDip/ShortSideDip`（`:14-18`）。注意旧 compact 短边 = `ButtonOneSideDip + ButtonGapDip*2 = 42.5`，**不是 35**。
  - `BarMainBarWidthDip/HeightDip=80`（`:30-32`），`BarMainButtonWidthDip/HeightDip=80`（`:38-39`），`BarMainButtonToMainBarGapDip=10`（`:41`）。
  - 默认启动宽度推导 `BarDefault*` 与 `static_assert(...==75/5/380/470)`（`:42-70`）。
  - `ResolveBarPptStandardPaginationGeometry(vertical,right)` 为 constexpr，内部取 `ButtonTwoSideDip`/`ButtonGapDip`（`:99-128`）。
- `UI3.ListLayout.cppm:50-54`：`ListLayoutMetrics{gap, one, two}` **已是运行时可传参的结构**，是最自然的运行时切入点。

**使用面**【确认，grep 统计】
- `ButtonOneSideDip`：12 个文件 60 处（`Bar.ShapeEditor.cpp` 13、`Bar.ConstructionPrompt.cpp` 8、`Bar.Button.cpp` 4、`Bar.GeometryAttribute.cpp` 4 …）。
- `ButtonTwoSideDip`：13 个文件 47 处（`PageControl.cpp` 6、`Bar.Metrics.cppm` 8 …）。
- `ButtonGapDip`：12 个文件 41 处。
- `BarMainBarHeightDip`：`Bar.Button.cpp:2033`（主栏列表布局 `spec.height`）、`Bar.EraserAttributeLayout.cppm:116`、`Bar.RecallAttributeLayout.cppm:71`（均作 `main.Height()` 缺省回退）、`Bar.Initialization.cpp:378`、`StartupPreview.State.cppm:29`。
- 尺寸相关 `static_assert` 只有 `UI3.Metrics.cppm:33`、`Bar.Metrics.cppm:67-70`、`Bar.Theme.cppm:33`（透明度，无关）。**编译期断言并不多**，真正的耦合是大量 `inline constexpr` 派生常量与硬编码字面量。
- 硬编码字面量（非常量引用）：`Bar.Initialization.cpp:213-214`（主按钮 40/40 中心）、`:325-326` 与 `Bar.Zoom.cppm:98-99`（默认位置 `w-80-50`/`h-80-200`）、`:358`（主按钮超椭圆 80×80）、`:370`（logo 高 80）；`Bar.MainLayoutPolicy.cppm:52`（`mainHalfWidthDip=40`、`gapDip=10` 默认值）；`Bar.StatusIndicatorModel.cppm:934`（`mainWidth=80,mainHeight=80` 默认值）；`Bar.BottomDock.cppm:21`（`BarBottomDockCenterThresholdDip=40`，【推断】与主栏半高 40 相关，竖直吸附区需按模式调整）；`Bar.Interaction.cpp:9335`（测试内 `bodyHeightDip=80`）。

**建议的运行时化方式**【推断】
- 保留现有 `inline constexpr` 作为"大尺寸"常量与 `static_assert`（大尺寸 golden 必须逐字节不变），**新增** `struct BarSizeMetrics { gap, one, two, mainBarHeight, mainButtonW/H, mainButtonGap, ... }` 与 `constexpr BarSizeMetrics ResolveBarSizeMetrics(BarSizeMode)`：Large 分支直接返回现有常量，Compact 分支返回新值，并对两套各自 `static_assert`。
- constexpr 纯函数（如 `ResolveBarPptStandardPaginationGeometry`、`ResolveButtonVisualMetrics`）改为接受 `const BarSizeMetrics&`（或增加重载，旧签名委托 Large），仍可 constexpr，测试可在编译期覆盖两模式。
- 渲染线程在帧首把当前模式解析成一份 metrics 快照，布局/绘制/命中统一消费这份快照，不在各处再读原子（避免同帧混用）。
- 主栏布局映射点：`Bar.Button.cpp:2043-2047` 按 `ButtonSizeEnum` 设 `columnSpan/rowSpan`，`:2048` 设 `item.width`——小尺寸在此把 2×2/2×1/1×2 映射为 1×1，`spec.height` 用模式高度。按钮预设本身不改（符合 PRD）。

### B2. StartupPreview（启动占位）

**现状**【确认】
- 常量全部来自 `Bar.Metrics`：`DefaultStartupBarWidthDip=470`、`DefaultStartupBarHeightDip=BarMainBarHeightDip(80)`、圆角 8、主按钮间隙 10、缓存宽度上下限（`StartupPreview.State.cppm:14-31`）。
- 像素尺寸：`ResolveStartupPreviewPixelSize(widthDip, ...)` 高度固定用 `DefaultStartupBarHeightDip`（`StartupPreview.State.cpp:128-138`）。
- 窗口边界：`ResolveStartupPreviewBounds` 角落态写死 `80.0`（`StartupPreview.cpp:851-858`）；`Start()` 传 `DefaultStartupBarHeightDip`（`:889-893`）；诊断 `SnapshotDiagnostics` 角落宽度写死 `80.0`（`:1105-1107`）。
- 缓存宽度：配置 `Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip`（`Other.Config.cppm:596`），只有一个字段，语义为"展开目标总包络 DIP"（spec `startup-preview.md:46,144`）。
- 写入链：Bar 渲染线程首个 committed 帧调用 `NotifyBarFirstCommittedFrame(mainButton->GetW(), mainBarLayoutWidth, !fold)`（`Bar.RenderLoop.cpp:12866-12876`）→ `publishedStartupBarWidthDip`（`StartupPreview.cpp:991-1010`）→ 主线程 `TakeCommittedStartupBarWidthDip` + `ShouldWriteCachedStartupBarWidthDip` 去重后 `config.Write()`（`IdtMain.cpp:4199-4211`）。**只在本进程首帧发布一次**，会话中切换尺寸不会更新缓存。
- Mini 读取：`IdtMain.cpp:3016-3025` 读取 Enable/Effect/ThemeMode/透明度/CachedWidth/Zoom2/ScaleBase/StartupPlacement；`previewOptions` 组装在 `:3074-3091`。

**关键隐患**【确认】
- `CalculateStartupBarTotalWidthDip` 与 `NotifyBarFirstCommittedFrame` 都要求 `mainButtonTargetWidthDip >= CachedStartupBarWidthMinimumDip`，而该下限 = `BarMainButtonWidthDip = 80`（`Bar.Metrics.cppm:63-64`；`StartupPreview.State.cpp:95-98`；`StartupPreview.cpp:996-1003`）。小尺寸主按钮若小于 80 DIP，**首帧宽度会被静默丢弃**、计算回退 470。必须把下限改为按模式（或取两模式主按钮宽度的最小值）。
- 高度 80、角落 80 写死，小尺寸下预览会比真实主栏高一倍多，交接时出现明显跳变。

**建议**【推断】
- 新增按模式的缓存字段，例如 `CachedStartupBarWidthDipCompact`（默认值 = 小尺寸默认展开包络），或保留单字段并另存"缓存所属模式"。推荐双字段：切换模式后下次启动仍能用对应模式的历史宽度，不会把大尺寸 470 用在小尺寸上。
- Mini 读取列表加入 `SizeMode`；`StartOptions` 增 `sizeMode`（或直接传 `heightDip`/`foldedSideDip`）；`ResolveStartupPreviewBounds`、`ResolveStartupPreviewPixelSize`、诊断中的 80 改为按模式取值。
- `NotifyBarFirstCommittedFrame` 附带首帧所用模式，主线程据此写对应字段。
- 小尺寸进度条（居中最大 192 DIP、左右 ≥12 DIP、高 3 DIP 填充）在 35 DIP 高内放得下【推断】；角落进度环外径 32 DIP、3 DIP 描边（spec `startup-preview.md:121`）在缩小后的主按钮内可能放不下，【待确认】小尺寸主按钮尺寸后再定。
- 启动预览在小尺寸下是否仍需要显示（还是直接关闭/沿用大尺寸外形），【待确认】。

### B3. 测试影响面

**Inkeys.exe 内嵌自测（`Inkeys.vcxproj:1055-1075` 编译进产品）**【确认】
- `Bar.EraserAttribute.Test.cpp`（`--bar-eraser-offscreen-test`，`IdtMain.cpp:2070`）：39 处尺寸字面量/常量，如 `:1140`（主按钮 `BarMainButtonWidthDip/HeightDip`）、`:1152`（主栏 `BarDefaultMainBarLayoutWidthDip × BarMainBarHeightDip`）、`:982-1006`（Freeze A2 拆分 70→32.5）、`:1365`（"localized fixed label fits the existing 70 DIP slot"）、`:1693`（自动按钮宽 = `ButtonTwoSideDip + 箭头宽`）。
- `Bar.GeometryAttribute.Test.cpp`（`--ui3-geometry-attribute-test`，`:1908`）：`:228-233` 手工 80×80 主按钮、580×80 主栏、70×70 锚点；`:895-904` 徽标位置按 70/32.5 计算。
- `Bar.MultiTarget.Test.cpp:723-724`：305/80 等表面尺寸。
- `Bar.Golden.Test.cpp`（`--ui3-golden-dump`）：35 个场景 BGRA 必须与基线逐字节相同（spec `ui3-layers.md:158`）。**大尺寸默认路径不得有任何像素变化**；小尺寸场景如需加入 golden，属新增基线。
- `Bar.Host.Test.cpp`、`Bar.Presentation.Test.cpp`、`Bar.AttributeDirty.Test.cpp`：未发现尺寸字面量（grep 0 处），预计只在行为（脏区、呈现）层受间接影响。

**InkeysHeadlessTests（`--no-window`）**【确认，grep 统计】
- `startup_preview_state_tests.cpp`（34 处）：`:868-888` 断言 `380/470/80/8` 推导与非法回退；`:205`、`:278`、`:293` 用 470×80 计算像素/边界。按模式参数化后需补小尺寸用例。
- `bar_bottom_dock_tests.cpp`（29 处）：`:325-336`、`:517-521`、`:872-873` 以 80 DIP 主栏高计算底栏吸附/可见边；`:776-777` 用 380 宽。竖直吸附区按模式调整后需补 35 DIP 用例。
- `page_control_tests.cpp:372-377`：断言 `PptControlLongSideDip==240`、`ShortSideDip==80`、按钮 = `ButtonTwoSideDip`。
- `whiteboard_ui_tests.cpp:248-251`：白板分组 `edgeMargin/bottomMargin/gap == ButtonGapDip`、`itemSizeDip == ButtonTwoSideDip`（来源 `Bar.SurfaceLayout.cppm:62-70`）。
- `eraser_attribute_tests.cpp`（10）、`recall_attribute_tests.cpp`（5）、`animation_tests.cpp`（7）、`window_geometry_tests.cpp`（6，如 `:420-424` 的 80/44）、`ui3_list_layout_tests.cpp`（3）、`status_indicator_tests.cpp`（2）、`setting_design_tests.cpp`（5，多为 380/470 等无关数值）。
- 这些断言大多锁定**大尺寸**常量；只要大尺寸常量不改，现有测试应继续通过，需要的是**新增**小尺寸用例，而不是修改旧断言。【推断】

### B4. 其他外围

- **白板**：白板复用 PageControl 的 PptBottomLeft/Right（spec `whiteboard.md:7`：左栏 305×80、右栏 230×80）；白板分页总走标准布局 `standard=wb||!BarPptCompactPaginationEnabled`（`Bar.Scene.cpp:526`、`:564`）；白板分组布局 `Bar.SurfaceLayout.cppm:62-70` 用 `ButtonTwoSideDip`。白板翻页是否也跟随小尺寸，【待确认】。`Whiteboard.cpp` 本身只 `import Inkeys.UI.Bar` 使用 `BarPageSurface`/背景规格（`:40`、`:54`、`:205`），未直接引用尺寸常量。
- **Freeze**：未发现 `IdtFreezeFrame` 依赖主栏尺寸；Freeze 只作为 A2 按钮参与主栏布局（Whiteboard/Freeze 上下共用一列，见 `Bar.Metrics.cppm:44-46` 注释与 EraserAttribute 测试 `:982-1006`）。小尺寸只有一行高度，A2 两个按钮需横排（由布局研究负责）。
- **PPT 闲置淡化**：`Bar.RenderLoop.cpp:7907-7954` 只做材质 alpha（`pptIdleFade`），保护条件包含各属性面板与 `constructionPrompt.Visible()`；未发现尺寸常量。小尺寸下步骤指示栏脱离主栏后仍应纳入保护条件。【推断】
- **PPT 控件记忆位置**：`PlugIn.PPTHelper.BottomBothWidth/Height` 等实际存的是位置 x/y（`PptSettingsPersistence.h:31-38`），切换尺寸后控件变小，恢复位置时的贴边/夹取是否需重算，【待确认】。
- **Bar.PresentDecision**：`Bar.PresentDecision.cppm` 只处理映射元组/需求/结果（`:30`、`:94`、`:117`、`:192`），未发现尺寸假设。
- **脏区**：`UI3.DirtyRegion.cppm`/`UI3.VisualDirty.cppm` 未引用尺寸常量；但模式切换会整体改变几何，【推断】切换帧应请求整幅重画（主题切换也是整幅重画，见 spec `configuration-i18n-and-assets.md:36`）。
- **初始缩放适配**：`Bar.Zoom.cppm:184-224` `FitInitialAfterMainBarLayout` 用 `mainButton->GetW() + 10 + mainBarWidth` 运行时值，模式无关；但默认位置 `Bar.Zoom.cppm:98-99` 与 `Bar.Initialization.cpp:325-326` 写死 80。

---

## C. 风险

1. **大尺寸像素回归**：golden 35 场景要求逐字节不变；任何把常量改成运行时取值的重构若引入浮点路径差异都会打破基线。
2. **StartupPreview 下限隐患**：`CachedStartupBarWidthMinimumDip = 80` 会让小主按钮的首帧宽度被丢弃并回退 470。
3. **单缓存字段污染**：只有一个 `CachedStartupBarWidthDip`，两模式共用会互相覆盖。
4. **线程模型**：缩放是设置线程直接 `ApplyConfigZoom`，主题是原子发布；小尺寸若照搬缩放写法，设置线程与渲染线程可能同时改几何。
5. **PageControl 独立线程/快照**：分页几何由 PageControl 自己构建（`PageControl.cppm:375-421` 的 `ResolvePptWidgetContracts` 依赖 constexpr 开关），模式必须通过发布版本号 + 快照同步，否则主栏与分页可能出现一帧不同步。
6. **`GeneralDraft` 位置聚合初始化**：新字段插错位置会让 `Setting.cpp:3333` 与测试 `:4293` 的初始化错位而不报错（类型兼容时）。
7. **配置 schema 行格式**：`Other.Config.cppm:591` 一行三个 `X`，编辑时容易误删/误并。
8. **i18n**：20 语言都要完成翻译，`check` 不允许遗留 `[[Need translation:` 标记；设置导航测试对侧栏结构敏感（只加行不改导航表则不受影响）。
9. **竖直吸附**：`BarBottomDockCenterThresholdDip=40` 等常量疑似以 80 DIP 主栏为前提，小尺寸下吸附区过大。

## D. 待确认问题

1. 尺寸切换是**立即生效**（热切换），还是"下次启动生效"（可大幅降低风险：只需启动时选择 metrics，用 note 提示）？
2. 小尺寸具体网格：35 DIP 高 = 1×1 边长 25 + 上下间隙 5？还是保留 32.5 边长、间隙改为 1.25？旧 compact 分页短边是 42.5 DIP，与 35 不一致，翻页控件取哪个高度？
3. 小尺寸主按钮尺寸（"整体缩小"到多少，35×35？）——决定 StartupPreview 角落外形、进度环能否放下、状态指示器扇形位置。
4. 配置字段路径/命名：`Experimental.Inkeys3.UI3.SizeMode`（与主题同组）还是 `UI.Bar.SizeMode`？取值 0=大/1=小是否可以？
5. 设置控件放在"主题"分区内，还是新开"尺寸"分区？用下拉（与主题一致）还是分段按钮？
6. 白板翻页控件（305×80 / 230×80）是否也进入小尺寸？
7. 小尺寸下是否保留启动加载预览；若保留，是否接受为小尺寸单独缓存宽度（新增配置字段）？
