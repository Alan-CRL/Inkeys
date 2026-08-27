module;

#include <windows.h>

#include <atomic>
#include <cwchar>
#include <mutex>
#include <utility>

#include "../../../IdtConfiguration.h"

module Inkeys.UI.Ppt;

import Inkeys.UI.PageControl;
import Inkeys.UI.Bar;
import Inkeys.Other.Config;
import Inkeys.Window;

namespace Inkeys::UI::Ppt
{
	namespace
	{
		struct TopmostRefreshState
		{
			bool presentationVisible = false;
			bool pending = false;
			bool inFlight = false;
		};

		[[nodiscard]] bool BeginTopmostRefreshPublication(
			TopmostRefreshState& state, bool visible) noexcept
		{
			if (visible && !state.presentationVisible) state.pending = true;
			state.presentationVisible = visible;
			if (!visible) state.pending = false;
			if (!state.pending || state.inFlight) return false;
			state.inFlight = true;
			return true;
		}

		void CompleteTopmostRefresh(
			TopmostRefreshState& state, bool succeeded) noexcept
		{
			state.inFlight = false;
			if (succeeded) state.pending = false;
		}

		std::atomic_bool initialized = false;
		std::mutex stateMutex;
		BusinessCallbacks business;
		LayoutConfiguration configuration;
		TopmostRefreshState topmostRefresh;
		bool topmostRefreshFailureLogged = false;
		int currentPage = -1;
		int totalPage = -1;

		void LogTopmostRefreshState(bool recovered) noexcept
		{
			auto& service = Inkeys::Window::GetService();
			const HWND root = service.OverlayRoot();
			RECT bounds{};
			if (root) (void)GetWindowRect(root, &bounds);
			const HWND owner = root ? GetWindow(root, GW_OWNER) : nullptr;
			const LONG_PTR exStyle = root
				? GetWindowLongPtrW(root, GWL_EXSTYLE) : 0;
			wchar_t message[512]{};
			swprintf_s(message,
				L"[Ppt] topmost refresh %s: hwnd=0x%llX owner=0x%llX "
				L"visible=%d topmost=%d bounds=(%ld,%ld,%ld,%ld)\n",
				recovered ? L"recovered" : L"failed",
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(root)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(owner)),
				root && IsWindowVisible(root) ? 1 : 0,
				(exStyle & WS_EX_TOPMOST) != 0 ? 1 : 0,
				bounds.left, bounds.top, bounds.right, bounds.bottom);
			OutputDebugStringW(message);
		}

		[[nodiscard]] LayoutConfiguration SnapshotLegacyConfiguration()
		{
			LayoutConfiguration snapshot;
			snapshot.bottomPairWidth = pptComSetlist.bottomBothWidth;
			snapshot.bottomPairHeight = pptComSetlist.bottomBothHeight;
			snapshot.middlePairWidth = pptComSetlist.middleBothWidth;
			snapshot.middlePairHeight = pptComSetlist.middleBothHeight;
			// 结束放映旧字段继续读写兼容，但不再参与运行时窗口布局。
			snapshot.exitWidth = pptComSetlist.bottomMiddleWidth;
			snapshot.exitHeight = pptComSetlist.bottomMiddleHeight;
			snapshot.bottomPairScale = pptComSetlist.bottomSideBothWidgetScale;
			snapshot.middlePairScale = pptComSetlist.middleSideBothWidgetScale;
			snapshot.exitScale = pptComSetlist.bottomSideMiddleWidgetScale;
			snapshot.showBottomPair = pptComSetlist.showBottomBoth;
			snapshot.showMiddlePair = pptComSetlist.showMiddleBoth;
			snapshot.showExit = pptComSetlist.showBottomMiddle;
			snapshot.rememberPosition = pptComSetlist.memoryWidgetPosition;
			return snapshot;
		}

		[[nodiscard]] Inkeys::UI::PageControl::PptLayoutState ToPageLayout(
			const LayoutConfiguration& source) noexcept
		{
			Inkeys::UI::PageControl::PptLayoutState result;
			result.bottomPairWidth = source.bottomPairWidth;
			result.bottomPairHeight = source.bottomPairHeight;
			result.middlePairWidth = source.middlePairWidth;
			result.middlePairHeight = source.middlePairHeight;
			result.bottomPairScale = source.bottomPairScale;
			result.middlePairScale = source.middlePairScale;
			result.showBottomPair = source.showBottomPair;
			result.showMiddlePair = source.showMiddlePair;
			result.rememberPosition = source.rememberPosition;
			return result;
		}

		void PublishSnapshot() noexcept
		{
			Inkeys::UI::PageControl::PptState state;
			{
				std::scoped_lock lock(stateMutex);
				state.presentationVisible = topmostRefresh.presentationVisible;
				state.longPressEnabled =
					Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress;
				state.currentPage = currentPage;
				state.totalPage = totalPage;
				state.layout = ToPageLayout(configuration);
			}
			Inkeys::UI::PageControl::PublishPptState(state);
			Inkeys::UI::Bar::SetPptPresentationActive(state.presentationVisible);
		}

