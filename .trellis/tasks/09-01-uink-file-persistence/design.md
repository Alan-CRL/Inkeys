# UInk 文件持久化设计草案

> 状态：用户范围和风险决策已于 2026-09-01 全部收敛，并已在最终规划摘要后的后续消息中明确批准开始实现。

## 1. Design Goals

- 对 UInk 1.0 Beta version `10` 的线格式、对象作用域、恢复和保存契约负责，而不是仅让当前 draw3 自产文件能够互相读取。
- 把不受信任文件解析与 draw3 运行时对象隔离；只有经过验证和归一化的值进入 UInk 逻辑模型，只有适配器确认可表达的值进入 draw3 文档。
- 读取尽可能保留有效内容，但绝不掩盖损坏、有损转换、未知内容或不可安全追加的状态。
- 完整保存和追加是两种显式操作；调用方不能通过同一个模糊 `Save` API 意外选择错误策略。
- 能力层不持有 DrawingController、窗口、D3D 资源或自动保存状态。
- 现有 draw3 内部模型与提交路径保持优先；转换层先适配差异，仅在正确转换有明确缺口时对 draw3 做最小、独立论证的调整。
- 从 `.uink` 打开的编辑会话始终以完整 UInk 模型为持久化真值；本任务不创建 draw3 投影，显示能力不能决定文件中哪些规范内容被保留。

## 2. Proposed Module Set

模块名在实现前按现有 C++20 module 命名习惯复核，职责边界如下：

| 模块 | 职责 | 不负责 |
| --- | --- | --- |
| `draw3.uink_model` | UInk Header、注册表、Canvas、混合内容、颜色、Shape、Media、逻辑身份、编辑会话与加载元数据 | MessagePack、文件 IO、draw3 渲染对象 |
| `draw3.uink_codec` | 单个对象及连续对象流的精确编码、限额解码、字段校验、对象偏移与诊断 | 路径替换、业务保存策略 |
| `draw3.uink_file` | 完整读取、来源 revision、冲突检测、临时文件校验、原子替换、Save/Save As 身份规则、追加预检与截断 | 自动保存调度、UI |
| `draw3.uink_draw3_export` | 从不可变 draw3 快照单向构造 UInk 模型，并报告无法准确导出的能力差异 | UInk 反向导入、预览、渲染、直接访问 DrawingController 或 GPU sidecar |

首版不包含 `uink_resources` 或任何 ZIP 依赖。`uink_model` 仍保存 Media 元数据；reader 验证 path 但不访问它，将资源标记为 unavailable。含 Media 文档只允许向原文件追加非 Media 对象，其他写入返回 `ResourcePackUnsupported`。

## 3. Data Flow

```text
untrusted bytes
    -> MessagePack object boundary decoder
    -> wire-value validation + duplicate-known-key detection
    -> UInk v10 logical model + diagnostics + valid prefix
    -> UInk editing session (full document + source revision)

UInk model edit/append batch
    -> semantic validation
    -> merge into the same full ordered UInk document

draw3 immutable export snapshot
    -> one-way export validation
    -> new UInk document or supported model-level additions

draw3 export snapshot or UInk model
    -> semantic validation + save policy
    -> exact-width MessagePack encoder
    -> temporary file + self-validation
    -> atomic replacement

validated existing file + append batch
    -> append analysis
    -> pre-encode complete objects
    -> optional tail truncation
    -> append + default FlushFileBuffers
```

关键原则是 codec 不直接构造 `InkStroke`，本任务也不把读取结果反向导入 draw3。这避免合法但 draw3 尚不能显示的 UInk 数据在解析或再次保存时被丢弃，也避免不规范值进入现有文档不变量。

## 4. Logical Model

### 4.1 Identity And Registries

