#include "Draw3.HiddenWindowTest.h"
#include "Draw3.Product.h"

import Inkeys.Window;

#include <atomic>
#include <chrono>
#include <cstdio>
#include <crtdbg.h>
#include <string>
#include <thread>
#include <vector>

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		using namespace std::chrono_literals;

		struct StyleContext
		{
			Inkeys::Window::Service* service = nullptr;
			std::atomic<std::uint64_t> callCount = 0;
		};

		bool ApplyDrawpadStyle(void* context, DWORD setMask, DWORD clearMask)
		{
			auto* style = static_cast<StyleContext*>(context);
			if (!style || !style->service) return false;
			style->callCount.fetch_add(1, std::memory_order_acq_rel);
			return style->service->SetExtendedStyleFlags(
				Inkeys::Window::WindowRole::Drawpad, setMask, clearMask);
		}

		void Report(const char* prefix, const char* name)
		{
			char message[256]{};
			std::snprintf(message, sizeof(message), "[Draw3Hidden] %s: %s\n", prefix, name);
			std::fputs(message, stderr);
			OutputDebugStringA(message);
		}

		bool Check(bool condition, const char* name, int& failures)
		{
			if (condition) return true;
			++failures;
			Report("FAIL", name);
			return false;
		}

		template <typename Predicate>
		bool WaitUntil(Predicate&& predicate, std::chrono::milliseconds timeout = 15s)
		{
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			do
			{
				if (predicate()) return true;
				std::this_thread::sleep_for(10ms);
			} while (std::chrono::steady_clock::now() < deadline);
			return predicate();
		}

		Inkeys::Window::WindowSpec MakeHiddenSpec(Inkeys::Window::WindowRole role,
			const std::wstring& className, WNDPROC windowProc, int width, int height)
		{
			Inkeys::Window::WindowSpec spec;
			spec.role = role;
			spec.className = className;
			spec.title = L"Inkeys Draw3 hidden integration test";
			spec.x = 32;
			spec.y = 48;
			spec.width = width;
			spec.height = height;
			spec.style = WS_POPUP | WS_CLIPCHILDREN;
			spec.exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT;
			spec.windowProc = windowProc;
			spec.visible = false;
			spec.bindMessages = false;
		return spec;
		}

		bool CheckPresentationStyle(HWND drawpad, HostPresentationMode mode, int& failures)
		{
			const auto style = static_cast<DWORD>(GetWindowLongPtrW(drawpad, GWL_EXSTYLE));
			bool correctModeStyle = false;
			switch (mode)
			{
			case HostPresentationMode::DirectCompositionVisualTree:
				correctModeStyle = (style & WS_EX_NOREDIRECTIONBITMAP) != 0 &&
					(style & WS_EX_LAYERED) == 0;
				break;
			case HostPresentationMode::DwmBlurBehind:
			case HostPresentationMode::DwmBlurBehind2:
				correctModeStyle = (style & (WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED)) == 0;
				break;
			case HostPresentationMode::UlwDirtyRect:
				correctModeStyle = (style & WS_EX_LAYERED) != 0 &&
					(style & WS_EX_NOREDIRECTIONBITMAP) == 0;
				break;
			default:
				break;
			}
			Check(correctModeStyle, "presenter mode style contract", failures);
			return Check((style & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT)) ==
				(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT) &&
				(style & WS_EX_TOPMOST) == 0,
				"no-activate tool-window click-through contract", failures) && correctModeStyle;
		}

		bool RunMode(Inkeys::Window::Service& service, StyleContext& styleContext,
			HWND magnifierHost, HWND freeze, HWND drawpad, HostPresentationMode requiredMode,
			bool allowDirectComposition, bool exerciseCommands,
			bool exerciseUlwDirtyRect, int& failures)
		{
			const std::uint64_t styleCallsBefore =
				styleContext.callCount.load(std::memory_order_acquire);
			const HostStyleCallbacks callbacks{ &styleContext, &ApplyDrawpadStyle };
			HostStartOptions options{ requiredMode };
			options.enableHiddenTestContactInjection = exerciseCommands;
			options.allowDirectComposition = allowDirectComposition;
			if (!Check(StartProduct(drawpad, callbacks, options),
				"start real Draw3 host", failures))
				return false;

			bool modeSucceeded = true;
			auto snapshot = ProductHost().RuntimeSnapshot();
			modeSucceeded &= Check(snapshot.running && snapshot.firstFrameReady &&
				snapshot.lastPresentSucceeded && snapshot.successfulPresentCount >= 1,
				"first transparent frame", failures);
			if (requiredMode != HostPresentationMode::Automatic)
				modeSucceeded &= Check(snapshot.presentationMode == requiredMode,
					"forced presenter used the requested real backend", failures);
			else
				modeSucceeded &= Check(snapshot.presentationMode != HostPresentationMode::Automatic,
					"automatic fallback selected a real backend", failures);
			modeSucceeded &= CheckPresentationStyle(drawpad, snapshot.presentationMode, failures);
			modeSucceeded &= Check(styleContext.callCount.load(std::memory_order_acquire) >
				styleCallsBefore, "presenter used Window Service style callback", failures);
			modeSucceeded &= Check(!IsWindowVisible(magnifierHost) && !IsWindowVisible(freeze) &&
				!IsWindowVisible(drawpad), "all integration HWNDs remain invisible", failures);
			modeSucceeded &= Check(GetWindow(freeze, GW_OWNER) == magnifierHost &&
				GetWindow(drawpad, GW_OWNER) == freeze,
				"owner chain remains magnifier-freeze-drawpad", failures);

			if (exerciseCommands)
			{
				// 通过隐藏 Drawpad 的 WndProc mailbox 注入完整 Down/Move/Up，
				// 不调用 SendInput，也不绕过真实绘制线程直接访问 Renderer。
				const auto beforeContact = ProductHost().RuntimeSnapshot();
				const auto postContact = [&](HiddenTestContactPhase phase, int x, int y)
				{
					return PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(phase), MAKELPARAM(x, y)) != FALSE;
				};
				modeSucceeded &= Check(postContact(HiddenTestContactPhase::Down, 48, 64) &&
					postContact(HiddenTestContactPhase::Move, 96, 80) &&
					postContact(HiddenTestContactPhase::Move, 128, 112) &&
					postContact(HiddenTestContactPhase::Up, 160, 128),
					"post hidden contact sequence through Drawpad mailbox", failures);
				modeSucceeded &= Check(WaitUntil([beforeContact]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.inputDownPublished > beforeContact.inputDownPublished &&
							state.inputMovePublished >= beforeContact.inputMovePublished + 2 &&
							state.inputTerminalPublished > beforeContact.inputTerminalPublished &&
							state.inputRecycled > beforeContact.inputRecycled &&
							state.successfulPresentCount > beforeContact.successfulPresentCount;
					}), "hidden contact reached Draw3 consumer and presented", failures);

				modeSucceeded &= Check(WaitUntil([]
					{
						return ProductHost().RuntimeSnapshot().pageCount >= 1;
					}), "document initialized on drawing thread", failures);
				constexpr Bridge::CommandType commands[] = {
					Bridge::CommandType::Clear,
					Bridge::CommandType::Undo,
					Bridge::CommandType::Redo,
					Bridge::CommandType::NextPage,
					Bridge::CommandType::PreviousPage,
				};
				for (const auto command : commands)
					modeSucceeded &= Check(PublishProductCommand(command) ==
						Bridge::CommandResult::Accepted, "bridge command accepted", failures);
				modeSucceeded &= Check(WaitUntil([]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.clearCommandCount >= 1 && state.undoCommandCount >= 1 &&
							state.redoCommandCount >= 1 && state.nextPageCommandCount >= 1 &&
							state.previousPageCommandCount >= 1 && state.pageCount >= 2 &&
							state.currentPageIndex == 0;
					}), "clear undo redo and page commands executed", failures);

				const auto beforeResize = ProductHost().RuntimeSnapshot();
				const RECT resizedBounds{ 44, 56, 428, 312 };
				modeSucceeded &= Check(service.SetBounds(Inkeys::Window::WindowRole::Drawpad,
					resizedBounds), "Window Service resize request", failures);
				modeSucceeded &= Check(WaitUntil([beforeResize]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.resizeCount > beforeResize.resizeCount &&
							state.committedWidth == 384 && state.committedHeight == 256 &&
							state.successfulPresentCount > beforeResize.successfulPresentCount;
					}), "resize rebuilt and presented real Draw3 resources", failures);
			}

			if (exerciseUlwDirtyRect)
			{
				snapshot = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(snapshot.presentationMode ==
					HostPresentationMode::UlwDirtyRect &&
					snapshot.ulwTransparentFullFrameVerified &&
					snapshot.ulwPremultipliedAlphaFailureCount == 0,
					"ULW full frame is transparent premultiplied BGRA", failures);
				Bridge::ProductState eraserState{};
				eraserState.tool = Bridge::Tool::FixedEraser;
				eraserState.clickThrough = true;
				PublishProductState(eraserState);
				const auto beforeDirtyPresent = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PostMessageW(drawpad, WM_MOUSEMOVE, 0,
					MAKELPARAM(96, 80)) != FALSE,
					"post hidden cursor message without SendInput", failures);
				modeSucceeded &= Check(
					PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(HiddenTestContactPhase::Down), MAKELPARAM(96, 80)) != FALSE &&
					PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(HiddenTestContactPhase::Move), MAKELPARAM(120, 96)) != FALSE &&
					PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(HiddenTestContactPhase::Up), MAKELPARAM(144, 112)) != FALSE,
					"post ULW dirty contact through Drawpad mailbox", failures);
				modeSucceeded &= Check(WaitUntil([beforeDirtyPresent]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.partialPresentCount > beforeDirtyPresent.partialPresentCount &&
							state.ulwDirtyRectPresentCount > beforeDirtyPresent.ulwDirtyRectPresentCount;
					}), "ULW used a real dirty-rect present", failures);
				snapshot = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(snapshot.ulwPremultipliedAlphaFailureCount == 0 &&
					snapshot.lastDirtyRect.right > snapshot.lastDirtyRect.left &&
					snapshot.lastDirtyRect.bottom > snapshot.lastDirtyRect.top,
					"ULW dirty pixels remain premultiplied", failures);
			}

			const auto stopStarted = std::chrono::steady_clock::now();
			StopProduct();
			const auto stopElapsed = std::chrono::steady_clock::now() - stopStarted;
			modeSucceeded &= Check(stopElapsed < 10s && !ProductRunning() &&
				!ProductFirstFrameReady(), "bounded complete Draw3 stop", failures);
			modeSucceeded &= Check(IsWindow(drawpad) && !IsWindowVisible(drawpad) &&
				GetWindow(drawpad, GW_OWNER) == freeze,
				"Host stop leaves hidden Window Service HWND intact", failures);
			return modeSucceeded;
		}
	}

	int RunHiddenWindowIntegrationTest() noexcept
	{
		// 隐藏验收不能弹出 CRT 调试对话框，所有断言改写入测试 stderr。
		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		int failures = 0;
		try
		{
			Inkeys::Window::Service service(64);
			const std::wstring processSuffix = std::to_wstring(GetCurrentProcessId());
			const auto makeSpecs = [&](bool dcompCompatible)
			{
				const std::wstring suffix = processSuffix + (dcompCompatible ? L".Dcomp" : L".Legacy");
				std::vector<Inkeys::Window::WindowSpec> specs;
				specs.push_back(MakeHiddenSpec(Inkeys::Window::WindowRole::MagnifierHost,
					L"Inkeys.Draw3.Hidden.Magnifier." + suffix, DefWindowProcW, 320, 240));
				specs.push_back(MakeHiddenSpec(Inkeys::Window::WindowRole::Freeze,
					L"Inkeys.Draw3.Hidden.Freeze." + suffix, DefWindowProcW, 320, 240));
				auto drawpadSpec = MakeHiddenSpec(Inkeys::Window::WindowRole::Drawpad,
					L"Inkeys.Draw3.Hidden.Drawpad." + suffix, DrawpadMsgCallback, 320, 240);
				if (dcompCompatible) drawpadSpec.exStyle |= WS_EX_NOREDIRECTIONBITMAP;
				specs.push_back(std::move(drawpadSpec));
				return specs;
			};
			StyleContext styleContext{ &service };

			// DComp 的 NOREDIRECTIONBITMAP 必须在 HWND 创建时存在；覆盖产品默认路径和完整桥接命令。
			if (!Check(service.Start(makeSpecs(true)), "start DComp-compatible hidden Window Service", failures))
				return 1;
			const HWND dcompMagnifierHost = service.Handle(Inkeys::Window::WindowRole::MagnifierHost);
			const HWND dcompFreeze = service.Handle(Inkeys::Window::WindowRole::Freeze);
			const HWND dcompDrawpad = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			Check(dcompMagnifierHost && dcompFreeze && dcompDrawpad, "hidden DComp HWND creation", failures);
			Check(!IsWindowVisible(dcompMagnifierHost) && !IsWindowVisible(dcompFreeze) &&
				!IsWindowVisible(dcompDrawpad), "DComp HWND creation never shows UI", failures);
			RunMode(service, styleContext, dcompMagnifierHost, dcompFreeze, dcompDrawpad,
				HostPresentationMode::Automatic, true, true, false, failures);
			RunMode(service, styleContext, dcompMagnifierHost, dcompFreeze, dcompDrawpad,
				HostPresentationMode::DirectCompositionVisualTree, true, false, false, failures);
			// Windows 可能把创建期 NOREDIRECTIONBITMAP 固化；回调结果必须与真实样式一致，不能伪造 legacy fallback。
			const bool clearReported = service.SetExtendedStyleFlags(
				Inkeys::Window::WindowRole::Drawpad, 0, WS_EX_NOREDIRECTIONBITMAP);
			const bool clearApplied = (static_cast<DWORD>(GetWindowLongPtrW(
				dcompDrawpad, GWL_EXSTYLE)) & WS_EX_NOREDIRECTIONBITMAP) == 0;
			Check(clearReported == clearApplied,
				"Window Service reports immutable DComp style truthfully", failures);
			if (!clearApplied)
			{
				// 已绑定 DComp 的 HWND 不能直接切换 ULW；先验证失败清理，再重建唯一 legacy HWND。
				const HostStyleCallbacks callbacks{ &styleContext, &ApplyDrawpadStyle };
				HostStartOptions legacyOnDcompOptions{ HostPresentationMode::UlwDirtyRect };
				legacyOnDcompOptions.allowDirectComposition = false;
				Check(!StartProduct(dcompDrawpad, callbacks, legacyOnDcompOptions),
					"legacy presenter rejects immutable DComp HWND", failures);
				Check(!ProductRunning() && !ProductFirstFrameReady(),
					"failed legacy startup fully stops Draw3 host", failures);
			}
			StopProduct();
			service.StopAndJoin();
			Check(!IsWindow(dcompMagnifierHost) && !IsWindow(dcompFreeze) && !IsWindow(dcompDrawpad),
				"Window Service destroys DComp-compatible hidden HWNDs", failures);

			// DWM/ULW 需要可切换的初始重定向表面；停止上一宿主后重建唯一的测试 Drawpad HWND。
			if (!Check(service.Start(makeSpecs(false)), "start DWM-compatible hidden Window Service", failures))
				return 1;
			const HWND legacyMagnifierHost = service.Handle(Inkeys::Window::WindowRole::MagnifierHost);
			const HWND legacyFreeze = service.Handle(Inkeys::Window::WindowRole::Freeze);
			const HWND legacyDrawpad = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			Check(legacyMagnifierHost && legacyFreeze && legacyDrawpad, "hidden DWM HWND creation", failures);
			Check(!IsWindowVisible(legacyMagnifierHost) && !IsWindowVisible(legacyFreeze) &&
				!IsWindowVisible(legacyDrawpad), "DWM HWND creation never shows UI", failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				HostPresentationMode::Automatic, false, false, false, failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				HostPresentationMode::DwmBlurBehind2, false, false, false, failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				HostPresentationMode::DwmBlurBehind, false, false, false, failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				HostPresentationMode::UlwDirtyRect, false, true, true, failures);
			StopProduct();
			service.StopAndJoin();
			Check(!IsWindow(legacyMagnifierHost) && !IsWindow(legacyFreeze) && !IsWindow(legacyDrawpad),
				"Window Service destroys DWM-compatible hidden HWNDs", failures);
		}
		catch (...)
		{
			StopProduct();
			++failures;
			Report("FAIL", "unexpected hidden integration exception");
		}
		if (failures == 0) Report("PASS", "all hidden integration checks");
		return failures == 0 ? 0 : 1;
	}
}
