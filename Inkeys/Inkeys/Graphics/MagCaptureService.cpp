#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MagCaptureService.h"
#include "MagnificationRuntime.h"
#include "Surface.hpp"
#include "../Drawing/Draw3/Draw3.DiagnosticOutput.h"
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>
#include <vector>

import Inkeys.Display;

namespace Inkeys::Graphics
{
	namespace Diagnostic = Drawing::Draw3::DiagnosticOutput;

	const char* MagCaptureReasonText(MagCaptureReason reason) noexcept
	{
		switch (reason)
		{
		case MagCaptureReason::None: return "none";
		case MagCaptureReason::Stopped: return "stopped";
		case MagCaptureReason::InvalidOptions: return "invalid-options";
		case MagCaptureReason::InvalidGeometry: return "invalid-geometry";
		case MagCaptureReason::Unsupported: return "unsupported-system-or-wow64";
		case MagCaptureReason::RuntimeUnavailable: return "runtime-unavailable";
		case MagCaptureReason::ThreadFailure: return "thread-failure";
		case MagCaptureReason::ComInitialization: return "com-initialization";
		case MagCaptureReason::WindowClass: return "window-class";
		case MagCaptureReason::HostWindow: return "host-window";
		case MagCaptureReason::ChildWindow: return "child-window";
		case MagCaptureReason::Transform: return "transform";
		case MagCaptureReason::Filter: return "filter";
		case MagCaptureReason::Source: return "source";
		case MagCaptureReason::Redraw: return "redraw";
		case MagCaptureReason::PrintWindow: return "print-window";
		case MagCaptureReason::InvalidBuffer: return "invalid-buffer";
		case MagCaptureReason::NearBlack: return "near-black";
		case MagCaptureReason::LowInformation: return "low-information";
		case MagCaptureReason::StartupTimeout: return "startup-timeout";
		case MagCaptureReason::ResultTimeout: return "result-timeout";
		case MagCaptureReason::CallbackFailure: return "callback-failure";
		case MagCaptureReason::CleanupFailure: return "cleanup-failure";
		}
		return "unknown";
	}

	MagFrameQuality AnalyzeMagFrame(std::span<const std::uint8_t> bgra,
		int width, int height, std::size_t stride) noexcept
	{
		MagFrameQuality quality;
		if (width <= 0 || height <= 0 ||
			static_cast<std::size_t>(width) > (std::numeric_limits<std::size_t>::max)() / 4 ||
			stride < static_cast<std::size_t>(width) * 4 ||
			static_cast<std::size_t>(height) > (std::numeric_limits<std::size_t>::max)() / stride ||
			bgra.size() < stride * static_cast<std::size_t>(height))
			return quality;

		const int stepX = (std::max)(1, width / 256);
		const int stepY = (std::max)(1, height / 144);
		double sum = 0, squares = 0;
		std::size_t black = 0, edges = 0, pairs = 0;
		for (int y = 0; y < height; y += stepY)
		{
			int previous = -1;
			for (int x = 0; x < width; x += stepX)
			{
				const auto* pixel = bgra.data() + static_cast<std::size_t>(y) * stride +
					static_cast<std::size_t>(x) * 4;
				const int luma = (29 * pixel[0] + 150 * pixel[1] + 77 * pixel[2]) / 256;
				sum += luma;
				squares += static_cast<double>(luma) * luma;
				black += luma <= 5;
				if (previous >= 0)
				{
					++pairs;
					edges += std::abs(luma - previous) >= 8;
				}
				previous = luma;
				++quality.sampledPixels;
			}
		}
		quality.mean = sum / static_cast<double>(quality.sampledPixels);
		quality.variance = (std::max)(0.0,
			squares / static_cast<double>(quality.sampledPixels) - quality.mean * quality.mean);
		quality.nearBlackRatio = static_cast<double>(black) / static_cast<double>(quality.sampledPixels);
		quality.edgeRatio = pairs ? static_cast<double>(edges) / static_cast<double>(pairs) : 0;
		// 暗色但有纹理的页面仍然有效；黑屏必须同时满足亮度、方差和黑像素比例。
		if (quality.mean < 2.0 && quality.variance < 4.0 && quality.nearBlackRatio >= 0.995)
			quality.reason = MagCaptureReason::NearBlack;
		else if (quality.variance < 4.0 || quality.edgeRatio < 0.002)
			quality.reason = MagCaptureReason::LowInformation;
		else
			quality.reason = MagCaptureReason::None;
		return quality;
	}

