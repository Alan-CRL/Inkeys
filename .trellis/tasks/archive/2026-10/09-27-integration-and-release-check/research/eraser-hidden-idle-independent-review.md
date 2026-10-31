# Win32 Release 隐藏橡皮 idle 断言独立只读复审

审查截止：2026-09-29 00:18 UTC。`Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` 当前 Git blob `4d363a80920961900c6f1de9ab092a298bd7e34a`；严格 UTF-8 可解码、无 BOM、全文件 2624 个 CRLF、裸 LF 0，`git diff --check --` exit 0。本轮只看该文件中 `:1562-1603` 的 idle 修补和相应 Host/Controller 调用链；同文件 H0→当前还包含其他隐藏测试与兼容改动，未据此替它们背书。不改产品/测试/共享任务文件，不运行构建、GUI 或性能采样。

## Findings (fixed in candidate)

- **旧合并断言的基线确实不可靠：** `HiddenWindowTest.cpp:1562-1568` 将 80 个 `kDraw3HiddenTestContactMessage` 经 `PostMessageW` 异步入 HWND 队列，循环后的 `large` 不是最后一条 Move 已由 Host 发布、Draw3 消费的屏障。`Host.cpp:1611-1615,1537-1545` 在消息分发时才调用 `PublishHiddenTestContact`/`input.PublishMove`；`ContactInput.cpp:766-796` 在成功发布 snapshot 后才计 `movePublished`，`Host.cpp:1419-1424` 才把该计数放入 RuntimeSnapshot。绘制线程的 `realPointCount` 来自 `DrawingController.cpp:7124-7130` 的已消费 stroke 点，并经 Host eraser 诊断锁另行发布（`Host.cpp:588-596,1397-1399`）。原 `large` 的计数与随后 idle 诊断无共同原子时点。
- **原始失败可进一步定位但不能归因：** 首轮 Win32 日志 `hf-code-freeze-Win32-draw3-eraser-hidden-test.stderr.log:1344` 的 `large.points=1660`，`:1356` 在 idle 约 247 ms 时已为 1672，`:1377` 在 idle 约 1013 ms 时仍为 1672，历史半径 80、下一半径约 9，`:1379` 合并断言失败。因此旧 `pointCount` 等式在该次运行中必定为假，且这 12 点在所见 247 ms 到 1 秒区间没有继续增长；但日志没有同步的 Move 发布/消费序号，不能证明它们一定来自迟到的真实消息，也不能宣称产品完全无早期 idle 伪点。同一二进制复跑在 `hf-code-freeze-Win32-eraser-repeat.stderr.log:2071` PASS，只证明条件受时序影响。
- **新断言仍有实际约束：** `HiddenWindowTest.cpp:1578-1603` 先等活动接触的 `idleSeconds>=1`、cursor/next radius 收缩，再在 `quiet` 断言 `historyRadiusPx > nextRadiusPx*1.5`；之后等待精确最小尺寸，并在原 250 ms 安静窗口末检查 `inputMovePublished` 和 `realPointCount` 与 `quiet` 相等，且 frameSequence 至多增长 2。它避免把已排队或模型尚未消化的真实 Move 当成 idle 伪点，同时继续检查达到 idle 后的静止点数和停止重绘。仅修改隐藏测试的断言基线；未见这组改动影响生产输入/渲染路径。

## Findings (not fixed)

- **P2，测试覆盖范围收窄且代码名易误读：** `:1576-1585` 的 `s.inputMovePublished>=postedMoveCount` 对从 `large` 后单调增长的成功发布计数恒成立，实际不证明 80 个已 Post 的 Move 都分发完，也不再检查从 `large` 到 `quiet` 的无新 Move/点。新 `quiet→settled` 比较覆盖 idle 已观测后的窗口；若产品只在最后真实 Move 到首次 1 秒 idle 之间插入伪点，新测试可能放过。鉴于旧 `large` 不是有效消费屏障，这个收窄有理由，但不应描述为原“整个 idle 区间无伪点”合同完全保留。若发布门要求这一更强合同，建议另加明确的最后输入分发/绘制消费屏障，再取早期基线；单纯恢复旧等式或延长 timeout 都不能证明它。
- **P2，失败诊断仍不够分辨：** `:1599-1601` 把 Move 计数和点数合并在一条无数值的 Check 中。若复测失败，仍无法仅凭该断言判断是新真实 Move、模型迟到还是伪点；`quiet`/`settled` 的两个计数与半径应在失败时分别记录。历史宽度仅在 `quiet` 测一次相对比值，未在 `settled` 核“相较 quiet 未被改写”；这是原测试已有的限度，此修补没有进一步削弱它。更小的局部改法是保留本次 quiet 基线与现有时长，补充失败时的两组计数；若要恢复早期区间覆盖，则需额外输入/消费同步证据，不能仅删减本次改动。
- 当前首轮失败和同二进制复跑 PASS 都发生在本次断言修补前，不能作为新 blob 的绿证。本轮未复跑 Win32 Release 连续三轮、ARM64/x64 同入口、真笔/Win7；根因“消息排队”仍是与源码和点数轨迹相容的解释，不是已证明的唯一原因。

