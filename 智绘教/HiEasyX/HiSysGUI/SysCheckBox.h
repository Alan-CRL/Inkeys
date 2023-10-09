/**
 * @file	SysCheckBox.h
 * @brief	HiSysGUI 控件分支：复选框
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 系统复选框控件
	*/
	class SysCheckBox : public SysControlBase
	{
	private:
		bool m_bChecked = false;
		void (*m_pFunc)(bool checked) = nullptr;

	protected:

		void RealCreate(HWND hParent) override;

	public:

		SysCheckBox();

#ifdef UNICODE
		SysCheckBox(HWND hParent, RECT rct, std::wstring strText = L"");
		SysCheckBox(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysCheckBox(HWND hParent, RECT rct, std::string strText = "");
		SysCheckBox(HWND hParent, int x, int y, int w, int h, std::string strText = "");
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
