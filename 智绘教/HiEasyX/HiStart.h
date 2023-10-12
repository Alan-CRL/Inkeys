/**
 * @file	HiStart.h
 * @brief	HiEasyX 库的开始界面渲染模块 2.0
 * @note	开场动画改编自慢羊羊的《艺术字系列：冰封的 EasyX》
 * @author	慢羊羊 <yw80@qq.com>，1.0 由 huidong（改编），2.0 由 Alan-CRL（改编）
*/

#pragma once
#include <graphics.h>

namespace HiEasyX
{
	/**
	 * @brief 渲染开场动画
	*/
	void RenderStartScene(HWND hWnd, int w, int h, int nPreCmdShow);
}
