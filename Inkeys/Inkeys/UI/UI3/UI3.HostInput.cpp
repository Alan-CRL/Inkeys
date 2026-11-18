module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../../Log/Log.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

module Inkeys.UI.UI3;
import :HostInput;
import Inkeys.UI.UI3.Interaction;

namespace Inkeys::UI::UI3
{
namespace
{
	using InteractionClock = std::chrono::steady_clock;
	// 宿主登记的诊断出口；未登记时拒绝路径不输出诊断。函数指针按原子读取，任何线程可用。
	std::atomic<SurfaceInputDiagnosticSink> surfaceInputDiagnosticSink{ nullptr };
	std::atomic<SurfaceInputDiagnosticEnabled> surfaceInputDiagnosticEnabled{ nullptr };
	template<class... Args>
	void SurfaceInputDiagnostic(HWND window, const char* stage, const char* reason, UINT message,
		const char* format, Args... args) noexcept
	{
		const auto sink = surfaceInputDiagnosticSink.load(std::memory_order_acquire);
		if (!sink) return;
		// 诊断关闭时不格式化：与迁移前「先查开关、再格式化」的次序一致。
		const auto enabled = surfaceInputDiagnosticEnabled.load(std::memory_order_acquire);
		if (enabled && !enabled()) return;
		// 只在拒绝路径上执行；先格式化成键值文本，再交给宿主的诊断通道。
		char detail[192];
		std::snprintf(detail, sizeof(detail), format, args...);
		sink(window, stage, reason, message, detail);
	}
	// 邮箱信封：表面事件按值携带；主内容消息只携带宿主交来的不透明载荷，两类共用同一 FIFO。
	struct SharedInputEnvelope
	{
		SurfaceInputEvent surface{};
		std::shared_ptr<void> main;
		bool forMain = false;
	};
	struct SurfaceInputRegistration
	{
		HWND window = nullptr;
		std::uint64_t contentInstance = 0;
		std::uint64_t generation = 0;
		SurfaceInputHandler handler = nullptr;
		SurfaceInputTick tick = nullptr;
		SurfaceInputCancel cancel = nullptr;
		void* context = nullptr;
		bool nativeWindow = true;
		std::atomic_bool active = true;
		SurfaceInputEvent acceptedNative;
		std::mutex callbackMutex;
		std::condition_variable callbackDrained;
		unsigned inFlight = 0;
		bool cancellationDelivered = false;
	};
	InteractionMailbox<SharedInputEnvelope> sharedInputMailbox;
	std::mutex interactionRegistrationMutex;
	std::vector<std::shared_ptr<SurfaceInputRegistration>> interactionRegistrations;
	std::atomic_bool interactionWorkerRunning = false;
	std::atomic_bool offscreenInteractionWorker = false;
	std::atomic<DWORD> interactionWorkerThreadId = 0;
	// 主内容处理器只是一组函数指针；泵与离屏 worker 各自在锁内取值快照后使用。
	std::mutex mainContentHandlerMutex;
	MainContentHandler mainContentHandler;
	bool mainContentHandlerRegistered = false;
	MainContentHandler SnapshotMainContentHandler()
	{
		std::lock_guard lock(mainContentHandlerMutex);
		return mainContentHandler;
	}
	// 注册键：HWND + 内容实例。
	bool SameSurfaceKey(const SurfaceInputRegistration& registration, HWND window,
		std::uint64_t contentInstance) noexcept
	{
		return registration.window == window && registration.contentInstance == contentInstance;
	}
	std::vector<std::shared_ptr<SurfaceInputRegistration>> SnapshotInteractionRegistrations()
	{
		std::lock_guard lock(interactionRegistrationMutex);
		return interactionRegistrations;
	}
	template<class Callback> bool WithSurfaceInputCallback(
		const std::shared_ptr<SurfaceInputRegistration>& registration,
		bool requireActive, bool cancellation, Callback callback)
	{
		{
			std::lock_guard lock(registration->callbackMutex);
			if (requireActive && !registration->active.load(std::memory_order_acquire)) return false;
			if (cancellation && !registration->active.load(std::memory_order_acquire)
				&& registration->cancellationDelivered) return false;
			++registration->inFlight;
		}
		try { callback(); }
		// 输入 owner 线程上只用非阻塞日志。
		catch (...) { (void)Inkeys::Log::TryWrite(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3, std::nullopt, "分页输入回调失败，保留取消屏障"); }
		{
			std::lock_guard lock(registration->callbackMutex);
			--registration->inFlight;
			if (cancellation && !registration->active.load(std::memory_order_acquire))
				registration->cancellationDelivered = true;
			registration->callbackDrained.notify_all();
		}
		return true;
	}
	SurfaceInputEvent AcceptedSurfaceCancellation(const std::shared_ptr<SurfaceInputRegistration>& registration)
	{
		std::lock_guard lock(registration->callbackMutex);
		auto result=registration->acceptedNative;result.window=registration->window;
		result.contentInstance=registration->contentInstance;
		result.message=WM_CANCELMODE;result.generation=registration->generation;
		return result;
	}

