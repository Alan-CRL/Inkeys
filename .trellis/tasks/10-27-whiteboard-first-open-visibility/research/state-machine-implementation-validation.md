# 三工作区统一交接：实施与验证记录

## 范围和授权

用户已批准最终设计并开始实施，范围为白板与Desktop／Presentation／Whiteboard切换的直接相关遗留问题。沿用当前Trellis任务；没有本轮commit/push授权。GUI、Computer Use、现场窗口/进程采样均禁止；只能静态、编译及离屏/CLI验证。

## 本轮解决的耦合

1. 原先目标、白板阶段、主栏缓存与原生窗口状态分别推进，容易在物理交接未完成时让UI先进入另一套状态。改为单一Desired／Committed／Pending协调结果，稳定主栏及工具读取Committed，目标画面可以按Pending准备。
2. 原生呈现完成曾与全局置顶、全局连续、前台授权等独立结果混合。场景窗口Prepare／Complete只确认必要窗口、输出目标和相对顺序；焦点请求在完成后至多异步一次，Shell继续独立发送最新绝对状态。
3. 过渡及实际Selection的输入门不一致，物理鼠标也可能经过RTS。新Down的Host过渡原因与已有门取OR；实际Selection在Controller拒绝新Down，终态和ControlWake仍继续，不能等第一笔收尾。
4. W→P的页码UI-ready若等待稳定场景先变P会产生循环等待。Pending允许目标页码先真实呈现，业务命中继续使用既有完整身份和门。
5. 取消、最小化、恢复及迟到背景回执必须保留真实资源责任。窗口回执核对准备时安全覆盖序号与HWND generation；白板背景区分像素准备和真实显示／隐藏，释放共享Freeze后迟到inactive不能再隐藏桌面内容。

## 验证状态

本记录在验证前建立。最终编译、离屏结果及独立审阅结论待补；此段不是通过声明。

日志目录：`Build/WhiteboardStateMachineValidation/`（ignored）。源编码/BOM/换行与HEAD原始格式比对；用户CSO和旧设置任务固定导出图像都建立字节及时间戳保护。

## 必须保留的限制

离屏测试不能证明真实USER32/DWM/Shell、任务栏自动让位、窗口焦点、多屏缩放以及Office/WPS实机表现。没有Windows7/8/8.1/10/11逐平台GUI记录，不能称这些平台已实测通过。失败补偿的私有Service和系统回调有源码复核，模拟操作次序测试不能冒充这些系统调用的动态覆盖。

## 原生恢复的明确边界

标准[ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)恢复会显示窗口；采用SW_SHOWNOACTIVATE避免恢复阶段主动激活。原生Prepare解除iconic可能短暂显示既有锚点，随后立即隐藏并等待真实目标完成。过渡新Down仍关闭，不采用未经保证的WS_MINIMIZE/placement技巧。[Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)说明owned窗口也可能随owner恢复，不能承诺这段系统调用跨窗口零瞬态。此边界需要人工观察，离屏不证明视觉无闪烁。

## 首轮编译与复审反馈

- 第一次完整`InkeysRepo.sln Debug|ARM64`使用ARM64原生MSBuild，168.63秒结束，退出码1；第一个有意义的错误为新增State生产probe把`shared_ptr<const PresentationTarget>`当optional调用emplace并写const成员。属于本轮测试源码类型错误，不属于SDK/runner问题。实现者改为可写make_shared填充后赋给只读共享指针。
- 同轮独立复审确定并修补安全退场的工具owner迁移、系统意图清理的guard ABA、最终Primary/BasicControl会话身份复核。不会通过清Host fault或伪造ready来完成交接。
- 原始诊断保存在`Build/WhiteboardStateMachineValidation/attempt-1-compile-diagnostic/`。修后需要重新冻结源快照、完整Solution构建和CLI，不能把首次已生成的其他二进制当整个修改验证成功。

## 最终复核与验证结果

