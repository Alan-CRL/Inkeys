# 2026-11-01 等待三点软件提交与退出的独立实测复核

## 范围与当前结论

只读复核 `Build/Validation/ppt-navigation-20261101-144216-578-e2c353b3` 的 manifest、PptWait、PptPerf、ShutdownTrace 与相关原控制流。只新增本文件；没有修改产品/测试/主会话文档，没有构建、GUI、Office 或提交操作。

本次数据可以证明：最长 Previous epoch6 等待过程中，左右下控件都实际执行了三次点绘制，并在同一 attempt 的完整像素事务后获得需要显示的 native receipt；右下 surface1 首次软件回执在 Publish 后 103.6843ms，比该请求 Complete 早 972.1256ms。它不是只走到 enqueue，也不是借另一请求/另一帧的成功记录。

用户随后明确澄清：**三点会出现，约0.5秒后消失，此时画布仍禁止新绘制，再等待1秒多后页码3→4且画布开放。** 因此当前问题是提示提前结束，后续页面准备/Draw3/UI ACK 等待仍缺少提示；不能再称本版三点从未画出或用户看漏。画布继续阻塞属于现有安全门，不能为了视觉反馈提前解除。

软件成功仍不等于 DWM/光学可见；本报告没有观察用户屏幕。用户描述中的0.5秒+1秒多也不能直接套在所选 Previous epoch6 上，该条只是本轮最长请求的独立软件证据。后续应按真实场次/目标/ACK 身份关联业务结束与页面准备完成，确定提示终态；本报告不实施该修复，不改 COM。

## 采集身份、完整性与全局统计

- PID89380，manifest status=`exited`，exit_code=0，capture_errors=[]。
- EXE SHA256=`AECD45C3CA5618C67798BD6148C87640E83F512B54AE9CC466E5E97887C6D98B`。
- DLL 与内嵌 DLL SHA256 同为 `1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3`。
- 独立解析 PptWait 得3834条 record，ordinal恰为1..3834且无重复，全部同PID、frequency=10,000,000、正QPC。
- stderr 第11096行 summary：sealed=1，capacity=16384，seen=retained=3834，dropped=invalid=output_errors=0。没有把“缺尾部flush”当作“没有等待”。
- 35个 publish-decision 全部 accepted，35个 complete-decision 全部 accepted。
- scene-draw 共845条：813 drawn，均 dots=3/branches=1；另32 not-drawn，均 dots=0/branches=0。
- pixels-result：812 submitted、32 no-pixels、1 retry。
- native-receipt：801 receipt-committed、42 candidate-stale；不能把 stale 当成功，也不能把所有 committed 当新画点。
- 对非零attempt逐条匹配，745个需要显示的新像素成功回执均找到唯一对应 Draw/Pixels，核对PID、session/epoch/direction、surface、publication/model revision、layout session/epoch、mode、pixel revision和时间先后，没有身份不匹配。
- 另56个 committed 的 attempt=0、flags=14，无 ShouldShow/PixelsSubmitted，属于隐藏/复用事务；不关联任何画点 attempt。42个 stale 中38为非零attempt、4为0，均不纳入成功链。

PptPerf 独立原始计数与summary一致：7309事件、3564span、35请求、0issues。summary的complete/0issues表示计时记录完整，不是等待视觉或性能验收结论。

## 最长 Previous epoch6 的原始计时

按同PID/QPC直接计算，native-request span31：

- begin QPC=1131315907122，end=1131326666592，总计 **1075.9470ms**。
- managed request6 mailbox-enqueue=1131315910155，dequeue=1131325222696，邮箱等待 **931.2541ms**。
- owner执行 **143.8306ms**，native queue=0.1545ms，focus=0.0639ms。
- 邮箱等待与 maintain-binding 剩余180.5474ms、Observe750.6926ms重叠，合计931.2400ms，余0.0141ms；导航长等待仍存在，不能由本次提示已绘制推导性能问题已解决。

PptWait publish-decision 为 ordinal209、stderr第7470行，QPC=1131315907940，即native请求开始后0.0818ms；session1/epoch6/direction1（Previous），activeSession1、activeEpoch6、completedEpoch5，flags15。

随后 publication118 的 snapshot/model accepted，model revision46→47（第7471-7472行）。Complete 为ordinal306、第7567行，QPC=1131326666039，距Publish **1075.8099ms**；完成水位推进到6，waiting清除；publication121/model47→48 的清除快照在第7568行接受。Complete marker距native-request结束仅0.0553ms，与当前提示跟随业务完成终止的代码边界一致，不证明页面/Draw3/UI ACK也已完成。

## 每个surface的真实同attempt链

以下均以 Publish QPC1131315907940 为0ms，不按最终flush的文本输出时刻计时。

| surface | scene configure ms | 实际draw ms | 完整pixels submitted ms | 同attempt native receipt ms | 判定 |
| --- | ---: | ---: | ---: | ---: | --- |
| 0 左下 | 18.7040 | 19.2498 | 70.1272 | 102.8323 | 有新三点像素与显示回执 |
| 1 右下 | 70.4284 | 71.9182 | 100.4267 | 103.6843 | 有新三点像素与显示回执 |
| 2 中左 | 100.8797 | 无该epoch实际Draw | 无 | 109.3503 | 隐藏/复用回执，不是新画点 |
| 3 中右 | 101.1321 | 无该epoch实际Draw | 无 | 109.5323 | 隐藏/复用回执，不是新画点 |

