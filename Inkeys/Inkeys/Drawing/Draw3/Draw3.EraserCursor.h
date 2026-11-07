#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
#include "Draw3.SpeedEraser.h"
#include "Assets/EraserGripVisual.h"

namespace Inkeys::Drawing::Draw3
{
	struct EraserCursorAppearance
	{
		EraserShape shape = EraserShape::Circle;
		EraserCursorPattern pattern = EraserCursorPattern::Grips;
		EraserCursorTheme theme = EraserCursorTheme::Light;
		friend bool operator==(const EraserCursorAppearance&, const EraserCursorAppearance&) = default;
	};
	struct EraserCursorMip
	{
		uint32_t width = 0, height = 0;
		std::vector<uint8_t> bgra;
	};
	struct EraserCursorRaster
	{
		// 形状半尺寸不含四周各 2px 的透明采样边；quad 使用首层纹理尺寸。
		float halfWidthPx = 0, halfHeightPx = 0;
		std::vector<EraserCursorMip> levels;
	};
	struct EraserCursorRecipe
	{
		EraserCursorAppearance appearance;
		float baseShortSidePx = 32, dpiScale = 1;
		uint32_t shortSidePx = 32;
		friend bool operator==(const EraserCursorRecipe&, const EraserCursorRecipe&) = default;
	};

	inline uint32_t ResolveEraserCursorBakeShortSide(float baseShortSide, float drawShortSide,
		uint32_t previous = 0) noexcept
	{
		uint32_t size = 32;
		while (size < 1024 && baseShortSide > static_cast<float>(size)) size *= 2;
		size = (std::max)(size, (std::min)(previous, 1024u));
		while (size < 1024 && drawShortSide > static_cast<float>(size) * 1.25f) size *= 2;
		return size;
	}
	inline float ResolveEraserCursorCornerRadius(float baseShortSide, float dpiScale,
		uint32_t bakedShortSide) noexcept
	{
		if (!(baseShortSide > 0) || !std::isfinite(baseShortSide) ||
			!(dpiScale > 0) || !std::isfinite(dpiScale)) return 0;
		const float side = static_cast<float>(bakedShortSide);
		return (std::min)(6.0f * dpiScale * side / baseShortSide, side * 0.5f);
	}
	inline EraserCursorRecipe ResolveEraserCursorRecipe(EraserCursorAppearance appearance,
		float baseShortSide, float dpiScale, float drawShortSide,
		const EraserCursorRecipe* previous = nullptr) noexcept
	{
		if (!(baseShortSide > 0) || !std::isfinite(baseShortSide)) baseShortSide = 32;
		if (!(dpiScale > 0) || !std::isfinite(dpiScale)) dpiScale = 1;
		// 位置不属于配方；只有同一外观、基准和 DPI 可以继承已升高的档位。
		const bool same = previous && previous->appearance == appearance &&
			previous->baseShortSidePx == baseShortSide && previous->dpiScale == dpiScale;
		return { appearance, baseShortSide, dpiScale,
			ResolveEraserCursorBakeShortSide(baseShortSide, drawShortSide, same ? previous->shortSidePx : 0) };
	}

