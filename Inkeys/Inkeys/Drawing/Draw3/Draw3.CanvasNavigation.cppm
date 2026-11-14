module;

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <vector>

export module Inkeys.Drawing.Draw3.canvas_navigation;

export namespace Inkeys::Drawing::Draw3
{
	inline constexpr double kCanvasPanPredictionSeconds = 0.150;
	inline constexpr float kCanvasPanKeyboardStepDip = 64.0f;
	inline constexpr float kCanvasViewportLimitDip = 1048576.0f;
	inline constexpr float kCanvasPanSharpSpeedThresholdDipPerSecond = 300.0f;
	inline constexpr float kCanvasPanMaximumFallbackBlurDip = 12.0f;

	// 默认/间接输入兼容门限；直接输入由 MotionContext 选择空间单位与参数。
	inline constexpr double kCanvasPanMinimumFlingVelocity = 30.0;
	inline constexpr float kCanvasPanTouchSlopDip = 15.0f;
	inline constexpr float kCanvasPanSlopEpsilonDip = 0.05f;
	inline constexpr float kCanvasPanExpandSlopFactor = 1.0f;
	inline constexpr float kCanvasPanScrollEpsilonDip = 0.1f;
	inline constexpr size_t kCanvasPanMaximumContacts = 16;
	inline constexpr size_t kCanvasPanMaximumRejectedContacts = 2048;
	inline constexpr double kCanvasPanTrackerGapSeconds = 0.040;
	inline constexpr double kCanvasPanBoostTimeoutSeconds = 0.050;
	inline constexpr double kCanvasPanBoostMinimumFlingSpeedSquared = 350.0 * 350.0;
	inline constexpr double kCanvasPanBoostMinimumScrollSpeedSquared = 150.0 * 150.0;
	inline constexpr double kCanvasPanBoostMinimumIntervalSeconds = 0.001;

	// 产品释放使用真实逐指尾段与唯一向量摩擦；设备只选择参数。
	inline constexpr double kCanvasPanReleaseWindowSeconds = 0.050;
	inline constexpr double kCanvasPanReleaseMinimumSpanSeconds = 0.020;
	inline constexpr size_t kCanvasPanReleaseMinimumSamples = 3;
	inline constexpr size_t kCanvasPanReleaseCapacity = 64;
	inline constexpr double kCanvasPanReleaseFullAgeSeconds = 0.030;
	inline constexpr double kCanvasPanReleaseZeroAgeSeconds = 0.070;
	inline constexpr double kCanvasPanReleaseReversalDip = 1.0;
	inline constexpr double kCanvasPanReleaseMinimumIntervalSeconds = 0.002;
	inline constexpr double kCanvasPanProductVelocityLimit = 12000.0;
	inline constexpr double kCanvasPanFrictionTauSeconds = 0.30;
	inline constexpr double kCanvasPanFrictionDeceleration = 1000.0;

	struct CanvasVector
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct CanvasViewportState
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct CanvasContentTranslationResult
	{
		CanvasVector viewportDelta = {};
		bool xClamped = false;
		bool yClamped = false;
	};

	// 屏幕位移与 Canvas 原点方向相反；返回被范围保护后的实际原点位移。
	CanvasVector ApplyCanvasContentTranslation(
		CanvasViewportState& viewport, CanvasVector contentDelta) noexcept;
	// 显式报告真正触及硬边界的轴，避免把远端 float 量化误差误判为限位。
	CanvasContentTranslationResult ApplyCanvasContentTranslationChecked(
		CanvasViewportState& viewport, CanvasVector contentDelta) noexcept;
	CanvasVector ScreenToCanvas(CanvasVector screen, CanvasViewportState viewport) noexcept;
	CanvasVector CanvasToScreen(CanvasVector canvas, CanvasViewportState viewport) noexcept;

	enum class CanvasPanSourceClass : uint8_t { None, Touch, Mouse, Pen, PenTail };
	enum class CanvasPanMotionUnit : uint8_t { SystemDip, Millimeters };
	enum class CanvasPanMotionProfile : uint8_t
	{ IndirectDip, TouchPhysical, TouchFallback, ScreenPenPhysical, ScreenPenFallback };

