module;

#include <atomic>
#include <chrono>
#include <cstdint>

export module Inkeys.Startup.Progress;

export namespace Inkeys::Startup
{
	enum class Milestone : std::uint8_t
	{
		SuperTopCrossed,
		MiniConfigRead,
		PreviewGeometryReady,
		PreviewOwnerReady,
		PreviewRenderClientReady,
		PreviewFirstFrameCommitted,
		LoggingReady,
		DpiAwarenessReady,
		LegacySurfaceReady,
		ComReady,
		DisplayReady,
		FullConfigReady,
		I18nReady,
		PluginsReady,
		PptComReady,
		RenderFactoriesReady,
		RenderDeviceReady,
		RenderSchedulerReady,
		FontsReady,
		WindowOverlayReady,
		WindowSettingReady,
		WindowServiceReady,
		Draw3WindowAttached,
		Draw3GraphicsReady,
		Draw3PresenterReady,
		Draw3RtsReady,
		Draw3ControllerReady,
		Draw3FirstFrameCommitted,
		SettingReady,
		WhiteboardReady,
		InitialTopmostRefresh,
		FreezeFirstFrameCommitted,
		PptUiClientsReady,
		BarWindowReady,
		BarUiGraphReady,
		BarMediaReady,
		BarComponentsReady,
		BarStateReady,
		BarInteractionReady,
		BarRenderClientRegistered,
		BarFirstFrameCommitted,
		Count,
	};

	struct Plan final
	{
		std::uint32_t totalUnits = 0;

		[[nodiscard]] static Plan ForStartup(bool previewEnabled) noexcept;
		[[nodiscard]] bool Contains(Milestone milestone) const noexcept;
		[[nodiscard]] std::uint16_t Weight(Milestone milestone) const noexcept;
		[[nodiscard]] bool PreviewEnabled() const noexcept;

	private:
		std::uint64_t milestoneMask_ = 0;
		friend class ProgressTracker;
	};

	struct Snapshot final
	{
		std::uint32_t completedUnits = 0;
		std::uint32_t totalUnits = 0;
		double actualRatio = 0.0;
		bool failed = false;
		std::uint32_t failureCode = 0;
		std::uint64_t revision = 0;
		std::chrono::steady_clock::time_point startTime{};
	};

	class ProgressTracker final
	{
	public:
		explicit ProgressTracker(Plan plan,
			std::chrono::steady_clock::time_point startTime =
				std::chrono::steady_clock::now()) noexcept;
		ProgressTracker(const ProgressTracker&) = delete;
		ProgressTracker& operator=(const ProgressTracker&) = delete;

		[[nodiscard]] bool Complete(Milestone milestone) noexcept;
		[[nodiscard]] bool Fail(std::uint32_t failureCode) noexcept;
		[[nodiscard]] Snapshot GetSnapshot() const noexcept;
		[[nodiscard]] const Plan& GetPlan() const noexcept { return plan_; }

	private:
		void Lock() const noexcept;
		void Unlock() const noexcept;

		Plan plan_;
		std::chrono::steady_clock::time_point startTime_{};
		mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
		std::uint64_t completedMask_ = 0;
		std::uint32_t completedUnits_ = 0;
		std::uint32_t failureCode_ = 0;
		std::uint64_t revision_ = 0;
		bool failed_ = false;
	};

	// 启动生产者只做无阻塞原子取指针与 tracker 的短临界区更新。
	void SetActiveTracker(ProgressTracker* tracker) noexcept;
	void ClearActiveTracker(ProgressTracker* tracker) noexcept;
	[[nodiscard]] bool Report(Milestone milestone) noexcept;
	[[nodiscard]] bool ReportFailure(std::uint32_t failureCode) noexcept;
	[[nodiscard]] Snapshot ActiveSnapshot() noexcept;
}
