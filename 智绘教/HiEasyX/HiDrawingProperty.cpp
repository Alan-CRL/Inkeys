#include "HiDrawingProperty.h"

namespace HiEasyX
{
	void DrawingProperty::SaveProperty()
	{
		m_pImg = GetWorkingImage();
		getaspectratio(&m_xasp, &m_yasp);
		m_cBk = getbkcolor();
		m_nBkMode = getbkmode();
		m_cFill = getfillcolor();
		getfillstyle(&m_fillstyle);
		m_cLine = getlinecolor();
		getlinestyle(&m_linestyle);
		m_nPolyFillMode = getpolyfillmode();
		m_nRop2Mode = getrop2();
		m_cText = gettextcolor();
		gettextstyle(&m_font);

		m_isSaved = true;
	}

	void DrawingProperty::SaveWorkingImageOnly()
	{
		m_pImg = GetWorkingImage();
		m_isSaved = true;
	}

	void DrawingProperty::ApplyProperty()
	{
		if (m_isSaved)
		{
			SetWorkingImage(m_pImg);
			setaspectratio(m_xasp, m_yasp);
			setbkcolor(m_cBk);
			setbkmode(m_nBkMode);
			setfillcolor(m_cFill);
			setfillstyle(&m_fillstyle);
			setlinecolor(m_cLine);
			setlinestyle(&m_linestyle);
			setpolyfillmode(m_nPolyFillMode);
			setrop2(m_nRop2Mode);
			settextcolor(m_cText);
			settextstyle(&m_font);
		}
	}

	void DrawingProperty::ApplyWorkingImageOnly()
	{
		if (m_isSaved)
		{
			SetWorkingImage(m_pImg);
		}
	}

	bool DrawingProperty::IsSaved()
	{
		return m_isSaved;
	}

	void DrawingProperty::Reset()
	{
		m_isSaved = false;
	}
};
