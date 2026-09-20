#include "pch.h"
#include "Graphics.h"

static float g_dpi = 96.f;
static DirectInkPresenter::Utils::ComPtr<ID2D1Factory> g_d2dFactory = nullptr;
static DirectInkPresenter::Utils::ComPtr<IDWriteFactory> g_dwriteFactory = nullptr;
static DirectInkPresenter::Utils::ComPtr<IWICImagingFactory> g_wicFactory = nullptr;

ID2D1Factory* DirectInkPresenter::UI::Graphics::GetD2DFactory() { return g_d2dFactory.Get(); }
IDWriteFactory* DirectInkPresenter::UI::Graphics::GetDwriteFactory() { return g_dwriteFactory.Get(); }
IWICImagingFactory* DirectInkPresenter::UI::Graphics::GetWicFactory() { return g_wicFactory.Get(); }

void DirectInkPresenter::UI::Graphics::CreateTargetForHwnd(HWND hWnd, ID2D1HwndRenderTarget** d2dHwndRenderTarget)
{
	if (!IsWindow(hWnd))
	{
		return;
	}

	RECT rcClient = {};
	GetClientRect(hWnd, &rcClient);

	Utils::ThrowIfFailed(
		GetD2DFactory()->CreateHwndRenderTarget(
			DirectInkPresenter::UI::Graphics::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
			DirectInkPresenter::UI::Graphics::HwndRenderTargetProperties(
				hWnd, DirectInkPresenter::UI::Graphics::SizeU(
					rcClient.right, rcClient.bottom
				), 
				D2D1_PRESENT_OPTIONS_IMMEDIATELY | D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
			),
			d2dHwndRenderTarget
		)
	);
}

void DirectInkPresenter::UI::Graphics::Initialize()
{
	Utils::ThrowIfFailed(
		D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED,
			IID_PPV_ARGS(&g_d2dFactory)
		)
	);
	Utils::ThrowIfFailed(
		DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(g_dwriteFactory.GetAddressOf())
		)
	);
	Utils::ThrowIfFailed(
		CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&g_wicFactory)
		)
	);
	static const auto pfnSetProcessDpiAwarenessContext = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
		GetProcAddress(GetModuleHandle(TEXT("User32")), "SetProcessDpiAwarenessContext")
	);
	if (pfnSetProcessDpiAwarenessContext)
	{
		pfnSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	}
	else
	{
		SetProcessDPIAware();
	}
}

FLOAT DirectInkPresenter::UI::Graphics::GetDpiScailingForHwnd(HWND hWnd)
{
	float dpiX = 0, dpiY = 0;
	GetDpiForHwnd(hWnd, dpiX, dpiY);
	return float(min(dpiX, dpiY)) / 96.f;
}

void DirectInkPresenter::UI::Graphics::GetDpiForHwnd(HWND hWnd, float& fDpiX, float& fDpiY)
{
	static const auto& pfnGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(GetModuleHandle(_T("User32")), "GetDpiForWindow");
	if (pfnGetDpiForWindow)
	{
		UINT dpi = pfnGetDpiForWindow(hWnd);
		fDpiX = (float)dpi;
		fDpiY = (float)dpi;
		return;
	}
	else
	{
		HMODULE hModule = LoadLibrary(_T("Shcore"));
		if (hModule)
		{
			const auto& pfnGetDpiForMonitor = (HRESULT(WINAPI*)(HMONITOR, DWORD, UINT*, UINT*))GetProcAddress(hModule, "GetDpiForMonitor");
			if (pfnGetDpiForMonitor)
			{
				HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
				Utils::ThrowIfFailed(pfnGetDpiForMonitor(hMonitor, 0, (UINT*)&fDpiX, (UINT*)&fDpiY));
				FreeLibrary(hModule);
				return;
			}
			FreeLibrary(hModule);
		}
	}
	HDC hdc = GetDC(hWnd);
	fDpiX = (float)GetDeviceCaps(hdc, LOGPIXELSX);
	fDpiY = (float)GetDeviceCaps(hdc, LOGPIXELSY);
	ReleaseDC(hWnd, hdc);
}

// ��ʹ�����豸�޹ص�����(DIP)��ʾ�ĳ���ת��Ϊʹ���������ر�ʾ�ĳ��ȡ�
LONG DirectInkPresenter::UI::Graphics::ConvertDipsToPixels(float dips, float dpi)
{
	if (dpi == -1.f)
	{
		dpi = g_dpi;
	}
	return lround(dips * (dpi / 96.f));
}

// ��ʹ���������ر�ʾ�ĳ���ת��Ϊʹ�����豸�޹ص�����(DIP)��ʾ�ĳ��ȡ�
float DirectInkPresenter::UI::Graphics::ConvertPixelsToDips(UINT pixels, float dpi)
{
	if (dpi == -1.f)
	{
		dpi = g_dpi;
	}
	return ceil((float)pixels / (dpi / 96.f));
}

void DirectInkPresenter::UI::Graphics::SetContextDpi(float dpi)
{
	g_dpi = dpi;
}

void DirectInkPresenter::UI::Graphics::SetContextDpi(HWND hWnd)
{
	GetDpiForHwnd(hWnd, g_dpi, g_dpi);
}

