#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadNavigation.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadInput.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <windows.h>

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.pen_cursor;

namespace
{
	using namespace Inkeys::Drawing::Draw3;

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[Draw3Contact] failed: " << name << '\n';
		return condition;
	}

	bool Near(float left, float right) noexcept
	{
		return std::abs(left - right) <= 0.0001f;
	}

	ContactSnapshot MakeSnapshot(float x, float y, ContactPhase phase)
	{
		ContactSnapshot snapshot{};
		snapshot.position = { x, y };
		snapshot.pressure = 0.75f;
		snapshot.qpc = 1;
		snapshot.phase = phase;
		return snapshot;
	}

	void TestContactLifecycle(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		constexpr std::uint32_t tabletContext = 0x31;
		constexpr std::uint32_t contactId = 0x41;
		if (!Expect(input.PublishDown(tabletContext, contactId, InputDeviceType::Pen,
			MakeSnapshot(10.0f, 20.0f, ContactPhase::Down)), "down is published"))
			++failures;

		ContactRecord* record = nullptr;
		if (!Expect(input.TryDequeue(record) && record != nullptr, "down is dequeued"))
		{
			++failures;
			return;
		}
		const ContactHandle handle{ record, record->Generation() };
		if (!Expect(record->DeviceType() == InputDeviceType::Pen &&
			record->DownSnapshot().position.x == 10.0f,
			"down snapshot keeps device and position")) ++failures;

		if (!Expect(input.PublishMove(tabletContext, contactId,
			MakeSnapshot(30.0f, 40.0f, ContactPhase::Move)), "move is published"))
			++failures;
		ContactSnapshot observed{};
		if (!Expect(input.TryReadSnapshot(handle, observed) &&
			observed.phase == ContactPhase::Move && observed.position.x == 30.0f,
			"move snapshot is visible to consumer")) ++failures;

		if (!Expect(input.PublishUp(tabletContext, contactId,
			MakeSnapshot(50.0f, 60.0f, ContactPhase::Up)), "up is published"))
			++failures;
		if (!Expect(input.TryReadSnapshot(handle, observed) &&
			observed.phase == ContactPhase::Up && observed.position.y == 60.0f,
			"terminal snapshot is retained")) ++failures;
		input.Recycle(handle);
		const auto diagnostics = input.DiagnosticsSnapshot();
		if (!Expect(diagnostics.downPublished == 1 && diagnostics.movePublished == 1 &&
			diagnostics.terminalPublished == 1 && diagnostics.recycled == 1 &&
			diagnostics.occupiedSlots == 0,
			"contact slot is recycled exactly once")) ++failures;
	}

	void TestInvalidAndWakeContracts(int& failures)
	{
		ContactInputCoordinator input;
		ContactSnapshot invalid = MakeSnapshot(1.0f, 2.0f, ContactPhase::Down);
		invalid.position.x = (std::numeric_limits<float>::quiet_NaN)();
		if (!Expect(!input.PublishDown(1, 1, InputDeviceType::Touch, invalid),
			"invalid coordinate is rejected")) ++failures;

		if (!Expect(input.PublishControlWake(), "control wake enters mailbox")) ++failures;
		ContactRecord* control = reinterpret_cast<ContactRecord*>(uintptr_t{ 1 });
		if (!Expect(input.TryDequeue(control) && control == nullptr,
			"control wake is distinguishable from contact")) ++failures;
		input.AcknowledgeControlWake();
	}

	void TestIdleMoveWakeSubscription(int& failures)
	{
		const auto check = [&](bool condition, const char* name)
		{
			if (!Expect(condition, name)) ++failures;
		};
		for (const InputDeviceType device : { InputDeviceType::Touch, InputDeviceType::Pen,
			InputDeviceType::MouseLeft, InputDeviceType::MouseRight })
		{
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			check(input.PublishDown(0xF3, 1, device,
				MakeSnapshot(1, 2, ContactPhase::Down)), "idle subscription device Down accepted");
			ContactRecord* record = nullptr;
			if (!input.TryDequeue(record) || !record)
			{
				check(false, "idle subscription real Down dequeued");
				continue;
			}
			const ContactHandle handle{ record, record->Generation() };
			const uint64_t baseline = input.CaptureWakeGeneration();
			check(input.PublishMove(0xF3, 1, MakeSnapshot(3, 4, ContactPhase::Move)) &&
				input.CaptureWakeGeneration() == baseline,
				"unsubscribed Move still merges without SetEvent");
			input.ArmIdleMoveWake();
			auto invalid = MakeSnapshot(3, 4, ContactPhase::Move);
			invalid.position.x = (std::numeric_limits<float>::quiet_NaN)();
			check(!input.PublishMove(0xF3, 1, invalid) &&
				!input.PublishMove(0xF3, 999, MakeSnapshot(3, 4, ContactPhase::Move)) &&
				input.CaptureWakeGeneration() == baseline,
				"invalid and absent-route Moves cannot consume the subscription");
			for (int index = 0; index < 32; ++index)
				check(input.PublishMove(0xF3, 1,
					MakeSnapshot(float(index), 5, ContactPhase::Move)), "subscribed burst Move accepted");
			ContactSnapshot latest;
			check(input.CaptureWakeGeneration() == baseline + 1 &&
				input.TryReadSnapshot(handle, latest) && latest.position.x == 31 &&
				!input.HasPendingWork() && input.DiagnosticsSnapshot().controlWakes == 0,
				"one subscribed burst wakes once, retains latest snapshot, and creates no marker");
			input.DisarmIdleMoveWake();
			check(input.PublishMove(0xF3, 1, MakeSnapshot(32, 5, ContactPhase::Move)) &&
				input.CaptureWakeGeneration() == baseline + 1,
				"disarmed consumer adds no per-Move wake");
			input.ArmIdleMoveWake();
			check(input.PublishUp(0xF3, 1, MakeSnapshot(32, 5, ContactPhase::Up)) &&
				input.WaitForWake(baseline + 1, 0) &&
				input.TryReadSnapshot(handle, latest) && latest.phase == ContactPhase::Up,
				"Up remains a real terminal wake while Move notification is armed");
			input.DisarmIdleMoveWake();
			input.Recycle(handle);
			input.ArmIdleMoveWake();
			input.ResetForNextRun();
			const bool restarted = input.PublishDown(0xF3, 2, device,
				MakeSnapshot(1, 2, ContactPhase::Down)) && input.TryDequeue(record) && record;
			check(restarted, "next stopped generation accepts a new Down");
			if (!restarted) continue;
			const uint64_t afterReset = input.CaptureWakeGeneration();
			check(!input.TryReadSnapshot(handle, latest) &&
				input.PublishMove(0xF3, 2, MakeSnapshot(3, 4, ContactPhase::Move)) &&
				input.CaptureWakeGeneration() == afterReset,
				"Reset removes stale subscription and stale contact cannot be read");
			const ContactHandle next{ record, record->Generation() };
			input.DiscardUntilTerminal(next);
			input.ArmIdleMoveWake();
			check(!input.PublishMove(0xF3, 2, MakeSnapshot(4, 5, ContactPhase::Move)) &&
				input.CaptureWakeGeneration() == afterReset &&
				input.PublishCancelled(0xF3, 2, MakeSnapshot(4, 5, ContactPhase::Cancelled)) &&
				input.WaitForWake(afterReset, 0) && input.DiagnosticsSnapshot().occupiedSlots == 0,
				"quarantined Move stays rejected and Cancel still closes its route");
			input.DisarmIdleMoveWake();
		}
	}

	void TestConcurrentIdleMoveBurst(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		std::vector<ContactHandle> handles;
		for (uint32_t id = 1; id <= 4; ++id)
		{
			if (!Expect(input.PublishDown(0xF4, id, InputDeviceType::Touch,
				MakeSnapshot(float(id), 1, ContactPhase::Down)), "multi-contact Down accepted")) ++failures;
			ContactRecord* record = nullptr;
			if (!input.TryDequeue(record) || !record) { ++failures; return; }
			handles.push_back({ record, record->Generation() });
		}
		const uint64_t generation = input.CaptureWakeGeneration();
		input.ArmIdleMoveWake();
		std::atomic<bool> start = false, accepted = true;
		std::vector<std::thread> producers;
		for (uint32_t id = 1; id <= 4; ++id)
			producers.emplace_back([&, id]
			{
				while (!start.load(std::memory_order_acquire)) YieldProcessor();
				for (int index = 1; index <= 256; ++index)
					if (!input.PublishMove(0xF4, id,
						MakeSnapshot(float(index), float(id), ContactPhase::Move)))
						accepted.store(false, std::memory_order_relaxed);
			});
		start.store(true, std::memory_order_release);
		for (auto& producer : producers) producer.join();
		input.DisarmIdleMoveWake();
		if (!Expect(accepted && input.CaptureWakeGeneration() == generation + 1 &&
			!input.HasPendingWork() && input.DiagnosticsSnapshot().controlWakes == 0,
			"four concurrent contacts consume exactly one idle Move subscription")) ++failures;
		for (uint32_t id = 1; id <= 4; ++id)
		{
			ContactSnapshot latest;
			if (!Expect(input.TryReadSnapshot(handles[id - 1], latest) &&
				latest.position.x == 256 && latest.position.y == float(id),
				"each contact retains its own latest concurrent Move")) ++failures;
			if (!Expect(input.PublishCancelled(0xF4, id,
				MakeSnapshot(256, float(id), ContactPhase::Cancelled)),
				"concurrent burst closes normally")) ++failures;
			input.Recycle(handles[id - 1]);
		}
	}

	void TestFailedControlWakeKeepsBridgeLive(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3::Bridge;
		ContactInputCoordinator input;
		StateBridge bridge;
		ProductState requested = bridge.Snapshot();
		requested.selectionMode = false;
		bridge.PublishState(requested);
		if (!Expect(bridge.Publish(CommandType::Clear) == CommandResult::Accepted,
			"Clear is accepted before control enqueue failure")) ++failures;
		constexpr std::uint32_t tabletContext = 0x71;
		constexpr std::uint32_t contactId = 0x72;
		if (!Expect(input.PublishDown(tabletContext, contactId, InputDeviceType::Pen,
			MakeSnapshot(1.0f, 2.0f, ContactPhase::Down)) &&
			input.PublishUp(tabletContext, contactId,
				MakeSnapshot(3.0f, 4.0f, ContactPhase::Up)),
			"contact Down and Up stay accepted before failed control wake")) ++failures;
		input.FailNextControlWakeEnqueueForTesting();
		if (!Expect(!input.PublishControlWake(),
			"only the next control token enqueue is injected to fail")) ++failures;

		ContactRecord* record = nullptr;
		if (!Expect(input.TryDequeue(record) && record != nullptr,
			"queued Down remains ahead of failed control wake")) ++failures;
		if (record)
		{
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot terminal{};
			if (!Expect(input.TryReadSnapshot(handle, terminal) && terminal.phase == ContactPhase::Up,
				"failed control wake does not discard the terminal sample")) ++failures;
			input.Recycle(handle);
		}
		const bool controlAvailable = input.TryDequeue(record) && record == nullptr;
		if (!Expect(controlAvailable,
			"failed control enqueue still reaches the production control consumer")) ++failures;
		if (controlAvailable)
		{
			input.AcknowledgeControlWake();
			const ProductState applied = bridge.Snapshot();
			Command command{};
			if (!Expect(!applied.selectionMode && bridge.TryConsume(command) &&
				command.type == CommandType::Clear && command.sequence == 1 &&
				!bridge.TryConsume(command),
				"selection and accepted Clear are consumed once after queued input")) ++failures;
		}
		const auto clearDiagnostics = input.DiagnosticsSnapshot();
		if (!Expect(clearDiagnostics.controlWakeEnqueueFailures == 1 &&
			clearDiagnostics.controlWakeInlineRecoveries == 1,
			"queued input is followed by one in-thread control fallback")) ++failures;

		ContactInputCoordinator stopInput;
		StateBridge stopBridge;
		if (!Expect(stopBridge.Publish(CommandType::Clear) == CommandResult::Accepted &&
			stopBridge.StopWithFinalCommand(CommandType::PrepareExitAutoSave),
			"Stop queues its final save barrier after accepted Clear")) ++failures;
		stopInput.FailNextControlWakeEnqueueForTesting();
		if (!Expect(!stopInput.PublishControlWake(),
			"Stop control token failure is injected without a later marker")) ++failures;
		const bool stopControlAvailable = stopInput.TryDequeue(record) && record == nullptr;
		if (!Expect(stopControlAvailable,
			"Stop final barrier is reachable without a later successful marker")) ++failures;
		if (stopControlAvailable)
		{
			stopInput.AcknowledgeControlWake();
			Command first{}, final{};
			if (!Expect(stopBridge.TryConsume(first) && first.type == CommandType::Clear &&
				stopBridge.TryConsume(final) && final.type == CommandType::PrepareExitAutoSave &&
				first.sequence + 1 == final.sequence && !stopBridge.TryConsume(final),
				"Stop consumes the accepted command then final save barrier exactly once")) ++failures;
		}
		const auto stopDiagnostics = stopInput.DiagnosticsSnapshot();
		if (!Expect(stopDiagnostics.controlWakeEnqueueFailures == 1 &&
			stopDiagnostics.controlWakeInlineRecoveries == 1,
			"empty Stop queue uses one in-thread control fallback")) ++failures;
	}

	void TestFailedControlWakeUnderContinuousContacts(int& failures)
	{
		ContactInputCoordinator input;
		constexpr int contactCount = 64;
		int published = 0;
		int consumed = 0;
		int controlCount = 0;
		int contactsBeforeControl = 0;
		auto publishTerminalContact = [&]()
		{
			const auto contactId = static_cast<std::uint32_t>(++published);
			const bool down = input.PublishDown(0x91, contactId, InputDeviceType::Pen,
				MakeSnapshot(float(contactId), 1.0f, ContactPhase::Down));
			const bool terminal = contactId % 2 == 0
				? input.PublishUp(0x91, contactId,
					MakeSnapshot(float(contactId), 2.0f, ContactPhase::Up))
				: input.PublishCancelled(0x91, contactId,
					MakeSnapshot(float(contactId), 2.0f, ContactPhase::Cancelled));
			return down && terminal;
		};
		if (!Expect(publishTerminalContact(), "first queued contact is accepted")) ++failures;
		input.FailNextControlWakeEnqueueForTesting();
		if (!Expect(!input.PublishControlWake(),
			"continuous-contact test injects one control enqueue failure")) ++failures;
		for (int step = 0; step < contactCount + 4 &&
			(consumed < contactCount || controlCount == 0); ++step)
		{
			ContactRecord* record = nullptr;
			if (!input.TryDequeue(record)) break;
			if (!record)
			{
				input.AcknowledgeControlWake();
				++controlCount;
				contactsBeforeControl = consumed;
				continue;
			}
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot terminal{};
			const ContactPhase expected = record->ContactId() % 2 == 0
				? ContactPhase::Up : ContactPhase::Cancelled;
			if (!Expect(input.TryReadSnapshot(handle, terminal) && terminal.phase == expected,
				"Up and Cancelled remain intact while control retries")) ++failures;
			input.Recycle(handle);
			++consumed;
			if (published < contactCount &&
				!Expect(publishTerminalContact(),
					"new contact remains accepted while control retries")) ++failures;
		}
		const bool continuousResult = consumed == contactCount && controlCount == 1 &&
			contactsBeforeControl >= 1 &&
			static_cast<size_t>(contactsBeforeControl) <= input.DiagnosticsSnapshot().slotCapacity;
		if (!Expect(continuousResult,
			"continuous new contacts neither starve control nor lose or duplicate Down"))
		{
			std::cerr << "[Draw3Contact] continuous details: published=" << published
				<< " consumed=" << consumed << " controls=" << controlCount
				<< " beforeControl=" << contactsBeforeControl
				<< " slotCapacity=" << input.DiagnosticsSnapshot().slotCapacity << '\n';
			++failures;
		}
	}

	void TestFailedControlWakeInterruptsBlockingConsumer(int& failures)
	{
		ContactInputCoordinator input;
		std::atomic<bool> waiting = false;
		std::atomic<bool> returned = false;
		ContactRecord* received = reinterpret_cast<ContactRecord*>(uintptr_t{ 1 });
		std::jthread consumer([&]()
			{
				waiting.store(true, std::memory_order_release);
				input.WaitDequeue(received);
				returned.store(true, std::memory_order_release);
			});
		while (!waiting.load(std::memory_order_acquire)) Sleep(1);
		input.FailNextControlWakeEnqueueForTesting();
		if (!Expect(!input.PublishControlWake(),
			"blocking consumer sees the injected control-only enqueue failure")) ++failures;
		for (int elapsed = 0; elapsed < 100 && !returned.load(std::memory_order_acquire); ++elapsed)
			Sleep(1);
		const bool wokeWithoutLaterMarker = returned.load(std::memory_order_acquire);
		// 红测旧实现仍阻塞时，用正常 marker 仅作测试线程回收，不能算成功路径。
		if (!wokeWithoutLaterMarker) (void)input.PublishControlWake();
		consumer.join();
		if (!Expect(wokeWithoutLaterMarker && received == nullptr,
			"blocking production consumer wakes from failed control enqueue alone")) ++failures;
		if (received == nullptr) input.AcknowledgeControlWake();
	}

	void TestControlWakeSeparatesAcceptedContacts(int& failures, bool failControlEnqueue)
	{
		using namespace Inkeys::Drawing::Draw3::Bridge;
		ContactInputCoordinator input;
		StateBridge bridge;
		std::atomic<bool> oldAccepted = false;
		std::jthread oldProducer([&]()
			{
				oldAccepted.store(input.PublishDown(0xA1, 1, InputDeviceType::Pen,
					MakeSnapshot(1.0f, 1.0f, ContactPhase::Down)) &&
					input.PublishUp(0xA1, 1, MakeSnapshot(1.0f, 2.0f, ContactPhase::Up)),
					std::memory_order_release);
			});
		oldProducer.join();
		if (!Expect(oldAccepted.load(std::memory_order_acquire),
			"old Down and Up are accepted before Clear")) ++failures;
		if (!Expect(bridge.Publish(CommandType::Clear) == CommandResult::Accepted,
			"Clear is accepted between old and new contacts")) ++failures;
		if (failControlEnqueue) input.FailNextControlWakeEnqueueForTesting();
		if (!Expect(input.PublishControlWake() != failControlEnqueue,
			"control wake follows the requested success or failure path")) ++failures;
		std::atomic<bool> newAccepted = false;
		std::jthread newProducer([&]()
			{
				newAccepted.store(input.PublishDown(0xA1, 2, InputDeviceType::Pen,
					MakeSnapshot(2.0f, 1.0f, ContactPhase::Down)) &&
					input.PublishCancelled(0xA1, 2,
						MakeSnapshot(2.0f, 2.0f, ContactPhase::Cancelled)),
						std::memory_order_release);
			});
		newProducer.join();
		if (!Expect(newAccepted.load(std::memory_order_acquire),
			"new Down and Cancelled are accepted after Clear")) ++failures;

		std::vector<int> observed;
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			ContactRecord* record = nullptr;
			if (!Expect(input.TryDequeue(record),
				"old contact, control, and new contact are all dequeued"))
			{
				++failures;
				break;
			}
			if (!record)
			{
				input.AcknowledgeControlWake();
				Command command{};
				if (!Expect(bridge.TryConsume(command) && command.type == CommandType::Clear,
					"control consumes the real Clear command")) ++failures;
				observed.push_back(0);
				continue;
			}
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot terminal{};
			const ContactPhase expected = record->ContactId() == 1
				? ContactPhase::Up : ContactPhase::Cancelled;
			if (!Expect(input.TryReadSnapshot(handle, terminal) && terminal.phase == expected,
				"both terminal samples survive the control boundary")) ++failures;
			observed.push_back(static_cast<int>(record->ContactId()));
			input.Recycle(handle);
		}
		// 真正的 Clear 边界必须夹在已接受的旧 Down 与后续新 Down 之间。
		if (!Expect(observed == std::vector<int>{ 1, 0, 2 },
			failControlEnqueue ? "failed marker keeps exact old/control/new order"
				: "physical marker keeps exact old/control/new order")) ++failures;
	}

	void TestDistinctCommandsKeepTheirIngressBoundaries(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3::Bridge;
		ContactInputCoordinator input;
		StateBridge bridge;
		if (!Expect(input.TryReserveCommandWake() &&
			bridge.Publish(CommandType::Clear) == CommandResult::Accepted,
			"first Clear reserves and enters the Bridge")) ++failures;
		input.PublishReservedCommandWake();
		if (!Expect(input.PublishDown(0xA2, 1, InputDeviceType::Pen,
			MakeSnapshot(1.0f, 1.0f, ContactPhase::Down)) &&
			input.PublishUp(0xA2, 1, MakeSnapshot(1.0f, 2.0f, ContactPhase::Up)),
			"contact between two commands is accepted")) ++failures;
		if (!Expect(input.TryReserveCommandWake() &&
			bridge.Publish(CommandType::Undo) == CommandResult::Accepted,
			"second Undo reserves and enters the Bridge")) ++failures;
		input.PublishReservedCommandWake();
		std::vector<int> observed;
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			ContactRecord* record = nullptr;
			if (!input.TryDequeue(record)) break;
			if (!record)
			{
				if (!Expect(input.LastDequeuedControlWakeKind() == ControlWakeKind::Command,
					"each business boundary is a Command marker")) ++failures;
				observed.push_back(0);
				input.AcknowledgeControlWake();
				continue;
			}
			observed.push_back(1);
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot terminal{};
			if (!Expect(input.TryReadSnapshot(handle, terminal) &&
				terminal.phase == ContactPhase::Up,
				"contact terminal survives two command boundaries")) ++failures;
			input.Recycle(handle);
		}
		// Clear、旧笔迹、Undo 各有自己的可见顺序，不能把后一个命令合并到前一个 marker。
		if (!Expect(observed == std::vector<int>{ 0, 1, 0 },
			"separate commands retain two ingress boundaries")) ++failures;
	}

	void TestFailedCommandMarkerKeepsOrdinalAndContactBoundary(int& failures)
	{
		ContactInputCoordinator input;
		if (!Expect(input.TryReserveCommandWake(),
			"first Command marker reserves a fixed slot")) ++failures;
		input.PublishReservedCommandWake();
		if (!Expect(input.PublishDown(0xA3, 1, InputDeviceType::Pen,
			MakeSnapshot(1.0f, 1.0f, ContactPhase::Down)) &&
			input.PublishUp(0xA3, 1, MakeSnapshot(1.0f, 2.0f, ContactPhase::Up)),
			"old contact is accepted before failed second Command")) ++failures;
		if (!Expect(input.TryReserveCommandWake(),
			"second Command marker reserves its fallback capacity")) ++failures;
		input.FailNextCommandWakeEnqueueForTesting();
		input.PublishReservedCommandWake();
		if (!Expect(input.PublishDown(0xA3, 2, InputDeviceType::Pen,
			MakeSnapshot(2.0f, 1.0f, ContactPhase::Down)) &&
			input.PublishCancelled(0xA3, 2,
				MakeSnapshot(2.0f, 2.0f, ContactPhase::Cancelled)),
			"new contact is accepted after failed second Command")) ++failures;
		std::vector<int> observed;
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			ContactRecord* record = nullptr;
			if (!Expect(input.TryDequeue(record),
				"both Command markers and both contacts remain reachable"))
			{
				++failures;
				break;
			}
			if (!record)
			{
				if (!Expect(input.LastDequeuedControlWakeKind() == ControlWakeKind::Command,
					"physical and inline markers retain Command kind")) ++failures;
				input.AcknowledgeControlWake();
				observed.push_back(0);
				continue;
			}
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot terminal{};
			const ContactPhase expected = record->ContactId() == 1
				? ContactPhase::Up : ContactPhase::Cancelled;
			if (!Expect(input.TryReadSnapshot(handle, terminal) && terminal.phase == expected,
				"terminal snapshots survive failed Command marker")) ++failures;
			observed.push_back(static_cast<int>(record->ContactId()));
			input.Recycle(handle);
		}
		if (!Expect(observed == std::vector<int>{ 0, 1, 0, 2 },
			"failed Command marker waits for its old Down then precedes new Down")) ++failures;
	}

