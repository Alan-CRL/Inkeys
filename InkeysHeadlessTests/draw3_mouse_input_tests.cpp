#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.MouseInput.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

import Inkeys.Drawing.Draw3.contact_input;

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	using Mouse = Detail::MouseInputState<ContactInputCoordinator, ContactSnapshot,
		InputDeviceType, ContactPhase>;

	bool Expect(bool condition, const char* name, int& failures)
	{
		if (!condition)
		{
			std::cerr << "[Draw3MouseInput] failed: " << name << '\n';
			++failures;
		}
		return condition;
	}

	ContactHandle Take(ContactInputCoordinator& input, int& failures)
	{
		ContactRecord* record = nullptr;
		// 页边界准入会发布 General ControlWake；遵守真实 ingress 合同，先消费并确认控制标记。
		while (input.TryDequeue(record))
		{
			if (record) return { record, record->Generation() };
			input.AcknowledgeControlWake();
		}
		Expect(false, "actual Coordinator Down", failures);
		return {};
	}

	void ExpectPhase(ContactInputCoordinator& input, ContactHandle handle,
		ContactPhase phase, const char* name, int& failures)
	{
		ContactSnapshot snapshot;
		Expect(handle && input.TryReadSnapshot(handle, snapshot) && snapshot.phase == phase,
			name, failures);
	}

	void TestLifecycleAndHandover(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		Mouse mouse;
		mouse.Start();
		Expect(mouse.Down(input, Mouse::Left, -320.0f, -120.0f, 10, false), "left Down", failures);
		const auto left = Take(input, failures);
		if (!left) return;
		const auto& down = left.record->DownSnapshot();
		Expect(left.record->DeviceType() == InputDeviceType::MouseLeft &&
			down.position.x == -320.0f && down.position.y == -120.0f && down.qpc == 10,
			"signed coordinates, QPC and device", failures);
		Expect(down.pressure < 0 && down.tilt < 0 && down.contactSize.width < 0 &&
			down.source.kind == SpeedEraser::SourceKind::Mouse &&
			down.source.recognition == SpeedEraser::SourceRecognition::Unknown,
			"compatibility input keeps unavailable capabilities unknown", failures);
		Expect(!mouse.Down(input, Mouse::Left, 0, 0, 11, false), "duplicate Down ignored", failures);
		mouse.Move(input, Mouse::Left, -321, -121, 12, false);
		ExpectPhase(input, left, ContactPhase::Move, "actual Move publication", failures);
		Expect(mouse.Down(input, Mouse::Right, 15, 16, 13, false), "right takes over", failures);
		ExpectPhase(input, left, ContactPhase::Up, "handover ends old stream", failures);
		const auto right = Take(input, failures);
		if (!right) return;
		Expect(right.record->DeviceType() == InputDeviceType::MouseRight,
			"right uses existing tool override identity", failures);
		mouse.Up(input, Mouse::Left, 20, 20, 14);
		Expect(mouse.Active(), "late old Up preserves new stream", failures);
		ExpectPhase(input, right, ContactPhase::Down, "old Up cannot touch right route", failures);
		mouse.Move(input, Mouse::Right, 30, 40, 15, false);
		mouse.Up(input, Mouse::Right, 31, 41, 16);
		mouse.Up(input, Mouse::Right, 31, 41, 17);
		mouse.Cancel(input, 18);
		ExpectPhase(input, right, ContactPhase::Up, "Up terminal is immutable", failures);
		input.Recycle(left);
		input.Recycle(right);
		const auto counts = input.DiagnosticsSnapshot();
		Expect(counts.downPublished == 2 && counts.terminalPublished == 2 &&
			counts.occupiedSlots == 0 && !mouse.Active(), "two unique streams fully recycled", failures);
	}

	void TestAdmissionAndCancel(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		Mouse mouse;
		// RTS 模式从不 Start 鼠标 producer，所有 WM_MOUSE 只能维护光标。
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 1, false), "RTS mode no second producer", failures);
		mouse.Move(input, Mouse::Left, 2, 3, 2, false);
		mouse.Up(input, Mouse::Left, 2, 3, 3);
		Expect(input.DiagnosticsSnapshot().downPublished == 0, "RTS mouse contact count zero", failures);
		mouse.Start(Mouse::Left);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 4, false), "startup held key quarantined", failures);
		mouse.Up(input, Mouse::Left, 1, 2, 5);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 6, true), "selection refuses Down", failures);
		mouse.Up(input, Mouse::Left, 1, 2, 7);
		input.SetAdmissionBlocked(true);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 8, false), "page boundary refuses Down", failures);
		input.SetAdmissionBlocked(false);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 9, false), "rejected held key cannot reenter", failures);
		mouse.Up(input, Mouse::Left, 1, 2, 10);
		Expect(mouse.Down(input, Mouse::Left, 1, 2, 11, false), "new physical Down admitted", failures);
		const auto first = Take(input, failures);
		if (!first) return;
		input.SetAdmissionBlocked(true);
		Expect(!input.ContactAdmitted(first), "actual Coordinator admission revision invalidated", failures);
		mouse.Move(input, Mouse::Left, 3, 4, 12, false);
		mouse.Cancel(input, 13);
		ExpectPhase(input, first, ContactPhase::Cancelled, "boundary cancellation, never synthetic Up", failures);
		input.Recycle(first);
		mouse.Up(input, Mouse::Left, 3, 4, 14);
		input.SetAdmissionBlocked(false);
		Expect(mouse.Down(input, Mouse::Right, 1, 2, 15, false), "right after boundary", failures);
		const auto second = Take(input, failures);
		mouse.Move(input, 0, 3, 4, 16, false);
		ExpectPhase(input, second, ContactPhase::Cancelled, "missing buttons cancel rather than Up", failures);
		mouse.Cancel(input, 17); // CaptureChanged / CancelMode / Hide 复用同一生产终态。
		mouse.Stop(input, 18);
		Expect(!mouse.Active() && !mouse.Open(), "cancel and stop idempotent", failures);
		Expect(input.DiagnosticsSnapshot().terminalPublished == 2, "one terminal per route", failures);
		input.Recycle(second);
	}

	void TestCaptureLossResynchronization(int& failures)
	{
		ContactInputCoordinator input;
		Mouse mouse;
		mouse.Start();
		Expect(mouse.Down(input, Mouse::Left, 1, 2, 1, false), "capture-loss initial Down", failures);
		const auto old = Take(input, failures);
		mouse.CaptureLost(input, 2);
		ExpectPhase(input, old, ContactPhase::Cancelled, "capture loss cancels old stream", failures);
		input.Recycle(old);
		// 用户在别窗释放（没有本窗 Up），回到画布的真实同键 Down 不能丢掉。
		Expect(mouse.Down(input, Mouse::Left, 3, 4, 3, false), "new same-button Down after external Up", failures);
		const auto next = Take(input, failures);
		mouse.Up(input, Mouse::Left, 3, 4, 4);
		input.Recycle(next);

		mouse.Start(Mouse::Left);
		mouse.CaptureLost(input, 5);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 6, false), "capture loss preserves startup quarantine", failures);
		mouse.Up(input, Mouse::Left, 1, 2, 7);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 8, true), "selection key quarantine begins", failures);
		mouse.CaptureLost(input, 9);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 10, false), "capture loss preserves selection quarantine", failures);
		mouse.Up(input, Mouse::Left, 1, 2, 11);
		Expect(mouse.Down(input, Mouse::Left, 1, 2, 12, false), "new Down after selection Up", failures);
		const auto boundary = Take(input, failures);
		input.SetAdmissionBlocked(true);
		mouse.Move(input, Mouse::Left, 2, 3, 13, false);
		mouse.CaptureLost(input, 14);
		input.SetAdmissionBlocked(false);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 15, false), "capture loss preserves page-boundary quarantine", failures);
		ExpectPhase(input, boundary, ContactPhase::Cancelled, "boundary stream stays cancelled", failures);
		input.Recycle(boundary);
		mouse.Up(input, Mouse::Left, 1, 2, 16);
		mouse.Stop(input, 17);
	}

	void TestStopOwnerInterleaving(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		Mouse mouse;
		mouse.Start();
		std::mutex mutex;
		std::condition_variable condition;
		bool ownerDownComplete = false, resumeOwner = false;
		std::thread owner([&]
		{
			(void)mouse.Down(input, Mouse::Left, 1, 2, 1, false);
			{
				std::unique_lock lock(mutex);
				ownerDownComplete = true;
				condition.notify_all();
				condition.wait(lock, [&] { return resumeOwner; });
			}
			// CloseAdmission 与 owner 消息交错：迟到 Up 必须取消，不能作为正常提交。
			mouse.Up(input, Mouse::Left, 4, 5, 2);
			(void)mouse.Down(input, Mouse::Right, 4, 5, 3, false);
			mouse.Stop(input, 4);
		});
		{
			std::unique_lock lock(mutex);
			condition.wait(lock, [&] { return ownerDownComplete; });
		}
		const auto contact = Take(input, failures);
		mouse.CloseAdmission();
		{
			std::scoped_lock lock(mutex);
			resumeOwner = true;
		}
		condition.notify_all();
		owner.join(); // 新一代必须在 owner 确认和旧 consumer 收尾之后 Reset。
		ExpectPhase(input, contact, ContactPhase::Cancelled, "stop wins late Up", failures);
		Expect(!mouse.Active() && input.DiagnosticsSnapshot().downPublished == 1 &&
			input.DiagnosticsSnapshot().terminalPublished == 1,
			"stop closes admission before owner cancellation", failures);
		input.Recycle(contact);
		input.ResetForNextRun();
		mouse.Start();
		mouse.Up(input, Mouse::Left, 9, 9, 5);
		Expect(!mouse.Active(), "old Up cannot create a next-generation stream", failures);
		Expect(mouse.Down(input, Mouse::Right, 10, 11, 6, false), "new generation starts clean", failures);
		const auto next = Take(input, failures);
		mouse.Stop(input, 7);
		ExpectPhase(input, next, ContactPhase::Cancelled, "owner Stop cancels active contact", failures);
		input.Recycle(next);
	}
}

int RunDraw3MouseInputTests()
{
	int failures = 0;
	TestLifecycleAndHandover(failures);
	TestAdmissionAndCancel(failures);
	TestCaptureLossResynchronization(failures);
	TestStopOwnerInterleaving(failures);
	if (!failures) std::cout << "PASS Draw3 mouse input lifecycle and stop interleaving\n";
	return failures;
}
