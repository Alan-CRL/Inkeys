#include "main.h"

#include "renderer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

namespace
{
	std::atomic<bool> g_clearCanvasRequested = false;
	std::atomic<bool> g_resizeRequested = false;
	std::atomic<int> g_pendingResizeWidth = 0;
	std::atomic<int> g_pendingResizeHeight = 0;
	std::atomic<int> g_brushShapeType = 0; // 0: 原来的画笔

	enum class InkPredictionMode
	{
		Disabled,
		StrokeEnd,
		Kalman
	};

	enum class LiveTipLengthMode
	{
		Short,
		Normal,
		Long
	};

	enum class DebugLayerColorMode
	{
		NormalInkColor,
		ColorizeLiveLayer
	};

	constexpr InkPredictionMode kActivePredictionMode = InkPredictionMode::Kalman;
	constexpr LiveTipLengthMode kActiveLiveTipLengthMode = LiveTipLengthMode::Normal;
	constexpr DebugLayerColorMode kActiveDebugLayerColorMode = DebugLayerColorMode::NormalInkColor;

	enum class StrokeTimingProfileId
	{
		Fps30,  // 30 FPS: 老 Win7/低功耗档；补齐 180 点/秒，每帧约 6 点，防抖窗口约 83ms。
		Fps60,  // 60 FPS: 默认兼容档；补齐 240 点/秒，每帧约 4 点，防抖窗口约 42ms。
		Fps120, // 120 FPS: 当前高质量测试档；补齐 360 点/秒，每帧约 3 点，防抖窗口约 21ms。
		Fps240  // 240 FPS: 高刷/高端档；补齐 480 点/秒，每帧约 2 点，防抖窗口约 10ms。
	};

	struct StrokeTimingProfile
	{
		double target_fps; // 主循环目标 FPS，传给 HighPrecisionWait。
		double min_output_rate; // 模型最少输出点数/秒，低于输入频率时用于补齐点密度。
		double live_tail_duration_seconds; // 实时笔锋保留的尾部时间，当前先换算成尾部点数。
		double prediction_interval_seconds; // Kalman 预测最大前瞻时间；实际预测长度还会乘置信度。
		int kalman_desired_number_of_samples; // Kalman 样本数置信度达到 1.0 所需的输入点数。
		int kalman_max_time_samples; // Kalman 保存的最近时间戳数量，用于修正非均匀输入间隔。
		double wobble_timeout_seconds; // 防抖移动平均窗口，按约 2.5 个输入间隔设置。
		float wobble_speed_floor_ratio; // 低于 expected_speed 的该比例时防抖最强。
		float wobble_speed_ceiling_ratio; // 高于 expected_speed 的该比例时基本不防抖。
		int max_outputs_per_call; // 单次 Update/Predict 最多输出点数，防止长时间卡顿后爆量补点。
	};

	constexpr StrokeTimingProfileId kDefaultStrokeTimingProfileId = StrokeTimingProfileId::Fps60;
	constexpr StrokeTimingProfileId kActiveStrokeTimingProfileId = StrokeTimingProfileId::Fps120; // 当前测试先使用 120 FPS。

	StrokeTimingProfile GetStrokeTimingProfile(StrokeTimingProfileId profileId = kDefaultStrokeTimingProfileId)
	{
		switch (profileId)
		{
		case StrokeTimingProfileId::Fps30:
			return {
				.target_fps = 30.0,
				.min_output_rate = 180.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 30.0,
				.kalman_desired_number_of_samples = 4,
				.kalman_max_time_samples = 5,
				.wobble_timeout_seconds = 2.5 / 30.0,
				// 慢速插值过渡带放宽，避免某个低速区间在强/弱防抖之间来回跳。
				.wobble_speed_floor_ratio = 0.015f,
				.wobble_speed_ceiling_ratio = 0.08f,
				.max_outputs_per_call = 2000
			};
		case StrokeTimingProfileId::Fps60:
			return {
				.target_fps = 60.0,
				.min_output_rate = 240.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 45.0,
				.kalman_desired_number_of_samples = 5,
				.kalman_max_time_samples = 10,
				.wobble_timeout_seconds = 2.5 / 60.0,
				// 慢速插值过渡带放宽，避免某个低速区间在强/弱防抖之间来回跳。
				.wobble_speed_floor_ratio = 0.015f,
				.wobble_speed_ceiling_ratio = 0.08f,
				.max_outputs_per_call = 2000
			};
		case StrokeTimingProfileId::Fps120:
			return {
				.target_fps = 120.0,
				.min_output_rate = 360.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 60.0,
				.kalman_desired_number_of_samples = 10,
				.kalman_max_time_samples = 20,
				.wobble_timeout_seconds = 2.5 / 120.0,
				// 慢速插值过渡带放宽，避免某个低速区间在强/弱防抖之间来回跳。
				.wobble_speed_floor_ratio = 0.015f,
				.wobble_speed_ceiling_ratio = 0.08f,
				.max_outputs_per_call = 2000
			};
		case StrokeTimingProfileId::Fps240:
			return {
				.target_fps = 240.0,
				.min_output_rate = 480.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 120.0,
				.kalman_desired_number_of_samples = 20,
				.kalman_max_time_samples = 40,
				.wobble_timeout_seconds = 2.5 / 240.0,
				// 慢速插值过渡带放宽，避免某个低速区间在强/弱防抖之间来回跳。
				.wobble_speed_floor_ratio = 0.015f,
				.wobble_speed_ceiling_ratio = 0.08f,
				.max_outputs_per_call = 2000
			};
		default:
			return GetStrokeTimingProfile();
		}
	}

