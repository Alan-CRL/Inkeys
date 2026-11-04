#include "../Inkeys/Inkeys/Input/UiPointerInput.h"
#include "../Inkeys/Inkeys/Input/UiTouchRouting.h"
#include "../Inkeys/Inkeys/Input/UiPointerDiagnostics.h"
#include <string>
#include <array>
#include <functional>
#include <initializer_list>
#include <vector>

namespace { std::string uiDiagnosticText; bool uiDiagnosticProviderReadable=false; }
#include "../Inkeys/Inkeys/UI/Bar/Bar.Haptics.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.BusyRing.h"
#include "../Inkeys/Inkeys/Window/Window.MessageDispatch.h"

#include <iostream>

import Inkeys.Message;

namespace
{
    namespace UiDiag = Inkeys::Input::UiPointer::Diagnostics;
    struct NativeDiagnosticFixture;
    NativeDiagnosticFixture* currentNativeDiagnosticFixture = nullptr;
    // 只替换新增诊断的只读 native 查询；假 HWND 不传给真实窗口 API。
    struct NativeDiagnosticFixture
    {
        UiDiag::NativeQueries queries;
        const UiDiag::NativeQueries* previous = UiDiag::testNativeQueries;
        NativeDiagnosticFixture* previousFixture = currentNativeDiagnosticFixture;
        unsigned windowQueries=0,ownerQueries=0,touchQueries=0,pointerQueries=0;
        BOOL windowValid=TRUE,visible=TRUE,enabled=TRUE,touch=TRUE,pointerOk=TRUE;
        DWORD ownerTid=GetCurrentThreadId(),ownerPid=42,ownerError=0,touchError=0,pointerError=0;
        ULONG touchFlags=TWF_WANTPALM;
        DWORD beforeTouchError=~DWORD{0},beforePointerError=~DWORD{0};
        UINT32 requestedPointer=0;
        POINTER_INFO info{};
        static BOOL WINAPI IsWindow(HWND){auto& f=*currentNativeDiagnosticFixture;++f.windowQueries;SetLastError(7001);return f.windowValid;}
        static BOOL WINAPI IsVisible(HWND){auto& f=*currentNativeDiagnosticFixture;++f.windowQueries;SetLastError(7002);return f.visible;}
        static BOOL WINAPI IsEnabled(HWND){auto& f=*currentNativeDiagnosticFixture;++f.windowQueries;SetLastError(7003);return f.enabled;}
        static DWORD WINAPI Owner(HWND,DWORD* pid){auto& f=*currentNativeDiagnosticFixture;++f.ownerQueries;*pid=f.ownerPid;SetLastError(f.ownerError);return f.ownerTid;}
        static BOOL WINAPI IsTouch(HWND,ULONG* flags){auto& f=*currentNativeDiagnosticFixture;++f.touchQueries;f.beforeTouchError=GetLastError();*flags=f.touchFlags;if(f.touchError)SetLastError(f.touchError);return f.touch;}
        static BOOL WINAPI PointerInfo(UINT32 id,POINTER_INFO* info){auto& f=*currentNativeDiagnosticFixture;++f.pointerQueries;f.beforePointerError=GetLastError();f.requestedPointer=id;*info=f.info;if(f.pointerError)SetLastError(f.pointerError);return f.pointerOk;}
        NativeDiagnosticFixture()
        {
            queries.isWindow=IsWindow;queries.isVisible=IsVisible;queries.isEnabled=IsEnabled;
            queries.owner=Owner;queries.isTouch=IsTouch;queries.pointerInfo=PointerInfo;
            currentNativeDiagnosticFixture=this;UiDiag::testNativeQueries=&queries;
        }
        ~NativeDiagnosticFixture(){UiDiag::testNativeQueries=previous;currentNativeDiagnosticFixture=previousFixture;}
        unsigned Count() const{return windowQueries+ownerQueries+touchQueries+pointerQueries;}
    };
    namespace RoutedTouch = Inkeys::Input::UiPointer::Touch;
    struct TouchRouteFixture;
    TouchRouteFixture* currentTouchRouteFixture = nullptr;
    struct RoutedDelivery { RoutedTouch::Packet packet; RoutedTouch::Identity identity; };
    struct TouchTargetProbe
    {
        std::size_t index = 0;
        RoutedTouch::Hit hit = RoutedTouch::Hit::Accept;
        bool accepted = true;
        unsigned probes = 0;
        std::vector<RoutedDelivery> deliveries;
        std::vector<RoutedTouch::Identity> cancellations;
        std::function<void()> duringProbe;
        std::function<void(const RoutedTouch::Packet&, RoutedTouch::Identity)> duringDeliver;
        std::function<void(RoutedTouch::Identity)> duringCancel;
        unsigned Count(Inkeys::Input::UiPointer::Kind kind) const
        {
            unsigned count = 0;
            for (const auto& delivery : deliveries) if (delivery.packet.kind == kind) ++count;
            return count;
        }
    };
    // 只替换原生命中查询；登记、代次、投递和取消均直接调用生产实现，不创建 HWND。
    struct TouchRouteFixture
    {
        std::array<RoutedTouch::Context, RoutedTouch::TargetCount> locals;
        std::array<TouchTargetProbe, RoutedTouch::TargetCount> targets;
        HWND candidate = Window(0);
        POINT queriedPoint{};
        unsigned candidateQueries = 0;
        bool queryScopeSeen = false, registrationsValid = true;
        std::function<void()> duringCandidate;
        static HWND Window(std::size_t index) noexcept
        { return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(index + 1)); }
        static HWND Candidate(POINT point)
        {
            auto& fixture = *currentTouchRouteFixture;
            fixture.queriedPoint = point;
            ++fixture.candidateQueries;
            fixture.queryScopeSeen = RoutedTouch::QueryActive();
            const auto callback = fixture.duringCandidate;
            if (callback) callback();
            return fixture.candidate;
        }
        static RoutedTouch::Probe Probe(void* user, const RoutedTouch::Packet&)
        {
            auto& target = *static_cast<TouchTargetProbe*>(user);
            ++target.probes;
            const auto callback = target.duringProbe;
            if (callback) callback();
            return { target.hit, 100 + target.index };
        }
        static bool Deliver(void* user, const RoutedTouch::Packet& packet, RoutedTouch::Identity identity)
        {
            auto& target = *static_cast<TouchTargetProbe*>(user);
            target.deliveries.push_back({ packet, identity });
            const auto callback = target.duringDeliver;
            if (callback) callback(packet, identity);
            return target.accepted;
        }
        static void Cancel(void* user, RoutedTouch::Identity identity)
        {
            auto& target = *static_cast<TouchTargetProbe*>(user);
            target.cancellations.push_back(identity);
            const auto callback = target.duringCancel;
            if (callback) callback(identity);
        }
        bool Register(std::size_t index)
        {
            const RoutedTouch::Adapter adapter{ Probe, Deliver, Cancel };
            return RoutedTouch::RegisterTarget(index, Window(index), locals[index], adapter, &targets[index]);
        }
        TouchRouteFixture()
        {
            RoutedTouch::Testing::Reset();
            currentTouchRouteFixture = this;
            RoutedTouch::Testing::SetCandidate(Candidate);
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                targets[index].index = index;
                registrationsValid = Register(index) && registrationsValid;
            }
        }
        ~TouchRouteFixture()
        {
            // 先移除测试重入动作，再在五份本地Context仍存活时注销生产登记。
            duringCandidate = {};
            for (auto& target : targets)
            { target.duringProbe = {}; target.duringDeliver = {}; target.duringCancel = {}; }
            RoutedTouch::Testing::Reset();
            currentTouchRouteFixture = nullptr;
        }
        void SendAt(const TOUCHINPUT& input, std::size_t origin, DWORD messageTime)
        { RoutedTouch::DispatchDecoded(Window(origin), std::span<const TOUCHINPUT>(&input, 1), messageTime); }
        void Send(const TOUCHINPUT& input, std::size_t origin = 0)
        { SendAt(input, origin, input.dwTime); }
        void Batch(std::initializer_list<TOUCHINPUT> inputs, std::size_t origin = 0)
        { RoutedTouch::DispatchDecoded(Window(origin), { inputs.begin(), inputs.size() }, inputs.size() ? inputs.begin()->dwTime : 0); }
    };
    TOUCHINPUT RouteInput(Inkeys::Input::UiPointer::Kind kind, DWORD time,
        LONG x = 100, LONG y = 200, DWORD contact = 40, std::uintptr_t device = 7, bool primary = true)
    {
        using Inkeys::Input::UiPointer::Kind;
        TOUCHINPUT input{};
        input.hSource = reinterpret_cast<HANDLE>(device);
        input.dwID = contact;
        input.x = x * 100; input.y = y * 100;
        input.dwTime = time; input.dwMask = TOUCHINPUTMASKF_TIMEFROMSYSTEM;
        input.dwFlags = TOUCHEVENTF_PEN | (primary ? TOUCHEVENTF_PRIMARY : 0)
            | (kind == Kind::Down ? TOUCHEVENTF_DOWN | TOUCHEVENTF_INRANGE
                : kind == Kind::Up ? TOUCHEVENTF_UP : TOUCHEVENTF_MOVE | TOUCHEVENTF_INRANGE);
        return input;
    }
    // PointerMode 路由测试：只替换 GetPointerInfo；登记、代次、投递和取消均走生产 router。
    struct FakePointer
    {
        POINTER_INFO info{};
        BOOL ok = TRUE;
        static FakePointer*& Current() { static FakePointer* current = nullptr; return current; }
        static BOOL WINAPI Query(UINT32 id, POINTER_INFO* out)
        {
            auto* fake = Current();
            if (!fake || !fake->ok) { SetLastError(8888); return FALSE; }
            *out = fake->info; out->pointerId = id; SetLastError(9999); return TRUE;
        }
        void Set(UINT32 type, UINT32 flags, POINT raw, DWORD time, std::uintptr_t device = 0x4567)
        {
            info = {}; info.pointerType = static_cast<POINTER_INPUT_TYPE>(type); info.pointerFlags = flags;
            info.ptPixelLocationRaw = raw; info.ptPixelLocation = raw; info.dwTime = time;
            info.sourceDevice = reinterpret_cast<HANDLE>(device);
        }
    };
    constexpr UINT32 PenDownFlags = POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_PRIMARY | POINTER_FLAG_DOWN;
    constexpr UINT32 PenMoveFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_PRIMARY | POINTER_FLAG_UPDATE;
    constexpr UINT32 PenUpFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_PRIMARY | POINTER_FLAG_UP;
    int RunTouchRoutingTests()
    {
        using Inkeys::Input::UiPointer::Kind;
        int failures = 0;
        const auto check = [&](bool ok, const char* description)
        { if (!ok) { ++failures; std::cerr << "FAIL " << description << '\n'; } };
        {
            TouchRouteFixture fixture;
            check(fixture.registrationsValid && RoutedTouch::TargetCount == 5,
                "Touch router registers five independent local contexts");
            fixture.candidate = fixture.Window(2);
            auto down = RouteInput(Kind::Down, 100, 1079, 1856, 1272);
            down.dwMask |= TOUCHINPUTMASKF_EXTRAINFO | TOUCHINPUTMASKF_CONTACTAREA;
            down.dwExtraInfo = 0x12345678; down.cxContact = 31; down.cyContact = 51;
            fixture.Send(down);
            const auto identity = fixture.locals[2].identity;
            check(fixture.locals[2].active && !fixture.locals[0].active && !fixture.locals[1].active
                && fixture.targets[2].Count(Kind::Down) == 1 && identity.window == fixture.Window(2)
                && identity.registration == fixture.locals[2].registration && fixture.locals[2].semantic == 102,
                "Touch delivered to old origin starts only actual Down target context");
            check(fixture.queryScopeSeen && !RoutedTouch::QueryActive()
                && fixture.queriedPoint.x == 1079 && fixture.queriedPoint.y == 1856,
                "Touch candidate uses real decoded screen point inside scoped native query");
            fixture.candidate = fixture.Window(4);
            fixture.Send(RouteInput(Kind::Move, 101, 2775, 1835, 1272), 1);
            fixture.Send(RouteInput(Kind::Up, 102, 2776, 1836, 1272), 0);
            check(!fixture.locals[2].active && fixture.targets[2].Count(Kind::Move) == 1
                && fixture.targets[2].Count(Kind::Up) == 1 && fixture.targets[4].deliveries.empty()
                && fixture.candidateQueries == 1 && fixture.targets[2].deliveries.back().identity == identity,
                "Touch Move and Up keep Down owner across source and hovered windows");
            check(fixture.targets[2].deliveries.front().packet.input.hSource == down.hSource
                && fixture.targets[2].deliveries.front().packet.input.dwFlags == down.dwFlags
                && fixture.targets[2].deliveries.front().packet.input.dwMask == down.dwMask
                && fixture.targets[2].deliveries.front().packet.input.dwExtraInfo == down.dwExtraInfo
                && fixture.targets[2].deliveries.front().packet.input.cxContact == down.cxContact
                && fixture.targets[2].deliveries.front().packet.input.cyContact == down.cyContact
                && fixture.targets[2].deliveries.front().packet.origin == fixture.Window(0)
                && fixture.targets[2].deliveries.front().packet.messageTime == 100,
                "Touch handoff retains original metadata and receiving window");
            DWORD time = 110;
            for (const auto target : { 0U, 1U, 2U, 1U })
            {
                fixture.candidate = fixture.Window(target);
                const unsigned before = fixture.targets[target].Count(Kind::Down);
                fixture.Send(RouteInput(Kind::Down, time, 200, 300, 1272));
                fixture.Send(RouteInput(Kind::Up, time + 1, 200, 300, 1272));
                check(fixture.targets[target].Count(Kind::Down) == before + 1 && !fixture.locals[target].active,
                    "Touch same in-range ID can repeatedly switch actual window without range reset");
                time += 3;
            }
            check(fixture.locals[0].nextGeneration == 1 && fixture.locals[1].nextGeneration == 2
                && fixture.locals[2].nextGeneration == 2 && fixture.locals[3].nextGeneration == 0,
                "Touch route generations belong to each local context");
        }
        {
            TouchRouteFixture fixture;
            fixture.candidate = reinterpret_cast<HWND>(99);
            fixture.Send(RouteInput(Kind::Down, 100));
            fixture.candidate = fixture.Window(0);
            fixture.Send(RouteInput(Kind::Move, 101)); fixture.Send(RouteInput(Kind::Up, 102));
            check(fixture.targets[0].deliveries.empty() && fixture.candidateQueries == 1,
                "Touch outside UI then moved into UI cannot invent a Down");
            for (const auto disposition : { RoutedTouch::Hit::Miss, RoutedTouch::Hit::Blocked })
            {
                fixture.targets[0].hit = disposition;
                fixture.Send(RouteInput(Kind::Down, 200 + static_cast<DWORD>(disposition)));
                check(!fixture.locals[0].active && fixture.targets[0].deliveries.empty()
                    && fixture.targets[1].probes == 0,
                    "Touch Miss or Blocked native candidate never searches behind it");
            }
        }
        {
            TouchRouteFixture fixture;
            const auto down = RouteInput(Kind::Down, 300);
            fixture.Send(down);
            const auto identity = fixture.locals[0].identity;
            fixture.Send(down, 2);
            check(fixture.targets[0].deliveries.size() == 1 && fixture.locals[0].identity == identity
                && fixture.candidateQueries == 1, "Touch duplicate Down across origins is delivered once");
            fixture.Send(RouteInput(Kind::Move, 310, 101, 200));
            fixture.Send(RouteInput(Kind::Move, 310, 102, 200));
            fixture.Send(RouteInput(Kind::Move, 310, 102, 200), 3);
            check(fixture.targets[0].Count(Kind::Move) == 2
                && fixture.targets[0].deliveries.back().packet.screen.x == 102,
                "Touch same-millisecond distinct positions survive while identical Move is deduplicated");
            const auto up = RouteInput(Kind::Up, 311, 102, 200);
            fixture.Send(up); fixture.Send(up, 2); fixture.Send(down, 1);
            check(fixture.targets[0].Count(Kind::Down) == 1 && fixture.targets[0].Count(Kind::Up) == 1,
                "Touch completed terminal and older Down replay do not create another gesture");
            fixture.Send(RouteInput(Kind::Down, 311, 102, 200));
            check(fixture.locals[0].active && fixture.locals[0].identity.generation != identity.generation,
                "Touch real new Down is valid in same millisecond as previous Up");
            fixture.Send(RouteInput(Kind::Up, 312, 102, 200));
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10));
            fixture.Send(RouteInput(Kind::Move, 120, 20, 20));
            fixture.Send(RouteInput(Kind::Up, 119, 9, 9));
            check(!fixture.locals[0].active && fixture.targets[0].Count(Kind::Up) == 1
                && fixture.targets[0].deliveries.back().packet.screen.x == 20
                && fixture.targets[0].deliveries.back().packet.input.x == 900,
                "Touch earlier same-generation Up ends route without rewinding accepted position or losing raw metadata");
            fixture.Send(RouteInput(Kind::Down, 130, 30, 30));
            const auto identity = fixture.locals[0].identity;
            fixture.Send(RouteInput(Kind::Up, 125, 5, 5));
            check(fixture.locals[0].Current(identity) && fixture.targets[0].Count(Kind::Up) == 1,
                "Touch terminal older than new Down cannot finish new generation");
            fixture.Send(RouteInput(Kind::Up, 131, 31, 31));
        }
        {
            TouchRouteFixture fixture;
            // 驱动时间可始终为零；这里只用原生消息时间区分先后，不能要求 dwTime 单调。
            const auto down = RouteInput(Kind::Down, 0);
            const auto up = RouteInput(Kind::Up, 0);
            fixture.SendAt(down, 0, 100); fixture.SendAt(up, 0, 101);
            fixture.SendAt(down, 0, 102); fixture.SendAt(up, 0, 103);
            check(fixture.targets[0].Count(Kind::Down) == 2 && fixture.targets[0].Count(Kind::Up) == 2
                && !fixture.locals[0].active, "Touch provider timestamp zero does not reject later native contacts");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 0xfffffff0U));
            fixture.Send(RouteInput(Kind::Move, 0xfffffff8U));
            fixture.Send(RouteInput(Kind::Up, 4));
            fixture.Send(RouteInput(Kind::Down, 16));
            const auto identity = fixture.locals[0].identity;
            fixture.Send(RouteInput(Kind::Up, 0xfffffff4U));
            check(fixture.locals[0].Current(identity), "Touch native message-time wrap rejects older terminal");
            fixture.Send(RouteInput(Kind::Up, 17));
            check(!fixture.locals[0].active && fixture.targets[0].Count(Kind::Up) == 2,
                "Touch native message-time wrap preserves legitimate contacts");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 11, 7));
            fixture.candidate = fixture.Window(1);
            fixture.Send(RouteInput(Kind::Down, 101, 20, 20, 11, 8));
            fixture.Send(RouteInput(Kind::Up, 102, 20, 20, 11, 8));
            check(fixture.locals[0].active && !fixture.locals[1].active && fixture.targets[1].deliveries.empty(),
                "Touch same ID from another device cannot steal or end selected contact");
            fixture.Send(RouteInput(Kind::Up, 103, 10, 10, 11, 7));
            fixture.Send(RouteInput(Kind::Down, 104, 20, 20, 11, 8));
            fixture.Send(RouteInput(Kind::Up, 105, 20, 20, 11, 8));
            check(fixture.targets[1].Count(Kind::Down) == 1 && !fixture.locals[1].active,
                "Touch competing device can begin after prior owner finishes");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1, 7, false));
            fixture.candidate = fixture.Window(1);
            fixture.Batch({ RouteInput(Kind::Up, 102, 10, 10, 1, 7, false), RouteInput(Kind::Down, 101, 20, 20, 2) });
            check(!fixture.locals[0].active && fixture.targets[0].cancellations.size() == 1
                && fixture.targets[0].Count(Kind::Up) == 0 && fixture.locals[1].active,
                "Touch primary replaces fallback before same-batch old fallback Up");
            fixture.Send(RouteInput(Kind::Up, 103, 20, 20, 2));
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1, 7, false));
            fixture.candidate = fixture.Window(1); fixture.targets[1].hit = RoutedTouch::Hit::Blocked;
            fixture.Send(RouteInput(Kind::Down, 101, 20, 20, 2));
            check(fixture.locals[0].active && fixture.targets[0].cancellations.empty(),
                "Touch blocked primary candidate does not cancel legitimate fallback");
            fixture.Send(RouteInput(Kind::Up, 102, 20, 20, 2));
            fixture.targets[1].hit = RoutedTouch::Hit::Accept;
            fixture.Send(RouteInput(Kind::Down, 103, 20, 20, 2));
            check(!fixture.locals[0].active && fixture.locals[1].active
                && fixture.targets[0].cancellations.size() == 1,
                "Touch accepted primary candidate takes over fallback once");
        }
        {
            TouchRouteFixture fixture;
            fixture.Batch({ RouteInput(Kind::Down, 100, 10, 10, 1, 7, false), RouteInput(Kind::Down, 100, 20, 20, 2, 7, false) });
            check(fixture.targets[0].Count(Kind::Down) == 1 && fixture.locals[0].latest.input.dwID == 1,
                "Touch batch without primary selects only first fallback contact");
        }
        {
            TouchRouteFixture fixture;
            fixture.locals[0].nextGeneration = 1ULL << 40;
            fixture.Send(RouteInput(Kind::Down, 100));
            const auto first = fixture.locals[0].identity;
            RoutedTouch::RequestCancel(first); RoutedTouch::DrainCancels();
            check(!fixture.locals[0].active && first.generation > 0xffffffffULL
                && fixture.targets[0].cancellations.size() == 1,
                "Touch full 64-bit identity cancels without native capture ticket");
            fixture.Send(RouteInput(Kind::Down, 110));
            const auto second = fixture.locals[0].identity;
            RoutedTouch::RequestCancel(second); RoutedTouch::RequestCancel(first); RoutedTouch::DrainCancels();
            check(!fixture.locals[0].active && fixture.targets[0].cancellations.size() == 2,
                "Touch later old cancel cannot replace pending current cancel");
            fixture.Send(RouteInput(Kind::Down, 120));
            const auto third = fixture.locals[0].identity;
            RoutedTouch::RequestCancel(second); RoutedTouch::DrainCancels();
            check(fixture.locals[0].Current(third), "Touch queued stale cancel cannot clear newer local generation");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100));
            const auto old = fixture.locals[0].identity;
            check(fixture.Register(0) && !fixture.locals[0].active
                && fixture.locals[0].registration != old.registration,
                "Touch target re-registration retires only previous registration");
            fixture.Send(RouteInput(Kind::Down, 110));
            const auto current = fixture.locals[0].identity;
            RoutedTouch::RequestCancel(old); RoutedTouch::DrainCancels();
            RoutedTouch::Cancel(fixture.locals[0], old);
            fixture.Send(RouteInput(Kind::Up, 105));
            check(fixture.locals[0].Current(current), "Touch old registration cancel and terminal do not affect replacement");
            fixture.Send(RouteInput(Kind::Up, 111));
        }
        {
            TouchRouteFixture fixture;
            fixture.candidate = fixture.Window(1);
            fixture.Send(RouteInput(Kind::Down, 100), 0);
            fixture.Send(RouteInput(Kind::Move, 101), 2);
            RoutedTouch::CancelWindow(fixture.Window(2));
            check(fixture.locals[1].active, "Touch later packet receiving window is not Down origin lifetime owner");
            RoutedTouch::CancelWindow(fixture.Window(0));
            check(!fixture.locals[1].active && fixture.targets[1].cancellations.size() == 1,
                "Touch original receiving window cancellation retires routed contact");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1));
            fixture.targets[0].duringCancel = [&](RoutedTouch::Identity)
            {
                fixture.targets[0].duringCancel = {};
                fixture.candidate = fixture.Window(1);
                fixture.Send(RouteInput(Kind::Down, 120, 20, 20, 2), 2);
            };
            const auto old = fixture.locals[0].identity;
            RoutedTouch::Cancel(fixture.locals[0], old);
            check(fixture.locals[1].active && fixture.targets[1].Count(Kind::Down) == 1,
                "Touch synchronous new Down in cancel callback survives old cancellation return");
            RoutedTouch::Cancel(fixture.locals[0], old);
            check(fixture.locals[1].active, "Touch repeated old cancellation cannot affect another window context");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1));
            fixture.targets[0].duringCancel = [&](RoutedTouch::Identity)
            {
                fixture.targets[0].duringCancel = {};
                fixture.candidate = fixture.Window(1);
                fixture.Send(RouteInput(Kind::Down, 120, 20, 20, 2), 0);
            };
            RoutedTouch::CancelWindow(fixture.Window(0));
            check(fixture.locals[1].active && fixture.targets[1].Count(Kind::Down) == 1,
                "Touch CancelWindow snapshot cannot clear newer reentrant route with same origin");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100));
            const auto old = fixture.locals[0].identity;
            fixture.targets[0].duringCancel = [&](RoutedTouch::Identity)
            { fixture.targets[0].duringCancel = {}; check(fixture.Register(0), "Touch cancel callback can rebuild target registration"); };
            RoutedTouch::RetireTarget(fixture.Window(0));
            fixture.Send(RouteInput(Kind::Down, 110));
            check(fixture.locals[0].active && fixture.locals[0].registration != old.registration
                && fixture.targets[0].Count(Kind::Down) == 2,
                "Touch old RetireTarget does not erase registration rebuilt inside cancel callback");
        }
        {
            TouchRouteFixture fixture;
            fixture.targets[0].duringDeliver = [&](const RoutedTouch::Packet& packet, RoutedTouch::Identity identity)
            {
                if (packet.kind != Kind::Down) return;
                fixture.targets[0].duringDeliver = {};
                check(fixture.locals[0].preparing, "Touch deliver sees local Preparing reservation");
                RoutedTouch::Cancel(fixture.locals[0], identity);
                fixture.Send(RouteInput(Kind::Down, 110, 20, 20, 2));
            };
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1));
            check(fixture.locals[0].active && !fixture.locals[0].preparing
                && fixture.locals[0].latest.input.dwID == 2 && fixture.targets[0].Count(Kind::Down) == 2,
                "Touch old Preparing completion cannot overwrite synchronous newer Down");
        }
        {
            TouchRouteFixture fixture;
            fixture.targets[0].duringDeliver = [&](const RoutedTouch::Packet& packet, RoutedTouch::Identity)
            {
                if (packet.kind != Kind::Down) return;
                fixture.targets[0].duringDeliver = {};
                check(fixture.Register(0), "Touch Preparing delivery can replace same-window registration");
                fixture.Send(RouteInput(Kind::Down, 110, 20, 20, 2));
            };
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1));
            check(fixture.locals[0].active && !fixture.locals[0].preparing
                && fixture.locals[0].latest.input.dwID == 2 && fixture.targets[0].cancellations.size() == 1,
                "Touch old Preparing return cannot clear new registration and contact");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1));
            fixture.targets[0].duringDeliver = [&](const RoutedTouch::Packet& packet, RoutedTouch::Identity identity)
            {
                if (packet.kind != Kind::Up) return;
                fixture.targets[0].duringDeliver = {};
                RoutedTouch::Cancel(fixture.locals[0], identity);
                fixture.Send(RouteInput(Kind::Down, 110, 20, 20, 2));
            };
            fixture.Send(RouteInput(Kind::Up, 101, 10, 10, 1));
            check(fixture.locals[0].active && fixture.locals[0].latest.input.dwID == 2
                && fixture.targets[0].Count(Kind::Up) == 1 && fixture.targets[0].Count(Kind::Down) == 2,
                "Touch old Up callback return cannot finish synchronously established new contact");
        }
        {
            TouchRouteFixture fixture;
            // 同步重入即使在返回前结束接触，也必须使外层 Probe 失效。
            const auto registration = fixture.locals[0].registration;
            fixture.targets[0].duringProbe = [&]
            {
                fixture.targets[0].duringProbe = {};
                fixture.Send(RouteInput(Kind::Down, 110, 20, 20, 2));
                fixture.Send(RouteInput(Kind::Up, 111, 20, 20, 2));
            };
            fixture.Send(RouteInput(Kind::Down, 100, 10, 10, 1));
            check(!fixture.locals[0].active && fixture.locals[0].registration == registration
                && fixture.targets[0].Count(Kind::Down) == 1 && fixture.targets[0].Count(Kind::Up) == 1,
                "Touch Probe local-generation change rejects older selection even after reentrant contact ends");
        }
        {
            TouchRouteFixture fixture;
            fixture.targets[0].duringProbe = [&]
            { fixture.targets[0].duringProbe = {}; RoutedTouch::RetireTarget(fixture.Window(0)); check(fixture.Register(0), "Touch Probe re-registers target"); };
            fixture.Send(RouteInput(Kind::Down, 100));
            check(!fixture.locals[0].active && fixture.targets[0].deliveries.empty(),
                "Touch Probe re-registration invalidates original Down selection");
        }
        {
            TouchRouteFixture fixture;
            fixture.duringCandidate = [&]
            { fixture.duringCandidate = {}; RoutedTouch::RetireTarget(fixture.Window(0)); check(fixture.Register(0), "Touch candidate callback re-registers target"); };
            fixture.Send(RouteInput(Kind::Down, 100));
            check(!fixture.locals[0].active && fixture.targets[0].deliveries.empty(),
                "Touch native candidate query re-registration invalidates original Down selection");
            fixture.Send(RouteInput(Kind::Down, 101));
            check(fixture.locals[0].active, "Touch new stable Down works after candidate query re-entry");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down, 100)); fixture.Send(RouteInput(Kind::Up, 101));
            fixture.candidate = fixture.Window(1);
            fixture.targets[1].duringDeliver = [&](const RoutedTouch::Packet& packet, RoutedTouch::Identity)
            {
                if (packet.kind != Kind::Down) return;
                fixture.targets[1].duringDeliver = {};
                // B 夺取捕获使 A 丢捕获，只取消 A 目标；不能按 B 的 origin=A 误清 B。
                RoutedTouch::CancelTargetWindow(fixture.Window(0));
            };
            fixture.Send(RouteInput(Kind::Down, 110), 0);
            check(fixture.locals[1].active && fixture.locals[1].preparing == false
                && fixture.targets[1].cancellations.empty(),
                "Touch target capture can cancel old origin capture without canceling Preparing handoff");
            RoutedTouch::CancelWindow(fixture.Window(0));
            check(!fixture.locals[1].active, "Touch origin hide still cancels preparing handoff lifetime after capture transfer");
        }
        {
            TouchRouteFixture fixture;
            fixture.targets[0].accepted = false;
            fixture.Send(RouteInput(Kind::Down, 100));
            check(!fixture.locals[0].active && fixture.targets[0].cancellations.size() == 1,
                "Touch rejected client delivery rolls back its local reservation once");
            RoutedTouch::Context extra;
            const RoutedTouch::Adapter adapter{ TouchRouteFixture::Probe, TouchRouteFixture::Deliver, TouchRouteFixture::Cancel };
            check(!RoutedTouch::RegisterTarget(RoutedTouch::TargetCount, reinterpret_cast<HWND>(99), extra, adapter, &fixture.targets[0]),
                "Touch native target registry is bounded to five slots");
        }
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down,100,10,10,1,7,false));
            fixture.Send(RouteInput(Kind::Move,101,11,11,1,7,true));
            check(fixture.locals[0].primary,"Touch fallback contact promotes when a later owned Move carries primary");
            fixture.candidate=fixture.Window(1);
            fixture.Send(RouteInput(Kind::Down,102,20,20,2,7,true));
            check(fixture.locals[0].active&&fixture.targets[0].cancellations.empty()&&fixture.targets[1].deliveries.empty(),
                "Touch another primary cannot replace a fallback already promoted to primary");
            fixture.Send(RouteInput(Kind::Up,103,11,11,1,7,true));
            check(!fixture.locals[0].active&&fixture.targets[0].Count(Kind::Up)==1,
                "Touch promoted contact keeps its original terminal ownership");
        }
        for(const bool reentrantTouch : {false,true})
        {
            TouchRouteFixture fixture;
            fixture.candidate=fixture.Window(1);
            bool rollback=false,protectMouse=false,protectOtherCapture=false,protectExistingCapture=false;
            fixture.targets[1].duringDeliver=[&](const RoutedTouch::Packet& packet,RoutedTouch::Identity identity)
            {
                if(packet.kind!=Kind::Down)return;
                fixture.targets[1].duringDeliver={};
                RoutedTouch::CancelWindow(fixture.Window(0));
                protectMouse=!RoutedTouch::ShouldRollbackPreparingCapture(fixture.locals[1],identity,fixture.Window(0),fixture.Window(1),0,5);
                protectOtherCapture=!RoutedTouch::ShouldRollbackPreparingCapture(fixture.locals[1],identity,fixture.Window(0),fixture.Window(2),0,0);
                protectExistingCapture=!RoutedTouch::ShouldRollbackPreparingCapture(fixture.locals[1],identity,fixture.Window(1),fixture.Window(1),0,0);
                if(reentrantTouch)fixture.Send(RouteInput(Kind::Down,101,10,10,2));
                rollback=RoutedTouch::ShouldRollbackPreparingCapture(fixture.locals[1],identity,fixture.Window(0),fixture.Window(1),0,0);
            };
            fixture.Send(RouteInput(Kind::Down,100,10,10,1));
            check(rollback==!reentrantTouch&&protectMouse&&protectOtherCapture&&protectExistingCapture,
                "Touch preparing capture rolls back only its canceled generation, preserving newer Mouse/Touch or other/preexisting capture");
            check(reentrantTouch?fixture.locals[1].active&&!fixture.locals[1].preparing:!fixture.locals[1].active,
                "Touch capture rollback decision follows actual router cancellation and reentrant route ownership");
        }
        for(const bool unregisterSourceDuringCancel : {false,true})
        {
            TouchRouteFixture fixture;
            fixture.Send(RouteInput(Kind::Down,100,10,10,1,7,false),0);
            fixture.candidate=fixture.Window(1);
            fixture.targets[0].duringCancel=[&](RoutedTouch::Identity)
            {
                fixture.targets[0].duringCancel={};
                // 接收源2既不是旧fallback的owner0，也不是待接纳目标1。
                if(unregisterSourceDuringCancel) { RoutedTouch::RetireTarget(fixture.Window(2));check(fixture.Register(2),"Touch fallback cancellation can re-register separate source"); }
                else RoutedTouch::CancelWindow(fixture.Window(2));
            };
            fixture.Send(RouteInput(Kind::Down,110,20,20,2,7,true),2);
            check(!fixture.locals[0].active&&!fixture.locals[1].active&&fixture.targets[0].cancellations.size()==1&&fixture.targets[1].deliveries.empty(),
                "Touch fallback cancellation rechecks separate source lifetime before creating primary route");
            fixture.Send(RouteInput(Kind::Down,111,20,20,2,7,true),2);
            check(fixture.locals[1].active&&fixture.targets[1].Count(Kind::Down)==1,
                "Touch stable source Down remains valid after prior fallback cancellation invalidated its handoff");
        }
        for(const bool unregisterSource : {false,true})
        {
            TouchRouteFixture fixture;
            fixture.candidate=fixture.Window(1);
            fixture.duringCandidate=[&]
            {
                fixture.duringCandidate={};
                if(unregisterSource) { RoutedTouch::RetireTarget(fixture.Window(0));check(fixture.Register(0),"Touch source re-registers during native query"); }
                else RoutedTouch::CancelWindow(fixture.Window(0));
            };
            fixture.Send(RouteInput(Kind::Down,100));
            check(!fixture.locals[1].active&&fixture.targets[1].deliveries.empty(),
                "Touch source lifecycle change during native query rejects a handoff to another valid target");
            fixture.Send(RouteInput(Kind::Down,101));
            check(fixture.locals[1].active&&fixture.targets[1].Count(Kind::Down)==1,
                "Touch stable source can hand off next Down after its prior lifecycle invalidated an old query");
        }
        {
            TouchRouteFixture fixture;
            fixture.candidate=fixture.Window(1);
            fixture.targets[1].duringProbe=[&]
            {fixture.targets[1].duringProbe={};RoutedTouch::CancelWindow(fixture.Window(0));};
            fixture.Send(RouteInput(Kind::Down,100));
            check(!fixture.locals[1].active&&fixture.targets[1].deliveries.empty(),
                "Touch source lifecycle is rechecked after the target presented probe too");
            fixture.Send(RouteInput(Kind::Down,101));
            check(fixture.locals[1].active,"Touch next stable handoff survives a previous source cancellation during probe");
        }
        {
            // PointerMode：Pointer 接触是唯一业务来源，目标 = 接收窗；WM_TOUCH 只是派生副本。
            TouchRouteFixture fixture;
            FakePointer fake;
            FakePointer::Current() = &fake;
            RoutedTouch::Testing::SetPointerQuery(FakePointer::Query);
            check(RoutedTouch::PointerMode(), "Pointer mode is enabled when GetPointerInfo is available");
            fixture.candidate = fixture.Window(4); // 若误用 WindowFromPoint，会选到错误目标。
            const auto wp = [](UINT32 id) { return static_cast<WPARAM>(id); };
            const auto window = [&](std::size_t index) { return fixture.Window(index); };
            fake.Set(PT_PEN, PenDownFlags, { 120, 240 }, 1000);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(77)), "Pointer pen Down is consumed as a contact");
            check(fixture.candidateQueries == 0 && fixture.targets[0].Count(Kind::Down) == 1 && fixture.locals[0].active,
                "Pointer Down targets the receiving window without WindowFromPoint");
            const auto& down = fixture.targets[0].deliveries.back().packet;
            check(down.input.dwID == 77 && (down.input.dwFlags & TOUCHEVENTF_PEN) && (down.input.dwFlags & TOUCHEVENTF_PRIMARY)
                && down.screen.x == 120 && down.screen.y == 240 && down.input.dwTime == 1000
                && reinterpret_cast<std::uintptr_t>(down.input.hSource) == 0x4567 && down.input.x == 12000,
                "Pointer contact carries id, device, time, pen flag and raw pixel in the shared contact record");
            fake.Set(PT_PEN, PenMoveFlags, { 130, 245 }, 1016);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERUPDATE, wp(77)) && fixture.targets[0].Count(Kind::Move) == 1,
                "Pointer contact Update becomes a Move for the owning route");
            fake.Set(PT_PEN, PenMoveFlags & ~POINTER_FLAG_INCONTACT, { 131, 246 }, 1020);
            check(!RoutedTouch::HandlePointer(window(0), WM_POINTERUPDATE, wp(77)) && fixture.targets[0].Count(Kind::Move) == 1,
                "Pointer hover Update is left to DefWindowProc and never moves a contact");
            // 抬起落在另一窗口：仍按接触身份回到 Down 的目标（隐式捕获不是正确性前提）。
            fake.Set(PT_PEN, PenUpFlags, { 900, 900 }, 1040);
            check(RoutedTouch::HandlePointer(window(2), WM_POINTERUP, wp(77)) && fixture.targets[0].Count(Kind::Up) == 1
                && !fixture.locals[0].active && fixture.targets[2].deliveries.empty(),
                "Pointer Up received by another window still ends the route of its Down owner");
            // 同一 pointerId 再次按下（范围内持续）获得新的 route generation。
            const auto firstGeneration = fixture.targets[0].deliveries.front().identity.generation;
            fake.Set(PT_PEN, PenDownFlags, { 121, 241 }, 2000);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(77)) && fixture.targets[0].Count(Kind::Down) == 2
                && fixture.targets[0].deliveries.back().identity.generation > firstGeneration,
                "Repeated Down of a persistent pointer id starts a new route generation");
            // 上次 Up 丢失：同 id 新 Down 先撤销旧路由，再接纳新的 Down。
            const auto cancelled = fixture.targets[0].cancellations.size();
            fake.Set(PT_PEN, PenDownFlags, { 122, 242 }, 3000);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(77)) && fixture.targets[0].cancellations.size() == cancelled + 1
                && fixture.targets[0].Count(Kind::Down) == 3 && fixture.locals[0].active,
                "A Down for a pointer whose Up was lost cancels the stale route instead of being ignored");
            fake.Set(PT_PEN, PenUpFlags | POINTER_FLAG_CANCELED, { 122, 242 }, 3050);
            const auto upsBefore = fixture.targets[0].Count(Kind::Up);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERUP, wp(77)) && fixture.targets[0].Count(Kind::Up) == upsBefore && !fixture.locals[0].active,
                "Canceled Pointer cancels the route without a successful Up");
            // 第二键/笔桶键：吞掉但不进业务。
            const auto downsBefore = fixture.targets[0].Count(Kind::Down);
            fake.Set(PT_PEN, PenDownFlags | POINTER_FLAG_SECONDBUTTON, { 123, 243 }, 4000);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(78)) && fixture.targets[0].Count(Kind::Down) == downsBefore,
                "Secondary-button Pointer Down is consumed but never reaches business");
            fake.Set(PT_PEN, PenDownFlags & ~POINTER_FLAG_FIRSTBUTTON, { 123, 243 }, 4001);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(78)) && fixture.targets[0].Count(Kind::Down) == downsBefore,
                "Pointer Down without the first button never reaches business");
            // 手指：PT_TOUCH 同样走 Pointer，且不带 PEN 位。
            fake.Set(PT_TOUCH, PenDownFlags, { 300, 310 }, 5000, 0x99);
            check(RoutedTouch::HandlePointer(window(1), WM_POINTERDOWN, wp(5)) && fixture.targets[1].Count(Kind::Down) == 1
                && !(fixture.targets[1].deliveries.back().packet.input.dwFlags & TOUCHEVENTF_PEN),
                "Pointer finger contact enters business without the pen flag");
            // PT_MOUSE、查询失败不接管。
            fake.Set(PT_MOUSE, PenDownFlags, { 1, 1 }, 6000);
            check(!RoutedTouch::HandlePointer(window(3), WM_POINTERDOWN, wp(9)), "Pointer mouse type is not a contact source");
            fake.Set(PT_PEN, PenDownFlags, { 1, 1 }, 6001); fake.ok = FALSE;
            check(!RoutedTouch::HandlePointer(window(3), WM_POINTERDOWN, wp(9)) && fixture.targets[3].deliveries.empty(),
                "A failed GetPointerInfo query falls back to default processing");
            fake.ok = TRUE;
            check(!RoutedTouch::HandlePointer(window(3), WM_POINTERENTER, wp(9)) && !RoutedTouch::HandlePointer(window(3), WM_POINTERLEAVE, wp(9)),
                "Pointer enter and leave are not contact messages");
            // 丢失终态的稳健性：同设备新的主接触撤销旧主接触，否则旧接触永久占用 Active() 并拒绝所有后续主 Down。
            const auto cancels1 = fixture.targets[1].cancellations.size();
            fake.Set(PT_TOUCH, PenDownFlags, { 310, 320 }, 8000, 0x99);
            check(RoutedTouch::HandlePointer(window(1), WM_POINTERDOWN, wp(6)) && fixture.targets[1].cancellations.size() == cancels1 + 1
                && fixture.targets[1].Count(Kind::Down) == 2 && fixture.locals[1].active && fixture.locals[1].latest.input.dwID == 6,
                "A new primary contact on the same device cancels a primary contact whose Up was lost");
            // 旧 id 的 Down 被拒绝（第二键）前，同 id 的旧路由也必须先撤销。
            const auto cancels2 = fixture.targets[1].cancellations.size();
            fake.Set(PT_TOUCH, PenDownFlags | POINTER_FLAG_SECONDBUTTON, { 311, 321 }, 8100, 0x99);
            check(RoutedTouch::HandlePointer(window(1), WM_POINTERDOWN, wp(6)) && fixture.targets[1].cancellations.size() == cancels2 + 1
                && !fixture.locals[1].active && fixture.targets[1].Count(Kind::Down) == 2,
                "Stale owner is cancelled before a rejected secondary-button Down so later Updates cannot move the old route");
            // Up/Update 查询失败：无法确认状态，撤销该 id 的路由而不是留下活动接触。
            fake.Set(PT_PEN, PenDownFlags, { 140, 150 }, 8200, 0x4567);
            (void)RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(90));
            const auto cancels0 = fixture.targets[0].cancellations.size();
            check(fixture.locals[0].active, "Pen contact is active before the failing Up query");
            fake.ok = FALSE;
            check(!RoutedTouch::HandlePointer(window(0), WM_POINTERUP, wp(90)) && fixture.targets[0].cancellations.size() == cancels0 + 1 && !fixture.locals[0].active,
                "A failed GetPointerInfo query on Up cancels the route it cannot confirm");
            fake.ok = TRUE;
            // 系统手势等接管捕获后不会再有 Up：按 id 撤销路由，且仍交回调用方继续处理该消息。
            fake.Set(PT_TOUCH, PenDownFlags, { 140, 150 }, 8300, 0x4568);
            (void)RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(92));
            const auto cancelsCapture = fixture.targets[0].cancellations.size();
            const auto upsCapture = fixture.targets[0].Count(Kind::Up);
            check(fixture.locals[0].active, "Touch contact is active before capture is lost");
            check(!RoutedTouch::HandlePointer(window(0), WM_POINTERCAPTURECHANGED, wp(93)) && fixture.locals[0].active
                && fixture.targets[0].cancellations.size() == cancelsCapture,
                "Capture loss of another pointer id leaves the active route alone");
            check(!RoutedTouch::HandlePointer(window(0), WM_POINTERCAPTURECHANGED, wp(92))
                && fixture.targets[0].cancellations.size() == cancelsCapture + 1 && !fixture.locals[0].active
                && fixture.targets[0].Count(Kind::Up) == upsCapture,
                "WM_POINTERCAPTURECHANGED cancels the route of that pointer id without a successful Up");
            const auto downsCapture = fixture.targets[0].Count(Kind::Down);
            fake.Set(PT_TOUCH, PenDownFlags, { 140, 150 }, 8310, 0x4568);
            check(RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(94)) && fixture.targets[0].Count(Kind::Down) == downsCapture + 1,
                "A new Down is accepted after capture loss");
            fake.Set(PT_TOUCH, PenUpFlags, { 140, 150 }, 8320, 0x4568);
            (void)RoutedTouch::HandlePointer(window(0), WM_POINTERUP, wp(94));
            // WM_TOUCH 的去向：PointerMode 丢弃，Legacy（Win7/应急开关）仍按原路径读取。
            {
                namespace Diag = Inkeys::Input::UiPointer::Diagnostics;
                namespace HapticDiag = Inkeys::Drawing::Draw3::HapticDiagnostics;
                const bool previousEnabled = HapticDiag::Enabled(); const auto oldSink = Diag::testSink;
                struct RestoreDiag { bool enabled; decltype(Diag::testSink) sink; ~RestoreDiag() { Diag::testSink = sink; HapticDiag::SetEnabled(enabled); } } restoreDiag{ previousEnabled, oldSink };
                Diag::testSink = [](const char* line, std::size_t size) { uiDiagnosticText.append(line, size); };
                HapticDiag::SetEnabled(true);
                uiDiagnosticText.clear();
                (void)RoutedTouch::HandleMessage(window(0), MAKEWPARAM(1, 0), 0);
                check(uiDiagnosticText.find("stage=touch_dropped reason=pointer_mode_derived") != std::string::npos
                    && uiDiagnosticText.find("GetTouchInputInfo_failed") == std::string::npos,
                    "WM_TOUCH is dropped as a derived copy in Pointer mode without being read");
                RoutedTouch::Testing::ForceLegacy(true);
                uiDiagnosticText.clear();
                (void)RoutedTouch::HandleMessage(window(0), MAKEWPARAM(1, 0), 0);
                check(!RoutedTouch::PointerMode() && !RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(91))
                    && uiDiagnosticText.find("touch_dropped") == std::string::npos && uiDiagnosticText.find("GetTouchInputInfo_failed") != std::string::npos,
                    "Legacy mode ignores Pointer contacts and still reads WM_TOUCH as the source");
                RoutedTouch::Testing::ForceLegacy(false);
            }
            // GLE 保护。
            SetLastError(4321);
            fake.Set(PT_PEN, PenDownFlags, { 130, 130 }, 7000);
            (void)RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(80));
            check(GetLastError() == 4321, "Pointer handling preserves the caller's last error through a successful query");
            SetLastError(4322); fake.ok = FALSE;
            (void)RoutedTouch::HandlePointer(window(0), WM_POINTERDOWN, wp(81));
            check(GetLastError() == 4322, "Pointer handling preserves the caller's last error through a failed query");
            fake.ok = TRUE;
            RoutedTouch::Testing::SetPointerQuery(nullptr);
            FakePointer::Current() = nullptr;
        }
        {
            // 白板翻页转圈：短暂切换不闪烁，超过延迟才可见；角度只由已用时间推导。
            namespace Bar = Inkeys::UI::Bar;
            check(!Bar::BusyRingVisible(true, 0.0) && !Bar::BusyRingVisible(true, 0.29) && Bar::BusyRingVisible(true, 0.30) && Bar::BusyRingVisible(true, 5.0),
                "Busy ring waits for the delay before showing during a whiteboard page switch");
            check(!Bar::BusyRingVisible(false, 5.0), "Busy ring is never shown when the switch is not in progress");
            check(Bar::BusyRingAngleDegrees(-1.0) == 0.0 && Bar::BusyRingAngleDegrees(0.0) == 0.0
                && std::abs(Bar::BusyRingAngleDegrees(Bar::BusyRingPeriodSeconds / 4) - 90.0) < 1e-9
                && std::abs(Bar::BusyRingAngleDegrees(Bar::BusyRingPeriodSeconds * 3 + Bar::BusyRingPeriodSeconds / 2) - 180.0) < 1e-6,
                "Busy ring angle is derived from elapsed time and wraps every period");
        }
        return failures;
    }
}

