import Inkeys.Helper.CrashHandler;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ShutdownSupervisor.h"

#include <shellapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <thread>
#include <utility>

#pragma comment(lib, "Shell32.lib")

// 现有 IdtMain 进程级退出意图槽；测试仅在已验真的 copied child 内模拟先 CAS、未 Arm 的窗口。
LONG* GetOffSignalInteropPointer();

namespace Inkeys::Shutdown
{
	namespace
	{
		constexpr wchar_t kSupervisorMode[] = L"--inkeys-internal-shutdown-supervisor-v1";
		constexpr wchar_t kTestMode[] = L"--shutdown-supervisor-tests";
		constexpr wchar_t kTestParentMode[] = L"--inkeys-internal-shutdown-test-parent";
		constexpr wchar_t kTestRestartedMode[] = L"--inkeys-internal-shutdown-test-restarted";
		constexpr wchar_t kRealUefTestChildMode[] = L"--inkeys-internal-real-uef-test-child";
		constexpr wchar_t kTestDirectoryPrefix[] = L"Inkeys Shutdown Test ";
		constexpr wchar_t kUefTestDirectoryPrefix[] = L"Inkeys Shutdown Test UEF ";
		constexpr DWORD kForcedExitCode = 0xE1430015;
		constexpr DWORD kFallbackForcedExitCode = 0xE1430016;
		constexpr DWORD kHandshakeWaitMilliseconds = 2500;
		constexpr DWORD kDeathWaitMilliseconds = 5000;
		std::atomic<int> g_armState{ 0 }; // 0 未请求，1 创建中，2 helper 已握手，3 全失败，4 仅自身兜底。
		std::atomic<ULONGLONG> g_fallbackDeadlineTick{ 0 };
		std::wstring g_authorizedUefTestDirectory; // 仅继承句柄验真的显式测试子进程写入。

		DWORD WINAPI FallbackDeadlineThread(void*) noexcept
		{
			// 此线程不进入 CRT/业务对象；即使外部 helper 创建调用自身卡住也保留原始截止。
			const ULONGLONG deadline = g_fallbackDeadlineTick.load(std::memory_order_acquire);
			for (;;)
			{
				const ULONGLONG now = GetTickCount64();
				if (now >= deadline) break;
				Sleep(static_cast<DWORD>(deadline - now));
			}
			TerminateProcess(GetCurrentProcess(), kFallbackForcedExitCode);
			return ERROR_GEN_FAILURE;
		}

		struct Handle
		{
			HANDLE value = nullptr;
			Handle() = default;
			explicit Handle(HANDLE handle) noexcept : value(handle) {}
			Handle(const Handle&) = delete;
			Handle& operator=(const Handle&) = delete;
			~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
			[[nodiscard]] bool Valid() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
		};

		struct AttributeList
		{
			void* memory = nullptr;
			bool initialized = false;
			AttributeList(const AttributeList&) = delete;
			AttributeList& operator=(const AttributeList&) = delete;
			AttributeList() = default;
			~AttributeList()
			{
				if (initialized) DeleteProcThreadAttributeList(static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory));
				if (memory) HeapFree(GetProcessHeap(), 0, memory);
			}
		};

		struct TestChildGuard
		{
			HANDLE process = nullptr;
			explicit TestChildGuard(HANDLE child) noexcept : process(child) {}
			~TestChildGuard()
			{
				if (process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
				{
					TerminateProcess(process, ERROR_OPERATION_ABORTED);
					WaitForSingleObject(process, kDeathWaitMilliseconds);
				}
			}
		};

		[[nodiscard]] bool EqualNoCase(const wchar_t* left, const wchar_t* right) noexcept
		{
			return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
		}

		[[nodiscard]] bool ParseUnsigned(const wchar_t* text, unsigned long long& value) noexcept
		{
			if (!text || !*text) return false;
			unsigned long long parsed = 0;
			for (const wchar_t* digit = text; *digit; ++digit)
			{
			if (*digit < L'0' || *digit > L'9') return false;
				const unsigned next = static_cast<unsigned>(*digit - L'0');
				if (parsed > (std::numeric_limits<unsigned long long>::max() - next) / 10) return false;
				parsed = parsed * 10 + next;
			}
			value = parsed;
			return true;
		}

		[[nodiscard]] bool CurrentImagePath(std::wstring& result)
		{
			for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity = capacity >= 16384 ? 32768 : capacity * 2)
			{
				std::wstring buffer(capacity, L'\0');
				const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
				if (length == 0) return false;
				if (length < capacity)
				{
					buffer.resize(length);
					result = std::move(buffer);
					const bool driveAbsolute = result.size() >= 3
						&& ((result[0] >= L'A' && result[0] <= L'Z')
							|| (result[0] >= L'a' && result[0] <= L'z'))
						&& result[1] == L':' && (result[2] == L'\\' || result[2] == L'/');
					const bool uncAbsolute = result.size() >= 3
						&& result[0] == L'\\' && result[1] == L'\\';
					if (driveAbsolute || uncAbsolute) return true;
					SetLastError(ERROR_BAD_PATHNAME);
					return false;
				}
				if (capacity == 32768) break;
			}
			SetLastError(ERROR_FILENAME_EXCED_RANGE);
			return false;
		}

