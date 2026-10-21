#include "MagnificationRuntime.h"

#include <mutex>
#include <new>

namespace Inkeys::Graphics
{
	namespace
	{
		struct Runtime
		{
			std::mutex mutex;
			HMODULE module = nullptr;
			decltype(&MagInitialize) initialize = nullptr;
			decltype(&MagUninitialize) uninitialize = nullptr;
			MagnificationApi api;
			MagnificationRuntimeSnapshot snapshot;
		};

		Runtime& SharedRuntime() noexcept
		{
			// 未返回的系统调用可能比服务对象活得久，避免静态析构撤走它的运行库。
			alignas(Runtime) static unsigned char storage[sizeof(Runtime)];
			static Runtime* runtime = new (storage) Runtime;
			return *runtime;
		}
	}

	MagnificationRuntimeLease::~MagnificationRuntimeLease()
	{
		(void)Reset();
	}

	bool MagnificationRuntimeLease::Acquire() noexcept
	{
		if (acquired_) return true;
		auto& runtime = SharedRuntime();
		std::lock_guard lock(runtime.mutex);
		if (runtime.snapshot.cleanupFailed)
		{
			SetLastError(ERROR_INVALID_STATE);
			return false;
		}
		if (runtime.snapshot.leases == 0)
		{
			runtime.module = LoadLibraryW(L"Magnification.dll");
			if (!runtime.module) return false;
			runtime.initialize = reinterpret_cast<decltype(runtime.initialize)>(
				GetProcAddress(runtime.module, "MagInitialize"));
			runtime.uninitialize = reinterpret_cast<decltype(runtime.uninitialize)>(
				GetProcAddress(runtime.module, "MagUninitialize"));
			runtime.api.setSource = reinterpret_cast<decltype(runtime.api.setSource)>(
				GetProcAddress(runtime.module, "MagSetWindowSource"));
			runtime.api.setFilter = reinterpret_cast<decltype(runtime.api.setFilter)>(
				GetProcAddress(runtime.module, "MagSetWindowFilterList"));
			runtime.api.setTransform = reinterpret_cast<decltype(runtime.api.setTransform)>(
				GetProcAddress(runtime.module, "MagSetWindowTransform"));
			if (!runtime.initialize || !runtime.uninitialize || !runtime.api.setSource ||
				!runtime.api.setFilter || !runtime.api.setTransform)
			{
				FreeLibrary(runtime.module);
				runtime.module = nullptr;
				SetLastError(ERROR_PROC_NOT_FOUND);
				return false;
			}
			if (!runtime.initialize())
			{
				const auto error = GetLastError();
				FreeLibrary(runtime.module);
				runtime.module = nullptr;
				SetLastError(error);
				return false;
			}
			++runtime.snapshot.initializeCount;
		}
		++runtime.snapshot.leases;
		acquired_ = true;
		return true;
	}

	bool MagnificationRuntimeLease::Reset() noexcept
	{
		if (!acquired_) return true;
		auto& runtime = SharedRuntime();
		std::lock_guard lock(runtime.mutex);
		acquired_ = false;
		if (--runtime.snapshot.leases != 0) return true;
		if (!runtime.uninitialize())
		{
			// 清理失败时保留 DLL，不向下一代宣称运行库已安全退出。
			runtime.snapshot.cleanupFailed = true;
			return false;
		}
		++runtime.snapshot.uninitializeCount;
		FreeLibrary(runtime.module);
		runtime.module = nullptr;
		runtime.api = {};
		return true;
	}

	const MagnificationApi* MagnificationRuntimeLease::Api() const noexcept
	{
		return acquired_ ? &SharedRuntime().api : nullptr;
	}

	MagnificationRuntimeSnapshot GetMagnificationRuntimeSnapshot() noexcept
	{
		auto& runtime = SharedRuntime();
		std::lock_guard lock(runtime.mutex);
		return runtime.snapshot;
	}

	bool SupportsMagCapture(unsigned long& error) noexcept
	{
		using VersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
		using Wow64Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
		const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		const auto versionFn = reinterpret_cast<VersionFn>(
			ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
		OSVERSIONINFOW version{ sizeof(version) };
		if (!versionFn || versionFn(&version) != 0 ||
			version.dwMajorVersion < 6 ||
			(version.dwMajorVersion == 6 && version.dwMinorVersion < 3) ||
			(ntdll && GetProcAddress(ntdll, "wine_get_version")))
		{
			error = ERROR_OLD_WIN_VERSION;
			return false;
		}
		const auto wow64Fn = reinterpret_cast<Wow64Fn>(GetProcAddress(
			GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
		USHORT processMachine = 0, nativeMachine = 0;
		BOOL wow64 = FALSE;
		if (wow64Fn)
		{
			if (!wow64Fn(GetCurrentProcess(), &processMachine, &nativeMachine))
			{
				error = GetLastError();
				return false;
			}
			wow64 = processMachine != IMAGE_FILE_MACHINE_UNKNOWN;
		}
		else if (!IsWow64Process(GetCurrentProcess(), &wow64))
		{
			error = GetLastError();
			return false;
		}
		error = wow64 ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
		return !wow64;
	}
}
