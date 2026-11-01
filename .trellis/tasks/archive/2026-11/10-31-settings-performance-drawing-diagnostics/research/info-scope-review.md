# R9 信息图标与作用范围复核

日期：2026-11-01。角色：trellis-check。仅只读复核本轮R9，写本记录；未修改源码、未构建/重跑测试、未启动GUI、未提交，也未扩大任务范围。

## 结论

当前未发现R9源码阻塞项。小型附注使用实心InfoSolid；只有已有普通解释之外的补充文字才显示蓝i，单段前提恢复普通说明。类/页级信息框、PPT教程/权限动作、通道和架构原呈现已恢复；其他具体条目的重启与风险附注仍保留。版本状态标题只作用于顶部更新状态框。

本轮ARM64构建与专项验证由根代理集中执行，本记录不将静态复核写成构建/测试已通过，也不声称进行了桌面截图验收。

## 1. 实心图标与公共参数归一

- `Setting.Controls.cpp:243`：InfoTextAt仅把小型附注glyph改为U+F167，保留原Caption字号、图标槽/间隔、Accent颜色、TextPrimary正文和测量方式。
- 本地 `Inkeys/additional/imfluent/imfluent_icons.h:1206` 的 `ImFluentIcon_InfoSolid` 为UTF-8 `EF 85 A7`，即U+F167；不是对描边E946改一个名称。专项字体查询也使用FindGlyphNoFallback(F167)。
- `SettingRow`在`:583`—`:586`对参数角色做窄归一：description为null/空串而note非空时，`description=note; note=nullptr`。之后完全走原说明的TextSecondary/Caption/几何，不保留i的缩进或额外4 DIP空白。
- 只有description和note均非空才走原独立附注布局。无note的旧行路径和宽度/退让动画仍由原算法处理；没有按中文/英文内容关键词做自动拆句。
- 归一只改本次函数局部指针，不保存到ImGui状态、跨帧容器或异步回调；页面IA(...).c_str()的临时字符串存活至整个同步调用表达式结束，公共封装内部同步SettingRow也在此范围内，没有新增悬空字符串生命周期。
- 控件层没有修改Notice的大图标、侧栏图标或字体默认值，InfoText仍只是低层辅助。

## 2. 类/页级与PPT边界恢复

| 用户要求 | 当前实现 | 判断 |
| --- | --- | --- |
| 快捷键原整块 | Pages.cpp:893恢复Notice(HotKey.N,HotKey.E) | 三语言HotKey.E与HEAD原文逐字相同，AltGr/模态/范围说明没有丢失或重复拆为组级i |
| ClassIsland原块 | Setting.cpp:4035恢复原协议Notice | 不用InfoText替代，不改变联动组件动作 |
| SuperTop原警告 | :3759恢复Warn Notice/Warning | 原整块提权、UAC、PPT及杀软注意仍在；具体开关自己的SuperTopNote另保留 |
| 实际架构不匹配 | :2862恢复条件Warning Notice | 保留inconsistentArchitecture条件与原修复建议，不变成常驻i行 |
| 更新通道原块 | :2956恢复ChannelTip Notice；:2970的ComboRow为null description、无note | 三语言ChannelTip与HEAD原文逐字相同，没有再分配ChannelNote |
| 目标更新架构原样 | :3007恢复Arch.E为普通description，未传note | 用户指定该条恢复普通呈现；业务架构映射/写回不变 |
| PPT详情去重支持范围 | :3546插件header后不再输出SupportNote | 插件主页NavigationRow的支持说明保持，详情没有上下重复支持行 |
| PPT教程卡片 | :3557恢复原Notice正文+Solve动作 | 原ppt-com URL保持，原条件/返回值驱动动作保持 |
| PPT管理员卡片 | :3564保留setAdmin条件；:3566恢复Notice正文+Solve | 原ppt-admin URL和权限说明保持，不多加小蓝i，不更改权限流程 |

