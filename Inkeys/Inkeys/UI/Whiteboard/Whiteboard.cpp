module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../../../IdtFreezeFrame.h"
#include "Whiteboard.Theme.h"
#include "Whiteboard.BackgroundState.h"
#include "Whiteboard.PatternFollow.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
#include "../../Drawing/Draw3/Draw3.DiagnosticOutput.h"
#endif

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

		// ---- Freeze 归属下的点阵/网格相位跟随（kWhiteboardPatternOwnedByFreeze；Draw3 归属时全部惰性） ----
		// Draw3 绘制线程经 PublishCanvasViewport 无锁写入最新视口；UI3 渲染线程在 RenderBackground 直接读取。
		WhiteboardFollow::ViewportChannel viewportChannel;
		std::atomic<std::uint64_t> viewportSerial = 0;
		std::atomic_bool followRequestPending = false;
		// 0 空白 / 1 点阵 / 2 网格，与 themePattern 同锁更新；写端据此对 Blank 主题零唤醒。
		std::atomic<int> patternKindMirror = 0;
		// 已知页快照的 generation：更老的视口样本来自上一段会话/上一页，不得画到当前页上。
		std::atomic<std::uint64_t> knownPageGeneration = 0;
		// 以下仅 UI3 渲染线程访问（隐藏测试串行驱动同一回调）。
		WhiteboardFollow::State followState;
		WhiteboardFollow::Phase followPhase;      // drawContent 在本帧 Render 时读取的相位
		WhiteboardFollow::Sample followSample;    // 本帧所画样本（身份供 State 记录与诊断）
		bool followChainRequested = false;        // 回执确认后本回调内紧接着再画一帧
		bool followReadContended = false;         // 读通道遇到持续写入：本回调返回 Retry
		bool followFramePhaseDriven = false;      // 本帧由相位变化触发（诊断只统计这类更新）
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
		// 仅 Debug 且环境变量 INKEYS_WHITEBOARD_PATTERN_PROFILE=1：M1 Freeze 纹理更新率，M2 相对 Draw3 视口的滞后。
		// 固定 512 样本，不逐帧分配/格式化/写盘；一次平移片段（moving 结束的落定帧）汇总输出一行。
		struct FollowProfiler
		{
			bool enabled = false;
			WhiteboardFollow::Profile profile;
			LARGE_INTEGER frequency{};
			FollowProfiler()
			{
				wchar_t value[2]{};
				enabled = GetEnvironmentVariableW(L"INKEYS_WHITEBOARD_PATTERN_PROFILE", value, 2) == 1 && value[0] == L'1';
				QueryPerformanceFrequency(&frequency);
			}
			double Ms(std::int64_t qpc) const noexcept
			{
				return frequency.QuadPart ? static_cast<double>(qpc) * 1000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
			}
			void Settle(const WhiteboardFollow::Sample& sample) noexcept
			{
				if (!enabled || !sample.active || sample.moving || profile.Empty()) return;
				LARGE_INTEGER now{};
				QueryPerformanceCounter(&now);
				profile.RecordSettle(sample.qpc ? Ms(now.QuadPart - sample.qpc) : 0.0);
				Flush();
			}
			void Flush() noexcept
			{
				if (profile.Empty()) return;
				const auto s = profile.Summarize();
				Inkeys::Drawing::Draw3::DiagnosticOutput::Printf(Inkeys::Drawing::Draw3::DiagnosticOutput::Stream::Error,
					"[WhiteboardPatternPerf] M1 updates=%zu dropped=%zu segment-ms=%.1f ups=%.1f gap-ms p50=%.2f p95=%.2f max=%.2f cost-ms p50=%.2f p95=%.2f max=%.2f\n"
					"[WhiteboardPatternPerf] M2 lag-frames p50=%.1f p95=%.1f max=%.1f ack-lag-frames p50=%.1f p95=%.1f max=%.1f (n=%zu) lag-ms p50=%.2f p95=%.2f max=%.2f lag-px p50=%.1f p95=%.1f max=%.1f settle-ms=%.2f\n",
					s.updates, s.dropped, s.segmentMs, s.updatesPerSecond, s.gapP50, s.gapP95, s.gapMax,
					s.costMsP50, s.costMsP95, s.costMsMax,
					s.lagFramesP50, s.lagFramesP95, s.lagFramesMax, s.ackLagFramesP50, s.ackLagFramesP95, s.ackLagFramesMax,
					s.ackSamples, s.lagMsP50, s.lagMsP95, s.lagMsMax, s.lagPxP50, s.lagPxP95, s.lagPxMax, s.settleMs);
				profile.Reset();
			}
		};
		FollowProfiler followProfiler;
