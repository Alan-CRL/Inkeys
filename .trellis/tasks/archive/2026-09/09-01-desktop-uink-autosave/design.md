# 桌面批注 UInk 自动保存设计

> 状态：已按用户批准实现；最终实现与验证结果见 `verification.md`。

## 1. 设计目标

- 只持久化普通 Desktop 批注，不持久化 Whiteboard 或 PPT 内容。
- 在 Clear 前和正常退出时，把符合资格的非空可见画布保存为独立、不可变的 `.uink`。
- Undo/Redo 不触发自动保存，本任务不实现跨 Clear 历史导航或恢复。
- 绘制线程只捕获不可变 CPU 快照；UInk 编码、文件写入和索引提交全部移到后台。
- 不同请求不覆盖、不合并、不静默丢弃；同一请求重试保持幂等。
- 保存顺序、跨进程冲突、退出收尾和文件/索引故障都具有可测试的确定语义。

## 2. 三态场景边界

当前 `Bridge::Workspace::Presentation` 同时表示桌面与 PPT。本任务先扩展为：

```cpp
enum class Workspace : std::uint8_t
{
    Desktop,
    Whiteboard,
    Presentation,
};
```

三态身份与 UInk 的 Screen Annotation、Whiteboard、Presentation/PPT 语义对应。现有选择辅助等共享能力继续覆盖 Desktop 与 Presentation，自动保存入口只接受 Desktop，PPT 页面绑定继续属于 Presentation。

本任务不完成三份文档的最终独立化，也不修复现有跨状态串墨。Desktop 与 PPT 仍共享主画布期间，不能把 `activeWorkspace == Desktop` 直接等同于内容全部来自 Desktop，需采用临时保存资格状态机：

```text
Eligible
  -- enter PPT --> PptTouched

PptTouched
  -- return Desktop --> PptTouched
  -- Clear/Exit save check --> skip
  -- Clear completes in Desktop --> Eligible (new empty interval)
```

- `PptTouched` 由明确的 PPT 场景切换事件设置，不能依赖当前会保留旧 page 的 `hasPage` 推断。
- Whiteboard 使用独立文档；进入和退出 Whiteboard 不改变主画布的 Desktop/PPT 保存资格。
- Desktop Clear 即使因空画布未保存，也会完成新区间边界并恢复 `Eligible`。
- 资格状态只决定能否提交自动保存请求，不改变现有 Clear、Undo、Redo 或画布内容。

## 3. 保存请求与身份

把自动保存状态抽成不含 UI/GPU 引用的值对象：

```text
DesktopAutoSaveRequest
  saveRequestId
  sessionId
  sequenceInSession
  trigger: Clear / Exit
  fileGuid
  createdAt
  localDate
  proposedRelativePath
  immutableSnapshot
  state: Pending / Writing / Committed / Failed
```

约束：

- `saveRequestId` 在接受 Clear/退出请求时生成，不能由数组下标、日期或现有文件数量推导。
- `sessionId` 在产品启动时生成；`sequenceInSession` 对已接受请求严格单调递增。
- `createdAt` 与 `localDate` 在请求创建时固定，后台提交跨午夜不得改变日期目录。
- `fileGuid` 在请求创建时生成，用于验证已存在的绑定文件是否确属同一逻辑文件。
- 快照入队后不可变，后续画布变化属于其他请求。
- 不同请求始终创建不同逻辑文件；同一请求的内部重试复用已经解析的目标路径和文件身份。

## 4. 设置与保留边界

- 恢复现有保存设置入口，`saveSetting.enable` 是捕获快照与提交请求前的总门控。
- 开关关闭时不捕获、不入队，也不创建新目录或索引；此前已经接受的请求继续到成功或明确失败的终态。
- `saveDays` 不参与新 UInk 路径或生命周期；首版没有定时清理、启动清理、容量淘汰或未知文件删除。
- 未来撤回范围是 reader/navigation policy，只限制读取范围，不物理删除历史。

## 5. 触发与关闭状态机

### Clear

