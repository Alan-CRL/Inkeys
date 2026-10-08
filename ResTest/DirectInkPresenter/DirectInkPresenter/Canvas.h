#pragma once
#include "pch.h"
#include "Utils.h"
#include "Window.h"
#include "Graphics.h"
#include "ContactReceiver.h"
#include "StrokeCollection.h"

namespace DirectInkPresenter
{
	namespace UI
	{
		class Canvas : Window, public Ink::IContactNotify
		{
		public:
			friend class Window;

			void SetPenColor(D2D1_COLOR_F d2dColor);
			void SetPenWidth(FLOAT fWidth);
			void SetDashStyle(D2D1_DASH_STYLE d2dDash);
			void SetOffset(FLOAT fOffsetX, FLOAT fOffsetY);
			void ShowEraser(bool bShow);
			void SetEraserPos(D2D1_POINT_2F point);
		private:
			void OnContactEnter();
			void OnContactMove();
			void OnContactDown();
			void OnContactUp();
			void OnContactLeave();
			void OnContactUpdated();
			void CreateDeviceResources();
			void DiscardDeviceResources();
			void UpdateDwmBlurBehind();

			virtual void ProcessIdleEvent();
			virtual void InitializeCreateParameters(CREATESTRUCT& cs);
			virtual LRESULT ProcessWindowMessages(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

			bool m_bShow = false;
			D2D1_POINT_2F m_point = {};
			Utils::ComPtr<ID2D1RoundedRectangleGeometry> m_eraserGemetry = nullptr;

			Utils::unique_ptr<Ink::ContactReceiver> m_contactReceiver;
			Utils::unique_ptr<Ink::StrokeCollection> m_strokeCollection;
			Utils::ComPtr<ID2D1HwndRenderTarget> m_d2dHwndRenderTarget = nullptr;
			Utils::ComPtr<ID2D1StrokeStyle> m_d2dStrokeStyle = nullptr;
			Utils::unordered_map<DWORD, Ink::Stroke*> m_strokes;
		};
	}
}