	float LerpFloat(float from, float to, float ratio)
	{
		return from + (to - from) * ratio;
	}

	float SmoothStep01(float value)
	{
		value = std::clamp(value, 0.0f, 1.0f);
		return value * value * (3.0f - 2.0f * value);
	}

	constexpr float kIdleMoveThresholdPx = 0.25f;
	constexpr float kVisualStablePositionEpsilonPx = 0.05f;
	constexpr float kVisualStableRadiusEpsilonPx = 0.02f;
	constexpr int kVisualStableRequiredFrames = 3;

	double GetQpcTimeMilliseconds()
	{
		static LARGE_INTEGER freq = { 0 };
		if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
	}

	void WriteFastConsoleLine(const char* text, DWORD length)
	{
		static HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (!consoleHandle || consoleHandle == INVALID_HANDLE_VALUE || length == 0) return;

		DWORD written = 0;
		if (!WriteConsoleA(consoleHandle, text, length, &written, nullptr))
		{
			WriteFile(consoleHandle, text, length, &written, nullptr);
		}
	}

	double GetLiveTipDurationSeconds(const StrokeTimingProfile& timingProfile)
	{
		switch (kActiveLiveTipLengthMode)
		{
		case LiveTipLengthMode::Short:
			return timingProfile.live_tail_duration_seconds * 0.65;
		case LiveTipLengthMode::Long:
			return timingProfile.live_tail_duration_seconds * 1.6;
		case LiveTipLengthMode::Normal:
		default:
			return timingProfile.live_tail_duration_seconds;
		}
	}

	void ApplyPredictionMode(StrokeModelParams& params, const KalmanPredictorParams& kalmanPredictorParams)
	{
		switch (kActivePredictionMode)
		{
		case InkPredictionMode::Disabled:
			params.prediction_params = DisabledPredictorParams{};
			break;
		case InkPredictionMode::StrokeEnd:
			params.prediction_params = StrokeEndPredictorParams{};
			break;
		case InkPredictionMode::Kalman:
		default:
			params.prediction_params = kalmanPredictorParams;
			break;
		}
	}

	struct StrokeWidthEstimator
	{
		float baseDiameter = 5.0f;
		float minDiameter = 4.0f;
		float maxDiameter = 7.0f;
		float expectedSpeed = 500.0f;
		float currentDiameter = 5.0f;
		double lastTime = 0.0;
		bool hasSample = false;

		StrokeWidthEstimator() = default;
		StrokeWidthEstimator(float baseDiameterValue, float expectedSpeedValue)
			: baseDiameter(baseDiameterValue),
			minDiameter(baseDiameterValue * 0.8f),
			maxDiameter(baseDiameterValue * 1.4f),
			expectedSpeed(max(1.0f, expectedSpeedValue)),
			currentDiameter(baseDiameterValue)
		{
		}

		InkPoint Append(const Result& result)
		{
			const double pointTime = result.time.Value();
			const float rawSpeed = std::hypot(result.velocity.x, result.velocity.y);
			const float speedRatio = SmoothStep01(rawSpeed / expectedSpeed);
			const float targetDiameter = LerpFloat(maxDiameter, minDiameter, speedRatio);

			if (!hasSample)
			{
				currentDiameter = targetDiameter;
				hasSample = true;
			}
			else
			{
				const double dt = max(0.0, pointTime - lastTime);
				const float alpha = std::clamp(static_cast<float>(1.0 - std::exp(-dt / 0.035)), 0.05f, 0.65f);
				currentDiameter = LerpFloat(currentDiameter, targetDiameter, alpha);
			}

			lastTime = pointTime;
			return InkPoint{
				result.position.x,
				result.position.y,
				currentDiameter * 0.5f,
				static_cast<float>(pointTime)
			};
		}
	};

	struct ActiveMouseStroke
	{
		StrokeModeler modeler;
		std::vector<Result> modeledResults;
		std::vector<Result> predictedResults;
		std::vector<InkPoint> realPoints;
		std::vector<InkPoint> predictedPoints;
		std::vector<InkPoint> l0DrawPoints;
		std::vector<InkPoint> previousL0DrawPoints;
		size_t convertedResultCount = 0;
		size_t committedIndex = 0;
		RECT lastL0Rect = RECT(0, 0, 0, 0);
		RECT currentL0Rect = RECT(0, 0, 0, 0);
		StrokeWidthEstimator widthEstimator;
		POINT lastRawPosition = POINT{ 0, 0 };
		bool hasLastRawPosition = false;
		bool idleFrozen = false;
		int visualStableFrameCount = 0;
		double lastMovementInputTime = 0.0;
		double lastFrameWallTime = 0.0;
		double logicalInputTime = 0.0;

		ActiveMouseStroke(float baseDiameter, float expectedSpeed)
			: widthEstimator(baseDiameter, expectedSpeed)
		{
		}
	};

	const char* GetDriverTypeName(D3D_DRIVER_TYPE driverType)
	{
		switch (driverType)
		{
		case D3D_DRIVER_TYPE_HARDWARE:
			return "Hardware";
		case D3D_DRIVER_TYPE_WARP:
			return "WARP";
		default:
			return "Unknown";
		}
	}

