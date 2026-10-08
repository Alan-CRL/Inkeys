# F-045 Draw3 控制边界最终独立复审

日期：2026-09-28。角色：独立只读 reviewer；未参与 F-045 实施。本轮只读当前 `Draw3.ContactInput.cpp/.cppm`、`Draw3.Host.cpp`、`Draw3.DrawingController.cpp/.cppm`、`Draw3.WindowControl.cpp/.cppm`、`InkeysHeadlessTests/draw3_contact_tests.cpp`、`IdtMain.cpp` 的相关实际 diff、Bridge/RTS/Product 调用链、测试日志与 Trellis 规范。未修改生产或测试源码，未启动 GUI、构建或采样。其它 F-023/F-025/F-029/F-031/F-038/F-039 的同文件 diff 不归因于 F-045。

## 结论

**F-045 的命令相对已接受 Down 的顺序在现有单消费者、唯一 Host 生产调用合同下静态闭合；本机 Debug 无窗口入口红→绿支持所测子链。未发现 F-045 新引入的确定 P1 死锁或数据损坏。** 该结论不包括真实 RTS/Presenter/GPU、活动 pen 永不终态、PPT/Office、实际 Clear 像素或最后退出保存成功。用户图片的画布卡死仍不能据此宣称复现或修复。

## 实际合同核对

| 边界 | 实际证据与判断 |
| --- | --- |
| 多 producer 下 Down 与实体 marker | `ContactInput.cpp:322-347,361-389,517-530,698-729`：原 32 个 Down token 收敛为唯一显式 producer token；`atomic_flag` 仅包 `try_enqueue`、成功 Down 计数与失败 marker 水位。`PublishDown` 只有入队成功、计数发布、解锁后才返回 true；随后发起的 Command marker 与该 Down 处于同 token FIFO。Move/Up/Cancel 沿原 slot snapshot/终态发布，不被新锁包住或丢弃。跨线程同时调用且尚未返回的 Down 与命令仍以获得 ingress latch 的顺序决定；没有伪造调用开始时间的总序。 |
| Command 失败 fallback | `ContactInput.cpp:392-439,903-930`：已预约 Command 失败入实体队列时固定 `downTarget` 与 `ordinal`，单消费端必须等旧 Down 出队且此前 Command ordinal 已消费，才返回一次 `nullptr/Command`。物理 marker 成功则从同 token FIFO 出队并退还一个预约。成功和失败两条路径没有删除 contact 或重试复制 Command。General fallback 独立，不消费 Bridge 命令。证明依赖唯一消费者；公共 `TryDequeue` API 自身没有防第二消费者。 |
| 容量与拒绝 | `Host.cpp:1323-1339,1670-1678` 与 `Bridge.cpp:119-158,177-196`：普通 Bridge 队列默认最多 256、final 独立槽；Host 在同一 `commandPublishMutex` 内先预约 258 固定额度再 `bridge.Publish`，被 Bridge 拒绝则取消。已接受命令必有实体或固定 ring marker；当前产品路径下 fallback ring 占用不超过未领取预约。ring 满触发 `std::terminate` 是不变量破坏处理，不是资源失败恢复。若 Bridge 容量、额外命令写入口或多消费者改变，258 证明需重做。 |
| Host 每 marker 恰一命令 | `Host.cpp:962-1028`：`ConsumeBridge` 始终泵持久化 completion，只有 `ControlWakeKind::Command` 才 `PumpOneBridgeCommand/bridge.TryConsume` 恰一条，并以 captured scene + canvas command 入 `WindowController` FIFO；General 不偷取命令。`PublishCommand` 和 `StopWithFinalCommand` 是当前仅有的生产命令写入口；现存其它 ProductBridge 使用为 state/target 发布或 snapshot。 |
| Stop final 与重启 | `Host.cpp:1030-1043,1323-1374`：Stop 先在短命令锁内关闭 Bridge 并预约 final，解锁后 RTS Shutdown，再发布 final Command marker；Shutdown 不在该锁内。`Start` 在上一代 producer/consumer 已停止前提下先 `input.ResetForNextRun()` 再 `bridge.Reset()`；`ContactInput.cpp:999-1024` 排空队列、释放旧 slot、清水位/预约/fallback/test 注入。Host 失败启动后仍要求 Stop/join 时序，不应在未停止旧 producer 时直接调用 Reset。 |
| Controller 同帧与等待 | `DrawingController.cpp:54-69,4804-4855,7890-8003,8852-8860`：`DrainIngressBatch` 在 null callback 使真实 canvas deque 非空后停止，`commandBoundaryPending` 阻止第二次批次、后台维护的单条 TryDequeue 与两条 idle/Hold `WaitDequeue` 继续取新 Down；旧 active 从共享 slot 读取 Up/Cancelled 并在 `active.empty()` 后由 `processCanvasCommands` 排净，才开放后续 Down。普通无 canvas 命令 General wake 不设置该 gate。WindowController 查询在同一 canvas mutex 下只读队列（`WindowControl.cpp:350-363`）。当前生产 `EnqueueCanvasCommand` 调用来自 Host 的绘制线程 callback，另有无窗口 probe；若未来窗口/业务线程直接入队并要求跨输入精确排序，需新的 ingress 序号合同。 |

## 发现与保留风险

### R-045-1 — 假设，条件性 P2：既存的 latest 状态与旧 Down 时序未固定

