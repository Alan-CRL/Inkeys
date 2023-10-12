#include "HiContainer.h"

namespace HiEasyX
{
	Container::Container()
	{
	}

	Container::~Container()
	{
	}

	void Container::UpdateRect(RECT rctOld)
	{
	}

	void Container::SetRect(RECT rct)
	{
		RECT old = m_rct;
		m_rct = rct;
		if (m_rct.bottom < m_rct.top) m_rct.bottom = m_rct.top;
		if (m_rct.right < m_rct.left) m_rct.right = m_rct.left;
		UpdateRect(old);
	}

	void Container::SetRect(int x, int y, int w, int h)
	{
		SetRect({ x,y,x + w,y + h });
	}

	void Container::SetPos(int x, int y)
	{
		SetRect({ x,y,x + GetWidth(),y + GetHeight() });
	}

	void Container::SetPos(POINT pt)
	{
		SetRect({ pt.x,pt.y,pt.x + GetWidth(),pt.y + GetHeight() });
	}

	void Container::SetWidth(int w)
	{
		SetRect({ m_rct.left, m_rct.top, m_rct.left + w, m_rct.bottom });
	}

	void Container::SetHeight(int h)
	{
		SetRect({ m_rct.left, m_rct.top, m_rct.right, m_rct.top + h });
	}

	void Container::Resize(int w, int h)
	{
		SetRect({ m_rct.left, m_rct.top, m_rct.left + w, m_rct.top + h });
	}

	void Container::MoveRel(int dx, int dy)
	{
		if(dx || dy)
			SetRect({ m_rct.left + dx, m_rct.top + dy, m_rct.right + dx, m_rct.bottom + dy });
	}
}