	const char* GetFeatureLevelName(D3D_FEATURE_LEVEL featureLevel)
	{
		switch (featureLevel)
		{
		case D3D_FEATURE_LEVEL_11_1:
			return "11_1";
		case D3D_FEATURE_LEVEL_11_0:
			return "11_0";
		case D3D_FEATURE_LEVEL_10_1:
			return "10_1";
		case D3D_FEATURE_LEVEL_10_0:
			return "10_0";
		case D3D_FEATURE_LEVEL_9_3:
			return "9_3";
		case D3D_FEATURE_LEVEL_9_2:
			return "9_2";
		case D3D_FEATURE_LEVEL_9_1:
			return "9_1";
		default:
			return "Unknown";
		}
	}

	HRESULT CreateD3D11DeviceWithCompatibleFeatureLevels(
		D3D_DRIVER_TYPE driverType,
		UINT creationFlags,
		CComPtr<ID3D11Device>& device,
		D3D_FEATURE_LEVEL& actualFeatureLevel,
		CComPtr<ID3D11DeviceContext>& deviceContext)
	{
		static const D3D_FEATURE_LEVEL preferredFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		static const D3D_FEATURE_LEVEL fallbackFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_0,
		};

		device.Release();
		deviceContext.Release();

		HRESULT hr = D3D11CreateDevice(
			nullptr,
			driverType,
			nullptr,
			creationFlags,
			preferredFeatureLevels,
			ARRAYSIZE(preferredFeatureLevels),
			D3D11_SDK_VERSION,
			&device,
			&actualFeatureLevel,
			&deviceContext
		);
		if (hr == E_INVALIDARG)
		{
			device.Release();
			deviceContext.Release();

			hr = D3D11CreateDevice(
				nullptr,
				driverType,
				nullptr,
				creationFlags,
				fallbackFeatureLevels,
				ARRAYSIZE(fallbackFeatureLevels),
				D3D11_SDK_VERSION,
				&device,
				&actualFeatureLevel,
				&deviceContext
			);
		}

		return hr;
	}
}

void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
{
	// 1. 计算目标帧时间 (毫秒)
	// 例如: 60FPS -> 16.666... ms
	double targetFrameTimeMs = 1000.0 / targetFPS;

	// 2. 计算还需要等待的时间 (毫秒)
	double waitTimeMs = targetFrameTimeMs - frameTimeSpentMs;

	// 如果已经超时（掉帧），直接返回，不等待
	if (waitTimeMs <= 0.0)
	{
		return;
	}

	// 获取高精度计时器的频率 (Ticks Per Second)
	static LARGE_INTEGER freq = { 0 };
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

	// 记录开始等待时刻的 QPC
	LARGE_INTEGER startCounter, currentCounter;
	QueryPerformanceCounter(&startCounter);

	// 将等待时间 (ms) 转换为 QPC 的 Ticks 单位
	// 公式: (ms * freq) / 1000
	long long waitTicks = (long long)((waitTimeMs * (double)freq.QuadPart) / 1000.0);
	long long targetEndTick = startCounter.QuadPart + waitTicks;

	// === 阶段一：Sleep (粗略等待) ===
	// 只有当剩余时间大于 2ms 时才启用 Sleep，留出 1.5ms 的安全余量给 Spin
	if (waitTimeMs > 2.0)
	{
		// 预留约 1.5ms 的时间给最后的忙等待，其余时间睡觉
		// 注意这里显式使用 std::milli
		double sleepMs = waitTimeMs - 1.5;
		std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepMs));
	}

	// === 阶段二：Spin (高精度忙等待) ===
	// 死循环直到 QPC 达到目标 Tick
	do
	{
		QueryPerformanceCounter(&currentCounter);

		YieldProcessor();
	} while (currentCounter.QuadPart < targetEndTick);
}
void UnionRectInPlace(RECT& target, const RECT& add)
{
	// 新增矩形无效，直接返回
	if (add.left >= add.right || add.top >= add.bottom) return;
	// target 是空矩形，直接替换
	if (target.left >= target.right || target.top >= target.bottom)
	{
		target = add;
		return;
	}

	target.left = min(target.left, add.left);
	target.top = min(target.top, add.top);
	target.right = max(target.right, add.right);
	target.bottom = max(target.bottom, add.bottom);
}

bool IsEmptyRect(const RECT& rect)
{
	return rect.left >= rect.right || rect.top >= rect.bottom;
}

RECT ClampRectToCanvas(RECT rect)
{
	rect.left = max(0L, rect.left);
	rect.top = max(0L, rect.top);
	rect.right = min((long)windowInfo.w, rect.right);
	rect.bottom = min((long)windowInfo.h, rect.bottom);
	if (IsEmptyRect(rect)) return RECT(0, 0, 0, 0);
	return rect;
}

RECT GetFullCanvasRect()
{
	return RECT(0, 0, static_cast<LONG>(windowInfo.w), static_cast<LONG>(windowInfo.h));
}

RECT RectFromStrokePoints(
	const vector<InkPoint>& points,
	size_t firstIndex = 0,
	size_t lastIndex = (std::numeric_limits<size_t>::max)())
{
	if (points.empty() || firstIndex >= points.size()) return RECT(0, 0, 0, 0);
	lastIndex = min(lastIndex, points.size());
	if (firstIndex >= lastIndex) return RECT(0, 0, 0, 0);

	RECT rect = RECT(0, 0, 0, 0);
	for (size_t i = firstIndex; i < lastIndex; ++i)
	{
		const InkPoint& point = points[i];
		const float padding = point.r + 3.0f;
		const RECT pointRect = RECT(
			static_cast<LONG>(std::floor(point.x - padding)),
			static_cast<LONG>(std::floor(point.y - padding)),
			static_cast<LONG>(std::ceil(point.x + padding)),
			static_cast<LONG>(std::ceil(point.y + padding))
		);
		UnionRectInPlace(rect, pointRect);
	}
	return ClampRectToCanvas(rect);
}

