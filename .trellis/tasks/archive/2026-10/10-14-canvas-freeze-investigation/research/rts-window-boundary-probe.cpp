// 独立 OS 实验：真正 RTS 回调 + 窗口边界，不直接调用 StylusDown/Up，不修改产品。
// 编译交给主会话；需 Windows SDK，链接 user32/gdi32/ole32/uuid。
// 运行会显示两个测试窗口并注入输入，必须由已授权的主会话执行。
#define NOMINMAX
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tchar.h>
#include <rtscom.h>
#include <tpcshrd.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;
struct Event {
    std::atomic_bool ready{false};
    ULONGLONG tick{};
    DWORD tid{};
    const char* name{};
    uint64_t a{}, b{}, c{}, d{};
};
static std::array<Event, 32768> events;
static std::atomic_uint eventCount{};
static std::atomic_uint downCount{}, upCount{}, disabledCount{};
static std::atomic_int phase{};
static std::atomic_bool rtsReady{}, stopRts{};
static HWND canvas{}, other{};
static bool noActivate = true, layered = false, topmost = false;
static bool inputHeld = false, injectionFailed = false;
static std::wstring action = L"cover", device = L"mouse", outPath;
static UINT32 touchId = 0;
static ULONGLONG started{};
static POINT contactPoint{};
static unsigned step{};

static uint64_t H(HWND h) { return reinterpret_cast<uintptr_t>(h); }
static void Log(const char* name, uint64_t a=0, uint64_t b=0, uint64_t c=0, uint64_t d=0) {
    const unsigned i = eventCount.fetch_add(1, std::memory_order_relaxed);
    if (i >= events.size()) return; // 回调只写预分配内存；不等待输出或锁。
    auto& e = events[i]; e.tick=GetTickCount64(); e.tid=GetCurrentThreadId();
    e.name=name; e.a=a; e.b=b; e.c=c; e.d=d;
    e.ready.store(true, std::memory_order_release);
}
static void State(const char* tag) {
    GUITHREADINFO info{sizeof(info)};
    const BOOL ok=GetGUIThreadInfo(GetCurrentThreadId(), &info);
    Log(tag,H(GetForegroundWindow()),H(GetCapture()),H(info.hwndFocus),ok);
    Log("canvas-state",H(canvas),IsWindowVisible(canvas),IsWindowEnabled(canvas),IsIconic(canvas));
    Log("counts",downCount.load(),upCount.load(),disabledCount.load(),phase.load());
}

