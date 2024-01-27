#pragma once
#include "IdtMain.h"

extern int uRealTimeStylus;

extern bool touchDown;												// 表示触摸设备是否被按下
extern int touchNum;												// 触摸点的点击个数
extern unordered_map<LONG, pair<int, int>> PreviousPointPosition;	// 用于速度计算

struct TouchMode
{
	POINT pt;
};
extern unordered_map<LONG, double> TouchSpeed;
extern unordered_map<LONG, TouchMode> TouchPos;
extern vector<LONG> TouchList;

struct TouchInfo
{
	LONG pid;
	POINT pt;
};
extern deque<TouchInfo> TouchTemp;

extern LONG TouchCnt;
extern unordered_map<LONG, LONG> TouchPointer;
extern shared_mutex PointPosSm, TouchSpeedSm, PointListSm, PointTempSm;

extern IRealTimeStylus* g_pRealTimeStylus;
extern IStylusSyncPlugin* g_pSyncEventHandlerRTS;

IRealTimeStylus* CreateRealTimeStylus(HWND hWnd);
bool EnableRealTimeStylus(IRealTimeStylus* pRealTimeStylus);

class CSyncEventHandlerRTS : public IStylusSyncPlugin
{
	CSyncEventHandlerRTS()
		: m_cRefCount(1),
		m_punkFTMarshaller(NULL),
		m_nContacts(0)
	{
	}
	virtual ~CSyncEventHandlerRTS()
	{
		if (m_punkFTMarshaller != NULL)
		{
			m_punkFTMarshaller->Release();
		}
	}

public:
	// Factory method
	static IStylusSyncPlugin* Create(IRealTimeStylus* pRealTimeStylus)
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

	STDMETHOD(StylusDown)(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG /*cPktCount*/, LONG* pPacket, LONG** /*ppInOutPkts*/)
	{
		uRealTimeStylus = 2;

		// 这是一个按下状态

		TABLET_CONTEXT_ID* pTcids;
		ULONG ulTcidCount;
		TABLET_CONTEXT_ID tcid;
		FLOAT fInkToDeviceScaleX;
		FLOAT fInkToDeviceScaleY;
		ULONG ulPacketProperties;
		PACKET_PROPERTY* pPacketProperties;

		piRtsSrc->GetAllTabletContextIds(&ulTcidCount, &pTcids);
		tcid = *pTcids;
		piRtsSrc->GetPacketDescriptionData(tcid, &fInkToDeviceScaleX, &fInkToDeviceScaleY, &ulPacketProperties, &pPacketProperties);

		TouchCnt++;
		TouchPointer[pStylusInfo->cid] = TouchCnt;

		TouchMode mode{};
		mode.pt.x = LONG(pPacket[0] * fInkToDeviceScaleX + 0.5);
		mode.pt.y = LONG(pPacket[1] * fInkToDeviceScaleY + 0.5);

		std::unique_lock<std::shared_mutex> lock1(PointPosSm);
		TouchPos[TouchCnt] = mode;
		lock1.unlock();

		std::unique_lock<std::shared_mutex> lock2(TouchSpeedSm);
		TouchSpeed[TouchCnt] = 0;
		PreviousPointPosition[TouchCnt].first = PreviousPointPosition[TouchCnt].second = -1;
		lock2.unlock();

		std::unique_lock<std::shared_mutex> lock3(PointListSm);
		TouchList.push_back(TouchCnt);
		lock3.unlock();

		TouchInfo info{};
		info.pid = TouchCnt;
		info.pt = mode.pt;

		std::unique_lock<std::shared_mutex> lock4(PointTempSm);
		TouchTemp.push_back(info);
		lock4.unlock();

		TouchCnt %= 100000;

		touchNum++;
		touchDown = true;

		return S_OK;
	}
	STDMETHOD(StylusUp)(IRealTimeStylus*, const StylusInfo* pStylusInfo, ULONG /*cPktCount*/, LONG* pPacket, LONG** /*ppInOutPkts*/)
	{
		uRealTimeStylus = 3;

		// 这是一个抬起状态

		touchNum = max(0, touchNum - 1);
		if (touchNum == 0) touchDown = false;

		auto it = std::find(TouchList.begin(), TouchList.end(), TouchPointer[pStylusInfo->cid]);
		if (it != TouchList.end())
		{
			std::unique_lock<std::shared_mutex> lock1(PointListSm);
			TouchList.erase(it);
			TouchPointer.erase(pStylusInfo->cid);
			lock1.unlock();
		}

		if (touchNum == 0)
		{
			std::unique_lock<std::shared_mutex> lock(PointPosSm);
			TouchPos.clear();
			lock.unlock();
		}

		return S_OK;
	}
	STDMETHOD(Packets)(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG /*cPktCount*/, ULONG /*cPktBuffLength*/, LONG* pPacket, ULONG* /*pcInOutPkts*/, LONG** /*ppInOutPkts*/)
	{
		uRealTimeStylus = 4;

		// 这是一个移动状态

		TABLET_CONTEXT_ID* pTcids;
		ULONG ulTcidCount;
		TABLET_CONTEXT_ID tcid;
		FLOAT fInkToDeviceScaleX;
		FLOAT fInkToDeviceScaleY;
		ULONG ulPacketProperties;
		PACKET_PROPERTY* pPacketProperties;

		piRtsSrc->GetAllTabletContextIds(&ulTcidCount, &pTcids);
		tcid = *pTcids;
		piRtsSrc->GetPacketDescriptionData(tcid, &fInkToDeviceScaleX, &fInkToDeviceScaleY, &ulPacketProperties, &pPacketProperties);

		TouchMode mode{};
		mode.pt.x = LONG(pPacket[0] * fInkToDeviceScaleX + 0.5);
		mode.pt.y = LONG(pPacket[1] * fInkToDeviceScaleY + 0.5);

		std::unique_lock<std::shared_mutex> lock2(PointPosSm);
		TouchPos[TouchPointer[pStylusInfo->cid]] = mode;
		lock2.unlock();

		return S_OK;
	}
	STDMETHOD(DataInterest)(RealTimeStylusDataInterest* pDataInterest)
	{
		*pDataInterest = (RealTimeStylusDataInterest)(RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp);

		return S_OK;
	}

