# C1 主栏布局模型与持久化设计

本文为实现提案。行号指设计时的工作树。Bar.*、UI3.*、Setting.* 分别在 Inkeys/Inkeys/UI/{Bar,UI3,Setting}/，Other.Config.* 在 Inkeys/Inkeys/Other/，*_tests.cpp 在 InkeysHeadlessTests/。下文“拟增”的类型和签名均是交付接口。

## 1. 两种分割线的身份与占位

在 Other.Config.cppm:75、174 增加稳定 ID `Inkeys.Bar.UserDivider`，默认 OneTwo。把它注册一次为 FixedA1 官方实体、allowMultiple=true（注册归属），放置资格单独按 §6 表判断。RegisterButton 在 Bar.Button.cpp:118–170 检查固定区官方前缀、Extension 的非官方点分名和 Setting 特例。此方案沿用原检查；在 A2/B1 的规范化中只对这个精确 ID 开放通行。保留 IsFixedButtonsA1Id 的旧数组分类含义，UserDivider 不加入 required。Bar.Button.cpp:769–784 的 Divider 仍用于两条常驻交界线；MoreBoundary 仍走 RegisterLayoutMarker（174–194）。

PresetInitialization 以现有纯 Shape 样式为蓝本创建独立的 UserDivider 原型，改绑自身 localState，保留 preset=Divider、oneTwo、空文字、无点击、禁用图标、透明度与光效（Bar.Button.cpp:233–268）。每条持久化记录按 §4 实例化。现有不可命中、绘制高度与按压复位分支继续按 preset 生效（Bar.Interaction.cpp:5406–5412、6260–6266，Bar.RenderLoop.cpp:3764–3774、3821–3830）。

在 ResolveBarButtonListLayout 的 Main 分支先判精确 id，再走原 preset/尺寸分支（Bar.Button.cpp:2068–2102）。小档给用户线 width=1 DIP、height=dividerHeight、centerHeight=one、columnBoundary=false；SingleRow 自然居中在 x+0.5 并推进 1+gap。大档同宽、高 50，维持 1×2 跨度。给 UI3 的 ListLayoutItem 末尾增加默认关闭的“独占窄列”布尔值，仅大档用户线设置。在 PairedRows 中，对可见窄列结束未填满的列，起点取当前 width，置 p.x=width+0.5、p.y=gap+two/2，令 width+=1+gap、x=width、y=gap；隐藏项不修改游标。现有 PairedRows 没有普通 1×2 推进分支，单改 columnBoundary 会得到错误坐标（UI3.ListLayout.cppm:17–26，UI3.ListLayout.cpp:97–150）。两档每条可见用户线独占 6 DIP，任意数量均递增；紧邻固定线也有间距。固定线继续在既有 5 DIP 间隙中取中心，不占宽；半列封列、镜像及折叠动画按旧路径。档位高度 80/42.5、小线高 28.5 见 Bar.Metrics.cppm:78–105。默认没有用户线，因此新分支对默认几何的贡献严格为零。

## 2. A 段校验与两种兼容迁移

将 NormalizeFixedZone/IsExactFixedZonePermutation 的值逻辑移入 §6 的纯模块。按 Other.Config.cppm:244–270 的默认表生成必需 ID 计数。逐项剥离历史 Divider，另外保留一个原序候选序列（含 UserDivider）；从候选按“本区允许重复项”谓词筛出必需项投影。谓词本期只接受 UserDivider。检查其余每个 ID、次数及总数；任何缺失、重复、未知或错区均整段退默认，连同该段用户线一起舍弃。必需项无法解析到本区注册元数据时也回该区完整 schema 默认值（投影时跳过无法创建的实体），避免写回残缺集合。成功时遍历**候选原序**回填注册默认 Size，绝不用投影重建结果；校验计数只数必需项。替换 Bar.Button.cpp:1406–1428 的按 zone 丢条目与总长等于 defaults.size() 的二次检查。A codec 本来忽略 Visible、容错 Size（Other.Config.cpp:349–379）。扩展重复项谓词即可支持未来获准的 A 项，本期不添加笔类型、实例 UUID 或 schema 字段。

