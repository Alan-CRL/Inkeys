# UInk 文件编解码与追加写入

## Status

- 当前阶段：用户已在最终规划摘要后的后续消息中明确批准开始实现，进入 Trellis 执行阶段。
- 实现边界：只实现独立能力层、工程登记与无窗口验证；不接入自动保存、场景写入、UI 或 UInk 到 draw3 的反向渲染。

## Goal

为 draw3 新增一组独立的 UInk 1.0 Beta（`Header.version = 10`）持久化模块，提供：

1. 从 `.uink` 连续 MessagePack 对象流读取有效内容，并按规范恢复截断尾部、处理未知或不规范内容；
2. 将内存模型完整保存为规范的 `.uink` 文件，并使用临时文件校验后原子替换；
3. 在满足规范和首版资源边界时，向文件末尾追加完整 Canvas/Ink/Shape 对象；Media append 等待 `.uink.extra` 事务能力；
4. 向调用方返回结构化诊断、有效前缀、安全追加位置和是否必须完整保存/另存为等决策信息；
5. 与现有 draw3 墨迹文档建立明确的转换边界，但暂不把能力接入 DrawingController、自动保存、文件对话框或具体业务场景。

## Normative Baseline

- 规范基线是 UInk 1.0 Beta version `10`，线格式已冻结；旧 `array(6)` Header、顶级 Device、Type ID `5` 为 Media 等预 Beta 草案不在兼容范围。
- 规范源以用户授权的本地原始文档 `D:\Project\Inkeys\website\docs\standard` 为准，并与当前发布页 `https://www.inkeys.top/standard/intro` 核对。
- 本任务必须覆盖该目录内全部 14 篇规范文档以及公开 `uink-v10` 一致性样例；详细索引见 `research/uink-v10-spec.md`。
- 首对象 Header 无效时拒绝按 UInk 10 解析；其余完整对象按规范执行“跳过、字段回退、临时内存修复或停止在有效前缀”，不得自行扫描字节重同步。

## Requirements

### 1. 模块边界

- UInk 模型、MessagePack 编解码、文件事务、追加判定和 draw3 转换必须与渲染控制器解耦。
- 核心 API 为同步、无窗口、可单元测试的能力层；线程调度、自动保存频率和 UI 提示由未来调用方负责。
- 不把文件路径、运行时可见性、ZIP 解码器状态或诊断信息塞进现有 `InkStroke`/`InkCanvas` 持久对象。
- 已确认采用完整 UInk v10 独立文档模型与编解码器，再提供 draw3 当前能力适配器；不能为了适配 draw3 子集而静默丢弃合法 UInk 数据。
- 现有 draw3 内部结构保持优先，保存和读取允许通过显式转换完成；只有转换正确性、状态快照或无损能力确有证据表明现有接口不足时，才允许对 draw3 做范围明确的最小调整，不把 UInk wire 字段机械搬入运行时模型。

### 2. 读取

- 按顶层 MessagePack 对象边界顺序解析，不假设存在外层 Array，也不依赖 Header 计数进行不受限分配。
- 严格验证 Header `array(7)`、Type ID、version、UUID 和字段顺序；数值兼容只接受数值不变、目标类型可精确表示且字段范围有效的转换，`bool` 不按数值处理。
- Map 解析必须保留键的原始出现顺序，以便发现任意已知键重复；不能先落入会吞掉重复键的 `std::map`。
- 支持显式或隐式 Device/Workspace、Canvas 作用域、Ink/Shape/Media 混合顺序、共享 `contentId`/`undoId`、`renderOnlyWhenLatest`、Color Map 和全部块级容错规则。
- 返回加载状态、有效前缀长度、失败对象起始偏移、可安全追加边界、未知内容标记、是否应另存为及结构化诊断。
- 所有临时身份、断环、默认 viewport、颜色/样式回退等恢复只存在于本次加载结果，不得自动写回源文件。
- 解析和构建过程必须使用所有架构一致的默认 `UInkReadLimits`：主文件 `128 MiB`、模型计费 `256 MiB`、顶层对象 `250,000`、单顶层对象 `32 MiB`、全文件 Ink/Shape 总点数 `4,000,000`、单 Ink 或单 Shape geometry `1,000,000` 点、MessagePack 深度 `32`、单 string/binary `8 MiB`、单 Array/Map `1,000,000` 项；调用方可显式收紧或放宽，但不得使用无限制默认入口。
- 任一资源限额超出均返回致命 `LimitExceeded`：不返回可用的部分 `UInkDocument` 或 `safeAppendOffset`，不修改来源文件；模型计费采用与 ABI 无关的保守固定规则，在声明长度分配和容器扩容前检查，使 Win32/x64/ARM64 的默认边界一致。

