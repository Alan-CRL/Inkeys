# 图形交互与样式整改 — 技术设计

各子任务的需求见对应的 `prd.md`；本文只写跨子任务的技术边界、数据流和取舍。

## 1. 子任务边界与共享文件

| 子任务 | 主要文件 | 与其他子任务共享 |
|---|---|---|
| edit-performance | `Draw3.Bridge.*`、`Draw3.ShapeRuntimeActions.inl`、`Draw3.DrawingController.cpp`（预览路径）、`Draw3.RendererShapes.cpp`、`SelectionOverlay.cpp`、`Bar.ShapeEditor.cpp`（意图发送、跟手） | ShapeEditor 同时被 visual 和 interaction 修改 |
| edit-interaction | `shape_core.*`、`shape_presets.inl`、`shape_interaction.*`、`shape_uink_*.inl`、`uink_draw3_*`、`Draw3.InkDocument/InkHistory`（辅助标志位） | 浮条开关的 UI 由 visual 负责 |
| edit-visual | `Bar.ShapeEditor.cpp`（绘制、浮条、颜色层）、新增的 Thumb 绘制 helper（`Bar.Rendering.*`）、`Bar.RenderLoop.cpp`（主栏 Thumb 改用 helper）、`SelectionOverlay.cpp`（按键） | — |
| toolbar-category | `Bar.GeometryAttribute.cpp`、`Bar.Main.cppm`（Geometry* 索引）、`IdtState.*`（辅助开关的工具状态） | 辅助开关的状态字段与 interaction 共用 |
| construction-prompt | `Bar.ConstructionPrompt.cpp`、`Bar.Interaction.cpp`（自动收起）、`shape_interaction.*`（Forward）、`Draw3.ShapeInteraction.h` | Forward 与 interaction 的 ConstructionSession 共用 |

## 2. 编辑预览数据流（edit-performance）

```
指针 move ──► ShapeEditor（UI worker）
              ├─ 本地：用共享核心 EditShape(快照对象, 手势) 计算候选 → 立即重绘外框和控制点（R3）
              └─ 写入「最新预览槽」（每个 session 1 个，latest-wins，带 gesture 序号）
绘制线程每帧 ─► 先处理有序命令队列（Begin / Commit / Cancel / Close / 属性），再取一次最新预览槽
              └─ 对象留在 L1：每帧只在 L1 重画这一个候选对象，叠在 L2 之上上屏
Commit ──────► 更新 CPU 文档和 journal（与现有相同），L1 改为显示最终对象，L2 不动
编辑结束 ────► 落定：把最终对象光栅化进 L2 一次，释放 L1
```

- **预览槽**：放在 Bridge 中，与命令 FIFO 并列。用原子交换或加锁覆盖写入。槽里带 session/gesture 身份，绘制线程取出后照常校验身份。Commit 中携带最终手势参数，即使最后一个预览被覆盖，提交也不受影响。终结意图仍走 FIFO 和现有的有界重试。
- **对象留在 L1**（用户方案）：刚完成并进入编辑态的 Shape 总在最上层。抬笔时不光栅化进 L2，而是作为 pending shape 留在 L1。每帧上屏为 L2 加 pending shape，编辑只重画 L1。
  - 落定触发：新的接触 Down、工具/页面/工作区/Clear/Esc/浮层关闭，以及任何读取 L2 完整像素的路径。需要逐一列出并接入：可信 L2 快照、缩略图、PPT 可见导出、设备重建、尺寸变化。
  - 状态：页面运行时记录 `pendingShapeId`，表示「L2 不含该对象」。
  - 编辑期间撤销该对象：直接丢弃 pending，L2 不变。
  - 失败处理：落定失败时走现有的权威重放路径。
  - 冻结背景（PreviewReplacement）方案作废。
- **缓存**：预览阶段的 revision 使用临时命名空间，不写入 128 项长期缓存。Commit 时才分配正式 revision。
- **浮层**：拖动期间窗口边界按块扩大（例如按 256DIP 取整），只增不减，手势结束后再收缩；内容不变时不调用 Present。
- **计量**：在预览槽中记录入队时间戳，绘制线程上屏时计算延迟，同时累计被覆盖的预览数，通过 RuntimeMetrics 或诊断日志输出。

## 3. 控制点模型与辅助显示（edit-interaction）

