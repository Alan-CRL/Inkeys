#include "SysGroup.h"
#include <tchar.h>

namespace HiEasyX
{
	void SysGroup::RealCreate(HWND hParent)
	{
		m_type = SCT_Group;
		m_hWnd = CreateControl(
			hParent,
			_T("Button"),
			_T(""),
			WS_CHILD | WS_GROUP
		);
	}

	SysGroup::SysGroup()
	{
	}

	SysGroup::SysGroup(HWND hParent)
	{
		Create(hParent);
	}

	void SysGroup::Create(HWND hParent)
	{
		RealCreate(hParent);
	}
}
