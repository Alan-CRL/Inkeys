# Windows Ink 绘制延迟回归：对话、调查与实现交接

## 文档信息

- 日期：2026-08-08
- Trellis 任务：`.trellis/tasks/08-08-windows-ink-input-lag-regression`
- 当前任务状态：`in_progress`
- 实现 commit：`949752a fix: reduce Windows Ink contact wake latency`
- 本文目的：让后续开发者能够理解本轮需求如何收缩、根因判断依据、最终补丁边界、验证结果与剩余风险。

本文记录可复核的工程分析、证据、假设、排除项和决策权衡，不包含模型内部隐藏的逐步思维链。

## 1. 初始请求

用户最初提出：

> 进行一个调查。最近我进行了 Windows Ink 画笔限制帧率，和 RTS 触控板问题修复还有 RTS 优化。仔细查看近一周 commit，目前出现了一个问题需要调查。我发现 windows ink 在绘制的时候非常不跟手（和 0729a 相比），但是现在鼠标和触摸都是正常的。请你找出问题，但是本轮修复，需要打开任务。

随后补充了更具体的表现：

> 在最新的测试中发现，windows ink 不但跟手感下降，在追上手的时候还有抽帧的现象。

初始目标因此包含两个层面：

1. 调查为什么只有 Windows Ink Pen 相对 7 月 29 日基线明显增加拖尾。
2. 解释为什么墨迹追上笔尖时会出现大跨度跳跃或抽帧感，而 Mouse 和 Touch 正常。

用户同时要求本轮必须建立任务，不接受没有任务记录的直接修补。

## 2. 用户明确的输入模型

讨论中用户反复确认了以下设计前提，这些前提最终成为补丁边界：

- RTS 可以按设备原始频率持续回调，例如约 270 Hz。
- RTS `Packets` 只负责尽快解码并覆盖最新点，不应因为画布 120 Hz 限帧而阻塞。
- 绘制线程按约 120 Hz 读取当前最新点并交给模型；中间丢掉若干原始点是 latest-only 设计的正常结果。
- 问题不应被解释成“必须把全部 270 Hz 原始点送给模型”，也不应引入 history/ring buffer。
- 用户用一句话概括了核心约束：

> RTS 回调中不应该有任何关于限制帧率的阻塞。

因此，调查重点从“原始点是否丢失”转为“120 Hz 帧边界读到的是否真是最新点，以及 RTS 回调是否被无意义调度打断”。

## 3. 讨论如何逐步收缩范围

| 阶段 | 讨论焦点 | 最终结论 |
|---|---|---|
| 症状确认 | Pen 拖尾并在追赶时抽帧，Mouse/Touch 正常 | 优先调查 Pen 专有路径，而不是全局模型或 Presenter |
| 120 Hz 与约 270 Hz | 是否需要保留全部原始包 | 保持 batch last-point 与 latest-only；中间点可丢弃 |
| Pen cursor wake | 为什么 Pen 需要额外唤醒 | Hover/终态需要离散唤醒；活动 Contact 已有 120 Hz 帧，不需要每包 cursor wake |
| Hover 方案 | 是否持续约 270 Hz 渲染 Hover | 用户明确否定；保留当前“坐标变化才刷新”的节能行为 |
| Win7/Win8 输入 | 是否只依赖 RTS | 保留 RTS + 动态 Pointer API 分工；Win7 使用 RTS fallback |
| `WM_POINTER` Contact | 是否停止发布接触坐标 | 明确否定；Contact Eraser、倒转笔等可能显示光标，坐标必须继续发布 |
| RTS 顺序 | 只交换 cursor/Move 顺序是否足够 | 不足；0729 已有旧顺序，真正差异是严格 deadline 后 cursor wake 的语义变化 |
| state gate | 是否同时修改 `74c33fc` | 没有直接证据，留到最小补丁后仍复现时再调查 |
| 最终范围 | 是否加入诊断、测试 API 或模型调整 | 全部拒绝，只改两个生产位置并同步文档 |

## 4. 当前 Ink 输入设计

### 4.1 接触绘制

RTS 是当前真实绘制 contact 的输入来源：

