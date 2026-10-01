module;

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dwrite_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

module Inkeys.UI.RenderPipeline;

#include "RenderPipeline.Diagnostics.h"

namespace Inkeys::UI::RenderPipeline
{
	namespace
	{
		thread_local FrameDiagnostics* currentDiagnostics = nullptr;

		class CallbackDiagnosticsScope
		{
		public:
			explicit CallbackDiagnosticsScope(FrameDiagnostics* current) noexcept
				: previous_(std::exchange(currentDiagnostics, current)) {}
			~CallbackDiagnosticsScope() { currentDiagnostics = previous_; }

		private:
			FrameDiagnostics* previous_;
		};

		constexpr auto ClientCount = static_cast<std::size_t>(Client::Count);
		constexpr auto FrameInterval = std::chrono::nanoseconds(16'666'667);
		constexpr std::array<Client, ClientCount> DispatchOrder{
			Client::Bar,
			Client::StartupPreview,
			Client::PptBottomLeft,
			Client::PptBottomRight,
			Client::PptMiddleLeft,
			Client::PptMiddleRight,
			Client::Settings,
			Client::WhiteboardFreeze,
		};

		[[nodiscard]] constexpr std::size_t Index(Client client) noexcept
		{
			return static_cast<std::size_t>(client);
		}

		[[nodiscard]] constexpr ClientMask AllClientMask() noexcept
		{
			return (ClientMask{ 1 } << static_cast<unsigned>(Client::Count)) - 1;
		}

		void FormatFrameState(std::ostream& out, const FrameDiagnostics& sample)
		{
			out << "attempt=" << sample.presentAttempted << ",ulwAttempt=" << sample.ulwAttempted
				<< ",ulwSuccess=" << sample.ulwSucceeded << ",commit=" << sample.presentCommitted
				<< ",barStamp=" << sample.hasBarCommitStamp << ",barCommitTicks=" << sample.barCommitTicks
				<< ",barAttemptSerial=" << sample.barAttemptSerial << ",barCommitEpoch=" << sample.barCommitEpoch
				<< ",presentAttemptFrameSerial=" << sample.presentAttemptFrameSerial
				<< ",deferred=" << sample.presentDeferred << ",failed=" << sample.presentFailed
				<< ",advanced=" << sample.animationAdvanced << ",backoffSkip=" << sample.backoffSkipped
				<< ",reset=" << sample.failureRecoveryReset << ",exception=" << sample.callbackException
				<< ",rawDtMs=" << sample.rawDtSeconds * 1000.0
				<< ",animationDtMs=" << sample.animationDtSeconds * 1000.0
				<< ",epoch=" << sample.epoch
				<< ",backend=" << (sample.backend == Backend::Warp ? "WARP" : "HW")
				<< ",target=" << sample.targetSize.cx << 'x' << sample.targetSize.cy
				<< ",capacity=" << sample.capacitySize.cx << 'x' << sample.capacitySize.cy
				<< ",viewport=" << sample.viewport.left << ':' << sample.viewport.top
				<< ':' << sample.viewport.right << ':' << sample.viewport.bottom
				<< ",source=" << sample.source.x << ':' << sample.source.y
				<< ",capacityZoom=" << sample.displayCapacityZoom << ",zoom=" << sample.zoom
				<< ",lightFlags=" << sample.lightFlags
				<< ",failureCount=" << sample.failureCount
				<< ",retryDelay=" << sample.retryDelayFrames << ",nextRetry=" << sample.nextRetryFrame
				<< ",resourceHr=0x" << std::hex << static_cast<std::uint32_t>(sample.resourceResult)
				<< ",getDcHr=0x" << static_cast<std::uint32_t>(sample.getDcResult)
				<< ",releaseDcHr=0x" << static_cast<std::uint32_t>(sample.releaseDcResult)
				<< ",endDrawHr=0x" << static_cast<std::uint32_t>(sample.endDrawResult) << std::dec
				<< ",ulwError=" << sample.ulwError;
		}

		std::string FormatDiagnostics(const DiagnosticsDetail::DiagnosticsAccumulator& diagnostics)
		{
			static constexpr const char* clientNames[]{ "Bar", "StartupPreview", "PptBottomLeft",
				"PptBottomRight", "PptMiddleLeft", "PptMiddleRight", "Settings", "WhiteboardFreeze" };
			static constexpr const char* resultNames[]{ "idle", "continue", "retry", "deviceLost", "stop" };
			static constexpr const char* stageNames[]{ "presentLockWait", "draw", "getDC", "ULW", "releaseDC", "endDraw",
				"wakeAndSnapshot", "displayTransition", "submitTargetsAndLayout",
				"advanceAnimationsAndDeriveLayout", "prepareLightingAndDemand", "dirtyAndPrepare", "resources" };
			static_assert(sizeof(stageNames) / sizeof(stageNames[0]) == static_cast<std::size_t>(FrameStage::Count));
			static constexpr const char* fallbackNames[]{ "transform", "sizeBudget", "warming", "createFailure",
				"other", "unavailable", "geometryScale", "quantizedRadius", "dpi", "alignment" };
			const auto& summary = diagnostics.Summary();
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(3)
				<< "[UI3Diag] batches=" << summary.batches
				<< " longBatches=" << summary.longBatches << " longPeriods=" << summary.longPeriods
				<< " batchMs(total/max)=" << summary.batchTotalMs << '/' << summary.batchMaxMs
				<< " activePeriodMaxMs=" << summary.periodMaxMs
				<< " masks(requested/continued/retry)=" << summary.requested << '/' << summary.continued << '/' << summary.retried
				<< " recovery(attempt/fail/success/ms)=" << summary.recoveryAttempts << '/'
				<< summary.recoveryFailures << '/' << summary.recoverySuccesses << '/' << summary.recoveryMs
				<< " previousFormatMs=" << diagnostics.PreviousFormatMs()
				<< " previousSinkMs=" << diagnostics.PreviousSinkMs()
				<< " sinkRejected=" << diagnostics.SinkRejected();
			for (std::size_t i = 0; i < summary.clients.size(); ++i)
			{
				const auto& client = summary.clients[i];
				if (client.calls == 0) continue;
				out << ' ' << clientNames[i] << "{calls=" << client.calls
					<< ",requested=" << client.requested << ",continued=" << client.continued << ",retried=" << client.retried;
				for (std::size_t j = 0; j < client.results.size(); ++j)
					out << ',' << resultNames[j] << '=' << client.results[j];
				out << ",callbackMs(total/max)=" << client.totalMs << '/' << client.maxMs
					<< ",activeGapMaxMs=" << client.activeGapMaxMs
					<< ",commitGapMs(raw/active)=" << client.commitRawGapMaxMs << '/' << client.commitActiveGapMaxMs
					<< ",advance=" << client.advanced << ",attempt=" << client.attempts << ",ulwAttempt=" << client.ulwAttempts
					<< ",commit=" << client.commits << ",deferred=" << client.deferred << ",backoffSkip=" << client.skipped
					<< ",reset=" << client.resets << ",fail=" << client.failures << ",recovered=" << client.recoveries
					<< ",exception=" << client.exceptions << ",lightFailureFrames=" << client.lightFailureFrames
					<< ",rawDtMs(total/max)=" << client.rawDtSeconds * 1000.0 << '/' << client.rawDtMaxSeconds * 1000.0
					<< ",advancedDtMs(total/max)=" << client.usedDtSeconds * 1000.0 << '/' << client.usedDtMaxSeconds * 1000.0;
				for (std::size_t j = 0; j < client.stageMs.size(); ++j)
					out << ',' << stageNames[j] << "Ms(total/max)=" << client.stageMs[j] << '/' << client.stageMaxMs[j];
				const auto& light = client.light;
				out << ",rounded(hit/miss/create/fail/ms)=" << light.roundedParentHit << '/' << light.roundedParentMiss
					<< '/' << light.roundedParentCreate << '/' << light.roundedParentFailure << '/' << light.roundedParentCreateMs
					<< ",geometry(hit/miss/create/fail/ms)=" << light.geometryParentHit << '/' << light.geometryParentMiss
					<< '/' << light.geometryParentCreate << '/' << light.geometryParentFailure << '/' << light.geometryParentCreateMs
					<< ",exactHit=" << light.exactHit << ",maskDraws=" << light.slices;
				for (std::size_t j = 0; j < light.exactFallback.size(); ++j)
					out << ",fallback_" << fallbackNames[j] << '=' << light.exactFallback[j];
				out << ",latest[";
				FormatFrameState(out, client.latest);
				if (client.maxMs >= DiagnosticsDetail::LongFrameMs)
				{
					out << "],slowest[";
					FormatFrameState(out, client.slowest);
				}
				if (client.failures != 0)
				{
					out << "],lastFailure[";
					FormatFrameState(out, client.lastFailure);
				}
				out << "]}";
			}
			return out.str();
		}

