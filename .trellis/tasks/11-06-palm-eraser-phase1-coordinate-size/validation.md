# 验证记录

## 环境与权限
- 工作分支：bugfix/hand-erase；本轮开始基线42f4b9a8。
- Windows 11 ARM64开发机。Visual Studio由vswhere定位；ARM64原生MSBuild版本检查通过（18.11.0.47511）。
- 主产品构建入口InkeysRepo.sln，Debug|ARM64；完整solution保留PptCOM与主程序既有构建关系。
- MSBuild每次在同一PowerShell调用先移除Env:PATH并设置MSBUILDDISABLENODEREUSE=1；构建至少允许5分钟。
- 默认python WindowsApps alias不可用，Trellis脚本使用已有KiCad Python；没有安装或修改环境。
- git默认子模块检查受HiMsg的.git/modules权限限制；主仓库检查使用--ignore-submodules=all，不宣称核查过子模块内部状态。

## 规划与静态检查
- 用户已批准最终规划并开始实施，已执行task.py start。
- 规划工件、manifest文件存在性及注入大小限制校验通过；文件保持CRLF。
- 构建和运行结果待下文记录，不使用已有EXE冒充当前源码验证。

## 实际执行结果
- 初次沙箱内完整构建：19:17:00开始，19:22:01退出，exitCode=1；日志仅空行，未出现编译器诊断，RealtimeStylus.obj仍为原时间。进程检查见MSBuild主进程和多个空闲子节点，未见cl/link或新产物；根因尚未由错误日志确认，不归类为源码编译错误。主进程已自行结束，没有执行终止。
- 代码审查发现并正在修正独立启动边界：Enabled内二次HDC查询失败必须作为WindowMapping初始化失败回传。
- 沙箱外首次构建：19:26:24开始，19:27:03退出，exitCode=1；已正常进入源码编译，第一个有效错误为Draw3.RealtimeStylus.cpp:2692 C2513，局部probe变量near与Windows SDK宏冲突。按局部源码问题修正，不修改项目或SDK。
- 修正后C++编译通过，但首次增量链接发生LNK1000 / IncrBuildImage / C0000005（link.exe 14.44）；只删除已核对的生成缓存Build/ARM64/Debug/Inkeys.ilk，未改项目/源码/工具链版本。Microsoft说明缺少.ilk会执行完整链接：[INCREMENTAL](https://learn.microsoft.com/en-us/cpp/build/reference/incremental-link-incrementally?view=msvc-170)。
- 清缓存后完整solution Debug|ARM64构建exitCode=0，产出Inkeys.exe及InkeysHeadlessTests.exe；存在原有数值转换/局部遮蔽等warning，未作无关清理。
- Palm Headless：152项、0失败；生产RTS probe：59项、0失败（故障注入预期输出一条E_FAIL诊断）；生产Palm probe PASS。
- ARM64产物导入表没有新增GetWindowDpiAwarenessContext/SetThreadDpiAwarenessContext静态导入；源码两者仅GetProcAddress解析。没有当前Win7 x86/x64新产物，故不声称该架构导入检查已执行。
- 首轮沙箱内Headless全量出现StartupPreviewState/ConfigMetadata临时配置写盘失败；同SHA256程序在沙箱外--no-window --startup-preview-only为0失败，确认至少启动预览写盘失败受环境影响。沙箱内全量在300秒截止，未完成；没有把它算为通过。随后同一SHA256的程序在沙箱外完成全量--no-window：19:37:34至19:44:11，exitCode=0，stderr为空，最后PASS animation correctness。该轮包含实际SpeedEraser/Palm及其它Headless回归，无需修改无关配置代码。

## 实机验收边界
本轮没有GUI/Computer Use授权。真实Surface Pro 11、异常设备、多显示器混合DPI及Win7 SP1 + KB2670838 x86/x64运行尚未执行，CPU/构建结果不得替代这些结果。

## Git
没有创建commit、push或修改Git/SSH签名配置。

## 最终通过结果
| 验证 | 结果 |
| --- | --- |
| InkeysRepo.sln Debug / ARM64，原生ARM64 MSBuild | exit 0；完整solution |
| InkeysHeadlessTests --no-window --palm-eraser-only | 152项，0失败 |
| Inkeys --draw3-rts-coordinate-test | 59项，0失败；故障注入E_FAIL为预期 |
| Inkeys --draw3-palm-eraser-test | PASS；CPU/no GPU/no HWND |
| InkeysHeadlessTests --no-window（沙箱外） | exit 0；PASS animation correctness；stderr为空 |
| 新增可选DPI API导入检查（当前ARM64产物） | 无静态导入；源码GetProcAddress动态解析 |
| 代码/规范审查、git diff --check、原编码/BOM/CRLF | 通过 |

产物SHA256：
- Inkeys.exe：7AF60158D01D6CE010BC30F8932E2C69D4F56A812D5C96CE88D1345F55E26928
- InkeysHeadlessTests.exe：8DEF299F61EF2DB92C45BF23AB66565A40A82CAA46E8733B51D87121768D7990

原始命令、退出码、时间、双流日志和imports输出位于validation/。本轮代码与自动化验收完成；任务保留in_progress供上述真实设备验收，不自动归档，不创建提交。

## 2026-11-07 范围收紧后复验

- 产品 RTS/Host/RtsStartup/IdtMain 恢复 42f4b9a8，仅保留面积 ÷C（PrepareAreaTransforms 传 contactScale）与 Palm sqrt(W×H)；上文 `--draw3-rts-coordinate-test` 及 59 项 RTS probe 已随撤回失效。
- InkeysRepo.sln Debug|ARM64 原生 MSBuild：exit 0。
- InkeysHeadlessTests --no-window --palm-eraser-only：152 项 0 失败；--no-window 全量：exit 0，stderr 为空，SpeedEraser failures=0，PASS animation correctness。
- Inkeys --draw3-palm-eraser-test：PASS。
- RtsSharedScaleCompatibilityForTesting 仅在 DRAW3_TESTING 构建中编译，本轮未执行；实机书写流畅度、Surface/异常机接触尺寸、Win7 仍未验证。
