#pragma once

#include <windows.h>
#include <directmanipulation.h>
#include <interactioncontext.h>
#include "Draw3.TouchpadNavigation.h"

namespace Inkeys::Drawing::Draw3::Detail
{
	// 按 Microsoft Precision Touchpad Reference 补写 SDK 26100 缺少的 ABI。
	// 帧 API 文档明确使用 POINTER_TOUCH_INFO；完整联合体 POINTER_TYPE_INFO 已由 WinUser.h 提供。
	constexpr UINT kStopContentInertia = 0x023B, kEndContentInertia = 0x023C;
	using RegisterTouchpadWindow = BOOL (WINAPI*)(HWND, BOOL);
	using ReadTouchpadHistory = BOOL (WINAPI*)(UINT32, UINT32*, UINT32*, POINTER_TOUCH_INFO*);
	using ProcessTouchpadFrames = HRESULT (WINAPI*)(HINTERACTIONCONTEXT, UINT32, UINT32, const POINTER_TYPE_INFO*);
	using ReportContentInertia = BOOL (WINAPI*)(HWND, BOOL);

	struct TouchpadInteractionApi
	{
		HMODULE library = nullptr;
		RegisterTouchpadWindow registration = nullptr;
		ReadTouchpadHistory history = nullptr;
		ProcessTouchpadFrames frames = nullptr;
		ReportContentInertia report = nullptr;
		decltype(&GetPointerInfo) info = nullptr;
		decltype(&CreateInteractionContext) create = nullptr;
		decltype(&DestroyInteractionContext) destroy = nullptr;
		decltype(&RegisterOutputCallbackInteractionContext) callback = nullptr;
		decltype(&SetInteractionConfigurationInteractionContext) configure = nullptr;
		decltype(&SetPropertyInteractionContext) property = nullptr;
		decltype(&SetTranslationParameterInteractionContext) translation = nullptr;
		decltype(&ProcessInertiaInteractionContext) inertia = nullptr;
		decltype(&StopInteractionContext) stop = nullptr;

		TouchpadInteractionApi() = default;
		TouchpadInteractionApi(const TouchpadInteractionApi&) = delete;
		TouchpadInteractionApi& operator=(const TouchpadInteractionApi&) = delete;
		~TouchpadInteractionApi() { if (library) FreeLibrary(library); }

