# Research: Published settings source audit

- Query: 对照 chore/publish@e2c212d4 与 fluent@834307a4 的设置内容，明确全部页面的文字、按钮、显示条件、配置、保存时机和即时副作用。
- Scope: internal
- Date: 2026-10-08

## Findings

### Source boundary and files found

本研究只读取父会话导出的固定源码；未执行任何 Git 操作。发布来源固定为 `e2c212d4d9cd68367c08363c450d392e9d63bcef`，Fluent 来源为 `834307a4`。下文 `P/` 指 `Inkeys/Cache/SettingBaseline/publish/`，`F/` 指 `Inkeys/Cache/SettingBaseline/fluent/`，不是当前正在变化的整合工作树。原始字节 SHA256 保存在 `content-source-summary.json`。

| File | Description |
| --- | --- |
| `P/Inkeys/Inkeys/UI/Setting/Setting.cpp` | 发布设置内容、业务 FIFO、侧栏、全局更新状态与所有插件详情的事实来源。 |
| `F/Inkeys/Inkeys/UI/Setting/Setting.cpp` | Fluent 旧全页内容与保存/同步合同；不能由整份发布文件覆盖。 |
| `F/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp` | Fluent 主页和常规页提取的渲染；单搜 Setting.cpp 会遗漏它们。 |
| `F/Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp` | 原 Fluent 导航分组/选中/展开框架；侧栏子任务接管。 |
| `P/F Inkeys/IdtConfiguration.cpp/.h` | 传统 deploy.json 的读取、捕获、废弃键清理与历史兼容结构体。 |
| `P/F Inkeys/Inkeys/Other/Other.Config.cpp/.cppm` | 新 main.json schema 与默认值；两个配置系统仍并存。 |
| `P/F Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc` | 简中、繁中、英文可见文案来源；字段值差异见独立 JSON。 |
| 当前 `.trellis/spec/native-desktop/` | 新发布配置、橡皮和运行时合同；详细条目见本文件后半部分。 |

### View inventory and measurement caveat

发布代码有 13 个顶层 case（含插件页），插件内部有 overview 和 4 个详情 case。实际可见视图为 13 个主页面（其中包含插件概览）加 4 个详情，即 17 个；Fluent 多一个“预设”视图，共 18 个。词法提取的 publish=18/fluent=19 case 总数重复计算了外层插件 case 和内层 overview，不能当作实际页面数或完整性证明。

`content-source-summary.json` 排除 C++ 注释和 `#if 0` 的正文，但保留其他条件编译分支。它记录每页 i18n key、配置引用和业务调用，可用于定位；相同引用数量不证明按钮、条件或副作用一致。

### Per-page content and behavior mapping

