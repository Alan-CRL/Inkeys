module;

export module Inkeys.UI.Bar.RecallChrome;

export namespace Inkeys::UI::Bar
{
	// 点击后 5 秒内保持刚点中的 2×2；到点再按可用性收成静止布局。
	inline constexpr double RecallChromeIdleSeconds = 5.0;
	// 与绘制属性里未启用笔型相同的灰色。
	inline constexpr unsigned RecallCommandUnavailableColor = 0x00C8C8C8;

	struct RecallHistoryAvailability
	{
		bool canUndo = false;
		bool canRedo = false;
	};

	enum class RecallChromeHold : unsigned char
	{
		None,
		Undo,
		Redo,
	};

	enum class RecallChromeEvent : unsigned char
	{
		UndoClick,
		RedoClick,
		IdleElapsed,
		Availability,
		Dismiss,
		ReturnToDesktop,
		SuppressPopup,
	};

	struct RecallChromeState
	{
		RecallChromeHold hold = RecallChromeHold::None;
		bool popupSuppressed = false;
		RecallHistoryAvailability availability{};
	};

	struct RecallChromeView
	{
		bool popupOpen = false;
		bool popupShowsRedo = true;
		bool mainUndoVisible = true;
		bool mainRedoVisible = false;
		bool undoExpanded = true;
		bool redoExpanded = false;
		bool undoEnabled = false;
		bool redoEnabled = false;
	};

	inline RecallChromeState ApplyRecallChromeEvent(
		RecallChromeState state, RecallChromeEvent event,
		RecallHistoryAvailability availability = {}) noexcept
	{
		switch (event)
		{
		case RecallChromeEvent::UndoClick:
			if (!state.availability.canUndo) break;
			state.hold = RecallChromeHold::Undo;
			state.popupSuppressed = false;
			break;
		case RecallChromeEvent::RedoClick:
			if (!state.availability.canRedo) break;
			state.hold = RecallChromeHold::Redo;
			state.popupSuppressed = false;
			break;
		case RecallChromeEvent::IdleElapsed:
		case RecallChromeEvent::Dismiss:
		case RecallChromeEvent::ReturnToDesktop:
			state.hold = RecallChromeHold::None;
			state.popupSuppressed = false;
			break;
		case RecallChromeEvent::SuppressPopup:
			state.popupSuppressed = true;
			break;
		case RecallChromeEvent::Availability:
			state.availability = availability;
			break;
		}
		return state;
	}

	inline RecallChromeView ResolveRecallChrome(RecallChromeState state) noexcept
	{
		RecallChromeView view;
		view.undoEnabled = state.availability.canUndo;
		view.redoEnabled = state.availability.canRedo;
		const bool popup = state.hold != RecallChromeHold::None && !state.popupSuppressed;
		if (state.hold == RecallChromeHold::Undo)
		{
			view.popupOpen = popup;
			view.popupShowsRedo = true;
			view.mainUndoVisible = true;
			view.mainRedoVisible = false;
			view.undoExpanded = true;
			return view;
		}
		if (state.hold == RecallChromeHold::Redo)
		{
			view.popupOpen = popup;
			view.popupShowsRedo = false;
			view.mainUndoVisible = false;
			view.mainRedoVisible = true;
			view.redoExpanded = true;
			return view;
		}
		view.popupOpen = false;
		if (state.availability.canUndo && state.availability.canRedo)
		{
			view.mainUndoVisible = true;
			view.mainRedoVisible = true;
			view.undoExpanded = false;
			view.redoExpanded = false;
			return view;
		}
		if (state.availability.canRedo)
		{
			view.mainUndoVisible = false;
			view.mainRedoVisible = true;
			view.undoExpanded = false;
			view.redoExpanded = true;
			return view;
		}
		view.mainUndoVisible = true;
		view.mainRedoVisible = false;
		view.undoExpanded = true;
		view.redoExpanded = false;
		return view;
	}

	inline bool RecallChromeIdleElapsed(
		RecallChromeHold hold, double idleSeconds) noexcept
	{
		return hold != RecallChromeHold::None
			&& idleSeconds >= RecallChromeIdleSeconds;
	}
}
