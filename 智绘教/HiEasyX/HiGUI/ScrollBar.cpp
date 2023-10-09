#include "ScrollBar.h"

namespace HiEasyX
{
	void ScrollBar::OnSpecialButtonMsg(void* pThis, ControlBase* pCtrl, int msgid, ExMessage msg)
	{
		ScrollBar* _this = (ScrollBar*)pThis;

		switch (msgid)
		{
		case CM_CLICK:
			if (pCtrl == &_this->m_btnTop)
			{
				_this->SetSliderContentPos(0);
			}
			else if (pCtrl == &_this->m_btnBottom)
			{
				_this->SetSliderContentPos((float)_this->GetContentLength());
			}
			break;
		}
	}

	void ScrollBar::OnNormalButtonMsg()
	{
		clock_t tNow = clock();
		float move_len = (float)(tNow - m_tPressed) / CLOCKS_PER_SEC * m_nSliderSpeed;
		if (m_btnUp.IsPressed())
		{
			if (m_tPressed != 0)
			{
				MoveSlider(-move_len);
			}
			m_tPressed = tNow;

			MarkNeedRedrawAndRender();
		}
		else if (m_btnDown.IsPressed())
		{
			if (m_tPressed != 0)
			{
				MoveSlider(move_len);
			}
			m_tPressed = tNow;

			MarkNeedRedrawAndRender();
		}
		else
		{
			m_tPressed = 0;
		}
	}

	void ScrollBar::Init()
	{
		m_btnTop.SetMsgProcFunc(OnSpecialButtonMsg, this);
		m_btnBottom.SetMsgProcFunc(OnSpecialButtonMsg, this);

		AddChild(&m_btnTop);
		AddChild(&m_btnUp);
		AddChild(&m_btnDown);
		AddChild(&m_btnBottom);
		AddChild(&m_btnDrag);

		UpdateViewRatio();
	}

	void ScrollBar::UpdateSliderRect()
	{
		if (m_bHorizontal)
		{
			m_btnDrag.SetRect((int)(m_info.btnW * 2 + m_info.slider_move_len * m_fPosRatio) + 1, 2, m_info.slider_len, m_info.btnH - 4);
		}
		else
		{
			m_btnDrag.SetRect(2, (int)(m_info.btnH * 2 + m_info.slider_move_len * m_fPosRatio) + 1, m_info.btnW - 4, m_info.slider_len);
		}

		MarkNeedRedrawAndRender();
	}

	void ScrollBar::UpdatePosRatio()
	{
		if (m_nLen)
		{
			m_fPosRatio = m_fPos / m_nLen;
		}
		else
		{
			m_fPosRatio = 1;
		}
	}

	void ScrollBar::UpdateViewRatio()
	{
		if (m_nLen)
		{
			m_fViewRatio = m_nViewLen / (float)m_nLen;
		}
		else
		{
			m_fViewRatio = 1;
		}
		UpdateScrollBarInfo();
	}

	void ScrollBar::UpdateScrollBarInfo()
	{
		if (m_bHorizontal)
		{
			m_info.btnW = m_nBtnWidth;
			m_info.btnH = GetHeight();
			m_info.slider_free_len = GetWidth() - m_info.btnW * 4;
		}
		else
		{
			m_info.btnW = GetWidth();
			m_info.btnH = m_nBtnHeight;
			m_info.slider_free_len = GetHeight() - m_info.btnH * 4;
		}

		m_info.slider_free_len -= 2;	// 左右各空出 1 像素
		m_info.slider_len = (int)(m_info.slider_free_len * m_fViewRatio);
		m_info.slider_move_len = m_info.slider_free_len - m_info.slider_len;
	}

	void ScrollBar::SetButtonHeight(int h)
	{
		m_nBtnHeight = h;

		MarkNeedRedrawAndRender();
	}

	void ScrollBar::SetButtonWidth(int w)
	{
		m_nBtnWidth = w;

		MarkNeedRedrawAndRender();
	}