#if defined(DRAW3_CONTACT_TESTING)
	struct CommandFallbackMissPause
	{
		HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		bool resumed = false;

		~CommandFallbackMissPause()
		{
			if (resume) CloseHandle(resume);
			if (entered) CloseHandle(entered);
		}
	};

	void PauseAfterCommandFallbackMiss(void* context) noexcept
	{
		auto& pause = *static_cast<CommandFallbackMissPause*>(context);
		const bool entered = SetEvent(pause.entered) != FALSE;
		// 即使测试前提失败也会有限退场；不能把调度超时算成次序逻辑 RED。
		const DWORD result = WaitForSingleObject(pause.resume, 10000);
		pause.resumed = entered && result == WAIT_OBJECT_0;
	}

	void TestCommandFallbackPublishedAfterConsumerProbe(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3::Bridge;
		ContactInputCoordinator input;
		StateBridge bridge;
		input.EnableDiagnostics(true);
		ContactRecord* initialRecord = nullptr;
		Command initialCommand{};
		if (!Expect(!input.HasPendingWork() && !input.TryDequeue(initialRecord) &&
			!bridge.TryConsume(initialCommand) && input.DiagnosticsSnapshot().controlWakes == 0,
			"ASYNC01 premise: fresh input and Bridge queues are empty"))
		{
			++failures;
			return;
		}
		// 两个事件先建立再启用 hook；包含失败分支，必须先 join 再销毁它们。
		CommandFallbackMissPause pause;
		if (!Expect(pause.entered && pause.resume,
			"ASYNC01 premise: both deterministic barrier events are created"))
		{
			++failures;
			return;
		}
		CommandFallbackMissHookForTesting hook{ &pause, &PauseAfterCommandFallbackMiss };
		input.SetNextCommandFallbackMissHookForTesting(&hook);
		int commandWakeCount = 0;
		int consumedCommandCount = 0;
		bool unexpectedEvent = false;
		Command consumedCommands[2]{};
		std::jthread consumer([&]
			{
				for (int attempt = 0; attempt < 4; ++attempt)
				{
					ContactRecord* record = nullptr;
					if (!input.TryDequeue(record)) continue;
					if (record)
					{
						unexpectedEvent = true;
						input.Recycle({ record, record->Generation() });
						continue;
					}
					if (input.LastDequeuedControlWakeKind() != ControlWakeKind::Command)
						unexpectedEvent = true;
					else
					{
						++commandWakeCount;
						Command command{};
						if (!bridge.TryConsume(command)) unexpectedEvent = true;
						else
						{
							if (consumedCommandCount < 2)
								consumedCommands[consumedCommandCount] = command;
							++consumedCommandCount;
						}
					}
					input.AcknowledgeControlWake();
				}
			});
		const bool entered = WaitForSingleObject(pause.entered, 5000) == WAIT_OBJECT_0;
		const auto publishCommand = [&](CommandType type, bool failEnqueue) noexcept
			{
				if (!input.TryReserveCommandWake()) return false;
				if (bridge.Publish(type) != CommandResult::Accepted)
				{
					input.CancelReservedCommandWake();
					return false;
				}
				if (failEnqueue) input.FailNextCommandWakeEnqueueForTesting();
				input.PublishReservedCommandWake();
				return true;
			};
		// 消费者已经判空：此时 C1 强制进 fallback，C2 才成功进入实体 FIFO。
		const bool firstPublished = entered && publishCommand(CommandType::Clear, true);
		const bool firstIsFallback = firstPublished && input.HasPendingWork() &&
			input.DiagnosticsSnapshot().controlWakes == 0;
		const bool secondPublished = firstIsFallback && publishCommand(CommandType::Undo, false);
		const bool secondIsPhysical = secondPublished &&
			input.DiagnosticsSnapshot().controlWakes == 1;
		const bool resumeSignaled = SetEvent(pause.resume) != FALSE;
		consumer.join();
		input.SetNextCommandFallbackMissHookForTesting(nullptr);
		const bool premise = entered && firstIsFallback && secondIsPhysical &&
			resumeSignaled && pause.resumed;
		if (!Expect(premise,
			"ASYNC01 premise: consumer probe precedes fallback C1 then physical C2"))
		{
			std::cerr << "[ASYNC01] premise=0 entered=" << entered
				<< " C1_fallback=" << firstIsFallback << " C2_physical=" << secondIsPhysical
				<< " resume_signaled=" << resumeSignaled << " resumed=" << pause.resumed << '\n';
			++failures;
			return;
		}

		const bool inputPending = input.HasPendingWork();
		Command leftover{};
		const bool bridgePending = bridge.TryConsume(leftover);
		// 258 是预约额度；实体 ConcurrentQueue 的初始 256 不是其硬容量。
		constexpr int reservationCapacity = 258;
		int availableReservations = 0;
		while (availableReservations < reservationCapacity && input.TryReserveCommandWake())
			++availableReservations;
		const bool reservationLimitHeld = !input.TryReserveCommandWake();
		for (int index = 0; index < availableReservations; ++index)
			input.CancelReservedCommandWake();
		std::cout << "[ASYNC01] premise=1 command_wakes=" << commandWakeCount
			<< " consumed_commands=" << consumedCommandCount
			<< " ingress_pending=" << inputPending << " bridge_pending=" << bridgePending
			<< " leftover_sequence=" << (bridgePending ? leftover.sequence : 0)
			<< " free_reservations=" << availableReservations << '\n';
		if (!Expect(!unexpectedEvent && commandWakeCount == 2 && consumedCommandCount == 2 &&
			consumedCommands[0].type == CommandType::Clear && consumedCommands[0].sequence == 1 &&
			consumedCommands[1].type == CommandType::Undo && consumedCommands[1].sequence == 2 &&
			!inputPending && !bridgePending && availableReservations == reservationCapacity &&
			reservationLimitHeld,
			"ASYNC01 fallback probe race preserves C1/C2 wakes, FIFO commands and reservations"))
			++failures;
	}
	enum class DeferredIngressTestKind
	{
		Command,
		Down,
		General,
	};

	bool PublishOrderingTestCommand(ContactInputCoordinator& input,
		Bridge::StateBridge& bridge, Bridge::CommandType type, bool failEnqueue) noexcept
	{
		if (!input.TryReserveCommandWake()) return false;
		const bool accepted = type == Bridge::CommandType::PrepareExitAutoSave
			? bridge.StopWithFinalCommand(type)
			: bridge.Publish(type) == Bridge::CommandResult::Accepted;
		if (!accepted)
		{
			input.CancelReservedCommandWake();
			return false;
		}
		if (failEnqueue) input.FailNextCommandWakeEnqueueForTesting();
		input.PublishReservedCommandWake();
		return true;
	}

	int AvailableOrderingTestReservations(ContactInputCoordinator& input) noexcept
	{
		int available = 0;
		while (available < 258 && input.TryReserveCommandWake()) ++available;
		const bool limitHeld = !input.TryReserveCommandWake();
		for (int index = 0; index < available; ++index) input.CancelReservedCommandWake();
		return limitHeld ? available : -1;
	}

	void TestCommandFallbackDefersIngress(int& failures, DeferredIngressTestKind kind,
		bool resetDeferred, ContactPhase terminal = ContactPhase::Up)
	{
		using namespace Inkeys::Drawing::Draw3::Bridge;
		ContactInputCoordinator input;
		StateBridge bridge;
		input.EnableDiagnostics(true);
		CommandFallbackMissPause probePause;
		CommandFallbackMissPause afterFirstPause;
		if (!Expect(probePause.entered && probePause.resume &&
			afterFirstPause.entered && afterFirstPause.resume,
			"ASYNC01 deferred premise: all barrier events are created"))
		{
			++failures;
			return;
		}
		CommandFallbackMissHookForTesting hook{ &probePause, &PauseAfterCommandFallbackMiss };
		input.SetNextCommandFallbackMissHookForTesting(&hook);
		int observed[4]{};
		int observedCount = 0;
		bool validEvents = true;
		const auto consume = [&](ContactRecord* record)
			{
				int event = -2;
				if (record)
				{
					const ContactHandle handle{ record, record->Generation() };
					ContactSnapshot snapshot{};
					validEvents = validEvents && (kind == DeferredIngressTestKind::Down ||
						kind == DeferredIngressTestKind::Command) &&
						record->DeviceType() == InputDeviceType::Pen && record->ContactId() == 1 &&
						record->DownSnapshot().position.x == 10 &&
						record->DownSnapshot().position.y == 20 &&
						input.TryReadSnapshot(handle, snapshot) && snapshot.phase == terminal &&
						snapshot.position.x == 30 && snapshot.position.y == 40;
					input.Recycle(handle);
					event = 0;
				}
				else if (input.LastDequeuedControlWakeKind() == ControlWakeKind::General)
					event = -1;
				else
				{
					Command command{};
					if (!bridge.TryConsume(command)) validEvents = false;
					else
					{
						event = static_cast<int>(command.sequence);
						const auto expected = command.sequence == 1 ? CommandType::Clear
							: command.sequence == 2 ? CommandType::Undo : CommandType::PrepareExitAutoSave;
						validEvents = validEvents && command.type == expected;
					}
				}
				if (observedCount < 4) observed[observedCount] = event;
				else validEvents = false;
				++observedCount;
				if (!record) input.AcknowledgeControlWake();
			};
		std::jthread consumer([&]
			{
				ContactRecord* record = nullptr;
				if (input.TryDequeue(record)) consume(record);
				// 第一次实际交出后停住，给主线程观察只有 deferred 的待办状态。
				PauseAfterCommandFallbackMiss(&afterFirstPause);
				if (!resetDeferred)
				{
					for (int attempt = 0; attempt < 4; ++attempt)
						if (input.TryDequeue(record)) consume(record);
				}
			});
		const bool entered = WaitForSingleObject(probePause.entered, 5000) == WAIT_OBJECT_0;
		bool firstIsFallback = false;
		bool physicalPublished = false;
		if (entered)
		{
			std::jthread producer([&]
				{
					firstIsFallback = PublishOrderingTestCommand(
						input, bridge, CommandType::Clear, true) &&
						input.HasPendingWork() && input.DiagnosticsSnapshot().controlWakes == 0;
					if (!firstIsFallback) return;
					if (kind == DeferredIngressTestKind::Command)
						physicalPublished = PublishOrderingTestCommand(
							input, bridge, CommandType::Undo, false) &&
							input.DiagnosticsSnapshot().controlWakes == 1;
					else if (kind == DeferredIngressTestKind::Down)
						physicalPublished = input.PublishDown(0xA5, 1, InputDeviceType::Pen,
							MakeSnapshot(10, 20, ContactPhase::Down)) &&
							(terminal == ContactPhase::Cancelled
								? input.PublishCancelled(0xA5, 1, MakeSnapshot(30, 40, terminal))
								: input.PublishUp(0xA5, 1, MakeSnapshot(30, 40, terminal)));
					else physicalPublished = input.PublishControlWake() &&
						input.DiagnosticsSnapshot().controlWakes == 1;
				});
			producer.join();
		}
		const bool probeResumeSignaled = SetEvent(probePause.resume) != FALSE;
		const bool firstReturned = WaitForSingleObject(afterFirstPause.entered, 5000) == WAIT_OBJECT_0;
		const bool deferredVisible = firstReturned && input.HasPendingWork();
		const bool needsLaterCommand = !resetDeferred && kind != DeferredIngressTestKind::General;
		bool laterPublished = !needsLaterCommand;
		if (firstReturned && firstIsFallback && physicalPublished && needsLaterCommand)
		{
			std::jthread producer([&]
				{
					// C2 仍被保留：再插入真实 Down/Up，使 C3 final 不能提前到 C2 或该笔之前。
					if (kind == DeferredIngressTestKind::Command &&
						(!input.PublishDown(0xA5, 1, InputDeviceType::Pen,
							MakeSnapshot(10, 20, ContactPhase::Down)) ||
							!input.PublishUp(0xA5, 1, MakeSnapshot(30, 40, ContactPhase::Up)))) return;
					laterPublished = PublishOrderingTestCommand(input, bridge,
						kind == DeferredIngressTestKind::Command
							? CommandType::PrepareExitAutoSave : CommandType::Undo, true);
				});
			producer.join();
		}
		const bool finalResumeSignaled = SetEvent(afterFirstPause.resume) != FALSE;
		consumer.join();
		input.SetNextCommandFallbackMissHookForTesting(nullptr);
		const bool premise = entered && firstIsFallback && physicalPublished && firstReturned &&
			laterPublished && probeResumeSignaled && finalResumeSignaled &&
			probePause.resumed && afterFirstPause.resumed;
		if (!Expect(premise, "ASYNC01 deferred premise: exact two-stage producer/consumer handoff"))
		{
			std::cerr << "[ASYNC01Deferred] premise=0 kind=" << static_cast<int>(kind)
				<< " reset=" << resetDeferred << " entered=" << entered
				<< " physical=" << physicalPublished << " first_returned=" << firstReturned
				<< " later=" << laterPublished << '\n';
			++failures;
			return;
		}

		bool result = validEvents && deferredVisible && observed[0] == 1;
		if (resetDeferred)
		{
			// producer 和 consumer 已全部 join；才可丢旧事件、旧槽及 Bridge 代次。
			result = result && observedCount == 1 && input.HasPendingWork();
			input.ResetForNextRun();
			bridge.Reset();
			ContactRecord* record = nullptr;
			Command command{};
			result = result && !input.HasPendingWork() && !input.TryDequeue(record) &&
				input.DiagnosticsSnapshot().occupiedSlots == 0 && !bridge.TryConsume(command);
			const bool newPublished = PublishOrderingTestCommand(
				input, bridge, CommandType::Clear, false);
			result = result && newPublished && input.TryDequeue(record) && !record &&
				input.LastDequeuedControlWakeKind() == ControlWakeKind::Command &&
				bridge.TryConsume(command) && command.type == CommandType::Clear && command.sequence == 1;
			input.AcknowledgeControlWake();
		}
		else if (kind == DeferredIngressTestKind::Command)
		{
			const auto diagnostics = input.DiagnosticsSnapshot();
			result = result && observedCount == 4 && observed[1] == 2 &&
				observed[2] == 0 && observed[3] == 3 && diagnostics.downPublished == 1 &&
				diagnostics.terminalPublished == 1 && diagnostics.recycled == 1 &&
				diagnostics.occupiedSlots == 0 && !bridge.Running();
		}
		else if (kind == DeferredIngressTestKind::Down)
		{
			const auto diagnostics = input.DiagnosticsSnapshot();
			result = result && observedCount == 3 && observed[1] == 0 && observed[2] == 2 &&
				diagnostics.downPublished == 1 && diagnostics.terminalPublished == 1 &&
				diagnostics.recycled == 1 && diagnostics.occupiedSlots == 0;
		}
		else result = result && observedCount == 2 && observed[1] == -1;

		if (kind == DeferredIngressTestKind::General)
		{
			// 原 General 仍恰好交出并清 pending；下一次请求须再次有实体唤醒。
			ContactRecord* record = nullptr;
			const bool published = input.PublishControlWake();
			result = result && published && input.TryDequeue(record) && !record &&
				input.LastDequeuedControlWakeKind() == ControlWakeKind::General;
			input.AcknowledgeControlWake();
		}
		Command leftover{};
		result = result && !input.HasPendingWork() && !bridge.TryConsume(leftover);
		const int available = AvailableOrderingTestReservations(input);
		std::cout << "[ASYNC01Deferred] premise=1 kind=" << static_cast<int>(kind)
			<< " reset=" << resetDeferred << " terminal=" << static_cast<int>(terminal)
			<< " observed=" << observedCount << " deferred_visible=" << deferredVisible
			<< " free_reservations=" << available << " result=" << (result && available == 258) << '\n';
		if (!Expect(result && available == 258,
			"ASYNC01 deferred event preserves ordinal, Down boundary, pending and stopped reset"))
			++failures;
	}
