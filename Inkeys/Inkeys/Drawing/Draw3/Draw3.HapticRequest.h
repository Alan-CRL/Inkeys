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

	// UI 操作自身可以切换/隐藏画布；点击资格由 Host/开关代际和接触凭据决定。
	inline Context ButtonContext(std::uint64_t run) noexcept { return { run, 0 }; }

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

	// 画布范围变化只终止画布绑定，不能截断刚被选择/隐藏按钮启动的独立反馈。
	inline bool CanvasBindingInvalid(bool bound, bool leave, bool allowed, bool revisionMatches) noexcept
	{
		return bound && (leave || !allowed || !revisionMatches);
	}

	struct CanvasPointerRequest
	{
		std::uint32_t pointerId = 0;
		std::uint32_t cursorId = 0;
		bool eraserHint = false;
		bool inContact = false;
		std::uint64_t revision = 0;
	};

	inline bool SameCanvasBinding(const CanvasPointerRequest& bound, const CanvasPointerRequest& request) noexcept
	{
		return request.pointerId && request.cursorId && request.revision && bound.pointerId == request.pointerId &&
			bound.cursorId == request.cursorId && bound.revision == request.revision;
	}
	// Pointer 范围身份与 RTS 笔划分开：普通抬笔停止播放，但下一笔仍可核对同一 tablet cursor。
	class CanvasPointerMailbox
	{
	public:
		struct State { CanvasPointerRequest pending; bool leave; };
		State Inspect() const noexcept { std::lock_guard lock(mutex_); return { pending_, leave_ }; }
		std::uint64_t Revision() const noexcept { return revision_.load(std::memory_order_acquire); }
		void Reset() noexcept
		{
			std::lock_guard lock(mutex_);
			if (range_.pointerId || pending_.pointerId || contactValid_) ClearRange();
			activeCursor_ = 0; contactValid_ = false;
		}
		bool Begin(std::uint32_t cursor, std::uint32_t contact, std::uint64_t generation,
			bool trusted, bool anotherPen, const char** reason = nullptr, std::uint32_t context = 0, std::uint64_t sourceGeneration = 0) noexcept
		{
			std::lock_guard lock(mutex_);
			activeCursor_ = trusted && !anotherPen ? cursor : 0;
			contact_ = contact; generation_ = generation; context_ = context; contactValid_ = true;
			contactRangeRevision_ = range_.revision;
			if (!activeCursor_ || (range_.pointerId && (range_.cursorId != activeCursor_ || (sourceGeneration_ && sourceGeneration_ != sourceGeneration))))
			{
				ClearRange();
				if (reason) *reason = anotherPen ? "multiple_pens" : !activeCursor_ ? "unknown_cursor" : "cursor_mismatch";
				return false;
			}
			sourceGeneration_ = sourceGeneration;
			pending_ = range_;
			if (reason) *reason = range_.pointerId ? "matched_range" : "await_range";
			return range_.pointerId != 0;
		}
		bool End(std::uint32_t contact, std::uint64_t generation, bool cancelled, std::uint32_t context = 0) noexcept
		{
			std::lock_guard lock(mutex_);
			// 旧接触迟到的终态不能停止后来一笔或清掉后来进入的范围。
			if (!contactValid_ || contact != contact_ || generation != generation_ || context != context_) return false;
			contactValid_ = false;
			if (range_.pointerId && contactRangeRevision_ != range_.revision) { activeCursor_ = 0; return false; }
			activeCursor_ = 0;
			pending_ = {};
			if (cancelled) ClearRange();
			return true;
		}
		bool Observe(std::uint32_t pointerId, bool eraserHint, bool start, bool inContact,
			std::uint32_t cursorId, const char** reason = nullptr) noexcept
		{
			std::lock_guard lock(mutex_);
			if (!pointerId || !cursorId || !start || (activeCursor_ && activeCursor_ != cursorId))
			{
				if (reason) *reason = !pointerId ? "zero_pointer" : !cursorId ? "unknown_cursor"
					: !start ? "no_new_request" : "cursor_mismatch";
				if (start && (!cursorId || (activeCursor_ && activeCursor_ != cursorId))) ClearRange();
				return false;
			}
			// 同一范围的 Pointer Down 不另建代际；RTS 已负责每笔重新预设。
			if (range_.pointerId == pointerId && range_.cursorId == cursorId && range_.eraserHint == eraserHint)
			{ if (reason) *reason = "same_range"; return false; }
			const bool awaitingRange = !range_.pointerId;
			if (reason) *reason = "queued";
			const auto revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
			range_ = pending_ = { pointerId, cursorId, eraserHint, inContact, revision };
			if (contactValid_ && awaitingRange) contactRangeRevision_ = revision;
			if (!contactValid_) sourceGeneration_ = 0;
			return true;
		}
		bool Take(CanvasPointerRequest& request) noexcept
		{
			std::lock_guard lock(mutex_);
			request = pending_; pending_ = {};
			if (!request.pointerId) return false;
			leave_ = false;
			return true;
		}
		bool TakeLeave() noexcept
		{
			std::lock_guard lock(mutex_);
			const bool result = leave_; leave_ = false; return result;
		}
	private:
		void ClearRange() noexcept
		{
			revision_.fetch_add(1, std::memory_order_acq_rel);
			range_ = pending_ = {}; leave_ = true; sourceGeneration_ = 0;
		}
		mutable std::mutex mutex_;
		std::atomic<std::uint64_t> revision_ = 0;
		CanvasPointerRequest pending_, range_;
		std::uint32_t activeCursor_ = 0, contact_ = 0, context_ = 0;
		std::uint64_t generation_ = 0, sourceGeneration_ = 0, contactRangeRevision_ = 0;
		bool leave_ = false, contactValid_ = false;
	};
	// 只返回丢弃信息；由调用方在 mailbox 锁外输出诊断日志。
	struct ClickTakeDiagnostics
	{
		unsigned dropped = 0;
		std::uint32_t lastPointerId = 0;
		const char* lastReason = "none";
	};
	inline const char* ClickInactiveReason(const ClickSource& source, Context current,
		std::uint64_t now) noexcept
	{
		if (!current.run) return "host_stopped";
		if (source.context != current) return "context_changed";
		if (!source.pointerId) return "missing_pointer";
		if (!source.valid.load(std::memory_order_acquire)) return "cancelled";
		const auto up = source.upAt.load(std::memory_order_acquire);
		if (!up) return "not_released";
		if (now < up || now < source.downAt) return "clock_order";
		if (now - up > ClickLifetimeMs || now - source.downAt > ContactLifetimeMs)
			return "expired";
		return "not_current";
	}
	class ClickMailbox
	{
	public:
		std::uint64_t Run() const noexcept { return run_.load(std::memory_order_acquire); }
		// 按钮开关只更新 Click 代际；不触碰画布范围，快速关开也不能复活旧请求。
		void SetEnabled(bool enabled) noexcept
		{
			std::lock_guard lock(mutex_);
			if (enabled_ == enabled) return;
			enabled_ = enabled;
			Clear();
			run_.store(running_ && enabled_ ? ++serial_ : 0, std::memory_order_release);
		}
		void Start() noexcept
		{
			std::lock_guard lock(mutex_);
			Clear();
			running_ = true;
			run_.store(enabled_ ? ++serial_ : 0, std::memory_order_release);
		}
		void Stop() noexcept
		{
			std::lock_guard lock(mutex_);
			running_ = false;
			run_.store(0, std::memory_order_release);
			Clear();
		}
		bool Publish(const Click& source, Context current, std::uint64_t now,
			const char** rejection = nullptr) noexcept
		{
			std::lock_guard lock(mutex_);
			if (rejection) *rejection = "none";
			if (!enabled_) { if (rejection) *rejection = "disabled"; return false; }
			if (!source) { if (rejection) *rejection = "missing_source"; return false; }
			if (current.run != Run()) { if (rejection) *rejection = "run_changed"; return false; }
			if (!source->Current(current, now))
			{
				if (rejection) *rejection = ClickInactiveReason(*source, current, now);
				return false;
			}
			for (auto& pending : pending_)
			{
				if (pending && !pending->Current(current, now)) pending.reset();
				if (pending) continue;
				if (source->submitted.exchange(true, std::memory_order_acq_rel))
				{
					if (rejection) *rejection = "duplicate";
					return false;
				}
				pending = source;
				return true;
			}
			if (rejection) *rejection = "mailbox_full";
			return false;
		}
		Click Take(Context current, std::uint64_t now,
			ClickTakeDiagnostics* diagnostics = nullptr) noexcept
		{
			std::lock_guard lock(mutex_);
			if (diagnostics) *diagnostics = {};
			for (auto& pending : pending_)
			{
				Click result = std::move(pending);
				if (result && current.run == Run() && result->Current(current, now)) return result;
				if (result && diagnostics)
				{
					++diagnostics->dropped;
					diagnostics->lastPointerId = result->pointerId;
					diagnostics->lastReason = current.run != Run() ? "run_changed"
						: ClickInactiveReason(*result, current, now);
				}
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
		bool enabled_ = true;
		bool running_ = false;
	};
}