- `BuildEditHandles` 改为按 preset 的表驱动描述生成控制点：控制点种类、位置推导函数、编辑函数、是否显示旋转钮、外框是否有角点。外框统一由共享核心给出 `frameRect`（本地坐标，基于含线宽的包围盒外扩 8DIP 的间隙，按 displayScale 换算），UI 只负责绘制，保证外框线和外框角点在同一位置。
- 圆：Drag 模式下第一次拖动半径点时，把 construction 转为 CenterRadius（保持圆心和半径不变）；半径点的位置取 construction.end 的方向。Drag 模式默认放在 45°。
- 椭圆焦点：内部规范化为 a≥b（方向写入 transform 旋转），焦点控制点 id 为 106/107；拖动遵循 PRD 的 A 方案。
- 弧角编辑：EditSession 保存手势开始以来的累计角，与构造时 ObserveRaw 的累计方式相同。
- **辅助显示的模型**：`ShapeStyle.auxiliary`（位域，包括 Center、Foci、Diagonals）。
  - 渲染：`BuildRenderData` 派生附加的 segment 和 dot，沿用同材质的覆盖并集规则。辅助点用填充圆，对角线使用虚线 dash。
  - 命中：辅助元素不参与本体命中。
- **持久化**：`inkeysShape` v4 增加字段 `aux`；v1/v2/v3 读入时 aux 为 0，圆和椭圆按旧行为不显示辅助元素，不补默认值，以免旧文件外观改变。UInk10 标准降级时，把辅助元素作为额外的标准部件写出，与复合部件共享 undoId 和 reference；读回时如果识别到 v4 根，就按 aux 重建，把这些部件视为派生内容，不当作独立对象。
- 浮条和主栏的开关通过 `ShapeUiAction::SetStyle`（对象）和工具状态（下一笔）两条路径分别处理。

## 4. 浮条与颜色层级（edit-visual）

- 浮条变成独立的面板对象：`attributeList`（SingleRow）加主栏 Divider。显隐使用 0.4s timeline 和 CanJoin，隐藏成员锚定到相邻成员。整体用 `BarAttributePanelMotion` 回弹。新会话时 `ResetLayout()`，所有成员 SetDirect。
- 颜色层级在同一个 SelectionOverlay 窗口中按 z 序绘制：浮条 < 色板层 < 取色器层。每层有各自的 motion 和 hit 区，输入按 z 序从顶层开始命中；点击空白处或按 Esc 时从顶层开始逐层关闭。取色器复用 `ColorPalette`、`GetBarColorPickerColor`、`ProjectBarColorPickerColor`。标记点和预览部分新写，色板交互不依赖主栏的全局状态。
- Thumb helper：`DrawBarSliderThumb(renderer, dc, center, outerDiameter, centerDiameter, accentOpacity, lightWeight, opacity, icon?)`。主栏粗细滑块迁移到这个 helper，要求像素不变（由回归测试保护）。
- 透明度：浮条使用 residentSurface 加 `GetThemeResidentSurfaceOpacity`，内容使用 `GetThemeResidentContentOpacity`；两个颜色层固定使用 `BarDrawAttributeSurfaceOpacity`（0.95）。`SetResidentOpacity` 和主题切换会向 RenderPipeline 请求 `Client::SelectionOverlay` 重绘。

## 5. 主栏面板分类（toolbar-category）

- 目录增加 `category` 字段（Regular/Solid/CoordinateFunction），移除圆角矩形条目。`categoryLayout_` 改为在分类切换时重新解析可见子集，`slot[i]` 改用按目录下标建立的映射。顶部分段控件使用 3 个 OneOne 宽度的 segment 按钮，写法沿用橡皮的 Selected 加 frameColorOverride。
- 正多边形图标轮播去掉 SetTimer，改为由 Advance 按 deadline 推进，到期时请求一帧。
- 辅助开关进入参数行：`[构造方式按钮…] | [辅助开关]`，开关是图标型的 OneOne 按钮，处于 Selected 状态时表示开启。

## 6. 绘制提示（construction-prompt）

