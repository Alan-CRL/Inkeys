#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace Inkeys::Drawing::Draw3::Haptics
{
	inline constexpr std::uint64_t ClickLifetimeMs = 250;
	inline constexpr std::uint64_t ContactLifetimeMs = 5000;
	struct Context
	{
		std::uint64_t run = 0;
		std::uint64_t canvas = 0;
		bool operator==(const Context&) const = default;
	};

	// 同一接触的消息、按下上下文和绘制请求共用凭据，取消不依赖队列空位。
	struct ClickSource
	{
		Context context;
		std::uint32_t pointerId = 0;
		std::uint64_t downAt = 0;
		std::atomic<std::uint64_t> upAt = 0;
		std::atomic_bool valid = true;
		std::atomic_bool submitted = false;

		bool Current(Context current, std::uint64_t now) const noexcept
		{
			const auto up = upAt.load(std::memory_order_acquire);
			return current.run && context == current && pointerId && up && now >= up &&
				now - up <= ClickLifetimeMs && now >= downAt &&
				now - downAt <= ContactLifetimeMs && valid.load(std::memory_order_acquire);
		}
	};
	using Click = std::shared_ptr<ClickSource>;

	inline bool ContinuousAllowed(bool selection, bool visible, bool laser, bool blocked,
		bool quarantined, bool pan, bool suppressed) noexcept
	{
		return !selection && visible && !laser && !blocked && !quarantined && !pan && !suppressed;
	}

	struct CanvasPointerRequest
	{
		std::uint32_t pointerId = 0;
		bool eraserHint = false;
		bool inContact = false;
		std::uint64_t revision = 0;
	};

	inline bool ContinuousCandidateAllowed(unsigned penContacts, bool inContact, bool waveformMatches) noexcept
	{
		return penContacts <= 1 && (penContacts == 0 || (inContact && waveformMatches));
	}

	// RTS 与 Pointer 无通用身份换算：新 RTS Down 清掉旧请求，只等随后明确的接触消息。
	class CanvasPointerMailbox
	{
	public:
		struct State { CanvasPointerRequest pending; bool leave; };
		State Inspect() const noexcept
		{
			std::lock_guard lock(mutex_);
			return { pending_, leave_ };
		}
		std::uint64_t Revision() const noexcept { return revision_.load(std::memory_order_acquire); }
		void Reset(bool requireContact = false) noexcept
		{
			std::lock_guard lock(mutex_);
			revision_.fetch_add(1, std::memory_order_acq_rel);
			pending_ = {};
			leave_ = true;
			requireContact_ = requireContact;
		}
		bool Observe(std::uint32_t pointerId, bool eraserHint, bool start, bool inContact) noexcept
		{
			std::lock_guard lock(mutex_);
			if (!pointerId || (!start && !requireContact_) || (requireContact_ && !inContact)) return false;
			const auto revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
			pending_ = { pointerId, eraserHint, inContact, revision };
			requireContact_ = false;
			return true;
		}
		bool Take(CanvasPointerRequest& request) noexcept
		{
			std::lock_guard lock(mutex_);
			request = pending_;
			pending_ = {};
			if (!request.pointerId) return false;
			// 消费新请求时会先停止旧反馈，同锁结算此前的 leave，避免下一帧误停新绑定。
			leave_ = false;
			return true;
		}
		bool TakeLeave() noexcept
		{
			std::lock_guard lock(mutex_);
			const bool result = leave_;
			leave_ = false;
			return result;
		}
	private:
		mutable std::mutex mutex_;
		std::atomic<std::uint64_t> revision_ = 0;
		CanvasPointerRequest pending_;
		bool leave_ = false;
		bool requireContact_ = false;
	};

	class ClickMailbox
	{
	public:
		std::uint64_t Run() const noexcept { return run_.load(std::memory_order_acquire); }
		void Start() noexcept
		{
			std::lock_guard lock(mutex_);
			Clear();
			run_.store(++serial_, std::memory_order_release);
		}
		void Stop() noexcept
		{
			std::lock_guard lock(mutex_);
			run_.store(0, std::memory_order_release);
			Clear();
		}
		bool Publish(const Click& source, Context current, std::uint64_t now) noexcept
		{
			std::lock_guard lock(mutex_);
			if (!source || current.run != Run() || !source->Current(current, now)) return false;
			for (auto& pending : pending_)
			{
				if (pending && !pending->Current(current, now)) pending.reset();
				if (pending) continue;
				if (source->submitted.exchange(true, std::memory_order_acq_rel)) return false;
				pending = source;
				return true;
			}
			return false;
		}
		Click Take(Context current, std::uint64_t now) noexcept
		{
			std::lock_guard lock(mutex_);
			for (auto& pending : pending_)
			{
				Click result = std::move(pending);
				if (result && current.run == Run() && result->Current(current, now)) return result;
			}
			return {};
		}
	private:
		void Clear() noexcept
		{
			for (auto& pending : pending_)
			{
				if (pending) pending->valid.store(false, std::memory_order_release);
				pending.reset();
			}
		}
		std::mutex mutex_;
		std::array<Click, 8> pending_{};
		std::atomic<std::uint64_t> run_ = 0;
		std::uint64_t serial_ = 0;
	};
}
