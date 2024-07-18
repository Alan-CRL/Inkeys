#include "SysRadioButton.h"
#include <tchar.h>

namespace HiEasyX
{
	void SysRadioButton::RealCreate(HWND hParent)
	{
		m_type = SCT_RadioButton;
		m_hWnd = CreateControl(
			hParent,
			_T("Button"),
			_T(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON
		);
	}

	SysRadioButton::SysRadioButton()
	{
	}

#ifdef UNICODE
	SysRadioButton::SysRadioButton(HWND hParent, RECT rct, std::wstring strText)
	{
		Create(hParent, rct, strText);
	}
	SysRadioButton::SysRadioButton(HWND hParent, int x, int y, int w, int h, std::wstring strText)
	{
		Create(hParent, x, y, w, h, strText);
	}
#else
	SysRadioButton::SysRadioButton(HWND hParent, RECT rct, std::string strText)
	{
		Create(hParent, rct, strText);
	}
	SysRadioButton::SysRadioButton(HWND hParent, int x, int y, int w, int h, std::string strText)
	{
		Create(hParent, x, y, w, h, strText);
	}
#endif

	LRESULT SysRadioButton::UpdateMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet)
	{
		if (msg == WM_COMMAND)
		{
			// 只要是按键按下消息就判断
			// 不能只判断自己的消息，因为同组的其它单选框被选择时，自己收不到消息
			if (HIWORD(wParam) == BN_CLICKED)
			{
				bool checked = Button_GetCheck(GetHandle());
				if (m_pFunc && m_bChecked != checked)
					m_pFunc(checked);
				m_bChecked = checked;
			}
		}

		bRet = false;
		return 0;
	}

	void SysRadioButton::RegisterMessage(void(*pFunc)(bool checked))
	{
		m_pFunc = pFunc;
	}

	void SysRadioButton::Check(bool check)
	{
		Button_SetCheck(GetHandle(), check);
		m_bChecked = check;
	}
}
