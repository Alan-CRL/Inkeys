#include "pch.h"
#include "ContactReceiver.h"

DirectInkPresenter::Ink::ContactReceiver::ContactReceiver(HWND hWnd, IContactNotify* pContactNotify) :
	m_hWnd(hWnd), m_contactNotify(pContactNotify)
{
	if (
	    IsWindow(m_hWnd) and
	    pContactNotify and
	    (GetSystemMetrics(SM_DIGITIZER) & (NID_READY + NID_MULTI_INPUT))
	)
	{
		SetContactFeedback(TRUE);
		RegisterTouchWindow(hWnd, TWF_FINETOUCH);
	}
}

DirectInkPresenter::Ink::ContactReceiver::~ContactReceiver()
{
	if (IsWindow(m_hWnd))
	{
		UnregisterTouchWindow(m_hWnd);
		SetContactFeedback(FALSE);
	}
	m_hWnd = nullptr;
	m_contactNotify = nullptr;
}

bool DirectInkPresenter::Ink::ContactReceiver::IsTouchEvent() 
{
	constexpr LONG_PTR c_SIGNATURE_MASK = 0xFFFFFF00;
	constexpr LONG_PTR c_MOUSEEVENTF_FROMTOUCH = 0xFF515700;

	LONG_PTR extraInfo = GetMessageExtraInfo();
	return ((extraInfo & c_SIGNATURE_MASK) == c_MOUSEEVENTF_FROMTOUCH);
}

void DirectInkPresenter::Ink::ContactReceiver::ProcessInputEvents(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_TOUCH)
	{
		int cInputs = LOWORD(wParam);
		PTOUCHINPUT pInputs = new TOUCHINPUT[cInputs];
		Utils::ThrowIfFailed(
			pInputs ? S_OK : E_POINTER
		);
		if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT)))
		{
			if (m_contactLastMap.empty())
			{
				ShowCursor(FALSE);
				m_contactNotify->OnContactEnter();				
			}
			for (int i = 0; i < cInputs; i++)
			{
				TOUCHINPUT& touchInput = pInputs[i];

				TranslateContactInfo(touchInput);
				UpdateContactInfo(touchInput);
				
				if (touchInput.dwFlags & TOUCHEVENTF_DOWN)
				{
					m_contactNotify->OnContactDown();
				}
				if (touchInput.dwFlags & TOUCHEVENTF_MOVE)
				{
					m_contactNotify->OnContactMove();
				}
				if (touchInput.dwFlags & TOUCHEVENTF_UP)
				{
					m_contactNotify->OnContactUp();

					m_contactLastMap.erase(touchInput.dwID);
				}
				else
				{
					// 已经记录了这个触摸点
					// 存入上一个触摸点信息中
					m_contactLastMap[touchInput.dwID] = m_currentContact;
				}

				m_contactNotify->OnContactUpdated();
			}
			if (m_contactLastMap.empty())
			{
				m_contactNotify->OnContactLeave();
				ShowCursor(TRUE);
			}
			m_dwCurrentContactID = -1;
			m_currentContact = ContactInfo();

			delete[] pInputs;
			CloseTouchInputHandle((HTOUCHINPUT)lParam);
		}
	}
	if (
		(
			uMsg == WM_LBUTTONDOWN or
			uMsg == WM_LBUTTONUP or
			uMsg == WM_MOUSEMOVE
		) and
		!IsTouchEvent()
	)
	{
		static bool valid = false;
		if (uMsg == WM_LBUTTONDOWN)
		{
			valid = true;
		}
		if (valid)
		{
			if (m_contactLastMap.empty())
			{
				SetCapture(m_hWnd);
				m_contactNotify->OnContactEnter();
			}
			UpdateClickInfo({ LOWORD(lParam), HIWORD(lParam) });
			if (uMsg == WM_LBUTTONDOWN)
			{
				m_contactNotify->OnContactDown();
			}
			if (uMsg == WM_MOUSEMOVE)
			{
				m_contactNotify->OnContactMove();
			}
			if (uMsg == WM_LBUTTONUP)
			{
				m_contactNotify->OnContactUp();

				m_contactLastMap.erase(0);
				valid = false;
			}
			else
			{
				m_contactLastMap[0] = m_currentContact;
			}
			if (m_contactLastMap.empty())
			{
				m_contactNotify->OnContactLeave();
				ReleaseCapture();
			}
			m_contactNotify->OnContactUpdated();
		}
	}
}