	bool MagCaptureResultTimedOut(std::uint64_t now, std::uint64_t lastCompletion, bool initializing) noexcept
	{
		return now >= lastCompletion && now - lastCompletion > (initializing ? 5000 : 1000);
	}

	namespace Detail
	{
		struct MagCaptureSharedState
		{
			std::mutex mutex;
			MagCaptureSnapshot snapshot;
			MagCaptureOptions options;
			MagCaptureService::FrameCallback callback;
			std::atomic<bool> publicationOpen{ true };
			std::atomic<std::uint64_t> lastCompletion{ 0 };
			std::atomic<bool> captureArmed{ false };
			std::uint64_t startedTick = 0;
			HANDLE stopEvent = nullptr, doneEvent = nullptr;
			HANDLE ownerThread = nullptr, monitorThread = nullptr;
			~MagCaptureSharedState()
			{
				if (ownerThread) CloseHandle(ownerThread);
				if (monitorThread) CloseHandle(monitorThread);
				if (stopEvent) CloseHandle(stopEvent);
				if (doneEvent) CloseHandle(doneEvent);
			}
		};
	}

	namespace
	{
		using SharedState = Detail::MagCaptureSharedState;
		constexpr wchar_t CaptureClass[] = L"Inkeys.ScrollFollow.MagCaptureHost";

		void Fail(SharedState& state, MagCaptureReason reason, DWORD error) noexcept
		{
			state.publicationOpen.store(false, std::memory_order_release);
			{
				std::lock_guard lock(state.mutex);
				// 首个有意义的失败优先；超时之后迟到的系统结果不能恢复发布。
				if (state.snapshot.state != MagCaptureState::Failed)
				{
					state.snapshot.state = MagCaptureState::Failed;
					state.snapshot.reason = reason;
					state.snapshot.error = error;
				}
			}
			if (state.stopEvent) SetEvent(state.stopEvent);
			Diagnostic::Printf(Diagnostic::Stream::Error,
				"[ScrollFollow][Mag] session=%llu failure=%s error=%lu\n",
				state.options.session, MagCaptureReasonText(reason), error);
		}

		bool Stopping(const SharedState& state) noexcept
		{
			return WaitForSingleObject(state.stopEvent, 0) == WAIT_OBJECT_0;
		}

		LRESULT CALLBACK CaptureWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
			if (message == WM_NCHITTEST) return HTTRANSPARENT;
			if (message == WM_ERASEBKGND) return 1;
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}

		struct WindowFilter
		{
			std::vector<HWND> handles;
			bool failed = false;
		};

		BOOL CALLBACK CollectOwnWindows(HWND hwnd, LPARAM parameter) noexcept
		{
			auto& filter = *reinterpret_cast<WindowFilter*>(parameter);
			DWORD process = 0;
			GetWindowThreadProcessId(hwnd, &process);
			if (process == GetCurrentProcessId())
			{
				try { filter.handles.push_back(hwnd); }
				catch (...) { filter.failed = true; return FALSE; }
			}
			return TRUE;
		}

		void PumpMessages() noexcept
		{
			MSG message{};
			for (int count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count)
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}

