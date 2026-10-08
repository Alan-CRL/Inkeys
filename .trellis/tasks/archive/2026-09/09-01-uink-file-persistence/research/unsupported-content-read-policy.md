# draw3 未支持内容与 UInk 读取策略调查

## 1. 结论摘要

当前 draw3 的存储模型是面向重放的紧凑子集，不适合作为完整 UInk reader 的目标结构。已确认将两个问题彻底分开：

1. UInk reader 对全部已注册 version 10 内容做结构化解析、字段校验和规范容错，结果进入独立 `UInkDocument`。
2. 本任务不执行 `UInkDocument -> InkCanvasCollection`；高级内容不会进入 draw3，也没有渲染、降级或占位要求。
3. 已确认当前 draw3 只通过调用方在安全点构造的不可变 snapshot，单向导出其可准确表达的子集；持久化模块不读取 Controller 或 sidecar。
4. 后续反向接入再按已选择的 A 方向决定视觉投影：规范回退优先，无法安全表达时不显示；投影不得回写完整 UInk 模型。

“draw3 不支持”不应被 codec 解释为“UInk reader 不读取”。否则无法同时满足完整规范校验、追加后保留和再次完整保存。

## 2. 当前 draw3 可表达范围

源码证据：

- `inkStrokeModelerTest/draw3/ink_document.cppm:56-64` 只有 Pen、Highlighter、Eraser、SolidLine、DashedLine、OutlineRectangle、FilledRectangle 七种 `StoredInkType`。
- `ink_document.cppm:73-85` 的点只有 x/y/width，样式只有 `fallbackRgb`、单一 opacity 和 texture；没有点级颜色或点级透明度。
- `ink_document.cppm:103-138` 的 Canvas 由 `DeviceKey`、固定结构 viewport 和 Stroke vector 组成，没有 Workspace/Device 注册树或 layer identity。
- `ink_document.cppm:108` 与实现校验只接受 `scale == 1.0f`。
- `stroke_geometry.cpp:104-113` 只把四种 Stored Shape 映射为实线、虚线、边框矩形和填充矩形 primitive。
- `stroke_geometry.cpp:780-812` 的 Highlighter 使用固定 nib 尺寸构造覆盖，不支持 UInk 高级荧光笔的点级样式锚点；任意 UInk Highlighter 的宽度语义也不能直接等同于当前固定 nib 渲染。

可以单向导出且仍需精确核对的子集：

| draw3 内容 | UInk 目标 | 边界 |
| --- | --- | --- |
| Pen | Ink/Pen | 点、块级颜色、opacity、默认 texture；零宽拒绝 |
| Highlighter | Ink/Highlighter | 块级样式；必须保持整条覆盖一次合成语义 |
| Eraser | Ink/Erase | 有序擦除几何；涉及修改旧对象时不能假装 append |
| SolidLine/DashedLine | Shape/Line | 当前固定 dash 模式需换算为规范 dashArray |
| Outline/FilledRectangle | Shape/Rectangle | 只导出能证明等价的轴对齐、固定样式子集 |
| page GUID / Canvas | UInk Canvas | 单 Workspace、默认 layer 0；Device 身份由 snapshot 提供 |
| viewport | UInk viewport | 当前只导出 scale=1 |

## 3. draw3 当前不支持的已注册 UInk 内容

### 3.1 Ink 与颜色

- Advanced Highlighter（`inkType=3`）及点级 Color Map/opacity 锚点。
- Color Map 的 `space` + `components` 扩展颜色，尤其允许大于 1 的 scRGB HDR 分量；draw3 只有 `fallbackRgb`。
- UInk 非默认或私有 texture 的已知/未知语义；当前模型虽有整数槽位，但没有对应渲染能力契约。
- UInk 任意逐点宽度的 Highlighter 与当前固定 nib 几何之间的精确映射。
- reserved/private `inkType` 的厂商语义；规范要求未知或不支持类型保留基础几何并以 Pen 作为有效回退。

### 3.2 Shape

- Polyline、Square、Ellipse、Circle、Polygon。
- Rectangle/Ellipse 的 rotation、Rectangle 的独立 cornerRadiusX/Y。
- Line/Polyline Marker，包括 OpenArrow。
- 任意 dashArray/dashOffset，而非 draw3 唯一固定虚线外观。
- 同一 Shape 中独立 Stroke 与 Fill、不同颜色/opacity/宽度，以及只描边、只填充、描边加填充的完整组合。
- UInk Shape 的参数化几何与 draw3 “两个点塞入 InkStroke”的表示差异。

UInk 规范明确：未知 shapeType 不能伪造为其他几何，应跳过该 Shape；但 Polyline/Square/Ellipse/Circle/Polygon 是 version 10 已注册类型，不属于未知类型，完整 reader 应能解析它们。

### 3.3 文档拓扑与语义

- 显式 Device/Workspace 注册表、父子树、Window Device 几何和多 Workspace。
- 同页同 Device 的多 layer、跨 Device 同 pageGuid 和规范合成顺序。
- 任意有限正 viewport scale、按 layer-0 继承 viewport。
- pageNumber/layerNumber、PPT slideId/hostId 与失配后的未绑定状态。
- 持久 contentId/undoId、连续撤回分组和 `renderOnlyWhenLatest` 隐藏原稿语义。
- Header Extension 的名称、说明和注册表元数据。

