#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "Draw3.WhiteboardModel.h"

namespace Inkeys::Drawing::Draw3
{
	// 白板点阵/网格的纯 CPU 几何，不依赖 D3D：与原 Freeze 的 DrawThemePattern 同一规则
	// （24 DIP 间距、1 DIP 点半径/线宽、18% 黑/白、以屏幕中心为相位），再加上画布视口位移。
	inline constexpr double kWhiteboardPatternStepDip = 24.0;
	inline constexpr float kWhiteboardPatternAlpha = 0.18F;
	inline constexpr float kWhiteboardPatternDotRadiusDip = 1.0F;
	inline constexpr float kWhiteboardPatternLineWidthDip = 1.0F;
	// 超出该范围的缩放视为无效，纹理不绘制（避免过密图元）。
	inline constexpr float kWhiteboardPatternMinimumDpiScale = 0.5F;
	inline constexpr float kWhiteboardPatternMaximumDpiScale = 8.0F;

	// 单帧参数：由控制器按当前页主题、视口和 DPI 生成，渲染器只消费这份值。
	struct WhiteboardPatternParams
	{
		WhiteboardPattern kind = WhiteboardPattern::Blank;
		float shade = 0.0F; // 0 = 黑（浅色背景），1 = 白（深色背景）。
		double stepPx = 0.0;
		double phaseX = 0.0; // 屏幕像素坐标下首条线/点相对 0 的偏移，范围 [0, stepPx)。
		double phaseY = 0.0;
		float dotRadiusPx = 0.0F;
		float lineHalfWidthPx = 0.0F;
		// 网格线按整幅画布长度发出，与 dirty 矩形无关：分块/局部重合成时同一序号的线逐顶点相同，
		// 光栅插值与整帧一致，scissor 只负责裁剪像素。
		int canvasWidth = 0;
		int canvasHeight = 0;

		[[nodiscard]] bool Enabled() const noexcept
		{
			return kind != WhiteboardPattern::Blank && stepPx > 0.0;
		}
	};

	// 与旧 Freeze 相同的亮度判据：浅色背景用黑纹理，其余用白纹理。
	[[nodiscard]] inline float WhiteboardPatternShade(std::uint32_t backgroundRgb) noexcept
	{
		return IsLightWhiteboardBackground(backgroundRgb) ? 0.0F : 1.0F;
	}

	// phase = (halfExtent - viewport) mod step；用 double，因为视口范围 ±1048576 px 超出 float 精度。
	[[nodiscard]] inline double WhiteboardPatternPhase(
		double halfExtentPx, double viewportPx, double stepPx) noexcept
	{
		if (!(stepPx > 0.0) || !std::isfinite(halfExtentPx) || !std::isfinite(viewportPx)) return 0.0;
		double phase = std::fmod(halfExtentPx - viewportPx, stepPx);
		if (phase < 0.0) phase += stepPx;
		if (!(phase >= 0.0) || phase >= stepPx) phase = 0.0; // 吸收 -ε + step 舍入到 step 的情形。
		return phase;
	}

	// Blank、非法主题、非法 DPI 或画布尺寸都返回 Enabled()==false。
	[[nodiscard]] inline WhiteboardPatternParams MakeWhiteboardPatternParams(
		const WhiteboardTheme& theme, double viewportX, double viewportY,
		int canvasWidth, int canvasHeight, float dpiScale) noexcept
	{
		WhiteboardPatternParams params;
		if (!ValidWhiteboardTheme(theme) || theme.pattern == WhiteboardPattern::Blank ||
			canvasWidth <= 0 || canvasHeight <= 0 || !std::isfinite(dpiScale) ||
			dpiScale < kWhiteboardPatternMinimumDpiScale || dpiScale > kWhiteboardPatternMaximumDpiScale)
			return params;
		params.kind = theme.pattern;
		params.shade = WhiteboardPatternShade(theme.backgroundRgb);
		params.stepPx = kWhiteboardPatternStepDip * static_cast<double>(dpiScale);
		params.phaseX = WhiteboardPatternPhase(canvasWidth * 0.5, viewportX, params.stepPx);
		params.phaseY = WhiteboardPatternPhase(canvasHeight * 0.5, viewportY, params.stepPx);
		params.canvasWidth = canvasWidth;
		params.canvasHeight = canvasHeight;
		params.dotRadiusPx = kWhiteboardPatternDotRadiusDip * dpiScale;
		params.lineHalfWidthPx = kWhiteboardPatternLineWidthDip * dpiScale * 0.5F;
		return params;
	}

