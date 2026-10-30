module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>

#include "../../../IdtFreezeFrame.h"
#include "Whiteboard.Theme.h"
#include "Whiteboard.BackgroundState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"

module Inkeys.UI.Whiteboard;

import Inkeys.UI.Bar;
import Inkeys.UI.PageControl;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Window;

namespace Inkeys::UI::Whiteboard
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		using Client = Inkeys::UI::RenderPipeline::Client;
		using FrameContext = Inkeys::UI::RenderPipeline::FrameContext;
		using FrameResult = Inkeys::UI::RenderPipeline::FrameResult;
		using Scene = Inkeys::UI::Bar::BarPageSurface;
		using WindowRole = Inkeys::Window::WindowRole;

		std::atomic_bool initialized = false;
		std::atomic_bool expandedLayoutTarget = false;
		std::atomic_bool active = false;
		WhiteboardDetail::BackgroundState backgroundState;
		struct BackgroundSubmission
		{
			WhiteboardDetail::BackgroundState::Frame frame;
			std::uint64_t deviceGeneration = 0, nativeRevision = 0;
			bool animationActive = false;
		};
		// 像素成功与原生确认分开保留；owner 忙时只重试回执，不重复整屏 GetDC/ULW。
		std::optional<BackgroundSubmission> backgroundSubmission;
		std::atomic<std::uint64_t> backgroundNativeRevision = 0;
		std::uint64_t backgroundFrameSerial = 0;
		std::mutex pageStateMutex;
		PageStateTransaction pageTransaction;
		std::uint32_t themeRgb = 0x123B32;
		int themePattern = 0;
		bool unsavedPages = false;
		std::uint64_t pageGeneration = 0;
		std::mutex backgroundRenderMutex;
		Scene backgroundScene;
		RECT configuredBackgroundScreen{};
		float configuredBackgroundScale = 0.0F;
		Inkeys::Display::Subscription displaySubscription;

		struct BackgroundTarget { RECT bounds{}; float scale = 1.0F; };
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		// 仅屏外 fixture 注入目标；生产仍只读 Display 的主屏快照。
		BackgroundTarget hiddenBackgroundTarget;
		bool hiddenBackgroundTest = false, hiddenBackgroundHoldReceipt = false;
		std::uint64_t hiddenBackgroundPixelCount = 0;
#endif
		std::atomic<std::shared_ptr<const BackgroundTarget>> renderedBackgroundTarget;
		[[nodiscard]] BackgroundTarget PrimaryTarget() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (hiddenBackgroundTest) return hiddenBackgroundTarget;
