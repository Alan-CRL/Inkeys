module;

#include <windows.h>

#include <atomic>
#include <mutex>
#include <utility>

#include "../../../IdtConfiguration.h"

module Inkeys.UI.Ppt;

import Inkeys.UI.PageControl;
import Inkeys.UI.Bar;

namespace Inkeys::UI::Ppt
{
	namespace
	{
		std::atomic_bool initialized = false;
		std::mutex stateMutex;
		BusinessCallbacks business;
		LayoutConfiguration configuration;
		bool presentationVisible = false;
		int currentPage = -1;
		int totalPage = -1;

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
				state.presentationVisible = presentationVisible;
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
			presentationVisible = false;
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
			presentationVisible = false;
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
		{
			std::scoped_lock lock(stateMutex);
			presentationVisible = visible;
		}
		PublishSnapshot();
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

	void FlashPageDirection(bool next) noexcept
	{
		if (initialized.load(std::memory_order_acquire))
			Inkeys::UI::PageControl::FlashPptDirection(next);
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

	void SetDebugEnabled(bool) noexcept
	{
		// PageControl 使用 Bar 的统一诊断和 dirty-region 路径。
	}
}
