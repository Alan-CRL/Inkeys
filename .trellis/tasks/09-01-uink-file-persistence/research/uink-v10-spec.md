# UInk 1.0 Beta Version 10 规范调查

调查日期：2026-09-01

## 1. Sources And Coverage

用户授权读取 `D:\Project\Inkeys\website\docs\standard` 原始文档。已逐篇阅读并与 `https://www.inkeys.top/standard/intro` 当前发布首页、侧栏目录及 GitHub 发布源核对。

| # | 文档 | 本任务关注点 |
| --- | --- | --- |
| 1 | `intro.md` | 格式目标、主文件/资源包边界、Beta 状态 |
| 2 | `version.md` | version 10 冻结边界、旧草案拒绝、文件身份 |
| 3 | `type.md` | Type ID、精确 MessagePack 编码、重复键、未知块 |
| 4 | `file/main.md` | 连续对象流、作用域、两棵注册树、顺序与容错 |
| 5 | `incremental.md` | 允许追加、恢复边界、完整保存条件和提交顺序 |
| 6 | `conformance.md` | 一致性流程、公开 fixtures 和第三方检查表 |
| 7 | `blocks/header.md` | 固定 Header、快照语义 |
| 8 | `blocks/headerExtension.md` | 文件说明、Device/Workspace 注册表和隐式单例 |
| 9 | `blocks/device.md` | Display/Window、硬件信息、父树与坐标空间 |
| 10 | `blocks/canvas.md` | 页面/图层身份、viewport、PPT、多显示器 |
| 11 | `blocks/ink.md` | 四类 Ink、点编码、Erase、latest-group |
| 12 | `blocks/shape.md` | 6 类参数化 Shape、Stroke/Fill/Marker |
| 13 | `blocks/media.md` | 资源路径、视觉/PDF/音视频字段、ZIP 安全 |
| 14 | `common/color.md` | SDR fallback、sRGB/scRGB 扩展和回退 |

发布侧栏中的“墨迹拓展文件”是首页锚点，不是第 15 篇独立规范页。当前公开样例位于 `/standard/uink-v10/`；本地 website checkout 未包含样例二进制，因此通过项目的 GitHub 发布源只读核对 manifest 和 SHA-256 清单。

## 2. Frozen Wire Boundary

- 当前且唯一兼容基线为 UInk 1.0 Beta，`Header.version = 10`，线格式已冻结。
- version 10 可继续接受不改变既有解释的文字澄清或可选扩展；不兼容修改必须换 version。
- 旧草案的 Header `array(6)`、顶级 Device、Type ID `5` 为 Media 均不属于兼容范围，无迁移义务。
- 没有额外 magic；`.uink` 扩展名本身不够。首对象必须同时满足 Header、`array(7)`、Type ID `0`、version `10`，否则拒绝按当前规范解析。
- UInk 保证数据语义、布局和基础呈现互操作，不要求自由曲线、Marker 外形、抗锯齿或复杂擦除像素级一致。

## 3. Physical File Layout

主文件不是一个外层 Array，而是连续 MessagePack 顶层对象：

```text
Header
[Header Extension]
Canvas
[Ink | Shape | Media]...
[Canvas [Ink | Shape | Media]...]...
EOF
```

结构不变量：

- Header 固定为第一块。
- Header Extension 可选、最多一个且必须紧跟 Header；之后只能是首个 Canvas 或 EOF。
- Device 仅是 Header Extension `devices` 内的 Map，不是顶层块。
- Canvas 开始新的内容作用域；后续 Ink/Shape/Media 归属最近 Canvas，直到下一 Canvas 或 EOF。
- 第一个 Canvas 前不得有内容块；Canvas 可以为空。
- 完整未知 Type ID 对象被跳过，不改变当前作用域；不能确定其结束边界时按流损坏停止。

已注册 Type ID：

| ID / wire type | 含义 |
| --- | --- |
| `0` / Array | Header |
| `1` / Map | Header Extension |
| `2` / Map | Canvas |
| `3` / Map | Ink |
| `4` / Map | Media |
| `5` / Map | Shape |

