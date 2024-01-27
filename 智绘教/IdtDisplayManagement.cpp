#include "IdtDisplayManagement.h"

int displays_number = 0;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	int* monitorCount = reinterpret_cast<int*>(dwData);
	(*monitorCount)++;
	return TRUE;
}