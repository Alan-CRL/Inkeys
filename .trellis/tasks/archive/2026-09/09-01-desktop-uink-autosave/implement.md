# 桌面批注 UInk 自动保存实施计划

> 用户已批准实现。Phase 0-7 已完成，Phase 8 的结果与受限项记录在 `verification.md`。

## Phase 0 - Approval And Pre-Development Gate

- [x] 用户审核 `prd.md`、`design.md` 与本实施计划的最终摘要，并在摘要之后明确批准开始实现。
- [x] 使用 `trellis-before-dev` 完整读取 native、native-desktop、Draw3、设置 UI、工程文件和测试相关规范。
- [x] 执行 `python .\.trellis\scripts\task.py start 09-01-desktop-uink-autosave`，确认任务由 `planning` 进入实现阶段。
- [x] 重新检查工作树，只处理本任务文件，不覆盖用户或其他任务的现有改动。
- [x] 固定实现期验证命令与 ARM64 原生 `MSBuild.exe` 路径；不启动 GUI，不创建 commit。

完成条件：最终规划已获明确批准，项目规范和工作树边界清楚，Trellis 任务正式启动。

## Phase 1 - UInk Production Integration Spike

- [x] 审计现有 `uink_model`、`uink_codec`、`uink_file`、`uink_draw3_export` 的 module imports 与测试宿主依赖。
- [x] 选择最小的单一源码接入方式：优先让 `Inkeys.vcxproj` 链接现有 UInk 模块；只有工程结构确实要求时才迁到产品与测试共同引用的共享目录，禁止复制实现。
- [x] 验证产品 `draw3.ink_document` 与现有 UInk exporter 类型一致，公共 API 不暴露测试工程类型。
- [x] 在功能接线前构建完整 `InkeysRepo.sln` 的 `Debug|ARM64`，尽早暴露 module scan、PCH、依赖顺序或 PptCOM 工程问题。
- [x] 保留完整保存、自校验、`fileGuid` 和 durable 文件提交能力；自动保存路径不得使用 append。

完成条件：产品工程可复用唯一 UInk 实现并通过基线构建，不改变 UInk v10 线格式。

回滚点：如果现有模块无法无损进入产品工程，停止功能接线并记录具体 import/build 阻塞；不得临时复制编码器绕过问题。

## Phase 2 - Three-State Scene Identity And Eligibility

- [x] 将 Bridge workspace 明确拆为 Desktop、Whiteboard、Presentation/PPT，默认普通批注场景为 Desktop。
- [x] 更新 Host、Product、DrawingController、WindowControl、PPT 联动和所有 switch/call site，保持 Whiteboard 文档与 PPT page 语义不变。
- [x] 修正 workspace 发布数据，使 Desktop 发布不会因旧 `hasPage` 残留而被识别为 PPT；PPT 进入事件必须是明确真值。
- [x] 增加纯逻辑 `Eligible/PptTouched` 保存资格状态机：进入 PPT 后锁定，返回 Desktop 不恢复，Desktop Clear 完成后恢复。
- [x] 保持 Whiteboard 切换不改变主画布资格；本阶段不拆分 Desktop/PPT 文档，也不修复共享内容迁移。
- [x] 添加三态发布、默认场景和所有资格转换的 headless tests。

完成条件：代码可稳定区分三态，任何共享画布曾进入 PPT 的区间都不能被误存为 Desktop。

回滚点：三态枚举会影响 Bridge ABI 与多个调用点；若出现无法在本任务内闭合的协议兼容问题，保留测试证据并暂停，而不是退回模糊的 Presentation 判断。

## Phase 3 - Settings Gate

- [x] 恢复当前被隐藏的保存开关 UI，仅开放本任务需要的 `saveSetting.enable` 控件与持久化路径。
- [x] 让开关在应用启动时正确恢复，并把值安全传到 Desktop 自动保存请求入口。
- [x] 关闭开关时不捕获快照、不入队、不创建 AutoSave 目录或索引；已经接受的请求不取消。
- [x] 保留 `saveDays` 的现有配置兼容，但明确不把它连接到新 UInk 删除、裁剪或启动清理逻辑。
- [x] 添加设置默认值、恢复值、运行时开关门控和任意 `saveDays` 不删除历史的测试。