## 4. Global MessagePack Rules

### 4.1 Writer

- 每个字段使用表中声明的精确 MessagePack 类别和位宽；`uint16` 不能因值小写成 positive fixint，`float32` 不能写成 float64。
- Header 还必须保持精确数组长度、字段顺序和 UUID str8 编码。
- 将容错读入的数值重新保存时，规范化回声明类型。
- 块级私有字段写在对应 `extra` Map；私有编号 `128+` 没有全局厂商命名空间或 vendorId。

### 4.2 Reader Numeric Compatibility

其他数值编码只有同时满足以下条件才可接受：

1. 源和目标都是数值；`bool` 不是数值。
2. 转换不改变数学值。整数目标要求源为数学整数；浮点目标要求目标浮点类型能精确表示。
3. 结果满足目标类型范围和字段自己的有限性/范围规则。

string、bool、Array、Map 不允许以其他类别替代。NaN/Infinity 是否使字段或块无效由具体字段规则决定。

### 4.3 Duplicate Keys

- 任一嵌套 Map 中重复出现任意“已知键”，使包含该 Map 的当前完整顶层块无效。
- 未知键按各页扩展规则忽略；不能改变已知字段解释。
- 实现不能先解码到只保留一个值的字典后才校验，否则无法发现重复键。

## 5. Header

固定 wire layout：

| index | field | exact encoding |
| --- | --- | --- |
| 0 | `type = 0` | uint16 |
| 1 | `version = 10` | uint16 |
| 2 | `guid` | 36 字符 UUID，str8，36 bytes |
| 3 | `deviceNum` | uint32 |
| 4 | `workspaceNum` | uint32 |
| 5 | `pageNum` | uint32 |
| 6 | `time` | uint64 Unix UTC seconds |

- Header GUID 在首次创建时生成，增量写入、完整保存、移动/重命名不改变；另存为新逻辑文件必须改变。
- counts 和 time 只表示最近一次完整保存。增量追加不回写 Header，所以 pageNum/time 落后是合法状态。
- 读取器从有效对象流重算当前状态，只比较并诊断快照差异；不得用 counts 做无界 reserve。
- 完整保存重算快照并保持同一逻辑文件的 GUID/version。

## 6. Header Extension, Workspace And Device

### 6.1 Header Extension

Map 字段：`type=1`、可选 `name`、`explanation`、`devices:Array<Map>`、`workspaces:Array<Map>`、`extra:Map`。

- Header Extension 或某一注册表缺失/为空时，只为该类建立一个文件内隐式单例。
- 隐式 Device 使用当前渲染目标根区域；隐式 Workspace 为 `workspaceType=0` 屏幕批注；Header 对应 count 必须为 1。
- 隐式项没有可序列化 UUID；不得生成随机 UUID 并自动回写。
- 某类注册表一旦有显式项，Canvas 必须写可解析的相应 GUID，不能与该类隐式项混用。Device 显式、Workspace 隐式或反向组合均合法。
- 重复 UUID、坏条目和父循环应警告并仅在内存构造修复结果，不自动保存。

### 6.2 Workspace

字段：必填 `guid`、`workspaceType:int32`；可选 `name`、`parentWorkspaceGuid`、`hostId`、`currentPageIndex:uint32`、`extra`。

类型：0 Screen Annotation、1 Whiteboard、2 Presentation、3-127 reserved、128+ private。未知类型按通用白板加载并保留内容，但不执行未知宿主绑定。

- currentPageIndex 缺失默认为 0；无对应页时回退第 0 页并警告。
- 子 Workspace 跟随父项可见性/生命周期并合成在父项之上；同级按数组顺序，后项在上。
- Workspace 与 Device 两棵树独立，父子 Workspace 不要求使用相关 Device。
- 循环时断开产生循环的父引用，将对应项作为临时根。
- 每个显式 Workspace 至少应有一个 Canvas，空 Canvas 表示空白首页。
- PPT 建议 hostId 对应 Presentation.Tags 稳定标识，具体幻灯片由 Canvas.slideId 定位。

