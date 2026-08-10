module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include <windows.h>
#include <DirectXMath.h>
#include <ink_stroke_modeler/stroke_modeler.h>

export module draw3.ink_prediction;

import draw3.contact_input;
import draw3.ink_document;
import draw3.renderer;

export namespace draw3
{
	// 选择模型预测方式。
	enum class InkPredictionMode { Disabled, StrokeEnd, Kalman };
	// 选择实时笔锋尾部长度。
	enum class LiveTipLengthMode { Short, Normal, Long };
	// 选择 L0 调试着色方式。
	enum class DebugLayerColorMode { NormalInkColor, ColorizeLiveLayer };
	// 选择帧率相关的建模参数组。
	enum class StrokeTimingProfileId { Fps30, Fps60, Fps120, Fps240 };
	// Mouse 与 Touch 可选择固定宽度或速度模拟压感。
	enum class InputWidthMode : uint32_t { Fixed, SimulatedPressure };
	// Pen 额外支持 RTS 硬件压力。
	enum class PenInputWidthMode : uint32_t { Fixed, SimulatedPressure, HardwarePressure };

	struct InputWidthModeSettings
	{
		InputWidthMode mouse = InputWidthMode::SimulatedPressure;
		InputWidthMode touch = InputWidthMode::SimulatedPressure;
		PenInputWidthMode pen = PenInputWidthMode::HardwarePressure;

		friend bool operator==(const InputWidthModeSettings&, const InputWidthModeSettings&) = default;
	};

	// 以单个 32 位原子快照发布三类设备设置，供外部设置线程安全更新。
	class InputWidthModeSettingsState
	{
	public:
		explicit InputWidthModeSettingsState(InputWidthModeSettings settings = {}) noexcept;
		bool Set(InputWidthModeSettings settings) noexcept;
		InputWidthModeSettings Get() const noexcept;

	private:
		std::atomic<uint32_t> encoded_ = 0;
	};

	// 选择当前工具的宽度来源；LaserPressure 使用独立比例，不改变普通 Pen 的压感契约。
	enum class StrokeWidthMode { SimulatedPressure, HardwarePressure, LaserPressure, Fixed };