#endif
			// 尺寸和 DPI 必须来自同一显示快照；白板始终覆盖完整 rcMonitor。
			const auto snapshot = Inkeys::Display::GetSnapshot();
			if (const auto* monitor = snapshot ? snapshot->Primary() : nullptr)
				return { monitor->bounds, static_cast<float>(monitor->effectiveDpiX
					? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI };
			return { {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, 1.0F };
		}

		void PublishPageControlSnapshot() noexcept
		{
			PageState page;
			Inkeys::UI::PageControl::WhiteboardState state;
			{
				std::scoped_lock lock(pageStateMutex);
				page = pageTransaction.Snapshot();
				state.backgroundRgb = themeRgb;
				state.pattern = themePattern;
				state.unsaved = unsavedPages;
				state.generation = pageGeneration;
			}
			state.expandedLayoutTarget = expandedLayoutTarget.load(
				std::memory_order_acquire);
			state.active = active.load(std::memory_order_acquire);
			state.currentPage = page.currentPage;
			state.totalPage = page.totalPage;
			state.previousEnabled = page.previousEnabled;
			state.previousInteractive = page.previousInteractive;
			state.pageEnabled = page.pageEnabled;
			state.pageInteractive = page.pageInteractive;
			state.nextEnabled = page.nextEnabled;
			state.nextInteractive = page.nextInteractive;
			state.nextIsAdd = page.nextIsAdd;
			state.switching = page.switching;
			Inkeys::UI::PageControl::PublishWhiteboardState(state);
		}

		void ConfigureBackgroundScene(const RECT& bounds, float scale)
		{
			const double widthDip = static_cast<double>(
				bounds.right - bounds.left) / scale;
			const double heightDip = static_cast<double>(
				bounds.bottom - bounds.top) / scale;
			Inkeys::UI::Bar::BarSurfaceBackgroundSpec background;
			background.bounds = { 0.0, 0.0, widthDip, heightDip };
			std::uint32_t rgb; int pattern;
			{ std::scoped_lock lock(pageStateMutex); rgb=themeRgb; pattern=themePattern; }
			background.drawContent = [rgb, pattern](auto* dc, auto rect) { WhiteboardVisual::DrawTheme(dc,rect,rgb,pattern); };
			background.fill = RGB(18, 59, 50);
			background.frame = RGB(18, 59, 50);
			background.useThemeColors = false;
			background.cornerRadiusDip = 0.0;
			background.frameThicknessDip = 0.0;
			background.fillOpacity = 1.0;
			background.frameOpacity = 0.0;
			(void)backgroundScene.ConfigureBackground(background);
			(void)backgroundScene.SetBounds(bounds, scale);
			configuredBackgroundScreen = bounds;
			configuredBackgroundScale = scale;
			renderedBackgroundTarget.store(std::make_shared<const BackgroundTarget>(BackgroundTarget{bounds, scale}), std::memory_order_release);
		}

		[[nodiscard]] bool IsDeviceLost(HRESULT hr) noexcept
		{
			return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET
				|| hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		bool CommitBackgroundInactive(const WhiteboardDetail::BackgroundState::Request& request)
		{
			if (!backgroundState.CommitInactive(request)) return false;
			Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
			return true;
		}

		void CancelBackgroundCandidate() noexcept
		{
			const auto revision = backgroundNativeRevision.exchange(0, std::memory_order_acq_rel);
			if (revision) Inkeys::Window::GetService().CancelCandidate(WindowRole::Freeze, revision);
		}

		void DiscardBackgroundSubmission() noexcept
		{
			if (!backgroundSubmission) return;
			const auto revision = backgroundSubmission->nativeRevision;
			if (revision)
			{
				Inkeys::Window::GetService().CancelCandidate(WindowRole::Freeze, revision);
				auto expected = revision;
				(void)backgroundNativeRevision.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
			}
			backgroundSubmission.reset();
		}

		bool PublishBackgroundCandidate(BackgroundSubmission& submission)
		{
			using Visibility = Inkeys::Window::WindowCandidateVisibility;
			Inkeys::Window::WindowCandidate candidate;
			candidate.frameSerial = submission.frame.frameSerial;
			candidate.workspaceRevision = submission.frame.request.revision;
			auto& target = candidate.targets[0];
			target.role = WindowRole::Freeze;
			target.hwnd = submission.frame.hwnd;
			target.generation = submission.frame.generation;
			target.bounds = submission.frame.bounds;
			// ULW 已原子提交像素与位置，owner 只核验，不能把迟到的旧 bounds 再搬回去。
			target.applyBounds = false;
			target.verifyBounds = submission.frame.request.active;
			target.presentationRevision = submission.frame.presentationRevision;
			target.requirePresentationMatch = submission.frame.request.active;
			target.visibility = !submission.frame.request.active ? Visibility::Hide
				: submission.frame.request.revision ? Visibility::Preserve : Visibility::Show;
			submission.nativeRevision = Inkeys::Window::GetService().PublishCandidate(candidate);
			if (!submission.nativeRevision) return false;
			backgroundNativeRevision.store(submission.nativeRevision, std::memory_order_release);
			if (!initialized.load(std::memory_order_acquire) || !backgroundState.FrameCurrent(submission.frame))
			{
				CancelBackgroundCandidate();
				return false;
			}
			return true;
		}

		FrameResult RenderBackground(const FrameContext& frameContext)
		{
			if (!initialized.load(std::memory_order_acquire)) return FrameResult::Idle;
			auto& service = Inkeys::Window::GetService();
			const auto request = backgroundState.Desired();
			const auto current = [request] { return initialized.load(std::memory_order_acquire)
				&& backgroundState.Current(request); };
			if (backgroundSubmission && (!backgroundState.FrameCurrent(backgroundSubmission->frame)
				|| (backgroundSubmission->nativeRevision
					&& !service.CandidateCurrent(WindowRole::Freeze, backgroundSubmission->nativeRevision))))
				DiscardBackgroundSubmission();
			// 已经释放的共享 Freeze 不再被白板重复隐藏；退出确认始终先于最小化暂停。
			if (!request.active && backgroundState.InactiveCommitted(request)) return FrameResult::Idle;
			if (!request.active && !backgroundState.Held())
				return CommitBackgroundInactive(request) ? FrameResult::Idle : FrameResult::Retry;
			const auto native = service.SurfaceSnapshot(WindowRole::Freeze);
			if (!native || !native->known) return FrameResult::Retry;
			if (const auto held = backgroundState.Held(); held
				&& (held->hwnd != native->hwnd || held->generation != native->generation))
			{
				// 只有 owner 的已知退休/新代际快照才能释放旧责任，未知采样不能冒充销毁。
				backgroundState.RetireHeld(*held);
				DiscardBackgroundSubmission();
			}
			if (!request.active)
			{
				const auto held = backgroundState.Held();
				if (!held) return CommitBackgroundInactive(request) ? FrameResult::Idle : FrameResult::Retry;
				if (!backgroundSubmission)
					backgroundSubmission = BackgroundSubmission{{request, held->hwnd, held->generation,
						native->bounds, backgroundState.ContentRevision(), ++backgroundFrameSerial}};
				auto& pending = *backgroundSubmission;
				if (!pending.nativeRevision)
				{
					(void)PublishBackgroundCandidate(pending);
					return FrameResult::Retry;
				}
				const auto receipt = service.TryReadCandidateReceipt(WindowRole::Freeze, pending.nativeRevision);
				if (!receipt || receipt->phase == Inkeys::Window::CommandPhase::Queued
					|| receipt->phase == Inkeys::Window::CommandPhase::Executing) return FrameResult::Retry;
				auto commitLease = service.TryAcquirePresentationLease(WindowRole::Freeze);
				if (!commitLease) return FrameResult::Retry;
				const bool hidden = WhiteboardDetail::BackgroundInactiveMatchesReceipt(request, *held,
					pending.frame.frameSerial, pending.nativeRevision, *receipt,
					receipt->phase == Inkeys::Window::CommandPhase::Committed);
				const auto actual = service.SurfaceSnapshot(WindowRole::Freeze);
				const bool committed = hidden && actual && actual->known && actual->hwnd == held->hwnd
					&& actual->generation == held->generation && !actual->visible
					&& WhiteboardDetail::CompleteBackgroundInactive(backgroundState, request,
						[&](const auto& surface) { return surface == *held && current(); });
				DiscardBackgroundSubmission();
				if (committed) Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
				return committed ? FrameResult::Idle : FrameResult::Retry;
			}
			if (service.WhiteboardWindowGroupMinimized())
			{
				if (backgroundSubmission) { DiscardBackgroundSubmission(); backgroundState.Invalidate(); }
				return FrameResult::Idle;
			}
			const HWND hwnd = native->hwnd;
			const auto generation = native->generation;
			if (!backgroundState.Bind(request, {hwnd, generation})) return FrameResult::Retry;
			const auto currentSurface = [hwnd, generation, current, &service]
			{
				const auto surface = service.SurfaceSnapshot(WindowRole::Freeze);
				return current() && surface && surface->known && surface->hwnd == hwnd && surface->generation == generation;
			};
			// 快照 try_lock 未取到不等于窗口已被替换；ULW 之后只有明确的身份/请求失效才算失败。
			const auto surfaceReplaced = [hwnd, generation, current, &service]
			{
				if (!current()) return true;
				const auto surface = service.SurfaceSnapshot(WindowRole::Freeze);
				return surface && (!surface->known || surface->hwnd != hwnd || surface->generation != generation);
			};
			const auto target = PrimaryTarget();
			const RECT bounds = target.bounds;
			const float scale = target.scale;
			std::unique_lock renderLock(backgroundRenderMutex);
			if (!EqualRect(&configuredBackgroundScreen, &bounds) || configuredBackgroundScale != scale)
			{
				ConfigureBackgroundScene(bounds, scale);
				backgroundState.Invalidate();
			}
			if (backgroundSubmission && !backgroundState.FrameCurrent(backgroundSubmission->frame))
				DiscardBackgroundSubmission();
			if (!backgroundSubmission)
			{
				if (const auto prepared = backgroundState.Prepared(request.revision); prepared
					&& !WhiteboardDetail::BackgroundFrameMatchesSurface(*prepared, *native, request.revision == 0))
					backgroundState.Invalidate();
				(void)backgroundScene.Advance(frameContext.frameTime);
			}
			const RECT presentation = backgroundScene.PresentationBounds();
			const UINT width = static_cast<UINT>(presentation.right - presentation.left);
			const UINT height = static_cast<UINT>(presentation.bottom - presentation.top);
			const auto previousTarget = backgroundScene.Renderer().GetTargetBitmapSize();
			const bool resourcesRecreated = backgroundScene.Renderer().GetDeviceGeneration() != frameContext.epoch.generation
				|| previousTarget.width != width || previousTarget.height != height;
			if (resourcesRecreated)
			{
				backgroundState.Invalidate();
				DiscardBackgroundSubmission();
			}
			const HRESULT resourceHr = backgroundScene.EnsureDeviceResources(frameContext.epoch, width, height);
			if (FAILED(resourceHr)) return IsDeviceLost(resourceHr) ? FrameResult::DeviceLost : FrameResult::Retry;
			if (backgroundSubmission && (!backgroundState.FrameCurrent(backgroundSubmission->frame)
				|| backgroundSubmission->deviceGeneration != frameContext.epoch.generation
				|| !EqualRect(&backgroundSubmission->frame.bounds, &presentation)))
				DiscardBackgroundSubmission();
			if (backgroundSubmission)
			{
				auto& pending = *backgroundSubmission;
				if (!pending.nativeRevision)
				{
					(void)PublishBackgroundCandidate(pending);
					return FrameResult::Retry;
				}
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (hiddenBackgroundTest && hiddenBackgroundHoldReceipt) return FrameResult::Retry;
#endif
				const auto receipt = service.TryReadCandidateReceipt(WindowRole::Freeze, pending.nativeRevision);
				if (!receipt || receipt->phase == Inkeys::Window::CommandPhase::Queued
					|| receipt->phase == Inkeys::Window::CommandPhase::Executing) return FrameResult::Retry;
				auto commitLease = service.TryAcquirePresentationLease(WindowRole::Freeze);
				if (!commitLease) return FrameResult::Retry;
				const auto actual = service.SurfaceSnapshot(WindowRole::Freeze);
				const bool acknowledged = WhiteboardDetail::BackgroundFrameMatchesReceipt(pending.frame,
					pending.nativeRevision, *receipt, receipt->phase == Inkeys::Window::CommandPhase::Committed,
					request.revision == 0) && currentSurface() && actual && !actual->iconic
					&& WhiteboardDetail::BackgroundFrameMatchesSurface(pending.frame, *actual, request.revision == 0);
				if (!acknowledged || !backgroundState.Prepare(pending.frame))
				{
					DiscardBackgroundSubmission();
					backgroundState.Invalidate();
					backgroundScene.Invalidate();
					return FrameResult::Retry;
				}
				backgroundScene.CommitPresented(false);
				(void)backgroundScene.ConsumeDamage();
				const bool animationActive = pending.animationActive;
				DiscardBackgroundSubmission();
				if (!animationActive) backgroundScene.SuspendForIdle(frameContext.frameTime);
				renderLock.unlock();
				commitLease.Reset();
				Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
				return animationActive ? FrameResult::Continue : FrameResult::Idle;
			}
			// 当前内容或设备身份没有成功回执时保留 damage；未决 native 不会再次进入这一绘制分支。
			if (resourcesRecreated || !backgroundState.Prepared(request.revision)) backgroundScene.Invalidate();
			const auto contentRevision = backgroundState.ContentRevision();
			const RECT frameDamage = backgroundScene.PendingDamage();
			if (IsRectEmpty(&frameDamage))
			{
				backgroundScene.SuspendForIdle(frameContext.frameTime);
				return FrameResult::Idle;
			}
			// 与 Workspace Prepare/回滚共用原子租约，owner 忙则让其它客户端继续，不等待 HWND。
			auto nativeLease = service.TryAcquirePresentationLease(WindowRole::Freeze);
			if (!nativeLease) return FrameResult::Retry;
			std::uint64_t presentationRevision = 0;
			auto* rawContext = backgroundScene.DeviceContext();
			auto* rawGdi = backgroundScene.GdiInteropRenderTarget();
			if (!rawContext || !rawGdi) return FrameResult::Retry;
			ComPtr<ID2D1DeviceContext> context(rawContext);
			ComPtr<ID2D1GdiInteropRenderTarget> gdi(rawGdi);
			// 与主栏/分页采用同一绘制事务；先裁剪，成功呈现后才消费脏区。
			backgroundScene.Renderer().BeginFrameDraw(context.Get(), D2D1::RectF(
				static_cast<FLOAT>(frameDamage.left), static_cast<FLOAT>(frameDamage.top),
				static_cast<FLOAT>(frameDamage.right), static_cast<FLOAT>(frameDamage.bottom)));
			(void)backgroundScene.Render(context.Get(), frameContext.frameTime);
			backgroundScene.Renderer().FinishFrameDrawCommands(context.Get());
			HDC source = nullptr;
			HRESULT getDcHr = gdi->GetDC(
				D2D1_DC_INITIALIZE_MODE_COPY, &source);
			HRESULT releaseDcHr = S_OK;
			bool presented = false;
			if (SUCCEEDED(getDcHr) && source)
			{
				POINT destination{ presentation.left, presentation.top };
				POINT sourcePoint{};
				SIZE size{ static_cast<LONG>(width), static_cast<LONG>(height) };
				BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				UPDATELAYEREDWINDOWINFO info{};
				info.cbSize = sizeof(info);
				info.pptDst = &destination;
				info.psize = &size;
				info.pptSrc = &sourcePoint;
				info.hdcSrc = source;
				info.pblend = &blend;
				RECT dirty = frameDamage;
				if (dirty.right <= dirty.left || dirty.bottom <= dirty.top)
					dirty = RECT{ 0, 0, static_cast<LONG>(width),
						static_cast<LONG>(height) };
				info.prcDirty = &dirty;
				info.dwFlags = ULW_ALPHA;
				HDC destinationDc = GetDC(nullptr);
				info.hdcDst = destinationDc;
				const bool pixelsApplied = currentSurface() && SubmitFreezeSurface(hwnd, &info, true);
				if (pixelsApplied) presentationRevision = nativeLease.PixelsChanged(WindowRole::Freeze);
				presented = pixelsApplied && !surfaceReplaced();
				if (destinationDc) ReleaseDC(nullptr, destinationDc);
				const RECT gdiModifiedRect{};
				releaseDcHr = gdi->ReleaseDC(&gdiModifiedRect);
			}
			else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
			const HRESULT endDrawHr = backgroundScene.Renderer().EndFrameDraw(context.Get());
			if (IsDeviceLost(getDcHr) || IsDeviceLost(releaseDcHr)
				|| IsDeviceLost(endDrawHr))
			{
				backgroundState.Invalidate();
				backgroundScene.ReleaseDeviceResources();
				return FrameResult::DeviceLost;
			}
			if (FAILED(getDcHr) || FAILED(releaseDcHr)
				|| FAILED(endDrawHr) || !presented)
			{
				backgroundState.Invalidate();
				backgroundScene.ReleaseDeviceResources();
				return FrameResult::Retry;
			}
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (hiddenBackgroundTest) ++hiddenBackgroundPixelCount;
#endif
			if (surfaceReplaced() || contentRevision != backgroundState.ContentRevision()) return FrameResult::Retry;
			backgroundSubmission = BackgroundSubmission{{request, hwnd, generation, presentation,
				contentRevision, ++backgroundFrameSerial, presentationRevision}, frameContext.epoch.generation,
				0, backgroundScene.AnimationActive()};
			(void)PublishBackgroundCandidate(*backgroundSubmission);
			return FrameResult::Retry;
		}

	}

	int RunHiddenBackgroundTests(const RECT& bounds,
		const std::function<bool()>& gateOwner, const std::function<void()>& releaseOwner)
	{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		using namespace std::chrono_literals;
		if (initialized.exchange(true)) return 1;
		int failures = 0;
		auto Check = [&](bool value, const char* message)
		{
			if (!value) { ++failures; std::fprintf(stderr, "[WhiteboardNative] FAIL %s\n", message); }
		};
		auto& service = Inkeys::Window::GetService();
		const bool previousOwnership = WhiteboardFreezeSurfaceOwned();
		SetWhiteboardFreezeSurfaceOwned(true);
		hiddenBackgroundTarget = { bounds, 1.0F }; hiddenBackgroundTest = true;
		hiddenBackgroundPixelCount = 0; hiddenBackgroundHoldReceipt = false;
		backgroundState.Reset(); backgroundSubmission.reset(); backgroundNativeRevision.store(0);
		configuredBackgroundScreen = {}; configuredBackgroundScale = 0;
		const auto epoch = Inkeys::UI::RenderPipeline::GetDeviceEpoch();
		auto Step = [&](std::uint64_t deviceGeneration = 0)
		{
			auto currentEpoch = epoch;
			if (deviceGeneration) currentEpoch.generation = deviceGeneration;
			return RenderBackground({currentEpoch, Inkeys::UI::RenderPipeline::GetSharedAssets(), std::chrono::steady_clock::now()});
		};
		auto Until = [&](auto&& predicate, std::uint64_t deviceGeneration = 0)
		{
			const auto deadline = std::chrono::steady_clock::now() + 5s;
			while (std::chrono::steady_clock::now() < deadline)
			{
				(void)Step(deviceGeneration);
				if (predicate()) return true;
				std::this_thread::sleep_for(1ms);
			}
			return predicate();
		};
		Check(service.Hide(WindowRole::Freeze), "fixture begins with a hidden Freeze");
		Check(backgroundState.Publish(true, 101), "first background request is accepted");
		hiddenBackgroundHoldReceipt = true;
		Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }), "production ULW publishes a native candidate");
		const auto firstPixels = hiddenBackgroundPixelCount;
		for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
		Check(firstPixels == 1 && hiddenBackgroundPixelCount == firstPixels && !BackgroundPrepared(101),
			"pending native receipt keeps full-screen pixels without repeated redraw or early prepared ACK");
		hiddenBackgroundHoldReceipt = false;
		Check(Until([&] { return BackgroundPrepared(101); }), "full matching owner receipt prepares first background");
		Check(!BackgroundMatchesActive(true, 101), "prepared hidden pixels cannot masquerade as final visible background");
		Check(service.Show(WindowRole::Freeze) && Until([&] { return BackgroundMatchesActive(true, 101); })
			&& hiddenBackgroundPixelCount == firstPixels,
			"published real native visibility completes Active without another pixel submission");

		// 退出不需要 ULW；阻塞真实 owner 时 UI callback 必须立即返回且不能提前 ACK。
		const bool gated = gateOwner && gateOwner();
		Check(gated, "background inactive test pauses the real overlay owner");
		Check(backgroundState.Publish(false, 102), "inactive request is accepted");
		const auto hideStarted = std::chrono::steady_clock::now();
		for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
		Check(std::chrono::steady_clock::now() - hideStarted < 250ms
			&& !BackgroundMatchesActive(false, 102) && hiddenBackgroundPixelCount == firstPixels,
			"inactive callbacks neither wait for owner nor repaint nor acknowledge queued Hide");
		if (releaseOwner) releaseOwner();
		Check(Until([&] { return BackgroundMatchesActive(false, 102); }), "real Hide receipt completes inactive ACK");

		Check(backgroundState.Publish(true, 103), "reentry request is accepted");
		hiddenBackgroundHoldReceipt = true;
		Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }), "reentry publishes pixels before delayed ACK");
		const auto retiredFrame = backgroundSubmission ? backgroundSubmission->frame : WhiteboardDetail::BackgroundState::Frame{};
		backgroundState.Invalidate(); CancelBackgroundCandidate();
		{ std::scoped_lock pageLock(pageStateMutex); themeRgb = 0x172A46; themePattern = 2; }
		configuredBackgroundScale = 0;
		Check(!backgroundState.Prepare(retiredFrame) && !BackgroundPrepared(103), "same-revision theme invalidation rejects old frame ACK");
		hiddenBackgroundHoldReceipt = false;
		Check(Until([&] { return BackgroundPrepared(103); }), "latest themed background obtains its own production receipt");

		Check(backgroundState.Publish(true, 104), "device-generation request is accepted");
		hiddenBackgroundHoldReceipt = true;
		Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }), "pre-device frame is pending");
		const auto oldDeviceFrame = backgroundSubmission ? backgroundSubmission->frame : WhiteboardDetail::BackgroundState::Frame{};
		hiddenBackgroundHoldReceipt = false;
		Check(Until([&] { return BackgroundPrepared(104); }, epoch.generation + 1)
			&& !backgroundState.FrameCurrent(oldDeviceFrame), "device generation discards old pixel candidate before acknowledging rebuilt resources");
		Check(backgroundState.Publish(false, 105), "final cleanup request is accepted");
		Check(Until([&] { return BackgroundMatchesActive(false, 105); }), "final native Hide retires shared Freeze ownership");
		CancelBackgroundCandidate(); DiscardBackgroundSubmission();
		(void)service.Hide(WindowRole::Freeze);
		backgroundScene.Reset(); backgroundState.Reset();
		configuredBackgroundScreen = {}; configuredBackgroundScale = 0;
		renderedBackgroundTarget.store({});
		{ std::scoped_lock pageLock(pageStateMutex); themeRgb = 0x123B32; themePattern = 0; }
		hiddenBackgroundHoldReceipt = false; hiddenBackgroundTest = false;
		SetWhiteboardFreezeSurfaceOwned(previousOwnership);
		initialized.store(false);
		std::fprintf(stderr, "[WhiteboardNative] failures=%d pixels=%llu\n", failures,
			static_cast<unsigned long long>(hiddenBackgroundPixelCount));
		return failures;