	ScrollBar::ScrollBar()
	{
		Init();
	}

	ScrollBar::ScrollBar(int x, int y, int w, int h, int len, int pos, bool bHorizontal)
	{
		m_bHorizontal = bHorizontal;
		SetRect(x, y, w, h);
		SetContentLength(len);
		SetSliderContentPos((float)pos);
		Init();
	}

	void ScrollBar::SetSliderContentPos(float pos)
	{
		if (pos < 0)
		{
			m_fPos = 0;
		}
		else if (pos > m_nLen)
		{
			m_fPos = (float)m_nLen;
		}
		else
		{
			m_fPos = pos;
		}
		UpdatePosRatio();
		UpdateSliderRect();
	}

	void ScrollBar::MoveSlider(float d)
	{
		SetSliderContentPos(m_fPos + d);
	}

	void ScrollBar::SetContentLength(int len)
	{
		m_nLen = len;
		UpdatePosRatio();
		UpdateViewRatio();
		UpdateSliderRect();
	}

	void ScrollBar::SetViewLength(int len)
	{
		m_nViewLen = len;
		UpdateViewRatio();
		UpdateSliderRect();
	}

	void ScrollBar::SetSliderSpeed(int speed)
	{
		m_nSliderSpeed = speed;
	}

	void ScrollBar::UpdateRect(RECT rctOld)
	{
		ControlBase::UpdateRect(rctOld);

		UpdateScrollBarInfo();

		if (m_bHorizontal)
		{
			m_btnTop.SetRect(0, 0, m_info.btnW, m_info.btnH);
			m_btnUp.SetRect(m_info.btnW, 0, m_info.btnW, m_info.btnH);
			m_btnBottom.SetRect(GetWidth() - m_info.btnW, 0, m_info.btnW, m_info.btnH);
			m_btnDown.SetRect(GetWidth() - m_info.btnW * 2, 0, m_info.btnW, m_info.btnH);
		}
		else
		{
			m_btnTop.SetRect(0, 0, m_info.btnW, m_info.btnH);
			m_btnUp.SetRect(0, m_info.btnH, m_info.btnW, m_info.btnH);
			m_btnDown.SetRect(0, GetHeight() - m_info.btnH * 2, m_info.btnW, m_info.btnH);
			m_btnBottom.SetRect(0, GetHeight() - m_info.btnH, m_info.btnW, m_info.btnH);
		}

		UpdateSliderRect();
	}

	void ScrollBar::UpdateMessage(ExMessage msg)
	{
		if (m_bVisible && m_bEnabled)
		{
			ControlBase::UpdateMessage(msg);

			// 此处响应消息无需转换
			//TransformMessage(msg);

			// 检查拖动状态
			m_MouseDrag.UpdateMessage(msg);
			if (m_btnDrag.IsPressed())
			{
				m_bDragging = true;
			}
			else if (msg.message == WM_LBUTTONUP)
			{
				m_bDragging = false;
			}

			// 拖动消息
			if (m_bDragging && m_MouseDrag.IsLeftDrag())
			{
				if (m_info.slider_move_len)
				{
					int drag = m_bHorizontal ? m_MouseDrag.GetDragX() : m_MouseDrag.GetDragY();
					MoveSlider(drag / (float)m_info.slider_move_len * m_nLen);
				}
			}

			// 滚轮消息响应区域
			RECT rctWheel = m_bSetOnWheelRct ? m_rctOnWheel : m_rct;
			bool bWheel = false;
			if (IsInRect(msg.x, msg.y, rctWheel) || m_bDragging)
			{
				if (msg.wheel)
				{
					bWheel = true;
					MoveSlider(-msg.wheel / 120.0f * 3);
				}
			}

			// 拖动或滚轮时需要重绘
			if (m_bDragging || bWheel)
			{
				MarkNeedRedrawAndRender();
			}
		}
	}