`PublishProductState → Host::PublishState → bridge.PublishState + PublishControlWake` 走可合并 General marker（`Product.cpp:86-103`, `Host.cpp:1629-1639`, `ContactInput.cpp:361-389`）。Bridge 的 `ProductState` 是 requested/latest；`PumpBridgeState` 才把它应用到 `WindowController::ActiveTool`，而已接受的 `ContactSnapshot` 仅带 admission revision，没有工具快照。一个旧 General 已 pending 时，新工具/选择状态不会得到自己的物理边界；`ConsumeBridge` 在旧 marker callback 调 `PumpBridgeState` 读取 latest 并 `window.SetActiveTool`（`Host.cpp:863-889,1018-1026`）。随后才出队的旧 Down 在 `initializeStroke` 读取 `window_.ActiveTool()`（`DrawingController.cpp:3758`）。最小可控调度是：① Window 已应用 Pen；② 先发布一个 General marker；③ Pen Down 返回 true 但绘制线程暂停；④ 用户请求 Eraser，`PublishControlWake` 因 pending 合并；⑤ 放行绘制线程先消费旧 General，把 applied tool 设为 Eraser；⑥ 再消费旧 Down，检查其实际 `RuntimeStroke.tool`。静态链显示存在把旧 Down 依新工具初始化的可能，尚未运行该调度或确认产品是否承诺“Down 接受时工具”语义，因此**仍是假设，不是确认缺陷**。F-045 保证**业务 Command** 相对 Down 的插序，没有给普通状态发布赋 per-Down 快照；不能声称用户现场由此触发。若合同要求旧工具语义，最小验证应直接用生产 Host/Controller 的可控调度断言工具身份，再选择在 Down 锁存 tool revision 或给状态建立专用顺序边界；不能仅把 General 合并关掉却无条件增加每次状态唤醒的排队延迟。

### R-045-2 — 功能/性能验证缺口，不是已确认 F-045 新缺陷

生产 probe `RunDraw3ControlFenceProductionProbe` 调用真实 ContactInput、WindowController 和与 `Run` 共用的 `DrainIngressBatch`，但 callback 是测试手工把 `Clear` 排到 WindowController；它没有实例化完整 Host、RTS、Renderer、Presenter、自动保存 worker，也没有跑 `processCanvasCommands` 的墨迹/历史事务（`DrawingController.cpp:2032-2091`）。Headless 的多命令、失败 marker、容量、reset 测试直接驱动 ContactInput/Bridge（`draw3_contact_tests.cpp:100-448`），证明这些局部合同，不证明最终 Clear/Undo、PPT identity 或 Exit UInk durable。新 `atomic_flag` 在 Down 热路径有短自旋，尚无并发 RTS 高压时的 Down 尾延迟/CPU 数据；不能用 Debug 无窗口 PASS 推断较 Canary/Inkeys2 的真实输入手感。旧 contact 永不 Up/Cancel 时，Command gate 会等待 `active.empty()`，正常 Clear 可能保持待办；退出的 15 秒监督器可终止旧进程但不等于保存或输入恢复成功。

## 红→绿日志核验与下一步

- 入口顺序红灯：`f045-ingress-red-headless.stderr.log` 明示实体及失败 marker 的 old/control/new 两项 FAIL；Stage2 `f045-ingress-stage2-*` 构建和 `--no-window` 转绿，但当时未覆盖多命令。
- 多命令红灯：`f045-multicommand-red-headless.stderr.log` 明示第二条命令无独立边界；后续 F-045 测试要求 `Clear→Down→Undo` 两个 marker、第二 marker 失败 ordinal/水位与 258 预约/代际 reset。当前主任务验证 ledger 记录 Debug 完整 Solution 与 Headless 通过。
- Controller probe 红灯：显式等待 GUI subsystem `Inkeys.exe --draw3-control-fence-test` 的 pid 55948 exit 1，`f045-control-fence-red-explicit.stderr.log` 为“同帧吞新 Down”；修补后 pid 44012 exit 0、`f045-control-fence-green-explicit.stderr.log` 空，最近相关完整 `InkeysRepo.sln Debug|ARM64` Build exit 0、`InkeysHeadlessTests --no-window` exit 0（`f044gc-f045fence-build-debug-arm64.log`、`f045-control-fence-green-headless.*`）。PID/退出码来自父任务 `validation.md:117,119`，本 reviewer 没有重跑；早先直接 `&` GUI EXE 的陈旧 `$LASTEXITCODE=0` 不作证据。
- 必须人工或后续隔离验证：真实 RTS 高压双/多接触与快速 Clear/Undo/PPT 翻页、旧 pen 卡住后新 Down 的门禁与响应、Stop final UInk 完成/失败、Host Stop→Start、Win7 SP1+仅 KB2670838 的硬件/WARP、ULW/DComp 成功呈现、Release/Win32/x64、用户现场 GUI/桌面穿透。F-043 15 秒链需单独报告旧 PID 结束与新 PID 至多一次。F-045 绿灯不能替代这些证据。

综合状态：F-045 入口与 Controller fence 的本机 Debug 子链 **已验证通过**；真正用户卡死、跨模块 Clear/Exit durable、工具状态/Down 语义、性能与兼容矩阵 **未验证/需人工或专项**。R-045-1 是起点前已存在的条件性假设，未测得实际影响；不要将其写成 F-045 新引入 P1。