### 6.3 Device

公共字段：必填 `guid`、`deviceType:int32`；可选 `name`、`hardware`、`extra`。

类型：0 Display、1 Window、2-127 reserved、128+ private。未知类型作为本次加载的临时根显示面，不自动回写。

Display 必填 `x/y:int32`（系统虚拟桌面逻辑原点，可负）、`width/height:uint32 > 0`。可选 Hardware Map 包含 name/id、`Map<string,string>` identifiers、正物理宽高、有限正 scaleFactor。硬件匹配失败不妨碍使用保存逻辑几何或当前目标。

Window 必填 parentDeviceGuid、`x/y:float32`、`width/height:float32 > 0`、`zIndex:uint32`。坐标相对父 Device；可嵌套，可越界并由宿主裁剪。同父项 zIndex 越大越上，相同值按注册顺序后项在上。父引用缺失或成环时断开并作为临时根。

Device 描述显示面位置/大小；Canvas.viewport 描述正在查看的 Canvas 世界区域。两者 x/y 坐标空间不同，禁止互作默认或替代。

## 7. Canvas

字段：`type=2`、条件 workspaceGuid/deviceGuid、必填 pageGuid/pageIndex/pageNumber/layerIndex/layerNumber、PPT 条件 slideId、可选 layer-0 viewport、extra。

### 7.1 Identity And Order

- pageGuid 在整份文件永久唯一；复制为新页生成新 GUID，重排/完整保存保持。跨设备/图层的同一逻辑页共享 pageGuid/pageIndex。
- 同 Workspace 内 pageGuid 与从 0 无空洞的 pageIndex 一一对应。
- 每个 `(workspaceKey, deviceKey, pageGuid)` 内 layerIndex 从 0 无空洞。
- `(workspaceKey, deviceKey, pageGuid, layerIndex)` 是 Canvas 唯一键。
- pageNumber/layerNumber 仅用于显示，可跳号或重复。
- 合成由 Workspace 注册顺序、Device zIndex 和 layerIndex 等逻辑规则决定，不依赖 Canvas 在对象流中的物理排列；规范写入顺序建议 Workspace、pageIndex、Device、layerIndex。
- 同步多显示器白板共享 Workspace/page identity，但各 Device 的 Canvas 内容、contentId、undoId 独立；独立翻页使用不同 Workspace。

### 7.2 Viewport

Map 必填 `x/y/scale:float32`；x/y 有限可负，scale 有限且 `>0`。映射：

```text
deviceX = (canvasX - viewport.x) * viewport.scale
deviceY = (canvasY - viewport.y) * viewport.scale
```

缺失默认 `{0,0,1}`。不注册旋转、错切或非等比缩放。viewport 归属 `(workspaceKey, deviceKey, pageGuid)`，仅该组 layerIndex 0 可保存；其他层继承。非 0 层字段忽略并警告；第 0 层 Map 缺字段、非有限或非正 scale 时整个 viewport 回退默认，不自动回写。

### 7.3 Recovery And PPT

- 显式引用缺失/无法解析时可建立临时 Workspace/root Device。
- 页面身份/pageIndex 无效时可按对象流创建临时独立页；layerIndex 无效时按流序创建临时图层。
- Presentation Canvas 必须有 slideId。`Slides.FindBySlideID` 失败时保留 Canvas 但标为未绑定；禁止按 pageIndex、当前页或其他幻灯片猜测附着。重新绑定后需完整保存。

## 8. Ordered Content, IDs And Undo

- Ink、Shape、Media 在每个 Canvas 中严格按顶层对象流混合处理，不能按类型重新排序。
- 三者共享从 0 连续递增的 contentId；该 ID 只在当前文件版本/Canvas 内有效，完整保存可重排，不能作外部稳定引用。
- undoId 从 0 开始且只允许不递减；相同且连续的内容对象构成一步撤回。
- UInk 保存当前有效内容，但不承诺关闭后 Redo。
- 完整保存移除真正撤回的对象，重新整理 contentId/undoId；被 latest-group 规则隐藏但尚未撤回的原稿仍是有效内容，必须保留。

