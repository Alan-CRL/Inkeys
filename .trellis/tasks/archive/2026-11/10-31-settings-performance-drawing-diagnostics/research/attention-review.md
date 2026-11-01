# R8 注意事项实现复核

日期：2026-11-01。角色：trellis-check。只读复核合并的Setting.Controls、Design布局、Setting/Pages调用、三语言与生成链、focused headless测试；只写本记录，不修改源码、不构建/重跑测试、不启动GUI、不提交。

## 结论

当前未发现需要修改产品代码的明确问题。盘点的活动注意事项已按显式参数或组级InfoText分离，业务动作保留；两个激光指定句已直接删除而未转移。核心布局保留无note的原路径，注意事项与说明同用Caption字号，蓝色信息i图标配正常前景正文，位于解释下方并参与原响应式退让。

本结论是源码/测试设计/格式的只读复核，不能替代根代理本轮完整ARM64构建、i18n检查和注意事项专项的最终退出码。未执行真实桌面或截图对比；与用户参考效果的对应以“说明下方、同字号、蓝i、正常文字”的实际绘制代码及DrawData断言为依据。

## 1. 盘点到实现的逐项闭合

| 盘点项目 | 合并后的归属 | 复核结果 |
| --- | --- | --- |
| 语言完整生效 | SelectE普通说明+SelectNote，ComboRow传note | QueueConfirmRestart和实际语言加载/保存保持 |
| 自动清理配置风险 | EnableE只说明清理行为，EnableNote保留降级/导入丢项恢复默认 | 不扩大成历史画布丢失风险，不改AutoClean |
| 启动位置 | 位置模式解释保留，NextStartupRequired传note | 保存/位置动作保持 |
| 启动加载动画 | E说明加载画面，NextStartupRequired传note | 未新增“必须现在重启”流程 |
| 画布帧率 | E保留刷新/资源权衡，RestartRequired传note | 继续只保存下次启动值 |
| 硬件加速 | E硬件/软件差异，Note含既有排障建议与重启 | 配置键/开关动作保持 |
| DComp/兆芯 | 原作用/回退解释保留，重启传note | 不改呈现回退和设备策略 |
| 五个控制台开关 | 各自E只描述输出，consoleToggle统一附RestartRequired | 仅保存配置，不改变诊断启动时机 |
| 更新通道 | ChannelTip正常通道说明，ChannelNote保留推荐、稳定性、误报/Canary注意 | 下拉映射、清安装包/重新检查动作不变 |
| 目标更新架构 | 原Arch.E作为note显式传入，普通解释可为空 | 风险没有删掉，架构切换业务保持 |
| 实际架构不匹配 | 原条件仍存在，条件内容用InfoText | 不变成无条件警告，不丢原修复建议 |
| 教学安全模式 | E说明操作，Note保留启动错误仍提示的例外 | Mode1/2中的重启名称不拆分 |
| 触摸面积辅助 | eraser.TouchArea.E功能说明+Note大屏建议 | 局部alias调用已接，物理尺寸缺失的动态Notice仍单独保持 |
| 快捷键总说明 | E功能概述+InfoText(HotKey.Note) | 模态/输入优先、组合保留和AltGr冲突均未丢失 |
| 定格快捷键 | FreezeE说明切换，FreezeNote工作区/下传限制 | 原数组对应item==5传note，未给其他快捷键错误附加 |
| PPT兼容版本前提 | PPTHelper.SupportNote在概览NavigationRow及详情InfoText | E不重复最低版本文字，两个不同页面各显示一次 |
| PPT权限与管理员检测 | 带note的ButtonRow替换原Notice，setAdmin条件保留 | 两个解决方案按钮及原ppt-com/ppt-admin URL保持，未移除教程动作 |
| SuperTop兼容与风险 | SupportNote；原Warn全文以InfoText；E1/E2普通状态+共用SuperTopNote | 两状态均有重启提示，BETA/UAC/权限/PPT/杀软原注意保持，未改提权流程 |
| DDB管理员启动 | 普通解释置空，note为“切换时需等待插件响应” | 移除旧不准确的UI阻塞/拦截间隔上限承诺；RestartDdb worker动作不变 |
| DDB精确控制权限 | 表格新增note字段，各需管理员条目显式传递 | “包括PPT控件/白板恢复/版本范围”等功能解释保持，回写intercept字段不变 |
| IslandCaller/SecRandom/NamePicker | 前提从description移到note | URL与IPC区别保持，无新增安装/权限流程 |
| ClassIsland组和换课 | 协议InfoText、无课表限制作为换课行note | 未因组级协议提示而遗漏课表前提 |

动态错误/确认仍保持原流程：语言重启确认、更新状态、PPT版本错误、画布恢复、快擦物理尺寸缺失和快捷键状态没有被当作普通E字符串机械删除。DDB许可块仍是独立许可内容，没有复制到每个开关下；正常功能说明与未用legacy键没有因关键词被恢复或改为新警告。

## 2. 激光删除与R7能力约束

三语言实际说明已变为：

- zh-CN：“BETA：允许多个手指同时使用激光笔。”“BETA：在激光笔轨迹周围显示粒子效果。”
- en-US：“BETA: Use the laser pointer with several fingers at the same time.”“BETA: Display particles around the laser trail.”
- zh-TW：“BETA：允許多個手指同時使用雷射筆。”“BETA：在雷射筆軌跡周圍顯示粒子效果。”