	// 输入/公共输出始终为系统 DIP；只有批次内部采用下列运动度量。
	struct CanvasPanMotionContext
	{
		CanvasPanMotionUnit unit = CanvasPanMotionUnit::SystemDip;
		CanvasPanMotionProfile profile = CanvasPanMotionProfile::IndirectDip;
		double motionPerDipX = 1.0, motionPerDipY = 1.0;
		uint64_t outputKey = 0; // 前端分配的有效输出/几何签名，不使用原始 revision。
		uint64_t sourceGeneration = 0; // 新 producer 代的首指隔离旧残余，不中断本批。
		bool inputMapped = false, mappingConflict = false, largeScreen = false;
		double touchWeight = 0.0, intentGain = 1.0;
		double touchSlop = kCanvasPanTouchSlopDip;
		double slopEpsilon = kCanvasPanSlopEpsilonDip;
		double scrollEpsilon = kCanvasPanScrollEpsilonDip;
		double reversalDistance = kCanvasPanReleaseReversalDip;
		double minimumFlingVelocity = kCanvasPanMinimumFlingVelocity;
		double boostMinimumFlingVelocity = 350.0;
		double boostMinimumScrollVelocity = 150.0;
		double velocityLimit = kCanvasPanProductVelocityLimit;
		friend bool operator==(const CanvasPanMotionContext&, const CanvasPanMotionContext&) = default;
	};
	CanvasPanMotionContext MakeCanvasPanMotionContext(CanvasPanMotionProfile profile,
		double motionPerDipX = 1.0, double motionPerDipY = 1.0) noexcept;
	bool IsCanvasPanMotionContextValid(const CanvasPanMotionContext& context) noexcept;
	struct CanvasPanFrictionParameters
	{
		double tauSeconds = kCanvasPanFrictionTauSeconds;
		double deceleration = kCanvasPanFrictionDeceleration;
	};
	CanvasPanFrictionParameters ResolveCanvasPanFriction(const CanvasPanMotionContext& context,
		double selectedMotionSpeed) noexcept;
	enum class CanvasPanAdmissionReason : uint8_t
	{ Accepted, ClassConflict, MetricConflict, InvalidContext, Capacity };
	const char* CanvasPanAdmissionReasonName(CanvasPanAdmissionReason reason) noexcept;
	struct CanvasPanAdmissionDecision
	{
		uint64_t key = 0;
		int64_t downQpc = 0;
		uint64_t sourceKey = 0;
		bool accepted = false;
		CanvasPanAdmissionReason reason = CanvasPanAdmissionReason::InvalidContext;
	};

	struct CanvasPanVelocitySample
	{
		int64_t qpc = 0;
		double x = 0.0;
		double y = 0.0;
	};

	// 平移触点快照消费判定：序号变化，或终态仍待消费。
	bool ShouldConsumeCanvasPanContactSnapshot(uint64_t snapshotSequence,
		uint64_t lastConsumedSequence, bool terminalPending) noexcept;

	// 活动 Touch 平移中的 Pen contact 由平台批次吞到抬笔；惯性中的新 Down 仍可抢占。
	bool ShouldBeginSuppressingPenContactDuringTouchPan(bool touchPanActive,
		bool penInContact) noexcept;
	bool ShouldSuppressPenContactForTouchPan(bool touchPanContactLive,
		int64_t penDownQpc, int64_t lastTouchPanEndQpc,
		bool suppressedPenContactLive) noexcept;
	bool IsPenContactSampleFresh(bool inContact, int64_t sampleQpc,
		int64_t suppressedTerminalQpc) noexcept;
	// 允许抢占的物理落笔必须先于本帧导航推进消费。
	bool ShouldPrioritizeDrawingContact(bool navigationInProgress,
		bool penInContact, bool mouseInContact) noexcept;
	float CanvasPanFallbackBlurDip(float speedDipPerSecond) noexcept;

	struct CanvasPanVelocityEstimate
	{
		bool valid = false;
		double x = 0.0;
		double y = 0.0;
	};

