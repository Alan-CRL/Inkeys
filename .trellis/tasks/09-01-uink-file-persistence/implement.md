# UInk 文件持久化实施计划

> 本文件是批准后的执行顺序；用户已明确批准进入实现阶段。

## Phase 0 - Approval And Scope Freeze

- [x] 用户确认完整 UInk v10 独立模型 + draw3 当前能力单向导出方案；draw3 仅在准确导出确有需要时最小调整。
- [x] 用户确认首版只实现 `.uink` 主文件；Media 资源引用按 unavailable 回退，不打开 `.uink.extra`。
- [x] 用户确认 Windows 7 SP1 + KB2670838 为硬性最低运行平台，并允许在确有必要时升级 vcpkg baseline。
- [x] 用户确认含 Media 文档采用 A：仅允许向原文件追加非 Media 对象，拒绝完整保存、Save As 和 Media 追加。
- [x] 用户确认已知但 draw3 不支持或仅降级显示的规范内容留在完整 UInk 真值中，并跨新增内容及后续写出保留。
- [x] 用户确认文件占用采用 A：每次操作短时占用、编辑期间释放，并以来源 revision 拒绝外部修改冲突。
- [x] 用户确认本任务不做 UInk 到 draw3 的导入或渲染；未来接入采用选择性安全降级 A，但另立任务设计和验证。
- [x] 用户确认已注册高级内容采用 A：全部已注册 version 10 内容进入项目自有强类型 UInk 模型，不使用 opaque 顶层 pass-through，也不因 draw3 不支持而报文件损坏。
- [x] 用户确认 draw3 导出采用 A：调用方在安全点构造不可变 export snapshot，持久化模块不直接读取 DrawingController 或 runtime sidecar。
- [x] 用户确认读取预算采用 A：所有架构使用统一保守默认档，允许显式覆盖但无无限制入口；正式大文件支持延期。
- [x] 用户确认 append 采用 A：默认 `FlushFileBuffers`，显式 buffered 模式必须标记；完整写入后刷盘失败返回 `WrittenNotDurable`，不得盲目重试。
- [x] 根据全部批注更新 `prd.md`/`design.md`，移除互斥方案和未决占位并完成 PRD 收敛。
- [x] 用户在看到最新最终规划摘要后的后续消息中明确批准开始实现。
- [x] 执行 Trellis pre-development context，复核 native/module/test 规范后再 `task.py start`。

完成条件：PRD、设计和验收标准无影响架构的未决项。

## Phase 1 - Dependency And Build Spike

- [x] 在 root `vcpkg.json` 最小新增 `msgpack`，保留当前 builtin baseline 和现有依赖顺序风格。
- [x] 用私有实现文件验证 msgpack-cxx `7.0.0` 在 ARM64 static triplet、C++20 module scan 和现有 PCH/编译选项下可用。
- [x] 若 7.0.0 缺少必要能力或存在阻塞缺陷，形成证据后再升级 baseline，并记录所有连带端口变化；没有必要则保持 baseline。
- [x] 验证显式 uint16/uint32/uint64/int32/float32/float64、str8、Map/Array 编码字节。
- [x] 验证 unpack 能逐个返回对象消费长度、保留重复 Map key，并配置/外包深度与容器限额。
- [x] 第三方类型不得出现在导出 module API。
- [x] 审计 msgpack 和新增编译产物的静态 imports，确认不因依赖引入 Win8+ 启动入口点。

完成条件：最小 round-trip 测试在 ARM64 无窗口运行，依赖选择没有工程阻塞。

## Phase 2 - UInk Model And Diagnostics

