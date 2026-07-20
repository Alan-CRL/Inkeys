module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <windows.h>
#include <DirectXMath.h>
#include <ink_stroke_modeler/stroke_modeler.h>

export module draw3.ink_prediction;

import draw3.contact_input;
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

	// 选择当前工具的宽度来源；未来笔速橡皮应增加独立策略，不复用模拟压感。
	enum class StrokeWidthMode { SimulatedPressure, HardwarePressure, Fixed };

	inline constexpr InkPredictionMode kActivePredictionMode = InkPredictionMode::Kalman;
	inline constexpr LiveTipLengthMode kActiveLiveTipLengthMode = LiveTipLengthMode::Normal;
	inline constexpr DebugLayerColorMode kActiveDebugLayerColorMode = DebugLayerColorMode::NormalInkColor;
	inline constexpr StrokeTimingProfileId kActiveStrokeTimingProfileId = StrokeTimingProfileId::Fps120;
	inline constexpr float kHighlighterMinimumStrokeLengthPx = 12.0f;

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
		double liveTipDurationSeconds;
		ink::stroke_model::KalmanPredictorParams kalmanPredictorParams;
		ink::stroke_model::StrokeModelParams modelParams;
		bool retainPredictionOnUp = false; // 默认由模型生成 Up 收尾；外部开关可选择保留最后可见 prediction。
		InputWidthModeSettings inputWidthModes = {};
	};

	// 根据 DPI 和当前帧率预设创建模型配置。
	StrokeModelConfiguration CreateStrokeModelConfiguration(int dpiX);
	// 将当前预测模式写入模型参数。
	void ApplyPredictionMode(ink::stroke_model::StrokeModelParams& params,
		const ink::stroke_model::KalmanPredictorParams& kalmanPredictorParams);
	// 为普通笔 contact 按设备设置解析本笔固定的宽度来源。
	StrokeWidthMode ResolveStrokeWidthMode(InputDeviceType deviceType,
		InputWidthModeSettings settings, float downPressure) noexcept;
	// 将归一化硬件压力线性映射为基准直径的 0.2–1.4 倍。
	float HardwarePressureDiameter(float baseDiameter, float pressure) noexcept;

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
	};

	// 标记当前几何切片是否包含整笔的真实起点或可见终点。
	enum class HighlighterBoundaryFlags : uint32_t
	{
		None = 0,
		Start = 1,
		End = 2
	};

	inline HighlighterBoundaryFlags operator|(HighlighterBoundaryFlags left, HighlighterBoundaryFlags right)
	{
		return static_cast<HighlighterBoundaryFlags>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
	}

	struct HighlighterStartDirectionState
	{
		DirectX::XMFLOAT2 direction = { 1.0f, 0.0f };
		bool locked = false;
	};

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
		size_t convertedResultCount = 0;
		size_t committedIndex = 0;
		RECT lastL0Rect = { 0, 0, 0, 0 };
		RECT currentL0Rect = { 0, 0, 0, 0 };
		StrokeWidthEstimator widthEstimator;
		StrokeWidthMode widthMode = StrokeWidthMode::SimulatedPressure;
		bool highlighter = false;
		bool hasCommittedGeometry = false;
		float realPathLength = 0.0f;
		InkPoint inputStartPoint = {};
		bool hasInputStartPoint = false;
		DirectX::XMFLOAT2 firstMovementDirection = { 1.0f, 0.0f };
		bool hasFirstMovementDirection = false;
		HighlighterStartDirectionState startDirectionState;
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

	// 将中心线转换为平头 body、内部圆角及可选的抬笔最短矩形。
	HighlighterGeometry BuildHighlighterGeometry(const std::vector<InkPoint>& points,
		HighlighterBoundaryFlags boundaryFlags, bool shortStrokeMode,
		const HighlighterStartDirectionState& startDirectionState);
	// 为不足 12px 的最终矩形选择确定性起笔方向。
	HighlighterStartDirectionState GetHighlighterShortStrokeDirectionState(const ActiveStroke& stroke);
	// 选择普通笔完成态尾段：默认连接模型 Up 结果，开关启用时保留最后可见 L0。
	void BuildCompletedPenTail(const ActiveStroke& stroke, bool retainPredictionOnUp,
		std::vector<InkPoint>& output);

	// 将矩形并入已有脏区。
	void UnionRectInPlace(RECT& target, const RECT& addition);
	// 判断矩形是否为空。
	bool IsEmptyRect(const RECT& rect);
	// 将矩形裁剪到当前画布。
	RECT ClampRectToCanvas(RECT rect, int width, int height);
	// 返回完整画布矩形。
	RECT GetFullCanvasRect(int width, int height);
	// 计算一段墨迹点覆盖的脏矩形。
	RECT RectFromStrokePoints(const std::vector<InkPoint>& points, int width, int height,
		StrokeShape shape = StrokeShape::RoundCapsule, size_t firstIndex = 0,
		size_t lastIndex = (std::numeric_limits<size_t>::max)());
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
