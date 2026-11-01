# 现有三语修正实施说明

本文件记录本次116项报告的最终文字处置；实际前后对照由主会话从baseline与最终JSONC生成。原因映射位于 `existing-locales-reasons.json`，键使用 **slash路径**（与I18n运行时扁平key一致），同一key的三语修改可共用原因。

## 实施边界

- 只修改zh-CN、en-US、zh-TW文案；首次sync更新快照/生成头，新增八key后不再sync，最终生成链由主会话集成。
- 原有595 key结构未迁移；追加活动界面硬编码文案对应8 key，现为603叶子。
- 保留已有品牌主页欢迎语、HeroTitle/HeroDescription、版权完整署名、HeroJoin连接符、地区社群分流及Desktop Mode/Screen centerline。
- 按实际手势解释颜色/粗细临时锁定；区分任务栏Base固定48DIP与Actual随厚度、掌擦Touch接触与预览比例。
- 通用品牌与快捷方式遵循用户确认：简中智绘教Inkeys，非简中Inkeys；不重命名内部key，不恢复旧UI。
- 英文主栏使用Whiteboard / To board / To show / End show等短译文，避免把报告建议的长句强塞固定文字槽；最终真实测量由runtime专项执行。
- 准确化SuperTop/UIAccess与权限/告警说明，保留5行段落；不改权限机制、不承诺锁屏绘制或无条件文件安全。
- CN/TW的全局/全域批注切换名称同步到说明与注册状态，避免同一快捷键混用新旧名称。

## 已执行校验

- 三语均603叶子，key集合与顺序一致，无待翻译标记。
- 逐key的占位符token与基准一致，换行数合同（含两条短标签例外）通过。真实C++ format类型验证交由主会话集成测试。
- 所有实际改动/新增key均有原因映射；原文件UTF-8无BOM/CRLF保持。
- 未运行编译或GUI；其他代理仍在翻译新增语言，故此时不以全目录check判定整任务完成。

## 报告116项最终处置（106项至少一语修正，10项保留）