	struct CanvasPanPointer
	{
		uint64_t key = 0;
		double x = 0.0;
		double y = 0.0;
	};

	enum class CanvasPanReleaseReason : uint8_t
	{
		None, Ready, InsufficientSamples, InsufficientSpan, TimingRejected,
		Stationary, Stale, Cancelled, WithinSlop, EmptyViewport, DirectionConflict, Disabled, InsufficientSupport
	};
	const char* CanvasPanReleaseReasonName(CanvasPanReleaseReason reason) noexcept;
	enum class CanvasPanSampleDisposition : uint8_t { Accepted, Reversed, Rejected };

	struct CanvasPanReleaseSupport
	{
		size_t anchorCount = 0, positiveIntervalCount = 0;
		int64_t firstQpc = 0, lastQpc = 0, supportFirstQpc = 0, supportLastQpc = 0;
		double speedUpperBound = 0;
		bool evaluated = false, valid = false;
	};

	// 每指只记录自己的真实测量；另一指更新和拓扑变更不能伪造同时采样。
	class CanvasPanReleaseVelocityTracker
	{
	public:
		void Configure(const CanvasPanMotionContext& context) noexcept;
		CanvasPanSampleDisposition AddSample(uint64_t key, CanvasPanVelocitySample sample,
			int64_t frequency) noexcept;
		void Clear() noexcept;
		void ClearPointer(uint64_t key) noexcept;
		void ClearAxis(bool x, bool y) noexcept;
		CanvasPanVelocityEstimate Estimate(uint64_t key, int64_t frequency) const noexcept;
		CanvasPanReleaseReason Reason(uint64_t key, int64_t frequency) const noexcept;
		size_t SampleCount(uint64_t key) const noexcept;
		double SampleSpanSeconds(uint64_t key, int64_t frequency) const noexcept;
		std::optional<double> RecentSpeedLimit(uint64_t key, int64_t frequency,
			CanvasVector estimatedVelocity) const noexcept;
		CanvasPanReleaseSupport ReleaseSupport(uint64_t key, int64_t frequency,
			CanvasPanVelocityEstimate estimatedVelocity) const noexcept;
		int64_t LastSampleQpc(uint64_t key) const noexcept;
		int64_t LastSampleQpc() const noexcept;
		// Touch释放使用空间进展时钟；原始观察时间仍由LastSampleQpc提供。
		int64_t LastMotionQpc(uint64_t key) const noexcept;
		size_t MotionSampleCount(uint64_t key) const noexcept;
		bool HasStationaryObservation(uint64_t key) const noexcept;
		bool ShortTouchSupported(uint64_t key, int64_t frequency) const noexcept;

	private:
		struct History
		{
			bool used = false;
			uint64_t key = 0;
			std::array<CanvasPanVelocitySample, kCanvasPanReleaseCapacity> samples = {};
			size_t count = 0;
			std::array<CanvasPanVelocitySample, kCanvasPanReleaseCapacity> progressSamples = {};
			size_t progressCount = 0; // 仅Touch使用，固定容量，不在Move分配。
			CanvasVector direction = {};
			std::optional<CanvasPanVelocitySample> reversalAnchor;

		};
		const History* Find(uint64_t key) const noexcept;
		std::span<const CanvasPanVelocitySample> MotionSamples(const History& history) const noexcept;
		std::array<History, kCanvasPanMaximumContacts> histories_ = {};
		double reversalDistance_ = kCanvasPanReleaseReversalDip;
		bool independentFit_ = false, touchProgress_ = false;
	};