bool UpdateRawPositionAndDetectMovement(ActiveMouseStroke& stroke, const POINT& rawPosition)
{
	if (!stroke.hasLastRawPosition)
	{
		stroke.lastRawPosition = rawPosition;
		stroke.hasLastRawPosition = true;
		return false;
	}

	const float dx = static_cast<float>(rawPosition.x - stroke.lastRawPosition.x);
	const float dy = static_cast<float>(rawPosition.y - stroke.lastRawPosition.y);
	if (dx * dx + dy * dy <= kIdleMoveThresholdPx * kIdleMoveThresholdPx) return false;

	stroke.lastRawPosition = rawPosition;
	return true;
}

bool AreL0VisualsClose(const vector<InkPoint>& current, const vector<InkPoint>& previous)
{
	if (current.size() != previous.size()) return false;

	const float positionEpsilonSq = kVisualStablePositionEpsilonPx * kVisualStablePositionEpsilonPx;
	for (size_t i = 0; i < current.size(); ++i)
	{
		const float dx = current[i].x - previous[i].x;
		const float dy = current[i].y - previous[i].y;
		if (dx * dx + dy * dy > positionEpsilonSq) return false;
		if (std::abs(current[i].r - previous[i].r) > kVisualStableRadiusEpsilonPx) return false;
	}

	return true;
}

void UpdateIdleFreezeState(ActiveMouseStroke& stroke, bool rawMoved, double liveTipDurationSeconds)
{
	if (rawMoved)
	{
		stroke.visualStableFrameCount = 0;
		stroke.previousL0DrawPoints = stroke.l0DrawPoints;
		return;
	}

	// 停笔后等笔锋和模拟粗细真正稳定，再冻结模型输入，避免继续生成无视觉变化的点。
	const bool stoppedLongEnough =
		(stroke.logicalInputTime - stroke.lastMovementInputTime) >= liveTipDurationSeconds;
	if (stoppedLongEnough && AreL0VisualsClose(stroke.l0DrawPoints, stroke.previousL0DrawPoints))
	{
		++stroke.visualStableFrameCount;
	}
	else
	{
		stroke.visualStableFrameCount = 0;
	}

	stroke.previousL0DrawPoints = stroke.l0DrawPoints;
	if (stroke.visualStableFrameCount >= kVisualStableRequiredFrames)
	{
		stroke.idleFrozen = true;
	}
}

void LogFrameTiming(
	size_t committedIndex,
	size_t realPointCount,
	size_t predictedPointCount,
	size_t l0PointCount,
	double workMs,
	double previousFrameMs,
	bool idleFrozen)
{
	const int realFps = (previousFrameMs > 0.001) ? static_cast<int>(1000.0 / previousFrameMs) : 0;
	char buffer[256];
	// 输出含义：
	// commit 已烘干到 L1 的真实点索引；work 当前帧绘制/模型耗时；
	// prev-real 上一整帧真实 FPS/耗时，包含等待和控制台输出；
	// realPts/predPts/l0Pts 分别是真实点、预测点、当前 L0 绘制点数；frozen 表示停笔稳定后是否冻结输入。
	const int lineLength = std::snprintf(
		buffer,
		sizeof(buffer),
		"commit:%zu work:%.3fms prev-real:%d FPS(%.3fms) realPts:%zu predPts:%zu l0Pts:%zu frozen:%d\r\n",
		committedIndex,
		workMs,
		realFps,
		previousFrameMs,
		realPointCount,
		predictedPointCount,
		l0PointCount,
		idleFrozen ? 1 : 0
	);
	if (lineLength <= 0) return;

	const DWORD writeLength = static_cast<DWORD>(min(lineLength, static_cast<int>(sizeof(buffer) - 1)));
	WriteFastConsoleLine(buffer, writeLength);
}

void AppendNewModeledPoints(ActiveMouseStroke& stroke)
{
	for (size_t i = stroke.convertedResultCount; i < stroke.modeledResults.size(); ++i)
	{
		stroke.realPoints.push_back(stroke.widthEstimator.Append(stroke.modeledResults[i]));
	}
	stroke.convertedResultCount = stroke.modeledResults.size();
}

void RebuildPredictedPoints(ActiveMouseStroke& stroke)
{
	stroke.predictedPoints.clear();

	// 预测点只用于本帧 L0，使用粗细估算器副本，不能污染真实笔画状态。
	StrokeWidthEstimator predictionWidth = stroke.widthEstimator;
	for (const Result& result : stroke.predictedResults)
	{
		stroke.predictedPoints.push_back(predictionWidth.Append(result));
	}
}

double GetPredictionDurationSeconds(const ActiveMouseStroke& stroke)
{
	if (stroke.realPoints.empty() || stroke.predictedPoints.empty()) return 0.0;
	return max(0.0, static_cast<double>(stroke.predictedPoints.back().time - stroke.realPoints.back().time));
}

size_t FindProtectedStartIndex(const vector<InkPoint>& points, double protectedDurationSeconds)
{
	if (points.size() < 2) return 0;

	const double startTime = static_cast<double>(points.back().time) - max(0.0, protectedDurationSeconds);
	size_t startIndex = 0;

	// 保留前一个连接点，避免 L1 和 L0 的交界处断开。
	while (startIndex + 1 < points.size() && points[startIndex + 1].time < startTime)
	{
		++startIndex;
	}
	return startIndex;
}

