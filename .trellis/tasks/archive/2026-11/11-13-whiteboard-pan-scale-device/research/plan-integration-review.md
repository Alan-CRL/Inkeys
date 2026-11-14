# Research: 单帧跨运动单位的惯性接线审查

- Query: Touch(mm) Up 与 Mouse(DIP) Down/Move 可在同帧且 publication 乱序；如何只沿现有有序事件链选运动上下文，并保持准入、状态和输出单位正确？
- Scope: internal（当前生产 CanvasNavigation / DrawingController / WindowControl / Host；规划级接口建议）
- Date: 2026-11-13
- 状态：只研究，未改产品、PRD 或 design，未编译、GUI 或 commit。

## Findings

### 1. 审查结论

“入口一直是系统 DIP，每个 Down 带不可变数值 MotionContext，Dynamics 在真实排序后的第一 Down 选择并复制上下文，出口恢复系统 DIP”的方向可行，且比 Controller 按 Register/整帧选一套单位可靠。应保留一个 `Step`、一套 QPC/phase 排序与一套既有接触/手势状态机。

必须再明确四个边界：

1. 每个 ScrollUpdate/释放诊断在对应事件发生时转回系统 DIP，不能帧末拿最后一个上下文统一转换整帧累计量。
2. Register 只产生 Pending 候选；尚未由有序 Down 准入的 Touch 不能被永远当作“已持有”，从而提前丢弃一支本该获准的 Pen。
3. 系统 DIP、输入像素和 motion 的快照/转换入口全部有明确合同，尤其是异向 `span`、标量速度、`ContentVelocity()` 与 tracker 调试入口。
4. MotionContext 的帧内索引只用于这次调用；动力学必须持有数值副本，跨帧和 fling 生命周期不保存 span/vector 指针或 index。

### 2. 当前接口和必要改动点

| 证据 | 当前行为 | 新接线必须处理 |
| --- | --- | --- |
| [CanvasNavigation.cppm:156](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cppm:156) | Sample 有 key/qpc/x/y/phase/sourceKey/allowInertia | 追加 Down 的 motionContextIndex；明确 x/y 永远系统 DIP |
| [CanvasNavigation.cpp:683](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:683) | Step 拒绝不递增帧时钟，之后内部 QPC/phase stable sort | 禁止用同 frameQpc 分段多次调用 Step；第二段会直接返回 Snapshot |
| [CanvasNavigation.cpp:740](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:740) | Down 前 flushMove；真实 Down 才更新 sourceKey/reset booster | 此处选择/继承 context，拒收不能改变 sourceKey 或旧残余 |
| [DrawingController.cpp:2839](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2839) | Register 按出队顺序记录 contacts，可能早于旧 Up 被动力学消费 | 不在此更新 active motion，也不靠 `HasContacts()` 推导第一指 |
| [DrawingController.cpp:2936](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2936) | Drain history 后按 record+generation 查 runtime，最终送 samples | Down context 保存在 runtime 的数值副本，组帧时建立 index，不保存可重分配容器内地址 |
| [DrawingController.cpp:2982](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2982) | Controller 用同一比较器 stable sort，Dynamics 有序 span 快路 | context index 随 sample 移动，不能排序后用数组位置重解释 index |
| [DrawingController.cpp:3004](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:3004) | sourceKey 高32位解出末来源类别以热禁当前惯性 | 不将 context index/profile/unit 塞入 sourceKey 高32；原类别与低32 context 身份合同保持 |
| [CanvasNavigation.cpp:611](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:611) | Snapshot 同时产 focus/span/hand/pointer/booster/fling 诊断 | 当前量都是 DIP；新实现不能让 motion 值泄漏到原接口 |

### 3. 最小数值上下文与生命周期

建议的概念结构（规划示意，不是产品代码）：

```text
MotionContext
  motionPerSystemDipX/Y       // 有限正对角变换 M
  unit                       // SystemDip 或 Millimeter；回退不假称毫米
  metricSignature            // 输出/映射/密度的语义身份
  profileFamily/version      // 间接基准、直接 Touch、屏幕 Pen 等
  spatialThresholds          // slop/epsilon/reversal/release/boost/limit
  profileParameters          // 仅数值；不含 HWND/COM/共享指针
```