		[[nodiscard]] bool ProcessImagePath(HANDLE process, std::wstring& result)
		{
			for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity = capacity >= 16384 ? 32768 : capacity * 2)
			{
				std::wstring buffer(capacity, L'\0');
				DWORD length = capacity;
				if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length))
				{
					buffer.resize(length);
					result = std::move(buffer);
					return !result.empty();
				}
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
				if (capacity == 32768) break;
			}
			SetLastError(ERROR_FILENAME_EXCED_RANGE);
			return false;
		}

		[[nodiscard]] bool SameExecutableFile(const std::wstring& left, const std::wstring& right) noexcept
		{
			Handle leftFile(CreateFileW(left.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			Handle rightFile(CreateFileW(right.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!leftFile.Valid() || !rightFile.Valid()) return false;
			BY_HANDLE_FILE_INFORMATION leftInfo{}, rightInfo{};
			return GetFileInformationByHandle(leftFile.value, &leftInfo)
				&& GetFileInformationByHandle(rightFile.value, &rightInfo)
				&& leftInfo.dwVolumeSerialNumber == rightInfo.dwVolumeSerialNumber
				&& leftInfo.nFileIndexHigh == rightInfo.nFileIndexHigh
				&& leftInfo.nFileIndexLow == rightInfo.nFileIndexLow;
		}

		[[nodiscard]] std::wstring Quote(const std::wstring& argument)
		{
			std::wstring quoted(1, L'"');
			size_t slashes = 0;
			for (const wchar_t character : argument)
			{
				if (character == L'\\') { ++slashes; continue; }
				if (character == L'"')
				{
					quoted.append(slashes * 2 + 1, L'\\');
					quoted += L'"';
				}
				else
				{
					quoted.append(slashes, L'\\');
					quoted += character;
				}
				slashes = 0;
			}
			quoted.append(slashes * 2, L'\\');
			quoted += L'"';
			return quoted;
		}

		[[nodiscard]] std::wstring ImageDirectory(const std::wstring& image)
		{
			const size_t separator = image.find_last_of(L"\\/");
			if (separator == std::wstring::npos) return {};
			return separator == 2 && image[1] == L':' ? image.substr(0, 3) : image.substr(0, separator);
		}

		[[nodiscard]] bool StartSameExecutable(const std::wstring& image,
			const std::wstring& arguments, bool inheritHandles, DWORD flags,
			STARTUPINFOEXW* extendedStartup, PROCESS_INFORMATION& process) noexcept
		{
			try
			{
				std::wstring command = Quote(image) + L" " + arguments;
				const std::wstring directory = ImageDirectory(image);
				if (directory.empty()) { SetLastError(ERROR_BAD_PATHNAME); return false; }
				STARTUPINFOW ordinary{};
				ordinary.cb = sizeof(ordinary);
				STARTUPINFOW* startup = extendedStartup ? &extendedStartup->StartupInfo : &ordinary;
				return CreateProcessW(image.c_str(), command.data(), nullptr, nullptr,
					inheritHandles ? TRUE : FALSE, flags, nullptr, directory.c_str(), startup, &process) != FALSE;
			}
			catch (...)
			{
				SetLastError(ERROR_NOT_ENOUGH_MEMORY);
				return false;
			}
		}

		[[nodiscard]] bool IsTestDirectory(const std::wstring& path)
		{
			std::wstring temp(32768, L'\0');
			const DWORD tempLength = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
			if (!tempLength || tempLength >= temp.size()) return false;
			temp.resize(tempLength);
			std::wstring full(32768, L'\0');
			const DWORD fullLength = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
			if (!fullLength || fullLength >= full.size()) return false;
			full.resize(fullLength);
			const DWORD attributes = GetFileAttributesW(full.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES
				|| !(attributes & FILE_ATTRIBUTE_DIRECTORY)
				|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
			if (full.size() > temp.size() && CompareStringOrdinal(full.c_str(),
				static_cast<int>(temp.size()), temp.c_str(), static_cast<int>(temp.size()), TRUE) == CSTR_EQUAL)
			{
				const std::wstring name = full.substr(temp.size());
				if (name.starts_with(kTestDirectoryPrefix)
					&& name.find_first_of(L"\\/") == std::wstring::npos) return true;
			}
			// 真实 UEF 测试仅接受本仓库忽略的 TestResults 唯一根。
			const std::wstring marker = L"\\TestResults\\release-hardening\\";
			const size_t markerAt = full.rfind(marker);
			if (markerAt == std::wstring::npos) return false;
			const std::wstring name = full.substr(markerAt + marker.size());
			if (!name.starts_with(kUefTestDirectoryPrefix)
				|| name.find_first_of(L"\\/") != std::wstring::npos) return false;
			const std::wstring repository = full.substr(0, markerAt);
			const DWORD solution = GetFileAttributesW((repository + L"\\InkeysRepo.sln").c_str());
			const DWORD trellis = GetFileAttributesW((repository + L"\\.trellis").c_str());
			return solution != INVALID_FILE_ATTRIBUTES && !(solution & FILE_ATTRIBUTE_DIRECTORY)
				&& trellis != INVALID_FILE_ATTRIBUTES && (trellis & FILE_ATTRIBUTE_DIRECTORY);
		}

		[[nodiscard]] ArmResult ArmCore(Intent intent, DWORD deadlineMilliseconds,
			const wchar_t* testDirectory, bool invalidPidForTest = false,
			bool simulateCreateFailureForTest = false,
			bool simulateEarlyExitForTest = false,
			bool simulateLateDeathForTest = false) noexcept
		{
			int expected = 0;
			if (!g_armState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
			{
				SetLastError(expected == 2 ? ERROR_ALREADY_EXISTS : ERROR_OPERATION_ABORTED);
				if (expected == 2) return ArmResult::AlreadyArmed;
				return expected == 4 ? ArmResult::FallbackArmed : ArmResult::Failed;
			}
			DWORD failure = ERROR_GEN_FAILURE;
			bool fallbackArmed = false;
			try
			{
				if (deadlineMilliseconds == 0 || deadlineMilliseconds > 60000
					|| (intent != Intent::Close && intent != Intent::Restart
						&& intent != Intent::CrashRestart)
					|| ((invalidPidForTest || simulateCreateFailureForTest
						|| simulateEarlyExitForTest || simulateLateDeathForTest) && !testDirectory)
					|| (testDirectory && !IsTestDirectory(testDirectory)))
				{
					failure = ERROR_INVALID_PARAMETER;
				}
				else
				{
					// 接受意图的时钟先于 CreateProcess 与握手，卡住的业务线程不参与计时。
					const ULONGLONG deadline = GetTickCount64() + deadlineMilliseconds;
					g_fallbackDeadlineTick.store(deadline, std::memory_order_release);
					Handle fallback(simulateLateDeathForTest ? nullptr
						: CreateThread(nullptr, 0, FallbackDeadlineThread, nullptr, 0, nullptr));
					fallbackArmed = fallback.Valid();
					std::wstring image;
					if (CurrentImagePath(image))
					{
						HANDLE inheritedParent = nullptr;
						const DWORD rights = SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION;
						if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
							&inheritedParent, rights, TRUE, 0))
						{
							Handle parent(inheritedParent);
							Handle acknowledge(CreateEventW(nullptr, TRUE, FALSE, nullptr));
							HANDLE inheritedAcknowledge = nullptr;
							if (acknowledge.Valid() && DuplicateHandle(GetCurrentProcess(), acknowledge.value,
								GetCurrentProcess(), &inheritedAcknowledge, EVENT_MODIFY_STATE,
								TRUE, 0))
							{
								Handle childAcknowledge(inheritedAcknowledge);
								SIZE_T attributeBytes = 0;
								InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
								AttributeList attributes;
								attributes.memory = HeapAlloc(GetProcessHeap(), 0, attributeBytes);
								if (attributes.memory)
								{
									STARTUPINFOEXW startup{};
									startup.StartupInfo.cb = sizeof(startup);
									startup.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.memory);
									if (InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes))
									{
										attributes.initialized = true;
										HANDLE inherited[] = { parent.value, childAcknowledge.value };
										if (UpdateProcThreadAttribute(startup.lpAttributeList, 0,
											PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
										{
											std::wstring arguments = std::wstring(kSupervisorMode)
												+ L" " + std::to_wstring(GetCurrentProcessId()
													+ static_cast<DWORD>(invalidPidForTest))
												+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(parent.value))
												+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childAcknowledge.value))
												+ L" " + std::to_wstring(deadline)
												+ (intent == Intent::CrashRestart ? L" X"
													: (intent == Intent::Restart ? L" R" : L" C"));
											if (testDirectory) arguments += L" " + Quote(testDirectory);
											if (simulateEarlyExitForTest) arguments += L" --test-ack-exit";
											if (simulateLateDeathForTest) arguments += L" --test-delayed-parent-exit";
											PROCESS_INFORMATION process{};
											if (!simulateCreateFailureForTest && StartSameExecutable(image, arguments, true,
												EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, &startup, process))
											{
												Handle childProcess(process.hProcess);
												Handle childThread(process.hThread);
												const HANDLE signals[] = { acknowledge.value, childProcess.value };
												const DWORD wait = WaitForMultipleObjects(2, signals, FALSE, kHandshakeWaitMilliseconds);
												if (wait == WAIT_OBJECT_0 && WaitForSingleObject(childProcess.value, 0) == WAIT_TIMEOUT)
												{
													g_armState.store(2, std::memory_order_release);
													SetLastError(ERROR_SUCCESS);
													return ArmResult::Armed;
												}
												failure = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_INVALID_HANDLE;
												// 未确认子进程握手时只终止本次刚创建的 helper，防止迟到守护。
												TerminateProcess(childProcess.value, ERROR_OPERATION_ABORTED);
												WaitForSingleObject(childProcess.value, kDeathWaitMilliseconds);
											}
											else failure = simulateCreateFailureForTest
												? ERROR_ACCESS_DENIED : GetLastError();
										}
										else failure = GetLastError();
									}
									else failure = GetLastError();
								}
								else failure = GetLastError();
							}
							else failure = GetLastError();
						}
						else failure = GetLastError();
					}
					else failure = GetLastError();
				}
			}
			catch (...)
			{
				failure = ERROR_NOT_ENOUGH_MEMORY;
			}
			g_armState.store(fallbackArmed ? 4 : 3, std::memory_order_release);
			SetLastError(failure);
			return fallbackArmed ? ArmResult::FallbackArmed : ArmResult::Failed;
		}

		[[nodiscard]] int RunSupervisorChild(int argc, wchar_t* const argv[])
		{
			if (argc != 7 && argc != 8 && argc != 9) return 21;
			unsigned long long pidValue = 0, parentValue = 0, acknowledgeValue = 0, deadlineValue = 0;
			if (!ParseUnsigned(argv[2], pidValue) || pidValue == 0 || pidValue > MAXDWORD
				|| !ParseUnsigned(argv[3], parentValue) || parentValue == 0
				|| parentValue > std::numeric_limits<uintptr_t>::max()
				|| !ParseUnsigned(argv[4], acknowledgeValue) || acknowledgeValue == 0
				|| acknowledgeValue > std::numeric_limits<uintptr_t>::max()
				|| !ParseUnsigned(argv[5], deadlineValue) || deadlineValue == 0
				|| (wcscmp(argv[6], L"C") != 0 && wcscmp(argv[6], L"R") != 0
					&& wcscmp(argv[6], L"X") != 0)) return 22;
			const std::wstring testDirectory = argc >= 8 ? argv[7] : L"";
			if (!testDirectory.empty() && !IsTestDirectory(testDirectory)) return 23;
			if (argc == 9 && (testDirectory.empty()
				|| (wcscmp(argv[8], L"--test-ack-exit") != 0
					&& wcscmp(argv[8], L"--test-delayed-parent-exit") != 0))) return 23;
			const bool delayedParentExitTest = argc == 9
				&& wcscmp(argv[8], L"--test-delayed-parent-exit") == 0;
			const ULONGLONG now = GetTickCount64();
			if (deadlineValue > now && deadlineValue - now > 60000) return 22;
			const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parentValue));
			const HANDLE acknowledge = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(acknowledgeValue));
			DWORD parentFlags = 0, acknowledgeFlags = 0;
			if (!GetHandleInformation(parent, &parentFlags) || !(parentFlags & HANDLE_FLAG_INHERIT)
				|| !GetHandleInformation(acknowledge, &acknowledgeFlags)
				|| !(acknowledgeFlags & HANDLE_FLAG_INHERIT)
				|| GetProcessId(parent) != static_cast<DWORD>(pidValue)
				|| static_cast<DWORD>(pidValue) == GetCurrentProcessId()) return 24;
			std::wstring ownImage, parentImage;
			if (!CurrentImagePath(ownImage) || !ProcessImagePath(parent, parentImage)
				|| !SameExecutableFile(ownImage, parentImage)) return 25;
			if (!SetEvent(acknowledge)) return 26;
			if (argc == 9 && !delayedParentExitTest)
				{ Sleep(500); return 31; } // 仅测试：握手后 supervisor 自己提前退出。
			const ULONGLONG waitStart = GetTickCount64();
			const DWORD waitMilliseconds = deadlineValue <= waitStart ? 0 :
				static_cast<DWORD>(deadlineValue - waitStart);
			DWORD wait = WaitForSingleObject(parent, waitMilliseconds);
			if (wait == WAIT_TIMEOUT)
			{
				if (WaitForSingleObject(parent, 0) != WAIT_OBJECT_0)
				{
					// 只对握手的精确父进程句柄强制退出；不按 PID 或进程名重新打开。
					const BOOL terminateRequested = delayedParentExitTest ? FALSE
						: TerminateProcess(parent, kForcedExitCode);
					// 自身兜底可能同时已发出终止、句柄尚未 signaled；失败也须等旧进程确实死亡。
					DWORD deathWait = WaitForSingleObject(parent, kDeathWaitMilliseconds);
					if (deathWait == WAIT_TIMEOUT)
					{
						OutputDebugStringW(L"ShutdownSupervisor: 旧进程终止超过5秒，继续等待同一句柄；不会提前拉起。\n");
						deathWait = WaitForSingleObject(parent, INFINITE);
					}
					if (deathWait != WAIT_OBJECT_0)
						return terminateRequested ? 28 : 27;
				}
			}
			else if (wait != WAIT_OBJECT_0) return 29;
			if (wcscmp(argv[6], L"C") == 0) return 0;
			// 新实例在旧进程真正终止后由此唯一 launcher 创建，不继承任何旧句柄。
			const bool crashRestart = wcscmp(argv[6], L"X") == 0;
			std::wstring arguments = crashRestart ? L"-CrashTry" : L"-Restart";
			if (!testDirectory.empty())
			{
				arguments = std::wstring(kTestRestartedMode) + L" " + Quote(testDirectory)
					+ L" " + std::to_wstring(static_cast<DWORD>(pidValue));
				if (crashRestart) arguments += L" -CrashTry";
			}
			PROCESS_INFORMATION restarted{};
			if (!StartSameExecutable(ownImage, arguments, false, 0, nullptr, restarted)) return 30;
			CloseHandle(restarted.hThread);
			CloseHandle(restarted.hProcess);
			return 0;
		}

		[[nodiscard]] std::wstring MarkerPath(const std::wstring& directory,
			const wchar_t* kind, DWORD pid)
		{
			return directory + L"\\" + kind + L"-" + std::to_wstring(pid) + L".txt";
		}

		[[nodiscard]] bool WriteMarker(const std::wstring& path,
			DWORD first, DWORD second, int third = -1) noexcept
		{
			Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!file.Valid()) return false;
			char text[64]{};
			const int length = third < 0
				? sprintf_s(text, "%lu %lu\n", first, second)
				: sprintf_s(text, "%lu %lu %d\n", first, second, third);
			DWORD written = 0;
			return length > 0 && WriteFile(file.value, text, static_cast<DWORD>(length),
				&written, nullptr) && written == static_cast<DWORD>(length);
		}

		[[nodiscard]] bool ReadMarker(const std::wstring& path,
			DWORD& first, DWORD& second, DWORD* third = nullptr) noexcept
		{
			Handle file(CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!file.Valid()) return false;
			char text[64]{};
			DWORD read = 0;
			if (!ReadFile(file.value, text, sizeof(text) - 1, &read, nullptr)) return false;
			return third
				? sscanf_s(text, "%lu %lu %lu", &first, &second, third) == 3
				: sscanf_s(text, "%lu %lu", &first, &second) == 2;
		}

		[[nodiscard]] int RunTestParent(int argc, wchar_t* const argv[])
		{
			if (argc != 4 || !IsTestDirectory(argv[3])) return 41;
			const wchar_t* scenario = argv[2];
			if (EqualNoCase(scenario, L"manual-pending-close"))
			{
				LONG intentSlot = 0;
				(void)TryClaimCrashRestartIntent(&intentSlot, 0, false);
				std::atomic<DWORD> closeResult{ static_cast<DWORD>(ArmResult::Failed) };
				std::thread closer([&]()
				{
					// 与正式 Close 一样先 CAS 0→1，只有获胜者能启动同一生产 ArmCore。
					if (InterlockedCompareExchange(&intentSlot, 1, 0) == 0)
						closeResult.store(static_cast<DWORD>(ArmCore(Intent::Close, 6000, argv[3])),
							std::memory_order_release);
				});
				closer.join();
				const bool lateCrashClaim = TryClaimCrashRestartIntent(&intentSlot, 0, true);
				if (!WriteMarker(MarkerPath(argv[3], L"parent", GetCurrentProcessId()),
					closeResult.load(std::memory_order_acquire), lateCrashClaim ? 1u : 0u)) return 43;
				Sleep(INFINITE);
				return 45;
			}
			Intent intent = Intent::Close;
			DWORD deadline = 15000;
			bool natural = false;
			bool duplicate = false;
			if (EqualNoCase(scenario, L"close-natural")) natural = true;
			else if (EqualNoCase(scenario, L"restart-natural")) { intent = Intent::Restart; natural = true; }
			else if (EqualNoCase(scenario, L"crash-natural")) { intent = Intent::CrashRestart; natural = true; }
			else if (EqualNoCase(scenario, L"close-hung15")) {}
			else if (EqualNoCase(scenario, L"restart-hung-short")) { intent = Intent::Restart; deadline = 3000; }
			else if (EqualNoCase(scenario, L"crash-hung-short")) { intent = Intent::CrashRestart; deadline = 3000; }
			else if (EqualNoCase(scenario, L"duplicate")) { deadline = 3000; duplicate = true; }
			else if (EqualNoCase(scenario, L"bad-pid")) { deadline = 3000; }
			else if (EqualNoCase(scenario, L"bad-pid-hung")) { deadline = 6000; }
			else if (EqualNoCase(scenario, L"create-fail-close")) { deadline = 3000; }
			else if (EqualNoCase(scenario, L"create-fail-restart")) { intent = Intent::Restart; deadline = 3000; }
			else if (EqualNoCase(scenario, L"helper-early-exit")) { intent = Intent::Restart; deadline = 3000; }
			else if (EqualNoCase(scenario, L"late-death-restart")) { intent = Intent::Restart; deadline = 3000; }
			else return 42;
			const bool badPid = EqualNoCase(scenario, L"bad-pid")
				|| EqualNoCase(scenario, L"bad-pid-hung");
			const bool createFailure = EqualNoCase(scenario, L"create-fail-close")
				|| EqualNoCase(scenario, L"create-fail-restart");
			const bool earlyExit = EqualNoCase(scenario, L"helper-early-exit");
			const bool lateDeath = EqualNoCase(scenario, L"late-death-restart");
			const ArmResult first = ArmCore(intent, deadline, argv[3], badPid,
				createFailure, earlyExit, lateDeath);
			const ArmResult second = duplicate ? ArmCore(Intent::Restart, deadline, argv[3]) : ArmResult::Armed;
			if (!WriteMarker(MarkerPath(argv[3], L"parent", GetCurrentProcessId()),
				static_cast<DWORD>(first), static_cast<DWORD>(second))) return 43;
			if (EqualNoCase(scenario, L"bad-pid"))
				return first == ArmResult::FallbackArmed ? 88 : 46;
			if (lateDeath)
			{
				if (first != ArmResult::Armed) return 46;
				Sleep(9500); // 仅测试：deadline 后再过 6 秒以上才让旧进程自然 signaled。
				return 77;
			}
			if (badPid || createFailure || earlyExit)
			{
				Sleep(INFINITE); // 失败路径只能由本进程 deadline 兜底结束。
				return 45;
			}
			if (first != ArmResult::Armed || second != (duplicate ? ArmResult::AlreadyArmed : ArmResult::Armed))
				return 44;
			if (natural) { Sleep(100); return 77; }
			Sleep(INFINITE); // 只在本次测试刚创建的进程内等待，交给精确 HANDLE 超时处理。
			return 45;
		}

		[[nodiscard]] int RunTestRestarted(int argc, wchar_t* const argv[])
		{
			unsigned long long oldPid = 0;
			if ((argc != 4 && argc != 5) || !IsTestDirectory(argv[2])
				|| !ParseUnsigned(argv[3], oldPid) || oldPid == 0 || oldPid > MAXDWORD)
				return 51;
			// 测试子进程把 helper 真正选出的启动参数写入 marker。
			const int argumentKind = argc == 4 ? 1
				: (wcscmp(argv[4], L"-CrashTry") == 0 ? 2 : 0);
			if (!argumentKind) return 53;
			return WriteMarker(MarkerPath(argv[2], L"restart", GetCurrentProcessId()),
				GetCurrentProcessId(), static_cast<DWORD>(oldPid), argumentKind) ? 0 : 52;
		}

		void PrintTestResult(const char* name, bool passed) noexcept
		{
			char text[160]{};
			const int length = sprintf_s(text, "shutdown-supervisor %s %s\n",
				name, passed ? "PASS" : "FAIL");
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			DWORD written = 0;
			if (length > 0 && output && output != INVALID_HANDLE_VALUE)
				WriteFile(output, text, static_cast<DWORD>(length), &written, nullptr);
		}

		void PrintCaseObservation(DWORD oldPid, DWORD exitCode,
			ULONGLONG elapsedMilliseconds, DWORD restartCount) noexcept
		{
			char text[192]{};
			const int length = sprintf_s(text,
				"shutdown-supervisor old_pid=%lu exit_code=%lu elapsed_ms=%llu restart_count=%lu\n",
				oldPid, exitCode, elapsedMilliseconds, restartCount);
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			DWORD written = 0;
			if (length > 0 && output && output != INVALID_HANDLE_VALUE)
				WriteFile(output, text, static_cast<DWORD>(length), &written, nullptr);
		}

		[[nodiscard]] bool MakeTestDirectory(const wchar_t* scenario, std::wstring& directory)
		{
			std::wstring temp(32768, L'\0');
			const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
			if (!length || length >= temp.size()) return false;
			temp.resize(length);
			for (unsigned attempt = 0; attempt != 16; ++attempt)
			{
				directory = temp + kTestDirectoryPrefix + L"测试 "
					+ std::to_wstring(GetCurrentProcessId()) + L" "
					+ std::to_wstring(GetTickCount64()) + L" " + scenario
					+ L" " + std::to_wstring(attempt);
				if (CreateDirectoryW(directory.c_str(), nullptr)) return true;
				if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
			}
			return false;
		}

		[[nodiscard]] DWORD CountRestartMarkers(const std::wstring& directory,
			DWORD expectedOldPid, DWORD expectedArgumentKind, bool& identityCorrect)
		{
			WIN32_FIND_DATAW found{};
			HANDLE search = FindFirstFileW((directory + L"\\restart-*.txt").c_str(), &found);
			if (search == INVALID_HANDLE_VALUE) return 0;
			DWORD count = 0;
			do
			{
				if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				DWORD newPid = 0, oldPid = 0, argumentKind = 0;
				if (!ReadMarker(directory + L"\\" + found.cFileName,
					newPid, oldPid, &argumentKind))
					continue; // 创建者尚未写完时等待下一次轮询。
				if (newPid == expectedOldPid || oldPid != expectedOldPid
					|| argumentKind != expectedArgumentKind)
					identityCorrect = false;
				++count;
			} while (FindNextFileW(search, &found));
			FindClose(search);
			return count;
		}

		void RemoveTestDirectory(const std::wstring& directory, DWORD parentPid)
		{
			if (!IsTestDirectory(directory)) return;
			DeleteFileW(MarkerPath(directory, L"parent", parentPid).c_str());
			WIN32_FIND_DATAW found{};
			HANDLE search = FindFirstFileW((directory + L"\\restart-*.txt").c_str(), &found);
			if (search != INVALID_HANDLE_VALUE)
			{
				do
				{
					if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						DeleteFileW((directory + L"\\" + found.cFileName).c_str());
				} while (FindNextFileW(search, &found));
				FindClose(search);
			}
			RemoveDirectoryW(directory.c_str());
		}

		[[nodiscard]] bool RunProcessCase(const std::wstring& image, const wchar_t* scenario,
			DWORD deadline, bool natural, bool restart)
		{
			std::wstring directory;
			if (!MakeTestDirectory(scenario, directory)) return false;
			const std::wstring arguments = std::wstring(kTestParentMode) + L" " + scenario
				+ L" " + Quote(directory);
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(image, arguments, false, CREATE_NO_WINDOW, nullptr, process))
			{
				RemoveTestDirectory(directory, 0);
				return false;
			}
			Handle child(process.hProcess);
			Handle childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			const std::wstring parentMarker = MarkerPath(directory, L"parent", process.dwProcessId);
			DWORD first = MAXDWORD, second = MAXDWORD;
			const ULONGLONG markerWaitStart = GetTickCount64();
			while (!ReadMarker(parentMarker, first, second)
				&& GetTickCount64() - markerWaitStart < 5000
				&& WaitForSingleObject(child.value, 0) == WAIT_TIMEOUT)
				Sleep(20);
			const bool badPid = EqualNoCase(scenario, L"bad-pid");
			const bool fallbackArm = badPid || EqualNoCase(scenario, L"bad-pid-hung")
				|| EqualNoCase(scenario, L"create-fail-close")
				|| EqualNoCase(scenario, L"create-fail-restart");
			const bool fallbackExit = EqualNoCase(scenario, L"bad-pid-hung")
				|| EqualNoCase(scenario, L"create-fail-close")
				|| EqualNoCase(scenario, L"create-fail-restart")
				|| EqualNoCase(scenario, L"helper-early-exit");
			bool passed = first == static_cast<DWORD>(fallbackArm ? ArmResult::FallbackArmed : ArmResult::Armed)
				&& second == static_cast<DWORD>(EqualNoCase(scenario, L"duplicate")
					? ArmResult::AlreadyArmed : ArmResult::Armed);
			const ULONGLONG armedObserved = GetTickCount64();
			const DWORD wait = WaitForSingleObject(child.value,
				deadline + (EqualNoCase(scenario, L"late-death-restart") ? 10000 : 5000));
			if (wait != WAIT_OBJECT_0)
			{
				// 测试失败只清理这个测试创建并仍持有的 child HANDLE。
				TerminateProcess(child.value, ERROR_OPERATION_ABORTED);
				WaitForSingleObject(child.value, kDeathWaitMilliseconds);
				passed = false;
			}
			DWORD exitCode = 0;
			const BOOL hasExitCode = GetExitCodeProcess(child.value, &exitCode);
			const bool expectedExit = badPid ? exitCode == 88u
				: natural ? exitCode == 77u
				: fallbackExit ? exitCode == kFallbackForcedExitCode
				: (exitCode == kForcedExitCode || exitCode == kFallbackForcedExitCode);
			passed = passed && hasExitCode && expectedExit;
			const ULONGLONG elapsed = GetTickCount64() - armedObserved;
			if (!natural && deadline == 15000)
				passed = passed && elapsed >= 14000 && elapsed <= 21000;
			bool identityCorrect = true;
			DWORD restartCount = 0;
			const DWORD expectedArgumentKind = EqualNoCase(scenario, L"crash-natural")
				|| EqualNoCase(scenario, L"crash-hung-short") ? 2 : 1;
			if (restart)
			{
				const ULONGLONG restartWaitStart = GetTickCount64();
				while ((restartCount = CountRestartMarkers(directory, process.dwProcessId,
					expectedArgumentKind, identityCorrect)) == 0
					&& GetTickCount64() - restartWaitStart < 5000)
					Sleep(20);
				Sleep(250);
				restartCount = CountRestartMarkers(directory, process.dwProcessId,
					expectedArgumentKind, identityCorrect);
				passed = passed && identityCorrect
					&& restartCount == 1
					&& identityCorrect;
			}
			else
			{
				Sleep(250);
				restartCount = CountRestartMarkers(directory, process.dwProcessId,
					expectedArgumentKind, identityCorrect);
				passed = passed && restartCount == 0;
			}
			PrintCaseObservation(process.dwProcessId, exitCode, elapsed, restartCount);
			RemoveTestDirectory(directory, process.dwProcessId);
			return passed;
		}

		[[nodiscard]] bool RunMalformedHandshakeCase(const std::wstring& image)
		{
			const std::wstring arguments = std::wstring(kSupervisorMode)
				+ L" " + std::to_wstring(GetCurrentProcessId())
				+ L" 4 8 " + std::to_wstring(GetTickCount64() + 15000) + L" C";
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(image, arguments, false, CREATE_NO_WINDOW, nullptr, process)) return false;
			Handle child(process.hProcess);
			Handle childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			if (WaitForSingleObject(child.value, 3000) != WAIT_OBJECT_0)
			{
				TerminateProcess(child.value, ERROR_OPERATION_ABORTED);
				WaitForSingleObject(child.value, kDeathWaitMilliseconds);
				return false;
			}
			DWORD exitCode = 0;
			return GetExitCodeProcess(child.value, &exitCode) && exitCode == 24;
		}

		[[nodiscard]] bool RunQuotedUnicodePathCase()
		{
			const std::wstring image = L"C:\\有 空格\\Inkeys.exe";
			const std::wstring directory = L"C:\\临时 文件夹\\末尾\\";
			const std::wstring command = Quote(image) + L" " + Quote(directory);
			int argc = 0;
			LPWSTR* argv = CommandLineToArgvW(command.c_str(), &argc);
			if (!argv) return false;
			const bool valid = argc == 2 && wcscmp(argv[0], image.c_str()) == 0
				&& wcscmp(argv[1], directory.c_str()) == 0;
			LocalFree(argv);
			return valid;
		}

		[[nodiscard]] bool MakeUefTestDirectory(std::wstring& directory,
			std::wstring& copiedImage)
		{
			std::wstring repository(32768, L'\0');
			const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(repository.size()), repository.data());
			if (!length || length >= repository.size()) return false;
			repository.resize(length);
			const DWORD solution = GetFileAttributesW((repository + L"\\InkeysRepo.sln").c_str());
			const DWORD trellis = GetFileAttributesW((repository + L"\\.trellis").c_str());
			if (solution == INVALID_FILE_ATTRIBUTES || (solution & FILE_ATTRIBUTE_DIRECTORY)
				|| trellis == INVALID_FILE_ATTRIBUTES || !(trellis & FILE_ATTRIBUTE_DIRECTORY))
				return false;
			const std::wstring results = repository + L"\\TestResults";
			const std::wstring evidence = results + L"\\release-hardening";
			for (const std::wstring& level : { results, evidence })
			{
				if (!CreateDirectoryW(level.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
					return false;
				const DWORD attributes = GetFileAttributesW(level.c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
					|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
			}
			for (unsigned attempt = 0; attempt != 16; ++attempt)
			{
				directory = evidence + L"\\" + kUefTestDirectoryPrefix + L"测试 "
					+ std::to_wstring(GetCurrentProcessId()) + L" "
					+ std::to_wstring(GetTickCount64()) + L" " + std::to_wstring(attempt);
				if (CreateDirectoryW(directory.c_str(), nullptr)) break;
				if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
				if (attempt == 15) return false;
			}
			const std::wstring binaryDirectory = directory + L"\\bin";
			if (!CreateDirectoryW(binaryDirectory.c_str(), nullptr)) return false;
			std::wstring sourceImage;
			if (!CurrentImagePath(sourceImage)) return false;
			copiedImage = binaryDirectory + L"\\Inkeys.exe";
			return CopyFileW(sourceImage.c_str(), copiedImage.c_str(), TRUE) != FALSE;
		}

		[[nodiscard]] bool FindCrashArtifact(const std::wstring& directory,
			DWORD pid, const wchar_t* extension, std::wstring& path,
			ULONGLONG& bytes)
		{
			const std::wstring pattern = directory + L"\\*_" + std::to_wstring(pid) + extension;
			WIN32_FIND_DATAW found{};
			HANDLE search = FindFirstFileW(pattern.c_str(), &found);
			if (search == INVALID_HANDLE_VALUE) return false;
			const bool present = !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
			if (present)
			{
				path = directory + L"\\" + found.cFileName;
				bytes = (static_cast<ULONGLONG>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
			}
			FindClose(search);
			return present && bytes > 0;
		}

		[[nodiscard]] bool ValidateDumpFile(const std::wstring& path)
		{
			Handle file(CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!file.Valid()) return false;
			char signature[4]{};
			DWORD read = 0;
			return ReadFile(file.value, signature, sizeof(signature), &read, nullptr)
				&& read == sizeof(signature) && memcmp(signature, "MDMP", sizeof(signature)) == 0;
		}

		[[nodiscard]] bool ValidateCrashReport(const std::wstring& path, DWORD pid)
		{
			Handle file(CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!file.Valid()) return false;
			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0
				|| size.QuadPart > 1024 * 1024) return false;
			std::string content(static_cast<size_t>(size.QuadPart), '\0');
			DWORD read = 0;
			if (!ReadFile(file.value, content.data(), static_cast<DWORD>(content.size()), &read, nullptr)
				|| read != static_cast<DWORD>(content.size())) return false;
			return content.find("ProcessId: " + std::to_string(pid) + "\r\n") != std::string::npos
				&& content.find("ExceptionCode: 0xE143C001") != std::string::npos
				&& content.find("DumpGenerated: true\r\n") != std::string::npos
				&& content.find("DumpFallbackAttempted: ") != std::string::npos
				&& content.find("DumpPrimaryError: ") != std::string::npos;
		}

		LONG WINAPI SuppressTestWer(EXCEPTION_POINTERS*) noexcept
		{
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[nodiscard]] int RunRealUefTestChild(int argc, wchar_t* const argv[])
		{
			if ((argc != 6 && argc != 7) || !IsTestDirectory(argv[5])) return 81;
			const bool manualReportStall = argc == 7
				&& wcscmp(argv[6], L"--manual-report-stall") == 0;
			const bool manualHold = argc == 7
				&& wcscmp(argv[6], L"--manual-report-hold") == 0;
			const bool noRestartStall = argc == 7
				&& wcscmp(argv[6], L"--no-restart-report-stall") == 0;
			const bool reportDiskFullOnce = argc == 7
				&& wcscmp(argv[6], L"--report-disk-full-once") == 0;
			const bool autoLostIntentStall = argc == 7
				&& wcscmp(argv[6], L"--auto-lost-intent-report-stall") == 0;
			if (argc == 7 && !manualReportStall && !manualHold
				&& !noRestartStall && !reportDiskFullOnce && !autoLostIntentStall) return 81;
			unsigned long long pidValue = 0, parentValue = 0, acknowledgeValue = 0;
			if (!ParseUnsigned(argv[2], pidValue) || pidValue == 0 || pidValue > MAXDWORD
				|| !ParseUnsigned(argv[3], parentValue) || parentValue == 0
				|| parentValue > std::numeric_limits<uintptr_t>::max()
				|| !ParseUnsigned(argv[4], acknowledgeValue) || acknowledgeValue == 0
				|| acknowledgeValue > std::numeric_limits<uintptr_t>::max()) return 82;
			const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parentValue));
			const HANDLE acknowledge = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(acknowledgeValue));
			DWORD parentFlags = 0, acknowledgeFlags = 0;
			if (!GetHandleInformation(parent, &parentFlags) || !(parentFlags & HANDLE_FLAG_INHERIT)
				|| !GetHandleInformation(acknowledge, &acknowledgeFlags)
				|| !(acknowledgeFlags & HANDLE_FLAG_INHERIT)
				|| GetProcessId(parent) != static_cast<DWORD>(pidValue)
				|| static_cast<DWORD>(pidValue) == GetCurrentProcessId()) return 83;
			std::wstring ownImage, parentImage;
			if (!CurrentImagePath(ownImage) || !ProcessImagePath(parent, parentImage)
				|| !EqualNoCase(ImageDirectory(ownImage).c_str(),
					(std::wstring(argv[5]) + L"\\bin").c_str())
				|| parentImage.find(L"Inkeys.exe") == std::wstring::npos) return 84;
			g_authorizedUefTestDirectory = argv[5];
			if (autoLostIntentStall)
			{
				LONG* slot = GetOffSignalInteropPointer();
				if (!slot || InterlockedCompareExchange(slot, 1, 0) != 0) return 87;
			}
			SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
			SetUnhandledExceptionFilter(SuppressTestWer);
			CrashHandler::SetFlag(noRestartStall ? 2 : (manualReportStall || manualHold ? 0 : 1));
			CrashHandler::IsSecond(false);
			if (manualReportStall || noRestartStall || autoLostIntentStall)
				CrashHandler::SetIsolatedUefTestMode(
					CrashHandler::IsolatedUefTestMode::StallAfterDumpOpen);
			else if (manualHold)
				CrashHandler::SetIsolatedUefTestMode(
					CrashHandler::IsolatedUefTestMode::ManualHoldAfterReport);
			else if (reportDiskFullOnce)
				CrashHandler::SetIsolatedUefTestMode(
					CrashHandler::IsolatedUefTestMode::ReportDiskFullOnce);
			CrashHandler::Initialize();
			if (!SetEvent(acknowledge)) return 85;
			// 已授权的测试 child 使用真实生产 UEF，异常不得在正常 C++ catch 中吞掉。
			RaiseException(0xE143C001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
			return 86;
		}

		enum class RealUefScenario : unsigned char { Auto, ManualStall, ManualHold, NoRestartStall, ReportDiskFullOnce, AutoLostIntentStall };

		[[nodiscard]] bool RunRealUefProcessTest(RealUefScenario scenario = RealUefScenario::Auto)
		{
			const bool manualReportStall = scenario == RealUefScenario::ManualStall;
			const bool manualHold = scenario == RealUefScenario::ManualHold;
			const bool noRestartStall = scenario == RealUefScenario::NoRestartStall;
			const bool reportDiskFullOnce = scenario == RealUefScenario::ReportDiskFullOnce;
			const bool autoLostIntentStall = scenario == RealUefScenario::AutoLostIntentStall;
			std::wstring directory, copiedImage;
			if (!MakeUefTestDirectory(directory, copiedImage)) return false;
			HANDLE inheritedParent = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
				&inheritedParent, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
			Handle parent(inheritedParent);
			Handle acknowledge(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			HANDLE inheritedAcknowledge = nullptr;
			if (!acknowledge.Valid() || !DuplicateHandle(GetCurrentProcess(), acknowledge.value,
				GetCurrentProcess(), &inheritedAcknowledge, EVENT_MODIFY_STATE, TRUE, 0)) return false;
			Handle childAcknowledge(inheritedAcknowledge);
			SIZE_T attributeBytes = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
			AttributeList attributes;
			attributes.memory = HeapAlloc(GetProcessHeap(), 0, attributeBytes);
			if (!attributes.memory) return false;
			STARTUPINFOEXW startup{};
			startup.StartupInfo.cb = sizeof(startup);
			startup.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.memory);
			if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes)) return false;
			attributes.initialized = true;
			HANDLE inherited[] = { parent.value, childAcknowledge.value };
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
				PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) return false;
			std::wstring arguments = std::wstring(kRealUefTestChildMode)
				+ L" " + std::to_wstring(GetCurrentProcessId())
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(parent.value))
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childAcknowledge.value))
				+ L" " + Quote(directory);
			if (manualReportStall) arguments += L" --manual-report-stall";
			if (manualHold) arguments += L" --manual-report-hold";
			if (noRestartStall) arguments += L" --no-restart-report-stall";
			if (reportDiskFullOnce) arguments += L" --report-disk-full-once";
			if (autoLostIntentStall) arguments += L" --auto-lost-intent-report-stall";
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(copiedImage, arguments, true,
				EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, &startup, process)) return false;
			Handle child(process.hProcess);
			Handle childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			const HANDLE signals[] = { acknowledge.value, child.value };
			const bool authorized = WaitForMultipleObjects(2, signals, FALSE, 5000) == WAIT_OBJECT_0;
			bool earlyRestart = false, identityCorrect = true;
			const ULONGLONG waitStart = GetTickCount64();
			while (authorized && WaitForSingleObject(child.value, 0) == WAIT_TIMEOUT
				&& GetTickCount64() - waitStart < 25000)
			{
				if (CountRestartMarkers(directory, process.dwProcessId, 2, identityCorrect) != 0)
					earlyRestart = true;
				Sleep(20);
			}
			const bool oldDead = WaitForSingleObject(child.value, 0) == WAIT_OBJECT_0;
			DWORD oldExitCode = 0;
			const BOOL hasExitCode = oldDead && GetExitCodeProcess(child.value, &oldExitCode);
			DWORD restartCount = 0;
			const ULONGLONG restartWaitStart = GetTickCount64();
			while (oldDead && scenario == RealUefScenario::Auto && (restartCount = CountRestartMarkers(directory,
				process.dwProcessId, 2, identityCorrect)) == 0
				&& GetTickCount64() - restartWaitStart < 5000)
				Sleep(20);
			Sleep(250);
			restartCount = CountRestartMarkers(directory, process.dwProcessId, 2, identityCorrect);
			const std::wstring crashDirectory = directory + L"\\bin\\Inkeys\\Crash";
			std::wstring dumpPath, reportPath;
			ULONGLONG dumpBytes = 0, reportBytes = 0;
			const bool dump = FindCrashArtifact(crashDirectory, process.dwProcessId,
				L".dmp", dumpPath, dumpBytes) && dumpBytes >= 32 && ValidateDumpFile(dumpPath);
			const bool report = FindCrashArtifact(crashDirectory, process.dwProcessId,
				L".txt", reportPath, reportBytes)
				&& ValidateCrashReport(reportPath, process.dwProcessId);
			WIN32_FIND_DATAW partialFound{};
			HANDLE partialSearch = FindFirstFileW((crashDirectory + L"\\*_"
				+ std::to_wstring(process.dwProcessId) + L".dm_").c_str(),
				&partialFound);
			const bool pendingDump = partialSearch != INVALID_HANDLE_VALUE
				&& !(partialFound.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				&& ((static_cast<ULONGLONG>(partialFound.nFileSizeHigh) << 32)
					| partialFound.nFileSizeLow) > 0;
			if (partialSearch != INVALID_HANDLE_VALUE) FindClose(partialSearch);
			char observation[320]{};
			const int textLength = sprintf_s(observation,
				"shutdown-supervisor real_uef_old_pid=%lu exit_code=%lu authorized=%u dump=%u dump_bytes=%llu report=%u report_bytes=%llu pending_dump=%u early_restart=%u restart_count=%lu\n",
				process.dwProcessId, oldExitCode, authorized ? 1u : 0u, dump ? 1u : 0u,
				dumpBytes, report ? 1u : 0u, reportBytes, pendingDump ? 1u : 0u,
				earlyRestart ? 1u : 0u, restartCount);
			DWORD written = 0;
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			if (textLength > 0 && output && output != INVALID_HANDLE_VALUE)
				WriteFile(output, observation, static_cast<DWORD>(textLength), &written, nullptr);
			if (!oldDead)
			{
				// 红测上限后只结束本轮仍持有的精确 child HANDLE，不碰用户进程。
				TerminateProcess(child.value, ERROR_OPERATION_ABORTED);
				WaitForSingleObject(child.value, kDeathWaitMilliseconds);
			}
			// 只尝试删除本轮复制的 EXE；dump/report/marker 留在唯一忽略目录供复核。
			DeleteFileW(copiedImage.c_str());
			if (manualReportStall || noRestartStall || autoLostIntentStall)
				return authorized && oldDead && hasExitCode
					&& oldExitCode == 0xE1430017 && !dump && !report
					&& pendingDump && !earlyRestart && restartCount == 0;
			if (manualHold)
				return authorized && oldDead && hasExitCode
					&& oldExitCode == 0xE143C001 && dump && report
					&& !pendingDump && !earlyRestart && restartCount == 0
					&& GetTickCount64() - waitStart >= 16000;
			return authorized && oldDead && hasExitCode && oldExitCode != 0
				&& dump && report && !earlyRestart && identityCorrect && restartCount == 1;
		}

		[[nodiscard]] int RunNoGuiTests()
		{
			std::wstring image;
			if (!CurrentImagePath(image)) return 61;
			bool passed = true;
			auto check = [&](const char* name, bool outcome)
			{
				PrintTestResult(name, outcome);
				passed = passed && outcome;
			};
			check("natural-close", RunProcessCase(image, L"close-natural", 15000, true, false));
			check("natural-restart", RunProcessCase(image, L"restart-natural", 15000, true, true));
			check("crash-restart-natural", RunProcessCase(image, L"crash-natural", 15000, true, true));
			check("forced-restart", RunProcessCase(image, L"restart-hung-short", 3000, false, true));
			check("crash-restart-forced", RunProcessCase(image, L"crash-hung-short", 3000, false, true));
			check("duplicate-request", RunProcessCase(image, L"duplicate", 3000, false, false));
			check("wrong-parent-pid", RunProcessCase(image, L"bad-pid", 3000, true, false));
			check("manual-pending-close-wins", RunProcessCase(image, L"manual-pending-close", 6000, false, false));
			check("late-death-restart", RunProcessCase(image, L"late-death-restart", 3000, true, true));
			check("fallback-create-failure-close", RunProcessCase(image, L"create-fail-close", 3000, false, false));
			check("fallback-create-failure-restart", RunProcessCase(image, L"create-fail-restart", 3000, false, false));
			check("fallback-handshake-failure", RunProcessCase(image, L"bad-pid-hung", 6000, false, false));
			check("fallback-helper-early-exit", RunProcessCase(image, L"helper-early-exit", 3000, false, false));
			check("malformed-handshake", RunMalformedHandshakeCase(image));
			check("quoted-unicode-path", RunQuotedUnicodePathCase());
			check("real-uef-auto-restart", RunRealUefProcessTest());
			check("manual-uef-report-deadline", RunRealUefProcessTest(RealUefScenario::ManualStall));
			check("manual-uef-cancel-hold", RunRealUefProcessTest(RealUefScenario::ManualHold));
			check("no-restart-uef-report-deadline", RunRealUefProcessTest(RealUefScenario::NoRestartStall));
			check("report-disk-full-preserves-dump", RunRealUefProcessTest(RealUefScenario::ReportDiskFullOnce));
			check("auto-lost-intent-report-deadline", RunRealUefProcessTest(RealUefScenario::AutoLostIntentStall));
			check("forced-close-15-seconds", RunProcessCase(image, L"close-hung15", 15000, false, false));
			return passed ? 0 : 62;
		}
	}

	ArmResult ArmShutdownSupervisor(Intent intent, DWORD deadlineMilliseconds) noexcept
	{
		return ArmCore(intent, deadlineMilliseconds,
			g_authorizedUefTestDirectory.empty() ? nullptr
				: g_authorizedUefTestDirectory.c_str());
	}

	bool TryClaimCrashRestartIntent(LONG* intentSlot, int crashMode,
		bool userAccepted) noexcept
	{
		// 手动确认前或用户拒绝时不占槽，正式 Close/Restart 仍可优先建立自己的15秒保护。
		if (crashMode != 1 && !(crashMode == 0 && userAccepted)) return false;
		return intentSlot && InterlockedCompareExchange(intentSlot, 3, 0) == 0;
	}

	bool TryRunShutdownSupervisorEarly(LPCWSTR fullCommandLine, int& exitCode) noexcept
	{
		if (!fullCommandLine) return false;
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(fullCommandLine, &argc);
		if (!argv)
		{
			// 内部模式解析失败也不得落入正常 GUI 初始化路径。
			if (wcsstr(fullCommandLine, kSupervisorMode)
				|| wcsstr(fullCommandLine, kTestParentMode)
				|| wcsstr(fullCommandLine, kTestRestartedMode)
				|| wcsstr(fullCommandLine, kRealUefTestChildMode)
				|| wcsstr(fullCommandLine, kTestMode))
			{
				exitCode = 70;
				return true;
			}
			return false;
		}
		bool recognized = argc >= 2 && (EqualNoCase(argv[1], kSupervisorMode)
			|| EqualNoCase(argv[1], kTestMode)
			|| EqualNoCase(argv[1], kTestParentMode)
			|| EqualNoCase(argv[1], kTestRestartedMode)
			|| EqualNoCase(argv[1], kRealUefTestChildMode));
		if (recognized && EqualNoCase(argv[1], kRealUefTestChildMode))
		{
			// 真实 RaiseException 必须位于任何 C++ catch 外，才能验证实际 UEF。
			exitCode = RunRealUefTestChild(argc, argv);
			LocalFree(argv);
			return true;
		}
		if (recognized)
		{
			try
			{
				if (EqualNoCase(argv[1], kSupervisorMode)) exitCode = RunSupervisorChild(argc, argv);
				else if (EqualNoCase(argv[1], kTestMode)) exitCode = argc == 2 ? RunNoGuiTests() : 71;
				else if (EqualNoCase(argv[1], kTestParentMode)) exitCode = RunTestParent(argc, argv);
				else exitCode = RunTestRestarted(argc, argv);
			}
			catch (...) { exitCode = 72; }
		}
		LocalFree(argv);
		return recognized;
	}
}