	inline constexpr InkPredictionMode kActivePredictionMode = InkPredictionMode::Kalman;
	inline constexpr LiveTipLengthMode kActiveLiveTipLengthMode = LiveTipLengthMode::Normal;
	inline constexpr DebugLayerColorMode kActiveDebugLayerColorMode = DebugLayerColorMode::NormalInkColor;
	inline constexpr StrokeTimingProfileId kActiveStrokeTimingProfileId = StrokeTimingProfileId::Fps120;
	// 人工验证临时开关：开启时禁用笔尾橡皮，并用测试色标出断触续接桥接段。
	inline constexpr bool kInterruptedStrokeReconnectManualTestModeEnabled = false;
	// 断触注入临时开关：开启时 RTS 会随机合成 Up、丢弃一段 Move，再从真实采样恢复 Down。
	inline constexpr bool kInterruptedStrokeReconnectSimulationEnabled = false;
	inline constexpr uint32_t kInterruptedStrokeReconnectSimulationMinimumIntervalMs = 180;
	inline constexpr uint32_t kInterruptedStrokeReconnectSimulationMaximumIntervalMs = 520;
	inline constexpr uint32_t kInterruptedStrokeReconnectSimulationMinimumDropMs = 20;
	inline constexpr uint32_t kInterruptedStrokeReconnectSimulationMaximumDropMs = 70;
	inline constexpr float kHighlighterNibAspectRatio = 8.0f;
	inline constexpr double kInterruptedStrokeReconnectWindowSeconds = 0.080;
	inline constexpr float kInterruptedStrokeReconnectDirectionLookbackPx = 12.0f;
	inline constexpr float kInterruptedStrokeReconnectMinimumDirectionPx = 4.0f;
	inline constexpr float kInterruptedStrokeReconnectMaximumAngleDegrees = 35.0f;
	inline constexpr float kInterruptedStrokeReconnectFallbackMaximumDistancePx = 32.0f;
	inline constexpr float kInterruptedStrokeReconnectPredictedMaximumDistancePx = 64.0f;
	inline constexpr float kInterruptedStrokeReconnectAdaptiveMaximumDistancePx = 256.0f;
	inline constexpr float kInterruptedStrokeReconnectAdaptiveDistanceSpeedRatio = 1.75f;
	inline constexpr float kInterruptedStrokeReconnectDistanceSlackPx = 4.0f;
	inline constexpr float kInterruptedStrokeReconnectEndpointRelativeTolerance = 0.35f;
	inline constexpr float kInterruptedStrokeReconnectAdaptiveRelativeTolerance = 0.50f;
	inline constexpr float kInterruptedStrokeReconnectBeyondHorizonUncertaintyRatio = 0.75f;
	inline constexpr float kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees = 35.0f;
	inline constexpr double kInterruptedStrokeReconnectInHorizonTerminalMaximumGapSeconds = 0.035;
	inline constexpr float kInterruptedStrokeReconnectInHorizonTerminalMaximumAngleDegrees = 15.0f;
	inline constexpr float kInterruptedStrokeReconnectInHorizonTerminalMinimumSpeedRatio = 0.50f;
	inline constexpr float kInterruptedStrokeReconnectInHorizonTerminalMaximumSpeedRatio = 2.0f;
	inline constexpr float kInterruptedStrokeReconnectComparisonEpsilonPx = 0.5f;
	inline constexpr float kInterruptedStrokeReconnectMinimumSpeedRatio = 0.35f;
	inline constexpr float kInterruptedStrokeReconnectMaximumSpeedRatio = 2.75f;
	inline constexpr size_t kMaximumInterruptedStrokeReconnectCandidates = 8;
	inline constexpr double kLaserFadeDurationSeconds = 0.8;

	enum class LaserTrailPhase : uint32_t
	{
		Inactive,
		Active,
		Hold,
		Fade
	};

	// 保存整组激光轨迹的 contact 计数和最后一次全部抬笔时刻。
	struct LaserTrailLifecycle
	{
		LaserTrailPhase phase = LaserTrailPhase::Inactive;
		uint32_t activeContactCount = 0;
		int64_t lastAllUpQpc = 0;
		double minimumHoldDurationSeconds = 0.0;
	};

	// 单 contact coverage 的批次模式；多 contact 一旦出现便锁定完整重绘。
	enum class LaserCoverageMode : uint32_t
	{
		Inactive,
		Incremental,
		FullRedraw
	};

	struct LaserIncrementalStrokeState
	{
		size_t stableCommittedIndex = 0;
		bool rebuildRequired = true;
	};

	struct LaserIncrementalRanges
	{
		size_t stableFirstIndex = 0;
		size_t stablePointCount = 0;
		size_t liveFirstIndex = 0;
		size_t livePointCount = 0;
		size_t nextStableCommittedIndex = 0;
	};

	// 按现有 live-tip/prediction 时间保护边界计算稳定 delta 与实时尾部范围。
	LaserIncrementalRanges PlanLaserIncrementalRanges(
		std::span<const InkPoint> realPoints,
		const LaserIncrementalStrokeState& state,
		double protectedDurationSeconds) noexcept;

	struct LaserLayerDirtyPlan
	{
		LaserIncrementalRanges ranges = {};
		RECT stableDeltaBounds = {};
		RECT stableBounds = {};
		RECT previousLiveBounds = {};
		RECT liveBounds = {};
		RECT layerBounds = {};
		RECT dirtyBounds = {};
	};

