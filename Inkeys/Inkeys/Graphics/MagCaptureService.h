#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>

namespace Inkeys::Graphics
{
	enum class MagCaptureState { Off, Starting, Capturing, Stopping, Failed };
	enum class MagCaptureReason
	{
		None, Stopped, InvalidOptions, InvalidGeometry, Unsupported,
		RuntimeUnavailable, ThreadFailure, ComInitialization, WindowClass, HostWindow, ChildWindow,
		Transform, Filter, Source, Redraw, PrintWindow, InvalidBuffer,
		NearBlack, LowInformation, StartupTimeout, ResultTimeout, CallbackFailure, CleanupFailure,
	};

	struct MagFrameQuality
	{
		MagCaptureReason reason = MagCaptureReason::InvalidBuffer;
		double mean = 0, variance = 0, nearBlackRatio = 0, edgeRatio = 0;
		std::size_t sampledPixels = 0;
		[[nodiscard]] bool Valid() const noexcept { return reason == MagCaptureReason::None; }
	};

	// BGRA top-down；alpha 不参与有效性判断。只借用到本次同步回调返回。
	struct MagCaptureFrame
	{
		std::span<const std::uint8_t> bgra;
		int width = 0, height = 0;
		std::size_t stride = 0;
		RECT sourceRect{}, workArea{};
		std::uint64_t session = 0, sequence = 0, displayGeneration = 0;
		std::int64_t sampledQpc = 0, qpcFrequency = 0;
		double captureMilliseconds = 0;
		MagFrameQuality quality;
		unsigned long error = ERROR_SUCCESS;
	};

	struct MagCaptureOptions
	{
		std::uint64_t session = 0;
		unsigned framesPerSecond = 20;
		bool verbose = false;
	};

	struct MagCaptureSnapshot
	{
		MagCaptureState state = MagCaptureState::Off;
		MagCaptureReason reason = MagCaptureReason::None;
		std::uint64_t session = 0, sequence = 0, validFrames = 0, invalidFrames = 0;
		std::uint64_t displayGeneration = 0;
		RECT sourceRect{}, workArea{};
		UINT dpiX = 96, dpiY = 96;
		HWND host = nullptr, child = nullptr;
		unsigned long ownerThreadId = 0, error = ERROR_SUCCESS;
		std::size_t excludedWindows = 0;
		double lastCaptureMilliseconds = 0, averageCaptureMilliseconds = 0;
		double maximumCaptureMilliseconds = 0, actualFramesPerSecond = 0;
		double startupMilliseconds = 0;
		MagFrameQuality lastQuality;
		bool resourcesReleased = true, cleanupSucceeded = true, publicationOpen = false;
	};

	[[nodiscard]] const char* MagCaptureReasonText(MagCaptureReason reason) noexcept;
	[[nodiscard]] MagFrameQuality AnalyzeMagFrame(std::span<const std::uint8_t> bgra,
		int width, int height, std::size_t stride) noexcept;
	[[nodiscard]] bool MagCaptureResultTimedOut(std::uint64_t now,
		std::uint64_t lastCompletion, bool initializing = false) noexcept;

	namespace Detail { struct MagCaptureSharedState; }

	class MagCaptureService final
	{
	public:
		using FrameCallback = std::function<void(const MagCaptureFrame&)>;
		MagCaptureService() noexcept = default;
		~MagCaptureService();
		MagCaptureService(const MagCaptureService&) = delete;
		MagCaptureService& operator=(const MagCaptureService&) = delete;
		[[nodiscard]] bool Start(MagCaptureOptions options, FrameCallback callback) noexcept;
		// 不等待 PrintWindow 或已准入的回调；resourcesReleased 后才允许重启和释放回调依赖。
		void Stop() noexcept;
		[[nodiscard]] MagCaptureSnapshot Snapshot() const noexcept;
	private:
		mutable std::mutex mutex_;
		std::shared_ptr<Detail::MagCaptureSharedState> state_;
	};
}
