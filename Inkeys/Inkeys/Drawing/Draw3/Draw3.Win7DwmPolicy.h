#pragma once

#include <windows.h>

namespace Inkeys::Drawing::Draw3
{
	// 先确认平台，再查询合成与着色；上一步失败时不再调用下一项 API。
	template<class QueryComposition, class QueryColor>
	HRESULT CheckWin7DwmEnvironment(int major, int minor, int build, const char*& stage,
		QueryComposition queryComposition, QueryColor queryColor) noexcept
	{
		stage = "Win7 SP1";
		if (major != 6 || minor != 1 || build < 7601) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
		stage = "DwmIsCompositionEnabled";
		BOOL enabled = FALSE;
		HRESULT result = queryComposition(&enabled);
		if (FAILED(result)) return result;
		if (!enabled) return DWM_E_COMPOSITIONDISABLED;
		stage = "DwmGetColorizationColor";
		DWORD color = 0;
		BOOL opaque = TRUE;
		result = queryColor(&color, &opaque);
		if (FAILED(result)) return result;
		return opaque ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) : S_OK;
	}

	template<class CreateChain, class DeviceLost, class RecordFallback>
	HRESULT CreatePreferredSwapChain(bool sequential, bool fallbackSequential, CreateChain create, DeviceLost deviceLost, RecordFallback record)
	{
		const HRESULT first = create(sequential);
		// 已选兼容链、创建成功或设备本身失效时，再试一遍没有意义。
		if (sequential == fallbackSequential || SUCCEEDED(first) || deviceLost(first)) return first;
		record(first);
		return create(fallbackSequential);
	}
}
