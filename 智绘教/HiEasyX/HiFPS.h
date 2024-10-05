﻿/**
 * @file	HiFPS.h
 * @brief	HiEasyX 库的帧率模块
 * @author	huidong
*/

#pragma once

#include "HiFunc.h"

#include <ctime>
#include <thread>

namespace HiEasyX
{
	struct tDelayFPS
	{
		clock_t tRecord;
		tDelayFPS();  // 声明构造函数，定义将在 HiFPS.cpp 中进行
	};

	/**
	 * @brief 根据目标帧率延时
	 * @param[in] fps			帧率
	 * @param[in] wait_long		是否长等待（降低占用）
	*/

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
	void DelayFPS(tDelayFPS& Record, int fps, bool wait_long = false);
};
