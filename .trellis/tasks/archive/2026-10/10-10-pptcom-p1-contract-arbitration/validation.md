# P1 验证记录

> 2026-10-10；包含 implement 历史及最终独立 check 结果。质量门禁为 PASS_P1_SCOPE；本轮仅 P1，产品消费者未接入。

## 构建和运行

Visual Studio 由现有 vswhere 查询安装路径，再定位 `MSBuild/Current/Bin/arm64/MSBuild.exe`。所有 MSBuild 均在同一 PowerShell invocation 先执行仓库要求的 PATH 规范化和 `MSBUILDDISABLENODEREUSE=1`，使用隐藏 Start-Process 与独立 stdout/stderr，不启动产品主窗口/Office/UAC。

| 轮次 | 命令和结果 | 证据 |
| --- | --- | --- |
| 默认 sandbox | 完整 InkeysRepo.sln Debug ARM64 /m:1 /nr:false；退出 1，首错 C1902 出现在标准库 array(1,1)，随后既有 cppm 同错 | TestResults/pptcom-p1/implement/build.stdout.log / build.stderr.log |
| 正常本机执行上下文 | 同一完整 solution；退出 1。C1902 消失；首个 C1060 出现在 MSVC include/xtree(736,1)，另见 IdtMagnification.cpp(382,1) | build-unsandboxed.stdout.log / .stderr.log |
| 上轮实际新生成 headless | InkeysHeadlessTests.exe --no-window；退出 0；P1 第一版 104 checks / 0 failures；整套 PASS animation correctness | headless-first.stdout.log / .stderr.log |
| 低并发完整 solution | /p:CL_MPCount=1 /m:1；退出 0，Inkeys/PptCOM/托管测试/headless 均成功；15 分钟上限 | build-lowparallel.stdout.log / .stderr.log |
| 补充回归后的 headless | 退出 0；107 checks / 0 failures；整套 PASS animation correctness | headless-final.stdout.log / .stderr.log |
| 单行 warning 修复后完整 solution | 同一低并发 Debug ARM64 配置；退出 0；新测试 C4389已通过 2u 消除，本轮无新增 warning | build-warningfix.stdout.log / .stderr.log |
| warning 修复后生成的 headless | --no-window；退出 0；107 checks / 0 failures；整套 PASS animation correctness；stderr 为空 | headless-warningfix.stdout.log / .stderr.log |
| 实际重建的托管测试 | 退出 0；PASS PptCOM descriptor ownership and session/owner contracts | managed.stdout.log / .stderr.log |

C1902 是 PDB manager 错误；沙箱外同工具链没有复现，只确认执行上下文差异，具体 IPC/版本根因没有查实。C1060 是编译器堆空间不足，在本机已有 targets 的 `Microsoft.Cl.Common.props` 支持 CL_MPCount 的依据下，主会话批准仅临时降低 invocation 并发；没有为此修改源码、工程、SDK、DLL 或全局配置。详见 [官方 C1902 说明](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/fatal-error-c1902?view=msvc-170)。

## AC 映射

测试实际调用 `PresentationLinkService` 公开 API 和 `GuardCommand`；fake 只提供消息与确认，无替代仲裁器，不 sleep。断言名称保存在 `InkeysHeadlessTests/presentation_link_tests.cpp`。

| AC | 已有实际测试与检查 |
| --- | --- |
| P1-AC1 | Unicode、strict UTF-8、非法 enum/零页/过大页/重复 topology、非法批次原子性、Unknown/optional、目录与源上限、同 epoch 能力更新一致性 |
| P1-AC2 | initial delta/full、duplicate sequence、gap 撤 route、old generation 高水位、epoch 显式换代与 retired 拒绝、heartbeat 不刷新页/活动、失联必须 Full、单调时钟 |
| P1-AC3 | 同一 source 多 show、可靠同 show 跨 source 合并、未验证非零 HWND/PID不归并、同文件/同 PID不同窗口独立、Ended 与重用新 lifetime、删除 tombstone、gap 新场次、过期旧 observer 不提供 continuity |
| P1-AC4 | backendPreference 仅同 show 内有效；late provider 不覆盖真实 foreground；initial 到达顺序稳定；同级保留 current；重复来源不叠加事实票 |
| P1-AC5 | 单次弱提示只观察；重复 revision/Page/heartbeat 不计样本；弱优势消失重置；新独立样本+持续窗口；soft interval 与 reverse window；source 恢复独立窗口 |
| P1-AC6 | 明确真实 foreground 快速切换；Inkeys 保留外部上下文；selfInduced不独立挑战；随后真实其他 show 快路径；Busy或尚无 Full 的新前台也撤销后台 route |
| P1-AC7 | B→C 最新意图，迟到 B 拒绝；epoch 替换旧确认拒绝；owner 失败/prepare timeout 阻止未变快照反复重试；弱准备期间 A 当前页直达；失效/强切换禁旧 route；被动切换无 Activate outbox |
| P1-AC8 | 同 show 稳定映射保持 session/target 且 route 递增；Ordinal/缺 document 无跨源映射；EndScreen需稳定文稿+拓扑；未知映射 target递增且不安全；Unknown/明确EndScreen不伪造最后页；正常绝对页直达 |
| P1-AC9 | 单一 outbox完整 route、service/fake guard、ID高水位、deadline、expectedPage、unsupported/Goto范围、old target、queued取消、dispatched timeout/disconnect OutcomeUnknown、迟到ACK拒绝、不自动重试、有界队列与结果历史 |
| P1-AC10 | 既有 full solution 和 headless 入口、编码/CRLF与工程最小登记；implement 版本完整构建与最后生成 headless 均退出 0；独立 check 已收束通过；25项不变与1项预期工程登记基线已核对 |

