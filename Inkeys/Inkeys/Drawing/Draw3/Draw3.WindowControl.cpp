module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <directmanipulation.h>
#include <dbt.h>
#include "Draw3.RtsStartup.h"
#include <wrl.h>
#include <objbase.h>
#include <tpcshrd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <mutex>
#include <utility>
#include <tchar.h> // Tablet Pen Service 属性宏仍使用 _T。

#include "Draw3.SpeedEraser.h"
#include "Draw3.HapticRequest.h"
#include "Draw3.HapticDiagnostics.h"
#include "Draw3.MouseInput.h"
#include "Draw3.TouchpadNavigation.h"
#include "Draw3.TouchpadInput.h"
#include "../../Business/PenWidthPolicy.hpp"

module Inkeys.Drawing.Draw3.window_control;

import Inkeys.Drawing.Draw3.diagnostics;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr UINT kApplySystemCursorMessage = WM_APP + 1;
		using Detail::kTouchpadNavigationMessage;
		constexpr UINT kStartTouchpadNavigationMessage = WM_APP + 0x3D6;
		std::atomic<UINT_PTR> nextTouchpadNavigationIdentity = 0;
		constexpr LONG_PTR kPromotedPointerSignatureMask = 0xFFFFFF00;
		constexpr LONG_PTR kPromotedPointerSignature = 0xFF515700;
		constexpr uint64_t kTouchInputBarrierKnownMask = uint64_t{ 1 } << 32u;

		// 发布停止证明前取得独立通知句柄；Host 的成功快路可以立即关闭原句柄。
		class TouchpadOwnerNotification
		{
		public:
			explicit TouchpadOwnerNotification(HANDLE source) noexcept
			{
				if (source) (void)DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
					&event_, 0, FALSE, DUPLICATE_SAME_ACCESS);
			}
			~TouchpadOwnerNotification() { if (event_) CloseHandle(event_); }
			TouchpadOwnerNotification(const TouchpadOwnerNotification&) = delete;
			TouchpadOwnerNotification& operator=(const TouchpadOwnerNotification&) = delete;
			explicit operator bool() const noexcept { return event_ != nullptr; }
			bool Signal() const noexcept { return event_ && SetEvent(event_) != FALSE; }
		private:
			HANDLE event_ = nullptr;
		};

		constexpr DWORD kTabletInputFlags =
			TABLET_ENABLE_MULTITOUCHDATA |
			TABLET_DISABLE_PRESSANDHOLD |
			TABLET_DISABLE_PENTAPFEEDBACK |
			TABLET_DISABLE_PENBARRELFEEDBACK |
			// 在首个接触前固定关闭系统 Touch UI；不影响 RTS 逐触点橡皮。
			TABLET_DISABLE_TOUCHUIFORCEOFF |
			TABLET_DISABLE_FLICKS;

		struct SystemCursorSnapshot
		{
			CURSORINFO info = { sizeof(CURSORINFO) };
			bool succeeded = false;
			DWORD error = ERROR_SUCCESS;
		};

		SystemCursorSnapshot ReadSystemCursorSnapshot() noexcept
		{
			const DWORD previousError = GetLastError();
			SystemCursorSnapshot snapshot;
			SetLastError(ERROR_SUCCESS);
			snapshot.succeeded = GetCursorInfo(&snapshot.info) != FALSE;
			if (!snapshot.succeeded) snapshot.error = GetLastError();
			SetLastError(previousError);
			return snapshot;
		}

		void RecordSystemCursorSnapshot(const char* trigger, HWND target, HWND hit,
			bool applied, const SystemCursorSnapshot& before,
			const SystemCursorSnapshot& after) noexcept
		{
			const DWORD previousError = GetLastError();
			const DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
			const DWORD hitThread = hit ? GetWindowThreadProcessId(hit, nullptr) : 0;
			// GetCursorInfo 是瞬时全局观测；Win7 不要求出现 Win8 才有的 SUPPRESSED 位。
			RecordCursorDiagnostic("system-observe trigger=%s applied=%u target=%p hit=%p targetThread=%lu hitThread=%lu before={ok=%u,error=%lu,flags=0x%lx,handle=%p} after={ok=%u,error=%lu,flags=0x%lx,handle=%p}",
				trigger ? trigger : "unknown", applied ? 1u : 0u,
				static_cast<void*>(target), static_cast<void*>(hit), targetThread, hitThread,
				before.succeeded ? 1u : 0u, before.error, before.info.flags,
				static_cast<void*>(before.info.hCursor), after.succeeded ? 1u : 0u,
				after.error, after.info.flags, static_cast<void*>(after.info.hCursor));
			SetLastError(previousError);
		}

		using GetPointerPenInfoFunction = BOOL(WINAPI*)(
			UINT32 pointerId, POINTER_PEN_INFO* penInfo);
		using GetPointerInfoFunction = BOOL(WINAPI*)(
			UINT32 pointerId, POINTER_INFO* pointerInfo);
		using GetPointerTypeFunction = BOOL(WINAPI*)(
			UINT32 pointerId, POINTER_INPUT_TYPE* pointerType);
		using GetCurrentInputMessageSourceFunction = BOOL(WINAPI*)(
			INPUT_MESSAGE_SOURCE* inputMessageSource);

		struct InputMessageSourceSnapshot
		{
			INPUT_MESSAGE_DEVICE_TYPE deviceType = IMDT_UNAVAILABLE;
			INPUT_MESSAGE_ORIGIN_ID originId = IMO_UNAVAILABLE;
			bool apiAvailable = false;
			bool succeeded = false;
			DWORD error = ERROR_SUCCESS;
		};

		InputMessageSourceSnapshot CurrentInputMessageSource() noexcept
		{
			const DWORD previousError = GetLastError();
			// Win7 不导出该 API；缺失时沿用 promoted 签名及 Touch barrier。
			static const GetCurrentInputMessageSourceFunction getSource = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetCurrentInputMessageSourceFunction>(
						GetProcAddress(user32, "GetCurrentInputMessageSource")) : nullptr;
				}();
			INPUT_MESSAGE_SOURCE source = {};
			InputMessageSourceSnapshot snapshot;
			snapshot.apiAvailable = getSource != nullptr;
			if (getSource)
			{
				SetLastError(ERROR_SUCCESS);
				snapshot.succeeded = getSource(&source) != FALSE;
				if (snapshot.succeeded)
				{
					snapshot.deviceType = source.deviceType;
					snapshot.originId = source.originId;
				}
				else snapshot.error = GetLastError();
			}
			// 诊断保留 API 原始结果，但不改变原来的来源回退或调用方 LastError。
			SetLastError(previousError);
			return snapshot;
		}

		void RecordMouseMessageSource(uint64_t eventId, HWND window, UINT message,
			WPARAM wParam, LPARAM lParam, const InputMessageSourceSnapshot& source) noexcept
		{
			if (!eventId) return;
			const DWORD previousError = GetLastError();
			const DWORD sent = InSendMessageEx(nullptr);
			RecordCursorDiagnostic("mouse-source event=%llu hwnd=%p message=0x%04x api=%u ok=%u error=%lu device=%u origin=%u sent=0x%lx wparam=0x%llx lparam=0x%llx",
				static_cast<unsigned long long>(eventId), static_cast<void*>(window), message,
				source.apiAvailable ? 1u : 0u, source.succeeded ? 1u : 0u,
				static_cast<unsigned long>(source.error), static_cast<unsigned>(source.deviceType),
				static_cast<unsigned>(source.originId), static_cast<unsigned long>(sent),
				static_cast<unsigned long long>(wParam), static_cast<unsigned long long>(lParam));
			SetLastError(previousError);
		}

		GetPointerTypeFunction ResolveGetPointerType() noexcept
		{
			static const GetPointerTypeFunction getPointerType = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetPointerTypeFunction>(
						GetProcAddress(user32, "GetPointerType")) : nullptr;
				}();
			return getPointerType;
		}

		GetPointerPenInfoFunction ResolveGetPointerPenInfo() noexcept
		{
			static const auto getPen = reinterpret_cast<GetPointerPenInfoFunction>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerPenInfo"));
			return getPen;
		}

		struct PointerDrawingApi
		{
			decltype(&GetPointerTouchInfo) touch;
			decltype(&GetPointerDevice) device;
			decltype(&GetPointerDeviceRects) rects;
			decltype(&RegisterPointerDeviceNotifications) notifications;
		};

		const PointerDrawingApi& DrawingPointerApi() noexcept
		{
			static const auto api = [] {
				const HMODULE user32 = GetModuleHandleW(L"user32.dll");
				return PointerDrawingApi{
					reinterpret_cast<decltype(&GetPointerTouchInfo)>(GetProcAddress(user32, "GetPointerTouchInfo")),
					reinterpret_cast<decltype(&GetPointerDevice)>(GetProcAddress(user32, "GetPointerDevice")),
					reinterpret_cast<decltype(&GetPointerDeviceRects)>(GetProcAddress(user32, "GetPointerDeviceRects")),
					reinterpret_cast<decltype(&RegisterPointerDeviceNotifications)>(GetProcAddress(user32, "RegisterPointerDeviceNotifications"))
				};
			}();
			return api;
		}

		struct PointerDetails
		{
			bool typeKnown = false;
			POINTER_INPUT_TYPE type = PT_POINTER;
			bool penInfoKnown = false;
			uint32_t cursorId = 0;
			bool positionKnown = false;
			bool primaryKnown = false;
			bool primary = false;
			POINT screenPosition = {};
			bool eraserHint = false;
			bool inContact = false;
			bool cancelled = false;
		};

		PointerDetails QueryPointerDetails(uint32_t pointerId, POINTER_PEN_INFO* drawingPen = nullptr) noexcept
		{
			PointerDetails details;
			// Win7 没有 Pointer Pen API，必须动态解析，缺失时继续使用当前工具。
			const auto getPointerPenInfo = ResolveGetPointerPenInfo();
			const GetPointerTypeFunction getPointerType = ResolveGetPointerType();
			if (!getPointerType || !getPointerType(pointerId, &details.type)) return details;
			details.typeKnown = true;
			static const GetPointerInfoFunction getPointerInfo = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetPointerInfoFunction>(
						GetProcAddress(user32, "GetPointerInfo")) : nullptr;
				}();
			if (getPointerInfo)
			{
				POINTER_INFO pointerInfo = {};
				if (getPointerInfo(pointerId, &pointerInfo) &&
					pointerInfo.pointerType == details.type)
				{
					details.positionKnown = true;
					details.screenPosition = pointerInfo.ptPixelLocation;
					details.primaryKnown = true;
					details.primary = (pointerInfo.pointerFlags & POINTER_FLAG_PRIMARY) != 0;
					details.cancelled = (pointerInfo.pointerFlags & POINTER_FLAG_CANCELED) != 0;
				}
			}
			if (details.type != PT_PEN || !getPointerPenInfo) return details;

			POINTER_PEN_INFO penInfo = {};
			if (!getPointerPenInfo(pointerId, &penInfo) ||
				penInfo.pointerInfo.pointerType != PT_PEN) return details;
			details.cancelled = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_CANCELED) != 0;
			details.penInfoKnown = true;
			if (drawingPen) *drawingPen = penInfo;
			// 只被动读取 tablet cursor；旧系统缺少 API 时仅省略触觉，不改输入或光标。
			using GetPointerCursorIdFunction = BOOL(WINAPI*)(UINT32, UINT32*);
			static const auto getPointerCursorId = []() noexcept
			{
				const HMODULE user32 = GetModuleHandleW(L"user32.dll");
				return user32 ? reinterpret_cast<GetPointerCursorIdFunction>(GetProcAddress(user32, "GetPointerCursorId")) : nullptr;
			}();
			UINT32 cursorId = 0;
			if (getPointerCursorId && getPointerCursorId(pointerId, &cursorId)) details.cursorId = cursorId;
			details.positionKnown = true;
			details.screenPosition = penInfo.pointerInfo.ptPixelLocation;
			details.eraserHint =
				(penInfo.penFlags & (PEN_FLAG_INVERTED | PEN_FLAG_ERASER)) != 0;
			details.inContact =
				(penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
			return details;
		}

		bool IsPromotedPointerMouseMessage(ULONG_PTR extraInfo) noexcept
		{
			return (extraInfo & kPromotedPointerSignatureMask) ==
				kPromotedPointerSignature;
		}

		DrawingCursorPointerAuthority AuthorityForPointerType(
			POINTER_INPUT_TYPE pointerType) noexcept
		{
			switch (pointerType)
			{
			case PT_PEN: return DrawingCursorPointerAuthority::Pen;
			case PT_MOUSE: return DrawingCursorPointerAuthority::Mouse;
			case PT_TOUCH: return DrawingCursorPointerAuthority::Touch;
			default: return DrawingCursorPointerAuthority::Unknown;
			}
		}

#if defined(DRAW3_RTS_DIAGNOSTICS)
		const char* MouseMessageName(UINT message) noexcept
		{
			switch (message)
			{
			case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
			case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
			case WM_LBUTTONUP: return "WM_LBUTTONUP";
			case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
			case WM_RBUTTONUP: return "WM_RBUTTONUP";
			case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
			case WM_MBUTTONUP: return "WM_MBUTTONUP";
			case WM_MOUSELEAVE: return "WM_MOUSELEAVE";
			case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
			default: return "WM_MOUSE_UNKNOWN";
			}
		}

		const char* PointerTypeName(POINTER_INPUT_TYPE pointerType,
			bool typeKnown) noexcept
		{
			if (!typeKnown) return "unknown";
			switch (pointerType)
			{
			case PT_PEN: return "pen";
			case PT_MOUSE: return "mouse";
			case PT_TOUCH: return "touch";
			case PT_TOUCHPAD: return "touchpad";
			default: return "pointer";
			}
		}

		const char* SystemCursorDecisionReason(
			DrawingCursorPointerAuthority authority, DrawingTool tool,
			bool penSampleValid, bool mouseSampleValid,
			bool mouseUsesSystemCursor, bool touchPanActive,
			bool realMouseTakeover, bool hide) noexcept
		{
			if (!hide) return "default-arrow";
			if (authority == DrawingCursorPointerAuthority::Touch) return "touch-input";
			if (touchPanActive && !realMouseTakeover) return "touch-pan";
			if (authority == DrawingCursorPointerAuthority::Pen) return "pen-authority";
			if (tool == DrawingTool::Eraser) return "eraser-tool";
			if (tool == DrawingTool::Laser) return "laser-tool";
			if (!mouseUsesSystemCursor && mouseSampleValid) return "mouse-app-cursor";
			if (penSampleValid) return "pen-sample-fallback";
			return "hide-policy";
		}
#endif

	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void WindowController::SetRtsTraceEnabled(bool enabled) noexcept
	{
		ConfigureRtsTrace(enabled);
		ConfigureDrawingCursorTrace(enabled);
		{
			std::lock_guard<std::mutex> lock(cursorTraceMutex_);
			lastCursorTraceStateKnown_ = false;
			lastSystemCursorDecisionKnown_ = false;
		}
		cursorTraceEnabled_.store(enabled, std::memory_order_release);
	}