- `UInkDocument` 保存 Header 永久 GUID、最近完整保存时间、可选说明、Device/Workspace 注册表、按对象流归属整理的 Canvas，以及加载来源元数据。
- `UInkEditingSession` 持有完整 `UInkDocument`、来源 revision、加载诊断和稳定逻辑内容身份；已加载文件的保存/追加都从该完整会话产生计划。
- Device 和 Workspace 分别使用“显式注册表”或“隐式单例”状态，不能在同一类注册表内混用。
- 隐式单例使用内部 tagged key，不生成可序列化随机 GUID。
- 原始父引用与本次加载的解析结果分离：断环、未解析引用和临时根项留在 resolution metadata，防止保存时无意覆盖原意。
- Canvas 保存稳定 page GUID、页面/图层索引、显示编号、可选 PPT slideId 和 layer-0 viewport。

### 4.2 Ordered Canvas Content

- 每个 Canvas 使用一个有序 `variant<Ink, Shape, Media>` 序列，不能拆成三个容器。
- `contentId`/`undoId` 作为加载数据和诊断依据保留；完整保存从当前有效顺序重新编号。
- `renderOnlyWhenLatest` 保留在 Ink/Shape 上；解析后计算可见性视图，但不删除隐藏且未撤回的原稿。
- Erase Ink 仍是有序内容，不在 codec 中破坏性改写先前对象。
- 规范已定义但 draw3 暂不支持的 Shape、高级荧光笔点样式、HDR Color Map 和 Media 仍是普通有序内容；本任务不为它们生成 draw3 投影或视觉回退。
- UInk 模型新增对象通过稳定 Canvas/content 身份插入或追加到完整序列，不能先过滤成 draw3 可显示子集再整体写回；未编辑高级对象的相对顺序保持不变。
- Unknown top-level object 不要求原样保存，但读取结果记录 `containsUnknownObjects` 和来源信任状态，以驱动 Save As。

### 4.3 Wire Values Versus Effective Values

对规范允许回退的字段同时记录：

- effective value：供预览、适配和再次显式规范化保存；
- diagnostic：原值为何无效、使用了何种回退；
- provenance flag：结果是否来自临时身份、默认值、钳制或未知类型回退。

首版不必保留每个无效原始 MessagePack 值；但必须避免把恢复结果自动写回源文件。保存 API 要求调用方显式选择新文件或允许规范化覆盖。

### 4.4 Format-Complete Typed Content

已确认采用完整强类型方案，不为 draw3 暂不支持的已注册块保存原始顶层 MessagePack 字节作为替代模型：

- `UInkColorMap` 保存 fallback、可选色彩空间与三个分量，并区分 declared/wire 信息和规范回退后的 effective 颜色。
- `UInkPoint` 保存 x/y/width；高级荧光笔的点级 Color/opacity 作为必须成对的可选 `UInkPointStyle` 保存。
- `UInkInk` 保存注册 ink type、块级 Color/opacity/texture、点列、latest 标志、ID 和块内 `extra`；Erase、Pen、Highlighter、Advanced Highlighter 均使用可字段级访问的项目类型。
- `UInkShape` 使用覆盖 Line、Polyline、Rectangle、Square、Ellipse、Circle、Polygon 的 geometry variant，并分别保存 Stroke、Fill、rotation、corner radius、dash 与 Marker 语义。
- Header Extension、Workspace、Device、Canvas、viewport、PPT 绑定和 Media 主文件元数据均使用项目自有结构，不能因 draw3 当前没有对应字段而省略。
- 块内私有 `extra` Map 使用递归且受 `UInkReadLimits` 约束的通用值树，保留 MessagePack 的 nil/bool/有符号与无符号整数/浮点/string/binary/array/map/extension 类别和 Map 顺序；它不是 companion `.uink.extra` ZIP，也不允许第三方 msgpack 类型逃逸出 codec 私有实现。

合法 UInk 内容是否可由 draw3 显示属于独立 capability 分析，不参与 reader 的成功/失败判断。未知顶层 Type ID 仍按规范跳过并标记来源状态，不因本选择获得未定义的 raw round-trip 承诺。

## 5. Decoder Pipeline

### 5.1 Framing

