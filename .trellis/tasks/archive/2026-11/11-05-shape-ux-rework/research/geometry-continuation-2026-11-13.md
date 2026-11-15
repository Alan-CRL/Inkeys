# 2026-11-13 续轮调研：图形构造、约束与遮挡

## 范围与共用链路

只读检查 `feature/shape` 当前源码，未改产品文件、未运行构建或测试。编号对应父任务 PRD 的 C4–C7、C9–C11。

- 构造真值与参数：`inkStrokeModelerTest/draw3/shape_core.h:44-68`。geometry/parts 必须派生。
- 平面展开：`shape_core.cpp:56-152`；高级点、弧和立体展开：`shape_presets.inl:34-166`。
- `Validate` 会重新展开并核对 geometry/parts：`shape_core.cpp:429-464`。
- 控制点/编辑：`shape_interaction.inl:131-224`、`:226-405`；会话：`:418-473`。
- 对象属性按钮经 `BuildShapePropertyCandidate` 与 Replace/journal：`Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeInteraction.h:49-57`、`Draw3.ShapeRuntimeActions.inl:563-600`。
- 下一笔工具状态只保存在会话内：`Inkeys/IdtState.h:138-146`。

## C4 三角形模式

- 默认 Drag：`shape_interaction.inl:124-129`；初始外接等腰三点：`shape_core.cpp:124-125`。
- 首次顶点编辑后写 VertexConstruction；校验只要求三个有限点：`shape_presets.inl:2-6`、`shape_core.cpp:143-150`。
- 三顶点已可独立编辑：`shape_interaction.inl:171-175`、`:356-390`。
- 工具只有圆模式，没有三角形模式：`IdtState.h:139-146`、`IdtState.cpp:2397-2399`。

模式不能由几何反推：自由模式初始也可能恰好等腰。需要独立构造字段。自由/等腰共用 Drag，等边使用 CenterRadius；等边可复用正三边形算法但保持 Triangle preset。ConstructionSession::Begin 的额外模式白名单目前只允许圆：`shape_interaction.inl:475-481`。

完成后切换须走共享候选/Replace 历史，参照 SetRegularSides 与 `ShapeUiActionKind::SetSides`。主栏参数行和浮条均需扩展，控件索引派生位于 `Bar.Main.cppm:362-369`。

若模式属于对象并跨文件恢复，construction codec 需升级 v5；当前 tool 固定四字段且严格校验：`shape_uink_metadata.inl:138-151`。旧三角形默认自由，保持外观与自由编辑。

待用户决定：等腰/等边转换的最小调整数学规则；约束模式下拖顶点保持约束还是自动切自由。持久化按图形既有“构造真值可重载”合同纳入 v5，不另询问。

## C5 等腰梯形

- 初始已对称：`shape_core.cpp:130-131`；校验只保证两底水平：`shape_presets.inl:7-9`。
- 拖顶点只同步同底另一端 y，并禁止交叉/退化：`shape_interaction.inl:365-370`。

“默认关”只能表示不强制等腰约束，不能表示初始几何非等腰。与 C4 共用约束字段；开启时对现有对象做最小调整，关闭只解除约束、不改几何。主栏已有对角线参数行，新增等腰开关不能覆盖辅助开关。模式跨文件恢复与 C4 共用 v5。

转换规则随三角形一起确认，建议固定下底与高度，仅对称调整上底两端，或采用用户选择的另一规则。

## C6 点工具起点、首拖与闪烁

- `ConfirmDrag` 对点工具只保存 end，忽略 down：`shape_interaction.inl:556-565`，首拖不会形成首边。
- 一点不满足折线/多边形最小点数，Candidate 为空，起点不可见。
- 新点使多边形暂时无效时 confirmedCandidate/candidate 同时变空；运行时在 Up 后清 L0 并等待下一笔：`Draw3.DrawingController.cpp:2994-3000`、`:18682-18695`。这会清空已确认路径，是闪烁根因。

最小边界：Generate/ConfirmDrag/Back/Forward 一起修。首个实际拖动加入 down/end 两点；单击只加入一点。单点预览可用合法零长度 Line 临时标记，Line 核心未拒绝零长度：`shape_core.cpp:199-213`，shape shader 有固定切向与圆头覆盖：`Inkeys/Inkeys/Drawing/Draw3/Assets/shapePixelShader.hlsl:35-47`。填充模式下临时预览强制可见描边，复用 `:500-506` 的 fallback。

测试：单击点可见、首拖两顶点、新点/暂时自交时旧路径不清空、Back/Forward/Cancel/Fatal 语义；核心序列外，在 `Draw3.ShapeRuntimeTests.inl:106-131` 检查每次 Up 后 preview/renderData 连续性，并用 renderer 像素验证单点。

## C7 完成才闭合、排序与边数

- 当前 Generate 直接用 Polygon 工厂生成预览，三个有效点后提前闭合：`shape_interaction.inl:495-508`；Complete 只返回已闭合候选：`:626-635`。
- 建议 candidate_ 始终为开放 Polyline 预览，confirmedCandidate_ 只保存通过最终校验的 Polygon，用于 CanComplete/Complete；Back/Forward 同步重建两种候选。最终 geometry.vertices 不重复首点，渲染 path 由 `shape_core.cpp:282-318` 闭合。
- 目录当前为正多边形→折线→自由多边形：`Bar.GeometryAttribute.cpp:114-116`，只交换自由多边形与折线，不重排 PresetKind 持久化数值。

