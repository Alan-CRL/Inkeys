module;

export module Inkeys.UI.Bar.RecallChrome;

export namespace Inkeys::UI::Bar
{
	// 主栏撤回外观：空闲 2×2、浮窗展开、或同一格内上 Redo 下 Undo。
	inline constexpr double RecallChromeIdleSeconds = 5.0;

	enum class RecallChromePhase : unsigned char
	{
		Idle,
		Popup,
		Compact,
	};

	enum class RecallChromeEvent : unsigned char
	{
		UndoClick,
		PopupRedoClick,
		BarRedoClick,
		Dismiss,
		DrawingStarted,
		ReturnToDesktop,
	};

	struct RecallChromeState
	{
		RecallChromePhase phase = RecallChromePhase::Idle;
	};

	struct RecallChromeView
	{
		RecallChromePhase phase = RecallChromePhase::Idle;
		bool popupOpen = false;
		bool mainRedoVisible = false;
		bool undoExpanded = true;
	};

	inline RecallChromeState ApplyRecallChromeEvent(
		RecallChromeState state, RecallChromeEvent event) noexcept
	{
		switch (event)
		{
		case RecallChromeEvent::UndoClick:
			state.phase = RecallChromePhase::Popup;
			break;
		case RecallChromeEvent::PopupRedoClick:
		case RecallChromeEvent::BarRedoClick:
			break;
		case RecallChromeEvent::Dismiss:
		case RecallChromeEvent::ReturnToDesktop:
			if (state.phase == RecallChromePhase::Popup)
				state.phase = RecallChromePhase::Compact;
			break;
		case RecallChromeEvent::DrawingStarted:
			state.phase = RecallChromePhase::Idle;
			break;
		}
		return state;
	}

	inline RecallChromeView ResolveRecallChrome(RecallChromeState state) noexcept
	{
		RecallChromeView view;
		view.phase = state.phase;
		switch (state.phase)
		{
		case RecallChromePhase::Popup:
			view.popupOpen = true;
			view.mainRedoVisible = false;
			view.undoExpanded = true;
			break;
		case RecallChromePhase::Compact:
			view.popupOpen = false;
			view.mainRedoVisible = true;
			view.undoExpanded = false;
			break;
		case RecallChromePhase::Idle:
			view.popupOpen = false;
			view.mainRedoVisible = false;
			view.undoExpanded = true;
			break;
		}
		return view;
	}

	inline bool RecallChromeIdleElapsed(
		RecallChromePhase phase, double idleSeconds) noexcept
	{
		return phase == RecallChromePhase::Popup
			&& idleSeconds >= RecallChromeIdleSeconds;
	}
}