1. 在 offset `0` 解码一个完整对象并严格验证 Header。
2. Header 失败即返回 `RejectedNotUInkV10`，不尝试扫描后续字节。
3. 顺序解码后续顶层对象，记录每个对象的 `[start, end)`。
4. 对完整对象先识别顶层结构与 Type ID，再执行块级验证。
5. EOF 在边界为正常；EOF 在最后对象内部为 `RecoveredTruncatedTail`；非尾部或对象间解码错误为 `StoppedAtCorruption`。
6. 不进行 magic 搜索或启发式重同步。

### 5.2 MessagePack Type Rules

- 写入路径使用明确的 uint16/uint32/uint64/int32/float32/float64、str8、Array 和 Map pack 操作。
- 读取数值先检查源类别，再做“数学值不变 + 目标精确可表示 + 字段范围”转换。
- `bool`、string、Array 和 Map 只接受各自类别。
- Map 以原始 key/value pair range 遍历；每一层维护“已见已知键”集合。任一已知键重复使包含该 Map 的完整顶层块无效。
- 未知键跳过其完整值；私有字段是否允许写入由 `extra` 规则决定。

### 5.3 Structural State Machine

解析器维护 `AfterHeader -> AfterHeaderExtension -> InCanvas` 状态：

- Header Extension 最多一个且只能紧跟 Header；错位的已知块按结构无效报告。
- Ink/Shape/Media 在首个 Canvas 前无归属，跳过并报告。
- Canvas 开始新作用域；内容直到下一个 Canvas 或 EOF。
- 完整未知 Type ID 可跳过，不改变当前 Canvas 作用域。
- Header 计数仅比较并警告，不参与预分配或拒绝。

### 5.4 Recovery Result

建议公开：

```cpp
enum class UInkReadStatus {
    Complete,
    RecoveredTruncatedTail,
    StoppedAtCorruption,
    RejectedNotUInkV10,
    LimitExceeded,
    IoError,
};

struct UInkReadResult {
    std::optional<UInkDocument> document;
    UInkReadStatus status;
    std::uint64_t validPrefixBytes;
    std::optional<std::uint64_t> safeAppendOffset;
    bool containsUnknownObjects;
    bool requiresSaveAs;
    bool requiresFullSaveBeforeAppend;
    std::vector<UInkDiagnostic> diagnostics;
};
```

命名可调整，但信息不能退化成单个 `bool` 或日志字符串。

## 6. Diagnostics And Limits

### 6.1 Diagnostics

`UInkDiagnostic` 至少包含：severity、稳定 code、顶层 object index、byte offset、可选 Type ID、字段路径和简短消息。稳定 code 用于测试和未来 UI 本地化；消息不作为程序分支条件。

建议严重度：

- `Info`：Header 快照落后等合法状态；
- `Warning`：字段回退、临时身份、未知类型、缺失资源；
- `Error`：当前块被跳过、对象流在有效前缀停止；
- `Fatal`：Header 拒绝、IO 或资源限额导致无法建立可用文档。

### 6.2 Limits

已确认所有架构使用同一 `UInkReadLimits` 默认档：

| 限额 | 默认值 |
| --- | ---: |
| 主 `.uink` 文件字节数 | `128 MiB` |
| 确定性模型计费 | `256 MiB` |
| 顶层对象数 | `250,000` |
| 单个顶层对象字节数 | `32 MiB` |
| 全文件 Ink + Shape geometry 总点数 | `4,000,000` |
| 单 Ink / 单 Shape geometry 点数 | `1,000,000` |
| MessagePack 嵌套深度 | `32` |
| 单 string / binary 字节数 | `8 MiB` |
| 单 Array / Map 项数 | `1,000,000` |

Device、Workspace、Canvas、内容和 Media 元数据同时受顶层对象、单对象、容器及模型总预算约束，不再建立更宽松的旁路。累计诊断默认最多保留 `4,096` 条，随后只增加一次 `DiagnosticsTruncated` 并继续按其他资源预算解析；Media path 另限 `32 KiB` UTF-8 字节，首版不读取 ZIP，因此不设置或声称执行 ZIP 解压预算。

