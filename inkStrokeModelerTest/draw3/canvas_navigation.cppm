module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module draw3.canvas_navigation;

export namespace draw3
{
	inline constexpr double kCanvasPanGestureWindowSeconds = 0.180;
	inline constexpr double kCanvasPanMomentumBlendSeconds = 0.120;
	inline constexpr double kCanvasPanReleaseVelocityHorizonSeconds = 0.100;
	inline constexpr double kCanvasPanPredictionSeconds = 0.150;
	inline constexpr float kCanvasPanKeyboardStepDip = 64.0f;
	inline constexpr float kCanvasViewportLimitDip = 1048576.0f;
	inline constexpr float kCanvasPanMaximumSpeedDipPerSecond = 24000.0f;
	inline constexpr float kCanvasPanInertiaDecelerationDipPerSecondSquared = 1200.0f;
	inline constexpr float kCanvasPanPenBrakeDecelerationDipPerSecondSquared = 12000.0f;
	inline constexpr float kCanvasPanSharpSpeedThresholdDipPerSecond = 300.0f;
	inline constexpr float kCanvasPanMaximumFallbackBlurDip = 12.0f;

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

	// 屏幕位移与 Canvas 原点方向相反；返回被范围保护后的实际原点位移。
	CanvasVector ApplyCanvasContentTranslation(
		CanvasViewportState& viewport, CanvasVector contentDelta) noexcept;
	CanvasVector ScreenToCanvas(CanvasVector screen, CanvasViewportState viewport) noexcept;
	CanvasVector CanvasToScreen(CanvasVector canvas, CanvasViewportState viewport) noexcept;

	enum class CanvasTouchDisposition : uint8_t
	{
		Draw,
		PanCandidate,
		Pan,
		Suppressed
	};

	struct CanvasTouchDecision
	{
		CanvasTouchDisposition disposition = CanvasTouchDisposition::Draw;
		bool beginPan = false;
		bool cancelExistingTouchDrawing = false;
		bool joinedExistingPan = false;
	};

	// 只管理触摸批次归属；contact 位置和生命周期仍由 RTS coordinator 持有。
	class CanvasTouchGestureState
	{
	public:
		CanvasTouchDecision OnTouchDown(uint64_t contactKey, int64_t qpc,
			int64_t qpcFrequency, bool inertiaActive, bool blockingContactActive) noexcept;
		CanvasTouchDisposition OnTouchUp(uint64_t contactKey) noexcept;
		void Update(int64_t qpc, int64_t qpcFrequency) noexcept;
		void InterruptForPenOrMouse() noexcept;
		void Reset() noexcept;
		bool PanActive() const noexcept;
		bool InertiaCandidateActive() const noexcept;
		// 惯性候选超时且首指仍按下时，请求快速制动但继续吞掉该指。
		bool InertiaBrakeRequested() const noexcept;
		bool BatchAllowsPan() const noexcept;
		size_t GestureContactCount() const noexcept;
		CanvasTouchDisposition Disposition(uint64_t contactKey) const noexcept;

	private:
		struct ContactState
		{
			uint64_t key = 0;
			CanvasTouchDisposition disposition = CanvasTouchDisposition::Draw;
		};

		std::vector<ContactState> contacts_;
		int64_t firstDownQpc_ = 0;
		bool batchStartedEligible_ = true;
		bool batchAllowsPan_ = true;
		bool panActive_ = false;
		bool inertiaCandidate_ = false;
	};

	struct CanvasPanMotionState
	{
		CanvasVector velocity = {};
		CanvasVector inheritedVelocity = {};
		double inheritedBlendRemainingSeconds = 0.0;
		bool inertiaActive = false;
	};

	void BeginCanvasPan(CanvasPanMotionState& motion, bool inheritInertia) noexcept;
	CanvasVector UpdateCanvasPan(CanvasPanMotionState& motion,
		CanvasVector contentDelta, double deltaSeconds) noexcept;
	void SetCanvasPanVelocity(CanvasPanMotionState& motion, CanvasVector velocity) noexcept;
	void EndCanvasPan(CanvasPanMotionState& motion,
		double secondsSinceLastInput = 0.0) noexcept;
	CanvasVector StepCanvasPanInertia(CanvasPanMotionState& motion,
		double deltaSeconds, bool penInRange) noexcept;
	void StopCanvasPan(CanvasPanMotionState& motion) noexcept;
	void InterruptCanvasPanForDrawing(CanvasPanMotionState& motion,
		CanvasTouchGestureState& gesture) noexcept;
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
	float CanvasPanSpeed(const CanvasPanMotionState& motion) noexcept;
	float CanvasPanFallbackBlurDip(float speedDipPerSecond) noexcept;

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