活动Setting/三语言/生成keys/中文快照搜索无NewStrokeNotice、两条指定新笔/不可用句或它们的新note版本。粒子仍为BeginDisabled内无toggle的SettingRow，UI传粒子false；Controller构造false、setter exchange(false)及其无额外wake约束没有被R8更改。

## 3. 核心绘制与布局合同

- `Setting.Controls.h`在公共SettingRow及Toggle/Combo/Slider/SliderInt/Button/ButtonGroup/Navigation封装尾部追加 `note=nullptr`；控件层不解析说明或依赖I18nKey，页面显式决定分离内容。
- `Setting.Controls.cpp:231` 的InfoTextHeight与`:238` 的InfoTextAt使用同一行高和图标缩进；说明与note均按Caption测量/绘制。图标E946使用Caption size和Accent蓝色，正文明确TextPrimary，未把正文染蓝或扩大字号。
- 图标为一个lineHeight槽，文本从槽后6 DIP开始；测量用相同扣除后的宽度，文本多行自然换行，单行至少保留图标高度。
- `Setting.Design.h:467` 的ResolveRowWithNote在note为空时直接返回原ResolveMultilineRowLayout。因而旧行几何/动画不会因新参数默认值产生保留空白。
- 有note时把说明、4 DIP间距、note高度作为整体交给原响应式说明布局；再拆出note矩形。note.top始终是description.bottom之后，动作区仍参与原先下移再扩宽的两阶段避让，stacked仍由原MeasureRow/ResolveRowLayout分配动作到文字块之后。
- SettingRow只调用action一次，Dummy和结束游标使用含note的整行height/RowGap，保持原分数DPI取整；不存在只绘制提示却不增加正文滚动高度的路径。
- InfoText组级版本用同一InfoTextAt/Height，再Dummy登记完整高度；空文本直接返回，不额外插入不可见布局项。
- NavigationRow把整张新高度作为实际按钮命中区域，未沿用不含note的旧高度；PPT教程ButtonRow仍使用原稳定action标签及返回值。

在允许的窄窗/宽窗范围内，该结构满足截图所需的“原说明下方蓝i小行”语义，而非新的大型Warning卡片。没有调用GUI或声称已看过实际桌面像素。

## 4. 专项测试质量与无窗口边界

`CheckSettingAttentionRows`位于 `InkeysHeadlessTests/setting_design_tests.cpp:348`，复核认为具有实际覆盖，而非仅镜像实现：

- 真实调用SettingRow和全部7种公共封装，检查action仅执行一次和真实action bounds；没有自写一个假行控件代替生产实现。
- 三语言长文、description为空的note-only、note=null/empty、900/500/320 DIP、明暗主题和既有DPI矩阵均进入真实ImGui帧。
- 每帧断言原title/description/action在card内且不重叠；note独立检查card内、在说明下方、与title/description/action无交叠；检查后续行游标和下个真实控件不侵入note。
- 无note时逐区域比较旧布局算法，用于明确的向后兼容合同；有note时还检查真实DrawData：一个蓝色i的4个顶点、Caption图标实际字形尺寸、TextPrimary文字顶点、全部本地化字符各出现一次、实际glyph坐标没有越过note分配范围。
- 因而即使测量函数也用于预期几何，独立的DrawData字形/颜色/字符数/坐标断言仍会捕获“不绘制、截断、错字号、错颜色或多次绘制”等问题。
- 宽度来回变换时强制出现reveal介于0与1的帧，再对同一几何/顶点约束复核，不以“没有动画帧”跳过过渡检查。
- wrapper比较有note时高度增加，同时检查空InfoText不移动游标、非空InfoText参与布局，不是条件恒真的“只要没崩就通过”。

`--setting-attention-only`沿既有headless入口早退到这组纯布局/ImGui帧测试；只加载真实字体并生成CPU DrawData，没有Win32 backend、HWND、D3D设备或桌面操作。本代理未执行该入口，也未重跑普通完整测试。

测试限度：它不模拟PPT教程点击启动外部浏览器，不运行UAC/插件。业务保留由回调/URL/条件diff静态确认；这些流程本轮无需为了UI提示改动而打开GUI。

## 5. 国际化、格式与收尾状态

- 当前zh-CN/en-US/zh-TW均572个叶子键，集合完全一致，note/Required键无待翻译占位；指定激光句三语言均已删除。不是只改中文却保留英文重启句重复说明。
- DDB/组件原本就是硬编码的中文前提，本轮仅显式移到note参数，没有引入第二套协议检测或悄悄改变业务含义。
- 所有修改源文件BOM与HEAD一致，工作树保持CRLF；`git diff --check --ignore-submodules=all`通过。没有原diff被全文件换行或格式化重写。
- 最新PRD/design含R8条款。已追加复核独立 `.trellis/spec/native-desktop/settings-attention-notes.md` 及native-desktop索引：七节合同与当前API参数位置、Caption 12/16 DIP、E946/Accent/TextPrimary、4 DIP间隔、空note旧几何、显式分离及focused无窗口入口一致；没有把整套共享控件合同重复塞入性能设置规范。
- 根代理已提供本轮完整ARM64构建exit0及i18n 572/572通过；attention-only与copy-only仍由根代理集中执行，本记录不把它们提前写成通过。
- 当前无需要修改源码的复核项。最终focused自检的命令/退出码由根代理集中记录，未执行的GUI/真机视觉不算已验证。
