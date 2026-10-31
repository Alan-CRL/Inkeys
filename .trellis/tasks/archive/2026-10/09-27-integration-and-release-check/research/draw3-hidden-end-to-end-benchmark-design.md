# Draw3 隐藏 Host 输入至呈现基准：测量门与设计（2026-09-29）

关联专项：`.trellis/tasks/09-27-draw3-performance/`。本文件是**实现前设计审查**；当前 `HostRuntimeSnapshot` 尚不能把成功 Present 精确归属到指定笔画，因此没有新增 benchmark 函数、CLI 或性能数字。

## 生产路径与环境

已验证的 `--draw3-hidden-test` 先由 Window Service 创建屏幕外、不可见的 Drawpad/Presentation HWND，`StartProduct` 启动真实 Host、RTS 初始化、ContactInput、DrawingController/modeler、独立 D3D11 renderer 与 DComp/ULW presenter。`kDraw3HiddenTestContactMessage` 仅显式测试选项启用；Drawpad WndProc 转发给 `Host::PublishHiddenTestContact`，生成 QPC、压力、设备/接触身份，再走与 RTS 共享的 contact mailbox。测量若实施，输入源必须明确标为“合成隐藏窗口消息”，不能宣称真实笔硬件采样或光学端到端；DComp/ULW 的成功调用时间只能是软件呈现代理。

每轮需记录进程 PID、EXE 配置及 H0/HF 工作区指纹、测试时钟 QPC 频率、系统/CPU/GPU/驱动、显示器/DPI/刷新率、供电、Window Service 四个 HWND 身份、实际 `HostRuntimeSnapshot.presentationMode`、D3D feature level/driver type（当前 Host 未公开这些字段时标记不可得）、主题/效果开关、输入轨迹与每个原始样本。禁用其它编译、扫描及性能任务；测试只写唯一忽略的 `TestResults/release-hardening/` 目录。测试必须保持正式渲染画质、样本数、动画与 DComp/ULW 选择，不把 Win7 实测可用的 `FLIP_SEQUENTIAL` 回退掉。

## 现有观察口不能证明“该笔成功 Present”

| 需要的边界 | 现有字段/源码 | 缺失的因果关联 |
| --- | --- | --- |
| 消息发布 → 模型消费 | `Host.cpp::PublishHiddenTestContact` 在 owner 消息处理内创建 QPC；`PenRuntimeDiagnostics` 有 `strokeId/inputSequence/realPointCount/active` | 原始 QPC、每个源样本 ID 不回传测试；这些诊断仅证明模型收到某笔，不能证明该笔已栅格化或呈现。`WaitUntil` 每 10ms 轮询尤其不能当 1–2ms 输入延迟。 |
| 成功 Present | `Host.cpp::ObservePresented` 在 `succeeded` 时增加全局 `successfulPresentCount`，然后分别发布 `lastPresentSucceeded`、dirty、output、`presentedContentRevision`；`RuntimeSnapshot` 分开读取这些原子量和 pen mutex | 成功计数没有 `strokeId` 或消费到的 `inputSequence`。快照非同一帧事务，`successfulPresentCount > before` 加 `pen.active`/dirty 坐标也可能拼接不同帧，不能将计数增量归属到本次 Down/Move/Up。`WaitForRuntimeRevision` 不逐成功 Present 唤醒。 |
| 文稿版本 | `DrawingController.cpp::publishCurrentPageContent` 只在 `hasContent` 布尔变化时推进 `currentContentRevision_`，Presenter 回显这个 revision | 同一非空页后续笔画的内容版本不变；活动笔 Down/Move 更不以该版本区分。因此 `presentedContentRevision == contentRevision` 不能证明目标笔画像素已进入成功帧。 |
| Up → 最终稳定 | `inputRecycled`、`pen.active=false`、`currentPageHasContent`、全局成功计数 | `Up` 消费、历史烘干、L2 与最后成功帧之间缺笔画/提交 revision 联结；不能用逻辑提交时间或“之后某帧成功”代替最后结果稳定。 |
| 队列深度 | `ContactInputDiagnosticsSnapshot` 内有 `occupiedSlots`，内部 queue 有 `size_approx()` | `HostRuntimeSnapshot` 未提供活跃槽或 queue 深度；`downPublished/inputMovePublished/recycled` 口径不同，不能相减伪造积压。 |

