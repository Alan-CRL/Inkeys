# 实施与验证计划

## 门禁

- [x] 分支/HEAD/原工作区指纹；脚本创建父任务子任务；读取实际workflow、agent与spec入口。
- [x] 主会话基线完整Solution Debug|ARM64 + headless --no-window + --state-mode-production-test（research/*.result.json）。
- [x] research、PRD/design/本计划自检，真实implement/check JSONL，独立设计review，修正后task.py start；用户已批准实施，不重复询问。

## 所有权与阶段

1. Core implement代理独占 Business/PenWidthPolicy.hpp、IdtState.h/.cpp、IdtConfiguration.h/.cpp、相关策略/配置/状态测试。先发布精确接口供UI/Draw3使用；恢复默认/优先级/迁移安全补生产测试。不得碰IdtMain、Bar、Draw3、工程文件。
2. UI implement代理独占 Bar.Layout/State/Interaction/RenderLoop/Metrics/I18nFormat等必要Bar文件、Setting相关必要入口、三语言源、UI测试。依赖core共享接口；不动State/config/Draw3/工程。连续slider、FineDial identity/cancel/revision与目标预览。
3. Draw3 implement代理独占 Draw3.Bridge.h、Host.h/.cpp、WindowControl.cppm/.cpp、DrawingController.cpp与Draw3 tests；基宽浮点转换、Down锁定、Host显示callback。不动既有dirty Win7诊断/presenter文件。
4. 主会话独占 IdtMain.cpp启动/回调连接、vcxproj登记、完整构建/集成与验收；按需求协调public接口。不并发改状态/布局大文件。不让子代理启动完整构建、递归implement/check或归档。
5. 分阶段可用headless/state/config测试与差异检查；全部集成后独立trellis-check覆盖完整scope，修补相关项并重测。
6. 更新规范、任务证据/人工清单与journal（正式no-commit入口）；保留in_progress直到必需门禁明确完成。不提交/归档父或别人的任务。

## 精确验证入口

所有命令从仓库根，research/run_verification.py记录argv/cwd/exit/耗时/双流，单命令900秒上限。

原生MSBuild由vswhere -latest -products * -property installationPath定位，再MSBuild/Current/Bin/arm64/MSBuild.exe；同PowerShell invocation先Remove-Item Env:PATH -ErrorAction SilentlyContinue及$env:MSBUILDDISABLENODEREUSE='1'，然后完整 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false /v:minimal。

- Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window。
- Build/ARM64/Debug/Inkeys.exe --state-mode-production-test；现有/新增早退CPU粗细/配置生产入口按实际实现登记。
- Build/ARM64/Debug/Inkeys.exe --config-boundary-test（确认隔离配置且无产品GUI）。
- PptCOM.Tests/bin/Release/PptCOM.Tests.exe（需要时）。
- pwsh -NoProfile -File Scripts/i18n.ps1 sync；check；git diff --check。
- Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test 为真实无HWND离屏测试，可运行；Draw3 GPU早退/纯CPU测试依现有研究列表。--draw3-hidden-test内部会Visible=1，不在当前无GUI授权下运行，用生产无窗口接缝和源码确认覆盖。

补测试调用真实policy、Json codec、State setters、CanvasDiameter seam和Bar helpers，不复制公式；矩阵与非法值、identity碰撞、设备变化/active stroke、startup/记忆、幂等/失败和UI cancel详见PRD。完整build仅主会话运行。头文件纯函数/模块代码无独立lint工具时明确编译验证类型。

## 回退点与报告

每阶段检查git diff只含必要变化，BOM/EOL不变。配置保留legacy/migration信息；故障先定位首个有效错误，区分源码/SDK/PATH/link工具链，不升级或绕过项目。回退只反向应用本次文件delta，不覆盖baseline.json中的其它改动。

最终research/verification.md列精确命令/结果、实现/自动运行/源码确认/人工四类；触摸/FineDial手感、PPT黑边、混合DPI换屏/活动笔、Win7渲染人工验收。缺设备不放弃可执行测试，也不声称完整产品体验通过。

## 本轮交付状态

- [x] 设计review通过后start，三领域真实implement worker与独立check完成。
- [x] 生产代码与工程集成，规范更新；最终自动门禁通过，32次精确命令含原失败日志见research/verification.md。
- [x] 原有41个工作区文件指纹保留，BOM/CRLF和diff检查通过。
- [x] 用户未授权commit，正式no-commit journal记录；未push/PR/release/归档。
- [ ] 真实touch/PPT/mixedDPI/Win7及其它架构人工验收，清单research/manual-acceptance.md。任务保持in_progress。

## 本轮增量实施

本轮用户要求优先，见research/followup-scope.md；覆盖原≈DIP显示与启动偏好恢复合同。沿用当前任务不归档，主会话统一构建，独立审查后交付，不自动commit。

### 本轮追加结果

代码、规范与独立修补完成；最终完整ARM64构建、headless/width/state/config/Draw3/离屏Bar/i18n/diff均通过，见research/followup-verification.md。任务保持in_progress，当前触摸实机体验待人工，本轮未commit/push/归档。
