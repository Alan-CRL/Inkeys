# 现有三语翻译修改对照表

对照范围：本任务实施前保存的原始快照与最终 `Inkeys/src/i18n/` 文件。仅列实际变更，未变条目省略；新增八种语言不计入此表。

表中的 `\n`、`\r`、`\t` 分别代表实际换行、回车和制表符；`␠` 表示首尾空格。格式占位符按原值保留。

共 **451** 项语言文案变化（同一 key 在不同语言分别计数）。

| 语言 | 修改 | 新增 | 移除 | 合计 |
|---|---:|---:|---:|---:|
| zh-CN | 42 | 84 | 0 | 126 |
| en-US | 79 | 84 | 0 | 163 |
| zh-TW | 78 | 84 | 0 | 162 |

## zh-CN

共 126 项变化。

| 类型 | Key | 修改前 | 修改后 | 原因 |
|---|---|---|---|---|
| 修改 | `Widget/LnkName` | <code>智绘教(屏幕批注工具)</code> | <code>智绘教Inkeys（屏幕批注工具）</code> | 按用户确认的通用品牌及新建快捷方式命名规则，保留本地化用途说明。 |
| 新增 | `UI/Bar/Components/ExplorerLabel` | —（原无此 key） | <code>文件管理器</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/TaskManagerLabel` | —（原无此 key） | <code>任务管理器</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ControlPanelLabel` | —（原无此 key） | <code>控制面板</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ShowDesktopLabel` | —（原无此 key） | <code>显示桌面</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/LockScreenLabel` | —（原无此 key） | <code>锁屏</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/EscapeLabel` | —（原无此 key） | <code>ESC键</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/RollCallLabel` | —（原无此 key） | <code>随机点名</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandSettingsLabel` | —（原无此 key） | <code>CI设置</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandProfileLabel` | —（原无此 key） | <code>档案编辑</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandSwapLabel` | —（原无此 key） | <code>快速换课</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 修改 | `UI/Bar/DrawAttributes/HoldThicknessLabel` | <code>保持并固定粗细</code> | <code>按住以锁定粗细</code> | 按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 修改 | `UI/Bar/ColorPicker/HoldColorLabel` | <code>保持并固定颜色</code> | <code>按住以锁定颜色</code> | 按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 修改 | `UI/Bar/Errors/TransparentBackgroundWarning` | <code>软件遇到透明背景无法正常显示的故障\n\nexe属性-&gt;关闭使用简化的颜色模式\nWindows7用户请开启Aero主题\n\n联系开发者-&gt;软件选项主页中\n重启软件试试</code> | <code>Inkeys 无法正常显示透明背景。\n\n请右键单击 Inkeys.exe，打开“属性 → 兼容性”，取消勾选“简化的颜色模式”。\nWindows 7 用户请启用 Aero 主题。\n\n重启 Inkeys 后再试。\n如仍有问题，请在设置主页联系开发者。</code> | 提供完整可执行的故障处理路径，修正系统选项名称；保留原有分段换行数。 |
| 修改 | `SettingsUI/Design/HomeRedistribution` | <code>任何人都可以查看、修改和分发此代码。</code> | <code>在 GPLv3 许可条件下，任何人都可以查看、修改和分发此项目的代码。</code> | 在既有开源许可上下文中明确分发条件，不改版权署名。 |
| 修改 | `SettingsUI/Language/UI/Language/en-US` | <code>English(Beta)</code> | <code>English &#183; en-US &#183; Beta</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Language/UI/Language/zh-CN` | <code>简体中文</code> | <code>简体中文 &#183; zh-CN</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Language/UI/Language/zh-TW` | <code>繁體中文(Beta)</code> | <code>繁體中文（台灣） &#183; zh-TW &#183; Beta</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Configuration/N` | <code>配置保存</code> | <code>配置与保存</code> | 栏目同时包含配置清理和画布保存，明确并列关系。 |
| 修改 | `SettingsUI/Configuration/CanvasSave/SaveTime/N` | <code>保存时长</code> | <code>历史画布保留期限</code> | 该设置控制历史画布保留期限，而非保存操作持续时间。 |
| 修改 | `SettingsUI/Version/LimitUpdate/N` | <code>自动更新被阻止！\n\n更新的版本系统兼容性有所调整，我们阻止了您的自动更新，因为自动更新后软件可能无法启动。请查看详细信息确认您的系统环境是否符合要求，并手动下载软件并更新。需要注意是，当前通道的修复功能将暂不可用。</code> | <code>自动更新被阻止！\n\n更新的版本系统兼容性有所调整，我们阻止了您的自动更新，因为自动更新后软件可能无法启动。请查看详细信息确认您的系统环境是否符合要求，并手动下载软件并更新。需要注意的是，当前通道的修复功能将暂不可用。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Version/Inkeys3Update/N` | <code>Inkeys3 已经正式发布！\n\n您可以通过底部按钮了解更多 Inkeys3 的信息。需要注意是，当前通道的修复功能将暂不可用。</code> | <code>Inkeys3 已经正式发布！\n\n您可以通过底部按钮了解更多 Inkeys3 的信息。需要注意的是，当前通道的修复功能将暂不可用。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Version/VersionTip` | <code>检测到软件架构与系统架构不符，与系统架构不适应有可能会导致软件效率降低，并影响体验。建议在本页面下方“软件修复”模块中修复软件（启用“修复时修正软件架构”选项），或在页面底端手动选择目标架构并修复。</code> | <code>检测到软件架构与系统架构不匹配，可能降低性能并影响使用体验。请在下方“软件修复”中启用“修复时修正软件架构”，或手动选择目标架构后进行修复。</code> | 删除重复拗口表达，保留实际修复选项及操作路径。 |
| 修改 | `SettingsUI/Version/Update/ChannelTip` | <code>正式通道(LTS) 推送最新的稳定版本, 具有较高的稳定性。（推荐）\n预览通道(Insider) 推送稳定性一般的预览版本, 且可能会被杀毒软件误报。\n早期通道(Canary) 推送 Inkeys3 早期版本, 不建议使用。</code> | <code>正式通道（LTS）：最新稳定版本，推荐日常使用。\n预览通道（Insider）：预览版本，稳定性可能较低，安全软件可能发出告警。\n早期通道（Canary）：Inkeys 3 早期版本，不建议日常使用。</code> | 统一三通道含义，避免绝对稳定性或把安全告警笼统定性为误报。 |
| 修改 | `SettingsUI/Regular/StartUp/AutoStartE` | <code>设置当前用户账户开机时启动 智绘教Inkeys。</code> | <code>当前用户登录 Windows 时自动启动智绘教Inkeys。</code> | 启动项由当前用户登录触发，并按已确认规则统一品牌。 |
| 修改 | `SettingsUI/Regular/StartUp/Link/E` | <code>自动修正名称和创建快捷方式，请点击“更多选项”。</code> | <code>可修正现有快捷方式的名称或创建新快捷方式。点击“更多选项”进行设置。</code> | 补足操作对象与配置入口，修正文句连接。 |
| 修改 | `SettingsUI/Regular/Appearance/Theme/E` | <code>选择“推荐皮肤”将会根据官方推荐皮肤自动更换。</code> | <code>选择“推荐皮肤”后，将自动采用官方当前推荐的主题样式。</code> | 消除循环定义，说明推荐皮肤的实际效果。 |
| 修改 | `SettingsUI/Regular/Appearance/TaskbarScaleBase/E` | <code>100% 时基础量高 48 DIP；真实量随任务栏厚度。</code> | <code>100% 缩放时，“任务栏基础量”的主栏高度固定为 48 DIP；“任务栏真实量”随任务栏厚度调整。</code> | 区分现有Base固定48DIP与Actual随任务栏厚度的两种模式，避免混淆。 |
| 修改 | `SettingsUI/Regular/Behavior/TopWindow/E` | <code>在非绘制模式下将按此间隔置顶窗口，落笔时暂停置顶窗口。更短的间隔可以更少地排除其他软件的窗口的干扰，但会占用更多的CPU资源。如果其他软件其他软件的窗口持续干扰，建议使用插件-超级置顶。</code> | <code>在非绘制模式下按设定间隔重新置顶窗口，落笔时暂停。间隔越短，越不容易被其他窗口遮挡，但 CPU 占用可能增加。若仍受到干扰，可尝试“超级置顶”插件。</code> | 删除重复和错字，说明周期性重新置顶、落笔暂停及资源权衡。 |
| 修改 | `SettingsUI/Regular/Behavior/CheckingRetract` | <code>点击时收起主栏</code> | <code>点击主栏外区域时收起</code> | 补充点击区域；兼容资源保留，不恢复退休设置。 |
| 修改 | `SettingsUI/Regular/Tentative/SafeMode/N` | <code>教学安全选项</code> | <code>崩溃处理方式</code> | 该组选项控制崩溃后动作，并非独立安全启动模式。 |
| 修改 | `SettingsUI/Draw/Haptics/N` | <code>笔震动反馈</code> | <code>手写笔触觉反馈</code> | 明确手写笔触觉反馈，与屏幕触摸反馈区分。 |
| 修改 | `SettingsUI/Draw/Cursor/DrawingStyle` | <code>画笔光标</code> | <code>笔形光标</code> | 源码对应PenIcon笔形图标；保留准确英文，明确中文含义。 |
| 修改 | `SettingsUI/Draw/Eraser/PalmEraser/Introduction` | <code>落笔后用手掌接触屏幕即可擦除，不必切换工具。手掌橡皮的大小会在识别时间窗口内确定，抬起后仍保留原来的工具。</code> | <code>用手掌接触屏幕即可擦除，无需切换工具。橡皮大小在接触后的识别时间窗口内确定，抬起后仍保留原工具。</code> | 按实际Touch面积识别机制表述，避免暗示另需物理手写笔先落下。 |
| 新增 | `SettingsUI/Experimental/N` | —（原无此 key） | <code>实验室</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 修改 | `SettingsUI/Experimental/ConsoleOutput/Haptics/N` | <code>测试触控反馈</code> | <code>手写笔触觉反馈诊断输出</code> | 这是诊断输出开关，而非发起一次触觉测试的动作。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tip` | <code>使用插件时需要保证智绘教Inkeys的程序权限和PPT程序是一致的才能正确识别。如果依然存在问题，可以参考解决方案。</code> | <code>使用插件时，请确保智绘教Inkeys与 PowerPoint/WPS 的运行权限相同，以便正确识别放映进程；如有问题，请参阅解决方案。</code> | 统一品牌并明确PPT联动的权限匹配条件。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Error` | <code>插件加载发生错误，可能是因为未安装 .NET Framework 4.x 或全局 COM 环境发生异常。\n错误代码</code> | <code>插件加载发生错误，可能是未安装 .NET Framework 4.x 或 COM 环境异常。\n详细信息：</code> | 消费者拼入完整Error:诊断信息而非纯数字，改为自然分隔前缀。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/CloseWppE` | <code>重启软件生效。在WPS退出放映后原来的WPP演示进程并不会立即关闭，而是保持游离卡死状态。此时再次开始放映PPT则无法立即识别到放映进程并开启PPT联动。开启此选项将帮助关闭游离卡死的WPP放映进程，这不会对您的文件造成影响，推荐开启。</code> | <code>重启软件后生效。尝试关闭 WPS 退出放映后残留的 WPP 进程，以改善下一次放映时的识别。</code> | 删除未经核验的“不会影响文件”绝对保证；保留兼容文案，不恢复旧功能。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/AutoTakeOverOnceE` | <code>防止的确需要使用 PPT 原生批注的场景。</code> | <code>避免在确实需要使用 PPT 原生批注时反复接管。</code> | 补足反复接管的动作对象，修复残缺句。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/SupportNote` | <code>支持 Microsoft PowerPoint 2007 和 Kingsoft WPS 2013 及以上版本。</code> | <code>支持 Microsoft PowerPoint 2007 及以上版本，以及 Kingsoft WPS 2013 及以上版本。</code> | 澄清两类软件各自最低版本的语法，不扩大既有支持范围。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/E` | <code>使用 UiAccess 技术在其他常规软件以及任务栏、Alt+Tab、屏幕键盘和锁屏界面等上方显示软件窗口。</code> | <code>使用 UIAccess 让窗口显示在受支持的系统界面和应用窗口上方。</code> | 去掉未经代码证实的锁屏显示保证，保留已有功能描述。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/Warn` | <code>超级置顶插件处于 BETA 阶段，可能遇到不稳定的问题。\n软件在启动时会通过再运行自身的方式进行提权，提权成功后再运行新的软件实例。\n该过程可能失败，导致错误地启动拥有管理员权限的软件，可能会影响 PPT 联动。\n过程中会弹出 UAC 确认弹窗，请点击确认或关闭 UAC 提示（不推荐）。\n过程中可能被杀毒软件误报，这是正常现象。</code> | <code>超级置顶插件处于 Beta 阶段，可能不稳定。\n软件启动时会重新运行自身以请求提升权限，再启动新实例。\n该过程可能失败；若程序以管理员权限运行，PPT 联动可能受影响。\n权限提升可能出现 UAC 提示；请核实程序来源和发布者后再确认。\n若安全软件发出告警，请核对文件来源与签名，不要直接停用防护。</code> | 准确说明权限提升与告警，删除关闭UAC的指引和把所有告警当作误报的保证。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/LicenseTitle` | —（原无此 key） | <code>GPLv3 开源插件</code> | 将既有DDB许可说明接入国际化，保留原有用途限定、GPLv3发布、无质量保证及使用后果声明。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/LicenseDescription` | —（原无此 key） | <code>插件仅供学习交流和研究使用，不得用于其他任何用途。\n同类软件悬浮窗拦截助手(DesktopDrawpadBlocker)是依据 GPLv3 许可协议发布的开源软件。\n并在 Github 仓库得到发布：https://github.com/Alan-CRL/DesktopDrawpadBlocker\n我们发布这款程序，希望它有用，但不承诺任何质量保证责任。\n用户在使用该插件时，需自行承担由此产生的后果和影响，使用插件则视为同意此协议。</code> | 将既有DDB许可说明接入国际化，保留原有用途限定、GPLv3发布、无质量保证及使用后果声明。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/UseSection` | —（原无此 key） | <code>使用插件</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableTitle` | —（原无此 key） | <code>启用插件</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableDescription` | —（原无此 key） | <code>默认模式下插件随软件开启和关闭。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/GeneralSection` | —（原无此 key） | <code>常规选项</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorTitle` | —（原无此 key） | <code>以管理员身份启动插件</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorDescription` | —（原无此 key） | <code>切换时需等待插件响应。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalTitle` | —（原无此 key） | <code>拦截间隔</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalDescription` | —（原无此 key） | <code>更短的间隔可以更快拦截出现的窗口，但会占用更多的CPU资源。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalShort` | —（原无此 key） | <code>短（500ms）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMediumShort` | —（原无此 key） | <code>较短（1s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMedium` | —（原无此 key） | <code>中等（3s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMediumLong` | —（原无此 key） | <code>较长（5s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalLong` | —（原无此 key） | <code>长（10s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PrecisionSection` | —（原无此 key） | <code>精确控制</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo3Title` | —（原无此 key） | <code>希沃白板3 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo5Title` | —（原无此 key） | <code>希沃白板5 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo5CTitle` | —（原无此 key） | <code>希沃轻白板（5C） 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PincoSidebarTitle` | —（原无此 key） | <code>希沃品课教师端 侧栏悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PincoDrawingTitle` | —（原无此 key） | <code>希沃品课教师端 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoPptTitle` | —（原无此 key） | <code>希沃PPT小工具</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IwbAssistantTitle` | —（原无此 key） | <code>希沃课堂助手 PPT小工具</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/YiouTitle` | —（原无此 key） | <code>欧帝白板 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AiClassTitle` | —（原无此 key） | <code>AiClass 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ClassInXTitle` | —（原无此 key） | <code>ClassIn X 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntelligentClassTitle` | —（原无此 key） | <code>天喻教育云互动课堂 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ChangYan4Title` | —（原无此 key） | <code>畅言智慧课堂4.0 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ChangYan5Title` | —（原无此 key） | <code>畅言智慧课堂5.0 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Iclass30SidebarTitle` | —（原无此 key） | <code>C30智能教学 侧栏悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Iclass30DrawingTitle` | —（原无此 key） | <code>C30智能教学 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopSidebarTitle` | —（原无此 key） | <code>希沃桌面 侧栏悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopDrawingTitle` | —（原无此 key） | <code>希沃桌面 桌面画笔悬浮窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IncludesPptNote` | —（原无此 key） | <code>包括PPT控件。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/RestoresWhiteboardNote` | —（原无此 key） | <code>包括PPT控件。支持在白板时自动恢复。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/WindowTrackingNote` | —（原无此 key） | <code>包括PPT控件。支持在白板时自动恢复，支持窗口追踪。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopVersionsNote` | —（原无此 key） | <code>1.0/2.0/2.5/3.0 普教版/高教版 通用</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorNote` | —（原无此 key） | <code>需要DDB管理员权限。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/Component/SoftwareSection` | —（原无此 key） | <code>软件</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SystemSection` | —（原无此 key） | <code>系统</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/KeyboardSection` | —（原无此 key） | <code>键盘模拟</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/RollCallSection` | —（原无此 key） | <code>点名器</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSection` | —（原无此 key） | <code>ClassIsland 联动</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ExplorerTitle` | —（原无此 key） | <code>启动 文件资源管理器</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/TaskManagerTitle` | —（原无此 key） | <code>启动 任务管理器</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ControlPanelTitle` | —（原无此 key） | <code>启动 控制面板</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ShowDesktopTitle` | —（原无此 key） | <code>显示 桌面</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/LockScreenTitle` | —（原无此 key） | <code>锁屏</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/EscapeTitle` | —（原无此 key） | <code>按下 ESC</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/AltF4Title` | —（原无此 key） | <code>按下 Alt+F4</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCaller1Title` | —（原无此 key） | <code>IslandCaller 1 随机点名</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCaller2Title` | —（原无此 key） | <code>IslandCaller 2 单人点名</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom1Title` | —（原无此 key） | <code>SecRandom 1 闪抽</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom2Title` | —（原无此 key） | <code>SecRandom 2 闪抽</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom2CompatTitle` | —（原无此 key） | <code>SecRandom 2 闪抽（兼容）</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/NamePickerTitle` | —（原无此 key） | <code>NamePicker 随机点名</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSettingsTitle` | —（原无此 key） | <code>ClassIsland 应用设置</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandProfileTitle` | —（原无此 key） | <code>ClassIsland 档案编辑</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSwapTitle` | —（原无此 key） | <code>ClassIsland 快速换课</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCallerRequirement` | —（原无此 key） | <code>需要安装 ClassIsland 插件 IslandCaller，并需要 ClassIsland 注册 Url 协议。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandomUrlRequirement` | —（原无此 key） | <code>需要 SecRandom 注册 Url 协议。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandomIpcRequirement` | —（原无此 key） | <code>需要 SecRandom 注册 Ipc 协议。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/NamePickerRequirement` | —（原无此 key） | <code>需要 NamePicker 注册 Url 协议。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandProtocolNote` | —（原无此 key） | <code>需要在 ClassIsland 应用设置 中注册 Url 导航协议。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSwapNote` | —（原无此 key） | <code>在 ClassIsland 当前没有加载课表时，此组件不起作用。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 修改 | `SettingsUI/HotKey/E` | <code>当前仅提供以下固定组合，可分别开启或关闭，暂不支持自定义。\n除全局往返外，其余快捷键仅在批注状态有效；设置文本输入和模态对话框优先。选择或穿透状态下交由当前应用处理。\n全局往返在设置普通编辑期间仍生效；应用模态对话框期间暂停动作，但仍保留该组合。国际键盘的 AltGr 可能与 Ctrl + Alt 冲突，可单独关闭全局往返。\n键盘快捷键不接管 PPT 翻页。</code> | <code>当前仅提供以下固定组合，可分别开启或关闭，暂不支持自定义。\n除全局批注切换外，其余快捷键仅在批注状态有效；设置文本输入和模态对话框优先。选择或穿透状态下交由当前应用处理。\n全局批注切换在设置普通编辑期间仍生效；应用模态对话框期间暂停动作，但仍保留该组合。国际键盘的 AltGr 可能与 Ctrl + Alt 冲突，可单独关闭全局批注切换。\n键盘快捷键不接管 PPT 翻页。</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/ToggleAnnotationN` | <code>全局往返</code> | <code>全局批注切换</code> | 明确该快捷键切换批注状态。 |
| 修改 | `SettingsUI/HotKey/Registering` | <code>正在注册全局往返</code> | <code>正在注册全局批注切换</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/Disabling` | <code>正在关闭全局往返</code> | <code>正在关闭全局批注切换</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/UnregisterFailed` | <code>全局往返注销失败，该组合仍被保留</code> | <code>全局批注切换注销失败，该组合仍被保留</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/Registered` | <code>全局往返已生效</code> | <code>全局批注切换已生效</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/Disabled` | <code>全局往返已关闭</code> | <code>全局批注切换已关闭</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/RegisterFailed` | <code>全局往返注册失败，组合可能被其它程序占用</code> | <code>全局批注切换注册失败，组合可能被其它程序占用</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/SynchronizationPending` | <code>正在同步按键状态；请在普通权限窗口中松开按键后重试。</code> | <code>正在同步按键状态；请切换到未提升权限的窗口，松开按键后重试。</code> | 明确“普通权限”指未提升权限窗口。 |
| 新增 | `SettingsUI/Sponsor/Thanks` | —（原无此 key） | <code>成功赞助后，可以联系作者将您的昵称和赞助的金额添加到社区名片中以表示感谢。</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/RenderingSection` | —（原无此 key） | <code>UI3 渲染调试</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DirtyRegions/N` | —（原无此 key） | <code>脏区调试</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DirtyRegions/E` | —（原无此 key） | <code>显示 UI3 每帧实际提交的脏区边界。</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/FrameRate/N` | —（原无此 key） | <code>显示帧率</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/FrameRate/E` | —（原无此 key） | <code>每秒更新上一秒平均帧率和无等待帧率。</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/ConsoleSection` | —（原无此 key） | <code>控制台输出</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DisplayInformation` | —（原无此 key） | <code>显示器与 EDID</code> | 将活动显示器与EDID调试信息标题接入国际化。 |
| 修改 | `SettingsUI/Update/Newer` | <code>软件相对最新版本更新</code> | <code>当前版本高于官方最新发布版本</code> | 自然表达当前版本比官方最新发布版本更新的状态。 |
## en-US

