#include "ScrollFollow.h"
#include "../CV/VisualMotionTracker.h"
#include "../Graphics/FreezeCapture.h"
#include "../Drawing/Draw3/Draw3.Product.h"
#include "../Drawing/Draw3/Draw3.DiagnosticOutput.h"
#include "../../IdtState.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#include <vector>

import Inkeys.UI.Freeze;
import Inkeys.UI.Bar;

namespace Inkeys::Business::ScrollFollow
{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	namespace LiveTrace
	{
		namespace { std::atomic<std::shared_ptr<Observer>> observer; }
		void Install(std::shared_ptr<Observer> value) noexcept { observer.store(std::move(value), std::memory_order_release); }
	}
#endif
	namespace
	{
		namespace Draw = Inkeys::Drawing::Draw3;
		namespace Capture = Inkeys::Graphics::FreezeCapture;
		using Clock = std::chrono::steady_clock;
		std::atomic_bool shutdownRequested = false;
		struct Service;
		std::atomic<Service*> existingService = nullptr;
		struct Context
		{
			std::shared_ptr<Draw::Follow::Channel> channel;
			CV::VisualMotionTracker tracker; // 仅原 Mag 回调串行使用。
			std::atomic_bool initialized = false, tracking = false, paused = false;
			std::atomic_uint64_t captureSession = 0;
			std::uint64_t session = 0;
			std::mutex resultMutex;
			CV::MotionResult result{};
		};
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		// 仅内部CLI替换外部边界；真实Tick/Frame/Tracker不替换，不创建HWND或控制线程。
		struct TestPorts
		{
			std::shared_ptr<Draw::Follow::Channel> channel = std::make_shared<Draw::Follow::Channel>();
			Capture::Snapshot capture{};
			Capture::FrameCallback callback;
			Draw::Shortcuts::Scope scope{};
			Clock::time_point now = Clock::time_point{} + std::chrono::seconds(100);
			std::uint64_t projection = 0, acknowledged = ~std::uint64_t{0}, acknowledgedRevision = 0, nextCapture = 11;
			unsigned holds = 0, starts = 0, stops = 0;
			bool available = true, beginAllowed = true;
			Capture::StartStatus startStatus = Capture::StartStatus::Started;
		};
		thread_local TestPorts* testPorts = nullptr;
		thread_local Service* testService = nullptr;
#endif
		Clock::time_point Now() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return testPorts->now;
#endif
			return Clock::now();
		}
		struct HostPort
		{
			std::shared_ptr<Draw::Follow::Channel> FollowChannel() const noexcept
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (testPorts) return testPorts->channel;
#endif
				return Draw::ProductHost().FollowChannel();
			}
			bool BeginFollow(std::uint64_t session, const std::shared_ptr<Draw::Follow::Channel>& channel) const noexcept
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (testPorts) return testPorts->beginAllowed && !testPorts->holds && channel == testPorts->channel && channel->Begin(session);
#endif
				return Draw::ProductHost().BeginFollow(session, channel);
			}
			void EndFollow(Draw::Follow::Disposition disposition, const std::shared_ptr<Draw::Follow::Channel>& channel = {}) const noexcept
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (testPorts) { if (!channel || channel == testPorts->channel) { testPorts->channel->Seal(disposition); testPorts->channel->OwnerEnd(testPorts->channel->Read().session); } return; }
#endif
				Draw::ProductHost().EndFollow(disposition, channel);
			}
			bool HoldFollowAction(bool hold, const std::shared_ptr<Draw::Follow::Channel>& channel) const noexcept
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (testPorts) { if (!channel || channel != testPorts->channel) return false; if (hold) ++testPorts->holds; else if (testPorts->holds) --testPorts->holds; return true; }
#endif
				return Draw::ProductHost().HoldFollowAction(hold, channel);
			}
		};
		Draw::Shortcuts::Scope CurrentScope() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return testPorts->scope;
#endif
			return Draw::CaptureProductShortcutContext().scope;
		}
		Capture::Snapshot CaptureSnapshot() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return testPorts->capture;
#endif
			return Capture::GetSnapshot();
		}
		Capture::StartResult StartCapture(Capture::FrameCallback callback) noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) { ++testPorts->starts; if (testPorts->startStatus != Capture::StartStatus::Started) return {testPorts->startStatus, 0}; testPorts->callback = std::move(callback); testPorts->capture = {}; testPorts->capture.ready = testPorts->capture.publishing = testPorts->capture.occupied = true; testPorts->capture.session = testPorts->nextCapture++; return {Capture::StartStatus::Started, testPorts->capture.session}; }