### 3. 完整保存

- 写入字段表声明的精确 MessagePack 类型和位宽，Header 必须使用固定布局和 `str8` UUID；不得依赖“最小整数编码”。
- 从当前有效逻辑状态重算 Header 快照、Canvas 顺序、连续 `contentId` 和非递减 `undoId`。
- 同一逻辑文件完整保存保持 Header、Workspace、Device 和页面永久 GUID；另存为新逻辑文件生成新的 Header GUID。
- 先写同目录临时文件并强制 `FlushFileBuffers`，关闭后完整回读/校验临时结果，再原子替换目标；完整保存没有关闭刷盘的选项，禁止边解析原文件边覆盖。
- 外部导入或包含未知内容的文件默认标记为 Save As；只有显式的“允许有损覆盖”选项才能覆盖原文件。
- 保存 API 不接受隐式修复结果直接回写；调用方必须显式选择规范化保存或另存为。

### 4. 追加写入

- 只在 EOF 追加一个或多个完整顶层对象；不写半个 Map，不原地更新 Header。
- 只允许向最后一个 Canvas 追加完整 Ink/Shape，或追加引用既有注册项的新 Canvas 及其非 Media 完整内容；任何包含 Media 的 append batch 都在打开写句柄前返回 `ResourcePackUnsupported`。
- 校验 Canvas 唯一键、连续 `contentId`、非递减 `undoId`、最后 Canvas 归属和所有“必须完整保存”的场景。
- 对恢复文件追加前，先根据读取结果确定安全边界并截断无效尾部；若完整无效块之后仍有有效块，拒绝中间截断追加并要求完整保存。
- 追加调用必须先完成全部对象编码和校验，再改变目标文件；部分写入失败时在同一独占句柄内截断回 batch 起点。若该回滚也失败，返回 `PartialCommitRequiresRecovery`、最后可信边界和系统错误，不得伪报目标未修改或成功。
- append 默认在整批写完后、成功返回前调用 `FlushFileBuffers`；只有测试或调用方明确承担风险的批处理才能显式选择 buffered 模式，成功结果必须区分 `CommittedDurable` 与 `CommittedBuffered`。
- 若整批对象已经完整写入，但 `FlushFileBuffers` 失败，返回 `WrittenNotDurable`、新长度和新来源 revision；该结果明确表示文件已改变且持久性未知，调用方不得把它当作“未写入”而盲目重试同一 batch。

### 5. `.uink.extra` 与 Media

