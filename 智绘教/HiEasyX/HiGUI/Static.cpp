#include "Static.h"

namespace HiEasyX
{
	void Static::Init()
	{
		m_bEnableBorder = false;
		m_bAutoRedrawWhenReceiveMsg = false;
	}

	Static::Static()
	{
		Init();
	}

#ifdef UNICODE
	Static::Static(int x, int y, int w, int h, std::wstring wstrText)
	{
		Init();
		SetRect(x, y, w, h);
		SetText(wstrText);
	}
#else
	Static::Static(int x, int y, int w, int h, std::string strText)
	{
		Init();
		SetRect(x, y, w, h);
		SetText(strText);
	}
#endif

#ifdef UNICODE
	std::vector<Static::Char> Static::Convert(std::wstring wstrText)
	{
		std::vector<Static::Char> vec;
		for (auto& ch : wstrText)
		{
			vec.push_back({ ch,m_cText,m_cBackground });
		}
		
		return vec;
	}
#else
	std::vector<Static::Char> Static::Convert(std::string strText)
	{
		std::vector<Static::Char> vec;
		for (auto& ch : strText)
		{
			vec.push_back({ ch,m_cText,m_cBackground });
		}
		
		return vec;
	}
#endif

#ifdef UNICODE
	std::wstring Static::Convert(std::vector<Char> vecText)
	{
		std::wstring wstr;
		for (auto& ch : vecText)
		{
			wstr.push_back(ch.ch);
		}
		
		return wstr;
	}
#else
	std::string Static::Convert(std::vector<Char> vecText)
	{
		std::string str;
		for (auto& ch : vecText)
		{
			str.push_back(ch.ch);
		}
		
		return str;
	}
#endif

#ifdef UNICODE
	void Static::ClearText()
	{
		m_wstrText.clear();
		m_vecText.clear();
		
		MarkNeedRedrawAndRender();
	}
#else
	void Static::ClearText()
	{
		m_strText.clear();
		m_vecText.clear();
		
		MarkNeedRedrawAndRender();
	}
#endif

#ifdef UNICODE
	void Static::AddText(std::wstring wstr, bool isSetTextColor, COLORREF cText, bool isSetBkColor, COLORREF cBk)
	{
		m_wstrText += wstr;
		if (!isSetTextColor)	cText = m_cText;
		if (!isSetBkColor)		cBk = m_cBackground;
		for (auto& ch : wstr)
		{
			m_vecText.push_back({ ch,cText,cBk });
		}
		
		MarkNeedRedrawAndRender();
	}
#else
	void Static::AddText(std::string str, bool isSetTextColor, COLORREF cText, bool isSetBkColor, COLORREF cBk)
	{
		m_strText += str;
		if (!isSetTextColor)	cText = m_cText;
		if (!isSetBkColor)		cBk = m_cBackground;
		for (auto& ch : str)
		{
			m_vecText.push_back({ ch,cText,cBk });
		}
		
		MarkNeedRedrawAndRender();
	}
#endif

#ifdef UNICODE
	void Static::SetText(std::wstring wstrText)
	{
		m_wstrText = wstrText;
		m_vecText = Convert(wstrText);
		
		MarkNeedRedrawAndRender();
	}
#else
	void Static::SetText(std::string strText)
	{
		m_strText = strText;
		m_vecText = Convert(strText);
		
		MarkNeedRedrawAndRender();
	}
#endif

#ifdef UNICODE
	void Static::SetText(std::vector<Char> vecText)
	{
		m_wstrText = Convert(vecText);
		m_vecText = vecText;
		
		MarkNeedRedrawAndRender();
	}
#else
	void Static::SetText(std::vector<Char> vecText)
	{
		m_strText = Convert(vecText);
		m_vecText = vecText;
		
		MarkNeedRedrawAndRender();
	}
#endif

#ifdef UNICODE
	void Static::Draw_Text(int nTextOffsetX, int nTextOffsetY)
	{
		int w = m_canvas.TextWidth(m_wstrText.c_str());
		int h = m_canvas.TextHeight(m_wstrText.c_str());
		
		m_canvas.MoveTo(
			(GetWidth() - w) / 2 + nTextOffsetX,
			(GetHeight() - h) / 2 + nTextOffsetY
			);
		
		for (auto& ch : m_vecText)
		{
			m_canvas.SetBkColor(ch.cBk);
			m_canvas.OutText(ch.ch, true, ch.cText);
		}
		
		m_canvas.SetBkColor(m_cBackground);
	}
#else
	void Static::Draw_Text(int nTextOffsetX, int nTextOffsetY)
	{
		int w = m_canvas.TextWidth(m_strText.c_str());
		int h = m_canvas.TextHeight(m_strText.c_str());
		
		m_canvas.MoveTo(
			(GetWidth() - w) / 2 + nTextOffsetX,
			(GetHeight() - h) / 2 + nTextOffsetY
			);
		
		for (auto& ch : m_vecText)
		{
			m_canvas.SetBkColor(ch.cBk);
			m_canvas.OutText(ch.ch, true, ch.cText);
		}
		
		m_canvas.SetBkColor(m_cBackground);
	}
#endif

	void Static::Draw(bool draw_child)
	{
		if (m_bRedraw)
		{
			ControlBase::Draw(false);

			if (m_pImgBlock)
			{
				m_canvas.PutImageIn_Alpha(
					m_pImgBlock->x, m_pImgBlock->y,
					m_pImgBlock->GetCanvas(),
					{ 0 },
					m_pImgBlock->alpha, m_pImgBlock->bUseSrcAlpha, m_pImgBlock->isAlphaCalculated
				);
			}

			Draw_Text();
		}

		if (draw_child)
		{
			DrawChild();
		}
	}

	void Static::SetImage(ImageBlock* pImgBlock)
	{
		m_pImgBlock = pImgBlock;

		MarkNeedRedrawAndRender();
	}
}
