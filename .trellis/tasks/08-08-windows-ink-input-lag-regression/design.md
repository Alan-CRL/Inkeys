# Windows Ink 输入诊断设计

## Investigation Boundary

本设计只增加观测，不裁决根因，也不改变现有失败策略。`949752a` 是日志中的 `baseCommit`，不是已验证结论。state gate、decoder lifecycle、Move latest-only、cursor wake、120 Hz deadline、coordinator、modeler 和 renderer 语义全部保持不变。

## Session Lifecycle

```text
wmain --rts-trace
  -> BeginInputDebugSession (非 callback，创建唯一日志并写 BEGIN)
  -> ConfigureRtsTrace(true)
  -> RTS callback / DrawingController 只写固定内存
  -> RealTimeStylusInput::Shutdown (回调停止后 flush)
  -> EndInputDebugSession (写 END 并关闭文件)
```

默认文件位于可执行文件旁的 `InputDebugLogs/`。显式输出使用 `--rts-trace-output <path>`。文件在 callback 停止后统一格式化和写入；初始化信息、session header/footer 同样只在非 RTS callback 路径写文件。

## RTS Result Model

`RtsPacketResult` 覆盖：

- `Success`
- `InvalidArguments`
- `StateGateBusy`
- `ContextMissing` / `ContextMismatch`
- `BindingMissing` / `BindingInsertFailed`
- `DecoderMissing` / `DecoderEnsureFailed`
- `GenerationMismatch`
- `PropertyCountMismatch`
- `DecodeFailed`
- `PublishDownFailed` / `PublishMoveFailed` / `PublishUpFailed`

Down 在 writer gate 内记录 decoder 初始命中、ensure、generation、decode 与 PublishDown。Packets 不增加恢复或 COM 查询，只对既有 binding/cache 检查结果分类。没有足够信息判断设备时写 `Unknown`；成功 Down 及 DrawingController contact identity 提供真实设备标签。

## Fixed Storage And Loss Accounting

- 产品 timeline 固定为 8192 条，测试构建缩小为 32 条验证同一 overwrite 算法。
- 单 contact 只保留有限成功 Move，所有失败在 contact 槽有空间时优先保留；正常 Packets timeline 仅保留前四次及每 16 次一次。
- 每种 result 有独立原子总数。失败额外保留首末 callback sequence 和首末 QPC，因此 timeline 覆盖或 `atomic_flag` 争用不会抹掉失败分类范围。
- Success 只增加一个 relaxed counter，不承担失败首末范围的额外原子操作。
- contact reason 使用独立短行输出，长 sequence 即使截断也不能遮蔽 reason 计数。
- lifecycle/error auxiliary 使用独立固定缓冲和明确类别输出，timeline 覆盖后仍可按 callback sequence/QPC 恢复。
- RTS 与 DrawingController 共用非阻塞 `atomic_flag`；争用时丢诊断样本并单独计数，不等待输入线程。

## Drawing Correlation

DrawingController 只在 Run 开始时缓存到 `inputDebugTraceEnabled=true` 后才采集诊断字段。关闭时不复制 Down snapshot、不读取额外 contact identity、不执行 Down 后 runtime `find_if`，每帧也不读取诊断 identity。

启用时记录：

```text
DownDequeue -> DownInitialize
SnapshotRead -> SnapshotFilter
ModelerUpdate / ModelerDeferredUp
ContactRecycle
```

关联键为 `tabletContextId/contactId/contactGeneration`；时序字段为 callback sequence、snapshot sequence、producer snapshot QPC 和 drawing consumer QPC。成功 snapshot/modeler 事件采样，失败与 terminal 事件保留。

## Mouse A/B

RTS decoder 的 `TDK_Mouse` 映射为 MouseLeft，Down 时按既有按键状态区分 MouseRight。即使 Mouse 不产生完整 RTS 序列，DrawingController 的 contact 日志仍进入同一 timeline，因此可比较相同消费/建模链中的 Pen 与 Mouse。Touch 采用同一设备标签规则。

## Verification

- 单元测试直接生成固定 trace 事件，不创建窗口、不模拟输入。
- session 测试断言 BEGIN/END、Debug/Release 配置、采样数量、Unknown、reason 与 Drawing/Modeler 汇总。
- 容量测试断言旧 timeline 事件被覆盖后，失败总数与首末 sequence/QPC 仍存在，contact reason 未被长 sequence 截断，auxiliary 事件仍有独立输出。
- 完整解决方案使用 ARM64 MSBuild 分别构建 Debug/Release，再运行两套 ARM64 测试程序。
- 最后静态搜索 callback 路径，确认没有新增 WriteFile/iostream/allocation/阻塞 mutex/COM/wake/wait。

## Phase 2: Modeler To Present Diagnostics

第二阶段保留第一阶段全部 RTS/Drawing timeline 和 aggregate，不扩大每 packet 成功日志。新增独立 fixed-capacity POD storage：

- `DrawingFrameTrace`：每个实际活动 frame 的 frame start、previous interval、latest consumed input、dirty/render、Present correlation、deadline/wait。
- `DrawingContactFrameTrace`：按 `(tcid, cid, generation, frameSeq)` 记录 snapshot age、Modeler/real/predicted/L0 数量、L1 committed index、prediction endpoint 和 drawable/changed geometry。
- `PresentTrace`：只在真实 presenter 调用处记录 `Present1`/ULW begin/end、HRESULT、参数和连续 submission interval；`frameSeq=0` 表示初始化或非活动全量提交。
- `CompositionCommitTrace`：只包围现有 DComp initialization `Commit()`，不增加逐帧 commit 或 completion wait。

DrawingController 每帧只在 trace 已启用时执行少量 QPC、标量复制和固定数组写入。每笔前三帧另存于固定 contact slot，timeline 覆盖也不能丢失落笔初期证据。Contact summary 和 Pen/Mouse aggregate 在 RTS 停止后的 flush 阶段格式化；热路径不排序、不写文件、不构造字符串。

`Present1` 使用现有 `SyncInterval=0`、`PresentFlags=0` 和 dirty rect，不改参数。日志中的 `presentEndQpc` 仅表示 CPU 调用/提交返回，不能声称测得扫描输出或 input-to-photon latency。

当前静态调查结论必须随日志输出/报告保留：swap chain 尝试 `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` 并调用 `SetMaximumFrameLatency(1)`，但源码没有 `GetFrameLatencyWaitableObject()`，也没有等待该 handle；本阶段不得接入。