- `ConstructionStatus` 增加 `totalSteps`（0 表示不定步数）和 `canForward`；`ConstructionSession` 增加 redo 栈。新增 `ShapeUiActionKind::ConstructionForward`。全局 Redo 在构造过程中执行 Forward。
- 提示面板改为：环形进度（DrawProgressRing 加公开封装）、文字（BarUiWordClass，13 Bold）、按钮 list（OneOne，最多 4 个）。表面材质与底栏提示保持一致；动画复用 bottomDock 的进度曲线和三阶段换字状态机（抽成可复用的 helper）。
- 自动收起：在 `ProcessAcceptedStrokeNotices` 中，如果 `ShapeUi` 快照处于 construction active 状态，就跳过 Drawing 策略。

## 7. 兼容与回滚

- 文件：v4 只是新增字段。旧版本读取 v4 文件时，走标准几何降级，能看到辅助元素；新版本能读 v1–v3。圆角矩形的枚举保留。
- 每个子任务各自独立提交。edit-performance 可以单独回滚（预览槽和轻量预览有开关，可退回现有路径）。主栏 Thumb helper 的迁移出问题时，可单独回退主栏调用点。
- spec 更新：`shapes.md`（P3 合同的控制点、外框、辅助显示、v4、Forward）、`rendering-and-ui.md`（分类面板、浮条、颜色层级、提示）、`bar-theme-material.md`（编辑器的常驻透明度）。

---

# 2026-11-13 实机验收续轮技术设计

## 8. 变更边界与最小行为差

当前差距不是单一绘制问题：浮条刻意冻结、点工具把开放预览与最终闭合候选混用、三角形/梯形没有约束真值、棱柱/棱锥投影固定且部件虚实规则不含完整面朝向、完成沿没有保留给提示。修改必须落在这些事实源，不在抬手回调或 renderer 末端遮盖症状。

预期修改边界：

- 共享模型与几何：`shape_core.h/.cpp`、`shape_presets.inl`、`shape_interaction.h/.inl`，负责约束、构造/编辑、开放预览、底面投影、遮挡和弧跨端点。
- 文件与历史：`shape_uink_metadata.inl`、`shape_uink_standard.inl` 及必要的导入核验，负责 v5 和旧部件规范化；不改变既有 PresetKind/FillMode 数值。
- 产品动作与状态：`Draw3.ShapeInteraction.h`、`Draw3.ShapeRuntimeActions.inl`、`Draw3.DrawingController.cpp`、`IdtState.h/.cpp`，负责当前对象约束动作、下一笔会话状态和完成沿。
- UI：`Bar.ShapeEditor.cpp`、`Bar.GeometryAttribute.cpp`、`Bar.Main.cppm`、`Bar.ConstructionPrompt.cpp`，以及确有首次展开问题时的 `Bar.RenderLoop.cpp`；负责布局、视觉和输入，不复制几何算法。
- 资源与测试：必要的新 SVG、RC、二十语 JSONC/生成头，以及现有 core/runtime/renderer/UI/storage 测试文件。不新增测试系统，不新增 HWND/线程，不修改普通笔路径，不处理 handoff 中无关遗留。

## 9. 约束真值与 v5

### 9.1 模型

在 `ToolSpec` 增加独立的平面约束枚举字段，默认 Free：Triangle 支持 Free/Isosceles/Equilateral，Trapezoid 支持 Free/Isosceles，其余 preset 必须 Free。自由与等腰三角形均使用 Drag；等边使用 CenterRadius。`ConstructionSession::Begin` 接受这些合法组合，拒绝其他 preset 的非 Free 值。

在 `SolidConstruction` 增加 `baseProjection`，默认 0.5；仅 Prism/Pyramid 可非默认。它是本地未旋转正多边形 y 坐标的乘数，不是绝对像素 ry，因此外框等比缩放时不变。共享一个投影 helper 供首步预览和最终展开使用；Axis 点位于投影后的底面前端，拖动只改比例并钳到非退化范围。

### 9.2 三角形转换与持续约束

规范顶点顺序保持当前的 apex=0、底边右/左=1/2；对象旋转留在 transform，转换在本地构造空间进行。

