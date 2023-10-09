/**
 * @file	HiIcon.h
 * @brief	HiEasyX 库的图标模块
 * @note	存储 HiEasyX 图标
 * @author	huidong
*/

#pragma once

#include <graphics.h>

// 图标大小
#define HIICON_WIDTH	64
#define HIICON_HEIGHT	64

namespace HiEasyX
{
	/**
	 * @brief 获取图标显存
	 * @see 图标大小 HIICON_WIDTH, HIICON_HEIGHT
	 * @return 显存指针
	*/
	DWORD* GetIconImageBuffer();

	/**
	 * @brief 获取图标对象（显存的拷贝）
	*/
	IMAGE* GetIconImage();
};
