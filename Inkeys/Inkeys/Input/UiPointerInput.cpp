#include "UiPointerInput.h"
#include "UiPointerDiagnostics.h"
#include "UiTouchRouting.h"
#include "../Window/Window.MessageDispatch.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <vector>

namespace Inkeys::Input::UiPointer
{
    namespace
    {
        // 普通点击不注册 Raw 通道，仍须观察跨窗口 Touch/Pen 对鼠标的接管。
        std::atomic<std::uint64_t> nonMouseRevision{ 0 };
        std::atomic<bool> nonMouseActive{ false };
    }

    bool Mailbox::Push(const Sample& sample) noexcept
    {
        std::lock_guard lock(mutex_);
        if (cancelled_) return false;
        if (size_ == Capacity)
        {
            Sample cancel = sample;
            cancel.kind = Kind::Cancel;
            cancel.cancelReason = CancelReason::Overflow;
            head_ = 0;
            size_ = 1;
            samples_[0] = cancel;
            cancelled_ = true;
            return false;
        }
        samples_[(head_ + size_) % Capacity] = sample;
        ++size_;
        return true;
    }

    bool Mailbox::TryPop(Sample& sample) noexcept
    {
        std::lock_guard lock(mutex_);
        if (!size_) return false;
        sample = samples_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    void Mailbox::Reset() noexcept
    {
        std::lock_guard lock(mutex_);
        head_ = size_ = 0;
        cancelled_ = false;
    }

    BeginResult Session::Begin(const Sample& down) noexcept
    {
        if (down.kind != Kind::Down || (!down.generation && down.source != Source::Mouse))
            { Diagnostics::Log("session",nullptr,"session_begin","invalid_down_or_generation",0,&down,"session=%p",this); return BeginResult::Rejected; }
        BeginResult result = BeginResult::Started;
        if (active_)
        {
            if (latest_.source != Source::Mouse || down.source != Source::Touch)
            { Diagnostics::Log("session",nullptr,"session_begin","existing_nonreplaceable_session",0,&down,"session=%p active_source=%s active_generation=%llu active_contact=%lu",this,Diagnostics::SourceName(latest_.source),static_cast<unsigned long long>(latest_.generation),latest_.contact); return BeginResult::Rejected; }
            result = BeginResult::ReplacedMouse;
        }
        latest_ = down;
        active_ = true;
        return result;
    }

    bool Session::Accept(const Sample& sample) noexcept
    {
        if (!active_ || sample.kind == Kind::Down || sample.source != latest_.source
            || sample.generation != latest_.generation || sample.device != latest_.device
            || sample.contact != latest_.contact)
        { Diagnostics::Log("session",nullptr,"session_accept","identity_mismatch_or_inactive",0,&sample,"session=%p active=%d active_source=%s active_generation=%llu active_device=0x%llx active_contact=%lu",this,active_,Diagnostics::SourceName(latest_.source),static_cast<unsigned long long>(latest_.generation),static_cast<unsigned long long>(latest_.device),latest_.contact); return false; }
        latest_ = sample;
        if (sample.kind == Kind::Up || sample.kind == Kind::Cancel) active_ = false;
        return true;
    }

    void Session::Reset() noexcept
    {
        active_ = false;
        latest_ = {};
    }

    bool MouseGesture::Begin(const Sample& nativeDown) noexcept
    {
        if (active_ || nativeDown.kind != Kind::Down || nativeDown.source != Source::Mouse
            || !nativeDown.generation) return false;
        const auto revision = nonMouseRevision.load();
        if (nonMouseActive.load()) return false;
        Reset();
        latest_ = nativeDown;
        pressTime_ = nativeDown.messageTime;
        sourceRevision_ = revision;
        active_ = true;
        if (SourceReplaced())
        {
            Reset();
            return false;
        }
        return true;
    }

    bool MouseGesture::SourceReplaced() const noexcept
    {
        return active_ && (sourceRevision_ != nonMouseRevision.load() || nonMouseActive.load());
    }

    bool MouseGesture::BeginDrag() noexcept
    {
        if (!active_ || SourceReplaced()) return false;
        dragging_ = true;
        return true;
    }

    void MouseGesture::AcceptNormalized(const Sample& sample) noexcept
    {
        // Raw 设备代次是补充身份；业务会话始终沿用原生 Down 的身份。
        const auto generation = latest_.generation;
        const auto device = latest_.device;
        const auto contact = latest_.contact;
        Sample normalized = sample;
        // 终态仍须结束会话，但不能用迟到的松开位置/时间倒退已接受的拖动轨迹。
        if (dragging_ && sample.kind == Kind::Up
            && static_cast<LONG>(sample.messageTime - latest_.messageTime) < 0)
        {
            normalized.screen = latest_.screen;
            normalized.messageTime = latest_.messageTime;
            normalized.tick = latest_.tick;
        }
        latest_ = normalized;
        latest_.generation = generation;
        latest_.device = device;
        latest_.contact = contact;
        if (sample.kind == Kind::Up || sample.kind == Kind::Cancel) active_ = false;
    }

    bool MouseGesture::AcceptMovement(const Sample& sample) noexcept
    {
        if (static_cast<LONG>(sample.messageTime - latest_.messageTime) < 0) return false;
        if (sample.screen.x == latest_.screen.x && sample.screen.y == latest_.screen.y)
            return false;
        hasMovement_ = true;
        AcceptNormalized(sample);
        return true;
    }

    bool MouseGesture::AcceptNative(const Sample& sample) noexcept
    {
        if (!active_ || sample.source != Source::Mouse || sample.kind == Kind::Down
            || sample.generation != latest_.generation || sample.device != latest_.device
            || sample.contact != latest_.contact) return false;
        if (sample.kind == Kind::Cancel)
        {
            AcceptNormalized(sample);
            return true;
        }
        // 接管后不把原始 Up 伪装成已接受事件；客户端轮询将明确取消并清理反馈。
        if (SourceReplaced()) return false;
        if (static_cast<LONG>(sample.messageTime - pressTime_) < 0) return false;
        if (sample.kind == Kind::Move)
        {
            if (rawGeneration_ && !rawDisabled_)
            {
                // 仅用已到达事件的时间判断移动权威；Raw 静默不能形成永久原生门控。
                // 50ms 不是按住期限，不生成 Up/Cancel，也不停止 Raw 松开监听。
                if (static_cast<LONG>(sample.messageTime - rawActivityTime_) < 50
                    || static_cast<LONG>(sample.messageTime - latest_.messageTime) < 0
                    || (hasRawTimeFloor_
                        && static_cast<LONG>(sample.messageTime - rawTimeFloor_) < 0))
                    return false;
                // 同坐标也推进回放下限，避免稍后迟到的 Raw 重放已经跨过的时间段。
                hasRawTimeFloor_ = true;
                rawTimeFloor_ = sample.messageTime;
            }
            return AcceptMovement(sample);
        }
        // 有效原生 Up 不需要 Raw 启动成功，也不因补充消息时间稍晚而丢失。
        AcceptNormalized(sample);
        return true;
    }

    bool MouseGesture::AcceptRaw(const Sample& sample) noexcept
    {
        if (!active_ || rawDisabled_ || SourceReplaced() || sample.source != Source::Mouse)
            return false;
        if (sample.kind == Kind::Cancel)
        {
            if (static_cast<LONG>(sample.messageTime - pressTime_) < 0
                || (sample.generation && rawGeneration_ && sample.generation != rawGeneration_))
                return false;
            rawDisabled_ = true;
            // generation=0 表示补充尚未绑定；启动超时不能取消仍由原生消息驱动的拖动。
            // 非零也覆盖队列溢出丢失 Down 的情况；实际拖动明确失去传输才取消。
            if (dragging_ && sample.generation)
            {
                AcceptNormalized(sample);
                return true;
            }
            return false;
        }
        if (!sample.generation || static_cast<LONG>(sample.messageTime - pressTime_) < 0)
            return false;
        if (sample.kind == Kind::Down)
        {
            if (!rawGeneration_)
            {
                rawGeneration_ = sample.generation;
                rawDevice_ = sample.device;
                rawActivityTime_ = sample.messageTime;
                // 迟绑定不能重放已由原生消费的时间段；同毫秒无法可靠区分重复与遗漏。
                hasRawTimeFloor_ = hasMovement_;
                rawTimeFloor_ = latest_.messageTime;
            }
            return false;
        }
        if (!rawGeneration_ || sample.generation != rawGeneration_
            || sample.device != rawDevice_) return false;
        if (sample.kind == Kind::Move)
        {
            if ((hasRawTimeFloor_
                    && static_cast<LONG>(sample.messageTime - rawTimeFloor_) <= 0)
                || static_cast<LONG>(sample.messageTime - latest_.messageTime) < 0
                || static_cast<LONG>(sample.messageTime - rawActivityTime_) < 0)
                return false;
            // 有效静止包也证明 Raw 仍在送达；旧包不能刷新活动时间或夺回权威。
            rawActivityTime_ = sample.messageTime;
            return AcceptMovement(sample);
        }
        if (!dragging_)
        {
            // Raw Up 不能把未过阈值的拖动候选变成一次成功点击。
            rawDisabled_ = true;
            return false;
        }
        AcceptNormalized(sample);
        return true;
    }

    void MouseGesture::Reset() noexcept
    {
        latest_ = {};
        pressTime_ = rawTimeFloor_ = rawActivityTime_ = 0;
        sourceRevision_ = 0;
        rawGeneration_ = 0;
        rawDevice_ = 0;
        active_ = dragging_ = rawDisabled_ = hasMovement_ = hasRawTimeFloor_ = false;
    }

    struct MouseChannel::State
    {
        Mailbox mailbox;
        HWND wakeWindow = nullptr;
        UINT wakeMessage = 0;
        bool wakePending = false;
        bool armed = false;
        bool receiving = false;
        DWORD pressTime = 0;
        ULONGLONG armedTick = 0;
        std::uint64_t generation = 0;
        std::uint64_t lastBegunGeneration = 0;
        Sample latest{};
    };

    struct Provider
    {
        static constexpr std::size_t HistoryCapacity = Mailbox::Capacity;
        std::mutex mutex;
        HWND sink = nullptr;
        bool available = false;
        bool held = false;
        bool historyWrapped = false;
        std::uint64_t nextGeneration = 0;
        std::uint64_t lastClaimedGeneration = 0;
        std::weak_ptr<MouseChannel::State> mouseOwner;
        std::weak_ptr<MouseChannel::State> touchOwner;
        std::uint64_t touchGeneration = 0;
        ULONGLONG desktopCheckTick = 0;
        bool desktopAvailable = true;
        bool testClockEnabled = false;
        ULONGLONG testClockTick = 0;
        Sample down{};
        Sample latest{};
        std::array<Sample, HistoryCapacity> history{};
        std::size_t historyHead = 0;
        std::size_t historySize = 0;
        std::array<std::weak_ptr<MouseChannel::State>, 16> channels{};

        static Provider& Instance() noexcept
        {
            static Provider provider;
            return provider;
        }

        static bool AtOrAfter(DWORD lhs, DWORD rhs) noexcept
        {
            return static_cast<LONG>(lhs - rhs) >= 0;
        }

        ULONGLONG NowTick() const noexcept
        {
            return testClockEnabled ? testClockTick : GetTickCount64();
        }

        static void Wake(MouseChannel::State& state) noexcept
        {
            if (!state.wakeWindow || !state.wakeMessage || state.wakePending) return;
            if (PostMessageW(state.wakeWindow, state.wakeMessage, 0, 0))
            {
                state.wakePending = true;
                return;
            }
            // 通知失败也不能继续积压；owner 的活动计时器负责取走这个取消。
            Sample cancel = state.latest;
            cancel.kind = Kind::Cancel;
            cancel.cancelReason = CancelReason::InputUnavailable;
            state.armed = state.receiving = false;
            state.mailbox.Reset();
            state.mailbox.Push(cancel);
        }

        static void CancelState(MouseChannel::State& state, CancelReason reason) noexcept
        {
            if (!state.armed && !state.receiving) return;
            Sample cancel = state.latest;
            cancel.kind = Kind::Cancel;
            cancel.generation = state.generation;
            cancel.cancelReason = reason;
            state.armed = state.receiving = false;
            // 取消是独立终态，即使原队列已满也必须可见。
            state.mailbox.Reset();
            state.mailbox.Push(cancel);
            Wake(state);
        }

        void CancelAll(CancelReason reason) noexcept
        {
            for (auto& weak : channels)
                if (auto state = weak.lock()) CancelState(*state, reason);
        }

        void ExpirePendingMouse(ULONGLONG now) noexcept
        {
            for (auto& weak : channels)
            {
                auto state = weak.lock();
                // 只限制原生 Down 等待 Raw Down 的启动阶段；静止按住不能推断为松开。
                if (state && state->armed && now - state->armedTick >= 1000)
                    CancelState(*state, CancelReason::InputUnavailable);
            }
        }

        static bool Deliver(MouseChannel::State& state, const Sample& sample) noexcept
        {
            state.latest = sample;
            if (!state.mailbox.Push(sample))
            {
                state.armed = state.receiving = false;
                Wake(state);
                return false;
            }
            if (sample.kind == Kind::Up || sample.kind == Kind::Cancel)
                state.receiving = false;
            Wake(state);
            return true;
        }

        void Append(const Sample& sample) noexcept
        {
            if (historySize == HistoryCapacity)
            {
                historyHead = (historyHead + 1) % HistoryCapacity;
                --historySize;
                historyWrapped = true;
            }
            history[(historyHead + historySize) % HistoryCapacity] = sample;
            ++historySize;

            for (auto& weak : channels)
            {
                auto state = weak.lock();
                if (!state) continue;
                if (state->armed && AtOrAfter(sample.messageTime, state->pressTime))
                {
                    if (sample.kind == Kind::Down)
                    {
                        if (sample.messageTime != state->pressTime)
                        {
                            CancelState(*state, CancelReason::InputUnavailable);
                            continue;
                        }
                        // 同一毫秒也可能先有窗外点击，只认领原生命中起点相同的 Down。
                        if (sample.screen.x != state->latest.screen.x
                            || sample.screen.y != state->latest.screen.y) continue;
                        auto owner = mouseOwner.lock();
                        if ((owner && owner != state) || sample.generation <= lastClaimedGeneration)
                        {
                            CancelState(*state, CancelReason::Replaced);
                            continue;
                        }
                        state->generation = sample.generation;
                        state->lastBegunGeneration = sample.generation;
                        lastClaimedGeneration = sample.generation;
                        mouseOwner = state;
                        state->armed = false;
                        state->receiving = true;
                    }
                    else if (sample.kind == Kind::Up || sample.kind == Kind::Cancel)
                    {
                        // 同毫秒的窗外 Up 不属于待绑定点击；等匹配 Down 或启动期限。
                        if (sample.messageTime != state->pressTime)
                            CancelState(*state, CancelReason::InputUnavailable);
                        continue;
                    }
                }
                if (state->receiving && state->generation == sample.generation)
                    Deliver(*state, sample);
            }
        }

        void Publish(Sample sample) noexcept
        {
            if (!available || !touchOwner.expired()) return;
            sample.source = Source::Mouse;
            sample.contact = 0;
            if (sample.kind == Kind::Down)
            {
                if (held)
                {
                    Sample cancel = latest;
                    cancel.kind = Kind::Cancel;
                    cancel.cancelReason = CancelReason::Replaced;
                    Append(cancel);
                }
                sample.generation = ++nextGeneration;
                if (!sample.generation) sample.generation = ++nextGeneration;
                down = sample;
                held = true;
            }
            else
            {
                if (!held) return;
                sample.generation = down.generation;
                // 一个鼠标会话保留按下时的设备归属，多设备移动不会改变身份。
                sample.device = down.device;
            }
            latest = sample;
            Append(sample);
            if (sample.kind == Kind::Up || sample.kind == Kind::Cancel) held = false;
        }

        bool Begin(const std::shared_ptr<MouseChannel::State>& channel,
            POINT screen, DWORD messageTime) noexcept
        {
            auto& state = *channel;
            if (!available || !touchOwner.expired()) return false;
            if (auto owner = mouseOwner.lock(); owner && owner != channel
                && (owner->armed || owner->receiving)) return false;
            state.mailbox.Reset();
            state.wakePending = false;
            state.armed = state.receiving = false;
            state.generation = 0;
            state.pressTime = messageTime;
            state.armedTick = NowTick();
            state.latest = {};
            state.latest.screen = screen;
            state.latest.messageTime = messageTime;

            std::size_t begin = historySize;
            for (std::size_t index = 0; index < historySize; ++index)
            {
                const auto& sample = history[(historyHead + index) % HistoryCapacity];
                if (sample.kind == Kind::Down && sample.messageTime == messageTime
                    && sample.screen.x == screen.x && sample.screen.y == screen.y
                    && sample.generation > state.lastBegunGeneration
                    && sample.generation > lastClaimedGeneration)
                {
                    begin = index;
                    break;
                }
            }
            if (begin == historySize)
            {
                state.armed = true;
                mouseOwner = channel;
                if (historyWrapped && historySize
                    && AtOrAfter(history[historyHead].messageTime, messageTime))
                    CancelState(state, CancelReason::Overflow);
                return true;
            }

            const auto& first = history[(historyHead + begin) % HistoryCapacity];
            state.generation = state.lastBegunGeneration = first.generation;
            lastClaimedGeneration = first.generation;
            mouseOwner = channel;
            state.receiving = true;
            for (std::size_t index = begin; index < historySize; ++index)
            {
                const auto& sample = history[(historyHead + index) % HistoryCapacity];
                if (sample.generation != state.generation) break;
                if (!Deliver(state, sample) || !state.receiving) break;
            }
            return true;
        }
    };

    MouseChannel::MouseChannel() : state_(std::make_shared<State>())
    {
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        for (auto& weak : provider.channels)
        {
            if (weak.expired())
            {
                weak = state_;
                return;
            }
        }
        // 产品当前只有 Bar 和四个翻页窗口；超出上限时拒绝而非无界扩容。
        state_.reset();
    }

    MouseChannel::~MouseChannel()
    {
        Diagnostics::LeaseEvent diagnostic("lease_destroy", DiagnosticId(), 0);
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (diagnostic.active) { diagnostic.owner=reinterpret_cast<std::uintptr_t>(provider.touchOwner.lock().get()); diagnostic.owned=provider.touchGeneration; diagnostic.reason="not_owner"; }
        if (provider.mouseOwner.lock() == state_) provider.mouseOwner.reset();
        if (provider.touchOwner.lock() == state_)
        {
            diagnostic.reason="released";
            provider.touchOwner.reset();
            provider.touchGeneration = 0;
            nonMouseActive.store(false);
        }
    }

    void MouseChannel::SetWakeWindow(HWND window, UINT message) noexcept
    {
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        state_->wakeWindow = window;
        state_->wakeMessage = message;
        state_->wakePending = false;
    }

    bool MouseChannel::BeginMouse(POINT screen, DWORD messageTime) noexcept
    {
        if (!state_) return false;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        return provider.Begin(state_, screen, messageTime);
    }

    bool MouseChannel::BeginTouch(std::uint64_t generation) noexcept
    {
        Diagnostics::LeaseEvent diagnostic("lease_begin", DiagnosticId(), generation);
        if (!state_ || !generation) { diagnostic.reason = state_ ? "zero_generation" : "no_state"; return false; }
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (diagnostic.active) { diagnostic.owner=reinterpret_cast<std::uintptr_t>(provider.touchOwner.lock().get()); diagnostic.owned=provider.touchGeneration; diagnostic.reason="not_owner"; }
        if (auto owner = provider.touchOwner.lock(); owner && owner != state_) { diagnostic.reason="other_channel_owns_lease"; return false; }
        provider.CancelAll(CancelReason::Replaced);
        provider.touchOwner = state_;
        provider.touchGeneration = generation;
        nonMouseActive.store(true);
        nonMouseRevision.fetch_add(1);
        provider.mouseOwner.reset();
        provider.lastClaimedGeneration = provider.nextGeneration;
        provider.held = false;
        provider.historyHead = provider.historySize = 0;
        provider.historyWrapped = false;
        diagnostic.reason="acquired";
        return true;
    }

    void MouseChannel::EndTouch(std::uint64_t generation) noexcept
    {
        Diagnostics::LeaseEvent diagnostic("lease_end", DiagnosticId(), generation);
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (diagnostic.active) { diagnostic.owner=reinterpret_cast<std::uintptr_t>(provider.touchOwner.lock().get()); diagnostic.owned=provider.touchGeneration; diagnostic.reason="not_owner"; }
        if (provider.touchOwner.lock() != state_
            || (generation && generation != provider.touchGeneration)) { diagnostic.reason=provider.touchOwner.lock()==state_ ? "stale_generation" : "not_owner"; return; }
        diagnostic.reason="released";
        provider.touchOwner.reset();
        provider.touchGeneration = 0;
        nonMouseActive.store(false);
    }

    bool MouseChannel::TryPop(Sample& sample) noexcept
    {
        if (!state_) return false;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (state_->mailbox.TryPop(sample)) return true;
        state_->wakePending = false;
        return false;
    }

    void MouseChannel::FinishMouse(std::uint64_t generation) noexcept
    {
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (generation && generation != state_->generation) return;
        state_->armed = state_->receiving = false;
        state_->mailbox.Reset();
        state_->wakePending = false;
        if (provider.mouseOwner.lock() == state_) provider.mouseOwner.reset();
    }

    void MouseChannel::Cancel(CancelReason reason) noexcept
    {
        Diagnostics::LeaseEvent diagnostic("lease_cancel_raw_only", DiagnosticId(), 0);
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if(diagnostic.active) { diagnostic.owner=reinterpret_cast<std::uintptr_t>(provider.touchOwner.lock().get()); diagnostic.owned=provider.touchGeneration; diagnostic.reason="raw_cancel_does_not_release_touch"; }
        Provider::CancelState(*state_, reason);
    }

    bool AttachRawMouse(HWND sink) noexcept
    {
        if (!sink || GetWindowThreadProcessId(sink, nullptr) != GetCurrentThreadId())
            return false;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (provider.sink && provider.sink != sink) return false;
        if (provider.available && provider.sink == sink) return true;
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x02;
        device.dwFlags = RIDEV_INPUTSINK;
        device.hwndTarget = sink;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
        {
            provider.CancelAll(CancelReason::InputUnavailable);
            return false;
        }
        provider.sink = sink;
        provider.available = true;
        return true;
    }

    void DetachRawMouse(HWND sink) noexcept
    {
        Diagnostics::LeaseEvent diagnostic("lease_detach_sink", reinterpret_cast<std::uintptr_t>(sink), 0);
        if (!sink || GetWindowThreadProcessId(sink, nullptr) != GetCurrentThreadId()) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if(diagnostic.active) { diagnostic.owner=reinterpret_cast<std::uintptr_t>(provider.touchOwner.lock().get());diagnostic.owned=provider.touchGeneration;diagnostic.reason="sink_mismatch"; }
        if (provider.sink != sink) return;
        provider.available = false;
        provider.CancelAll(CancelReason::SinkDestroyed);
        provider.sink = nullptr;
        provider.mouseOwner.reset();
        diagnostic.reason="released_all_sink_destroyed";
        provider.touchOwner.reset();
        provider.touchGeneration = 0;
        nonMouseActive.store(false);
        provider.lastClaimedGeneration = provider.nextGeneration;
        provider.held = false;
        provider.historyHead = provider.historySize = 0;
        provider.historyWrapped = false;
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x02;
        device.dwFlags = RIDEV_REMOVE;
        (void)RegisterRawInputDevices(&device, 1, sizeof(device));
    }

    bool RawMouseAvailable() noexcept
    {
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        return provider.available;
    }

    bool CheckInputDesktop() noexcept
    {
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        const ULONGLONG now = provider.NowTick();
        provider.ExpirePendingMouse(now);
        if (provider.desktopCheckTick && now - provider.desktopCheckTick < 100)
            return provider.desktopAvailable;
        provider.desktopCheckTick = now;
        HDESK inputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        bool available = false;
        if (inputDesktop)
        {
            wchar_t inputName[256]{};
            wchar_t threadName[256]{};
            DWORD needed = 0;
            const HDESK threadDesktop = GetThreadDesktop(GetCurrentThreadId());
            available = threadDesktop
                && GetUserObjectInformationW(inputDesktop, UOI_NAME,
                    inputName, sizeof(inputName), &needed)
                && GetUserObjectInformationW(threadDesktop, UOI_NAME,
                    threadName, sizeof(threadName), &needed)
                && std::wcscmp(inputName, threadName) == 0;
            CloseDesktop(inputDesktop);
        }
        provider.desktopAvailable = available;
        if (!available) provider.CancelAll(CancelReason::InputUnavailable);
        return available;
    }

    bool IsPromotedMouseExtraInfo(ULONG_PTR extraInfo) noexcept
    {
        return (extraInfo & 0xFFFFFF00u) == 0xFF515700u;
    }

    bool HandleRawMouse(LPARAM rawInput, Sample* activity) noexcept
    {
        RAWINPUT input{};
        UINT bytes = sizeof(input);
        const UINT received = GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInput),
            RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER));
        auto& provider = Provider::Instance();
        if (received == static_cast<UINT>(-1) || received < sizeof(RAWINPUTHEADER))
        {
            std::lock_guard lock(provider.mutex);
            provider.CancelAll(CancelReason::InputUnavailable);
            return false;
        }
        if (input.header.dwType != RIM_TYPEMOUSE
            || received < offsetof(RAWINPUT, data) + sizeof(RAWMOUSE)
            || IsPromotedMouseExtraInfo(input.data.mouse.ulExtraInformation)) return false;

        Sample sample;
        sample.device = reinterpret_cast<std::uintptr_t>(input.header.hDevice);
        std::lock_guard lock(provider.mutex);
        if (!provider.available) return false;
        Inkeys::Window::MessageDispatch::Sample posted;
        if (Inkeys::Window::MessageDispatch::TryGet(provider.sink, WM_INPUT,
            input.header.wParam, rawInput, posted))
        {
            // MSG.pt 是入队时的完整屏幕坐标，积压时不能把历史轨迹改成当前光标。
            sample.screen = posted.screen;
            sample.messageTime = posted.time;
        }
        else
        {
            if (!GetCursorPos(&sample.screen))
            {
                provider.CancelAll(CancelReason::InputUnavailable);
                return false;
            }
            sample.messageTime = static_cast<DWORD>(GetMessageTime());
        }
        sample.tick = Inkeys::Window::MessageDispatch::PostedTick(sample.messageTime);
        if (activity) *activity = sample;
        const USHORT flags = input.data.mouse.usButtonFlags;
        if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)
        {
            sample.kind = Kind::Down;
            provider.Publish(sample);
        }
        else if (input.data.mouse.lLastX || input.data.mouse.lLastY
            || (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
        {
            sample.kind = Kind::Move;
            provider.Publish(sample);
        }
        if (flags & RI_MOUSE_LEFT_BUTTON_UP)
        {
            sample.kind = Kind::Up;
            provider.Publish(sample);
        }
        return true;
    }

    namespace Testing
    {
        void ResetProvider(bool available) noexcept
        {
            auto& provider = Provider::Instance();
            std::lock_guard lock(provider.mutex);
            provider.CancelAll(CancelReason::Requested);
            provider.available = available;
            provider.sink = nullptr;
            provider.mouseOwner.reset();
            provider.touchOwner.reset();
            provider.touchGeneration = 0;
            nonMouseActive.store(false);
            nonMouseRevision.fetch_add(1);
            provider.lastClaimedGeneration = provider.nextGeneration;
            provider.held = false;
            provider.historyHead = provider.historySize = 0;
            provider.historyWrapped = false;
            provider.testClockEnabled = false;
            provider.desktopCheckTick = 0;
            provider.desktopAvailable = true;
        }

        void PublishMouse(Sample sample) noexcept
        {
            auto& provider = Provider::Instance();
            std::lock_guard lock(provider.mutex);
            provider.Publish(sample);
        }

        void SetClock(ULONGLONG tick) noexcept
        {
            auto& provider = Provider::Instance();
            std::lock_guard lock(provider.mutex);
            provider.testClockEnabled = true;
            provider.testClockTick = tick;
        }

        void PollPendingMouse() noexcept
        {
            auto& provider = Provider::Instance();
            std::lock_guard lock(provider.mutex);
            provider.ExpirePendingMouse(provider.NowTick());
        }
    }
}

