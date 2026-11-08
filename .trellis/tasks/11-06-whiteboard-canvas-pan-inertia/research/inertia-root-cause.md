# Research: 画布平移惯性——"无法接续"与"突然变大"的根因、数值证据与替换模型

- **Query**: 找出 Draw3 现有画布平移惯性的两个缺陷的根因（惯性中新滑动无法像浏览器 FlingBooster 一样叠加；惯性/速度/距离偶发突然变大），给出数值证据，并提出稳健的速度/惯性模型。
- **Scope**: mixed（内部：`Draw3.CanvasNavigation.*`、`Draw3.DrawingController.cpp`、输入层、历史提交、旧单测、spec；外部：Chromium `FlingBooster` / `FlingCurve` / `PhysicsBasedFlingCurve`、Android `VelocityTracker`、Compose `VelocityTracker`）
- **Date**: 2026-11-06
- 行号均基于工作树 `feature/whiteboard3`。`Nav.cpp/.cppm` = `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp/.cppm`；`Ctl` = `Draw3.DrawingController.cpp`；`RTS` = `Draw3.RealtimeStylus.cpp`；`CI` = `Draw3.ContactInput.cpp`；`Win` = `Draw3.WindowControl.cpp`。
- 复现脚本：`.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/research/sim/`（`python -I`，纯标准库，见该目录 `README.txt`；结果文本 `results_*.txt`）。

---

## 0. 一屏结论

1. **没有任何真机日志可用**。全仓库和 `.trellis` 中 `pan-release / pan-anomaly / pan-begin / inertia-first-step` 只出现在源码格式串与一个"源码里必须含这些字符串"的静态断言（`Ctl:12191/13679/13888/13793`、`inkStrokeModelerTestTests/laser_incremental_coverage_tests.cpp:545-554`），没有被捕获的日志。以下所有数值来自"精确移植 + 合成输入"，输入参数（抖动 0.7 ms、基础延迟 2 ms、QPC 10 MHz、位置噪声 0.15 DIP）是**假设**，不是实测。
2. **稳态估速本身是准的**：匀速滑动（60/120/240 Hz、抖动、1–3 ms 量化、鼠标 125–1000 Hz + 消息合并）下 `release/true = 1.00–1.02`。所以"突然变大"不是稳态噪声，而是下面几个**少样本 / 基线错位 / 距离放大**的机制。
3. **"突然变大"最强候选（按严重度）**
   - **H1 触点拓扑变化后的时间/位置基线错位**（`Ctl:11877-11889`、`Ctl:13768-13772` → `Nav.cpp:352-368`）：二指→一指（两指抬起相隔 0–60 ms，即真实"双指甩"）后首个 Move 只剩 2 个样本，位移按"上次消费的快照"算、时间按"抬起时刻"算。120 Hz 触摸：释放速度 `p99 = 2.10×`、最大 `2.81×`，`glide/ideal` 最大 `7.9×`；60 Hz 触摸：`p90 = 3.33×`、最大 `5.61×`，`glide/ideal` 最大 `31×`，P(释放速度>2×) = `11.5%`。
   - **H2 距离 ∝ v² + 24000 DIP/s 上限**（`Nav.cppm:20-21`、`Nav.cpp:472-501`）：任何 2× 的速度误差 = 4× 距离；上限处单次滑行 `48000 DIP`（1080p 约 25 屏宽）。这是放大器，使 H1/H3 的小误差变成"突然飞出去"。
   - **H3 Down 零点 + "≥2 样本即出速度" + 无最小时间跨度**（`Nav.cpp:35-44`、`72-102`、`379-398`）：Down 与首个 Move 若被背靠背派发（线程停顿后）而位置已走了一段，首个速度直接顶到 24000 clamp（`first-emit/true = 8.0`）；≤16 ms 的短拖拽释放时 `15%` 试验直接以 24000 起滑（滑行 48000 DIP）。
   - **H4 100 ms 线性拟合滞后 → 停手"幻影惯性" + 轻甩被低估**（`Nav.cpp:46-70/72-102`）：快速接近后减速停手再抬起（真实释放速度 0）时，模型给出（中位）`387–4724 DIP/s`、幻影滑行（中位）`13–1860 DIP`（p99 最大 2198）（例：3000 DIP/s 巡航、100 ms 减速、停 50 ms 后抬起 → `1301 DIP/s`、滑行 `141 DIP`，100% 复现）。同一个滞后让加速中抬起的甩动被低估到 `0.42–0.78×`（滑行只有应有的 `17–60%`）。**同一估速器既会偏小又会偏大，是"手感忽大忽小"的直接来源**。
4. **"无法接续"是设计结论，不是 bug**：`OnTouchDown` 把惯性中的首个触点降为 `PanCandidate`（不产生速度），`BeginCanvasPan` 立即停惯性并把旧速度放进一次性 `releaseVelocityCandidate`，首个有效 Move 永久清掉它，`EndCanvasPan` 只取新 `directVelocity`；spec 明文禁止叠加（`runtime-and-rendering.md:873/902/914`）。仿真：同向 4000+4000 DIP/s，间隔 80–500 ms，第二次滑行距离 = 单次滑行的 `1.01–1.03×`（零叠加）；单指落下时新滑动贡献 `0`，旧惯性还被 12000 DIP/s² 加倍制动。历史上唯一的"接续"是 120 ms 线性衰减混合（`04081c6e`…`a2277444`），按设计在 120 ms 后权重归零，释放时几乎无贡献，随后被 `e4ddd5c9` 整体删除。
5. **推荐替换模型**（§4，已在仿真中对比）：每指独立 LSQ2 估速器（100 ms 窗口、≥3 样本、≥20 ms 跨度、2 ms 合并、40 ms 断口、软停手门限 30→70 ms）+ 触点增减**不重置**估速器 + 闭式"指数+库仑摩擦"惯性（`τ=0.30 s, c=1000 DIP/s²`，速度上限 12000，距离近似线性于速度）+ Chromium 式 FlingBooster（同向、残余≥350、新速度≥350、50 ms 截止，叠加"上一次起滑速度"，总上限 18000）+ 笔靠近即停。仿真结果：二指甩释放 `p99 1.03–1.04×`（现有 120 Hz `2.11×`、60 Hz `5.55×`）、停手 50 ms 后幻影速度中位 `124–250 DIP/s`（现有 `774–2602`，再配 250 DIP/s 最小释放速度即不起滑）、加速甩动 `0.85–1.01×`（现有 `0.42–0.50×`）。
6. **输入层结构性问题**（放大 H1/H3/H4）：每触点单槽 mailbox（"活动帧仍合并 Move"，`CI:805`），RTS `Packets` 只取 `lastPacket`（`RTS:1417`），时间戳是**回调到达时刻** `QueryQpc()`（`RTS:1389/1434`、`Win:501-502`），不是设备时间戳；渲染循环只每帧采样一次（`Ctl:16804`、`18572-18582`）。仿真里给估速器喂"每个事件"（producer 侧环形缓冲）后：burst 下最大误差 `1.71→1.22`，帧停顿下"丢失速度"`8.0%→0%`，36 ms 极短甩动从"无速度 58.2%"到 `0.99–1.10×`。