- 已确认首版不实现 `.uink.extra` ZIP 的发现、打开、读取、复制、重建或双文件提交，也不新增 ZIP 依赖。
- `.uink` 主文件仍必须完整解析 Media 块、验证资源路径语法并保留 MIME、布局、顺序、撤回、播放和 PDF 页元数据。
- 读取其他软件生成的 Media 资源引用时，不跟随或打开内部 `path`；将资源标记为不可用并返回诊断，同时保留 Media 布局/顺序，继续加载基础 Ink/Shape 和后续块。
- 本任务不把 Media 或其他 UInk 内容反向导入 draw3；完整 UInk 模型仍保留 Media 元数据，避免读取阶段静默丢失。
- 含 Media 引用的来源文档只允许向原 `.uink` 文件追加不含 Media 的 Ink/Shape/Canvas 对象；执行前必须严格验证追加计划，原有主文件前缀和 companion `.uink.extra` 保持逐字节不变。
- 含 Media 引用文档的完整保存和 Save As，以及任何来源上的 Media append，一律以结构化 `ResourcePackUnsupported` 拒绝；首版不得制造资源丢失或伪装成完整资源保存。

### 6. draw3 适配

- 对 draw3 原生场景，现有 `InkCanvasCollection` 仍是运行时 CPU 文档真值；预测点、GPU 缓存和合成缓存不得进入 UInk。
- `.uink` 读取在本任务中只生成独立的完整 `UInkDocument`/`UInkEditingSession`，不导入 `InkCanvasCollection`，也不决定预览、降级渲染或不显示策略。
- UInk 读取结果必须在不依赖 draw3 的情况下，以完整强类型结构保留全部已注册 version 10 语义，包括更多 Shape、高级荧光笔点样式、HDR Color Map、多 Workspace/Device/图层、任意合法 viewport 和 Media 主文件元数据；这些合法内容不得因 draw3 暂不支持而产生文件损坏诊断。
- 每个已注册字段都必须进入可字段级访问、校验和再次编码的项目自有类型；块内私有 `extra` Map 使用有资源预算的通用 MessagePack 值树保留，不向公共 API 暴露第三方 msgpack 对象，也不与 companion `.uink.extra` 文件混淆。
- 为保存当前 draw3 文档，采用无控制器依赖的单向 `Draw3UInkExportSnapshot -> UInkDocument` 转换；快照由 draw3 调用方在绘制线程安全点构造，封闭其数据所有权和生命周期，并显式携带页面/Canvas 身份、有序有效内容、撤回分组及 Workspace/Device 导出描述。它只处理当前可准确表达的 Pen/Highlighter/Eraser/四类 Shape 和文档身份，不承担反向导入或渲染。
- 加载会话通过 UInk 模型级 API 新增普通内容后，后续保存或追加必须保留先前高级内容及其相对语义顺序；实际 draw3 编辑接入另立后续任务。
- 单向导出必须显式处理当前模型差异：单 Workspace、`DeviceKey`、无图层、viewport 仅允许 scale=1、Shape 内嵌在 `StoredInkType`、无 Media、无持久 `contentId`/`undoId`、撤回可见性位于 runtime sidecar。
- 已选择的“规范回退优先、否则不显示”A 方案仅作为未来 `UInkDocument -> draw3` 接入指导，本任务不实现或测试视觉降级。
- 持久化模块不得持有 `DrawingController` 指针、获取 runtime locks 或自行读取 sidecar；本任务不修改 DrawingController 的自动保存或输入提交路径，未来接入只负责在安全点生成不可变快照并调用能力层。

### 7. 依赖与工程约束

- MessagePack 首选 vcpkg 的 `msgpack`（msgpack-cxx）端口，先验证仓库当前 baseline 的 `7.0.0`，不启用不需要的 Boost feature。
- 已允许在确有必要时升级 vcpkg baseline；升级必须由缺失的协议能力、编译兼容或已修复缺陷等证据驱动，并审计所有连带端口版本变化，不能只为追逐上游最新版升级。
- 使用显式 packer API 控制整数、浮点、字符串和容器编码；使用能够保留 Map 原始键项并支持流式对象边界/上限控制的解码路径。
- 新增 C++ 模块遵循现有 `.cppm` 公共接口、`.cpp` 实现、UTF-8 BOM + CRLF 和中文关键注释规范。
- 修改保持最小范围，不顺带重构 draw3 文档、历史或渲染系统。

