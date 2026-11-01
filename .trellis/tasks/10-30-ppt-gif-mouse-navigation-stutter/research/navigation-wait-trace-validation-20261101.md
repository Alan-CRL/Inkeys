# 2026-11-01 等待链路诊断准备验证

## 本轮结论

用户样本135913-372-bfe291f9正常exit0，主退出清理973.6181ms；上轮正常退出15秒兜底未复现，旧根因未定位。47个导航请求仍有Next1636.5365ms/mailbox1238.9103ms，用户明确三点不可见；性能与等待可见性验收仍失败。详见normal-exit-trace-results及独立复核。

静态研究已排除简单漏比较/漏Request/漏脏区等直接缺陷；现有enqueue不证明UI接受/实际提交。保守Observe拆分涉及命令准入、仅导航FIFO与旧墨迹失效，且单独节省约296ms不足以消除当前长尾，本轮未实施该重构。先按navigation-wait-trace-plan准备可区分等待失效边界的数字诊断。

## 实现边界与独立审查

新增PptWaitingDiagnostics.h；Ppt.cpp发布/完成/清除/生命周期/快照；PageControl.cpp model/scene/Draw/Present/pending receipt；Bar.Scene.cpp实际三点分支计数；IdtMain正常入口Prepare与所有producer真实join后的flush；既有headless helper和真实离屏像素fixture。

固定16384槽，仅INKEYS_PPT_PERF=1分配，render仅数字/POD/原子/TLS，无输出、格式化、分配、新等待或新渲染请求。正常退出后退休prepared入口并输出[PptWait]记录和seen/retained/dropped/invalid/output_errors；强退前无flush属于incomplete。新的private waitingAttempt仅跟随实际提交的completedPixels→pending.pixels，空damage复用0=unknown，不改public模型/ready/业务。

独立trellis-check静态通过：actualDraw→完整GetDC/ULW/ReleaseDC/EndDraw→原pending receipt同attempt，receipt成功在原身份/scene验证后；Bar/PageControl的Display订阅Reset等待activeCalls=0，原ui3/PPT/Setting/Window/Render真正排空，故flush无并发读槽。nested/null TLS屏蔽、prepared退休、失败/恢复与invalid分母正确。像素fixture空BGRA读取风险已局部补短路；无产品行为修复。非零导航epoch取证边界已同步spec。

## 构建与测试

- 旧二进制既有--bar-eraser-offscreen-test基线exit0。新增24组合（四surface×100/150/200%×Previous/Next）的同页等待实际BGRA、另一方向/页码不变、partial=full、complete恢复均通过；因此未定位到确定绘制缺陷。
- 最终完整InkeysRepo.sln Debug|ARM64，动态查找Visual Studio ARM64原生MSBuild，同PowerShell规范化PATH/Path为单一键、保留该进程原搜索值、禁用node复用，900秒预算，允许已证明可用的PDB环境；exit0，17.76秒，0error/10既有warning（hashlib C4267重复及原布局C4244）。初次像素构建用移除PATH的简单形式有非致命pwsh.exe找不到提示；最终保留原值的规范化后不再出现，未改工具链/源码配置。
- 最终--ppt-session-only exit0；默认关闭零读钟/槽、同attempt、nested/null、overflow/invalid、四producer真实join和异常writer/LastError均通过。
- 最终--bar-eraser-offscreen-test exit0，PageControlUI3 failures=0；新增局部recorder明确开启、记录非空，实际branches1/dots3，并与关闭时BGRA一致，避免测试早期入口未Prepare而两次都off。
- 原PptCOM.cs/SlideShowSession.cs/PresentationDescriptor.cs及跟踪DLL/TLB与本轮开始的SHA完全一致；内嵌DLL仍1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3，TLB仍C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606。此轮没有新COM获取/释放，不重复跑未受改动的managed运行矩阵。
- EXE SHA256 AECD45C3CA5618C67798BD6148C87640E83F512B54AE9CC466E5E97887C6D98B；Headless EXE 8AC1011E4330BFA9F7B66F00676387DC8271F16E6FC71082D452FB7DC968B794。EXE内有PptWait标记。
- 最终PrepareOnly exit0，记录144002-128-6d2f5d13，未启动GUI；实际opt/deploy.json和Inkeys/Config/main.json前后SHA均不变。之前143916预检只覆盖deploy，已补正确main路径核查，非人工样本。
- 六源码UTF-8无BOM/CRLF与diff检查通过；日志位于Build/Validation/navigation-wait-trace-20261101。AI仅运行无窗口CLI，没有操控用户桌面、启动应用主窗口或Office；不commit/push、不归档。

## 人工交接与证据边界

新版只增加诊断，不宣称三点或1.23秒已修复。用户原入口打开同一PPT环境，控件3↔4短测到一两次明显等待，然后正常菜单/设置退出Inkeys以统一输出数值记录即可，无需大矩阵。等待可见性和真实软件提交由新样本继续定位；软件提交不是光学可见/PowerPoint自动退出或下一文稿可打开的证明。
