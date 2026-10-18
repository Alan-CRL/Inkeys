// Generated verbatim fragments; regenerate using make-presentation-load-probe.py.
#pragma once
namespace ProbeOriginal
{
    using namespace Inkeys::Drawing::Draw3;
		bool PresentationTrackMatchesMode(PresentationStorageTrack track,
			Bridge::SlideBindingMode mode) noexcept
		{
			return track == PresentationStorageTrack::Base ||
				(mode == Bridge::SlideBindingMode::StableSlideId
					? track == PresentationStorageTrack::SlideIdSidecar ||
						track == PresentationStorageTrack::SlideIdSession
					: track == PresentationStorageTrack::PageIndexSidecar ||
						track == PresentationStorageTrack::PageIndexSession);
		}

		bool PresentationLoadedForLane(
			const PresentationPersistenceCompletion& completion) noexcept
		{
			if (completion.status != PresentationPersistenceStatus::Loaded ||
				!PresentationTrackMatchesMode(completion.storageTrack,
					completion.target.bindingMode) ||
				!completion.loadedSnapshot || !completion.fileGuid ||
				completion.loadedSnapshot->fileGuid != *completion.fileGuid)
				return false;
			return completion.loadedSnapshot->workspaceType ==
				(completion.target.bindingMode == Bridge::SlideBindingMode::StableSlideId
					? 2 : draw3::uink::kInkeysPageIndexWorkspaceType);
		}

		bool PresentationEmptyLaneVerified(
			const PresentationPersistenceCompletion& completion) noexcept
		{
			return completion.status == PresentationPersistenceStatus::NotFound &&
				PresentationTrackMatchesMode(completion.storageTrack,
					completion.target.bindingMode);
		}

		bool PresentationLoadUnresolved(Bridge::Workspace workspace,
			bool persistenceInitialized, std::uint64_t mutationRevision) noexcept
		{
			return workspace == Bridge::Workspace::Presentation &&
				!persistenceInitialized && mutationRevision == 0;
		}

		template <typename Submit>
		bool SubmitCurrentPresentationLoad(const Bridge::PresentationTarget& target,
			std::uint64_t slotGeneration, Submit&& submit) noexcept
		{
			if (slotGeneration == 0) return false;
			try
			{
				PresentationLoadRequest request;
				request.target = target;
				request.slotGeneration = slotGeneration;
				return submit(std::move(request));
			}
			catch (...) { return false; }
		}

		struct PresentationCurrentLoadRetry
		{
			static constexpr std::array<std::uint64_t, 4> kDelaysMs =
				{ 250, 500, 1000, 2000 };
			bool ExitPrepared() const noexcept { return exitPrepared_; }
			bool AllowsLoad(bool exitRequested) const noexcept
			{
				return !exitPrepared_ && !exitRequested;
			}
			bool PrepareExit() noexcept
			{
				if (exitPrepared_) return false;
				// 退出屏障在首个 ACK 前成为单调终态；迟到回执不可重新排队。
				exitPrepared_ = true;
				Cancel();
				return true;
			}

			void Cancel() noexcept
			{
				target_.reset();
				slotGeneration_ = 0;
				automaticAttempts_ = 0;
				deadlineMs_ = 0;
				scheduled_ = false;
				manualOnly_ = false;
			}

			void OnIoError(const Bridge::PresentationTarget& target,
				std::uint64_t generation, std::uint64_t nowMs) noexcept
			{
				if (exitPrepared_) return;
				if (generation == 0) { Cancel(); return; }
				if (!Matches(target, generation))
				{
					Cancel();
					try { target_.emplace(target); }
					catch (...) { manualOnly_ = true; return; }
					slotGeneration_ = generation;
				}
				if (automaticAttempts_ == kDelaysMs.size())
				{
					scheduled_ = false;
					if (!manualOnly_)
						std::fprintf(stderr,
							"[Draw3.Presentation] action=current_load_retry result=manual_required attempts=%u generation=%llu\n",
							automaticAttempts_,
							static_cast<unsigned long long>(generation));
					manualOnly_ = true;
					return;
				}
				const std::uint64_t delay = kDelaysMs[automaticAttempts_];
				deadlineMs_ = nowMs > UINT64_MAX - delay ? UINT64_MAX : nowMs + delay;
				scheduled_ = true;
				manualOnly_ = false;
				std::fprintf(stderr,
					"[Draw3.Presentation] action=current_load_retry result=scheduled attempt=%u delay_ms=%llu generation=%llu\n",
					static_cast<unsigned>(automaticAttempts_ + 1),
					static_cast<unsigned long long>(delay),
					static_cast<unsigned long long>(generation));
			}

