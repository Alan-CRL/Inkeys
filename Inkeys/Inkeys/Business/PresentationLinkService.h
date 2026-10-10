#pragma once

#include "PresentationLinkContracts.h"

#include <deque>
#include <map>
#include <set>

namespace Inkeys::Business::PresentationLink
{
	// 单一 owner 串行调用，返回值和 outbox 都是纯值；不持有 COM/窗口/线程资源。
	class PresentationLinkService
	{
	public:
		PresentationLinkService(std::string hostId, Policy policy, Limits limits = {});
		[[nodiscard]] ReceiveResult RegisterSource(const SourceInfo& info, Revision epoch, Tick now);
		[[nodiscard]] ReceiveResult Receive(const DirectoryMessage& message, Tick now);
		[[nodiscard]] bool Disconnect(const std::string& sourceId, Revision epoch, Tick now);
		[[nodiscard]] bool ObserveForeground(const ForegroundObservation& observation, Tick now);
		[[nodiscard]] bool Advance(Tick now);
		[[nodiscard]] bool Confirm(const BindingConfirmation& confirmation, Tick now);
		[[nodiscard]] SelectedSnapshot Selected() const;
		[[nodiscard]] std::optional<BindingRequest> TakeBindingRequest();
		[[nodiscard]] CommandStatus SubmitCommand(const CommandRequest& request, Tick now);
		[[nodiscard]] std::vector<Command> TakeCommands();
		[[nodiscard]] bool ReceiveCommandResult(const CommandResult& result, Tick now);
		[[nodiscard]] std::optional<CommandStatus> CommandOutcome(Revision commandId) const;
		[[nodiscard]] std::size_t CandidateCount() const noexcept;
		[[nodiscard]] bool NeedsFull(const std::string& sourceId) const;

	private:
		struct Entry
		{
			Candidate value;
			Revision logicalShow = 0;
			Revision blockedRevision = 0;
			Tick pageAt = 0, activityAt = 0, availableSince = 0, uncertainSince = 0;
			bool uncertain = false;
		};
		struct Source
		{
			SourceInfo info;
			Revision epoch = 0, sequence = 0;
			Tick lastSeen = 0;
			bool connected = false, needsFull = true;
			std::map<std::string, Entry> entries;
			std::map<std::string, Revision> retiredCandidates;
			std::map<std::string, std::pair<Revision, Revision>> candidateVersions;
			std::set<Revision> retiredEpochs;
		};
		struct Choice
		{
			std::string sourceId, candidateId;
			Revision epoch = 0, generation = 0, logicalShow = 0;
			bool operator==(const Choice&) const = default;
		};
		struct Challenge
		{
			Choice choice;
			Tick since = 0;
			Revision observation = 0;
			std::size_t samples = 0;
			bool sourceOnly = false;
			Choice evidence;
			bool foregroundEvidence = false;
		};
		struct CommandRecord { Command command; CommandStatus status = CommandStatus::Queued; };
		std::string hostId_;
		Policy policy_;
		Limits limits_;
		Tick now_ = 0, lastSoftSwitch_ = 0;
		bool haveSoftSwitch_ = false;
		bool unprovenPageMapping_ = false;
		Revision nextLogical_ = 0, nextSession_ = 0, nextRoute_ = 0, nextTarget_ = 0;
		Revision nextRequest_ = 0, lastCommandId_ = 0, previousSoftShow_ = 0;
		std::map<std::string, Source> sources_;
		std::set<std::string> retiredShows_;
		std::optional<Choice> current_;
		std::optional<Route> route_;
		std::optional<Candidate> published_;
		std::optional<Challenge> challenge_;
		std::optional<BindingRequest> pending_, bindingOutbox_;
		std::optional<ShowIdentity> foreground_;
		std::optional<std::uint32_t> foregroundProcess_;
		Revision foregroundRevision_ = 0, lastExternalShow_ = 0;
		Revision foregroundEvidenceRevision_ = 0;
		Tick foregroundAt_ = 0;
		bool foregroundInkeys_ = false;
		SelectionReason reason_ = SelectionReason::None;
		std::map<Revision, CommandRecord> commands_;
		std::deque<Revision> commandOrder_;
		std::deque<Command> commandOutbox_;

		[[nodiscard]] bool ValidCandidate(const Candidate& candidate, const SourceInfo& source) const;
		[[nodiscard]] bool SetNow(Tick now) noexcept;
		[[nodiscard]] const Entry* Find(const Choice& choice) const;
		[[nodiscard]] Entry* Find(const Choice& choice);
		[[nodiscard]] bool Available(const Choice& choice) const;
		[[nodiscard]] int Tier(Revision logicalShow) const;
		[[nodiscard]] std::optional<Choice> BestChoice() const;
		[[nodiscard]] bool BetterSource(const Choice& left, const Choice& right) const;
		[[nodiscard]] std::optional<Choice> ActivitySource(Revision logicalShow) const;
		void AssignShows();
		void Reconsider();
		void RevokeRoute();
		void Prepare(const Choice& choice, SelectionReason reason, bool hard);
		void PublishCurrent();
		void ExpireCommands();
		void TrimCommandHistory();
	};
}
