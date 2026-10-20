#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Inkeys::Input::UiPointer
{
    enum class Source { Mouse, Touch, Pen };
    enum class Kind { Down, Move, Up, Cancel };
    enum class CancelReason { None, Requested, Overflow, InputUnavailable, SinkDestroyed, Replaced };

    struct Sample
    {
        Kind kind = Kind::Move;
        Source source = Source::Mouse;
        POINT screen{};
        ULONGLONG tick = 0;
        DWORD messageTime = 0;
        std::uint64_t generation = 0;
        std::uintptr_t device = 0;
        DWORD contact = 0;
        CancelReason cancelReason = CancelReason::None;
    };

    // 有界队列不合并移动；溢出直接终止当前会话，避免丢失 Up 后卡住。
    class Mailbox
    {
    public:
        static constexpr std::size_t Capacity = 512;
        bool Push(const Sample& sample) noexcept;
        bool TryPop(Sample& sample) noexcept;
        void Reset() noexcept;
    private:
        std::mutex mutex_;
        std::array<Sample, Capacity> samples_{};
        std::size_t head_ = 0;
        std::size_t size_ = 0;
        bool cancelled_ = false;
    };

    enum class BeginResult { Started, Rejected, ReplacedMouse };

    // 仅由消费者上下文调用；不同来源、触点和旧代次不能结束当前会话。
    class Session
    {
    public:
        BeginResult Begin(const Sample& down) noexcept;
        bool Accept(const Sample& sample) noexcept;
        void Reset() noexcept;
        bool Active() const noexcept { return active_; }
        const Sample& Latest() const noexcept { return latest_; }
    private:
        Sample latest_{};
        bool active_ = false;
    };

    // 原生消息建立交互；Raw 只补充当前拖动，不能替普通点击提交。
    // Accepted Raw 样本统一保留原生身份，消费者只读取 Latest() 后执行业务。
    class MouseGesture
    {
    public:
        bool Begin(const Sample& nativeDown) noexcept;
        bool BeginDrag() noexcept;
        bool AcceptNative(const Sample& sample) noexcept;
        // 只接收已为本次原生 Down 保守认领的 MouseChannel 样本。
        bool AcceptRaw(const Sample& sample) noexcept;
        void Reset() noexcept;
        bool Active() const noexcept { return active_; }
        bool Dragging() const noexcept { return dragging_; }
        bool SourceReplaced() const noexcept;
        bool RawBound() const noexcept { return rawGeneration_ != 0; }
        bool RawDisabled() const noexcept { return rawDisabled_; }
        DWORD RawActivityTime() const noexcept { return rawActivityTime_; }
        const Sample& Latest() const noexcept { return latest_; }
    private:
        bool AcceptMovement(const Sample& sample) noexcept;
        void AcceptNormalized(const Sample& sample) noexcept;
        Sample latest_{};
        DWORD pressTime_ = 0;
        DWORD rawTimeFloor_ = 0;
        DWORD rawActivityTime_ = 0;
        std::uint64_t sourceRevision_ = 0;
        std::uint64_t rawGeneration_ = 0;
        std::uintptr_t rawDevice_ = 0;
        bool active_ = false;
        bool dragging_ = false;
        bool rawDisabled_ = false;
        bool hasMovement_ = false;
        bool hasRawTimeFloor_ = false;
    };

    // 可选补充通道保守认领 Raw 会话，重放已经到达的轨迹和 Up。
    // BeginMouse 失败或补充 Cancel 均不能否定已经成立的原生按下/点击。
    // SetWakeWindow 只投递通知；调用者必须在自己的上下文 TryPop，禁止生产线程执行业务。
    class MouseChannel
    {
    public:
        MouseChannel();
        ~MouseChannel();
        MouseChannel(const MouseChannel&) = delete;
        MouseChannel& operator=(const MouseChannel&) = delete;
        void SetWakeWindow(HWND window, UINT message) noexcept;
        bool BeginMouse(POINT screen, DWORD messageTime) noexcept;
        // 跨窗口直接指针租约暂停鼠标传输；触摸及 PPT 笔兼容路径共用，不执行业务。
        bool BeginTouch(std::uint64_t generation) noexcept;
        void EndTouch(std::uint64_t generation = 0) noexcept;
        bool TryPop(Sample& sample) noexcept;
        void FinishMouse(std::uint64_t generation = 0) noexcept;
        void Cancel(CancelReason reason = CancelReason::Requested) noexcept;
    private:
        struct State;
        std::shared_ptr<State> state_;
        friend struct Provider;
    };

    // 注册、注销和读取 HRAWINPUT 均在 sink 所属窗口线程调用；不使用 NOLEGACY。
    bool AttachRawMouse(HWND sink) noexcept;
    void DetachRawMouse(HWND sink) noexcept;
    bool RawMouseAvailable() noexcept;
    // 活动交互轮询；内部最多每 100ms 检查一次，不读取全局按键状态。
    bool CheckInputDesktop() noexcept;
    // 返回 true 表示有效的非触摸提升鼠标包；Bar 可据此保留边缘光效活动记录。
    bool HandleRawMouse(LPARAM rawInput, Sample* activity = nullptr) noexcept;
    bool IsPromotedMouseExtraInfo(ULONG_PTR extraInfo) noexcept;

    namespace Testing
    {
        // 无 HWND 的确定性生产入口测试；不要在产品活动时调用。
        void ResetProvider(bool available = true) noexcept;
        void PublishMouse(Sample sample) noexcept;
        void SetClock(ULONGLONG tick) noexcept;
        void PollPendingMouse() noexcept;
    }
}
