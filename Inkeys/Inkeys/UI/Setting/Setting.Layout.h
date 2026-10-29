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
	// Win11 默认窗口圆角半径（DWMWCP_DEFAULT），按 DPI 缩放；数值待实机确认。
	inline constexpr float WindowCornerRadiusDip = 8.0F;

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
		LayoutRect feedback;
		LayoutRect debug;
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

	// outsideResizeOnly 为 accent 平台：恢复态左/右/下不保留 sizing frame，使 HWND 矩形即可见内容，
	// accent 模糊不再溢出；缩放热区全部由窗外的 resize 边框窗口承担。
	[[nodiscard]] inline WindowFrameInsets ResolveVisibleWindowFrameInsets(
		WindowFrameInsets sizingFrame, bool maximized, bool compositionEnabled,
		int visibleBorderPixels, bool useThinTopBorder = true,
		bool outsideResizeOnly = false) noexcept
	{
		// 仅现代 DWM 收薄恢复态顶部；旧 Aero 的四边 frame 必须完整保留。
		if (!maximized && compositionEnabled && useThinTopBorder)
			sizingFrame.top = std::clamp(visibleBorderPixels, 0, sizingFrame.top);
		if (!maximized && compositionEnabled && outsideResizeOnly)
			sizingFrame.left = sizingFrame.right = sizingFrame.bottom = 0;
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

	// insideResize 为 false 时（accent 平台）HWND 内不返回任何缩放命中，热区只在窗外边框窗口。
	[[nodiscard]] inline WindowFrameHit HitTestWindowFrame(
		const WindowFrameRect& outer, const WindowFrameRect& client,
		int x, int y, bool maximized, int topResizePixels = 0,
		bool protectClientControl = false, bool insideResize = true) noexcept
	{
		if (!outer.Contains(x, y)) return WindowFrameHit::Outside;
		if (maximized || !insideResize) return WindowFrameHit::Client;
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

	// 透明 resize 边框窗口：外框为 Setting HWND 四周各扩展一个 sizing frame 宽度；
	// 区域挖掉 HWND 本身（圆角平台按圆角挖空），accent 模糊因此不会进入边框窗口。
	struct WindowResizeBorderGeometry
	{
		WindowFrameRect outer;     // 屏幕坐标。
		WindowFrameRect excluded;  // 相对 outer 左上角，即 SetWindowRgn 的坐标系。
		WindowFrameInsets band;    // 四边外侧热区宽度。
		int cornerRadius = 0;      // 挖空矩形的圆角半径；0 为直角。

		[[nodiscard]] bool Empty() const noexcept
		{
			return outer.Width() <= excluded.Width() && outer.Height() <= excluded.Height();
		}

		// 与 CreateRectRgn(outer) - CreateRoundRectRgn(excluded, 2r, 2r) 对应的像素中心判定。
		[[nodiscard]] bool Contains(int screenX, int screenY) const noexcept
		{
			if (!outer.Contains(screenX, screenY)) return false;
			const int x = screenX - outer.left;
			const int y = screenY - outer.top;
			if (!excluded.Contains(x, y)) return true;
			const int radius = (std::min)(cornerRadius,
				(std::min)(excluded.Width(), excluded.Height()) / 2);
			if (radius <= 0) return false;
			const double px = x + 0.5;
			const double py = y + 0.5;
			const double cx = px < excluded.left + radius ? excluded.left + radius
				: px > excluded.right - radius ? excluded.right - radius : px;
			const double cy = py < excluded.top + radius ? excluded.top + radius
				: py > excluded.bottom - radius ? excluded.bottom - radius : py;
			const double dx = px - cx;
			const double dy = py - cy;
			return dx * dx + dy * dy > static_cast<double>(radius) * radius;
		}
	};

	[[nodiscard]] inline WindowResizeBorderGeometry ResolveWindowResizeBorderGeometry(
		const WindowFrameRect& window, const WindowFrameInsets& thickness,
		int cornerRadius) noexcept
	{
		const WindowFrameInsets extension{ (std::max)(0, thickness.left),
			(std::max)(0, thickness.top), (std::max)(0, thickness.right),
			(std::max)(0, thickness.bottom) };
		WindowResizeBorderGeometry result;
		result.band = extension;
		result.outer = ExpandWindowFrameRect(window, extension);
		result.excluded = { extension.left, extension.top,
			extension.left + (std::max)(0, window.Width()),
			extension.top + (std::max)(0, window.Height()) };
		result.cornerRadius = std::clamp(cornerRadius, 0,
			(std::min)(result.excluded.Width(), result.excluded.Height()) / 2);
		return result;
	}

	// 边框窗口的命中：角区沿边延伸一个热区宽度（不小于圆角半径），圆角切掉的像素也归最近的角。
	[[nodiscard]] inline WindowFrameHit HitTestWindowResizeBorder(
		const WindowResizeBorderGeometry& geometry, int x, int y) noexcept
	{
		if (!geometry.Contains(x, y)) return WindowFrameHit::Outside;
		const WindowFrameRect window{ geometry.outer.left + geometry.excluded.left,
			geometry.outer.top + geometry.excluded.top,
			geometry.outer.left + geometry.excluded.right,
			geometry.outer.top + geometry.excluded.bottom };
		const bool left = x < window.left + (std::max)(geometry.cornerRadius, geometry.band.left);
		const bool right = x >= window.right - (std::max)(geometry.cornerRadius, geometry.band.right);
		const bool top = y < window.top + (std::max)(geometry.cornerRadius, geometry.band.top);
		const bool bottom = y >= window.bottom - (std::max)(geometry.cornerRadius, geometry.band.bottom);
		if (top && left) return WindowFrameHit::TopLeft;
		if (top && right) return WindowFrameHit::TopRight;
		if (bottom && left) return WindowFrameHit::BottomLeft;
		if (bottom && right) return WindowFrameHit::BottomRight;
		if (y < window.top) return WindowFrameHit::Top;
		if (y >= window.bottom) return WindowFrameHit::Bottom;
		if (x < window.left) return WindowFrameHit::Left;
		if (x >= window.right) return WindowFrameHit::Right;
		return WindowFrameHit::Outside;
	}

	enum class NavigationLayout
	{
		Open,
		Compact,
		Overlay,
	};

	[[nodiscard]] inline float DpiScale(std::uint32_t dpi) noexcept
	{
		return static_cast<float>(dpi ? dpi : 96U) / 96.0F;
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

		// 主题、反馈与调试入口始终保留，窄窗先让版本和标题文字让位。
		const float debugRight = (std::max)(0.0F, captionStart - rightHeaderSpacing);
		result.debug = { (std::max)(0.0F,
			debugRight - TitleBarThemeButtonWidthDip * scale), 0.0F, debugRight, height };
		const float feedbackRight = (std::max)(0.0F, result.debug.left - rightHeaderSpacing);
		result.feedback = { (std::max)(0.0F,
			feedbackRight - TitleBarThemeButtonWidthDip * scale), 0.0F, feedbackRight, height };
		const float themeRight = (std::max)(0.0F, result.feedback.left - rightHeaderSpacing);
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

		// 极窄物理预算先让标题/版本让位；保留三入口后，拖动区只能使用剩余空间。
		// 常规 720-DIP 最小窗仍可保留完整的 96-DIP 拖动区。
		const float dragStart = (std::max)(result.icon.right, (std::min)(
			(std::max)(identityLeft, result.identity.right), rightHeaderEnd - minimumDragWidth));
		result.identity.right = (std::min)(result.identity.right, dragStart);
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