	// 多 contact fallback 只脏化新增稳定区域和旧/新 live，完整几何仍按原顺序绘制。
	LaserLayerDirtyPlan PlanLaserLayerDirty(
		std::span<const InkPoint> realPoints,
		std::span<const InkPoint> visiblePoints,
		const LaserIncrementalStrokeState& state,
		RECT stableBounds, RECT previousLiveBounds,
		double protectedDurationSeconds, float dpiScale,
		int width, int height) noexcept;
	// 资源可用时首个 contact 进入快路，第二个 contact 将当前批次锁定回退。
	LaserCoverageMode SelectLaserCoverageMode(
		LaserCoverageMode current, size_t layerCount,
		bool resourcesAvailable) noexcept;

	// 新 contact 在完全消失前会恢复整组满亮并重新进入 Active。
	void BeginLaserContact(LaserTrailLifecycle& lifecycle) noexcept;
	// 只有最后一根 Laser 抬起时才开始 Hold 计时。
	void EndLaserContact(LaserTrailLifecycle& lifecycle, int64_t upQpc) noexcept;
	// 当前批次实际发射粒子后，主体 Hold 不得早于粒子寿命结束。
	void RequireLaserMinimumHold(
		LaserTrailLifecycle& lifecycle, double minimumSeconds) noexcept;
	double EffectiveLaserHoldDurationSeconds(
		const LaserTrailLifecycle& lifecycle, double configuredSeconds) noexcept;
	// 根据当前设置即时重算 Hold/Fade，并返回平滑后的整组 opacity。
	float EvaluateLaserTrailOpacity(LaserTrailLifecycle& lifecycle,
		int64_t nowQpc, int64_t qpcFrequency, double holdDurationSeconds) noexcept;
	// 最后一个 contact 结束且仍有未烘干层时，才允许进入批次烘干。
	bool ShouldBakeLaserBatch(
		const LaserTrailLifecycle& lifecycle, size_t pendingLayerCount) noexcept;
	// Cancelled 或空几何层不能参与活动合成和稳定烘干。
	bool ShouldCompositeLaserLayer(bool cancelled, size_t pointCount) noexcept;
	// 保存与目标帧率联动的建模和预测参数。
	struct StrokeTimingProfile
	{
		double target_fps;
		double min_output_rate;
		double live_tail_duration_seconds;
		double prediction_interval_seconds;
		int kalman_desired_number_of_samples;
		int kalman_max_time_samples;
		double wobble_timeout_seconds;
		float wobble_speed_floor_ratio;
		float wobble_speed_ceiling_ratio;
		int max_outputs_per_call;
	};

	// 汇总启动时生成的墨迹模型配置。
	struct StrokeModelConfiguration
	{
		StrokeTimingProfile timingProfile;
		float expectedSpeed;
		float dpiScale = 1.0f;
		double liveTipDurationSeconds;
		ink::stroke_model::KalmanPredictorParams kalmanPredictorParams;
		ink::stroke_model::StrokeModelParams modelParams;
		bool invertedPenEraserEnabled = true; // 默认允许倒转 Pen 在画笔/荧光笔下临时覆盖为橡皮。
		bool interruptedStrokeReconnectEnabled = true; // 默认允许 Touch 短暂断触继续同一模型；Pen/Mouse 始终正常收尾。
		bool hapticFeedbackEnabled = true; // 当前原型默认启用触觉；后续由 Inkeys3 设置替换。
		bool performanceHudEnabled = false; // 默认关闭开发诊断 HUD；开启时只统计 Draw3 绘制循环。
		bool drawingCursorDuringContactEnabled = false; // 默认落笔时隐藏应用光标；外部可显式开启。
		bool laserParticlesEnabled = false; // GPU 粒子默认关闭；外部可按需开启，资源不可用时只降级激光主体。
		bool laserMultiTouchDrawingEnabled = false; // 默认只接受第一根 Touch，外部可显式启用激光笔多指绘图。
		LaserParticleConfig laserParticleConfig = {};
		double laserHoldDurationSeconds = 1.0; // 最后一根激光笔抬起后的满亮留存时间。
		InputWidthModeSettings inputWidthModes = {};
	};

