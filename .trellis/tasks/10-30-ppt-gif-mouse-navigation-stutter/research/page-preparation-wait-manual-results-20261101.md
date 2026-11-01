# 2026-11-01 页面准备提示人工验收通过

## 样本和人工结论

采样Build/Validation/ppt-navigation-20261101-152235-157-66d7a179，PID77072，正确修复版EXE94502B2B4A66BDB810542CF9EDFF5DC450F83611CA62671780075BB5EF5FDF73；DLL/内嵌1061DDF5...不变。manifest status=exited、exit_code=0、capture_errors为空。

用户针对“第3→4等待时三点是否在画布冻结准备期间保留，并随新页码显示一起清除”明确回复：这次正常保留和清除。

因此指定PPT/当前Debug ARM64的页面准备提示实机验收通过，停止重复该项人工矩阵。本轮只读分析/记录，未追加产品代码修改、构建或GUI操作，未commit/push。

## 软件证据与边界

PptWait sealed1，capacity16384，seen=retained=1744，dropped=invalid=output_errors=0；Preparing=1024共1008条。实际准备Draw107条均epoch0/direction0、branches2/dots6（两个按钮各三点），其余47条not-drawn/no-pixels不算真实画点。

首个3→4窗口：15:22:49.961 raw3/descriptor4暂不可信，15:22:51.576 page_ui_ready target2。右下surface1 generic attempt10/publication43/model18在49.976605 Draw，49.977615 pixelsSubmitted/pixel146，49.990131同attempt receiptCommitted/candidate289；flags1130含Preparing、Visible、PixelsSubmitted、ShouldShow，没有借用旧命令epoch/方向。数字记录证明软件链；用户反馈提供实际可见/清除确认。

准备false后的generic-only帧可能被现有epoch门过滤，不能由缺记录推断清除或input-ready；本次清除验收来自用户明确反馈及先前真实像素回归，不把PptWait缺行冒充成功。仍保留原target/Draw3 Present/UI ACK/input规则。

## 实际延迟仍在

离线分析exit0：6145事件/3045span/13请求，0issues/0incomplete。8次Next命令24.310–42.3125ms，中位30.701ms；5次Previous133.109–1012.3007ms，中位815.524ms。

最慢Previous epoch6总1012.3007ms、邮箱855.8518ms、owner155.5009ms。维护剩余74.7354ms+完整Observe781.0943ms=855.8297ms，与邮箱差约0.0221ms，仍是owner维护占用导致排队。不能因为提示正常或本轮最大值小于旧轮1.64秒就宣称性能改善；测试页面/节奏不是受控对照。

Next命令几十毫秒不等于整页就绪：上述首次3→4已观测Pending约1.615秒，页面准备仍另计。用户允许在确属PPT/兼容安全代价且无必要时不继续优化；当前不能把完整Observe直接省掉或cached stamp当现场proof。此前partial-proof与最高优先级跳过整个ROT的研究均有明确安全/收益限制，未实施。剩余性能/是否继续更改调度需单独评估，任务不由此自动归档。

## 正常退出

本轮Inkeys exit0，45个ShutdownTrace span均闭合，退出接纳到main清理结束1001.898ms；未出现event-retire/retired-retry/wait记录。上轮有3次CP retirement重试而最后是否清零未知，本轮不以成功退出倒推上轮已解决，也不证明PowerPoint全进程引用清零、已退出或下一文稿已验证。旧15秒兜底未复现，根因仍未定位。

## 当前验收状态

- 页面准备三点保留/清除：PASS（用户+实际同attempt软件链，当前指定PPT/Debug ARM64）。
- 原命令水位/ready/COM和真实像素回归：之前验证通过，本轮未改代码。
- 实际owner排队及端到端页面准备延迟：仍存在，不标修复。
- 其他Office/WPS、Release、PowerPoint真实退出/下一文稿与旧偶发超时：保留原未验边界。
- 当前Trellis任务保持in_progress，不自动commit/push/归档；无需再重复三点手测。
