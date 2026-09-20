# draw3 墨迹存储与 UInk 差距调查

调查日期：2026-09-01

## 1. Examined Code And Specs

- `inkStrokeModelerTest/draw3/ink_document.cppm`
- `inkStrokeModelerTest/draw3/ink_document.cpp`
- `inkStrokeModelerTest/draw3/ink_history.cppm`
- `inkStrokeModelerTest/draw3/drawing_controller.cppm`
- `inkStrokeModelerTest/draw3/drawing_controller.cpp`
- `.trellis/spec/native/runtime-and-rendering.md`
- 历史任务 `.trellis/tasks/archive/2026-08/draw3-source/archive/2026-08/08-09-ink-persistence-document-model/`

先前任务明确把 UInk 编解码、第三方兼容和文件对话框排除在范围外；过去没有已经批准的 UInk schema 映射决定。本任务是独立的新能力层，不应把旧 runtime history 设计误当作线格式设计。

## 2. Current CPU Document Model

```text
InkCanvasCollection
  workspaceGuid: InkGuid
  pages: vector<InkPage>

InkPage
  guid: InkGuid
  canvases: vector<InkCanvas>

InkCanvas
  device: DeviceKey(uint64)
  viewport: {x, y, scale}
  strokes: vector<InkStroke>

InkStroke
  style: {StoredInkType, fallbackRgb, opacity, texture}
  points: vector<{x, y, width}>
```

`InkGuid` 是 16 bytes；默认 DeviceKey 为 0。`StoredInkType`：

| value | current meaning |
| --- | --- |
| 0 | Pen |
| 1 | Highlighter |
| 2 | Eraser |
| 3 | SolidLine |
| 4 | DashedLine |
| 5 | OutlineRectangle |
| 6 | FilledRectangle |

文档层当前特征：

- 单个 workspace GUID，多 page、多 Canvas；没有显式 Workspace/Device registry。
- page GUID 必须非零且不重复；页面使用 vector 顺序，没有显式 pageIndex/pageNumber。
- Canvas 没有 layerIndex/layerNumber、PPT slideId 或 extra。
- viewport x/y 要求有限并限制在约 `+/-1048576`，scale 当前只接受精确 `1.0`。
- stroke 只有统一样式和点列；普通 Stroke 点 width 当前允许 `>= 0`，Shape 路径另有正宽/端点约束。
- Shape 被压在 `StoredInkType` 中，并继续使用 InkStroke 的点列/样式，而不是独立参数化 Shape model。
- 没有 Media、HDR Color Map、点级高级荧光笔样式或私有扩展。
- 没有持久化 contentId、undoId、renderOnlyWhenLatest。

## 3. Runtime History Is A Sidecar

`CanvasRuntimeHistory` 独立保存：

- stroke index 到 render item 的稳定顺序；
- 当前 visibility 和 redo stack；
- history/cache generation、栅格 bounds、preimage/composition cache 等运行时数据。

项目规范明确：`InkStroke`/Canvas 只保存显示所需 CPU 数据，visibility 和 GPU/cache 状态不进入持久对象。DrawingController 在非 Laser stroke 完成时：

1. 只用 confirmed real points 完成 Stored Stroke；
2. 先 append 到 document；
3. 丢弃 redo 并登记 runtime history；
4. 再从 Stored Stroke 绘制。

这与 UInk 的影响：

- UInk 完整保存需要知道“真正已撤回”与“仍有效但 latest-group 隐藏”的区别，仅凭 `InkCanvasCollection` 不够。
- 当前 runtime undo 是逐 RenderItem；UInk undoId 可让多个连续对象共享一步，适配器需要显式 grouping 输入。
- redo 不应写入 UInk，符合规范“不承诺关闭后 Redo”。
- prediction、L1/L2、preimage、composition cache 全部不得持久化。

## 4. Creation And Ownership Today

`DrawingController::Run` 当前创建新的单 `InkCanvasCollection`、一个空白页和默认 Canvas，控制器私有持有可选 document 及各页 runtime state。当前没有公开 persistence API，也没有文件生命周期。

因此本任务不应：