namespace Inkeys::Input::UiPointer::Touch
{
    namespace
    {
        struct Target { HWND window=nullptr; Context* local=nullptr; Adapter adapter{}; void* user=nullptr; std::uint64_t registration=0; };
        std::array<Target,TargetCount> targets;
        std::uint64_t nextRegistration=0;
        struct Intent { Identity identity{}; };
        std::mutex cancelMutex;
        std::array<Intent,TargetCount> cancels;
        std::array<Identity,TargetCount> cancelRegistrations; // 仅登记身份镜像，不保存业务接触。
#ifdef DRAW3_CONTACT_TESTING
        Testing::Candidate testCandidate=nullptr;
        Testing::PointerQuery testPointerQuery=nullptr;
        bool testForceLegacy=false;
#endif
        Target* Find(HWND window) noexcept { for(auto& target:targets)if(target.window==window&&window)return &target;return nullptr; }
        Target* Owner(const TOUCHINPUT& input) noexcept
        { for(auto& target:targets)if(target.local&&target.local->Owns(reinterpret_cast<std::uintptr_t>(input.hSource),input.dwID))return &target;return nullptr; }
        Target* Active() noexcept { for(auto& target:targets)if(target.local&&target.local->active)return &target;return nullptr; }
        bool SameTarget(const Target& target) noexcept
        {const auto* current=Find(target.window);return current&&current->registration==target.registration&&current->local==target.local;}
        HWND CandidateAt(POINT point) noexcept
        {
            QueryScope scope;
#ifdef DRAW3_CONTACT_TESTING
            if(testCandidate)return testCandidate(point);
#endif
            return WindowFromPoint(point);
        }
        void LogPacket(const Target* target,const Packet& packet,const char* stage,const char* reason) noexcept
        {
            if(!Diagnostics::Enabled())return;
            Sample sample;sample.source=Source::Touch;sample.kind=packet.kind;sample.screen=packet.screen;
            sample.messageTime=packet.input.dwTime;sample.tick=packet.tick;
            sample.device=reinterpret_cast<std::uintptr_t>(packet.input.hSource);sample.contact=packet.input.dwID;
            if(target&&target->local)sample.generation=target->local->identity.generation;
            Diagnostics::Log("touch_route",target?target->window:packet.origin,stage,reason,WM_TOUCH,&sample,
                "origin=%p target=%p registration=%llu route_generation=%llu flags=0x%lx mask=0x%lx extra=0x%llx primary=%d message_time=%lu preparing=%d semantic=%llu",
                packet.origin,target?target->window:nullptr,static_cast<unsigned long long>(target?target->registration:0),
                static_cast<unsigned long long>(sample.generation),packet.input.dwFlags,packet.input.dwMask,
                static_cast<unsigned long long>(packet.input.dwExtraInfo),(packet.input.dwFlags&TOUCHEVENTF_PRIMARY)!=0,
                packet.messageTime,target&&target->local&&target->local->preparing,
                static_cast<unsigned long long>(target&&target->local?target->local->semantic:0));
        }
        void CancelTarget(Target target,Identity expected) noexcept
        {
            if(!target.local||expected.registration!=target.registration||!target.local->Current(expected))return;
            // 先退休这一窗的代次，再执行可能同步重入的客户端清理。
            target.local->retired=target.local->latest;target.local->hasRetired=true;
            target.local->active=false;target.local->preparing=false;++target.local->mutation;
            Diagnostics::Log("touch_route",target.window,"route_cancel","retired",WM_CANCELMODE,nullptr,"registration=%llu route_generation=%llu",static_cast<unsigned long long>(expected.registration),static_cast<unsigned long long>(expected.generation));
            if(target.adapter.cancel)target.adapter.cancel(target.user,expected);
        }
        void Continue(Target target,Packet packet) noexcept
        {
            if(!SameTarget(target)||!target.local->active)return;
            auto& local=*target.local;const auto identity=local.identity;
            if(static_cast<LONG>(packet.messageTime-local.downMessageTime)<0){LogPacket(&target,packet,"touch_select","older_than_down");return;}
            // 只比较原生 MSG 时间，不要求提供方 dwTime 递增；略早于 Move 的 Up 仍收尾。
            if(static_cast<LONG>(packet.messageTime-local.latest.messageTime)<0)
            {if(packet.kind!=Kind::Up){LogPacket(&target,packet,"touch_select","older_move");return;}packet.screen=local.latest.screen;}
            const auto& old=local.latest;
            if(old.kind==packet.kind&&old.input.dwTime==packet.input.dwTime&&old.input.dwFlags==packet.input.dwFlags&&old.screen.x==packet.screen.x&&old.screen.y==packet.screen.y){LogPacket(&target,packet,"touch_select","duplicate");return;}
            local.latest=packet;
            const bool accepted=target.adapter.deliver&&target.adapter.deliver(target.user,packet,identity);
            LogPacket(&target,packet,"touch_deliver",accepted?"accepted":"client_rejected");
            if(!SameTarget(target)||!local.Current(identity))return;
            if(!accepted)CancelTarget(target,identity);
            else if(packet.kind==Kind::Up){local.retired=packet;local.hasRetired=true;local.active=false;local.preparing=false;++local.mutation;}
        }
    }
    bool RegisterTarget(std::size_t slot,HWND window,Context& local,Adapter adapter,void* user) noexcept
    {
        if(slot>=targets.size()||!window||!adapter.probe||!adapter.deliver||!adapter.cancel)return false;
        if(targets[slot].window)RetireTarget(targets[slot].window);
        if(targets[slot].window)return false; // 取消回调已重入登记新窗，旧调用不能覆盖。
        const auto registration=++nextRegistration;
        local.window=window;local.registration=registration;local.active=false;local.preparing=false;++local.mutation;
        targets[slot]={window,&local,adapter,user,registration};
        {std::lock_guard lock(cancelMutex);cancelRegistrations[slot]={window,registration,0};cancels[slot]={};}
        return true;
    }
    void Cancel(Context& local,Identity expected) noexcept
    {if(auto* target=Find(expected.window);target&&target->local==&local)CancelTarget(*target,expected);}
    void CancelTargetWindow(HWND window) noexcept
    {if(auto* target=Find(window);target&&target->local)CancelTarget(*target,target->local->identity);}
    void CancelWindow(HWND window) noexcept
    {
        // 即使源窗尚无活动接触，也要退休WindowFromPoint/probe中的旧事务。
        if(auto* source=Find(window);source&&source->local)++source->local->sourceEpoch;
        const auto snapshot=targets;
        std::array<Identity,TargetCount> identities{};
        for(std::size_t i=0;i<snapshot.size();++i)if(snapshot[i].local)identities[i]=snapshot[i].local->identity;
        for(std::size_t i=0;i<snapshot.size();++i){const auto& target=snapshot[i];
            if(target.local&&(target.window==window||target.local->downOrigin==window))CancelTarget(target,identities[i]);}
    }
    void RetireTarget(HWND window) noexcept
    {
        // 先摘旧登记再回调，回调同步建立的同 HWND 新登记不能被外层退休掉。
        const auto snapshot=targets;
        std::array<Identity,TargetCount> identities{};
        for(std::size_t i=0;i<snapshot.size();++i)if(snapshot[i].local)identities[i]=snapshot[i].local->identity;
        {std::lock_guard lock(cancelMutex);for(std::size_t i=0;i<cancelRegistrations.size();++i)if(cancelRegistrations[i].window==window){cancelRegistrations[i]={};cancels[i]={};}}
        for(auto& target:targets)if(target.window==window){
            if(target.local&&target.local->registration==target.registration){target.local->registration=0;++target.local->mutation;}
            target={};
        }
        for(std::size_t i=0;i<snapshot.size();++i){const auto& target=snapshot[i];
            if(target.local&&(target.window==window||target.local->downOrigin==window))CancelTarget(target,identities[i]);}
    }
    void RequestCancel(Identity identity) noexcept
    {
        if(!identity)return;
        {
            std::lock_guard lock(cancelMutex);
            Intent* slot=nullptr;
            for(std::size_t i=0;i<cancelRegistrations.size();++i)if(cancelRegistrations[i].window==identity.window&&cancelRegistrations[i].registration==identity.registration){slot=&cancels[i];break;}
            if(!slot)return;
            const auto old=slot->identity;
            if(old.registration>identity.registration||(old.registration==identity.registration&&old.generation>identity.generation))return;
            slot->identity=identity;
        }
        // 唤醒只带信号，完整64位身份留在有界邮箱；Win32也不会截断代次。
        PostMessageW(identity.window,CancelWakeMessage,0,0);
    }
    void DrainCancels() noexcept
    {
        std::array<Intent,TargetCount> pending;
        {std::lock_guard lock(cancelMutex);pending=cancels;cancels={};}
        for(const auto& intent:pending)if(auto* target=Find(intent.identity.window);target&&target->registration==intent.identity.registration)CancelTarget(*target,intent.identity);
    }
    void DispatchDecoded(HWND origin,std::span<const TOUCHINPUT> inputs,DWORD messageTime,bool originIsTarget) noexcept
    {
        DrainCancels();
        const bool anyPrimary=std::any_of(inputs.begin(),inputs.end(),[](const auto& input){return (input.dwFlags&TOUCHEVENTF_PRIMARY)!=0;});
        bool fallbackUsed=false;
        // 同批primary先处理，迟到fallback Up不能完成新接触。
        for(int pass=0;pass!=2;++pass)for(const auto& input:inputs)
        {
            const bool primary=(input.dwFlags&TOUCHEVENTF_PRIMARY)!=0;if(primary!=(pass==0))continue;
            Packet packet;packet.kind=(input.dwFlags&TOUCHEVENTF_DOWN)?Kind::Down:(input.dwFlags&TOUCHEVENTF_UP)?Kind::Up:Kind::Move;packet.input=input;packet.screen={TOUCH_COORD_TO_PIXEL(input.x),TOUCH_COORD_TO_PIXEL(input.y)};packet.messageTime=messageTime;packet.tick=Window::MessageDispatch::PostedTick(messageTime);packet.origin=origin;
            if(input.dwFlags&TOUCHEVENTF_DOWN)
            {
                if(auto* owner=Owner(input)){LogPacket(owner,packet,"touch_select","duplicate_down_owner_exists");continue;}
                bool retired=false;for(const auto& previous:targets)if(previous.local&&previous.local->hasRetired){const auto& last=previous.local->retired;if(last.input.hSource==input.hSource&&last.input.dwID==input.dwID&&static_cast<LONG>(messageTime-last.messageTime)<0){retired=true;break;}}
                if(retired){LogPacket(nullptr,packet,"touch_select","retired_sample");continue;}
                if(!primary&&(anyPrimary||fallbackUsed)){LogPacket(nullptr,packet,"touch_select","fallback_not_selected");continue;}
                if(auto* active=Active();active&&(!primary||active->local->primary)){LogPacket(active,packet,"touch_select","another_contact_active");continue;}
                if(!primary)fallbackUsed=true;
                const auto* source=Find(origin);
                if(!source||!source->local){LogPacket(nullptr,packet,"touch_probe","origin_not_registered");continue;}
                const auto sourceTarget=*source;const auto sourceEpoch=source->local->sourceEpoch;
                const auto sourceCurrent=[&]{return SameTarget(sourceTarget)&&sourceTarget.local->sourceEpoch==sourceEpoch;};
                const auto registrySnapshot=targets;
                const HWND candidate=originIsTarget?origin:CandidateAt(packet.screen);
                if(!sourceCurrent()){LogPacket(nullptr,packet,"touch_probe","origin_changed_during_query");continue;}
                const Target* before=nullptr;for(const auto& registered:registrySnapshot)if(registered.window==candidate&&candidate){before=&registered;break;}
                if(!before||!SameTarget(*before)){LogPacket(nullptr,packet,"touch_probe",before?"registration_changed_during_query":"candidate_not_registered");continue;}
                const auto target=*before;const auto mutation=target.local->mutation;
                packet.kind=Kind::Down;
                const auto probe=target.adapter.probe(target.user,packet);
                if(!sourceCurrent()){LogPacket(&target,packet,"touch_probe","origin_changed_during_probe");continue;}
                if(!SameTarget(target)||target.local->mutation!=mutation||probe.hit!=Hit::Accept){LogPacket(&target,packet,"touch_probe",!SameTarget(target)?"registration_changed":target.local->mutation!=mutation?"local_generation_changed":probe.hit==Hit::Blocked?"blocked":"miss");continue;}
                if(auto* active=Active()){
                    if(!primary||active->local->primary)continue;
                    const bool sameLocal=active->local==target.local;
                    CancelTarget(*active,active->local->identity);
                    // 旧fallback清理也可能同步隐藏/注销原接收窗，不能继续旧Down事务。
                    if(!sourceCurrent()){LogPacket(&target,packet,"touch_probe","origin_changed_during_fallback_cancel");continue;}
                    if(Active()||!SameTarget(target)||target.local->mutation!=mutation+(sameLocal?1:0))continue;
                }
                auto& local=*target.local;
                local.identity={target.window,target.registration,++local.nextGeneration};local.downOrigin=origin;local.downMessageTime=messageTime;local.semantic=probe.semantic;local.latest=packet;local.active=true;local.preparing=true;local.primary=primary;++local.mutation;
                const auto identity=local.identity;
                Diagnostics::Log("touch_route",target.window,"route_down","selected",WM_TOUCH,nullptr,"origin=%p registration=%llu route_generation=%llu contact=%lu device=%p point=%ld,%ld semantic=%llu",origin,static_cast<unsigned long long>(identity.registration),static_cast<unsigned long long>(identity.generation),input.dwID,input.hSource,packet.screen.x,packet.screen.y,static_cast<unsigned long long>(probe.semantic));
                const bool accepted=target.adapter.deliver(target.user,packet,identity);
                LogPacket(&target,packet,"touch_deliver",accepted?"accepted":"client_rejected");
                if(SameTarget(target)&&local.Current(identity)){if(accepted)local.preparing=false;else CancelTarget(target,identity);}
            }
            auto* owned=Owner(input);if(!owned){LogPacket(nullptr,packet,"touch_select","no_down_owner");continue;}
            // 先前缺primary的fallback可在后续包升级，不能再被另一个primary抢走。
            if(primary)owned->local->primary=true;
            LogPacket(owned,packet,"touch_select","owned_contact");
            if(input.dwFlags&TOUCHEVENTF_UP){packet.kind=Kind::Up;Continue(*owned,packet);}
            else if((input.dwFlags&TOUCHEVENTF_MOVE)&&!(input.dwFlags&TOUCHEVENTF_DOWN)){packet.kind=Kind::Move;Continue(*owned,packet);}
        }
    }
    namespace
    {
        using PointerInfoFn=BOOL(WINAPI*)(UINT32,POINTER_INFO*);
        PointerInfoFn PointerInfoQuery() noexcept
        {
#ifdef DRAW3_CONTACT_TESTING
            if(testPointerQuery)return testPointerQuery;
#endif
            // 可选导出：Win7 没有，不建立硬导入。
            static const auto query=reinterpret_cast<PointerInfoFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetPointerInfo"));
            return query;
        }
        bool LegacyForced() noexcept
        {
#ifdef DRAW3_CONTACT_TESTING
            if(testPointerQuery)return testForceLegacy;
#endif
            static const bool forced=[]()noexcept{
                const DWORD error=GetLastError();wchar_t value[2]{};
                const bool result=GetEnvironmentVariableW(L"INKEYS_UI_TOUCH_LEGACY",value,2)==1&&value[0]==L'1';
                SetLastError(error);return result;}();
            return forced;
        }
    }
    bool PointerMode() noexcept { return PointerInfoQuery()!=nullptr&&!LegacyForced(); }
    bool HandlePointer(HWND origin,UINT message,WPARAM wParam) noexcept
    {
        if(message!=WM_POINTERDOWN&&message!=WM_POINTERUPDATE&&message!=WM_POINTERUP&&message!=WM_POINTERCAPTURECHANGED)return false;
        if(!PointerMode())return false;
        // 查询和日志不得污染调用方正在检查的错误状态。
        struct ErrorGuard{DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);}} preserve;
        const UINT32 id=GET_POINTERID_WPARAM(wParam);
        const auto cancelId=[id]()noexcept{
            const auto snapshot=targets;
            for(const auto& target:snapshot)
                if(target.local&&target.local->active&&target.local->latest.input.dwID==id)CancelTarget(target,target.local->identity);
        };
        // 系统手势或其他窗口接管捕获后不会再有该 id 的 Up：撤销其路由并释放业务状态；仍交给旁观者与 DefWindowProc。
        if(message==WM_POINTERCAPTURECHANGED)
        {
            Diagnostics::Log("pointer_route",origin,"pointer_capture_changed","cancel_contact",message,nullptr,"pointer=%u",id);
            cancelId();
            return false;
        }
        const auto query=PointerInfoQuery();
        POINTER_INFO info{};
        if(!query(id,&info))
        {
            // 抬起/移动的查询失败时无法确认接触状态：撤销按该 id 持有的路由，避免残留活动接触挡住后续 Down。
            if(message!=WM_POINTERDOWN)cancelId();
            return false;
        }
        if(info.pointerType!=PT_TOUCH&&info.pointerType!=PT_PEN)return false;
        // 悬停（未接触）保持原路径，由 DefWindowProc 处理。
        if(message==WM_POINTERUPDATE&&!(info.pointerFlags&POINTER_FLAG_INCONTACT))return false;
        // 以值型 TOUCHINPUT 承载 Pointer 接触：device=sourceDevice、id=pointerId、时间/坐标与触觉观察同源。
        TOUCHINPUT input{};
        input.hSource=info.sourceDevice;input.dwID=id;input.dwTime=info.dwTime;
        input.x=info.ptPixelLocationRaw.x*100;input.y=info.ptPixelLocationRaw.y*100;
        input.dwFlags=message==WM_POINTERDOWN?TOUCHEVENTF_DOWN:message==WM_POINTERUP?TOUCHEVENTF_UP:TOUCHEVENTF_MOVE;
        if(info.pointerFlags&POINTER_FLAG_PRIMARY)input.dwFlags|=TOUCHEVENTF_PRIMARY;
        if(info.pointerType==PT_PEN)input.dwFlags|=TOUCHEVENTF_PEN;
        Sample sample;sample.source=info.pointerType==PT_PEN?Source::Pen:Source::Touch;
        sample.kind=message==WM_POINTERDOWN?Kind::Down:message==WM_POINTERUP?Kind::Up:Kind::Move;
        sample.screen=info.ptPixelLocationRaw;sample.messageTime=info.dwTime;sample.device=reinterpret_cast<std::uintptr_t>(info.sourceDevice);sample.contact=id;
        if(info.pointerFlags&POINTER_FLAG_CANCELED){
            // 异常终止：撤销该接触，不生成成功 Up。
            if(auto* owner=Owner(input))CancelTarget(*owner,owner->local->identity);
            Diagnostics::Log("pointer_route",origin,"pointer_cancel","canceled_flag",message,&sample,"pointer_flags=0x%x",info.pointerFlags);
            return true;
        }
        if(message==WM_POINTERDOWN){
            // 同一 pointerId 在范围内持续；若上次 Up 丢失，先撤销旧路由（即使本次 Down 随后被拒绝）。
            if(auto* stale=Owner(input)){
                Diagnostics::Log("pointer_route",origin,"pointer_down","stale_owner_cancelled",message,&sample,"pointer_flags=0x%x",info.pointerFlags);
                CancelTarget(*stale,stale->local->identity);
            }
            constexpr UINT32 otherButtons=POINTER_FLAG_SECONDBUTTON|POINTER_FLAG_THIRDBUTTON|POINTER_FLAG_FOURTHBUTTON|POINTER_FLAG_FIFTHBUTTON;
            // 第二键/笔桶键等价于右键类输入：吞掉但不进业务。
            if(!(info.pointerFlags&POINTER_FLAG_FIRSTBUTTON)||(info.pointerFlags&otherButtons)){
                Diagnostics::Log("pointer_route",origin,"pointer_down","non_primary_button",message,&sample,"pointer_flags=0x%x",info.pointerFlags);
                return true;
            }
            // 手指每次接触的 pointerId 都不同：同一设备出现新的主接触，说明旧主接触已丢失终态（系统手势/捕获丢失），
            // 否则它会一直占用 Active() 并拒绝之后所有主 Down。正在准备（捕获重入）的接触不撤销。
            if(info.pointerFlags&POINTER_FLAG_PRIMARY){
                const auto snapshot=targets;
                for(const auto& target:snapshot)
                    if(target.local&&target.local->active&&!target.local->preparing&&target.local->primary
                        &&target.local->latest.input.hSource==input.hSource&&target.local->latest.input.dwID!=id){
                        Diagnostics::Log("pointer_route",origin,"pointer_down","lost_primary_cancelled",message,&sample,"pointer_flags=0x%x",info.pointerFlags);
                        CancelTarget(target,target.local->identity);
                    }
            }
        }
        DispatchDecoded(origin,std::span<const TOUCHINPUT>(&input,1),static_cast<DWORD>(GetMessageTime()),true);
        return true;
    }
    LRESULT HandleMessage(HWND origin,WPARAM wParam,LPARAM lParam) noexcept
    {
        struct Close { HTOUCHINPUT handle;~Close(){CloseTouchInputHandle(handle);} } close{reinterpret_cast<HTOUCHINPUT>(lParam)};
        const UINT count=LOWORD(wParam);
        // PointerMode 下 WM_TOUCH 只是 Pointer 的派生副本（含画布接触派发到 UI 窗的那部分），句柄关闭后丢弃。
        if(PointerMode()){
            Sample dropped;dropped.source=Source::Touch;dropped.kind=Kind::Move;
            Diagnostics::Log("touch_route",origin,"touch_dropped","pointer_mode_derived",WM_TOUCH,&dropped,"count=%u",count);
            return 0;
        }
        try {
            std::vector<TOUCHINPUT> inputs(count);
            if(!count||!GetTouchInputInfo(close.handle,count,inputs.data(),sizeof(TOUCHINPUT))){
                const auto error=GetLastError();Diagnostics::Log("touch_route",origin,"touch_read",count?"GetTouchInputInfo_failed":"empty_batch",WM_TOUCH,nullptr,"count=%u error=%lu",count,error);
                CancelWindow(origin);return 0;}
            Sample batch;batch.source=Source::Touch;batch.kind=Kind::Move;
            Diagnostics::Log("touch_route",origin,"touch_read","success",WM_TOUCH,&batch,"count=%u batch=%llu",count,static_cast<unsigned long long>(Diagnostics::rawSequence));
            DispatchDecoded(origin,inputs,static_cast<DWORD>(GetMessageTime()));
        } catch(...) {Diagnostics::Log("touch_route",origin,"touch_read","decode_exception",WM_TOUCH,nullptr,"count=%u",count);CancelWindow(origin);}
        return 0;
    }
#ifdef DRAW3_CONTACT_TESTING
    void Testing::SetCandidate(Candidate candidate) noexcept {testCandidate=candidate;}
    void Testing::SetPointerQuery(PointerQuery query) noexcept {testPointerQuery=query;}
    void Testing::ForceLegacy(bool legacy) noexcept {testForceLegacy=legacy;}
    void Testing::Reset() noexcept
    {for(auto target:targets)if(target.window)RetireTarget(target.window);{std::lock_guard lock(cancelMutex);cancels={};cancelRegistrations={};}testCandidate=nullptr;testPointerQuery=nullptr;testForceLegacy=false;}
#endif
}