	enum class TouchReleaseMode : uint8_t { NotEvaluated, Stop, Measured, ShortSupported, ResidualOnly, DistanceBounded };
	enum class TouchReleaseReason : uint8_t { None, Cancelled, Disabled, NoScroll, ConfirmedStop, StaleRaw, Unsupported, Velocity, Residual, Distance };
	enum class TouchResponseSource : uint8_t { NotEvaluated, Measured, Base };
	struct TouchVelocityEvidence
	{
		CanvasVector raw = {}, velocity = {};
		int64_t motionQpc = 0;
		uint64_t contactKey = 0;
		bool valid = false, shortSupported = false, supportLimited = false;
	};
	struct TouchReleaseFacts
	{
		CanvasPanMotionContext context;
		TouchVelocityEvidence evidence;
		CanvasVector direction = {}, confirmedNet = {}, residual = {};
		double rawAgeSeconds = 0, motionAgeSeconds = 0, scrollAgeSeconds = 0;
		bool scrolled = false, cancelled = false, allowInertia = true, residualEligible = false, platformObserved = false, budgetConfirmed = false, confirmedStopped = false;
	};
	struct TouchReleaseDecision
	{
		TouchReleaseMode mode = TouchReleaseMode::NotEvaluated;
		TouchReleaseReason reason = TouchReleaseReason::None;
		TouchResponseSource response = TouchResponseSource::NotEvaluated;
		CanvasVector handVelocity = {}, velocitySeed = {};
		CanvasPanFrictionParameters parameters;
		uint64_t evidenceContactKey = 0;
		int64_t evidenceMotionQpc = 0;
		double distanceBudget = 0;
		bool addedResidual = false, boostSuppressed = false;
	};
	TouchReleaseDecision ResolveTouchRelease(const TouchReleaseFacts& facts) noexcept;
	const char* TouchReleaseModeName(TouchReleaseMode mode) noexcept;
	struct TouchMotionTag
	{
		uint64_t group = 0, leg = 0, epoch = 0, metric = 0;
		friend bool operator==(const TouchMotionTag&, const TouchMotionTag&) = default;
	};
	struct TouchMotionFeedback
	{
		TouchMotionTag tag;
		int64_t frameQpc = 0, scrollQpc = 0;
		bool present = false, eligible = false;
	};

	enum class CanvasPanPhase : uint8_t { Idle, Coasting, Locked };
	enum class CanvasPanContactPhase : uint8_t { Down, Move, Up, Cancelled };

	struct CanvasPanContactSample
	{
		uint64_t key = 0;
		int64_t qpc = 0;
		double x = 0.0;
		double y = 0.0;
		CanvasPanContactPhase phase = CanvasPanContactPhase::Move;
		uint64_t sourceKey = 0; // 调用方提供设备类别/来源身份，内核不依赖平台类型。
		bool allowInertia = true; // 只门控释放，不丢弃真实跟手与终态。
		size_t motionContextIndex = 0; // 仅 Down 读取，索引只在本次 Step 有效。
	};

	enum class CanvasPanGestureType : uint8_t { ScrollBegin, ScrollUpdate, ScrollEnd, FlingStart, FlingCancel };
	struct CanvasPanGestureEvent
	{
		CanvasPanGestureType type = CanvasPanGestureType::ScrollUpdate;
		double timeSeconds = 0.0;
		CanvasVector delta = {};
		CanvasVector velocity = {};
		uint32_t sourceDevice = 0;
		uint32_t modifiers = 0;
		bool inertial = false;
		bool preventBoosting = false;
	};

	struct CanvasPanGestureBatch
	{
		std::array<CanvasPanGestureEvent, 2> events = {};
		size_t count = 0;
	};

	class CanvasPanGestureDetector
	{
	public:
		void Configure(const CanvasPanMotionContext& context) noexcept;
		CanvasPanGestureBatch Down(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			int64_t frequency) noexcept;
		CanvasPanGestureBatch Move(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			int64_t frequency) noexcept;
		CanvasPanGestureBatch Up(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			uint64_t actionKey, bool cancelled, int64_t frequency,
			CanvasPanVelocityEstimate releaseVelocity) noexcept;
		void Reset() noexcept;
		bool Scrolling() const noexcept;

	private:
		CanvasVector lastFocus_ = {};
		CanvasPanPointer primaryDown_ = {};
		std::array<CanvasPanPointer, 2> secondaryDown_ = {};
		size_t secondaryCount_ = 0;
		size_t maximumPointerCount_ = 0;
		bool withinSlop_ = true;
		bool scrollSent_ = false;
		float touchSlop_ = kCanvasPanTouchSlopDip;
		float slopEpsilon_ = kCanvasPanSlopEpsilonDip;
		float scrollEpsilon_ = kCanvasPanScrollEpsilonDip;
		double minimumFlingVelocity_ = kCanvasPanMinimumFlingVelocity;
	};

