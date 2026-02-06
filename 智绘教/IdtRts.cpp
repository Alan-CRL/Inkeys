#include "IdtRts.h"

#include "IdtConfiguration.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtWindow.h"
#include "Inkeys/Other/IdtInputs.h"

IdtAtomic<bool> rtsDown;												// 表示触摸设备是否被按下
IdtAtomic<int> rtsNum = 0, touchNum = 0, inkNum = 0;					// 点、触摸点、触控笔的点击个数

unordered_map<LONG, pair<int, int>> PreviousPointPosition;				//用于速度计算
unordered_map<LONG, double> TouchSpeed;

unordered_map<LONG, TouchMode> TouchPos;
deque<TouchInfo> TouchTemp;
vector<LONG> TouchList;

IdtAtomic<unsigned short> TouchCnt = 0;
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
	if (SUCCEEDED(hr))
	{
		hr = pRealTimeStylus2->put_FlicksEnabled(FALSE);
		if (FAILED(hr))
		{
			IDTLogger->warn("[CreateRealTimeStylus] 无法禁用轻拂手势 (put_FlicksEnabled 失败)，将保持默认设置。");
		}
		pRealTimeStylus2->Release(); // 用完必须释放接口
	}
	else
	{
		// 获取接口失败（可能系统版本过低），不影响核心功能，仅日志
		IDTLogger->info("[CreateRealTimeStylus] 当前系统不支持 IRealTimeStylus2 (轻拂控制)，已跳过。");
	}

	// 注册RTS对象以接收多点触摸输入。
	IRealTimeStylus3* pRealTimeStylus3 = NULL;
	hr = pRealTimeStylus->QueryInterface(&pRealTimeStylus3);
	if (SUCCEEDED(hr))
	{
		hr = pRealTimeStylus3->put_MultiTouchEnabled(TRUE);
		if (FAILED(hr))
		{
			IDTLogger->warn("[CreateRealTimeStylus] 无法启用多点触控 (put_MultiTouchEnabled 失败)，将回退为单点模式。");
		}
		pRealTimeStylus3->Release(); // 用完必须释放接口
	}
	else
	{
		// 获取接口失败，不影响核心功能，仅日志
		IDTLogger->info("[CreateRealTimeStylus] 当前系统不支持 IRealTimeStylus3 (多点触控)，已跳过。");
	}

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

	// 输入融合感知：触摸点、手写笔
	TouchMode mode{};
	TouchInfo info{};

	{
		ULONG ulTcidCount;
		TABLET_CONTEXT_ID* pTcids;
		piRtsSrc->GetAllTabletContextIds(&ulTcidCount, &pTcids);
		piRtsSrc->GetPacketDescriptionData(pTcids[0], &mode.inkToDeviceScaleX, &mode.inkToDeviceScaleY, &mode.packetPropertiesCount, &mode.pPacketProperties);
		CoTaskMemFree(mode.pPacketProperties);
		CoTaskMemFree(pTcids);
	}
	{
		piRtsSrc->GetPacketDescriptionData(pStylusInfo->tcid, nullptr, nullptr, &mode.packetPropertiesCount, &mode.pPacketProperties);
	}

	// 获取数据包信息
	for (int i = 0; i < mode.packetPropertiesCount; i++)
	{
		GUID guid = mode.pPacketProperties[i].guid;
		if (guid == GUID_PACKETPROPERTY_GUID_X) mode.pt.x = LONG(pPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_Y) mode.pt.y = LONG(pPacket[i] * mode.inkToDeviceScaleY + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE)
		{
			mode.pressureMin = mode.pPacketProperties[i].PropertyMetrics.nLogicalMin;
			mode.pressureMax = mode.pPacketProperties[i].PropertyMetrics.nLogicalMax;

			mode.pressure = double(pPacket[i] - mode.pressureMin) / double(mode.pressureMax - mode.pressureMin);
		}
		else if (guid == GUID_PACKETPROPERTY_GUID_WIDTH) mode.touchWidth = LONG(pPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_HEIGHT) mode.touchHeight = LONG(pPacket[i] * mode.inkToDeviceScaleY + 0.5);
	}

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
				if (IdtInputs::IsKeyBoardDown(VK_RBUTTON) && !IdtInputs::IsKeyBoardDown(VK_LBUTTON)) mode.type = deviceType = 3;
				else mode.type = deviceType = 2;
			}
		}

		piTablet2->Release();
		piTablet->Release();
	}
	mode.isInvertedCursor = pStylusInfo->bIsInvertedCursor;

	// 如果是鼠标则不管 未来选项
	// if (mode.type == 2) return S_OK;

	LONG touchCnt = static_cast<LONG>(++TouchCnt);

	unique_lock<shared_mutex> lockTouchPointer(touchPointerSm);
	TouchPointer[pStylusInfo->cid] = touchCnt;
	info.pid = touchCnt;
	lockTouchPointer.unlock();

	std::unique_lock<std::shared_mutex> lock1(touchPosSm);
	TouchPos[touchCnt] = mode;
	lock1.unlock();

	std::unique_lock<std::shared_mutex> lock2(touchSpeedSm);
	TouchSpeed[touchCnt] = 0;
	PreviousPointPosition[touchCnt].first = PreviousPointPosition[touchCnt].second = -1;
	lock2.unlock();

	std::unique_lock<std::shared_mutex> lock3(pointListSm);
	TouchList.push_back(touchCnt);
	lock3.unlock();

	info.mode = mode;

	std::unique_lock<std::shared_mutex> lock4(touchTempSm);
	TouchTemp.push_back(info);
	lock4.unlock();

	rtsNum++, rtsDown = true;
	if (deviceType == 0) touchNum++;
	if (deviceType == 1) inkNum++;

	// 光标隐藏提前指令
	if (setlist.hideTouchPointer) SendMessage(drawpad_window, WM_SETCURSOR, (WPARAM)drawpad_window, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));

	return S_OK;
}
HRESULT CSyncEventHandlerRTS::StylusUp(IRealTimeStylus*, const StylusInfo* pStylusInfo, ULONG cPktCount, LONG* pPacket, LONG** /*ppInOutPkts*/)
{
	// 这是一个抬起状态

	shared_lock<shared_mutex> lockTouchPointer(touchPointerSm);
	auto pIt = TouchPointer.find(pStylusInfo->cid);
	if (pIt == TouchPointer.end()) return S_OK;
	int pid = pIt->second;
	lockTouchPointer.unlock();
	shared_lock<shared_mutex> lock1(touchPosSm);
	const auto mode = &TouchPos[pid];
	lock1.unlock();

	rtsNum = max(0, rtsNum - 1);
	if (rtsNum == 0) rtsDown = false;

	auto it = std::find(TouchList.begin(), TouchList.end(), pid);
	if (it != TouchList.end())
	{
		{
			if (mode->type == 0)
				touchNum = max(0, touchNum - 1);
			if (mode->type == 1)
				inkNum = max(0, inkNum - 1);

			if (mode->pPacketProperties != nullptr) CoTaskMemFree(mode->pPacketProperties);
		}

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
HRESULT CSyncEventHandlerRTS::Packets(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG cPktCount, ULONG /*cPktBuffLength*/, LONG* pPacket, ULONG* /*pcInOutPkts*/, LONG** /*ppInOutPkts*/)
{
	// 这是一个移动状态

	if (cPktCount == 0) return S_OK;

	shared_lock<shared_mutex> lockTouchPointer(touchPointerSm);
	auto pIt = TouchPointer.find(pStylusInfo->cid);
	if (pIt == TouchPointer.end()) return S_OK;
	int pid = pIt->second;
	lockTouchPointer.unlock();
	shared_lock<shared_mutex> lock1(touchPosSm);
	TouchMode mode = TouchPos[pid];
	lock1.unlock();

	const ULONG propsPerPacket = mode.packetPropertiesCount; // 单个数据包的属性数量
	ULONG lastPacketIndex = cPktCount - 1; // 1. 计算最后一个数据包的索引 (cPktCount - 1)
	LONG* lastPacket = pPacket + (lastPacketIndex * propsPerPacket); // 2. 定位到最后一个数据包在 pPacket 缓冲区中的起始地址

	for (int i = 0; i < mode.packetPropertiesCount; i++)
	{
		GUID guid = mode.pPacketProperties[i].guid;
		if (guid == GUID_PACKETPROPERTY_GUID_X) mode.pt.x = LONG(lastPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_Y) mode.pt.y = LONG(lastPacket[i] * mode.inkToDeviceScaleY + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE) mode.pressure = double(lastPacket[i] - mode.pressureMin) / double(mode.pressureMax - mode.pressureMin);
		else if (guid == GUID_PACKETPROPERTY_GUID_WIDTH) mode.touchWidth = LONG(lastPacket[i] * mode.inkToDeviceScaleX + 0.5);
		else if (guid == GUID_PACKETPROPERTY_GUID_HEIGHT) mode.touchHeight = LONG(lastPacket[i] * mode.inkToDeviceScaleY + 0.5);
	}

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
			LONG pid = -1;
			bool idExists = false;

			shared_lock<shared_mutex> lockPointListSm(pointListSm);
			if (i < rtsNum) pid = TouchList[i];
			else
			{
				lockPointListSm.unlock();
				continue;
			}
			lockPointListSm.unlock();

			if (pid < 0) continue;

			std::shared_lock<std::shared_mutex> lock1(touchPosSm);
			auto it = TouchPos.find(pid);
			if (it != TouchPos.end())
			{
				x = it->second.pt.x;
				y = it->second.pt.y;
				idExists = true;
			}
			lock1.unlock();

			if (!idExists) continue;

			std::shared_lock<std::shared_mutex> lock2(touchSpeedSm);
			if (PreviousPointPosition[pid].first == -1 && PreviousPointPosition[pid].second == -1) PreviousPointPosition[pid].first = x, PreviousPointPosition[pid].second = y, speed = 1;
			else speed = (TouchSpeed[pid] + sqrt(pow(x - PreviousPointPosition[pid].first, 2) + pow(y - PreviousPointPosition[pid].second, 2))) / 2;
			lock2.unlock();

			std::unique_lock<std::shared_mutex> lock3(touchSpeedSm);
			TouchSpeed[pid] = speed;
			lock3.unlock();

			PreviousPointPosition[pid].first = x, PreviousPointPosition[pid].second = y;
		}

		if (tRecord)
		{
			int delay = 1000 / 20 - (clock() - tRecord);
			if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
		tRecord = clock();
	}
}

// 鼠标输入兼容
IdtAtomic<LONG> leftButtonPid = 0, rightButtonPid = 0;
void HandleMouseInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) confirmaNoMouUpSignal = false;

	bool leftButtonNUp = false;
	bool rightButtonNUp = false;

	if (msg == WM_LBUTTONDOWN)
	{
		// 这是一个按下状态 (左键)
		// 相当于 StylusDown

		// 如果左键已经按下，则忽略
		if (leftButtonPid == 0)
		{
			if (rightButtonPid != 0)
			{
				cerr << 123 << endl;
				rightButtonNUp = true;
			}

			TouchMode mode{};
			TouchInfo info{};

			// 1. 获取数据包信息 (对于鼠标)
			mode.pt.x = GET_X_LPARAM(lParam);
			mode.pt.y = GET_Y_LPARAM(lParam);
			mode.pressure = 0.0;
			mode.touchWidth = 0;
			mode.touchHeight = 0;
			mode.isInvertedCursor = false;

			// 2. 获取设备类型 (左键为2)
			mode.type = 2;

			// 3. 分配并存储新的触摸点信息
			LONG touchCnt = static_cast<LONG>(++TouchCnt);
			leftButtonPid = touchCnt; // 记录左键的ID
			info.pid = touchCnt;

			std::unique_lock<std::shared_mutex> lock1(touchPosSm);
			TouchPos[touchCnt] = mode;
			lock1.unlock();

			std::unique_lock<std::shared_mutex> lock2(touchSpeedSm);
			TouchSpeed[touchCnt] = 0;
			PreviousPointPosition[touchCnt] = { -1, -1 };
			lock2.unlock();

			std::unique_lock<std::shared_mutex> lock3(pointListSm);
			TouchList.push_back(touchCnt);
			lock3.unlock();

			info.mode = mode;

			std::unique_lock<std::shared_mutex> lock4(touchTempSm);
			TouchTemp.push_back(info);
			lock4.unlock();

			// 4. 更新全局状态
			rtsNum++;
			rtsDown = true;

			// 光标隐藏提前指令（因为有可能是由触摸引起的鼠标消息）
			if (setlist.hideTouchPointer) SendMessage(drawpad_window, WM_SETCURSOR, (WPARAM)drawpad_window, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
		}
	}
	if (msg == WM_RBUTTONDOWN)
	{
		// 这是一个按下状态 (右键)
		// 相当于 StylusDown

		// 如果右键已经按下，则忽略
		if (rightButtonPid == 0)
		{
			if (leftButtonPid != 0)
			{
				leftButtonNUp = true;
			}

			TouchMode mode{};
			TouchInfo info{};

			// 1. 获取数据包信息 (对于鼠标)
			mode.pt.x = GET_X_LPARAM(lParam);
			mode.pt.y = GET_Y_LPARAM(lParam);
			mode.pressure = 0.0;
			mode.touchWidth = 0;
			mode.touchHeight = 0;
			mode.isInvertedCursor = false;

			// 2. 获取设备类型 (右键为3)
			mode.type = 3;

			// 3. 分配并存储新的触摸点信息
			LONG touchCnt = static_cast<LONG>(++TouchCnt);
			rightButtonPid = touchCnt; // 记录右键的ID
			info.pid = touchCnt;

			std::unique_lock<std::shared_mutex> lock1(touchPosSm);
			TouchPos[touchCnt] = mode;
			lock1.unlock();

			std::unique_lock<std::shared_mutex> lock2(touchSpeedSm);
			TouchSpeed[touchCnt] = 0;
			PreviousPointPosition[touchCnt] = { -1, -1 };
			lock2.unlock();

			std::unique_lock<std::shared_mutex> lock3(pointListSm);
			TouchList.push_back(touchCnt);
			lock3.unlock();

			info.mode = mode;

			std::unique_lock<std::shared_mutex> lock4(touchTempSm);
			TouchTemp.push_back(info);
			lock4.unlock();

			// 4. 更新全局状态
			rtsNum++;
			rtsDown = true;
		}
	}

	if (msg == WM_MOUSEMOVE)
	{
		// 这是一个移动状态
		// 相当于 Packets

		// 检查左键是否按下并移动
		if (wParam & MK_LBUTTON)
		{
			if (leftButtonPid != 0)
			{
				unique_lock<shared_mutex> lock(touchPosSm);
				// 检查 TouchPos 中是否还存在该点 (以防万一)
				if (TouchPos.count(leftButtonPid)) {
					TouchPos[leftButtonPid].pt.x = GET_X_LPARAM(lParam);
					TouchPos[leftButtonPid].pt.y = GET_Y_LPARAM(lParam);
				}
			}
		}
		else if (leftButtonPid != 0) leftButtonNUp = true;

		// 检查右键是否按下并移动
		if (wParam & MK_RBUTTON)
		{
			if (rightButtonPid != 0)
			{
				unique_lock<shared_mutex> lock(touchPosSm);
				if (TouchPos.count(rightButtonPid)) {
					TouchPos[rightButtonPid].pt.x = GET_X_LPARAM(lParam);
					TouchPos[rightButtonPid].pt.y = GET_Y_LPARAM(lParam);
				}
				lock.unlock();
			}
		}
		else if (rightButtonPid != 0) rightButtonNUp = true;
	}

	if (msg == WM_LBUTTONUP || leftButtonNUp)
	{
		// 这是一个抬起状态 (左键)
		// 相当于 StylusUp

		// 如果左键没有被记录为按下，则忽略
		if (leftButtonPid != 0)
		{
			LONG pid = leftButtonPid;
			leftButtonPid = 0; // 重置ID
			leftButtonNUp = false;

			// 1. 更新全局计数器
			rtsNum = max(0, rtsNum - 1);
			if (rtsNum == 0) rtsDown = false;

			// 2. 从列表中移除触摸点
			unique_lock<shared_mutex> lockPointListSm(pointListSm);
			auto it = std::find(TouchList.begin(), TouchList.end(), pid);
			if (it != TouchList.end())
			{
				TouchList.erase(it);
			}
			lockPointListSm.unlock();

			// 3. 如果没有活动的触摸/输入点，则清空所有数据
			if (rtsNum == 0)
			{
				unique_lock<shared_mutex> lockTouchPosSm(touchPosSm);
				TouchPos.clear();
				lockTouchPosSm.unlock();

				touchNum = 0;
				inkNum = 0;
			}
		}
	}
	if (msg == WM_RBUTTONUP || rightButtonNUp)
	{
		// 这是一个抬起状态 (右键)
		// 相当于 StylusUp

		cerr << 3 << endl;
		// 如果右键没有被记录为按下，则忽略
		if (rightButtonPid != 0)
		{
			cerr << 456 << endl;

			LONG pid = rightButtonPid;
			rightButtonPid = 0; // 重置ID
			rightButtonNUp = false;

			// 1. 更新全局计数器
			rtsNum = max(0, rtsNum - 1);
			if (rtsNum == 0) rtsDown = false;

			// 2. 从列表中移除触摸点
			unique_lock<shared_mutex> lockPointListSm(pointListSm);
			auto it = std::find(TouchList.begin(), TouchList.end(), pid);
			if (it != TouchList.end())
			{
				TouchList.erase(it);
			}
			lockPointListSm.unlock();

			// 3. 如果没有活动的触摸/输入点，则清空所有数据
			if (rtsNum == 0)
			{
				unique_lock<shared_mutex> lockTouchPosSm(touchPosSm);
				TouchPos.clear();
				lockTouchPosSm.unlock();

				touchNum = 0;
				inkNum = 0;
			}
		}
	}
}

