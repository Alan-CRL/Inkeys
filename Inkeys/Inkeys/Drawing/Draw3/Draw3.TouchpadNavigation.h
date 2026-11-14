#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <algorithm>
#include <vector>

namespace Inkeys::Drawing::Draw3
{
	enum class TouchpadNavigationPhase : std::uint8_t { Running, Inertia, Ready, Cancelled };
	struct TouchpadNavigationSample
	{
		std::uint64_t epoch = 0, sequence = 0;
		std::int64_t qpc = 0;
		double cumulativeX = 0.0, cumulativeY = 0.0;
		TouchpadNavigationPhase phase = TouchpadNavigationPhase::Ready;
		std::uint64_t admissionRevision = 0;
	};

	namespace Detail
	{
		// 真正的 COM 停止和无窗口故障注入共用同一退休顺序；失败保留未完成的 owner 责任。
		struct TouchpadNavigationRetirement
		{
			bool handlerRegistered = false, viewportAbandoned = false, managerActivated = false;
			template<class Remove, class Abandon, class Deactivate>
			bool Complete(Remove remove, Abandon abandon, Deactivate deactivate) noexcept
			{
				if (handlerRegistered)
				{
					if (!remove()) return false;
					handlerRegistered = false;
				}
				if (!viewportAbandoned)
				{
					if (!abandon()) return false;
					viewportAbandoned = true;
				}
				if (managerActivated)
				{
					if (!deactivate()) return false;
					managerActivated = false;
				}
				return true;
			}
		};

