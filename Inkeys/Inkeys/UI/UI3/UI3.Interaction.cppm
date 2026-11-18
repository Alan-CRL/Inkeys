module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

export module Inkeys.UI.UI3.Interaction;

export namespace Inkeys::UI::UI3
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

	enum class GestureActivation { OnRelease, OnPress };
	enum class GestureButton { Primary, Secondary };
	enum class GestureMovePolicy { CancelHold, RestartHold };
	enum class GestureDistance { Axes, Euclidean };
	enum class GestureDoubleClick { Disabled, Interval, Native };
	struct GestureConfig
	{
		GestureActivation activation = GestureActivation::OnRelease;
		GestureActivation secondaryActivation = GestureActivation::OnPress;
		GestureDoubleClick doubleClick = GestureDoubleClick::Disabled;
		std::chrono::milliseconds doubleClickInterval{500}; // 同目标成功主键点击的 Down 间隔，含等于边界。
		bool holdEnabled = false;
		std::chrono::milliseconds holdDelay{200}, holdDuration{800}; // 均非负；时长从延时结束开始计。
		double moveThreshold = 5.0; // 调用方已乘坐标缩放；恰好等于阈值不算移动。
		GestureMovePolicy movePolicy = GestureMovePolicy::CancelHold;
		GestureDistance distance = GestureDistance::Euclidean;
		bool cancelOnHitExit = true; // 捕获取色可跨出色盘；普通按钮离开原命中即作废。
		bool consumeClickOnHoldStart = true;
		bool advanceHoldOnRelease = true; // 图形 Up 补到期提交；picker Up 沿用最后一次采样。
	};
	struct GesturePointerInput
	{
		using Time = std::chrono::steady_clock::time_point;
		ButtonPointerEvent kind = ButtonPointerEvent::Move;
		Time time{}; // 只消费调用方单调时间；本单元不读取时钟。
		double x = 0, y = 0;
		std::uint64_t hit = 0;
		GestureButton button = GestureButton::Primary;
		bool nativeDoubleClick = false; // 原生第二次 Down，Native 模式绑定最后成功目标，忽略动画后的 hit。
	};
	// Click：有效主键激活；SecondaryClick：次键按配置的边沿激活；DoubleClick：替代第二次 Click。
	// LongPressStarted/Completed：now >= 稳定点时间+延时 / 再加完成时长，各发一次。
	// Cancelled：Leave/Cancel、普通按钮移出命中、超移动阈值取消或重置长按；重置不结束接触。
	enum class GestureKind { Click, SecondaryClick, DoubleClick, LongPressStarted, LongPressCompleted, Cancelled };
	struct GestureEvent
	{
		GestureKind kind = GestureKind::Cancelled;
		std::uint64_t target = 0;
		GestureButton button = GestureButton::Primary;
		GesturePointerInput::Time time{};
	};
	struct GestureUpdate
	{
		// 一次跨过两个长按期限再抬起，最多产生开始、完成、点击；替换接触还可先取消。
		GestureEvent events[4]{};
		std::size_t count = 0;
		std::optional<GesturePointerInput::Time> nextWake;
	};
	class PointerGestureRecognizer
	{
	public:
		using Time = GesturePointerInput::Time;
		explicit PointerGestureRecognizer(GestureConfig config = {}) noexcept : config_(config) {}
		GestureUpdate Update(const GesturePointerInput& input) noexcept;
		GestureUpdate Advance(Time now) noexcept;
		std::optional<Time> NextWake() const noexcept;
		void Reset() noexcept;
	private:
		void Emit(GestureUpdate& result, GestureKind kind, Time time) const noexcept;
		void AdvanceHold(GestureUpdate& result, Time now) noexcept;
		void Activate(GestureUpdate& result, Time now) noexcept;
		void EndContact() noexcept;
		void CancelContact() noexcept;
		GestureConfig config_;
		std::uint64_t target_ = 0, lastClicked_ = 0;
		GestureButton button_ = GestureButton::Primary;
		Time downTime_{}, stableTime_{}, lastClickDown_{};
		double stableX_ = 0, stableY_ = 0;
		bool contact_ = false, clickAllowed_ = false, holdActive_ = false;
		bool holdStarted_ = false, holdCompleted_ = false, secondClick_ = false, nativeContinuation_ = false;
	};
}