### 3.4 Media 与扩展

- 图片/SVG/视频/PDF 的 path、MIME、尺寸、仿射 transform、opacity。
- 音视频 autoplay/loop/volume/startTime/playbackRate 和 PDF pageCount/pageIndex。
- `.uink` 块内的 `extra` Map。它与 companion `.uink.extra` ZIP 不是同一概念；若要求私有扩展往返，建议使用受限额约束的通用 UInk/MessagePack value tree 保存该已知字段。
- companion `.uink.extra` 资源发现、解码与渲染已明确不在首版；Media 主文件元数据仍是已注册内容。

## 4. 是否存在已记录的后续实现计划

截至 2026-09-01，仓库源码、Trellis 活跃任务和可检索会话中未发现 Advanced Highlighter、HDR Color Map、Polyline/Square/Ellipse/Circle/Polygon、Media 渲染或多图层 draw3 的已批准实施计划。

现有历史只证明以下范围：

- `08-11-shape-tools-line-rectangle` 只实现实线、固定虚线、边框矩形和填充矩形，并明确不处理外部 Shape 文件兼容。
- `08-09-ink-persistence-document-model` 把多图层、Shape、Media、HDR 持久化列为 Out of Scope。
- 当前 UInk 任务是首次为这些规范概念建立完整文件模型；它不应被误解为同时批准相应 renderer。

这里的“没有计划”仅表示仓库中没有已记录、已批准的实施任务，不代表未来产品永远不会支持。

## 5. 推荐读取矩阵

| 输入类别 | UInk reader 行为 | UInk 模型 | draw3 行为（本任务） |
| --- | --- | --- | --- |
| 已注册且有效，draw3 可表达 | 完整解析和校验 | 强类型原始语义 | 不反向导入 |
| 已注册且有效，draw3 不可表达 | 完整解析和校验 | 强类型原始语义 | 不反向导入，不警告为文件损坏 |
| 已注册字段存在规范回退 | 记录 declared/wire provenance，计算 effective value | 保留足够信息避免自动改写来源 | 不反向导入 |
| 已注册块无有效必填语义 | 按对应规范跳过当前块并诊断 | 不伪造对象 | 不反向导入 |
| 未知顶层 Type ID | 完整解码对象边界后跳过，标记 containsUnknown | 规范不要求 raw 保留 | 不反向导入 |
| `.uink` 内 Media 引用 | 解析完整主文件元数据，资源 unavailable | 强类型 Media metadata | 不打开 `.uink.extra` |
| `.uink` 内已知 `extra` Map | 按预算解析项目自有通用 value tree | opaque-but-structured extension value | 不解释私有语义 |

能力差异应由单独的 future `AnalyzeDraw3Projection` 一类接口报告，不能污染 codec 的文件有效性结果。一个合法 Advanced Highlighter 对 reader 来说是普通成功内容，而不是 warning；只有未来调用 draw3 投影时才产生 `Unsupported` 或 `PartialPreview`。

## 6. 高级荧光笔的具体建议

推荐模型至少保留：

- declared `inkType=3`；
- 块级完整 Color Map、opacity、texture；
- 每个点的 x/y/width；
- 每点可选且必须成对的 Color Map + opacity 样式锚点；
- contentId、undoId、renderOnlyWhenLatest 和 `extra`。

这只是数据结构和 codec 工作，不包含插值、笔刷几何、tone mapping 或 GPU shader。未来 draw3 接入可以选择完全不投影，也可以生成只读的普通 Highlighter fallback；无论如何都不应把 fallback 写回上述结构。

## 7. 备选：不透明 Pass-Through

若不为 draw3 不支持的已注册内容建立强类型结构，可以只保存每个完整 MessagePack 对象的原始字节和最小顺序信息。这样能减少首版模型代码，但代价明显：

- 无法证明高级字段符合规范，也无法给出字段级损坏诊断。
- 完整保存时难以安全重排 contentId/undoId、规范化编码或编辑内部值。
- 原始对象与模型级 Canvas/注册表修复结果可能不一致。
- 能安全承诺的主要是“不改旧前缀的原文件追加”，而不是完整可编辑 round-trip。

直接跳过且不保留 raw bytes 更不满足“后续新增内容再保存仍保留高级效果”，不建议作为选项。

## 8. 确认选择

2026-09-01 用户确认 A：全部已注册 version 10 内容进入项目自有的强类型 `UInkDocument`，高级内容完全不进入 draw3；本任务只做数据保留、校验、保存和追加。这样内部结构确实会更丰富，但复杂度来自已冻结的文件规范，而不是提前实现 renderer。

因此，合法但 draw3 不支持的内容必须正常读取并接受字段级校验；顶层 opaque MessagePack pass-through B 被排除。块内私有 `extra` 仍由有界的通用值树保留，因为其私有语义没有可注册的具体类型。