保留旧 A2 特例（Bar.Button.cpp:1375–1383）：去掉历史 Divider 后，原候选恰好只有 Whiteboard、Freeze 两项且匹配 IsLegacyBarA2Pair 才在末尾补 EndShow。含新用户线且缺 EndShow 的是新格式非法 A2，退默认。旧 Pierce 的迁移过滤保留。把 A1 六项旧序判断从 Bar.Button.cpp:1385–1404 移至 Config::TryMigrateLegacyBarButtonLayout（Other.Config.cpp:1171–1184、1219–1249），在旧单数组成功拆分、忽略 Divider/退休 ID 后，逐项比较 fixedA1Json 的长度和 ID。仅精确 Select, Draw, Eraser, Geometry, Recall, Clean 改用 MakeDefaultFixedButtonsA1，其他排列照录。已有任一新段就不进入此函数的拆分；ReadMini 的只读门禁见 Other.Config.cpp:1160–1165。直接提供三段或后来手动排成同一旧顺序，重启也须保留。

## 3. B 的持久化语义与真实 More

启动先执行 §5。Load 取同一组三段快照，调用 FromConfig（含迁入纯模块的 NormalizeExtensionZone），将 B 按下表单次从左至右解析。现有基础为 Bar.Button.cpp:1432–1486，禁止扩展冒用官方前缀的规则在 Other.Config.cppm:209–242。B codec/整数组提交见 Other.Config.cpp:383–418、454–474、1148–1150。

| 输入 | 四段结果与写回 |
| --- | --- |
| 第一个 MoreBoundary | 切换到 More，标记输出 twoTwo/true，无按钮；后续标记忽略，仍留在 More。 |
| 没有标记 | 已有合法实体均属 B1，序列化时在 B1 末尾补唯一标记。空数组得到默认 B。 |
| Setting | B1+More 首次出现的位置有效，后续全部丢弃。固定可用，尺寸按注册默认、Visible 规范为 true；缺失则追加到 More 尾。移到 B1 时不再另外注入 Setting。 |
| UserDivider / 历史 Divider / 错区 | 用户线仅在标记前保留，允许连续；保留 B 的 Visible；历史 Divider、固定 A 按钮、其余未知官方 ID 与非法点分 ID 丢弃。 |
| 已注册组件或插件 | 原序、Visible 保留，Size 校正为注册默认。16 个内置允许重复；其他注册为单例者跨标记只取首次，即使首项隐藏。 |
| 合法点分扩展未注册 | 每项保留所在段、重复次数、顺序及 Visible，尺寸按 DefaultSizeForBarButtonId；无运行时按钮。重装后依注册元数据再规范化。 |
| JSON 元素非对象、空/非字符串 Id、Visible 非 bool | 属字段 codec 失败，整字段取 schema 默认 [MoreBoundary, Setting]；区别于字符串 ID 语法错误时的逐项过滤。未知 Size 走尺寸回退。 |

Load 保留 Bar.Button.cpp:1609–1621、1644–1683 的固定、Redo、CanvasDebug、Pierce 与边界插入顺序。用 B1 的全部可呈现条目代替 1622–1629 的前二投影；More 按钮固定在 B1 末尾。发布 explicitMore 为标记后可呈现条目的有序实例，forcedOverflow 每次清空，任何配置都不会填入。Visible=false 的 B 实体保留模型与实例，在输出可呈现序列时过滤，避免仅设置 hide 后仍占 More 网格（UI3.ListLayout.cpp:54–58）。快照在 moreSnapshotMutex 下整体替换，最终调用 ApplyMainPresentationLocked。保留双 vector 接口使命中、动画及脏区扫描继续工作（Bar.Button.cppm:122–130，Bar.Interaction.cpp:6068–6079）。ResolveBarButtonListLayout 的第二组为空时 showDivider 自动为 false；原列数、上下展开、关闭、动画算法照用（Bar.Button.cpp:2127–2148，Bar.RenderLoop.cpp:5465–5508）。More 为空仍展示入口，HasButtons=false 自动收起。删掉 GetLegacyExtensionButtons 的活动栈及旧投影路径（Bar.Button.cpp:1505–1559，Bar.Button.cppm:229–233）。

