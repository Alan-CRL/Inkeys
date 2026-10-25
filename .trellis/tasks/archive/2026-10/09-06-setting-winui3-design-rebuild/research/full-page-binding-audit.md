# Research: 全页迁移绑定与视觉覆盖审计

- Query: 首批之外全部设置页、PPT / SuperTop / Shortcut / DDB 细页的控件、状态、保存、副作用与安全视觉导航路线。
- Scope: internal；以主会话导出的 `0ffd0341` 原始快照为准，不重复首批导航/Home/General清单。
- Date: 2026-09-06

## Findings

### 文件、证据与范围

本文件中的 `Setting.cpp:N` **一律锚定 `0ffd0341` 的 `Inkeys/Inkeys/UI/Setting/Setting.cpp`**。研究读取 `research/full-baseline/Setting.cpp`，不是并发修改后的工作区；快照由主会话通过 git 导出，研究代理未执行 git。`Setting.Wrap.h / Setting.Widgets.cpp / Setting.Pages.cpp / Setting.Controls.cpp` 同目录快照用于确认包装边界；产品 `IdtConfiguration.cpp`、`src/i18n/zh-CN.jsonc`、`Net.Update.cppm` 仅作字段/文案补证。

| 原页面 | 原逻辑区间 | 需保留的真实内容（含条件分支） |
| --- | --- | --- |
| Language | 2252–2281 | 1 combo |
| Configuration | 2283–2343 | 2 toggle + 1 combo |
| Version | 2345–2591 | 版本信息卡、6设置卡、UpdateNew/架构警告、渠道说明、条件构建详情入口 |
| CI/CD | 2594–2633 | 返回、2外链、完整构建字段长文 |
| Draw | 2730–2879 | 9卡：6 toggle + 2 combo + 1 slider |
| Preset | 2881–2973 | 3 toggle；手动模式下另2 slider |
| Plugin overview | 3018–3043 | 4插件卡/4内部导航按钮，动态版本或错误状态 |
| PPT | 3045–3317 | 10 toggle + 重置位置；3个缩放卡各有slider/reset/sync，共14业务卡、20个业务输入；错误/提示/管理员InfoBar |
| SuperTop | 3319–3348 | 长Warning + 2 toggle；hasSuperTop条件文案 |
| Shortcut | 3350–3378 | 2 toggle，带实际快捷方式副作用 |
| DDB | 3380–3532 | 版本/GitHub/Warning，启用+admin+interval+17项精确控制（20输入） |
| Components | 3537–3631 | 16 toggle，5分组，ClassIsland/协议说明 |
| Hotkey | 3632–3642 | 多行只读说明，无快捷键编辑器 |
| Experimental | 3643–3749 | 最多4 toggle + 1 slider，其中dynamic/FPS条件显示 |
| Sponsor | 3750–3773 | 原赞助图片+说明 |
| Debug | 3774–3866 | 触摸测试动作、实时多行状态、内部滚动 |

以上统计结合源码模式检索与实际 helper 调用/数组展开判断。不能直接以 BeginSettingsCard 或 Slider 字符串出现次数判定控件数量（PPT helper 重复3次、DDB数组17次、组件helper16次）。`tabPerformance` 只有enum声明，无case、无实际页面，不创建新“性能页”。

### 共用保存与异步边界

- `Setting.cpp:1443–1449` 的局部宏把 `WriteSetting/PptComWriteSetting/ShellExecuteW/SetStartupState/RestartProgram/CloseProgram` 改为 Queue helper。留在该宏域中业务仍走原FIFO；提取到其他文件必须显式传动作，不能照抄宏外函数名而恢复渲染线程I/O。
- 旧配置 `WriteSetting()` 冻结 CaptureSettingJson；PPT `PptComWriteSetting()` 冻结 CapturePptComSettingJson；`QueueConfigWrite()` 冻结 Inkeys::Config副本 (`769–818`)。仅在实际变化/动作触发时排队；重排、滚动、导航不应产生配置写入。
- 更新字段访问使用 `setlistUpdateMutex` (`1450–1478`)。`EnableAutoUpdate` 每帧从 GetEnableAutoUpdate同步 (`2035`)；不要把它改为只初始化一次的UI副本。
- 大部分普通draft驻留在协程 (`1862–2011`)，PPT三组scale及save records也是驻留值。重新渲染卡片不能每帧重置draft而打断输入。
- 页面内部基线没有显式 BeginDisabled。唯一通用禁用是 `navigationState.overlayOpen` 禁用整个内容区 (`2178`)；下面的条件显示不得擅自变成新的功能禁用规则。