HRESULT SafeRTSInit(HWND drawpad_window, IRealTimeStylus** ppRTS, IStylusSyncPlugin** ppPlugin)
{
	HRESULT hr = S_OK;
	__try {
		*ppRTS = CreateRealTimeStylus(drawpad_window);
		if (*ppRTS == NULL) return E_FAIL;

		GUID props[] = { GUID_PACKETPROPERTY_GUID_X, GUID_PACKETPROPERTY_GUID_Y,
						 GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE, GUID_PACKETPROPERTY_GUID_WIDTH,
						 GUID_PACKETPROPERTY_GUID_HEIGHT };

		hr = (*ppRTS)->SetDesiredPacketDescription(5, props);
		if (FAILED(hr)) return hr;

		*ppPlugin = CSyncEventHandlerRTS::Create(*ppRTS);
		if (*ppPlugin == NULL) return E_FAIL;

		if (!EnableRealTimeStylus(*ppRTS)) return E_FAIL;

		return S_OK;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return E_UNEXPECTED; // 捕获到硬件/底层崩溃
	}
}
void InitRTSLogic()
{
	IRealTimeStylus* pRTS = NULL;
	IStylusSyncPlugin* pPlugin = NULL;

	// 调用包装函数，这里可以安全使用 try/catch 或其他 C++ 对象
	HRESULT hr = SafeRTSInit(drawpad_window, &pRTS, &pPlugin);

	if (FAILED(hr))
	{
		g_pRealTimeStylus = NULL;
		g_pSyncEventHandlerRTS = NULL;

		IDTLogger->warn("[主线程] RTS 初始化失败，错误码: {}", hr);
		useMouseInput = true;
	}
	else
	{
		g_pRealTimeStylus = pRTS;
		g_pSyncEventHandlerRTS = pPlugin;

		IDTLogger->info("[主线程] RTS 初始化完成");
	}
}
