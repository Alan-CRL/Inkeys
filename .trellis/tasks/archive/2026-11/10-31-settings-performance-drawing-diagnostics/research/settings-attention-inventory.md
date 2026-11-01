# R8 活动设置注意事项盘点

日期：2026-11-01。只读检查 Setting.cpp、Setting.Pages.cpp、三语言说明及必要消费者；本文件是实施前盘点，行号以盘点时源码为准，后续新增提示参数会令行号移动。只写此研究文件，未修改产品/其他规划文件、未构建/GUI/提交。

## 1. 分类原则与必须直接删除的句子

- 只有真实的特殊生效时机、外部权限/依赖、兼容限制、数据风险和明确行为例外进入解释下方的信息 i 行。不要根据“清空/重启/需要”等关键词自动拆句。
- 提示正文与解释同字号，蓝色 i 图标、正常前景文字；自动换行纳入本行高度，不能作为新的大警告卡片盖住操作控件。
- 激光多指 E 中“修改后对新开始的笔画生效”、粒子 E 中“当前版本暂不可用”及三语言对应句子直接删除，**不得作为任何新提示的来源**。保留 BETA、无粒子toggle、Controller强制关闭。原统一 NewStrokeNotice 不恢复。
- 不因为添加提示改变选项的实际时机、权限申请或动作；静态解释、运行状态、独立错误/重启确认对话框要分开处理。

## 2. 确认需要分离的活动说明

下表 key 均省略 `SettingsUI.` 前缀。中文原句用于界定内容，实施时同步 en-US/zh-TW，不只处理中文。已有全局或子页提示应避免再给每个控件重复同一句。

| 活动条目/来源 | 普通解释保留 | 应独立的注意事项 | 真实行为与锚点 |
| --- | --- | --- | --- |
| 界面语言 `Language.UI.SelectE` | Choose language可保留或按既有语言显示 | 需要重启软件才能完全生效 | Setting.cpp:2699–2711先保存/加载语言，再QueueConfirmRestart；原Warn是独立确认，不删改 |
| 自动清理配置 `Configuration.Clean.EnableE` | 启动和保存时删除多余配置项 | 降级再升级/导入配置可能丢失部分配置项，恢复默认值 | Setting.cpp:2723；IdtConfiguration.cpp:533在AutoClean时clear。是配置风险，不是历史画布文件丢失 |
| 启动位置 `Preset.StartupPlacement.E` | 居中底栏/左右悬浮的显示区别 | 更改在下次启动生效 | Pages.cpp:799–804；对应保存逻辑不移动当前主栏 |
| 画布帧率 `Performance.CanvasFrameRate.E` | 刷新频率及资源/流畅度权衡 | 重启软件后生效 | Setting.cpp:4070–4075仅保存Drawing.FrameRate，Host取启动快照 |
| 画布硬件加速 `Experimental.Draw3.HardwareAcceleration.E` | 显卡加速/关闭后软件绘制 | 重启生效；可将“绘制异常或严重卡顿时可尝试关闭”作为同一提示中的既有排障建议 | Setting.cpp:4079–4083；主程序启动捕获，不能文案改成即时 |
| DComp `Performance.DirectComposition.E` | 支持设备用系统合成，不可用自动回退 | 重启软件后生效 | Setting.cpp:4085–4089；窗口创建期偏好 |
| 兆芯兼容 `Experimental.Draw3.ZhaoxinCompatibility.E` | 兆芯GPU兼容上传、其他GPU无影响，可关闭对比性能 | 重启软件后生效 | Setting.cpp:4091–4095；不是更换CPU或所有显卡的总模式 |
| 控制台TouchArea/PptCOM/Draw3/Haptics/Cursor五项 | 各自输出内容、用途，限频等说明保留 | 下次启动/重启后生效 | Setting.cpp:4161–4184的consoleToggle仅保存；IdtMain.cpp:2928–2950启动统一冻结并启用控制台/诊断 |
| 目标更新架构 `Version.Update.Arch.E` | 可无额外普通解释，不必编造 | 与系统架构不一致可能无法启动或严重影响性能 | Setting.cpp:3007–3029；这是后续安装包目标，不是本进程即时切换架构 |
| 更新通道 `Version.Update.ChannelTip` | LTS/Insider/Canary的正常通道差异 | Insider可能杀软误报；Canary不建议使用，原警示不可丢失 | Setting.cpp:2956–2973目前为组级Notice+Combo；提示应与通道关联，不能搬成每个下拉选项名字 |
| 教学安全模式 `Regular.Tentative.SafeMode.E` | 崩溃时按所选方式处理 | 启动过程中错误仍会提示，是静默模式的例外 | Pages.cpp:883–886。四个Mode名称中的“重启软件”是选项本身，不能拆掉 |
| 超级置顶 `PlugIn.SuperTop.Capability.SuperTopE1/E2` | 当前已经/未超级置顶等状态；必要时保留启动方式解释 | 重启后生效，以及原“小概率置顶失败”注意 | Setting.cpp:3765–3774按hasSuperTop选择两个E；必须同时处理两种状态，不能只改E1 |
| 超级置顶简介 `PlugIn.SuperTop.E` | UiAccess置顶作用和覆盖的系统界面范围 | 仅支持Windows8及以上（已有文案，不扩大支持承诺） | Setting.cpp:3525–3528通过插件概览共用description显示；不是只有ToggleRow里才有前提 |
| 触摸面积辅助 `Draw.Eraser.TouchArea.E` | 使橡皮略大于手指，避免遮住光标 | 不建议大屏设备启用 | Pages.cpp:696–698局部`eraser.TouchArea.E`，完整I18nKey搜索会漏。与运行时物理尺寸缺失警告不同 |
| 快捷键总说明 `HotKey.E` | 固定组合、不能自定义、批注/选择/文本/模态优先和PPT翻页范围等正常行为 | 国际键盘AltGr可能与Ctrl+Alt冲突，可单独关闭全局往返 | Pages.cpp:890已有组级Notice，可能需拆为普通解释+信息行；不改变全局快捷键注册/保留规则 |
| 定格快捷键 `HotKey.FreezeE` | 切换屏幕定格 | PPT和白板不可用；不可用时不触发下层同名快捷键 | Pages.cpp:900经descriptions数组显示，不能只检查直接ToggleRow实参 |