模型预算使用项目定义、与 ABI 无关的保守计费表，不以 `sizeof(T)` 或 allocator 实际开销作为跨平台契约；string/binary payload、容器元素、已知模型节点和通用 `extra` 节点都在分配前按安全加乘法计费。实现阶段以三个目标平台中最不利的布局校核计费表，确保它不会明显低估实际 owned payload，并以测试固定同一输入在 Win32/x64/ARM64 的限额结果。

调用方可通过显式 options 收紧或放宽每个值，但公共 API 不提供“无限制”预设。任一限额在声明长度分配前或流式累计中被超过时，返回 fatal `LimitExceeded`、`document = nullopt`、`safeAppendOffset = nullopt`；可以保留只读诊断和已扫描字节信息，但不能据此授权写入或尾部修复，来源文件保持不变。

正式支持超大文件所需的 lazy/segmented model、memory mapping、后台加载、延迟目标和架构专用档均不在首版；显式提高限额只是调用方承担资源风险的专家选项，不构成大文件兼容保证。

## 7. Block Validation Summary

- Header：任何固定布局、Type ID、version 或 UUID 错误均拒绝文件。
- Header Extension/registries：缺失注册表建立对应隐式单例；重复 GUID、循环和坏条目只做本次加载修复并警告。
- Device/Workspace 未知子类型：分别作为临时根显示面/通用白板回退；不执行未知宿主逻辑。
- Canvas：引用、页面身份或索引损坏时生成临时加载身份；无效 layer-0 viewport 回退默认，非 layer-0 viewport 忽略；PPT slideId 失配保持未绑定。
- Ink：无点或点必填数据损坏则跳过；未知 inkType -> Pen，未知 texture -> 0；有限 opacity 越界钳制，NaN/Infinity 无效；点级高级样式必须成对。
- Shape：未知 shapeType 或无法得到有效几何/必要样式则跳过；未知 Marker -> None，未知 fillType -> Solid；非法 dash 回退实线；圆角、Marker 和 fill 按几何适用性处理。
- Media：缺 path/MIME/必要视觉尺寸则跳过；变换无效回退单位矩阵；有限 opacity/volume 钳制；播放率回退；资源不可用保留布局并继续。
- Color：有效扩展颜色优先，否则 fallback；扩展字段不成对、未知空间、错误长度、非有限或 srgb 越界均回退；容错读取过大 fallback 可取低 24 位。

完整逐页规则与错误矩阵见 `research/uink-v10-spec.md`，实现测试以该矩阵为来源。

## 8. Full Save Transaction

### 8.1 Validation And Identity

- `SaveExistingLogicalFile`：保持 Header GUID 及既有永久 GUID。
- `SaveAsNewLogicalFile`：生成新 Header GUID，页面/注册项 GUID 是否复用按“逻辑副本”语义在 API 中明确；默认保持文档内部稳定引用，仅更换文件身份。
- `NormalizeImportedWithExplicitLoss`：只有调用方显式确认未知内容可能丢失时可覆盖来源路径。

保存前从完整 `UInkEditingSession` 建立不可变 save plan，验证所有对象和资源，重排规范输出顺序并重新编号。draw3 未渲染或降级渲染的已知规范对象仍参与计划；任何失败发生在目标替换前。

### 8.2 Main File Commit

1. 在目标同一目录创建不可预测名称的临时 `.uink`，确保最终 rename/replace 位于同一卷。
2. 编码完整对象流，flush/close。
3. 用严格读取器自检临时文件，必要时比较计划摘要和对象计数。
4. 在提交窗口重新验证来源/目标 revision；不匹配时返回 `SourceChanged`。
5. 使用 Windows 可用的原子替换语义提交；保留可诊断的原系统错误。
6. 提交失败时按 `ReplaceFileW` 的实际状态恢复或保留可读版本，不能笼统假设原目标必然未变。