		std::mutex assetMutex;
		std::mutex prepareMutex;
		SharedAssets sharedAssets;
		DeviceEpoch currentEpoch;
		std::optional<DeviceEpoch> preparedEpoch;
		std::optional<DeviceEpoch> pendingEpoch;
		std::uint64_t nextGeneration = 1;
		std::atomic_bool initialized = false;

		HRESULT CreateRenderDevice(
			Backend backend, std::uint64_t generation, DeviceEpoch& epoch)
		{
			ComPtr<ID2D1Factory1> factory;
			{
				std::scoped_lock lock(assetMutex);
				factory = sharedAssets.d2dFactory;
			}
			if (!factory) return E_POINTER;

			const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
			const D3D_DRIVER_TYPE driverType = backend == Backend::Hardware
				? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
			const D3D_FEATURE_LEVEL requestedFeatureLevels[]{
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0,
			};
			const D3D_FEATURE_LEVEL windows7FeatureLevels[]{
				D3D_FEATURE_LEVEL_11_0,
			};

			DeviceEpoch nextEpoch;
			nextEpoch.backend = backend;
			nextEpoch.generation = generation;
			HRESULT hr = D3D11CreateDevice(
				nullptr, driverType, nullptr, creationFlags,
				requestedFeatureLevels, ARRAYSIZE(requestedFeatureLevels),
				D3D11_SDK_VERSION,
				nextEpoch.d3dDevice.ReleaseAndGetAddressOf(),
				&nextEpoch.featureLevel,
				nextEpoch.immediateContext.ReleaseAndGetAddressOf());
			if (hr == E_INVALIDARG)
			{
				// Windows 7 旧运行时不接受包含 11.1 的列表，显式退回 11.0。
				hr = D3D11CreateDevice(
					nullptr, driverType, nullptr, creationFlags,
					windows7FeatureLevels, ARRAYSIZE(windows7FeatureLevels),
					D3D11_SDK_VERSION,
					nextEpoch.d3dDevice.ReleaseAndGetAddressOf(),
					&nextEpoch.featureLevel,
					nextEpoch.immediateContext.ReleaseAndGetAddressOf());
			}
			if (FAILED(hr)) return hr;

			// Platform Update 只保证基础 D3D11；Device1 是可选能力快照。
			nextEpoch.d3dDevice.As(&nextEpoch.d3dDevice1);
			hr = nextEpoch.d3dDevice.As(&nextEpoch.dxgiDevice);
			if (FAILED(hr)) return hr;

			ComPtr<IDXGIAdapter> adapter;
			hr = nextEpoch.dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf());
			if (FAILED(hr)) return hr;
			hr = adapter->GetParent(IID_PPV_ARGS(nextEpoch.dxgiFactory.ReleaseAndGetAddressOf()));
			if (FAILED(hr)) return hr;

			hr = factory->CreateDevice(
				nextEpoch.dxgiDevice.Get(), nextEpoch.d2dDevice.ReleaseAndGetAddressOf());
			if (FAILED(hr)) return hr;

			epoch = std::move(nextEpoch);
			return S_OK;
		}

		[[nodiscard]] FrameContext SnapshotFrameContext(
			std::chrono::steady_clock::time_point frameTime)
		{
			std::scoped_lock lock(assetMutex);
			return FrameContext{ currentEpoch, sharedAssets, frameTime };
		}

		bool RecoverDevice()
		{
			Backend backend = Backend::Warp;
			std::uint64_t generation = 0;
			{
				std::scoped_lock lock(assetMutex);
				backend = currentEpoch.backend;
				generation = nextGeneration++;
			}

			DeviceEpoch recovered;
			if (FAILED(CreateRenderDevice(backend, generation, recovered))) return false;
			// 只有管线线程调用恢复并发布新代次，客户端不会看到半更新资产。
			std::scoped_lock prepareLock(prepareMutex);
			std::scoped_lock lock(assetMutex);
			preparedEpoch.reset();
			pendingEpoch.reset();
			currentEpoch = std::move(recovered);
			return true;
		}