## Verification

- 静态：核实际修补 hunk、`WaitUntil` 的 10 ms 轮询、异步 HWND→Host→ContactInput→Controller 点数链、原始 Win32 两份日志、当前 blob/文件格式与 `git diff --check`。未发现新产品代码阻断；上述两项为测试证据强度问题。
- Lint/TypeCheck/Build/Tests：按父任务明确只读且构建槽被占用，本轮未运行；修补后的任何 PASS 应以新源码对应产物的真实退出码和日志单独记录。

## 增量复核：删除无效单调计数条件

审查截止：2026-09-29 00:20 UTC；当前 `Draw3.HiddenWindowTest.cpp` Git blob `717f5c4498f64c65b19953766a5d7ebb8139eb2f`。相对上节 `4d363a80...`，`:1576-1584` 删除 `postedMoveCount` 与 lambda 捕获；WaitUntil 只检查同一 eraser 仍 active、`idleSeconds>=1`、cursor 与下一几何半径已缩小。严格 UTF-8、无 BOM、2623 个 CRLF、裸 LF 0；`git diff --check --` exit 0。没有改产品链。

- **前述“无意义 `>=`” finding 已关闭。** 当前 WaitUntil 不再暗示已证明全部 80 条 Posted Move 被分发；`:1585-1602` 仍在真实 idle 之后锁存 `quiet.inputMovePublished` 和 `quiet.eraser.realPointCount`，并在等最小尺寸及 250 ms 后与 `settled` 精确比较，帧停止也仍要求 `settled.frameSequence<=stopped+2`。历史半径断言仍为 `quiet.historyRadiusPx > quiet.nextRadiusPx*1.5`，并**未**在 `settled` 比较历史半径；不要把它称为整段安静窗口内半径完全不变。本次删除未引入新的代码或测试阻断。
- **剩余 P2 覆盖缺口未变：** `:1562-1568` 的最后 80 个 `PostMessageW` 到 `:1585` 观测 idle 并没有最后输入的分发/绘制消费因果屏障。新断言只证明 `quiet→settled` 区间不再增加已发布 Move 和真实点；若最后真实 Move 后、首次达到 1 秒 idle 前出现伪点或历史半径异常恢复，当前测试可能看不到。旧 Win32 首轮 1660→1672 的来源仍未获同步序号证明，不能据此把产品问题归因为测试时序。若需要更强声明，应先建立输入消费屏障和独立失败计数证据，再对屏障之后的早期 idle 取基线。
- 本增量仅静态核当前文件、指纹及格式；没有重跑 Win32/ARM64/x64 隐藏测试。前一版及其更早的一红一绿均不能代表新 blob 的动态结果。

## 最终增量复核：安静窗口历史半径

审查截止：2026-09-29 00:26 UTC；当前 `Draw3.HiddenWindowTest.cpp` Git blob `72e74f16a7c42bd00e75fe2d2f1c6637b7d670e1`。相对上节 `717f5c44...`，在 `:1601-1602` 增一条 `std::abs(settled.eraser.historyRadiusPx - quiet.eraser.historyRadiusPx) < 0.001f` 的独立断言；`:17` 已含 `<cmath>`，本文件其它位置也使用 `std::abs`。严格 UTF-8、无 BOM、2625 个 CRLF、裸 LF 0，`git diff --check --` exit 0。

- **上节“仅在 quiet 检历史宽度”的缺口在 quiet→settled 区间已关闭。** `:1585-1590` 先以 quiet 检历史半径仍显著大于新游标半径；`:1591-1604` 等待实际尺寸到最小，再经过 250 ms，依次核没有新增 Move/真实点、历史半径与 quiet 差小于 0.001 px、帧数至多增长 2。`modeSucceeded &= Check(...)` 不短路，任一项失败均计入结果。float 绝对差比较对非有限值也不会误通过；未发现断言顺序、头文件或类型的新阻断。
- **最后 PostMessage→quiet 的因果屏障缺口仍在。** `:1562-1568` 的异步 Move 到 `:1585` 静止基线之间，没有证明最后一条 Move 已经由绘制线程消费。新半径断言只能证明 quiet 之后的历史宽度不变，不能证明早期 idle 未有伪点或历史改写，也不能把旧 Win32 首轮 1660→1672 的原因确定为测试时序。若发布结论要覆盖这段早期区间，仍需独立的输入消费屏障和对应证据。
- 本轮只读静态复核；没有构建或运行新 blob 的 Win32/ARM64/x64 隐藏测试，旧日志不作为新 blob PASS。最终结论为无新增源码/测试阻断，保留上述 P2 覆盖边界。
