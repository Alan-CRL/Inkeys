#include "ScrollFollowDebug.h"

#include <algorithm>
#include <cstring>
#include <cmath>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
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

		struct Recording
		{
			explicit Recording(std::uint64_t after) : afterSession(after), data(std::make_shared<RecordingData>())
			{ data->inputs.reserve(RecordingInfo::MaximumFrames); }
			const std::uint64_t afterSession;
			std::mutex mutex;
			std::shared_ptr<RecordingData> data;
			std::atomic<RecordingState> state = RecordingState::Armed;
			std::atomic<RecordingStop> stop = RecordingStop::None, requested = RecordingStop::None;
			std::atomic_uint64_t session = 0;
			std::atomic_size_t count = 0, bytes = 0;
			std::atomic<double> duration = 0;
			std::atomic_int64_t beganUs = 0;
			RecordingInfo Info() const noexcept
			{
				RecordingInfo value; value.state=state.load(std::memory_order_acquire); value.stop=stop.load();
				value.businessSession=session.load(); value.frames=count.load(); value.pixelBytes=bytes.load(); value.durationSeconds=duration.load();
				value.completePrefix=value.state==RecordingState::Complete && value.frames>0; return value;
			}
			bool Expired() const noexcept
			{
				const auto began=beganUs.load(); return began && std::chrono::duration_cast<std::chrono::microseconds>(Capture::Clock::now().time_since_epoch()).count()-began>=10000000;
			}
			void Seal(RecordingStop reason) noexcept // 持有 mutex；封存后不再改像素或元数据。
			{
				const auto current=state.load(); if (current==RecordingState::Complete || current==RecordingState::Failed) return;
				stop.store(reason); data->info=Info(); data->info.stop=reason;
				data->info.state=reason>=RecordingStop::InvalidInput?RecordingState::Failed:RecordingState::Complete;
				data->info.completePrefix=data->info.state==RecordingState::Complete && !data->inputs.empty();
				state.store(data->info.state,std::memory_order_release);
			}
			void Finish(RecordingStop reason) noexcept
			{
				try
				{
					auto none=RecordingStop::None; requested.compare_exchange_strong(none,reason);
					std::scoped_lock lock(mutex); Seal(requested.load());
				}
				catch (...) {}
			}
		};
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
		std::atomic<std::shared_ptr<Recording>> recording;
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
		if (auto recording=expected->impl_->recording.load()) recording->Finish(RecordingStop::Closed);
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
	bool Channel::ArmRecording() noexcept
	{
		if (!Active()) return false;
		try
		{
			// 显式重录会替换上一段；在途生产者仅持有旧 recorder，不可写入新会话。
			auto next=std::make_shared<Recording>(ReadLive().status.businessSession);
			impl_->recording.store(std::move(next),std::memory_order_release); return true;
		}
		catch (...) { return false; }
	}
	void Channel::FinishRecording() noexcept
	{
		if (auto recording=impl_->recording.load(std::memory_order_acquire)) recording->Finish(RecordingStop::UserStopped);
	}
	RecordingInfo Channel::ReadRecording() const noexcept
	{
		auto recording=impl_->recording.load(std::memory_order_acquire); if (!recording) return {};
		// UI 轮询也封存墙钟上限；即使捕获不再交付帧，录制不会永远停在 Recording。
		if (recording->state.load()==RecordingState::Recording && recording->Expired()) recording->Finish(RecordingStop::TimeLimit);
		return recording->Info();
	}
	std::shared_ptr<const RecordingData> Channel::TakeRecording() noexcept
	{
		auto recording=impl_->recording.load(std::memory_order_acquire); if (!recording) return {};
		recording->Finish(RecordingStop::UserStopped);
		const auto info=recording->Info();
		// 封存锁异常时仍可能处于Recording，不能把还可修改的数据当不可变导出快照。
		return info.frames && (info.state==RecordingState::Complete || info.state==RecordingState::Failed)?recording->data:nullptr;
	}
	void Channel::RecordInput(const FrameMetadata& metadata, const CV::MotionFrame& input, const CV::MotionResult* motion) noexcept
	{
		if (!Active()) return;
		auto recording=impl_->recording.load(std::memory_order_acquire); if (!recording) return;
		const auto state=recording->state.load(std::memory_order_acquire);
		if (state==RecordingState::Complete || state==RecordingState::Failed) return;
		// UI 封存仅锁定元数据，不复制像素；回调永不等待该锁，竞争时明确标记不完整。
		std::unique_lock<std::mutex> lock(recording->mutex,std::defer_lock);
		try { (void)lock.try_lock(); }
		catch (...) { auto none=RecordingStop::None; recording->requested.compare_exchange_strong(none,RecordingStop::ConcurrentAccess); return; }
		if (!lock.owns_lock())
		{
			auto none=RecordingStop::None; recording->requested.compare_exchange_strong(none,RecordingStop::ConcurrentAccess); return;
		}
		try
		{
			if (recording->requested.load()!=RecordingStop::None) { recording->Seal(recording->requested.load()); return; }
			if (recording->state.load()==RecordingState::Armed)
			{
				if (metadata.operation!=Operation::Initialize || metadata.identity.businessSession<=recording->afterSession) return;
				recording->session.store(metadata.identity.businessSession); recording->beganUs.store(std::chrono::duration_cast<std::chrono::microseconds>(Capture::Clock::now().time_since_epoch()).count());
				recording->state.store(RecordingState::Recording,std::memory_order_release);
			}
			else if (recording->state.load()!=RecordingState::Recording) return;
			if (recording->Expired()) { recording->Seal(RecordingStop::TimeLimit); return; }
			const auto& image=input.image; const bool bgra=image.format==CV::MotionPixelFormat::Bgra32;
			const auto pixelCount=static_cast<std::uint64_t>((std::max)(0,image.width))*static_cast<std::uint64_t>((std::max)(0,image.height));
			const auto rowBytes=static_cast<std::size_t>((std::max)(0,image.width))*(bgra?4:1);
			const auto rows=image.height>0?static_cast<std::size_t>(image.height-1):0;
			if (!image.data || (!bgra && image.format!=CV::MotionPixelFormat::Gray8) || image.width<=0 || image.height<=0 || image.width>16384 || image.height>16384
				|| pixelCount>64ULL*1024*1024 || image.strideBytes<rowBytes || (rows && image.strideBytes>((std::numeric_limits<std::size_t>::max)()-rowBytes)/rows)
				|| rows*image.strideBytes+rowBytes>image.sizeBytes || !std::isfinite(input.timestampSeconds) || input.sequence!=metadata.identity.sequence
				|| (metadata.operation==Operation::AwaitTracking)!=(motion==nullptr))
			{ recording->Seal(RecordingStop::InvalidInput); return; }
			const auto& inputs=recording->data->inputs;
			if (metadata.identity.businessSession!=recording->session.load()) { recording->Seal(RecordingStop::SessionEnded); return; }
			if (!inputs.empty())
			{
				const auto& first=inputs.front(); const auto& previous=inputs.back();
				if (metadata.identity.captureSession!=first.metadata.identity.captureSession || metadata.identity.displayGeneration!=first.metadata.identity.displayGeneration)
				{ recording->Seal(RecordingStop::IdentityChanged); return; }
				if (input.sequence!=previous.metadata.identity.sequence+1 || input.timestampSeconds<=previous.timestampSeconds || metadata.operation==Operation::Initialize)
				{ recording->Seal(RecordingStop::InputGap); return; }
				if (input.timestampSeconds-first.timestampSeconds>=RecordingInfo::MaximumSeconds) { recording->Seal(RecordingStop::TimeLimit); return; }
			}
			if (pixelCount>RecordingInfo::MaximumBytes-recording->bytes.load()) { recording->Seal(RecordingStop::ByteLimit); return; }
			const auto began=Capture::Clock::now(); RecordedInput saved;
			saved.metadata=metadata; saved.metadata.identity.windowGeneration=Generation(); saved.width=image.width; saved.height=image.height;
			saved.originalStrideBytes=image.strideBytes; saved.originalSizeBytes=image.sizeBytes; saved.originalFormat=image.format;
			saved.timestampSeconds=input.timestampSeconds; saved.captureValid=input.captureValid; saved.inputLowInformation=input.inputLowInformation; saved.hasMotion=motion!=nullptr; if (motion) saved.motion=*motion;
			saved.gray.resize(static_cast<std::size_t>(pixelCount));
			if (bgra)
			{
				// 与生产预处理共用 OpenCV 转换；保持全尺寸及 alpha 无关语义，回放只省去同一次灰度化。
				cv::Mat source(image.height,image.width,CV_8UC4,const_cast<std::uint8_t*>(image.data),image.strideBytes);
				cv::Mat gray(image.height,image.width,CV_8UC1,saved.gray.data()); cv::cvtColor(source,gray,cv::COLOR_BGRA2GRAY);
			}
			else for (int y=0;y<image.height;++y) std::memcpy(saved.gray.data()+static_cast<std::size_t>(y)*image.width,image.data+static_cast<std::size_t>(y)*image.strideBytes,static_cast<std::size_t>(image.width));
			saved.copyMs=std::chrono::duration<double,std::milli>(Capture::Clock::now()-began).count();
			recording->data->inputs.push_back(std::move(saved)); recording->bytes.fetch_add(static_cast<std::size_t>(pixelCount));
			recording->count.store(recording->data->inputs.size()); recording->duration.store(input.timestampSeconds-recording->data->inputs.front().timestampSeconds);
			if (recording->requested.load()!=RecordingStop::None) recording->Seal(recording->requested.load());
			else if (recording->count.load()==RecordingInfo::MaximumFrames) recording->Seal(RecordingStop::FrameLimit);
			else if (recording->bytes.load()==RecordingInfo::MaximumBytes) recording->Seal(RecordingStop::ByteLimit);
		}
		catch (...) { recording->Seal(RecordingStop::AllocationFailure); }
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
						status.capture.failureQuality, status.capture.nativeError, status.capture.nativeErrorValid,
						status.capture.failureStage==Capture::FailureStage::Quality && status.capture.failureQualityObserved };
				summary.ownerAppliedSequence = status.ownerAppliedSequence; summary.viewportX = status.viewportX; summary.viewportY = status.viewportY;
				impl_->Append(summary);
			}
			impl_->status = status; impl_->statusValid = true;
		}
		catch (...) { impl_->summaryDrops.fetch_add(1, std::memory_order_relaxed); }
		if (status.state==State::Off || status.state==State::Stopping || status.state==State::Paused)
			if (auto recording=impl_->recording.load(std::memory_order_acquire); recording && recording->state.load()==RecordingState::Recording)
				recording->Finish(RecordingStop::SessionEnded);
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
	const char* OperationName(Operation value) noexcept
	{
		switch (value) { case Operation::Initialize:return "Initialize"; case Operation::Process:return "Process"; case Operation::AwaitTracking:return "AwaitTracking"; }
		return "Unknown";
	}
	const char* RecordingStateName(RecordingState value) noexcept
	{
		switch (value) { case RecordingState::Off:return "Off"; case RecordingState::Armed:return "Armed"; case RecordingState::Recording:return "Recording"; case RecordingState::Complete:return "Complete"; case RecordingState::Failed:return "Failed"; }
		return "Unknown";
	}
	const char* RecordingStopName(RecordingStop value) noexcept
	{
		switch (value)
		{
#define RECORD_STOP(name) case RecordingStop::name:return #name
			RECORD_STOP(None); RECORD_STOP(TimeLimit); RECORD_STOP(FrameLimit); RECORD_STOP(ByteLimit); RECORD_STOP(SessionEnded); RECORD_STOP(UserStopped); RECORD_STOP(Closed);
			RECORD_STOP(InvalidInput); RECORD_STOP(InputGap); RECORD_STOP(IdentityChanged); RECORD_STOP(AllocationFailure); RECORD_STOP(ConcurrentAccess);
#undef RECORD_STOP
		}
		return "Unknown";
	}

}
