#pragma once

#include "Draw3.Bridge.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Inkeys::Drawing::Draw3
{
	enum class DCompRetirementResult : std::uint8_t
	{
		Completed,
		Failed,
		Timeout,
		Busy,
	};
	struct DCompRetirementSlot
	{
		struct State
		{
			bool completed = false;
			bool succeeded = false;
		};
		std::mutex mutex;
		std::condition_variable wake;
		std::shared_ptr<State> active;
	};
	inline DCompRetirementSlot& GetDCompRetirementSlot() noexcept
	{
		static auto* slot = new DCompRetirementSlot;
		return *slot;
	}
	template<class Wait>
	[[nodiscard]] DCompRetirementResult WaitForDCompRetirement(Wait wait,
		std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
	{
		auto& slot = GetDCompRetirementSlot();
		std::shared_ptr<DCompRetirementSlot::State> state;
		{
			std::scoped_lock lock(slot.mutex);
			if (slot.active) return DCompRetirementResult::Busy;
			state = std::make_shared<DCompRetirementSlot::State>();
			slot.active = state;
		}
		try
		{
			std::thread([state, &slot, wait = std::move(wait)]() mutable
			{
				bool succeeded = false;
				try { succeeded = wait(); }
				catch (...) {}
				{
					std::scoped_lock lock(slot.mutex);
					state->succeeded = succeeded;
					state->completed = true;
					if (slot.active == state) slot.active.reset();
				}
				slot.wake.notify_all();
			}).detach();
		}
		catch (...)
		{
			std::scoped_lock lock(slot.mutex);
			if (slot.active == state) slot.active.reset();
			return DCompRetirementResult::Failed;
		}
		std::unique_lock lock(slot.mutex);
		if (!slot.wake.wait_for(lock, timeout, [&] { return state->completed; }))
			return DCompRetirementResult::Timeout;
		return state->succeeded ? DCompRetirementResult::Completed : DCompRetirementResult::Failed;
	}

	// DComp 输出交接只在目标 revision 的首个成功全帧 Present 后消费一次屏障。
	struct DCompHandoffBarrierState
	{
		std::uint64_t pendingOutputRevision = 0;

		void Arm(std::uint64_t outputRevision) noexcept
		{
			pendingOutputRevision = outputRevision;
		}

		void Cancel() noexcept
		{
			pendingOutputRevision = 0;
		}

		bool Consume(bool directComposition, bool presentFull,
			bool presentSucceeded, std::uint64_t outputRevision) noexcept
		{
			if (!directComposition || !presentFull || !presentSucceeded ||
				pendingOutputRevision == 0 || pendingOutputRevision != outputRevision)
				return false;
			pendingOutputRevision = 0;
			return true;
		}
	};

	enum class PresentationDescriptorStatus : std::uint8_t
	{
		StableSlideIds,
		PageIndexFallback,
		TransientBusy,
		Unavailable,
	};

	enum class PresentationTargetDisposition : std::uint8_t
	{
		Publish,
		PreservePrevious,
		Isolate,
	};

	constexpr PresentationTargetDisposition ResolvePresentationTargetDisposition(
		PresentationDescriptorStatus status, bool pageAndTopologyMatch,
		std::uint64_t descriptorBindingRevision,
		std::optional<std::uint64_t> currentBindingRevision) noexcept
	{
		if (status == PresentationDescriptorStatus::Unavailable)
			return PresentationTargetDisposition::Isolate;
		if (status != PresentationDescriptorStatus::TransientBusy &&
			pageAndTopologyMatch) return PresentationTargetDisposition::Publish;
		return currentBindingRevision &&
			*currentBindingRevision == descriptorBindingRevision
			? PresentationTargetDisposition::PreservePrevious
			: PresentationTargetDisposition::Isolate;
	}

	struct PresentationDescriptor
	{
		PresentationDescriptorStatus status =
			PresentationDescriptorStatus::Unavailable;
		std::string provider;
		std::string fullName;
		std::string presentationName;
		std::int32_t applicationProcessId = 0;
		std::int64_t slideShowHwnd = 0;
		std::uint32_t currentPage = 0;
		std::uint32_t totalPage = 0;
		std::optional<std::int32_t> currentSlideId;
		std::vector<std::int32_t> slideIds;
		std::uint64_t bindingRevision = 0;
	};

	struct PresentationDescriptorParseResult
	{
		std::optional<PresentationDescriptor> descriptor;
		std::string error;
	};

	PresentationDescriptorParseResult ParsePresentationDescriptorJson(
		std::wstring_view json) noexcept;
	std::optional<Bridge::PresentationTarget> ResolvePresentationTarget(
		const PresentationDescriptor& descriptor) noexcept;
	std::optional<Bridge::PresentationTarget> ResolveEndScreenTarget(
		const PresentationDescriptor& descriptor) noexcept;
	bool CanUpgradePresentationBindingByOrdinal(
		const Bridge::PresentationTarget& previous,
		const Bridge::PresentationTarget& next,
		std::size_t documentPageCount) noexcept;
	bool CanReusePresentationDocumentSlot(
		const Bridge::PresentationTarget& previous,
		const Bridge::PresentationTarget& next,
		std::size_t documentPageCount) noexcept;
	bool StablePresentationTopologyChanged(
		const Bridge::PresentationTarget& previous,
		const Bridge::PresentationTarget& next) noexcept;
	std::string FormatPresentationKey(const Bridge::PresentationKey& key);
}
