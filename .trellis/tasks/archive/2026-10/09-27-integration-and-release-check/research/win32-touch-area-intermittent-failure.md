# Win32 Release 隐藏 Touch 面积辅助间歇失败独立只读调查

审查截止：2026-09-29 00:47 UTC。父任务记录同一最终 Win32 Release 隐藏橡皮入口三轮自然退出 **0/1/0**；本轮未运行二进制，逐条读取 `hf-eraser-idle-settled-Win32-round{1,2,3}.stderr.log`、更早 `hf-code-freeze-Win32-eraser-repeat.stderr.log` 和当前源码。源码 Git blob：`Draw3.HiddenWindowTest.cpp=72e74f16a7c42bd00e75fe2d2f1c6637b7d670e1`、`Draw3.Host.cpp=63f6518d3aa1f419fb0835e954038df8579ea72f`、`Draw3.ContactInput.cpp=e9b1792417d7cb40c14ddca969ae84ab09a83777`、`Draw3.DrawingController.cpp=6b77b771c45479a5a35a65c07280853fcd8c752a`；四文件 `git diff --check` exit 0。本报告不改产品、测试或共享任务文档，不构建/采样。

## 已确认的事件与严重度

- **P1 发布验证阻断：** round2 的四条 FAIL 在 `hf-eraser-idle-settled-Win32-round2.stderr.log:1477-1482`，全部属于第二次 `RunMode`（`mode=1`，`HostPresentationMode::UlwDirtyRect`）的同一 Touch 面积辅助序列；第一次 `mode=0` 的面积探针在 `:323-328` 成功。round1、round3 的 `mode=1` 分别在 `:1507-1512`、以及更早 Win32 复跑 `:1326-1331` 均达到参考下限 39 DIP，round1/3 末尾分别为整体橡皮 PASS。0/1/0 不是稳定 PASS，首个 FAIL 的条件未被证实满足，应保留该发布门禁。此轮不能从一次失败推出新产品回归或现场硬件缺陷。
- **首因在慢拖尺寸断言，后三项是同段后续观测：** `HiddenWindowTest.cpp:1785-1800` 设 30×20 px 合法面积并发 Touch Down，再发 35 个名义间隔 30 ms 的 Move；随后最多等 15 s 要求 `contactArea.active && effectiveDiameterDip>38.5 && <40` 且 cursor=2×nextRadius。round2 首个 FAIL 在日志 `:1477`。测试仍继续以同一 contact/area 做静止放大、稳定和过期检查（源码 `:1801-1837`）；在约 15 s 等待后 `AreaProbe:1478,1481` 已显示 `activeFloor=16`、`contactArea.active=0`，所以 `:1479-1482` 三个失败不能独立证明三个产品缺陷。该日志仍保留 `ref=firstRef=39`、`refDip=30×20`，说明**某时刻参考尺寸已被接受**；`historyRadius=19.2427`（直径约 38.4854，略低于测试的 38.5 下界）是接近阈值的线索，不是失败前实际直径峰值的证明。后续 `AreaProbe:1485-1486` 的另一段恢复达到 50 DIP，亦不证明首段本应通过。
- **`seq697→699` 不是面积 Touch 被无故终止：** round2 日志 `:1467-1476` 的 seq697 Down 坐标 `(60,120)`、contact generation 80，对应 `HiddenWindowTest.cpp:1737` 的前一条 Laptop 场景验证；约 30 ms 后的 seq699 end 对应 `:1742` 显式 `Cancelled`。面积验证的新 Down 是 `:1786` 的 `(60,140)`。`AreaProbe` 文本 `active` 取自 `d.contactArea.active`，`age` 取自 `d.idleSeconds`（`:1747-1757`），不能当成 `eraser.active=false` 或 contact 已终止。失败后仅发送 Move 的 `:1841` 使点数从 612 增至 616，且记录 `anchor=1`（round2 日志 `:1483-1484`）；这是该面积序列仍能消费输入的反证。面积段前 `SetEraserDevelopmentOptions({})` 关闭 `touchAreaTrace`（`Host.cpp:1581-1593,598-620`），故 round2 日志没有 `(60,140)` 新 Down 到首个 FAIL 之间的逐帧面积原因/直径峰值。

## 调用链与尚未证实的竞争

