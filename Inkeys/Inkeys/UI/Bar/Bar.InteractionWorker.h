#pragma once

#include <Windows.h>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>

namespace Inkeys::UI::Bar
{
	// 手势的局部变量留在协程帧里；等待消息只登记续接点，不运行嵌套消息泵。
	template<class T> class InteractionTask
	{
	public:
		struct promise_type;
		using Handle = std::coroutine_handle<promise_type>;
		struct promise_type
		{
			std::optional<T> result;
			std::exception_ptr error;
			std::coroutine_handle<> continuation;
			InteractionTask get_return_object() noexcept { return InteractionTask(Handle::from_promise(*this)); }
			std::suspend_always initial_suspend() const noexcept { return {}; }
			struct Final
			{
				bool await_ready() const noexcept { return false; }
				std::coroutine_handle<> await_suspend(Handle handle) const noexcept
				{
					return handle.promise().continuation ? handle.promise().continuation : std::noop_coroutine();
				}
				void await_resume() const noexcept {}
			};
			Final final_suspend() const noexcept { return {}; }
			template<class U> void return_value(U&& value) { result.emplace(std::forward<U>(value)); }
			void unhandled_exception() noexcept { error = std::current_exception(); }
		};
		InteractionTask() = default;
		explicit InteractionTask(Handle handle) noexcept : handle_(handle) {}
		InteractionTask(InteractionTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
		InteractionTask& operator=(InteractionTask&& other) noexcept
		{
			if (this != &other) { Reset(); handle_ = std::exchange(other.handle_, {}); }
			return *this;
		}
		InteractionTask(const InteractionTask&) = delete;
		~InteractionTask() { Reset(); }
		void Reset() noexcept { if (handle_) { handle_.destroy(); handle_ = {}; } }
		void Start() { if (handle_ && !handle_.done()) handle_.resume(); }
		bool Done() const noexcept { return !handle_ || handle_.done(); }
		void Rethrow() const { if (handle_ && handle_.promise().error) std::rethrow_exception(handle_.promise().error); }
		struct Awaiter
		{
			Handle handle;
			bool await_ready() const noexcept { return !handle || handle.done(); }
			std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) const noexcept
			{
				handle.promise().continuation = parent;
				return handle;
			}
			T await_resume()
			{
				if (handle.promise().error) std::rethrow_exception(handle.promise().error);
				return std::move(*handle.promise().result);
			}
		};
		Awaiter operator co_await() const noexcept { return { handle_ }; }
	private:
		Handle handle_{};
	};

	// 单一有界 FIFO：只合并相邻且属于同一接触的 Move，绝不跨越 Down/Up。
	template<class Event, std::size_t Capacity = 512> class InteractionMailbox
	{
	public:
		template<class CanCoalesce> bool Push(Event event, CanCoalesce canCoalesce)
		{return Push(std::move(event),std::move(canCoalesce),[](Event&){return true;});}
		template<class CanCoalesce,class Admit> bool Push(Event event, CanCoalesce canCoalesce,Admit admit)
		{
			std::lock_guard lock(mutex_);
			if(overflowed_)return false;
			const bool coalesce=!queue_.empty()&&canCoalesce(queue_.back(),event);
			if(!coalesce&&queue_.size()>=Capacity){queue_.clear();overflowed_=true;++revision_;wake_.notify_one();return false;}
			// 仅固定的接收元数据在锁内发布；业务处理仍全部由 worker 执行。
			if(!admit(event))return false;
			if(coalesce)queue_.back()=std::move(event);else queue_.push_back(std::move(event));
			++revision_;
			wake_.notify_one();
			return true;
		}
		bool Pop(Event& event)
		{
			std::lock_guard lock(mutex_);
			if (queue_.empty()) return false;
			event = std::move(queue_.front()); queue_.pop_front(); return true;
		}
		bool ConsumeOverflow() noexcept
		{
			std::lock_guard lock(mutex_);
			return std::exchange(overflowed_, false);
		}
		bool OverflowPending() const noexcept {std::lock_guard lock(mutex_);return overflowed_;}
		void CompleteOverflow() noexcept {std::lock_guard lock(mutex_);overflowed_=false;++revision_;wake_.notify_one();}
		template<class Predicate> std::size_t RemoveIf(Predicate predicate)
		{
			std::lock_guard lock(mutex_);
			std::size_t count = 0;
			for (auto it = queue_.begin(); it != queue_.end();)
				if (predicate(*it)) { it = queue_.erase(it); ++count; } else ++it;
			return count;
		}
		void Wake() noexcept { std::lock_guard lock(mutex_); ++revision_; wake_.notify_one(); }
		std::uint64_t Revision() const noexcept { std::lock_guard lock(mutex_); return revision_; }
		void WaitUntil(std::chrono::steady_clock::time_point deadline, std::uint64_t observed)
		{
			std::unique_lock lock(mutex_);
			wake_.wait_until(lock, deadline, [&] { return revision_ != observed || !queue_.empty() || overflowed_; });
		}
	private:
		mutable std::mutex mutex_;
		std::condition_variable wake_;
		std::deque<Event> queue_;
		std::uint64_t revision_ = 0;
		bool overflowed_ = false;
	};