		// 仅数值跨线程；同阶段累计值可合并，阶段边沿和终态不能被 Move 覆盖。
		class TouchpadNavigationBridge
		{
		public:
			using Wake = void(*)(void*);
			std::atomic_bool enabled = false, inertiaAllowed = true;
			void SetWake(Wake wake, void* context) noexcept
			{
				std::scoped_lock lock(mutex_); wake_ = wake; context_ = context;
			}
			std::uint64_t Cancel(std::int64_t takeoverQpc = 0) noexcept
			{
				std::scoped_lock lock(mutex_);
				const auto epoch = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
				if (takeoverQpc > 0)
				{
					// 已准入来源在过去的 Down 接手：作废其后预先排队的 native 区间，保留此前重叠证据。
					std::array<RunningInterval, 64> retained{};
					std::size_t kept = 0;
					for (std::size_t i = 0; i != runningCount_; ++i)
					{
						auto interval = runningIntervals_[(runningFirst_ + i) % runningIntervals_.size()];
						if (interval.begin >= takeoverQpc) continue;
						if (!interval.end || interval.end > takeoverQpc) interval.end = takeoverQpc;
						retained[kept++] = interval;
					}
					runningIntervals_ = retained; runningFirst_ = 0; runningCount_ = kept;
					unknownThrough_ = (std::min)(unknownThrough_, takeoverQpc - 1);
					lastPublishedQpc_ = (std::min)(lastPublishedQpc_, takeoverQpc);
				}
				else CloseRunning(lastPublishedQpc_);
				count_ = 0; known_ = false; active_.store(false); running_.store(false); startQpc_.store(0); interaction_.store(false);
				if (wake_) wake_(context_);
				return epoch;
			}
			bool Publish(std::uint64_t epoch, TouchpadNavigationPhase phase,
				double x, double y, std::int64_t qpc, std::uint64_t admissionRevision = 0) noexcept
			{
				if (!std::isfinite(x) || !std::isfinite(y) || qpc <= 0) return false;
				std::scoped_lock lock(mutex_);
				if (epoch != epoch_.load(std::memory_order_acquire)) return false;
				if (qpc < lastPublishedQpc_) return false;
				lastPublishedQpc_ = qpc;
				const bool active = phase == TouchpadNavigationPhase::Running || phase == TouchpadNavigationPhase::Inertia;
				const bool edge = !known_ || lastPhase_ != phase;
				if (phase == TouchpadNavigationPhase::Running && (!active_.load() || !running_.load()))
				{
					startQpc_.store(qpc, std::memory_order_release);
					gestureAdmissionRevision_ = admissionRevision;
					if (runningCount_ == runningIntervals_.size())
					{
						// 查询所需历史若已淘汰，截止前的 Down 保守拒收，不猜“过去没有重叠”。
						unknownThrough_ = (std::max)(unknownThrough_, runningIntervals_[runningFirst_].end);
						runningFirst_ = (runningFirst_ + 1) % runningIntervals_.size(); --runningCount_;
					}
					runningIntervals_[(runningFirst_ + runningCount_++) % runningIntervals_.size()] = { qpc, 0, admissionRevision };
				}
				else if (phase != TouchpadNavigationPhase::Running) CloseRunning(qpc);
				active_.store(active, std::memory_order_release);
				running_.store(phase == TouchpadNavigationPhase::Running, std::memory_order_release);
				TouchpadNavigationSample value{ epoch, ++sequence_, qpc, x, y, phase, gestureAdmissionRevision_ };
				if (count_ && samples_[count_ - 1].phase == phase) samples_[count_ - 1] = value;
				else if (count_ < samples_.size()) samples_[count_++] = value;
				else
				{
					// 边沿溢出取消整代；不能丢 READY 后把旧手势当作仍在拖动。
					epoch_.fetch_add(1, std::memory_order_acq_rel); count_ = 0; known_ = false;
					CloseRunning(qpc); unknownThrough_ = (std::max)(unknownThrough_, qpc);
					active_.store(false); running_.store(false); startQpc_.store(0); interaction_.store(false);
					if (wake_) wake_(context_);
					return false;
				}
				known_ = true; lastPhase_ = phase;
				if (edge && wake_) wake_(context_);
				return true;
			}
			std::uint64_t Drain(std::vector<TouchpadNavigationSample>& output)
			{
				output.clear(); output.reserve(samples_.size()); // 分配在锁外。
				std::scoped_lock lock(mutex_);
				output.insert(output.end(), samples_.begin(), samples_.begin() + count_); count_ = 0;
				return epoch_.load(std::memory_order_acquire);
			}
			std::uint64_t Epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
			bool Active() const noexcept { return active_.load(std::memory_order_acquire); }
			bool NeedsUpdate() const noexcept { return Active() || interaction_.load(std::memory_order_acquire); }
			void SetInteractionActive(bool active, std::uint64_t epoch) noexcept
			{
				std::scoped_lock lock(mutex_);
				if (epoch != epoch_.load(std::memory_order_acquire)) return;
				if (interaction_.exchange(active, std::memory_order_acq_rel) != active && wake_) wake_(context_);
			}
			bool Running() const noexcept { return running_.load(std::memory_order_acquire); }
			bool RunningAt(std::int64_t qpc, std::uint64_t admissionRevision = 0) const noexcept
			{
				if (qpc <= 0) return Running();
				std::scoped_lock lock(mutex_);
				if (qpc <= unknownThrough_) return true;
				for (std::size_t i = 0; i != runningCount_; ++i)
				{
					const auto& interval = runningIntervals_[(runningFirst_ + i) % runningIntervals_.size()];
					if (interval.admissionRevision == admissionRevision && qpc >= interval.begin &&
						(!interval.end || qpc <= interval.end)) return true;
				}
				return false;
			}
			std::int64_t StartQpc() const noexcept { return startQpc_.load(std::memory_order_acquire); }
		private:
			void CloseRunning(std::int64_t qpc) noexcept
			{
				if (!runningCount_) return;
				auto& interval = runningIntervals_[(runningFirst_ + runningCount_ - 1) % runningIntervals_.size()];
				if (!interval.end) interval.end = (std::max)(interval.begin, qpc);
			}
			mutable std::mutex mutex_;
			struct RunningInterval { std::int64_t begin = 0, end = 0; std::uint64_t admissionRevision = 0; };
			std::array<RunningInterval, 64> runningIntervals_{};
			std::size_t runningFirst_ = 0, runningCount_ = 0;
			std::int64_t unknownThrough_ = 0, lastPublishedQpc_ = 0;
			std::uint64_t gestureAdmissionRevision_ = 0;
			std::array<TouchpadNavigationSample, 64> samples_{};
			std::size_t count_ = 0;
			std::uint64_t sequence_ = 0;
			std::atomic<std::uint64_t> epoch_ = 1;
			std::atomic_bool active_ = false, running_ = false, interaction_ = false;
			std::atomic<std::int64_t> startQpc_ = 0;
			bool known_ = false;
			TouchpadNavigationPhase lastPhase_ = TouchpadNavigationPhase::Ready;
			Wake wake_ = nullptr;
			void* context_ = nullptr;
		};

