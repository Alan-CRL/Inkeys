#pragma once

struct ImFont;

// 必须在当前真实字体上下文的首次 CPU atlas 模拟之前调用，不创建 HWND。
int RunSettingHomeWarpProbe(ImFont* regular, ImFont* strong, ImFont* icons);
