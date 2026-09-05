#pragma once

#include "Draw3.Bridge.h"
#include "Draw3.Host.h"

#include <windows.h>

namespace Inkeys::Drawing::Draw3
{
	// 产品只允许这一份 Host；Window Service 仍拥有 Drawpad HWND。
	// 创建 HWND 前由透明呈现模块探测是否应预置不可变的 DComp 样式。
	bool ShouldPreconfigureNoRedirectionBitmap();
	Host& ProductHost() noexcept;
	bool StartProduct(HWND drawpad, HWND drawpadPresentation,
		HostStyleCallbacks callbacks = {},
		HostStartOptions options = {},
		HostRuntimeCallbacks runtimeCallbacks = {});
	void StopProduct() noexcept;
	bool ProductRunning() noexcept;
	bool ProductFirstFrameReady() noexcept;
	HostRuntimeSnapshot ProductRuntimeSnapshot() noexcept;
	bool WaitForProductContentRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) noexcept;
	bool WaitForProductRuntimeRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) noexcept;
	LRESULT ForwardProductMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	void PublishProductState(const Bridge::ProductState& state) noexcept;
	void PublishProductWorkspace(Bridge::Workspace workspace) noexcept;
	std::optional<std::uint64_t> PublishProductPresentationTarget(
		const Bridge::PresentationTarget& target) noexcept;
	void ClearProductPresentationTarget() noexcept;
	// 仅运行中的 Host 接受页请求；相同绝对页幂等成功，调用方可安全周期复核。
	bool PublishProductPage(std::uint32_t page) noexcept;
	Bridge::CommandResult PublishProductCommand(Bridge::CommandType command) noexcept;
	void SetProductActivationAllowed(bool enabled) noexcept;
}

// Window Service 的 Drawpad WndProc 入口；不创建或销毁 HWND。
LRESULT CALLBACK DrawpadMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
