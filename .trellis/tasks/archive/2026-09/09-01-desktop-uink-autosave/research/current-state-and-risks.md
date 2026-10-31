# 桌面 UInk 自动保存现状调查

> 调查日期：2026-09-01；状态：仅调查，未修改产品代码。

## 0. 已确认的产品决策

- 自动保存只在 Clear 前和程序退出时触发；Undo/Redo，包括跨 Clear 的撤销，不触发自动保存。
- 跨 Clear 撤销表示用户放弃离开的内容，本任务不为其建立文件，也不实现跨 Clear 历史导航或恢复。
- 保存根目录固定为 `<程序根目录>/Inkeys/AutoSave`。`Inkeys/IdtMain.cpp:138` 表明现有 `globalPath` 来自 `GetCurrentExeDirectory()`，因此新目录与旧 `<程序根目录>/ScreenShot` 分离。
- 场景身份拆为 Desktop、Whiteboard、PPT 三态，首版自动保存只服务普通 Desktop。
- 三态文档最终独立、旧画布内存淘汰、按日期逐区间回载、撤回范围设置与恢复时间提示均是未来能力；当前文件和索引需保留兼容入口，但本任务不实现这些功能。
- 已确认混合画布采用保守策略：共享主画布进入 PPT 后标记 `PptTouched`，回到 Desktop 仍跳过保存；在 Desktop 完成下一次 Clear、开始全新空区间后才恢复 `Eligible`。
- 本任务完成后，下一项工作明确为 Desktop/PPT 切换时的画布转换与独立归属。
- 已确认恢复并复用 `saveSetting.enable` 作为 Desktop 自动保存开关；`saveDays` 不作用于新 UInk，首版不自动删除或清扫 `AutoSave` 内容。
- 已确认采用 `<程序根目录>/Inkeys/AutoSave/desktop/YYYY-MM-DD/` 与每日版本化 `index.json`，不使用无界全局历史明细索引。
- 已确认正常退出先停止新输入，在 controller 存活时捕获最终快照，再关闭生产端并排空全部请求；不设置会放弃写入的硬超时，明确失败只记诊断日志、不弹阻塞窗口，随后正常退出。

## 1. 结论摘要

1. Draw2 的日期目录、历史索引和“清空/退出时留档”可以作为产品意图参考，但其 detached thread、全局可变 JSON 和时间戳唯一命名不能直接沿用。
2. 当前 Draw3 只有 `Presentation` 与 `Whiteboard` 两种 workspace；桌面批注和 PPT 共用 `Presentation`，且 PPT 页状态不能被普通 workspace 发布可靠清除。仅在保存触发点判断“当前是否 PPT”无法保证桌面文件不混入 PPT 墨迹。
3. 当前 Clear 会删除文档 stroke、替换运行时历史并永久截断撤销/重做。用户已取消跨 Clear 撤销时的自动保存，因此该缺口不再需要由本任务补齐。
4. UInk 基础能力已有完整保存、来源 revision 检查、临时文件自校验与替换，但尚未接入产品工程。现有 Draw3 捕获还会复制文档中的全部 stroke，未应用运行时撤销可见性。
5. 后台保存应使用一个有所有权、可 drain 的串行 worker。绘制线程只在安全点生成不可变可见快照；文件编码、持久化与索引提交在 worker 中执行。不能继续使用 detached thread。
6. 简化后每次 Clear/退出保存都创建不可变的新历史文件；仍需稳定 `saveRequestId` 保证单次请求重试幂等，并防止同毫秒文件名冲突。

## 2. Draw2 可参考行为

### 2.1 已有行为

- `Inkeys/IdtHistoricalDrawpad.cpp:160` 的 `SaveScreenShot` 受 `saveSetting.enable` 控制。
- `Inkeys/IdtHistoricalDrawpad.cpp:164` 至 `:170` 使用日期目录和毫秒时间戳 PNG 文件名。
- `Inkeys/IdtHistoricalDrawpad.cpp:187` 至 `:229` 把日期、时间和文件路径写入全局 `ScreenShot/attribute_directory.json`。
- `Inkeys/IdtHistoricalDrawpad.cpp:64` 的加载逻辑按 `saveDays` 清理过期记录，并删除索引之外的文件。
- `Inkeys/IdtDrawpad.cpp:1549` 至 `:1553` 的退出/清空相关路径会同步或通过 detached thread 调用保存；其他历史淘汰路径也使用相同保存入口。
- Draw2 只保留最多 10 个 `RecallImage` 内存快照；`Inkeys/IdtHistoricalDrawpad.cpp:346` 的 `IdtRecovery` 会从索引中的 PNG 读取更早历史。因此旧设计同时承担自动留档与内存历史的磁盘后备。

### 2.2 不能沿用的实现

- 多个 detached thread 会同时修改 `record_value` 并整体覆写同一个 JSON，没有互斥、提交顺序或退出 drain。
- 毫秒时间戳不是逻辑身份；同毫秒仍可能冲突，也不能保证同一内部请求重试时保持幂等。
- PNG 写入和索引写入不是一个可恢复事务，崩溃后可能产生孤儿文件或失效索引。
- 启动时删除所有“未知文件”会妨碍未来格式迁移、损坏恢复和人工找回，不应复制到新 UInk 历史目录。
- `record_pointer_add` 只更新浏览指针，不提供稳定历史项身份或版本控制。

