#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dxgi.h>

#include <cstdint>
#include <limits>

namespace Inkeys::UI::Bar
{
	[[nodiscard]] constexpr bool IsBarSharedDeviceLost(HRESULT hr) noexcept
	{
		return hr == DXGI_ERROR_DEVICE_REMOVED
			|| hr == DXGI_ERROR_DEVICE_RESET
			|| hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	}

	struct BarPresentDemand
	{
		bool visual = false;
		bool lighting = false;
		bool renderOnce = false;
	};

	enum class BarPresentFailureClass : unsigned char
	{
		None,
		DeviceResources,
		GetDc,
		UpdateLayeredWindow,
		ReleaseDc,
		EndDraw,
	};

	enum class BarPresentLeaseOutcome : unsigned char
	{
		CosmeticSkipped,
		Acquired,
	};

	struct BarPresentAttemptResult
	{
		BarPresentLeaseOutcome lease = BarPresentLeaseOutcome::CosmeticSkipped;
		HRESULT getDcHr = E_FAIL;
		BOOL updateLayeredWindowResult = FALSE;
		HRESULT releaseDcHr = E_FAIL;
		HRESULT endDrawHr = E_FAIL;
		RECT presentedBounds{};

		[[nodiscard]] static constexpr BarPresentAttemptResult CosmeticLeaseSkipped() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr BarPresentAttemptResult Acquired(
			HRESULT getDcResult,
			BOOL updateLayeredWindowResult,
			HRESULT releaseDcResult,
			HRESULT endDrawResult,
			const RECT& bounds) noexcept
		{
			return {
				BarPresentLeaseOutcome::Acquired,
				getDcResult,
				updateLayeredWindowResult,
				releaseDcResult,
				endDrawResult,
				bounds,
			};
		}

		[[nodiscard]] constexpr bool IsComplete() const noexcept
		{
			return lease == BarPresentLeaseOutcome::Acquired
				&& SUCCEEDED(getDcHr)
				&& updateLayeredWindowResult != FALSE
				&& SUCCEEDED(releaseDcHr)
				&& SUCCEEDED(endDrawHr);
		}

		[[nodiscard]] constexpr bool NeedsTargetRecreation() const noexcept
		{
			return getDcHr == D2DERR_RECREATE_TARGET
				|| releaseDcHr == D2DERR_RECREATE_TARGET
				|| endDrawHr == D2DERR_RECREATE_TARGET;
		}

		[[nodiscard]] constexpr bool HasSharedDeviceLoss() const noexcept
		{
			return IsBarSharedDeviceLost(getDcHr)
				|| IsBarSharedDeviceLost(releaseDcHr)
				|| IsBarSharedDeviceLost(endDrawHr);
		}

		[[nodiscard]] constexpr BarPresentFailureClass GetFailureClass() const noexcept
		{
			if (lease != BarPresentLeaseOutcome::Acquired)
				return BarPresentFailureClass::None;
			if (FAILED(getDcHr)) return BarPresentFailureClass::GetDc;
			if (updateLayeredWindowResult == FALSE)
				return BarPresentFailureClass::UpdateLayeredWindow;
			if (FAILED(releaseDcHr)) return BarPresentFailureClass::ReleaseDc;
			if (FAILED(endDrawHr)) return BarPresentFailureClass::EndDraw;
			return BarPresentFailureClass::None;
		}
	};

	enum class BarPresentCompletionKind : unsigned char
	{
		Deferred,
		Committed,
		Retry,
		RecreateTarget,
	};

	struct BarPresentCompletion
	{
		BarPresentCompletionKind kind = BarPresentCompletionKind::Deferred;

		[[nodiscard]] constexpr bool IsCommitted() const noexcept
		{
			return kind == BarPresentCompletionKind::Committed;
		}

		[[nodiscard]] constexpr bool NeedsTargetRecreation() const noexcept
		{
			return kind == BarPresentCompletionKind::RecreateTarget;
		}
	};

	class BarPresentDecision
	{
	public:
		constexpr explicit BarPresentDecision(const RECT& initialPresentedBounds = {}) noexcept
			: lastPresentedBounds(initialPresentedBounds)
		{
		}

		constexpr void AddDemand(const BarPresentDemand& demand) noexcept
		{
			pendingVisual = pendingVisual || demand.visual;
			pendingLighting = pendingLighting || demand.lighting;
			pendingRenderOnce = pendingRenderOnce || demand.renderOnce;
		}

		[[nodiscard]] constexpr bool ShouldPresent() const noexcept
		{
			return pendingVisual || pendingLighting || pendingRenderOnce;
		}

		[[nodiscard]] constexpr bool NeedsInteractivePass() const noexcept
		{
			return pendingVisual || pendingRenderOnce;
		}

		[[nodiscard]] constexpr bool NeedsFullDirty() const noexcept
		{
			return fullDirtyRequired;
		}

		[[nodiscard]] constexpr RECT LastPresentedBounds() const noexcept
		{
			return lastPresentedBounds;
		}

		[[nodiscard]] constexpr bool HasPendingVisual() const noexcept
		{
			return pendingVisual;
		}

		[[nodiscard]] constexpr bool HasPendingLighting() const noexcept
		{
			return pendingLighting;
		}

		[[nodiscard]] constexpr bool HasPendingRenderOnce() const noexcept
		{
			return pendingRenderOnce;
		}

