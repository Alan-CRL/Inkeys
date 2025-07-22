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
#include <atlbase.h>

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
#include <unordered_dense.h>

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

extern wstring userId;
extern wstring globalPath;
extern wstring dataPath;

extern wstring programArchitecture;
extern wstring targetArchitecture;

void CloseProgram();
void RestartProgram();

extern int offSignal; //关闭指令
extern map <wstring, bool> threadStatus; //线程状态管理

extern shared_ptr<spdlog::logger> IDTLogger;

// 私有模板
template <typename IdtAtomicT>
class IdtAtomic
{
	static_assert(is_trivially_copyable_v<IdtAtomicT>, "IdtAtomic<IdtAtomicT>: IdtAtomicT 必须是平凡可复制 (TriviallyCopyable) 的类型。");
	static_assert(atomic<IdtAtomicT>::is_always_lock_free, "IdtAtomic<IdtAtomicT>: IdtAtomicT 对应的 atomic<IdtAtomicT> 必须保证始终无锁 (lock-free)。");

private:
	atomic<IdtAtomicT> value;

public:
	IdtAtomic() noexcept = default;
	IdtAtomic(IdtAtomicT desired) noexcept : value(desired) {}
	IdtAtomic(const IdtAtomic& other) noexcept { value.store(other.value.load()); }

	IdtAtomic& operator=(const IdtAtomic& other) noexcept {
		if (this != &other) value.store(other.value.load());
		return *this;
	}

	IdtAtomic(IdtAtomic&& other) noexcept { value.store(other.value.load()); }
	IdtAtomic& operator=(IdtAtomic&& other) noexcept { value.store(other.value.load()); return *this; }

	IdtAtomicT load(memory_order order = memory_order_seq_cst) const noexcept { return value.load(order); }
	void store(IdtAtomicT desired, memory_order order = memory_order_seq_cst) noexcept { value.store(desired, order); }

	IdtAtomicT exchange(IdtAtomicT desired, memory_order order = memory_order_seq_cst) noexcept { return value.exchange(desired, order); }
	bool compare_exchange_weak(IdtAtomicT& expected, IdtAtomicT desired,
		memory_order success = memory_order_seq_cst,
		memory_order failure = memory_order_seq_cst) noexcept {
		return value.compare_exchange_weak(expected, desired, success, failure);
	}
	bool compare_exchange_strong(IdtAtomicT& expected, IdtAtomicT desired,
		memory_order success = memory_order_seq_cst,
		memory_order failure = memory_order_seq_cst) noexcept {
		return value.compare_exchange_strong(expected, desired, success, failure);
	}
	bool compare_set_strong(const IdtAtomicT& expected_val_in, IdtAtomicT desired_val,
		std::memory_order success = std::memory_order_seq_cst,
		std::memory_order failure = std::memory_order_seq_cst) noexcept {
		IdtAtomicT expected_local = expected_val_in;
		return value.compare_exchange_strong(expected_local, desired_val, success, failure);
	}

	template <typename T = IdtAtomicT, typename = std::enable_if_t<std::is_integral_v<T>>>
	T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
		return value.fetch_add(arg, order);
	}
	template <typename T = IdtAtomicT, typename = std::enable_if_t<std::is_integral_v<T>>>
	T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
		return value.fetch_sub(arg, order);
	}

	operator IdtAtomicT() const noexcept { return load(); }
	IdtAtomic& operator=(IdtAtomicT desired) noexcept { store(desired); return *this; }

	// Increment/Decrement Operators added
	template <typename T = IdtAtomicT, typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator++() noexcept {
		return value.fetch_add(1, std::memory_order_seq_cst) + 1;
	}
	template <typename T = IdtAtomicT, typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator++(int) noexcept {
		return value.fetch_add(1, std::memory_order_seq_cst);
	}
	template <typename T = IdtAtomicT, typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator--() noexcept {
		return value.fetch_sub(1, std::memory_order_seq_cst) - 1;
	}
	template <typename T = IdtAtomicT, typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator--(int) noexcept {
		return value.fetch_sub(1, std::memory_order_seq_cst);
	}

	void wait(IdtAtomicT expected, std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		value.wait(expected, order);
	}
	void notify_one() noexcept
	{
		value.notify_one();
	}
	void notify_all() noexcept
	{
		value.notify_all();
	}
};
template<typename IdtOptionalT>
class IdtOptional
{
	static_assert(is_default_constructible_v<IdtOptionalT>, "IdtOptional<IdtOptionalT>: IdtOptionalT 必须是默认可构造类型");

private:
	optional<IdtOptionalT> opt;
public:
	IdtOptional() = default;
	IdtOptional(nullopt_t) : opt(nullopt) {}
	IdtOptional(const IdtOptionalT& val) : opt(val) {}
	IdtOptional(IdtOptionalT&& val) : opt(move(val)) {}

	// 判断是否有值
	bool has_value() const { return opt.has_value(); }

	// 支持直接赋值
	IdtOptional& operator=(const IdtOptionalT& val) { opt = val; return *this; }
	IdtOptional& operator=(IdtOptionalT&& val) { opt = move(val); return *this; }
	IdtOptional& operator=(nullopt_t) { opt = nullopt; return *this; }

	// 只读场景（const对象/const成员）
	operator const IdtOptionalT& () const { return opt ? *opt : default_value(); }
	const IdtOptionalT& operator*() const { return opt ? *opt : default_value(); }
	const IdtOptionalT* operator->() const { return opt ? addressof(*opt) : addressof(default_value()); }
	// 写场景（非const对象/成员）
	operator IdtOptionalT& ()
	{
		if (!opt) opt = IdtOptionalT{};
		return *opt;
	}
	IdtOptionalT& operator*()
	{
		if (!opt) opt = IdtOptionalT{};
		return *opt;
	}
	IdtOptionalT* operator->()
	{
		if (!opt) opt = IdtOptionalT{};
		return addressof(*opt);
	}

	// 比较操作
	auto operator<=>(const IdtOptional& other) const
	{
		return this->operator const IdtOptionalT & () <=> other.operator const IdtOptionalT & ();
	}
	bool operator==(const IdtOptional& other) const
	{
		return this->operator const IdtOptionalT & () == other.operator const IdtOptionalT & ();
	}
	bool operator!=(const IdtOptional& other) const
	{
		return this->operator const IdtOptionalT & () != other.operator const IdtOptionalT & ();
	}

private:
	static const IdtOptionalT& default_value()
	{
		static IdtOptionalT t{};
		return t;
	}
};

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