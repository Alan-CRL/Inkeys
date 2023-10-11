#include "ControlBase.h"

namespace HiEasyX
{
	void ControlBase::Init()
	{
		// TODO：等 EasyX 修复后加上
		/*m_canvas.SetTypeface(L"Arial");
		m_canvas.SetBkColor(m_cBackground);
		m_canvas.SetTextColor(m_cText);
		m_canvas.SetLineColor(m_cBorder);*/
	}

	ControlBase::ControlBase()
	{
		Init();
	}

#ifdef UNICODE
	ControlBase::ControlBase(std::wstring wstrText)
	{
		Init();
		SetText(wstrText);
	}
#else
	ControlBase::ControlBase(int x, int y, int w, int h, std::string strText)
	{
		Init();
		SetRect(x, y, w, h);
		SetText(strText);
	}
#endif

	ControlBase::~ControlBase()
	{
	}

	void ControlBase::UpdateRect(RECT rctOld)
	{
		Container::UpdateRect(rctOld);

		m_canvas.Resize(GetWidth(), GetHeight());

		// 标识重绘和渲染
		MarkNeedRedrawAndRender();

		// 不是第一次设置位置时，需要清除旧区域
		if (m_bCompleteFirstSetRect)
		{
			// 标识清空旧区域
			MarkNeedClearRect(rctOld);
		}
		else
		{
			m_bCompleteFirstSetRect = true;
		}

		if (m_pParent)
		{
			m_pParent->ChildRectChanged(this);
		}
	}

	void ControlBase::MarkNeedRedrawAndRender()
	{
		m_bRedraw = true;
		m_bRender = true;

		// 重绘后，渲染时也要渲染子控件，否则会覆盖
		for (auto& child : m_listChild)
			child->m_bRender = true;
	}

	void ControlBase::MarkNeedClearRect(RECT rct)
	{
		m_bClear = true;
		m_rctClear = rct;
	}

	void ControlBase::SetEnable(bool enable)
	{
		m_bEnabled = enable;

		MarkNeedRedrawAndRender();
	}

	void ControlBase::SetParent(ControlBase* p)
	{
		if (m_pParent)
		{
			m_pParent->RemoveChild(this);
		}
		if (p)
		{
			m_pParent = p;
			p->AddChild(this);
		}

		MarkNeedRedrawAndRender();
	}

	void ControlBase::EnableAutoSizeForChild(bool enable)
	{
		m_bAutoSizeForChild = enable;
	}

	std::list<ControlBase*>& ControlBase::GetChildList()
	{
		return m_listChild;
	}

	size_t ControlBase::GetChildCount()
	{
		size_t sum = m_listChild.size();
		for (auto& child : m_listChild)
			sum += child->GetChildCount();
		return sum;
	}

	void ControlBase::AddChild(ControlBase* p, int offset_x, int offset_y)
	{
		for (auto& child : m_listChild)
			if (child == p)
				return;
		p->MoveRel(offset_x, offset_y);
		p->m_pParent = this;
		m_listChild.push_back(p);
		ChildRectChanged(p);

		MarkNeedRedrawAndRender();
	}

	void ControlBase::RemoveChild(ControlBase* p)
	{
		m_listChild.remove(p);

		MarkNeedRedrawAndRender();
	}

	void ControlBase::SetVisible(bool bVisible)
	{
		m_bVisible = bVisible;

		// 设为可见时重新渲染
		if (m_bVisible)
		{
			m_bRender = true;
		}
		// 设为不可见时清除
		else
		{
			MarkNeedClearRect(m_rct);
		}
	}

	void ControlBase::EnableAutoRedraw(bool enable)
	{
		m_bAutoRedrawWhenReceiveMsg = enable;
	}

	void ControlBase::SetBkColor(COLORREF color)
	{
		m_cBackground = color;
		m_canvas.SetBkColor(color);

		MarkNeedRedrawAndRender();
	}

	void ControlBase::SetTextColor(COLORREF color)
	{
		m_cText = color;
		m_canvas.SetTextColor(color);

		MarkNeedRedrawAndRender();
	}

	void ControlBase::EnableBorder(bool bEnableBorder, COLORREF color, int thickness)
	{
		m_bEnableBorder = bEnableBorder;
		if (bEnableBorder)
		{
			m_cBorder = color;
			m_nBorderThickness = thickness;

			m_canvas.SetLineColor(m_cBorder);
		}

		MarkNeedRedrawAndRender();
	}

