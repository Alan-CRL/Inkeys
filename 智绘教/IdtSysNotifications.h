#pragma once

#include <iostream>
#include <windows.h>
#include <thread>
#include <chrono>

bool IdtSysNotificationsImageAndText04(std::wstring AppName, int HideTime, std::wstring ImagePath, std::wstring FirstLine, std::wstring SecondLine = L"", std::wstring ThirdLine = L"");
bool IdtSysNotificationsText04(std::wstring AppName, int HideTime, std::wstring FirstLine, std::wstring SecondLine = L"", std::wstring ThirdLine = L"");