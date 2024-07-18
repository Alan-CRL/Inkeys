/**
 * @file	SysRadioButton.h
 * @brief	HiSysGUI 控件分支：单选框
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 系统单选框控件
	*/
	class SysRadioButton : public SysControlBase
	{
	private:
		bool m_bChecked = false;
		void (*m_pFunc)(bool checked) = nullptr;

	protected:

		void RealCreate(HWND hParent) override;

	public:

		SysRadioButton();

#ifdef UNICODE
		SysRadioButton(HWND hParent, RECT rct, std::wstring strText = L"");
		SysRadioButton(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysRadioButton(HWND hParent, RECT rct, std::string strText = "");
		SysRadioButton(HWND hParent, int x, int y, int w, int h, std::string strText = "");
#endif

		LRESULT UpdateMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet) override;

		/**
		 * @brief 注册点击消息
		 * @param[in] pFunc 消息响应函数
		*/
		void RegisterMessage(void (*pFunc)(bool checked));

		/**
		 * @brief 获取选中状态
		*/
		bool IsChecked() const { return m_bChecked; }

		void Check(bool check);
	};
}
