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

	// ---- 摩擦耦合平移动力学（画布内核，纯 CPU）----
	// 手感相关常量全部集中在本块，待真机标定；单位为 DIP、DIP/s、DIP/s²、秒。
	// 这些是实验参数，不是持久化或公开接口的兼容契约。
	inline constexpr double kCanvasPanVelocityLimitDipPerSecond = 12000.0;
	inline constexpr double kCanvasPanStopSpeedDipPerSecond = 20.0;
	inline constexpr double kCanvasPanMinimumReleaseSpeedDipPerSecond = 250.0;
	inline constexpr double kCanvasPanMaximumStepSeconds = 0.100;
	// 自由摩擦（画布对背景）与手摩擦（手指对画布）：dv/dt = -v/tau - c*sign(v)。
	inline constexpr double kCanvasPanFreeFrictionTauSeconds = 0.30;
	inline constexpr double kCanvasPanFreeFrictionCoulombDipPerSecondSquared = 1000.0;
	inline constexpr double kCanvasPanHandFrictionTauSeconds = 0.06;
	inline constexpr double kCanvasPanHandFrictionCoulombDipPerSecondSquared = 6000.0;
	// 释放门限：最后样本到抬起的年龄 <=30 ms 满速，>=70 ms 为零，其间线性。
	inline constexpr double kCanvasPanReleaseGateFullSeconds = 0.030;
	inline constexpr double kCanvasPanReleaseGateZeroSeconds = 0.070;
	inline constexpr double kCanvasPanLiftedHandHoldSeconds = 0.080;
	// 每触点估速器：窗口、容量、有效性、合并与断口。
	inline constexpr double kCanvasPanTrackerWindowSeconds = 0.100;
	inline constexpr size_t kCanvasPanTrackerCapacity = 20;
	inline constexpr size_t kCanvasPanTrackerMinimumSamples = 3;
	inline constexpr double kCanvasPanTrackerMinimumSpanSeconds = 0.020;
	inline constexpr double kCanvasPanTrackerMergeSeconds = 0.002;
	inline constexpr double kCanvasPanTrackerGapSeconds = 0.040;
	inline constexpr double kCanvasPanTrackerGapFramePaddingSeconds = 0.016;
	// 手速未知时的“手静止”证据：位移小且已按下足够久；或从未形成有效速度但已同样久不再上报。
	inline constexpr double kCanvasPanHandStationaryDelaySeconds = 0.030;
	inline constexpr double kCanvasPanHandStationaryDisplacementDip = 4.0;
	inline constexpr size_t kCanvasPanMaximumContacts = 16;

	struct CanvasPanFriction
	{
		double tauSeconds = 0.0;
		double coulombDipPerSecondSquared = 0.0;
	};

	inline constexpr CanvasPanFriction kCanvasPanFreeFriction{
		kCanvasPanFreeFrictionTauSeconds,
		kCanvasPanFreeFrictionCoulombDipPerSecondSquared };
	inline constexpr CanvasPanFriction kCanvasPanHandFriction{
		kCanvasPanHandFrictionTauSeconds,
		kCanvasPanHandFrictionCoulombDipPerSecondSquared };

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

	// ---- 摩擦耦合平移动力学：公共接口 ----
	// 画布与背景之间是较小的自由摩擦；手与画布之间是较大的手摩擦。按下惯性中的画布时，
	// 相对速度 w = v画布 - v手 按手摩擦衰减，趋零后“抓住”并 1:1 跟手。
	// 本组类型不依赖窗口/D3D，位置与速度均以 DIP 计；像素换算由调用方完成。
	struct CanvasPanVelocityEstimate
	{
		bool valid = false;
		double x = 0.0;
		double y = 0.0;
	};

	struct CanvasPanFrictionStep
	{
		double distance = 0.0;
		double speed = 0.0;
	};

	// dv/dt = -v/tau - c 的逐帧精确解（速度模长）：A=v+c*tau，v'=A*e^(-dt/tau)-c*tau。
	// 低于停止阈值返回 {0,0}；dt 非法返回 {0,speed}；dt 上限为 kCanvasPanMaximumStepSeconds。
	CanvasPanFrictionStep StepCanvasPanFriction(double speed, double deltaSeconds,
		CanvasPanFriction friction) noexcept;
	// 从 speed 滑行到静止的总距离闭式解 v*tau - c*tau^2*ln(1+v/(c*tau))（不含停止阈值截断）。
	double CanvasPanGlideDistance(double speed, CanvasPanFriction friction) noexcept;
	// 释放门限：年龄 <=30 ms 为 1，>=70 ms 为 0，其间线性；负值或非有限值为 0。
	double CanvasPanReleaseGate(double ageSeconds) noexcept;

	// 单个触点的速度估计器：样本为触点绝对位置(DIP)与到达 QPC。
	// 最近 100 ms、至多 20 个样本；二次最小二乘，取最新样本时刻的一阶导。
	class CanvasPanVelocityTracker
	{
	public:
		// 非有限输入、非递增 QPC 返回 false 且不改变状态；与同簇起点相隔 <=2 ms 的样本合并，
		// 间隔超过 max(40 ms, frameSeconds+16 ms) 视为手曾静止并清窗。
		bool AddSample(int64_t qpc, double x, double y, int64_t qpcFrequency,
			double frameSeconds = 0.0) noexcept;
		// 样本不足 3 个或跨度不足 20 ms 时返回 valid=false（不是 0，也不做两点差分）；
		// 结果按 kCanvasPanVelocityLimitDipPerSecond 等比限幅。
		CanvasPanVelocityEstimate Estimate(int64_t qpcFrequency) const noexcept;
		// 视口触限后清除对应轴的速度历史（把该轴历史压平到最新位置）。
		void ClearAxis(bool x, bool y) noexcept;
		void Reset() noexcept;
		size_t SampleCount() const noexcept;
		int64_t LastSampleQpc() const noexcept;

	private:
		std::array<CanvasPanVelocitySample, kCanvasPanTrackerCapacity> samples_ = {};
		size_t count_ = 0;
		int64_t clusterStartQpc_ = 0;
	};

	enum class CanvasPanPhase : uint8_t
	{
		Idle,
		Coasting, // 无手，自由摩擦
		Slipping, // 有手但尚未抓住，画布仍按自身速度滑动
		Locked    // 有手且 1:1 跟手
	};

	enum class CanvasPanContactPhase : uint8_t
	{
		Down,
		Move,
		Up,
		Cancelled
	};

	// 一次触点输入。key 在触点生命周期内必须唯一稳定；同一帧可按时间顺序给出同一触点的多个样本。
	// 未知 key 的 Down/Move 视为新触点，未知或已抬起触点的 Up/Cancelled 被忽略。
	struct CanvasPanContactSample
	{
		uint64_t key = 0;
		int64_t qpc = 0;
		double x = 0.0;
		double y = 0.0;
		CanvasPanContactPhase phase = CanvasPanContactPhase::Move;
	};

	struct CanvasPanFrameInput
	{
		int64_t frameQpc = 0;
		int64_t qpcFrequency = 0;
		std::span<const CanvasPanContactSample> contacts = {};
		bool penHover = false;   // 惯性中以手摩擦(v手=0)加速制动
		bool penContact = false; // 落笔：立即清空手势与惯性
	};

	struct CanvasPanFrameOutput
	{
		CanvasVector contentDelta = {};    // 本帧内容位移，DIP
		CanvasVector contentVelocity = {}; // 当前内容速度，DIP/s（供 tile 预测）
		CanvasPanPhase phase = CanvasPanPhase::Idle;
		size_t heldContacts = 0;
		// 诊断字段：手速（含停手门限）、最后一只手离开时的释放速度与年龄。
		CanvasVector handVelocity = {};
		bool handVelocityKnown = false;
		bool released = false;
		double releaseSpeed = 0.0;
		double releaseAgeSeconds = 0.0;
	};

	// Idle/Coasting/Slipping/Locked 状态机。每个绘制帧调用一次 Step；
	// 触点数量变化只重基线内容位移，不清任何估速器。
	// 非法/倒退的帧时钟被忽略；Cancelled、倒退的 Up 时间与轻点（无 Move）都不会产生惯性。
	class CanvasPanDynamics
	{
	public:
		CanvasPanFrameOutput Step(const CanvasPanFrameInput& input) noexcept;
		// 离开移动模式/翻页/切工作区/加载/最小化/Resize/落笔：立即清零。
		// 调用方此后不应继续提交仍想忽略的旧触点，否则它们会被当作新触点。
		void Stop() noexcept;
		// 视口触及硬限位的轴：清零画布速度与该轴的估速历史。
		void ClearAxis(bool x, bool y) noexcept;
		CanvasPanPhase Phase() const noexcept;
		bool Active() const noexcept;
		CanvasVector ContentVelocity() const noexcept;
		size_t HeldContactCount() const noexcept;
		// 只供测试/诊断读取，指针仅在下一次 Step/Stop 前有效。
		const CanvasPanVelocityTracker* FindTracker(uint64_t key) const noexcept;

	private:
		struct Contact
		{
			bool used = false;
			bool held = false;
			bool joined = false; // 内容位移基线有效
			bool liftedStep = false;
			bool cancelledStep = false;
			uint64_t key = 0;
			CanvasPanVelocityTracker tracker;
			double x = 0.0;
			double y = 0.0;
			double refX = 0.0;
			double refY = 0.0;
			double anchorX = 0.0;
			double anchorY = 0.0;
			int64_t anchorQpc = 0;
			int64_t lastSampleQpc = 0;
			int64_t upQpc = 0;
			CanvasPanVelocityEstimate lifted = {}; // 抬起时冻结的（已过停手门限的）速度
		};

		CanvasPanFrameOutput FinishStep(const CanvasPanFrameInput& input, double dt) noexcept;
		Contact* FindHeld(uint64_t key) noexcept;
		bool HasLiftedRecord(uint64_t key) const noexcept;
		Contact* Allocate() noexcept;
		void Ingest(const CanvasPanContactSample& sample, int64_t qpcFrequency,
			double frameSeconds, bool firstHand) noexcept;
		CanvasPanVelocityEstimate HandVelocity(int64_t referenceQpc,
			int64_t qpcFrequency) const noexcept;
		bool HandStationary(int64_t nowQpc, int64_t qpcFrequency) const noexcept;
		void EndStep(int64_t nowQpc, size_t heldContacts, int64_t qpcFrequency) noexcept;

		std::array<Contact, kCanvasPanMaximumContacts> contacts_ = {};
		CanvasPanPhase phase_ = CanvasPanPhase::Idle;
		double velocityX_ = 0.0;
		double velocityY_ = 0.0;
		int64_t lastFrameQpc_ = 0;
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