#endif


	// DM 对象和回调均留在 HWND owner；绘制线程只持纯数值 Bridge。
	class TouchpadNavigationOwner : public std::enable_shared_from_this<TouchpadNavigationOwner>
	{
		class Handler final : public Microsoft::WRL::RuntimeClass<
			Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDirectManipulationViewportEventHandler,
			IDirectManipulationInteractionEventHandler>
		{
		public:
			explicit Handler(std::weak_ptr<TouchpadNavigationOwner> owner) : owner_(std::move(owner)) {}
			HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(IDirectManipulationViewport*,
				DIRECTMANIPULATION_STATUS current, DIRECTMANIPULATION_STATUS previous) override
			{
				Microsoft::WRL::ComPtr<Handler> keepAlive(this);
				if (auto owner = owner_.lock()) owner->Status(current, previous);
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE OnViewportUpdated(IDirectManipulationViewport*) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnContentUpdated(IDirectManipulationViewport*, IDirectManipulationContent* content) override
			{
				Microsoft::WRL::ComPtr<Handler> keepAlive(this);
				if (auto owner = owner_.lock()) owner->Content(content);
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE OnInteraction(IDirectManipulationViewport2*,
				DIRECTMANIPULATION_INTERACTION_TYPE interaction) override
			{
				Microsoft::WRL::ComPtr<Handler> keepAlive(this);
				if (auto owner = owner_.lock()) owner->Interaction(interaction);
				return S_OK;
			}
		private:
			std::weak_ptr<TouchpadNavigationOwner> owner_;
		};
	public:
		explicit TouchpadNavigationOwner(std::shared_ptr<Detail::TouchpadNavigationBridge> bridge, ContactInputCoordinator* input)
			: bridge_(std::move(bridge)), frontend_(*bridge_), input_(input) {}
		bool Initialize(HWND window, bool pointerReady)
		{
			// 同一 HWND 的输入委托只能交给已就绪的 Pointer 后端。
			if (!pointerReady)
			{
				LogCanvasPan("touchpad-init action=quarantined reason=rts-dm-hwnd-conflict");
				return false;
			}
			window_ = window;
			const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return false;
			uninitialize_ = SUCCEEDED(apartment);
			HRESULT result = CoCreateInstance(__uuidof(DirectManipulationManager), nullptr,
				CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager_));
			if (SUCCEEDED(result)) result = manager_->GetUpdateManager(IID_PPV_ARGS(&updates_));
			if (SUCCEEDED(result)) result = manager_->CreateViewport(nullptr, window, IID_PPV_ARGS(&viewport_));
			if (SUCCEEDED(result)) result = Detail::ConfigureTouchpadViewport(*viewport_.Get());
			RECT rect{};
			if (SUCCEEDED(result) && (!GetClientRect(window, &rect) || rect.right <= 0 || rect.bottom <= 0)) result = E_FAIL;
			if (SUCCEEDED(result)) { rect_ = rect; result = viewport_->SetViewportRect(&rect_); }
			if (SUCCEEDED(result)) result = viewport_->GetPrimaryContent(IID_PPV_ARGS(&content_));
			if (SUCCEEDED(result))
			{
				handler_ = Microsoft::WRL::Make<Handler>(weak_from_this());
				result = handler_ ? viewport_->AddEventHandler(window, handler_.Get(), &cookie_) : E_OUTOFMEMORY;
				retirement_.handlerRegistered = SUCCEEDED(result);
			}
			if (SUCCEEDED(result)) result = manager_->Activate(window);
			if (SUCCEEDED(result)) { retirement_.managerActivated = true; result = viewport_->Enable(); }
			if (SUCCEEDED(result))
			{
				viewportEnabled_ = bridge_->enabled.load();
				if (!viewportEnabled_) result = viewport_->Disable();
			}
			if (FAILED(result))
			{
				LogCanvasPan("touchpad-init result=0x%08lx action=unavailable", static_cast<unsigned long>(result));
				(void)Shutdown(); // 若清理也失败，由调用方保留 holder，等待 Host 的 owner 停止证明。
				return false;
			}
			frontend_.Reset(bridge_->Epoch());
			initialized_ = true;
			LogCanvasPan("touchpad-init input=explicit-pass-through update=manual interaction-events=1");
			return true;
		}
		bool NeedsCleanup() const noexcept
		{ return uninitialize_ || manager_ || viewport_ || handler_; }
		void Apply()
		{
			if (!initialized_ || !viewport_ || failed_) return;
			const auto epoch = bridge_->Epoch();
			if (bridge_->Active() && !bridge_->Running() && !bridge_->inertiaAllowed.load())
			{ EndInertia(); return; }
			if (bridge_->NeedsUpdate() && !Admitted()) { Cancel(); return; }
			const bool enabled = bridge_->enabled.load(std::memory_order_acquire);
			// 重新进入拖动时必须主动 Enable；已禁用的 viewport 不能依靠下一次命中来启动。
			if (frontend_.Epoch() != epoch || enabled != viewportEnabled_)
			{
				frontend_.Reset(epoch);
				viewport_->Stop(); viewport_->Disable(); viewportEnabled_ = false;
				RECT rect{};
				if (GetClientRect(window_, &rect) && rect.right > 0 && rect.bottom > 0)
				{
					rect_ = rect;
					if (FAILED(viewport_->SetViewportRect(&rect_))) { failed_ = true; Cancel(); return; }
				}
				if (enabled)
				{
					if (FAILED(viewport_->Enable())) { failed_ = true; Cancel(); return; }
					viewportEnabled_ = true;
					if (!Reset()) Cancel();
				}
			}
		}
		bool Contact(UINT32 pointer)
		{
			Apply();
			if (!initialized_ || !viewport_ || failed_ || !bridge_->enabled.load(std::memory_order_acquire) || !input_ || input_->AdmissionBlocked()) return false;
			// 固定配置仅在初始化设置；SetContact 的 S_OK 只证明关联，不能伪造 Running。
			if (FAILED(viewport_->Enable())) { failed_ = true; Cancel(); return false; }
			HRESULT associated = E_FAIL;
			const bool accepted = frontend_.Associate(bridge_->Epoch(), input_->AdmissionRevision(), pointer,
				[&](UINT32 id) { associated = viewport_->SetContact(id); return SUCCEEDED(associated); });
			LogCanvasPan("touchpad-contact pointer=%u result=0x%08lx accepted=%u running=%u needs-update=%u",
				pointer, static_cast<unsigned long>(associated), accepted ? 1u : 0u,
				bridge_->Running() ? 1u : 0u, bridge_->NeedsUpdate() ? 1u : 0u);
			if (!accepted)
			{
				if (!frontend_.Accepting()) StopViewport();
				return false;
			}
			Update(); return true;
		}
		void Update()
		{
			Apply();
			if (initialized_ && updates_ && !failed_ && bridge_->enabled.load(std::memory_order_acquire))
			{
				const HRESULT result = updates_->Update(nullptr);
				if (FAILED(result)) Cancel();
			}
		}
		bool Shutdown()
		{
			frontend_.Reset(bridge_->Epoch());
			if (viewport_ && !retirement_.viewportAbandoned) { viewport_->Stop(); viewport_->Disable(); }
			if (!retirement_.Complete(
				[this]() { return !viewport_ || SUCCEEDED(viewport_->RemoveEventHandler(cookie_)); },
				[this]() { return !viewport_ || SUCCEEDED(viewport_->Abandon()); },
				[this]() { return !manager_ || SUCCEEDED(manager_->Deactivate(window_)); }))
				return false; // 不释放 holder/COM apartment，不把“已请求停止”冒充停止证明。
			initialized_ = false;
			ReleaseResources();
			return true;
		}
	private:
		void ReleaseResources() noexcept
		{
			handler_.Reset(); content_.Reset(); viewport_.Reset(); updates_.Reset(); manager_.Reset();
			if (uninitialize_) { CoUninitialize(); uninitialize_ = false; }
		}
		bool Admitted() const noexcept
		{ return input_ && !input_->AdmissionBlocked() && input_->AdmissionRevision() == frontend_.AdmissionRevision(); }
		bool Reset()
		{
			resetting_ = true;
			const HRESULT result = viewport_ ? viewport_->ZoomToRect(0.0f, 0.0f,
				static_cast<float>(rect_.right), static_cast<float>(rect_.bottom), FALSE) : E_FAIL;
			resetting_ = false;
			if (FAILED(result)) { failed_ = true; return false; }
			x_ = y_ = 0.0;
			return true;
		}
		void StopViewport()
		{
			if (!viewport_) return;
			viewport_->Stop();
			if (failed_ || !bridge_->enabled.load() || !Reset())
			{
				viewport_->Disable();
				viewportEnabled_ = false;
			}
		}
		void Cancel()
		{
			frontend_.Cancel();
			StopViewport();
		}
		void EndInertia()
		{
			// 不提升 epoch/清空队列：禁惯性不能吞掉尚未消费的 Running 末点。
			Publish(TouchpadNavigationPhase::Cancelled);
			StopViewport();
		}
		void Publish(TouchpadNavigationPhase phase)
		{
			if (!frontend_.Accepting() || resetting_) return;
			if (!Admitted()) { Cancel(); return; }
			if (phase == TouchpadNavigationPhase::Inertia && !bridge_->inertiaAllowed.load()) { EndInertia(); return; }
			LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
			if (!frontend_.PublishStatus(phase, x_, y_, qpc.QuadPart))
				StopViewport();
		}
		void Interaction(DIRECTMANIPULATION_INTERACTION_TYPE interaction)
		{
			if (!frontend_.Accepting() || resetting_) return;
			if (!Admitted()) { Cancel(); return; }
			if (interaction != DIRECTMANIPULATION_INTERACTION_BEGIN && interaction != DIRECTMANIPULATION_INTERACTION_END) return;
			frontend_.Interaction(interaction == DIRECTMANIPULATION_INTERACTION_BEGIN);
			LogCanvasPan("touchpad-interaction type=%s accepted=%u running=%u needs-update=%u",
				interaction == DIRECTMANIPULATION_INTERACTION_BEGIN ? "begin" : "end",
				frontend_.Accepting() ? 1u : 0u, bridge_->Running() ? 1u : 0u, bridge_->NeedsUpdate() ? 1u : 0u);
		}
		void Status(DIRECTMANIPULATION_STATUS current, DIRECTMANIPULATION_STATUS previous)
		{
			if (!frontend_.Accepting() || resetting_ || (current == previous && current != DIRECTMANIPULATION_READY)) return;
			// Enable/初始化 READY 可能在关联后异步到达，不能提前结束尚待识别的真实 interaction。
			if ((current == DIRECTMANIPULATION_READY && !frontend_.CanPublishStatus(TouchpadNavigationPhase::Ready)) ||
				(current == DIRECTMANIPULATION_INERTIA && !frontend_.CanPublishStatus(TouchpadNavigationPhase::Inertia))) return;
			LogCanvasPan("touchpad-status previous=%u current=%u accepted=1", static_cast<unsigned>(previous), static_cast<unsigned>(current));
			if (current == DIRECTMANIPULATION_RUNNING) Publish(TouchpadNavigationPhase::Running);
			else if (current == DIRECTMANIPULATION_INERTIA)
			{
				// 阶段边沿可先于合并的 content 通知；按 Running 结清最后累计变换再决定尾滑。
				Content(content_.Get());
				if (!frontend_.Accepting()) return;
				if (!bridge_->inertiaAllowed.load(std::memory_order_acquire))
				{
					Publish(TouchpadNavigationPhase::Ready);
					StopViewport(); return;
				}
				Publish(TouchpadNavigationPhase::Inertia);
			}
			else if (current == DIRECTMANIPULATION_READY)
			{
				Content(content_.Get());
				if (!frontend_.Accepting()) return;
				Publish(TouchpadNavigationPhase::Ready);
				if (!Reset()) Cancel();
			}
		}
		void Content(IDirectManipulationContent* content)
		{
			if (!frontend_.Accepting() || resetting_ || !content) return;
			float transform[6]{};
			if (FAILED(content->GetContentTransform(transform, 6))) { Cancel(); return; }
			for (float value : transform) if (!std::isfinite(value)) { Cancel(); return; }
			// 本模式只有平移；拒绝意外缩放/旋转，不从无效矩阵制造画布跳变。
			if (std::abs(transform[0] - 1.0f) > 0.0001f || std::abs(transform[3] - 1.0f) > 0.0001f ||
				std::abs(transform[1]) > 0.0001f || std::abs(transform[2]) > 0.0001f) { Cancel(); return; }
			if (x_ == transform[4] && y_ == transform[5]) return;
			x_ = transform[4]; y_ = transform[5];
			// 内容可先于状态到达；先缓存累计值，确认 Running/Inertia 后才发布几何和持有。
			if (frontend_.HasConfirmedPhase()) Publish(frontend_.Phase());
		}
		std::shared_ptr<Detail::TouchpadNavigationBridge> bridge_;
		Detail::TouchpadNavigationFrontend frontend_;
		ContactInputCoordinator* input_ = nullptr; // Host 保留直到 owner Stop 确认。
		Microsoft::WRL::ComPtr<IDirectManipulationManager> manager_;
		Microsoft::WRL::ComPtr<IDirectManipulationUpdateManager> updates_;
		Microsoft::WRL::ComPtr<IDirectManipulationViewport> viewport_;
		Microsoft::WRL::ComPtr<IDirectManipulationContent> content_;
		Microsoft::WRL::ComPtr<Handler> handler_;
		HWND window_ = nullptr;
		RECT rect_{};
		DWORD cookie_ = 0;
		double x_ = 0.0, y_ = 0.0;
		Detail::TouchpadNavigationRetirement retirement_;
		bool uninitialize_ = false;
		bool resetting_ = false, failed_ = false, initialized_ = false, viewportEnabled_ = false;
	};

	class TouchpadInteractionOwner
	{
	public:
		TouchpadInteractionOwner(Detail::TouchpadNavigationBridge& bridge, ContactInputCoordinator& input)
			: bridge_(bridge), input_(input), output_(bridge) {}

		TouchpadAvailability Initialize(HWND window)
		{
			window_ = window;
			if (!api_.Load()) return TouchpadAvailability::RegistrationUnavailable;
			const INTERACTION_CONTEXT_CONFIGURATION pan{ INTERACTION_ID_MANIPULATION,
				INTERACTION_CONFIGURATION_FLAG_MANIPULATION | INTERACTION_CONFIGURATION_FLAG_MANIPULATION_TRANSLATION_X |
				INTERACTION_CONFIGURATION_FLAG_MANIPULATION_TRANSLATION_Y |
				INTERACTION_CONFIGURATION_FLAG_MANIPULATION_MULTIPLE_FINGER_PANNING |
				INTERACTION_CONFIGURATION_FLAG_MANIPULATION_TRANSLATION_INERTIA };
			if (FAILED(api_.create(&context_)) || FAILED(api_.configure(context_, 1, &pan)) ||
				FAILED(api_.property(context_, INTERACTION_CONTEXT_PROPERTY_FILTER_POINTERS, FALSE)) ||
				FAILED(api_.property(context_, INTERACTION_CONTEXT_PROPERTY_MEASUREMENT_UNITS, 1)) ||
				FAILED(api_.translation(context_, TRANSLATION_PARAMETER_MAX_CONTACT_COUNT, 2)) ||
				FAILED(api_.translation(context_, TRANSLATION_PARAMETER_MIN_CONTACT_COUNT, 2)) ||
				FAILED(api_.callback(context_, OnOutput, this)))
				return TouchpadAvailability::InitFailed;
			// 最后才向 USER32 接管窗口，任何前置失败都不改变 RTS 消息路由。
			registered_ = api_.registration(window_, TRUE) != FALSE;
			if (!registered_) LogCanvasPan("touchpad-register failed error=%lu", GetLastError());
			return registered_ ? TouchpadAvailability::Ok : TouchpadAvailability::RegistrationUnavailable;
		}

		bool Shutdown()
		{
			Stop();
			if (registered_ && IsWindow(window_) && !api_.registration(window_, FALSE)) return false;
			registered_ = false;
			if (context_ && FAILED(api_.destroy(context_))) return false;
			context_ = nullptr;
			return true;
		}

		void Update()
		{
			if (recognizing_ && (!Admitted() || (output_.Gliding() && !bridge_.inertiaAllowed.load()))) Stop();
		}

		// 有值表示该消息属于触控板；false 让未接管的序列全程走系统默认处理。
		std::optional<bool> Message(UINT message, WPARAM param, bool moveMode)
		{
			if (!registered_) return std::nullopt;
			if (message == WM_TIMER && timer_ && param == timer_)
			{
				Update();
				if (timer_)
				{
					if (FAILED(api_.inertia(context_))) stopPending_ = true;
					FinishOutput();
				}
				return true;
			}
			if (message == Detail::kStopContentInertia || message == Detail::kEndContentInertia)
			{
				// 系统要求立即停止：先作废本代已排队的惯性位移，再结束识别。
				if (output_.Gliding()) { bridge_.Cancel(); Stop(); }
				return true;
			}
			const UINT32 id = GET_POINTERID_WPARAM(param);
			if (message == WM_POINTERCAPTURECHANGED)
			{
				if (!std::erase(contacts_, id)) return std::nullopt;
				Stop();
				return consume_;
			}
			if (message == WM_TIMER) return std::nullopt;
			const bool tracked = std::find(contacts_.begin(), contacts_.end(), id) != contacts_.end();
			POINTER_INFO info{};
			const bool known = api_.info(id, &info) != FALSE;
			if (known && info.pointerType != PT_TOUCHPAD)
			{
				// 丢失终态的旧 ID 可能已复用于笔或触摸，必须放行新设备的首包。
				if (tracked) { std::erase(contacts_, id); Stop(); }
				return std::nullopt;
			}
			if (!known && !tracked) return std::nullopt;
			if (message == WM_POINTERLEAVE || message == WM_POINTERENTER)
			{
				if (tracked && IS_POINTER_CANCELED_WPARAM(param))
				{
					std::erase(contacts_, id);
					Stop();
				}
				return tracked && consume_;
			}
			if (message == WM_POINTERDOWN && !tracked)
			{
				// 设备变化或丢失终态后已不存在的指针不再阻止新序列开始。
				std::erase_if(contacts_, [this](UINT32 contact) { POINTER_INFO stale{}; return api_.info(contact, &stale) == FALSE; });
				if (contacts_.empty())
				{
					Stop();
					consume_ = moveMode && bridge_.enabled.load() && !input_.AdmissionBlocked();
					if (consume_)
					{
						output_.Begin(input_.AdmissionRevision());
						recognizing_ = true;
					}
				}
				contacts_.push_back(id);
			}
			else if (!tracked) return false;
			if (message == WM_POINTERUP) std::erase(contacts_, id);
			if (!consume_) return false;
			// 已接管序列即使中途换工具也消费到 Up，不能从中途交回系统再提升滚轮。
			if (!moveMode || !known || (info.pointerFlags & POINTER_FLAG_CANCELED) || !Admitted()) Stop();
			if (recognizing_ && output_.NewFrame(info))
			{
				if (!ProcessFrame(id)) stopPending_ = true;
				FinishOutput();
			}
			return true;
		}

		void DeviceChanged()
		{
			// 只停止识别；已接管序列继续消费到终态，不能把尾包交回默认处理。
			Stop();
		}
	private:
		static std::int64_t Now() noexcept
		{ LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return now.QuadPart; }

		bool Admitted() const noexcept
		{ return !input_.AdmissionBlocked() && output_.Current(input_.AdmissionRevision()); }

		bool ProcessFrame(UINT32 id)
		{
			UINT32 frames = 0, pointers = 0;
			if (!api_.history(id, &frames, &pointers, nullptr) || !frames || !pointers ||
				frames > (std::numeric_limits<UINT32>::max)() / pointers) return false;
			try
			{
				std::vector<POINTER_TOUCH_INFO> raw(static_cast<std::size_t>(frames) * pointers);
				if (!api_.history(id, &frames, &pointers, raw.data())) return false;
				if (!frames || !pointers || static_cast<std::uint64_t>(frames) * pointers > raw.size()) return false;
				frames = output_.NewHistoryFrames(raw.data(), frames, pointers);
				if (!frames) return true;
				std::vector<POINTER_TYPE_INFO> typed(static_cast<std::size_t>(frames) * pointers);
				// 保留系统要求的逆时序、每帧的全部触点和设备相对坐标。
				for (std::size_t i = 0; i < typed.size(); ++i)
				{
					if (raw[i].pointerInfo.pointerType != PT_TOUCHPAD) return false;
					typed[i].type = PT_TOUCHPAD;
					typed[i].touchInfo = raw[i];
				}
				return SUCCEEDED(api_.frames(context_, frames, pointers, typed.data()));
			}
			catch (const std::bad_alloc&) { return false; }
			catch (const std::length_error&) { return false; }
		}

		static void CALLBACK OnOutput(void* data, const INTERACTION_CONTEXT_OUTPUT* value)
		{
			auto& self = *static_cast<TouchpadInteractionOwner*>(data);
			if (self.recognizing_ && !self.stopPending_ &&
				(!self.Admitted() || !self.output_.Apply(*value, Now()))) self.stopPending_ = true;
		}
		void FinishOutput()
		{
			// 等识别回调退栈再 Stop 或启动时钟，避免在同一 context 内重入处理输入。
			if (stopPending_ || !Admitted()) { Stop(); return; }
			if (!output_.Gliding()) { StopTimer(); return; }
			if (!timer_)
			{
				timer_ = SetTimer(window_, reinterpret_cast<UINT_PTR>(this), 15, nullptr);
				if (!timer_) { Stop(); return; }
				if (api_.report) api_.report(window_, TRUE);
			}
		}
		void StopTimer()
		{
			if (!timer_) return;
			KillTimer(window_, timer_);
			timer_ = 0;
			if (api_.report) api_.report(window_, FALSE);
		}
		void Stop()
		{
			const bool active = std::exchange(recognizing_, false);
			output_.End(Now());
			stopPending_ = false;
			StopTimer();
			if (active) api_.stop(context_);
		}

		Detail::TouchpadNavigationBridge& bridge_;
		ContactInputCoordinator& input_;
		Detail::TouchpadInteractionOutput output_;
		Detail::TouchpadInteractionApi api_;
		HWND window_ = nullptr;
		HINTERACTIONCONTEXT context_ = nullptr;
		UINT_PTR timer_ = 0;
		std::vector<UINT32> contacts_;
		bool registered_ = false, consume_ = false, recognizing_ = false, stopPending_ = false;
	};

	WindowController::~WindowController()
	{
		// HWND 生命周期由 Window Service 管理；这里只解绑外部附着状态。
		DetachExternal();
	}

	bool WindowController::AttachExternal(HWND window, ExternalWindowCallbacks callbacks)
	{
		(void)callbacks; // 产品样式由 HostStyleCallbacks/Window Service 负责。
		if (!window || !IsWindow(window)) return false;
		if (externalWindow_.load(std::memory_order_acquire)) return false;
		mouseSelectionMode_.ResetWorkspace();
		touchpadNavigationIdentity_.store(nextTouchpadNavigationIdentity.fetch_add(1, std::memory_order_acq_rel) + 1);
		touchpadEntry_.store(TouchpadEntry::None, std::memory_order_release);
		touchpadNavigationAllowed_.store(true, std::memory_order_release);
		touchpadNavigationStoppedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!touchpadNavigationStoppedEvent_) return false;
		{
			// Host generation 之间不得继承上一代异步 completion 或画布命令。
			const std::scoped_lock lock(canvasCommandMutex_);
			canvasCommands_.clear();
			presentationCompletion_.reset();
			commandsBeforePresentationCompletion_ = 0;
		}
		RECT client = {};
		if (!GetClientRect(window, &client))
		{ CloseHandle(touchpadNavigationStoppedEvent_); touchpadNavigationStoppedEvent_ = nullptr; return false; }
		for (HANDLE& event : mouseOwnerControlEvents_)
		{
			event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!event)
			{
				for (HANDLE& created : mouseOwnerControlEvents_)
				{
					if (created) CloseHandle(created);
					created = nullptr;
				}
				CloseHandle(touchpadNavigationStoppedEvent_); touchpadNavigationStoppedEvent_ = nullptr;
				return false;
			}
		}
		hapticSurfaceVisible_.store(IsWindowVisible(window) != FALSE, std::memory_order_release);
		RequestHapticPointerLeave();
		exitRequested_.store(false, std::memory_order_release);
		resizeRequested_.store(false, std::memory_order_release);
		fullPresentRequested_.store(false, std::memory_order_release);
		compositionChangedRequested_.store(false, std::memory_order_release);
		drawingCursorRenderRequested_.store(false, std::memory_order_release);
		systemCursorRefreshPosted_.store(false, std::memory_order_release);
		activationAllowed_.store(false, std::memory_order_release);
		window_.store(window, std::memory_order_release);
		size_.width = (std::max)(0L, client.right - client.left);
		size_.height = (std::max)(0L, client.bottom - client.top);
		defaultCursor_ = LoadCursorW(nullptr, IDC_ARROW);
		cursorSystemDiagnosticKnown_ = false;
		lastTouchMousePositionKnown_ = false;
		cursorMouseDiagnosticEvent_ = 0;
		// 多点和反馈策略须在首个 contact 前发布；脱离时只恢复本对象改过的属性。
		const HANDLE previousTabletProperty = GetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY);
		const ATOM tabletPropertyAtom = GlobalAddAtom(MICROSOFT_TABLETPENSERVICE_PROPERTY);
		SetLastError(ERROR_SUCCESS);
		const bool tabletPropertyInstalled = SetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY,
			reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kTabletInputFlags))) != FALSE;
		const DWORD tabletPropertyError = tabletPropertyInstalled ? ERROR_SUCCESS : GetLastError();
		previousTabletInputProperty_.store(previousTabletProperty, std::memory_order_release);
		tabletInputPropertyInstalled_.store(tabletPropertyInstalled, std::memory_order_release);
		if (tabletPropertyAtom) GlobalDeleteAtom(tabletPropertyAtom);
		if (!tabletPropertyInstalled)
			LogWin32Error("Draw3.TabletInput.SetProp", tabletPropertyError ? tabletPropertyError : ERROR_GEN_FAILURE);
		RecordCursorDiagnostic("tablet-policy phase=attach hwnd=%p flags=0x%lx installed=%u error=%lu previous=%p",
			static_cast<void*>(window), static_cast<unsigned long>(kTabletInputFlags),
			tabletPropertyInstalled ? 1u : 0u, tabletPropertyError, previousTabletProperty);
		// 其余附着状态就绪后再开放 WndProc 转发，避免观察到半初始化 HWND。
		externalWindow_.store(true, std::memory_order_release);
		return true;
	}

	void WindowController::DetachExternal() noexcept
	{
		// Host 已先证明 producer 停止；不能先断消息入口再等待 owner 释放 COM。
		if (!StopTouchpadNavigation()) return;
		touchpadNavigationBridge_->SetWake(nullptr, nullptr);
		// 先关闭新消息入口，再清理 HWND 属性和输入状态。
		const bool wasAttached = externalWindow_.exchange(false, std::memory_order_acq_rel);
		mouseSelectionMode_.ResetWorkspace();
		RequestHapticPointerLeave();
		{
			const std::scoped_lock lock(canvasCommandMutex_);
			canvasCommands_.clear();
			presentationCompletion_.reset();
			commandsBeforePresentationCompletion_ = 0;
		}
		if (!wasAttached) return;
		if (touchpadNavigationStoppedEvent_) CloseHandle(touchpadNavigationStoppedEvent_);
		touchpadNavigationStoppedEvent_ = nullptr;
		RestoreTabletInputProperty(window_.load(std::memory_order_acquire));
		inputCoordinator_.store(nullptr, std::memory_order_release);
		activationAllowed_.store(false, std::memory_order_release);
		penCursorSample_.Clear();
		mouseCursorSample_.Clear();
		cursorOwner_.store(DrawingCursorPointerAuthority::Unknown,
			std::memory_order_release);
		touchCursorSuppressed_.store(false, std::memory_order_release);
		window_.store(nullptr, std::memory_order_release);
		for (HANDLE& event : mouseOwnerControlEvents_)
		{
			if (event) CloseHandle(event);
			event = nullptr;
		}
	}

	void WindowController::RestoreTabletInputProperty(HWND window) noexcept
	{
		if (!tabletInputPropertyInstalled_.exchange(false, std::memory_order_acq_rel)) return;
		const HANDLE previous = previousTabletInputProperty_.exchange(nullptr, std::memory_order_acq_rel);
		const HANDLE installed = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kTabletInputFlags));
		// HWND 已销毁或宿主后来改过属性时，不撤销别人的策略；这里不拥有 HANDLE 资源。
		if (!window || !IsWindow(window) || GetPropW(window, MICROSOFT_TABLETPENSERVICE_PROPERTY) != installed)
			return;
		SetLastError(ERROR_SUCCESS);
		const bool restored = previous
			? SetPropW(window, MICROSOFT_TABLETPENSERVICE_PROPERTY, previous) != FALSE
			: RemovePropW(window, MICROSOFT_TABLETPENSERVICE_PROPERTY) == installed;
		const DWORD error = restored ? ERROR_SUCCESS : GetLastError();
		if (!restored) LogWin32Error("Draw3.TabletInput.RestoreProp", error ? error : ERROR_GEN_FAILURE);
		RecordCursorDiagnostic("tablet-policy phase=restore hwnd=%p restored=%u error=%lu previous=%p",
			static_cast<void*>(window), restored ? 1u : 0u, error, previous);
	}

	LRESULT WindowController::HandleExternalMessage(
		HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!externalWindow_.load(std::memory_order_acquire) ||
			window_.load(std::memory_order_acquire) != window)
			return DefWindowProcW(window, message, wParam, lParam);
		return HandleWindowMessage(window, message, wParam, lParam);
	}


	int RunTouchpadIngressProductionProbe() noexcept
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* name)
		{
			if (!condition) { ++failures; std::fprintf(stderr, "[Draw3TouchpadIngress] FAIL: %s\n", name); }
		};
		try
		{
			TouchpadNavigationOwner refused(std::make_shared<Detail::TouchpadNavigationBridge>(), nullptr);
			check(!refused.Initialize(nullptr, false) && !refused.NeedsCleanup(),
				"DM refuses initialization before COM when the pointer producer is not ready");
			struct Viewport
			{
				unsigned configured = 0, options = 0;
				HRESULT ActivateConfiguration(DIRECTMANIPULATION_CONFIGURATION) { ++configured; return S_OK; }
				HRESULT SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS value) { options = value; return S_OK; }
				bool AutomaticallyAssociates() const { return !(options & DIRECTMANIPULATION_VIEWPORT_OPTIONS_EXPLICITHITTEST); }
				bool InputVisible() const { return (options & DIRECTMANIPULATION_VIEWPORT_OPTIONS_INPUT) != 0; }
			} viewport;
			check(SUCCEEDED(Detail::ConfigureTouchpadViewport(viewport)), "production viewport setup succeeds");
			unsigned associated = 0;
			for (const auto type : { PT_MOUSE, PT_TOUCH, PT_PEN, PT_POINTER })
			{
				const bool claimed = Detail::DispatchTouchpadHitTest(true, true, type, 41,
					[&](UINT32) { ++associated; return true; });
				check(!claimed && !associated && !viewport.AutomaticallyAssociates() && viewport.InputVisible(),
					"non-touchpad hit test preserves normal input without automatic DM association");
			}
			check(Detail::DispatchTouchpadHitTest(true, true, PT_TOUCHPAD, 42,
				[&](UINT32 id) { ++associated; return id == 42; }) && associated == 1,
				"only explicit touchpad reaches SetContact");
			Viewport oldPolicy; oldPolicy.options = DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE;
			check(oldPolicy.AutomaticallyAssociates() && !oldPolicy.InputVisible(),
				"negative control detects the previous manual-update-only policy");

			Detail::TouchpadNavigationBridge bridge; bridge.enabled.store(true);
			Detail::TouchpadNavigationFrontend frontend(bridge);
			const auto epoch = bridge.Epoch();
			check(frontend.Associate(epoch, 7, 42, [](UINT32) { return true; }) &&
				!bridge.Active() && !bridge.RunningAt(20, 7) && !bridge.NeedsUpdate(),
				"SetContact success in READY is not a held gesture or a permanent frame subscription");
			check(!frontend.PublishStatus(TouchpadNavigationPhase::Ready, 99, 0, 10) && frontend.Accepting(),
				"initial READY cannot retire an associated candidate before BEGIN");
			frontend.Interaction(true);
			check(!frontend.PublishStatus(TouchpadNavigationPhase::Ready, 99, 0, 12) && bridge.NeedsUpdate(),
				"repeated READY does not cut an unrecognized interaction");
			check(bridge.NeedsUpdate() && !bridge.RunningAt(20, 7), "interaction BEGIN pumps without blocking other devices");
			frontend.Interaction(false);
			check(!bridge.NeedsUpdate() && !frontend.Accepting(), "unrecognized BEGIN/END closes without timeout");
			check(frontend.Associate(epoch, 7, 42, [&](UINT32)
				{ frontend.Interaction(true); return true; }), "synchronous BEGIN is retained");
			check(frontend.PublishStatus(TouchpadNavigationPhase::Running, 0, 0, 30) && bridge.RunningAt(31, 7),
				"only confirmed RUNNING acquires native ownership");
			check(frontend.PublishStatus(TouchpadNavigationPhase::Inertia, 12, 0, 40), "confirmed inertia is published");
			frontend.Interaction(false);
			check(bridge.Active() && bridge.NeedsUpdate(), "interaction END does not cut active native inertia");
			check(frontend.PublishStatus(TouchpadNavigationPhase::Ready, 14, 0, 50) &&
				!bridge.NeedsUpdate() && !frontend.Accepting(), "READY retires the frame subscription");
			check(frontend.Associate(epoch, 7, 42, [&](UINT32)
				{ frontend.Interaction(true); frontend.PublishStatus(TouchpadNavigationPhase::Running, 0, 0, 59);
				  frontend.PublishStatus(TouchpadNavigationPhase::Ready, 0, 0, 60); return true; }) &&
				!bridge.NeedsUpdate() && !frontend.Accepting(), "S_OK tail cannot resurrect synchronous READY");
			const auto beforeCancel = bridge.Epoch();
			check(!frontend.Associate(beforeCancel, 7, 42, [&](UINT32)
				{ bridge.Cancel(); return true; }) && !bridge.NeedsUpdate() && !frontend.Accepting(),
				"S_OK tail cannot resurrect reentrant epoch cancellation");
			check(!frontend.Associate(bridge.Epoch(), 7, 42, [&](UINT32)
				{ frontend.Interaction(true); return false; }) && !bridge.NeedsUpdate() && !frontend.Accepting(),
				"association failure retires pending interaction");
			check(viewport.configured == 1, "contacts never reconfigure a system viewport already processing input");
		}
		catch (...) { check(false, "frontend probe exception"); }
		if (!failures) std::fprintf(stderr, "[Draw3TouchpadIngress] PASS: explicit input isolation and real association/interaction/status routines; no HWND\n");
		return failures;
	}

	int RunTouchpadOwnerNotificationProductionProbe() noexcept
	{
		const HANDLE original = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!original) return 1;
		HANDLE observer = nullptr;
		if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &observer,
			0, FALSE, DUPLICATE_SAME_ACCESS)) { CloseHandle(original); return 1; }
		TouchpadOwnerNotification notification(original);
		CloseHandle(original); // 模拟 Host 在 stopped=true 快路中立即关闭原句柄。
		const bool independent = notification && notification.Signal() &&
			WaitForSingleObject(observer, 0) == WAIT_OBJECT_0;
		CloseHandle(observer);
		TouchpadOwnerNotification unavailable(nullptr);
		return independent && !unavailable && !unavailable.Signal() ? 0 : 1;
	}

	void WindowController::FinishTouchpadNavigationDispatch(HWND window) noexcept
	{
		if (--touchpadDispatchDepth_) return;
		const auto operation = std::exchange(touchpadDeferredOperation_, 0u);
		if (touchpadStopCompletionPending_)
		{
			const HANDLE completed = touchpadNavigationStoppedEvent_;
			TouchpadOwnerNotification notification(completed);
			// 复制失败时不发布成功，Host 保留对象；失败通知仍可安全使用尚未退休的原句柄。
			const bool succeeded = touchpadStopSucceeded_ && static_cast<bool>(notification);
			touchpadStopCompletionPending_ = false;
			touchpadNavigationOwnerStopped_.store(succeeded, std::memory_order_release);
			// 此后 Host 可能立即销毁 self/关闭原 HANDLE；只使用独立拥有的局部通知，不再读成员。
			if (notification) (void)notification.Signal();
			else if (completed) SetEvent(completed);
			return;
		}
		if (operation == 1)
		{
			// 普通更新交还消息队列，不能沿 COM 的嵌套 pump 递归 Update 直至占满 owner 堆栈。
			touchpadNavigationPosted_.store(false, std::memory_order_release);
			RequestTouchpadNavigationUpdate();
			return;
		}
		if (operation == 2) HandleExternalMessage(window, kTouchpadNavigationMessage,
			(touchpadNavigationIdentity_.load() << 1u) | 1u, reinterpret_cast<LPARAM>(this));
	}

	TouchpadAvailability WindowController::StartTouchpadNavigation(TouchpadEntry entry) noexcept
	{
		const HWND window = Handle();
		if (!window || !touchpadNavigationAllowed_.load() || entry == TouchpadEntry::None)
			return TouchpadAvailability::InitFailed;
		touchpadEntry_.store(entry);
		touchpadNavigationOwnerStopped_.store(false);
		const WPARAM identity = touchpadNavigationIdentity_.load();
		const LPARAM target = reinterpret_cast<LPARAM>(this);
		// 新入口在 HWND owner 同步完成初始化，实际返回值随生产者结果一起发布。
		DWORD_PTR result = 0;
		if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId())
			result = HandleExternalMessage(window, kStartTouchpadNavigationMessage, identity, target);
		else if (!SendMessageTimeoutW(window, kStartTouchpadNavigationMessage, identity, target,
			SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 5000, &result)) return TouchpadAvailability::InitFailed;
		if (result == static_cast<DWORD_PTR>(TouchpadAvailability::Ok)) return TouchpadAvailability::Ok;
		return result == static_cast<DWORD_PTR>(TouchpadAvailability::RegistrationUnavailable)
			? TouchpadAvailability::RegistrationUnavailable : TouchpadAvailability::InitFailed;
	}

	void WindowController::SetTouchpadNavigationEnabled(bool enabled, bool inertiaAllowed) noexcept
	{
		enabled = enabled && touchpadNavigationAllowed_.load(std::memory_order_acquire);
		const bool changed = touchpadNavigationEnabled_.exchange(enabled, std::memory_order_acq_rel) != enabled;
		const bool inertiaChanged = touchpadNavigationInertia_.exchange(inertiaAllowed, std::memory_order_acq_rel) != inertiaAllowed;
		touchpadNavigationBridge_->enabled.store(enabled, std::memory_order_release);
		touchpadNavigationBridge_->inertiaAllowed.store(inertiaAllowed, std::memory_order_release);
		if (!changed && !inertiaChanged) return;
		if (!enabled) touchpadNavigationBridge_->Cancel();
		RequestTouchpadNavigationUpdate();
	}

	void WindowController::CancelTouchpadNavigation(std::int64_t takeoverQpc) noexcept
	{
		touchpadNavigationBridge_->Cancel(takeoverQpc); // 先作废累计值，owner 未处理 Stop 时也不能再移动画布。
		RequestTouchpadNavigationUpdate();
	}

	void WindowController::RequestTouchpadNavigationUpdate() noexcept
	{
		if (!touchpadNavigationAllowed_.load(std::memory_order_acquire)) return;
		const HWND window = Handle();
		if (!window || touchpadNavigationPosted_.exchange(true, std::memory_order_acq_rel)) return;
		touchpadNavigationOwnerStopped_.store(false, std::memory_order_release);
		if (!PostMessageW(window, kTouchpadNavigationMessage, touchpadNavigationIdentity_.load() << 1u, reinterpret_cast<LPARAM>(this)))
			touchpadNavigationPosted_.store(false, std::memory_order_release);
	}

	bool WindowController::StopTouchpadNavigation() noexcept
	{
		// 本 attach 代永久封口；drawing 尚未退出时逐帧 SetEnabled 也不能重新开 producer。
		touchpadNavigationAllowed_.store(false, std::memory_order_release);
		touchpadEntry_.store(TouchpadEntry::None);
		touchpadNavigationEnabled_.store(false, std::memory_order_release);
		touchpadNavigationBridge_->enabled.store(false, std::memory_order_release);
		touchpadNavigationBridge_->Cancel();
		if (touchpadNavigationOwnerStopped_.load(std::memory_order_acquire)) return true;
		const HWND window = Handle();
		if (!window || !IsWindow(window)) return false;
		if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId())
		{
			if (touchpadDispatchDepth_) return false;
			return HandleExternalMessage(window, kTouchpadNavigationMessage, (touchpadNavigationIdentity_.load() << 1u) | 1u, reinterpret_cast<LPARAM>(this)) == 1;
		}
		const HANDLE completed = touchpadNavigationStoppedEvent_;
		if (!completed || !ResetEvent(completed)) return false;
		if (!PostMessageW(window, kTouchpadNavigationMessage, (touchpadNavigationIdentity_.load() << 1u) | 1u, reinterpret_cast<LPARAM>(this))) return false;
		// 超时不是释放证明；Host 沿既有 unproven producer stop 合同保留本对象。
		return WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0 &&
			touchpadNavigationOwnerStopped_.load(std::memory_order_acquire);
	}

	std::uint64_t WindowController::DrainTouchpadNavigation(std::vector<TouchpadNavigationSample>& samples)
	{ return touchpadNavigationBridge_->Drain(samples); }
	std::uint64_t WindowController::TouchpadNavigationEpoch() const noexcept
	{ return touchpadNavigationBridge_->Epoch(); }
	bool WindowController::TouchpadNavigationActive() const noexcept
	{ return touchpadNavigationBridge_->Active(); }
	bool WindowController::TouchpadNavigationNeedsUpdate() const noexcept
	{ return touchpadNavigationBridge_->NeedsUpdate(); }
	bool WindowController::TouchpadNavigationRunning() const noexcept
	{ return touchpadNavigationBridge_->Running(); }
	bool WindowController::TouchpadNavigationRunningAt(std::int64_t qpc) const noexcept
	{
		const auto* input = inputCoordinator_.load(std::memory_order_acquire);
		return touchpadNavigationBridge_->RunningAt(qpc, input ? input->AdmissionRevision() : 0);
	}
	std::int64_t WindowController::TouchpadNavigationStartQpc() const noexcept
	{ return touchpadNavigationBridge_->StartQpc(); }
	void WindowController::PublishHiddenTestTouchpadNavigation(TouchpadNavigationPhase phase,
		double x, double y, std::int64_t qpc) noexcept
	{
		if (!touchpadNavigationBridge_->enabled.load(std::memory_order_acquire)) return;
		const auto* input = inputCoordinator_.load(std::memory_order_acquire);
		const auto revision = input ? input->AdmissionRevision() : 0;
		touchpadNavigationBridge_->Publish(TouchpadNavigationEpoch(), phase, x, y, qpc, revision);
		if (phase == TouchpadNavigationPhase::Inertia && !touchpadNavigationBridge_->inertiaAllowed.load())
			touchpadNavigationBridge_->Publish(TouchpadNavigationEpoch(), TouchpadNavigationPhase::Cancelled, x, y, qpc, revision);
	}

	void WindowController::PublishHiddenTestTouchpadInteraction(bool begin) noexcept
	{
		const auto* input = inputCoordinator_.load(std::memory_order_acquire);
		Detail::TouchpadNavigationFrontend frontend(*touchpadNavigationBridge_);
		frontend.Associate(TouchpadNavigationEpoch(), input ? input->AdmissionRevision() : 0, 1,
			[](UINT32) { return true; });
		frontend.Interaction(begin);
	}

	void WindowController::CloseMouseInputAdmission() noexcept
	{
		mouseInput_.CloseAdmission();
	}

	WindowController::PointerDevice& WindowController::PointerDeviceFor(HANDLE handle, InputDeviceType type)
	{
		for (auto& item : pointerDevices_)
			if (item.handle == handle) return item;

		PointerDevice item;
		item.handle = handle;
		auto& source = item.source;
		source.contextId = ++nextPointerContext_;
		source.generation = source.contextId;
		const auto& api = DrawingPointerApi();
		POINTER_DEVICE_INFO info{};
		if (api.device(handle, &info))
		{
			SpeedEraser::SourceKind kind = SpeedEraser::SourceKind::Unknown;
			switch (info.pointerDeviceType)
			{
			case POINTER_DEVICE_TYPE_INTEGRATED_PEN: kind = SpeedEraser::SourceKind::IntegratedPen; break;
			case POINTER_DEVICE_TYPE_EXTERNAL_PEN: kind = SpeedEraser::SourceKind::ExternalPen; break;
			case POINTER_DEVICE_TYPE_TOUCH: kind = SpeedEraser::SourceKind::Touch; break;
			case POINTER_DEVICE_TYPE_TOUCH_PAD: kind = SpeedEraser::SourceKind::TouchPad; break;
			}
			const bool mapped = api.rects && api.rects(handle, &item.deviceRect, &item.displayRect) &&
				item.deviceRect.right > item.deviceRect.left && item.deviceRect.bottom > item.deviceRect.top &&
				item.displayRect.right > item.displayRect.left && item.displayRect.bottom > item.displayRect.top;
			if (!mapped)
			{
				// 失败的 API 调用可能改写输出参数；禁止以后用这些矩形作亚像素转换。
				item.deviceRect = {};
				item.displayRect = {};
			}
			source.kind = SpeedEraser::ClassifySource(static_cast<uint32_t>(type), true, false, false,
				kind, true, false, mapped && !info.monitor);
			source.recognition = source.kind == SpeedEraser::SourceKind::Unknown ?
				SpeedEraser::SourceRecognition::Conflict : SpeedEraser::SourceRecognition::PointerCursor;
			if (mapped && source.kind != SpeedEraser::SourceKind::Unknown)
			{
				source.mappedMonitor = reinterpret_cast<uintptr_t>(info.monitor);
				source.mappedLeft = item.displayRect.left;
				source.mappedTop = item.displayRect.top;
				source.mappedWidth = item.displayRect.right - item.displayRect.left;
				source.mappedHeight = item.displayRect.bottom - item.displayRect.top;
			}
		}
		return pointerDevices_.emplace_back(item);
	}

	namespace
	{
		void TracePointerPacket(UINT32 id, const ContactSnapshot& snapshot, uint64_t performance,
			UINT32 mask, bool published) noexcept
		{
			if (!CursorDiagnosticsEnabled()) return;
			LARGE_INTEGER now{};
			QueryPerformanceCounter(&now);
			RecordCursorDiagnostic("pointer-input tcid=%u cid=%u phase=%u mask=0x%x performance=%llu arrival=%lld publish=%lld published=%u",
				snapshot.source.contextId, id, static_cast<unsigned>(snapshot.phase), mask,
				static_cast<unsigned long long>(performance), static_cast<long long>(snapshot.qpc),
				static_cast<long long>(now.QuadPart), published ? 1u : 0u);
		}
	}

	void WindowController::FinishPointerContact(std::size_t index, ContactPhase phase, int64_t qpc,
		uint64_t performance, UINT32 mask) noexcept
	{
		auto contact = pointerContacts_[index];
		pointerContacts_.erase(pointerContacts_.begin() + index);
		auto& snapshot = contact.last;
		snapshot.qpc = qpc;
		snapshot.phase = phase;
		const bool cancelled = phase == ContactPhase::Cancelled;
		if (auto* input = inputCoordinator_.load(std::memory_order_acquire))
		{
			const bool published = cancelled ?
				input->PublishCancelled(snapshot.source.contextId, contact.pointerId, snapshot) :
				input->PublishUp(snapshot.source.contextId, contact.pointerId, snapshot);
			TracePointerPacket(contact.pointerId, snapshot, performance, mask, published);
		}
		if (contact.type == InputDeviceType::Touch) NotifyTouchContactEnd();
		else
		{
			NotifyPenFeedbackContact({ contact.penToken, snapshot.source.contextId, contact.pointerId,
				snapshot.source.generation, contact.downQpc, cancelled ? PenFeedbackPhase::Cancelled : PenFeedbackPhase::Up });
			ClearPenCursorSampleForContact(contact.penToken);
			if (cancelled)
			{
				if (suppressedPenPointerId_.load(std::memory_order_acquire) == contact.pointerId)
				{
					suppressedPenPointerId_.store(0, std::memory_order_release);
					SetPointerPenContactSuppressed(false);
				}
				if (pointerPenRangeId_ == contact.pointerId)
				{
					pointerPenRangeId_ = 0;
					RequestHapticPointerLeave();
				}
			}
		}
	}

	void WindowController::CancelPointerContacts(HANDLE device) noexcept
	{
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);
		for (auto i = pointerContacts_.size(); i > 0; --i)
			if (!device || pointerContacts_[i - 1].device == device)
				FinishPointerContact(i - 1, ContactPhase::Cancelled, now.QuadPart);
	}

	bool WindowController::HandlePointerContact(HWND window, UINT message, UINT32 id, bool cancelled,
		const POINTER_PEN_INFO* pen, const POINTER_TOUCH_INFO* touch, UINT32 cursorId, int64_t qpc)
	{
		auto it = std::find_if(pointerContacts_.begin(), pointerContacts_.end(),
			[id](const auto& c) { return c.pointerId == id; });
		const POINTER_INFO* info = pen ? &pen->pointerInfo : touch ? &touch->pointerInfo : nullptr;
		cancelled = cancelled || (info && (info->pointerFlags & POINTER_FLAG_CANCELED));
		const bool down = message == WM_POINTERDOWN;
		const bool up = message == WM_POINTERUP;
		const bool live = it != pointerContacts_.end();
		const bool switched = live && info && (info->sourceDevice != it->device ||
			(pen != nullptr) != (it->type == InputDeviceType::Pen));
		if (live && (down || cancelled || switched || (!info && up)))
		{
			const auto performance = cancelled && info && !switched ?
				PointerInput::PerformanceCount(info->PerformanceCount, it->performanceCount, qpc) : 0;
			const UINT32 mask = pen ? pen->penMask : touch ? touch->touchMask : 0;
			FinishPointerContact(it - pointerContacts_.begin(), ContactPhase::Cancelled, qpc, performance, mask);
			it = pointerContacts_.end();
		}
		if (!info || cancelled) return live || info;
		if (!down && it == pointerContacts_.end()) return up || (info->pointerFlags & POINTER_FLAG_INCONTACT);
		if (!up && !(info->pointerFlags & POINTER_FLAG_INCONTACT))
		{
			if (it != pointerContacts_.end())
				FinishPointerContact(it - pointerContacts_.begin(), ContactPhase::Cancelled, qpc);
			return live;
		}
		auto* input = inputCoordinator_.load(std::memory_order_acquire);
		if (!input) return true;
		if (down && (!mouseInput_.Open() ||
			static_cast<LONG>(static_cast<DWORD>(GetMessageTime()) - mouseStartTick_) < 0)) return true;
		POINT origin{};
		if (!ClientToScreen(window, &origin))
		{
			if (up) FinishPointerContact(it - pointerContacts_.begin(), ContactPhase::Cancelled, qpc);
			return true;
		}

		const auto type = pen ? InputDeviceType::Pen : InputDeviceType::Touch;
		const auto& device = PointerDeviceFor(info->sourceDevice, type);
		if (device.source.kind == SpeedEraser::SourceKind::TouchPad) return true;
		ContactSnapshot snapshot;
		snapshot.qpc = qpc;
		snapshot.phase = down ? ContactPhase::Down : up ? ContactPhase::Up : ContactPhase::Move;
		snapshot.source = down ? device.source : it->last.source;
		if (down) snapshot.source.cursorId = cursorId;
		const auto point = PointerInput::MapPosition(info->ptPixelLocationRaw, info->ptHimetricLocationRaw,
			origin, pen ? &device.deviceRect : nullptr, pen ? &device.displayRect : nullptr);
		snapshot.position = { point.x, point.y };
		const UINT32 mask = pen ? pen->penMask : touch->touchMask;
		if (pen)
		{
			snapshot.pressure = PointerInput::Pressure((mask & PEN_MASK_PRESSURE) != 0, pen->pressure);
			const auto angles = PointerInput::Tilt((mask & PEN_MASK_TILT_X) != 0,
				(mask & PEN_MASK_TILT_Y) != 0, pen->tiltX, pen->tiltY);
			snapshot.tilt = angles.tilt;
			snapshot.orientation = angles.orientation;
			snapshot.isInvertedCursor = (pen->penFlags & (PEN_FLAG_INVERTED | PEN_FLAG_ERASER)) != 0;
		}
		else
		{
			const auto area = PointerInput::ContactArea((mask & TOUCH_MASK_CONTACTAREA) != 0, touch->rcContactRaw);
			snapshot.rawContactSize = { area.rawWidth, area.rawHeight };
			snapshot.contactSize = { area.widthPx, area.heightPx };
			snapshot.contactAreaUnits = area.units;
		}

		const auto performance = PointerInput::PerformanceCount(info->PerformanceCount,
			down ? 0 : it->performanceCount, qpc);
		if (down)
		{
			// 先保留终态所需的绑定；同一 pointerId 可在 Up 后用于下一笔。
			pointerContacts_.push_back({ id, info->sourceDevice, type, snapshot, performance,
				pen ? ++nextPointerToken_ : 0, qpc });
			it = pointerContacts_.end() - 1;
			if (pen) NotifyPenFeedbackContact({ it->penToken, snapshot.source.contextId, id,
				snapshot.source.generation, qpc, PenFeedbackPhase::Down });
			const bool published = input->PublishDown(snapshot.source.contextId, id, type, snapshot);
			TracePointerPacket(id, snapshot, performance, mask, published);
			if (!published)
			{
				if (pen) NotifyPenFeedbackContact({ it->penToken, snapshot.source.contextId, id,
					snapshot.source.generation, qpc, PenFeedbackPhase::Cancelled });
				pointerContacts_.erase(it);
				return true;
			}
			if (touch) NotifyTouchContactBegin(true, static_cast<DWORD>(GetMessageTime()));
		}
		else
		{
			it->last = snapshot;
			if (performance) it->performanceCount = performance;
			if (up)
			{
				FinishPointerContact(it - pointerContacts_.begin(), ContactPhase::Up, qpc, performance, mask);
				return true;
			}
			const bool published = input->PublishMove(snapshot.source.contextId, id, snapshot);
			TracePointerPacket(id, snapshot, performance, mask, published);
		}
		if (pen)
		{
			DrawingCursorSample sample;
			sample.x = point.x;
			sample.y = point.y;
			sample.valid = sample.inContact = true;
			sample.inverted = snapshot.isInvertedCursor;
			sample.source = snapshot.source;
			sample.qpc = qpc;
			PublishPenCursorSampleImpl(sample, it->penToken, true, id);
		}
		return true;
	}

	bool WindowController::StartPointerInput() noexcept
	{
		const auto& api = DrawingPointerApi();
		if (!ResolveGetPointerType() || !ResolveGetPointerPenInfo() || !api.touch || !api.device || !api.notifications)
			return false;
		pointerStartRequested_.store(true, std::memory_order_release);
		return SetMouseCompatibilityEnabled(true);
	}

	bool WindowController::StopPointerInput() noexcept
	{
		return SetMouseCompatibilityEnabled(false);
	}

	bool WindowController::SetMouseCompatibilityEnabled(bool enabled) noexcept
	{
		mouseStartAllowed_.store(enabled, std::memory_order_release);
		if (!enabled)
		{
			pointerStartRequested_.store(false, std::memory_order_release);
			CloseMouseInputAdmission();
			if (mouseOwnerStopped_.load(std::memory_order_acquire)) return true;
		}
		const HWND window = Handle();
		if (!window || !IsWindow(window)) return false;
		// pending start 也必须走 owner 停止握手，不能因超时前尚未执行而误认为静止。
		if (enabled) mouseOwnerStopped_.store(false, std::memory_order_release);
		const WPARAM operation = enabled ? Detail::kMouseInputStart : Detail::kMouseInputStop;
		const LPARAM identity = reinterpret_cast<LPARAM>(this);
		if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId())
		{
			if (mouseDispatchDepth_ != 0) return false;
			return HandleExternalMessage(window, Detail::kMouseInputOwnerMessage, operation, identity) == 1;
		}
		const auto operationIndex = static_cast<std::size_t>(operation);
		const HANDLE completed = mouseOwnerControlEvents_[operationIndex];
		if (!completed || !ResetEvent(completed)) return false;
		mouseOwnerControlSucceeded_[operationIndex].store(false, std::memory_order_release);
		// 队列屏障不能用 SendMessage：它可能在 SetCapture 的 USER32 调用中重入，
		// 让停止确认先于外层 WM_MOUSE 退栈。PostMessage 在 owner 取下一消息时执行。
		if (!PostMessageW(window, Detail::kMouseInputOwnerMessage, operation, identity)) return false;
		// 超时不是停止证明；Host 保留仍被 owner 借用的资源并走致命清理合同。
		return WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0 &&
			mouseOwnerControlSucceeded_[operationIndex].load(std::memory_order_acquire);
	}

	void WindowController::ReleaseMouseCapture(HWND window) noexcept
	{
		if (GetCapture() == window) (void)ReleaseCapture();
	}

	void WindowController::CancelMouseInput(HWND window, bool captureLost) noexcept
	{
		LARGE_INTEGER qpc{};
		QueryPerformanceCounter(&qpc);
		if (auto* coordinator = inputCoordinator_.load(std::memory_order_acquire))
		{
			if (captureLost) mouseInput_.CaptureLost(*coordinator, qpc.QuadPart);
			else mouseInput_.Cancel(*coordinator, qpc.QuadPart);
		}
		ClearMouseCursorSample();
		ReleaseMouseCapture(window); // 状态已清，WM_CAPTURECHANGED 同步重入不会产生第二终态。
	}

	void WindowController::HandleCompatibilityMouse(HWND window, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		auto* coordinator = inputCoordinator_.load(std::memory_order_acquire);
		if (!coordinator) return;
		// 旧代次排队的 Down/Move/Up 均不能改变新模式，尤其旧 Up 不能结束新笔。
		if (static_cast<LONG>(static_cast<DWORD>(GetMessageTime()) - mouseStartTick_) <= 0) return;
		LARGE_INTEGER qpc{};
		QueryPerformanceCounter(&qpc);
		const float x = static_cast<float>(GET_X_LPARAM(lParam));
		const float y = static_cast<float>(GET_Y_LPARAM(lParam));
		const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN;
		const bool up = message == WM_LBUTTONUP || message == WM_RBUTTONUP;
		const auto button = message == WM_RBUTTONDOWN || message == WM_RBUTTONUP
			? decltype(mouseInput_)::Right : decltype(mouseInput_)::Left;
		// 白板移动模式下左键拖动是画布平移（右键仍由控制器丢弃），不按 Selection 隔离。
		const bool isolateForSelection = mouseSelectionMode_.Isolates();
		if (down)
		{
			// 鼠标兜底不经过 RTS/Pointer Down，也要通知白板收起菜单。
			NotifyCanvasContactDown();
			if (mouseInput_.Down(*coordinator, button, x, y, qpc.QuadPart, isolateForSelection))
			{
				(void)SetCapture(window);
				if (GetCapture() != window) mouseInput_.CaptureLost(*coordinator, qpc.QuadPart);
			}
		}
		else if (up) mouseInput_.Up(*coordinator, button, x, y, qpc.QuadPart);
		else if (message == WM_MOUSEMOVE)
		{
			const std::uint32_t held = ((wParam & MK_LBUTTON) ? decltype(mouseInput_)::Left : 0u) |
				((wParam & MK_RBUTTON) ? decltype(mouseInput_)::Right : 0u);
			mouseInput_.Move(*coordinator, held, x, y, qpc.QuadPart, isolateForSelection);
		}
		if (!mouseInput_.Active()) ReleaseMouseCapture(window);
		SetTouchCursorSuppressed(false);
		SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
		DrawingCursorSample sample;
		sample.x = x;
		sample.y = y;
		sample.valid = true;
		sample.inContact = mouseInput_.CursorHeld(); // 准入拒收仍按住时光标按接触态跟随画布帧率。
		sample.qpc = qpc.QuadPart;
		PublishMouseCursorSample(sample);
		if (message == WM_MOUSEMOVE && !trackingMouseLeave_)
		{
			TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
			trackingMouseLeave_ = TrackMouseEvent(&tracking) != FALSE;
		}
	}

	HWND WindowController::Handle() const
	{
		return window_.load(std::memory_order_acquire);
	}

	WindowSize WindowController::Size() const
	{
		return size_;
	}

	void WindowController::CommitSize(int width, int height)
	{
		size_.width = width;
		size_.height = height;
	}

	bool WindowController::TryDequeueCanvasCommand(CanvasCommand& command)
	{
		bool tookCompletion = false;
		{
			const std::scoped_lock lock(canvasCommandMutex_);
			if (presentationCompletion_ && commandsBeforePresentationCompletion_ == 0)
			{
				command = {};
				command.type = CanvasCommandType::PresentationPersistenceCompleted;
				command.presentationPersistenceCompletion = std::move(presentationCompletion_);
				tookCompletion = true;
			}
			else
			{
				if (canvasCommands_.empty()) return false;
				command = std::move(canvasCommands_.front());
				canvasCommands_.pop_front();
				if (presentationCompletion_) --commandsBeforePresentationCompletion_;
			}
		}
		// 不持命令锁发布 wake；Host 可能仍保留下一份终态，不能依赖新输入才继续。
		if (tookCompletion) RequestControlWake();
		return true;
	}

	bool WindowController::HasPendingCanvasCommand() noexcept
	{
		const std::scoped_lock lock(canvasCommandMutex_);
		return presentationCompletion_ || !canvasCommands_.empty();
	}

	bool WindowController::ConsumeResizeRequest(WindowSize& size)
	{
		if (!resizeRequested_.exchange(false, std::memory_order_acquire)) return false; // 消费一次跨线程尺寸请求。
		size.width = pendingResizeWidth_.load(std::memory_order_relaxed);
		size.height = pendingResizeHeight_.load(std::memory_order_relaxed);
		return true;
	}

	bool WindowController::ConsumeFullPresentRequest()
	{
		return fullPresentRequested_.exchange(false, std::memory_order_acquire);
	}

	bool WindowController::ConsumeCompositionChangedRequest()
	{
		return compositionChangedRequested_.exchange(false, std::memory_order_acquire);
	}

	bool WindowController::ConsumeDrawingCursorRenderRequest()
	{
		return drawingCursorRenderRequested_.exchange(false, std::memory_order_acquire);
	}

	void WindowController::RequestFullPresent()
	{
		fullPresentRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	void WindowController::SetInputCoordinator(ContactInputCoordinator* coordinator)
	{
		inputCoordinator_.store(coordinator, std::memory_order_release);
		touchpadNavigationBridge_->SetWake(coordinator ? +[](void* context)
			{ (void)static_cast<ContactInputCoordinator*>(context)->PublishControlWake(); } : nullptr, coordinator);
		if (coordinator) coordinator->PublishControlWake(); // 接线前已产生的窗口请求也不能漏唤醒。
	}

	void WindowController::RequestControlWake()
	{
		if (ContactInputCoordinator* coordinator = inputCoordinator_.load(std::memory_order_acquire))
			coordinator->PublishControlWake();
	}

	void WindowController::QueueCanvasCommand(CanvasCommand command)
	{
		{
			const std::scoped_lock lock(canvasCommandMutex_);
			// 尚未接受的相邻绝对目标可合并；中间的 Clear/Undo/保存屏障绝不跨越。
			if (command.type == CanvasCommandType::SetPresentationTarget &&
				!canvasCommands_.empty() &&
				// 预留终态也构成 FIFO 屏障，不能把其前方目标与后方目标合并。
				(!presentationCompletion_ || commandsBeforePresentationCompletion_ < canvasCommands_.size()) &&
				canvasCommands_.back().type == CanvasCommandType::SetPresentationTarget)
				canvasCommands_.back() = std::move(command);
			else canvasCommands_.push_back(std::move(command));
		}
		RequestControlWake();
	}

	void WindowController::SetGpuTransparentComposition(bool enabled)
	{
		gpuTransparentComposition_.store(enabled, std::memory_order_release);
	}

	DrawingTool WindowController::ActiveTool() const
	{
		return activeTool_.load(std::memory_order_relaxed);
	}

	void WindowController::SetActiveTool(DrawingTool tool) noexcept
	{
		if (static_cast<uint32_t>(tool) > static_cast<uint32_t>(DrawingTool::Shape))
			return;
		const bool changed = activeTool_.exchange(tool, std::memory_order_acq_rel) != tool;
		RequestDrawingCursorRender();
		if (changed) QueueSystemCursorRefresh();
		RequestControlWake();
	}

	bool WindowController::SelectionMode() const noexcept
	{
		return mouseSelectionMode_.Selection();
	}

	void WindowController::SetSelectionMode(bool enabled) noexcept
	{
		if (!mouseSelectionMode_.SetSelection(enabled))
			return;
		if (!enabled && touchpadEntry_.load() != TouchpadEntry::None) CancelTouchpadNavigation();
		// 静止切换也要清理旧瞬态并刷新本窗口光标，不等待下一条移动消息。
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
		RequestControlWake();
	}

	bool WindowController::CanvasPanMode() const noexcept
	{
		return mouseSelectionMode_.CanvasPan();
	}

	void WindowController::SetCanvasPanWorkspace(bool whiteboard) noexcept
	{
		const bool leaving = !whiteboard && CanvasPanMode();
		mouseSelectionMode_.SetWhiteboard(whiteboard);
		if (leaving && touchpadEntry_.load() != TouchpadEntry::None) CancelTouchpadNavigation();
	}

	bool WindowController::AutoSaveEnabled() const noexcept
	{
		return autoSaveEnabled_.load(std::memory_order_acquire);
	}

	void WindowController::SetAutoSaveEnabled(bool enabled) noexcept
	{
		autoSaveEnabled_.store(enabled, std::memory_order_release);
	}

	void WindowController::SetActivationAllowed(bool enabled) noexcept
	{
		activationAllowed_.store(enabled, std::memory_order_release);
	}

	EraserWidthMode WindowController::ActiveEraserWidthMode() const noexcept
	{
		return EraserWidthModeForRevision(
			eraserWidthModeRevision_.load(std::memory_order_acquire));
	}

	uint32_t WindowController::ActiveEraserWidthModeRevision() const noexcept
	{
		return eraserWidthModeRevision_.load(std::memory_order_acquire);
	}

	void WindowController::SetEraserWidthMode(EraserWidthMode mode) noexcept
	{
		uint32_t revision = eraserWidthModeRevision_.load(std::memory_order_relaxed);
		const EraserWidthMode current = EraserWidthModeForRevision(revision);
		if (current == mode) return;
		++revision;
		if (EraserWidthModeForRevision(revision) != mode) ++revision;
		eraserWidthModeRevision_.store(revision, std::memory_order_release);
		RequestControlWake();
	}



	void WindowController::PublishHiddenTestMouseCursor(const DrawingCursorSample& sample) noexcept
	{
		SetTouchCursorSuppressed(false);
		SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
		PublishMouseCursorSample(sample);
	}

	void WindowController::ClearHiddenTestMouseCursor() noexcept
	{
		ClearMouseCursorSample();
	}

	void WindowController::SetEraserInputs(const SpeedEraser::InputSettings& settings)
	{
		{std::scoped_lock lock(speedEraserConfigMutex_);if(eraserInputs_==settings)return;eraserInputs_=settings;}
		RequestDrawingCursorRender();RequestControlWake();
	}
	SpeedEraser::InputSettings WindowController::EraserInputsSnapshot() const
	{
		std::scoped_lock lock(speedEraserConfigMutex_);return eraserInputs_;
	}

	void WindowController::SetSpeedEraserDisplayScale(const SpeedEraser::DisplayScale& scale)
	{
		{
			std::scoped_lock lock(speedEraserConfigMutex_);
			if (speedEraserDisplayScale_ == scale) return;
			speedEraserDisplayScale_ = scale;
		}
		RequestDrawingCursorRender();
		RequestControlWake();
	}

	SpeedEraser::DisplayScale WindowController::SpeedEraserDisplayScaleSnapshot() const
	{
		std::scoped_lock lock(speedEraserConfigMutex_);
		return speedEraserDisplayScale_;
	}

	void WindowController::SetSpeedEraserDeviceMode(SpeedEraser::DeviceMode mode)
	{
		{
			std::scoped_lock lock(speedEraserConfigMutex_);
			if (speedEraserDeviceMode_ == mode) return;
			speedEraserDeviceMode_ = mode;
		}
		RequestDrawingCursorRender();
		RequestControlWake();
	}

	SpeedEraser::DeviceMode WindowController::SpeedEraserDeviceModeSnapshot() const
	{
		std::scoped_lock lock(speedEraserConfigMutex_);
		return speedEraserDeviceMode_;
	}

	CanvasPanDisplaySnapshot WindowController::CanvasPanDisplayContextSnapshot() const
	{
		std::scoped_lock lock(speedEraserConfigMutex_);
		return { speedEraserDisplayScale_, speedEraserDeviceMode_ };
	}

	void WindowController::SetProductVisualStyle(
		uint32_t colorRgba, double widthDip,
		const Inkeys::Business::PenWidth::Context& context,
		const draw3::shapes::ToolSpec& shapeTool,
		const draw3::shapes::ShapeStyle& shapeStyle) noexcept
	{
		if (!Inkeys::Business::PenWidth::IsValidDip(widthDip)) widthDip = 3.0f;
		{
			std::scoped_lock lock(productVisualStyleMutex_);
			productVisualStyle_ = { colorRgba, widthDip, context, shapeTool, shapeStyle };
		}
		RequestDrawingCursorRender();
		RequestControlWake();
	}

	ProductVisualStyle WindowController::ProductVisualStyleSnapshot() const noexcept
	{
		std::scoped_lock lock(productVisualStyleMutex_);
		return productVisualStyle_;
	}

	void WindowController::EnqueueCanvasCommand(CanvasCommand command)
	{
		QueueCanvasCommand(command);
	}

	bool WindowController::TryEnqueuePresentationCompletion(
		const std::shared_ptr<const PresentationPersistenceCompletion>& completion) noexcept
	{
		if (!completion) return false;
		{
			const std::scoped_lock lock(canvasCommandMutex_);
			if (presentationCompletion_) return false;
			presentationCompletion_ = completion;
			commandsBeforePresentationCompletion_ = canvasCommands_.size();
		}
		RequestControlWake();
		return true;
	}

	void WindowController::RequestExit() noexcept
	{
		exitRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	bool WindowController::ConfigureDrawingCursor(
		DrawingTool tool, const DrawingCursorAppearance& appearance)
	{
		if (!IsValidDrawingCursorAppearance(appearance))
		{
			DiagnosticOutput::Line(DiagnosticOutput::OutError) << "Invalid drawing cursor appearance for tool="
				<< static_cast<uint32_t>(tool) << std::endl;
			return false;
		}
		if (tool == DrawingTool::Pen) penCursorAppearance_ = appearance;
		else if (tool == DrawingTool::Highlighter) highlighterCursorAppearance_ = appearance;
		else if (tool == DrawingTool::Eraser) eraserCursorAppearance_ = appearance;
		else if (tool == DrawingTool::Laser) laserCursorAppearance_ = appearance;
		else penCursorAppearance_ = appearance;
		RequestDrawingCursorRender();
		return true;
	}

	DrawingCursorAppearance WindowController::CursorAppearanceForTool(
		DrawingTool tool) const noexcept
	{
		if (tool == DrawingTool::Pen) return penCursorAppearance_;
		if (tool == DrawingTool::Highlighter) return highlighterCursorAppearance_;
		if (tool == DrawingTool::Eraser) return eraserCursorAppearance_;
		if (tool == DrawingTool::Laser) return laserCursorAppearance_;
		return penCursorAppearance_;
	}

	void WindowController::SetMouseUsesSystemCursor(bool enabled) noexcept
	{
		if (mouseUsesSystemCursor_.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		RequestDrawingCursorRender(); // 同时清理旧应用光标脏区并切换系统箭头。
		QueueSystemCursorRefresh();
	}

	void WindowController::SetInjectedMouseAsTablet(bool enabled) noexcept
	{
		if (injectedMouseAsTablet_.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	bool WindowController::GetMouseUsesSystemCursor() const noexcept
	{
		return mouseUsesSystemCursor_.load(std::memory_order_acquire);
	}

	void WindowController::SetActiveDrawingCursorTool(DrawingTool tool) noexcept
	{
		const int32_t encoded = static_cast<int32_t>(tool);
		if (activeDrawingCursorTool_.exchange(encoded, std::memory_order_acq_rel) != encoded)
		{
			RequestDrawingCursorRender();
			QueueSystemCursorRefresh();
		}
	}

	void WindowController::ClearActiveDrawingCursorTool() noexcept
	{
		if (activeDrawingCursorTool_.exchange(-1, std::memory_order_acq_rel) != -1)
		{
			RequestDrawingCursorRender();
			QueueSystemCursorRefresh();
		}
	}

	DrawingTool WindowController::EffectiveDrawingCursorTool() const noexcept
	{
		const int32_t activeTool = activeDrawingCursorTool_.load(std::memory_order_acquire);
		if (activeTool >= static_cast<int32_t>(DrawingTool::Pen) &&
			activeTool <= static_cast<int32_t>(DrawingTool::Shape))
			return static_cast<DrawingTool>(activeTool);
		return ActiveTool();
	}

	DrawingCursorPointerAuthority WindowController::CursorOwner() const noexcept
	{
		return ResolveDrawingCursorVisualAuthority(
			cursorOwner_.load(std::memory_order_acquire),
			touchCursorSuppressed_.load(std::memory_order_acquire),
			touchPanActive_.load(std::memory_order_acquire),
			realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire));
	}

	bool WindowController::ReadPenCursorSample(DrawingCursorSample& sample, bool* suppressed) const noexcept
	{
		// 样本与抑制通道同截面读取，避免跨 API 发布后拼成不同笔的反馈。
		std::scoped_lock lock(penFeedbackMutex_);
		const bool read = penCursorSample_.Read(sample);
		if (suppressed) *suppressed = penCursorFromRts_ ? penRtsFeedbackSuppressed_ :
			penContactSuppressedForTouchPan_.load(std::memory_order_acquire);
		return read;
	}

	bool WindowController::ReadMouseCursorSample(DrawingCursorSample& sample) const noexcept
	{
		return mouseCursorSample_.Read(sample);
	}

	void WindowController::SetTouchPanActive(bool active) noexcept
	{
		if (touchPanActive_.exchange(active, std::memory_order_acq_rel) == active) return;
		RecordCursorDiagnostic("touch-pan active=%u owner=%u suppressed=%u mouseTakeover=%u",
			active ? 1u : 0u, static_cast<unsigned>(CursorOwner()),
			touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u,
			realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire) ? 1u : 0u);
		if (active)
			realMouseTakeoverDuringTouchPan_.store(false, std::memory_order_release);
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState(active ? "touch-pan-start" : "touch-pan-stop",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		if (active)
		{
			// Hover 可能已预启动触觉；Pan 期间保留 Pen presence，但隐藏应用光标。
			RequestHapticPointerLeave();
			if (penSample.valid && penSample.inContact)
				SetPenContactSuppressedForTouchPan(true);
		}
		else
		{
			const bool mouseTakeover = realMouseTakeoverDuringTouchPan_.load(
				std::memory_order_acquire);
			if (mouseTakeover && mouseSample.valid) SetTouchCursorSuppressed(false);
			SetDrawingCursorOwner(ResolveDrawingCursorOwnerAfterTouchPan(
				mouseTakeover,
				penSample.valid, mouseSample.valid));
		}
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
		RequestControlWake();
	}

	bool WindowController::TouchPanActive() const noexcept
	{
		return touchPanActive_.load(std::memory_order_acquire);
	}

	void WindowController::SuppressPenContactForTouchPan() noexcept
	{
		SetPenContactSuppressedForTouchPan(true);
		RequestHapticPointerLeave();
		// Pen presence 独立保留，供兼容 Mouse 判断和 Pan 结束后的 owner 恢复。
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
		RequestControlWake();
	}

	bool WindowController::PenContactSuppressedForTouchPan() const noexcept
	{
		std::scoped_lock lock(penFeedbackMutex_);
		// Pointer 本地锁存不能反向否决已被导航接纳的 RTS contact。
		return penCursorFromRts_ ? penRtsFeedbackSuppressed_ :
			penContactSuppressedForTouchPan_.load(std::memory_order_acquire);
	}

	void WindowController::NotifyPenFeedbackContact(const PenFeedbackContact& contact) noexcept
	{
		if (!contact.token || contact.downQpc <= 0) return;
		std::scoped_lock lock(penFeedbackMutex_);
		auto found = std::find_if(penFeedbackContacts_.begin(), penFeedbackContacts_.end(),
			[&](const auto& state) { return state.contact.token == contact.token; });
		if (contact.phase == PenFeedbackPhase::Down)
		{
			if (found != penFeedbackContacts_.end()) return; // 重复元数据不得重开已终止 token。
			for (auto& state : penFeedbackContacts_)
				if (!state.terminal && state.contact.tabletContextId == contact.tabletContextId &&
					state.contact.cursorId == contact.cursorId) state.terminal = true;
			constexpr size_t maximumContacts = 4096;
			if (penFeedbackContacts_.size() == maximumContacts)
			{
				const auto closed = std::find_if(penFeedbackContacts_.begin(), penFeedbackContacts_.end(),
					[](const auto& state) { return state.terminal; });
				if (closed == penFeedbackContacts_.end()) return;
				// 淘汰后拒绝更早的精确时间键，不能把旧队列 Down 误配给重用键。
				penFeedbackRetiredDownQpc_ = (std::max)(penFeedbackRetiredDownQpc_, closed->contact.downQpc);
				penFeedbackContacts_.erase(closed);
			}
			try
			{
				PenFeedbackState state;
				state.contact = contact;
				state.suppressed = touchPanActive_.load(std::memory_order_acquire);
				penFeedbackContacts_.push_back(state); // 只在 Down 分配，Move 不扩容。
			}
			catch (...) { return; } // 可选反馈凭据不足不能丢弃真实导航输入。
		}
		else if (found != penFeedbackContacts_.end() &&
			found->contact.tabletContextId == contact.tabletContextId &&
			found->contact.cursorId == contact.cursorId &&
			found->contact.sourceGeneration == contact.sourceGeneration &&
			found->contact.downQpc == contact.downQpc)
		{
			found->terminal = true;
			if (penCursorFromRts_ && penCursorFeedbackToken_ == contact.token)
				penRtsFeedbackSuppressed_ = false;
		}
	}

	uint64_t WindowController::NavigationPenFeedbackToken(uint32_t tabletContextId,
		uint32_t cursorId, uint64_t sourceGeneration, int64_t downQpc) const noexcept
	{
		std::scoped_lock lock(penFeedbackMutex_);
		if (downQpc <= penFeedbackRetiredDownQpc_) return 0;
		uint64_t token = 0;
		for (const auto& state : penFeedbackContacts_)
			if (state.contact.tabletContextId == tabletContextId && state.contact.cursorId == cursorId &&
				state.contact.sourceGeneration == sourceGeneration && state.contact.downQpc == downQpc)
			{
				if (token) return 0; // 精确键碰撞也不能选择“最近一笔”。
				token = state.contact.token;
			}
		return token;
	}

	void WindowController::SetNavigationPenFeedbackEpoch(uint64_t navigationEpoch) noexcept
	{
		std::scoped_lock lock(penFeedbackMutex_);
		penFeedbackNavigationEpoch_ = navigationEpoch;
	}

	void WindowController::ApplyNavigationPenDecision(uint64_t token, uint64_t panKey,
		uint64_t handleGeneration, uint64_t navigationEpoch, bool accepted) noexcept
	{
		bool changed = false;
		{
			std::scoped_lock lock(penFeedbackMutex_);
			if (!token || !panKey || !handleGeneration || !navigationEpoch ||
				navigationEpoch != penFeedbackNavigationEpoch_) return;
			const auto found = std::find_if(penFeedbackContacts_.begin(), penFeedbackContacts_.end(),
				[&](const auto& state) { return state.contact.token == token; });
			if (found == penFeedbackContacts_.end() || found->terminal) return;
			// 一笔的最终权限不反复改写；旧 handle/key 不能认领同一个 token。
			if (found->decided && (found->panKey != panKey || found->handleGeneration != handleGeneration ||
				found->suppressed != !accepted)) return;
			found->panKey = panKey; found->handleGeneration = handleGeneration;
			found->decided = true; found->suppressed = !accepted;
			if (penCursorFromRts_ && penCursorFeedbackToken_ == token)
			{
				changed = penRtsFeedbackSuppressed_ != !accepted;
				penRtsFeedbackSuppressed_ = !accepted;
			}
		}
		if (!changed) return;
		if (!accepted) RequestHapticPointerLeave();
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}


	void WindowController::NotifyTouchContactBegin() noexcept
	{
		NotifyTouchContactBegin(true, GetTickCount());
	}

	void WindowController::NotifyTouchContactBegin(bool trackActiveContact,
		uint32_t touchBarrierTick) noexcept
	{
		const uint64_t barrierState = kTouchInputBarrierKnownMask |
			static_cast<uint64_t>(touchBarrierTick);
		latestTouchInputBarrierTick_.store(barrierState, std::memory_order_release);
		if (trackActiveContact)
			activeTouchContactCount_.fetch_add(1u, std::memory_order_acq_rel);
		// 活动 Pan 已确认真实 Mouse 接管时，后续 Touch 指头不能清掉 Mouse 光标。
		const bool keepMouseTakeover = touchPanActive_.load(std::memory_order_acquire) &&
			realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire);
		if (!keepMouseTakeover)
		{
			SetTouchCursorSuppressed(true);
			ClearMouseCursorSample();
		}
		RecordCursorDiagnostic("touch-begin tracked=%u count=%u barrier=%u keepMouse=%u owner=%u suppressed=%u",
			trackActiveContact ? 1u : 0u,
			activeTouchContactCount_.load(std::memory_order_acquire), touchBarrierTick,
			keepMouseTakeover ? 1u : 0u, static_cast<unsigned>(CursorOwner()),
			touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u);
		// Mailbox 已经无效时 Clear 会早退；Touch Down 仍必须重新应用专用工具策略。
		QueueSystemCursorRefresh();
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState(trackActiveContact ? "touch-contact-begin" : "touch-pointer-barrier",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
	}

	void WindowController::NotifyTouchContactEnd() noexcept
	{
		uint32_t count = activeTouchContactCount_.load(std::memory_order_acquire);
		while (count > 0 && !activeTouchContactCount_.compare_exchange_weak(
			count, count - 1u, std::memory_order_acq_rel, std::memory_order_acquire))
		{
		}
		// Win7 终态可能另行恢复箭头；重复应用当前策略，保留 Touch 抑制等待真实输入。
		QueueSystemCursorRefresh();
		RecordCursorDiagnostic("touch-end count=%u owner=%u suppressed=%u",
			activeTouchContactCount_.load(std::memory_order_acquire),
			static_cast<unsigned>(CursorOwner()),
			touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u);
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("touch-contact-end",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
	}

	void WindowController::SetPenContactSuppressedForTouchPan(bool suppressed, bool updateRts) noexcept
	{
		{
			std::scoped_lock lock(penFeedbackMutex_);
			if (updateRts && suppressed && penCursorFromRts_ && penCursorFeedbackToken_)
				for (auto& state : penFeedbackContacts_)
					if (state.contact.token == penCursorFeedbackToken_ && !state.terminal && !state.decided)
						state.suppressed = penRtsFeedbackSuppressed_ = true;
		}
		const bool changed = penContactSuppressedForTouchPan_.exchange(
			suppressed, std::memory_order_acq_rel) != suppressed;
		if (!changed) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState(suppressed ? "suppression-latched" : "suppression-cleared",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
		if (suppressed)
		{
			RequestHapticPointerLeave();
			RequestControlWake();
		}
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	void WindowController::SetPointerPenContactSuppressed(bool suppressed) noexcept
	{
		pointerPenContactSuppressed_.store(suppressed, std::memory_order_release);
		SetPenContactSuppressedForTouchPan(suppressed, false);
	}

	void WindowController::PublishPenCursorSample(const DrawingCursorSample& sample) noexcept
	{
		PublishPenCursorSampleImpl(sample, 0, false);
	}

	void WindowController::PublishPenCursorSampleForContact(const DrawingCursorSample& sample,
		uint64_t token) noexcept
	{
		PublishPenCursorSampleImpl(sample, token, true);
	}

	void WindowController::ClearPenCursorSampleForContact(uint64_t token) noexcept
	{
		bool changed = false;
		{
			std::scoped_lock lock(penFeedbackMutex_);
			if (!penCursorFromRts_ || (token && token != penCursorFeedbackToken_)) return;
			if (!token && penCursorFeedbackToken_)
				for (const auto& state : penFeedbackContacts_)
					if (state.contact.token == penCursorFeedbackToken_ && !state.terminal) return;
			penCursorFromRts_ = false;
			penCursorFeedbackToken_ = 0;
			penRtsFeedbackSuppressed_ = false;
			if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0)
				changed = penContactSuppressedForTouchPan_.exchange(false, std::memory_order_acq_rel);
			// 身份校验与 plain mailbox Clear 必须原子化；旧终态不能越过新笔发布。
			changed = penCursorSample_.Clear() || changed;
		}
		FinishPenCursorClear(changed);
	}

	void WindowController::PublishPenCursorSampleImpl(const DrawingCursorSample& inputSample,
		uint64_t token, bool fromRts, uint32_t pointerContactId) noexcept
	{
		DrawingCursorSample sample = inputSample;
		DrawingCursorSample previous;
		bool suppressed = false, suppressionChanged = false, changed = false;
		{
			std::scoped_lock lock(penFeedbackMutex_);
			if (fromRts && token)
			{
				const auto found = std::find_if(penFeedbackContacts_.rbegin(), penFeedbackContacts_.rend(),
					[&](const auto& state) { return state.contact.token == token; });
				if (found != penFeedbackContacts_.rend())
				{
					if (found->terminal || found->contact.tabletContextId != inputSample.source.contextId ||
						found->contact.cursorId != (pointerContactId ? pointerContactId : inputSample.source.cursorId) ||
						found->contact.sourceGeneration != inputSample.source.generation) return;
					suppressed = found->suppressed;
				}
				else suppressed = touchPanActive_.load(std::memory_order_acquire) && inputSample.inContact;
			}
			else if (fromRts) suppressed = touchPanActive_.load(std::memory_order_acquire) && inputSample.inContact;
			else suppressed = ShouldSuppressPenFeedbackForTouchPan(touchPanActive_.load(std::memory_order_acquire),
				pointerPenContactSuppressed_.load(std::memory_order_acquire) ||
				penContactSuppressedForTouchPan_.load(std::memory_order_acquire) ||
				penCompatibilityMouseContactSuppressed_.load(std::memory_order_acquire), true, sample.inContact);
			if (!fromRts && suppressed)
				suppressionChanged = !penContactSuppressedForTouchPan_.exchange(true, std::memory_order_acq_rel);
			if (sample.valid)
			{
				// 沿用来源只供视觉策略，不能作为 RTS/Pointer 同笔身份凭据。
				const bool sourceKnown = sample.source.kind != SpeedEraser::SourceKind::Unknown ||
					sample.source.recognition != SpeedEraser::SourceRecognition::Unknown;
				DrawingCursorSample known;
				const bool hasKnown = lastKnownPenSource_.Read(known) && known.valid;
				if (sourceKnown)
				{
					if (!hasKnown || !(known.source == sample.source))
					{
						known.valid = true;
						known.source = sample.source;
						lastKnownPenSource_.Publish(known);
					}
				}
				else if (hasKnown) sample.source = known.source;
			}
			penCursorFromRts_ = fromRts;
			penCursorFeedbackToken_ = fromRts ? token : 0;
			penRtsFeedbackSuppressed_ = fromRts && suppressed;
			penCursorSample_.Read(previous);
			// 身份/抑制投影与样本发布同锁；Mutex 内不执行窗口、触觉或唤醒。
			changed = penCursorSample_.Publish(sample);
		}
		if (sample.valid) SetTouchCursorSuppressed(false);
		const bool touchPanActive = touchPanActive_.load(std::memory_order_acquire);
		const bool realMouseTakeover = realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire);
		if (sample.valid && (!touchPanActive || !realMouseTakeover))
			SetDrawingCursorOwner(DrawingCursorPointerAuthority::Pen);
		if (suppressionChanged)
		{
			RequestHapticPointerLeave(); RequestControlWake();
			RequestDrawingCursorRender(); QueueSystemCursorRefresh();
		}
		if (suppressed || !changed) return;
		RecordCursorDiagnostic("pen-sample contact=%u inverted=%u x=%.1f y=%.1f qpc=%lld owner=%u",
			sample.inContact ? 1u : 0u, sample.inverted ? 1u : 0u,
			sample.x, sample.y, static_cast<long long>(sample.qpc),
			static_cast<unsigned>(CursorOwner()));
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("pen-sample", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		// 接触期间由活动绘制帧读取 mailbox，避免每个 Pen 包额外唤醒。
		const bool stateChanged = previous.valid != sample.valid || previous.inContact != sample.inContact ||
			previous.inverted != sample.inverted;
		// 移动模式用系统箭头，持续悬停不能逐包打断正在推进的画布惯性。
		if (stateChanged || (!sample.inContact && !CanvasPanMode())) RequestDrawingCursorRender();
		// 接触中只发节拍唤醒：正常书写时不改变帧节奏，拒收（无活动笔画）时光标仍按帧率跟随。
		else if (!CanvasPanMode()) RequestDrawingCursorRenderPaced();
		if (previous.valid != sample.valid || previous.inContact != sample.inContact ||
			previous.inverted != sample.inverted) QueueSystemCursorRefresh();
	}

	void WindowController::ClearPenCursorSample() noexcept
	{
		bool changed = false;
		{
			std::scoped_lock lock(penFeedbackMutex_);
			penCursorFromRts_ = false; penCursorFeedbackToken_ = 0; penRtsFeedbackSuppressed_ = false;
			if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0)
				changed = penContactSuppressedForTouchPan_.exchange(false, std::memory_order_acq_rel);
			changed = penCursorSample_.Clear() || changed;
		}
		FinishPenCursorClear(changed);
	}

	void WindowController::FinishPenCursorClear(bool changed) noexcept
	{
		// Up 后 OutOfRange 可再次 Clear；即使 mailbox 已空，也要重应用策略。
		QueueSystemCursorRefresh();
		if (!changed) return;
		RecordCursorDiagnostic("pen-clear owner=%u", static_cast<unsigned>(CursorOwner()));
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("pen-sample-clear", lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		RequestDrawingCursorRender();
	}

	void WindowController::PublishMouseCursorSample(
		const DrawingCursorSample& sample) noexcept
	{
		DrawingCursorSample previous;
		mouseCursorSample_.Read(previous);
		if (!mouseCursorSample_.Publish(sample)) return;
		RecordCursorDiagnostic("mouse-sample contact=%u x=%.1f y=%.1f qpc=%lld owner=%u",
			sample.inContact ? 1u : 0u, sample.x, sample.y,
			static_cast<long long>(sample.qpc), static_cast<unsigned>(CursorOwner()));
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("mouse-sample", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		const bool stateChanged = previous.valid != sample.valid || previous.inContact != sample.inContact;
		// 移动模式由系统箭头跟随位置；普通 Move 只更新 mailbox，不能作为控制命令打断画布帧节拍。
		// 按下期间的纯位置变化与笔一致按画布帧率合并；性能选项开启时恢复逐移动打断。
		if (stateChanged || (!sample.inContact && !CanvasPanMode()) ||
			(!CanvasPanMode() && unthrottledMouseMoveWhileDrawing_.load(std::memory_order_acquire)))
			RequestDrawingCursorRender();
		else if (!CanvasPanMode()) RequestDrawingCursorRenderPaced();
		if (stateChanged) QueueSystemCursorRefresh();
	}

	void WindowController::ClearMouseCursorSample() noexcept
	{
		if (!mouseCursorSample_.Clear()) return;
		RecordCursorDiagnostic("mouse-clear owner=%u",
			static_cast<unsigned>(CursorOwner()));
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("mouse-sample-clear", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	bool WindowController::ShouldIgnoreMouseCursorMessage(bool promotedPointerMessage,
		bool penSampleValid, bool touchBarrierKnown,
		uint32_t mouseMessageTick, uint32_t touchBarrierTick,
		INPUT_MESSAGE_DEVICE_TYPE inputSource) const noexcept
	{
		return Inkeys::Drawing::Draw3::ShouldIgnoreMouseCursorMessage(
			promotedPointerMessage, ResolveGetPointerType() != nullptr,
			penSampleValid, touchBarrierKnown, mouseMessageTick, touchBarrierTick,
			inputSource);
	}

	void WindowController::RecordMouseCursorDiagnosticState(
		uint64_t eventId, const char* phase) const noexcept
	{
		if (!eventId) return;
		DrawingCursorSample mouse;
		mouseCursorSample_.Read(mouse);
		RecordCursorDiagnostic("mouse-state event=%llu phase=%s owner=%u persistent=%u suppressed=%u touchCount=%u mouseValid=%u mouseContact=%u x=%.1f y=%.1f pan=%u takeover=%u",
			static_cast<unsigned long long>(eventId), phase, static_cast<unsigned>(CursorOwner()),
			static_cast<unsigned>(cursorOwner_.load(std::memory_order_acquire)),
			touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u,
			activeTouchContactCount_.load(std::memory_order_acquire), mouse.valid ? 1u : 0u,
			mouse.inContact ? 1u : 0u, mouse.x, mouse.y,
			touchPanActive_.load(std::memory_order_acquire) ? 1u : 0u,
			realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire) ? 1u : 0u);
	}

	void WindowController::RequestDrawingCursorRender() noexcept
	{
		drawingCursorRenderRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	void WindowController::RequestDrawingCursorRenderPaced() noexcept
	{
		drawingCursorRenderRequested_.store(true, std::memory_order_release);
		if (ContactInputCoordinator* coordinator = inputCoordinator_.load(std::memory_order_acquire))
			coordinator->PublishPacedWake();
	}

	void WindowController::SetUnthrottledMouseMoveWhileDrawing(bool enabled) noexcept
	{
		unthrottledMouseMoveWhileDrawing_.store(enabled, std::memory_order_release);
	}

	void WindowController::QueueSystemCursorRefresh() noexcept
	{
		const HWND window = window_.load(std::memory_order_acquire);
		if (!window || systemCursorRefreshPosted_.exchange(true, std::memory_order_acq_rel)) return;
		if (!PostMessageW(window, kApplySystemCursorMessage, 0, 0))
		{
			const DWORD error = GetLastError();
			systemCursorRefreshPosted_.store(false, std::memory_order_release);
			RecordCursorDiagnostic("system-post ok=0 hwnd=%p error=%lu", static_cast<void*>(window), error);
		}
	}

	void WindowController::SetDrawingCursorOwner(
		DrawingCursorPointerAuthority owner) noexcept
	{
		const auto previous = cursorOwner_.exchange(owner, std::memory_order_acq_rel);
		if (previous == owner) return;
		RecordCursorDiagnostic("owner old=%u new=%u effective=%u suppressed=%u",
			static_cast<unsigned>(previous), static_cast<unsigned>(owner),
			static_cast<unsigned>(CursorOwner()),
			touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u);
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("cursor-owner-change", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	void WindowController::SetTouchCursorSuppressed(bool suppressed) noexcept
	{
		const bool previous = touchCursorSuppressed_.exchange(
			suppressed, std::memory_order_acq_rel);
		if (previous == suppressed) return;
		RecordCursorDiagnostic("touch-suppression old=%u new=%u effectiveOwner=%u persistentOwner=%u",
			previous ? 1u : 0u, suppressed ? 1u : 0u,
			static_cast<unsigned>(CursorOwner()),
			static_cast<unsigned>(cursorOwner_.load(std::memory_order_acquire)));
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void WindowController::TraceCursorState(const char* eventName, uint32_t pointerId,
		POINTER_INPUT_TYPE pointerType, bool pointerTypeKnown,
		bool forceLifecycle) noexcept
	{
		if (!cursorTraceEnabled_.load(std::memory_order_acquire)) return;
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		const DrawingCursorPointerAuthority authority = CursorOwner();
		const DrawingTool tool = EffectiveDrawingCursorTool();
		const bool touchPanActive = touchPanActive_.load(std::memory_order_acquire);
		const uint32_t activeTouchContactCount = activeTouchContactCount_.load(
			std::memory_order_acquire);
		const bool realMouseTakeover = realMouseTakeoverDuringTouchPan_.load(
			std::memory_order_acquire);
		const bool suppression = PenContactSuppressedForTouchPan();
		const uint32_t suppressedPointerId = suppressedPenPointerId_.load(
			std::memory_order_acquire);
		const auto hapticState = hapticPointers_.Inspect();
		const uint32_t pendingHapticPointerId = hapticState.pending.pointerId;
		const bool hapticPointerIdRequested = pendingHapticPointerId != 0;
		const bool hapticPointerLeaveRequested = hapticState.leave;
		const bool selectionMode = SelectionMode();
		const bool mouseUsesSystemCursor = selectionMode || mouseUsesSystemCursor_.load(
			std::memory_order_acquire);
		const bool hide = ShouldHideSystemDrawingCursor(authority,
			!selectionMode && tool == DrawingTool::Eraser,
			!selectionMode && tool == DrawingTool::Laser,
			penSample.valid, mouseSample.valid, mouseUsesSystemCursor,
			touchPanActive, realMouseTakeover,
			injectedMouseAsTablet_.load(std::memory_order_acquire) &&
				mouseSample.source.kind == SpeedEraser::SourceKind::ExternalPen, CanvasPanMode());
		DrawingCursorDiagnosticVisualState appVisual;
		const bool appVisualKnown = ReadDrawingCursorDiagnosticVisualState(appVisual);

		// 坐标和 sequence 不进入 key，Pointer Update 只有状态变化才输出。
		uint64_t stateKey = pointerId;
		stateKey = stateKey * 17u + static_cast<uint64_t>(pointerType);
		stateKey = stateKey * 17u + (pointerTypeKnown ? 1u : 0u);
		stateKey = stateKey * 17u + static_cast<uint64_t>(authority);
		stateKey = stateKey * 17u + (touchPanActive ? 1u : 0u);
		stateKey = stateKey * 17u + activeTouchContactCount;
		stateKey = stateKey * 17u + (realMouseTakeover ? 1u : 0u);
		stateKey = stateKey * 17u + (suppression ? 1u : 0u);
		stateKey = stateKey * 17u + suppressedPointerId;
		stateKey = stateKey * 17u + pendingHapticPointerId;
		stateKey = stateKey * 17u + (hapticPointerIdRequested ? 1u : 0u);
		stateKey = stateKey * 17u + (hapticPointerLeaveRequested ? 1u : 0u);
		stateKey = stateKey * 17u + (penSample.valid ? 1u : 0u);
		stateKey = stateKey * 17u + (penSample.inContact ? 1u : 0u);
		stateKey = stateKey * 17u + (penSample.inverted ? 1u : 0u);
		stateKey = stateKey * 17u + (mouseSample.valid ? 1u : 0u);
		stateKey = stateKey * 17u + (mouseSample.inContact ? 1u : 0u);
		stateKey = stateKey * 17u + (appVisualKnown ? 1u : 0u);
		stateKey = stateKey * 17u + (appVisual.visible ? 1u : 0u);
		stateKey = stateKey * 17u + static_cast<uint64_t>(appVisual.reason);
		stateKey = stateKey * 17u + (hide ? 1u : 0u);

		std::lock_guard<std::mutex> lock(cursorTraceMutex_);
		if (!forceLifecycle && lastCursorTraceStateKnown_ &&
			lastCursorTraceStateKey_ == stateKey) return;
		lastCursorTraceStateKnown_ = true;
		lastCursorTraceStateKey_ = stateKey;
		char line[960] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[CURSOR_TRACE][state] event=%s pointerId=%u pointerEventType=%s(%u) "
			"cursorOwner=%u touchPanActive=%u activeTouchContacts=%u "
			"realMouseTakeover=%u suppression=%u suppressedPointerId=%u "
			"haptic={pointerId=%u,pending=%u,leave=%u} "
			"penSample={valid=%u,contact=%u,inverted=%u} "
			"mouseSample={valid=%u,contact=%u} appCursor=%s appReason=%s "
			"ShouldHideSystemDrawingCursor=%u systemCursor=%s reason=%s\r\n",
			eventName ? eventName : "unknown", pointerId,
			PointerTypeName(pointerType, pointerTypeKnown),
			static_cast<unsigned>(pointerType), static_cast<unsigned>(authority),
			touchPanActive ? 1u : 0u, activeTouchContactCount,
			realMouseTakeover ? 1u : 0u,
			suppression ? 1u : 0u,
			suppressedPointerId, pendingHapticPointerId,
			hapticPointerIdRequested ? 1u : 0u,
			hapticPointerLeaveRequested ? 1u : 0u,
			penSample.valid ? 1u : 0u,
			penSample.inContact ? 1u : 0u, penSample.inverted ? 1u : 0u,
			mouseSample.valid ? 1u : 0u, mouseSample.inContact ? 1u : 0u,
			appVisualKnown ? appVisual.visible ? "visible" : "hidden" : "unknown",
			appVisualKnown ? DrawingCursorDiagnosticVisualReasonName(
				appVisual.reason) : "unknown",
			hide ? 1u : 0u, hide ? "hidden" : "arrow",
			SystemCursorDecisionReason(authority, tool, penSample.valid,
				mouseSample.valid, mouseUsesSystemCursor, touchPanActive,
				realMouseTakeover, hide));
		if (length > 0)
		{
			DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, line, (std::min)(
				static_cast<size_t>(length), sizeof(line) - 1));
			DiagnosticOutput::Wake();
		}
	}

	void WindowController::TraceTouchMouseMessage(UINT message, uint32_t messageTick,
		uint32_t touchBarrierTick, bool touchBarrierKnown,
		ULONG_PTR extraInfo, bool promotedPointerMessage,
		int x, int y, uint32_t activeTouchContactCount,
		bool accepted, const char* reason) noexcept
	{
		if (activeTouchContactCount == 0 ||
			!cursorTraceEnabled_.load(std::memory_order_acquire)) return;
		std::lock_guard<std::mutex> lock(cursorTraceMutex_);
		char line[640] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[CURSOR_TRACE][touch-mouse] message=%s(0x%04x) messageTime=%u "
			"touchBarrier=%u barrierKnown=%u extraInfo=0x%llx promoted=%u "
			"x=%d y=%d activeTouchContacts=%u accepted=%u reason=%s\r\n",
			MouseMessageName(message), static_cast<unsigned>(message), messageTick,
			touchBarrierTick, touchBarrierKnown ? 1u : 0u,
			static_cast<unsigned long long>(extraInfo),
			promotedPointerMessage ? 1u : 0u, x, y, activeTouchContactCount,
			accepted ? 1u : 0u, reason ? reason : "unknown");
		if (length > 0)
		{
			DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, line, (std::min)(
				static_cast<size_t>(length), sizeof(line) - 1));
			DiagnosticOutput::Wake();
		}
	}
#endif

	void WindowController::ApplyWindowCursor(const char* trigger, bool forceDiagnostic) noexcept
	{
		POINT cursorPosition = {};
		const HWND window = window_.load(std::memory_order_acquire);
		if (!window || !GetCursorPos(&cursorPosition)) return;
		const HWND cursorWindow = WindowFromPoint(cursorPosition);
		const bool diagnosticsEnabled = CursorDiagnosticsEnabled();
		const uint64_t diagnosticTick = diagnosticsEnabled ? GetTickCount64() : 0;
		const bool observationDue = diagnosticsEnabled && (forceDiagnostic ||
			!cursorSystemDiagnosticKnown_ || lastCursorSystemDiagnosticHit_ != cursorWindow ||
			diagnosticTick - lastCursorSystemDiagnosticTick_ >= 1000);
		if (cursorWindow != window && (!cursorWindow || !IsChild(window, cursorWindow)))
		{
			if (diagnosticsEnabled && (observationDue || lastCursorSystemDiagnosticApplied_))
			{
				cursorSystemDiagnosticKnown_ = true;
				lastCursorSystemDiagnosticHit_ = cursorWindow;
				lastCursorSystemDiagnosticTick_ = diagnosticTick;
				lastCursorSystemDiagnosticApplied_ = false;
				RecordCursorDiagnostic("system-skip trigger=%s cursor=(%ld,%ld) target=%p hit=%p",
					trigger ? trigger : "unknown", cursorPosition.x, cursorPosition.y,
					static_cast<void*>(window), static_cast<void*>(cursorWindow));
				const auto snapshot = ReadSystemCursorSnapshot();
				RecordSystemCursorSnapshot(trigger, window, cursorWindow, false, snapshot, snapshot);
			}
			return; // 私有刷新消息不能改变其他窗口当前拥有的系统光标。
		}
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		const DrawingTool tool = EffectiveDrawingCursorTool();
		const DrawingCursorPointerAuthority authority = CursorOwner();
		const bool selectionMode = SelectionMode();
		// 白板移动将 Pen/Mouse 交还系统默认光标，Touch 仍由共享策略保持隐藏。
		const bool mouseUsesSystemCursor = selectionMode ||
			mouseUsesSystemCursor_.load(std::memory_order_acquire);
		const bool touchPanActive = touchPanActive_.load(std::memory_order_acquire);
		const bool realMouseTakeover = realMouseTakeoverDuringTouchPan_.load(
			std::memory_order_acquire);
		const bool hide = ShouldHideSystemDrawingCursor(
			authority,
			!selectionMode && tool == DrawingTool::Eraser,
			!selectionMode && tool == DrawingTool::Laser,
			penSample.valid, mouseSample.valid,
			mouseUsesSystemCursor, touchPanActive, realMouseTakeover,
			injectedMouseAsTablet_.load(std::memory_order_acquire) &&
				mouseSample.source.kind == SpeedEraser::SourceKind::ExternalPen, CanvasPanMode());
		uint64_t key = static_cast<uint64_t>(authority);
		key = key * 17u + static_cast<uint64_t>(tool);
		key = key * 17u + (hide ? 1u : 0u);
		key = key * 17u + (penSample.valid ? 1u : 0u);
		key = key * 17u + (mouseSample.valid ? 1u : 0u);
		key = key * 17u + (touchPanActive ? 1u : 0u);
		key = key * 17u + (realMouseTakeover ? 1u : 0u);
		key = key * 17u + (selectionMode ? 1u : 0u);
		// 仅决策/生命周期变化或每秒一次采样，不为每个鼠标包查询全局光标状态。
		const bool observe = diagnosticsEnabled && (observationDue ||
			!lastCursorSystemDiagnosticApplied_ || lastCursorSystemDiagnosticKey_ != key);
		const auto before = observe ? ReadSystemCursorSnapshot() : SystemCursorSnapshot{};
		SetCursor(hide ? nullptr : defaultCursor_); // 命中本画布时由 owner 线程应用，不使用 ShowCursor 计数。
		if (observe)
		{
			const auto after = ReadSystemCursorSnapshot();
			cursorSystemDiagnosticKnown_ = true;
			lastCursorSystemDiagnosticKey_ = key;
			lastCursorSystemDiagnosticHit_ = cursorWindow;
			lastCursorSystemDiagnosticTick_ = diagnosticTick;
			lastCursorSystemDiagnosticApplied_ = true;
			RecordCursorDiagnostic("system trigger=%s hidden=%u tool=%u owner=%u persistent=%u touchSuppressed=%u penValid=%u mouseValid=%u pan=%u mouseTakeover=%u selection=%u cursor=(%ld,%ld)",
				trigger ? trigger : "unknown", hide ? 1u : 0u,
				static_cast<unsigned>(tool), static_cast<unsigned>(authority),
				static_cast<unsigned>(cursorOwner_.load(std::memory_order_acquire)),
				touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u,
				penSample.valid ? 1u : 0u, mouseSample.valid ? 1u : 0u,
				touchPanActive ? 1u : 0u, realMouseTakeover ? 1u : 0u,
				selectionMode ? 1u : 0u, cursorPosition.x, cursorPosition.y);
			RecordSystemCursorSnapshot(trigger, window, cursorWindow, true, before, after);
		}
#if defined(DRAW3_RTS_DIAGNOSTICS)
		if (cursorTraceEnabled_.load(std::memory_order_acquire))
		{
			DrawingCursorDiagnosticVisualState appVisual;
			const bool appVisualKnown = ReadDrawingCursorDiagnosticVisualState(appVisual);
			uint64_t decisionKey = static_cast<uint64_t>(authority);
			decisionKey = decisionKey * 17u + static_cast<uint64_t>(tool);
			decisionKey = decisionKey * 17u + (penSample.valid ? 1u : 0u);
			decisionKey = decisionKey * 17u + (penSample.inContact ? 1u : 0u);
			decisionKey = decisionKey * 17u + (mouseSample.valid ? 1u : 0u);
			decisionKey = decisionKey * 17u + (mouseSample.inContact ? 1u : 0u);
			decisionKey = decisionKey * 17u + (mouseUsesSystemCursor ? 1u : 0u);
			decisionKey = decisionKey * 17u + (touchPanActive ? 1u : 0u);
			decisionKey = decisionKey * 17u + (realMouseTakeover ? 1u : 0u);
			const auto hapticState = hapticPointers_.Inspect();
			decisionKey = decisionKey * 17u + hapticState.pending.pointerId;
			decisionKey = decisionKey * 17u + (hapticState.pending.pointerId ? 1u : 0u);
			decisionKey = decisionKey * 17u + (hapticState.leave ? 1u : 0u);
			decisionKey = decisionKey * 17u + (hide ? 1u : 0u);
			decisionKey = decisionKey * 17u + (appVisualKnown ? 1u : 0u);
			decisionKey = decisionKey * 17u + (appVisual.visible ? 1u : 0u);
			decisionKey = decisionKey * 17u + static_cast<uint64_t>(appVisual.reason);
			std::lock_guard<std::mutex> lock(cursorTraceMutex_);
			if (!lastSystemCursorDecisionKnown_ ||
				lastSystemCursorDecisionKey_ != decisionKey)
			{
				lastSystemCursorDecisionKnown_ = true;
				lastSystemCursorDecisionKey_ = decisionKey;
				char line[768] = {};
				const int length = std::snprintf(line, sizeof(line),
					"[CURSOR_TRACE][system] trigger=%s pointerId=%u pointerEventType=%s(%u) "
					"cursorOwner=%u touchPanActive=%u realMouseTakeover=%u "
					"suppression=%u suppressedPointerId=%u "
					"haptic={pointerId=%u,pending=%u,leave=%u} "
					"penSample={valid=%u,contact=%u,inverted=%u} "
					"mouseSample={valid=%u,contact=%u} appCursor=%s "
					"appReason=%s ShouldHideSystemDrawingCursor=%u "
					"systemCursor=%s reason=%s\r\n",
					trigger ? trigger : "unknown",
					lastCursorTracePointerId_.load(std::memory_order_acquire),
					PointerTypeName(static_cast<POINTER_INPUT_TYPE>(
						lastCursorTracePointerType_.load(std::memory_order_acquire)),
						lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire)),
					lastCursorTracePointerType_.load(std::memory_order_acquire),
					static_cast<unsigned>(authority),
					touchPanActive ? 1u : 0u, realMouseTakeover ? 1u : 0u,
					PenContactSuppressedForTouchPan() ? 1u : 0u,
					suppressedPenPointerId_.load(std::memory_order_acquire),
					hapticState.pending.pointerId,
					hapticState.pending.pointerId ? 1u : 0u,
					hapticState.leave ? 1u : 0u,
					penSample.valid ? 1u : 0u, penSample.inContact ? 1u : 0u,
					penSample.inverted ? 1u : 0u,
					mouseSample.valid ? 1u : 0u, mouseSample.inContact ? 1u : 0u,
					appVisualKnown ? appVisual.visible ? "visible" : "hidden" : "unknown",
					appVisualKnown ? DrawingCursorDiagnosticVisualReasonName(
						appVisual.reason) : "unknown",
					hide ? 1u : 0u, hide ? "hidden" : "arrow",
					SystemCursorDecisionReason(authority, tool, penSample.valid,
						mouseSample.valid, mouseUsesSystemCursor, touchPanActive,
						realMouseTakeover, hide));
				if (length > 0)
				{
					DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, line, (std::min)(
						static_cast<size_t>(length), sizeof(line) - 1));
					DiagnosticOutput::Wake();
				}
			}
		}
#else
		(void)trigger;
#endif
	}

	bool WindowController::ConsumeHapticPointer(Haptics::CanvasPointerRequest& request)
	{
		return hapticPointers_.Take(request);
	}

	bool WindowController::ConsumeHapticPointerLeave()
	{
		return hapticPointers_.TakeLeave();
	}

	void WindowController::RequestHapticPointerLeave() noexcept
	{
		ResetHapticPointer();
	}

	bool WindowController::ExitRequested() const
	{
		return exitRequested_.load(std::memory_order_acquire);
	}

	LRESULT WindowController::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == Detail::kMouseInputOwnerMessage)
		{
			if (lParam != reinterpret_cast<LPARAM>(this) ||
				(wParam != Detail::kMouseInputStart && wParam != Detail::kMouseInputStop)) return 0;
			bool succeeded = false;
			if (mouseDispatchDepth_ == 0 && wParam == Detail::kMouseInputStart)
			{
				const bool pointerRequested = pointerStartRequested_.load(std::memory_order_acquire);
				if (inputCoordinator_.load(std::memory_order_acquire) &&
					mouseStartAllowed_.load(std::memory_order_acquire) &&
					!mouseCompatibilitySelected_.load(std::memory_order_acquire) &&
					(!pointerRequested || DrawingPointerApi().notifications(window, FALSE)))
				{
					const std::uint32_t held = ((GetKeyState(VK_LBUTTON) & 0x8000) ? decltype(mouseInput_)::Left : 0u) |
						((GetKeyState(VK_RBUTTON) & 0x8000) ? decltype(mouseInput_)::Right : 0u);
					pointerInputSelected_ = pointerRequested;
					mouseStartTick_ = GetTickCount();
					mouseInput_.Start(held);
					mouseOwnerStopped_.store(false, std::memory_order_release);
					mouseCompatibilitySelected_.store(true, std::memory_order_release);
					succeeded = true;
				}
			}
			else if (mouseDispatchDepth_ == 0 && wParam == Detail::kMouseInputStop)
			{
				mouseInput_.CloseAdmission();
				if (pointerInputSelected_)
				{
					pointerInputSelected_ = false;
					pointerPenRangeId_ = 0;
					CancelPointerContacts();
					pointerDevices_.clear();
					RequestHapticPointerLeave();
				}
				CancelMouseInput(window);
				if (GetCapture() != window)
				{
					mouseCompatibilitySelected_.store(false, std::memory_order_release);
					mouseOwnerStopped_.store(true, std::memory_order_release);
					succeeded = true;
				}
			}
			// start/stop 独立事件，超时的旧 start 不能冒充后来的 stop 确认。
			const auto operationIndex = static_cast<std::size_t>(wParam);
			mouseOwnerControlSucceeded_[operationIndex].store(succeeded, std::memory_order_release);
			if (mouseOwnerControlEvents_[operationIndex]) SetEvent(mouseOwnerControlEvents_[operationIndex]);
			return succeeded ? 1 : 0;
		}
		struct TouchpadDispatch
		{
			WindowController& self; HWND window;
			TouchpadDispatch(WindowController& controller, HWND hwnd) : self(controller), window(hwnd)
			{ ++self.touchpadDispatchDepth_; }
			~TouchpadDispatch() { self.FinishTouchpadNavigationDispatch(window); }
		};
		if (message == kStartTouchpadNavigationMessage)
		{
			if (lParam != reinterpret_cast<LPARAM>(this) || wParam != touchpadNavigationIdentity_.load() ||
				touchpadDispatchDepth_ || !touchpadNavigationAllowed_.load() ||
				touchpadNavigationOwner_ || touchpadInteractionOwner_) return 0;
			TouchpadDispatch dispatch(*this, window);
			auto* input = inputCoordinator_.load(std::memory_order_acquire);
			if (!input) return 0;
			const auto entry = touchpadEntry_.load();
			auto result = TouchpadAvailability::InitFailed;
			try
			{
				if (entry == TouchpadEntry::DirectManipulation)
				{
					auto owner = std::make_shared<TouchpadNavigationOwner>(touchpadNavigationBridge_, input);
					const bool ready = owner->Initialize(window, pointerInputSelected_);
					if (ready || owner->NeedsCleanup()) touchpadNavigationOwner_ = std::move(owner);
					if (ready) result = TouchpadAvailability::Ok;
				}
				else if (entry == TouchpadEntry::WindowRegistration && !pointerInputSelected_)
				{
					auto owner = std::make_shared<TouchpadInteractionOwner>(*touchpadNavigationBridge_, *input);
					result = owner->Initialize(window);
					if (result == TouchpadAvailability::Ok || !owner->Shutdown())
						touchpadInteractionOwner_ = std::move(owner);
				}
			}
			catch (const std::bad_alloc&) { result = TouchpadAvailability::InitFailed; }
			LogCanvasPan("touchpad-init entry=%u result=%u", static_cast<unsigned>(entry), static_cast<unsigned>(result));
			return static_cast<LRESULT>(result);
		}
		if (message == kTouchpadNavigationMessage)
		{
			if (lParam != reinterpret_cast<LPARAM>(this) ||
				(wParam >> 1u) != touchpadNavigationIdentity_.load(std::memory_order_acquire)) return 0;
			const bool stop = (wParam & 1u) != 0;
			if (touchpadDispatchDepth_)
			{
				// DM 的 COM 调用可泵消息；确认必须等外层调用退栈，不能在嵌套 Stop 中释放自己。
				touchpadDeferredOperation_ = (std::max)(touchpadDeferredOperation_, stop ? 2u : 1u);
				return 0;
			}
			touchpadNavigationPosted_.store(false, std::memory_order_release);
			TouchpadDispatch dispatch(*this, window);
			if (stop || !touchpadNavigationAllowed_.load(std::memory_order_acquire))
			{
				const bool succeeded = (!touchpadNavigationOwner_ || touchpadNavigationOwner_->Shutdown()) &&
					(!touchpadInteractionOwner_ || touchpadInteractionOwner_->Shutdown());
				if (succeeded) { touchpadNavigationOwner_.reset(); touchpadInteractionOwner_.reset(); }
				touchpadStopSucceeded_ = succeeded;
				touchpadStopCompletionPending_ = true;
				return succeeded ? 1 : 0;
			}
			if (touchpadInteractionOwner_) touchpadInteractionOwner_->Update();
			if (auto owner = touchpadNavigationOwner_) owner->Update();
			return 1;
		}
		if (message == DM_POINTERHITTEST)
		{
			POINTER_INPUT_TYPE type = PT_POINTER;
			const auto getType = ResolveGetPointerType();
			const UINT32 pointer = GET_POINTERID_WPARAM(wParam);
			const auto* input = inputCoordinator_.load(std::memory_order_acquire);
			const bool admitted = pointerInputSelected_ && touchpadNavigationOwner_ && !touchpadDispatchDepth_ && CanvasPanMode() &&
				touchpadNavigationAllowed_.load(std::memory_order_acquire) &&
				touchpadNavigationEnabled_.load(std::memory_order_acquire) && input && !input->AdmissionBlocked();
			const bool typeKnown = admitted && getType && getType(pointer, &type);
			const bool accepted = Detail::DispatchTouchpadHitTest(admitted, typeKnown, type, pointer, [&](UINT32 id)
			{
				TouchpadDispatch dispatch(*this, window);
				const auto owner = touchpadNavigationOwner_;
				const bool associated = owner && owner->Contact(id);
				if (associated) NotifyCanvasContactDown();
				return associated;
			});
			LogCanvasPan("touchpad-hit-test pointer=%u type=%u admitted=%u accepted=%u reason=%s",
				pointer, static_cast<unsigned>(type), admitted ? 1u : 0u, accepted ? 1u : 0u,
				accepted ? "touchpad-associated" : !admitted ? "gate-closed" : !typeKnown ? "type-unknown" :
				type != PT_TOUCHPAD ? "other-input-preserved" : "association-failed");
			return 0; // 不允许非触控板经默认处理自动进入正在运行的 DM viewport。
		}
		const bool deviceChanged = (message == WM_POINTERDEVICECHANGE && wParam != PDC_ARRIVAL) ||
			(message == WM_DEVICECHANGE && (wParam == DBT_DEVNODES_CHANGED || wParam == DBT_DEVICEREMOVECOMPLETE));
		if (deviceChanged && (touchpadInteractionOwner_ || touchpadNavigationOwner_))
		{
			CancelTouchpadNavigation();
			if (touchpadInteractionOwner_ && !touchpadDispatchDepth_)
			{
				TouchpadDispatch dispatch(*this, window);
				touchpadInteractionOwner_->DeviceChanged();
			}
		}
		if (touchpadInteractionOwner_ && !touchpadDispatchDepth_ &&
			(message == WM_TIMER || message == Detail::kStopContentInertia || message == Detail::kEndContentInertia ||
				message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP ||
				message == WM_POINTERENTER || message == WM_POINTERLEAVE || message == WM_POINTERCAPTURECHANGED))
		{
			TouchpadDispatch dispatch(*this, window);
			if (const auto handled = touchpadInteractionOwner_->Message(message, wParam, CanvasPanMode()))
				return *handled ? 0 : DefWindowProcW(window, message, wParam, lParam);
		}
		if (pointerInputSelected_)
		{
			if (message == WM_POINTERDEVICECHANGE)
			{
				// 设备移除或映射变化会使已锁存的物理矩形失效。无设备句柄时全部失效。
				if (wParam != PDC_ARRIVAL)
				{
					const HANDLE device = reinterpret_cast<HANDLE>(lParam);
					const bool known = std::any_of(pointerDevices_.begin(), pointerDevices_.end(),
						[device](const auto& item) { return item.handle == device; });
					CancelPointerContacts(known ? device : nullptr);
					std::erase_if(pointerDevices_, [&](const auto& item) { return !known || item.handle == device; });
				}
				return 0;
			}
			if (message == WM_POINTERCAPTURECHANGED ||
				((message == WM_POINTERLEAVE || message == WM_POINTERENTER) && IS_POINTER_CANCELED_WPARAM(wParam)))
			{
				const auto id = GET_POINTERID_WPARAM(wParam);
				auto it = std::find_if(pointerContacts_.begin(), pointerContacts_.end(),
					[id](const auto& item) { return item.pointerId == id; });
				if (it != pointerContacts_.end())
				{
					LARGE_INTEGER now{};
					QueryPerformanceCounter(&now);
					FinishPointerContact(it - pointerContacts_.begin(), ContactPhase::Cancelled, now.QuadPart);
					return 0;
				}
				if (message == WM_POINTERCAPTURECHANGED)
				{
					if (pointerPenRangeId_ != id) return 0;
					pointerPenRangeId_ = 0;
				}
			}
			if (message == WM_CANCELMODE || message == WM_DESTROY || message == WM_CLOSE)
				CancelPointerContacts();
			if (message == WM_DESTROY)
			{
				// 下方已有的 DM 销毁分支会提前返回，先在 owner 确认两条输入均已停止。
				pointerInputSelected_ = false;
				pointerPenRangeId_ = 0;
				pointerDevices_.clear();
				RequestHapticPointerLeave();
				mouseInput_.CloseAdmission();
				CancelMouseInput(window);
				mouseCompatibilitySelected_.store(false, std::memory_order_release);
				if (!mouseDispatchDepth_ && GetCapture() != window)
					mouseOwnerStopped_.store(true, std::memory_order_release);
			}
		}
		// SetContact 造成的 pointer capture 改变是 DM 接管，不是整手势取消。
		if (message == WM_CANCELMODE || message == WM_KILLFOCUS || message == WM_CLOSE || message == WM_DESTROY ||
			message == WM_SIZE || (message == WM_SHOWWINDOW && !wParam) ||
			(message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) ||
			(message == WM_WINDOWPOSCHANGED && lParam &&
				(reinterpret_cast<const WINDOWPOS*>(lParam)->flags & SWP_HIDEWINDOW)))
		{
			CancelTouchpadNavigation();
			if (message == WM_DESTROY)
			{
				touchpadNavigationAllowed_.store(false, std::memory_order_release);
				if (touchpadDispatchDepth_) touchpadDeferredOperation_ = 2;
				else HandleExternalMessage(window, kTouchpadNavigationMessage,
					(touchpadNavigationIdentity_.load() << 1u) | 1u, reinterpret_cast<LPARAM>(this));
				return DefWindowProcW(window, message, wParam, lParam); // Stop 确认后只使用局部参数。
			}
		}
		const bool compatibility = mouseCompatibilitySelected_.load(std::memory_order_acquire);
		if (compatibility)
		{
			// Pointer 整个生命周期交默认处理，系统提供的单点鼠标提升只走下面一条 producer。
			if (!pointerInputSelected_ && message >= WM_NCPOINTERUPDATE && message <= WM_POINTERROUTEDRELEASED)
				return DefWindowProcW(window, message, wParam, lParam);
			if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
				message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MOUSEMOVE)
			{
				struct DispatchScope
				{
					unsigned& depth;
					explicit DispatchScope(unsigned& value) noexcept : depth(value) { ++depth; }
					~DispatchScope() { --depth; }
				} dispatch(mouseDispatchDepth_);
				if (pointerInputSelected_)
				{
					// Pointer 已直接消费笔/触摸；只按提升签名和消息来源剔除残余提升消息。
					// 不用触摸时间屏障：同 tick 的真实鼠标 Up 被吞会留下按键位和捕获。
					const auto source = CurrentInputMessageSource().deviceType;
					if (IsPromotedPointerMouseMessage(GetMessageExtraInfo()) ||
						source == IMDT_TOUCH || source == IMDT_PEN) return 0;
				}
				HandleCompatibilityMouse(window, message, wParam, lParam);
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_KILLFOCUS ||
				(message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(lParam) != window) ||
				(message == WM_SHOWWINDOW && !wParam) ||
				(message == WM_WINDOWPOSCHANGED && lParam &&
					(reinterpret_cast<const WINDOWPOS*>(lParam)->flags & SWP_HIDEWINDOW)) ||
				(message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) ||
				message == WM_DESTROY || message == WM_CLOSE)
				CancelMouseInput(window, message == WM_CAPTURECHANGED);
			if (message == WM_DESTROY)
			{
				mouseInput_.CloseAdmission();
				mouseCompatibilitySelected_.store(false, std::memory_order_release);
			}
		}
		const bool gpuTransparent = gpuTransparentComposition_.load(std::memory_order_acquire); // GPU 透明模式下由 backbuffer 负责背景。
		switch (message)
		{
		case WM_CLOSE:
			// 外部 HWND 由 Window Service 销毁；Draw3 只请求绘制线程退出。
			exitRequested_.store(true, std::memory_order_release);
			RequestControlWake();
			return 0;

		case kApplySystemCursorMessage:
			systemCursorRefreshPosted_.store(false, std::memory_order_release);
			ApplyWindowCursor("posted-refresh", true);
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				ApplyWindowCursor("WM_SETCURSOR");
				return TRUE;
			}
			break;

		case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
			// 与首触前的 property 使用同一策略，关闭系统触摸反馈并保留 RTS 多点。
			return static_cast<LRESULT>(kTabletInputFlags);

		case WM_POINTERCAPTURECHANGED:
		case WM_CANCELMODE:
			RequestHapticPointerLeave();
			RequestControlWake();
			if (HapticDiagnostics::Enabled())
			{
				hapticIngressDiagnostics_ = {};
				HapticDiagnostics::Log("canvas_ingress", "message=%u reason=cancel_mode revision=%llu omitted_updates=%llu",
					message, static_cast<unsigned long long>(HapticPointerRevision()),
					static_cast<unsigned long long>(hapticIngressUpdatesOmitted_));
				hapticIngressUpdatesOmitted_ = 0;
			}
			// Pointer 模式下笔/触摸序列全程由本窗口消费，不交默认处理。
			if (pointerInputSelected_ && message == WM_POINTERCAPTURECHANGED) return 0;
			break;

		case WM_POINTERLEAVE:
		{
			const uint32_t pointerId = static_cast<uint32_t>(LOWORD(wParam));
			const PointerDetails details = QueryPointerDetails(pointerId);
			if (pointerInputSelected_)
			{
				if (details.cancelled)
				{
					LARGE_INTEGER now{};
					QueryPerformanceCounter(&now);
					HandlePointerContact(window, message, pointerId, true, nullptr, nullptr, 0, now.QuadPart);
				}
				if (pointerPenRangeId_ != pointerId)
				{
					// 类型查询失败时仍以本窗口登记的绑定为准，已接管的序列不能落入默认处理。
					const bool tracked = std::any_of(pointerContacts_.begin(), pointerContacts_.end(),
						[pointerId](const auto& contact) { return contact.pointerId == pointerId; });
					if (tracked || details.type == PT_TOUCH || details.type == PT_PEN) return 0;
					break;
				}
				pointerPenRangeId_ = 0;
			}
#if defined(DRAW3_RTS_DIAGNOSTICS)
			lastCursorTracePointerId_.store(pointerId, std::memory_order_release);
			lastCursorTracePointerType_.store(
				static_cast<uint32_t>(details.type), std::memory_order_release);
			lastCursorTracePointerTypeKnown_.store(
				details.typeKnown, std::memory_order_release);
#endif
			const DrawingCursorPointerAuthority previousAuthority =
				cursorOwner_.load(std::memory_order_acquire);
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
			const bool suppressedPenPointer =
				suppressedPenPointerId_.load(std::memory_order_acquire) == pointerId;
			const DrawingCursorPointerAuthority pointerEventType = details.typeKnown
				? AuthorityForPointerType(details.type)
				: suppressedPenPointer ||
					previousAuthority == DrawingCursorPointerAuthority::Pen || penSampleValid
					? DrawingCursorPointerAuthority::Pen : previousAuthority;
			const bool penPointer = pointerEventType == DrawingCursorPointerAuthority::Pen;
			if (penPointer) ClearPenCursorSample();
			if (penPointer &&
				(suppressedPenPointerId_.load(std::memory_order_acquire) == 0 ||
					suppressedPenPointerId_.load(std::memory_order_relaxed) == pointerId))
			{
				suppressedPenPointerId_.store(0, std::memory_order_release);
				SetPenContactSuppressedForTouchPan(false);
			}
			if (penPointer)
				penCompatibilityMouseContactSuppressed_.store(false,
					std::memory_order_release);
			SetDrawingCursorOwner(ResolveDrawingCursorOwnerForPointerEvent(
				previousAuthority, pointerEventType,
				touchPanActive_.load(std::memory_order_acquire),
				realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire)));
			if (penPointer)
			{
				RequestHapticPointerLeave();
				RequestControlWake();
			}
			if (HapticDiagnostics::Enabled())
			{
				for (auto& diagnostic : hapticIngressDiagnostics_)
					if (diagnostic.known && diagnostic.pointerId == pointerId) diagnostic.known = false;
				HapticDiagnostics::Log("canvas_ingress", "message=%u pointer=%u type=%u known=%d reason=pointer_leave pen=%d revision=%llu time=%ld omitted_updates=%llu",
					message, pointerId, static_cast<unsigned>(details.type), details.typeKnown, penPointer,
					static_cast<unsigned long long>(HapticPointerRevision()), GetMessageTime(),
					static_cast<unsigned long long>(hapticIngressUpdatesOmitted_));
				hapticIngressUpdatesOmitted_ = 0;
			}
			RecordCursorDiagnostic("pointer-leave id=%u type=%u known=%u owner=%u suppressed=%u penValid=%u",
				pointerId, static_cast<unsigned>(details.type), details.typeKnown ? 1u : 0u,
				static_cast<unsigned>(CursorOwner()),
				touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u,
				penSampleValid ? 1u : 0u);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			TraceCursorState("WM_POINTERLEAVE", pointerId, details.type,
				details.typeKnown, true);
#endif
			if (pointerInputSelected_) return 0;
			break;
		}
		case WM_POINTERENTER:
		case WM_POINTERUPDATE:
		case WM_POINTERDOWN:
		case WM_POINTERUP:
		{
			// 与鼠标共用 owner 停止屏障；通知 UI 时即使重入也不能在外层退栈前确认停产。
			struct PointerDispatchScope
			{
				unsigned* depth;
				~PointerDispatchScope() { if (depth) --*depth; }
			} dispatch{ pointerInputSelected_ ? &mouseDispatchDepth_ : nullptr };
			if (dispatch.depth) ++*dispatch.depth;
			LARGE_INTEGER arrival{};
			POINTER_PEN_INFO penInfo{};
			POINTER_TOUCH_INFO touchInfo{};
			bool touchKnown = false;
			if (pointerInputSelected_) QueryPerformanceCounter(&arrival);
			const uint32_t pointerId = static_cast<uint32_t>(LOWORD(wParam));
			if (!pointerInputSelected_ && message == WM_POINTERDOWN) NotifyCanvasContactDown();
			const PointerDetails details = QueryPointerDetails(pointerId, pointerInputSelected_ ? &penInfo : nullptr);
			if (pointerInputSelected_ && details.type == PT_TOUCH)
				touchKnown = DrawingPointerApi().touch(pointerId, &touchInfo) &&
					touchInfo.pointerInfo.pointerType == PT_TOUCH;
			if (pointerInputSelected_ && message == WM_POINTERDOWN &&
				(details.type == PT_PEN || details.type == PT_TOUCH)) NotifyCanvasContactDown();
#if defined(DRAW3_RTS_DIAGNOSTICS)
			lastCursorTracePointerId_.store(pointerId, std::memory_order_release);
			lastCursorTracePointerType_.store(
				static_cast<uint32_t>(details.type), std::memory_order_release);
			lastCursorTracePointerTypeKnown_.store(
				details.typeKnown, std::memory_order_release);
#endif
			const DrawingCursorPointerAuthority previousAuthority =
				cursorOwner_.load(std::memory_order_acquire);
			const DrawingCursorPointerAuthority pointerEventType = details.typeKnown
				? AuthorityForPointerType(details.type) : DrawingCursorPointerAuthority::Unknown;
			if (pointerEventType == DrawingCursorPointerAuthority::Touch &&
				details.positionKnown)
			{
				POINT touchClientPosition = details.screenPosition;
				if (ScreenToClient(window, &touchClientPosition))
				{
					lastTouchMouseX_ = touchClientPosition.x;
					lastTouchMouseY_ = touchClientPosition.y;
					lastTouchMousePositionKnown_ = true;
					lastTouchMouseDiagnosticTick_ = static_cast<uint32_t>(GetMessageTime());
					lastTouchMouseDiagnosticPointerId_ = pointerId;
					lastTouchMouseDiagnosticPrimary_ = details.primaryKnown
						? (details.primary ? 2u : 1u) : 0u;
					lastTouchMouseDiagnosticFromPointer_ = true;
					RecordCursorDiagnostic("touch-position via=pointer id=%u primary=%u tick=%u x=%ld y=%ld",
						pointerId, lastTouchMouseDiagnosticPrimary_, lastTouchMouseDiagnosticTick_,
						touchClientPosition.x, touchClientPosition.y);
				}
			}
			if (!pointerInputSelected_ && ShouldClearMouseCursorSampleForPointerEvent(
				pointerEventType, message == WM_POINTERDOWN))
			{
				// Pointer 仅作 message-time barrier fallback；活跃计数由 RTS 权威生命周期维护。
				NotifyTouchContactBegin(false, static_cast<uint32_t>(GetMessageTime()));
			}
			const bool penPointer = pointerEventType == DrawingCursorPointerAuthority::Pen ||
				suppressedPenPointerId_.load(std::memory_order_acquire) == pointerId;
			const bool penInContact = details.inContact || message == WM_POINTERDOWN;
			if (details.typeKnown)
			{
				if (pointerEventType == DrawingCursorPointerAuthority::Mouse &&
					touchPanActive_.load(std::memory_order_acquire))
					realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
				if (pointerEventType == DrawingCursorPointerAuthority::Mouse)
					SetTouchCursorSuppressed(false);
				SetDrawingCursorOwner(ResolveDrawingCursorOwnerForPointerEvent(
					previousAuthority, pointerEventType,
					touchPanActive_.load(std::memory_order_acquire),
					realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire)));
			}
			if (pointerInputSelected_ && details.type == PT_PEN && message != WM_POINTERUP &&
				!details.cancelled && !IS_POINTER_CANCELED_WPARAM(wParam)) pointerPenRangeId_ = pointerId;
			if (message == WM_POINTERUP && penPointer)
			{
				penCompatibilityMouseContactSuppressed_.store(false,
					std::memory_order_release);
				if (!pointerInputSelected_) ClearPenCursorSample(); // Pointer 绘制按本笔 token 清除。
				if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0 ||
					suppressedPenPointerId_.load(std::memory_order_relaxed) == pointerId)
				{
					suppressedPenPointerId_.store(0, std::memory_order_release);
					SetPointerPenContactSuppressed(false);
				}
			}
			else if (details.type == PT_PEN && details.positionKnown)
			{
				if (ShouldSuppressPenFeedbackForTouchPan(
					touchPanActive_.load(std::memory_order_acquire),
					pointerPenContactSuppressed_.load(std::memory_order_acquire),
					true, penInContact))
				{
					suppressedPenPointerId_.store(pointerId, std::memory_order_release);
					SetPointerPenContactSuppressed(true);
				}
				POINT clientPosition = details.screenPosition;
				if ((!pointerInputSelected_ || !penInContact) && ScreenToClient(window, &clientPosition))
				{
					DrawingCursorSample sample;
					sample.x = static_cast<float>(clientPosition.x);
					sample.y = static_cast<float>(clientPosition.y);
					if (pointerInputSelected_ && details.penInfoKnown)
					{
						sample.source = PointerDeviceFor(penInfo.pointerInfo.sourceDevice, InputDeviceType::Pen).source;
						sample.source.cursorId = details.cursorId;
					}
					sample.valid = true;
					sample.inverted = details.eraserHint;
					sample.inContact = penInContact;
					LARGE_INTEGER qpc = {};
					QueryPerformanceCounter(&qpc);
					sample.qpc = qpc.QuadPart;
					PublishPenCursorSample(sample);
				}
			}
			if (!pointerInputSelected_ && penPointer && details.cancelled)
			{
				RequestHapticPointerLeave();
				RequestControlWake();
			}
			bool hapticObserved = false;
			const bool hapticDiagnostic = HapticDiagnostics::Enabled();
			const char* hapticReason = nullptr;
			// 缓存原准入表达式实际读取的门禁值；-1 表示已在更早条件拒绝。
			int hapticPan = -1, hapticSuppressed = -1;
			if (message != WM_POINTERUP && pointerId != 0 && details.typeKnown &&
				details.type == PT_PEN && details.penInfoKnown && !details.cancelled &&
				!(hapticPan = touchPanActive_.load(std::memory_order_acquire)) &&
				!(hapticSuppressed = pointerPenContactSuppressed_.load(std::memory_order_acquire)))
			{
				// 进入范围时保留 tablet cursor，RTS 普通抬笔不销毁范围身份。
				const auto previousHapticRevision = hapticPointers_.Revision();
				hapticObserved = hapticPointers_.Observe(pointerId, details.eraserHint,
					message == WM_POINTERENTER || message == WM_POINTERDOWN, penInContact, details.cursorId,
					hapticDiagnostic ? &hapticReason : nullptr);
				if (hapticObserved || previousHapticRevision != hapticPointers_.Revision()) RequestControlWake();
			}
			if (hapticDiagnostic)
			{
				if (!hapticReason)
					hapticReason = message == WM_POINTERUP ? "pointer_up"
					: !pointerId ? "zero_pointer" : !details.typeKnown ? "unknown_type"
					: details.type != PT_PEN ? "non_pen" : !details.penInfoKnown ? "no_pen_info"
					: details.cancelled ? "cancelled" : hapticPan > 0 ? "touch_pan" : "pen_suppressed";
				const auto revision = HapticPointerRevision();
				const uint32_t state = static_cast<uint32_t>(details.type) |
					(static_cast<uint32_t>(details.typeKnown) << 4) |
					(static_cast<uint32_t>(details.penInfoKnown) << 5) |
					(static_cast<uint32_t>(penInContact) << 6) |
					(static_cast<uint32_t>(details.cancelled) << 7) |
					(static_cast<uint32_t>(hapticObserved) << 8) |
					(static_cast<uint32_t>(hapticPan + 1) << 9) |
					(static_cast<uint32_t>(hapticSuppressed + 1) << 11);
				auto diagnostic = std::find_if(hapticIngressDiagnostics_.begin(), hapticIngressDiagnostics_.end(),
					[&](const auto& value) { return value.known && value.pointerId == pointerId; });
				if (diagnostic == hapticIngressDiagnostics_.end())
				{
					// 末槽预留真实 Pen；满槽不淘汰旧项，避免多指交错 Update 反复变成首次。
					const auto eligibleEnd = details.typeKnown && details.type == PT_PEN
						? hapticIngressDiagnostics_.end() : hapticIngressDiagnostics_.end() - 1;
					const auto free = std::find_if(hapticIngressDiagnostics_.begin(), eligibleEnd,
						[](const auto& value) { return !value.known; });
					if (free != eligibleEnd) diagnostic = free;
				}
				const bool tracked = diagnostic != hapticIngressDiagnostics_.end();
				if (message == WM_POINTERUPDATE && !tracked)
				{
					if (hapticIngressUpdatesOmitted_ != (std::numeric_limits<uint64_t>::max)())
						++hapticIngressUpdatesOmitted_;
				}
				else
				{
					// 边沿始终可见；省略计数只附到本来就要输出的日志，不额外刷屏。
					if (message != WM_POINTERUPDATE || hapticObserved || !diagnostic->known ||
						diagnostic->state != state || diagnostic->revision != revision ||
						std::strcmp(diagnostic->reason, hapticReason) != 0)
					{
						HapticDiagnostics::Log("canvas_ingress",
							"message=%u pointer=%u type=%u known=%d pen_info=%d contact=%d cancelled=%d pan=%d suppressed=%d observed=%d reason=%s revision=%llu time=%ld position_known=%d point=%ld,%ld omitted_updates=%llu",
							message, pointerId, static_cast<unsigned>(details.type), details.typeKnown,
							details.penInfoKnown, penInContact, details.cancelled, hapticPan, hapticSuppressed,
							hapticObserved, hapticReason, static_cast<unsigned long long>(revision),
							GetMessageTime(), details.positionKnown, details.screenPosition.x, details.screenPosition.y,
							static_cast<unsigned long long>(hapticIngressUpdatesOmitted_));
						hapticIngressUpdatesOmitted_ = 0;
					}
					if (tracked) *diagnostic = { message != WM_POINTERUP, pointerId, state, revision, hapticReason };
				}
			}
			if (CursorDiagnosticsEnabled())
				RecordCursorDiagnostic("pointer message=0x%04x id=%u type=%u known=%u positionKnown=%u x=%ld y=%ld penContact=%u eraser=%u owner=%u persistent=%u suppressed=%u touchCount=%u mouseTakeover=%u",
					message, pointerId, static_cast<unsigned>(details.type),
					details.typeKnown ? 1u : 0u, details.positionKnown ? 1u : 0u,
					details.screenPosition.x, details.screenPosition.y,
					penInContact ? 1u : 0u, details.eraserHint ? 1u : 0u,
					static_cast<unsigned>(CursorOwner()),
					static_cast<unsigned>(cursorOwner_.load(std::memory_order_acquire)),
					touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u,
					activeTouchContactCount_.load(std::memory_order_acquire),
					realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire) ? 1u : 0u);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const char* pointerEventName = message == WM_POINTERDOWN
				? "WM_POINTERDOWN" : message == WM_POINTERUP
				? "WM_POINTERUP" : message == WM_POINTERENTER
				? "WM_POINTERENTER" : "WM_POINTERUPDATE";
			TraceCursorState(pointerEventName, pointerId, details.type,
				details.typeKnown,
				message == WM_POINTERDOWN || message == WM_POINTERUP);
#endif
			if (pointerInputSelected_ && (message != WM_POINTERENTER || details.cancelled))
			{
				const bool consumed = HandlePointerContact(window, message, pointerId,
					details.cancelled || IS_POINTER_CANCELED_WPARAM(wParam),
					details.penInfoKnown ? &penInfo : nullptr, touchKnown ? &touchInfo : nullptr,
					details.cursorId, arrival.QuadPart);
				if (message == WM_POINTERDOWN && details.type == PT_TOUCH &&
					std::none_of(pointerContacts_.begin(), pointerContacts_.end(),
						[pointerId](const auto& contact) { return contact.pointerId == pointerId; }))
					NotifyTouchContactBegin(false, static_cast<DWORD>(GetMessageTime()));
				// 已识别但读取失败的接触也不能交给 DefWindowProc 提升成另一条鼠标笔迹。
				if (consumed) return 0;
			}
			if (pointerInputSelected_ && (details.type == PT_PEN || details.type == PT_TOUCH)) return 0;
			break;
		}

		case WM_DESTROY:
		{
			penCursorSample_.Clear();
			mouseCursorSample_.Clear();
			cursorOwner_.store(
				DrawingCursorPointerAuthority::Unknown, std::memory_order_release);
			touchCursorSuppressed_.store(false, std::memory_order_release);
			touchPanActive_.store(false, std::memory_order_release);
			realMouseTakeoverDuringTouchPan_.store(false, std::memory_order_release);
			penContactSuppressedForTouchPan_.store(false, std::memory_order_release);
			pointerPenContactSuppressed_.store(false, std::memory_order_release);
			penCompatibilityMouseContactSuppressed_.store(false,
				std::memory_order_release);
			suppressedPenPointerId_.store(0, std::memory_order_release);
			// 销毁时也只释放当前仍命中本窗口的光标，避免覆盖下层应用的选择。
			POINT cursorPosition = {};
			if (GetCursorPos(&cursorPosition))
			{
				const HWND cursorWindow = WindowFromPoint(cursorPosition);
				if (cursorWindow == window || (cursorWindow && IsChild(window, cursorWindow)))
					SetCursor(defaultCursor_);
			}
			RestoreTabletInputProperty(window);
			exitRequested_.store(true, std::memory_order_release); // 通知主循环退出。
			RequestControlWake();
			const bool external = externalWindow_.load(std::memory_order_acquire);
			if (compatibility && mouseDispatchDepth_ == 0 && GetCapture() != window)
				mouseOwnerStopped_.store(true, std::memory_order_release);
			if (!external)
			{
				PostQuitMessage(0);
				return 0;
			}
			// Window Service 所在线程还负责其它 overlay HWND，不能被 Draw3
			// 的外部窗口过程投递 WM_QUIT；交回宿主默认处理即可。
			return DefWindowProcW(window, message, wParam, lParam);
		}

		case WM_DWMCOLORIZATIONCOLORCHANGED:
		case WM_DWMCOMPOSITIONCHANGED:
			compositionChangedRequested_.store(true, std::memory_order_release); // DWM 状态变化交给主循环刷新 presenter。
			RequestControlWake();
			return 0;

		case WM_MOUSEACTIVATE:
			// 只由 Enter/Exit 事务切换，避免每帧抢焦点或在 Presentation 中激活。
			return activationAllowed_.load(std::memory_order_acquire)
				? MA_ACTIVATE : MA_NOACTIVATE;

		case WM_ERASEBKGND:
			if (gpuTransparent) return 1; // 阻止 GDI 擦背景，避免透明区域闪烁。
			break;

		case WM_PAINT:
			if (gpuTransparent)
			{
				// 新暴露区域由主绘制线程重新提交整张画布。
				RequestFullPresent();
				ValidateRect(window, nullptr);
				return 0;
			}
			break;

		case WM_SHOWWINDOW:
			hapticSurfaceVisible_.store(wParam != 0, std::memory_order_release);
			if (!wParam) RequestHapticPointerLeave();
			RequestControlWake();
			QueueSystemCursorRefresh(); // 主画布重现时静止指针也须恢复当前工具策略。
			[[fallthrough]];
		case WM_ACTIVATE:
			if (gpuTransparent) RequestFullPresent(); // 窗口可见性变化后重新提交完整画布。
			break;

		case WM_WINDOWPOSCHANGED:
		{
			const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
			if (position)
			{
				if (position->flags & (SWP_HIDEWINDOW | SWP_SHOWWINDOW))
				{
					hapticSurfaceVisible_.store((position->flags & SWP_HIDEWINDOW) == 0, std::memory_order_release);
					if (position->flags & SWP_HIDEWINDOW) RequestHapticPointerLeave();
					RequestControlWake();
				}
			}
			if (gpuTransparent)
			{
				if (!position || !(position->flags & SWP_NOMOVE) || !(position->flags & SWP_NOSIZE)) RequestFullPresent(); // 移动或尺寸变化后刷新 DWM 读取内容。
			}
			if (position && (position->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)))
				QueueSystemCursorRefresh();
			break;
		}

		case WM_SIZE:
			if (wParam != SIZE_MINIMIZED)
			{
				const int width = static_cast<int>(LOWORD(lParam));
				const int height = static_cast<int>(HIWORD(lParam));
				if (width > 0 && height > 0)
				{
					// 窗口过程只记录尺寸，D3D 资源仍由主绘制线程重建。
					pendingResizeWidth_.store(width, std::memory_order_relaxed);
					pendingResizeHeight_.store(height, std::memory_order_relaxed);
					resizeRequested_.store(true, std::memory_order_release);
					RequestControlWake();
				}
				if (gpuTransparent) RequestFullPresent();
			}
			break;

		case WM_DPICHANGED:
			// DPI 变化不直接改资源，先请求一帧完整重建。
			compositionChangedRequested_.store(true, std::memory_order_release);
			RequestControlWake();
			return 0;

		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		{
			const bool buttonUp = message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
				message == WM_MBUTTONUP;
			const bool buttonDown = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
				message == WM_MBUTTONDOWN ||
				(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
			const uint32_t messageTick = static_cast<uint32_t>(GetMessageTime());
			const ULONG_PTR extraInfo = GetMessageExtraInfo();
			const bool promotedPointerMessage = IsPromotedPointerMouseMessage(extraInfo);
			const auto sourceSnapshot = CurrentInputMessageSource();
			const INPUT_MESSAGE_DEVICE_TYPE inputSource = sourceSnapshot.deviceType;
			const uint64_t diagnosticEvent = CursorDiagnosticsEnabled()
				? ++cursorMouseDiagnosticEvent_ : 0;
			RecordMouseMessageSource(diagnosticEvent, window, message, wParam, lParam, sourceSnapshot);
			RecordMouseCursorDiagnosticState(diagnosticEvent, "before");
			const uint64_t barrierState = latestTouchInputBarrierTick_.load(
				std::memory_order_acquire);
			const bool touchBarrierKnown =
				(barrierState & kTouchInputBarrierKnownMask) != 0;
			const uint32_t touchBarrierTick = static_cast<uint32_t>(barrierState);
			const int mouseClientX = GET_X_LPARAM(lParam);
			const int mouseClientY = GET_Y_LPARAM(lParam);
			if (inputSource == IMDT_TOUCH ||
				(inputSource == IMDT_UNAVAILABLE && promotedPointerMessage &&
					(extraInfo & 0x80u) != 0))
			{
				// Touch 兼容消息继续更新末点；其低位只辅助记录位置，不决定输入归属。
				lastTouchMouseX_ = mouseClientX;
				lastTouchMouseY_ = mouseClientY;
				lastTouchMousePositionKnown_ = true;
				lastTouchMouseDiagnosticTick_ = messageTick;
				lastTouchMouseDiagnosticPointerId_ = 0;
				lastTouchMouseDiagnosticPrimary_ = 0;
				lastTouchMouseDiagnosticFromPointer_ = false;
			}
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
			const auto recordMouseDecision = [&](bool accepted, const char* reason) noexcept
			{
				if (!CursorDiagnosticsEnabled()) return;
				RecordCursorDiagnostic("mouse event=%llu message=0x%04x accepted=%u reason=%s source=%u promoted=%u extra=0x%llx tick=%u barrier=%u known=%u x=%d y=%d buttonDown=%u buttonUp=%u",
					static_cast<unsigned long long>(diagnosticEvent), message, accepted ? 1u : 0u, reason,
					static_cast<unsigned>(inputSource), promotedPointerMessage ? 1u : 0u,
					static_cast<unsigned long long>(extraInfo), messageTick,
					touchBarrierTick, touchBarrierKnown ? 1u : 0u,
					mouseClientX, mouseClientY, buttonDown ? 1u : 0u,
					buttonUp ? 1u : 0u);
			};
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const uint32_t activeTouchContactCount = activeTouchContactCount_.load(
				std::memory_order_acquire);
			const auto traceMouseDecision = [&](bool accepted, const char* reason) noexcept
			{
				TraceTouchMouseMessage(message, messageTick, touchBarrierTick,
					touchBarrierKnown, extraInfo, promotedPointerMessage,
					mouseClientX, mouseClientY, activeTouchContactCount,
					accepted, reason);
			};
#endif
			const auto filter = FilterMouseCursorMessage({
				.message = message,
				.buttonDown = buttonDown,
				.promotedPointerMessage = promotedPointerMessage,
				.pointerApiAvailable = ResolveGetPointerType() != nullptr,
				.penSampleValid = penSampleValid,
				.touchBarrierKnown = touchBarrierKnown,
				.messageTick = messageTick,
				.touchBarrierTick = touchBarrierTick,
				.inputSource = inputSource,
				.sourceQuerySucceeded = sourceSnapshot.succeeded,
				.origin = sourceSnapshot.originId,
				.touchSuppressed = touchCursorSuppressed_.load(std::memory_order_acquire),
				.touchPositionKnown = lastTouchMousePositionKnown_,
				.touchX = lastTouchMouseX_, .touchY = lastTouchMouseY_,
				.mouseX = mouseClientX, .mouseY = mouseClientY,
				.pointerAuthority = cursorOwner_.load(std::memory_order_acquire)
			});
			if (diagnosticEvent)
				RecordCursorDiagnostic("mouse-filter event=%llu touchKnown=%u touchX=%d touchY=%d touchTick=%u touchId=%u primary=%u via=%s same=%u sourceReject=%u systemReject=%u positionReject=%u buttonBypass=%u",
					static_cast<unsigned long long>(diagnosticEvent), lastTouchMousePositionKnown_ ? 1u : 0u,
					lastTouchMouseX_, lastTouchMouseY_, lastTouchMouseDiagnosticTick_,
					lastTouchMouseDiagnosticPointerId_, lastTouchMouseDiagnosticPrimary_,
					lastTouchMouseDiagnosticFromPointer_ ? "pointer" : "compat-mouse",
					lastTouchMousePositionKnown_ && mouseClientX == lastTouchMouseX_ &&
						mouseClientY == lastTouchMouseY_ ? 1u : 0u,
					filter.sourceRejected ? 1u : 0u, filter.systemRejected ? 1u : 0u,
					filter.positionRejected ? 1u : 0u, filter.buttonBypass ? 1u : 0u);
			if (filter.rejectionReason)
			{
				recordMouseDecision(false, filter.rejectionReason);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, filter.rejectionReason);
#endif
				RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
				break;
			}
			if (buttonUp && penCompatibilityMouseContactSuppressed_.exchange(
				false, std::memory_order_acq_rel))
			{
				recordMouseDecision(false, "pen-compatibility-terminal");
				if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0)
					SetPenContactSuppressedForTouchPan(false);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pen-compatibility-terminal");
#endif
				RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
				break;
			}
			if (penCompatibilityMouseContactSuppressed_.load(
				std::memory_order_acquire))
			{
				recordMouseDecision(false, "pen-compatibility-latched");
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pen-compatibility-latched");
#endif
				RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
				break;
			}
			if (buttonUp && ShouldSuppressMouseButtonUpCursorSample(CursorOwner()))
			{
				recordMouseDecision(false, "pointer-terminal-owner");
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pointer-terminal-owner");
#endif
				RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
				break; // Pointer 终态后的兼容 Mouse Up 不能把已隐藏光标重新发布为 Hover。
			}
			LARGE_INTEGER qpc = {};
			LARGE_INTEGER qpcFrequency = {};
			QueryPerformanceCounter(&qpc);
			QueryPerformanceFrequency(&qpcFrequency);
			const float mouseX = static_cast<float>(mouseClientX);
			const float mouseY = static_cast<float>(mouseClientY);
			const double penSampleAgeSeconds = penSampleValid && qpcFrequency.QuadPart > 0 &&
				qpc.QuadPart >= penSample.qpc
				? static_cast<double>(qpc.QuadPart - penSample.qpc) /
					static_cast<double>(qpcFrequency.QuadPart)
				: (std::numeric_limits<double>::infinity)();
			if (ShouldTreatMouseContactAsPenCompatibilityMessage(
				touchPanActive_.load(std::memory_order_acquire),
				penSampleValid, buttonDown, mouseX - penSample.x, mouseY - penSample.y,
				penSampleAgeSeconds))
			{
				recordMouseDecision(false, "pen-compatibility-position");
				// 没有 Pointer Down 的设备仍会提升兼容鼠标消息；只抑制反馈，不改变平移 authority。
				penCompatibilityMouseContactSuppressed_.store(true,
					std::memory_order_release);
				SuppressPenContactForTouchPan();
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pen-compatibility-position");
#endif
				RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
				break;
			}
			if (touchPanActive_.load(std::memory_order_acquire))
				realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
			SetTouchCursorSuppressed(false);
			SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
			if (message == WM_MOUSEMOVE && !trackingMouseLeave_)
			{
				TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
				trackingMouseLeave_ = TrackMouseEvent(&tracking) != FALSE;
			}
			DrawingCursorSample sample;
			sample.x = mouseX;
			sample.y = mouseY;
			sample.valid = true;
			sample.inContact = buttonDown;
			sample.qpc = qpc.QuadPart;
			// 非 Windows Ink 数位板以驱动注入的鼠标到达；仅在用户开启时标记为数位板来源。
			if (injectedMouseAsTablet_.load(std::memory_order_acquire) && sourceSnapshot.succeeded &&
				sourceSnapshot.deviceType == IMDT_MOUSE && sourceSnapshot.originId == IMO_INJECTED)
				sample.source.kind = SpeedEraser::SourceKind::ExternalPen;
			PublishMouseCursorSample(sample);
			lastTouchMousePositionKnown_ = false;
			recordMouseDecision(true, "accepted-mouse");
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceMouseDecision(true, "accepted-real-mouse");
#endif
			RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
			break;
		}

		case WM_MOUSELEAVE:
			mouseCanvasExitRevision_.fetch_add(1);
		{
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const uint32_t messageTick = static_cast<uint32_t>(GetMessageTime());
			const ULONG_PTR extraInfo = GetMessageExtraInfo();
			const uint64_t barrierState = latestTouchInputBarrierTick_.load(
				std::memory_order_acquire);
			TraceTouchMouseMessage(message, messageTick,
				static_cast<uint32_t>(barrierState),
				(barrierState & kTouchInputBarrierKnownMask) != 0,
				extraInfo, IsPromotedPointerMouseMessage(extraInfo), 0, 0,
				activeTouchContactCount_.load(std::memory_order_acquire),
				true, "accepted-mouse-leave-clear");
#endif
			trackingMouseLeave_ = false;
			ClearMouseCursorSample();
			// Touch 会暂时覆盖有效归属；离窗时仍要清除持久的 Mouse 归属。
			if (cursorOwner_.load(std::memory_order_acquire) ==
				DrawingCursorPointerAuthority::Mouse)
				SetDrawingCursorOwner(DrawingCursorPointerAuthority::Unknown);
			RecordCursorDiagnostic("mouse-leave owner=%u suppressed=%u",
				static_cast<unsigned>(CursorOwner()),
				touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u);
			break;
		}

		case WM_MOUSEWHEEL:
		{
			const uint32_t messageTick = static_cast<uint32_t>(GetMessageTime());
			const ULONG_PTR extraInfo = GetMessageExtraInfo();
			const bool promotedPointerMessage = IsPromotedPointerMouseMessage(extraInfo);
			const auto sourceSnapshot = CurrentInputMessageSource();
			const INPUT_MESSAGE_DEVICE_TYPE inputSource = sourceSnapshot.deviceType;
			const uint64_t diagnosticEvent = CursorDiagnosticsEnabled()
				? ++cursorMouseDiagnosticEvent_ : 0;
			RecordMouseMessageSource(diagnosticEvent, window, message, wParam, lParam, sourceSnapshot);
			RecordMouseCursorDiagnosticState(diagnosticEvent, "before");
			const uint64_t barrierState = latestTouchInputBarrierTick_.load(
				std::memory_order_acquire);
			const bool touchBarrierKnown =
				(barrierState & kTouchInputBarrierKnownMask) != 0;
			const uint32_t touchBarrierTick = static_cast<uint32_t>(barrierState);
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
			const bool accepted = !ShouldIgnoreMouseCursorMessage(promotedPointerMessage,
				penSampleValid, touchBarrierKnown, messageTick, touchBarrierTick,
				inputSource);
			if (accepted)
			{
				if (touchPanActive_.load(std::memory_order_acquire))
					realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
				SetTouchCursorSuppressed(false);
				SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
			}
			if (CursorDiagnosticsEnabled())
				RecordCursorDiagnostic("mouse-wheel event=%llu accepted=%u source=%u promoted=%u tick=%u barrier=%u owner=%u suppressed=%u",
					static_cast<unsigned long long>(diagnosticEvent), accepted ? 1u : 0u, static_cast<unsigned>(inputSource),
					promotedPointerMessage ? 1u : 0u, messageTick, touchBarrierTick,
					static_cast<unsigned>(CursorOwner()),
					touchCursorSuppressed_.load(std::memory_order_acquire) ? 1u : 0u);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const bool staleQueuedMouse = touchBarrierKnown &&
				static_cast<LONG>(messageTick - touchBarrierTick) <= 0;
			TraceTouchMouseMessage(message, messageTick, touchBarrierTick,
				touchBarrierKnown, extraInfo, promotedPointerMessage,
				GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
				activeTouchContactCount_.load(std::memory_order_acquire), accepted,
				accepted ? "accepted-real-mouse-wheel" :
				promotedPointerMessage ? "promoted-pointer" :
				staleQueuedMouse ? "stale-touch-barrier" :
				inputSource == IMDT_TOUCH ? "source-touch" :
				inputSource == IMDT_PEN ? "source-pen" : "pen-compatibility-filter");
#endif
			RecordMouseCursorDiagnosticState(diagnosticEvent, "after");
			break;
		}

		}
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
