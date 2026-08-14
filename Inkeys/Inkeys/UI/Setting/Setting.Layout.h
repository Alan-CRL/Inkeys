#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Inkeys::UI::Setting
{
	inline constexpr float DefaultWidthDip = 960.0F;
	inline constexpr float DefaultHeightDip = 700.0F;
	inline constexpr float MinimumWidthDip = 720.0F;
	inline constexpr float MinimumHeightDip = 520.0F;
	inline constexpr float TitleBarHeightDip = 40.0F;
	inline constexpr float PageMaximumWidthDip = 920.0F;

	enum class NavigationLayout
	{
		Open,
		Compact,
		Overlay,
	};

	[[nodiscard]] inline float NormalizeUserScale(float scale) noexcept
	{
		if (!std::isfinite(scale)) return 1.0F;
		return std::clamp(scale, 1.0F, 2.0F);
	}

	[[nodiscard]] inline float DpiScale(std::uint32_t dpi) noexcept
	{
		return static_cast<float>(dpi ? dpi : 96U) / 96.0F;
	}

	[[nodiscard]] inline float EffectiveScale(
		std::uint32_t dpi, float userScale) noexcept
	{
		return DpiScale(dpi) * NormalizeUserScale(userScale);
	}

	[[nodiscard]] inline int ScaleDip(float dip, float effectiveScale) noexcept
	{
		return static_cast<int>(std::lround(dip * effectiveScale));
	}

	[[nodiscard]] inline int ResolveWindowExtent(
		float dip, float effectiveScale, int workAreaPixels) noexcept
	{
		const int scaledValue = ScaleDip(dip, effectiveScale);
		const int scaled = scaledValue > 1 ? scaledValue : 1;
		return workAreaPixels > 0 && workAreaPixels < scaled
			? workAreaPixels : scaled;
	}

	[[nodiscard]] inline NavigationLayout ResolveNavigationLayout(
		float clientWidthPixels, float effectiveScale) noexcept
	{
		const float widthDip = effectiveScale > 0.0F
			? clientWidthPixels / effectiveScale : clientWidthPixels;
		if (widthDip >= 900.0F) return NavigationLayout::Open;
		if (widthDip >= 760.0F) return NavigationLayout::Compact;
		return NavigationLayout::Overlay;
	}

	[[nodiscard]] inline float ResolvePageWidth(
		float availablePixels, float effectiveScale) noexcept
	{
		if (!std::isfinite(availablePixels) || availablePixels <= 0.0F)
			return 0.0F;
		const float scale = std::isfinite(effectiveScale) && effectiveScale > 0.0F
			? effectiveScale : 1.0F;
		const float maximumPixels = PageMaximumWidthDip * scale;
		return availablePixels < maximumPixels ? availablePixels : maximumPixels;
	}

	[[nodiscard]] inline float ResolvePageTransitionProgress(
		float elapsedSeconds, float durationSeconds = 0.16F) noexcept
	{
		if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0F)
			return 1.0F;
		if (!std::isfinite(elapsedSeconds))
			return elapsedSeconds > 0.0F ? 1.0F : 0.0F;
		return std::clamp(elapsedSeconds / durationSeconds, 0.0F, 1.0F);
	}
}
