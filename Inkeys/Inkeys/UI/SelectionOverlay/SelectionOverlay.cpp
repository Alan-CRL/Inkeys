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
#include <cstdio>
#include "../Bar/Bar.InteractionWorker.h"
#include "../../../IdtState.h"
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
		std::atomic_bool running=false;
		std::atomic_uint64_t lifecycle=0;
		std::mutex lifecycleMutex;
		Inkeys::Display::Subscription displaySubscription;
		std::uint64_t nativeTicket=0,nextTicket=1;
		enum class NativeKind { None, Mouse, Touch, TouchPen, PointerPen };
		struct NativeContact { std::uint64_t source; std::uint32_t id; };
		// 原生接触集合只在窗口线程修改，准入与拓扑规则供同源无窗口回归使用。
		struct NativeContactRouting
		{
			std::vector<NativeContact> contacts;
			NativeKind kind=NativeKind::None;
			bool body=false, preserveTrajectory=false;
			bool Has(std::uint64_t source,std::uint32_t id) const
			{return std::any_of(contacts.begin(),contacts.end(),[&](const auto& c){return c.source==source&&c.id==id;});}
			bool CanBegin(NativeKind input,std::uint64_t source,std::uint32_t id,bool occupied,bool bodyHit) const
			{
				return occupied&&!Has(source,id)&&contacts.size()<16&&(contacts.empty()||(kind==input&&input==NativeKind::Touch&&body&&bodyHit));
			}
			void Begin(NativeKind input,std::uint64_t source,std::uint32_t id,bool bodyHit)
			{if(contacts.empty()){kind=input;body=bodyHit;}contacts.push_back({source,id});}
			bool End(std::uint64_t source,std::uint32_t id)
			{if(!Has(source,id))return false;std::erase_if(contacts,[&](const auto& c){return c.source==source&&c.id==id;});return contacts.empty();}
			void Clear(){contacts.clear();kind=NativeKind::None;body=false;preserveTrajectory=false;}
		};
		NativeContactRouting nativeRouting;
		bool releasingCapture=false;
		bool IsNativeReleaseTicket(std::uint64_t active,std::uint64_t received) noexcept{return active!=0&&active==received;}
		bool NativeCaptureLossCancels(bool releasing) noexcept{return !releasing;}
		using PointerInfoFn=BOOL(WINAPI*)(UINT32,POINTER_INFO*);
		const auto pointerInfo=reinterpret_cast<PointerInfoFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetPointerInfo"));
		constexpr UINT ReleaseCaptureMessage=WM_APP+0x375;
		void Request() noexcept {RenderPipeline::Request(Client::SelectionOverlay);}
		// refresh回调由Draw3在同一互斥内串行调用；主栏只关心构造提示与编辑会话开闭，不随每次拖动预览重绘。
		struct BarRefreshKey{bool object=false,construction=false;std::uint64_t session=0;};
		BarRefreshKey barRefreshKey;
		void Refresh(void*) noexcept
		{
			Request();
			const auto snapshot=Inkeys::Drawing::Draw3::ProductShapeUiSnapshot();
			const BarRefreshKey next{snapshot&&snapshot->object.has_value(),snapshot&&snapshot->construction.active,snapshot?snapshot->identity.session:0};
			if(next.construction||next.object!=barRefreshKey.object||next.construction!=barRefreshKey.construction||next.session!=barRefreshKey.session)
				RenderPipeline::Request(Client::Bar);
			barRefreshKey=next;
			Bar::WakeInteractionWorker();
		}
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
			// 窗口组最小化视为画布失焦：每次最小化只请求一次关闭，由绘制线程落定图形。
			static bool minimizedFocusLossSent=false;
			const bool minimized=service.WhiteboardWindowGroupMinimized();
			if(minimized&&!minimizedFocusLossSent&&snapshot&&snapshot->object){Inkeys::Drawing::Draw3::RequestShapeEditorFocusLoss();minimizedFocusLossSent=true;}
			if(!minimized)minimizedFocusLossSent=false;
			if(!target->Visible()||minimized)
			{
				(void)service.Hide(WindowRole::SelectionOverlay);target->CommitPresented(false);return FrameResult::Idle;
			}
			const RECT bounds=target->Bounds();
			// 快照、布局、动画与光照均未变化时不重复回读/ULW；窗口与命中沿用上次成功帧。
			if(!target->NeedsPresent())return target->AnimationActive()?FrameResult::Continue:FrameResult::Idle;
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
			if(e.message==WM_MOUSEWHEEL)target->Pointer(e.message,e.screenPoint,e.captureOwned,e.presentedRevision,GET_WHEEL_DELTA_WPARAM(e.wParam));
			else target->Contact(e.message,e.screenPoint,e.contactSource,e.contactId,e.touch,e.pen,e.inputSeconds,e.captureOwned,e.presentedRevision);
			if(e.message==WM_CANCELMODE||e.message==WM_CAPTURECHANGED)
				PostMessageW(e.window,ReleaseCaptureMessage,static_cast<WPARAM>(e.captureTicket),0);
			Request();
		}
		std::chrono::steady_clock::time_point Tick(std::chrono::steady_clock::time_point now,void*)
		{
			const auto target=std::atomic_load(&scene);
			const auto snapshot=Inkeys::Drawing::Draw3::ProductShapeUiSnapshot();
			static std::uint64_t finishedSession=0,finishedEpoch=0;
			bool restorePending=false;
			if(snapshot&&snapshot->finishedEditingSession&&(snapshot->finishedEditingSession!=finishedSession||snapshot->finishedEditingEntryEpoch!=finishedEpoch))
			{
				const auto epoch=snapshot->finishedEditingEntryEpoch;const auto workspace=snapshot->finishedEditingWorkspace;
				// 发布回调只唤醒 worker；状态恢复离开 Draw3 发布锁后才排入业务队列。
				if(Bar::QueueInteractionComponentAction([workspace,epoch]{RestoreToolAfterShapeEditing(workspace,epoch);}))
				{finishedSession=snapshot->finishedEditingSession;finishedEpoch=epoch;}
				else restorePending=true;
			}
			static auto previousPrediction=std::chrono::steady_clock::time_point::max();
			const auto prediction=target?target->TickPrediction(now):std::chrono::steady_clock::time_point::max();
			if(prediction!=std::chrono::steady_clock::time_point::max()||previousPrediction<=now)Request();
			previousPrediction=prediction;
			const auto toolbar=target?target->TickToolbarIdle(now):std::chrono::steady_clock::time_point::max();
			if(toolbar<=now)Request();
			return (std::min)({prediction,toolbar>now?toolbar:std::chrono::steady_clock::time_point::max(),Bar::RetryShapeUiIntents(now),restorePending?now+std::chrono::milliseconds(8):std::chrono::steady_clock::time_point::max()});
		}
		void Cancel(const Bar::SurfaceInputEvent& e,void*)
		{
			const auto target=std::atomic_load(&scene);
			if(target)target->CancelPointer();if(e.window)PostMessageW(e.window,ReleaseCaptureMessage,static_cast<WPARAM>(e.captureTicket),0);Request();
		}
		void ReleaseNative(HWND hwnd)
		{
			nativeRouting.Clear();nativeTicket=0;
			releasingCapture=true;
			if(GetCapture()==hwnd)ReleaseCapture();releasingCapture=false;
		}
		void CancelNative(HWND hwnd,UINT reason=WM_CANCELMODE)
		{
			if(nativeTicket){Bar::SurfaceInputEvent e;e.window=hwnd;e.message=reason;e.generation=lifecycle.load();e.captureTicket=nativeTicket;(void)Bar::SubmitSurfaceInput(e);}
			ReleaseNative(hwnd);
		}
		bool ContactNative(HWND hwnd,UINT message,POINT point,NativeKind kind,std::uint64_t source,std::uint32_t id)
		{
			const auto target=std::atomic_load(&scene);
			if(!running.load()||!target)return false;
			const bool down=message==WM_LBUTTONDOWN,up=message==WM_LBUTTONUP;
			auto found=std::find_if(nativeRouting.contacts.begin(),nativeRouting.contacts.end(),[&](const auto& c){return c.source==source&&c.id==id;});
			if(down)
			{
				const auto hit=target->Hit(point);
				if(!hit.occupied)return false;
				const bool body=!hit.button&&(hit.body||hit.handle==100);
				// 首个接触锁定来源；附加触点只能加入本体手势，不能抢占按钮或专属控制点。
				if(!nativeRouting.CanBegin(kind,source,id,hit.occupied,body))return false;
				if(nativeRouting.contacts.empty())
				{
					nativeRouting.kind=kind;nativeRouting.body=body;nativeTicket=nextTicket++;
					// 位置编辑只需最新坐标；角度和可加入多指的本体保留有序轨迹，避免三维逐点重建积压。
					nativeRouting.preserveTrajectory=target->PreservePointerTrajectory(point)||(kind==NativeKind::Touch&&body);
					// Pointer 自带接触捕获；共用鼠标捕获只维护同一窗口手势生命周期。
					SetCapture(hwnd);if(GetCapture()!=hwnd){ReleaseNative(hwnd);return false;}
				}
				nativeRouting.Begin(kind,source,id,body);
			}
			else if(found==nativeRouting.contacts.end()||nativeRouting.kind!=kind)return false;
			Bar::SurfaceInputEvent e;e.window=hwnd;e.message=message;e.screenPoint=point;e.generation=lifecycle.load();
			e.contactSource=source;e.contactId=id;e.touch=kind==NativeKind::Touch;e.pen=kind==NativeKind::TouchPen||kind==NativeKind::PointerPen;
			e.inputSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
			e.captureTicket=nativeTicket;e.captureOwned=true;e.preserveMoveSamples=nativeRouting.preserveTrajectory;e.presentedRevision=target->PresentedGeneration();
			if(!Bar::SubmitSurfaceInput(e)){CancelNative(hwnd);return false;}
			if(up)
			{
				const bool last=nativeRouting.End(source,id);
				// 非末指抬起保留捕获和同一事务；迟到的释放消息必须匹配票据。
				if(last)PostMessageW(hwnd,ReleaseCaptureMessage,static_cast<WPARAM>(nativeTicket),0);
			}
			return true;
		}
		LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
		{
			const auto target=std::atomic_load(&scene);
			if(message==ReleaseCaptureMessage){if(IsNativeReleaseTicket(nativeTicket,wp))ReleaseNative(hwnd);return 0;}
			if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
			if(message==WM_TABLET_QUERYSYSTEMGESTURESTATUS)return TABLET_DISABLE_PRESSANDHOLD|TABLET_DISABLE_PENTAPFEEDBACK|TABLET_DISABLE_PENBARRELFEEDBACK|TABLET_DISABLE_FLICKS;
			if(message==WM_NCHITTEST)return running.load()&&target&&target->Hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}).occupied?HTCLIENT:HTTRANSPARENT;
			if(message==WM_TOUCH)
			{
				std::vector<TOUCHINPUT> inputs(LOWORD(wp));
				if(GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lp),static_cast<UINT>(inputs.size()),inputs.data(),sizeof(TOUCHINPUT)))
				{
					std::stable_partition(inputs.begin(),inputs.end(),[](const auto& t){return (t.dwFlags&TOUCHEVENTF_PRIMARY)!=0;});
					for(const auto& t:inputs)ContactNative(hwnd,t.dwFlags&TOUCHEVENTF_DOWN?WM_LBUTTONDOWN:t.dwFlags&TOUCHEVENTF_UP?WM_LBUTTONUP:WM_MOUSEMOVE,
						{TOUCH_COORD_TO_PIXEL(t.x),TOUCH_COORD_TO_PIXEL(t.y)},t.dwFlags&0x40u?NativeKind::TouchPen:NativeKind::Touch,reinterpret_cast<std::uintptr_t>(t.hSource),t.dwID);
				}
				else if(nativeRouting.kind==NativeKind::Touch||nativeRouting.kind==NativeKind::TouchPen)CancelNative(hwnd);
				CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lp));return 0;
			}
			if(message==WM_POINTERDOWN||message==WM_POINTERUPDATE||message==WM_POINTERUP)
			{
				POINTER_INFO info{};const auto id=GET_POINTERID_WPARAM(wp);
				if(pointerInfo&&pointerInfo(id,&info)&&info.pointerType==PT_PEN)
				{
					if(info.pointerFlags&POINTER_FLAG_CANCELED){if(nativeRouting.kind==NativeKind::PointerPen)CancelNative(hwnd);}
					else ContactNative(hwnd,message==WM_POINTERDOWN?WM_LBUTTONDOWN:message==WM_POINTERUP?WM_LBUTTONUP:WM_MOUSEMOVE,info.ptPixelLocation,NativeKind::PointerPen,reinterpret_cast<std::uintptr_t>(info.sourceDevice),id);
					return 0;
				}
			}
			if(message==WM_POINTERUP&&nativeRouting.kind==NativeKind::PointerPen&&std::any_of(nativeRouting.contacts.begin(),nativeRouting.contacts.end(),[&](const auto& c){return c.id==GET_POINTERID_WPARAM(wp);}))CancelNative(hwnd);
			if(message==WM_POINTERCAPTURECHANGED){if(!releasingCapture&&nativeRouting.kind==NativeKind::PointerPen&&std::any_of(nativeRouting.contacts.begin(),nativeRouting.contacts.end(),[&](const auto& c){return c.id==GET_POINTERID_WPARAM(wp);}))CancelNative(hwnd);return 0;}
			if((message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)&&Message::IsPointerGeneratedMouseMessage(message,GetMessageExtraInfo()))return 0;
			// 所有来源共用 SetCapture 所有权；Pointer 自身失捕由上面的消息独立取消。
			if(message==WM_CAPTURECHANGED){if(NativeCaptureLossCancels(releasingCapture))CancelNative(hwnd,message);return 0;}
			if(message==WM_CANCELMODE||(message==WM_SHOWWINDOW&&!wp)||message==WM_DESTROY){CancelNative(hwnd);if(message==WM_CANCELMODE)return 0;}
			if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_MOUSEMOVE)
			{
				POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(hwnd,&point);
				if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
				if(ContactNative(hwnd,message,point,NativeKind::Mouse,0,0)||message!=WM_MOUSEMOVE)return 0;
				if(nativeRouting.kind!=NativeKind::None)return 0;
			}
			if(message==WM_MOUSEMOVE||message==WM_MOUSELEAVE||message==WM_MOUSEWHEEL)
			{
				Bar::SurfaceInputEvent e;e.window=hwnd;e.message=message;e.wParam=wp;e.lParam=lp;e.generation=lifecycle.load();
				e.screenPoint={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(message!=WM_MOUSEWHEEL)ClientToScreen(hwnd,&e.screenPoint);
				e.inputSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
				if(target)e.presentedRevision=target->PresentedGeneration();(void)Bar::SubmitSurfaceInput(e);return 0;
			}
			return DefWindowProcW(hwnd,message,wp,lp);
		}

	}
	int RunNativeInputRoutingTests() noexcept
	{
		int failures=0,checks=0;
		const auto expect=[&](bool value,const char* name){++checks;if(!value){++failures;std::fprintf(stderr,"[ShapeNative] FAIL %s\n",name);}};
		NativeContactRouting route;
		expect(!route.CanBegin(NativeKind::Touch,1,1,false,true),"blank Down cannot start an editor contact");
		expect(!route.Has(1,1),"Move without Down has no owner");
		expect(route.CanBegin(NativeKind::Touch,1,7,true,true),"first body touch accepted");route.Begin(NativeKind::Touch,1,7,true);
		expect(!route.CanBegin(NativeKind::Touch,1,7,true,true),"duplicate source/id Down rejected");
		expect(route.CanBegin(NativeKind::Touch,2,7,true,true),"same id from different source remains independent");route.Begin(NativeKind::Touch,2,7,true);
		expect(route.contacts.size()==2,"second Down joins the existing group");
		expect(!route.CanBegin(NativeKind::Touch,1,8,true,false),"second contact over a control cannot join body gesture");
		expect(!route.CanBegin(NativeKind::Mouse,0,0,true,true)&&!route.CanBegin(NativeKind::PointerPen,3,9,true,true),"mouse and pen cannot mix with touch owner");
		expect(!route.End(1,7)&&route.Has(2,7),"first Up preserves remaining finger and capture");
		expect(!route.End(1,7)&&route.Has(2,7),"duplicate old Up cannot release another source");
		expect(route.End(2,7),"last Up releases group capture");route.Clear();
		expect(route.CanBegin(NativeKind::PointerPen,5,1,true,false),"single pen can acquire control");route.Begin(NativeKind::PointerPen,5,1,false);
		expect(!route.CanBegin(NativeKind::TouchPen,5,1,true,false),"pen WM_TOUCH duplicate cannot acquire pointer-owned control");
		expect(!route.CanBegin(NativeKind::Touch,9,1,true,true),"touch cannot join pen control");
		route.Clear();expect(!route.Has(5,1)&&route.kind==NativeKind::None,"cancel or hide retires source ownership");
		route.Begin(NativeKind::Touch,1,1,false);expect(!route.CanBegin(NativeKind::Touch,1,2,true,true),"handle-first gesture stays single-contact");route.Clear();
		for(unsigned i=0;i<16;++i){expect(route.CanBegin(NativeKind::Touch,1,i,true,true),"bounded group accepts legal contact");route.Begin(NativeKind::Touch,1,i,true);}
		expect(!route.CanBegin(NativeKind::Touch,1,16,true,true),"seventeenth contact rejected without changing group");
		expect(!route.End(1,16)&&route.contacts.size()==16,"rejected terminal cannot release accepted contacts");route.Clear();
		expect(!IsNativeReleaseTicket(0,0)&&!IsNativeReleaseTicket(22,21)&&IsNativeReleaseTicket(22,22),"late or zero capture-release ticket cannot retire newer gesture");
		expect(NativeCaptureLossCancels(false),"pen and touch cancel on common mouse capture loss");
		expect(!NativeCaptureLossCancels(true),"intentional release does not cancel the completed transaction");
		std::fprintf(stderr,"[ShapeNative] %d checks, %d failures\n",checks,failures);return failures?1:0;
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
}