	enum class InterruptedStrokeReconnectMotionSource
	{
		None,
		PredictionPosition,
		RealTail
	};

	enum class InterruptedStrokeReconnectRejectReason
	{
		None,
		IdentityMismatch,
		InvalidTime,
		WindowExpired,
		InvalidSpeed,
		InvalidDirection,
		Distance,
		ForecastError,
		Angle
	};

	// 保存按新 Down 时刻解析出的预测位移、平均速度和终点速度诊断。
	struct InterruptedStrokeReconnectMotion
	{
		bool valid = false;
		InterruptedStrokeReconnectMotionSource source =
			InterruptedStrokeReconnectMotionSource::None;
		DirectX::XMFLOAT2 direction = {};
		DirectX::XMFLOAT2 predictedDisplacement = {};
		DirectX::XMFLOAT2 terminalDirection = {};
		bool directionReliable = false;
		bool terminalDirectionValid = false;
		float speed = -1.0f;
		float recentInputSpeed = -1.0f;
		float terminalSpeed = -1.0f;
		float predictedDistance = 0.0f;
		double forecastDurationMilliseconds = 0.0;
		double predictionHorizonMilliseconds = 0.0;
		double beyondPredictionHorizonMilliseconds = 0.0;
	};

	// 描述一次物理 Up 到新 Down 的运动学续接候选。
	struct InterruptedStrokeReconnectInput
	{
		PointF previousPosition = {};
		int64_t previousUpQpc = 0;
		PointF newPosition = {};
		int64_t newDownQpc = 0;
		int64_t qpcFrequency = 0;
		float dpiScale = 1.0f;
		InterruptedStrokeReconnectMotion motion;
	};

	// 返回续接判定和成功日志所需的量化指标。
	struct InterruptedStrokeReconnectResult
	{
		bool matched = false;
		InterruptedStrokeReconnectRejectReason rejectReason =
			InterruptedStrokeReconnectRejectReason::None;
		InterruptedStrokeReconnectMotionSource motionSource =
			InterruptedStrokeReconnectMotionSource::None;
		double gapMilliseconds = 0.0;
		double forecastDurationMilliseconds = 0.0;
		double predictionHorizonMilliseconds = 0.0;
		double beyondPredictionHorizonMilliseconds = 0.0;
		float distance = 0.0f;
		float minimumDistance = 0.0f;
		float maximumDistance = 0.0f;
		float expectedDistance = 0.0f;
		float endpointError = 0.0f;
		float maximumEndpointError = 0.0f;
		float referenceSpeed = 0.0f;
		float recentInputSpeed = -1.0f;
		float longitudinalDistance = 0.0f;
		float longitudinalError = 0.0f;
		float maximumLongitudinalError = 0.0f;
		float lateralError = 0.0f;
		float maximumLateralError = 0.0f;
		float terminalSpeed = -1.0f;
		float angleDegrees = 180.0f;
		float terminalVelocityAngleDegrees = 180.0f;
		float selectedDirectionAngleDegrees = 180.0f;
		float bridgeSpeed = 0.0f;
		float speedRatio = 0.0f;
		float matchScore = (std::numeric_limits<float>::infinity)();
		bool directionReliable = false;
		bool predictionExtrapolated = false;
		bool selectedTerminalDirectionCorridor = false;
		bool selectedInHorizonTerminalDirectionCorridor = false;
	};

	// 汇总必须保持一致的设备、工具和笔宽策略；工具值由 DrawingTool 转为固定整数。
	struct InterruptedStrokeReconnectIdentity
	{
		InputDeviceType deviceType = InputDeviceType::Touch;
		uint32_t selectedTool = 0;
		uint32_t tool = 0;
		StrokeWidthMode widthMode = StrokeWidthMode::SimulatedPressure;
		bool invertedCursor = false;
		bool suppressPressure = false;
	};