		void PersistPageLayout(Inkeys::UI::PageControl::PptLayoutState layout)
		{
			std::function<void(LayoutConfiguration)> callback;
			LayoutConfiguration next;
			{
				std::scoped_lock lock(stateMutex);
				configuration.bottomPairWidth = layout.bottomPairWidth;
				configuration.bottomPairHeight = layout.bottomPairHeight;
				configuration.middlePairWidth = layout.middlePairWidth;
				configuration.middlePairHeight = layout.middlePairHeight;
				configuration.bottomPairScale = layout.bottomPairScale;
				configuration.middlePairScale = layout.middlePairScale;
				configuration.showBottomPair = layout.showBottomPair;
				configuration.showMiddlePair = layout.showMiddlePair;
				configuration.rememberPosition = layout.rememberPosition;
				next = configuration;
				callback = business.persistPosition;
			}
			if (callback) callback(std::move(next));
		}
	}

	bool Initialize(BusinessCallbacks callbacks)
	{
		if (initialized.exchange(true, std::memory_order_acq_rel)) return false;
		{
			std::scoped_lock lock(stateMutex);
			business = std::move(callbacks);
			configuration = SnapshotLegacyConfiguration();
			topmostRefresh = {};
			topmostRefreshFailureLogged = false;
			currentPage = -1;
			totalPage = -1;
		}
		if (!Inkeys::UI::PageControl::Acquire())
		{
			initialized.store(false, std::memory_order_release);
			std::scoped_lock lock(stateMutex);
			business = {};
			return false;
		}
		BusinessCallbacks callbackSnapshot;
		{
			std::scoped_lock lock(stateMutex);
			callbackSnapshot = business;
		}
		Inkeys::UI::PageControl::SetPptCallbacks({
			std::move(callbackSnapshot.previousPage),
			std::move(callbackSnapshot.nextPage),
			std::move(callbackSnapshot.viewShow),
			PersistPageLayout,
		});
		Inkeys::UI::Bar::SetEndShowCallback(
			std::move(callbackSnapshot.endShow));
		PublishSnapshot();
		return true;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		{
			std::scoped_lock lock(stateMutex);
			topmostRefresh = {};
			topmostRefreshFailureLogged = false;
		}
		PublishSnapshot();
		Inkeys::UI::Bar::SetEndShowCallback({});
		Inkeys::UI::PageControl::SetPptCallbacks({});
		Inkeys::UI::PageControl::Release();
		std::scoped_lock lock(stateMutex);
		business = {};
	}

	WNDPROC WindowProc() noexcept
	{
		return Inkeys::UI::PageControl::WindowProc();
	}

	void PublishPresentationVisible(bool visible) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool requestTopmostRefresh = false;
		{
			std::scoped_lock lock(stateMutex);
			// 进入放映只抬升 owner 树根；失败后由后续 500ms 状态发布重试。
			requestTopmostRefresh = BeginTopmostRefreshPublication(
				topmostRefresh, visible);
			if (!visible) topmostRefreshFailureLogged = false;
		}
		PublishSnapshot();
		if (!requestTopmostRefresh) return;

		const bool refreshed =
			Inkeys::Window::GetService().RequestTopmostRefresh();
		bool logFailure = false;
		bool logRecovery = false;
		{
			std::scoped_lock lock(stateMutex);
			CompleteTopmostRefresh(topmostRefresh, refreshed);
			if (refreshed)
			{
				logRecovery = topmostRefreshFailureLogged;
				topmostRefreshFailureLogged = false;
			}
			else if (topmostRefresh.pending && !topmostRefreshFailureLogged)
			{
				topmostRefreshFailureLogged = true;
				logFailure = true;
			}
		}
		if (logFailure) LogTopmostRefreshState(false);
		else if (logRecovery) LogTopmostRefreshState(true);
	}

	void PublishPageState(int current, int total) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		{
			std::scoped_lock lock(stateMutex);
			currentPage = current;
			totalPage = total;
		}
		PublishSnapshot();
	}

	void NotifyConfigurationChanged(ConfigGroup group) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		// ExitShow 旧设置不再影响运行时，其余分组统一重读兼容配置快照。
		if (group != ConfigGroup::ExitShow)
		{
			std::scoped_lock lock(stateMutex);
			configuration = SnapshotLegacyConfiguration();
		}
		PublishSnapshot();
		Inkeys::UI::PageControl::NotifyLayoutChanged();
	}

	void QueueGlobalWheel(short delta) noexcept
	{
		if (initialized.load(std::memory_order_acquire))
			Inkeys::UI::PageControl::QueuePptWheel(delta);
	}

	void SetDebugEnabled(bool enabled) noexcept
	{
		Inkeys::UI::PageControl::SetDebugEnabled(enabled);
	}
}