#endif

	void TestCommandWakeReservationAndGenerationReset(int& failures)
	{
		ContactInputCoordinator input;
		int reserved = 0;
		while (reserved < 258 && input.TryReserveCommandWake()) ++reserved;
		if (!Expect(reserved == 258 && !input.TryReserveCommandWake(),
			"fixed Command reservation capacity rejects before Bridge acceptance")) ++failures;
		for (int index = 0; index < reserved; ++index) input.CancelReservedCommandWake();
		if (!Expect(input.TryReserveCommandWake(),
			"reservation capacity recovers after cancellation")) ++failures;
		input.PublishReservedCommandWake();
		if (!Expect(input.PublishControlWake(),
			"old generation may also leave a General marker")) ++failures;
		input.ResetForNextRun();
		ContactRecord* record = nullptr;
		if (!Expect(!input.TryDequeue(record),
			"new generation does not consume an old Command or General marker")) ++failures;
		if (!Expect(input.TryReserveCommandWake(),
			"new generation starts with free Command reservation capacity")) ++failures;
		input.PublishReservedCommandWake();
		if (!Expect(input.TryDequeue(record) && record == nullptr &&
			input.LastDequeuedControlWakeKind() == ControlWakeKind::Command,
			"new generation Command marker remains live")) ++failures;
	}

	void TestPageAdmissionAndQuarantine(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		ContactRecord* record = nullptr;
		auto dequeueContact = [&]() -> ContactHandle
		{
			while (input.TryDequeue(record))
			{
				if (record) return { record, record->Generation() };
				input.AcknowledgeControlWake();
			}
			return {};
		};
		input.PublishDown(1, 1, InputDeviceType::Pen, MakeSnapshot(10, 20, ContactPhase::Down));
		const auto old = dequeueContact();
		if (!Expect(input.ContactAdmitted(old), "initial Down enters current page")) ++failures;
		input.SetAdmissionBlocked(true);
		if (!Expect(!input.ContactAdmitted(old), "page boundary invalidates queued old Down")) ++failures;
		input.DiscardUntilTerminal(old);
		if (!Expect(input.HasQuarantinedContacts() && input.DiagnosticsSnapshot().occupiedSlots == 1 &&
			!input.PublishMove(1, 1, MakeSnapshot(50, 60, ContactPhase::Move)),
			"old contact holds its route and ignores moves after sealing")) ++failures;
		input.PublishDown(1, 2, InputDeviceType::Touch, MakeSnapshot(30, 40, ContactPhase::Down));
		const auto during = dequeueContact();
		input.SetAdmissionBlocked(false);
		if (!Expect(!input.ContactAdmitted(during), "UI ack cannot admit a Down made while closed")) ++failures;
		input.DiscardUntilTerminal(during);
		input.PublishDown(1, 3, InputDeviceType::Pen, MakeSnapshot(70, 80, ContactPhase::Down));
		const auto fresh = dequeueContact();
		if (!Expect(input.ContactAdmitted(fresh), "new Down after UI ack is admitted")) ++failures;
		input.PublishUp(1, 1, MakeSnapshot(100, 110, ContactPhase::Up));
		input.PublishCancelled(1, 2, MakeSnapshot(100, 110, ContactPhase::Cancelled));
		if (!Expect(!input.HasQuarantinedContacts() && input.DiagnosticsSnapshot().recycled == 2 &&
			input.ContactAdmitted(fresh), "old terminals retire exactly their own routes")) ++failures;
		input.PublishUp(1, 3, MakeSnapshot(90, 100, ContactPhase::Up));
		input.Recycle(fresh);
		if (!Expect(input.DiagnosticsSnapshot().occupiedSlots == 0,
			"all page boundary slots are returned without waiting for another frame")) ++failures;

		// 真实 producer/consumer 并发覆盖 Up 抢先与隔离抢先两条 ownership 路径。
		for (int iteration = 0; iteration < 128; ++iteration)
		{
			input.PublishDown(2, 1, InputDeviceType::Pen, MakeSnapshot(1, 2, ContactPhase::Down));
			const auto concurrent = dequeueContact();
			std::jthread terminal([&]() { input.PublishUp(2, 1, MakeSnapshot(3, 4, ContactPhase::Up)); });
			input.DiscardUntilTerminal(concurrent);
			terminal.join();
			if (!Expect(input.DiagnosticsSnapshot().occupiedSlots == 0 && !input.HasQuarantinedContacts(),
				"terminal versus quarantine race neither leaks nor double-recycles")) { ++failures; break; }
		}
	}

	void TestClosingDiscardLiveness(int& failures)
	{
		const auto waitFor = [](const std::atomic<bool>& signal, DWORD timeoutMs)
		{
			const ULONGLONG deadline = GetTickCount64() + timeoutMs;
			while (!signal.load(std::memory_order_acquire) &&
				GetTickCount64() < deadline) Sleep(1);
			return signal.load(std::memory_order_acquire);
		};
		const auto exercise = [&](std::uint32_t contactId, ContactPhase terminal,
			bool preQuarantine, bool recycleInstead, bool invalidTerminal)
		{
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			constexpr std::uint32_t tabletContext = 0xC057;
			ContactRecord* record = nullptr;
			if (!Expect(input.PublishDown(tabletContext, contactId, InputDeviceType::Pen,
				MakeSnapshot(10, 20, ContactPhase::Down)) &&
				input.TryDequeue(record) && record, "prepare Closing Discard contact"))
			{
				++failures;
				return;
			}
			const ContactHandle handle{ record, record->Generation() };
			if (preQuarantine) input.DiscardUntilTerminal(handle);
			ContactClosePauseForTesting pause;
			input.PauseNextCloseAfterRouteClosedForTesting(&pause);
			ContactSnapshot terminalSnapshot = MakeSnapshot(30, 40, terminal);
			if (invalidTerminal) terminalSnapshot.position.x =
				(std::numeric_limits<float>::quiet_NaN)();
			std::atomic<bool> producerFinished = false;
			std::thread producer([&]
				{ if (terminal == ContactPhase::Cancelled)
					input.PublishCancelled(tabletContext, contactId, terminalSnapshot);
				else input.PublishUp(tabletContext, contactId, terminalSnapshot);
				producerFinished.store(true, std::memory_order_release); });
			const bool closingEntered = waitFor(pause.entered, 1000);
			bool returnedBeforeResume = false;
			if (closingEntered)
			{
				std::atomic<bool> consumerFinished = false;
				std::thread consumer([&]
					{ if (recycleInstead) input.Recycle(handle);
					else input.DiscardUntilTerminal(handle);
					consumerFinished.store(true, std::memory_order_release); });
				returnedBeforeResume = waitFor(consumerFinished, 100);
				if (returnedBeforeResume)
				{
					ContactSnapshot abandoned;
					const auto early = input.DiagnosticsSnapshot();
					if (!Expect(early.occupiedSlots == 1 && early.recycled == 0 &&
						input.HasQuarantinedContacts() == preQuarantine &&
						!input.TryReadSnapshot(handle, abandoned),
						"abandoned Closing keeps slot occupied and hides snapshot")) ++failures;
					std::atomic<bool> repeatFinished = false;
					std::thread repeat([&]
						{ input.DiscardUntilTerminal(handle);
							repeatFinished.store(true, std::memory_order_release); });
					const bool repeatReturned = waitFor(repeatFinished, 100);
					if (!Expect(repeatReturned, "repeat Discard never waits or double-owns Closing"))
						++failures;
					// 先放行 producer，再 join；异常分支也不留下测试线程。
					pause.resume.store(true, std::memory_order_release);
					producer.join();
					consumer.join();
					repeat.join();
				}
				else
				{
					pause.resume.store(true, std::memory_order_release);
					producer.join();
					consumer.join();
				}
			}
			else
			{
				pause.resume.store(true, std::memory_order_release);
				producer.join();
				input.Recycle(handle);
			}
			if (!Expect(closingEntered && producerFinished.load(std::memory_order_acquire),
				"Up or Cancel reaches real Closing pause")) ++failures;
			if (!Expect(!closingEntered || returnedBeforeResume,
				"ordinary Discard or Recycle returns while producer remains Closing")) ++failures;
			input.DiscardUntilTerminal(handle); // Free 后重复调用不能二次释放。
			const auto after = input.DiagnosticsSnapshot();
			if (!Expect(after.occupiedSlots == 0 && after.recycled == 1 &&
				after.terminalPublished == 1 && !input.HasQuarantinedContacts(),
				"Closing completion releases exactly one slot")) ++failures;
			ContactRecord* reused = nullptr;
			const bool publishedAgain = input.PublishDown(tabletContext, contactId + 1000,
				InputDeviceType::Pen, MakeSnapshot(50, 60, ContactPhase::Down));
			while (input.TryDequeue(reused) && !reused) input.AcknowledgeControlWake();
			if (!Expect(publishedAgain && reused == record &&
				reused->Generation() != handle.generation,
				"released slot is reused with a fresh generation")) ++failures;
			if (reused)
			{
				ContactSnapshot oldSnapshot, freshSnapshot;
				const ContactHandle fresh{ reused, reused->Generation() };
				if (!Expect(!input.TryReadSnapshot(handle, oldSnapshot) &&
					input.TryReadSnapshot(fresh, freshSnapshot) &&
					!input.ContactAdmitted(handle) && input.ContactAdmitted(fresh),
					"old generation cannot read or admit reused contact")) ++failures;
				input.PublishUp(tabletContext, contactId + 1000,
					MakeSnapshot(55, 65, ContactPhase::Up));
				input.Recycle(fresh);
			}
			if (!Expect(input.DiagnosticsSnapshot().occupiedSlots == 0,
				"reused contact also retires cleanly")) ++failures;
		};
		exercise(1, ContactPhase::Up, false, false, false);
		exercise(2, ContactPhase::Cancelled, false, false, false);
		exercise(3, ContactPhase::Up, true, false, false);
		exercise(4, ContactPhase::Cancelled, true, false, false);
		exercise(5, ContactPhase::Up, false, true, false);
		exercise(6, ContactPhase::Up, false, false, true);

		ContactInputCoordinator producerFirst;
		producerFirst.EnableDiagnostics(true);
		ContactRecord* completed = nullptr;
		if (producerFirst.PublishDown(0xC057, 100, InputDeviceType::Pen,
			MakeSnapshot(10, 20, ContactPhase::Down)) &&
			producerFirst.TryDequeue(completed) && completed)
		{
			const ContactHandle handle{ completed, completed->Generation() };
			producerFirst.PublishUp(0xC057, 100, MakeSnapshot(30, 40, ContactPhase::Up));
			producerFirst.DiscardUntilTerminal(handle);
			producerFirst.DiscardUntilTerminal(handle);
			const auto result = producerFirst.DiagnosticsSnapshot();
			if (!Expect(result.occupiedSlots == 0 && result.recycled == 1 &&
				result.terminalPublished == 1,
				"producer-first ConsumerOwned is recycled once")) ++failures;
		}
		else if (!Expect(false, "prepare producer-first contact")) ++failures;
	}

	void TestCursorOpacityContracts(int& failures)
	{
		DrawingCursorAppearance highlighterAppearance = {
			DrawingCursorShape::Rectangle, 6.25f, 50.0f, 1.0f, 0.2f, 0.8f
		};
		highlighterAppearance.opacity =
			Bridge::kHighlighterCompositeOpacity;
		highlighterAppearance.fillAlpha = 1.0f;
		DrawingCursorAppearance eraserAppearance = {
			DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f
		};
		eraserAppearance.opacity = 0.5f;
		eraserAppearance.fillAlpha = 1.0f;

		DrawingCursorSample penHover = {
			.x = 30.0f, .y = 40.0f, .qpc = 1, .valid = true
		};
		DrawingCursorSample mouseHover = {
			.x = 60.0f, .y = 70.0f, .qpc = 2, .valid = true
		};
		DrawingCursorVisual visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(visual.visible &&
			Near(visual.appearance.opacity, Bridge::kHighlighterCompositeOpacity) &&
			Near(visual.appearance.fillAlpha, 1.0f),
			"highlighter hover keeps Draw3 composite opacity")) ++failures;
		DrawingCursorSample penContact = penHover;
		penContact.inContact = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(!visual.visible,
			"pen contact keeps the default application cursor hidden")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, false, false, false, false);
		if (!Expect(visual.visible &&
			Near(visual.appearance.opacity, Bridge::kHighlighterCompositeOpacity),
			"mouse application cursor keeps highlighter opacity")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(!visual.visible,
			"ordinary mouse hover keeps the system cursor policy")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Touch,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(!visual.visible,
			"touch authority does not create a primary hover cursor")) ++failures;

		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 0.5f),
			"pen eraser hover remains translucent")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"pen eraser contact becomes opaque")) ++failures;

		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 0.5f),
			"mouse eraser hover remains translucent")) ++failures;
		mouseHover.inContact = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"mouse eraser contact becomes opaque")) ++failures;

		DrawingCursorSample invertedHover = penHover;
		invertedHover.inverted = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 0.5f),
			"inverted pen eraser hover remains translucent")) ++failures;
		invertedHover.inContact = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"inverted pen eraser contact becomes opaque")) ++failures;
		visual = MakeTouchEraserDrawingCursorVisual(
			100.0f, 150.0f, eraserAppearance);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"touch eraser contact remains opaque")) ++failures;
	}

	void TestPerDeviceCursorStyles(int& failures)
	{
		const DrawingCursorAppearance penAppearance = {
			DrawingCursorShape::Circle, 9.0f, 9.0f, 0.2f, 0.4f, 0.8f
		};
		DrawingCursorAppearance highlighter = {
			DrawingCursorShape::Rectangle, 3.75f, 30.0f, 1.0f, 0.8f, 0.2f
		};
		highlighter.opacity = Bridge::kHighlighterCompositeOpacity;
		highlighter.fillAlpha = 1.0f;
		DrawingCursorAppearance eraser = {
			DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f
		};
		eraser.opacity = 0.5f;
		CursorSettings settings;
		DrawingCursorSample pen = { .x = 30.0f, .y = 40.0f, .valid = true };
		DrawingCursorSample mouse = { .x = 60.0f, .y = 70.0f, .valid = true };
		const auto mouseOwner = DrawingCursorPointerAuthority::Mouse;
		const auto penOwner = DrawingCursorPointerAuthority::Pen;
		auto resolve = [&](DrawingCursorPointerAuthority owner, const DrawingCursorAppearance& appearance,
			Bridge::Tool tool = Bridge::Tool::Pen, bool penIconAllowed = true, float scale = 1.0f,
			bool erases = false, bool contactVisible = true)
		{
			const auto effective = Bridge::ResolveCursorSettingsForTool(tool, settings);
			return ResolvePrimaryDrawingCursorVisual(pen, mouse, owner, appearance, eraser,
				erases, contactVisible, false, effective.mouse == CursorStyle::Arrow, &effective, penIconAllowed, scale);
		};
		const auto highlighted = [](const DrawingCursorVisual& v) { return Near(v.appearance.outlineWidth, 1.25f); };
		const auto plain = [](const DrawingCursorVisual& v) { return Near(v.appearance.outlineWidth, 0.5f); };
		const auto setScreen = [&] {
			pen.source = { SpeedEraser::SourceKind::IntegratedPen, SpeedEraser::SourceRecognition::RtsCapabilities };
		};
		const auto setTablet = [&] {
			pen.source = { SpeedEraser::SourceKind::ExternalPen, SpeedEraser::SourceRecognition::PointerCursor };
		};

		// 默认值：鼠标=系统光标（不自绘）、屏幕笔=小圆点、数位板=高亮小圆点；荧光笔预览仍存在。
		setScreen();
		const auto screenDefault = resolve(penOwner, penAppearance);
		setTablet();
		const auto tabletDefault = resolve(penOwner, penAppearance);
		if (!Expect(!resolve(mouseOwner, penAppearance).visible &&
			screenDefault.visible && screenDefault.appearance.shape == DrawingCursorShape::Circle &&
			Near(screenDefault.appearance.width, 9.0f) && plain(screenDefault) &&
			tabletDefault.visible && tabletDefault.appearance.shape == DrawingCursorShape::Circle &&
			highlighted(tabletDefault) &&
			resolve(mouseOwner, highlighter, Bridge::Tool::Highlighter, false).visible,
			"defaults: system mouse, plain screen-pen dot, highlighted tablet dot, highlighter preview unchanged")) ++failures;

		// 五种样式 × 屏幕笔/数位板/Unknown/Conflict：形状、尺寸与描边只由样式和设备行决定。
		struct StyleCase { CursorStyle style; DrawingCursorShape shape; float width; bool highlight; };
		const StyleCase styles[] = {
			{ CursorStyle::Arrow, DrawingCursorShape::Arrow, 13.0f, false },
			{ CursorStyle::Pen, DrawingCursorShape::PenIcon, 24.0f, false },
			{ CursorStyle::HighlightPen, DrawingCursorShape::PenIcon, 24.0f, true },
			{ CursorStyle::Dot, DrawingCursorShape::Circle, 9.0f, false },
			{ CursorStyle::HighlightDot, DrawingCursorShape::Circle, 9.0f, true } };
		for (const auto& c : styles)
		{
			settings = {};
			settings.screenPen = settings.tablet = c.style;
			for (int row = 0; row < 4; ++row)
			{
				if (row == 0) setScreen();
				else if (row == 1) setTablet();
				else if (row == 2) pen.source = {};
				else pen.source = { SpeedEraser::SourceKind::IntegratedPen, SpeedEraser::SourceRecognition::Conflict };
				for (const auto tool : { Bridge::Tool::Pen, Bridge::Tool::HardPen })
				{
					const auto visual = resolve(penOwner, penAppearance, tool);
					if (!Expect(visual.visible && visual.appearance.shape == c.shape &&
						Near(visual.appearance.width, c.width) && (c.style == CursorStyle::Arrow || highlighted(visual) == c.highlight),
						"each style renders its own shape and edge on screen pen, tablet, unknown and conflict rows")) ++failures;
				}
			}
		}

		// 屏幕笔与数位板互相独立；未知/冲突沿用数位板行。
		settings = {};
		settings.screenPen = CursorStyle::Pen; settings.tablet = CursorStyle::HighlightDot;
		setScreen();
		const auto screenRow = resolve(penOwner, penAppearance);
		setTablet();
		const auto tabletRow = resolve(penOwner, penAppearance);
		pen.source = {};
		const auto unknownRow = resolve(penOwner, penAppearance);
		if (!Expect(screenRow.appearance.shape == DrawingCursorShape::PenIcon &&
			tabletRow.appearance.shape == DrawingCursorShape::Circle &&
			unknownRow.appearance.shape == DrawingCursorShape::Circle && highlighted(unknownRow),
			"screen pen and tablet rows are independent and unknown sources use the tablet row")) ++failures;

		// 画笔热点与边界（左上尖端为输入点）、DPI 缩放不移动热点。
		settings = {};
		settings.mouse = CursorStyle::HighlightPen;
		const auto icon = resolve(mouseOwner, penAppearance);
		const RECT bounds = DrawingCursorVisualBounds(icon, 200, 200);
		const auto scaledIcon = resolve(mouseOwner, penAppearance, Bridge::Tool::Pen, true, 2.0f);
		if (!Expect(icon.visible && icon.appearance.shape == DrawingCursorShape::PenIcon && highlighted(icon) &&
			Near(icon.x, 60.0f) && Near(icon.y, 70.0f) &&
			Near(icon.appearance.centerOffsetX, 12.0f) && Near(icon.appearance.centerOffsetY, 12.0f) &&
			bounds.left == 58 && bounds.top == 68 && bounds.right == 86 && bounds.bottom == 96 &&
			Near(scaledIcon.x, icon.x) && Near(scaledIcon.appearance.width, 48.0f),
			"pen cursor keeps its upper-left hotspot, bounds and DPI scaling")) ++failures;
		// 仅箭头改用细黑边；低 DPI 最少 1px，192 DPI 仍为 1px，盒尺寸和尖端不变。
		settings = {}; settings.screenPen = CursorStyle::Arrow;
		setScreen();
		struct ArrowScaleCase { float scale; float edge; };
		for (const auto& test : { ArrowScaleCase{ 0.5f, 1.0f }, { 1.0f, 1.0f }, { 1.5f, 1.0f },
			{ 2.0f, 1.0f }, { 2.75f, 1.375f }, { 4.0f, 2.0f } })
		{
			const auto arrow = resolve(penOwner, penAppearance, Bridge::Tool::Pen, true, test.scale);
			if (!Expect(arrow.visible && Near(arrow.appearance.width, 13.0f * test.scale) &&
				Near(arrow.appearance.height, 19.5f * test.scale) && Near(arrow.appearance.outlineWidth, test.edge) &&
				Near(arrow.x, pen.x) && Near(arrow.y, pen.y) &&
				Near(arrow.appearance.centerOffsetX, 6.5f * test.scale) && Near(arrow.appearance.centerOffsetY, 9.75f * test.scale),
				"arrow border stays thin with a one-physical-pixel minimum while its size and hotspot are unchanged")) ++failures;
		}
		settings.mouse = CursorStyle::HighlightPen;
		mouse.x = -12.0f; mouse.y = -8.0f;
		const RECT clipped = DrawingCursorVisualBounds(resolve(mouseOwner, penAppearance), 100, 100);
		if (!Expect(clipped.left == 0 && clipped.top == 0 && clipped.right == 14 && clipped.bottom == 18,
			"pen cursor clips negative canvas coordinates with its actual offset bounds")) ++failures;
		mouse.x = 60.0f; mouse.y = 70.0f;

		// 鼠标行：Arrow 为系统光标不自绘；其余样式为应用光标，接触期间仍可见。
		for (const auto& c : styles)
		{
			settings = {};
			settings.mouse = c.style;
			mouse.inContact = true;
			const auto visual = resolve(mouseOwner, penAppearance);
			mouse.inContact = false;
			if (!Expect(visual.visible == (c.style != CursorStyle::Arrow) &&
				(!visual.visible || (visual.appearance.shape == c.shape && Near(visual.appearance.width, c.width))),
				"mouse row draws every style except the real system arrow")) ++failures;
		}

		// 荧光笔与图形：不受任何设备样式影响；荧光笔矩形/填充/原高亮规则保持。
		for (const auto& c : styles)
		{
			settings = {};
			settings.mouse = settings.touch = settings.screenPen = settings.tablet = c.style;
			setScreen();
			const auto screenMarker = resolve(penOwner, highlighter, Bridge::Tool::Highlighter, false);
			setTablet();
			const auto tabletMarker = resolve(penOwner, highlighter, Bridge::Tool::Highlighter, false);
			const auto mouseMarker = resolve(mouseOwner, highlighter, Bridge::Tool::Highlighter, false);
			const auto shapeCircle = resolve(penOwner, penAppearance, Bridge::Tool::SolidLine, false);
			const auto mouseShape = resolve(mouseOwner, penAppearance, Bridge::Tool::SolidLine, false);
			if (!Expect(screenMarker.appearance.shape == DrawingCursorShape::Rectangle &&
				Near(screenMarker.appearance.width, 3.75f) && Near(screenMarker.appearance.height, 30.0f) &&
				Near(screenMarker.appearance.opacity * screenMarker.appearance.fillAlpha, 0.35f) && plain(screenMarker) &&
				tabletMarker.appearance.shape == DrawingCursorShape::Rectangle && highlighted(tabletMarker) &&
				mouseMarker.visible && mouseMarker.appearance.shape == DrawingCursorShape::Rectangle &&
				highlighted(mouseMarker) &&
				shapeCircle.appearance.shape == DrawingCursorShape::Circle && Near(shapeCircle.appearance.width, 9.0f) &&
				!mouseShape.visible,
				"highlighter and shape cursors ignore every device style")) ++failures;
		}

		// 橡皮、倒转笔尾、触摸主光标与激光不受设备样式影响。
		settings = {};
		settings.mouse = settings.screenPen = settings.tablet = CursorStyle::HighlightPen;
		const auto mouseEraser = resolve(mouseOwner, highlighter, Bridge::Tool::ConfiguredEraser, false, 1, true);
		pen.inverted = true;
		const auto hoverAlpha = resolve(penOwner, highlighter).appearance.opacity;
		pen.inContact = true;
		const auto contactAlpha = resolve(penOwner, highlighter).appearance.opacity;
		const bool touchPrimary = resolve(DrawingCursorPointerAuthority::Touch, highlighter).visible;
		pen.inverted = pen.inContact = false;
		const bool laserHover = ResolveLaserDrawingCursorVisual(pen, mouse, penOwner, penAppearance).visible &&
			!ResolveLaserDrawingCursorVisual(pen, mouse, DrawingCursorPointerAuthority::Touch, penAppearance).visible;
		pen.inContact = true;
		const bool laserContact = ResolveLaserDrawingCursorVisual(pen, mouse, penOwner, penAppearance).visible;
		pen.inContact = false;
		if (!Expect(mouseEraser.visible && mouseEraser.appearance.shape == DrawingCursorShape::EraserGripCircle &&
			Near(mouseEraser.appearance.opacity, 0.5f) && Near(mouseEraser.appearance.outlineOpacity, -1.0f) &&
			Near(hoverAlpha, 0.5f) && Near(contactAlpha, 1.0f) && !touchPrimary && laserHover && !laserContact,
			"eraser, inverted pen, touch primary and laser keep their dedicated behavior")) ++failures;

		// 四态 × 五种样式 × 来源行：仅两种小圆点按设备隐藏，不再受旧统一 Contact 开关限制。
		for (int mode = 0; mode <= 3; ++mode)
			for (const auto& c : styles)
				for (int row = 0; row < 6; ++row)
				{
					settings = {};
					settings.dotHideOnContact = static_cast<DotCursorHideMode>(mode);
					settings.mouse = settings.screenPen = settings.tablet = c.style;
					settings.injectedMouseAsTablet = true;
					if (row == 0) setScreen();
					else if (row == 1) setTablet();
					else if (row == 2) pen.source = {};
					else if (row == 3) pen.source = { SpeedEraser::SourceKind::IntegratedPen, SpeedEraser::SourceRecognition::Conflict };
					mouse.source = { row == 4 ? SpeedEraser::SourceKind::ExternalPen : SpeedEraser::SourceKind::Mouse };
					const auto owner = row < 4 ? penOwner : mouseOwner;
					const bool systemMouse = row == 5 && c.style == CursorStyle::Arrow;
					const bool dot = c.style == CursorStyle::Dot || c.style == CursorStyle::HighlightDot;
					const bool hidden = dot && (row == 0 ? (mode == 1 || mode == 3)
						: row < 5 ? (mode == 2 || mode == 3) : false);
					pen.inContact = mouse.inContact = true;
					for (const auto tool : { Bridge::Tool::Pen, Bridge::Tool::HardPen })
					{
						const auto visual = resolve(owner, penAppearance, tool, true, 1.0f, false, false);
						if (!Expect(visual.visible == (!systemMouse && !hidden),
							"contact hiding applies only to both dot variants on the selected pen device rows")) ++failures;
					}
					const auto marker = resolve(owner, highlighter, Bridge::Tool::Highlighter, false, 1.0f, false, false);
					const auto shape = resolve(owner, penAppearance, Bridge::Tool::SolidLine, false, 1.0f, false, false);
					if (!Expect(marker.visible && marker.appearance.shape == DrawingCursorShape::Rectangle &&
						Near(marker.appearance.height, 30.0f) && Near(marker.appearance.opacity * marker.appearance.fillAlpha, 0.35f) &&
						shape.visible == (row < 5) && (!shape.visible || shape.appearance.shape == DrawingCursorShape::Circle),
						"dedicated marker and shape cursors remain visible during pen contact in every hide mode")) ++failures;
					if (row < 4)
					{
						pen.inverted = true;
						const auto inverted = resolve(owner, penAppearance, Bridge::Tool::Pen, true, 1.0f, false, false);
						pen.inverted = false;
						if (!Expect(inverted.visible && inverted.appearance.shape == DrawingCursorShape::EraserGripCircle,
							"inverted eraser takes precedence over dot contact hiding")) ++failures;
					}
					pen.inContact = mouse.inContact = false;
					if (!Expect(resolve(owner, penAppearance).visible == !systemMouse,
						"a real hover sample restores every selected pen cursor after contact")) ++failures;
				}
		pen.source = mouse.source = {};
		settings = {};

		// 非 Windows Ink 数位板：驱动注入的鼠标按开关使用数位板行并隐藏系统箭头。
		settings = {};
		settings.tablet = CursorStyle::HighlightDot;
		mouse.source = { SpeedEraser::SourceKind::ExternalPen, SpeedEraser::SourceRecognition::Unknown };
		const bool plainMouse = !resolve(mouseOwner, penAppearance).visible;
		settings.injectedMouseAsTablet = true;
		const auto injected = resolve(mouseOwner, penAppearance);
		const bool hideArrow = ShouldHideSystemDrawingCursor(mouseOwner, false, false, false, true, true, false, false, true);
		const bool keepArrow = !ShouldHideSystemDrawingCursor(mouseOwner, false, false, false, true, true, false, false, false);
		mouse.source = { SpeedEraser::SourceKind::Mouse, SpeedEraser::SourceRecognition::Unknown };
		const bool realMouse = !resolve(mouseOwner, penAppearance).visible;
		mouse.source = {};
		if (!Expect(plainMouse && injected.visible && injected.appearance.shape == DrawingCursorShape::Circle &&
			highlighted(injected) && hideArrow && keepArrow && realMouse,
			"injected tablet mouse uses the tablet row only when marked and enabled")) ++failures;

		// 移动模式不依赖笔样式或残留工具；退出后立即恢复原有绘制策略。
		for (const auto authority : { penOwner, mouseOwner, DrawingCursorPointerAuthority::Unknown })
			for (const bool valid : { false, true })
				for (const bool injectedTablet : { false, true })
				{
					if (!Expect(!ShouldHideSystemDrawingCursor(authority, true, true,
						valid, valid, false, false, false, injectedTablet, true),
						"whiteboard move restores the default system cursor for pen, tablet and mouse")) ++failures;
				}
		if (!Expect(ShouldHideSystemDrawingCursor(penOwner, false, false, true, false) &&
			ShouldHideSystemDrawingCursor(penOwner, false, false, true, false, true, true, false, false, true) &&
			ShouldHideSystemDrawingCursor(DrawingCursorPointerAuthority::Touch, false, false,
				false, false, true, false, false, false, true) &&
			!ShouldHideSystemDrawingCursor(penOwner, false, false, true, true, false, true, true, true, true),
			"move mode preserves touch hiding, real mouse takeover and ordinary drawing pen policy")) ++failures;

		// 触摸：默认无光标；逐接触生成所选软/硬笔光标，缩放按各自 DPI。
		const auto touchVisual = [&](CursorStyle style, float scale = 1.0f)
		{
			return MakeTouchPenDrawingCursorVisual(15.0f, 25.0f, penAppearance, style, scale);
		};
		const auto touchArrow = touchVisual(CursorStyle::Arrow);
		const auto scaledTouchArrow = touchVisual(CursorStyle::Arrow, 2.0f);
		const auto touchPen = touchVisual(CursorStyle::Pen, 2.0f);
		const auto touchDot = touchVisual(CursorStyle::Dot);
		const auto touchHighDot = touchVisual(CursorStyle::HighlightDot);
		if (!Expect(!touchVisual(CursorStyle::None).visible &&
			touchArrow.visible && touchArrow.appearance.shape == DrawingCursorShape::Arrow &&
			scaledTouchArrow.visible && Near(scaledTouchArrow.appearance.width, 26.0f) && Near(scaledTouchArrow.appearance.height, 39.0f) &&
			Near(scaledTouchArrow.appearance.outlineWidth, 1.0f) &&
			touchPen.visible && touchPen.appearance.shape == DrawingCursorShape::PenIcon &&
			Near(touchPen.appearance.width, 48.0f) && Near(touchPen.appearance.outlineWidth, 1.0f) &&
			touchDot.visible && Near(touchDot.x, 15.0f) && Near(touchDot.y, 25.0f) &&
			touchDot.appearance.shape == DrawingCursorShape::Circle && Near(touchDot.appearance.width, 9.0f) && plain(touchDot) &&
			touchHighDot.visible && highlighted(touchHighDot) &&
			!MakeTouchPenDrawingCursorVisual(NAN, 0.0f, penAppearance, CursorStyle::Dot, 1.0f).visible,
			"touch pen cursors follow the selected style at each contact")) ++failures;

		// 配置值、旧配置回落、设置页下拉索引与打包往返。
		bool packedOk = true;
		for (int m = 1; m <= 5; ++m) for (int t = 0; t <= 5; ++t) for (int u = 1; u <= 5; ++u)
			for (int v = 1; v <= 5; ++v) for (int i = 0; i < 2; ++i) for (int mode = 0; mode <= 3; ++mode)
			{
				const CursorSettings expected{ static_cast<CursorStyle>(m), static_cast<CursorStyle>(t),
					static_cast<CursorStyle>(u), static_cast<CursorStyle>(v), i != 0, static_cast<DotCursorHideMode>(mode) };
				const uint32_t bits = PackCursorSettings(expected);
				packedOk = packedOk && UnpackCursorSettings(bits) == expected && (bits & kCursorSettingsValidBit) != 0;
			}
		bool indexOk = true;
		for (const auto device : { CursorDevice::Mouse, CursorDevice::Touch, CursorDevice::ScreenPen, CursorDevice::Tablet })
			for (int index = 0; index < CursorStyleItemCount(device); ++index)
				indexOk = indexOk && CursorStyleToItemIndex(device, CursorStyleFromItemIndex(device, index)) == index;
		const CursorSettings defaults;
		if (!Expect(packedOk && indexOk && defaults.mouse == CursorStyle::Arrow && defaults.touch == CursorStyle::None &&
			defaults.screenPen == CursorStyle::Dot && defaults.tablet == CursorStyle::HighlightDot &&
			defaults.dotHideOnContact == DotCursorHideMode::ScreenPen &&
			RestoreDotCursorHideMode(-1) == DotCursorHideMode::ScreenPen && RestoreDotCursorHideMode(4) == DotCursorHideMode::ScreenPen &&
			CursorStyleItemCount(CursorDevice::Touch) == 6 && CursorStyleItemCount(CursorDevice::Mouse) == 5 &&
			CursorStyleFromItemIndex(CursorDevice::Touch, 0) == CursorStyle::None &&
			CursorStyleFromItemIndex(CursorDevice::Mouse, 0) == CursorStyle::Arrow,
			"cursor styles round-trip through packing and drop-down indices; no-cursor exists only for touch")) ++failures;
		if (!Expect(RestoreCursorStyle(CursorDevice::Mouse, 0) == CursorStyle::Arrow &&
			RestoreCursorStyle(CursorDevice::Touch, 0) == CursorStyle::None &&
			RestoreCursorStyle(CursorDevice::ScreenPen, 6) == CursorStyle::Dot &&
			RestoreCursorStyle(CursorDevice::Tablet, -1) == CursorStyle::HighlightDot &&
			RestoreDeviceCursorStyle(CursorDevice::Mouse, -1, 1, 1) == CursorStyle::HighlightPen &&
			RestoreDeviceCursorStyle(CursorDevice::Mouse, -1, 0, 1) == CursorStyle::Arrow &&
			RestoreDeviceCursorStyle(CursorDevice::ScreenPen, -1, 1, 1) == CursorStyle::Pen &&
			RestoreDeviceCursorStyle(CursorDevice::ScreenPen, -1, 1, 0) == CursorStyle::Dot &&
			RestoreDeviceCursorStyle(CursorDevice::Tablet, -1, 0, 1) == CursorStyle::HighlightPen &&
			RestoreDeviceCursorStyle(CursorDevice::Tablet, -1, 0, 0) == CursorStyle::HighlightDot &&
			RestoreDeviceCursorStyle(CursorDevice::Touch, -1, 1, 1) == CursorStyle::None &&
			RestoreDeviceCursorStyle(CursorDevice::Tablet, 4, 0, 1) == CursorStyle::Dot,
			"missing device keys inherit the legacy mouse/pen types and otherwise use per-device defaults")) ++failures;
	}

	void TestSystemTouchMoveFilter(int& failures)
	{
		MouseCursorMessageFilterInput input{
			.buttonDown = true, .pointerApiAvailable = true,
			.touchBarrierKnown = true, .messageTick = 118939656u, .touchBarrierTick = 118939218u,
			.sourceQuerySucceeded = true, .origin = IMO_SYSTEM, .touchSuppressed = true,
			.touchPositionKnown = true, .touchX = 2034, .touchY = 811, .mouseX = 2034, .mouseY = 811
		};
		// 回放 event=51 的按键态与 event=60 的非按键态，并覆盖多指使末点不同/不可得。
		for (int contact = 0; contact < 2; ++contact)
		{
			input.buttonDown = contact != 0;
			for (int position = 0; position < 3; ++position)
			{
				input.touchPositionKnown = position != 2;
				input.mouseX = position == 0 ? 2034 : 2359;
				const auto result = FilterMouseCursorMessage(input);
				if (!Expect(result.rejectionReason && result.systemRejected && !result.buttonBypass,
					"system move cannot reclaim touch with buttons or a different last contact")) ++failures;
			}
		}

		input.touchPositionKnown = true;
		input.mouseX = input.touchX;
		input.buttonDown = true;
		input.inputSource = IMDT_MOUSE;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"identified mouse can take over at the same touch position")) ++failures;
		input.inputSource = IMDT_TOUCHPAD;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"identified touchpad can take over")) ++failures;
		input.inputSource = IMDT_PEN;
		if (!Expect(FilterMouseCursorMessage(input).sourceRejected,
			"pen compatibility mouse still cannot publish a mouse sample")) ++failures;
		input.inputSource = IMDT_TOUCH;
		if (!Expect(FilterMouseCursorMessage(input).sourceRejected,
			"touch compatibility mouse still cannot publish a mouse sample")) ++failures;

		input.inputSource = IMDT_UNAVAILABLE;
		input.touchSuppressed = false;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"system move retains existing mouse behavior after confirmed takeover")) ++failures;
		input.touchSuppressed = true;
		input.sourceQuerySucceeded = false;
		input.pointerApiAvailable = false;
		if (!Expect(!FilterMouseCursorMessage(input).systemRejected &&
			!FilterMouseCursorMessage(input).rejectionReason,
			"missing or failed source API is not classified as system input")) ++failures;
		input.buttonDown = false;
		if (!Expect(FilterMouseCursorMessage(input).positionRejected,
			"Win7 stationary unknown move retains the existing fallback")) ++failures;
		++input.mouseX;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"Win7 actual mouse movement retains the existing takeover path")) ++failures;
		input.sourceQuerySucceeded = true;
		input.pointerApiAvailable = true;
		input.origin = IMO_INJECTED;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"application injection is not blanket classified as system touch move")) ++failures;
		input.origin = IMO_SYSTEM;
		input.message = WM_LBUTTONDOWN;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"system move rule does not alter button message policy")) ++failures;
	}

	void TestSystemTouchMoveSequence(int& failures)
	{
		DrawingCursorSampleMailbox mouseMailbox;
		DrawingCursorPointerAuthority persistentOwner = DrawingCursorPointerAuthority::Mouse;
		bool touchSuppressed = true; // RTS Down 清旧样本，Up 不恢复旧归属。
		mouseMailbox.Publish({ .x = 30.0f, .y = 40.0f, .valid = true });
		mouseMailbox.Clear();
		MouseCursorMessageFilterInput input{
			.buttonDown = true, .pointerApiAvailable = true,
			.touchBarrierKnown = true, .messageTick = 118939656u, .touchBarrierTick = 118939218u,
			.sourceQuerySucceeded = true, .origin = IMO_SYSTEM,
			.touchPositionKnown = true, .touchX = 2034, .touchY = 811, .mouseX = 2034, .mouseY = 811
		};
		// 测试生产过滤入口到 mailbox/visual 的组合；不模拟 Windows 的消息来源 API。
		const auto receiveMouse = [&]()
		{
			input.touchSuppressed = touchSuppressed;
			const auto decision = FilterMouseCursorMessage(input);
			if (decision.rejectionReason) return false;
			touchSuppressed = false;
			persistentOwner = DrawingCursorPointerAuthority::Mouse;
			mouseMailbox.Publish({ .x = static_cast<float>(input.mouseX),
				.y = static_cast<float>(input.mouseY), .valid = true, .inContact = input.buttonDown });
			return true;
		};
		const DrawingCursorAppearance eraser{
			DrawingCursorShape::EraserGripCircle, 64.0f, 64.0f, 1.0f, 1.0f, 1.0f, 0.5f };
		const auto primaryVisual = [&]()
		{
			DrawingCursorSample mouse;
			mouseMailbox.Read(mouse);
			return ResolvePrimaryDrawingCursorVisual({}, mouse,
				ResolveDrawingCursorVisualAuthority(persistentOwner, touchSuppressed, false, false),
				eraser, eraser, true, true);
		};
		if (!Expect(!receiveMouse() && !primaryVisual().visible &&
			MakeTouchEraserDrawingCursorVisual(2035.0f, 811.5f, eraser).visible,
			"event 51 leaves only the active touch eraser visual")) ++failures;
		// Touch Up 的兼容 Mouse Up 仍被拒绝，不能给错误的 Mouse 样本补一次释放。
		input.message = WM_LBUTTONUP;
		input.buttonDown = false;
		input.inputSource = IMDT_TOUCH;
		input.promotedPointerMessage = true;
		input.messageTick = 118939718u;
		if (!Expect(!receiveMouse() && !primaryVisual().visible,
			"touch up has no pressed primary cursor to leave behind")) ++failures;
		input.message = WM_MOUSEMOVE;
		input.inputSource = IMDT_UNAVAILABLE;
		input.promotedPointerMessage = false;
		input.messageTick = 118942828u;
		input.mouseX = input.touchX = 2359;
		input.mouseY = input.touchY = 762;
		if (!Expect(!receiveMouse() && !primaryVisual().visible && touchSuppressed,
			"event 60 does not resurrect a hover cursor after touch up")) ++failures;
		input.inputSource = IMDT_MOUSE;
		input.origin = IMO_HARDWARE;
		++input.messageTick;
		if (!Expect(receiveMouse() && primaryVisual().visible &&
			Near(primaryVisual().appearance.opacity, 0.5f),
			"real mouse immediately recovers normal hover at the same position")) ++failures;
	}

	void TestSystemPenMoveSequence(int& failures)
	{
		const DrawingCursorAppearance brush{ DrawingCursorShape::Circle, 9, 9, 0.2f, 0.4f, 0.8f };
		DrawingCursorAppearance marker{ DrawingCursorShape::Rectangle, 3.75f, 30, 1, 0.8f, 0.2f };
		marker.opacity = Bridge::kHighlighterCompositeOpacity;
		marker.fillAlpha = 1;
		const DrawingCursorSample oldMouse{ .x = 30, .y = 40, .qpc = 100, .valid = true };
		DrawingCursorSample penHover{ .x = 10, .y = 20, .qpc = 1000, .valid = true };
		penHover.source = { SpeedEraser::SourceKind::IntegratedPen, SpeedEraser::SourceRecognition::RtsCapabilities };
		struct DelayedMove { uint32_t tick; int x, y; bool held; };
		constexpr DelayedMove delayed[] = {
			{ 2000, 10, 20, false }, { 2000, 500, 600, false },
			{ 2000, 500, 600, true }, { 600000, 30, 40, false },
			{ 0x70000000u, 900, 700, true }
		};
		for (const auto style : { CursorStyle::Arrow, CursorStyle::HighlightPen })
		{
			DrawingCursorSampleMailbox penMailbox, mouseMailbox;
			mouseMailbox.Publish(oldMouse);
			auto owner = DrawingCursorPointerAuthority::Mouse;
			MouseCursorMessageFilterInput input{ .pointerApiAvailable = true,
				.sourceQuerySucceeded = true, .origin = IMO_SYSTEM };
			MouseCursorMessageFilterResult lastDecision;
			const auto receiveMouse = [&]()
			{
				DrawingCursorSample pen;
				penMailbox.Read(pen);
				input.penSampleValid = pen.valid;
				input.pointerAuthority = owner;
				lastDecision = FilterMouseCursorMessage(input);
				if (lastDecision.rejectionReason) return false;
				owner = ResolveDrawingCursorOwnerForPointerEvent(owner, DrawingCursorPointerAuthority::Mouse);
				mouseMailbox.Publish({ .x = static_cast<float>(input.mouseX),
					.y = static_cast<float>(input.mouseY), .qpc = input.messageTick,
					.valid = true, .inContact = input.buttonDown });
				return true;
			};
			const auto publishPen = [&](DrawingCursorSample sample)
			{
				penMailbox.Publish(sample);
				owner = ResolveDrawingCursorOwnerForPointerEvent(owner, DrawingCursorPointerAuthority::Pen);
			};
			const auto primaryVisual = [&](bool highlighter = false)
			{
				DrawingCursorSample pen, mouse;
				penMailbox.Read(pen); mouseMailbox.Read(mouse);
				const auto settings = Bridge::ResolveCursorSettingsForTool(
					highlighter ? Bridge::Tool::Highlighter : Bridge::Tool::Pen, CursorSettings{ .mouse = style });
				return ResolvePrimaryDrawingCursorVisual(pen, mouse, owner,
					highlighter ? marker : brush, brush, false, false, false,
					style == CursorStyle::Arrow, &settings, !highlighter);
			};
			// 默认系统样式不会画 Mouse 主视觉；新式会暴露同一错误接管，不能只遮掉图形。
			if (!Expect(primaryVisual().visible == (style == CursorStyle::HighlightPen),
				"only Drawing renders a valid mouse brush sample before pen ownership")) ++failures;
			DrawingCursorSample before;
			mouseMailbox.Read(before);
			for (int phase = 0; phase < 3; ++phase)
			{
				if (phase < 2)
				{
					auto sample = penHover; sample.inContact = phase == 1;
					publishPen(sample);
				}
				else penMailbox.Clear(); // Up/OutOfRange 清样本并保留 Pen owner。
				for (const auto& move : delayed)
				{
					input.messageTick = move.tick; input.mouseX = move.x; input.mouseY = move.y;
					input.buttonDown = move.held;
					const bool accepted = receiveMouse();
					DrawingCursorSample mouse;
					mouseMailbox.Read(mouse);
					const auto visual = primaryVisual();
					if (!Expect(!accepted && lastDecision.systemRejected &&
						owner == DrawingCursorPointerAuthority::Pen && mouse.sequence == before.sequence &&
						Near(mouse.x, oldMouse.x) && Near(mouse.y, oldMouse.y) &&
						visual.visible == (phase == 0) &&
						(!visual.visible || (visual.appearance.shape == DrawingCursorShape::Circle && Near(visual.x, penHover.x))),
						"delayed repeated system moves cannot overwrite pen ownership or the mouse mailbox")) ++failures;
				}
			}
			penMailbox.Clear(); // 重复终态也不能回弹仍在 mailbox 中的旧 Mouse。
			if (!Expect(!primaryVisual().visible && !primaryVisual(true).visible &&
				ShouldHideSystemDrawingCursor(owner, false, false, false, true, style == CursorStyle::Arrow),
				"both styles keep old mouse and highlighter previews hidden after pen leaves")) ++failures;

			struct Takeover { UINT message; INPUT_MESSAGE_DEVICE_TYPE device; INPUT_MESSAGE_ORIGIN_ID origin;
				bool querySucceeded, pointerApi; };
			const Takeover takeovers[] = {
				{ WM_MOUSEMOVE, IMDT_MOUSE, IMO_HARDWARE, true, true },
				{ WM_MOUSEMOVE, IMDT_TOUCHPAD, IMO_HARDWARE, true, true },
				{ WM_LBUTTONDOWN, IMDT_UNAVAILABLE, IMO_SYSTEM, true, true },
				{ WM_MOUSEMOVE, IMDT_UNAVAILABLE, IMO_SYSTEM, false, true },
				{ WM_MOUSEMOVE, IMDT_UNAVAILABLE, IMO_UNAVAILABLE, false, false },
				{ WM_MOUSEMOVE, IMDT_UNAVAILABLE, IMO_INJECTED, true, true },
				{ WM_MOUSEMOVE, IMDT_UNAVAILABLE, IMO_HARDWARE, true, true },
				{ WM_MOUSEMOVE, IMDT_UNAVAILABLE, IMO_UNAVAILABLE, true, true }
			};
			for (const auto& takeover : takeovers)
			{
				publishPen(penHover); penMailbox.Clear();
				input.message = takeover.message; input.inputSource = takeover.device; input.origin = takeover.origin;
				input.sourceQuerySucceeded = takeover.querySucceeded; input.pointerApiAvailable = takeover.pointerApi;
				input.buttonDown = takeover.message == WM_LBUTTONDOWN;
				const bool accepted = receiveMouse();
				const auto visual = primaryVisual();
				if (!Expect(accepted && owner == DrawingCursorPointerAuthority::Mouse &&
					!lastDecision.systemRejected && visual.visible == (style == CursorStyle::HighlightPen) &&
					(!visual.visible || visual.appearance.shape == DrawingCursorShape::PenIcon) &&
					ShouldHideSystemDrawingCursor(owner, false, false, false, true, style == CursorStyle::Arrow) ==
						(style == CursorStyle::HighlightPen),
					"real input, button down and existing API fallbacks immediately recover the selected mouse style")) ++failures;
			}
			// 真实接管后正常系统刷新仍沿既有 Mouse 行为；新 Pen 可以再次即时接管。
			input.message = WM_MOUSEMOVE; input.inputSource = IMDT_UNAVAILABLE;
			input.origin = IMO_SYSTEM; input.sourceQuerySucceeded = input.pointerApiAvailable = true;
			input.buttonDown = false;
			if (!Expect(receiveMouse() && owner == DrawingCursorPointerAuthority::Mouse,
				"system refresh remains accepted after mouse has already taken over")) ++failures;
			publishPen(penHover);
			if (!Expect(primaryVisual().visible && primaryVisual().appearance.shape == DrawingCursorShape::Circle &&
				Near(primaryVisual().x, penHover.x) && !receiveMouse(),
				"new pen hover immediately recovers pen preview and its system-move protection")) ++failures;
			penHover.inContact = true; publishPen(penHover); penHover.inContact = false;
			if (!Expect(!primaryVisual().visible && owner == DrawingCursorPointerAuthority::Pen,
				"new pen contact preserves its separate contact visibility rule")) ++failures;
			penMailbox.Clear();
			if (!Expect(!receiveMouse() && !primaryVisual().visible,
				"the next pen leave retains protection without a time window")) ++failures;
		}
	}

	void TestTouchCursorOwnership(int& failures)
	{
		const DrawingCursorSample penHover{ .x = 10.0f, .y = 20.0f, .valid = true };
		const DrawingCursorSample mouseHover{ .x = 30.0f, .y = 40.0f, .valid = true };
		const DrawingCursorAppearance eraser{
			DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f };
		// 最后一指 Up 后保持 Touch 视觉归属，旧 Pen/Mouse Hover 不能重新露出。
		const auto touchOwner = ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Pen, true, false, false);
		if (!Expect(touchOwner == DrawingCursorPointerAuthority::Touch &&
			!ResolvePrimaryDrawingCursorVisual(penHover, mouseHover, touchOwner,
				eraser, eraser, true, true).visible &&
			ShouldHideSystemDrawingCursor(touchOwner, false, false, true, true),
			"touch hides old application and system cursors")) ++failures;
		if (!Expect(ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Mouse, true, false, false) ==
			DrawingCursorPointerAuthority::Touch,
			"touch suppresses a stale mouse owner")) ++failures;
		if (!Expect(ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Mouse, true, true, true) ==
			DrawingCursorPointerAuthority::Mouse,
			"real mouse takeover during touch pan remains visible")) ++failures;
		if (!Expect(ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Mouse, false, false, false) ==
			DrawingCursorPointerAuthority::Mouse &&
			ResolveDrawingCursorVisualAuthority(
				DrawingCursorPointerAuthority::Pen, false, false, false) ==
			DrawingCursorPointerAuthority::Pen,
			"new mouse or pen input restores normal cursor ownership")) ++failures;
		if (!Expect(ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_TOUCH) &&
			ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_PEN) &&
			!ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_MOUSE) &&
			!ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_TOUCHPAD),
			"identified touch and pen compatibility mouse messages are ignored")) ++failures;
		if (!Expect(ShouldIgnoreMouseCursorMessage(true, false, false,
			true, 101u, 100u, IMDT_UNAVAILABLE) &&
			ShouldIgnoreMouseCursorMessage(false, false, false,
			true, 100u, 100u, IMDT_UNAVAILABLE) &&
			!ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_UNAVAILABLE),
			"Win7 compatibility signature and touch barrier remain effective")) ++failures;
		// Touch Up 后来源缺失的原位 Move 不能恢复旧鼠标光标；真正移动仍可接管。
		if (!Expect(ShouldIgnoreUnattributedTouchMouseMove(
			true, IMDT_UNAVAILABLE, true, 2500, 1029, 2500, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				true, IMDT_UNAVAILABLE, true, 2500, 1029, 2501, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				true, IMDT_MOUSE, true, 2500, 1029, 2500, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				false, IMDT_UNAVAILABLE, true, 2500, 1029, 2500, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				true, IMDT_UNAVAILABLE, false, 2500, 1029, 2500, 1029),
			"unattributed touch-position move cannot reclaim the cursor")) ++failures;
	}

	int BenchmarkContactMove()
	{
		LARGE_INTEGER frequency{};
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
			return 1;
		constexpr int warmupRuns = 3;
		constexpr int measuredRuns = 11;
		constexpr int iterations = 20000;
		std::vector<double> samples;
		samples.reserve(measuredRuns);
		for (int run = -warmupRuns; run < measuredRuns; ++run)
		{
			ContactInputCoordinator input;
			if (!input.PublishDown(1, 1, InputDeviceType::Pen,
				MakeSnapshot(0.0f, 0.0f, ContactPhase::Down)))
				return 1;
			ContactRecord* record = nullptr;
			if (!input.TryDequeue(record) || !record) return 1;
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot move = MakeSnapshot(0.0f, 0.0f, ContactPhase::Move);
			ContactSnapshot observed{};
			std::uint64_t previousSequence = 0;
			bool complete = true;
			LARGE_INTEGER start{}, end{};
			QueryPerformanceCounter(&start);
			for (int index = 0; index < iterations; ++index)
			{
				// 每个 Move 都走生产发布和一致快照读取，避免把丢样误算成吞吐收益。
				move.position.x = static_cast<float>(index);
				move.qpc = index + 2;
				complete &= input.PublishMove(1, 1, move);
				complete &= input.TryReadSnapshot(handle, observed);
				complete &= observed.position.x == static_cast<float>(index) &&
					observed.sequence > previousSequence;
				previousSequence = observed.sequence;
			}
			QueryPerformanceCounter(&end);
			ContactSnapshot terminal{};
			ContactSnapshot up = MakeSnapshot(1.0f, 1.0f, ContactPhase::Up);
			up.qpc = iterations + 2;
			complete &= input.PublishUp(1, 1, up);
			complete &= input.TryReadSnapshot(handle, terminal);
			input.Recycle(handle);
			if (!complete || observed.position.x != static_cast<float>(iterations - 1) ||
				terminal.phase != ContactPhase::Up)
				return 1;
			if (run >= 0)
				samples.push_back(static_cast<double>(end.QuadPart - start.QuadPart) *
					1.0e9 / static_cast<double>(frequency.QuadPart) / iterations);
		}
		const std::vector<double> runMeans = samples;
		std::sort(samples.begin(), samples.end());
		const double median = samples[samples.size() / 2];
		const double noise = median > 0.0
			? (samples.back() - samples.front()) * 100.0 / median : 0.0;
		std::cout << "BENCH_DRAW3 contact_move_publish_read iterations=" << iterations
			<< " warmup_runs=" << warmupRuns << " measured_runs=" << measuredRuns
			<< " median_run_mean_ns=" << median
			<< " max_run_mean_ns=" << samples.back()
			<< " noise_pct=" << noise << " run_mean_samples_ns=";
		for (size_t index = 0; index < runMeans.size(); ++index)
			std::cout << (index == 0 ? "" : ",") << runMeans[index];
		std::cout << '\n';
		return 0;
	}
}

