module;

#include <windows.h>

#include <d2d1_1.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module Inkeys.UI.StartupPreview;
export import Inkeys.Startup.Progress;
export import Inkeys.UI.StartupPreview.Format;

export namespace Inkeys::UI::StartupPreview
{
	using Microsoft::WRL::ComPtr;

	struct CommittedBarFrame final
	{
		std::uint64_t deviceGeneration = 0;
		std::uint64_t visualRevision = 0;
		bool stableForCache = false;
		D2D1_RECT_U sourceCrop{};
		RECT viewport{};
		RECT screenDestination{};
		RECT monitorBounds{};
		RECT monitorWorkArea{};
		PreviewMetadata metadata{};
		ComPtr<ID2D1Bitmap1> bitmap;
		std::vector<std::uint8_t> cpuPixels;
	};

	struct StartOptions final
	{
		bool enabled = true;
		HINSTANCE instance = nullptr;
		UINT embeddedResourceId = 306;
		std::wstring cachePath;
		RECT monitorBounds{};
		RECT contentBounds{};
		PreviewCompatibility compatibility{};
		std::array<std::uint8_t, 32> embeddedVisualSignature{};
		Inkeys::Startup::ProgressTracker* progress = nullptr;
		std::chrono::steady_clock::time_point startTime{};
		std::function<void(std::uint8_t)> requestBarAlpha;
		std::function<std::uint8_t()> committedBarAlpha;
	};

	struct Diagnostics final
	{
		CacheState cacheState = CacheState::Missing;
		bool active = false;
		bool firstFrameCommitted = false;
		bool automaticStopPosted = false;
		bool ownerThreadExited = false;
	};

	[[nodiscard]] bool Start(const StartOptions& options);
	void Stop() noexcept;
	[[nodiscard]] bool IsActive() noexcept;
	[[nodiscard]] bool IsPresentationAvailable() noexcept;
	[[nodiscard]] bool AcceptsCommittedBarFrames() noexcept;
	[[nodiscard]] bool ShouldBarStartTransparent() noexcept;
	[[nodiscard]] CacheState CurrentCacheState() noexcept;
	void RevalidateTopmost() noexcept;
	void UpdateContentBounds(const RECT& bounds, std::uint64_t revision) noexcept;
	[[nodiscard]] bool PublishCommittedBarFrame(CommittedBarFrame frame) noexcept;
	void SetBarStartupState(BarStartupState state) noexcept;
	[[nodiscard]] BarStartupState GetBarStartupState() noexcept;
	void NotifyBarPresentationAlphaCommitted(std::uint8_t alpha) noexcept;
	void RequestFailureFrame() noexcept;
	[[nodiscard]] bool WaitForFailureFrame(
		std::chrono::milliseconds timeout) noexcept;
	void ConfigureDeveloperCapture(std::wstring outputPath,
		PreviewCompatibility compatibility = {}) noexcept;
	[[nodiscard]] bool DeveloperCaptureCompleted() noexcept;
	[[nodiscard]] bool DeveloperCaptureRequested() noexcept;
	[[nodiscard]] Diagnostics SnapshotDiagnostics() noexcept;
}