### 语言与配置

| 控件/显示 | 字段与取值 | 实际写入/副作用 | 行号 |
| --- | --- | --- | --- |
| `language-ui / language-select` | UI SelectLanguage → `setlist.selectLanguage`，0=en-US、1=zh-CN、2=zh-TW | combo真变化且旧配置不同：改字段 → WriteSetting → I18n::load对应语言 → **使用新语言文字** QueueConfirmRestart(Language.UI.Warn)。不只是下次启动才改变本页文字 | 2257–2276 |
| `configuration-clean` | ConfigurationSetting.Enable → `Inkeys::config.Config.AutoClean` | 改字段 → QueueConfigWrite → WriteSetting。后者CaptureSettingJson在AutoClean为真时clear旧setlistVal，因此双写不是冗余，可删去旧配置项 | 2288–2298；IdtConfiguration.cpp:408 |
| `configuration-history` | SaveSetting.Enable → `setlist.saveSetting.enable` | 改字段 → WriteSetting | 2305–2314 |
| `configuration-retention` | SaveSetting.SaveDays → `setlist.saveSetting.saveDays`；**保存索引0..5**，UI1/3/5/10/30天/永不 | combo真变化 → WriteSetting；不改成实际天数。history关闭时该行仍显示且可用 | 2319–2338 |

需保留长文：AutoClean说明包含降级/升级/导入导致配置项丢失风险；CanvasSave说明ScreenShot路径及超级恢复；语言切换的重启确认必须保留，视觉脚本不得选新语言来测试。

### 版本与构建详情

| 控件/显示 | 字段/条件及行为 | 行号 |
| --- | --- | --- |
| 手动更新Warning+action | 仅 `AutomaticUpdateState==UpdateNew` 出现；action置 `mandatoryUpdate=true`、state=UpdateObtainInformation | 2349–2357 |
| 架构Warning | `inconsistentArchitecture` 时显示VersionTip，不等于控件被禁用 | 2359–2363 |
| `version-info` | TextureSettingSign[1]保纵横比；editionVersion/date、buildTime、programArchitecture/targetArchitecture、IDT_RELEASE/DebugTag | 2367–2396 |
| 手动/自动构建标签 | `settingCICD.url.empty()`→ManualBuild，否则AutoBuild并显示CICDInfo内部入口→tabCICD | 2397–2412 |
| `version-user-id` / Copy | description含真实userId；点击OpenClipboard→EmptyClipboard→分配Unicode内存→CF_UNICODETEXT→CloseClipboard，有剪贴板副作用 | 2419–2445 |
| `version-repair` | 若EnableFixWithChangeArchitecture则根据targetArchitecture写win64/arm64/否则win32并WriteSetting；mandatoryUpdate=true；若UpdateNotStarted则QueueAutomaticUpdate，否则state=UpdateObtainInformation | 2451–2468 |
| `version-repair-architecture` | **仅驻留布尔EnableFixWithChangeArchitecture，默认true，无配置键/即时写盘** | 2472–2476；1867 |
| `version-auto-update` | UI EnableAutoUpdate→setlist.enableAutoUpdate；真变化才动作，具体状态分支见下 | 2481–2505 |
| 渠道InfoBar | Update.ChannelTip，一直显示，无action | 2509–2511 |
| `version-update-channel` | 当前setlist.UpdateChannel映射LTS/Insider/Canary，保存原字符串；combo变化才动作，具体状态分支见下 | 2512–2545 |
| `version-update-architecture` | 当前setlist.updateArchitecture映射索引0=win64、1=win32、2=arm64，保存原字符串；带Arch.E说明 | 2550–2585 |
| CICD返回 | back→tab6，仅内部导航 | 2596–2598 |
| `cicd-details` | url/repoUrl分别Hyperlink→FIFO Shell；branch、submitter、buildTime、buildOS、buildOSVersion、buildRunnerImageOS、buildRunnerImageVersion、msBuildVersion均保留长文本 | 2603–2629 |

更新状态不能简化成“改字段+写配置”：

