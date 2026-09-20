# 桌面批注 UInk 自动保存与历史索引

## Status

- 实现与质量门禁已于 2026-09-01 完成，全部验收项均有代码或自动化证据。
- 遵循本轮“不创建 commit”的要求，任务保持 active 且不归档；最终结果见 `verification.md`。

## Goal

仅为普通桌面批注建立非阻塞的 `.uink` 自动保存：Clear 前和程序退出时把非空、可保存的桌面画布作为独立文件持久化，按日期建立可靠索引，并为未来历史画布界面及逐区间恢复保留稳定数据契约。

## Confirmed Context

- Draw2 的日期目录和历史索引可作为产品意图参考，但 detached thread、全局 JSON 并发覆写、时间戳唯一身份及未知文件清扫不能沿用。
- 仓库已有 UInk v10 模型、编解码、完整保存、自校验和来源 revision 能力；本任务接入产品场景，不重新设计 UInk 线格式。
- 当前 Draw3 的 `Presentation` 同时承担 Desktop 与 PPT，完整文档独立化将在下一任务处理；本任务先建立 Desktop、Whiteboard、PPT 三态身份和临时保存资格保护。
- 详细代码证据与风险见 `research/current-state-and-risks.md`，技术方案见 `design.md`。

## Requirements

### Scenario eligibility

- 场景身份拆分为 Desktop、Whiteboard、PPT 三态，自动保存入口只接受 Desktop。
- Whiteboard 和 PPT 状态不创建本功能的 `.uink` 或索引项。
- Desktop/PPT 文档尚未独立期间，共享主画布一旦进入 PPT，当前区间从 `Eligible` 变为 `PptTouched`。
- `PptTouched` 区间返回 Desktop 后，Clear 前和退出时均跳过保存；只有 Desktop Clear 完成并产生新空区间后才恢复 `Eligible`。

### Save triggers

- `saveSetting.enable` 开启、当前场景为 Desktop、资格为 `Eligible` 且画布非空时，用户 Clear 前自动保存当前可见内容。
- 相同条件下，程序关闭时自动保存当前可见内容。
- Undo/Redo 不触发自动保存，包括未来可能出现的跨 Clear 撤销。
- 每次 Clear 或退出触发都创建一个不可变的新历史文件；本任务不更新或覆盖已提交历史文件。

### Settings and retention

- 恢复现有保存设置入口，`saveSetting.enable` 作为 Desktop UInk 自动保存总开关。
- 开关关闭时不捕获快照、不入队，也不创建自动保存目录或索引；已经开始执行的请求继续收敛到终态。
- `saveDays` 不作用于新 UInk，首版不执行过期删除、容量淘汰、启动清理或未知文件删除。
- 未来的撤回范围设置只限制读取范围，不物理删除超出范围的历史。

### Storage and identity

- 程序根目录由现有 `GetCurrentExeDirectory()` 语义确定，自动保存根为 `<程序根目录>/Inkeys/AutoSave`。
- Desktop 布局固定为 `desktop/YYYY-MM-DD/`；日期取保存请求创建时的系统本地日期，后台提交跨午夜不得改变已绑定路径。
- 文件名为 `HHmmssfff_<saveRequestId-short>.uink`；`saveRequestId` 提供唯一身份和重试幂等，时间部分只用于可读性。
- 同一请求重试不得产生重复文件或索引项，不同请求不得因同毫秒、执行顺序或多进程竞争互相覆盖。

### Daily index

- 每个日期目录包含一个版本化 `index.json`，不建立持续膨胀的全局历史明细索引。
- 索引根包含 `schemaVersion`、`scenario` 与日期；每个条目至少包含 `dailySequence`、`saveRequestId`、`sessionId`、`sequenceInSession`、`trigger`、`fileGuid`、相对路径和带时区的 `createdAt`。
- 每日条目具有确定顺序；未来可从最新日期开始倒序读取，并按上一段、当前 `sessionId` 或全部日期历史限制范围。
- `.uink` 必须先 durable commit，索引随后自校验并原子发布；有效索引不得指向未完成或不存在的文件。
- 文件已提交而索引失败时保留孤儿文件并记录日志，不自动删除；旧索引继续有效。