完成条件：用户可见的开关只控制新的 Desktop 自动保存，且没有引入任何自动删除路径。

## Phase 4 - Visible Immutable Snapshot

- [x] 定义不含 controller、runtime lock、D3D/GPU 资源和预测缓存的 Desktop 自动保存快照值对象。
- [x] 在 DrawingController 命令安全点实现 visible snapshot builder，依据 runtime history 只选择当前可见 stroke/shape，保留顺序、样式和稳定身份。
- [x] 复用现有 UInk export snapshot 与转换 API，避免自动保存另建一套 Draw3 -> UInk 映射。
- [x] 明确空画布判定与快照捕获使用同一可见真值，已撤销到空状态不得生成请求。
- [x] 记录捕获耗时、元素数量与估算字节数，供大画布卡顿基准判断；本任务不引入未经证据支持的增量存储。
- [x] 添加 Undo、Redo 分支、擦除、shape、空画布及快照后继续绘制不改变已捕获内容的纯 CPU 测试。

完成条件：后台拿到的是完整自有、不可变、只含当前可见 Desktop 内容的快照。

回滚点：若深复制在实测规模下超过现有交互预算，先保留一致性测试并量化结果，再单独评审不可变共享方案；不得把运行时引用交给 worker。

## Phase 5 - AutoSave Storage And Daily Index

- [x] 新增独立 Desktop AutoSave 存储模块，根目录严格使用 `GetCurrentExeDirectory()/Inkeys/AutoSave/desktop`。
- [x] 在请求创建时固定带时区 `createdAt`、本地日期、`saveRequestId`、`fileGuid`、`sessionId` 与会话序号。
- [x] 实现 `YYYY-MM-DD/HHmmssfff_<saveRequestId-short>.uink` 候选路径及 create-new 冲突处理；不同请求不覆盖，同一请求按 `fileGuid` 幂等验证。
- [x] 使用结构化 JSON API 实现 version 1 每日索引，包含 `dailySequence`、请求/会话身份、触发类型、文件身份、相对路径和精确时间。
- [x] 为每个规范化根路径与日期使用跨进程 named mutex；持锁后重读最新索引、分配序号并按 `saveRequestId` 幂等提交。
- [x] 按设计顺序实现 UInk 先 durable、索引后自校验并原子替换，保留最后已知有效索引备份。
- [x] 实现保守故障语义：保留索引失败后的孤儿 UInk，主/备索引均损坏时停止该日期索引写入并记录日志，不删除或猜测重建未知文件。
- [x] 添加可注入文件系统/故障点，覆盖同毫秒请求、候选碰撞、内部重试、跨午夜、模拟两个进程的 writer 竞争、文件失败、索引失败和主/备索引恢复。

完成条件：文件与每日索引不会因线程、多实例、重试或时间碰撞互相覆盖，任何有效索引只引用 durable UInk。

回滚点：新路径没有旧数据迁移要求；出现事务缺陷时可由 `saveSetting.enable` 关闭入口，同时保留所有已写文件，不执行清理。

## Phase 6 - Owned Worker And Trigger Wiring

- [x] 由 Draw3 Host 建立单一、有所有权、可 join 的串行 autosave worker 和有序请求队列。
- [x] `Submit` 对相同 `saveRequestId` 返回已有状态，对不同请求逐一接受且不合并、不覆盖、不静默丢弃。
- [x] 首版不设置会拒绝已接受请求的固定队列上限；记录队列深度、快照总字节和终态，内存分配失败必须显式失败。
- [x] 在 Desktop Clear 的破坏性操作之前按开关、场景、资格和非空条件捕获并提交，随后立即继续 Clear，不等待编码或磁盘。
- [x] 在 Undo/Redo 和未来跨 Clear 撤销路径中保持零自动保存调用。
- [x] 验证连续多次 Clear 的请求顺序、每段内容和索引项一一对应。

完成条件：正常交互不执行磁盘 I/O，所有已接受的人工边界请求都可观测地到达唯一终态。

## Phase 7 - Shutdown Handshake

