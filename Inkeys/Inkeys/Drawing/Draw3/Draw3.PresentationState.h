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
			desired.selectionMode, desired.workspace, HostModeUsesAuxiliarySelection(runtime.presentationMode))
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

	inline ShownFrameKey DrawpadShownFrameKey(const HostRuntimeSnapshot& runtime,
		std::uint64_t modeRevision, std::uint64_t workspaceRevision,
		std::uint64_t primaryGeneration, std::uint64_t auxiliaryGeneration) noexcept
	{
		return { runtime.hostGeneration, runtime.appliedProductRevision, modeRevision, workspaceRevision,
			runtime.workspace, runtime.presentationReady, runtime.currentPageIndex, runtime.presentationMode,
			runtime.readyOutputTarget, runtime.readyOutputRevision, runtime.contentRevision, runtime.resizeCount, runtime.recovery.revision,
			runtime.committedWidth, runtime.committedHeight, primaryGeneration, auxiliaryGeneration };
	}

	enum class InkHandoffAction { ShowAndRequest, Wait, RetireSource, Exhausted };

	// 仅在旧窗可见、隐藏准备已通过后进入。每次显示获得独立回执；三次确认失败后返回 Exhausted，
	// 由调用方退回直接成对切换，本函数自身不产生撤窗动作。
	inline InkHandoffAction PlanInkHandoff(const ShownFrameKey& current, bool targetVisible,
		const ShownFrameRequest* receipt, unsigned attempts, bool timedOut) noexcept
	{
		if (targetVisible && receipt && receipt->key == current)
		{
			const auto status = receipt->status.load();
			if (status == ShownFrameStatus::Confirmed) return InkHandoffAction::RetireSource;
			if (status == ShownFrameStatus::Pending && !timedOut) return InkHandoffAction::Wait;
		}
		return attempts < 3 ? InkHandoffAction::ShowAndRequest : InkHandoffAction::Exhausted;
	}

	inline bool CanvasRecoveryRetryMatches(const CanvasRecoveryState& expected,
		const CanvasRecoveryState& current, const Bridge::ProductState& desired,
		bool running, bool stopping, bool consumerFatal) noexcept
	{
		return running && !stopping && !consumerFatal && expected.revision != 0 &&
			expected == current && current.retryAvailable && RecoveryMatchesDesired(current, desired);
	}

	// 最后发出的请求可能仍在 Controller 退避；已呈现工作区相同也必须发送新意图，退休过期请求。
	inline bool PresentationSceneNeedsDispatch(const Bridge::ProductState& desired,
		Bridge::Workspace presentedWorkspace, Bridge::Workspace requestedWorkspace,
		std::uint64_t requestedTargetRevision, bool restoreLatestScene) noexcept
	{
		if (restoreLatestScene || requestedWorkspace != desired.workspace) return true;
		if (desired.workspace == Bridge::Workspace::Presentation)
			return desired.presentationTarget
				? desired.presentationTarget->targetRevision != requestedTargetRevision
				: presentedWorkspace != desired.workspace || requestedTargetRevision != 0;
		return presentedWorkspace != desired.workspace;
	}

	// 回执责任与工作区是否变化独立：旧场景重放失败后，同一桌面也可能仍欠 ready。
	inline bool PresentationSceneNeedsReady(const Bridge::ProductState& desired,
		Bridge::Workspace presentedWorkspace,
		const std::optional<Bridge::PresentationReadyIdentity>& presentedTarget,
		bool commandScenePending) noexcept
	{
		return commandScenePending || presentedWorkspace != desired.workspace ||
			(desired.workspace == Bridge::Workspace::Presentation && desired.presentationTarget &&
				presentedTarget != Bridge::ReadyIdentityFor(*desired.presentationTarget));
	}

	// UI 的真实提交可以先于 CPU ready；最终准入仍须等待两种回执同时匹配。
	inline bool TryLatchPresentationUiReady(const Bridge::ProductState& desired,
		const std::optional<Bridge::PresentationReadyIdentity>& suspended,
		const Bridge::PresentationReadyIdentity& identity,
		std::optional<Bridge::PresentationReadyIdentity>& uiReady) noexcept
	{
		if (desired.workspace != Bridge::Workspace::Presentation || !desired.presentationTarget ||
			Bridge::ReadyIdentityFor(*desired.presentationTarget) != identity || suspended == identity)
			return false;
		uiReady = identity;
		return true;
	}

	constexpr bool DrawpadHasVisibleContent(bool hasInk, bool laserVisible) noexcept
	{
		return hasInk || laserVisible;
	}

	constexpr bool ShouldCompactDrawpadBar(bool whiteboard, bool selectionMode,
		bool hasInk, bool laserVisible) noexcept
	{
		return !whiteboard && selectionMode && !DrawpadHasVisibleContent(hasInk, laserVisible);
	}

	inline bool UsesDrawpadInkHandoff(const HostRuntimeSnapshot& runtime) noexcept
	{
		return DrawpadHasVisibleContent(runtime.currentPageHasContent, runtime.laserVisible) &&
			runtime.workspace != Bridge::Workspace::Whiteboard &&
			(runtime.presentationMode == HostPresentationMode::DirectCompositionVisualTree ||
				runtime.presentationMode == HostPresentationMode::DwmBlurBehind);
	}

	constexpr DrawpadPresentationSurface ResolveDrawpadPresentationSurface(
		bool selectionMode, bool currentPageHasContent, bool laserVisible,
		bool auxiliaryFullFrameClean, bool auxiliaryAvailable) noexcept
	{
		// ULW 单窗口：选择态 Drawpad 保持可见并穿透，空页也不隐藏（内容为空即全透明）。
		if (!selectionMode || !auxiliaryAvailable) return DrawpadPresentationSurface::Primary;
		return !DrawpadHasVisibleContent(currentPageHasContent, laserVisible) && auxiliaryFullFrameClean
			? DrawpadPresentationSurface::Hidden
			: DrawpadPresentationSurface::Presentation;
	}
}