	enum class ButtonPointerEvent { Move, Down, Up, Leave, Cancel };
	struct ButtonPointerResult
	{
		std::uint64_t previousHover = 0, hover = 0;
		std::uint64_t previousPressed = 0, pressed = 0, activate = 0;
	};
	class ButtonPointerState
	{
	public:
		ButtonPointerResult Update(ButtonPointerEvent event, std::uint64_t hit,
			std::optional<POINT> screenPoint = std::nullopt) noexcept
		{
			ButtonPointerResult result{ hover_, hover_, pressed_, pressed_, 0 };
			switch (event)
			{
			case ButtonPointerEvent::Down:
				suppressHoverUntilPointerMove_ = false;
				pressed_ = hit; hover_ = 0; break;
			case ButtonPointerEvent::Move:
				if (suppressHoverUntilPointerMove_)
				{
					if (!screenPoint || (screenPoint->x == hoverSuppressionScreenPoint_.x
						&& screenPoint->y == hoverSuppressionScreenPoint_.y))
					{
						hover_ = 0;
						break;
					}
					suppressHoverUntilPointerMove_ = false;
				}
				if (pressed_ && hit != pressed_) pressed_ = 0;
				hover_ = pressed_ ? 0 : hit; break;
			case ButtonPointerEvent::Up:
				if (pressed_ && hit == pressed_) result.activate = pressed_;
				pressed_ = 0; hover_ = 0;
				suppressHoverUntilPointerMove_ = screenPoint.has_value();
				if (screenPoint) hoverSuppressionScreenPoint_ = *screenPoint;
				break;
			case ButtonPointerEvent::Leave: case ButtonPointerEvent::Cancel:
				suppressHoverUntilPointerMove_ = false;
				pressed_ = 0; hover_ = 0; break;
			}
			result.hover = hover_; result.pressed = pressed_; return result;
		}
		void Reset() noexcept { hover_ = pressed_ = 0; suppressHoverUntilPointerMove_ = false; }
	private:
		std::uint64_t hover_ = 0, pressed_ = 0;
		POINT hoverSuppressionScreenPoint_{};
		bool suppressHoverUntilPointerMove_ = false;
	};

	struct SurfaceInputEvent
	{
		HWND window = nullptr;
		UINT message = 0;
		WPARAM wParam = 0;
		LPARAM lParam = 0;
		ULONG_PTR extraInfo = 0;
		DWORD messageTime = 0;
		ULONGLONG tick = 0;
		POINT screenPoint{};
		POINT logicalPoint{};
		std::uint64_t generation = 0;
		std::uint64_t presentedRevision = 0;
		std::uint64_t semanticRevision = 0;
		std::uint64_t captureTicket = 0;
		std::uint64_t hitButtonId = 0;
		std::uint32_t contactId = 0;
		bool touch = false;
		bool captureOwned = false;
		bool nativeCaptureReleased = false;
		bool logicalPointValid = false;
		bool backgroundHit = false;
	};
	using SurfaceInputHandler = void(*)(const SurfaceInputEvent&, void*);
	using SurfaceInputTick = std::chrono::steady_clock::time_point(*)(std::chrono::steady_clock::time_point, void*);
	using SurfaceInputCancel = void(*)(const SurfaceInputEvent&, void*);
	bool RegisterInteractionSurface(HWND, std::uint64_t generation, SurfaceInputHandler,
		SurfaceInputTick, SurfaceInputCancel, void*, bool nativeWindow = true);
	void UnregisterInteractionSurface(HWND);
	[[nodiscard]] bool DrainInteractionSurfaceCallbacks() noexcept;
	bool SubmitSurfaceInput(const SurfaceInputEvent&);
	void WakeInteractionWorker() noexcept;
	[[nodiscard]] bool QueueInteractionComponentAction(std::function<void()> action);
	class ScopedInteractionWorker final
	{
	public:
		~ScopedInteractionWorker();
		ScopedInteractionWorker(const ScopedInteractionWorker&) = delete;
		ScopedInteractionWorker& operator=(const ScopedInteractionWorker&) = delete;
	private:
		struct State;
		ScopedInteractionWorker();
		std::unique_ptr<State> state_;
		friend std::unique_ptr<ScopedInteractionWorker> CreateOffscreenInteractionWorker();
	};
	// 无 Window Service/Draw3 Host 时才能取得唯一输入 owner；先注销/排空目标，再销毁 scope。
	[[nodiscard]] std::unique_ptr<ScopedInteractionWorker> CreateOffscreenInteractionWorker();
	int RunSharedInteractionWorkerTests() noexcept;
}
