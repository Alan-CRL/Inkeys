#include "IdtStart.h"

#include "IdtDisplayManagement.h"
#include "IdtUpdate.h"

struct
{
	// 主显示器信息
	int screenWidth;
	int screenHeight;
	int screenPhyWidth;
	int screenPhyHeight;
	// 屏幕是横向还是纵向
	int screenOrientation;
	// 内存大小
	int ramSize;
	// 系统是否大于等于 Windows10
	bool isWindows10OrGreater;
	// 系统是否含有触摸设备
	bool hasTouchDevice;
}hardwareInfo;

// 分辨率不合适、内存不合适、系统低于win10

class EditionStateStruct
{
public:
	bool result;
	wstring editionDate;
} editionState;
void GetEdition()
{
	editionState.result = false, editionState.editionDate = L"";

	EditionInfoClass editionInfo = GetEditionInfo("LTS", "");
	if (editionInfo.errorCode == 200)
	{
		editionState.editionDate = editionInfo.editionDate;
		editionState.result = true;
	}
}

void StartForInkeys()
{
	thread(GetEdition).detach();

	// 获取基础信息
	{
		// 主显示器信息
		hardwareInfo.screenWidth = MainMonitor.MonitorWidth;
		hardwareInfo.screenHeight = MainMonitor.MonitorHeight;
		hardwareInfo.screenPhyWidth = MainMonitor.MonitorPhyWidth;
		hardwareInfo.screenPhyHeight = MainMonitor.MonitorPhyHeight;
		// 屏幕是横向还是纵向
		hardwareInfo.screenOrientation = MainMonitor.displayOrientation;

		// 内存大小
		MEMORYSTATUSEX memoryStatus;
		memoryStatus.dwLength = sizeof(memoryStatus);
		GlobalMemoryStatusEx(&memoryStatus);

		// 系统是否高于或是 Windows10
		hardwareInfo.isWindows10OrGreater = (GetWindowsVersion() >= 10);

		// 是否拥有触摸设备
		int digitizerStatus = GetSystemMetrics(SM_DIGITIZER);
		hardwareInfo.hasTouchDevice = (digitizerStatus & NID_READY) && (digitizerStatus & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH));
	}
}

int GetWindowsVersion()
{
	int ret = 0;

	// 动态加载 ntdll.dll 并获取 RtlGetVersion 函数地址
	HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
	if (hMod)
	{
		RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
		if (pRtlGetVersion)
		{
			// 创建并初始化 RTL_OSVERSIONINFOW 结构体
			RTL_OSVERSIONINFOW rovi = { 0 };
			rovi.dwOSVersionInfoSize = sizeof(rovi);
			// 调用 RtlGetVersion 获取系统版本信息
			if (pRtlGetVersion(&rovi) == 0)
				ret = rovi.dwMajorVersion;
		}
	}

	return ret;
}
bool hasTouchDevice()
{
	// 检查是否有触摸支持或者手写笔支持
	int digitizerStatus = GetSystemMetrics(SM_DIGITIZER);
	bool hasTouchInput = (digitizerStatus & NID_READY) && (digitizerStatus & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH));
	return hasTouchInput;
}