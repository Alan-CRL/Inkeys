#pragma once

// 在 RenderPipeline module / import 后包含；内部纯数值聚合也由无窗口测试直接验证。
namespace Inkeys::UI::RenderPipeline::DiagnosticsDetail
{
	namespace
	{
		using DiagnosticClock = std::chrono::steady_clock;
		constexpr auto DiagnosticClientCount = static_cast<std::size_t>(Client::Count);
		constexpr double LongFrameMs = 50.0;
		constexpr auto DiagnosticInterval = std::chrono::seconds(1);
		constexpr bool HealthySummariesEnabled = false;

		double Milliseconds(DiagnosticClock::duration duration) noexcept
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}

		struct ClientDiagnostics
		{
			std::uint64_t calls = 0, requested = 0, continued = 0, retried = 0;
			std::array<std::uint64_t, 5> results{};
			std::uint64_t advanced = 0, attempts = 0, ulwAttempts = 0, commits = 0;
			std::uint64_t deferred = 0, skipped = 0, resets = 0;
			std::uint64_t failures = 0, recoveries = 0, exceptions = 0, lightFailureFrames = 0;
			double totalMs = 0.0, maxMs = 0.0, activeGapMaxMs = 0.0;
			double commitRawGapMaxMs = 0.0, commitActiveGapMaxMs = 0.0;
			double rawDtSeconds = 0.0, usedDtSeconds = 0.0;
			double rawDtMaxSeconds = 0.0, usedDtMaxSeconds = 0.0;
			std::array<double, static_cast<std::size_t>(FrameStage::Count)> stageMs{};
			std::array<double, static_cast<std::size_t>(FrameStage::Count)> stageMaxMs{};
			LightingDiagnostics light;
			FrameDiagnostics latest;
			FrameDiagnostics slowest;
			FrameDiagnostics lastFailure;
		};

		struct DiagnosticsSummary
		{
			std::uint64_t batches = 0, longBatches = 0, longPeriods = 0;
			std::uint64_t recoveryAttempts = 0, recoveryFailures = 0, recoverySuccesses = 0;
			ClientMask requested = 0, continued = 0, retried = 0;
			double batchTotalMs = 0.0, batchMaxMs = 0.0, periodMaxMs = 0.0;
			double recoveryMs = 0.0;
			std::array<ClientDiagnostics, DiagnosticClientCount> clients{};
		};

		class DiagnosticsAccumulator
		{
		public:
			void Reset() noexcept { *this = {}; }

			void BeginBatch(DiagnosticClock::time_point now) noexcept
			{
				// 健康窗口定期清零；被限频挡住的异常必须一直保留到 sink 接收。
				if (!windowStarted_ || (!HealthySummariesEnabled && !pending_ && now - windowStart_ >= DiagnosticInterval))
				{
					summary_ = {};
					windowStart_ = now;
					windowStarted_ = true;
				}
				if (previousBatchActive_)
				{
					const double period = Milliseconds(now - previousBatch_);
					summary_.periodMaxMs = (std::max)(summary_.periodMaxMs, period);
					if (period >= LongFrameMs)
					{
						++summary_.longPeriods;
						pending_ = true;
					}
				}
				previousBatch_ = now;
				previousBatchActive_ = true;
			}

			void ResetClientActivity(Client client) noexcept
			{
				// 同一槽重新注册是新生命周期；保留窗口计数，但不跨实例连接活动间隔。
				activity_[static_cast<std::size_t>(client)] = {};
			}

			void MarkIdle() noexcept
			{
				previousBatchActive_ = false;
				for (auto& state : activity_)
				{
					state.active = false;
					state.commitActive = false;
				}
			}

