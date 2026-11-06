module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "../StartupPlacement.h"
#include "StartupPreview.Effects.Drawing.h"

module Inkeys.UI.StartupPreview;
import Inkeys.UI.RenderPipeline;

#pragma comment(lib, "dxguid.lib")

namespace Inkeys::UI::StartupPreview
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		using Inkeys::UI::RenderPipeline::Client;
		using Inkeys::UI::RenderPipeline::FrameContext;
		using Inkeys::UI::RenderPipeline::FrameResult;
		using namespace std::chrono_literals;

		constexpr wchar_t PreviewClassName[] = L"Inkeys.StartupPreview.Owner.v2";
		constexpr UINT OwnerShowMessage = WM_APP + 0x351;
		constexpr UINT OwnerHideMessage = WM_APP + 0x352;
		constexpr UINT OwnerTopmostMessage = WM_APP + 0x353;
		constexpr UINT OwnerMoveMessage = WM_APP + 0x354;
		constexpr UINT OwnerStopMessage = WM_APP + 0x355;
		constexpr auto HandoffFailureLimit = 750ms;

		[[nodiscard]] int Width(const RECT& bounds) noexcept
		{
			return (std::max)(0L, bounds.right - bounds.left);
		}

		[[nodiscard]] int Height(const RECT& bounds) noexcept
		{
			return (std::max)(0L, bounds.bottom - bounds.top);
		}

		[[nodiscard]] bool SameBounds(const RECT& left, const RECT& right) noexcept
		{
			return left.left == right.left && left.top == right.top
				&& left.right == right.right && left.bottom == right.bottom;
		}

		[[nodiscard]] bool IsSharedDeviceLoss(HRESULT result) noexcept
		{
			return result == DXGI_ERROR_DEVICE_REMOVED
				|| result == DXGI_ERROR_DEVICE_RESET
				|| result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		struct OwnerState final
		{
			OwnerState() noexcept : stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
			~OwnerState() { if (stopEvent) CloseHandle(stopEvent); }

			std::mutex mutex;
			std::mutex presentationMutex;
			std::condition_variable condition;
			HANDLE stopEvent = nullptr;
			HWND window = nullptr;
			DWORD threadId = 0;
			RECT latestBounds{};
			RECT appliedBounds{};
			std::uint64_t latestRevision = 1;
			std::uint64_t appliedRevision = 1;
			bool ready = false;
			bool exited = false;
			bool creationSucceeded = false;
			bool stopRequested = false;
		};

		struct OwnerSnapshot final
		{
			HWND window = nullptr;
			RECT bounds{};
			std::uint64_t revision = 0;
		};

		LRESULT CALLBACK PreviewWindowProc(HWND window, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
			case WM_NCCREATE:
			{
				const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
				SetWindowLongPtrW(window, GWLP_USERDATA,
					reinterpret_cast<LONG_PTR>(create->lpCreateParams));
				return TRUE;
			}
			case WM_MOUSEACTIVATE:
				return MA_NOACTIVATEANDEAT;
			case WM_NCHITTEST:
				return HTCLIENT;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MBUTTONDBLCLK:
			case WM_XBUTTONDOWN:
			case WM_XBUTTONUP:
			case WM_XBUTTONDBLCLK:
				return 0;
			case OwnerShowMessage:
				ShowWindow(window, SW_SHOWNOACTIVATE);
				return 0;
			case OwnerHideMessage:
				ShowWindow(window, SW_HIDE);
				return 0;
			case OwnerTopmostMessage:
				(void)SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
				return 0;
			case OwnerMoveMessage:
			{
				auto* state = reinterpret_cast<OwnerState*>(
					GetWindowLongPtrW(window, GWLP_USERDATA));
				if (!state) return 0;
				std::scoped_lock presentationLock(state->presentationMutex);
				RECT bounds{};
				std::uint64_t revision = 0;
				{
					std::scoped_lock lock(state->mutex);
					bounds = state->latestBounds;
					revision = state->latestRevision;
					if (revision <= state->appliedRevision) return 0;
				}
				if (SetWindowPos(window, HWND_TOPMOST, bounds.left, bounds.top,
					Width(bounds), Height(bounds), SWP_NOACTIVATE))
				{
					std::scoped_lock lock(state->mutex);
					state->appliedBounds = bounds;
					state->appliedRevision = revision;
				}
				return 0;
			}
			case OwnerStopMessage:
				ShowWindow(window, SW_HIDE);
				DestroyWindow(window);
				return 0;
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProcW(window, message, wParam, lParam);
			}
		}

		class PreviewOwner final
		{
		public:
			~PreviewOwner() { Stop(); }

			[[nodiscard]] bool Start(HINSTANCE instance, const RECT& bounds)
			{
				if (thread_.joinable()) return false;
				state_ = std::make_shared<OwnerState>();
				if (!state_->stopEvent)
				{
					state_.reset();
					return false;
				}
				state_->latestBounds = bounds;
				state_->appliedBounds = bounds;
				auto state = state_;
				thread_ = std::thread([state, instance]
					{
						{
							std::scoped_lock lock(state->mutex);
							state->threadId = GetCurrentThreadId();
						}
						WNDCLASSEXW klass{};
						klass.cbSize = sizeof(klass);
						klass.hInstance = instance;
						klass.lpfnWndProc = PreviewWindowProc;
						klass.lpszClassName = PreviewClassName;
						klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
						const ATOM atom = RegisterClassExW(&klass);
						const DWORD error = atom ? ERROR_SUCCESS : GetLastError();
						RECT initial{};
						bool stopBeforeCreate = false;
						{
							std::scoped_lock lock(state->mutex);
							initial = state->latestBounds;
							stopBeforeCreate = state->stopRequested;
						}
						HWND window = nullptr;
						if (!stopBeforeCreate && (atom || error == ERROR_CLASS_ALREADY_EXISTS))
						{
							window = CreateWindowExW(
								WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
								PreviewClassName, L"", WS_POPUP,
								initial.left, initial.top, Width(initial), Height(initial),
								nullptr, nullptr, instance, state.get());
							if (window)
								(void)SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
									SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
						}
						bool stopAfterCreate = false;
						{
							std::scoped_lock lock(state->mutex);
							stopAfterCreate = state->stopRequested;
							state->window = stopAfterCreate ? nullptr : window;
							state->creationSucceeded = window != nullptr && !stopAfterCreate;
							state->ready = true;
						}
						state->condition.notify_all();
						if (window)
						{
							// 消息队列尚未建立时的停止投递可能失败，阻塞前由 owner 再次核对。
							bool stopBeforeWait = stopAfterCreate;
							if (!stopBeforeWait)
							{
								std::scoped_lock lock(state->mutex);
								stopBeforeWait = state->stopRequested;
							}
							if (stopBeforeWait)
							{
								ShowWindow(window, SW_HIDE);
								DestroyWindow(window);
							}
							else
							{
								const HANDLE stopEvent = state->stopEvent;
								for (;;)
								{
									// 事件优先于窗口消息；超时仅兜底 SetEvent/投递均失败的停止请求。
									const DWORD wait = MsgWaitForMultipleObjectsEx(1, &stopEvent,
										1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
									if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
									{
										std::scoped_lock lock(state->mutex);
										if (state->stopRequested) break;
									}
									if (wait == WAIT_TIMEOUT) continue;
									if (wait != WAIT_OBJECT_0 + 1) break;
									MSG message{};
									if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) continue;
									if (message.message == WM_QUIT) break;
									TranslateMessage(&message);
									DispatchMessageW(&message);
								}
								// WM_QUIT 也可能先于停止窗口消息到达，仍由创建线程销毁 HWND。
								if (IsWindow(window))
								{
									ShowWindow(window, SW_HIDE);
									DestroyWindow(window);
								}
							}
						}
						{
							std::scoped_lock lock(state->mutex);
							state->window = nullptr;
							state->exited = true;
						}
						state->condition.notify_all();
					});
				std::unique_lock lock(state_->mutex);
				if (!state_->condition.wait_for(lock, 2s,
					[this] { return state_->ready; })) return false;
				return state_->creationSucceeded && !state_->stopRequested;
			}

			void Stop() noexcept
			{
				if (!thread_.joinable()) return;
				auto state = state_;
				RequestStop();
				std::unique_lock lock(state->mutex);
				const bool exited = state->condition.wait_for(lock, 1500ms,
					[&state] { return state->exited; });
				lock.unlock();
				if (exited) thread_.join();
				else thread_.detach();
				state_.reset();
			}

			[[nodiscard]] OwnerSnapshot Snapshot() const noexcept
			{
				if (!state_) return {};
				std::scoped_lock lock(state_->mutex);
				return { state_->window, state_->appliedBounds,
					state_->appliedRevision };
			}

			[[nodiscard]] BOOL PresentPixels(HDC source, BYTE alpha,
				UINT width, UINT height, const RECT& dirty) noexcept
			{
				if (!state_ || !source || width == 0 || height == 0) return FALSE;
				auto state = state_;
				std::scoped_lock presentationLock(state->presentationMutex);
				HWND window = nullptr;
				RECT bounds{};
				{
					std::scoped_lock lock(state->mutex);
					window = state->window;
					bounds = state->appliedBounds;
				}
				if (!window || Width(bounds) != static_cast<int>(width)
					|| Height(bounds) != static_cast<int>(height)) return FALSE;
				POINT destination{ bounds.left, bounds.top };
				POINT sourcePoint{};
				SIZE size{ static_cast<LONG>(width), static_cast<LONG>(height) };
				BLENDFUNCTION blend{ AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
				UPDATELAYEREDWINDOWINFO update{};
				update.cbSize = sizeof(update);
				// 每帧显式提交 owner 几何，避免某些 DWM 路径接受空几何却不刷新。
				update.pptDst = &destination;
				update.psize = &size;
				update.pptSrc = &sourcePoint;
				update.hdcSrc = source;
				update.pblend = &blend;
				// 脏区之外沿用窗口已提交像素；扩大的粒子窗口在拼好后只回读并提交主栏区域。
				update.prcDirty = &dirty;
				update.dwFlags = ULW_ALPHA;
				return UpdateLayeredWindowIndirect(window, &update);
			}

			void Show() noexcept { Post(OwnerShowMessage); }
			void Hide() noexcept { Post(OwnerHideMessage); }
			void RevalidateTopmost() noexcept { Post(OwnerTopmostMessage); }
			void RequestStop() noexcept
			{
				if (!state_) return;
				auto state = state_;
				HWND window = nullptr;
				DWORD threadId = 0;
				{
					std::scoped_lock lock(state->mutex);
					if (state->exited) return;
					// 消息只用于唤醒；迟到的 owner 必须能读到持续有效的停止请求。
					state->stopRequested = true;
					window = state->window;
					threadId = state->threadId;
				}
				(void)SetEvent(state->stopEvent);
				if (window) (void)PostMessageW(window, OwnerStopMessage, 0, 0);
				else if (threadId) (void)PostThreadMessageW(threadId, WM_QUIT, 0, 0);
			}

			void Move(const RECT& bounds, std::uint64_t revision) noexcept
			{
				if (!state_) return;
				HWND window = nullptr;
				{
					std::scoped_lock lock(state_->mutex);
					if (revision <= state_->latestRevision) return;
					state_->latestRevision = revision;
					state_->latestBounds = bounds;
					window = state_->window;
				}
				if (window) PostMessageW(window, OwnerMoveMessage, 0, 0);
			}

			[[nodiscard]] bool HasExited() const noexcept
			{
				if (!state_) return false;
				std::scoped_lock lock(state_->mutex);
				return state_->exited;
			}

		private:
			void Post(UINT message) noexcept
			{
				if (const auto snapshot = Snapshot(); snapshot.window)
					PostMessageW(snapshot.window, message, 0, 0);
			}

			std::shared_ptr<OwnerState> state_;
			std::thread thread_;
		};

		struct RenderResources final
		{
			std::uint64_t generation = 0;
			UINT width = 0, height = 0;
			ComPtr<ID2D1DeviceContext> context;
			ComPtr<ID2D1Bitmap1> target;
			ComPtr<ID2D1StrokeStyle> ringStroke;
			ComPtr<ID2D1GdiInteropRenderTarget> gdi;
			ComPtr<ID2D1SolidColorBrush> progressTrack, progressFill;
			Drawing::EffectRenderer effect;
			Effects::Appearance appearance;
			// 上一次成功提交的内容范围；资源重建后为空，下一帧整窗提交。
			RECT committedContent{};
			bool hasCommittedContent = false;

			void Reset() noexcept
			{
				effect.Reset();
				if (context) context->SetTarget(nullptr);
				ringStroke.Reset(); progressFill.Reset(); progressTrack.Reset();
				gdi.Reset(); target.Reset(); context.Reset();
				generation = 0; width = 0; height = 0;
				committedContent = {}; hasCommittedContent = false;
			}
		};

		enum class Phase : std::uint8_t
		{
			Preparing,
			WaitingForBar,
			FadingOut,
			WaitingForOpaqueBar,
			Failure,
			ExitFadingOut,
			Stopping,
			Stopped,
		};

		struct Runtime final
		{
			StartOptions options;
			Effects::Rect targetRect{}, scatterRect{};
			std::uint32_t effectSeed = 1;
			PreviewOwner owner;
			RenderResources resources;
			std::chrono::steady_clock::time_point shimmerEpoch{};
			std::chrono::steady_clock::time_point shownTime{};
			std::chrono::steady_clock::time_point fadeStart{};
			std::chrono::steady_clock::time_point barFadeStart{};
			std::chrono::steady_clock::time_point firstHandoffFailure{};
			ProgressVisualReducer progressVisual;
			HandoffTimingReducer handoffTiming;
			Phase phase = Phase::Preparing;
			std::uint8_t committedPreviewAlpha = 0;
			std::uint8_t fadeStartAlpha = 255;
			std::chrono::milliseconds handoffFadeDuration =
				StartupPreviewQuickHandoffDuration;
			OrderedHandoffReducer handoff;
			std::atomic_bool active = false;
			std::atomic_bool registered = false;
			std::atomic_bool firstFrameCommitted = false;
			std::atomic_bool previewFadeOutCommitted = false;
			std::atomic_bool barAlpha0Committed = false;
			std::atomic_bool barAlpha255Committed = false;
			std::atomic_bool automaticStopPosted = false;
			std::atomic_bool fatalRequested = false;
			std::atomic_bool exitFadeRequested = false;
			std::atomic_bool failureFrameCommitted = false;
			std::atomic_bool presentationAvailable = false;
			std::atomic_int committedBarAlpha = 255;
			std::mutex conditionMutex;
			std::condition_variable condition;
			std::mutex failureMutex;
			std::condition_variable failureCondition;
			std::mutex fadeOutMutex;
			std::condition_variable fadeOutCondition;
		};

		std::mutex runtimeMutex;
		std::shared_ptr<Runtime> currentRuntime;
		std::atomic<BarStartupState> globalBarStartupState =
			BarStartupState::NotStarted;
		std::atomic<double> publishedStartupBarWidthDip = 0.0;

		[[nodiscard]] std::shared_ptr<Runtime> SnapshotRuntime() noexcept
		{
			std::scoped_lock lock(runtimeMutex);
			return currentRuntime;
		}

		[[nodiscard]] float Clamp01(double value) noexcept
		{
			return static_cast<float>(std::clamp(value, 0.0, 1.0));
		}

		[[nodiscard]] float DpiScale(const Runtime& runtime) noexcept
		{
			return static_cast<float>(ResolveStartupPreviewScale(
				runtime.options.dpi, runtime.options.barZoom, runtime.options.uiBaseScale));
		}

		[[nodiscard]] HRESULT EnsureResources(Runtime& runtime,
			const FrameContext& frame, UINT width, UINT height)
		{
			if (!frame.epoch.d2dDevice || width == 0 || height == 0) return E_INVALIDARG;
			auto& resources = runtime.resources;
			if (resources.generation == frame.epoch.generation && resources.width == width
				&& resources.height == height && resources.context && resources.target && resources.gdi) return S_OK;
			RenderResources next;
			next.generation = frame.epoch.generation; next.width = width; next.height = height;
			HRESULT result = frame.epoch.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &next.context);
			if (FAILED(result)) return result;
			const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			result = next.context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &properties, &next.target);
			if (SUCCEEDED(result)) result = next.context.As(&next.gdi);
			if (SUCCEEDED(result)) result = next.context->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, .24f), &next.progressTrack);
			if (SUCCEEDED(result)) result = next.context->CreateSolidColorBrush(D2D1::ColorF(.38f, .8f, 1.f), &next.progressFill);
			ComPtr<ID2D1Factory> factory; next.context->GetFactory(&factory);
			if (SUCCEEDED(result)) result = Drawing::CreateRingStroke(factory.Get(), &next.ringStroke);
			const bool floating = StartupPlacementIsFolded(runtime.options.startupPlacement);
			next.appearance = Effects::ResolveAppearance(runtime.options.themeMode,
				runtime.options.backgroundOpacity, runtime.options.contentOpacity, floating);
			if (SUCCEEDED(result)) result = next.effect.Initialize(next.context.Get(), width, height,
				runtime.targetRect, runtime.scatterRect, DpiScale(runtime), floating,
				runtime.options.effect, next.appearance, runtime.effectSeed);
			if (FAILED(result)) return result;
			resources.Reset(); resources = std::move(next);
			return S_OK;
		}

		[[nodiscard]] double EffectElapsed(const Runtime& runtime,
			std::chrono::steady_clock::time_point now) noexcept
		{
			return runtime.shownTime == std::chrono::steady_clock::time_point{} ? 0.0
				: (std::max)(0.0, std::chrono::duration<double>(now - runtime.shownTime).count());
		}

		[[nodiscard]] HRESULT PrepareBackground(Runtime& runtime, const FrameContext& frame)
		{
			const double width = Effects::Width(runtime.targetRect), height = Effects::Height(runtime.targetRect);
			const GeometryRect maskBounds{ 0.0, 0.0, width, height };
			const auto gradient = ResolveStartupPreviewShimmerGradient(width, height);
			const double supportLength = std::hypot(gradient.endX - gradient.startX, gradient.endY - gradient.startY);
			const auto travel = ResolveShimmerTravel(maskBounds, gradient, (std::max)(8.0, supportLength * .10));
			const double elapsed = EffectElapsed(runtime, frame.frameTime);
			const double shimmerElapsed = runtime.options.effect == StartupPreviewEffect::PixelAssembly
				? (std::max)(0.0, elapsed - Effects::AssemblySeconds) : elapsed;
			const auto translation = ResolveShimmerTranslation(travel,
				EaseShimmerPhase(std::fmod(shimmerElapsed, StartupPreviewShimmerCycleSeconds) / StartupPreviewShimmerCycleSeconds));
			return runtime.resources.effect.Prepare(elapsed,
				D2D1::Point2F(static_cast<float>(gradient.startX + translation.x), static_cast<float>(gradient.startY + translation.y)),
				D2D1::Point2F(static_cast<float>(gradient.endX + translation.x), static_cast<float>(gradient.endY + translation.y)));
		}

		[[nodiscard]] HRESULT DrawProgress(Runtime& runtime,
			const Inkeys::Startup::Snapshot& snapshot,
			std::chrono::steady_clock::time_point now, bool success,
			float contentOpacity, bool& progressVisible)
		{
			progressVisible = false;
			const bool failed = snapshot.failed || runtime.phase == Phase::Failure;
			const auto visual = runtime.progressVisual.Update(
				now, snapshot.actualRatio, success, failed);
			if (contentOpacity <= 0.f || visual.opacity <= 0.001) return S_OK;
			const float scale = DpiScale(runtime);
			const float width = Effects::Width(runtime.targetRect);
			const float height = Effects::Height(runtime.targetRect);
			const float opacity = Clamp01(visual.opacity * contentOpacity);
			runtime.resources.context->SetTransform(D2D1::Matrix3x2F::Translation(runtime.targetRect.left, runtime.targetRect.top));
			const HRESULT result = Drawing::DrawProgress(runtime.resources.context.Get(), runtime.resources.ringStroke.Get(),
				runtime.resources.progressTrack.Get(), runtime.resources.progressFill.Get(),
				runtime.options.startupPlacement, width, height, scale,
				visual.displayedRatio, opacity, visual.failureColorProgress >= 1.0, runtime.resources.appearance.light);
			runtime.resources.context->SetTransform(D2D1::Matrix3x2F::Identity());
			progressVisible = result == S_OK;
			return result;
		}

		void RequestAutomaticStop(const std::shared_ptr<Runtime>& runtime)
		{
			if (runtime->automaticStopPosted.exchange(true,
				std::memory_order_acq_rel)) return;
			// 当前 callback 自己位于 scheduler 渲染线程，不能同步 Unregister 等待自身退出。
			runtime->resources.Reset();
			runtime->active.store(false, std::memory_order_release);
			runtime->phase = Phase::Stopping;
			runtime->owner.Hide();
			runtime->owner.RequestStop();
		}

		void ReleaseResourcesOnRenderThread(
			const std::shared_ptr<Runtime>& runtime) noexcept
		{
			try
			{
				auto completed = std::make_shared<std::promise<void>>();
				auto future = completed->get_future();
				if (Inkeys::UI::RenderPipeline::PostControl([runtime, completed]
					{
						runtime->resources.Reset();
						try { completed->set_value(); }
						catch (...) {}
					}))
					(void)future.wait_for(500ms);
				else
				{
					// 管线已停止时没有 render thread 可投递，只做进程收尾兜底。
					runtime->resources.Reset();
				}
			}
			catch (...) {}
		}

		void RecoverOpaqueBar(const std::shared_ptr<Runtime>& runtime) noexcept
		{
			const auto recovery = runtime->handoff.Bypass();
			if (recovery.requestBarFadeIn && runtime->options.requestBarAlpha)
				runtime->options.requestBarAlpha(255);
			if (runtime->committedBarAlpha.load(std::memory_order_acquire) == 255
				&& runtime->handoff.BarAlpha255Committed().destroyPreview)
				RequestAutomaticStop(runtime);
		}

		[[nodiscard]] FrameResult RenderFrame(
			const std::shared_ptr<Runtime>& runtime, const FrameContext& frame)
		{
			if (!runtime->active.load(std::memory_order_acquire))
				return FrameResult::Idle;
			if (runtime->fatalRequested.exchange(false, std::memory_order_acq_rel))
				runtime->phase = Phase::Failure;
			if (runtime->exitFadeRequested.exchange(false, std::memory_order_acq_rel))
			{
				runtime->phase = Phase::ExitFadingOut;
				runtime->fadeStart = frame.frameTime;
				runtime->fadeStartAlpha = runtime->committedPreviewAlpha;
			}

			const auto snapshot = runtime->options.progress
				? runtime->options.progress->GetSnapshot()
				: Inkeys::Startup::Snapshot{};
			const bool trackerComplete = snapshot.totalUnits != 0
				&& snapshot.completedUnits == snapshot.totalUnits;
			const bool barReady = globalBarStartupState.load(
				std::memory_order_acquire) == BarStartupState::FirstFrameCommitted;
			const bool success = trackerComplete && barReady && !snapshot.failed;
			const int barAlpha = runtime->committedBarAlpha.load(std::memory_order_acquire);
			if (barAlpha == 0 && runtime->phase == Phase::WaitingForBar)
			{
				const auto handoff = runtime->handoff.BarAlpha0Committed(success && Effects::AssemblyReady(
					runtime->options.effect, EffectElapsed(*runtime, frame.frameTime)));
				if (handoff.requestPreviewFadeOut)
				{
					// handoff 启动时冻结档位，后续提交不再改变双方时长。
					runtime->handoffFadeDuration =
						runtime->handoffTiming.FreezeDuration();
					runtime->phase = Phase::FadingOut;
					runtime->fadeStart = frame.frameTime;
					runtime->fadeStartAlpha = runtime->committedPreviewAlpha;
				}
			}

			const auto owner = runtime->owner.Snapshot();
			if (!owner.window)
			{
				RecoverOpaqueBar(runtime);
				return FrameResult::Idle;
			}
			const UINT width = static_cast<UINT>(Width(owner.bounds));
			const UINT height = static_cast<UINT>(Height(owner.bounds));
			const HRESULT resourceResult = EnsureResources(*runtime, frame, width, height);
			if (FAILED(resourceResult))
			{
				if (barReady) RecoverOpaqueBar(runtime);
				return IsSharedDeviceLoss(resourceResult)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}

			std::uint8_t windowAlpha = 255;
			if (!runtime->firstFrameCommitted.load(std::memory_order_acquire))
				windowAlpha = 0;
			else if (runtime->options.effect != StartupPreviewEffect::PixelAssembly
				&& runtime->shownTime != std::chrono::steady_clock::time_point{})
				windowAlpha = ResolveFadeInAlpha(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						frame.frameTime - runtime->shownTime),
					StartupPreviewFadeInDuration);
			if (runtime->phase == Phase::FadingOut
				|| runtime->phase == Phase::WaitingForOpaqueBar)
			{
				windowAlpha = ResolveFadeOutAlphaFrom(runtime->fadeStartAlpha,
					std::chrono::duration_cast<std::chrono::milliseconds>(
						frame.frameTime - runtime->fadeStart),
					runtime->handoffFadeDuration);
			}
			else if (runtime->phase == Phase::ExitFadingOut)
				windowAlpha = ResolveFadeOutAlphaFrom(runtime->fadeStartAlpha,
					std::chrono::duration_cast<std::chrono::milliseconds>(
						frame.frameTime - runtime->fadeStart),
					StartupPreviewExitFadeOutDuration);
			if (runtime->phase == Phase::Failure) windowAlpha = 255;

			auto& resources = runtime->resources;
			const HRESULT backgroundResult = PrepareBackground(*runtime, frame);
			if (FAILED(backgroundResult))
			{
				if (backgroundResult == D2DERR_RECREATE_TARGET) resources.Reset();
				if (barReady) RecoverOpaqueBar(runtime);
				return IsSharedDeviceLoss(backgroundResult) ? FrameResult::DeviceLost : FrameResult::Retry;
			}
			resources.context->SetTarget(resources.target.Get());
			resources.context->BeginDraw();
			resources.context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			// 背景及所有光影先整组合成，进度独立置顶；用户alpha和ULW交接alpha各只应用一次。
			resources.effect.DrawBackground(resources.context.Get());
			bool progressVisible = false;
			const HRESULT progressResult = DrawProgress(*runtime, snapshot, frame.frameTime,
				success, resources.appearance.contentOpacity, progressVisible);
			if (FAILED(progressResult))
			{
				// 圆弧构造失败不能冒充可见进度或最终红帧；按既有资源失败路径重试/绕过。
				const HRESULT end = resources.context->EndDraw();
				if (end == D2DERR_RECREATE_TARGET) resources.Reset();
				if (barReady) RecoverOpaqueBar(runtime);
				return IsSharedDeviceLoss(progressResult) || IsSharedDeviceLoss(end)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}

			const auto presentOwner = runtime->owner.Snapshot();
			if (!presentOwner.window || !SameBounds(presentOwner.bounds, owner.bounds))
			{
				const HRESULT end = resources.context->EndDraw();
				if (end == D2DERR_RECREATE_TARGET) resources.Reset();
				return IsSharedDeviceLoss(end)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			const RECT content = resources.effect.ContentBounds();
			RECT dirty{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			if (resources.hasCommittedContent) UnionRect(&dirty, &content, &resources.committedContent);
			HDC source = nullptr;
			HRESULT getDc = resources.gdi->GetDC(
				D2D1_DC_INITIALIZE_MODE_COPY, &source);
			BOOL update = FALSE;
			HRESULT release = E_FAIL;
			if (SUCCEEDED(getDc) && source)
			{
				// per-pixel alpha 保留在 D2D surface，整窗淡入淡出只走 SourceConstantAlpha。
				update = runtime->owner.PresentPixels(source,
					windowAlpha, width, height, dirty);
				release = resources.gdi->ReleaseDC(nullptr);
			}
			else if (SUCCEEDED(getDc)) getDc = E_POINTER;
			const HRESULT endDraw = resources.context->EndDraw();
			const bool committed = SUCCEEDED(getDc) && update != FALSE
				&& SUCCEEDED(release) && SUCCEEDED(endDraw);
			if (!committed)
			{
				if (endDraw == D2DERR_RECREATE_TARGET
					|| release == D2DERR_RECREATE_TARGET) resources.Reset();
				if (barReady)
				{
					if (runtime->firstHandoffFailure
						== std::chrono::steady_clock::time_point{})
						runtime->firstHandoffFailure = frame.frameTime;
					if (frame.frameTime - runtime->firstHandoffFailure
						>= HandoffFailureLimit) RecoverOpaqueBar(runtime);
				}
				return IsSharedDeviceLoss(getDc) || IsSharedDeviceLoss(release)
					|| IsSharedDeviceLoss(endDraw)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			runtime->firstHandoffFailure = {};
			runtime->committedPreviewAlpha = windowAlpha;
			resources.committedContent = content;
			resources.hasCommittedContent = true;
			// 只有真实 ULW 成功的可见帧才有资格切换慢速 handoff 档位。
			runtime->handoffTiming.PreviewFrameCommitted(
				progressVisible, windowAlpha);

			if (!runtime->firstFrameCommitted.exchange(true,
				std::memory_order_acq_rel))
			{
				// 首帧必须以 SourceConstantAlpha=0 提交，然后无激活显示并从透明开始。
				runtime->presentationAvailable.store(true, std::memory_order_release);
				runtime->shownTime = frame.frameTime;
				runtime->shimmerEpoch = frame.frameTime;
				runtime->progressVisual.MarkPreviewShown(frame.frameTime);
				(void)runtime->handoff.PreviewFirstAlpha0Committed();
				runtime->phase = Phase::WaitingForBar;
				runtime->owner.Show();
				if (runtime->options.progress)
					(void)runtime->options.progress->Complete(
						Inkeys::Startup::Milestone::PreviewFirstFrameCommitted);
				runtime->condition.notify_all();
			}

			if (runtime->phase == Phase::FadingOut && windowAlpha == 0)
			{
				runtime->previewFadeOutCommitted.store(true,
					std::memory_order_release);
				const auto handoff = runtime->handoff.PreviewFadeOutCommitted();
				if (handoff.requestBarFadeIn)
					runtime->phase = Phase::WaitingForOpaqueBar;
			}
			if (runtime->phase == Phase::ExitFadingOut && windowAlpha == 0)
			{
				runtime->previewFadeOutCommitted.store(true,
					std::memory_order_release);
				runtime->fadeOutCondition.notify_all();
				RequestAutomaticStop(runtime);
			}
			if (runtime->phase == Phase::WaitingForOpaqueBar)
			{
				if (runtime->barFadeStart == std::chrono::steady_clock::time_point{})
					runtime->barFadeStart = frame.frameTime;
				const auto requestedAlpha = ResolveFadeInAlpha(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						frame.frameTime - runtime->barFadeStart),
					runtime->handoffFadeDuration);
				if (runtime->options.requestBarAlpha)
					runtime->options.requestBarAlpha(requestedAlpha);
			}
			if (runtime->phase == Phase::WaitingForOpaqueBar && barAlpha == 255)
			{
				runtime->barAlpha255Committed.store(true,
					std::memory_order_release);
				if (runtime->handoff.BarAlpha255Committed().destroyPreview)
					RequestAutomaticStop(runtime);
			}
			if (runtime->phase == Phase::Failure
				&& !runtime->failureFrameCommitted.exchange(true,
					std::memory_order_acq_rel))
			{
				runtime->failureCondition.notify_all();
			}
			return FrameResult::Continue;
		}
	}

	RECT ResolveStartupPreviewBounds(const RECT& monitorBounds,
		const RECT& workArea, double widthDip, double heightDip,
		UINT dpi, double zoom, StartupPlacement placement, double uiBaseScale,
		double desktopDockLine) noexcept
	{
		const bool floating = StartupPlacementIsFolded(placement);
		const double visibleWidthDip = (floating ? 80.0 : widthDip) + 1.0;
		const double visibleHeightDip = (floating ? 80.0 : heightDip) + 1.0;
		return ResolveStartupPlacementBounds(placement, monitorBounds, workArea,
			visibleWidthDip, visibleHeightDip, ResolveStartupPreviewScale(dpi, zoom, uiBaseScale),
			static_cast<double>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI, desktopDockLine);
	}

	bool Start(const StartOptions& options)
	{
		try
		{
			if (!options.enabled || !options.progress
				|| !Inkeys::UI::RenderPipeline::IsInitialized()) return false;
			{
				std::scoped_lock lock(runtimeMutex);
				if (currentRuntime) return false;
			}
			auto runtime = std::make_shared<Runtime>();
			runtime->options = options;
			runtime->options.effect = NormalizeStartupPreviewEffect(static_cast<int>(options.effect));
			runtime->effectSeed = GetTickCount() ^ GetCurrentProcessId();
			runtime->options.startupPlacement = NormalizeStartupPlacement(static_cast<int>(options.startupPlacement));
			runtime->options.cachedWidthDip = ResolveCachedStartupBarWidthDip(
				options.cachedWidthDip);
			runtime->options.dpi = options.dpi == 0
				? USER_DEFAULT_SCREEN_DPI : options.dpi;
			runtime->options.barZoom = std::isfinite(options.barZoom)
				&& options.barZoom > 0.0 ? options.barZoom : 1.0;
			if (!runtime->options.instance)
				runtime->options.instance = GetModuleHandleW(nullptr);
			RECT workArea = options.workArea;
			if (Width(workArea) == 0 || Height(workArea) == 0)
				workArea = options.monitorBounds;
			RECT bounds = options.contentBounds;
			if (Width(bounds) == 0 || Height(bounds) == 0)
				bounds = ResolveStartupPreviewBounds(options.monitorBounds, workArea,
					runtime->options.cachedWidthDip, DefaultStartupBarHeightDip,
					runtime->options.dpi, runtime->options.barZoom, runtime->options.startupPlacement, runtime->options.uiBaseScale,
					runtime->options.desktopDockLine);
			// 粒子窗口可以外扩，但正式目标坐标和逻辑宽度缓存保持不变。
			const Effects::Rect target{ static_cast<float>(bounds.left), static_cast<float>(bounds.top),
				static_cast<float>(bounds.right), static_cast<float>(bounds.bottom) };
			const Effects::Rect work{ static_cast<float>(workArea.left), static_cast<float>(workArea.top),
				static_cast<float>(workArea.right), static_cast<float>(workArea.bottom) };
			auto presentation = target;
			auto scatter = target;
			if (runtime->options.effect == StartupPreviewEffect::PixelAssembly)
			{
				scatter = Effects::ScatterBounds(target, work,
					StartupPlacementIsFolded(runtime->options.startupPlacement));
				presentation = Effects::PresentationBounds(target, scatter, work, DpiScale(*runtime));
			}
			runtime->targetRect = { target.left - presentation.left, target.top - presentation.top,
				target.right - presentation.left, target.bottom - presentation.top };
			runtime->scatterRect = { scatter.left - presentation.left, scatter.top - presentation.top,
				scatter.right - presentation.left, scatter.bottom - presentation.top };
			bounds = RECT{ static_cast<LONG>(presentation.left), static_cast<LONG>(presentation.top),
				static_cast<LONG>(presentation.right), static_cast<LONG>(presentation.bottom) };
			if (!runtime->owner.Start(runtime->options.instance, bounds)) return false;
			runtime->active.store(true, std::memory_order_release);
			{
				std::scoped_lock lock(runtimeMutex);
				currentRuntime = runtime;
			}
			const bool registered = Inkeys::UI::RenderPipeline::Register(
				Client::StartupPreview, [runtime](const FrameContext& frame)
					{ return RenderFrame(runtime, frame); });
			if (!registered)
			{
				Stop();
				return false;
			}
			runtime->registered.store(true, std::memory_order_release);
			(void)options.progress->Complete(
				Inkeys::Startup::Milestone::PreviewOwnerReady);
			(void)options.progress->Complete(
				Inkeys::Startup::Milestone::PreviewRenderClientReady);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
			std::unique_lock lock(runtime->conditionMutex);
			if (!runtime->condition.wait_for(lock, 2s, [runtime]
				{ return runtime->firstFrameCommitted.load(std::memory_order_acquire); }))
			{
				lock.unlock();
				Stop();
				return false;
			}
			return true;
		}
		catch (...)
		{
			Stop();
			return false;
		}
	}

	void Stop() noexcept
	{
		std::shared_ptr<Runtime> runtime;
		{
			std::scoped_lock lock(runtimeMutex);
			runtime = std::move(currentRuntime);
		}
		if (!runtime) return;
		runtime->active.store(false, std::memory_order_release);
		if (runtime->registered.exchange(false, std::memory_order_acq_rel))
			Inkeys::UI::RenderPipeline::Unregister(Client::StartupPreview);
		ReleaseResourcesOnRenderThread(runtime);
		runtime->owner.Hide();
		runtime->owner.Stop();
		runtime->phase = Phase::Stopped;
	}

	bool IsActive() noexcept
	{
		const auto runtime = SnapshotRuntime();
		return runtime && runtime->active.load(std::memory_order_acquire);
	}

	bool IsPresentationAvailable() noexcept
	{
		const auto runtime = SnapshotRuntime();
		return runtime && runtime->presentationAvailable.load(std::memory_order_acquire);
	}

	bool ShouldBarStartTransparent() noexcept
	{
		const auto runtime = SnapshotRuntime();
		return runtime && runtime->active.load(std::memory_order_acquire)
			&& runtime->firstFrameCommitted.load(std::memory_order_acquire);
	}

	void RevalidateTopmost() noexcept
	{
		if (const auto runtime = SnapshotRuntime()) runtime->owner.RevalidateTopmost();
	}

	void NotifyBarFirstCommittedFrame(double mainButtonTargetWidthDip,
		double layoutTotalWidthDip, bool expanded) noexcept
	{
		// 即使首帧折叠，宽度缓存也代表展开目标包络，不能以80DIP污染下次居中启动。
		(void)expanded;
		if (!std::isfinite(mainButtonTargetWidthDip)
			|| mainButtonTargetWidthDip < CachedStartupBarWidthMinimumDip
			|| mainButtonTargetWidthDip > CachedStartupBarWidthMaximumDip
			|| (!std::isfinite(layoutTotalWidthDip)
				|| layoutTotalWidthDip <= 0.0
				|| layoutTotalWidthDip > CachedStartupBarWidthMaximumDip
					- mainButtonTargetWidthDip
					- StartupMainButtonToMainBarGapDip)) return;
		const double widthDip = CalculateStartupBarTotalWidthDip(
			mainButtonTargetWidthDip, layoutTotalWidthDip, true);
		if (!IsValidCachedStartupBarWidthDip(widthDip)) return;
		double expected = 0.0;
		(void)publishedStartupBarWidthDip.compare_exchange_strong(
			expected, widthDip, std::memory_order_acq_rel);
	}

	bool TakeCommittedStartupBarWidthDip(double& widthDip) noexcept
	{
		const double value = publishedStartupBarWidthDip.exchange(
			0.0, std::memory_order_acq_rel);
		if (!IsValidCachedStartupBarWidthDip(value)) return false;
		widthDip = value;
		return true;
	}

	void SetBarStartupState(BarStartupState state) noexcept
	{
		auto current = globalBarStartupState.load(std::memory_order_acquire);
		for (;;)
		{
			if (current == BarStartupState::FirstFrameCommitted
				|| static_cast<unsigned>(state) < static_cast<unsigned>(current)) return;
			if (globalBarStartupState.compare_exchange_weak(current, state,
				std::memory_order_acq_rel)) break;
		}
		if (state == BarStartupState::FirstFrameCommitted)
			(void)Inkeys::Startup::Report(
				Inkeys::Startup::Milestone::BarFirstFrameCommitted);
		else if (state == BarStartupState::WindowMissing)
			(void)Inkeys::Startup::ReportFailure(0xB001u);
		else if (state == BarStartupState::ClientRegistrationFailed)
			(void)Inkeys::Startup::ReportFailure(0xB002u);
		else if (state == BarStartupState::StartupFailed)
			(void)Inkeys::Startup::ReportFailure(0xB003u);
		else if (state == BarStartupState::StoppedBeforeReady)
			(void)Inkeys::Startup::ReportFailure(0xB004u);
		if (const auto runtime = SnapshotRuntime())
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
	}

	BarStartupState GetBarStartupState() noexcept
	{
		return globalBarStartupState.load(std::memory_order_acquire);
	}

	void NotifyBarPresentationAlphaCommitted(std::uint8_t alpha) noexcept
	{
		if (const auto runtime = SnapshotRuntime())
		{
			runtime->committedBarAlpha.store(alpha, std::memory_order_release);
			if (alpha == 0) runtime->barAlpha0Committed.store(true,
				std::memory_order_release);
			if (alpha == 255) runtime->barAlpha255Committed.store(true,
				std::memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		}
	}

	void RequestFailureFrame() noexcept
	{
		if (const auto runtime = SnapshotRuntime())
		{
			runtime->fatalRequested.store(true, std::memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		}
	}

	bool WaitForFailureFrame(std::chrono::milliseconds timeout) noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime) return false;
		std::unique_lock lock(runtime->failureMutex);
		return runtime->failureCondition.wait_for(lock, timeout, [runtime]
			{ return runtime->failureFrameCommitted.load(std::memory_order_acquire); });
	}

	void RequestFadeOutForExit() noexcept
	{
		if (const auto runtime = SnapshotRuntime())
		{
			runtime->exitFadeRequested.store(true, std::memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		}
	}

	bool WaitForFadeOut(std::chrono::milliseconds timeout) noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime) return false;
		std::unique_lock lock(runtime->fadeOutMutex);
		return runtime->fadeOutCondition.wait_for(lock, timeout, [runtime]
			{ return runtime->previewFadeOutCommitted.load(
				std::memory_order_acquire); });
	}

	Diagnostics SnapshotDiagnostics() noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime) return {};
		return {
			!StartupPlacementIsFolded(runtime->options.startupPlacement)
				? runtime->options.cachedWidthDip : 80.0,
			runtime->active.load(std::memory_order_acquire),
			runtime->firstFrameCommitted.load(std::memory_order_acquire),
			runtime->previewFadeOutCommitted.load(std::memory_order_acquire),
			runtime->barAlpha0Committed.load(std::memory_order_acquire),
			runtime->barAlpha255Committed.load(std::memory_order_acquire),
			runtime->automaticStopPosted.load(std::memory_order_acquire),
			runtime->owner.HasExited(),
			!runtime->active.load(std::memory_order_acquire),
		};
	}
}
