/*
 * @file		IdtMain.h
 * @brief		智绘教项目中心头文件
 * @note		用于声明中心头文件以及相关中心变量
 *
 * @envir		MSVC v143 | Windows SDK 10.0.26100
 * @site		https://github.com/Alan-CRL/Inkeys
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// 程序入口点位于 IdtMain.cpp，各个文件的解释将于稍后编写，目前其名称对应作用
// 编译提示：.NET 版本默认为 .NET Framework 4.0 ，最低要求 .NET Framework 4.0（如需更改请查看 PptCOM.cs）
// 首次编译需要确认 .NET Framework 版本为 4.0，如果不一致请执行 位于 PptCOM.cs 的 <切换 .NET Framework 指南>

#pragma once

#define IDT_RELEASE
// #pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )

// 智绘教最低兼容 Windows 7 sp0
// #define _WIN32_WINNT 0x0601
// #define WINVER 0x0601

// 基础类
#include <iostream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <future>
#include "IdtAtomic.h"

#include <windows.h>
#include "Inkeys/Message/Message.Legacy.hpp"

// 图形类
#include <gdiplus.h>

// 项目内部仍以 COLORREF 的低 24 位保存 RGB，高 8 位保存 alpha。
#ifndef RGBA
#define RGBA(r, g, b, a) (COLORREF)(((b) << 16) | ((g) << 8) | (r) | ((a) << 24))
#endif
#ifndef GetAValue
#define GetAValue(rgba) (BYTE)(((rgba) >> 24) & 0xFF)
#endif

// COM类
#include <comutil.h>
#include <ole2.h>
#include <rtscom.h>
#include <rtscom_i.c>
#include <comdef.h>

// 文件类
#include <filesystem>
#include "json/json.h"
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
#include <wrl/client.h>

// 日志类
#define SPDLOG_WCHAR_FILENAMES
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>

// I18N类
#include <locale>
#include <codecvt>

// 哈希
#include "ankerl/unordered_dense.h"

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
using Microsoft::WRL::ComPtr;

#define HiBeginDraw() BEGIN_TASK()
#define HiEndDraw() END_TASK(); REDRAW_WINDOW()

extern wstring buildTime;
extern wstring editionVersion;
extern wstring editionDate;

extern wstring userId;
extern wstring globalPath;
extern wstring pluginPath;

extern wstring programArchitecture;
extern wstring targetArchitecture;
extern wstring windowsEdition;

void CloseProgram();
void RestartProgram();
void SetOffSignal(int signal);
LONG* GetOffSignalInteropPointer();

extern IdtAtomic<int> offSignal; //关闭指令

extern shared_ptr<spdlog::logger> IDTLogger;
extern IdtAtomic<bool> useMouseInput;

// 调测专用
#ifndef IDT_RELEASE
void Test();
void Testb(bool t);
void Testi(long long t);
void Testd(double t);
void Testw(wstring t);
void Testa(string t);
#define TestFalse false
#define TestCout cout

// this_thread::sleep_for(chrono::milliseconds(int))
#endif