因此**现在不能有效报告 Down→该笔首个成功 Present、Move→包含该样本的成功帧、Up→该笔最终稳定、队列深度分位数**。已有 `--draw3-hidden-test` 的 `successfulPresentCount` 断言是功能回归，不是逐笔延迟样本。本任务先停在测量门，不实现 `RunHiddenWindowEndToEndBenchmark()`，主 agent 暂不把该符号接进 `IdtMain.cpp`。

## 若冻结跨文件观察合同后的实施方案

必须由 Host/Controller owner 独立设计并审查：在**绘制线程构建的实际待提交帧**中锁存本帧 `strokeId`、已消费的该笔 input sequence、终态/烘干 revision 与输出/设备 epoch；只在对应 Presenter 返回成功后，以单一版本化快照发布 `{successSerial, QPC, strokeId, consumedSequence, finalRevision, backend, dirty, outputEpoch}`。不能在成功回调时再读取“最新 pen 诊断”倒填旧帧。对同时存在多笔/光标的帧，需定义一帧覆盖的多笔上界或单笔测试隔离合同，不能只记录一个任意 strokeId。队列采样至少分别记录 `size_approx()` 与活跃槽/producer marker，明确近似性及 sampling overhead；不从 `published-recycled` 反推。

合同就绪后，测试文件可新增精确签名 `int RunHiddenWindowEndToEndBenchmark() noexcept;`，由主 agent 独占 `IdtMain.cpp` 接显式 CLI。每个事件先记录发送端 QPC、消息 owner 接收 QPC、Host 发布后的 source QPC/序号、模型消费 QPC、匹配成功帧 QPC；保持相同轨迹与每个必要 Down/Move/Up/Cancel，不以 latest-only 或降帧产生“收益”。Down 样本截止于**首次明确包含该 strokeId/Down sequence 的成功帧**，Move 截止于包含相应 sequence 的成功帧；Up 同时要求终态被消费、最终 L2 revision 被成功帧回显且状态在预定安静窗口内不再变更。这样仍是软件成功 Present 上界，不是显示器发光时刻。

每个 backend/工具/轨迹单独进程三轮，进程间不共用缓存：每轮先 ≥16 条完整笔画预热，再 ≥100 条测量笔画；冷启动与首交互另列。活动 Move 逐样本计数，不把 100 条笔画下几千 Move 均值叫逐笔 P95。每轮保留事件级 CSV/JSONL、失败样本、QPC 原值、输入序号、成功帧序号、renderer/backend；汇总 median/P95、长帧比例与轮间噪声。100 条笔画的 P99 接近极值，必须标“探索性、不稳定”，若发布判断依赖 P99 应增至约 ≥1000 有效事件并重复轮次；不能挑最好一轮。线程 CPU、进程 private bytes/working set/handle、可得显存指标和 cache hit/miss/create/evict 分开列，无法取到的 GPU/显存/缓存指标明确 `未验证`。一旦漏序、epoch 变更、Present 失败、worker I/O 阻塞或 >限定超时，原始失败仍入分母并标无效轮，不静默剔除。

HC/H2 的既有二进制没有同一观察合同；即使 HF 将来有本软件基准，也不能据此与用户指定 Canary/Inkeys2 做同机逐笔量化胜出。真笔硬件、Win7 SP1 仅 KB2670838 的 FL11 有/无→HARDWARE/WARP、DComp 不可用→ULW、FLIP，以及光学延迟仍是独立人工/设备门禁。