实现阶段需针对“目标不存在”和“替换既有文件”分别确认 Win32 API；不能把 `std::filesystem::rename` 的跨平台语义想当然当作原子替换保证。

### 8.3 Deferred Resource Pack

首版不发现或打开 `.uink.extra`，不复制资源包，也不执行双文件提交。reader 对 Media path 只做语法验证并生成 resource-unavailable 状态。

规范要求含资源的完整保存遵守资源并集和“资源包先、主文件后”的事务。由于首版没有能力验证该前提，不能自行假设已有 companion ZIP 正确。已确认采用 A：只允许向原 `.uink` 追加不含 Media 的对象；完整保存、Save As 和 Media 追加在创建临时文件或修改目标前返回 `ResourcePackUnsupported`。

## 9. Append Transaction

分成纯分析与写入两个 API：

```cpp
UInkAppendPlan AnalyzeAppend(const UInkReadResult&, const UInkAppendBatch&);
UInkAppendResult ExecuteAppend(std::wstring_view path, const UInkAppendPlan&,
                               const UInkAppendOptions&);
```

`UInkAppendPlan` 固定来源 revision、文件长度、可选安全截断点、预编码 bytes、目标 Canvas key、首/末 contentId 和 undoId。执行前重新核对同一文件身份和内容版本，避免分析后文件被外部修改。

顺序：

1. 拒绝 Header/注册表/结构变化、旧 Canvas 修改、撤回/重做或其他必须完整保存状态；任何包含 Media 的 batch 在打开写句柄前返回 `ResourcePackUnsupported`，若来源已含 Media，则还只允许向原路径追加非 Media Ink/Shape/Canvas；
2. 判断无效数据是否只位于尾部，确定安全截断边界；
3. 验证并在内存中完整编码整个 append batch；
4. 以独占事务句柄打开文件并复核来源 revision/长度；
5. 如需恢复，先截断到安全边界；
6. 一次写入预编码对象；
7. 默认调用 `FlushFileBuffers`，只有显式 `Buffered` 选项跳过；
8. 返回新文件长度、后续编号、来源 revision 和持久性状态。

截断本身是有意修复操作，因此只在调用方提交已验证 append plan 时发生；普通读取永不修改来源。成功追加只能改变原 `.uink` 的尾部，不重编码已有前缀，也不发现、打开或改写 `.uink.extra`。

`UInkAppendOptions` 的默认 durability 为 `Durable`。`FlushFileBuffers` 成功后返回 `CommittedDurable`；调用方显式选择 `Buffered` 时返回 `CommittedBuffered`，不能使用与 durable 成功相同的无标记状态。这里的 durable 表示操作系统接受了持久刷新请求，不额外承诺存储硬件不会谎报缓存行为。

若完整预编码 batch 已全部写入，但 `FlushFileBuffers` 失败，结果为 `WrittenNotDurable`，并携带写入后的长度、revision 和系统错误；它不是“目标未修改”，调用方不得自动重放同一 batch。若写入过程中失败，事务在同一独占句柄内尝试截断回 batch 起点；来源原有无效尾部已经按批准计划移除且回滚成功时可返回 `TailRepairedNoAppend`。若回滚本身失败，则返回 `PartialCommitRequiresRecovery`、最后可信边界、可观测长度和系统错误；文件可能带有可由严格 reader 识别的截断尾部，但绝不报告未修改或成功。完整保存始终刷写临时文件，没有 buffered 选项。

## 10. File Occupation And Source Revision

现有 `Other.Config.cpp` 已使用 `CreateFileW`、禁止共享、最多三次有效尝试和 100 ms 间隔来覆盖短暂占用，并在读写完成后立即关闭句柄。UInk 可复用“短事务 + 有界重试”的意图，但不能复用原地 `SetEndOfFile` 后写入或无差别重试所有错误的实现。

建议的 `SourceRevision` 至少包含卷/文件身份、字节长度、最后写入时间和读取字节流的内容摘要；Header GUID 只作为语义身份和诊断信息，不能替代内容版本。摘要在读取同一句柄时增量计算，避免文件在读取和另一次 hash 之间变化。