		void CaptureLoop(SharedState& state, const MagnificationApi& api,
			HWND child, DibSurface& surface)
		{
			const RECT source = state.snapshot.sourceRect;
			const int width = source.right - source.left, height = source.bottom - source.top;
			LARGE_INTEGER frequency{};
			QueryPerformanceFrequency(&frequency);
			const auto started = GetTickCount64();
			std::uint64_t next = started, lastReport = started;
			const DWORD interval = (std::max)(1ul, 1000ul / state.options.framesPerSecond);
			double totalMilliseconds = 0;
			MagCaptureReason previousReason = MagCaptureReason::Stopped;
			while (!Stopping(state))
			{
				PumpMessages();
				const auto now = GetTickCount64();
				if (now < next)
				{
					MsgWaitForMultipleObjects(1, &state.stopEvent, FALSE,
						static_cast<DWORD>(next - now), QS_ALLINPUT);
					continue;
				}
				MagCaptureFrame frame;
				frame.width = width;
				frame.height = height;
				frame.stride = static_cast<std::size_t>(width) * 4;
				frame.sourceRect = source;
				frame.workArea = state.snapshot.workArea;
				frame.displayGeneration = state.snapshot.displayGeneration;
				frame.session = state.options.session;
				frame.sequence = state.snapshot.sequence + 1;
				frame.qpcFrequency = frequency.QuadPart;
				LARGE_INTEGER begin{}, end{};
				QueryPerformanceCounter(&begin);
				WindowFilter filter;
				SetLastError(ERROR_SUCCESS);
				MagCaptureReason reason = MagCaptureReason::None;
				if (!EnumWindows(&CollectOwnWindows, reinterpret_cast<LPARAM>(&filter)) || filter.failed)
					reason = MagCaptureReason::Filter;
				else if (!api.setFilter(child, MW_FILTERMODE_EXCLUDE,
					static_cast<int>(filter.handles.size()), filter.handles.data()))
					reason = MagCaptureReason::Filter;
				else if (!api.setSource(child, source))
					reason = MagCaptureReason::Source;
				else if (!RedrawWindow(child, nullptr, nullptr,
					RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN))
					reason = MagCaptureReason::Redraw;
				else
				{
					// 清零避免 API 成功但未写入时把上一帧误当作新结果；相同真实静止帧仍有效。
					surface.clear();
					if (!PrintWindow(child, surface.dc(), 0x00000002 /* PW_RENDERFULLCONTENT */))
						reason = MagCaptureReason::PrintWindow;
				}
				frame.error = reason == MagCaptureReason::None ? ERROR_SUCCESS : GetLastError();
				GdiFlush();
				QueryPerformanceCounter(&end);
				frame.sampledQpc = end.QuadPart;
				frame.captureMilliseconds = 1000.0 * static_cast<double>(end.QuadPart - begin.QuadPart) /
					static_cast<double>(frequency.QuadPart);
				const auto pixels = surface.pixels();
				frame.bgra = { reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size_bytes() };
				frame.quality = reason == MagCaptureReason::None
					? AnalyzeMagFrame(frame.bgra, width, height, frame.stride) : MagFrameQuality{};
				if (reason != MagCaptureReason::None) frame.quality.reason = reason;
				const auto completed = GetTickCount64();
				state.lastCompletion.store(completed, std::memory_order_release);
				totalMilliseconds += frame.captureMilliseconds;
				{
					std::lock_guard lock(state.mutex);
					auto& snapshot = state.snapshot;
					snapshot.sequence = frame.sequence;
					snapshot.validFrames += frame.quality.Valid();
					snapshot.invalidFrames += !frame.quality.Valid();
					snapshot.lastQuality = frame.quality;
					snapshot.lastCaptureMilliseconds = frame.captureMilliseconds;
					snapshot.maximumCaptureMilliseconds = (std::max)(
						snapshot.maximumCaptureMilliseconds, frame.captureMilliseconds);
					snapshot.averageCaptureMilliseconds = totalMilliseconds / static_cast<double>(frame.sequence);
					snapshot.actualFramesPerSecond = 1000.0 * static_cast<double>(frame.sequence) /
						static_cast<double>((std::max)(1ull, completed - started));
					snapshot.excludedWindows = filter.handles.size();
				}
				if (state.options.verbose || completed - lastReport >= 1000 ||
					frame.quality.reason != previousReason)
				{
					Diagnostic::Printf(Diagnostic::Stream::Output,
						"[ScrollFollow][Mag] session=%llu seq=%llu size=%dx%d capture_ms=%.3f "
						"valid=%d reason=%s error=%lu mean=%.3f variance=%.3f black=%.6f edge=%.6f excluded=%zu\n",
						frame.session, frame.sequence, width, height, frame.captureMilliseconds,
						frame.quality.Valid(), MagCaptureReasonText(frame.quality.reason), frame.error,
						frame.quality.mean, frame.quality.variance, frame.quality.nearBlackRatio,
						frame.quality.edgeRatio, filter.handles.size());
					lastReport = completed;
					previousReason = frame.quality.reason;
				}
				if (state.publicationOpen.load(std::memory_order_acquire))
				{
					try { state.callback(frame); }
					catch (...) { Fail(state, MagCaptureReason::CallbackFailure, ERROR_UNHANDLED_EXCEPTION); }
				}
				// 慢帧跳过周期，绝不补发积压图像或追赶调用。
				next += interval;
				if (next <= GetTickCount64()) next = GetTickCount64() + interval;
			}
		}

