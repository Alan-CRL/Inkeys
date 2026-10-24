#pragma once

#include <cstdint>
#include "../../Drawing/Draw3/Draw3.Recovery.h"

namespace Inkeys::UI::PptRecovery
{
	enum class Kind : std::uint8_t
	{
		None, Recovering, Conflict, ReadFailed, PresentFailed, Failed,
	};

	// 只携带已发布故障的身份；它不能充当文稿/UI ready 回执。
	struct State
	{
		Kind kind = Kind::None;
		std::uint64_t session = 0, targetRevision = 0, revision = 0;
		bool retryAvailable = false;
		friend constexpr bool operator==(const State&, const State&) noexcept = default;

		[[nodiscard]] constexpr bool Matches(const State& expected) const noexcept
		{
			return revision != 0 && session != 0 && targetRevision != 0
				&& revision == expected.revision && session == expected.session
				&& targetRevision == expected.targetRevision && kind == expected.kind;
		}
		[[nodiscard]] constexpr bool CanRetry(const State& expected) const noexcept
		{
			return retryAvailable && expected.retryAvailable && kind != Kind::None
				&& kind != Kind::Recovering && Matches(expected);
		}
	};

	[[nodiscard]] inline State Project(
		const Drawing::Draw3::CanvasRecoveryState& recovery,
		const Drawing::Draw3::Bridge::ProductState& desired,
		std::uint64_t session, bool whiteboard, bool consumerFatal) noexcept
	{
		using namespace Drawing::Draw3;
		if (whiteboard || recovery.status == CanvasRecoveryStatus::None || !recovery.revision
			|| !recovery.presentationTarget || recovery.presentationTarget->sessionRevision != session
			|| !RecoveryMatchesDesired(recovery, desired)) return {};
		State result;
		result.session = session;
		result.targetRevision = recovery.presentationTarget->targetRevision;
		result.revision = recovery.revision;
		result.kind = Kind::Failed;
		if (recovery.status == CanvasRecoveryStatus::Retrying || recovery.status == CanvasRecoveryStatus::Recovering)
			result.kind = Kind::Recovering;
		else if (recovery.reason == CanvasRecoveryReason::TopologyConflict) result.kind = Kind::Conflict;
		else if (recovery.reason == CanvasRecoveryReason::CurrentLoadIoError ||
			recovery.reason == CanvasRecoveryReason::CurrentLoadRejected) result.kind = Kind::ReadFailed;
		else if (recovery.reason == CanvasRecoveryReason::PresentFailure) result.kind = Kind::PresentFailed;
		result.retryAvailable = recovery.retryAvailable && !consumerFatal
			&& recovery.reason != CanvasRecoveryReason::ConsumerFatal && result.kind != Kind::Recovering;
		return result;
	}
}