- 在 controller 的抬笔提交路径直接打开文件；
- 让 codec 获取 DrawingController 指针或 runtime locks；
- 把自动保存错误混进绘制主循环；
- 为了文件格式改动预测/渲染提交顺序。

已确认未来接入由绘制线程在安全点生成不可变 export snapshot，再交给同步持久化能力；实际调度可在另一个任务决定。

## 5. Mapping Matrix

| UInk concept | Current draw3 | Proposed handling |
| --- | --- | --- |
| Header GUID/version/counts/time | none | UInk file model/service owns；Save/Save As 显式 |
| Workspace registry/tree/type/host/current page | one workspace GUID only | standalone UInk model；draw3 adapter 默认单 Workspace，宿主信息由 snapshot 提供 |
| Device registry/tree/geometry | uint64 DeviceKey only | 不能直接当 UUID；snapshot 提供稳定 GUID/描述或显式默认单例 |
| pageGuid | InkPage.guid | 可无损映射；保留 16-byte UUID |
| pageIndex/pageNumber | vector order only | export 由顺序生成；import 检查连续性 |
| layers | each page has Canvas vector but no layer identity | 不能假设 vector 同时代表 Device 和 layer；需由 adapter policy 明确分组 |
| viewport x/y/positive scale | x/y + scale exactly 1 | 完整 UInk 模型保留；单向 draw3 export 只生成 scale=1，反向导入延期 |
| ordered Ink/Shape/Media | vector<InkStroke> only | standalone ordered variant；本任务不反向导入 draw3 |
| Pen | StoredInkType::Pen | UInk inkType 1 |
| Highlighter | StoredInkType::Highlighter | UInk inkType 2；当前渲染也需保持整条覆盖语义 |
| Advanced Highlighter | none | 已确认由强类型 UInk 模型保留；draw3 import/降级延期 |
| Erase Ink | StoredInkType::Eraser | UInk inkType 0；作用顺序可映射，普通切割板擦仍需 full save |
| Ink relative deltas | absolute stored points | codec export 首点绝对，其余计算有限 delta；import 累加并检查溢出/有限性 |
| point width `>0` | ordinary model accepts `>=0` | export 零宽必须拒绝/诊断，不应任意夹取 |
| Color Map HDR | fallbackRgb only | 完整 Color Map 留在 UInk model；未来 draw3 import 可使用规范 fallback |
| opacity | float | 范围需验证；规范 reader 可钳有限越界，writer 不输出越界 |
| texture | int-like style field | 已知 0 可映射；未知按规范回退但记录诊断 |
| Shape Line | SolidLine/DashedLine | 映射 UInk Shape Line；dashArray 需匹配当前实际 dash pattern |
| Shape Rectangle | Outline/FilledRectangle | 映射 Rectangle；Stroke/Fill 精确语义需结合 renderer 确认 |
| Polyline/Square/Ellipse/Circle/Polygon | none | 独立模型保留；反向 adapter 延期 |
| Media/resource pack | none | 主文件元数据由独立模型保留；不接资源或渲染 |
| contentId | implicit vector order | full save 重编号；append 分析从文件状态承接 |
| undoId | runtime history, no persisted groups | export snapshot 显式提供；不能从 document 猜测复杂分组 |
| latest-group | none | standalone semantics；future adapter snapshot 可显式表达 |
| unknown top-level object | none | reader skip + `requiresSaveAs`; 不承诺 raw preservation |

## 6. Critical Semantic Gaps

### 6.1 Canvas Vector Ambiguity

UInk 的 Canvas key 同时包含 workspace、device、page、layer。当前 page 只有 `vector<InkCanvas>`，Canvas 只有 DeviceKey，没有 layer index。若同 Device 出现多个 Canvas，当前结构没有足够信息判断它们是图层还是重复/其他用途。适配器不能自行猜测：

- 推荐当前 draw3 export 每个 `(page, DeviceKey)` 只允许一个 Canvas，并写 layer 0；
- 出现重复 DeviceKey 时返回 model invariant/unsupported，除非用户另行定义其语义；
- 完整 UInk 多图层继续保留在独立 UInk model，不强塞回 draw3。

### 6.2 Shape Representation

当前 Shape 是两个端点等有限数据塞入 InkStroke，UInk Shape 是参数化独立块。映射前需从 renderer 核对：