```text
RTS callback
  -> 解码 batch 最后一个 packet
  -> ContactInputCoordinator 覆盖 latest snapshot
  -> 绘制线程按现有帧 deadline 读取 snapshot
  -> Stroke Modeler / prediction / render / Present
```

`Packets` 热路径保持 last-point、latest-only、无分配和不进入 D3D/模型。Mouse、Pen、Touch 的绘制 contact 最终都通过 coordinator 交给绘制线程。

### 4.2 Pen 瞬态 cursor

Pen cursor 坐标有两个发布来源：

- RTS：Down、Packets 和 InAirPackets 发布 Pen X/Y/QPC、倒转状态和 Contact 状态。
- Windows 8+ `WM_POINTERENTER/UPDATE`：发布 Pointer API 取得的 Pen 坐标和设备 authority。

两者共用 `WindowController::PublishPenCursorSample` 和同一个 latest-only mailbox。这个 mailbox 不是绘制 contact 数据，不进入 Stroke Modeler、L0/L1/L2 或持久墨迹。

### 4.3 Hover、Pointer API 与 Win7

- Hover 保持事件驱动：RTS InAir 或 Pointer Update 的坐标变化触发 cursor render request；没有新增持续刷新循环。
- Windows 8+ Pointer API 提供较早的 Pen/Mouse/Touch authority、Pen Hover 坐标，以及触觉设备绑定需要的 pointerId/笔尾提示。
- `WM_POINTER` 不是新的绘制墨迹来源；RTS 仍是 contact 绘制输入来源。
- Windows 7 SP1 + KB2670838 没有 Pointer API 时，动态解析失败并回退到 RTS Pen/InAir 与非 promoted Mouse 消息。
- Up/Leave/Clear 会清理旧 cursor；终态坐标不会被伪装成永久 Hover。

## 5. Commit 调查证据

### 5.1 7 月 29 日基线

用户给出的 `0729a` 不是当前仓库中可解析的 Git ref。本轮以 7 月 29 日最后一个可检查提交 `f76a35a` 作为近似源码基线；用户保留的原始构建仍是最终体验基线。

### 5.2 cursor-before-Move 不是本周新回归

`ec0f008 Implement L0 transient drawing cursor (GPU backbuffer)` 在 7 月 25 日已经让 RTS `Packets` 执行：

```text
DecodeSnapshot
PublishPenCursor
PublishMove
```

因此，单纯看到当前代码是 cursor-before-Move，不能解释为什么 0729 正常而当前版本异常。这个顺序是风险放大窗口，但不是本周才出现的差异。

### 5.3 真正改变语义的严格 deadline

`8f1ee5a fix: enforce drawing frame deadline` 在 8 月 3 日把活动帧等待从：

```cpp
input_.WaitForWake(frameWakeGeneration, remainingFrameBudgetMs);
```

改为：

```cpp
input_.WaitForFrameDeadline(remainingFrameBudgetMs);
```

旧 `WaitForWake` 在 wake generation 改变时提前返回，所以 Pen cursor 的高频 wake 会意外推动绘制循环接近输入频率。严格 deadline 是正确的 120 FPS 修复，但它改变了 cursor wake 的实际作用：事件仍会唤醒高优先级绘制线程，却不再允许提前生成一帧。

### 5.4 最近 RTS 优化与 state gate

- `415ee5e perf: optimize RTS packet decoder hot path` 保持固定 decoder、last-packet 与热路径约束。
- `74c33fc fix: harden RTS decoder lifecycle synchronization` 增加 rare-writer/lock-free-reader state gate。

state gate reject 理论上也可能丢弃 packet，但本轮没有 trace 证据证明它是当前症状的主因。为了保持最小修复，`74c33fc` 未修改；如果真机在本补丁后仍复现，再使用已有 RTS trace 单独调查。

## 6. 工作根因分析

### 6.1 修复前的 Contact Packets 路径

```text
RTS Packets
  -> decode latest packet
  -> PublishPenCursorSample(inContact=true)
       -> update cursor mailbox
       -> RequestDrawingCursorRender
       -> PublishControlWake / SetEvent
  -> PublishMove(latest contact snapshot)
  -> diagnostics
```