	namespace EraserCursorDetail
	{
		inline float Coverage(float d) noexcept { return std::clamp(0.5f - d, 0.0f, 1.0f); }
		inline float Length(float x, float y) noexcept { return std::sqrt(x * x + y * y); }
		inline float RoundRectDistance(float x, float y, float hx, float hy, float radius) noexcept
		{
			const float qx = std::abs(x) - (hx - radius), qy = std::abs(y) - (hy - radius);
			return Length((std::max)(qx, 0.0f), (std::max)(qy, 0.0f)) +
				(std::min)((std::max)(qx, qy), 0.0f) - radius;
		}
		inline float Capsule(float x, float y, float ax, float ay, float bx, float by, float radius) noexcept
		{
			const float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
			const float h = std::clamp((px * dx + py * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
			return Length(px - dx * h, py - dy * h) - radius;
		}
		inline float VerticalCapsule(float x, float y, float radius, float halfHeight) noexcept
		{
			const float span = (std::max)(halfHeight - radius, 0.0f);
			return Length(x, y - std::clamp(y, -span, span)) - radius;
		}
		inline float Star(float x, float y, float radius) noexcept
		{
			const float rounding = radius * 0.06f, rr = radius - rounding;
			const float ax = std::abs(x) + 1e-4f, ay = std::abs(y) + 1e-4f;
			const float px = std::sqrt(ax / rr), py = std::sqrt(ay / rr), u = px + py - 1.0f;
			if (u < -0.35f) return -8;
			if (u > 0.35f) return 8;
			return u / (std::max)(Length(0.5f * px / ax, 0.5f * py / ay), 1e-4f) - rounding;
		}
		inline float PatternDistance(EraserCursorPattern pattern, float x, float y, float side) noexcept
		{
			if (pattern == EraserCursorPattern::Grips)
				return (std::min)(VerticalCapsule(x + ERASER_GRIP_STRIPE_OFFSET_RATIO * side, y,
					ERASER_GRIP_STRIPE_RADIUS_RATIO * side, ERASER_GRIP_STRIPE_HALF_HEIGHT_RATIO * side),
					VerticalCapsule(x - ERASER_GRIP_STRIPE_OFFSET_RATIO * side, y,
						ERASER_GRIP_STRIPE_RADIUS_RATIO * side, ERASER_GRIP_STRIPE_HALF_HEIGHT_RATIO * side));
			if (pattern != EraserCursorPattern::Sparkle || side < 20) return 8;
			float d = Star(x - 0.15f * side, y + 0.15f * side, 0.12f * side);
			if (side >= 40) d = (std::min)(d, Star(x - 0.31f * side, y, 0.05f * side));
			d = (std::min)(d, Capsule(x, y, -0.27f * side, 0.17f * side,
				-0.13f * side, 0.03f * side, 0.026f * side));
			return (std::min)(d, Capsule(x, y, -0.16f * side, 0.26f * side,
				-0.06f * side, 0.16f * side, 0.026f * side));
		}
		inline float BandDistance(float t, float s, float t0, float t1, bool roundStart) noexcept
		{
			const float x = t - (t0 + t1) * 0.5f;
			// 两个端点分别选角半径，保留纸套间 2.5 单位的直角细缝。
			const float radius = (roundStart ? x < 0 : x >= 0) ? 5.0f : 0.0f;
			return RoundRectDistance(x, s, (t1 - t0) * 0.5f, 14.0f, radius);
		}
		inline void CompositeLogo(float x, float y, float side, EraserCursorAppearance appearance,
			float ring, float& channel, float& alpha) noexcept
		{
			if (side < 24) return;
			const float scale = (appearance.shape == EraserShape::Rectangle ? 0.54f : 0.48f) * side / 142.0f;
			const float lx = x / scale + 128.5f, ly = y / scale + 129.0f;
			const float dx = lx - 99.28f, dy = ly - 198.04f;
			const float norm2 = 0.4399f * 0.4399f + 0.8981f * 0.8981f;
			const float t = (dx * 0.4399f - dy * 0.8981f) / norm2;
			const float s = (dx * 0.8981f + dy * 0.4399f) / norm2;
			const float pen = (std::min)(BandDistance(t, s, 9, 61, true), BandDistance(t, s, 63.5f, 145, false));
			const float screen = RoundRectDistance(lx - 128.5f, ly - 127.775f, 69.5f, 50, 6);
			// 对完整屏幕扣除橡皮外扩 6 单位，再合成橡皮；屏幕与橡皮统一用描边灰，保持预乘色。
			const float screenAlpha = Coverage(screen * scale) * (1.0f - Coverage((pen - 6) * scale));
			const float penAlpha = Coverage(pen * scale);
			const float logoAlpha = penAlpha + screenAlpha * (1.0f - penAlpha);
			channel = ring * logoAlpha + channel * (1.0f - logoAlpha);
			alpha = logoAlpha + alpha * (1.0f - logoAlpha);
		}
	}

	inline EraserCursorRaster RasterizeEraserCursor(EraserCursorAppearance appearance,
		uint32_t shortSidePx, float cornerRadiusPx)
	{
		EraserCursorRaster result;
		if (shortSidePx == 0 || shortSidePx > 1024 || !std::isfinite(cornerRadiusPx)) return result;
		const float side = static_cast<float>(shortSidePx);
		result.halfWidthPx = side * 0.5f;
		result.halfHeightPx = side * (appearance.shape == EraserShape::Rectangle ? 0.75f : 0.5f);
		const float corner = std::clamp(cornerRadiusPx, 0.0f, side * 0.5f);
		EraserCursorMip level;
		level.width = static_cast<uint32_t>(std::ceil(result.halfWidthPx * 2)) + 4;
		level.height = static_cast<uint32_t>(std::ceil(result.halfHeightPx * 2)) + 4;
		level.bgra.resize(static_cast<size_t>(level.width) * level.height * 4);
		const bool dark = appearance.theme == EraserCursorTheme::Dark;
		const float fill = (dark ? 40.0f : 255.0f) / 255.0f;
		const float ring = (dark ? 110.0f : 207.0f) / 255.0f;
		// 所有纹样统一使用描边灰。
		const float mark = ring;
		for (uint32_t j = 0; j < level.height; ++j)
		for (uint32_t i = 0; i < level.width; ++i)
		{
			using namespace EraserCursorDetail;
			const float x = i + 0.5f - level.width * 0.5f, y = j + 0.5f - level.height * 0.5f;
			const float distance = appearance.shape == EraserShape::Circle ? Length(x, y) - result.halfWidthPx :
				RoundRectDistance(x, y, result.halfWidthPx, result.halfHeightPx, corner);
			const float outer = Coverage(distance), inner = Coverage(distance + side * ERASER_GRIP_OUTLINE_RATIO);
			float alpha = outer, channel = ((std::max)(0.0f, outer - inner) * ring + inner * fill);
			if (outer > 0)
			{
				float straight = channel / outer;
				const float mask = (std::min)(Coverage(PatternDistance(appearance.pattern, x, y, side)), inner);
				straight += mask * (mark - straight);
				alpha += mask * (1.0f - alpha);
				channel = straight * alpha;
			}
			if (appearance.pattern == EraserCursorPattern::Inkeys) CompositeLogo(x, y, side, appearance, ring, channel, alpha);
			const auto offset = (static_cast<size_t>(j) * level.width + i) * 4;
			const auto a = static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255));
			const auto c = static_cast<uint8_t>(std::lround(std::clamp(channel, 0.0f, alpha) * 255));
			level.bgra[offset] = level.bgra[offset + 1] = level.bgra[offset + 2] = c;
			level.bgra[offset + 3] = a;
		}
		result.levels.push_back(std::move(level));
		while (result.levels.back().width > 1 || result.levels.back().height > 1)
		{
			const auto& source = result.levels.back();
			EraserCursorMip mip;
			mip.width = (std::max)(1u, source.width / 2); mip.height = (std::max)(1u, source.height / 2);
			mip.bgra.resize(static_cast<size_t>(mip.width) * mip.height * 4);
			// 按源像素面积做盒式平均，奇数边长也保持对称且不丢边缘。
			for (uint32_t y = 0; y < mip.height; ++y)
			for (uint32_t x = 0; x < mip.width; ++x)
			{
				const double x0 = static_cast<double>(x) * source.width / mip.width;
				const double x1 = static_cast<double>(x + 1) * source.width / mip.width;
				const double y0 = static_cast<double>(y) * source.height / mip.height;
				const double y1 = static_cast<double>(y + 1) * source.height / mip.height;
				for (uint32_t c = 0; c < 4; ++c)
				{
					double sum = 0;
					for (uint32_t sy = static_cast<uint32_t>(y0); sy < static_cast<uint32_t>(std::ceil(y1)); ++sy)
					for (uint32_t sx = static_cast<uint32_t>(x0); sx < static_cast<uint32_t>(std::ceil(x1)); ++sx)
					{
						const double weight = ((std::min)(x1, static_cast<double>(sx + 1)) - (std::max)(x0, static_cast<double>(sx))) *
							((std::min)(y1, static_cast<double>(sy + 1)) - (std::max)(y0, static_cast<double>(sy)));
						sum += weight * source.bgra[(static_cast<size_t>(sy) * source.width + sx) * 4 + c];
					}
					mip.bgra[(static_cast<size_t>(y) * mip.width + x) * 4 + c] =
						static_cast<uint8_t>(std::lround(sum / ((x1 - x0) * (y1 - y0))));
				}
			}
			result.levels.push_back(std::move(mip));
		}
		return result;
	}
}