			void OnTerminalFailure() noexcept
			{
				scheduled_ = false;
				manualOnly_ = true;
			}

			std::optional<double> RemainingWaitMilliseconds(
				const Bridge::PresentationTarget& target,
				std::uint64_t generation, std::uint64_t nowMs) const noexcept
			{
				if (exitPrepared_ || !scheduled_ || manualOnly_ ||
					!Matches(target, generation))
					return std::nullopt;
				return static_cast<double>((std::max)(
					std::uint64_t{ 1 }, deadlineMs_ > nowMs ? deadlineMs_ - nowMs : 0));
			}

			template <typename Submit>
			bool TrySubmit(const Bridge::PresentationTarget& target,
				std::uint64_t generation, std::uint64_t nowMs,
				bool& loadPending, bool persistenceInitialized,
				Submit&& submit) noexcept
			{
				if (exitPrepared_ || !scheduled_ || manualOnly_) return false;
				if (!Matches(target, generation) || persistenceInitialized)
				{
					Cancel();
					return false;
				}
				if (loadPending || nowMs < deadlineMs_) return false;
				scheduled_ = false;
				++automaticAttempts_;
				loadPending = SubmitCurrentPresentationLoad(target, generation,
					std::forward<Submit>(submit));
				if (!loadPending) OnIoError(target, generation, nowMs);
				return loadPending;
			}

		private:
			bool Matches(const Bridge::PresentationTarget& target,
				std::uint64_t generation) const noexcept
			{
				return target_ && generation != 0 &&
					slotGeneration_ == generation && *target_ == target;
			}

			std::optional<Bridge::PresentationTarget> target_;
			std::uint64_t slotGeneration_ = 0;
			std::uint64_t deadlineMs_ = 0;
			unsigned automaticAttempts_ = 0;
			bool scheduled_ = false;
			bool manualOnly_ = false;
			bool exitPrepared_ = false;
		};

    // 此 do/while 仅给原分支的 continue 提供作用域；生产分支内容没有改写。
    inline void ApplyOriginalCurrentLoadFailure(
        const PresentationPersistenceCompletion& completion,
        bool& activePresentationLoadPending,
        bool& activePresentationPersistenceInitialized,
        const std::optional<Bridge::PresentationTarget>& activePresentationTarget,
        std::uint64_t activePresentationSlotGeneration,
        PresentationCurrentLoadRetry& currentLoadRetry)
    {
        const bool loadedVerified = PresentationLoadedForLane(completion);
        const bool emptyVerified = PresentationEmptyLaneVerified(completion);
        do
        {
						activePresentationLoadPending = false;
						if (!emptyVerified && !loadedVerified)
						{
							activePresentationPersistenceInitialized = false;
							if (completion.status == PresentationPersistenceStatus::IoError &&
								activePresentationTarget)
								currentLoadRetry.OnIoError(*activePresentationTarget,
									activePresentationSlotGeneration, GetTickCount64());
							else
							{
								currentLoadRetry.OnTerminalFailure();
								std::fprintf(stderr,
									"[Draw3.Presentation] action=current_load result=manual_required status=%u generation=%llu\n",
									static_cast<unsigned>(completion.status),
									static_cast<unsigned long long>(activePresentationSlotGeneration));
							}
							continue;
						}

        } while (false);
    }

    inline bool OriginalHostWouldQueueTarget(
        const Bridge::ProductState& state,
        std::uint64_t requestedPresentationTargetRevision,
        bool restoreLatestScene)
    {
			if (state.workspace == Bridge::Workspace::Presentation &&
				state.presentationTarget &&
				(state.presentationTarget->targetRevision !=
					requestedPresentationTargetRevision || restoreLatestScene))
            return true;
        return false;
    }
}
