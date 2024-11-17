/*
 * @file		IdtMain.h
 * @brief		智绘教项目中心头文件
 * @note		用于声明中心头文件以及相关中心变量
 *
 * @envir		Visual Studio 2022 | MSVC v143 | .NET Framework 3.5 | EasyX_20240601
 * @site		https://github.com/Alan-CRL/Intelligent-Drawing-Teaching
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// 程序入口点位于 IdtMain.cpp，各个文件的解释将于稍后编写，目前其名称对应作用
// 编译提示：.NET 版本默认为 .NET Framework 4.0 ，最低要求 .NET Framework 3.5（如需更改请查看 PptCOM.cs）
// 首次编译需要确认 .NET Framework 版本为 4.0，如果不一致请执行 位于 PptCOM.cs 的 <切换 .NET Framework 指南>

#pragma once

// #define IDT_RELEASE
// #pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )

// 智绘教最低兼容 Windows 7 sp0
// #define _WIN32_WINNT 0x0601
// #define WINVER 0x0601

// 基础类
#include <iostream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <windows.h>

// 图形类
#include "HiEasyX.h"
#include <gdiplus.h>

// COM类
#include <comutil.h>
#include <ole2.h>
#include <rtscom.h>
#include <rtscom_i.c>
#include <comdef.h>

// 文件类
#include <filesystem>
#include "jsoncpp/json.h"
#include "hashlib++/hashlibpp.h"
#include "ziputils/unzip.h"

// 其他类
#include <typeinfo>
#include <psapi.h>
#include <netlistmgr.h>
#include <wininet.h>
#include <intrin.h>
#include <regex>
#include <dwmapi.h>
#include <wbemidl.h>
#include <versionhelpers.h>
#include <mutex>
#include <shared_mutex>
#include <variant>

// 日志类
#define SPDLOG_WCHAR_FILENAMES
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>

// I18N类
#include <locale>
#include <codecvt>

//链接库
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wininet.lib")
#pragma comment(lib, "Urlmon.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "wbemuuid.lib")

using namespace std;
using namespace Gdiplus;

#define HiBeginDraw() BEGIN_TASK()
#define HiEndDraw() END_TASK(); REDRAW_WINDOW()

extern wstring buildTime;
extern wstring editionDate;
extern wstring editionChannel;
extern wstring editionCode;

extern wstring userId;
extern wstring globalPath;
extern wstring dataPath;

extern int offSignal, offSignalReady; //关闭指令
extern map <wstring, bool> threadStatus; //线程状态管理

extern shared_ptr<spdlog::logger> IDTLogger;

//调测专用
#ifndef IDT_RELEASE
void Test();
void Testb(bool t);
void Testi(long long t);
void Testd(double t);
void Testw(wstring t);
void Testa(string t);

// this_thread::sleep_for(chrono::milliseconds(int))
#endif