- `HiddenWindowTest.cpp:1627-1628` 的 `postSource` 只确认 `PostMessageW` 入队，面积段 Down/35 个 Move 的返回值均未检查。owner 分发经 `Host.cpp:1611-1615,1490-1578`，使用固定隐藏 tablet context `0xD303` 和按设备固定的 Touch `contactId`；Down/Move/Cancel 分别调用真实 `ContactInputCoordinator`。`ContactInput.cpp:732-763` 的 Down 以新 generation 占槽，`:766-796` 的 Move 在同 `(tabletContextId,contactId)` 的 Producing route 上覆盖最新快照并计 `movePublished`；绘制线程 `DrawingController.cpp:6081-6133` 只消费最新序号，合成慢于 producer 时可跨过中间 Move。这是足以使**控制器所见采样间隔**不同于测试的 30 ms `sleep_for` 的机制，但本轮没有该失败段的已接受 Move 数或控制器间隔，不能据此定因。
- 模型确有敏感门：`Draw3.SpeedEraser.h:148-154,243` 的面积参考连续间隔和运动证据上限均为 80 ms，确认累计为 50 ms、缺失释放为 2 s；`Draw3.SpeedEraser.cpp:695-718,1347-1351,1390-1395` 需要有效连续真实移动才能建立/推动面积下限。round2 的 39 DIP 参考已建立，但其实际直径是否因样本间隔、Move 覆盖、确认时点或别的产品错误而未及时跨过 38.5，**证据不足**。不能直接调宽容差或延长 15 s；规范 `.trellis/spec/native-desktop/input-and-ink.md:221-240` 要求慢拖有效面积下限，也反对放宽容差掩盖错误。
- 父任务提出的旧 Cancel/新 Down 交错可达窗口需区分所有权与证据：`HiddenWindowTest.cpp:1742-1744` 只等 `!eraser.active`，而 `DrawingController.cpp:10345-10356` 的 end-of-frame `DiscardUntilTerminal` 发生在诊断发布之后；所以新 Down 可能先于旧 slot 位图释放。可是已看到旧 `Cancelled` 对应的 seq699 end，`ContactInput.cpp:564-640` 用 generation 关闭旧 route，`:732-754` 的新 Down 取得另一可用 slot，`FindProducing:473-491` 不选已交出生产所有权的旧 route；现有日志没有显示旧终态误关闭新 generation。`Host.cpp:1537-1565` 对成功旧 Cancel 发 Touch End、对新 Down 发 Touch Begin，当前固定 contactId 需要专门日志/注入才能排除错路由，不能仅凭 30 ms 的 seq699 断言它发生了。

## 最小可执行验证与修复方向（未实施）

1. 在隐藏测试的**此段**先锁存旧 `inputTerminalPublished/inputRecycled`，对 `:1742` 的 Cancel 等终态与回收计数都前进，再发面积 Down；核 `PostMessageW` 返回、`inputDownPublished` 前进及诊断非零 `contactGeneration`、位置 `(60,140)`。这样可直接区分旧 route 交接与新 Down 未送达；若只加回收屏障使失败消失，继续用暂停旧尾帧的测试钩子复现，不能把症状消失直接等同修复。
2. 在 35 个 Move 前后记录成功入队数、`inputMovePublished` 增量及一份有限内存诊断轨迹：同一 contact generation、`sampleValid/referenceReady/referenceFresh`、`stableMotionSeconds`、`activeFloorDip`、实际/目标直径、真实点数与最大观察到的样本间隔；失败时才输出。可仅在此段开启已有 250 ms 限频 `touchAreaTrace` 辅助定位，但其日志改变时序，不能单独作为根因证据。先在同一 Win32 Release 隔离二进制串行复现；若所有已接受/已消费间隔满足 80 ms 而实际直径仍达不到下限，转产品 Controller/SpeedEraser 问题；若存在大于 80 ms 的消费缺口，追加一段有界连续有效移动并验证参考/尺寸恢复，同时保留原一次 35 Move 判定的失败证据。
3. 定向故障注入可在测试输入的连续段中人为加入一次约 100 ms 间隔，再提供新的连续 30 ms 有效 Move，分别检查参考确认重置与恢复；这是检验 80 ms 门对样本缺口的预期，不可用来宣称 round2 已被复现。若发现同身份旧终态被送到新 Down，才需要修生产 route/Host 的身份合同；否则优先改测试的确认屏障或采样诊断，不盲改模型阈值。

## 结论与未验

本次静态与日志复审确认 round2 是**同一 Touch 面积慢拖序列的间歇失败**，且此前“seq697 面积 Down 30 ms 自动终止”“AreaProbe active0 等于路由已停”的解释与实际坐标/字段不符。尚无失败段逐 Move 接受、绘制消费、模型面积诊断或新的故障注入，无法裁定测试调度、旧尾帧交错或产品面积算法哪一个是首因。发布验证保持 P1 阻断；真 Win7/真实 Touch 硬件/性能与本轮构建测试均未执行。
