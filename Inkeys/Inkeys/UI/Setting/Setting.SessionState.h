#pragma once

#include <dxgi.h>

#include <cstdint>
#include <utility>

namespace Inkeys::UI::Setting
{
	class StartupPreviewPreference final
	{
	public:
		explicit constexpr StartupPreviewPreference(bool enabled = true) noexcept
			: startupEnabled_(enabled), configuredEnabled_(enabled) {}

		[[nodiscard]] constexpr bool StartupEnabled() const noexcept
		{
			return startupEnabled_;
		}

		[[nodiscard]] constexpr bool ConfiguredEnabled() const noexcept
		{
			return configuredEnabled_;
		}

		[[nodiscard]] constexpr bool SetConfigured(bool enabled) noexcept
		{
			if (configuredEnabled_ == enabled) return false;
			configuredEnabled_ = enabled;
			writePending_ = true;
			return true;
		}

		[[nodiscard]] constexpr bool ConsumeWritePending() noexcept
		{
			return std::exchange(writePending_, false);
		}

	private:
		bool startupEnabled_ = true;
		bool configuredEnabled_ = true;
		bool writePending_ = false;
	};

	[[nodiscard]] constexpr bool IsSharedDeviceLoss(HRESULT result) noexcept
	{
		return result == DXGI_ERROR_DEVICE_REMOVED
			|| result == DXGI_ERROR_DEVICE_RESET
			|| result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	}

	struct ResizeSnapshot
	{
		std::uint64_t serial = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct BusinessCompletionSnapshot
	{
		std::uint64_t serial = 0;
		bool succeeded = true;
	};

	struct SessionDecision
	{
		bool release = false;
		bool rebuild = false;
		bool resize = false;
		bool probeOcclusion = false;
		bool render = false;
		bool consumeBusinessCompletion = false;
	};

	class SessionState
	{
	public:
		void SetVisible(bool visible) noexcept { visible_ = visible; }
		[[nodiscard]] bool IsVisible() const noexcept { return visible_; }
		void SetOccluded(bool occluded) noexcept { occluded_ = occluded; }
		void QueueResize(std::uint32_t width, std::uint32_t height) noexcept
		{
			if (!width || !height) return;
			++resizeSerial_;
			resizeWidth_ = width;
			resizeHeight_ = height;
		}
		void PublishBusinessCompletion(
			std::uint64_t serial, bool succeeded = true) noexcept
		{
			businessCompletion_ = serial;
			businessSucceeded_ = succeeded;
		}

		[[nodiscard]] SessionDecision Resolve(
			std::uint64_t epoch, bool hasSession) const noexcept
		{
			SessionDecision decision;
			decision.release = hasSession && (!visible_ || (epoch_ && epoch_ != epoch));
			decision.rebuild = visible_ && (!hasSession || decision.release);
			decision.resize = visible_ && hasSession
				&& resizeSerial_ != consumedResizeSerial_;
			decision.probeOcclusion = visible_ && hasSession && occluded_;
			decision.render = visible_ && !decision.probeOcclusion;
			decision.consumeBusinessCompletion =
				businessCompletion_ != consumedBusinessCompletion_;
			return decision;
		}

		[[nodiscard]] ResizeSnapshot Resize() const noexcept
		{
			return { resizeSerial_, resizeWidth_, resizeHeight_ };
		}

		[[nodiscard]] BusinessCompletionSnapshot BusinessCompletion() const noexcept
		{
			return { businessCompletion_, businessSucceeded_ };
		}

		void CommitEpoch(std::uint64_t epoch) noexcept
		{
			epoch_ = epoch;
		}

		void ConsumeResize(std::uint64_t serial) noexcept
		{
			if (serial > consumedResizeSerial_ && serial <= resizeSerial_)
				consumedResizeSerial_ = serial;
		}

		void ConsumeBusinessCompletion(std::uint64_t serial) noexcept
		{
			if (serial > consumedBusinessCompletion_
				&& serial <= businessCompletion_)
				consumedBusinessCompletion_ = serial;
		}

		void Release() noexcept
		{
			epoch_ = 0;
			occluded_ = false;
		}

	private:
		bool visible_ = false;
		bool occluded_ = false;
		std::uint64_t epoch_ = 0;
		std::uint64_t businessCompletion_ = 0;
		std::uint64_t consumedBusinessCompletion_ = 0;
		std::uint64_t resizeSerial_ = 0;
		std::uint64_t consumedResizeSerial_ = 0;
		bool businessSucceeded_ = true;
		std::uint32_t resizeWidth_ = 0;
		std::uint32_t resizeHeight_ = 0;
	};
}