- 转 Free：仅解除约束，顶点不动。
- 转 Isosceles：固定 v1/v2；保留 v0 相对底边的有符号高度，将 v0 移到 `baseMidpoint + normal * signedHeight`。
- 转 Equilateral：固定 v1/v2；v0 移到 `baseMidpoint + normal * sign * sqrt(3)/2 * baseLength`。
- Isosceles 拖 apex：固定底边，指针投影到底边垂直平分线；拖底边端点时另一端固定，保留之前有符号高度并随新底边重建 apex。
- Equilateral 拖底边端点：另一端固定，按新底边重建等边 apex；拖 apex 时固定底边中点和方向，按指针在底边法线上的目标高度等比更新底边两半长，再重建 apex。
- 所有约束编辑沿用最近有效状态钳制；共线、零底边、非有限值不提交。

### 9.3 梯形转换与持续约束

顶点顺序保持 top-left/top-right/bottom-right/bottom-left。开启 Isosceles 时固定 v2/v3、上底长度和高度，将 v0/v1 对称放在下底中垂线两侧；关闭仅解除约束。等腰模式下拖上底一点时对称更新另一端，拖下底一点时保留下底另一端、上底长度和高度并重居中；禁止底边交叉与退化。

共享核心提供 `SetPlanarConstraint` 候选函数，UI 经新增 `ShapeUiActionKind::SetConstraint` 调用，沿用 BuildShapePropertyCandidate、Replace、journal、Undo/Redo 与 revision 校验。下一笔状态仅存会话内，不新增启动配置。

### 9.4 编解码与旧文件

`inkeysShape v5` 的 tool map 增加 constraint；Solid 参数在 v5 增加 baseProjection。v5 继续写 aux。以下对象写 v5：非 Free 约束、sides>12、Prism/Pyramid 非默认投影，以及使用新遮挡派生规则的 Prism/Pyramid。其他对象保持最低可用版本。

旧版本读取：constraint=Free、baseProjection=0.5、sides 仍按旧上限。新版接受 3–14 并拒绝 15。v5 对旧读取器是未知版本，走标准部件降级，不把 13/14 塞进 v3/v4 后让旧读取器判 Invalid。

旧 v3/v4 Prism/Pyramid 的 parts 来自旧虚实算法：保留旧派生核验用于验证持久化部件，核验通过后替换为当前构造派生 parts，再执行完整 Validate；不得直接忽略 parts 不匹配。标准导出、冷历史与自动保存复用同一构造真值和当前派生结果。

## 10. 点工具、弧和立体几何

### 10.1 折线/自由多边形

点工具首次 ConfirmDrag 区分单击和实际拖动：单击追加 rawDown 一点，拖动追加 rawDown/rawUp 两点。Back/Forward 以完整 points_ 序列为准，首拖回退/恢复也一致。

一点时用临时零长度 Line 作为可见起点；填充样式下临时预览强制可见描边并复用现有颜色 fallback。已确认点列始终产生开放 Polyline 候选，即使新点导致最终 Polygon 暂时无效也不清空旧路径。Polygon 另存通过最终校验的 confirmedCandidate_ 供 CanComplete/Complete；仅 Complete 返回闭合 Polygon。临时预览不改变最终 preset、填充、撤销条数或文件构造。

### 10.2 弧/扇形

第一步 Circle 预览复制样式并使用 Dashed；纯填充扇形临时补可见描边，最终对象恢复原 style。

角度编辑继续使用 EditSession 的累计 angleDelta。候选扫角跨过零并变号时交换 construction.start/end 所代表的起止角，同时把 sweep 变为对应相反表示，使被拖点连续跟随、另一端和原绕向保持；往回跨越可逆。恰好零扫角仍钳在最近有效状态，|sweep| 上限仍为 2π。活动 Angle 手柄的按压视觉在角色交换时跟随当前实际端点，Commit 使用同一最终意图，禁止抬手回跳。

### 10.3 棱柱/棱锥可见性

用共享构造参数建立一致的局部三维投影模型：底面点由未压缩正多边形和 baseProjection 还原，高度向量与底面投影共用同一视图方向。对每个底面/侧面求有符号朝向；一条边只有在所有相邻面都背向观察方向时才为隐藏虚线，否则为实线。锥顶高度和底面角度因此都会影响棱锥侧棱。临界共面使用统一 epsilon，避免浮点导致相邻帧抖动。

该规则只改共享 ExpandAdvanced 的部件 style 和必要几何，不增加 shader 分支。棱柱与棱锥使用同一可见性定义，但测试分别记录行为，不顺带改圆柱/圆锥既有教学约定。

## 11. UI 布局、模式与样式

### 11.1 浮条位置与外框

