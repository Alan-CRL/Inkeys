#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"

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

int RunDraw3ContactInputTests()
{
	int failures = 0;
	TestContactLifecycle(failures);
	TestInvalidAndWakeContracts(failures);
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
	TestTouchCursorOwnership(failures);
	TestSystemTouchMoveFilter(failures);
	TestSystemTouchMoveSequence(failures);
	return failures;
}

int RunDraw3ContactInputBenchmarks()
{
	return BenchmarkContactMove();
}
