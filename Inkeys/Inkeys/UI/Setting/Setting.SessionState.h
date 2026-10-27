#pragma once

#include <dxgi.h>
#include <dbt.h>

#include <cstdint>
#include <mutex>
#include <utility>

namespace Inkeys::UI::Setting
{
	template <typename Callback>
	decltype(auto) WithSettingInputUnlocked(
		std::unique_lock<std::recursive_mutex>& inputLock, Callback&& callback)
	{
		struct RestoreInputLock
		{
			std::unique_lock<std::recursive_mutex>& lock;
			~RestoreInputLock() { lock.lock(); }
		};
		// DXGI Present 可能等待窗口线程；释放 IO 锁让焦点/输入消息完成，返回后再读 ImGui。
		inputLock.unlock();
		const RestoreInputLock restore{ inputLock };
		return std::forward<Callback>(callback)();
	}

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

	[[nodiscard]] constexpr UINT ResolveSettingClientDpi(
		UINT windowDpi, UINT messageDpi, UINT monitorDpi, UINT systemDpi) noexcept
	{
		if (windowDpi) return windowDpi;
		if (messageDpi) return messageDpi;
		if (monitorDpi) return monitorDpi;
		return systemDpi ? systemDpi : USER_DEFAULT_SCREEN_DPI;
	}

	struct SettingSystemMenuState
	{
		bool restore, move, size, minimize, maximize;
	};

	[[nodiscard]] constexpr SettingSystemMenuState ResolveSettingSystemMenuState(
		bool minimized, bool maximized, bool canSize, bool canMinimize, bool canMaximize) noexcept
	{
		const bool normal = !minimized && !maximized;
		return { !normal, normal, normal && canSize,
			!minimized && canMinimize, !maximized && canMaximize };
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

	enum class SettingAccentState : int
	{
		Disabled = 0,
		BlurBehind = 3,
		AcrylicBlurBehind = 4,
	};

	[[nodiscard]] constexpr SettingAccentState ResolveSettingAccentState(
		bool enabled, InteractiveWindowOperation operation) noexcept
	{
		if (!enabled) return SettingAccentState::Disabled;
		return operation == InteractiveWindowOperation::None
			? SettingAccentState::AcrylicBlurBehind : SettingAccentState::BlurBehind;
	}

	[[nodiscard]] constexpr bool IsSettingImGuiInputMessage(
		UINT message, WPARAM wParam, LPARAM lParam) noexcept
	{
		// 只允许真实 backend 输入访问 IO；移动/尺寸等原生消息不得等待渲染锁。
		switch (message)
		{
		case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
		case WM_MOUSELEAVE: case WM_NCMOUSELEAVE:
		case WM_DESTROY:
		case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
		case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
		case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
		case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
		case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
		case WM_SETFOCUS: case WM_KILLFOCUS: case WM_INPUTLANGCHANGE:
		case WM_CHAR: case WM_IME_COMPOSITION: case WM_IME_CHAR:
			return true;
		case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
			return wParam < 256;
		case WM_SETCURSOR:
			return LOWORD(lParam) == HTCLIENT;
		case WM_DEVICECHANGE:
			return wParam == DBT_DEVNODES_CHANGED;
		default:
			return false;
		}
	}

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

		[[nodiscard]] constexpr bool HasPresentationWork() const noexcept
		{
			return render || probeOcclusion;
		}
	};

	enum class SettingOpenTarget : std::uint8_t { Current, SpeedEraser };
	struct SettingOpenRequest
	{
		std::uint64_t serial = 0, showSerial = 0, hideSerial = 0;
		SettingOpenTarget target = SettingOpenTarget::Current;
		bool opening = false;
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
		void RequestShow(SettingOpenTarget target) noexcept
		{
			const bool opening = !visible_;
			SetVisible(true);
			// 目标与真实 Show/Hide 身份同批发布；普通激活不额外消耗开屏机会。
			openRequest_ = { openRequest_.serial + 1, showSerial_, hideSerial_, target, opening };
		}
		[[nodiscard]] SettingOpenRequest GetOpenRequest() const noexcept { return openRequest_; }
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
		SettingOpenRequest openRequest_;
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