	void ControlBase::SetAlpha(BYTE alpha, bool bUseCanvasAlpha, bool isAlphaCalculated)
	{
		m_alpha = alpha;
		m_bUseCanvasAlpha = bUseCanvasAlpha;
		m_isAlphaCalculated = isAlphaCalculated;

		MarkNeedRedrawAndRender();
	}

#ifdef UNICODE
	void ControlBase::SetText(std::wstring wstr)
	{
		m_wstrText = wstr;
		
		MarkNeedRedrawAndRender();
	}
#else
	void ControlBase::SetText(std::string str)
	{
		m_strText = str;
		
		MarkNeedRedrawAndRender();
	}
#endif

	void ControlBase::Draw_Text(int nTextOffsetX, int nTextOffsetY)
	{
		m_canvas.SetBkColor(m_cBackground);
		m_canvas.SetTextColor(m_cText);
		
#ifdef UNICODE
		int w = m_canvas.TextWidth(m_wstrText.c_str());
		int h = m_canvas.TextHeight(m_wstrText.c_str());
		
		m_canvas.OutTextXY(
			(GetWidth() - w) / 2 + nTextOffsetX,
			(GetHeight() - h) / 2 + nTextOffsetY,
			m_wstrText.c_str()
			);
#else
		int w = m_canvas.TextWidth(m_strText.c_str());
		int h = m_canvas.TextHeight(m_strText.c_str());
		
		m_canvas.OutTextXY(
			(GetWidth() - w) / 2 + nTextOffsetX,
			(GetHeight() - h) / 2 + nTextOffsetY,
			m_strText.c_str()
			);
#endif
		
	}

	void ControlBase::Redraw()
	{
		MarkNeedRedrawAndRender();
		Draw();
	}

	void ControlBase::Draw(bool draw_child)
	{
		if (m_bRedraw || m_bAlwaysRedrawAndRender)
		{
			m_canvas.SetLineThickness(m_nBorderThickness);
			m_canvas.FillRectangle(
				0, 0, GetWidth() - 1, GetHeight() - 1,
				true, m_bEnableBorder ? m_cBorder : m_cBackground, m_cBackground
			);

			if (!m_bAlwaysRedrawAndRender)
			{
				m_bRedraw = false;
			}
		}

		if (draw_child)
		{
			DrawChild();
		}
	}

	void ControlBase::DrawChild()
	{
		for (auto& child : m_listChild)
		{
			child->Draw();
		}
	}

	void ControlBase::Render(Canvas* dst, RECT* pRct, int* pCount)
	{
		// 当前控件获取到的重绘矩形记录
		size_t size = m_bRender ? 1 : GetChildCount();	// 若自身需要绘制，则只需要记录一个矩形
		RECT* my_rct = new RECT[size];
		int my_count = 0;								// 当前控件获取到的重绘区域数量统计

		// 为当前控件的子控件执行清除任务
		// 必须在 Render 前执行，否则可能覆盖效果
		for (auto& child : m_listChild)
		{
			if (child->m_bClear)
			{
				m_canvas.ClearRectangle(child->m_rctClear);
				MOVE_RECT(child->m_rctClear, m_rct.left, m_rct.top);
				dst->ClearRectangle(child->m_rctClear);

				child->m_bClear = false;
				child->m_rctClear = { 0 };
			}
		}

		// 控件必须可显示才渲染
		if (m_bVisible)
		{
			// 子控件可能很多，开启批量绘制
			m_canvas.BeginBatchDrawing();

			// 如果自身需要渲染，则只需要记录自身矩形
			if (m_bRender || m_bAlwaysRedrawAndRender)
			{
				// 子控件先绘制到此控件画布上，再绘制到 dst
				for (auto& child : m_listChild)
					child->Render(&m_canvas);

				my_rct[my_count++] = { 0,0,GetWidth(),GetHeight() };

				if (!m_bAlwaysRedrawAndRender)
					m_bRender = false;
			}

			// 若自身不渲染，则必须记录子控件渲染区域
			else
			{
				for (auto& child : m_listChild)
				{
					child->Render(&m_canvas, my_rct, &my_count);
				}
			}

			m_canvas.EndBatchDrawing();

			// 渲染有更新的区域
			for (int i = 0; i < my_count; i++)
			{
				dst->PutImageIn_Alpha(
					m_rct.left, m_rct.top,
					&m_canvas,
					my_rct[i],
					m_alpha, m_bUseCanvasAlpha, m_isAlphaCalculated
				);
			}

			// 为了返回区域信息给父控件，需要转换区域坐标参考系
			if (pRct)
			{
				for (int i = 0; i < my_count; i++)
				{
					MOVE_RECT(my_rct[i], m_rct.left, m_rct.top);
					pRct[(*pCount)++] = my_rct[i];
				}
			}
		}

		delete[] my_rct;

		/*for (auto& child : m_listChild)
			child->Render(&m_canvas);
		dst->PutImageIn_Alpha(
			m_rct.left, m_rct.top,
			&m_canvas,
			{ 0 },
			m_alpha, m_bUseCanvasAlpha, m_isAlphaCalculated
		);*/
	}

