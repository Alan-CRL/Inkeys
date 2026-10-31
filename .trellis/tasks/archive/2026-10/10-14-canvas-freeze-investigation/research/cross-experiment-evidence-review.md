# Research: 输入唤醒、管道背压与 PPT 实验的证据边界复核

- Query: 在真实 PPT probe 已通过后，交叉复核 input-wake 与 pipe 实验，指出可能被最终报告过度推断的部分；不改其它研究者的文档或产品。
- Scope: internal；只读当前源码、生成器、原始实验日志、采样栈、状态 JSON 和既有本地截图。未运行 GUI、未编译/运行 probe、未执行 Git。
- Date: 2026-10-15（Asia/Shanghai）

## Findings

### 1. 可使用的结论层级

三项实验分别验证不同机制，不能合并成一个“已找到用户视频统一根因”的结论：

| 实验 | 已有直接证据 | 可以报告 | 不能据此报告 |
| --- | --- | --- | --- |
| Input wake | `Build/CanvasFreezeInvestigation/input-wake-probe.log` 全 PASS、`probe_failures=0`；真实 coordinator/SpeedEraser 与原 while、DrainIngressBatch | Move 不推进 generation；摘录 while 经超时继续等待；真实 Up 能使它退出，包括 command boundary 条件 | 真实 Touch/Pen/RTS/完整 Run 已完成 GUI 复现，或一次接触后所有后续输入永久失效 |
| stdout pipe | 同 PID read→pause→read；read bytes 60205→60205→145487；同 Draw3 TID 连续三次采到同步 WriteFile；恢复后有 frame/present；截图显示 Setting 页面变化与恢复后的红线 | 在受控读端停读条件下，实际 Draw3 owner 可以被同步输出阻塞，而 Setting 仍能操作；读端恢复后绘制消费者继续前进 | 用户控制台必然处于 QuickEdit；原视频必然同根因；已经获得完整、PAC 验证的 Flush→Run 栈 |
| PPT sharing | `presentation-load-probe/run.log:45` failures=0 与退出码0；真实 service/UInk/Bridge，原码 retry/failure/Host 条件 | 短暂分享冲突可被归为 SourceChanged，原码转 manualOnly；释放后文件正常但同 target 无自动重提，明确新 load 可恢复 | Desktop 卡死已复现、真实冷槽淘汰/GUI完整链已执行、实际等待过24小时、必须重启才能恢复 |

当前 `input-permanent-stall-audit.md` 与 `window-permanent-stall-audit.md` 大体已经声明这些限制。本复核未发现它们把所有条件候选直接写成视频根因；需要注意的是最终摘录标题、列表或进度一句话时，不得丢掉正文中的前提与恢复出口。

### 2. Input wake：已经运行的协议实验与尚未运行的完整产品行为

已直接阅读 `input-permanent-stall-audit.md:11`–`:27`、`:46`–`:116`、`:164`–`:176`、`:258`–`:264`，以及 `make-input-wake-probe.py` 和原始运行日志。

- 实验不是任意重写的等待模型：生成器摘录产品 coordinator 和原 controller while，直接包含产品 SpeedEraser；真实 PublishMove、PublishUp 和 wake generation 都实际执行。这支持“所测协议缺口存在”。
- `make-input-wake-probe.py:119` 的 `CheckActualEraserGate` 独立确认 SpeedEraser 可收敛至无动画/无 area deadline；`:144` 的 `CheckMoveAndTerminal` 再直接调用摘录 while。**实验没有让完整 controller 根据真实硬件事件自行走到该分支。** 从这两段组件证据到完整可达链，还依赖文档给出的静态分支证明。
- source.kind 和 InputDeviceType 在实验中均为 Touch（`:104`、`:112`）。Pen 的“稳定 owner、接触期不请求 cursor render”来自静态源码；不能把 Touch probe 全 PASS 表述为真实 Pen 动态验证也通过。
- “无限”是协议条件下的源码结论：测试实际超过一次1000ms timeout后仍等待，随后主动发送 Up；并没有等待无限时间。原 while 在固定 generation 下重复等待的源码使条件结论成立，不需要夸成实测永久。
- 原始日志明确有 `Up exits original idle wait`、`Boundary: Up exits original idle wait despite pending ingress` 与 terminal readable 的 PASS。这是对“有效 Up 已发布仍永久堵住全部画布”推断的直接反证。`input-permanent-stall-audit.md:89` 已正确限制，不应在最终摘要丢失。
- command boundary probe 使用 WindowController 替身的 pending 标记与原 DrainIngressBatch；它没有执行完整 active runtime 结束、renderer 成功帧、bridge state 恢复。不要将“Up 解除 while”扩大成整个产品所有后置状态已经恢复。
- current generation + pre-existing queued work 会 timeout 是 WaitForWake 的接口事实，不单独证明一个产品 lost wake。必须同时证明调用者在有 pending queue 时进入该 API、没有后续 generation；文档 `:174` 对此已有说明。

