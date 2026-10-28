#include "Draw3.DiagnosticOutput.h"
#include "Draw3.Product.h"
#include "Draw3.HapticDiagnostics.h"

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
	Bridge::CommandResult RequestWhiteboardTheme(const WhiteboardTheme& theme,
		std::uint64_t generation, std::size_t page) noexcept
	{
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire) || !productHost.Running())
			return Bridge::CommandResult::NotRunning;
		return productHost.PublishWhiteboardTheme(theme, generation, page);
	}

	Host& ProductHost() noexcept { return productHost; }

	bool StartProduct(HWND drawpad, HWND drawpadPresentation,
		HostStyleCallbacks callbacks, HostStartOptions options,
		HostRuntimeCallbacks runtimeCallbacks)
	{
		std::scoped_lock lock(productMutex);
		if (productStopping.load(std::memory_order_acquire)) return false;
		options.requirePresentationUiReady = true;
		return productHost.Start(drawpad, drawpadPresentation, callbacks, options,
			runtimeCallbacks);
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

	bool RetryProductCanvasRecovery(const CanvasRecoveryState& expected,
		const std::function<bool()>& stillDesired) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return false;
		std::scoped_lock callLock(productCallMutex);
		return !productStopping.load(std::memory_order_acquire) && (!stillDesired || stillDesired()) &&
			productHost.RetryCanvasRecovery(expected, stillDesired);
	}

	Haptics::Context ProductHapticContext() noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return {};
		std::scoped_lock callLock(productCallMutex);
		return productStopping.load(std::memory_order_acquire) ? Haptics::Context{} : productHost.HapticContext();
	}

	bool PublishProductHapticClick(const Haptics::Click& source) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) { HapticDiagnostics::Log("product", "click=drop reason=stopping"); return false; }
		bool accepted = false;
		const bool diagnostic = HapticDiagnostics::Enabled();
		HostHapticClickDiagnostics diagnostics;
		{
			std::scoped_lock callLock(productCallMutex);
			accepted = !productStopping.load(std::memory_order_acquire) &&
				productHost.PublishHapticClick(source, diagnostic ? &diagnostics : nullptr);
		}
		// Host/Click mailbox 和产品生命周期锁均已退出，日志不能阻塞停止闸门。
		if (diagnostic && !accepted && diagnostics.rejection)
			HapticDiagnostics::Log("host_click", "reason=%s pointer=%u source_run=%llu source_revision=%llu host_run=%llu host_revision=%llu",
				diagnostics.rejection, source ? source->pointerId : 0,
				static_cast<unsigned long long>(source ? source->context.run : 0),
				static_cast<unsigned long long>(source ? source->context.canvas : 0),
				static_cast<unsigned long long>(diagnostics.context.run),
				static_cast<unsigned long long>(diagnostics.context.canvas));
		HapticDiagnostics::Log("product", "click=%s pointer=%u", accepted ? "published" : "dropped", source ? source->pointerId : 0);
		return accepted;
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
		// 内部 stop 只取消/释放捕获；不可被 productStopping 或 Running 闸门丢弃。
		// 不持 productCallMutex 跨 owner 完成等待，Stop 的外层已 drain 普通调用。
		if (message == Detail::kMouseInputOwnerMessage && wParam == Detail::kMouseInputStop)
			return productHost.ForwardMessage(window, message, wParam, lParam);
		if (productStopping.load(std::memory_order_acquire))
			return DefWindowProcW(window, message, wParam, lParam);
		// 先声明日志 scope：正常返回/异常时都先释放生命周期锁，再输出诊断。
		HapticDiagnostics::ProductMessageOutputScope diagnosticOutput;
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
		merged.penWidthContext = state.penWidthContext;
		merged.colorRgba = state.colorRgba;
		merged.paintDevice = state.paintDevice;
		merged.eraserInputs = state.eraserInputs;
		merged.cursorSettings = state.cursorSettings;
		merged.selectionMode = state.selectionMode;
		merged.autoSaveEnabled = state.autoSaveEnabled;
		productHost.PublishState(merged);
	}

	bool SetProductWorkspaceTransitionPending(std::uint64_t epoch, bool pending,
		std::uint64_t expectedHostGeneration) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return false;
		std::scoped_lock callLock(productCallMutex);
		return !productStopping.load(std::memory_order_acquire) &&
			productHost.SetWorkspaceTransitionPending(epoch, pending, expectedHostGeneration);
	}

	void PublishProductWorkspace(Bridge::Workspace workspace,
		const std::function<bool()>& stillDesired) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return;
		std::scoped_lock callLock(productCallMutex);
		// 与白板权威发布共用提交锁；旧采样不能在进入/离开后覆盖当前工作区。
		if (productStopping.load(std::memory_order_acquire) ||
			(stillDesired && !stillDesired())) return;
		if (productHost.ProductBridge().PublishWorkspace(workspace))
			productHost.PublishState(productHost.ProductBridge().Snapshot());
	}

	std::optional<std::uint64_t> PublishProductPresentationTarget(
		const Bridge::PresentationTarget& target,
		const std::function<bool()>& stillDesired) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return std::nullopt;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire) ||
			!productHost.Running() || (stillDesired && !stillDesired())) return std::nullopt;
		bool changed = false;
		const auto revision = productHost.ProductBridge().PublishPresentationTarget(
			target, &changed);
		if (changed) productHost.PublishState(productHost.ProductBridge().Snapshot());
		return revision;
	}

	bool PublishProductPresentationUiReady(
		const Bridge::PresentationReadyIdentity& identity) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return false;
		std::scoped_lock callLock(productCallMutex);
		return !productStopping.load(std::memory_order_acquire) &&
			productHost.PublishPresentationUiReady(identity);
	}

	bool SetProductPresentationInputSuspended(
		const Bridge::PresentationReadyIdentity& expected, bool suspended) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return false;
		std::scoped_lock callLock(productCallMutex);
		return !productStopping.load(std::memory_order_acquire) &&
			productHost.SetPresentationInputSuspended(expected, suspended);
	}

	void ClearProductPresentationTarget(
		const std::function<bool()>& stillDesired) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire) ||
			(stillDesired && !stillDesired())) return;
		if (productHost.ProductBridge().ClearPresentationTarget())
			productHost.PublishState(productHost.ProductBridge().Snapshot());
	}

	bool PublishProductPage(std::uint32_t page) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return false;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire)
			|| !productHost.Running()) return false;
		// 兼容入口也必须同步文稿目标，不能制造只有页码变化的半状态。
		Bridge::ProductState state = productHost.ProductBridge().Snapshot();
		if (state.hasPage && state.page == page) return true;
		if (state.presentationTarget)
		{
			Bridge::PresentationTarget target = *state.presentationTarget;
			if (page >= target.totalPages) return false;
			target.pageIndex = page;
			target.slideId = target.bindingMode == Bridge::SlideBindingMode::StableSlideId
				? std::optional<std::int32_t>(target.slideIds[page]) : std::nullopt;
			const bool published = productHost.ProductBridge().PublishPresentationTarget(
				target).has_value();
			if (published)
				productHost.PublishState(productHost.ProductBridge().Snapshot());
			return published;
		}
		state.page = page;
		state.hasPage = true;
		productHost.PublishState(state);
		return true;
	}

	Bridge::CommandResult PublishProductCommand(Bridge::CommandType command) noexcept
	{
		if (productStopping.load(std::memory_order_acquire))
			return Bridge::CommandResult::NotRunning;
		std::scoped_lock callLock(productCallMutex);
		if (productStopping.load(std::memory_order_acquire) ||
			!productHost.Running())
			return Bridge::CommandResult::NotRunning;
		return productHost.PublishCommand(command);
	}

	Shortcuts::Context CaptureProductShortcutContext() noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return {};
		std::scoped_lock lock(productCallMutex);
		return productStopping.load(std::memory_order_acquire)
			? Shortcuts::Context{} : productHost.ShortcutContext();
	}

	std::uint64_t PublishProductShortcutCommand(Bridge::CommandType type,
		const Shortcuts::Scope& expected, std::uint64_t leaseId) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return 0;
		std::scoped_lock lock(productCallMutex);
		return productStopping.load(std::memory_order_acquire) ? 0 :
			productHost.PublishShortcutCommand(type, expected, leaseId);
	}

	std::uint64_t PublishProductShortcutFence(const Shortcuts::Context& expected,
		std::uint64_t leaseId) noexcept
	{
		if (!leaseId || productStopping.load(std::memory_order_acquire)) return 0;
		std::scoped_lock lock(productCallMutex);
		return productStopping.load(std::memory_order_acquire) ? 0 :
			productHost.PublishShortcutCommand(Bridge::CommandType::ShortcutFence,
				expected.scope, leaseId, expected.contentRevision);
	}

	bool TryTakeProductShortcutCompletion(std::uint64_t id,
		Shortcuts::Completion& completion) noexcept
	{
		return productHost.TakeShortcutCompletion(id, completion);
	}
	void RetireProductShortcutRequest(std::uint64_t id) noexcept
	{
		productHost.RetireShortcutRequest(id);
	}
	std::uint64_t AcquireProductShortcutLease(const Shortcuts::Context& expected,
		bool requireContentRevision) noexcept
	{
		if (productStopping.load(std::memory_order_acquire)) return 0;
		std::scoped_lock lock(productCallMutex);
		return productStopping.load(std::memory_order_acquire) ? 0 :
			productHost.AcquireShortcutLease(expected, requireContentRevision);
	}
	void ReleaseProductShortcutLease(std::uint64_t id) noexcept
	{
		std::scoped_lock lock(productCallMutex);
		productHost.ReleaseShortcutLease(id);
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
	namespace DiagnosticOutput = Inkeys::Drawing::Draw3::DiagnosticOutput;
	try
	{
		if ((msg == Inkeys::Drawing::Draw3::Detail::kMouseInputOwnerMessage &&
			wParam == Inkeys::Drawing::Draw3::Detail::kMouseInputStop) ||
			Inkeys::Drawing::Draw3::ProductRunning())
			return Inkeys::Drawing::Draw3::ForwardProductMessage(hWnd, msg, wParam, lParam);
	}
	catch (const std::exception& exception)
	{
		// WndProc 不能让 C++ 异常穿过 USER32，否则 CRT 会直接 abort。
		DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error, "[Draw3] drawpad message stopped: %s\n", exception.what());
	}
	catch (...)
	{
		DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3] drawpad message stopped: unknown exception\n");
	}

	// Host 启动前仍由默认 WndProc 接管，避免向尚未附着的控制器发送消息。
	// SDK 的 tablet 常量在部分 ARM64 SDK 组合中未导出，使用文档固定值保持兼容。
	if (msg == 0x02CCu)
		return 0x00000001u | 0x00000008u | 0x00000100u |
			0x00000200u | 0x00010000u;
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}