## 4. 实例、投影与资源所有权

采用“注册原型 + 按条目克隆”。注册项保留 button 供现有注册信息、本地化与固定单例查询；为其增加实例取得方法。固定实体和 Setting 直接返回原 shared_ptr，保留 preset[]、barButtonState、面板锚与点击回调身份。标记无实体；两条边界线仍由 boundaryDividers 单独持有。16 个 registerComponent 统一改 allowMultiple=true，only 服从 RegisterButton 的反值赋值（Bar.Button.cpp:147–169、763–786、810–863、867–966）。资格由注册元数据、模型与归一化共同执行。用 shared_ptr 的 Snapshot/Replace，不从同一个裸指针构造第二个控制块（UI3.List.cppm:20–36）。

可重复项原型从不放入活动列表。对原型使用 BarButtonClass 拷贝构造（Bar.Button.cppm:43–88），带上基准尺寸、可见性、preset、稳定 ID、only、标签策略与 closeMoreAfterAction；UI3.Button.cppm:83–128 复制 Shape、SVG/PNG、字体文字、badge、图标种类与 clickFunc。只从未交互的原型创建，强制 state=&localState（基类对外部 state 会保留原指针，官方 Divider 原型即属此情形）。每份可独立修改几何/继承、显隐、颜色、hoverStage、pressScale、动画、绘制记录、按下/选中与设备缓存。不可克隆已经处于悬停、按下或过渡中的活动项。

BarButtonSetClass 持有本次配置的重复项强引用。相同规范布局重复 Load 时复用同一批实例（包括隐藏条目），避免 Bar.Button.cpp:990–997 的地址指纹变化；布局改变时按段、ID、同 ID 出现序号复用仍对应的项，其余创建/释放，运行时无需持久实例号。单例始终沿用原对象。重排/删改导致相同 ID 的实例匹配有歧义时，对变化的同 ID 子序列创建新实例并取消旧命中（具体判据为比较该段此 ID 的条目数、可见性及下标列表；任一变化且涉及重复项，则重建该组）；不承诺被删除条目的交互状态转移。新集合在 presentationMutex 内完成列表、More 快照、tot 和最终投影发布。ApplyMainPresentationLocked 除现有单例/运行时对象，还必须遍历重复项实例，使用各自 baseSize/baseUserVisible（Bar.Button.cpp:1026–1032、1090–1104）。RefreshLocalizedText 更新原型后按 ID 原位更新活动副本的短标签，含 More 和隐藏项；保留现有多语言元数据（1116–1135、1219 起，Bar.RenderLoop.cpp:13571–13575）。

关于简报所称 DiscardDeviceResources，当前产品入口实际为 UIRendering::NotifyFrameTargetDiscarded → DiscardDeviceDependentCaches → services_.resetDeviceCaches → ResetIconCaches（UI3.Renderer.cpp:260–270，Bar.Main.cpp:83–89，Bar.Button.cpp:1691–1709）。缓存清理覆盖注册对象、每个当前实例、两条固定线与 More，分别重置 SVG 和使用中的 PNG；遍历时取得强引用，不在注册锁内持有会重入注册表的路径。退休实例只允许旧帧/手势结束，并且不能再提交绘制。交互端 Bar.Interaction.cpp:6068–6097、6260–6295 的手势已有强引用；跨消息的 hoveredMainBarButton/lastClickedMainBarButton 仍是裸指针（8486–8489）。hover 候选从原 Snapshot 直接保存 shared_ptr，跨消息 hover 用强引用，双击用 weak_ptr.lock() 比身份，取消/离开时释放；在旧手势释放执行动作前复核该实例仍在原活动段。检查 5280–5284、5401–5452、6102–6110、6268–6272、6340–6362 全部消费点。切换由帧 owner 执行（§7），避免渲染期间替换 Bar.RenderLoop.cpp:9956 等裸借用的列表。不永久缓存所有曾删除的按钮。