---

## 1. Findings

### Files Found

| File Path | Description |
|---|---|
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cppm` | 常量（L14-24）、`CanvasPanMotionState`（L153-171）、API 声明 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp` | 估速/释放/惯性内核（L26-102、L335-503）与 Touch 批次 FSM（L179-333） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | Touch Down（L12142-12215）、`rebuildPanContactBaseline`（L11865-11923）、`updateCanvasNavigation`（L13523-13910）、主循环调用点（L16802-16804）、帧等待（L18546-18582） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | RTS 回调：`QueryQpc()`（L207）、`Packets` 只取 `lastPacket`（L1417）、到达时间戳（L1389/L1434） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` | `PublishMove` 单槽覆盖（L775-809，L780-784 写锁竞争直接丢 Move，L805 注释）、`TryReadSnapshot`（L827-847）、`WaitForFrameDeadline`（L1223-1252） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | 鼠标兜底：`HandleCompatibilityMouse` 在处理函数内 `QueryPerformanceCounter`（L494-541），坐标为整数 `GET_X_LPARAM`（L503-504） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm` | `kCanvasNavigationProductIntegrationEnabled = false`（L30） |
| `inkStrokeModelerTestTests/canvas_navigation_tests.cpp` | 旧沙盒单测（L171-349）；不属于 `InkeysRepo.sln`，`InkeysHeadlessTests` 不编译 `Draw3.CanvasNavigation.*`（见同目录 `move-mode-input-routing.md` §0 第 6 条） |
| `.trellis/spec/native/runtime-and-rendering.md` | L848-931 "Free Canvas Navigation And Predictive Recovery" |
| `.trellis/tasks/archive/2026-08/draw3-source/archive/2026-08/08-12-canvas-pan-dynamic-rendering/{prd,design,implement}.md` | 历史设计：implement.md 第 10/13/14/15/16/17 步为估速与"不叠加"的演变 |

### 1.1 速度数据通路（决定了估速器看到什么）

```
手指 → 触摸屏报告(60/120/240 Hz, 静止不报) → RTS/WM_MOUSEMOVE 回调 [到达时刻 QueryPerformanceCounter 作为 snapshot.qpc]
     → PublishMove: 每触点单槽 "latest wins"（CI:775-809；RTS Packets 只取 lastPacket，RTS:1417）
     → 绘制线程每帧 (target_fps ∈ {30,60,120,240}，默认 120，见 Ctl:7523-7531 的自测断言；WaitForFrameDeadline 等到 1000/fps−work，Ctl:18572-18582)
       updateCanvasNavigation(nowQpc) 读每触点最新 snapshot，sequence 未变则跳过（Ctl:13571-13574）
     → 质心 centroidDelta → UpdateCanvasPan(inputQpc = max(本帧 snapshot.qpc)) → AppendVelocitySample / EstimateVelocity
```

要点：
- 估速器**每帧至多一个样本**（帧率 = 采样率），样本时间 = 该帧最新事件的**到达时刻**，不是设备时间。
- Up 终态写入同一槽（`PublishUp`→`Close`，CI:811-817），覆盖尚未被消费的最后几个 Move。
- 多指：质心用各触点"各自最新"的位置，但样本时间取 `max(qpc)`（Ctl:13588-13589、13653-13654）→ 位置与时间不属于同一时刻。
- Win32 鼠标兜底坐标是整数像素（Win:503-504）。

### 1.2 估速与释放的精确逻辑（移植对象）

| 函数 | 位置 | 行为 |
|---|---|---|
| `ClampVelocity` | Nav.cpp:26-33 | 24000 DIP/s（Nav.cppm:20）等比缩放 |
| `ResetVelocitySamples` | Nav.cpp:35-44 | 在 `inputQpc`（Down 时刻）放入零点样本 `(qpc,0,0)` |
| `AppendVelocitySample` | Nav.cpp:46-70 | `qpc<=末样本qpc` 丢弃（L50-51）；容量 24（L52-57）；裁到 100 ms 窗口（L60-69） |
| `EstimateVelocity` | Nav.cpp:72-102 | **一次线性拟合**；`count<2` 无速度（L75）；`denominator<=1e-12` 无速度（L98）；**≥2 个样本（含零点）即输出** |
| `UpdateCanvasPan` | Nav.cpp:370-409 | 仅 Move 且 `positional` 才更新；首个有效 Move：`hasNewMove=true` 并清候选（L382-385）；样本只在 `inputQpc>末样本qpc` 时追加，否则位置仍跟手但位移**不进样本**（L388-396） |
| `EndCanvasPan` | Nav.cpp:426-459 | `hasNewMove`→`selected=Clamp(direct)`（L429-433）；否则才用候选（L434-443）；年龄 `>100 ms` 清零（L451-453）；`≥5 DIP/s` 即启动惯性（L458） |
| `StepCanvasPanInertia` | Nav.cpp:472-501 | 常减速 6000（笔靠近 12000），梯形积分，`dt` 上限 50 ms（L477），`<5 DIP/s` 停 |
| 控制器释放 | Ctl:13845-13908 | `age = panReleaseQpc − lastPanInputQpc`（`lastPanInputQpc` = 最后一个速度样本 qpc，Ctl:13714-13715；Down 时置 `down.qpc`，Ctl:12201） |

### 1.3 历史（"已经失败过的修法"）

| 提交 | 日期 | 内容 | 与本问题关系 |
|---|---|---|---|
| `04081c6e` | 08-12 | 初版：`directVelocity = contentDelta/deltaSeconds`（逐帧差分）+ **120 ms 线性衰减的 inheritedVelocity 混合**（同时叠加到位置与速度） | 唯一一次"接续"；权重 `remaining/0.120` 随 `deltaSeconds` 递减，120 ms 后为 0，而一次有意的滑动通常 100–300 ms ⇒ 释放时权重≈0–0.2 |
| `bafb58f4` | 08-12 | 增加 `CanvasPanReleaseAgeSeconds` | 100 ms 释放年龄门限的来源 |
| `7b8bdd30` | 08-13 | 去掉 Windows `IInertiaProcessor`，引入 24 样本/100 ms 线性拟合 | 本文分析的估速器 |
| `281c7400` | 08-13 | `ResetCanvasPanVelocitySamples` 时间基准不倒退（`max(inputQpc,lastUpdateQpc)`） | H1 的现状来源 |
| `a2277444` | 08-13 | 删除"残余动量推动位置"，增加 `topologyReleaseVelocity` | 修"画布被旧速度推着走"，仍混合速度 |
| `e4ddd5c9` | 08-14 | **删除全部混合**；旧速度改为一次性 `releaseVelocityCandidate`，首个有效 Move 永久清掉；新增 `hasNewMove/selectedReleaseVelocity` | 现状。spec 明文固化"绝不叠加" |

