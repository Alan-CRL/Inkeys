#include "main.h"

#include "renderer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <dcomp.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

namespace
{
	std::atomic<bool> g_clearCanvasRequested = false;
	std::atomic<bool> g_resizeRequested = false;
	std::atomic<bool> g_fullPresentRequested = false;
	std::atomic<bool> g_exitRequested = false;
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

	enum class TransparentPresentMode
	{
		UlwDirtyRect, // ULW + dirty rect，保留近透明背景避免鼠标穿透。
		DirectCompositionVisualTree, // DComp 单 visual + composition swapchain 路径。
		DwmBlurBehind, // 一代 DWM blur-behind，对照保留，默认回退链不再自动使用。
		DwmBlurBehind2 // 正式 DWM extend-frame 方案：无边框 + DwmExtendFrameIntoClientArea。
	};

	constexpr InkPredictionMode kActivePredictionMode = InkPredictionMode::Kalman;
	constexpr LiveTipLengthMode kActiveLiveTipLengthMode = LiveTipLengthMode::Normal;
	constexpr DebugLayerColorMode kActiveDebugLayerColorMode = DebugLayerColorMode::NormalInkColor;
	constexpr TransparentPresentMode kPreferredTransparentPresentMode = TransparentPresentMode::DwmBlurBehind2;
	TransparentPresentMode g_activeTransparentPresentMode = kPreferredTransparentPresentMode;
	bool g_presentFailureLogged = false;

	const char* TransparentPresentModeName(TransparentPresentMode mode)
	{
		switch (mode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			return "UlwDirtyRect";
		case TransparentPresentMode::DirectCompositionVisualTree:
			return "DirectCompositionVisualTree";
		case TransparentPresentMode::DwmBlurBehind:
			return "DwmBlurBehind";
		case TransparentPresentMode::DwmBlurBehind2:
			return "DwmBlurBehind2";
		default:
			return "Unknown";
		}
	}

	bool IsUlwDirtyRectMode(TransparentPresentMode mode)
	{
		return mode == TransparentPresentMode::UlwDirtyRect;
	}

	bool IsUlwDirtyRectMode()
	{
		return IsUlwDirtyRectMode(g_activeTransparentPresentMode);
	}

	bool IsDirectCompositionMode(TransparentPresentMode mode)
	{
		return mode == TransparentPresentMode::DirectCompositionVisualTree;
	}

	bool IsDirectCompositionMode()
	{
		return IsDirectCompositionMode(g_activeTransparentPresentMode);
	}

	bool IsDwmBlurBehindMode(TransparentPresentMode mode)
	{
		return mode == TransparentPresentMode::DwmBlurBehind;
	}

	bool IsDwmBlurBehindMode()
	{
		return IsDwmBlurBehindMode(g_activeTransparentPresentMode);
	}

	bool IsDwmBlurBehind2Mode(TransparentPresentMode mode)
	{
		return mode == TransparentPresentMode::DwmBlurBehind2;
	}

	bool IsDwmBlurBehind2Mode()
	{
		return IsDwmBlurBehind2Mode(g_activeTransparentPresentMode);
	}

	bool IsDwmGlassMode(TransparentPresentMode mode)
	{
		return IsDwmBlurBehindMode(mode) || IsDwmBlurBehind2Mode(mode);
	}

	bool IsDwmGlassMode()
	{
		return IsDwmGlassMode(g_activeTransparentPresentMode);
	}

	bool IsGpuTransparentCompositionMode(TransparentPresentMode mode)
	{
		return IsDirectCompositionMode(mode) || IsDwmGlassMode(mode);
	}

	bool IsGpuTransparentCompositionMode()
	{
		return IsGpuTransparentCompositionMode(g_activeTransparentPresentMode);
	}

	XMFLOAT4 GetWindowBackgroundColorForMode(TransparentPresentMode mode)
	{
		if (IsGpuTransparentCompositionMode(mode))
		{
			// GPU 合成路径交给 DComp/DWM 读取 alpha，背景保持全透明。
			return kTransparentLayerClearColor;
		}
		return kTransparentWindowBackgroundColor;
	}

	XMFLOAT4 GetActiveWindowBackgroundColor()
	{
		return GetWindowBackgroundColorForMode(g_activeTransparentPresentMode);
	}

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

	struct WindowChromeMetrics
	{
		RECT windowRect = RECT(0, 0, 0, 0);
		int windowWidth = 0;
		int windowHeight = 0;
		int clientOffsetX = 0;
		int clientOffsetY = 0;
		int clientWidth = 0;
		int clientHeight = 0;
		int rightBorder = 0;
		int bottomBorder = 0;
		int resizeBorder = 6;
		RECT closeButtonRect = RECT(0, 0, 0, 0);
	};

	bool BuildWindowChromeMetrics(HWND hwnd, WindowChromeMetrics& metrics)
	{
		if (!hwnd) return false;

		RECT windowRect = {};
		RECT clientRect = {};
		if (!GetWindowRect(hwnd, &windowRect)) return false;
		if (!GetClientRect(hwnd, &clientRect)) return false;

		POINT clientOrigin = { 0, 0 };
		if (!ClientToScreen(hwnd, &clientOrigin)) return false;

		metrics.windowRect = windowRect;
		metrics.windowWidth = static_cast<int>(windowRect.right - windowRect.left);
		metrics.windowHeight = static_cast<int>(windowRect.bottom - windowRect.top);
		metrics.clientOffsetX = clientOrigin.x - windowRect.left;
		metrics.clientOffsetY = clientOrigin.y - windowRect.top;
		metrics.clientWidth = static_cast<int>(clientRect.right - clientRect.left);
		metrics.clientHeight = static_cast<int>(clientRect.bottom - clientRect.top);
		metrics.rightBorder = max(0, metrics.windowWidth - metrics.clientOffsetX - metrics.clientWidth);
		metrics.bottomBorder = max(0, metrics.windowHeight - metrics.clientOffsetY - metrics.clientHeight);
		metrics.resizeBorder = max(6, min(12, max(metrics.clientOffsetX, metrics.bottomBorder)));

		const int closeButtonWidth = 46;
		const int closeRight = max(metrics.clientOffsetX, metrics.windowWidth - metrics.rightBorder);
		const int closeTop = metrics.resizeBorder;
		const int closeBottom = max(metrics.clientOffsetY, closeTop + 26);
		metrics.closeButtonRect = RECT(
			max(metrics.clientOffsetX, closeRight - closeButtonWidth),
			closeTop,
			closeRight,
			min(metrics.windowHeight, closeBottom)
		);

		return metrics.windowWidth > 0 && metrics.windowHeight > 0 &&
			metrics.clientWidth > 0 && metrics.clientHeight > 0;
	}

	bool PointInRectInclusive(const RECT& rect, int x, int y)
	{
		return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
	}

