#pragma once

namespace Inkeys::Drawing::Draw3
{
	// 在产品、配置和窗口初始化前处理独立离屏对照；普通启动直接返回 false。
	bool TryRunDraw3ResourceProbeEarly(int argc, wchar_t** argv, int& exitCode) noexcept;
}