## 3. Draw3 场景隔离缺口

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h:28` 的 `Workspace` 只有 `Presentation` 与 `Whiteboard`；默认值也是 `Presentation`。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:1447` 以 `Presentation` 初始化主文档，白板使用第二份文档。桌面批注与 PPT 因此落在同一主文档域。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp:98` 的 workspace 发布试图设置 `hasPage = false`，但 `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp:12` 会保留此前已发布的 PPT page。
- UInk 导出器在 `inkStrokeModelerTest/draw3/uink_draw3_export.cpp:307` 将当前导出 workspace 写为类型 `0`（Screen Annotation）。如果主文档混有 PPT 内容，文件元数据和实际内容会矛盾。

### 影响

- 已确认让 Bridge 场景枚举与 UInk workspace 类型对齐为 `Desktop/ScreenAnnotation`、`Whiteboard`、`Presentation/PPT` 三态。
- 本任务只增加 Desktop 自动保存入口；修复跨状态串墨和建立最终独立文档域明确延期。
- 在独立文档完成前，单纯检查触发时 workspace 不能证明共享主文档内没有 PPT 墨迹。已确认首版用临时资格状态保护：`Eligible -> PptTouched` 后不可因返回 Desktop 自动恢复，只能由 Desktop Clear 创建的新空区间恢复。

## 4. Clear、Undo、Redo 与逻辑画布段

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:4108` 的 `clearCurrentPage` 会调用 `ClearStrokes()`，再用全新的 `CanvasPageRuntimeState` 替换当前历史。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:4219` 明确把 Clear 作为永久截断当前撤销/重做分支的操作。
- Undo/Redo 当前只在一个 `CanvasRuntimeHistory` 内修改可见性；新的内容提交会丢弃 redo 分支。

### 对本任务的影响

- Clear 在现有命令安全点先捕获当前非空桌面画布，再沿用当前破坏性清除。
- Undo/Redo 不调用自动保存服务；本任务不增加 Clear 边界时间线，也不维护可返回并覆盖的历史段身份。
- 每个 Clear/退出事件产生独立 `saveRequestId` 和新 `.uink`；文件一旦提交即视为不可变历史。
- 如果未来另一个任务增加跨 Clear 恢复，再单独定义恢复后重新保存是新历史还是更新旧历史，避免为尚不存在的行为预置复杂状态。

## 5. UInk 能力与接入缺口

### 5.1 已有能力

- `inkStrokeModelerTest/draw3/uink_file.cppm:36` 区分保存既有逻辑文件与另存为新逻辑文件。
- `inkStrokeModelerTest/draw3/uink_file.cppm:49` 定义 `SourceChanged`、自校验失败和部分提交恢复等明确状态。
- UInk 文件层已实现完整编码、同目录临时文件、自校验、来源 revision 校验和替换流程，适合复用为后台 worker 的持久化原语。
- Undo、擦除和分支编辑都可能删除或改变可见内容，因此自动保存必须走完整保存；append 不能作为通用更新策略。

### 5.2 仍需解决

- UInk 源目前只加入 `inkStrokeModelerTest` 与 `inkStrokeModelerTestTests`，`Inkeys/Inkeys.vcxproj` 尚未编译这些模块。
- `inkStrokeModelerTest/draw3/uink_draw3_export.cpp:262` 至 `:270` 复制 `sourceCanvas.Strokes()` 的全部 stroke；撤销可见性在 Draw3 runtime history sidecar 中，自动保存必须从当前可见历史构造快照。
- 每次触发都保存为新逻辑文件，可直接使用 `SaveAsNewLogicalFile`；本任务不需要实现自动覆盖既有逻辑文件。
- 不可变快照必须在绘制线程安全点创建，且不得把 DrawingController、runtime lock、D3D 资源或预测缓存交给 worker。
- 当前仅有 Draw3 -> UInk 的单向导出，没有 UInk -> Draw3 投影。由于历史恢复已明确排除，这不再是本任务阻塞项。

## 6. 并发与退出生命周期

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:647` 的 Stop 先停止 bridge/RTS，再 `RequestExit()` 并 join 绘制线程。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:4758` 在看到退出标志后直接跳出循环；随后 controller 在绘制线程销毁。
- 因此关闭保存不能在 controller 已销毁后临时读取文档，也不能把保存任务丢给无人 join 的线程。

推荐结构：

```text
绘制线程安全点
  -> 生成 visible immutable snapshot + saveRequestId
  -> 提交到单一有所有权的保存队列
  -> 后台 worker：UInk 转换/编码/文件提交/索引提交
  -> Host Stop：禁止新任务，保存当前非空桌面段，drain worker，随后释放文档