---

## 2. 假设排序（每条：位置 → 机制 → 数值证据 → 复现）

指标：`rel/true` = 释放速度 ÷ 手指真实抬起速度；`glide/ideal` = 仿真滑行距离 ÷（同模型用完美速度的滑行距离 `v²/12000`）；`peak/true` = 手势期间 `directVelocity` 峰值 ÷ 真实速度。N 见各表（固定种子）。

### H1（严重，多指）触点数量变化后的基线错位 → 2 样本高估

- **位置**：`Ctl:11865-11890`（`rebuildPanContactBaseline` 用 `contact.snapshot.position`，即**上次消费**的位置，作为新基线；L11887-11889 以 `max(inputQpc, lastUpdateQpc)` 为时间零点）；`Ctl:13768-13772`（拓扑变化触发）；`Nav.cpp:352-368`；`Ctl:12209-12212`（加指同理，`down.qpc` 作零点）。
- **机制**：新零点样本 = `(T_u, pos_at_T_a)`，其中 `T_u` 是被抬起手指的 Up 到达时刻、`T_a` 是剩余手指**上次被消费**快照的时间（`T_a≤T_u`）。剩余手指下一次报告在 `T_b`：速度 = `v·(T_b−T_a)/(T_b−T_u)`，当 `T_u→T_b` 时无上界。仅 2 个样本时（`Nav.cpp:75` 允许）这就是 `directVelocity`；若此时抬起，`EndCanvasPan` 直接取它（`hasNewMove=true` 分支，Nav.cpp:429-433）。
- **确定性复现**（`sim/repro_topology_spike.py`，纯移植、无随机）：双指恒速 3000 DIP/s，A 在 236 ms 抬起，B 最后被消费的位置在 231 ms：重置后首个 Move（239 ms，位移 24 DIP）速度 = `24/0.003 = 8000 DIP/s = 2.67×`；再来一个样本后抬起，释放速度 `4082`（`1.36×`），滑行 `1389 DIP` vs 正确 `750 DIP`。
- **Monte-Carlo**（`results_current2.txt` G2，二指恒速 3000 DIP/s，第二指在第一指之后 U(0,60) ms 抬起，N=1500）：

| 触摸报告率 | rel/true med / p90 / p99 / max | P(>1.5×) | P(>2×) | P(>3×) | glide/ideal p99 / max |
|---|---|---|---|---|---|
| 60 Hz | 1.04 / 3.33 / 4.21 / 5.61 | 13.3% | 11.5% | 10.9% | 17.7 / 31.5 |
| 120 Hz | 1.04 / 1.34 / 2.10 / 2.81 | 7.0% | 5.1% | 0 | 4.4 / 7.9 |
| 240 Hz | 1.00 / 1.04 / 1.18 / 1.56 | 0.1% | 0 | 0 | 1.4 / 2.4 |

  按抬起间隔分桶（120 Hz，N=4000）：`5–10 ms` 桶 `P(>2×)=30.4%`、`10–15 ms` 桶 `23.2%`（最大 2.81×）；`≥15 ms` 为 0。**这正是人做"双指甩"时两指抬起的典型间隔。**
  `results_current.txt` G：stagger 8 ms 时 `P(>2×)=36.4%`；stagger≥8 ms 时 `peak/true` 最大 2.8–4.2（拓扑重置后的瞬时 `directVelocity`，影响 `ComputeCanvasPredictionOffset`/tile 预测，Nav.cpp:576-595）。
- **渲染循环帧率相关**（`sim/results_compare_fps.txt`，120 Hz 触摸、二指 U(0,60) ms、N=800）：循环越快于报告率越严重——循环 240/120/60/30 fps：`P(释放>1.5×)` = 11.5% / 5.8% / 0.5% / 0%，最大 `2.81× / 2.82× / 1.52× / 1.19×`。原因：循环快于报告率时相邻帧分别只消费到不同手指的新报告，质心/时间错配更严重；`target_fps` 档位为 30/60/120/240（默认 120）。
- **次要**：拓扑帧里 `velocityPosition` 对 Up 触点不更新（Ctl:13583-13590 只在 Move 更新；Up 走 L13599-13615），该帧速度样本只含被抬起手指一半位移（轨迹 `cdelta=24.85 vdelta=12.40`）。
- **在单指/鼠标白板拖动模式不触发**；但 PRD 要求保留多指质心，故必须修。

### H2（放大器，恒在）距离 ∝ v²，上限 24000

- **位置**：`Nav.cppm:20-21`；`Nav.cpp:472-501`（常减速 6000）。
- **数值**（`results_current2.txt` Q）：

| v (DIP/s) | 滑行 (DIP) | 1080p 屏宽(1920) | 4K@150%(2560) | 停止时间 |
|---|---|---|---|---|
| 1500 | 188 | 0.10 | 0.07 | 0.25 s |
| 3000 | 750 | 0.39 | 0.29 | 0.50 s |
| 6000 | 3000 | 1.56 | 1.17 | 1.00 s |
| 12000 | 12000 | 6.25 | 4.69 | 2.00 s |
| 24000 | 48000 | 25.0 | 18.8 | 4.00 s |

  误差放大：1.5×→2.25×距离；2×→4×；2.77×（H1 典型）→7.7×；5.6×（H1 60 Hz 最大）→31×。24000 DIP/s ≈ 6.3 m/s（96 DIP/in），已超过绝大多数真实手指速度，作为"上限"几乎不起保护作用。
- 对照：Chromium 桌面触摸 `PhysicsBasedFlingCurve` 也是二次（`kDefaultPhysicalDeceleration=2.7559e-5 inch/ms²`，≈2646 px/s² @96ppi），但**距离被限制在视口的 3 倍**（`kDefaultBoundsMultiplier = 3`）且时长 ≤ 2 s（`kMaxCurveDurationForFling`）。Draw3 的无限画布没有这个限幅。

### H3（严重，鼠标/极短手势）Down 零点 + 2 样本出速度 + 无最小时间跨度

- **位置**：`Nav.cpp:35-44`（零点在 `down.qpc`）、`72-102`（`count<2`/`den<=1e-12`，一个 100 µs 的间隔就足以出速度）、`379-398`；`Ctl:12186`（`BeginCanvasPan(.., down.qpc)`）、`Ctl:12204`。
- **机制**：样本时间是**回调到达时刻**。窗口线程/RTS 线程停顿后，Down 与第一个合并 Move 会在 ~0.1 ms 内背靠背派发，但位置已走了整个停顿期。`Δ/0.15 ms` 直接顶到 24000 clamp（`first-emit/true = 8.00`，`results_current2.txt` M1）。
- **数值**（鼠标，按下时已在以 3000 DIP/s 移动；Down+首个 Move 间隔 0.15 ms；N=1500）：

