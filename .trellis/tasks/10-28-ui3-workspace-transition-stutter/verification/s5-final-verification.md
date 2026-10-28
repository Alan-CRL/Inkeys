# S5 最终实现与验证

2026-10-29。沿用 `10-28-ui3-workspace-transition-stutter`；S5 P1–P4 实现和本轮自动化正确性验证完成。**原任务 R4 的完整帧预算仍未达标，真实 Office/WPS/DWM/设备验收未完成**，因此任务保持 `in_progress`。没有 commit、push、归档或启动产品主窗口/Computer Use。

## 已实现

1. **UI 与原生窗口所有权分离**：PageControl/Whiteboard 的 render callback 保留动画、布局、D2D/GetDC/ULW、固定命中候选；向现有 Window Service 非阻塞发布候选并消费真实回执。ULW 已定位后只核验 bounds，不再执行同步 SetBounds/Show/Hide。
2. **latest-wins 与完整成功边界**：固定 role/pair mailbox，revision、HWND/generation、presentationRevision、frame/workspace 身份及实际矩形/显隐完整匹配；完整 ACK 前不消费对应 damage、不发布新命中或页码 UI-ready。静态像素等待 native 时复用，新 hover/光照继续推进并取代候选。
3. **幂等与层级事件**：相同 bounds/显隐不重复写入；普通 Show 不整组 Refresh。Pointer Down promote 与显式根刷新使用独立固定事件邮箱，避免普通像素候选或命令队列争用吞掉意图。现有串行 UI 生产者不会耗尽固定槽；不承诺任意数量并发生产者都接纳成功。
4. **pair 与 Bar 直移**：原生 bounds/回滚和 ULW 共用 try-only role lease，租约保留到回执或恢复退休。pair 第二窗失败真实回滚，mask3 后才确认；最后 Move+Up 无后续输入也收敛。Bar 保留原 translation、held/release surface、吸附/居中/显示屏障；中央屏障取消旧请求，保留已移动但未消费的真实结果，禁止升级旧位图版本。
5. **取消/提交与恢复**：在原生终态边界对取消和提交作原子裁决；strict receipt 与只供退休处理的 receipt 分开，后者不授予 UI-ready。旧代际 Hide 在安全覆盖锁存前拒绝；等价 Hide 不制造安全版本变化；恢复/租约阻挡不自旋；首个 native error 与 rollbackError 分开保存。
6. **原协调器异步推进**：StateMonitoring 使用显式 Prepare/Complete 回执，复用 Host runtime 信号接收背景/原生进展；保留 250ms 兜底、原有失败次数/退避、旧 token 拒绝、Executing 责任和源补偿。通知不代表 ready。SyncDraw3State 的原生副作用移到该协调线程；定格按钮/快捷键使用异步根刷新。

独立复审：[P1/P2](s5-review-p1p2.md)、[P3/P4](s5-review-p3p4.md)。调用范围见 [UI3 窗口调用审计](../research/ui3-window-call-audit.md)。相关 rendering-and-ui、whiteboard、canvas-freeze-boundaries、PPT native-session、诊断规范及跨层指南已同步，旧 UI 同步等待/直接窗口操作示例已修订。

## 构建、格式与版本

- 完整 `InkeysRepo.sln Debug|ARM64`，vswhere 动态定位 ARM64 原生 MSBuild，`/m:1 /nr:false`，同 invocation PATH 规范化，900 秒期限。最终 `s5/s5-relink/build.exit.txt` 为 **0**。
- 产品 EXE SHA-256：`FF001E90C3AA2C1B02D01F0740FDC6854F1E0B6FF534FA1C80CFA903AFBAEEEF`。
- Headless EXE SHA-256：`743B4B91A7F20CC5A7B6D94BE814E5E44B32EFCCE0F3E867A07BA2962C332BB9`。
- 保留会话开始前 PageControl 性能计时/驱动与文档；原 EXE、源码、diff 在 `TestResults/ui3-workspace-transition/s5/baseline/`。
- 21 个被修改的既有源码文件均保持原 BOM 状态与 CRLF，没有全文件格式化。`git diff --check --ignore-submodules` 通过，格式/hash 清单为 `s5/final-source-format.json`。三个新增 Window 内部头文件不引入另一线程或窗口管理器。
- Trellis manifests 校验通过；大规范仍超过注入上限，实现和复审均按 reading-map 从磁盘补读相关章节，未修改上下文上限。

