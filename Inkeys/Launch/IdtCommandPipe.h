#pragma once

#include "IdtSingleInstance.h"
#include <sddl.h>
#include <tlhelp32.h>
#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <thread>

#pragma comment(lib, "advapi32.lib")

namespace Inkeys::Launch::CommandPipe
{
	inline constexpr wchar_t WhiteboardArgument[] = L"-Whiteboard";
	inline bool IsWhiteboardArgument(std::wstring_view value) noexcept { return value == WhiteboardArgument; }
	inline bool HasWhiteboardArgument()
	{
		int count = 0; LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!values) return false;
		bool found = false;
		for (int i = 1; i < count; ++i) found |= IsWhiteboardArgument(values[i]);
		LocalFree(values); return found;
	}

	struct Request
	{
		std::uint32_t magic = 0x57424B49, version = 1, command = 1, reserved = 0;
	};
	static_assert(sizeof(Request) == 16 && offsetof(Request, reserved) == 12);
	enum class Ack : unsigned char { Accepted = 1, Exiting = 2, Unsupported = 3 };
	enum class Result { Missing, Accepted, Failed };

	inline bool Valid(const Request& value) noexcept
	{
		return value.magic == Request{}.magic && value.version == 1 && value.command == 1 && !value.reserved;
	}
	inline std::vector<unsigned char> UserSid(HANDLE process)
	{
		HANDLE raw = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return {};
		Handle token(raw); DWORD needed = 0;
		(void)GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &needed);
		if (!needed || needed > 65536) return {};
		std::vector<unsigned char> bytes(needed);
		if (!GetTokenInformation(token.Get(), TokenUser, bytes.data(), needed, &needed)) return {};
		const auto sid = reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid;
		if (!IsValidSid(sid)) return {};
		const DWORD size = GetLengthSid(sid); std::vector<unsigned char> result(size);
		if (!CopySid(size, result.data(), sid)) return {};
		return result;
	}
	inline std::wstring Prefix(const std::wstring& root)
	{
		const auto identity = PresenceName(root); DWORD session = 0;
		if (identity.empty() || !ProcessIdToSessionId(GetCurrentProcessId(), &session)) return {};
		return L"\\\\.\\pipe\\Inkeys.Command." + std::to_wstring(session) + L"." + identity;
	}
	inline std::wstring Name(const std::wstring& root)
	{
		const auto prefix = Prefix(root);
		return prefix.empty() ? std::wstring{} : prefix + L"." + std::to_wstring(GetCurrentProcessId());
	}
	inline bool SameUser(const std::vector<unsigned char>& own, const std::vector<unsigned char>& peer)
	{ return !own.empty() && !peer.empty() && EqualSid(const_cast<unsigned char*>(own.data()), const_cast<unsigned char*>(peer.data())); }
	inline bool SamePeer(DWORD pid, const std::wstring& root)
	{
		Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
		if (!process.Get() || !WithinRoot(Image(process.Get()), root)) return false;
		DWORD ownSession = 0, peerSession = 0;
		if (!ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) || !ProcessIdToSessionId(pid, &peerSession)
			|| ownSession != peerSession) return false;
		auto own = UserSid(GetCurrentProcess()), peer = UserSid(process.Get());
		return SameUser(own, peer);
	}

	// 操作和缓冲同寿命；停止事件优先取消 I/O，不能让无响应客户端拖住关停。
	struct Operation
	{
		OVERLAPPED overlapped{};
		std::array<unsigned char, 33> bytes{};
		Operation() { overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
		~Operation() { if (overlapped.hEvent) CloseHandle(overlapped.hEvent); }
	};
	inline bool Complete(HANDLE pipe, std::unique_ptr<Operation>& operation, BOOL started,
		DWORD error, DWORD& transferred, HANDLE stop, DWORD timeout)
	{
		if (started) return GetOverlappedResult(pipe, &operation->overlapped, &transferred, FALSE) != FALSE;
		if (error != ERROR_IO_PENDING) return false;
		const HANDLE waits[]{ operation->overlapped.hEvent, stop };
		const DWORD waited = WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, timeout);
		if (waited == WAIT_OBJECT_0)
			return GetOverlappedResult(pipe, &operation->overlapped, &transferred, FALSE) != FALSE;
		(void)CancelIoEx(pipe, &operation->overlapped);
		if (WaitForSingleObject(operation->overlapped.hEvent, 1000) != WAIT_OBJECT_0)
		{
			// 极端内核取消失败也不无限等待；保留内核仍引用的缓冲，避免释放后写入。
			(void)operation.release();
		}
		return false;
	}
	inline bool Transfer(HANDLE pipe, void* buffer, DWORD size, bool write, HANDLE stop, DWORD timeout, DWORD& transferred)
	{
		if (size > 33) return false;
		auto operation = std::make_unique<Operation>();
		if (!operation->overlapped.hEvent) return false;
		if (write) std::memcpy(operation->bytes.data(), buffer, size);
		const BOOL started = write ? WriteFile(pipe, operation->bytes.data(), size, &transferred, &operation->overlapped)
			: ReadFile(pipe, operation->bytes.data(), size, &transferred, &operation->overlapped);
		const DWORD error = GetLastError();
		if (!Complete(pipe, operation, started, error, transferred, stop, timeout)) return false;
		if (!write && transferred <= size) std::memcpy(buffer, operation->bytes.data(), transferred);
		return transferred <= size;
	}

	inline std::wstring LifetimeName(const std::wstring& root)
	{
		const auto identity = PresenceName(root);
		return identity.empty() ? std::wstring{} : L"Local\\" + identity + L".CommandLifetime";
	}

	class Server
	{
		Handle stop;
		std::thread worker;
		static constexpr unsigned Exiting = 1, Pending = 2;
		std::atomic_uint flags = Exiting;
		std::wstring root;
		bool lifetimeReady = false;
		std::vector<std::pair<std::wstring, HANDLE>> lifetimes;
		bool RetainLifetime()
		{
			const auto name = LifetimeName(root); if (name.empty()) return false;
			for (const auto& registered : lifetimes) if (registered.first == name) return true;
			auto sid = UserSid(GetCurrentProcess()); LPWSTR sidText = nullptr;
			if (sid.empty() || !ConvertSidToStringSidW(sid.data(), &sidText)) return false;
			const auto text = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sidText) + L")";
			LocalFree(sidText); PSECURITY_DESCRIPTOR security = nullptr;
			if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &security, nullptr)) return false;
			SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
			// 此对象只保留已准入进程寿命，客户端只读 SYNCHRONIZE，无须写入较高完整性对象。
			Handle marker(CreateEventExW(&attributes, name.c_str(), CREATE_EVENT_MANUAL_RESET, SYNCHRONIZE));
			const DWORD error = GetLastError(); LocalFree(security); SetLastError(error);
			if (!marker.Get()) return false;
			try { lifetimes.emplace_back(name, marker.Get()); }
			catch (...) { return false; }
			(void)marker.Release(); return true;
		}
		Handle Create()
		{
			auto sid = UserSid(GetCurrentProcess()); LPWSTR sidText = nullptr;
			if (sid.empty() || !ConvertSidToStringSidW(sid.data(), &sidText)) return {};
			// 同用户与 SYSTEM 可访问；Medium 标签允许普通入口连接 UIAccess/提升后的实例。
			const auto descriptor = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sidText) + L")S:(ML;;NW;;;ME)";
			LocalFree(sidText); PSECURITY_DESCRIPTOR security = nullptr;
			if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1, &security, nullptr)) return {};
			SECURITY_ATTRIBUTES attributes{ sizeof(attributes), security, FALSE };
			const auto name = Name(root);
			const DWORD mode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
			HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				mode, 1, 64, 64, 0, &attributes);
			const DWORD error = GetLastError(); LocalFree(security); SetLastError(error); return Handle(pipe);
		}
		void Run(HANDLE raw)
		{
			using SetName = HRESULT(WINAPI*)(HANDLE, PCWSTR);
			auto setName = reinterpret_cast<SetName>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
			if (setName) (void)setName(GetCurrentThread(), L"Inkeys Whiteboard Command");
			Handle pipe(raw);
			while (!(flags.load(std::memory_order_acquire) & Exiting))
			{
				auto operation = std::make_unique<Operation>();
				if (!operation->overlapped.hEvent) break;
				DWORD transferred = 0;
				const BOOL connected = ConnectNamedPipe(pipe.Get(), &operation->overlapped);
				const DWORD error = GetLastError();
				if (!(error == ERROR_PIPE_CONNECTED || Complete(pipe.Get(), operation, connected, error, transferred, stop.Get(), INFINITE))) break;
				DWORD pid = 0; Request request{}; std::array<unsigned char, sizeof(Request) + 1> bytes{};
				if (GetNamedPipeClientProcessId(pipe.Get(), &pid) && SamePeer(pid, root)
					&& Transfer(pipe.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), false, stop.Get(), 1000, transferred)
					&& transferred == sizeof(Request))
				{
					std::memcpy(&request, bytes.data(), sizeof(request));
					if (Valid(request))
					{
						Ack ack = Ack::Exiting;
						if (TryQueue()) ack = Ack::Accepted;
						(void)Transfer(pipe.Get(), &ack, sizeof(ack), true, stop.Get(), 500, transferred);
						// 不做阻塞 Flush；等客户端读完回执并关闭后再断开，避免丢失未读回执。
						unsigned char closed = 0;
						(void)Transfer(pipe.Get(), &closed, 1, false, stop.Get(), 500, transferred);
					}
				}
				(void)DisconnectNamedPipe(pipe.Get());
			}
		}
	public:
		~Server() { Stop(); for (const auto& lifetime : lifetimes) CloseHandle(lifetime.second); }
		bool Start(const std::wstring& directory)
		{
			Stop(); root = directory;
			// 仅在准入后的 Start 建立；Stop/纯关闭释放原 presence 时仍持有，直到进程真正退场。
			lifetimeReady = RetainLifetime();
			if (!lifetimeReady) return false;
			flags.fetch_and(~Exiting, std::memory_order_acq_rel);
			stop.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!stop.Get() || Name(root).empty()) return false;
			auto pipe = Create(); if (!pipe.Get()) return false;
			try { worker = std::thread([this, raw = pipe.Get()] { Run(raw); }); }
			catch (...) { return false; }
			(void)pipe.Release(); return true;
		}
		bool LifetimeReady() const noexcept { return lifetimeReady; }
		void BeginExit() noexcept
		{
			flags.fetch_or(Exiting, std::memory_order_acq_rel);
			if (stop.Get()) SetEvent(stop.Get());
		}
		void Stop() noexcept { BeginExit(); if (worker.joinable()) worker.join(); }
		bool TakePending() noexcept { return (flags.fetch_and(~Pending, std::memory_order_acq_rel) & Pending) != 0; }
		bool TakeReadyPending() noexcept
		{
			unsigned before = flags.load(std::memory_order_acquire);
			while (!(before & Exiting) && (before & Pending))
				if (flags.compare_exchange_weak(before, before & ~Pending, std::memory_order_acq_rel)) return true;
			return false;
		}
		void Queue() noexcept { flags.fetch_or(Pending, std::memory_order_release); }
		bool TryQueue() noexcept
		{
			unsigned before = flags.load(std::memory_order_acquire);
			while (!(before & Exiting))
				if (flags.compare_exchange_weak(before, before | Pending, std::memory_order_acq_rel)) return true;
			return false;
		}
	};
	inline Server server;
	inline bool TakeHandoffIntent(bool& launchIntent)
	{
		server.Stop();
		const bool requested = server.TakePending() || launchIntent;
		launchIntent = false; return requested;
	}
	inline void DiscardIntent(bool& launchIntent) noexcept
	{ (void)server.TakePending(); launchIntent = false; }

	inline Result SendBytes(const std::wstring& root, void* request, DWORD size, DWORD retryMissing = 0)
	{
		const auto prefix = Prefix(root); if (prefix.empty()) return Result::Failed;
		const auto deadline = GetTickCount64() + (retryMissing ? retryMissing : 1500);
		Handle pipe; DWORD serverPid = 0;
		for (;;)
		{
			// 同名多实例的 ServerProcessId 可能固定为首创建者；每个 PID 独占端点，不能放松身份校验。
			Handle processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
			if (!processes.Get()) return Result::Failed;
			PROCESSENTRY32W process{sizeof(process)}; bool busy = false;
			if (!Process32FirstW(processes.Get(), &process)) return Result::Failed;
			do
			{
				if (GetTickCount64() >= deadline) return Result::Failed;
				const auto name = prefix + L"." + std::to_wstring(process.th32ProcessID);
				const HANDLE candidate = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
					FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
				if (candidate != INVALID_HANDLE_VALUE) { pipe.Reset(candidate); serverPid = process.th32ProcessID; break; }
				const DWORD error = GetLastError();
				if (error == ERROR_PIPE_BUSY) busy = true;
				else if (error != ERROR_FILE_NOT_FOUND) return Result::Failed;
			} while (Process32NextW(processes.Get(), &process));
			if (pipe.Get()) break;
			if (GetLastError() != ERROR_NO_MORE_FILES) return Result::Failed;
			if (!busy && !retryMissing) return Result::Missing;
			Sleep(20); // 枚举、候选连接、读写共用一个绝对截止，不按实例数累计等待。
		}
		DWORD actualPid = 0;
		if (!GetNamedPipeServerProcessId(pipe.Get(), &actualPid) || actualPid != serverPid || !SamePeer(actualPid, root)) return Result::Failed;
		auto remaining = [&]() -> DWORD { const auto now = GetTickCount64(); return now < deadline ? static_cast<DWORD>(deadline - now) : 0; };
		DWORD transferred = 0;
		if (!Transfer(pipe.Get(), request, size, true, nullptr, remaining(), transferred) || transferred != size) return Result::Failed;
		Ack ack = Ack::Unsupported;
		if (!Transfer(pipe.Get(), &ack, sizeof(ack), false, nullptr, remaining(), transferred)
			|| transferred != sizeof(ack)) return Result::Failed;
		return ack == Ack::Accepted ? Result::Accepted : Result::Failed;
	}

	inline Result Send(const std::wstring& root, DWORD retryMissing = 0)
	{ Request request; return SendBytes(root, &request, sizeof(request), retryMissing); }

	enum class OwnerLifetime { Missing, Present, Error };
	inline OwnerLifetime ProbeLifetime(const std::wstring& root)
	{
		const auto name = LifetimeName(root); if (name.empty()) return OwnerLifetime::Error;
		Handle marker(OpenEventW(SYNCHRONIZE, FALSE, name.c_str()));
		if (marker.Get()) return OwnerLifetime::Present;
		return GetLastError() == ERROR_FILE_NOT_FOUND ? OwnerLifetime::Missing : OwnerLifetime::Error;
	}

	enum class Admission { Accepted, Forwarded, ForwardFailed, Duplicate, Error };
	inline Admission Admit(const std::wstring& root, bool whiteboard, bool allow)
	{
		if (whiteboard)
		{
			const auto forwarded = Send(root);
			if (forwarded != Result::Missing) return forwarded == Result::Accepted ? Admission::Forwarded : Admission::ForwardFailed;
			if (!allow)
			{
				// 只认准入后建立的寿命标记；两个尚未准入的冷启动仍由原 Acquire 原子选出一个赢家。
				const auto peer = ProbeLifetime(root);
				if (peer == OwnerLifetime::Error) return Admission::ForwardFailed;
				if (peer == OwnerLifetime::Present)
					return Send(root, 3000) == Result::Accepted ? Admission::Forwarded : Admission::ForwardFailed;
			}
		}
		const auto admission = Launch::Acquire(root, allow);
		if (admission == Launch::Admission::Accepted) return Admission::Accepted;
		if (admission == Launch::Admission::Error) return Admission::Error;
		if (!whiteboard) return Admission::Duplicate;
		return Send(root, 3000) == Result::Accepted ? Admission::Forwarded : Admission::ForwardFailed;
	}

	inline std::wstring TestEvent(const std::wstring& root, const wchar_t* event, const std::wstring& index)
	{ return L"Local\\" + PresenceName(root) + L".CommandTest." + event + L"." + index; }
	inline bool IsSingleInstanceTestDriver()
	{
		int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
		const bool result = args && count == 2 && std::wstring_view(args[1]) == L"--single-instance-tests";
		if (args) LocalFree(args); return result;
	}
	inline int RunTests();
	inline bool TryRunTests(int& result)
	{
		int count = 0; auto raw = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!raw) return false;
		std::vector<std::wstring> args; for (int i = 0; i < count; ++i) args.emplace_back(raw[i]); LocalFree(raw);
		const auto root = Directory(Image());
		wchar_t replacementRoot[32768]{};
		const DWORD replacement = GetEnvironmentVariableW(L"INKEYS_TEST_WHITEBOARD_REPLACEMENT", replacementRoot, ARRAYSIZE(replacementRoot));
		if (replacement && replacement < ARRAYSIZE(replacementRoot) && Equal(replacementRoot, root) && PrivateTestRoot(root))
		{
			result = 1;
			if (HasWhiteboardArgument() && Adopt() == 1)
			{
				std::ofstream marker(std::filesystem::path(root) / L"replacement-intent.txt");
				marker << "whiteboard-once"; marker.close(); result = marker ? 0 : 1;
			}
			return true;
		}
		if (count < 2) return false;
		const auto mode = args[1];
		if (mode == L"--whiteboard-command-tests") { result = count == 2 ? RunTests() : 2; return true; }
		const bool listening = mode == L"--whiteboard-command-test-server", silent = mode == L"--whiteboard-command-test-silent",
			client = mode == L"--whiteboard-command-test-client", handoff = mode == L"--whiteboard-command-test-handoff",
			retry = mode == L"--whiteboard-command-test-retry", spoof = mode == L"--whiteboard-command-test-spoof",
			cold = mode == L"--whiteboard-command-test-cold", localNoPipe = mode == L"--whiteboard-command-test-local-no-pipe";
		if (!listening && !silent && !client && !handoff && !retry && !spoof && !cold && !localNoPipe) return false;
		result = 2;
		if (!PrivateTestRoot(root)) return true;
		if (localNoPipe)
		{
			if (count != 2 || Launch::Acquire(root, false) != Launch::Admission::Accepted) return true;
			Handle blocked(CreateNamedPipeW(Name(root).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64, 64, 0, nullptr));
			if (!blocked.Get()) return true;
			const bool pipeListening = server.Start(root);
			server.Queue(); const bool local = server.TakeReadyPending() && !server.TakeReadyPending();
			const bool external = Admit(root, true, false) == Admission::ForwardFailed;
			server.BeginExit(); server.Queue();
			result = !pipeListening && server.LifetimeReady() && local && external && !server.TakeReadyPending() ? 0 : 1;
			return true;
		}
		if (cold)
		{
			if (count != 2) return true;
			Handle begin(OpenEventW(SYNCHRONIZE, FALSE, TestEvent(root, L"ColdStart", L"cold").c_str()));
			Handle release(OpenEventW(SYNCHRONIZE, FALSE, TestEvent(root, L"ColdRelease", L"cold").c_str()));
			if (!begin.Get() || !release.Get() || WaitForSingleObject(begin.Get(), 10000) != WAIT_OBJECT_0) return true;
			const auto admission = Admit(root, true, false);
			const bool accepted = admission == Admission::Accepted;
			if (accepted && !server.Start(root)) return true;
			std::ofstream verdict(std::filesystem::path(root) / (L"cold-" + std::to_wstring(GetCurrentProcessId()) + L".txt"));
			verdict << static_cast<int>(admission); verdict.close();
			const bool released = WaitForSingleObject(release.Get(), 15000) == WAIT_OBJECT_0;
			server.Stop();
			result = released && (admission == Admission::Forwarded || (accepted && server.TakePending())) ? 0 : 1;
			return true;
		}
		if (handoff)
		{
			if (count != 2 || Launch::Acquire(root, false) != Launch::Admission::Accepted || !server.Start(root)) return true;
			// 真实管道接纳启动中意图，冻结后使用真实 Ready/父退出协议送到替代进程。
			const bool accepted = Send(root) == Result::Accepted;
			server.Stop(); const bool pending = server.TakePending();
			SetEnvironmentVariableW(L"INKEYS_TEST_WHITEBOARD_REPLACEMENT", root.c_str());
			result = accepted && pending && !server.TakePending()
				&& StartReplacement(Image(), WhiteboardArgument) ? 0 : 1;
			return true;
		}
		if (retry)
		{
			if (count != 2 || Launch::Acquire(root, false) != Launch::Admission::Accepted) return true;
			const bool missing = Send(root) == Result::Missing;
			const bool duplicate = Launch::Acquire(root, false) == Launch::Admission::Duplicate;
			Handle begin(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			std::thread listener([&] { (void)WaitForSingleObject(begin.Get(), 1000); (void)server.Start(root); });
			SetEvent(begin.Get());
			const auto sent = Send(root, 3000); listener.join();
			server.Stop(); result = missing && duplicate && sent == Result::Accepted && server.TakePending() ? 0 : 1;
			return true;
		}
		if (client)
		{
			if (count != 4 || !PrivateTestRoot(args[2])) return true;
			const auto& target = args[2]; const auto& variant = args[3];
			if (variant == L"deny" || variant == L"whiteboard-deny" || variant == L"whiteboard-allow")
			{
				const auto admission = Admit(target, variant != L"deny", variant == L"whiteboard-allow");
				result = admission == Admission::Forwarded ? 0 : admission == Admission::Duplicate ? 10 : admission == Admission::Accepted ? 20 : 11; return true;
			}
			Request request; std::array<unsigned char, 33> bytes{}; DWORD size = sizeof(request);
			if (variant == L"version") request.version = 2;
			else if (variant == L"magic") request.magic = 0;
			else if (variant == L"reserved") request.reserved = 1;
			else if (variant == L"command") request.command = 2;
			else if (variant == L"short") size = 1;
			else if (variant == L"oversize") size = static_cast<DWORD>(bytes.size());
			else if (variant != L"normal") return true;
			std::memcpy(bytes.data(), &request, sizeof(request));
			const auto sent = SendBytes(target, bytes.data(), size);
			result = sent == Result::Accepted ? 0 : sent == Result::Missing ? 12 : 11; return true;
		}
		if ((spoof && (count != 4 || !PrivateTestRoot(args[3]))) || (!spoof && count != 3)) return true;
		const auto& index = args[2];
		if (index != L"1" && index != L"2" && index != L"bad" && index != L"silent" && index != L"spoof") return true;
		Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, TestEvent(root, L"Ready", index).c_str()));
		Handle release(OpenEventW(SYNCHRONIZE, FALSE, TestEvent(root, L"Release", index).c_str()));
		Handle exit(OpenEventW(SYNCHRONIZE, FALSE, TestEvent(root, L"Exit", index).c_str()));
		Handle stopped(OpenEventW(EVENT_MODIFY_STATE, FALSE, TestEvent(root, L"Stopped", index).c_str()));
		if (!ready.Get() || !release.Get() || !exit.Get() || !stopped.Get()) return true;
		if (Launch::Acquire(root, true) != Launch::Admission::Accepted) return true;
		Handle silentPipe;
		if (silent)
		{
			silentPipe.Reset(CreateNamedPipeW(Name(root).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64, 64, 0, nullptr));
			if (!silentPipe.Get() || silentPipe.Get() == INVALID_HANDLE_VALUE) { silentPipe.Release(); return true; }
		}
		else if (!server.Start(spoof ? args[3] : root)) return true;
		SetEvent(ready.Get());
		const HANDLE waits[]{ release.Get(), exit.Get() };
		const auto wait = WaitForMultipleObjects(2, waits, FALSE, 120000);
		server.Stop(); silentPipe.Reset();
		if (wait == WAIT_OBJECT_0 + 1) ReleaseForClose(); // 真实复现纯关闭先释放占位、进程仍在清理的窗口。
		SetEvent(stopped.Get());
		if (wait == WAIT_OBJECT_0 + 1) (void)WaitForSingleObject(release.Get(), 5000);
		const bool once = server.TakePending(), twice = server.TakePending();
		std::ofstream marker(std::filesystem::path(root) / (L"pending-" + index + L".txt")); marker << once << " " << twice;
		result = wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1 ? 0 : 1; return true;
	}

	inline int RunTests()
	{
		wchar_t temporary[32768]{}; if (!GetTempPathW(ARRAYSIZE(temporary), temporary)) return 1;
		const auto root = std::wstring(temporary) + L"Inkeys Instance Test " + std::to_wstring(GetCurrentProcessId())
			+ L"-pipe-" + std::to_wstring(GetTickCount64());
		const auto other = root + L" other";
		if (!PrivateTestRoot(root) || !PrivateTestRoot(other)) return 1;
		struct Cleanup
		{
			std::wstring root, other;
			~Cleanup() { std::error_code error; if (PrivateTestRoot(root)) std::filesystem::remove_all(root, error);
				if (PrivateTestRoot(other)) std::filesystem::remove_all(other, error); }
		} cleanup{root, other};
		std::error_code error;
		if (!std::filesystem::create_directory(root, error) || !std::filesystem::create_directory(other, error)
			|| !std::filesystem::copy_file(Image(), root + L"\\Inkeys.exe", error)
			|| !std::filesystem::copy_file(Image(), other + L"\\Inkeys.exe", error)) return 1;
		auto start = [&](const std::wstring& directory, const std::wstring& arguments, Handle& process)
		{
			const auto image = directory + L"\\Inkeys.exe"; auto command = L"\"" + image + L"\" " + arguments;
			STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION created{};
			if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &created)) return false;
			process.Reset(created.hProcess); CloseHandle(created.hThread); return true;
		};
		auto wait = [&](Handle& process, DWORD expected)
		{
			if (!process.Get()) return false;
			if (WaitForSingleObject(process.Get(), 15000) != WAIT_OBJECT_0)
			{ TerminateProcess(process.Get(), ERROR_CANCELLED); (void)WaitForSingleObject(process.Get(), 1000); return false; }
			DWORD code = 0; const bool read = GetExitCodeProcess(process.Get(), &code) != FALSE;
			if (!read || code != expected) std::fprintf(stderr, "[WhiteboardCommandExit] actual=%lu expected=%lu read=%d\n", code, expected, read);
			return read && code == expected;
		};
		auto run = [&](const std::wstring& directory, const std::wstring& arguments, DWORD expected)
		{
			Handle process; const bool ok = start(directory, arguments, process) && wait(process, expected);
			std::fwprintf(stderr, L"[WhiteboardCommand] %ls expected=%lu ok=%d\n", arguments.c_str(), expected, ok); return ok;
		};
		auto send = [&](const std::wstring& directory, const std::wstring& target, const wchar_t* variant, DWORD expected)
		{ return run(directory, L"--whiteboard-command-test-client \"" + target + L"\" " + variant, expected); };
		struct Listener
		{
			Handle ready, release, exit, stopped, process;
			~Listener() { if (release.Get()) SetEvent(release.Get()); if (process.Get() && WaitForSingleObject(process.Get(), 2000) != WAIT_OBJECT_0)
				{ TerminateProcess(process.Get(), ERROR_CANCELLED); (void)WaitForSingleObject(process.Get(), 1000); } }
		};
		auto listen = [&](Listener& listener, const std::wstring& directory, const wchar_t* index, const wchar_t* mode, const std::wstring& extra = L"")
		{
			listener.ready.Reset(CreateEventW(nullptr, TRUE, FALSE, TestEvent(directory, L"Ready", index).c_str()));
			listener.release.Reset(CreateEventW(nullptr, TRUE, FALSE, TestEvent(directory, L"Release", index).c_str()));
			listener.exit.Reset(CreateEventW(nullptr, TRUE, FALSE, TestEvent(directory, L"Exit", index).c_str()));
			listener.stopped.Reset(CreateEventW(nullptr, TRUE, FALSE, TestEvent(directory, L"Stopped", index).c_str()));
			const bool ready = listener.ready.Get() && listener.release.Get() && listener.exit.Get() && listener.stopped.Get()
				&& start(directory, std::wstring(mode) + L" " + index + extra, listener.process)
				&& WaitForSingleObject(listener.ready.Get(), 5000) == WAIT_OBJECT_0;
			std::fwprintf(stderr, L"[WhiteboardCommandListen] %ls %ls ready=%d\n", mode, index, ready); return ready;
		};
		auto stop = [&](Listener& listener) { SetEvent(listener.release.Get()); return wait(listener.process, 0); };
		auto pending = [&](const wchar_t* index, bool expected)
		{
			int once = -1, twice = -1; std::ifstream marker(std::filesystem::path(root) / (std::wstring(L"pending-") + index + L".txt"));
			marker >> once >> twice; return once == static_cast<int>(expected) && twice == 0;
		};
		auto ownSid = UserSid(GetCurrentProcess()); std::vector<unsigned char> wrongSid(SECURITY_MAX_SID_SIZE); DWORD sidSize = static_cast<DWORD>(wrongSid.size());
		const bool wrongUser = CreateWellKnownSid(WinNullSid, nullptr, wrongSid.data(), &sidSize) && !SameUser(ownSid, wrongSid) && SameUser(ownSid, ownSid);
		server.Queue(); bool failedLaunch = true; DiscardIntent(failedLaunch);
		const bool discarded = !failedLaunch && !server.TakePending();
		server.Queue(); server.BeginExit();
		const bool retired = !server.TakeReadyPending() && !server.TryQueue(); (void)server.TakePending();
		bool ok = wrongUser && discarded && retired && IsWhiteboardArgument(L"-Whiteboard") && !IsWhiteboardArgument(L"-whiteboard")
			&& !IsWhiteboardArgument(L"-Whiteboard-extra") && !IsWhiteboardArgument(L" -Whiteboard");
		// 待更新哨兵不允许共享读取/写入；转发仍需在同一准入函数中成功，字节保持不动。
		{ std::ofstream sentinel(std::filesystem::path(root) / L"update.json"); sentinel << "whiteboard-update-sentinel"; }
		Handle sentinel(CreateFileW((root + L"\\update.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		ok = run(root, L"--whiteboard-command-test-local-no-pipe", 0) && ok;
		Handle coldStart(CreateEventW(nullptr, TRUE, FALSE, TestEvent(root, L"ColdStart", L"cold").c_str()));
		Handle coldRelease(CreateEventW(nullptr, TRUE, FALSE, TestEvent(root, L"ColdRelease", L"cold").c_str()));
		Handle coldProcesses[2];
		ok = coldStart.Get() && coldRelease.Get() && start(root, L"--whiteboard-command-test-cold", coldProcesses[0])
			&& start(root, L"--whiteboard-command-test-cold", coldProcesses[1]) && ok;
		SetEvent(coldStart.Get()); int coldVerdicts[2]{-1,-1};
		for (unsigned attempt = 0; attempt < 500 && (coldVerdicts[0] < 0 || coldVerdicts[1] < 0); ++attempt)
		{
			for (unsigned i = 0; i < 2; ++i)
			{ std::ifstream verdict(std::filesystem::path(root) / (L"cold-" + std::to_wstring(GetProcessId(coldProcesses[i].Get())) + L".txt")); verdict >> coldVerdicts[i]; }
			if (coldVerdicts[0] < 0 || coldVerdicts[1] < 0) Sleep(20);
		}
		const int accepted = static_cast<int>(Admission::Accepted), forwarded = static_cast<int>(Admission::Forwarded);
		ok = ((coldVerdicts[0] == accepted && coldVerdicts[1] == forwarded) || (coldVerdicts[1] == accepted && coldVerdicts[0] == forwarded)) && ok;
		SetEvent(coldRelease.Get()); ok = wait(coldProcesses[0], 0) && wait(coldProcesses[1], 0) && ok;
		std::fprintf(stderr, "[WhiteboardCommandCold] concurrent=%d/%d (one admitted, one forwarded)\n", coldVerdicts[0], coldVerdicts[1]);
		Listener first, second;
		ok = listen(first, root, L"1", L"--whiteboard-command-test-server") && ok;
		ok = send(root, root, L"deny", 10) && ok;
		ok = send(root, root, L"whiteboard-deny", 0) && ok;
		ok = send(root, root, L"whiteboard-allow", 0) && ok;
		ok = send(other, root, L"normal", 11) && ok;
		ok = send(root, other, L"normal", 12) && ok;
		ok = listen(second, root, L"2", L"--whiteboard-command-test-server") && ok;
		ok = stop(first) && pending(L"1", true) && ok;
		ok = send(root, root, L"normal", 0) && ok;
		SetEvent(second.exit.Get());
		ok = WaitForSingleObject(second.stopped.Get(), 3000) == WAIT_OBJECT_0 && ok;
		ok = send(root, root, L"whiteboard-deny", 11) && ok;
		ok = send(root, root, L"deny", 20) && ok;
		ok = send(root, root, L"whiteboard-allow", 20) && ok;
		ok = stop(second) && pending(L"2", true) && ok;
		Listener bad;
		ok = listen(bad, root, L"bad", L"--whiteboard-command-test-server") && ok;
		ok = send(root, root, L"normal", 0) && ok;
		ok = stop(bad) && pending(L"bad", true) && ok;
		bad.process.Reset(); bad.ready.Reset(); bad.release.Reset(); bad.exit.Reset(); bad.stopped.Reset();
		ok = listen(bad, root, L"bad", L"--whiteboard-command-test-server") && ok;
		for (const auto variant : {L"magic", L"version", L"command", L"reserved", L"short", L"oversize"})
			ok = send(root, root, variant, 11) && ok;
		ok = stop(bad) && pending(L"bad", false) && ok;
		Listener silent;
		ok = listen(silent, root, L"silent", L"--whiteboard-command-test-silent") && ok;
		const auto timeoutStart = GetTickCount64();
		ok = send(root, root, L"normal", 11) && GetTickCount64() - timeoutStart < 4000 && ok;
		ok = stop(silent) && ok;
		Listener spoof;
		ok = listen(spoof, other, L"spoof", L"--whiteboard-command-test-spoof", L" \"" + root + L"\"") && ok;
		ok = send(root, root, L"normal", 11) && ok;
		ok = stop(spoof) && ok;
		ok = run(root, L"--whiteboard-command-test-retry", 0) && ok;
		sentinel.Reset();
		{ std::ifstream read(std::filesystem::path(root) / L"update.json"); std::string text; std::getline(read, text);
			ok = text == "whiteboard-update-sentinel" && ok; }
		ok = !StartReplacement(root + L"\\Inkeys.exe", L"-Whiteboard extra") && ok;
		ok = run(root, L"--whiteboard-command-test-handoff", 0) && ok;
		std::string marker;
		for (unsigned i = 0; i < 150 && marker.empty(); ++i)
		{ std::ifstream read(std::filesystem::path(root) / L"replacement-intent.txt"); std::getline(read, marker); if (marker.empty()) Sleep(20); }
		ok = marker == "whiteboard-once" && ok;
		std::fprintf(stderr, "[WhiteboardCommand] fixed protocol, directory/user boundary, multi-listener, one-shot handoff: %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 1;
	}
}