| 拖拽时长 | first-emit/true | 释放 rel/true med / p99 / max | P(释放≥24000 clamp) | 滑行 p99 / max |
|---|---|---|---|---|
| 16 ms | 8.00 | 1.25 / 8.00 / 8.00 | 15.1% | 48000 / 48000 DIP |
| 24 ms | 8.00 | 1.13 / 1.56 / 8.00 | 0.5% | 1822 / 48000 |
| ≥36 ms | 8.00（仅瞬态） | ≤1.32 | 0 | ≤1298 |

  触摸从静止落下时首个位移很小，高估较温和（极短 24 ms 甩动：`first-emit 1.44–1.58×`，`results_current.txt` D）。此项的**主动速度**尖峰（8×）会驱动 tile 预测范围（`Nav.cpp:576-595`，150 ms × 24000 = 3600 DIP 的预取带），是"卡顿"候选之一。
- 另：`Nav.cpp:388-396`——同一 tick 的 Down 与 Move（1 ms 量化时常见）`inputQpc<=末样本qpc`，样本被丢弃但画布位置已跟随；该段位移永远不进样本（低影响，仅少一个样本）。

### H4（恒在）线性拟合滞后：停手幻影惯性 + 轻甩低估

- **位置**：`Nav.cpp:72-102`（100 ms 窗口一阶拟合）、`Nav.cpp:451-453` + `Ctl:13854-13856`（年龄门限 100 ms）。
- **机制**：对加速/减速的位置曲线，一阶拟合给出窗口平均速度，相对"末端速度"滞后约 `a·W/2`（`W=100 ms`）。加速抬起 → 偏小；减速停手 → 偏大；停手后 100 ms 内抬起仍照常给惯性（Android 用 40 ms：`ASSUME_POINTER_STOPPED_TIME`，`VelocityTracker.cpp:83`）。
- **偏小**（`results_current.txt` A2/B/C/D，N=500）：

| 场景 | rel/true | glide/ideal |
|---|---|---|
| flick_sin 4000（120 ms，抬起时速度平台）@60/120/240 Hz | 0.72 / 0.70 / 0.73 | 0.52 / 0.49 / 0.53 |
| flick_lin 4000（80 ms，加速中抬起）@60/120/240 Hz | 0.42 / 0.47 / 0.47 | 0.17 / 0.22 / 0.22 |
| 50 ms 极短甩动，1–3 ms 量化 @120 Hz | 0.42 | 0.18 |

  `P(rel<0.7×)`：flick_sin@120 Hz = 48.2%，flick_lin = 100%。滑行只有应有的 17–60%。
- **偏大（幻影惯性）**（真实释放速度 = 0；`results_current2.txt` F2，120 Hz）：

| 巡航 | 减速时长 | 停留 | 释放速度 med / p99 | 幻影滑行 DIP med / p99 | P(滑行>50) |
|---|---|---|---|---|---|
| 1500 | 100 ms | 50 ms | 774 / 795 | 50 / 53 | 47% |
| 3000 | 100 ms | 0 | 1780 / 1818 | 264 / 276 | 100% |
| 3000 | 100 ms | 50 ms | 1301 / 1357 | 141 / 154 | 100% |
| 3000 | 100 ms | 95 ms | 1301 / 1358 | 141 / 154 | 100%（停了 95 ms 仍滑） |
| 6000 | 60 ms | 0 | 4724 / 5135 | 1860 / 2198 | 100% |
| 6000 | 100 ms | 50 ms | 2602 / 2709 | 564 / 612 | 100% |

  即"精准放置"类拖动（快速接近→停住→抬起）几乎每次都会多滑一截。**这是"偶发变大"里最容易被用户感知的来源之一，且不需要任何异常输入。**
- **100 ms 年龄门限是悬崖**（`results_current.txt` E，Up 比最后一个 Move 晚到 N ms，手指实际一直在动）：N=20…95 ms → `rel/true 0.78`（恒定全速）；N=105/140 ms → `0`（惯性完全消失）。

### H5（中，结构）输入层丢样本 + 到达时间戳

- **位置**：`CI:775-809`（单槽覆盖；`CI:780-784` 写锁竞争直接丢弃该 Move）、`RTS:1417/1433-1434`（每回调只取最后一个包、`QueryQpc()` 作时间）、`Win:501-503`。
- **机制**：burst / 线程停顿会把多个设备报告压成一个"晚到但位置跨度大"的样本；Up 也会吞掉最后几个 Move。
- **数值**（`results_compare.txt` §4，N=400，对比现有/候选）：当前估速器在这些场景只是更偏小（burst_p=0.30 @120 Hz: `0.70`），但对**少样本窗口**的候选（LSQ2）`burst` 下 `p99 1.43 / max 1.71`、帧停顿（5%/帧，20–50 ms）下 `8.0%` 试验因为"Up 吞掉最后 Move → 年龄过大"而丢失速度；给估速器喂**每个事件**（producer 侧小环形缓冲，`lsq2+ring`）：burst `max 1.22`、停顿 `丢失 0%`、36 ms 极短甩动 `0.99–1.10×`（无环形缓冲时 58.2% 无速度）。
- 与 H1 同源：多指质心的"位置取各触点最新、时间取 max(qpc)"（Ctl:13588-13589）使时间/位置错配，是二指 `peak/true` 在 240 Hz 也达 1.6–2.1 的原因（`results_compare.txt` §7，`cur`）。

### H6（中）帧停顿时的单步位移尖峰

- **位置**：`Nav.cpp:477`（dt 上限 50 ms）、`Ctl:13789-13801`（`dt=nowQpc−lastNavigationQpc`，`dt<=0` 才回退 `1/target_fps`）、`Ctl:16757`（`graphicsRecoveryPending_` 时 `continue` 跳过本帧导航）。
- **数值**（`results_current.txt` I，每帧 2% 概率停顿 20–50 ms，120 Hz 循环）：3000 DIP/s 单步最大 `141 DIP`（正常一帧 25）；12000 DIP/s 单步最大 `598 DIP`（正常一帧 100，50 ms cap = 600）。时间被 cap 丢弃，随后不补回——可见为"一跳"。非速度尖峰，但表现为"突然多跳一段"。

### H7（低）其它已验证的小问题

- 点按（无 Move）后 `EndCanvasPan` 的 `NoNewMoveResidual`（Nav.cpp:434-443）会在 100 ms 内**复活**旧速度（按 Down 时刻的残余，且未扣除按下期间已衰减的部分）。
- `Ctl:13659` 等 `velocityDelta` 在拓扑帧只含半步（见 H1 次要）。
- 位置/速度均不使用设备时间戳；`GetPointerInfo` 的 `POINTER_INFO.PerformanceCount`（Win32 文档的 QPC 输入时间）可用于 WM_POINTER 路径，仓库内未用，本文未验证其在 RTS 路径的可得性。