绘制线程使用 `THREAD_PRIORITY_ABOVE_NORMAL`。严格 deadline 下，Contact cursor 的每包 wake 已不能产生额外帧，但仍可能唤醒并调度绘制线程。由于 wake 位于 `PublishMove` 之前，它可能扩大以下竞态窗口：

1. RTS 已发布 cursor，但尚未覆盖最新 Move。
2. 高优先级绘制线程被事件唤醒并继续等待/进入临近 deadline 的高精度阶段。
3. RTS callback 的 Move 发布被延后。
4. 下一帧边界可能仍读到旧 contact snapshot。
5. 新点滞留到再下一帧，视觉上增加约一个 120 Hz 帧周期的拖尾，并以更大跨度追赶。

这个机制同时解释了“比 Mouse 更不跟手”和“追上时抽帧”：问题不是缺少历史点，而是帧边界偶尔拿到上一份 latest snapshot。

### 6.2 为什么 0729 没有同样明显的问题

0729 已有 cursor-before-Move，但当时 cursor wake 会让 `WaitForWake` 提前结束，实际绘制频率可能被输入事件推高。旧行为以突破目标帧率的方式掩盖了发布顺序风险。

`8f1ee5a` 正确地恢复严格 120 FPS 后，每包 cursor wake 不再带来新帧，只剩调度成本和抢占窗口，于是旧顺序的副作用变得可见。

### 6.3 为什么不归因于约 270 Hz 原始输入

- latest-only 是既定设计，绘制线程只需要在每个 120 Hz 帧边界读取最新点。
- 丢弃帧间的中间 packet 不等于增加一整帧延迟。
- Mouse 和 Touch 的正常表现说明模型不要求消费全部原始点才能跟手。
- 当前症状更符合“偶尔读取旧 snapshot 后跨帧追赶”，而不是“均匀少采样”。

### 6.4 为什么 Touch 没有同类 Pen wake 问题

Touch 的接触 cursor 来自活动 contact runtime，不通过 Pen cursor mailbox 为每个包发布单独 cursor wake。Pen 则同时拥有 contact snapshot 和独立 Pen cursor mailbox，问题发生在后者的额外唤醒路径。

Mouse cursor 也有自己的消息/光标路径，但没有同样的高频 RTS Pen callback 在 cursor wake 后尚未发布 Move 的特定顺序组合。

> 状态：以上是由 commit 差异、线程优先级、调用顺序和症状共同支持的工作根因。由于用户随后禁止继续动态测试，仍需实体 Pen A/B 才能升级为硬件实测结论。

## 7. 考虑过但拒绝的方案

### 7.1 持续约 270 Hz 渲染 Hover

拒绝原因：用户明确要求保持移动后才刷新的节能行为。当前 Hover 已是事件驱动，没有必要加入持续渲染循环。

### 7.2 Pen Move history/ring buffer

拒绝原因：违背已确认的 latest-only 模型，增加队列、内存和模型消费复杂度，却不解决帧边界读旧 snapshot 的核心问题。

### 7.3 停止 `WM_POINTER` 发布 Contact 坐标

拒绝原因：接触橡皮、倒转笔和未来可能显示的 Contact cursor 仍需要最新坐标。RTS 与 Pointer 样本都应继续覆盖 mailbox，只取消冗余 wake。

### 7.4 只交换 cursor 与 Move 顺序

拒绝原因：可以缩小抢占窗口，但仍保留每个 Contact 包的无效 wake；严格 deadline 下这些 wake 仍有调度成本。

### 7.5 只取消 Contact cursor wake

拒绝原因：虽然已消除主要调度干扰，但明确保持 Move 优先发布可保证 120 Hz 帧先看到模型需要的最新 snapshot，并与根因分析一致。

### 7.6 修改 deadline、模型、prediction 或 state gate

拒绝原因：这些区域没有直接证据，且会扩大行为面。严格 120 FPS、模型策略和 `74c33fc` 均保持不变。

### 7.7 新增 trace/counter 或测试专用 API

拒绝原因：仓库已有 RTS trace；本补丁只需静态核对既有调用顺序，不值得增加公共或测试接口。

## 8. 最终批准的最小方案

### 8.1 `WindowController::PublishPenCursorSample`