```

队列规则：

- 每个 `saveRequestId` 最多提交一次；重复入队或内部重试必须幂等。
- 不同保存请求必须保持独立任务；不能为了合并队列而丢掉连续两次 Clear 之间的画布。
- 串行 worker 已足以避免文件与索引竞争，无需线程池。
- 正常退出应停止新输入，在 controller 存活时捕获最终合格快照，然后关闭生产端并等待全部请求进入成功或明确失败终态；不设置会主动放弃写入的硬超时。
- 写入明确失败时只记录可诊断日志，不弹阻塞窗口，随后正常退出；强制结束进程和系统断电不属于可 drain 的正常退出。
- 单个串行 worker 只解决进程内竞争；多个应用实例更新同一日期索引时仍需跨进程互斥、持锁后重读和原子替换。
- 将磁盘 I/O 移出绘制线程不代表完全无卡顿；超大画布的深复制仍需基准测试，必要时再引入增量或共享不可变存储。

## 7. 文件命名与索引建议

已确认的首版布局：

```text
<程序根目录>/Inkeys/AutoSave/
  desktop/
    YYYY-MM-DD/
      index.json
      HHmmssfff_<saveRequestId-short>.uink
```

- 日期与时间服务于浏览和可读性，`saveRequestId` 才是防冲突身份。
- 第一次保存用“目标必须不存在”的创建语义；碰撞则生成新候选，不能静默覆盖。
- 同一请求重试始终复用已绑定路径，不同请求始终创建新文件。
- 每日一个版本化索引，便于未来历史 UI 按日期加载，避免每次改写无界的全局 JSON。
- 日期键使用请求创建时的系统本地日期，精确时间保存时区信息；worker 跨午夜提交不会移动已绑定路径。
- `.uink` 先 durable commit，再原子发布索引。该顺序最多留下可在启动时恢复的孤儿文件，不会留下指向未完成文件的有效索引。
- 索引至少包含 `schemaVersion`、`saveRequestId`、`scenario=desktop`、相对路径、`fileGuid`、`createdAt` 和可选诊断状态。
- 为未来反向逐区间加载，索引还应记录 `sessionId`、会话内单调顺序和 `trigger=clear|exit`。`createdAt` 负责按日期浏览，顺序字段避免系统时间回拨破坏同一会话内的先后关系。
- “仅上一段/仅当前启动/全部历史”应当是未来读取范围策略，而不是在当前索引中丢失历史；是否物理删除文件属于独立的保留期策略。

## 8. 设置、保留期与兼容性

- 默认配置初始化在 `Inkeys/IdtMain.cpp:948` 设置 `saveSetting.enable = true`、`saveDays = 2`（旧枚举含义为 5 天）。
- 设置 UI 在 `Inkeys/Inkeys/UI/Setting/Setting.cpp:1991` 被 `#if 0` 隐藏，原因是 Draw3 文件保存与超级恢复尚未准备好。
- 已确认恢复保存开关入口并让 `saveSetting.enable` 控制新功能；旧 `saveDays` 值继续保留，但不执行 UInk 保留期删除。
- 新 UInk 历史固定写入 `<程序根目录>/Inkeys/AutoSave`，与旧 `ScreenShot` PNG 分目录保存；首版不迁移、不删除旧 PNG，也不清扫未知 UInk 文件。

## 9. 已收敛边界与后续风险

- 所有空画布均不创建文件或索引记录；只保存符合场景、资格和开关条件的非空画布。
- 正常退出不以硬超时放弃已接受请求；明确失败只记录诊断、不弹阻塞窗口，强制终止和断电不承诺最后一次请求完成。
- 首版不执行保留期清理；索引/文件不一致只做保守恢复或隔离，不以“清理”为名删除未知内容。
- 历史文件首版只保证可被未来 UI 发现；手动打开、恢复和磁盘后备 Undo 已排除。
- 未来按日期逐区间 Undo 的 UInk importer、失败回退、内存淘汰和提示 UI 另立任务；当前索引只提供所需排序与会话元数据。
- 本任务之后优先处理 Desktop/PPT 切换画布转换；该任务完成后复核并删除 `PptTouched` 临时门控。

## 10. 预计测试面

- 场景门控：Desktop 的 `Eligible` 区间可保存，Whiteboard/PPT 状态不触发；`PptTouched` 区间返回 Desktop 后仍跳过。
- 混合资格：进入 PPT 后返回 Desktop 仍跳过，Desktop Clear 后的新空区间恢复保存。
- 设置与保留：开关关闭不产生请求；`saveDays` 的任意值都不会删除 UInk 或索引。
- 索引兼容：跨日期和同会话顺序稳定，可按 `sessionId` 过滤且可区分 Clear/退出来源。
- 触发边界：Clear/退出保存非空画布，Undo/Redo 与空画布不保存。
- 身份与排序：单次请求幂等、不同请求不冲突、连续 Clear 不丢记录。
- UInk 可见内容：已撤销内容不导出，擦除/分支编辑使用完整保存。
- 文件与索引：同名碰撞、文件成功/索引失败、崩溃遗留恢复、损坏索引。
- 生命周期：关闭时最终快照、生产端关闭、无丢弃超时的队列 drain、明确写入失败与日志路径。