int RunUiPointerTests()
{
    using namespace Inkeys::Input::UiPointer;
    int failures = 0;
    auto check = [&](bool condition, const char* description)
    {
        if (!condition) { ++failures; std::cerr << "FAIL " << description << '\n'; }
    };
    auto packet = [](Kind kind, DWORD time, LONG x = 0, LONG y = 0)
    {
        Sample sample;
        sample.kind = kind;
        sample.screen = { x, y };
        sample.tick = time;
        sample.messageTime = time;
        sample.device = 7;
        return sample;
    };

    check(IsPromotedMouseExtraInfo(0xFF515780u)
        && IsPromotedMouseExtraInfo(0xFF515700u)
        && !IsPromotedMouseExtraInfo(0u), "UI raw mouse promoted signature filter");

    check(Inkeys::Message::IsPenGeneratedMouseMessage(WM_LBUTTONDOWN, 0xFF515700u)
        && !Inkeys::Message::IsPenGeneratedMouseMessage(WM_LBUTTONDOWN, 0xFF515780u)
        && !Inkeys::Message::IsPenGeneratedMouseMessage(WM_LBUTTONDOWN, 0),
        "PPT compatibility routes only confirmed pen input outside Raw Mouse");

    {
        namespace Dispatch = Inkeys::Window::MessageDispatch;
        MSG outer{};
        outer.hwnd = reinterpret_cast<HWND>(1);
        outer.message = WM_INPUT;
        outer.wParam = RIM_INPUTSINK;
        outer.lParam = 10;
        outer.time = 100;
        outer.pt = { -90000, 80000 };
        Dispatch::Sample sample;
        check(!Dispatch::TryGet(outer.hwnd, WM_INPUT, outer.wParam, outer.lParam, sample),
            "UI posted sample is unavailable outside dispatch");
        {
            Dispatch::Scope scope(outer);
            check(Dispatch::TryGet(outer.hwnd, WM_INPUT, outer.wParam, outer.lParam, sample)
                && sample.screen.x == -90000 && sample.screen.y == 80000 && sample.time == 100,
                "UI posted sample preserves original full screen point and timestamp");
            check(!Dispatch::TryGet(outer.hwnd, WM_INPUT, 0, outer.lParam, sample)
                && !Dispatch::TryGet(outer.hwnd, WM_INPUT, outer.wParam, 11, sample)
                && !Dispatch::TryGet(nullptr, WM_INPUT, outer.wParam, outer.lParam, sample)
                && !Dispatch::TryGet(outer.hwnd, WM_LBUTTONDOWN, outer.wParam, outer.lParam, sample),
                "UI reentrant messages cannot borrow another queued sample");
            MSG inner = outer;
            inner.time = 101;
            inner.pt = { 20, 30 };
            inner.lParam = 11;
            {
                Dispatch::Scope nested(inner);
                check(Dispatch::TryGet(inner.hwnd, WM_INPUT, inner.wParam, inner.lParam, sample)
                    && sample.time == 101 && sample.screen.x == 20,
                    "UI nested dispatch uses its own sampling point");
            }
            check(Dispatch::TryGet(outer.hwnd, WM_INPUT, outer.wParam, outer.lParam, sample)
                && sample.time == 100 && sample.screen.x == -90000,
                "UI nested dispatch restores outer sampling point");
        }
        check(!Dispatch::TryGet(outer.hwnd, WM_INPUT, outer.wParam, outer.lParam, sample),
            "UI dispatch snapshot does not outlive queue dispatch");
        check(Dispatch::PostedTick(100, 900) == 100
            && Dispatch::PostedTick(0xFFFFFFF0u, 0x100000020ull) == 0xFFFFFFF0ull,
            "UI posted ticks retain history and extend native timestamp wrap");
    }

    {
        Mailbox mailbox;
        for (std::size_t i = 0; i < Mailbox::Capacity; ++i)
            check(mailbox.Push(packet(Kind::Move, static_cast<DWORD>(i))),
                "UI pointer mailbox preserves bounded move history");
        auto overflow = packet(Kind::Up, 600);
        overflow.generation = 12;
        check(!mailbox.Push(overflow), "UI pointer overflow reports failure");
        Sample sample;
        check(mailbox.TryPop(sample) && sample.kind == Kind::Cancel
            && sample.cancelReason == CancelReason::Overflow && sample.generation == 12,
            "UI pointer overflow has guaranteed explicit cancel");
        check(!mailbox.Push(packet(Kind::Up, 601)) && !mailbox.TryPop(sample),
            "UI pointer overflow stays terminated until explicit reset");
        mailbox.Reset();
        check(mailbox.Push(packet(Kind::Down, 602)) && mailbox.TryPop(sample)
            && sample.kind == Kind::Down, "UI pointer mailbox reset permits next gesture");
    }

    {
        Session session;
        auto mouse = packet(Kind::Down, 1, -90000, 80000);
        mouse.generation = 1;
        check(session.Begin(mouse) == BeginResult::Started, "UI pointer mouse begins");
        auto unrelated = mouse;
        unrelated.kind = Kind::Up;
        unrelated.generation = 99;
        check(!session.Accept(unrelated) && session.Active(), "UI pointer stale Up ignored");
        auto touch = mouse;
        touch.source = Source::Touch;
        touch.generation = 2;
        touch.device = 99;
        touch.contact = 123;
        check(session.Begin(touch) == BeginResult::ReplacedMouse,
            "UI pointer touch replaces mouse explicitly");
        mouse.kind = Kind::Up;
        check(!session.Accept(mouse) && session.Active(), "UI pointer old mouse Up cannot end touch");
        unrelated = touch;
        unrelated.kind = Kind::Up;
        ++unrelated.contact;
        check(!session.Accept(unrelated), "UI pointer unrelated contact Up ignored");
        unrelated = touch;
        unrelated.kind = Kind::Move;
        ++unrelated.device;
        check(!session.Accept(unrelated), "UI pointer identical contact id on other source ignored");
        touch.kind = Kind::Cancel;
        touch.cancelReason = CancelReason::Requested;
        check(session.Accept(touch) && !session.Active()
            && session.Latest().screen.x == -90000 && session.Latest().screen.y == 80000
            && session.Latest().kind == Kind::Cancel,
            "UI pointer touch cancel preserves full screen coordinates and distinct outcome");
    }

    {
        Session session;
        auto pen = packet(Kind::Down, 80);
        pen.source = Source::Pen;
        pen.generation = 12;
        check(session.Begin(pen) == BeginResult::Started, "PPT legacy pen starts isolated session");
        auto release = pen;
        release.kind = Kind::Up;
        release.source = Source::Mouse;
        check(!session.Accept(release), "PPT physical mouse Up cannot end pen session");
        release.source = Source::Touch;
        check(!session.Accept(release), "PPT touch Up cannot end pen session");
        release.source = Source::Pen;
        check(session.Accept(release) && !session.Active(), "PPT legacy pen Up ends only pen session");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        Testing::PublishMouse(packet(Kind::Down, 100, 1));
        Testing::PublishMouse(packet(Kind::Move, 101, 2));
        Testing::PublishMouse(packet(Kind::Up, 102, 3));
        check(channel.BeginMouse({ 1, 0 }, 100), "UI raw begin after rapid release succeeds");
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Down,
            "UI raw history replays Down first");
        const auto generation = sample.generation;
        check(channel.TryPop(sample) && sample.kind == Kind::Move && sample.screen.x == 2
            && sample.tick == 101 && sample.generation == generation,
            "UI raw history retains move position time and generation");
        check(channel.TryPop(sample) && sample.kind == Kind::Up && sample.screen.x == 3
            && !channel.TryPop(sample), "UI raw early Up is retained exactly once");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        check(channel.BeginMouse({ 4, 0 }, 200), "UI raw legacy-before-raw arms channel");
        Sample sample;
        check(!channel.TryPop(sample), "UI raw pending begin fabricates no sample");
        auto down = packet(Kind::Down, 200, 4);
        down.device = 0; // 精密触摸板可没有设备句柄，不能因此当作触摸副本丢弃。
        Testing::PublishMouse(down);
        Testing::PublishMouse(packet(Kind::Up, 201, 5));
        check(channel.TryPop(sample) && sample.kind == Kind::Down && sample.device == 0,
            "UI raw null-device touchpad Down preserved");
        check(channel.TryPop(sample) && sample.kind == Kind::Up && sample.device == 0,
            "UI raw future Up retains gesture device identity");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        // 系统时间粒度可能让相邻两次点击共享同一个 messageTime。
        Testing::PublishMouse(packet(Kind::Down, 300, 10));
        Testing::PublishMouse(packet(Kind::Up, 300, 11));
        Testing::PublishMouse(packet(Kind::Down, 300, 20));
        Testing::PublishMouse(packet(Kind::Up, 300, 21));
        Sample sample;
        check(channel.BeginMouse({ 10, 0 }, 300) && channel.TryPop(sample)
            && sample.screen.x == 10, "UI raw identical-time clicks bind FIFO");
        const auto first = sample.generation;
        check(channel.TryPop(sample) && sample.kind == Kind::Up && sample.screen.x == 11,
            "UI raw first rapid click keeps its own release");
        channel.FinishMouse(first);
        check(channel.BeginMouse({ 20, 0 }, 300) && channel.TryPop(sample)
            && sample.screen.x == 20 && sample.generation != first,
            "UI raw second rapid click keeps independent generation");
        channel.FinishMouse(first);
        check(channel.TryPop(sample) && sample.kind == Kind::Up && sample.screen.x == 21,
            "UI raw stale finish cannot discard next gesture release");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        Testing::PublishMouse(packet(Kind::Down, 350, 1));
        Testing::PublishMouse(packet(Kind::Up, 350, 2));
        Testing::PublishMouse(packet(Kind::Down, 350, 10));
        Testing::PublishMouse(packet(Kind::Up, 350, 11));
        Sample sample;
        check(channel.BeginMouse({ 10, 0 }, 350) && channel.TryPop(sample)
            && sample.kind == Kind::Down && sample.screen.x == 10,
            "UI raw begin skips unclaimed outside click with identical timestamp");
        check(channel.TryPop(sample) && sample.kind == Kind::Up && sample.screen.x == 11,
            "UI raw targeted click receives its own Up after outside same-ms click");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        check(channel.BeginMouse({ 10, 0 }, 360), "UI raw target can arm before same-ms outside packets");
        Testing::PublishMouse(packet(Kind::Down, 360, 1));
        Testing::PublishMouse(packet(Kind::Up, 360, 2));
        Sample sample;
        check(!channel.TryPop(sample), "UI raw outside same-ms Down and Up do not bind or cancel target");
        Testing::PublishMouse(packet(Kind::Down, 360, 10));
        Testing::PublishMouse(packet(Kind::Up, 361, 11));
        check(channel.TryPop(sample) && sample.kind == Kind::Down && sample.screen.x == 10
            && channel.TryPop(sample) && sample.kind == Kind::Up && sample.screen.x == 11,
            "UI raw armed target binds later matching location without borrowing outside Up");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        namespace Dispatch = Inkeys::Window::MessageDispatch;
        // 模拟 owner 积压后逐条分发，轨迹曾越界后回到起点，不能只保留当前点。
        for (DWORD index = 0; index < 4; ++index)
        {
            MSG message{};
            message.hwnd = reinterpret_cast<HWND>(1);
            message.message = WM_INPUT;
            message.wParam = RIM_INPUTSINK;
            message.lParam = index;
            message.time = 370 + index;
            message.pt = { index == 1 ? 100000L : 0L, -80000L };
            Dispatch::Scope scope(message);
            Dispatch::Sample posted;
            check(Dispatch::TryGet(message.hwnd, message.message, message.wParam, message.lParam, posted),
                "UI raw backlog obtains production dispatch snapshot");
            auto sample = packet(index == 0 ? Kind::Down : index == 3 ? Kind::Up : Kind::Move,
                posted.time, posted.screen.x, posted.screen.y);
            sample.tick = Dispatch::PostedTick(posted.time, 900);
            Testing::PublishMouse(sample);
        }
        check(channel.BeginMouse({ 0, -80000 }, 370), "UI raw historical excursion binds target");
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Down && sample.tick == 370,
            "UI raw delayed Down retains time before replayed moves");
        check(channel.TryPop(sample) && sample.screen.x == 100000 && sample.tick == 371,
            "UI raw backlog preserves out-of-window maximum excursion and velocity time");
        check(channel.TryPop(sample) && sample.screen.x == 0 && sample.tick == 372
            && channel.TryPop(sample) && sample.kind == Kind::Up && sample.tick == 373,
            "UI raw backlog return and release retain original order and time");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        check(channel.BeginMouse({}, 400), "UI raw ordered history channel begins");
        Testing::PublishMouse(packet(Kind::Down, 400));
        for (DWORD i = 1; i <= 200; ++i)
            Testing::PublishMouse(packet(Kind::Move, 400 + i, static_cast<LONG>(i)));
        Testing::PublishMouse(packet(Kind::Up, 601, 201));
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Down, "UI raw ordered move start");
        for (DWORD i = 1; i <= 200; ++i)
            check(channel.TryPop(sample) && sample.kind == Kind::Move && sample.tick == 400 + i
                && sample.screen.x == static_cast<LONG>(i),
                "UI raw wheel velocity samples are not coalesced or retimed");
        check(channel.TryPop(sample) && sample.kind == Kind::Up,
            "UI raw ordered moves preserve terminal Up");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        Testing::SetClock(100);
        check(channel.BeginMouse({}, 620), "UI raw startup deadline begins only while unbound");
        Testing::SetClock(1099);
        Testing::PollPendingMouse();
        Sample sample;
        check(!channel.TryPop(sample), "UI raw startup deadline does not cancel before one second");
        Testing::SetClock(1100);
        Testing::PollPendingMouse();
        check(channel.TryPop(sample) && sample.kind == Kind::Cancel
            && sample.cancelReason == CancelReason::InputUnavailable,
            "UI raw missing startup Down expires with Cancel instead of synthetic Up");
        check(channel.BeginMouse({}, 621), "UI raw channel can start after missing-input cancellation");
        Testing::PublishMouse(packet(Kind::Down, 621));
        check(channel.TryPop(sample) && sample.kind == Kind::Down,
            "UI raw deadline stops applying once physical Down binds");
        Testing::SetClock(100000);
        Testing::PollPendingMouse();
        check(!channel.TryPop(sample), "UI raw bound stationary hold never times out");
        Testing::PublishMouse(packet(Kind::Up, 622));
        check(channel.TryPop(sample) && sample.kind == Kind::Up,
            "UI raw stationary hold ends only with real release");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        check(channel.BeginMouse({}, 650), "UI raw missing-Down channel arms");
        Testing::PublishMouse(packet(Kind::Down, 651));
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Cancel
            && sample.cancelReason == CancelReason::InputUnavailable,
            "UI raw missing press never binds a later unrelated Down");
        check(channel.BeginMouse({}, 651) && channel.TryPop(sample) && sample.kind == Kind::Down,
            "UI raw mismatching future press remains available to its real native Down");
        Testing::PublishMouse(packet(Kind::Up, 652));
        check(channel.TryPop(sample) && sample.kind == Kind::Up,
            "UI raw next valid press still receives its release");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        check(channel.BeginMouse({}, 700), "UI raw overflow channel begins");
        Testing::PublishMouse(packet(Kind::Down, 700));
        for (std::size_t i = 0; i < Mailbox::Capacity; ++i)
            Testing::PublishMouse(packet(Kind::Move, 701 + static_cast<DWORD>(i)));
        Testing::PublishMouse(packet(Kind::Up, 1300));
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Cancel
            && sample.cancelReason == CancelReason::Overflow && !channel.TryPop(sample),
            "UI raw transport overflow terminates explicitly instead of losing release");
        channel.FinishMouse();
        check(channel.BeginMouse({}, 700) && channel.TryPop(sample)
            && sample.kind == Kind::Cancel && sample.cancelReason == CancelReason::Overflow,
            "UI raw lost pre-begin history cancels instead of guessing a gesture");
        MouseGesture gesture;
        auto native = packet(Kind::Down, 700);
        native.generation = 60;
        check(sample.generation == 0 && gesture.Begin(native) && gesture.BeginDrag()
            && !gesture.AcceptRaw(sample) && gesture.Active(),
            "UI unbound lost history cannot cancel an actual native drag");
        native.kind = Kind::Up;
        check(gesture.AcceptNative(native), "UI native release survives unavailable Raw history");
    }

    {
        Testing::ResetProvider();
        MouseChannel bar;
        MouseChannel page;
        Testing::PublishMouse(packet(Kind::Down, 1400));
        check(bar.BeginMouse({}, 1400) && !page.BeginMouse({}, 1400),
            "UI raw physical press has one active consumer");
        Sample sample;
        check(bar.TryPop(sample) && sample.kind == Kind::Down, "UI raw global owner starts");
        check(page.BeginTouch(7), "UI touch across windows acquires transport lease");
        check(bar.TryPop(sample) && sample.kind == Kind::Cancel
            && sample.cancelReason == CancelReason::Replaced,
            "UI touch cancels other window mouse through private transport");
        check(!bar.BeginMouse({}, 1401) && !bar.BeginTouch(8),
            "UI touch owner rejects other mouse and touch begin");
        page.EndTouch(6);
        check(!bar.BeginMouse({}, 1401), "UI stale touch End cannot release newer lease");
        page.EndTouch(7);
        check(bar.BeginMouse({}, 1402), "UI touch end permits new mouse gesture");
        Testing::PublishMouse(packet(Kind::Down, 1402));
        Testing::PublishMouse(packet(Kind::Up, 1403));
        check(bar.TryPop(sample) && sample.kind == Kind::Down,
            "UI mouse after touch has new Down");
        check(bar.TryPop(sample) && sample.kind == Kind::Up,
            "UI mouse after touch has reliable release");
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        check(channel.BeginMouse({}, 0xFFFFFFFEu), "UI raw timestamp wrap channel arms");
        Testing::PublishMouse(packet(Kind::Down, 0xFFFFFFFEu));
        Testing::PublishMouse(packet(Kind::Up, 1));
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Down
            && channel.TryPop(sample) && sample.kind == Kind::Up,
            "UI raw timestamp wrap preserves ordered edges");
        channel.FinishMouse();
        Testing::ResetProvider(false);
        check(!channel.BeginMouse({}, 3), "UI raw unavailable input rejects begin");
        Testing::ResetProvider();
        check(channel.BeginMouse({}, 4), "UI raw restored input can rearm");
        Testing::PublishMouse(packet(Kind::Down, 4));
        channel.Cancel();
        check(channel.TryPop(sample) && sample.kind == Kind::Cancel
            && sample.cancelReason == CancelReason::Requested,
            "UI raw explicit cancel is distinct from normal Up");
    }
    {
        // 普通点击的生产策略不以 Raw 可用、Down 到达或精确配对为条件。
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            Testing::ResetProvider(scenario != 0);
            Testing::SetClock(10);
            MouseChannel channel;
            MouseGesture gesture;
            auto native = packet(Kind::Down, 2000, 10, 20);
            native.generation = 80 + scenario;
            check(gesture.Begin(native), "UI native click starts independently of Raw registration");
            const bool supplement = channel.BeginMouse(native.screen, native.messageTime);
            if (scenario == 0)
                check(!supplement, "UI unavailable Raw only rejects optional supplement");
            if (scenario == 2)
            {
                Testing::PublishMouse(packet(Kind::Down, 2001, 11, 21));
                Testing::PublishMouse(packet(Kind::Up, 2002, 12, 22));
            }
            Testing::SetClock(1010);
            Testing::PollPendingMouse();
            Sample sample;
            while (channel.TryPop(sample))
                check(!gesture.AcceptRaw(sample) && gesture.Active(),
                    "UI absent or mismatched Raw cannot cancel a native click");
            native.kind = Kind::Up;
            native.messageTime = 2003;
            native.tick = 2003;
            check(gesture.AcceptNative(native) && !gesture.Active()
                && gesture.Latest().kind == Kind::Up,
                "UI native Up completes click without a matching Raw Down");
            check(!gesture.AcceptNative(native), "UI repeated native Up completes only once");
            channel.FinishMouse();
        }
    }

    {
        // 未绑定补充的 1000ms 期限不属于原生拖动；慢拖、快拖与长按都不能被它结束。
        for (const bool mismatchedDown : { false, true })
        {
            Testing::ResetProvider();
            Testing::SetClock(10000);
            MouseChannel channel;
            MouseGesture gesture;
            auto native = packet(Kind::Down, 10000, 10, 20);
            native.generation = 300;
            check(gesture.Begin(native) && channel.BeginMouse(native.screen, native.messageTime),
                "UI native drag prepares an optional unbound channel");
            native.kind = Kind::Move;
            native.screen.x = 30;
            native.tick = native.messageTime = 10001;
            check(gesture.AcceptNative(native) && gesture.BeginDrag(),
                "UI actual native drag starts without Raw ownership");
            Sample unboundCancel;
            int cancelCount = 0;
            for (DWORD offset : { 999u, 1000u, 1500u, 5000u })
            {
                Testing::SetClock(10000 + offset);
                if (mismatchedDown && offset == 999)
                    Testing::PublishMouse(packet(Kind::Down, 10002, 11, 21));
                Testing::PollPendingMouse();
                Sample sample;
                while (channel.TryPop(sample))
                {
                    ++cancelCount;
                    unboundCancel = sample;
                    check(sample.kind == Kind::Cancel && sample.generation == 0
                        && sample.cancelReason == CancelReason::InputUnavailable
                        && !gesture.AcceptRaw(sample) && gesture.Active() && gesture.Dragging(),
                        "UI unbound timeout or mismatch revokes only Raw during actual native drag");
                    channel.FinishMouse();
                }
                check(cancelCount == ((mismatchedDown || offset >= 1000) ? 1 : 0),
                    "UI optional Raw startup cancels once at its original deadline or mismatch");
                native.screen.x += 10;
                native.tick = native.messageTime = 10000 + offset;
                check(gesture.AcceptNative(native) && gesture.Latest().tick == native.tick
                    && gesture.Latest().screen.x == native.screen.x && gesture.Active(),
                    "UI native movement remains effective beyond one second and multiple seconds");
            }
            native.kind = Kind::Up;
            native.tick = native.messageTime = 16000;
            check(gesture.AcceptNative(native) && !gesture.Active()
                && gesture.Latest().kind == Kind::Up && !gesture.AcceptNative(native)
                && !gesture.AcceptRaw(unboundCancel),
                "UI long native drag ends once only on actual native Up");
            native.kind = Kind::Down;
            native.generation = 301;
            native.tick = native.messageTime = 17000;
            check(gesture.Begin(native) && gesture.BeginDrag()
                && !gesture.AcceptRaw(unboundCancel) && gesture.Active(),
                "UI old unbound cancellation cannot disable or end the next drag");
            auto raw = native;
            raw.generation = 400;
            check(!gesture.AcceptRaw(raw), "UI next drag can still bind after stale cancel rejection");
            raw.kind = Kind::Cancel;
            raw.cancelReason = CancelReason::InputUnavailable;
            ++raw.generation;
            check(!gesture.AcceptRaw(raw) && gesture.Active(),
                "UI cancellation with another bound generation cannot end current drag");
            --raw.generation;
            check(gesture.AcceptRaw(raw) && !gesture.Active()
                && gesture.Latest().kind == Kind::Cancel,
                "UI matching bound transport failure explicitly cancels actual drag");
        }
    }

    {
        Testing::ResetProvider();
        MouseChannel channel;
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2100, 10, 20);
        native.generation = 90;
        check(gesture.Begin(native), "UI drag candidate begins on native Down");
        // 快速越界并释放已先到达采集线程，消费者开始时仍重放完整轨迹。
        Testing::PublishMouse(packet(Kind::Down, 2100, 10, 20));
        Testing::PublishMouse(packet(Kind::Move, 2101, 1000, 20));
        Testing::PublishMouse(packet(Kind::Up, 2102, 1100, 20));
        check(channel.BeginMouse(native.screen, native.messageTime),
            "UI optional drag replays release before consumer begins");
        Sample sample;
        check(channel.TryPop(sample) && sample.kind == Kind::Down
            && !gesture.AcceptRaw(sample) && gesture.Latest().kind == Kind::Down,
            "UI Raw Down establishes supplement without duplicate business Down");
        check(channel.TryPop(sample) && gesture.AcceptRaw(sample)
            && gesture.Latest().generation == native.generation
            && gesture.Latest().device == native.device
            && gesture.Latest().screen.x == 1000 && gesture.Latest().tick == 2101,
            "UI candidate Raw Move preserves trajectory time and native business identity");
        check(gesture.BeginDrag(), "UI candidate can cross threshold using outside Raw Move");
        check(channel.TryPop(sample) && gesture.AcceptRaw(sample) && !gesture.Active()
            && gesture.Latest().kind == Kind::Up && gesture.Latest().screen.x == 1100,
            "UI early Raw Up ends actual outside drag");
        native.kind = Kind::Up;
        native.messageTime = 2102;
        check(!gesture.AcceptNative(native), "UI Raw-first then native Up completes drag once");
        channel.FinishMouse();
    }

    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2200, 5, 6);
        native.generation = 91;
        auto raw = native;
        raw.generation = 191;
        raw.device = 99;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw),
            "UI native pending click may have optional Raw identity");
        raw.kind = Kind::Up;
        raw.messageTime = 2201;
        check(!gesture.AcceptRaw(raw) && gesture.Active() && !gesture.Dragging(),
            "UI Raw Up cannot commit un-dragged candidate click");
        native.kind = Kind::Up;
        native.messageTime = 2201;
        check(gesture.AcceptNative(native) && gesture.Latest().kind == Kind::Up,
            "UI native Up still commits un-dragged candidate click");
    }

    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2300, 0, 0);
        native.generation = 92;
        auto raw = native;
        raw.generation = 192;
        check(gesture.Begin(native) && gesture.BeginDrag(),
            "UI native drag begins before optional Raw identity");
        native.kind = Kind::Move;
        native.tick = native.messageTime = 2301;
        native.screen.x = 10;
        check(gesture.AcceptNative(native), "UI native drag movement accepted before Raw binding");
        check(!gesture.AcceptRaw(raw), "UI late Raw Down establishes optional movement authority");
        raw.kind = Kind::Move;
        raw.tick = raw.messageTime = 2301;
        raw.screen.x = 10;
        check(!gesture.AcceptRaw(raw) && gesture.Latest().tick == 2301,
            "UI duplicate Raw movement cannot double-count dial path or speed");
        raw.tick = raw.messageTime = 2302;
        check(!gesture.AcceptRaw(raw) && gesture.Latest().tick == 2301,
            "UI same-position later Raw report does not retime velocity history");
        raw.screen.x = 20;
        check(gesture.AcceptRaw(raw) && gesture.Latest().tick == 2302,
            "UI distinct next Raw movement preserves sample time");
        native.tick = native.messageTime = 2301;
        native.screen.x = 10;
        check(!gesture.AcceptNative(native), "UI delayed native Move cannot rewind Raw trajectory");
        native.kind = Kind::Up;
        check(gesture.AcceptNative(native) && !gesture.Active()
            && gesture.Latest().kind == Kind::Up && gesture.Latest().screen.x == 20
            && gesture.Latest().tick == 2302 && gesture.Latest().messageTime == 2302
            && gesture.Latest().generation == native.generation,
            "UI older native Up completes without rewinding accepted drag position or time");
        raw.kind = Kind::Up;
        raw.messageTime = 2303;
        check(!gesture.AcceptRaw(raw), "UI native-first then Raw Up completes once");
        const auto oldNative = native;
        native.kind = Kind::Down;
        native.generation = 93;
        native.tick = native.messageTime = 2304;
        check(gesture.Begin(native) && gesture.BeginDrag(), "UI rapid next drag gets new native identity");
        check(!gesture.AcceptNative(oldNative) && !gesture.AcceptRaw(raw) && gesture.Active(),
            "UI previous native and Raw Up cannot end next gesture");
        auto stale = native;
        stale.kind = Kind::Up;
        stale.messageTime = 2303;
        check(!gesture.AcceptNative(stale), "UI native Up older than current Down is rejected");
        native.kind = Kind::Cancel;
        native.cancelReason = CancelReason::Requested;
        check(gesture.AcceptNative(native) && gesture.Latest().kind == Kind::Cancel,
            "UI explicit lifecycle cancel remains distinct from successful release");
    }

    {
        Testing::ResetProvider();
        for (const bool rawFirst : { false, true })
        {
            MouseGesture gesture;
            auto native = packet(Kind::Down, 2350);
            native.generation = 193;
            auto raw = native;
            raw.generation = 293;
            check(gesture.Begin(native) && !gesture.AcceptRaw(raw),
                "UI same-ms movement test begins native gesture with optional Raw identity");
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool rawSource = pass == 0 ? rawFirst : !rawFirst;
                for (LONG position : { 10L, 20L, 0L })
                {
                    auto move = rawSource ? raw : native;
                    move.kind = Kind::Move;
                    move.tick = move.messageTime = 2351;
                    move.screen.x = position;
                    const bool accepted = rawSource ? gesture.AcceptRaw(move)
                        : gesture.AcceptNative(move);
                    check(accepted == rawSource,
                        "UI bound Raw exclusively preserves same-ms path regardless of native queue order");
                }
            }
            check(gesture.Latest().screen.x == 0 && gesture.Latest().tick == 2351,
                "UI native duplicate stream cannot repeat bound Raw excursion or retime velocity");
        }
    }

    {
        Testing::ResetProvider();
        for (const auto reason : { CancelReason::InputUnavailable, CancelReason::Overflow })
        for (const bool dragging : { false, true })
        {
            MouseGesture gesture;
            auto native = packet(Kind::Down, 2400);
            native.generation = 94;
            auto raw = native;
            raw.generation = 194;
            check(gesture.Begin(native) && !gesture.AcceptRaw(raw),
                "UI overflow policy begins independent native gesture");
            if (dragging) check(gesture.BeginDrag(), "UI overflow policy enters drag");
            raw.kind = Kind::Cancel;
            raw.cancelReason = reason;
            const bool ended = gesture.AcceptRaw(raw);
            check(ended == dragging && gesture.Active() != dragging,
                "UI bound failure cancels actual drag while preserving ordinary pending click");
            if (dragging)
                check(gesture.Latest().kind == Kind::Cancel,
                    "UI overflow cannot invent successful drag Up");
            else
            {
                native.kind = Kind::Up;
                check(gesture.AcceptNative(native), "UI native click survives supplemental queue overflow");
            }
        }
    }

    for (const auto reason : { CancelReason::InputUnavailable, CancelReason::Overflow })
    {
        Testing::ResetProvider();
        MouseChannel channel;
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2450);
        native.generation = 195;
        check(gesture.Begin(native) && gesture.BeginDrag() && channel.BeginMouse({}, 2450),
            "UI native drag may start before consuming optional Raw Down");
        Testing::PublishMouse(packet(Kind::Down, 2450));
        if (reason == CancelReason::Overflow)
        {
            for (std::size_t index = 0; index < Mailbox::Capacity; ++index)
                Testing::PublishMouse(packet(Kind::Move, 2451 + static_cast<DWORD>(index)));
        }
        else channel.Cancel(reason);
        Sample cancel;
        check(channel.TryPop(cancel) && cancel.kind == Kind::Cancel
            && cancel.cancelReason == reason && cancel.generation != 0
            && gesture.AcceptRaw(cancel) && !gesture.Active(),
            "UI bound-channel failure cancels actual drag even before consumer sees Raw Down");
        channel.FinishMouse();
    }

    {
        Testing::ResetProvider();
        MouseChannel otherWindow;
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2500);
        native.generation = 95;
        check(gesture.Begin(native) && otherWindow.BeginTouch(100),
            "UI native-only click observes cross-window touch ownership");
        check(gesture.SourceReplaced(), "UI source replacement is visible without native or Raw packet");
        otherWindow.EndTouch(100);
        check(gesture.SourceReplaced(), "UI completed touch still invalidates previous mouse gesture");
        native.kind = Kind::Up;
        check(!gesture.AcceptNative(native) && gesture.Active() && gesture.SourceReplaced(),
            "UI native Up after source takeover cannot be accepted as successful old click");
        native.kind = Kind::Cancel;
        native.cancelReason = CancelReason::Replaced;
        check(gesture.AcceptNative(native) && gesture.Latest().kind == Kind::Cancel,
            "UI explicit source-replacement cancellation retires old click and feedback");
        check(otherWindow.BeginTouch(101), "UI Pen compatibility can use nonmouse ownership lease");
        native.kind = Kind::Down;
        native.generation = 96;
        check(!gesture.Begin(native), "UI native mouse cannot begin while nonmouse lease active");
        otherWindow.EndTouch(100);
        check(!gesture.Begin(native), "UI stale End cannot release newer nonmouse lease");
        otherWindow.EndTouch(101);
        check(gesture.Begin(native) && !gesture.SourceReplaced(),
            "UI fresh native press works after current nonmouse lease ends");
        native.kind = Kind::Up;
        check(gesture.AcceptNative(native), "UI fresh click after touch completes normally");
    }
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2600);
        native.generation = 97;
        auto raw = native;
        raw.generation = 197;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw),
            "UI candidate begins before optional raw replacement");
        raw.kind = Kind::Cancel;
        raw.cancelReason = CancelReason::Replaced;
        check(!gesture.AcceptRaw(raw) && gesture.Active() && !gesture.SourceReplaced(),
            "UI raw-only replacement revokes supplement without cancelling native click");
        native.kind = Kind::Up;
        native.messageTime = 2601;
        check(gesture.AcceptNative(native) && gesture.Latest().kind == Kind::Up,
            "UI native Up survives missing raw release followed by raw replacement");
    }

    {
        Testing::ResetProvider();
        MouseChannel otherWindow;
        auto native = packet(Kind::Down, 2700);
        native.generation = 98;
        MouseGesture queuedAdmission;
        check(queuedAdmission.Begin(native), "UI native Down snapshots source admission before queueing");
        check(otherWindow.BeginTouch(102), "UI queued mouse Down can be superseded by another window");
        otherWindow.EndTouch(102);
        // Bar metadata copies this value policy across the shared mailbox, without owning Raw or business.
        MouseGesture dequeued = queuedAdmission;
        check(dequeued.Active() && dequeued.SourceReplaced(),
            "UI dequeued admission cannot resurrect mouse Down across completed touch takeover");
        check(otherWindow.BeginTouch(103), "UI second nonmouse lease begins before queued Down");
        MouseGesture rejectedAdmission;
        native.generation = 99;
        check(!rejectedAdmission.Begin(native), "UI source admission rejects Down received during touch");
        otherWindow.EndTouch(103);
        dequeued = rejectedAdmission;
        check(!dequeued.Active(), "UI ended lease cannot revive rejected queued Down");
        native.generation = 100;
        check(dequeued.Begin(native) && !dequeued.SourceReplaced(),
            "UI fresh Down after takeover still starts normally");
    }
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2800);
        native.generation = 101;
        auto raw = native;
        raw.generation = 201;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw) && gesture.BeginDrag(),
            "UI older raw terminal test begins an actual native drag");
        raw.kind = Kind::Move;
        raw.screen.x = 60;
        raw.tick = raw.messageTime = 2802;
        check(gesture.AcceptRaw(raw), "UI accepted drag movement precedes delayed Raw Up");
        raw.kind = Kind::Up;
        raw.screen.x = 30;
        raw.tick = raw.messageTime = 2801;
        check(gesture.AcceptRaw(raw) && !gesture.Active() && gesture.Latest().kind == Kind::Up
            && gesture.Latest().screen.x == 60 && gesture.Latest().messageTime == 2802
            && gesture.Latest().tick == 2802 && gesture.Latest().generation == 101,
            "UI older Raw Up ends once without rewinding accepted drag position or time");
    }
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 2900);
        native.generation = 102;
        auto raw = native;
        raw.generation = 202;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw),
            "UI Raw movement authority binds before native movement");
        native.kind = Kind::Move;
        native.screen.x = 1;
        native.tick = native.messageTime = 2901;
        check(!gesture.AcceptNative(native), "UI bound native coalesced move cannot own Raw timestamp");
        raw.kind = Kind::Move;
        raw.screen.x = 50;
        raw.tick = raw.messageTime = 2901;
        check(gesture.AcceptRaw(raw) && gesture.Latest().screen.x == 50 && gesture.BeginDrag(),
            "UI full Raw same-ms excursion survives native-first message arrival");
        raw.screen.x = 0;
        check(gesture.AcceptRaw(raw) && gesture.Latest().screen.x == 0,
            "UI same-ms Raw return preserves complete excursion");
        native.kind = Kind::Up;
        native.screen.x = 0;
        check(gesture.AcceptNative(native), "UI native release still ends Raw-owned movement");
    }

    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 3000);
        native.generation = 103;
        auto raw = native;
        raw.generation = 203;
        check(gesture.Begin(native), "UI late-bind floor begins on native Down");
        native.kind = Kind::Move;
        native.screen.x = 10;
        native.tick = native.messageTime = 3001;
        check(gesture.AcceptNative(native) && !gesture.AcceptRaw(raw),
            "UI native movement establishes floor before late Raw binding");
        raw.kind = Kind::Move;
        raw.screen.x = 50;
        raw.tick = raw.messageTime = 3001;
        check(!gesture.AcceptRaw(raw) && gesture.Latest().screen.x == 10,
            "UI late binding cannot replay ambiguous already-consumed same-ms trajectory");
        raw.screen.x = 20;
        raw.tick = raw.messageTime = 3002;
        check(gesture.AcceptRaw(raw), "UI late binding accepts later ordered Raw movement");
        raw.kind = Kind::Cancel;
        raw.cancelReason = CancelReason::InputUnavailable;
        check(!gesture.AcceptRaw(raw) && gesture.Active(),
            "UI candidate transport failure preserves native fallback");
        native.kind = Kind::Move;
        native.screen.x = 30;
        native.tick = native.messageTime = 3003;
        check(gesture.AcceptNative(native), "UI native movement resumes after candidate Raw failure");
        native.kind = Kind::Up;
        check(gesture.AcceptNative(native), "UI native fallback Up still completes candidate");
    }
    for (bool rawUpFirst : { false, true })
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 4000);
        native.generation = 104;
        auto raw = native;
        raw.generation = 204;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw) && gesture.BeginDrag(),
            "UI silent Raw fallback begins with bound drag");
        raw.kind = Kind::Move;
        raw.screen.x = 20;
        raw.tick = raw.messageTime = 4001;
        check(gesture.AcceptRaw(raw), "UI normal Raw movement starts before silence");
        native.kind = Kind::Move;
        native.screen.x = 40;
        native.tick = native.messageTime = 4050;
        check(!gesture.AcceptNative(native), "UI fresh Raw suppresses native movement before fifty ms");
        native.tick = native.messageTime = 4051;
        check(gesture.AcceptNative(native) && gesture.Latest().screen.x == 40
            && gesture.RawBound() && !gesture.RawDisabled(),
            "UI fifty-ms event gap permits native movement without ending Raw ownership");
        for (DWORD time : { 5000u, 8000u })
        {
            native.screen.x += 10;
            native.tick = native.messageTime = time;
            check(gesture.AcceptNative(native) && gesture.Active(),
                "UI silent bound Raw allows native drag for arbitrary seconds");
        }
        raw.screen.x = -100;
        raw.tick = raw.messageTime = 7999;
        check(!gesture.AcceptRaw(raw) && gesture.Latest().screen.x == 60,
            "UI delayed Raw before native floor cannot rewind resumed drag");
        raw.tick = raw.messageTime = 8000;
        check(!gesture.AcceptRaw(raw), "UI same-ms Raw cannot replay native-consumed trajectory");
        native.tick = native.messageTime = 8100;
        check(!gesture.AcceptNative(native) && gesture.Active(),
            "UI stationary native takeover advances floor without fake movement or release");
        raw.tick = raw.messageTime = 8099;
        check(!gesture.AcceptRaw(raw), "UI stationary native continuation also blocks older Raw replay");
        raw.tick = raw.messageTime = 8100;
        check(!gesture.AcceptRaw(raw), "UI stationary native continuation blocks same-ms Raw replay");
        raw.screen.x = 70;
        raw.tick = raw.messageTime = 8101;
        check(gesture.AcceptRaw(raw) && gesture.Latest().screen.x == 70,
            "UI newer Raw movement safely resumes after native fallback");
        native.screen.x = 75;
        native.tick = native.messageTime = 8102;
        check(!gesture.AcceptNative(native), "UI resumed Raw restores single movement authority");
        raw.kind = Kind::Up;
        raw.tick = raw.messageTime = 8103;
        native.kind = Kind::Up;
        native.tick = native.messageTime = 8104;
        if (rawUpFirst)
            check(gesture.AcceptRaw(raw) && !gesture.AcceptNative(native) && !gesture.Active(),
                "UI Raw Up after fallback ends once before native Up");
        else
            check(gesture.AcceptNative(native) && !gesture.AcceptRaw(raw) && !gesture.Active(),
                "UI native Up after fallback ends once before Raw Up");
    }
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 9000);
        native.generation = 105;
        auto raw = native;
        raw.generation = 205;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw),
            "UI Raw freshness test binds optional identity");
        raw.kind = Kind::Move;
        raw.tick = raw.messageTime = 9035;
        check(!gesture.AcceptRaw(raw) && gesture.RawActivityTime() == 9035,
            "UI valid repeated-coordinate Raw refreshes arrival without manufacturing movement");
        native.kind = Kind::Move;
        native.screen.x = 20;
        native.tick = native.messageTime = 9084;
        check(!gesture.AcceptNative(native), "UI repeated-coordinate Raw keeps native suppressed for fifty ms");
        native.tick = native.messageTime = 9085;
        check(gesture.AcceptNative(native), "UI repeated-coordinate Raw still permits fallback after silence");
        raw.screen.x = -30;
        raw.tick = raw.messageTime = 9084;
        check(!gesture.AcceptRaw(raw) && gesture.RawActivityTime() == 9035,
            "UI old-floor Raw neither refreshes freshness nor takes movement authority");
        raw.screen.x = 25;
        raw.tick = raw.messageTime = 9090;
        check(gesture.AcceptRaw(raw), "UI later valid Raw takes movement authority");
        raw.screen.x = -30;
        raw.tick = raw.messageTime = 9089;
        check(!gesture.AcceptRaw(raw) && gesture.RawActivityTime() == 9090,
            "UI out-of-order Raw cannot refresh last activity");
    }
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 0xFFFFFFD0u);
        native.generation = 106;
        auto raw = native;
        raw.generation = 206;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw) && gesture.BeginDrag(),
            "UI silent stream test begins across DWORD timestamp wrap");
        native.kind = Kind::Move;
        native.screen.x = 20;
        native.messageTime = 1;
        check(!gesture.AcceptNative(native), "UI wrapped event gap below fifty ms remains fresh");
        native.messageTime = 2;
        check(gesture.AcceptNative(native), "UI wrapped fifty-ms event gap permits fallback");
        raw.kind = Kind::Move;
        raw.screen.x = -20;
        raw.messageTime = 0xFFFFFFF0u;
        check(!gesture.AcceptRaw(raw), "UI wrapped native floor rejects older Raw movement");
        raw.messageTime = 2;
        check(!gesture.AcceptRaw(raw), "UI wrapped native floor rejects same-ms Raw movement");
        raw.screen.x = 25;
        raw.messageTime = 3;
        check(gesture.AcceptRaw(raw), "UI newer wrapped Raw safely resumes movement");
        raw.kind = Kind::Up;
        check(gesture.AcceptRaw(raw) && !gesture.Active(),
            "UI wrapped fallback preserves real Raw release");
    }
    {
        Testing::ResetProvider();
        MouseGesture gesture;
        auto native = packet(Kind::Down, 10000);
        native.generation = 107;
        auto raw = native;
        raw.generation = 207;
        check(gesture.Begin(native) && !gesture.AcceptRaw(raw) && gesture.BeginDrag(),
            "UI Raw release listener test binds actual drag");
        native.kind = Kind::Move;
        native.screen.x = 80;
        native.tick = native.messageTime = 10050;
        check(gesture.AcceptNative(native), "UI native takeover precedes Raw-only release");
        raw.kind = Kind::Up;
        raw.screen.x = 20;
        raw.tick = raw.messageTime = 10020;
        check(gesture.AcceptRaw(raw) && !gesture.Active() && gesture.Latest().screen.x == 80
            && gesture.Latest().messageTime == 10050,
            "UI Raw Up still ends native-owned movement without rewinding accepted position");
        native.kind = Kind::Up;
        native.messageTime = 10060;
        check(!gesture.AcceptNative(native), "UI native Up cannot complete fallback gesture twice");
    }
    {
        namespace Haptic = Inkeys::UI::Bar::Haptics;
        Haptic::ButtonMotionGate motion;
        check(!motion.IsClick(), "Haptic palette tap needs a new Down");
        motion.Begin({ 100, 100 }, 4, 4);
        motion.Observe({ 103, 98 });
        check(motion.IsClick(), "Haptic palette tap tolerates small pen jitter");
        motion.Observe({ 105, 100 });
        motion.Observe({ 100, 100 });
        check(!motion.IsClick(), "Haptic palette drag cannot regain tap feedback by returning");
        motion.Begin({ 100, 100 }, 4, 4);
        motion.Cancel();
        check(!motion.IsClick(), "Haptic cancelled palette gesture cannot tap");
        auto pen = std::make_shared<Inkeys::Drawing::Draw3::Haptics::ClickSource>();
        check(!Haptic::AcceptedClick(pen, {}, WM_LBUTTONUP, false), "Haptic mouse Up cannot inherit pen Down");
        check(!Haptic::AcceptedClick(pen, pen, WM_MOUSEMOVE, false), "Haptic lost button Move is not accepted Up");
        check(!Haptic::AcceptedClick(pen, pen, WM_LBUTTONUP, true), "Haptic dragged or cancelled release rejected");
        check(Haptic::AcceptedClick(pen, pen, WM_LBUTTONUP, false) == pen, "Haptic accepted button retains exact contact");
        // 普通按钮由业务命中接受，不能被选色板轻点的移动门限再次否决。
        motion.Begin({ 100, 100 }, 4, 4);
        motion.Observe({ 115, 110 });
        check(!motion.IsClick() && Haptic::AcceptedClick(pen, pen, WM_LBUTTONUP, false) == pen,
            "Haptic accepted ordinary button survives pen motion beyond mouse drag threshold");
        {
            Haptic::AcceptedClickScope callback(pen);
            Haptic::RejectButtonAction();
            check(!Haptic::buttonActionAccepted, "Haptic business rejection suppresses callback feedback");
            { Haptic::AcceptedClickScope nested({}); check(Haptic::buttonActionAccepted, "Haptic callback has fresh decision"); }
            check(!Haptic::buttonActionAccepted, "Haptic nested callback restores outer rejection");
        }
        check(!Haptic::acceptedClick && Haptic::buttonActionAccepted, "Haptic scope does not leak into programmatic callback");
    }
    {
        namespace Diag=Inkeys::Input::UiPointer::Diagnostics;
        namespace HapticDiag=Inkeys::Drawing::Draw3::HapticDiagnostics;
        const bool previous=HapticDiag::Enabled();
        const DWORD previousError=GetLastError();
        const auto oldSink=Diag::testSink;
        struct RestoreDiagnostic { bool enabled;DWORD error;decltype(Diag::testSink) sink;~RestoreDiagnostic(){Diag::testSink=sink;HapticDiag::SetEnabled(enabled);SetLastError(error);} } restore{previous,previousError,oldSink};
        Diag::testSink=[](const char* line,std::size_t size) { uiDiagnosticText.append(line,size);uiDiagnosticProviderReadable=RawMouseAvailable();SetLastError(9876); };
        uiDiagnosticText.clear();Testing::ResetProvider(true);
        HapticDiag::SetEnabled(false);SetLastError(1234);
        Diag::Log("test",nullptr,"disabled","not_emitted",WM_LBUTTONDOWN,nullptr,"test=1");
        check(!Diag::Raw("test",reinterpret_cast<HWND>(1),WM_TOUCH,1,2,3)&&uiDiagnosticText.empty()&&GetLastError()==1234,"UI diagnostics disabled fast return preserves LastError without querying invalid HWND");
        for(const UINT message : {WM_RBUTTONDOWN,WM_RBUTTONDBLCLK,WM_MBUTTONDOWN,WM_MBUTTONDBLCLK,WM_XBUTTONDOWN,WM_XBUTTONDBLCLK})
            check(Diag::InputMessage(message)&&Diag::RawMessageKind(message)==Kind::Down&&std::strcmp(Diag::MessageName(message),"other")!=0,"UI diagnostics identify side/middle/X button presses without treating them as Move");
        for(const UINT message : {WM_RBUTTONUP,WM_MBUTTONUP,WM_XBUTTONUP})
            check(Diag::InputMessage(message)&&Diag::RawMessageKind(message)==Kind::Up&&std::strcmp(Diag::MessageName(message),"other")!=0,"UI diagnostics identify side/middle/X button releases as terminal");
        check(std::strcmp(Diag::MessageName(WM_POINTERENTER),"WM_POINTERENTER")==0&&std::strcmp(Diag::MessageName(WM_POINTERCAPTURECHANGED),"WM_POINTERCAPTURECHANGED")==0,"UI diagnostics name pointer enter and capture change messages");
        HapticDiag::SetEnabled(true);
        Sample sample;sample.kind=Kind::Down;sample.source=Source::Touch;sample.generation=7;sample.contact=12;sample.device=45;sample.screen={101,202};sample.messageTime=555;
        Diag::Log("test",nullptr,"format","accepted",WM_LBUTTONDOWN,&sample,"owner_generation=%u",91u);
        check(GetLastError()==1234&&uiDiagnosticText.find("v=1")!=std::string::npos&&uiDiagnosticText.find("contact=12 generation=7 device=0x2d")!=std::string::npos&&uiDiagnosticText.find("time=555 point=101,202")!=std::string::npos&&uiDiagnosticText.find("owner_generation=91")!=std::string::npos,"UI diagnostics preserve error and correlated fields through sink");
        uiDiagnosticText.clear();
        Diag::Log("test",nullptr,"unclassified_move","filtered",WM_MOUSEMOVE,nullptr,"extra=0x%llx",0ULL);
        check(uiDiagnosticText.find("source=unknown phase=move")!=std::string::npos&&GetLastError()==1234,"UI diagnostic limiter placeholder cannot classify an unknown Move as physical Mouse");
        Diag::MoveLimiter limiter;unsigned suppressed=0;sample.kind=Kind::Move;
        check(limiter.Admit(1,sample,2,1000,suppressed),"UI diagnostic first Move visible");
        check(!limiter.Admit(1,sample,2,1249,suppressed)&&limiter.Admit(1,sample,2,1250,suppressed)&&suppressed==1,"UI diagnostic Move boundary and suppressed count");
        check(!limiter.Admit(1,sample,2,1251,suppressed),"UI diagnostic repeated Move throttled");
        auto anotherSource=sample;anotherSource.source=Source::Pen;check(limiter.Admit(1,anotherSource,2,1251,suppressed),"UI diagnostic limiter isolates sources");
        sample.kind=Kind::Up;check(limiter.Admit(1,sample,2,1252,suppressed)&&suppressed==1,"UI diagnostic Up never throttled and flushes count");
        sample.kind=Kind::Cancel;check(limiter.Admit(1,sample,2,1252,suppressed),"UI diagnostic Cancel never throttled");
        for(unsigned i=0;i<1000;++i){sample.kind=Kind::Move;sample.contact=i;limiter.Admit(i,sample,2,2000,suppressed);}
        check(limiter.entries.size()==32,"UI diagnostic tracking stays bounded across many contacts");
        std::string longDetail(900,'x');Diag::Log("test",nullptr,"format","large_detail",0,nullptr,"%s",longDetail.c_str());
        check(uiDiagnosticText.find("truncated=1")!=std::string::npos,"UI diagnostic truncation visible");
        uiDiagnosticText.clear();
        {
            MouseChannel owner,other;
            check(owner.BeginTouch(401)&&!other.BeginTouch(402),"Diagnostic lease does not alter owner rejection");
            owner.EndTouch(400);owner.Cancel();
            check(!other.BeginTouch(403),"Diagnostic stale End and Raw Cancel do not release Touch");
            owner.EndTouch(401);check(other.BeginTouch(404),"Diagnostic matching End releases Touch");
        }
        check(uiDiagnosticProviderReadable&&uiDiagnosticText.find("other_channel_owns_lease")!=std::string::npos&&uiDiagnosticText.find("stale_generation")!=std::string::npos&&uiDiagnosticText.find("raw_cancel_does_not_release_touch")!=std::string::npos&&uiDiagnosticText.find("lease_destroy")!=std::string::npos,"UI lease snapshot logs outside provider lock including ignored releases");
    }
    {
        namespace Diag=Inkeys::Input::UiPointer::Diagnostics;
        namespace HapticDiag=Inkeys::Drawing::Draw3::HapticDiagnostics;
        const bool previous=HapticDiag::Enabled();const DWORD previousError=GetLastError();const auto oldSink=Diag::testSink;
        struct RestoreNativeDiagnostic {bool enabled;DWORD error;decltype(Diag::testSink) sink;~RestoreNativeDiagnostic(){Diag::testSink=sink;HapticDiag::SetEnabled(enabled);SetLastError(error);}} restore{previous,previousError,oldSink};
        NativeDiagnosticFixture fixture;
        Diag::testSink=[](const char* line,std::size_t size){uiDiagnosticText.append(line,size);SetLastError(9876);};
        const HWND window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1234));
        HapticDiag::SetEnabled(false);uiDiagnosticText.clear();SetLastError(1234);
        Diag::TouchRegistration("bar",window,0,TRUE,9999);
        Diag::WindowSnapshot("bar",window,"disabled",WM_POINTERDOWN,77);
        Diag::PointerDownSnapshot("bar",window,WM_POINTERDOWN,77,"wndproc");
        check(fixture.Count()==0&&uiDiagnosticText.empty()&&GetLastError()==1234,
            "Touch registration diagnostics off perform no native window or Pointer queries and preserve GLE");
        HapticDiag::SetEnabled(true);fixture.ownerError=55;fixture.touchError=66;
        Diag::TouchRegistration("bar",window,0,TRUE,9999);
        check(GetLastError()==1234&&fixture.windowQueries==3&&fixture.ownerQueries==1&&fixture.touchQueries==1&&fixture.pointerQueries==0
            &&fixture.beforeTouchError==0&&uiDiagnosticText.find("registration_result=1 registration_error=0 registration_error_applicable=0 requested_flags=0x0")!=std::string::npos
            &&uiDiagnosticText.find("owner_pid=42 owner_matches=1 owner_error=0")!=std::string::npos
            &&uiDiagnosticText.find("is_touch=1 is_touch_flags=0x2 is_touch_flags_valid=1 is_touch_error=0")!=std::string::npos,
            "Touch registration success ignores stale GLE, reads back flags on owner thread and restores caller error");
        check(uiDiagnosticText.find("stage=touch_window_snapshot reason=after_registration msg=other/0x0 source=none phase=none")!=std::string::npos&&uiDiagnosticText.find("phase=down")==std::string::npos,
            "Touch registration readback is a window-state snapshot, not a fabricated input Down");
        uiDiagnosticText.clear();fixture.touch=FALSE;fixture.touchFlags=0;fixture.touchError=0;fixture.ownerTid=0;fixture.ownerError=ERROR_INVALID_WINDOW_HANDLE;
        Diag::TouchRegistration("page_bottom_left",window,0,FALSE,ERROR_ACCESS_DENIED);
        check(GetLastError()==1234&&uiDiagnosticText.find("registration_result=0 registration_error=5 registration_error_applicable=1")!=std::string::npos
            &&uiDiagnosticText.find("owner_matches=0 owner_error=1400")!=std::string::npos
            &&uiDiagnosticText.find("is_touch=0 is_touch_flags=0x0 is_touch_flags_valid=0 is_touch_error=0")!=std::string::npos,
            "Touch registration failure keeps captured failure code while an unregistered readback does not inherit old GLE");
        uiDiagnosticText.clear();fixture.touchError=ERROR_INVALID_WINDOW_HANDLE;
        Diag::WindowSnapshot("page_bottom_left",window,"pointer_down",WM_POINTERDOWN,77);
        check(GetLastError()==1234&&fixture.beforeTouchError==0&&uiDiagnosticText.find("is_touch_error=1400")!=std::string::npos,
            "Touch snapshot distinguishes observed IsTouchWindow query error from plain false without altering GLE");
        fixture.ownerTid=GetCurrentThreadId()+1;fixture.ownerError=0;fixture.touch=TRUE;fixture.touchError=0;
        fixture.info.pointerId=77;fixture.info.pointerType=PT_PEN;fixture.info.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT;
        fixture.info.sourceDevice=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x4567));fixture.info.hwndTarget=window;
        fixture.info.frameId=88;fixture.info.dwTime=54321;fixture.info.ptPixelLocation={100,200};fixture.info.ptPixelLocationRaw={101,202};
        fixture.pointerError=67;uiDiagnosticText.clear();
        const unsigned before=fixture.Count();
        Diag::PointerDownSnapshot("bar",window,WM_POINTERDOWN,77,"channel_callback");
        Diag::PointerDownSnapshot("bar",window,WM_POINTERUPDATE,77,"wndproc");
        check(fixture.Count()==before&&uiDiagnosticText.empty(),"Pointer native snapshot skips channel duplicate and Move before all queries");
        Diag::PointerDownSnapshot("bar",window,WM_POINTERDOWN,77,"wndproc");
        Diag::PointerDownSnapshot("bar",window,WM_POINTERDOWN,77,"wndproc");
        check(GetLastError()==1234&&fixture.pointerQueries==2&&fixture.requestedPointer==77&&fixture.beforePointerError==0
            &&uiDiagnosticText.find("stage=touch_window_snapshot reason=pointer_down")!=std::string::npos
            &&uiDiagnosticText.find("owner_matches=0 owner_error=0")!=std::string::npos
            &&uiDiagnosticText.find("source=pen phase=down contact=77")!=std::string::npos
            &&uiDiagnosticText.find("pointer_api=1 pointer_ok=1 pointer_error=0 pointer_error_applicable=0 requested_pointer=77 info_pointer=77 pointer_type=3")!=std::string::npos
            &&uiDiagnosticText.find("frame=88 system_time=54321 pixel=100,200 raw_pixel=101,202")!=std::string::npos
            &&uiDiagnosticText.find("source_device=")!=std::string::npos&&uiDiagnosticText.find("hwnd_target=")!=std::string::npos
            &&uiDiagnosticText.find("truncated=1")==std::string::npos,
            "Every native Pointer Down reads window and complete GetPointerInfo source even off owner thread with bounded untruncated output");
        for(const auto type:{PT_TOUCH,PT_MOUSE}){
            fixture.info.pointerType=type;uiDiagnosticText.clear();
            Diag::PointerDownSnapshot("page_middle_left",window,WM_POINTERDOWN,MAKEWPARAM(77,0x1234),"wndproc");
            check(fixture.requestedPointer==77&&GetLastError()==1234
                &&uiDiagnosticText.find(type==PT_TOUCH?"source=touch phase=down":"source=mouse phase=down")!=std::string::npos,
                "Pointer source comes from queried Pointer type and raw message id, independently of compatibility signature");
        }
        uiDiagnosticText.clear();fixture.pointerOk=FALSE;fixture.pointerError=ERROR_NOT_FOUND;
        Diag::PointerDownSnapshot("bar",window,WM_POINTERDOWN,77,"wndproc");
        check(GetLastError()==1234&&uiDiagnosticText.find("stage=pointer_info reason=query_failed")!=std::string::npos
            &&uiDiagnosticText.find("source=unknown phase=down")!=std::string::npos
            &&uiDiagnosticText.find("pointer_api=1 pointer_ok=0 pointer_error=1168 pointer_error_applicable=1")!=std::string::npos
            &&uiDiagnosticText.find("info_pointer=0 pointer_type=0 pointer_flags=0x0")!=std::string::npos,
            "GetPointerInfo failure records its immediate GLE and never guesses source from extra or partial output");
        uiDiagnosticText.clear();fixture.queries.pointerInfo=nullptr;const auto pointerCalls=fixture.pointerQueries;
        Diag::PointerDownSnapshot("page_bottom_right",window,WM_POINTERDOWN,77,"wndproc");
        check(GetLastError()==1234&&fixture.pointerQueries==pointerCalls
            &&uiDiagnosticText.find("stage=pointer_info reason=api_unavailable")!=std::string::npos
            &&uiDiagnosticText.find("pointer_api=0 pointer_ok=0 pointer_error=0 pointer_error_applicable=0")!=std::string::npos
            &&uiDiagnosticText.find("source=unknown phase=down")!=std::string::npos,
            "Missing optional Pointer API is separate from query failure and does not change caller state");
    }
    {
        // 阶段1探针：真实 Probe::Swallow 的开关、接触筛选、窗口/捕获证据与错误保护。
        namespace Diag=Inkeys::Input::UiPointer::Diagnostics;
        namespace HapticDiag=Inkeys::Drawing::Draw3::HapticDiagnostics;
        const bool previous=HapticDiag::Enabled();const DWORD previousError=GetLastError();const auto oldSink=Diag::testSink;
        struct RestoreProbe {bool enabled;DWORD error;decltype(Diag::testSink) sink;~RestoreProbe(){Diag::Probe::testEnvironment=-1;Diag::testSink=sink;HapticDiag::SetEnabled(enabled);SetLastError(error);}} restore{previous,previousError,oldSink};
        NativeDiagnosticFixture fixture;
        Diag::testSink=[](const char* line,std::size_t size){uiDiagnosticText.append(line,size);SetLastError(9876);};
        const HWND bar=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1234));
        const HWND page=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x2345));
        fixture.info.pointerId=77;fixture.info.pointerType=PT_PEN;fixture.info.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT;
        fixture.info.sourceDevice=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x4567));fixture.info.dwTime=900;fixture.info.ptPixelLocation={10,20};
        Diag::Probe::testEnvironment=0;HapticDiag::SetEnabled(true);uiDiagnosticText.clear();SetLastError(1234);
        check(!Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,77)&&fixture.Count()==0&&uiDiagnosticText.empty()&&GetLastError()==1234,
            "Pointer probe without the environment switch never queries, swallows or formats");
        Diag::Probe::testEnvironment=1;HapticDiag::SetEnabled(false);
        check(!Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,77)&&fixture.Count()==0&&uiDiagnosticText.empty()&&GetLastError()==1234,
            "Pointer probe requires diagnostics and stays inert when they are off");
        HapticDiag::SetEnabled(true);
        Diag::Probe::Announce("bar",bar);
        check(uiDiagnosticText.find("stage=probe_mode reason=enabled")!=std::string::npos&&GetLastError()==1234,"Pointer probe announces itself once per registered window");
        check(!Diag::Probe::Swallow("bar",bar,WM_LBUTTONDOWN,77)&&!Diag::Probe::Swallow("bar",bar,WM_POINTERENTER,77)&&!Diag::Probe::Swallow("bar",bar,WM_POINTERLEAVE,77)
            &&!Diag::Probe::Swallow("bar",bar,WM_POINTERCAPTURECHANGED,77),"Pointer probe only handles contact-stage Down, Update and Up messages");
        uiDiagnosticText.clear();fixture.info.pointerFlags=POINTER_FLAG_INRANGE;
        const unsigned hoverQueries=fixture.pointerQueries;
        check(!Diag::Probe::Swallow("bar",bar,WM_POINTERUPDATE,77)&&fixture.pointerQueries==hoverQueries+1&&uiDiagnosticText.empty()&&GetLastError()==1234,
            "Pointer probe leaves hover Updates to DefWindowProc");
        fixture.info.pointerType=PT_MOUSE;fixture.info.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT;
        check(!Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,77)&&uiDiagnosticText.empty(),"Pointer probe never swallows real mouse pointers");
        fixture.info.pointerType=PT_PEN;fixture.pointerOk=FALSE;
        check(!Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,77)&&uiDiagnosticText.empty()&&GetLastError()==1234,"Pointer probe passes through when GetPointerInfo fails");
        fixture.pointerOk=TRUE;const auto realQuery=fixture.queries.pointerInfo;fixture.queries.pointerInfo=nullptr;
        check(!Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,77)&&uiDiagnosticText.empty(),"Pointer probe passes through when the optional Pointer API is missing");
        fixture.queries.pointerInfo=realQuery;
        Diag::Probe::CountLegacy(WM_TOUCH,0);Diag::Probe::CountLegacy(WM_LBUTTONDOWN,0xff51570a);Diag::Probe::CountLegacy(WM_LBUTTONUP,0xff5157aa);Diag::Probe::CountLegacy(WM_LBUTTONDOWN,0);Diag::Probe::CountLegacy(WM_MOUSEMOVE,0xff51570a);
        uiDiagnosticText.clear();
        check(Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,77)&&GetLastError()==1234
            &&uiDiagnosticText.find("stage=probe_contact reason=swallow_down")!=std::string::npos&&uiDiagnosticText.find("source=pen phase=down contact=77")!=std::string::npos
            &&uiDiagnosticText.find("known_down=0 same_window=1")!=std::string::npos&&uiDiagnosticText.find("legacy_touch_seen=1 legacy_signed_mouse_seen=2")!=std::string::npos,
            "Pointer probe swallows a pen contact Down, preserves GLE and reports derived views counted so far");
        // Move 诊断按设备/接触限频 250ms；换一个设备句柄使该次 Update 不被限频吞掉，验证的是探针而非限频器。
        uiDiagnosticText.clear();fixture.info.pointerFlags=POINTER_FLAG_INCONTACT;fixture.info.sourceDevice=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x4568));
        check(Diag::Probe::Swallow("bar",bar,WM_POINTERUPDATE,77)&&uiDiagnosticText.find("reason=swallow_update")!=std::string::npos&&uiDiagnosticText.find("known_down=1 same_window=1")!=std::string::npos,
            "Pointer probe swallows contact Updates after a tracked Down");
        uiDiagnosticText.clear();fixture.info.pointerFlags=POINTER_FLAG_UP;fixture.info.sourceDevice=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x4567));
        check(Diag::Probe::Swallow("page_bottom_left",page,WM_POINTERUP,77)&&uiDiagnosticText.find("reason=swallow_up")!=std::string::npos
            &&uiDiagnosticText.find("known_down=1 same_window=0")!=std::string::npos&&uiDiagnosticText.find("down_window=0000000000001234")!=std::string::npos,
            "Pointer probe records an Up that reached a different window than its Down");
        uiDiagnosticText.clear();
        check(Diag::Probe::Swallow("bar",bar,WM_POINTERUP,77)&&uiDiagnosticText.find("known_down=0 same_window=1")!=std::string::npos,"Pointer probe retires a contact after its Up");
        uiDiagnosticText.clear();fixture.info.pointerType=PT_TOUCH;fixture.info.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT;
        check(Diag::Probe::Swallow("bar",bar,WM_POINTERDOWN,78)&&uiDiagnosticText.find("source=touch phase=down contact=78")!=std::string::npos,"Pointer probe also swallows finger contacts");
    }
    failures += RunTouchRoutingTests();
    Testing::ResetProvider(false);
    return failures;
}
