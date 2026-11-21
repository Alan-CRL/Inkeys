# Draw3 书写延迟优化 · 阶段 A（GPU 调度、帧延迟与 L0 脏区）

## Goal

降低 Draw3 书写（鼠标/触摸/笔，RTS/Pointer）在低性能设备和课堂并发负载（PPT 放映、录屏、视频）下的延迟与卡顿，保留“每计算帧只采用一个最新点”的设计。压测工具只是复现手段，不是目标场景。本任务是阶段 A；阶段 B、C 见文末。

## 已定位的原因（证据见 research/gpu-stress-latency-experiments.md 第一至六轮）

1. **GPU 调度降级（主因）。** Win11 上，进程拥有可见窗口但不是前台时，约 3 秒后 QoS 被降为 1，GPU 调度优先级随之变为 1；与窗口样式（NOACTIVATE、TOOLWINDOW 等）无关。前台程序和无窗口进程是 16。前台程序持续占用 GPU 时，Inkeys 的帧可能排队数秒（WPR 实测单包等待最长 2.3 s，可见延迟 >1.8 s）。
2. **熔断放大。** 饥饿约 1 s 即触发 waitable `signal-lost` 熔断，本会话永久退回非 waitable 交换链，并重建一次（闪空）。
3. **关闭等待后延迟无界。** 关闭“Waitable 模式下启用等待”时仍创建 waitable 标志的交换链，但不等待；这种链的 Present 不受帧延迟上限约束，提交无界堆积（实测 400→1840 ms，笔画越长越大）。
4. **每帧整屏清除 L0。** 书写时每帧把 L0 的两张整屏纹理（BGRA8 + R16_FLOAT）各清一次，2880×1920 下逻辑量约 31.6 MiB/帧；高性能 GPU 上可忽略，弱集显上待实测。

## 已确认的决策

- 性能页新增 **“GPU 调度优先级”下拉框**，三档均用 `D3DKMTSetProcessSchedulingPriorityClass`（Win7 SP1 起可用）：
  - 标准 = NORMAL
  - **均衡 = HIGH（默认）**
  - 高性能 = REALTIME
- 性能页新增 **“关闭电源节流”开关（默认开）**，用 `SetProcessInformation(ProcessPowerThrottling)` 关闭执行速度节流，说明“Windows 10 1709 起生效”。
- 档位行带信息 i：未关闭电源节流时，系统可能把 Inkeys 降为后台优先级，所选档位可能不会生效（措辞不点名具体档位：其他系统和环境下各档位是否受影响尚未验证）。
- 已知事实（写入说明与文档，不改变用户决策）：
  - Win11 实测：HIGH 与 NORMAL 效果相同（节流时都是 1，关闭节流后都是 16），Win11 上“均衡”的收益来自“关闭电源节流”开关；只有 REALTIME（23）在节流状态下也生效。
  - REALTIME 书写延迟与“关闭节流”相当，但实测使 OBS 渲染掉帧升到 23–25%（“关闭节流”为 2.2%，现状 4.4%），说明中需提示演示/录屏时慎用。
  - HIGH 在 Win7/Win10 上是否有别于 NORMAL、Win10 上 Inkeys 默认是否被降级，由本任务的诊断包在老设备上确认。——2026-11-21 回收（research 第八轮）：Win10 HD 4600 上 HIGH 与 NORMAL 延迟无差别；Intel 核显上即使被降到优先级 1 也不会饿死，压力延迟由抢占粒度决定，档位与节流基本不起作用。
- 分工：Claude 规划与编写代码；Codex 负责调查与独立审查（`gpt-6.2-sol` High fast 用于检查点，`gpt-6.2-astra` High 用于最终审查）。

## Requirements

- **R1 调度档位**：下拉三档映射 NORMAL / HIGH / REALTIME，默认均衡（HIGH）。设置变更即时生效，无需重启；启动时按配置应用一次。
- **R2 电源节流开关**：默认开。开 = 显式关闭执行速度节流（`ControlMask=EXECUTION_SPEED, StateMask=0`）；关 = 交还系统默认（`ControlMask=0`）。即时生效。
- **R3 兼容与降级**：两类调用都通过 `GetProcAddress` 动态获取，系统不支持或调用失败时不影响启动与绘制，只记一次日志（含系统版本、请求值、返回码）。Win7/Win8 上节流开关项仍可见，说明其不生效条件。
- **R4 日志**：启动与每次变更记录请求档位、节流开关、调用结果，以及 `D3DKMTGetProcessSchedulingPriorityClass` 的读回值。
- **R5 熔断判据**：waitable 等待超时后，若上一帧 GPU 工作仍未完成（`PreviousFrameGpuIdle()` 为假），该次超时既不计入也不打断 `signal-lost` 连续计数；只有 GPU 已空闲而信号仍不来时才计数。句柄失效（Failed）仍立即熔断。
- **R6 关闭等待用普通链**：设置关闭“Waitable 模式下启用等待”时，DComp 模式直接创建普通 FLIP 交换链（不带 waitable 标志），由设备级 `SetMaximumFrameLatency(1)` 在 Present 内限流。
- **R7 L0 只清脏区**：书写每帧的 L0 清除改为只清“上一帧 L0 中实际有内容的范围”。范围外必须始终保持 Add=0、Retain=1；范围须覆盖所有写入 L0 的内容（笔尾、预测段、抗锯齿、笔尖锥形、活动形状）。纯橡皮/纯激光接触且 L0 为空时不清除。优先 `ID3D11DeviceContext1::ClearView` 带矩形，不可用时回退为整屏清除。
- **R8 诊断包**：在仓库 `Scripts/` 下提供手动使用的自动化诊断工具，可在 Win7 SP1 / Win10 的低性能设备上运行（x86 与 x64），收集环境信息、自动测量各档位与各负载下的可见延迟、收集 Inkeys 日志；Win10 上可选录制 WPR GPU 轨迹。不进解决方案和 CI。
- **R9 保留不变**：“每计算帧一个最新点”、输入后端采样、熔断后的恢复路径、ULW/Win7Dwm 路径行为均不改变。