- 关闭AutoUpdate且state=UpdateRestart：QueueBusiness(ClearInstallerAndSetAutoUpdate, flag)，随后state=UpdateObtainInformation；其余情况SetEnableAutoUpdate→WriteSetting；若打开且state=UpdateNew再切ObtainInformation。
- 更改Channel/Architecture且state=UpdateRestart：对应ClearInstallerAndSetChannel/Architecture(payload)，随后state=UpdateObtainInformation；其余分支setter→WriteSetting，仅当state!=UpdateNotStarted才切ObtainInformation。
- 三个ClearInstallerAndSet*在生产者锁内更新字段并冻结JSON (`777–789`)，worker实际删除并重建`globalPath + installer`后写JSON (`683–692`)。**这些是有实际磁盘/下载行为的控件，不能为了截图切换。**
- QueueAutomaticUpdate只启动既有长期网络线程，不能把下载循环放到FIFO主体 (`753–755`)。
- 基线此page只有UpdateNew的条件提示，没有独立下载进度卡或通用“检查更新”按钮。不要把不存在的old UI声称已保留；如果新增状态展示，必须仍依据真实AutomaticUpdateState。

### 绘制

全部9行没有额外条件禁用；普通toggle仅字段真变化才WriteSetting。

| ID / 控件 | UI draft → 原配置 | 额外行为/取值 | 行号 |
| --- | --- | --- | --- |
| draw-device / combo | PaintDevice→setlist.paintDevice | 0触摸、1鼠标/笔；写后 `drawingScale=GetDrawingScale()`、`stopTimingError=GetStopTimingError()` | 2735–2751 |
| draw-lift-straighten / toggle | LiftStraighten→setlist.liftStraighten | 保留教学一体机建议说明 | 2757–2766 |
| draw-wait-straighten / toggle | WaitStraighten→setlist.waitStraighten | 保留按住1秒的操作说明 | 2770–2779 |
| draw-endpoint / toggle | PointAdsorption→setlist.pointAdsorption | 保留直线/矩形端点抬笔吸附说明 | 2783–2792 |
| draw-smooth / toggle | SmoothWriting→setlist.smoothWriting | 无说明的单行卡仍须完整命中 | 2798–2806 |
| draw-eraser-mode / combo | EraserMode→setlist.eraserSetting.eraserMode | **0/1/2对应i18n Mode3/Mode2/Mode1**，不能因重排序改变序号 | 2813–2829 |
| draw-prepare / sliderInt | PreparationQuantity→setlist.performanceSetting.preparationQuantity | 0..20；仅非active且变更才WriteSetting→**ResetPrepareCanvas()** | 2836–2847 |
| draw-super / toggle | SuperDraw→setlist.performanceSetting.superDraw | WriteSetting，保留性能说明 | 2851–2859 |
| draw-hide-pointer / toggle | HideTouchPointer→setlist.hideTouchPointer | WriteSetting；保留仅触控白点/鼠标笔信号恢复光标的说明 | 2865–2874 |

### 预设

| ID | UI→配置 | 写入/条件 | 行号 |
| --- | --- | --- | --- |
| preset-memory-width | PresetSetting.MemoryWidth→setlist.presetSetting.memoryWidth | toggle变化→WriteSetting | 2885–2894 |
| preset-memory-color | PresetSetting.MemoryColor→setlist.presetSetting.memoryColor | toggle变化→WriteSetting | 2898–2907 |
| preset-adaptive | PresetSetting.AutoDefaultWidth→setlist.presetSetting.autoDefaultWidth | toggle变化→WriteSetting。description把 **stateMode.Pen.Brush1.widthPreset / Highlighter1.widthPreset** 插入AutoThicknessE，不是手工宽度字段 | 2913–2928 |
| preset-pen-width | PresetSetting.DefaultBrush1Width→setlist.presetSetting.defaultBrush1Width | 仅!AutoDefaultWidth显示；1..30，round整数；非active且配置不同才WriteSetting | 2932–2948 |
| preset-highlighter-width | PresetSetting.DefaultHighlighter1Width→setlist.presetSetting.defaultHighlighter1Width | 同一条件；10..100、round整数、非active保存 | 2952–2967 |

隐藏两条手动slider不得重置其值，不将用户当前画笔粗细回写为预设。不为视觉截图临时toggle自适应。

