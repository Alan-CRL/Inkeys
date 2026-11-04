#include "../../../../Inkeys/Inkeys/Window/Window.CandidateMailbox.h"
#include "../../../../Inkeys/Inkeys/Window/Window.PresentationLease.h"
#include "../../../../Inkeys/Inkeys/Window/Window.CommandCompletion.h"
#include "../../../../Inkeys/Inkeys/Window/Window.NativeEventMailbox.h"
#include <cstdio>

int main()
{
	using Mailbox = Inkeys::Window::Detail::CandidateMailbox<int, int, 4>;
	Mailbox box;
	int failures = 0;
	const auto check = [&](bool result, const char* name) { if (!result) { ++failures; std::printf("FAIL %s\n", name); } };
	const auto a = box.Publish(0, 1, 10);
	const auto b = box.Publish(0, 1, 20);
	check(a.revision && b.revision > a.revision && b.coalesced && b.depth == 1, "coalescing keeps one fixed slot");
	const auto latest = box.Take(0);
	check(latest && latest->candidate == 20 && box.Current(*latest) && !box.Take(0), "latest wins instead of playing history");
	const auto newer = box.Publish(0, 1, 30);
	check(!box.Current(*latest) && !box.Complete(*latest, 20) && !box.TryRead(0, b.revision), "late completion cannot overwrite new candidate");
	const auto pair = box.Publish(1, 6, 40);
	const auto inFlightPair = box.Take(1);
	check(inFlightPair && box.Current(*inFlightPair), "pair begins with both role revisions");
	check(box.Publish(2, 4, 50).revision && !box.Current(*inFlightPair), "one newer member cancels entire pair");
	box.Cancel(0, newer.revision);
	const auto cancelled = box.Take(0);
	check(cancelled && !box.Current(*cancelled), "cancelled queued candidate cannot execute");
	box.Cancel(2, pair.revision);
	const auto surviving = box.Take(2);
	check(surviving && box.Current(*surviving), "old cancellation cannot cancel newer revision");
	check(box.Complete(*surviving, 50) && box.TryRead(2, surviving->revision) == 50, "exact receipt only");
	check(box.Publish(1, 6, 70).revision && !box.TryRead(2, surviving->revision), "new pair invalidates prior singleton receipt");
	const auto oldReceipt = box.Publish(3, 8, 80);
	const auto beforePolicy = box.Take(3);
	check(beforePolicy && box.Complete(*beforePolicy, 80), "native state begins with complete receipt");
	box.InvalidateRoles(8);
	check(!box.Current(*beforePolicy) && !box.TryRead(3, oldReceipt.revision), "policy native mutation retires prior candidate receipt");
	const auto afterPolicy = box.Publish(3, 8, 90);
	check(afterPolicy.revision && box.Current(3, afterPolicy.revision), "new candidate can reconcile changed policy state");
	check(!Inkeys::Window::Detail::CandidatePumpHasWork(2, 2), "blocked recovery slot does not self-wake");
	check(Inkeys::Window::Detail::CandidatePumpHasWork(6, 2), "another free candidate slot can drain during recovery");
	box.Reset();
	check(!box.Current(*surviving) && !box.TryRead(2, surviving->revision), "reset invalidates old epoch");
	check(box.Publish(2, 4, 60).revision > surviving->revision, "reset never reuses revision");
	std::atomic<std::uint64_t> leases = 0;
	check(Inkeys::Window::Detail::TryAcquirePresentationBits(leases, 3), "atomic pair lease");
	check(!Inkeys::Window::Detail::TryAcquirePresentationBits(leases, 2), "overlapping renderer cannot acquire native pair lease");
	check(Inkeys::Window::Detail::TryAcquirePresentationBits(leases, 4), "unrelated renderer continues");
	Inkeys::Window::Detail::ReleasePresentationBits(leases, 3);
	check(leases.load() == 4, "retired pair releases only its own roles");
	Inkeys::Window::Detail::ReleasePresentationBits(leases, 4);
	const auto waiting = box.Publish(0, 3, 100);
	const auto deferred = box.Take(0); box.Defer(*deferred);
	const auto replacement = box.Publish(0, 3, 200); box.Defer(*deferred);
	const auto resumed = box.Take(0);
	check(waiting.revision && replacement.coalesced && resumed && resumed->candidate == 200 && !box.Take(0), "lease-blocked geometry coalesces without replay");
	Inkeys::Window::Detail::CommandCompletion expired(std::chrono::steady_clock::now() - std::chrono::milliseconds(1));
	check(expired.Phase() == Inkeys::Window::Detail::CommandPhase::Expired && !expired.Begin(), "async queued expiry needs no Wait or owner Begin");
	Inkeys::Window::Detail::CommandCompletion errors({});
	errors.RecordNativeFailure(5); errors.RecordNativeFailure(1223); errors.RecordRollbackFailure(31);
	check(errors.NativeError() == 5 && errors.RollbackError() == 31, "original native error survives rollback error");
	const auto committedMove = box.Publish(3, 8, 300);
	const auto nativeMove = box.Take(3);
	check(nativeMove && box.TryCommit(*nativeMove, [] { return true; }), "native transaction wins commit boundary");
	box.Cancel(3, committedMove.revision); // 模拟 Commit 已完成、回执尚未发布的取消。
	check(box.Complete(*nativeMove, 300) && box.TryRead(3, committedMove.revision) == 300,
		"late cancellation preserves physically committed unread move");
	{
		using Events = Inkeys::Window::Detail::NativeEventMailbox<std::array<std::uint64_t, 3>>;
		Events events; std::uint64_t previous = 0;
		for (std::uint64_t i = 1; i <= 10000; ++i)
		{
			const auto revision = events.Publish({ i % 4, i, i ^ 0x5a5a });
			check(revision != 0, "owner-delayed event mailbox always admits serialized pointer down");
			if (previous) check(!events.Current(previous), "newest pointer down wins across role order");
			previous = revision;
		}
		const auto latestEvent = events.Take();
		check(latestEvent && latestEvent->payload[1] == 10000 && latestEvent->payload[2] == (10000 ^ 0x5a5a)
			&& !events.Take(), "owner receives exact latest fixed identity payload once");
		events.Invalidate(); check(!events.Current(previous), "shutdown invalidates accepted native event identity");
	}
	{
		Mailbox retired;
		const auto request = retired.Publish(0, 1, 1); const auto work = retired.Take(0);
		retired.RequestCancel(0, request.revision);
		int nativeCommits = 0;
		check(!retired.TryCommit(*work, [&] { ++nativeCommits; return true; }) && nativeCommits == 0,
			"cancellation before commit forces owner retirement instead of success");
		check(!retired.Defer(*work), "cancellation during lease deferral cannot lose retirement responsibility");
		check(retired.Complete(*work, -1) && retired.TryReadRetirement(0, request.revision) == -1 && !retired.TryRead(0, request.revision),
			"cancelled terminal geometry is readable only through retirement API");
		const auto next = retired.Publish(0, 1, 2); const auto committed = retired.Take(0);
		check(retired.TryCommit(*committed, [] { return true; }), "native transaction wins atomic commit boundary");
		retired.Cancel(0, next.revision); retired.RequestCancel(0, next.revision);
		check(retired.Complete(*committed, 2) && retired.TryRead(0, next.revision) == 2,
			"late cancellation preserves physically committed unread move");
	}
	std::printf("WindowCandidateProbe failures=%d\n", failures);
	return failures ? 1 : 0;
}
