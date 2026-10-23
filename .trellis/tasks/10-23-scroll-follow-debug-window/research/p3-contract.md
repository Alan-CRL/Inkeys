# P3 数据模型与窗口交接合同（实现前准备）

仅准备接口，尚未实现产品文件。P2 真实 Mag 排除通过或 main 明确安排后才开始 P3。建议新增 `Inkeys/Inkeys/Business/ScrollFollowDebug.h/.cpp`，命名空间 `Inkeys::Business::ScrollFollow::Debug`；公开头只依赖标准库、ScrollFollow.h、VisualMotionTracker.h、FreezeCapture.h（这三份均无 HWND/OpenCV 类型）。GDI/导出与这里分离。

## 最小公开接口

```cpp
using ChannelPtr = std::shared_ptr<Channel>;
ChannelPtr Activate() noexcept;                   // UI owner 创建/发布新窗口代次；分配失败返回空
bool Enabled() noexcept;                          // 一个原子快速判断，普通关闭路径到此结束
ChannelPtr Current() noexcept;                    // 仅 Enabled 后取得当前共享寿命
void Deactivate(const ChannelPtr& expected) noexcept; // 只撤销相同窗口代次，不影响新代次

class WriteLease {                                // move-only；析构未 Publish 自动放弃
public:
    explicit operator bool() const noexcept;
    CV::TrackerDebugSnapshot* Tracker() noexcept;   // 无 lease 返回 nullptr
    bool Publish(bool targetPublished, double totalTrackerMs) noexcept;   // 图像+快照一次发布；只调用一次
};
class Channel {
public:
    bool Active() const noexcept;
    std::uint64_t Generation() const noexcept;
    WriteLease BeginFrame(const FrameMetadata&, const CV::MotionImageView&) noexcept;
    void RecordMotion(const FrameMetadata&, const CV::MotionResult&, double totalTrackerMs) noexcept;
    void UpdateStatus(const LiveStatus&) noexcept;
    LiveSnapshot ReadLive() const noexcept;
    bool ConsumeLatest() noexcept;                 // 仅 UI owner 调用
    const FramePacket* Displayed() const noexcept; // 没消费过时为 nullptr
    std::unique_ptr<FramePacket> CloneDisplayed() const noexcept; // UI 显式导出时深拷贝
};
```

Activate 创建 3 个空图像槽及数值存储，成功后发布共享指针，再发布 Enabled=true；不启动线程或任务。Deactivate 先封闭 expected，再仅在 current==expected 时清除全局 current/Enabled；UI owner 串行调用 Activate/Deactivate，expected 身份阻止迟到旧窗口关闭新窗口。Context **不永久保存 Channel**。每次真正 CV 调用前以 Enabled 快判断后 Current 取得共享指针，lease 只保活当前回调；controller Tick 同样只在 enabled 时短暂取得。关闭后 paused Context 也不能长期留住三个大图像缓冲。可在同一个 Follow 会话内部打开/关闭/重建，以便有限性能验证；正常用户入口仍只在启动打开一次。

## 按值数据与单位

以下名称为固定合同；可以沿现有风格声明 aggregate，但不要增加框架/观察者层。

