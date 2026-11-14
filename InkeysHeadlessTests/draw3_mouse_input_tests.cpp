#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.MouseInput.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"
#include <chrono>
#include <condition_variable>
#include <initializer_list>
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
		bool ownerWaitCompleted = false;
		std::thread owner([&]
		{
			(void)mouse.Down(input, Mouse::Left, 1, 2, 1, false);
			{
				std::unique_lock lock(mutex);
				ownerDownComplete = true;
				condition.notify_all();
				ownerWaitCompleted = condition.wait_for(lock, std::chrono::seconds(2),
					[&] { return resumeOwner; });
			}
			// CloseAdmission 与 owner 消息交错：迟到 Up 必须取消，不能作为正常提交。
			mouse.Up(input, Mouse::Left, 4, 5, 2);
			(void)mouse.Down(input, Mouse::Right, 4, 5, 3, false);
			mouse.Stop(input, 4);
		});
		{
			std::unique_lock lock(mutex);
			Expect(condition.wait_for(lock, std::chrono::seconds(2),
				[&] { return ownerDownComplete; }), "owner Down completes before timeout", failures);
		}
		const auto contact = Take(input, failures);
		mouse.CloseAdmission();
		{
			std::scoped_lock lock(mutex);
			resumeOwner = true;
		}
		condition.notify_all();
		owner.join(); // 新一代必须在 owner 确认和旧 consumer 收尾之后 Reset。
		Expect(ownerWaitCompleted, "owner cancellation resumes before timeout", failures);
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

	// 白板移动模式：兜底鼠标左键必须送达控制器作为平移触点，其他 Selection 场景仍隔离。
	void TestCanvasPanModeIsolation(int& failures)
	{
		Expect(Detail::MouseSelectionIsolates(true, false), "desktop/PPT Selection isolates", failures);
		Expect(!Detail::MouseSelectionIsolates(true, true), "whiteboard Move mode does not isolate", failures);
		Expect(!Detail::MouseSelectionIsolates(false, false) && !Detail::MouseSelectionIsolates(false, true),
			"drawing tools never isolate", failures);

		Detail::MouseSelectionModeState mode;
		mode.SetSelection(false);
		mode.SetWhiteboard(true); // 工作区确认先于准入重开；此时仍是绘制工具。
		Expect(!mode.CanvasPan() && !mode.Isolates(), "whiteboard drawing mode", failures);
		mode.SetSelection(true); // 不推进绘制帧，首个 Down 必须已经可以平移。
		Expect(mode.CanvasPan() && !mode.Isolates(), "selection synchronously enables pan", failures);
		mode.SetWhiteboard(false);
		Expect(!mode.CanvasPan() && mode.Isolates(), "confirmed desktop keeps Selection isolated", failures);
		mode.SetWhiteboard(true);
		Expect(mode.CanvasPan() && !mode.Isolates(), "workspace confirmation enables pan before admission", failures);
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		Mouse mouse;
		mouse.Start();
		Expect(mouse.Down(input, Mouse::Left, 10, 20, 1, mode.Isolates()),
			"Move mode admits left Down", failures);
		const auto pan = Take(input, failures);
		if (!pan) return;
		Expect(pan.record->DeviceType() == InputDeviceType::MouseLeft, "pan contact is MouseLeft", failures);
		mouse.Move(input, Mouse::Left, 14, 26, 2, Detail::MouseSelectionIsolates(true, true));
		ExpectPhase(input, pan, ContactPhase::Move, "Move mode keeps publishing Move", failures);
		// 工作区离开白板（Selection 不变、平移模式关闭）：活动流收为 Cancelled，并隔离该键。
		mouse.Move(input, Mouse::Left, 15, 27, 3, Detail::MouseSelectionIsolates(true, false));
		ExpectPhase(input, pan, ContactPhase::Cancelled, "leaving Move mode cancels the stream", failures);
		input.Recycle(pan);
		Expect(!mouse.Down(input, Mouse::Left, 1, 2, 4, Detail::MouseSelectionIsolates(true, false)),
			"desktop Selection refuses the same held key", failures);
		mouse.Up(input, Mouse::Left, 1, 2, 5);
		// 右键在移动模式也只是普通兜底接触（控制器负责丢弃），隔离只取决于模式位。
		Expect(mouse.Down(input, Mouse::Right, 3, 4, 6, Detail::MouseSelectionIsolates(true, true)),
			"Move mode still forwards right button for controller-side discard", failures);
		const auto right = Take(input, failures);
		if (right) { mouse.Up(input, Mouse::Right, 3, 4, 7); input.Recycle(right); }
		Expect(input.DiagnosticsSnapshot().occupiedSlots == 0, "pan mode streams fully recycled", failures);
	}

	// 模式发布后直接 Down，不借助绘制线程下一帧；旧终态未回收时也能开始下一次拖动。
	void TestCanvasPanImmediateDownAndRapidRestart(int& failures)
	{
		for (const bool workspaceFirst : { false, true })
		{
			Detail::MouseSelectionModeState mode;
			mode.SetSelection(false);
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			Mouse mouse;
			mouse.Start();
			if (workspaceFirst) mode.SetWhiteboard(true);
			mode.SetSelection(true);
			if (!workspaceFirst) mode.SetWhiteboard(true);
			Expect(mode.CanvasPan(), "both mode publication orders confirm Move", failures);
			ContactHandle previous{};
			for (int drag = 0; drag != 3; ++drag)
			{
				const std::int64_t qpc = 10 + drag * 4;
				Expect(mouse.Down(input, Mouse::Left, 17, 29, qpc, mode.Isolates()),
					"first or rapid Move Down admitted without another publication", failures);
				const auto current = Take(input, failures);
				if (!current) break;
				if (previous)
				{
					ExpectPhase(input, previous, ContactPhase::Up,
						"next Down preserves unconsumed previous Up", failures);
					input.Recycle(previous);
				}
				const auto& down = current.record->DownSnapshot();
				Expect(down.position.x == 17 && down.position.y == 29 && down.qpc == qpc,
					"immediate Down keeps client coordinates and time", failures);
				mouse.Move(input, Mouse::Left, -41, 700, qpc + 1, mode.Isolates());
				ContactSnapshot moved{};
				Expect(input.TryReadSnapshot(current, moved) && moved.phase == ContactPhase::Move
					&& moved.position.x == -41 && moved.position.y == 700 && moved.qpc == qpc + 1,
					"Move preserves signed outside-client coordinates", failures);
				mouse.Up(input, Mouse::Left, -43, 701, qpc + 2);
				ContactSnapshot ended{};
				Expect(input.TryReadSnapshot(current, ended) && ended.phase == ContactPhase::Up
					&& ended.position.x == -43 && ended.position.y == 701 && ended.qpc == qpc + 2,
					"rapid Up keeps terminal coordinates", failures);
				previous = current;
			}
			if (previous) input.Recycle(previous);
			const auto counts = input.DiagnosticsSnapshot();
			Expect(!mouse.Active() && counts.downPublished == 3 && counts.terminalPublished == 3
				&& counts.occupiedSlots == 0, "rapid Move drags each terminate exactly once", failures);
		}
	}

	void TestCanvasPanCancellationAndWorkspaceReset(int& failures)
	{
		for (const bool captureLost : { false, true })
		{
			Detail::MouseSelectionModeState mode;
			mode.SetWhiteboard(true);
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			Mouse mouse;
			mouse.Start();
			Expect(mouse.Down(input, Mouse::Left, 7, 9, 1, mode.Isolates()),
				"cancellation fixture Move Down", failures);
			const auto contact = Take(input, failures);
			if (!contact) continue;
			mouse.Move(input, Mouse::Left, -8, 400, 2, mode.Isolates());
			if (captureLost) mouse.CaptureLost(input, 3);
			else mouse.Cancel(input, 3);
			// 原生取消可能同步触发失捕获，随后还有迟到 Move/Up；终态只能发布一次。
			mouse.CaptureLost(input, 4);
			mouse.Cancel(input, 5);
			mouse.Move(input, Mouse::Left, 100, 200, 6, mode.Isolates());
			mouse.Up(input, Mouse::Left, 100, 200, 7);
			ContactSnapshot ended{};
			Expect(input.TryReadSnapshot(contact, ended) && ended.phase == ContactPhase::Cancelled
				&& ended.position.x == -8 && ended.position.y == 400 && ended.qpc == 3,
				"reentrant cancellation preserves first terminal and last accepted position", failures);
			Expect(!mouse.Active() && input.DiagnosticsSnapshot().terminalPublished == 1,
				"cancellation cascade leaves no active mouse owner", failures);
			input.Recycle(contact);
			Expect(mouse.Down(input, Mouse::Left, 11, 13, 8, mode.Isolates()),
				"fresh Down after cancellation is accepted", failures);
			const auto next = Take(input, failures);
			mode.ResetWorkspace();
			mouse.Move(input, Mouse::Left, 15, 17, 9, mode.Isolates());
			ExpectPhase(input, next, ContactPhase::Cancelled,
				"workspace reset cancels an active Move drag", failures);
			mouse.CaptureLost(input, 10);
			mode.SetWhiteboard(true);
			Expect(!mouse.Down(input, Mouse::Left, 15, 17, 11, mode.Isolates()),
				"workspace reset quarantine survives capture loss and mode reentry", failures);
			mouse.Up(input, Mouse::Left, 15, 17, 12);
			input.Recycle(next);
			const auto counts = input.DiagnosticsSnapshot();
			Expect(!mouse.Active() && counts.downPublished == 2 && counts.terminalPublished == 2
				&& counts.occupiedSlots == 0, "reset and capture-loss routes fully retired", failures);
		}
	}
	void TestCanvasPanWorkspaceRestart(int& failures)
	{
		Detail::MouseSelectionModeState mode;
		mode.SetWhiteboard(true);
		mode.SetSelection(true);
		Expect(mode.CanvasPan(), "restart fixture confirmed whiteboard Selection", failures);
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		Mouse mouse;
		mouse.Start();
		Expect(mouse.Down(input, Mouse::Left, 10, 20, 1, mode.Isolates()),
			"old whiteboard run admits Down", failures);
		const auto old = Take(input, failures);
		mouse.Stop(input, 2);
		ExpectPhase(input, old, ContactPhase::Cancelled, "restart cancels old mouse stream", failures);
		input.Recycle(old);
		// Stop/Detach 后同一 Host 以默认 Desktop 重启，不靠 SetWhiteboard(false) 回调清理旧门禁。
		mode.ResetWorkspace();
		mode.ResetWorkspace(); // Attach/Detach 的重复复位幂等。
		input.ResetForNextRun();
		mouse.Start();
		mode.SetSelection(true);
		const auto publishedBeforeRestartDown = input.DiagnosticsSnapshot().downPublished;
		Expect(!mode.CanvasPan() && mode.Isolates(), "Desktop restart clears stale pan gate", failures);
		Expect(!mouse.Down(input, Mouse::Left, 10, 20, 3, mode.Isolates()) &&
			input.DiagnosticsSnapshot().downPublished == publishedBeforeRestartDown && !mouse.Active(),
			"first Desktop Selection Down stays isolated without workspace callback", failures);
		mouse.Up(input, Mouse::Left, 10, 20, 4);
		mode.SetWhiteboard(true); // 新一代确认仍须立即放行首个 Down，不等待绘制帧。
		Expect(mouse.Down(input, Mouse::Left, 10, 20, 5, mode.Isolates()),
			"fresh workspace confirmation reopens pan synchronously", failures);
		const auto next = Take(input, failures);
		mouse.Up(input, Mouse::Left, 10, 20, 6);
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
	TestCanvasPanModeIsolation(failures);
	TestCanvasPanWorkspaceRestart(failures);
	TestCanvasPanImmediateDownAndRapidRestart(failures);
	TestCanvasPanCancellationAndWorkspaceReset(failures);
	if (!failures) std::cout << "PASS Draw3 mouse input lifecycle, stop interleaving and canvas pan mode\n";
	return failures;
}
