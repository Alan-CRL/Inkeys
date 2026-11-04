# 2026-11-01 页面准备等待提示的人工样本独立验收

## 结论与范围

用户已明确回复：**这次3→4三点正常保留和清除。** 新样本的默认关闭数值诊断也确认，已有Pending页面准备阶段实际生成两按钮三点，经同一attempt完整像素事务和需要显示的native回执提交，没有借用已结束命令的epoch或方向。

因此，本轮“已观测Pending期间缺少等待提示”的窄修复，在指定文件本次人工复测中通过。此结论不扩大为所有Office/WPS、冷暖多轮或所有输入场景均已验收，也不表示实际翻页耗时已经缩短。本轮不新增代码、不追加人工矩阵。

只读复核 `Build/Validation/ppt-navigation-20261101-152235-157-66d7a179` 的manifest、PptWait、PptPerf、ShutdownTrace与nativeGate，仅新增本报告；未修改产品/测试/主会话文档，未构建、操作GUI/Office或提交。

## 产物与诊断完整性

- PID77072，status=`exited`，exit_code=0，capture_errors=[]。
- EXE SHA256=`94502B2B4A66BDB810542CF9EDFF5DC450F83611CA62671780075BB5EF5FDF73`。
- DLL与内嵌DLL同为 `1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3`，managed产物保持。
- PptWait实际1744条record，ordinal完整1..1744。stderr第8286行summary：sealed=1，seen=retained=1744，dropped=invalid=output_errors=0，capacity=16384。
- Preparing1024标记共1008条，**全部epoch=0、direction=0**，没有套旧请求。
- 准备阶段154次render attempt中，107条scene-draw实际drawn，全部dots=6/branches=2，对应107条pixels submitted；另47条not-drawn及47条no-pixels，不能计作真实画点。
- 准备阶段native receipt中143 committed、24 candidate-stale。逐非零attempt独立核对，95个新像素/需要显示的成功回执，均能唯一匹配相同PID、session/layout、surface、publication/model revision、mode、attempt及pixel revision的draw/pixels，时间顺序正确，dots6/branches2，没有身份冲突。
- 另48个committed为attempt0、flags1034，不带ShouldShow64或PixelsSubmitted32，是隐藏/复用事务，不为画点背书；stale也不算成功。

## 首个3→4 Pending窗口与右下提交链

同一native日志 `native/idt1793517756345.log` 中，首个3→4窗口如下。日志文本毫秒时刻用于与PptWait的QPC/UTC锚对齐阶段，不把不同编号领域强行合并。

| 边界 | 北京时间 | 原始证据 |
| --- | --- | --- |
| Next epoch3入队 | 15:22:49.476 | native第93行 |
| 命令Complete | 15:22:49.500 | native第99行 |
| raw3/descriptor4失配，trustedTarget/Page=false | 15:22:49.961 | native第103行；已有真实准备事实 |
| 准备快照 | 15:22:49.962074 | stderr第6647行，generic epoch0/direction0，publication43 |
| 右下实际双按钮画点 | 15:22:49.976605 | stderr第6655行，attempt10，dots6/branches2 |
| 完整新像素事务成功 | 15:22:49.977615 | stderr第6656行，attempt10/pixel_revision146 |
| 同attempt需要显示的native回执成功 | 15:22:49.990131 | stderr第6663行，candidate289/frame148/scene_serial148 |
| raw4/descriptor4一致、target revision2接受 | 15:22:51.531 | native第104-105行 |
| document_ready revision2 | 15:22:51.540 | native第107行 |
| page_ui_ready revision2 | 15:22:51.576 | native第109行 |

右下surface1的draw、pixels、receipt都为session1/epoch0/direction0、layoutSession1/layoutEpoch2、publication43/model_revision18、attempt10，pixels/receipt的pixel_revision均146。准备快照QPC=1155269892385；右下draw=1155270037694，pixels=1155270047794，receipt=1155270172959，分别在快照后 **14.5309ms、15.5409ms、28.0574ms**。

- draw flags1162=Preparing1024+PositionActive2+Visible8+DrawInvoked128。
- pixels flags1194再含PixelsSubmitted32。
- receipt flags1130=Preparing1024+PositionActive2+Visible8+PixelsSubmitted32+ShouldShow64。

