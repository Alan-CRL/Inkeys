#pragma once
#include "IdtMain.h"
#include <ctime>

extern SYSTEMTIME sys_time;
//时间戳
wstring getTimestamp();
wstring getCurrentDate();
//获取日期
wstring CurrentDate();
//获取时间
wstring CurrentTime();

void GetTime();
string GetCurrentTimeAll();

tm GetCurrentLocalTime();