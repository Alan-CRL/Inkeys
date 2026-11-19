#pragma once

#include <string>

namespace Inkeys::Drawing::Draw3
{
	// 回退原因从绘制线程按值发布，重建主窗口时一并交给下一代 Host。
	struct PresentationFallbackReport
	{
		std::string presentationWarning;
		std::string chainWarning;
		std::string lastAttemptFailure;
	};
}
