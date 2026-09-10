#pragma once

// 临时底栏取证入口；移除本文件、实现与 INKEYS_BAR_BOTTOM_DOCK_TRACE 工程定义即可关闭。
#if defined(INKEYS_BAR_BOTTOM_DOCK_TRACE) && !defined(IDT_RELEASE)
#define INKEYS_BAR_BOTTOM_DOCK_TRACE_ACTIVE 1
#else
#define INKEYS_BAR_BOTTOM_DOCK_TRACE_ACTIVE 0
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>

namespace Inkeys::UI::Bar::BottomDockTrace
{
	// 构建开关跨普通 TU 保持一致；IDT_RELEASE 在产品调用点进一步禁用插桩。
#if defined(INKEYS_BAR_BOTTOM_DOCK_TRACE)
	inline constexpr bool Enabled = true;
#else
	inline constexpr bool Enabled = false;
#endif
	inline constexpr std::uint32_t SchemaVersion = 1;
	enum class Event : std::uint32_t
	{
		GestureStart, GestureBasis, Pointer, EnvironmentRebase, DirectMove,
		GestureEnd, FrameSnapshot, VerticalMapping, Invalidated, Submit,
		PresentResult, Committed, AbsorbBefore, AbsorbAfter,
	};
	enum Group : std::uint32_t
	{
		State = 1, Environment = 2, Input = 4, Geometry = 8,
		Spring = 16, Window = 32, Result = 64, Timing = 128, PresentedState = 256,
	};
	enum Reason : std::uint32_t
	{
		None, BarrierPending, GeometryBusy, GeometryUnavailable, ApiFailure,
		Cancelled, ReleaseHandoff, InvalidatedTuple, FailureBackoff, NoPresentation,
	};
	enum Flag : std::uint32_t
	{
		Touch = 1, Captured = 2, Detached = 4, OsWindowValid = 8,
		BasisAvailable = 16, GrabPointValid = 32, AnimationsEnabled = 64,
		CaptureSeeded = 128, CaptureSpringActive = 256, GripSpringActive = 512,
		FullReplacement = 1024, WindowApiAttempted = 2048, GripRecoverySeeded = 4096,
		VerticalHandoffSeeded = 8192,
	};
	// 与渲染候选、独立图像 probe 分开，原样记录生产成功快照的坐标域。
	struct PresentedStateRecord
	{
		POINT origin{}, directTranslation{};
		double zoom = 0.0;
		double mainHeightDip = 0.0; // 0 表示生产快照尚无成功的实际高度。
		std::array<double, 2> baseY{}, visualY{}, mainCenterScreen{};
		double rawMainCenterScreenX = 0.0, rawBodyCenterScreenX = 0.0;
		std::uint64_t transitionSerial = 0, mappingSerial = 0, displaySerial = 0;
	};
	struct Record
	{
		Event event{};
		std::uint32_t groups = 0, flags = 0, reason = None, thread = 0;
		std::uint64_t sequence = 0, gesture = 0, frame = 0, dropped = 0;
		std::int64_t qpc = 0, snapshotQpc = 0, submitQpc = 0;
		std::uint64_t consumedSerial = 0, currentSerial = 0, deferredSerial = 0,
			presentedSerial = 0, displaySerial = 0;
		std::uint64_t taggedSerial = 0, observedBeforeSerial = 0, deviceGeneration = 0;
		std::uint32_t mode = 0, phase = 0, centerMode = 0, centerPhase = 0, tool = 0;
		bool drag = false, recovery = false, opensRight = false;
		std::array<double, 2> elasticInputDip{};
		RECT monitor{}, workArea{};
		POINT monitorOrigin{};
		std::uint32_t dpi = 0;
		double configZoom = 0.0, zoom = 0.0, dockLine = 0.0;
		double insetDip = 0.0, dpiScale = 0.0;
		std::array<double, 2> pointer{}, grabOffset{}, logicalDown{}, normalizedDown{}, rawGrip{};
		std::uint64_t basisFrame = 0;
		std::int64_t basisQpc = 0;
		std::array<double, 2> root{}, mainSize{}, baseY{}, visualY{};
		double baseSize = 0.0, stroke = 0.0, scaleY = 1.0, translationY = 0.0;
		std::array<double, 4> barBounds{};
		double barStroke = 0.0;
		// 生产求解器与独立 Down probe 分开，便于识别屏幕约束和实际抓点偏差。
		bool grabSolverValid = false, grabConstrained = false;
		double grabNormalizedY = 0.5, grabRawPointerScreenY = 0.0;
		std::array<double, 3> grabSolverDip{}; // logical / desired / effective DIP
		// base left/right、visual left/right、scale、刚性抓手 X 位移。
		std::array<double, 6> horizontal{};
		std::array<double, 3> gripSpring{}, captureSpring{};
		std::array<double, 2> gripBefore{}, captureBefore{}, seedScreen{};
		double rawDt = 0.0, frameDt = 0.0, integratedDt = 0.0, animationSpeed = 1.0;
		double gripDt = 0.0, captureDt = 0.0;
		std::array<std::int64_t, 4> stageQpc{};
		POINT desired{}, actual{}, frameTranslation{}, capacityOrigin{}, source{}, destination{};
		SIZE capacitySize{}, windowSize{};
		RECT viewport{}, cachedWindow{}, osWindow{};
		HRESULT resourceHr = E_PENDING, getDc = E_PENDING, releaseDc = E_PENDING, endDraw = E_PENDING;
		std::int32_t ulw = -1;
		DWORD winError = 0;
		bool committed = false;
		PresentedStateRecord presentedState{};
	};
	static_assert(std::is_trivially_copyable_v<Record>);