| 页面 / source line | Published active content | Configuration / save / immediate action | Sync requirements vs old Fluent |
| --- | --- | --- | --- |
| 主页 `Setting.cpp:1612` | Home.Prompt、产品图片、网站/GitHub、AlanCRL、Home.Developer、QQ 2685549821、邮箱 alan-crl@foxmail.com、Home.Group/GroupE、BilibiliChannel、FeedBack 与反馈二维码 | 无配置；网站按 zh-CN 分 www/en，群入口 zh-CN 打开 qm.qq.com 邀请，其他语言 GitHub discussions；Bilibili、问卷外链。 | Fluent 可保留自适应布局与框架插画，但需恢复 QQ/邮箱可见内容、群文案与目标；旧 HomeAction::Community 在 zh-CN 指向 community.html，与群邀请不同。反馈二维码使用 TextureSettingSign[7]，source:1815。删除 Preset 卡片；Fluent DrawDescription 不得仍提到已删除智能绘图。 |
| 语言 `:1832` | 下拉 en-US、zh-CN、zh-TW；Select/SelectE 和 Warn | `setlist.selectLanguage` 改变才 WriteSetting；立即 I18n::load 对应资源，再 QueueConfirmRestart（source:1907）。 | 保留 Fluent ComboRow；同步三语言新版 Warn。 |
| 配置保存 `:1956` | 配置清理、桌面画布自动保存；保留时长 UI 被 `#if 0` 禁用 `:2102`，不可迁回可见 UI | AutoClean 写新 config + QueueConfigWrite + WriteSetting；`Save.Enable` 修改需 `SyncDraw3State()` 再 WriteSetting（source:2087）。 | 保留 Save.Enable；同步新自动保存文案；删除可见 retention combo，但不擅删仍有消费者的 saveDays 配置。 |
| 软件版本 `:2191` | 手动更新提示、架构不一致提示、版本/日期/架构/Debug-Release/手动或CI构建信息、复制UserID、修复、修复改架构、自动更新、渠道LTS/Insider/Canary、架构64/32/ARM64 | 复制使用 Clipboard API；修复设置 mandatoryUpdate 并启动/复用更新；切换更新配置通常立即 WriteSetting，已下载待重启时通过清理安装器的 FIFO 命令后变 UpdateObtainInformation。 | 核对同名 key 的所有操作；全局状态栏内容移到版本页（见下），不能只保留 UpdateNew 提示。 |
| CI/CD `:2869` | 返回、运行/仓库链接、分支、提交者、构建时间、OS/镜像/工具版本 | ShellExecuteW(FIFO) 打开真实 URL；只读信息无写盘。 | Fluent 路由和可换行排版保留；无新增业务字段。 |
| 常规 `:3023` | 启动总开关；创建快捷方式/更多；启动加载动画；外观顺序：动画、动画速率（仅动画开启）、边缘光影、动态边缘光影（仅光影开启）、Bar缩放、Setting缩放；置顶间隔、右键关闭、安全模式 | Startup SetStartupState + WriteSetting。加载动画写 StartupPreview.Enable，经过 Preference.SetConfigured/ConsumeWritePending，source:3163。动画/光影写 config、即时 SetAnimationOptions/SetEdgeLightingOptions、QueueConfigWrite。动画速率0.1–5.0、按0.1取整、实时同步但松手写盘；Bar Zoom0.5–2.0、两位小数、实时 SetConfigZoom，松手写盘。Setting Zoom1–2、两位小数，松手保存。置顶索引0..6对应100ms/500ms/1s/3s/5s/10s/30s。安全模式四项。 | 删除 AvoidFullscreen 与兼容详情。不能再在实验页重复动画/动态光影。隐藏子控件不覆盖已保存值；当页布局高度随条件变化。Source SettingUIScale.E 的重启提示与 Fluent 已有即时缩放合同有语义冲突，需协调者明确记录例外。 |
| 绘制 `:3731` | 绘图设备 Touch/MousePen；橡皮总自动粗细；五入口 MouseLeft/MouseRight/Touch/PenTip/PenTail 的 Fixed/Speed；仅 Pen入口且Speed时显示笔速响应；触摸接触面积辅助 | PaintDevice 修改 SyncDraw3State + WriteSetting（:3802）。整页取一份 EraserPreferencesSnapshot。总开关 SetGlobalEraserPreference(-1,-1,bool)，由 helper 发布/排队保存；入口 SetEraserInputPreference + QueueConfigWrite。PenResponse 在不支持自动检测时不显示 enum0，但 enum1/2稳定保存（:3916）。TouchArea 从 ProductHost options读/写，同步 config并QueueConfigWrite（:3951）。 | 删除 AI draw、旧橡皮mode/size、抬笔平滑、隐藏触控光标、性能、预设。不能用总开关统一五入口 kind；关闭总开关时仍可编辑入口配置。 |
| 插件概览 `:3989` | PPTHelper（版本/错误）、SuperTop 20260202a、DDB版本、LnkHelper 20250223a，进入4详情 | 只改子页路由，无配置。 | 保留四个详情，不遗漏来源/版本信息。 |
| PPT详情 `:4250` | 错误/组件教程/管理员教程；显示加载页；BottomBoth/MiddleBoth；重置位置/记忆位置；两组0.5–3.0 scale、Reset、Sync；自动接管/一次接管/展开接管/长按翻页 | 设置值分别走 PptComWriteSetting 或新 config QueueConfigWrite；显示开关 Notify BottomPair/MiddlePair。Reset source:4614使用 `Ppt::ResetPositions()`，再PptComWriteSetting。Scale按0.01取整，两组同步；source:4715/4830在拖动变化时立即更新pptComSetlist并Notify对应组，两个Sync开启时即时同步另一组；Reset即刻设1.0，重新启用Sync时立即取另一组保存值并PptComWriteSetting；source:4892只在控件释放后更新Record并额外写盘/Notify。AutoTakeOver/Once/Expand/LongPress属于新 main.json。 | 删除 FixedHandWriting；不显示 BottomMiddle/ExitShow及第三scale；不清零旧退出控件的position字段代替ResetPositions。AutoKillWpsProcess/CloseWpp整个块注释禁用（:4924），保持隐藏。Scale拖动即时应用、松手保存应保持发布行为；不能仅凭source:4892末尾写盘判断误推应用时机。 |
| SuperTop `:5187` | Warn、Enable（依据hasSuperTop选不同说明）、Indicator | `setlist.plugInSetting.superTop.*` 改变才WriteSetting。 | 不自动重启进程或改变原权限处理；保留条件说明。 |
| LnkHelper `:5400` | FixLnk、CreateLnk | 旧setlist字段 + WriteSetting；开FixLnk立即SetShortcut；改CreateLnk仅CorrectLnk开启时SetShortcut。 | 保存与执行条件同时保留，不能把CreateLnk总是调用SetShortcut。 |
| DDB `:5596` | GitHub/GPL及风险说明；启用；管理员启动；间隔500/1000/3000/5000/10000；17个准确拦截目标 | Enable通过ConfigureDdb FIFO做hash/plugin启动/卸载及持久化；Admin先WriteSetting再RestartDdb；间隔/拦截先WriteSetting再QueueDdbWriteInteraction(true,false)。 | 拦截清单为 Seewo3/5/5C、Pinco侧栏/画笔、PPT、IwbAssistant、Yiou、AiClass、ClassInX、IntelligentClass、ChangYan4/5、Iclass30侧栏/画笔、SeewoDesktop侧栏/画笔。保留有管理员/恢复/包含PPT控件的具体说明，不整块恢复旧布局。 |
| 组件 `:6545` | 16个快捷按钮开关，分应用、系统、键盘、随机点名、联动 | 每项旧bool变化后WriteSetting，再 `SyncUi3BuiltInComponents()`（source:6594等），即时更新主栏。 | 固定源顺序：Explorer/Taskmgr/Control；Desktop/Lock；Esc/AltF4；IslandCaller1/2、SecRandom1/2/2Compat、NamePicker；ClassIslandSettings/Profile/Classswap。只保存不刷新UI3是遗漏。 |
| 快捷键 `:7285` | HotKey.N/HotKey.E只读说明 | 无配置，无完整编辑器。 | 保持当前范围；不新增快捷键功能。 |
| 实验室 `:7375` | ConsoleOutput.TouchArea/PptCOM/Draw3/Cursor；在支持设备启动DComp；DirtyDebug；仅DebugMode开时ShowFrameRate | 四个ConsoleOutput开关写main.json、QueueConfigWrite，按说明下次启动应用；TouchArea名称已经扩展为输入/橡皮综合诊断但键保持原样。DComp config默认true，重启生效，false使用ULW。Debug.Enable和ShowFrameRate分别保存，先SetDebugOptions后QueueConfigWrite。 | 移除迁到常规的Animation/Speed/DynamicEdge项；隐藏ShowFrameRate时不得写false。source实验页标题仍硬编码“实验室”，不要误判为三语言全部完成。 |
| 赞助 `:7612` | 图片[9]；“成功赞助后，可以联系作者将您的昵称和赞助的金额添加到社区名片中以表示感谢。”（:7625） | 无配置，无新增支付操作。 | 同步完整原句；自适应图片/文字排版保留。 |
| 调试 `:7680` | 显示器与EDID：拓扑、monitor数、名称、Windows标识、像素、DPI、物理尺寸/不可用原因、EDID raw尺寸。DRAW3_RTS_DIAGNOSTICS条件：临时橡皮模型、标尺、手动宽高、Pen beta、TouchArea、诊断及状态。 | 每帧Display::GetSnapshot。临时模型/标尺/beta/diagnostics仅写ProductHost options；只有TouchArea共享持久化字段并QueueConfigWrite。变化在下一独立接触/批次应用。 | 完整替换旧TouchTest按钮、TouchList/Recall/PPT状态文本，source已无这些入口；保留编译条件，不把临时参数误存进正式config。 |