文档仍保留“只生成、编译结果由主会话另落盘”的历史口径，而运行日志现在已有 PASS。这里属于结果回链滞后，**不是实验未运行或结论反转**；主汇总应引用实际日志。未修改原文。

### 3. Pipe：实际系统等待已采到，栈深与 console 类型仍须保留限制

#### 3.1 直接核验的数据

`pipe_backpressure_probe.py` 确实创建 stdout=PIPE，读端保持连接，control= pause 时仅 PeekNamedPipe、不 ReadFile；read 模式恢复排空。因此这是明确受控的外部流控条件，不能误称“原始设置操作本身必现”。

`Build/CanvasFreezeInvestigation/pipe-experiment` 的三个状态 JSON：

| 文件 | PID | mode | elapsed（秒） | bytes_read | available |
| --- | --- | --- | --- | --- | --- |
| before-status.json | 16544 | read | 89.7931 | 60205 | 0 |
| blocked-status.json | 16544 | pause | 397.2985 | 60205 | 4461 |
| resumed-status.json | 16544 | read | 497.2959 | 145487 | 0 |

available=4461 只是该次 Peek 的可读字节数，不能把它命名为“已精确测得管道最大容量”。bytes_read 不前进证明脚本当时没排空，**仅此一个计数不能证明 render 被堵**；真正支持后者的是下述连续线程栈。

- `blocked-stacks-1.txt:450`–`:453`：TID5192，三帧，ZwWriteFile、WriteFile、带高位的原始 LR。
- `blocked-stacks-2.txt:446`–`:449`：同 TID、相同系统调用与原始 LR。总计 sampled=110 failed=1，不宜将整个进程第二次采样写成“所有线程零失败”；目标 Draw3 线程自身 resume_error=0。
- `blocked-stacks-3.txt:446`–`:449`：同 TID 同等待；新增的 `normalized-candidate` 在本地 PDB 中解析为 WriteFastConsoleLine，`Draw3.Diagnostics.cpp:55`，仍明确 `candidate-only=1`。三次目标线程均归还了自己增加的 suspend count。
- `stdout.log:43`、`:53` 等 frame 将 TID5192 对应到 Draw3；`:1220`–`:1222` 后续又有该线程新 frame/runtime/present success=1。行内 tick 为 producer 记录时刻，不是读端读到该批次的时刻。
- 本研究者直接查看 `pipe-before.png`、`pipe-paused.png`、`pipe-resumed.png`：Setting 从帮助页面变为绘制配置页面，恢复图出现之前图中没有的长红线。截图是静态状态证据；尝试绘制/页面点击的时序由主会话操作记录与控制阶段提供，不能单张截图独立证明。

#### 3.2 避免的过度推断

1. **不能称为已完整展开的 WriteFastConsoleLine→FlushCursorDiagnostics→Run 栈。** 原始可靠系统帧仅展开到 WriteFile；调用者是低48位显示候选的匹配。当前源码明确 Draw3 flush 在 Run，但源码调用链和实际完整栈证据不同。窗口研究文档 `:169` 已正确保留此限制。
2. **不能从 pipe 推断 QuickEdit/console selection 已复现。** 该实验主要走 WriteConsoleA 失败后的 WriteFile fallback，和用户实际 conhost/Terminal 不同。需要单独的宿主/mode/selection 对照。
3. **不能由缓冲排空后的红线与一帧 success 推断所有输入已无损保留。** 环形诊断会丢记录，输入队列也有容量；恢复后的帧可能包含积压输入。若最终声称“恢复后新的物理 Down/Up 正常”，需源事件时间晚于恢复读端、且新 Stroke 的独立记录。即使只显示积压帧，也已经足以证明消费者从同步写返回并继续执行。
4. **不能以“UI 可用”证明整个 UI 系统所有线程都健康。** 这里能支持 Setting 在该阶段接受并呈现页面变化；Bar、overlay、业务 reconcile 是否一直畅通还要各自记录。现有截图与采样足以支持线程分离的相容性，不能覆盖所有界面。
5. **不能把构造性外部无限等待称为已证应用内 mutex 死锁。** 这里等待读端，恢复读端可解除；这正是日志所有权的活性缺口，与另一个 Window/RTS 锁环不是同一因果。
6. 原视频故障后若持续出现新 tick 的 Draw3 frame，只能排除**那个时段**永久停在 sink，不能排除后续时刻进入。反之屏幕残留末屏日志也不能证明活性。其它研究文档对此已有正确限制。

