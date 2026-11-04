#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace Inkeys::Window::Detail
{
	// 少量 Pointer Down / 根刷新事件的固定 latest 邮箱；owner 读槽时不占用生产者槽。
	template<class Payload>
	class NativeEventMailbox
	{
	public:
		struct Work { std::uint64_t revision = 0; Payload payload{}; };
		[[nodiscard]] std::uint64_t Publish(const Payload& payload) noexcept
		{
			for (std::size_t i = 0; i < slots_.size(); ++i)
			{
				auto& slot = slots_[i]; std::uint64_t free = 0;
				if (!slot.state.compare_exchange_strong(free, Writing, std::memory_order_acq_rel)) continue;
				slot.payload = payload;
				const auto ticket = next_.fetch_add(1, std::memory_order_relaxed) + 1;
				if (ticket > ((Reading - 1) >> 4)) { slot.state.store(0, std::memory_order_release); return 0; }
				const auto revision = (ticket << 4) | (i + 1);
				const auto previous = latest_.exchange(revision, std::memory_order_acq_rel);
				slot.state.store(revision, std::memory_order_release);
				if (previous)
				{
					auto& old = slots_[(previous & 15) - 1]; auto published = previous;
					(void)old.state.compare_exchange_strong(published, 0, std::memory_order_acq_rel);
				}
				// 并发生产者可能已经发布更新意图；仅回收自己仍未被读取的旧槽。
				if (latest_.load(std::memory_order_acquire) != revision)
				{ auto published = revision; (void)slot.state.compare_exchange_strong(published, 0, std::memory_order_acq_rel); }
				return revision;
			}
			return 0;
		}
		[[nodiscard]] std::optional<Work> Take() noexcept
		{
			const auto revision = latest_.load(std::memory_order_acquire);
			if (!revision) return {};
			auto& slot = slots_[(revision & 15) - 1]; auto published = revision;
			if (!slot.state.compare_exchange_strong(published, revision | Reading, std::memory_order_acq_rel)) return {};
			std::optional<Work> result;
			if (latest_.load(std::memory_order_acquire) == revision) result = Work{ revision, slot.payload };
			slot.state.store(0, std::memory_order_release);
			return result;
		}
		[[nodiscard]] bool Current(std::uint64_t revision) const noexcept
		{ return revision && latest_.load(std::memory_order_acquire) == revision; }
		void Invalidate() noexcept
		{
			const auto previous = latest_.exchange(0, std::memory_order_acq_rel);
			if (previous)
			{ auto expected = previous; (void)slots_[(previous & 15) - 1].state.compare_exchange_strong(expected, 0, std::memory_order_acq_rel); }
		}
	private:
		static constexpr std::uint64_t Reading = std::uint64_t{1} << 63;
		static constexpr std::uint64_t Writing = ~std::uint64_t{0};
		struct Slot { std::atomic<std::uint64_t> state = 0; Payload payload{}; };
		std::array<Slot, 8> slots_{};
		std::atomic<std::uint64_t> next_ = 0, latest_ = 0;
	};
}
