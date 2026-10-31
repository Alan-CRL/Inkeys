#define NOMINMAX
#include "../Inkeys/Inkeys/UI/Bar/Bar.InteractionWorker.h"

#include <array>
#include <iostream>
#include <string_view>
#include "../Inkeys/Inkeys/Helper/SecRandom.PipeIo.h"
#include "../Inkeys/Inkeys/UI/Ppt/Ppt.Recovery.h"
#include <thread>

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
	void TestCanvasRecoveryProjection()
	{
		using namespace Inkeys::Drawing::Draw3;
		using namespace Inkeys::UI;
		Bridge::ProductState desired;
		desired.workspace = Bridge::Workspace::Presentation;
		Bridge::PresentationTarget target;
		target.key.bytes[0] = 1; target.sessionRevision = 7; target.targetRevision = 9;
		target.sourceIdentity = "path:c:\\recovery.pptx"; target.totalPages = 2;
		desired.presentationTarget = std::make_shared<const Bridge::PresentationTarget>(target);
		CanvasRecoveryState fault;
		fault.status = CanvasRecoveryStatus::ProtectedConflict;
		fault.reason = CanvasRecoveryReason::TopologyConflict;
		fault.workspace = desired.workspace; fault.presentationTarget = Bridge::ReadyIdentityFor(target);
		fault.revision = 11; fault.retryAvailable = true;
		const auto view = PptRecovery::Project(fault, desired, 7, false, false);
		Check(view.kind == PptRecovery::Kind::Conflict && view.CanRetry(view), "current conflict has an exact retry token");
		Check(PptRecovery::Project(fault, desired, 7, true, false).kind == PptRecovery::Kind::None,
			"whiteboard transaction cannot publish PPT recovery interaction");
		Check(PptRecovery::Project(fault, desired, 8, false, false).kind == PptRecovery::Kind::None,
			"old presentation session cannot publish current fault");
		Check(!PptRecovery::Project(fault, desired, 7, false, true).CanRetry(view), "fatal consumer cannot be restarted by UI retry");
		auto newer = view; ++newer.revision;
		Check(!newer.CanRetry(view), "late retry click cannot claim a newer fault");
		++target.targetRevision;
		desired.presentationTarget = std::make_shared<const Bridge::PresentationTarget>(target);
		Check(PptRecovery::Project(fault, desired, 7, false, false).kind == PptRecovery::Kind::None,
			"new target retires old recovery UI");
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
	void TestPipeCancellation()
	{
		using namespace Inkeys::SecRandom::Detail;
		struct PipePair
		{
			HANDLE server = INVALID_HANDLE_VALUE, client = INVALID_HANDLE_VALUE;
			PipePair()
			{
				const auto name = L"\\\\.\\pipe\\Inkeys-SecRandom-test-" + std::to_wstring(GetCurrentProcessId())
					+ L"-" + std::to_wstring(GetTickCount64());
				server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
					PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 1000, nullptr);
				if (server == INVALID_HANDLE_VALUE) return;
				client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
					OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
				if (client != INVALID_HANDLE_VALUE) (void)ConnectNamedPipe(server, nullptr);
			}
			~PipePair()
			{
				if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
				if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
			}
			bool Write(std::string_view value)
			{
				DWORD written = 0;
				return WriteFile(server, value.data(), static_cast<DWORD>(value.size()), &written, nullptr)
					&& written == value.size();
			}
		} pipe;
		Check(pipe.client != INVALID_HANDLE_VALUE, "SecRandom no-window named-pipe fixture opens");
		if (pipe.client == INVALID_HANDLE_VALUE) return;
		const auto deadline = [] { return std::chrono::steady_clock::now() + std::chrono::seconds(2); };
		PipeCancellation idle({});
		Check(pipe.Write("reply\n"), "SecRandom fixture supplies response");
		auto read = TransferPipe(pipe.client, nullptr, 512, deadline(), idle);
		Check(read.error == ERROR_SUCCESS && std::string(read.data.begin(), read.data.end()) == "reply\n",
			"SecRandom production read copies completed owned buffer");
		auto write = TransferPipe(pipe.client, "request\n", 8, deadline(), idle);
		Check(write.error == ERROR_SUCCESS && write.transferred == 8, "SecRandom production overlapped write completes");
		if (write.error == ERROR_SUCCESS)
		{
			char received[8]{}; DWORD bytes = 0;
			Check(ReadFile(pipe.server, received, sizeof(received), &bytes, nullptr) && bytes == 8
				&& std::string_view(received, bytes) == "request\n", "SecRandom server observes complete owned request");
		}
		std::stop_source stop;
		PipeCancellation cancel(stop.get_token());
		std::jthread cancelSoon([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); stop.request_stop(); });
		const auto before = std::chrono::steady_clock::now();
		read = TransferPipe(pipe.client, nullptr, 512, deadline(), cancel);
		Check(read.error == ERROR_OPERATION_ABORTED && !read.quarantined
			&& std::chrono::steady_clock::now() - before < std::chrono::seconds(1),
			"SecRandom pending read observes stop and actual cancellation completion");
		Check(PipeOperationRegistry::Instance().Outstanding() == 0, "SecRandom completed cancellation releases all records");
		std::stop_source stopReaders;
		PipeCancellation cancelReaders(stopReaders.get_token());
		std::array<std::jthread, 4> readers;
		std::array<DWORD, 4> readerErrors{};
		for (size_t index = 0; index != readers.size(); ++index)
			readers[index] = std::jthread([&, index]
			{ readerErrors[index] = TransferPipe(pipe.client, nullptr, 512, deadline(), cancelReaders).error; });
		const auto capacityDeadline = deadline();
		while (PipeOperationRegistry::Instance().Outstanding() != readers.size()
			&& std::chrono::steady_clock::now() < capacityDeadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		const bool full = PipeOperationRegistry::Instance().Outstanding() == readers.size();
		const auto fifth = TransferPipe(pipe.client, nullptr, 512, deadline(), idle);
		stopReaders.request_stop();
		for (auto& reader : readers) reader.join();
		Check(full && fifth.error == ERROR_BUSY, "SecRandom registry caps all simultaneous operations at four");
		Check(std::all_of(readerErrors.begin(), readerErrors.end(), [](DWORD error) { return error == ERROR_OPERATION_ABORTED; })
			&& PipeOperationRegistry::Instance().Outstanding() == 0, "SecRandom exact cancellation drains all four concurrent reads");
		// 模拟驱动忽略取消：必须保留内核借用的地址，禁止继续累积请求；随后真正完成才能回收。
		PipeIoCancellationPolicy deferred;
		deferred.graceMs = 0;
		deferred.cancel = +[](HANDLE, LPOVERLAPPED) -> BOOL { return TRUE; };
		read = TransferPipe(pipe.client, nullptr, 512,
			std::chrono::steady_clock::now() + std::chrono::milliseconds(20), idle, deferred);
		Check(read.error == ERROR_TIMEOUT && read.quarantined
			&& PipeOperationRegistry::Instance().Outstanding() == 1, "SecRandom incomplete cancellation is retained in fixed registry");
		const auto blocked = TransferPipe(pipe.client, nullptr, 512, deadline(), idle);
		Check(blocked.error == ERROR_BUSY && PipeOperationRegistry::Instance().Outstanding() == 1,
			"SecRandom quarantined request blocks unbounded new operations");
		Check(pipe.Write("late"), "SecRandom delayed kernel operation is allowed to finish");
		const auto reapDeadline = deadline();
		while (PipeOperationRegistry::Instance().Outstanding() && std::chrono::steady_clock::now() < reapDeadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		Check(PipeOperationRegistry::Instance().Outstanding() == 0, "SecRandom quarantine reaps only after actual completion");
		Check(pipe.Write("again"), "SecRandom fixture provides post-quarantine response");
		read = TransferPipe(pipe.client, nullptr, 512, deadline(), idle);
		Check(read.error == ERROR_SUCCESS && read.transferred == 5,
			"SecRandom operations recover after quarantined completion");
	}

}

int RunUi3InputTests()
{
	TestCanvasRecoveryProjection();
	TestMailboxBoundaries(); TestPointerState(); TestContinuations(); TestPipeCancellation();
	return failures;
}
