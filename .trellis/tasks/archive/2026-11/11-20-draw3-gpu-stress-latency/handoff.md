# 交接：11-20-draw3-gpu-stress-latency（阶段 A）

更新：2026-11-21。状态：**阶段 A 已完成并归档**（用户宣布结束）。`refactoring/draw` 上的提交：`b114fee1`（S1 R5/R6）、`5a0f945c`（S2 R1–R4）、`5d3b96a3`（S3 R7）。诊断包、压力工具与实验脚本只入库到本地 `diagnostic/performance`（`ed05bc35`：`Scripts/LatencyDiag`、`Scripts/StressLoad`、`Scripts/GpuLatencyLab`）。实机回收结论见 research 第八轮。阶段 B、C 未开始；父任务（中期重构 11-16 及其第 2、4 步）不随本任务结束。

> 以下为 2026-11-20 规划阶段的交接内容，保留作历史记录；文中 `%TEMP%\inkeys-codex\gpu-latency\` 下的工具已移入 `diagnostic/performance` 的 `Scripts/GpuLatencyLab/`，原件已删除。

## 必读文件

- `prd.md`：阶段 A 需求 R1–R9、验收项、阶段 B/C 范围。
- `design.md`：各部分实现方案与行号。
- `implement.md`：分工、通用约定、S0–S5 步骤。
- `research/gpu-stress-latency-experiments.md`：第一至六轮全部实测证据。

## 结论速记

1. **主因**：Win11 上进程拥有可见窗口但不在前台时，约 3 秒后 QoS 被降为 1，GPU 调度优先级也变成 1；与窗口样式无关（已用探针验证）。前台程序和无窗口进程是 16，DWM 是 23。前台程序占用 GPU 时，Inkeys 的帧排队可达数秒。
2. **实测映射**（Inkeys，WPR `ChangePriority`）：
   - 节流时：NORMAL / ABOVE_NORMAL / HIGH 都是 1，REALTIME 是 23；
   - 关闭执行速度节流（`SetProcessInformation(ProcessPowerThrottling, {Version=1, ControlMask=EXECUTION_SPEED, StateMask=0})`）后：QoS 1→0，NORMAL / HIGH 都是 16，REALTIME 是 23；
   - 设备级 `SetGPUThreadPriority` 绝对值（`0x40000000|n`，n=16–29）非管理员可精确生效，并能越过节流；相对值没有作用。用户方案**未采用**设备级。
3. **压测对照**（压力工具占前台）：现状 >1.8 s；关闭节流 45–75 ms；REALTIME 34–125 ms。OBS + 压力：现状掉帧 4.4%；关闭节流 2.2%；REALTIME 23–25%。
4. **关闭等待**：waitable 链但不等待时 400→1840 ms 无界增长；普通链 47–133 ms。
5. **整屏操作审计**：书写每帧只有 L0 双清除是整屏（31.6 MiB/帧 @2880×1920）；骁龙上每帧 GPU 执行 p95 仅 0.39 ms，弱集显待测。

## 已确认的用户决策

- 性能页下拉“GPU 调度优先级”：标准 = NORMAL、**均衡 = HIGH（默认）**、高性能 = REALTIME（`D3DKMTSetProcessSchedulingPriorityClass`）。
- 新增开关“关闭电源节流”（**默认开**），说明“Windows 10 1709 及以上生效”；关闭开关时传 `ControlMask=0, StateMask=0`（交还系统默认，**不是**强制节流）。
- 档位行 i 提示用笼统措辞：“未关闭电源节流时，Windows 可能把 Inkeys 视为后台程序并降低其优先级，所选档位可能不会生效。”
- 高性能说明需提示录屏时慎用。
- 设置变更即时生效；启动时在 Draw3 首帧前应用一次；API 全部 `GetProcAddress` 动态获取（`SetProcessInformation` 从 Win8 起才导出，静态调用会让 Win7 无法启动）。
- R5（GPU 忙时超时不计入熔断）、R6（关闭等待时用普通链）、R7（L0 只清脏区，未登记写入退回整屏清除）、R8（自动化诊断包，Win7 SP1/Win10，x86/x64）均纳入阶段 A。
- 阶段 B：抬笔三轮整屏清除、形状中间层、激光烘焙。阶段 C：L2 快照整屏拷贝（与白板平移优化一起）。外部 WM_CLOSE 后进程滞留：暂不修改。
- 阶段 A 完成后，再到 Intel HD Graphics 4600（Win10）机器上做诊断包与 WPR 诊断。
- 分工：**Claude 写全部代码**；Codex 只读调查与审查（`gpt-6.2-sol` High fast 用于检查点，`gpt-6.2-astra` High default 用于最终审查）。不提交、不 push，除非用户当场授权。

## 环境与工具（都在 `%TEMP%\inkeys-codex\gpu-latency\`，不入库）

- `tools/lag.py`：画圆 + 截屏测可见延迟（按红色墨迹检测）；`exp3.py`：按配置跑多组（`n-wait`、`high-wait`、`noeco-wait`、`rt-wait`、`rt-nowait`、`rt-plain`）；`exp-obs.py`：OBS 对照（`obs` / `obs-stress`）；`gpuprio.py`：外部设置 D3DKMT 调度类；`throttle.py`：外部设置电源节流 `off|on|default`；`fg.py`：`focus <窗口类>` / `who`；`analyze.py`、`prio.py`：解析 xperf 导出；`mouse.py`、`shot.py`。
- `wpr/record.cmd`：提权录制（必须纯 ASCII、绝对路径；会弹 UAC，用户允许）。导出：`xperf -i trace.etl -o dxg.csv -a dumper -provider "{802ec45a-1e99-4b83-9920-87c98277ba9d}" -add_fieldnames`。
- `probe/gpuprio_probe.exe`、`probe/qos_probe.exe`：设备级优先级与窗口状态探针。
- 备份：`main.json.bak`（Inkeys 配置，`EnableWaitableWait=false` 是用户原值）、`obs-websocket-config.json.bak`（OBS 已还原，`server_enabled=false`）。

## 坑

- **PID**：其他会话会在 `Inkeys-draw` 工作树里跑 Inkeys 测试；只匹配本仓库 `Build\` 下、不带参数的 Inkeys.exe，并用日志头 `pid=` 核对。
- **前台变量**：竞争程序在前台是 16，在后台是 1，结果完全不同；用 `fg.py focus InkeysStressLoadWindow`（注入 Alt 后 `SetForegroundWindow`），绘制模式下画布会接管点击。
- 关闭 Inkeys 用 `taskkill /F /PID`；不带 /F 的 WM_CLOSE 会让进程滞留。
- 实验改过 `EnableWaitableWait` 后要还原。OBS 测试需要临时开启 websocket，测完还原；录像写入 `D:/Personal/Downloads`，测完只删本次生成的文件。
- Git Bash heredoc 会吞反斜杠；含反斜杠的脚本用 Write 写文件。Codex 简报同样用 Write 写入 `%TEMP%`。
- 构建与测试命令见 `implement.md` 的“通用约定”。

## 下一步

1. 用户批准后：`python ./.trellis/scripts/task.py start 11-20-draw3-gpu-stress-latency`。
2. S0：写 Codex Sol 开工核对简报（核对项见 `implement.md` S0），核实报告后修订 design。
3. 按 S1→S5 推进，每步：自审 → 构建/测试 → 实测 → Codex 检查点 → 修复。