	LRESULT HitTestWindowChrome(HWND hwnd, LPARAM lParam)
	{
		WindowChromeMetrics metrics;
		if (!BuildWindowChromeMetrics(hwnd, metrics)) return HTCLIENT;

		const int screenX = GET_X_LPARAM(lParam);
		const int screenY = GET_Y_LPARAM(lParam);
		const int x = screenX - metrics.windowRect.left;
		const int y = screenY - metrics.windowRect.top;
		if (x < 0 || y < 0 || x >= metrics.windowWidth || y >= metrics.windowHeight) return HTNOWHERE;

		const bool onLeft = x < metrics.resizeBorder;
		const bool onRight = x >= metrics.windowWidth - metrics.resizeBorder;
		const bool onTop = y < metrics.resizeBorder;
		const bool onBottom = y >= metrics.windowHeight - metrics.resizeBorder;

		if (onTop && onLeft) return HTTOPLEFT;
		if (onTop && onRight) return HTTOPRIGHT;
		if (onBottom && onLeft) return HTBOTTOMLEFT;
		if (onBottom && onRight) return HTBOTTOMRIGHT;
		if (onLeft) return HTLEFT;
		if (onRight) return HTRIGHT;
		if (onTop) return HTTOP;
		if (onBottom) return HTBOTTOM;

		if (PointInRectInclusive(metrics.closeButtonRect, x, y)) return HTCLOSE;
		if (y < metrics.clientOffsetY) return HTCAPTION;

		return HTCLIENT;
	}

	struct UlwDirtyRectPresenter
	{
		HWND hwnd = nullptr;
		CComPtr<ID3D11Device> device;
		CComPtr<ID3D11DeviceContext> context;
		CComPtr<ID3D11Texture2D> stagingTexture;
		UINT stagingWidth = 0;
		UINT stagingHeight = 0;
		HDC memoryDC = nullptr;
		HBITMAP dibBitmap = nullptr;
		HGDIOBJ oldBitmap = nullptr;
		void* dibBits = nullptr;
		int dibWidth = 0;
		int dibHeight = 0;
		int clientOffsetX = 0;
		int clientOffsetY = 0;

		DWORD MakePremultipliedBgra(BYTE r, BYTE g, BYTE b, BYTE a)
		{
			const BYTE pr = static_cast<BYTE>((static_cast<UINT>(r) * a + 127) / 255);
			const BYTE pg = static_cast<BYTE>((static_cast<UINT>(g) * a + 127) / 255);
			const BYTE pb = static_cast<BYTE>((static_cast<UINT>(b) * a + 127) / 255);
			return (static_cast<DWORD>(a) << 24) |
				(static_cast<DWORD>(pr) << 16) |
				(static_cast<DWORD>(pg) << 8) |
				static_cast<DWORD>(pb);
		}

		void FillDibRect(const RECT& rect, DWORD color)
		{
			if (!dibBits || dibWidth <= 0 || dibHeight <= 0) return;

			const LONG left = max(0L, rect.left);
			const LONG top = max(0L, rect.top);
			const LONG right = min(static_cast<LONG>(dibWidth), rect.right);
			const LONG bottom = min(static_cast<LONG>(dibHeight), rect.bottom);
			if (left >= right || top >= bottom) return;

			DWORD* pixels = static_cast<DWORD*>(dibBits);
			const size_t rowWidth = static_cast<size_t>(dibWidth);
			for (LONG y = top; y < bottom; ++y)
			{
				std::fill(
					pixels + static_cast<size_t>(y) * rowWidth + static_cast<size_t>(left),
					pixels + static_cast<size_t>(y) * rowWidth + static_cast<size_t>(right),
					color
				);
			}
		}

		void ForceAlphaRect(const RECT& rect, BYTE alpha)
		{
			if (!dibBits || dibWidth <= 0 || dibHeight <= 0) return;

			const LONG left = max(0L, rect.left);
			const LONG top = max(0L, rect.top);
			const LONG right = min(static_cast<LONG>(dibWidth), rect.right);
			const LONG bottom = min(static_cast<LONG>(dibHeight), rect.bottom);
			if (left >= right || top >= bottom) return;

			DWORD* pixels = static_cast<DWORD*>(dibBits);
			const size_t rowWidth = static_cast<size_t>(dibWidth);
			const DWORD alphaMask = static_cast<DWORD>(alpha) << 24;
			for (LONG y = top; y < bottom; ++y)
			{
				DWORD* row = pixels + static_cast<size_t>(y) * rowWidth;
				for (LONG x = left; x < right; ++x)
				{
					row[x] = (row[x] & 0x00FFFFFF) | alphaMask;
				}
			}
		}

		void DrawDibLine(int x0, int y0, int x1, int y1, DWORD color)
		{
			if (!dibBits || dibWidth <= 0 || dibHeight <= 0) return;

			const int dx = abs(x1 - x0);
			const int sx = x0 < x1 ? 1 : -1;
			const int dy = -abs(y1 - y0);
			const int sy = y0 < y1 ? 1 : -1;
			int error = dx + dy;
			while (true)
			{
				FillDibRect(RECT(x0 - 1, y0 - 1, x0 + 2, y0 + 2), color);
				if (x0 == x1 && y0 == y1) break;

				const int doubledError = 2 * error;
				if (doubledError >= dy)
				{
					error += dy;
					x0 += sx;
				}
				if (doubledError <= dx)
				{
					error += dx;
					y0 += sy;
				}
			}
		}

		void ForceChromeAlpha(const WindowChromeMetrics& metrics)
		{
			ForceAlphaRect(RECT(0, 0, metrics.windowWidth, metrics.clientOffsetY), 255);
			ForceAlphaRect(RECT(0, metrics.clientOffsetY, metrics.clientOffsetX, metrics.windowHeight), 255);
			ForceAlphaRect(RECT(metrics.windowWidth - metrics.rightBorder, metrics.clientOffsetY, metrics.windowWidth, metrics.windowHeight), 255);
			ForceAlphaRect(RECT(0, metrics.windowHeight - metrics.bottomBorder, metrics.windowWidth, metrics.windowHeight), 255);
		}