## 最终独立检查

最终全范围复核见 [check-report.md](check-report.md)。implement 的107项是历史通过结果；checker新增31项边界回归并收束5处局部漏洞：提取能力/模式复核、强前台pending降为弱证据、非法payload gap撤route、Active→Unknown→Inactive墓碑、重复终态Full不阻止健康候选刷新。

| 轮次 | 实际结果 | 证据（TestResults/pptcom-p1/check/） |
| --- | --- | --- |
| 新回归未修复产物构建 | 完整solution Debug ARM64 exit0 | build-red.stdout.log / .stderr.log / .exit.txt |
| 默认受限runner红轮 | exit1，127 checks / 8 P1 failures；另外11个既有配置/临时文件写入失败，未修改其代码 | headless-red.* |
| 同红产物正常上下文 | exit1，127 checks / 8 failures；stderr只剩8个P1失败，环境噪声消失 | headless-red-unsandboxed.* |
| checker最终完整solution | ARM64原生MSBuild，/m:1 /p:CL_MPCount=1，10分钟上限、同invocation PATH规范化；exit0，native/headless均编译最终Service，stderr为空，无新warning | build-green.* |
| checker最终fresh headless | --no-window；exit0，138 checks / 0 failures，整套PASS animation correctness，stderr为空 | headless-green.* |

P1-AC1..10全范围结论为PASS_P1_SCOPE。每轮读取实际进程退出码，未仅依据stdout判成功。源码/测试收束后格式、XML与git diff --check通过；下面真实平台/集成限制仍成立。

## 范围和门禁

新代码和两个工程及测试入口已静态检查 UTF-8/CRLF；animation_tests.cpp 原有 UTF-8 BOM 保留。`git diff --ignore-submodules=all --check` 通过。implement 与主会话分别按 P0 的26项SHA-256记录核对：25项完全一致（含COM/旧ABI/随附DLL/TLB），唯一预期变化为 Inkeys.vcxproj 的已批准三行文件登记；工程 diff 已逐行核对。

Win7 SP1 x86/x64原版.NET4、管理员/普通用户双向 helper、真实 Office/WPS/损坏 COM、实际事件 owner/资源释放、共享内存/命名管道生产通道、Draw3/UI 接管和真实呈现时延，全部 NOT VERIFIED。本轮没有 GUI/Computer Use/Office/UAC 测试，也没有进入 P2。

## 本轮提交与收尾状态

用户已授权必要阶段commit。P0已成功提交6d4931b7；P1功能提交尝试沿原SSH签名配置，实际exit1：1Password: failed to fill whole buffer，Git随后failed to write commit object。没有创建P1 commit，没有绕过签名/修改配置/重试/push。P1代码与测试已暂存，质量门禁仍PASS_P1_SCOPE；提交失败是本机签名问题，不是编译或测试失败。

按仓库签名失败即停止及trellis-finish-work先工作提交后归档的规则，P1仍in_progress，待签名提交恢复后归档和记录journal；P2未开始。失败说明属于任务记录，尚未暂存。本轮未改动其他任务或旧journal。

## 签名重试成功

用户明确要求重新尝试提交后，沿原SSH签名配置成功创建P1工作提交 0b968e2b6ad2d1f50ea48025308f505fd001f8ae；提交对象含SSH签名。前述失败为历史记录，现继续完成归档/journal；未push，未开始P2。
