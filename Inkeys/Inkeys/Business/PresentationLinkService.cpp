#include "PresentationLinkService.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace Inkeys::Business::PresentationLink
{
	namespace
	{
		bool Fresh(Tick now, Tick then, Tick duration) noexcept
		{
			return now >= then && now - then < duration;
		}
		Tick Deadline(Tick now, Tick duration) noexcept
		{
			return duration > std::numeric_limits<Tick>::max() - now
				? std::numeric_limits<Tick>::max() : now + duration;
		}
		bool Text(const std::string& value, std::size_t limit, bool required = false)
		{
			if (value.size() > limit || (required && value.empty())) return false;
			// 身份按严格 UTF-8 验证；拒绝 NUL、过长编码、代理项及超范围字符。
			for (std::size_t i = 0; i < value.size();)
			{
				const auto first = static_cast<unsigned char>(value[i++]);
				if (first == 0) return false;
				if (first < 0x80) continue;
				std::uint32_t code = 0, minimum = 0;
				std::size_t trailing = 0;
				if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; minimum = 0x80; trailing = 1; }
				else if (first >= 0xe0 && first <= 0xef) { code = first & 0xf; minimum = 0x800; trailing = 2; }
				else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; minimum = 0x10000; trailing = 3; }
				else return false;
				if (trailing > value.size() - i) return false;
				while (trailing--)
				{
					const auto next = static_cast<unsigned char>(value[i++]);
					if ((next & 0xc0) != 0x80) return false;
					code = (code << 6) | (next & 0x3f);
				}
				if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
			}
			return true;
		}
		bool ValidSource(const SourceInfo& source, const Limits& limits)
		{
			return Text(source.sourceId, limits.identityBytes, true)
				&& Text(source.backend, limits.identityBytes, true)
				&& Text(source.application, limits.identityBytes)
				&& Text(source.transport, limits.identityBytes, true)
				&& Text(source.observerDomain, limits.identityBytes)
				&& source.backendPreference >= -1000 && source.backendPreference <= 1000
				&& static_cast<unsigned>(source.health) <= static_cast<unsigned>(Quality::Valid)
				&& static_cast<unsigned>(source.controlMode) <= static_cast<unsigned>(ControlMode::BrokerSelect)
				&& (source.capabilities & ~((1u << 12) - 1)) == 0;
		}
		std::string ShowKey(const ShowIdentity& show)
		{
			return std::to_string(show.session) + ":" + std::to_string(show.processId)
				+ ":" + std::to_string(show.processCreated) + ":" + std::to_string(show.window)
				+ ":" + show.windowLifetime;
		}
		bool SameTargetPage(const Page& left, const Page& right)
		{
			return left.kind == right.kind && left.ordinal == right.ordinal && left.total == right.total
				&& left.stableKey == right.stableKey && left.identityQuality == right.identityQuality
				&& left.topology == right.topology;
		}
		std::size_t CandidateBytes(const Candidate& value)
		{
			std::size_t bytes = value.candidateId.size() + value.localShowId.size() + value.show.windowLifetime.size();
			if (value.document) bytes += value.document->key.size() + (value.document->path ? value.document->path->size() : 0)
				+ (value.document->title ? value.document->title->size() : 0);
			if (value.page.stableKey) bytes += value.page.stableKey->size();
			for (const auto& key : value.page.topology) bytes += key.size();
			return bytes;
		}
		Capability OperationCapability(Operation operation) noexcept
		{
			switch (operation)
			{
			case Operation::Next: return Capability::Next;
			case Operation::Previous: return Capability::Previous;
			case Operation::Goto: return Capability::Goto;
			case Operation::End: return Capability::End;
			case Operation::Activate: return Capability::Activate;
			case Operation::ExitAnnotation: return Capability::NativeAnnotation;
			}
			return static_cast<Capability>(0);
		}
		bool Terminal(CommandStatus status) noexcept
		{
			return status != CommandStatus::Queued && status != CommandStatus::AwaitingResult;
		}
		bool SafePage(const Candidate& candidate)
		{
			if (candidate.lifecycle != Lifecycle::Active || candidate.quality != Quality::Valid
				|| !candidate.document || candidate.document->key.empty()) return false;
			const auto& page = candidate.page;
			return page.kind == PageKind::Slide && page.ordinal
				&& (page.identityQuality == IdentityQuality::Ordinal
					|| (page.identityQuality == IdentityQuality::Stable && page.stableKey));
		}
	}

	bool IsReliableShow(const ShowIdentity& show) noexcept
	{
		return show.processVerified && show.windowVerified && show.processId != 0
			&& show.processCreated != 0 && show.window != 0 && !show.windowLifetime.empty();
	}

	bool SamePage(const Candidate& left, const Candidate& right) noexcept
	{
		if (!left.document || !right.document || left.document->scope != IdentityScope::Document
			|| right.document->scope != IdentityScope::Document
			|| left.document->quality != IdentityQuality::Stable || right.document->quality != IdentityQuality::Stable
			|| left.document->key != right.document->key || left.page.kind != right.page.kind) return false;
		if (left.page.kind == PageKind::EndScreen) return left.page.total == right.page.total
			&& left.page.identityQuality == IdentityQuality::Stable && right.page.identityQuality == IdentityQuality::Stable
			&& !left.page.topology.empty() && left.page.topology == right.page.topology;
		return left.page.kind == PageKind::Slide && left.page.identityQuality == IdentityQuality::Stable
			&& right.page.identityQuality == IdentityQuality::Stable && left.page.stableKey
			&& left.page.stableKey == right.page.stableKey && left.page.ordinal == right.page.ordinal
			&& left.page.total == right.page.total && left.page.topology == right.page.topology;
	}

	CommandStatus GuardCommand(const Command& command, const Route& currentRoute,
		const Candidate& candidate, const SourceInfo& source, Tick now, ExecutionLedger& ledger) noexcept
	{
		if (command.commandId == 0 || command.target != currentRoute || now >= command.deadline
			|| source.sourceId != currentRoute.sourceId || ledger.sourceId != source.sourceId
			|| ledger.epoch != currentRoute.epoch || source.health != Quality::Valid
			|| candidate.candidateId != currentRoute.candidateId || candidate.generation != currentRoute.generation
			|| candidate.localShowId != currentRoute.localShowId || candidate.lifecycle != Lifecycle::Active
			|| candidate.quality != Quality::Valid || source.controlMode != ControlMode::BrokerSelect)
			return CommandStatus::RejectedStale;
		if (command.commandId <= ledger.lastCommandId) return CommandStatus::Duplicate;
		if (!HasCapability(source.capabilities, OperationCapability(command.operation))) return CommandStatus::Unsupported;
		if (command.expectedPage && !SameTargetPage(*command.expectedPage, candidate.page)) return CommandStatus::RejectedStale;
		if (command.operation == Operation::Goto && (!command.gotoOrdinal || !candidate.page.total
			|| *command.gotoOrdinal == 0 || *command.gotoOrdinal > *candidate.page.total)) return CommandStatus::RejectedStale;
		if (command.operation != Operation::Goto && command.gotoOrdinal) return CommandStatus::RejectedStale;
		ledger.lastCommandId = command.commandId;
		return CommandStatus::Queued;
	}

	PresentationLinkService::PresentationLinkService(std::string hostId, Policy policy, Limits limits)
		: hostId_(std::move(hostId)), policy_(policy), limits_(limits)
	{
		if (!Text(hostId_, limits.identityBytes, true) || policy.sourceTimeout == 0
			|| policy.pageFreshness == 0 || policy.activityFreshness == 0 || policy.weakWindow == 0
			|| policy.softSwitchInterval == 0 || policy.reverseWindow == 0 || policy.sourceRecoveryWindow == 0
			|| policy.bindingTimeout == 0 || policy.busyGrace == 0 || policy.weakSamples < 2
			|| limits.sources == 0 || limits.candidatesPerSource == 0 || limits.candidatesTotal == 0
			|| limits.identityBytes == 0 || limits.pages == 0 || limits.candidateHistory < limits.candidatesPerSource
			|| limits.epochHistory == 0 || limits.retiredShows == 0 || limits.commands == 0
			|| limits.commandHistory < limits.commands || limits.valueBytes == 0 || limits.directoryBytes < limits.valueBytes)
			throw std::invalid_argument("PresentationLinkService requires explicit valid policy and bounded limits");
		if (limits.identityBytes > 32768 || limits.pages > 10000 || limits.valueBytes > 1048576
			|| limits.directoryBytes > 16777216 || limits.sources > 1024 || limits.candidatesTotal > 65536)
			throw std::invalid_argument("PresentationLinkService limits exceed hard contract bounds");
	}

	bool PresentationLinkService::SetNow(Tick now) noexcept
	{
		if (now < now_) return false;
		now_ = now;
		return true;
	}

	ReceiveResult PresentationLinkService::RegisterSource(const SourceInfo& info, Revision epoch, Tick now)
	{
		if (epoch == 0 || !ValidSource(info, limits_) || now < now_) return ReceiveResult::Invalid;
		auto existing = sources_.find(info.sourceId);
		if (existing == sources_.end())
		{
			if (sources_.size() >= limits_.sources) return ReceiveResult::Capacity;
			Source source; source.info = info; source.epoch = epoch; source.lastSeen = now;
			sources_.emplace(info.sourceId, std::move(source));
		}
		else
		{
			if (existing->second.epoch == epoch || existing->second.retiredEpochs.contains(epoch)) return ReceiveResult::OldEpoch;
			if (existing->second.retiredEpochs.size() >= limits_.epochHistory) return ReceiveResult::Capacity;
			auto retired = existing->second.retiredEpochs;
			retired.insert(existing->second.epoch);
			Source replacement; replacement.info = info; replacement.epoch = epoch; replacement.lastSeen = now;
			replacement.retiredEpochs = std::move(retired);
			existing->second = std::move(replacement);
		}
		(void)SetNow(now); Reconsider();
		return ReceiveResult::Accepted;
	}

	bool PresentationLinkService::ValidCandidate(const Candidate& value, const SourceInfo& source) const
	{
		if (!Text(value.candidateId, limits_.identityBytes, true) || !Text(value.localShowId, limits_.identityBytes, true)
			|| value.generation == 0 || value.stateRevision == 0
			|| static_cast<unsigned>(value.lifecycle) > static_cast<unsigned>(Lifecycle::Ended)
			|| static_cast<unsigned>(value.quality) > static_cast<unsigned>(Quality::Valid)
			|| static_cast<unsigned>(value.activity.providerActive) > static_cast<unsigned>(TriState::Yes)
			|| (value.lifecycle == Lifecycle::Active && !HasCapability(source.capabilities, Capability::ObserveLifecycle))) return false;
		std::size_t bytes = value.candidateId.size() + value.localShowId.size() + value.show.windowLifetime.size();
		if (!Text(value.show.windowLifetime, limits_.identityBytes)
			|| (value.show.processVerified && (value.show.processId == 0 || value.show.processCreated == 0))
			|| (value.show.windowVerified && (value.show.window == 0 || value.show.windowLifetime.empty()))) return false;
		if (value.document)
		{
			const auto& document = *value.document;
			if (!Text(document.key, limits_.identityBytes, true)
				|| static_cast<unsigned>(document.scope) > static_cast<unsigned>(IdentityScope::Document)
				|| static_cast<unsigned>(document.quality) > static_cast<unsigned>(IdentityQuality::Stable)
				|| (document.path && !Text(*document.path, limits_.identityBytes))
				|| (document.title && !Text(*document.title, limits_.identityBytes))) return false;
			if (document.quality == IdentityQuality::Stable && document.scope == IdentityScope::Document
				&& !HasCapability(source.capabilities, Capability::StableDocumentIdentity)) return false;
			bytes += document.key.size() + (document.path ? document.path->size() : 0) + (document.title ? document.title->size() : 0);
		}
		const auto& page = value.page;
		if (static_cast<unsigned>(page.kind) > static_cast<unsigned>(PageKind::EndScreen)
			|| static_cast<unsigned>(page.identityQuality) > static_cast<unsigned>(IdentityQuality::Stable)
			|| page.topology.size() > limits_.pages || (page.total && (*page.total == 0 || *page.total > limits_.pages))
			|| (page.ordinal && (*page.ordinal == 0 || *page.ordinal > limits_.pages || (page.total && *page.ordinal > *page.total)))) return false;
		if ((page.kind != PageKind::Unknown || page.ordinal || page.stableKey || !page.topology.empty())
			&& (!HasCapability(source.capabilities, Capability::ObservePage) || page.revision == 0)) return false;
		if (page.total && !HasCapability(source.capabilities, Capability::PageCount)) return false;
		if (page.kind != PageKind::Slide && (page.ordinal || page.stableKey)) return false;
		if (page.kind == PageKind::Slide && !page.ordinal && !page.stableKey) return false;
		if (page.stableKey && (!Text(*page.stableKey, limits_.identityBytes, true)
			|| page.identityQuality != IdentityQuality::Stable || !HasCapability(source.capabilities, Capability::StablePageIdentity))) return false;
		if (!page.topology.empty())
		{
			if (!page.total || page.topology.size() != *page.total || page.topologyRevision == 0
				|| !HasCapability(source.capabilities, Capability::StablePageIdentity)) return false;
			std::set<std::string> unique;
			for (const auto& key : page.topology)
			{
				if (!Text(key, limits_.identityBytes, true) || !unique.insert(key).second) return false;
				bytes += key.size();
				if (bytes > limits_.valueBytes) return false;
			}
			if (page.kind == PageKind::Slide && page.stableKey && page.ordinal
				&& page.topology[*page.ordinal - 1] != *page.stableKey) return false;
		}
		if (page.stableKey) bytes += page.stableKey->size();
		if ((value.activity.providerActive != TriState::Unknown || value.activity.processHint || value.activity.wpsHint)
			&& value.activity.observationRevision == 0) return false;
		return bytes <= limits_.valueBytes;
	}

	ReceiveResult PresentationLinkService::Receive(const DirectoryMessage& message, Tick now)
	{
		const auto& envelope = message.envelope;
		if (now < now_ || envelope.hostId != hostId_ || envelope.major != 1 || envelope.minor != 0
			|| envelope.sequence == 0 || static_cast<unsigned>(envelope.kind) > static_cast<unsigned>(MessageKind::Heartbeat)) return ReceiveResult::Invalid;
		auto found = sources_.find(envelope.sourceId);
		if (found == sources_.end() || found->second.epoch != envelope.epoch) return ReceiveResult::OldEpoch;
		auto& source = found->second;
		if (envelope.sequence <= source.sequence) return ReceiveResult::OldSequence;
		const bool gap = source.sequence != 0 && envelope.sequence - source.sequence != 1;
		if (gap)
		{
			// 合法 envelope 已证明观察有缺口；payload 校验失败也不能保留旧可执行路由。
			// 保留原目录/高水位供全量校验，既不发布部分 payload，也不消耗其 sequence。
			(void)SetNow(now); source.needsFull = true; source.connected = false; Reconsider();
		}
		if (message.source && (message.source->sourceId != envelope.sourceId || !ValidSource(*message.source, limits_))) return ReceiveResult::Invalid;
		const auto info = message.source.value_or(source.info);
		if (envelope.kind == MessageKind::Heartbeat && (!message.candidates.empty() || !message.removed.empty() || message.source)) return ReceiveResult::Invalid;
		if (envelope.kind == MessageKind::Full && !message.removed.empty()) return ReceiveResult::Invalid;
		if (message.candidates.size() > limits_.candidatesPerSource || message.removed.size() > limits_.candidatesPerSource) return ReceiveResult::Capacity;
		const bool timedOut = source.connected && !Fresh(now, source.lastSeen, policy_.sourceTimeout);
		const bool reset = source.needsFull || gap || timedOut;
		// 先完整验证在局部副本上构造目录，非法第二项不得使第一项可见。
		auto entries = envelope.kind == MessageKind::Full ? std::map<std::string, Entry>{} : source.entries;
		auto retired = source.retiredCandidates;
		auto versions = source.candidateVersions;
		auto retiredShows = retiredShows_;
		std::set<std::string> touched;
		std::size_t bytes = 0;
		for (const auto& value : message.candidates)
		{
			if (!ValidCandidate(value, info) || !touched.insert(value.candidateId).second) return ReceiveResult::Invalid;
			bytes += value.candidateId.size() + value.localShowId.size() + value.show.windowLifetime.size();
			if (value.document) bytes += value.document->key.size() + (value.document->path ? value.document->path->size() : 0) + (value.document->title ? value.document->title->size() : 0);
			for (const auto& key : value.page.topology) bytes += key.size();
			if (value.page.stableKey) bytes += value.page.stableKey->size();
			if (bytes > limits_.directoryBytes) return ReceiveResult::Capacity;
			const auto old = source.entries.find(value.candidateId);
			const auto tombstone = retired.find(value.candidateId);
			const bool terminalObservation = value.lifecycle == Lifecycle::Inactive || value.lifecycle == Lifecycle::Ended;
			// 重复终态是目录事实，不是复活；更旧代与同退役代的 Active/Unknown 仍拒绝。
			if (tombstone != retired.end() && (value.generation < tombstone->second
				|| (value.generation == tombstone->second && !terminalObservation))) return ReceiveResult::Invalid;
			const auto highWater = versions.find(value.candidateId);
			if (highWater != versions.end() && (value.generation < highWater->second.first
				|| (value.generation == highWater->second.first && value.stateRevision < highWater->second.second))) return ReceiveResult::Invalid;
			versions[value.candidateId] = { value.generation, value.stateRevision };
			Entry entry; entry.value = value; entry.pageAt = now; entry.activityAt = now; entry.availableSince = now;
			if (old != source.entries.end())
			{
				if (value.generation < old->second.value.generation) return ReceiveResult::Invalid;
				if (value.generation == old->second.value.generation)
				{
					const auto& previous = old->second.value;
					if (value.stateRevision < previous.stateRevision || (value.stateRevision == previous.stateRevision && value != previous)
						|| value.localShowId != previous.localShowId || value.show != previous.show
						|| value.page.revision < previous.page.revision || value.activity.observationRevision < previous.activity.observationRevision
						|| (value.page.revision == previous.page.revision && value.page != previous.page)
						|| (value.activity.observationRevision == previous.activity.observationRevision && value.activity != previous.activity)) return ReceiveResult::Invalid;
					if (!reset)
					{
						entry.logicalShow = old->second.logicalShow;
						entry.blockedRevision = old->second.blockedRevision;
						if (value.page.revision == previous.page.revision) entry.pageAt = old->second.pageAt;
						if (value.activity.observationRevision == previous.activity.observationRevision) entry.activityAt = old->second.activityAt;
						if (value.quality == Quality::Valid && value.lifecycle == Lifecycle::Active
							&& previous.quality == Quality::Valid && previous.lifecycle == Lifecycle::Active) entry.availableSince = old->second.availableSince;
						if (value.quality != Quality::Valid || value.lifecycle == Lifecycle::Unknown)
						{
							entry.uncertain = true; entry.uncertainSince = old->second.uncertain ? old->second.uncertainSince : now;
						}
					}
				}
			}
			if (IsReliableShow(value.show) && retiredShows.contains(ShowKey(value.show)) && value.lifecycle == Lifecycle::Active) return ReceiveResult::Invalid;
			// Active 经 Unknown/Busy 过渡后再明确 Inactive，同样结束此前已知场次。
			if (value.lifecycle == Lifecycle::Ended || (value.lifecycle == Lifecycle::Inactive
				&& old != source.entries.end() && old->second.logicalShow != 0))
			{
				retired[value.candidateId] = value.generation;
				if (value.lifecycle == Lifecycle::Ended && IsReliableShow(value.show)) retiredShows.insert(ShowKey(value.show));
			}
			if (terminalObservation) entry.logicalShow = 0; // 终态不分配或恢复可执行场次。
			entries[value.candidateId] = std::move(entry);
		}
		for (const auto& removal : message.removed)
		{
			if (!Text(removal.candidateId, limits_.identityBytes, true) || removal.generation == 0
				|| !touched.insert(removal.candidateId).second) return ReceiveResult::Invalid;
			const auto old = source.entries.find(removal.candidateId);
			if (old == source.entries.end() || old->second.value.generation != removal.generation) return ReceiveResult::Invalid;
			entries.erase(removal.candidateId); retired[removal.candidateId] = removal.generation;
		}
		if (envelope.kind == MessageKind::Full)
			for (const auto& [id, entry] : source.entries)
				if (!entries.contains(id)) retired[id] = entry.value.generation;
		bytes = 0;
		for (const auto& [id, entry] : entries)
		{
			if (!ValidCandidate(entry.value, info)) return ReceiveResult::Invalid;
			bytes += CandidateBytes(entry.value);
			if (bytes > limits_.directoryBytes) return ReceiveResult::Capacity;
		}
		if (entries.size() > limits_.candidatesPerSource || retired.size() > limits_.candidateHistory || versions.size() > limits_.candidateHistory
			|| retiredShows.size() > limits_.retiredShows
			|| CandidateCount() - source.entries.size() + entries.size() > limits_.candidatesTotal) return ReceiveResult::Capacity;
		(void)SetNow(now);
		if (reset && envelope.kind != MessageKind::Full)
		{
			source.entries.clear(); source.needsFull = true; source.connected = false; source.sequence = envelope.sequence;
			Reconsider(); return gap ? ReceiveResult::Gap : ReceiveResult::NeedFull;
		}
		if (envelope.kind != MessageKind::Heartbeat)
		{
			if (reset)
			{
				if (current_ && current_->sourceId == envelope.sourceId) RevokeRoute();
				if (pending_ && pending_->target.sourceId == envelope.sourceId) { pending_.reset(); bindingOutbox_.reset(); }
			}
			if (info.health == Quality::Valid && source.info.health != Quality::Valid)
				for (auto& [id, entry] : entries) entry.availableSince = now;
			source.entries = std::move(entries); source.retiredCandidates = std::move(retired);
			source.candidateVersions = std::move(versions);
			source.info = info; retiredShows_ = std::move(retiredShows);
		}
		source.sequence = envelope.sequence; source.lastSeen = now; source.connected = true; source.needsFull = false;
		AssignShows(); Reconsider();
		return ReceiveResult::Accepted;
	}

	bool PresentationLinkService::Disconnect(const std::string& id, Revision epoch, Tick now)
	{
		auto source = sources_.find(id);
		if (source == sources_.end() || source->second.epoch != epoch || !SetNow(now)) return false;
		source->second.connected = false; source->second.needsFull = true; source->second.entries.clear();
		Reconsider(); return true;
	}

	void PresentationLinkService::AssignShows()
	{
		std::map<std::string, Revision> reliable;
		for (const auto& [id, source] : sources_)
		{
			// 入站消息也推进时间；未经过 Advance 的过期来源不能提供连续观察证明。
			if (!source.connected || source.needsFull || !Fresh(now_, source.lastSeen, policy_.sourceTimeout)) continue;
			for (const auto& [candidateId, entry] : source.entries)
				if (entry.logicalShow != 0 && entry.value.lifecycle == Lifecycle::Active && IsReliableShow(entry.value.show)
					&& !retiredShows_.contains(ShowKey(entry.value.show))) reliable.emplace(ShowKey(entry.value.show), entry.logicalShow);
		}
		for (auto& [id, source] : sources_)
			for (auto& [candidateId, entry] : source.entries)
			{
				if (entry.value.lifecycle != Lifecycle::Active) continue;
				if (IsReliableShow(entry.value.show) && retiredShows_.contains(ShowKey(entry.value.show))) { entry.logicalShow = 0; continue; }
				if (entry.logicalShow != 0) continue;
				if (IsReliableShow(entry.value.show))
				{
					const auto key = ShowKey(entry.value.show);
					const auto old = reliable.find(key);
					entry.logicalShow = old == reliable.end() ? ++nextLogical_ : old->second;
					reliable.emplace(key, entry.logicalShow);
				}
				else entry.logicalShow = ++nextLogical_;
			}
	}

	const PresentationLinkService::Entry* PresentationLinkService::Find(const Choice& choice) const
	{
		const auto source = sources_.find(choice.sourceId);
		if (source == sources_.end() || source->second.epoch != choice.epoch) return nullptr;
		const auto entry = source->second.entries.find(choice.candidateId);
		if (entry == source->second.entries.end() || entry->second.value.generation != choice.generation
			|| entry->second.logicalShow != choice.logicalShow) return nullptr;
		return &entry->second;
	}
	PresentationLinkService::Entry* PresentationLinkService::Find(const Choice& choice)
	{
		return const_cast<Entry*>(static_cast<const PresentationLinkService*>(this)->Find(choice));
	}
	bool PresentationLinkService::Available(const Choice& choice) const
	{
		const auto* entry = Find(choice);
		if (!entry || entry->logicalShow == 0) return false;
		const auto& source = sources_.at(choice.sourceId);
		return source.connected && !source.needsFull && Fresh(now_, source.lastSeen, policy_.sourceTimeout)
			&& source.info.health == Quality::Valid && entry->value.lifecycle == Lifecycle::Active
			&& entry->value.quality == Quality::Valid
			&& entry->blockedRevision != entry->value.stateRevision
			&& (entry->value.page.revision == 0 || Fresh(now_, entry->pageAt, policy_.pageFreshness));
	}
	int PresentationLinkService::Tier(Revision logicalShow) const
	{
		int tier = 0;
		for (const auto& [id, source] : sources_)
			for (const auto& [candidateId, entry] : source.entries)
			{
				Choice choice{ id, candidateId, source.epoch, entry.value.generation, entry.logicalShow };
				if (entry.logicalShow != logicalShow || !Available(choice)) continue;
				tier = std::max(tier, 1);
				if (foreground_ && IsReliableShow(entry.value.show) && entry.value.show == *foreground_
					&& (foregroundInkeys_ || Fresh(now_, foregroundAt_, policy_.activityFreshness))) return 3;
				if (Fresh(now_, entry.activityAt, policy_.activityFreshness)
					&& (entry.value.activity.providerActive == TriState::Yes || entry.value.activity.processHint || entry.value.activity.wpsHint)) tier = 2;
				if (foregroundProcess_ && entry.value.show.processVerified && entry.value.show.processId == *foregroundProcess_
					&& Fresh(now_, foregroundAt_, policy_.activityFreshness)) tier = 2;
			}
		return tier;
	}
	bool PresentationLinkService::BetterSource(const Choice& left, const Choice& right) const
	{
		const auto rank = [&](const Choice& choice)
		{
			const auto* entry = Find(choice);
			const auto& info = sources_.at(choice.sourceId).info;
			unsigned controls = 0;
			for (unsigned i = 6; i < 12; ++i) controls += (info.capabilities >> i) & 1u;
			return std::tuple{ Available(choice), entry && SafePage(entry->value),
				entry ? static_cast<int>(entry->value.page.identityQuality) : 0, controls, info.backendPreference };
		};
		return rank(left) > rank(right);
	}
	std::optional<PresentationLinkService::Choice> PresentationLinkService::ActivitySource(Revision logicalShow) const
	{
		for (const auto& [id, source] : sources_)
			for (const auto& [candidateId, entry] : source.entries)
			{
				Choice choice{ id, candidateId, source.epoch, entry.value.generation, entry.logicalShow };
				if (entry.logicalShow == logicalShow && Available(choice) && Fresh(now_, entry.activityAt, policy_.activityFreshness)
					&& (entry.value.activity.providerActive == TriState::Yes || entry.value.activity.processHint || entry.value.activity.wpsHint)) return choice;
			}
		return std::nullopt;
	}
	std::optional<PresentationLinkService::Choice> PresentationLinkService::BestChoice() const
	{
		std::map<Revision, Choice> shows;
		for (const auto& [id, source] : sources_)
			for (const auto& [candidateId, entry] : source.entries)
			{
				Choice choice{ id, candidateId, source.epoch, entry.value.generation, entry.logicalShow };
				if (!Available(choice)) continue;
				auto best = shows.find(choice.logicalShow);
				if (best == shows.end()) shows.emplace(choice.logicalShow, choice);
				else if (BetterSource(choice, best->second)) best->second = choice;
			}
		if (current_ && Available(*current_))
		{
			auto best = shows.find(current_->logicalShow);
			if (best != shows.end() && !BetterSource(best->second, *current_)) best->second = *current_;
		}
		std::optional<Choice> result;
		for (const auto& [logical, choice] : shows)
		{
			if (!result || Tier(logical) > Tier(result->logicalShow)) result = choice;
			else if (Tier(logical) == Tier(result->logicalShow))
			{
				const bool keep = current_ && current_->logicalShow == logical && Available(*current_);
				const bool alreadyCurrent = current_ && current_->logicalShow == result->logicalShow && Available(*current_);
				if (keep || (!alreadyCurrent && logical == lastExternalShow_)) result = choice;
				// 无 current 时按 source/candidate 稳定键决胜，不按到达顺序或权限。
				else if (!alreadyCurrent && result->logicalShow != lastExternalShow_
					&& std::tie(choice.sourceId, choice.candidateId) < std::tie(result->sourceId, result->candidateId)) result = choice;
			}
		}
		return result;
	}

	bool PresentationLinkService::ObserveForeground(const ForegroundObservation& observation, Tick now)
	{
		if (observation.revision <= foregroundRevision_ || now < now_
			|| static_cast<unsigned>(observation.kind) > static_cast<unsigned>(ForegroundKind::Inkeys)
			|| (observation.show && (!IsReliableShow(*observation.show) || !Text(observation.show->windowLifetime, limits_.identityBytes, true)))) return false;
		(void)SetNow(now); foregroundRevision_ = observation.revision;
		if (!observation.selfInduced)
		{
			foregroundInkeys_ = observation.kind == ForegroundKind::Inkeys;
			if (!foregroundInkeys_)
			{
				foreground_ = observation.kind == ForegroundKind::External ? observation.show : std::nullopt;
				foregroundProcess_ = observation.kind == ForegroundKind::External ? observation.processHint : std::nullopt;
				foregroundAt_ = now;
				foregroundEvidenceRevision_ = observation.revision;
			}
		}
		if (foreground_)
			for (const auto& [id, source] : sources_)
				for (const auto& [candidateId, entry] : source.entries)
					if (entry.value.show == *foreground_ && entry.logicalShow != 0) lastExternalShow_ = entry.logicalShow;
		Reconsider(); return true;
	}

	void PresentationLinkService::RevokeRoute()
	{
		if (route_) { route_.reset(); ++nextRoute_; }
		ExpireCommands();
	}
	void PresentationLinkService::Prepare(const Choice& choice, SelectionReason reason, bool hard)
	{
		if (pending_ && pending_->target.sourceId == choice.sourceId && pending_->target.epoch == choice.epoch
			&& pending_->target.candidateId == choice.candidateId && pending_->target.generation == choice.generation
			&& pending_->target.logicalShow == choice.logicalShow) return;
		if (hard) RevokeRoute();
		const auto* entry = Find(choice);
		BindingRequest request;
		request.requestId = ++nextRequest_;
		request.target = Route{ choice.sourceId, choice.candidateId, entry->value.localShowId,
			choice.epoch, choice.generation, choice.logicalShow, 0, 0, 0 };
		request.activityRevision = entry->value.activity.observationRevision;
		request.foregroundRevision = foregroundRevision_;
		request.deadline = Deadline(now_, policy_.bindingTimeout); request.reason = reason;
		pending_ = request; bindingOutbox_ = request; challenge_.reset();
	}
	void PresentationLinkService::PublishCurrent()
	{
		if (!current_ || !route_ || !Available(*current_)) return;
		const auto& value = Find(*current_)->value;
		if (!published_ || published_->document != value.document || !SameTargetPage(published_->page, value.page))
			route_->targetRevision = ++nextTarget_;
		if (unprovenPageMapping_ && value.document && value.document->scope == IdentityScope::Document
			&& value.document->quality == IdentityQuality::Stable && value.page.identityQuality == IdentityQuality::Stable
			&& value.page.stableKey && published_ && value.page.revision > published_->page.revision) unprovenPageMapping_ = false;
		published_ = value;
	}

	void PresentationLinkService::Reconsider()
	{
		for (auto& [id, source] : sources_)
			if (source.connected && !Fresh(now_, source.lastSeen, policy_.sourceTimeout))
			{ source.connected = false; source.needsFull = true; source.entries.clear(); }
		if (pending_ && now_ >= pending_->deadline)
		{
			const Choice expired{ pending_->target.sourceId, pending_->target.candidateId,
				pending_->target.epoch, pending_->target.generation, pending_->target.logicalShow };
			if (auto* entry = Find(expired)) entry->blockedRevision = entry->value.stateRevision;
			pending_.reset(); bindingOutbox_.reset();
		}
		if (current_ && !Available(*current_)) RevokeRoute();
		PublishCurrent(); ExpireCommands();
		const auto best = BestChoice();
		// 已确认真实前台转移时，B 未就绪也不能把命令继续发给后台 A。
		if (foreground_ && (foregroundInkeys_ || Fresh(now_, foregroundAt_, policy_.activityFreshness)))
		{
			if (!best || Tier(best->logicalShow) != 3)
			{
				RevokeRoute(); pending_.reset(); bindingOutbox_.reset(); challenge_.reset(); return;
			}
			if (current_ && current_->logicalShow != best->logicalShow) RevokeRoute();
		}
		if (pending_)
		{
			const Choice pendingChoice{ pending_->target.sourceId, pending_->target.candidateId,
				pending_->target.epoch, pending_->target.generation, pending_->target.logicalShow };
			// 强前台证据消失后，即使同一候选仍是弱提示赢家，也须重新经过弱观察门。
			if (!best || *best != pendingChoice || !Available(pendingChoice) || now_ >= pending_->deadline
				|| (pending_->reason == SelectionReason::Foreground && Tier(pendingChoice.logicalShow) != 3))
			{ pending_.reset(); bindingOutbox_.reset(); }
			else return;
		}
		if (!best) { challenge_.reset(); return; }
		if (current_ && *best == *current_ && route_) { challenge_.reset(); return; }
		if (!current_ || !Available(*current_) || (!route_ && *best == *current_))
		{
			// Busy/Unknown 仅短期保留上下文，期间已禁命令；明确结束/失联不宽限。
			const auto* uncertain = current_ ? Find(*current_) : nullptr;
			if (uncertain && uncertain->uncertain && Fresh(now_, uncertain->uncertainSince, policy_.busyGrace)
				&& Tier(best->logicalShow) < 3 && best->logicalShow != current_->logicalShow) return;
			Prepare(*best, current_ ? SelectionReason::Failover : SelectionReason::Initial, true); return;
		}
		if (Tier(best->logicalShow) == 3 && best->logicalShow != current_->logicalShow)
		{
			Prepare(*best, SelectionReason::Foreground, true); return;
		}
		const bool sourceOnly = best->logicalShow == current_->logicalShow;
		if (!sourceOnly && Tier(best->logicalShow) <= Tier(current_->logicalShow)) { challenge_.reset(); return; }
		const auto* entry = Find(*best);
		const auto evidence = ActivitySource(best->logicalShow);
		const bool foregroundEvidence = !evidence && foregroundProcess_ && Tier(best->logicalShow) == 2;
		const auto evidenceChoice = evidence.value_or(*best);
		const auto observation = foregroundEvidence ? foregroundEvidenceRevision_ : Find(evidenceChoice)->value.activity.observationRevision;
		if (!challenge_ || challenge_->choice != *best || challenge_->sourceOnly != sourceOnly
			|| challenge_->evidence != evidenceChoice || challenge_->foregroundEvidence != foregroundEvidence)
			challenge_ = Challenge{ *best, now_, observation, 1, sourceOnly, evidenceChoice, foregroundEvidence };
		else if (observation > challenge_->observation)
		{
			challenge_->observation = observation;
			if (challenge_->samples < policy_.weakSamples) ++challenge_->samples;
		}
		const Tick window = sourceOnly ? policy_.sourceRecoveryWindow
			: best->logicalShow == previousSoftShow_ ? std::max(policy_.weakWindow, policy_.reverseWindow) : policy_.weakWindow;
		if (Fresh(now_, challenge_->since, window) || (haveSoftSwitch_ && Fresh(now_, lastSoftSwitch_, policy_.softSwitchInterval))) return;
		if (sourceOnly)
		{
			if (Fresh(now_, entry->availableSince, policy_.sourceRecoveryWindow)) return;
		}
		else if (challenge_->samples < policy_.weakSamples) return;
		Prepare(*best, sourceOnly ? SelectionReason::SourceQuality : SelectionReason::WeakActivity, false);
	}

	bool PresentationLinkService::Advance(Tick now)
	{
		if (!SetNow(now)) return false;
		Reconsider(); return true;
	}
	bool PresentationLinkService::Confirm(const BindingConfirmation& confirmation, Tick now)
	{
		if (!Advance(now) || !pending_ || confirmation.requestId != pending_->requestId
			|| confirmation.target != pending_->target) return false;
		const auto request = *pending_;
		const Choice choice{ request.target.sourceId, request.target.candidateId, request.target.epoch,
			request.target.generation, request.target.logicalShow };
		const auto* entry = Find(choice);
		if (!confirmation.success)
		{
			if (auto* failed = Find(choice)) failed->blockedRevision = failed->value.stateRevision;
			pending_.reset(); bindingOutbox_.reset(); challenge_.reset(); Reconsider(); return false;
		}
		if (!entry || !Available(choice) || confirmation.snapshot != entry->value) return false;
		const bool sameShow = current_ && current_->logicalShow == choice.logicalShow;
		const bool mappedPage = sameShow && published_ && SamePage(*published_, entry->value);
		const bool sourceChanged = current_ && (current_->sourceId != choice.sourceId || current_->epoch != choice.epoch);
		const bool stableIdentity = entry->value.page.identityQuality == IdentityQuality::Stable && entry->value.page.stableKey
			&& entry->value.document && entry->value.document->scope == IdentityScope::Document
			&& entry->value.document->quality == IdentityQuality::Stable;
		unprovenPageMapping_ = sameShow && !mappedPage && !stableIdentity && (unprovenPageMapping_ || sourceChanged);
		const auto session = sameShow ? nextSession_ : ++nextSession_;
		if (!mappedPage) ++nextTarget_;
		Route route = request.target; route.showSessionRevision = session;
		route.routeRevision = ++nextRoute_; route.targetRevision = nextTarget_;
		if (request.reason == SelectionReason::WeakActivity || request.reason == SelectionReason::SourceQuality)
		{
			previousSoftShow_ = current_ ? current_->logicalShow : 0;
			lastSoftSwitch_ = now_; haveSoftSwitch_ = true;
		}
		current_ = choice; route_ = route; published_ = entry->value; reason_ = request.reason;
		pending_.reset(); bindingOutbox_.reset(); challenge_.reset(); ExpireCommands();
		return true;
	}
	SelectedSnapshot PresentationLinkService::Selected() const
	{
		SelectedSnapshot result;
		result.state = pending_ ? SelectionState::Pending : route_ ? SelectionState::Stable : SelectionState::Unknown;
		result.reason = pending_ ? pending_->reason : reason_;
		if (current_) result.activityTier = Tier(current_->logicalShow);
		if (route_ && current_ && Available(*current_))
		{
			result.route = route_; result.candidate = published_;
			result.capabilities = sources_.at(current_->sourceId).info.capabilities;
			result.pageSafe = !unprovenPageMapping_ && published_ && SafePage(*published_);
		}
		return result;
	}
	std::optional<BindingRequest> PresentationLinkService::TakeBindingRequest()
	{
		auto request = bindingOutbox_; bindingOutbox_.reset(); return request;
	}
	std::size_t PresentationLinkService::CandidateCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& [id, source] : sources_) count += source.entries.size();
		return count;
	}
	bool PresentationLinkService::NeedsFull(const std::string& id) const
	{
		const auto found = sources_.find(id); return found == sources_.end() || found->second.needsFull;
	}

	void PresentationLinkService::TrimCommandHistory()
	{
		while (commands_.size() >= limits_.commandHistory)
		{
			auto expired = std::find_if(commandOrder_.begin(), commandOrder_.end(),
				[&](Revision id) { return Terminal(commands_.at(id).status); });
			if (expired == commandOrder_.end()) return;
			commands_.erase(*expired); commandOrder_.erase(expired);
		}
	}
	void PresentationLinkService::ExpireCommands()
	{
		for (auto& [id, record] : commands_)
		{
			if (Terminal(record.status)) continue;
			if (!route_ || record.command.target != *route_ || now_ >= record.command.deadline)
				record.status = record.status == CommandStatus::AwaitingResult ? CommandStatus::OutcomeUnknown : CommandStatus::RejectedStale;
		}
		std::erase_if(commandOutbox_, [&](const Command& command)
		{ const auto found = commands_.find(command.commandId); return found == commands_.end() || found->second.status != CommandStatus::Queued; });
	}
	CommandStatus PresentationLinkService::SubmitCommand(const CommandRequest& request, Tick now)
	{
		if (!Advance(now)) return CommandStatus::RejectedStale;
		if (request.commandId == 0 || request.commandId <= lastCommandId_) return CommandStatus::Duplicate;
		if (!route_ || !current_ || !Available(*current_) || request.logicalShow != route_->logicalShow
			|| request.routeRevision != route_->routeRevision || request.targetRevision != route_->targetRevision) return CommandStatus::RejectedStale;
		const auto& source = sources_.at(route_->sourceId).info;
		Command command{ request.commandId, *route_, request.deadline, request.operation, request.gotoOrdinal, request.expectedPage };
		ExecutionLedger ledger{ source.sourceId, route_->epoch, 0 };
		const auto guarded = GuardCommand(command, *route_, Find(*current_)->value, source, now_, ledger);
		if (guarded != CommandStatus::Queued) return guarded;
		std::size_t active = 0;
		for (const auto& [id, record] : commands_) if (!Terminal(record.status)) ++active;
		TrimCommandHistory();
		if (active >= limits_.commands || commands_.size() >= limits_.commandHistory) return CommandStatus::Capacity;
		lastCommandId_ = request.commandId;
		commands_.emplace(command.commandId, CommandRecord{ command, CommandStatus::Queued });
		commandOrder_.push_back(command.commandId); commandOutbox_.push_back(std::move(command));
		return CommandStatus::Queued;
	}
	std::vector<Command> PresentationLinkService::TakeCommands()
	{
		ExpireCommands();
		std::vector<Command> result;
		for (const auto& command : commandOutbox_)
		{
			auto& record = commands_.at(command.commandId);
			if (record.status != CommandStatus::Queued) continue;
			if (!route_ || !current_ || !Available(*current_)) { record.status = CommandStatus::RejectedStale; continue; }
			const auto& source = sources_.at(route_->sourceId).info;
			// 此临时 ledger 只复核提取时的合同，不消耗实际执行 owner 独立持有的去重高水位。
			ExecutionLedger ledger{ source.sourceId, route_->epoch, 0 };
			const auto guarded = GuardCommand(command, *route_, Find(*current_)->value, source, now_, ledger);
			if (guarded != CommandStatus::Queued) { record.status = guarded; continue; }
			record.status = CommandStatus::AwaitingResult; result.push_back(command);
		}
		commandOutbox_.clear(); return result;
	}
	bool PresentationLinkService::ReceiveCommandResult(const CommandResult& result, Tick now)
	{
		if (!Advance(now)) return false;
		auto record = commands_.find(result.commandId);
		if (record == commands_.end() || record->second.status != CommandStatus::AwaitingResult
			|| record->second.command.target != result.target || !route_ || *route_ != result.target
			|| (result.status != CommandStatus::Executed && result.status != CommandStatus::RejectedStale
				&& result.status != CommandStatus::Unsupported && result.status != CommandStatus::Failed
				&& result.status != CommandStatus::OutcomeUnknown)) return false;
		record->second.status = result.status; return true;
	}
	std::optional<CommandStatus> PresentationLinkService::CommandOutcome(Revision id) const
	{
		const auto found = commands_.find(id);
		return found == commands_.end() ? std::nullopt : std::optional<CommandStatus>(found->second.status);
	}
}
