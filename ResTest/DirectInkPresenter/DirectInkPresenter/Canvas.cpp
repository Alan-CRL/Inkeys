#include "pch.h"
#include "Canvas.h"

void DirectInkPresenter::UI::Canvas::CreateDeviceResources()
{
	if (!m_d2dHwndRenderTarget)
	{
		Graphics::CreateTargetForHwnd(GetHwnd(), &m_d2dHwndRenderTarget);
	}
}

void DirectInkPresenter::UI::Canvas::DiscardDeviceResources()
{
	m_d2dHwndRenderTarget.Reset();
}

void DirectInkPresenter::UI::Canvas::UpdateDwmBlurBehind()
{
	if (IsCompositionActive())
	{
		DWM_BLURBEHIND bb = { DWM_BB_ENABLE | DWM_BB_BLURREGION | DWM_BB_TRANSITIONONMAXIMIZED, TRUE, CreateRectRgn(0, 0, -1, -1), TRUE };
		DwmEnableBlurBehindWindow(GetHwnd(), &bb);
		DeleteObject(bb.hRgnBlur);
	}
}

void DirectInkPresenter::UI::Canvas::ShowEraser(bool bShow)
{
	m_bShow = bShow;
}

void DirectInkPresenter::UI::Canvas::SetEraserPos(D2D1_POINT_2F point)
{
	m_point = point;

	Utils::ThrowIfFailed(
		Graphics::GetD2DFactory()->CreateRoundedRectangleGeometry(
			Graphics::RoundedRect(
				Graphics::RectF(
					m_point.x - 40.f,
					m_point.y - 80.f,
					m_point.x + 40.f,
					m_point.y + 80.f
				),
				10.f,
				10.f
			),
			&m_eraserGemetry
		)
	);
}

void DirectInkPresenter::UI::Canvas::OnContactEnter()
{
}

void DirectInkPresenter::UI::Canvas::OnContactMove()
{
	const auto& contactInfo = m_contactReceiver->GetCurrentContactInfo();
	D2D1_POINT_2F d2dPoint = Graphics::Point2F(
		Graphics::ConvertPixelsToDips(contactInfo->x),
		Graphics::ConvertPixelsToDips(contactInfo->y)
	);
	m_strokes[m_contactReceiver->GetCurrentContactID()]->Add(d2dPoint);

	CreateDeviceResources();
	if (!m_d2dHwndRenderTarget)
	{
		return;
	}

	{
		Utils::ComPtr<ID2D1SolidColorBrush> d2dBrush = nullptr;
		Utils::ComPtr<ID2D1SolidColorBrush> d2dEraserBrush = nullptr;
		//m_d2dHwndRenderTarget->BeginDraw();
		m_d2dHwndRenderTarget->CreateSolidColorBrush(UI::Graphics::ColorF(UI::Graphics::ColorF::Red), &d2dBrush);
		m_d2dHwndRenderTarget->CreateSolidColorBrush(UI::Graphics::ColorF(1, 1, 1, 0.5f), &d2dEraserBrush);
		/*m_d2dHwndRenderTarget->FillEllipse(
		    UI::Graphics::Ellipse(
		        UI::Graphics::Point2F(
		            contactInfo->x,
		            contactInfo->y
		        ),
				contactInfo->cxContact,
				contactInfo->cyContact
		    ), d2dBrush.Get()
		);*/
		/*m_d2dHwndRenderTarget->Clear();
		m_d2dHwndRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		m_d2dHwndRenderTarget->FillGeometry(
			m_strokes[m_contactReceiver->GetCurrentContactID()]->GetPathGeometry(),
			d2dBrush.Get()
		);*/
		/*m_d2dHwndRenderTarget->FillRoundedRectangle(
			UI::Graphics::RoundedRect(
				UI::Graphics::RectF(
					contactInfo->x - contactInfo->cxContact / 2,
					contactInfo->y - contactInfo->cyContact / 2,
					contactInfo->x + contactInfo->cxContact / 2,
					contactInfo->y + contactInfo->cyContact / 2
				),
				15.f,
				15.f
			),
			d2dEraserBrush.Get()
		);*/
		//m_d2dHwndRenderTarget->EndDraw();
	}
}

void DirectInkPresenter::UI::Canvas::OnContactDown()
{
	const auto& contactInfo = m_contactReceiver->GetCurrentContactInfo();
	D2D1_POINT_2F d2dPoint = Graphics::Point2F(
		Graphics::ConvertPixelsToDips(contactInfo->x),
		Graphics::ConvertPixelsToDips(contactInfo->y)
	);
	m_strokes[m_contactReceiver->GetCurrentContactID()] = m_strokeCollection->Create();
	m_strokes[m_contactReceiver->GetCurrentContactID()]->Add(d2dPoint);
}

void DirectInkPresenter::UI::Canvas::OnContactUp()
{
	const auto& contactInfo = m_contactReceiver->GetCurrentContactInfo();
	D2D1_POINT_2F d2dPoint = Graphics::Point2F(
		Graphics::ConvertPixelsToDips(contactInfo->x),
		Graphics::ConvertPixelsToDips(contactInfo->y)
	);
	m_strokes[m_contactReceiver->GetCurrentContactID()]->Add(d2dPoint);
	m_strokes.erase(m_contactReceiver->GetCurrentContactID());
}