- `Operation { Initialize, Process }`；`SummaryKind { Motion, State, CaptureFailure }`。
- `PauseCause { None, InvalidRoi, TrackerPaused, CaptureQuality, CaptureApi, CaptureStartupTimeout, CaptureResultTimeout, CaptureUnavailable, ViewportClamped, InitializationTimeout, ControlFailure }`。
- `FrameIdentity { uint64_t windowGeneration, businessSession, captureSession, sequence, displayGeneration; }`。所有帧/摘要携带复合身份；不能仅拿 sequence 跨会话比较。windowGeneration 由 Channel 填，其他来自实际 Context/Frame。
- `FrameMetadata`：identity、Operation、Capture::Geometry、Capture::Quality、Capture::Statistics、`Capture::Rect roi`、`int captureWidth/captureHeight`、`size_t captureStrideBytes`、`double sourcePixelsPerPixelX/Y`、`Capture::TimePoint sampledAt`、`chrono::microseconds sampleInterval`。roi 是实际借用输入在 capture buffer 内的半开像素坐标；左上桌面坐标 = geometry.sourceRect.left/top + roi.left/top。sampledAt 直接取 Frame.timestamp；sampleInterval 取后述真实读回观测，不从 UI 消费间隔推导。
- `FramePacket`：metadata、`int width/height`、`size_t strideBytes`、`vector<uint8_t> bgra`、`CV::TrackerDebugSnapshot tracker`、`double copyMs/totalTrackerMs`、`bool targetPublished`。totalTrackerMs 是实际整次 Initialize/Process 的 wall time（包含开启的诊断记录），不是四个分段之和。图像为紧凑 top-down BGRA，stride=width*4，逐行复制传给真实 Tracker 的同一 input.image，绝不留原指针。tracker.result 已含完整生产结果，无须另存可漂移的 MotionResult。targetPublished 仅记录实际 channel->Publish 的返回值；Initialize 不发布 Target，记录 false。它不等于 owner 已应用，后者来自 live.ownerAppliedSequence/viewport。
- `LiveStatus`：`State state`、`uint64_t businessSession/captureSession`、`bool ownsCapture/awaitTracking`、PauseCause、`Capture::Snapshot capture`（仅匹配实际 captureSession 时有 ownsCapture=true）、`uint64_t ownerLatestSequence/ownerAppliedSequence`、`double viewportX/Y`、`bool viewportClamped`、`chrono::steady_clock::time_point observedAt`。这是一份 controller 实际读取到的状态，不存图像，不改 Tracker 决策。
- `NumericSummary`：SummaryKind、FrameIdentity、sampledAt/observedAt、`bool hasMotion`、MotionResult（仅 hasMotion=true 有效）、State、PauseCause、捕获 accepted/rejected/apiFailures 计数、ownerAppliedSequence、viewportX/Y、copyMs/totalTrackerMs（仅 hasMotion=true 时有效）。默认 MotionResult 不能被解释成拒绝。State/CaptureFailure 事件只在会话、状态、原因或失败计数改变时添加，不由每个 16ms Tick 填满历史。
- `Counters`：framesPublished、framesOverwritten、framesConsumed、frameDrops、summaryDrops、trackerCalls/Accepted/Rejected。Channel 代次重置；tracker 计数仅涵盖窗口开启期间真正观察的 CV 调用，不能冒充开启前完整会话计数。Capture accepted 使用原 Capture 会话累计值，UI 标清计数范围。
- `LiveSnapshot`：LiveStatus、Counters、`array<NumericSummary,30> summaries`、count；按从旧到新排列有效项。

CV 的 reference/forward/refined/backward、added.point、candidate.flow/bounds、affinePosition 为处理图像坐标，除以实际各轴 scaleX/scaleY 还原 ROI 原始像素；画到当前预览再统一做一次视图变换。anchors/totals/deltas/结果已经是原图/内容像素，不再次缩放。grid 的证据按参考点格；retained/added 按当前格。mainMotion 不代表接受；仅 firstFailure 指向实际失败，checks.condition 不统一表示失败。P1 头可能由 reviewer 补充只读门限，UI 直接使用它。

## 三槽与线程所有权