Controller Register 保存该 Down 的不可变候选快照；Step 组装 `span<const MotionContext>`，sample 只记录 index。Dynamics 遇到真正第一 Down 时校验 index 和全部数值，复制为自己拥有的 activeContext。末指 Up 后继续保留副本直至 fling/boost 完全退休。帧结束销毁/复用 context 数组也安全。

索引不能作为兼容性依据：不同帧 index=0 可能是完全不同的密度；同帧两个 index 也可能语义相同。比较有效 signature/数值，不比较指针或原始 revision。

追加同类 Touch：兼容性只检查真实输出映射/metric，忽略不影响度量的 profile 偏好变化，继承本批首指的 M、空间阈值与 profile。不能令第二指改写已有 tracker/质心/惯性单位，也不能从后来的指自动把 DIP 批次升级为毫米。

第一指 Unknown 仍可系统 DIP 回退。附加 Unknown 若有可证的同屏精确映射，可在不升级可信度的前提下继承；若没有任何映射证据，不应仅凭实际 InputDeviceType=Touch 就借用另一输入设备的毫米。具体拒收规则需在实现合同中明确；本报告建议保守拒收毫米批次中映射未知的附加来源，同时保持第一指回退可用。已知映射明确冲突的来源不能继承。

### 4. 为什么只在 Register 预检还不够

当前 `TouchPanHeld(input,atQpc)` 只筛 `panManaged` 与 Touch 类，未筛 disposition；`OwnerClass(input,atQpc)` 对非 deadBatch 则已排除 Suppressed。见 [DrawingController.cpp:2745](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2745)、[DrawingController.cpp:2759](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2759)。因此回传 rejected key 后必须一致更新 disposition，并让 held/owner/Active/笔抑制只认有效已准入接触；物理活着的忽略 Touch 只负责等终态，不算导航持有。

还有更早的问题：`HandleSelectionDown` 在 Step 前查询 TouchPanHeld/OwnerClass，可能立即 TrackSuppressedPenDown 或 DiscardUntilTerminal，且 `SuppressLaterPendingContacts` 会修改未来候选 disposition。见 [DrawingController.cpp:3140](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:3140)、[DrawingController.cpp:2787](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2787)。仅在 Step 返回时纠正 Touch held，无法复活已经被永久丢弃的 Pen。

可复现的规划测试时序（不是本轮实测）：

```text
A 可信 Touch Down@10，Up@30
B 不兼容 Touch Down@20，持续按住；B 应被拒收
C Pen Down@40；A 已结束，B 不应持有，C 应能接手

若以上全在一帧内出队，Step 前 TouchPanHeld 可能读到 B，把 C 永久抑制。
若 B 的 publication 比 A 早，Register 时甚至还不知道 A 才是真正第一指。
```

**纯预检能可靠解决的范围：** 新 Down 有明确 source screen rect 与当前输出冲突、数值非法，或已有真正准入且在该 QPC 仍活着的批次 context 已知时，可立即返回拒收/兼容事实。这是只读比较，无需另一套排序。

**纯预检不能独立解决的范围：** 同帧尚未消费的候选谁先 Down、哪一个会因兼容性被拒、它是否还能代表 owner。扫描物理 interval 后模拟这些 accept/reject 决策，本质上仍复制了状态机；只取最早 overlapping interval 也不可靠，因为已拒收接触不能作为跨手势桥接。

### 5. 建议的最小可靠准入边界

沿当前一条有序事件链做以下调整，避免创建第二套手势排序：

