#pragma once
#include "pch.h"
#include "Utils.h"

namespace DirectInkPresenter
{
	namespace Ink
	{
		struct ContactInfo
		{
			bool bPrimary = false;
			enum class Type
			{
				Touch,
				Mouse,
				Pen
			} cType = Type::Touch;
			LONG x = -1;
			LONG y = -1;
			DWORD cxContact = -1;
			DWORD cyContact = -1;
			DWORD dwTime = -1;
			DWORD dwContactID = -1;
		};

		struct IContactNotify
		{
			virtual void OnContactMove() {};
			virtual void OnContactDown() {};
			virtual void OnContactUp() {};
			virtual void OnContactEnter() {};
			virtual void OnContactLeave() {};
			virtual void OnContactUpdated() {};
		};

		class ContactReceiver
		{
		public:
			// Ҫ��ȡ��������Ĵ��ڣ����մ����¼��Ľӿ�
			ContactReceiver(HWND hWnd, IContactNotify* pContactNotify);
			virtual ~ContactReceiver();

			// ����WM_TOUCH��Ϣ
			void ProcessInputEvents(UINT uMsg, WPARAM wParam, LPARAM lParam);
			// ��ȡ��һ�����������Ϣ������������򷵻�nullptr
			const ContactInfo* GetLastContactInfo(DWORD dwContactID = -1);
			// ��ȡ��ǰ��������Ϣ��ȷ����IContactNotify�е���
			const ContactInfo* GetCurrentContactInfo();
			// ��ȡ��ǰ������������ȷ����IContactNotify�е���
			size_t GetCurrentContactAmount() const { return m_contactLastMap.size(); }
			// ��ȡ��ǰ������ID
			DWORD GetCurrentContactID() const { return m_dwCurrentContactID; }
		private:
			// �ж��������Դ�Ƿ�Ϊ��������WM_*BUTTON*�е���
			static inline bool IsTouchEvent();
			// �ر�Windows 8�����Ĵ�������Ч����ˮ����
			void SetContactFeedback(BOOL bDisable);
			// ���´�����Ϣ
			void UpdateContactInfo(TOUCHINPUT& touchInput);
			// ���봥������Ϣ
			void TranslateContactInfo(TOUCHINPUT& touchInput);
			// �����������Ϣ
			void UpdateClickInfo(POINT pt);

			HWND										m_hWnd = nullptr;
			DWORD										m_dwCurrentContactID = -1;
			ContactInfo									m_currentContact = ContactInfo();
			IContactNotify*								m_contactNotify = nullptr;
			Utils::unordered_map<DWORD, ContactInfo>	m_contactLastMap = {};
		};
	}
}



