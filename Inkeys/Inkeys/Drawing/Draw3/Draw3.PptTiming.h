#pragma once

#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace Inkeys::Drawing::Draw3
{
	inline void TracePptTiming(const char* stage, std::uint64_t session,
		std::uint64_t target, std::int64_t observedQpc = 0) noexcept
	{
		static const bool enabled = []() noexcept
		{
			wchar_t value[8]{};
			return GetEnvironmentVariableW(L"INKEYS_PPT_TIMING", value, 8) > 0 &&
				value[0] == L'1';
		}();
		if (!enabled) return;
		LARGE_INTEGER now{}, frequency{};
		// 可延后关联目标编号，但时间仍取自 native 首次观测点。
		if (observedQpc == 0) QueryPerformanceCounter(&now);
		else now.QuadPart = observedQpc;
		QueryPerformanceFrequency(&frequency);
		char message[256]{};
		std::snprintf(message, sizeof(message),
			"[PptSync] stage=%s session=%llu target=%llu qpc=%lld frequency=%lld\n",
			stage, static_cast<unsigned long long>(session),
			static_cast<unsigned long long>(target),
			static_cast<long long>(now.QuadPart),
			static_cast<long long>(frequency.QuadPart));
		std::fputs(message, stderr);
		OutputDebugStringA(message);
	}
}
