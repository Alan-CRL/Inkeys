module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <limits>
#include <vector>
#include <windows.h>
#include <DirectXMath.h>
#include <ink_stroke_modeler/stroke_modeler.h>

export module draw3.ink_prediction;

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

	inline constexpr InkPredictionMode kActivePredictionMode = InkPredictionMode::Kalman;
	inline constexpr LiveTipLengthMode kActiveLiveTipLengthMode = LiveTipLengthMode::Normal;
	inline constexpr DebugLayerColorMode kActiveDebugLayerColorMode = DebugLayerColorMode::NormalInkColor;
	inline constexpr StrokeTimingProfileId kActiveStrokeTimingProfileId = StrokeTimingProfileId::Fps120;

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
	};

	// 根据 DPI 和当前帧率预设创建模型配置。
	StrokeModelConfiguration CreateStrokeModelConfiguration(int dpiX);
	// 将当前预测模式写入模型参数。
	void ApplyPredictionMode(ink::stroke_model::StrokeModelParams& params,
		const ink::stroke_model::KalmanPredictorParams& kalmanPredictorParams);

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
	};

	// 按路径距离平滑平头笔走势，并限制短线段上的方向旋转。
	struct StrokeDirectionEstimator
	{
		float responseDistance = 50.0f;
		float directionX = 1.0f;
		float directionY = 0.0f;
		float lastPositionX = 0.0f;
		float lastPositionY = 0.0f;
		bool hasPosition = false;
		bool hasDirection = false;

		StrokeDirectionEstimator() = default;
		explicit StrokeDirectionEstimator(float responseDistanceValue);
		void Append(InkPoint& point);
	};

	// 保存一次鼠标笔画的模型结果、预测结果和三层提交状态。
	struct ActiveMouseStroke
	{
		ink::stroke_model::StrokeModeler modeler;
		std::vector<ink::stroke_model::Result> modeledResults;
		std::vector<ink::stroke_model::Result> predictedResults;
		std::vector<InkPoint> realPoints;
		std::vector<InkPoint> predictedPoints;
		std::vector<InkPoint> l0DrawPoints;
		std::vector<InkPoint> previousL0DrawPoints;
		size_t convertedResultCount = 0;
		size_t committedIndex = 0;
		RECT lastL0Rect = { 0, 0, 0, 0 };
		RECT currentL0Rect = { 0, 0, 0, 0 };
		StrokeWidthEstimator widthEstimator;
		StrokeDirectionEstimator directionEstimator;
		bool fixedWidth = false;
		bool trackDirection = false;
		POINT lastRawPosition = { 0, 0 };
		bool hasLastRawPosition = false;
		bool idleFrozen = false;
		int visualStableFrameCount = 0;
		double lastMovementInputTime = 0.0;
		double lastFrameWallTime = 0.0;
		double logicalInputTime = 0.0;

		ActiveMouseStroke(float baseDiameter, float expectedSpeed, bool fixedWidthValue = false,
			bool trackDirectionValue = false);
	};

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
	bool UpdateRawPositionAndDetectMovement(ActiveMouseStroke& stroke, const POINT& rawPosition);
	// 在视觉稳定后冻结停笔输入。
	void UpdateIdleFreezeState(ActiveMouseStroke& stroke, bool rawMoved, double liveTipDurationSeconds);
	// 转换尚未处理的真实建模结果。
	void AppendNewModeledPoints(ActiveMouseStroke& stroke, float inputSpeed = -1.0f);
	// 使用笔宽估算器副本重建预测绘制点。
	void RebuildPredictedPoints(ActiveMouseStroke& stroke);
	// 返回预测末端相对真实末端的时长。
	double GetPredictionDurationSeconds(const ActiveMouseStroke& stroke);
	// 重建当前 L0 的真实尾部和预测点。
	void RebuildL0DrawPoints(ActiveMouseStroke& stroke, double liveTipDurationSeconds,
		StrokeShape shape, int width, int height);
	// 将保护窗口之前的稳定前缀提交到 L1。
	RECT CommitStablePrefixToL1(ActiveMouseStroke& stroke, double liveTipDurationSeconds,
		double predictionDurationSeconds, DirectX::XMFLOAT4 color, StrokeShape shape,
		InkRenderer& renderer, int width, int height);
	// 清空并重绘当前 L0 实时内容。
	void DrawL0LiveComposite(ActiveMouseStroke& stroke, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkRenderer& renderer);
}
