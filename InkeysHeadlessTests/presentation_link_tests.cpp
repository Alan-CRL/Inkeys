#include "../Inkeys/Inkeys/Business/PresentationLinkService.h"

#include <iostream>
#include <map>
#include <stdexcept>

namespace
{
	using namespace Inkeys::Business::PresentationLink;
	Capabilities Caps()
	{
		return static_cast<Capabilities>(Capability::ObserveLifecycle)
			| static_cast<Capabilities>(Capability::ObservePage)
			| static_cast<Capabilities>(Capability::PageCount)
			| static_cast<Capabilities>(Capability::StableDocumentIdentity)
			| static_cast<Capabilities>(Capability::StablePageIdentity)
			| static_cast<Capabilities>(Capability::PageEvents)
			| static_cast<Capabilities>(Capability::Next)
			| static_cast<Capabilities>(Capability::Previous)
			| static_cast<Capabilities>(Capability::Goto)
			| static_cast<Capabilities>(Capability::End)
			| static_cast<Capabilities>(Capability::Activate);
	}
	Policy TestPolicy()
	{
		// 仅测试用时间单位；不是 Win7/Office 实测或产品阈值。
		return Policy{ 10000, 10000, 10000, 10, 40, 30, 15, 50, 20, 3 };
	}
	Candidate Show(std::string id, std::uint32_t instance = 1)
	{
		Candidate value;
		value.candidateId = std::move(id); value.localShowId = "local-show";
		value.generation = 1; value.stateRevision = 1;
		value.lifecycle = Lifecycle::Active; value.quality = Quality::Valid;
		value.document = Document{ "document-key", IdentityScope::Document, IdentityQuality::Stable,
			std::string("C:\\课件\\演示.pptx"), std::string("演示") };
		value.show = ShowIdentity{ 1, 42, 123456, 100 + instance,
			"lifetime-" + std::to_string(instance), true, true };
		value.page = Page{ PageKind::Slide, 1, 3, std::string("slide-1"), IdentityQuality::Stable, 1, 1,
			{ "slide-1", "slide-2", "slide-3" } };
		value.activity = Activity{ TriState::No, false, false, 1 };
		return value;
	}
	void PageTo(Candidate& value, std::uint32_t ordinal)
	{
		++value.stateRevision; ++value.page.revision;
		value.page.ordinal = ordinal; value.page.stableKey = "slide-" + std::to_string(ordinal);
	}
	void Active(Candidate& value, TriState activity)
	{
		++value.stateRevision; ++value.activity.observationRevision; value.activity.providerActive = activity;
	}
	struct FakeAdapter
	{
		SourceInfo info;
		Revision epoch = 1, sequence = 0;
		std::map<std::string, Candidate> latest;
		ExecutionLedger ledger;
		explicit FakeAdapter(std::string id, int preference = 0)
		{
			info = SourceInfo{ std::move(id), "PptCOM", "PowerPoint", "fake", "ordinary", 1,
				Quality::Valid, Caps(), ControlMode::BrokerSelect, preference };
			ledger = { info.sourceId, epoch, 0 };
		}
		ReceiveResult Register(PresentationLinkService& service, Tick now = 0)
		{
			return service.RegisterSource(info, epoch, now);
		}
		ReceiveResult Send(PresentationLinkService& service, std::vector<Candidate> values,
			Tick now = 0, MessageKind kind = MessageKind::Delta, std::vector<Removal> removed = {})
		{
			DirectoryMessage message{ Envelope{ 1, 0, kind, "host", info.sourceId, epoch, ++sequence },
				std::nullopt, values, std::move(removed) };
			const auto result = service.Receive(message, now);
			if (result == ReceiveResult::Accepted && kind != MessageKind::Heartbeat)
			{
				if (kind == MessageKind::Full) latest.clear();
				for (const auto& value : values) latest[value.candidateId] = value;
				for (const auto& removal : message.removed) latest.erase(removal.candidateId);
			}
			return result;
		}
		ReceiveResult Heartbeat(PresentationLinkService& service, Tick now)
		{
			return Send(service, {}, now, MessageKind::Heartbeat);
		}
		BindingConfirmation Confirmation(const BindingRequest& request, bool success = true) const
		{
			return { request.requestId, request.target, success, latest.at(request.target.candidateId) };
		}
		bool Bind(PresentationLinkService& service, Tick now = 0)
		{
			const auto request = service.TakeBindingRequest();
			return request && request->target.sourceId == info.sourceId && service.Confirm(Confirmation(*request), now);
		}
	};
	ForegroundObservation Foreground(Revision revision, const Candidate& value)
	{
		return { revision, ForegroundKind::External, value.show, std::nullopt, false };
	}
	CommandRequest Request(Revision id, const SelectedSnapshot& selected, Tick deadline = 100)
	{
		CommandRequest request;
		request.commandId = id; request.logicalShow = selected.route->logicalShow;
		request.routeRevision = selected.route->routeRevision; request.targetRevision = selected.route->targetRevision;
		request.deadline = deadline; request.expectedPage = selected.candidate->page;
		return request;
	}
	struct Checks
	{
		int failures = 0, count = 0;
		void operator()(bool good, const char* label)
		{
			++count;
			if (!good) { ++failures; std::cerr << "FAIL presentation-link: " << label << '\n'; }
		}
	};
}

