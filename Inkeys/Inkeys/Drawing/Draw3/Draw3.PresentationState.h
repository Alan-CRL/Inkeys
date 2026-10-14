#pragma once

#include "Draw3.Host.h"

#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	enum class DrawpadPresentationSurface : std::uint8_t
	{
		Primary,
		Presentation,
		Hidden,
	};

	// 只有同一工作区/模式的成功呈现帧可用于 HWND 交接，等待时保留原可见表面。
	inline bool DrawpadPresentationReady(const Bridge::ProductState& desired,
		const HostRuntimeSnapshot& runtime) noexcept
	{
		const auto target = Bridge::SelectionUsesAuxiliaryOutput(
			desired.selectionMode, desired.workspace)
			? HostOutputTarget::SelectionUlw : HostOutputTarget::PrimaryDrawpad;
		return runtime.firstFrameReady && !runtime.commandScenePending &&
			runtime.selectionMode == desired.selectionMode &&
			runtime.workspace == desired.workspace &&
			runtime.requestedOutputTarget == target && runtime.readyOutputTarget == target &&
			runtime.readyOutputRevision == runtime.requestedOutputRevision &&
			runtime.presentedContentRevision == runtime.contentRevision &&
			(desired.workspace != Bridge::Workspace::Presentation ||
				(desired.presentationTarget && runtime.presentationReady ==
					Bridge::ReadyIdentityFor(*desired.presentationTarget)));
	}

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