- 独立`trellis-check`复核协调器、Host输入门、Window/Whiteboard背景与UI投影，没有发现其他确定阻断。复核发现Freeze在非白板模式下也吞掉系统最小化/恢复命令；`Window.cppm`现在要求白板活动才接管，`window_tests.cpp`增加非白板Freeze回归断言。该补丁尚未通过编译或执行。
- PPT焦点离屏测试最初失败的三个断言源于fixture只发出白板返回请求、没有模拟协调器提交Presentation，却立即期待PPT输入重新开放。`IdtPlug-in.cpp`现使用`WorkspaceSceneScope`建立并提交Presentation；此测试夹具修正尚未用新二进制重跑。
- 变更前构建的二进制运行`--draw3-control-fence-test`、`--setting-business-cancellation-test`、`--bar-eraser-offscreen-test`和`--draw3-whiteboard-storage-test`均退出0；`InkeysHeadlessTests.exe --no-window`退出0，耗时326.33秒，末尾为`PASS animation correctness`。这些结果不包含后续PPT测试fixture和Freeze回归断言，不能作为最终源码的完整验证。
- 最后一次成功的完整Solution构建在PPT测试fixture和Freeze复审补丁之前。之后对最新源码的完整`InkeysRepo.sln Debug|ARM64`构建两次分别在C++ `MultiToolTask`约10分钟后返回失败，均为0 warning、0 error且没有C++诊断；另一次串行诊断构建明确报`Window.cppm(1,1): error C1902: Program database manager mismatch`。独立HeadlessTests工程同样在`Window.cppm(1,1)`报C1902；把对应`vc143.pdb`/`vc143.idb`移入诊断目录后重试仍报相同C1902。诊断与缓存保存在`attempt-3-serial-diagnostic/`和`headless-project-build/`。这是本机MSVC/PDB构建工具链阻断；没有改项目设置或绕过PDB继续构建。
- 最终源码编码/换行审计覆盖35个源码/spec文件，构建后哈希快照检查通过；`git diff --check`与`Scripts/i18n.ps1 check`通过，en-US和zh-TW均为533/533。9份CSO及旧设置任务10份PNG的内容和时间戳与本轮前一致。
- 因最新源码未能通过构建，状态机PPT fixture和新增Freeze断言仍待构建后运行；任务维持`in_progress`。本轮没有commit/push，也没有启动GUI。真实USER32/DWM/Shell、焦点、任务栏、多显示器和Windows 7/8/8.1/10/11仍需后续人工验收。
- 用户要求重新完整构建后，本轮执行了原生ARM64 MSBuild的`InkeysRepo.sln /t:Rebuild Debug|ARM64 /m:4`。Rebuild在约10分02秒后返回1，主项目`MultiToolTask`失败，日志为`0 Error(s), 0 Warning(s)`，没有定位到源码诊断；PptCOM的Roslyn server连接超时20秒后回退编译并成功导出TLB。日志位于`Build/WhiteboardStateMachineValidation/attempt-4-full-rebuild/`。
- Rebuild没有生成新的`Inkeys.exe`（旧`InkeysHeadlessTests.exe`时间戳仍为2026-10-27 20:17），所以没有用旧二进制重试CLI，避免把旧产物结果当作最新源码验证。9份CSO在Rebuild后恢复并再次核对哈希/时间戳一致。

## 用户重建产物后的复验

- 用户随后成功生成了新的`Build/ARM64/Debug/Inkeys.exe`（2026-10-27 15:37 UTC，67,207,168字节）。用此产品二进制重新运行`--state-mode-production-test`、`--draw3-control-fence-test`、`--setting-business-cancellation-test`、`--bar-eraser-offscreen-test`、`--draw3-whiteboard-storage-test`，均退出码0且未超时；日志和结果JSON保存在`Build/WhiteboardStateMachineValidation/`。
- `InkeysHeadlessTests.exe --no-window`并未用这次产品二进制重跑。独立`InkeysHeadlessTests.vcxproj Debug|ARM64 /t:Rebuild`在134.67秒后失败，多个模块首行报`C1902: Program database manager mismatch`，构建返回1且未留下`InkeysHeadlessTests.exe`。因此之前14:14 UTC的`--no-window`通过记录仍属于旧二进制，不能代表最新的`window_tests.cpp`已执行通过。此次阻断在本机MSVC/PDB构建链；未改项目配置或尝试绕过。
- 用户重建后核对9份CSO哈希均与保护基线一致，其中7份时间戳已被构建更新；保留当前时间戳。旧设置任务10份PNG的哈希和时间戳全部与基线一致。
- 本次未启动GUI。五项产品级CLI通过，但独立headless目标未能编译，所以最新窗口状态机断言仍未运行；任务不能据此宣称验证全部完成。

## 选择模式退出时的基础 owner 链回归（2026-10-28）

- 用户明确授权一次本机复现，随后提供100%手动路径：选择模式进入白板，点击一次Drawpad背景，再点击主栏Close。未点击背景的空白路径或电脑操控不总能触发，不能替代该手动路径；电脑操控启动均未携带Harness参数。
- 临时生产诊断确认四次失败均在Prepare `apply-band`，receipt仍为Executing。逐角色读回显示owner/band正确且topmost=true，但Freeze、DrawpadPresentation、Drawpad、PPT和Bar全部不满足owned高于owner；原相对helper只修PPT/Bar，没有修基础链，故有限重试不能收敛。现场保存在`Build/WhiteboardExitFailure20261028/`。
- `Window.ZOrderPolicy.h`现在按子到父用NOACTIVATE|NOOWNERZORDER相对插入修复基础链，再执行原PPT保序；不恢复全局TOP或continuous完成门。`window_tests.cpp`覆盖整链反转、外部穿插、topmost=true、Bar>PPT及幂等。临时阶段诊断已移除。
- 修复后电脑操控同路径成功且主栏恢复桌面位置；用户随后多轮手动打开/关闭均无法复现，日志无apply-band/Z-order失败。用户另确认修复前在1秒内让其他窗口获得焦点可恢复，与原生顺序交接故障一致。
- 最终仅含修复/回归/规范的源码已通过完整`InkeysRepo.sln Debug|ARM64`构建和`Inkeys.exe --state-mode-production-test`；首次修复版完整`InkeysHeadlessTests.exe --no-window`退出0并结束于`PASS animation correctness`。
- 最终去诊断二进制再次运行完整`InkeysHeadlessTests.exe --no-window`，退出0、stderr为空并结束于`PASS animation correctness`，日志位于`Build/WhiteboardExitFailure20261028/final-headless.*`。`git diff --check`通过；未commit/push。