	void ControlBase::SetMsgProcFunc(MESSAGE_PROC_FUNC func)
	{
		m_funcMessageProc = func;
		m_funcMessageProc_Class = nullptr;
		m_pCalledClass = nullptr;
	}

	void ControlBase::SetMsgProcFunc(MESSAGE_PROC_FUNC_CLASS static_class_func, void* _this)
	{
		m_funcMessageProc = nullptr;
		m_funcMessageProc_Class = static_class_func;
		m_pCalledClass = _this;
	}

	ExMessage& ControlBase::TransformMessage(ExMessage& msg)
	{
		switch (GetExMessageType(msg))
		{
		case EM_MOUSE:
			msg.x -= GetX();
			msg.y -= GetY();
			break;
		default:
			break;
		}
		return msg;
	}

	void ControlBase::CallUserMsgProcFunc(int msgid, ExMessage msg)
	{
		if (m_funcMessageProc)
		{
			m_funcMessageProc(this, msgid, msg);
		}
		else if (m_funcMessageProc_Class)
		{
			m_funcMessageProc_Class(m_pCalledClass, this, msgid, msg);
		}
	}

	void ControlBase::ChildRectChanged(ControlBase* pChild)
	{
		if (m_bAutoSizeForChild)
		{
			int _w = GetWidth(), _h = GetHeight();
			if (pChild->m_rct.right > _w)		_w += pChild->m_rct.right - _w;
			if (pChild->m_rct.bottom > _h)		_h += pChild->m_rct.bottom - _h;
			Resize(_w, _h);
		}
	}

	void ControlBase::UpdateMessage(ExMessage msg)
	{
		if (m_bVisible && m_bEnabled)
		{
			TransformMessage(msg);

			// 标识该消息是否值得重绘
			bool msg_worth_redraw = false;

			// 鼠标在区域内
			if (IsInRect(msg.x, msg.y, { 0,0,GetWidth(),GetHeight() }))
			{
				// 鼠标移入
				if (!m_bHovered)
				{
					m_bHovered = true;
					CallUserMsgProcFunc(CM_HOVER, msg);
				}

				switch (msg.message)
				{
				case WM_LBUTTONDOWN:
				{
					m_bPressed = true;
					CallUserMsgProcFunc(CM_PRESS, msg);
					if (!m_bFocused)
					{
						m_bFocused = true;
						CallUserMsgProcFunc(CM_FOCUS, msg);
					}
				}
				break;

				case WM_LBUTTONUP:
				{
					m_bPressed = false;
					if (m_bFocused)
					{
						CallUserMsgProcFunc(CM_PRESS_OVER, msg);
						CallUserMsgProcFunc(CM_CLICK, msg);
					}
				}
				break;

				case WM_LBUTTONDBLCLK:
				{
					CallUserMsgProcFunc(CM_DOUBLE_CLICK, msg);
				}
				break;

				default:
				{
					CallUserMsgProcFunc(CM_OTHER, msg);
				}
				break;

				}

				msg_worth_redraw = true;
			}

			// 鼠标不在区域内
			else
			{
				// 移出
				if (m_bHovered)
				{
					m_bHovered = false;
					CallUserMsgProcFunc(CM_HOVER_OVER, msg);

					msg_worth_redraw = true;
				}

				// 离开区域，按下失效
				if (m_bPressed)
				{
					m_bPressed = false;
					CallUserMsgProcFunc(CM_PRESS_OVER, msg);

					msg_worth_redraw = true;
				}

				switch (msg.message)
				{
				case WM_LBUTTONDOWN:
				{
					if (m_bFocused)
					{
						m_bFocused = false;
						CallUserMsgProcFunc(CM_FOCUS_OVER, msg);

						msg_worth_redraw = true;
					}
				}
				break;

				default:
				{
					CallUserMsgProcFunc(CM_OTHER, msg);
				}
				break;

				}
			}

			// 默认重绘，且满足条件
			if (m_bAutoRedrawWhenReceiveMsg && msg_worth_redraw)
			{
				MarkNeedRedrawAndRender();
			}

			for (auto& child : m_listChild)
			{
				child->UpdateMessage(msg);
			}
		}
	}
}
