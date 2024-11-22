#pragma once
#include "IdtMain.h"

extern int uRealTimeStylus;

extern bool touchDown;												// 表示触摸设备是否被按下
extern int touchNum;												// 触摸点的点击个数
extern unordered_map<LONG, pair<int, int>> PreviousPointPosition;	// 用于速度计算

struct TouchMode
{
	POINT pt;
	double pressure;
	long long touchWidth;
	long long touchHeight;

	// 辅助变量
	FLOAT inkToDeviceScaleX;
	FLOAT inkToDeviceScaleY;
	LONG logicalMin;
	LONG logicalMax;
};
extern unordered_map<LONG, double> TouchSpeed;
extern unordered_map<LONG, TouchMode> TouchPos;
extern vector<LONG> TouchList;

struct TouchInfo
{
	LONG pid;
	POINT pt;

	// 信息变量
	int type; // 触摸0 手写笔1 左键2 右键3
	bool isInvertedCursor;
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
	static IStylusSyncPlugin* Create(IRealTimeStylus* pRealTimeStylus);

	STDMETHOD(StylusDown)(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG cPktCount, LONG* pPacket, LONG** ppInOutPkts);
	STDMETHOD(StylusUp)(IRealTimeStylus*, const StylusInfo* pStylusInfo, ULONG cPktCount, LONG* pPacket, LONG** ppInOutPkts);
	STDMETHOD(Packets)(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG cPktCount, ULONG cPktBuffLength, LONG* pPacket, ULONG* pcInOutPkts, LONG** ppInOutPkts);
	STDMETHOD(DataInterest)(RealTimeStylusDataInterest* pDataInterest);

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