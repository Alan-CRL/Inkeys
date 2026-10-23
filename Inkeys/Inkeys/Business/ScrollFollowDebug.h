#pragma once

#include "ScrollFollow.h"
#include "../CV/VisualMotionTracker.h"
#include "../Graphics/FreezeCapture.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Inkeys::Business::ScrollFollow::Debug
{
	namespace Capture = Inkeys::Graphics::FreezeCapture;
	enum class Operation : std::uint8_t { Initialize, Process };
	enum class SummaryKind : std::uint8_t { Motion, State, CaptureFailure };
	enum class PauseCause : std::uint8_t
	{
		None, InvalidRoi, TrackerPaused, CaptureQuality, CaptureApi, CaptureStartupTimeout,
		CaptureResultTimeout, CaptureUnavailable, ViewportClamped, InitializationTimeout, ControlFailure,
	};
	struct FrameIdentity
	{
		std::uint64_t windowGeneration = 0, businessSession = 0, captureSession = 0, sequence = 0, displayGeneration = 0;
	};
	struct FrameMetadata
	{
		FrameIdentity identity{};
		Operation operation = Operation::Initialize;
		Capture::Geometry geometry{};
		Capture::Quality quality{};
		Capture::Statistics statistics{};
		Capture::Rect roi{}; // capture buffer 内的半开像素坐标，非桌面坐标。
		int captureWidth = 0, captureHeight = 0;
		std::size_t captureStrideBytes = 0;
		double sourcePixelsPerPixelX = 1, sourcePixelsPerPixelY = 1;
		Capture::TimePoint sampledAt{};
		std::chrono::microseconds sampleInterval{};
	};
	struct FramePacket
	{
		FrameMetadata metadata{};
		int width = 0, height = 0;
		std::size_t strideBytes = 0;
		std::vector<std::uint8_t> bgra; // 自有紧凑 top-down BGRA，strideBytes=width*4。
		CV::TrackerDebugSnapshot tracker{};
		double copyMs = 0, totalTrackerMs = 0;
		bool targetPublished = false;
	};
	struct LiveStatus
	{
		State state = State::Off;
		std::uint64_t businessSession = 0, captureSession = 0;
		bool ownsCapture = false, awaitTracking = false;
		PauseCause pauseCause = PauseCause::None;
		Capture::Snapshot capture{};
		std::uint64_t ownerLatestSequence = 0, ownerAppliedSequence = 0;
		double viewportX = 0, viewportY = 0;
		bool viewportClamped = false;
		Capture::TimePoint observedAt{};
	};
	struct CaptureFailureInfo
	{
		bool valid = false;
		Capture::FailureStage stage = Capture::FailureStage::None;
		Capture::StopReason reason = Capture::StopReason::None;
		Capture::TimePoint at{};
		Capture::Quality quality{}; // 仅 stage==Quality 时有效。
		std::uint32_t nativeError = 0;
		bool nativeErrorValid = false;
	};
	struct NumericSummary
	{
		SummaryKind kind = SummaryKind::State;
		FrameIdentity identity{};
		Capture::TimePoint sampledAt{}, observedAt{};
		bool hasMotion = false;
		CV::MotionResult motion{};
		CaptureFailureInfo captureFailure{}; // 记录事件发生时的故障，不被后续 End/live 更新替换。
		State state = State::Off;
		PauseCause pauseCause = PauseCause::None;
		std::uint64_t captureAccepted = 0, captureRejected = 0, captureApiFailures = 0, ownerAppliedSequence = 0;
		double viewportX = 0, viewportY = 0, copyMs = 0, totalTrackerMs = 0;
	};
	struct Counters
	{
		std::uint64_t framesPublished = 0, framesOverwritten = 0, framesConsumed = 0, frameDrops = 0, summaryDrops = 0;
		std::uint64_t trackerCalls = 0, trackerAccepted = 0, trackerRejected = 0;
	};
	struct LiveSnapshot
	{
		LiveStatus status{};
		Counters counters{};
		std::array<NumericSummary, 30> summaries{}; // 有效项按旧到新排列。
		std::size_t count = 0;
	};

	class Channel;
	using ChannelPtr = std::shared_ptr<Channel>;
	class WriteLease
	{
	public:
		WriteLease() noexcept = default;
		~WriteLease();
		WriteLease(WriteLease&&) noexcept;
		WriteLease& operator=(WriteLease&&) noexcept;
		WriteLease(const WriteLease&) = delete;
		WriteLease& operator=(const WriteLease&) = delete;
		explicit operator bool() const noexcept;
		CV::TrackerDebugSnapshot* Tracker() noexcept;
		bool Publish(bool targetPublished, double totalTrackerMs) noexcept;
	private:
		friend class Channel;
		WriteLease(ChannelPtr, unsigned) noexcept;
		void Reset() noexcept;
		ChannelPtr owner_;
		unsigned slot_ = 0;
	};
	class Channel : public std::enable_shared_from_this<Channel>
	{
	public:
		~Channel();
		bool Active() const noexcept;
		std::uint64_t Generation() const noexcept;
		WriteLease BeginFrame(const FrameMetadata&, const CV::MotionImageView&) noexcept;
		void RecordMotion(const FrameMetadata&, const CV::MotionResult&, double totalTrackerMs) noexcept;
		void UpdateStatus(const LiveStatus&) noexcept;
		LiveSnapshot ReadLive() const noexcept;
		// 仅 UI owner 消费；Freeze 时不调用 ConsumeLatest，front 保持不可变。
		bool ConsumeLatest() noexcept;
		const FramePacket* Displayed() const noexcept;
		std::unique_ptr<FramePacket> CloneDisplayed() const noexcept;
	private:
		friend class WriteLease;
		friend ChannelPtr Activate() noexcept;
		friend void Deactivate(const ChannelPtr&) noexcept;
		explicit Channel(std::uint64_t);
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
	ChannelPtr Activate() noexcept;
	bool Enabled() noexcept;
	ChannelPtr Current() noexcept;
	void Deactivate(const ChannelPtr& expected) noexcept;
	// Frame/控制线程只保留首个故障，后续 Stop(Requested) 不得冲掉来源。
	void LatchPause(std::atomic<PauseCause>&, PauseCause) noexcept;
	const char* PauseCauseName(PauseCause) noexcept;
}
