#include "IdtRts.h"

int uRealTimeStylus;

bool touchDown = false;   // 表示触摸设备是否被按下
int touchNum = 0;         // 触摸点的点击个数

unordered_map<LONG, double> TouchSpeed;
unordered_map<LONG, TouchMode> TouchPos;
vector<LONG> TouchList;

deque<TouchInfo> TouchTemp;

LONG TouchCnt = 0;
unordered_map<LONG, LONG> TouchPointer;
shared_mutex PointPosSm, TouchSpeedSm, PointListSm, PointTempSm;

IRealTimeStylus* g_pRealTimeStylus = NULL;
IStylusSyncPlugin* g_pSyncEventHandlerRTS = NULL;

IRealTimeStylus* CreateRealTimeStylus(HWND hWnd)
{
	// Check input argument
	if (hWnd == NULL)
	{
		//ASSERT(hWnd && L"CreateRealTimeStylus: invalid argument hWnd");
		return NULL;
	}

	// Create RTS object
	IRealTimeStylus* pRealTimeStylus = NULL;
	HRESULT hr = CoCreateInstance(CLSID_RealTimeStylus, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pRealTimeStylus));
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"CreateRealTimeStylus: failed to CoCreateInstance of RealTimeStylus");
		return NULL;
	}

	// Attach RTS object to a window
	hr = pRealTimeStylus->put_HWND((HANDLE_PTR)hWnd);
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"CreateRealTimeStylus: failed to set window handle");
		pRealTimeStylus->Release();
		return NULL;
	}

	// Register RTS object for receiving multi-touch input.
	IRealTimeStylus3* pRealTimeStylus3 = NULL;
	hr = pRealTimeStylus->QueryInterface(&pRealTimeStylus3);
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"CreateRealTimeStylus: cannot access IRealTimeStylus3");
		pRealTimeStylus->Release();
		return NULL;
	}
	hr = pRealTimeStylus3->put_MultiTouchEnabled(TRUE);
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"CreateRealTimeStylus: failed to enable multi-touch");
		pRealTimeStylus->Release();
		pRealTimeStylus3->Release();
		return NULL;
	}
	pRealTimeStylus3->Release();

	return pRealTimeStylus;
}
bool EnableRealTimeStylus(IRealTimeStylus* pRealTimeStylus)
{
	// Check input arguments
	if (pRealTimeStylus == NULL)
	{
		//ASSERT(pRealTimeStylus && L"EnableRealTimeStylus: invalid argument RealTimeStylus");
		return NULL;
	}

	// Enable RTS object
	HRESULT hr = pRealTimeStylus->put_Enabled(TRUE);
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"EnableRealTimeStylus: failed to enable RealTimeStylus");
		return false;
	}

	return true;
}

void RTSSpeed()
{
	int x, y;
	int lastx = -1, lasty = -1;

	while (!off_signal)
	{
		for (int i = 0; i < touchNum; i++)
		{
			std::shared_lock<std::shared_mutex> lock1(PointPosSm);
			x = TouchPos[TouchList[i]].pt.x;
			y = TouchPos[TouchList[i]].pt.y;
			lock1.unlock();

			if (lastx == -1 && lasty == -1) lastx = x, lasty = y;

			double speed = (TouchSpeed[TouchList[i]] + sqrt(pow(x - lastx, 2) + pow(y - lasty, 2))) / 2;
			std::unique_lock<std::shared_mutex> lock2(TouchSpeedSm);
			TouchSpeed[TouchList[i]] = speed;
			lock2.unlock();

			lastx = x, lasty = y;
		}

		hiex::DelayFPS(20);
	}
}