#endif

		struct BackgroundTarget { RECT bounds{}; float scale = 1.0F; };
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		// 仅屏外 fixture 注入目标；生产仍只读 Display 的主屏快照。
		BackgroundTarget hiddenBackgroundTarget;
		bool hiddenBackgroundTest = false, hiddenBackgroundHoldReceipt = false;
		std::uint64_t hiddenBackgroundPixelCount = 0;
		// 仅测试：在 D2D 目标交给 ULW 之前读回整屏像素（预乘 BGRA）。
		std::function<void(HDC, int, int)> hiddenBackgroundCapture;
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
			// 纹理归属由 kWhiteboardPatternOwner 选择：Freeze 归属画底色加带相位的点阵/网格（相位由 RenderBackground
			// 在每次 Render 前写入 followPhase，闭包不捕获，避免相位变化触发重配置）；Draw3 归属只画纯色。
			std::uint32_t rgb; int pattern;
			{ std::scoped_lock lock(pageStateMutex); rgb=themeRgb; pattern=themePattern; }
			if constexpr (Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze)
				background.drawContent = [rgb, pattern](auto* dc, auto rect)
				{
					Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
					if (!dc || FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(rgb), &brush))) return;
					dc->FillRectangle(rect, brush.Get());
					WhiteboardVisual::DrawThemePatternPhase(dc, rect, rgb, pattern, followPhase.xDip, followPhase.yDip, brush.Get());
				};
			else
				background.drawContent = [rgb](auto* dc, auto rect) { WhiteboardVisual::DrawThemeSolid(dc,rect,rgb); };
			background.fill = RGB(18, 59, 50);
			background.frame = RGB(18, 59, 50);
			background.useThemeColors = false;
			background.cornerRadiusDip = 0.0;
			background.frameThicknessDip = 0.0;
			background.fillOpacity = 1.0;
			background.frameOpacity = 0.0;
			(void)backgroundScene.ConfigureBackground(background);
			(void)backgroundScene.SetBounds(bounds, scale);
			// 重配置会整屏重画：已画相位作废，下一帧按最新视口重新记录。
			followState = {};
			configuredBackgroundScreen = bounds;
			configuredBackgroundScale = scale;
			renderedBackgroundTarget.store(std::make_shared<const BackgroundTarget>(BackgroundTarget{bounds, scale}), std::memory_order_release);
		}

		[[nodiscard]] bool IsDeviceLost(HRESULT hr) noexcept
		{
			return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET
				|| hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		void ReleaseInactiveBackgroundResources()
		{
			std::scoped_lock renderLock(backgroundRenderMutex);
			// 仅退休当前客户端的像素资源；文稿、主题与共享 WARP epoch 均保持。
			backgroundScene.ReleaseDeviceResources();
			configuredBackgroundScreen = {};
			configuredBackgroundScale = 0;
			renderedBackgroundTarget.store({});
		}

		bool CommitBackgroundInactive(const WhiteboardDetail::BackgroundState::Request& request)
		{
			if (!backgroundState.CommitInactive(request)) return false;
			ReleaseInactiveBackgroundResources();
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

		// 读取最新视口并规划 Freeze 相位（不改 followState）。样本不可用时沿用上一帧所画相位，
		// 从未画过则使用视口 (0,0) 的相位（与改动前像素一致）。
		[[nodiscard]] WhiteboardFollow::Plan EvaluatePatternFollow(double widthPx, double heightPx,
			float scale, WhiteboardFollow::Sample& sample) noexcept
		{
			sample = {};
			switch (viewportChannel.Read(sample))
			{
			case WhiteboardFollow::ReadResult::Ok: break;
			case WhiteboardFollow::ReadResult::Contended: followReadContended = true; [[fallthrough]];
			case WhiteboardFollow::ReadResult::Empty: sample = {}; break;
			}
			return WhiteboardFollow::PlanFollow(sample, knownPageGeneration.load(std::memory_order_acquire),
				followState, widthPx, heightPx, scale, patternKindMirror.load(std::memory_order_acquire) != 0);
		}

		FrameResult RenderBackgroundOnce(const FrameContext& frameContext)
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
			if (!request.active && backgroundState.InactiveCommitted(request))
			{
				ReleaseInactiveBackgroundResources();
				return FrameResult::Idle;
			}
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
				if (committed)
				{
					commitLease.Reset();
					ReleaseInactiveBackgroundResources();
					Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
				}
				return committed ? FrameResult::Idle : FrameResult::Retry;
			}
			if (service.WhiteboardWindowGroupMinimized())
			{
				if (backgroundSubmission) { DiscardBackgroundSubmission(); backgroundState.Invalidate(); }
				if (!native->visible || native->iconic)
				{
					// 只有 owner 已发布真实隐藏/最小化，才归还背景；恢复沿原门完整重建。
					backgroundState.Invalidate();
					ReleaseInactiveBackgroundResources();
				}
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
				// 流水线：回执确认后若 Draw3 视口又前进，同一回调内紧接着画下一帧（否则每帧需两个批次，速率腰斩）。
				// 租约与渲染锁在此全部释放，下一遍从头重新校验；只在无在途提交时才会新增 scene damage。
				bool chain = false;
				if constexpr (Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze)
				{
					WhiteboardFollow::Sample latest;
					const auto plan = EvaluatePatternFollow(static_cast<double>(bounds.right - bounds.left),
						static_cast<double>(bounds.bottom - bounds.top), scale, latest);
					chain = plan.draw;
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
					if (followProfiler.enabled && plan.usable)
						followProfiler.profile.RecordAck(static_cast<double>(latest.serial - followState.drawnSerial));
					// 落定相位已画出时没有下一像素帧，也必须结算片段。
					if (plan.usable && !plan.draw) followProfiler.Settle(latest);
#endif
				}
				if (!animationActive && !chain) backgroundScene.SuspendForIdle(frameContext.frameTime);
				renderLock.unlock();
				commitLease.Reset();
				Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
				followChainRequested = chain;
				return animationActive ? FrameResult::Continue : FrameResult::Idle;
			}
			// Freeze 归属：读取 Draw3 最新视口。相位变化只是场景像素脏区（scene damage），不是内容失效——
			// 绝不调用 backgroundState.Invalidate()/CancelBackgroundCandidate()，否则连续平移会每帧取消在途回执。
			// 此处必然没有在途提交（上方分支一定返回），所以新增 damage 不会被回执确认时的 ConsumeDamage 吞掉。
			followFramePhaseDriven = false;
			if constexpr (Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze)
			{
				const auto plan = EvaluatePatternFollow(static_cast<double>(bounds.right - bounds.left),
					static_cast<double>(bounds.bottom - bounds.top), scale, followSample);
				followPhase = plan.phase; // 本帧无论由何触发，所有重画都使用最新可用相位
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
				if (plan.usable && !plan.draw) followProfiler.Settle(followSample);
#endif
				if (plan.draw) { backgroundScene.Invalidate(); followFramePhaseDriven = true; }
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
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
			LONGLONG followDrawStartQpc = 0;
			if (followProfiler.enabled)
			{
				LARGE_INTEGER start{};
				QueryPerformanceCounter(&start);
				followDrawStartQpc = start.QuadPart;
			}
#endif
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
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (hiddenBackgroundCapture) hiddenBackgroundCapture(source, static_cast<int>(width), static_cast<int>(height));
#endif
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
			if constexpr (Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze)
			{
				// 记下实际画出的相位与样本身份，供下一次“是否需要重画”比较。
				const bool patternDrawn = patternKindMirror.load(std::memory_order_acquire) != 0;
				followState.drawnValid = patternDrawn;
				if (patternDrawn)
				{
					followState.drawn = followPhase;
					if (followSample.active) followState.drawnSerial = followSample.serial;
				}
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
				if (followProfiler.enabled && followFramePhaseDriven && followSample.active)
				{
					LARGE_INTEGER now{};
					QueryPerformanceCounter(&now);
					WhiteboardFollow::Sample latest;
					if (viewportChannel.Read(latest) == WhiteboardFollow::ReadResult::Ok)
					{
						const double lagPx = std::hypot(static_cast<double>(latest.x) - followSample.x,
							static_cast<double>(latest.y) - followSample.y);
						followProfiler.profile.Record(followProfiler.Ms(now.QuadPart),
							static_cast<double>(latest.serial - followSample.serial),
							followSample.qpc ? followProfiler.Ms(now.QuadPart - followSample.qpc) : 0.0, lagPx,
							followProfiler.Ms(now.QuadPart - followDrawStartQpc));
						followProfiler.Settle(followSample);
					}
				}
#endif
			}
			backgroundSubmission = BackgroundSubmission{{request, hwnd, generation, presentation,
				contentRevision, ++backgroundFrameSerial, presentationRevision}, frameContext.epoch.generation,
				0, backgroundScene.AnimationActive()};
			(void)PublishBackgroundCandidate(*backgroundSubmission);
			return FrameResult::Retry;
		}

		// 渲染回调入口。Freeze 归属下，回执确认后若 Draw3 视口又前进，则在同一回调里再跑一遍以画下一帧
		// （第一遍已释放全部租约和渲染锁并重新读取通道），使每个 UI3 批次产出一个像素帧；第二遍只会提交新帧，
		// 不会再次确认，因此至多两遍。
		FrameResult RenderBackground(const FrameContext& frameContext)
		{
			if (!initialized.load(std::memory_order_acquire)) return FrameResult::Idle;
			// 先清写端的“已请求”标记再读通道：写端若在此之后发布，会看到 false 并重新请求（两端都是 RMW，互相可见）。
			followRequestPending.exchange(false, std::memory_order_acq_rel);
			followReadContended = false;
			followChainRequested = false;
			FrameResult result = RenderBackgroundOnce(frameContext);
			if constexpr (Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze)
			{
				if (followChainRequested)
				{
					followChainRequested = false;
					const FrameResult chained = RenderBackgroundOnce(frameContext);
					followChainRequested = false;
					result = (std::max)(result, chained);
				}
				// 通道被写端持续占用到自旋上限（实际不会发生）：下一批再读，不能丢掉落定帧。
				if (followReadContended && result < FrameResult::Retry) result = FrameResult::Retry;
			}
			return result;
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
		Check(backgroundScene.Renderer().GetTargetBitmapSize().width > 0 && backgroundScene.DeviceContext(),
			"prepared background owns real WARP target resources");
		Check(service.Show(WindowRole::Freeze) && Until([&] { return BackgroundMatchesActive(true, 101); })
			&& hiddenBackgroundPixelCount == firstPixels,
			"published real native visibility completes Active without another pixel submission");

		// 退出不需要 ULW；阻塞真实 owner 时 UI callback 必须立即返回且不能提前 ACK。
		const bool gated = gateOwner && gateOwner();
		Check(gated, "background inactive test pauses the real overlay owner");
		Check(backgroundState.Publish(false, 102), "inactive request is accepted");
		const auto hideStarted = std::chrono::steady_clock::now();
		for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
		std::fprintf(stderr, "[WhiteboardNative] owner-held inactive callbacks: %.2f ms (measurement only)\n",
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - hideStarted).count());
		Check(!BackgroundMatchesActive(false, 102) && hiddenBackgroundPixelCount == firstPixels,
			"owner-held inactive callbacks neither repaint nor acknowledge queued Hide");
		Check(backgroundScene.Renderer().GetTargetBitmapSize().width > 0,
			"pending Hide keeps target until real owner retirement");
		if (releaseOwner) releaseOwner();
		Check(Until([&] { return BackgroundMatchesActive(false, 102); }), "real Hide receipt completes inactive ACK");
		Check(backgroundScene.Renderer().GetTargetBitmapSize().width == 0 && !backgroundScene.DeviceContext(),
			"real inactive ACK releases the full-screen target and private context");
		for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
		Check(backgroundScene.Renderer().GetTargetBitmapSize().width == 0
			&& hiddenBackgroundPixelCount == firstPixels,
			"inactive callbacks do not rebuild pixels after retirement");
		Check(Inkeys::UI::RenderPipeline::GetDeviceEpoch().d3dDevice.Get() == epoch.d3dDevice.Get(),
			"retiring the client leaves shared WARP device unchanged");

		Check(backgroundState.Publish(true, 103), "reentry request is accepted");
		hiddenBackgroundHoldReceipt = true;
		Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }), "reentry publishes pixels before delayed ACK");
		Check(backgroundScene.Renderer().GetTargetBitmapSize().width > 0
			&& hiddenBackgroundPixelCount > firstPixels,
			"reentry rebuilds a real target and submits fresh pixels before ready");
		const auto retiredFrame = backgroundSubmission ? backgroundSubmission->frame : WhiteboardDetail::BackgroundState::Frame{};
		backgroundState.Invalidate(); CancelBackgroundCandidate();
		{ std::scoped_lock pageLock(pageStateMutex); themeRgb = 0x172A46; themePattern = 2; }
		configuredBackgroundScale = 0;
		Check(!backgroundState.Prepare(retiredFrame) && !BackgroundPrepared(103), "same-revision theme invalidation rejects old frame ACK");
		hiddenBackgroundHoldReceipt = false;
		Check(Until([&] { return BackgroundPrepared(103); }), "latest themed background obtains its own production receipt");

		// ---- 点阵/网格随 Draw3 视口重画相位（真实 WARP 场景 + 真实 Freeze HWND/owner；像素对参考 D2D 渲染） ----
		{
			namespace Draw3 = Inkeys::Drawing::Draw3;
			const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
			const std::uint64_t generation = 7;
			std::vector<std::uint32_t> captured;
			hiddenBackgroundCapture = [&](HDC source, int w, int h)
			{
				BITMAPINFO info{};
				info.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
				void* bits = nullptr;
				HDC memory = CreateCompatibleDC(nullptr);
				HBITMAP dib = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
				if (dib && bits)
				{
					const HGDIOBJ old = SelectObject(memory, dib);
					BitBlt(memory, 0, 0, w, h, source, 0, 0, SRCCOPY);
					SelectObject(memory, old);
					captured.assign(static_cast<const std::uint32_t*>(bits),
						static_cast<const std::uint32_t*>(bits) + static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
				}
				if (dib) DeleteObject(dib);
				if (memory) DeleteDC(memory);
			};
			ComPtr<ID2D1DeviceContext> reference;
			Check(epoch.d2dDevice && SUCCEEDED(epoch.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &reference)),
				"pattern fixture creates an independent D2D reference context on the shared WARP device");
			// 参考：旧 Freeze 的 DrawTheme（视口 0）；非零视口用旧 DrawThemePattern 在锚点平移后的大 bounds 上重画。
			auto Reference = [&](float scale, std::uint32_t rgb, int pattern, double viewportX, double viewportY, bool shifted, bool exactPhase = false)
			{
				std::vector<std::uint32_t> pixels;
				ComPtr<ID2D1Bitmap1> target, readable;
				const auto size = D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height));
				const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
				if (!reference || FAILED(reference->CreateBitmap(size, nullptr, 0,
						D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, 96.0F, 96.0F), &target))
					|| FAILED(reference->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(
						D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, 96.0F, 96.0F), &readable))) return pixels;
				reference->SetTarget(target.Get());
				reference->BeginDraw();
				reference->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
				reference->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
				const D2D1_RECT_F rect = D2D1::RectF(0.0F, 0.0F, static_cast<float>(width / scale), static_cast<float>(height / scale));
				if (exactPhase)
				{
					WhiteboardVisual::DrawThemeSolid(reference.Get(), rect, rgb);
					const auto phase = WhiteboardFollow::ResolvePhase(width, height, scale, viewportX, viewportY);
					WhiteboardVisual::DrawThemePatternPhase(reference.Get(), rect, rgb, pattern, phase.xDip, phase.yDip);
				}
				else if (!shifted) WhiteboardVisual::DrawTheme(reference.Get(), rect, rgb, pattern);
				else
				{
					WhiteboardVisual::DrawThemeSolid(reference.Get(), rect, rgb);
					const double anchorX = width * 0.5 - viewportX, anchorY = height * 0.5 - viewportY;
					const double halfX = (std::max)(std::fabs(anchorX), std::fabs(width - anchorX)) + 96.0 * scale;
					const double halfY = (std::max)(std::fabs(anchorY), std::fabs(height - anchorY)) + 96.0 * scale;
					WhiteboardVisual::DrawThemePattern(reference.Get(), D2D1::RectF(
						static_cast<float>((anchorX - halfX) / scale), static_cast<float>((anchorY - halfY) / scale),
						static_cast<float>((anchorX + halfX) / scale), static_cast<float>((anchorY + halfY) / scale)), rgb, pattern);
				}
				const HRESULT drawn = reference->EndDraw();
				reference->SetTarget(nullptr);
				D2D1_MAPPED_RECT mapped{};
				if (FAILED(drawn) || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr))
					|| FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return pixels;
				pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
				for (int row = 0; row < height; ++row)
					std::memcpy(pixels.data() + static_cast<std::size_t>(row) * width, mapped.bits + static_cast<std::size_t>(row) * mapped.pitch,
						static_cast<std::size_t>(width) * 4);
				readable->Unmap();
				return pixels;
			};
			// 返回 {maxChannelDelta, pixelCount>2}，忽略四周 border 像素。
			auto Compare = [&](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int border)
			{
				std::pair<int, std::size_t> result{ a.size() == b.size() && !a.empty() ? 0 : 255, 0 };
				if (result.first) return result;
				for (std::size_t index = 0; index < a.size(); ++index)
				{
					if (a[index] == b[index]) continue;
					const int x = static_cast<int>(index % static_cast<std::size_t>(width)), y = static_cast<int>(index / static_cast<std::size_t>(width));
					if (x < border || y < border || x >= width - border || y >= height - border) continue;
					int pixelMax = 0;
					for (int shift = 0; shift < 32; shift += 8)
						pixelMax = (std::max)(pixelMax, std::abs(static_cast<int>((a[index] >> shift) & 255) - static_cast<int>((b[index] >> shift) & 255)));
					result.first = (std::max)(result.first, pixelMax);
					if (pixelMax > 2) ++result.second;
				}
				return result;
			};
			auto PublishSample = [&](bool active, bool moving, float x, float y, std::uint64_t sampleGeneration = ~std::uint64_t{ 0 }, std::uint32_t page = 0)
			{
				Draw3::WhiteboardViewportSample sample;
				sample.active = active; sample.moving = moving; sample.x = x; sample.y = y; sample.page = page;
				sample.generation = sampleGeneration == ~std::uint64_t{ 0 } ? generation : sampleGeneration;
				PublishCanvasViewport(sample); // 生产写端；测试中白板模块未激活，不会 Request 真实调度器
			};
			// 与 PublishPageSnapshot 相同的主题失效动作（不调用会唤醒调度器/分页窗口的公共入口）。
			auto SetTheme = [&](std::uint32_t rgb, int pattern)
			{
				bool retires;
				{
					std::scoped_lock lock(pageStateMutex);
					retires = WhiteboardFollow::ThemeChangeRetiresPixels(themeRgb, themePattern, rgb, pattern,
						Draw3::kWhiteboardPatternOwnedByFreeze);
					themeRgb = rgb; themePattern = pattern;
					patternKindMirror.store(pattern, std::memory_order_release);
					knownPageGeneration.store(generation, std::memory_order_release);
				}
				if (retires) { configuredBackgroundScale = 0; backgroundState.Invalidate(); CancelBackgroundCandidate(); }
				return retires;
			};
			auto Settle = [&](const char* name)
			{
				// 必须真的产生了新像素（而不是沿用上一帧的 Prepared 状态）并且回执链完整结束。
				const auto before = hiddenBackgroundPixelCount;
				Check(Until([&] { return hiddenBackgroundPixelCount > before && !backgroundSubmission && BackgroundPrepared(103); }), name);
			};
			constexpr int kEdge = 3;
			// 参考侧浮点锚点的舍入噪声经 D2D/WARP 定点顶点吸附最多放大为约 4 LSB；相位错误至少几十 LSB。
			constexpr int kShiftedTolerance = 6;
			if constexpr (Draw3::kWhiteboardPatternOwnedByFreeze)
			{
				for (const float patternScale : { 1.0F, 1.5F })
					for (const int pattern : { 1, 2 })
					{
						hiddenBackgroundTarget.scale = patternScale;
						SetTheme(0x123B32, pattern);
						PublishSample(true, false, 0.0F, 0.0F);
						Settle("pattern frame at viewport zero settles through the production receipt");
						const auto zero = Compare(captured, Reference(patternScale, 0x123B32, pattern, 0.0, 0.0, false), kEdge);
						std::fprintf(stderr, "[WhiteboardNative] Freeze %s scale=%.1f viewport(0,0) vs legacy DrawTheme: maxDelta=%d over2=%zu\n",
							pattern == 1 ? "dots" : "grid", patternScale, zero.first, zero.second);
						Check(zero.first <= (patternScale == 1.0F ? 0 : 1), "viewport zero reproduces the pre-follow Freeze pattern");
						PublishSample(true, false, 137.25F, -61.5F);
						Settle("pattern frame at a fractional viewport settles");
						const auto moved = Compare(captured, Reference(patternScale, 0x123B32, pattern, 137.25, -61.5, true), 8);
						std::fprintf(stderr, "[WhiteboardNative] Freeze %s scale=%.1f viewport(137.25,-61.5) vs anchor-shifted legacy: maxDelta=%d over2=%zu\n",
							pattern == 1 ? "dots" : "grid", patternScale, moved.first, moved.second);
						Check(moved.first <= kShiftedTolerance, "shifted viewport matches the anchor-shifted legacy reference");
					}
				hiddenBackgroundTarget.scale = 1.0F;
			}
			else
			{
				// Draw3 归属：Freeze 只画纯色，仅 RGB 变化才退休像素；纹理类型变化不得使已准备帧失效。
				std::fprintf(stderr, "[WhiteboardNative] SKIP Freeze-follow streams/M1/M2: Draw3 owns pattern; verifying Freeze solid colour only\n");
				Check(SetTheme(0x123B32, 1), "RGB change retires Freeze pixels under Draw3 ownership");
				PublishSample(true, false, 137.25F, -61.5F);
				Settle("Draw3-owned fallback settles");
				bool uniform = !captured.empty();
				for (const std::uint32_t value : captured) uniform = uniform && value == 0xFF123B32u;
				Check(uniform, "Draw3 ownership leaves Freeze with only the opaque theme colour");
				const auto prepared = backgroundState.Prepared(103);
				const auto pixelsNow = hiddenBackgroundPixelCount;
				Check(!SetTheme(0x123B32, 2) && prepared && backgroundState.Prepare(*prepared),
					"Draw3 ownership: pattern-kind change does not retire the prepared frame");
				PublishSample(true, true, 500.0F, 0.0F);
				for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
				Check(hiddenBackgroundPixelCount == pixelsNow, "Draw3 ownership: viewport changes never repaint Freeze");
			}
			if constexpr (Draw3::kWhiteboardPatternOwnedByFreeze)
			{
				// 在途期间平移样本只合并：不重画、不取消候选、不使内容失效；回执确认后同一回调内画下一帧；
				// 最后以精确落定帧结束，协议回到 Idle。
				SetTheme(0x123B32, 1);
				PublishSample(true, false, 0.0F, 0.0F);
				Settle("in-flight fixture starts from a settled frame");
				const auto contentRevision = backgroundState.ContentRevision();
				const auto pixelsBefore = hiddenBackgroundPixelCount;
				hiddenBackgroundHoldReceipt = true;
				PublishSample(true, true, 50.0F, 0.0F);
				Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }),
					"pan sample draws once and publishes a native candidate");
				const auto firstNative = backgroundSubmission ? backgroundSubmission->nativeRevision : 0;
				const auto firstSerial = backgroundSubmission ? backgroundSubmission->frame.frameSerial : 0;
				for (int n = 1; n <= 40; ++n) { PublishSample(true, true, 50.0F + 11.0F * n, 0.0F); (void)Step(); }
				Check(hiddenBackgroundPixelCount == pixelsBefore + 1 && backgroundSubmission
					&& backgroundSubmission->nativeRevision == firstNative && backgroundNativeRevision.load() == firstNative
					&& backgroundState.ContentRevision() == contentRevision,
					"pan samples during a pending candidate neither redraw nor cancel it nor invalidate content");
				// 落定样本（亚像素偏移）也在途排队；确认后链式再画恰好一帧并精确落在最终相位。
				PublishSample(true, false, 490.1F, 0.0F);
				hiddenBackgroundHoldReceipt = false;
				bool chained = false;
				const auto chainDeadline = std::chrono::steady_clock::now() + 5s;
				while (std::chrono::steady_clock::now() < chainDeadline && !chained)
				{
					const bool had = backgroundSubmission.has_value();
					const auto before = hiddenBackgroundPixelCount;
					(void)Step();
					chained = had && hiddenBackgroundPixelCount == before + 1;
					if (!chained) std::this_thread::sleep_for(1ms);
				}
				Check(chained, "receipt acknowledgement is followed by the next pattern frame in the same callback");
				Check(backgroundSubmission && backgroundSubmission->nativeRevision != firstNative
					&& backgroundSubmission->frame.frameSerial != firstSerial && backgroundState.ContentRevision() == contentRevision,
					"chained frame is a fresh submission without content invalidation");
				Check(Until([&] { return !backgroundSubmission && BackgroundPrepared(103); }), "settled pattern frame completes the receipt chain");
				const auto settled = hiddenBackgroundPixelCount;
				bool idle = true;
				for (unsigned repeat = 0; repeat < 8; ++repeat) idle = idle && Step() == FrameResult::Idle;
				Check(idle && hiddenBackgroundPixelCount == settled && BackgroundPrepared(103)
					&& backgroundState.ContentRevision() == contentRevision, "protocol is Idle with the latest frame prepared and no further pixels");
				const auto finalFrame = Compare(captured, Reference(1.0F, 0x123B32, 1, 490.1, 0.0, true), 8);
				std::fprintf(stderr, "[WhiteboardNative] Freeze settled frame vs reference at the exact final viewport: maxDelta=%d over2=%zu\n",
					finalFrame.first, finalFrame.second);
				Check(finalFrame.first <= kShiftedTolerance, "settled frame sits at the exact final phase");

				// 页身份：更老的 generation、离开白板的样本都不得再画；更新的 generation 立即生效。
				PublishSample(true, false, 900.0F, 0.0F, generation - 1);
				for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
				PublishSample(false, false, 901.0F, 0.0F);
				for (unsigned repeat = 0; repeat < 8; ++repeat) (void)Step();
				Check(hiddenBackgroundPixelCount == settled, "stale-generation and inactive samples never repaint the pattern");
				PublishSample(true, false, 900.0F, 0.0F, generation + 1);
				Settle("a newer generation sample repaints immediately");

				// 纹理类型变化（RGB 不变）在 Freeze 归属下必须退休旧帧；这是内容失效，与相位变化不同。
				const auto oldFrame = backgroundState.Prepared(103);
				Check(oldFrame.has_value(), "fixture holds a prepared frame before the pattern-kind change");
				const bool retired = SetTheme(0x123B32, 2);
				Check(retired && (!oldFrame || !backgroundState.Prepare(*oldFrame)), "pattern-kind change retires the previous frame under Freeze ownership");
				Settle("pattern-kind change redraws as a grid");
				const auto grid = Compare(captured, Reference(1.0F, 0x123B32, 2, 900.0, 0.0, true), 8);
				Check(grid.first <= kShiftedTolerance, "grid redraw after the pattern-kind change matches the reference");
				// 页切换不等 state 线程：真实回执后同一回调须画出新页样本，而不是下一次才更新相位。
				SetTheme(0x123B32, 1);
				PublishSample(true, false, 3.25F, -7.5F);
				Settle("page-turn fixture starts prepared");
				const auto pageRevision = backgroundState.ContentRevision();
				hiddenBackgroundHoldReceipt = true;
				PublishSample(true, true, 17.25F, -7.5F);
				Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }), "page-turn fixture holds an in-flight frame");
				PublishSample(true, false, -41.375F, 27.625F, generation + 1, 1);
				knownPageGeneration.store(generation + 1, std::memory_order_release);
				hiddenBackgroundHoldReceipt = false;
				bool pageChained = false;
				const auto pageDeadline = std::chrono::steady_clock::now() + 5s;
				while (!pageChained && std::chrono::steady_clock::now() < pageDeadline)
				{
					const bool pending = backgroundSubmission.has_value();
					const auto pixels = hiddenBackgroundPixelCount;
					(void)Step();
					pageChained = pending && hiddenBackgroundPixelCount == pixels + 1;
					if (!pageChained) std::this_thread::sleep_for(1ms);
				}
				Check(pageChained && followSample.page == 1 && followSample.generation == generation + 1
					&& followState.drawn == WhiteboardFollow::ResolvePhase(width, height, 1.0F, -41.375, 27.625)
					&& backgroundState.ContentRevision() == pageRevision, "page-turn phase changes in the receipt callback without content invalidation");
				Check(!captured.empty() && captured == Reference(1.0F, 0x123B32, 1, -41.375, 27.625, true, true),
					"page-turn chained pixels exactly match the new-page viewport");
				Check(Until([&] { return !backgroundSubmission && BackgroundPrepared(103); }), "page-turn receipt chain finishes");
				const auto pagePixels = hiddenBackgroundPixelCount;
				const auto pagePhase = followState.drawn;
				PublishSample(true, false, 100.0F, 200.0F, generation, 0);
				for (unsigned repeat = 0; repeat < 8; ++repeat)
					Check(Step() == FrameResult::Idle && hiddenBackgroundPixelCount == pagePixels && followState.drawn == pagePhase,
						"old-generation page sample leaves drawn phase and Idle unchanged");

				const auto beforeTheme = backgroundState.Prepared(103);
				Check(SetTheme(0x172A46, 1) && beforeTheme && !backgroundState.Prepare(*beforeTheme), "RGB theme change retires previous pixels");
				PublishSample(true, false, -41.375F, 27.625F);
				Settle("new RGB theme obtains fresh pixels and receipt");
				Check(!captured.empty() && captured == Reference(1.0F, 0x172A46, 1, -41.375, 27.625, true, true),
					"theme redraw keeps the latest viewport phase pixel-exact");
				SetTheme(0x172A46, 0);
				Settle("Blank theme retires its last pattern pixels once");
				const auto blankPixels = hiddenBackgroundPixelCount;
				const auto blankRevision = backgroundState.ContentRevision();
				for (unsigned repeat = 0; repeat < 8; ++repeat)
				{
					PublishSample(true, true, 300.0F + repeat * 50.0F, -100.0F);
					Check(Step() == FrameResult::Idle && hiddenBackgroundPixelCount == blankPixels
						&& backgroundState.ContentRevision() == blankRevision, "Blank viewport updates never repaint or invalidate content");
				}
				Check(!captured.empty() && captured == Reference(1.0F, 0x172A46, 0, 0.0, 0.0, true, true), "Blank frame contains solid colour only");

			}
			if constexpr (Draw3::kWhiteboardPatternOwnedByFreeze)
			{
				// 实测（本机 WARP，屏外 Freeze + 真实 owner/ULW；不含 DWM 取用与扫描输出）：生产回调逐次更新的耗时。
				// 60 Hz 样本经生产回调合并；速率、滞后和链式帧只作离屏测量，不限制机器速度。
				LARGE_INTEGER frequency{};
				QueryPerformanceFrequency(&frequency);
				const auto Ms = [&](LONGLONG ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency.QuadPart); };
				// 60 Hz 发布脚本：慢机器按实际经过的采样时刻合并最新值；耗时只打印，30 秒只用于挂起检测。
				auto RunFollowStream = [&](float scale, int pattern)
				{
					hiddenBackgroundTarget.scale = scale;
					SetTheme(0x123B32, pattern);
					PublishSample(true, false, 0.125F, -0.25F);
					Settle("60 Hz follow stream starts from a prepared frame");
					const auto revision = backgroundState.ContentRevision();
					double x = 0.125, y = -0.25;
					for (int segment = 0; segment < 3; ++segment)
					{
						const auto started = std::chrono::steady_clock::now();
						const auto deadline = started + 30s;
						const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
							std::chrono::duration<double>(1.0 / 60.0));
						auto nextSample = started;
						int samples = 0;
						std::uint64_t callbacks = 0, chainedFrames = 0, maxLag = 0;
						const auto firstPixel = hiddenBackgroundPixelCount;
						bool contentStable = true, candidateStable = true;
						auto StreamStep = [&]
						{
							const auto published = viewportSerial.load(std::memory_order_acquire);
							if (published >= followState.drawnSerial)
								maxLag = (std::max)(maxLag, published - followState.drawnSerial);
							const auto pending = backgroundSubmission;
							const auto pixels = hiddenBackgroundPixelCount;
							const auto result = Step();
							++callbacks;
							if (pending && hiddenBackgroundPixelCount > pixels) ++chainedFrames;
							// 没有新像素且仍保留同一帧时，原生候选身份不得因平移变掉。
							if (pending && backgroundSubmission && hiddenBackgroundPixelCount == pixels)
								candidateStable = candidateStable && pending->frame.frameSerial == backgroundSubmission->frame.frameSerial
									&& pending->nativeRevision == backgroundSubmission->nativeRevision;
							contentStable = contentStable && backgroundState.ContentRevision() == revision;
							return result;
						};
						while (samples < 30 && std::chrono::steady_clock::now() < deadline)
						{
							const auto now = std::chrono::steady_clock::now();
							if (now < nextSample) { std::this_thread::sleep_for(1ms); continue; }
							while (samples < 30 && nextSample <= now)
							{
								++samples;
								const double speed = segment == 0 ? 3000.0 * samples / 30.0 : segment == 1 ? 3000.0 : 12000.0;
								x += speed * scale / 60.0;
								PublishSample(true, true, static_cast<float>(x), static_cast<float>(y));
								nextSample += period;
							}
							(void)StreamStep();
						}
						Check(samples == 30, "60 Hz follow stream reaches every scripted sample before hang deadline");
						// 最后一帧保留非整数物理像素，避免速度步长刚好整格时漏检落定相位。
						const float finalX = static_cast<float>(x + 0.375), finalY = static_cast<float>(y - 0.625);
						PublishSample(true, false, finalX, finalY);
						bool idle = false;
						while (std::chrono::steady_clock::now() < deadline)
						{
							idle = StreamStep() == FrameResult::Idle && !backgroundSubmission && BackgroundPrepared(103);
							if (idle) break;
							std::this_thread::sleep_for(1ms);
						}
						Check(idle && contentStable && candidateStable, "pan-only stream preserves content/candidate identity and returns to Idle");
						// 统计到落定回执为止，参考栅格/读回与后续 Idle 探针不计入回调速率。
						const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
						const auto expected = WhiteboardFollow::ResolvePhase(width, height, scale, finalX, finalY);
						Check(followState.drawnValid && followState.drawn == expected, "settled stream phase equals ResolvePhase exactly");
						const auto exact = Reference(scale, 0x123B32, pattern, finalX, finalY, true, true);
						Check(!captured.empty() && captured == exact, "settled stream pixels equal the final-viewport reference including edges");
						const auto settledPixels = hiddenBackgroundPixelCount;
						for (unsigned repeat = 0; repeat < 8; ++repeat)
							Check(Step() == FrameResult::Idle && hiddenBackgroundPixelCount == settledPixels, "settled stream stays Idle without repaint");
						std::fprintf(stderr, "[WhiteboardNative] M1 offscreen scale=%.1f pattern=%d segment=%s samples=%d callbacks/s=%.2f pixels/s=%.2f; M2 max-lag-samples=%llu chained-frames=%llu (paced 60 Hz, no DWM)\n",
							scale, pattern, segment == 0 ? "accelerating-to-3000" : segment == 1 ? "3000-DIP/s" : "12000-DIP/s", samples,
							seconds > 0.0 ? callbacks / seconds : 0.0, seconds > 0.0 ? (hiddenBackgroundPixelCount - firstPixel) / seconds : 0.0,
							static_cast<unsigned long long>(maxLag), static_cast<unsigned long long>(chainedFrames));
						x = finalX; y = finalY;
					}
				};
				for (const float scale : { 1.0F, 1.5F, 2.0F })
					for (const int pattern : { 1, 2 }) RunFollowStream(scale, pattern);
				hiddenBackgroundTarget.scale = 1.0F;
				// 实测：同一 D2D 管线在 3840x2160 的单次更新（不含 ULW/DWM）。BeginDraw -> 绘制 -> GetDC(COPY) -> ReleaseDC -> EndDraw。
				ComPtr<ID2D1Bitmap1> bigTarget;
				const auto bigSize = D2D1::SizeU(3840, 2160);
				if (reference && SUCCEEDED(reference->CreateBitmap(bigSize, nullptr, 0, D2D1::BitmapProperties1(
					D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
					D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0F, 96.0F), &bigTarget)))
				{
					ComPtr<ID2D1GdiInteropRenderTarget> bigGdi;
					reference->SetTarget(bigTarget.Get());
					if (SUCCEEDED(reference.As(&bigGdi)))
						for (const float scale : { 1.0F, 1.5F, 2.0F })
							for (const int pattern : { 1, 2 })
							{
								std::array<double, 7> draw{}, getDc{}, endDraw{}, total{};
								for (std::size_t round = 0; round < draw.size(); ++round)
								{
									const auto phase = WhiteboardFollow::ResolvePhase(3840.0, 2160.0, scale, 7.3 * round + 0.37, 3.1 * round);
									LARGE_INTEGER t0{}, t1{}, t2{}, t3{}, t4{};
									QueryPerformanceCounter(&t0);
									reference->BeginDraw();
									reference->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
									reference->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
									const D2D1_RECT_F rect = D2D1::RectF(0.0F, 0.0F, 3840.0F / scale, 2160.0F / scale);
									WhiteboardVisual::DrawThemeSolid(reference.Get(), rect, 0x123B32);
									WhiteboardVisual::DrawThemePatternPhase(reference.Get(), rect, 0x123B32, pattern, phase.xDip, phase.yDip);
									QueryPerformanceCounter(&t1);
									HDC dc = nullptr;
									const HRESULT got = bigGdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc);
									QueryPerformanceCounter(&t2);
									const RECT none{};
									if (SUCCEEDED(got)) (void)bigGdi->ReleaseDC(&none);
									QueryPerformanceCounter(&t3);
									(void)reference->EndDraw();
									QueryPerformanceCounter(&t4);
									draw[round] = Ms(t1.QuadPart - t0.QuadPart); getDc[round] = Ms(t2.QuadPart - t1.QuadPart);
									endDraw[round] = Ms(t4.QuadPart - t3.QuadPart); total[round] = Ms(t4.QuadPart - t0.QuadPart);
								}
								for (auto* series : { &draw, &getDc, &endDraw, &total }) std::sort(series->begin() + 1, series->end()); // 丢弃首轮预热
								std::fprintf(stderr, "[WhiteboardNative] measured D2D+WARP 3840x2160 zoom=%.0f%% %s per update (excl. ULW/DWM; median of 6 after warm-up): record=%.2f ms GetDC(COPY)=%.2f ms EndDraw=%.2f ms total=%.2f ms\n",
									scale * 100.0F, pattern == 1 ? "dots" : "grid", draw[4], getDc[4], endDraw[4], total[4]);
							}
					reference->SetTarget(nullptr);
				}
			}
			// FOLLOW-TESTS-END
			hiddenBackgroundCapture = nullptr;
		}
		Check(backgroundState.Publish(true, 104), "device-generation request is accepted");
		hiddenBackgroundHoldReceipt = true;
		Check(Until([&] { return backgroundSubmission && backgroundSubmission->nativeRevision; }), "pre-device frame is pending");
		const auto oldDeviceFrame = backgroundSubmission ? backgroundSubmission->frame : WhiteboardDetail::BackgroundState::Frame{};
		hiddenBackgroundHoldReceipt = false;
		Check(Until([&] { return BackgroundPrepared(104); }, epoch.generation + 1)
			&& !backgroundState.FrameCurrent(oldDeviceFrame), "device generation discards old pixel candidate before acknowledging rebuilt resources");
		Check(backgroundState.Publish(false, 105), "final cleanup request is accepted");
		Check(Until([&] { return BackgroundMatchesActive(false, 105); }), "final native Hide retires shared Freeze ownership");
		Check(backgroundScene.Renderer().GetTargetBitmapSize().width == 0 && !backgroundScene.DeviceContext(),
			"final Hide returns all background target resources");
		CancelBackgroundCandidate(); DiscardBackgroundSubmission();
		(void)service.Hide(WindowRole::Freeze);
		backgroundScene.Reset(); backgroundState.Reset();
		configuredBackgroundScreen = {}; configuredBackgroundScale = 0;
		renderedBackgroundTarget.store({});
		{ std::scoped_lock pageLock(pageStateMutex); themeRgb = 0x123B32; themePattern = 0; }
		patternKindMirror.store(0, std::memory_order_release);
		knownPageGeneration.store(0, std::memory_order_release);
		PublishCanvasViewport({}); // 通道复位为“离开白板”
		followState = {}; followChainRequested = false; followReadContended = false;
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
		followRequestPending.store(false, std::memory_order_release);
		patternKindMirror.store(0, std::memory_order_release);
		knownPageGeneration.store(0, std::memory_order_release);
		followState = {};
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
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
		followProfiler.Flush();