| # | 级别 | 报告Key | 最终处置 |
|---:|---|---|---|
| 1 | P0 | `UI.Centre.SelectClean` | 修正（en-US）：修正把“清空”误写成Delete键提示的歧义。 |
| 2 | P0 | `UI.Operate.Freeze` | 修正（en-US）：将同一个定格操作的Hold、Lock和Freeze统一为Freeze。 |
| 3 | P0 | `UI.Bar.MainButtons.ResumeWhiteboardLabel` | 修正（en-US）：区分返回白板与返回放映，用短标签适配固定主栏文字槽。 |
| 4 | P0 | `UI.Bar.MainButtons.ReturnToShowLabel` | 修正（en-US）：区分返回白板与返回放映，用短标签适配固定主栏文字槽。 |
| 5 | P0 | `UI.Bar.MainButtons.FreezeLabel` | 修正（en-US）：将同一个定格操作的Hold、Lock和Freeze统一为Freeze。 |
| 6 | P0 | `UI.Bar.DrawAttributes.HoldThicknessLabel` | 修正（zh-CN、en-US、zh-TW）：按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 7 | P0 | `UI.Bar.ColorPicker.HoldColorLabel` | 修正（zh-CN、zh-TW）：按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 8 | P0 | `UI.Bar.CanvasRecovery.Conflict` | 修正（en-US）：修正将冲突错误译成绑定的状态文字。 |
| 9 | P0 | `UI.Bar.CanvasRecovery.Recovering` | 修正（en-US）：明确恢复中的动作状态，替换泛化的Busy。 |
| 10 | P0 | `SettingsUI.Home.Group` | 保留：源码已按zh-CN→QQ、其他语言→GitHub分流，文案与目标一致。 |
| 11 | P0 | `SettingsUI.Home.GroupE` | 保留：群号与Discussions路径为地区入口数据，不是互译关系。 |
| 12 | P0 | `SettingsUI.Feedback.CommunityDescription` | 保留：社区说明与已实装的地区入口分流一致。 |
| 13 | P0 | `SettingsUI.PlugIn.SuperTop.E` | 修正（zh-CN、en-US、zh-TW）：去掉未经代码证实的锁屏显示保证，保留已有功能描述。 |
| 14 | P0 | `SettingsUI.PlugIn.SuperTop.Warn` | 修正（zh-CN、en-US、zh-TW）：准确说明权限提升与告警，删除关闭UAC的指引和把所有告警当作误报的保证。 |
| 15 | P1 | `Widget.LnkName` | 修正（zh-CN、en-US、zh-TW）：按用户确认的通用品牌及新建快捷方式命名规则，保留本地化用途说明。 |
| 16 | P1 | `UI.Operate.Revoke` | 修正（zh-TW）：同一Undo操作统一使用台湾界面常用的復原。 |
| 17 | P1 | `UI.Bar.MainButtons.WhiteboardLabel` | 保留：真实DWrite测量Whiteboard在37 DIP短槽宽68 DIP，按报告允许的空间例外保留Board；完整窗口标题仍使用Whiteboard。 |
| 18 | P1 | `UI.Bar.MainButtons.EndPresentationLabel` | 修正（en-US）：保留清楚且紧凑的结束放映短标签，避免长译文挤占固定按钮槽。 |
| 19 | P1 | `UI.Bar.DrawAttributes.AnnotationLineLabel` | 修正（en-US）：明确为标注线工具，而非泛指所有批注。 |
| 20 | P1 | `UI.Bar.CanvasRecovery.PresentFailed` | 修正（en-US）：明确为显示阶段状态，而非查看动作。 |
| 21 | P1 | `UI.Bar.Errors.TransparentBackgroundWarning` | 修正（zh-CN、en-US、zh-TW）：提供完整可执行的故障处理路径，修正系统选项名称；保留原有分段换行数。 |
| 22 | P1 | `Dialogs.WorkspaceTransitionFailure.Whiteboard` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 23 | P1 | `Dialogs.WorkspaceTransitionFailure.Return` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 24 | P1 | `SettingsUI.Design.HeroJoin` | 保留：英文句子拼接有意需要空格，中文连接符为空。 |
| 25 | P1 | `SettingsUI.Design.HomeCopyright` | 保留：现有完整署名已在历史任务中明确批准，不机械统一双译名。 |
| 26 | P1 | `SettingsUI.Design.TopWindowSummary` | 修正（en-US）：明确周期性重新置顶，而非保持在顶层的时长。 |
| 27 | P1 | `SettingsUI.Configuration.CanvasSave.SaveTime.N` | 修正（zh-CN、en-US、zh-TW）：该设置控制历史画布保留期限，而非保存操作持续时间。 |
| 28 | P1 | `SettingsUI.Configuration.CanvasSave.SaveTime.E` | 修正（en-US）：明确按已保存画布的年龄与保留期限执行清理。 |
| 29 | P1 | `SettingsUI.Version.LimitUpdate.N` | 修正（zh-CN、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 30 | P1 | `SettingsUI.Version.Inkeys3Update.N` | 修正（zh-CN、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 31 | P1 | `SettingsUI.Version.VersionTip` | 修正（zh-CN、en-US、zh-TW）：删除重复拗口表达，保留实际修复选项及操作路径。 |
| 32 | P1 | `SettingsUI.Version.Info.ReleaseArch` | 修正（en-US）：消费者另行拼接架构值及分隔符，标签不重复添加竖线或冒号。 |
| 33 | P1 | `SettingsUI.Version.Update.ChannelTip` | 修正（zh-CN、en-US、zh-TW）：统一三通道含义，避免绝对稳定性或把安全告警笼统定性为误报。 |
| 34 | P1 | `SettingsUI.Version.Update.Arch.E` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 35 | P1 | `SettingsUI.Regular.ToolbarExpansionDirection.Centerline` | 保留：布局实现及规范明确使用屏幕中心线，Screen centerline准确。 |
| 36 | P1 | `SettingsUI.Regular.Appearance.AnimationEnableE` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 37 | P1 | `SettingsUI.Regular.Appearance.AnimationRateE` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 38 | P1 | `SettingsUI.Regular.Behavior.TopWindow.E` | 修正（zh-CN、en-US、zh-TW）：删除重复和错字，说明周期性重新置顶、落笔暂停及资源权衡。 |
| 39 | P1 | `SettingsUI.Regular.Behavior.RightClickClose` | 修正（en-US）：明确右键点击的是主图标，不是任意图标。 |
| 40 | P1 | `SettingsUI.Regular.Behavior.DrawingRetract` | 修正（en-US）：统一收起与隐藏的区别；兼容资源保留，不恢复退休设置。 |
| 41 | P1 | `SettingsUI.Regular.Behavior.ErasingRetract` | 修正（en-US）：统一收起与隐藏的区别；兼容资源保留，不恢复退休设置。 |
| 42 | P1 | `SettingsUI.Regular.Behavior.DraggingRetract` | 修正（en-US）：统一收起与隐藏的区别；兼容资源保留，不恢复退休设置。 |
| 43 | P1 | `SettingsUI.Regular.Behavior.CheckingRetract` | 修正（zh-CN、en-US、zh-TW）：补充点击区域；兼容资源保留，不恢复退休设置。 |
| 44 | P1 | `SettingsUI.Regular.Behavior.CheckingRetractE` | 修正（en-US）：使用真实工具名称，明确生效模式与区域。 |
| 45 | P1 | `SettingsUI.Regular.Tentative.SafeMode.N` | 修正（zh-CN、en-US、zh-TW）：该组选项控制崩溃后动作，并非独立安全启动模式。 |
| 46 | P1 | `SettingsUI.Draw.Haptics.N` | 修正（zh-CN、zh-TW）：明确手写笔触觉反馈，与屏幕触摸反馈区分。 |
| 47 | P1 | `SettingsUI.Draw.Cursor.DrawingStyle` | 修正（zh-CN、zh-TW）：源码对应PenIcon笔形图标；保留准确英文，明确中文含义。 |
| 48 | P1 | `SettingsUI.Draw.Effect.Device.MousePen` | 保留：Desktop Mode是已有明确批准的产品名称。 |
| 49 | P1 | `SettingsUI.Draw.Eraser.ResponseFor` | 修正（en-US）：将内部变量式名称改为适用设备的自然提示。 |
| 50 | P1 | `SettingsUI.Performance.InterfaceFrameRate.E` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 51 | P1 | `SettingsUI.Experimental.UI3.AllowFractionalFontSize.E` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 52 | P1 | `SettingsUI.Experimental.ConsoleOutput.Haptics.N` | 修正（zh-CN、en-US、zh-TW）：这是诊断输出开关，而非发起一次触觉测试的动作。 |
| 53 | P1 | `SettingsUI.PlugIn.PPTHelper.E` | 修正（zh-TW）：补全简报与画笔两类控制按钮，并统一放映、画布及控制术语。 |
| 54 | P1 | `SettingsUI.PlugIn.PPTHelper.Sync` | 修正（en-US）：去除没有解释的ADJ缩写，明确同步调整。 |
| 55 | P1 | `SettingsUI.PlugIn.PPTHelper.Error` | 修正（zh-CN、en-US、zh-TW）：消费者拼入完整Error:诊断信息而非纯数字，改为自然分隔前缀。 |
| 56 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.N` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 57 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomBoth` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 58 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.MiddleBoth` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 59 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomMiddle` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 60 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetPosition.N` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 61 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetPosition.Reset` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 62 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetPosition.Remember` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 63 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.N` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 64 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.BottomSideBoth` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 65 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.MiddleSideBoth` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 66 | P1 | `SettingsUI.PlugIn.PPTHelper.WidgetScale.State.BottomSideMiddle` | 修正（en-US、zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 67 | P1 | `SettingsUI.PlugIn.PPTHelper.Tentative.CloseWppE` | 修正（zh-CN、en-US、zh-TW）：删除未经核验的“不会影响文件”绝对保证；保留兼容文案，不恢复旧功能。 |
| 68 | P1 | `SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverOnceE` | 修正（zh-CN、en-US、zh-TW）：补足反复接管的动作对象，修复残缺句。 |
| 69 | P1 | `SettingsUI.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress` | 修正（en-US）：明确启用的是长按翻页动作，而不是另一类按钮。 |
| 70 | P1 | `SettingsUI.PlugIn.PPTHelper.SupportNote` | 修正（zh-CN、en-US、zh-TW）：澄清两类软件各自最低版本的语法，不扩大既有支持范围。 |
| 71 | P1 | `SettingsUI.PlugIn.SuperTop.Capability.SuperTop` | 修正（en-US）：删除重复Super并统一功能名称。 |
| 72 | P1 | `SettingsUI.PlugIn.SuperTop.Capability.SuperTopE1` | 修正（en-US）：当前状态是持续生效与否，不应把关闭状态描述为执行失败。 |
| 73 | P1 | `SettingsUI.PlugIn.SuperTop.Capability.SuperTopE2` | 修正（en-US）：当前状态是持续生效与否，不应把关闭状态描述为执行失败。 |
| 74 | P1 | `SettingsUI.DebugSoftware.N` | 修正（en-US）：明确该页为调试工具，避免仅用无动作对象的Debug徽记。 |
| 75 | P1 | `SettingsUI.Update.Check` | 修正（en-US）：明确检查对象是更新，避免孤立Check按钮歧义。 |
| 76 | P1 | `SettingsUI.Update.InformationDamage` | 修正（en-US）：明确损坏的是已下载更新信息，补足自然句法。 |
| 77 | P1 | `SettingsUI.Update.DownloadDamage` | 修正（en-US）：明确损坏的是已下载更新文件。 |
| 78 | P1 | `SettingsUI.Update.Newer` | 修正（zh-CN、en-US、zh-TW）：自然表达当前版本比官方最新发布版本更新的状态。 |
| 79 | P2 | `UI.Pen.Highlighter` | 修正（en-US）：使用规范工具名称，替换难懂缩写；不恢复旧UI路径。 |
| 80 | P2 | `UI.Operate.Recall` | 修正（zh-TW）：恢复操作统一使用復原，避免回復的用字歧义；不改变兼容恢复行为。 |
| 81 | P2 | `UI.Bar.Diagnostics.FrameRateFormat` | 修正（en-US、zh-TW）：统一帧率与无限制帧率指标术语，采用紧凑文案；保留真实format参数。 |
| 82 | P2 | `UI.Bar.Diagnostics.FrameRateUnavailable` | 修正（en-US、zh-TW）：无数值状态与有数值诊断使用相同指标术语和紧凑布局。 |
| 83 | P2 | `SettingsUI.N` | 修正（en-US、zh-TW）：按用户确认的通用品牌规则统一非简中产品名；主页欢迎语例外。 |
| 84 | P2 | `SettingsUI.Design.HeroTitle` | 保留：历史已批准的地区化营销句。 |
| 85 | P2 | `SettingsUI.Design.HeroDescription` | 保留：历史已批准的地区化营销句。 |
| 86 | P2 | `SettingsUI.Design.HomeRedistribution` | 修正（zh-CN、en-US、zh-TW）：在既有开源许可上下文中明确分发条件，不改版权署名。 |
| 87 | P2 | `SettingsUI.Home.Developer` | 修正（en-US、zh-TW）：按已确认规则统一通用品牌，保留现有开发者身份称谓。 |
| 88 | P2 | `SettingsUI.Language.UI.Language.en-US` | 修正（zh-CN、en-US、zh-TW）：按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 89 | P2 | `SettingsUI.Language.UI.Language.zh-TW` | 修正（zh-CN、en-US、zh-TW）：按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 90 | P2 | `SettingsUI.Configuration.N` | 修正（zh-CN、en-US、zh-TW）：栏目同时包含配置清理和画布保存，明确并列关系。 |
| 91 | P2 | `SettingsUI.Configuration.CanvasSave.N` | 修正（en-US、zh-TW）：统一台湾地区的儲存用语。 |
| 92 | P2 | `SettingsUI.Regular.StartUp.AutoStartE` | 修正（zh-CN、en-US、zh-TW）：启动项由当前用户登录触发，并按已确认规则统一品牌。 |
| 93 | P2 | `SettingsUI.Regular.StartUp.Link.E` | 修正（zh-CN、en-US、zh-TW）：补足操作对象与配置入口，修正文句连接。 |
| 94 | P2 | `SettingsUI.Regular.Appearance.Theme.E` | 修正（zh-CN、en-US、zh-TW）：消除循环定义，说明推荐皮肤的实际效果。 |
| 95 | P2 | `SettingsUI.Regular.Appearance.Theme.Skip4` | 修正（en-US）：补足英语固定表达所需冠词。 |
| 96 | P2 | `SettingsUI.Regular.Appearance.TaskbarScaleBase.N` | 修正（en-US）：用自然英语表述任务栏缩放设置，替换机械名词堆砌。 |
| 97 | P2 | `SettingsUI.Regular.Appearance.TaskbarScaleBase.E` | 修正（zh-CN、en-US、zh-TW）：区分现有Base固定48DIP与Actual随任务栏厚度的两种模式，避免混淆。 |
| 98 | P2 | `SettingsUI.Regular.Behavior.TopWindow.1s` | 修正（en-US）：用自然英语表达相对间隔级别。 |
| 99 | P2 | `SettingsUI.Regular.Behavior.TopWindow.5s` | 修正（en-US）：用自然英语表达相对间隔级别。 |
| 100 | P2 | `SettingsUI.Regular.Tentative.AllowMultipleInstances.E` | 修正（zh-TW）：统一執行個體术语，明确只阻止新实例而不关闭既有实例。 |
| 101 | P2 | `SettingsUI.Draw.Cursor.Description` | 修正（en-US）：说明高亮的是相应设备的光标边缘，避免被理解为设备自身边缘。 |
| 102 | P2 | `SettingsUI.Draw.Touch.N` | 修正（zh-TW）：统一台湾地区既有触控术语。 |
| 103 | P2 | `SettingsUI.Draw.Eraser.PalmEraser.Introduction` | 修正（zh-CN、en-US、zh-TW）：按实际Touch面积识别机制表述，避免暗示另需物理手写笔先落下。 |
| 104 | P2 | `SettingsUI.Draw.Eraser.PalmEraser.PreviewDescription` | 修正（en-US）：比例只改变页面预览，不改变实际阈值或橡皮形状。 |
| 105 | P2 | `SettingsUI.Preset.StartupPlacement.E` | 修正（zh-TW）：统一主欄、底欄产品名称及浮动窗口描述。 |
| 106 | P2 | `SettingsUI.Preset.StartupPlacement.CenteredBottomBar` | 修正（zh-TW）：统一置中底欄产品名称。 |
| 107 | P2 | `SettingsUI.Performance.RenderingSection` | 修正（zh-TW）：渲染技术设置与绘图操作概念区分，统一技术术语。 |
| 108 | P2 | `SettingsUI.Experimental.UI3.AllowFractionalFontSize.N` | 修正（zh-TW）：渲染技术设置与绘图操作概念区分，统一技术术语。 |
| 109 | P2 | `SettingsUI.PlugIn.N` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 110 | P2 | `SettingsUI.PlugIn.PPTHelper.VersionError` | 修正（zh-TW）：修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 111 | P2 | `SettingsUI.PlugIn.PPTHelper.Tip` | 修正（zh-CN、en-US、zh-TW）：统一品牌并明确PPT联动的权限匹配条件。 |
| 112 | P2 | `SettingsUI.PlugIn.PPTHelper.Tentative.N` | 修正（zh-TW）：与其他设置页面的实验选项名称保持一致。 |
| 113 | P2 | `SettingsUI.PlugIn.LnkHelper.E` | 修正（en-US、zh-TW）：按用户确认的通用品牌规则统一非简中名称，并精炼快捷方式保障说明。 |
| 114 | P2 | `SettingsUI.HotKey.ToggleAnnotationN` | 修正（zh-CN、zh-TW）：明确该快捷键切换批注状态。 |
| 115 | P2 | `SettingsUI.HotKey.FreezeE` | 修正（en-US）：自然描述切换屏幕定格状态。 |
| 116 | P2 | `SettingsUI.HotKey.SynchronizationPending` | 修正（zh-CN、en-US、zh-TW）：明确“普通权限”指未提升权限窗口。 |

## 后续集成注意

最终sync前，保存全部已审译目标值；zh-CN新增8 key与HotKey说明/状态名称更新会触发正常待翻译标记。按本次完成的译文恢复对应值后再check，不把marker误当最终译文。快照/生成头此时仍是首次595结构，主会话负责最终603结构同步。
