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

	// Chromium 阈值：ui/events/gesture_detection/{gesture_detector.cc,gesture_configuration.h}。
	// LSQ2/连甩：ui/events/{velocity_tracker/velocity_tracker.cc,blink/fling_booster.cc}。
	// 曲线：ui/events/gestures/{physics_based_fling_curve,fling_curve}.cc；贝塞尔：ui/gfx/geometry/cubic_bezier.cc。
	// 推进时序：components/input/fling_controller.cc。
	// 单位为 DIP、DIP/s、秒；ppi=96 是本模块 DIP 输入的适配，不是物理屏幕 PPI。
	inline constexpr double kCanvasPanVelocityLimitDipPerSecond = 17000.0;
	inline constexpr double kCanvasPanMinimumFlingVelocity = 30.0;
	inline constexpr float kCanvasPanTouchSlopDip = 15.0f;
	inline constexpr float kCanvasPanSlopEpsilonDip = 0.05f;
	inline constexpr float kCanvasPanExpandSlopFactor = 1.0f;
	inline constexpr float kCanvasPanScrollEpsilonDip = 0.1f;
	inline constexpr double kCanvasPanTrackerWindowSeconds = 0.100;
	inline constexpr size_t kCanvasPanTrackerCapacity = 20;
	inline constexpr size_t kCanvasPanMaximumContacts = 16;
	inline constexpr double kCanvasPanTrackerGapSeconds = 0.040;
	inline constexpr double kCanvasPanTrackerUpGapSeconds = 0.080;
	inline constexpr float kCanvasPanPixelsPerInch = 96.0f;
	inline constexpr float kCanvasPanPixelDeceleration = 2300.0f;
	inline constexpr float kCanvasPanPhysicalDeceleration = 2.7559e-5f;
	inline constexpr float kCanvasPanMaximumFlingSeconds = 2.0f;
	inline constexpr float kCanvasPanFlingBoundsMultiplier = 3.0f;
	inline constexpr float kCanvasPanBezierP1X = 0.2f;
	inline constexpr float kCanvasPanBezierP1Y = 1.0f;
	inline constexpr float kCanvasPanBezierP2X = 0.55f;
	inline constexpr float kCanvasPanBezierP2Y = 1.0f;
	inline constexpr double kCanvasPanBezierEpsilon = 1e-7;
	inline constexpr double kCanvasPanBoostTimeoutSeconds = 0.050;
	inline constexpr double kCanvasPanBoostMinimumFlingSpeedSquared = 350.0 * 350.0;
	inline constexpr double kCanvasPanBoostMinimumScrollSpeedSquared = 150.0 * 150.0;
	inline constexpr double kCanvasPanBoostMinimumIntervalSeconds = 0.001;
	inline constexpr double kCanvasPanFlingFrameSeconds = 1.0 / 60.0;
	inline constexpr float kCanvasPanLegacyAlpha = -5.70762e3f;
	inline constexpr float kCanvasPanLegacyBeta = 1.72e2f;
	inline constexpr float kCanvasPanLegacyGamma = 3.7f;

	// 白板产品策略使用真实逐指尾段与向量摩擦；上方 Chromium 常量仅保留数值参考。
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

	enum class CanvasPanFlingCurveKind : uint8_t { PhysicsBased, Legacy, Friction };
	inline constexpr CanvasPanFlingCurveKind kCanvasPanFlingCurveKind = CanvasPanFlingCurveKind::PhysicsBased;

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

	// Chromium LSQ2：一个多指 tracker，共享 movement 环；遇到缺失 id 的 movement 即停止回溯。
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

	class CanvasPanVelocityTracker
	{
	public:
		bool AddMovement(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			int64_t qpcFrequency) noexcept;
		void Clear() noexcept;
		void Reset() noexcept;
		void ClearPointer(uint64_t key) noexcept;
		void PointerUp(int64_t qpc, int64_t qpcFrequency) noexcept;
		void ClearAxis(bool x, bool y) noexcept;
		CanvasPanVelocityEstimate Estimate(uint64_t key, int64_t qpcFrequency) const noexcept;
		size_t SampleCount(uint64_t key) const noexcept;
		int64_t LastSampleQpc() const noexcept;

	private:
		struct Movement
		{
			int64_t qpc = 0;
			size_t count = 0;
			std::array<CanvasPanPointer, kCanvasPanMaximumContacts> pointers = {};
		};
		std::array<Movement, kCanvasPanTrackerCapacity> movements_ = {};
		size_t index_ = 0;
		size_t count_ = 0;
		int64_t lastEventQpc_ = 0;
	};

	enum class CanvasPanReleaseReason : uint8_t
	{
		None, Ready, InsufficientSamples, InsufficientSpan, TimingRejected,
		Stationary, Stale, Cancelled, WithinSlop, EmptyViewport, DirectionConflict, Disabled
	};
	const char* CanvasPanReleaseReasonName(CanvasPanReleaseReason reason) noexcept;
	enum class CanvasPanSampleDisposition : uint8_t { Accepted, Reversed, TimingRejected, Rejected };

	// 每指只记录自己的真实测量；另一指更新和拓扑变更不能伪造同时采样。
	class CanvasPanReleaseVelocityTracker
	{
	public:
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
		int64_t LastSampleQpc(uint64_t key) const noexcept;
		int64_t LastSampleQpc() const noexcept;

	private:
		struct History
		{
			bool used = false;
			uint64_t key = 0;
			std::array<CanvasPanVelocitySample, kCanvasPanReleaseCapacity> samples = {};
			size_t count = 0;
			CanvasVector direction = {};
			std::optional<CanvasPanVelocitySample> reversalAnchor;
			bool timingRejected = false;
		};
		const History* Find(uint64_t key) const noexcept;
		std::array<History, kCanvasPanMaximumContacts> histories_ = {};
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
		CanvasPanGestureBatch Down(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			uint64_t actionKey, int64_t frequency, bool trackVelocity = true) noexcept;
		CanvasPanGestureBatch Move(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			int64_t frequency, bool trackVelocity = true) noexcept;
		CanvasPanGestureBatch Up(int64_t qpc, std::span<const CanvasPanPointer> pointers,
			uint64_t actionKey, bool cancelled, int64_t frequency,
			std::optional<CanvasPanVelocityEstimate> releaseOverride = {}) noexcept;
		void Reset() noexcept;
		bool Scrolling() const noexcept;
		CanvasPanVelocityTracker& Tracker() noexcept;
		const CanvasPanVelocityTracker& Tracker() const noexcept;

	private:
		CanvasPanVelocityTracker tracker_;
		CanvasVector lastFocus_ = {};
		CanvasPanPointer primaryDown_ = {};
		std::array<CanvasPanPointer, 2> secondaryDown_ = {};
		size_t secondaryCount_ = 0;
		size_t maximumPointerCount_ = 0;
		bool withinSlop_ = true;
		bool scrollSent_ = false;
	};

	class CanvasPanFlingBooster
	{
	public:
		CanvasVector GetVelocityForFlingStart(const CanvasPanGestureEvent& event,
			bool projectCurrentResidual = false) noexcept;
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
	};

	double SolveCanvasPanCubicBezier(double x, double p1x, double p1y,
		double p2x = kCanvasPanBezierP2X, double p2y = kCanvasPanBezierP2Y) noexcept;

	class CanvasPanFlingCurve
	{
	public:
		void Start(CanvasVector velocity, CanvasVector viewportSizeDip, float boostMultiplier = 1.0f,
			CanvasPanFlingCurveKind kind = kCanvasPanFlingCurveKind) noexcept;
		bool Advance(double elapsedSeconds, CanvasVector& offset, CanvasVector& velocity) noexcept;
		void ClearAxis(bool x, bool y) noexcept;
		double Duration() const noexcept;
		CanvasVector Distance() const noexcept;
		CanvasVector P1() const noexcept;

	private:
		CanvasPanFlingCurveKind kind_ = kCanvasPanFlingCurveKind;
		CanvasVector distance_ = {};
		CanvasVector p1_ = {};
		CanvasVector previousOffset_ = {};
		CanvasVector legacyRatio_ = {};
		double duration_ = 0.0;
		double previousSeconds_ = 0.0;
		double legacyTimeOffset_ = 0.0;
		double legacyPositionOffset_ = 0.0;
		double frictionInitialSpeed_ = 0.0;
		CanvasVector frictionDirection_ = {};
	};

	class CanvasPanFlingController
	{
	public:
		void Start(const CanvasPanGestureEvent& event, double lastScrollSeconds,
			CanvasVector viewportSizeDip, CanvasPanFlingBooster& booster,
			CanvasPanFlingCurveKind kind = kCanvasPanFlingCurveKind) noexcept;
		CanvasVector Progress(double nowSeconds, CanvasPanFlingBooster& booster) noexcept;
		void Cancel() noexcept;
		void StopFling(CanvasPanFlingBooster& booster) noexcept;
		void ClearAxis(bool x, bool y, CanvasPanFlingBooster& booster) noexcept;
		bool Active() const noexcept;
		CanvasVector Velocity() const noexcept;
		CanvasVector Offset() const noexcept;
		double StartSeconds() const noexcept;

	private:
		CanvasPanFlingCurve curve_;
		bool productFriction_ = false;
		CanvasVector cumulative_ = {};
		CanvasVector velocity_ = {};
		double startSeconds_ = 0.0;
		double lastProgressSeconds_ = 0.0;
		bool active_ = false;
		bool firstUpdateSent_ = false;
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
	};

	class CanvasPanDynamics
	{
	public:
		CanvasPanFrameOutput Step(const CanvasPanFrameInput& input) noexcept;
		void Stop() noexcept;
		void StopFling() noexcept;
		void ClearAxis(bool x, bool y) noexcept;
		CanvasPanPhase Phase() const noexcept;
		bool Active() const noexcept;
		CanvasVector ContentVelocity() const noexcept;
		size_t HeldContactCount() const noexcept;
		const CanvasPanReleaseVelocityTracker& VelocityTracker() const noexcept;
		// 预置首触点的历史，只供估速/基线，不派发 scroll；本任务不接生产输入。
		bool SeedContactHistory(uint64_t key, std::span<const CanvasPanVelocitySample> samples,
			int64_t qpcFrequency) noexcept;

	private:
		struct Contact
		{
			bool held = false;
			CanvasPanPointer pointer = {};
			int64_t lastQpc = 0;
		};
		CanvasPanFrameOutput Snapshot(int64_t frequency) const noexcept;
		void Dispatch(const CanvasPanGestureBatch& batch, const CanvasPanFrameInput& input,
			CanvasPanFrameOutput& output) noexcept;
		std::array<Contact, kCanvasPanMaximumContacts> contacts_ = {};
		CanvasPanGestureDetector detector_;
		CanvasPanReleaseVelocityTracker releaseTracker_;
		CanvasPanFlingBooster booster_;
		CanvasPanFlingController fling_;
		CanvasVector velocity_ = {};
		int64_t lastFrameQpc_ = 0;
		int64_t lastInputQpc_ = 0;
		uint64_t lastSourceKey_ = 0;
		std::optional<double> lastScrollSeconds_;
		uint64_t seedKey_ = 0;
		std::vector<CanvasPanVelocitySample> seedSamples_;
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
