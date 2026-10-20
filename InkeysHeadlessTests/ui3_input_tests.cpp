#define NOMINMAX
#include "../Inkeys/Inkeys/UI/Bar/Bar.InteractionWorker.h"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
	using namespace Inkeys::UI::Bar;
	int failures = 0;
	void Check(bool value, std::string_view name)
	{
		if (!value) { ++failures; std::cerr << "FAIL " << name << '\n'; }
	}
	struct Input
	{
		unsigned surface = 0, contact = 0, serial = 0;
		ButtonPointerEvent kind = ButtonPointerEvent::Move;
	};
	bool Coalesce(const Input& previous, const Input& next)
	{
		return previous.kind == ButtonPointerEvent::Move && next.kind == ButtonPointerEvent::Move
			&& previous.surface == next.surface && previous.contact == next.contact;
	}
	void TestMailboxBoundaries()
	{
		InteractionMailbox<Input, 8> mailbox;
		Check(mailbox.Push({ 1, 5, 1, ButtonPointerEvent::Down }, Coalesce), "ui3 mailbox accepts Down");
		for (unsigned serial = 2; serial != 1002; ++serial)
			Check(mailbox.Push({ 1, 5, serial, ButtonPointerEvent::Move }, Coalesce), "ui3 consecutive Move coalesces");
		Check(mailbox.Push({ 2, 5, 1002, ButtonPointerEvent::Down }, Coalesce), "ui3 other surface preserves boundary");
		Check(mailbox.Push({ 1, 5, 1003, ButtonPointerEvent::Up }, Coalesce), "ui3 mailbox accepts Up");
		Input event;
		Check(mailbox.Pop(event) && event.serial == 1, "ui3 Down remains first");
		Check(mailbox.Pop(event) && event.serial == 1001, "ui3 last consecutive Move retained");
		Check(mailbox.Pop(event) && event.surface == 2 && event.serial == 1002, "ui3 targets remain ordered");
		Check(mailbox.Pop(event) && event.kind == ButtonPointerEvent::Up, "ui3 Up remains last");
		Check(!mailbox.Pop(event), "ui3 move storm stays bounded");
		for (unsigned index = 0; index != 8; ++index)
			Check(mailbox.Push({ index, 0, index, ButtonPointerEvent::Down }, Coalesce), "ui3 capacity admits exact bound");
		Check(!mailbox.Push({ 9, 0, 9, ButtonPointerEvent::Up }, Coalesce), "ui3 overflow reported");
		Check(mailbox.ConsumeOverflow() && !mailbox.ConsumeOverflow(), "ui3 overflow cancellation is consumed once");
		Check(!mailbox.Pop(event), "ui3 overflow discards complete in-flight gesture");
		const auto revision = mailbox.Revision(); mailbox.Wake();
		const auto before = std::chrono::steady_clock::now();
		mailbox.WaitUntil(before + std::chrono::seconds(1), revision);
		Check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100), "ui3 wake before wait is retained");
	}
	void TestPointerState()
	{
		std::array<ButtonPointerState, 5> states;
		Check(states[0].Update(ButtonPointerEvent::Down, 7).pressed == 7, "ui3 standard Down captures identity");
		Check(states[4].Update(ButtonPointerEvent::Down, 8).pressed == 8, "ui3 independent surface can press");
		Check(states[4].Update(ButtonPointerEvent::Up, 8).activate == 8, "ui3 independent surface can release");
		Check(states[0].Update(ButtonPointerEvent::Up, 7).activate == 7, "ui3 first surface retains its contact");
		states[1].Update(ButtonPointerEvent::Down, 9);
		states[1].Update(ButtonPointerEvent::Move, 10);
		Check(states[1].Update(ButtonPointerEvent::Up, 9).activate == 0, "ui3 drag-out cannot reactivate on Up");
		states[2].Update(ButtonPointerEvent::Down, 11);
		Check(states[2].Update(ButtonPointerEvent::Cancel, 11).pressed == 0, "ui3 Cancel releases pressed state");
		Check(states[2].Update(ButtonPointerEvent::Up, 11).activate == 0, "ui3 stale Up after Cancel has no action");
	}
	struct Continuation
	{
		std::coroutine_handle<> suspended;
		bool await_ready() const noexcept { return false; }
		void await_suspend(std::coroutine_handle<> handle) noexcept { suspended = handle; }
		void await_resume() const noexcept {}
		void Resume() { auto handle = std::exchange(suspended, {}); handle.resume(); }
	};
	InteractionTask<int> WaitForGesture(Continuation& wait, unsigned& frames)
	{
		struct Frame { unsigned& count; ~Frame() { ++count; } } frame{ frames };
		co_await wait;
		co_return 3;
	}
	InteractionTask<int> ParentGesture(Continuation& wait, unsigned& frames, int& result)
	{
		result = co_await WaitForGesture(wait, frames);
		co_return result + 1;
	}
	void TestContinuations()
	{
		Continuation wait; unsigned frames = 0; int result = 0;
		auto mainGesture = ParentGesture(wait, frames, result); mainGesture.Start();
		Check(!mainGesture.Done() && wait.suspended, "ui3 nested gesture yields to owner");
		ButtonPointerState paging;
		paging.Update(ButtonPointerEvent::Down, 20);
		Check(paging.Update(ButtonPointerEvent::Up, 20).activate == 20 && !mainGesture.Done(),
			"ui3 paging continues while main coroutine awaits");
		wait.Resume();
		Check(mainGesture.Done() && result == 3 && frames == 1, "ui3 nested continuation resumes exactly once");
		mainGesture.Rethrow();
		Continuation cancelledWait; int unused = 0;
		{ auto cancelled = ParentGesture(cancelledWait, frames, unused); cancelled.Start(); }
		Check(frames == 2, "ui3 shutdown destroys suspended child gesture frame");
	}
}

int RunUi3InputTests()
{
	TestMailboxBoundaries(); TestPointerState(); TestContinuations();
	return failures;
}