	void ScrollBar::Draw(bool draw_child)
	{
		// 按钮消息
		// 由于按下消息不会一直发送，所以此消息只能在每次重绘时处理
		OnNormalButtonMsg();

		if (m_bRedraw)
		{
			ControlBase::Draw();

			Canvas& canvasTop = m_btnTop.GetCanvas();
			Canvas& canvasUp = m_btnUp.GetCanvas();
			Canvas& canvasDown = m_btnDown.GetCanvas();
			Canvas& canvasBottom = m_btnBottom.GetCanvas();

			COLORREF cLine = DARKGRAY;

			if (m_bHorizontal)
			{
				int bottom_y = GetHeight() - m_nDrawInterval;
				int middle_y = GetHeight() / 2;
				int right_x = m_nBtnWidth - m_nDrawInterval;

				// 绘制朝左箭头
				auto pDrawLeft = [this, cLine, bottom_y, middle_y, right_x](Canvas& canvas) {
					canvas.SetLineColor(cLine);
					canvas.MoveTo(right_x, m_nDrawInterval);
					canvas.LineTo(m_nDrawInterval, middle_y);
					canvas.LineTo(right_x, bottom_y);
				};

				// 绘制朝右箭头
				auto pDrawRight = [this, cLine, bottom_y, middle_y, right_x](Canvas& canvas) {
					canvas.SetLineColor(cLine);
					canvas.MoveTo(m_nDrawInterval, m_nDrawInterval);
					canvas.LineTo(right_x, middle_y);
					canvas.LineTo(m_nDrawInterval, bottom_y);
				};

				pDrawLeft(canvasTop);
				pDrawLeft(canvasUp);
				pDrawRight(canvasDown);
				pDrawRight(canvasBottom);

				canvasTop.Line(m_nDrawInterval, m_nDrawInterval, m_nDrawInterval, bottom_y);
				canvasBottom.Line(right_x, m_nDrawInterval, right_x, bottom_y);
			}
			else
			{
				int bottom_y = m_nBtnHeight - m_nDrawInterval;
				int middle_x = GetWidth() / 2;
				int right_x = GetWidth() - m_nDrawInterval;

				// 绘制朝上箭头
				auto pDrawUp = [this, cLine, bottom_y, middle_x, right_x](Canvas& canvas) {
					canvas.SetLineColor(cLine);
					canvas.MoveTo(m_nDrawInterval, bottom_y);
					canvas.LineTo(middle_x, m_nDrawInterval);
					canvas.LineTo(right_x, bottom_y);
				};

				// 绘制朝下箭头
				auto pDrawDown = [this, cLine, bottom_y, middle_x, right_x](Canvas& canvas) {
					canvas.SetLineColor(cLine);
					canvas.MoveTo(m_nDrawInterval, m_nDrawInterval);
					canvas.LineTo(middle_x, bottom_y);
					canvas.LineTo(right_x, m_nDrawInterval);
				};

				pDrawUp(canvasTop);
				pDrawUp(canvasUp);
				pDrawDown(canvasDown);
				pDrawDown(canvasBottom);

				canvasTop.Line(m_nDrawInterval, m_nDrawInterval, right_x, m_nDrawInterval);
				canvasBottom.Line(m_nDrawInterval, bottom_y, right_x, bottom_y);
			}
		}

		if (draw_child)
		{
			DrawChild();
		}
	}

	bool ScrollBar::IsSliderPosChanged()
	{
		bool r = m_bSliderPosChanged;
		m_bSliderPosChanged = false;
		return m_bSliderPosChanged;
	}

	void ScrollBar::SetOnWheelRect(RECT rct)
	{
		m_rctOnWheel = rct;
		m_bSetOnWheelRct = true;
	}

	void ScrollBar::EnableHorizontal(bool enable)
	{
		m_bHorizontal = enable;

		MarkNeedRedrawAndRender();
	}
}