- [x] 重排 Host 正常 Stop：先停止新命令/contact producer，再让仍存活的 DrawingController 在安全点执行最终 Desktop Exit 检查与快照。
- [x] 等待最终捕获确认后关闭队列生产端，拒绝所有晚到请求，并 drain 既有 Clear/Exit 请求。
- [x] 不设置主动放弃写入的硬超时；等待每个请求 `Committed` 或 `Failed` 后再退出绘制线程并销毁 controller/worker。
- [x] 明确文件/索引错误、目录不可写和磁盘空间不足等失败只写诊断日志，不弹阻塞窗口，正常关闭继续完成。
- [x] 添加可控慢写与失败注入测试，证明 controller 生命周期、生产端关闭、请求顺序、join 和失败退出不存在死锁或 use-after-free。

完成条件：正常退出不会在 controller 销毁后读取画布，也不会遗留 detached 写线程或放弃已接受请求。

回滚点：Shutdown 顺序涉及 Host 生命周期；若测试出现锁循环或无法证明所有权，先撤下触发接线并保留独立 worker 测试，不以 detached thread 临时替代。

## Phase 8 - Quality Gate

- [x] 使用 `rg` 静态核对全部 workspace switch、自动保存触发点、Undo/Redo 零调用、`saveDays` 零删除和项目文件条目。
- [x] 运行 `git diff --check`，并核对所有修改文件保持原编码与换行；C++ 关键流程使用简短中文注释。
- [x] 使用 ARM64 原生 `MSBuild.exe` 构建完整 `InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`，超时不少于 5 分钟，不单独构建 `Inkeys.vcxproj`。
- [x] 运行 `ARM64\Debug\inkStrokeModelerTestTests.exe` 与 `ARM64\Debug\InkeysHeadlessTests.exe` 等全部相关无窗口测试。
- [x] 对照 `prd.md` 逐项回填场景门控、触发、设置、可见快照、唯一性、幂等、索引事务、跨午夜、退出 drain 和失败路径验收结果。
- [x] 使用 `trellis-check` 完成规范、构建、测试、数据流与重复实现审查；发现问题后修复并重复相关门禁。
- [x] 不启动 GUI、不操控桌面、不自动删除任何历史文件、不创建 commit。

完成条件：完整 ARM64 solution 构建、相关 headless tests、静态检查和 Trellis 质量门禁全部通过；任何受限验证明确记录。

## Planned File Ownership

实际新增文件名在 Phase 1 后固定，预期最小影响范围：

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h` 与 `.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cppm` 与 `.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cppm` 与 `.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cppm` 与 `.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm` 与 `.cpp`，仅在场景发布或关闭接线需要时修改
- 新增的 `Draw3.AutoSave`、`Draw3.AutoSaveIndex` 等职责单一模块及实现文件
- `Inkeys/Inkeys/UI/Setting/Setting.cpp` 与现有设置持久化调用点
- `Inkeys/IdtMain.cpp` 及 PPT 场景发布调用点，仅做三态与设置接线所需最小修改
- `Inkeys/Inkeys.vcxproj` 与 `.filters`
- `inkStrokeModelerTest/draw3/uink_*`，仅在共享生产接入确实需要最小调整时修改
- `inkStrokeModelerTestTests/` 与 `InkeysHeadlessTests/` 下聚焦本功能的无窗口测试

不计划修改 UInk v10 线格式，不计划新增 importer，不计划实现 Whiteboard/PPT 自动保存或历史清理。

## Implementation Result

- Phase 1-7 已完成：共享 UInk 接入、三态与 `PptTouched`、设置门控、可见 CPU 快照、每日事务索引、owned worker、Clear/Exit 和关闭排空均已落地。
- Phase 8 已完成：两个 ARM64 Debug solution 全量重建、UInk/Draw3 tests、`--no-window` tests、静态约束、编码换行与 Trellis 审查均通过。
- 遵循本轮不创建 commit 的要求，`trellis-finish-work` 未归档任务或写入会自动提交的 session journal；任务保留 active 状态。
- 下一任务保持为 Desktop/PPT 切换时的画布转换、归属和独立化；本任务没有提前实现该内容。