		void DrawWindowChrome(const WindowChromeMetrics& metrics)
		{
			const DWORD titleColor = MakePremultipliedBgra(35, 43, 54, 255);
			const DWORD borderColor = MakePremultipliedBgra(74, 85, 104, 255);
			const DWORD closeColor = MakePremultipliedBgra(185, 28, 28, 255);
			const DWORD closeGlyphColor = MakePremultipliedBgra(255, 255, 255, 255);

			FillDibRect(RECT(0, 0, metrics.windowWidth, metrics.clientOffsetY), titleColor);
			FillDibRect(RECT(0, 0, metrics.windowWidth, metrics.resizeBorder), borderColor);
			FillDibRect(RECT(0, metrics.clientOffsetY, metrics.clientOffsetX, metrics.windowHeight), borderColor);
			FillDibRect(RECT(metrics.windowWidth - metrics.rightBorder, metrics.clientOffsetY, metrics.windowWidth, metrics.windowHeight), borderColor);
			FillDibRect(RECT(0, metrics.windowHeight - metrics.bottomBorder, metrics.windowWidth, metrics.windowHeight), borderColor);
			FillDibRect(metrics.closeButtonRect, closeColor);

			WCHAR title[128] = {};
			if (!GetWindowTextW(hwnd, title, ARRAYSIZE(title)) || title[0] == L'\0')
			{
				wcscpy_s(title, L"Ink Stroke Modeler Test");
			}

			if (memoryDC)
			{
				HGDIOBJ oldFont = SelectObject(memoryDC, GetStockObject(DEFAULT_GUI_FONT));
				SetBkMode(memoryDC, TRANSPARENT);
				SetTextColor(memoryDC, RGB(226, 232, 240));

				RECT textRect = RECT(
					metrics.clientOffsetX + 12,
					metrics.resizeBorder,
					max(metrics.clientOffsetX + 12, metrics.closeButtonRect.left - 8),
					max(metrics.clientOffsetY, metrics.resizeBorder + 24)
				);
				DrawTextW(memoryDC, title, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
				if (oldFont && oldFont != HGDI_ERROR) SelectObject(memoryDC, oldFont);
			}

			const int cx = (metrics.closeButtonRect.left + metrics.closeButtonRect.right) / 2;
			const int cy = (metrics.closeButtonRect.top + metrics.closeButtonRect.bottom) / 2;
			const int glyphRadius = 6;
			DrawDibLine(cx - glyphRadius, cy - glyphRadius, cx + glyphRadius, cy + glyphRadius, closeGlyphColor);
			DrawDibLine(cx + glyphRadius, cy - glyphRadius, cx - glyphRadius, cy + glyphRadius, closeGlyphColor);
			ForceChromeAlpha(metrics);
		}

		void ReleaseDib()
		{
			if (memoryDC && oldBitmap)
			{
				SelectObject(memoryDC, oldBitmap);
				oldBitmap = nullptr;
			}
			if (dibBitmap)
			{
				DeleteObject(dibBitmap);
				dibBitmap = nullptr;
			}
			if (memoryDC)
			{
				DeleteDC(memoryDC);
				memoryDC = nullptr;
			}
			dibBits = nullptr;
			dibWidth = 0;
			dibHeight = 0;
			clientOffsetX = 0;
			clientOffsetY = 0;
		}

		void Reset()
		{
			ReleaseDib();
			stagingTexture.Release();
			device.Release();
			context.Release();
			stagingWidth = 0;
			stagingHeight = 0;
			hwnd = nullptr;
		}

		bool CreateStagingTexture(UINT width, UINT height)
		{
			if (!device || width == 0 || height == 0) return false;
			if (stagingTexture && stagingWidth == width && stagingHeight == height) return true;

			stagingTexture.Release();

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.MiscFlags = 0;

			if (FAILED(device->CreateTexture2D(&desc, nullptr, &stagingTexture)))
			{
				stagingWidth = 0;
				stagingHeight = 0;
				return false;
			}

			stagingWidth = width;
			stagingHeight = height;
			return true;
		}

		bool EnsureWindowDib()
		{
			RECT clientRect = {};
			if (!hwnd || !GetClientRect(hwnd, &clientRect)) return false;

			const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
			const int clientHeight = static_cast<int>(clientRect.bottom - clientRect.top);
			if (clientWidth <= 0 || clientHeight <= 0) return false;

			if (memoryDC && dibBitmap && dibBits &&
				dibWidth == clientWidth && dibHeight == clientHeight &&
				clientOffsetX == 0 && clientOffsetY == 0)
			{
				return true;
			}

			ReleaseDib();

			memoryDC = CreateCompatibleDC(nullptr);
			if (!memoryDC) return false;

			BITMAPINFO bitmapInfo = {};
			bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bitmapInfo.bmiHeader.biWidth = clientWidth;
			bitmapInfo.bmiHeader.biHeight = -clientHeight; // top-down，坐标直接和客户区一致。
			bitmapInfo.bmiHeader.biPlanes = 1;
			bitmapInfo.bmiHeader.biBitCount = 32;
			bitmapInfo.bmiHeader.biCompression = BI_RGB;

			dibBitmap = CreateDIBSection(memoryDC, &bitmapInfo, DIB_RGB_COLORS, &dibBits, nullptr, 0);
			if (!dibBitmap || !dibBits)
			{
				ReleaseDib();
				return false;
			}

			oldBitmap = SelectObject(memoryDC, dibBitmap);
			if (!oldBitmap || oldBitmap == HGDI_ERROR)
			{
				ReleaseDib();
				return false;
			}

			dibWidth = clientWidth;
			dibHeight = clientHeight;
			clientOffsetX = 0;
			clientOffsetY = 0;

			// ULW 的全透明像素会穿透鼠标，背景保留 alpha=1 的近透明值。
			const DWORD backgroundPixel = 0x01000000;
			std::fill_n(static_cast<DWORD*>(dibBits), static_cast<size_t>(dibWidth) * static_cast<size_t>(dibHeight), backgroundPixel);
			return true;
		}

		bool Initialize(HWND inHwnd, ID3D11Device* inDevice, ID3D11DeviceContext* inContext, UINT width, UINT height)
		{
			Reset();
			hwnd = inHwnd;
			device = inDevice;
			context = inContext;
			if (!hwnd || !device || !context) return false;

			return Resize(width, height);
		}

		bool Resize(UINT width, UINT height)
		{
			return CreateStagingTexture(width, height) && EnsureWindowDib();
		}

		bool Present(ID3D11Texture2D* finalTexture, RECT dirty, bool presentFull)
		{
			if (!context || !stagingTexture || !finalTexture) return false;
			if (!EnsureWindowDib()) return false;

			if (presentFull)
			{
				dirty = RECT(0, 0, static_cast<LONG>(stagingWidth), static_cast<LONG>(stagingHeight));
			}
			dirty.left = max(0L, dirty.left);
			dirty.top = max(0L, dirty.top);
			dirty.right = min(static_cast<LONG>(stagingWidth), dirty.right);
			dirty.bottom = min(static_cast<LONG>(stagingHeight), dirty.bottom);
			if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) return true;

			const int windowDirtyLeft = clientOffsetX + static_cast<int>(dirty.left);
			const int windowDirtyTop = clientOffsetY + static_cast<int>(dirty.top);
			const int windowDirtyRight = clientOffsetX + static_cast<int>(dirty.right);
			const int windowDirtyBottom = clientOffsetY + static_cast<int>(dirty.bottom);
			if (windowDirtyLeft < 0 || windowDirtyTop < 0 ||
				windowDirtyRight > dibWidth || windowDirtyBottom > dibHeight)
			{
				return false;
			}

			D3D11_BOX sourceRegion = {};
			sourceRegion.left = static_cast<UINT>(dirty.left);
			sourceRegion.top = static_cast<UINT>(dirty.top);
			sourceRegion.front = 0;
			sourceRegion.right = static_cast<UINT>(dirty.right);
			sourceRegion.bottom = static_cast<UINT>(dirty.bottom);
			sourceRegion.back = 1;
			context->CopySubresourceRegion(
				stagingTexture,
				0,
				static_cast<UINT>(dirty.left),
				static_cast<UINT>(dirty.top),
				0,
				finalTexture,
				0,
				&sourceRegion
			);

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) return false;