- [x] 新增 UInk version/type 常量、GUID/隐式 key、Header、注册表、Canvas、viewport、Color、Ink、Shape、Media 和 ordered content 模型；覆盖高级荧光笔点样式、全部注册 Shape geometry/stroke/fill、HDR 与 Media 主文件字段。
- [x] 新增受资源预算约束的项目自有通用 MessagePack 值树保存块内私有 `extra`，并保持第三方 msgpack 类型只存在于 codec 私有边界。
- [x] 新增稳定诊断 code、severity、object/offset/field context。
- [x] 新增 `UInkReadLimits`、确定性模型计费、加载 provenance、来源 revision、Save As/unknown/recovery flags；默认值固定为 128 MiB 文件、256 MiB 模型、250,000 顶层对象、32 MiB 单对象、4,000,000 总点、1,000,000 单对象点、深度 32、8 MiB string/binary、1,000,000 容器项。
- [x] 定义持有完整文档、来源 revision、加载诊断和稳定逻辑内容身份的 `UInkEditingSession`；不依赖 draw3 投影。
- [x] 实现注册树、Canvas identity、page/layer 连续性和 latest-group 的纯逻辑校验器。
- [x] 添加模型不变量和纯 CPU 测试。

完成条件：不依赖文件 IO 即可表达规范全部已注册结构和恢复状态。

## Phase 3 - Exact MessagePack Codec

- [x] 实现 Header 固定 `array(7)` 编解码和 UInk 10 识别拒绝路径。
- [x] 实现公共 exact numeric conversion、finite/range、UUID、Map key 和 duplicate-known-key helpers。
- [x] 将全部已注册 version 10 字段解码为强类型项目模型并执行字段级校验；合法但 draw3 不支持的内容正常成功，不走 opaque 顶层 pass-through。
- [x] 依次实现 Header Extension/Device/Workspace、Canvas/viewport、Color、Ink、Shape、Media 编解码。
- [x] 实现顶层对象流状态机、unknown Type skip、block fallback/skip 和 valid-prefix 记录。
- [x] 实现 EOF boundary、truncated tail、mid-stream corruption、limit exceeded 结果。
- [x] 在任何声明长度分配/reserve 前用安全算术检查限额；`LimitExceeded` 不返回 document 或 safe append offset，并覆盖默认边界与 `+1`、调用方覆盖和跨架构确定性测试。
- [x] 将 `research/uink-v10-spec.md` 的错误矩阵逐行转换为表驱动测试。

完成条件：内存字节流读取/写入通过规范字段、恢复和精确编码测试。

## Phase 4 - Conformance Fixtures

- [x] 将官方 `fixtures.json`、`SHA256SUMS.txt` 和 9 个输出以可追溯方式放入测试夹具或由固定校验脚本获取；不得在测试时依赖在线网络。
- [x] 校验每个 fixture 的字节长度和 SHA-256。
- [x] 对 8 个 `.uink` 验证 expectedObjects、隐式/显式模型、numeric compatibility、unknown skip 和 truncated recovery。
- [x] 记录 `.uink.extra` fixture 的 manifest/SHA-256，但首版不打开或解析 ZIP；验证对应主文件 Media 按 resource-unavailable 回退。
- [x] 为规范未覆盖的恶意/错误输入补充本地生成样例。

完成条件：公开样例全部产生预期结果，fixture manifest 的过时 `pre-Beta` 描述不改变 version 10 Beta 解释。

## Phase 5 - Full File Save

- [x] 实现只读文件加载、稳定文件身份采集和完整诊断返回。
- [x] 实现 save plan：从完整编辑会话选择有效内容、保持高级对象、处理永久 identity、规范顺序、Header 快照和 content/undo 重新编号。
- [x] 实现同目录临时主文件、强制 flush/close、严格自检和 Windows 原子替换；完整保存不提供 buffered 模式。
- [x] 实现短事务占用、有界重试、来源/目标 revision 复核、ReplaceFile backup 核对和 `SourceChanged`/partial-commit 结果；编辑期间不持有文件句柄。
- [x] 文件事务只使用 Windows 7 可用的 UTF-16 Win32 API；拒绝无 fallback 的 Win8+ 文件或时钟 API。
- [x] 区分保存既有逻辑文件、Save As 和显式允许有损覆盖。
- [x] 含 Media 引用文档的完整保存和 Save As 在修改目标前返回 `ResourcePackUnsupported`。
- [x] 添加 IO/磁盘写入/校验/替换失败的故障注入，验证原文件不变。

