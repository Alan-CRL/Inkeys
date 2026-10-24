#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <stop_token>

namespace Inkeys::Graphics::FreezeCapture
{
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	struct Rect
	{
		std::int32_t left = 0, top = 0, right = 0, bottom = 0;
		bool operator==(const Rect&) const = default;
	};

	struct Geometry
	{
		Rect primaryBounds{};
		Rect sourceRect{};
		Rect workArea{};
		std::uint64_t displayGeneration = 0;
		int width = 0, height = 0;
		bool operator==(const Geometry&) const = default;
	};

	enum class QualityStatus : std::uint8_t { Valid, InvalidBuffer, NearBlack, LowInformation };
	struct Quality
	{
		QualityStatus status = QualityStatus::InvalidBuffer;
		double meanLuma = 0;
		double variance = 0;
		double blackFraction = 0;
		std::uint32_t samples = 0;
	};

	enum class StopReason : std::uint8_t
	{
		None, Requested, FreezeActive, Unavailable, GeometryChanged,
		StartupTimeout, ResultTimeout, BackendFailure, CallbackFailure, Shutdown,
	};
	enum class StartStatus : std::uint8_t { Started, Busy, NotReady, FreezeActive, InvalidArgument };
	struct StartResult
	{
		StartStatus status = StartStatus::NotReady;
		std::uint64_t session = 0;
	};

	struct TimedStage
	{
		std::chrono::microseconds duration{};
		bool observed = false; // 未执行的阶段不能用默认零值冒充耗时。
	};
	struct BackendTimings
	{
		TimedStage redraw{}, printWindow{}, gdiFlush{}, pixelWork{};
	};
	struct Statistics
	{
		std::uint64_t attempts = 0, accepted = 0, rejected = 0, apiFailures = 0;
		std::uint64_t skipped = 0, lateResults = 0, warmupReads = 0;
		std::chrono::microseconds lastReadDuration{};
		std::chrono::microseconds maxReadDuration{};
		// accepted 是图像交付/捕获活性，低信息白页仍由 Tracker 判断是否有可信运动。
		std::uint64_t lowInformation = 0;
		TimedStage update{}, analyze{}, callback{};
		BackendTimings backend{};
		// 回调返回后才完成计时；Frame 中保留的是此前已完成回调，须核对其序号。
		std::uint64_t callbackSequence = 0;
	};

	enum class FailureStage : std::uint8_t { None, Begin, Update, Read, Quality, Callback, End, Timeout };
	struct Snapshot
	{
		std::uint64_t session = 0, sequence = 0;
		Geometry geometry{};
		Statistics statistics{};
		Quality quality{};
		StopReason reason = StopReason::None;
		bool ready = false, publishing = false, occupied = false, inFlight = false;
		// 只读观测：Read 返回时间不是 compositor 最新内容的凭据。
		TimePoint lastReadStarted{}, lastReadCompleted{}, lastAcceptedSample{}, lastFailureAt{};
		std::chrono::microseconds lastSampleInterval{};
		FailureStage failureStage = FailureStage::None;
		std::uint32_t nativeError = 0;
		bool nativeErrorValid = false;
		bool qualityObserved = false; // 仅在真正 Analyze 后有效，API 失败的默认 Quality 不是分析结果。
		// 失败现场独立保留，后续有效图像不得覆盖；仅 Quality 失败时有效。
		Quality failureQuality{};
		bool failureQualityObserved = false;
	};

	struct Frame
	{
		// BGRA top-down，alpha=255。借用只在本次回调内有效，不得留存 span。
		std::span<const std::uint32_t> bgra;
		int width = 0, height = 0;
		std::size_t strideBytes = 0;
		Geometry geometry{};
		// 由已核对的 identity Mag transform 决定为 1:1，不按 client/source 宽比缩放或再乘 DPI。
		double sourcePixelsPerPixelX = 1, sourcePixelsPerPixelY = 1;
		TimePoint timestamp{}; // 后端 Read 返回时刻（之后才做 Analyze），不是 compositor 最新帧凭据。
		std::uint64_t session = 0, sequence = 0;
		Quality quality{};
		Statistics statistics{};
		std::chrono::microseconds sampleInterval{};
	};
	using FrameCallback = std::function<void(const Frame&)>;