		// Owner 前端与故障注入共用：关联成功不等于已识别，只有真实状态能发布 Running。
		class TouchpadNavigationFrontend
		{
		public:
			explicit TouchpadNavigationFrontend(TouchpadNavigationBridge& bridge) noexcept
				: bridge_(bridge), epoch_(bridge.Epoch()) {}
			template<class SetContact>
			bool Associate(std::uint64_t epoch, std::uint64_t admissionRevision,
				std::uint32_t pointer, SetContact setContact)
			{
				if (!bridge_.enabled.load(std::memory_order_acquire) || epoch != bridge_.Epoch()) return false;
				const bool continuing = Accepting() && bridge_.Active();
				const auto association = ++association_;
				epoch_ = epoch; admissionRevision_ = admissionRevision; accepting_ = true;
				if (!continuing) { phaseKnown_ = false; phase_ = TouchpadNavigationPhase::Ready; }
				// 先建立接纳身份，以允许 SetContact 内同步到来的真实状态/interaction 回调。
				if (!setContact(pointer))
				{
					if (association == association_ && epoch == bridge_.Epoch()) Cancel();
					return false;
				}
				return association == association_ && epoch == bridge_.Epoch();
			}
			void Interaction(bool begin) noexcept
			{
				if (!Accepting()) return;
				bridge_.SetInteractionActive(begin, epoch_);
				if (!begin && !bridge_.Active()) { accepting_ = false; phaseKnown_ = false; }
			}
			bool CanPublishStatus(TouchpadNavigationPhase phase) const noexcept
			{
				return Accepting() && (phase == TouchpadNavigationPhase::Running ||
					phase == TouchpadNavigationPhase::Cancelled || HasConfirmedPhase());
			}
			bool PublishStatus(TouchpadNavigationPhase phase, double x, double y, std::int64_t qpc) noexcept
			{
				if (!CanPublishStatus(phase)) return false;
				if (!bridge_.Publish(epoch_, phase, x, y, qpc, admissionRevision_)) { Cancel(); return false; }
				phase_ = phase;
				phaseKnown_ = phase == TouchpadNavigationPhase::Running || phase == TouchpadNavigationPhase::Inertia;
				if (!phaseKnown_) { accepting_ = false; bridge_.SetInteractionActive(false, epoch_); }
				return true;
			}
			void Reset(std::uint64_t epoch) noexcept
			{
				++association_; epoch_ = epoch; accepting_ = false; phaseKnown_ = false; phase_ = TouchpadNavigationPhase::Ready;
				bridge_.SetInteractionActive(false, epoch_);
			}
			void Cancel() noexcept { Reset(bridge_.Cancel()); }
			bool Accepting() const noexcept { return accepting_ && epoch_ == bridge_.Epoch(); }
			bool HasConfirmedPhase() const noexcept { return Accepting() && phaseKnown_; }
			std::uint64_t Epoch() const noexcept { return epoch_; }
			std::uint64_t AdmissionRevision() const noexcept { return admissionRevision_; }
			TouchpadNavigationPhase Phase() const noexcept { return phase_; }
		private:
			TouchpadNavigationBridge& bridge_;
			std::uint64_t epoch_ = 0, admissionRevision_ = 0, association_ = 0;
			TouchpadNavigationPhase phase_ = TouchpadNavigationPhase::Ready;
			bool accepting_ = false, phaseKnown_ = false;
		};
	}
}
