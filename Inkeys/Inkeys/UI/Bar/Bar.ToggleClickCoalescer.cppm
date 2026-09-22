module;

#include <array>
#include <chrono>
#include <cstddef>
#include "../../Drawing/Draw3/Draw3.Bridge.h"

export module Inkeys.UI.Bar.ToggleClickCoalescer;

export namespace Inkeys::UI::Bar
{
	enum class BarToggleChannel : std::size_t
	{
		Main,
		DrawAttribute,
		GeometryAttribute,
		More,
		ThicknessAdjust,
		PenTypeMenu,
		EraserAttribute,
		EraserSensitivity,
		Count,
	};

	struct BarDrawButtonToggleDecision
	{
		bool openDrawAttribute = false;
	};

	inline BarDrawButtonToggleDecision ResolveBarDrawButtonToggleDecision(
		bool drawAttributeOpen) noexcept
	{
		// 属性面板只负责显隐，不能改写最后选择的笔型。
		return { !drawAttributeOpen };
	}

	enum class BarClearClickAction : unsigned char
	{
		None,
		PublishClear,
		EnterSelection
	};

	inline BarClearClickAction ResolveBarClearClickAction(
		bool selectionMode, bool currentPageHasContent,
		bool doubleClickContinuation,
		bool clearAttemptedForDoubleClick,
		bool acceptedClearForDoubleClick) noexcept
	{
		if (doubleClickContinuation && clearAttemptedForDoubleClick)
			return acceptedClearForDoubleClick
				? BarClearClickAction::EnterSelection
				: BarClearClickAction::PublishClear;
		if (currentPageHasContent) return BarClearClickAction::PublishClear;
		return selectionMode
			? BarClearClickAction::None
			: BarClearClickAction::EnterSelection;
	}

	inline BarClearClickAction ResolveEraserAttributeClearClickAction(
		bool currentPageHasContent, bool doubleClickContinuation,
		bool clearAttemptedForDoubleClick,
		bool acceptedClearForDoubleClick) noexcept
	{
		if (doubleClickContinuation && clearAttemptedForDoubleClick)
			return acceptedClearForDoubleClick
				? BarClearClickAction::EnterSelection
				: BarClearClickAction::PublishClear;
		return currentPageHasContent
			? BarClearClickAction::PublishClear
			: BarClearClickAction::None;
	}

	enum class BarEraserClearReturnMode : unsigned char
	{
		Drawing,
		Shape,
		Eraser,
	};

	inline BarEraserClearReturnMode ResolveEraserClearReturnMode(
		Inkeys::Drawing::Draw3::Bridge::CompletedStrokeKind kind) noexcept
	{
		using Kind = Inkeys::Drawing::Draw3::Bridge::CompletedStrokeKind;
		if (kind == Kind::Shape) return BarEraserClearReturnMode::Shape;
		if (kind == Kind::Eraser) return BarEraserClearReturnMode::Eraser;
		return BarEraserClearReturnMode::Drawing;
	}

	class BarToggleClickCoalescer
	{
	public:
		using Clock = std::chrono::steady_clock;
		using Duration = Clock::duration;

		explicit BarToggleClickCoalescer(
			Duration mergeWindow = std::chrono::milliseconds(300)) noexcept
			: mergeWindow_(mergeWindow)
		{
		}

		bool TryBegin(
			BarToggleChannel channel,
			Clock::time_point now = Clock::now()) noexcept
		{
			const auto index = static_cast<std::size_t>(channel);
			if (index >= entries_.size()) return false;

			auto& entry = entries_[index];
			if (entry.active && now >= entry.lastToggle
				&& now - entry.lastToggle < mergeWindow_)
			{
				return false;
			}

			entry.lastToggle = now;
			entry.active = true;
			return true;
		}

	private:
		struct Entry
		{
			Clock::time_point lastToggle{};
			bool active = false;
		};

		Duration mergeWindow_;
		std::array<Entry, static_cast<std::size_t>(BarToggleChannel::Count)>
			entries_{};
	};
}