## 9. Ink

必填：`type=3`、contentId、undoId、`inkType:int32`、Color Map、`opacity:float32 0..1`、`texture:int32`、至少一个 points；可选 renderOnlyWhenLatest bool 和 extra。

类型：0 Erase、1 Pen、2 Highlighter、3 Advanced Highlighter、4-127 reserved、128+ private。未知/不支持 inkType 保留基础几何并回退 Pen。未知 texture 回退 0。

点 Map：x/y/width float32，首点绝对、后续 x/y 是相对前一点 delta，width 是完整直径且必须 `>0`。高级荧光笔点级 color/opacity 必须成对；锚点外延用首/末样式，锚点间平滑插值算法由实现决定。

- Pen 使用块级颜色/透明度。
- Highlighter 和 Advanced Highlighter 必须先形成整条覆盖，再整体做一次 alpha source-over，不能逐段叠加导致交叠变深。
- Erase 按对象流只作用于此前 Ink/Shape，不作用 Media。
- UInk 1.0 不注册压力、时间、倾斜、朝向、速度、加速度或预测点；未知点键忽略。
- 有限 opacity 越界钳制 0..1；NaN/Infinity 使字段无效。
- 无点、点 Map 损坏或缺必填字段时跳过整条 Ink，警告后继续。
- 会切断/改写旧对象的普通板擦必须把结果规范化后完整保存；只有新的完整 Erase Ink 可直接追加。

## 10. Shape

必填：`type=5`、contentId、undoId、shapeType、geometry；条件 stroke/fill；可选 latest flag/extra。至少一种样式；Line/Polyline 必须 Stroke 且不得 Fill。

类型：0 Line、1 Polyline、2 Rectangle、3 Square、4 Ellipse、5 Circle、6 Polygon、7-127 reserved、128+ private。未知 shapeType 不能安全伪造，跳过整块。

几何：

- Line 恰好 2 个绝对 Point；Polyline 至少 2；Polygon 至少 3 且隐式闭合，写入器不重复首点，fill 用 non-zero winding。
- Rectangle/Ellipse：centerX/Y、正 width/height、可选 rotation（Canvas 正向为顺时针）。
- Rectangle 可选成对 cornerRadiusX/Y；均缺失或均 0 表示直角。只有一个、混合 0/正数或越过半宽/半高时两者回退 0 并警告。Ellipse 出现圆角字段时忽略并警告。
- Square：centerX/Y、正 size、可选 rotation。
- Circle：centerX/Y、正 radius，无 rotation。

Stroke：Color、opacity、正 width；可选 dashArray、dashOffset、markers。非空 dashArray 必须为偶数个非负有限数且总和 `>0`，否则连同非法 dashOffset 按实线。只定义 Marker 0 None、1 OpenArrow；未知 Marker 回退 None。Marker 仅 Line/Polyline 可用，闭合图形中忽略。Marker 不产生额外对象或 ID。

Fill：fillType 0 Solid，其他为 reserved/private；未知 fillType 使用有效 Color/opacity 按 Solid 回退。Line/Polyline 禁止 Fill，闭合 Shape 可仅 Stroke、仅 Fill或两者兼有。

缺 type/contentId/undoId/shapeType/geometry/必要样式，或最终无有效几何/样式时跳过 Shape。有限 opacity 越界按 Ink 同类规则钳制；其他字段先用明确回退，无回退时使相关几何/样式无效。

## 11. `renderOnlyWhenLatest`

每个 Canvas 反向扫描：

1. Media 跳过，既不加入组也不停止。
2. 连续遇到 `true` Ink/Shape 时加入“末尾最新组”。
3. 遇到首个缺失/false Ink/Shape 时停止。
4. 最新组和所有未标记内容显示；其他更早的 true 内容隐藏。

该规则只影响显示，不合并 undoId。典型形状修正先存 true 原稿，再存 false 结果；结果存在时原稿隐藏，撤回结果并完整保存后原稿可重新成为尾部组。隐藏但未撤回原稿必须保留。

