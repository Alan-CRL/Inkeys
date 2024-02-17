#include "HiFPS.h"

namespace HiEasyX
{
	tDelayFPS::tDelayFPS() : tRecord(0) {}

	// DelayFPS 于 24/01/31 更新
	// 此次更新对这个函数进行改动，其目的是解决多线程使用时引发的问题，并提高精度
	// 同时将 HpSleep(delay); 改为 std::this_thread::sleep_for(std::chrono::milliseconds(delay)); 有助于进一步提高精度
	// this_thread::sleep_for 函数存在问题，据 https://developercommunity.visualstudio.com/t/Modifying-the-system-time-to-the-past-s/10476559 中，一对此函数进行修复，请确认 MSVC 版本以避免错误
	// 使用模板
	/*
		static hiex::tDelayFPS recond;
		hiex::DelayFPS(recond, 24);
	*/
	// 或是在循环上一级定义 hiex::tDelayFPS recond; 需要确保每个线程中有自己的 tDelayFPS，多个线程不能使用同一个 tDelayFPS
	void DelayFPS(tDelayFPS& Record, int fps, bool wait_long)
	{
		if (wait_long)
		{
			Sleep(500);
			return;
		}

		clock_t tNow = clock();
		if (Record.tRecord)
		{
			int delay = 1000 / fps - (tNow - Record.tRecord);
			if (delay > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				//HpSleep(delay);
			}
		}
		Record.tRecord = clock();
	}
};