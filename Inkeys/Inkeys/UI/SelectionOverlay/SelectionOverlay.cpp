module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>
#include <d2d1_1helper.h>
#include <tpcshrd.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
#include "../Bar/Bar.InteractionWorker.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
module Inkeys.UI.SelectionOverlay;
import Inkeys.UI.Bar;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Message;
import Inkeys.Window;

namespace Inkeys::UI::SelectionOverlay
{
	namespace
	{
		using namespace Inkeys::UI::RenderPipeline;
		using Inkeys::Window::WindowRole;
		std::shared_ptr<Bar::ShapeEditorSurface> scene;
		std::atomic_bool escapeDown=false;
		std::atomic_bool running=false;
		std::atomic_uint64_t lifecycle=0;
		std::mutex lifecycleMutex;
		Inkeys::Display::Subscription displaySubscription;
		std::uint64_t nativeTicket=0,nextTicket=1;
		std::uint32_t touchId=0;
		bool translatingTouch=false,releasingCapture=false,touchActive=false;
		constexpr UINT ReleaseCaptureMessage=WM_APP+0x375;
		void Request() noexcept {RenderPipeline::Request(Client::SelectionOverlay);}
		void Refresh(void*) noexcept {Request();RenderPipeline::Request(Client::Bar);}
		FrameResult Render(const FrameContext& frame)
		{
			const auto target=std::atomic_load(&scene);
			if(!running.load()||!target)return FrameResult::Idle;
			auto& service=Window::GetService();const HWND hwnd=service.Handle(WindowRole::SelectionOverlay);
			if(!hwnd)return FrameResult::Idle;
			const auto snapshot=Inkeys::Drawing::Draw3::ProductShapeUiSnapshot();
			const auto display=Inkeys::Display::GetSnapshot();
			RECT work{};if(display)
			{
				const POINT center{snapshot?snapshot->canvasOriginX+static_cast<LONG>((snapshot->bounds.left+snapshot->bounds.right)/2):0,snapshot?snapshot->canvasOriginY+static_cast<LONG>((snapshot->bounds.top+snapshot->bounds.bottom)/2):0};
				const auto* monitor=display->Find(MonitorFromPoint(center,MONITOR_DEFAULTTONEAREST));if(!monitor)monitor=display->Primary();if(monitor)work=monitor->workArea;
			}
			RECT bar{};if(const HWND h=service.Handle(WindowRole::Bar))GetWindowRect(h,&bar);
			target->Configure(snapshot,work,bar);target->Advance(frame.frameTime);
			if(!target->Visible()||service.WhiteboardWindowGroupMinimized())
			{
				(void)service.Hide(WindowRole::SelectionOverlay);target->CommitPresented(false);return FrameResult::Idle;
			}
			const RECT bounds=target->Bounds();
			const HRESULT resource=target->EnsureDeviceResources(frame.epoch);
			if(FAILED(resource))return resource==D2DERR_RECREATE_TARGET?FrameResult::DeviceLost:FrameResult::Retry;
				const HRESULT present = target->Present([hwnd](const UPDATELAYEREDWINDOWINFO& info){return UpdateLayeredWindowIndirect(hwnd,&info);});
			if (present == D2DERR_RECREATE_TARGET) { target->ReleaseDeviceResources(); return FrameResult::DeviceLost; }
			if (FAILED(present)) return FrameResult::Retry;
			// 窗口外框和输入真值在完整ULW事务成功后一起提交，失败继续沿用旧帧。
			if(!service.SetBounds(WindowRole::SelectionOverlay,bounds)||!service.Show(WindowRole::SelectionOverlay))return FrameResult::Retry;
			target->CommitPresented();return target->AnimationActive()?FrameResult::Continue:FrameResult::Idle;
		}
		void Input(const Bar::SurfaceInputEvent& e,void*)
		{
			const auto target=std::atomic_load(&scene);
			if(!running.load()||!target||e.generation!=lifecycle.load())return;
			if(e.message==WM_KEYDOWN||e.message==WM_KEYUP)target->Keyboard(static_cast<BYTE>(e.wParam),e.message==WM_KEYDOWN);
			else target->Pointer(e.message,e.screenPoint,e.captureOwned,e.presentedRevision,GET_WHEEL_DELTA_WPARAM(e.wParam));
			if(e.message==WM_CANCELMODE||e.message==WM_CAPTURECHANGED)
				PostMessageW(e.window,ReleaseCaptureMessage,static_cast<WPARAM>(e.captureTicket),0);
			Request();
		}
		std::chrono::steady_clock::time_point Tick(std::chrono::steady_clock::time_point now,void*)
		{return Bar::RetryShapeUiIntents(now);}
		void Cancel(const Bar::SurfaceInputEvent& e,void*)
		{
			const auto target=std::atomic_load(&scene);
			if(target)target->CancelPointer();if(e.window)PostMessageW(e.window,ReleaseCaptureMessage,static_cast<WPARAM>(e.captureTicket),0);Request();
		}
		LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
		{
			const auto target=std::atomic_load(&scene);
			if(message==ReleaseCaptureMessage)
			{
				if(nativeTicket&&nativeTicket==wp){nativeTicket=0;touchId=0;touchActive=false;releasingCapture=true;if(GetCapture()==hwnd)ReleaseCapture();releasingCapture=false;}return 0;
			}
			if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
			if(message==WM_TABLET_QUERYSYSTEMGESTURESTATUS)return TABLET_DISABLE_PRESSANDHOLD|TABLET_DISABLE_PENTAPFEEDBACK|TABLET_DISABLE_PENBARRELFEEDBACK|TABLET_DISABLE_FLICKS;
			if(message==WM_NCHITTEST)
			{
				if(!running.load()||!target)return HTTRANSPARENT;
				return target->Hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}).occupied?HTCLIENT:HTTRANSPARENT;
			}
			if(message==WM_TOUCH)
			{
				std::vector<TOUCHINPUT> inputs(LOWORD(wp));if(GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lp),static_cast<UINT>(inputs.size()),inputs.data(),sizeof(TOUCHINPUT)))
				{
					std::stable_partition(inputs.begin(),inputs.end(),[](const TOUCHINPUT& t){return (t.dwFlags&TOUCHEVENTF_PRIMARY)!=0;});
					for(const auto& t:inputs)
					{
						if(!touchActive&&(t.dwFlags&TOUCHEVENTF_DOWN)){touchId=t.dwID;touchActive=true;}
						if(!touchActive||t.dwID!=touchId)continue;
						POINT point{TOUCH_COORD_TO_PIXEL(t.x),TOUCH_COORD_TO_PIXEL(t.y)};ScreenToClient(hwnd,&point);
						const UINT translated=t.dwFlags&TOUCHEVENTF_DOWN?WM_LBUTTONDOWN:t.dwFlags&TOUCHEVENTF_UP?WM_LBUTTONUP:WM_MOUSEMOVE;
						translatingTouch=true;WindowProc(hwnd,translated,translated==WM_LBUTTONUP?0:MK_LBUTTON,MAKELPARAM(point.x,point.y));translatingTouch=false;
						if(t.dwFlags&TOUCHEVENTF_UP){touchId=0;touchActive=false;}
					}
				}CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lp));return 0;
			}
			if((message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)&&!translatingTouch&&Message::IsTouchGeneratedMouseMessage(message,GetMessageExtraInfo()))return 0;
			if(message==WM_CAPTURECHANGED&&releasingCapture)return 0;
			if(message==WM_MOUSEMOVE||message==WM_MOUSELEAVE||message==WM_MOUSEWHEEL||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_CANCELMODE||message==WM_CAPTURECHANGED||message==WM_KEYDOWN||message==WM_KEYUP)
			{
				Bar::SurfaceInputEvent e;e.window=hwnd;e.message=message;e.wParam=wp;e.lParam=lp;e.generation=lifecycle.load();
				e.screenPoint={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(message!=WM_MOUSEWHEEL)ClientToScreen(hwnd,&e.screenPoint);e.touch=translatingTouch;e.contactId=touchId;
				if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
				if(message==WM_LBUTTONDOWN){SetCapture(hwnd);if(GetCapture()==hwnd)nativeTicket=nextTicket++;}
				e.captureTicket=nativeTicket;e.captureOwned=GetCapture()==hwnd;
				if(target)e.presentedRevision=target->PresentedGeneration();
				const bool accepted=Bar::SubmitSurfaceInput(e);
				if(message==WM_LBUTTONUP||(!accepted&&message==WM_LBUTTONDOWN)){const auto ticket=nativeTicket;PostMessageW(hwnd,ReleaseCaptureMessage,static_cast<WPARAM>(ticket),0);}
				return 0;
			}return DefWindowProcW(hwnd,message,wp,lp);
		}
	}
	Window::WindowSpec MakeWindowSpec()
	{
		Window::WindowSpec spec;spec.role=WindowRole::SelectionOverlay;spec.title=L"Inkeys Shape Editor";
		spec.className=L"Inkeys.UI.SelectionOverlay";spec.style=WS_POPUP;spec.exStyle=WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
		spec.windowProc=WindowProc;spec.bindMessages=false;spec.created=[](HWND hwnd){RegisterTouchWindow(hwnd,TWF_WANTPALM);};
		return spec;
	}
	bool Acquire()
	{
		std::scoped_lock lock(lifecycleMutex);if(running.load())return true;
		std::atomic_store(&scene,std::make_shared<Bar::ShapeEditorSurface>());const auto generation=lifecycle.fetch_add(1)+1;
		const auto hwnd=Window::GetService().Handle(WindowRole::SelectionOverlay);
		if(!hwnd||!Bar::RegisterInteractionSurface(hwnd,generation,Input,Tick,Cancel,nullptr))return false;
		running=true;
		if(!RenderPipeline::Register(Client::SelectionOverlay,Render)){running=false;Bar::UnregisterInteractionSurface(hwnd);(void)Bar::DrainInteractionSurfaceCallbacks();std::atomic_store(&scene,std::shared_ptr<Bar::ShapeEditorSurface>{});return false;}
		Inkeys::Drawing::Draw3::SetShapeUiRefreshCallback(Refresh,nullptr);
		displaySubscription=Inkeys::Display::Subscribe([](auto){Request();});Request();return true;
	}
	void Release() noexcept
	{
		std::scoped_lock lock(lifecycleMutex);if(!running.exchange(false))return;
		Inkeys::Drawing::Draw3::SetShapeUiRefreshCallback(nullptr,nullptr);displaySubscription.Reset();
		RenderPipeline::Unregister(Client::SelectionOverlay);const HWND hwnd=Window::GetService().Handle(WindowRole::SelectionOverlay);Bar::UnregisterInteractionSurface(hwnd);
		(void)Bar::DrainInteractionSurfaceCallbacks();if(const auto target=std::atomic_load(&scene))target->CancelPointer();Bar::ClearShapeUiIntents();(void)Window::GetService().Hide(WindowRole::SelectionOverlay);std::atomic_store(&scene,std::shared_ptr<Bar::ShapeEditorSurface>{});
	}
	void NotifyStateChanged() noexcept {Request();}
	bool TryQueueKeyboard(BYTE key,bool down)
	{
		if(key==VK_ESCAPE)
		{
			if(!down)return escapeDown.exchange(false);
			if(escapeDown.load())return true;
			if(!running.load())return false;
			const auto snapshot=Inkeys::Drawing::Draw3::ProductShapeUiSnapshot();
			if(!snapshot||(!snapshot->object&&!snapshot->construction.active))return false;
			escapeDown=true;
			Inkeys::Drawing::Draw3::ShapeUiAction action;action.identity=snapshot->identity;
			action.kind=snapshot->construction.active?Inkeys::Drawing::Draw3::ShapeUiActionKind::ConstructionCancel:Inkeys::Drawing::Draw3::ShapeUiActionKind::CloseEditor;
			(void)Bar::SubmitShapeUiIntent(action);
			if(const HWND hwnd=Window::GetService().Handle(WindowRole::SelectionOverlay))PostMessageW(hwnd,WM_CANCELMODE,0,0);
			return true;
		}
		if(!running.load())return false;POINT point{};const HWND hwnd=Window::GetService().Handle(WindowRole::SelectionOverlay);
		if(!GetCursorPos(&point)||WindowFromPoint(point)!=hwnd)return false;
		if(key!=VK_TAB&&key!=VK_LEFT&&key!=VK_RIGHT&&key!=VK_RETURN&&key!=VK_SPACE)return false;
		Bar::SurfaceInputEvent e;e.window=hwnd;e.message=down?WM_KEYDOWN:WM_KEYUP;e.wParam=key;e.generation=lifecycle.load();return Bar::SubmitSurfaceInput(e);
	}
}