- DashedLine 的实际 dash/gap 长度和 offset；
- FilledRectangle 是否还有描边、描边宽度取何值；
- rectangle 两点究竟是角点还是中心/尺寸派生；
- opacity/color 对 Fill 与 Stroke 的应用。

在这些语义核对完成前，design 只定义类型方向，不写死未经验证的数值。

### 6.3 Viewport

UInk 合法 scale 为任意有限正数；draw3 constructor 当前只允许 1.0。直接导入并回退 1 会改变内容与显示布局。推荐：

- 完整 reader/model 保留原 viewport；
- draw3 adapter 对非 1.0 返回 Partial/Unsupported；
- 不在本任务扩展 draw3 平移缩放渲染能力。

### 6.4 Width Zero

UInk Ink 点宽必须严格正。draw3 ordinary stored point 验证允许 0，虽然实际输入通常可能不会生成。writer 应在 adapter 边界拒绝零宽并定位 page/canvas/stroke/point；自动改为 epsilon 会制造不同外观且无规范依据。

### 6.5 Undo/Visibility

旧历史任务曾写“未来 UInk 导出只枚举当前可见项”，但新规范要求保留 latest-group 隐藏但未撤回的原稿。不能简单使用 `visible == true` 过滤全部对象。导出 snapshot 需要区分：

- 已撤回，不保存；
- 普通有效并可见；
- 有效但因 renderOnlyWhenLatest 暂时隐藏，仍保存；
- redo 分支，不保存。

当前 runtime sidecar 没有这一完整 UInk 分类，因此首版能力模块应接收明确数据，而不是偷偷推导。

## 7. Confirmed Boundary

2026-09-01 已确认采用以下方案：

```text
UInkEditingSession
  owns Full UInkDocument + source revision + stable logical identities
          ^                         |
          | decode/read             | encode/full-save/append
          |                         v
       .uink bytes <---------- UInk file service

InkCanvasCollection + runtime snapshot
          |
          | one-way exact export
          v
     Full UInkDocument

Full UInkDocument -X-> InkCanvasCollection
  (reverse import / preview is a follow-up task)
```

理由：

1. 用户要求参考规范所有内容，而当前 draw3 只覆盖明显子集。
2. reader 的容错、未知检测和 Save As 策略需要看到完整 wire model，不能在转换过程中丢信息。
3. persistence 能先独立测试，未来 draw3 扩展多层/HDR/Media 时不必重写 wire codec。
4. 场景接入可后置，符合本轮“只实现能力”的边界。
5. draw3 不支持的已知 Shape、高级荧光笔和 HDR Color Map 仍留在完整会话；本任务不先做反向投影，新增普通 UInk 内容直接合并回有序序列。

代价是首版 model/测试文件更多，但换取完整规范能力与清晰的转换边界。draw3 现有内部设计保持优先，只有正确转换确有必要时才做最小调整；不采用“draw3 子集 codec”方案。

## 8. Planned Non-Changes

- 不改变 `InkStroke` 的不可变存储与 append-before-draw 顺序。
- 不把 UInk fields 全部加入现有 `ink_document` 类型。
- 不修改 runtime history/cache 数据结构来迎合文件格式。
- 不接入数字键 page/undo 命令、DrawingController::Run 或 ClearCanvas。
- 不新增文件 UI、自动保存线程或渲染器功能。
- 不实现 `UInkDocument -> InkCanvasCollection`、预览或视觉降级；选择性安全降级 A 留给后续接入任务。
- 不在该能力任务里修复 width `>=0` 的全局 document 契约；只在 UInk export 边界严格验证，除非后续证据证明应单独修复模型不变量。

## 9. Confirmed Snapshot Input

2026-09-01 用户确认采用 A：draw3 调用方在安全点生成不可变 `Draw3UInkExportSnapshot`，显式传入当前有效内容、撤回分组和导出身份。持久化模块不读取 `DrawingController` 或 runtime sidecar，也不要求扩充 `InkCanvasCollection` 来承载运行时状态。快照不借用可变运行时数据；是否安全共享已有不可变 Stroke 值留给实现阶段在不改变该契约的前提下决定。