			const size_t copyBytes = static_cast<size_t>(dirty.right - dirty.left) * 4;
			const BYTE* srcBase = static_cast<const BYTE*>(mapped.pData) +
				static_cast<size_t>(dirty.top) * mapped.RowPitch +
				static_cast<size_t>(dirty.left) * 4;
			BYTE* dstBase = static_cast<BYTE*>(dibBits) +
				static_cast<size_t>(windowDirtyTop) * static_cast<size_t>(dibWidth) * 4 +
				static_cast<size_t>(windowDirtyLeft) * 4;
			for (LONG y = dirty.top; y < dirty.bottom; ++y)
			{
				const size_t rowIndex = static_cast<size_t>(y - dirty.top);
				std::memcpy(
					dstBase + rowIndex * static_cast<size_t>(dibWidth) * 4,
					srcBase + rowIndex * mapped.RowPitch,
					copyBytes
				);
			}
			context->Unmap(stagingTexture, 0);

			RECT windowRect = {};
			if (!GetWindowRect(hwnd, &windowRect)) return false;

			POINT dstPoint = { windowRect.left, windowRect.top };
			SIZE dstSize = { dibWidth, dibHeight };
			POINT srcPoint = { 0, 0 };
			RECT windowDirty = {
				static_cast<LONG>(windowDirtyLeft),
				static_cast<LONG>(windowDirtyTop),
				static_cast<LONG>(windowDirtyRight),
				static_cast<LONG>(windowDirtyBottom)
			};
			BLENDFUNCTION blend = {};
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = 255;
			blend.AlphaFormat = AC_SRC_ALPHA;

			UPDATELAYEREDWINDOWINFO ulwInfo = {};
			ulwInfo.cbSize = sizeof(ulwInfo);
			ulwInfo.hdcDst = nullptr;
			ulwInfo.pptDst = &dstPoint;
			ulwInfo.psize = &dstSize;
			ulwInfo.hdcSrc = memoryDC;
			ulwInfo.pptSrc = &srcPoint;
			ulwInfo.pblend = &blend;
			ulwInfo.dwFlags = ULW_ALPHA;
			ulwInfo.prcDirty = presentFull ? nullptr : &windowDirty;