ShapeEditor 在新会话锁存 placementSide=Below。每帧从本地/权威候选的 transform+frameRect 求当前侧边中心；只有当前侧不能完整容纳浮条或与主栏冲突时才试另一侧，换边后锁存。原侧恢复空间不反向切换。两侧都不足时保持当前侧并复用 FitEditorPose，不在每帧重置为 Below。普通 Move 对锚点 SetDirect；仅实际换边可复用既有位置过渡，抬手不新增追赶。

浮条、回弹锚点、色板/边数/取色器方向共享同一 placementSide。frameRect 始终发布。外框绘制增加 UI 谓词，仅 Line/Arrow/Rectangle（含旧对象对应构造）不画圆角框；旋转连接线和立体角标独立判断。

### 11.2 常驻文字和约束控件

提取主栏辅助标签映射供主栏/浮栏共用，按钮使用真实字体测量后的图标+文字槽，而不是悬停 hint。Triangle 增加自由/等腰/等边，Trapezoid 增加等腰文字开关；当前对象与下一笔分别走 SetConstraint 和 IdtState 会话状态。纯 Fill 下隐藏辅助项，但保留构造方式、约束和边数等真正几何参数。

浮条新增常驻文字后仍走 SingleRow 与既有 fit/两行策略；主栏参数行按实际项数计算宽高和 hint。新文案先改 zh-CN，再用既有 i18n sync/check 更新二十语和生成头。

### 11.3 填充与辅助显示

主栏 AttributeItems 和浮条 ToolbarItems 删除 OutlineAndFill 入口，保留按钮 ID/枚举兼容。Outline 表示 stroke=true/fill=false，Fill 表示 stroke=false/fill=true；旧混合状态在两个保留按钮上同时 Selected，点击其中一个才写单一模式。

辅助开关 wanted 与 showStroke 联动。共享 BuildAuxiliaryGeometry 在纯 Fill 时不派生辅助视觉，但 ShapeStyle.auxiliary 位不变；历史 bounds、dirty、导出和重载因此一致。Center 编辑柄在对象支持且启用 Center 辅助时，Thumb 整体 opacity 额外乘 0.5；命中热区和常驻内容透明度公式不变。

### 11.4 边数与目录

只交换 UI 目录中的 Polygon/Polyline，使 RegularPolygon 与 Polygon 相邻。所有 3–12 硬编码改为共享上限 14，主栏和浮条边数层排布 12 个实际按钮（3–14）；新增 13/14 SVG 和 RC 映射，轮播/文字/命中/键盘同步。

## 12. 首次字体与完成环

Shape/More 文本把最终 `targetPose.scale` 下的字号写入 `fontTargetSize`，当前字号仍随动画绘制。Shape 覆盖类别、分段、圆/三角形构造文字并保留长标签拟合。More 先走真实 RenderLoop 冷启动回归；若生产路径仍有目标缺失，在所属最终布局计算处补齐，不新增异步字体等待。

固定步数进行中仍显示 `(step-1)/N`。正常 Complete 时利用现有 `ConstructionStatus::finished` 发布可区分的终态，并在后续 inactive 快照保留完成锁存直到新构造/取消/工具切换；ConstructionPrompt 在 open→false 且收到同会话完成沿时将 ringProgress 定向到 1，与既有 0.4 秒收缩并行。Cancel/失败/工具切换不得设置完成锁存，避免把末步取消误当完成。

## 13. 回滚与验证边界

- 约束、投影、遮挡和 v5 是共享核心单源；UI 不包含第二份几何公式。需要回滚时可按 core→runtime→UI 的提交边界逆向恢复，旧文件兼容路径不能单独删除。
- 离屏/无头测试覆盖算法与真实生产动作入口；截图验证 UI 视觉，不能用 imports/编译代替。触屏/笔手感、真实屏幕遮挡和观感仍需用户实机验收。
- 既有未跟踪 `inkStrokeModelerTest/*.cso` 是构建产物，不属于本任务提交；完整构建后检查并清理仅由本轮测试新建的多余输出。

# 2026-11-14 第二轮设计补充