已确认采用方案 A：

1. 读取时以允许其他读取者、拒绝写入/删除的短时句柄取得一致字节流；只对 `ERROR_SHARING_VIOLATION`、`ERROR_LOCK_VIOLATION` 等瞬时冲突有界重试。
2. 编辑期间不持有文件句柄，`UInkEditingSession` 保存来源 revision。
3. 保存/追加前取得目标的进程内与跨进程事务 guard；在同一受控窗口复核 revision，不匹配即返回 `SourceChanged`，不覆盖、不自动合并。
4. append 在同一独占文件句柄内完成复核、可选截断、写入和默认 flush，因此不存在主动释放句柄造成的中间竞争窗口；刷盘失败按 `WrittenNotDurable` 报告已发生的文件变化。
5. full save 先完成临时文件自检。替换既有目标时使用唯一 backup 接收被替换版本，并核对 backup revision 是否等于预期；不相等则报告提交期冲突并尝试恢复外部版本。目标原先不存在时使用“不覆盖已出现目标”的提交方式。

Windows 7 的普通文件 API 不提供“仅当目标 hash 等于 X 时原子替换”的 CAS 操作，因此非协作进程在最终替换瞬间仍需通过 backup 核对和恢复处理。任何无法确定最终路径状态的情况必须返回独立的 partial-commit 结果并保留恢复材料，不能笼统报告成功或删除唯一可读副本。

未采用的方案 B 是从打开文档起持续持有拒绝写入的句柄，直到关闭会话；它虽然更早阻止普通外部写入，但会在整个编辑期间妨碍其他软件，且最终替换仍需专门处理句柄共享和恢复。

## 11. Draw3 One-Way Export And Deferred Import

### 11.1 One-Way Export

已确认采用 A：单向适配器输入是由 draw3 调用方在绘制线程安全点构造的显式不可变 `Draw3UInkExportSnapshot`，包含：

- `InkCanvasCollection` 的只读页面/Canvas/Stroke 数据；
- 每个 Canvas 当前有效内容的可见/撤回分组快照；
- 文件或场景需要的 Workspace/Device 描述；
- 由调用方选择的目标 Header identity 和 save mode。

快照必须封闭所有权和生命周期，不保留指向可变 document、runtime sidecar、GPU cache 或预测缓冲的借用指针。对于本身不可变且生命周期受快照持有的 draw3 值，允许实现阶段选择安全共享以避免无谓深拷贝；这不能改变快照读取期间状态固定的契约。

适配器不持锁读取 DrawingController，也不自行推断 sidecar 状态。本任务只实现 snapshot 到 UInk 模型的纯转换，不接快照调度、自动保存或业务场景。

当前映射提案：

- Pen/Highlighter/Eraser -> UInk Ink 1/2/0；
- SolidLine/DashedLine -> Shape Line，虚线使用规范 dashArray；
- OutlineRectangle/FilledRectangle -> Shape Rectangle，样式按当前渲染语义精确确认后映射；
- 首点写绝对坐标，后续点写相对 delta；width 必须 `> 0`；
- 现有 page GUID 复用，单 Workspace/默认 layer 0；DeviceKey 到显式 Device GUID 的稳定映射需要由快照提供，不能把 uint64 直接伪装成 UUID；
- runtime 已撤回内容不写入，隐藏但属于 UInk latest-group 语义的内容保留。

适配器不是要求 draw3 按 UInk wire schema 重构。若 page/Canvas 身份、撤回分组或受支持 Shape 的准确表达确实缺少必要信息，只调整最小的导出契约，并保持渲染热路径不依赖 codec。

### 11.2 Deferred UInk-To-Draw3 Import

本任务的读取终点是完整 `UInkDocument`/`UInkEditingSession`，不生成 `InkCanvasCollection`、GPU 对象、预览状态或 capability projection。通过模型 API 新增普通 UInk 内容后，保存与追加仍从完整模型生成，因而不需要先解决 draw3 如何显示高级内容。