### 已验证"不是"来源（避免重复排查）

- 稳态匀速：`rel/true 1.00–1.02`（60/120/240 Hz、1500–12000 DIP/s、30/60/240 fps 循环）。
- 1–3 ms 时间戳量化、鼠标 125–1000 Hz + 8/16 ms 消息合并：单独不产生高估（均在偏小侧 0.70–0.80，由 H4 引起）。
- "Up 不进样本"（Nav.cpp:13599-13615 / `UpdateCanvasPan` 的 `updateVelocity=false`）逻辑自洽；`hasNewMove` 分支无内部矛盾。

### 排查提示（拿到真机日志后如何对号入座）

`LogCanvasPan` 在显式 RTS diagnostics 下输出（spec L869）。重点字段：
- H1：`pan-lifecycle`（拓扑变化）紧邻 `pan-anomaly ... samples=2 ... dt-ms<5`，随后 `pan-release ... samples=2/3 direct≈2–5× 之前的 direct`。
- H3：`pan-begin` 后首个 `pan-move`/`pan-anomaly` 出现 `samples=2 dt-ms<1 direct≈24000`。
- H4：`pan-release ... sample-span-ms≈100 age-ms∈[30,99] speed>0`，而最后几条 `pan-move content-delta` 很小。
- H6：`inertia-first-step ... dt-ms>16`、`inertia-stop`；`pan-release age-ms` 的分布可直接标定"抬起延迟"分布（§4.2 的门限参数应据此校准）。

---

## 3. 为什么现有设计无法叠加惯性（阻断状态/分支逐条）

| # | 位置 | 行为 | 对叠加的影响 |
|---|---|---|---|
| 1 | Nav.cpp:192-201；Ctl:12155、12162-12164 | 惯性中的首个触点：`inertiaCandidate_ = inertiaActive && batchAllowsPan_` → disposition=`PanCandidate`（不是 Pan） | 控制器只对 `Disposition==Pan` 的触点采样（Ctl:13583、13627）⇒ 单指/鼠标式滑动**永远没有速度样本** |
| 2 | Nav.cpp:219-232 | 只有 ≤180 ms 内的第二指才 `beginPan` | 叠加需要双指 |
| 3 | Nav.cpp:251-263、292-297；Ctl:13777-13778 | 候选 180 ms 超时 → 首指 `Suppressed`、`InertiaBrakeRequested()` → 12000 DIP/s² | 旧惯性被加倍制动，新滑动整体丢弃；仿真：单指落下后新滑动贡献 `0`，旧滑行额外 `79–781 DIP`（间隔 500…80 ms）；若指在 180 ms 前抬起则 `OnTouchUp` 直接 `Reset`，惯性不停也不接 |
| 4 | Nav.cpp:338-347（`BeginCanvasPan`） | `velocity={}`、`directVelocity={}`、`inertiaActive=false`、`hasNewMove=false`；旧速度只进 `releaseVelocityCandidate(Residual)` | 旧惯性在落指瞬间被清零，速度不连续 |
| 5 | Nav.cpp:382-385 | 首个 `positional` Move：`hasNewMove=true`，候选与来源清零 | "第一条有效 Move 永久切断旧惯性" |
| 6 | Nav.cpp:429-433 | `hasNewMove` ⇒ `selected = Clamp(directVelocity)` | 释放速度 = 纯新速度 |
| 7 | Nav.cpp:434-443、451-453 | 仅无 Move 时用候选，且需 `age≤100 ms` | 唯一"保留旧速度"路径是点按复活（H7） |
| 8 | Nav.cpp:352-368 | 拓扑重置清 `direct/hasNewMove`，只在无后续 Move 时保留一次性 Topology 候选 | 多指拓扑变化中也无法累计 |
| 9 | spec L873、L902、L914；archive prd/design | "绝不叠加旧速度"写成契约和 Bad Case | 需要改规范，不只是改代码 |

仿真（`results_stacking.txt`，同向 v1=v2=4000 DIP/s，第二次滑动在第一次释放后 `gap` 抬指）：

| gap | 落指时残余 | 第二次测得速度 | 第二次起滑速度 | 第二次滑行 | 相对单次滑行(1333) |
|---|---|---|---|---|---|
| 80 ms | 3495 | 4013 | 4013（`new-direct`） | 1371 | 1.03× |
| 150 ms | 3097 | 4013 | 4013 | 1368 | 1.03× |
| 300 ms | 2196 | 4013 | 4013 | 1360 | 1.02× |
| 500 ms | 993 | 4013 | 4013 | 1350 | 1.01× |

结论：完全不叠加。历史上唯一的"接续"（120 ms 线性衰减混合）按设计在 120 ms 后归零，释放时（通常 150–250 ms 后）贡献≈0，且混合项同时进位置会产生"画布被推着走"（`a2277444` 去除）——所以多次修补都没有真正解决，因为问题是**模型不存在"跨手势的速度状态"**，不是某个分支写错。

---

## 4. 替换模型提案

仿真对比脚本：`sim/proposed.py`、`sim/compare.py`、`sim/run_stacking.py`；参数均为待真机标定的候选。

### 4.1 速度估计器（每指独立，Android/Compose 同源）

- **每个触点一个 `CanvasPanVelocityTracker`**，样本为该触点**绝对位置**（不是质心累计位移）：环形缓冲 20；追加规则：
  - 与末样本时间差 `≤ 2 ms` ⇒ 合并（保留较新位置与较新时间，避免 100 µs 级"burst 对"）；
  - 与末样本间隔 `> max(40 ms, frameDt + 16 ms)` ⇒ 清空历史（手指曾静止；Android `ASSUME_POINTER_STOPPED_TIME = 40 ms`、Compose `AssumePointerMoveStoppedMilliseconds = 40`；加 `frameDt` 项是为了让渲染循环自己的停顿不被误判为"手指停了"，仿真里 5%/帧停顿下用到）；
  - 丢弃比最新样本早 `> 100 ms` 的样本（Android `HORIZON=100 ms`、Compose `HorizonMilliseconds=100`）。
- **估计**：样本数 `≥3` 且跨度 `≥20 ms` 才输出；二次最小二乘（LSQ2），取**最新样本时刻**的一阶导数（Android 默认 `LSQ2`，`DEFAULT_STRATEGY_BY_AXIS` X/Y；Compose `calculateLeastSquaresVelocity`）；不满足条件返回"无效"而不是 0 或两点差分。
- **不重置**：加指/减指只重基线质心位置（保持画布不跳），**不清**任何触点的估速器（根除 H1）。
- **速度上限**：`12000 DIP/s`（≈3.2 m/s @96 DIP/in；Android `ViewConfiguration` 默认最大 fling 速度约 8000 dp/s ≈ 4800 DIP/s，此值凭记忆，未在本次核实）。
- **最小释放速度**：`250 DIP/s`，以下不起滑（仿真：LSQ2 在 `停手≥50 ms` 的幻影速度 p99=171–184，被此门限滤掉）。

