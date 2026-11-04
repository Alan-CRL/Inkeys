# 2026-11-01 等待链路实测与用户澄清

## 采集与完整性

目录Build/Validation/ppt-navigation-20261101-144216-578-e2c353b3，PID89380，EXE AECD45C3...，DLL/内嵌均1061DDF5...，正确本轮版本。manifest exit0/capture_errors空。PptWait sealed1、capacity16384、seen=retained=3834、dropped=invalid=output_errors=0；PptPerf 7309事件/3564span/35请求/0issues/0incomplete。解析器complete仅代表协议完整，不是性能或视觉通过。

## 等待不是没画，而是命令后提前消失

用户最新澄清：第3页点Next后有三点，约0.5秒消失，画布仍不能新画；再1秒多页码才3→4且画布开放。此前“不可见”描述实际指真正页面准备阻塞阶段没有三点，不再沿用“整个请求没有画点”的假设。

35次publish全accepted；813次drawn均branches1/dots3，812次pixels submitted。独立复核801个committed中745个为非零同attempt新像素显示链，其余56个attempt0/flags14为隐藏复用；42个stale排除。最长Previous epoch6的右下surface1首次draw在Publish后71.9182ms、pixels100.4267ms、同attempt35/pixel_revision421/candidate805的receipt103.6843ms，flags110含Waiting/Visible/PixelsSubmitted/ShouldShow；比Complete早972.1256ms。证明软件链，不把它当光学可见或指责用户看漏。

后续缺口由原生日志直接确认：Next epoch5 14:42:51.224 enqueue→51.265 Complete；51.777 raw3/descriptor4失配→53.224两者4一致→53.233 document_ready→53.261 page_ui_ready（target6）。同类epoch3为46.826 Complete→47.309失配→48.778 UI ready。命令后约2秒才就绪；不能把用户近似0.5秒映射成某一具体41ms请求。

原命令等待仅随native outstanding，CompleteRequest释放时清current。PptInfo已在失配/未呈现阶段保留旧数字并发布真实PageStateMode.Pending，但PageControl仅消费命令等待，所以实际页面准备缺提示。画布冻结与新页码延迟是原身份/Present/UI ACK安全链，应保留。

## 最小修复决定

按page-preparation-wait-fix-plan-20261101.md独立投影已有Pending为纯视觉pagePreparing：同场Active/Visible且无recovery时，两个PPT导航按钮显示通用三点；原命令方向等待与Complete水位不变。InkReady/BasicControl与新页码成功显示同帧去点，不用该bool开放输入。

不追踪无证据的Complete→首次Pending约512ms，不加soft TTL/意图tracker/新COM协议；同页动画/no-op没有Pending就仍按原命令终态结束，避免永久点。Recovery/白板/隐藏及BasicControl原边界保留。研究见navigation-wait-visual-boundary-20261101.md，独立数据见navigation-wait-trace-independent-results-20261101.md。

## 性能与退出边界

25次Next最小14.418ms、中位391.857ms、最大908.396ms；10次Previous最小112.051ms、中位527.592ms、最大1075.947ms，后者mailbox931.2541ms/owner143.8306ms。节奏/页面构成非受控对照，不能与上一轮1.64秒直接宣称稳定改善；命令之后的约2秒页面准备也不能被native-request时长掩盖。

本轮Inkeys exit0；47个ShutdownTrace span均闭合，其中额外4个是早期两次service/reset，退出相关43个。接纳退出到main结束896.301ms。最终managed finally111.4314ms，进入事件retire并执行3轮retry/2次wait；最后PendingCount未记录，是否已清零未知。不能据exit0或scope闭合宣称无COM残留，也不能反推一定泄漏。旧15秒兜底未复现，根因仍未定位；本轮先不扩改COM清理。

本次采样分析不操作GUI；实现仅后续窄UI修复，不归档或提交。性能/真实PowerPoint退出和下一文稿未验证部分继续独立保留。