## Acceptance Criteria

- [x] **默认配置（均衡 + 关闭节流）**，本机 GPU 压力工具占前台：书写可见延迟 p50 ≤ 80 ms、最大 ≤ 150 ms，无熔断（用 `lag` 同口径测量 3 圆 × 5 次截屏）。——实测 waitable 链 44/90 ms、普通链 58/73 ms，无熔断（research 第七轮）。
- [x] **标准 + 节流开关关闭**：行为与改动前一致（可复现 >1 s 的饥饿，作为对照）。——776–1538 ms 或 1.5 s 内未上屏。
- [x] **高性能**：书写延迟不劣于默认配置；OBS + 压力场景的掉帧数据记录在 research。——OBS + 压力下与默认相当（56/103 vs 56/101 ms），OBS 掉帧 23.5%；纯压力矩阵中 60–78 ms，略高于默认 44 ms（样本 10 次/格）。
- [x] **OBS 录屏 + 压力**：默认配置下 OBS 渲染掉帧不高于改动前（4.4%）。——2.5%。
- [x] **R5**：GPU 忙导致的超时不触发熔断（单元测试覆盖策略判定；压测中“标准 + 节流开”出现长时间饥饿时不熔断或熔断次数减少，以日志为准）；句柄失效仍熔断。——饥饿期间超时全部记为 gpuBusyTimeouts、无熔断；实测发现并修复 GPU 忙时欠账封顶导致的队列残留（GpuBusy 欠账上限 32，溢出则 QueueOverflow 熔断）。
- [x] **R6**：关闭等待后，压力下可见延迟有上限（不再随笔画增长；参考 47–133 ms），日志显示创建的是非 waitable 链。——flags=0x0，压力下 37–110 ms。
- [ ] **R7**（部分）：书写、预测、橡皮、形状、多指场景无残影（截图比对）；纯橡皮/纯激光接触时不清 L0；`ClearView` 不可用时回退整屏清除。——WARP 像素读回覆盖局部清除/跳过/回退；上屏只验证了普通笔圆环无残影，荧光笔、多指、形状、橡皮、激光未做上屏截图。2026-11-21 用户宣布阶段 A 结束，此项不再补测，按现状验收。
- [x] **R8**：诊断包在本机（ARM64 构建）完整跑通一次；产出环境报告、各配置延迟表、日志副本；x86/x64 可编译。老设备实测在阶段 A 完成后进行。——本机 arm64 端到端跑通 5 次；x64/x86 已打包且无 Win8+ 导入。2026-11-21 已在 4 台实机（骁龙 X、UHD 620、UHD 730、HD 4600 Win10）跑通（research 第八轮）。诊断包、压力工具与实验脚本按用户决定只入库到 `diagnostic/performance` 分支（`Scripts/LatencyDiag`、`Scripts/StressLoad`、`Scripts/GpuLatencyLab`），不进 `refactoring/draw`。
- [x] 日志记录档位、节流开关、调用结果与读回值。
- [x] 完整 Solution（Debug|ARM64）构建通过；InkeysHeadlessTests 与设置页相关测试不新增失败；i18n 同步完成。
- [x] 规范同步：`draw3-integration.md` 中“开关关闭 = 保留 waitable 交换链但不等待”改为新行为，并补充调度档位与节流的说明。

## 范围外

- **阶段 B**（之后单独处理）：抬笔时 L1/L0 三轮整屏清除；形状 composite/material 中间层整屏清除（`RendererShapes.cpp:145/319/342`）；激光烘焙整屏拷贝与清除（`Renderer.cpp:323` 等）。
- **阶段 C**（与白板平移性能优化一起）：可信 L2 快照整屏拷贝（每笔一次、平移每帧，`RendererSnapshot.cpp:172`）。
- 外部 WM_CLOSE 关闭后进程滞留（调查结论见 research 第二轮，用户决定暂不修改）。
- 设备级 `SetGPUThreadPriority` 绝对优先级（已验证可行，用户方案未采用；如诊断包显示 Win10 存在降级且无节流接口，再另行评估）。
