#include "IdtStart.h"

#include "IdtDisplayManagement.h"

struct
{
	int screenWidth;
	int screenHeight;
	int screenPhyWidth;
	int screenPhyHeight;

	int ramSize;
}hardwareInfo;

void StartForInkeys()
{
	// 获取基础信息
	{
		// 主显示器大小
		hardwareInfo.screenWidth = MainMonitor.MonitorWidth;
		hardwareInfo.screenHeight = MainMonitor.MonitorHeight;

		// 主显示器物理尺寸
		hardwareInfo.screenPhyWidth = MainMonitor.MonitorPhyWidth;
		hardwareInfo.screenPhyHeight = MainMonitor.MonitorPhyHeight;

		// 内存大小
		MEMORYSTATUSEX memoryStatus;
		memoryStatus.dwLength = sizeof(memoryStatus);
		GlobalMemoryStatusEx(&memoryStatus);
	}
}