### 易混淆的“启动时”

- `Regular.StartUp.LoadingAnimationE`（Pages.cpp:828–830）“控制下次启动时是否显示加载动画”描述本来只出现在启动时的功能。可以把“下次启动时应用”作为中性生效提示，但不要新增“必须立刻重启”的警告；当前启动画面已经结束。
- `PlugIn.LnkHelper.Capability.FixLnkE`（Setting.cpp:3790–3797）“软件启动时修正快捷方式”是功能说明；开启时回调还会立即 `shortcutAssistant.SetShortcut()`。**不能机械改成重启后才生效**。
- `Configuration.CanvasSave.EnableE`（Setting.cpp:2738–2745）“清空桌面画布或退出时自动保存非空画布”是实际触发条件，保持主解释，不是删除数据警告。

## 3. 权限、插件与协议前提：重要的非普通E字符串

### 3.1 PPT与超级置顶的已有组级注意

| 当前来源 | 注意内容 | 处理边界 |
| --- | --- | --- |
| PPTHelper.Tip，Setting.cpp:3557–3563 | Inkeys与PPT/WPS权限必须一致才能识别放映 | 已是静态组级Notice且带解决方案按钮；改为i行时保留教程动作，不能丢链接或在每个PPT开关下重复 |
| PPTHelper.Warn，:3564–3572 | 检测到放映程序总以管理员运行，Inkeys需对应权限；附解决方案 | 条件化检测提示，与普通描述不同；保留条件和教程动作，不改权限流程 |
| SuperTop.Warn，:3759–3762 | BETA不稳定、启动提权/新实例、失败时可能变管理员影响PPT、UAC与杀软提示 | 已有整块静态注意；若本轮整理其呈现，须完整保留这些既有事实并贴合SuperTop入口，不另造安全建议，也不能把整段风险删成只有“重启生效” |

