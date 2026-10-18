// 独立真实 USER32 / 生产 ImGui backend 探针；不是完整 Inkeys/RTS 复现。
// 编译时链接仓库的 imgui.cpp/imgui_draw.cpp/imgui_tables.cpp/imgui_widgets.cpp/imgui_impl_win32.cpp。
// 不挂起线程、不修改其他进程；20 秒看门狗只结束本探针。
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "Window.CommandCompletion.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(
    HWND, UINT, WPARAM, LPARAM, ImGuiIO&);

namespace {
struct Work { std::function<bool()> call; std::shared_ptr<std::promise<bool>> done; };
struct Group {
    HWND hwnd = nullptr;
    DWORD tid = 0;
    HANDLE wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::mutex mutex;
    std::deque<Work> queue;
    std::atomic<bool> stop = false;
    std::thread thread;
};
Group root, setting;
std::atomic<bool> logicalVisible = false;
std::atomic<bool> finished = false;
std::atomic<unsigned> stage = 0;
std::atomic<unsigned> settingMouseDown = 0, settingMouseUp = 0, rootMouseDown = 0;
std::atomic<unsigned> captureChanged = 0, cancelMode = 0;
std::recursive_mutex imguiMutex;

void Snapshot(const char* label, bool owned) {
    GUITHREADINFO s{sizeof(s)}, r{sizeof(r)};
    const BOOL sok = GetGUIThreadInfo(setting.tid, &s);
    const BOOL rok = GetGUIThreadInfo(root.tid, &r);
    std::printf("SNAP label=%s owned=%d logical=%d visible=%d owner=%p foreground=%p "
        "setting_ok=%d active=%p focus=%p capture=%p flags=%lu "
        "root_ok=%d root_capture=%p mouse=%u/%u root_down=%u capture_changed=%u cancel=%u\n",
        label, owned, logicalVisible.load(), IsWindowVisible(setting.hwnd),
        static_cast<void*>(GetWindow(setting.hwnd, GW_OWNER)),
        static_cast<void*>(GetForegroundWindow()), sok,
        static_cast<void*>(s.hwndActive), static_cast<void*>(s.hwndFocus),
        static_cast<void*>(s.hwndCapture), s.flags, rok,
        static_cast<void*>(r.hwndCapture), settingMouseDown.load(), settingMouseUp.load(),
        rootMouseDown.load(), captureChanged.load(), cancelMode.load());
    std::fflush(stdout);
}

LRESULT CALLBACK RootProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_LBUTTONDOWN) ++rootMouseDown;
    // 与透明产品画布相同，普通鼠标点击不激活它。
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK SettingProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_LBUTTONDOWN) ++settingMouseDown;
    if (msg == WM_LBUTTONUP) ++settingMouseUp;
    if (msg == WM_CAPTURECHANGED) ++captureChanged;
    if (msg == WM_CANCELMODE) ++cancelMode;
    // 对应 Setting.cpp:610–617；调用的 backend 是仓库真实实现。
    if (logicalVisible.load()) {
        std::lock_guard lock(imguiMutex);
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandlerEx(
            hwnd, msg, wp, lp, ImGui::GetIO())) return TRUE;
    }
    // 模拟 WM_CLOSE 的即时逻辑关闭；原产品实际 HWND Hide 由 worker 排队。
    if (msg == WM_CLOSE) { logicalVisible = false; return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Submit(Group& group, std::function<bool()> call) {
    if (GetCurrentThreadId() == group.tid) return call();
    Work work{std::move(call), std::make_shared<std::promise<bool>>()};
    auto result = work.done->get_future();
    {
        std::lock_guard lock(group.mutex);
        group.queue.push_back(std::move(work));
    }
    SetEvent(group.wake);
    // 直接复用生产 helper，默认 deadline=nullopt，探针外部看门狗限制整个实验。
    return Inkeys::Window::Detail::WaitForCommandResult(result, std::nullopt);
}

void Run(Group& group, bool isSetting, std::promise<bool> ready) {
    group.tid = GetCurrentThreadId();
    const wchar_t* name = isSetting ? L"OwnerCaptureSettingProbe" : L"OwnerCaptureRootProbe";
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name;
    wc.lpfnWndProc = isSetting ? SettingProc : RootProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    group.hwnd = CreateWindowExW(isSetting ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        name, isSetting ? L"Inkeys research: Setting capture" : L"Inkeys research: Drawpad model",
        WS_POPUP | WS_CLIPCHILDREN | WS_BORDER, isSetting ? 220 : 60, 80, 320, 220,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!group.hwnd) { ready.set_value(false); return; }
    if (isSetting) {
        std::lock_guard lock(imguiMutex);
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        if (!ImGui_ImplWin32_Init(group.hwnd)) { ready.set_value(false); return; }
    }
    ShowWindow(group.hwnd, isSetting ? SW_HIDE : SW_SHOWNOACTIVATE);
    ready.set_value(true);
    // 复现 Window.cpp:913–935 的外层 event queue + message pump 形状。
    while (!group.stop.load()) {
        const auto wait = MsgWaitForMultipleObjectsEx(1, &group.wake, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0) {
            ResetEvent(group.wake);
            std::deque<Work> work;
            { std::lock_guard lock(group.mutex); work.swap(group.queue); }
            for (auto& item : work) {
                bool ok = false;
                try { ok = item.call(); } catch (...) {}
                item.done->set_value(ok);
            }
        }
        if (wait == WAIT_FAILED) break;
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }
    if (isSetting) {
        logicalVisible = false;
        if (GetCapture() == group.hwnd) ReleaseCapture();
        std::lock_guard lock(imguiMutex);
        ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    }
    DestroyWindow(group.hwnd);
    UnregisterClassW(name, wc.hInstance);
}

bool Start(Group& group, bool isSetting) {
    std::promise<bool> promise;
    auto ready = promise.get_future();
    group.thread = std::thread([&group, isSetting, p=std::move(promise)]() mutable {
        Run(group, isSetting, std::move(p));
    });
    return ready.get();
}

void Stop(Group& group) {
    group.stop = true; SetEvent(group.wake);
    if (group.thread.joinable()) group.thread.join();
    CloseHandle(group.wake);
}
}

