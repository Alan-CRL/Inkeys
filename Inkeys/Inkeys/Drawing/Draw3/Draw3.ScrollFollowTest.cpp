#include "Draw3.ScrollFollowTest.h"
#include "Draw3.ScrollFollow.h"
#include "Draw3.Bridge.h"

#include <cstdio>
#include <limits>
#include <thread>

namespace Inkeys::Drawing::Draw3
{
	int RunScrollFollowProtocolTests() noexcept
	{
		int failures = 0;
		const auto check = [&](bool value, const char* label)
		{ if (!value) { ++failures; std::fprintf(stderr, "[ScrollFollowProtocol] FAIL %s\n", label); } };
		check(!Follow::OwnsCaptureSession(false, 0, 7) && !Follow::OwnsCaptureSession(false, 8, 7), "old reason before current Start is ignored");
		check(!Follow::OwnsCaptureSession(true, 8, 7) && Follow::OwnsCaptureSession(true, 8, 8), "capture reason and statistics require current successful Start");
		Follow::Channel channel;
		Follow::Identity identity;
		identity.host = 7; identity.scene = 11; identity.device = 13; identity.geometry = 17;
		identity.workspace[0] = 23; identity.page[0] = 29; identity.width = 800; identity.height = 600;
		check(channel.Begin(1) && channel.Blocked() && !channel.Begin(2), "begin owns admission");
		check(!channel.OwnerBegin(2, identity, 100, 200) && channel.OwnerBegin(1, identity, 100, 200), "owner baseline session");
		Follow::Target target{1, 1, identity, 30.25, -120.5};
		check(channel.Publish(target), "first absolute");
		check(!channel.Publish(target), "duplicate rejected");
		target.sequence = 2; target.identity.page[1] = 1;
		check(!channel.Publish(target), "page mismatch"); target.identity = identity;
		target.x = std::numeric_limits<double>::quiet_NaN(); check(!channel.Publish(target), "nonfinite"); target.x = 31.5;
		for (std::uint64_t sequence = 2; sequence <= 4096; ++sequence)
		{ target.sequence = sequence; target.y = -static_cast<double>(sequence); check(channel.Publish(target), "latest slot"); }
		check(channel.Pending(false)->sequence == 4096, "bounded latest wins");
		check(channel.Seal(Follow::Disposition::Restore), "end freezes final");
		target.sequence = 4097; check(!channel.Publish(target), "late frame rejected");
		channel.OwnerEnd(1); check(!channel.BarrierReady(), "processed is not ready");
		const auto final = channel.Pending(true);
		check(final && final->sequence == 4096, "end carries final");
		channel.Presented(1); check(channel.Read().phase == Follow::Phase::Ending, "unapplied cannot present");
		channel.Applied(1, 4096, 68.5, 4296, false);
		channel.Presented(2); check(channel.Read().phase == Follow::Phase::Ending, "late session present rejected");
		channel.Presented(1); check(!channel.BarrierReady(), "full present still waits pair ack");
		channel.Handoff(2); check(!channel.BarrierReady(), "old handoff rejected");
		channel.Handoff(1); check(channel.BarrierReady() && !channel.Blocked(), "normal complete");
		check(channel.Begin(2), "new session"); channel.OwnerEnd(1);
		check(channel.Read().phase == Follow::Phase::Beginning, "old end cannot mutate new begin");
		check(channel.OwnerBegin(2, identity, 0, 0), "second owner");
		check(channel.Seal(Follow::Disposition::Restore), "second seal");
		check(!channel.Seal(Follow::Disposition::Shutdown), "upgrade one reliable end");
		channel.OwnerEnd(2); check(channel.BarrierReady() && channel.Read().phase == Follow::Phase::Retired, "shutdown never waits visible ack");
		check(channel.Begin(3) && channel.Seal(Follow::Disposition::Restore), "cancel before owner begins");
		check(channel.OwnerBegin(3, identity, 9, 11) && !channel.Read().publishing, "late Begin stays sealed");
		channel.OwnerEnd(3); channel.Presented(3); channel.Handoff(3);
		check(channel.BarrierReady(), "zero target rollback completes normally");
		check(channel.Begin(4), "failed init session"); channel.Fail(4);
		check(channel.Seal(Follow::Disposition::Restore), "failed Begin still consumes reserved End");
		channel.OwnerEnd(4); check(channel.BarrierReady(), "failed owner terminal releases barrier");
		// 用真实 Bridge 验证满队列终态、前后 FIFO 与最终保存的独立固定槽。
		Bridge::StateBridge bridge(2);
		check(bridge.Publish(Bridge::CommandType::FollowBegin, 9) == Bridge::CommandResult::Accepted &&
			bridge.Publish(Bridge::CommandType::Undo) == Bridge::CommandResult::Accepted, "fill ordinary capacity");
		check(bridge.PublishFollowEnd(9), "full ordinary still accepts end");
		check(bridge.Publish(Bridge::CommandType::Redo) == Bridge::CommandResult::QueueFull, "ordinary full preserved");
		Bridge::Command command;
		check(bridge.TryConsume(command) && command.type == Bridge::CommandType::FollowBegin && command.followSession == 9, "begin first");
		check(bridge.Publish(Bridge::CommandType::Redo) == Bridge::CommandResult::Accepted, "later command accepted");
		check(bridge.StopWithFinalCommand(Bridge::CommandType::PrepareExitAutoSave), "exit final separate");
		check(bridge.TryConsume(command) && command.type == Bridge::CommandType::Undo, "accepted prior fifo");
		check(bridge.TryConsume(command) && command.type == Bridge::CommandType::FollowEnd && command.followSession == 9, "reliable end ordinal");
		check(bridge.TryConsume(command) && command.type == Bridge::CommandType::Redo, "accepted later fifo");
		check(bridge.TryConsume(command) && command.type == Bridge::CommandType::PrepareExitAutoSave && !bridge.TryConsume(command), "exit last once");
		Follow::Channel timed; timed.Begin(20); timed.OwnerBegin(20, identity, 0, 0);
		Follow::Target first{20, 1, identity, 0, -10}; first.readCompletedAt = std::chrono::steady_clock::now();
		check(timed.Publish(first), "timed publish"); const auto pending = timed.Pending(false);
		first.sequence = 2; check(timed.Publish(first), "newer target before apply");
		timed.Applied(20, 1, 0, 10, false, &*pending);
		check(timed.Read().appliedTiming.sequence == 1 && timed.Read().presentedTiming.sequence == 0, "apply not present");
		const auto returnedAt = std::chrono::steady_clock::now();
		timed.ObservePresent(19, returnedAt); check(timed.Read().presentedTiming.sequence == 0, "stale present session");
		timed.ObservePresent(20, returnedAt); const auto timing = timed.Read().presentedTiming;
		check(timing.sequence == 1 && timing.presentedAt == returnedAt && timing.publishedAt == pending->publishedAt && timing.readCompletedAt <= timing.publishedAt &&
			timing.publishedAt <= timing.appliedAt && timing.appliedAt <= timing.presentedAt, "timing belongs to applied target not overwritten latest");
		timed.ObservePresent(20, std::chrono::steady_clock::now()); check(timed.Read().presentedTiming.presentedAt == timing.presentedAt, "first successful present retained");
		// Stop 与晚发布共用短门；无论谁先拿到，final 都是最后成功提交的目标。
		for (unsigned race = 0; race < 64; ++race)
		{
			Follow::Channel raced; raced.Begin(1); raced.OwnerBegin(1, identity, 0, 0);
			std::jthread writer([&] { for (unsigned i = 1; i <= 128; ++i) (void)raced.Publish({1, i, identity, double(i), -double(i)}); });
			raced.Seal(Follow::Disposition::Restore); writer.join();
			const auto value = raced.Read();
			check(value.final.sequence == value.latest.sequence && value.final.x == value.latest.x && !value.publishing, "atomic final race");
		}
		std::fprintf(stderr, "[ScrollFollowProtocol] failures=%d\n", failures);
		return failures ? 1 : 0;
	}
}
