#pragma once
#include "pch.h"
#include "Utils.h"

namespace DirectInkPresenter
{
	namespace UI
	{
		class Window
		{
		public:
			HWND GetHwnd() const { return m_hWnd; }

			static void Initialize(HINSTANCE hInstance);
			static int Run();
			template <typename T, typename... Args>
			static T const* Create(Args&&... args)
			{
				CREATESTRUCT cs = {};
				T* ptr = new T(args...);
				if (ptr)
				{
					dynamic_cast<Window*>(ptr)->InitializeCreateParameters(cs);
					HWND hWnd = CreateWindowEx(
						cs.dwExStyle,
						cs.lpszClass,
						cs.lpszName,
						cs.style,
						cs.x,
						cs.y,
						cs.cx,
						cs.cy,
						cs.hwndParent,
						cs.hMenu,
						cs.hInstance,
						cs.lpCreateParams
					);
					if (!hWnd)
					{
						delete ptr;
						Utils::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
					}
				}
				return ptr;
			}
		protected:
			Window() = default;
			virtual ~Window() = default;

			virtual void InitializeCreateParameters(CREATESTRUCT& cs);
			virtual LRESULT ProcessWindowMessages(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) { return DefWindowProc(GetHwnd(), uMsg, wParam, lParam); }
			virtual void ProcessIdleEvent() {}
		private:
			static Utils::list<Window*> g_windowManager;
			static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		
			HWND m_hWnd = nullptr;
		};
	}
}