		[[nodiscard]] constexpr bool CanAttemptPresent(
			std::uint64_t frameSerial) const noexcept
		{
			return activeFailureClass == BarPresentFailureClass::None
				|| frameSerial >= nextRetryFrame;
		}

		[[nodiscard]] constexpr bool HasFailureBackoff() const noexcept
		{
			return activeFailureClass != BarPresentFailureClass::None;
		}

		[[nodiscard]] constexpr BarPresentFailureClass ActiveFailureClass() const noexcept
		{
			return activeFailureClass;
		}

		[[nodiscard]] constexpr unsigned int ConsecutiveFailureCount() const noexcept
		{
			return consecutiveFailureCount;
		}

		[[nodiscard]] constexpr std::uint64_t RetryDelayFrames() const noexcept
		{
			return retryDelayFrames;
		}

		[[nodiscard]] constexpr std::uint64_t NextRetryFrame() const noexcept
		{
			return nextRetryFrame;
		}

		constexpr void ObserveDemandGeneration(
			std::uint64_t demandGeneration) noexcept
		{
			if (HasFailureBackoff()
				&& demandGeneration != failureDemandGeneration)
				ResetFailureRecovery();
		}

		constexpr void ObserveDeviceGeneration(
			std::uint64_t deviceGeneration) noexcept
		{
			if (HasFailureBackoff()
				&& deviceGeneration != failureDeviceGeneration)
				ResetFailureRecovery();
		}

		constexpr void RecordFailure(BarPresentFailureClass failureClass,
			std::uint64_t deviceGeneration,
			std::uint64_t demandGeneration,
			std::uint64_t frameSerial) noexcept
		{
			if (failureClass == BarPresentFailureClass::None)
			{
				ResetFailureRecovery();
				return;
			}

			const bool sameFailureSeries = activeFailureClass == failureClass
				&& failureDeviceGeneration == deviceGeneration
				&& failureDemandGeneration == demandGeneration;
			if (!sameFailureSeries) consecutiveFailureCount = 0;
			if (consecutiveFailureCount < MaximumBackoffFailureCount)
				++consecutiveFailureCount;

			activeFailureClass = failureClass;
			failureDeviceGeneration = deviceGeneration;
			failureDemandGeneration = demandGeneration;
			retryDelayFrames = CalculateRetryDelayFrames(consecutiveFailureCount);
			const std::uint64_t maximumFrame =
				(std::numeric_limits<std::uint64_t>::max)();
			nextRetryFrame = frameSerial > maximumFrame - retryDelayFrames
				? maximumFrame : frameSerial + retryDelayFrames;
		}

		constexpr void ResetFailureRecovery() noexcept
		{
			activeFailureClass = BarPresentFailureClass::None;
			failureDeviceGeneration = 0;
			failureDemandGeneration = 0;
			consecutiveFailureCount = 0;
			retryDelayFrames = 0;
			nextRetryFrame = 0;
		}

		constexpr void RequireFullDirtyRetry() noexcept
		{
			fullDirtyRequired = true;
		}

		[[nodiscard]] constexpr BarPresentCompletion CompleteAttempt(
			const BarPresentAttemptResult& result,
			std::uint64_t deviceGeneration = 0,
			std::uint64_t demandGeneration = 0,
			std::uint64_t frameSerial = 0) noexcept
		{
			if (result.lease == BarPresentLeaseOutcome::CosmeticSkipped)
			{
				// 装饰帧让出租约只延后提交，不能吞掉最后一次可见状态。
				return { BarPresentCompletionKind::Deferred };
			}

			if (!result.IsComplete())
			{
				// 任一提交阶段失败都保留请求，并要求下一次覆盖完整目标。
				fullDirtyRequired = true;
				RecordFailure(result.GetFailureClass(), deviceGeneration,
					demandGeneration, frameSerial);
				return { result.NeedsTargetRecreation()
					? BarPresentCompletionKind::RecreateTarget
					: BarPresentCompletionKind::Retry };
			}

			// pending 与已呈现边界只在完整事务成功后一起推进。
			ResetFailureRecovery();
			lastPresentedBounds = result.presentedBounds;
			pendingVisual = false;
			pendingLighting = false;
			pendingRenderOnce = false;
			fullDirtyRequired = false;
			return { BarPresentCompletionKind::Committed };
		}

	private:
		[[nodiscard]] static constexpr std::uint64_t CalculateRetryDelayFrames(
			unsigned int failureCount) noexcept
		{
			if (failureCount == 0) return 0;
			const unsigned int shift = failureCount > 6 ? 6 : failureCount - 1;
			const std::uint64_t delay = std::uint64_t{ 1 } << shift;
			return delay < MaximumRetryDelayFrames
				? delay : MaximumRetryDelayFrames;
		}

		static constexpr unsigned int MaximumBackoffFailureCount = 7;
		static constexpr std::uint64_t MaximumRetryDelayFrames = 60;
		RECT lastPresentedBounds{};
		bool pendingVisual = false;
		bool pendingLighting = false;
		bool pendingRenderOnce = false;
		bool fullDirtyRequired = false;
		BarPresentFailureClass activeFailureClass = BarPresentFailureClass::None;
		std::uint64_t failureDeviceGeneration = 0;
		std::uint64_t failureDemandGeneration = 0;
		unsigned int consecutiveFailureCount = 0;
		std::uint64_t retryDelayFrames = 0;
		std::uint64_t nextRetryFrame = 0;
	};
}