	// 帧入口门控：只有白板工作区且主题非 Blank 才产生纹理；桌面/PPT 恒为未启用，
	// 保持画布空时真透明（SelectionUlw 零 alpha 校验依赖于此）。
	[[nodiscard]] inline WhiteboardPatternParams ResolveWhiteboardPatternForFrame(
		bool whiteboardWorkspace, const WhiteboardTheme& theme, double viewportX, double viewportY,
		int canvasWidth, int canvasHeight, float dpiScale) noexcept
	{
		if (!whiteboardWorkspace) return {};
		return MakeWhiteboardPatternParams(theme, viewportX, viewportY, canvasWidth, canvasHeight, dpiScale);
	}

	// 与相位无关的外观键：任何一项变化都需要整屏重新合成；视口平移已经自带整屏合成。
	struct WhiteboardPatternKey
	{
		WhiteboardPattern kind = WhiteboardPattern::Blank;
		float shade = 0.0F;
		double stepPx = 0.0;
		friend bool operator==(const WhiteboardPatternKey&, const WhiteboardPatternKey&) = default;
	};

	[[nodiscard]] inline WhiteboardPatternKey MakeWhiteboardPatternKey(
		const WhiteboardPatternParams& params) noexcept
	{
		if (!params.Enabled()) return {};
		return { params.kind, params.shade, params.stepPx };
	}

	// 枚举与矩形 [left,right)x[top,bottom) 相交（含 AA 边距）的全部图元。
	// emit(x0, y0, x1, y1, halfWidth)：点是 x0==x1 且 y0==y1 的零长度线（半线宽 = 点半径）；
	// 网格是贯穿整幅画布并向两侧外延的长线。图元坐标只取决于绝对序号 k 与画布尺寸，因此分块矩形与整帧矩形
	// 对同一序号产生逐位相同的图元。返回图元数。
	template <class Emit>
	inline std::size_t EnumerateWhiteboardPatternPrimitives(
		const WhiteboardPatternParams& params, int left, int top, int right, int bottom, Emit&& emit)
	{
		if (!params.Enabled() || left >= right || top >= bottom) return 0;
		const bool grid = params.kind == WhiteboardPattern::Grid;
		const double halfWidth = grid ? params.lineHalfWidthPx : params.dotRadiusPx;
		// VS 外延 halfWidth+2，再留 1 px 余量，保证 scissor 边缘外的图元不会漏掉。
		const double margin = halfWidth + 3.0;
		const double step = params.stepPx;
		const auto firstIndex = [&](double low, double phase) {
			return static_cast<long long>(std::ceil((low - margin - phase) / step));
		};
		const auto lastIndex = [&](double high, double phase) {
			return static_cast<long long>(std::floor((high + margin - phase) / step));
		};
		const long long xFirst = firstIndex(left, params.phaseX), xLast = lastIndex(right, params.phaseX);
		const long long yFirst = firstIndex(top, params.phaseY), yLast = lastIndex(bottom, params.phaseY);
		std::size_t count = 0;
		const float half = static_cast<float>(halfWidth);
		if (!grid)
		{
			for (long long ix = xFirst; ix <= xLast; ++ix)
			{
				const float x = static_cast<float>(params.phaseX + static_cast<double>(ix) * step);
				for (long long iy = yFirst; iy <= yLast; ++iy)
				{
					const float y = static_cast<float>(params.phaseY + static_cast<double>(iy) * step);
					emit(x, y, x, y, half);
					++count;
				}
			}
			return count;
		}
		// 外延 halfWidth+4：圆头与 AA 都落在画布之外。
		const float lineStartY = static_cast<float>(-halfWidth - 4.0);
		const float lineEndY = static_cast<float>(static_cast<double>(params.canvasHeight) + halfWidth + 4.0);
		const float lineStartX = static_cast<float>(-halfWidth - 4.0);
		const float lineEndX = static_cast<float>(static_cast<double>(params.canvasWidth) + halfWidth + 4.0);
		for (long long ix = xFirst; ix <= xLast; ++ix)
		{
			const float x = static_cast<float>(params.phaseX + static_cast<double>(ix) * step);
			emit(x, lineStartY, x, lineEndY, half);
			++count;
		}
		for (long long iy = yFirst; iy <= yLast; ++iy)
		{
			const float y = static_cast<float>(params.phaseY + static_cast<double>(iy) * step);
			emit(lineStartX, y, lineEndX, y, half);
			++count;
		}
		return count;
	}
}