共 163 项变化。

| 类型 | Key | 修改前 | 修改后 | 原因 |
|---|---|---|---|---|
| 修改 | `Widget/LnkName` | <code>智绘教Inkeys (Screen Annotation Tool)</code> | <code>Inkeys (Screen Annotation Tool)</code> | 按用户确认的通用品牌及新建快捷方式命名规则，保留本地化用途说明。 |
| 修改 | `UI/Centre/SelectClean` | <code>Select(Del)</code> | <code>Select (Clear)</code> | 修正把“清空”误写成Delete键提示的歧义。 |
| 修改 | `UI/Pen/Highlighter` | <code>Hlight</code> | <code>Highlighter</code> | 使用规范工具名称，替换难懂缩写；不恢复旧UI路径。 |
| 修改 | `UI/Operate/Freeze` | <code>Hold</code> | <code>Freeze</code> | 将同一个定格操作的Hold、Lock和Freeze统一为Freeze。 |
| 修改 | `UI/Operate/Pierce` | <code>Pass</code> | <code>Click-through</code> | 统一穿透术语，避免Pass/penetrate等含糊表达。 |
| 修改 | `UI/Bar/MainButtons/ResumeWhiteboardLabel` | <code>Resume</code> | <code>To board</code> | 区分返回白板与返回放映，用短标签适配固定主栏文字槽。 |
| 修改 | `UI/Bar/MainButtons/ReturnToShowLabel` | <code>Resume</code> | <code>To show</code> | 区分返回白板与返回放映，用短标签适配固定主栏文字槽。 |
| 修改 | `UI/Bar/MainButtons/FreezeLabel` | <code>Lock</code> | <code>Freeze</code> | 将同一个定格操作的Hold、Lock和Freeze统一为Freeze。 |
| 修改 | `UI/Bar/MainButtons/EndPresentationLabel` | <code>End Show</code> | <code>End show</code> | 保留清楚且紧凑的结束放映短标签，避免长译文挤占固定按钮槽。 |
| 新增 | `UI/Bar/Components/ExplorerLabel` | —（原无此 key） | <code>Files</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/TaskManagerLabel` | —（原无此 key） | <code>Tasks</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ControlPanelLabel` | —（原无此 key） | <code>Control</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ShowDesktopLabel` | —（原无此 key） | <code>Desktop</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/LockScreenLabel` | —（原无此 key） | <code>Lock</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/EscapeLabel` | —（原无此 key） | <code>Esc</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/RollCallLabel` | —（原无此 key） | <code>Pick name</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandSettingsLabel` | —（原无此 key） | <code>CI settings</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandProfileLabel` | —（原无此 key） | <code>Profiles</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandSwapLabel` | —（原无此 key） | <code>Swap class</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 修改 | `UI/Bar/DrawAttributes/AnnotationLineLabel` | <code>Annotation</code> | <code>Annot. line</code> | 明确是标注线而非泛指批注，并根据实际80 DIP菜单槽测量采用Annot. line短标签。 |
| 修改 | `UI/Bar/DrawAttributes/HoldThicknessLabel` | <code>Hold to lock size</code> | <code>Hold to lock width</code> | 按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 修改 | `UI/Bar/Diagnostics/FrameRateFormat` | <code>Frame rate: {:.2f} FPS &#124; Unlimited: {:.2f} FPS</code> | <code>FPS: {:.2f} &#124; Uncapped: {:.2f}</code> | 统一帧率与无限制帧率指标术语，采用紧凑文案；保留真实format参数。 |
| 修改 | `UI/Bar/Diagnostics/FrameRateUnavailable` | <code>Frame rate: -- FPS &#124; Unlimited: -- FPS</code> | <code>FPS: -- &#124; Uncapped: --</code> | 无数值状态与有数值诊断使用相同指标术语和紧凑布局。 |
| 修改 | `UI/Bar/CanvasRecovery/Conflict` | <code>Bind</code> | <code>Conflict</code> | 修正将冲突错误译成绑定的状态文字。 |
| 修改 | `UI/Bar/CanvasRecovery/PresentFailed` | <code>View</code> | <code>Display</code> | 明确为显示阶段状态，而非查看动作。 |
| 修改 | `UI/Bar/CanvasRecovery/Recovering` | <code>Busy</code> | <code>Recovering</code> | 明确恢复中的动作状态，替换泛化的Busy。 |
| 修改 | `UI/Bar/Errors/TransparentBackgroundWarning` | <code>Inkeys cannot display the transparent background correctly.\n\nExecutable properties -&gt; disable simplified color mode\nWindows 7 users: enable the Aero theme\n\nContact the developer from the Options home page.\nRestart Inkeys and try again.</code> | <code>Inkeys cannot display its transparent background correctly.\n\nRight-click Inkeys.exe, open Properties → Compatibility, and clear Reduced color mode.\nOn Windows 7, enable an Aero theme.\n\nRestart Inkeys and try again.\nIf the problem persists, contact the developer from the Settings home page.</code> | 提供完整可执行的故障处理路径，修正系统选项名称；保留原有分段换行数。 |
| 修改 | `SettingsUI/N` | <code>智绘教Inkeys Options</code> | <code>Inkeys Options</code> | 按用户确认的通用品牌规则统一非简中产品名；主页欢迎语例外。 |
| 修改 | `SettingsUI/Design/HomeRedistribution` | <code>Anyone can view, modify, and distribute this code.</code> | <code>Under the terms of GPLv3, anyone may inspect, modify, and redistribute this project’s code.</code> | 在既有开源许可上下文中明确分发条件，不改版权署名。 |
| 修改 | `SettingsUI/Design/TopWindowSummary` | <code>How often the window stays on top while not drawing.</code> | <code>How often Inkeys brings its window back to the top when not drawing.</code> | 明确周期性重新置顶，而非保持在顶层的时长。 |
| 修改 | `SettingsUI/Home/Developer` | <code>智绘教Inkeys Developer</code> | <code>Inkeys Developer</code> | 按已确认规则统一通用品牌，保留现有开发者身份称谓。 |
| 修改 | `SettingsUI/Language/UI/Language/en-US` | <code>English(Beta)</code> | <code>English &#183; en-US &#183; Beta</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Language/UI/Language/zh-CN` | <code>简体中文</code> | <code>简体中文 &#183; zh-CN</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Language/UI/Language/zh-TW` | <code>繁體中文(Beta)</code> | <code>繁體中文（台灣） &#183; zh-TW &#183; Beta</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Configuration/N` | <code>Config &amp; Save</code> | <code>Configuration and storage</code> | 栏目同时包含配置清理和画布保存，明确并列关系。 |
| 修改 | `SettingsUI/Configuration/CanvasSave/N` | <code>Canvas History</code> | <code>Canvas saving</code> | 统一台湾地区的儲存用语。 |
| 修改 | `SettingsUI/Configuration/CanvasSave/SaveTime/N` | <code>Keep duration</code> | <code>History retention period</code> | 该设置控制历史画布保留期限，而非保存操作持续时间。 |
| 修改 | `SettingsUI/Configuration/CanvasSave/SaveTime/E` | <code>Delete expired canvases on startup if they exceed the set duration.</code> | <code>On startup, delete saved canvases older than the selected retention period.</code> | 明确按已保存画布的年龄与保留期限执行清理。 |
| 修改 | `SettingsUI/Version/VersionTip` | <code>Incompatible software and system architecture may cause performance issues. It&#39;s recommended to repair the software in the &#39;Software Repair&#39; section below (select &#39;Fix architecture&#39;), or select the target architecture below and repair.</code> | <code>The application architecture does not match your system, which may reduce performance. In Software Repair below, enable “Fix architecture during repair”, or select the target architecture manually before repairing.</code> | 删除重复拗口表达，保留实际修复选项及操作路径。 |
| 修改 | `SettingsUI/Version/Info/ReleaseArch` | <code>Software Architecture &#124; System Architecture:</code> | <code>Application and system architectures</code> | 消费者另行拼接架构值及分隔符，标签不重复添加竖线或冒号。 |
| 修改 | `SettingsUI/Version/Update/ChannelTip` | <code>Stable Channel (LTS): most stable version (recommended)\nInsider Channel: preview versions, may be falsely reported as viruses.\nCanary Channel: pushes an early version of Inkeys3, which is not recommended for use.</code> | <code>Stable (LTS): latest stable release, recommended for everyday use.\nInsider: preview releases; stability may be lower and security software may raise alerts.\nCanary: early Inkeys 3 builds, not recommended for everyday use.</code> | 统一三通道含义，避免绝对稳定性或把安全告警笼统定性为误报。 |
| 修改 | `SettingsUI/Version/Update/Arch/E` | <code>Warning: Architecture mismatches may cause the software to fail or run poorly.</code> | <code>Warning: A target architecture that does not match your system may prevent Inkeys from launching or cause severe performance issues.</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Regular/StartUp/AutoStartE` | <code>Launch 智绘教Inkeys when the user logs in.</code> | <code>Launch Inkeys when the current user signs in to Windows.</code> | 启动项由当前用户登录触发，并按已确认规则统一品牌。 |
| 修改 | `SettingsUI/Regular/StartUp/Link/E` | <code>Auto-correct name and create shortcut, click &#39;More Options&#39; for details.</code> | <code>Fix existing shortcut names or create a new shortcut. Select More options to configure this.</code> | 补足操作对象与配置入口，修正文句连接。 |
| 修改 | `SettingsUI/Regular/Appearance/Theme/E` | <code>Recommended skin will use the official setting automatically.</code> | <code>Select Recommended skin to automatically apply the current officially recommended theme.</code> | 消除循环定义，说明推荐皮肤的实际效果。 |
| 修改 | `SettingsUI/Regular/Appearance/Theme/Skip4` | <code>Year of Horse</code> | <code>Year of the Horse</code> | 补足英语固定表达所需冠词。 |
| 修改 | `SettingsUI/Regular/Appearance/TaskbarScaleBase/N` | <code>Taskbar scale base</code> | <code>Taskbar-based scaling</code> | 用自然英语表述任务栏缩放设置，替换机械名词堆砌。 |
| 修改 | `SettingsUI/Regular/Appearance/TaskbarScaleBase/E` | <code>At 100%: base is 48 DIP; actual follows taskbar size.</code> | <code>At 100% scale, Taskbar base size fixes the main bar height at 48 DIP; Actual taskbar size follows the taskbar thickness.</code> | 区分现有Base固定48DIP与Actual随任务栏厚度的两种模式，避免混淆。 |
| 修改 | `SettingsUI/Regular/Behavior/TopWindow/E` | <code>How often to keep window on top in non-draw mode. Shorter=more responsive but higher CPU use. Use &#39;Super Top&#39; plugin if interference persists.</code> | <code>When not drawing, Inkeys reasserts its topmost position at the selected interval; this pauses when you put the pen down. Shorter intervals can reduce interference from other windows but may increase CPU usage. If needed, try the SuperTop plugin.</code> | 删除重复和错字，说明周期性重新置顶、落笔暂停及资源权衡。 |
| 修改 | `SettingsUI/Regular/Behavior/TopWindow/1s` | <code>Medium-Short (1s)</code> | <code>Somewhat short (1 s)</code> | 用自然英语表达相对间隔级别。 |
| 修改 | `SettingsUI/Regular/Behavior/TopWindow/5s` | <code>Medium-Long (5s)</code> | <code>Somewhat long (5 s)</code> | 用自然英语表达相对间隔级别。 |
| 修改 | `SettingsUI/Regular/Behavior/RightClickClose` | <code>Close on right-click icon</code> | <code>Right-click the main icon to exit Inkeys</code> | 明确右键点击的是主图标，不是任意图标。 |
| 修改 | `SettingsUI/Regular/Behavior/DrawingRetract` | <code>Hide main bar while drawing</code> | <code>Collapse the main toolbar while drawing</code> | 统一收起与隐藏的区别；兼容资源保留，不恢复退休设置。 |
| 修改 | `SettingsUI/Regular/Behavior/ErasingRetract` | <code>Hide main bar when erasing</code> | <code>Collapse the main toolbar while erasing</code> | 统一收起与隐藏的区别；兼容资源保留，不恢复退休设置。 |
| 修改 | `SettingsUI/Regular/Behavior/DraggingRetract` | <code>Hide main bar when dragging</code> | <code>Collapse the main toolbar while dragging it</code> | 统一收起与隐藏的区别；兼容资源保留，不恢复退休设置。 |
| 修改 | `SettingsUI/Regular/Behavior/CheckingRetract` | <code>Hide main bar on click</code> | <code>Collapse when clicking outside the main toolbar</code> | 补充点击区域；兼容资源保留，不恢复退休设置。 |
| 修改 | `SettingsUI/Regular/Behavior/CheckingRetractE` | <code>Only in select/penetrate mode, works on non-mainbar areas.</code> | <code>Only applies when clicking outside the main toolbar in Select or Click-through mode.</code> | 使用真实工具名称，明确生效模式与区域。 |
| 修改 | `SettingsUI/Regular/Tentative/SafeMode/N` | <code>Crash Safe Mode</code> | <code>Crash handling</code> | 该组选项控制崩溃后动作，并非独立安全启动模式。 |
| 修改 | `SettingsUI/Draw/Cursor/Description` | <code>The mouse option applies only to soft and hard pens. Highlighters always preview width; shapes use the system cursor. The default pen cursor is circular. Screen pens have no highlighted edges; pen tablets and unrecognized pens have highlighted edges.</code> | <code>The mouse option applies only to soft and hard pens. Highlighters always preview width; shapes use the system cursor. The default pen cursor is circular. Cursor edge highlighting is disabled for screen styluses and enabled for pen tablets and unrecognized pens.</code> | 说明高亮的是相应设备的光标边缘，避免被理解为设备自身边缘。 |
| 修改 | `SettingsUI/Draw/Eraser/PalmEraser/Introduction` | <code>Place your palm on the screen to erase without switching tools. Its size is determined during the recognition window; lifting your palm keeps your original tool selected.</code> | <code>Place your palm on the screen to erase without switching tools. The eraser size is determined during the recognition window after contact; lifting your palm keeps your original tool selected.</code> | 按实际Touch面积识别机制表述，避免暗示另需物理手写笔先落下。 |
| 修改 | `SettingsUI/Draw/Eraser/PalmEraser/PreviewDescription` | <code>The outlined area shows the current drawing screen&#39;s pixel threshold. Select a ratio to change its shape. Its physical size may differ on another screen.</code> | <code>The outlined area shows the current drawing screen’s pixel threshold. Switch the preview ratio to view different proportions. Its physical size may differ on another screen.</code> | 比例只改变页面预览，不改变实际阈值或橡皮形状。 |
| 修改 | `SettingsUI/Draw/Eraser/ResponseFor` | <code>Speed response device</code> | <code>Speed sensitivity applies to</code> | 将内部变量式名称改为适用设备的自然提示。 |
| 新增 | `SettingsUI/Experimental/N` | —（原无此 key） | <code>Labs</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 修改 | `SettingsUI/Experimental/ConsoleOutput/Haptics/N` | <code>Test pen haptic feedback</code> | <code>Pen haptic feedback diagnostics</code> | 这是诊断输出开关，而非发起一次触觉测试的动作。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Sync` | <code>Sync ADJ</code> | <code>Sync adjustments</code> | 去除没有解释的ADJ缩写，明确同步调整。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tip` | <code>To use the plugin, please ensure that the permissions for the Inkeys application and the PPT application are the same for proper detection. If issues persist, please refer to the solution.</code> | <code>To use the plugin, run Inkeys and PowerPoint/WPS with the same privilege level so the slideshow process can be detected. If problems persist, see the solution.</code> | 统一品牌并明确PPT联动的权限匹配条件。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Error` | <code>An error occurred during plugin loading. This may be because .NET Framework 4.x is not installed or an exception occurred in the global COM environment.\nCode␠</code> | <code>The plugin could not load. .NET Framework 4.x may be missing, or the COM environment may be unavailable.\nDetails:␠</code> | 消费者拼入完整Error:诊断信息而非纯数字，改为自然分隔前缀。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/BottomBoth` | <code>Show bottom-side widgets</code> | <code>Show controls at both bottom corners</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/MiddleBoth` | <code>Show middle-side widgets</code> | <code>Show controls on both sides at mid-height</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/BottomMiddle` | <code>Show bottom main bar widget</code> | <code>Show controls on the bottom toolbar</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/Page/BottomSideBoth` | <code>Bottom-side widgets scale</code> | <code>Bottom corner control scale</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/Page/MiddleSideBoth` | <code>Middle-side widgets scale</code> | <code>Mid-side control scale</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/State/BottomSideMiddle` | <code>Bottom main bar widget scale</code> | <code>Bottom toolbar control scale</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/CloseWppE` | <code>Requires a restart to take effect. After exiting a presentation in KingSoft WPS, the original WPP process sometimes doesn&#39;t close properly and remains stuck. This prevents the app from detecting new presentations and enabling integration. Enabling this option helps terminate these stuck processes. It is safe for your files and is recommended.</code> | <code>Requires a restart. Attempts to close WPP processes left behind after a WPS slideshow exits, to improve detection of the next slideshow.</code> | 删除未经核验的“不会影响文件”绝对保证；保留兼容文案，不恢复旧功能。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/AutoTakeOverOnceE` | <code>Avoid interfering when PPT native annotations are intentionally needed.</code> | <code>Avoid repeatedly taking over when PPT native annotations are intentionally needed.</code> | 补足反复接管的动作对象，修复残缺句。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/EnablePageButtonLongPress` | <code>Enable long-press page turning buttons</code> | <code>Enable page turning by holding the navigation buttons</code> | 明确启用的是长按翻页动作，而不是另一类按钮。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/SupportNote` | <code>Supports Microsoft PowerPoint 2007, Kingsoft WPS 2013, and later versions.</code> | <code>Supports Microsoft PowerPoint 2007 or later and Kingsoft WPS 2013 or later.</code> | 澄清两类软件各自最低版本的语法，不扩大既有支持范围。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/E` | <code>Use UiAccess to display Inkeys above other applications and system elements such as the Taskbar, Alt+Tab switcher, on-screen keyboard, and lock screen.</code> | <code>Use UIAccess to display the window above supported system UI and application windows.</code> | 去掉未经代码证实的锁屏显示保证，保留已有功能描述。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/Warn` | <code>The SuperTop feature is in BETA and may be unstable.\nThe application restarts itself upon launch to request elevated privileges, then launches a new instance.\nThis process may fail, resulting in an incorrect launch with administrator rights, which can interfere with PPT integration.\nA UAC (User Account Control) prompt will appear; please click &#39;Yes&#39; or disable UAC prompts (not recommended).\nIt might be flagged by antivirus software; this is a false positive and is normal.</code> | <code>The SuperTop plugin is in Beta and may be unstable.\nAt startup, Inkeys restarts itself to request elevated privileges, then launches a new instance.\nThis process may fail; running with administrator privileges can affect PPT integration.\nElevation may trigger a UAC prompt. Verify the source and publisher before allowing it.\nIf security software raises an alert, check the file source and signature instead of disabling protection.</code> | 准确说明权限提升与告警，删除关闭UAC的指引和把所有告警当作误报的保证。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/Capability/SuperTop` | <code>Enable Super SuperTop</code> | <code>Enable SuperTop</code> | 删除重复Super并统一功能名称。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/Capability/SuperTopE1` | <code>The application uses a three-stage launch to keep its windows on top.\n(Currently successful)</code> | <code>The application uses a three-stage launch to keep its windows on top.\n(SuperTop is currently active)</code> | 当前状态是持续生效与否，不应把关闭状态描述为执行失败。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/Capability/SuperTopE2` | <code>The application uses a three-stage launch to keep its windows on top.\n(Currently failed)</code> | <code>The application uses a three-stage launch to keep its windows on top.\n(SuperTop is not active)</code> | 当前状态是持续生效与否，不应把关闭状态描述为执行失败。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/LicenseTitle` | —（原无此 key） | <code>GPLv3 open-source plugin</code> | 将既有DDB许可说明接入国际化，保留原有用途限定、GPLv3发布、无质量保证及使用后果声明。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/LicenseDescription` | —（原无此 key） | <code>This plugin is for learning, discussion, and research only; it must not be used for any other purpose.\nDesktopDrawpadBlocker is open-source software released under the GPLv3 license.\nPublished on GitHub: https://github.com/Alan-CRL/DesktopDrawpadBlocker\nWe hope this program is useful, but provide no warranty of quality.\nUsers are responsible for the effects and consequences of using this plugin; use constitutes acceptance of this agreement.</code> | 将既有DDB许可说明接入国际化，保留原有用途限定、GPLv3发布、无质量保证及使用后果声明。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/UseSection` | —（原无此 key） | <code>Using the plugin</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableTitle` | —（原无此 key） | <code>Enable plugin</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableDescription` | —（原无此 key） | <code>In the default mode, the plugin starts and stops with Inkeys.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/GeneralSection` | —（原无此 key） | <code>General options</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorTitle` | —（原无此 key） | <code>Run the plugin as administrator</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorDescription` | —（原无此 key） | <code>Wait for the plugin to respond when changing this setting.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalTitle` | —（原无此 key） | <code>Blocking interval</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalDescription` | —（原无此 key） | <code>Shorter intervals block newly appearing windows sooner, but use more CPU resources.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalShort` | —（原无此 key） | <code>Short (500 ms)</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMediumShort` | —（原无此 key） | <code>Somewhat short (1 s)</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMedium` | —（原无此 key） | <code>Medium (3 s)</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMediumLong` | —（原无此 key） | <code>Somewhat long (5 s)</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalLong` | —（原无此 key） | <code>Long (10 s)</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PrecisionSection` | —（原无此 key） | <code>Detailed control</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo3Title` | —（原无此 key） | <code>希沃白板3 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo5Title` | —（原无此 key） | <code>希沃白板5 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo5CTitle` | —（原无此 key） | <code>希沃轻白板（5C） — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PincoSidebarTitle` | —（原无此 key） | <code>希沃品课教师端 — floating sidebar</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PincoDrawingTitle` | —（原无此 key） | <code>希沃品课教师端 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoPptTitle` | —（原无此 key） | <code>希沃PPT小工具</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IwbAssistantTitle` | —（原无此 key） | <code>希沃课堂助手 — PPT tools</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/YiouTitle` | —（原无此 key） | <code>欧帝白板 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AiClassTitle` | —（原无此 key） | <code>AiClass — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ClassInXTitle` | —（原无此 key） | <code>ClassIn X — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntelligentClassTitle` | —（原无此 key） | <code>天喻教育云互动课堂 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ChangYan4Title` | —（原无此 key） | <code>畅言智慧课堂4.0 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ChangYan5Title` | —（原无此 key） | <code>畅言智慧课堂5.0 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Iclass30SidebarTitle` | —（原无此 key） | <code>C30智能教学 — floating sidebar</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Iclass30DrawingTitle` | —（原无此 key） | <code>C30智能教学 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopSidebarTitle` | —（原无此 key） | <code>希沃桌面 — floating sidebar</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopDrawingTitle` | —（原无此 key） | <code>希沃桌面 — desktop pen window</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IncludesPptNote` | —（原无此 key） | <code>Includes PPT controls.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/RestoresWhiteboardNote` | —（原无此 key） | <code>Includes PPT controls. Supports automatic restoration in whiteboard mode.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/WindowTrackingNote` | —（原无此 key） | <code>Includes PPT controls. Supports automatic restoration in whiteboard mode and window tracking.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopVersionsNote` | —（原无此 key） | <code>Works with versions 1.0/2.0/2.5/3.0, for both general and higher education.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorNote` | —（原无此 key） | <code>Requires DDB administrator privileges.</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 修改 | `SettingsUI/PlugIn/LnkHelper/E` | <code>Ensures that existing 智绘教Inkeys shortcuts remain valid after application updates, which can change the program&#39;s file name. Optionally, it can also create a new desktop shortcut if one does not already exist.</code> | <code>Ensures that existing Inkeys shortcuts remain valid after application updates, which can change the program’s file name. Optionally creates a desktop shortcut if one does not already exist.</code> | 按用户确认的通用品牌规则统一非简中名称，并精炼快捷方式保障说明。 |
| 新增 | `SettingsUI/Component/SoftwareSection` | —（原无此 key） | <code>Applications</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SystemSection` | —（原无此 key） | <code>System</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/KeyboardSection` | —（原无此 key） | <code>Keyboard simulation</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/RollCallSection` | —（原无此 key） | <code>Name pickers</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSection` | —（原无此 key） | <code>ClassIsland integration</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ExplorerTitle` | —（原无此 key） | <code>Open File Explorer</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/TaskManagerTitle` | —（原无此 key） | <code>Open Task Manager</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ControlPanelTitle` | —（原无此 key） | <code>Open Control Panel</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ShowDesktopTitle` | —（原无此 key） | <code>Show desktop</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/LockScreenTitle` | —（原无此 key） | <code>Lock screen</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/EscapeTitle` | —（原无此 key） | <code>Press Esc</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/AltF4Title` | —（原无此 key） | <code>Press Alt+F4</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCaller1Title` | —（原无此 key） | <code>IslandCaller 1: pick a random name</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCaller2Title` | —（原无此 key） | <code>IslandCaller 2: pick one name</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom1Title` | —（原无此 key） | <code>SecRandom 1: quick pick</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom2Title` | —（原无此 key） | <code>SecRandom 2: quick pick</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom2CompatTitle` | —（原无此 key） | <code>SecRandom 2: quick pick (compatible)</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/NamePickerTitle` | —（原无此 key） | <code>NamePicker: random name</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSettingsTitle` | —（原无此 key） | <code>ClassIsland settings</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandProfileTitle` | —（原无此 key） | <code>ClassIsland profile editor</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSwapTitle` | —（原无此 key） | <code>ClassIsland quick class swap</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCallerRequirement` | —（原无此 key） | <code>Requires the IslandCaller plugin in ClassIsland and registration of the ClassIsland URL protocol.</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandomUrlRequirement` | —（原无此 key） | <code>Requires registration of the SecRandom URL protocol.</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandomIpcRequirement` | —（原无此 key） | <code>Requires registration of the SecRandom IPC protocol.</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/NamePickerRequirement` | —（原无此 key） | <code>Requires registration of the NamePicker URL protocol.</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandProtocolNote` | —（原无此 key） | <code>Register the URL navigation protocol in ClassIsland settings.</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSwapNote` | —（原无此 key） | <code>This component has no effect when ClassIsland has no timetable loaded.</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 修改 | `SettingsUI/HotKey/FreezeE` | <code>Toggle the frozen screen.</code> | <code>Toggle screen freeze.</code> | 自然描述切换屏幕定格状态。 |
| 修改 | `SettingsUI/HotKey/SynchronizationPending` | <code>Synchronizing key state. Release keys in a normal-permission window, then retry.</code> | <code>Synchronizing key state. Switch to a non-elevated window, release the keys, then retry.</code> | 明确“普通权限”指未提升权限窗口。 |
| 新增 | `SettingsUI/Sponsor/Thanks` | —（原无此 key） | <code>After sponsoring, contact the author to have your nickname and contribution added to the community card as a thank-you.</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 修改 | `SettingsUI/DebugSoftware/N` | <code>[Debug]</code> | <code>Debug tools</code> | 明确该页为调试工具，避免仅用无动作对象的Debug徽记。 |
| 新增 | `SettingsUI/DebugSoftware/RenderingSection` | —（原无此 key） | <code>UI3 rendering diagnostics</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DirtyRegions/N` | —（原无此 key） | <code>Dirty region diagnostics</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DirtyRegions/E` | —（原无此 key） | <code>Show the boundaries of dirty regions actually submitted in each UI3 frame.</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/FrameRate/N` | —（原无此 key） | <code>Show frame rate</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/FrameRate/E` | —（原无此 key） | <code>Update the previous second’s average and uncapped frame rates once per second.</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/ConsoleSection` | —（原无此 key） | <code>Console output</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DisplayInformation` | —（原无此 key） | <code>Displays and EDID</code> | 将活动显示器与EDID调试信息标题接入国际化。 |
| 修改 | `SettingsUI/Update/Check` | <code>Check</code> | <code>Check for updates</code> | 明确检查对象是更新，避免孤立Check按钮歧义。 |
| 修改 | `SettingsUI/Update/InformationDamage` | <code>Downloaded info corrupted</code> | <code>Downloaded update information is corrupted</code> | 明确损坏的是已下载更新信息，补足自然句法。 |
| 修改 | `SettingsUI/Update/DownloadDamage` | <code>File damaged</code> | <code>Downloaded update file is corrupted</code> | 明确损坏的是已下载更新文件。 |
| 修改 | `SettingsUI/Update/Newer` | <code>Newer than official latest</code> | <code>Current version is newer than the latest official release</code> | 自然表达当前版本比官方最新发布版本更新的状态。 |
## zh-TW