	class CanvasPanFlingBooster
	{
	public:
		void Configure(const CanvasPanMotionContext& context) noexcept;
		CanvasVector GetVelocityForFlingStart(const CanvasPanGestureEvent& event) noexcept;
		bool ShouldBoostFling(const CanvasPanGestureEvent& event) const noexcept;
		void ObserveGestureEvent(const CanvasPanGestureEvent& event) noexcept;
		void ObserveProgressFling(CanvasVector velocity) noexcept;
		void Reset() noexcept;
		CanvasVector StartingVelocity() const noexcept;
		CanvasVector CurrentVelocity() const noexcept;
		double CutoffSeconds() const noexcept;

	private:
		CanvasVector startingVelocity_ = {};
		CanvasVector currentVelocity_ = {};
		std::optional<double> cutoff_;
		std::optional<double> previousScrollTime_;
		uint32_t sourceDevice_ = 0;
		uint32_t modifiers_ = 0;
		double minimumFlingSpeedSquared_ = kCanvasPanBoostMinimumFlingSpeedSquared;
		double minimumScrollSpeedSquared_ = kCanvasPanBoostMinimumScrollSpeedSquared;
		double velocityLimit_ = kCanvasPanProductVelocityLimit;
	};

	class CanvasPanFlingCurve
	{
	public:
		void Start(CanvasVector velocity, CanvasVector viewportSizeDip,
			CanvasPanFrictionParameters parameters = {}) noexcept;
		bool Advance(double elapsedSeconds, CanvasVector& offset, CanvasVector& velocity) noexcept;
		void ClearAxis(bool x, bool y) noexcept;
		double Duration() const noexcept;
		CanvasVector Distance() const noexcept;

	private:
		CanvasVector distance_ = {};
		double duration_ = 0.0;
		double frictionInitialSpeed_ = 0.0;
		CanvasVector frictionDirection_ = {};
		CanvasPanFrictionParameters parameters_ = {};
	};

	class CanvasPanFlingController
	{
	public:
		void Start(const CanvasPanGestureEvent& event, double lastScrollSeconds,
			CanvasVector viewportSizeDip, CanvasPanFlingBooster& booster,
			CanvasPanFrictionParameters parameters = {}) noexcept;
		CanvasVector Progress(double nowSeconds, CanvasPanFlingBooster& booster) noexcept;
		void Cancel() noexcept;
		void StopFling(CanvasPanFlingBooster& booster) noexcept;
		void ClearAxis(bool x, bool y, CanvasPanFlingBooster& booster) noexcept;
		bool Active() const noexcept;
		CanvasVector Velocity() const noexcept;
		CanvasVector Offset() const noexcept;

	private:
		CanvasPanFlingCurve curve_;
		CanvasVector cumulative_ = {};
		CanvasVector velocity_ = {};
		double startSeconds_ = 0.0;
		double lastProgressSeconds_ = 0.0;
		bool active_ = false;
		bool clearX_ = false;
		bool clearY_ = false;
	};

	struct CanvasPanFrameInput
	{
		int64_t frameQpc = 0;
		int64_t qpcFrequency = 0;
		std::span<const CanvasPanContactSample> contacts = {};
		bool penHover = false; // 仅 fling 中立即停止并清 booster；不打断手按住的拖动。
		bool penContact = false;
		CanvasVector viewportSizeDip = {}; // 由调用方传入客户区 DIP 尺寸；空视口不启动 fling。
		std::span<const CanvasPanMotionContext> motionContexts = {};
		std::span<CanvasPanAdmissionDecision> admissionDecisions = {};
	};

	struct CanvasPanPointerDiagnostic
	{
		uint64_t key = 0;
		size_t sampleCount = 0;
		CanvasVector velocity = {};
	};

