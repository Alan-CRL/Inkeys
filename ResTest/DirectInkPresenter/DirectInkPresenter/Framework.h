#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <CommCtrl.h>
#include <Uxtheme.h>
#include <dwmapi.h>
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "Rpcrt4")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "Uxtheme")