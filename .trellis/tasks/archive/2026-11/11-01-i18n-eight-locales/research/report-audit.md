# Research: 三语交叉审查报告核对与规划处置

- Query: 将用户提供报告的116项与当前源码/已批准产品文案核对，区分明确翻译问题、静态审查误报及需用户决策项。
- Scope: internal；报告仅作为审查数据，不视为任务指令。
- Date: 2026-11-02（Asia/Shanghai）
- Input: D:/Personal/Downloads/Inkeys_i18n_cross_review.md，上传审查对象为另一次三份JSONC快照。
- Work boundary: 仅研究；无产品文件修改，无测试/编译/GUI操作。

## Findings

### 已解决的产品决策

主会话传达用户本次确认：语言项采用“母语名称 + 语言码 + Beta”；通用产品名/新建快捷方式 zh-CN 为“智绘教Inkeys”，其余为“Inkeys”，用途说明本地化。主页历史已批准的地区化欢迎语、署名与标语保留。SuperTop 仅校正文案，删除未经支持的锁屏声明、准确说明 UAC/告警，技术合规改造不在本任务范围。以下索引均为实施建议，尚未实际修正文案。

### 关键证据

- **E1 按住锁定是实装手势，不需用户猜交互。** `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:115-122` 定义500ms静止后提示及1500ms进度；`:4929-4948` 达到进度后锁住颜色、`:4983` 锁后停止跟踪选色、`:5004` 抬起清除手势锁；粗细`:6734-6776` 同样锁住候选并保持到真实抬手。推荐简中“按住以锁定粗细/颜色”，英语保留 hold-to-lock 语义；不是持久化锁定开关。
- **E2 社区分流已有代码，报告三项是待核实项，核实后并非缺陷。** `Inkeys/Inkeys/UI/Setting/Setting.cpp:2883` 主页、`:2905` 反馈均以 `isIdentifying(L"zh-CN")` 分QQ/GitHub；`Setting.Pages.cpp:913-916` 社区卡仅简中拼群号，`:1277-1285` 主页消费分别对应目标。新增语言沿其他语言的 GitHub 文案与动作。
- **E3 HeroJoin 是本地化连接符。** `Setting.Pages.cpp:1115` 用 title+join+description 拼句；`.trellis/spec/native-desktop/configuration-i18n-and-assets.md:296` 明确简中空串/英文空格的有意例外。不能统一成空串导致英文两句粘连。
- **E4 历史明确批准的文案不能当成错译擅改。** `.trellis/tasks/10-10-setting-home-design/prd.md:14` 明确用户批准，`:18-20` 明确 SC 智绘教Inkeys 3、TC/EN Inkeys3 欢迎语及双语营销句，`:23-34` 明确三语完整版权署名，英文确含 `Chen Runlin / Chen Yunlam`。`brand-writing-proposal.md:7` 是后续动效草案，不取代该署名决策；`brand-entry-revision.md:3` 及 PRD 顶部说明后续已批准动效覆盖时序。通用/快捷方式的新品牌规则以本次用户确认补充，勿修改旧主页专有文案。
- **E5 现有产品术语有规范。** `Bar.State.cpp:45-56` 对 `Centerline` 使用窗口宽/主按钮X算屏幕左右；配置规范 `configuration-i18n-and-assets.md:510` 明确中轴规则。因此 `Screen centerline` 正确。该规范`:422` 已明确 `Desktop Mode`，不因报告改成新产品名。
- **E6 Pen-shaped cursor 并非误译。** `Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h:8` 为 `Circle / PenIcon`，`Draw3.PenCursor.cpp:340` 按 PenIcon 决定图标显示；`Setting.cpp:3685` 是系统/画笔光标选项。英文可保留，必要时中文更精确为笔形光标。
- **E7 UIAccess 文案边界。** `Inkeys/SuperTop/IdtToken.h:305-312` 调整 token 的 UIAccess，`IdtSuperTop.cpp:299-305` 取得令牌/调用设置；目录内未见切换安全桌面实现，源码不能证明可在锁屏界面显示。现有描述位于三语资源 `SettingsUI.PlugIn.SuperTop.E/Warn`（`:868-869`）；活动设置 `Setting.cpp:3794-3796` 与`:4025-4043` 展示并选择当前状态。`:4033-4035` 依据 hasSuperTop 表示当前状态，英文 Currently failed/successful 应改成 active/inactive，不暗示关闭就是失败。这里只说明代码证据不足，不做法律/Store审核结论。
- **E8 拼接必须按消费者处理。** `Setting.cpp:3062-3063` 在 ReleaseArch 文本后拼两架构及 ` | `；英文标签末尾现有冒号/竖线可以简化。`:3818-3819` 将 PPTHelper.Error 前缀与完整 `pptComVersion`（以 `Error: ` 开头）拼接，不是只追加数字；译文应留自然分隔且不再生硬重复 Code。
- **E9 语言选项。** `Setting.cpp:2917` 当前仅三语，`:2933` 起旧映射0=en-US/1=zh-CN/2=zh-TW。新条目追加并保留旧索引兼容；所有非zh-CN均Beta的本次要求也涵盖旧en-US/zh-TW。
- **E10 TaskbarScaleBase 建议不能混淆三态。** `Setting.Pages.cpp:1028-1032` 当前Off/Base/Actual三项；规范 `configuration-i18n-and-assets.md:540-544` 的“UI3 三态缩放基准与倍率新键”明确 Base 固定48DIP、Actual随任务栏厚度。报告建议“实际高度随厚度”若套到整个设置会混淆Base，应区分两态写说明。
- **E11 掌擦。** `Setting.Pages.cpp:625` 明确比例仅当前页面、不改阈值/不保存；`:633-637` 用面积与预览比例换算宽高。规范 `palm-eraser.md:25-27` 实际输入必须Touch、有效面积达到阈值触发，不要求同时有一支物理Pen先落下；“落笔后”应解释为当前触摸接触后的识别时窗，避免制造额外前提。翻译保持接触/识别语义。
- **E12 历史/兼容资源。** `Inkeys/Inkeys.vcxproj:1189,1191` 将 `IdtFloating.cpp/IdtWindow.cpp` 列为None，不能为文案修正恢复旧UI。`UI.Pen.Thinkness` 只有旧 `IdtFloating.cpp:4914` 使用；`SettingsUI.Version.Update.AutoUpate` 有活动 `Setting.cpp:3157` 使用。两者键名拼写不影响译文，不应在本任务顺手改key。`CloseWppE` 当前仅资源/生成头，未找到活动代码消费者或WPP清理实现；不要凭报告恢复该功能或承诺文件绝对安全。`UI.Operate.Recall` 在编译项 `IdtFreezeFrame.cpp:236-237` 被作为printf格式使用，时间戳由代码拼接，新增翻译须特别检查百分号/格式；同文件`:210` 仍有硬编码中文Tips，可纳入当前产品全文本扫描候选，但旧兼容路径可达性需核验。