### 插件总览与通用细页导航

总览4卡均只有内部“插件选项”按钮，没有启用开关 (`2991–3004`)：

| 原id | title / 状态 | 子路由 | 行号 |
| --- | --- | --- | --- |
| ppt-helper | PPTHelper.N；pptComVersion若以`Error: `起始则显示VersionError，否则原版本 | tabPlug2 | 3022–3028 |
| super-top | SuperTop.N；20260202a | tabSuperTop | 3029–3032 |
| ddb | DesktopDrawpadBlocker.N；ddbInteractionSetList.DdbEdition | tabPlugDDB | 3033–3037 |
| shortcut-helper | LnkHelper.N；20250223a | tabPlug3 | 3038–3041 |

细页通用返回只改`settingPlugInTab=tabPlug1`，保留settingTab=tab4；原通用导航插件页会reset到tabPlug1。DDB标题下GitHub是真外链，其余PPT/SuperTop/Shortcut此处无源码链接。总览description必须同时保留status和插件功能描述，中间换行 (`2995–2998`)。

### PPT联动

错误/长文先于控件：`pptComVersion`为Error时显示错误Warning含原始错误字符串；始终显示Tip InfoBar，action→`https://www.inkeys.top/tutorial/ppt-com`；`pptComSetlist.setAdmin`时另有Warn和action→`https://www.inkeys.top/tutorial/ppt-admin` (`3047–3075`)。**Error状态不禁用PPT设置。**

| ID / 控件 | 字段 | 写入与即时通知 | 行号 |
| --- | --- | --- | --- |
| ppt-fixed-ink | PptComFixedHandWriting→pptComSetlist.fixedHandWriting | PptComWriteSetting；保留各页独立墨迹长说明 | 3080–3086 |
| ppt-loading-screen | PptComShowLoadingScreen→pptComSetlist.showLoadingScreen | PptComWriteSetting | 3088–3094 |
| ppt-bottom-pair | ShowBottomBoth→pptComSetlist.showBottomBoth | PptComWriteSetting→NotifyConfigurationChanged(BottomPair) | 3099–3106 |
| ppt-middle-pair | ShowMiddleBoth→pptComSetlist.showMiddleBoth | PptComWriteSetting→NotifyConfigurationChanged(MiddlePair) | 3108–3115 |
| ppt-exit-control | ShowBottomMiddle→pptComSetlist.showBottomMiddle | PptComWriteSetting→NotifyConfigurationChanged(ExitShow) | 3117–3124 |
| ppt-reset-position / button | pptComSetlist.bottomBothWidth/Height、middleBothWidth/Height、bottomMiddleWidth/Height及各draft均置0 | PptComWriteSetting→NotifyConfigurationChanged(All)；不是scale reset | 3129–3143 |
| ppt-remember-position | MemoryWidgetPosition→pptComSetlist.memoryWidgetPosition | PptComWriteSetting | 3147–3152 |
| ppt-bottom-scale | BottomSideBothWidgetScale→pptComSetlist.bottomSideBothWidgetScale | slider/reset/sync特殊算法见下 | 3193–3196 |
| ppt-middle-scale | MiddleSideBothWidgetScale→pptComSetlist.middleSideBothWidgetScale | 同上 | 3197–3200 |
| ppt-exit-scale | BottomSideMiddleWidgetScale→pptComSetlist.bottomSideMiddleWidgetScale | 同上 | 3201–3204 |
| ppt-auto-takeover | `Inkeys::config.PlugIn.PPTHelper.AutoTakeOver` | 每帧读本地bool，toggle变化改配置→QueueConfigWrite | 3281–3288 |
| ppt-auto-takeover-once | 同组AutoTakeOverOnce | 同上；不因总开关false隐藏/禁用 | 3290–3297 |
| ppt-auto-takeover-expand | 同组AutoTakeOverExpand | 同上；不因总开关false隐藏/禁用 | 3299–3305 |
| ppt-long-press | 同组Tentative.EnablePageButtonLongPress | 同上 | 3307–3314 |

PPT三组scale必须整体迁移 (`3157–3277`)：

