#include "../Inkeys/Inkeys/Input/UiPointerInput.h"
#include "../Inkeys/Inkeys/Window/Window.MessageDispatch.h"

#include <iostream>

import Inkeys.Message;

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
        // Bar metadata copies this value policy across the HiMsg queue, without owning Raw or business.
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
    Testing::ResetProvider(false);
    return failures;
}