		bool PublishPendingBackend()
		{
			std::scoped_lock prepareLock(prepareMutex);
			std::scoped_lock lock(assetMutex);
			if (!pendingEpoch) return false;
			// currentEpoch 只在渲染线程控制点发布，客户端始终看到完整代次。
			currentEpoch = std::move(*pendingEpoch);
			pendingEpoch.reset();
			return true;
		}
	}

	FrameDiagnostics* CurrentFrameDiagnostics() noexcept { return currentDiagnostics; }

	void StampBarCommit(FrameDiagnostics* diagnostics, bool committed,
		std::uint64_t attemptSerial, std::uint64_t epoch, BarCommitClock clock) noexcept
	{
		// 旧异常诊断或测试 clock 不授权真戳；仅显式采样的完整事务首次确认读钟。
		if (!diagnostics || !diagnostics->detailedCaptureEnabled
			|| !committed || diagnostics->hasBarCommitStamp) return;
		diagnostics->barCommitTicks = clock ? clock()
			: std::chrono::steady_clock::now().time_since_epoch().count();
		diagnostics->barAttemptSerial = attemptSerial;
		diagnostics->barCommitEpoch = epoch;
		diagnostics->hasBarCommitStamp = true;
	}

	FrameStageTimer::FrameStageTimer(FrameDiagnostics* diagnostics, FrameStage stage) noexcept
		: diagnostics_(diagnostics), stage_(stage)
	{
		// 原六段仍服务异常 sink；新七段仅在真正 detailed capture 时读钟/写样本。
		if (diagnostics_ && stage_ >= FrameStage::WakeAndSnapshot
			&& !diagnostics_->detailedCaptureEnabled) diagnostics_ = nullptr;
		if (diagnostics_) start_ = std::chrono::steady_clock::now();
	}

	FrameStageTimer::~FrameStageTimer() { Stop(); }

	void FrameStageTimer::Stop() noexcept
	{
		if (!diagnostics_) return;
		diagnostics_->stageMs[static_cast<std::size_t>(stage_)] +=
			DiagnosticsDetail::Milliseconds(std::chrono::steady_clock::now() - start_);
		diagnostics_ = nullptr;
	}

	void DispatchState::Request(ClientMask mask) noexcept
	{
		requested_.fetch_or(mask & AllClientMask(), std::memory_order_release);
	}

	ClientMask DispatchState::TakeRequested() noexcept
	{
		return requested_.exchange(0, std::memory_order_acq_rel);
	}

	void DispatchState::Reset() noexcept
	{
		requested_.store(0, std::memory_order_release);
	}

	DispatchDecision DispatchState::Complete(ClientMask work,
		ClientMask registered,
		const std::array<FrameResult, ClientCount>& results) noexcept
	{
		DispatchDecision decision;
		decision.work = work;
		for (const auto client : DispatchOrder)
		{
			const auto bit = Mask(client);
			if ((work & bit) == 0) continue;
			switch (results[Index(client)])
			{
			case FrameResult::Continue:
			case FrameResult::Retry:
				decision.next |= bit;
				break;
			case FrameResult::DeviceLost:
				decision.rebuildSharedDevice = true;
				break;
			case FrameResult::Stop:
				decision.stop = true;
				break;
			case FrameResult::Idle:
			default:
				break;
			}
		}
		if (decision.rebuildSharedDevice) decision.next |= registered;
		decision.next &= registered;
		// 显式请求可能与注册并发，不能用调用方稍旧的 registered 快照提前清除。
		decision.requested = TakeRequested();
		decision.next |= decision.requested;
		decision.sleep = decision.next == 0 && !decision.stop;
		return decision;
	}

	struct Scheduler::Impl
	{
		enum class RawPhase : std::uint8_t { Empty, Reserved, Prepared, Releasing };

		struct RawActivity
		{
			std::uint64_t generation = 0, epoch = 0, segment = 0;
			std::int64_t callbackTicks = 0, commitTicks = 0, trueBarCommitTicks = 0;
			bool initialized = false, active = false, commitActive = false, trueBarCommitActive = false;
		};

		[[nodiscard]] static std::int64_t Ticks(std::chrono::steady_clock::time_point time) noexcept
		{
			return time.time_since_epoch().count();
		}

		void RawMarkIdle() noexcept
		{
			if (!rawActive || rawIdle) return;
			rawIdle = true;
			++rawActive->idleTransitions;
			for (auto& activity : rawActivity)
			{
				activity.active = false;
				activity.commitActive = false;
				activity.trueBarCommitActive = false;
			}
		}

		void RawBeginBatch(std::chrono::steady_clock::time_point frameTime,
			ClientMask registered, ClientMask work) noexcept
		{
			if (!rawActive) return;
			rawIdle = false;
			rawBatch = {};
			rawBatch.runSerial = rawActive->runSerial;
			rawBatch.batchSerial = ++rawBatchSerial;
			rawBatch.frameTimeTicks = Ticks(frameTime);
			rawBatch.beginTicks = Ticks(std::chrono::steady_clock::now());
			rawBatch.registered = registered;
			rawBatch.work = work;
		}

		void RawEndBatch(std::chrono::steady_clock::time_point end,
			ClientMask requested, ClientMask continued, ClientMask retried) noexcept
		{
			if (!rawActive) return;
			rawBatch.endTicks = Ticks(end);
			rawBatch.requested = requested;
			rawBatch.continued = continued;
			rawBatch.retried = retried;
			rawBatch.validTime = DiagnosticsDetail::RawBatchTimeValid(rawBatch);
			if (!rawBatch.validTime) ++rawActive->invalidBatches;
			++rawActive->batchSeen;
			if (rawActive->batchRetained < rawActive->capacity)
				rawActive->batches[rawActive->batchRetained++] = rawBatch;
			else ++rawActive->batchDropped;
		}

		void RawRecordCallback(Client client, std::uint64_t generation,
			const FrameContext& context, std::chrono::steady_clock::time_point frameTime,
			FrameResult result, const FrameDiagnostics& frame,
			std::chrono::steady_clock::time_point start,
			std::chrono::steady_clock::time_point end,
			ClientMask requested, ClientMask continued, ClientMask retried) noexcept
		{
			if (!rawActive) return;
			RawCallbackSample sample;
			sample.runSerial = rawActive->runSerial;
			sample.batchSerial = rawBatch.batchSerial;
			sample.callbackSerial = ++rawCallbackSerial;
			sample.client = client;
			sample.callbackGeneration = generation;
			sample.contextEpoch = context.epoch.generation;
			sample.backend = context.epoch.backend;
			sample.featureLevel = context.epoch.featureLevel;
			sample.schedulerIdleEpoch = rawActive->idleTransitions;
			sample.result = result;
			const auto bit = Mask(client);
			sample.requested = (requested & bit) != 0;
			sample.continued = (continued & bit) != 0;
			sample.retried = (retried & bit) != 0;
			sample.frameTimeTicks = Ticks(frameTime);
			sample.startTicks = Ticks(start);
			sample.endTicks = Ticks(end);
			sample.frame = frame;
			auto& activity = rawActivity[Index(client)];
			if (!activity.initialized || !activity.active || activity.generation != generation
				|| activity.epoch != context.epoch.generation)
			{
				++activity.segment;
				activity.active = false;
				activity.commitActive = false;
				activity.trueBarCommitActive = false;
			}
			sample.activitySegment = activity.segment;
			sample.hasPreviousActiveCallback = activity.active;
			if (activity.active) sample.previousActiveCallbackTicks = activity.callbackTicks;
			sample.hasPreviousActiveCommit = activity.commitActive;
			if (activity.commitActive) sample.previousActiveCommitTicks = activity.commitTicks;
			sample.validTime = DiagnosticsDetail::RawCallbackTimeValid(sample);
			if (!sample.validTime) ++rawActive->invalidCallbacks;
			sample.hasPreviousTrueBarCommit = activity.trueBarCommitActive;
			if (activity.trueBarCommitActive) sample.previousTrueBarCommitTicks = activity.trueBarCommitTicks;
			sample.barCommitStatus = DiagnosticsDetail::ClassifyBarCommitStamp(sample);
			const bool trueBarCommitted = sample.barCommitStatus == BarCommitStampStatus::Valid;
			const bool unverifiedBarCommit = sample.barCommitStatus == BarCommitStampStatus::Unverified;
			const bool invalidBarCommitStamp = sample.barCommitStatus == BarCommitStampStatus::Invalid;
			// 成功序号独立于回调/退避/尝试；满容量后仍保留真实分母。
			if (trueBarCommitted) sample.barSuccessSerial = ++rawBarSuccessSerial;
			rawActive->trueBarCommits += trueBarCommitted;
			rawActive->unverifiedBarCommits += unverifiedBarCommit;
			rawActive->invalidBarCommitStamps += invalidBarCommitStamp;
			auto& counters = rawActive->clients[Index(client)];
			++counters.seen;
			counters.advanced += frame.animationAdvanced;
			counters.attempts += frame.presentAttempted;
			counters.commits += frame.presentCommitted;
			counters.failures += DiagnosticsDetail::FrameFailed(result, frame);
			counters.idle += result == FrameResult::Idle;
			counters.retries += result == FrameResult::Retry;
			counters.trueBarCommits += trueBarCommitted;
			counters.unverifiedBarCommits += unverifiedBarCommit;
			counters.invalidBarCommitStamps += invalidBarCommitStamp;
			++rawActive->callbackSeen;
			if (rawActive->callbackRetained < rawActive->capacity)
				rawActive->callbacks[rawActive->callbackRetained++] = sample;
			else ++rawActive->callbackDropped;
			activity.initialized = true;
			activity.generation = generation;
			activity.epoch = context.epoch.generation;
			activity.active = result == FrameResult::Continue || result == FrameResult::Retry;
			activity.callbackTicks = sample.startTicks;
			if (frame.presentCommitted)
			{
				activity.commitActive = activity.active;
				activity.commitTicks = sample.endTicks;
			}
			if (!activity.active) activity.commitActive = false;
			if (trueBarCommitted)
			{
				activity.trueBarCommitActive = activity.active;
				activity.trueBarCommitTicks = frame.barCommitTicks;
			}
			else if (unverifiedBarCommit || invalidBarCommitStamp)
			{
				// 缺失或矛盾戳不成为成功，也不能假定其两侧为连续真提交。
				activity.trueBarCommitActive = false;
			}
			if (!activity.active) activity.trueBarCommitActive = false;
			rawBatch.executed |= bit;
		}

		Impl() { wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
		~Impl()
		{
			Stop();
			if (wakeEvent) CloseHandle(wakeEvent);
		}

		void Stop() noexcept
		{
			stopRequested.store(true, std::memory_order_release);
			if (wakeEvent) SetEvent(wakeEvent);
			if (renderThread.joinable()) renderThread.join();
			// Scheduler 可重启；停止后不能把旧请求或控制命令带进下一轮。
			dispatch.Reset();
			controlRequested.store(false, std::memory_order_release);
			{
				std::scoped_lock lock(callbackMutex);
				controlTasks.clear();
				diagnosticsSink = {};
				pendingDiagnosticsSink.reset();
				diagnostics.Reset();
				// joined 之后才封口，renderThread 早发布 running=false 不代表记录已完整。
				if (rawActive)
				{
					rawActive->sealed = true;
					sealedReport = std::move(rawActive);
					rawActive.reset();
				}
				rawThreadJoined = true;
				++rawLifecycleEpoch;
			}
			if (wakeEvent) ResetEvent(wakeEvent);
			running.store(false, std::memory_order_release);
		}

		void EmitDiagnostics() noexcept
		{
			if (!diagnosticsSink) return;
			const auto start = std::chrono::steady_clock::now();
			if (!diagnostics.Ready(start)) return;
			diagnostics.BeginAttempt(start);
			auto formatted = start;
			bool accepted = false;
			try
			{
				const auto message = FormatDiagnostics(diagnostics);
				formatted = std::chrono::steady_clock::now();
				// 格式化和异步投递都在管线/资源锁外；错误不能改变回调或帧结果。
				accepted = diagnosticsSink(message);
			}
			catch (...) {}
			const auto end = std::chrono::steady_clock::now();
			diagnostics.CompleteAttempt(accepted,
				DiagnosticsDetail::Milliseconds(formatted - start),
				DiagnosticsDetail::Milliseconds(end - formatted));
		}

		DiagnosticsSink diagnosticsSink;
		std::optional<DiagnosticsSink> pendingDiagnosticsSink;
		DiagnosticsDetail::DiagnosticsAccumulator diagnostics;
		std::atomic<RawPhase> rawPhase = RawPhase::Empty;
		std::uint64_t rawReservationToken = 0, rawLifecycleEpoch = 0, rawRunSerial = 0;
		bool rawThreadJoined = true, rawIdle = true;
		std::optional<RawCaptureReport> rawPrepared, rawActive, sealedReport;
		RawCaptureTestHook rawTestHook = nullptr;
		void* rawTestContext = nullptr;
		std::array<RawActivity, ClientCount> rawActivity{};
		RawBatchSample rawBatch;
		std::uint64_t rawBatchSerial = 0, rawCallbackSerial = 0, rawBarSuccessSerial = 0;
		std::mutex callbackMutex;
		std::condition_variable callbackCondition;
		std::array<RenderCallback, ClientCount> callbacks{};
		std::array<std::size_t, ClientCount> activeCallbacks{};
		std::array<std::uint64_t, ClientCount> callbackGenerations{}, diagnosticGenerations{};
		ContextProvider contextProvider;
		DeviceRecoveryCallback deviceRecovery;
		ControlCallback controlCallback;
		std::deque<ControlTask> controlTasks;
		DispatchState dispatch;
		HANDLE wakeEvent = nullptr;
		std::jthread renderThread;
		std::atomic_bool stopRequested = false;
		std::atomic_bool controlRequested = false;
		std::atomic_bool running = false;
	};

	Scheduler::Scheduler() : impl_(new Impl) {}

	Scheduler::~Scheduler()
	{
		delete impl_;
		impl_ = nullptr;
	}

	bool Scheduler::Start(ContextProvider contextProvider,
		DeviceRecoveryCallback deviceRecovery,
		ControlCallback controlCallback)
	{
		if (!impl_ || !impl_->wakeEvent) return false;
		// 自然 Stop 结果也必须先 join 封口，不能让下一轮覆盖仍在退出的 raw owner。
		if (!impl_->running.load(std::memory_order_acquire) && impl_->renderThread.joinable())
			impl_->Stop();
		if (impl_->running.exchange(true, std::memory_order_acq_rel)) return false;
		impl_->stopRequested.store(false, std::memory_order_release);
		{
			std::scoped_lock lock(impl_->callbackMutex);
			impl_->contextProvider = std::move(contextProvider);
			impl_->deviceRecovery = std::move(deviceRecovery);
			impl_->controlCallback = std::move(controlCallback);
			// Start/Stop 的单 owner 生命周期与配置预约分开；旧锁外候选不能跨越一次启动后发布。
			++impl_->rawLifecycleEpoch;
			impl_->rawThreadJoined = false;
			++impl_->rawRunSerial;
			if (impl_->rawPhase.load(std::memory_order_acquire) == Impl::RawPhase::Prepared)
			{
				impl_->rawActive = std::move(impl_->rawPrepared);
				impl_->rawPrepared.reset();
				impl_->rawPhase.store(Impl::RawPhase::Empty, std::memory_order_release);
				impl_->rawActive->runSerial = impl_->rawRunSerial;
				impl_->rawActive->originTicks = Impl::Ticks(std::chrono::steady_clock::now());
				impl_->rawActivity = {};
				impl_->rawBatchSerial = impl_->rawCallbackSerial = impl_->rawBarSuccessSerial = 0;
				impl_->rawIdle = true;
			}
		}

		try { impl_->renderThread = std::jthread([this]
			{
				auto registeredMask = [this]() -> ClientMask
					{
						ClientMask registered = 0;
						std::scoped_lock lock(impl_->callbackMutex);
						for (const auto client : DispatchOrder)
							if (impl_->callbacks[Index(client)]) registered |= Mask(client);
						return registered;
					};
				auto takeControlRequest = [this, &registeredMask]() -> ClientMask
					{
						std::deque<ControlTask> tasks;
						std::optional<DiagnosticsSink> pendingSink;
						{
							std::scoped_lock lock(impl_->callbackMutex);
							tasks.swap(impl_->controlTasks);
							pendingSink.swap(impl_->pendingDiagnosticsSink);
						}
						if (pendingSink)
						{
							if (!impl_->diagnosticsSink || !*pendingSink) impl_->diagnostics.Reset();
							impl_->diagnosticsSink = std::move(*pendingSink);
						}
						// 生命周期控制始终先于设备恢复重试，且与渲染回调同线程串行。
						for (auto& task : tasks)
						{
							try { task(); }
							catch (...) {}
						}
						if (!impl_->controlRequested.exchange(
							false, std::memory_order_acq_rel)) return 0;
						ControlCallback callback;
						{
							std::scoped_lock lock(impl_->callbackMutex);
							callback = impl_->controlCallback;
						}
						// 控制发布成功后只请求当下实际注册的客户端。
						return callback && callback() ? registeredMask() : 0;
					};
				ClientMask requested = 0, continued = 0, retried = 0;
				auto takeRequested = [this, &requested]() -> ClientMask
					{
						const auto next = impl_->dispatch.TakeRequested();
						requested |= next;
						return next;
					};
				auto takeControl = [&]() -> ClientMask
					{
						const auto next = takeControlRequest();
						requested |= next;
						return next;
					};
				ClientMask pending = takeRequested();
				bool recoveryPending = false;
				auto nextDeadline = std::chrono::steady_clock::now();
				while (!impl_->stopRequested.load(std::memory_order_acquire))
				{
					pending |= takeControl();
					impl_->EmitDiagnostics();
					if (pending == 0)
					{
						ResetEvent(impl_->wakeEvent);
						// reset 后再次交换请求和控制位，覆盖 idle 边界竞态。
						pending = takeRequested();
						pending |= takeControl();
						if (pending == 0)
						{
							if (impl_->stopRequested.load(std::memory_order_acquire)) break;
							impl_->RawMarkIdle();
							DWORD waitMs = INFINITE;
							if (impl_->diagnosticsSink)
							{
								impl_->diagnostics.MarkIdle();
								const auto delay = impl_->diagnostics.WaitDelay(std::chrono::steady_clock::now());
								if (delay != (std::chrono::milliseconds::max)())
									waitMs = static_cast<DWORD>(delay.count());
							}
							// 异常转 idle 后只等到诊断期限；超时不制造任何渲染请求。
							WaitForSingleObject(impl_->wakeEvent, waitMs);
							pending = takeRequested();
							pending |= takeControl();
						}
						if (impl_->stopRequested.load(std::memory_order_acquire)) break;
						if (pending == 0) continue;
					}

					const auto now = std::chrono::steady_clock::now();
					if (now < nextDeadline) std::this_thread::sleep_until(nextDeadline);
					const auto frameTime = std::chrono::steady_clock::now();
					nextDeadline = frameTime + FrameInterval;
					pending |= takeRequested();

					ContextProvider contextProvider;
					DeviceRecoveryCallback deviceRecovery;
					ClientMask registered = 0;
					{
						std::scoped_lock lock(impl_->callbackMutex);
						contextProvider = impl_->contextProvider;
						deviceRecovery = impl_->deviceRecovery;
						for (const auto client : DispatchOrder)
							if (impl_->callbacks[Index(client)]) registered |= Mask(client);
					}
					pending &= registered;
					if (pending == 0) continue;
					const bool diagnosticActive = static_cast<bool>(impl_->diagnosticsSink);
					const bool rawActive = static_cast<bool>(impl_->rawActive);
					const bool frameSampleActive = diagnosticActive || rawActive;
					if (diagnosticActive) impl_->diagnostics.BeginBatch(frameTime);
					if (rawActive) impl_->RawBeginBatch(frameTime, registered, pending);
					if (recoveryPending)
					{
						// 恢复失败保留同一批 registered 客户端，下一节拍继续恢复而不使用旧 epoch。
						const auto recoveryStart = frameSampleActive
							? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
						const bool recovered = deviceRecovery && deviceRecovery();
						const auto recoveryEnd = frameSampleActive
							? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
						if (rawActive)
						{
							impl_->rawBatch.recoveryAttempted = true;
							impl_->rawBatch.recoverySucceeded = recovered;
							impl_->rawBatch.recoveryStartTicks = Impl::Ticks(recoveryStart);
							impl_->rawBatch.recoveryEndTicks = Impl::Ticks(recoveryEnd);
						}
						if (diagnosticActive)
							impl_->diagnostics.AddRecovery(recovered, DiagnosticsDetail::Milliseconds(
								recoveryEnd - recoveryStart));
						if (!recovered)
						{
							pending |= registered;
							const auto end = frameSampleActive
								? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
							if (rawActive) impl_->RawEndBatch(end, requested, continued, retried);
							if (diagnosticActive)
							{
								impl_->diagnostics.EndBatch(end, requested, continued, retried);
								impl_->EmitDiagnostics();
							}
							continue;
						}
						recoveryPending = false;
						pending |= registered;
					}
					const FrameContext context = contextProvider
						? contextProvider(frameTime) : FrameContext{ {}, {}, frameTime };
					std::array<FrameResult, ClientCount> results{};
					results.fill(FrameResult::Idle);
					const auto work = std::exchange(pending, 0);
					if (rawActive)
					{
						impl_->rawBatch.work = work;
						impl_->rawBatch.contextValid = true;
						impl_->rawBatch.contextEpoch = context.epoch.generation;
					}
					const auto requestedWork = requested & work;
					const auto continuedWork = continued & work;
					const auto retriedWork = retried & work;
					for (const auto client : DispatchOrder)
					{
						const auto bit = Mask(client);
						if ((work & bit) == 0) continue;
						RenderCallback callback;
						std::uint64_t callbackGeneration = 0;
						{
							std::scoped_lock lock(impl_->callbackMutex);
							callback = impl_->callbacks[Index(client)];
							if (frameSampleActive) callbackGeneration = impl_->callbackGenerations[Index(client)];
							if (callback) ++impl_->activeCallbacks[Index(client)];
						}
						if (!callback) continue;
						std::optional<FrameDiagnostics> sample;
						if (frameSampleActive)
						{
							if (diagnosticActive && callbackGeneration != impl_->diagnosticGenerations[Index(client)])
							{
								impl_->diagnostics.ResetClientActivity(client);
								impl_->diagnosticGenerations[Index(client)] = callbackGeneration;
							}
							sample.emplace();
							sample->detailedCaptureEnabled = rawActive;
							sample->epoch = context.epoch.generation;
							sample->backend = context.epoch.backend;
						}
						const auto callbackStart = frameSampleActive
							? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
						{
							CallbackDiagnosticsScope diagnosticsScope(sample ? &*sample : nullptr);
							try { results[Index(client)] = callback(context); }
							catch (...)
							{
								results[Index(client)] = FrameResult::Retry;
								if (sample) sample->callbackException = true;
							}
						}
						const auto callbackEnd = frameSampleActive
							? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
						if (sample && rawActive)
							impl_->RawRecordCallback(client, callbackGeneration, context, frameTime,
								results[Index(client)], *sample, callbackStart, callbackEnd,
								requestedWork, continuedWork, retriedWork);
						if (sample && diagnosticActive)
							impl_->diagnostics.AddClient(client, results[Index(client)], *sample,
								callbackStart, callbackEnd, requestedWork, continuedWork, retriedWork);
						{
							std::scoped_lock lock(impl_->callbackMutex);
							--impl_->activeCallbacks[Index(client)];
						}
						impl_->callbackCondition.notify_all();
						// 共享设备失效或进程级停止后，不再让本帧后续客户端使用旧上下文。
						if (results[Index(client)] == FrameResult::DeviceLost
							|| results[Index(client)] == FrameResult::Stop) break;
					}

					// 回调执行期间可能注册新客户端；用最新掩码保留其首次请求。
					registered = registeredMask();
					const auto decision = impl_->dispatch.Complete(work, registered, results);
					const auto batchEnd = frameSampleActive
						? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
					if (rawActive)
					{
						impl_->rawBatch.deviceLost = decision.rebuildSharedDevice;
						impl_->rawBatch.stopped = decision.stop;
						impl_->RawEndBatch(batchEnd, requestedWork, continuedWork, retriedWork);
					}
					if (diagnosticActive)
					{
						impl_->diagnostics.EndBatch(batchEnd, requestedWork, continuedWork, retriedWork);
						impl_->EmitDiagnostics();
					}
					if (decision.stop) break;
					pending = decision.next;
					requested = decision.requested;
					continued = 0;
					retried = 0;
					for (const auto client : DispatchOrder)
					{
						if ((work & Mask(client)) == 0) continue;
						if (results[Index(client)] == FrameResult::Continue) continued |= Mask(client);
						if (results[Index(client)] == FrameResult::Retry) retried |= Mask(client);
					}
					if (decision.rebuildSharedDevice)
					{
						recoveryPending = true;
						retried |= registered;
					}
				}
				// 退出只投递已经到期的记录，不为诊断延迟进程关闭。
				impl_->EmitDiagnostics();
				std::deque<ControlTask> finalTasks;
				{
					std::scoped_lock lock(impl_->callbackMutex);
					// 关闭接收与提取队列同锁完成，保证已接受的生命周期任务不会滞留。
					impl_->running.store(false, std::memory_order_release);
					finalTasks.swap(impl_->controlTasks);
				}
				for (auto& task : finalTasks)
				{
					try { task(); }
					catch (...) {}
				}
			}); }
		catch (...)
		{
			impl_->Stop();
			return false;
		}
		return true;
	}

	void Scheduler::Stop() noexcept
	{
		if (impl_) impl_->Stop();
	}

	bool Scheduler::Register(Client client, RenderCallback callback)
	{
		if (!impl_ || !callback || client >= Client::Count) return false;
		{
			std::scoped_lock lock(impl_->callbackMutex);
			impl_->callbacks[Index(client)] = std::move(callback);
			++impl_->callbackGenerations[Index(client)];
		}
		Request(client);
		return true;
	}

	void Scheduler::Unregister(Client client) noexcept
	{
		if (!impl_ || client >= Client::Count) return;
		std::unique_lock lock(impl_->callbackMutex);
		impl_->callbacks[Index(client)] = {};
		++impl_->callbackGenerations[Index(client)];
		impl_->callbackCondition.wait(lock, [this, client]
			{ return impl_->activeCallbacks[Index(client)] == 0; });
	}

	void Scheduler::Request(Client client) noexcept
	{
		if (client < Client::Count) Request(Mask(client));
	}

	void Scheduler::Request(ClientMask mask) noexcept
	{
		if (!impl_) return;
		impl_->dispatch.Request(mask);
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	void Scheduler::RequestControl() noexcept
	{
		if (!impl_) return;
		impl_->controlRequested.store(true, std::memory_order_release);
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	bool Scheduler::PostControl(ControlTask task)
	{
		if (!impl_ || !task || !impl_->running.load(std::memory_order_acquire)
			|| impl_->stopRequested.load(std::memory_order_acquire)) return false;
		{
			std::scoped_lock lock(impl_->callbackMutex);
			if (!impl_->running.load(std::memory_order_acquire)
				|| impl_->stopRequested.load(std::memory_order_acquire)) return false;
			impl_->controlTasks.push_back(std::move(task));
		}
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
		return true;
	}

	bool Scheduler::SetDiagnosticsSink(DiagnosticsSink sink)
	{
		if (!impl_) return false;
		{
			std::scoped_lock lock(impl_->callbackMutex);
			if (impl_->running.load(std::memory_order_acquire)
				&& impl_->stopRequested.load(std::memory_order_acquire)) return false;
			// 沿已有控制点交接，热路径不复制 std::function，也不读取全局 logger。
			impl_->pendingDiagnosticsSink = std::move(sink);
		}
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
		return true;
	}

	bool Scheduler::ConfigureRawCapture(std::size_t capacity)
	{
		if (!impl_ || capacity > 65536) return false;
		constexpr std::size_t MaxRawBytes = 64ull * 1024 * 1024;
		constexpr std::size_t PerEntryBytes = sizeof(RawCallbackSample) + sizeof(RawBatchSample);
		static_assert(32768 <= MaxRawBytes / PerEntryBytes);
		if (capacity != 0 && capacity > MaxRawBytes / PerEntryBytes) return false;
		std::uint64_t epoch = 0, token = 0;
		RawCaptureTestHook hook = nullptr;
		void* hookContext = nullptr;
		std::optional<RawCaptureReport> released;
		{
			std::unique_lock lock(impl_->callbackMutex, std::try_to_lock);
			if (!lock || impl_->running.load(std::memory_order_acquire)
				|| !impl_->rawThreadJoined || impl_->rawActive) return false;
			const auto phase = impl_->rawPhase.load(std::memory_order_acquire);
			if (phase == Impl::RawPhase::Reserved || phase == Impl::RawPhase::Releasing) return false;
			if (capacity == 0)
			{
				if (phase != Impl::RawPhase::Prepared) return true;
				released = std::move(impl_->rawPrepared);
				impl_->rawPrepared.reset();
				impl_->rawPhase.store(Impl::RawPhase::Releasing, std::memory_order_release);
				hook = impl_->rawTestHook;
				hookContext = impl_->rawTestContext;
			}
			else
			{
				if (phase != Impl::RawPhase::Empty || impl_->sealedReport) return false;
				epoch = impl_->rawLifecycleEpoch;
				token = ++impl_->rawReservationToken;
				impl_->rawPhase.store(Impl::RawPhase::Reserved, std::memory_order_release);
				hook = impl_->rawTestHook;
				hookContext = impl_->rawTestContext;
			}
		}
		if (capacity == 0)
		{
			try { if (hook) hook(RawCaptureTestPoint::BeforeRelease, hookContext); }
			catch (...) {}
			released.reset();
			auto expected = Impl::RawPhase::Releasing;
			impl_->rawPhase.compare_exchange_strong(expected, Impl::RawPhase::Empty,
				std::memory_order_acq_rel);
			return true;
		}

		std::optional<RawCaptureReport> candidate;
		try
		{
			candidate.emplace();
			candidate->capacity = capacity;
			candidate->allocatedBytes = capacity * PerEntryBytes;
			candidate->callbacks.resize(capacity);
			candidate->batches.resize(capacity);
			if (hook) hook(RawCaptureTestPoint::AfterAllocation, hookContext);
		}
		catch (...)
		{
			candidate.reset();
			auto expected = Impl::RawPhase::Reserved;
			impl_->rawPhase.compare_exchange_strong(expected, Impl::RawPhase::Empty,
				std::memory_order_acq_rel);
			return false;
		}
		{
			std::unique_lock lock(impl_->callbackMutex, std::try_to_lock);
			if (lock && !impl_->running.load(std::memory_order_acquire)
				&& impl_->rawThreadJoined && impl_->rawLifecycleEpoch == epoch
				&& impl_->rawReservationToken == token
				&& impl_->rawPhase.load(std::memory_order_acquire) == Impl::RawPhase::Reserved
				&& !impl_->sealedReport)
			{
				impl_->rawPrepared = std::move(candidate);
				impl_->rawPhase.store(Impl::RawPhase::Prepared, std::memory_order_release);
				return true;
			}
		}
		// 候选释放完成前保持 Reserved，防止并发配置峰值超过单份预算。
		candidate.reset();
		auto expected = Impl::RawPhase::Reserved;
		impl_->rawPhase.compare_exchange_strong(expected, Impl::RawPhase::Empty,
			std::memory_order_acq_rel);
		return false;
	}

	std::optional<RawCaptureReport> Scheduler::TakeRawCapture() noexcept
	{
		if (!impl_) return std::nullopt;
		std::optional<RawCaptureReport> report;
		{
			std::unique_lock lock(impl_->callbackMutex, std::try_to_lock);
			if (!lock || !impl_->rawThreadJoined || impl_->running.load(std::memory_order_acquire)
				|| !impl_->sealedReport) return std::nullopt;
			report = std::move(impl_->sealedReport);
			impl_->sealedReport.reset();
		}
		report->callbacks.resize(static_cast<std::size_t>(report->callbackRetained));
		report->batches.resize(static_cast<std::size_t>(report->batchRetained));
		return report;
	}

	void Scheduler::SetRawCaptureTestHookForTests(RawCaptureTestHook hook, void* context) noexcept
	{
		if (!impl_) return;
		std::scoped_lock lock(impl_->callbackMutex);
		impl_->rawTestHook = hook;
		impl_->rawTestContext = context;
	}

	void Scheduler::WakeForStop() noexcept
	{
		if (impl_ && impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	namespace
	{
		Scheduler scheduler;
	}

	HRESULT Initialize()
	{
		if (initialized.load(std::memory_order_acquire)) return S_FALSE;
		SharedAssets nextAssets;
		HRESULT hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_MULTI_THREADED,
			__uuidof(ID2D1Factory1), nullptr,
			reinterpret_cast<void**>(nextAssets.d2dFactory.ReleaseAndGetAddressOf()));
		if (FAILED(hr)) return hr;
		hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory1),
			reinterpret_cast<IUnknown**>(nextAssets.dwriteFactory.ReleaseAndGetAddressOf()));
		if (FAILED(hr)) return hr;
		{
			std::scoped_lock lock(assetMutex);
			sharedAssets = std::move(nextAssets);
		}

		DeviceEpoch initialEpoch;
		hr = CreateRenderDevice(Backend::Warp, nextGeneration++, initialEpoch);
		if (FAILED(hr))
		{
			std::scoped_lock lock(assetMutex);
			sharedAssets = {};
			return hr;
		}
		{
			std::scoped_lock lock(assetMutex);
			currentEpoch = std::move(initialEpoch);
		}
		if (!scheduler.Start(
			SnapshotFrameContext, RecoverDevice, PublishPendingBackend))
		{
			std::scoped_lock lock(assetMutex);
			currentEpoch = {};
			sharedAssets = {};
			return E_FAIL;
		}
		initialized.store(true, std::memory_order_release);
		return S_OK;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		scheduler.Stop();
		std::scoped_lock prepareLock(prepareMutex);
		std::scoped_lock lock(assetMutex);
		preparedEpoch.reset();
		pendingEpoch.reset();
		currentEpoch = {};
		sharedAssets = {};
	}

	bool IsInitialized() noexcept
	{
		return initialized.load(std::memory_order_acquire);
	}

	HRESULT InitializeFontCollection(IDWriteFontFileLoader* fileLoader,
		IDWriteFontCollectionLoader* collectionLoader,
		std::span<const UINT> resourceIds)
	{
		if (!fileLoader || !collectionLoader || resourceIds.empty()) return E_INVALIDARG;
		ComPtr<IDWriteFactory1> factory = DWriteFactory();
		if (!factory) return E_POINTER;
		HRESULT hr = factory->RegisterFontFileLoader(fileLoader);
		if (FAILED(hr) && hr != DWRITE_E_ALREADYREGISTERED) return hr;
		hr = factory->RegisterFontCollectionLoader(collectionLoader);
		if (FAILED(hr) && hr != DWRITE_E_ALREADYREGISTERED) return hr;

		ComPtr<IDWriteFontCollection> collection;
		hr = factory->CreateCustomFontCollection(
			collectionLoader, resourceIds.data(),
			static_cast<UINT32>(resourceIds.size_bytes()),
			collection.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return hr;
		std::scoped_lock lock(assetMutex);
		sharedAssets.fontCollection = std::move(collection);
		return S_OK;
	}

	DeviceEpoch GetDeviceEpoch()
	{
		std::scoped_lock lock(assetMutex);
		return currentEpoch;
	}

	SharedAssets GetSharedAssets()
	{
		std::scoped_lock lock(assetMutex);
		return sharedAssets;
	}

	ComPtr<ID2D1Factory1> D2DFactory() { return GetSharedAssets().d2dFactory; }
	ComPtr<IDWriteFactory1> DWriteFactory() { return GetSharedAssets().dwriteFactory; }
	ComPtr<IDWriteFontCollection> FontCollection() { return GetSharedAssets().fontCollection; }

	HRESULT PrepareBackend(Backend backend)
	{
		std::scoped_lock prepareLock(prepareMutex);
		std::uint64_t generation = 0;
		{
			std::scoped_lock lock(assetMutex);
			if (currentEpoch.d2dDevice && currentEpoch.backend == backend)
			{
				preparedEpoch.reset();
				return S_FALSE;
			}
			preparedEpoch.reset();
			generation = nextGeneration++;
		}
		DeviceEpoch prepared;
		const HRESULT hr = CreateRenderDevice(backend, generation, prepared);
		if (FAILED(hr)) return hr;
		std::scoped_lock lock(assetMutex);
		preparedEpoch = std::move(prepared);
		return S_OK;
	}

	bool CommitPreparedBackend() noexcept
	{
		{
			std::scoped_lock prepareLock(prepareMutex);
			std::scoped_lock lock(assetMutex);
			if (!preparedEpoch || pendingEpoch) return false;
			pendingEpoch = std::move(*preparedEpoch);
			preparedEpoch.reset();
		}
		// 先释放准备锁再唤醒；epoch 仍只由唯一渲染线程发布。
		scheduler.RequestControl();
		return true;
	}

	bool Register(Client client, RenderCallback callback)
	{
		return scheduler.Register(client, std::move(callback));
	}

	void Unregister(Client client) noexcept { scheduler.Unregister(client); }
	void Request(Client client) noexcept { scheduler.Request(client); }
	void Request(ClientMask mask) noexcept { scheduler.Request(mask); }
	bool PostControl(ControlTask task) { return scheduler.PostControl(std::move(task)); }
	void WakeForStop() noexcept { scheduler.WakeForStop(); }
	bool ConfigureRawCapture(std::size_t capacity) { return scheduler.ConfigureRawCapture(capacity); }
	std::optional<RawCaptureReport> TakeRawCapture() noexcept { return scheduler.TakeRawCapture(); }
	bool SetDiagnosticsSink(DiagnosticsSink sink) { return scheduler.SetDiagnosticsSink(std::move(sink)); }
}
