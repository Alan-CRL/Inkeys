#pragma once

#include "UiPointerInput.h"
#include "../Drawing/Draw3/Draw3.HapticDiagnostics.h"
#include <array>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <utility>
#include <type_traits>

namespace Inkeys::Input::UiPointer::Diagnostics
{
    inline bool Enabled() noexcept { return Drawing::Draw3::HapticDiagnostics::Enabled(); }
    // 诊断中的系统查询/输出不得污染调用方正在检查的错误状态。
    struct ErrorScope
    {
        bool active = Enabled();
        DWORD error = active ? GetLastError() : 0;
        ~ErrorScope() noexcept { if (active) SetLastError(error); }
    };
    struct MoveLimiter
    {
        struct Entry { std::uintptr_t window = 0, device = 0; std::uint64_t generation = 0; DWORD contact = 0; unsigned stage = 0, suppressed = 0; ULONGLONG tick = 0; bool used = false; Source source = Source::Mouse; };
        std::array<Entry,32> entries{};
        unsigned next = 0;
        bool Admit(std::uintptr_t window, const Sample& sample, unsigned stage, ULONGLONG tick, unsigned& suppressed) noexcept
        {
            Entry* entry = nullptr;
            for (auto& item : entries) if (item.used && item.window==window && item.device==sample.device && item.source==sample.source && item.generation==sample.generation && item.contact==sample.contact && item.stage==stage) { entry=&item; break; }
            if (!entry) { entry=&entries[next++ % entries.size()]; *entry={window,sample.device,sample.generation,sample.contact,stage,0,tick,true,sample.source}; suppressed=0; return true; }
            if (sample.kind==Kind::Move && tick-entry->tick<250) { ++entry->suppressed; return false; }
            suppressed=std::exchange(entry->suppressed,0); entry->tick=tick;
            if (sample.kind==Kind::Up || sample.kind==Kind::Cancel) entry->used=false;
            return true;
        }
    };
    inline const char* SourceName(Source value) noexcept { return value==Source::Mouse?"mouse":value==Source::Pen?"pen":value==Source::Touch?"touch":"unknown"; }
    inline const char* KindName(Kind value) noexcept { return value==Kind::Down?"down":value==Kind::Up?"up":value==Kind::Cancel?"cancel":"move"; }
    inline const char* MessageName(UINT msg) noexcept
    {
        switch(msg) { case WM_TOUCH:return "WM_TOUCH"; case WM_POINTERDOWN:return "WM_POINTERDOWN"; case WM_POINTERUP:return "WM_POINTERUP"; case WM_POINTERUPDATE:return "WM_POINTERUPDATE"; case WM_POINTERLEAVE:return "WM_POINTERLEAVE"; case WM_POINTERENTER:return "WM_POINTERENTER"; case WM_POINTERCAPTURECHANGED:return "WM_POINTERCAPTURECHANGED"; case WM_LBUTTONDOWN:return "WM_LBUTTONDOWN"; case WM_LBUTTONUP:return "WM_LBUTTONUP"; case WM_LBUTTONDBLCLK:return "WM_LBUTTONDBLCLK"; case WM_MOUSEMOVE:return "WM_MOUSEMOVE"; case WM_RBUTTONDOWN:return "WM_RBUTTONDOWN"; case WM_RBUTTONUP:return "WM_RBUTTONUP"; case WM_RBUTTONDBLCLK:return "WM_RBUTTONDBLCLK"; case WM_MBUTTONDOWN:return "WM_MBUTTONDOWN"; case WM_MBUTTONUP:return "WM_MBUTTONUP"; case WM_MBUTTONDBLCLK:return "WM_MBUTTONDBLCLK"; case WM_XBUTTONDOWN:return "WM_XBUTTONDOWN"; case WM_XBUTTONUP:return "WM_XBUTTONUP"; case WM_XBUTTONDBLCLK:return "WM_XBUTTONDBLCLK"; case WM_CAPTURECHANGED:return "WM_CAPTURECHANGED"; case WM_CANCELMODE:return "WM_CANCELMODE"; case WM_MOUSELEAVE:return "WM_MOUSELEAVE"; default:return "other"; }
    }
    inline bool InputMessage(UINT msg) noexcept
    {
        return msg==WM_TOUCH || (msg>=WM_POINTERUPDATE && msg<=WM_POINTERLEAVE) || msg==WM_POINTERCAPTURECHANGED || msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP || msg==WM_LBUTTONDBLCLK || msg==WM_RBUTTONDOWN || msg==WM_RBUTTONUP || msg==WM_RBUTTONDBLCLK || msg==WM_MBUTTONDOWN || msg==WM_MBUTTONUP || msg==WM_MBUTTONDBLCLK || msg==WM_XBUTTONDOWN || msg==WM_XBUTTONUP || msg==WM_XBUTTONDBLCLK || msg==WM_MOUSEMOVE || msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || msg==WM_MOUSELEAVE;
    }
    inline constexpr Kind RawMessageKind(UINT msg) noexcept
    {
        switch(msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        case WM_POINTERDOWN: return Kind::Down;
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
        case WM_POINTERUP: return Kind::Up;
        case WM_CANCELMODE: case WM_CAPTURECHANGED: case WM_POINTERCAPTURECHANGED: case WM_POINTERLEAVE: return Kind::Cancel;
        default: return Kind::Move;
        }
    }
    inline thread_local MoveLimiter limiter;
    inline thread_local std::uint64_t rawSequence = 0;
    inline std::atomic<std::uint64_t> schemaGeneration{~std::uint64_t{0}};
    // 测试只替换诊断输出，不替换输入、命中或业务策略。
#if defined(DRAW3_CONTACT_TESTING)
    inline thread_local void (*testSink)(const char*,std::size_t) = nullptr;
#endif
    inline void Write(const char* line,std::size_t size) noexcept
    {
#if defined(DRAW3_CONTACT_TESTING)
        if(testSink) { testSink(line,size); return; }
#endif
        Drawing::Draw3::DiagnosticOutput::Write(Drawing::Draw3::DiagnosticOutput::Stream::Error,line,size);
    }
    template<class... Args> void Log(const char* role,HWND window,const char* stage,const char* reason,UINT message,const Sample* sample,const char* format,Args... args) noexcept
    {
        static_assert(((std::is_arithmetic_v<Args> || std::is_pointer_v<Args>) && ...), "UI diagnostic format arguments must be primitive values");
        if(!Enabled())return;
        ErrorScope preserve;
        const auto tick=GetTickCount64();
        const auto generation=Drawing::Draw3::HapticDiagnostics::generation.load(std::memory_order_relaxed);
        if(schemaGeneration.exchange(generation,std::memory_order_relaxed)!=generation) {
            constexpr char schema[]="[PenHaptics] event=ui_schema v=1 enabled=1 build=" __DATE__ "T" __TIME__ " move_interval_ms=250 limiter_slots=32\n";
            Write(schema,sizeof(schema)-1);
        }
        Sample move;
        if(!sample && (message==WM_MOUSEMOVE || message==WM_POINTERUPDATE)) { move.source=static_cast<Source>(-1); sample=&move; } // 限频占位不代表已识别为物理鼠标。
        unsigned suppressed=0, stageKey=2166136261u;
        for(const char* c=stage;*c;++c)stageKey=(stageKey^static_cast<unsigned char>(*c))*16777619u;
        if(sample && !limiter.Admit(reinterpret_cast<std::uintptr_t>(window),*sample,stageKey,tick,suppressed))return;
        char detail[768],line[1536];
        const int detailSize=std::snprintf(detail,sizeof(detail),format,args...);
        const int count=std::snprintf(line,sizeof(line),"[PenHaptics] event=ui_input v=1 tick=%llu tid=%lu role=%s hwnd=%p stage=%s reason=%s msg=%s/0x%x source=%s phase=%s contact=%lu generation=%llu device=0x%llx sample_tick=%llu time=%lu point=%ld,%ld suppressed=%u truncated=%u %s\n",
            static_cast<unsigned long long>(tick),GetCurrentThreadId(),role,window,stage,reason,MessageName(message),message,sample?SourceName(sample->source):"none",message==WM_TOUCH&&(!std::strcmp(stage,"raw")||!std::strcmp(stage,"touch_read"))?"batch":sample?KindName(sample->kind):"none",sample?sample->contact:0UL,static_cast<unsigned long long>(sample?sample->generation:0),static_cast<unsigned long long>(sample?sample->device:0),static_cast<unsigned long long>(sample?sample->tick:0),sample?sample->messageTime:0UL,sample?sample->screen.x:0L,sample?sample->screen.y:0L,suppressed,static_cast<unsigned>(detailSize<0||detailSize>=static_cast<int>(sizeof(detail))),detailSize<0?"format_failed":detail);
        if(count>0)Write(line,(std::min)(static_cast<std::size_t>(count),sizeof(line)-1));
    }
    // 仅供注册与 Pointer Down 旁路取证；函数替身不进入输入业务或改变窗口状态。
    struct NativeQueries
    {
        using PointerInfo = BOOL(WINAPI*)(UINT32, POINTER_INFO*);
        decltype(&::IsWindow) isWindow = ::IsWindow;
        decltype(&::IsWindowVisible) isVisible = ::IsWindowVisible;
        decltype(&::IsWindowEnabled) isEnabled = ::IsWindowEnabled;
        decltype(&::GetWindowThreadProcessId) owner = ::GetWindowThreadProcessId;
        decltype(&::IsTouchWindow) isTouch = ::IsTouchWindow;
        PointerInfo pointerInfo = nullptr;
    };
#if defined(DRAW3_CONTACT_TESTING)
    inline thread_local const NativeQueries* testNativeQueries = nullptr;
#endif
    inline const NativeQueries& WindowQueries() noexcept
    {
#if defined(DRAW3_CONTACT_TESTING)
        if(testNativeQueries)return *testNativeQueries;
#endif
        static const NativeQueries queries;
        return queries;
    }
    inline NativeQueries::PointerInfo PointerInfoQuery() noexcept
    {
#if defined(DRAW3_CONTACT_TESTING)
        if(testNativeQueries)return testNativeQueries->pointerInfo;
#endif
        // Win7 没有此导出；只在诊断开启的 Pointer Down 内首次解析，不建立硬导入。
        static const auto query = reinterpret_cast<NativeQueries::PointerInfo>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetPointerInfo"));
        return query;
    }
    // 阶段1实测探针：INKEYS_POINTER_PROBE=1 且诊断开启时，五个UI窗对接触阶段的触摸/笔 Pointer
    // 处理后不交 DefWindowProc，只验证系统是否因此不再派生 WM_TOUCH 与兼容 Mouse。
    // 不接入任何业务，也不改变悬停（非接触）消息；关闭时不查询、不分配、不格式化。
    namespace Probe
    {
#if defined(DRAW3_CONTACT_TESTING)
        inline thread_local int testEnvironment = -1;
#endif
        inline bool Requested() noexcept
        {
#if defined(DRAW3_CONTACT_TESTING)
            if(testEnvironment>=0)return testEnvironment!=0;
#endif
            static const bool requested=[]()noexcept{
                const DWORD error=GetLastError();wchar_t value[2]{};
                const bool result=GetEnvironmentVariableW(L"INKEYS_POINTER_PROBE",value,2)==1&&value[0]==L'1';
                SetLastError(error);return result;}();
            return requested;
        }
        inline bool Active() noexcept { return Requested()&&Enabled(); }
        struct Tracked { UINT32 id = 0; HWND window = nullptr; bool used = false; };
        struct State
        {
            std::mutex mutex;
            std::array<Tracked,16> contacts{};
            unsigned next = 0;
            std::atomic<unsigned> swallowed{0}, legacyTouch{0}, legacyMouse{0};
        };
        inline State& Shared() noexcept { static State state; return state; }
        // 只统计窗口线程入口看到的派生视图；Bar 的 channel_callback 副本不重复计数。
        inline void CountLegacy(UINT msg,ULONG_PTR extra) noexcept
        {
            if(!Active())return;
            if(msg==WM_TOUCH)++Shared().legacyTouch;
            else if((msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP)&&(extra&0xffffff00u)==0xff515700u)++Shared().legacyMouse;
        }
        inline void Announce(const char* role,HWND window) noexcept
        {
            if(!Active())return;
            ErrorScope preserve;
            Log(role,window,"probe_mode","enabled",0,nullptr,"probe=%d contact_pointer=return_without_defwindowproc hover=defwindowproc business=wm_touch_and_real_mouse_only",1);
        }
        // 返回 true 表示该接触阶段 Pointer 已在探针内吞掉，调用方直接 return 0。
        inline bool Swallow(const char* role,HWND window,UINT msg,WPARAM wParam) noexcept
        {
            if(!Active()||(msg!=WM_POINTERDOWN&&msg!=WM_POINTERUPDATE&&msg!=WM_POINTERUP))return false;
            ErrorScope preserve;
            const auto query=PointerInfoQuery();
            const UINT32 id=GET_POINTERID_WPARAM(wParam);
            POINTER_INFO info{};
            if(!query||!query(id,&info)||(info.pointerType!=PT_TOUCH&&info.pointerType!=PT_PEN))return false;
            // 悬停（未接触）仍沿原路径交给 DefWindowProc，与现有行为一致。
            if(msg==WM_POINTERUPDATE&&!(info.pointerFlags&POINTER_FLAG_INCONTACT))return false;
            auto& state=Shared();
            HWND downWindow=window;bool known=false;
            {
                std::lock_guard lock(state.mutex);
                Tracked* entry=nullptr;
                for(auto& item:state.contacts)if(item.used&&item.id==id){entry=&item;break;}
                known=entry!=nullptr;
                if(msg==WM_POINTERDOWN){
                    if(!entry)entry=&state.contacts[state.next++%state.contacts.size()];
                    *entry={id,window,true};
                }else if(entry){downWindow=entry->window;if(msg==WM_POINTERUP)entry->used=false;}
            }
            // 隐式捕获证据：抬起/移动是否仍回到按下窗口，以及此刻屏幕点是否已离开按下窗口矩形。
            RECT receiving{},down{};
            const POINT point=info.ptPixelLocation;
            const bool inReceiving=GetWindowRect(window,&receiving)&&PtInRect(&receiving,point);
            const bool inDown=GetWindowRect(downWindow,&down)&&PtInRect(&down,point);
            const unsigned swallowed=++state.swallowed;
            Sample sample;sample.source=info.pointerType==PT_PEN?Source::Pen:Source::Touch;
            sample.kind=msg==WM_POINTERDOWN?Kind::Down:msg==WM_POINTERUP?Kind::Up:Kind::Move;
            sample.contact=id;sample.screen=point;sample.messageTime=info.dwTime;
            sample.device=reinterpret_cast<std::uintptr_t>(info.sourceDevice);
            Log(role,window,"probe_contact",msg==WM_POINTERDOWN?"swallow_down":msg==WM_POINTERUP?"swallow_up":"swallow_update",msg,&sample,
                "pointer_flags=0x%x known_down=%d same_window=%d down_window=%p in_receiving_rect=%d in_down_rect=%d swallowed=%u legacy_touch_seen=%u legacy_signed_mouse_seen=%u",
                info.pointerFlags,known,window==downWindow,downWindow,inReceiving,inDown,swallowed,
                state.legacyTouch.load(std::memory_order_relaxed),state.legacyMouse.load(std::memory_order_relaxed));
            return true;
        }
    }
    inline void WindowSnapshot(const char* role,HWND window,const char* reason,UINT message,UINT32 pointerId=0) noexcept
    {
        if(!Enabled())return;
        ErrorScope preserve;
        const auto& query=WindowQueries();
        const BOOL valid=query.isWindow(window),visible=query.isVisible(window),enabled=query.isEnabled(window);
        DWORD ownerPid=0;SetLastError(ERROR_SUCCESS);
        const DWORD ownerTid=query.owner(window,&ownerPid);
        const DWORD ownerError=ownerTid?ERROR_SUCCESS:GetLastError();
        if(!ownerTid)ownerPid=0;
        ULONG flags=0;SetLastError(ERROR_SUCCESS);
        const BOOL touch=query.isTouch(window,&flags);
        // false 也可能只是未注册；只记录清零后观察值，不将旧 GLE 当作查询失败。
        const DWORD touchError=touch?ERROR_SUCCESS:GetLastError();
        Sample sample;sample.source=static_cast<Source>(-1);sample.kind=Kind::Down;sample.contact=pointerId;
        // 注册读回是状态快照，不伪装成一次输入Down。
        Log(role,window,"touch_window_snapshot",reason,message,message==WM_POINTERDOWN?&sample:nullptr,
            "call_tid=%lu owner_tid=%lu owner_pid=%lu owner_matches=%d owner_error=%lu is_window=%d visible=%d enabled=%d is_touch=%d is_touch_flags=0x%lx is_touch_flags_valid=%d is_touch_error=%lu error_basis=cleared_before_query received_touch=unknown",
            GetCurrentThreadId(),ownerTid,ownerPid,ownerTid!=0&&ownerTid==GetCurrentThreadId(),ownerError,
            valid,visible,enabled,touch,flags,touch!=FALSE,touchError);
    }
    inline void TouchRegistration(const char* role,HWND window,ULONG flags,BOOL result,DWORD failureError) noexcept
    {
        if(!Enabled())return;
        ErrorScope preserve;
        Log(role,window,"touch_registration",result?"registered":"register_failed",0,nullptr,
            "registration_result=%d registration_error=%lu registration_error_applicable=%d requested_flags=0x%lx call_tid=%lu",
            result,result?ERROR_SUCCESS:failureError,result==FALSE,flags,GetCurrentThreadId());
        WindowSnapshot(role,window,"after_registration",0);
        Probe::Announce(role,window);
    }
    inline void PointerDownSnapshot(const char* role,HWND window,UINT message,WPARAM wParam,const char* entry) noexcept
    {
        if(!Enabled()||message!=WM_POINTERDOWN||std::strcmp(entry,"wndproc"))return;
        ErrorScope preserve;
        const UINT32 id=GET_POINTERID_WPARAM(wParam);
        WindowSnapshot(role,window,"pointer_down",message,id);
        const auto query=PointerInfoQuery();
        POINTER_INFO info{};BOOL ok=FALSE;DWORD error=ERROR_SUCCESS;
        if(query){SetLastError(ERROR_SUCCESS);ok=query(id,&info);if(!ok){error=GetLastError();info={};}}
        Sample sample;sample.source=static_cast<Source>(-1);sample.kind=Kind::Down;sample.contact=id;
        if(ok){sample.source=info.pointerType==PT_PEN?Source::Pen:info.pointerType==PT_TOUCH?Source::Touch:info.pointerType==PT_MOUSE?Source::Mouse:static_cast<Source>(-1);
            sample.screen=info.ptPixelLocation;sample.messageTime=info.dwTime;sample.device=reinterpret_cast<std::uintptr_t>(info.sourceDevice);}
        Log(role,window,"pointer_info",!query?"api_unavailable":ok?"queried":"query_failed",message,&sample,
            "pointer_api=%d pointer_ok=%d pointer_error=%lu pointer_error_applicable=%d requested_pointer=%u info_pointer=%u pointer_type=%u pointer_flags=0x%x source_device=%p hwnd_target=%p frame=%u system_time=%lu pixel=%ld,%ld raw_pixel=%ld,%ld source_basis=GetPointerInfo touch_hsource=unknown",
            query!=nullptr,ok,error,query!=nullptr&&!ok,id,info.pointerId,static_cast<unsigned>(info.pointerType),info.pointerFlags,
            info.sourceDevice,info.hwndTarget,info.frameId,info.dwTime,info.ptPixelLocation.x,info.ptPixelLocation.y,
            info.ptPixelLocationRaw.x,info.ptPixelLocationRaw.y);
    }
    inline bool Raw(const char* role,HWND window,UINT msg,WPARAM wp,LPARAM lp,std::uintptr_t channel,const char* entry="wndproc") noexcept
    {
        if(!Enabled() || !InputMessage(msg))return false;
        ErrorScope preserve;
        PointerDownSnapshot(role,window,msg,wp,entry);
        const ULONG_PTR extra=GetMessageExtraInfo();
        if(!std::strcmp(entry,"wndproc"))Probe::CountLegacy(msg,extra);
        Sample sample; sample.messageTime=GetMessageTime(); sample.tick=GetTickCount64();
        sample.contact=(msg>=WM_POINTERUPDATE&&msg<=WM_POINTERLEAVE)?LOWORD(wp):0;
        sample.source=(extra&0xffffff00u)==0xff515700u ? ((extra&0x80)?Source::Touch:Source::Pen):Source::Mouse;
        if((msg>=WM_POINTERUPDATE&&msg<=WM_POINTERLEAVE)||msg==WM_POINTERCAPTURECHANGED)sample.source=static_cast<Source>(-1);
        sample.kind=RawMessageKind(msg);
        // 原始批次没有接触phase；入口Move限频，单次解码后的Down/Up逐点完整记录。
        if(msg==WM_TOUCH) { sample.source=Source::Touch; sample.kind=Kind::Move; }
        ++rawSequence;
        static thread_local MoveLimiter rawLimiter;
        unsigned rawSuppressed=0;
        if(!rawLimiter.Admit(reinterpret_cast<std::uintptr_t>(window),sample,0,sample.tick,rawSuppressed))return false;
        const DWORD pos=GetMessagePos(); sample.screen={static_cast<short>(LOWORD(pos)),static_cast<short>(HIWORD(pos))};
        RECT rect{};const BOOL rectOk=GetWindowRect(window,&rect);
        Log(role,window,"raw","arrived",msg,&sample,"entry=%s batch=%llu raw_suppressed=%u point_origin=GetMessagePos time_origin=GetMessageTime channel=0x%llx wparam=0x%llx lparam=0x%llx extra=0x%llx touch_count=%u capture=%p rect_ok=%d rect=%ld,%ld,%ld,%ld",entry,static_cast<unsigned long long>(rawSequence),rawSuppressed,static_cast<unsigned long long>(channel),static_cast<unsigned long long>(wp),static_cast<unsigned long long>(lp),static_cast<unsigned long long>(extra),static_cast<unsigned>(msg==WM_TOUCH?LOWORD(wp):0),GetCapture(),rectOk,rect.left,rect.top,rect.right,rect.bottom);
        return true;
    }
    struct LeaseEvent
    {
        bool active = Enabled();
        const char* stage; const char* reason="no_state";
        std::uintptr_t requester,owner=0; std::uint64_t requested,owned=0;
        explicit LeaseEvent(const char* event,std::uintptr_t channel,std::uint64_t generation) noexcept:stage(event),requester(channel),requested(generation){}
        ~LeaseEvent() noexcept { if(active)Log("provider",nullptr,stage,reason,0,nullptr,"owner_snapshot=before_mutation requester=0x%llx owner=0x%llx requested_generation=%llu owner_generation=%llu",static_cast<unsigned long long>(requester),static_cast<unsigned long long>(owner),static_cast<unsigned long long>(requested),static_cast<unsigned long long>(owned)); }
    };
}
