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
			spec.x = -32000;
			spec.y = -32000;
			spec.width = width;
			spec.height = height;
			spec.style = WS_POPUP | WS_CLIPCHILDREN;
			spec.exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
			if (role == Inkeys::Window::WindowRole::DrawpadPresentation)
				spec.exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
			else if (role != Inkeys::Window::WindowRole::Drawpad)
				spec.exStyle |= WS_EX_TRANSPARENT;
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
			return Check((style & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) ==
				(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) &&
				(style & WS_EX_TRANSPARENT) == 0 &&
				(style & WS_EX_TOPMOST) == 0,
				"primary Drawpad has no click-through style", failures) && correctModeStyle;
		}

		bool CheckPresentationWindowStyle(HWND presentation, int& failures)
		{
			const auto style = static_cast<DWORD>(GetWindowLongPtrW(
				presentation, GWL_EXSTYLE));
			return Check((style & (WS_EX_LAYERED | WS_EX_TRANSPARENT |
				WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) ==
				(WS_EX_LAYERED | WS_EX_TRANSPARENT |
					WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW),
				"selection ULW window has fixed click-through style", failures);
		}

		bool RunMode(Inkeys::Window::Service& service, StyleContext& styleContext,
			HWND magnifierHost, HWND freeze, HWND drawpad, HWND presentation,
			HostPresentationMode requiredMode,
			bool allowDirectComposition, bool exerciseCommands,
			bool exerciseUlwDirtyRect, int& failures)
		{
			const std::uint64_t styleCallsBefore =
				styleContext.callCount.load(std::memory_order_acquire);
			const HostStyleCallbacks callbacks{ &styleContext, &ApplyDrawpadStyle };
			HostStartOptions options{ requiredMode };
			options.enableHiddenTestContactInjection = exerciseCommands;
			options.allowDirectComposition = allowDirectComposition;
			if (!Check(StartProduct(drawpad, presentation, callbacks, options),
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
			modeSucceeded &= CheckPresentationWindowStyle(presentation, failures);
			modeSucceeded &= Check(SendMessageW(drawpad, WM_MOUSEACTIVATE,
				reinterpret_cast<WPARAM>(drawpad),
				MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE,
				"primary Drawpad mouse input never activates the overlay", failures);
			modeSucceeded &= Check(styleContext.callCount.load(std::memory_order_acquire) >
				styleCallsBefore, "presenter used Window Service style callback", failures);
			modeSucceeded &= Check(!IsWindowVisible(magnifierHost) && !IsWindowVisible(freeze) &&
				!IsWindowVisible(drawpad) && !IsWindowVisible(presentation),
				"all integration HWNDs remain invisible", failures);
			modeSucceeded &= Check(GetWindow(freeze, GW_OWNER) == magnifierHost &&
				GetWindow(drawpad, GW_OWNER) == freeze &&
				GetWindow(presentation, GW_OWNER) == freeze,
				"drawpad and presentation remain Freeze siblings", failures);
			modeSucceeded &= Check(WaitUntil([]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.selectionMode &&
					state.requestedOutputTarget == HostOutputTarget::SelectionUlw &&
					state.readyOutputTarget == HostOutputTarget::SelectionUlw &&
					state.readyOutputRevision == state.requestedOutputRevision &&
					state.presentedContentRevision == state.contentRevision &&
					state.auxiliaryFullFrameClean;
			}), "initial selection ULW target completes a clean frame", failures);
			const auto initialSelection = ProductHost().RuntimeSnapshot();
			Bridge::ProductState drawingState{};
			drawingState.selectionMode = false;
			PublishProductState(drawingState);
			modeSucceeded &= Check(WaitUntil([initialSelection]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return !state.selectionMode &&
					state.requestedOutputTarget == HostOutputTarget::PrimaryDrawpad &&
					state.readyOutputTarget == HostOutputTarget::PrimaryDrawpad &&
					state.readyOutputRevision == state.requestedOutputRevision &&
					state.requestedOutputRevision > initialSelection.requestedOutputRevision &&
					state.presentedContentRevision == state.contentRevision;
			}), "drawing mode preheats the primary target before readiness", failures);
			const auto primaryReady = ProductHost().RuntimeSnapshot();
			Bridge::ProductState selectionState{};
			selectionState.selectionMode = true;
			PublishProductState(selectionState);
			modeSucceeded &= Check(WaitUntil([primaryReady]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.selectionMode &&
					state.requestedOutputTarget == HostOutputTarget::SelectionUlw &&
					state.readyOutputTarget == HostOutputTarget::SelectionUlw &&
					state.readyOutputRevision == state.requestedOutputRevision &&
					state.requestedOutputRevision > primaryReady.requestedOutputRevision &&
					state.presentedContentRevision == state.contentRevision &&
					state.auxiliaryFullFrameClean;
			}), "selection mode advances generation and restores clean ULW", failures);

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
							state.successfulPresentCount > beforeContact.successfulPresentCount &&
							state.currentPageHasContent;
					}), "hidden contact reached Draw3 consumer and presented", failures);

				modeSucceeded &= Check(WaitUntil([]
					{
						return ProductHost().RuntimeSnapshot().pageCount >= 1;
					}), "document initialized on drawing thread", failures);
				auto pageZeroContent = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(pageZeroContent.currentPageHasContent &&
					pageZeroContent.contentRevision > beforeContact.contentRevision,
					"stored stroke publishes current page content", failures);

				const auto beforeNextPage = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::NextPage) ==
					Bridge::CommandResult::Accepted, "next page command accepted", failures);
				modeSucceeded &= Check(WaitUntil([beforeNextPage]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.nextPageCommandCount > beforeNextPage.nextPageCommandCount &&
							state.pageCount >= 2 && state.currentPageIndex == 1 &&
							!state.currentPageHasContent;
					}), "switching to a blank page publishes no content", failures);
				const auto pageOneEmpty = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(pageOneEmpty.contentRevision >
					pageZeroContent.contentRevision,
					"content revision advances on a boolean state change", failures);

				// 空页上的橡皮虽然视觉仍为空，也必须作为历史内容保留。
				Bridge::ProductState eraserState{};
				eraserState.tool = Bridge::Tool::FixedEraser;
				eraserState.selectionMode = false;
				PublishProductState(eraserState);
				const auto beforeEraser = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postContact(HiddenTestContactPhase::Down, 72, 88) &&
					postContact(HiddenTestContactPhase::Move, 112, 104) &&
					postContact(HiddenTestContactPhase::Up, 144, 120),
					"post eraser history sequence on blank page", failures);
				modeSucceeded &= Check(WaitUntil([beforeEraser, pageOneEmpty]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.inputDownPublished > beforeEraser.inputDownPublished &&
							state.inputTerminalPublished > beforeEraser.inputTerminalPublished &&
							state.inputRecycled > beforeEraser.inputRecycled &&
							state.currentPageHasContent &&
							state.contentRevision > pageOneEmpty.contentRevision;
					}), "eraser history counts as content on a visually blank page", failures);

				const auto beforePreviousPage = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::PreviousPage) ==
					Bridge::CommandResult::Accepted, "previous page command accepted", failures);
				modeSucceeded &= Check(WaitUntil([beforePreviousPage]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.previousPageCommandCount >
							beforePreviousPage.previousPageCommandCount &&
							state.currentPageIndex == 0 && state.currentPageHasContent;
					}), "returning to the first page restores its content state", failures);

				const auto beforeClear = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Clear) ==
					Bridge::CommandResult::Accepted, "clear command accepted", failures);
				modeSucceeded &= Check(WaitUntil([beforeClear]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.clearCommandCount > beforeClear.clearCommandCount &&
							!state.currentPageHasContent &&
							state.contentRevision > beforeClear.contentRevision;
					}), "clear permanently publishes an empty current page", failures);
				const auto afterClear = ProductHost().RuntimeSnapshot();

				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Undo) ==
					Bridge::CommandResult::Accepted, "undo after clear accepted", failures);
				modeSucceeded &= Check(WaitUntil([afterClear]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.undoCommandCount > afterClear.undoCommandCount;
					}), "undo after clear was consumed", failures);
				const auto afterUndo = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(!afterUndo.currentPageHasContent &&
					afterUndo.contentRevision == afterClear.contentRevision,
					"undo cannot recover content removed by clear", failures);

				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Redo) ==
					Bridge::CommandResult::Accepted, "redo after clear accepted", failures);
				modeSucceeded &= Check(WaitUntil([afterUndo]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.redoCommandCount > afterUndo.redoCommandCount;
					}), "redo after clear was consumed", failures);
				const auto afterRedo = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(!afterRedo.currentPageHasContent &&
					afterRedo.contentRevision == afterClear.contentRevision,
					"redo cannot recover content removed by clear", failures);

				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::NextPage) ==
					Bridge::CommandResult::Accepted, "next page after clear accepted", failures);
				modeSucceeded &= Check(WaitUntil([afterRedo]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.nextPageCommandCount > afterRedo.nextPageCommandCount &&
							state.currentPageIndex == 1 && state.currentPageHasContent &&
							state.contentRevision > afterRedo.contentRevision;
					}), "clear preserves content on other pages", failures);
				const auto restoredOtherPage = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::PreviousPage) ==
					Bridge::CommandResult::Accepted, "return to cleared page accepted", failures);
				modeSucceeded &= Check(WaitUntil([restoredOtherPage]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.previousPageCommandCount >
							 restoredOtherPage.previousPageCommandCount &&
							state.currentPageIndex == 0 && !state.currentPageHasContent &&
							state.contentRevision > restoredOtherPage.contentRevision;
					}), "cleared page remains empty after page round-trip", failures);

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
				RECT presentationBounds = {};
				GetWindowRect(presentation, &presentationBounds);
				modeSucceeded &= Check(presentationBounds.left == resizedBounds.left &&
					presentationBounds.top == resizedBounds.top &&
					presentationBounds.right - presentationBounds.left == 384 &&
					presentationBounds.bottom - presentationBounds.top == 256,
					"resize keeps selection ULW bounds synchronized", failures);
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
				eraserState.selectionMode = false;
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
			modeSucceeded &= Check(IsWindow(drawpad) && IsWindow(presentation) &&
				!IsWindowVisible(drawpad) && !IsWindowVisible(presentation) &&
				GetWindow(drawpad, GW_OWNER) == freeze &&
				GetWindow(presentation, GW_OWNER) == freeze,
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
				specs.push_back(MakeHiddenSpec(
					Inkeys::Window::WindowRole::DrawpadPresentation,
					L"Inkeys.Draw3.Hidden.Presentation." + suffix,
					DefWindowProcW, 320, 240));
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
			const HWND dcompPresentation = service.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation);
			const HWND dcompDrawpad = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			Check(dcompMagnifierHost && dcompFreeze && dcompPresentation && dcompDrawpad,
				"hidden DComp HWND creation", failures);
			Check(!IsWindowVisible(dcompMagnifierHost) && !IsWindowVisible(dcompFreeze) &&
				!IsWindowVisible(dcompPresentation) && !IsWindowVisible(dcompDrawpad),
				"DComp HWND creation never shows UI", failures);
			Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Presentation) &&
				!IsWindowVisible(dcompDrawpad) && IsWindowVisible(dcompPresentation),
				"presentation visibility selects only the auxiliary window", failures);
			Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Primary) &&
				IsWindowVisible(dcompDrawpad) && !IsWindowVisible(dcompPresentation),
				"primary visibility selects only the Drawpad window", failures);
			Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden) &&
				!IsWindowVisible(dcompDrawpad) && !IsWindowVisible(dcompPresentation),
				"hidden visibility leaves both windows hidden", failures);
			RunMode(service, styleContext, dcompMagnifierHost, dcompFreeze, dcompDrawpad,
				dcompPresentation,
				HostPresentationMode::Automatic, true, true, false, failures);
			RunMode(service, styleContext, dcompMagnifierHost, dcompFreeze, dcompDrawpad,
				dcompPresentation,
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
				Check(!StartProduct(dcompDrawpad, dcompPresentation, callbacks,
					legacyOnDcompOptions),
					"legacy presenter rejects immutable DComp HWND", failures);
				Check(!ProductRunning() && !ProductFirstFrameReady(),
					"failed legacy startup fully stops Draw3 host", failures);
			}
			StopProduct();
			service.StopAndJoin();
			Check(!IsWindow(dcompMagnifierHost) && !IsWindow(dcompFreeze) &&
				!IsWindow(dcompPresentation) && !IsWindow(dcompDrawpad),
				"Window Service destroys DComp-compatible hidden HWNDs", failures);

			// DWM/ULW 需要可切换的初始重定向表面；停止上一宿主后重建唯一的测试 Drawpad HWND。
			if (!Check(service.Start(makeSpecs(false)), "start DWM-compatible hidden Window Service", failures))
				return 1;
			const HWND legacyMagnifierHost = service.Handle(Inkeys::Window::WindowRole::MagnifierHost);
			const HWND legacyFreeze = service.Handle(Inkeys::Window::WindowRole::Freeze);
			const HWND legacyPresentation = service.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation);
			const HWND legacyDrawpad = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			Check(legacyMagnifierHost && legacyFreeze && legacyPresentation && legacyDrawpad,
				"hidden DWM HWND creation", failures);
			Check(!IsWindowVisible(legacyMagnifierHost) && !IsWindowVisible(legacyFreeze) &&
				!IsWindowVisible(legacyPresentation) && !IsWindowVisible(legacyDrawpad),
				"DWM HWND creation never shows UI", failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				legacyPresentation,
				HostPresentationMode::Automatic, false, false, false, failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				legacyPresentation,
				HostPresentationMode::DwmBlurBehind2, false, false, false, failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				legacyPresentation,
				HostPresentationMode::DwmBlurBehind, false, false, false, failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				legacyPresentation,
				HostPresentationMode::UlwDirtyRect, false, true, true, failures);
			StopProduct();
			service.StopAndJoin();
			Check(!IsWindow(legacyMagnifierHost) && !IsWindow(legacyFreeze) &&
				!IsWindow(legacyPresentation) && !IsWindow(legacyDrawpad),
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