int RunPresentationLinkTests()
{
	using namespace Inkeys::Business::PresentationLink;
	Checks check;
	{
		bool rejected = false;
		try { PresentationLinkService invalid("host", {}); }
		catch (const std::invalid_argument&) { rejected = true; }
		check(rejected, "policy has no implicit production timing defaults");
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a");
		check(adapter.Register(service) == ReceiveResult::Accepted, "explicit source registration");
		auto a = Show("a");
		check(adapter.Send(service, { a }, 0, MessageKind::Full) == ReceiveResult::Accepted && adapter.Bind(service), "full valid Unicode candidate confirmed");
		const auto original = service.Selected();
		auto valid = a; PageTo(valid, 2); auto bad = Show("bad"); bad.page.ordinal = 0;
		check(adapter.Send(service, { valid, bad }, 1) == ReceiveResult::Invalid
			&& service.Selected().candidate == original.candidate && service.CandidateCount() == 1, "invalid second candidate cannot partially publish first");
		--adapter.sequence;
		check(adapter.Send(service, { valid }, 1) == ReceiveResult::Accepted, "invalid batch did not consume sequence");
		bad = valid; bad.candidateId = "bad"; bad.page.kind = static_cast<PageKind>(99);
		check(adapter.Send(service, { bad }, 2) == ReceiveResult::Invalid, "invalid enum rejected"); --adapter.sequence;
		bad = valid; bad.candidateId = std::string("bad\xc0\xaf", 5);
		check(adapter.Send(service, { bad }, 2) == ReceiveResult::Invalid, "malformed UTF-8 rejected"); --adapter.sequence;
		bad = valid; bad.candidateId = "bad"; bad.page.topology[1] = bad.page.topology[0];
		check(adapter.Send(service, { bad }, 2) == ReceiveResult::Invalid, "duplicate topology rejected"); --adapter.sequence;
		bad = valid; bad.candidateId = "bad"; bad.page.total = 10001;
		check(adapter.Send(service, { bad }, 2) == ReceiveResult::Invalid, "page upper bound validated before copying directory"); --adapter.sequence;
		auto unknown = valid; ++unknown.stateRevision; ++unknown.page.revision;
		unknown.page = Page{ PageKind::Unknown, {}, {}, {}, IdentityQuality::Unknown, unknown.page.revision, 0, {} };
		unknown.document.reset();
		check(adapter.Send(service, { unknown }, 2) == ReceiveResult::Accepted && !service.Selected().pageSafe
			&& !service.Selected().candidate->page.ordinal, "unknown optional fields retained without fabricating first or last slide");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); a.generation = 7;
		check(adapter.Send(service, { a }) == ReceiveResult::NeedFull, "delta cannot admit initial source");
		check(adapter.Send(service, { a }, 0, MessageKind::Full) == ReceiveResult::Accepted && adapter.Bind(service), "full recovers initial delta rejection");
		const auto old = service.Selected();
		DirectoryMessage replay{ Envelope{ 1, 0, MessageKind::Full, "host", "a", 1, adapter.sequence }, {}, { a }, {} };
		check(service.Receive(replay, 1) == ReceiveResult::OldSequence, "duplicate sequence ignored");
		++adapter.sequence;
		check(adapter.Heartbeat(service, 1) == ReceiveResult::Gap && service.NeedsFull("a") && !service.Selected().route, "sequence gap revokes route and requires full");
		auto obsolete = a; obsolete.generation = 6; obsolete.stateRevision = 100;
		check(adapter.Send(service, { obsolete }, 2, MessageKind::Full) == ReceiveResult::Invalid, "gap full cannot resurrect older candidate generation"); --adapter.sequence;
		check(adapter.Send(service, { a }, 2, MessageKind::Full) == ReceiveResult::Accepted && adapter.Bind(service, 2), "same generation full restores observations after gap");
		check(service.Selected().route->logicalShow != old.route->logicalShow
			&& service.Selected().route->showSessionRevision != old.route->showSessionRevision, "unobserved standalone show does not inherit old logical session");
		check(service.RegisterSource(adapter.info, 2, 3) == ReceiveResult::Accepted && !service.Selected().route, "explicit new epoch revokes old route");
		replay.envelope.sequence = 100;
		check(service.Receive(replay, 3) == ReceiveResult::OldEpoch
			&& service.RegisterSource(adapter.info, 1, 3) == ReceiveResult::OldEpoch, "old packet and retired epoch registration cannot restore old source");
		adapter.epoch = 2; adapter.sequence = 0;
		check(adapter.Send(service, { a }, 3, MessageKind::Full) == ReceiveResult::Accepted && adapter.Bind(service, 3), "new epoch must establish full and confirmation");
		check(!service.Advance(2), "owner clock cannot go backwards");
	}
	{
		auto policy = TestPolicy(); policy.sourceTimeout = 20; policy.pageFreshness = 10; policy.activityFreshness = 7;
		PresentationLinkService service("host", policy); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); a.activity.providerActive = TriState::Yes;
		(void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service);
		check(adapter.Heartbeat(service, 8) == ReceiveResult::Accepted && service.Selected().activityTier == 1, "heartbeat does not refresh expired activity");
		check(adapter.Heartbeat(service, 10) == ReceiveResult::Accepted && !service.Selected().route
			&& !service.Selected().pageSafe, "heartbeat does not refresh expired page");
		check(service.Advance(30) && service.NeedsFull("a") && service.CandidateCount() == 0, "source timeout clears admission and route");
		check(adapter.Heartbeat(service, 31) == ReceiveResult::NeedFull && service.NeedsFull("a"), "heartbeat cannot revive timed-out source");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b", 20);
		(void)a.Register(service); (void)b.Register(service);
		auto first = Show("a"); auto duplicate = Show("b"); duplicate.localShowId = "other-source-local";
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		const auto before = service.Selected();
		(void)b.Send(service, { duplicate }, 1, MessageKind::Full);
		check(!service.TakeBindingRequest() && service.Selected().route == before.route, "better source recovery has independent stability window");
		check(service.Advance(15) && !service.TakeBindingRequest(), "source recovery does not complete before window");
		check(service.Advance(16) && b.Bind(service, 16), "continuously better source prepares after recovery window");
		const auto after = service.Selected();
		check(after.route->logicalShow == before.route->logicalShow && after.route->showSessionRevision == before.route->showSessionRevision
			&& after.route->targetRevision == before.route->targetRevision && after.route->routeRevision > before.route->routeRevision,
			"reliable same show and stable page preserve logical session and target while route advances");
		check(service.TakeCommands().empty(), "passive source binding never emits Activate");
		check(service.Disconnect("b", 1, 17) && !service.Selected().route && a.Bind(service, 17), "current source failure bypasses recovery interval and reconfirms one backup");
		check(service.Selected().route->logicalShow == before.route->logicalShow, "continuous duplicate keeps logical show across source failure");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b", 1000);
		(void)a.Register(service); (void)b.Register(service);
		auto first = Show("a"); first.show.windowVerified = false;
		auto second = first; second.candidateId = "b";
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		const auto before = service.Selected();
		(void)b.Send(service, { second }, 1, MessageKind::Full);
		(void)service.Advance(100);
		check(!service.TakeBindingRequest() && service.Selected().route == before.route, "same file and unverified nonzero HWND/PID do not merge or backend-rank steal another show");
		check(!SamePage(Candidate{}, Candidate{}), "missing document cannot prove page mapping");
		first = Show("a"); second = Show("b"); first.page.stableKey.reset(); second.page.stableKey.reset();
		first.page.identityQuality = second.page.identityQuality = IdentityQuality::Ordinal;
		first.page.topology.clear(); second.page.topology.clear();
		check(!SamePage(first, second), "same ordinal does not prove cross-source page identity");
		first = Show("a"); second = Show("b"); first.document->scope = IdentityScope::SourceSession;
		check(!SamePage(first, second), "source-local document cannot prove cross-source page identity");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto first = Show("first", 1), second = Show("second", 2);
		(void)adapter.Send(service, { first, second }, 0, MessageKind::Full); (void)adapter.Bind(service);
		const auto initial = service.Selected();
		check(service.CandidateCount() == 2 && initial.route->candidateId == "first", "one source multiple shows starts with stable key");
		(void)service.ObserveForeground(Foreground(1, second), 1);
		check(!service.Selected().route && service.Selected().state == SelectionState::Pending && adapter.Bind(service, 1), "same PID distinct verified show switches through confirmation");
		check(service.Selected().route->logicalShow != initial.route->logicalShow, "same file and same PID distinct HWND get distinct logical sessions");
		const auto secondSession = service.Selected();
		Active(first, TriState::Yes); (void)adapter.Send(service, { first }, 2);
		check(!service.TakeBindingRequest() && service.Selected().route == secondSession.route, "late provider weak hint cannot override verified foreground show");
		check(service.ObserveForeground({ 2, ForegroundKind::Inkeys, {}, {}, false }, 3) && service.Selected().activityTier == 3,
			"Inkeys focus preserves last confirmed external context");
		auto ownFocus = Foreground(3, first); ownFocus.selfInduced = true;
		check(service.ObserveForeground(ownFocus, 4) && !service.TakeBindingRequest() && service.Selected().route == secondSession.route,
			"self-induced Focus cannot independently challenge external show");
		check(service.ObserveForeground(Foreground(4, first), 5) && adapter.Bind(service, 5), "next genuine other foreground bypasses soft cooldown");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a", 1), second = Show("b", 2);
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		(void)b.Send(service, { second }, 0, MessageKind::Full);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 1);
		check(!service.TakeBindingRequest(), "one weak hint begins observation only");
		PageTo(second, 2); (void)b.Send(service, { second }, 5);
		(void)b.Heartbeat(service, 11); (void)service.Advance(12);
		check(!service.TakeBindingRequest() && service.Selected().route->sourceId == "a", "page-only packets repeated activity revision and heartbeat add no independent sample");
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 13);
		check(!service.TakeBindingRequest(), "second sample still below configured independent count");
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 14);
		check(b.Bind(service, 14), "third independent sample with sustained advantage prepares weak switch");
		Active(second, TriState::No); (void)b.Send(service, { second }, 15);
		Active(first, TriState::Yes); (void)a.Send(service, { first }, 16);
		Active(first, TriState::Yes); (void)a.Send(service, { first }, 20);
		Active(first, TriState::Yes); (void)a.Send(service, { first }, 25);
		(void)service.Advance(46);
		check(!service.TakeBindingRequest(), "reverse observation cannot bypass soft-switch interval");
		check(service.Advance(54) && a.Bind(service, 54), "reverse challenge respects distinct reverse window and switch interval");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b"), duplicate("z", 20);
		(void)a.Register(service); (void)b.Register(service); (void)duplicate.Register(service);
		auto first = Show("a", 1), second = Show("b", 2), extra = Show("z", 2);
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		(void)b.Send(service, { second }, 0, MessageKind::Full); (void)duplicate.Send(service, { extra }, 0, MessageKind::Full);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 1);
		Active(extra, TriState::Yes); (void)duplicate.Send(service, { extra }, 2);
		(void)service.Advance(12);
		check(!service.TakeBindingRequest(), "duplicate reporting source does not count as second activity vote");
		Active(second, TriState::No); (void)b.Send(service, { second }, 13);
		Active(extra, TriState::No); (void)duplicate.Send(service, { extra }, 13);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 14);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 15);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 16);
		check(!service.TakeBindingRequest(), "loss of advantage cancels observation duration and sample accumulation");
		check(service.Advance(24) && duplicate.Bind(service, 24), "activity evidence can come from one source while authoritative snapshot chosen from another");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a", 1), b = Show("b", 2), c = Show("c", 3);
		(void)adapter.Send(service, { a, b, c }, 0, MessageKind::Full); (void)adapter.Bind(service);
		(void)service.ObserveForeground(Foreground(1, b), 1); const auto prepareB = service.TakeBindingRequest();
		check(prepareB && !service.Selected().route, "strong switch disables old command route before preparation completes");
		(void)service.ObserveForeground(Foreground(2, c), 2); const auto prepareC = service.TakeBindingRequest();
		check(prepareC && prepareC->requestId != prepareB->requestId
			&& !service.Confirm(adapter.Confirmation(*prepareB), 2), "late B confirmation cannot transiently overwrite newer C intent");
		check(service.Confirm(adapter.Confirmation(*prepareC), 2) && service.Selected().route->candidateId == "c", "only current C confirmation commits");
		++b.stateRevision; b.quality = Quality::Busy; (void)adapter.Send(service, { b }, 3);
		(void)service.ObserveForeground(Foreground(3, b), 3);
		check(!service.Selected().route && !service.TakeBindingRequest() && service.Selected().state == SelectionState::Unknown,
			"verified foreground B busy disables A/C route even without usable B snapshot");
		++b.stateRevision; b.quality = Quality::Valid; (void)adapter.Send(service, { b }, 4);
		check(adapter.Bind(service, 4), "foreground B recovery must be confirmed before commands resume");
		(void)service.ObserveForeground(Foreground(4, a), 5); const auto failed = service.TakeBindingRequest();
		check(failed && !service.Confirm(adapter.Confirmation(*failed, false), 5), "owner preparation failure rejected");
		(void)service.Advance(6);
		check(!service.TakeBindingRequest() && !service.Selected().route, "failed unchanged snapshot cannot cause endless prepare retries");
		PageTo(a, 2); (void)adapter.Send(service, { a }, 7);
		check(adapter.Bind(service, 7), "new reliable state can retry previously failed binding");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a", 1), second = Show("b", 2);
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		(void)b.Send(service, { second }, 0, MessageKind::Full);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 1);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 2);
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 11); const auto pending = service.TakeBindingRequest();
		const auto beforePage = service.Selected(); PageTo(first, 2); (void)a.Send(service, { first }, 12);
		check(pending && service.Selected().route && service.Selected().candidate->page.ordinal == 2u
			&& service.Selected().route->targetRevision > beforePage.route->targetRevision, "weak preparation continues current page fast path without waiting for binding debounce");
		Active(second, TriState::No); (void)b.Send(service, { second }, 13);
		check(!service.Confirm(b.Confirmation(*pending), 13) && service.Selected().route->sourceId == "a", "challenge disadvantage cancels pending weak owner confirmation");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b", 10);
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a"), second = Show("b");
		first.page.stableKey.reset(); second.page.stableKey.reset(); first.page.topology.clear(); second.page.topology.clear();
		first.page.identityQuality = second.page.identityQuality = IdentityQuality::Ordinal;
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service); const auto before = service.Selected();
		(void)b.Send(service, { second }, 1, MessageKind::Full); (void)service.Advance(16); (void)b.Bind(service, 16);
		check(service.Selected().route->logicalShow == before.route->logicalShow
			&& service.Selected().route->targetRevision > before.route->targetRevision && !service.Selected().pageSafe,
			"unproven same-show ordinal mapping advances target and stays unsafe instead of inheriting ready");
		++second.stateRevision; ++second.page.revision;
		second.page = Page{ PageKind::Unknown, {}, 3, {}, IdentityQuality::Unknown, second.page.revision, 0, {} };
		(void)b.Send(service, { second }, 17);
		check(!service.Selected().pageSafe && service.Selected().candidate->page.kind == PageKind::Unknown, "black/white or unavailable slide remains explicit Unknown");
		++second.stateRevision; ++second.page.revision; second.page.kind = PageKind::EndScreen;
		(void)b.Send(service, { second }, 18);
		check(service.Selected().candidate->page.kind == PageKind::EndScreen && !service.Selected().pageSafe
			&& !service.Selected().candidate->page.ordinal, "explicit EndScreen is not fabricated final slide and does not claim input ready");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service);
		const auto first = service.Selected();
		check(service.SubmitCommand(Request(1, first), 1) == CommandStatus::Queued, "one confirmed route receives command");
		auto commands = service.TakeCommands();
		check(commands.size() == 1 && commands.front().target == *first.route, "single-route outbox contains full session/route/target identity");
		check(GuardCommand(commands.front(), *first.route, a, adapter.info, 1, adapter.ledger) == CommandStatus::Queued
			&& GuardCommand(commands.front(), *first.route, a, adapter.info, 1, adapter.ledger) == CommandStatus::Duplicate,
			"fake execution owner reuses guard and rejects duplicate non-idempotent command");
		check(service.SubmitCommand(Request(1, first), 2) == CommandStatus::Duplicate, "service high water rejects repeated command ID");
		const auto beforeResult = service.Selected();
		check(service.ReceiveCommandResult({ 1, commands.front().target, CommandStatus::Executed }, 2)
			&& service.Selected().route == beforeResult.route && service.Selected().candidate == beforeResult.candidate,
			"command ACK only updates result ledger, not binding/page");
		auto unsupported = Request(2, first); unsupported.operation = Operation::ExitAnnotation;
		check(service.SubmitCommand(unsupported, 3) == CommandStatus::Unsupported, "unsupported operation rejected");
		auto expired = Request(2, first, 3);
		check(service.SubmitCommand(expired, 3) == CommandStatus::RejectedStale, "deadline checked before enqueue");
		auto wrongPage = Request(2, first); wrongPage.expectedPage->ordinal = 2;
		check(service.SubmitCommand(wrongPage, 3) == CommandStatus::RejectedStale, "expected absolute page validated");
		auto invalidGoto = Request(2, first); invalidGoto.operation = Operation::Goto; invalidGoto.gotoOrdinal = 4;
		check(service.SubmitCommand(invalidGoto, 3) == CommandStatus::RejectedStale, "Goto target must fit observed page count");
		check(service.SubmitCommand(Request(2, first, 5), 3) == CommandStatus::Queued, "next unique command accepted");
		const auto unknownCommand = service.TakeCommands(); (void)service.Advance(5);
		check(service.CommandOutcome(2) == CommandStatus::OutcomeUnknown && service.TakeCommands().empty(), "dispatched timeout is OutcomeUnknown without automatic resend");
		check(!service.ReceiveCommandResult({ 2, unknownCommand.front().target, CommandStatus::Executed }, 5), "late ACK cannot replace unknown outcome");
		check(service.SubmitCommand(Request(3, first), 6) == CommandStatus::Queued, "queued command prior to page change");
		PageTo(a, 2); (void)adapter.Send(service, { a }, 7);
		check(service.CommandOutcome(3) == CommandStatus::RejectedStale && service.TakeCommands().empty(), "page target change cancels old queued command before drain");
		check(service.SubmitCommand(Request(4, first), 8) == CommandStatus::RejectedStale, "captured stale target request rejected");
		const auto current = service.Selected();
		check(service.SubmitCommand(Request(4, current), 8) == CommandStatus::Queued, "current target accepts next command");
		commands = service.TakeCommands();
		check(service.Disconnect("a", 1, 9) && service.CommandOutcome(4) == CommandStatus::OutcomeUnknown
			&& !service.ReceiveCommandResult({ 4, commands.front().target, CommandStatus::Executed }, 9), "disconnection after dispatch keeps unknown result and rejects old ACK");
		auto fakeRoute = *current.route; ++fakeRoute.generation;
		check(GuardCommand(commands.front(), fakeRoute, a, adapter.info, 9, adapter.ledger) == CommandStatus::RejectedStale, "execution guard independently rejects candidate generation change");
		adapter.info.controlMode = ControlMode::LegacyAutoSelect;
		check(GuardCommand(commands.front(), *current.route, a, adapter.info, 9, adapter.ledger) == CommandStatus::RejectedStale, "LegacyAutoSelect cannot execute BrokerSelect command");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a"), duplicate = Show("b");
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service); const auto old = service.Selected();
		(void)b.Send(service, { duplicate }, 0, MessageKind::Full);
		++first.stateRevision; first.lifecycle = Lifecycle::Ended;
		check(a.Send(service, { first }, 1) == ReceiveResult::Accepted && !service.Selected().route, "explicit verified Ended invalidates duplicate stale show observations");
		++duplicate.stateRevision;
		check(b.Send(service, { duplicate }, 2) == ReceiveResult::Invalid, "another source old lifetime cannot resurrect ended show"); --b.sequence;
		first = Show("a"); first.generation = 2; first.show.windowLifetime = "new-lifetime";
		check(a.Send(service, { first }, 2) == ReceiveResult::Accepted && a.Bind(service, 2)
			&& service.Selected().route->logicalShow != old.route->logicalShow, "same PID/HWND new lifetime receives new show session");
		check(a.Send(service, {}, 3, MessageKind::Delta, { { "a", 2 } }) == ReceiveResult::Accepted && !service.Selected().route, "explicit deletion revokes route");
		++first.stateRevision;
		check(a.Send(service, { first }, 4) == ReceiveResult::Invalid, "deleted candidate generation cannot revive through newer sequence");
	}
	{
		auto limits = Limits{}; limits.sources = 1; limits.candidatesPerSource = 1; limits.candidatesTotal = 1;
		limits.commands = 1; limits.commandHistory = 1;
		PresentationLinkService service("host", TestPolicy(), limits); FakeAdapter a("a"), b("b");
		check(a.Register(service) == ReceiveResult::Accepted && b.Register(service) == ReceiveResult::Capacity, "source directory bounded");
		auto first = Show("a"), second = Show("b", 2);
		check(a.Send(service, { first, second }, 0, MessageKind::Full) == ReceiveResult::Capacity && service.CandidateCount() == 0, "candidate capacity failure is atomic"); --a.sequence;
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service); const auto snapshot = service.Selected();
		check(service.SubmitCommand(Request(1, snapshot, 3), 1) == CommandStatus::Queued
			&& service.SubmitCommand(Request(2, snapshot), 1) == CommandStatus::Capacity, "command queue and outstanding record count bounded");
		(void)service.TakeCommands(); (void)service.Advance(3);
		check(service.SubmitCommand(Request(2, snapshot), 4) == CommandStatus::Queued && !service.CommandOutcome(1), "bounded terminal history evicted for new command");
		check(service.SubmitCommand(Request(1, snapshot), 4) == CommandStatus::Duplicate, "evicted command still rejected by monotonic ID high water");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a", 1), second = Show("b", 2);
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		(void)service.ObserveForeground(Foreground(1, second), 1);
		check(!service.Selected().route && service.Selected().state == SelectionState::Unknown,
			"verified foreground with not-yet-full source revokes background route");
		(void)b.Send(service, { second }, 2, MessageKind::Full);
		check(b.Bind(service, 2), "full candidate for verified foreground can then prepare and confirm");
		DirectoryMessage downgrade{ Envelope{ 1, 0, MessageKind::Delta, "host", "b", 1, b.sequence + 1 }, b.info, {}, {} };
		downgrade.source->capabilities &= ~static_cast<Capabilities>(Capability::StablePageIdentity);
		const auto stable = service.Selected();
		check(service.Receive(downgrade, 3) == ReceiveResult::Invalid && service.Selected().route == stable.route
			&& service.Selected().capabilities == stable.capabilities, "capability downgrade cannot leave retained stable page contradictory to source metadata");
		auto left = Show("left"), right = Show("right");
		left.page.kind = right.page.kind = PageKind::EndScreen;
		left.page.ordinal.reset(); right.page.ordinal.reset(); left.page.stableKey.reset(); right.page.stableKey.reset();
		left.page.topology.clear(); right.page.topology.clear();
		check(!SamePage(left, right), "same page count alone cannot map EndScreen between sources");
		left.page.topology = right.page.topology = { "slide-1", "slide-2", "slide-3" };
		check(SamePage(left, right), "explicit end screen maps only with stable document and matching complete topology");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a", 1), second = Show("b", 2);
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		(void)b.Send(service, { second }, 0, MessageKind::Full);
		++first.stateRevision; first.quality = Quality::Busy; (void)a.Send(service, { first }, 1);
		check(!service.Selected().route && !service.TakeBindingRequest(), "busy current immediately disables commands but retains bounded show context");
		(void)service.Advance(20);
		check(!service.TakeBindingRequest(), "busy grace is independent of weak challenge window");
		check(service.Advance(21) && b.Bind(service, 21), "busy grace expiry allows reliable fallback with confirmation");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b", 1000);
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a", 1), second = Show("b", 2);
		(void)b.Send(service, { second }, 0, MessageKind::Full); const auto firstPending = service.TakeBindingRequest();
		(void)a.Send(service, { first }, 0, MessageKind::Full); const auto stablePending = service.TakeBindingRequest();
		check(firstPending && stablePending && stablePending->target.sourceId == "a"
			&& !service.Confirm(b.Confirmation(*firstPending), 0) && service.Confirm(a.Confirmation(*stablePending), 0),
			"initial multiple source order does not change stable key winner or permit old pending confirmation");
		(void)service.ObserveForeground(Foreground(1, second), 1); const auto oldEpoch = service.TakeBindingRequest();
		check(service.RegisterSource(b.info, 2, 2) == ReceiveResult::Accepted && oldEpoch
			&& !service.Confirm(b.Confirmation(*oldEpoch), 2), "pending owner confirmation rejected after explicit source epoch replacement");
	}
	{
		auto policy = TestPolicy(); policy.sourceTimeout = 10;
		PresentationLinkService service("host", policy); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a"), duplicate = Show("b");
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service); const auto old = service.Selected();
		(void)b.Send(service, { duplicate }, 10, MessageKind::Full);
		check(b.Bind(service, 10) && service.Selected().route->logicalShow != old.route->logicalShow,
			"expired old observer cannot seed identity continuity merely because no Advance happened before new source full");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full);
		const auto expired = service.TakeBindingRequest(); (void)service.Advance(50);
		check(expired && !service.Confirm(adapter.Confirmation(*expired), 50) && !service.TakeBindingRequest(), "binding timeout blocks unchanged state and rejects expired confirmation");
		PageTo(a, 2); (void)adapter.Send(service, { a }, 51);
		check(adapter.Bind(service, 51), "new state can prepare after timed-out owner confirmation");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service);
		check(service.SubmitCommand(Request(1, service.Selected()), 1) == CommandStatus::Queued, "queue command before source operation capability withdrawal");
		adapter.info.capabilities &= ~static_cast<Capabilities>(Capability::Next);
		DirectoryMessage metadata{ Envelope{ 1, 0, MessageKind::Delta, "host", "a", 1, ++adapter.sequence }, adapter.info, {}, {} };
		check(service.Receive(metadata, 2) == ReceiveResult::Accepted && service.TakeCommands().empty()
			&& service.CommandOutcome(1) == CommandStatus::Unsupported, "drain revalidates withdrawn operation capability before dispatch");
		adapter.info.capabilities |= static_cast<Capabilities>(Capability::Next);
		metadata.envelope.sequence = ++adapter.sequence; metadata.source = adapter.info; (void)service.Receive(metadata, 3);
		check(service.SubmitCommand(Request(2, service.Selected()), 3) == CommandStatus::Queued, "queue command before source control mode changes");
		adapter.info.controlMode = ControlMode::LegacyAutoSelect;
		metadata.envelope.sequence = ++adapter.sequence; metadata.source = adapter.info;
		check(service.Receive(metadata, 4) == ReceiveResult::Accepted && service.TakeCommands().empty()
			&& service.CommandOutcome(2) == CommandStatus::RejectedStale, "drain cannot dispatch BrokerSelect command after LegacyAutoSelect takeover");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter a("a"), b("b");
		(void)a.Register(service); (void)b.Register(service); auto first = Show("a", 1), second = Show("b", 2);
		(void)a.Send(service, { first }, 0, MessageKind::Full); (void)a.Bind(service);
		(void)b.Send(service, { second }, 0, MessageKind::Full); Active(second, TriState::Yes); (void)b.Send(service, { second }, 1);
		(void)service.ObserveForeground(Foreground(1, second), 2); const auto strong = service.TakeBindingRequest();
		(void)service.ObserveForeground({ 2, ForegroundKind::Unavailable, {}, {}, false }, 3);
		check(strong && !service.Confirm(b.Confirmation(*strong), 3) && !service.Selected().route,
			"lost exact foreground invalidates old strong confirmation even when same show remains weak best");
		check(service.Advance(13) && !service.TakeBindingRequest(), "downgraded strong candidate still requires independent weak samples");
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 14);
		check(!service.TakeBindingRequest(), "downgraded candidate second weak sample cannot reuse old strong preparation");
		Active(second, TriState::Yes); (void)b.Send(service, { second }, 15);
		check(b.Bind(service, 15) && service.Selected().activityTier == 2, "only sustained weak evidence can create a new confirmation after foreground loss");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service); const auto old = service.Selected();
		auto malformed = a; malformed.page.ordinal = 0;
		DirectoryMessage gap{ Envelope{ 1, 0, MessageKind::Full, "host", "a", 1, adapter.sequence + 2 }, {}, { malformed }, {} };
		check(service.Receive(gap, 1) == ReceiveResult::Invalid && service.NeedsFull("a") && !service.Selected().route,
			"valid gap envelope revokes route even if full payload is invalid");
		check(service.SubmitCommand(Request(1, old), 1) == CommandStatus::RejectedStale, "invalid gap payload cannot retain old executable route");
		check(adapter.Send(service, { a }, 2, MessageKind::Full) == ReceiveResult::Accepted && adapter.Bind(service, 2), "invalid gap recovery still needs full and owner confirmation");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service);
		DirectoryMessage gap{ Envelope{ 1, 0, MessageKind::Delta, "host", "a", 1, adapter.sequence + 2 }, adapter.info, {}, {} };
		gap.source->capabilities &= ~static_cast<Capabilities>(Capability::StablePageIdentity);
		check(service.Receive(gap, 1) == ReceiveResult::Invalid && service.NeedsFull("a") && !service.Selected().route,
			"invalid metadata on valid delta gap cannot retain old route");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service); const auto old = service.Selected();
		++a.stateRevision; a.lifecycle = Lifecycle::Inactive;
		check(adapter.Send(service, { a }, 1) == ReceiveResult::Accepted && !service.Selected().route, "explicit Inactive retires active candidate and route");
		++a.stateRevision; a.lifecycle = Lifecycle::Active;
		check(adapter.Send(service, { a }, 2) == ReceiveResult::Invalid, "same generation cannot reactivate explicitly retired candidate"); --adapter.sequence;
		++a.generation;
		check(adapter.Send(service, { a }, 2) == ReceiveResult::Accepted && adapter.Bind(service, 2)
			&& service.Selected().route->logicalShow != old.route->logicalShow, "new generation after inactive receives new logical session");
		const auto active = service.Selected();
		check(adapter.Send(service, {}, 3, MessageKind::Full) == ReceiveResult::Accepted && !service.Selected().route, "full omission explicitly retires candidate and revokes route");
		check(adapter.Send(service, { a }, 4) == ReceiveResult::Invalid && active.route->logicalShow != old.route->logicalShow, "omitted candidate cannot revive same generation");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); const auto old = service.TakeBindingRequest();
		++a.generation; ++a.stateRevision; (void)adapter.Send(service, { a }, 1);
		check(old && !service.Confirm(adapter.Confirmation(*old), 1) && adapter.Bind(service, 1), "candidate generation replacement rejects old preparation before new confirmation");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); a.stateRevision = 7; (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service);
		++adapter.sequence; (void)adapter.Heartbeat(service, 1);
		auto obsolete = a; obsolete.stateRevision = 6;
		check(adapter.Send(service, { obsolete }, 2, MessageKind::Full) == ReceiveResult::Invalid && service.NeedsFull("a"), "gap recovery preserves candidate state revision high water as well as generation");
	}
	{
		auto limits = Limits{}; limits.candidatesPerSource = 1; limits.candidateHistory = 1;
		PresentationLinkService service("host", TestPolicy(), limits); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service);
		(void)adapter.Send(service, {}, 1, MessageKind::Delta, { { "a", 1 } });
		check(adapter.Send(service, { Show("b", 2) }, 2, MessageKind::Full) == ReceiveResult::Capacity
			&& service.CandidateCount() == 0 && !service.Selected().route, "retired candidate history exhaustion rejects atomically instead of evicting replay protection");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto a = Show("a"); (void)adapter.Send(service, { a }, 0, MessageKind::Full); (void)adapter.Bind(service); const auto old = service.Selected();
		++a.stateRevision; a.lifecycle = Lifecycle::Unknown; (void)adapter.Send(service, { a }, 1);
		++a.stateRevision; a.lifecycle = Lifecycle::Inactive;
		check(adapter.Send(service, { a }, 2) == ReceiveResult::Accepted && !service.Selected().route, "explicit Inactive ends known session even after Unknown transition");
		++a.stateRevision; a.lifecycle = Lifecycle::Active;
		check(adapter.Send(service, { a }, 3) == ReceiveResult::Invalid, "Unknown to Inactive tombstone prevents same generation session resurrection"); --adapter.sequence;
		++a.generation;
		check(adapter.Send(service, { a }, 3) == ReceiveResult::Accepted && adapter.Bind(service, 3)
			&& service.Selected().route->logicalShow != old.route->logicalShow, "new generation after Unknown to Inactive starts new logical session");
	}
	{
		PresentationLinkService service("host", TestPolicy()); FakeAdapter adapter("a"); (void)adapter.Register(service);
		auto ended = Show("ended", 1), live = Show("live", 2); ended.generation = 2;
		(void)adapter.Send(service, { ended, live }, 0, MessageKind::Full); (void)adapter.Bind(service);
		(void)service.ObserveForeground(Foreground(1, live), 1); (void)adapter.Bind(service, 1);
		++ended.stateRevision; ended.lifecycle = Lifecycle::Ended;
		check(adapter.Send(service, { ended }, 2) == ReceiveResult::Accepted && service.Selected().route->candidateId == "live", "terminal candidate does not revoke a different live show");
		PageTo(live, 2);
		check(adapter.Send(service, { ended, live }, 3, MessageKind::Full) == ReceiveResult::Accepted
			&& service.Selected().candidate->page.ordinal == 2u && service.CandidateCount() == 2, "repeated terminal in full permits healthy candidate atomic refresh");
		const auto stable = service.Selected();
		check(adapter.Send(service, { ended, live }, 4, MessageKind::Full) == ReceiveResult::Accepted
			&& service.Selected().route == stable.route, "same generation terminal full repeat does not allocate or restore a show");
		auto resurrected = ended; ++resurrected.stateRevision; resurrected.lifecycle = Lifecycle::Active;
		check(adapter.Send(service, { resurrected, live }, 5, MessageKind::Full) == ReceiveResult::Invalid
			&& service.Selected().route == stable.route, "terminal repeat exception cannot revive same generation Active"); --adapter.sequence;
		resurrected.lifecycle = Lifecycle::Unknown;
		check(adapter.Send(service, { resurrected }, 5) == ReceiveResult::Invalid, "terminal repeat exception cannot restore same generation Unknown"); --adapter.sequence;
		auto older = ended; --older.generation;
		check(adapter.Send(service, { older }, 5) == ReceiveResult::Invalid, "older terminal generation remains rejected by tombstone"); --adapter.sequence;
		check(service.Disconnect("a", 1, 6) && adapter.Send(service, { ended, live }, 7, MessageKind::Full) == ReceiveResult::Accepted
			&& adapter.Bind(service, 7) && service.Selected().route->candidateId == "live", "terminal full after observation gap stays non-executable while live show reconfirms");
		check(adapter.Send(service, {}, 8, MessageKind::Delta, { { "ended", 2 } }) == ReceiveResult::Accepted
			&& adapter.Send(service, { ended, live }, 9, MessageKind::Full) == ReceiveResult::Accepted
			&& service.Selected().route->candidateId == "live", "deleted terminal can return as non-executable observation without defeating tombstone");
	}
	std::cout << "Presentation link contracts/arbitration: " << check.count << " checks, " << check.failures << " failures\n";
	return check.failures;
}
