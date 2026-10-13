module;

export module Inkeys.UI.Bar.RecallChrome;

export namespace Inkeys::UI::Bar
{
	// 点击后 3 秒内保持位置与命令身份；没有点击时，从待调整目标变化开始等待。
	inline constexpr double RecallChromeIdleSeconds = 3.0;
	// 与绘制属性里未启用笔型相同的灰色。
	inline constexpr unsigned RecallCommandUnavailableColor = 0x00C8C8C8;

	inline unsigned ResolveRecallCommandContentColor(bool enabled, unsigned normalColor) noexcept
	{
		return enabled ? normalColor : RecallCommandUnavailableColor;
	}

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

	enum class RecallChromeLayout : unsigned char
	{
		Undo,
		Compact,
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
		// 几何独立锁存，实时可用性只改变颜色和动作门禁。
		RecallChromeLayout layout = RecallChromeLayout::Undo;
		RecallChromeLayout pendingLayout = RecallChromeLayout::Undo;
		bool settlePending = false;
		unsigned long long idleResetSerial = 0;
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

	inline RecallChromeLayout RecallChromeRestingLayout(RecallHistoryAvailability availability) noexcept
	{
		if (availability.canUndo && availability.canRedo) return RecallChromeLayout::Compact;
		return availability.canRedo ? RecallChromeLayout::Redo : RecallChromeLayout::Undo;
	}

	inline RecallChromeState ApplyRecallChromeEvent(
		RecallChromeState state, RecallChromeEvent event,
		RecallHistoryAvailability availability = {}) noexcept
	{
		switch (event)
		{
		case RecallChromeEvent::UndoClick:
		case RecallChromeEvent::RedoClick:
			// 事件由已接受的命令产生；弹窗连点不能把主栏与弹窗身份互换。
			if (state.hold == RecallChromeHold::None)
			{
				state.hold = event == RecallChromeEvent::RedoClick
					? RecallChromeHold::Redo : RecallChromeHold::Undo;
				state.layout = event == RecallChromeEvent::RedoClick
					? RecallChromeLayout::Redo : RecallChromeLayout::Undo;
			}
			state.popupSuppressed = false;
			state.settlePending = true;
			++state.idleResetSerial;
			break;
		case RecallChromeEvent::IdleElapsed:
			state.layout = RecallChromeRestingLayout(state.availability);
			state.pendingLayout = state.layout;
			state.hold = RecallChromeHold::None;
			state.popupSuppressed = false;
			state.settlePending = false;
			break;
		case RecallChromeEvent::Dismiss:
		case RecallChromeEvent::ReturnToDesktop:
		case RecallChromeEvent::SuppressPopup:
			// 关闭面板不清保护期，尺寸/身份仍等最后一次点击后的 3 秒。
			state.popupSuppressed = true;
			break;
		case RecallChromeEvent::Availability:
			state.availability = availability;
			state.pendingLayout = RecallChromeRestingLayout(availability);
			state.settlePending = state.hold != RecallChromeHold::None
				|| state.layout != state.pendingLayout;
			break;
		}
		return state;
	}

	inline bool RecallChromeCommandEnabled(RecallHistoryAvailability availability, bool redo) noexcept
	{
		return redo ? availability.canRedo : availability.canUndo;
	}

	inline RecallChromeState ApplyRecallChromeCommandResult(RecallChromeState state,
		bool redo, RecallHistoryAvailability clickAvailability, bool accepted) noexcept
	{
		// Draw3 可在 Publish 后立即耗尽末步，只用点击前快照认可本次展开。
		if (!accepted || !RecallChromeCommandEnabled(clickAvailability, redo))
		{
			// 末步后仍在原位置连点时，只延长已有保护，不开新面板或发命令。
			if (state.settlePending) ++state.idleResetSerial;
			return state;
		}
		return ApplyRecallChromeEvent(state, redo ? RecallChromeEvent::RedoClick : RecallChromeEvent::UndoClick);
	}

	inline bool RecallChromeNeedsIdleRestart(RecallChromeState before, RecallChromeState after) noexcept
	{
		return after.settlePending && (!before.settlePending
			|| before.idleResetSerial != after.idleResetSerial
			|| (after.hold == RecallChromeHold::None && before.pendingLayout != after.pendingLayout));
	}

	inline RecallChromeView ResolveRecallChrome(RecallChromeState state) noexcept
	{
		RecallChromeView view;
		view.undoEnabled = state.availability.canUndo;
		view.redoEnabled = state.availability.canRedo;
		view.popupOpen = state.hold != RecallChromeHold::None && !state.popupSuppressed;
		view.popupShowsRedo = state.hold != RecallChromeHold::Redo;
		view.mainUndoVisible = state.layout != RecallChromeLayout::Redo;
		view.mainRedoVisible = state.layout != RecallChromeLayout::Undo;
		view.undoExpanded = state.layout == RecallChromeLayout::Undo;
		view.redoExpanded = state.layout == RecallChromeLayout::Redo;
		return view;
	}

	inline bool RecallChromeIdleElapsed(RecallChromeState state, double idleSeconds) noexcept
	{
		return state.settlePending && idleSeconds >= RecallChromeIdleSeconds;
	}
}