const DirectInkPresenter::Ink::ContactInfo* DirectInkPresenter::Ink::ContactReceiver::GetLastContactInfo(DWORD dwContactID)
{
	if (dwContactID == -1)
	{
		dwContactID = m_dwCurrentContactID;
	}
	if (m_dwCurrentContactID == -1)
	{
		return nullptr;
	}
	if (m_contactLastMap.find(dwContactID) != m_contactLastMap.end())
	{
		return &m_contactLastMap[dwContactID];
	}
	return nullptr;
}

const DirectInkPresenter::Ink::ContactInfo* DirectInkPresenter::Ink::ContactReceiver::GetCurrentContactInfo()
{
	return &m_currentContact;
}

void DirectInkPresenter::Ink::ContactReceiver::SetContactFeedback(BOOL bDisable)
{
	static const auto pfnSetWindowFeedbackSetting = (BOOL(WINAPI*)(HWND, int, DWORD, UINT32, CONST VOID*))GetProcAddress(GetModuleHandle(_T("User32")), "SetWindowFeedbackSetting");
	if (!bDisable)
	{
		if (pfnSetWindowFeedbackSetting)
		{
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_CONTACTVISUALIZATION, 0, sizeof(BOOL), nullptr);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_TAP, 0, sizeof(BOOL), nullptr);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_DOUBLETAP, 0, sizeof(BOOL), nullptr);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_PRESSANDHOLD, 0, sizeof(BOOL), nullptr);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_RIGHTTAP, 0, sizeof(BOOL), nullptr);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_GESTURE_PRESSANDTAP, 0, sizeof(BOOL), nullptr);
		}
	}
	else
	{
		if (pfnSetWindowFeedbackSetting)
		{
			BOOL bOpened = FALSE;
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_CONTACTVISUALIZATION, 0, sizeof(BOOL), &bOpened);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_TAP, 0, sizeof(BOOL), &bOpened);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_DOUBLETAP, 0, sizeof(BOOL), &bOpened);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_PRESSANDHOLD, 0, sizeof(BOOL), &bOpened);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_TOUCH_RIGHTTAP, 0, sizeof(BOOL), &bOpened);
			pfnSetWindowFeedbackSetting(m_hWnd, FEEDBACK_GESTURE_PRESSANDTAP, 0, sizeof(BOOL), &bOpened);
		}
	}
}

void DirectInkPresenter::Ink::ContactReceiver::UpdateContactInfo(TOUCHINPUT& touchInput)
{
	// 记录当前触摸点ID
	m_dwCurrentContactID = touchInput.dwID;
	// 主触摸点
	m_currentContact.bPrimary = (touchInput.dwFlags & TOUCHEVENTF_PRIMARY);
	// 判断是由手还是笔触发的触摸事件
	m_currentContact.cType = touchInput.dwFlags & TOUCHEVENTF_PEN ? ContactInfo::Type::Pen : ContactInfo::Type::Touch;
	// 开始写入当前触摸点信息
	// 触摸点坐标，面积，时间戳，ID
	m_currentContact.x = touchInput.x;
	m_currentContact.y = touchInput.y;
	m_currentContact.cxContact = touchInput.cxContact;
	m_currentContact.cyContact = touchInput.cyContact;
	m_currentContact.dwTime = touchInput.dwTime;
	m_currentContact.dwContactID = touchInput.dwID;
}

void DirectInkPresenter::Ink::ContactReceiver::TranslateContactInfo(TOUCHINPUT& touchInput)
{
	// 转换触摸坐标
	POINT ptInput = { TOUCH_COORD_TO_PIXEL(touchInput.x), TOUCH_COORD_TO_PIXEL(touchInput.y) };
	ScreenToClient(m_hWnd, &ptInput);
	touchInput.x = ptInput.x;
	touchInput.y = ptInput.y;

	// 转换触摸面积
	if (touchInput.dwMask & TOUCHINPUTMASKF_CONTACTAREA)
	{
		touchInput.cxContact = TOUCH_COORD_TO_PIXEL(touchInput.cxContact);
		touchInput.cyContact = TOUCH_COORD_TO_PIXEL(touchInput.cyContact);
	}
	else
	{
		touchInput.cxContact = -1;
		touchInput.cyContact = -1;
	}
}

void DirectInkPresenter::Ink::ContactReceiver::UpdateClickInfo(POINT pt)
{
	// 记录当前点击ID
	m_dwCurrentContactID = 0;
	m_currentContact.bPrimary = true;
	// 鼠标触发的事件
	m_currentContact.cType = ContactInfo::Type::Mouse;
	// 开始写入当前信息
	// 坐标，面积，时间戳，ID
	m_currentContact.x = pt.x;
	m_currentContact.y = pt.y;
	m_currentContact.cxContact = 1;
	m_currentContact.cyContact = 1;
	m_currentContact.dwTime = -1;
	m_currentContact.dwContactID = 0;
}