依据3731e3ab继续实施。构造真值新增顶角身份和新版斜二测立体参数，旧构造兼容读取。投影/控制点/遮挡统一从核心推导。样式条由屏幕AABB定位，移动目标持续插值；辅助柄偏移只在UI不污染几何。构造中顶点编辑保留construction session，终结动作与预览发布遵守既有FIFO/session屏障。闭合虚线相位CPU/GPU/导出共用定义。
分工边界：geometry负责模型/展开/非构造会话编辑/codec；runtime负责ConstructionSession与生产输入/预览/动作；ui负责Bar/工具状态/资源；主代理集成虚线及完整验证。共享文件按成员区域协调，不覆盖其他代理改动。

# 2026-11-14 第三轮（已批准实施，基线 ec1d5138）

本段替代前两轮冲突要求。三个子任务已建立，父任务继续 in_progress，不自动提交、推送或归档。
- 普通跟随直接移动，只有换边/避让分支改变时保留连续残差动画。
- 修复完成后撤回延迟与重做浮窗阻塞几何入口，验证真实输入及历史发布链。
- 平面胶囊旋转+四向箭头移动，矩形保留、直线箭头无；立体仅移动。
- 图文紧凑居中、圆形文案圆心，辅助柄24DIP附近避让、四向箭头且按下无跳变。
- 等腰双斜杠只在构造/编辑显示，退出编辑消失；按钮右侧三个点标识并循环约束顶角。
- 平行四边形固定原单位模板比例，以首末点为长对角线旋转绘制。
- 平面四边形允许过零翻向，梯形仍禁交叉；绘制中顶点调整后可继续加点和撤回恢复。
- 立体统一固定教材斜二测：x水平，y向右上45度或左上135度半长，z竖直。圆底严格数学投影。
- 箱体外接矩形宽高分解对应底面短对角线；立方体模型等边，第二笔仅高度方向。提示底面/高度。
- 底面柄径向等比缩放+切向Z旋转，长方体保留首笔比例；圆柱锥仅半径。不保留俯仰/二维旋转。
- 棱柱顶面中心/棱锥顶点横向侧偏纵向高度，侧偏随Z旋转，去底面平移。正形开关持续约束。
- 棱柱/锥3–8，正多边形3–14，三个独立会话值启动均6；约束与边数依次布局。
- 私有构造统一v7，删除开发期v1–v6解析迁移与旧立体算法。保留已发布直线矩形及通用UInk标准几何，不保证旧开发期构造恢复。

验证：核心几何、真实生产输入/历史、UI逐帧/DPI/主题/长文案、v7及标准降级/冷历史/自动保存；原生ARM64两个solution和现有无窗口测试。实机触控与动画观感留待用户确认。

## 第四轮设计（2026-11-14）

最小边界为共享shape_core/shape_interaction的平行四边形和箱体构造，以及Bar.ShapeEditor显示层与ConstructionPrompt顺序；不重构窗口/渲染架构。SelectionOverlay透明HWND负责外框/控制点/样式栏及子面板，Bar负责主栏/构造提示，Draw3负责图形。

删除以整个主栏HWND矩形清空编辑层及拒绝命中的逻辑；主栏实际非透明内容由既有Z序遮挡。最终视觉姿态参与bounds/clip/Hit，旧成功帧与当前帧的动画范围都必须覆盖；失败呈现不提前提交Hit。半圆问题先像素复现再定位，不能盲增固定边距。胶囊沿停靠边切线，保持20DIP高度，控件偏移不污染几何。停靠以当前边优先，空闲超屏时选最近可容纳边，否则最少溢出并限制屏内。焦点成对选位短轴镜像；等腰标记从一侧镜像生成。

平行四边形初始水平边使用固定偏斜比，长对角线吻合手势；编辑基向量从手势开始快照锁存，固定对角点反解两轴有符号长度，过零不终止。长方体底面外接框W/H反解水平边W-H和模型深度2sqrt(2)H，窄框压缩H保留最小水平边；Cube按固定模型方形投影比例内接。立体仍使用既有v7模型参数，不改变完成后编辑入口。

## 2026-11-14 第五轮

第五轮：修改共享立体几何的按preset投影、逆映射及面朝向，圆柱/圆锥使用x=x、y=-y/3+height，模型Z旋转先于投影，其他立体保持45度半长。模型半径仍相等，首笔/完成/控制点/隐藏边共用映射。箱体仅MakeSolidConstruction分支恢复e8eb2f1b。存储沿用开发期v7不新增兼容；共享核心与UInk、renderer和生产历史验证。
