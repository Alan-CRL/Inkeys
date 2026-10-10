#pragma once

#include <d3dcommon.h>
#include <string_view>

namespace Inkeys::Drawing::Draw3::shader_compatibility
{
	// 只推广已在 Win7 WARP 实测通过的完整程序，诊断精确 0/1 保留同设备原版对照。
	constexpr bool UseGuardedPixelShader(int majorVersion, int minorVersion,
		D3D_DRIVER_TYPE driver, bool diagnostics, std::wstring_view guardedOverride) noexcept
	{
		if (diagnostics)
		{
			if (guardedOverride == L"0") return false;
			if (guardedOverride == L"1") return true;
		}
		return majorVersion == 6 && minorVersion == 1 && driver == D3D_DRIVER_TYPE_WARP;
	}
}
