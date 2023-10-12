/**
 * @file	SysStatic.h
 * @brief	HiSysGUI 控件分支：静态控件
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

#include <graphics.h>

namespace HiEasyX
{
	/**
	 * @brief 系统静态控件
	*/
	class SysStatic : public SysControlBase
	{
	protected:

		void RealCreate(HWND hParent) override;

	public:

		SysStatic();

#ifdef UNICODE
		SysStatic(HWND hParent, RECT rct, std::wstring strText = L"");
		SysStatic(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysStatic(HWND hParent, RECT rct, std::string strText = "");
		SysStatic(HWND hParent, int x, int y, int w, int h, std::string strText = "");
#endif

		/**
		 * @brief 设置文本居中
		 * @param[in] center 是否居中
		*/
		void Center(bool center);

		/**
		 * @brief 设置图片
		 * @param[in] enable	是否启用图像
		 * @param[in] img		图像
		*/
		void Image(bool enable, IMAGE* img = nullptr);
	};
}