	// 写者只尝试一次；消费者也只复制 POD，不在这个短临界区格式化或写文件。
	template<std::size_t Capacity>
	class Buffer
	{
	public:
		bool TryPush(Record record) noexcept
		{
			if (lock_.test_and_set(std::memory_order_acquire))
			{
				dropped_.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			bool accepted = count_ < Capacity;
			if (accepted)
			{
				record.sequence = ++sequence_;
				record.dropped = dropped_.load(std::memory_order_relaxed);
				records_[write_] = record;
				write_ = (write_ + 1) % Capacity;
				++count_;
			}
			else dropped_.fetch_add(1, std::memory_order_relaxed);
			lock_.clear(std::memory_order_release);
			return accepted;
		}
		bool TryPop(Record& record) noexcept
		{
			if (lock_.test_and_set(std::memory_order_acquire)) return false;
			const bool available = count_ != 0;
			if (available)
			{
				record = records_[read_];
				read_ = (read_ + 1) % Capacity;
				--count_;
			}
			lock_.clear(std::memory_order_release);
			return available;
		}
		std::uint64_t Dropped() const noexcept { return dropped_.load(); }
	private:
		std::array<Record, Capacity> records_{};
		std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
		std::atomic<std::uint64_t> dropped_{ 0 };
		std::size_t read_ = 0, write_ = 0, count_ = 0;
		std::uint64_t sequence_ = 0;
	};

	struct Limits
	{
		std::uint64_t fileBytes = 8 * 1024 * 1024;
		std::size_t retainedFiles = 4;
		std::uint32_t tailMilliseconds = 1000;
	};
	struct Statistics
	{
		std::uint64_t written = 0, dropped = 0, probeMisses = 0, writerErrors = 0;
		std::uint64_t runId = 0, writerDropped = 0;
	};
	std::int64_t Now() noexcept;
	std::int64_t Frequency() noexcept;
	bool ReadWindow(HWND window, RECT& bounds) noexcept;
	std::string Serialize(const Record& record, std::uint64_t runId);
	bool IsOwnedFilename(const std::filesystem::path& filename);
	bool ResolveInitialGrab(Record& start, const Record& basis, const RECT& actualWindow) noexcept;

	class Recorder
	{
	public:
		Recorder();
		~Recorder();
		Recorder(const Recorder&) = delete;
		Recorder& operator=(const Recorder&) = delete;
		bool Start(const std::filesystem::path& directory, Limits limits = {}, bool enabled = Enabled) noexcept;
		void Stop() noexcept;
		std::uint64_t ActiveGesture() const noexcept;
		std::uint64_t BeginGesture(Record& start, const RECT& actualWindow) noexcept;
		void EndGesture(Record end) noexcept;
		void Push(Record record) noexcept;
		void Presented(Record probe) noexcept;
		void InvalidatePresented() noexcept;
		Statistics Stats() const noexcept;
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
		std::atomic<Impl*> published_{ nullptr };
	};
	Recorder& Get() noexcept;
}
