#include "ScrollFollowDebug.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace Inkeys::Business::ScrollFollow::Debug
{
	namespace
	{
		constexpr unsigned IndexMask = 3, Dirty = 4, Closed = 8;
		std::atomic_bool enabled = false;
		std::atomic<ChannelPtr> current;
		std::atomic_uint64_t nextGeneration = 1;
		bool Layout(const CV::MotionImageView& image, std::size_t& rowBytes) noexcept
		{
			if (!image.data || image.format != CV::MotionPixelFormat::Bgra32 || image.width <= 0 || image.height <= 0
				|| static_cast<std::uint64_t>(image.width) * image.height > 64ULL * 1024 * 1024) return false;
			rowBytes = static_cast<std::size_t>(image.width) * 4;
			const auto previousRows = static_cast<std::size_t>(image.height - 1);
			return image.strideBytes >= rowBytes && (!previousRows
				|| image.strideBytes <= (std::numeric_limits<std::size_t>::max() - rowBytes) / previousRows)
				&& previousRows * image.strideBytes + rowBytes <= image.sizeBytes;
		}
	}
	struct Channel::Impl
	{
		explicit Impl(std::uint64_t value) : generation(value) {}
		const std::uint64_t generation;
		std::array<FramePacket, 3> slots{};
		std::atomic_uint middle = 2;
		unsigned front = 0, back = 1; // front 只归 UI，back/writing 只归 Mag。
		bool frontValid = false, writing = false;
		std::atomic_uint64_t published = 0, overwritten = 0, consumed = 0, drops = 0, summaryDrops = 0;
		std::atomic_uint64_t trackerCalls = 0, trackerAccepted = 0, trackerRejected = 0;
		mutable std::mutex valuesMutex;
		LiveStatus status{};
		std::array<NumericSummary, 30> history{};
		std::size_t historyNext = 0, historyCount = 0;
		bool statusValid = false;
		void Append(const NumericSummary& summary) noexcept
		{
			history[historyNext] = summary; historyNext = (historyNext + 1) % history.size();
			historyCount = std::min(historyCount + 1, history.size());
		}
	};

	Channel::Channel(std::uint64_t generation) : impl_(std::make_unique<Impl>(generation)) {}
	Channel::~Channel() = default;
	bool Channel::Active() const noexcept { return (impl_->middle.load(std::memory_order_acquire) & Closed) == 0; }
	std::uint64_t Channel::Generation() const noexcept { return impl_->generation; }
	ChannelPtr Activate() noexcept
	{
		try
		{
			auto value = ChannelPtr(new Channel(nextGeneration.fetch_add(1, std::memory_order_relaxed)));
			if (auto old = current.exchange(value, std::memory_order_acq_rel)) old->impl_->middle.fetch_or(Closed, std::memory_order_acq_rel);
			enabled.store(true, std::memory_order_release);
			return value;
		}
		catch (...) { return {}; }
	}
	bool Enabled() noexcept { return enabled.load(std::memory_order_acquire); }
	ChannelPtr Current() noexcept
	{
		if (!Enabled()) return {};
		auto value = current.load(std::memory_order_acquire);
		return value && value->Active() ? value : ChannelPtr{};
	}
	void Deactivate(const ChannelPtr& expected) noexcept
	{
		if (!expected) return;
		expected->impl_->middle.fetch_or(Closed, std::memory_order_acq_rel);
		auto value = expected;
		if (current.compare_exchange_strong(value, {}, std::memory_order_acq_rel)) enabled.store(false, std::memory_order_release);
	}
	WriteLease::WriteLease(ChannelPtr owner, unsigned slot) noexcept : owner_(std::move(owner)), slot_(slot) {}
	WriteLease::~WriteLease() { Reset(); }
	WriteLease::WriteLease(WriteLease&& other) noexcept : owner_(std::move(other.owner_)), slot_(other.slot_) {}
	WriteLease& WriteLease::operator=(WriteLease&& other) noexcept
	{
		if (this != &other) { Reset(); owner_ = std::move(other.owner_); slot_ = other.slot_; }
		return *this;
	}
	void WriteLease::Reset() noexcept
	{
		if (owner_)
		{
			owner_->impl_->writing = false; owner_->impl_->drops.fetch_add(1, std::memory_order_relaxed); owner_.reset();
		}
	}
	WriteLease::operator bool() const noexcept { return owner_ != nullptr; }
	CV::TrackerDebugSnapshot* WriteLease::Tracker() noexcept { return owner_ ? &owner_->impl_->slots[slot_].tracker : nullptr; }
	bool WriteLease::Publish(bool targetPublished, double totalTrackerMs) noexcept
	{
		if (!owner_) return false;
		auto& impl = *owner_->impl_; auto& packet = impl.slots[slot_];
		packet.targetPublished = targetPublished; packet.totalTrackerMs = totalTrackerMs;
		auto middle = impl.middle.load(std::memory_order_acquire);
		while (!(middle & Closed))
		{
			if (impl.middle.compare_exchange_weak(middle, slot_ | Dirty, std::memory_order_acq_rel))
			{
				impl.back = middle & IndexMask; impl.writing = false;
				impl.published.fetch_add(1, std::memory_order_relaxed);
				if (middle & Dirty) impl.overwritten.fetch_add(1, std::memory_order_relaxed);
				owner_.reset(); return true;
			}
		}
		Reset(); return false;
	}
	WriteLease Channel::BeginFrame(const FrameMetadata& metadata, const CV::MotionImageView& image) noexcept
	{
		if (!Active()) return {};
		std::size_t rowBytes = 0;
		if (impl_->writing || !Layout(image, rowBytes)) { impl_->drops.fetch_add(1, std::memory_order_relaxed); return {}; }
		try
		{
			auto owner = weak_from_this().lock(); if (!owner) return {};
			auto& packet = impl_->slots[impl_->back];
			const auto began = Capture::Clock::now();
			packet.bgra.resize(rowBytes * static_cast<std::size_t>(image.height));
			for (int y = 0; y < image.height; ++y)
				std::memcpy(packet.bgra.data() + static_cast<std::size_t>(y) * rowBytes,
					image.data + static_cast<std::size_t>(y) * image.strideBytes, rowBytes);
			packet.copyMs = std::chrono::duration<double, std::milli>(Capture::Clock::now() - began).count();
			packet.metadata = metadata; packet.metadata.identity.windowGeneration = Generation();
			packet.width = image.width; packet.height = image.height; packet.strideBytes = rowBytes;
			packet.targetPublished = false; packet.totalTrackerMs = 0;
			impl_->writing = true;
			return WriteLease(std::move(owner), impl_->back);
		}
		catch (...) { impl_->drops.fetch_add(1, std::memory_order_relaxed); return {}; }
	}
	void Channel::RecordMotion(const FrameMetadata& metadata, const CV::MotionResult& motion, double totalTrackerMs) noexcept
	{
		if (!Active()) return;
		impl_->trackerCalls.fetch_add(1, std::memory_order_relaxed);
		(motion.accepted ? impl_->trackerAccepted : impl_->trackerRejected).fetch_add(1, std::memory_order_relaxed);
		try
		{
			// Mag 只尝试取得数值锁；UI 即使很慢也不能阻塞原捕获回调。
			std::unique_lock lock(impl_->valuesMutex, std::try_to_lock);
			if (!lock.owns_lock()) { impl_->summaryDrops.fetch_add(1, std::memory_order_relaxed); return; }
			NumericSummary summary;
			summary.kind = SummaryKind::Motion; summary.identity = metadata.identity; summary.identity.windowGeneration = Generation();
			summary.sampledAt = metadata.sampledAt; summary.observedAt = Capture::Clock::now(); summary.hasMotion = true; summary.motion = motion;
			summary.state = impl_->status.state; summary.pauseCause = impl_->status.pauseCause;
			summary.captureAccepted = metadata.statistics.accepted; summary.captureRejected = metadata.statistics.rejected;
			summary.captureApiFailures = metadata.statistics.apiFailures; summary.ownerAppliedSequence = impl_->status.ownerAppliedSequence;
			summary.viewportX = impl_->status.viewportX; summary.viewportY = impl_->status.viewportY; summary.totalTrackerMs = totalTrackerMs;
			if (impl_->writing) summary.copyMs = impl_->slots[impl_->back].copyMs;
			impl_->Append(summary);
		}
		catch (...) { impl_->summaryDrops.fetch_add(1, std::memory_order_relaxed); }
	}
	void Channel::UpdateStatus(const LiveStatus& status) noexcept
	{
		if (!Active()) return;
		try
		{
			std::scoped_lock lock(impl_->valuesMutex);
			const auto& old = impl_->status;
			const bool failureChanged = status.ownsCapture && (!old.ownsCapture || old.captureSession != status.captureSession
				|| old.capture.statistics.rejected != status.capture.statistics.rejected || old.capture.statistics.apiFailures != status.capture.statistics.apiFailures
				|| old.capture.lastFailureAt != status.capture.lastFailureAt || old.capture.failureStage != status.capture.failureStage
				|| old.capture.nativeErrorValid != status.capture.nativeErrorValid
				|| (status.capture.nativeErrorValid && old.capture.nativeError != status.capture.nativeError));
			if (!impl_->statusValid || failureChanged || old.businessSession != status.businessSession || old.state != status.state
				|| old.pauseCause != status.pauseCause || old.awaitTracking != status.awaitTracking)
			{
				NumericSummary summary;
				summary.kind = failureChanged && status.capture.failureStage != Capture::FailureStage::None ? SummaryKind::CaptureFailure : SummaryKind::State;
				summary.identity = { Generation(), status.businessSession, status.captureSession, status.ownsCapture ? status.capture.sequence : 0,
					status.ownsCapture ? status.capture.geometry.displayGeneration : 0 };
				summary.observedAt = status.observedAt; summary.state = status.state; summary.pauseCause = status.pauseCause;
				if (status.ownsCapture)
				{
					summary.sampledAt = status.capture.lastReadCompleted; summary.captureAccepted = status.capture.statistics.accepted;
					summary.captureRejected = status.capture.statistics.rejected; summary.captureApiFailures = status.capture.statistics.apiFailures;
				}
				if (summary.kind == SummaryKind::CaptureFailure)
					summary.captureFailure = { true, status.capture.failureStage, status.capture.reason, status.capture.lastFailureAt,
						status.capture.quality, status.capture.nativeError, status.capture.nativeErrorValid };
				summary.ownerAppliedSequence = status.ownerAppliedSequence; summary.viewportX = status.viewportX; summary.viewportY = status.viewportY;
				impl_->Append(summary);
			}
			impl_->status = status; impl_->statusValid = true;
		}
		catch (...) { impl_->summaryDrops.fetch_add(1, std::memory_order_relaxed); }
	}
	LiveSnapshot Channel::ReadLive() const noexcept
	{
		LiveSnapshot result;
		try
		{
			std::scoped_lock lock(impl_->valuesMutex); result.status = impl_->status; result.count = impl_->historyCount;
			const auto begin = (impl_->historyNext + impl_->history.size() - impl_->historyCount) % impl_->history.size();
			for (std::size_t i = 0; i < result.count; ++i) result.summaries[i] = impl_->history[(begin + i) % impl_->history.size()];
		}
		catch (...) {}
		result.counters = { impl_->published.load(), impl_->overwritten.load(), impl_->consumed.load(), impl_->drops.load(), impl_->summaryDrops.load(),
			impl_->trackerCalls.load(), impl_->trackerAccepted.load(), impl_->trackerRejected.load() };
		return result;
	}
	bool Channel::ConsumeLatest() noexcept
	{
		auto middle = impl_->middle.load(std::memory_order_acquire);
		while ((middle & Dirty) && !(middle & Closed))
		{
			if (impl_->middle.compare_exchange_weak(middle, impl_->front, std::memory_order_acq_rel))
			{
				impl_->front = middle & IndexMask; impl_->frontValid = true; impl_->consumed.fetch_add(1, std::memory_order_relaxed); return true;
			}
		}
		return false;
	}
	const FramePacket* Channel::Displayed() const noexcept { return impl_->frontValid ? &impl_->slots[impl_->front] : nullptr; }
	std::unique_ptr<FramePacket> Channel::CloneDisplayed() const noexcept
	{
		try { const auto* packet = Displayed(); return packet ? std::make_unique<FramePacket>(*packet) : nullptr; }
		catch (...) { return {}; }
	}
	void LatchPause(std::atomic<PauseCause>& destination, PauseCause cause) noexcept
	{
		auto expected = PauseCause::None; destination.compare_exchange_strong(expected, cause, std::memory_order_acq_rel);
	}
	const char* PauseCauseName(PauseCause cause) noexcept
	{
		switch (cause)
		{
#define INKEYS_DEBUG_PAUSE(name) case PauseCause::name: return #name
			INKEYS_DEBUG_PAUSE(None); INKEYS_DEBUG_PAUSE(InvalidRoi); INKEYS_DEBUG_PAUSE(TrackerPaused); INKEYS_DEBUG_PAUSE(CaptureQuality);
			INKEYS_DEBUG_PAUSE(CaptureApi); INKEYS_DEBUG_PAUSE(CaptureStartupTimeout); INKEYS_DEBUG_PAUSE(CaptureResultTimeout);
			INKEYS_DEBUG_PAUSE(CaptureUnavailable); INKEYS_DEBUG_PAUSE(ViewportClamped); INKEYS_DEBUG_PAUSE(InitializationTimeout); INKEYS_DEBUG_PAUSE(ControlFailure);
#undef INKEYS_DEBUG_PAUSE
		}
		return "Unknown";
	}
}
