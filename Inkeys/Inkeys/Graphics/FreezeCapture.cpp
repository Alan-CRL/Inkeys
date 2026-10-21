#include "FreezeCapture.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace Inkeys::Graphics::FreezeCapture
{
	namespace
	{
		using namespace std::chrono_literals;
		constexpr auto frameInterval = 50ms;
		constexpr auto firstFrameDelay = 800ms;
		constexpr auto retryDelay = 100ms;
		constexpr auto steadyDelay = 10ms;
		constexpr auto startupGrace = 5s;
		constexpr auto resultTimeout = 1s;
	}

	bool IsValidGeometry(const Geometry& geometry) noexcept
	{
		const auto& primary = geometry.primaryBounds;
		const auto& source = geometry.sourceRect;
		const auto& work = geometry.workArea;
		// 本入口只复用原主屏 0,0 的定格几何，不静默缩放或重设已有窗口尺寸。
		return primary.left == 0 && primary.top == 0 && primary.right > 1 && primary.bottom > 1
			&& primary.right <= 32768 && primary.bottom <= 32768
			&& geometry.width == primary.right && geometry.height == primary.bottom - 1
			&& static_cast<std::uint64_t>(geometry.width) * geometry.height <= 64ULL * 1024 * 1024
			&& source == Rect{ 0, 0, primary.right - 1, primary.bottom - 1 }
			&& work.left >= 0 && work.top >= 0 && work.right <= primary.right && work.bottom <= primary.bottom
			&& work.right > work.left && work.bottom > work.top && geometry.displayGeneration != 0;
	}

	Quality Analyze(std::span<const std::uint32_t> pixels, int width, int height) noexcept
	{
		Quality result;
		if (width <= 0 || height <= 0
			|| static_cast<std::uint64_t>(width) * height != pixels.size()) return result;
		const int columns = std::min(width, 64), rows = std::min(height, 64);
		std::array<double, 3> sum{}, squares{};
		std::uint32_t black = 0;
		for (int row = 0; row < rows; ++row)
			for (int column = 0; column < columns; ++column)
			{
				const int x = static_cast<int>((2LL * column + 1) * width / (2LL * columns));
				const int y = static_cast<int>((2LL * row + 1) * height / (2LL * rows));
				const auto pixel = pixels[static_cast<std::size_t>(y) * width + x];
				const std::array<unsigned, 3> rgb = { (pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255 };
				for (std::size_t channel = 0; channel < rgb.size(); ++channel)
				{
					sum[channel] += rgb[channel];
					squares[channel] += rgb[channel] * rgb[channel];
				}
				if (std::max({ rgb[0], rgb[1], rgb[2] }) <= 2) ++black;
			}
		result.samples = static_cast<std::uint32_t>(columns * rows);
		result.blackFraction = static_cast<double>(black) / result.samples;
		result.meanLuma = (sum[0] * 0.2126 + sum[1] * 0.7152 + sum[2] * 0.0722) / result.samples;
		for (std::size_t channel = 0; channel < sum.size(); ++channel)
		{
			const double mean = sum[channel] / result.samples;
			result.variance = std::max(result.variance, squares[channel] / result.samples - mean * mean);
		}
		result.status = result.blackFraction >= 0.995 ? QualityStatus::NearBlack
			: result.variance < 2.0 ? QualityStatus::LowInformation : QualityStatus::Valid;
		return result;
	}

	Session::Session(Wake wake, Now now) noexcept : wake_(wake), now_(now) {}

	void Session::WakeWorker() const noexcept
	{
		if (wake_) wake_();
	}

	void Session::RevokeLocked(StopReason reason) noexcept
	{
		if (state_.publishing)
		{
			state_.publishing = false;
			state_.reason = reason;
		}
	}

	void Session::SetReady(bool ready) noexcept
	{
		{
			std::scoped_lock lock(mutex_);
			state_.ready = ready;
			windowAccess_ = ready;
			if (!ready) RevokeLocked(StopReason::Shutdown);
		}
		monitor_.notify_all();
		WakeWorker();
	}

	void Session::SetFreezeState(std::uint64_t revision, bool active, bool available) noexcept
	{
		{
			std::scoped_lock lock(mutex_);
			if (revision < freezeRevision_) return;
			freezeRevision_ = revision;
			freezeBlocked_ = active || !available;
			if (freezeBlocked_) RevokeLocked(StopReason::FreezeActive);
		}
		monitor_.notify_all();
		WakeWorker();
	}

	StartResult Session::Start(const Geometry& geometry, FrameCallback callback) noexcept
	{
		if (!callback || !IsValidGeometry(geometry)) return { StartStatus::InvalidArgument };
		StartResult result;
		{
			std::scoped_lock lock(mutex_);
			if (state_.occupied) return { StartStatus::Busy, state_.session };
			if (!state_.ready || !windowAccess_) return { StartStatus::NotReady };
			if (freezeBlocked_) return { StartStatus::FreezeActive };
			const auto session = state_.session + 1;
			state_ = {};
			state_.ready = state_.publishing = state_.occupied = true;
			state_.session = session;
			state_.geometry = geometry;
			callback_ = std::move(callback);
			started_ = due_ = nextSlot_ = now_();
			lastResult_ = {};
			phase_ = Phase::Begin;
			backendBegun_ = false;
			result = { StartStatus::Started, session };
		}
		monitor_.notify_all();
		WakeWorker();
		return result;
	}

	void Session::Stop(StopReason reason) noexcept
	{
		{
			std::scoped_lock lock(mutex_);
			RevokeLocked(reason);
		}
		monitor_.notify_all();
		WakeWorker();
	}

	Snapshot Session::GetSnapshot() const noexcept
	{
		std::scoped_lock lock(mutex_);
		return state_;
	}

	TimePoint Session::DeadlineLocked() const noexcept
	{
		return state_.statistics.accepted ? lastResult_ + resultTimeout : started_ + startupGrace;
	}

	std::chrono::milliseconds Session::NextDelay() const noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!state_.occupied) return std::chrono::milliseconds::max();
		if (!state_.publishing) return 0ms;
		return std::max(0ms, std::chrono::ceil<std::chrono::milliseconds>(due_ - now_()));
	}

	bool Session::DisableWindowAccess() noexcept
	{
		bool canUninitialize = false;
		{
			std::scoped_lock lock(mutex_);
			state_.ready = windowAccess_ = false;
			RevokeLocked(StopReason::Unavailable);
			canUninitialize = !state_.inFlight;
		}
		monitor_.notify_all();
		WakeWorker();
		return canUninitialize;
	}

	void Session::Drain(Backend& backend) noexcept
	{
		bool endBackend = false, accessWindows = false;
		{
			std::scoped_lock lock(mutex_);
			if (!state_.occupied || state_.publishing || state_.inFlight) return;
			state_.inFlight = true;
			endBackend = backendBegun_;
			accessWindows = windowAccess_;
		}
		const bool cleaned = !endBackend || backend.End(accessWindows);
		FrameCallback released;
		{
			std::scoped_lock lock(mutex_);
			state_.inFlight = false;
			if (!cleaned)
			{
				// 清理调用已返回，但未完全收起的控件不再接受新捕获会话。
				state_.ready = windowAccess_ = false;
				state_.reason = StopReason::BackendFailure;
			}
			backendBegun_ = false;
			state_.occupied = false;
			released = std::move(callback_);
		}
		monitor_.notify_all();
	}

	void Session::Pump(Backend& backend) noexcept
	{
		Supervise();
		Geometry geometry;
		Phase phase;
		{
			std::scoped_lock lock(mutex_);
			if (!state_.occupied || state_.inFlight || !state_.publishing || !windowAccess_ || now_() < due_)
				return;
			state_.inFlight = true;
			phase = phase_;
			geometry = state_.geometry;
			if (phase == Phase::Begin) backendBegun_ = true;
			if (phase == Phase::Read) ++state_.statistics.attempts;
		}
		const auto began = now_();
		std::span<const std::uint32_t> pixels;
		const BackendResult result = phase == Phase::Begin ? backend.Begin(geometry)
			: phase == Phase::Update ? backend.Update(geometry) : backend.Read(geometry, pixels);
		const auto completed = now_();
		const Quality quality = phase == Phase::Read && result.succeeded
			? Analyze(pixels, geometry.width, geometry.height) : Quality{};
		Frame frame;
		bool deliver = false;
		{
			std::scoped_lock lock(mutex_);
			if (phase == Phase::Read)
			{
				state_.statistics.lastReadDuration = std::chrono::duration_cast<std::chrono::microseconds>(completed - began);
				state_.statistics.maxReadDuration = std::max(state_.statistics.maxReadDuration, state_.statistics.lastReadDuration);
				state_.quality = quality;
			}
			if (state_.publishing && completed >= DeadlineLocked())
				RevokeLocked(state_.statistics.accepted ? StopReason::ResultTimeout : StopReason::StartupTimeout);
			if (!state_.publishing || !windowAccess_)
			{
				if (phase == Phase::Read) ++state_.statistics.lateResults;
				state_.inFlight = false;
				return;
			}
			if (!result.succeeded)
			{
				++state_.statistics.apiFailures;
				state_.quality = {};
				if (phase == Phase::Read) ++state_.statistics.rejected;
				if (phase == Phase::Begin || result.failure != StopReason::BackendFailure)
					RevokeLocked(result.failure);
				phase_ = Phase::Update;
				due_ = completed + retryDelay;
			}
			else if (phase == Phase::Begin)
			{
				phase_ = Phase::Update;
				due_ = completed;
			}
			else if (phase == Phase::Update)
			{
				phase_ = Phase::Read;
				due_ = completed + (state_.statistics.attempts == 0 ? firstFrameDelay
					: state_.statistics.accepted == 0 ? retryDelay : steadyDelay);
				if (state_.statistics.attempts == 0) nextSlot_ = due_;
			}
			else
			{
				if (quality.status == QualityStatus::Valid)
				{
					++state_.sequence;
					++state_.statistics.accepted;
					lastResult_ = completed;
					deliver = true;
				}
				else ++state_.statistics.rejected;
				// 只保留一个 DIB，慢调用跳过已经错过的绝对时隙，不积压待捕获帧。
				nextSlot_ += frameInterval;
				if (nextSlot_ <= completed)
				{
					const auto missed = (completed - nextSlot_) / frameInterval + 1;
					nextSlot_ += frameInterval * missed;
					state_.statistics.skipped += static_cast<std::uint64_t>(missed);
				}
				phase_ = Phase::Update;
				due_ = nextSlot_;
				if (deliver)
					frame = { pixels, geometry.width, geometry.height,
						static_cast<std::size_t>(geometry.width) * sizeof(std::uint32_t), geometry,
						1.0, 1.0,
						completed, state_.session, state_.sequence, quality, state_.statistics };
			}
			if (!deliver) state_.inFlight = false;
		}
		monitor_.notify_all();
		if (!deliver) return;
		// 不持状态锁调用消费者；Stop 可立即关门，但已准入的回调必须自然返回。
		try { callback_(frame); }
		catch (...) { Stop(StopReason::CallbackFailure); }
		{
			std::scoped_lock lock(mutex_);
			state_.inFlight = false;
		}
		monitor_.notify_all();
	}

	void Session::Supervise() noexcept
	{
		bool revoked = false;
		{
			std::scoped_lock lock(mutex_);
			if (state_.publishing && now_() >= DeadlineLocked())
			{
				RevokeLocked(state_.statistics.accepted ? StopReason::ResultTimeout : StopReason::StartupTimeout);
				revoked = true;
			}
		}
		if (revoked) WakeWorker();
	}

	void Session::RunSupervisor(std::stop_token stop) noexcept
	{
		std::stop_callback wake(stop, [this]
		{
			// 与等待谓词共用锁，避免停止通知落在谓词检查与真正 wait 之间而丢失。
			std::scoped_lock lock(mutex_);
			monitor_.notify_all();
		});
		std::unique_lock lock(mutex_);
		while (!stop.stop_requested())
		{
			if (!state_.publishing)
			{
				monitor_.wait(lock, [&] { return stop.stop_requested() || state_.publishing; });
				continue;
			}
			if (now_() < DeadlineLocked())
			{
				monitor_.wait_until(lock, DeadlineLocked());
				continue;
			}
			RevokeLocked(state_.statistics.accepted ? StopReason::ResultTimeout : StopReason::StartupTimeout);
			lock.unlock();
			WakeWorker();
			lock.lock();
		}
	}
}