	void CancelRegisteredInteractionSurfaces()
	{
		for (const auto& registration : SnapshotInteractionRegistrations())
			(void)WithSurfaceInputCallback(registration, false, true, [&]
			{
				const auto cancellation=AcceptedSurfaceCancellation(registration);
				if (registration->cancel) registration->cancel(cancellation, registration->context);
			});
	}
	void DispatchSurfaceInteraction(const SurfaceInputEvent& event)
	{
		for (const auto& registration : SnapshotInteractionRegistrations())
		{
			if (!SameSurfaceKey(*registration, event.window, event.contentInstance)) continue;
			if (event.generation != 0 && event.generation != registration->generation) { SurfaceInputDiagnostic(event.window,"dispatch_reject","registration_generation_changed",event.message,"event_generation=%llu registered_generation=%llu ticket=%llu",static_cast<unsigned long long>(event.generation),static_cast<unsigned long long>(registration->generation),static_cast<unsigned long long>(event.captureTicket)); continue; }
			const bool cancellation = event.message == WM_CANCELMODE || event.message == WM_CAPTURECHANGED
				|| event.message == WM_DESTROY;
			if (WithSurfaceInputCallback(registration, true, cancellation, [&]
			{
				if (cancellation && !event.nativeCaptureReleased)
				{
					if (registration->cancel) registration->cancel(event, registration->context);
				}
				else if (registration->handler) registration->handler(event, registration->context);
			})) break;
		}
	}
	InteractionClock::time_point TickSurfaceInteractions(InteractionClock::time_point now)
	{
		auto deadline = InteractionClock::time_point::max();
		for (const auto& registration : SnapshotInteractionRegistrations())
		{
			if (!registration->active.load(std::memory_order_acquire))
			{
				(void)WithSurfaceInputCallback(registration, false, true, [&]
				{
					const auto cancellation=AcceptedSurfaceCancellation(registration);
					if (registration->cancel) registration->cancel(cancellation, registration->context);
				});
				continue;
			}
			(void)WithSurfaceInputCallback(registration, true, false, [&]
			{
				if (registration->tick) deadline = (std::min)(deadline, registration->tick(now, registration->context));
			});
		}
		{
			std::lock_guard lock(interactionRegistrationMutex);
			std::erase_if(interactionRegistrations, [](const auto& registration)
			{
				std::lock_guard callbackLock(registration->callbackMutex);
				return !registration->active.load(std::memory_order_acquire)
					&& registration->cancellationDelivered && registration->inFlight == 0;
			});
		}
		return deadline;
	}
	// 泵：main 为主内容处理器的取值快照（可为空），context 为宿主的续接上下文。
	InteractionClock::time_point PumpSharedInteraction(const MainContentHandler& main, void* context,
		InteractionClock::time_point now)
	{
		if (sharedInputMailbox.OverflowPending())
		{
			struct Complete {~Complete(){sharedInputMailbox.CompleteOverflow();}} complete;
			(void)Inkeys::Log::TryWrite(Inkeys::Log::Level::Warn, Inkeys::Log::Module::UI3, std::nullopt,
				"输入邮箱溢出：已清空在途输入并建立取消屏障，取消全部已准入的捕获");
			// 丢掉整个在途接触并显式取消；宁可取消点击，也不能丢 Up 后留下捕获。
			if (main.overflow) main.overflow(context);
			CancelRegisteredInteractionSurfaces();
		}
		SharedInputEnvelope event;
		for (unsigned budget = 0; budget != 32 && sharedInputMailbox.Pop(event); ++budget)
		{
			if (!event.forMain) DispatchSurfaceInteraction(event.surface);
			else if (main.dispatch) main.dispatch(std::move(event.main), context);
			if (main.resume) (void)main.resume(context, now);
			// 主手势取消后先重建续接，再消费随后接触；不能把新 Down/Up 丢在旧协程后。
			if (main.cancelled && main.cancelled(context)) break;
		}
		// 时间事件也通过同一 owner，长按重复不再依赖绘制帧率。
		auto deadline = TickSurfaceInteractions(now);
		if (main.tick) deadline = (std::min)(deadline, main.tick(now));
		if (main.resume)
			for (unsigned budget = 0; budget != 32 && main.resume(context, now); ++budget) {}
		return main.deadline ? (std::min)(deadline, main.deadline(context)) : deadline;
	}
}
	bool RegisterInteractionSurface(HWND window, std::uint64_t generation, SurfaceInputHandler handler,
		SurfaceInputTick tick, SurfaceInputCancel cancel, void* context, bool nativeWindow,
		std::uint64_t contentInstance)
	{
		if (!window || !handler) return false;
		auto registration = std::make_shared<SurfaceInputRegistration>();
		registration->window = window; registration->generation = generation;
		registration->contentInstance = contentInstance;
		registration->handler = handler; registration->tick = tick;
		registration->cancel = cancel; registration->context = context; registration->nativeWindow = nativeWindow;
		{
			std::lock_guard lock(interactionRegistrationMutex);
			for (const auto& previous : interactionRegistrations)
				if (SameSurfaceKey(*previous, window, contentInstance))
				{
					std::lock_guard callbackLock(previous->callbackMutex);
					previous->active.store(false, std::memory_order_release);
				}
			interactionRegistrations.push_back(std::move(registration));
		}
		sharedInputMailbox.Wake();
		// 调用线程可能是窗口过程或输入 owner，只用非阻塞日志。
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
			"输入表面注册 hwnd=0x{:X} generation={} instance={} native={}",
			reinterpret_cast<std::uintptr_t>(window), generation, contentInstance, nativeWindow);
		return true;
	}
	void UnregisterInteractionSurface(HWND window, std::uint64_t contentInstance)
	{
		// HWND owner 不等待 worker，避免与 worker 的窗口效果请求形成互锁。
		bool retired = false, nativeWindow = false;
		std::uint64_t generation = 0;
		{
			std::lock_guard lock(interactionRegistrationMutex);
			for (const auto& registration : interactionRegistrations)
				if (SameSurfaceKey(*registration, window, contentInstance))
				{
					std::lock_guard callbackLock(registration->callbackMutex);
					if (registration->active.exchange(false, std::memory_order_acq_rel))
					{
						retired = true; generation = registration->generation;
						nativeWindow = registration->nativeWindow;
					}
				}
		}
		sharedInputMailbox.RemoveIf([&](const auto& event)
		{
			return !event.forMain && event.surface.window == window
				&& event.surface.contentInstance == contentInstance;
		});
		sharedInputMailbox.Wake();
		// 重复注销不改变状态，只在确实退休了活动注册时记录。
		if (retired)
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
				"输入表面注销 hwnd=0x{:X} generation={} instance={} native={}",
				reinterpret_cast<std::uintptr_t>(window), generation, contentInstance, nativeWindow);
	}
	bool DrainInteractionSurfaceCallbacks() noexcept
	{
		const auto registrations = SnapshotInteractionRegistrations();
		if (std::none_of(registrations.begin(), registrations.end(), [](const auto& registration)
			{ return !registration->active.load(std::memory_order_acquire); })) return true;
		if (interactionWorkerThreadId.load(std::memory_order_acquire) == GetCurrentThreadId()) return false;
		for (const auto& registration : registrations)
			if (!registration->active.load(std::memory_order_acquire)
				&& registration->nativeWindow
				&& GetWindowThreadProcessId(registration->window, nullptr) == GetCurrentThreadId()) return false;
		sharedInputMailbox.Wake();
		for (const auto& registration : registrations)
		{
			std::unique_lock lock(registration->callbackMutex);
			if (registration->active.load(std::memory_order_acquire)) continue;
			if (!interactionWorkerRunning.load(std::memory_order_acquire))
			{
				registration->callbackDrained.wait(lock, [&] { return registration->inFlight == 0; });
				registration->cancellationDelivered = true;
			}
			else registration->callbackDrained.wait(lock, [&]
			{
				return registration->inFlight == 0 && (registration->cancellationDelivered
					|| !interactionWorkerRunning.load(std::memory_order_acquire));
			});
		}
		return true;
	}

	bool QueueInteractionComponentAction(std::function<void()> action)
	{
		if (offscreenInteractionWorker.load(std::memory_order_acquire) || !action) return false;
		// 业务队列留在宿主：这里只转交，不在输入 owner 上执行。
		const auto main = SnapshotMainContentHandler();
		return main.queueComponentAction && main.queueComponentAction(std::move(action));
	}

	bool SubmitSurfaceInput(const SurfaceInputEvent& input)
	{
		std::shared_ptr<SurfaceInputRegistration> registration;
		{
			std::lock_guard lock(interactionRegistrationMutex);
			for(const auto& candidate:interactionRegistrations)if(SameSurfaceKey(*candidate,input.window,input.contentInstance)
				&&candidate->active.load(std::memory_order_acquire)&&(input.generation==0||input.generation==candidate->generation)){
				registration=candidate;break;
			}
		}
		if(!registration) { SurfaceInputDiagnostic(input.window,"queue_reject","no_active_registration_or_generation",input.message,"generation=%llu semantic=%llu",static_cast<unsigned long long>(input.generation),static_cast<unsigned long long>(input.semanticRevision)); return false; }
		SharedInputEnvelope event; event.surface = input;
		return sharedInputMailbox.Push(std::move(event), [](const auto& previous, const auto& next)
		{
			return !previous.forMain && !next.forMain && previous.surface.window == next.surface.window
				&& previous.surface.contentInstance == next.surface.contentInstance
				&& previous.surface.message == WM_MOUSEMOVE && next.surface.message == WM_MOUSEMOVE
				&& !previous.surface.preserveMoveSamples && !next.surface.preserveMoveSamples
				&& previous.surface.generation == next.surface.generation
				&& previous.surface.presentedRevision == next.surface.presentedRevision
				&& previous.surface.semanticRevision == next.surface.semanticRevision
				&& previous.surface.captureTicket == next.surface.captureTicket
				&& previous.surface.contactId == next.surface.contactId && previous.surface.touch == next.surface.touch
				&& previous.surface.contactSource == next.surface.contactSource && previous.surface.pen == next.surface.pen
				&& previous.surface.wParam == next.surface.wParam;
		},[registration](auto& admitted){
			std::lock_guard lock(registration->callbackMutex);
			if(!registration->active.load(std::memory_order_acquire))return false;
			auto& input=admitted.surface;
			const bool cancel=input.message==WM_CANCELMODE||input.message==WM_CAPTURECHANGED||input.message==WM_DESTROY;
			if(cancel&&!input.nativeCaptureReleased&&!input.captureTicket&&!input.touchRegistration){
				input.captureTicket=registration->acceptedNative.captureTicket;
				input.touchRegistration=registration->acceptedNative.touchRegistration;
				input.touchRouteGeneration=registration->acceptedNative.touchRouteGeneration;
			}else if(input.message==WM_LBUTTONDOWN)registration->acceptedNative=input;
			return true;
		});
	}
	void WakeInteractionWorker() noexcept { sharedInputMailbox.Wake(); }
	struct ScopedInteractionWorker::State
	{
		std::jthread thread;
		std::mutex readyMutex;
		std::condition_variable ready;
		bool started = false;
	};
	ScopedInteractionWorker::ScopedInteractionWorker() = default;
	ScopedInteractionWorker::~ScopedInteractionWorker()
	{
		if (!state_) return;
		state_->thread.request_stop(); sharedInputMailbox.Wake();
		if (state_->thread.joinable()) state_->thread.join();
	}
	std::unique_ptr<ScopedInteractionWorker> CreateOffscreenInteractionWorker()
	{
		// 宿主策略（生产窗口服务/画布在运行、进程正在退出）经主内容处理器询问，UI3 不直接认识它们。
		const MainContentHandler main = SnapshotMainContentHandler();
		if (main.offscreenWorkerAllowed && !main.offscreenWorkerAllowed()) return {};
		bool expected = false;
		if (!interactionWorkerRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return {};
		try
		{
			if (!SnapshotInteractionRegistrations().empty())
			{ interactionWorkerRunning.store(false, std::memory_order_release); return {}; }
			auto scope = std::unique_ptr<ScopedInteractionWorker>(new ScopedInteractionWorker);
			scope->state_ = std::make_unique<ScopedInteractionWorker::State>();
			auto* state = scope->state_.get();
			if (main.resetPending) main.resetPending();
			sharedInputMailbox.RemoveIf([](const auto&) { return true; });
			(void)sharedInputMailbox.ConsumeOverflow();
			offscreenInteractionWorker.store(true, std::memory_order_release);
			state->thread = std::jthread([state, main](std::stop_token stop)
			{
				// 主内容的续接上下文属于本线程，由宿主建立与销毁；建立失败时本 worker 只服务表面。
				void* context = nullptr;
				try { if (main.beginWorker) context = main.beginWorker(); } catch (...) {}
				const MainContentHandler pumpMain = context ? main : MainContentHandler{};
				interactionWorkerThreadId.store(GetCurrentThreadId(), std::memory_order_release);
				{
					std::lock_guard lock(state->readyMutex); state->started = true; state->ready.notify_all();
				}
				try
				{
					while (!stop.stop_requested())
					{
						const auto observed = sharedInputMailbox.Revision();
						const auto deadline = PumpSharedInteraction(pumpMain, context, InteractionClock::now());
						if (!stop.stop_requested()) sharedInputMailbox.WaitUntil(deadline, observed);
					}
				}
				catch (...) { (void)Inkeys::Log::TryWrite(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3, std::nullopt, "无窗口输入 owner 退出"); }
				// scope 结束时关闭所有未注销目标，等待中的 drain 得到确定完成通知。
				for (const auto& registration : SnapshotInteractionRegistrations())
				{
					std::lock_guard lock(registration->callbackMutex);
					registration->active.store(false, std::memory_order_release);
				}
				CancelRegisteredInteractionSurfaces();
				(void)TickSurfaceInteractions(InteractionClock::now());
				if (context && main.endWorker) main.endWorker(context);
				interactionWorkerThreadId.store(0, std::memory_order_release);
				offscreenInteractionWorker.store(false, std::memory_order_release);
				interactionWorkerRunning.store(false, std::memory_order_release);
			});
			std::unique_lock readyLock(state->readyMutex);
			state->ready.wait(readyLock, [&] { return state->started; });
			return scope;
		}
		catch (...)
		{
			offscreenInteractionWorker.store(false, std::memory_order_release);
			interactionWorkerRunning.store(false, std::memory_order_release);
			return {};
		}
	}

	bool RegisterMainContentHandler(const MainContentHandler& handler)
	{
		if (!handler.dispatch || !handler.resume || !handler.cancelled || !handler.overflow
			|| !handler.tick || !handler.deadline) return false;
		{
			std::lock_guard lock(mainContentHandlerMutex);
			if (mainContentHandlerRegistered) return false;
			mainContentHandler = handler; mainContentHandlerRegistered = true;
		}
		// 生产路径在输入 owner 线程登记，只用非阻塞日志。
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
			"主内容输入处理器注册 offscreen_context={} component_actions={}",
			handler.beginWorker != nullptr && handler.endWorker != nullptr, handler.queueComponentAction != nullptr);
		return true;
	}
	void UnregisterMainContentHandler() noexcept
	{
		{
			std::lock_guard lock(mainContentHandlerMutex);
			if (!mainContentHandlerRegistered) return;
			mainContentHandler = {}; mainContentHandlerRegistered = false;
		}
		(void)Inkeys::Log::TryWrite(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3, std::nullopt,
			"主内容输入处理器注销");
	}
	bool SubmitMainContentInput(std::shared_ptr<void> payload, MainContentCoalesce canCoalesce,
		MainContentAdmit admit)
	{
		if (!payload) return false;
		SharedInputEnvelope event; event.forMain = true; event.main = std::move(payload);
		// 合并与准入仍在邮箱锁内判定；主内容消息只与相邻的主内容消息合并，绝不跨越表面事件。
		return sharedInputMailbox.Push(std::move(event),
			[canCoalesce](const SharedInputEnvelope& previous, const SharedInputEnvelope& next)
			{
				return previous.forMain && next.forMain && canCoalesce
					&& canCoalesce(previous.main.get(), next.main.get());
			},
			[admit](SharedInputEnvelope& admitted) { return !admit || admit(admitted.main.get()); });
	}

	bool BeginInteractionWorker() noexcept
	{
		bool expected = false;
		if (!interactionWorkerRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
		interactionWorkerThreadId.store(GetCurrentThreadId(), std::memory_order_release);
		return true;
	}
	void EndInteractionWorker() noexcept
	{
		interactionWorkerRunning.store(false, std::memory_order_release);
		// owner 已停：等待取消回执的 drain 改按「无 worker」条件醒来。
		for (const auto& registration : SnapshotInteractionRegistrations())
		{
			std::lock_guard lock(registration->callbackMutex);
			registration->callbackDrained.notify_all();
		}
		interactionWorkerThreadId.store(0, std::memory_order_release);
	}
	bool InteractionWorkerRunning() noexcept { return interactionWorkerRunning.load(std::memory_order_acquire); }
	bool OffscreenInteractionWorkerActive() noexcept { return offscreenInteractionWorker.load(std::memory_order_acquire); }
	std::uint64_t InteractionInputRevision() noexcept { return sharedInputMailbox.Revision(); }
	void WaitInteractionInput(std::chrono::steady_clock::time_point deadline, std::uint64_t observed)
	{
		sharedInputMailbox.WaitUntil(deadline, observed);
	}
	std::chrono::steady_clock::time_point PumpInteractionWorker(void* mainContext,
		std::chrono::steady_clock::time_point now)
	{
		return PumpSharedInteraction(mainContext ? SnapshotMainContentHandler() : MainContentHandler{},
			mainContext, now);
	}
	void CancelInteractionSurfaces() { CancelRegisteredInteractionSurfaces(); }

	bool InteractionInputOverflowPending() noexcept { return sharedInputMailbox.OverflowPending(); }
	std::size_t InteractionSurfaceRegistrationCount()
	{
		std::lock_guard lock(interactionRegistrationMutex);
		return interactionRegistrations.size();
	}
	void ResetInteractionInputForTesting()
	{
		{
			std::lock_guard lock(interactionRegistrationMutex);
			interactionRegistrations.clear();
		}
		sharedInputMailbox.RemoveIf([](const auto&) { return true; });
	}

	void SetSurfaceInputDiagnosticSink(SurfaceInputDiagnosticSink sink,
		SurfaceInputDiagnosticEnabled enabled) noexcept
	{
		// 先放开关再放出口：读方先取出口，看到出口时开关已就位。
		surfaceInputDiagnosticEnabled.store(enabled, std::memory_order_release);
		surfaceInputDiagnosticSink.store(sink, std::memory_order_release);
	}
}