int main(int argc, char** argv) {
    std::thread watchdog([] {
        for (int i=0; i<200 && !finished.load(); ++i) Sleep(100);
        if (!finished.load()) {
            std::fprintf(stderr,"TIMEOUT own_probe_only stage=%u\n",stage.load());
            std::fflush(stderr);
            TerminateProcess(GetCurrentProcess(),124);
        }
    });
    bool started = Start(root,false) && Start(setting,true);
    unsigned residualCases = 0, failedPreconditions = 0;
    if (started) for (const bool owned : {false,true}) {
        stage = owned ? 20 : 10;
        Submit(setting,[&] {
            if (GetCapture()==setting.hwnd) ReleaseCapture();
            SetLastError(ERROR_SUCCESS);
            SetWindowLongPtrW(setting.hwnd,GWLP_HWNDPARENT,
                reinterpret_cast<LONG_PTR>(owned ? root.hwnd : nullptr));
            if (GetWindow(setting.hwnd,GW_OWNER)!=(owned ? root.hwnd : nullptr)) return false;
            logicalVisible = true;
            ShowWindow(setting.hwnd,SW_SHOW);
            SetForegroundWindow(setting.hwnd); SetActiveWindow(setting.hwnd); SetFocus(setting.hwnd);
            return true;
        });
        Snapshot("show",owned);
        // SendMessage 明确构造 backend Down/Up 顺序，不冒充实际硬件/RTS 包。
        if (!Submit(setting,[] {
            SendMessageW(setting.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,80));
            return GetCapture()==setting.hwnd;
        })) ++failedPreconditions;
        Snapshot("down",owned);
        ++stage;
        logicalVisible = false;
        Snapshot("logical-hide-before-native",owned);
        Submit(setting,[] { ShowWindow(setting.hwnd,SW_HIDE); return true; });
        Snapshot("native-hide",owned);
        ++stage;
        Submit(setting,[] {
            SendMessageW(setting.hwnd,WM_LBUTTONUP,0,MAKELPARAM(40,80));
            return true;
        });
        Snapshot("up-after-hide",owned);
        GUITHREADINFO info{sizeof(info)};
        if(GetGUIThreadInfo(setting.tid,&info) && info.hwndCapture==setting.hwnd) ++residualCases;
        // 可恢复性对照：显式取消模式走真实 DefWindowProc，观测而不假定它会释放。
        Submit(setting,[] { SendMessageW(setting.hwnd,WM_CANCELMODE,0,0); return true; });
        Snapshot("after-cancelmode",owned);
        Submit(setting,[] {
            if (GetCapture()==setting.hwnd) ReleaseCapture();
            SetWindowLongPtrW(setting.hwnd,GWLP_HWNDPARENT,0);
            return true;
        });
    }
    if (started && argc > 1 && std::strcmp(argv[1],"--owner-stress")==0) {
        // 不制造暂停或持锁；仅使两个正常窗口命令队列自然交错。
        // Setting 始终隐藏，观测隐藏 owner 的反复附加/分离与根置顶同步消息。
        stage = 40;
        std::atomic<unsigned> ownerFailures=0, topmostFailures=0;
        std::thread ownerRequests([&] {
            for(unsigned i=0;i<200;++i) {
                if(!Submit(setting,[i] {
                    const HWND desired=(i%2)==0 ? root.hwnd : nullptr;
                    SetWindowLongPtrW(setting.hwnd,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(desired));
                    if(!desired) SetWindowPos(setting.hwnd,HWND_NOTOPMOST,0,0,0,0,
                        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                    return GetWindow(setting.hwnd,GW_OWNER)==desired;
                })) ++ownerFailures;
            }
        });
        std::thread rootRequests([&] {
            for(unsigned i=0;i<200;++i) {
                if(!Submit(root,[i] {
                    return SetWindowPos(root.hwnd,(i%2)==0 ? HWND_TOPMOST : HWND_NOTOPMOST,
                        0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE;
                })) ++topmostFailures;
            }
        });
        ownerRequests.join(); rootRequests.join();
        std::printf("STRESS owner_completed=200 topmost_completed=200 owner_failures=%u topmost_failures=%u\n",
            ownerFailures.load(),topmostFailures.load());
        std::fflush(stdout);
    }
    stage = 99;
    if (setting.thread.joinable()) Stop(setting);
    if (root.thread.joinable()) Stop(root);
    finished=true; watchdog.join();
    std::printf("RESULT started=%d precondition_failures=%u residual_hidden_capture_cases=%u "
        "scope=real_user32_and_production_imgui_backend_not_full_product\n",
        started,failedPreconditions,residualCases);
    return !started || failedPreconditions ? 2 : 0;
}
