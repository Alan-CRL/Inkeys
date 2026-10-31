# F-012 启动故障注入删除：独立复审

- 审查基线：H0 `8b156fca59f0337a6afc6d722941666fcf143080`；复审当前未提交的 `Inkeys/IdtMain.cpp` diff。
- 范围：只读检查 H0/当前调用链、`-WarnTry`/`-CrashTry`、相关测试与规范；未构建、未运行 GUI 或故障注入。

## 差异与行为结论

1. 当前 diff 仅删除 H0 `IdtMain.cpp:180–205` 的 `RunStartupPreviewRetryFailureForManualTest` 及其在 PptCOM 初始化后的 H0 `:1579–1583` 调用；其余 `IdtMain.cpp` 代码无改动，`git diff --check -- Inkeys/IdtMain.cpp` 无输出。H0 函数在 Preview 活动且 `INKEYS_STARTUP_PREVIEW_RETRY_FAILURE` **存在**时触发，未检查内容是否为 `1`；首次等待 800 ms 后尝试以 `-WarnTry` 启动新进程，第二次发布人工 fatal `0xD0FE`。该调用没有 `IDT_RELEASE` 门禁。现在 PptCOM 成功后的当前 `IdtMain.cpp:1545–1552` 直接进入自动更新初始化；全仓产品代码搜索不再有该函数、环境变量或 `0xD0FE` 的引用。**结论：F-012 的环境变量触发路径已从当前产品源码删除；仅设置该变量不会造成这条人工重启/fatal 链。**
2. 正式初始化失败路径仍在：当前 `IdtMain.cpp:159–177` 的 `PublishFatalStartupFailure` 保留失败报告、错误帧、提示与 Preview 淡出；COM 失败 `:1164–1172`、PptCOM activation 失败 `:1531–1542`、共享渲染初始化失败 `:1556–1565`、字体初始化失败 `:1590–1601` 仍调用它。`IdtMain.cpp:2090–2104` 仍消费异步启动失败并有界退出。这些是**错误退出**，不能据静态阅读称为自动重试成功。
3. `-WarnTry` 的正式回退仍在。当前 `IdtMain.cpp:367–421` 解析标记并在 Release 的单实例检查中允许它通过；编入工程的 `Inkeys/Inkeys/Window/Window.Legacy.cpp:94–146` 在覆盖层首帧超时且 startup tracker 不能接收失败时，首次以 `-WarnTry` 启动，第二次提示并停止。若 tracker 可接收失败，该分支交给主线程 fatal 处理。`Inkeys/IdtWindow.cpp:73–81` 也有类似旧代码，但 `Inkeys/Inkeys.vcxproj:1171` 将它标为 `<None>`；不能把这份旧代码当作当前生产重试的证据。
4. `-CrashTry` 的正式崩溃标记仍在。当前 `Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp:30–35,330–339,441–484` 安装异常处理器、在二次崩溃/重入时抑制再次处理，并按现有策略启动带 `-CrashTry` 的进程；`IdtMain.cpp:369,421` 解析标记并调用 `CrashHandler::IsSecond(true)`。当前 `IdtMain.cpp:2156` 的主动 `-Restart` 也保留。此次差异没有触及这些代码。静态链路只能证明代码仍在，不能证明新进程创建、单实例交接或恢复成功。
5. `IdtMain.cpp:140–156` 的另一条 `INKEYS_STARTUP_PREVIEW_MANUAL_DELAY` 仅在显式值 `1` 或显式命令行请求时延迟里程碑，不发布失败/重启。这不是 F-012 的残留故障入口。手工传入 `-WarnTry` 仍会跳过 Release 单实例门禁，是已有正式标记的行为，应在崩溃/重启专项中单独核对滥用与交接，不能因本补丁声称整个启动入口没有风险。

## 规范与测试缺口

- `.trellis/spec/native-desktop/startup-preview.md:65,144` 仍明确要求保留 `INKEYS_STARTUP_PREVIEW_RETRY_FAILURE` 和 `RunStartupPreviewRetryFailureForManualTest`，与本次 F-012 删除和用户的发布前清理要求冲突。由规范所有者在确认产品取舍后同步修订；本复审按文件所有权不改规范。
- `InkeysHeadlessTests/startup_preview_state_tests.cpp:391–424` 验证进度颜色的普通重试/fatal 算法，`startup_progress_tests.cpp:93–116` 验证失败冻结；它们不执行 `IdtMain`、`ShellExecuteW` 或实际异常处理。当前没有能用这些测试替代的自动重启端到端证据。
- 建议最小复验：`rg` 确认产品源码中无被删函数、环境变量和 `0xD0FE`；完整 `InkeysRepo.sln` Debug/Release|ARM64 构建以及既有严格无窗口测试；在隔离进程/配置中分别人工验证普通启动（含环境变量不存在、`0`、`1`）、真正窗口首帧失败的 `-WarnTry` 有限重试、真实未处理异常的 `-CrashTry` 二次抑制和用户主动 `-Restart`。后面几项需要真实 GUI/异常注入授权，不得用本次静态复审标 PASS。

## 审查判定

**代码差异可接受，待验证。** 删除范围精确，没有静态证据表明正式错误、窗口超时回退、崩溃或主动重启实现被删除；环境变量人工 fatal 路径无当前源码残留。规范过期需修订，构建与真实启动/重启/恢复仍未验证。