#endif
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
			// 归属 Freeze：底色或纹理类型任一变化都退休像素（Freeze 自己画纹理）；归属 Draw3：Freeze 只画纯色，
			// 仅底色改变才失效，纹理类型只更新分页预览快照，避免整屏重绘并取消候选。视口相位变化从不走这里。
			themeChanged = WhiteboardFollow::ThemeChangeRetiresPixels(themeRgb, themePattern, rgb, pattern,
				Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze);
			themeRgb = rgb; themePattern = pattern;
			unsavedPages = unsaved; pageGeneration = generation;
			patternKindMirror.store(pattern, std::memory_order_release);
			// 页快照由 state 线程顺序发布；最新者即已知代次（控制器重建后代次从头计数，也不会被旧最大值卡死）。
			knownPageGeneration.store(generation, std::memory_order_release);
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

	void PublishCanvasViewport(const Inkeys::Drawing::Draw3::WhiteboardViewportSample& sample) noexcept
	{
		if constexpr (!Inkeys::Drawing::Draw3::kWhiteboardPatternOwnedByFreeze)
		{
			(void)sample; // Draw3 归属：视口已在画布内合成纹理，Freeze 不需要它。
		}
		else
		{
			if (!initialized.load(std::memory_order_acquire)) return;
			auto stamped = sample;
			stamped.serial = viewportSerial.fetch_add(1, std::memory_order_relaxed) + 1;
#if defined(INKEYS_WHITEBOARD_PATTERN_PROFILE)
			if (followProfiler.enabled)
			{
				LARGE_INTEGER now{};
				QueryPerformanceCounter(&now);
				stamped.qpc = now.QuadPart;
			}
#endif
			viewportChannel.Write(stamped);
			if (!WhiteboardFollow::ShouldWakeFreeze(patternKindMirror.load(std::memory_order_acquire),
				active.load(std::memory_order_acquire), sample.active)) return;
			// 已有未消费的请求就不再 SetEvent：Freeze 回调开头先清标记再读通道，最新样本不会丢。
			if (!followRequestPending.exchange(true, std::memory_order_acq_rel))
				Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
		}
	}

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
