# E03 初始化失败接触身份：独立实施前复审

日期：2026-09-30（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。已读真实context/artifacts、`contact-failure-identity-design.md`、`input-closing-quiescence-postcommit.md` 与当前 Controller/Coordinator/产品CLI调用链。只读，不构建/测试/启动进程、不改产品/实施者设计/共享记录，唯一输出为本报告；不递归派agent。

## 结论

**GREEN_DESIGN，可以在主会话确认E01_UNIT_FROZEN后进入仅旧逻辑共享helper+真实生产probe的红测阶段。** 设计所述同key错误目标是当前代码支持的条件性身份缺陷；使用已有精确handle Discard足够，不需改输入状态机/所有权、样本频率或新增公开probe接口。实现需遵守下面明确的逐句保留和验证口径，最终代码仍独立复审。

## 根因与真实调用者核对

- `Draw3.DrawingController.cpp::Run::initializeStroke:5202–5206` 已经持有 `(record地址,generation)`，核代次后复制不可变Down。
- 三个失败调用者当前都用 `PublishCancelled(tabletContextId,contactId,cancelled)` 后 `Recycle(handle)`：acquireStroke空（5709–5717）、modeler.Reset失败（5807–5819）、modeler.Update(kDown)失败（5867–5879）。
- `ContactInput.cpp::FindProducing:473–491` 只查同key当前Producing/Quarantined，跳过已结束ConsumerOwned；`PublishCancelled:807–812` 没有精确handle参数。合法producer串行A.Down→A.Up→B.Down后，consumer迟到处理A的初始化失败会Cancel仍Producing的B，再Recycle A，非相同record的generation可都为1。
- 故障前提是这些初始化失败发生，不应宣布所有正常输入必然失效或用户截图唯一根因。无需靠并发RTS writer、降低必要输入样本或修改modeler默认参数制造红证。

## 红测提取必须真正保持旧逻辑

阶段1只提取目前三段相同的“复制当前已捕获的Down→phase Cancelled→按原key PublishCancelled→Recycle旧handle”；实际三个失败分支都调用该helper。

实现时不得先把红helper改成Discard、提前增加会屏蔽同key问题的generation/no-op修补，或者只在测试里保留一份旧取消算法。若helper读取 `handle.record->DownSnapshot()`，须保持与initializeStroke已核同代次后复制的不可变Down一致；不要改成latest/终态快照。也可在红提取阶段显式传入原Down，绿色再收窄到只需handle；以最小实际diff为准。

旧helper的Producing分支会伪Cancel并回收，I02应自然红；ConsumerOwned A与Producing B错目标由I01直接检出。Closing分支在当前F057 `Recycle` 已会变ClosingDiscarded，因此I03可能旧版也通过，**不要求所有新增case红**，只要求能确证此身份/生命周期错误的关键断言红→绿。

## 绿色helper与runtime清理核对

最终内部helper只调用现有 `input.DiscardUntilTerminal(handle)`，不按key重找、伪造Up/Cancel或读新record字段；三调用者删除后续无条件Recycle，不再形成第二份回收逻辑。

| 旧handle状态 | 现有Discard处理 | 初始化失败后的正确结果 |
| --- | --- | --- |
| Producing | 精确CAS→Quarantined | consumer不再读/画；物理Up/Cancel仍由producer释放一次，不提前回收正在按下的route |
| Closing | 精确CAS→ClosingDiscarded | consumer有界返回；真实Close完成后producer唯一释放 |
| ConsumerOwned | 调已有Recycle→Free | 释放旧接触，不能改同key当前新接触 |
| 已Discard/Quarantined | 有界no-op | 不重复计数或释放 |
| stale地址代次 | 精确generation校验拒绝 | 不影响该地址的新generation |

`ContactInput.cpp:837–882` 已提供这些合同。Quarantined Move在 `PublishMove:766–795` 的Producing检查失败，不触发新模型输入；真实Up/Cancel通过FindProducing包含Quarantined和Close释放。因此不需要补一套取消算法，也不需要额外Recycle(Producing)。

- acquireStroke返回空时没有已取得runtime，保留原return false，不虚构释放。
- Reset/Update失败仍逐句保留错误报告、`runtime->cancelled=true`、`handBackSpeedEraserController(*runtime)`、清handle、`inUse=false`、return false；不把旧runtime追加active/history或制作成功光标/笔迹提交。
- 成功初始化、模型频率、pressure/width/color/cursor、history/保存/GPU/线程所有权无改动。仅失败接触拒收，不能以“优化”丢弃正常B的已接受样本。

## probe与独立验证范围

当前 `IdtMain.cpp:706` 的产品 `--draw3-parked-desktop-exit-test` 真正调用 `RunParkedDesktopExitAutoSaveTest`（Controller.cpp:3338），其末尾到3730行已有CPU/Coordinator真实helper测试。可在同module加内部测试并由该真实入口累计返回失败数，无需修改Main/工程/public module接口。

I01必须同时比较record地址和代次、核B Move/真实Up不被Cancel、A旧handle失效及槽位再用的C不受stale A影响；I02核Producing占槽直至物理终态、重复拒收无重复回收；I03用既有真实Close CAS后的pause hook，producer/consumer任何失败出口均先resume再join，不能SuspendThread、detach或让pause对象先销毁。

这验证的是**三个真实失败调用者共用的生产拒收helper**。没有实际触发acquire/modeler的每一个失败条件就不能写“三个完整失败分支动态通过”；真实modeler/runtime/speed-eraser故障注入可在后续最小单元补强。不能在测试复制正确Discard算法后称生产通过。

## 普通页边界必须继续独立验收

`sealPresentationContacts:6307–6350` 基于AdmissionRevision，使用最后已消费实点调用completeModelUp，再由烘干/history路径（10345–10358）精确Discard。该动作不是当前初始化失败helper，Clear在相同Desktop也不必改变AdmissionRevision。

设计正确地把真Controller::Run/PPT admission/成功Present/旧页history或UInk/B页无旧笔/producer暂停期间命令进度交给独立隐藏Host fixture。**本helper绿灯不证明正常PPT页切换Closing已通过**；不为该fixture开放关闭的导航/白板入口，不通过latest-only省略必要Down/Move/Up/Cancel或让等待问题消失。Host/header/module跨界接口由主会话单独冻结，Controller worker不能顺手越权修改。

AbortUnqueuedDown与Reset前RTS callback静止的证据仍按研究报告分别调查，不能从public Coordinator任意并发推断真实串行RTS writer必死锁，也不能从此helper推断整个Host换代安全。Win7/真实Touch/Pen/用户现场继续独立保留。

## 验证

本轮只读源码/设计/context与CLI真实登记；无产品修补。Lint/TypeCheck/Build/Tests/GUI：未运行。GREEN只允许受控红测实施，不等于实现已完成、动态PASS或发布就绪。
