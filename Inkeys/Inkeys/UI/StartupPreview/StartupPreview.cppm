module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <limits>
#include "../StartupPreview.Effect.h"

#include <chrono>
#include <cstdint>
#include <functional>

export module Inkeys.UI.StartupPreview;
export import Inkeys.Startup.Progress;
export import Inkeys.UI.StartupPreview.State;
import Inkeys.UI.Bar.Metrics;
export import Inkeys.UI.StartupPlacement;

using namespace Inkeys::UI::UI3;

export namespace Inkeys::UI::StartupPreview
{
	inline constexpr double StartupBarCornerRadiusDip =
		DefaultStartupBarCornerRadiusDip;
	inline constexpr double StartupPreviewSurfaceColorChannel =
		static_cast<double>(DarkSurfaceColorChannel) / 255.0;
	inline constexpr double StartupPreviewSurfaceFrameColorChannel =
		static_cast<double>(DarkSurfaceFrameColorChannel) / 255.0;
	inline constexpr double StartupPreviewFillAlpha = BarMainBarFillOpacity;
	inline constexpr double StartupPreviewFloatingFillAlpha = BarMainButtonCollapsedFillOpacity;
	inline constexpr double StartupPreviewFrameAlpha = BarMainBarFrameOpacity;

	struct StartOptions final
	{
		bool enabled = StartupPreviewEnabledByDefault;
		StartupPreviewEffect effect = StartupPreviewEffect::Shimmer;
		int themeMode = 1;
		int backgroundOpacity = 0;
		int contentOpacity = 100;
		StartupPlacement startupPlacement = StartupPlacement::CenteredBottomBar;
		HINSTANCE instance = nullptr;
		double cachedWidthDip = DefaultCachedStartupBarWidthDip;
		// 小尺寸：预览高度与折叠外形 42.5 DIP、圆角 8（cachedWidthDip 由调用方取小尺寸缓存）。
		bool compact = false;
		double barZoom = 1.0;
		double uiBaseScale = 0.0;
		double desktopDockLine = std::numeric_limits<double>::quiet_NaN();
		UINT dpi = USER_DEFAULT_SCREEN_DPI;
		RECT monitorBounds{};
		RECT workArea{};
		RECT contentBounds{};
		Inkeys::Startup::ProgressTracker* progress = nullptr;
		std::function<void(std::uint8_t)> requestBarAlpha;
	};

	struct Diagnostics final
	{
		double totalWidthDip = DefaultCachedStartupBarWidthDip;
		bool active = false;
		bool firstFrameCommitted = false;
		bool previewFadeOutCommitted = false;
		bool barAlpha0Committed = false;
		bool barAlpha255Committed = false;
		bool automaticStopPosted = false;
		bool ownerThreadExited = false;
		bool previewInactive = true;
	};

	[[nodiscard]] RECT ResolveStartupPreviewBounds(const RECT& monitorBounds,
		const RECT& workArea, double widthDip, double heightDip,
		UINT dpi, double zoom,
		StartupPlacement placement = StartupPlacement::CenteredBottomBar,
		double uiBaseScale = 0.0,
		double desktopDockLine = std::numeric_limits<double>::quiet_NaN(),
		double foldedSideDip = 80.0) noexcept;

	[[nodiscard]] bool Start(const StartOptions& options);
	void Stop() noexcept;
	[[nodiscard]] bool IsActive() noexcept;
	[[nodiscard]] bool IsPresentationAvailable() noexcept;
	[[nodiscard]] bool ShouldBarStartTransparent() noexcept;
	void RevalidateTopmost() noexcept;
	// compact：首帧所在的界面尺寸档位，与宽度同一条记录发布，主线程据此选择写回字段。
	void NotifyBarFirstCommittedFrame(double mainButtonTargetWidthDip,
		double layoutTotalWidthDip, bool expanded, bool compact = false) noexcept;
	[[nodiscard]] bool TakeCommittedStartupBarWidthDip(double& widthDip, bool* compact = nullptr) noexcept;
	void SetBarStartupState(BarStartupState state) noexcept;
	[[nodiscard]] BarStartupState GetBarStartupState() noexcept;
	void NotifyBarPresentationAlphaCommitted(std::uint8_t alpha) noexcept;
	void RequestFailureFrame() noexcept;
	[[nodiscard]] bool WaitForFailureFrame(
		std::chrono::milliseconds timeout) noexcept;
	void RequestFadeOutForExit() noexcept;
	[[nodiscard]] bool WaitForFadeOut(
		std::chrono::milliseconds timeout) noexcept;
	[[nodiscard]] Diagnostics SnapshotDiagnostics() noexcept;
}