## 12. Color Map

必填 `fallback:uint32`，规范 writer 限制 `<=0xFFFFFF`；可选扩展 `space:string` 与 `components:Array<float32>(3)` 必须成对。

- 两扩展都缺失：用 fallback。
- 只出现一个、未知 space、长度非 3、任一非有限、srgb 分量越过 0..1：扩展无效，用 fallback。
- `srgb` 分量 0..1；`scrgb` 为有限线性值且可 >1。
- 有效扩展优先显示，fallback 表达预期 SDR 外观；tone mapping/色域由软件决定。
- 容错读取到大于 24 bit 的 uint32 可取低 24 位。
- fallback 缺失/类型错误使所属必填颜色/样式无效，再按 Ink/Shape 上层规则处理。

## 13. Media And `.uink.extra`

基础字段：`type=4`、contentId、undoId、path、mimeType、可选 extra。实际资源在同名 `.uink.extra` ZIP；主文件保存所有顺序、几何、播放和 PDF 状态，ZIP 无额外索引。

### 13.1 Safe Path And Budgets

path/entry 必须 UTF-8、NFC、`/` 分隔的根内相对路径。拒绝开头 `/`、URI、盘符/绝对路径、反斜杠、NUL/控制字符、空段、`.`、`..`。NFC 后重复 entry 使相关资源不可用，不能静默选第一或最后一项。

不把不可信 ZIP 直接解压到工作目录。读取/分配/解码前检查条目数、单项大小、总解压大小和压缩比预算，并在流式解压/解码期间按实际输出持续累计，不能只信 ZIP 声明。超限只使当前资源缺失，不影响基础 Ink/Shape/后续块。

缺资源包/entry、MIME 未知、声明与安全嗅探明显不符、解码失败都视为资源不可用；保留 Media 布局/顺序并继续。SVG 按非活动图像，默认禁脚本、事件、外网、外部文件和其他可执行内容，不能安全降级则不渲染。

### 13.2 Media Fields

视觉媒体（图像/SVG/视频/PDF 页）要求正 width/height；可选 transform float32[6] 和 opacity。矩阵 `[a,b,c,d,e,f]` 按 `x'=a*x+c*y+e`, `y'=b*x+d*y+f`；缺失为单位矩阵，长度错/非有限也回退单位并警告。有限 opacity 越界钳制，NaN/Infinity 无效。

PDF 可选正 pageCount 和 pageIndex（默认 0）。能解析时实际页数优先；不一致警告。非法 pageCount 忽略，pageIndex 钳制到实际范围。资源缺失仍保留布局及可选页占位。

音视频可选 autoplay=false、loop=false、volume=1、startTime float64=0、playbackRate=1。有限 volume 钳制；startTime 钳制到 0..时长；非法 playbackRate 回退 1。平台不能 autoplay 不构成文件拒绝。

缺 path/mimeType/当前视觉类型必要尺寸时跳过当前 Media；未知块键忽略。Erase 不作用 Media。

## 14. Stream Recovery Matrix

| Situation | Read result | Continue later objects? | Source mutation |
| --- | --- | --- | --- |
| EOF exactly on boundary | normal completion | n/a | none |
| EOF inside final object | discard incomplete object, keep previous | no bytes remain | none |
| decode error in non-tail object | keep objects before failed start | no, stop | none |
| undecodable bytes between objects | keep objects before byte segment | no, stop | none |
| complete unknown Type ID | skip whole object | yes | none |
| complete known block with invalid fields | block-specific fallback or skip | yes | none |
| invalid Header | reject UInk 10 | no | none |
| implementation limit exceeded | stop/reject according to usable prefix; report limit | no unsafe continuation | none |

禁止逐字节搜索疑似下一个对象边界。普通读取永不截断或修复源文件。

## 15. Append Rules

允许：

- 向文件最后 Canvas 追加完整 Ink/Shape/Media；
- 追加引用既有显式注册项或隐式单例的新 Canvas，再追加完整内容。

约束：

