# Research: 主栏按钮表现写入库存与组件重载

- Query: 分离式绘制时添加组件为何令 Freeze 从 2×1 恢复 2×2；如何把全部主栏按钮尺寸、显隐和业务表现统一为状态投影，避免外部入口覆盖有效属性。
- Scope: internal；UI3 主栏注册表、A1/B/A2、组件设置、初始化、表现计算及现有无窗口回归。历史计时器生命周期由另一个研究记录深入负责。
- Date: 2026-10-14
- Baseline: 主会话提供 feature/mode、HEAD 2dd3b344；本研究不执行 Git 操作，不构建，不启动 GUI 或 Computer Use，只写本研究文件。

## Files Found

- `Inkeys/Inkeys/UI/Bar/Bar.Button.cppm`：主栏按钮的有效 size/userVisible/hide、注册默认、稳定列表与 StateUpdate 公共/内部入口。
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`：全部主栏网格尺寸和运行时显隐生产写入；注册、三区 Load、A2 投影、工具文案及 Recall 事件接线。
- `Inkeys/Inkeys/UI/Bar/Bar.A2.h`：已有纯 A2 投影策略；Separate、SelectionEmpty、PPT、Whiteboard、FreezeAvailable 的矩阵。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`：UpdateRendering 的序列化更新与通知；内容、场景、工作区的状态发布入口。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`：BarUISet 的 Recall 状态、计时器和原有方法声明。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：尺寸/显隐消费者、More 高亮直写、动画批次、隐藏按钮锚点及 Recall 时间推进。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：主栏 IsVisible/Disable 交互门禁，点击与共享按压/悬停算法。
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`：预设、组件、Load/StateUpdate 初始化；折叠首帧的合法视觉终态初始化。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：16 个旧组件开关写入后调用唯一 SyncUi3BuiltInComponents 入口。
- `Inkeys/IdtPlug-in.cpp`：三处外部 UpdateDrawButtonStyle 后又 UpdateRendering 的冗余分支入口。
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp`：真实 barUISet、Load、模式切换、历史墙钟、布局动画和 D2D 离屏回归。
- `InkeysHeadlessTests/recall_attribute_tests.cpp`：当前按旧函数名称检查生产 Recall/精简态接线的源码断言。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp`：独立 Surface/分页按钮；共享 BarButtonClass 并不表示其属于主栏列表。
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp` 与 `Bar.RecallAttribute.cpp`：独立属性按钮及弹窗命令身份，不是主栏列表的有效网格投影。

## Findings

### 1. 已确认的故障链

1. `Setting.cpp:564-568` 的 `SyncUi3BuiltInComponents()` 调用 `barUISet.barButtonSet.SyncLegacyExtensionButtons()`，然后 `barUISet.UpdateRendering()`。
2. 16 个开关均调用此方法：`Setting.cpp:6841,6874,6907,6962,6995,7050,7083,7144,7183,7222,7261,7300,7339,7437,7464,7497`。例如 Explorer 在 `6836-6841` 更新 `setlist.component.shortcutButton.appliance.explorer`，先 `WriteSetting()`，后主栏同步。
3. `Bar.Button.cpp:1390-1393` 的 SyncLegacyExtensionButtons 直接调用 `Load()`。因此一个 B 组件开关会重建 A1+B+A2 全部列表，而不是仅增减 B。
4. `AppendFixedButtons():1203-1217` 每次把配置条目的 Size 直接写到有效 `button->size`。Freeze 在构造时为 `twoTwo`（`604`），注册保存该默认（`RegisterButton():139`）；NormalizeFixedZone 最终采用注册默认（`1128` 附近）。所以 Load 把运行时 `twoOne` 覆盖为 `twoTwo`。
5. `Load():1386-1387` 只重新套一次 Recall 投影，没有重新套 A2。
6. 随后的 UpdateRendering 调用 StateUpdate（`Bar.Main.cpp:278-293`）。但 `UpdateWhiteboardButtonStyle():952,955` 仅用业务 styleKey 短路；组件列表变化不改变 Separate/tool/content/scene/freeze key，`968-982` 的 A2 尺寸及显隐重新应用被跳过。
7. Pierce 不属于 AppendFixedButtons，因而仍为 `twoOne`；Freeze 已为 `twoTwo`。这精确产生用户反馈的上下半高组合被打破，而且后续同状态刷新无法自愈。

这是两个所有权层混用：Load 写的是“配置/注册基础尺寸”，字段却也是渲染使用的“运行时有效尺寸”；A2 又把缓存的“业务条件未变化”误当成“按钮的有效属性未被别处覆盖”。单纯在组件回调补一次 A2 或清除 styleKey 可修局部症状，但不满足本轮统一全部主栏表现的要求。

### 2. 主栏有效字段写入库存

以下行号为研究时基线；实现移动后以函数为准。

| 写入方 | 范围与字段 | 当前类别 | 本次建议 |
| --- | --- | --- | --- |
| PresetInitialization `215-737` | Divider、Select、Draw、Eraser、Geometry、Recall、Redo、Clean、Whiteboard、Pierce、Freeze、EndShow、Setting、More 的默认 size/hide/userVisible；初始 SVG/文字 | 构造默认，合法 | 保留构造初始化；运行后不能依赖它重新覆盖有效状态 |
| RegisterButton `128-139` | userVisible、defaultSize、defaultUserVisible | 注册元数据，合法 | 可继续建立基础元数据，不把注册默认当运行时最终投影 |
| RegisterBuiltInComponents `752-789` | 内置组件 twoTwo/hide=false、初始 PNG/文字及局部 state | 构造默认，合法 | 保留组件登记与业务动作 |
| AppendFixedButtons `1214-1215` | 全 A1/A2 userVisible 与有效 size | 配置与有效表现混用，故障源 | Load 只更新基础尺寸/配置可见性和成员；有效 size 由统一状态入口应用 |
| GetLegacyExtensionButtons `1270-1272` | 启用组件 userVisible=true/hide=false/defaultSize | 配置成员投影与有效表现混用 | 成员/基础元数据由 Load 决定，最终有效表现由统一入口应用 |
| AppendBoundaryDivider `1287-1289` | 两条稳定 Divider 可见、oneTwo | 静态元数据与有效表现写入 | 保留稳定对象、注册默认；统一入口保持该有效值 |
| Load explicitMore Setting `1351-1353` | Setting 可见、twoTwo | 同上 | 元数据与成员处理；统一入口确定最终有效值 |
| UpdateWhiteboardButtonStyle `968-982` | Whiteboard/Freeze/Pierce/EndShow size/hide；Pierce/Whiteboard 内容 | 派生表现，但入口与缓存混合 | 几何/显隐进入统一投影；内容缓存只能抑制无变化的内容动画，不能跳过重载后的几何一致性 |
| PresetHoming `1441-1465` | Eraser/Geometry hide；Select 工具文案；调用 Recall 有效写入 | 派生表现与面板副作用混合 | 显隐/内容走统一结果；面板归位保留原状态语义，不能制造先显示后隐藏的中间投影 |
| CalcState `1469-1508` | Pierce、Clear、Select、Draw、Eraser、Geometry、Freeze、Whiteboard、Setting 的 Selected/Disable/None | 派生业务状态 | 纳入统一解析与应用；清空/定格原禁用行为保持 |
| ApplyRecallChromeVisualsLocked `1552-1571` | Undo/Redo size/hide、recallAttribute popup 状态 | Recall 事件分支直接应用有效表现 | 事件只更新 RecallChromeState/期限/面板相关状态；统一主栏投影读取同一 RecallChromeView 后应用有效 size/hide |
| RenderLoop SubmitTargetsAndLayout `1617` | More localState Selected/None | 外部派生表现直写 | More 高亮从 fold/moreExpanded/HasButtons 统一派生 |
| RenderLoop More 无内容 `5148` | More localState None；关闭 moreExpanded | 同上，有真实面板状态修正 | 状态修正可保留，但最终高亮需统一投影；不要再留第二个写高亮入口 |

工具内容入口：Draw `913-936`、A2 `944-987`、Eraser `989-1004`、Geometry `1006-1027`、Select `1446-1462`。全部已调用 `TransitionContent()`，其内部仅在内容实际变化时动画（`46-59`）。统一入口仍应使用该内容转换，不能直接改 `name.content` 或每次重启动画。

### 3. 需要保留的主栏语义矩阵

- Select、Draw、Geometry、Eraser、Clean 基础为 twoTwo。非白板、Selection 且当前页无内容时隐藏 Geometry/Eraser；Select 保持可见。其他工具或有内容 Selection 保持完整工具按钮。
- Clear 仅 Selection+空页为 Disable；空页绘制/Pierce 保持可点以回到 Selection，双击和 Clear 拒绝语义不变。
- A2 使用已有 `ResolveBarA2Projection()`，不另复制模式判断。Separate 绘制/非空 Selection 时 Pierce 上 twoOne、Freeze 下 twoOne；空页 Selection 时隐藏 Pierce，Freeze twoTwo；PPT/Freeze不可用隐藏 Freeze，可用 Pierce twoTwo；Whiteboard 无 Pierce。
- Whiteboard 的入口关闭由既有 IsWhiteboardFeatureEnabled 决定，其 twoOne/twoTwo 与原工作区行为保持。EndShow 在放映时可见且 twoTwo。
- Recall/Redo 的 twoOne/twoTwo/隐藏由锁存 RecallChromeState 和精简 Selection 门禁决定。不得从实时 availability 直接决定布局，也不得因组件重载清除历史保护。
- More 基础 twoTwo，常驻完整布局；fold 时仍保留列表用于完整展开宽度判断，视觉由现有收起动画隐藏。Selected 由有效面板状态派生。
- Setting 在 explicitMore、内置组件在主栏前两个及 forcedOverflow 中分布；组件增删不改变 A1/A2 配置和稳定官方按钮身份。
- Divider oneTwo，只占原网格封列/间隙，纯 Shape 0.30 光影，不能因统一所有按钮而成为普通可按按钮。

### 4. 唯一计算/应用入口的最小建议

1. 保留 `BarButtonSetClass::StateUpdate(const StateModeClass&)` 作为已有主栏统一入口，内部先一次性捕获同组依赖：工具快照、模式设置、内容、PPT/Whiteboard/Freeze可用性、fold/More状态、RecallChromeView 和注册/配置基础尺寸。
2. 在小型内部纯投影 helper/结构中计算各主栏 ID 的最终 size/hide/业务高亮及内容语义。基础 size 从独立元数据来源取，动态 A1、A2、Recall 覆盖按明确优先级计算，不从当前有效 size 反推默认。
3. 一个应用阶段只提交一次最终有效 size/hide/Selected/Disable，并通过既有 TransitionContent 更新内容。相关动作入口只修改业务状态、布局成员或配置基础值并走统一刷新。缓存只保存已应用最终结果/内容 key；重载有基础/成员修订时必须重新一致化。
4. Load 不再临时改已在显示的官方按钮有效尺寸。新列表替换与统一应用应沿现有更新序列化边界设计，确保消费者不能在配置基础写回后读取到一帧未投影的 size。不要引入另一个独立画布/状态链。
5. 所有 Recall 锁内事件保持状态/期限更新；锁外再发统一更新，避免 StateUpdate 重新取得 recallChromeMutex 而死锁。不要持该锁去调用全局 UpdateRendering；具体事件/计时顺序见另一研究。
6. 提前发布统一有效目标后，RenderLoop 继续负责目标坐标、尺寸像素动画、内容推进、命中、dirty、批次/中点及隐藏锚点。无需强制 SetDirect 或取消动画来“恢复尺寸”。相同输入重复刷新不能重启动画。
7. 外部 `IdtPlug-in.cpp:499,571,740` 的 UpdateDrawButtonStyle 后紧跟 UpdateRendering；可删除冗余分支调用或让公共 wrapper 转统一刷新，避免工具表现还可绕过主栏统一入口。无必要扩大业务副作用。
8. 初始化 `Bar.Initialization.cpp:168-173` 与 GUI fixture `Bar.Presentation.Test.cpp:816-820` 的 Load/StateUpdate 可继续使用同一入口；构造/首次折叠终态仍保留。须保证早期 CLI/预设尚未初始化时的既有 contentStateUpdatesReady 门禁，不能访问空 preset/wordMap。

可用“注册基础 size”已有 registration.defaultSize，或明确配置基础字段；当前规范将所有合法配置 Size 纠正为注册默认，因此本轮不必开放新的用户改尺寸功能。重要的是移除 Load 对有效 size 的覆写，并让统一入口覆盖所有特殊/普通主栏按钮。

### 5. 合法视觉写入与非主栏边界

以下不能因本轮要求而全局禁止或迁入主栏投影：

- `Bar.RenderLoop.cpp:2511-3053` 的 `SetButtonPositionTar`、各 size 分支 SetTar、内容动画推进及 `AnchorHiddenBarButton` 是有效逻辑网格到当前动画几何的 owner。统一状态层只决定目标网格，不直接操控 val。
- `Bar.Initialization.cpp:177-189` 为浮动启动设置隐藏首帧的位置/透明度终态，是构造时的合法视觉初始化。
- `Bar.Interaction.cpp` 与 `Bar.Scene.cpp` 的 emph、pressScale、hoverStage、StopHover/Retarget 是短期指针反馈，不是业务有效 size/hide；五秒悬停 Fading 是合法动画，与三秒 Recall 布局保护不同。
- `Bar.Scene.cpp:857-865,898-910,1506-1507,1711-1712` 写 Scene 自持按钮的 size/userVisible/hide；这些 Widget 属于独立分页 Surface，不是主栏 barButtonSet；保持其已有 spec->target 分层。
- `Bar.EraserAttribute.cpp:112,296` 是属性面板独立按钮构造和可用性投影。
- `Bar.RecallAttribute.cpp:90,252` 是弹窗历史按钮构造及身份/样式应用；与主栏稳定 Recall/Redo 是不同实例。保持 same-frame icon/name/preset 身份合同及动画推进。
- 主按钮为 80 DIP superellipse，点击脉冲和停靠尺寸 owner 已在 RenderLoop，不是 four-grid BarButtonClass。此次规范全主栏网格按钮，不应顺手重构其脉冲/停靠状态机。

禁用注意：`Bar.Interaction.cpp:3943` 对 state==Disable 直接跳过点击；主栏 Recall 在灰色连点时需延长既有三秒保护（`Bar.Button.cpp:474-475`）。不能仅为形式统一把历史按钮设成 Disable 而无配套特殊命令门禁，这会改变现有行为。统一不可用表现需要仍保留历史命令专用反馈资格和当前禁用透明度。

### 6. 真实 Load 先失败后通过的无窗口入口

使用已有 `Inkeys.exe --bar-eraser-offscreen-test`；`IdtMain.cpp:1197-1198` 在普通 GUI 初始化前分派真实离屏 runner。不要新建猜测 CLI 参数，也不要执行 Presentation GUI runner。

当前 fixture 适合直接扩展：

- `Bar.EraserAttribute.Test.cpp:325-333` 使用真实 `owner=barUISet`，加载打包 i18n/字体，PresetInitialization 后 Load。
- `328-332` 先在内存把 A2 Freeze Size 规范化为注册 TwoTwo，防止早期 CLI 的 Load 触发迁移 Write。新增 fixture 必须延续这一准备，不能意外写用户配置。
- 当前 fixture 尚未 RegisterBuiltInComponents；新增组件场景应调用真实注册入口，它加载内置 PNG，不执行组件 click/action，不打开资源管理器等 GUI。
- `370` 开启 contentStateUpdatesReady；设置 Separate 并切 Pen，已有断言 `391-395` 确认 Pierce/Freeze 同为 twoOne。
- 保存并修改真实旧开关，例如 Explorer false->true，再调用 `SyncLegacyExtensionButtons()` 和 `owner.UpdateRendering()`，完全复用设置 production 入口链，断言两者仍 twoOne、Pierce在Freeze前、组件出现在 B。旧实现此断言必失败，因为 A2缓存 key 不变。
- true->false、false->true、0/1/2/3+ 组件及 overflow/explicitMore、mode/tool/content不变下反复同步；保持官方对象身份，配置 A1/A2 不随有效尺寸改变，迁移不产生实际写盘。
- 覆盖 Combined/Separate×SelectionEmpty/nonempty/Pen/Shape/Eraser/Pierce×desktop/PPT/Whiteboard 必要边界；同一 StateUpdate 重复120帧保持 size/hide，内容转换同目标不重启。
- 复用实际布局目标/动画 helper 验证重新加载后 x/y/h/w从当前值继续、穿透上定格下、隐藏锚点/hit过滤、非相关按钮目标不跳动。现有测试 `454-531` 已有四X排序和隐藏Pierce的位置动画 fixture，可在组件重载场景组合，避免只断言纯策略。
- 历史 timer/Load 交错和墙钟测试已有 `533-578`；具体新增事件矩阵由历史研究负责。不能只测试同一个 projection helper 就宣称真实 Load 已修复。

`InkeysHeadlessTests --no-window` 的纯策略与源码合同不能代替真实 Bar Load runner：Headless 不直接运行主程序 module 入口。`recall_attribute_tests.cpp:397-425` 当前硬编码寻找 PresetHoming/CalcState/ApplyRecallChromeVisualsLocked，若统一入口改变必须更新该合同为“唯一最终投影和精简态优先”，不能保留仅为测试字符串存在的旧写入路径。

## Related Specs

已读取 `.trellis/workflow.md`、当前任务 prd/design/implement、native-desktop index/cpp-conventions/configuration-i18n-and-assets 与 rendering-and-ui 相关完整合同，以及 build-and-compatibility 验证边界。

- `configuration-i18n-and-assets.md:153-267`：注册默认、A1+B+A2、严格 normalization、Size纠正、配置与hide分层、稳定Divider/More/组件容量。
- `configuration-i18n-and-assets.md:422-473`：选择与穿透矩阵、Pierce长期隐藏成员、Clear不可用、独立短标签动画。
- `configuration-i18n-and-assets.md` 的主栏图标顺序/方向合同：列表与上下关系不改、折叠完整宽度、排序/动画/hidden锚点。
- `rendering-and-ui.md:45-112`：Freeze唯一业务owner、Recall锁存几何和三秒保护、精简Selection最高优先级、灰色连点延期。
- `rendering-and-ui.md:406-431`：独立Surface/分页按钮与共享视觉helper边界。
- `rendering-and-ui.md:1333-1490`：第三光透明度、五秒悬停、current/target、内容转换和批次中点。

主会话应通过 update-spec 更新当前仍允许 PresetHoming 临时改变 size/hide 的旧表述，新增主栏有效表现唯一写入入口以及组件 Load 不覆写动态尺寸的合同。本研究不修改 spec。

## External References

本次为内部源码所有权/接线问题，无需外部文档、网络查找或依赖版本变更；不把以前自动验证结果宣称为本轮已验证。

## Caveats / Not Found

- 研究确认因果和可以实际执行的既有无窗口回归入口；未运行回归、构建或 GUI，不宣称视觉已修复。
- UpdateRendering 的 static mutex 只序列化同入口 StateUpdate；Load 当前在其外调用。统一修复需核对组件重建与有效投影的发布顺序，不应仅重新排列两句而保留瞬间写回。
- 所有主程序 StateUpdate 使用唯一 barUISet；其内部 Homing/Recall 也引用全局 owner。无需为了测试扩大成任意实例泛型，真实离屏 fixture 已用该全局 owner。
- 注册默认 Freeze 是 TwoTwo，而 schema default A2 初始可能为 TwoOne，Load会规范化；测试须沿用已有内存规范化准备避免写盘。
- GUI Presentation fixture 不是无窗口验证，不启用或运行。真实鼠标/触摸、组件设置视觉、动画中途点击与PPT仍人工验收。
- 此处不深入改计时器、五秒悬停或三秒历史策略，避免与另一个研究主题重复；只是列出有效表现直写的位置和合同风险。

### 7. 等宽布局变化与布局 fingerprint（后续补充）

- `Bar.RenderLoop.cpp:2445-2449` 当前 mainBarLayoutChange 只比较 IconOrder 与总目标宽度；`653` 缓存 mainBarLayoutWidth。仅“宽度没变”不能证明按钮几何没变。
- 例如 Undo/Redo twoOne+twoOne 同列与 twoTwo 命令单列总宽相同，但身份、Y位置、有效高度和隐藏锚点都变化；Freeze/Pierce 一列的 twoTwo 与两枚 twoOne 也同宽。普通组件等宽替换/成员顺序变化亦同理。
- 建议在原 StateUpdate 统一投影基础上，供 RenderLoop 比较小型布局 fingerprint/有界序列：稳定实例身份或内部ID/preset、成员顺序、有效网格 size、IsVisible结果。运行时 Pierce 没有持久化 ID，不能依赖非空string ID；可用稳定对象身份或内部preset标识。含隐藏成员的 size/visibility 是合理的，因为现有 packing在部分 size分支先封列再跳过隐藏项，且隐藏锚点可能变更。
- 指纹不包含 Selected/Disable、hoverStage、emph、内容颜色/文字、pressScale、瞬时动画 val；这些变化不应重开主栏布局批次。IconOrder/方向/Fold/DPI仍沿各自既有真实几何变化机制，不重复堆入每次status刷新。
- 使用指纹变化补充原 mainBarLayoutChange，继续复用 `2448-2525` 的 CanJoin<=0.5、后半程完整新批次、当前值续接及换边关键帧逻辑；无需新的布局框架。mainBarLayoutExpands仍用总宽度判断Back曲线，等宽变化按现有非扩张平滑曲线。
- 同输入刷新不产生新fingerprint，不递增布局revision；hover和禁用透明度动画也不能伪装成布局变化。新增回归需覆盖等宽布局转换、组件等宽替换/顺序、反复StateUpdate及hover状态变化不重开布局批次。
