# 最终无 GUI 验证（2026-10-22 实屏继续轮次）

任务：10-21-scroll-follow-persistence-regression；基线 `feature/move / 10abcac3a494d638ee1d25ef010ca1a35482bb09`。任务继续 in_progress，不 finish/archive、commit 或 push。

## 本轮结论与边界

当前 ARM64 完整 Headless 回归、x64 完整 Solution、5 项 x64 纯 CLI、16 项 x64 严格坏 argv、两架构 PE 和目标范围格式/XML检查通过。checker 本次没有修改任何产品源码、工程、依赖或工具链，也未运行 GUI、精确 live 参数、输入注入、截图或 Computer Use。

这一轮利用再次锁屏的时间只做无 GUI 验证。主会话随后收到用户手动解锁并将 GUI 单独交给 core；checker 已在所有构建和进程结束后交还所有权，只离线写报告。新 StateMonitoring stale-ACK 谓词的真实 style→系统窗口 ACK、实屏墨迹 A/B 和最终文件链仍由后续独立 GUI 证据判定，本报告不能将其写成通过。

前一轮真实共享 Mag 捕获/20 次启停和空页 Business/系统窗口 ACK 的证据见本任务对应 live-capture/live-product 报告。它们属于各自旧二进制和具体测试范围，不转移成本轮新 ink 用例的通过。

## Findings (fixed)

本次最终离线验证未发现需要修改的新源码问题，未做额外修复。主会话此前授权 core 实施的两项最小生产改动及此次 ACK 修复，分别保持已有报告的证据边界：

- Business RefreshUi 复用原 `UI::Bar::RefreshToolState()` readiness 保护，避免早于 Bar 初始化直接访问文字对象；旧 AV 仍没有精确线程栈，不能由成功重试认定唯一故障指令。
- StateMonitoring 增加运行中真实 ACK 过期的事实判断，不变更 Publish/dirty/一次 bool/250ms 协议。独立精确增量审查见 `research/live-style-ack-check.md`；没有 fake refresh 或放宽工具 ready 断言。
- MTA、私有 GUI fixture 及其安全/输入/清理变更属于 harness；原 shared Mag 初始化/创建窗口/操作线程与 readyPromise 未改为新的模型。

## Verification

### ARM64 完整 --no-window：本次实际重新执行

严格按早退无窗口入口启动一次 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，隐藏子进程并重定向 stdout/stderr，期限 600000ms。新建私有 TEMP/TMP 为 `D:\Project\Inkeys\Repo\Inkeys\Build\Hc2e`，长度恰好 40 字符，满足旧深层 fixture 路径预算；没有改全局 TEMP 或碰用户数据。

- PID：70284；实际 exit **0**；外部耗时 **156914ms（约 2 分 37 秒）**。
- stdout **42296B**，stderr **35B**，后者为 `[ScrollFollowProtocol] failures=0`。
- 原始前缀：`Build/ScrollFollow/live-final-headless-215940296`；对应 `.log`、`-stderr.log`、`-result.json` 保存路径、PID、前后 hash、耗时及真实退出码。
- Tracker 矩阵记录 failures=0；末段还包含原 pen width/codec、eraser/recall、自动收起、快捷键/鼠标 transport、动画等通过。进程完整退出后才启动 x64 MSBuild，不把某个中途 PASS 当整体结果。
- 运行前后 Headless SHA256 均为 `ED614767D28D07A27D60B0197F964097645B321A5229BD6502BA7E7E6727456A`，与上一软件阶段相同。此次是当前磁盘字节重新完整运行，并非声称 Headless 新编入仅在主程序里的 StateMonitoring 变化。

### 主程序 ARM64 类型/链接证据：独立读取 core 原始日志