## 5. 一次性导入与启动次序

唯一生产导入点在 Bar.Initialization.cpp:204–208，PresetInitialization、RegisterBuiltInComponents 之后、首次消费 Load 的布局之前。拟增 InitializeBarLayout：先收集旧标记，再取得三段作合并输入，完成规范化/导入/发布，最后 Load 消费结果。普通 Load、编辑页和刷新一律只取新布局。离屏 fixture 可显式调用同一初始化函数测试导入，无须每个 Load 再读开关。启动完成之前设置页不发布编辑，用同一初始化就绪状态约束读取。配置的 ReadAll/旧数组拆分先完成（Other.Config.cpp:1160–1166）。不增加版本键。

把 16 个稳定 ID 和旧字段的对应关系从 Bar.Button.cpp:867–966 提为一份有序映射，与 Other.Config.cppm:420–445 对齐，注册、设置页和导入使用同一 ID；注册顺序仍由 207–219 的有序列表机制体现。配置适配器在 Config 的锁内读取/清零对应字段；导入次序用完整的定义顺序，即使 835–839 因资源缺失跳过实体注册也能保留合法配置 ID。纯 ImportLegacy(model, enabledIdsInOrder) 按 true 项的序号将前二追加 B1，其余追加 More（新装 More 的 Setting 原位在前）。已有有效自定义布局优先：同 ID 已有可见实例则保留位置与全部副本；若全隐藏，原位启用首份；仅缺失 ID 追加。已有项占用该次导入的序号，不按现有 B1 数量截断。这是混合新旧数据时的合并约定，避免重载时重复追加、丢失原有排序或丢弃已开启组件。

启动导入与编辑提交由 `layoutApplyMutex` 串行化，Config 提供三段快照与整组替换（Config 锁内完成，其他 Write/复制只见完整组合）。启动在释放锁后同步 `config.Write()` 一次（启动初始化不在渲染回调中，沿用旧 Load 的启动保存）；仅有修正或开关为 true 时保存。启动写失败保留完整内存布局、不设待写状态：磁盘仍保留旧开关，下次启动按同一规则重新导入，之后任意完整配置写入也会带上新布局。重置同样清零 16 项，保留尺寸、方向、缩放等偏好。（2026-11-20 C1 第 3 轮按审查裁定修订。）

## 6. 编辑模型与纯函数

拟增独立叶模块 Inkeys.UI.Bar.LayoutModel（Bar.LayoutModel.cppm），export namespace Inkeys::UI::Bar。只 import Inkeys.Other.Config 的配置值类型、ID 校验和默认表，不访问全局 config、HWND 或设备；配置模块不反向 import。Headless 直接 ClCompile 产品模块源文件，见 InkeysHeadlessTests.vcxproj:97–109，139–144 已编入 Other.Config.cppm/.cpp。新模块按同样方式分别登记产品与测试工程；产品 filters 镜像目录（Inkeys.vcxproj:1098，Inkeys.vcxproj.filters:2342）。新测试由 animation_tests.cpp:76、1722、1895 的声明/main/累计失败数惯例接入。

以下均为拟增签名。M、C 分别简写 BarLayoutModel、BarLayoutCatalog。M 有 A1/B1/More/A2 四个有序 vector，元素复用 BarExtensionButtonLayoutEntry {Id, Size, Visible}；A 的 Visible 恒 true。C 是注册描述值快照（ID、注册区、实体或标记、默认尺寸、是否可重复），无按钮/回调。Position={Segment, size_t index}，ItemSource 是已有 Position 或库 ID 的变体。PlaceDecision 包含允许与否及原因（来源无效、下标越界、分区不符、不可新增、单例冲突）。BarLayoutConfigValues 是配置模块中的三 vector 值结构，元素沿用 Other.Config.cppm:98–112，不含 ConfigSequence 的锁。