class Plugin final : public IStylusSyncPlugin {
    std::atomic<ULONG> refs{1};
    ComPtr<IUnknown> marshaler;
    HRESULT marshalResult;
public:
    Plugin() : marshalResult(CoCreateFreeThreadedMarshaler(this, &marshaler)) {}
    HRESULT Ready() const { return marshalResult; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** p) override {
        if(!p) return E_POINTER; *p=nullptr;
        if(iid==__uuidof(IMarshal) && marshaler) return marshaler->QueryInterface(iid,p);
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IStylusPlugin)||iid==__uuidof(IStylusSyncPlugin)) {
            *p=static_cast<IStylusSyncPlugin*>(this); AddRef(); return S_OK;
        } return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE DataInterest(RealTimeStylusDataInterest* p) override {
        if(!p) return E_POINTER;
        *p=static_cast<RealTimeStylusDataInterest>(RTSDI_RealTimeStylusEnabled|RTSDI_RealTimeStylusDisabled|
            RTSDI_StylusInRange|RTSDI_StylusOutOfRange|RTSDI_StylusDown|RTSDI_StylusUp|
            RTSDI_Packets|RTSDI_InAirPackets|RTSDI_SystemEvents|RTSDI_TabletAdded|RTSDI_TabletRemoved|
            RTSDI_UpdateMapping|RTSDI_Error); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RealTimeStylusEnabled(IRealTimeStylus*,ULONG n,const TABLET_CONTEXT_ID* ids) override {
        Log("RTS-enabled",n,phase.load()); for(ULONG i=0;ids && i<n;++i) Log("RTS-context",ids[i]); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RealTimeStylusDisabled(IRealTimeStylus*,ULONG n,const TABLET_CONTEXT_ID*) override {
        ++disabledCount; Log("RTS-disabled",n,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StylusInRange(IRealTimeStylus*,TABLET_CONTEXT_ID t,STYLUS_ID s) override {
        Log("RTS-range-in",t,s,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StylusOutOfRange(IRealTimeStylus*,TABLET_CONTEXT_ID t,STYLUS_ID s) override {
        Log("RTS-range-out",t,s,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StylusDown(IRealTimeStylus*,const StylusInfo* s,ULONG n,LONG*,LONG**) override {
        ++downCount; Log("RTS-down",s?s->tcid:0,s?s->cid:0,n,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StylusUp(IRealTimeStylus*,const StylusInfo* s,ULONG n,LONG* p,LONG**) override {
        ++upCount; Log("RTS-up",s?s->tcid:0,s?s->cid:0,n,(uint64_t(phase.load())<<1)|(p?1u:0u)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StylusButtonDown(IRealTimeStylus*,STYLUS_ID s,const GUID*,POINT*) override {
        Log("RTS-button-down",s); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StylusButtonUp(IRealTimeStylus*,STYLUS_ID s,const GUID*,POINT*) override {
        Log("RTS-button-up",s); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Packets(IRealTimeStylus*,const StylusInfo* s,ULONG n,ULONG,LONG*,ULONG*,LONG**) override {
        Log("RTS-packets",s?s->tcid:0,s?s->cid:0,n,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InAirPackets(IRealTimeStylus*,const StylusInfo* s,ULONG n,ULONG,LONG*,ULONG*,LONG**) override {
        Log("RTS-air",s?s->tcid:0,s?s->cid:0,n,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CustomStylusDataAdded(IRealTimeStylus*,const GUID*,ULONG,const BYTE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SystemEvent(IRealTimeStylus*,TABLET_CONTEXT_ID t,STYLUS_ID s,SYSTEM_EVENT e,SYSTEM_EVENT_DATA) override {
        Log("RTS-system",t,s,e,phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TabletAdded(IRealTimeStylus*,IInkTablet*) override { Log("RTS-tablet-added"); return S_OK; }
    HRESULT STDMETHODCALLTYPE TabletRemoved(IRealTimeStylus*,LONG n) override { Log("RTS-tablet-removed",n); return S_OK; }
    HRESULT STDMETHODCALLTYPE Error(IRealTimeStylus*,IStylusPlugin*,RealTimeStylusDataInterest i,HRESULT e,LONG_PTR*) override {
        Log("RTS-error",i,uint32_t(e),phase.load()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateMapping(IRealTimeStylus*) override { Log("RTS-mapping",phase.load()); return S_OK; }
};

static void RtsThread() {
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    Log("COM-init",uint32_t(co));
    if(FAILED(co)) return;
    ComPtr<IRealTimeStylus> rts;
    HRESULT hr=CoCreateInstance(__uuidof(RealTimeStylus),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&rts));
    Log("RTS-create",uint32_t(hr));
    if(SUCCEEDED(hr)) { hr=rts->put_HWND(reinterpret_cast<HANDLE_PTR>(canvas)); Log("RTS-bind",uint32_t(hr)); }
    if(SUCCEEDED(hr)) { hr=rts->SetAllTabletsMode(TRUE); Log("RTS-all-tablets",uint32_t(hr)); }
    ComPtr<IRealTimeStylus2> rts2;
    if(rts && SUCCEEDED(rts.As(&rts2))) Log("RTS-flicks-off",uint32_t(rts2->put_FlicksEnabled(FALSE)));
    ComPtr<IRealTimeStylus3> rts3;
    if(rts && SUCCEEDED(rts.As(&rts3))) Log("RTS-multitouch",uint32_t(rts3->put_MultiTouchEnabled(TRUE)));
    ComPtr<Plugin> plugin; plugin.Attach(new Plugin());
    bool added=false;
    if(SUCCEEDED(hr)) { hr=plugin->Ready(); Log("RTS-marshaler",uint32_t(hr)); }
    if(SUCCEEDED(hr)) { hr=rts->AddStylusSyncPlugin(0,plugin.Get()); added=SUCCEEDED(hr); Log("RTS-add",uint32_t(hr)); }
    if(SUCCEEDED(hr)) { hr=rts->put_Enabled(TRUE); Log("RTS-enable-return",uint32_t(hr)); }
    if(SUCCEEDED(hr)) rtsReady.store(true,std::memory_order_release);
    while(!stopRts.load(std::memory_order_acquire)) Sleep(20);
    if(rts && added) {
        Log("RTS-stop-begin"); Log("RTS-stop-return",uint32_t(rts->put_Enabled(FALSE)));
        ComPtr<IStylusSyncPlugin> removed;
        Log("RTS-remove-return",uint32_t(rts->RemoveStylusSyncPlugin(0,&removed)));
    }
    rts3.Reset(); rts2.Reset(); rts.Reset(); plugin.Reset(); CoUninitialize();
}

// 本探针的每次 Down 都配真实 OS Up；触摸保持接触时周期性 UPDATE，避免注入超时被取消。
static void Inject(unsigned kind, POINT p) { // 0=down,1=move,2=up
    if(device==L"manual") return;
    if(kind==0 && inputHeld) { Log("invalid-double-down"); injectionFailed=true; return; }
    if(kind!=0 && !inputHeld) return;
    BOOL ok=FALSE; DWORD error=0;
    if(device==L"touch") {
        if(kind==2) p=contactPoint; // SDK 合同：UP 坐标必须等于上一有效 UPDATE。
        // 单触点实验默认使用 0；可显式重放原 id=7，隔离注入上下文容量/ID 的影响。
        POINTER_TOUCH_INFO t{}; t.pointerInfo.pointerType=PT_TOUCH; t.pointerInfo.pointerId=touchId;
        t.pointerInfo.ptPixelLocation=p;
        t.pointerInfo.pointerFlags=kind==2 ? POINTER_FLAG_UP :
            POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|(kind==0?POINTER_FLAG_DOWN:POINTER_FLAG_UPDATE);
        t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;
        t.rcContact={p.x-4,p.y-4,p.x+4,p.y+4}; t.orientation=90; t.pressure=512;
        if(kind!=1) {
            Log("touch-parameters",touchId,t.pointerInfo.pointerFlags,uint32_t(p.x),uint32_t(p.y));
            Log("touch-optional",t.touchMask,t.touchFlags,t.orientation,t.pressure);
        }
        ok=InjectTouchInput(1,&t); error=ok?0:GetLastError();
    } else {
        INPUT in[2]{}; in[0].type=INPUT_MOUSE;
        const int left=GetSystemMetrics(SM_XVIRTUALSCREEN), top=GetSystemMetrics(SM_YVIRTUALSCREEN);
        in[0].mi.dx=MulDiv(p.x-left,65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
        in[0].mi.dy=MulDiv(p.y-top,65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
        in[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
        unsigned count=1;
        if(kind!=1) { count=2; in[1].type=INPUT_MOUSE; in[1].mi.dwFlags=kind==0?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP; }
        ok=SendInput(count,in,sizeof(INPUT))==count; error=ok?0:GetLastError();
    }
    Log(kind==0?"inject-down":kind==1?"inject-move":"inject-up",ok,error,phase.load(),device==L"touch");
    if(ok) { contactPoint=p; inputHeld=kind!=2; }
    else { injectionFailed=true; Log("injection-failure-invalidates-trial",kind,error); }
}

static void Boundary() {
    phase=2; State("before-boundary");
    if(action==L"hide") ShowWindow(canvas,SW_HIDE);
    else if(action==L"disable") EnableWindow(canvas,FALSE);
    else if(action==L"minimize") ShowWindow(canvas,SW_MINIMIZE);
    else if(action==L"capture") { SetCapture(canvas); Log("capture-established",H(GetCapture())); SetCapture(other); }
    else if(action==L"cancel") { SetCapture(canvas); SendMessageW(canvas,WM_CANCELMODE,0,0); }
    else if(action==L"cover") { SetWindowPos(other,HWND_TOPMOST,100,100,700,500,SWP_SHOWWINDOW); SetForegroundWindow(other); }
    else if(action==L"focus") { ShowWindow(other,SW_SHOWNORMAL); SetForegroundWindow(other); }
    else if(action==L"leave") Inject(1,{GetSystemMetrics(SM_CXSCREEN)-20,GetSystemMetrics(SM_CYSCREEN)-50});
    State("after-boundary");
}
static void Restore() {
    phase=3;
    if(GetCapture()==canvas || GetCapture()==other) ReleaseCapture();
    EnableWindow(canvas,TRUE);
    ShowWindow(other,SW_HIDE);
    ShowWindow(canvas,SW_SHOWNOACTIVATE);
    SetWindowPos(canvas,topmost?HWND_TOPMOST:HWND_TOP,100,100,700,500,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    State("restored-without-explicit-focus");
}
static POINT At(int x,int y) { POINT p{x,y}; ClientToScreen(canvas,&p); return p; }
static void Timer() {
    const ULONGLONG elapsed=GetTickCount64()-started;
    if(device==L"manual") { if(elapsed>=30000) PostQuitMessage(0); return; }
    if(!rtsReady.load(std::memory_order_acquire)) { if(elapsed>=10000) PostQuitMessage(2); return; }
    static ULONGLONG readyTick=GetTickCount64();
    const ULONGLONG t=GetTickCount64()-readyTick;
    // 每步均交还 message pump，避免一次顺序 SendInput 被随后窗口操作追越。
    static const ULONGLONG when[]{1000,1200,1600,2400,2600,3000,3400,3800,4400,5000,5200,5600,6500,7000,7200,7600,8500};
    if(step<std::size(when) && t>=when[step]) {
        Log("script-step",step,t);
        switch(step++) {
        case 0: phase=0; Inject(0,At(100,140)); break;
        case 1: Inject(1,At(240,160)); break;
        case 2: Inject(2,contactPoint); State("baseline-finished"); break;
        case 3: phase=1; Inject(0,At(100,240)); break;
        case 4: Inject(1,At(220,260)); break;
        case 5: Boundary(); break;
        case 6: Inject(1,contactPoint); break;
        case 7: Inject(2,contactPoint); State("boundary-up-issued"); break;
        case 8: Restore(); break;
        case 9: Inject(0,At(100,340)); break;
        case 10: Inject(1,At(240,360)); break;
        case 11: Inject(2,contactPoint); State("first-new-contact-finished"); break;
        case 12: phase=4; SetForegroundWindow(canvas); State("explicit-focus-recovery"); break;
        case 13: Inject(0,At(300,340)); break;
        case 14: Inject(1,At(400,360)); break;
        case 15: Inject(2,contactPoint); State("focused-new-contact-finished"); break;
        case 16: PostQuitMessage(injectionFailed?3:0); break;
        }
    } else if(inputHeld && device==L"touch") Inject(1,contactPoint);
}

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_MOUSEACTIVATE: Log("WM_MOUSEACTIVATE",H(h),w,l); if(h==canvas && noActivate) return MA_NOACTIVATE; break;
    case WM_ACTIVATE: case WM_ACTIVATEAPP: case WM_KILLFOCUS: case WM_SETFOCUS:
    case WM_CANCELMODE: case WM_CAPTURECHANGED: case WM_POINTERCAPTURECHANGED:
    case WM_SHOWWINDOW: case WM_ENABLE: case WM_SIZE: case WM_DISPLAYCHANGE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_POINTERDOWN: case WM_POINTERUP:
    case WM_POINTERENTER: case WM_POINTERLEAVE:
        Log("window-message",H(h),m,w,static_cast<uintptr_t>(l)); break;
    case WM_TIMER: if(h==canvas) Timer(); return 0;
    case WM_KEYDOWN:
        if(w=='B') Boundary(); else if(w=='R') Restore(); else if(w=='F') SetForegroundWindow(canvas);
        else if(w==VK_ESCAPE) PostQuitMessage(0); break;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT p{}; HDC dc=BeginPaint(h,&p); RECT r{}; GetClientRect(h,&r);
        FillRect(dc,&r,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
        const wchar_t* label=h==canvas?L"RTS boundary probe: B=boundary R=restore F=focus Esc=stop":L"Probe obstacle window";
        DrawTextW(dc,label,-1,&r,DT_TOP|DT_LEFT); EndPaint(h,&p); return 0;
    }
    }
    return DefWindowProcW(h,m,w,l);
}
int wmain(int argc,wchar_t** argv) {
    for(int i=1;i<argc;++i) {
        if(!wcscmp(argv[i],L"--action") && i+1<argc) action=argv[++i];
        else if(!wcscmp(argv[i],L"--device") && i+1<argc) device=argv[++i];
        else if(!wcscmp(argv[i],L"--out") && i+1<argc) outPath=argv[++i];
        else if(!wcscmp(argv[i],L"--touch-id") && i+1<argc) {
            wchar_t* end{}; const unsigned long parsed=wcstoul(argv[++i],&end,10);
            if(!end || *end || parsed>255) return 64; touchId=static_cast<UINT32>(parsed);
        }
        else if(!wcscmp(argv[i],L"--activate")) noActivate=false;
        else if(!wcscmp(argv[i],L"--layered")) layered=true;
        else if(!wcscmp(argv[i],L"--topmost")) topmost=true;
        else return 64;
    }
    if(outPath.empty()) return 64;
    if(device!=L"mouse" && device!=L"touch" && device!=L"manual") return 64;
    if(action!=L"none" && action!=L"hide" && action!=L"disable" && action!=L"minimize" &&
       action!=L"capture" && action!=L"cancel" && action!=L"cover" && action!=L"focus" && action!=L"leave") return 64;
    SetProcessDPIAware();
    if(device==L"touch") {
        const BOOL ok=InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE);
        Log("touch-initialization",ok,ok?0:GetLastError(),1,touchId);
        Log("touch-structure-sizes",sizeof(POINTER_TOUCH_INFO),sizeof(POINTER_INFO));
        Log("virtual-screen",uint32_t(GetSystemMetrics(SM_XVIRTUALSCREEN)),uint32_t(GetSystemMetrics(SM_YVIRTUALSCREEN)),
            GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN));
        if(!ok) return 65;
    }
    WNDCLASSW wc{}; wc.lpfnWndProc=Proc; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"InkeysRtsBoundaryProbe"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc)) return 66;
    DWORD ex=(noActivate?WS_EX_NOACTIVATE:0)|(layered?WS_EX_LAYERED:0)|(topmost?WS_EX_TOPMOST:0);
    canvas=CreateWindowExW(ex,wc.lpszClassName,L"RTS boundary canvas",WS_POPUP|WS_BORDER,100,100,700,500,nullptr,nullptr,wc.hInstance,nullptr);
    other=CreateWindowExW(0,wc.lpszClassName,L"RTS obstacle",WS_OVERLAPPEDWINDOW,850,100,500,400,nullptr,nullptr,wc.hInstance,nullptr);
    if(!canvas || !other) return 67;
    if(layered) SetLayeredWindowAttributes(canvas,0,255,LWA_ALPHA);
    const ULONG_PTR flags=TABLET_DISABLE_PRESSANDHOLD|TABLET_DISABLE_PENTAPFEEDBACK|TABLET_DISABLE_PENBARRELFEEDBACK|TABLET_DISABLE_FLICKS|TABLET_ENABLE_MULTITOUCHDATA;
    SetPropW(canvas,MICROSOFT_TABLETPENSERVICE_PROPERTY,reinterpret_cast<HANDLE>(flags));
    ShowWindow(canvas,noActivate?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
    State("initial"); Log("flags",ex,flags,GetSystemMetrics(SM_DIGITIZER),GetSystemMetrics(SM_MAXIMUMTOUCHES));
    started=GetTickCount64(); std::thread worker(RtsThread); SetTimer(canvas,1,50,nullptr);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    const int scriptResult=static_cast<int>(msg.wParam);
    KillTimer(canvas,1); if(inputHeld) Inject(2,contactPoint);
    // RTS 的停止/移除可能同步发窗口消息；等待 worker 时仍泵本线程消息。
    stopRts.store(true,std::memory_order_release);
    static std::atomic_bool joined{};
    std::thread joiner([&]{worker.join();joined=true;});
    while(!joined.load()) {
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
    joiner.join(); State("stopped"); DestroyWindow(other); DestroyWindow(canvas);
    FILE* file{}; if(_wfopen_s(&file,outPath.c_str(),L"wb") || !file) return 68;
    fprintf(file,"# action=%ls device=%ls noactivate=%u layered=%u topmost=%u injectionFailed=%u\n",action.c_str(),device.c_str(),noActivate,layered,topmost,injectionFailed);
    fprintf(file,"index,tick,tid,event,a,b,c,d\n");
    const unsigned total=eventCount.load();
    for(unsigned i=0;i<std::min<unsigned>(total,static_cast<unsigned>(events.size()));++i) {
        const auto& e=events[i]; if(e.ready.load(std::memory_order_acquire))
            fprintf(file,"%u,%llu,%lu,%s,%llu,%llu,%llu,%llu\n",i,e.tick,e.tid,e.name,e.a,e.b,e.c,e.d);
    }
    fprintf(file,"# total=%u capacity=%zu down=%u up=%u disabled=%u\n",total,events.size(),downCount.load(),upCount.load(),disabledCount.load());
    fclose(file); return injectionFailed?3:scriptResult;
}