- 未完成轨迹、部分 Map、Header 原地更新、通过重复旧 Canvas 打补丁都禁止。
- 仅文件物理末尾最后 Canvas 可继续；新 contentId 承接共享序列，undoId 不小于此前值。
- 新 Canvas key 不重复；跨 Device 同逻辑页复用 pageGuid/pageIndex。
- 恢复后再次追加：截断 incomplete/error tail；尾部连续完整无效已知块从第一块开始截断；若无效完整块后还有有效块，必须完整保存，禁止从文件中间截断追加。

必须完整保存：注册表、Workspace parent/host/current page、撤回/重做、旧页/图层修改、普通板擦切旧内容、修改/移动/删除已有内容、PDF pageIndex、已有 viewport、页面/图层/page identity 结构或任何无法证明 append 等价的变化。

## 16. Full Save Transaction

- 完整保存只保留当前有效内容和规范要求保留的 latest hidden originals，重排 IDs、重算 Header。
- 保持同逻辑文件 Header/Workspace/Device/page GUID。
- 先确定主对象和资源引用。
- 有资源时先生成/校验临时资源包，暂时包含旧主文件与新主文件引用的并集。
- 生成/校验临时主文件。
- 先替换 `.uink.extra`，最后原子替换 `.uink`；主文件成功后才清理多余资源。
- 禁止直接在原文件上边读边覆盖。
- 外部导入或含未知内容默认 Save As；只有用户明确确认有损才可覆盖。

## 17. Public Fixtures

`fixtures.json` 与 `SHA256SUMS.txt` 发布 9 个输出：

| Fixture | Purpose |
| --- | --- |
| `implicit-single-canvas.uink` | 无扩展的双隐式单例 |
| `explicit-multilayer.uink` | 显式注册、多 Device/Workspace、多图层和 layer-0 viewport |
| `mixed-latest.uink` | Ink/Shape/Media 混排及 latest group |
| `incremental-tail.uink` | Header 快照落后、按流重算 |
| `truncated-tail.uink` | 丢弃截断末对象 |
| `unknown-block.uink` | 跳过完整未知 Type ID |
| `numeric-compat.uink` | 无损非规范数值编码 |
| `media-safe.uink` | Media 安全路径和引用 |
| `media-safe.uink.extra` | 安全 PNG/SVG ZIP entries |

manifest 提供用途、hex、长度、SHA-256、decodedObjects/expectedObjects 或 ZIP entries，适合作为自动测试真值。

发现一处上游元数据陈旧：`fixtures.json.format` 仍写 `UInk version 10 pre-Beta conformance fixtures`。实际字节使用冻结的 array(7)、version 10 和 Type 0-5，当前 conformance 页面明确把这些文件列为 Beta 样例。因此实现按当前规范和实际字节解释为 Beta，不因这句描述回退到旧草案；测试记录该差异，暂不修改上游网站。

## 18. Implementation Consequences

1. reader 必须是对象流 parser + 语义状态机，不能只对单个 struct 调 `convert`。
2. 完整的错误状态至少需要“拒绝文件、恢复尾部、停在损坏前缀、跳过块、字段回退、临时内存修复”六个层级。
3. append 不是简单 `ofstream(app)`；必须依赖最近一次严格扫描、safe offset、文件身份复核和 pre-encoded batch。
4. 全规范 UInk 模型明显大于当前 draw3 模型；直接把 wire 数据塞入 `InkCanvasCollection` 会造成静默丢失。
5. `.uink.extra` 虽可选，但 Media 和完整保存提交顺序属于冻结规范。若首版延期资源包，API 必须显式说明能力缺口。

## 19. Approved First-Version Scope

2026-09-01 已确认首版只实现 `.uink` 主文件，不发现、打开、读取、复制或写入 `.uink.extra`。读取其他软件生成的 Media 引用时，仍解析并保留主文件中的 Media 元数据和布局，将资源标记为 unavailable，并继续处理 Ink/Shape。写入采用 A：只允许向原文件追加非 Media 对象；完整保存、Save As 和 Media 追加返回 `ResourcePackUnsupported`。