namespace
{
	void TestConsumerFatalAdmissionGate(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3;
		ContactInputCoordinator input;
		input.SetConsumerFatal();
		const auto fatalRevision = input.AdmissionRevision();
		input.SetAdmissionBlocked(false);
		input.SetShortcutAdmissionBlocked(true);
		input.SetShortcutAdmissionBlocked(false);
		if (!Expect(input.ConsumerFatal() && input.AdmissionBlocked() &&
			!input.BaseAdmissionBlocked() && input.AdmissionRevision() == fatalRevision,
			"consumer fatal survives ordinary scene refresh and shortcut release")) ++failures;
		input.SetConsumerFatal();
		if (!Expect(input.AdmissionRevision() == fatalRevision,
			"repeated fatal transition is monotonic and idempotent")) ++failures;
		input.PublishDown(89, 1, InputDeviceType::Pen, MakeSnapshot(1, 1, ContactPhase::Down));
		ContactRecord* record = nullptr;
		while (input.TryDequeue(record) && !record) input.AcknowledgeControlWake();
		const ContactHandle blocked{ record, record ? record->Generation() : 0 };
		if (!Expect(record && !input.ContactAdmitted(blocked),
			"real new Down cannot draw after fatal even when all ordinary gates are open")) ++failures;
		input.PublishUp(89, 1, MakeSnapshot(1, 1, ContactPhase::Up));
		if (record) input.Recycle(blocked);
		input.ResetForNextRun(); // 本夹具无并发 producer/consumer，模拟真正停止的生命周期边界。
		if (!Expect(!input.ConsumerFatal() && !input.AdmissionBlocked() &&
			input.AdmissionRevision() > fatalRevision,
			"only stopped lifecycle reset clears consumer fatal and advances admission generation")) ++failures;
	}