1. `HandleSelectionDown` 保留静态路由：非 Selection 仍建笔；不是白板移动/MouseRight 等无资格仍直接拒；native quarantine 的既有门禁保持。动态 owner/metric 尚未确定的白板候选只 Register 为 Pending，不提前作最终跨类拒收、Touch held 或永久笔抑制。
2. Register 不修改 activeContext，不让 Pending 作为 `OwnerClass/TouchPanHeld` 的已准入事实，也不使用 Pending 调用 `SuppressLaterPendingContacts` 退休别的未知候选。
3. Dynamics 原有排序后的 Down 分支做唯一的最终 class/metric 裁决：无 accepted held 时选择新 context；有 held 时只允许原类别和兼容 metric。拒收时不 flush 新拓扑、不改 lastSource、不影响 residual，只记录该 key 的拒收事实。
4. 输出按真实处理顺序的 admission decision（key/downQpc/accepted/reason；必要时包含 pen 被 Touch 拒收的原因），Controller 用它更新 runtime，不再重新求一遍赢家。
5. Controller 应用决策后再发布 `TouchPanHeld`、窗口抑制和后续 mailbox 状态。拒收运行时标为 Suppressed/只等终态并保留原 key/generation，未来 Move 不参与运动，真实 Up/Cancelled 可靠回收。

这会把既有动态跨类判定从 publication 边界移入已存在的 Dynamics Down 分支，并非复制第二套 FSM。Header 可继续只认识 sourceKey 数字类别 token，不导入平台枚举或 Display 服务。已有纯 helper 可留作数值 guard；它的拒收不能覆盖真实有序结果。

**笔抑制需特别保留：** 当前 `HasLiveSuppressedPenContact` 会让一支在 Touch 期间落下的笔直到真实终态都保持抑制。后移准入时，不得让同一支被拒 Pen 在 Touch Up 后凭 Move 重新进入；也应测试“同帧被抑制 Pen 仍活着，再来另一 Pen”的既有规则。可在同一 contact 状态中保留 rejected-until-terminal 原因，或由 ordered decisions 驱动既有 Controller 抑制记录；不能只返回帧末 held 总数而丢失事件时间事实。所有候选的终态仍应对 ignored 状态可见。

`ObserveCanvasPanPenMailbox` 也在 Step 前调用 TouchPanHeld 并可能置 suppress flag，不能漏改为只读已接受状态；有 Pending 输入时先 drain/消费权威导航判定再做最终持有发布。见 [DrawingController.cpp:3196](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:3196)、[DrawingController.cpp:19026](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:19026)。

Admission 回传容量不能只设为 16：同一帧可有许多顺序 Down/Up，尽管任意时刻只持有不超过 16 个。建议 caller 预分配与 samples 数相同的 decision span，或使用已有有界 scratch；若容量不足明确 fail closed，不静默漏掉 rejected key。省去为每个 Step 新建无界返回 vector。

### 6. 同 QPC、重置与旧残余

比较器合同为 `(qpc,phase)`，phase 数字顺序 Down < Move < Up < Cancelled，同 QPC 同阶段 stable。因此 `Touch Up@T` 与 `Mouse Down@T` 仍视作重叠，不能为了切单位把 Up 排到 Down 前。Mouse 应按既有跨类规则拒收；只在 Mouse Down 严格晚于 Touch Up 时正常接手。见 [CanvasNavigation.cpp:690](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:690)、[DrawingController.cpp:2745](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2745)。

在 Step 内切 context：先 flushMove，把此前已接受的增量按旧 M 转为系统 DIP；若 metric/source 类不兼容，清旧 fling/booster，再安装新 activeContext、清 tracker 并正常 Down。不要在事件循环中调用会重置 lastFrameQpc 的完整 `Stop()`；当前 `Stop` 会令帧时钟归零。清惯性用 `StopFling`，接触/估速按真实第一 Down 的现有逻辑初始化。见 [CanvasNavigation.cpp:848](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:848)。

相同 metric/来源的正常连续同向手势保持旧 residual 规则；不能仅因帧内 index、新的原始 revision 或速度依赖的本次 friction 选参不同就无条件清 boost。真正跨单位/类别/语义 profile 家族才隔离。无害 preference 更新应到下一完整手势采纳；空间 metric 变化则是立即中断条件。

### 7. canonical 系统 DIP 的完整出口

令 `m = M * dip`，M 是分轴正对角矩阵。以下应恢复为系统 DIP：

