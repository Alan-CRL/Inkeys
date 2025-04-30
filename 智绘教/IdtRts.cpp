#include "IdtRts.h"

#include "IdtDrawpad.h"

IdtAtomic<bool> rtsDown;												// 表示触摸设备是否被按下
IdtAtomic<int> rtsNum = 0, touchNum = 0, inkNum = 0;					// 点、触摸点、触控笔的点击个数

unordered_map<LONG, pair<int, int>> PreviousPointPosition;				//用于速度计算
unordered_map<LONG, double> TouchSpeed;

unordered_map<LONG, TouchMode> TouchPos;
deque<TouchInfo> TouchTemp;
vector<LONG> TouchList;

LONG TouchCnt = 0;
unordered_map<LONG, LONG> TouchPointer;

shared_mutex touchPosSm, touchSpeedSm, pointListSm, touchTempSm, touchPointerSm;

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

	// 禁用 win7 轻拂手势
	IRealTimeStylus2* pRealTimeStylus2 = NULL;
	hr = pRealTimeStylus->QueryInterface(&pRealTimeStylus2);
	if (FAILED(hr))
	{
		pRealTimeStylus->Release();
		return NULL;
	}
	hr = pRealTimeStylus2->put_FlicksEnabled(FALSE);
	if (FAILED(hr))
	{
		pRealTimeStylus->Release();
		pRealTimeStylus2->Release();
		return NULL;
	}
	pRealTimeStylus2->Release();

	// 注册RTS对象以接收多点触摸输入。
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

