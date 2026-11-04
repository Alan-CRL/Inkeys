#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Inkeys::Window::Detail
{
	[[nodiscard]] constexpr bool CandidatePumpHasWork(std::uint64_t pending, std::uint64_t retiring) noexcept
	{
		return (pending & ~retiring) != 0;
	}

	// UI 只尝试复制固定槽；owner 永远不能持此锁执行 HWND 调用。
	template<class Candidate, class Receipt, std::size_t Count>
	class CandidateMailbox
	{
	public:
		static_assert(Count <= 64);
		static constexpr std::uint64_t Committed = std::uint64_t{1} << 63;
		static constexpr std::uint64_t CancelRequested = std::uint64_t{1} << 62;
		static constexpr std::uint64_t RevisionMask = ~(Committed | CancelRequested);
		struct Work
		{
			std::size_t slot = 0;
			std::uint64_t revision = 0, roleMask = 0;
			Candidate candidate{};
		};
		struct Publication { std::uint64_t revision = 0; bool coalesced = false; std::size_t depth = 0; };

		[[nodiscard]] Publication Publish(std::size_t slot, std::uint64_t roleMask, const Candidate& candidate)
		{
			if (slot >= Count || !roleMask) return {};
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock()) return {};
			if (nextRevision_ == RevisionMask) return {};
			auto& target = slots_[slot];
			const bool coalesced = target.pending;
			const auto revision = ++nextRevision_;
			target.work = { slot, revision, roleMask, candidate };
			target.pending = true;
			pending_.fetch_or(std::uint64_t{1} << slot, std::memory_order_relaxed);
			target.receipt.reset();
			if (!coalesced) ++depth_;
			masks_[slot].store(roleMask, std::memory_order_relaxed);
			for (std::size_t i = 0; i < Count; ++i)
				if (roleMask & (std::uint64_t{1} << i)) roles_[i].store(revision, std::memory_order_release);
			current_[slot].store(revision, std::memory_order_release);
			return { revision, coalesced, depth_ };
		}

		[[nodiscard]] std::optional<Work> Take(std::size_t slot)
		{
			std::scoped_lock lock(mutex_);
			auto& target = slots_[slot];
			if (!target.pending) return {};
			target.pending = false;
			pending_.fetch_and(~(std::uint64_t{1} << slot), std::memory_order_relaxed);
			--depth_;
			return target.work;
		}

		bool Defer(const Work& work)
		{
			std::scoped_lock lock(mutex_);
			auto& target = slots_[work.slot];
			if (target.work.revision != work.revision || !Current(work)) return false;
			if (target.pending) return true;
			target.pending = true;
			++depth_;
			pending_.fetch_or(std::uint64_t{1} << work.slot, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] bool Current(const Work& work) const noexcept
		{
			return Current(work.slot, work.revision);
		}

		[[nodiscard]] bool Current(std::size_t slot, std::uint64_t revision) const noexcept
		{
			if (slot >= Count || !revision) return false;
			const auto state = current_[slot].load(std::memory_order_acquire);
			if ((state & RevisionMask) != revision || (state & CancelRequested)) return false;
			const auto roleMask = masks_[slot].load(std::memory_order_relaxed);
			for (std::size_t i = 0; i < Count; ++i)
				if ((roleMask & (std::uint64_t{1} << i))
					&& roles_[i].load(std::memory_order_acquire) != revision) return false;
			const auto verified = current_[slot].load(std::memory_order_acquire);
			return (verified & RevisionMask) == revision && !(verified & CancelRequested);
		}

		void Cancel(std::size_t slot, std::uint64_t revision) noexcept
		{
			if (slot >= Count) return;
			auto state = current_[slot].load(std::memory_order_acquire);
			if ((state & RevisionMask) == revision && !(state & Committed))
				(void)current_[slot].compare_exchange_strong(state, 0, std::memory_order_acq_rel);
		}
		void RequestCancel(std::size_t slot, std::uint64_t revision) noexcept
		{
			if (slot < Count && revision) (void)current_[slot].compare_exchange_strong(revision, revision | CancelRequested, std::memory_order_acq_rel);
		}

		template<class Commit>
		[[nodiscard]] bool TryCommit(const Work& work, Commit&& commit)
		{
			// 这里只串行化固定 payload / completion phase，禁止在 commit 中调用 HWND。
			std::scoped_lock lock(mutex_);
			if (!Current(work)) return false;
			auto expected = work.revision;
			if (!current_[work.slot].compare_exchange_strong(expected, work.revision | Committed, std::memory_order_acq_rel)) return false;
			if (commit()) return true;
			// 期限先赢时仍由 owner 回滚；保留该槽直到失败回执到达。
			current_[work.slot].store(work.revision | CancelRequested, std::memory_order_release);
			return false;
		}

		// 非 candidate 的真实窗口政策也退休旧回执；新发布仍可沿同一槽继续收敛。
		void InvalidateRoles(std::uint64_t mask) noexcept
		{
			for (std::size_t i = 0; i < Count; ++i)
				if (mask & (std::uint64_t{1} << i)) roles_[i].store(0, std::memory_order_release);
		}
		[[nodiscard]] std::uint64_t PendingMask() const noexcept { return pending_.load(std::memory_order_relaxed); }

		[[nodiscard]] bool Complete(const Work& work, const Receipt& receipt)
		{
			std::scoped_lock lock(mutex_);
			auto& target = slots_[work.slot];
			if (target.work.revision != work.revision) return false;
			target.receipt = receipt;
			return true;
		}

		[[nodiscard]] std::optional<Receipt> TryRead(std::size_t slot, std::uint64_t revision) const
		{
			if (slot >= Count) return {};
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock() || slots_[slot].work.revision != revision
				|| !Current(slots_[slot].work)) return {};
			return slots_[slot].receipt;
		}
		[[nodiscard]] std::optional<Receipt> TryReadRetirement(std::size_t slot, std::uint64_t revision) const
		{
			if (slot >= Count) return {};
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock() || slots_[slot].work.revision != revision) return {};
			return slots_[slot].receipt;
		}

		[[nodiscard]] std::size_t Depth() const
		{
			return depth_.load(std::memory_order_relaxed);
		}

		void Reset()
		{
			std::scoped_lock lock(mutex_);
			for (auto& slot : slots_) slot = {};
			for (auto& revision : current_) revision.store(0, std::memory_order_release);
			for (auto& revision : roles_) revision.store(0, std::memory_order_release);
			depth_ = 0;
			pending_.store(0, std::memory_order_relaxed);
		}
	private:
		struct Slot { Work work{}; bool pending = false; std::optional<Receipt> receipt; };
		mutable std::mutex mutex_;
		std::array<Slot, Count> slots_{};
		std::array<std::atomic<std::uint64_t>, Count> current_{}, roles_{}, masks_{};
		std::uint64_t nextRevision_ = 0;
		std::atomic<std::size_t> depth_ = 0;
		std::atomic<std::uint64_t> pending_ = 0;
	};
}