	struct BackendResult
	{
		bool succeeded = false;
		StopReason failure = StopReason::BackendFailure;
		// 只能在失败原生 API 返回后立即取得；几何/包装失败没有可靠错误码时保持 false。
		std::uint32_t nativeError = 0;
		bool nativeErrorValid = false;
		BackendTimings timings{};
		bool warmup = false; // 后端已读回但表面仍在预热；成功交付从下一轮开始。
	};

	// 全部方法只由原 MagnifierThread 调用；实现不初始化运行库或另建 Mag HWND。
	class Backend
	{
	public:
		virtual ~Backend() = default;
		virtual BackendResult Begin(const Geometry&) noexcept = 0;
		virtual BackendResult Update(const Geometry&) noexcept = 0;
		virtual BackendResult Read(const Geometry&, std::span<const std::uint32_t>&) noexcept = 0;
		// accessWindows=false 时只释放自有 DIB，不再操作已退出的 HWND/runtime。
		virtual bool End(bool accessWindows) noexcept = 0;
	};

	[[nodiscard]] const char* FailureStageName(FailureStage) noexcept;
	[[nodiscard]] bool IsValidGeometry(const Geometry&) noexcept;
	[[nodiscard]] Quality Analyze(std::span<const std::uint32_t>, int width, int height) noexcept;

	// 会话状态与可测调度器。Pump 与 Drain 只能由同一个串行者调用。
	class Session
	{
	public:
		using Wake = void(*)() noexcept;
		using Now = TimePoint(*)() noexcept;
		explicit Session(Wake wake = nullptr, Now now = &Clock::now) noexcept;
		void SetReady(bool ready) noexcept;
		void SetFreezeState(std::uint64_t revision, bool active, bool available) noexcept;
		[[nodiscard]] StartResult Start(const Geometry&, FrameCallback) noexcept;
		// 立即撤销新发布；已准入的借用回调仍可能执行，调用方须保活其捕获对象。
		void Stop(StopReason reason = StopReason::Requested) noexcept;
		[[nodiscard]] Snapshot GetSnapshot() const noexcept;
		[[nodiscard]] std::chrono::milliseconds NextDelay() const noexcept;
		void Pump(Backend&) noexcept;
		void Drain(Backend&) noexcept;
		void Supervise() noexcept;
		void RunSupervisor(std::stop_token) noexcept;
		// 原 owner 异常退出时只关门，绝不等待 PW；false 表示不得 MagUninitialize。
		[[nodiscard]] bool DisableWindowAccess() noexcept;

	private:
		enum class Phase : std::uint8_t { Begin, Update, Read };
		void RevokeLocked(StopReason) noexcept;
		void CompleteCycleLocked(TimePoint) noexcept;
		void ObserveFailureLocked(FailureStage, TimePoint, std::uint32_t error = 0, bool valid = false) noexcept;
		[[nodiscard]] TimePoint DeadlineLocked() const noexcept;
		void WakeWorker() const noexcept;

		mutable std::mutex mutex_;
		std::condition_variable monitor_;
		Wake wake_;
		Now now_;
		Snapshot state_{};
		FrameCallback callback_;
		Phase phase_ = Phase::Begin;
		TimePoint started_{}, lastResult_{}, due_{}, nextSlot_{};
		std::uint64_t freezeRevision_ = 0;
		bool freezeBlocked_ = false, windowAccess_ = false, backendBegun_ = false;
	};

	// 产品入口复用原定格窗口；Start 从同一 Display snapshot 固定全部几何。
	[[nodiscard]] StartResult Start(FrameCallback) noexcept;
	void Stop(StopReason reason = StopReason::Requested) noexcept;
	[[nodiscard]] Snapshot GetSnapshot() noexcept;
}
