# Win32 Touch 面积隐藏测试观测门独立只读复审

审查截止：2026-09-29 01:00 UTC。`Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` 当前 Git blob `9b46f9563aae7ebb6e07bf78df6e322bba4f7535`，严格 UTF-8、无 BOM、2650 个 CRLF、裸 LF 0；`git diff --check -- Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` exit 0。对照上一轮冻结的 `72e74f16...` 源码摘录和本轮实际 H0→当前相关 diff hunk，新增范围仅此隐藏测试中旧 Touch Cancel、新面积 Down 与 35 Move 后的观测；本文件其他历史 diff 不在此结论内。未改产品/测试源码或共享文档，未构建/运行 GUI/采样。此前 Win32 同入口 0/1/0 仍是发布验证阻断，新观测门尚无动态结果。

## Findings (fixed in candidate)

- `HiddenWindowTest.cpp:1742-1748` 在 Laptop Touch 的显式 Cancel 前取 Host 快照，要求 `PostMessageW` 入队成功，并等待 `!s.eraser.active`、`inputTerminalPublished` 和 `inputRecycled` 都较基线前进。`Host.cpp:1533-1565` 把成功 Cancel 交真实 `ContactInputCoordinator`，`ContactInput.cpp:564-640` 只有 Close 成功才计 terminal，route Free/`ReleaseSlot` 后才计 recycle；普通 ConsumerOwned 还由 Controller 末帧 `DiscardUntilTerminal`（`DrawingController.cpp:10345-10356`）退休。此门比旧 `!active` 单条件更接近 route owner 交接，15 s 默认 `WaitUntil` 不改变 35 次面积 Move 的目标时距。
- `HiddenWindowTest.cpp:1790-1798` 对 `(60,140)` 面积 Down 核 Post 成功、`inputDownPublished` 增长、当前 eraser active/Touch、面积开关已锁存、非零 generation 与输入位置 y=140。`ContactInput.cpp:732-759` 新 Down 才会计 downPublished；`DrawingController.cpp:7124-7130,7180-7194` 的诊断位置与 generation 来自选中 runtime。前一 Laptop Down 坐标是 `(60,120)`，因此该门在本测试序列里足以排除“只读到上一接触的 active”这一误判；不要求每槽代次数字必须大于前一槽，因为新 Down 可占不同槽。这是正确的身份检查，没有新增脆弱的代次数值排序假设。
- `HiddenWindowTest.cpp:1803-1825` 仍发送原 35 个、每次 `sleep_for(30ms)` 的 Move；`areaMovesPosted &= postSource(...)` 在每轮都求值，任一 Post 入队失败会单独报错。`[AreaIngress]` 只有一行，format 的 `%u/%llu/%d/%.3f/%.1f` 与各 `static_cast`、整型提升和 float→double 实参一致；数值来自段末 Host 快照，原 `>38.5 && <40`、面积活性/光标条件与后续 held/expiry 断言均未放宽。此次观测修改不触碰 Host/ContactInput/Controller/SpeedEraser 产品代码。

## Findings (not fixed / evidence boundary)

- **P2，旧 route 恰次退休并未由三个全局量严格证明：** `inputTerminalPublished`/`inputRecycled` 是 Host 的全局累计诊断（`Host.cpp:1419-1424`），不是 `(tabletContextId,contactId,generation)` 的回执。紧前 `HiddenWindowTest.cpp:1734-1736` 的上一课堂 Touch Up 仍只等 `!eraser.active`；它的末帧 recycle 若落在 `beforeSceneCancel` 之后，新的 Cancel 可以提供 terminal 增量、旧 Up 可以提供 recycled 增量，使 `:1745-1748` 同时为真而当前 Laptop Cancel 所属 slot 尚未释放。generation/route 代码未显示因此会误路由，但“retire old route”这条测试标签比已证明的范围强。若需要关闭此交错，最小测试补强是在 `:1734` 前后也等上一 Up 的 `inputRecycled` 前进，再取 `beforeSceneCancel`，或提供精确 handle/generation 退休回执；不要把全局计数相加冒充身份绑定。
- **P2，`[AreaIngress] movesAccepted` 是“当时已发布数”，不能直接读作 35 条全部送达或绘制消费数：** `postSource` 只说明 `PostMessageW` 入队；`Host::PublishHiddenTestContact`/`ContactInput::PublishMove` 后才计 `movePublished`，而 `Host::RuntimeSnapshot` 先取 eraser 诊断锁、稍后才取全局 input 计数（`Host.cpp:1397-1424`），两组数无共同原子时点。段末 `:1811-1822` 的一次日志可能小于 35，只因 HWND 尚有待分发消息；它也不能给出绘制线程所见的最大原始样本间隔或失败前实际直径峰值。此打印尚未被当作 PASS 条件，因此不新增假失败，但分析新日志时必须按“snapshot 时已发布的 Move 增量”解释。建议在必要时于慢拖 WaitUntil 失败后再打印计数/面积诊断，并用接触身份或原始 QPC 轨迹区分被拒、迟到和绘制覆盖。
- 回收门和段末 `fprintf` 会改变面积段**之前**的调度与 WaitUntil 前的微小时序；原 35 个 Move 和阈值虽保持，若新二进制转为全绿，也只能证明加入门后的序列通过，不能回推 round2 的根因已修。原 round2 `seq697/699` 是 `(60,120)` Laptop 接触及显式 Cancel，`AreaProbe active=0` 是面积下限状态而非接触终止；见前一独立调查。35 Move 的 80 ms 生产采样门、测试发送/发布/消费区分及先前 0/1/0 均仍需新 Win32 Release 实际证据。