估速器选型证据（`results_compare.txt` / `results_compare_variants.txt`，N=400；rel/true 的 med / p99 / max，最优者加粗）：

| 场景 | 现有(cur) | lsq1w50(50 ms 线性) | **lsq2** | impulse(Android Impulse) |
|---|---|---|---|---|
| 匀速 3000 @120 Hz | 1.00/1.01/1.01 | 1.00/1.03/1.03 | **1.00/1.05/1.06** | 1.03/1.24/1.49 |
| flick_sin 4000 @120 Hz | 0.72/0.78/0.81 | 0.92/0.96/0.97 | 1.11/1.16/1.16 | **1.02/1.23/1.47** |
| flick_lin 4000，80 ms | 0.47/0.49/0.50 | 0.63/0.72/0.72 | **0.93/1.01/1.01** | 0.83/1.12/1.14 |
| flick_lin 50 ms，q=2 ms | 0.42/0.51/0.52 | 0.42/0.59/0.66 | **0.85/1.01/1.03** | 0.80/1.19/1.21 |
| burst_p=0.30 @120 Hz | 0.70/0.87/0.90 | 0.90/1.65/2.34 | **1.08/1.43/1.71** | 1.02/3.00/3.00（peak 10.8×） |
| 二指甩 U(0,60) ms @60/120/240 Hz | 1.04/5.55/5.60；1.03/2.11/2.82；1.00/1.19/1.56 | 1.00/1.03/1.04 等 | **1.00/1.04/1.06；1.00/1.03/1.04；1.00/1.03/1.04** | 1.01/1.08/1.17；1.00/1.20/1.34；… |
| 减速停手：3000/100 ms 减速/停 50 ms（真值 0） | 释放 1301 DIP/s，滑 141 DIP | 361，37 | **124，6** | 261，22 |
| 同上，停 95 ms | 1301，141 | 0，0 | **0，0** | 0，0 |

结论：LSQ2 在"手势形状偏差（加速/减速/平台）"与"拓扑/触点变化"两类问题上全面最好；Impulse 在平台型甩动最准，但在本项目的**到达时间戳**（抖动/burst）下尾部差（burst `p99 3.0`、`peak 10.8×`），故不选；LSQ2 的主要偏差是平台型甩动 **+12–16%**（`avg21` 混合版降到 +1–6%，但加速甩动退回 0.79；`lsq2b6` 折中 +6–10% / 加速 0.86；可后续调参，不影响架构）。

**强烈建议（可选的输入层改动，仿真上界）**：producer 侧给每触点加一个小环形缓冲（≤16 个 `(qpc,x,y)`），消费端按序灌给 tracker，而不是每帧只读"最新"。`lsq2+ring`：burst `p99 1.20 / max 1.22`（无环 1.43/1.71）、帧停顿丢速度 `0%`（无环 8.0%）、36 ms 极短甩动 `0.99–1.10×`（无环 58.2% 无速度）、停手幻影 `0–117 DIP/s`。若不做环形缓冲，需接受上述尾部并保留 `frameDt` 断口放宽。

### 4.2 释放门限（取代 100 ms 悬崖）

`gate(age) = 1`（`age≤30 ms`）→ 线性 → `0`（`age≥70 ms`）；`age = Up到达 − 最后一个样本到达`。理由：触摸屏对静止手指不报点，"最后 Move 后 N ms 才抬起"与"抬起延迟 N ms"在样本层面不可区分；Android 取 40 ms 清零。取 30/70 是折中：精准放置不再多滑，面板抬起延迟 ≤30 ms 的设备不受影响。**此处必须用真机 `pan-release age-ms` 分布校准**（见 §2 排查提示）。仿真：Up 延迟 20 ms：全速；50 ms：`0.56–0.60×`；≥70 ms：0。

### 4.3 触点拓扑

- 释放速度 = 最后一次抬起时刻"仍在 Pan 的触点"和"80 ms 内刚抬起的触点"各自（带 gate）速度的**均值**；有效样本不足的触点不参与。
- 质心位置继续用于画布跟手（保持现有 `previousPanCentroid` 重基线），但速度完全由每指 tracker 产生，彻底避免 H1 与 Ctl:13588-13589 的时间/位置错配。
- 仿真（二指甩 U(0,60) ms，`compare.py` §7）：`lsq2` 释放 `p99 1.04 / 1.03 / 1.04`、`max 1.06 / 1.04 / 1.04`（60/120/240 Hz）。

### 4.4 惯性曲线与停止

- 闭式积分（`dv/dt = −v/τ − c`，每帧精确，与帧率/卡顿无关）：`A=v+cτ`，`v(t+dt)=A·e^{−dt/τ}−cτ`，`Δs=Aτ(1−e^{−dt/τ})−cτ·dt`；总距离 `d(v0)=v0τ − cτ²·ln(1+v0/(cτ))`，停止时间 `t*=τ·ln(1+v0/(cτ))`；`v<20 DIP/s` 停。
- 推荐 `τ=0.30 s, c=1000 DIP/s²`，速度上限 12000，`dt` 上限 100 ms（闭式积分后上限仅防休眠恢复）：

| v0 (DIP/s) | 现有 `v²/12000`（t_stop） | 提案（t_stop） | 备选 τ=.35 c=600 |
|---|---|---|---|
| 500 | 21（0.08 s） | 62（0.29 s） | 85 |
| 1000 | 83（0.17 s） | 168（0.44 s） | 221 |
| 3000 | 750（0.50 s） | 684（0.72 s） | 850 |
| 6000 | 3000（1.00 s） | 1526（0.91 s） | 1851 |
| 12000 | 12000（2.00 s） | 3266（1.11 s） | 3901 |
| 24000 | 48000（4.00 s） | 6804（1.32 s） | 8051 |

  对误差的敏感度：2× 速度误差 → 距离 `2.2×`（现有 4×）；2.77× → `3.2×`（现有 7.7×）。3000 DIP/s 的"网页级"手感基本不变（684 vs 750），低速略长、高速显著收敛——**需要产品确认低速手感**（想保留旧的"克制"低速，可提高 `c` 或抬高最小释放速度；`d` 公式可直接重算）。
- Chromium 桌面触摸对照：`PhysicsBasedFlingCurve` 距离上限 3×视口、时长 ≤2 s；`FlingCurve` 另有指数项（`kDefaultAlpha=-5.70762e+03, Beta=1.72e+02, Gamma=3.7`）。本提案取"指数+摩擦"以得到有限停止时间。

