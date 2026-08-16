#pragma once

#include <array>
#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	enum class DrawpadPresentationAction : std::uint8_t
	{
		EnableClickThrough,
		DisableClickThrough,
		Show,
		Hide,
	};

	struct DrawpadPresentationPlan
	{
		std::array<DrawpadPresentationAction, 2> actions{};
	};

	// 操作顺序是合同的一部分：先修正样式，再改变窗口可见性。
	constexpr DrawpadPresentationPlan ResolveDrawpadPresentationPlan(
		bool selectionMode, bool currentPageHasContent) noexcept
	{
		if (selectionMode && !currentPageHasContent)
		{
			return { { DrawpadPresentationAction::DisableClickThrough,
				DrawpadPresentationAction::Hide } };
		}
		if (selectionMode)
		{
			return { { DrawpadPresentationAction::EnableClickThrough,
				DrawpadPresentationAction::Show } };
		}
		return { { DrawpadPresentationAction::DisableClickThrough,
			DrawpadPresentationAction::Show } };
	}
}