1. 每卡slider0.5..3.0，round两位，倍数indicator；reset置该值1.0；sync ToggleButton操控对应临时`*Unifie`。初始bottom=true、middle=true、exit=false (`1933–1935`)，**不是配置字段**。
2. `anyScaleActive`是在3个slider之后累计，不被reset/sync/后续文字IsItemActive覆盖。
3. 同步由false变true时当前组先采用另一已同步组的值：bottom优先middle再exit；middle优先bottom再exit；exit优先bottom再middle (`3206–3225`)。
4. 比较draft与pptComSetlist判定变化，按bottom→else middle→else exit优先级，把变化值传播至参与同步的其他组 (`3228–3254`)。
5. 任一最终值变化：三字段写入pptComSetlist→NotifyConfigurationChanged(All)，**拖动实时生效**。
6. 仅当!anyScaleActive，且与三个Record任一不同：更新三个Record→PptComWriteSetting，**松手保存**。不要给3个slider各写一份独立保存逻辑导致半组数据。

`PptUiWidgetScale`虽仍声明却无实际控件，不新造第四个缩放。新配置4项与旧PPT配置保持各自文件，不合并。

### SuperTop与快捷方式

| ID | 字段/条件 | 原副作用 | 行号 |
| --- | --- | --- | --- |
| super-top-enable | PlugInSetting.SuperTop.Enable→setlist.plugInSetting.superTop.enable | WriteSetting；不立即触发提权/重启。hasSuperTop只选择E1/E2“当前已/未超级置顶”说明，不禁用输入 | 3328–3337 |
| super-top-indicator | PlugInSetting.SuperTop.Indicator→setlist.plugInSetting.superTop.indicator | WriteSetting | 3339–3345 |
| shortcut-correct | CorrectLnk→setlist.shortcutAssistant.correctLnk | WriteSetting后仅value=true调用**shortcutAssistant.SetShortcut()** | 3356–3363 |
| shortcut-create | CreateLnk→setlist.shortcutAssistant.createLnk | WriteSetting后，只要correctLnk为true便调用**SetShortcut()**（不只在create=true时调用） | 3367–3375 |

SuperTop Warning是多段长说明，含BETA、三段式启动/提权、UAC、可能影响PPT；迁移时可折叠但要完整可访问。它说明未来启动行为，不要添加本轮toggle后的立即提权流程。Shortcut两按钮是开关，不是“现在打开组件”的普通导航。

### DDB全部20项

公共内容：版本来自DdbEdition，GitHub→`https://github.com/Alan-CRL/DesktopDrawpadBlocker`，GPLv3/许可风险InfoBar；不丢其长插件名称或许可说明 (`3382–3388`)。

| ID | 字段/范围 | 原保存/动作 | 行号 |
| --- | --- | --- | --- |
| ddb-enable | Ddb.Enable→ddbInteractionSetList.enable | QueueBusiness(ConfigureDdb)：flag=value，secondaryFlag=现runAsAdmin，text=pluginPath+DesktopDrawpadBlocker/EXE，directory=插件目录，parameters=当前EXE，digest=DdbSHA256 | 3391–3405 |
| ddb-admin | Ddb.RunAsAdmin→ddbInteractionSetList.runAsAdmin | **先WriteSetting，再QueueBusiness(RestartDdb)**，flag=value，text=插件EXE | 3409–3420 |
| ddb-interval | ddbInteractionSetList.sleepTime | UI索引0..4 → **实际毫秒500/1000/3000/5000/10000**；非标准旧值展示index3，只有用户改变才写。WriteSetting→QueueDdbWriteInteraction(true,false) | 3422–3442 |

DDB配置关闭时，admin、interval和以下17项仍然显示且可用。精确开关数组每行指向`Ddb.intercept.<字段>`；当任一行变化，统一把17个draft字段复制到`ddbInteractionSetList.intercept`，WriteSetting→QueueDdbWriteInteraction(true,false) (`3496–3529`)。

