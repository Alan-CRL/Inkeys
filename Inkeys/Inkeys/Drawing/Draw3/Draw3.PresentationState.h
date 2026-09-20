#pragma once

#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	enum class DrawpadPresentationSurface : std::uint8_t
	{
		Primary,
		Presentation,
		Hidden,
	};

	constexpr DrawpadPresentationSurface ResolveDrawpadPresentationSurface(
		bool selectionMode, bool currentPageHasContent,
		bool auxiliaryFullFrameClean) noexcept
	{
		if (!selectionMode) return DrawpadPresentationSurface::Primary;
		return !currentPageHasContent && auxiliaryFullFrameClean
			? DrawpadPresentationSurface::Hidden
			: DrawpadPresentationSurface::Presentation;
	}
}