### Threading and shutdown

- 绘制线程只在安全点生成不含 GPU/controller 引用的不可变可见快照，不执行 UInk 编码、磁盘写入或索引提交。
- 一个有所有权的串行后台 worker 按请求处理 UInk 和每日索引；不得使用 detached thread。
- 不同保存请求不能合并；相同 `saveRequestId` 必须幂等去重。
- 关闭时先停止新产品命令和输入生产者，在 controller 仍存活时捕获最终合格快照，再关闭生产端并 drain 全部保存请求。
- 退出 drain 不设置会主动放弃写入的硬超时；等待文件与索引到达成功或明确失败的终态后再销毁文档和 worker。
- 明确写入失败时记录可诊断日志并正常退出，首版不显示阻塞式错误窗口，也不得伪报保存成功。

### Future compatibility

- 每个文件必须是可独立恢复的完整 Desktop 区间，不依赖相邻文件才能解码。
- 索引元数据必须支持未来按日期一次回载一个区间，以及显示当前恢复到的保存时间。
- 未来只有在文件与索引都 durable 后，内存管理器才可释放对应旧画布；本任务不执行内存淘汰或反向加载。

## Acceptance Criteria

- [x] Bridge/Host 可明确表达 Desktop、Whiteboard、PPT，只有 Desktop `Eligible` 区间能够触发自动保存。
- [x] 共享画布进入 PPT 后返回 Desktop 仍跳过保存，Desktop Clear 后的新空区间恢复资格。
- [x] Clear 前与退出时保存符合条件的非空画布；Undo/Redo、空画布、关闭开关、Whiteboard、PPT 和 `PptTouched` 均不保存。
- [x] UI/绘制线程不执行文件编码、磁盘写入或索引提交，连续 Clear 不发生写入竞争或丢失已接受请求。
- [x] 文件只写入 `<程序根目录>/Inkeys/AutoSave/desktop/YYYY-MM-DD/`，同毫秒请求和内部重试不会产生覆盖或重复历史。
- [x] 每日 `index.json` 可独立读取，包含未来按日期、会话和区间恢复所需的稳定元数据。
- [x] 文件先于索引 durable 发布；异常后有效索引不指向不完整文件，失败恢复不删除未知 UInk。
- [x] `saveSetting.enable` 可见且生效；`saveDays` 的任意值都不会删除或裁剪新 UInk 历史。
- [x] 退出会等待全部请求到达终态且不以硬超时放弃写入；失败记录日志后退出，不弹阻塞窗口。
- [x] 自动化测试覆盖三态门控、`PptTouched`、触发条件、可见快照、唯一身份、幂等、索引事务、跨午夜、退出 drain 和失败路径。

## Out of Scope

- Whiteboard 自动保存或恢复。
- PPT 自动保存、演示文稿信息索引或墨迹恢复。
- Desktop、Whiteboard、PPT 三份画布的最终独立化及现有串墨修复。
- 历史画布 UI、恢复时间提示 UI、UInk 到 Draw3 的反向导入。
- 跨 Clear Undo、跨文件 Undo、跨重启恢复、旧画布内存淘汰和撤回范围设置。
- UInk 自动过期、容量配额、手动清理和其他物理保留策略。
- 将多个区间打包为一个多页 `.uink`，或重新设计 UInk 线格式。

## Required Follow-up

- 本任务完成后的下一项工作是 Desktop/PPT 切换时的画布转换、归属和独立化。
- 后续以独立文档真值替代临时 `PptTouched` 门控；已经生成的 Desktop 文件与索引 schema 保持兼容，不进行迁移性重写。

## Approval Gate

- 用户已于 2026-09-01 明确批准开始实现。
- 当前没有未决的用户侧产品问题；实施和验证证据记录在 `verification.md`。