			return UpdateLayeredWindowIndirect(hwnd, &ulwInfo) != FALSE;
		}
	};

	UlwDirtyRectPresenter g_ulwDirtyRectPresenter;

	bool PresentSwapChainWithDirtyRects(IDXGISwapChain1* swapChain, RECT dirty, bool presentFull)
	{
		if (!swapChain) return false;

		DXGI_PRESENT_PARAMETERS presentParameters = {};
		if (!presentFull)
		{
			dirty.left = max(0L, dirty.left);
			dirty.top = max(0L, dirty.top);
			dirty.right = min(static_cast<LONG>(windowInfo.w), dirty.right);
			dirty.bottom = min(static_cast<LONG>(windowInfo.h), dirty.bottom);
			if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) return true;

			presentParameters.DirtyRectsCount = 1;
			presentParameters.pDirtyRects = &dirty;
		}

		return SUCCEEDED(swapChain->Present1(0, 0, &presentParameters));
	}

	void LogHresult(const char* step, HRESULT hr)
	{
		cout << step << " failed. HRESULT=0x" << hex << static_cast<unsigned long>(hr) << dec << endl;
	}

	void LogWin32Error(const char* step, DWORD error)
	{
		cout << step << " failed. GetLastError=" << error << endl;
	}

	bool TrySetWindowLongPtr(HWND hwnd, int index, LONG_PTR value, const char* step)
	{
		SetLastError(ERROR_SUCCESS);
		const LONG_PTR previousValue = SetWindowLongPtr(hwnd, index, value);
		const DWORD error = GetLastError();
		if (previousValue == 0 && error != ERROR_SUCCESS)
		{
			LogWin32Error(step, error);
			return false;
		}
		return true;
	}

	bool IsDirectCompositionApiAvailable()
	{
		HMODULE dcompModule = LoadLibraryW(L"dcomp.dll");
		if (!dcompModule) return false;

		const FARPROC createDevice = GetProcAddress(dcompModule, "DCompositionCreateDevice");
		FreeLibrary(dcompModule);
		return createDevice != nullptr;
	}

	RECT GetPrimaryMonitorRect()
	{
		POINT origin = { 0, 0 };
		HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO monitorInfo = {};
		monitorInfo.cbSize = sizeof(monitorInfo);
		if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
		{
			return monitorInfo.rcMonitor;
		}
		return RECT(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	}

	bool IsDwmGlassTransparencyAvailable(TransparentPresentMode mode)
	{
		DWORD colorizationColor = 0;
		BOOL opaqueBlend = TRUE;
		const HRESULT hr = DwmGetColorizationColor(&colorizationColor, &opaqueBlend);
		if (FAILED(hr))
		{
			cout << TransparentPresentModeName(mode) << " DwmGetColorizationColor failed. HRESULT=0x"
				<< hex << static_cast<unsigned long>(hr) << dec << endl;
			return false;
		}
		if (opaqueBlend)
		{
			// DWM extend-frame 路径下 opaque blend 只记录，不阻止 UNSPECIFIED 继续测试。
			cout << "[" << TransparentPresentModeName(mode) << "] DWM glass is opaque; continue unspecified alpha path." << endl;
		}
		return true;
	}

	bool EnsureBorderlessTransparentWindowStyle(HWND hwnd, bool noRedirectionBitmap, bool layered)
	{
		if (!hwnd) return false;

		WCHAR title[128] = {};
		if (!GetWindowTextW(hwnd, title, ARRAYSIZE(title)) || title[0] == L'\0')
		{
			SetWindowTextW(hwnd, L"Ink Stroke Modeler Test");
		}

		bool changed = false;
		const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
		const LONG_PTR desiredStyle =
			(style & ~(WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) |
			WS_POPUP | WS_CLIPCHILDREN;
		if (desiredStyle != style)
		{
			if (!TrySetWindowLongPtr(hwnd, GWL_STYLE, desiredStyle, "SetWindowLongPtr(GWL_STYLE)"))
			{
				return false;
			}
			changed = true;
		}

		const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
		LONG_PTR desiredExStyle = exStyle & ~(
			WS_EX_LAYERED |
			WS_EX_TRANSPARENT |
			WS_EX_WINDOWEDGE |
			WS_EX_CLIENTEDGE |
			WS_EX_DLGMODALFRAME |
			WS_EX_STATICEDGE |
			WS_EX_NOREDIRECTIONBITMAP);
		if (layered)
		{
			desiredExStyle |= WS_EX_LAYERED;
		}
		if (noRedirectionBitmap)
		{
			// DComp 内容由 visual tree 进入 DWM，避免 HWND 自身重定向位图盖住透明客户区。
			desiredExStyle |= WS_EX_NOREDIRECTIONBITMAP;
		}
		if (desiredExStyle != exStyle)
		{
			if (!TrySetWindowLongPtr(hwnd, GWL_EXSTYLE, desiredExStyle, "SetWindowLongPtr(GWL_EXSTYLE)"))
			{
				return false;
			}
			changed = true;
		}

		if (changed)
		{
			SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		return true;
	}

	bool EnsureUlwWindowStyle(HWND hwnd)
	{
		// ULW 只启用 layered window，不设置 WS_EX_TRANSPARENT，避免鼠标穿透。
		return EnsureBorderlessTransparentWindowStyle(hwnd, false, true);
	}

	struct DirectCompositionVisualTreePresenter
	{
		HWND hwnd = nullptr;
		HMODULE dcompModule = nullptr;
		CComPtr<IDCompositionDevice> compositionDevice;
		CComPtr<IDCompositionTarget> compositionTarget;
		CComPtr<IDCompositionVisual> rootVisual;

		using DCompositionCreateDeviceFn = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);

		void Reset()
		{
			rootVisual.Release();
			compositionTarget.Release();
			compositionDevice.Release();
			hwnd = nullptr;
		}

		bool LoadDCompCreateDevice(DCompositionCreateDeviceFn& createDevice)
		{
			createDevice = nullptr;
			if (!dcompModule)
			{
				dcompModule = LoadLibraryW(L"dcomp.dll");
				if (!dcompModule)
				{
					cout << "DirectComposition unavailable. LoadLibrary(dcomp.dll) GetLastError="
						<< GetLastError() << endl;
					return false;
				}
			}

			createDevice = reinterpret_cast<DCompositionCreateDeviceFn>(
				GetProcAddress(dcompModule, "DCompositionCreateDevice")
			);
			if (!createDevice)
			{
				cout << "DirectComposition unavailable. GetProcAddress(DCompositionCreateDevice) GetLastError="
					<< GetLastError() << endl;
				return false;
			}
			return true;
		}

		bool Initialize(HWND inHwnd, IDXGIDevice* dxgiDevice, IDXGISwapChain1* swapChain, UINT width, UINT height)
		{
			(void)width;
			(void)height;
			Reset();
			hwnd = inHwnd;
			if (!hwnd || !dxgiDevice || !swapChain) return false;
			if (!EnsureBorderlessTransparentWindowStyle(hwnd, true, false)) return false;

			DCompositionCreateDeviceFn createDevice = nullptr;
			if (!LoadDCompCreateDevice(createDevice)) return false;

			IDCompositionDevice* rawDevice = nullptr;
			HRESULT hr = createDevice(dxgiDevice, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&rawDevice));
			if (FAILED(hr) || !rawDevice)
			{
				LogHresult("DCompositionCreateDevice", hr);
				return false;
			}
			compositionDevice.Attach(rawDevice);

			hr = compositionDevice->CreateTargetForHwnd(hwnd, TRUE, &compositionTarget);
			if (FAILED(hr) || !compositionTarget)
			{
				LogHresult("IDCompositionDevice::CreateTargetForHwnd", hr);
				return false;
			}

			hr = compositionDevice->CreateVisual(&rootVisual);
			if (FAILED(hr) || !rootVisual)
			{
				LogHresult("IDCompositionDevice::CreateVisual", hr);
				return false;
			}

			hr = rootVisual->SetContent(swapChain);
			if (FAILED(hr))
			{
				LogHresult("IDCompositionVisual::SetContent", hr);
				return false;
			}

			hr = compositionTarget->SetRoot(rootVisual);
			if (FAILED(hr))
			{
				LogHresult("IDCompositionTarget::SetRoot", hr);
				return false;
			}

			hr = compositionDevice->Commit();
			if (FAILED(hr))
			{
				LogHresult("IDCompositionDevice::Commit", hr);
				return false;
			}
			return true;
		}

		bool Resize(UINT width, UINT height)
		{
			(void)width;
			(void)height;
			return true;
		}

		bool Present(IDXGISwapChain1* swapChain, RECT dirty, bool presentFull)
		{
			return PresentSwapChainWithDirtyRects(swapChain, dirty, presentFull);
		}
	};

	DirectCompositionVisualTreePresenter g_directCompositionPresenter;

	struct DwmBlurBehindPresenter
	{
		HWND hwnd = nullptr;

		void Reset()
		{
			hwnd = nullptr;
		}

		bool EnsureSystemChrome()
		{
			return EnsureBorderlessTransparentWindowStyle(hwnd, false, false);
		}

		bool UpdateDwmBlurBehind()
		{
			if (!hwnd) return false;

			BOOL compositionEnabled = FALSE;
			HRESULT hr = DwmIsCompositionEnabled(&compositionEnabled);
			if (FAILED(hr))
			{
				LogHresult("DwmBlurBehind DwmIsCompositionEnabled", hr);
				return false;
			}
			if (!compositionEnabled)
			{
				cout << "[DwmBlurBehind] DWM composition is disabled." << endl;
				return false;
			}

			// 参考 DirectInkPresenter：整窗 blur region 让 premultiplied BGRA 背景透出。
			HRGN blurRegion = CreateRectRgn(0, 0, -1, -1);
			if (!blurRegion) return false;

			DWM_BLURBEHIND blurBehind = {};
			blurBehind.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION | DWM_BB_TRANSITIONONMAXIMIZED;
			blurBehind.fEnable = TRUE;
			blurBehind.hRgnBlur = blurRegion;
			blurBehind.fTransitionOnMaximized = TRUE;
			hr = DwmEnableBlurBehindWindow(hwnd, &blurBehind);
			DeleteObject(blurRegion);
			if (FAILED(hr))
			{
				LogHresult("DwmBlurBehind DwmEnableBlurBehindWindow", hr);
			}
			return SUCCEEDED(hr);
		}

		bool Initialize(HWND inHwnd, UINT width, UINT height)
		{
			(void)width;
			(void)height;
			Reset();
			hwnd = inHwnd;
			return EnsureSystemChrome() && UpdateDwmBlurBehind();
		}

		bool Resize(UINT width, UINT height)
		{
			(void)width;
			(void)height;
			return UpdateDwmBlurBehind();
		}

		bool Present(IDXGISwapChain1* swapChain, RECT dirty, bool presentFull)
		{
			// DWM 路径仍由原 D3D swapchain Present1 输出，透明只来自 blur-behind 初始化和 backbuffer alpha。
			return PresentSwapChainWithDirtyRects(swapChain, dirty, presentFull);
		}
	};

	DwmBlurBehindPresenter g_dwmBlurBehindPresenter;

	struct DwmBlurBehind2Presenter
	{
		HWND hwnd = nullptr;

		void Reset()
		{
			hwnd = nullptr;
		}

		bool UpdateExtendedGlassFrame()
		{
			if (!hwnd) return false;

			BOOL compositionEnabled = FALSE;
			HRESULT hr = DwmIsCompositionEnabled(&compositionEnabled);
			if (FAILED(hr))
			{
				LogHresult("DwmBlurBehind2 DwmIsCompositionEnabled", hr);
				return false;
			}
			if (!compositionEnabled)
			{
				cout << "[DwmBlurBehind2] DWM composition is disabled." << endl;
				return false;
			}

			// DwmBlurBehind2 使用整窗 glass frame，让 Win7 Aero 按 frame alpha 处理客户区。
			MARGINS margins = { -1, -1, -1, -1 };
			hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
			if (FAILED(hr))
			{
				LogHresult("DwmBlurBehind2 DwmExtendFrameIntoClientArea", hr);
			}
			return SUCCEEDED(hr);
		}

		bool Initialize(HWND inHwnd, UINT width, UINT height)
		{
			(void)width;
			(void)height;
			Reset();
			hwnd = inHwnd;
			return EnsureBorderlessTransparentWindowStyle(hwnd, false, false) && UpdateExtendedGlassFrame();
		}

		bool Resize(UINT width, UINT height)
		{
			(void)width;
			(void)height;
			return UpdateExtendedGlassFrame();
		}

		bool Present(IDXGISwapChain1* swapChain, RECT dirty, bool presentFull)
		{
			return PresentSwapChainWithDirtyRects(swapChain, dirty, presentFull);
		}
	};

	DwmBlurBehind2Presenter g_dwmBlurBehind2Presenter;

	void ResetTransparentPresenters()
	{
		g_ulwDirtyRectPresenter.Reset();
		g_directCompositionPresenter.Reset();
		g_dwmBlurBehindPresenter.Reset();
		g_dwmBlurBehind2Presenter.Reset();
		g_presentFailureLogged = false;
	}

	void ReleaseTransparentPipelineAttempt(CComPtr<IDXGISwapChain1>& swapChain)
	{
		ResetTransparentPresenters();
		inkRenderer.ReleaseResources();
		swapChain.Release();
	}

	bool ConfigureWindowForTransparentMode(TransparentPresentMode mode, HWND hwnd)
	{
		switch (mode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			return EnsureUlwWindowStyle(hwnd);
		case TransparentPresentMode::DirectCompositionVisualTree:
			return EnsureBorderlessTransparentWindowStyle(hwnd, true, false);
		case TransparentPresentMode::DwmBlurBehind:
			return EnsureBorderlessTransparentWindowStyle(hwnd, false, false);
		case TransparentPresentMode::DwmBlurBehind2:
			return EnsureBorderlessTransparentWindowStyle(hwnd, false, false);
		default:
			return false;
		}
	}

	int BuildTransparentPresentFallbackModes(TransparentPresentMode preferredMode, TransparentPresentMode modes[4])
	{
		switch (preferredMode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			modes[0] = TransparentPresentMode::UlwDirtyRect;
			return 1;
		case TransparentPresentMode::DwmBlurBehind:
			modes[0] = TransparentPresentMode::DwmBlurBehind;
			modes[1] = TransparentPresentMode::UlwDirtyRect;
			return 2;
		case TransparentPresentMode::DwmBlurBehind2:
			modes[0] = TransparentPresentMode::DwmBlurBehind2;
			modes[1] = TransparentPresentMode::UlwDirtyRect;
			return 2;
		case TransparentPresentMode::DirectCompositionVisualTree:
		default:
			modes[0] = TransparentPresentMode::DirectCompositionVisualTree;
			modes[1] = TransparentPresentMode::DwmBlurBehind2;
			modes[2] = TransparentPresentMode::UlwDirtyRect;
			return 3;
		}
	}

	bool CreateSwapChainForTransparentMode(
		TransparentPresentMode mode,
		IDXGIFactory2* dxgiFactory,
		ID3D11Device* device,
		HWND hwnd,
		UINT width,
		UINT height,
		CComPtr<IDXGISwapChain1>& outSwapChain)
	{
		outSwapChain.Release();
		if (!dxgiFactory || !device || !hwnd || width == 0 || height == 0) return false;

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = width;
		swapChainDesc.Height = height;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.AlphaMode = IsGpuTransparentCompositionMode(mode) ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = 0;

		HRESULT hr = S_OK;
		if (IsDirectCompositionMode(mode))
		{
			hr = dxgiFactory->CreateSwapChainForComposition(
				device,
				&swapChainDesc,
				nullptr,
				&outSwapChain
			);
			if (FAILED(hr) || !outSwapChain)
			{
				LogHresult("DirectCompositionVisualTree CreateSwapChainForComposition", hr);
				return false;
			}
			return true;
		}

		hr = dxgiFactory->CreateSwapChainForHwnd(
			device,
			hwnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			&outSwapChain
		);
		if (FAILED(hr) && IsDwmGlassMode(mode))
		{
			cout << TransparentPresentModeName(mode)
				<< " CreateSwapChainForHwnd premultiplied alpha failed. HRESULT=0x"
				<< hex << static_cast<unsigned long>(hr) << dec << endl;
			if (!IsDwmGlassTransparencyAvailable(mode))
			{
				outSwapChain.Release();
				return false;
			}
			cout << "[" << TransparentPresentModeName(mode)
				<< "] Retry CreateSwapChainForHwnd with unspecified alpha mode for Win7 Aero glass path." << endl;
			outSwapChain.Release();
			swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			hr = dxgiFactory->CreateSwapChainForHwnd(
				device,
				hwnd,
				&swapChainDesc,
				nullptr,
				nullptr,
				&outSwapChain
			);
		}
		if (FAILED(hr) || !outSwapChain)
		{
			if (IsDwmGlassMode(mode))
			{
				cout << TransparentPresentModeName(mode) << " CreateSwapChainForHwnd failed. HRESULT=0x"
					<< hex << static_cast<unsigned long>(hr) << dec << endl;
			}
			else
			{
				LogHresult("UlwDirtyRect CreateSwapChainForHwnd", hr);
			}
			return false;
		}
		return true;
	}

	bool InitializeTransparentPresenter(
		HWND hwnd,
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		IDXGIDevice* dxgiDevice,
		IDXGISwapChain1* swapChain,
		UINT width,
		UINT height)
	{
		switch (g_activeTransparentPresentMode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			return g_ulwDirtyRectPresenter.Initialize(hwnd, device, context, width, height);
		case TransparentPresentMode::DirectCompositionVisualTree:
			(void)device;
			(void)context;
			return g_directCompositionPresenter.Initialize(hwnd, dxgiDevice, swapChain, width, height);
		case TransparentPresentMode::DwmBlurBehind:
			(void)device;
			(void)context;
			(void)dxgiDevice;
			(void)swapChain;
			return g_dwmBlurBehindPresenter.Initialize(hwnd, width, height);
		case TransparentPresentMode::DwmBlurBehind2:
			(void)device;
			(void)context;
			(void)dxgiDevice;
			(void)swapChain;
			return g_dwmBlurBehind2Presenter.Initialize(hwnd, width, height);
		default:
			return false;
		}
	}

	bool TryInitializeTransparentPipeline(
		TransparentPresentMode mode,
		HWND hwnd,
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		IDXGIDevice* dxgiDevice,
		IDXGIFactory2* dxgiFactory,
		UINT width,
		UINT height,
		CComPtr<IDXGISwapChain1>& swapChain)
	{
		ReleaseTransparentPipelineAttempt(swapChain);
		g_activeTransparentPresentMode = mode;
		cout << "Trying transparent present mode: " << TransparentPresentModeName(mode) << endl;

		if (!ConfigureWindowForTransparentMode(mode, hwnd))
		{
			cout << "[" << TransparentPresentModeName(mode) << "] Configure window style failed." << endl;
			return false;
		}

		if (!CreateSwapChainForTransparentMode(mode, dxgiFactory, device, hwnd, width, height, swapChain))
		{
			cout << "[" << TransparentPresentModeName(mode) << "] Create swapchain failed." << endl;
			return false;
		}

		inkRenderer.SetWindowBackgroundColor(GetWindowBackgroundColorForMode(mode));
		if (!inkRenderer.Init(device, context, swapChain, width, height))
		{
			cout << "[" << TransparentPresentModeName(mode) << "] InkRenderer::Init failed." << endl;
			return false;
		}

		if (!InitializeTransparentPresenter(hwnd, device, context, dxgiDevice, swapChain, width, height))
		{
			cout << "[" << TransparentPresentModeName(mode) << "] Initialize transparent presenter failed." << endl;
			return false;
		}

		cout << "Active transparent present mode: " << TransparentPresentModeName(mode) << endl;
		return true;
	}

	bool InitializeTransparentPipelineWithFallback(
		HWND hwnd,
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		IDXGIDevice* dxgiDevice,
		IDXGIFactory2* dxgiFactory,
		UINT width,
		UINT height,
		CComPtr<IDXGISwapChain1>& swapChain)
	{
		TransparentPresentMode modes[4] = {};
		const int modeCount = BuildTransparentPresentFallbackModes(kPreferredTransparentPresentMode, modes);
		for (int i = 0; i < modeCount; ++i)
		{
			if (TryInitializeTransparentPipeline(
				modes[i],
				hwnd,
				device,
				context,
				dxgiDevice,
				dxgiFactory,
				width,
				height,
				swapChain))
			{
				return true;
			}

			ReleaseTransparentPipelineAttempt(swapChain);
			if (i + 1 < modeCount)
			{
				cout << "Transparent present mode " << TransparentPresentModeName(modes[i])
					<< " failed; fallback to " << TransparentPresentModeName(modes[i + 1]) << "." << endl;
			}
		}

		cout << "All transparent present modes failed." << endl;
		return false;
	}

	bool ResizeTransparentPresenter(UINT width, UINT height)
	{
		bool result = false;
		switch (g_activeTransparentPresentMode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			result = g_ulwDirtyRectPresenter.Resize(width, height);
			break;
		case TransparentPresentMode::DirectCompositionVisualTree:
			result = g_directCompositionPresenter.Resize(width, height);
			break;
		case TransparentPresentMode::DwmBlurBehind:
			result = g_dwmBlurBehindPresenter.Resize(width, height);
			break;
		case TransparentPresentMode::DwmBlurBehind2:
			result = g_dwmBlurBehind2Presenter.Resize(width, height);
			break;
		default:
			result = false;
			break;
		}
		if (!result)
		{
			cout << "ResizeTransparentPresenter failed in mode "
				<< TransparentPresentModeName(g_activeTransparentPresentMode) << endl;
		}
		return result;
	}

	bool PresentTransparentFrame(IDXGISwapChain1* swapChain, RECT dirty, bool presentFull)
	{
		bool result = false;
		switch (g_activeTransparentPresentMode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			result = g_ulwDirtyRectPresenter.Present(inkRenderer.backBufferTexture, dirty, presentFull);
			break;
		case TransparentPresentMode::DirectCompositionVisualTree:
			result = g_directCompositionPresenter.Present(swapChain, dirty, presentFull);
			break;
		case TransparentPresentMode::DwmBlurBehind:
			result = g_dwmBlurBehindPresenter.Present(swapChain, dirty, presentFull);
			break;
		case TransparentPresentMode::DwmBlurBehind2:
			result = g_dwmBlurBehind2Presenter.Present(swapChain, dirty, presentFull);
			break;
		default:
			result = false;
			break;
		}
		if (!result)
		{
			if (!g_presentFailureLogged)
			{
				cout << "PresentTransparentFrame failed in mode "
					<< TransparentPresentModeName(g_activeTransparentPresentMode)
					<< "; request full present on next frame." << endl;
				g_presentFailureLogged = true;
			}
			g_fullPresentRequested.store(true, std::memory_order_release);
		}
		else
		{
			g_presentFailureLogged = false;
		}
		return result;
	}

	void RefreshDwmBlurBehindAfterCompositionChanged()
	{
		if (IsDwmBlurBehindMode())
		{
			g_dwmBlurBehindPresenter.UpdateDwmBlurBehind();
			g_fullPresentRequested.store(true, std::memory_order_release);
		}
		if (IsDwmBlurBehind2Mode())
		{
			g_dwmBlurBehind2Presenter.UpdateExtendedGlassFrame();
			g_fullPresentRequested.store(true, std::memory_order_release);
		}
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
	const int logicFps = (workMs > 0.001) ? static_cast<int>(1000.0 / workMs) : 0;
	const int realFps = (previousFrameMs > 0.001) ? static_cast<int>(1000.0 / previousFrameMs) : 0;
	char buffer[256];
	// 输出含义：
	// commit 已烘干到 L1 的真实点索引；work 当前帧绘制/模型耗时；
	// logic 当前帧性能 FPS，不包含 HighPrecisionWait 等待时间；
	// prev-real 上一整帧真实 FPS/耗时，包含等待和控制台输出；
	// realPts/predPts/l0Pts 分别是真实点、预测点、当前 L0 绘制点数；frozen 表示停笔稳定后是否冻结输入。
	const int lineLength = std::snprintf(
		buffer,
		sizeof(buffer),
		"commit:%zu work:%.3fms logic:%d FPS prev-real:%d FPS(%.3fms) realPts:%zu predPts:%zu l0Pts:%zu frozen:%d\r\n",
		committedIndex,
		workMs,
		logicFps,
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

LRESULT CALLBACK Draw3WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		g_exitRequested.store(true, std::memory_order_release);
		break;

	case WM_DWMCOMPOSITIONCHANGED:
		RefreshDwmBlurBehindAfterCompositionChanged();
		return 0;

	case WM_ERASEBKGND:
		if (IsGpuTransparentCompositionMode()) return 1;
		break;

	case WM_PAINT:
		if (IsGpuTransparentCompositionMode())
		{
			// 新暴露区域可能没有 redirection surface 内容，交给主循环全量重提交一次。
			g_fullPresentRequested.store(true, std::memory_order_release);
			ValidateRect(hWnd, nullptr);
			return 0;
		}
		break;

	case WM_SHOWWINDOW:
	case WM_ACTIVATE:
		if (IsGpuTransparentCompositionMode())
		{
			g_fullPresentRequested.store(true, std::memory_order_release);
		}
		break;

	case WM_WINDOWPOSCHANGED:
		if (IsGpuTransparentCompositionMode())
		{
			const WINDOWPOS* windowPos = reinterpret_cast<const WINDOWPOS*>(lParam);
			if (!windowPos || ((windowPos->flags & SWP_NOMOVE) == 0) || ((windowPos->flags & SWP_NOSIZE) == 0))
			{
				g_fullPresentRequested.store(true, std::memory_order_release);
			}
		}
		break;

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
			if (IsGpuTransparentCompositionMode())
			{
				g_fullPresentRequested.store(true, std::memory_order_release);
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

		case '9':
		case VK_NUMPAD9:
			g_exitRequested.store(true, std::memory_order_release);
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
		const RECT monitorRect = GetPrimaryMonitorRect();
		windowInfo.w = static_cast<int>(monitorRect.right - monitorRect.left);
		windowInfo.h = static_cast<int>(monitorRect.bottom - monitorRect.top);
		hiex::PreSetWindowStyle(WS_POPUP);
		hiex::PreSetWindowPos(monitorRect.left, monitorRect.top);
		if (IsDirectCompositionMode(kPreferredTransparentPresentMode) && IsDirectCompositionApiAvailable())
		{
			// 只有确认 DComp API 存在才预置此样式；Win7 会在 CreateWindowEx 阶段因该样式返回 87。
			hiex::PreSetWindowStyleEx(WS_EX_NOREDIRECTIONBITMAP);
		}
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

	CComPtr<IDXGIAdapter> dxgiAdapter;
	HRESULT hr = dxgiDevice1->GetAdapter(&dxgiAdapter);
	if (FAILED(hr) || !dxgiAdapter)
	{
		LogHresult("IDXGIDevice1::GetAdapter", hr);
		return -1;
	}

	CComPtr<IDXGIFactory2> dxgiFactory;
	hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
	if (FAILED(hr) || !dxgiFactory)
	{
		LogHresult("IDXGIAdapter::GetParent(IDXGIFactory2)", hr);
		return -1;
	}

	CComPtr<IDXGISwapChain1> swapChain;
	if (!InitializeTransparentPipelineWithFallback(
		windowHWND,
		d3dDevice_HARDWARE,
		d3dDeviceContext,
		dxgiDevice1,
		dxgiFactory,
		static_cast<UINT>(windowInfo.w),
		static_cast<UINT>(windowInfo.h),
		swapChain))
	{
		cout << "Failed to initialize any transparent present pipeline." << endl;
		return -1;
	}

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
			const RECT fullCanvasRect = GetFullCanvasRect();
			inkRenderer.ClearRTV(inkRenderer.layerL2RTV, GetActiveWindowBackgroundColor());
			inkRenderer.ClearRTV(inkRenderer.layerL1RTV, kTransparentLayerClearColor);
			inkRenderer.ClearRTV(inkRenderer.layerL0RTV, kTransparentLayerClearColor);
			inkRenderer.ClearRTV(inkRenderer.backBufferRTV, GetActiveWindowBackgroundColor());
			CompositeLayersToBackBuffer(fullCanvasRect);
			PresentTransparentFrame(swapChain, fullCanvasRect, true);
		};

	auto presentFullCanvas = [&swapChain]()
		{
			const RECT fullCanvasRect = GetFullCanvasRect();
			CompositeLayersToBackBuffer(fullCanvasRect);
			PresentTransparentFrame(swapChain, fullCanvasRect, true);
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
			if (!ResizeTransparentPresenter(static_cast<UINT>(width), static_cast<UINT>(height)))
			{
				cout << "Failed to resize transparent presenter to " << width << "x" << height << endl;
				windowInfo.w = oldWidth;
				windowInfo.h = oldHeight;
				return false;
			}

			// resize 后窗口逻辑尺寸立即跟随，新区域由 L2 的当前模式背景色和透明 L1/L0 重新合成。
			windowInfo.w = width;
			windowInfo.h = height;
			if (presentAfterResize) presentFullCanvas();
			return true;
		};

	clearCanvas();

	ExMessage m{};
	while (!g_exitRequested.load(std::memory_order_acquire))
	{
		if (g_clearCanvasRequested.exchange(false, std::memory_order_relaxed))
		{
			clearCanvas();
		}
		processPendingResize(true);
		if (g_fullPresentRequested.exchange(false, std::memory_order_acquire))
		{
			presentFullCanvas();
		}

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
					PresentTransparentFrame(swapChain, compositeRect, isFirstFrame);
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
				PresentTransparentFrame(swapChain, finalPresentRect, isFirstFrame);
			}
			else
			{
				inkRenderer.ClearRTV(inkRenderer.layerL0RTV, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));
				if (isFirstFrame) presentFullCanvas();
			}

			hiex::flushmessage_win32(EM_MOUSE, windowHWND);
		}
	}

	if (!g_exitRequested.load(std::memory_order_acquire))
	{
		getmessage(EM_KEY);
	}
	return 0;
}