void ApplyLiveTipTaper(vector<InkPoint>& points, double liveTipDurationSeconds)
{
	if (points.empty() || liveTipDurationSeconds <= 0.0) return;

	const double endTime = points.back().time;
	const double tipStartTime = endTime - liveTipDurationSeconds;
	size_t firstTipIndex = points.size() - 1;
	while (firstTipIndex > 0 && static_cast<double>(points[firstTipIndex - 1].time) >= tipStartTime)
	{
		--firstTipIndex;
	}

	// 笔锋长度不够时不直接收成最尖，等尾部时长长起来后再逐步变细。
	const double actualTipSpan = max(0.0, endTime - static_cast<double>(points[firstTipIndex].time));
	const float spanRatio = SmoothStep01(static_cast<float>(actualTipSpan / liveTipDurationSeconds));
	const float newestScale = LerpFloat(1.0f, 0.28f, spanRatio);

	for (size_t i = firstTipIndex; i < points.size(); ++i)
	{
		InkPoint& point = points[i];

		const float ageRatio = (actualTipSpan > 0.000001)
			? static_cast<float>((endTime - static_cast<double>(point.time)) / actualTipSpan)
			: 0.0f;
		const float tipRatio = SmoothStep01(ageRatio);
		const float scale = LerpFloat(newestScale, 1.0f, tipRatio);
		point.r *= scale;
	}
}

void RebuildL0DrawPoints(ActiveMouseStroke& stroke, double liveTipDurationSeconds)
{
	stroke.l0DrawPoints.clear();

	if (!stroke.realPoints.empty())
	{
		const size_t startIndex = min(stroke.committedIndex, stroke.realPoints.size() - 1);
		stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.realPoints.begin() + startIndex, stroke.realPoints.end());
	}

	stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.predictedPoints.begin(), stroke.predictedPoints.end());
	ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds);
	stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints);
}

RECT CommitStablePrefixToL1(
	ActiveMouseStroke& stroke,
	double liveTipDurationSeconds,
	double predictionDurationSeconds,
	XMFLOAT4 color,
	float shapeType,
	bool eraser)
{
	if (stroke.realPoints.size() < 2) return RECT(0, 0, 0, 0);

	const double protectedDuration = liveTipDurationSeconds + predictionDurationSeconds;
	const size_t protectedStartIndex = FindProtectedStartIndex(stroke.realPoints, protectedDuration);
	if (protectedStartIndex <= stroke.committedIndex) return RECT(0, 0, 0, 0);

	vector<InkPoint> stablePoints(
		stroke.realPoints.begin() + stroke.committedIndex,
		stroke.realPoints.begin() + protectedStartIndex + 1
	);

	inkRenderer.SetOMTarget(inkRenderer.layerL1RTV);
	inkRenderer.DrawStrokeOrDot(stablePoints, color, shapeType, eraser);
	stroke.committedIndex = protectedStartIndex;
	return RectFromStrokePoints(stablePoints);
}

void DrawL0LiveComposite(ActiveMouseStroke& stroke, XMFLOAT4 color, float shapeType, bool eraser)
{
	inkRenderer.ClearRTV(inkRenderer.layerL0RTV, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));
	if (stroke.l0DrawPoints.empty()) return;

	inkRenderer.SetOMTarget(inkRenderer.layerL0RTV);
	inkRenderer.DrawStrokeOrDot(stroke.l0DrawPoints, color, shapeType, eraser);
}

void CompositeLayersToBackBuffer(RECT dirty)
{
	dirty = ClampRectToCanvas(dirty);
	if (IsEmptyRect(dirty)) return;

	inkRenderer.CopyResource(inkRenderer.backBufferTexture, inkRenderer.layerL2Texture, dirty);
	inkRenderer.AlphaBlendResource(inkRenderer.backBufferRTV, inkRenderer.layerL1SRV, dirty);
	inkRenderer.AlphaBlendResource(inkRenderer.backBufferRTV, inkRenderer.layerL0SRV, dirty);
}

void PresentFrame(IDXGISwapChain1* swapChain, RECT dirty, bool presentFull)
{
	if (presentFull)
	{
		swapChain->Present(0, 0);
		return;
	}

	dirty = ClampRectToCanvas(dirty);
	if (IsEmptyRect(dirty)) return;

	DXGI_PRESENT_PARAMETERS parameters = {};
	parameters.DirtyRectsCount = 1;
	parameters.pDirtyRects = &dirty;
	parameters.pScrollRect = nullptr;
	parameters.pScrollOffset = nullptr;

	swapChain->Present1(0, 0, &parameters);
}