边数上限需同步：工厂 `shape_core.cpp:59-61`；对象编辑 `shape_interaction.inl:412-416`；构造会话 `:475-480`；工具状态 `IdtState.cpp:2544-2551`；codec `shape_uink_metadata.inl:144-149`；主栏 `Bar.GeometryAttribute.cpp:489-495`、`:163`、`:540-546`；浮条 `Bar.ShapeEditor.cpp:1176`、`:3082-3084`；控件索引 `Bar.Main.cppm:362-369`；新增 Polygon13/14 SVG 与 `Inkeys.rc:336-356` 资源。

旧读取器会把 v3/v4 的 sides=13/14 判 Invalid，而非未知版本降级：`uink_draw3_import.cpp:163-166`、`:188-196`。新边数对象应写 v5，让旧读取器走标准几何。现有把 13 当非法的测试在 `shape_core_tests.cpp:669-716`，另更新 `shape_uink_tests.inl:197-205`、`:256-262`、`:303-355`。

## C9 棱柱/棱锥底面角度

- SolidConstruction 只有 depth/height：`shape_core.h:54-59`。
- 棱柱/棱锥底面固定 y×0.5：`shape_presets.inl:143-152`。
- 当前只有首顶点与高度点：`shape_interaction.inl:199-201`；圆柱/圆锥已有 ry Axis 点与编辑：`:191-197`、`:346-353`。

最小边界：SolidConstruction 增加默认等价 0.5 的底面投影比；PolygonSolid 增加 Axis 控制点；ExpandAdvanced 与首步预览 `shape_interaction.inl:523-535` 共用投影 helper。比例在等比外框缩放时不变。非默认值纳入 v5，旧对象默认 0.5。

建议定义为“本地未旋转正多边形 y 方向的投影比”，控制点位于底面外接椭圆前端，而不是某个顶点；拖动只改投影比并钳到有效扁平度。该精确定义在最终规划中列出供确认。

## C10 棱锥/棱柱虚线遮挡

- 当前全在 `shape_presets.inl:153-165`：底边按相邻顶点 x 增减与 height 正负；棱柱另一底全实；侧棱在两个相邻底边都隐藏时才虚线。
- 棱锥判定未使用锥顶位置或 |height|，固定底面只改高度不会改变任何侧棱虚实。应改共享展开器的面朝向/可见性，而不是 GPU dash。
- 棱锥侧棱由相邻两个三角侧面的可见性共同决定；棱柱先保留既有教学约定并逐项验证，不无关重写。
- parts.style 被持久化且 Validate 严格核对：`shape_core.cpp:448-464`。改算法必须处理旧 v3/v4 parts 与新派生不一致；新算法对象写 v5 走旧读取器标准几何，旧支持版本读取时按旧算法核验后规范化。
- 当前测试只断言“存在某虚线部件”：`shape_core_tests.cpp:736-740`，renderer 只测覆盖并集：`Draw3.ShapeRendererTests.cpp:275-283`。需逐边几何夹具，覆盖高/矮锥、正负高度、旋转、边数和 C9 投影比。

## C11 弧/扇形预览与跨端点

- 第一步用原样式 Circle(CenterRadius)：`shape_interaction.inl:511-537`；没有虚线预览。
- 当前 EditSession + angleDelta 已解决跨 180° 与 latest-wins 丢采样：`shape_interaction.inl:427-468`；原生输入保留角度轨迹：`SelectionOverlay.cpp:140-149`。
- EditShape 在扫角变号、趋零或超过 2π 时失败：`:313-327`；EditSession 钳在最近有效态：`:451-466`，所以跨端点后卡死并抬手回跳。

最小边界：第一步临时预览样式改 Dashed，扇形 Fill 下补可见描边，最终对象保留原线型。“确定半径用虚线”按整条临时圆周虚线理解，不新增半径辅助线。

跨端点保留累计角；候选扫角 q 与原 sweep 变号时，用 -q 并交换起/终点表示，使固定另一端和旋转方向保持，反向跨回可逆。浮层按下 handle id 在手势期间固定：`Bar.ShapeEditor.cpp:1891-1917`、`:1947-1968`，角色交换时按压视觉也需同步。现有 construction 足以保存交换结果，无需新字段。零扫角仍保持上一有效候选；整圆上限 2π 保持现状。

测试：Arc/Sector × 正/反 sweep × 起点 102/终点 103；跨零继续、跨回、latest-wins 合并、Commit 不回跳、变换对象；虚线首步与最终样式；保存/重载。

## 回归入口

- 共享核心：`inkStrokeModelerTestTests/shape_core_tests.cpp` → `--shape-core-only`
- 文件协议：`shape_uink_tests.inl` → `--uink-file-only`、`--uink-presentation-only`
- 生产输入与 Replace/journal：`Draw3.ShapeRuntimeTests.inl` → `--draw3-shape-core-test`
- 实虚线与单点像素：`Draw3.ShapeRendererTests.cpp` → `--draw3-shape-renderer-test`
- 主栏/浮条：`--ui3-geometry-attribute-test`、`--shape-tool-state-test`、`--ui3-shape-editor-test`
- 新字段另跑 `--draw3-whiteboard-storage-test`、`--desktop-autosave-only` 与 i18n 检查。

## 2026-11-13 用户决策：三角形与梯形约束

C4 采用「固定底边、持续约束」：以默认三角形的底边为基准，切换时固定底边两端，只移动对顶点到底边垂直平分线；等边同时调整高度。等腰/等边模式下后续拖动也保持对应约束，不自动切回自由。C5 梯形沿用同一方向。持久化按图形既有“构造真值可重载”合同纳入 v5，不另询问。