void DirectInkPresenter::UI::Canvas::OnContactLeave()
{
}

void DirectInkPresenter::UI::Canvas::OnContactUpdated()
{
	m_strokeCollection->Commit();
}

void DirectInkPresenter::UI::Canvas::ProcessIdleEvent()
{
	CreateDeviceResources();
	if (!m_d2dHwndRenderTarget)
	{
		return;
	}

	Utils::ComPtr<ID2D1SolidColorBrush> d2dBrush = nullptr;
	m_d2dHwndRenderTarget->CreateSolidColorBrush(UI::Graphics::ColorF(UI::Graphics::ColorF::Red), &d2dBrush);
	m_d2dHwndRenderTarget->SetDpi(0, 0);
	m_d2dHwndRenderTarget->BeginDraw();
	m_d2dHwndRenderTarget->Clear();

	m_strokeCollection->Draw(m_d2dHwndRenderTarget.Get(), d2dBrush.Get());
	if (m_bShow)
	{
		Utils::ComPtr<ID2D1SolidColorBrush> d2dEraserBrush = nullptr;
		m_d2dHwndRenderTarget->CreateSolidColorBrush(UI::Graphics::ColorF(1, 1, 1, 0.5f), &d2dEraserBrush);
		m_d2dHwndRenderTarget->FillGeometry(m_eraserGemetry.Get(), d2dEraserBrush.Get());
	}

	HRESULT hr = m_d2dHwndRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
		CreateDeviceResources();
		UpdateDwmBlurBehind();
	}
	else
	{
		Utils::ThrowIfFailed(hr);
	}
}

void DirectInkPresenter::UI::Canvas::InitializeCreateParameters(CREATESTRUCT& cs)
{
	Window::InitializeCreateParameters(cs);
	//cs.dwExStyle |= WS_EX_LAYERED | WS_EX_PALETTEWINDOW;
}

LRESULT DirectInkPresenter::UI::Canvas::ProcessWindowMessages(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = TRUE;
	switch (uMsg)
	{
		case WM_CREATE:
		{
			const auto& pcs = (LPCREATESTRUCT)lParam;
			pcs->cx = lround(Graphics::GetDpiScailingForHwnd(GetHwnd()) * (float)pcs->cx);
			pcs->cy = lround(Graphics::GetDpiScailingForHwnd(GetHwnd()) * (float)pcs->cy);

			m_contactReceiver = Utils::make_unique<Ink::ContactReceiver>(GetHwnd(), this);
			m_strokeCollection = Utils::make_unique<Ink::StrokeCollection>(Graphics::GetD2DFactory());

			CreateDeviceResources();
			Graphics::GetD2DFactory()->CreateStrokeStyle(
			    Graphics::StrokeStyleProperties(
			        D2D1_CAP_STYLE_ROUND,
			        D2D1_CAP_STYLE_ROUND,
			        D2D1_CAP_STYLE_ROUND,
					D2D1_LINE_JOIN_ROUND,
					10.0f,
					D2D1_DASH_STYLE_SOLID,
					0.0f
			    ),
			    nullptr,
			    0,
			    &m_d2dStrokeStyle
			);
			Graphics::SetContextDpi(GetHwnd());

			UpdateDwmBlurBehind();
			break;
		}
		case WM_KEYDOWN:
		{
			if (wParam == 'Z')
			{
				m_strokeCollection->Undo();
			}
			if (wParam == 'R')
			{
				m_strokeCollection->Redo();
			}
			if (wParam == 'E')
			{
				POINT pt = {};
				GetCursorPos(&pt);
				ScreenToClient(GetHwnd(), &pt);
				D2D1_POINT_2F d2dPoint = Graphics::Point2F(
					Graphics::ConvertPixelsToDips(pt.x),
					Graphics::ConvertPixelsToDips(pt.y)
				);

				SetEraserPos(d2dPoint);
				ShowEraser(true);

				m_strokeCollection->Erase(m_eraserGemetry.Get());
				m_strokeCollection->Commit();
			}
			break;
		}
		case WM_KEYUP:
		{
			if (wParam == 'E')
			{
				SetEraserPos({});
				ShowEraser(false);
			}
			break;
		}

		case WM_DWMCOMPOSITIONCHANGED:
		{
			UpdateDwmBlurBehind();
			break;
		}


		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_MOUSEMOVE:
		case WM_TOUCH:
		{
			m_contactReceiver->ProcessInputEvents(uMsg, wParam, lParam);
			break;
		}
		case WM_PAINT:
		{
			ValidateRect(GetHwnd(), nullptr);
			break;
		}
		case WM_SIZE:
		{
			if (wParam != SIZE_MINIMIZED && m_d2dHwndRenderTarget)
			{
				HRESULT hr = m_d2dHwndRenderTarget->Resize(
				    UI::Graphics::SizeU(
				        LOWORD(lParam), HIWORD(lParam)
				    )
				);
				if (hr == D2DERR_RECREATE_TARGET)
				{
					DiscardDeviceResources();
				}
				else
				{
					Utils::ThrowIfFailed(hr);
				}
			}
			break;
		}
		default:
			return DefWindowProc(Window::GetHwnd(), uMsg, wParam, lParam);
	}
	return 0;
}

