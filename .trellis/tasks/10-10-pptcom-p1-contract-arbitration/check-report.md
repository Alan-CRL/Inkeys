# P1 独立 Trellis Check

> 2026-10-10；状态 PASS_P1_SCOPE。复核范围为 P1-AC1..10 全部合同及工程登记。本轮未接入生产消费者，P2 未开始。源码和测试所有权已释放给主会话。

## Findings (fixed)

1. File: Inkeys/Inkeys/Business/PresentationLinkService.cpp；InkeysHeadlessTests/presentation_link_tests.cpp
   - Issue: TakeCommands 只检查 target/deadline；同 source/epoch 的控制能力撤回或切到 LegacyAutoSelect 后，已排队操作仍进入执行 outbox。执行端 guard 能挡住，但服务提取阶段遗漏复核。
   - Fix: 提取时以服务持有的当前 route/candidate/source 调用同一 GuardCommand。临时 ExecutionLedger 只校验，不消耗执行 owner 的独立去重高水位；Unsupported/RejectedStale 不进入 outbox。新增能力撤回和控制模式改变的回归。
2. File: Inkeys/Inkeys/Business/PresentationLinkService.cpp；InkeysHeadlessTests/presentation_link_tests.cpp
   - Issue: Foreground 准备 B 后，精确前台证据消失而 B 仍为弱提示 BestChoice，pending 只比较候选，旧强确认可绕过 weakWindow/独立样本门。
   - Fix: Foreground pending 不再满足 tier3 时取消并重新进入弱挑战。回归验证旧强确认被拒绝，随后只有持续窗口与独立弱样本齐备才产生新确认。
3. File: Inkeys/Inkeys/Business/PresentationLinkService.cpp；InkeysHeadlessTests/presentation_link_tests.cpp
   - Issue: 合法已注册 source/epoch 的 sequence gap 携带非法目录/能力批次时，payload 的 Invalid 提前返回，使缺口前旧 route 继续可执行。
   - Fix: 核准 envelope 后先标 NeedsFull/失去准入并撤 route，再校验 payload。非法内容不部分写目录/高水位、不消耗 sequence；合法 Full 仍可直接恢复。新增非法 Full/非法 metadata gap、旧命令拒绝与恢复确认回归。
4. File: Inkeys/Inkeys/Business/PresentationLinkService.cpp；InkeysHeadlessTests/presentation_link_tests.cpp
   - Issue: Active → Unknown → Inactive 只因上一包不是 Active 而漏写墓碑，同 generation 可恢复此前 logicalShow。
   - Fix: 根据此前已有 logicalShow 退役已知场次，而不是只看上一包 lifecycle；明确终态不保留可执行 logicalShow。新增 Unknown 过渡后明确 Inactive、同代拒绝、新代新场次回归。
5. File: Inkeys/Inkeys/Business/PresentationLinkService.cpp；InkeysHeadlessTests/presentation_link_tests.cpp
   - Issue: 墓碑无条件拒绝相同退役 generation，合法 Full 重复携带 Inactive/Ended 会阻止同批其他健康候选刷新。
   - Fix: 经主会话按既有目录合同确认，仅允许相同墓碑代的 Inactive/Ended 不可执行观察；继续高水位、字段一致性及容量校验，终态 logicalShow 清零。相同代 Active/Unknown 和更旧代仍 Invalid。新增重复终态与健康候选 Full、gap/删除后终态回流不可执行及旧代拒绝回归。

## Findings (not fixed)

无 P1 范围内未解决的源码、合同或工程问题。P2 及其后真实平台/集成验证未实施，属于已批准阶段边界，不作为 P1 源码通过的推导。

## 全范围验收

已读取任务 prd/design/implement、check.jsonl 全13项、父设计、P0 基线/通信决策、review-scenarios，以及全部受影响 spec。get_context.py --mode packages 确认 single-repo；native-desktop/ppt-interop 索引和相应检查规则已应用。native demo 的独立 solution/GUI 验证不适用于本次普通 Business 纯逻辑。