	// 根据 DPI 和当前帧率预设创建模型配置。
	StrokeModelConfiguration CreateStrokeModelConfiguration(int dpiX);
	// 将当前预测模式写入模型参数。
	void ApplyPredictionMode(ink::stroke_model::StrokeModelParams& params,
		const ink::stroke_model::KalmanPredictorParams& kalmanPredictorParams);
	// 为普通笔 contact 按设备设置解析本笔固定的宽度来源。
	// HardwarePressure 普通笔禁用 L0 实时笔锋；其余宽度模式沿用配置时长。
	double ResolveLiveTipTaperDurationSeconds(StrokeWidthMode widthMode,
		double configuredLiveTipDurationSeconds) noexcept;
	StrokeWidthMode ResolveStrokeWidthMode(InputDeviceType deviceType,
		InputWidthModeSettings settings, float downPressure) noexcept;
	// 判断倒转 Pen 是否请求把当前可绘制工具覆盖为橡皮。
	bool ShouldUseInvertedPenEraser(InputDeviceType deviceType,
		bool isInvertedCursor, bool enabled, bool selectedToolSupportsOverride) noexcept;
	// 倒转 Pen 的压力不稳定，进入模型前统一标记为未知。
	float ResolveStylusPressureForModel(InputDeviceType deviceType,
		bool isInvertedCursor, float pressure) noexcept;
	// 将归一化硬件压力线性映射为基准直径的 0.2–1.4 倍。
	float HardwarePressureDiameter(float baseDiameter, float pressure) noexcept;
	// Laser 使用较温和的 0.65–1.4 倍整组材质缩放；无效压力保持基准直径。
	float LaserPressureDiameter(float baseDiameter, float pressure) noexcept;
	// 从末端真实点的有限回看窗口计算稳定延伸方向。
	bool TryGetInterruptedStrokeTailDirection(const std::vector<InkPoint>& realPoints,
		float dpiScale, DirectX::XMFLOAT2& direction) noexcept;
	// 按 Down 间隔选择预测位置并生成模型位移；prediction 无效时回退真实尾方向与滤波输入速度。
	InterruptedStrokeReconnectMotion ResolveInterruptedStrokeReconnectMotion(
		const std::vector<ink::stroke_model::Result>& predictedResults,
		const std::vector<InkPoint>& realPoints,
		DirectX::XMFLOAT2 fallbackDirection, float fallbackSpeed,
		double upInputTime, double gapSeconds, float dpiScale) noexcept;
	// 以时间、预测落点走廊或保守回退策略判断新 Down 是否属于同一笔。
	InterruptedStrokeReconnectResult EvaluateInterruptedStrokeReconnect(
		const InterruptedStrokeReconnectInput& input) noexcept;
	// 续接前要求设备、工具和本笔固定策略完全一致。
	bool AreInterruptedStrokeReconnectIdentitiesCompatible(
		const InterruptedStrokeReconnectIdentity& previous,
		const InterruptedStrokeReconnectIdentity& current) noexcept;
	// 断触修正只面向容易发生物理断触的 Touch 输入。
	bool IsInterruptedStrokeReconnectDeviceSupported(InputDeviceType deviceType) noexcept;
	// 多候选命中时按归一化落点误差、角度、距离和较新的 Up 确定唯一候选。
	bool IsBetterInterruptedStrokeReconnectMatch(
		const InterruptedStrokeReconnectResult& candidate, int64_t candidateUpQpc,
		const InterruptedStrokeReconnectResult& current, int64_t currentUpQpc) noexcept;
	// 返回超过固定候选上限后必须立即完成的数量。
	size_t GetInterruptedStrokeReconnectEvictionCount(size_t candidateCount) noexcept;
	// deadline 到点即应完成候选，避免仅候选等待错过清理。
	bool IsInterruptedStrokeReconnectExpired(int64_t deadlineQpc, int64_t nowQpc) noexcept;

