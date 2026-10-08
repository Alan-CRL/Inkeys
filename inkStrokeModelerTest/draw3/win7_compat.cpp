#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace
{
	using GetSystemTimePreciseAsFileTimeFunction = void(WINAPI*)(LPFILETIME);

	void WINAPI GetCompatibleSystemTimePreciseAsFileTime(LPFILETIME value) noexcept
	{
		if (!value) return;
		static const GetSystemTimePreciseAsFileTimeFunction preciseFunction = []() noexcept
		{
			const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
			return kernel32 ? reinterpret_cast<GetSystemTimePreciseAsFileTimeFunction>(
				GetProcAddress(kernel32, "GetSystemTimePreciseAsFileTime")) : nullptr;
		}();
		if (preciseFunction)
			preciseFunction(value);
		else
			GetSystemTimeAsFileTime(value); // 纯 Win7 使用原有系统时钟精度，避免启动期静态导入 Win8 API。
	}
}

extern "C" GetSystemTimePreciseAsFileTimeFunction
	__imp_GetSystemTimePreciseAsFileTime = GetCompatibleSystemTimePreciseAsFileTime;

#if defined(_M_IX86)
// x86 stdcall 的导入指针带参数字节后缀，映射到上面的兼容函数指针。
#pragma comment(linker, "/alternatename:__imp__GetSystemTimePreciseAsFileTime@4=___imp_GetSystemTimePreciseAsFileTime")
#endif