当前主程序 D6A348E 由 core 唯一 `product-live-style-ack` 全 Solution 构建得到，外部 exit0/15559ms，MSBuild 15.23s、6 个既有 hashlib++ C4267、0 Error；日志 `Build/ScrollFollow/motion-tracker-build-product-live-style-ack*`。checker 已独立核对原始日志和当前磁盘 hash，没有为本轮重复一个无变化 ARM64 build。

### x64 完整 Solution：本次实际执行

复用已有 `Build/ScrollFollow/build-final-cross.ps1 -Platform x64 -Round live-final-offline`。同一 PowerShell invocation 规范单一 Path、`MSBUILDDISABLENODEREUSE=1`；vswhere 定位当前 ARM64 原生 MSBuild；完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=arm64 /m:1 /nr:false`，允许 900000ms。没有单独构建 Inkeys.vcxproj。

- 第一次实际 exit **0**，外部 **27330ms**，MSBuild 日志 **27.02s**；**68 Warning(s)、0 Error(s)**。
- 本次没有源码/环境/链接错误，没有 ILK/PDB 修复、重复构建或配置绕行。之前轮次已知的缓存故障不冒充本次发生的问题。
- Warning 位于原 hashlib++ C4267、未修改 Setting.cpp 的 C4244 及 IdtMain 原数值转换行；未为零警告修改无关源码，未见新增 inl/新 ACK 条件的诊断。
- 原始日志：`Build/ScrollFollow/solution-build-x64-live-final-offline.log`、`-console.log`、空 `-stderr.log`。

### x64 纯 CLI 与严格坏 argv：本次实际执行

使用已审查早退入口，`Start-Process -WindowStyle Hidden`，私有 TEMP，重定向日志，未运行精确 live flag。主程序 protocol/state 与 16 坏参数复用原离线 runner，只把测试架构/私有 TEMP/输出目录参数换为本轮值，不改产品代码。

| CLI | 实际 exit | 外部耗时 |
| --- | ---: | ---: |
| Inkeys --scroll-follow-protocol-test | 0 | 439ms |
| Inkeys --state-mode-production-test | 0 | 232ms |
| Inkeys --bar-view-adjust-config-test | 0 | 302ms |
| Headless --scroll-follow-only | 0 | 572ms |
| Headless --freeze-capture-only | 0 | 2082ms |

16 个坏 argv 覆盖 empty/ink live 参数的 suffix、前置未知、尾随参数、重复、混合 capture/protocol、malformed 组合，全部 exit **2** 且 stdout/stderr **0B**。没有任何单参数精确 live flag 被离线 runner 执行。

原始结果：`Build/ScrollFollow/product-live-stage/offline-x64-216261812/result.json` 及各 stdout/stderr，另外 3 项在 `Build/ScrollFollow/live-final-x64-targets-result.json` 及同前缀各日志。protocol 明确实际 Tick/Frame/Tracker、外部 Capture/Host/ACK 为 seams；freeze-capture-only 明确 no HWND/fake pixels，不能写成正式 Mag 实屏重测。x64 在 Windows 11 ARM64 主机上执行，不是 x64 硬件验收；WOW64 可选功能门不会因为纯测试 bypass 而视为真实可用。

### 两架构 PE/import/平台门

复用 `Build/ScrollFollow/pe-check.ps1`，vswhere 和当前 ARM64 原生 dumpbin 只读取主程序 `/headers /imports /dependents`，分别保存 `Build/ScrollFollow/pe-live-final-arm64-*`、`pe-live-final-x64-*`。

- Machine 为 **AA64/ARM64**、**8664/x64**，目标正确。头部 OS/subsystem 版本 ARM64 **6.02**，x64 **6.00**；这些头值不等于系统运行支持已测。
- MAGNIFICATION.dll 两者均在 delay-load 区，常规依赖区没有新增 OpenCV/FFmpeg DLL 或 WGC 依赖。`RtlGetVersion`、CreateForWindow、SetWindowDisplayAffinity、MagSetImageScalingCallback 均无硬导入；原 Callback 的动态能力查找不因此改为硬依赖。
- 相对于上一软件最终 PE，两架构新增的硬导入函数只有 **DwmGetWindowAttribute、SendInput**，属于既有旧 Win32 接口，用于新专用测试/安全检查；未引入 Windows 10 捕获功能或捕获排除属性。精确清单和差异见 `Build/ScrollFollow/live-final-offline-pe.json`。
- x64 无 WaitOnAddress/api-ms-win-core-synch-l1-2-0.dll 硬导入；ARM64 保留该架构既有运行库同步入口。未改 CRT、SDK 或链接选项掩盖差异，不用 ARM64 表证明 Win7 运行。
- 生产 ScrollFollow::Capability 仍 GetProcAddress 动态查 RtlGetVersion，要求版本 >=6.3 且 IsWow64Process 成功/非 WOW64；运行期 Mag ready/可用性仍另核。该静态门检查不是 Win7/8.1/WOW64 实机或 Release/harness-off 运行验收。

### Lint / 格式 / XML / i18n

- **Lint/静态：pass。** 当前 6 个变更/新增源码严格 UTF-8、原 BOM、物理 CRLF、原 EOF 风格通过；IdtMain 保留无 EOF newline，新 inl 为无 BOM/CRLF/有 EOF newline。全工作树 `git diff --check --ignore-submodules=all` exit0、stdout空；只记录现有 Trellis 文档 LF→CRLF 提示，没有重排或格式化它们。
- **TypeCheck：pass。** 当前 ARM64 和本次 x64 完整 Solution 实际编译/链接证据见上；未修改导出接口、项目或依赖版本。
- 4 XML 解析通过：Inkeys.vcxproj、filters、Headless vcxproj、BarViewAdjust.svg。当前源码增量只有 5 个 tracked CPP/H 加新增 inl，工程文件没有本轮改动。
- i18n 按主会话要求不重复运行，已读 `Build/ScrollFollow/live-final-i18n-check.log`：en-US、zh-TW 各 **426/426**，PASS。
- 静态完整元数据：`Build/ScrollFollow/live-final-offline-static.json`；package context 仍 Single-repo/no packages，按 native-desktop/native 集成及跨层规范核对。所有者/GUI安全/源码行为检查继续见本任务各独立报告，不以 XML 或编译替代行为证据。

## 冻结源码与产物

下表为 checker 离线实际读取的磁盘文件 hash；后续若新构建必须重新关联版本，不称进程内存 hash。

| 文件 | SHA256 |
| --- | --- |
| Inkeys/IdtMagnification.cpp | `C8B1651708F2EF9ECE15B577912195BF4C39ED2CDC6E768F1E1490E774C2F16A` |
| Inkeys/IdtMagnification.h | `E770139B74FC7215F58DAAE2C80D12601775544EF0BBFFDB2EBD3C9DDD908DB6` |
| Inkeys/IdtMain.cpp | `461015F3B557FFFA25FF40F135CD4FAA6C31347D8116EA23D14A651C000C8486` |
| Inkeys/IdtState.cpp | `3E7814E507FBC2B1C909ACB3B95A1019A5E1B56B63FBF796D2D98EA9B3443762` |
| Inkeys/Inkeys/Business/ScrollFollow.cpp | `FFE8124961A426D5D505D8C09219A7A2E61DBB5FCD8EA3E14E964299D87A77DC` |
| Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl | `6C655BF5449AEF8A401DDC0B5A35854069574BD6C99D0A7C280F847B62956D7A` |
| Build/ARM64/Debug/Inkeys.exe | `D6A348E46F78E0FC53F5C39B921EFAB11A7BB4B498DE4CACC03940964FA456ED` |
| Build/ARM64/Debug/InkeysHeadlessTests.exe | `ED614767D28D07A27D60B0197F964097645B321A5229BD6502BA7E7E6727456A` |
| Build/x64/Debug/Inkeys.exe | `2F6C8BB05535C7527DD6C7213B31DA46E90F3083DBB870C1B4939EC130988B4C` |
| Build/x64/Debug/InkeysHeadlessTests.exe | `F1AC4B3E4BC3C151562EF86692CD034F429FA24E48D39E928101DACEB9D1D1DE` |

两份 Headless 字节分别与上一软件阶段相同。主程序两架构含本轮 live harness 和 ACK 修复；不能把旧实屏结果的 hash 换成本表新主程序。

## Findings (not fixed) / 未测项

- **真实 ink 及 style→系统 ACK：本报告不判通过。** checker 离线验证结束时，主会话已将新一轮 GUI 交 core；下一步应独立读取其新的原始日志、专用像素及真实 UInk，不能以纯 CLI 或此前空页系统 ACK 代替。
- 浏览器/Word/PDF、长滚动/惯性、主栏完整视觉、活动真实笔/触控、旧 Windows/WOW64/Release、真实 resize/device/显示/DPI 故障和长时资源稳定性不在这次无 GUI范围；先前限定用例各自保持证据，不扩称。
- 未新增并发精确调度测试证明每种 Publish/旧 ACK 交错；持久事实谓词的代码推理与有限实际收敛应分别记录。
- 既有 PID72368 AV 无 dump/线程栈，当前通过的初始化及安全 helper 修复不能反向给它一个确定的精确原因。
- 本轮没有新的代码阻断或环境错误；上述是尚未覆盖的实际验收，不擅自标父/子任务 complete。

全部无 GUI 子进程和构建已退出，源码/build/test 所有权已交还主会话。checker 后续仅离线审查新增实际证据。

## 报告完成时追加的运行状态

主会话通知：用户重新解锁回复后的当前短 ink retry PID59040 仍在初始化前命中 security-before-initialization，child exit4/174ms，无 root、窗口或输入。core 仅继续只读核对屏幕命中元数据。本记录不把安全门结果诊断为 Mag/PrintWindow/RTS 或墨迹失败，也没有新增任何真实 ink 通过。checker 没有再运行 build、CLI 或 GUI。

## 2026-10-23 当前双向LK5后的完整无窗口回归

此段为新实际证据；上文2026-10-22的156.914秒、旧二进制及当时墨迹待测状态保留为历史，不混用版本或未测结论。后续原生墨迹/长程独立通过分别见live-product-ink-complete-check.md、live-long-complete-check.md；不由无窗口检查代替实屏。

root执行，checker只读核验、未重跑：`Build/ScrollFollow/native-lk5-final-no-window-1023.json` 记录PID20032、严格单参 `--no-window`、timeout900000ms、exit0、307.752秒。当前Headless磁盘SHA256独立复算 `CC3F315472FA01EAAF1DD9E00557C9099868CF1C6B77F36963CE1D4138FFF0A5`，与receipt一致；对应生产Tracker BF3409及tests EF45B732，准确差异/构建见live-lk5-calibration-check.md。

原stdout48801B：WindowCommandCompletion/CanvasPolicy failures0、旧14组6918合法帧与新native22组704帧全部accepted/rejected0；四个实际native负例仍为AmbiguousMotion、ForwardBackward、NonTranslation、NonTranslation，末尾 `PASS visual motion tracker failures=0`。pen-width policy/codec PASS，EraserAttribute216布局failures0、RecallAttribute0、Auto-collapse与Keyboard shortcut config均0 failures，最终 `PASS animation correctness`。独立搜索无行首FAIL。

stderr35B仅 `[ScrollFollowProtocol] failures=0`。无源码/环境错误；本次执行范围是完整既有no-window回归，不是产品实屏、外部应用、x64或所有IdtState分支验证。没有另建测试、构建或修改源码；external增量尚未交付时不读取半成品。父/子任务保持in_progress，不commit/push/finish/archive。