未来反向接入已经选择“规范定义的安全回退优先，否则不显示”的 A 方向，例如 HDR 使用 Color Map 的 SDR fallback；但高级荧光笔如何投影成普通荧光笔、哪些 Shape 可以近似显示等均留给后续任务，本任务不实现也不测试。

reader 内部表示已确认采用 A：全部已注册 version 10 内容解析为项目自有的强类型 UInk 结构，只是不导入 draw3。合法的 Advanced Highlighter、draw3 未支持 Shape、HDR Color Map、多层内容或 Media 元数据均是正常读取成功；只有字段本身违反规范时才按对应块级规则诊断、回退或跳过。opaque 顶层 MessagePack pass-through 不属于首版模型契约。

## 12. Dependency Choice

- root `vcpkg.json` 增加 `msgpack`，先验证仓库 baseline 已锁定的 msgpack-cxx `7.0.0`，不启用 Boost feature。
- 选择理由：官方 C++ 实现、header-only、支持 packer 精确编码、对象树保留 Map entries、流式 unpack 和元素限制；比 C API 更符合当前 C++20 模块，比老旧 msgpack11 更适合严格协议实现。
- 具体 unpack API、zone 生命周期、异常边界和 module include 方式在实现第一阶段做一个小型编译验证，再固定封装，避免第三方类型泄漏到公共 module API。
- 若 7.0.0 缺少必须能力、存在已知缺陷或无法在当前模块工程可靠使用，允许升级 vcpkg baseline；升级前后记录所有端口版本差异并重跑完整解决方案验证。
- 首版不引入 ZIP 库。

## 13. Windows 7 SP1 Compatibility

- Windows SDK 10/v143 是编译环境，不改变 Windows 7 SP1 + KB2670838 的运行时下限。
- 文件 IO 使用 UTF-16 Win32 API；读取/写入基于 `CreateFileW`/`ReadFile`/`WriteFile`，安全截断基于 `SetFilePointerEx` + `SetEndOfFile`，持久刷新基于 `FlushFileBuffers`。
- 替换既有文件优先使用同卷临时文件和 `ReplaceFileW`；创建新目标使用 Win7 可用的 move/rename 路径。失败处理必须识别 `ReplaceFileW` 文档列出的部分移动状态，并利用备份/临时文件恢复可读版本；不能假设 API 返回失败就代表所有文件名完全未变。
- 不依赖 `PathCch*`、`GetTempPath2W`、`CopyFile2`、`SetFileInformationByHandle` 的新 rename class 或其他 Win8+ API。
- Header 只需 Unix 秒，使用 `GetSystemTimeAsFileTime` 或项目现有兼容层；禁止因标准库实现静态导入 Win8 才提供的 `GetSystemTimePreciseAsFileTime`。
- 第三方库和 vcpkg baseline 变更都要审计最终 imports。发现 Win8+ API 时，按项目现有 `win7_compat.cpp` 模式动态解析并提供 Win7 fallback，或更换实现。
- ARM64 开发机构建验证与 Win7 兼容审计分别记录；没有 Win7 目标机实测时不宣称运行验证通过。

微软官方文档把 `ReplaceFileW`、`SetEndOfFile`、`FlushFileBuffers` 的最低客户端列为 Windows XP；`GetSystemTimePreciseAsFileTime` 的最低客户端是 Windows 8。详细证据见 `research/win7-compatibility.md`。

## 14. Test Architecture