	// 根据原始输入速度平滑估算墨迹半径。
	struct StrokeWidthEstimator
	{
		float baseDiameter = 5.0f;
		float minDiameter = 4.0f;
		float maxDiameter = 7.0f;
		float expectedSpeed = 500.0f;
		float currentDiameter = 5.0f;
		double lastTime = 0.0;
		// 记录上一个建模点，用于限制相邻点的空间半径变化。
		float lastPositionX = 0.0f;
		float lastPositionY = 0.0f;
		bool hasSample = false;
		bool hasInputSpeed = false;

		StrokeWidthEstimator() = default;
		StrokeWidthEstimator(float baseDiameterValue, float expectedSpeedValue);
		// 追加建模结果；inputSpeed 为负时只推进几何状态并保持当前笔宽。
		InkPoint Append(const ink::stroke_model::Result& result, float inputSpeed = -1.0f);
		// 使用模型插值后的硬件压力追加建模结果；无效压力保持上一真实宽度。
		InkPoint AppendHardwarePressure(const ink::stroke_model::Result& result);
		// 使用 Laser 独立压力比例，并复用相邻点半径变化限速。
		InkPoint AppendLaserPressure(const ink::stroke_model::Result& result);
	};

	// 标记当前几何切片是否包含整笔的真实起点或可见终点。
	// 保存一个 contact 的模型结果、预测结果和三层提交状态。
	struct ActiveStroke
	{
		ink::stroke_model::StrokeModeler modeler;
		std::vector<ink::stroke_model::Result> modeledResults;
		std::vector<ink::stroke_model::Result> predictedResults;
		std::vector<InkPoint> realPoints;
		std::vector<InkPoint> predictedPoints;
		std::vector<InkPoint> l0DrawPoints;
		std::vector<InkPoint> previousL0DrawPoints;
		HighlighterGeometry l0HighlighterGeometry;
		// 荧光笔稳定前缀的 CPU 重放缓存，Up 和 resize 都直接重放这里。
		HighlighterGeometry committedHighlighterGeometry;
		size_t convertedResultCount = 0;
		size_t committedIndex = 0;
		RECT lastL0Rect = { 0, 0, 0, 0 };
		RECT currentL0Rect = { 0, 0, 0, 0 };
		StrokeWidthEstimator widthEstimator;
		StrokeWidthMode widthMode = StrokeWidthMode::SimulatedPressure;
		bool highlighter = false;
		bool hasCommittedGeometry = false;
		InkPoint inputStartPoint = {};
		bool hasInputStartPoint = false;
		POINT lastRawPosition = { 0, 0 };
		bool hasLastRawPosition = false;
		bool idleFrozen = false;
		int visualStableFrameCount = 0;
		double lastMovementInputTime = 0.0;
		double lastFrameWallTime = 0.0;
		double logicalInputTime = 0.0;

		ActiveStroke(float baseDiameter, float expectedSpeed,
			StrokeWidthMode widthModeValue = StrokeWidthMode::SimulatedPressure,
			bool highlighterValue = false);
		// 清空长度并保留已分配容量，供绘制线程复用模型对象。
		void Reset(float baseDiameter, float expectedSpeed,
			StrokeWidthMode widthModeValue = StrokeWidthMode::SimulatedPressure,
			bool highlighterValue = false);
	};

	// 将中心线转换为固定竖直矩形沿路径扫掠的几何；单点生成一个点击矩形。
	HighlighterGeometry BuildHighlighterGeometry(const std::vector<InkPoint>& points);
	// 原地重建荧光笔几何并复用 primitive 容量，供每帧 L0 热路径使用。
	void RebuildHighlighterGeometry(
		std::span<const InkPoint> points, HighlighterGeometry& output);
	// 用已确认真实点生成普通笔完成态尾段，并烘入最终笔锋宽度。
	void BuildCompletedPenTail(const ActiveStroke& stroke,
		double liveTipTaperSeconds, std::vector<InkPoint>& output);
	// 把完成态真实点转换为不含 time/prediction 的持久 Stroke；scratch 由绘制线程复用。
	std::optional<InkStroke> FinalizeStoredStroke(const ActiveStroke& stroke,
		StoredInkStyle style, double liveTipTaperSeconds,
		std::vector<InkPoint>& scratch);