### 4.5 Fling boost（叠加规则，Chromium `FlingBooster` 语义）

源码要点（`ui/events/blink/fling_booster.cc`，2026-11-06 取自 main）：
- 条件：同一输入源/修饰键；落指后（`GestureFlingCancel` 起算）**每次**同向 scroll update 把截止时间顺延 `kFlingBoostTimeoutDelay = 50 ms`；fling_start 时 `timestamp ≤ cutoff`、新旧速度点积 `>0`、`current_fling_velocity`（衰减后的当前速度）`≥ 350 px/s`、新速度 `≥ 350 px/s`（`kMinBoostFlingSpeedSquare = 350²`）。
- 提升量：`velocity += previous_fling_starting_velocity_`（**上一次起滑速度，不是衰减后的残余**；每次提升后的结果成为下一次的 previous，可连续提升）。
- 取消：scroll 与 fling 反向（点积 ≤0）、scroll 速度 `<150 px/s`（`kMinBoostTouchScrollSpeedSquare = 150²`）、超时、输入源变化、`FlingCancel.prevent_boosting` ⇒ `Reset()`。

提案（单位 DIP，速度来自 §4.1）：
1. 落指时若惯性仍在：**立即停惯性，画布跟手**（保持现有"抓住"手感，Chromium 也在落指时 FlingCancel），同时把 `{prevStart, residual, grabTime}` 交给 booster，`armed = |residual| ≥ 350`。
2. 手指移动期间：累计位移 ≥ 8 DIP（触摸 slop，避免落指抖动）后判方向——与 `prevStart` 点积 ≤0 ⇒ 解除 armed；瞬时速度 `<150` ⇒ 解除；每次满足条件把 `cutoff` 顺延 `max(50 ms, 2×frameDt)`（避免渲染停顿误解除）。
3. 释放：`armed && release ≤ cutoff && dot>0 && |v_new| ≥ 350` ⇒ `v = v_new + prevStart`，`|v| ≤ 18000`（1.5×`V_MAX`）；否则 `v = v_new`。任一路径结束后 `prevStart = v`。
4. 反向 ⇒ 不叠加，`v = v_new`（旧惯性在落指时已停）。
5. 点按（无 Move）⇒ 停惯性、**不复活旧速度**（删除 `NoNewMoveResidual`/`TopologyNoNewMove` 的"复活"语义，spec L873/L901/L902 需同步改写；若产品要保留"抓住再放手继续滑"可显式加开关）。
6. 页面切换、模式切换、笔 hover/落笔、输入源不同（触摸 vs 鼠标）、窗口 Resize ⇒ `booster.Reset()`。

仿真（`results_stacking.txt`，同向 v1=v2=4000，`sim` 的 LSQ2 + 闭式曲线）：

| gap | 落指残余 | v1 起滑 | 无 boost: v2 / 滑行 / 总位移 | **boost（+prevStart）: v2 / 滑行 / 总位移** | boost(+residual): v2 / 总位移 |
|---|---|---|---|---|---|
| 80 ms | 2953 | 3994 | 4098 / 988 / 3115 | **8093 / 2128 / 4255** | 7051 / 3955 |
| 150 ms | 2305 | 3994 | 4098 / 988 / 3289 | **8093 / 2128 / 4430** | 6403 / 3943 |
| 300 ms | 1280 | 3994 | 4098 / 988 / 3552 | **8093 / 2128 / 4692** | 5378 / 3913 |
| 500 ms | 511 | 3994 | 4086 / 984 / 3719 | **8081 / 2124 / 4859** | 4598 / 3862 |

  反向：`v2 = −4098`，滑行 `−988`，与第一次无关。连续同向 4000 DIP/s 甩动（每次间隔 150 ms）：起滑速度 `4000 → 8000 → 12000 → 16000 → 18000(封顶) → 18000`，单次滑行 `960 → 2101 → 3266 → 4440 → 5030(封顶)`——有界。
  **待产品确认**：叠加"上次起滑速度"（Chromium，越晚抓越"赠送"，总位移随 gap 增大）还是叠加"落指时残余"（动量守恒式，总位移≈恒定 3.9k，更保守）。两者实现仅差 `add_residual` 一行。

### 4.6 笔靠近立即停

`StepCanvasPanInertia(..., penInRange)`（及控制器 `penBrake`，Ctl:13776）改为：`penInRange && !suppressPenUntilRelease` ⇒ `StopCanvasPan(motion)`，本帧返回零位移，并 `booster.Reset()`。现状 12000 DIP/s² 不是急停（`results_current2.txt` P）：

| 笔靠近时速度 | 停止时间 | 额外滑行 |
|---|---|---|
| 1500 | 125 ms | 94 DIP |
| 3000 | 250 ms | 375 DIP |
| 6000 | 500 ms | 1500 DIP |
| 12000 | 1000 ms | 6000 DIP |

（`InertiaBrakeRequested` 的 12000 候选超时制动随单指拖动入口一起删除——拖动模式下首个触点直接成为 Pan，不再有 `PanCandidate`。）

### 4.7 纯 CPU 单测清单（数值来自仿真，留余量）

建议落点：把 `Draw3.CanvasNavigation.*` 加入 `InkeysHeadlessTests`（目前未编译，见 `move-mode-input-routing.md` §0-6），测试用确定性 LCG 生成与 `sim/streams.py` 等价的流。

估速器（`Tracker`）
1. 恒速 3000 DIP/s、8.3 ms 间隔、±1 ms 抖动、噪声 0.15：`|v/3000−1| ≤ 0.08`（仿真 p99 1.05）。
2. 加速 `a=50000 DIP/s²` 抬起：`0.85 ≤ v/v_true ≤ 1.10`（现有 0.47–0.50）。
3. 平台型甩动：`v/v_true ≤ 1.20`（仿真 1.16）。
4. 少于 3 样本或跨度 <20 ms ⇒ 无效；**回归**：重置后 3 ms 的单个 24 DIP Move（`sim/repro_topology_spike.py`）不得输出 `8000 DIP/s`。
5. 两样本相隔 0.15 ms ⇒ 合并，`≤1.2×`，不触及 clamp。
6. 间隔 >40 ms 清空；`frameDt=60 ms` 的卡顿帧、手指持续运动 ⇒ 不清空。
7. 停手：减速到 0 后停 50 ms ⇒ 释放 `<250 DIP/s`（无惯性）；停 ≥70 ms ⇒ 恰为 0。
8. gate：age 20/30/50/70/95 ms ⇒ 1/1/0.5/0/0。
9. NaN/inf/倒退时间/重复 qpc ⇒ 无 NaN，状态不变。
10. `V_MAX` 等比缩放保持方向。

