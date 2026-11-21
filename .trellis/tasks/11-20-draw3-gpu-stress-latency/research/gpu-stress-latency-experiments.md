# GPU 压力下输入延迟：定位实验与证据（2026-11-20）

## 环境
- Windows 11 ARM64（Snapdragon，非管理员会话），Debug 构建 `Build/ARM64/Debug/Inkeys.exe`（构建时间 14:35:53，分支 refactoring/draw）。
- 主画布 DirectCompositionVisualTree，2880×1920，`EnableWaitableWait=true`（实验期间临时开启，结束后已还原为用户原值 false）。
- 压力：`Build/Tools/StressLoad/arm64/StressLoad.exe --cpu 0 --gpu 8`（只开 GPU，每批 8 次全屏重像素 Draw，2 批在途，不 Present）。每组只运行约 10 秒。

## 测量方法（无需改代码）
- 工具在 `%TEMP%/inkeys-codex/gpu-latency/tools/`（不入仓库）：
  - `lag.py`：SendInput 按住左键沿圆周以 0.5 圈/秒、125 Hz 注入；首圈内 5 次 BitBlt 截屏（DWM 合成后的屏幕），检测红色墨迹尖端角度，与截屏时刻注入角度之差换算为**可见延迟**。基线 3–14 ms，测法自洽。
  - `gpuprio.py`：用 gdi32 导出的 `D3DKMTSetProcessSchedulingPriorityClass` 从外部修改目标进程 GPU 调度类。非管理员下 HIGH 与 REALTIME 均返回 STATUS_SUCCESS，且实测生效。
  - `exp2.py`：每组配置重启 Inkeys（避免熔断状态残留），点“绘制”，按配置设优先级/启动压力，测三个圆。

## 结果

| 配置 | 可见延迟（每组 3 圆 × 5 次截屏） | waitable 日志 |
|---|---|---|
| 无压力 | 3–14 ms | 120 acquired/s，等待≈0 |
| GPU 压力，Inkeys GPU 调度类 NORMAL | 首 1.8 s 内无任何墨迹上屏（>1.8 s） | 压力开启后约 1 s：`acquired=0 timeouts=8`，`signal-lost` 熔断，重建为非 waitable 链 |
| GPU 压力，Inkeys HIGH | 100–1000 ms，多数时间停滞 | 同样约 1 s 内熔断 |
| GPU 压力，Inkeys REALTIME | **30–80 ms** | 不熔断；信号间隔 28–44 ms，等待均值约 27 ms，偶发超时均被补等还清 |
| GPU 压力，Inkeys NORMAL，压力工具降为 IDLE | >1.8 s，无改善 | 熔断 |

截图旁证：NORMAL 优先级下画出的“圆”变成稀疏直线弦（相邻两帧采样点间隔几百毫秒）。

## 结论
1. **主因：GPU 调度饥饿。** Draw3 每帧 GPU 工作量小，但 GPU 调度类为 NORMAL，且画布是不激活窗口，拿不到前台加成，排在长着色包后面得不到执行。RTS/Pointer、鼠标/触摸都走这条公共链路，所以三种模式都受影响。CPU 压力只减少采样点，不阻塞 GPU，因此仍然跟手。
2. **放大因素：熔断。** 饥饿超过约 1 s（8 次连续 200 ms 超时）即判为 `signal-lost`。熔断后本会话永久退回非 waitable 交换链，失去“等到空位再取最新点”的保护，还会引发一次重建闪空。饥饿时上一帧 GPU 工作确实未完成，`PreviousFrameGpuIdle()` 可以区分这种情况与句柄真正失效。
3. **“关闭等待 → 近 1 s”**：开关关闭时仍创建带 `FRAME_LATENCY_WAITABLE_OBJECT` 标志的交换链（`Draw3.TransparentPresentation.cpp:1842`），但不取句柄、不等待。按 DXGI 契约，此时 Present 不按帧延迟阻塞，提交可以无界超前。
4. **残余：** REALTIME 下每帧 GPU 周转仍约 28 ms。候选原因有两个：一是压力着色包的抢占粒度，二是 Draw3 每帧对 L0 两张 RTV 做整屏清除（Codex 报告，`Draw3.DrawingController.cpp:23666`，`Renderer.cpp:351-359`，逻辑约 31.6 MiB/帧）。需要 GPU 计时或 WPR 才能区分。

