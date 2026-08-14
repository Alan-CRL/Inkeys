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
	inline constexpr float TitleBarHeightDip = 32.0F;
	inline constexpr float TitleBarIconSizeDip = 16.0F;
	inline constexpr float TitleBarHorizontalInsetDip = 16.0F;
	inline constexpr float TitleBarContentSpacingDip = 12.0F;
	inline constexpr float TitleBarRightHeaderSpacingDip = 8.0F;
	inline constexpr float TitleBarVersionPaddingDip = 12.0F;
	inline constexpr float TitleBarMinimumDragWidthDip = 96.0F;
	inline constexpr float PageMaximumWidthDip = 920.0F;

	struct LayoutRect
	{
		float left = 0.0F;
		float top = 0.0F;
		float right = 0.0F;
		float bottom = 0.0F;

		[[nodiscard]] float Width() const noexcept { return right - left; }
		[[nodiscard]] float Height() const noexcept { return bottom - top; }
		[[nodiscard]] bool Contains(float x, float y) const noexcept
		{
			return x >= left && x < right && y >= top && y < bottom;
		}
	};

	struct TitleBarGeometry
	{
		LayoutRect icon;
		LayoutRect identity;
		LayoutRect drag;
		LayoutRect version;
		LayoutRect minimize;
		LayoutRect maximize;
		LayoutRect close;
		float height = 0.0F;
		bool versionVisible = false;
	};

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

	[[nodiscard]] inline TitleBarGeometry ResolveTitleBarGeometry(
		float clientWidthPixels, float effectiveScale,
		float captionButtonWidthPixels, float titleTextWidthPixels,
		float versionTextWidthPixels) noexcept
	{
		const float scale = std::isfinite(effectiveScale) && effectiveScale > 0.0F
			? effectiveScale : 1.0F;
		const float width = std::isfinite(clientWidthPixels)
			? (std::max)(0.0F, clientWidthPixels) : 0.0F;
		const float height = TitleBarHeightDip * scale;
		const float captionWidth = std::isfinite(captionButtonWidthPixels)
			? (std::max)(height, captionButtonWidthPixels) : height;
		const float captionStart = (std::max)(0.0F, width - captionWidth * 3.0F);
		const float inset = TitleBarHorizontalInsetDip * scale;
		const float iconSize = TitleBarIconSizeDip * scale;
		const float spacing = TitleBarContentSpacingDip * scale;
		const float rightHeaderSpacing = TitleBarRightHeaderSpacingDip * scale;
		const float minimumDragWidth = TitleBarMinimumDragWidthDip * scale;
		const float titleWidth = std::isfinite(titleTextWidthPixels)
			? (std::max)(0.0F, titleTextWidthPixels) : 0.0F;
		const float versionWidth = (std::isfinite(versionTextWidthPixels)
			? (std::max)(0.0F, versionTextWidthPixels) : 0.0F)
			+ TitleBarVersionPaddingDip * scale * 2.0F;

		TitleBarGeometry result;
		result.height = height;
		result.close = { width - captionWidth, 0.0F, width, height };
		result.maximize = { width - captionWidth * 2.0F, 0.0F,
			width - captionWidth, height };
		result.minimize = { captionStart, 0.0F,
			width - captionWidth * 2.0F, height };
		result.icon = { inset, (height - iconSize) * 0.5F,
			inset + iconSize, (height + iconSize) * 0.5F };

		const float identityLeft = result.icon.right + spacing;
		const float rightHeaderEnd = (std::max)(identityLeft,
			captionStart - rightHeaderSpacing);
		const float identityDesiredRight = identityLeft + titleWidth;
		const float identityMaximumRight = (std::max)(identityLeft,
			rightHeaderEnd - minimumDragWidth);
		result.identity = { result.icon.left, 0.0F,
			(std::min)(identityDesiredRight, identityMaximumRight), height };

		const float dragStart = (std::max)(result.icon.right + spacing,
			result.identity.right);
		result.versionVisible = versionTextWidthPixels > 0.0F
			&& rightHeaderEnd - dragStart >= minimumDragWidth + versionWidth;
		if (result.versionVisible)
		{
			result.version = { rightHeaderEnd - versionWidth, 0.0F,
				rightHeaderEnd, height };
			result.drag = { dragStart, 0.0F, result.version.left, height };
		}
		else
		{
			result.drag = { dragStart, 0.0F, rightHeaderEnd, height };
		}
		return result;
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