~~~cpp
M FromConfig(const BarLayoutConfigValues&, const C&);
BarLayoutConfigValues ToConfig(const M&);
PlaceDecision CanPlace(const M&, const ItemSource&, Position gap, const C&);
std::optional<M> Move(const M&, Position from, Position gap, const C&);
std::optional<M> Insert(const M&, std::string_view id, Position gap, const C&);
std::optional<M> Remove(const M&, Position at, const C&);
M RemoveAllOf(const M&, std::string_view componentId, const C&);
M RemoveUserDividers(const M&);
std::optional<M> AutoAdd(const M&, std::string_view componentId, const C&);
M Reset();
M ImportLegacy(const M&, const std::vector<std::string>& enabledIdsInOrder);
~~~

FromConfig 执行 §2–3；ToConfig 输出 A1/A2 的 {Id,Size}、B1 + 唯一标记 + More 的 {Id,Size,Visible}。固定交界线、实体 More 与伴随按钮均不编码。Reset 取三个 MakeDefault*().Snapshot() 转成默认四段（Other.Config.cppm:244–270）。规范模型往返恒等。

| 条目 | Move 允许段 | 库 Insert | Remove |
| --- | --- | --- | --- |
| A1 必需项 | A1 | 拒绝 | 拒绝 |
| A2 必需项 | A2 | 拒绝 | 拒绝 |
| Setting | B1、More | 拒绝 | 拒绝 |
| 注册普通组件 | B1、More | 允许，注册单例已存在则拒绝 | 允许 |
| 用户线 | A1、B1、A2 | 允许连续 | 允许 |
| 固定线、标记、运行时入口或伴随按钮 | 无模型位置 | 拒绝 | 拒绝 |
| 已存合法缺席插件 | B1、More | 未注册不得新建 | 允许删除记录 |

CanPlace 为 Insert/Move 共用唯一资格检查；移动单例排除源项。落点是移除源之前的缝隙 [0,n]，源下标属 [0,n)。同段右移先减一，原位或紧邻右缝隙返回同一值；跨段保留实例条目数据，移入 A 的用户线遵守 A 可见性合同。Insert 按注册尺寸新建可见条目。非法操作返回空 optional，输入不变。Remove 只删指定位置；RemoveAllOf 删除 B1/More 给定组件的全部记录含隐藏，保护项、用户线和未命中均返回原值；RemoveUserDividers 只删 A1/B1/A2 的精确新 ID。AutoAdd 只收注册普通组件，数 B1 中普通组件条目（含重复、隐藏与合法缺席插件，不计 Setting、用户线），小于二在 B1 尾 Insert，否则在 More 尾 Insert。此阈值仅用于自动添加。Reset 是纯函数，开关清零由应用层处理。依据为父 design.md §3.1–3.4、子 prd.md:9–17、Other.Config.cpp:383–417。

## 7. 应用与线程归属

BarButtonSetClass 提供 ReadBarLayout()（返回已发布的内存快照，启动前为空）与 ApplyBarLayout(expected,next,resetLegacy=false) → Applied/Unchanged/Stale。提交在 `layoutApplyMutex` 内核对 expected、整组替换内存配置并发布 committedLayout，**不写盘**：设置页运行在共享渲染线程，调用方在 Applied 后 `QueueConfigWrite()`，由设置业务 worker 写出最新完整配置。同值提交为 Unchanged；只清旧开关、布局不变时为 Applied 但不请求重载；布局变化时置原子 reloadPending 并 `UpdateRendering(false)`。