### 8. 最低平台兼容

- Windows 7 SP1 + KB2670838 是本任务不可降低的最低运行平台；Windows 11 ARM64 构建成功不能替代该兼容结论。
- 文件读取、截断、刷盘、临时文件和原子替换必须使用 Windows 7 已存在的 Unicode Win32/CRT 能力，避免静态导入 Win8+ API。
- 若标准库或第三方依赖在 v143 下引入 Win8+ 入口点，必须改用 Win7 实现、复用项目现有动态解析回退模式，或更换依赖；不得要求安装 KB2670838 之外的新平台补丁来启动。
- Header 时间戳不要求 Win8 的高精度时钟；使用 Win7 可用时钟或项目现有兼容层即可。
- 实现后必须审计最终二进制静态 imports 和新增 DLL 依赖；没有目标系统实测记录时，只能声明“符合代码/API 兼容约束，目标环境待验证”，不得虚报已在 Win7 运行通过。

### 9. 文件占用与外部冲突

- 读取、完整保存和追加必须防止进程内及跨进程冲突；禁止默认为最后写入者覆盖外部改动。
- 可借鉴现有配置模块“短时文件占用 + 有界重试”的方式，但不能沿用其原地截断覆盖策略；只对共享冲突/锁冲突等可恢复错误重试，并返回稳定诊断。
- 读取结果携带来源 revision/fingerprint；完整保存和追加在受控文件占用期间重新验证来源身份与内容版本，发现外部修改时返回结构化 `SourceChanged`，不静默覆盖或自动合并。
- 追加的复核、安全截断、写入和 flush 必须位于同一次独占事务句柄内；完整保存继续使用同目录临时文件、自检和 Win7 兼容替换，并处理复核到替换之间的竞争窗口。
- 已确认采用 A：读取、完整保存和追加各自短时占用文件，编辑期间不持有句柄；不采用整个编辑会话持续独占。

## Acceptance Criteria

