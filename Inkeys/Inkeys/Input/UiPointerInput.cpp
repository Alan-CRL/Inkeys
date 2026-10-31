#include "UiPointerInput.h"
#include "../Window/Window.MessageDispatch.h"

#include <algorithm>
#include <atomic>
#include <cwchar>

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
            return BeginResult::Rejected;
        BeginResult result = BeginResult::Started;
        if (active_)
        {
            if (latest_.source != Source::Mouse || down.source != Source::Touch)
                return BeginResult::Rejected;
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
            return false;
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
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (provider.mouseOwner.lock() == state_) provider.mouseOwner.reset();
        if (provider.touchOwner.lock() == state_)
        {
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
        if (!state_ || !generation) return false;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (auto owner = provider.touchOwner.lock(); owner && owner != state_) return false;
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
        return true;
    }

    void MouseChannel::EndTouch(std::uint64_t generation) noexcept
    {
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (provider.touchOwner.lock() != state_
            || (generation && generation != provider.touchGeneration)) return;
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
        if (!state_) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
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
        if (!sink || GetWindowThreadProcessId(sink, nullptr) != GetCurrentThreadId()) return;
        auto& provider = Provider::Instance();
        std::lock_guard lock(provider.mutex);
        if (provider.sink != sink) return;
        provider.available = false;
        provider.CancelAll(CancelReason::SinkDestroyed);
        provider.sink = nullptr;
        provider.mouseOwner.reset();
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
