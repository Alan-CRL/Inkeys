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
	// ��ȡ������Ϣ
	{
		// ����ʾ����С
		hardwareInfo.screenWidth = MainMonitor.MonitorWidth;
		hardwareInfo.screenHeight = MainMonitor.MonitorHeight;

		// ����ʾ������ߴ�
		hardwareInfo.screenPhyWidth = MainMonitor.MonitorPhyWidth;
		hardwareInfo.screenPhyHeight = MainMonitor.MonitorPhyHeight;

		// �ڴ��С
		MEMORYSTATUSEX memoryStatus;
		memoryStatus.dwLength = sizeof(memoryStatus);
		GlobalMemoryStatusEx(&memoryStatus);
	}
}