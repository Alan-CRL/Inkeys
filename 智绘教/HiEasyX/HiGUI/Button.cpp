#include "Button.h"

namespace HiEasyX
{
	void Button::InitColor()
	{
		m_cBorder = m_cBorder_Normal;
		m_cBackground = m_cBackground_Normal;
	}

	Button::Button()
	{
		InitColor();
	}

#ifdef UNICODE
	Button::Button(int x, int y, int w, int h, std::wstring wstrText)
	{
		SetRect(x, y, w, h);
		SetText(wstrText);
		InitColor();
	}
#else
	Button::Button(int x, int y, int w, int h, std::string strText)
	{
		SetRect(x, y, w, h);
		SetText(strText);
		InitColor();
	}
#endif

	void Button::SetEnable(bool enable)
	{
		ControlBase::SetEnable(enable);
		if (!enable)
		{
			m_cText = m_cText_Disabled;
			m_cBorder = m_cBorder_Disabled;
			m_cBackground = m_cBackground_Disabled;
		}
		else
		{
			InitColor();
		}
	}

	void Button::UpdateMessage(ExMessage msg)
	{
		if (m_bVisible && m_bEnabled)
		{
			ControlBase::UpdateMessage(msg);

			if (!m_bEnableClassicStyle)
			{
				if (m_bPressed)
				{
					m_cBorder = m_cBorder_Pressed;
					m_cBackground = m_cBackground_Pressed;
				}
				else if (m_bHovered)
				{
					m_cBorder = m_cBorder_Hovered;
					m_cBackground = m_cBackground_Hovered;
				}
				else
				{
					InitColor();
				}
			}
		}
	}

	void Button::EnableClassicStyle(bool enable)
	{
		m_bEnableClassicStyle = enable;
		if (enable)
		{
			m_cBackground = CLASSICGRAY;
		}
	}

	void Button::Draw(bool draw_child)
	{
		if (m_bRedraw)
		{
			ControlBase::Draw(false);

			if (m_bEnableClassicStyle)
			{
				if (m_bPressed)
				{
					m_canvas.SetLineColor(m_cClassicPressedBorder3D);
					m_canvas.Line(1, 1, 1, GetHeight() - 2);
					m_canvas.Line(1, 1, GetWidth() - 2, 1);
					Draw_Text(1, 1);
				}
				else
				{
					m_canvas.SetLineColor(m_cClassicNormalBorder3D);
					m_canvas.Line(GetWidth() - 2, 1, GetWidth() - 2, GetHeight() - 2);
					m_canvas.Line(1, GetHeight() - 2, GetWidth() - 2, GetHeight() - 2);
					Draw_Text();
				}
			}
			else
			{
				Draw_Text();
			}
		}

		if (draw_child)
		{
			DrawChild();
		}
	}
}
