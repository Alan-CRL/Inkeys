#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Inkeys::Business::PresentationLink
{
	using Tick = std::uint64_t;
	using Revision = std::uint64_t;
	enum class TriState { Unknown, No, Yes };
	enum class Lifecycle { Unknown, Inactive, Active, Ended };
	enum class Quality { Unavailable, Busy, Stale, Valid };
	enum class PageKind { Unknown, Slide, EndScreen };
	enum class IdentityScope { SourceSession, Document };
	enum class IdentityQuality { Unknown, Ordinal, Stable };
	enum class MessageKind { Full, Delta, Heartbeat };
	enum class ControlMode { LegacyAutoSelect, BrokerSelect };
	enum class Capability : std::uint32_t
	{
		ObserveLifecycle = 1u << 0, ObservePage = 1u << 1, PageCount = 1u << 2,
		StableDocumentIdentity = 1u << 3, StablePageIdentity = 1u << 4,
		PageEvents = 1u << 5, Next = 1u << 6, Previous = 1u << 7,
		Goto = 1u << 8, End = 1u << 9, Activate = 1u << 10,
		NativeAnnotation = 1u << 11
	};
	using Capabilities = std::uint32_t;
	constexpr bool HasCapability(Capabilities values, Capability value) noexcept
	{
		return (values & static_cast<Capabilities>(value)) != 0;
	}
	struct SourceInfo
	{
		std::string sourceId, backend, application, transport, observerDomain;
		std::uint32_t observerSession = 0;
		Quality health = Quality::Unavailable;
		Capabilities capabilities = 0;
		ControlMode controlMode = ControlMode::BrokerSelect;
		int backendPreference = 0;
		bool operator==(const SourceInfo&) const = default;
	};
	struct Envelope
	{
		std::uint16_t major = 1, minor = 0;
		MessageKind kind = MessageKind::Full;
		std::string hostId, sourceId;
		Revision epoch = 0, sequence = 0;
	};
	struct Document
	{
		std::string key;
		IdentityScope scope = IdentityScope::SourceSession;
		IdentityQuality quality = IdentityQuality::Unknown;
		std::optional<std::string> path, title;
		bool operator==(const Document&) const = default;
	};
	struct ShowIdentity
	{
		std::uint32_t session = 0, processId = 0;
		std::uint64_t processCreated = 0, window = 0;
		// 此 token 由可信 adapter 核验窗口生命周期；不是 source-local generation。
		std::string windowLifetime;
		bool processVerified = false, windowVerified = false;
		bool operator==(const ShowIdentity&) const = default;
	};
	struct Page
	{
		PageKind kind = PageKind::Unknown;
		std::optional<std::uint32_t> ordinal, total;
		std::optional<std::string> stableKey;
		IdentityQuality identityQuality = IdentityQuality::Unknown;
		Revision revision = 0, topologyRevision = 0;
		std::vector<std::string> topology;
		bool operator==(const Page&) const = default;
	};
	struct Activity
	{
		TriState providerActive = TriState::Unknown;
		bool processHint = false, wpsHint = false;
		Revision observationRevision = 0;
		bool operator==(const Activity&) const = default;
	};
	struct Candidate
	{
		std::string candidateId, localShowId;
		Revision generation = 0, stateRevision = 0;
		Lifecycle lifecycle = Lifecycle::Unknown;
		Quality quality = Quality::Unavailable;
		std::optional<Document> document;
		ShowIdentity show;
		Page page;
		Activity activity;
		bool operator==(const Candidate&) const = default;
	};
	struct Removal
	{
		std::string candidateId;
		Revision generation = 0;
	};
	struct DirectoryMessage
	{
		Envelope envelope;
		std::optional<SourceInfo> source;
		std::vector<Candidate> candidates;
		std::vector<Removal> removed;
	};
	enum class ForegroundKind { Unavailable, External, Inkeys };
	struct ForegroundObservation
	{
		Revision revision = 0;
		ForegroundKind kind = ForegroundKind::Unavailable;
		std::optional<ShowIdentity> show;
		std::optional<std::uint32_t> processHint;
		bool selfInduced = false;
	};
	// 时间单位由同一调用 owner 定义；P1 不配置任何产品时延初值。
	struct Policy
	{
		Tick sourceTimeout = 0, pageFreshness = 0, activityFreshness = 0;
		Tick weakWindow = 0, softSwitchInterval = 0, reverseWindow = 0;
		Tick sourceRecoveryWindow = 0, bindingTimeout = 0, busyGrace = 0;
		std::size_t weakSamples = 0;
	};
	struct Limits
	{
		std::size_t sources = 32, candidatesPerSource = 64, candidatesTotal = 256;
		std::size_t identityBytes = 32768, pages = 10000;
		std::size_t candidateHistory = 256, epochHistory = 128, retiredShows = 256;
		std::size_t commands = 64, commandHistory = 128;
		std::size_t valueBytes = 1048576, directoryBytes = 4194304;
	};
	struct Route
	{
		std::string sourceId, candidateId, localShowId;
		Revision epoch = 0, generation = 0, logicalShow = 0;
		Revision showSessionRevision = 0, routeRevision = 0, targetRevision = 0;
		bool operator==(const Route&) const = default;
	};
	enum class SelectionState { Unknown, Pending, Stable };
	enum class SelectionReason { None, Initial, Foreground, WeakActivity, SourceQuality, Failover };
	struct SelectedSnapshot
	{
		SelectionState state = SelectionState::Unknown;
		SelectionReason reason = SelectionReason::None;
		std::optional<Route> route;
		std::optional<Candidate> candidate;
		Capabilities capabilities = 0;
		int activityTier = 0;
		bool pageSafe = false;
	};
	struct BindingRequest
	{
		Revision requestId = 0;
		Route target;
		Revision activityRevision = 0, foregroundRevision = 0;
		Tick deadline = 0;
		SelectionReason reason = SelectionReason::None;
	};
	struct BindingConfirmation
	{
		Revision requestId = 0;
		Route target;
		bool success = false;
		Candidate snapshot;
	};
	enum class Operation { Next, Previous, Goto, End, Activate, ExitAnnotation };
	struct CommandRequest
	{
		Revision commandId = 0, logicalShow = 0, routeRevision = 0, targetRevision = 0;
		Tick deadline = 0;
		Operation operation = Operation::Next;
		std::optional<std::uint32_t> gotoOrdinal;
		std::optional<Page> expectedPage;
	};
	struct Command
	{
		Revision commandId = 0;
		Route target;
		Tick deadline = 0;
		Operation operation = Operation::Next;
		std::optional<std::uint32_t> gotoOrdinal;
		std::optional<Page> expectedPage;
	};
	enum class CommandStatus
	{
		Queued, AwaitingResult, Executed, RejectedStale, Unsupported,
		Failed, OutcomeUnknown, Duplicate, Capacity
	};
	struct CommandResult
	{
		Revision commandId = 0;
		Route target;
		CommandStatus status = CommandStatus::Failed;
	};
	// 执行 owner 每次接受新 epoch 后显式重置；高水位使历史淘汰后仍不能重放。
	struct ExecutionLedger
	{
		std::string sourceId;
		Revision epoch = 0, lastCommandId = 0;
	};
	enum class ReceiveResult { Accepted, Invalid, OldEpoch, OldSequence, NeedFull, Gap, Capacity };
	[[nodiscard]] bool IsReliableShow(const ShowIdentity& identity) noexcept;
	[[nodiscard]] bool SamePage(const Candidate& left, const Candidate& right) noexcept;
	[[nodiscard]] CommandStatus GuardCommand(const Command& command,
		const Route& currentRoute, const Candidate& currentCandidate,
		const SourceInfo& source, Tick now, ExecutionLedger& ledger) noexcept;
}