该链证明右下通用准备提示进入实际绘制、完整GetDC/ULW/ReleaseDC/EndDraw和原窗口验真提交链；不是将旧epoch3的命令三点延长或把旧成功记录匹配过来。左下surface0同样有attempt9/pixel_revision73/candidate288，15:22:49.989787回执成功。

PptWait model_revision18和Draw3/nativeGate target revision2属于不同编号领域；本报告按同一session的唯一3→4准备时间窗关联两条链，不把编号相等作为依据。后续准备也可由键盘等来源触发，1008条Preparing记录不是1008个按钮请求。

## 清除与输入安全的证据边界

用户已观察到本版三点正常保留和清除，这是本次实机可见性验收的直接事实。nativeGate又确认原3→4可信目标、document ready、page UI ready仍按原链完成，没有为了提示提前授予画布输入权限。

Preparing=false且没有command epoch之后，原generic采样门会停止保留该帧。本样本最后一次准备快照仍约在15:22:51.540312；**精确去点像素帧与其QPC不能仅由PptWait尾部倒推**，缺失属于unknown，不填0、不借后来ready记录伪造一条generic去点提交。软件事务也不是DWM/光学测量。

本修复按现有PageStateMode Pending投影，只覆盖已有准备事实。命令Complete到首个Pending之间本窗口约462ms，没有被伪造成页面忙；本次不宣称逐帧连续覆盖命令到所有input gate开放的每个时刻。pagePreparing=false不等于inputReady，原身份/Present/UI ACK与输入门保持。

## 实际延迟仍须单列

原始PptPerf解析为6145事件、3045span、13个native请求，summary complete、0issues，与原始计数一致：

- Next共8次，最大epoch9 **42.3125ms**，owner41.3900ms。
- Previous共5次，最大epoch6/native span31 **1012.3007ms**，mailbox855.8518ms、owner155.5009ms。

独立按原始QPC重算该Previous总时长和邮箱驻留，均与summary一致。邮箱区间与同owner维护的重叠为：

| 阶段 | 完整阶段 ms | 与邮箱等待重叠 ms |
| --- | ---: | ---: |
| maintain-binding span686 | 859.4889 | 74.7354 |
| 其中ROT span687 | 859.4160 | 74.6734 |
| Observe span691 | 781.0943 | 781.0943 |

父项维护剩余74.7354 + Observe781.0943 = **855.8297ms**，与邮箱855.8518ms只差0.0221ms。严格ROT子项少0.0620ms，为父项其它维护开销，不能父子重复累加。

这仍是明确的秒级Previous长尾。Next的几十毫秒仅为按钮业务请求时间，不能替代随后约秒级页面准备/输入恢复耗时；本轮修复的是用户能看到的等待反馈，不是宣称Office/owner/Observe已变快。13次样本也不足以构成新的冷暖完整性能矩阵，本次不再追加该矩阵。

## 正常退出复核

- ShutdownTrace共90事件、45span，全部begin/end闭合，零缺失。
- 接纳关闭到main-shutdown结束 **1001.8980ms**；main-shutdown自身906.9592ms。
- manifest ended_utc距关闭接纳约3627.056ms，包含main scope之后的进程/父采集流排空等收尾，不能作为精确死亡tick，也不能用main scope结束代替进程退出证据；本次exit0独立确认正常退出。
- 最终managed finally6.1634ms、FullCleanup(final)6.0346ms、event disconnect4.4968ms，均闭合。
- window/presentation/application释放scope分别0.0053/1.0229/0.1525ms，均返回；scope返回不等于实际RCW计数为零。
- 整个样本没有cleanup-event-retire、shutdown-retired-event-retry或shutdown-retired-event-wait。最终retired外层scope0.0089ms且零retry，与本次owner退出时没有待重试CP lease的原控制流一致；不能据此反推前次第三轮retry后的未知状态，亦不能证明所有Office资源全局归零。

本次正常退出没有重现旧E1430016，旧偶发故障仍不能由成功样本倒推根因。本报告不以scope闭合断言PowerPoint已退出或下一文稿必能打开。

## 本轮验收落点

指定PPT本次3→4的等待反馈，已有“用户确认正常保留/清除”与“真实Pending通用三点进入同attempt软件提交链”的一致证据，可记为本窄修复人工复测通过。真实输入门照旧，性能长尾和旧偶发退出故障保持独立边界；本次不扩范围、不新改代码、不追加人工测试矩阵。