### Global update status is content, not a layout requirement

Source `Setting.cpp:7858` 之后有独立状态栏，它不属于调试页。应移植到版本页的 Fluent Notice/rows，避免回流绝对坐标底栏。

- 12状态：NotStarted、ObtainInformation、InformationFail、InformationDamage、InformationUnStandardized、Downloading、DownloadFail、DownloadDamage、Restart、Latest、Newer、New；架构/手动更新等提示不属于额外状态。
- NotStarted 提供Repair，使用 Dialogs.UpdateModuleUnavailable.Body 的 QueueInformation；另有ManualDownload外链。获取/失败/损坏等状态保留ManualDownload；下载状态显示downloadLine；Restart只说明状态，不自行重启。
- Latest/Newer附LTS/Insider/Dev/Canary/Other渠道说明。
- New提供UpdateNow，将mandatoryUpdate=true并设UpdateObtainInformation（:8305）。
- 始终有Check按钮（:8323）；NotStarted时信息提示，否则设UpdateObtainInformation。

### Exact i18n delta

`content-i18n-delta.json`列出了三个语言的新增、删除和已存在值的变更；每语言43新增、76删除、4值改变（限定SettingsUI/Dialogs前缀）。删除集包含34个Fluent专有Design键，不能整集删除；需保留仍被框架使用的键，移除废弃条目相关的键。

