#include "Draw3.HiddenWindowTest.h"
#include "Draw3.Product.h"

import Inkeys.Window;
import draw3.uink_file;
import draw3.uink_draw3_import;

#include <atomic>
#include <chrono>
#include <cmath>
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


		bool CheckSizeBoundaryFile(const SpeedEraser::Diagnostics& diagnostic)
		{
			using namespace draw3::uink;
			const auto file=CreateUInkGuid(), workspace=CreateUInkGuid(), page=CreateUInkGuid();
			if(!file || !workspace || !page || !diagnostic.resumedWithAnchor)return false;
			Draw3UInkExportSnapshot snapshot;
			snapshot.fileGuid=*file;snapshot.workspaceGuid=*workspace;
			Draw3UInkCanvasSnapshot canvas;canvas.pageGuid=*page;canvas.pageNumber=1;
			Draw3UInkStrokeSnapshot stroke;stroke.style.kind=Draw3UInkStrokeKind::Eraser;
			for(size_t i=0;i<3;++i)stroke.points.push_back({diagnostic.boundaryPoints[i*3],
				diagnostic.boundaryPoints[i*3+1],diagnostic.boundaryPoints[i*3+2]});
			canvas.strokes.push_back(stroke);snapshot.canvases.push_back(canvas);
			const auto exported=ExportDraw3SnapshotToUInk(snapshot);
			if(!exported.document)return false;
			UInkEditingSession session;session.document=*exported.document;session.provenance.sourceWasExternal=false;
			const std::string id=FormatUInkGuid(*file);
			const std::wstring path=L"Build\\eraser-size-boundary-"+std::wstring(id.begin(),id.end())+L".uink";
			UInkSaveOptions options;options.mode=UInkSaveMode::SaveAsNewLogicalFile;
			if(SaveUInkFile(path,session,options).status!=UInkSaveStatus::Committed)return false;
			const auto read=ReadUInkFile(path);
			if(!read.document)return false;
			const auto imported=ImportDraw3UInkDocument(*read.document);
			if(!imported.snapshot || imported.snapshot->canvases.size()!=1 ||
				imported.snapshot->canvases[0].strokes.size()!=1)return false;
			const auto& restored=imported.snapshot->canvases[0].strokes[0].points;
			if(restored.size()!=3)return false;
			for(size_t i=0;i<3;++i)
				if(std::abs(restored[i].x-stroke.points[i].x)>0.001f ||
					std::abs(restored[i].y-stroke.points[i].y)>0.001f ||
					std::abs(restored[i].width-stroke.points[i].width)>0.001f)return false;
			return restored[0].x==restored[1].x && restored[0].y==restored[1].y &&
				restored[0].width>restored[1].width && restored[2].width<=diagnostic.dpiX/96*36;
		}

		bool CheckPenDwellAndResume(HWND drawpad, int& failures)
		{
			Bridge::ProductState penState;
			penState.workspace = Bridge::Workspace::Whiteboard;
			penState.tool = Bridge::Tool::Pen;
			penState.selectionMode = false;
			penState.widthDip = 8.0f;
			PublishProductState(penState);
			bool succeeded = Check(WaitUntil([]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return !state.selectionMode && state.workspace == Bridge::Workspace::Whiteboard;
			}), "pen dwell test uses the drawing workspace", failures);
			const auto post = [&](HiddenTestContactPhase phase, int x, int y)
			{
				return PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
					static_cast<WPARAM>(phase) | kHiddenTestMouseFlag, MAKELPARAM(x, y)) != FALSE;
			};
			const auto beforeDown = ProductHost().RuntimeSnapshot().pen;
			succeeded &= Check(post(HiddenTestContactPhase::Down, 60, 150),
				"post pen dwell Down", failures);
			if (!Check(WaitUntil([beforeDown]
			{
				const auto pen = ProductHost().RuntimeSnapshot().pen;
				return pen.active && pen.strokeId != beforeDown.strokeId && pen.realPointCount > 0;
			}), "drawing thread consumes pen dwell Down", failures)) return false;
			const auto strokeId = ProductHost().RuntimeSnapshot().pen.strokeId;
			bool sawTaper = false;
			int x = 60;
			int y = 150;
			const auto moveAndWait = [&](int nextX, int nextY)
			{
				const auto before = ProductHost().RuntimeSnapshot().pen.inputSequence;
				if (!post(HiddenTestContactPhase::Move, nextX, nextY)) return false;
				// 等待消费序号，而非生产计数，确保每个稀疏点经过真实帧循环。
				return WaitUntil([before, strokeId]
				{
					const auto pen = ProductHost().RuntimeSnapshot().pen;
					return pen.active && pen.strokeId == strokeId && pen.inputSequence > before;
				}, 2s);
			};
			for (int n = 0; n < 20; ++n)
			{
				std::this_thread::sleep_for(16ms);
				++x;
				if (n % 4 == 0) ++y;
				succeeded &= Check(moveAndWait(x, y), "consume each 1px pen Move", failures);
				const auto pen = ProductHost().RuntimeSnapshot().pen;
				sawTaper |= pen.baseRadius > 0.0f && pen.tipRadius < pen.baseRadius - 0.02f;
			}
			succeeded &= Check(sawTaper, "moving pen has a visible simulated tip", failures);
			const auto waitForDwell = [&]
			{
				return WaitUntil([strokeId]
				{
					const auto pen = ProductHost().RuntimeSnapshot().pen;
					return pen.active && pen.strokeId == strokeId && pen.frozen &&
						pen.endpointError <= 0.05f && pen.baseRadius > 0.0f &&
						std::abs(pen.tipRadius - pen.baseRadius) <= 0.02f;
				}, 2s);
			};
			succeeded &= Check(waitForDwell(),
				"stopped pen reaches endpoint and loses its tip before freezing", failures);
			const auto frozen = ProductHost().RuntimeSnapshot().pen;
			bool bounded = true;
			for (int n = 0; n < 30; ++n)
			{
				std::this_thread::sleep_for(10ms);
				const auto held = ProductHost().RuntimeSnapshot().pen;
				bounded &= held.active && held.frozen && held.modelUpdateCount == frozen.modelUpdateCount &&
					held.realPointCount == frozen.realPointCount && held.l0PointCount == frozen.l0PointCount &&
					held.committedRealIndex == frozen.committedRealIndex &&
					std::abs(held.tipRadius - frozen.tipRadius) <= 0.02f;
			}
			succeeded &= Check(bounded, "frozen pen model and real/L0/L1 counts remain fixed", failures);
			// 依次向右、向左、向下：明确覆盖同向、180 度反向和 90 度转向。
			for (int direction = 0; direction < 3; ++direction)
			{
				bool unlocked = false;
				bool sawModeledLag = false;
				for (int n = 0; n < 12; ++n)
				{
					std::this_thread::sleep_for(16ms);
					if (direction == 2) ++y;
					else x += direction == 1 ? -1 : 1;
					succeeded &= Check(moveAndWait(x, y), "consume resumed pen Move", failures);
					const auto pen = ProductHost().RuntimeSnapshot().pen;
					unlocked |= !pen.recovering && !pen.frozen;
					sawModeledLag |= pen.endpointError > 0.05f;
					succeeded &= Check(std::isfinite(pen.tipRadius) && pen.tipRadius > 0.0f &&
						pen.tipRadius <= pen.baseRadius + 0.02f,
						"resumed visible tip remains bounded by its base radius", failures);
				}
				succeeded &= Check(unlocked && sawModeledLag,
					"resumed pen unlocks and follows model output instead of pinning every raw point", failures);
				succeeded &= Check(waitForDwell(), "resumed pen settles and fades again", failures);
			}
			const auto beforeUp = ProductHost().RuntimeSnapshot().pen;
			succeeded &= Check(post(HiddenTestContactPhase::Up, x, y), "post same-position pen Up", failures);
			succeeded &= Check(WaitUntil([strokeId]
			{
				const auto pen = ProductHost().RuntimeSnapshot().pen;
				return pen.strokeId == strokeId && !pen.active;
			}, 2s), "drawing thread publishes completed pen geometry", failures);
			const auto completed = ProductHost().RuntimeSnapshot().pen;
			succeeded &= Check(completed.endpointError <= 0.05f &&
				std::abs(completed.tipRadius - beforeUp.tipRadius) <= 0.02f &&
				std::abs(completed.baseRadius - beforeUp.baseRadius) <= 0.02f &&
				completed.realPointCount == beforeUp.realPointCount,
				"same-position Up preserves settled endpoint, radius and point count", failures);
			std::fprintf(stderr, "[PenDwell] stroke=%llu updates=%llu real=%zu L0=%zu L1=%zu endpoint=%g tip=%g base=%g\n",
				static_cast<unsigned long long>(completed.strokeId),
				static_cast<unsigned long long>(completed.modelUpdateCount), completed.realPointCount,
				completed.l0PointCount, completed.committedRealIndex, completed.endpointError,
				completed.tipRadius, completed.baseRadius);
			return succeeded;
		}

		bool CheckPenPhysicalRelease(HWND drawpad, int& failures)
		{
			bool succeeded = true;
			for (const WPARAM device : { kHiddenTestMouseFlag, kHiddenTestTouchFlag,
				kHiddenTestIntegratedPenFlag, kHiddenTestIntegratedPenFlag | kHiddenTestNoPressureFlag })
			for (int scenario = 0; scenario < 3; ++scenario)
			{
				const auto post = [&](HiddenTestContactPhase phase, int x, WPARAM extra = 0)
				{
					return PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(phase) | device | extra, MAKELPARAM(x, 180)) != FALSE;
				};
				const auto oldId = ProductHost().RuntimeSnapshot().pen.strokeId;
				succeeded &= Check(post(HiddenTestContactPhase::Down, 40), "release test Down", failures);
				if (!Check(WaitUntil([oldId]
				{
					const auto p = ProductHost().RuntimeSnapshot().pen;
					return p.active && p.strokeId != oldId;
				}, 2s), "release Down consumed", failures)) return false;
				const auto id = ProductHost().RuntimeSnapshot().pen.strokeId;
				int x = 40;
				for (int n = 0; n < 10; ++n)
				{
					const auto sequence = ProductHost().RuntimeSnapshot().pen.inputSequence;
					x += 12;
					succeeded &= Check(post(HiddenTestContactPhase::Move, x), "release Move", failures);
					succeeded &= Check(WaitUntil([id, sequence]
					{
						const auto p = ProductHost().RuntimeSnapshot().pen;
						return p.strokeId == id && p.inputSequence > sequence;
					}, 2s), "release Move consumed", failures);
				}
				if (scenario == 1)
					succeeded &= Check(WaitUntil([id]
					{
						const auto p = ProductHost().RuntimeSnapshot().pen;
						return p.strokeId == id && p.frozen;
					}, 2s), "held release settles before Up", failures);
				if (scenario == 2) std::this_thread::sleep_for(40ms);
				if (scenario == 0) x += 12; // 明确仍在运动的 Up，不能把调度等待误当成快速抬笔。
				succeeded &= Check(post(HiddenTestContactPhase::Up, x,
					scenario == 2 ? kHiddenTestDelayedUpFlag : 0), "release Up", failures);
				succeeded &= Check(WaitUntil([id]
				{
					const auto p = ProductHost().RuntimeSnapshot().pen;
					return p.strokeId == id && p.terminalLocked;
				}, 2s), "physical Up locks display time", failures);
				const auto released = ProductHost().RuntimeSnapshot().pen;
				if (device == kHiddenTestTouchFlag && scenario == 0)
					succeeded &= Check(released.active && released.awaitingReconnect,
						"fast Touch release observes actual pending candidate before completion", failures);
				succeeded &= Check(released.displayTime == released.physicalUpTime &&
					released.endpointError <= 0.05f, "release time and raw endpoint are authoritative", failures);
				succeeded &= Check(WaitUntil([id]
				{
					const auto p = ProductHost().RuntimeSnapshot().pen;
					return p.strokeId == id && !p.active;
				}, 2s), "release candidate eventually completes", failures);
				const auto complete = ProductHost().RuntimeSnapshot().pen;
				succeeded &= Check(complete.displayTime == released.displayTime &&
					std::abs(complete.tipRadius - released.tipRadius) <= 0.02f,
					"reconnect timeout does not age released tip", failures);
				if (scenario == 1)
					succeeded &= Check(std::abs(complete.tipRadius - complete.baseRadius) <= 0.02f,
						"held release cannot create a new fine tip", failures);
				if (scenario == 0 && device != kHiddenTestIntegratedPenFlag)
					succeeded &= Check(complete.tipRadius < complete.baseRadius - 0.02f,
						"fast simulated release retains a fine tip", failures);
				succeeded &= Check(complete.acceptedTailCount > 0 &&
					complete.rawUp[0] == static_cast<float>(x),
					"release trace records accepted tail and physical endpoint", failures);
				std::fprintf(stderr, "[PenRelease] device=%u scenario=%d time=%g tip=%g base=%g model=%zu accepted=%zu\n",
					complete.deviceType, scenario, complete.displayTime, complete.tipRadius,
					complete.baseRadius, complete.modelTailCount, complete.acceptedTailCount);
			}

			// 同一真实 Touch mailbox 成功续接后必须解除上一物理 Up 的显示锁。
			const auto postTouch = [&](HiddenTestContactPhase phase, int x)
			{
				return SendMessageW(drawpad, kDraw3HiddenTestContactMessage,
					static_cast<WPARAM>(phase) | kHiddenTestTouchFlag, MAKELPARAM(x, 210)) == 0;
			};
			const auto oldId = ProductHost().RuntimeSnapshot().pen.strokeId;
			postTouch(HiddenTestContactPhase::Down, 40);
			if (!Check(WaitUntil([oldId]
			{
				const auto p = ProductHost().RuntimeSnapshot().pen;
				return p.active && p.strokeId != oldId;
			}, 2s), "reconnect test Down consumed", failures)) return false;
			const auto id = ProductHost().RuntimeSnapshot().pen.strokeId;
			int x = 40;
			for (int n = 0; n < 10; ++n)
			{
				const auto sequence = ProductHost().RuntimeSnapshot().pen.inputSequence;
				x += 12;
				postTouch(HiddenTestContactPhase::Move, x);
				succeeded &= Check(WaitUntil([sequence]
				{ return ProductHost().RuntimeSnapshot().pen.inputSequence > sequence; }, 2s),
					"reconnect test Move consumed", failures);
			}
			postTouch(HiddenTestContactPhase::Up, x);
			succeeded &= Check(WaitUntil([id]
			{
				const auto p = ProductHost().RuntimeSnapshot().pen;
				return p.strokeId == id && p.awaitingReconnect && p.terminalLocked;
			}, 50ms), "Touch Up enters locked reconnect candidate", failures);
			// 真实同位重触走原预测落点门禁，不把 raw 速度外推当成冻结预测轨迹。
			succeeded &= Check(postTouch(HiddenTestContactPhase::Down, x),
				"same-position recontact enters the real mailbox", failures);
			succeeded &= Check(WaitUntil([id]
			{
				const auto p = ProductHost().RuntimeSnapshot().pen;
				return p.strokeId == id && p.active && !p.awaitingReconnect && !p.terminalLocked;
			}, 1s), "successful Touch reconnect clears physical Up lock", failures);
			const auto resumedSequence = ProductHost().RuntimeSnapshot().pen.inputSequence;
			succeeded &= Check(postTouch(HiddenTestContactPhase::Move, x + 12),
				"reconnected Touch publishes real movement", failures);
			succeeded &= Check(WaitUntil([id, resumedSequence]
			{
				const auto p = ProductHost().RuntimeSnapshot().pen;
				return p.strokeId == id && p.active && !p.terminalLocked &&
					p.inputSequence > resumedSequence;
			}, 2s), "reconnected Touch continues on the same unlocked stroke", failures);
			postTouch(HiddenTestContactPhase::Up, x + 12);
			succeeded &= Check(WaitUntil([]
			{ return !ProductHost().RuntimeSnapshot().pen.active; }, 2s), "reconnected Touch completes", failures);
			return succeeded;
		}

		bool RunMode(Inkeys::Window::Service& service, StyleContext& styleContext,
			HWND magnifierHost, HWND freeze, HWND drawpad, HWND presentation,
			HostPresentationMode requiredMode,
			bool allowDirectComposition, bool exerciseCommands,
			bool exerciseUlwDirtyRect, int& failures, bool exerciseEraser = false)
		{
			const std::uint64_t styleCallsBefore =
				styleContext.callCount.load(std::memory_order_acquire);
			const HostStyleCallbacks callbacks{ &styleContext, &ApplyDrawpadStyle };
			HostStartOptions options{ requiredMode };
			options.enableHiddenTestContactInjection = exerciseCommands || exerciseEraser;
			options.allowDirectComposition = allowDirectComposition;
			if(exerciseEraser)
			{
				// 隐藏 HWND 位于屏幕外；注入明确的逻辑像素表面，不伪造 EDID 或实测物理尺寸。
				SpeedEraser::DisplayScale scale;scale.monitor=1;scale.generation=7;
				scale.pixelWidth=320;scale.pixelHeight=240;scale.logicalOutputKnown=true;
				options.hiddenTestDisplayScale=scale;
			}
			if (!Check(StartProduct(drawpad, presentation, callbacks, options),
				"start real Draw3 host", failures))
				return false;

			if(exerciseEraser)
			{
				// 在 RTS context 已建立后才启用，验证缓存补打；真实 packet 仍需设备手工采集。
				auto diagnostics=ProductHost().EraserDevelopmentOptions();
				diagnostics.touchAreaTrace=true;
				ProductHost().SetEraserDevelopmentOptions(diagnostics);
			}
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
					}), "clear publishes an empty current page", failures);
				const auto afterClear = ProductHost().RuntimeSnapshot();

				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Undo) ==
					Bridge::CommandResult::Accepted, "undo after clear accepted", failures);
				modeSucceeded &= Check(WaitUntil([afterClear]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.undoCommandCount > afterClear.undoCommandCount &&
							state.currentPageHasContent &&
							state.contentRevision > afterClear.contentRevision;
					}), "undo restores content removed by clear", failures);
				const auto afterUndo = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(afterUndo.currentPageHasContent,
					"undo clear republishes current page content", failures);

				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::NextPage) ==
					Bridge::CommandResult::Accepted, "next page after clear accepted", failures);
				modeSucceeded &= Check(WaitUntil([afterUndo]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.nextPageCommandCount > afterUndo.nextPageCommandCount &&
							state.currentPageIndex == 1 && state.currentPageHasContent &&
							state.successfulPresentCount > afterUndo.successfulPresentCount;
					}), "clear preserves content on other pages", failures);
				const auto restoredOtherPage = ProductHost().RuntimeSnapshot();
				// Host内容通知只在空/非空变化时发布；同为非空的翻页用页号、命令和呈现验证。
				std::fprintf(stderr,"[ClearRoundtrip] page=%zu content=%d revision=%llu previous=%llu presents=%llu previousPresents=%llu\n",
					restoredOtherPage.currentPageIndex,restoredOtherPage.currentPageHasContent,
					static_cast<unsigned long long>(restoredOtherPage.contentRevision),static_cast<unsigned long long>(afterUndo.contentRevision),
					static_cast<unsigned long long>(restoredOtherPage.successfulPresentCount),static_cast<unsigned long long>(afterUndo.successfulPresentCount));
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::PreviousPage) ==
					Bridge::CommandResult::Accepted, "return to cleared page accepted", failures);
				modeSucceeded &= Check(WaitUntil([restoredOtherPage]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.previousPageCommandCount >
							 restoredOtherPage.previousPageCommandCount &&
							state.currentPageIndex == 0 && state.currentPageHasContent &&
							state.successfulPresentCount > restoredOtherPage.successfulPresentCount;
					}), "undo-restored page keeps its content after page round-trip", failures);

				// 真实 Controller 三态回归：命令必须作用于发布时的 PPT，而不是随后的 latest scene。
				Bridge::ProductState penState{};
				penState.tool = Bridge::Tool::Pen;
				penState.selectionMode = false;
				PublishProductState(penState);
				// Clear沿用active.empty的提交边界；队列接受后等待Up，事务完成后旧事件不能复活内容。
				const auto beforeActiveClear=ProductHost().RuntimeSnapshot();
				postContact(HiddenTestContactPhase::Down,70,80);
				postContact(HiddenTestContactPhase::Move,100,90);
				modeSucceeded &= Check(WaitUntil([beforeActiveClear]{return ProductHost().RuntimeSnapshot().inputDownPublished>beforeActiveClear.inputDownPublished;}),"active contact starts before Clear",failures);
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Clear)==Bridge::CommandResult::Accepted,"active Clear accepted",failures);
				std::this_thread::sleep_for(50ms);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().clearCommandCount==beforeActiveClear.clearCommandCount,"Clear respects active contact boundary",failures);
				const auto afterActiveClear=ProductHost().RuntimeSnapshot();
				postContact(HiddenTestContactPhase::Up,130,100);
				modeSucceeded &= Check(WaitUntil([afterActiveClear]{const auto s=ProductHost().RuntimeSnapshot();return s.inputTerminalPublished>afterActiveClear.inputTerminalPublished && s.clearCommandCount==afterActiveClear.clearCommandCount+1 && !s.currentPageHasContent;}),"late Up after Clear cannot resurrect content",failures);
				postContact(HiddenTestContactPhase::Up,130,100);std::this_thread::sleep_for(50ms);
				modeSucceeded &= Check(!ProductHost().RuntimeSnapshot().currentPageHasContent,"duplicate late Up leaves Clear result intact",failures);
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([afterActiveClear]{const auto s=ProductHost().RuntimeSnapshot();return s.undoCommandCount>afterActiveClear.undoCommandCount && s.currentPageHasContent;}),"one Undo restores active Clear",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([afterActiveClear]{const auto s=ProductHost().RuntimeSnapshot();return s.redoCommandCount>afterActiveClear.redoCommandCount && !s.currentPageHasContent;}),"one Redo reapplies active Clear",failures);
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([]{return ProductHost().RuntimeSnapshot().currentPageHasContent;}),"restore Clear before scene tests",failures);
				const auto beforeFirstRecoveredStrokeUndo=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([beforeFirstRecoveredStrokeUndo]{const auto s=ProductHost().RuntimeSnapshot();return s.undoCommandCount>beforeFirstRecoveredStrokeUndo.undoCommandCount && s.currentPageHasContent && s.successfulPresentCount>beforeFirstRecoveredStrokeUndo.successfulPresentCount;}),"Clear-restored canvas keeps older strokes undoable",failures);
				const auto beforeSecondRecoveredStrokeUndo=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([beforeSecondRecoveredStrokeUndo]{const auto s=ProductHost().RuntimeSnapshot();return s.undoCommandCount>beforeSecondRecoveredStrokeUndo.undoCommandCount && !s.currentPageHasContent;}),"Clear-restored canvas can undo to empty",failures);
				const auto recoveredCanvasEmpty=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([recoveredCanvasEmpty]{return ProductHost().RuntimeSnapshot().undoCommandCount>recoveredCanvasEmpty.undoCommandCount;}),"Undo at recovered canvas root is consumed",failures);
				const auto afterBlockedOlderCanvasUndo=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(!afterBlockedOlderCanvasUndo.currentPageHasContent && afterBlockedOlderCanvasUndo.contentRevision==recoveredCanvasEmpty.contentRevision,"recovered canvas root cannot cross an older Clear",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([afterBlockedOlderCanvasUndo]{const auto s=ProductHost().RuntimeSnapshot();return s.redoCommandCount>afterBlockedOlderCanvasUndo.redoCommandCount && s.currentPageHasContent;}),"Redo after recovered canvas Undo restores a stroke",failures);
				const auto desktopBeforeInk = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postContact(HiddenTestContactPhase::Down, 56, 72) &&
					postContact(HiddenTestContactPhase::Move, 104, 96) &&
					postContact(HiddenTestContactPhase::Up, 152, 120),
					"draw persistent Desktop ink before Presentation switching", failures);
				modeSucceeded &= Check(WaitUntil([desktopBeforeInk]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.workspace == Bridge::Workspace::Desktop &&
							state.currentPageHasContent &&
							state.inputRecycled > desktopBeforeInk.inputRecycled &&
							state.successfulPresentCount > desktopBeforeInk.successfulPresentCount;
					}), "Desktop owns its ink before entering A", failures);

				const auto beforeInvalidRedo=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([beforeInvalidRedo]{const auto s=ProductHost().RuntimeSnapshot();return s.redoCommandCount>beforeInvalidRedo.redoCommandCount && s.currentPageHasContent;}),"new ink cancels old Clear redo",failures);

				Bridge::PresentationTarget targetA{};
				targetA.key.bytes[0] = 0xA1;
				targetA.bindingMode = Bridge::SlideBindingMode::StableSlideId;
				targetA.sourceIdentity = "path:c:\\hidden\\a.pptx";
				targetA.presentationName = "a.pptx";
				targetA.provider = "PowerPoint";
				targetA.bindingToken = "PowerPoint:1:101:1";
				targetA.slideIds = { 101 };
				targetA.slideId = 101;
				targetA.totalPages = 1;
				targetA.bindingRevision = 1;
				Bridge::PresentationTarget targetB = targetA;
				targetB.key.bytes[0] = 0xB2;
				targetB.sourceIdentity = "path:c:\\hidden\\b.pptx";
				targetB.presentationName = "b.pptx";
				targetB.bindingToken = "PowerPoint:1:202:1";
				targetB.slideIds = { 202 };
				targetB.slideId = 202;

				auto waitForPresentation = [&](const Bridge::PresentationTarget& target,
					std::uint64_t targetRevision, bool hasContent, const char* name)
					{
						return Check(WaitUntil([target, targetRevision, hasContent]
							{
								const auto state = ProductHost().RuntimeSnapshot();
								return state.workspace == Bridge::Workspace::Presentation &&
									state.currentPageHasContent == hasContent &&
									state.presentationReady &&
									state.presentationReady->key == target.key &&
									state.presentationReady->targetRevision == targetRevision &&
									state.presentationReady->slideId == target.slideId;
							}), name, failures);
					};

				const auto firstARevision = PublishProductPresentationTarget(targetA);
				modeSucceeded &= Check(firstARevision.has_value(),
					"publish Presentation A", failures);
				if (firstARevision)
					modeSucceeded &= waitForPresentation(targetA, *firstARevision, false,
						"A starts with its independent empty canvas");
				const auto aBeforeInk = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postContact(HiddenTestContactPhase::Down, 64, 80) &&
					postContact(HiddenTestContactPhase::Move, 112, 104) &&
					postContact(HiddenTestContactPhase::Up, 168, 136),
					"draw ink into Presentation A", failures);
				modeSucceeded &= Check(WaitUntil([aBeforeInk]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.workspace == Bridge::Workspace::Presentation &&
							state.currentPageHasContent &&
							state.contentRevision > aBeforeInk.contentRevision;
					}), "A owns its stored ink", failures);

				const auto beforeSceneStampedClear = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Clear) ==
					Bridge::CommandResult::Accepted,
					"queue Clear while A is the command scene", failures);
				PublishProductWorkspace(Bridge::Workspace::Desktop);
				modeSucceeded &= Check(WaitUntil([beforeSceneStampedClear]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.workspace == Bridge::Workspace::Desktop &&
							state.clearCommandCount >
								beforeSceneStampedClear.clearCommandCount &&
							state.currentPageHasContent;
					}), "A Clear executes before Desktop latest state and preserves Desktop ink",
					failures);

				const auto clearedARevision = PublishProductPresentationTarget(targetA);
				modeSucceeded &= Check(clearedARevision.has_value(),
					"re-enter cleared Presentation A", failures);
				if (clearedARevision)
					modeSucceeded &= waitForPresentation(targetA, *clearedARevision, false,
						"A remains empty after scene-stamped Clear");

				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([]{const auto s=ProductHost().RuntimeSnapshot();return s.workspace==Bridge::Workspace::Presentation && s.currentPageHasContent;}),"Presentation Clear Undo restores its boundary",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([]{const auto s=ProductHost().RuntimeSnapshot();return s.workspace==Bridge::Workspace::Presentation && !s.currentPageHasContent;}),"Presentation Clear Redo reuses its transaction",failures);
				const auto clearedABeforeInk = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postContact(HiddenTestContactPhase::Down, 72, 88) &&
					postContact(HiddenTestContactPhase::Move, 120, 112) &&
					postContact(HiddenTestContactPhase::Up, 176, 144),
					"draw new isolated ink into A", failures);
				modeSucceeded &= Check(WaitUntil([clearedABeforeInk]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.workspace == Bridge::Workspace::Presentation &&
							state.currentPageHasContent &&
							state.contentRevision > clearedABeforeInk.contentRevision;
					}), "A accepts new ink after Clear", failures);

				const auto beforeSecondPresentationClear=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Clear);
				modeSucceeded &= Check(WaitUntil([beforeSecondPresentationClear]{const auto s=ProductHost().RuntimeSnapshot();return s.clearCommandCount>beforeSecondPresentationClear.clearCommandCount && !s.currentPageHasContent;}),"second Presentation Clear creates a newer canvas",failures);
				const auto afterSecondPresentationClear=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([afterSecondPresentationClear]{const auto s=ProductHost().RuntimeSnapshot();return s.undoCommandCount>afterSecondPresentationClear.undoCommandCount && s.currentPageHasContent;}),"second Presentation Clear restores the previous canvas",failures);
				const auto restoredPreviousPresentationCanvas=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([restoredPreviousPresentationCanvas]{const auto s=ProductHost().RuntimeSnapshot();return s.undoCommandCount>restoredPreviousPresentationCanvas.undoCommandCount && !s.currentPageHasContent;}),"restored Presentation canvas can undo to empty",failures);
				const auto emptyPreviousPresentationCanvas=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([emptyPreviousPresentationCanvas]{return ProductHost().RuntimeSnapshot().undoCommandCount>emptyPreviousPresentationCanvas.undoCommandCount;}),"Presentation Undo at restored root is consumed",failures);
				const auto afterBlockedPresentationUndo=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(!afterBlockedPresentationUndo.currentPageHasContent && afterBlockedPresentationUndo.contentRevision==emptyPreviousPresentationCanvas.contentRevision,"Presentation Undo cannot cross to the canvas before the restored one",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([afterBlockedPresentationUndo]{const auto s=ProductHost().RuntimeSnapshot();return s.redoCommandCount>afterBlockedPresentationUndo.redoCommandCount && s.currentPageHasContent;}),"Presentation stroke Redo remains available after boundary restore",failures);

				const auto firstBRevision = PublishProductPresentationTarget(targetB);
				modeSucceeded &= Check(firstBRevision.has_value(),
					"switch directly from A to B", failures);
				if (firstBRevision)
					modeSucceeded &= waitForPresentation(targetB, *firstBRevision, false,
						"B does not inherit A ink and publishes B ready identity");
				const auto returnARevision = PublishProductPresentationTarget(targetA);
				modeSucceeded &= Check(returnARevision.has_value(),
					"switch directly from B back to A", failures);
				if (returnARevision)
					modeSucceeded &= waitForPresentation(targetA, *returnARevision, true,
						"A restores its own ink and exact ready revision after B");

				Bridge::ProductState whiteboardState;whiteboardState.workspace=Bridge::Workspace::Whiteboard;whiteboardState.tool=Bridge::Tool::Pen;whiteboardState.selectionMode=false;
				PublishProductState(whiteboardState);
				PublishProductWorkspace(Bridge::Workspace::Whiteboard);
				modeSucceeded &= Check(WaitUntil([]{return ProductHost().RuntimeSnapshot().workspace==Bridge::Workspace::Whiteboard;}),"switch to hidden whiteboard",failures);
				postContact(HiddenTestContactPhase::Down,70,80);postContact(HiddenTestContactPhase::Move,110,100);postContact(HiddenTestContactPhase::Up,150,120);
				modeSucceeded &= Check(WaitUntil([]{return ProductHost().RuntimeSnapshot().currentPageHasContent;}),"whiteboard has independent ink",failures);
				PublishProductCommand(Bridge::CommandType::Clear);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().currentPageHasContent;}),"whiteboard Clear empties annotation",failures);
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([]{return ProductHost().RuntimeSnapshot().currentPageHasContent;}),"whiteboard Clear Undo restores annotation",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().currentPageHasContent;}),"whiteboard Clear Redo reapplies transaction",failures);
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
				modeSucceeded &= CheckPenDwellAndResume(drawpad, failures);
				modeSucceeded &= CheckPenPhysicalRelease(drawpad, failures);
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


			if(exerciseEraser)
			{
				Bridge::ProductState speedState{};
				speedState.tool=Bridge::Tool::SpeedEraser;
				speedState.selectionMode=false;
				PublishProductState(speedState);
				const auto mouseContact=[&](HiddenTestContactPhase phase,int x,int y)
				{
					return PostMessageW(drawpad,kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(phase)|kHiddenTestMouseFlag,MAKELPARAM(x,y))!=FALSE;
				};
				modeSucceeded &= Check(mouseContact(HiddenTestContactPhase::Hover,60,80),"mouse fine hover input",failures);
				modeSucceeded &= Check(WaitUntil([]
				{
					const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.preview && d.effectiveDiameterDip<=16.1f && d.cursorDiameterPx>0;
				},3s),"real no-Move mouse hover reaches minimum",failures);
				modeSucceeded &= Check(mouseContact(HiddenTestContactPhase::Down,60,80),"DIP speed eraser down",failures);
				modeSucceeded &= Check(WaitUntil([]{return ProductHost().RuntimeSnapshot().eraser.active;}),
					"actual eraser diagnostic becomes active",failures);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().eraser.effectiveDiameterDip<=16.2f,
					"actual Down inherits fine hover without growing",failures);
				float fineMin=1000,fineMax=0;
				for(int i=1;i<=18;++i)
				{
					mouseContact(HiddenTestContactPhase::Move,60+i,80);
					std::this_thread::sleep_for(i%2?80ms:120ms);
					const auto d=ProductHost().RuntimeSnapshot().eraser;
					fineMin=(std::min)(fineMin,d.effectiveDiameterDip);fineMax=(std::max)(fineMax,d.effectiveDiameterDip);
					modeSucceeded &= Check(d.fine.held && d.effectiveDiameterDip<=16.5f &&
						std::abs(d.cursorDiameterPx-d.nextRadiusPx*2)<0.01f,
						"real sparse one-pixel mouse moves retain fine cursor and accepted radius",failures);
				}
				std::fprintf(stderr,"[FineIngress] mode=%u peakToPeakDIP=%g maxDIP=%g\n",
					static_cast<unsigned>(requiredMode),fineMax-fineMin,fineMax);
				int lastX=60;
				for(int i=0;i<80;++i)
				{
					lastX=(i%2)?60:250;
					mouseContact(HiddenTestContactPhase::Move,lastX,80);
					std::this_thread::sleep_for(20ms);
				}
				const auto large=ProductHost().RuntimeSnapshot();
				std::fprintf(stderr,"[EraserProbe] large active=%d device=%u dip=%.2f cursor=%.2f speed=%.2f evidence=%.3f points=%llu idle=%.3f\n",
					large.eraser.active,large.eraser.inputType,large.eraser.effectiveDiameterDip,
					large.eraser.cursorDiameterPx,large.eraser.speed,large.eraser.evidenceSeconds,
					static_cast<unsigned long long>(large.eraser.realPointCount),large.eraser.idleSeconds);

				modeSucceeded &= Check(large.eraser.cursorDiameterPx>large.eraser.dpiX/96*70 &&
					large.eraser.effectiveDiameterDip>70,"actual contact cursor reaches sweep size",failures);
				const auto moveCount=large.inputMovePublished;
				const auto pointCount=large.eraser.realPointCount;
				// 完全停止所有Move，包括光标消息；只让真实绘制线程的时钟运行。
				modeSucceeded &= Check(WaitUntil([moveCount]
				{
					const auto s=ProductHost().RuntimeSnapshot();
					return s.inputMovePublished==moveCount && s.eraser.active &&
						s.eraser.idleSeconds>=1.0 && s.eraser.cursorDiameterPx>0 &&
						s.eraser.cursorDiameterPx<=s.eraser.dpiX/96*18 &&
						s.eraser.nextRadiusPx<=s.eraser.dpiX/96*9.0f;
				},4s),"no Move: final contact cursor and next geometry visibly shrink",failures);
				const auto quiet=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(quiet.inputMovePublished==moveCount &&
					quiet.eraser.realPointCount==pointCount &&
					quiet.eraser.historyRadiusPx>quiet.eraser.nextRadiusPx*1.5f,
					"idle does not rewrite historical width or submit fake points",failures);
				modeSucceeded &= Check(WaitUntil([]
				{
					return ProductHost().RuntimeSnapshot().eraser.effectiveDiameterDip<=16.001f;
				},3s),"effective size settles at exact minimum",failures);
				const auto stopped=ProductHost().RuntimeSnapshot().eraser.frameSequence;
				std::this_thread::sleep_for(250ms);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().eraser.frameSequence<=stopped+2,
					"settled held eraser stops idle frames",failures);
				modeSucceeded &= Check(mouseContact(HiddenTestContactPhase::Move,lastX+4,84),
					"resume with one short real Move",failures);
				modeSucceeded &= Check(WaitUntil([moveCount]
				{
					const auto s=ProductHost().RuntimeSnapshot();
					return s.inputMovePublished>moveCount && s.eraser.resumedWithAnchor &&
						s.eraser.resumedMaxRadiusPx<=s.eraser.dpiX/96*10 &&
						s.eraser.resumedBottom-s.eraser.resumedTop<=s.eraser.dpiY/96*22+10;
				}),"resumed actual geometry footprint has no old large-radius tail",failures);
				modeSucceeded &= Check(CheckSizeBoundaryFile(ProductHost().RuntimeSnapshot().eraser),
					"actual size-boundary points survive UInk save/read/import",failures);
				mouseContact(HiddenTestContactPhase::Up,lastX+4,84);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
					"speed eraser up finishes normally",failures);
				const auto beforeUndo=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([beforeUndo]{return ProductHost().RuntimeSnapshot().undoCommandCount>beforeUndo.undoCommandCount;}),
					"size-break stroke supports real Undo",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([beforeUndo]{return ProductHost().RuntimeSnapshot().redoCommandCount>beforeUndo.redoCommandCount;}),
					"size-break stroke supports real Redo",failures);

				const auto postSource=[&](HiddenTestContactPhase phase,WPARAM flags,int x=80,int y=100)
				{return PostMessageW(drawpad,kDraw3HiddenTestContactMessage,static_cast<WPARAM>(phase)|flags,MAKELPARAM(x,y))!=FALSE;};
				SpeedEraser::DevelopmentOptions development;development.diagnostics=true;
				ProductHost().SetEraserDevelopmentOptions(development);
				for(const auto flags:{kHiddenTestExternalPenFlag,kHiddenTestIntegratedPenFlag,kHiddenTestTouchFlag,WPARAM{0}})
				{
					postSource(HiddenTestContactPhase::Down,flags);
					const auto expected=flags==kHiddenTestIntegratedPenFlag?SpeedEraser::ResponseModel::ScreenPenHybrid:
						flags==kHiddenTestTouchFlag?SpeedEraser::ResponseModel::DirectTouch:SpeedEraser::ResponseModel::IndirectDip;
					modeSucceeded &= Check(WaitUntil([expected,flags]
					{
						const auto d=ProductHost().RuntimeSnapshot().eraser;
						return d.active && d.response==expected && d.inputType==(flags==kHiddenTestTouchFlag?0u:1u);
					}),"actual pen/touch identity routes through the selected response",failures);
					modeSucceeded &= Check(WaitUntil([]
					{
						const auto d=ProductHost().RuntimeSnapshot().eraser;
						return d.active && d.effectiveDiameterDip<=16.1f;
					},3s),"actual pen or touch can become fine with no Move",failures);
					if(flags==kHiddenTestExternalPenFlag)
					{
						development.response=SpeedEraser::ResponseOverride::ScreenPenHybrid;
						ProductHost().SetEraserDevelopmentOptions(development);
						postSource(HiddenTestContactPhase::Move,flags,84,102);
						std::this_thread::sleep_for(60ms);
						modeSucceeded &= Check(ProductHost().RuntimeSnapshot().eraser.response==expected,
							"development selection does not change units inside an active contact",failures);
					}
					postSource(HiddenTestContactPhase::Cancelled,flags,84,102);
					modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
						"cancel closes true source without mouse lifecycle substitution",failures);
					if(flags==kHiddenTestExternalPenFlag)
					{
						postSource(HiddenTestContactPhase::Down,flags);
						modeSucceeded &= Check(WaitUntil([]
						{
							const auto d=ProductHost().RuntimeSnapshot().eraser;
							return d.active && d.inputType==1 && d.inputSource.kind==SpeedEraser::SourceKind::ExternalPen &&
								d.response==SpeedEraser::ResponseModel::ScreenPenHybrid;
						}),"next independent pen contact applies override without faking Touch",failures);
						postSource(HiddenTestContactPhase::Cancelled,flags);
						modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
							"forced-response contact completes",failures);
						development.response=SpeedEraser::ResponseOverride::Automatic;
						ProductHost().SetEraserDevelopmentOptions(development);
					}
				}
				ProductHost().SetEraserDevelopmentOptions({});
				// 合成手动大屏通过真实 Host/Down 路径解析；切场景只在下一次接触生效。
				auto classroomState=speedState;classroomState.paintDevice=0;
				PublishProductState(classroomState);
				SpeedEraser::DevelopmentOptions classroomDevelopment;classroomDevelopment.diagnostics=true;
				classroomDevelopment.touchAreaTrace=true; // 复用限频诊断观察运动期间速度，不逐包输出。
				classroomDevelopment.scale=SpeedEraser::ScaleOverride::ManualSurface;
				classroomDevelopment.calibration={1,139,78,0};
				ProductHost().SetEraserDevelopmentOptions(classroomDevelopment);
				modeSucceeded &= Check(WaitUntil([]{return ProductHost().EraserDisplayScaleSnapshot().development.scale==
					SpeedEraser::ScaleOverride::ManualSurface;}),"manual classroom scale reaches Host before new Down",failures);
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,120);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.response==SpeedEraser::ResponseModel::DirectTouch &&
						d.motionSource==SpeedEraser::ScaleSource::ManualCalibration &&
						d.requestedDeviceMode==SpeedEraser::DeviceMode::LargeScreen &&
						std::abs(d.sweepEnterSpeed-350)<0.01f && std::abs(d.largeTargetSpeed-1300)<0.01f &&
						std::abs(d.cursorDiameterPx-d.nextRadiusPx*2)<0.01f;}),
					"manual classroom Touch ingress latches scene curve and matching cursor",failures);
				const auto beforeClassroomMoves=ProductHost().RuntimeSnapshot().inputMovePublished;
				for(int i=1;i<=25;++i)
				{
					postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,60+i*2,120);
					std::this_thread::sleep_for(40ms);
				}
				modeSucceeded &= Check(WaitUntil([beforeClassroomMoves]{const auto s=ProductHost().RuntimeSnapshot();
					const auto& d=s.eraser;
					return s.inputMovePublished>=beforeClassroomMoves+25 && d.active &&
						d.effectiveDiameterDip>=31.5f && d.effectiveDiameterDip<=35.2f && d.targetDiameterDip<=35.2f &&
						std::abs(d.cursorDiameterPx-d.nextRadiusPx*2)<0.01f;}),
					"manual classroom ordinary local Touch remains near the selected B",failures);
				const auto classroomObserved=ProductHost().RuntimeSnapshot().eraser;
				std::fprintf(stderr,"[TouchSceneHidden] unit=%s speed=%.3f sweepSpeed=%.3f targetDIP=%.3f actualDIP=%.3f cursorPx=%.3f geometryPx=%.3f\n",
					SpeedEraser::MotionUnitName(classroomObserved.motionUnit),classroomObserved.speed,classroomObserved.sweepSpeed,
					classroomObserved.targetDiameterDip,classroomObserved.effectiveDiameterDip,
					classroomObserved.cursorDiameterPx,classroomObserved.nextRadiusPx*2);
				PublishProductState(speedState);
				std::this_thread::sleep_for(60ms);
				postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,112,120);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.requestedDeviceMode==SpeedEraser::DeviceMode::LargeScreen &&
						std::abs(d.largeTargetSpeed-1300)<0.01f;}),
					"active Touch keeps its latched scene after product setting changes",failures);
				postSource(HiddenTestContactPhase::Up,kHiddenTestTouchFlag,112,120);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
					"manual classroom contact closes before scene change",failures);
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,120);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.requestedDeviceMode==SpeedEraser::DeviceMode::Laptop &&
						std::abs(d.largeTargetSpeed-512.5f)<0.01f;}),
					"next Touch contact receives explicit Laptop cap with the same manual scale",failures);
				postSource(HiddenTestContactPhase::Cancelled,kHiddenTestTouchFlag,60,120);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
					"manual scene probe cancels without leaking contact",failures);
				ProductHost().SetEraserDevelopmentOptions({});
				// 面积辅助通过真实 mailbox、控制器、光标、模型和保存链路验收。
				const auto areaProbe=[&](const char* label)
				{
					const auto s=ProductHost().RuntimeSnapshot();const auto& d=s.eraser;
					std::fprintf(stderr,"[AreaProbe] %s mode=%u dip=%g cursor=%g floor=%g ref=%g active=%d age=%g reason=%d points=%llu history=%g anchor=%d radius=%g frame=%llu\n",
						label,static_cast<unsigned>(requiredMode),d.effectiveDiameterDip,d.cursorDiameterPx,d.contactArea.activeFloorDip,
						d.contactArea.referenceFloorDip,d.contactArea.active,d.idleSeconds,static_cast<int>(d.contactArea.reason),
						static_cast<unsigned long long>(d.realPointCount),d.historyRadiusPx,d.resumedWithAnchor,d.resumedMaxRadiusPx,
						static_cast<unsigned long long>(d.frameSequence));
				};
				const auto setAreaOption=[&](bool enabled)
				{
					auto options=ProductHost().EraserDevelopmentOptions();
					options.touchContactAreaAssistance=enabled;options.diagnostics=true;
					ProductHost().SetEraserDevelopmentOptions(options);
					return WaitUntil([enabled]{return ProductHost().RuntimeSnapshot().touchContactAreaAssistanceEnabled==enabled;});
				};
				modeSucceeded &= Check(setAreaOption(false),"apply independent area option",failures);
				postSource(HiddenTestContactPhase::Hover,kHiddenTestMouseFlag,60,80);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.preview && d.inputType==2 && d.effectiveDiameterDip<=16.01f;}),"prepare frozen mouse fine hover",failures);
				const auto beforeToggle=ProductHost().RuntimeSnapshot().eraser;
				modeSucceeded &= Check(setAreaOption(true),"enable touch-only area assistance",failures);
				std::this_thread::sleep_for(60ms);
				modeSucceeded &= Check(std::abs(ProductHost().RuntimeSnapshot().eraser.effectiveDiameterDip-beforeToggle.effectiveDiameterDip)<0.001f,
					"area toggle does not reset mouse fine hover",failures);
				const SpeedEraser::ContactLengthMetrics syntheticAxis{2,1000,0,30000,true};
				const SpeedEraser::ContactLengthMetrics syntheticSpan{2,10000,0,30000,true};
				const auto areaTransform=SpeedEraser::ResolveContactLengthTransform(syntheticAxis,syntheticSpan,1);
				const auto convertedArea=SpeedEraser::ConvertContactArea(300,200,areaTransform,areaTransform);
				modeSucceeded &= Check(convertedArea.units==SpeedEraser::ContactAreaUnits::CanvasPixels &&
					std::abs(convertedArea.widthPx-30)<0.001f && std::abs(convertedArea.heightPx-20)<0.001f,
					"synthetic metadata uses the product relative-length converter before eraser ingress",failures);
				ProductHost().SetHiddenTestContactArea(convertedArea);
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,140);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.inputType==0 && d.contactArea.enabled;}),"real Touch receives latched area option",failures);
				std::this_thread::sleep_for(120ms);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().eraser.effectiveDiameterDip<=16.01f,
					"Touch Down with a large reported finger still starts small",failures);
				int areaX=60;
				for(int i=1;i<=35;++i)
				{
					areaX=60+i;postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,areaX,140);
					std::this_thread::sleep_for(30ms);
				}
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.contactArea.active && d.effectiveDiameterDip>38.5f && d.effectiveDiameterDip<40.0f &&
					std::abs(d.cursorDiameterPx-d.nextRadiusPx*2)<0.01f;}),"slow Touch drag has matching area-assisted cursor and geometry",failures);
				const auto assisted=ProductHost().RuntimeSnapshot();
				ProductHost().SetHiddenTestContactArea({400,280,40,28,SpeedEraser::ContactAreaUnits::CanvasPixels});
				for(int i=0;i<6;++i)
				{
					postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,areaX,140);
					std::this_thread::sleep_for(30ms);
				}
				const auto pressed=ProductHost().RuntimeSnapshot();
				areaProbe("before-held-check");
				modeSucceeded &= Check(std::abs(pressed.eraser.contactArea.referenceFloorDip-39)<0.01f &&
					pressed.eraser.cursorDiameterPx<=assisted.eraser.cursorDiameterPx+0.05f,
					"larger same-position area does not create larger erasure",failures);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && !d.needsAnimation && d.contactArea.active &&
					std::abs(d.effectiveDiameterDip-d.contactArea.activeFloorDip)<0.001f;}),"held Touch settles at accepted assistance floor",failures);
				areaProbe("after-held-check");
				const auto resting=ProductHost().RuntimeSnapshot();
				std::this_thread::sleep_for(150ms);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().eraser.frameSequence<=resting.eraser.frameSequence+2,
					"stable assistance floor sleeps until data expiry",failures);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.effectiveDiameterDip<=16.01f &&
					 d.contactArea.reason==SpeedEraser::ContactAreaReason::Expired;},5s),
					"no Move: scheduled expiry releases stale assistance",failures);
				const auto expired=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(expired.eraser.historyRadiusPx>expired.eraser.nextRadiusPx*1.5f &&
					expired.eraser.realPointCount==resting.eraser.realPointCount,"area expiry preserves historical geometry",failures);
				// 明确覆盖真实长停顿，不只依赖约4秒的面积过期窗口。
				std::this_thread::sleep_for(7s);
				areaProbe("before-resume");
				postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,areaX+4,144);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.resumedWithAnchor && d.resumedMaxRadiusPx<=10 && d.idleModelReanchors>0 && d.evidenceSeconds<0.0001;}),"long-idle Touch resume preserves current radius without synthetic speed",failures);
				areaProbe("after-resume");
				modeSucceeded &= Check(CheckSizeBoundaryFile(ProductHost().RuntimeSnapshot().eraser),
					"area-assisted size boundary survives actual UInk save/read/import",failures);
				ProductHost().SetHiddenTestContactArea({0,0,0,0,SpeedEraser::ContactAreaUnits::CanvasPixels});
				postSource(HiddenTestContactPhase::Up,kHiddenTestTouchFlag,areaX+4,144);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
					"Touch terminal zero area completes without reopening erasure",failures);
				const auto areaHistory=ProductHost().RuntimeSnapshot();
				PublishProductCommand(Bridge::CommandType::Undo);
				modeSucceeded &= Check(WaitUntil([areaHistory]{return ProductHost().RuntimeSnapshot().undoCommandCount>areaHistory.undoCommandCount;}),
					"area geometry supports real Undo",failures);
				PublishProductCommand(Bridge::CommandType::Redo);
				modeSucceeded &= Check(WaitUntil([areaHistory]{return ProductHost().RuntimeSnapshot().redoCommandCount>areaHistory.redoCommandCount;}),
					"area geometry supports real Redo",failures);
				ProductHost().SetHiddenTestContactArea({300,200,30,20,SpeedEraser::ContactAreaUnits::CanvasPixels});
				modeSucceeded &= Check(setAreaOption(false),"disable area independently of Touch speed",failures);
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,170);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && !d.contactArea.enabled && d.effectiveDiameterDip<=16.01f;}),"next Touch contact uses disabled option and small start",failures);
				std::this_thread::sleep_for(7s);
				postSource(HiddenTestContactPhase::Up,kHiddenTestTouchFlag,60,170);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),"long held Touch Up ends without a model gap failure",failures);
				modeSucceeded &= Check(setAreaOption(true),"restore experimental setting for fixed bypass test",failures);
				auto fixedState=speedState;fixedState.tool=Bridge::Tool::FixedEraser;PublishProductState(fixedState);
				const auto fixedBefore=ProductHost().RuntimeSnapshot().inputDownPublished;
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,170);
				modeSucceeded &= Check(WaitUntil([fixedBefore]{const auto s=ProductHost().RuntimeSnapshot();
					return s.inputDownPublished>fixedBefore && std::abs(s.eraser.cursorDiameterPx-32)<0.01f;}),
					"fixed Touch eraser ignores area and uses default32 DIP",failures);
				postSource(HiddenTestContactPhase::Cancelled,kHiddenTestTouchFlag,60,170);
				ProductHost().SetHiddenTestContactArea({});

				// 五入口读取同一配置快照，但逐contact解析，不沿用首指的Fixed/Speed结果。
				auto entryState=speedState;entryState.tool=Bridge::Tool::ConfiguredEraser;
				const std::array<WPARAM,5> entryFlags={kHiddenTestMouseFlag,kHiddenTestRightMouseFlag,kHiddenTestTouchFlag,
					kHiddenTestIntegratedPenFlag,kHiddenTestIntegratedPenFlag|kHiddenTestPenTailFlag};
				SpeedEraser::DevelopmentOptions entryDevelopment;entryDevelopment.diagnostics=true;entryDevelopment.touchAreaTrace=true;
				ProductHost().SetEraserDevelopmentOptions(entryDevelopment);
				for(size_t entry=0;entry<entryFlags.size();++entry)
				for(const auto kind:{SpeedEraser::EraserKind::Fixed,SpeedEraser::EraserKind::Speed})
				{
					entryState.eraserInputs.entries[entry].kind=kind;
					PublishProductState(entryState);std::this_thread::sleep_for(40ms);
					const double before=ProductHost().RuntimeSnapshot().eraser.downSeconds;
					postSource(HiddenTestContactPhase::Down,entryFlags[entry],50+static_cast<int>(entry)*30,180);
					modeSucceeded &= Check(WaitUntil([entry,kind,before]
					{
						const auto d=ProductHost().RuntimeSnapshot().eraser;
						return d.eraserContact && d.downSeconds>before && d.entry==static_cast<SpeedEraser::InputEntry>(entry) &&
							d.eraserKind==kind && std::abs(d.downDiameterPx-d.firstPointRadiusPx*2)<0.01f &&
							(kind!=SpeedEraser::EraserKind::Fixed || std::abs(d.cursorDiameterPx-32)<0.01f);
					}),"five configured eraser inputs agree with first-point geometry",failures);
					postSource(HiddenTestContactPhase::Cancelled,entryFlags[entry],50+static_cast<int>(entry)*30,180);
					modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.eraserContact;}),
						"configured eraser input retires independently",failures);
				}
				// 全局尺寸变化经真实Host发布：固定光标、Down和首点共享B，活动接触保持旧B。
				for (auto base : {SpeedEraser::BaseSize::Small, SpeedEraser::BaseSize::Medium, SpeedEraser::BaseSize::Large})
				{
					entryState.eraserInputs.baseSize=base;
					SpeedEraser::SetGlobalAutomatic(entryState.eraserInputs,false);
					PublishProductState(entryState);std::this_thread::sleep_for(40ms);
					for(size_t entry=0;entry<entryFlags.size();++entry)
					{
						const double before=ProductHost().RuntimeSnapshot().eraser.downSeconds;
						postSource(HiddenTestContactPhase::Down,entryFlags[entry],130,160);
						modeSucceeded &= Check(WaitUntil([before,base]{const auto d=ProductHost().RuntimeSnapshot().eraser;
							return d.eraserContact && d.downSeconds>before && std::abs(d.downDiameterPx-static_cast<int>(base))<0.01f &&
								std::abs(d.downDiameterPx-d.firstPointRadiusPx*2)<0.01f;}),"globalB reaches five fixed entry first points",failures);
						postSource(HiddenTestContactPhase::Cancelled,entryFlags[entry],130,160);
						modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.eraserContact;}),"globalB contact retires",failures);
					}
				}
				entryState.eraserInputs.baseSize=SpeedEraser::BaseSize::Small;
				PublishProductState(entryState);std::this_thread::sleep_for(40ms);
				postSource(HiddenTestContactPhase::Down,entryFlags[0],100,160);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.eraserContact && std::abs(d.downDiameterPx-static_cast<int>(SpeedEraser::BaseSize::Small))<0.01f;}),"small active contact starts",failures);
				entryState.eraserInputs.baseSize=SpeedEraser::BaseSize::Large;
				PublishProductState(entryState);std::this_thread::sleep_for(60ms);
				postSource(HiddenTestContactPhase::Move,entryFlags[0],110,160);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.eraserContact && std::abs(d.cursorDiameterPx-static_cast<int>(SpeedEraser::BaseSize::Small))<0.01f;}),"active contact latches oldB across global change",failures);
				postSource(HiddenTestContactPhase::Cancelled,entryFlags[0],110,160);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.eraserContact;}),"latched contact ends",failures);
				entryState.eraserInputs=speedState.eraserInputs;
				// 笔尖/左键绘画仍是绘画；右键和笔尾是临时覆盖，不污染selectedTool。
				entryState.tool=Bridge::Tool::Pen;PublishProductState(entryState);std::this_thread::sleep_for(40ms);
				for(size_t entry:{size_t{0},size_t{3},size_t{1},size_t{4}})
				{
					const double before=ProductHost().RuntimeSnapshot().eraser.downSeconds;
					postSource(HiddenTestContactPhase::Down,entryFlags[entry],100,190);
					modeSucceeded &= Check(WaitUntil([before,entry]
					{
						const auto d=ProductHost().RuntimeSnapshot().eraser;
						return d.downSeconds>before && (entry==1 || entry==4?
							d.eraserContact && d.selectedTool!=d.effectiveTool:
							!d.eraserContact && d.selectedTool==d.effectiveTool);
					}),"right/tail override only erasing and leave ordinary drawing unchanged",failures);
					postSource(HiddenTestContactPhase::Cancelled,entryFlags[entry],100,190);
					std::this_thread::sleep_for(50ms);
				}
				entryState.tool=Bridge::Tool::ConfiguredEraser;
				entryState.eraserInputs.entries[0].kind=SpeedEraser::EraserKind::Fixed;
				entryState.eraserInputs.entries[1].kind=SpeedEraser::EraserKind::Speed;
				PublishProductState(entryState);std::this_thread::sleep_for(40ms);
				postSource(HiddenTestContactPhase::Down,entryFlags[0],40,180);
				std::this_thread::sleep_for(40ms);
				postSource(HiddenTestContactPhase::Down,entryFlags[1],260,180);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.eraserContact && d.entry==SpeedEraser::InputEntry::MouseRight && d.eraserKind==SpeedEraser::EraserKind::Speed;}),
					"same batch left Fixed and right Speed remain independent",failures);
				postSource(HiddenTestContactPhase::Cancelled,entryFlags[1],260,180);
				std::this_thread::sleep_for(40ms);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.eraserContact && d.entry==SpeedEraser::InputEntry::MouseLeft && d.eraserKind==SpeedEraser::EraserKind::Fixed;}),
					"retiring right does not replace left entry configuration",failures);
				postSource(HiddenTestContactPhase::Cancelled,entryFlags[0],40,180);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.eraserContact;}),
					"same-batch mouse contacts retire",failures);
				postSource(HiddenTestContactPhase::Hover,entryFlags[1],260,180);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.preview && d.entry==SpeedEraser::InputEntry::MouseRight &&
						d.inputType==3;}),
					"right mouse preview reports the right input type",failures);
				for(auto& setting:entryState.eraserInputs.entries)setting.kind=SpeedEraser::EraserKind::Speed;
				PublishProductState(entryState);std::this_thread::sleep_for(40ms);
				// 真实绘制链路复现无害诊断revision后的屏幕笔落笔，首点不能从16跳32/50。
				postSource(HiddenTestContactPhase::Hover,entryFlags[3],80,180);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.preview && d.entry==SpeedEraser::InputEntry::PenTip && d.effectiveDiameterDip<=16.1f;},3s),
					"integrated pen establishes fine Hover",failures);
				entryDevelopment.diagnostics=false;ProductHost().SetEraserDevelopmentOptions(entryDevelopment);
				std::this_thread::sleep_for(40ms);
				postSource(HiddenTestContactPhase::Down,entryFlags[3],80,180);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.entry==SpeedEraser::InputEntry::PenTip && d.sessionInherited &&
						d.downDiameterPx<=16.5f && std::abs(d.downDiameterPx-d.firstPointRadiusPx*2)<0.01f;}),
					"pen metadata-only revision preserves fine Down and actual first point",failures);
				postSource(HiddenTestContactPhase::Cancelled,entryFlags[3],80,180);
				std::this_thread::sleep_for(50ms);
				for(size_t entry:{size_t{0},size_t{1},size_t{3},size_t{4}})
				{
					postSource(HiddenTestContactPhase::Down,entryFlags[entry],40,180);
					std::this_thread::sleep_for(40ms);
					for(int n=0;n<65;++n)
					{
						postSource(HiddenTestContactPhase::Move,entryFlags[entry],n%2?40:270,180);
						std::this_thread::sleep_for(16ms);
					}
					int gapX=270,gapY=180;
					for(int gapMs:{20,50,100,200,500,2000})
					{
						const auto before=ProductHost().RuntimeSnapshot().eraser;
						postSource(HiddenTestContactPhase::Up,entryFlags[entry],gapX,gapY);
						std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
						// 远离旧落点，尺寸连续绝不作为断触连接证据。
						gapX=gapX==40?270:40;gapY=gapY==40?180:40;
						postSource(HiddenTestContactPhase::Down,entryFlags[entry],gapX,gapY);
						modeSucceeded &= Check(WaitUntil([before,entry]
						{
							const auto d=ProductHost().RuntimeSnapshot().eraser;
							return d.active && d.entry==static_cast<SpeedEraser::InputEntry>(entry) && d.downSeconds>before.downSeconds &&
								d.sessionInherited && std::abs(d.downDiameterPx-d.firstPointRadiusPx*2)<0.01f;
						}),"non-Touch independent Down inherits event-time size and matching first radius",failures);
						const auto after=ProductHost().RuntimeSnapshot().eraser;
						std::fprintf(stderr,"[EntryIngress] mode=%u entry=%zu gapMs=%d beforePx=%g downPx=%g firstRadius=%g cursorPx=%g points=%llu reason=%s\n",
							static_cast<unsigned>(requiredMode),entry,gapMs,before.cursorDiameterPx,after.downDiameterPx,
							after.firstPointRadiusPx,after.cursorDiameterPx,static_cast<unsigned long long>(after.realPointCount),after.sessionReason);
						modeSucceeded &= Check(after.realPointCount<=2,"independent Down starts a new geometry list, without a gap capsule",failures);
					}
					postSource(HiddenTestContactPhase::Cancelled,entryFlags[entry],gapX,gapY);
					std::this_thread::sleep_for(50ms);
				}

				ProductHost().SetEraserDevelopmentOptions({});


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

	int RunHiddenWindowIntegrationTest(bool eraserOnly) noexcept
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

			if(eraserOnly)
			{
				RunMode(service,styleContext,dcompMagnifierHost,dcompFreeze,dcompDrawpad,dcompPresentation,
					HostPresentationMode::Automatic,true,false,false,failures,true);
				StopProduct();
				service.StopAndJoin();
				if(!Check(service.Start(makeSpecs(false)),"fresh ULW eraser service",failures))return 1;
				RunMode(service,styleContext,service.Handle(Inkeys::Window::WindowRole::MagnifierHost),
					service.Handle(Inkeys::Window::WindowRole::Freeze),
					service.Handle(Inkeys::Window::WindowRole::Drawpad),
					service.Handle(Inkeys::Window::WindowRole::DrawpadPresentation),
					HostPresentationMode::UlwDirtyRect,false,false,false,failures,true);
				StopProduct();service.StopAndJoin();
				if(failures==0)Report("PASS","DIP eraser actual cursor, idle scheduling, geometry footprint and Undo/Redo");
				return failures==0?0:1;
			}
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