	void TestShortcutAdmissionGateComposition(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3;
		ContactInputCoordinator input;
		const auto initial = input.AdmissionRevision();
		input.SetShortcutAdmissionBlocked(true);
		const auto leaseRevision = input.AdmissionRevision();
		if (!Expect(input.AdmissionBlocked() && !input.BaseAdmissionBlocked() && leaseRevision != initial,
			"shortcut lease closes real production contact admission")) ++failures;
		const bool downPublished = input.PublishDown(88, 1, InputDeviceType::Pen,
			MakeSnapshot(1, 1, ContactPhase::Down));
		ContactRecord* record = nullptr;
		while (input.TryDequeue(record) && !record) input.AcknowledgeControlWake();
		const ContactHandle blockedContact{ record, record ? record->Generation() : 0 };
		if (!Expect(downPublished && record && !input.ContactAdmitted(blockedContact),
			"real producer route retains terminal ownership but cannot draw during shortcut transaction")) ++failures;
		input.PublishUp(88, 1, MakeSnapshot(1, 1, ContactPhase::Up));
		if (record) input.Recycle(blockedContact);
		input.SetAdmissionBlocked(true); // 模拟 lease 期间 PPT load/fatal 关闭基础门。
		input.SetShortcutAdmissionBlocked(false);
		if (!Expect(input.AdmissionBlocked() && input.BaseAdmissionBlocked() && input.AdmissionRevision() == leaseRevision,
			"releasing shortcut lease preserves independently closed base gate")) ++failures;
		input.SetShortcutAdmissionBlocked(true);
		input.SetAdmissionBlocked(false);
		if (!Expect(input.AdmissionBlocked() && !input.BaseAdmissionBlocked(),
			"PPT gate opening does not release an active shortcut lease")) ++failures;
		input.SetShortcutAdmissionBlocked(false);
		if (!Expect(!input.AdmissionBlocked() && input.AdmissionRevision() > leaseRevision,
			"last owner release advances admission revision and allows fresh contacts")) ++failures;
	}
	void TestNavigationHistory(int& failures)
	{
		const auto check = [&](bool value, const char* message) { if (!Expect(value, message)) ++failures; };
		for (int inputCase = 0; inputCase < 4; ++inputCase)
		{
			const auto device = inputCase == 0 ? InputDeviceType::Touch : inputCase == 1 ? InputDeviceType::MouseLeft : InputDeviceType::Pen;
			const bool inverted = inputCase == 3;
			ContactInputCoordinator input;
			const auto sample = [inverted](float x, int64_t qpc, ContactPhase phase)
			{ auto value = MakeSnapshot(x, 0, phase); value.qpc = qpc; value.isInvertedCursor = inverted; return value; };
			check(input.PublishDown(71, 1, device, sample(0, 1, ContactPhase::Down)), "navigation Down published");
			ContactRecord* first = nullptr;
			check(input.TryDequeue(first) && first, "navigation Down dequeued");
			if (!first) continue;
			const ContactHandle handle{ first, first->Generation() };
			input.PublishMove(71, 1, sample(48, 17, ContactPhase::Move));
			input.PublishMove(71, 1, sample(96, 33, ContactPhase::Move));
			input.PublishMove(71, 1, sample(72, 41, ContactPhase::Move));
			input.PublishUp(71, 1, sample(48, 49, ContactPhase::Up));
			std::vector<ContactNavigationEvent> events;
			const auto status = input.DrainNavigationHistory(events);
			check(events.size() == 5 && status.dropped == 0, "navigation preserves all callback samples through Up");
			if (events.size() == 5)
			{
				check(events.front().snapshot.isInvertedCursor == inverted && events.back().snapshot.isInvertedCursor == inverted,
					"navigation preserves actual tip/tail identity from Down through terminal");
				check(events[3].snapshot.phase == ContactPhase::Move && events[3].snapshot.position.x == 72 &&
					events[4].snapshot.phase == ContactPhase::Up && events[3].ordinal < events[4].ordinal,
					"terminal cannot overwrite final reverse Move");
			}
			ContactSnapshot latest;
			check(input.TryReadSnapshot(handle, latest) && latest.phase == ContactPhase::Up && latest.position.x == 48,
				"drawing latest mailbox still keeps original terminal contract");
			input.Recycle(handle);

			input.PublishDown(71, 2, device, sample(0, 80, ContactPhase::Down));
			ContactRecord* a = nullptr;
			check(input.TryDequeue(a) && a, "watermark first Down dequeued");
			input.DrainNavigationHistory(events);
			input.PublishDown(71, 3, device, sample(100, 100, ContactPhase::Down));
			input.PublishUp(71, 2, sample(30, 110, ContactPhase::Up));
			const auto blocked = input.DrainNavigationHistory(events);
			check(events.empty() && blocked.pending == 2, "unregistered Down fences later Up in navigation stream");
			ContactRecord* next = nullptr;
			check(input.TryDequeue(next) && next, "watermark second Down dequeued");
			input.DrainNavigationHistory(events);
			check(events.size() == 2 && events[0].snapshot.phase == ContactPhase::Down &&
				events[1].snapshot.phase == ContactPhase::Up, "Down and Up delivered together after registration watermark");
			if (a) input.Recycle({ a, a->Generation() });
			input.PublishUp(71, 3, sample(100, 120, ContactPhase::Up));
			if (next) input.Recycle({ next, next->Generation() });
		}
	}

