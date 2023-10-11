#include "Page.h"

#include "../HiDrawingProperty.h"

namespace HiEasyX
{
	void Page::Init(COLORREF cBk)
	{
		EnableBorder(false);

		// TODO：等 EasyX 修好了再启用
		//SetBkColor(cBk);

		m_bAutoRedrawWhenReceiveMsg = false;
	}

	Page::Page()
	{
		Init();
	}

	Page::Page(int w, int h, COLORREF cBk)
	{
		SetRect(0, 0, w, h);
		Init(cBk);
	}

	Page::Page(Canvas* pCanvas)
	{
		BindToCanvas(pCanvas);
		Init();
	}

	void Page::BindToCanvas(Canvas* pCanvas)
	{
		SetRect(0, 0, pCanvas->GetWidth(), pCanvas->GetHeight());
		m_pCanvas = pCanvas;
		SetBkColor(m_pCanvas->GetBkColor());
	}

	void Page::push(ControlBase* pCtrl, int offset_x, int offset_y)
	{
		AddChild(pCtrl, offset_x, offset_y);
	}

	void Page::push(const std::list<ControlBase*> list)
	{
		for (auto& child : list)
			AddChild(child);
	}

	void Page::remove(ControlBase* pCtrl)
	{
		RemoveChild(pCtrl);
	}

	void Page::Render(Canvas* dst, RECT* pRct, int* pCount)
	{
		if (!dst && m_pCanvas)
		{
			dst = m_pCanvas;
		}

		return ControlBase::Render(dst, pRct, pCount);
	}

	void Page::UpdateImage(Canvas* pCanvas)
	{
		Draw();
		Render(pCanvas);
	}
}