完成条件：所有成功结果可由严格 reader 重读，所有失败结果保留原目标。

## Phase 6 - Incremental Append

- [x] 实现纯 `AnalyzeAppend`：来源状态、最后 Canvas、编号、结构变化和 full-save 原因。
- [x] 实现 invalid tail 分类、安全截断位置及“中间无效块”拒绝。
- [x] append batch 在内存中完整验证和编码；执行前复核来源 revision 与长度。
- [x] 在同一独占事务句柄内实现复核、可选截断、追加和默认 `FlushFileBuffers`；显式 buffered 返回 `CommittedBuffered`，刷盘成功返回 `CommittedDurable`。
- [x] 实现写入失败回滚到 batch 起点、`TailRepairedNoAppend`、回滚失败的 `PartialCommitRequiresRecovery` 和完整写入后刷盘失败的 `WrittenNotDurable`；结果携带最后可信边界/新长度/revision 并阻止调用方盲目重试。
- [x] 含 Media 来源只允许原文件非 Media 追加；任何 Media append 在开写句柄前返回 `ResourcePackUnsupported`，保证既有 `.uink` 和 `.uink.extra` 不变。
- [x] 覆盖追加 Ink/Shape、新 Canvas、空 Canvas、恢复后追加、durable/buffered、flush failure、半写回滚及回滚失败、短时占用和并发外部修改拒绝。

完成条件：成功追加只产生规范完整对象；所有无法恢复原状的错误都返回带最后可信边界的 partial-commit 状态，不伪报未修改或成功。

## Phase 7 - Media Fallback Without Resource Pack

- [x] 完整解析和保留 `.uink` 内 Media 元数据、内容顺序和撤回分组。
- [x] 验证 NFC 相对 path 语法，但不把 path 解析为本地文件、不发现 companion ZIP、不读取任何 entry。
- [x] 所有 Media 资源返回 unavailable 状态与稳定诊断；保留视觉布局/PDF 页占位信息并继续读取 Ink/Shape。
- [x] 允许严格验证后的原文件非 Media 追加；拒绝 Media append、完整保存和 Save As。
- [x] 为合法引用、路径穿越、URI、盘符、反斜杠、控制字符和缺失 companion 建立无文件访问测试。

完成条件：第三方含 Media 的主文件可安全降级读取，首版不会打开 `.uink.extra` 或伪装完成资源保存。

## Phase 8 - Draw3 One-Way Export And Preservation

- [x] 定义无控制器依赖、所有权与生命周期封闭的 draw3 export snapshot 和能力报告；snapshot 显式携带页面/Canvas 身份、有序有效内容、撤回分组和 Workspace/Device 描述，不定义 UInk import/preview API。
- [x] 映射现有 Pen/Highlighter/Eraser 点列和样式，转换首点绝对/后续相对坐标。
- [x] 覆盖 Advanced Highlighter、全部注册 Shape、HDR Color Map、多层拓扑、Media 元数据和块内 `extra` 的强类型读取与模型级往返；新增普通 UInk 内容后再次写出仍保留原高级语义和相对顺序。
- [x] 精确核实现有 Solid/Dashed Line、Outline/Filled Rectangle 的渲染语义后映射 Shape。
- [x] 映射现有页面 GUID、Workspace、Device 和 layer-0 viewport；对非 1.0 scale 等能力差异返回诊断。
- [x] 从 runtime snapshot 生成有效内容和 undo groups；不保存 redo、GPU cache 或预测点，转换过程不访问 Controller、sidecar 或可变运行时状态。
- [x] 通过纯 UInk 模型 API 向已加载会话新增普通内容，不以 draw3 snapshot 整体覆盖加载会话。
- [x] 覆盖更多 Shape、高级荧光笔、HDR Color Map、多层 Canvas 和 Media 元数据与新增普通内容一起语义往返；不启动或模拟渲染。

