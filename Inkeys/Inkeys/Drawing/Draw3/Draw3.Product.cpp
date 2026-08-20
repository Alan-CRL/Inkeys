#include "Draw3.Product.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>

namespace
{
	Inkeys::Drawing::Draw3::Host productHost;
	std::mutex productMutex;
	// SetWindowPos 会同步重入 Drawpad WndProc，调用闸门必须允许同一 owner 线程嵌套。
	std::recursive_mutex productCallMutex;
	std::atomic_bool productStopping = false;
}

namespace Inkeys::Drawing::Draw3
{
	Host& ProductHost() noexcept { return productHost; }

	bool StartProduct(HWND drawpad, HWND drawpadPresentation,
		HostStyleCallbacks callbacks, HostStartOptions options)
	{
		std::scoped_lock lock(productMutex);
		if (productStopping.load(std::memory_order_acquire)) return false;
		return productHost.Start(drawpad, drawpadPresentation, callbacks, options);
	}

	void StopProduct() noexcept
	{
		{
			std::scoped_lock lock(productMutex);
			if (productStopping.exchange(true, std::memory_order_acq_rel)) return;
		}
		// 先阻止新调用并 drain 已进入 WndProc/bridge 的调用；锁不能跨越 Host::Stop，
		// 否则绘制线程在退出时回调 Window Service 可能与 owner thread 形成等待环。
		{
			std::scoped_lock callLock(productCallMutex);
		}
		productHost.Stop();
		productStopping.store(false, std::memory_order_release);
	}

	bool ProductRunning() noexcept
	{
		return !productStopping.load(std::memory_order_acquire) && productHost.Running();
	}

	bool ProductFirstFrameReady() noexcept
	{
		return !productStopping.load(std::memory_order_acquire) &&
			productHost.FirstFrameReady();
	}

	HostRuntimeSnapshot ProductRuntimeSnapshot() noexcept
	{
		return productHost.RuntimeSnapshot();
	}

	bool WaitForProductContentRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) noexcept
	{
		return productHost.WaitForContentRevision(revision, timeoutMilliseconds);
	}

	bool WaitForProductRuntimeRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) noexcept
	{
		return productHost.WaitForRuntimeRevision(revision, timeoutMilliseconds);
	}

	LRESULT ForwardProductMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (productStopping.load(std::memory_order_acquire))
			return DefWindowProcW(window, message, wParam, lParam);
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire))
			return DefWindowProcW(window, message, wParam, lParam);
		return productHost.ForwardMessage(window, message, wParam, lParam);
	}

	void PublishProductState(const Bridge::ProductState& state) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire)) return;
		// 工具栏只覆盖绘制属性，页码和工作区由各自事务入口维护。
		Bridge::ProductState merged = productHost.ProductBridge().Snapshot();
		merged.tool = state.tool;
		merged.widthDip = state.widthDip;
		merged.colorRgba = state.colorRgba;
		merged.selectionMode = state.selectionMode;
		productHost.PublishState(merged);
	}

	void PublishProductWorkspace(Bridge::Workspace workspace) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire)) return;
		Bridge::ProductState state = productHost.ProductBridge().Snapshot();
		state.workspace = workspace;
		state.hasPage = false;
		productHost.PublishState(state);
	}

	void PublishProductPage(std::uint32_t page) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire)) return;
		// 页码只覆盖快照中的页面字段，避免 PPT 线程回写陈旧工具状态。
		Bridge::ProductState state = productHost.ProductBridge().Snapshot();
		state.page = page;
		state.hasPage = true;
		productHost.PublishState(state);
	}

	Bridge::CommandResult PublishProductCommand(Bridge::CommandType command) noexcept
	{
		if (productStopping.load(std::memory_order_acquire))
			return Bridge::CommandResult::NotRunning;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire))
			return Bridge::CommandResult::NotRunning;
		return productHost.PublishCommand(command);
	}

	void SetProductActivationAllowed(bool enabled) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire)) return;
		productHost.SetActivationAllowed(enabled);
	}
}

LRESULT CALLBACK DrawpadMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	try
	{
		if (Inkeys::Drawing::Draw3::ProductRunning())
			return Inkeys::Drawing::Draw3::ForwardProductMessage(hWnd, msg, wParam, lParam);
	}
	catch (const std::exception& exception)
	{
		// WndProc 不能让 C++ 异常穿过 USER32，否则 CRT 会直接 abort。
		std::fprintf(stderr, "[Draw3] drawpad message stopped: %s\n", exception.what());
	}
	catch (...)
	{
		std::fputs("[Draw3] drawpad message stopped: unknown exception\n", stderr);
	}

	// Host 启动前仍由默认 WndProc 接管，避免向尚未附着的控制器发送消息。
	// SDK 的 tablet 常量在部分 ARM64 SDK 组合中未导出，使用文档固定值保持兼容。
	if (msg == 0x02CCu)
		return 0x00000001u | 0x00000008u | 0x00000100u |
			0x00000200u | 0x00010000u;
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}