IStylusSyncPlugin* CSyncEventHandlerRTS::Create(IRealTimeStylus* pRealTimeStylus)
{
	// Check input argument
	if (pRealTimeStylus == NULL)
	{
		//ASSERT(pRealTimeStylus != NULL && L"CSyncEventHandlerRTS::Create: invalid argument RealTimeStylus");
		return NULL;
	}

	// Instantiate CSyncEventHandlerRTS object
	CSyncEventHandlerRTS* pSyncEventHandlerRTS = new CSyncEventHandlerRTS();
	if (pSyncEventHandlerRTS == NULL)
	{
		//ASSERT(pSyncEventHandlerRTS != NULL && L"CSyncEventHandlerRTS::Create: cannot create instance of CSyncEventHandlerRTS");
		return NULL;
	}

	// Create free-threaded marshaller for this object and aggregate it.
	HRESULT hr = CoCreateFreeThreadedMarshaler(pSyncEventHandlerRTS, &pSyncEventHandlerRTS->m_punkFTMarshaller);
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"CSyncEventHandlerRTS::Create: cannot create free-threaded marshaller");
		pSyncEventHandlerRTS->Release();
		return NULL;
	}

	// Add CSyncEventHandlerRTS object to the list of synchronous plugins in the RTS object.
	hr = pRealTimeStylus->AddStylusSyncPlugin(
		0,                      // insert plugin at position 0 in the sync plugin list
		pSyncEventHandlerRTS);  // plugin to be inserted - event handler CSyncEventHandlerRTS
	if (FAILED(hr))
	{
		//ASSERT(SUCCEEDED(hr) && L"CEventHandlerRTS::Create: failed to add CSyncEventHandlerRTS to the RealTimeStylus plugins");
		pSyncEventHandlerRTS->Release();
		return NULL;
	}

	return pSyncEventHandlerRTS;
}
HRESULT CSyncEventHandlerRTS::StylusDown(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG /*cPktCount*/, LONG* pPacket, LONG** /*ppInOutPkts*/)
{
	// 这是一个按下状态
	TouchMode mode{};
	TouchInfo info{};

	ULONG ulPacketProperties;
	PACKET_PROPERTY* pPacketProperties;
	{
		ULONG ulTcidCount;
		TABLET_CONTEXT_ID* pTcids;
		piRtsSrc->GetAllTabletContextIds(&ulTcidCount, &pTcids);
		piRtsSrc->GetPacketDescriptionData(pTcids[0], &mode.inkToDeviceScaleX, &mode.inkToDeviceScaleY, &ulPacketProperties, &pPacketProperties);
		CoTaskMemFree(pTcids);
	}
	piRtsSrc->GetPacketDescriptionData(pStylusInfo->tcid, nullptr, nullptr, &ulPacketProperties, &pPacketProperties);

	// 获取数据包信息
	for (int i = 0; i < ulPacketProperties; i++)
	{
		GUID guid = pPacketProperties[i].guid;
		if (guid == GUID_PACKETPROPERTY_GUID_X) mode.pt.x = LONG(pPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_Y) mode.pt.y = LONG(pPacket[i] * mode.inkToDeviceScaleY + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE)
		{
			mode.logicalMin = pPacketProperties[i].PropertyMetrics.nLogicalMin;
			mode.logicalMax = pPacketProperties[i].PropertyMetrics.nLogicalMax;

			mode.pressure = double(pPacket[i] - mode.logicalMin) / double(mode.logicalMax - mode.logicalMin);
		}
		else if (guid == GUID_PACKETPROPERTY_GUID_WIDTH) mode.touchWidth = LONG(pPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_HEIGHT) mode.touchHeight = LONG(pPacket[i] * mode.inkToDeviceScaleY + 0.5);
	}
	CoTaskMemFree(pPacketProperties);
	// 获取设备类型
	int deviceType = 0;
	{
		IInkTablet* piTablet = NULL;
		piRtsSrc->GetTabletFromTabletContextId(pStylusInfo->tcid, &piTablet);

		IInkTablet2* piTablet2 = NULL;
		piTablet->QueryInterface(&piTablet2);

		TabletDeviceKind temp;
		piTablet2->get_DeviceKind(&temp);

		{
			if (temp == TabletDeviceKind::TDK_Touch) mode.type = deviceType = 0;
			else if (temp == TabletDeviceKind::TDK_Pen) mode.type = deviceType = 1;
			else
			{
				if (KeyBoradDown[VK_RBUTTON] && !KeyBoradDown[VK_LBUTTON]) mode.type = deviceType = 3;
				else mode.type = deviceType = 2;
			}
		}

		piTablet2->Release();
		piTablet->Release();
	}
	mode.isInvertedCursor = pStylusInfo->bIsInvertedCursor;

	unique_lock<shared_mutex> lockTouchPointer(touchPointerSm);
	TouchPointer[pStylusInfo->cid] = ++TouchCnt;
	info.pid = TouchCnt;
	lockTouchPointer.unlock();

	std::unique_lock<std::shared_mutex> lock1(touchPosSm);
	TouchPos[TouchCnt] = mode;
	lock1.unlock();

	std::unique_lock<std::shared_mutex> lock2(touchSpeedSm);
	TouchSpeed[TouchCnt] = 0;
	PreviousPointPosition[TouchCnt].first = PreviousPointPosition[TouchCnt].second = -1;
	lock2.unlock();

	std::unique_lock<std::shared_mutex> lock3(pointListSm);
	TouchList.push_back(TouchCnt);
	lock3.unlock();

	info.mode = mode;

	std::unique_lock<std::shared_mutex> lock4(touchTempSm);
	TouchTemp.push_back(info);
	lock4.unlock();

	rtsNum++, rtsDown = true;
	if (deviceType == 0) touchNum++;
	if (deviceType == 1) inkNum++;
	TouchCnt %= 100000;

	return S_OK;
}
HRESULT CSyncEventHandlerRTS::StylusUp(IRealTimeStylus*, const StylusInfo* pStylusInfo, ULONG /*cPktCount*/, LONG* pPacket, LONG** /*ppInOutPkts*/)
{
	// 这是一个抬起状态

	rtsNum = max(0, rtsNum - 1);
	if (rtsNum == 0) rtsDown = false;

	int pid = TouchPointer[pStylusInfo->cid];

	auto it = std::find(TouchList.begin(), TouchList.end(), pid);
	if (it != TouchList.end())
	{
		shared_lock<shared_mutex> lockPointPosSm(touchPosSm);
		{
			if (TouchPos.find(pid) != TouchPos.end())
			{
				if (TouchPos[pid].type == 0)
					touchNum = max(0, touchNum - 1);
				if (TouchPos[pid].type == 1)
					inkNum = max(0, inkNum - 1);
			}
		}
		lockPointPosSm.unlock();

		unique_lock<shared_mutex> lockPointListSm(pointListSm);
		TouchList.erase(it);
		TouchPointer.erase(pStylusInfo->cid);
		lockPointListSm.unlock();
	}

	if (rtsNum == 0)
	{
		unique_lock<shared_mutex> lockTouchPosSm(touchPosSm);
		TouchPos.clear();
		lockTouchPosSm.unlock();

		touchNum = 0;
		inkNum = 0;
	}

	return S_OK;
}
HRESULT CSyncEventHandlerRTS::Packets(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG /*cPktCount*/, ULONG /*cPktBuffLength*/, LONG* pPacket, ULONG* /*pcInOutPkts*/, LONG** /*ppInOutPkts*/)
{
	// 这是一个移动状态

	ULONG ulPacketProperties;
	PACKET_PROPERTY* pPacketProperties;
	piRtsSrc->GetPacketDescriptionData(pStylusInfo->tcid, nullptr, nullptr, &ulPacketProperties, &pPacketProperties);

	shared_lock<shared_mutex> lockTouchPointer(touchPointerSm);
	int pid = TouchPointer[pStylusInfo->cid];
	lockTouchPointer.unlock();

	shared_lock<shared_mutex> lock1(touchPosSm);
	TouchMode mode = TouchPos[pid];
	lock1.unlock();
	for (int i = 0; i < ulPacketProperties; i++)
	{
		GUID guid = pPacketProperties[i].guid;
		if (guid == GUID_PACKETPROPERTY_GUID_X) mode.pt.x = LONG(pPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_Y) mode.pt.y = LONG(pPacket[i] * mode.inkToDeviceScaleY + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE) mode.pressure = double(pPacket[i] - mode.logicalMin) / double(mode.logicalMax - mode.logicalMin);
		else if (guid == GUID_PACKETPROPERTY_GUID_WIDTH) mode.touchWidth = LONG(pPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_HEIGHT) mode.touchHeight = LONG(pPacket[i] * mode.inkToDeviceScaleY + 0.5);
	}
	CoTaskMemFree(pPacketProperties);

	unique_lock<shared_mutex> lock2(touchPosSm);
	TouchPos[pid] = mode;
	lock2.unlock();

	return S_OK;
}
HRESULT CSyncEventHandlerRTS::DataInterest(RealTimeStylusDataInterest* pDataInterest)
{
	*pDataInterest = (RealTimeStylusDataInterest)(RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp);

	return S_OK;
}