#else
		(void)bounds; (void)gateOwner; (void)releaseOwner; return 1;
#endif
	}

	bool Initialize(BusinessCallbacks callbacks)
	{
		if (initialized.exchange(true, std::memory_order_acq_rel)) return false;
		expandedLayoutTarget.store(false, std::memory_order_release);
		active.store(false, std::memory_order_release);
		backgroundState.Reset();
		{
			std::scoped_lock lock(pageStateMutex);
			pageTransaction.Reset();
		}
		if (!Inkeys::UI::PageControl::Acquire())
		{
			initialized.store(false, std::memory_order_release);
			return false;
		}
		Inkeys::UI::PageControl::SetWhiteboardCallbacks({
			std::move(callbacks.previousPage),
			std::move(callbacks.nextPage),
			std::move(callbacks.closeWhiteboard),
			std::move(callbacks.changeTheme),
		});
		if (!Inkeys::UI::RenderPipeline::Register(
			Client::WhiteboardFreeze, RenderBackground))
		{
			Inkeys::UI::PageControl::SetWhiteboardCallbacks({});
			Inkeys::UI::PageControl::Release();
			initialized.store(false, std::memory_order_release);
			return false;
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[](Inkeys::Display::SnapshotPtr snapshot)
			{
				if (!initialized.load(std::memory_order_acquire)) return;
				const auto* monitor = snapshot ? snapshot->Primary() : nullptr;
				const auto rendered = renderedBackgroundTarget.load(std::memory_order_acquire);
				const bool changed = !monitor || !rendered || !EqualRect(&monitor->bounds, &rendered->bounds)
					|| rendered->scale != static_cast<float>(monitor->effectiveDpiX ? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI;
				if (changed) { backgroundState.Invalidate(); CancelBackgroundCandidate(); Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze); }
				Inkeys::UI::PageControl::NotifyLayoutChanged();
			});
		PublishPageControlSnapshot();
		return true;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		expandedLayoutTarget.store(false, std::memory_order_release);
		active.store(false, std::memory_order_release);
		PublishPageControlSnapshot();
		displaySubscription.Reset();
		CancelBackgroundCandidate();
		Inkeys::UI::RenderPipeline::Unregister(Client::WhiteboardFreeze);
		(void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
		{
			std::unique_lock renderLock(backgroundRenderMutex);
			DiscardBackgroundSubmission();
			backgroundScene.Reset();
			backgroundScene.ReleaseDeviceResources();
		}
		backgroundState.Reset();
		Inkeys::UI::PageControl::SetWhiteboardCallbacks({});
		Inkeys::UI::PageControl::Release();
		std::scoped_lock pageLock(pageStateMutex);
		pageTransaction.Reset();
	}

	WNDPROC WindowProc() noexcept
	{
		return Inkeys::UI::PageControl::WindowProc();
	}

	void PublishExpandedLayoutTarget(bool value) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		if (expandedLayoutTarget.exchange(value, std::memory_order_acq_rel)
			== value) return;
		PublishPageControlSnapshot();
	}

	void PublishActive(bool value, std::uint64_t revision) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		if (!backgroundState.Publish(value, revision)) return;
		CancelBackgroundCandidate();
		if (!value && backgroundState.InactiveCommitted(backgroundState.Desired()))
			Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
		active.store(value, std::memory_order_release);
		PublishPageControlSnapshot();
		Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
	}

	void CancelPointerCapture() noexcept
	{
		if (initialized.load(std::memory_order_acquire))
			Inkeys::UI::PageControl::CancelPointerCapture();
	}

	void PublishPageSnapshot(int current, int total, bool changing,
		std::uint32_t rgb, int pattern, bool unsaved, std::uint64_t generation) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool themeChanged;
		{
			// 页码、主题和身份一次提交，禁止主题回执夹带上一页的可交互状态。
			std::scoped_lock lock(pageStateMutex);
			(void)pageTransaction.Publish(current, total, changing);
			themeChanged = themeRgb != rgb || themePattern != pattern;
			themeRgb = rgb; themePattern = pattern;
			unsavedPages = unsaved; pageGeneration = generation;
		}
		if (themeChanged)
		{
			std::scoped_lock lock(backgroundRenderMutex);
			configuredBackgroundScale = 0;
			backgroundState.Invalidate();
			CancelBackgroundCandidate();
		}
		PublishPageControlSnapshot();
		if (themeChanged) Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
	}

	void PublishPageState(int current, int total, bool changing) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool changed = false;
		{
			std::scoped_lock lock(pageStateMutex);
			const PageState previous = pageTransaction.Snapshot();
			const PageState next = pageTransaction.Publish(current, total, changing);
			changed = previous.currentPage != next.currentPage
				|| previous.totalPage != next.totalPage
				|| previous.switching != next.switching
				|| previous.nextIsAdd != next.nextIsAdd;
		}
		if (changed) PublishPageControlSnapshot();
	}

	bool Active() noexcept { return active.load(std::memory_order_acquire); }

	bool BackgroundPrepared(std::uint64_t revision) noexcept
	{
		const auto frame = backgroundState.Prepared(revision);
		const auto native = Inkeys::Window::GetService().SurfaceSnapshot(WindowRole::Freeze);
		return frame && native && WhiteboardDetail::BackgroundFrameMatchesSurface(*frame, *native, false)
			&& backgroundState.FrameCurrent(*frame);
	}

	bool BackgroundMatchesActive(bool value, std::uint64_t revision) noexcept
	{
		const auto request = backgroundState.Desired();
		if (request.active != value || (revision && request.revision != revision)) return false;
		if (!value) return backgroundState.InactiveCommitted(request);
		const auto frame = backgroundState.Prepared(revision);
		const auto native = Inkeys::Window::GetService().SurfaceSnapshot(WindowRole::Freeze);
		// 最终显隐来自 Workspace Complete 的 owner 快照，不以 desired 或准备回执冒充 Active。
		return frame && native && WhiteboardDetail::BackgroundFrameMatchesSurface(*frame, *native, true)
			&& backgroundState.FrameCurrent(*frame);
	}
}
