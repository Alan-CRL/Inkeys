#pragma once

#include <Windows.h>
#include <cstdint>

namespace Inkeys::Window::MessageDispatch
{
    inline constexpr UINT SettingCancelModalMessage = WM_APP + 0x532;

    [[nodiscard]] constexpr bool MatchesSettingModalWake(bool currentSetting, bool ownerThread,
        std::uintptr_t postedGeneration, std::uintptr_t windowGeneration) noexcept
    {
        return currentSetting && ownerThread && postedGeneration != 0
            && postedGeneration == windowGeneration;
    }

    struct Sample
    {
        POINT screen{};
        DWORD time = 0;
    };

    namespace Detail
    {
        inline thread_local const MSG* current = nullptr;
    }

    // 保存队列原始采样；嵌套分发退出后恢复外层，发送消息不得借用别的输入位置。
    class Scope
    {
    public:
        explicit Scope(const MSG& message) noexcept : previous_(Detail::current)
        {
            Detail::current = &message;
        }
        ~Scope() { Detail::current = previous_; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        const MSG* previous_;
    };

    inline bool TryGet(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        Sample& sample) noexcept
    {
        const MSG* current = Detail::current;
        if (!current || current->hwnd != window || current->message != message
            || current->wParam != wParam || current->lParam != lParam) return false;
        sample = { current->pt, current->time };
        return true;
    }

    // 原生消息时间为 32 位；转换到同一 uptime，不能用出队时间替换历史速度样本。
    inline ULONGLONG PostedTick(DWORD time, ULONGLONG now = GetTickCount64()) noexcept
    {
        const DWORD age = static_cast<DWORD>(now) - time;
        return age < 0x80000000u && now >= age ? now - age : now;
    }
}