构建中先后保留了以下失败证据，未掩盖为成功：沙箱 C1902（PDB IPC 环境问题）；新增 Bar native fixture 的 protected 成员访问错误（用局部测试派生类修正，未放宽产品访问）；MSVC 增量链接器两次 `LNK1000/IncrBuildImage` 内部异常（只移走并保留准确定位的生成文件 Inkeys.ilk，按原配置重链接成功）。没有更改 SDK、工具链、依赖或项目配置。既有数值转换/诊断 warning 保留；构建日志中的 pwsh 查找回退不代替真实退出码判定。

## 最终实际运行

以下均读取 OS 进程退出码，全部 **0**，未超时：

| 入口 | 结果与证据目录（相对 TestResults/ui3-workspace-transition/s5） |
| --- | --- |
| `--page-control-hidden-test` | `s5-native-first/`；WindowCandidateNative、WorkspaceWindowNative（5 次通知）、PageDirectNative、BarNativeMove、WhiteboardNative（5 次像素提交）、PageControlHidden 全部 failures=0 |
| `--state-mode-production-test` | `s5-native-first/`；workspace/工具/旧回调/shortcut/FineDial 与新增异步阶段、通知/预算合同 |
| `--bar-eraser-offscreen-test` | `s5-ui-regressions/`；公共 renderer、固定捕获帧/新 damage、PageControlUI3 零失败 |
| `--ui3-bottom-dock-transition-test` | `s5-ui-regressions/`；生产 Seek/固定表面/释放吸收/反复拖动/任务栏几何、DPI/负原点/吸附屏障，premature=0、failures=0 |
| `--ui3-workspace-centering-test` | `s5-ui-regressions/`；六向、快速反向、左右展开、负原点及失败呈现，summary failures=0 |
| `--ui3-input-worker-test` | `s5-ui-regressions/`；生产 mailbox/dispatcher/continuation/deadline/cancel |
| `InkeysHeadlessTests.exe --no-window` | `s5-headless-final/`；约 6 分 59 秒，末尾 `PASS animation correctness`；已保存 result.json/日志 |

实际 native fixture 在屏外创建并自行销毁 HWND；新 Workspace 用保持画布隐藏的 Desktop policy 调用真实 RequestPrepare/Complete，检查 payload/Current/observer 发布顺序、排队/在途取消和旧凭据失效。Page 的最后 Up 用例在任何 fallback 绘制前检查实际 pair 回执，必须成功 mask3 或注入失败 mask0；Bar 使用真实 DIB ULW 验证像素版本、中央屏障、已完成未读取移动与新代际拒绝。Whiteboard 使用生产 RenderBackground 和真实 1920×1080 ULW，等待 native 回执期间不重复整屏绘制。

确定性红绿包括：同请求 Invalidate 后旧背景帧复活、overlapping pair 使旧单窗 ACK 失效、原生 Commit 与邮箱回执之间的取消、密集进展通知加速失败预算。Whiteboard/P4 的 ARM64 探针红测 exit1、绿测 exit0 及原始日志落盘于 `s5-whiteboard/`、`s5-p4/`。Window 两次红测在工具会话中观察到 `FAIL new pair invalidates prior singleton receipt` / `FAIL late cancellation preserves physically committed unread move` 与 failures=1，但没有单独保存红测 stdout/进程退出码或旧 EXE；绿测工具明确 exit0。`window-candidate-probe/` 只保留后续更新的源码/EXE/obj 和测试片段，不能冒充原始红测冻结产物。该纯 helper 探针用动态发现的 ARM64 cl 14.51/SDK10.0.26100（/std:c++20 /EHsc /utf-8 /MT）；最终主 solution 仍用既有14.44工具链，集成测试也已执行。其余新增边界以实际故障注入验证，不把静态检查或 API 缺失编译错误冒充执行红测。

Headless 既有测试生成的十张旧任务路径 PNG 已核对来源、文件数及无跟踪文件，移入各阶段 evidence 目录保存；没有修改无关测试或用户数据。

## 生产四 HWND 三轮交替 A/B

同机 Windows 11 ARM64/Qualcomm、Balanced 电源，Debug、默认 WARP/动画/光效、96 DPI。每轮按旧版→新版交替执行，双方各六方向×3 cycle×3 轮 = **54 次完整转场，0 incomplete**；六个进程退出码均为 0，采样时没有并行构建或其他测试。

