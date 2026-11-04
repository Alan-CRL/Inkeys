# 2026-11-01 页面准备提示修复验证

## 根因与修复

144216实测与用户澄清确认：命令阶段三点正常，native Complete清除后，raw/descriptor尚可能失配并使画布Pending。Next epoch5示例从Complete51.265到UI ready53.261约2秒，原提示未覆盖后段。原画布门禁必须保持。

已在Ppt同一stateMutex快照、同场recovery过滤后投影pagePreparing（Active/Visible/非零session/Pending/无recovery）。PageControl把真实准备OR入两箭头三点，保留旧页码；InkReady/BasicControl与新页码同帧去点。命令方向tracker、Complete-before-Publish水位和native gate原样；不以新bool作为input-ready，不改COM或raw/descriptor可信校验。

本轮5个功能/测试文件：Ppt.cpp、PageControl.cppm/cpp、PptWaitingDiagnostics.h、page_control_tests.cpp；另Bar.Scene.cpp仅一行旧注释同步，绘制不改。Generic Preparing=1024以真实session、epoch0/direction0采样，不回填旧request；两个按钮可产生branches2/dots6，准备false后generic-only采样可能过滤，缺失属unknown，不当作input-ready。

完整质量审查通过，随后测试枚举引用一处编译错误以既有decltype(frame.recovery.kind)::ReadFailed局部修正，独立复核语义不变。无不相关重构/配置修改，UTF-8无BOM/CRLF和git diff --check通过。

## 构建与无窗口验证

- 实际构建系统MSBuild，完整InkeysRepo.sln Debug|ARM64；动态查找Visual Studio ARM64原生host、同invocation规范化Path/PATH且保留原进程搜索值、禁node复用，900秒预算，使用已证明正常的PDB执行环境。
- 首轮56.73秒失败，仅新测试PptRecovery未限定的C2653/C2065；产品编译有既有类型转换warning，不改无关代码。修正后同solution exit0，6.44秒，0warning/0error；无工具链/SDK/项目配置改动。
- InkeysHeadlessTests.exe --page-control-only：exit0。覆盖命令Complete后Pending、原水位与late Publish、同页/no-op、隐藏/退场/recovery、仅bool变化重绘，原ready/旧ACK拒绝保持。
- InkeysHeadlessTests.exe --ppt-session-only：exit0，既有数字协议/并发/LastError回归保持。
- Inkeys.exe --bar-eraser-offscreen-test：exit0，PageControlUI3 failures=0。四surface×100/150/200%×Previous/Next原24组合扩展真实BGRA生命周期：命令完成后旧3/N Pending仍双三点，4/N同帧去点，BasicControl target0清除，partial=full；明确启用局部recorder，generic epoch0/direction0、branches2/dots6且采样开关不改变BGRA。
- PptCOM.cs/SlideShowSession.cs/PresentationDescriptor.cs和跟踪DLL/TLB与改前SHA完全一致；内嵌DLL仍1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3，TLB仍C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606。本轮无COM获取/释放变化，不额外运行未改动managed运行矩阵。
- EXE SHA256 94502B2B4A66BDB810542CF9EDFF5DC450F83611CA62671780075BB5EF5FDF73；Headless EXE 628D0697D23CEFE1B345ABA403A15F5EBB163C0B332C855F3258E93AE6F1675A。
- PrepareOnly通过，记录151749-906-9beeddf9（仅prepared-only，不是人工样本）；opt/deploy.json和实际Inkeys/Config/main.json SHA前后均相同，无GUI。
- 所有命令日志位于Build/Validation/page-preparation-wait-fix-20261101。AI没有启动应用主窗口、Office或操控桌面，未commit/push或归档。

## 实机边界与下一步

本次修复已确认的Pending阶段提示缺口，不缩短真实COM/Observe/descriptor延迟，不覆盖Complete到首次Pending的无证据区间。三点清除随新页码成功显示，不表示所有input条件已在同一CPU时刻开放。Recovery/BasicControl/白板原语义保持；CP末次retired是否清零未知、旧15秒超时未定位仍单列，不能借UI修复宣称已解决。

用户原入口同PPT只需再测一次第3→4，观察真正冻结准备阶段两个按钮的三点是否保留，并在新页码显示时清除；完成后正常退出以采集。152235人工样本现已由用户确认正常保留和清除，此项实机通过；实际性能长尾仍保留，任务保持in_progress。详见page-preparation-wait-manual-results-20261101.md。