## Verification

- 静态：核本轮真实 hunk、消息→Host→ContactInput→Controller 计数/身份链、日志 format 参数顺序、旧/新坐标区别、文件编码/换行与 diff check；未见新源码阻断或错误的代次比较。
- Lint/TypeCheck/Build/Tests：父任务指定只读且另有构建槽，本轮未运行；旧 0/1/0 不可标为新 blob PASS。

## 增量复核：课堂 Up 回收门与日志口径

审查截止：2026-09-29 01:04 UTC；当前 `Draw3.HiddenWindowTest.cpp` Git blob `3e09768c8051b9bbec4ea3da0e002781ffbbaae4`。相对上节源码摘录，实际 hunk 在 `:1734-1740` 为上一课堂 Touch Up 新增成功入队、`!eraser.active`、终态发布与回收增量门；Laptop Cancel 门随之位于 `:1746-1752`。`:1816` 日志字段改为 `movesPublishedSoFar`，对应 `:1818` 的段末 `inputMovePublished-areaMovesBefore`，format 实参与上一版保持匹配。严格 UTF-8、无 BOM、2654 个 CRLF、裸 LF 0；该文件 `git diff --check` exit 0。产品代码、35 次/30 ms Move 与 `>38.5 && <40` 判定未变。

- **此前日志名误读风险已关闭，Up 交接门也更强。** `movesPublishedSoFar` 准确表述拍摄该快照时已成功进入 `ContactInput::PublishMove` 的全局增量，不宣称绘制线程消费或全部 35 个 HWND 消息已分发。课堂 Up 的 `PostMessageW` 失败现在有独立断言；它的 terminal/recycled 增量及 inactive 都须成立后才发布下一 Laptop Down。两道门没有按 generation 数值排序，也没有改变面积段名义样本时距；未见新增编译类型或消息顺序阻断。
- **前述 P2 计数归属风险降低但未严格关闭。** `inputTerminalPublished`、`inputRecycled` 仍是全局累计值（`Host.cpp:1419-1424`），不是某个 handle 的回执。更早隐藏接触若留下一笔尚未回收，课堂 Up 的 terminal 可与更早 route 的 recycled 配对而通过 `:1737-1740`；随后 Laptop Cancel 的 terminal 可与课堂 route 的 recycled 配对通过 `:1749-1752`。这条“回收债逐段前移”需要旧尾帧足够迟才出现，现有日志未证明发生，但两道 `>baseline` 不能逻辑排除。若发布证据要证明**面积 Down 前所有旧接触确实退场**，最小观测是于无预期活动接触的交接点等待 `inputRecycled==inputDownPublished`（并核 terminal 计数/本次 inactive），或使用精确 handle/generation 回执；不能仅以两次全局增量命名为各自 slot 的回收。
- 新 Up 门会改变进入面积段前的实际调度，适于隔离 owner 交接，但新二进制即使转绿，也不能反推原 round2 首因或将原 0/1/0 改报 PASS。本轮仅静态核 diff、format、文件格式和指纹，未运行构建/隐藏测试；此前“35 Move 后仅一份段末快照、未记录绘制采样最大间隔/失败时峰值”的验证缺口仍在。

## 增量复核：面积 Down 前全局空路由等式

审查截止：2026-09-29 01:12 UTC；当前 `Draw3.HiddenWindowTest.cpp` Git blob `968f8411213f1807c5f87fbb12e088c2943ba1c0`。实际新增 `:1793-1795`：在原两道 Up/Cancel 门之后、面积样本设置和新 `(60,140)` Down 之前，最多等待 15 s 令 `inputDownPublished==inputRecycled`。严格 UTF-8、无 BOM、2657 个 CRLF、裸 LF 0；`git diff --check --` exit 0。35 Move、30 ms、38.5 DIP、产品源码未动。

