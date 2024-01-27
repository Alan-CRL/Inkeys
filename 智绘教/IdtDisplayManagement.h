//智绘教将在后续版本中考虑对多显示器的支持

#pragma once
#include "IdtMain.h"

extern int displays_number;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);