#pragma once

#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Inkeys::Drawing::Draw3::Follow
{
	// 旧Capture终态会保留；只有成功Start后的同代快照属于当前控制会话。
	constexpr bool OwnsCaptureSession(bool started, std::uint64_t owned, std::uint64_t observed) noexcept
	{ return started && owned != 0 && owned == observed; }
	enum class Disposition : std::uint8_t { Restore, RetireScene, Shutdown };
	enum class Phase : std::uint8_t { Off, Beginning, Active, Ending, Presented, Complete, Retired, Failed };
	struct Identity
	{
		std::array<std::uint8_t, 16> workspace{}, page{};
		std::uint64_t host = 0, scene = 0, device = 0, geometry = 0;
		int width = 0, height = 0;
		friend bool operator==(const Identity&, const Identity&) = default;
	};
	struct Timing
	{
		std::uint64_t sequence = 0;
		std::chrono::steady_clock::time_point readCompletedAt{}, publishedAt{}, appliedAt{}, presentedAt{};
	};
	struct Target
	{
		std::uint64_t session = 0, sequence = 0;
		Identity identity{};
		double x = 0, y = 0; // 原图内容位移，已经且仅已经映射一次到 client px。
		std::chrono::steady_clock::time_point readCompletedAt{}, publishedAt{};
	};
	struct Snapshot
	{
		std::uint64_t session = 0, applied = 0, rejected = 0, endFrames = 0, recoveryFrames = 0;
		Phase phase = Phase::Off;
		Disposition disposition = Disposition::Restore;
		Identity identity{};
		Target latest{}, final{};
		double baseX = 0, baseY = 0, viewportX = 0, viewportY = 0;
		bool publishing = false, endQueued = false, handoff = false, clamped = false;
		Timing appliedTiming{}, presentedTiming{};
	};
	// 固定大小共享邮箱；图像计算及 HWND/GPU 调用都在此短门之外。
	class Channel
	{
	public:
		Snapshot Read() const noexcept { std::scoped_lock lock(mutex_); return state_; }
		bool Begin(std::uint64_t session) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (!session || BlockedLocked()) return false;
			state_ = {}; state_.session = session; state_.phase = Phase::Beginning;
			return true;
		}
		bool OwnerBegin(std::uint64_t session, const Identity& identity, double x, double y) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session != session || state_.phase != Phase::Beginning) return false;
			state_.identity = identity; state_.baseX = state_.viewportX = x;
			state_.baseY = state_.viewportY = y;
			state_.latest = { state_.session, 0, identity, 0, 0 };
			state_.final = state_.latest; state_.phase = Phase::Active;
			state_.publishing = !state_.endQueued;
			return true;
		}
		bool Publish(const Target& target) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (!state_.publishing || target.session != state_.session ||
				target.identity != state_.identity || target.sequence <= state_.latest.sequence ||
				!std::isfinite(target.x) || !std::isfinite(target.y))
			{ ++state_.rejected; return false; }
			state_.latest = target; state_.latest.publishedAt = std::chrono::steady_clock::now(); return true;
		}
		// 与 Publish 同锁冻结 final，晚到的 LK 结果没有第三种提交次序。
		bool Seal(Disposition disposition) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (!BlockedLocked() && !(state_.phase == Phase::Failed && !state_.endQueued)) return false;
			if (disposition > state_.disposition) state_.disposition = disposition;
			state_.publishing = false;
			if (state_.endQueued) return false;
			state_.final = state_.latest; state_.endQueued = true;
			return true;
		}
		void OwnerEnd(std::uint64_t session) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session == session && BlockedLocked()) state_.phase = Phase::Ending;
		}
		std::optional<Target> Pending(bool ending) const noexcept
		{
			std::scoped_lock lock(mutex_);
			const auto& target = ending ? state_.final : state_.latest;
			if ((state_.phase != Phase::Active && state_.phase != Phase::Ending) ||
				target.session != state_.session || target.sequence <= state_.applied) return {};
			return target;
		}
		void Applied(std::uint64_t session, std::uint64_t sequence, double x, double y, bool clamped, const Target* source = nullptr) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session != session) return;
			state_.applied = sequence; state_.viewportX = x; state_.viewportY = y;
			state_.clamped |= clamped;
			// Pending 后可能已有更新发布，必须使用实际应用的目标，不能读取 latest 冒充同帧。
			state_.appliedTiming = {}; state_.appliedTiming.sequence = sequence;
			state_.appliedTiming.appliedAt = std::chrono::steady_clock::now();
			if (source && source->session == session && source->sequence == sequence)
			{ state_.appliedTiming.readCompletedAt = source->readCompletedAt; state_.appliedTiming.publishedAt = source->publishedAt; }
		}
		void ObservePresent(std::uint64_t session, std::chrono::steady_clock::time_point returnedAt) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session != session || !state_.applied || state_.presentedTiming.sequence == state_.applied || returnedAt < state_.appliedTiming.appliedAt) return;
			// 仅绘制 owner 在重放完成且 Present 成功后调用；时间不代表 compositor 的真实显示时刻。
			state_.presentedTiming = state_.appliedTiming;
			state_.presentedTiming.presentedAt = returnedAt;
		}
		void RecoveryFrame(std::uint64_t session, bool pending) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session != session) return;
			++state_.endFrames; if (pending) ++state_.recoveryFrames;
		}
		void Presented(std::uint64_t session) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session == session && state_.phase == Phase::Ending && state_.applied == state_.final.sequence)
				state_.phase = Phase::Presented;
		}
		void Handoff(std::uint64_t session) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.session == session && state_.phase == Phase::Presented) state_.handoff = true;
		}
		bool BarrierReady() noexcept
		{
			std::scoped_lock lock(mutex_);
			if (state_.disposition != Disposition::Restore ||
				(state_.phase == Phase::Presented && state_.handoff))
			{
				state_.phase = state_.disposition == Disposition::Restore ? Phase::Complete : Phase::Retired;
				return true;
			}
			return state_.phase == Phase::Failed || state_.phase == Phase::Retired;
		}
		void Fail(std::uint64_t session = 0) noexcept
		{
			std::scoped_lock lock(mutex_); if (session && session != state_.session) return; state_.publishing = false; state_.phase = Phase::Failed;
		}
		bool Blocked() const noexcept { std::scoped_lock lock(mutex_); return BlockedLocked(); }
	private:
		bool BlockedLocked() const noexcept
		{
			return state_.phase == Phase::Beginning || state_.phase == Phase::Active ||
				state_.phase == Phase::Ending || state_.phase == Phase::Presented;
		}
		mutable std::mutex mutex_;
		Snapshot state_{};
	};
}
