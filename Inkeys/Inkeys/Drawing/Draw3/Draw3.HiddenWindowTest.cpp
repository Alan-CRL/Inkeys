#include "Draw3.HiddenWindowTest.h"
#include "Draw3.Product.h"
#include "Draw3.Presentation.h"
#include "Draw3.PresentationState.h"

import Inkeys.Window;
import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.drawing_controller;
import Inkeys.Drawing.Draw3.ink_prediction;
import draw3.uink_file;
import draw3.uink_draw3_import;

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <crtdbg.h>
#include <cstring>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <span>
#include <string>
#include <tchar.h>
#include <tpcshrd.h>
#include <thread>
#include <vector>
#include <variant>
#include <wrl/client.h>

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

		constexpr UINT kHiddenCaptureProbeMessage = WM_APP + 0x3D4u;
		LRESULT CALLBACK HiddenDrawpadWindowProc(HWND window, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			if (message == kHiddenCaptureProbeMessage)
			{
				if (wParam == 1) SetCapture(window);
				return GetCapture() == window ? 1 : 0;
			}
			return DrawpadMsgCallback(window, message, wParam, lParam);
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
			// 前一用例可能已处于白板绘制态；先经过可观察的选择态，避免旧工具尚未被消费就注入 Down。
			auto selectionState = penState;
			selectionState.selectionMode = true;
			PublishProductState(selectionState);
			bool succeeded = Check(WaitUntil([]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.selectionMode && state.workspace == Bridge::Workspace::Whiteboard;
			}), "pen dwell selection handoff", failures);
			PublishProductState(penState);
			succeeded &= Check(WaitUntil([]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return !state.selectionMode && state.workspace == Bridge::Workspace::Whiteboard;
			}), "pen dwell test uses the drawing workspace", failures);
			if (!succeeded) return false;
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

		bool CheckPresentationPersistence(Inkeys::Window::Service& service,
			HWND drawpad, HWND presentation,
			const HostStyleCallbacks& callbacks, HostStartOptions options, int& failures)
		{
			// 独立真实 Host / worker 事务；测试文件只写到当前 Build 产物旁的唯一目录，留供复核。
			StopProduct();
			wchar_t imagePath[32768]{};
			const DWORD imageLength = GetModuleFileNameW(nullptr, imagePath, 32768);
			if (!Check(imageLength > 0 && imageLength < 32768,
				"resolve hidden persistence artifact directory", failures)) return false;
			LARGE_INTEGER started{};
			QueryPerformanceCounter(&started);
			const auto root = std::filesystem::path(imagePath).parent_path() /
				L"Draw3HiddenPptPersistence" / (std::to_wstring(GetCurrentProcessId()) +
					L"-" + std::to_wstring(started.QuadPart));
			std::error_code directoryError;
			std::filesystem::create_directories(root, directoryError);
			if (!Check(!directoryError, "create isolated presentation persistence root", failures)) return false;
			options.autoSaveRoot = root.wstring();
			std::fprintf(stderr, "[Draw3Hidden] persistence_root=%ls\n", options.autoSaveRoot.c_str());
			if (!Check(StartProduct(drawpad, presentation, callbacks, options),
				"start hidden Host with real persistence worker", failures)) return false;

			PresentationDescriptor descriptor;
			descriptor.status = PresentationDescriptorStatus::StableSlideIds;
			descriptor.provider = "PowerPoint";
			descriptor.fullName = "C:\\hidden\\persistence-a.pptx";
			descriptor.presentationName = "persistence-a.pptx";
			descriptor.applicationProcessId = static_cast<std::int32_t>(GetCurrentProcessId());
			descriptor.slideShowHwnd = reinterpret_cast<std::intptr_t>(drawpad);
			descriptor.currentPage = 1;
			descriptor.totalPage = 3;
			descriptor.currentSlideId = 601;
			descriptor.slideIds = { 601, 602, 603 };
			descriptor.bindingRevision = 1;
			auto resolvedA = ResolvePresentationTarget(descriptor);
			descriptor.fullName = "C:\\hidden\\persistence-b.pptx";
			descriptor.presentationName = "persistence-b.pptx";
			descriptor.currentSlideId = 701;
			descriptor.slideIds = { 701, 702, 703 };
			auto resolvedB = ResolvePresentationTarget(descriptor);
			if (!Check(resolvedA && resolvedB,
				"derive persisted identities using production descriptor resolver", failures)) return false;
			auto targetA = *resolvedA;
			auto targetB = *resolvedB;
			targetA.sessionRevision = 31;
			targetB.sessionRevision = 32;
			bool succeeded = true;
			const auto select = [&](Bridge::PresentationTarget& target, bool hasContent, const char* name)
			{
				const auto accepted = PublishProductPresentationTarget(target);
				if (!Check(accepted.has_value(), "persisted target accepted", failures)) return false;
				target.targetRevision = *accepted;
				const auto identity = Bridge::ReadyIdentityFor(target);
				const bool ready = Check(WaitUntil([identity, hasContent]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.workspace == Bridge::Workspace::Presentation &&
						state.presentationReady == identity && state.currentPageHasContent == hasContent;
				}), name, failures);
				return ready && Check(PublishProductPresentationUiReady(identity),
					"persisted target receives matching page UI commit", failures);
			};
			Bridge::ProductState pen;
			pen.tool = Bridge::Tool::Pen;
			pen.selectionMode = false;
			PublishProductState(pen);
			succeeded &= select(targetA, false, "persistent A starts with an empty first page");
			const auto firstIdentity = Bridge::ReadyIdentityFor(targetA);
			const auto beforeHeld = ProductHost().RuntimeSnapshot();
			const auto post = [&](HiddenTestContactPhase phase, int x, int y)
			{
				return PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
					static_cast<WPARAM>(phase), MAKELPARAM(x, y)) != FALSE;
			};
			const auto writePage = [&](int x, int y, const char* name)
			{
				const bool posted = post(HiddenTestContactPhase::Down, x, y) &&
					post(HiddenTestContactPhase::Move, x + 45, y + 20) &&
					post(HiddenTestContactPhase::Up, x + 60, y + 25);
				return Check(posted && WaitUntil([]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.currentPageHasContent && !state.pen.active;
				}, 3s), name, failures);
			};
			succeeded &= Check(post(HiddenTestContactPhase::Down, 42, 50) &&
				post(HiddenTestContactPhase::Move, 110, 72), "post persistent held stroke", failures);
			succeeded &= Check(WaitUntil([beforeHeld]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.pen.active && state.pen.strokeId > beforeHeld.pen.strokeId && state.pen.inputSequence > 2;
			}), "persistent old page accepted still-down ink", failures);
			targetA.pageIndex = 1;
			targetA.slideId = 602;
			succeeded &= select(targetA, false, "held ink seals and saves before second page becomes ready");
			succeeded &= Check(!PublishProductPresentationUiReady(firstIdentity),
				"late first-page UI commit stays rejected with persistence enabled", failures);
			succeeded &= Check(post(HiddenTestContactPhase::Move, 210, 150) &&
				post(HiddenTestContactPhase::Up, 240, 180), "retire old physical contact after persistent switch", failures);
			succeeded &= Check(WaitUntil([beforeHeld]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.inputRecycled > beforeHeld.inputRecycled && !state.currentPageHasContent;
			}), "late physical terminal cannot contaminate persisted second page", failures);
			// 末张真实幻灯片与结束页写不同笔迹，跨页和冷读均不能把它们合并。
			targetA.pageIndex = 2;
			targetA.slideId = 603;
			succeeded &= select(targetA, false, "last real slide starts empty");
			succeeded &= writePage(55, 60, "last real slide accepts ink A");
			targetA.pageKind = Bridge::PresentationPageKind::EndScreen;
			targetA.pageIndex = targetA.totalPages;
			targetA.slideId.reset();
			succeeded &= select(targetA, false, "first end screen has an independent empty canvas");
			succeeded &= writePage(150, 80, "end screen accepts ink B");
			succeeded &= Check(PublishProductCommand(Bridge::CommandType::Undo) ==
			Bridge::CommandResult::Accepted && WaitUntil([]
			{
				return !ProductHost().RuntimeSnapshot().currentPageHasContent;
			}, 3s), "end screen Undo affects only its own ink", failures);
			succeeded &= Check(PublishProductCommand(Bridge::CommandType::Redo) ==
			Bridge::CommandResult::Accepted && WaitUntil([]
			{
				return ProductHost().RuntimeSnapshot().currentPageHasContent;
			}, 3s), "end screen Redo restores only its own ink", failures);
			const auto beforeEndClear = ProductHost().RuntimeSnapshot();
			succeeded &= Check(PublishProductCommand(Bridge::CommandType::Clear) ==
				Bridge::CommandResult::Accepted && WaitUntil([beforeEndClear]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.clearCommandCount > beforeEndClear.clearCommandCount &&
					!state.currentPageHasContent;
			}, 3s), "end screen Clear changes only its own canvas", failures);
			succeeded &= Check(PublishProductCommand(Bridge::CommandType::Undo) ==
				Bridge::CommandResult::Accepted && WaitUntil([]
			{
				return ProductHost().RuntimeSnapshot().currentPageHasContent;
			}, 5s), "end screen Undo restores its Clear boundary", failures);
			succeeded &= Check(PublishProductCommand(Bridge::CommandType::Redo) ==
				Bridge::CommandResult::Accepted && WaitUntil([]
			{
				return !ProductHost().RuntimeSnapshot().currentPageHasContent;
			}, 5s), "end screen Redo clears only its own canvas", failures);
			succeeded &= Check(PublishProductCommand(Bridge::CommandType::Undo) ==
				Bridge::CommandResult::Accepted && WaitUntil([]
			{
				return ProductHost().RuntimeSnapshot().currentPageHasContent;
			}, 5s), "end screen remains writable after Clear history", failures);
			targetA.pageKind = Bridge::PresentationPageKind::Slide;
			targetA.pageIndex = 2;
			targetA.slideId = 603;
			succeeded &= select(targetA, true, "return from end screen restores last real slide ink A");
			targetA.pageKind = Bridge::PresentationPageKind::EndScreen;
			targetA.pageIndex = targetA.totalPages;
			targetA.slideId.reset();
			succeeded &= select(targetA, true, "return to end screen restores its own ink B");
			succeeded &= select(targetB, false, "dirty A parks at independent persistent B");
			const auto beforeRestart = Bridge::ReadyIdentityFor(targetB);
			StopProduct(); // 最终屏障与 CloseAndDrain 必须使已接受的旧页保存落盘。
			const auto presentationRoot = root / L"presentation";
			succeeded &= Check(std::filesystem::is_regular_file(presentationRoot / L"index.json"),
				"real controller save committed the presentation index", failures);
			bool completeFile = false;
			bool separateEndContents = false;
			bool distinctInkFingerprints = false;
			const auto inkFingerprint = [](const draw3::uink::UInkCanvas& canvas)
			{
				std::uint64_t hash = 1469598103934665603ull;
				for (const auto& item : canvas.content)
					if (const auto* ink = std::get_if<draw3::uink::UInkInk>(&item))
					{
						hash = (hash ^ ink->points.size()) * 1099511628211ull;
						for (const auto& point : ink->points)
						{
							hash = (hash ^ std::bit_cast<std::uint32_t>(point.x)) *
								1099511628211ull;
							hash = (hash ^ std::bit_cast<std::uint32_t>(point.y)) *
								1099511628211ull;
						}
					}
				return hash;
			};
			const auto files = presentationRoot / L"files";
			if (std::filesystem::is_directory(files))
				for (const auto& file : std::filesystem::directory_iterator(files))
				{
					if (!file.is_regular_file() || file.path().extension() != L".uink") continue;
					const auto read = draw3::uink::ReadUInkFile(file.path().wstring());
					const bool complete = read.status == draw3::uink::UInkReadStatus::Complete && read.document;
					succeeded &= Check(complete, "drained controller file is a complete readable UInk transaction", failures);
					completeFile = completeFile || complete;
					if (complete)
					{
						const draw3::uink::UInkCanvas* firstSlide = nullptr;
						const draw3::uink::UInkCanvas* lastSlide = nullptr;
						const draw3::uink::UInkCanvas* endScreen = nullptr;
						for (const auto& canvas : read.document->canvases)
						{
							if (canvas.slideId == 601) firstSlide = &canvas;
							if (canvas.slideId == 603) lastSlide = &canvas;
							if (draw3::uink::InkeysPageKind(canvas.extra) ==
								draw3::uink::UInkInkeysPageKind::EndScreen) endScreen = &canvas;
						}
						separateEndContents = separateEndContents || (lastSlide && endScreen &&
							lastSlide->pageGuid != endScreen->pageGuid &&
							!lastSlide->content.empty() && !endScreen->content.empty() &&
							endScreen->pageIndex == 3 && !endScreen->slideId);
						// 文件内真实笔迹坐标区分 A/B/Z，配合下方目标身份与成功 ULW Present 校验。
						if (firstSlide && lastSlide && endScreen &&
							!firstSlide->content.empty() && !lastSlide->content.empty() &&
							!endScreen->content.empty())
						{
							const auto a = inkFingerprint(*lastSlide);
							const auto b = inkFingerprint(*firstSlide);
							const auto z = inkFingerprint(*endScreen);
							distinctInkFingerprints = distinctInkFingerprints ||
								(a != b && a != z && b != z);
						}
					}
				}
			succeeded &= Check(completeFile, "held-contact boundary produced durable UInk ink", failures);
			succeeded &= Check(separateEndContents,
				"one UInk file contains distinct last-slide and marked end-page content", failures);
			succeeded &= Check(distinctInkFingerprints,
				"real UInk coordinates distinguish Selection A/B/Z content", failures);
			if (!Check(StartProduct(drawpad, presentation, callbacks, options),
				"restart hidden Host from the same isolated persistence root", failures)) return false;
			const auto restarted = ProductHost().RuntimeSnapshot();
			succeeded &= Check(restarted.running &&
				restarted.workspace == Bridge::Workspace::Desktop &&
				!restarted.presentationReady && !restarted.presentationUiReady,
				"Host restart clears old presentation and UI readiness", failures);
			PublishProductState(pen);
			// 全新 Controller 没有 warm slot；直接以结束页目标冷读，再核对真实 SlideID 重排。
			++targetA.sessionRevision;
			targetA.slideIds = { 603, 601, 602 };
			targetA.pageKind = Bridge::PresentationPageKind::EndScreen;
			targetA.pageIndex = targetA.totalPages;
			targetA.slideId.reset();
			succeeded &= select(targetA, true,
				"cold Host enters end screen before any normal slide and restores ink B");
			targetA.pageKind = Bridge::PresentationPageKind::Slide;
			targetA.pageIndex = 1;
			targetA.slideId = 601;
			succeeded &= select(targetA, true, "cold reload restores sealed old-page ink at reordered SlideID");
			succeeded &= Check(!PublishProductPresentationUiReady(beforeRestart),
				"previous Host UI commit cannot open the restarted Host", failures);
			targetA.pageIndex = 0;
			targetA.slideId = 603;
			succeeded &= select(targetA, true, "cold reorder preserves last-slide ink A");
			targetA.pageIndex = 2;
			targetA.slideId = 602;
			succeeded &= select(targetA, false, "cold reload proves late old-contact packets never saved onto the second SlideID");
			targetA.pageKind = Bridge::PresentationPageKind::EndScreen;
			targetA.pageIndex = targetA.totalPages;
			targetA.slideId.reset();
			succeeded &= select(targetA, true, "cold reload restores end-page ink B after normal SlideID reorder");
			// 选择态连续切换有内容页时，布尔值相同仍需等待新内容版本与 ULW 呈现收敛。
			Bridge::ProductState selectionView = pen;
			selectionView.selectionMode = true;
			PublishProductState(selectionView);
			const auto selectInSelection = [&](Bridge::PresentationTarget& target,
				bool hasContent, const char* name)
			{
				const auto before = ProductHost().RuntimeSnapshot();
				if (!select(target, hasContent, name)) return false;
				const auto identity = Bridge::ReadyIdentityFor(target);
				const bool contentWake = ProductHost().WaitForContentRevision(
					before.contentRevision, 2000);
				const bool settled = WaitUntil([identity, before, hasContent]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.running && state.selectionMode &&
						state.workspace == Bridge::Workspace::Presentation &&
						state.presentationReady == identity &&
						state.currentPageHasContent == hasContent &&
						state.contentRevision > before.contentRevision &&
						state.presentedContentRevision == state.contentRevision &&
						state.requestedOutputTarget == HostOutputTarget::SelectionUlw &&
						state.readyOutputTarget == HostOutputTarget::SelectionUlw &&
						state.readyOutputRevision == state.requestedOutputRevision &&
						(hasContent || state.auxiliaryFullFrameClean);
				}, 3s);
				if (!Check(contentWake && settled,
					"Selection page content revision reaches the presented ULW frame", failures))
				{
					const auto state = ProductHost().RuntimeSnapshot();
					std::fprintf(stderr,
						"[Draw3Hidden] Selection %s page=%zu has=%d content=%llu presented=%llu output=%u/%u revision=%llu/%llu\n",
						name, state.currentPageIndex, state.currentPageHasContent,
						static_cast<unsigned long long>(state.contentRevision),
						static_cast<unsigned long long>(state.presentedContentRevision),
						static_cast<unsigned>(state.requestedOutputTarget),
						static_cast<unsigned>(state.readyOutputTarget),
						static_cast<unsigned long long>(state.requestedOutputRevision),
						static_cast<unsigned long long>(state.readyOutputRevision));
					return false;
				}
				const auto state = ProductHost().RuntimeSnapshot();
				const auto surface = ResolveDrawpadPresentationSurface(
					state.selectionMode, state.currentPageHasContent,
					state.auxiliaryFullFrameClean);
				const auto expected = hasContent
					? Inkeys::Window::DrawpadSurfaceVisibility::Presentation
					: Inkeys::Window::DrawpadSurfaceVisibility::Hidden;
				return Check(surface == (hasContent
					? DrawpadPresentationSurface::Presentation
					: DrawpadPresentationSurface::Hidden) &&
					service.SetDrawpadSurfaceVisibility(expected) &&
					!IsWindowVisible(drawpad) &&
					((IsWindowVisible(presentation) != FALSE) == hasContent) &&
					(!hasContent || CheckPresentationWindowStyle(presentation, failures)),
					name, failures);
			};
			targetA.pageKind = Bridge::PresentationPageKind::Slide;
			targetA.pageIndex = 0;
			targetA.slideId = 603;
			succeeded &= selectInSelection(targetA, true, "Selection A shows the real slide ink");
			targetA.pageIndex = 1;
			targetA.slideId = 601;
			succeeded &= selectInSelection(targetA, true, "Selection B shows another inked slide");
			targetA.pageIndex = 0;
			targetA.slideId = 603;
			succeeded &= selectInSelection(targetA, true, "Selection returns to A without tool change");
			targetA.pageIndex = 2;
			targetA.slideId = 602;
			succeeded &= selectInSelection(targetA, false, "Selection E hides an empty slide");
			targetA.pageIndex = 1;
			targetA.slideId = 601;
			succeeded &= selectInSelection(targetA, true, "Selection returns to B");
			targetA.pageKind = Bridge::PresentationPageKind::EndScreen;
			targetA.pageIndex = targetA.totalPages;
			targetA.slideId.reset();
			succeeded &= selectInSelection(targetA, true, "Selection Z shows independent end ink");
			targetA.pageKind = Bridge::PresentationPageKind::Slide;
			targetA.pageIndex = 0;
			targetA.slideId = 603;
			succeeded &= selectInSelection(targetA, true, "Selection returns from Z to A");
			targetA.pageIndex = 2;
			targetA.slideId = 602;
			succeeded &= selectInSelection(targetA, false, "Selection returns to empty E");
			succeeded &= selectInSelection(targetB, false, "Selection changes between two empty documents");
			succeeded &= selectInSelection(targetA, false, "Selection restores empty E after document switch");
			targetA.pageKind = Bridge::PresentationPageKind::EndScreen;
			targetA.pageIndex = targetA.totalPages;
			targetA.slideId.reset();
			succeeded &= selectInSelection(targetA, true, "Selection returns to Z before held-contact exit");
			const auto beforeDuplicate = ProductHost().RuntimeSnapshot();
			const auto duplicateRevision = PublishProductPresentationTarget(targetA);
			succeeded &= Check(duplicateRevision == targetA.targetRevision &&
				ProductHost().RuntimeSnapshot().contentRevision == beforeDuplicate.contentRevision,
				"identical accepted target does not republish content", failures);
			succeeded &= Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden),
				"hide Selection ULW before returning to pen", failures);
			PublishProductState(pen);
			succeeded &= Check(WaitUntil([targetA]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return !state.selectionMode &&
					state.presentationReady == Bridge::ReadyIdentityFor(targetA) &&
					state.requestedOutputTarget == HostOutputTarget::PrimaryDrawpad &&
					state.readyOutputTarget == HostOutputTarget::PrimaryDrawpad &&
					state.readyOutputRevision == state.requestedOutputRevision &&
					state.presentedContentRevision == state.contentRevision;
			}, 3s), "pen mode remains usable after Selection paging", failures);
			// 真退出时仍按下的旧笔只在结束页收尾；它的后续 Move/Up 不得写到桌面。
			const auto beforeExit = ProductHost().RuntimeSnapshot();
			succeeded &= Check(post(HiddenTestContactPhase::Down, 70, 90) &&
				post(HiddenTestContactPhase::Move, 125, 115) && WaitUntil([]
				{
					return ProductHost().RuntimeSnapshot().pen.active;
				}, 2s), "end-page held contact accepted before real workspace exit", failures);
			PublishProductWorkspace(Bridge::Workspace::Desktop);
			Bridge::ProductState selection = pen;
			selection.selectionMode = true;
			PublishProductState(selection);
			succeeded &= Check(WaitUntil([]
			{
				const auto state = ProductHost().RuntimeSnapshot();
				return state.workspace == Bridge::Workspace::Desktop &&
					state.selectionMode && !state.currentPageHasContent && !state.pen.active;
			}, 3s), "held end stroke settles before empty desktop selection", failures);
			succeeded &= Check(post(HiddenTestContactPhase::Move, 180, 150) &&
				post(HiddenTestContactPhase::Up, 185, 155) && WaitUntil([beforeExit]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.inputRecycled > beforeExit.inputRecycled &&
						!state.currentPageHasContent;
				}, 3s), "late physical terminal cannot write the new desktop", failures);
			succeeded &= Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden),
				"restore hidden integration windows after Selection paging", failures);
			if (succeeded) Report("PASS", "real Host held-contact save, drain, cold reload and SlideID reorder");
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
			options.requirePresentationUiReady = true;
			if (exerciseCommands)
			{
				// PPT 命令必须经过真实异步加载；缺少 root 会让 Controller 按设计阻止输入。
				wchar_t imagePath[32768]{};
				const DWORD imageLength = GetModuleFileNameW(nullptr, imagePath, 32768);
				if (!Check(imageLength > 0 && imageLength < 32768,
					"resolve hidden command persistence directory", failures)) return false;
				LARGE_INTEGER started{};
				if (!Check(QueryPerformanceCounter(&started) != FALSE,
					"identify hidden command persistence run", failures)) return false;
				const auto parent = std::filesystem::path(imagePath).parent_path() /
					L"Draw3HiddenPptCommands";
				std::error_code directoryError;
				std::filesystem::create_directories(parent, directoryError);
				const DWORD parentAttributes = GetFileAttributesW(parent.c_str());
				if (!Check(!directoryError && parentAttributes != INVALID_FILE_ATTRIBUTES &&
					(parentAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
					(parentAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
					"create non-reparse hidden command persistence parent", failures)) return false;
				const auto root = parent / (std::to_wstring(GetCurrentProcessId()) +
					L"-" + std::to_wstring(started.QuadPart));
				const bool created = std::filesystem::create_directory(root, directoryError);
				const DWORD rootAttributes = GetFileAttributesW(root.c_str());
				if (!Check(created && !directoryError && rootAttributes != INVALID_FILE_ATTRIBUTES &&
					(rootAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
					(rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
					"create unique non-reparse hidden command persistence root", failures)) return false;
				options.autoSaveRoot = root.wstring();
				std::fprintf(stderr, "[Draw3Hidden] command_persistence_root=%ls\n",
					options.autoSaveRoot.c_str());
			}
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
			const auto modeDownBaseline=snapshot.inputDownPublished;
			const auto modeRecycledBaseline=snapshot.inputRecycled;
			modeSucceeded &= Check(snapshot.running && snapshot.firstFrameReady &&
				snapshot.lastPresentSucceeded && snapshot.successfulPresentCount >= 1,
				"first transparent frame", failures);
			if (requiredMode != HostPresentationMode::Automatic)
				modeSucceeded &= Check(snapshot.presentationMode == requiredMode,
					"forced presenter used the requested real backend", failures);
			else
				modeSucceeded &= Check(
					snapshot.presentationMode == HostPresentationMode::UlwDirtyRect ||
					(allowDirectComposition && snapshot.presentationMode ==
						HostPresentationMode::DirectCompositionVisualTree),
					"automatic fallback selected only DComp or ULW", failures);
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
				GetWindow(drawpad, GW_OWNER) == presentation &&
				GetWindow(presentation, GW_OWNER) == freeze,
				"Drawpad remains owned by Presentation below Freeze", failures);
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
			modeSucceeded &= Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Primary) &&
				IsWindowVisible(drawpad) && !IsWindowVisible(presentation),
				"real Host primary output has a visible non-through Drawpad", failures);
			modeSucceeded &= Check(SendMessageW(drawpad, kHiddenCaptureProbeMessage, 1, 0) == 1,
				"owner thread captures only the primary Drawpad for exit probe", failures);
			Bridge::ProductState selectionState{};
			selectionState.selectionMode = true;
			PublishProductState(selectionState);
			// 模拟退出时 Selection 先于完整 ULW 帧到达，窗口事务先撤下旧主窗。
			modeSucceeded &= Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden) &&
				!IsWindowVisible(drawpad) && !IsWindowVisible(presentation),
				"waiting Selection hides the old primary HWND", failures);
			modeSucceeded &= Check(SendMessageW(drawpad, kHiddenCaptureProbeMessage, 0, 0) == 0,
				"hiding Selection releases primary Drawpad capture on its owner thread", failures);
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
			modeSucceeded &= Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Presentation) &&
				!IsWindowVisible(drawpad) && IsWindowVisible(presentation) &&
				(static_cast<DWORD>(GetWindowLongPtrW(presentation, GWL_EXSTYLE)) &
					(WS_EX_LAYERED | WS_EX_TRANSPARENT)) ==
					(WS_EX_LAYERED | WS_EX_TRANSPARENT),
				"ready Selection restores only the layered through surface", failures);
			modeSucceeded &= Check(service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden) &&
				!IsWindowVisible(drawpad) && !IsWindowVisible(presentation),
				"Selection transition restores hidden offscreen windows", failures);

			if (exerciseCommands)
			{
				// 通过隐藏 Drawpad 的 WndProc mailbox 注入完整 Down/Move/Up，
				// 不调用 SendInput，也不绕过真实绘制线程直接访问 Renderer。
				const auto beforeContact = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(beforeContact.completedStrokeKind ==
					Bridge::CompletedStrokeKind::None,
					"fresh Host has no completed stroke kind", failures);
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
							state.currentPageHasContent &&
							state.completedStrokeKind == Bridge::CompletedStrokeKind::Drawing;
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
							!state.currentPageHasContent &&
							state.completedStrokeKind == Bridge::CompletedStrokeKind::Drawing;
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
				std::this_thread::sleep_for(40ms); // 先让异步工具状态到达绘制线程，再注入实际笔划。
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
							state.contentRevision > pageOneEmpty.contentRevision &&
							state.completedStrokeKind == Bridge::CompletedStrokeKind::Eraser;
					}), "eraser history counts as content on a visually blank page", failures);

				const auto beforePreviousPage = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::PreviousPage) ==
					Bridge::CommandResult::Accepted, "previous page command accepted", failures);
				modeSucceeded &= Check(WaitUntil([beforePreviousPage]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.previousPageCommandCount >
							beforePreviousPage.previousPageCommandCount &&
							state.currentPageIndex == 0 && state.currentPageHasContent &&
							state.completedStrokeKind == Bridge::CompletedStrokeKind::Eraser;
					}), "returning to the first page restores its content state", failures);

				const auto beforeClear = ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Clear) ==
					Bridge::CommandResult::Accepted, "clear command accepted", failures);
				modeSucceeded &= Check(WaitUntil([beforeClear]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.clearCommandCount > beforeClear.clearCommandCount &&
							!state.currentPageHasContent &&
							state.contentRevision > beforeClear.contentRevision &&
							state.completedStrokeKind == Bridge::CompletedStrokeKind::Eraser;
					}), "clear publishes an empty current page", failures);
				const auto afterClear = ProductHost().RuntimeSnapshot();

				modeSucceeded &= Check(PublishProductCommand(Bridge::CommandType::Undo) ==
					Bridge::CommandResult::Accepted, "undo after clear accepted", failures);
				modeSucceeded &= Check(WaitUntil([afterClear]
					{
						const auto state = ProductHost().RuntimeSnapshot();
						return state.undoCommandCount > afterClear.undoCommandCount &&
							state.currentPageHasContent &&
							state.contentRevision > afterClear.contentRevision &&
							state.completedStrokeKind == Bridge::CompletedStrokeKind::Eraser;
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
				targetA.sessionRevision = 11;
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
						const bool ready = Check(WaitUntil([target, targetRevision, hasContent]
							{
								const auto state = ProductHost().RuntimeSnapshot();
								return state.workspace == Bridge::Workspace::Presentation &&
									state.currentPageHasContent == hasContent &&
									state.presentationReady &&
									state.presentationReady->key == target.key &&
									state.presentationReady->targetRevision == targetRevision &&
									state.presentationReady->slideId == target.slideId;
							}), name, failures);
						Bridge::PresentationTarget accepted = target;
						accepted.targetRevision = targetRevision;
						return ready && Check(PublishProductPresentationUiReady(
							Bridge::ReadyIdentityFor(accepted)), "explicit hidden page UI commit", failures);
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

				// 真实绘制线程：一直按住旧页，仍须完成新页 Present，但等 UI ack 后才接收新 Down。
				Bridge::PresentationTarget boundary = targetA;
				boundary.key.bytes[0] = 0xC3;
				boundary.sourceIdentity = "path:c:\\hidden\\boundary.pptx";
				boundary.slideIds = { 301, 302, 303 };
				boundary.slideId = 301;
				boundary.totalPages = 3;
				boundary.sessionRevision = 21;
				const auto boundaryFirst = PublishProductPresentationTarget(boundary);
				if (boundaryFirst)
				{
					modeSucceeded &= waitForPresentation(boundary, *boundaryFirst, false, "boundary page begins empty");
					boundary.targetRevision = *boundaryFirst;
					const auto oldIdentity = Bridge::ReadyIdentityFor(boundary);
					const auto beforeHeld = ProductHost().RuntimeSnapshot();
					postContact(HiddenTestContactPhase::Down, 48, 52);
					postContact(HiddenTestContactPhase::Move, 112, 72);
					modeSucceeded &= Check(WaitUntil([beforeHeld]
					{
						const auto p = ProductHost().RuntimeSnapshot().pen;
						return p.active && p.strokeId > beforeHeld.pen.strokeId && p.inputSequence > 2;
					}), "old page has an accepted still-down stroke", failures);
					boundary.pageIndex = 1; boundary.slideId = 302;
					const auto pageTwo = PublishProductPresentationTarget(boundary);
					if (pageTwo)
					{
						boundary.targetRevision = *pageTwo;
						const auto secondIdentity = Bridge::ReadyIdentityFor(boundary);
						modeSucceeded &= Check(WaitUntil([secondIdentity]
						{
							const auto state = ProductHost().RuntimeSnapshot();
							return state.presentationReady == secondIdentity && !state.currentPageHasContent &&
								!state.presentationInputReady && !state.pen.active;
						}), "held contact seals without Up and canvas ready waits for UI", failures);
						modeSucceeded &= Check(!PublishProductPresentationUiReady(oldIdentity), "late old-page UI ack is rejected", failures);
						modeSucceeded &= Check(PublishProductPresentationUiReady(secondIdentity) &&
							ProductHost().RuntimeSnapshot().presentationInputReady, "matching UI ack opens new input", failures);
						postContact(HiddenTestContactPhase::Move, 240, 190);
						postContact(HiddenTestContactPhase::Up, 260, 200);
						modeSucceeded &= Check(WaitUntil([beforeHeld]
						{
							const auto state = ProductHost().RuntimeSnapshot();
							return state.inputRecycled > beforeHeld.inputRecycled && !state.currentPageHasContent;
						}), "old Move and Up cannot write on the new page", failures);
						postContact(HiddenTestContactPhase::Down, 52, 92);
						postContact(HiddenTestContactPhase::Move, 100, 112);
						postContact(HiddenTestContactPhase::Up, 160, 132);
						modeSucceeded &= Check(WaitUntil([] { return ProductHost().RuntimeSnapshot().currentPageHasContent; }),
							"fresh Down after UI ack writes new page", failures);
						boundary.pageIndex = 2; boundary.slideId = 303;
						PublishProductPresentationTarget(boundary);
						boundary.pageIndex = 0; boundary.slideId = 301;
						const auto rapid = PublishProductPresentationTarget(boundary);
						modeSucceeded &= Check(!PublishProductPresentationUiReady(secondIdentity), "rapid-target late ack cannot reopen an old page", failures);
						if (rapid)
						{
							modeSucceeded &= waitForPresentation(boundary, *rapid, true, "rapid return restores sealed old-page stroke");
							boundary.targetRevision = *rapid;
							const auto current = Bridge::ReadyIdentityFor(boundary);
							modeSucceeded &= Check(SetProductPresentationInputSuspended(current, true) &&
								!PublishProductPresentationUiReady(current) && !ProductHost().RuntimeSnapshot().presentationInputReady,
								"end or unknown state suspends input without dropping the accepted page", failures);
							modeSucceeded &= Check(!SetProductPresentationInputSuspended(secondIdentity, false) &&
								SetProductPresentationInputSuspended(current, false) && !ProductHost().RuntimeSnapshot().presentationInputReady &&
								PublishProductPresentationUiReady(current) && ProductHost().RuntimeSnapshot().presentationInputReady,
								"current identity and a fresh UI commit resume suspended input", failures);
						}
					}
					const auto parkedB = PublishProductPresentationTarget(targetB);
					if (parkedB) modeSucceeded &= waitForPresentation(targetB, *parkedB, false, "park dirty boundary document at B");
					boundary.slideIds = { 303, 301, 302 };
					boundary.pageIndex = 1; boundary.slideId = 301;
					++boundary.sessionRevision;
					const auto reordered = PublishProductPresentationTarget(boundary);
					if (reordered) modeSucceeded &= waitForPresentation(boundary, *reordered, true,
						"warm slot reorders with its old SlideIDs before replacing target");
					boundary.pageIndex = 0; boundary.slideId = 303;
					const auto blankReordered = PublishProductPresentationTarget(boundary);
					if (blankReordered) modeSucceeded &= waitForPresentation(boundary, *blankReordered, false,
						"warm topology maps the empty SlideID instead of old ordinal ink");
				}

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

				// 最近笔类仅由正常结束的实际笔划更新；命令、翻页和取消均不得覆盖。
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().completedStrokeKind ==
					Bridge::CompletedStrokeKind::Drawing,
					"Clear Undo Redo preserve the last completed drawing kind", failures);
				auto kindState = whiteboardState;
				kindState.tool = Bridge::Tool::SolidLine;
				PublishProductState(kindState);
				std::this_thread::sleep_for(40ms);
				const auto beforeShape = ProductHost().RuntimeSnapshot();
				postContact(HiddenTestContactPhase::Down, 80, 72);
				postContact(HiddenTestContactPhase::Move, 136, 104);
				postContact(HiddenTestContactPhase::Up, 192, 136);
				modeSucceeded &= Check(WaitUntil([beforeShape]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.inputRecycled > beforeShape.inputRecycled &&
						state.completedStrokeKind == Bridge::CompletedStrokeKind::Shape;
				}), "completed shape updates the last stroke kind", failures);
				kindState.tool = Bridge::Tool::Laser;
				PublishProductState(kindState);
				std::this_thread::sleep_for(40ms);
				const auto beforeLaser = ProductHost().RuntimeSnapshot();
				postContact(HiddenTestContactPhase::Down, 88, 80);
				postContact(HiddenTestContactPhase::Move, 144, 112);
				postContact(HiddenTestContactPhase::Up, 200, 144);
				modeSucceeded &= Check(WaitUntil([beforeLaser]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.inputRecycled > beforeLaser.inputRecycled &&
						state.completedStrokeKind == Bridge::CompletedStrokeKind::Drawing;
				}), "completed laser maps to the drawing kind", failures);
				kindState.tool = Bridge::Tool::SolidLine;
				PublishProductState(kindState);
				std::this_thread::sleep_for(40ms);
				const auto beforeCancelledShape = ProductHost().RuntimeSnapshot();
				postContact(HiddenTestContactPhase::Down, 96, 88);
				postContact(HiddenTestContactPhase::Move, 152, 120);
				postContact(HiddenTestContactPhase::Cancelled, 208, 152);
				modeSucceeded &= Check(WaitUntil([beforeCancelledShape]
				{
					const auto state = ProductHost().RuntimeSnapshot();
					return state.inputRecycled > beforeCancelledShape.inputRecycled;
				}), "cancelled shape retires on the drawing thread", failures);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().completedStrokeKind ==
					Bridge::CompletedStrokeKind::Drawing,
					"cancelled stroke does not update the last completed kind", failures);
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
				// 完全停止所有Move，包括光标消息；只让真实绘制线程的时钟运行。
				modeSucceeded &= Check(WaitUntil([]
				{
					const auto s=ProductHost().RuntimeSnapshot();
					return s.eraser.active &&
						s.eraser.idleSeconds>=1.0 && s.eraser.cursorDiameterPx>0 &&
						s.eraser.cursorDiameterPx<=s.eraser.dpiX/96*18 &&
						s.eraser.nextRadiusPx<=s.eraser.dpiX/96*9.0f;
				},4s),"no Move: final contact cursor and next geometry visibly shrink",failures);
				const auto quiet=ProductHost().RuntimeSnapshot();
				// 已观测到实际 idle 后再锁存无输入基线；PostMessage 只保证进入 owner 队列。
				const auto moveCount=quiet.inputMovePublished;
				const auto pointCount=quiet.eraser.realPointCount;
				modeSucceeded &= Check(quiet.eraser.historyRadiusPx>quiet.eraser.nextRadiusPx*1.5f,
					"idle preserves historical width",failures);
				modeSucceeded &= Check(WaitUntil([]
				{
					return ProductHost().RuntimeSnapshot().eraser.effectiveDiameterDip<=16.001f;
				},3s),"effective size settles at exact minimum",failures);
				const auto stopped=ProductHost().RuntimeSnapshot().eraser.frameSequence;
				std::this_thread::sleep_for(250ms);
				const auto settled=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(settled.inputMovePublished==moveCount &&
					settled.eraser.realPointCount==pointCount,
					"idle does not submit fake points after settling",failures);
				modeSucceeded &= Check(std::abs(settled.eraser.historyRadiusPx-quiet.eraser.historyRadiusPx)<0.001f,
					"idle does not rewrite historical width after settling",failures);
				modeSucceeded &= Check(settled.eraser.frameSequence<=stopped+2,
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
					if(expected==SpeedEraser::ResponseModel::DirectTouch ||
						expected==SpeedEraser::ResponseModel::ScreenPenHybrid)
						modeSucceeded &= Check(WaitUntil([expected]{const auto d=ProductHost().RuntimeSnapshot().eraser;
							return d.active && std::abs(d.evidenceStartSeconds-0.080)<0.001 &&
								std::abs(d.evidenceFullSeconds-(expected==SpeedEraser::ResponseModel::DirectTouch?0.180:0.200))<0.001 &&
								std::abs(d.growthTauSeconds-(expected==SpeedEraser::ResponseModel::DirectTouch?0.220:0.200))<0.001 &&
								std::abs(d.evidenceCapDiameterDip-d.sizes.standardDiameterDip)<0.01f;}),
							"real Touch/ScreenPen Down reports new growth evidence parameters without precharged size",failures);
					modeSucceeded &= Check(WaitUntil([flags]{const auto d=ProductHost().RuntimeSnapshot().eraser;
						return d.inputContact && d.inputPositionValid && d.contactId==d.inputSource.cursorId &&
							d.contactGeneration!=0 && std::abs(d.inputCanvasXpx-80)<0.01f &&
							std::abs(d.inputCanvasYpx-100)<0.01f &&
							(flags!=kHiddenTestTouchFlag || (d.cursorVisible &&
								std::abs(d.cursorCanvasXpx-80)<0.01f && std::abs(d.cursorCanvasYpx-100)<0.01f));}),
						"diagnostic input identity and canvas cursor belong to the same contact",failures);
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
				constexpr int classroomMoveCount=40;
				constexpr int classroomLastX=60+classroomMoveCount*2;
				for(int i=1;i<=classroomMoveCount;++i)
				{
					postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,60+i*2,120);
					std::this_thread::sleep_for(40ms);
				}
				modeSucceeded &= Check(WaitUntil([beforeClassroomMoves,classroomMoveCount]{const auto s=ProductHost().RuntimeSnapshot();
					const auto& d=s.eraser;
					return s.inputMovePublished>=beforeClassroomMoves+classroomMoveCount && d.active &&
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
				postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,classroomLastX+2,120);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.requestedDeviceMode==SpeedEraser::DeviceMode::LargeScreen &&
						std::abs(d.largeTargetSpeed-1300)<0.01f;}),
					"active Touch keeps its latched scene after product setting changes",failures);
				const auto beforeClassroomUp=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postSource(HiddenTestContactPhase::Up,kHiddenTestTouchFlag,classroomLastX+2,120),
					"post classroom Touch Up",failures);
				modeSucceeded &= Check(WaitUntil([beforeClassroomUp]{const auto s=ProductHost().RuntimeSnapshot();
					return !s.eraser.active && s.inputTerminalPublished>beforeClassroomUp.inputTerminalPublished &&
						s.inputRecycled>beforeClassroomUp.inputRecycled;}),
					"manual classroom contact closes and retires before scene change",failures);
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,120);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.requestedDeviceMode==SpeedEraser::DeviceMode::Laptop &&
						std::abs(d.largeTargetSpeed-512.5f)<0.01f;}),
					"next Touch contact receives explicit Laptop cap with the same manual scale",failures);
				const auto beforeSceneCancel=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postSource(HiddenTestContactPhase::Cancelled,kHiddenTestTouchFlag,60,120),
					"post manual scene Touch Cancel",failures);
				modeSucceeded &= Check(WaitUntil([beforeSceneCancel]{const auto s=ProductHost().RuntimeSnapshot();
					return !s.eraser.active && s.inputTerminalPublished>beforeSceneCancel.inputTerminalPublished &&
						s.inputRecycled>beforeSceneCancel.inputRecycled;}),
					"manual scene probe cancels and retires old route before the next Touch",failures);
				ProductHost().SetEraserDevelopmentOptions({});
				// 面积辅助通过真实 mailbox、控制器、光标、模型和保存链路验收。
				const auto areaProbe=[&](const char* label)
				{
					const auto s=ProductHost().RuntimeSnapshot();const auto& d=s.eraser;
					std::fprintf(stderr,"[AreaProbe] %s mode=%u dip=%g cursor=%g floor=%g ref=%g firstRef=%g refDip=%gx%g recoveries=%u recoveryMs=%g recovering=%d releasing=%d aboveB=%d active=%d age=%g reason=%d points=%llu history=%g anchor=%d radius=%g frame=%llu\n",
						label,static_cast<unsigned>(requiredMode),d.effectiveDiameterDip,d.cursorDiameterPx,d.contactArea.activeFloorDip,
						d.contactArea.referenceFloorDip,d.contactArea.firstReferenceFloorDip,
						d.contactArea.referenceWidthDip,d.contactArea.referenceHeightDip,d.contactArea.recoveryCount,
						d.contactArea.recoveryMotionSeconds*1000,d.contactArea.recovering,d.contactArea.releasing,
						d.contactArea.areaFloorAboveStandard,d.contactArea.active,d.idleSeconds,static_cast<int>(d.contactArea.reason),
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
					return d.preview && d.inputType==2 && d.effectiveDiameterDip<=16.01f &&
						d.inputPositionValid && std::abs(d.inputCanvasXpx-60)<0.01f &&
						d.cursorVisible && std::abs(d.cursorCanvasXpx-60)<0.01f;}),
					"prepare frozen mouse fine hover with matching diagnostic position",failures);
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
				modeSucceeded &= Check(WaitUntil([modeDownBaseline,modeRecycledBaseline]{
					const auto s=ProductHost().RuntimeSnapshot();
					return s.inputDownPublished>=modeDownBaseline &&
						s.inputRecycled>=modeRecycledBaseline &&
						s.inputDownPublished-modeDownBaseline==s.inputRecycled-modeRecycledBaseline;}),
					"all current-run contact routes retire before area Touch Down",failures);
				ProductHost().SetHiddenTestContactArea(convertedArea);
				const auto beforeAreaDown=ProductHost().RuntimeSnapshot();
				modeSucceeded &= Check(postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,140),
					"post area Touch Down",failures);
				modeSucceeded &= Check(WaitUntil([beforeAreaDown]{const auto s=ProductHost().RuntimeSnapshot();
					const auto& d=s.eraser;
					return s.inputDownPublished>beforeAreaDown.inputDownPublished && d.active &&
						d.inputType==0 && d.contactArea.enabled && d.contactGeneration>0 &&
						d.inputPositionValid && std::abs(d.inputCanvasYpx-140)<0.01f;}),
					"real area Touch Down reaches this contact generation",failures);
				std::this_thread::sleep_for(120ms);
				modeSucceeded &= Check(ProductHost().RuntimeSnapshot().eraser.effectiveDiameterDip<=16.01f,
					"Touch Down with a large reported finger still starts small",failures);
				int areaX=60;
				const auto areaMovesBefore=ProductHost().RuntimeSnapshot().inputMovePublished;
				bool areaMovesPosted=true;
				for(int i=1;i<=35;++i)
				{
					areaX=60+i;areaMovesPosted &= postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,areaX,140);
					std::this_thread::sleep_for(30ms);
				}
				modeSucceeded &= Check(areaMovesPosted,"post all slow area Touch Moves",failures);
				const auto areaIngress=ProductHost().RuntimeSnapshot();
				std::fprintf(stderr,"[AreaIngress] mode=%u movesPublishedSoFar=%llu generation=%llu active=%d areaEnabled=%d sampleValid=%d referenceReady=%d areaActive=%d diameter=%.3f target=%.3f floor=%.3f idleMs=%.1f points=%llu\n",
					static_cast<unsigned>(requiredMode),
					static_cast<unsigned long long>(areaIngress.inputMovePublished-areaMovesBefore),
					static_cast<unsigned long long>(areaIngress.eraser.contactGeneration),
					areaIngress.eraser.active,areaIngress.eraser.contactArea.enabled,
					areaIngress.eraser.contactArea.sampleValid,
					areaIngress.eraser.contactArea.referenceReady,
					areaIngress.eraser.contactArea.active,
					areaIngress.eraser.effectiveDiameterDip,areaIngress.eraser.targetDiameterDip,
					areaIngress.eraser.contactArea.activeFloorDip,areaIngress.eraser.idleSeconds*1000,
					static_cast<unsigned long long>(areaIngress.eraser.realPointCount));
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
				// 等真实模型结果排空后再断言休眠；首次达到floor时仍可能有待消费的旧Move。
				modeSucceeded &= Check(WaitUntil([]{
					const auto before=ProductHost().RuntimeSnapshot().eraser;
					if(!before.active || before.needsAnimation || !before.contactArea.active)return false;
					std::this_thread::sleep_for(100ms);
					const auto after=ProductHost().RuntimeSnapshot().eraser;
					return after.active && !after.needsAnimation && after.contactArea.active &&
						after.realPointCount==before.realPointCount &&
						std::abs(after.effectiveDiameterDip-after.contactArea.activeFloorDip)<0.001f;
				},3s),"area floor waits for the previously queued model output",failures);
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
				// 旧参考失配先按原规则回落；只有后续真实拖动可恢复，光标与新几何仍共用同一尺寸。
				ProductHost().SetHiddenTestContactArea({200,400,20,40,SpeedEraser::ContactAreaUnits::CanvasPixels});
				const auto recoveryDownBefore=ProductHost().RuntimeSnapshot().inputDownPublished;
				postSource(HiddenTestContactPhase::Down,kHiddenTestTouchFlag,60,170);
				modeSucceeded &= Check(WaitUntil([recoveryDownBefore]{const auto s=ProductHost().RuntimeSnapshot();
					return s.inputDownPublished>recoveryDownBefore && s.eraser.active && s.eraser.contactArea.enabled;}),
					"recovery probe starts a fresh Touch area contact",failures);
				int recoveryX=60;
				for(int i=1;i<=35;++i)
				{
					recoveryX=60+i;postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,recoveryX,170);
					std::this_thread::sleep_for(30ms);
				}
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.contactArea.referenceReady &&
						std::abs(d.contactArea.firstReferenceFloorDip-50)<0.01f && d.effectiveDiameterDip>45;}),
					"recovery probe first establishes its bounded narrow reference",failures);
				ProductHost().SetHiddenTestContactArea({520,500,52,50,SpeedEraser::ContactAreaUnits::CanvasPixels});
				for(int i=0;i<30;++i)
				{
					postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,recoveryX,170);
					std::this_thread::sleep_for(30ms);
				}
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.contactArea.referenceReady && d.contactArea.recoveryCount==0 &&
						!d.contactArea.areaFloorAboveStandard && d.effectiveDiameterDip<=32.5f;},3s),
					"stationary relative outlier releases the old floor without recovering",failures);
				areaProbe("recovery-released");
				const auto releasedArea=ProductHost().RuntimeSnapshot().eraser;
				for(int i=1;i<=35;++i)
				{
					recoveryX=95+i;postSource(HiddenTestContactPhase::Move,kHiddenTestTouchFlag,recoveryX,170);
					std::this_thread::sleep_for(30ms);
				}
				modeSucceeded &= Check(WaitUntil([releasedArea]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.active && d.contactArea.recoveryCount==1 && d.contactArea.referenceWidthDip>50 &&
						d.contactArea.referenceFloorDip<=d.contactArea.firstReferenceFloorDip+0.01f &&
						d.effectiveDiameterDip>40 && d.realPointCount>releasedArea.realPointCount &&
						std::abs(d.cursorDiameterPx-d.nextRadiusPx*2)<0.01f &&
						std::abs(d.historyRadiusPx-d.nextRadiusPx)<0.01f;},3s),
					"real drag restores capped area reference with matching cursor and new geometry",failures);
				areaProbe("recovery-restored");
				postSource(HiddenTestContactPhase::Up,kHiddenTestTouchFlag,recoveryX,170);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.active;}),
					"recovered Touch contact closes without leaking area state",failures);
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
					const auto& d=s.eraser;
					return s.inputDownPublished>fixedBefore && std::abs(d.cursorDiameterPx-32)<0.01f &&
						d.eraserKind==SpeedEraser::EraserKind::Fixed && d.inputPositionValid &&
						std::abs(d.inputCanvasXpx-60)<0.01f && std::abs(d.inputCanvasYpx-170)<0.01f &&
						std::abs(d.targetDiameterDip-d.effectiveDiameterDip)<0.01f;}),
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
				auto inkState=speedState;inkState.tool=Bridge::Tool::HardPen;
				PublishProductState(inkState);std::this_thread::sleep_for(50ms);
				postSource(HiddenTestContactPhase::Down,kHiddenTestIntegratedPenFlag,110,100);
				modeSucceeded &= Check(WaitUntil([]{const auto d=ProductHost().RuntimeSnapshot().eraser;
					return d.inputContact && !d.eraserContact && d.inputType==1 && d.inputPositionValid &&
						std::abs(d.inputCanvasXpx-110)<0.01f && std::abs(d.inputCanvasYpx-100)<0.01f;}),
					"ordinary pen contact reports its input position without an eraser size",failures);
				postSource(HiddenTestContactPhase::Cancelled,kHiddenTestIntegratedPenFlag,110,100);
				modeSucceeded &= Check(WaitUntil([]{return !ProductHost().RuntimeSnapshot().eraser.inputContact;}),
					"ordinary pen diagnostic contact closes",failures);

				ProductHost().SetEraserDevelopmentOptions({});


			}

			if (exerciseCommands)
				modeSucceeded &= CheckPresentationPersistence(service, drawpad, presentation, callbacks, options, failures);

			const auto stopStarted = std::chrono::steady_clock::now();
			StopProduct();
			const auto stopElapsed = std::chrono::steady_clock::now() - stopStarted;
			modeSucceeded &= Check(stopElapsed < 10s && !ProductRunning() &&
				!ProductFirstFrameReady(), "bounded complete Draw3 stop", failures);
			modeSucceeded &= Check(IsWindow(drawpad) && IsWindow(presentation) &&
				!IsWindowVisible(drawpad) && !IsWindowVisible(presentation) &&
				GetWindow(drawpad, GW_OWNER) == presentation &&
				GetWindow(presentation, GW_OWNER) == freeze,
				"Host stop leaves hidden Window Service HWND intact", failures);
			return modeSucceeded;
		}
	}

	int RunRendererMapCompatibilityTest() noexcept
	{
		int failures = 0;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		D3D_FEATURE_LEVEL actualLevel = D3D_FEATURE_LEVEL_11_0;
		constexpr D3D_FEATURE_LEVEL requestedLevel[] = { D3D_FEATURE_LEVEL_11_0 };
		const HRESULT createResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
			nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, requestedLevel, ARRAYSIZE(requestedLevel),
			D3D11_SDK_VERSION, device.GetAddressOf(), &actualLevel, context.GetAddressOf());
		if (!Check(SUCCEEDED(createResult) && device && context &&
			actualLevel == D3D_FEATURE_LEVEL_11_0,
			"WARP FL11.0 device without HWND", failures)) return 1;

		InkRenderer renderer;
		renderer.device = device;
		renderer.context = context;
		renderer.viewportWidth = 64.0f;
		renderer.viewportHeight = 64.0f;
		D3D11_BUFFER_DESC constantDescription = {
			48, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (!Check(SUCCEEDED(device->CreateBuffer(&constantDescription, nullptr,
			renderer.globalCB.GetAddressOf())), "create production constant buffer", failures))
			return 1;
		D3D11_BUFFER_DESC inkDescription = {};
		inkDescription.ByteWidth = static_cast<UINT>(
			InkRenderer::kMaxBufferCapacity * sizeof(InkPoint));
		inkDescription.Usage = D3D11_USAGE_DYNAMIC;
		inkDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		inkDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		inkDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		inkDescription.StructureByteStride = sizeof(InkPoint);
		if (!Check(SUCCEEDED(device->CreateBuffer(&inkDescription, nullptr,
			renderer.inkDataBuffer.GetAddressOf())), "create production dynamic SRV buffer", failures))
			return 1;
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription = {};
		viewDescription.Format = DXGI_FORMAT_UNKNOWN;
		viewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		viewDescription.Buffer.NumElements = static_cast<UINT>(InkRenderer::kMaxBufferCapacity);
		if (!Check(SUCCEEDED(device->CreateShaderResourceView(renderer.inkDataBuffer.Get(),
			&viewDescription, renderer.inkDataSRV.GetAddressOf())),
			"create production dynamic SRV", failures)) return 1;
		D3D11_BUFFER_DESC stagingDescription = inkDescription;
		stagingDescription.Usage = D3D11_USAGE_STAGING;
		stagingDescription.BindFlags = 0;
		stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDescription.MiscFlags = 0;
		stagingDescription.StructureByteStride = 0;
		Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
		if (!Check(SUCCEEDED(device->CreateBuffer(&stagingDescription, nullptr,
			staging.GetAddressOf())), "create no-window readback buffer", failures)) return 1;

		const auto checkUploaded = [&](const void* expected, size_t bytes, const char* name)
		{
			context->CopyResource(staging.Get(), renderer.inkDataBuffer.Get());
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (!Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
				"read back dynamic SRV upload", failures)) return false;
			const bool matches = std::memcmp(mapped.pData, expected, bytes) == 0;
			context->Unmap(staging.Get(), 0);
			return Check(matches, name, failures);
		};
		const DirectX::XMFLOAT4 color(0.2f, 0.4f, 0.6f, 1.0f);
		const std::array<InkPoint, 2> firstStroke = { InkPoint{ 2, 3, 4, 0 },
			InkPoint{ 18, 20, 4, 1 } };
		const std::array<InkPoint, 2> secondStroke = { InkPoint{ 5, 7, 3, 2 },
			InkPoint{ 30, 31, 3, 3 } };
		const std::array<ShapePrimitive, 1> shape = { ShapePrimitive{
			{ 4, 6, 2, 0 }, { 28, 32, 0, 0 } } };
		// 强制模拟 Win7/驱动不支持扩展：每批必须 DISCARD 并从零偏移上传。
		renderer.mapNoOverwriteOnDynamicBufferSRV = false;
		Check(renderer.DrawStroke(firstStroke, color) == 0 && renderer.m_bufferHead == 2,
			"first unsupported-SRV stroke uses DISCARD", failures);
		Check(renderer.DrawStroke(secondStroke, color) == 0 && renderer.m_bufferHead == 2,
			"second unsupported-SRV stroke resets offset", failures);
		checkUploaded(secondStroke.data(), sizeof(secondStroke),
			"second stroke occupies first two InkPoint slots");
		Check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, color) == 0 &&
			renderer.m_bufferHead == 2, "unsupported-SRV shape resets offset", failures);
		checkUploaded(shape.data(), sizeof(shape), "shape occupies first two InkPoint slots");

		D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
		const bool warpOptionsAvailable = SUCCEEDED(device->CheckFeatureSupport(
			D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)));
		std::fprintf(stderr, "[Draw3RendererMap] WARP_FL11_0 options=%d dynamic_srv_no_overwrite=%d\n",
			warpOptionsAvailable, warpOptionsAvailable && options.MapNoOverwriteOnDynamicBufferSRV != FALSE);
		Microsoft::WRL::ComPtr<ID3D11Device> hardwareDevice;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> hardwareContext;
		D3D_FEATURE_LEVEL hardwareLevel = D3D_FEATURE_LEVEL_11_0;
		const HRESULT hardwareResult = D3D11CreateDevice(nullptr,
			D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			requestedLevel, ARRAYSIZE(requestedLevel), D3D11_SDK_VERSION,
			hardwareDevice.GetAddressOf(), &hardwareLevel, hardwareContext.GetAddressOf());
		D3D11_FEATURE_DATA_D3D11_OPTIONS hardwareOptions = {};
		const bool hardwareOptionsAvailable = SUCCEEDED(hardwareResult) && hardwareDevice &&
			SUCCEEDED(hardwareDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,
				&hardwareOptions, sizeof(hardwareOptions)));
		std::fprintf(stderr,
			"[Draw3RendererMap] HARDWARE_FL11_0 create_hr=0x%08X options=%d dynamic_srv_no_overwrite=%d\n",
			static_cast<unsigned int>(hardwareResult), hardwareOptionsAvailable,
			hardwareOptionsAvailable && hardwareOptions.MapNoOverwriteOnDynamicBufferSRV != FALSE);
		if (warpOptionsAvailable && options.MapNoOverwriteOnDynamicBufferSRV)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (Check(SUCCEEDED(context->Map(renderer.inkDataBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped)),
				"begin supported-SRV ring allocation", failures))
			{
				context->Unmap(renderer.inkDataBuffer.Get(), 0);
				renderer.m_bufferHead = 0;
				renderer.mapNoOverwriteOnDynamicBufferSRV = true;
				Check(renderer.DrawStroke(firstStroke, color) == 0 &&
					renderer.DrawStroke(secondStroke, color) == 0 &&
					renderer.m_bufferHead == 4,
					"supported-SRV device retains NO_OVERWRITE ring", failures);
			}
		}
		context->ClearState();
		renderer.ReleaseResources();
		if (failures == 0) Report("PASS", "no-window WARP dynamic-SRV map compatibility");
		return failures == 0 ? 0 : 1;
	}

	int RunRendererFailureCommitTest() noexcept
	{
		int failures = 0;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		D3D_FEATURE_LEVEL actualLevel = D3D_FEATURE_LEVEL_11_0;
		constexpr D3D_FEATURE_LEVEL requestedLevel[] = { D3D_FEATURE_LEVEL_11_0 };
		const HRESULT createResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
			nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, requestedLevel, ARRAYSIZE(requestedLevel),
			D3D11_SDK_VERSION, device.GetAddressOf(), &actualLevel, context.GetAddressOf());
		if (!Check(SUCCEEDED(createResult) && device && context &&
			actualLevel == D3D_FEATURE_LEVEL_11_0,
			"failure test creates no-window WARP FL11.0 device", failures)) return 1;

		InkRenderer renderer;
		renderer.device = device;
		renderer.context = context;
		renderer.viewportWidth = 64.0f;
		renderer.viewportHeight = 64.0f;
		renderer.mapNoOverwriteOnDynamicBufferSRV = false;
		const D3D11_BUFFER_DESC constantDescription = {
			48, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (!Check(SUCCEEDED(device->CreateBuffer(&constantDescription, nullptr,
			renderer.globalCB.GetAddressOf())), "failure test creates constant buffer", failures))
			return 1;

		const auto createBufferPair = [&](UINT byteWidth,
			Microsoft::WRL::ComPtr<ID3D11Buffer>& writable,
			Microsoft::WRL::ComPtr<ID3D11Buffer>& unwritable) -> bool
		{
			D3D11_BUFFER_DESC description = {};
			description.ByteWidth = byteWidth;
			description.Usage = D3D11_USAGE_DYNAMIC;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			description.StructureByteStride = byteWidth /
				static_cast<UINT>(InkRenderer::kMaxBufferCapacity);
			if (FAILED(device->CreateBuffer(&description, nullptr,
				writable.GetAddressOf()))) return false;
			// 合法的 DEFAULT 缓冲区没有 CPU 写权限；真实 Map 应返回失败 HRESULT。
			description.Usage = D3D11_USAGE_DEFAULT;
			description.CPUAccessFlags = 0;
			return SUCCEEDED(device->CreateBuffer(&description, nullptr,
				unwritable.GetAddressOf()));
		};
		Microsoft::WRL::ComPtr<ID3D11Buffer> writableInk;
		Microsoft::WRL::ComPtr<ID3D11Buffer> unwritableInk;
		Microsoft::WRL::ComPtr<ID3D11Buffer> writableHighlighter;
		Microsoft::WRL::ComPtr<ID3D11Buffer> unwritableHighlighter;
		if (!Check(createBufferPair(static_cast<UINT>(
			InkRenderer::kMaxBufferCapacity * sizeof(InkPoint)),
			writableInk, unwritableInk) &&
			createBufferPair(static_cast<UINT>(
			InkRenderer::kMaxBufferCapacity * sizeof(HighlighterPrimitive)),
			writableHighlighter, unwritableHighlighter),
			"failure test creates valid writable/unwritable structured buffers", failures))
			return 1;

		const DirectX::XMFLOAT4 color(0.2f, 0.4f, 0.6f, 1.0f);
		const std::vector<InkPoint> points = {
			{ 4.0f, 4.0f, 3.0f, 0.0f },
			{ 12.0f, 12.0f, 3.0f, 1.0f },
			{ 20.0f, 20.0f, 3.0f, 2.0f }
		};
		ActiveStroke pen(6.0f, 500.0f);
		pen.realPoints = points;
		renderer.inkDataBuffer = unwritableInk;
		const LiveRasterSubmission failedPen = CommitStablePrefixToL1(pen, 0.0, 0.0, color,
			StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(!failedPen.succeeded && IsRectEmpty(&failedPen.dirty) &&
			pen.committedIndex == 0 &&
			!pen.hasCommittedGeometry,
			"failed real pen Map leaves stable L1 cursor unchanged", failures);
		renderer.inkDataBuffer = writableInk;
		const LiveRasterSubmission retriedPen = CommitStablePrefixToL1(pen, 0.0, 0.0, color,
			StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(retriedPen.succeeded && !IsRectEmpty(&retriedPen.dirty) &&
			pen.committedIndex == 1 &&
			pen.hasCommittedGeometry,
			"pen stable L1 retries once after writable buffer restored", failures);

		ActiveStroke highlighter(6.0f, 500.0f,
			StrokeWidthMode::SimulatedPressure, true);
		highlighter.realPoints = points;
		renderer.highlighterPrimitiveBuffer = unwritableHighlighter;
		const LiveRasterSubmission failedHighlighter = CommitStablePrefixToL1(highlighter,
			0.0, 0.0, color, StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(!failedHighlighter.succeeded && IsRectEmpty(&failedHighlighter.dirty) &&
			highlighter.committedIndex == 0 &&
			!highlighter.hasCommittedGeometry &&
			highlighter.committedHighlighterGeometry.primitives.empty(),
			"failed real highlighter Map does not cache unsubmitted geometry", failures);
		renderer.highlighterPrimitiveBuffer = writableHighlighter;
		const LiveRasterSubmission retriedHighlighter = CommitStablePrefixToL1(highlighter,
			0.0, 0.0, color, StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(retriedHighlighter.succeeded &&
			!IsRectEmpty(&retriedHighlighter.dirty) && highlighter.committedIndex == 1 &&
			highlighter.hasCommittedGeometry &&
			highlighter.committedHighlighterGeometry.primitives.size() == 1,
			"highlighter L1 retry caches stable prefix exactly once", failures);

		ActiveStroke eraser(6.0f, 500.0f);
		eraser.realPoints = { points[0], points[1] };
		renderer.inkDataBuffer = unwritableInk;
		const LiveRasterSubmission failedEraser = CommitEraserRealPointsToL1(eraser,
			StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(!failedEraser.succeeded && IsRectEmpty(&failedEraser.dirty) &&
			eraser.committedIndex == 0 &&
			!eraser.hasCommittedGeometry,
			"failed real eraser Map leaves L1 cursor unchanged", failures);
		renderer.inkDataBuffer = writableInk;
		const LiveRasterSubmission retriedEraser = CommitEraserRealPointsToL1(eraser,
			StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(retriedEraser.succeeded && !IsRectEmpty(&retriedEraser.dirty) &&
			eraser.committedIndex == 1 &&
			eraser.hasCommittedGeometry,
			"eraser L1 retry commits accepted real points once", failures);

		ActiveStroke eraserDot(6.0f, 500.0f);
		eraserDot.hasInputStartPoint = true;
		eraserDot.inputStartPoint = points[0];
		renderer.inkDataBuffer = unwritableInk;
		const LiveRasterSubmission failedDot = CommitEraserRealPointsToL1(eraserDot,
			StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(!failedDot.succeeded && IsRectEmpty(&failedDot.dirty) &&
			!eraserDot.hasCommittedGeometry,
			"failed eraser single-point Map preserves retry eligibility", failures);
		renderer.inkDataBuffer = writableInk;
		const LiveRasterSubmission retriedDot = CommitEraserRealPointsToL1(eraserDot,
			StrokeShape::RoundCapsule, renderer, 64, 64);
		Check(retriedDot.succeeded && !IsRectEmpty(&retriedDot.dirty) &&
			eraserDot.hasCommittedGeometry,
			"single-point eraser retry commits after buffer restored", failures);

		pen.l0DrawPoints = points;
		renderer.inkDataBuffer = unwritableInk;
		Check(!DrawL0LiveComposite(pen, color, StrokeShape::RoundCapsule,
			renderer, false), "L0 pen reports real Map failure", failures);
		renderer.inkDataBuffer = writableInk;
		Check(DrawL0LiveComposite(pen, color, StrokeShape::RoundCapsule,
			renderer, false), "L0 pen reports retry submission", failures);
		renderer.highlighterPrimitiveBuffer = unwritableHighlighter;
		Check(!DrawL0LiveComposite(highlighter, color, StrokeShape::RoundCapsule,
			renderer, false), "L0 highlighter reports real Map failure", failures);
		renderer.highlighterPrimitiveBuffer = writableHighlighter;
		Check(DrawL0LiveComposite(highlighter, color, StrokeShape::RoundCapsule,
			renderer, false), "L0 highlighter reports retry submission", failures);
		const std::array<ShapePrimitive, 1> shape = { ShapePrimitive{
			{ 4, 6, 2, 0 }, { 28, 32, 0, 0 } } };
		renderer.inkDataBuffer = unwritableInk;
		Check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, color) < 0,
			"Shape batch reports real Map failure", failures);
		renderer.inkDataBuffer = writableInk;
		Check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, color) == 0,
			"Shape batch reports retry submission", failures);

		context->ClearState();
		renderer.ReleaseResources();
		if (failures == 0) Report("PASS", "no-window WARP raster submission failure retry");
		return failures == 0 ? 0 : 1;
	}

	int RunLaserRasterFailureTest() noexcept
	{
		int failures = 0;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		D3D_FEATURE_LEVEL actualLevel = D3D_FEATURE_LEVEL_11_0;
		constexpr D3D_FEATURE_LEVEL requestedLevel[] = { D3D_FEATURE_LEVEL_11_0 };
		const HRESULT createResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
			nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, requestedLevel, ARRAYSIZE(requestedLevel),
			D3D11_SDK_VERSION, device.GetAddressOf(), &actualLevel, context.GetAddressOf());
		if (!Check(SUCCEEDED(createResult) && device && context &&
			actualLevel == D3D_FEATURE_LEVEL_11_0,
			"Laser test creates no-window WARP FL11.0 device", failures)) return 1;

		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
		if (!Check(SUCCEEDED(device.As(&dxgiDevice)) && dxgiDevice &&
			SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) && adapter &&
			SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2),
				reinterpret_cast<void**>(factory.GetAddressOf()))) && factory,
			"Laser test finds WARP DXGI 1.2 factory", failures)) return 1;
		DXGI_SWAP_CHAIN_DESC1 description = {};
		description.Width = 64;
		description.Height = 64;
		description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		description.BufferCount = 2;
		description.Scaling = DXGI_SCALING_STRETCH;
		description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
		if (!Check(SUCCEEDED(factory->CreateSwapChainForComposition(device.Get(),
			&description, nullptr, swapChain.GetAddressOf())) && swapChain,
			"Laser test creates FLIP composition swapchain without HWND", failures)) return 1;

		InkRenderer renderer;
		if (!Check(renderer.Init(device.Get(), context.Get(), swapChain.Get(), 64, 64),
			"Laser test initializes production renderer and shaders", failures))
		{
			context->ClearState();
			renderer.ReleaseResources();
			return 1;
		}
		D3D11_BUFFER_DESC unwritableDescription = {};
		renderer.inkDataBuffer->GetDesc(&unwritableDescription);
		// 合法的 DEFAULT 缓冲区没有 CPU 写权限，第二层真实 Map 必须失败。
		unwritableDescription.Usage = D3D11_USAGE_DEFAULT;
		unwritableDescription.CPUAccessFlags = 0;
		Microsoft::WRL::ComPtr<ID3D11Buffer> unwritableInk;
		if (Check(SUCCEEDED(device->CreateBuffer(&unwritableDescription, nullptr,
			unwritableInk.GetAddressOf())),
			"Laser test creates non-CPU-writable structured buffer", failures))
		{
			failures += RunLaserRasterFailureProductionProbe(renderer,
				unwritableInk.Get());
		}
		context->ClearState();
		renderer.ReleaseResources();
		if (failures == 0) Report("PASS", "no-window WARP Laser bake transaction");
		return failures == 0 ? 0 : 1;
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
					L"Inkeys.Draw3.Hidden.Drawpad." + suffix, HiddenDrawpadWindowProc, 320, 240);
				if (dcompCompatible) drawpadSpec.exStyle |= WS_EX_NOREDIRECTIONBITMAP;
				specs.push_back(std::move(drawpadSpec));
				return specs;
			};
			StyleContext styleContext{ &service };

			// 目标系统缺少 DComp API 时仍运行 legacy HWND/ULW 合同。
			const bool dcompAvailable = ShouldPreconfigureNoRedirectionBitmap();
			if (!Check(service.Start(makeSpecs(dcompAvailable)), "start initial hidden Window Service", failures))
				return 1;
			const HWND dcompMagnifierHost = service.Handle(Inkeys::Window::WindowRole::MagnifierHost);
			const HWND dcompFreeze = service.Handle(Inkeys::Window::WindowRole::Freeze);
			const HWND dcompPresentation = service.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation);
			const HWND dcompDrawpad = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			Check(dcompMagnifierHost && dcompFreeze && dcompPresentation && dcompDrawpad,
				"initial hidden HWND creation", failures);
			Check(!IsWindowVisible(dcompMagnifierHost) && !IsWindowVisible(dcompFreeze) &&
				!IsWindowVisible(dcompPresentation) && !IsWindowVisible(dcompDrawpad),
				"initial HWND creation never shows UI", failures);

			if(eraserOnly)
			{
				RunMode(service,styleContext,dcompMagnifierHost,dcompFreeze,dcompDrawpad,dcompPresentation,
					HostPresentationMode::Automatic,dcompAvailable,false,false,failures,true);
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
				HostPresentationMode::Automatic, dcompAvailable, true, false, failures);
			if (dcompAvailable)
				RunMode(service, styleContext, dcompMagnifierHost, dcompFreeze, dcompDrawpad,
					dcompPresentation,
					HostPresentationMode::DirectCompositionVisualTree, true, false, false, failures);
			if (dcompAvailable)
			{
				// Windows 可能把创建期 NOREDIRECTIONBITMAP 固化；回调结果必须与真实样式一致。
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
			}
			StopProduct();
			service.StopAndJoin();
			Check(!IsWindow(dcompMagnifierHost) && !IsWindow(dcompFreeze) &&
				!IsWindow(dcompPresentation) && !IsWindow(dcompDrawpad),
				"Window Service destroys initial hidden HWNDs", failures);

			// ULW 需要可切换的初始重定向表面；停止上一宿主后重建唯一的测试 Drawpad HWND。
			if (!Check(service.Start(makeSpecs(false)), "start legacy-compatible hidden Window Service", failures))
				return 1;
			const HWND legacyMagnifierHost = service.Handle(Inkeys::Window::WindowRole::MagnifierHost);
			const HWND legacyFreeze = service.Handle(Inkeys::Window::WindowRole::Freeze);
			const HWND legacyPresentation = service.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation);
			const HWND legacyDrawpad = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			Check(legacyMagnifierHost && legacyFreeze && legacyPresentation && legacyDrawpad,
				"hidden legacy HWND creation", failures);
			Check(!IsWindowVisible(legacyMagnifierHost) && !IsWindowVisible(legacyFreeze) &&
				!IsWindowVisible(legacyPresentation) && !IsWindowVisible(legacyDrawpad),
				"legacy HWND creation never shows UI", failures);
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				legacyPresentation,
				HostPresentationMode::Automatic, false, false, false, failures);
			const HostStyleCallbacks legacyCallbacks{ &styleContext, &ApplyDrawpadStyle };
			const HostPresentationMode disabledModes[] = {
				HostPresentationMode::DwmBlurBehind2,
				HostPresentationMode::DwmBlurBehind
			};
			for (const HostPresentationMode disabledMode : disabledModes)
			{
				// 强制选择必须在附着 HWND 前拒绝，失败后仍能继续启动 ULW。
				const auto styleCallsBefore = styleContext.callCount.load(std::memory_order_acquire);
				const auto windowStyleBefore = GetWindowLongPtrW(legacyDrawpad, GWL_EXSTYLE);
				const HANDLE tabletPropertyBefore = GetProp(
					legacyDrawpad, MICROSOFT_TABLETPENSERVICE_PROPERTY);
				HostStartOptions disabledOptions{ disabledMode };
				disabledOptions.allowDirectComposition = false;
				Check(!StartProduct(legacyDrawpad, legacyPresentation, legacyCallbacks,
					disabledOptions), "forced DWM presenter is disabled", failures);
				Check(!ProductRunning() && !ProductFirstFrameReady(),
					"rejected DWM startup fully stops Draw3 host", failures);
				Check(styleContext.callCount.load(std::memory_order_acquire) == styleCallsBefore,
					"rejected DWM startup does not configure HWND", failures);
				Check(GetWindowLongPtrW(legacyDrawpad, GWL_EXSTYLE) == windowStyleBefore &&
					GetProp(legacyDrawpad, MICROSOFT_TABLETPENSERVICE_PROPERTY) == tabletPropertyBefore,
					"rejected DWM startup preserves HWND style and tablet property", failures);
				StopProduct();
			}
			RunMode(service, styleContext, legacyMagnifierHost, legacyFreeze, legacyDrawpad,
				legacyPresentation,
				HostPresentationMode::UlwDirtyRect, false, true, true, failures);
			StopProduct();
			service.StopAndJoin();
			Check(!IsWindow(legacyMagnifierHost) && !IsWindow(legacyFreeze) &&
				!IsWindow(legacyPresentation) && !IsWindow(legacyDrawpad),
				"Window Service destroys legacy-compatible hidden HWNDs", failures);
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