- [x] 读取公开 8 个 `.uink` 样例，结果符合 `fixtures.json` 的对象数、恢复与语义预期，并校验 SHA-256 清单；记录但不解析首版范围外的 `.uink.extra` 样例。
- [x] Header、全部已注册块和嵌套结构具有规范编码/解码测试；写入测试逐字节确认固定宽度、Header `array(7)` 和对象流无外层容器。
- [x] 全部已注册 version 10 内容均能从强类型 UInk API 做字段级断言；高级荧光笔点样式、全部 Shape geometry/stroke/fill、HDR Color Map、拓扑、viewport、PPT 和 Media 主文件字段不得退化为原始顶层 MessagePack pass-through，块内私有 `extra` 使用有界通用值树往返。
- [x] 数值兼容、已知键重复、错误类型、缺失必填字段、未知 Type ID/子类型、NaN/Infinity、非法 UUID、非法顺序和 Header 快照不一致均有表驱动测试。
- [x] 默认 `UInkReadLimits` 的每项边界值与 `+1` 拒绝路径均有测试；Win32/x64/ARM64 使用相同数值和确定性模型计费，`LimitExceeded` 不返回可用文档/追加位置且不修改来源。
- [x] 截断尾对象、对象间垃圾、非末尾解码错误、完整无效尾块以及“无效块后仍有有效块”覆盖读取状态和追加边界测试。
- [x] Ink/Shape/Media 混合顺序、Erase 作用域、`renderOnlyWhenLatest` 跳过 Media 的反向扫描、撤回分组和隐藏原稿保留有测试。
- [x] 显式/隐式注册表、父引用断环、Canvas 临时身份、多 Device/图层排序、PPT 失配不误附着、viewport 继承/回退有测试。
- [x] 完整保存强制刷写临时文件、关闭、严格校验并原子替换；故障注入证明原文件保持可读，并验证同文件保存与 Save As 的 GUID 语义。
- [x] 追加成功、拒绝、恢复后截断、部分写入/回滚失败和刷盘失败均有真实文件/故障注入测试；可回滚失败不留下本次调用生成的半对象，回滚本身失败返回 `PartialCommitRequiresRecovery` 和最后可信边界，完整 batch 写入后的刷盘失败返回 `WrittenNotDurable` 和新 revision，二者都不会被建议盲目重试。
- [x] 短时占用、有界重试、来源 revision 复核、外部替换和并发追加均有测试；编辑期间不持有文件句柄，冲突返回 `SourceChanged`，不静默覆盖或自动合并。
- [x] Media 引用在不打开 `.uink.extra` 的前提下保留主文件元数据与布局、返回资源不可用诊断，并且不影响 Ink/Shape；非法/危险 path 永不触发文件访问。
- [x] 含 Media 的来源仅允许向原文件追加非 Media 对象；含 Media 来源的完整保存/Save As 和任何 Media append 返回 `ResourcePackUnsupported`，且拒绝发生在开写句柄前，原 `.uink` 与 `.uink.extra` 均不改变。
- [x] draw3 单向导出覆盖当前 Pen/Highlighter/Eraser/四类 Shape、页面/Canvas/viewport 和运行时撤回快照；快照与控制器/sidecar 解耦且不引用可变运行时状态，不支持的导出值返回结构化诊断，不静默伪造。
- [x] 加载含更多 Shape、高级荧光笔、HDR Color Map、多图层或 Media 元数据的文件后，通过 UInk 模型新增普通内容并写出仍保留原高级内容及语义顺序；测试不依赖 draw3 导入或渲染。
- [x] 最终二进制不静态导入 Win8+ 文件/时钟 API，新增依赖不提高 Windows 7 SP1 + KB2670838 启动门槛；目标系统未实测时明确记录为待验证。
- [x] `git diff --check`、静态规范检查、完整解决方案 `Debug|ARM64` 构建和无窗口测试通过；构建使用 ARM64 MSBuild，超时不少于 5 分钟。

## Out Of Scope

- 自动保存时机、定时器、抬笔即写接入、关闭/切页保存策略和 UI 提示。
- 文件选择器、最近文件、云同步、网络传输、缩略图和业务场景绑定。
- `UInkDocument -> InkCanvasCollection` 导入、draw3 文件会话绑定、预览、选择性降级和不支持内容的视觉占位；另立后续接入任务。
- 为适配 UInk 而扩展 draw3 渲染器的多图层、Media、HDR、高级荧光笔、任意 viewport scale 或完整 Shape 渲染能力。
- `.uink.extra` ZIP 读取、写入、复制、清理、资源解码与 Media 控件；后续另立任务实现。
- 支持 UInk version 10 预 Beta 草案或未来未发布版本的迁移。
- 为超出首版默认预算的大文件提供正式兼容承诺、性能目标、架构差异档、懒加载、内存映射或分段文档模型；调用方显式放宽限额不等于首版承诺这些能力。
- 修改 UInk 规范网站或公开样例；发现的上游元数据问题只在研究记录中说明。

## Research

- `research/uink-v10-spec.md`：14 篇规范、恢复/容错矩阵和公开样例核对。
- `research/draw3-storage-gap-analysis.md`：当前 draw3 数据模型、历史 sidecar 与 UInk 映射差距。
- `research/msgpack-library-selection.md`：vcpkg MessagePack 候选和选型结论。
- `research/win7-compatibility.md`：最低平台证据、允许 API 和依赖/import 审计边界。
- `research/file-conflict-strategy.md`：现有配置占用机制、来源 revision 和读/保存/追加冲突方案。
- `research/unsupported-content-read-policy.md`：draw3 当前/规划能力缺口、完整 UInk 模型边界和读取策略建议。
- `design.md`：推荐架构和 API 契约草案。
- `implement.md`：待批准后的分阶段实施与验证顺序。