三语言4个变更都是：CanvasSave.Enable、CanvasSave.EnableE、Language.UI.Warn、Regular.Appearance.SettingUIScale.E。前两项明确改为“桌面画布自动保存”，说明清空/退出时保存非空画布。新增控件文案在Draw.Eraser、ConsoleOutput、Regular.Animation/Edge/Startup加载动画；不能用旧硬编码中文说明代替已有翻译键。

### Configuration ownership, defaults and retired keys

- deploy.json：语言、Startup、SettingGlobalScale、置顶间隔、右键关闭、SafeMode、PaintDevice、Save.Enable、组件、SuperTop、LnkHelper及PPT传统设置。Fluent额外的SettingDarkMode保留在此，默认false、缺项/错误类型回退false。
- main.json：AutoClean；UI.Bar.Zoom；Drawing.Eraser；Experimental.Inkeys3.*；PPT新的AutoTakeOver等。
- Source `Other.Config.cppm:276`：Eraser.Automatic=true、BaseDiameterDip=32、Sensitivity=1；五入口kind和两个PenResponse初始-1，由运行时归一化。TouchContactAreaAssistance=false、EnableDirectComposition=true；ConsoleOutput四项false；Debug.Enable=false/ShowFrameRate=true；EdgeLighting.Enable=true/Dynamic=false；Animation.Enable=true/SpeedRate=1.0；PPT AutoTakeOver=false/Once=true/Expand=true/LongPress=false。
- Source `IdtConfiguration.cpp:569` 无条件清理 Regular.AvoidFullScreen、PointAdsorption、SmoothWriting、HideTouchPointerBeta、Performance、Preset，即便AutoClean=false。Experimental.Inkeys3.UI3旧路由键清理source:664。PPT FixedHandWriting退出活动读取/捕获合同。结构体保留部分历史字段不能证明UI仍应存在。
- Save.SaveDays仍读取/写回，尽管其UI隐藏；不能以隐藏UI为理由删除其配置。Preset迁移仅保存迁移证据，启动恢复被禁用，不能恢复旧默认宽度影响当前DIP笔宽。

### Code patterns and related specs

- Rendering vs business：Shared RenderPipeline持有D3D/ImGui；配置捕获先冻结快照，再投到Setting FIFO，ShellExecute/COM/模态提示也走同一队列。不要因采用发布内容恢复渲染线程阻塞操作。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:37`：废弃配置无条件清理，Save.Enable保留；`:91`：Debug.ShowFrameRate子项隐藏不覆盖偏好；`:268`：SettingDarkMode；`:272`：JSONC为源、Header由sync生成。
- `.trellis/spec/native-desktop/eraser-attributes.md:18`：橡皮完整快照、独立自动bool、五入口kind不改；写盘不打开设置窗口。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：resident设置会话、FIFO、唯一render线程、epoch恢复。

### External references

没有外部网页依赖。精确版本来源是父会话导出的固定仓库对象e2c212d4与834307a4；本审计不基于当前远程分支或可变化的dev。

## Caveats / Not Found

- 本文件是来源合同与遗漏定位，不是本次构建/行为验收结论。实现工作树并发变化，最终需在设置工作完成后重新核对。
- 词法工具不是完整C++预处理器，保留除#if0之外的条件分支；Debug diagnostics只在相应宏生效时存在。
- SettingUIScale说明存在发布/Fluent框架差异，保留Fluent松手即时应用文案。2026-10-08独立Checker和实现代理复核source:4660–4920后更正PPT scale早期结论：发布同样拖动实时应用、松手写盘；此前只看末尾保存块的判断不完整，协调者已撤销deferred指令。
- trellis-start SKILL.md在本环境缺失，父会话确认按workflow和task CLI执行；研究直接读workflow、PRD/design/implement及相关spec，不读implement.jsonl/check.jsonl。
- 本研究未启动GUI、编译、同步i18n、修改代码/spec/项目、操作Git/index、commit或push。只写活动任务research目录。