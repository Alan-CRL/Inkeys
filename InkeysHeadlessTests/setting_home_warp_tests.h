#pragma once

struct ImFont;

// 必须在当前真实字体上下文的首次 CPU atlas 模拟之前调用，不创建 HWND。
int RunSettingHomeWarpProbe(ImFont* regular, ImFont* strong, ImFont* icons);

#include <filesystem>
#include <functional>

// 独立 ImGui 上下文、首次 CPU atlas 模拟前使用；真实 DX11/WARP，无 HWND。
int SaveSettingFontComparison(const std::filesystem::path& path, float scale,
    const std::function<void()>& draw, int warmFrames = 3,
    const std::function<void()>& afterSubmit = {},
    const std::function<bool()>& stopAfterSubmit = {});