	struct CanvasPanFrameOutput
	{
		CanvasVector contentDelta = {};
		CanvasVector contentVelocity = {};
		CanvasPanPhase phase = CanvasPanPhase::Idle;
		uint64_t sourceKey = 0; // 动力学实际接受的最后来源；不从 producer 出队顺序推测惯性归属。
		size_t heldContacts = 0;
		CanvasVector handVelocity = {};
		bool handVelocityKnown = false;
		bool scrolling = false;
		bool released = false;
		double releaseSpeed = 0.0;
		double releaseAgeSeconds = 0.0;
		CanvasVector rawReleaseVelocity = {};
		CanvasVector selectedReleaseVelocity = {};
		CanvasVector boostedReleaseVelocity = {};
		CanvasPanReleaseReason releaseReason = CanvasPanReleaseReason::None;
		size_t releaseSampleCount = 0;
		double releaseSpanSeconds = 0.0;
		double releaseTailSpeedLimit = 0.0;
		bool releaseTailSpeedLimited = false;
		uint64_t releaseContactKey = 0;
		CanvasPanReleaseSupport releaseSupport = {}; // 速度上界用内部运动单位，QPC保持真实事件时钟。
		bool releaseSupportLimited = false, releaseOutputLimited = false, releaseBoostSuppressed = false;
		size_t releaseMotionSampleCount = 0;
		int64_t releaseLastRawQpc = 0, releaseLastMotionQpc = 0;
		bool releaseMotionAgeLimited = false;
		TouchReleaseDecision touchDecision;
		TouchMotionTag releaseTouchTag;
		TouchMotionFeedback motionFeedback;
		double releaseRawAgeSeconds = 0, releasePlatformSeconds = 0;
		size_t rejectedSamples = 0;
		size_t reversalCount = 0;
		size_t timingResetCount = 0;
		CanvasVector focus = {};
		double span = 0.0;
		size_t pointerCount = 0;
		double scaleDelta = 1.0;
		size_t sampleCount = 0;
		std::array<CanvasPanPointerDiagnostic, kCanvasPanMaximumContacts> pointerDiagnostics = {};
		CanvasVector boosterStartingVelocity = {};
		CanvasVector boosterCurrentVelocity = {};
		double boosterCutoffSeconds = 0.0;
		CanvasVector flingOffset = {};
		size_t admissionDecisionCount = 0;
		bool admissionFailed = false; // 新生产调用缓冲不足时，消费前失败，由前端 Stop。
		uint64_t releaseSourceKey = 0;
		CanvasPanMotionContext releaseMotionContext = {};
		CanvasVector releaseMotionVelocity = {};
		CanvasPanFrictionParameters frictionParameters = {};
	};

	class CanvasPanDynamics
	{
	public:
		CanvasPanFrameOutput Step(const CanvasPanFrameInput& input) noexcept;
		void Stop() noexcept;
		void StopFling() noexcept;
		bool ConfirmAppliedMotion(const TouchMotionFeedback& feedback, CanvasVector actualContentDeltaDip) noexcept;
		void ClearAxis(bool x, bool y) noexcept;
		CanvasPanPhase Phase() const noexcept;
		bool Active() const noexcept;
		CanvasVector ContentVelocity() const noexcept;
		size_t HeldContactCount() const noexcept;
		// 调试 tracker 保留运动域单位；其它公共速度均为系统 DIP/s。
		const CanvasPanReleaseVelocityTracker& VelocityTracker() const noexcept;
		const CanvasPanMotionContext& MotionContext() const noexcept;