		DWORD WINAPI CaptureOwner(void* parameter) noexcept
		{
			std::unique_ptr<std::shared_ptr<SharedState>> holder(
				static_cast<std::shared_ptr<SharedState>*>(parameter));
			const auto state = std::move(*holder);
			holder.reset();
			HWND host = nullptr, child = nullptr;
			bool registered = false, cleaned = true;
			const HINSTANCE instance = GetModuleHandleW(nullptr);
			MagnificationRuntimeLease runtime;
			DibSurface surface;
			const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			try
			{
				const auto run = [&]()
				{
					if (Stopping(*state)) return;
					// Mag 在当前 HWND owner 上使用 COM；共享 DLL lease 不能替代线程初始化。
					if (FAILED(comResult))
					{
						Fail(*state, MagCaptureReason::ComInitialization, static_cast<DWORD>(comResult));
						return;
					}
					const auto runtimeStarted = GetTickCount64();
					if (!runtime.Acquire())
					{
						Fail(*state, MagCaptureReason::RuntimeUnavailable, GetLastError());
						return;
					}
					Diagnostic::Printf(Diagnostic::Stream::Output,
						"[ScrollFollow][Mag] session=%llu runtime_ready_ms=%llu\n",
						state->options.session, GetTickCount64() - runtimeStarted);
					if (Stopping(*state)) return;
					WNDCLASSEXW windowClass{ sizeof(windowClass) };
					windowClass.hInstance = instance;
					windowClass.lpfnWndProc = &CaptureWndProc;
					windowClass.lpszClassName = CaptureClass;
					registered = RegisterClassExW(&windowClass) != 0;
					if (!registered)
					{
						Fail(*state, MagCaptureReason::WindowClass, GetLastError());
						return;
					}
					const auto source = state->snapshot.sourceRect;
					const int width = source.right - source.left, height = source.bottom - source.top;
					host = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
						WS_EX_TOOLWINDOW, CaptureClass, L"Inkeys Mag Capture", WS_POPUP,
						source.left, source.top, width, height, nullptr, nullptr, instance, nullptr);
					if (!host || !SetLayeredWindowAttributes(host, 0, 0, LWA_ALPHA))
					{
						Fail(*state, MagCaptureReason::HostWindow, GetLastError());
						return;
					}
					child = CreateWindowExW(WS_EX_NOACTIVATE, WC_MAGNIFIER, L"",
						WS_CHILD | WS_VISIBLE, 0, 0, width, height, host, nullptr, instance, nullptr);
					if (!child)
					{
						Fail(*state, MagCaptureReason::ChildWindow, GetLastError());
						return;
					}
					MAGTRANSFORM transform{};
					transform.v[0][0] = transform.v[1][1] = transform.v[2][2] = 1;
					if (!runtime.Api()->setTransform(child, &transform))
					{
						Fail(*state, MagCaptureReason::Transform, GetLastError());
						return;
					}
					if (!surface.resize(width, height))
					{
						Fail(*state, MagCaptureReason::InvalidBuffer, ERROR_NOT_ENOUGH_MEMORY);
						return;
					}
					// alpha 在 Show 前已设为 0，维持可绘制生命周期但桌面永远不显示捕获内容。
					ShowWindow(host, SW_SHOWNOACTIVATE);
					{
						std::lock_guard lock(state->mutex);
						state->snapshot.host = host;
						state->snapshot.child = child;
						state->snapshot.ownerThreadId = GetCurrentThreadId();
						state->snapshot.startupMilliseconds = static_cast<double>(GetTickCount64() - state->startedTick);
						if (state->snapshot.state == MagCaptureState::Starting)
							state->snapshot.state = MagCaptureState::Capturing;
					}
					Diagnostic::Printf(Diagnostic::Stream::Output,
						"[ScrollFollow][Mag] session=%llu initialized host=%p child=%p source=(%ld,%ld,%ld,%ld) alpha=0 startup_ms=%llu\n",
						state->options.session, host, child, source.left, source.top, source.right, source.bottom,
						GetTickCount64() - state->startedTick);
					// 冷启动只在 owner 上等待；采样就绪后才开始独立的一秒结果期限。
					state->lastCompletion.store(GetTickCount64(), std::memory_order_release);
					state->captureArmed.store(true, std::memory_order_release);
					CaptureLoop(*state, *runtime.Api(), child, surface);
				};
				run();
			}
			catch (...) { Fail(*state, MagCaptureReason::ThreadFailure, ERROR_UNHANDLED_EXCEPTION); }
			if (child) cleaned = DestroyWindow(child) != FALSE && cleaned;
			if (host) cleaned = DestroyWindow(host) != FALSE && cleaned;
			surface.reset();
			if (registered) cleaned = UnregisterClassW(CaptureClass, instance) != FALSE && cleaned;
			cleaned = runtime.Reset() && cleaned;
			if (SUCCEEDED(comResult)) CoUninitialize();
			state->callback = {};
			state->publicationOpen.store(false, std::memory_order_release);
			if (!cleaned) Fail(*state, MagCaptureReason::CleanupFailure, GetLastError());
			{
				std::lock_guard lock(state->mutex);
				state->snapshot.host = state->snapshot.child = nullptr;
				state->snapshot.cleanupSucceeded = cleaned;
				state->snapshot.resourcesReleased = true;
				if (state->snapshot.state != MagCaptureState::Failed)
				{
					state->snapshot.state = MagCaptureState::Off;
					state->snapshot.reason = MagCaptureReason::Stopped;
				}
			}
			Diagnostic::Printf(Diagnostic::Stream::Output,
				"[ScrollFollow][Mag] session=%llu cleanup=%d\n", state->options.session, cleaned);
			SetEvent(state->doneEvent);
			return 0;
		}

		DWORD WINAPI CaptureMonitor(void* parameter) noexcept
		{
			std::unique_ptr<std::shared_ptr<SharedState>> holder(
				static_cast<std::shared_ptr<SharedState>*>(parameter));
			const auto state = std::move(*holder);
			holder.reset();
			while (WaitForSingleObject(state->doneEvent, 25) == WAIT_TIMEOUT)
			{
				if (Stopping(*state)) break;
				const bool initializing = !state->captureArmed.load(std::memory_order_acquire);
				if (MagCaptureResultTimedOut(GetTickCount64(), initializing ? state->startedTick :
					state->lastCompletion.load(std::memory_order_acquire), initializing))
				{
					// 只关发布并请求退出；未返回的 PrintWindow 继续独占它的 HWND/DIB/lease。
					Fail(*state, initializing ? MagCaptureReason::StartupTimeout : MagCaptureReason::ResultTimeout,
						ERROR_TIMEOUT);
					break;
				}
			}
			return 0;
		}

		HANDLE StartThread(LPTHREAD_START_ROUTINE procedure, const std::shared_ptr<SharedState>& state)
		{
			auto parameter = std::make_unique<std::shared_ptr<SharedState>>(state);
			const HANDLE thread = CreateThread(nullptr, 0, procedure, parameter.get(), 0, nullptr);
			if (thread) (void)parameter.release();
			return thread;
		}
	}

	MagCaptureService::~MagCaptureService() { Stop(); }

	bool MagCaptureService::Start(MagCaptureOptions options, FrameCallback callback) noexcept
	{
		std::lock_guard lock(mutex_);
		if (state_ && ((state_->ownerThread && WaitForSingleObject(state_->ownerThread, 0) != WAIT_OBJECT_0) ||
			(state_->monitorThread && WaitForSingleObject(state_->monitorThread, 0) != WAIT_OBJECT_0)))
		{
			SetLastError(ERROR_BUSY);
			return false;
		}
		try
		{
			auto state = std::make_shared<SharedState>();
			state_ = state;
			state->options = options;
			state->snapshot.session = options.session;
			state->callback = std::move(callback);
			if (!options.session || !state->callback || !options.framesPerSecond || options.framesPerSecond > 60)
			{
				Fail(*state, MagCaptureReason::InvalidOptions, ERROR_INVALID_PARAMETER);
				return false;
			}
			unsigned long capabilityError = 0;
			if (!SupportsMagCapture(capabilityError))
			{
				Fail(*state, MagCaptureReason::Unsupported, capabilityError);
				return false;
			}
			const auto display = Display::GetSnapshot();
			const auto* primary = display ? display->Primary() : nullptr;
			if (!primary || primary->fallback || primary->pixelWidth <= 0 || primary->pixelHeight <= 0 ||
				primary->bounds.right <= primary->bounds.left || primary->bounds.bottom <= primary->bounds.top)
			{
				Fail(*state, MagCaptureReason::InvalidGeometry, ERROR_INVALID_DATA);
				return false;
			}
			state->snapshot.sourceRect = primary->bounds;
			state->snapshot.workArea = primary->workArea;
			state->snapshot.displayGeneration = display->generation;
			state->snapshot.dpiX = primary->effectiveDpiX;
			state->snapshot.dpiY = primary->effectiveDpiY;
			state->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			state->doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!state->stopEvent || !state->doneEvent)
			{
				Fail(*state, MagCaptureReason::ThreadFailure, GetLastError());
				return false;
			}
			state->snapshot.resourcesReleased = false;
			state->snapshot.state = MagCaptureState::Starting;
			state->startedTick = GetTickCount64();
			state->lastCompletion.store(state->startedTick, std::memory_order_release);
			state->ownerThread = StartThread(&CaptureOwner, state);
			if (!state->ownerThread)
			{
				state->snapshot.resourcesReleased = true;
				Fail(*state, MagCaptureReason::ThreadFailure, GetLastError());
				return false;
			}
			state->monitorThread = StartThread(&CaptureMonitor, state);
			if (!state->monitorThread)
			{
				Fail(*state, MagCaptureReason::ThreadFailure, GetLastError());
				return false;
			}
			return true;
		}
		catch (...)
		{
			if (state_)
			{
				if (!state_->ownerThread) state_->snapshot.resourcesReleased = true;
				Fail(*state_, MagCaptureReason::ThreadFailure, ERROR_NOT_ENOUGH_MEMORY);
			}
			return false;
		}
	}

	void MagCaptureService::Stop() noexcept
	{
		std::lock_guard lock(mutex_);
		if (!state_) return;
		state_->publicationOpen.store(false, std::memory_order_release);
		{
			std::lock_guard stateLock(state_->mutex);
			if (state_->snapshot.state == MagCaptureState::Starting ||
				state_->snapshot.state == MagCaptureState::Capturing)
				state_->snapshot.state = MagCaptureState::Stopping;
		}
		if (state_->stopEvent) SetEvent(state_->stopEvent);
	}

	MagCaptureSnapshot MagCaptureService::Snapshot() const noexcept
	{
		std::lock_guard lock(mutex_);
		if (!state_) return {};
		std::lock_guard stateLock(state_->mutex);
		auto snapshot = state_->snapshot;
		snapshot.resourcesReleased &= (!state_->ownerThread ||
			WaitForSingleObject(state_->ownerThread, 0) == WAIT_OBJECT_0) &&
			(!state_->monitorThread || WaitForSingleObject(state_->monitorThread, 0) == WAIT_OBJECT_0);
		snapshot.publicationOpen = state_->publicationOpen.load(std::memory_order_acquire);
		return snapshot;
	}
}
