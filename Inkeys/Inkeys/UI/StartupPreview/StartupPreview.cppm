module;

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <functional>

export module Inkeys.UI.StartupPreview;
export import Inkeys.Startup.Progress;
export import Inkeys.UI.StartupPreview.State;
import Inkeys.UI.Bar.Metrics;

export namespace Inkeys::UI::StartupPreview
{
	inline constexpr double StartupBarCornerRadiusDip =
		DefaultStartupBarCornerRadiusDip;
	inline constexpr double StartupPreviewSurfaceColorChannel =
		static_cast<double>(BarDarkSurfaceColorChannel) / 255.0;
	inline constexpr double StartupPreviewSurfaceFrameColorChannel =
		static_cast<double>(BarDarkSurfaceFrameColorChannel) / 255.0;
	inline constexpr double StartupPreviewFillAlpha = BarMainBarFillOpacity;
	inline constexpr double StartupPreviewFrameAlpha = BarMainBarFrameOpacity;

	struct StartOptions final
	{
		bool enabled = StartupPreviewEnabledByDefault;
		HINSTANCE instance = nullptr;
		double cachedWidthDip = DefaultCachedStartupBarWidthDip;
		double barZoom = 1.0;
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
		UINT dpi, double zoom) noexcept;

	[[nodiscard]] bool Start(const StartOptions& options);
	void Stop() noexcept;
	[[nodiscard]] bool IsActive() noexcept;
	[[nodiscard]] bool IsPresentationAvailable() noexcept;
	[[nodiscard]] bool ShouldBarStartTransparent() noexcept;
	void RevalidateTopmost() noexcept;
	void NotifyBarFirstCommittedFrame(double mainButtonTargetWidthDip,
		double layoutTotalWidthDip, bool expanded) noexcept;
	[[nodiscard]] bool TakeCommittedStartupBarWidthDip(double& widthDip) noexcept;
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
