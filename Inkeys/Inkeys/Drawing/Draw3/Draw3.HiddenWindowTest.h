#pragma once

namespace Inkeys::Drawing::Draw3
{
	// 仅创建不可见 Window Service HWND，返回值可直接作为进程退出码。
	int RunHiddenWindowIntegrationTest() noexcept;
}
