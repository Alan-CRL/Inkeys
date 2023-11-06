#include "IdtMagnification.h"

IMAGE MagnificationBackground = CreateImageColor(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), RGBA(255, 255, 255, 255), false);
HWND hwndHost, hwndMag;
int magnificationWindowReady;

shared_mutex MagnificationBackgroundSm;
RECT magWindowRect;
RECT hostWindowRect;

HINSTANCE hInst;
BOOL MagImageScaling(HWND hwnd, void* srcdata, MAGIMAGEHEADER srcheader, void* destdata, MAGIMAGEHEADER destheader, RECT unclipped, RECT clipped, HRGN dirty)
{
	BITMAPINFOHEADER bmif;
	HBITMAP hBmp = NULL;
	BYTE* pBits = nullptr;

	bmif.biSize = sizeof(BITMAPINFOHEADER);
	bmif.biHeight = srcheader.height;
	bmif.biWidth = srcheader.width;
	bmif.biSizeImage = bmif.biWidth * bmif.biHeight * 4;
	bmif.biPlanes = 1;
	bmif.biBitCount = (WORD)(bmif.biSizeImage / bmif.biHeight / bmif.biWidth * 8);
	bmif.biCompression = BI_RGB;

	LPBYTE pData = (BYTE*)new BYTE[bmif.biSizeImage];
	memcpy(pData, (LPBYTE)srcdata + srcheader.offset, bmif.biSizeImage);
	LONG nLineSize = bmif.biWidth * bmif.biBitCount / 8;
	BYTE* pLineData = new BYTE[nLineSize];
	LONG nLineStartIndex = 0;
	LONG nLineEndIndex = bmif.biHeight - 1;
	while (nLineStartIndex < nLineEndIndex)
	{
		BYTE* pStart = pData + (nLineStartIndex * nLineSize);
		BYTE* pEnd = pData + (nLineEndIndex * nLineSize);
		memcpy(pLineData, pStart, nLineSize);
		memcpy(pStart, pEnd, nLineSize);
		memcpy(pEnd, pLineData, nLineSize);
		nLineStartIndex++;
		nLineEndIndex--;
	}

	// 使用CreateDIBSection来创建HBITMAP
	HDC hDC = GetDC(NULL);
	hBmp = CreateDIBSection(hDC, (BITMAPINFO*)&bmif, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
	ReleaseDC(NULL, hDC);

	if (hBmp && pBits)
	{
		memcpy(pBits, pData, bmif.biSizeImage);
		MagnificationBackground = Bitmap2Image(&hBmp, false);
	}

	delete[] pLineData;
	delete[] pData;
	DeleteObject(hBmp);

	return 1;
}
void UpdateMagWindow()
{
	POINT mousePoint;
	GetCursorPos(&mousePoint);

	RECT sourceRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

	// Set the source rectangle for the magnifier control.
	std::unique_lock<std::shared_mutex> lock1(MagnificationBackgroundSm);
	MagSetWindowSource(hwndMag, sourceRect);
	lock1.unlock();

	// Reclaim topmost status, to prevent unmagnified menus from remaining in view.
	SetWindowPos(hwndHost, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

	// Force redraw.
	InvalidateRect(hwndMag, NULL, TRUE);
}

LRESULT CALLBACK MagnifierWindowWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hWnd, message, wParam, lParam);
}
ATOM RegisterHostWindowClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex = {};

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = MagnifierWindowWndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wcex.lpszClassName = TEXT("MagnifierWindow");

	return RegisterClassEx(&wcex);
}
BOOL SetupMagnifier(HINSTANCE hinst)
{
	hostWindowRect.top = 0;
	hostWindowRect.bottom = GetSystemMetrics(SM_CYSCREEN);
	hostWindowRect.left = 0;
	hostWindowRect.right = GetSystemMetrics(SM_CXSCREEN);

	RegisterHostWindowClass(hinst);
	hwndHost = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED, TEXT("MagnifierWindow"), TEXT("Screen Magnifier Sample"), RESTOREDWINDOWSTYLES, 0, 0, hostWindowRect.right, hostWindowRect.bottom, NULL, NULL, hInst, NULL);
	if (!hwndHost) return FALSE;

	SetLayeredWindowAttributes(hwndHost, 0, 255, LWA_ALPHA);

	GetClientRect(hwndHost, &magWindowRect);
	hwndMag = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"), WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE, magWindowRect.left, magWindowRect.top, magWindowRect.right, magWindowRect.bottom, hwndHost, NULL, hInst, NULL);
	if (!hwndMag) return FALSE;

	SetWindowLong(hwndMag, GWL_STYLE, GetWindowLong(hwndMag, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
	SetWindowPos(hwndMag, NULL, hostWindowRect.left, hostWindowRect.top, hostWindowRect.right - hostWindowRect.left, hostWindowRect.bottom - hostWindowRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
	SetWindowLong(hwndMag, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

	// Set the magnification factor.
	MAGTRANSFORM matrix;
	memset(&matrix, 0, sizeof(matrix));
	matrix.v[0][0] = 1.0f;
	matrix.v[1][1] = 1.0f;
	matrix.v[2][2] = 1.0f;

	BOOL ret = MagSetWindowTransform(hwndMag, &matrix);
	if (ret)
	{
		MAGCOLOREFFECT magEffectInvert =
		{ { // MagEffectInvert
			{  1.0f,  0.0f,  0.0f,  0.0f,  0.0f },
			{  0.0f,  1.0f,  0.0f,  0.0f,  0.0f },
			{  0.0f,  0.0f,  1.0f,  0.0f,  0.0f },
			{  0.0f,  0.0f,  0.0f,  1.0f,  0.0f },
			{  1.0f,  1.0f,  1.0f,  0.0f,  1.0f }
		} };

		ret = MagSetColorEffect(hwndMag, NULL);
	}

	MagSetImageScalingCallback(hwndMag, (MagImageScalingCallback)MagImageScaling);

	return ret;
}

bool RequestUpdateMagWindow;

void MagnifierThread()
{
	//LOG(INFO) << "尝试初始化MagnificationAPI";
	MagInitialize();
	//LOG(INFO) << "成功初始化MagnificationAPI";

	//LOG(INFO) << "尝试创建MagnificationAPI虚拟窗口";
	SetupMagnifier(GetModuleHandle(0));
	ShowWindow(hwndHost, SW_HIDE);
	UpdateWindow(hwndHost);
	//LOG(INFO) << "成功创建MagnificationAPI虚拟窗口";

	while (!off_signal)
	{
		if (magnificationWindowReady >= 4)
		{
			std::vector<HWND> hwndList;
			hwndList.emplace_back(floating_window);
			hwndList.emplace_back(drawpad_window);
			hwndList.emplace_back(ppt_window);
			hwndList.emplace_back(test_window);
			MagSetWindowFilterList(hwndMag, MW_FILTERMODE_EXCLUDE, hwndList.size(), hwndList.data());

			magnificationWindowReady = -1;
			//LOG(INFO) << "成功MagnificationAPI刷新第一帧";

			break;
		}

		Sleep(500);
	}

	RequestUpdateMagWindow = true;
	while (!off_signal)
	{
		//hiex::DelayFPS(24);
		while (!RequestUpdateMagWindow) Sleep(100);

		UpdateMagWindow();
		RequestUpdateMagWindow = false;
	}

	//LOG(INFO) << "尝试释放MagnificationAPI";
	MagUninitialize();
	//LOG(INFO) << "成功释放MagnificationAPI";
}