### 3.2 DDB的硬编码注意

`Setting.cpp:3814`的DDB子页不全走i18n；只扫描JSON说明会漏掉以下项：

| 控件/行号 | 保留的功能解释 | 要独立的注意 |
| --- | --- | --- |
| 管理员启动DDB :3844–3855 | 以管理员运行插件 | 切换需要等待插件响应。旧文案声称“软件阻塞至多拦截间隔”，但实际RestartDdb由worker执行，:1000–1015按25×500ms上限等待旧进程退出；不要宣称UI线程阻塞，也不要新增错误的拦截间隔上限。显示调整不改流程 |
| 畅言4/5 :3912–3915 | 包括PPT控件、支持白板时恢复 | 需要DDB管理员权限 |
| C30侧栏 :3916 | 控制相应侧栏 | 需要DDB管理员权限 |
| C30桌面画笔 :3917–3918 | 包括PPT控件、白板恢复、窗口追踪 | 需要DDB管理员权限 |
| 希沃桌面侧栏/画笔 :3919–3922 | 1.0/2.0/2.5/3.0及普教/高教版本范围 | 需要DDB管理员权限；版本范围是兼容范围说明，可保留普通解释，不因包含“通用”就移成警告 |

“包括PPT控件”“支持白板自动恢复/窗口追踪”是功能描述，不是权限注意。DDB“更短间隔更快、CPU占用更多”（:3866–3868）是普通参数权衡，可保留解释；不要为了分离注意重构这个插件配置。

DDB GPLv3说明（:3820–3823）含许可、免责声明、仓库链接，是独立插件许可块，不应复制到每条DDB设置下或按“风险”关键词裁掉法律文本。若需要统一其视觉，只能在原区域保持语义/链接，不在本次盘点里替用户重新解释许可。

### 3.3 组件协议/应用状态前提

| 条目 | 现有特别前提 | 活动锚点 |
| --- | --- | --- |
| IslandCaller1/2 | 安装ClassIsland的IslandCaller插件，并注册ClassIsland URL协议 | Setting.cpp:4006–4015 |
| SecRandom1 | 注册URL协议 | :4016–4019 |
| SecRandom2 | 注册IPC协议，不能误改成URL | :4020–4023 |
| SecRandom2兼容 | 注册URL协议，与IPC项分开 | :4024–4027 |
| NamePicker | 注册URL协议 | :4028–4031 |
| ClassIsland联动组 | 在ClassIsland设置注册URL导航协议 | :4033–4037已有组级Notice；可放一个组级i行，避免三个组件重复 |
| ClassIsland快速换课 | 当前无加载课表时不起作用 | :4046–4050，属于额外状态前提，不能因上面已有协议提示而漏掉 |

Bar.Button.cpp:871–925将这些活动组件注册为对应BuiltInComponentAction，说明确属当前产品入口而非旧i18n残留；实际外部提供方是否已经注册协议属于运行环境，本任务只呈现既有前提，不新增外部检测或安装流程。

## 4. 应保留普通解释或原独立状态的内容

### 普通功能/模式说明，不机械改成注意

- 选择/穿透的清空或保留墨迹规则（Pages.cpp:769–775）：这是用户所选模式的定义，不新增“不可恢复”警告。
- 清空快捷键的第一次/第二次按压、长按不计两次（Pages.cpp:899–900）：交互规则，不是数据损坏提示。
- 软件安全模式的“弹窗提示并重启”“静默重启”等Mode1/2名称：必须作为下拉项原样保留，不能把“重启”抽成i行。
- 笔/鼠标光标适用软硬笔、荧光笔、形状以及设备边缘高亮规则（Setting.cpp:3419–3422）：本身是功能范围解释，不发明新限制。
- 更低界面帧率/更高画布帧率、置顶间隔、DDB间隔的资源消耗权衡：正常参数说明；帧率的“重启生效”另分离即可。
- 小数字体的静止/缩放行为及“立即生效”（:4106–4116）：不是重启前提，不误加重启提示。