#endif
			return Capture::Start(std::move(callback));
		}
		void StopCapture(Capture::StopReason reason = Capture::StopReason::Requested) noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) { ++testPorts->stops; testPorts->capture.publishing = false; testPorts->capture.reason = reason; return; }
#endif
			Capture::Stop(reason);
		}
		void WakeHost() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return;
#endif
			Draw::ProductHost().WakeFollow();
		}
		std::uint64_t ProjectionEpoch() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return testPorts->projection;
#endif
			return ScrollFollowProjectionEpoch();
		}
		std::uint64_t Project(bool enabled) noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return testPorts->projection = ((testPorts->projection & ~std::uint64_t{1}) + 2) | (enabled ? 1u : 0u);
#endif
			return RequestScrollFollowProjection(enabled);
		}
		bool Acknowledged(std::uint64_t epoch, std::uint64_t revision = 0) noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return testPorts->acknowledged == epoch && testPorts->acknowledgedRevision >= revision;
#endif
			return ScrollFollowPresentationAcknowledged(epoch, revision);
		}
		void RefreshPresentation() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return;
#endif
			RequestScrollFollowPresentationRefresh();
		}
		void RefreshUi() noexcept
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testPorts) return;
#endif
			// Host可先于Bar资源就绪；沿原刷新入口保留唤醒并保护未初始化的文字对象。
			Inkeys::UI::Bar::RefreshToolState();
		}
		bool Capability() noexcept
		{
			// 可选功能动态查询版本；不增加 Win7 缺失入口的硬导入。
			using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
			const auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
			OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
			BOOL wow64 = FALSE;
			return fn && fn(&version) == 0 && (version.dwMajorVersion > 6 ||
				(version.dwMajorVersion == 6 && version.dwMinorVersion >= 3)) &&
				IsWow64Process(GetCurrentProcess(), &wow64) && !wow64;
		}
		std::atomic_bool diagnostics = false, verboseFrames = false;
		void Log(const char* event, std::uint64_t session) noexcept
		{
			if (diagnostics.load()) Draw::DiagnosticOutput::Printf(Draw::DiagnosticOutput::Stream::Error,
				"[ScrollFollow] event=%s session=%llu\n", event, static_cast<unsigned long long>(session));
		}
		void Frame(const std::shared_ptr<Context>& context, const Capture::Frame& frame) noexcept
		{
			const auto owner = context->channel->Read();
			if (shutdownRequested.load(std::memory_order_acquire) || !owner.publishing || owner.session != context->session ||
				frame.session != context->captureSession.load(std::memory_order_acquire) || context->paused.load()) return;
			const int left = std::max(0, frame.geometry.workArea.left - frame.geometry.sourceRect.left);
			const int top = std::max(0, frame.geometry.workArea.top - frame.geometry.sourceRect.top);
			const int right = std::min(frame.width, frame.geometry.workArea.right - frame.geometry.sourceRect.left);
			const int bottom = std::min(frame.height, frame.geometry.workArea.bottom - frame.geometry.sourceRect.top);
			if (right <= left || bottom <= top) { context->paused = true; return; }
			const auto offset = static_cast<std::size_t>(top) * frame.strideBytes + static_cast<std::size_t>(left) * 4;
			CV::MotionFrame input;
			input.image = { reinterpret_cast<const std::uint8_t*>(frame.bgra.data()) + offset,
				frame.bgra.size_bytes() - offset, frame.strideBytes, right - left, bottom - top, CV::MotionPixelFormat::Bgra32 };
			input.timestampSeconds = std::chrono::duration<double>(frame.timestamp.time_since_epoch()).count();
			input.sequence = frame.sequence;
			CV::MotionResult result;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			auto operation = LiveTrace::Operation::AwaitTracking;
#endif
			if (!context->initialized.load())
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				operation = LiveTrace::Operation::Initialize;
#endif
				result = context->tracker.Initialize(input);
				if (result.accepted) context->initialized.store(true);
			}
			else if (context->tracking.load())
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				operation = LiveTrace::Operation::Process;
#endif
				result = context->tracker.Process(input);
				if (result.accepted)
				{
					Draw::Follow::Target target{ context->session, frame.sequence, owner.identity,
						result.total.x * frame.sourcePixelsPerPixelX, result.total.y * frame.sourcePixelsPerPixelY };
					if (context->channel->Publish(target)) WakeHost();
				}
				if (result.state == CV::MotionState::Paused) context->paused.store(true);
			}
			{ std::scoped_lock lock(context->resultMutex); context->result = result; }
			// 原逐帧诊断只追加现成特征身份统计；relay代次本身不能证明初始点已全部离屏。
			if (verboseFrames.load()) Draw::DiagnosticOutput::Printf(Draw::DiagnosticOutput::Stream::Error,
				"[ScrollFollow] session=%llu capture_session=%llu frame=%llu accepted=%d reason=%s total_px=(%.4f,%.4f) confidence=%.4f inliers=%d fb_p95=%.4f residual_p95=%.4f coverage=(%.3f,%.3f) relay=%llu initial_survivors=%d added=%d retained=%d retired=%d pw_us=%lld attempted=%d forward=%d backward=%d fb_valid=%d point_inlier_ratio=%.6f spatial_inlier_ratio=%.6f survival_ratio=%.6f reference_cells=%d occupied_cells=%d inlier_cells=%d interior_cells=%d runner_up_ratio=%.6f affine_variation=%.6f affine_residual=%.6f reference_sequence=%llu consecutive_rejects=%d\n",
				static_cast<unsigned long long>(context->session), static_cast<unsigned long long>(frame.session),
				static_cast<unsigned long long>(frame.sequence), result.accepted, CV::MotionReasonName(result.reason), result.total.x, result.total.y, result.confidence, result.statistics.inliers,
				result.statistics.fbP95, result.statistics.residualP95, result.statistics.coverageX, result.statistics.coverageY,
				static_cast<unsigned long long>(result.statistics.relayGeneration), result.statistics.initialSurvivors, result.statistics.added, result.statistics.retained, result.statistics.retired,
				static_cast<long long>(frame.statistics.lastReadDuration.count()),
				result.statistics.attempted, result.statistics.forward, result.statistics.backward, result.statistics.fbValid,
				result.statistics.pointInlierRatio, result.statistics.spatialInlierRatio, result.statistics.survivalRatio,
				result.statistics.referenceCells, result.statistics.occupiedCells, result.statistics.inlierCells, result.statistics.interiorCells,
				result.statistics.runnerUpRatio, result.statistics.affineVariation, result.statistics.affineResidual,
				static_cast<unsigned long long>(result.statistics.referenceSequence), result.statistics.consecutiveRejects);
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (auto observer = LiveTrace::observer.load(std::memory_order_acquire))
				observer->Observe({context->session, frame.session, frame.geometry.displayGeneration,
					frame.geometry.sourceRect.left, frame.geometry.sourceRect.top, frame.width, frame.height,
					frame.geometry.sourceRect.left + left, frame.geometry.sourceRect.top + top,
					frame.geometry.sourceRect.right, frame.geometry.sourceRect.bottom,
					frame.geometry.workArea.left, frame.geometry.workArea.top, frame.geometry.workArea.right, frame.geometry.workArea.bottom,
					frame.sourcePixelsPerPixelX, frame.sourcePixelsPerPixelY}, operation, input, result);