文件：`inkStrokeModelerTest/draw3/window_control.cpp`

最终逻辑：

```cpp
if (!penCursorSample_.Publish(sample)) return;
// 接触期间由活动绘制帧读取 mailbox，避免每个 Pen 包额外唤醒。
if (!sample.inContact) RequestDrawingCursorRender();
```

保持不变的行为：

- RTS 与 `WM_POINTER` 的所有有效 Pen 样本继续写 mailbox。
- 首次 authority 变化仍可独立请求 render 和系统 cursor 刷新。
- Hover 样本仍在位置/状态变化时请求 cursor render。
- `QueueSystemCursorRefresh` 的 valid/contact/inverted 状态比较保持不变。
- Clear、Up/Leave、工具变化和系统 cursor 刷新保持原行为。

### 8.2 RTS `Packets`

文件：`inkStrokeModelerTest/draw3/realtime_stylus.cpp`

修复前：

```text
decode -> cursor -> Move -> diagnostics
```

修复后：

```text
decode -> Move -> cursor -> diagnostics
```

具体契约：

- 继续只解码 batch 最后一个 packet。
- `PublishMove` 仍走原 coordinator/interruption simulation 分支。
- 即使 Move publication 返回失败，仍继续更新 Pen cursor mailbox。
- diagnostics 仍最后记录原 `published` 结果。
- 没有增加等待、分配、COM 查询、恢复逻辑或新失败语义。

### 8.3 文档同步

`.trellis/spec/native/runtime-and-rendering.md` 已记录：

- Contact Pen cursor 只覆盖 mailbox，不逐包发 render/control wake。
- Hover、Clear、终态和 authority 变化保留离散唤醒。
- RTS `Packets` 固定为 Move -> cursor -> diagnostics。
- 不允许为去重而停止发布 `WM_POINTER` Contact 坐标。

任务目录中的 `prd.md`、`design.md`、`implement.md`、context JSONL 和 `task.json` 已同步最小范围；任务保持 `in_progress`。

## 9. 明确未修改的区域

本补丁没有修改：

- 任何 `WM_POINTER` message case 或坐标来源。
- Hover 刷新策略、Hover 阈值或持续帧策略。
- `contact_input`、queue、seqlock、wake generation 或 frame deadline。
- Stroke Modeler、prediction、压力、姿态、笔宽或 dirty rect。
- Mouse、Touch、Precision Touchpad 路径。
- `74c33fc` state gate。
- Down、Up、InAir、decoder、binding 或 interruption simulation 策略。
- 公共 API、类型、运行时诊断和测试专用接口。

## 10. 验证记录

### 10.1 静态检查

- 产品 diff 只有 `window_control.cpp` 和 `realtime_stylus.cpp` 的两个局部行为变化。
- `WM_POINTER` handler、Hover、Clear、Up/Leave 未发生代码差异。
- Contact 样本仍先调用 `penCursorSample_.Publish(sample)`。
- RTS `Packets` 顺序已核对为 Move -> Pen cursor -> diagnostics。
- Move 失败不会提前 return，cursor 仍更新。
- `git diff --check` 通过。
- C++ 文件保持 UTF-8 BOM + CRLF；Trellis Markdown 保持 UTF-8 无 BOM + LF。

### 10.2 构建与现有测试

- 完整解决方案 `Debug|ARM64` 构建通过，0 warning / 0 error。
- 完整解决方案 `Release|ARM64` 构建通过，0 warning / 0 error。
- Debug ARM64 测试程序通过。
- Release ARM64 测试程序通过。
- 测试覆盖 Pen cursor、contact input、RTS decoder/binding/lifecycle、Error decoder 保留、InAir cache hit/miss 和 state gate。

### 10.3 无效的严格基准

在用户发出“现在不准动态测试”前，Release 严格运行指标基准已经启动并结束。报告显示：

```text
strictPass=false
landingCount=0
activeFrameCount=0
presentCount=4
```

这表示该次 `SendInput` 没有进入实际绘制 contact 链，失败原因是没有样本人口，不是测得帧率或延迟超过阈值。该结果无效，不能用于支持或反驳补丁。

收到禁止动态测试的指令后没有重跑，也没有再启动应用、SendInput、运行指标或真机验证。