	// 描述 Stored Stroke 栅格化目标；origin 是目标左上角对应的 Canvas 坐标。
	struct StoredStrokeRasterTarget
	{
		const OperatorLayerResources* operatorLayer = nullptr;
		float originX = 0.0f;
		float originY = 0.0f;
		int width = 0;
		int height = 0;
	};

	struct StoredStrokeRasterResult
	{
		bool succeeded = false;
		RECT dirty = {};
	};

	// 从持久对象重建最终 operator；全屏首次提交与 tile cache 共用此入口。
	StoredStrokeRasterResult DrawStoredStroke(const InkStroke& stroke, InkRenderer& renderer,
		const StoredStrokeRasterTarget& target, std::vector<InkPoint>& pointScratch,
		HighlighterGeometry& highlighterScratch);

	// 将矩形并入已有脏区。
	void UnionRectInPlace(RECT& target, const RECT& addition);
	// 判断矩形是否为空。
	bool IsEmptyRect(const RECT& rect);
	// 将矩形裁剪到当前画布。
	RECT ClampRectToCanvas(RECT rect, int width, int height);
	// 返回完整画布矩形。
	RECT GetFullCanvasRect(int width, int height);
	// 计算一段墨迹点覆盖的脏矩形。
	RECT RectFromStrokePoints(std::span<const InkPoint> points, int width, int height,
		StrokeShape shape = StrokeShape::RoundCapsule);
	// 激光脏区按每点实体半径覆盖固定 5px 漫反射和抗锯齿 padding。
	RECT RectFromLaserPoints(std::span<const InkPoint> points,
		float dpiScale, int width, int height);
	// 更新原始坐标并判断是否发生有效移动。
	bool UpdateRawPositionAndDetectMovement(ActiveStroke& stroke, const POINT& rawPosition);
	// 在视觉稳定后冻结停笔输入。
	void UpdateIdleFreezeState(ActiveStroke& stroke, bool rawMoved, double liveTipDurationSeconds);
	// 转换尚未处理的真实建模结果。
	void AppendNewModeledPoints(ActiveStroke& stroke, float inputSpeed = -1.0f);
	// 使用笔宽估算器副本重建预测绘制点。
	void RebuildPredictedPoints(ActiveStroke& stroke);
	// 返回预测末端相对真实末端的时长。
	double GetPredictionDurationSeconds(const ActiveStroke& stroke);
	// 重建当前 L0 的真实尾部和预测点。
	void RebuildL0DrawPoints(ActiveStroke& stroke, double liveTipDurationSeconds,
		StrokeShape shape, int width, int height);
	// 将保护窗口之前的稳定前缀提交到 L1。
	RECT CommitStablePrefixToL1(ActiveStroke& stroke, double liveTipDurationSeconds,
		double predictionDurationSeconds, DirectX::XMFLOAT4 color, StrokeShape shape,
		InkRenderer& renderer, int width, int height);
	// 橡皮不保留 L0，直接把新增真实点（含单击圆点）提交到 L1。
	RECT CommitEraserRealPointsToL1(ActiveStroke& stroke, StrokeShape shape,
		InkRenderer& renderer, int width, int height);
	// 清空并重绘当前 L0 实时内容。
	void DrawL0LiveComposite(ActiveStroke& stroke, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkRenderer& renderer, bool clearLayer = true);
}

// 多个实现单元共享的内部策略，不导出到模块使用方。
namespace draw3::ink_prediction_detail
{
	StrokeTimingProfile GetStrokeTimingProfile(StrokeTimingProfileId id);
	double GetLiveTipDurationSeconds(const StrokeTimingProfile& profile);
	size_t FindProtectedStartIndex(
		std::span<const InkPoint> points, double protectedDurationSeconds);
}
