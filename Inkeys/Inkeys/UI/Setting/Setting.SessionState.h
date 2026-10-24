#pragma once

#include <dxgi.h>

#include <cstdint>
#include <utility>

namespace Inkeys::UI::Setting
{
	enum class BarButtonClickAction : std::uint8_t
	{
		ShowAndActivate,
		Activate,
		Hide,
	};

	[[nodiscard]] constexpr BarButtonClickAction ResolveBarButtonClickAction(
		bool visible, bool focused) noexcept
	{
		if (!visible) return BarButtonClickAction::ShowAndActivate;
		return focused ? BarButtonClickAction::Hide
			: BarButtonClickAction::Activate;
	}

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

	[[nodiscard]] constexpr bool ShouldPlaySettingEntryAnimation(
		int mode, std::uint64_t showSerial) noexcept
	{
		// 首次机会按真实 Show 序号消耗，尺寸跳过、取消及资源重建都不能补播。
		if (mode < 0 || mode > 2) mode = 1;
		return showSerial != 0 && (mode == 2 || (mode == 1 && showSerial == 1));
	}

	struct LinkedPptScaleDraft
	{
		float bottom;
		float middle;
		bool bottomLinked;
		bool middleLinked;
	};

	[[nodiscard]] constexpr LinkedPptScaleDraft ResolveLinkedPptScaleDraft(
		LinkedPptScaleDraft draft, bool bottomEdited, bool middleEdited,
		bool bottomJoined, bool middleJoined) noexcept
	{
		if (bottomJoined && draft.middleLinked) draft.bottom = draft.middle;
		if (middleJoined && draft.bottomLinked) draft.middle = draft.bottom;
		// 只使用本帧真实编辑；松手前的旧保存 Record 不能把连续拖动回滚。
		if (draft.bottomLinked && draft.middleLinked)
		{
			if (bottomEdited) draft.middle = draft.bottom;
			else if (middleEdited) draft.bottom = draft.middle;
		}
		return draft;
	}

	[[nodiscard]] constexpr bool IsSharedDeviceLoss(HRESULT result) noexcept
	{
		return result == DXGI_ERROR_DEVICE_REMOVED
			|| result == DXGI_ERROR_DEVICE_RESET
			|| result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	}

	[[nodiscard]] constexpr bool IsSharedDeviceLoss(HRESULT result, HRESULT removedReason) noexcept
	{
		// GetDeviceRemovedReason 的失败本身证明 lease 已失效，包括 HUNG/INVALID_CALL 等原因。
		return IsSharedDeviceLoss(result) || FAILED(removedReason);
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
		bool resetInput = false;
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
		void SetVisible(bool visible) noexcept
		{
			if (visible && !visible_) ++showSerial_;
			if (!visible && visible_) ++hideSerial_;
			visible_ = visible;
			if (!visible) InvalidateSession();
		}
		[[nodiscard]] std::uint64_t ShowSerial() const noexcept { return showSerial_; }
		[[nodiscard]] std::uint64_t HideSerial() const noexcept { return hideSerial_; }
		[[nodiscard]] bool IsVisible() const noexcept { return visible_; }
		// 原生 Hide 可能晚于新会话；只失效输入，不覆盖更新的 Show 意图。
		void InvalidateSession() noexcept { ++inputResetSerial_; }
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
			decision.resetInput = hasResident && inputResetSerial_ != consumedInputResetSerial_;
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

		[[nodiscard]] std::uint64_t InputResetSerial() const noexcept { return inputResetSerial_; }
		void ConsumeInputReset(std::uint64_t serial) noexcept
		{
			// 只消费本帧观察的 Hide，后到的原生通知留给下一帧清理。
			if (serial > consumedInputResetSerial_ && serial <= inputResetSerial_)
				consumedInputResetSerial_ = serial;
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
			// 只在旧会话已销毁或确认无旧会话的新建边界消费，不由旧帧提交清除。
			consumedInputResetSerial_ = inputResetSerial_;
		}

		void ReleasePresentation() noexcept { occluded_ = false; }

	private:
		bool visible_ = false;
		bool occluded_ = false;
		std::uint64_t inputResetSerial_ = 0;
		std::uint64_t consumedInputResetSerial_ = 0;
		std::uint64_t epoch_ = 0;
		std::uint64_t showSerial_ = 0;
		std::uint64_t hideSerial_ = 0;
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