- **P1 测试门风险：该绝对等式不是当前 Host 跨 RunMode 生命周期的不变量。** `Host.cpp:1041-1045` 每次 Start 调 `input.ResetForNextRun()`，`ContactInput.cpp:1053-1079` 把旧记录置 Free、清 freeMask/队列，却不清 `downPublished` 与 `recycled`；两计数在 `ContactInput.cpp:681-686` 的同一 Impl 中持续累计，`Host::ResetRuntimeDiagnostics` 也不触及它们。`HiddenWindowTest.cpp:2535-2547` 的 `eraserOnly` 入口在同一 ProductHost 实例先跑 mode0、Stop，再跑 mode1。若 mode0 Stop 时有一个已发布 Down 的槽在 consumer 计 recycled 之前被下一次 Start 的 ResetStoppedRecord 释放，mode1 开始即有历史 `downPublished-recycled=1`；即使 mode1 当前所有合法接触都准确回收，`:1794` 也永远为假，15 s 后形成与面积模型无关的假失败。`Host.cpp:1337-1363` 停 RTS、排最终命令并 join，但没有把 Reset 清空的记录数补入 recycled；mode0 末尾普通 Pen Cancel 只等 `!eraser.inputContact`（隐藏测试 `:2101-2108`），故本报告不能证明这种尾帧差额必定为零。当前日志没有新 blob 的运行，不能断言历史差额实际发生，也不能把这个潜在门误判归于产品面积回归。
- **单次运行内的价值仍成立，但条件须限定本次运行。** 若从该次 Host.Start 后空输入时锁存 `baseDown/baseRecycled`，在面积 Down 前比较 `inputDownPublished-baseDown == inputRecycled-baseRecycled`，可排除跨 RunMode 的历史计数差，并检查本轮已接受 Down 是否全数回收。需保证基线确在本轮首个合成 Down 前且无计划中的外部接触；若要不依赖累计计数归属，直接暴露并检查当前 `ContactInputDiagnosticsSnapshot.occupiedSlots==0`（`:1172-1174`）更符合“空路由”标签，但需要测试可见的 Host 诊断字段。无论采用哪种方式，已停但未计数的旧 generation 不应拿来判本次面积 Down 的活性。
- 这条等待在已有 `modeSucceeded` 为 false 时仍会执行（使用 `&=`），不会跳过后续诊断；但因此可能增加一次 15 s 的无关超时。当前是静态发现的可执行性缺口，**不是**对新 Win32 二进制的实测失败。旧 0/1/0 仍属发布验证阻断，不能由新等式的假失败或将来偶然绿灯替代针对原慢拖首因的证据。

## 最终增量复核：按本轮 RunMode 计数归零

审查截止：2026-09-29 01:16 UTC；当前 `Draw3.HiddenWindowTest.cpp` Git blob `840ed4a8fa516485c2d7686983a9bf56b6bf793f`。新增 `:910-911` 从 `RunMode` 已有的第一次 `ProductHost().RuntimeSnapshot()` 锁存 `modeDownBaseline/modeRecycledBaseline`；`:1795-1800` 把绝对等式替换为先防计数回退、再比较两项本轮增量。严格 UTF-8、无 BOM、2662 个 CRLF、裸 LF 0，`git diff --check --` exit 0。没有改产品源或面积 35 次×30 ms、38.5 DIP 的断言。

- **上节 P1 跨 RunMode 累计差假失败已按静态合同关闭。** `Host.cpp:1041-1045` 的 `ResetForNextRun()` 在 `StartProduct` 内发生；基线 `HiddenWindowTest.cpp:909-911` 在 StartProduct 成功返回（`:897-899`）及只启用诊断（`:901-907`）之后，任何本 `RunMode` 的测试 Down 之前。最早的合成 Down 在后续 `exerciseCommands` 段（`:1003-1020`）或橡皮段；启用 `touchAreaTrace` 不发布接触。`eraserOnly` 两次 `RunMode`（`:2540-2551`）各自重新取基线，前次 Stop/Reset 可能留下的绝对 `downPublished-recycled` 常数偏移被消掉。ContactInput 在本轮内不会再 Reset；其成功 Down 才计 downPublished，route Free/ReleaseSlot 后才计 recycled。`>=` 防止无符号减法在异常计数回退时绕出大值，因此当无外部并发 Down 并且没有双释放时，两项本轮增量相等是“该轮此前已发布 Down 全已退休”的可执行前置条件。
- **未见合法 held route 导致此处必然假失败。** 紧邻的课堂 Up 与 Laptop Cancel 都已发终态并等待 inactive/terminal/recycle（`:1734-1752`）；面积 Down 在等式通过后才发布（`:1795-1805`）。此前其它试验若仍留有本轮 accepted Down，等式会拒绝进入面积断言，这是应调查的输入清理/生命周期前置失败，不能记作面积模型本身失败。Pen 中断续接候选窗口仅 80 ms（`Draw3.InkPrediction.cppm:88`），远短于本 `WaitUntil` 默认 15 s；当前代码没有计划在此交接点保留物理 Down。若外部 RTS 在基线前就意外输入隐藏 HWND，本轮差分不包含它；离屏测试没有这类预期，但这仍是对绝对“当前 occupiedSlots=0”声明的边界。需要完全不依赖计数时可另暴露 `occupiedSlots`，不应为本轮最小测试修补擅改 Host 接口。
- **性能与剩余证据：** 基线复用原有首次快照，只多读两个整数字段；面积前 `WaitUntil` 与上节数量相同，10 ms 轮询且只在隐藏测试，产品输入热路径没有新工作。`[AreaIngress] movesPublishedSoFar` 仍只是段末发布计数，不是绘制消费/样本最大间隔；新门改变面积段前调度，不解释原 Win32 round2 首因。此次仅静态核真实源码、调用顺序、格式和指纹，未构建/运行新 blob。旧 0/1/0 仍为动态发布门，须以新二进制实测单独更新。
