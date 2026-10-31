#pragma once

#include "Draw3.Bridge.h"

#include <cstdint>
#include <optional>

namespace Inkeys::Drawing::Draw3
{
	enum class CanvasRecoveryStatus : std::uint8_t
	{
		None, Recovering, Retrying, ProtectedConflict, ManualRetryRequired, Failed,
	};
	enum class CanvasRecoveryReason : std::uint8_t
	{
		None, TopologyConflict, ScenePreparation, CurrentLoadIoError,
		CurrentLoadRejected, PresentFailure, ConsumerFatal,
	};
	struct CanvasRecoveryState
	{
		CanvasRecoveryStatus status = CanvasRecoveryStatus::None;
		CanvasRecoveryReason reason = CanvasRecoveryReason::None;
		// 表示失败/恢复请求的目标，不能拿仍在活动的旧 CPU 文稿代替。
		Bridge::Workspace workspace = Bridge::Workspace::Desktop;
		std::optional<Bridge::PresentationReadyIdentity> presentationTarget;
		std::uint32_t attempts = 0;
		bool retryAvailable = false;
		std::uint64_t revision = 0; // Host 单调令牌，拒绝迟到或重复的手动重试。
		friend bool operator==(const CanvasRecoveryState&, const CanvasRecoveryState&) noexcept = default;
	};

	inline bool RecoveryMatchesDesired(const CanvasRecoveryState& recovery,
		const Bridge::ProductState& desired) noexcept
	{
		return recovery.workspace == desired.workspace && recovery.presentationTarget ==
			(desired.workspace == Bridge::Workspace::Presentation && desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget)) : std::nullopt);
	}
	inline CanvasRecoveryState ProjectCanvasRecovery(const CanvasRecoveryState* reported,
		const Bridge::ProductState& desired, bool consumerFatal) noexcept
	{
		CanvasRecoveryState next;
		next.workspace = desired.workspace;
		if (desired.workspace == Bridge::Workspace::Presentation && desired.presentationTarget)
			next.presentationTarget = Bridge::ReadyIdentityFor(*desired.presentationTarget);
		if (reported && RecoveryMatchesDesired(*reported, desired)) next = *reported;
		if (consumerFatal)
		{
			next.status = CanvasRecoveryStatus::Failed;
			next.reason = CanvasRecoveryReason::ConsumerFatal;
			next.retryAvailable = false;
		}
		next.revision = 0; // 只有 Host 可以给投影分配当前生命周期的单调令牌。
		return next;
	}

	inline bool CanvasRecoveryBlocksAdmission(const CanvasRecoveryState& projected,
		const CanvasRecoveryState* reported) noexcept
	{
		// UI 对新意图可以暂不显示旧故障；owner 尚未恢复时绝不能仅靠切换意图打开输入。
		return projected.status != CanvasRecoveryStatus::None ||
			(reported && reported->status != CanvasRecoveryStatus::None);
	}

}
