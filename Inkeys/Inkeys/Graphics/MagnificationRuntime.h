#pragma once

#include <windows.h>
#include <magnification.h>

#include <cstddef>

namespace Inkeys::Graphics
{
	struct MagnificationApi
	{
		decltype(&MagSetWindowSource) setSource = nullptr;
		decltype(&MagSetWindowFilterList) setFilter = nullptr;
		decltype(&MagSetWindowTransform) setTransform = nullptr;
	};

	struct MagnificationRuntimeSnapshot
	{
		std::size_t leases = 0;
		unsigned long initializeCount = 0, uninitializeCount = 0;
		bool cleanupFailed = false;
	};

	// Freeze 与连续捕获共用进程运行库；窗口仍由各自的 owner 线程销毁。
	class MagnificationRuntimeLease final
	{
	public:
		MagnificationRuntimeLease() noexcept = default;
		~MagnificationRuntimeLease();
		MagnificationRuntimeLease(const MagnificationRuntimeLease&) = delete;
		MagnificationRuntimeLease& operator=(const MagnificationRuntimeLease&) = delete;
		[[nodiscard]] bool Acquire() noexcept;
		[[nodiscard]] bool Reset() noexcept;
		[[nodiscard]] const MagnificationApi* Api() const noexcept;
	private:
		bool acquired_ = false;
	};

	[[nodiscard]] MagnificationRuntimeSnapshot GetMagnificationRuntimeSnapshot() noexcept;
	[[nodiscard]] bool SupportsMagCapture(unsigned long& error) noexcept;
}