### 左下 surface0

同一 session1/epoch6/direction1、publication118/model47/layoutSession1/layoutEpoch2/mode1：

- attempt34，draw QPC1131316100438，dots3/branches1，flags142；第7475行。
- pixels submitted QPC1131316609212，pixel_revision176，flags174；第7476行。
- receipt committed QPC1131316936263，attempt34/pixel_revision176/candidate804/frame177/scene_serial177，flags110；第7483行。

### 右下 surface1

同一 session1/epoch6/direction1、publication118/model47/layoutSession1/layoutEpoch2/mode1：

- attempt35，draw QPC1131316627122，dots3/branches1，flags142；第7479行。
- pixels submitted QPC1131316912207，attempt35/pixel_revision421，flags174；第7480行。
- receipt committed QPC1131316944783，attempt35/pixel_revision421/candidate805/frame421/scene_serial421，flags110；第7487行。

flags174=PositionActive2+Waiting4+Visible8+PixelsSubmitted32+DrawInvoked128。flags110=PositionActive2+Waiting4+Visible8+PixelsSubmitted32+ShouldShow64，证明该回执要求显示且携带这次新像素；不是只提交隐藏窗口。

首个右下回执比Complete早 **972.1256ms**。该epoch右下共19次真实Draw及19次pixels submitted，17个成功同attempt回执；最后一个为attempt54、publication119/model47、pixel_revision439、candidate825，第7561行，Publish后486.6310ms。未匹配回执的像素不能补成成功；此处证明不依赖那些attempt，也不声称整个区间持续光学可见。

### 中间两个surface

surface2/3分别为candidate806/807，attempt=0、pixel_revision=0、flags14。flags14只有PositionActive/Waiting/Visible模型位，没有ShouldShow64/PixelsSubmitted32；这是模型含等待但本目标隐藏/没有新像素的成功原生事务。不得用它们为其它surface或本次三点绘制背书。

### 证据到达的软件边界

源码中pixelsSubmitted只在原GetDC、ULW、ReleaseDC、EndDraw全部成功后置位；receiptCommitted在原pending身份、窗口实际状态、presentation lease、scene candidate及当前revision校验通过后记录。本报告用私有waitingAttempt随原completedPixels→pending.pixels传递的身份核验，不用当前TLS或后来model反填旧帧。

Draw的三次FillEllipse仅证明已有绘制命令执行；与submitted及同attempt receipt结合，才能确认进入完整软件提交链。当前日志没有真实像素读回、截图或光学测量，不能进一步证明屏幕上每颗点的对比度、遮挡或持续可见时长。用户的实际反馈是“三点已出现、之后提前消失”，必须保留为独立事实。

## 正常退出及CP退休的独立结果

本次原生ShutdownTrace全程是 **94事件、47个完整span，零未闭合**；其中启动阶段前两次较早结束的ppt-managed-service及对应snapshot-reset多出4个，扣除后为本次退出关联的43个span，不能按上一样本固定条数判丢失。

- 首个set-off QPC1131918282960 → main-shutdown end：**896.3010ms**；main-shutdown本身 **803.0359ms**。
- 到manifest ended_utc约 **1283.464ms**，包含父采集者WaitForExit/流排空，不是精确进程死亡tick。
- Arm90.9382ms；PPT状态等待509.4385ms与main join PPT487.0652ms互相重叠，不能重复累加。
- StopDraw3 81.6925ms、Setting3.1957ms、Window107.3754ms、RenderPipeline2.5368ms、main COM cleanup0.1708ms，均闭合。
- 最终managed finally span3476为 **111.4314ms**；FullCleanup(final) span3478为 **4.4234ms**。
- disconnect3.2141ms；window/presentation/application释放scope分别0.0111/0.1107/0.2129ms，均返回。scope返回不等于COM引用计数已归零。

**本次不能写“没有retired lease”。** stderr第7206-7207行明确进入cleanup-event-retire，随后最终退休scope3492耗时106.9024ms，执行三次retry与两次wait。第一次wait0.0113ms，第二次106.2210ms；至少前两轮结束后PendingCount仍非零，才会进入wait分支。

第三次retry结束后没有最终PendingCount、TryDisconnect结果或PptComService返回值的数字marker；普通pending文本也未采到。因此最终lease是否清零是 **unknown**。进程exit0及所有scope闭合只证明本次Inkeys有界退出，不证明CP已全部释放；也不能反向断言必然泄漏、PowerPoint未退出或下一文稿不能打开。

## 后续边界

新数据已排除“此次长请求期间Publish全被拒/从未进入Draw或软件呈现”的解释。用户澄清把待修范围指向业务返回后、页面准备和真实ACK仍未完成的阶段；应保留等待提示直到准确的终态，而不是提前解除画布准入。具体完成、拒绝、取消、结束场次和过期请求规则需按实际源代码设计，不能仅延长固定秒数或修改COM来遮掩。

旧E1430016退出故障仍未在带阶段的新样本中复现，本次正常退出不能倒推旧卡点。CP退休最终状态则是本次新记录中的独立证据缺口，必须与三点生命周期问题分开陈述。