- 唯一 Mag producer 独占 back，唯一 UI owner 独占 front；atomic middle 打包槽号和 dirty，必要时含 closed 位。开始槽 0/1/2，front 未有效。
- BeginFrame 只访问 producer back。检查 BGRA 布局/溢出/可读最后行，最大复制范围沿现有 Capture 64M pixels 上限；向量保留 capacity 并逐行 memcpy。分配/布局失败丢诊断帧并返回空 lease；生产 CV 仍按原参数调用（debug=nullptr），不得改变其结果。
- Tracker 直接写 lease.Tracker()。Publish 以 acquire/release 原子交换 back 与 middle，置 dirty；producer 获得旧 middle 为下一 back，覆盖尚未消费的帧增加 framesOverwritten。不等待 UI、GDI、锁、目录对话框或 I/O。关闭采用同一个 packed middle 的 closed 位/CAS 或等价线性化方案，关闭后旧 lease 不能发布到新代次。
- ConsumeLatest 仅 dirty 时交换 front 与 middle，并清 dirty。Displayed 在下一次 ConsumeLatest/Channel 销毁前不可变。Freeze 只是不再 ConsumeLatest；front 固定，producer 仍在其余两个槽交替，绝不写 front。恢复冻结直接消费最新，不播放队列。
- 无 Tracker 的 AwaitTracking、状态更新或 capture 失败，均不 BeginFrame/Publish。暂停前最后真实失败帧因此留在 pending/front；UI 一次消费后保持，后续 ReadLive 不能覆盖图像/同帧统计。Capture 失败无新的 Tracker 图像时保留旧图，并用 live 原因标明。
- metadata/history 使用独立短 mutex；ReadLive 只在锁内复制固定数值，不能持锁绘图/格式化/导出/调用 Business。RecordMotion 的计数用原子、30 条 ring 用 try_lock，争用时只丢数值摘要并增加 summaryDrops，Mag 不等待。UpdateStatus 为 controller 单写者，可短暂锁这份数值数据；它不能持有或访问 front/back 图像。避免不受 C++ 内存模型保护的“seqlock + 普通字段”数据竞争。
- CloneDisplayed 在 UI 点击保存时一次深拷贝图像/同帧统计，后续导出 worker 只拿克隆，不 pin front、不持 Channel。UI 同时复制 ReadLive 的 30 条摘要与自身显示选项，然后选择目录/异步写盘；最多一个导出任务。取消/失败只影响导出。

## 精确接入点

ScrollFollow.cpp Frame 中保留所有现有早退及生产 resultMutex store，包括 AwaitTracking 存默认 result 的原行为：

1. 现有会话/owner/capture guard 通过后计算同一 ROI。ROI 无效时先 CAS latch InvalidRoi，再执行原 context->paused=true；不构造假 CV/图像。
2. 仅在实际 Initialize 或 Process 分支内获取短暂 ChannelPtr、构造 metadata、BeginFrame，给原 Tracker 增加 lease.Tracker() 可空参数。无需为 AwaitTracking 构造大型快照。
3. 保留实际 result.accepted、Target 发布、WakeHost、context 标志和原 result store 的顺序。Process 的 result.state==Paused 时先 CAS latch TrackerPaused，再执行原 paused.store(true)。仅在已经取得有效 active Channel 的路径，在 BeginFrame 复制之后、原 Tracker 调用之前读取 steady_clock，调用返回立即再读一次（在 Target 发布/WakeHost/结果锁之前），得到 totalTrackerMs；关闭路径不新增这两个计时调用。RecordMotion 只对真实调用执行一次并接收该值；即使图像复制失败，原结果仍可形成数值摘要。lease.Publish 接收实际 targetPublished 与同一 totalTrackerMs。
4. controller Tick/Change 只从真实一次 owner/Capture snapshot 生成 LiveStatus。质量 rejected 变化 latch CaptureQuality；API failures 变化 latch CaptureApi；!publishing 按实际 reason 对应 startup/result timeout 或 unavailable；clamp 对应 ViewportClamped；原逻辑执行哪条先，第一条成功 CAS 的 cause 保留到新 Context。
5. **首因只存一个小 per-Context atomic PauseCause，不保存 Channel**。Frame 与 controller 在原故障位置 compare_exchange(None,cause)。Tick 会在 Paused 继续进入检查，StopCapture(Requested) 也会改变 Capture publishing；后续 generic unavailable/Requested 不能覆盖首因。状态可以继续更新为 Stopping/Off，但保留该 Context 的真实暂停来源。Initialize 返回 Paused 不等于业务已 Paused，不能新增生产自动暂停逻辑。

## Capture 只读补充

保持 Begin/Update/Read/Stop/Pump/监督的调度与决策。给 Snapshot 追加：`lastReadStarted`、`lastReadCompleted`、`lastAcceptedSample`、`lastSampleInterval`、`lastFailureAt`、`FailureStage { None, Begin, Update, Read, Quality, Callback, End, Timeout }`、`uint32_t nativeError`、`bool nativeErrorValid`。BackendResult 仅尾部追加 nativeError/valid，旧 `{true}` / `{false,reason}` 初始化兼容。