## 尚未验证
- 设备级 `IDXGIDevice::SetGPUThreadPriority`（相对 +7 / 绝对 16–29 软实时）能否达到与进程级 REALTIME 相同的效果，以及非管理员下是否被拒绝。
- REALTIME 是否影响 DWM 与其他前台应用（实验中 DWM 正常合成、压力工具继续运行）。
- 触摸/笔输入下的同等改善（仅测了鼠标注入；机理在公共链路，预期一致）。
- WPR GPU 轨迹需要管理员权限（非管理员 `wpr -start GPU` 报 “Failed to enable the policy”）。

## 附带发现（与本任务无关，单独报告）
- 用 `taskkill /IM Inkeys.exe`（不带 /F，向每个顶层窗口发 WM_CLOSE）关闭时，一个实例在窗口全部销毁后进程仍滞留 2 分钟以上，UI3 线程继续空转，内存短时升至 1.9 GB，最终被强制结束。日志：`Build/ARM64/Debug/Inkeys/Logs/2026-11-20T15-15-24.log` 15:17:11 之后。

## Codex 调查（gpt-6.2-astra High，read-only，约 15 分钟）
- 报告：`%TEMP%/inkeys-codex/gpu-latency/report-A.md`。代码事实（单脏矩形 Present1、L0 整屏双清除、关闭等待仍带 waitable 标志、无任何 GPU 调度优先级设置、单设备无共享纹理）已逐条抽查属实。
- 其中“压力工具 GPU 调度类降为 IDLE”的对照无改善，说明修复应落在**提升 Draw3 自身调度**，而不能指望负载方让步。

## 第二轮（2026-11-20 15:30–16:10）