完成条件：当前 draw3 可表达子集能准确单向导出，完整 UInk 模型中的支持范围外内容可独立读写和保留。

## Phase 9 - Quality Gate

- [x] 使用 `rg` 静态核对模块导入、公共 API、TODO 和错误路径。
- [x] 运行 `git diff --check`，确认新增源码保持 UTF-8 BOM + CRLF，任务文档保持原项目格式。
- [x] 使用 ARM64 `MSBuild.exe` 构建完整 `inkStrokeModelerTest.sln /p:Configuration=Debug /p:Platform=ARM64`，超时不少于 5 分钟。
- [x] 运行全部 ARM64 无窗口测试；按变更风险补充 Release 构建或测试。
- [x] 审计新增静态 imports、DLL 和 CRT/第三方依赖，确认 Windows 7 SP1 + KB2670838 启动边界；发现 Win8+ API 时必须提供动态回退或移除。
- [x] 若可用，在 Windows 7 SP1 + KB2670838 x86/x64 目标机运行主文件读/写/追加无窗口测试；若环境不可用，明确记录“代码/API 兼容已审计，目标系统待验证”。
- [x] 使用 Trellis check，逐项回填 Acceptance Criteria 和实现日志。
- [x] 运行文件占用、来源 revision、外部替换、并发 append、flush failure/半写回滚、Media 写入拒绝和高级内容保留的无窗口回归测试。
- [x] 不启动 GUI，不操控桌面；仅在用户明确授权后提交 commit。

完成条件：规范一致性、恢复、文件事务、适配和工程构建全部通过，未执行或受限验证明确记录。

## Verification Log - 2026-09-01

- 完整 `Debug|ARM64` solution 使用 ARM64 原生 MSBuild 构建通过，随后全部无窗口测试通过；源码格式归一化后重复该门禁仍通过。
- 完整 `Release|x64` 与 `Release|x86` solution 构建和全部无窗口测试通过。
- 8 个官方 `.uink` fixture 的长度与 SHA-256 逐项匹配清单；仅记录 `.uink.extra` manifest/hash，仓库中未落地 companion 文件。
- UInk 测试覆盖规范编解码、错误/恢复矩阵、全部默认限额、事务故障注入、并发 revision 冲突、Media 早拒绝、强类型高级内容保留和 draw3 不可变单向导出。
- x64/x86 PE 与 imports 审计未发现无 fallback 的 Win8+ API；msgpack-cxx `7.0.0` 为 header-only，无新增运行时 DLL。
- Windows 7 SP1 + KB2670838 目标机环境不可用：代码/API/PE/import 兼容已审计，目标系统待验证。
- `git diff --check` 与源码边界静态扫描通过；未启动 GUI、未接入自动保存/读取场景；本次提交由用户单独明确授权。

## Phase 10 - Deferred Integration And Manual Acceptance

- [ ] 待后端上层接入本模块后，验证真实调用链中的读取、完整保存和追加写入行为。
- [ ] 在具备人工验收条件后完成文件互操作检查，再由用户确认是否结束并归档本任务。

当前实现提交不代表任务结束；`task.json.status` 继续保持 `in_progress`。

## Planned File Ownership

实际文件名在 Phase 1 后固定，预期最小影响范围：

- `vcpkg.json`
- `inkStrokeModelerTest/draw3/uink_model.cppm` 与实现文件
- `inkStrokeModelerTest/draw3/uink_codec.cppm` 与实现文件
- `inkStrokeModelerTest/draw3/uink_file.cppm` 与实现文件
- `inkStrokeModelerTest/draw3/uink_draw3_export.cppm` 与实现文件
- `inkStrokeModelerTest/inkStrokeModelerTest.vcxproj` 及 filters（若工程未使用自动 glob）
- `inkStrokeModelerTestTests/` 下聚焦 UInk 的测试与 fixtures

不计划修改 DrawingController 的输入、绘制或自动保存路径。
