#pragma once

#include "UiPointerInput.h"
#include <array>
#include <span>

namespace Inkeys::Input::UiPointer::Touch
{
    inline constexpr std::size_t TargetCount = 5;
    inline constexpr UINT CancelWakeMessage = WM_APP + 0x35f;
    enum class Hit { Miss, Blocked, Accept };
    struct Probe { Hit hit = Hit::Miss; std::uint64_t semantic = 0; };
    struct Identity
    {
        HWND window = nullptr;
        std::uint64_t registration = 0, generation = 0;
        explicit operator bool() const noexcept { return window && registration && generation; }
        bool operator==(const Identity&) const = default;
    };
    struct Packet
    {
        TOUCHINPUT input{};
        POINT screen{};
        DWORD messageTime = 0;
        ULONGLONG tick = 0;
        HWND origin = nullptr;
        Kind kind = Kind::Move;
    };
    // 五个窗口各自拥有一份；跨窗登记层不再保存另一份活动手势。
    struct Context
    {
        HWND window = nullptr;
        std::uint64_t registration = 0, nextGeneration = 0;
        Identity identity{};
        Packet latest{};
        HWND downOrigin = nullptr;
        DWORD downMessageTime = 0;
        std::uint64_t semantic = 0;
        Packet retired{};
        bool hasRetired = false;
        bool active = false, preparing = false, primary = false;
        std::uint64_t mutation = 0;
        std::uint64_t sourceEpoch = 0; // 隐藏/取消接收源使在途查询失效，不阻止其后稳定的新Down转交。
        bool Owns(std::uintptr_t device, DWORD contact) const noexcept
        { return active && reinterpret_cast<std::uintptr_t>(latest.input.hSource) == device && latest.input.dwID == contact; }
        bool Current(Identity expected) const noexcept { return active && identity == expected; }
    };
    // 只回收本次准备新取得、且没有被重入新Touch/Mouse票据取代的原生捕获。
    inline bool ShouldRollbackPreparingCapture(const Context& local, Identity expected,
        HWND captureBefore, HWND captureAfter, std::uint64_t ticketBefore, std::uint64_t ticketAfter) noexcept
    {
        return !local.Current(expected) && local.identity == expected && local.registration == expected.registration
            && captureBefore != expected.window && captureAfter == expected.window && ticketBefore == ticketAfter;
    }
    struct Adapter
    {
        Probe (*probe)(void*, const Packet&) = nullptr;
        bool (*deliver)(void*, const Packet&, Identity) = nullptr;
        void (*cancel)(void*, Identity) = nullptr;
    };
    // scope仅改变本进程Touch查询的纯命中资格，不改变物理Mouse消息策略。
    inline thread_local unsigned queryDepth = 0;
    struct QueryScope { QueryScope() noexcept { ++queryDepth; } ~QueryScope() { --queryDepth; } };
    inline bool QueryActive() noexcept { return queryDepth != 0; }

    bool RegisterTarget(std::size_t slot, HWND, Context&, Adapter, void*) noexcept;
    void RetireTarget(HWND) noexcept;
    void CancelWindow(HWND) noexcept;
    void CancelTargetWindow(HWND) noexcept;
    void Cancel(Context&, Identity) noexcept;
    void RequestCancel(Identity) noexcept;
    void DrainCancels() noexcept;
    // originIsTarget：Pointer 接触的实际接收窗即业务目标，不再用 WindowFromPoint 选窗。
    void DispatchDecoded(HWND origin, std::span<const TOUCHINPUT>, DWORD messageTime, bool originIsTarget = false) noexcept;
    LRESULT HandleMessage(HWND origin, WPARAM, LPARAM) noexcept;
    // PointerMode：可选 GetPointerInfo 可用（Win8+），手指/笔接触只由 Pointer 进入业务；WM_TOUCH 与
    // 带 Pointer 签名的兼容 Mouse 都是派生副本，按类型丢弃。INKEYS_UI_TOUCH_LEGACY=1 可强制回到 WM_TOUCH 来源。
    bool PointerMode() noexcept;
    // 返回 true 表示该 Pointer 消息已作为接触处理（调用方直接 return 0，不交 DefWindowProc，
    // 系统也就不再派生 WM_TOUCH/兼容 Mouse/长按右键）；悬停、PT_MOUSE 与查询失败返回 false。
    bool HandlePointer(HWND origin, UINT message, WPARAM wParam) noexcept;
#ifdef DRAW3_CONTACT_TESTING
    namespace Testing
    {
        using Candidate = HWND(*)(POINT);
        void SetCandidate(Candidate) noexcept;
        using PointerQuery = BOOL(WINAPI*)(UINT32, POINTER_INFO*);
        void SetPointerQuery(PointerQuery) noexcept; // 非空即启用 PointerMode，并以替身代替 GetPointerInfo
        void ForceLegacy(bool) noexcept;             // 模拟 Win7/INKEYS_UI_TOUCH_LEGACY：PointerMode 关闭，WM_TOUCH 仍为来源
        void Reset() noexcept;
    }
#endif
}
