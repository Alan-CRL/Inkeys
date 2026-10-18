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
		PositionState positions;
		std::uint64_t publicationRevision = 0;
		std::uint64_t targetRevision = 0;
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
			result.session = positions.session;
			result.epoch = positions.epoch;
			result.pairVersions = positions.pairVersions;
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
				state.layout = ToPageLayout(positions.configuration);
				state.targetRevision = targetRevision;
				state.publicationRevision = ++publicationRevision;
			}
			Inkeys::UI::PageControl::PublishPptState(state);
		}

		void CommitPageLayout(std::size_t pair,
			Inkeys::UI::PageControl::PptLayoutState layout)
		{
			std::scoped_lock lock(stateMutex);
			if (pair >= layout.pairVersions.size()) return;
			(void)positions.CommitPair(layout.session, layout.epoch, pair,
				layout.pairVersions[pair], pair == 0 ? layout.bottomPairWidth : layout.middlePairWidth,
				pair == 0 ? layout.bottomPairHeight : layout.middlePairHeight);
			// PageControl 已提交这次位置；不因每个鼠标采样反向请求渲染或写盘。
		}

		std::string CapturePositionSave()
		{
			const auto& layout = positions.configuration;
			return CapturePptComPositionSettingJson({ layout.bottomPairWidth,
				layout.bottomPairHeight, layout.middlePairWidth, layout.middlePairHeight },
				layout.rememberPosition);
		}

		void DispatchSettings(std::string payload)
		{
			if (payload.empty()) return;
			std::function<void(std::string)> callback;
			{
				std::scoped_lock lock(stateMutex);
				callback = business.persistSettings;
			}
			if (callback) callback(std::move(payload));
		}

		void PagePresented(std::uint64_t session, std::uint64_t target,
			int current, int total)
		{
			std::function<void(std::uint64_t, std::uint64_t, int, int)> callback;
			{
				std::scoped_lock lock(stateMutex);
				if (!positions.active || session != positions.session
					|| target != targetRevision || current != currentPage || total != totalPage) return;
				callback = business.pagePresented;
			}
			if (callback) callback(session, target, current, total);
		}

	}

	bool Initialize(BusinessCallbacks callbacks)
	{
		if (initialized.exchange(true, std::memory_order_acq_rel)) return false;
		{
			std::scoped_lock lock(stateMutex);
			business = std::move(callbacks);
			positions = {};
			positions.configuration = SnapshotLegacyConfiguration();
			(void)SavedPptComPositions();
			publicationRevision = 0;
			targetRevision = 0;
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
			std::move(callbackSnapshot.endShow),
			CommitPageLayout,
			PagePresented,
		});
		PublishSnapshot();
		return true;
	}

	void Shutdown() noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		std::uint64_t session = 0;
		{
			std::scoped_lock lock(stateMutex);
			session = positions.session;
		}
		PublishSession(session, false, nullptr);
		DispatchSettings(RetryPptComSettingJson());
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		{
			std::scoped_lock lock(stateMutex);
			topmostRefresh = {};
			topmostRefreshFailureLogged = false;
		}
		PublishSnapshot();
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
		DispatchSettings(RetryPptComSettingJson());
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

	void PublishPageState(int current, int total, std::uint64_t target) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		{
			std::scoped_lock lock(stateMutex);
			currentPage = current;
			totalPage = total;
			targetRevision = target;
		}
		PublishSnapshot();
	}

	void PublishSession(std::uint64_t session, bool active, HWND showWindow) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		Inkeys::UI::PageControl::FlushPositionCommits();
		std::string save;
		bool changed = false;
		{
			std::scoped_lock lock(stateMutex);
			if (session < positions.session) return;
			if (active)
			{
				if (positions.active && positions.session != session
					&& positions.configuration.rememberPosition) save = CapturePositionSave();
				const auto saved = RestorablePptComPositions();
				LayoutConfiguration baseline;
				baseline.bottomPairWidth = saved.bottomX;
				baseline.bottomPairHeight = saved.bottomY;
				baseline.middlePairWidth = saved.middleX;
				baseline.middlePairHeight = saved.middleY;
				changed = positions.BeginSession(session, baseline);
			}
			else if (positions.active && positions.session == session)
			{
				if (positions.configuration.rememberPosition) save = CapturePositionSave();
				(void)positions.EndSession(session);
				topmostRefresh = {};
				topmostRefreshFailureLogged = false;
				changed = true;
			}
			if (changed)
			{
				currentPage = totalPage = -1;
				targetRevision = 0;
			}
		}
		DispatchSettings(std::move(save));
		if (!changed) return;
		PublishSnapshot();
		Inkeys::UI::Bar::PublishPptSession(session, active, showWindow);
	}

	void NotifyConfigurationChanged(ConfigGroup group) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		Inkeys::UI::PageControl::FlushPositionCommits();
		std::string save;
		{
			std::scoped_lock lock(stateMutex);
			const auto next = SnapshotLegacyConfiguration();
			auto& layout = positions.configuration;
			// 设置只合并非位置字段；旧 legacy 坐标不是本场偏好的所有者。
			if (group == ConfigGroup::All || group == ConfigGroup::BottomPair)
			{
				layout.bottomPairScale = next.bottomPairScale;
				layout.showBottomPair = next.showBottomPair;
			}
			if (group == ConfigGroup::All || group == ConfigGroup::MiddlePair)
			{
				layout.middlePairScale = next.middlePairScale;
				layout.showMiddlePair = next.showMiddlePair;
			}
			if (positions.SetRemember(next.rememberPosition)) save = CapturePositionSave();
		}
		DispatchSettings(std::move(save));
		PublishSnapshot();
	}

	void ResetPositions() noexcept
	{
		if (!initialized.load(std::memory_order_acquire))
		{
			// helper 未启动时由设置 FIFO 接续写入，重置仍必须更新保存目标。
			(void)CapturePptComPositionSettingJson({}, pptComSetlist.memoryWidgetPosition);
			return;
		}
		std::string save;
		{
			std::scoped_lock lock(stateMutex);
			positions.RestorePositions({});
			save = CapturePositionSave();
		}
		DispatchSettings(std::move(save));
		PublishSnapshot();
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