### 4. PPT 结果和编译参数的复核

已直接核对真实 `run.log:19`–`:34`：FileShare.None 获取成功；raw UInk status=6（IoError）；Current completion=4（SourceChanged）；原码 failure branch 输入抑制；释放句柄后 source revision/SHA 相同且 raw read Complete；原码 retry 不再提交。随后 `:42` 明确的新请求真实恢复 Loaded。以上不是 mock worker 返回码。

实验保留真实 service 的 owned-version metadata pin，FileShare.None 获取仍成功（`:19`），因此本轮没有因“现有 pin 导致独占句柄根本取不到”而使占用前提失效。

“24h”来自 retry API 接收的 nowMs 前移24小时，不是等待一天，也不验证产品消息泵运行一天。冷槽淘汰仅验证 eligibility；实际 eviction/reentry 调用链由源码证明，未运行完整 GUI。

首次 build 曾有 C5050 `_M_FP_PRECISE` 与 IFC 环境不一致；研究脚本已在 `run-presentation-load-probe.ps1:41` 补 `/fp:precise` 对齐产品，PS Parser 通过。未改变产品。主会话随后已复编/重跑；本复核读取更新后的 build.log（UTC 2026-10-15 00:55:41）仅一行源码文件名，C5050 已消失，build.exitcode=0；00:55:47 的 run.log 仍全 PASS、failures=0，run.exitcode=0。首轮和对齐参数后的结果均已完成。

### 5. 汇总材料时需处理的时效问题

本次复核读取时，`research/validation-summary.json` 仍写 `head_gui_status="blocked by Windows session lock"`；这属于早先尝试的状态，已不能代表后续真实 pipe GUI 实验。最终报告须按时间分开“早先锁屏阻碍”和“后续已在 HEAD 进程完成受控背压测试”，不可沿用该字段概括当前没有 GUI 验证。本研究者仅指出，未改他人共享汇总。

`research/evidence-review.md` 是此前首轮链接失败、第二轮构建尚在进行时的审读快照，正文已经写“截至本复核采样”。不能从该旧快照抽取“HEAD尚未编译”作为最终状态；新 summary 的 shared-provider exit=0、实际 HEAD 运行证据优先。

## Files Found

- `research/input-permanent-stall-audit.md`、`make-input-wake-probe.py`、`input-wake-probe.generated.cpp`：协议分析、源码提取与替身边界。
- `Build/CanvasFreezeInvestigation/input-wake-probe.log`：真实组件实验结果日志。
- `research/window-permanent-stall-audit.md`、`pipe_backpressure_probe.py`、`thread_stack_probe.cpp`：同步 sink、管道控制与 ARM64 栈显示的证据边界。
- `Build/CanvasFreezeInvestigation/pipe-experiment/*status.json`、`blocked-stacks-1/2/3.txt`、`stdout.log`：实际管道状态、重复采样及消费者 TID。
- `Build/CanvasFreezeInvestigation/pipe-before.png`、`pipe-paused.png`、`pipe-resumed.png`：实际本地画面；仅只读查看，没有操作桌面。
- `Build/CanvasFreezeInvestigation/presentation-load-probe/build.log`、`build.exitcode`、`run.log`、`run.exitcode`：真实 worker 实验及首次编译 warning。
- `research/render-permanent-stall-audit.md`、`run-presentation-load-probe.ps1`：本研究者更新的实际 PPT 结果与匹配编译参数。
- `research/validation-summary.json`、`evidence-review.md`：不同调查时刻的汇总快照，须避免当作当前最终状态。

## Related Specs

- `.trellis/spec/native-desktop/draw3-integration.md`：唯一 runtime、RTS/input、PPT恢复闸门与真实 Present 证据。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：不以日志输出占住业务线程，以及采样/聚合/非阻塞边界。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：匹配工具链/ABI；独立组件 probe 不等于完整产品/GUI验证。

## External References

本复核未重新检索网络，也没有把其它研究中引用的文档扩张为新的系统保证；仅核对本地原码与原始证据。QuickEdit、PAC 和 Windows RTS 的一般 API 描述仍不能替代本次具体现场。

## Caveats / Not Found

- 未修改其它研究者文档、共享 task design/implement/summary 或产品。
- 没有从这三项实验获得用户录屏同一进程、同一版本、同一时段的故障栈，因此用户原始问题的最终根因仍需保持条件表达。
- 首次与 `/fp:precise` 参数修正后的 PPT 编译/运行均退出0；本复核直接查看第二次 build.log，确认 C5050 已清除。