#endif
		}
		struct Action { std::uint64_t id = 0; bool completed = false, gated = false, retired = false; std::uint64_t publishedRevision = 0; Draw::Shortcuts::Scope scope{}; std::shared_ptr<Draw::Follow::Channel> channel; };
		struct Service
		{
			std::mutex mutex;
			std::condition_variable wake;
			State state = State::Off;
			std::atomic<State> visibleState = State::Off;
			std::atomic_uint64_t visibleSession = 0;
			bool toggle = false, stop = false, shutdown = false, retired = false, serviceHold = false;
			std::uint64_t nextSession = 1, nextAction = 1, projection = 0;
			std::array<Action, 64> actions{};
			std::shared_ptr<Context> context;
			Clock::time_point started{}, nextStart{}, lastStatistics{};
			Capture::Statistics statistics{};
			EndReason stopReason = EndReason::Toggle;
			bool captureStarted = false, projected = false, restoring = false, cleanupLogged = false;
			std::jthread worker;
			explicit Service(bool runWorker = true) { if (runWorker) { worker = std::jthread([this](std::stop_token token) { Run(token); }); existingService.store(this, std::memory_order_release); } }
			~Service() { if (existingService.load(std::memory_order_acquire) == this) existingService.store(nullptr, std::memory_order_release); worker.request_stop(); wake.notify_all(); }
			void Change(State next) noexcept
			{
				if (state == next) return;
				state = next; visibleState.store(next); visibleSession.store(context ? context->session : 0);
				Log(next == State::Off ? "off" : next == State::Initializing ? "initializing" :
					next == State::Tracking ? "tracking" : next == State::Paused ? "paused" : "stopping", context ? context->session : 0);
				RefreshUi();
			}
			void Run(std::stop_token token) noexcept
			{
				while (!token.stop_requested())
				{
					std::unique_lock lock(mutex);
					wake.wait_for(lock, std::chrono::milliseconds(16));
					if (token.stop_requested()) break;
					try { Tick(); if (shutdown) return; } catch (...) { stop = true; Log("control-failure", context ? context->session : 0); }
				}
			}
			void Tick()
			{
				const auto now = Now();
				const HostPort host;
				if (shutdownRequested.load(std::memory_order_acquire)) shutdown = true;
				if (shutdown)
				{
					StopCapture(Capture::StopReason::Shutdown);
					host.EndFollow(Draw::Follow::Disposition::Shutdown, context ? context->channel : nullptr);
					if (context) { context->tracking = false; context->paused = true; }
					toggle = false; stop = false; Change(State::Off); return;
				}
				if (toggle)
				{
					toggle = false;
					if (state != State::Off) stop = true;
					else if (std::none_of(actions.begin(), actions.end(), [](const Action& a) { return a.id != 0; }) && Available())
					{
						auto candidate = std::make_shared<Context>(); candidate->channel = host.FollowChannel();
						candidate->session = nextSession++;
						if (host.BeginFollow(candidate->session, candidate->channel))
						{
							context = std::move(candidate); serviceHold = host.HoldFollowAction(true, context->channel);
							started = now; nextStart = now; captureStarted = projected = restoring = cleanupLogged = false;
							retired = false; stop = false; statistics = {}; Change(State::Initializing);
						}
					}
				}
				if (!context || state == State::Off)
				{
					if (context && captureStarted && !cleanupLogged)
					{
						const auto drained = CaptureSnapshot();
						if (drained.session == context->captureSession.load() && !drained.occupied && !drained.inFlight)
						{ cleanupLogged = true; Log("capture-drained", context->session); }
					}
					for (auto& action : actions) if (action.id && action.completed && (action.scope != CurrentScope() || Acknowledged(ProjectionEpoch(), action.publishedRevision)))
					{ const auto channel = action.channel; const bool gated = action.gated; action = {}; if (gated) host.HoldFollowAction(false, channel); }
					return;
				}
				const auto owner = context->channel->Read();
				const auto capture = CaptureSnapshot();
				const bool ownsCapture = Draw::Follow::OwnsCaptureSession(captureStarted, context->captureSession.load(), capture.session);
				if (owner.endQueued || owner.phase == Draw::Follow::Phase::Failed || owner.phase == Draw::Follow::Phase::Retired) stop = true;
				if (owner.disposition != Draw::Follow::Disposition::Restore || (ownsCapture && (capture.reason == Capture::StopReason::GeometryChanged ||
					capture.reason == Capture::StopReason::FreezeActive || capture.reason == Capture::StopReason::Shutdown || capture.reason == Capture::StopReason::Unavailable)))
				{
					retired = true; stop = true;
					for (auto& action : actions) if (action.id) action.retired = true;
					host.EndFollow(Draw::Follow::Disposition::RetireScene, context->channel);
				}
				if (state == State::Initializing && !stop)
				{
					if (owner.phase == Draw::Follow::Phase::Active && !captureStarted && now >= nextStart)
					{
						const auto result = StartCapture([context = context](const Capture::Frame& frame) { Frame(context, frame); });
						captureStarted = result.status == Capture::StartStatus::Started;
						if (captureStarted) context->captureSession.store(result.session, std::memory_order_release);
						nextStart = now + std::chrono::milliseconds(80);
					}
					if (context->initialized.load() && !projected)
					{ projection = Project(true); projected = true; }
					if (projected && Acknowledged(projection))
					{ context->tracking = true; statistics = ownsCapture ? capture.statistics : Capture::Statistics{}; Change(State::Tracking); }
					if (now - started > std::chrono::seconds(5)) { Log("initialization-timeout", context->session); stop = true; }
				}
				if ((state == State::Tracking || state == State::Paused) && !stop && ownsCapture)
				{
					// Capture 的无效缓冲/黑图/低信息都是严重捕获失效；普通三拒绝完全由Tracker负责。
					if (capture.statistics.rejected != statistics.rejected) { context->paused = true; Log("capture-invalid", context->session); }
					if (capture.statistics.apiFailures != statistics.apiFailures || !capture.publishing || owner.clamped) { context->paused = true; if (state != State::Paused) Log(owner.clamped ? "viewport-clamped" : "capture-unavailable", context->session); }
					statistics = capture.statistics;
					if (context->paused.load()) { context->tracking = false; StopCapture(); Change(State::Paused); }
				}
				if (stop && state != State::Stopping)
				{
					Log(stopReason == EndReason::ToolIntent ? "end-tool-intent" : stopReason == EndReason::Freeze ? "end-freeze" : stopReason == EndReason::Workspace ? "end-workspace" : stopReason == EndReason::CanvasCommand ? "end-canvas-command" : "end-toggle", context->session);
					context->tracking = false; host.EndFollow(retired ? Draw::Follow::Disposition::RetireScene : Draw::Follow::Disposition::Restore, context->channel);
					StopCapture(); stop = false; Change(State::Stopping);
				}
				if (state == State::Stopping)
				{
					const auto end = context->channel->Read();
					if (!restoring && (end.phase == Draw::Follow::Phase::Presented || end.phase == Draw::Follow::Phase::Failed || end.phase == Draw::Follow::Phase::Retired || retired))
					{ projection = Project(false); restoring = true; }
					if (restoring && (retired || Acknowledged(projection)))
					{
						context->channel->Handoff(context->session); WakeHost();
						if (retired || !context->channel->Blocked())
						{
							if (serviceHold) { host.HoldFollowAction(false, context->channel); serviceHold = false; }
							stop = false; Change(State::Off);
						}
					}
				}
				if (diagnostics.load() && now - lastStatistics >= std::chrono::seconds(1))
				{
					lastStatistics = now;
					CV::MotionResult motion; { std::scoped_lock resultLock(context->resultMutex); motion = context->result; }
					Draw::DiagnosticOutput::Printf(Draw::DiagnosticOutput::Stream::Error,
						"[ScrollFollow] stats session=%llu capture=%llu rejected=%llu api_failures=%llu owner_seq=%llu viewport_px=(%.3f,%.3f) reason=%s confidence=%.3f tracked=%d inliers=%d fb_p95=%.3f residual_p95=%.3f coverage=(%.3f,%.3f) relay=%llu pw_us=%lld occupied=%d inflight=%d clamp=%d\n",
						static_cast<unsigned long long>(context->session), static_cast<unsigned long long>(capture.statistics.accepted),
						static_cast<unsigned long long>(capture.statistics.rejected), static_cast<unsigned long long>(capture.statistics.apiFailures),
						static_cast<unsigned long long>(owner.applied), owner.viewportX, owner.viewportY, CV::MotionReasonName(motion.reason), motion.confidence,
						motion.statistics.fbValid, motion.statistics.inliers, motion.statistics.fbP95, motion.statistics.residualP95,
						motion.statistics.coverageX, motion.statistics.coverageY, static_cast<unsigned long long>(motion.statistics.relayGeneration),
						static_cast<long long>(capture.statistics.lastReadDuration.count()), capture.occupied, capture.inFlight, owner.clamped);
				}
			}
		};
		Service& Get() {
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (testService) return *testService;
#endif
			static Service service; return service;
		}
	}
	bool Available() noexcept
	{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		if (testPorts) return !shutdownRequested.load() && testPorts->available && !testPorts->capture.occupied;
#endif
		const auto capture = CaptureSnapshot();
		static const bool capable = Capability();
		return !shutdownRequested.load(std::memory_order_acquire) && capable && Draw::ProductRunning() && capture.ready && !capture.occupied && !Inkeys::UI::Freeze::IsActive() &&
			Draw::ProductHost().ProductBridge().Snapshot().workspace == Draw::Bridge::Workspace::Desktop;
	}
	Snapshot GetSnapshot() noexcept { auto& s = Get(); return {s.visibleState.load(), s.visibleSession.load(), Available()}; }
	void RequestToggle() noexcept { if (shutdownRequested.load()) return; auto& s = Get(); {std::scoped_lock lock(s.mutex); if (!s.shutdown) s.toggle = !s.toggle;} s.wake.notify_all(); }
	void RequestStop(EndReason reason) noexcept
	{
		auto& s = Get(); {std::scoped_lock lock(s.mutex); s.toggle = false; s.stop = true; s.stopReason = reason;
			if (s.context && s.state != State::Off) HostPort{}.EndFollow(Draw::Follow::Disposition::Restore, s.context->channel);
			StopCapture(); } s.wake.notify_all();
	}
	ActionPreparation PrepareAction(EndReason reason) noexcept
	{
		if (shutdownRequested.load()) return {{}, ActionState::Retired};
		auto& s = Get(); std::scoped_lock lock(s.mutex);
		if (s.shutdown || (s.retired && s.state != State::Off)) return {{}, ActionState::Retired};
		for (auto& action : s.actions) if (!action.id)
		{
			action = {s.nextAction++, false, false}; action.scope = CurrentScope(); action.channel = s.context ? s.context->channel : HostPort{}.FollowChannel(); if (s.state != State::Off) action.gated = HostPort{}.HoldFollowAction(true, action.channel);
			if (s.state != State::Off) { s.stop = true; s.stopReason = reason; HostPort{}.EndFollow(Draw::Follow::Disposition::Restore, s.context->channel); StopCapture(); s.wake.notify_all(); }
			return {{action.id}, s.state == State::Off ? ActionState::Ready : ActionState::Pending};
		}
		return {};
	}
	ActionState PollAction(ActionTicket ticket) noexcept
	{
		if (shutdownRequested.load(std::memory_order_acquire)) return ActionState::Retired;
		auto& s = Get(); std::scoped_lock lock(s.mutex);
		if (s.shutdown || (s.retired && s.state != State::Off)) return ActionState::Retired;
		for (const auto& a : s.actions) if (a.id == ticket.id && ticket.id)
			return a.retired || a.scope != CurrentScope() ? ActionState::Retired : s.state == State::Off ? ActionState::Ready : ActionState::Pending;
		return ActionState::Failed;
	}
	void CompleteAction(ActionTicket ticket, std::uint64_t publishedStateRevision) noexcept
	{
		auto& s = Get(); std::scoped_lock lock(s.mutex);
		for (auto& a : s.actions) if (a.id == ticket.id && ticket.id) { a.completed = true; a.publishedRevision = publishedStateRevision; RefreshPresentation(); }
		s.wake.notify_all();
	}
	void CancelAction(ActionTicket ticket) noexcept
	{
		auto& s = Get(); std::scoped_lock lock(s.mutex);
		for (auto& a : s.actions) if (a.id == ticket.id && ticket.id) { const auto channel = a.channel; const bool gated = a.gated; a = {}; if (gated) HostPort{}.HoldFollowAction(false, channel); }
	}
	void RetireScene(EndReason reason) noexcept
	{
		auto& s = Get(); {std::scoped_lock lock(s.mutex); s.toggle = false; for (auto& action : s.actions) if (action.id) action.retired = true; if (s.state != State::Off) {s.retired = true; s.stop = true; s.stopReason = reason;
			HostPort{}.EndFollow(Draw::Follow::Disposition::RetireScene, s.context->channel); } StopCapture(); } s.wake.notify_all();
	}
	void RequestShutdownNoWait() noexcept
	{
		shutdownRequested.store(true, std::memory_order_release);
		if (auto* service = existingService.load(std::memory_order_acquire)) service->wake.notify_all();
	}
	void RequestShutdown() noexcept
	{
		RequestShutdownNoWait();
		HostPort{}.EndFollow(Draw::Follow::Disposition::Shutdown);
		StopCapture(Capture::StopReason::Shutdown);
	}
	void SetDiagnostics(bool enabled, bool verbose) noexcept { diagnostics = enabled; verboseFrames = enabled && verbose; }

	int RunScrollFollowControlTests() noexcept
	{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		int failures = 0;
		const auto check = [&](bool condition, const char* label)
		{ if (!condition) { ++failures; std::fprintf(stderr, "[ScrollFollowControl] FAIL %s\n", label); } };
		// 本接缝只在当前CLI线程生效；没有替换普通进程绑定、启动Host、窗口或控制worker。
		struct Environment
		{
			TestPorts ports;
			Service service{false};
			bool previousShutdown = shutdownRequested.exchange(false);
			Environment() { testPorts = &ports; testService = &service; ports.scope.hostGeneration = 1; ports.scope.pageEpoch = 1; }
			~Environment() { testService = nullptr; testPorts = nullptr; shutdownRequested = previousShutdown; }
			void Begin()
			{
				RequestToggle(); service.Tick();
				Draw::Follow::Identity identity;
				identity.host = ports.scope.hostGeneration; identity.scene = 7; identity.device = 3;
				identity.workspace[0] = 13; identity.page[0] = 17; identity.width = 640; identity.height = 480;
				ports.channel->OwnerBegin(service.context->session, identity, 40, 60); service.Tick();
			}
			void Emit(unsigned sequence, int dx = 0, unsigned seed = 0, bool blank = false, std::uint64_t captureSession = 0)
			{
				std::vector<std::uint32_t> pixels(640 * 480);
				for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x)
				{
					std::uint32_t value = static_cast<std::uint32_t>((x - dx + 4096) / 4) * 0x9e3779b9u ^ static_cast<std::uint32_t>(y / 4) * 0x85ebca6bu ^ seed;
					value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15;
					const auto gray = blank ? 255u : 20u + value % 216u;
					pixels[static_cast<std::size_t>(y) * 640 + x] = 0xff000000u | gray * 0x010101u;
				}
				Capture::Frame frame;
				frame.bgra = pixels; frame.width = 640; frame.height = 480; frame.strideBytes = 640 * 4;
				frame.geometry = {{0,0,640,480},{0,0,640,480},{16,24,624,456},1,640,480};
				frame.timestamp = ports.now + std::chrono::milliseconds(sequence * 50);
				frame.sequence = sequence; frame.session = captureSession ? captureSession : ports.capture.session;
				frame.quality.status = Capture::QualityStatus::Valid;
				if (ports.callback) ports.callback(frame);
			}
			void Track() { Begin(); Emit(1); service.Tick(); ports.acknowledged = ports.projection; service.Tick(); }
			void PresentEnd()
			{
				const auto state = ports.channel->Read();
				if (const auto final = ports.channel->Pending(true))
					ports.channel->Applied(final->session, final->sequence, state.baseX-final->x, state.baseY-final->y, false);
				ports.channel->Presented(state.session);
			}
			void End()
			{
				service.Tick(); PresentEnd(); service.Tick();
				ports.acknowledged = ports.projection; service.Tick(); ports.channel->BarrierReady(); service.Tick();
			}
		};
		try
		{
			{
				Environment e;
				e.ports.capture.reason = Capture::StopReason::GeometryChanged; e.ports.capture.session = 9;
				e.ports.startStatus = Capture::StartStatus::NotReady; e.Begin();
				check(e.service.state == State::Initializing && !e.service.captureStarted && e.ports.holds == 1, "old Capture reason cannot retire new Initialize");
				e.ports.now += std::chrono::milliseconds(100); e.ports.startStatus = Capture::StartStatus::Started; e.service.Tick();
				e.Emit(1,0,0,true); e.service.Tick();
				check(!e.service.context->initialized && e.service.state == State::Initializing, "rejected F0 stays in warmup");
				e.Emit(2,0,0,false,9); check(!e.service.context->initialized, "wrong Capture session frame ignored");
				e.Emit(3); e.service.Tick();
				check(e.service.context->initialized && e.service.projected && e.service.state == State::Initializing && !e.service.context->tracking, "real F0 requests selection but waits pair ACK");
				e.ports.acknowledged = e.ports.projection - 2; e.service.Tick();
				check(e.service.state == State::Initializing, "old projection ACK cannot start Tracking");
				e.ports.acknowledged = e.ports.projection; e.service.Tick(); e.Emit(4,4);
				check(e.service.state == State::Tracking && e.ports.channel->Read().latest.sequence == 4 && std::abs(e.ports.channel->Read().latest.x - 4) < .1, "real Tracker original-pixel motion reaches actual Channel");
				const auto last = e.ports.channel->Read().latest;
				auto action = PrepareAction(); check(action.state == ActionState::Pending && e.ports.holds == 2, "action adds own hold before reliable End");
				e.Emit(5,8); check(e.ports.channel->Read().final.sequence == last.sequence, "late valid Tracker frame cannot pass sealed End");
				e.service.Tick(); check(PollAction(action.ticket) == ActionState::Pending, "processed End is not action Ready");
				e.PresentEnd(); e.service.Tick(); check(PollAction(action.ticket) == ActionState::Pending, "full presented End still waits original output ACK");
				e.ports.acknowledged = e.ports.projection; e.service.Tick();
				check(e.service.state == State::Stopping && e.ports.channel->Read().handoff, "control only hands off matching final session");
				e.ports.channel->BarrierReady(); e.service.Tick();
				check(PollAction(action.ticket) == ActionState::Ready && e.ports.holds == 1, "End only returns service hold, action remains closed");
				CompleteAction(action.ticket, 42); e.ports.acknowledgedRevision = 41; e.service.Tick();
				check(e.ports.holds == 1, "action real publication receipt must be acknowledged");
				e.ports.acknowledgedRevision = 42; e.service.Tick();
				check(e.ports.holds == 0 && PollAction(action.ticket) == ActionState::Failed, "completed action releases exactly its hold");
				check(!Available(), "undrained Capture still prevents restart");
			}
			{
				Environment e; e.Track();
				e.Emit(2,0,0x12345678u); e.service.Tick();
				check(e.service.state == State::Tracking && !e.service.context->result.accepted && e.service.context->result.statistics.consecutiveRejects == 1, "ordinary first visual reject retains Tracking");
				e.Emit(3,0,0x12345678u); e.service.Tick();
				check(e.service.state == State::Tracking && e.service.context->result.statistics.consecutiveRejects == 2, "ordinary second visual reject retains reference");
				e.Emit(4,0,0x12345678u); e.service.Tick();
				check(e.service.state == State::Paused && !e.service.context->tracking && !e.ports.capture.publishing, "third visual reject pauses actual Business");
				e.Emit(5,4); e.service.Tick(); check(e.service.state == State::Paused && e.ports.channel->Read().latest.sequence == 0, "Paused never auto-recovers");
				RequestStop(); e.End(); check(e.service.state == State::Off && e.ports.holds == 0, "Paused can end and return hold");
			}
			{
				Environment e; e.Track(); ++e.ports.capture.statistics.rejected; e.service.Tick();
				check(e.service.state == State::Paused, "one severe rejected capture pauses immediately");
			}
			{
				Environment e; e.Track(); ++e.ports.capture.statistics.apiFailures; e.service.Tick();
				check(e.service.state == State::Paused, "one new Begin/Update API failure pauses immediately");
			}
			{
				Environment e; e.Begin(); e.ports.now += std::chrono::seconds(6); e.service.Tick(); e.End();
				check(e.service.state == State::Off && !e.service.context->tracking && e.ports.holds == 0, "F0 timeout rolls back to original output");
			}
			{
				Environment e; e.Track(); const auto first = PrepareAction(); const auto second = PrepareAction();
				check(e.ports.holds == 3 && first.ticket.id != second.ticket.id, "two actions retain independent holds");
				CancelAction(first.ticket); CancelAction(first.ticket); check(e.ports.holds == 2, "cancel is idempotent and retains other blocker");
				RetireScene(EndReason::Workspace); e.service.Tick();
				check(PollAction(second.ticket) == ActionState::Retired && e.ports.channel->Read().disposition == Draw::Follow::Disposition::RetireScene, "scene retirement cancels pending action without Window ACK");
				CancelAction(second.ticket); check(e.ports.holds == 0, "retirement and cancellation return only own holds");
			}
			{
				Environment e; const auto offAction = PrepareAction(); RequestToggle(); e.service.Tick();
				check(e.service.state == State::Off && !e.service.context, "Off action excludes new Begin");
				CancelAction(offAction.ticket); e.Track(); const auto action = PrepareAction(); e.service.Tick();
				RequestShutdown(); e.service.Tick();
				check(e.service.state == State::Off && e.service.shutdown && PollAction(action.ticket) == ActionState::Retired &&
					e.ports.channel->Read().disposition == Draw::Follow::Disposition::Shutdown && e.ports.channel->BarrierReady(), "Shutdown upgrades pending normal End and never waits visible ACK");
				CancelAction(action.ticket); check(e.ports.holds == 1, "shutdown cancellation cannot release service hold");
			}
			{
				Environment e; e.Track(); const auto action = PrepareAction(); const auto oldChannel = e.ports.channel;
				e.ports.channel = std::make_shared<Draw::Follow::Channel>(); ++e.ports.scope.hostGeneration; e.ports.holds = 1;
				CancelAction(action.ticket); check(e.ports.holds == 1, "old action cannot return new Host hold");
				RequestStop(); check(!e.ports.channel->Read().endQueued, "old service Stop cannot seal new Host");
				oldChannel->Seal(Draw::Follow::Disposition::RetireScene); e.service.Tick();
				check(e.service.state == State::Off && e.ports.holds == 1, "old service cleanup cannot return new Host hold");
			}
		}
		catch (...) { ++failures; std::fputs("[ScrollFollowControl] FAIL exception\n", stderr); }
		std::fprintf(stderr, "[ScrollFollowControl] failures=%d; actual Tick/Frame/Tracker, external Capture/Host/ACK are seams; no HWND\n", failures);
		return failures ? 1 : 0;
#else
		return 2;
#endif
	}
}