void RTSSpeed()
{
	int x, y;
	double speed;

	clock_t tRecord = 0;
	while (!offSignal)
	{
		for (int i = 0; i < rtsNum; i++)
		{
			std::shared_lock<std::shared_mutex> lock1(touchPosSm);
			x = TouchPos[TouchList[i]].pt.x;
			y = TouchPos[TouchList[i]].pt.y;
			lock1.unlock();

			std::shared_lock<std::shared_mutex> lock2(touchSpeedSm);
			if (PreviousPointPosition[TouchList[i]].first == -1 && PreviousPointPosition[TouchList[i]].second == -1) PreviousPointPosition[TouchList[i]].first = x, PreviousPointPosition[TouchList[i]].second = y, speed = 1;
			else speed = (TouchSpeed[TouchList[i]] + sqrt(pow(x - PreviousPointPosition[TouchList[i]].first, 2) + pow(y - PreviousPointPosition[TouchList[i]].second, 2))) / 2;
			lock2.unlock();

			std::unique_lock<std::shared_mutex> lock3(touchSpeedSm);
			TouchSpeed[TouchList[i]] = speed;
			lock3.unlock();

			PreviousPointPosition[TouchList[i]].first = x, PreviousPointPosition[TouchList[i]].second = y;
		}

		if (tRecord)
		{
			int delay = 1000 / 20 - (clock() - tRecord);
			if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
		tRecord = clock();
	}
}