/**
 * @file	SysButton.h
 * @brief	HiSysGUI 控件分支：按钮
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

#include <graphics.h>

namespace HiEasyX
{
	/**
	 * @brief 系统按钮控件
	*/
	class SysButton : public SysControlBase
	{
	private:
		int m_nClickCount = 0;
		void (*m_pFunc)() = nullptr;

	protected:

		void RealCreate(HWND hParent) override;

	public:

		SysButton();

#ifdef UNICODE
		SysButton(HWND hParent, RECT rct, std::wstring strText = L"");
		SysButton(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysButton(HWND hParent, RECT rct, std::string strText = "");
		SysButton(HWND hParent, int x, int y, int w, int h, std::string strText = "");
#endif

		LRESULT UpdateMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet) override;

		/**
		 * @brief 注册点击消息
		 * @param[in] pFunc 消息处理函数
		*/
		void RegisterMessage(void (*pFunc)());

		/**
		 * @brief 设置图片
		 * @param[in] enable		是否启用按钮图片
		 * @param[in] img			图片
		 * @param[in] reserve_text	是否保留按钮中的文字
		*/
		void Image(bool enable, IMAGE* img = nullptr, bool reserve_text = false);

		/**
		 * @brief 获取点击次数
		*/
		int GetClickCount();

		/**
		 * @brief <pre>
		 *		判断是否点击按键
		 * 
		 *	备注：
		 *		建议使用 GetClickCount，使用此函数可能丢失点击次数信息
		 * </pre>
		*/
		bool IsClicked();
	};
}