	void TestNavigationHistoryOverflow(int& failures)
	{
		ContactInputCoordinator input;
		input.PublishDown(72, 1, InputDeviceType::Touch, MakeSnapshot(0, 0, ContactPhase::Down));
		ContactRecord* first = nullptr;
		if (!input.TryDequeue(first) || !first) { ++failures; return; }
		const uint64_t downEpoch = first->NavigationEpoch();
		for (size_t i = 0; i <= kContactNavigationHistoryCapacity; ++i)
			input.PublishMove(72, 1, MakeSnapshot(static_cast<float>(i), 0, ContactPhase::Move));
		const auto status = input.NavigationHistoryStatus();
		if (!Expect(status.epoch != downEpoch && status.dropped == kContactNavigationHistoryCapacity &&
			status.pending <= kContactNavigationHistoryCapacity, "navigation overflow is bounded and invalidates old Down epoch")) ++failures;
		input.PublishDown(72, 2, InputDeviceType::Touch, MakeSnapshot(0, 0, ContactPhase::Down));
		ContactRecord* next = nullptr;
		if (!Expect(input.TryDequeue(next) && next && next->NavigationEpoch() == status.epoch,
			"fresh Down after historical overflow owns the new complete epoch")) ++failures;
		input.PublishUp(72, 1, MakeSnapshot(1, 0, ContactPhase::Up));
		input.PublishUp(72, 2, MakeSnapshot(1, 0, ContactPhase::Up));
		input.Recycle({ first, first->Generation() });
		if (next) input.Recycle({ next, next->Generation() });
	}

