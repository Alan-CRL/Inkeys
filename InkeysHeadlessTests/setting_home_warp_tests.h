#pragma once

struct ImFont;

// 必须在当前真实字体上下文的首次 CPU atlas 模拟之前调用，不创建 HWND。
int RunSettingHomeWarpProbe(ImFont* regular, ImFont* strong, ImFont* icons);

#include <filesystem>
#include <functional>
#include <algorithm>
#include <array>
#include "../Inkeys/Inkeys/UI/Setting/Setting.Pages.h"

// 样例时刻以原 2.66s 开屏书写；按阶段比例映射到当前时序，断言仍落在同一阶段的同一相对位置。
inline float HomeEntryPhaseTime(float legacy)
{
    using Timing = Inkeys::UI::Setting::Design::HomeEntryTiming;
    constexpr std::array<float, 6> from{ 0.0F, .24F, 1.30F, 1.90F, 2.44F, 2.66F };
    const std::array<float, 6> to{ 0.0F, Timing::Wait, Timing::WritingEnd, Timing::DockStart, Timing::SettleStart, Timing::Total };
    for (std::size_t index = 1; index < from.size(); ++index)
        if (legacy <= from[index] || index + 1 == from.size())
            return to[index - 1] + (to[index] - to[index - 1]) * std::clamp((legacy - from[index - 1]) / (from[index] - from[index - 1]), 0.0F, 1.0F);
    return legacy;
}

// 独立 ImGui 上下文、首次 CPU atlas 模拟前使用；真实 DX11/WARP，无 HWND。
int SaveSettingFontComparison(const std::filesystem::path& path, float scale,
    const std::function<void()>& draw, int warmFrames = 3,
    const std::function<void()>& afterSubmit = {},
    const std::function<bool()>& stopAfterSubmit = {});
