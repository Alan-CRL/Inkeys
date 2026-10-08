#include "pch.h"
#include "Window.h"
#include "resource.h"
#define DirectInkPresenter_UI_Window_Class _T("DirectInkPresenter::UI::Window")

HINSTANCE g_hInst = nullptr;
DirectInkPresenter::Utils::list<DirectInkPresenter::UI::Window*> DirectInkPresenter::UI::Window::g_windowManager = {};

void DirectInkPresenter::UI::Window::Initialize(HINSTANCE hInstance)
{
	WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
	g_hInst = hInstance;
	wcex.hInstance = g_hInst;
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.lpszClassName = DirectInkPresenter_UI_Window_Class;
	wcex.hbrBackground = nullptr;
	wcex.hIcon = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_DIRECTINKPRESENTER));
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_DIRECTINKPRESENTER));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	Utils::ThrowIfFailed(
		RegisterClassEx(&wcex) ? S_OK : HRESULT_FROM_WIN32(GetLastError())
	);
}

int DirectInkPresenter::UI::Window::Run()
{
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if (g_windowManager.size())
			{
				for (const auto& i : g_windowManager)
				{
					i->ProcessIdleEvent();
				}
				WaitMessage();
			}
			else
			{
				PostQuitMessage(0);
			}
		}
	}
	return int(msg.wParam);
}

void DirectInkPresenter::UI::Window::InitializeCreateParameters(CREATESTRUCT& cs)
{
	cs =
	{
		this,
		g_hInst,
		nullptr,
		HWND_DESKTOP,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		DirectInkPresenter_UI_Window_Class,
		DirectInkPresenter_UI_Window_Class,
		0
	};
}

LRESULT DirectInkPresenter::UI::Window::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Window* window = nullptr;

	if (uMsg == WM_NCCREATE)
	{
		LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
		window = (Window*)pcs->lpCreateParams;
		window->m_hWnd = hWnd;
		g_windowManager.push_back(window);
		SetWindowLongPtrW(
		    hWnd,
		    GWLP_USERDATA,
		    reinterpret_cast<LONG_PTR>(window)
		);
	}
	else
	{
		window = reinterpret_cast<Window*>(
		          static_cast<LONG_PTR>(
		              GetWindowLongPtrW(
		                  hWnd,
		                  GWLP_USERDATA
		              )
		          )
		      );
	}

	LRESULT lr = 0;
	BOOL bHandled = FALSE;
	if (window)
	{
		lr = window->ProcessWindowMessages(uMsg, wParam, lParam, bHandled);
	}

	if (uMsg == WM_NCDESTROY)
	{
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
		g_windowManager.remove(window);
		delete window;
		window = nullptr;
	}
	if (bHandled)
	{
		return lr;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