| 输出/入口 | 正确处理 |
| --- | --- |
| 每个 ScrollUpdate.delta | 当场 `M^-1 * deltaMotion` 后累加到 frame.contentDeltaDip |
| 帧末 flingDelta/velocity | 使用 fling 自己锁存的 context 恢复 DIP，不能拿最后注册者 |
| raw/selected/boostedReleaseVelocity | 释放事件当场恢复 DIP；另外保存该 release 的 source/context/单位诊断副本 |
| releaseSpeed | 对已经恢复 DIP 的 selected vector 求 norm，不能除几何平均倍率 |
| releaseTailSpeedLimit | 作为标量上限，沿该 release motion 方向逆变换再取 norm；零/未知方向输出 0/明确 unavailable |
| Snapshot pointer velocities/handVelocity | 每个 motion vector 逆变换后再按原规则组合 |
| focus/span | 先把每个 pointer 还原 DIP，再求 focus 和平均半径；异向变换下 span 不能直接除一个平均系数 |
| boosterStarting/CurrentVelocity、flingOffset | 同样逆变换，保留秒/计数等无量纲字段 |
| `ContentVelocity()` | 对外明确返回系统 DIP/s；Controller 的 VelocityDip/VelocityPixels 保持既有名字和含义 |
| `VelocityTracker()` | 不能把内部 motion tracker 暗中标成 DIP；测试/诊断入口需明确 Motion 语义或提供 canonical 查询 |
| `viewportSizeDip` | 保持公开系统 DIP；需要 motion 视口时在内部按 M 转换，非正视口仍禁止 fling |

`CanvasPanDynamics::Step` 当前有一处内部赋值 `velocity_=Snapshot(...).handVelocity`。如果 Snapshot 开始返回 canonical DIP，而 velocity_ 仍是 motion，这行会造成下次输出再逆变换一次。应让内部计算始终 motion，Snapshot/ContentVelocity 只在公共边界转换；或将 velocity_ 明确改为 canonical 并完整梳理使用。前者更少混用。见 [CanvasNavigation.cpp:831](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:831)。

一帧多次释放时现有 FrameOutput 只保留一组末次 release 诊断。保留此有界语义可以，但必须额外标记 releaseSource/context；最后一个 accepted Down 的 frame.sourceKey 可能已是 Mouse，而 release 数据来自前面的 Touch。不要用帧末 context 事后给所有 release 字段贴标签。

所有门槛在 motion 中作判定，例如 stationary 仍比较 motion releaseSpeed 与 motion minimum；之后才把公开 releaseSpeed 设为 DIP/s。不能在 line801 处混用 canonical 标量与 motion 阈值。Controller 像素输出只再除当前系统 DIP/px，不再除 M。

### 8. 窗口快照、低频消息与中断

当前 DisplayScale/DeviceMode 各自 getter 都使用 `speedEraserConfigMutex_`，但分两次 getter 可能拼出不同瞬时状态。应新增小型同锁组合 snapshot，供每个 Down 构建 context；不要求改动整个 Display/Host 架构。见 [WindowControl.cpp:1256](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1256)、[WindowControl.cpp:1273](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1273)。

组合读取防的是读端撕裂，不自动成为跨两个 setter 的事务。Host 当前先 PumpDisplayScale，再从 bridge snapshot 应用 paintDevice 模式；若要求二者同一个发布代，需小型统一 setter/发布点，或明确只在本轮 Host pump 完成后读取其已应用快照。不能仅加 getter 就宣称 Display 和 Bridge 天然同代。见 [Host.cpp:1797](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1797)、[Host.cpp:1836](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1836)。

DPI/output/物理可用性或有效密度变化：在消费旧导航前通过控制 wake 比较有效 metric signature，调用 Controller.Stop，旧接触 Suppressed 到终态；下一完整 Down 解析新域。不能因为没有 Move 就等到下一指才发现失效，也不能用原始 revision 任意增长误停。无害 profile/mode 偏好变化仅更新 pending resolver 状态，旧批次及旧 fling 用已锁值。

### 9. 必须加入的验证案例