			void AddClient(Client client, FrameResult result, const FrameDiagnostics& sample,
				DiagnosticClock::time_point start, DiagnosticClock::time_point end,
				ClientMask requested, ClientMask continued, ClientMask retried) noexcept
			{
				const auto index = static_cast<std::size_t>(client);
				auto& out = summary_.clients[index];
				auto& activity = activity_[index];
				const auto bit = Mask(client);
				++out.calls;
				out.requested += (requested & bit) != 0;
				out.continued += (continued & bit) != 0;
				out.retried += (retried & bit) != 0;
				++out.results[static_cast<std::size_t>(result)];
				const double duration = Milliseconds(end - start);
				out.totalMs += duration;
				if (out.calls == 1 || duration >= out.maxMs) out.slowest = sample;
				out.maxMs = (std::max)(out.maxMs, duration);
				if (activity.active)
				{
					const double gap = Milliseconds(start - activity.callback);
					out.activeGapMaxMs = (std::max)(out.activeGapMaxMs, gap);
					if (gap >= LongFrameMs) pending_ = true;
				}
				if (duration >= LongFrameMs) pending_ = true;
				out.advanced += sample.animationAdvanced;
				out.attempts += sample.presentAttempted;
				out.ulwAttempts += sample.ulwAttempted;
				out.commits += sample.presentCommitted;
				out.deferred += sample.presentDeferred;
				out.skipped += sample.backoffSkipped;
				out.resets += sample.failureRecoveryReset;
				out.exceptions += sample.callbackException;
				out.rawDtSeconds += sample.rawDtSeconds;
				if (sample.animationAdvanced) out.usedDtSeconds += sample.animationDtSeconds;
				out.rawDtMaxSeconds = (std::max)(out.rawDtMaxSeconds, sample.rawDtSeconds);
				if (sample.animationAdvanced)
					out.usedDtMaxSeconds = (std::max)(out.usedDtMaxSeconds, sample.animationDtSeconds);
				for (std::size_t i = 0; i < out.stageMs.size(); ++i)
				{
					out.stageMs[i] += sample.stageMs[i];
					out.stageMaxMs[i] = (std::max)(out.stageMaxMs[i], sample.stageMs[i]);
				}
				AddLight(out.light, sample.light);
				if (sample.light.roundedParentFailure != 0 || sample.light.geometryParentFailure != 0
					|| sample.light.exactFallback[static_cast<std::size_t>(ExactFallback::CreateFailure)] != 0)
				{
					// 遮罩降级单独触发记录，不能用 present 成功冒充缓存恢复。
					++out.lightFailureFrames;
					pending_ = true;
				}
				out.latest = sample;
				// Retry 也用于合法的布局交接；只有实际失败信息才建立错误/恢复链。
				const bool presentFailed = sample.presentFailed
					|| sample.resourceResult < 0 || sample.getDcResult < 0
					|| sample.releaseDcResult < 0 || sample.endDrawResult < 0
					|| (sample.ulwAttempted && sample.ulwError != 0);
				const bool failed = presentFailed || sample.callbackException || result == FrameResult::DeviceLost;
				if (failed)
				{
					++out.failures;
					out.lastFailure = sample;
					activity.failed = true;
					activity.needsCommit = activity.needsCommit || presentFailed || sample.barSampled;
					pending_ = true;
				}
				else if (activity.failed && (sample.presentCommitted
					|| (!activity.needsCommit && !sample.barSampled
						&& (result == FrameResult::Idle || result == FrameResult::Continue))))
				{
					++out.recoveries;
					activity.failed = false;
					activity.needsCommit = false;
					pending_ = true;
				}
				if (sample.failureRecoveryReset) pending_ = true;
				if (!activity.active) activity.commitActive = false;
				if (sample.presentCommitted)
				{
					if (activity.hasCommit)
					{
						const double gap = Milliseconds(end - activity.commit);
						out.commitRawGapMaxMs = (std::max)(out.commitRawGapMaxMs, gap);
						// raw 保留故障证据，但 idle 后首帧不能把静止时间当成活动长帧。
						if (activity.commitActive)
							out.commitActiveGapMaxMs = (std::max)(out.commitActiveGapMaxMs, gap);
					}
					activity.commit = end;
					activity.hasCommit = true;
					activity.commitActive = true;
				}
				activity.callback = start;
				activity.active = result != FrameResult::Idle && result != FrameResult::Stop;
				if (!activity.active) activity.commitActive = false;
			}