| 原ID | 字段（两结构同名） | 必须保留的特别说明 | 行号 |
| --- | --- | --- | --- |
| ddb-seewo3 | SeewoWhiteboard3Floating | 希沃白板3桌面画笔 | 3456 |
| ddb-seewo5 | SeewoWhiteboard5Floating | 希沃白板5桌面画笔 | 3458 |
| ddb-seewo5c | SeewoWhiteboard5CFloating | 希沃轻白板5C | 3460 |
| ddb-pinco-sidebar | SeewoPincoSideBarFloating | 希沃品课教师端侧栏 | 3462 |
| ddb-pinco-drawing | SeewoPincoDrawingFloating | 希沃品课教师端画笔 | 3464 |
| ddb-seewo-ppt | SeewoPPTFloating | 希沃PPT小工具 | 3466 |
| ddb-iwb-assistant | SeewoIwbAssistantFloating | 希沃课堂助手PPT | 3468 |
| ddb-yiou | YiouBoardFloating | 支持自动恢复 | 3470 |
| ddb-aiclass | AiClassFloating | AiClass | 3472 |
| ddb-classinx | ClassInXFloating | ClassIn X | 3474 |
| ddb-intelligent-class | IntelligentClassFloating | 包括PPT控件 | 3476 |
| ddb-changyan4 | ChangYanFloating | 包括PPT，支持白板自动恢复，需要管理员 | 3478 |
| ddb-changyan5 | ChangYan5Floating | 同上 | 3481 |
| ddb-iclass30-sidebar | Iclass30SidebarFloating | 需要管理员 | 3484 |
| ddb-iclass30-drawing | Iclass30Floating | 包括PPT、白板恢复/窗口追踪、需要管理员 | 3486 |
| ddb-seewo-desktop-sidebar | SeewoDesktopSideBarFloating | 1.0/2.0/2.5/3.0普教与高教通用，需要管理员 | 3489 |
| ddb-seewo-desktop-drawing | SeewoDesktopDrawingFloating | 同上 | 3492 |

DDB FIFO语义：ConfigureDdb冻结旧配置及close/open JSON (`791–798`)；worker开时可能创建目录、hash检查、等待运行实例退出、解包EXE、以普通/runas启动；关时发送close、禁用插件启动项、删除start_up.signal (`700–736`)。RestartDdb若没在运行直接成功，否则close→最多25×500ms等待→open→Shell (`738–746`)。WriteDdb自身也写旧配置和交互JSON (`747–749`)；不要顺手删除表面重复的WriteSetting。所有DDB输入均排除在只导航截图动作之外。

### 组件全部16项

共用动作 `renderComponentToggle`：stored!=value时**改setlist→WriteSetting→SyncUi3BuiltInComponents()**，后者SyncLegacyExtensionButtons+UpdateRendering (`3540–3550,950–954`)。不能改为新版ExtensionButtons持久化；不能只写盘不更新当前Bar。

下面字段均在`setlist.component.shortcutButton.`之下；各draft以原ComponentShortcutButton*命名，不能按名称猜执行动作：此页开关只是控制按钮可见性。

| ID | 配置后缀 | 文案/条件说明 | 行号 |
| --- | --- | --- | --- |
| component-explorer | appliance.explorer | 文件资源管理器 | 3560 |
| component-taskmgr | appliance.taskmgr | 任务管理器 | 3563 |
| component-control | appliance.control | 控制面板 | 3566 |
| component-desktop | system.desktop | 显示桌面 | 3571 |
| component-lock | system.lockWorkStation | 锁屏 | 3574 |
| component-escape | keyboard.keyboardesc | ESC | 3579 |
| component-alt-f4 | keyboard.keyboardAltF4 | Alt+F4 | 3582 |
| component-island-caller-1 | rollCall.IslandCaller1 | ClassIsland的IslandCaller插件+URL协议 | 3587 |
| component-island-caller-2 | rollCall.IslandCaller2 | 同上 | 3592 |
| component-sec-random-1 | rollCall.SecRandom1 | URL协议 | 3597 |
| component-sec-random-2 | rollCall.SecRandom2 | **IPC协议** | 3601 |
| component-sec-random-compat | rollCall.SecRandom2Compat | **URL协议兼容模式** | 3605 |
| component-name-picker | rollCall.NamePicker | URL协议 | 3609 |
| component-classisland-settings | linkage.classislandSettings | ClassIsland应用设置 | 3617 |
| component-classisland-profile | linkage.classislandProfile | 档案编辑 | 3621 |
| component-classisland-swap | linkage.classislandClassswap | 未加载课表时组件不起作用 | 3625 |

保持软件/系统/键盘模拟/点名器/ClassIsland联动5分组；ClassIsland联动前有注册URL导航协议InfoBar (`3615`)。说明能力不足不是设置开关的禁用条件。

