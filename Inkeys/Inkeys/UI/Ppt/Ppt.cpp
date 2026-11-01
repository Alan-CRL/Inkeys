module;

#include <windows.h>
#include "Ppt.Recovery.h"
#include "../../../PptWaitingDiagnostics.h"

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
		PagePresentationState pageState;
		StablePageNumbers stablePageNumbers;
		Inkeys::UI::PageControl::PptNavigationWaitingState navigationWaiting;
		PptRecovery::State pageRecovery;
		TopmostRefreshState topmostRefresh;
		bool topmostRefreshFailureLogged = false;

		namespace WaitTrace=Inkeys::Business::PptWaitTrace;
		// 调用方持有原stateMutex；只拷贝已有纯值，不增加业务锁或窗口读取。
		WaitTrace::Fields WaitingFields(std::uint64_t session=0,std::uint64_t epoch=0,unsigned direction=0) noexcept
		{
			WaitTrace::Fields value;if(!WaitTrace::Enabled())return value;
			value.session=session?session:navigationWaiting.current.session;
			value.epoch=epoch?epoch:navigationWaiting.current.epoch;
			value.direction=direction?direction:static_cast<unsigned>(navigationWaiting.current.direction);
			value.positionSession=positions.session;value.activeSession=navigationWaiting.activeSession;
			value.activeEpoch=navigationWaiting.current.epoch;value.completedEpoch=navigationWaiting.completedEpoch;
			value.layoutSession=positions.session;value.layoutEpoch=positions.epoch;value.publication=publicationRevision;
			value.flags=WaitTrace::Initialized|(positions.active?WaitTrace::PositionActive:0)
				|(navigationWaiting.current.waiting?WaitTrace::Waiting:0)|(topmostRefresh.presentationVisible?WaitTrace::Visible:0);
			return value;
		}

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
				state.presentationActive = positions.active;
				state.presentationVisible = topmostRefresh.presentationVisible;
				state.longPressEnabled =
					Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress;
				state.idleFadeSeconds = Inkeys::config.PlugIn.PPTHelper.IdleFadeSeconds;
				state.currentPage = pageState.currentPage;
				state.totalPage = pageState.totalPage;
				state.layout = ToPageLayout(positions.configuration);
				state.targetRevision = pageState.targetRevision;
				state.pageAckEpoch = pageState.pageAckEpoch;
				state.navigationWaiting = navigationWaiting.current;
				state.recovery = pageRecovery.session == positions.session ? pageRecovery : PptRecovery::State{};
				// 与旧页码和过滤后的同场recovery取同一快照；命令返回不能代替真实页面准备完成。
				state.pagePreparing = state.presentationActive && state.presentationVisible && state.layout.session != 0
					&& (pageState.mode == PageStateMode::Pending || state.recovery.kind == PptRecovery::Kind::Recovering)
					&& (state.recovery.kind == PptRecovery::Kind::None || state.recovery.kind == PptRecovery::Kind::Recovering);
				// 保留控制/ACK页码；恢复或Pending仅覆写数字文本，恢复清除先到也不能泄露未来目标页。
				state.numericPageOverride = state.presentationActive
					&& (pageState.mode == PageStateMode::Pending || state.recovery.kind == PptRecovery::Kind::Recovering);
				if(state.numericPageOverride&&stablePageNumbers.session==positions.session){
					state.displayedCurrentPage=stablePageNumbers.currentPage;state.displayedTotalPage=stablePageNumbers.totalPage;
				}
				state.publicationRevision = ++publicationRevision;
				auto waitFields=WaitingFields();
				if(state.pagePreparing){waitFields.flags|=WaitTrace::Preparing;waitFields.session=state.layout.session;
					waitFields.epoch=0;waitFields.direction=0;} // 通用准备没有命令epoch，不借用已完成请求。
				WaitTrace::Capture(WaitTrace::Stage::Snapshot,waitFields,WaitTrace::Result::Accepted);
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
			int current, int total, std::uint64_t epoch)
		{
			std::scoped_lock lock(stateMutex);
			if (!positions.active || session != positions.session
				|| pageRecovery.kind != PptRecovery::Kind::None
				|| !pageState.CanAcknowledge(current, total, target, epoch)) return;
			// 纯值回执与模式变更串行完成，不能验完旧代次后跨过一次暂停再迟到转发。
			if (business.pagePresented) business.pagePresented(session, target, current, total);
		}

		void RetryCanvas(PptRecovery::State expected)
		{
			std::function<void(PptRecovery::State)> callback;
			{
				std::scoped_lock lock(stateMutex);
				if (!positions.active || !topmostRefresh.presentationVisible
					|| positions.session != expected.session || !pageRecovery.CanRetry(expected)) return;
				callback = business.retryCanvas;
			}
			// 产品重试不能跨 UI 状态锁；最终由 Host 再核验实际故障令牌。
			if (callback) callback(expected);
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
			pageState = {};
			stablePageNumbers = {};
			navigationWaiting = {};
			pageRecovery = {};
			topmostRefresh = {};
			topmostRefreshFailureLogged = false;
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
			RetryCanvas,
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
			navigationWaiting = {};
			stablePageNumbers = {};
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

	void PublishPageState(int current, int total, std::uint64_t target,
		PageStateMode mode) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		{
			std::scoped_lock lock(stateMutex);
			pageState.Publish(current, total, target, mode);
			// 只有真实页面发布的InkReady/BasicControl更新缓存；Snapshot/recovery通知不反向授予可信数字。
			if(positions.active)stablePageNumbers.Observe(positions.session,current,total,mode);
		}
		PublishSnapshot();
	}

	void PublishNavigationWaiting(std::uint64_t session, std::uint64_t epoch, bool next) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) {
			WaitTrace::Fields fields;fields.session=session;fields.epoch=epoch;fields.direction=next?2:1;
			WaitTrace::Capture(WaitTrace::Stage::Publish,fields,WaitTrace::Result::Uninitialized);return;
		}
		bool changed = false;
		{
			std::scoped_lock lock(stateMutex);
			if (!positions.active || positions.session != session) {
				WaitTrace::Capture(WaitTrace::Stage::Publish,WaitingFields(session,epoch,next?2:1),WaitTrace::Result::PositionMismatch);return;
			}
			changed = navigationWaiting.Publish(session, epoch,
				next ? Inkeys::UI::PageControl::PptNavigationDirection::Next
					: Inkeys::UI::PageControl::PptNavigationDirection::Previous);
			WaitTrace::Capture(WaitTrace::Stage::Publish,WaitingFields(session,epoch,next?2:1),
				changed?WaitTrace::Result::Accepted:WaitTrace::Result::TrackerRejected);
		}
		if (changed) PublishSnapshot();
	}

	void CompleteNavigationWaiting(std::uint64_t session, std::uint64_t epoch) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool changed = false;
		{
			std::scoped_lock lock(stateMutex);
			changed = navigationWaiting.Complete(session, epoch);
			WaitTrace::Capture(WaitTrace::Stage::Complete,WaitingFields(session,epoch),
				changed?WaitTrace::Result::Accepted:WaitTrace::Result::Unchanged);
		}
		if (changed) PublishSnapshot();
	}

	void ClearNavigationWaiting(std::uint64_t session) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool changed = false;
		{
			std::scoped_lock lock(stateMutex);
			const auto fields=WaitingFields(session);
			changed = navigationWaiting.Clear(session);
			WaitTrace::Capture(WaitTrace::Stage::Clear,fields,changed?WaitTrace::Result::Accepted:WaitTrace::Result::Unchanged);
		}
		if (changed) PublishSnapshot();
	}

	void PublishCanvasRecovery(PptRecovery::State recovery) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		{
			std::scoped_lock lock(stateMutex);
			if (recovery.kind != PptRecovery::Kind::None &&
				(!positions.active || recovery.session != positions.session || !recovery.revision
					|| !recovery.targetRevision)) return;
			if (recovery.kind != PptRecovery::Kind::None && pageRecovery.session == recovery.session
				&& recovery.revision < pageRecovery.revision) return;
			if (pageRecovery == recovery) return;
			pageRecovery = recovery;
		}
		PublishSnapshot();
	}

	void PublishSession(std::uint64_t session, bool active, HWND showWindow) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		Inkeys::UI::PageControl::FlushPositionCommits();
		std::string save;
		bool changed = false;
		bool refreshWindow = false;
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
				refreshWindow = positions.active && positions.session == session;
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
				pageState = {};
				stablePageNumbers = {};
				WaitTrace::Capture(WaitTrace::Stage::Session,WaitingFields(),WaitTrace::Result::Reset);
				navigationWaiting.Clear();
				if (positions.active) navigationWaiting.BeginSession(positions.session);
				pageRecovery = {};
			}
		}
		DispatchSettings(std::move(save));
		if (!changed && !refreshWindow) return;
		if (changed) PublishSnapshot();
		// 同一 COM 场次后来取得窗口时只更新锚点，不重放进入/位置恢复。
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

	void SetDebugEnabled(bool enabled) noexcept
	{
		Inkeys::UI::PageControl::SetDebugEnabled(enabled);
	}
}