			void AddRecovery(bool succeeded, double durationMs) noexcept
			{
				++summary_.recoveryAttempts;
				summary_.recoveryFailures += !succeeded;
				summary_.recoverySuccesses += succeeded;
				summary_.recoveryMs += durationMs;
				pending_ = true;
			}

			void EndBatch(DiagnosticClock::time_point end, ClientMask requested,
				ClientMask continued, ClientMask retried) noexcept
			{
				++summary_.batches;
				summary_.requested |= requested;
				summary_.continued |= continued;
				summary_.retried |= retried;
				const double duration = Milliseconds(end - previousBatch_);
				summary_.batchTotalMs += duration;
				summary_.batchMaxMs = (std::max)(summary_.batchMaxMs, duration);
				if (duration >= LongFrameMs)
				{
					++summary_.longBatches;
					pending_ = true;
				}
				if constexpr (HealthySummariesEnabled)
				{
					if (end - windowStart_ >= DiagnosticInterval) pending_ = true;
				}
			}

			bool Ready(DiagnosticClock::time_point now) const noexcept
			{
				return pending_ && (!hasAttempt_ || now - lastAttempt_ >= DiagnosticInterval);
			}

			std::chrono::milliseconds WaitDelay(DiagnosticClock::time_point now) const noexcept
			{
				if (!pending_) return (std::chrono::milliseconds::max)();
				if (Ready(now)) return std::chrono::milliseconds::zero();
				return std::chrono::ceil<std::chrono::milliseconds>(lastAttempt_ + DiagnosticInterval - now);
			}

			void BeginAttempt(DiagnosticClock::time_point now) noexcept
			{
				// sink 丢弃或抛异常也占用本次额度，避免每帧格式化/重试写日志。
				lastAttempt_ = now;
				hasAttempt_ = true;
			}

			void CompleteAttempt(bool accepted, double formatMs, double sinkMs) noexcept
			{
				previousFormatMs_ = formatMs;
				previousSinkMs_ = sinkMs;
				if (!accepted)
				{
					++sinkRejected_;
					return;
				}
				summary_ = {};
				windowStarted_ = false;
				pending_ = false;
				sinkRejected_ = 0;
			}

			const DiagnosticsSummary& Summary() const noexcept { return summary_; }
			double PreviousFormatMs() const noexcept { return previousFormatMs_; }
			double PreviousSinkMs() const noexcept { return previousSinkMs_; }
			std::uint64_t SinkRejected() const noexcept { return sinkRejected_; }

		private:
			static void AddLight(LightingDiagnostics& out, const LightingDiagnostics& sample) noexcept
			{
				out.roundedParentHit += sample.roundedParentHit;
				out.roundedParentMiss += sample.roundedParentMiss;
				out.roundedParentCreate += sample.roundedParentCreate;
				out.roundedParentFailure += sample.roundedParentFailure;
				out.geometryParentHit += sample.geometryParentHit;
				out.geometryParentMiss += sample.geometryParentMiss;
				out.geometryParentCreate += sample.geometryParentCreate;
				out.geometryParentFailure += sample.geometryParentFailure;
				out.roundedParentCreateMs += sample.roundedParentCreateMs;
				out.geometryParentCreateMs += sample.geometryParentCreateMs;
				out.exactHit += sample.exactHit;
				out.slices += sample.slices;
				for (std::size_t i = 0; i < out.exactFallback.size(); ++i)
					out.exactFallback[i] += sample.exactFallback[i];
			}

			struct Activity
			{
				DiagnosticClock::time_point callback{}, commit{};
				bool active = false, hasCommit = false, commitActive = false, failed = false;
				bool needsCommit = false;
			};
			std::array<Activity, DiagnosticClientCount> activity_{};
			DiagnosticsSummary summary_;
			DiagnosticClock::time_point previousBatch_{}, windowStart_{}, lastAttempt_{};
			bool previousBatchActive_ = false, windowStarted_ = false;
			bool pending_ = false, hasAttempt_ = false;
			double previousFormatMs_ = 0.0, previousSinkMs_ = 0.0;
			std::uint64_t sinkRejected_ = 0;
		};
	}
}