### 实验、快捷键、支持开发、调试

实验字段前缀`Inkeys::config.Experimental.Inkeys3.UI3.`；draft对应`Experimental.Inkeys3`：

| ID | 字段 | 显示/保存/实时动作 | 行号 |
| --- | --- | --- | --- |
| experimental-dynamic-light | EdgeLighting.Dynamic | 仅EdgeLightingEnable为true显示；改字段→SetEdgeLightingOptions(Enable,Dynamic)→QueueConfigWrite；隐藏不覆盖值 | 3648–3663 |
| experimental-dirty-debug | Debug.Enable | 改字段→SetDebugOptions(DebugMode,ShowFrameRate)→QueueConfigWrite | 3668–3681 |
| experimental-frame-rate | Debug.ShowFrameRate | 仅DebugMode为true显示；改字段→SetDebugOptions(DebugMode,ShowFrameRate)→QueueConfigWrite；隐藏不覆盖值 | 3686–3700 |
| experimental-animation | Animation.Enable | 改字段→SetAnimationOptions(Enable,SpeedRate)→QueueConfigWrite | 3705–3718 |
| experimental-animation-speed | Animation.SpeedRate | **即使AnimationEnable=false仍显示可用**；0.1..5.0，round一位；>0.0001变化即写double并SetAnimationOptions，置savePending，非active再QueueConfigWrite并清pending | 3723–3744 |

- Hotkey `3632–3640`：只读InfoBar，保留全局Ctrl+Win+Alt与绘制Ctrl+Q/E/Z等分组/换行和现有尾部说明。不要把它扩展为未授权的编辑器。
- Sponsor `3752–3771`：TextureSettingSign[9]，按settingSign[9]真实宽高（宽无效fallback0.56）展示；原“成功赞助后可联系作者添加社区名片”说明。图片本身不是可点击捐款按钮，不能将截图动作变成支付。
- Debug `3778–3783`：唯一AccentButton调用**ChangeStateModeToTouchTest()**，会改变画布模式；视觉脚本不点击。下面实时状态不是固定示例文案。
- Debug `3793–3854`：rtsDown/rtsNum/touchNum，逐TouchList pid/type（触摸、笔及倒置、鼠标左右键）、坐标、TouchSpeed、面积/压力或不支持；TouchPos与TouchSpeed分别按原shared_mutex读取。还显示TouchList/TouchTemp、RecallImage大小/峰值、FirstDraw、PPT COM成功/错误及TotalPage/CurrentPage、显示器数量、MainMonitor像素/物理尺寸。保留全部信息和长行换行/滚动，不另引并发模型重构。
- 原Debug外卡520 DIP、内scroll480 DIP (`3788,3856`) 可随新布局调整，但**必须仍能读完整状态**，不能只留卡片固定高度而裁掉后半内容。

### 全部真实GUI视图覆盖表（只导航和滚动）

以下数值仅用来核对原enum与路由，不代表已有注入页面API，也不授权修改进程内存。实际脚本应依据已截图的坐标/Win32客户区与真实可见导航元素点击，不能把未来布局位置当成已知事实。

