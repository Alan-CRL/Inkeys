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
#include <memory>
#include <mutex>
#include <thread>

#include "../Bar/Bar.BottomDock.h"

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
		constexpr auto PreviewFadeDuration = 160ms;
		constexpr auto PreviewFadeInDuration = 180ms;
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
			std::mutex mutex;
			std::mutex presentationMutex;
			std::condition_variable condition;
			HWND window = nullptr;
			DWORD threadId = 0;
			RECT latestBounds{};
			RECT appliedBounds{};
			std::uint64_t latestRevision = 1;
			std::uint64_t appliedRevision = 1;
			bool ready = false;
			bool exited = false;
			bool creationSucceeded = false;
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
						{
							std::scoped_lock lock(state->mutex);
							initial = state->latestBounds;
						}
						HWND window = nullptr;
						if (atom || error == ERROR_CLASS_ALREADY_EXISTS)
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
						{
							std::scoped_lock lock(state->mutex);
							state->window = window;
							state->creationSucceeded = window != nullptr;
							state->ready = true;
						}
						state->condition.notify_all();
						if (window)
						{
							MSG message{};
							while (GetMessageW(&message, nullptr, 0, 0) > 0)
							{
								TranslateMessage(&message);
								DispatchMessageW(&message);
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
				return state_->creationSucceeded;
			}

			void Stop() noexcept
			{
				if (!thread_.joinable()) return;
				auto state = state_;
				HWND window = nullptr;
				DWORD threadId = 0;
				{
					std::scoped_lock lock(state->mutex);
					window = state->window;
					threadId = state->threadId;
				}
				if (window) PostMessageW(window, OwnerStopMessage, 0, 0);
				else if (threadId) PostThreadMessageW(threadId, WM_QUIT, 0, 0);
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
				UINT width, UINT height) noexcept
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
				update.dwFlags = ULW_ALPHA;
				return UpdateLayeredWindowIndirect(window, &update);
			}

			void Show() noexcept { Post(OwnerShowMessage); }
			void Hide() noexcept { Post(OwnerHideMessage); }
			void RevalidateTopmost() noexcept { Post(OwnerTopmostMessage); }
			void RequestStop() noexcept { Post(OwnerStopMessage); }

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
			UINT width = 0;
			UINT height = 0;
			ComPtr<ID2D1DeviceContext> context;
			ComPtr<ID2D1Bitmap1> target;
			ComPtr<ID2D1Bitmap1> mask;
			ComPtr<ID2D1GdiInteropRenderTarget> gdi;
			ComPtr<ID2D1SolidColorBrush> baseFill;
			ComPtr<ID2D1SolidColorBrush> frameStroke;
			ComPtr<ID2D1LinearGradientBrush> shimmer;
			ComPtr<ID2D1SolidColorBrush> progressTrack;
			ComPtr<ID2D1SolidColorBrush> progressFill;
			ComPtr<ID2D1SolidColorBrush> progressError;

			void Reset() noexcept
			{
				if (context) context->SetTarget(nullptr);
				progressError.Reset();
				progressFill.Reset();
				progressTrack.Reset();
				shimmer.Reset();
				frameStroke.Reset();
				baseFill.Reset();
				gdi.Reset();
				mask.Reset();
				target.Reset();
				context.Reset();
				generation = 0;
				width = 0;
				height = 0;
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
			PreviewOwner owner;
			RenderResources resources;
			std::chrono::steady_clock::time_point shimmerEpoch{};
			std::chrono::steady_clock::time_point shownTime{};
			std::chrono::steady_clock::time_point fadeStart{};
			std::chrono::steady_clock::time_point barFadeStart{};
			std::chrono::steady_clock::time_point firstHandoffFailure{};
			ProgressVisualReducer progressVisual;
			Phase phase = Phase::Preparing;
			std::uint8_t committedPreviewAlpha = 0;
			std::uint8_t fadeStartAlpha = 255;
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
				runtime.options.dpi, runtime.options.barZoom));
		}

		[[nodiscard]] HRESULT EnsureResources(Runtime& runtime,
			const FrameContext& frame, UINT width, UINT height)
		{
			if (!frame.epoch.d2dDevice || width == 0 || height == 0)
				return E_INVALIDARG;
			auto& resources = runtime.resources;
			if (resources.generation == frame.epoch.generation
				&& resources.width == width && resources.height == height
				&& resources.context && resources.target && resources.mask
				&& resources.gdi) return S_OK;

			RenderResources next;
			next.generation = frame.epoch.generation;
			next.width = width;
			next.height = height;
			HRESULT result = frame.epoch.d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &next.context);
			if (FAILED(result)) return result;
			const auto targetProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			result = next.context->CreateBitmap(D2D1::SizeU(width, height), nullptr,
				0, &targetProperties, &next.target);
			if (FAILED(result)) return result;
			const auto maskProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			result = next.context->CreateBitmap(D2D1::SizeU(width, height), nullptr,
				0, &maskProperties, &next.mask);
			if (FAILED(result)) return result;
			result = next.context.As(&next.gdi);
			if (FAILED(result)) return result;

			result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.f), &next.baseFill);
			if (FAILED(result)) return result;
			result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &next.frameStroke);
			if (FAILED(result)) return result;
			result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 1.f, 1.f, 0.24f), &next.progressTrack);
			if (FAILED(result)) return result;
			result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(0.38f, 0.80f, 1.f, 1.f), &next.progressFill);
			if (FAILED(result)) return result;
			result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 0.38f, 0.44f, 1.f), &next.progressError);
			if (FAILED(result)) return result;

			std::array<D2D1_GRADIENT_STOP, 7> stops{
				D2D1::GradientStop(0.00f, D2D1::ColorF(1.f, 1.f, 1.f, 0.00f)),
				D2D1::GradientStop(0.24f, D2D1::ColorF(1.f, 1.f, 1.f, 0.035f)),
				D2D1::GradientStop(0.40f, D2D1::ColorF(1.f, 1.f, 1.f, 0.09f)),
				D2D1::GradientStop(0.50f, D2D1::ColorF(1.f, 1.f, 1.f, 0.20f)),
				D2D1::GradientStop(0.60f, D2D1::ColorF(1.f, 1.f, 1.f, 0.075f)),
				D2D1::GradientStop(0.78f, D2D1::ColorF(1.f, 1.f, 1.f, 0.025f)),
				D2D1::GradientStop(1.00f, D2D1::ColorF(1.f, 1.f, 1.f, 0.00f)),
			};
			ComPtr<ID2D1GradientStopCollection> collection;
			result = next.context->CreateGradientStopCollection(stops.data(),
				static_cast<UINT32>(stops.size()), &collection);
			if (FAILED(result)) return result;
			const float support = static_cast<float>((std::max)(160u, width / 2));
			result = next.context->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(
					D2D1::Point2F(0.f, 0.f), D2D1::Point2F(support, 0.f)),
				collection.Get(), &next.shimmer);
			if (FAILED(result)) return result;

			// A8 mask 缓存实际圆角 alpha；窗口不再包含旧 blur padding。
			next.context->SetTarget(next.mask.Get());
			next.context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			next.context->BeginDraw();
			next.context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			const float scale = DpiScale(runtime);
			const float radius = static_cast<float>(StartupBarCornerRadiusDip) * scale;
			const float stroke = (std::max)(1.f, scale);
			const auto maskRect = D2D1::RoundedRect(
				D2D1::RectF(stroke * 0.5f, stroke * 0.5f,
					static_cast<float>(width) - stroke * 0.5f,
					static_cast<float>(height) - stroke * 0.5f), radius, radius);
			ComPtr<ID2D1SolidColorBrush> maskFill;
			ComPtr<ID2D1SolidColorBrush> maskStroke;
			result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 1.f, 1.f,
					static_cast<float>(StartupPreviewFillAlpha)), &maskFill);
			if (SUCCEEDED(result)) result = next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 1.f, 1.f,
					static_cast<float>(StartupPreviewFrameAlpha)), &maskStroke);
			if (SUCCEEDED(result))
			{
				next.context->FillRoundedRectangle(&maskRect, maskFill.Get());
				next.context->DrawRoundedRectangle(&maskRect, maskStroke.Get(), stroke);
			}
			const HRESULT maskEnd = next.context->EndDraw();
			if (FAILED(result)) return result;
			if (FAILED(maskEnd)) return maskEnd;
			next.context->SetTarget(next.target.Get());
			next.context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			resources.Reset();
			resources = std::move(next);
			return S_OK;
		}

		void DrawShimmer(Runtime& runtime, const FrameContext& frame,
			float contentOpacity)
		{
			auto& resources = runtime.resources;
			if (!resources.mask || !resources.shimmer || contentOpacity <= 0.f) return;
			const double width = static_cast<double>(resources.width);
			const double height = static_cast<double>(resources.height);
			const double support = static_cast<double>((std::max)(160u,
				resources.width / 2));
			const GeometryRect maskBounds{ 0.0, 0.0, width, height };
			const ShimmerGradient gradient{ 0.0, 0.0, support, 0.0, 0.0, 1.0 };
			const auto travel = ResolveShimmerHorizontalTravel(
				maskBounds, gradient, (std::max)(8.0, support * 0.10));
			const double cycle = runtime.shimmerEpoch
				== std::chrono::steady_clock::time_point{} ? 0.0
				: ResolveShimmerCycleRatio(frame.frameTime,
					runtime.shimmerEpoch, std::chrono::duration<double>(1.75));
			const double translation = ResolveShimmerTranslationX(
				travel, EaseShimmerPhase(cycle));
			resources.shimmer->SetStartPoint(D2D1::Point2F(
				static_cast<float>(gradient.startX + translation), 0.f));
			resources.shimmer->SetEndPoint(D2D1::Point2F(
				static_cast<float>(gradient.endX + translation), 0.f));
			resources.shimmer->SetOpacity(contentOpacity);
			const auto previous = resources.context->GetAntialiasMode();
			resources.context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			const auto destination = D2D1::RectF(0.f, 0.f,
				static_cast<float>(width), static_cast<float>(height));
			const auto source = destination;
			resources.context->FillOpacityMask(resources.mask.Get(),
				resources.shimmer.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
				&destination, &source);
			resources.context->SetAntialiasMode(previous);
		}

		void DrawProgress(Runtime& runtime,
			const Inkeys::Startup::Snapshot& snapshot,
			std::chrono::steady_clock::time_point now, bool success,
			float contentOpacity)
		{
			const bool failed = snapshot.failed || runtime.phase == Phase::Failure;
			const auto visual = runtime.progressVisual.Update(
				now, snapshot.actualRatio, success, failed);
			if (contentOpacity <= 0.f || visual.opacity <= 0.001) return;
			const float scale = DpiScale(runtime);
			const float width = static_cast<float>(runtime.resources.width);
			const float height = static_cast<float>(runtime.resources.height);
			// 窄窗口保留两侧各 12 DIP，未来仅显示主按钮时进度条仍可辨认。
			const float progressWidth = (std::min)(192.f * scale,
				(std::max)(0.f, width - 24.f * scale));
			if (progressWidth <= 0.f) return;
			const float centerX = width * 0.5f;
			const float centerY = height * 0.5f;
			const float trackHeight = (std::max)(1.f, scale);
			const float indicatorHeight = (std::max)(3.f, 3.f * scale);
			const auto track = D2D1::RoundedRect(
				D2D1::RectF(centerX - progressWidth / 2.f,
					centerY - trackHeight / 2.f,
					centerX + progressWidth / 2.f,
					centerY + trackHeight / 2.f),
				trackHeight / 2.f, trackHeight / 2.f);
			const float opacity = Clamp01(visual.opacity * contentOpacity);
			runtime.resources.progressTrack->SetOpacity(opacity);
			runtime.resources.context->FillRoundedRectangle(&track,
				runtime.resources.progressTrack.Get());
			const float fillWidth = progressWidth
				* static_cast<float>(visual.displayedRatio);
			if (fillWidth <= 0.f) return;
			const auto fill = D2D1::RoundedRect(
				D2D1::RectF(track.rect.left, centerY - indicatorHeight / 2.f,
					track.rect.left + fillWidth, centerY + indicatorHeight / 2.f),
				indicatorHeight / 2.f, indicatorHeight / 2.f);
			auto* brush = visual.red ? runtime.resources.progressError.Get()
				: runtime.resources.progressFill.Get();
			brush->SetOpacity(opacity);
			runtime.resources.context->FillRoundedRectangle(&fill, brush);
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

		void RecoverOpaqueBar(const std::shared_ptr<Runtime>& runtime) noexcept
		{
			if (runtime->options.requestBarAlpha)
				runtime->options.requestBarAlpha(255);
			if (runtime->committedBarAlpha.load(std::memory_order_acquire) == 255)
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
			if (success && barAlpha == 0 && runtime->phase == Phase::WaitingForBar)
			{
				runtime->phase = Phase::FadingOut;
				runtime->fadeStart = frame.frameTime;
				runtime->fadeStartAlpha = runtime->committedPreviewAlpha;
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
			else if (runtime->shownTime != std::chrono::steady_clock::time_point{})
				windowAlpha = ResolveFadeInAlpha(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						frame.frameTime - runtime->shownTime), PreviewFadeInDuration);
			if (runtime->phase == Phase::FadingOut
				|| runtime->phase == Phase::ExitFadingOut
				|| runtime->phase == Phase::WaitingForOpaqueBar)
			{
				windowAlpha = ResolveFadeOutAlphaFrom(runtime->fadeStartAlpha,
					std::chrono::duration_cast<std::chrono::milliseconds>(
						frame.frameTime - runtime->fadeStart), PreviewFadeDuration);
			}
			if (runtime->phase == Phase::Failure) windowAlpha = 255;

			auto& resources = runtime->resources;
			resources.context->SetTarget(resources.target.Get());
			resources.context->BeginDraw();
			resources.context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			const float radius = static_cast<float>(StartupBarCornerRadiusDip)
				* DpiScale(*runtime);
			const float stroke = (std::max)(1.f, DpiScale(*runtime));
			const auto rounded = D2D1::RoundedRect(
				D2D1::RectF(stroke * 0.5f, stroke * 0.5f,
					static_cast<float>(width) - stroke * 0.5f,
					static_cast<float>(height) - stroke * 0.5f),
				radius, radius);
			// 整窗淡入淡出只由 ULW 的 SourceConstantAlpha 执行；brush 保持 per-pixel alpha。
			resources.baseFill->SetOpacity(static_cast<float>(StartupPreviewFillAlpha));
			resources.frameStroke->SetOpacity(static_cast<float>(StartupPreviewFrameAlpha));
			resources.context->FillRoundedRectangle(&rounded, resources.baseFill.Get());
			resources.context->DrawRoundedRectangle(&rounded,
				resources.frameStroke.Get(), stroke);
			DrawShimmer(*runtime, frame, 1.f);
			DrawProgress(*runtime, snapshot, frame.frameTime, success, 1.f);

			const auto presentOwner = runtime->owner.Snapshot();
			if (!presentOwner.window || !SameBounds(presentOwner.bounds, owner.bounds))
			{
				const HRESULT end = resources.context->EndDraw();
				if (end == D2DERR_RECREATE_TARGET) resources.Reset();
				return IsSharedDeviceLoss(end)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			HDC source = nullptr;
			HRESULT getDc = resources.gdi->GetDC(
				D2D1_DC_INITIALIZE_MODE_COPY, &source);
			BOOL update = FALSE;
			HRESULT release = E_FAIL;
			if (SUCCEEDED(getDc) && source)
			{
				// per-pixel alpha 保留在 D2D surface，整窗淡入淡出只走 SourceConstantAlpha。
				update = runtime->owner.PresentPixels(source,
					windowAlpha, width, height);
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

			if (!runtime->firstFrameCommitted.exchange(true,
				std::memory_order_acq_rel))
			{
				// 首帧必须以 SourceConstantAlpha=0 提交，然后无激活显示并从透明开始。
				runtime->presentationAvailable.store(true, std::memory_order_release);
				runtime->shownTime = frame.frameTime;
				runtime->shimmerEpoch = frame.frameTime;
				runtime->progressVisual.MarkPreviewShown(frame.frameTime);
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
						frame.frameTime - runtime->barFadeStart), PreviewFadeDuration);
				if (runtime->options.requestBarAlpha)
					runtime->options.requestBarAlpha(requestedAlpha);
			}
			if (runtime->phase == Phase::WaitingForOpaqueBar && barAlpha == 255)
			{
				runtime->barAlpha255Committed.store(true,
					std::memory_order_release);
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
		UINT dpi, double zoom) noexcept
	{
		const LONG width = RoundStartupPreviewDipToPixels(widthDip, dpi, zoom);
		const LONG height = RoundStartupPreviewDipToPixels(heightDip, dpi, zoom);
		const bool monitorValid = Width(monitorBounds) > 0
			&& Height(monitorBounds) > 0;
		const RECT resolvedMonitor = monitorValid ? monitorBounds : workArea;
		const LONG left = resolvedMonitor.left
			+ (Width(resolvedMonitor) - width) / 2;
		const double dpiScale = static_cast<double>(dpi ? dpi
			: USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI;
		const LONG dockLine = static_cast<LONG>(std::lround(
			Inkeys::UI::Bar::ResolveBarBottomDockLine(
				resolvedMonitor, workArea, 0.0, dpiScale)));
		// 与正式 Bar 一致：按显示器居中，只有底部任务栏会抬高 dock line。
		const LONG top = dockLine - height;
		return { left, top, left + width, top + height };
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
					runtime->options.dpi, runtime->options.barZoom);
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
		runtime->resources.Reset();
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
		if (!std::isfinite(mainButtonTargetWidthDip)
			|| mainButtonTargetWidthDip < CachedStartupBarWidthMinimumDip
			|| mainButtonTargetWidthDip > CachedStartupBarWidthMaximumDip
			|| (expanded && (!std::isfinite(layoutTotalWidthDip)
				|| layoutTotalWidthDip <= 0.0
				|| layoutTotalWidthDip > CachedStartupBarWidthMaximumDip
					- mainButtonTargetWidthDip
					- StartupMainButtonToMainBarGapDip))) return;
		const double widthDip = CalculateStartupBarTotalWidthDip(
			mainButtonTargetWidthDip, layoutTotalWidthDip, expanded);
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
			runtime->options.cachedWidthDip,
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