活动Setting.cpp/Pages.cpp搜索已无InfoText调用；类级信息没有换个位置继续使用小型i。保留公共InfoText API不等于给任何类/页重新加i，也无需为本次用户纠正删除无关公共能力。

## 3. 单段前提与仍保留的具体条目附注

7个单段组件前提均恢复为description参数，无note：IslandCaller1/2、SecRandom1/2/2兼容、NamePicker和ClassIsland快速换课（Setting.cpp:4007—:4050）。URL/IPC区别、插件安装前提和无课表限制文字都保留，只还原普通灰色说明呈现。其他仅有note而无description的条目也受公共归一保护，不再显示i。

以下真正“普通解释之外的补充”仍有显式note，没有因R9整页回滚误撤销：

- 语言SelectNote与原QueueConfirmRestart；配置清理EnableNote的数据风险。
- 启动位置和启动加载动画NextStartupRequired。
- 画布帧率、DComp、兆芯的RestartRequired；硬件加速自身排障/重启Note。
- 五个控制台开关的统一RestartRequired。
- 教学安全模式的启动错误例外、触摸面积辅助的大屏建议。
- 具体定格快捷键的FreezeNote；SuperTop开关E1/E2对应的重启/失败Note。
- DDB已有功能解释之外的管理员前提。DDB管理员启动只有单段“切换需等待”时公共层将其归为普通说明，与R9原则一致。

激光两个指定句仍直接删除，没有恢复统一新笔提示；两条BETA说明、粒子无toggle和运行时强制false未被R9修改。

## 4. 独立版本状态标题

- `Setting.cpp:2802`的`##update-status`仅改为`SettingsUI.Version.StatusTitle`。
- 三语言为“版本状态”“Version status”“版本狀態”。
- 实际更新偏好栏目仍读取`SettingsUI.Version.Update.N`，对应“更新偏好”“Update Preferences”“更新偏好”，未用状态标题覆盖设置栏目。
- 当前StatusTitle的产品调用只此一处；没有改变状态机、按钮、更新通道/架构或重启更新动作。

## 5. 专项覆盖是否有效

在原注意事项真实ImGui专项中加入R9覆盖，而非仅断言新局部变量：

- description为null和空串两种都经过真实SettingRow；原三语言、明暗主题、900/500/320 DIP和DPI矩阵仍执行。
- note降为普通说明时，真实DrawData必须零Accent顶点、TextSecondary的可见字形数量完整；这可发现仍画i、文字丢失或错误前景色，不能靠测试自行归一预期来掩盖。
- 几何与直接传同一段description的情况对照，核对height/action/description等原布局；没有残留note槽或多出高度。
- description+额外note仍检查F167的真实非fallback字形尺寸、一个蓝色glyph、TextPrimary全部字符顶点、与动作/下一行无重叠，并强制经过reveal中间态。
- 7类公开wrapper透传测试及focused headless严格无窗口入口保持，未更改为只“没有崩溃”的伪通过条件。

本代理未执行专项；最终退出码以根代理集中验证为准。

## 6. 国际化、格式与规范

- zh-CN/en-US/zh-TW当前均571个叶子键，集合一致。HotKey.E和ChannelTip通过脚本与HEAD原文逐字比较，三语言均True。
- 生成StatusTitle键只增加独立标题映射；不借本次恢复翻译改变数据值或既有选项。
- 检查所有已修改源文件：BOM状态相对HEAD保持，工作树CRLF；`git diff --check --ignore-submodules=all`通过。前轮局部diff未被整文件换行/格式化改写。
- PRD R9、design最终边界和`settings-attention-notes.md`已同步InfoSolid、note-only普通说明、原类级块保留等合同；没有误回到R8尚未澄清的广泛i行规则。
- 当前无需要修改产品代码的复核项；完整ARM64构建、i18n与注意/copy专项仍由根代理集中记录，GUI未授权且未执行。
