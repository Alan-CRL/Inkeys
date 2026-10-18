module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <windows.h>

export module Inkeys.UI.Whiteboard;

export namespace Inkeys::UI::Whiteboard
{
	// Whiteboard 只暴露业务回调；UI 视觉状态由 BarSurfaceScene 持有。
	struct BusinessCallbacks
	{
		std::function<void()> previousPage;
		std::function<void()> nextPage;
	};

	struct PageState
	{
		int currentPage = 1;
		int totalPage = 1;
		bool previousEnabled = false;
		bool pageEnabled = true;
		bool nextEnabled = true;
		bool previousInteractive = false;
		bool pageInteractive = true;
		bool nextInteractive = true;
		bool nextIsAdd = false;
		bool switching = false;
	};

	[[nodiscard]] PageState ResolvePageState(
		int currentPage, int totalPage, bool switching,
		std::optional<bool> latchedNextIsAdd = std::nullopt,
		std::optional<bool> latchedPreviousEnabled = std::nullopt) noexcept;

	// 分页事务锁存上一稳定帧的边界语义；业务页码仍可在事务中继续变化。
	class PageStateTransaction
	{
	public:
		PageStateTransaction() noexcept
			: state_(ResolvePageState(1, 1, false)) {}

		[[nodiscard]] PageState Publish(
			int currentPage, int totalPage, bool switching) noexcept
		{
			const int normalizedTotal = (std::max)(1, totalPage);
			const int normalizedCurrent = std::clamp(
				currentPage, 1, normalizedTotal);
			const bool startsTransaction = !switching_ && switching;

			// 首次发布可能直接处于 switching=true，基线必须来自真实输入。
			if (!hasStablePublishedState_)
			{
				stablePreviousEnabled_ = normalizedCurrent > 1;
				stableNextIsAdd_ = normalizedCurrent >= normalizedTotal;
				hasStablePublishedState_ = true;
			}
			if (startsTransaction || (switching && !latchedPreviousEnabled_.has_value()))
			{
				latchedPreviousEnabled_ = stablePreviousEnabled_;
				latchedNextIsAdd_ = stableNextIsAdd_;
			}

			PageState next;
			if (switching)
			{
				next = ResolvePageState(normalizedCurrent, normalizedTotal, true,
					latchedNextIsAdd_, latchedPreviousEnabled_);
			}
			else
			{
				next = ResolvePageState(normalizedCurrent, normalizedTotal, false);
				stablePreviousEnabled_ = next.previousEnabled;
				stableNextIsAdd_ = next.nextIsAdd;
				latchedPreviousEnabled_.reset();
				latchedNextIsAdd_.reset();
			}
			switching_ = switching;
			state_ = next;
			return state_;
		}

		[[nodiscard]] PageState Snapshot() const noexcept { return state_; }
		[[nodiscard]] bool HasStablePublishedState() const noexcept
		{
			return hasStablePublishedState_;
		}
		void Reset() noexcept
		{
			state_ = ResolvePageState(1, 1, false);
			switching_ = false;
			hasStablePublishedState_ = false;
			stablePreviousEnabled_ = false;
			stableNextIsAdd_ = false;
			latchedPreviousEnabled_.reset();
			latchedNextIsAdd_.reset();
		}

	private:
		PageState state_{};
		bool switching_ = false;
		bool hasStablePublishedState_ = false;
		bool stablePreviousEnabled_ = false;
		bool stableNextIsAdd_ = false;
		std::optional<bool> latchedPreviousEnabled_;
		std::optional<bool> latchedNextIsAdd_;
	};

	[[nodiscard]] inline PageState ResolvePageState(
		int currentPage, int totalPage, bool switching,
		std::optional<bool> latchedNextIsAdd,
		std::optional<bool> latchedPreviousEnabled) noexcept
	{
		PageState state;
		state.totalPage = (std::max)(1, totalPage);
		state.currentPage = std::clamp(currentPage, 1, state.totalPage);
		state.switching = switching;
		// enabled 表示稳定的业务语义；翻页事务只锁住输入，不改变视觉状态。
		state.previousEnabled = state.currentPage > 1;
		state.pageEnabled = true;
		state.nextEnabled = true;
		state.previousInteractive = !switching && state.previousEnabled;
		state.pageInteractive = !switching && state.pageEnabled;
		state.nextInteractive = !switching && state.nextEnabled;
		state.nextIsAdd = state.currentPage >= state.totalPage;
		if (switching)
		{
			if (latchedPreviousEnabled.has_value())
				state.previousEnabled = *latchedPreviousEnabled;
			if (latchedNextIsAdd.has_value())
				state.nextIsAdd = *latchedNextIsAdd;
			state.previousInteractive = false;
			state.pageInteractive = false;
			state.nextInteractive = false;
		}
		return state;
	}

	bool Initialize(BusinessCallbacks callbacks);
	void Shutdown() noexcept;
	[[nodiscard]] WNDPROC WindowProc() noexcept;
	void PublishExpandedLayoutTarget(bool expanded) noexcept;
	void PublishActive(bool active) noexcept;
	void CancelPointerCapture() noexcept;
	void PublishPageState(int currentPage, int totalPage, bool switching) noexcept;
	[[nodiscard]] bool Active() noexcept;
	[[nodiscard]] bool BackgroundMatchesActive(bool active) noexcept;
}
