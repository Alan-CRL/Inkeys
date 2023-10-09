/**
 * @file	HiFPS.h
 * @brief	HiEasyX 库的帧率模块
 * @author	huidong
*/

#pragma once

namespace HiEasyX
{
	/**
	 * @brief 根据目标帧率延时
	 * @param[in] fps			帧率
	 * @param[in] wait_long		是否长等待（降低占用）
	*/
	void DelayFPS(int fps, bool wait_long = false);
};