命令：`INKEYS_PAGE_CONTROL_TRANSITION_PERF=1 Inkeys.exe --page-control-hidden-test`。旧版 SHA-256 `EA7B51CAA0423037568A432CA1222D3BC0DA8EDC98B421F5797CAD64FE2DEBFA`，新版为上述 FF001…；原始 EXE、frames/summary/metadata/result 在 `s5/final-ab/`。计算脚本 `s5/analyze.py`，结果 `s5/final-ab/analysis.json`。

| 四窗工作批次（包含冷首帧） | 旧版 | S5 最终 |
| --- | ---: | ---: |
| 批次数 | 2,271 | 10,137 |
| P50 ms | 9.747 | 1.848 |
| P95 ms | 11.866 | 3.178 |
| P99 ms | 15.335 | 4.574 |

P95 下降约 **73.2%**。最终确认 **19,675** 次像素及 native 成功提交，不能用 40,548 次 callback 代替。candidate→receipt P50/P95/P99 为 **0.029/0.122/0.519 ms**，最大 33.513 ms；owner 成功完成的像素提交间隔 P50/P95/P99 为 **1.995/3.569/4.987 ms**，最大 46.631 ms。方向/Idle 边界不串联。旧版没有成功时刻列，不伪造旧成功间隔作对比。

三轮队列峰值 **4/3/3**，coalesced **5/0/0**，stale **2/4/1**；每轮实际显隐变化 **24**、普通帧 promote **0**、重复 native bounds 写入 **0**。这些累计计数取每轮最终值，没有把每行累计值相加。UI 的 set_bounds_ms 为 0；visibility_ms 在新版明确表示发布成本，metadata 标记了语义变化。

以上为真实 ULW/owner 软件事务的 unlimited 屏外测量，不是显示器 FPS、GPU duration 或真实 DWM 光学呈现承诺。P1/P2 独立阶段的较早 A/B 另见 [阶段报告](s5-p1p2-validation.md)，不与最终交替对照混用。

## 共享渲染剩余预算：明确未完成

另对旧/新 EXE 各运行同口径 `INKEYS_UI3_TRANSITION_PERF=full INKEYS_UI3_TRANSITION_CYCLES=2 --ui3-workspace-centering-test`：六方向×100/150/200%×3轮×2cycle，各 **108 次转场**、54 组；两版退出码均0、failures=0，九份 raw 均 sealed、零 callback/batch 丢样。原始数据在 `s5/{s5-shared-baseline,s5-shared-scheduler}/raw/`，结果 `s5/shared-comparison.json`。

| 该负载的组分位中位数 | 旧版 | S5 |
| --- | ---: | ---: |
| batch P95 ms | 22.226 | 22.934 |
| 成功 Bar 提交间隔 P95 ms | 23.322 | 23.772 |
| GetDC P95 ms | 10.550 | 10.543 |
| 满足 P95≤16.67/P99≤33.4 的组数 | 1/54 | 1/54 |

这是以最终 HWND sink 替身运行的 Bar/PageControl 共享 Scheduler 负载，**不包含 native Window Service、Freeze 背景或原生协调器**，不能拿它抵消或外推上述真实四 HWND 收益。这里没有得到稳定的剩余绘制成本收益，也不能把约 3% 的单轮组中位差异作确定回归归因；两版都存在同类预算缺口。2cycle 是明确的短测，不冒充旧任务 5cycle/Release 矩阵或原计划30cycle。

因此 R6–R10 的 S5 实现已落地，但 **R4 仍未闭环，不能宣称全部三倍率稳定60FPS或用户现场全部卡顿已消失**。原研究中尚未解决的绘制/GetDC 成本继续保留；没有通过降低帧率、关闭动画/光效、跳过 ACK 或放宽预算制造达标。

## 尚未验收的边界

- 真实 PowerPoint/WPS、DWM 可见合成、物理触控、实际任务栏自动隐藏/升起、真正跨显示器/DPI/最小化恢复的端到端用户体验。自动化覆盖相应状态/几何/故障合同，不等价于现场验收。
- 原有前台 fallback 边界：在原生 Complete 排队期间用户主动切到外部前台，现有捕获/重基准协议仍需真实 Office/焦点测试。本次没有扩大焦点许可，也没有另造焦点协议。
- native API 本身不可被1500ms期限抢占；在途未退休时继续保留真实恢复责任。D2D/GetDC/ULW 的同步呈现成本仍存在，异步窗口所有权不承诺这些调用永不等待。

当前改动可审阅且所有本轮自动化正确性门禁通过；后续性能/现场验收继续在原任务中进行。