- `uink_codec_tests.cpp`：规范对象、精确字节、字段矩阵、重复键、数值兼容、未知类型。
- `uink_stream_recovery_tests.cpp`：对象边界、截断、垃圾字节、有效前缀、安全追加边界。
- `uink_semantics_tests.cpp`：注册树、Canvas、混合顺序、latest group、Erase、临时恢复及 draw3 不支持的全部已注册强类型结构；测试必须直接断言高级字段，不能只比较 raw MessagePack 字节。
- `uink_file_tests.cpp`：真实临时目录、完整保存、自检、原子替换故障注入、append、durable/buffered 结果、`WrittenNotDurable`、半写回滚、`PartialCommitRequiresRecovery`、短暂占用重试、外部替换和 revision 冲突。
- `uink_draw3_export_tests.cpp`：现有七种 StoredInkType、页面/Canvas、零宽和单向导出拒绝；不包含 UInk 反向导入或视觉测试。
- `uink_preservation_tests.cpp`：更多 Shape、高级荧光笔、HDR Color Map、多层 Canvas 和 Media 元数据与新增普通 UInk 内容共同往返。
- `uink_media_fallback_tests.cpp`：不打开 companion ZIP，验证合法/危险 path、资源 unavailable、布局保留、Ink/Shape 连续读取、原文件非 Media 追加和其他写入拒绝。
- `uink_conformance_tests.cpp`：下载/检入经过 SHA-256 固定的官方样例，逐项执行 `fixtures.json` 预期。
- 静态 import 检查：审计测试程序及最终接入二进制新增的 Kernel32/依赖入口点，拒绝无 fallback 的 Win8+ API。

所有测试必须无窗口运行。最终按项目要求构建完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64`，并运行对应 ARM64 测试可执行文件。

## 15. Risks And Mitigations

| 风险 | 后果 | 约束 |
| --- | --- | --- |
| MessagePack 解码先转 map | 重复已知键被吞掉 | 始终遍历原始 pair 列表 |
| Header 计数直接 reserve | 恶意文件内存放大 | 全局限额 + 渐进增长，不信任快照 |
| 子集模型直接重存 | 合法 UInk 内容静默丢失 | 完整独立模型或明确 Save As/有损确认 |
| 恢复读取自动截断 | 只读操作修改用户文件 | 读取纯只读，截断仅属于显式 append 事务 |
| 追加边编码边写或假定回滚必成 | 半对象或错误的“未修改”结果 | 全批次预编码；部分写失败先回滚，回滚失败返回 `PartialCommitRequiresRecovery` |
| 普通板擦作为 append | 旧对象与新片段同时生效 | 要求完整保存 |
| 两文件提交顺序错误 | 旧主文件引用资源失效 | 资源并集、资源包先提交、主文件最后 |
| 把“draw3 不支持”等同于“reader 不读取” | 高级 Shape/荧光笔/HDR 无法校验或重存 | reader 与 draw3 解耦，完整 UInk 会话是真值 |
| 加载后来源被外部修改 | 最后写入者静默覆盖 | `SourceRevision` 复核并返回 `SourceChanged` |
| 复核后目标在替换瞬间变化 | 非协作软件的版本被覆盖 | 事务 guard、ReplaceFile backup 核对、冲突恢复与 partial-commit 结果 |
| 直接读 runtime sidecar | 线程竞态或 GPU/控制器耦合 | 由调用方生成不可变导出快照 |
| 第三方异常越过 module 边界 | API 和构建不稳定 | 私有封装并转换为项目结果类型 |
| 主文件保存含 Media 引用 | companion 资源丢失或事务不完整 | 完整保存/Save As/Media 追加返回 `ResourcePackUnsupported`，只放行原文件非 Media 追加 |
| 新代码/依赖静态导入 Win8+ API | Win7 启动时 loader 失败 | Win7 API 实现、动态解析回退、最终 import 审计 |

## 16. Planning Readiness

用户拥有的范围、兼容性和风险决策均已确认：完整 UInk 强类型独立模型、Media 写入 A 方案、高级内容保留、不可变 draw3 导出快照、统一保守读取预算、默认 durable append、draw3 反向接入延期、短事务占用 + revision 冲突拒绝和 Win7 最低平台。合法但 draw3 不支持的已注册内容不产生损坏诊断，也不使用 opaque 顶层 pass-through；正式大文件支持延期。

在执行 `task.py start` 前，仍须先向用户呈现基于这些最终文档的完整规划摘要，并由用户在后续消息中明确批准开始实现。当前选择 append A 本身不构成该阶段批准。
