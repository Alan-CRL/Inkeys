#pragma once

#include <dxgi.h>

#include <cstdint>

namespace Inkeys::UI::Setting
{
	[[nodiscard]] constexpr bool IsSharedDeviceLoss(HRESULT result) noexcept
	{
		return result == DXGI_ERROR_DEVICE_REMOVED
			|| result == DXGI_ERROR_DEVICE_RESET
			|| result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	}

	enum class InteractiveWindowOperation : std::uint8_t
	{
		None,
		Move,
		Size,
	};

	[[nodiscard]] constexpr InteractiveWindowOperation
		InteractiveOperationFromHitTest(LRESULT hit) noexcept
	{
		if (hit == HTCAPTION) return InteractiveWindowOperation::Move;
		switch (hit)
		{
		case HTLEFT:
		case HTRIGHT:
		case HTTOP:
		case HTBOTTOM:
		case HTTOPLEFT:
		case HTTOPRIGHT:
		case HTBOTTOMLEFT:
		case HTBOTTOMRIGHT:
			return InteractiveWindowOperation::Size;
		default:
			return InteractiveWindowOperation::None;
		}
	}

	[[nodiscard]] constexpr InteractiveWindowOperation
		InteractiveOperationFromSystemCommand(WPARAM command) noexcept
	{
		switch (command & 0xFFF0U)
		{
		case SC_MOVE: return InteractiveWindowOperation::Move;
		case SC_SIZE: return InteractiveWindowOperation::Size;
		default: return InteractiveWindowOperation::None;
		}
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
		bool initializeResident = false;
		bool rebuildDeviceResources = false;
		bool rebuildFonts = false;
		bool releasePresentation = false;
		bool createPresentation = false;
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
		void QueueFontRebuild() noexcept { ++fontRebuildSerial_; }
		void PublishBusinessCompletion(
			std::uint64_t serial, bool succeeded = true) noexcept
		{
			businessCompletion_ = serial;
			businessSucceeded_ = succeeded;
		}

		[[nodiscard]] SessionDecision Resolve(
			std::uint64_t epoch, bool hasResident,
			bool hasPresentation) const noexcept
		{
			SessionDecision decision;
			const bool epochChanged = hasResident && epoch_ && epoch_ != epoch;
			decision.initializeResident = !hasResident;
			decision.rebuildDeviceResources = epochChanged;
			decision.rebuildFonts = hasResident
				&& fontRebuildSerial_ != consumedFontRebuildSerial_;
			decision.releasePresentation = hasPresentation
				&& (!visible_ || epochChanged);
			decision.createPresentation = visible_
				&& (!hasPresentation || decision.releasePresentation);
			decision.resize = visible_ && hasPresentation
				&& !decision.releasePresentation
				&& resizeSerial_ != consumedResizeSerial_;
			decision.probeOcclusion = visible_ && hasPresentation
				&& !decision.releasePresentation && occluded_;
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

		[[nodiscard]] std::uint64_t FontRebuildSerial() const noexcept
		{
			return fontRebuildSerial_;
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

		void ConsumeFontRebuild(std::uint64_t serial) noexcept
		{
			if (serial > consumedFontRebuildSerial_
				&& serial <= fontRebuildSerial_)
				consumedFontRebuildSerial_ = serial;
		}

		void ConsumeBusinessCompletion(std::uint64_t serial) noexcept
		{
			if (serial > consumedBusinessCompletion_
				&& serial <= businessCompletion_)
				consumedBusinessCompletion_ = serial;
		}

		void ReleaseResident() noexcept
		{
			epoch_ = 0;
			occluded_ = false;
		}

		void ReleasePresentation() noexcept { occluded_ = false; }

	private:
		bool visible_ = false;
		bool occluded_ = false;
		std::uint64_t epoch_ = 0;
		std::uint64_t businessCompletion_ = 0;
		std::uint64_t consumedBusinessCompletion_ = 0;
		std::uint64_t resizeSerial_ = 0;
		std::uint64_t consumedResizeSerial_ = 0;
		std::uint64_t fontRebuildSerial_ = 0;
		std::uint64_t consumedFontRebuildSerial_ = 0;
		bool businessSucceeded_ = true;
		std::uint32_t resizeWidth_ = 0;
		std::uint32_t resizeHeight_ = 0;
	};
}