	void TestNavigationUrgentFrameWake(int& failures)
	{
		ContactInputCoordinator input;
		input.PublishControlWake();
		const ULONGLONG before = GetTickCount64();
		input.WaitForFrameDeadline(1000.0);
		if (!Expect(GetTickCount64() - before < 500, "already-published control wake interrupts active frame budget")) ++failures;
		ContactRecord* record = nullptr;
		while (input.TryDequeue(record)) if (!record) input.AcknowledgeControlWake();
		std::thread producer([&]() { Sleep(20); input.PublishControlWake(); });
		const ULONGLONG waiting = GetTickCount64();
		input.WaitForFrameDeadline(1000.0);
		producer.join();
		if (!Expect(GetTickCount64() - waiting < 500, "new control wake interrupts active frame budget")) ++failures;

		ContactInputCoordinator ordinary;
		ordinary.PublishDown(73, 1, InputDeviceType::Touch, MakeSnapshot(0, 0, ContactPhase::Down));
		ordinary.WaitForFrameDeadline(0.0); // 消费此前 Down 的 urgent generation。
		ordinary.ArmIdleMoveWake();
		std::thread mover([&]() { Sleep(20); ordinary.PublishMove(73, 1, MakeSnapshot(10, 0, ContactPhase::Move)); });
		const ULONGLONG moveWait = GetTickCount64();
		ordinary.WaitForFrameDeadline(100.0);
		mover.join();
		if (!Expect(GetTickCount64() - moveWait >= 80, "ordinary Move wake does not bypass active frame budget")) ++failures;
	}

