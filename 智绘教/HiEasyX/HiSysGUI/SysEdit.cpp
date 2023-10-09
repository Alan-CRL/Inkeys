#include "SysEdit.h"
#include <tchar.h>

namespace HiEasyX
{
	void SysEdit::RealCreate(HWND hParent)
	{
		m_type = SCT_Edit;
		m_hWnd = CreateControl(
			hParent,
			_T("Edit"),
			_T(""),
			m_lBasicStyle
		);

		ApplyProperty();

		// ´´½¨»­Ë¢
		SetBkColor(m_cBk);
	}

	SysEdit::SysEdit()
	{
	}

#ifdef UNICODE
	SysEdit::SysEdit(HWND hParent, RECT rct, std::wstring strText)
	{
		Create(hParent, rct, strText);
	}
	SysEdit::SysEdit(HWND hParent, int x, int y, int w, int h, std::wstring strText)
	{
		Create(hParent, x, y, w, h, strText);
	}
#else
	SysEdit::SysEdit(HWND hParent, RECT rct, std::string strText)
	{
		Create(hParent, rct, strText);
	}
	SysEdit::SysEdit(HWND hParent, int x, int y, int w, int h, std::string strText)
	{
		Create(hParent, x, y, w, h, strText);
	}
#endif

	SysEdit::~SysEdit()
	{
		if (hBk)
			DeleteBrush(hBk);
	}

	void SysEdit::PreSetStyle(PreStyle pre_style)
	{
		m_lBasicStyle |= (
			(pre_style.multiline ? ES_MULTILINE : 0)
			| (pre_style.center_text ? ES_CENTER : 0)
			| (pre_style.vscroll ? WS_VSCROLL : 0)
			| (pre_style.auto_vscroll ? ES_AUTOVSCROLL : 0)
			| (pre_style.hscroll ? WS_HSCROLL : 0)
			| (pre_style.auto_hscroll ? ES_AUTOHSCROLL : 0)
			);
	}

	LRESULT SysEdit::UpdateMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet)
	{
		switch (msg)
		{
		case WM_COMMAND:
			if (LOWORD(wParam) == GetID())
			{
				switch (HIWORD(wParam))
				{
				case EN_UPDATE:
					m_bEdited = true;
					if (m_pFunc)
						m_pFunc(GetText());
					break;
				}
			}
			break;

		case WM_CTLCOLOREDIT:
			if ((HWND)lParam == GetHandle())
			{
				::SetBkColor((HDC)wParam, m_cTextBk);
				::SetTextColor((HDC)wParam, m_cText);
				//::SetBkMode((HDC)wParam, TRANSPARENT);

				bRet = true;
				return (INT_PTR)hBk;
				//return (LRESULT)GetStockObject(WHITE_BRUSH);
			}
			break;
		}

		bRet = false;
		return 0;
	}

#ifdef UNICODE
	void SysEdit::RegisterMessage(void(*pFunc)(std::wstring wstrText))
	{
		m_pFunc = pFunc;
	}
#else
	void SysEdit::RegisterMessage(void(*pFunc)(std::string wstrText))
	{
		m_pFunc = pFunc;
	}
#endif

	void SysEdit::ApplyProperty()
	{
		long style = m_lBasicStyle;
		style |= (
			(m_property.left_align ? ES_LEFT : 0)
			| (m_property.right_align ? ES_RIGHT : 0)
			| (m_property.uppercase ? ES_UPPERCASE : 0)
			| (m_property.lowercase ? ES_LOWERCASE : 0)
			// password
			// read_only
			| (m_property.number_only ? ES_NUMBER : 0)
			);

		SetWindowLong(GetHandle(), GWL_STYLE, style);

		if (m_property.password)
			Edit_SetPasswordChar(GetHandle(), L'*');
		else
			Edit_SetPasswordChar(GetHandle(), 0);

		Edit_SetReadOnly(GetHandle(), m_property.read_only);
	}

	void SysEdit::RightAlign(bool enable)
	{
		m_property.left_align = !enable;
		m_property.right_align = enable;
		ApplyProperty();
	}

	void SysEdit::Uppercase(bool enable)
	{
		m_property.uppercase = enable;
		//m_property.lowercase = !enable;
		ApplyProperty();
	}

	void SysEdit::Lowercase(bool enable)
	{
		//m_property.uppercase = !enable;
		m_property.lowercase = enable;
		ApplyProperty();
	}

	void SysEdit::Password(bool enable)
	{
		m_property.password = enable;
		ApplyProperty();
	}

	void SysEdit::ReadOnly(bool enable)
	{
		m_property.read_only = enable;
		ApplyProperty();
	}

	void SysEdit::NumberOnly(bool enable)
	{
		m_property.number_only = enable;
		ApplyProperty();
	}

	int SysEdit::GetMaxTextLength()
	{
		return (int)SendMessage(GetHandle(), EM_GETLIMITTEXT, 0, 0);
	}

	void SysEdit::SetMaxTextLength(int len)
	{
		SendMessage(GetHandle(), EM_SETLIMITTEXT, (WPARAM)len, 0);
	}

	void SysEdit::SetBkColor(COLORREF color)
	{
		m_cBk = color;
		if (hBk)
			DeleteBrush(hBk);
		hBk = CreateSolidBrush(color);
		InvalidateRect(GetHandle(), nullptr, true);
	}

	void SysEdit::SetTextBkColor(COLORREF color)
	{
		m_cTextBk = color;
		InvalidateRect(GetHandle(), nullptr, true);
	}

	void SysEdit::SetTextColor(COLORREF color)
	{
		m_cText = color;
		InvalidateRect(GetHandle(), nullptr, true);
	}

	void SysEdit::GetSel(int* begin, int* end)
	{
		SendMessage(GetHandle(), EM_GETSEL, (WPARAM)begin, (LPARAM)end);
	}

	void SysEdit::SetSel(int begin, int end)
	{
		SendMessage(GetHandle(), EM_SETSEL, begin, end);
	}

	void SysEdit::Copy()
	{
		SendMessage(GetHandle(), WM_COPY, 0, 0);
	}

	void SysEdit::Cut()
	{
		SendMessage(GetHandle(), WM_CUT, 0, 0);
	}

	void SysEdit::Paste()
	{
		SendMessage(GetHandle(), WM_PASTE, 0, 0);
	}

	void SysEdit::Delete()
	{
		SendMessage(GetHandle(), WM_CLEAR, 0, 0);
	}

#ifdef UNICODE
	void SysEdit::Replace(std::wstring wstrText)
	{
		SendMessage(GetHandle(), EM_REPLACESEL, true, (LPARAM)wstrText.c_str());
	}
#else
	void SysEdit::Replace(std::string strText)
	{
		SendMessage(GetHandle(), EM_REPLACESEL, true, (LPARAM)strText.c_str());
	}
#endif

	bool SysEdit::IsEdited()
	{
		bool r = m_bEdited;
		m_bEdited = false;
		return r;
	}

}