1. publication 顺序与 QPC 相反的 `Touch Up(t1)→Mouse Down(t2>t1)→Move` 同帧：几何正确累计，Mouse 使用 DIP，无旧毫米 boost；对照按帧分送结果一致（动画从各自呈现起步的既有差异单独处理）。
2. 同 QPC `Touch Up/Mouse Down`：Down 仍先处理，Mouse 拒收；同 QPC 同 phase 的 stable ties 不变。
3. 同帧早 Touch 的 delta 与后 Mouse 的 delta 用两种 M，逐段已知几何和等于最终 DIP 位移，不能帧末统一逆变换。
4. 上文 A/B/C：不兼容 B 拒收后不拦 C Pen；交换 A/B 的 publication 顺序仍相同。
5. 同屏兼容第二 Touch 继承首指 context；profile 在两指之间更新也不换 metric/阈值；未知来源/已知冲突的附加 Touch 按明确规则处理。
6. 先拒收 Touch，之后其 Move/Up/Cancelled 不影响 held/owner/残余；末终态可回收，无长驻 Active。
7. Touch 期间 Pen 被抑制、Touch 先 Up 而 Pen 仍按住、另一 Pen 同帧到达：保持既有直到真实终态的抑制规则，不让 Move 复活。
8. 异向 M 的双指 focus/span、release norm、limit、booster、ContentVelocity 与像素输出：分别独立计算几何期望。
9. Step 返回后立即销毁/覆盖上下文数组，再推进无样本 fling：结果不变，证明无悬空 index/span。
10. invalid index/nonfinite M/decision buffer 不足：失败封口并收终态，不污染旧 accepted context，不越界。
11. 空闲无输入期间收到 DPI/output/物理失效控制 wake：旧 fling 立即停；只有无害 profile 或无关 revision 变化不中断。
12. 同帧多次释放后再 Down：release诊断仍指向对应释放来源，frame.sourceKey 保持末次实际准入类别，按高32位热禁正确对象。

### 10. RTS / Window 前帧抑制的实际风险与最小文件边界

**核实：RTS contact 没有被 TouchPanActive 提前丢弃。** `StylusDown` 解码并填好 source 后先发布 Pen cursor，再经 coordinator 发布真实 Down；代码中没有读取 TouchPanActive/PenContactSuppressed 来决定是否 PublishDown。见 [RealtimeStylus.cpp:1257](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1257)。RTS 的 readiness、TouchPad、解码和 binding 错误仍有独立门禁，但与这里的导航抑制不同。因此不需要为这个问题重写 RTS 后端；后续必须维持真实 contact 的完整到达。

**确有窗口反馈锁存风险。** `PublishPenCursorSample` 会按上一帧 touchPanActive（或已有 suppression）锁存 `penContactSuppressedForTouchPan`；仍发布 Pen presence，但提前返回反馈处理。WM_POINTER 路径还会锁存 suppressedPenPointerId。见 [WindowControl.cpp:1594](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1594)、[WindowControl.cpp:2407](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2407)。`SetTouchPanActive(false)` 不清掉这个锁存；通常等匹配 Pen Up/leave/clear 才解除。见 [WindowControl.cpp:1430](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1430)、[WindowControl.cpp:2395](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2395)。

于是，即使前帧 accepted Touch A 已物理 Up，下一帧处理其终态前仍可能保留 touchPanActive=true；B 是后来应拒收的 Touch，C Pen 又在 Step 前到达。C 的真实 RTS Down 可保留，但 Window 可能先把 C 的光标/触觉反馈抑制到 Up。如果 Controller 又把这个反馈 flag 当作导航准入事实，就会形成自我加强。

本仓库内较小而可靠的方案：