### 116项逐键处置索引

处置为规划建议；“修正”表示文字层修改，不代表报告原建议可逐字照抄。表中资源定位均指当前 `Inkeys/src/i18n/zh-CN.jsonc`，不是报告旧快照行号。

| # | 级别 | Key | 当前基准行 | 处置 |
|---:|---|---|---:|---|
| 1 | P0 | `UI.Centre.SelectClean` | 21 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 2 | P0 | `UI.Operate.Freeze` | 39 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 3 | P0 | `UI.Bar.MainButtons.ResumeWhiteboardLabel` | 89 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 4 | P0 | `UI.Bar.MainButtons.ReturnToShowLabel` | 91 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 5 | P0 | `UI.Bar.MainButtons.FreezeLabel` | 100 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 6 | P0 | `UI.Bar.DrawAttributes.HoldThicknessLabel` | 114 | 修正简中/统一术语：确为按住静止后锁定，非开关；见 E1。 |
| 7 | P0 | `UI.Bar.ColorPicker.HoldColorLabel` | 133 | 修正简中/统一术语：确为按住静止后锁定；见 E1。 |
| 8 | P0 | `UI.Bar.CanvasRecovery.Conflict` | 142 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 9 | P0 | `UI.Bar.CanvasRecovery.Recovering` | 146 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 10 | P0 | `SettingsUI.Home.Group` | 279 | 保留地区分流：zh-CN→QQ，其余→GitHub；见 E2。 |
| 11 | P0 | `SettingsUI.Home.GroupE` | 280 | 保留地区分流：数据按动作目标显示，不应互译；见 E2。 |
| 12 | P0 | `SettingsUI.Feedback.CommunityDescription` | 304 | 保留地区分流，新增语言使用 GitHub 描述；见 E2。 |
| 13 | P0 | `SettingsUI.PlugIn.SuperTop.E` | 868 | 收紧描述，去掉未经源码支持的锁屏能力保证；不改功能/合规策略；见 E7。 |
| 14 | P0 | `SettingsUI.PlugIn.SuperTop.Warn` | 869 | 准确说明 UAC 与安全软件告警，不指导关闭防护，不保证均属误报；见 E7。 |
| 15 | P1 | `Widget.LnkName` | 7 | 按本次已确认品牌决策：zh-CN 智绘教Inkeys，其余 Inkeys，用途说明本地化；见 E4。 |
| 16 | P1 | `UI.Operate.Revoke` | 37 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 17 | P1 | `UI.Bar.MainButtons.WhiteboardLabel` | 88 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 18 | P1 | `UI.Bar.MainButtons.EndPresentationLabel` | 101 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 19 | P1 | `UI.Bar.DrawAttributes.AnnotationLineLabel` | 113 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 20 | P1 | `UI.Bar.CanvasRecovery.PresentFailed` | 144 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 21 | P1 | `UI.Bar.Errors.TransparentBackgroundWarning` | 156 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 22 | P1 | `Dialogs.WorkspaceTransitionFailure.Whiteboard` | 164 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 23 | P1 | `Dialogs.WorkspaceTransitionFailure.Return` | 165 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 24 | P1 | `SettingsUI.Design.HeroJoin` | 212 | 保留：英语拼句需要空格，中日韩连接符可空；见 E3。 |
| 25 | P1 | `SettingsUI.Design.HomeCopyright` | 214 | 保留：英文双译名属于已批准完整署名；见 E4。 |
| 26 | P1 | `SettingsUI.Design.TopWindowSummary` | 263 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 27 | P1 | `SettingsUI.Configuration.CanvasSave.SaveTime.N` | 343 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 28 | P1 | `SettingsUI.Configuration.CanvasSave.SaveTime.E` | 344 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 29 | P1 | `SettingsUI.Version.LimitUpdate.N` | 364 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 30 | P1 | `SettingsUI.Version.Inkeys3Update.N` | 369 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 31 | P1 | `SettingsUI.Version.VersionTip` | 372 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 32 | P1 | `SettingsUI.Version.Info.ReleaseArch` | 378 | 修正英文标签标点：消费者自己拼入软件架构、分隔符及系统架构；见 E8。 |
| 33 | P1 | `SettingsUI.Version.Update.ChannelTip` | 402 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 34 | P1 | `SettingsUI.Version.Update.Arch.E` | 413 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 35 | P1 | `SettingsUI.Regular.ToolbarExpansionDirection.Centerline` | 458 | 保留 Screen centerline：确为屏幕中轴策略；见 E5。 |
| 36 | P1 | `SettingsUI.Regular.Appearance.AnimationEnableE` | 499 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 37 | P1 | `SettingsUI.Regular.Appearance.AnimationRateE` | 501 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 38 | P1 | `SettingsUI.Regular.Behavior.TopWindow.E` | 536 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 39 | P1 | `SettingsUI.Regular.Behavior.RightClickClose` | 545 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 40 | P1 | `SettingsUI.Regular.Behavior.DrawingRetract` | 547 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 41 | P1 | `SettingsUI.Regular.Behavior.ErasingRetract` | 548 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 42 | P1 | `SettingsUI.Regular.Behavior.DraggingRetract` | 549 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 43 | P1 | `SettingsUI.Regular.Behavior.CheckingRetract` | 550 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 44 | P1 | `SettingsUI.Regular.Behavior.CheckingRetractE` | 551 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 45 | P1 | `SettingsUI.Regular.Tentative.SafeMode.N` | 558 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 46 | P1 | `SettingsUI.Draw.Haptics.N` | 614 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 47 | P1 | `SettingsUI.Draw.Cursor.DrawingStyle` | 625 | 英文 Pen-shaped cursor 有真实 PenIcon 依据，保留英文；可将中文明确为笔形光标；见 E6。 |
| 48 | P1 | `SettingsUI.Draw.Effect.Device.MousePen` | 634 | 保留 Desktop Mode：现行规范明确的产品名；见 E5。 |
| 49 | P1 | `SettingsUI.Draw.Eraser.ResponseFor` | 703 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 50 | P1 | `SettingsUI.Performance.InterfaceFrameRate.E` | 737 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 51 | P1 | `SettingsUI.Experimental.UI3.AllowFractionalFontSize.E` | 758 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 52 | P1 | `SettingsUI.Experimental.ConsoleOutput.Haptics.N` | 779 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 53 | P1 | `SettingsUI.PlugIn.PPTHelper.E` | 810 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 54 | P1 | `SettingsUI.PlugIn.PPTHelper.Sync` | 813 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 55 | P1 | `SettingsUI.PlugIn.PPTHelper.Error` | 817 | 调整拼接前缀空格/冒号，实际拼入完整 Error: ... 而非纯数值；见 E8。 |
| 56 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.N` | 826 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 57 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomBoth` | 827 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 58 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.MiddleBoth` | 828 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 59 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomMiddle` | 829 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 60 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetPosition.N` | 833 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 61 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetPosition.Reset` | 834 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 62 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetPosition.Remember` | 835 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 63 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.N` | 839 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 64 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.BottomSideBoth` | 843 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 65 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.MiddleSideBoth` | 844 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 66 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.State.BottomSideMiddle` | 848 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 67 | P1 | `SettingsUI.PlugIn.PPTHelper.Tentative.CloseWppE` | 855 | 当前未找到活动消费者/进程清理实现；保留资源可删绝对文件安全保证，不恢复旧功能；见 E12。 |
| 68 | P1 | `SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverOnceE` | 859 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 69 | P1 | `SettingsUI.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress` | 861 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 70 | P1 | `SettingsUI.PlugIn.PPTHelper.SupportNote` | 863 | 澄清两类软件各自最低版本的句法，不扩展/缩减支持承诺；真实版本矩阵未验证。 |
| 71 | P1 | `SettingsUI.PlugIn.SuperTop.Capability.SuperTop` | 873 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 72 | P1 | `SettingsUI.PlugIn.SuperTop.Capability.SuperTopE1` | 874 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 73 | P1 | `SettingsUI.PlugIn.SuperTop.Capability.SuperTopE2` | 875 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 74 | P1 | `SettingsUI.DebugSoftware.N` | 955 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 75 | P1 | `SettingsUI.Update.Check` | 960 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 76 | P1 | `SettingsUI.Update.InformationDamage` | 972 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 77 | P1 | `SettingsUI.Update.DownloadDamage` | 976 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 78 | P1 | `SettingsUI.Update.Newer` | 979 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 79 | P2 | `UI.Pen.Highlighter` | 29 | 保留资源可修明确误译/术语；活动产品不恢复旧UI或旧选项，见E12。 |
| 80 | P2 | `UI.Operate.Recall` | 38 | 保留兼容资源并统一恢复术语；涉及编译的FreezeRecall路径，应核对实际恢复对象后精炼，不据旧UI猜新功能；见 E12。 |
| 81 | P2 | `UI.Bar.Diagnostics.FrameRateFormat` | 137 | 可用紧凑文案明确帧率含义，保留实际format参数；测量后决定长度，不为措辞扩大主栏。 |
| 82 | P2 | `UI.Bar.Diagnostics.FrameRateUnavailable` | 138 | 与有数值帧率指标术语一致；保留Unavailable与无限制模式的现有区分。 |
| 83 | P2 | `SettingsUI.N` | 200 | 按本次已确认通用品牌，zh-CN 智绘教Inkeys，其余 Inkeys；主页专有欢迎语例外；见 E4。 |
| 84 | P2 | `SettingsUI.Design.HeroTitle` | 211 | 保留：已批准的地区化营销句，无需机械直译；见 E4。 |
| 85 | P2 | `SettingsUI.Design.HeroDescription` | 213 | 保留：已批准的地区化营销句；见 E4。 |
| 86 | P2 | `SettingsUI.Design.HomeRedistribution` | 217 | 在既有 GPLv3 文案上下文内澄清许可条件，不改已批准署名；见 E4。 |
| 87 | P2 | `SettingsUI.Home.Developer` | 278 | 只统一本次确认的通用品牌；现有身份称谓不凭审查报告升级为法律身份。 |
| 88 | P2 | `SettingsUI.Language.UI.Language.en-US` | 318 | 按已确认母语名称 + 语言码 + Beta，统一十一语种列表；见 E9。 |
| 89 | P2 | `SettingsUI.Language.UI.Language.zh-TW` | 320 | 按已确认母语名称 + 语言码 + Beta；见 E9。 |
| 90 | P2 | `SettingsUI.Configuration.N` | 328 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 91 | P2 | `SettingsUI.Configuration.CanvasSave.N` | 338 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 92 | P2 | `SettingsUI.Regular.StartUp.AutoStartE` | 477 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 93 | P2 | `SettingsUI.Regular.StartUp.Link.E` | 491 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 94 | P2 | `SettingsUI.Regular.Appearance.Theme.E` | 509 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 95 | P2 | `SettingsUI.Regular.Appearance.Theme.Skip4` | 513 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 96 | P2 | `SettingsUI.Regular.Appearance.TaskbarScaleBase.N` | 517 | 改善名词堆砌，保持既有三态缩放行为。 |
| 97 | P2 | `SettingsUI.Regular.Appearance.TaskbarScaleBase.E` | 518 | 区分 Base=固定48DIP与Actual=任务栏厚度；不要照抄报告可能混合两态的建议；见 E10。 |
| 98 | P2 | `SettingsUI.Regular.Behavior.TopWindow.1s` | 539 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 99 | P2 | `SettingsUI.Regular.Behavior.TopWindow.5s` | 541 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 100 | P2 | `SettingsUI.Regular.Tentative.AllowMultipleInstances.E` | 569 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 101 | P2 | `SettingsUI.Draw.Cursor.Description` | 621 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 102 | P2 | `SettingsUI.Draw.Touch.N` | 641 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 103 | P2 | `SettingsUI.Draw.Eraser.PalmEraser.Introduction` | 670 | 统一为触摸接触触发，避免英文丢条件或中文暗示必须另有手写笔落下；见 E11。 |
| 104 | P2 | `SettingsUI.Draw.Eraser.PalmEraser.PreviewDescription` | 681 | 修正英文为预览比例；比例仅页面状态，不改阈值；见 E11。 |
| 105 | P2 | `SettingsUI.Preset.StartupPlacement.E` | 724 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 106 | P2 | `SettingsUI.Preset.StartupPlacement.CenteredBottomBar` | 725 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 107 | P2 | `SettingsUI.Performance.RenderingSection` | 744 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 108 | P2 | `SettingsUI.Experimental.UI3.AllowFractionalFontSize.N` | 757 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 109 | P2 | `SettingsUI.PlugIn.N` | 806 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 110 | P2 | `SettingsUI.PlugIn.PPTHelper.VersionError` | 811 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 111 | P2 | `SettingsUI.PlugIn.PPTHelper.Tip` | 815 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 112 | P2 | `SettingsUI.PlugIn.PPTHelper.Tentative.N` | 853 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 113 | P2 | `SettingsUI.PlugIn.LnkHelper.E` | 890 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 114 | P2 | `SettingsUI.HotKey.ToggleAnnotationN` | 913 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 115 | P2 | `SettingsUI.HotKey.FreezeE` | 924 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |
| 116 | P2 | `SettingsUI.HotKey.SynchronizationPending` | 932 | 修正明确语病/术语与表达，按消费者和可用空间精炼，不改行为。 |

## Files Found / Related Specs

- `Inkeys/src/i18n/{zh-CN,en-US,zh-TW}.jsonc`：当前文案基准与译文，报告116键均应以当前文件为准。
- `Inkeys/Inkeys/UI/Bar/{Bar.Interaction,Bar.Initialization,Bar.RenderLoop,Bar.State}.cpp`：按住锁定、显示及布局实际消费者。
- `Inkeys/Inkeys/UI/Setting/{Setting,Setting.Pages}.cpp`：社区、品牌、语言、版本/错误拼接、掌擦预览等活动设置路径。
- `Inkeys/SuperTop/{IdtToken.h,IdtSuperTop.cpp}`：UIAccess token和启动链；不证明锁屏显示能力。
- `.trellis/spec/native-desktop/index.md`：活动UI3与兼容源码边界。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：i18n生成链、HeroJoin例外、产品术语、三态缩放。
- `.trellis/spec/native-desktop/palm-eraser.md`：手掌识别与临时工具/预览合同。
- `.trellis/spec/guides/index.md`：交叉审查结论必须回到源码验证。

## External References

未浏览外网；报告尾部微软链接为报告引用，本研究没有据此下法律/兼容性结论。字体来源/许可证由独立字体研究负责。

## Caveats / Not Found

- 此文件是规划核对，不是116条最终译文验收；实施仍需逐条对照实际译文、format、换行和空间预算。
- 没有运行GUI、构建或翻译脚本；无窗口测量与十一语言完整校验由实施计划安排。
- 真实PowerPoint/WPS最低版本矩阵、真实锁屏显示能力未验证；不据文案审查扩大支持范围。
- 兼容源码编译存在不等于产品路径可达。特别是FreezeRecall/hardcoded Tips需要实施前确认可达性，不能仅靠文件名认定活动UI。