共 162 项变化。

| 类型 | Key | 修改前 | 修改后 | 原因 |
|---|---|---|---|---|
| 修改 | `Widget/LnkName` | <code>智繪教(螢幕批註工具)</code> | <code>Inkeys（螢幕批註工具）</code> | 按用户确认的通用品牌及新建快捷方式命名规则，保留本地化用途说明。 |
| 修改 | `UI/Operate/Revoke` | <code>撤回</code> | <code>復原</code> | 同一Undo操作统一使用台湾界面常用的復原。 |
| 修改 | `UI/Operate/Recall` | <code>超級回復</code> | <code>超級復原</code> | 恢复操作统一使用復原，避免回復的用字歧义；不改变兼容恢复行为。 |
| 新增 | `UI/Bar/Components/ExplorerLabel` | —（原无此 key） | <code>檔案總管</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/TaskManagerLabel` | —（原无此 key） | <code>工作管理員</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ControlPanelLabel` | —（原无此 key） | <code>控制台</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ShowDesktopLabel` | —（原无此 key） | <code>顯示桌面</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/LockScreenLabel` | —（原无此 key） | <code>鎖定畫面</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/EscapeLabel` | —（原无此 key） | <code>Esc鍵</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/RollCallLabel` | —（原无此 key） | <code>隨機點名</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandSettingsLabel` | —（原无此 key） | <code>CI設定</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandProfileLabel` | —（原无此 key） | <code>設定檔</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 新增 | `UI/Bar/Components/ClassIslandSwapLabel` | —（原无此 key） | <code>快速換課</code> | 将活动组件主栏按钮的短标签接入国际化，独立于设置中的完整名称适配固定文字槽。 |
| 修改 | `UI/Bar/DrawAttributes/HoldThicknessLabel` | <code>按住以固定粗細</code> | <code>按住以鎖定粗細</code> | 按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 修改 | `UI/Bar/ColorPicker/HoldColorLabel` | <code>按住以固定顏色</code> | <code>按住以鎖定顏色</code> | 按实际按住静止后锁定的手势描述操作，避免误读为持久开关。 |
| 修改 | `UI/Bar/Diagnostics/FrameRateFormat` | <code>幀率: {:.2f} FPS &#124; 無限制幀率: {:.2f} FPS</code> | <code>影格率: {:.2f} FPS &#124; 無上限: {:.2f} FPS</code> | 统一帧率与无限制帧率指标术语，采用紧凑文案；保留真实format参数。 |
| 修改 | `UI/Bar/Diagnostics/FrameRateUnavailable` | <code>幀率: -- FPS &#124; 無限制幀率: -- FPS</code> | <code>影格率: -- FPS &#124; 無上限: -- FPS</code> | 无数值状态与有数值诊断使用相同指标术语和紧凑布局。 |
| 修改 | `UI/Bar/Errors/TransparentBackgroundWarning` | <code>軟體遇到透明背景無法正常顯示的問題\n\nexe 內容-&gt;停用「使用簡化的色彩模式」\nWindows 7 使用者請啟用 Aero 主題\n\n聯絡開發者-&gt;軟體選項首頁\n重新啟動軟體後再試</code> | <code>Inkeys 無法正常顯示透明背景。\n\n請在 Inkeys.exe 上按右鍵，開啟「內容 → 相容性」，取消勾選「簡化的色彩模式」。\nWindows 7 使用者請啟用 Aero 主題。\n\n重新啟動 Inkeys 後再試。\n如果問題仍然存在，請透過設定首頁聯絡開發者。</code> | 提供完整可执行的故障处理路径，修正系统选项名称；保留原有分段换行数。 |
| 修改 | `Dialogs/WorkspaceTransitionFailure/Whiteboard` | <code>白板視窗切換未完成，已停止自動重試。可再次點擊主列的白板按鈕取消本次切換，或前往設定的繪圖頁重試。</code> | <code>白板視窗切換未完成，已停止自動重試。可再次點擊主欄的白板按鈕取消本次切換，或前往設定的繪圖頁重試。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `Dialogs/WorkspaceTransitionFailure/Return` | <code>退出白板未完成，已停止自動重試。可再次點擊主列的白板按鈕返回白板，或前往設定的繪圖頁重試。</code> | <code>退出白板未完成，已停止自動重試。可再次點擊主欄的白板按鈕返回白板，或前往設定的繪圖頁重試。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/N` | <code>智绘教Inkeys 選項</code> | <code>Inkeys 選項</code> | 按用户确认的通用品牌规则统一非简中产品名；主页欢迎语例外。 |
| 修改 | `SettingsUI/Design/HomeRedistribution` | <code>任何人都可以檢視、修改及散布此程式碼。</code> | <code>在 GPLv3 授權條款下，任何人均可檢視、修改及散布此專案的程式碼。</code> | 在既有开源许可上下文中明确分发条件，不改版权署名。 |
| 修改 | `SettingsUI/Home/Developer` | <code>智绘教Inkeys 創始開發者</code> | <code>Inkeys 創始開發者</code> | 按已确认规则统一通用品牌，保留现有开发者身份称谓。 |
| 修改 | `SettingsUI/Language/UI/Language/en-US` | <code>English(Beta)</code> | <code>English &#183; en-US &#183; Beta</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Language/UI/Language/zh-CN` | <code>简体中文</code> | <code>简体中文 &#183; zh-CN</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Language/UI/Language/zh-TW` | <code>繁體中文(Beta)</code> | <code>繁體中文（台灣） &#183; zh-TW &#183; Beta</code> | 按用户确认的母语名称、语言码与Beta显示规则统一标签。 |
| 修改 | `SettingsUI/Configuration/N` | <code>設定保存</code> | <code>設定與儲存</code> | 栏目同时包含配置清理和画布保存，明确并列关系。 |
| 修改 | `SettingsUI/Configuration/CanvasSave/N` | <code>畫布保存</code> | <code>畫布儲存</code> | 统一台湾地区的儲存用语。 |
| 修改 | `SettingsUI/Configuration/CanvasSave/SaveTime/N` | <code>保存時長</code> | <code>歷史畫布保留期限</code> | 该设置控制历史画布保留期限，而非保存操作持续时间。 |
| 修改 | `SettingsUI/Version/LimitUpdate/N` | <code>自動更新已被封鎖！\n\n新版的系統相容性需求已有所調整，我們已封鎖自動更新，因為自動更新後軟體可能無法啟動。請先查看詳細資訊，確認您的系統環境是否符合要求，並手動下載與更新軟體。需要註意的是，目前通道的修復功能將暫時無法使用。</code> | <code>自動更新已被封鎖！\n\n新版的系統相容性需求已有所調整，我們已封鎖自動更新，因為自動更新後軟體可能無法啟動。請先查看詳細資訊，確認您的系統環境是否符合要求，並手動下載與更新軟體。需要注意的是，目前通道的修復功能將暫時無法使用。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Version/Inkeys3Update/N` | <code>Inkeys3 已正式發布！\n\n您可以透過下方按鈕進一步了解 Inkeys3 的相關資訊。需要註意的是，目前通道的修復功能將暫時無法使用。</code> | <code>Inkeys3 已正式發布！\n\n您可以透過下方按鈕進一步了解 Inkeys3 的相關資訊。需要注意的是，目前通道的修復功能將暫時無法使用。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Version/VersionTip` | <code>檢測到軟體架構與系統架構不符，與系統架構不適應有可能會導致軟體效率降低，並影響體驗。建議在本頁面下方「軟體修復」模組中修復軟體（啟用「修復時修正軟體架構」選項），或在頁面底部手動選擇目標架構並修復。</code> | <code>偵測到軟體架構與系統架構不相符，可能降低效能並影響使用體驗。請在下方「軟體修復」中啟用「修復時修正軟體架構」，或手動選擇目標架構後進行修復。</code> | 删除重复拗口表达，保留实际修复选项及操作路径。 |
| 修改 | `SettingsUI/Version/Update/ChannelTip` | <code>正式通道(LTS) 推送最新的穩定版本，具備較高之穩定性。（推薦）\n預覽通道(Insider) 推送穩定性一般的預覽版本，且可能會被防毒軟體誤報。\n早期通道(Canary) 推送 Inkeys3 早期版本，不建議使用。</code> | <code>正式通道（LTS）：最新穩定版本，建議日常使用。\n預覽通道（Insider）：預覽版本，穩定性可能較低，安全軟體可能發出警告。\n早期通道（Canary）：Inkeys 3 早期版本，不建議日常使用。</code> | 统一三通道含义，避免绝对稳定性或把安全告警笼统定性为误报。 |
| 修改 | `SettingsUI/Version/Update/Arch/E` | <code>註意：與現在系統不一致的架構可能會導致軟體無法啟動或嚴重效能問題。</code> | <code>注意：與現在系統不一致的架構可能會導致軟體無法啟動或嚴重效能問題。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Regular/StartUp/AutoStartE` | <code>設為目前用戶帳戶開機時啟動 智绘教Inkeys。</code> | <code>目前使用者登入 Windows 時自動啟動 Inkeys。</code> | 启动项由当前用户登录触发，并按已确认规则统一品牌。 |
| 修改 | `SettingsUI/Regular/StartUp/Link/E` | <code>自動修正名稱並建立捷徑，請點選「更多選項」。</code> | <code>可修正現有捷徑名稱或建立新捷徑。請選擇「更多選項」進行設定。</code> | 补足操作对象与配置入口，修正文句连接。 |
| 修改 | `SettingsUI/Regular/Appearance/AnimationEnableE` | <code>關閉後，UI3 主列與設定介面的導覽及頁面動畫將立即完成。</code> | <code>關閉後，UI3 主欄與設定介面的導覽及頁面動畫將立即完成。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Regular/Appearance/AnimationRateE` | <code>調整 UI3 主列與設定介面的動畫速率，範圍為 0.1x–5.0x。</code> | <code>調整 UI3 主欄與設定介面的動畫速率，範圍為 0.1x–5.0x。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Regular/Appearance/Theme/E` | <code>選擇「推薦皮膚」將會根據官方推薦皮膚自動替換。</code> | <code>選擇「推薦皮膚」後，系統會自動套用官方目前推薦的主題樣式。</code> | 消除循环定义，说明推荐皮肤的实际效果。 |
| 修改 | `SettingsUI/Regular/Appearance/TaskbarScaleBase/E` | <code>100% 時基礎量高 48 DIP；真實量依工作列厚度。</code> | <code>100% 縮放時，「工作列基礎量」的主欄高度固定為 48 DIP；「工作列真實量」會隨工作列厚度調整。</code> | 区分现有Base固定48DIP与Actual随任务栏厚度的两种模式，避免混淆。 |
| 修改 | `SettingsUI/Regular/Behavior/TopWindow/E` | <code>在非繪圖模式下將依此間隔將視窗置頂，落筆時暫停置頂視窗。間隔越短越能減少其他軟體視窗幹擾，但會消耗較多CPU資源。如果其他軟體視窗持續幹擾，建議使用插件-超級置頂。</code> | <code>在非繪製模式下依設定間隔重新置頂視窗，落筆時暫停。間隔越短，越不容易被其他視窗遮擋，但可能增加 CPU 使用量。若仍受到干擾，可嘗試「超級置頂」外掛程式。</code> | 删除重复和错字，说明周期性重新置顶、落笔暂停及资源权衡。 |
| 修改 | `SettingsUI/Regular/Behavior/CheckingRetract` | <code>點擊時收起主欄</code> | <code>點擊主欄外區域時收起</code> | 补充点击区域；兼容资源保留，不恢复退休设置。 |
| 修改 | `SettingsUI/Regular/Tentative/SafeMode/N` | <code>教學安全選項</code> | <code>當機處理方式</code> | 该组选项控制崩溃后动作，并非独立安全启动模式。 |
| 修改 | `SettingsUI/Regular/Tentative/AllowMultipleInstances/E` | <code>允許從目前的安裝目錄同時執行多個程式執行個體。關閉後將阻止額外執行個體啟動，正在執行的個體不受影響。</code> | <code>允許從目前的安裝目錄同時執行多個程式執行個體。關閉後將阻止其他執行個體啟動，不影響已在執行的執行個體。</code> | 统一執行個體术语，明确只阻止新实例而不关闭既有实例。 |
| 修改 | `SettingsUI/Draw/Haptics/N` | <code>筆震動回饋</code> | <code>手寫筆觸覺回饋</code> | 明确手写笔触觉反馈，与屏幕触摸反馈区分。 |
| 修改 | `SettingsUI/Draw/Cursor/DrawingStyle` | <code>畫筆游標</code> | <code>筆形游標</code> | 源码对应PenIcon笔形图标；保留准确英文，明确中文含义。 |
| 修改 | `SettingsUI/Draw/Touch/N` | <code>觸摸</code> | <code>觸控</code> | 统一台湾地区既有触控术语。 |
| 修改 | `SettingsUI/Draw/Eraser/PalmEraser/Introduction` | <code>落筆後用手掌接觸螢幕即可擦除，不必切換工具。手掌橡皮的大小會在辨識時間視窗內確定，抬起後仍保留原來的工具。</code> | <code>用手掌接觸螢幕即可擦除，無需切換工具。橡皮擦大小在接觸後的辨識時間視窗內確定，抬起後仍保留原工具。</code> | 按实际Touch面积识别机制表述，避免暗示另需物理手写笔先落下。 |
| 修改 | `SettingsUI/Preset/StartupPlacement/E` | <code>置中的底部工具列會展開顯示主工具列；左下角或右下角懸浮視窗只顯示主按鈕。</code> | <code>置中的底欄啟動後會展開主欄；左下角或右下角的浮動視窗僅顯示主按鈕。</code> | 统一主欄、底欄产品名称及浮动窗口描述。 |
| 修改 | `SettingsUI/Preset/StartupPlacement/CenteredBottomBar` | <code>置中底部工具列</code> | <code>置中底欄</code> | 统一置中底欄产品名称。 |
| 修改 | `SettingsUI/Performance/InterfaceFrameRate/E` | <code>調整主列、設定和翻頁控制項的流暢度。較低影格率可降低資源使用量，修改後立即生效。</code> | <code>調整主欄、設定和翻頁控制項的流暢度。較低影格率可降低資源使用量，修改後立即生效。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Performance/RenderingSection` | <code>繪製與相容性</code> | <code>渲染與相容性</code> | 渲染技术设置与绘图操作概念区分，统一技术术语。 |
| 新增 | `SettingsUI/Experimental/N` | —（原无此 key） | <code>實驗室</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 修改 | `SettingsUI/Experimental/UI3/AllowFractionalFontSize/N` | <code>允許介面字型以非整數像素尺寸繪製</code> | <code>允許介面字型以非整數像素尺寸渲染</code> | 渲染技术设置与绘图操作概念区分，统一技术术语。 |
| 修改 | `SettingsUI/Experimental/UI3/AllowFractionalFontSize/E` | <code>關閉時，主列和控制項靜止狀態的字型像素尺寸向下取整；縮放及轉場動畫仍連續變化。立即生效。</code> | <code>關閉時，主欄和控制項靜止狀態的字型像素尺寸向下取整；縮放及轉場動畫仍連續變化。立即生效。</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/Experimental/ConsoleOutput/Haptics/N` | <code>測試觸控回饋</code> | <code>手寫筆觸覺回饋診斷輸出</code> | 这是诊断输出开关，而非发起一次触觉测试的动作。 |
| 修改 | `SettingsUI/PlugIn/N` | <code>插件</code> | <code>外掛程式</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/E` | <code>在投影片演示時提供控製按鈕與畫筆功能。每頁擁有獨立畫板，可將筆跡固定在頁面上。不影響原有功能及外接裝置使用。</code> | <code>在投影片放映時提供簡報控制按鈕與畫筆控制按鈕。每頁有獨立畫布，讓筆跡固定在對應頁面上，不影響原有功能及外接裝置的使用。</code> | 补全简报与画笔两类控制按钮，并统一放映、画布及控制术语。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/VersionError` | <code>版本號未知（插件發生錯誤）</code> | <code>版本號未知（外掛程式發生錯誤）</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tip` | <code>使用插件時需要確保智绘教Inkeys的程式權限和PPT程式是一致的才能正確識別。如果依然存在問題，可以參考解決方案。</code> | <code>使用外掛程式時，請確保 Inkeys 與 PowerPoint/WPS 的執行權限相同，以便正確偵測放映程序；如有問題，請參閱解決方案。</code> | 统一品牌并明确PPT联动的权限匹配条件。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Error` | <code>外掛程式載入發生錯誤，可能是因為未安裝 .NET Framework 4.x 或全域 COM 環境發生異常。\n錯誤代碼</code> | <code>外掛程式載入發生錯誤，可能是未安裝 .NET Framework 4.x 或 COM 環境異常。\n詳細資訊：</code> | 消费者拼入完整Error:诊断信息而非纯数字，改为自然分隔前缀。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/N` | <code>控製項顯示</code> | <code>控制項顯示</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/BottomBoth` | <code>顯示底部兩側控製項</code> | <code>顯示底部兩側控制項</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/MiddleBoth` | <code>顯示中部兩側控製項</code> | <code>顯示中部兩側控制項</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetDisplay/BottomMiddle` | <code>顯示底部主欄控製項</code> | <code>顯示底部主欄控制項</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetPosition/N` | <code>控製項位置</code> | <code>控制項位置</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetPosition/Reset` | <code>重設控製項位置</code> | <code>重設控制項位置</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetPosition/Remember` | <code>記憶控製項位置</code> | <code>記憶控制項位置</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/N` | <code>控製項縮放</code> | <code>控制項縮放</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/Page/BottomSideBoth` | <code>底部兩側控製項縮放</code> | <code>底部兩側控制項縮放</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/Page/MiddleSideBoth` | <code>中部兩側控製項縮放</code> | <code>中部兩側控制項縮放</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/WidgetScale/State/BottomSideMiddle` | <code>底部主欄控製項縮放</code> | <code>底部主欄控制項縮放</code> | 修正繁中錯字，統一主欄、控制項、注意與外掛程式等既有術語。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/N` | <code>實驗性選項</code> | <code>實驗選項</code> | 与其他设置页面的实验选项名称保持一致。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/CloseWppE` | <code>重啟軟體生效。在WPS退出放映後，原來的WPP簡報處理序並不會立卽關閉，而是保持遊離卡死狀態。此時再次開始放映PPT則無法立卽識別到放映處理序並開啟PPT連動。開啟此選項將幫助關閉遊離卡死的WPP放映處理序，這不會對您的檔案造成影響，推薦開啟。</code> | <code>重新啟動軟體後生效。嘗試關閉 WPS 結束放映後殘留的 WPP 處理序，以改善下次放映時的偵測。</code> | 删除未经核验的“不会影响文件”绝对保证；保留兼容文案，不恢复旧功能。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/Tentative/AutoTakeOverOnceE` | <code>避免確實需要使用 PPT 原生批註的情境。</code> | <code>避免在確實需要使用 PPT 原生批註時反覆接管。</code> | 补足反复接管的动作对象，修复残缺句。 |
| 修改 | `SettingsUI/PlugIn/PPTHelper/SupportNote` | <code>支援 Microsoft PowerPoint 2007 與 Kingsoft WPS 2013 及以上版本。</code> | <code>支援 Microsoft PowerPoint 2007 或更新版本，以及 Kingsoft WPS 2013 或更新版本。</code> | 澄清两类软件各自最低版本的语法，不扩大既有支持范围。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/E` | <code>使用 UiAccess 技術在其他一般軟體以及工作列、Alt+Tab、螢幕小鍵盤和鎖定畫面等上方顯示軟體視窗。</code> | <code>使用 UIAccess 讓視窗顯示在受支援的系統介面及應用程式視窗上方。</code> | 去掉未经代码证实的锁屏显示保证，保留已有功能描述。 |
| 修改 | `SettingsUI/PlugIn/SuperTop/Warn` | <code>超級置頂插件處於 BETA 階段，可能遇到不穩定的問題。\n軟體在啟動時會透過再次執行自身的方式來提升權限，提升權限成功後再執行新的軟體執行個體。\n該過程可能失敗，導致錯誤地啟動擁有系統管理員權限的軟體，可能會影響 PPT 連動。\n過程中會彈出 UAC 確認對話方塊，請點擊確認或關閉 UAC 提示（不推薦）。\n過程中可能被防毒軟體誤報，這是正常現象。</code> | <code>超級置頂外掛程式處於 Beta 階段，可能不穩定。\n軟體啟動時會重新執行自身以要求提升權限，再啟動新執行個體。\n此過程可能失敗；若程式以系統管理員權限執行，PPT 連動可能受到影響。\n權限提升可能出現 UAC 提示；請確認程式來源與發行者後再允許。\n若安全軟體發出警告，請核對檔案來源與簽章，勿直接停用防護。</code> | 准确说明权限提升与告警，删除关闭UAC的指引和把所有告警当作误报的保证。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/LicenseTitle` | —（原无此 key） | <code>GPLv3 開放原始碼外掛程式</code> | 将既有DDB许可说明接入国际化，保留原有用途限定、GPLv3发布、无质量保证及使用后果声明。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/LicenseDescription` | —（原无此 key） | <code>外掛程式僅供學習交流和研究使用，不得用於其他任何用途。\n同類軟體浮動視窗攔截助手（DesktopDrawpadBlocker）是依 GPLv3 授權條款發布的開放原始碼軟體。\n發布於 GitHub 儲存庫：https://github.com/Alan-CRL/DesktopDrawpadBlocker\n我們發布此程式，希望它有用，但不承擔任何品質保證責任。\n使用者須自行承擔使用此外掛程式產生的後果與影響；使用即視為同意本協議。</code> | 将既有DDB许可说明接入国际化，保留原有用途限定、GPLv3发布、无质量保证及使用后果声明。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/UseSection` | —（原无此 key） | <code>使用外掛程式</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableTitle` | —（原无此 key） | <code>啟用外掛程式</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableDescription` | —（原无此 key） | <code>預設模式下，外掛程式會隨 Inkeys 啟動和關閉。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/GeneralSection` | —（原无此 key） | <code>一般選項</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorTitle` | —（原无此 key） | <code>以系統管理員身分啟動外掛程式</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorDescription` | —（原无此 key） | <code>變更此設定時，需要等待外掛程式回應。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalTitle` | —（原无此 key） | <code>攔截間隔</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalDescription` | —（原无此 key） | <code>間隔越短，越能及時攔截新出現的視窗，但會使用較多 CPU 資源。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalShort` | —（原无此 key） | <code>短（500 ms）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMediumShort` | —（原无此 key） | <code>較短（1 s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMedium` | —（原无此 key） | <code>中等（3 s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalMediumLong` | —（原无此 key） | <code>較長（5 s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntervalLong` | —（原无此 key） | <code>長（10 s）</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PrecisionSection` | —（原无此 key） | <code>精確控制</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo3Title` | —（原无此 key） | <code>希沃白板3 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo5Title` | —（原无此 key） | <code>希沃白板5 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Seewo5CTitle` | —（原无此 key） | <code>希沃轻白板（5C） 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PincoSidebarTitle` | —（原无此 key） | <code>希沃品课教师端 側欄浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/PincoDrawingTitle` | —（原无此 key） | <code>希沃品课教师端 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoPptTitle` | —（原无此 key） | <code>希沃PPT小工具</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IwbAssistantTitle` | —（原无此 key） | <code>希沃课堂助手 PPT小工具</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/YiouTitle` | —（原无此 key） | <code>欧帝白板 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AiClassTitle` | —（原无此 key） | <code>AiClass 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ClassInXTitle` | —（原无此 key） | <code>ClassIn X 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IntelligentClassTitle` | —（原无此 key） | <code>天喻教育云互动课堂 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ChangYan4Title` | —（原无此 key） | <code>畅言智慧课堂4.0 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/ChangYan5Title` | —（原无此 key） | <code>畅言智慧课堂5.0 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Iclass30SidebarTitle` | —（原无此 key） | <code>C30智能教学 側欄浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/Iclass30DrawingTitle` | —（原无此 key） | <code>C30智能教学 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopSidebarTitle` | —（原无此 key） | <code>希沃桌面 側欄浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopDrawingTitle` | —（原无此 key） | <code>希沃桌面 桌面畫筆浮動視窗</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/IncludesPptNote` | —（原无此 key） | <code>包含 PPT 控制項。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/RestoresWhiteboardNote` | —（原无此 key） | <code>包含 PPT 控制項。支援在白板模式下自動恢復。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/WindowTrackingNote` | —（原无此 key） | <code>包含 PPT 控制項。支援在白板模式下自動恢復及視窗追蹤。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/SeewoDesktopVersionsNote` | —（原无此 key） | <code>適用於 1.0/2.0/2.5/3.0 的普教版及高教版</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 新增 | `SettingsUI/PlugIn/DesktopDrawpadBlocker/AdministratorNote` | —（原无此 key） | <code>DDB 需以系統管理員權限執行。</code> | 将活动DDB详情文案接入国际化，保留识别对象专名及各项功能条件，不修改窗口匹配逻辑。 |
| 修改 | `SettingsUI/PlugIn/LnkHelper/E` | <code>在程式啟動時確保已經建立的智绘教Inkeys捷徑是有效的。因為程式更新會修改檔案名稱，這會導致捷徑失效。可選的，如果桌面沒有智绘教Inkeys的捷徑，則建立一個。</code> | <code>在程式啟動時確保現有 Inkeys 捷徑有效。程式更新可能變更檔案名稱，造成捷徑失效。也可選擇在桌面沒有 Inkeys 捷徑時建立一個。</code> | 按用户确认的通用品牌规则统一非简中名称，并精炼快捷方式保障说明。 |
| 新增 | `SettingsUI/Component/SoftwareSection` | —（原无此 key） | <code>軟體</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SystemSection` | —（原无此 key） | <code>系統</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/KeyboardSection` | —（原无此 key） | <code>鍵盤模擬</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/RollCallSection` | —（原无此 key） | <code>點名器</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSection` | —（原无此 key） | <code>ClassIsland 連動</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ExplorerTitle` | —（原无此 key） | <code>開啟檔案總管</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/TaskManagerTitle` | —（原无此 key） | <code>開啟工作管理員</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ControlPanelTitle` | —（原无此 key） | <code>開啟控制台</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ShowDesktopTitle` | —（原无此 key） | <code>顯示桌面</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/LockScreenTitle` | —（原无此 key） | <code>鎖定畫面</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/EscapeTitle` | —（原无此 key） | <code>按下 Esc</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/AltF4Title` | —（原无此 key） | <code>按下 Alt+F4</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCaller1Title` | —（原无此 key） | <code>IslandCaller 1 隨機點名</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCaller2Title` | —（原无此 key） | <code>IslandCaller 2 單人點名</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom1Title` | —（原无此 key） | <code>SecRandom 1 快速抽取</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom2Title` | —（原无此 key） | <code>SecRandom 2 快速抽取</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandom2CompatTitle` | —（原无此 key） | <code>SecRandom 2 快速抽取（相容）</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/NamePickerTitle` | —（原无此 key） | <code>NamePicker 隨機點名</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSettingsTitle` | —（原无此 key） | <code>ClassIsland 應用程式設定</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandProfileTitle` | —（原无此 key） | <code>ClassIsland 設定檔編輯</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSwapTitle` | —（原无此 key） | <code>ClassIsland 快速換課</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/IslandCallerRequirement` | —（原无此 key） | <code>需要安裝 ClassIsland 外掛程式 IslandCaller，並註冊 ClassIsland 的 URL 通訊協定。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandomUrlRequirement` | —（原无此 key） | <code>需要註冊 SecRandom 的 URL 通訊協定。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/SecRandomIpcRequirement` | —（原无此 key） | <code>需要註冊 SecRandom 的 IPC 通訊協定。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/NamePickerRequirement` | —（原无此 key） | <code>需要註冊 NamePicker 的 URL 通訊協定。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandProtocolNote` | —（原无此 key） | <code>需要在 ClassIsland 的應用程式設定中註冊 URL 導覽通訊協定。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 新增 | `SettingsUI/Component/ClassIslandSwapNote` | —（原无此 key） | <code>ClassIsland 目前未載入課表時，此元件不會生效。</code> | 将活动组件设置的分类、操作及协议条件接入国际化；保留组件专名，不改变真实动作或注册要求。 |
| 修改 | `SettingsUI/HotKey/E` | <code>目前僅提供以下固定組合，可分別開啟或關閉，暫不支援自訂。\n除全域往返外，其餘快捷鍵僅在批註狀態有效；設定文字輸入與模態對話框優先。選擇或穿透狀態下交由目前應用程式處理。\n全域往返在設定一般編輯期間仍生效；應用程式模態對話框期間暫停動作，但仍保留該組合。國際鍵盤的 AltGr 可能與 Ctrl + Alt 衝突，可單獨關閉全域往返。\n鍵盤快捷鍵不接管 PPT 翻頁。</code> | <code>目前僅提供以下固定組合，可分別開啟或關閉，暫不支援自訂。\n除全域批註切換外，其餘快捷鍵僅在批註狀態有效；設定文字輸入與模態對話框優先。選擇或穿透狀態下交由目前應用程式處理。\n全域批註切換在設定一般編輯期間仍生效；應用程式模態對話框期間暫停動作，但仍保留該組合。國際鍵盤的 AltGr 可能與 Ctrl + Alt 衝突，可單獨關閉全域批註切換。\n鍵盤快捷鍵不接管 PPT 翻頁。</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/ToggleAnnotationN` | <code>全域往返</code> | <code>全域批註切換</code> | 明确该快捷键切换批注状态。 |
| 修改 | `SettingsUI/HotKey/Registering` | <code>正在註冊全域往返</code> | <code>正在註冊全域批註切換</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/Disabling` | <code>正在關閉全域往返</code> | <code>正在關閉全域批註切換</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/UnregisterFailed` | <code>全域往返取消註冊失敗，該組合仍被保留</code> | <code>全域批註切換取消註冊失敗，該組合仍被保留</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/Registered` | <code>全域往返已生效</code> | <code>全域批註切換已生效</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/Disabled` | <code>全域往返已關閉</code> | <code>全域批註切換已關閉</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/RegisterFailed` | <code>全域往返註冊失敗，組合可能被其他程式占用</code> | <code>全域批註切換註冊失敗，組合可能被其他程式占用</code> | 跟随批注切换名称，统一说明与注册状态中的同一快捷键称呼。 |
| 修改 | `SettingsUI/HotKey/SynchronizationPending` | <code>正在同步按鍵狀態；請在一般權限視窗中放開按鍵後重試。</code> | <code>正在同步按鍵狀態；請切換至未提升權限的視窗，放開按鍵後重試。</code> | 明确“普通权限”指未提升权限窗口。 |
| 新增 | `SettingsUI/Sponsor/Thanks` | —（原无此 key） | <code>贊助後，可聯絡作者，將您的暱稱與贊助金額加入社群名片，以表達感謝。</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/RenderingSection` | —（原无此 key） | <code>UI3 渲染除錯</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DirtyRegions/N` | —（原无此 key） | <code>髒區除錯</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DirtyRegions/E` | —（原无此 key） | <code>顯示 UI3 每一影格實際提交的髒區邊界。</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/FrameRate/N` | —（原无此 key） | <code>顯示影格率</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/FrameRate/E` | —（原无此 key） | <code>每秒更新前一秒的平均影格率與無等待影格率。</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/ConsoleSection` | —（原无此 key） | <code>主控台輸出</code> | 将活动界面硬编码文字接入国际化，补齐实验室、赞助致谢与渲染诊断页面。 |
| 新增 | `SettingsUI/DebugSoftware/DisplayInformation` | —（原无此 key） | <code>顯示器與 EDID</code> | 将活动显示器与EDID调试信息标题接入国际化。 |
| 修改 | `SettingsUI/Update/Newer` | <code>軟體相對最新版更新</code> | <code>目前版本較官方最新發布版本新</code> | 自然表达当前版本比官方最新发布版本更新的状态。 |

## 核对依据

- 原始快照：`baseline/zh-CN.jsonc`、`baseline/en-US.jsonc`、`baseline/zh-TW.jsonc`。
- 116 项审查报告的逐项处理与源码依据：`research/report-audit.md` 及 `research/existing-locales-fixes.md`。
- 按实际资源值生成，不把保留既定地区化的文案记为修改。