BarRenderLoopCoordinator::RenderFrame 在 offSignal 检查后以 exchange(false) 消费请求：先置命中暂停、递增布局代次，再 Load()（读取 committedLayout，不取配置锁、不等磁盘）。命中冻结记录代次，暂停期间或代次在冻结中途变化时不给按钮命中；按下手势与双击首击保存冻结时的代次，抬起或续接时代次不符、实例不在活动组则不执行。被替换的重复项实例进入退休列表，帧成功呈现、新命中表发布后释放并解除暂停。不使用合成 WM_CANCELMODE（会关闭无关面板）。替换 Setting.cpp 原本直接 SyncLegacyExtensionButtons→Load→UpdateRendering 的路径并更新源码测试边界。

## 8. C4 前的组件页

保留 tabComponent 的 16 个 ToggleRow、ImGui ID、顺序、文字、提示与 ClassIsland 说明（Setting.cpp:4277–4362）。每帧一个模型快照，renderComponentToggle 最后参数从 IdtAtomic<bool>& 改成对应稳定组件 ID（使用 §5 同一映射，区别于 component-explorer 等 ImGui ID）。value 取 B1/More 有无该 ID，包括隐藏项；开启取 AutoAdd，关闭取 RemoveAllOf，随后 ApplyBarLayout。成功更新本帧快照，遇到过期值重新读取。启动未就绪时不允许提交。原组件页多语言排版检查仍适用（setting_design_tests.cpp:7921–7974）。C1 无新文案与图标，IdtI18nKeys.g.h 的生成入口是 Scripts/i18n.ps1:13、496、732。

## 9. 测试与验收门禁

| 用例 | 输入与预期 |
| --- | --- |
| H1 全约束矩阵 | §6 每行 × 四段 × Move/Insert/Remove，覆盖每个拒绝原因；检查输入未变。源/目标越界、同段双向、尾缝隙、原位、空段、跨段、重复 ID 下标及注册单例。 |
| H2 固定区 | 用户线首中尾、连续线、历史 Divider、乱序和错误 Size；缺项、重复、错区、多余、未知逐一整区回默认，其他段不变；旧 A2 双顺序。临时配置对照旧 ButtonLayout 与新 A1 的同一六项旧序，仅有一个新键、只读 Mini 与二次读取。 |
| H3 B / codec | 对 §3 每行比较四段和序列化字段：缺/双边界、Setting 跨区重复/缺失/隐藏、More 中用户线、错误/合法缺席插件 ID、注册单例跨区、非法 Visible 和 Size。显式 B1 四项无截断；二次规范化恒等。 |
| H4 编辑与重置 | 0/1/2/3 个组件时 AutoAdd（混入 Setting、用户线、重复及隐藏）；手动插入第 3/4 项；删一份、跨段删尽同 ID、只删用户线，检查剩余顺序。Reset 三段逐字段等于新 Config，其他显示偏好保持预设非默认值。 |
| H5 导入/落盘 | 16 项逐个、全开、稀疏、全关、已存在/全隐藏的同 ID、注册失败，检查分区/顺序及全 false。复用 config_metadata_tests.cpp:62–90、129–160 的私有 main.json，Write 后新 Config 重读，再初始化无追加或多写。写失败重试及另一并发 Write 的产物只含完整组合。 |
| H6 装箱 | ui3_list_layout_tests.cpp:146–162、181–185 的原固定线与单行细线断言保留，补两档 × 段首中尾 × 半列/隐藏/紧邻交界。连续两条、三条的可见中心差 6 DIP；固定线贡献宽度零，镜像后仍分离。 |
| O1 真实投影 | 产品离屏构造 B1 四组件+两条连续线、More 两份同 ID；检查配置重读后的实际主栏坐标与 More 顺序、独立地址/localState，按下一份另一份不变。覆盖大小切换、Setting 移上栏、空 More、未知及隐藏项无网格。 |
| O2 生命周期/同步 | 两次同布局 Load 的指纹与单例身份完全相同；删除 hover/press 中实例，旧目标不执行；语言切换与设备丢弃后所有副本刷新。连续保存、合并唤醒后最后一帧与磁盘三段相同。 |

**已有断言逐项对照**（位置为修改前）：

