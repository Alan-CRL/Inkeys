# 2026-11-01 页面准备等待提示的最小修复

## 证据与目标

最新样本144216-578-e2c353b3正确EXE AECD45C3...、exit0、3834 PptWait记录零掉样。35次Publish均接受，带三点的窗口软件提交已证。用户进一步明确：第3页点Next会出现三点，约0.5秒后消失，画布仍不能新画，再1秒多才显示第4页并恢复画布。

日志中的同类Next epoch5：14:42:51.224入队，51.265命令Complete；51.777 raw3/descriptor4不一致使可信target失效并进入Pending，53.224两者一致，53.233文稿ready，53.261 UI ready。不能把用户近似0.5秒直接映射成该41ms请求，但二者证明相同缺口：命令终态不代表页面呈现终态。

## 行为差距与选择

原PptNavigationWaiting只描述native命令outstanding，Complete即清除。PptInfo早已有真实PageStateMode::Pending，用于raw/descriptor失配/同场busy/目标尚未呈现时保留旧数字并阻断旧回执；该准备状态没有投影到三点。

增加独立纯视觉pagePreparing，来源为同一PublishSnapshot中的pageState.mode==Pending、放映active且visible、同场有效recovery.kind==None。PageControl在PPT页面准备期间两个导航按钮均显示三点（通用页面准备，不猜方向）；原native命令方向等待与完成水位保持原样。InkReady/BasicControl投影false，下一成功提交新页码的同帧去点。此bool不表示最终Draw3 UI ACK已接受，不改变任何输入/ready/命令准入。

不以任意TTL等待可能的换页，不等target改变作为唯一终态，不新建命令意图tracker或改COM协议。Complete→首次Pending（本例约512ms）没有页面忙的可验证事实，本轮不伪造该段；修复覆盖真正Pending阻塞而提示消失的阶段。同页动画/首末页no-op继续原有限命令等待；键盘若触发真实Pending，也可显示准备提示。

## 最小文件与所有权

- UI/Ppt/Ppt.cpp：Snapshot从既有mode/active/visible/同场recovery投影bool；不更改PublishPageState/CanAcknowledge/业务回调顺序。
- UI/PageControl/PageControl.cppm：PptState新增默认false的纯视觉bool及ArePptStatesEquivalent比较；不改等待tracker水位/命令gate。
- UI/PageControl/PageControl.cpp：BuildPageContent通用准备OR原方向等待；白板隔离、原recovery文案/重试优先。扩展原真实离屏fixture验证同页Pending→COM Complete仍有三点→新页InkReady清除、BasicControl/Recovery/白板边界及partial/full。
- 必要的既有headless/page_control_tests.cpp：状态比较/等待水位/ready不变回归。生产投影应由实际源核对，不为测试新增GUI入口/通用架构。
- 如需让已有PptWait覆盖新的通用准备阶段，只在Ppt.cpp/PageControl.cpp及PptWaitingDiagnostics.h增加明确Preparing flag；epoch0表示通用页面准备，不能归到某个旧命令或借用旧方向。沿用固定槽/停产flush，无新增字段/输出线程/热路径IO。DrawScope可由非零命令epoch或明确准备flag激活；generic两按钮的branches2/dots6不冒充单按钮三点。原native请求分析不受影响。

本轮不改IdtPlug-in业务、安全门、PptCOM.cs、dynamic/确定性释放、接口/TLB、超时/调度、raw/descriptor身份校验、颜色/尺寸/动画/i18n。

## 回归与质量门

1. 同页3/N、命令已Complete且tracker空，Pending仍显示双按钮三点，旧数字保留；InkReady4/N新帧清除，Partial=Full及三倍率/四surface真实BGRA；命令方向等待单独仍仅对应方向。
2. BasicControl/HWND0不等待不存在的墨迹ACK；Recovery保留原错误/重试文案；白板/隐藏/退出/换场不残留PPT三点。同页动画和no-op不触发新永久等待。
3. 原Complete-before-Publish/跨场/完成水位不变，新bool不能触发或完成Draw3/PageControl ready；晚旧ACK不能清除后来Pending。
4. 如扩展数值采样，默认off零新读钟/槽，明确通用epoch0与flags，不假配旧request；同attempt/原pending回执、nested/null、并发/overflow/LastError及真实启停BGRA等价保持。
5. 独立trellis-check，完整InkeysRepo.sln Debug|ARM64/ARM64原生MSBuild/900秒预算；PPT+PageControl无窗口及真实离屏入口，核对本轮managed源/DLL/TLB完全未变、内嵌和PrepareOnly配置SHA。

不运行GUI/Office，不commit/push，不归档。验证后交用户用原文件短测，明确本轮修的是已证实的页面准备提示缺口，COM队列长尾仍单列。