1. 在现有命令安全点等待活动 contact 收敛。
2. 若开关开启、场景为 Desktop、资格为 `Eligible` 且画布非空，创建请求并捕获当前可见快照；否则不创建任何文件或索引记录。
3. 请求一经队列接受即可沿用当前破坏性 Clear，不等待磁盘 I/O。
4. Desktop Clear 完成后把新空区间资格设为 `Eligible`。

### Undo/Redo

不调用自动保存服务。当前 Draw3 的段内 Undo/Redo 保持不变；未来跨 Clear 撤销也不因跨越边界而保存被放弃的内容。

### 正常退出

1. Host 停止接收新的产品命令和 contact producer，但保持绘制线程与 controller 可用。
2. 通过绘制线程安全点执行最终检查，只为开关开启、Desktop、`Eligible`、非空的画布捕获一个 Exit 请求。
3. 等待该捕获动作完成，随后关闭保存队列生产端。
4. drain 所有已经接受的 Clear/Exit 请求，直到每个请求进入 `Committed` 或 `Failed`。
5. 再请求绘制线程退出，销毁 controller、文档和 worker。

正常退出不设置会主动放弃写入的硬超时。磁盘操作明确失败时记录诊断并把请求置为 `Failed`，不弹阻塞式窗口，也不伪报成功；强制结束进程、系统断电等非协作关闭不承诺完成最后一次请求，只依赖文件与索引的原子性保护已有历史。

## 6. 可见不可变快照

快照只包含 UInk 可持久化的 CPU 数据：workspace/page/canvas 身份、viewport、当前有效可见的 stroke/shape 与样式。它不能携带 D3D 资源、GPU history cache、预测点、controller 指针或运行时锁。

当前 `CaptureDraw3UInkExportSnapshot` 从 `InkCanvas::Strokes()` 全量复制，未应用 DrawingController 的 Undo sidecar。自动保存需增加显式 visible snapshot builder，由 DrawingController 在安全点按 runtime history 选出当前有效项，再交给现有 `ExportDraw3SnapshotToUInk`。该 builder 应保持可见元素顺序、样式与稳定身份，并通过纯数据测试证明已撤销分支不会导出。

快照捕获本身仍会占用绘制线程时间。首版优先保证所有权和一致性，并记录捕获耗时、stroke 数量和快照字节数；若大画布基准表明深复制不可接受，再单独设计共享不可变存储或增量快照，不能把 controller 生命周期泄漏给 worker。

## 7. UInk 生产工程接入

UInk v10 的模型、编码、完整保存和自校验目前只编入测试工程。生产接入应把现有 UInk 模块作为唯一实现源加入 `Inkeys.vcxproj`，或迁到产品与测试共同引用的共享目录；不得复制出第二套编码器。

接入时需要验证：

- 模块导入与产品现有 `draw3.ink_document` 类型一致，没有测试宿主依赖。
- 自动保存只调用完整保存/`SaveAsNewLogicalFile` 等价路径，不使用 append。
- `fileGuid`、自校验、同目录临时文件与 durable 发布能力由现有文件层复用。
- UInk -> Draw3 importer 仍不在本任务范围内。

## 8. 保存调度器

由 Host 拥有一个可显式关闭并 join 的串行 worker：

```text
Submit(request)
  -> deduplicate saveRequestId
  -> ordered queue
  -> commit full UInk file
  -> commit daily index
  -> publish terminal status
```

- 一个进程内只有一个 worker 写自动保存历史，避免同一日期索引的线程竞争；线程池没有收益。
- 不同 `saveRequestId` 不能合并或覆盖。相同 ID 的重复提交只返回既有状态。
- 首版不设置会拒绝或丢弃已接受业务请求的固定队列上限。Clear 是低频人工事件；队列深度和累计快照字节数必须可观测，异常内存分配失败进入显式 `Failed`。
- worker 严格按本进程接受顺序处理请求；请求状态只允许单向进入一个终态。
- worker 只接收值对象，不反向访问 controller、runtime 或 GPU。

单 worker 只能解决进程内竞争。每日索引提交还需使用由规范化 AutoSave 根路径和日期哈希派生的跨进程 named mutex；持锁后重新读取最新有效索引，再分配当日序号和提交更新，防止两个应用实例互相覆盖索引。

## 9. 文件布局、唯一性与索引

固定布局：

