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
	inline constexpr float TitleBarCaptionButtonWidthDip = 46.0F;
	inline constexpr float TitleBarThemeButtonWidthDip = 32.0F;
	inline constexpr float TitleBarCaptionGlyphSizeDip = 10.0F;
	inline constexpr float TitleBarIconSizeDip = 16.0F;
	inline constexpr float TitleBarHorizontalInsetDip = 16.0F;
	inline constexpr float TitleBarContentSpacingDip = 16.0F;
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
		LayoutRect themeToggle;
		LayoutRect minimize;
		LayoutRect maximize;
		LayoutRect close;
		float height = 0.0F;
		bool versionVisible = false;
	};

	// HWND 外框和客户区共用物理像素几何；系统边框不乘设置界面的用户倍率。
	struct WindowFrameRect
	{
		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;

		[[nodiscard]] int Width() const noexcept { return right - left; }
		[[nodiscard]] int Height() const noexcept { return bottom - top; }
		[[nodiscard]] bool Contains(int x, int y) const noexcept
		{
			return x >= left && x < right && y >= top && y < bottom;
		}
	};

	struct WindowFrameInsets
	{
		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;
	};

	struct WindowFrameSize
	{
		int width = 0;
		int height = 0;
	};

	enum class WindowFrameHit
	{
		Client, Outside, Left, Right, Top, Bottom,
		TopLeft, TopRight, BottomLeft, BottomRight,
	};

	[[nodiscard]] inline WindowFrameInsets ResolveVisibleWindowFrameInsets(
		WindowFrameInsets sizingFrame, bool maximized, bool compositionEnabled,
		int visibleBorderPixels) noexcept
	{
		// DWM 的顶部没有左右/下边那段不可见外扩；恢复态只保留实际可见描边。
		if (!maximized && compositionEnabled)
			sizingFrame.top = std::clamp(visibleBorderPixels, 0, sizingFrame.top);
		return sizingFrame;
	}

	[[nodiscard]] inline WindowFrameRect InsetWindowFrameRect(
		const WindowFrameRect& outer, const WindowFrameInsets& frame) noexcept
	{
		return { outer.left + frame.left, outer.top + frame.top,
			outer.right - frame.right, outer.bottom - frame.bottom };
	}

	[[nodiscard]] inline WindowFrameRect ExpandWindowFrameRect(
		const WindowFrameRect& client, const WindowFrameInsets& frame) noexcept
	{
		return { client.left - frame.left, client.top - frame.top,
			client.right + frame.right, client.bottom + frame.bottom };
	}

	[[nodiscard]] inline WindowFrameSize ResolveMinimumWindowFrame(
		int clientWidth, int clientHeight, const WindowFrameInsets& frame,
		int workWidth, int workHeight) noexcept
	{
		const auto outer = ExpandWindowFrameRect(
			{ 0, 0, (std::max)(1, clientWidth), (std::max)(1, clientHeight) }, frame);
		return {
			workWidth > 0 ? (std::min)(outer.Width(), workWidth) : outer.Width(),
			workHeight > 0 ? (std::min)(outer.Height(), workHeight) : outer.Height() };
	}

	[[nodiscard]] inline WindowFrameRect ResolveWindowFrameCreationCorrection(
		const WindowFrameRect& requestedClient, const WindowFrameRect& measuredOuter,
		const WindowFrameSize& measuredClient, const WindowFrameRect& workArea) noexcept
	{
		// 用已创建 HWND 的实际 frame 差值校正，避免创建前估算与 DWM 描边差一像素。
		int width = (std::max)(1, measuredOuter.Width() + requestedClient.Width() - measuredClient.width);
		int height = (std::max)(1, measuredOuter.Height() + requestedClient.Height() - measuredClient.height);
		const bool hasWorkArea = workArea.Width() > 0 && workArea.Height() > 0;
		if (hasWorkArea)
		{
			width = (std::min)(width, workArea.Width());
			height = (std::min)(height, workArea.Height());
		}
		if (width == measuredOuter.Width() && height == measuredOuter.Height())
			return measuredOuter;
		int left = requestedClient.left + requestedClient.Width() / 2 - width / 2;
		int top = requestedClient.top + requestedClient.Height() / 2 - height / 2;
		if (hasWorkArea)
		{
			left = std::clamp(left, workArea.left, workArea.right - width);
			top = std::clamp(top, workArea.top, workArea.bottom - height);
		}
		return { left, top, left + width, top + height };
	}

	// MINMAXINFO 的最大化字段由 USER32 预填并按目标显示器补偿，只改最小跟踪尺寸。
	// 模板保持本几何头不依赖 windows.h，生产与测试均传入真实 MINMAXINFO。
	template<class MinMaxInfo>
	inline void ApplyWindowFrameMinimumTrack(MinMaxInfo& limits,
		const WindowFrameSize& minimum) noexcept
	{
		limits.ptMinTrackSize.x = minimum.width;
		limits.ptMinTrackSize.y = minimum.height;
	}

	[[nodiscard]] inline WindowFrameHit HitTestWindowFrame(
		const WindowFrameRect& outer, const WindowFrameRect& client,
		int x, int y, bool maximized, int topResizePixels = 0,
		bool protectClientControl = false) noexcept
	{
		if (!outer.Contains(x, y)) return WindowFrameHit::Outside;
		if (maximized) return WindowFrameHit::Client;
		// 仅空白 caption 顶部可补足缩放高度；按钮和其余客户区始终优先。
		const int topLimit = (std::max)(client.top, outer.top + topResizePixels);
		if (client.Contains(x, y) && (protectClientControl || y >= topLimit))
			return WindowFrameHit::Client;
		const bool left = x < client.left;
		const bool right = x >= client.right;
		const bool top = y < topLimit;
		const bool bottom = y >= client.bottom;
		if (top && left) return WindowFrameHit::TopLeft;
		if (top && right) return WindowFrameHit::TopRight;
		if (bottom && left) return WindowFrameHit::BottomLeft;
		if (bottom && right) return WindowFrameHit::BottomRight;
		if (top) return WindowFrameHit::Top;
		if (bottom) return WindowFrameHit::Bottom;
		if (left) return WindowFrameHit::Left;
		if (right) return WindowFrameHit::Right;
		return WindowFrameHit::Client;
	}

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
			? (std::max)(1.0F, captionButtonWidthPixels)
			: TitleBarCaptionButtonWidthDip * scale;
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

		// 主题入口始终保留，窗口变窄时先让版本信息和标题文字让位。
		const float themeRight = (std::max)(0.0F, captionStart - rightHeaderSpacing);
		result.themeToggle = { (std::max)(0.0F,
			themeRight - TitleBarThemeButtonWidthDip * scale), 0.0F, themeRight, height };
		const float identityLeft = result.icon.right + spacing;
		const float rightHeaderEnd = (std::max)(identityLeft,
			result.themeToggle.left - rightHeaderSpacing);
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