| AC | 结论及实际核查 |
| --- | --- |
| P1-AC1 | PASS：版本/host/source/epoch/sequence、显式生命周期/质量/optional 与12项能力；严格 UTF-8、枚举/页数/拓扑/文本预算、目录/历史容量；候选批次与 metadata 原子校验。入包与最终目录文本预算在不同循环分别计数，中间 bytes=0，无双倍扣预算。 |
| P1-AC2 | PASS：明确注册 epoch，Full 初始/恢复、sequence/generation/state 高水位、gap/断连 route 撤销；非法 payload gap 同样撤销。heartbeat 仅存活，不续页或活动。 |
| P1-AC3 | PASS：单源多候选；只有可靠 session/PID/创建身份/window/lifetime 合并；弱 HWND/路径、同文件不同窗口隔离；显式终态、Unknown 过渡后的 Inactive、删除/Full遗漏、gap/过期观察、新 lifetime、新代和终态重复均核查。 |
| P1-AC4 | PASS：统一真实 foreground > 弱提示 > 运行；backend/权限/transport 不跨 show 加分；同级保持、初选稳定键、多源不累加票；权威动态值来自一个 source。 |
| P1-AC5 | PASS：独立活动 revision/弱窗口、短脉冲取消、重复页包/heartbeat不计样本、同show非权威来源活动证据、soft interval/reverse window/source恢复。所有策略由调用者显式传入。 |
| P1-AC6 | PASS：迟到 provider 不盖过真实 foreground；Inkeys 保留外部上下文；selfInduced 不独立挑战；新的真实 show 快切；新前台 Busy/无 Full 先撤后台 route。 |
| P1-AC7 | PASS：prepare/匹配 confirm/commit；B→C、epoch/generation变化、强证据降级、owner失败/timeout拒绝旧确认；弱准备中当前页快路径；强切換无后台命令 route，无被动 Activate。 |
| P1-AC8 | PASS：可靠 same-show source switch 保留 logicalShow/showSession，route推进；证明映射后才保留target，Ordinal/未知映射隔离；当前绝对页更新即时推进target。EndScreen保留明确语义，不声明普通页或Draw3 ready。 |
| P1-AC9 | PASS：仅一个route、完整target/ID/deadline/expectedPage/Goto/能力；提交/提取/执行三处 guard；源端独立 ledger、有限命令/结果及ID高水位；已发送失败/超时 OutcomeUnknown，无改投重试，ACK不改页/绑定。 |
| P1-AC10 | PASS_P1_SCOPE：最终完整 Debug ARM64 solution、fresh headless --no-window、格式/XML/diff/工程登记检查通过；P0生产COM及随附产物不变。实际平台/Office/呈现门禁见下文。 |

GuardCommand 的 currentRoute/currentCandidate 在服务提交/提取来自自身已确认状态，fake 执行测试来自 owner 捕获的 Selected route 和已观测 candidate；没有从 command.target 复刻可信状态。执行 ledger 与服务提取临时 ledger 独立。新增逻辑只有标准库，不引入 COM/Win32/Draw3/线程/transport 依赖。

## Verification

| 检查 | 结果与证据 |
| --- | --- |
| Lint / 格式 | PASS。仓库没有该普通C++模块的独立 lint 命令；git diff --ignore-submodules=all --check、严格UTF-8/CRLF/行末空格审计、两个 vcxproj XML 解析通过。animation_tests.cpp 原 UTF-8 BOM 保留，工程原 CRLF 保留。 |
| TypeCheck / Build | PASS。当前 VS ARM64 原生 MSBuild，完整 InkeysRepo.sln Debug ARM64 /m:1 /p:CL_MPCount=1，隐藏进程，10分钟限时，同一 invocation 的 PATH 规范化及 MSBUILDDISABLENODEREUSE=1。build-green.exit.txt=0；native/headless均编译修后 Service；stderr为空，最终增量轮次无新 warning。 |
| Tests / 最终产物 | PASS。fresh InkeysHeadlessTests.exe --no-window，headless-green.exit.txt=0；138 checks / 0 failures；整套 PASS animation correctness；stderr为空。 |
| 红回归 | 已复现。build-red.exit.txt=0；未修复产物在正常执行上下文 headless-red-unsandboxed.exit.txt=1，127 checks / 8 failures，stderr仅8个P1失败，分别覆盖提取复核、强→弱pending和非法payload gap。之后另补生命周期/终态回归，最终138全绿。 |
| 受限 runner 环境噪声 | 默认上下文红轮整体exit1，除同8个P1失败外有11个既有配置/临时文件写入失败；相同红产物在正常上下文11项消失。未改这些测试或项目以绕过环境。 |
| 工程与基线 | native工程仅2个ClInclude+1个ClCompile；headless工程仅2个ClInclude+2个ClCompile；现有main仅声明+调用。主会话已核P0 26项SHA：25一致，唯一工程差异为上述3行登记；独立check逐行核实工程/main最小diff。 |
| 旧托管测试 | implement已记录本次实际重建 PptCOM.Tests exit0；C#与旧部署产物未改，未无必要重复运行。 |

日志位于忽略生成目录 TestResults/pptcom-p1/check/：build-red.*、headless-red.*、headless-red-unsandboxed.*、build-green.*、headless-green.*；每轮明确保存进程退出码。历史 C1902/C1060 环境诊断及托管测试证据见 [validation.md](validation.md)。本轮未改变SDK、工具链、NuGet、全局环境配置或生成部署DLL/TLB。

## Spec Sync 与实际限制

主会话拥有 multisource-service.md，并已同步gap/payload边界、强证据降级、终态退役/重复观察、提取guard/独立ledger、P1 pageSafe与旧Present/UI ack边界；独立check核对当前签名和行为一致。未改旧 com-contract/native-session-ui3 合同。

NOT VERIFIED：真实 PowerPoint/WPS、损坏 COM、Office RCW/连接点释放与内存趋势、实际COM apartment；Win7 SP1 x86/x64原版.NET4；双向权限 helper/UAC/token/ACL；生产共享内存/命名管道及第三方 adapter；Draw3/UI 接管、真实前台观察与光学/端到端时延。没有启动GUI、Office或UAC，没有Computer Use，没有P2实现；独立复核代理未commit/push/archive，阶段提交与归档由主会话按用户授权随后收尾。