```text
<程序根目录>/Inkeys/AutoSave/
  desktop/
    YYYY-MM-DD/
      index.json
      HHmmssfff_<saveRequestId-short>.uink
```

- 根目录沿用 `GetCurrentExeDirectory()` 的程序根语义。
- `YYYY-MM-DD` 与 `HHmmssfff` 来自请求创建时的本地时间；`createdAt` 在索引中保存带 UTC offset 的 ISO 8601 精确时间。
- 文件发布使用 create-new 语义。候选名碰撞时增加确定的安全后缀并记录最终相对路径，绝不覆盖已有历史。
- 同一请求重试若发现已发布文件，必须校验 UInk 与预生成 `fileGuid` 一致后复用；不一致按命名碰撞处理。

每日 `index.json` 使用结构化 JSON API，首版 schema 示例：

```json
{
  "schemaVersion": 1,
  "scenario": "desktop",
  "date": "2026-09-01",
  "entries": [
    {
      "dailySequence": 1,
      "saveRequestId": "...",
      "sessionId": "...",
      "sequenceInSession": 1,
      "trigger": "clear",
      "fileGuid": "...",
      "relativePath": "213045127_ab12cd34.uink",
      "createdAt": "2026-09-01T21:30:45.127+08:00"
    }
  ]
}
```

- `saveRequestId` 是幂等键；同一 ID 已存在时必须验证其 `fileGuid` 与路径一致。
- `dailySequence` 在跨进程互斥区内根据最新有效索引单调分配，数组按该字段升序保存；未来读取可从最新日期和最大序号开始倒序遍历。
- `sessionId + sequenceInSession` 保留同一次启动内的真实顺序，即使系统时钟回拨也不受影响。
- `createdAt` 用于日期浏览和展示，不能单独承担唯一性或会话顺序。

## 10. 文件与索引事务

每个请求按以下顺序提交：

1. 在目标日期目录写 UInk 临时文件并调用现有解码/身份自校验。
2. flush 后以 create-new 方式 durable 发布最终 UInk。
3. 获取每日 named mutex，重新读取并校验最新 `index.json`；主文件无效时只允许回退到最后已知有效备份。
4. 按 `saveRequestId` 做幂等检查，追加包含最终路径的条目。
5. 在同目录写索引临时文件，flush、重新解析并校验 schema/唯一性/引用文件存在性。
6. 使用 Windows 原子替换语义发布 `index.json`，保留最后已知有效备份，随后释放 mutex。

故障规则：

- 文件写入或自校验失败：不发布目标文件，不修改索引，请求进入 `Failed`。
- 文件已发布而索引失败：保留 UInk 孤儿和旧索引，记录请求、路径及错误；同一进程可重试索引提交。
- 主索引损坏：使用有效备份继续；主文件与备份都无效时隔离该日期索引写入并记录错误，不删除任何 UInk。
- 首版不凭文件名猜测 `sessionId` 等缺失元数据，不自动把跨重启孤儿写回索引；未来恢复工具可使用 UInk `fileGuid` 与诊断记录处理。
- 退出时上述失败均形成明确终态，日志后正常退出，不显示阻塞窗口。

## 11. 未来逐区间恢复兼容性

未来目标是在 Clear 的文件与索引均 durable 后释放旧画布 CPU 内存；继续 Undo 时按倒序一次加载一个 `.uink` 区间，并可把范围限制为上一段、当前 `sessionId` 或全部日期历史。

本任务只建立数据契约：

- 每个文件是一个可独立恢复的完整 Desktop 区间，不依赖相邻文件才能解码。
- 每日索引提供日期、当日稳定次序、会话身份、会话内次序、触发来源和精确保存时间。
- 只有文件与索引都 durable 后，未来内存管理器才可释放对应 CPU 状态；当前任务不执行释放。
- 撤回范围只限制遍历，不隐式删除文件。

## 12. 实现阶段与后续衔接

详细执行顺序、测试矩阵和回滚点见 `implement.md`；任务已经批准并进入最终质量门禁。

本任务完成后优先开展 Desktop/PPT 切换时的画布转换、归属和独立化。后续任务应以独立文档真值替代 `PptTouched` 临时门控；已有 Desktop 自动保存文件和索引 schema 保持兼容，不做迁移性重写。