- Pump 原 began/completed 已存在；只在原 phase==Read 的统计更新位置填时间、计算与上次 Read completed 的间隔。accepted 分支复制原 completed；所有这些时间只代表后端 Read 返回，不承诺 compositor 新鲜度。UI 以 lastSampleInterval 显示实际返回采样 FPS，Capture accepted/Tracker accepted 和约 100ms 的预览消费速率分别显示。
- API 失败在原 !result.succeeded 分支记录 phase 对应 stage 和真实 completed；质量失败在原 quality!=Valid 分支记录 Quality；Callback/End/Timeout 在各自已有失败位置记录，不增加重试或撤销。
- nativeError 只能在实际失败 API 返回后立即取得，沿 BackendResult 传出，不能到 Pump/Tick 再 GetLastError。PrintWindow 和 GdiFlush 仍按原顺序都调用（原来即使 printed=false 也执行 flush）；先分别捕获各自返回后错误，再按原 printed 优先失败来源记录。包装 helper/几何校验没有可靠原生错误时 valid=false，显示“无原生错误码”，不把陈旧线程 last-error 当新错误。现有 lastReadDuration 包含整个 Backend::Read，UI 标签必须是 Capture Read duration，不能宣称隔离的 PrintWindow 时间。

## 集成边界补充

- FramePacket 的 Capture statistics/quality/geometry/timestamp 必须从回调的同一个 Frame 按值取，不能计算后再读取全局 GetSnapshot 来拼接为“该帧数据”；LiveStatus 才可以来自 controller 的当前 Snapshot。
- 发布图像的尺寸/偏移严格使用原 Frame 构建的 input.image 与原 left/top/right/bottom。sourceRect 与 frame.width 的既有一像素差不应触发新的裁剪或宽比缩放。
- 没到 Gray 的早退会有 processingWidth/Height 与 scaleX/Y 为零。仍可显示真实原始图像和数值拒绝原因，但必须跳过依赖处理坐标的标记，不能除零、默认乘二或把旧参考尺寸强拼到新几何。
- referenceSequenceBefore 才是箭头起点对应的旧可信参考。拒绝后的恢复可能跨多帧，参考点、光流和主运动需显示该旧序号；snapshot.result.statistics.referenceSequence 是提交后的新序号。
- 固定 ROI 原点属于 desktop/capture 几何；Tracker 点在 ROI 内。绘制 ROI 预览不能再加 desktop/source/ROI 的绝对偏移；要标注桌面坐标时才加一次 sourceRect.origin + roi.origin。
- 新 Follow 会话或冻结期间仍可显示旧 front；图像/同帧统计始终用 packet 的 session/frame，顶部单独标 live 会话和 stale/frozen，不拿新 live 状态替换 old packet 的 MotionStatistics。所有坐标映射以 packet 的实际各轴 scale 为准。

## 五组短测试

1. 奇数尺寸、非零 ROI offset/padded stride；提交后复用原借用缓冲，验证 BGRA 和 Tracker 输入/参考身份同一帧；错误布局/分配失败只丢诊断，不改变生产结果；totalTrackerMs 非负且和 summary/packet 完全一致，计时不包含拷贝或 Target 发布。
2. 连续发布远快于消费：只见最后完整 packet，generation/session/sequence 不串；固定 front 冻结时其余两槽继续交换、30 摘要继续推进；恢复直接拿最新。
3. 最终 Tracker 拒绝帧发布后反复更新 Paused/capture-stop 状态，图像和统计保持失败身份；AwaitTracking/CaptureFailure 的 hasMotion=false，不增加 Tracker rejected。
4. BeginFrame 之后 Deactivate，再同 Follow Activate 新代次；旧 lease 不能进入新代次，无永久 Context 引用，前一 Channel 在最后在途 lease 释放后回收。关闭路径不分配大对象/复制图像。
5. 两个线程竞争首因：Tracker/ROI 或 quality/API 的首个 latch 保留，后续 Requested/!publishing 不覆写。FreezeCapture fake backend 检查成功、API/quality/timeout 时间和有效 nativeError 来源，而原调度/计数/Stop 决策不变。

### 审查补充：原生 last-error 有效性

立即 GetLastError 只是必要条件。API 的 Return Value 文档必须明确保证失败时可查询 extended error 才标 nativeErrorValid=true；PrintWindow、GdiFlush、RedrawWindow 等未有该保证时设 false，不把残留线程错误当可靠原生故障。失败阶段/时间仍保留，原调用顺序和决策保持。