### 10.4 尚未验证

- 实体 Windows Ink Pen 相对 0729 构建的 A/B 跟手性。
- 墨迹追上笔尖时的抽帧是否消失。
- Contact Eraser、倒转笔及其他可见 Contact cursor 是否持续跟随。
- Hover、触觉、Mouse、Touch、Precision Touchpad 的真机无回归。
- 成功 Present 在实体输入下仍不超过 120 FPS。
- D3D Debug Layer 与真实桌面视觉检查。

## 11. Commit 与工作区状态

实现已经提交：

```text
949752a fix: reduce Windows Ink contact wake latency
```

提交包含两个产品文件、运行时规范和完整 Trellis 任务记录。任务没有归档或结束，`task.json` 仍为：

```json
"status": "in_progress"
```

工作区原有未跟踪 `Vcpkg/` 未被修改或加入提交。

## 12. 后续建议

允许恢复动态验证后，按以下顺序执行：

1. 使用同一设备、相同画布参数对比 0729 原始构建与 `949752a`。
2. 分别测试慢速直线、快速曲线、突然加速和停住后继续，重点观察额外拖尾与追赶跳跃。
3. 测试 Pen、Highlighter、Eraser 和倒转笔 Contact cursor。
4. 验证 Hover、触觉、Mouse、Touch 和 Precision Touchpad 行为没有变化。
5. 使用有效实体输入或能够进入真实 contact 链的指标方案确认成功 Present 不超过 120 FPS。
6. 如果最小修复后仍复现，再开启已有 RTS trace，检查 `74c33fc` state gate reject、callback 间隔和 Move publication；不要预防性扩大当前补丁。

## 13. 交接结论

本轮没有把问题归因于 120 Hz 丢弃中间点，也没有恢复约 270 Hz 绘制。补丁处理的是严格 120 Hz deadline 引入后暴露出的调度组合：Contact cursor 每包 wake 已不能生成帧，却可能在 latest Move 发布前干扰高优先级线程调度。

最终修复通过“Contact cursor 只更新 mailbox”和“RTS Move 优先于 cursor 发布”消除这个组合，同时保留全部坐标来源、Hover、系统 cursor、触觉和现有模型策略。自动构建与现有测试已经通过，但根因和体验改善仍需实体 Pen A/B 最终确认。

## 14. 实体对照后的最终结论（2026-08-09）

- `0729a` 与最新构建都存在“墨迹相对实体笔尖比 Mouse 相对系统光标更落后”的观感；`0729a` 的约 270 Hz 更新只让差异不明显，因此这不是最新提交引入的回归。
- 打开同屏软件光标后，以 Pen 应用光标和 Mouse 系统光标作为同类参照，两者的墨迹跟随表现一致，没有原先判断的严重额外拖手。
- cadence B 把同帧消费中间 RTS 样本的比例从约 45% 提高到接近 100%，但没有改善主观跟手性；该实验不能作为产品修复，ordered ring、额外指标和测试 hook 全部撤回。
- 问题 2/3 在本轮实体测试中均未复现，不能声称已经修复，也没有证据把它们归因于 cursor wake。
- 保留 `949752a` 的 Move 先发布、Contact cursor 不逐包唤醒顺序，因为它减少无效唤醒且符合现有 120 Hz 架构；不再把它描述为已证明的延迟根因修复。
- 诊断提交 `418338a`、`2520c2c` 对生产/测试代码的改动恢复到 `949752a` 行为；专用 A/B 构建、日志、probe 和 detached worktree 删除。
- cadence 审查独立发现的 `writerLatch_` 跨 generation ownership ABA 仍以最小一行修复保留；它不依赖 ordered ring，也不改变正常输入节奏。
- 新增默认关闭的 Draw3 运行时设置：开启后普通 Pen/Highlighter 在 Contact 中显示与 Hover 相同的应用内瞬态光标，系统光标策略不变；实际按钮和持久化等待 Inkeys 设置层接入。

## 15. 最终人工核验

用户于 2026-08-09 确认人工核验已经完成，并要求结束任务、提交代码。未额外推断或补写用户没有明确提供的设备、场景和 D3D Debug Layer 细节。