	void TestTouchpadIngressInputIsolation(int& failures)
	{
		struct Viewport
		{
			unsigned options = 0, configured = 0, contacts = 0;
			HRESULT ActivateConfiguration(DIRECTMANIPULATION_CONFIGURATION) { ++configured; return S_OK; }
			HRESULT SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS value) { options = value; return S_OK; }
		};
		Viewport viewport;
		failures += !Expect(SUCCEEDED(Detail::ConfigureTouchpadViewport(viewport)), "production touchpad setup succeeds");
		struct InputCase { InputDeviceType device; POINTER_INPUT_TYPE pointer; bool inverted; };
		for (const auto& source : { InputCase{InputDeviceType::MouseLeft, PT_MOUSE, false},
			InputCase{InputDeviceType::Touch, PT_TOUCH, false}, InputCase{InputDeviceType::Pen, PT_PEN, false},
			InputCase{InputDeviceType::Pen, PT_PEN, true} })
		{
			ContactInputCoordinator input;
			bool delivered = false;
			const auto ordinary = [&]()
			{
				auto down = MakeSnapshot(10, 20, ContactPhase::Down); down.qpc = 10; down.isInvertedCursor = source.inverted;
				auto move = down; move.phase = ContactPhase::Move; move.qpc = 20; move.position.x = 50;
				auto up = move; up.phase = ContactPhase::Up; up.qpc = 30;
				delivered = input.PublishDown(0xAB21, 1, source.device, down) &&
					input.PublishMove(0xAB21, 1, move) && input.PublishUp(0xAB21, 1, up);
			};
			const bool associated = Detail::DispatchTouchpadHitTest(true, true, source.pointer, 1,
				[&](UINT32) { ++viewport.contacts; return true; });
			// 负对照模拟文档定义的自动命中：缺少 explicit 时，已有 DM 手势会先关联别类 Down。
			const auto deliverThroughPolicy = [&](unsigned options)
			{
				if (options & DIRECTMANIPULATION_VIEWPORT_OPTIONS_EXPLICITHITTEST)
					if (options & DIRECTMANIPULATION_VIEWPORT_OPTIONS_INPUT) ordinary();
			};
			deliverThroughPolicy(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE);
			failures += !Expect(!delivered, "negative control catches automatic viewport stealing ordinary input");
			deliverThroughPolicy(viewport.options);
			failures += !Expect(!associated && !viewport.contacts && delivered, "configured viewport preserves ordinary input into real coordinator");
			ContactRecord* record = nullptr;
			if (!Expect(input.TryDequeue(record) && record, "preserved source Down is dequeued")) { ++failures; continue; }
			const ContactHandle handle{ record, record->Generation() };
			std::vector<ContactNavigationEvent> history;
			const auto status = input.DrainNavigationHistory(history);
			failures += !Expect(status.pending == 0 && record->DeviceType() == source.device && history.size() == 3 &&
				history.front().snapshot.isInvertedCursor == source.inverted && history.back().snapshot.phase == ContactPhase::Up,
				"mouse touch pen and tail preserve real navigation history through DM setup boundary");
			input.Recycle(handle);
		}
		failures += !Expect(Detail::DispatchTouchpadHitTest(true, true, PT_TOUCHPAD, 7,
			[&](UINT32 id) { ++viewport.contacts; return id == 7; }) && viewport.contacts == 1 && viewport.configured == 1,
			"only explicit touchpad is associated without per-contact reconfiguration");
		failures += !Expect(!Detail::DispatchTouchpadHitTest(false, true, PT_TOUCHPAD, 7, [](UINT32) { return true; }) &&
			!Detail::DispatchTouchpadHitTest(true, false, PT_TOUCHPAD, 7, [](UINT32) { return true; }),
			"closed admission and unknown source never associate native contacts");
	}

	void TestTouchpadFrontendLifecycle(int& failures)
	{
		Detail::TouchpadNavigationBridge bridge; bridge.enabled.store(true);
		Detail::TouchpadNavigationFrontend frontend(bridge);
		std::vector<TouchpadNavigationSample> samples;
		const auto epoch = bridge.Epoch();
		failures += !Expect(frontend.Associate(epoch, 5, 7, [](UINT32) { return true; }) &&
			!bridge.Active() && !bridge.NeedsUpdate() && !frontend.HasConfirmedPhase() && !bridge.RunningAt(15, 5),
			"SetContact success without recognition cannot hold other devices or subscribe frames forever");
		failures += !Expect(!frontend.PublishStatus(TouchpadNavigationPhase::Ready, 99, 0, 10) && frontend.Accepting(),
			"initial READY before recognition does not retire the association");
		frontend.Interaction(true);
		failures += !Expect(!frontend.PublishStatus(TouchpadNavigationPhase::Ready, 99, 0, 12) && frontend.Accepting(),
			"repeated READY leaves real pending interaction alive until END or recognized motion");
		failures += !Expect(bridge.NeedsUpdate() && !bridge.Active() && !bridge.RunningAt(15, 5),
			"BEGIN schedules manual updates before a gesture is recognized without acquiring ownership");
		bridge.Drain(samples);
		failures += !Expect(samples.empty(), "pending interaction does not manufacture native Running samples");
		frontend.Interaction(false);
		failures += !Expect(!bridge.NeedsUpdate() && !frontend.Accepting(), "unrecognized interaction END closes pending without timeout");

		frontend.Associate(epoch, 5, 7, [&](UINT32) { frontend.Interaction(true); return true; });
		failures += !Expect(frontend.PublishStatus(TouchpadNavigationPhase::Running, 0, 0, 20) && bridge.RunningAt(21, 5),
			"actual Running starts native interval and ownership");
		frontend.PublishStatus(TouchpadNavigationPhase::Running, 12.5, -2.5, 22);
		frontend.PublishStatus(TouchpadNavigationPhase::Inertia, 16.5, -3.5, 24);
		frontend.Interaction(false);
		failures += !Expect(bridge.NeedsUpdate() && bridge.Active() && !bridge.Running(), "END does not truncate native inertia");
		frontend.PublishStatus(TouchpadNavigationPhase::Ready, 18.5, -4.5, 26);
		bridge.Drain(samples);
		failures += !Expect(samples.size() == 3 && samples.front().cumulativeX == 12.5 &&
			samples.back().cumulativeX == 18.5 && !bridge.NeedsUpdate() && !frontend.Accepting(),
			"confirmed lifecycle retains geometry and closes activity at READY");

		const auto activeEpoch = bridge.Epoch();
		frontend.Associate(activeEpoch, 5, 7, [&](UINT32) { frontend.Interaction(true); return true; });
		bridge.Cancel(28); // 真实其他来源 Down 取消等待识别的 native association。
		frontend.Interaction(true);
		failures += !Expect(!frontend.Accepting() && !bridge.NeedsUpdate() && !bridge.RunningAt(29, 5),
			"physical takeover cancels pending and late callbacks do not revive it");
		const auto currentEpoch = bridge.Epoch();
		failures += !Expect(frontend.Associate(currentEpoch, 5, 7, [&](UINT32)
			{ frontend.Interaction(true); frontend.PublishStatus(TouchpadNavigationPhase::Running, 0, 0, 29);
			  frontend.PublishStatus(TouchpadNavigationPhase::Ready, 0, 0, 30); return true; }) &&
			!frontend.Accepting() && !bridge.NeedsUpdate(), "synchronous READY remains ended after SetContact returns S_OK");
		failures += !Expect(!frontend.Associate(bridge.Epoch(), 5, 7, [&](UINT32)
			{ bridge.Cancel(32); return true; }) && !frontend.Accepting(), "S_OK cannot overwrite synchronous epoch cancellation");
		failures += !Expect(!frontend.Associate(bridge.Epoch(), 5, 7, [&](UINT32)
			{ frontend.Interaction(true); return false; }) && !bridge.NeedsUpdate() && !bridge.Active(),
			"failed association retires the interaction rather than leaving a phantom hold");
	}

	void TestTouchpadNavigationRetirement(int& failures)
	{
		Detail::TouchpadNavigationRetirement retirement;
		retirement.handlerRegistered = retirement.managerActivated = true;
		unsigned removes = 0, abandons = 0, deactivates = 0;
		bool removeOk = false, abandonOk = false, deactivateOk = false;
		auto complete = [&]()
		{
			return retirement.Complete([&]() { ++removes; return removeOk; },
				[&]() { ++abandons; return abandonOk; }, [&]() { ++deactivates; return deactivateOk; });
		};
		failures += !Expect(!complete() && retirement.handlerRegistered && abandons == 0 && deactivates == 0,
			"touchpad failed handler removal retains owner and defers later COM retirement");
		removeOk = true;
		failures += !Expect(!complete() && !retirement.handlerRegistered && !retirement.viewportAbandoned && deactivates == 0,
			"touchpad failed Abandon does not prove viewport retirement");
		abandonOk = true;
		failures += !Expect(!complete() && retirement.viewportAbandoned && retirement.managerActivated,
			"touchpad failed Deactivate retains remaining manager responsibility");
		const auto previousRemoves = removes, previousAbandons = abandons;
		deactivateOk = true;
		failures += !Expect(complete() && !retirement.managerActivated && removes == previousRemoves && abandons == previousAbandons,
			"touchpad retry completes only unretired COM step");
		const auto previousDeactivates = deactivates;
		failures += !Expect(complete() && deactivates == previousDeactivates,
			"touchpad completed retirement is idempotent");
	}

	void TestTouchpadNavigationBridge(int& failures)
	{
		Detail::TouchpadNavigationBridge bridge;
		unsigned wakes = 0;
		bridge.SetWake(+[](void* context) { ++*static_cast<unsigned*>(context); }, &wakes);
		const auto firstEpoch = bridge.Epoch();
		bridge.Publish(firstEpoch, TouchpadNavigationPhase::Running, 0, 0, 10);
		for (int i = 1; i <= 1000; ++i)
			bridge.Publish(firstEpoch, TouchpadNavigationPhase::Running, i * 0.125, -i * 0.25, 10 + i);
		failures += !Expect(wakes == 1 && bridge.Running() && bridge.StartQpc() == 10,
			"touchpad cumulative moves keep first contact time and only wake on begin");
		bridge.Publish(firstEpoch, TouchpadNavigationPhase::Inertia, 128, -252, 1011);
		bridge.Publish(firstEpoch, TouchpadNavigationPhase::Ready, 130, -254, 1012);
		std::vector<TouchpadNavigationSample> samples;
		failures += !Expect(bridge.Drain(samples) == firstEpoch && samples.size() == 3 &&
			samples[0].cumulativeX == 125 && samples[0].cumulativeY == -250 &&
			samples[1].phase == TouchpadNavigationPhase::Inertia && samples[2].cumulativeX == 130 && !bridge.Active(),
			"touchpad coalesces cumulative geometry but retains inertia and final ready");
		bridge.Publish(firstEpoch, TouchpadNavigationPhase::Running, 0, 0, 1020);
		const auto secondEpoch = bridge.Cancel();
		const unsigned afterCancel = wakes;
		failures += !Expect(!bridge.Publish(firstEpoch, TouchpadNavigationPhase::Inertia, 999, 999, 1021),
			"touchpad cancelled epoch rejects delayed callback");
		bridge.Publish(secondEpoch, TouchpadNavigationPhase::Running, 0, 0, 1022);
		failures += !Expect(wakes == afterCancel + 1 && bridge.Running() && bridge.StartQpc() == 1022,
			"touchpad new running after cancel wakes and starts fresh baseline");
		bridge.Publish(secondEpoch, TouchpadNavigationPhase::Running, std::numeric_limits<double>::infinity(), 0, 1023);
		failures += !Expect(bridge.Drain(samples) == secondEpoch && samples.size() == 1 && samples[0].cumulativeX == 0,
			"touchpad rejects nonfinite transform without replacing valid geometry");
		for (int i = 0; i != 65; ++i)
			bridge.Publish(secondEpoch, i % 2 ? TouchpadNavigationPhase::Inertia : TouchpadNavigationPhase::Running, i, i, 2000 + i);
		const auto overflowEpoch = bridge.Drain(samples);
		failures += !Expect(overflowEpoch != secondEpoch && samples.empty() && !bridge.Active(),
			"touchpad phase overflow cancels batch rather than losing terminal boundary");
		const unsigned beforeRestart = wakes;
		bridge.Publish(overflowEpoch, TouchpadNavigationPhase::Running, 0, 0, 3000);
		failures += !Expect(wakes == beforeRestart + 1, "touchpad first begin after overflow wakes");
		bridge.SetWake(nullptr, nullptr);
		Detail::TouchpadNavigationBridge history;
		const auto epoch = history.Epoch();
		history.Publish(epoch, TouchpadNavigationPhase::Running, 0, 0, 12);
		history.Publish(epoch, TouchpadNavigationPhase::Ready, 20, 0, 22);
		failures += !Expect(!history.Running() && history.RunningAt(20) && history.RunningAt(22) &&
			!history.RunningAt(11) && !history.RunningAt(23),
			"touchpad historical running overlap includes terminal QPC after latest ready");
		history.Publish(epoch, TouchpadNavigationPhase::Running, 0, 0, 30);
		history.Publish(epoch, TouchpadNavigationPhase::Inertia, 10, 0, 34);
		history.Publish(epoch, TouchpadNavigationPhase::Ready, 15, 0, 40);
		failures += !Expect(history.RunningAt(20) && history.RunningAt(32) && history.RunningAt(34) &&
			!history.RunningAt(28) && !history.RunningAt(35),
			"touchpad retains multiple running intervals and excludes inertial tail");
		history.Drain(samples);
		Detail::TouchpadNavigationBridge revisionHistory;
		const auto revisionEpoch = revisionHistory.Epoch();
		revisionHistory.Publish(revisionEpoch, TouchpadNavigationPhase::Running, 0, 0, 12, 2);
		revisionHistory.Publish(revisionEpoch, TouchpadNavigationPhase::Ready, 20, 0, 22, 4);
		revisionHistory.Drain(samples);
		failures += !Expect(samples.size() == 2 && samples[0].admissionRevision == 2 && samples[1].admissionRevision == 2 &&
			revisionHistory.RunningAt(20, 2) && !revisionHistory.RunningAt(20, 4),
			"touchpad locks begin admission revision through terminal and scopes historical ownership");
		Detail::TouchpadNavigationBridge takeover;
		const auto takeoverEpoch = takeover.Epoch();
		takeover.Publish(takeoverEpoch, TouchpadNavigationPhase::Running, 0, 0, 10);
		takeover.Publish(takeoverEpoch, TouchpadNavigationPhase::Ready, 5, 0, 14);
		takeover.Publish(takeoverEpoch, TouchpadNavigationPhase::Running, 0, 0, 18);
		takeover.Publish(takeoverEpoch, TouchpadNavigationPhase::Ready, 10, 0, 22);
		const auto afterTakeover = takeover.Cancel(16);
		failures += !Expect(takeover.RunningAt(12) && !takeover.RunningAt(20) &&
			takeover.Publish(afterTakeover, TouchpadNavigationPhase::Running, 0, 0, 21),
			"touchpad takeover trims future ownership but preserves earlier overlap and accepts fresh epoch");
		for (int i = 0; i != 64; ++i)
		{
			const auto begin = 100 + i * 4;
			history.Publish(epoch, TouchpadNavigationPhase::Running, 0, 0, begin);
			history.Publish(epoch, TouchpadNavigationPhase::Ready, 1, 0, begin + 1);
			history.Drain(samples);
		}
		failures += !Expect(history.RunningAt(11) && !history.RunningAt(50) && history.RunningAt(352),
			"touchpad discarded history is conservatively unknown while retained gaps stay non-overlapping");
	}

}

int RunDraw3ContactInputTests()
{
	int failures = 0;
	TestTouchpadIngressInputIsolation(failures);
	TestTouchpadFrontendLifecycle(failures);
	TestTouchpadNavigationRetirement(failures);
	TestTouchpadNavigationBridge(failures);
	TestContactLifecycle(failures);
	TestNavigationHistory(failures);
	TestNavigationHistoryOverflow(failures);
	TestNavigationUrgentFrameWake(failures);
	TestShortcutAdmissionGateComposition(failures);
	TestConsumerFatalAdmissionGate(failures);
	TestInvalidAndWakeContracts(failures);
	TestIdleMoveWakeSubscription(failures);
	TestConcurrentIdleMoveBurst(failures);
	TestFailedControlWakeKeepsBridgeLive(failures);
	TestFailedControlWakeUnderContinuousContacts(failures);
	TestFailedControlWakeInterruptsBlockingConsumer(failures);
	TestControlWakeSeparatesAcceptedContacts(failures, false);
	TestControlWakeSeparatesAcceptedContacts(failures, true);
	TestDistinctCommandsKeepTheirIngressBoundaries(failures);
	TestFailedCommandMarkerKeepsOrdinalAndContactBoundary(failures);
#if defined(DRAW3_CONTACT_TESTING)
	TestCommandFallbackPublishedAfterConsumerProbe(failures);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::Command, false);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::Down, false);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::Down, false, ContactPhase::Cancelled);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::General, false);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::Command, true);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::Down, true);
	TestCommandFallbackDefersIngress(failures, DeferredIngressTestKind::General, true);
#endif
	TestCommandWakeReservationAndGenerationReset(failures);
	TestPageAdmissionAndQuarantine(failures);
	TestClosingDiscardLiveness(failures);
	TestCursorOpacityContracts(failures);
	TestPerDeviceCursorStyles(failures);
	TestTouchCursorOwnership(failures);
	TestSystemTouchMoveFilter(failures);
	TestSystemTouchMoveSequence(failures);
	TestSystemPenMoveSequence(failures);
	return failures;
}

int RunDraw3ContactInputBenchmarks()
{
	return BenchmarkContactMove();
}