LRESULT CALLBACK Draw3WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED)
		{
			const int width = static_cast<int>(LOWORD(lParam));
			const int height = static_cast<int>(HIWORD(lParam));
			if (width > 0 && height > 0)
			{
				// WndProc 只记录尺寸变化，D3D 资源释放和重建放回主绘制线程处理。
				g_pendingResizeWidth.store(width, std::memory_order_relaxed);
				g_pendingResizeHeight.store(height, std::memory_order_relaxed);
				g_resizeRequested.store(true, std::memory_order_release);
			}
		}
		break;

	case WM_KEYDOWN:
		switch (wParam)
		{
		case '0':
		case VK_NUMPAD0:
			g_clearCanvasRequested.store(true, std::memory_order_relaxed);
			return 0;

		case '1':
		case VK_NUMPAD1:
			g_brushShapeType.store(0, std::memory_order_relaxed);
			return 0;
		}
		break;
	}

	return HIWINDOW_DEFAULT_PROC;
}

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h, EW_SHOWCONSOLE, _T(""), Draw3WndProc);
	}

	// 初始化 D3D 设备
	CComPtr<ID3D11DeviceContext> d3dDeviceContext; // DC
	{
		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL actualFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		D3D_DRIVER_TYPE activeDriverType = D3D_DRIVER_TYPE_UNKNOWN;
		HRESULT hr = S_OK;

		hr = CreateD3D11DeviceWithCompatibleFeatureLevels(
			D3D_DRIVER_TYPE_HARDWARE,
			creationFlags,
			d3dDevice_HARDWARE,
			actualFeatureLevel,
			d3dDeviceContext
		);
		if (FAILED(hr))
		{
			cout << "Hardware device initialization failed. Falling back to WARP." << endl;

			hr = CreateD3D11DeviceWithCompatibleFeatureLevels(
				D3D_DRIVER_TYPE_WARP,
				creationFlags,
				d3dDevice_HARDWARE,
				actualFeatureLevel,
				d3dDeviceContext
			);

			if (FAILED(hr))
			{
				cout << "Failed to initialize a D3D11 device with both Hardware and WARP." << endl;
				return -1;
			}

			activeDriverType = D3D_DRIVER_TYPE_WARP;
		}
		else
		{
			activeDriverType = D3D_DRIVER_TYPE_HARDWARE;
		}

		cout << "Current D3D device: " << GetDriverTypeName(activeDriverType) << endl;
		cout << "D3D feature level: " << GetFeatureLevelName(actualFeatureLevel) << endl;

		hr = d3dDevice_HARDWARE->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice1));
		if (FAILED(hr))
		{
			cout << "Failed to query IDXGIDevice1 from the D3D11 device." << endl;
			return -1;
		}
	}

	// 从 windows8 开始可以考虑 SwapChain2 的 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT 更适合墨迹输入

	// 常规场景下的墨迹输入应使用 dxgiDevice1::SetMaximumFrameLatency(1) 来确保有一帧的间隙 CPU 处理时间留给 GPU 并行渲染来提高性能
	dxgiDevice1->SetMaximumFrameLatency(1);

	// 后续性能选项卡中可以提供一个 GPU 高优先级 的选项
	// dxgiDevice1->SetGPUThreadPriority(2);

	// SwapChain
	CComPtr<IDXGISwapChain1> swapChain;
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = windowInfo.w;
		swapChainDesc.Height = windowInfo.h;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Flags = 0;

		CComPtr<IDXGIAdapter> dxgiAdapter;
		dxgiDevice1->GetAdapter(&dxgiAdapter);

		CComPtr<IDXGIFactory2> dxgiFactory;
		dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

		dxgiFactory->CreateSwapChainForHwnd(
			d3dDevice_HARDWARE,
			windowHWND,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain
		);

		// win7 上 SetBackgroundColor 会因 E_NOTIMPL 失败
		//DXGI_RGBA color = { 1.0f, 1.0f, 1.0f, 1.0f };
		//swapChain->SetBackgroundColor(&color);
	}

	// 交换链应该保证指定脏区，而不是全部重绘
	// 后续修改，非 flip_discard

	inkRenderer.Init(d3dDevice_HARDWARE, d3dDeviceContext, swapChain, static_cast<UINT>(windowInfo.w), static_cast<UINT>(windowInfo.h));

	// 每帧绘制前应该
	/*
			inkRenderer.SetOMTarget();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.backBufferRTV, clearColor);
	*/

	// 简单的 DPI 初始化
	int dpiX;
	{
		HDC screen = GetDC(nullptr);
		dpiX = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(nullptr, screen);
	}
	// 初始调测参数
	const bool debug = true;
	const StrokeTimingProfile timingProfile = GetStrokeTimingProfile(kActiveStrokeTimingProfileId);
	const float expected_speed = 500.0f * (static_cast<float>(dpiX) / 96.0f); // DPI 期望速度
	const float limited_speed = expected_speed * 3.0f; // 最高允许速度
	const double liveTipDurationSeconds = GetLiveTipDurationSeconds(timingProfile); // L0 笔锋可见时长
	// 模型初始化
	KalmanPredictorParams kalman_predictor_params;
	{
		kalman_predictor_params.process_noise = 0.05;
		kalman_predictor_params.measurement_noise = 0.01;
		kalman_predictor_params.min_stable_iteration = 4;
		kalman_predictor_params.max_time_samples = timingProfile.kalman_max_time_samples;
		kalman_predictor_params.min_catchup_velocity = expected_speed / 1000.0f;
		kalman_predictor_params.acceleration_weight = 0.5f;
		kalman_predictor_params.jerk_weight = 0.1f;
		kalman_predictor_params.prediction_interval = Duration(timingProfile.prediction_interval_seconds);
		kalman_predictor_params.confidence_params = {
			.desired_number_of_samples = timingProfile.kalman_desired_number_of_samples,
			.max_estimation_distance = 1.5f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.min_travel_speed = 0.05f * expected_speed,
			.max_travel_speed = 0.25f * expected_speed,
			.max_linear_deviation = 10.0f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.baseline_linearity_confidence = 0.4f
		};
	}
	StrokeModelParams params{
		.wobble_smoother_params{
			.is_enabled = true,
			.timeout = Duration(timingProfile.wobble_timeout_seconds),
			.speed_floor = timingProfile.wobble_speed_floor_ratio * expected_speed,
			.speed_ceiling = timingProfile.wobble_speed_ceiling_ratio * expected_speed
		},
		.position_modeler_params{
			.spring_mass_constant = 11.f / 32400,
			.drag_constant = 72.f
		},
		.sampling_params{
			.min_output_rate = timingProfile.min_output_rate,
			.end_of_stroke_stopping_distance = .001f,
			.end_of_stroke_max_iterations = 20,
			.max_outputs_per_call = timingProfile.max_outputs_per_call
		},
	};
	auto clearCanvas = [&swapChain]()
		{
			const XMFLOAT4 finalCanvasClearColor(1.0f, 1.0f, 1.0f, 1.0f);
			const XMFLOAT4 transparentClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			inkRenderer.ClearRTV(inkRenderer.layerL2RTV, finalCanvasClearColor);
			inkRenderer.ClearRTV(inkRenderer.layerL1RTV, transparentClearColor);
			inkRenderer.ClearRTV(inkRenderer.layerL0RTV, transparentClearColor);
			inkRenderer.ClearRTV(inkRenderer.backBufferRTV, finalCanvasClearColor);
			swapChain->Present(0, 0);
		};

	auto presentFullCanvas = [&swapChain]()
		{
			const RECT fullCanvasRect = GetFullCanvasRect();
			CompositeLayersToBackBuffer(fullCanvasRect);
			PresentFrame(swapChain, fullCanvasRect, true);
		};

	auto processPendingResize = [&swapChain, &presentFullCanvas](bool presentAfterResize)
		{
			if (!g_resizeRequested.exchange(false, std::memory_order_acquire)) return false;

			const int width = g_pendingResizeWidth.load(std::memory_order_relaxed);
			const int height = g_pendingResizeHeight.load(std::memory_order_relaxed);
			if (width <= 0 || height <= 0) return false;
			if (width == windowInfo.w && height == windowInfo.h) return false;

			const int oldWidth = windowInfo.w;
			const int oldHeight = windowInfo.h;
			if (!inkRenderer.Resize(swapChain, static_cast<UINT>(width), static_cast<UINT>(height)))
			{
				cout << "Failed to resize D3D resources to " << width << "x" << height << endl;
				windowInfo.w = oldWidth;
				windowInfo.h = oldHeight;
				return false;
			}

			// resize 后窗口逻辑尺寸立即跟随，新区域由 L2 白底和透明 L1/L0 重新合成。
			windowInfo.w = width;
			windowInfo.h = height;
			if (presentAfterResize) presentFullCanvas();
			return true;
		};

	clearCanvas();

	ExMessage m{};
	while (true)
	{
		if (g_clearCanvasRequested.exchange(false, std::memory_order_relaxed))
		{
			clearCanvas();
		}
		processPendingResize(true);

		if (!hiex::peekmessage_win32(&m, EM_MOUSE, true, windowHWND))
		{
			Sleep(1);
			continue;
		}

		if (m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN)
		{
			bool eraser = (m.message == WM_RBUTTONDOWN) ? true : false;
			eraser = false;

			// 检查设备是否丢失，并重建
			// TODO

			RECT strokeDirty = RECT(0, 0, 0, 0);
			bool isFirstFrame = true;

			ApplyPredictionMode(params, kalman_predictor_params);

			const float baseDiameter = eraser ? 50.0f : 5.0f;
			const float shapeType = static_cast<float>(g_brushShapeType.load(std::memory_order_relaxed));
			const XMFLOAT4 stableInkColor(1.0f, 0.0f, 0.0f, 1.0f);
			const XMFLOAT4 liveInkColor = (kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer)
				? XMFLOAT4(0.0f, 0.35f, 1.0f, 1.0f)
				: stableInkColor;

			ActiveMouseStroke stroke(baseDiameter, expected_speed);
			if (absl::Status status = stroke.modeler.Reset(params); !status.ok())
			{
				cout << "Error: " << status.message() << endl;
			}

			float xO = m.x;
			float yO = m.y;

			chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();

			Input input
			{
				.event_type = Input::EventType::kDown,
				.position = ink::stroke_model::Vec2(xO,yO),
				.time = Time(0.0)
			};
			if (absl::Status status = stroke.modeler.Update(input, stroke.modeledResults); !status.ok())
			{
				cout << "Error: " << status.message() << endl;
			}
			AppendNewModeledPoints(stroke);
			stroke.lastRawPosition = POINT{ static_cast<LONG>(xO), static_cast<LONG>(yO) };
			stroke.hasLastRawPosition = true;

			// 帧率保持
			double lastFrameStartMs = GetQpcTimeMilliseconds();
			bool hasFrameTiming = false;
			while (1)
			{
				const double frameStartMs = GetQpcTimeMilliseconds();
				const double previousFrameMs = hasFrameTiming ? (frameStartMs - lastFrameStartMs) : 0.0;
				lastFrameStartMs = frameStartMs;
				hasFrameTiming = true;

				bool forceL0Redraw = false;
				if (processPendingResize(false))
				{
					stroke.lastL0Rect = RECT(0, 0, 0, 0);
					stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints);
					strokeDirty = ClampRectToCanvas(strokeDirty);
					isFirstFrame = true;
					forceL0Redraw = true;
				}

				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(windowHWND, &pt);

				const double wallElapsedSeconds =
					chrono::duration<double>(chrono::high_resolution_clock::now() - start).count();
				const double wallDeltaSeconds = max(0.0, wallElapsedSeconds - stroke.lastFrameWallTime);
				stroke.lastFrameWallTime = wallElapsedSeconds;

				const bool rawMoved = UpdateRawPositionAndDetectMovement(stroke, pt);
				if (rawMoved)
				{
					stroke.idleFrozen = false;
					stroke.visualStableFrameCount = 0;
				}

				RECT stableDirty = RECT(0, 0, 0, 0);
				RECT l0FrameDirty = RECT(0, 0, 0, 0);
				if (!stroke.idleFrozen)
				{
					stroke.logicalInputTime += wallDeltaSeconds;
					if (rawMoved) stroke.lastMovementInputTime = stroke.logicalInputTime;

					Input input
					{
						.event_type = Input::EventType::kMove,
						.position = ink::stroke_model::Vec2(static_cast<float>(pt.x), static_cast<float>(pt.y)),
						.time = Time(stroke.logicalInputTime) // 冻结时不推进逻辑时间，恢复后不会一次性补点。
					};

					if (absl::Status status = stroke.modeler.Update(input, stroke.modeledResults); !status.ok())
					{
						cout << "Error: " << status.message() << endl;
					}
					AppendNewModeledPoints(stroke);

					stroke.predictedResults.clear();
					if (kActivePredictionMode != InkPredictionMode::Disabled)
					{
						if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
						{
							stroke.predictedResults.clear();
						}
					}
					RebuildPredictedPoints(stroke);

					const double predictionDurationSeconds = GetPredictionDurationSeconds(stroke);
					stableDirty = CommitStablePrefixToL1(
						stroke,
						liveTipDurationSeconds,
						predictionDurationSeconds,
						stableInkColor,
						shapeType,
						eraser
					);

					stroke.lastL0Rect = stroke.currentL0Rect;
					RebuildL0DrawPoints(stroke, liveTipDurationSeconds);
					UpdateIdleFreezeState(stroke, rawMoved, liveTipDurationSeconds);
					DrawL0LiveComposite(stroke, liveInkColor, shapeType, eraser);
					UnionRectInPlace(l0FrameDirty, stroke.lastL0Rect);
					UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect);
				}
				else if (forceL0Redraw)
				{
					stroke.lastL0Rect = RECT(0, 0, 0, 0);
					stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints);
					DrawL0LiveComposite(stroke, liveInkColor, shapeType, eraser);
					UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect);
				}

				RECT frameDirty = RECT(0, 0, 0, 0);
				UnionRectInPlace(frameDirty, stableDirty);
				UnionRectInPlace(frameDirty, l0FrameDirty);
				frameDirty = ClampRectToCanvas(frameDirty);

				if (!IsEmptyRect(frameDirty))
				{
					// 首帧会全屏 Present，必须先把整张画布合成到当前 backbuffer。
					const RECT compositeRect = isFirstFrame ? GetFullCanvasRect() : frameDirty;
					CompositeLayersToBackBuffer(compositeRect);
					PresentFrame(swapChain, frameDirty, isFirstFrame);
					isFirstFrame = false;
				}

				UnionRectInPlace(strokeDirty, stableDirty);

				if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) break;
				hiex::flushmessage_win32(EM_MOUSE, windowHWND);

				// 帧率锁
				{
					const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
					HighPrecisionWait(workMs, timingProfile.target_fps);
					LogFrameTiming(
						stroke.committedIndex,
						stroke.realPoints.size(),
						stroke.predictedPoints.size(),
						stroke.l0DrawPoints.size(),
						workMs,
						previousFrameMs,
						stroke.idleFrozen
					);
				}
			}

			if (processPendingResize(false))
			{
				stroke.lastL0Rect = RECT(0, 0, 0, 0);
				stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints);
				strokeDirty = ClampRectToCanvas(strokeDirty);
				isFirstFrame = true;
			}

			// 抬笔时把最后一帧用户看到的 L0 原样落到 L1，再整体烘干到 L2。
			if (!stroke.l0DrawPoints.empty())
			{
				inkRenderer.SetOMTarget(inkRenderer.layerL1RTV);
				inkRenderer.DrawStrokeOrDot(stroke.l0DrawPoints, liveInkColor, shapeType, eraser);
				UnionRectInPlace(strokeDirty, stroke.currentL0Rect);
			}

			strokeDirty = ClampRectToCanvas(strokeDirty);
			if (!IsEmptyRect(strokeDirty))
			{
				inkRenderer.AlphaBlendResource(inkRenderer.layerL2RTV, inkRenderer.layerL1SRV, strokeDirty);
				inkRenderer.ClearRTV(inkRenderer.layerL1RTV, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));
				inkRenderer.ClearRTV(inkRenderer.layerL0RTV, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));
				const RECT finalPresentRect = isFirstFrame ? GetFullCanvasRect() : strokeDirty;
				inkRenderer.CopyResource(inkRenderer.backBufferTexture, inkRenderer.layerL2Texture, finalPresentRect);
				PresentFrame(swapChain, strokeDirty, isFirstFrame);
			}
			else
			{
				inkRenderer.ClearRTV(inkRenderer.layerL0RTV, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));
				if (isFirstFrame) presentFullCanvas();
			}

			hiex::flushmessage_win32(EM_MOUSE, windowHWND);
		}
	}

	getmessage(EM_KEY);
	return 0;
}
