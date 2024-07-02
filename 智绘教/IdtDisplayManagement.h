/*
 * @file		IdtDisplayManagement.h
 * @brief		IDT Display management | 显示器管理
 * @note		Obtain and manage display status | 获取并管理显示器状态
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// All function and variable descriptions should be in the corresponding cpp file.
// 所有的函数和变量说明应该在对应的 cpp 文件中。

#pragma once
#include "IdtMain.h"

extern int DisplaysNumber;
extern shared_mutex DisplaysNumberSm;
extern vector<tuple<HMONITOR, RECT, bool>> DisplaysInfo; // 显示器句柄、显示器范围、是否是主显示器
extern shared_mutex DisplaysInfoSm;

extern bool enableAppBarAutoHide;

struct MainMonitorStruct
{
	HMONITOR Monitor;
	RECT rcMonitor;

	int MonitorWidth;
	int MonitorHeight;

	bool operator==(const MainMonitorStruct& other) const
	{
		return Monitor == other.Monitor &&
			EqualRect(&rcMonitor, &other.rcMonitor) &&
			MonitorWidth == other.MonitorWidth &&
			MonitorHeight == other.MonitorHeight;
	}
	bool operator!=(const MainMonitorStruct& other) const
	{
		return !(*this == other);
	}
};
extern MainMonitorStruct MainMonitor;

void DisplayManagementMain();