		bool Load() noexcept
		{
			const HMODULE user = GetModuleHandleW(L"user32.dll");
			library = LoadLibraryExW(L"ninput.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!user || !library) return false;
			registration = Resolve<RegisterTouchpadWindow>(user, "RegisterTouchpadCapableWindow");
			history = Resolve<ReadTouchpadHistory>(user, "GetPointerFrameTouchpadInfoHistory");
			report = Resolve<ReportContentInertia>(user, "ReportWindowContentInertia");
			info = Resolve<decltype(info)>(user, "GetPointerInfo");
			create = Resolve<decltype(create)>(library, "CreateInteractionContext");
			destroy = Resolve<decltype(destroy)>(library, "DestroyInteractionContext");
			callback = Resolve<decltype(callback)>(library, "RegisterOutputCallbackInteractionContext");
			configure = Resolve<decltype(configure)>(library, "SetInteractionConfigurationInteractionContext");
			property = Resolve<decltype(property)>(library, "SetPropertyInteractionContext");
			translation = Resolve<decltype(translation)>(library, "SetTranslationParameterInteractionContext");
			inertia = Resolve<decltype(inertia)>(library, "ProcessInertiaInteractionContext");
			stop = Resolve<decltype(stop)>(library, "StopInteractionContext");
			frames = Resolve<ProcessTouchpadFrames>(library, "ProcessPointerFramesInteractionContext2");
			// 部分 Win11 只按官方序号导出。旧版帧函数丢失 touchInfo，无法遵从系统触控板设置。
			if (!frames && registration && history)
			{
				const auto address = GetProcAddress(library, MAKEINTRESOURCEA(2507));
				MEMORY_BASIC_INFORMATION page{};
				// GetProcAddress 对稀疏序号表的空洞可能返回非空值，只接受可执行入口。
				if (address && VirtualQuery(reinterpret_cast<LPCVOID>(address), &page, sizeof(page)) &&
					(page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
					frames = reinterpret_cast<ProcessTouchpadFrames>(address);
			}
			return registration && history && frames && info && create && destroy && callback &&
				configure && property && translation && inertia && stop;
		}
	private:
		template<class Fn> static Fn Resolve(HMODULE library, LPCSTR name) noexcept
		{ return reinterpret_cast<Fn>(GetProcAddress(library, name)); }
	};

	class TouchpadInteractionOutput
	{
	public:
		explicit TouchpadInteractionOutput(TouchpadNavigationBridge& bridge) : bridge_(bridge) {}

		void Begin(std::uint64_t revision) noexcept
		{
			epoch_ = bridge_.Epoch(); revision_ = revision;
			x_ = y_ = 0; running_ = gliding_ = frameKnown_ = false;
		}
		bool Current(std::uint64_t revision) const noexcept
		{ return epoch_ == bridge_.Epoch() && revision == revision_ && bridge_.enabled.load(); }
		bool NewFrame(const POINTER_INFO& info) noexcept
		{
			if (frameKnown_ && device_ == info.sourceDevice && frame_ == info.frameId) return false;
			previousKnown_ = frameKnown_ && device_ == info.sourceDevice; previousFrame_ = frame_;
			frameKnown_ = true; device_ = info.sourceDevice; frame_ = info.frameId;
			return true;
		}
		UINT32 NewHistoryFrames(const POINTER_TOUCH_INFO* history, UINT32 frames, UINT32 pointers) const noexcept
		{
			// 历史由新到旧；上一批的最新帧及其后的旧帧都已经送过识别器。
			if (previousKnown_)
				for (UINT32 i = 0; i < frames; ++i)
					if (history[static_cast<std::size_t>(i) * pointers].pointerInfo.frameId == previousFrame_) return i;
			return frames;
		}
		bool Gliding() const noexcept { return gliding_; }
		void End(std::int64_t qpc) noexcept
		{
			if (running_) bridge_.Publish(epoch_, TouchpadNavigationPhase::Cancelled, x_, y_, qpc, revision_);
			epoch_ = 0; running_ = gliding_ = false;
		}
		bool Apply(const INTERACTION_CONTEXT_OUTPUT& output, std::int64_t qpc) noexcept
		{
			if (!Current(revision_)) return false;
			if (output.interactionId != INTERACTION_ID_MANIPULATION) return true;
			const auto flags = output.interactionFlags;
			const bool inertia = (flags & INTERACTION_FLAG_INERTIA) != 0;
			if ((flags & INTERACTION_FLAG_CANCEL) || ((inertia || gliding_) && !bridge_.inertiaAllowed.load()))
			{ End(qpc); return false; }
			if ((flags & INTERACTION_FLAG_BEGIN) && !running_)
			{ running_ = true; gliding_ = false; x_ = y_ = 0; }
			if (!running_) return true;
			// IC 已按显示器缩放输出屏幕像素，补发 Down 的绝对位置和其它变换均不参与累计。
			const auto& delta = output.arguments.manipulation.delta;
			const double nextX = x_ + delta.translationX, nextY = y_ + delta.translationY;
			if (!std::isfinite(nextX) || !std::isfinite(nextY)) { End(qpc); return false; }
			x_ = nextX; y_ = nextY;
			gliding_ = gliding_ || inertia;
			if (!bridge_.Publish(epoch_, gliding_ ? TouchpadNavigationPhase::Inertia : TouchpadNavigationPhase::Running,
				x_, y_, qpc, revision_)) return false;
			if (flags & INTERACTION_FLAG_END)
			{
				running_ = gliding_ = false;
				return bridge_.Publish(epoch_, TouchpadNavigationPhase::Ready, x_, y_, qpc, revision_);
			}
			return true;
		}
	private:
		TouchpadNavigationBridge& bridge_;
		std::uint64_t epoch_ = 0, revision_ = 0;
		double x_ = 0, y_ = 0;
		HANDLE device_ = nullptr;
		UINT32 frame_ = 0, previousFrame_ = 0;
		bool frameKnown_ = false, previousKnown_ = false, running_ = false, gliding_ = false;
	};

	// MANUALUPDATE 只控制输出节拍；显式命中和 INPUT 才保护原 RTS/Mouse 输入所有权。
	template<class Viewport>
	HRESULT ConfigureTouchpadViewport(Viewport& viewport)
	{
		const auto configuration = DIRECTMANIPULATION_CONFIGURATION_INTERACTION |
			DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y |
			DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA;
		const HRESULT configured = viewport.ActivateConfiguration(configuration);
		if (FAILED(configured)) return configured;
		return viewport.SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE |
			DIRECTMANIPULATION_VIEWPORT_OPTIONS_DISABLEPIXELSNAPPING |
			DIRECTMANIPULATION_VIEWPORT_OPTIONS_INPUT | DIRECTMANIPULATION_VIEWPORT_OPTIONS_EXPLICITHITTEST);
	}

	template<class Associate>
	bool DispatchTouchpadHitTest(bool admitted, bool typeKnown, POINTER_INPUT_TYPE type,
		UINT32 pointer, Associate associate)
	{
		return admitted && typeKnown && type == PT_TOUCHPAD && associate(pointer);
	}
}