| 文件与行 | 旧断言 → 新断言 | 需求 |
| --- | --- | --- |
| Bar.EraserAttribute.Test.cpp:913 | tot==stableButtons.size()+min(count,2) → 将 count 个组件显式放 B1，tot==stableButtons.size()+count，逐项核对 ID 顺序 | prd.md:9、24 |
| 同文件:914 前半 | explicitMore.size()==1 → 此矩阵夹具仍要求一项并校验其为 Setting，另加显式 More 重复与 Setting 上栏用例 | prd.md:9、12–13 |
| 同文件:914 后半 | forcedOverflow.size()==max(0,count-2) → 每个 count 下均 empty()，同时验证显式 More 内容完整 | prd.md:9、24 |
| recall_attribute_tests.cpp:433–437 | Load 源码检查终点从 SyncLegacyExtensionButtons 改为紧随其后的 ResetIconCaches；仍要求起止有效且该段有 ApplyMainPresentationLocked(GetStateModeSnapshot()) | prd.md:9、16，移除动态旧开关投影 |

Bar.EraserAttribute.Test.cpp:696–714、866–901、1014–1017、1105–1106 改夹具为保存/恢复三段，通过模型或配置触发刷新。单独驱动离屏帧消费生产 pending。保留 705–712 的 +1、Pierce/Freeze 检查、904–911 的工具组合、919–922 的地址指纹、1014–1015 的 A 写回保护及 1106 的历史保护；保留原 count 序列和嵌套矩阵。1343–1356 仍验证注册原型与本地化。

**默认 golden。** 原三组 MakeDefault* 与 16 个 false 生成空 B1、More 仅原 Setting 单例（Other.Config.cppm:244–270、417–445）。交界、More 入口、伴随按钮的插入位置、投影、资源与装箱输入相同；新几何分支仅在存在用户线时可达。这是预期像素相同的推导。验收须在同设备、字体、配置和构建档位下分别运行改前/改后 Inkeys.exe --ui3-golden-dump <独立输出目录>（IdtMain.cpp:1914–1919，Bar.Golden.Test.cpp:283–303、600–635）。比场景清单、状态、图像尺寸及每个 .bgra/.png 的所有字节，禁止新增 SKIP、缺图或用新输出覆盖旧基线。

**系统验收。** 以私有配置手写 B1 四组件、两相连 UserDivider，More 放相同组件两份；冷启动、退出、重启各检查一次三段、可见次序和独立命中。真实窗口由主 Agent 在获得 GUI 授权后验收；无窗口 O1 和私有文件重读可先执行。构建使用 InkeysRepo.sln 的 Debug | ARM64（InkeysRepo.sln:12、45、53–66），检查退出码并运行全套 --no-window，具体命令与失败定位见 implement.md。

## 10. 配置 spec 修订索引

实现验收时改 configuration-i18n-and-assets.md:223–336 本场景。227–242 更新 Scope/Signatures 为模型、应用及启动导入；247–259 保留字段形态/命名规则，替换并行投影、旧开关写入、单原型所有权与设备缓存说明，写明官方用户线注册归属。260–277 改 A 校验、旧 A1 触发条件、Setting 跨区唯一、缺标记修复、可见性与缺席插件；将前二容量、活动栈、两组横线、相邻折叠与旧单例警示换为 §1–5 合同。保留 262–265 的运行时伴随与固定线视觉/命中、268–270 的动画交互、279–280 的锁与最终投影。286–310 更新错误表，特别将 292 的非法 Visible 回退改为实际 schema 默认 [MoreBoundary,Setting]，补并存新旧数据、设备丢弃、相邻用户线。314–336 替换对应示例、测试、Wrong/Correct，以 §9 为验证基准。

## 接口留给后续子任务

C2 取 FromConfig/ReadBarLayout 的四段值、注册描述和条目克隆。C3 使用 CanPlace、Move、Insert、Remove、ApplyBarLayout。C4 使用 ReadBarLayout、AutoAdd、RemoveAllOf、RemoveUserDividers、Reset 与同一应用入口。依赖依据为子 prd.md:28–30 与父 implement.md 的任务地图。