拓扑
11. 二指恒速、第二指在 0–60 ms 内（步长 1 ms）抬起、带抖动：`0.9 ≤ v/v_true ≤ 1.15`（现有最大 5.6×）。
12. 减指不清剩余触点 tracker（样本数不变）；加指的新触点 <3 样本时不参与均值、质心速度无尖峰。

惯性
13. 闭式距离：`d(3000)=684±1`、`d(12000)=3266±2`；dt 序列 {1/240,1/60,0.03,1/120} 总距离差 ≤0.5 DIP。
14. dt=0/负/NaN 安全；dt=100 ms 一步等于 12 步 8.33 ms（≤0.5 DIP）。
15. 速度单调不增、有限时间停止、不出现负速度。
16. 敏感度：`d(2v)/d(v) ≤ 2.5`（v∈[1000,9000]）；`d(18000) ≤ 5100`。
17. 笔靠近：`Step(..., penInRange=true)` 返回 `{0,0}`、`inertiaActive=false`、速度 0。

Booster
18. 同向、残余≥350、新≥350、在 cutoff 内 ⇒ `v = v_new + prevStart`，封顶 18000。
19. 反向（slop 后点积≤0）⇒ `v=v_new`，booster 解除。
20. 残余 <350 ⇒ 不叠加；cutoff 之后（停顿 >50 ms）释放 ⇒ 不叠加。
21. 落指抖动在 8 DIP slop 内的反向分量 ⇒ 不解除。
22. 连续 6 次 4000：`4000, 8000, 12000, 16000, 18000, 18000`。
23. 点按（Down/Up 无 Move）⇒ 惯性停止、不复活；输入源/页面/模式变化 ⇒ Reset。

集成（CPU 驱动 harness，复刻 `drivers.py`）
24. 两次同向滑动（gap 80–500 ms）：总位移 `≥1.25×` 无叠加情形且 `≤` 上限；二指甩 U(0,60) 的 `P(release>1.3×)=0`；停手放置 P(滑行>10 DIP)=0（`rest≥70 ms`）。
25. 与现有行为的**特征化测试**（先写，后随替换删除）：现有端口在二指 stagger 8 ms 下 `P(>2×)≈30%`；同向叠加 `glide2/single ∈ [1.0,1.05]`。

### 4.8 需要同步改写的 spec 条目

`runtime-and-rendering.md`：L870–873（Pan 入口、候选期、"绝不叠加"、残余候选）、L877（6000/12000/50 ms/梯形积分）、L879（`ResetCanvasPanVelocitySamples` 约束）、L898/L901/L902（验证矩阵）、L912（Good Case：3200 DIP/s → 0.53 s / 853 DIP）、L914（Bad Case）、L918（Tests Required）。

---

## 5. External References

- [Chromium `ui/events/blink/fling_booster.cc`/`.h`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/events/blink/fling_booster.cc) — 叠加条件与常量：`kMinBoostFlingSpeedSquare=350²`、`kMinBoostTouchScrollSpeedSquare=150²`、`kFlingBoostTimeoutDelay=50 ms`；提升 = `velocity += previous_fling_starting_velocity_`；反向/低速/超时/换输入源 Reset。（2026-11-06 抓取）
- [Chromium `physics_based_fling_curve.cc/.h`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/events/gestures/physics_based_fling_curve.cc) — `kDefaultPixelDeceleration=2300`、`kMaxCurveDurationForFling=2.0`、`kDefaultPhysicalDeceleration=2.7559e-5 inch/ms²`、`kDefaultBoundsMultiplier=3`（距离封顶 3×视口）。
- [Chromium `ui/events/gestures/fling_curve.cc`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/events/gestures/fling_curve.cc) — 指数+线性项 fling 曲线常量。
- [Android `libs/input/VelocityTracker.cpp`/`include/input/VelocityTracker.h`](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/libs/input/VelocityTracker.cpp) — `HORIZON=100 ms`、`HISTORY_SIZE=20`、`ASSUME_POINTER_STOPPED_TIME=40 ms`（Move 间隔与 UP 距最后事件均清零）、默认 X/Y 策略 LSQ2、Impulse 策略与"样本间隔过近会高估速度"的注释（Legacy `MIN_DURATION=10 ms`）。
- [androidx Compose `VelocityTracker.kt`](https://android.googlesource.com/platform/frameworks/support/+/refs/heads/androidx-main/compose/ui/ui/src/commonMain/kotlin/androidx/compose/ui/input/pointer/util/VelocityTracker.kt) — `HorizonMilliseconds=100`、`AssumePointerMoveStoppedMilliseconds=40`、`HistorySize=20`、`MinSampleSize=2`，默认 `Strategy.Lsq2`（Impulse 用于 differential 数据）。
- Win32 `POINTER_INFO.PerformanceCount`（输入 QPC 时间戳）——仓库未使用，未在本任务中验证。

### Related Specs

- `.trellis/spec/native/runtime-and-rendering.md:848-931` — Free Canvas Navigation And Predictive Recovery（契约要改写，见 §4.8）。
- `.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/research/move-mode-input-routing.md` — 白板移动模式输入路由、单指入口、模式切换钩子、测试编译现状。
- `.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/prd.md` — 任务范围（边界阻力等不在内）。

## Caveats / Not Found

- **无真机日志**：全部数值是合成输入下的 Monte-Carlo（固定种子，可复现）；到达抖动/基础延迟/burst 概率/抬起延迟/触摸报告率/QPC 频率（假设 10 MHz）都是假设。绝对概率（例如"5.1% 的二指甩 >2×"）不应当作线上发生率；机制与相对排序是可信的。**建议先开 RTS diagnostics 抓 `pan-release age-ms / samples / pan-anomaly`**，用来标定 §4.2 门限与 H1/H3 的真实触发率。
- 产品当前由 `kCanvasNavigationProductIntegrationEnabled=false`（Win.cppm:30）关闭，上述缺陷是在旧沙盒/同源内核上观察到的；Draw3 产品树里没有可运行的导航单测（`InkeysHeadlessTests` 不含该模块）。
- 用户的"突然变大"具体表现未知：H1（多指）、H4（停手幻影惯性，不需要异常输入）、H3（鼠标/极短手势）、H2（放大器）都可能；单指/鼠标白板拖动模式下 H1 不触发。
- 触摸"静止不报点"、抬起延迟分布、RTS `Packets` 一次回调含多包的比例，均未测量；`lsq2+ring` 是上界估计，producer 环形缓冲的实现成本（`ContactSnapshot` seqlock 之外再加 SPSC）未评估。
- LSQ2 对平台型甩动偏高 12–16%、对极短(<30 ms)手势拒绝出速度，是有意取舍，参数可调。
- 低速滑行手感（提案曲线比现有更长）、boost 叠加"起滑速度 vs 残余"、点按是否"抓住后继续滑"，是产品决策点。
- Chromium/Android 源码为 2026-11-06 抓取的 main 分支，常量以当时版本为准。