	private:
		struct Contact
		{
			bool held = false;
			CanvasPanPointer pointer = {};
			int64_t lastQpc = 0;
		};
		CanvasPanFrameOutput Snapshot(int64_t frequency, CanvasVector* handMotion = nullptr) const noexcept;
		void Dispatch(const CanvasPanGestureBatch& batch, const CanvasPanFrameInput& input,
			CanvasPanFrameOutput& output) noexcept;
		std::array<Contact, kCanvasPanMaximumContacts> contacts_ = {};
		CanvasPanGestureDetector detector_;
		CanvasPanReleaseVelocityTracker releaseTracker_;
		CanvasPanFlingBooster booster_;
		CanvasPanFlingController fling_;
		CanvasVector velocity_ = {};
		int64_t lastFrameQpc_ = 0;
		uint64_t lastSourceKey_ = 0;
		CanvasPanMotionContext motionContext_ = {};
		struct TouchGroup
		{
			TouchMotionTag tag;
			TouchVelocityEvidence candidate;
			CanvasVector requestedNet = {}, confirmedNet = {}, residual = {};
			int64_t lastScrollQpc = 0, lastRawQpc = 0, lastConfirmedFrame = 0, intentQpc = 0;
			bool scrolled = false, residualEligible = false, budgetConfirmed = false, distanceEligible = true, confirmedStopped = false;
		} touch_;
		uint64_t touchGroupSerial_ = 0, touchLegSerial_ = 0, touchEpoch_ = 1, touchMetricSerial_ = 0;
		void BeginTouchLeg(bool requireIndependent = false, int64_t qpc = 0) noexcept;
		void RecordTouchScroll(CanvasVector delta, int64_t qpc, const CanvasPanFrameInput& input, CanvasPanFrameOutput& output) noexcept;
		bool CacheTouchEvidence(int64_t motionQpc, int64_t frequency) noexcept;
		struct RejectedContact
		{
			uint64_t key = 0;
			CanvasPanAdmissionReason reason = CanvasPanAdmissionReason::InvalidContext;
			bool suppressesPen = false;
		};
		std::vector<RejectedContact> rejectedContacts_;
		std::vector<uint64_t> earlyTerminalKeys_; // 只记录本帧排到 Down 前的坏终态身份。
	};

	struct CanvasRenderBudgetInput
	{
		double targetFrameMilliseconds = 1000.0 / 120.0;
		double workMilliseconds = 0.0;
		double presentMilliseconds = 0.0;
		double tileEwmaMilliseconds = 0.5;
	};

	struct CanvasRenderBudget
	{
		double milliseconds = 0.0;
		size_t maximumTiles = 0;
	};

	CanvasRenderBudget ComputeCanvasRenderBudget(
		const CanvasRenderBudgetInput& input) noexcept;
	// 权威 L2 写入失败后必须等待 tile 恢复，不能沿用旧的“可见区清晰”状态。
	bool CanvasVisibleClarityAfterAuthoritativeWrite(
		bool wasClear, bool writeSucceeded) noexcept;
	CanvasVector ComputeCanvasPredictionOffset(CanvasVector velocity,
		float viewportWidth, float viewportHeight) noexcept;

	struct CanvasRect
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	// 返回当前视口、150ms 预测扫掠和前后各一圈 tile 余量的查询范围。
	std::optional<CanvasRect> ComputeCanvasRenderCoverageBounds(
		CanvasViewportState viewport, float viewportWidth, float viewportHeight,
		CanvasVector contentVelocity, uint32_t tileSize) noexcept;

	struct CanvasTileCoordinate
	{
		int32_t x = 0;
		int32_t y = 0;
		friend auto operator<=>(const CanvasTileCoordinate&,
			const CanvasTileCoordinate&) noexcept = default;
	};

	enum class CanvasTilePriority : uint8_t
	{
		Visible,
		LeadingEdge,
		Predicted,
		Trailing
	};

	struct CanvasPlannedTile
	{
		CanvasTileCoordinate tile = {};
		CanvasTilePriority priority = CanvasTilePriority::Trailing;
	};

	struct CanvasRenderTilePlan
	{
		std::vector<CanvasPlannedTile> tiles;
		size_t visibleTileCount = 0;
	};

	// 只规划已有内容 Tile；一圈余量内按可见、前缘、预测、后缘排序。
	CanvasRenderTilePlan PlanCanvasRenderTiles(
		std::span<const CanvasTileCoordinate> contentTiles,
		CanvasViewportState viewport, float viewportWidth, float viewportHeight,
		CanvasVector contentVelocity, uint32_t tileSize);
	// 快照只允许覆盖两个真实世界视口的交集，未知像素保持透明。
	std::optional<CanvasRect> ComputeCanvasSnapshotScreenIntersection(
		CanvasViewportState snapshotViewport, CanvasViewportState currentViewport,
		float viewportWidth, float viewportHeight) noexcept;
}