| 序号 / 视图 | 原主tab/子tab | 安全入口与返回 | 重点截图区域 | 本轮视觉脚本跳过 |
| --- | --- | --- | --- | --- |
| 01 主页 | 0 / — | 主导航Home | hero、入口、作者区，默认+宽+窄 | 主操作、外链（首批绑定另表） |
| 02 语言 | 1 / — | 主导航语言 | 长说明、combo边缘、窄布局 | 更改语言，会保存并弹重启 |
| 03 配置保存 | 3 / — | 主导航配置 | 3行，长清理说明，页底 | 所有开关/选项，AutoClean有删除旧键行为 |
| 04 软件版本 | 11 / — | 主导航版本或标题栏版本 | 顶部信息、警告、userId长文、修复/更新区页底 | 更新/修复、所有设置、复制ID（剪贴板） |
| 05 构建详情 | 2 / — | 版本中CICDInfo（仅url非空）；back→版本 | 长url与完整字段、页底 | 两个外链；无入口时记录自然不可达 |
| 06 常规 | 4 / — | 主导航常规 | 字体/9行/长Details，默认+宽+窄 | 所有配置动作（首批绑定另表） |
| 07 绘制 | 5 / — | 主导航绘制 | 9行顶/中/底、准备画布slider | 所有输入，尤其重建画布和绘制设备 |
| 08 预设 | 7 / — | 主导航预设 | 自适应动态说明；若自然手动则2slider；页底 | toggle/slider；不为截图切自适应 |
| 09 插件总览 | 8 / 0 | 主导航插件会reset子页 | 4卡长描述/状态/入口；页底 | 仅“插件选项”是可点内部导航 |
| 10 PPT联动 | 8 / 1 | 总览ppt-helper插件选项；back→总览 | error/admin提示若自然存在，顶/中/底，3scale卡，实验4行 | solve外链、所有toggle/slider、reset/sync |
| 11 超级置顶 | 8 / 3 | 总览super-top；back→总览 | 完整长Warning、hasSuperTop说明、2行 | 两个设置开关 |
| 12 DDB | 8 / 4 | 总览ddb；back→总览 | 长标题、许可、3基础行、17项顶/中/底，窄长说明 | GitHub、所有开关/下拉，可能启动/重启外部插件 |
| 13 快捷方式 | 8 / 2 | 总览shortcut-helper；或常规“更多选项”；back→总览 | 2设置行及说明 | 两toggle可能立即更改桌面快捷方式 |
| 14 组件 | 9 / — | 主导航组件 | 5组/16行，协议长说明、底部ClassIsland | 全部toggle（会改Bar按钮）；不要误点产品Bar对应动作 |
| 15 快捷键 | 10 / — | 主导航快捷键 | 多行分组/说明完整，无重叠 | 不模拟文中快捷键，可能切绘制/冻结 |
| 16 实验选项 | 14 / — | 主导航实验 | natural条件分支，速度行；页底 | 所有配置；不为显示FPS/dynamic改父开关 |
| 17 支持开发 | 12 / — | 主导航赞助/支持 | 原赞助图比例、整张码/文字、说明，窄窗 | 不扫描/触发支付或外链 |
| 18 软件调试 | 13 / — | 主导航调试 | 实时状态卡顶部/内部滚动到底、窄长行 | “开启”触摸测试，不切主画布状态 |

共享可执行视觉动作：汉堡、主导航13页、插件4内部入口、子页返回、若出现的CICD内部入口、清楚的详情展开/收起、内容/导航滚动、窗口大小变化；overlay遮罩/Esc只是关闭pane。所有持久化开关/slider/选中combo项、复制、外链、重启/退出、更新/修复、插件启停、模式切换都跳过。鼠标移动或滚轮定位于内容空白/滚动区，避免把键盘箭头送入已聚焦slider/combo。

最低覆盖策略：默认窗口对18视图各记录top，长页追加mid/bottom；宽窗复核主页/常规/版本/PPT/组件；窄内容预算复核常规、SuperTop长Warning、DDB最后长说明、PPT复合scale卡、Debug内scroll。当前自然状态没有的条件分支用静态审计/无窗口fixture验证并明确标记，不能伪称真实GUI覆盖。

## Related Specs

- `.trellis/workflow.md`：本次授权已由主会话记录，研究只维护自身产物。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：FIFO与shared renderer/resident/Begin-End边界。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：两套配置、组件旧字段实时投影与i18n源/生成链。
- `.trellis/spec/ppt-interop/index.md`：旧PPT配置与新PPTHelper配置并存；不改COM/Office支持边界。
- `research/approved-full-migration.md`：全页视觉改造，主会话可脚本GUI；研究代理仍禁止运行应用。

## External References

本次为固定提交的内部行为审计，无外部网络调研。文中列出的URL是原产品动作目标，不表示已打开或验证内容。

## Caveats / Not Found

- `tabPerformance=6`无case，不存在第19页。
- `settingCICD.url`为空时构建详情无用户入口；不能通过造状态或额外页入口来强行凑齐GUI截图。
- 原无“插件关闭则所有项禁用”“COM错误则不可设置”“history关闭则retention禁用”等规则；不要根据主观常识引入行为变更。
- 基线旧语言/i18n中仍有残留占位/不一致，不以“保留功能”为名新造控件。本清单逐项原业务映射，不宣称迁移后的工作区已完成验证。
- 研究没有改代码、运行git、启动应用或操控桌面；实际截图与build由主会话执行。