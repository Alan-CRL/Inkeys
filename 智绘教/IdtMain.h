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

#define IDT_RELEASE
// #pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )

// 智绘教最低兼容 Windows 7 sp1
// #define _WIN32_WINNT 0x0601
// #define WINVER 0x0601

// 基础类
#include <iostream>									// 提供标准输入输出流
#include <thread>									// 提供线程相关的类和函数
#include <unordered_map>							// 提供基于哈希的map容器
#include <utility>									// 提供一些实用程序模板类
#include <windows.h>								// Windows API的基本头文件

// 图形类
#include "HiEasyX.h"								// HiEasyX 扩展库
#include <gdiplus.h>								// GDI+绘图接口

// COM类
#include <comutil.h>								// COM的实用程序集
#include <ole2.h>									// OLE库（对象链接与嵌入）
#include <rtscom.h>									// RTS 触控库
#include <rtscom_i.c>								// RTS 触控库 COM 接口
#include <comdef.h>									// 提供COM定义和实用程序

// 文件类
#include <filesystem>								// 文件系统库
#include "jsoncpp/json.h"							// JSON操作库
#include "hashlib++/hashlibpp.h"					// 哈希库
#include "ziputils/unzip.h"							// 解压缩库

// 其他类
#include <typeinfo>									// 提供type_info类，用于运行时类型查询
#include <psapi.h>									// 提供进程状态API
#include <netlistmgr.h>								// 用于网络列表管理
#include <wininet.h>								// 提供Internet的函数和宏
#include <intrin.h>									// 提供与CPU相关的内部函数
#include <regex>									// 提供正则表达式相关功能
#include <dwmapi.h>									// 桌面窗口管理器API
#include <wbemidl.h>								// 用于Windows管理器功能
#include <versionhelpers.h>							// 提供版本辅助函数
#include <mutex>									// 提供互斥量相关功能
#include <shared_mutex>								// 提供共享互斥量功能
#include <variant>									// 更加安全的联合体用于 i18n

// 日志类
#define SPDLOG_WCHAR_FILENAMES
#include <spdlog/spdlog.h>							// 提供日志记录服务
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

extern wstring buildTime; //构建时间
extern wstring editionDate; //程序发布日期
extern wstring editionChannel;
extern wstring editionCode; //程序版本

extern wstring userId; //用户ID（主板序列号）
extern wstring globalPath; //程序当前路径

extern int offSignal, offSignalReady; //关闭指令
extern map <wstring, bool> threadStatus; //线程状态管理

extern shared_ptr<spdlog::logger> IDTLogger;

//调测专用
#ifndef IDT_RELEASE
void Test();
void Testb(bool t);
void Testi(long long t);
void Testw(wstring t);
void Testa(string t);

// this_thread::sleep_for(chrono::milliseconds(int))
#endif