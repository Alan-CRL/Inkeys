/**
 * @file	SysGroupBox.h
 * @brief	HiSysGUI 控件分支：分组框
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief <pre>
	 *		系统分组框控件
	 * 
	 *	注意：
	 *		创建控件时不要将此控件作为父控件，否则无法创建控件
	 * </pre>
	 * 
	 * @bug
	 *		分组框背景绘制有 Bug，请斟酌使用
	*/
	class SysGroupBox : public SysControlBase
	{
	protected:

		void RealCreate(HWND hParent) override;

	public:

		SysGroupBox();

#ifdef UNICODE
		SysGroupBox(HWND hParent, RECT rct, std::wstring strText = L"");
		SysGroupBox(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysGroupBox(HWND hParent, RECT rct, std::string strText = "");
		SysGroupBox(HWND hParent, int x, int y, int w, int h, std::string strText = "");
#endif
	};
}