### 动态状态/错误与独立对话框，保持流程

- 语言 `Language.UI.Warn`用于QueueConfirmRestart（:2711），是明确询问是否重启的对话框，不是描述字符串；不要删确认动作或替换成行提示。
- 更新进度/失败/已下载需重启（:2759–2825）的 `SettingsUI.Update.*` 是状态机，不是选项解释；保持状态和动作。
- 软件架构实际不匹配 `Version.VersionTip`（:2858–2865）是条件检测后的状态；与目标更新架构选项自身的风险分别保留，不用所有版本页永远显示重复不匹配提示。
- PPT版本获取失败/缺少.NET提示（:3548–3555）是异常诊断，不是每个PPT开关的前提行；不能隐藏真实错误。
- Draw3恢复警告（绘制页靠前）、快照中未初始化/失败/停止等信息是实际状态，不在R8改绘图逻辑或伪造提示。
- 笔速橡皮 `draft.physicalSizeWarning`（Pages.cpp:652–656，经局部`speed.SizeWarning*`）是运行环境动态降级：真实缺少物理尺寸时用DPI估算，不能变成每次开橡皮都必显的静态注意。不得与“大屏不建议面积辅助”合并丢掉条件。
- 快捷键HookFailed/SynchronizationPending等是运行状态（Setting.cpp:3090–3103到GeneralDraft），与AltGr的静态兼容注意不同；保留实际状态/按键释放操作。
- 首页预览版提示和开源协议/条款链接不是设置选项，不能仅因“品质/协议”被塞入任意设置行。

## 5. 未活动或旧兼容键：不因本次整理恢复UI

以下直接源码搜索未发现对应活动设置入口，不应批量为它们添加提示卡或恢复功能：

- `Design.AvoidFullscreenSummary`：旧避免全屏的重启说明，已退役。
- `Configuration.CanvasSave.SaveTime.*`：保留文案不是当前画布保存行的定时清理控件。
- `Regular.Behavior.CheckingRetractE`、`Regular.Appearance.Theme.E`：未在当前Setting/Pages渲染相应旧项。
- `PlugIn.PPTHelper.Tentative.CloseWppE`：旧WPS关闭进程/重启说明，当前PPT页只渲染AutoTakeOver/Once/Expand/LongPress，不恢复旧开关。
- `Version.LimitUpdate.*`、`Version.Inkeys3Update.*`：当前版本页未使用旧整块发布公告；不能把它们当现行更新风险依据。

但“完整I18nKey未直接引用”不能自动判未用：SpeedEraser的`speed/eraser`别名、快捷键的descriptions数组、PPT的renderToggleCard和组件列表均是活动的间接引用，应按本文件锚点处理。

## 6. 三语言与防遗漏验收

盘点时zh-CN/en-US/zh-TW三语言各559个叶子键，以上重点说明的三语言都有对应值。拆分注意应同步新增/调整三语言及生成keys/中文快照；不要把英文剩余Restart句或繁中“重新啟動/系統管理員”留在原解释造成重复。

收尾建议按清单逐类检查，而非仅grep重启：

1. 语言、启动位置、画布帧率、硬件/DComp/兆芯、SuperTop两个状态、五个控制台开关。
2. 自动清理的数据风险、架构风险与更新通道风险。
3. PPT权限与其教程动作、SuperTop兼容/提权既有注意、DDB五组管理员前提。
4. IslandCaller/NamePicker/SecRandom URL与IPC、ClassIsland协议与课表两种前提。
5. SafeMode启动例外、AltGr、Freeze工作区限制、触摸面积辅助大屏建议，以及条件运行状态不被误改。
6. 两条激光指定句确实删除而未转移；BETA与粒子无toggle/强制false保持；统一新笔提示不恢复。

本文件不表示所有旧Notice必须无条件改样式；它给出需要实施代理明确归属的静态注意、动态状态和独立对话框边界，避免遗漏或产生新的警告。实际布局字号、换行和控件重叠由本轮控件测试验证，GUI未授权。