### 更正：目标进程
- 另一个会话同时在 `D:\Project\Inkeys\Repo\Inkeys-draw` 跑 Inkeys 离屏测试。早期脚本按进程名取 PID，WPR 那一轮把 REALTIME 设到了别的会话的进程上（45304），真正的画布进程 44492 仍为普通优先级。
- 脚本改为只匹配本仓库 `Build\` 下、不带参数的实例，并用日志头的 `pid=` 核对后复测：普通 84–1814 ms，REALTIME 45–88 ms（16:06–16:07，PID 32500/45552 与日志一致）。结论不变。

### 关闭等待时的三种交换链状态（压力 level 8，Inkeys REALTIME，15:32–15:34）
| 状态 | 可见延迟 |
|---|---|
| waitable 链 + 显式等待 | 45–75 ms |
| waitable 链 + 不等待（设置关闭时的现状） | 400 → 1840 ms，笔画越长越大 |
| 普通链 + 不等待（通过熔断重建得到） | 47–133 ms |

带 waitable 标志的链不再受设备级帧延迟上限约束（官方说明：此时必须改用 `IDXGISwapChain2::SetMaximumFrameLatency`，由应用等待句柄）。不等待就没有任何节流，提交帧在 DWM 前无界堆积。普通链由 `IDXGIDevice1::SetMaximumFrameLatency(1)`（`Draw3.GraphicsInitialization.cpp:161`）在 Present 内阻塞，排队有上限。

### WPR GPU 轨迹（管理员录制，15:44，Inkeys 44492 普通优先级）
xperf dumper 导出 DxgKrnl 事件，按进程统计 QueuePacket→DmaPacket（排队/执行）与 ChangePriority：

| 进程 | PublicPriority / SchedulingPriority | 无压力书写：包执行 p50/p95 | 压力书写：排队 p50/p95/max | 压力期包数 |
|---|---|---|---|---|
| Inkeys | 8 / **1** | 0.20 / 0.39 ms | **90 ms / 2.30 s / 2.33 s** | 92（无压力 6.5 s 内 1984） |
| StressLoad（前台） | 16 / **16** | — | 19 / 26 / 30 ms；执行 27.5 ms，从未被抢占成功 | 355 |
| dwm | 23 / 23（另有一个 26） | 0.30 / 0.44 ms | 21 / 26 / 51 ms | 915 |

- 同为 NORMAL 调度类，前台的压力工具实际调度优先级 16，不激活的 Inkeys 只有 1。这就是饥饿的直接原因。
- 另一会话那个被设成 REALTIME 的进程，上下文优先级为 23，与 DWM 相同，说明 REALTIME 类映射到 DWM 级别。
- **L0 整屏双清除不是瓶颈**：无压力时 Inkeys 每个包 GPU 执行 p95 仅 0.39 ms，结论是不改。
- **提权后残余 30–80 ms 来自 DWM**：压力工具单包 27.5 ms 且不可抢占，DWM 也要排队 21–51 ms，Inkeys 无法消除。

### 关闭滞留问题（Codex gpt-6.2-sol High fast，只读；报告 `%TEMP%/inkeys-codex/gpu-latency/report-B.md`）
- 已核实：Drawpad 的 `WM_DESTROY` 在触控板清理分支提前 `return DefWindowProcW`（`Draw3.WindowControl.cpp:3061`），跳过设置 `exitRequested_`、唤醒绘制线程的退出通知（`:3483`）。Bar、DrawpadPresentation、MagnifierHost、SelectionOverlay 等窗口的 WM_CLOSE 走默认处理直接销毁。owner 先被销毁时，Drawpad 只会收到 WM_DESTROY，Draw3 不退出，`ProductRunning()` 保持为真，主线程不发起退出（`IdtMain.cpp:4187`）。
- 未证实：内存涨到 1.9 GB 的来源（首嫌 Bar 失效窗口的 WARP 离屏重绘重试），需运行期采样。
- 第二个短命 Inkeys.exe 无法从日志归因；本轮出现的同名进程已证实来自另一会话的测试。

## 第三轮：整屏清除/拷贝审计（只读，Claude grep + Codex gpt-6.2-sol High fast 交叉核对）
报告：`%TEMP%/inkeys-codex/gpu-latency/report-C.md`。下表各项均已读码核实；字节量按 2880×1920 / 1920×1080 计算的逻辑量，实际带宽未测。

| 位置 | 内容 | 频率 | 面积 | 逻辑量 |
|---|---|---|---|---|
| `DrawingController.cpp:23666-23670` → `Renderer.cpp:267-271` | L0 Add+Retain 清除 | **书写每帧**；纯橡皮、纯激光接触也会执行（只有活动形状未变化时跳过） | 整屏 | 31.6 / 11.9 MiB/帧 |
| `DrawingController.cpp:23406`、`23463`、`6164`（RebuildActiveLayers） | L1+L0 清除 | 每次抬笔 **三轮**（每条完成笔画还多一轮） | 整屏 | 约 190 / 71 MiB/次 |
| `RendererShapes.cpp:145/319/342` | 形状 composite/material 中间层清除 | 形状预览每帧（`DrawingController.cpp:6118`），抬笔写入、历史重放（`StrokeGeometry.cpp:1110`）时按对象执行 | 整屏（层大小 = viewport） | 每对象 ≥ 31.6 / 11.9 MiB |
| `RendererSnapshot.cpp:172`，触发条件 `DrawingController.cpp:23815` | 可信 L2 快照 CopyResource | 每写完一笔一次；**画布平移时每帧** | 整屏 | 21.1 / 7.9 MiB/次 |
| `Renderer.cpp:323` 等 | 激光烘焙整拷颜色层到 scratch；批末/过期整清 coverage | 每次激光烘焙/结束 | 整屏 | 21.1 / 7.9 MiB/次 |
| `InkHistoryGpu.cpp:775/781`、各类清屏/翻页/恢复/Resize | L0/L1/L2/backbuffer 清除 | 低频 | 整屏 | — |

已有边界、无需修改：L1 追加（只画新增稳定段）、L2→backbuffer 复制与 L1/L0 合成（`frameDirty`）、Present1（单个脏矩形，双缓冲 FLIP_SEQUENTIAL）、激光活动 coverage（`ClearLaserCoverageRect`，矩形绘制 pass）、历史瓦片（128²/256² 切片，L2 用 `ClearView` 带矩形）、白板纹理与调试层（scissor）、ULW 拷贝（裁剪脏盒）、粒子（按槽 Dispatch）。

可复用的现成写法：`InkHistoryGpu.cpp:712-742` `ClearL2Tile`。优先用 `ID3D11DeviceContext1::ClearView` 带矩形清除；拿不到 `ID3D11DeviceContext1` 时，从已清空的 scratch 纹理 `CopySubresourceRegion` 回退。激光另有矩形绘制 pass（`RendererLaser.cpp:301-313`）。

按矩形清除需要的不变量：层在“上次写入包络”之外必须保持 Add=0、Retain=1；包络必须覆盖所有触点、预测段、抗锯齿边缘与笔尖锥形，否则会留残影。

## 第四轮：档位映射与 OBS 对照（2026-11-20 16:55–17:25）

### WPR：调度类 / 设备优先级 → 实际 GPU 调度优先级（ChangePriority 事件）
Inkeys（不激活透明窗口）进程级 `D3DKMTSetProcessSchedulingPriorityClass`：

| 调度类 | 公开值 / 实际调度优先级 |
|---|---|
| IDLE | 0 / 0 |
| BELOW_NORMAL | 4 / 1 |
| NORMAL / ABOVE_NORMAL / HIGH | 8 / **1**（切换时无 ChangePriority 事件，实际调度优先级未变） |
| REALTIME | 23 / 23 |

探针进程（无窗口控制台程序）的设备级 `IDXGIDevice::SetGPUThreadPriority`，非管理员调用：
- 绝对模式（`0x40000000 | n`）设 16/17/18/20/23/26/29 时，实际调度优先级**精确等于 n**；设 1 时为 1；设 8、12（保留区间）无变化。`GetGPUThreadPriority` 读回不反映绝对值。
- 相对模式 0–7 在基准 16 上不改变实际调度优先级。

### 根因细化：Windows 电源节流（EcoQoS）
- 探针默认就是 16，Inkeys 默认只有 1；两者的上下文 `Qos` 不同（Inkeys 0x1，探针 0x0）。
- 对 Inkeys 调用 `SetProcessInformation(ProcessPowerThrottling, {ControlMask=EXECUTION_SPEED, StateMask=0})`（显式关闭执行速度节流），WPR 显示 Qos 1→0，实际调度优先级 **1→16**；显式开启节流时 Qos=5、优先级 1。非管理员可调用。
- 结论：Windows 把不激活、透明的 Inkeys 判为后台/省电进程，GPU 调度优先级随之降到 1。前台程序为 16，所以与前台程序竞争时 Inkeys 被饿死。CPU 侧同样受 EcoQoS 影响（未单独测量）。

### 控制前台变量后的书写延迟（压力工具 level 8，用 SetForegroundWindow 保证其为前台）
未控制前台时有一轮 NORMAL 只有 45–73 ms：压力工具没抢到前台，两边都是 1，公平分享。

| 档位 | 可见延迟 | 熔断 |
|---|---|---|
| NORMAL（现状） | >1.2–1.8 s | 是 |
| HIGH | >1.2–1.8 s | 是 |
| **关闭电源节流（16）** | **45–75 ms** | 否 |
| REALTIME（23） | 34–125 ms | 否 |

### OBS 录屏对照（OBS 32.0.1，显示器采集 2880×1920@60，x264；临时开启 websocket，测后配置原样还原、录像删除）
| 场景 | 档位 | 书写延迟 p50 / max | OBS 渲染掉帧 | OBS 平均渲染耗时 |
|---|---|---|---|---|
| 仅 OBS | 默认 | 6.8 / 30 ms | 0/603 | 1.40 ms |
| 仅 OBS | 关闭节流 | 5.6 / 13 ms | 0/597 | 1.45 ms |
| 仅 OBS | REALTIME | 5.7 / 14 ms | 0/597 | 1.39 ms |
| OBS + 压力工具占前台 | 默认 | 1208 / 1817 ms | 26/594（4.4%） | 4.4 ms |
| OBS + 压力工具占前台 | 关闭节流 | 58 / 96 ms；复测 61 / 104 ms | 14/613（2.3%）；复测 13/597（2.2%） | 4.2 ms |
| OBS + 压力工具占前台 | REALTIME | 62 / 100 ms；复测 63 / 127 ms | **140/604（23%）；复测 151/608（25%）** | **14.0 / 11.4 ms** |

结论：关闭电源节流（16）在书写延迟上与 REALTIME 相当，对 OBS 反而比现状更好；REALTIME 没有额外的书写收益，却使 OBS 渲染掉帧升到约 1/4。编码掉帧各组均为 0。

## 第五轮：关闭节流 + 调度类；节流状态下的设备级优先级（WPR，17:37）
- Inkeys 关闭执行速度节流后（Qos=0，16），调度类改为 ABOVE_NORMAL、HIGH 均无 ChangePriority 事件，仍为 16；REALTIME 为 23；从 REALTIME 回到 HIGH 为 16。**HIGH 无论是否节流都等同于 NORMAL。**
- 探针先对自身开启执行速度节流（Qos=5，实际 1）：相对值 +7 无变化（仍 1）；**绝对值 16/18/20 分别生效为 16/18/20（Qos 仍为 5）**；回到相对 0 又变为 1。即设备级绝对优先级可以越过节流降级，只作用于该 D3D 设备的上下文。
- 推论（各系统的“均衡”实现）：
  - Win11 / Win10 1709+：关闭执行速度节流（GPU 恢复到 16，CPU 侧也不再 EcoQoS）。
  - Win10+：Draw3 设备设绝对优先级 16，保证即使节流接口不可用、或系统以其他方式降级，GPU 侧也是 16。微软文档说明绝对模式仅 Windows 10+ 有效。
  - Win7：无节流接口，也无绝对模式，只有相对值 −7..7。相对值在本机没有任何作用，Win7 的调度行为无法在本机验证。
- 未验证：Win10（无 EcoQoS 的机器）上 Inkeys 默认是否也被降到 1；Win7 上不激活窗口的默认优先级。计划在 HD 4600（Win10）上用 WPR 读 ChangePriority 确认；Win7 需要有实机才能确认。

## 第六轮：为什么 Inkeys 被判为后台（窗口状态探针，WPR，18:00）
探针 `qos_probe.exe`（%TEMP%，不入库）创建 D3D11 设备持续提交小任务，按变体创建窗口，每种运行 20 秒，由 Python 依次启动（父进程相同）：

| 变体 | QoS / 实际优先级变化 |
|---|---|
| 无窗口 | 全程 Qos=0，16 |
| 普通可见窗口（WS_OVERLAPPEDWINDOW，SW_SHOWNOACTIVATE，不在前台） | 启动 16 → **约 3 秒后 Qos=1，降为 1**，直到退出 |
| 仿 Inkeys 覆盖窗（NOACTIVATE\|TOOLWINDOW\|TOPMOST\|LAYERED\|TRANSPARENT，全屏，alpha 1） | 同上，约 3 秒后降为 1 |
| 覆盖窗去掉 NOACTIVATE | 同上，约 3 秒后降为 1 |

（另有一轮每种 3 秒的测试：所有变体在 3 秒内保持 16，只在销毁窗口时短暂出现 Qos=1；当时的上下文句柄复用导致部分归属错误，已修正解析脚本。）

结论：
- 降级与 NOACTIVATE/TOOLWINDOW 等样式无关。规则是：**进程拥有可见窗口、但不是前台进程**，约 3 秒后 QoS 降为 1，GPU 调度优先级随之为 1。没有窗口的进程保持 16，前台进程为 16。
- 要被系统当成“前台”，只能真正拥有前台窗口，这会抢走 PPT 等程序的焦点，不可行。
- 不改窗口、不抢焦点的显式声明，就是 `SetProcessInformation(ProcessPowerThrottling)` 关闭执行速度节流（第四、五轮已实测：Qos 1→0，优先级 1→16）。这正是“均衡”档的第一层。
- 推论：任何“有窗口但在后台”的程序（包括最小化的录屏软件）在 Win11 上同样是 1。Inkeys 恢复到 16 后，会优先于这类程序，但不会压过前台程序（同为 16，轮流使用 GPU）。

## 第七轮：阶段 A 实施后实测（2026-11-20 20:33–21:24，Debug ARM64，压力 level 8 占前台）

工具：`Scripts/LatencyDiag`（arm64）与临时脚本 `exp4.py`（A/B）、`exp5.py`（饥饿后恢复）、`exp-obs2.py`。期间 1Password 周期性（约 10 分 20 秒）弹出 Windows 安全验证框：它是更高完整性级别的前台窗口，`SendInput` 被 UIPI 静默丢弃，受影响的组记为 noInk，已剔除/复测。

### 启动与 R6
- 启动日志：`进程调度 tier=balanced class=4 ... status=0x00000000 readBack=4 throttlingApplied=true`。
- 关闭等待时：`waitable disabled by setting; using ordinary swapchain`，交换链 `flags=0x0`。

### 诊断包矩阵（每格 10 次截屏，p50 / 最大）
| 压力在前台 | 普通链（关闭等待） | waitable 链（最终修复后） |
|---|---|---|
| 标准 + 节流默认 | 776 / 1472 ms（另 3 次未上屏） | 1.5 s 内未上屏 |
| 标准 + 关闭节流 | 45 / 110 ms | 41 / 56 ms |
| 均衡 + 节流默认 | 1.6 s 内未上屏 | 1.5 s 内未上屏 |
| **均衡 + 关闭节流（默认）** | **58 / 73 ms** | **44 / 90 ms** |
| 高性能 + 默认 / 关闭 | 105/120、84/95 ms | 60/84、78/92 ms |
无负载各组 −18…10 ms（负值为预测领先）。诊断包结束后 Inkeys 调度状态恢复为原值（class=4，节流 control=0x1）。

### R5 回归与修复（GPU 忙时欠账封顶）
- 现象：waitable 链上先经历饥饿再切回默认配置，延迟停在 130–350 ms 不恢复（干净 A/B：waitable 36–56 ms、普通链 37–91 ms）。
- `exp5`：基线 45–53 ms；3 秒饥饿后同配置 60–246 ms，6 秒后仍未恢复。日志 `debtOverflowTotal` 0→13：饥饿期间超时仍 Present，欠账封顶 2，超出的帧永不补还、留在合成队列。改动前饥饿约 1 秒即熔断重建，问题被掩盖。
- 修复：新增策略 `GpuBusy`，此后无令牌 Present 的欠账上限 `kFrameLatencyMaxBusyDebt`。上限 16 时：3 秒饥饿后 45–79 ms（欠账顶到 16、溢出 1、恢复后一秒补还 17 次）；但诊断包 7 秒饥饿仍溢出 5 帧，REALTIME 组帧交还间隔约 40 ms → 约 +200 ms（246 ms）。上限改为 32（5 秒忙计时内约 25 次超时 Present）后矩阵如上表，无熔断、无溢出（欠账最高 9）。另加兜底：GPU 忙期间仍溢出则熔断（`QueueOverflow`）重建普通链。
- R5 本身：饥饿阶段每秒 3–8 次 200 ms 超时全部记为 `gpuBusyTimeouts`，整轮无熔断（改动前约 1 秒熔断）。

### OBS 录屏 + 压力（waitable 链，录制 2880×1920@60）
| 档位 | 书写 p50 / 最大 | OBS 渲染掉帧 | 平均渲染耗时 |
|---|---|---|---|
| 默认（均衡 + 关闭节流） | 56 / 101 ms | 15/608（2.5%） | 5.7 ms |
| 高性能 | 56 / 103 ms | 141/599（23.5%） | 8.6 ms |
websocket 配置原样还原，本次录像已删除。

### 未覆盖
- 残影：只验证了普通笔圆环（截图无残留）；荧光笔、预测开关、多指、形状构造/编辑、橡皮、激光未做上屏截图（逻辑由 WARP 像素读回与生产探针覆盖）。
- Win7 / Win10 老设备、x86/x64 诊断包实机运行（包已打好：`Build/Tools/LatencyDiag/package-x64`、`package-x86`）。

## 第八轮：诊断包实机回收（2026-11-21，Release x64 构建，诊断包 v2）

第一批（11-20 晚，5 台）：两台 Win7 为 VMware 虚拟机（x86 虚拟机 Inkeys 硬件 D3D11 失败退回 WARP+ULW，GPU 压力对它无意义）；HD 4600（Win10 19045）与 UHD 730（Win11 26100）压力下 p50 64–87 ms，与档位/节流无关。诊断工具据此补了 StressLoad 帧耗时、抢占粒度/HAGS、VM 识别、WPR 提权助手等（v2）。

第二批（v2，4 台，压力 level 8 占前台，每格 4 环 × 9 次截屏，p50 / p90 / max）：

| 机器 | StressLoad 单包 | 标准 + 节流默认 | 均衡 + 关闭节流（默认） | 高性能 + 关闭节流 |
|---|---|---|---|---|
| 骁龙 X Elite，Win11 26300 | 26.7 ms | 428 / 1078 / 1249 | 39 / 61 / 91 | 53 / 75 / 108 |
| i7-8565U UHD 620，Win11 26300（电池） | 59–65 ms | 93 / 109 / 118 | 106 / 118 / 174 | 111 / 120 / 155 |
| i3-12100 UHD 730，Win11 26100.1301 | 36.2 ms | 62 / 128 / 291 | 57 / 66 / 110 | 51 / 301 / 412 * |
| i7-4810MQ HD 4600，Win10 19045 | 51.4 ms | 81 / 92 / 144 | 81 / 98 / 103 | 83 / 213 / 444 * |

无负载各格 −8…8 ms。骁龙上“均衡 + 节流默认”27/36 次 1.3 s 内未上屏；OBS 作自备负载时各档 p50 −2…7 ms。

结论：
- **R5/R6 实机成立**：4 台均无熔断、无交换链重建；GPU 忙超时全部记为 `gpuBusyTimeouts`；骁龙饥饿期间欠账最高 24（上限 32），无溢出，恢复后逐帧还清。Win10 19045 上 `SetProcessInformation` 关闭节流成功（`throttlingApplied=true`）。
- **Intel 核显的压力延迟 ≈ StressLoad 单包 × 1.6**，档位与节流基本无影响：抢占粒度为三角形/图元级，全屏大三角形的包无法中途抢占，Inkeys 帧和 DWM 合成各要等一次包边界；Inkeys 帧率随之降到 1/单包（UHD 730 约 20–27 帧/秒）。课堂负载（PPT、视频）单包只有几毫秒，这个下限相应很小。
- **WPR**：UHD 620（26300）标准 + 节流默认时 Inkeys 同样被系统降到调度优先级 1（Qos=1），但包最长只排队 180 ms，不会像骁龙那样饿死（骁龙同条件单包等待 1.9 s）。UHD 730（26100.1301）上 HIGH + 关闭节流后仍为 public=15 / sched=1（该版本事件无 Qos 字段），因 Intel 不饿死而无可见影响。
- **高性能（REALTIME）在 Intel 上的长尾（* 号）多为测量伪影**：WPR 显示 csrss.exe（GDI 截屏的 GPU 上下文，优先级 1）包排队最长 1172 ms（均衡档 220 ms），诊断工具的 `BitBlt` 因此卡顿 0.8–1.3 s，延迟值出现负数或随时间线性增长。剔除后 UHD 730 p50 47–53 ms、HD 4600 关闭节流 p50 91 ms；Inkeys 自身包最长从 145 ms 降到 37 ms。HD 4600 另有一环墨迹冻结约 1.3 s，原因未明（该机 WPR 失败）。用户决定高性能档的说明与可见性不变。
- **诊断工具已知问题**（未修改）：截屏卡顿与“已画完”样本应剔除并单独计数；Win10 上 `wpr -stop` 报 `0x80010106` 失败，需在该机 `wpr -cancel`；Win10 读不到原节流状态（按配置回退恢复）。

### 未覆盖（阶段 A 结束时）
- 残影上屏截图只做了普通笔；Win7 实机（非虚拟机）未测。用户 2026-11-21 宣布阶段 A 结束，不再补测。