	// IStylusSyncPlugin methods with trivial inline implementation, they all return S_OK
	STDMETHOD(RealTimeStylusEnabled)(IRealTimeStylus*, ULONG, const TABLET_CONTEXT_ID*) { return S_OK; }
	STDMETHOD(RealTimeStylusDisabled)(IRealTimeStylus*, ULONG, const TABLET_CONTEXT_ID*) { return S_OK; }
	STDMETHOD(StylusInRange)(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) { return S_OK; }
	STDMETHOD(StylusOutOfRange)(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) { return S_OK; }
	STDMETHOD(InAirPackets)(IRealTimeStylus*, const StylusInfo*, ULONG, ULONG, LONG*, ULONG*, LONG**) { return S_OK; }
	STDMETHOD(StylusButtonUp)(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) { return S_OK; }
	STDMETHOD(StylusButtonDown)(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) { return S_OK; }
	STDMETHOD(SystemEvent)(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID, SYSTEM_EVENT, SYSTEM_EVENT_DATA) { return S_OK; }
	STDMETHOD(TabletAdded)(IRealTimeStylus*, IInkTablet*) { return S_OK; }
	STDMETHOD(TabletRemoved)(IRealTimeStylus*, LONG) { return S_OK; }
	STDMETHOD(CustomStylusDataAdded)(IRealTimeStylus*, const GUID*, ULONG, const BYTE*) { return S_OK; }
	STDMETHOD(Error)(IRealTimeStylus*, IStylusPlugin*, RealTimeStylusDataInterest, HRESULT, LONG_PTR*) { return S_OK; }
	STDMETHOD(UpdateMapping)(IRealTimeStylus*) { return S_OK; }

	// IUnknown methods
	STDMETHOD_(ULONG, AddRef)()
	{
		return InterlockedIncrement(&m_cRefCount);
	}
	STDMETHOD_(ULONG, Release)()
	{
		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
		if (cNewRefCount == 0)
		{
			delete this;
		}
		return cNewRefCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
	{
		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
		{
			*ppvObj = this;
			AddRef();
			return S_OK;
		}
		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
		{
			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
		}

		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

private:
	LONG m_cRefCount;                   // COM object reference count
	IUnknown* m_punkFTMarshaller;       // free-threaded marshaller
	int m_nContacts;                    // number of fingers currently in the contact with the touch digitizer
};

void RTSSpeed();