- **CanvasNavigation.cpp/.cppm：** 在既有有序 Down 中增加数值 class/metric 最终准入；接触持有集合只含 accepted，ignored-until-terminal/拒收事实不改变质心或来源。输出 ordered admission decisions，保留 sourceKey 的高32类/低32context。每帧只推进一次时钟。
- **DrawingController.cpp：** Register 改 Pending；白板移动模式只保留前置静态资格与 native Running 的既有历史门禁，移除 Pending 触发的动态 owner/TouchPanHeld/永久 Pen 拒收与 SuppressLaterPendingContacts 资格。Dynamics 裁决后更新 accepted/rejected runtime，再发布 held；持续 PenContact mailbox 只用于应有的真实新 Down/空闲惯性中断，不能因合法已接纳 Pen 持续按住或 Pending 存在每帧 Stop。被拒 Pen 继续保持到真实终态，不能凭 Move 复活。
- **WindowControl.cpp/.cppm：** TouchPanActive 与反馈 suppression 是导航结果的投影。若窗口为了即时隐藏反馈暂时依据旧 bool 作判断，该判断必须标为 provisional，不能反向成为 core 准入真值。接到 Dynamics 对该 Pen 的权威 accepted/rejected 结果后，按稳定 contact 身份、Down QPC 和 navigation epoch 校验后纠正相应反馈状态；较旧确认不能清掉新一笔已确认的 suppression。不能仅 `SetTouchPanActive(false)` 就无条件清所有 suppressed Pen，因为真实 Touch 期间落下的 Pen 仍须抑制到 Up。
- **RealtimeStylus.cpp：** 预计无需为导航权限新增门禁；作为回归证据检查其 Down/Move/Up 完整发布与 cursor presence。若实际编码需要暴露已具备的 cursor/context 身份，仅做小型接线，不改后端选择或显示服务。
- **现有生产 CPU probe / Headless tests：** 覆盖前帧 Touch 已 Up 但窗口 bool 未清、B 拒收、C Pen accepted 的 contact+feedback 双结果，以及真正 Touch 期间 rejected Pen 仍锁到终态、旧 epoch ack 不影响新 Pen。

如果选择让 producer 根据 Touch 状态作**永久**抑制，单帧 bool 无法保证正确；必须提供带已准入身份与真实 QPC 终态的 Touch interval 查询，排除 Pending/Rejected，并在终态更新后查询。该方案还需处理 owner 发布尚未看见终态的窗口；只发布一个“起止时间”常量并不能消除所有异步竞态。相比之下，本次保留完整 producer contact、最终权限在有序 Dynamics、窗口反馈可由带身份/代际的权威结果纠正，边界更小且可无窗口测试。

这些变动是为支持同帧不同 metric 与新增“拒收不兼容来源”的必要接线。无需保留旧测试中固定的 helper 名称或提前准入位置；应保留并重写其可观察合同：真实重叠互斥、同 QPC 次序、被抑制的笔直到终态、无错误触觉/光标复活、合法新 Down 能接手。

## Caveats / Not Found

- 本报告没有重新调查 EDID；有关信任判定沿用本任务 display-trust-and-fallback.md。
- 未运行任何测试；上文时序是根据现有调用顺序推导的必测风险，不冒充已经实机复现。
- 任意 publication 乱序下的“仅 Register 纯预检即可决定全部兼容性”不能由当前数据结构保证。若最终实现仍保留提前永久拒 Pen，必须限制其依据为已接受事实并证明所有 Pending 路径不触发；不能只添加帧末 rejected-key 反馈便宣布问题解决。
- 这份报告是设计审查建议，不授权进入实施；主会话负责合并到最终技术方案和执行清单。

## 主会话收敛记录

本报告第3–10节已合并到最终design.md 3.1–3.3及implement.md B/V3/V4/V8：Pending不参与owner/held，动态权限只在有序Dynamics Down裁决；足额caller决策缓冲、带身份/QPC/epoch的Pen反馈纠正、逐事件canonical DIP、完整快照/标量出口、内部velocity防二次转换与release独立来源均列为实施合同。现有producer完整发布保持，不扩展输入后端。

未知附加来源的最终规则：mm批次要求可证的同屏精确映射，否则拒收；首指Unknown可DIP回退，DIP批次后加可信指在无输出冲突时继承DIP，不升级度量。省略context的旧纯DIP调用保留默认，新生产适配调用必须有合法context和足额决策缓冲。未发现必须再向用户追问的产品决策；实际代码/生产probe仍是之后实施的验证工作。
