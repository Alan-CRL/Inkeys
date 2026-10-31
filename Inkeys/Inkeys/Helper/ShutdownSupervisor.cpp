import Inkeys.Helper.CrashHandler;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ShutdownSupervisor.h"
#include "FailedCleanupDeadline.h"
#include "FailedCleanupRealCases.h"

#include <shellapi.h>
#include <objbase.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "Shell32.lib")

// 现有 IdtMain 进程级退出意图槽；测试仅在已验真的 copied child 内模拟先 CAS、未 Arm 的窗口。
LONG* GetOffSignalInteropPointer();
void SetOffSignal(int signal);

namespace Inkeys::Shutdown
{
	namespace
	{
		constexpr wchar_t kSupervisorMode[] = L"--inkeys-internal-shutdown-supervisor-v1";
		constexpr wchar_t kTestMode[] = L"--shutdown-supervisor-tests";
		constexpr wchar_t kTestParentMode[] = L"--inkeys-internal-shutdown-test-parent";
		constexpr wchar_t kTestRestartedMode[] = L"--inkeys-internal-shutdown-test-restarted";
		constexpr wchar_t kRealUefTestChildMode[] = L"--inkeys-internal-real-uef-test-child";
		constexpr wchar_t kStartupFailureChildMode[] = L"--inkeys-internal-startup-failure-child-v1";
		constexpr wchar_t kCleanupChildMode[] = L"--inkeys-internal-failed-cleanup-child-v1";
		constexpr wchar_t kCleanupRealChildMode[] = L"--inkeys-internal-failed-cleanup-real-child-v1";
		constexpr wchar_t kTestDirectoryPrefix[] = L"Inkeys Shutdown Test ";
		constexpr wchar_t kUefTestDirectoryPrefix[] = L"Inkeys Shutdown Test UEF ";
		constexpr DWORD kForcedExitCode = 0xE1430015;
		constexpr DWORD kFallbackForcedExitCode = 0xE1430016;
		constexpr DWORD kFailedArmCloseForcedExitCode = 0xE1430018;
		constexpr DWORD kFailedArmRestartForcedExitCode = 0xE1430019;
		constexpr DWORD kHandshakeWaitMilliseconds = 2500;
		constexpr DWORD kDeathWaitMilliseconds = 5000;
		std::atomic<int> g_armState{ 0 }; // 0 未请求，1 创建中，2 helper 已握手，3 全失败，4 仅自身兜底。
		std::atomic<ULONGLONG> g_fallbackDeadlineTick{ 0 };
		std::wstring g_authorizedUefTestDirectory; // 仅继承句柄验真的显式测试子进程写入。
		enum class FailedArmTestScenario : unsigned char
		{
			None, CloseCreate, RestartCreate, CloseHandshake, RestartHandshake,
			CloseConsumed, CloseExpired
		};
		std::atomic<FailedArmTestScenario> g_failedArmTestScenario{ FailedArmTestScenario::None };
		constexpr DWORD kFailedArmObservationMagic = 0x1430FA01;
		struct FailedArmTestObservation
		{
			DWORD magic;
			DWORD version;
			DWORD bytes;
			volatile LONG published;
			ULONGLONG deadlineTick;
			ULONGLONG failedTick;
			DWORD result;
			DWORD error;
			DWORD state;
			DWORD reserved;
		};
		static_assert(sizeof(FailedArmTestObservation) == 48);
		static_assert(offsetof(FailedArmTestObservation, deadlineTick) == 16);
		static_assert(offsetof(FailedArmTestObservation, published) == 12);
		FailedArmTestObservation* g_failedArmTestObservation = nullptr; // 仅授权child，Arm前已映射。
		constexpr DWORD kStartupFailureObservationMagic = 0x1430FA02;
		struct StartupFailureObservation
		{
			DWORD magic, version, bytes, site;
			volatile LONG authorized, failurePublished, armPublished, gatePublished;
			DWORD expectedFailureCode, realInitResult;
			DWORD boundary, armResult, armError, armState;
			LONG acceptedIntent;
			DWORD testMode; // 0 保持旧停滞用例；1 仅授权child自然确认退出。
			ULONGLONG requestTick, failureTick, armStartedTick, armDeadlineTick, gateTick;
			DWORD windowCountAtGate, reserved2;
			ULONGLONG reserved3[2];
		};
		static_assert(sizeof(StartupFailureObservation) == 128);
		static_assert(offsetof(StartupFailureObservation, requestTick) == 64);
		static_assert(offsetof(StartupFailureObservation, armDeadlineTick) == 88);
		StartupFailureObservation* g_startupFailureObservation = nullptr;
		std::atomic<StartupFailureSite> g_startupFailureSite{ StartupFailureSite::None };

		enum class CleanupPrimitiveScenario : DWORD { None, Expiry, Allocation, Monitor, Join, Ordinary, Cancel, Dormant, Earlier, BeginCancel, ExpiryCancel };
		constexpr DWORD kCleanupObservationMagic = 0x1430FA03;
		struct CleanupPrimitiveObservation
		{
			DWORD magic, version, bytes, scenario;
			volatile LONG authorized, activated, published, afterComplete;
			ULONGLONG graceDeadline, publishTick, existingDeadline;
			volatile LONG monitorPaused, cancelWitness;
			ULONGLONG activateTick, childStartTick;
			DWORD injectedFlag, reserved;
			ULONGLONG padding;
		};
		static_assert(sizeof(CleanupPrimitiveObservation) == 96);
		static_assert(offsetof(CleanupPrimitiveObservation, graceDeadline) == 32);
		CleanupPrimitiveObservation* g_cleanupObservation = nullptr;
		HANDLE g_cleanupMonitorPaused = nullptr;
		ULONGLONG g_cleanupEarlierDeadline = 0; // 仅验真child显式earlier场景。
		FailedCleanupSignal g_cleanupConcurrentSignal; // 只在同一验真child启动其producer前写。
		CleanupRealPacket* g_cleanupRealObservation = nullptr;
		CleanupRealCase g_cleanupRealCase = CleanupRealCase::None;
		std::wstring g_cleanupRealDirectory;
		std::atomic_bool g_cleanupRealFatalRecorded{ false };

		DWORD WINAPI BeginCleanupTestSignal(void*) noexcept
		{
			g_cleanupObservation->graceDeadline = g_cleanupConcurrentSignal.BeginKnownFailure();
			InterlockedExchange(&g_cleanupObservation->activated, 1);
			return 0;
		}

		ULONGLONG PublishCleanupTestFatal() noexcept
		{
			ULONGLONG deadline = PublishFatalFailedCleanupNoWait();
			if (g_cleanupEarlierDeadline && (!deadline || g_cleanupEarlierDeadline < deadline))
				deadline = g_cleanupEarlierDeadline;
			auto* observation = g_cleanupObservation;
			if (observation && InterlockedCompareExchange(&observation->published, 2, 0) == 0)
			{
				observation->publishTick = GetTickCount64();
				observation->existingDeadline = deadline;
				if (g_cleanupMonitorPaused)
					InterlockedExchange(&observation->monitorPaused,
						WaitForSingleObject(g_cleanupMonitorPaused, 0) == WAIT_OBJECT_0 ? 1 : 0);
				InterlockedExchange(&observation->published, 1);
			}
			return deadline;
		}

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

		struct MappedView
		{
			void* value = nullptr;
			MappedView() = default;
			explicit MappedView(void* view) noexcept : value(view) {}
			MappedView(const MappedView&) = delete;
			MappedView& operator=(const MappedView&) = delete;
			~MappedView() { if (value) UnmapViewOfFile(value); }
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
			bool simulateLateDeathForTest = false,
			bool simulateFallbackCreateFailureForTest = false,
			DWORD simulateFailureDelayForTest = 0) noexcept
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
						|| simulateEarlyExitForTest || simulateLateDeathForTest
						|| simulateFallbackCreateFailureForTest || simulateFailureDelayForTest) && !testDirectory)
					|| simulateFailureDelayForTest > 17000
					|| (simulateFailureDelayForTest && (!simulateFallbackCreateFailureForTest
						|| !simulateCreateFailureForTest))
					|| (testDirectory && !IsTestDirectory(testDirectory)))
				{
					failure = ERROR_INVALID_PARAMETER;
				}
				else
				{
					// 接受意图的时钟先于 CreateProcess 与握手，卡住的业务线程不参与计时。
					const ULONGLONG deadline = GetTickCount64() + deadlineMilliseconds;
					g_fallbackDeadlineTick.store(deadline, std::memory_order_release);
					Handle fallback((simulateLateDeathForTest || simulateFallbackCreateFailureForTest) ? nullptr
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
			// 仅已验真的 copied child 模拟 Arm 已消耗/超过原始截止；正式调用始终为0。
			if (testDirectory && simulateFallbackCreateFailureForTest
				&& simulateCreateFailureForTest && simulateFailureDelayForTest <= 17000
				&& g_fallbackDeadlineTick.load(std::memory_order_acquire) != 0)
				Sleep(simulateFailureDelayForTest);
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
			DWORD first, DWORD second, int third = -1, bool flushToDisk = false) noexcept
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
				&written, nullptr) && written == static_cast<DWORD>(length)
				&& (!flushToDisk || FlushFileBuffers(file.value));
		}

		[[nodiscard]] bool ExactDurableMarker(const std::wstring& path,
			DWORD first, DWORD second) noexcept
		{
			Handle file(CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!file.Valid()) return false;
			char expected[64]{}, actual[64]{};
			const int length = sprintf_s(expected, "%lu %lu\n", first, second);
			DWORD read = 0;
			return length > 0 && ReadFile(file.value, actual, sizeof(actual), &read, nullptr)
				&& read == static_cast<DWORD>(length)
				&& memcmp(expected, actual, read) == 0;
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

		[[nodiscard]] FailedArmTestScenario ParseFailedArmTest(const wchar_t* option) noexcept
		{
			if (!option) return FailedArmTestScenario::None;
			if (wcscmp(option, L"--failed-arm-create-close") == 0) return FailedArmTestScenario::CloseCreate;
			if (wcscmp(option, L"--failed-arm-create-restart") == 0) return FailedArmTestScenario::RestartCreate;
			if (wcscmp(option, L"--failed-arm-handshake-close") == 0) return FailedArmTestScenario::CloseHandshake;
			if (wcscmp(option, L"--failed-arm-handshake-restart") == 0) return FailedArmTestScenario::RestartHandshake;
			if (wcscmp(option, L"--failed-arm-consumed-close") == 0) return FailedArmTestScenario::CloseConsumed;
			if (wcscmp(option, L"--failed-arm-expired-close") == 0) return FailedArmTestScenario::CloseExpired;
			return FailedArmTestScenario::None;
		}

		[[nodiscard]] bool IsFailedArmRestart(FailedArmTestScenario scenario) noexcept
		{
			return scenario == FailedArmTestScenario::RestartCreate
				|| scenario == FailedArmTestScenario::RestartHandshake;
		}

		[[nodiscard]] bool HasExactFailedArmChildIdentity(const std::wstring& ownImage,
			const std::wstring& parentImage, const wchar_t* directory,
			const wchar_t* expectedParentImage) noexcept
		{
			try
			{
				if (!expectedParentImage || wcslen(expectedParentImage) < 3) return false;
				const bool absolute = (expectedParentImage[1] == L':'
					&& ((expectedParentImage[0] >= L'A' && expectedParentImage[0] <= L'Z')
						|| (expectedParentImage[0] >= L'a' && expectedParentImage[0] <= L'z'))
					&& (expectedParentImage[2] == L'\\' || expectedParentImage[2] == L'/'))
					|| (expectedParentImage[0] == L'\\' && expectedParentImage[1] == L'\\');
				if (!absolute || !SameExecutableFile(parentImage, expectedParentImage)) return false;
				const std::wstring binaryDirectory = std::wstring(directory) + L"\\bin";
				const std::wstring copiedImage = binaryDirectory + L"\\Inkeys.exe";
				for (const wchar_t* path : { directory, binaryDirectory.c_str() })
				{
					const DWORD attributes = GetFileAttributesW(path);
					if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
						|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
				}
				const DWORD copiedAttributes = GetFileAttributesW(copiedImage.c_str());
				return copiedAttributes != INVALID_FILE_ATTRIBUTES
					&& !(copiedAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
					&& EqualNoCase(ImageDirectory(ownImage).c_str(), binaryDirectory.c_str())
					&& SameExecutableFile(ownImage, copiedImage);
			}
			catch (...) { return false; }
		}

		[[nodiscard]] StartupFailureSite ParseStartupFailureSite(const wchar_t* value) noexcept
		{
			if (!value) return StartupFailureSite::None;
			if (wcscmp(value, L"D004") == 0) return StartupFailureSite::Com;
			if (wcscmp(value, L"D005") == 0) return StartupFailureSite::PptCom;
			if (wcscmp(value, L"D003") == 0) return StartupFailureSite::Font;
			if (wcscmp(value, L"B002") == 0) return StartupFailureSite::BarState;
			return StartupFailureSite::None;
		}

		[[nodiscard]] DWORD StartupFailureCode(StartupFailureSite site) noexcept
		{
			switch (site)
			{
			case StartupFailureSite::Com: return 0xD004u;
			case StartupFailureSite::PptCom: return 0xD005u;
			case StartupFailureSite::Font: return 0xD003u;
			case StartupFailureSite::BarState: return 0xB002u;
			default: return 0;
			}
		}

		[[nodiscard]] int AuthorizeStartupFailureChild(int argc, wchar_t* const argv[])
		{
			if (argc != 9 || !IsTestDirectory(argv[5])) return 81;
			const auto site = ParseStartupFailureSite(argv[6]);
			if (site == StartupFailureSite::None) return 81;
			unsigned long long pidValue = 0, parentValue = 0, acknowledgeValue = 0,
				observationValue = 0;
			if (!ParseUnsigned(argv[2], pidValue) || pidValue == 0 || pidValue > MAXDWORD
				|| !ParseUnsigned(argv[3], parentValue) || parentValue == 0
				|| parentValue > std::numeric_limits<uintptr_t>::max()
				|| !ParseUnsigned(argv[4], acknowledgeValue) || acknowledgeValue == 0
				|| acknowledgeValue > std::numeric_limits<uintptr_t>::max()
				|| !ParseUnsigned(argv[8], observationValue) || observationValue == 0
				|| observationValue > std::numeric_limits<uintptr_t>::max()) return 82;
			const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parentValue));
			const HANDLE acknowledge = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(acknowledgeValue));
			const HANDLE observationHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(observationValue));
			DWORD parentFlags = 0, acknowledgeFlags = 0, observationFlags = 0;
			if (!GetHandleInformation(parent, &parentFlags) || !(parentFlags & HANDLE_FLAG_INHERIT)
				|| !GetHandleInformation(acknowledge, &acknowledgeFlags)
				|| !(acknowledgeFlags & HANDLE_FLAG_INHERIT)
				|| !GetHandleInformation(observationHandle, &observationFlags)
				|| !(observationFlags & HANDLE_FLAG_INHERIT)
				|| GetProcessId(parent) != static_cast<DWORD>(pidValue)
				|| static_cast<DWORD>(pidValue) == GetCurrentProcessId()) return 83;
			std::wstring ownImage, parentImage;
			if (!CurrentImagePath(ownImage) || !ProcessImagePath(parent, parentImage)
				|| !HasExactFailedArmChildIdentity(ownImage, parentImage,
					argv[5], argv[7])) return 84;
			void* view = MapViewOfFile(observationHandle, FILE_MAP_WRITE,
				0, 0, sizeof(StartupFailureObservation));
			if (!view) return 83;
			auto* observation = static_cast<StartupFailureObservation*>(view);
			if (observation->magic != kStartupFailureObservationMagic
				|| observation->version != 1 || observation->bytes != sizeof(*observation)
				|| observation->site != static_cast<DWORD>(site)
				|| observation->testMode > 1
				|| observation->expectedFailureCode != StartupFailureCode(site)
				|| InterlockedCompareExchange(&observation->authorized, 0, 0) != 0)
			{
				UnmapViewOfFile(view);
				return 83;
			}
			g_startupFailureObservation = observation; // 鉴权后才持有；由进程结束释放。
			g_startupFailureSite.store(site, std::memory_order_release);
			InterlockedExchange(&observation->authorized, 1);
			if (!WriteMarker(MarkerPath(argv[5], L"durable", GetCurrentProcessId()),
				GetCurrentProcessId(), 1, -1, true)) return 89;
			if (!SetEvent(acknowledge)) return 85;
			return 0;
		}

		[[nodiscard]] int RunRealUefTestChild(int argc, wchar_t* const argv[])
		{
			const FailedArmTestScenario failedArm = argc == 9
				? ParseFailedArmTest(argv[6]) : FailedArmTestScenario::None;
			if ((argc != 6 && argc != 7 && failedArm == FailedArmTestScenario::None)
				|| !IsTestDirectory(argv[5])) return 81;
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
			// 新故障选项还须验证精确 parent 文件与非reparse复制链，不能只凭旧文件名子串授权。
			if (failedArm != FailedArmTestScenario::None
				&& !HasExactFailedArmChildIdentity(ownImage, parentImage, argv[5], argv[7])) return 84;
			Handle observationHandle;
			MappedView observationView;
			if (failedArm != FailedArmTestScenario::None)
			{
				unsigned long long observationValue = 0;
				DWORD observationFlags = 0;
				if (!ParseUnsigned(argv[8], observationValue) || observationValue == 0
					|| observationValue > std::numeric_limits<uintptr_t>::max()) return 82;
				observationHandle.value = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(observationValue));
				if (!GetHandleInformation(observationHandle.value, &observationFlags)
					|| !(observationFlags & HANDLE_FLAG_INHERIT)) return 83;
				observationView.value = MapViewOfFile(observationHandle.value, FILE_MAP_WRITE,
					0, 0, sizeof(FailedArmTestObservation));
				if (!observationView.value) return 83;
				auto* observation = static_cast<FailedArmTestObservation*>(observationView.value);
				if (observation->magic != kFailedArmObservationMagic || observation->version != 1
					|| observation->bytes != sizeof(FailedArmTestObservation)
					|| InterlockedCompareExchange(&observation->published, 0, 0) != 0) return 83;
				g_failedArmTestObservation = observation;
			}
			g_authorizedUefTestDirectory = argv[5];
			if (failedArm != FailedArmTestScenario::None)
			{
				g_failedArmTestScenario.store(failedArm, std::memory_order_release);
				const DWORD signal = IsFailedArmRestart(failedArm) ? 2u : 1u;
				if (!WriteMarker(MarkerPath(argv[5], L"durable", GetCurrentProcessId()),
					GetCurrentProcessId(), signal, -1, true)) return 89;
				if (!SetEvent(acknowledge)) return 85;
				// 真正进入产品意图/Arm/清理入口；红版本会返回后卡在这里，绿版本不得返回。
				SetOffSignal(static_cast<int>(signal));
				(void)WriteMarker(MarkerPath(argv[5], L"cleanup-entered", GetCurrentProcessId()),
					GetCurrentProcessId(), signal);
				Sleep(INFINITE);
				return 90;
			}
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

		enum class RealUefScenario : unsigned char
		{
			Auto, ManualStall, ManualHold, NoRestartStall, ReportDiskFullOnce, AutoLostIntentStall,
			FailedArmCloseCreate, FailedArmRestartCreate, FailedArmCloseHandshake, FailedArmRestartHandshake,
			FailedArmCloseConsumed, FailedArmCloseExpired
		};

		[[nodiscard]] const wchar_t* FailedArmOption(RealUefScenario scenario) noexcept
		{
			switch (scenario)
			{
			case RealUefScenario::FailedArmCloseCreate: return L"--failed-arm-create-close";
			case RealUefScenario::FailedArmRestartCreate: return L"--failed-arm-create-restart";
			case RealUefScenario::FailedArmCloseHandshake: return L"--failed-arm-handshake-close";
			case RealUefScenario::FailedArmRestartHandshake: return L"--failed-arm-handshake-restart";
			case RealUefScenario::FailedArmCloseConsumed: return L"--failed-arm-consumed-close";
			case RealUefScenario::FailedArmCloseExpired: return L"--failed-arm-expired-close";
			default: return nullptr;
			}
		}

		[[nodiscard]] bool RunRealUefProcessTest(RealUefScenario scenario = RealUefScenario::Auto,
			bool wrongParentIdentityForTest = false)
		{
			const wchar_t* failedArmOption = FailedArmOption(scenario);
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
			Handle observationMapping;
			Handle childObservation;
			MappedView observationView;
			if (failedArmOption)
			{
				observationMapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
					PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(FailedArmTestObservation)), nullptr);
				if (!observationMapping.Valid()) return false;
				observationView.value = MapViewOfFile(observationMapping.value, FILE_MAP_WRITE,
					0, 0, sizeof(FailedArmTestObservation));
				if (!observationView.value) return false;
				auto* observation = static_cast<FailedArmTestObservation*>(observationView.value);
				memset(observation, 0, sizeof(*observation));
				observation->magic = kFailedArmObservationMagic;
				observation->version = 1;
				observation->bytes = static_cast<DWORD>(sizeof(*observation));
				if (!DuplicateHandle(GetCurrentProcess(), observationMapping.value, GetCurrentProcess(),
					&childObservation.value, FILE_MAP_WRITE, TRUE, 0)) return false;
			}
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
			HANDLE inherited[] = { parent.value, childAcknowledge.value, childObservation.value };
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
				PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
				(failedArmOption ? 3u : 2u) * sizeof(HANDLE), nullptr, nullptr)) return false;
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
			if (failedArmOption)
			{
				std::wstring launcherImage;
				if (!CurrentImagePath(launcherImage)) return false;
				arguments += L" " + std::wstring(failedArmOption) + L" "
					+ Quote(wrongParentIdentityForTest ? copiedImage : launcherImage)
					+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childObservation.value));
			}
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
			const ULONGLONG deathObservedTick = GetTickCount64();
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
			if (failedArmOption)
			{
				const DWORD signal = IsFailedArmRestart(ParseFailedArmTest(failedArmOption)) ? 2u : 1u;
				auto* observation = static_cast<FailedArmTestObservation*>(observationView.value);
				// writer填完固定字段才Interlocked发布；parent仍持有mapping，child死亡也保留快照。
				const bool published = InterlockedCompareExchange(&observation->published, 0, 0) == 1;
				const DWORD armResult = published ? observation->result : MAXDWORD;
				const DWORD armError = published ? observation->error : MAXDWORD;
				const DWORD armState = published ? observation->state : MAXDWORD;
				const ULONGLONG failureTick = published ? observation->failedTick : 0;
				const ULONGLONG originalDeadline = published ? observation->deadlineTick : 0;
				const ULONGLONG remainingTicks = originalDeadline > failureTick ? originalDeadline - failureTick : 0;
				const DWORD remaining = published && remainingTicks <= 60000
					? static_cast<DWORD>(remainingTicks) : MAXDWORD;
				const bool cleanupEntered = GetFileAttributesW(MarkerPath(directory, L"cleanup-entered",
					process.dwProcessId).c_str()) != INVALID_FILE_ATTRIBUTES;
				const bool durable = ExactDurableMarker(MarkerPath(directory, L"durable", process.dwProcessId),
					process.dwProcessId, signal);
				const ULONGLONG elapsed = deathObservedTick - waitStart;
				const DWORD failureToDeath = published && oldDead
					? static_cast<DWORD>(deathObservedTick - failureTick) : MAXDWORD;
				char observationText[512]{};
				const int length = sprintf_s(observationText,
					"shutdown-supervisor failed_arm_old_pid=%lu exit_code=%lu old_dead=%u authorized=%u elapsed_ms=%llu arm_result=%lu arm_state=%lu arm_error=%lu deadline_tick=%llu failed_tick=%llu remaining_ms=%lu failure_to_death_ms=%lu cleanup_entered=%u durable=%u restart_count=%lu wrong_identity=%u\n",
					process.dwProcessId, oldDead ? oldExitCode : STILL_ACTIVE, oldDead ? 1u : 0u,
					authorized ? 1u : 0u, elapsed, armResult,
					armState, armError, originalDeadline, failureTick, remaining, failureToDeath, cleanupEntered ? 1u : 0u,
					durable ? 1u : 0u, restartCount, wrongParentIdentityForTest ? 1u : 0u);
				DWORD written = 0;
				const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
				if (length > 0 && output && output != INVALID_HANDLE_VALUE)
					WriteFile(output, observationText, static_cast<DWORD>(length), &written, nullptr);
				if (!oldDead)
				{
					TerminateProcess(child.value, ERROR_OPERATION_ABORTED);
					WaitForSingleObject(child.value, kDeathWaitMilliseconds);
				}
				DeleteFileW(copiedImage.c_str());
				if (wrongParentIdentityForTest)
					return !authorized && oldDead && hasExitCode && oldExitCode == 84
						&& !published && !cleanupEntered && !durable && restartCount == 0;
				const bool expired = scenario == RealUefScenario::FailedArmCloseExpired;
				const bool consumed = scenario == RealUefScenario::FailedArmCloseConsumed;
				const bool timingCorrect = expired ? elapsed >= 16000 && elapsed <= 23000
					&& remaining == 0 && failureToDeath <= 2000
					: elapsed >= 14000 && elapsed <= 21000
						&& (!consumed || (remaining >= 6000 && remaining <= 10000
							&& elapsed <= 19000 && failureToDeath <= 12000));
				return authorized && oldDead && hasExitCode && !earlyRestart && restartCount == 0
					&& oldExitCode == (signal == 2 ? kFailedArmRestartForcedExitCode : kFailedArmCloseForcedExitCode)
					&& published && armResult == static_cast<DWORD>(ArmResult::Failed)
					&& armState == 3 && armError != ERROR_SUCCESS && originalDeadline != 0 && timingCorrect
					&& !cleanupEntered && durable;
			}
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

		[[nodiscard]] bool RunFailedArmNoInheritedHandlesCase()
		{
			std::wstring directory, copiedImage, launcherImage;
			if (!MakeUefTestDirectory(directory, copiedImage) || !CurrentImagePath(launcherImage)) return false;
			const std::wstring arguments = std::wstring(kRealUefTestChildMode) + L" "
				+ std::to_wstring(GetCurrentProcessId()) + L" 4 8 " + Quote(directory)
				+ L" --failed-arm-create-close " + Quote(launcherImage) + L" 12";
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(copiedImage, arguments, false, CREATE_NO_WINDOW, nullptr, process)) return false;
			Handle child(process.hProcess), childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			const bool oldDead = WaitForSingleObject(child.value, 3000) == WAIT_OBJECT_0;
			DWORD exitCode = 0;
			const bool passed = oldDead && GetExitCodeProcess(child.value, &exitCode) && exitCode == 83;
			if (oldDead) DeleteFileW(copiedImage.c_str());
			PrintCaseObservation(process.dwProcessId, exitCode, 0, 0);
			return passed;
		}

		[[nodiscard]] bool RunFailedArmTests()
		{
			bool passed = true;
			auto check = [&](const char* name, bool outcome)
			{
				PrintTestResult(name, outcome);
				passed = passed && outcome;
			};
			check("failed-arm-create-close", RunRealUefProcessTest(RealUefScenario::FailedArmCloseCreate));
			check("failed-arm-create-restart", RunRealUefProcessTest(RealUefScenario::FailedArmRestartCreate));
			check("failed-arm-handshake-close", RunRealUefProcessTest(RealUefScenario::FailedArmCloseHandshake));
			check("failed-arm-handshake-restart", RunRealUefProcessTest(RealUefScenario::FailedArmRestartHandshake));
			check("failed-arm-consumed-deadline", RunRealUefProcessTest(RealUefScenario::FailedArmCloseConsumed));
			check("failed-arm-expired-deadline", RunRealUefProcessTest(RealUefScenario::FailedArmCloseExpired));
			check("failed-arm-exact-parent-rejection",
				RunRealUefProcessTest(RealUefScenario::FailedArmCloseCreate, true));
			check("failed-arm-no-inherited-handles", RunFailedArmNoInheritedHandlesCase());
			return passed;
		}

		[[nodiscard]] const wchar_t* StartupFailureOption(StartupFailureSite site) noexcept
		{
			switch (site)
			{
			case StartupFailureSite::Com: return L"D004";
			case StartupFailureSite::PptCom: return L"D005";
			case StartupFailureSite::Font: return L"D003";
			case StartupFailureSite::BarState: return L"B002";
			default: return nullptr;
			}
		}

		struct StartupPromptDismissal
		{
			DWORD childPid = 0;
			HWND prompt = nullptr;
			ULONGLONG postedTick = 0;
		};

		BOOL CALLBACK ConfirmOwnedStartupPrompt(HWND hwnd, LPARAM parameter) noexcept
		{
			auto& dismissal = *reinterpret_cast<StartupPromptDismissal*>(parameter);
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid != dismissal.childPid || !IsWindowVisible(hwnd)) return TRUE;
			wchar_t title[80]{}, className[96]{};
			if (!GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)))
				|| wcscmp(title, L"Inkeys Tips") != 0
				|| !GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) return TRUE;
			const bool native = wcscmp(className, L"#32770") == 0;
			constexpr wchar_t fluentPrefix[] = L"Inkeys.FluentMessageBox.";
			const bool fluent = wcsncmp(className, fluentPrefix, std::size(fluentPrefix) - 1) == 0;
			if (!native && !fluent) return TRUE;
			// 只确认仍属精确存活child的提示；不发布全局按键、不关闭其它产品窗口。
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid != dismissal.childPid || !IsWindowVisible(hwnd)) return TRUE;
			const bool posted = native
				? PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0) != FALSE
				: PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0) != FALSE;
			if (!posted) return TRUE;
			dismissal.prompt = hwnd;
			dismissal.postedTick = GetTickCount64();
			return FALSE;
		}

		[[nodiscard]] bool RunStartupFailureProcessTest(StartupFailureSite site,
			bool wrongParentIdentity = false, bool naturalExit = false)
		{
			const wchar_t* option = StartupFailureOption(site);
			if (!option) return false;
			std::wstring directory, copiedImage, launcherImage;
			if (!MakeUefTestDirectory(directory, copiedImage)
				|| !CurrentImagePath(launcherImage)) return false;
			HANDLE inheritedParent = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
				&inheritedParent, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
			Handle parent(inheritedParent);
			Handle acknowledge(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			HANDLE inheritedAcknowledge = nullptr;
			if (!acknowledge.Valid() || !DuplicateHandle(GetCurrentProcess(), acknowledge.value,
				GetCurrentProcess(), &inheritedAcknowledge, EVENT_MODIFY_STATE, TRUE, 0)) return false;
			Handle childAcknowledge(inheritedAcknowledge);
			Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
				0, sizeof(StartupFailureObservation), nullptr));
			if (!mapping.Valid()) return false;
			MappedView observationView(MapViewOfFile(mapping.value, FILE_MAP_WRITE,
				0, 0, sizeof(StartupFailureObservation)));
			if (!observationView.value) return false;
			auto* observation = static_cast<StartupFailureObservation*>(observationView.value);
			memset(observation, 0, sizeof(*observation));
			observation->magic = kStartupFailureObservationMagic;
			observation->version = 1;
			observation->bytes = sizeof(*observation);
			observation->site = static_cast<DWORD>(site);
			observation->expectedFailureCode = StartupFailureCode(site);
			observation->testMode = naturalExit ? 1u : 0u;
			Handle childMapping;
			if (!DuplicateHandle(GetCurrentProcess(), mapping.value, GetCurrentProcess(),
				&childMapping.value, FILE_MAP_WRITE, TRUE, 0)) return false;
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
			HANDLE inherited[] = { parent.value, childAcknowledge.value, childMapping.value };
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
				PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) return false;
			const std::wstring arguments = std::wstring(kStartupFailureChildMode)
				+ L" " + std::to_wstring(GetCurrentProcessId())
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(parent.value))
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childAcknowledge.value))
				+ L" " + Quote(directory) + L" " + option + L" "
				+ Quote(wrongParentIdentity ? copiedImage : launcherImage)
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childMapping.value));
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(copiedImage, arguments, true,
				EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, &startup, process)) return false;
			Handle child(process.hProcess), childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			const HANDLE signals[] = { acknowledge.value, child.value };
			const bool authorized = WaitForMultipleObjects(2, signals, FALSE, 5000) == WAIT_OBJECT_0;
			const ULONGLONG observedStart = GetTickCount64();
			StartupPromptDismissal dismissal{ process.dwProcessId };
			bool dead = false;
			if (naturalExit && !wrongParentIdentity)
			{
				// HANDLE尚未signaled时PID不会复用；确认消息只送本轮child的真实提示。
				while (GetTickCount64() - observedStart < 25000u)
				{
					const DWORD wait = WaitForSingleObject(child.value, 50);
					if (wait == WAIT_OBJECT_0) { dead = true; break; }
					if (wait != WAIT_TIMEOUT) break;
					if (!dismissal.prompt)
						EnumWindows(ConfirmOwnedStartupPrompt, reinterpret_cast<LPARAM>(&dismissal));
				}
			}
			else dead = WaitForSingleObject(child.value,
				wrongParentIdentity ? 3000u : 25000u) == WAIT_OBJECT_0;
			const ULONGLONG deathTick = GetTickCount64();
			DWORD exitCode = STILL_ACTIVE;
			if (dead) (void)GetExitCodeProcess(child.value, &exitCode);
			const bool failure = InterlockedCompareExchange(&observation->failurePublished, 0, 0) == 1;
			const bool arm = InterlockedCompareExchange(&observation->armPublished, 0, 0) == 1;
			const bool gate = InterlockedCompareExchange(&observation->gatePublished, 0, 0) == 1;
			const bool durable = ExactDurableMarker(MarkerPath(directory, L"durable",
				process.dwProcessId), process.dwProcessId, 1);
			const ULONGLONG armToDeath = deathTick >= observation->armStartedTick
				? deathTick - observation->armStartedTick : 0;
			char line[800]{};
			const int length = sprintf_s(line,
				"shutdown-supervisor startup_old_pid=%lu site=%lu exit_code=%lu dead=%u authorized=%u failure=%u code=%lu real_result=0x%08lX arm=%u arm_state=%lu intent_at_gate=%ld gate=%u boundary=%lu elapsed_ms=%llu deadline=%llu gate_tick=%llu death_tick=%llu durable=%u wrong_identity=%u natural=%u prompt_hwnd=%p prompt_tick=%llu arm_start_tick=%llu arm_to_death_ms=%llu\n",
				process.dwProcessId, static_cast<DWORD>(site), exitCode, dead ? 1u : 0u,
				authorized ? 1u : 0u, failure ? 1u : 0u, observation->expectedFailureCode,
				observation->realInitResult, arm ? 1u : 0u, observation->armState,
				observation->acceptedIntent, gate ? 1u : 0u, observation->boundary,
				deathTick - observedStart, observation->armDeadlineTick,
				observation->gateTick, deathTick, durable ? 1u : 0u,
				wrongParentIdentity ? 1u : 0u, naturalExit ? 1u : 0u,
				dismissal.prompt, dismissal.postedTick, observation->armStartedTick, armToDeath);
			DWORD written = 0;
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			if (length > 0 && output && output != INVALID_HANDLE_VALUE)
				WriteFile(output, line, static_cast<DWORD>(length), &written, nullptr);
			if (!dead)
			{
				// 红测仅清理自己仍持有的精确子进程；外部强杀绝不算产品绿色。
				TerminateProcess(child.value, ERROR_OPERATION_ABORTED);
				WaitForSingleObject(child.value, kDeathWaitMilliseconds);
			}
			DeleteFileW(copiedImage.c_str());
			if (wrongParentIdentity)
				return !authorized && dead && exitCode == 84 && !failure && !gate && !durable;
			if (naturalExit)
				return authorized && failure && gate && arm && dead && durable && dismissal.prompt
					&& observation->acceptedIntent == 1 && observation->armState != 0
					&& observation->armDeadlineTick > observation->armStartedTick
					&& armToDeath < 12000u && dismissal.postedTick >= observation->gateTick
					&& exitCode == (site == StartupFailureSite::Com
						|| site == StartupFailureSite::PptCom ? 1u : 0u);
			return authorized && failure && gate && arm && dead
				&& durable
				&& observation->acceptedIntent == 1 && observation->armState != 0
				&& observation->armDeadlineTick > observation->armStartedTick
				&& armToDeath >= 14000 && armToDeath <= 21000
				&& (exitCode == kFallbackForcedExitCode || exitCode == kForcedExitCode);
		}

		[[nodiscard]] bool RunStartupFailureNoInheritedHandlesCase()
		{
			std::wstring directory, copiedImage, launcherImage;
			if (!MakeUefTestDirectory(directory, copiedImage)
				|| !CurrentImagePath(launcherImage)) return false;
			const std::wstring arguments = std::wstring(kStartupFailureChildMode) + L" "
				+ std::to_wstring(GetCurrentProcessId()) + L" 4 8 " + Quote(directory)
				+ L" D004 " + Quote(launcherImage) + L" 12";
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(copiedImage, arguments, false,
				CREATE_NO_WINDOW, nullptr, process)) return false;
			Handle child(process.hProcess), childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			const bool dead = WaitForSingleObject(child.value, 3000) == WAIT_OBJECT_0;
			DWORD exitCode = STILL_ACTIVE;
			if (dead) (void)GetExitCodeProcess(child.value, &exitCode);
			if (dead) DeleteFileW(copiedImage.c_str());
			PrintCaseObservation(process.dwProcessId, exitCode, 0, 0);
			return dead && exitCode == 83;
		}

		[[nodiscard]] CleanupPrimitiveScenario ParseCleanupScenario(const wchar_t* value) noexcept
		{
			if (!value) return CleanupPrimitiveScenario::None;
			if (wcscmp(value, L"expiry") == 0) return CleanupPrimitiveScenario::Expiry;
			if (wcscmp(value, L"allocation") == 0) return CleanupPrimitiveScenario::Allocation;
			if (wcscmp(value, L"monitor") == 0) return CleanupPrimitiveScenario::Monitor;
			if (wcscmp(value, L"join") == 0) return CleanupPrimitiveScenario::Join;
			if (wcscmp(value, L"ordinary") == 0) return CleanupPrimitiveScenario::Ordinary;
			if (wcscmp(value, L"cancel") == 0) return CleanupPrimitiveScenario::Cancel;
			if (wcscmp(value, L"dormant") == 0) return CleanupPrimitiveScenario::Dormant;
			if (wcscmp(value, L"earlier") == 0) return CleanupPrimitiveScenario::Earlier;
			if (wcscmp(value, L"begin-cancel") == 0) return CleanupPrimitiveScenario::BeginCancel;
			if (wcscmp(value, L"expiry-cancel") == 0) return CleanupPrimitiveScenario::ExpiryCancel;
			return CleanupPrimitiveScenario::None;
		}

		[[nodiscard]] const wchar_t* CleanupScenarioOption(CleanupPrimitiveScenario scenario) noexcept
		{
			switch (scenario)
			{
			case CleanupPrimitiveScenario::Expiry: return L"expiry";
			case CleanupPrimitiveScenario::Allocation: return L"allocation";
			case CleanupPrimitiveScenario::Monitor: return L"monitor";
			case CleanupPrimitiveScenario::Join: return L"join";
			case CleanupPrimitiveScenario::Ordinary: return L"ordinary";
			case CleanupPrimitiveScenario::Cancel: return L"cancel";
			case CleanupPrimitiveScenario::Dormant: return L"dormant";
			case CleanupPrimitiveScenario::Earlier: return L"earlier";
			case CleanupPrimitiveScenario::BeginCancel: return L"begin-cancel";
			case CleanupPrimitiveScenario::ExpiryCancel: return L"expiry-cancel";
			default: return nullptr;
			}
		}

		[[nodiscard]] int RunCleanupPrimitiveChild(int argc, wchar_t* const argv[])
		{
			if (argc != 9 || !IsTestDirectory(argv[5])) return 81;
			const auto scenario = ParseCleanupScenario(argv[6]);
			if (scenario == CleanupPrimitiveScenario::None) return 81;
			unsigned long long pidValue = 0, parentValue = 0, acknowledgeValue = 0, mappingValue = 0;
			if (!ParseUnsigned(argv[2], pidValue) || !pidValue || pidValue > MAXDWORD
				|| !ParseUnsigned(argv[3], parentValue) || !parentValue || parentValue > UINTPTR_MAX
				|| !ParseUnsigned(argv[4], acknowledgeValue) || !acknowledgeValue || acknowledgeValue > UINTPTR_MAX
				|| !ParseUnsigned(argv[8], mappingValue) || !mappingValue || mappingValue > UINTPTR_MAX) return 82;
			const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parentValue));
			const HANDLE acknowledge = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(acknowledgeValue));
			const HANDLE mapping = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(mappingValue));
			DWORD parentFlags = 0, acknowledgeFlags = 0, mappingFlags = 0;
			if (!GetHandleInformation(parent, &parentFlags) || !(parentFlags & HANDLE_FLAG_INHERIT)
				|| !GetHandleInformation(acknowledge, &acknowledgeFlags) || !(acknowledgeFlags & HANDLE_FLAG_INHERIT)
				|| !GetHandleInformation(mapping, &mappingFlags) || !(mappingFlags & HANDLE_FLAG_INHERIT)
				|| GetProcessId(parent) != pidValue || pidValue == GetCurrentProcessId()) return 83;
			std::wstring ownImage, parentImage;
			if (!CurrentImagePath(ownImage) || !ProcessImagePath(parent, parentImage)
				|| !HasExactFailedArmChildIdentity(ownImage, parentImage, argv[5], argv[7])) return 84;
			void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(CleanupPrimitiveObservation));
			if (!view) return 83;
			auto* observation = static_cast<CleanupPrimitiveObservation*>(view);
			if (observation->magic != kCleanupObservationMagic || observation->version != 1
				|| observation->bytes != sizeof(*observation) || observation->scenario != static_cast<DWORD>(scenario)
				|| InterlockedCompareExchange(&observation->authorized, 0, 0) != 0)
			{
				UnmapViewOfFile(view);
				return 83;
			}
			g_cleanupObservation = observation; // 已验真独立信封，映射由进程结束释放。
			observation->childStartTick = GetTickCount64();
			InterlockedExchange(&observation->authorized, 1);
			if (!WriteMarker(MarkerPath(argv[5], L"durable", GetCurrentProcessId()),
				GetCurrentProcessId(), 1, -1, true)) return 89;
			if (!SetEvent(acknowledge)) return 85;
			FailedCleanupTestGates gates{};
			gates.forceStateAllocationFailure = scenario == CleanupPrimitiveScenario::Allocation
				|| scenario == CleanupPrimitiveScenario::Earlier;
			if (scenario == CleanupPrimitiveScenario::Earlier)
			{
				g_cleanupEarlierDeadline = GetTickCount64() + 6000;
				observation->injectedFlag = 1;
			}
			gates.forceMonitorCreationFailure = scenario == CleanupPrimitiveScenario::Monitor;
			Handle monitorEntered, continueMonitor;
			if (scenario == CleanupPrimitiveScenario::Join)
			{
				monitorEntered.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				continueMonitor.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				if (!monitorEntered.Valid() || !continueMonitor.Valid()) return 86;
				g_cleanupMonitorPaused = monitorEntered.value;
				gates.monitorCancelExitEnteredEvent = monitorEntered.value;
				gates.continueMonitorExitEvent = continueMonitor.value;
			}
			Handle raceEntered, continueRace;
			if (scenario == CleanupPrimitiveScenario::BeginCancel || scenario == CleanupPrimitiveScenario::ExpiryCancel)
			{
				raceEntered.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				continueRace.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				if (!raceEntered.Valid() || !continueRace.Valid()) return 86;
				if (scenario == CleanupPrimitiveScenario::BeginCancel)
				{
					gates.afterBeginClaimedEvent = raceEntered.value;
					gates.continueBeginEvent = continueRace.value;
				}
				else
				{
					gates.beforeExpiredClaimedEvent = raceEntered.value;
					gates.continueExpiredEvent = continueRace.value;
				}
			}
			FailedCleanupDeadline scope(&PublishCleanupTestFatal, gates);
			scope.PrepareOrFatal();
			const auto signal = scope.Signal();
			if (scenario == CleanupPrimitiveScenario::BeginCancel)
			{
				g_cleanupConcurrentSignal = signal; // 强引用静态对象，不把scope栈借给测试线程。
				observation->activateTick = GetTickCount64();
				Handle producer(CreateThread(nullptr, 0, BeginCleanupTestSignal, nullptr, 0, nullptr));
				if (!producer.Valid()) return 86;
				const bool entered = WaitForSingleObject(raceEntered.value, 2000) == WAIT_OBJECT_0;
				InterlockedExchange(&observation->monitorPaused, entered ? 1 : 0);
				if (entered) scope.CompleteOrFatal(); // 让Cancel先赢；producer还停在Begin CAS后。
				SetEvent(continueRace.value);
				if (WaitForSingleObject(producer.value, 2000) != WAIT_OBJECT_0)
					for (;;) { TerminateProcess(GetCurrentProcess(), 87); Sleep(1); }
				if (!entered) return 87;
				InterlockedExchange(&observation->afterComplete, 1);
				InterlockedExchange(&observation->cancelWitness,
					signal.BeginKnownFailure() == 0 && g_cleanupConcurrentSignal.BeginKnownFailure() == 0 ? 1 : 0);
				g_cleanupConcurrentSignal = {};
				Sleep(16000);
				return 0;
			}
			if (scenario == CleanupPrimitiveScenario::Allocation || scenario == CleanupPrimitiveScenario::Monitor
				|| scenario == CleanupPrimitiveScenario::Join || scenario == CleanupPrimitiveScenario::Earlier)
			{
				scope.CompleteOrFatal(); // GREEN准备/真实join失败均不能越过此处。
				InterlockedExchange(&observation->afterComplete, 1);
				return 0;
			}
			if (scenario == CleanupPrimitiveScenario::Dormant)
			{
				Sleep(16000); // 尚未失败的初始化没有cold-start timer。
				scope.CompleteOrFatal();
				InterlockedExchange(&observation->afterComplete, 1);
				return 0;
			}
			if (scenario == CleanupPrimitiveScenario::Ordinary)
			{
				SetOffSignal(1);
				observation->existingDeadline = PublishedShutdownDeadlineTick();
			}
			observation->activateTick = GetTickCount64();
			observation->graceDeadline = signal.BeginKnownFailure();
			InterlockedExchange(&observation->activated, 1);
			if (scenario == CleanupPrimitiveScenario::Cancel)
			{
				scope.CompleteOrFatal();
				InterlockedExchange(&observation->afterComplete, 1);
				FailedCleanupDeadline nextScope(&PublishCleanupTestFatal);
				nextScope.PrepareOrFatal();
				const auto nextSignal = nextScope.Signal();
				observation->padding = nextSignal.BeginKnownFailure();
				const bool oldCancelled = signal.BeginKnownFailure() == 0;
				Sleep(250);
				const bool nextUnchanged = nextSignal.BeginKnownFailure() == observation->padding;
				nextScope.CompleteOrFatal();
				InterlockedExchange(&observation->cancelWitness,
					oldCancelled && nextUnchanged && nextSignal.BeginKnownFailure() == 0 ? 1 : 0);
				Sleep(16000); // 两个scope均真join；旧Signal不会激活独立下一代。
				return 0;
			}
			if (scenario == CleanupPrimitiveScenario::ExpiryCancel)
			{
				const bool entered = WaitForSingleObject(raceEntered.value, 17000) == WAIT_OBJECT_0;
				InterlockedExchange(&observation->monitorPaused, entered ? 1 : 0);
				if (!entered) return 88;
				InterlockedExchange(&observation->cancelWitness, 1);
				scope.CompleteOrFatal(); // 时钟已到期，Complete须赢Expired，不能返回下一轮。
				InterlockedExchange(&observation->afterComplete, 1);
				SetEvent(continueRace.value);
				return 0;
			}
			Sleep(250); // 跨明确tick间隔重复Begin，错误重加15秒不能蒙混相同deadline。
			const bool same = signal.BeginKnownFailure() == observation->graceDeadline;
			InterlockedExchange(&observation->cancelWitness, same ? 1 : 0);
			for (;;) Sleep(INFINITE); // 已知失败清理受控停住，RED仅parent上限后清理。
		}

		[[nodiscard]] bool RunCleanupPrimitiveProcessTest(CleanupPrimitiveScenario scenario,
			bool wrongParentIdentity = false, bool omitInheritedHandles = false)
		{
			const wchar_t* option = CleanupScenarioOption(scenario);
			if (!option) return false;
			std::wstring directory, copiedImage, launcherImage;
			if (!MakeUefTestDirectory(directory, copiedImage) || !CurrentImagePath(launcherImage)) return false;
			HANDLE inheritedParent = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &inheritedParent,
				SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
			Handle parent(inheritedParent), acknowledge(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			Handle childAcknowledge, childMapping;
			if (!acknowledge.Valid() || !DuplicateHandle(GetCurrentProcess(), acknowledge.value, GetCurrentProcess(),
				&childAcknowledge.value, EVENT_MODIFY_STATE, TRUE, 0)) return false;
			Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
				sizeof(CleanupPrimitiveObservation), nullptr));
			if (!mapping.Valid()) return false;
			MappedView observationView(MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(CleanupPrimitiveObservation)));
			if (!observationView.value) return false;
			auto* observation = static_cast<CleanupPrimitiveObservation*>(observationView.value);
			memset(observation, 0, sizeof(*observation));
			observation->magic = kCleanupObservationMagic;
			observation->version = 1;
			observation->bytes = sizeof(*observation);
			observation->scenario = static_cast<DWORD>(scenario);
			if (!DuplicateHandle(GetCurrentProcess(), mapping.value, GetCurrentProcess(),
				&childMapping.value, FILE_MAP_WRITE, TRUE, 0)) return false;
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
			HANDLE inherited[] = { parent.value, childAcknowledge.value, childMapping.value };
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				inherited, sizeof(inherited), nullptr, nullptr)) return false;
			const std::wstring arguments = std::wstring(kCleanupChildMode)
				+ L" " + std::to_wstring(GetCurrentProcessId())
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(parent.value))
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childAcknowledge.value))
				+ L" " + Quote(directory) + L" " + option + L" "
				+ Quote(wrongParentIdentity ? copiedImage : launcherImage)
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childMapping.value));
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(copiedImage, arguments, !omitInheritedHandles,
				(omitInheritedHandles ? 0u : EXTENDED_STARTUPINFO_PRESENT) | CREATE_NO_WINDOW,
				omitInheritedHandles ? nullptr : &startup, process)) return false;
			Handle child(process.hProcess), childThread(process.hThread);
			TestChildGuard childGuard(child.value);
			const HANDLE signals[]{ acknowledge.value, child.value };
			const bool authorized = WaitForMultipleObjects(2, signals, FALSE, 5000) == WAIT_OBJECT_0;
			const DWORD budget = wrongParentIdentity || omitInheritedHandles ? 3000u
				: scenario == CleanupPrimitiveScenario::Expiry || scenario == CleanupPrimitiveScenario::ExpiryCancel
				? 40000u : 25000u;
			const bool dead = WaitForSingleObject(child.value, budget) == WAIT_OBJECT_0;
			const ULONGLONG deathTick = GetTickCount64();
			DWORD exitCode = STILL_ACTIVE;
			if (dead) (void)GetExitCodeProcess(child.value, &exitCode);
			const bool activated = InterlockedCompareExchange(&observation->activated, 0, 0) == 1;
			const bool published = InterlockedCompareExchange(&observation->published, 0, 0) == 1;
			const bool durable = ExactDurableMarker(MarkerPath(directory, L"durable", process.dwProcessId), process.dwProcessId, 1);
			char line[850]{};
			const int length = sprintf_s(line,
				"failed-cleanup child_pid=%lu scenario=%lu authorized=%u activated=%u published=%u after_complete=%ld monitor_paused=%ld witness=%ld dead=%u exit_code=%lu child_start=%llu activate=%llu grace_deadline=%llu published_tick=%llu existing_deadline=%llu next_deadline=%llu death_tick=%llu durable=%u wrong_identity=%u no_inherit=%u\n",
				process.dwProcessId, static_cast<DWORD>(scenario), authorized ? 1u : 0u, activated ? 1u : 0u,
				published ? 1u : 0u, observation->afterComplete, observation->monitorPaused, observation->cancelWitness,
				dead ? 1u : 0u, exitCode, observation->childStartTick, observation->activateTick,
				observation->graceDeadline, observation->publishTick, observation->existingDeadline, observation->padding, deathTick,
				durable ? 1u : 0u, wrongParentIdentity ? 1u : 0u, omitInheritedHandles ? 1u : 0u);
			DWORD written = 0;
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			if (length > 0 && output && output != INVALID_HANDLE_VALUE)
				WriteFile(output, line, static_cast<DWORD>(length), &written, nullptr);
			if (!dead)
			{
				TerminateProcess(child.value, ERROR_OPERATION_ABORTED);
				WaitForSingleObject(child.value, kDeathWaitMilliseconds);
			}
			DeleteFileW(copiedImage.c_str());
			if (wrongParentIdentity || omitInheritedHandles)
				return !authorized && dead && exitCode == (wrongParentIdentity ? 84u : 83u)
					&& !activated && !published && !durable;
			if (!authorized || !dead || !durable) return false;
			if (scenario == CleanupPrimitiveScenario::Dormant)
				return exitCode == 0 && observation->afterComplete == 1 && !activated && !published
					&& deathTick - observation->childStartTick >= 16000u;
			if (scenario == CleanupPrimitiveScenario::BeginCancel)
				return exitCode == 0 && observation->afterComplete == 1 && activated && !published
					&& observation->monitorPaused == 1 && observation->cancelWitness == 1
					&& deathTick - observation->activateTick >= 16000u;
			if (scenario == CleanupPrimitiveScenario::Cancel)
				return exitCode == 0 && observation->afterComplete == 1 && activated && !published
					&& observation->cancelWitness == 1 && observation->graceDeadline > observation->activateTick
					&& observation->padding >= observation->graceDeadline
					&& deathTick - observation->activateTick >= 16000u;
			if (scenario == CleanupPrimitiveScenario::Ordinary)
				return activated && observation->afterComplete == 0
					&& (exitCode == kForcedExitCode || exitCode == kFallbackForcedExitCode)
					&& deathTick - observation->activateTick >= 14000u
					&& deathTick - observation->activateTick <= 21000u
					&& observation->existingDeadline > observation->activateTick
					&& deathTick >= observation->existingDeadline
					&& deathTick - observation->existingDeadline <= 6000u;
			if (!published || observation->afterComplete != 0) return false;
			if (scenario == CleanupPrimitiveScenario::Earlier)
				return observation->injectedFlag == 1 && exitCode == 0xE143001Bu
					&& observation->existingDeadline > observation->publishTick
					&& deathTick >= observation->existingDeadline
					&& deathTick - observation->existingDeadline <= 1000u;
			if (scenario == CleanupPrimitiveScenario::Expiry || scenario == CleanupPrimitiveScenario::ExpiryCancel)
				return activated && observation->cancelWitness == 1
					&& observation->graceDeadline >= observation->activateTick + 14990u
					&& observation->graceDeadline <= observation->activateTick + 15500u
					&& observation->publishTick >= observation->graceDeadline && exitCode == 0xE143001Au
					&& (scenario != CleanupPrimitiveScenario::ExpiryCancel || observation->monitorPaused == 1)
					&& deathTick >= observation->graceDeadline + 15000u
					&& deathTick - (observation->graceDeadline + 15000u) <= 6000u;
			if (deathTick < observation->publishTick || deathTick - observation->publishTick < 14000u
				|| deathTick - observation->publishTick > 21000u) return false;
			if (scenario == CleanupPrimitiveScenario::Join)
				return observation->monitorPaused == 1 && exitCode == 0xE143001Cu;
			return exitCode == 0xE143001Bu;
		}

		[[nodiscard]] int RunCleanupPrimitiveTests(CleanupPrimitiveScenario only = CleanupPrimitiveScenario::None)
		{
			bool passed = true;
			const auto check = [&](const char* name, bool result)
			{
				PrintTestResult(name, result);
				passed = passed && result;
			};
			constexpr CleanupPrimitiveScenario cases[]{ CleanupPrimitiveScenario::Expiry,
				CleanupPrimitiveScenario::Allocation, CleanupPrimitiveScenario::Monitor, CleanupPrimitiveScenario::Join,
				CleanupPrimitiveScenario::Ordinary, CleanupPrimitiveScenario::Cancel, CleanupPrimitiveScenario::Dormant,
				CleanupPrimitiveScenario::Earlier, CleanupPrimitiveScenario::BeginCancel, CleanupPrimitiveScenario::ExpiryCancel };
			for (const auto scenario : cases)
			{
				if (only != CleanupPrimitiveScenario::None && only != scenario) continue;
				check(scenario == CleanupPrimitiveScenario::Expiry ? "cleanup-expiry"
					: scenario == CleanupPrimitiveScenario::Allocation ? "cleanup-allocation"
					: scenario == CleanupPrimitiveScenario::Monitor ? "cleanup-monitor-create"
					: scenario == CleanupPrimitiveScenario::Join ? "cleanup-real-cancel-join"
					: scenario == CleanupPrimitiveScenario::Ordinary ? "cleanup-preserves-ordinary"
					: scenario == CleanupPrimitiveScenario::Cancel ? "cleanup-cancel-old-signal"
					: scenario == CleanupPrimitiveScenario::Earlier ? "cleanup-publisher-earlier-deadline"
					: scenario == CleanupPrimitiveScenario::BeginCancel ? "cleanup-begin-cas-cancel"
					: scenario == CleanupPrimitiveScenario::ExpiryCancel ? "cleanup-expiry-cas-complete" : "cleanup-dormant",
					RunCleanupPrimitiveProcessTest(scenario));
			}
			check("cleanup-wrong-parent-rejected", RunCleanupPrimitiveProcessTest(CleanupPrimitiveScenario::Dormant, true));
			check("cleanup-no-inherited-handles", RunCleanupPrimitiveProcessTest(CleanupPrimitiveScenario::Dormant, false, true));
			return passed ? 0 : 64;
		}

		struct CleanupRealOption { CleanupRealCase scenario; const wchar_t* option; };
		constexpr CleanupRealOption kCleanupRealOptions[]{
			{ CleanupRealCase::PresenterHold, L"C03-presenter-hold" },
			{ CleanupRealCase::PresenterRelease, L"C03-presenter-release" },
			{ CleanupRealCase::WindowBeforeHold, L"C05-before-hold" },
			{ CleanupRealCase::WindowBeforeRelease, L"C05-before-release" },
			{ CleanupRealCase::WindowCreatedHold, L"C05-created-hold" },
			{ CleanupRealCase::WindowCreatedRelease, L"C05-created-release" },
			{ CleanupRealCase::MainUlw, L"C07-main-ulw" },
			{ CleanupRealCase::RenderHold, L"C08-render-hold" },
			{ CleanupRealCase::RenderRelease, L"C08-render-release" },
			{ CleanupRealCase::DesktopHold, L"C09-desktop-hold" },
			{ CleanupRealCase::DesktopRelease, L"C09-desktop-release" },
			{ CleanupRealCase::PptHold, L"C10-ppt-hold" },
			{ CleanupRealCase::PptRelease, L"C10-ppt-release" },
			{ CleanupRealCase::NaturalClose, L"C11-natural-close" },
			{ CleanupRealCase::ReadDesktopCommitted, L"C09-read-committed" },
			{ CleanupRealCase::ReadPptCommitted, L"C10-read-committed" },
			{ CleanupRealCase::ReadPptForeignSession, L"C10-read-foreign-session" }
		};

		CleanupRealCase ParseCleanupRealCase(const wchar_t* option) noexcept
		{
			if (option) for (const auto& entry : kCleanupRealOptions)
				if (wcscmp(option, entry.option) == 0) return entry.scenario;
			return CleanupRealCase::None;
		}

		const wchar_t* CleanupRealCaseOption(CleanupRealCase scenario) noexcept
		{
			for (const auto& entry : kCleanupRealOptions)
				if (scenario == entry.scenario) return entry.option;
			return nullptr;
		}

		bool IsCleanupRealReader(CleanupRealCase scenario) noexcept
		{
			return scenario >= CleanupRealCase::ReadDesktopCommitted
				&& scenario <= CleanupRealCase::ReadPptForeignSession;
		}

		bool AllZeroCleanupBytes(const void* data, size_t size) noexcept
		{
			const auto* bytes = static_cast<const BYTE*>(data);
			for (size_t i = 0; i != size; ++i) if (bytes[i]) return false;
			return true;
		}

		bool ValidCleanupReceipt(const CleanupRealReceipt& receipt, bool desktop) noexcept
		{
			if (AllZeroCleanupBytes(receipt.fileGuid, 16)
				|| AllZeroCleanupBytes(receipt.workspaceGuid, 16)
				|| AllZeroCleanupBytes(receipt.pageGuid, 16)
				|| !AllZeroCleanupBytes(receipt.reserved, sizeof(receipt.reserved))
				|| !receipt.uinkLength || receipt.uinkLength > 8u * 1024u * 1024u
				|| !receipt.strokeCount || receipt.strokeCount > 32 || !receipt.pointCount || receipt.pointCount > 4096
				|| !receipt.activeCanvasCount || receipt.activeCanvasCount > 8
				|| receipt.retainedCanvasCount > 16 || receipt.deviceCount > 4
				|| receipt.totalPages == 0 || receipt.totalPages > 3
				|| receipt.pageIndex > receipt.totalPages || receipt.bindingMode > 4
				|| receipt.pageKind > 4 || receipt.processLocalIdentity > 1 || receipt.desktopTrigger > 1
				|| AllZeroCleanupBytes(receipt.indexSha256, 32)
				|| AllZeroCleanupBytes(receipt.uinkSha256, 32)
				|| AllZeroCleanupBytes(receipt.geometrySha256, 32)) return false;
			// 日期与session都是固定ASCII字段；禁止截断、非canonical尾部和隐藏附加路径。
			const auto canonical = [](const char* text, size_t capacity, size_t length) noexcept
			{
				if (length >= capacity || text[length] != '\0') return false;
				for (size_t i = length; i != capacity; ++i) if (text[i]) return false;
				return true;
			};
			if (!canonical(receipt.storageSession, 40, 36)) return false;
			bool nonzeroSession = false;
			for (size_t i = 0; i != 36; ++i)
			{
				const char value = receipt.storageSession[i];
				if (i == 8 || i == 13 || i == 18 || i == 23) { if (value != '-') return false; }
				else
				{
					if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
					if (value != '0') nonzeroSession = true;
				}
			}
			if (!nonzeroSession) return false;
			if (!AllZeroCleanupBytes(receipt.localDate, 16))
			{
				if (!canonical(receipt.localDate, 16, 10)) return false;
				for (size_t i = 0; i != 10; ++i)
					if (i == 4 || i == 7) { if (receipt.localDate[i] != '-') return false; }
					else if (receipt.localDate[i] < '0' || receipt.localDate[i] > '9') return false;
			}
			else if (!AllZeroCleanupBytes(receipt.localDate, 16)) return false;
			if (desktop)
			{
				if (receipt.workspaceType != 0 || receipt.pageIndex != 0 || receipt.totalPages != 1
					|| receipt.bindingMode || receipt.pageKind || receipt.processLocalIdentity
					|| receipt.mutationRevision || receipt.bindingRevision || receipt.targetRevision || receipt.sessionRevision
					|| !receipt.sequenceInSession || !receipt.dailySequence || receipt.activeCanvasCount != 1
					|| receipt.retainedCanvasCount || !AllZeroCleanupBytes(receipt.presentationKey, 16)
					|| !AllZeroCleanupBytes(receipt.slideIds, sizeof(receipt.slideIds))
					|| !canonical(receipt.localDate, 16, 10)) return false;
				const auto digit = [&](size_t index) noexcept { return static_cast<unsigned>(receipt.localDate[index] - '0'); };
				const unsigned year = digit(0) * 1000 + digit(1) * 100 + digit(2) * 10 + digit(3);
				const unsigned month = digit(5) * 10 + digit(6), day = digit(8) * 10 + digit(9);
				if (!year || month < 1 || month > 12 || !day) return false;
				constexpr unsigned days[]{ 31,28,31,30,31,30,31,31,30,31,30,31 };
				const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
				if (day > days[month - 1] + (month == 2 && leap ? 1u : 0u)) return false;
			}
			else
			{
				// 首批仅两张固定StableSlideId和独立EndScreen，拒绝未知enum/另一页拓扑。
				if (receipt.workspaceType != 2 || receipt.bindingMode != 0 || receipt.pageKind > 1
					|| receipt.processLocalIdentity || receipt.totalPages != 2 || receipt.activeCanvasCount != 3
					|| receipt.retainedCanvasCount || !receipt.mutationRevision || receipt.bindingRevision != 1
					|| !receipt.targetRevision || receipt.dailySequence || receipt.sequenceInSession || receipt.desktopTrigger
					|| receipt.slideIds[0] != 601 || receipt.slideIds[1] != 602 || receipt.slideIds[2] || receipt.slideIds[3]
					|| AllZeroCleanupBytes(receipt.presentationKey, 16) || !AllZeroCleanupBytes(receipt.localDate, 16)
					|| (receipt.pageKind == 0 ? receipt.pageIndex >= receipt.totalPages : receipt.pageIndex != receipt.totalPages)) return false;
			}
			return true;
		}

		struct CleanupRealPathLeases
		{
			std::vector<HANDLE> files;
			~CleanupRealPathLeases() { for (HANDLE file : files) CloseHandle(file); }
		};

		bool CanonicalCleanupPath(const std::wstring& path, std::wstring& full)
		{
			full.assign(32768, L'\0');
			const DWORD size = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
			if (!size || size >= full.size()) return false;
			full.resize(size);
			return full.size() >= 3 && full[1] == L':' && full[2] == L'\\';
		}

		bool LeaseCleanupPath(const std::wstring& full, bool directory, CleanupRealPathLeases& leases)
		{
			// 每层以OPEN_REPARSE_POINT打开并禁止delete共享，检验后不能被换成另一目录/链接。
			for (size_t end = 3;;)
			{
				const bool leaf = end == full.size();
				Handle file(CreateFileW(full.substr(0, end).c_str(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
				BY_HANDLE_FILE_INFORMATION info{};
				if (!file.Valid() || !GetFileInformationByHandle(file.value, &info)
					|| (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
					|| (!!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != (!leaf || directory))) return false;
				leases.files.push_back(file.value);
				file.value = nullptr;
				if (leaf) return true;
				const size_t next = full.find(L'\\', end + (end == 3 ? 0 : 1));
				end = next == std::wstring::npos ? full.size() : next;
				if (end < 3 || end > full.size() || leases.files.size() > 256) return false;
			}
		}

		int RunCleanupRealChild(int argc, wchar_t* const argv[])
		{
			if (argc != 9 || !IsTestDirectory(argv[5])) return 81;
			const auto scenario = ParseCleanupRealCase(argv[6]);
			if (scenario == CleanupRealCase::None) return 81;
			unsigned long long pidValue = 0, parentValue = 0, ackValue = 0, mappingValue = 0;
			if (!ParseUnsigned(argv[2], pidValue) || !pidValue || pidValue > MAXDWORD
				|| !ParseUnsigned(argv[3], parentValue) || !parentValue || parentValue > UINTPTR_MAX
				|| !ParseUnsigned(argv[4], ackValue) || !ackValue || ackValue > UINTPTR_MAX
				|| !ParseUnsigned(argv[8], mappingValue) || !mappingValue || mappingValue > UINTPTR_MAX) return 82;
			const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parentValue));
			const HANDLE ack = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(ackValue));
			const HANDLE mapping = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(mappingValue));
			for (HANDLE handle : { parent, ack, mapping })
			{
				DWORD flags = 0;
				if (!GetHandleInformation(handle, &flags) || !(flags & HANDLE_FLAG_INHERIT)) return 83;
			}
			if (GetProcessId(parent) != pidValue || pidValue == GetCurrentProcessId()) return 83;
			std::wstring ownImage, parentImage, directory, source;
			if (!CurrentImagePath(ownImage) || !ProcessImagePath(parent, parentImage)
				|| !HasExactFailedArmChildIdentity(ownImage, parentImage, argv[5], argv[7])
				|| !CanonicalCleanupPath(argv[5], directory) || !CanonicalCleanupPath(parentImage, source)) return 84;
			const std::wstring marker = L"\\TestResults\\release-hardening\\";
			const auto markerAt = directory.rfind(marker);
			if (markerAt == std::wstring::npos) return 84;
			const std::wstring repositoryPrefix = directory.substr(0, markerAt) + L"\\";
			if (source.size() <= repositoryPrefix.size() || CompareStringOrdinal(source.c_str(),
				static_cast<int>(repositoryPrefix.size()), repositoryPrefix.c_str(),
				static_cast<int>(repositoryPrefix.size()), TRUE) != CSTR_EQUAL) return 84;
			CleanupRealPathLeases leases;
			std::wstring copied;
			if (!CanonicalCleanupPath(directory + L"\\bin\\Inkeys.exe", copied)
				|| !LeaseCleanupPath(directory, true, leases) || !LeaseCleanupPath(copied, false, leases)
				|| !LeaseCleanupPath(source, false, leases)) return 84;
			MappedView view(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(CleanupRealPacket)));
			if (!view.value) return 83;
			auto* packet = static_cast<CleanupRealPacket*>(view.value);
			const auto& header = packet->header;
			const bool pptReader = scenario == CleanupRealCase::ReadPptCommitted || scenario == CleanupRealCase::ReadPptForeignSession;
			CleanupRealTrace initialTrace{};
			memcpy(&initialTrace, &packet->trace, sizeof(initialTrace));
			// 旧HWND仅用于重构bindingToken，绝不赋予对已死进程窗口的操作权。
			if (pptReader)
			{
				if (!initialTrace.oldDrawpad || initialTrace.oldDrawpad > UINTPTR_MAX) return 83;
				initialTrace.oldDrawpad = 0;
			}
			if (header.magic != kCleanupRealMagic || header.version != kCleanupRealVersion
				|| header.bytes != sizeof(*packet) || header.scenario != static_cast<DWORD>(scenario)
				|| header.issuerParentPid != pidValue || header.expectedIntent != 1 || header.reservedFlags
				|| header.authorized || header.result || header.stage || header.error
				|| AllZeroCleanupBytes(header.nonce, 16) || !AllZeroCleanupBytes(&initialTrace, sizeof(initialTrace))
				|| !AllZeroCleanupBytes(&packet->observed, sizeof(packet->observed))
				|| (IsCleanupRealReader(scenario)
					? !header.originChildPid || header.originChildPid == GetCurrentProcessId() || !ValidCleanupReceipt(packet->expected, scenario == CleanupRealCase::ReadDesktopCommitted)
					: header.originChildPid || !AllZeroCleanupBytes(&packet->expected, sizeof(packet->expected)))) return 83;
			g_cleanupRealObservation = packet;
			g_cleanupRealCase = scenario;
			g_cleanupRealDirectory = directory;
			g_cleanupRealFatalRecorded.store(false, std::memory_order_release);
			packet->trace.childStartTick = GetTickCount64();
			InterlockedExchange(&packet->header.authorized, 1);
			PublishCleanupRealStage(*packet, CleanupRealStage::Authorized);
			if (!SetEvent(ack)) return 85;
			const VerifiedCleanupRealLaunch launch{ scenario, packet, directory };
			const int result = scenario == CleanupRealCase::MainUlw
				? RunMainFailedCleanupUlwCounterexample(launch) : RunAuthorizedFailedCleanupRealFixture(launch);
			// 夹具只能在全部真实owner/monitor/辅助线程join后返回；hold保留view/lease到死亡。
			g_cleanupRealObservation = nullptr;
			g_cleanupRealCase = CleanupRealCase::None;
			g_cleanupRealDirectory.clear();
			return result;
		}

		struct CleanupRealRun
		{
			std::wstring directory, copiedImage, launcherImage;
			GUID nonce{};
		};

		bool RunCleanupRealProcess(CleanupRealRun& run, CleanupRealCase scenario,
			CleanupRealReceipt* observed = nullptr, DWORD* observedPid = nullptr,
			const CleanupRealReceipt* expected = nullptr, DWORD originPid = 0, unsigned negative = 0,
			ULONGLONG originDrawpad = 0, ULONGLONG* observedDrawpad = nullptr)
		{
			HANDLE inheritedParent = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &inheritedParent,
				SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
			Handle parent(inheritedParent), ack(CreateEventW(nullptr, TRUE, FALSE, nullptr)), childAck, childMapping;
			if (!ack.Valid() || !DuplicateHandle(GetCurrentProcess(), ack.value, GetCurrentProcess(),
				&childAck.value, EVENT_MODIFY_STATE, TRUE, 0)) return false;
			Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(CleanupRealPacket), nullptr));
			if (!mapping.Valid()) return false;
			MappedView view(MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(CleanupRealPacket)));
			if (!view.value) return false;
			auto* packet = static_cast<CleanupRealPacket*>(view.value);
			memset(packet, 0, sizeof(*packet));
			packet->header.magic = negative == 3 ? kCleanupObservationMagic : kCleanupRealMagic;
			packet->header.version = kCleanupRealVersion;
			packet->header.bytes = negative == 3 ? sizeof(CleanupPrimitiveObservation) : sizeof(*packet);
			packet->header.scenario = static_cast<DWORD>(scenario);
			packet->header.issuerParentPid = GetCurrentProcessId();
			packet->header.originChildPid = originPid;
			packet->header.expectedIntent = 1;
			memcpy(packet->header.nonce, &run.nonce, 16);
			if (expected) packet->expected = *expected;
			if (scenario == CleanupRealCase::ReadPptCommitted || scenario == CleanupRealCase::ReadPptForeignSession)
				packet->trace.oldDrawpad = originDrawpad;
			if (!DuplicateHandle(GetCurrentProcess(), mapping.value, GetCurrentProcess(),
				&childMapping.value, FILE_MAP_WRITE, TRUE, 0)) return false;
			SIZE_T bytes = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
			AttributeList attributes;
			attributes.memory = HeapAlloc(GetProcessHeap(), 0, bytes);
			if (!attributes.memory) return false;
			STARTUPINFOEXW startup{};
			startup.StartupInfo.cb = sizeof(startup);
			startup.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.memory);
			if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return false;
			attributes.initialized = true;
			HANDLE handles[]{ parent.value, childAck.value, childMapping.value };
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				handles, sizeof(handles), nullptr, nullptr)) return false;
			const wchar_t* option = negative == 4 ? L"C00-unapproved" : CleanupRealCaseOption(scenario);
			if (!option) return false;
			const auto arguments = std::wstring(kCleanupRealChildMode) + L" " + std::to_wstring(GetCurrentProcessId())
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(parent.value))
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childAck.value))
				+ L" " + Quote(run.directory) + L" " + option + L" "
				+ Quote(negative == 1 ? run.copiedImage : run.launcherImage)
				+ L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childMapping.value));
			PROCESS_INFORMATION process{};
			if (!StartSameExecutable(run.copiedImage, arguments, negative != 2,
				CREATE_NO_WINDOW | (negative == 2 ? 0u : EXTENDED_STARTUPINFO_PRESENT),
				negative == 2 ? nullptr : &startup, process)) return false;
			Handle child(process.hProcess), thread(process.hThread);
			TestChildGuard guard(child.value);
			const HANDLE signals[]{ ack.value, child.value };
			const bool authorized = WaitForMultipleObjects(2, signals, FALSE, 5000) == WAIT_OBJECT_0;
			const ULONGLONG waitStart = GetTickCount64();
			bool dead = false;
			for (;;)
			{
				if (WaitForSingleObject(child.value, 50) == WAIT_OBJECT_0) { dead = true; break; }
				const ULONGLONG now = GetTickCount64();
				const LONG stage = InterlockedCompareExchange(&packet->header.stage, 0, 0);
				const bool ordinary = scenario >= CleanupRealCase::RenderHold && scenario <= CleanupRealCase::NaturalClose;
				const ULONGLONG close = stage >= static_cast<LONG>(CleanupRealStage::ClosePublished)
					? packet->trace.closeRequestTick : 0;
				const ULONGLONG budget = negative ? 3000u : IsCleanupRealReader(scenario) ? 20000u : 45000u;
				if (now - waitStart > budget || (!negative && ordinary
					&& ((close && now - close > 25000u) || (!close && now - waitStart > 20000u)))) break;
			}
			const ULONGLONG death = GetTickCount64();
			DWORD exitCode = STILL_ACTIVE;
			if (dead) (void)GetExitCodeProcess(child.value, &exitCode);
			const LONG stage = InterlockedCompareExchange(&packet->header.stage, 0, 0);
			const auto& trace = packet->trace;
			char line[1200]{};
			const int length = sprintf_s(line,
				"cleanup-real child_pid=%lu case=%lu authorized=%u dead=%u exit=%lu stage=%ld result=%ld error=%ld negative=%u start=%llu failure=%llu gate=%llu grace=%llu close=%llu deadline=%llu fatal=%llu death=%llu start_returned=%ld stop_returned=%ld cancelled=%ld old_destroyed=%ld intent=%ld arm=%ld reader=%ld generation=%llu/%llu presents=%llu/%llu/%llu save=%llu/%llu/%llu/%llu\n",
				process.dwProcessId, static_cast<DWORD>(scenario), authorized ? 1u : 0u, dead ? 1u : 0u,
				exitCode, stage, packet->header.result, packet->header.error, negative,
				trace.childStartTick, trace.failureTick, trace.gateTick, trace.graceDeadline, trace.closeRequestTick,
				trace.ordinaryDeadline, trace.fatalPublishTick, death, trace.startReturned, trace.stopReturned,
				trace.oldSignalCancelled, trace.oldChainDestroyed, trace.intentObserved, trace.armState, trace.readerSucceeded,
				trace.oldGeneration, trace.newGeneration, trace.baselineSuccessPresents, trace.gateSuccessPresents,
				trace.afterGraceSuccessPresents, trace.saveAccepted, trace.saveCommitted, trace.saveFailed, trace.savePending);
			DWORD written = 0;
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			if (length > 0 && output && output != INVALID_HANDLE_VALUE)
				WriteFile(output, line, static_cast<DWORD>(length), &written, nullptr);
			if (!dead) return false; // guard仅清本轮HANDLE，永不把parent kill计为自然产品退出。
			if (negative)
				return !authorized && packet->header.authorized == 0 && stage == 0
					&& AllZeroCleanupBytes(&packet->trace, sizeof(trace))
					&& GetFileAttributesW((run.directory + L"\\artifacts").c_str()) == INVALID_FILE_ATTRIBUTES
					&& exitCode == (negative == 1 ? 84u : negative == 4 ? 81u : 83u);
			// 完整原始包在child已死亡后create-new保存，包含全部receipt摘要，不读半写帧。
			const auto artifact = run.directory + L"\\packet-" + std::to_wstring(process.dwProcessId) + L".bin";
			Handle file(CreateFileW(artifact.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!file.Valid() || !WriteFile(file.value, packet, sizeof(*packet), &written, nullptr)
				|| written != sizeof(*packet) || !FlushFileBuffers(file.value)) return false;
			if (!authorized || packet->header.authorized != 1 || packet->header.error != 0) return false;
			if (observed) *observed = packet->observed;
			if (observedPid) *observedPid = process.dwProcessId;
			if (observedDrawpad) *observedDrawpad = trace.oldDrawpad;
			const bool startupHold = scenario == CleanupRealCase::PresenterHold
				|| scenario == CleanupRealCase::WindowBeforeHold || scenario == CleanupRealCase::WindowCreatedHold;
			const bool ordinaryHold = scenario == CleanupRealCase::RenderHold
				|| scenario == CleanupRealCase::DesktopHold || scenario == CleanupRealCase::PptHold;
			if (startupHold)
				return exitCode == 0xE143001Au && trace.faultReached == 1 && trace.startReturned == 0
					&& trace.stopReturned == 0 && trace.failureTick && trace.gateTick >= trace.failureTick
					&& trace.graceDeadline >= trace.failureTick + 14990u && trace.fatalPublishTick >= trace.graceDeadline
					&& death >= trace.graceDeadline + 15000u && death - (trace.graceDeadline + 15000u) <= 2000u
					&& trace.intentObserved == 1;
			if (ordinaryHold)
				return (exitCode == kForcedExitCode || exitCode == kFallbackForcedExitCode) && trace.faultReached == 1
					&& trace.startReturned == 1 && trace.stopReturned == 0 && trace.stopEnterTick >= trace.closeRequestTick
					&& trace.gateTick && trace.closeRequestTick >= trace.gateTick && trace.closeRequestTick - trace.gateTick <= 2000u
					&& trace.ordinaryDeadline > trace.closeRequestTick && death >= trace.ordinaryDeadline
					&& death - trace.ordinaryDeadline <= 2000u && trace.intentObserved == 1
					&& (trace.armState == 2 || trace.armState == 4) && trace.gateSuccessPresents > trace.baselineSuccessPresents;
			if (exitCode != 0 || packet->header.result != 1 || stage != static_cast<LONG>(CleanupRealStage::Finished)) return false;
			if (IsCleanupRealReader(scenario))
				return trace.readerSucceeded == 1 && (scenario == CleanupRealCase::ReadPptForeignSession
					|| (ValidCleanupReceipt(packet->observed, scenario == CleanupRealCase::ReadDesktopCommitted) && expected
						&& memcmp(&packet->observed, expected, sizeof(*expected)) == 0));
			if (scenario == CleanupRealCase::PresenterRelease || scenario == CleanupRealCase::WindowBeforeRelease
				|| scenario == CleanupRealCase::WindowCreatedRelease)
				return trace.startReturned == 1 && trace.stopReturned == 1 && trace.oldSignalCancelled == 1
					&& trace.oldChainDestroyed == 1 && trace.faultReached == 1 && !trace.fatalPublishTick
					&& trace.intentObserved == 0 && trace.graceDeadline && death >= trace.graceDeadline + 1000u;
			if (scenario == CleanupRealCase::MainUlw)
				return trace.firstStartFalseTick && trace.oldJoinTick >= trace.firstStartFalseTick && trace.oldChainDestroyed == 1
					&& trace.oldSignalCancelled == 1 && trace.newGeneration > trace.oldGeneration
					&& trace.firstSuccessTick >= trace.oldJoinTick && trace.postGraceStrokeTick >= trace.graceDeadline + 1000u
					&& trace.afterGraceSuccessPresents > trace.baselineSuccessPresents && trace.stopReturned == 1;
			return trace.startReturned == 1 && trace.stopReturned == 1 && trace.intentObserved == 1
				&& (trace.armState == 2 || trace.armState == 4) && trace.ordinaryDeadline > trace.closeRequestTick;
		}

		int RunCleanupRealTests(CleanupRealCase scenario)
		{
			if (scenario == CleanupRealCase::None || IsCleanupRealReader(scenario)) return 71;
			bool passed = true;
			for (unsigned negative = 1; negative <= 4; ++negative)
			{
				CleanupRealRun run;
				const bool ready = MakeUefTestDirectory(run.directory, run.copiedImage)
					&& CurrentImagePath(run.launcherImage) && SUCCEEDED(CoCreateGuid(&run.nonce));
				const bool result = ready && RunCleanupRealProcess(run, CleanupRealCase::NaturalClose, nullptr, nullptr, nullptr, 0, negative);
				PrintTestResult(negative == 1 ? "cleanup-real-wrong-image" : negative == 2 ? "cleanup-real-no-inherit"
					: negative == 3 ? "cleanup-real-old-packet" : "cleanup-real-unapproved-case", result);
				passed = passed && result;
				if (ready) DeleteFileW(run.copiedImage.c_str());
			}
			if (!passed) return 65; // 授权合同不成立时不进入新的真实fault/窗口场景。
			CleanupRealRun run;
			if (!MakeUefTestDirectory(run.directory, run.copiedImage) || !CurrentImagePath(run.launcherImage)
				|| FAILED(CoCreateGuid(&run.nonce))) return 65;
			CleanupRealReceipt observed{};
			DWORD producerPid = 0;
			ULONGLONG producerDrawpad = 0;
			passed = RunCleanupRealProcess(run, scenario, &observed, &producerPid, nullptr, 0, 0, 0, &producerDrawpad);
			PrintTestResult("cleanup-real-product-module", passed);
			const bool desktop = scenario == CleanupRealCase::DesktopHold || scenario == CleanupRealCase::DesktopRelease
				|| scenario == CleanupRealCase::NaturalClose;
			const bool ppt = scenario == CleanupRealCase::PptHold || scenario == CleanupRealCase::PptRelease;
			if (passed && (desktop || ppt))
			{
				const bool receiptValid = ValidCleanupReceipt(observed, desktop);
				const bool readable = receiptValid && RunCleanupRealProcess(run,
					desktop ? CleanupRealCase::ReadDesktopCommitted : CleanupRealCase::ReadPptCommitted,
					nullptr, nullptr, &observed, producerPid, 0, producerDrawpad);
				PrintTestResult("cleanup-real-fresh-committed-reader", readable);
				passed = passed && readable;
				if (ppt)
				{
					const bool foreign = receiptValid && RunCleanupRealProcess(run, CleanupRealCase::ReadPptForeignSession,
						nullptr, nullptr, &observed, producerPid, 0, producerDrawpad);
					PrintTestResult("cleanup-real-foreign-session-rejected", foreign);
					passed = passed && foreign;
				}
			}
			DeleteFileW(run.copiedImage.c_str()); // reader阶段完毕；仅本次copy，不清孤儿版本或未知数据。
			return passed ? 0 : 65;
		}

		[[nodiscard]] int RunStartupFailureTests(StartupFailureSite only = StartupFailureSite::None,
			bool naturalExit = false)
		{
			bool passed = true;
			auto check = [&](const char* name, bool result)
			{
				PrintTestResult(name, result);
				passed = passed && result;
			};
			if (only == StartupFailureSite::None || only == StartupFailureSite::Com)
				check(naturalExit ? "startup-d004-natural-main" : "startup-d004-actual-main",
					RunStartupFailureProcessTest(StartupFailureSite::Com, false, naturalExit));
			if (only == StartupFailureSite::None || only == StartupFailureSite::PptCom)
				check(naturalExit ? "startup-d005-natural-main" : "startup-d005-actual-main",
					RunStartupFailureProcessTest(StartupFailureSite::PptCom, false, naturalExit));
			if (only == StartupFailureSite::None || only == StartupFailureSite::Font)
				check(naturalExit ? "startup-d003-natural-main" : "startup-d003-actual-main",
					RunStartupFailureProcessTest(StartupFailureSite::Font, false, naturalExit));
			if (only == StartupFailureSite::None || only == StartupFailureSite::BarState)
				check(naturalExit ? "startup-b002-natural-main" : "startup-b002-actual-main",
					RunStartupFailureProcessTest(StartupFailureSite::BarState, false, naturalExit));
			check("startup-wrong-parent-rejected",
				RunStartupFailureProcessTest(StartupFailureSite::Com, true));
			check("startup-no-inherited-handles", RunStartupFailureNoInheritedHandlesCase());
			return passed ? 0 : 63;
		}

		[[nodiscard]] int RunNoGuiTests(bool failedArmOnly = false)
		{
			if (failedArmOnly) return RunFailedArmTests() ? 0 : 62;
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
			check("failed-arm-cases", RunFailedArmTests());
			return passed ? 0 : 62;
		}
	}

	bool IsAuthorizedCleanupRealLaunch(const VerifiedCleanupRealLaunch& launch) noexcept
	{
		return launch.packet && launch.packet == g_cleanupRealObservation
			&& launch.scenario == g_cleanupRealCase && launch.scenario != CleanupRealCase::None
			&& EqualNoCase(launch.directory.c_str(), g_cleanupRealDirectory.c_str())
			&& InterlockedCompareExchange(&launch.packet->header.authorized, 0, 0) == 1;
	}

	void PublishCleanupRealStage(CleanupRealPacket& packet, CleanupRealStage stage) noexcept
	{
		const LONG desired = static_cast<LONG>(stage);
		LONG current = InterlockedCompareExchange(&packet.header.stage, 0, 0);
		while (current < desired)
		{
			const LONG previous = InterlockedCompareExchange(&packet.header.stage, desired, current);
			if (previous == current) return;
			current = previous;
		}
	}

	ULONGLONG PublishAuthorizedCleanupFatal() noexcept
	{
		const ULONGLONG deadline = PublishFatalFailedCleanupNoWait();
		auto* packet = g_cleanupRealObservation;
		// local/outer 可幂等并发接管，只有首见证写映射；不做业务等待或格式化。
		if (packet && !g_cleanupRealFatalRecorded.exchange(true, std::memory_order_acq_rel))
		{
			packet->trace.fatalPublishTick = GetTickCount64();
			InterlockedExchange(&packet->trace.intentObserved,
				InterlockedCompareExchange(GetOffSignalInteropPointer(), 0, 0));
			PublishCleanupRealStage(*packet, CleanupRealStage::ClosePublished);
		}
		return deadline;
	}

	bool RunAuthorizedCleanupClose(const VerifiedCleanupRealLaunch& launch) noexcept
	{
		if (!IsAuthorizedCleanupRealLaunch(launch) || launch.scenario < CleanupRealCase::MainUlw
			|| launch.scenario > CleanupRealCase::NaturalClose) return false;
		auto& trace = launch.packet->trace;
		trace.closeRequestTick = GetTickCount64();
		SetOffSignal(1); // 正式产品意图仲裁和原15秒监督，测试不另建/延长截止。
		trace.ordinaryDeadline = PublishedShutdownDeadlineTick();
		InterlockedExchange(&trace.intentObserved, InterlockedCompareExchange(GetOffSignalInteropPointer(), 0, 0));
		const int state = g_armState.load(std::memory_order_acquire);
		InterlockedExchange(&trace.armState, state);
		PublishCleanupRealStage(*launch.packet, CleanupRealStage::ClosePublished);
		return trace.intentObserved == 1 && (state == 2 || state == 4) && trace.ordinaryDeadline > trace.closeRequestTick;
	}

	ArmResult ArmShutdownSupervisor(Intent intent, DWORD deadlineMilliseconds) noexcept
	{
		if (g_startupFailureObservation)
		{
			g_startupFailureObservation->armStartedTick = GetTickCount64();
			g_startupFailureObservation->requestTick = g_startupFailureObservation->armStartedTick;
		}
		const FailedArmTestScenario failedArm = g_failedArmTestScenario.load(std::memory_order_acquire);
		const bool injected = failedArm != FailedArmTestScenario::None;
		const bool handshake = failedArm == FailedArmTestScenario::CloseHandshake
			|| failedArm == FailedArmTestScenario::RestartHandshake;
		const DWORD failureDelay = failedArm == FailedArmTestScenario::CloseConsumed ? 6000u
			: failedArm == FailedArmTestScenario::CloseExpired ? 16500u : 0u;
		const ArmResult result = ArmCore(intent, deadlineMilliseconds,
			g_authorizedUefTestDirectory.empty() ? nullptr
				: g_authorizedUefTestDirectory.c_str(), handshake, injected && !handshake,
				false, false, injected, failureDelay);
		if (g_startupFailureObservation)
		{
			const DWORD savedError = GetLastError();
			g_startupFailureObservation->armResult = static_cast<DWORD>(result);
			g_startupFailureObservation->armError = savedError;
			g_startupFailureObservation->armState = g_armState.load(std::memory_order_acquire);
			g_startupFailureObservation->armDeadlineTick = g_fallbackDeadlineTick.load(std::memory_order_acquire);
			InterlockedExchange(&g_startupFailureObservation->armPublished, 1);
			SetLastError(savedError);
		}
		if (injected && g_failedArmTestObservation)
		{
			const DWORD error = GetLastError();
			// 仅已验真child更新Arm前映射的固定packet；截止接管不等待文件/日志/堆或测试回执。
			g_failedArmTestObservation->deadlineTick = g_fallbackDeadlineTick.load(std::memory_order_acquire);
			g_failedArmTestObservation->failedTick = GetTickCount64();
			g_failedArmTestObservation->result = static_cast<DWORD>(result);
			g_failedArmTestObservation->error = error;
			g_failedArmTestObservation->state = static_cast<DWORD>(g_armState.load(std::memory_order_acquire));
			InterlockedExchange(&g_failedArmTestObservation->published, 1);
			SetLastError(error);
		}
		return result;
	}

	bool IsAuthorizedStartupFailureChild() noexcept
	{
		return g_startupFailureObservation != nullptr;
	}

	bool IsAuthorizedStartupFailure(StartupFailureSite site) noexcept
	{
		return g_startupFailureObservation
			&& g_startupFailureSite.load(std::memory_order_acquire) == site;
	}

	void PublishAuthorizedStartupFailure(StartupFailureSite site,
		DWORD failureCode, HRESULT actualResult) noexcept
	{
		if (!IsAuthorizedStartupFailure(site) || failureCode != StartupFailureCode(site)) return;
		g_startupFailureObservation->realInitResult = static_cast<DWORD>(actualResult);
		g_startupFailureObservation->failureTick = GetTickCount64();
		InterlockedExchange(&g_startupFailureObservation->failurePublished, 1);
	}

	void HoldAuthorizedStartupBoundary(DWORD failureCode, DWORD boundary) noexcept
	{
		auto* observation = g_startupFailureObservation;
		if (!observation || observation->expectedFailureCode != failureCode
			|| InterlockedCompareExchange(&observation->failurePublished, 0, 0) != 1
			|| InterlockedCompareExchange(&observation->gatePublished, 0, 0) != 0) return;
		observation->boundary = boundary;
		observation->gateTick = GetTickCount64();
		if (LONG* intent = GetOffSignalInteropPointer())
			observation->acceptedIntent = InterlockedCompareExchange(intent, 0, 0);
		observation->armState = g_armState.load(std::memory_order_acquire);
		InterlockedExchange(&observation->gatePublished, 1);
		// 无停滞反例仍经过真实提示/清理；授权之外和旧Hold用例不改变行为。
		if (observation->testMode == 1) return;
		// 正式监督若已建立会独立强退；红测仅由父进程上限后清理本child。
		Sleep(INFINITE);
	}

	ULONGLONG PublishedShutdownDeadlineTick() noexcept
	{
		return g_fallbackDeadlineTick.load(std::memory_order_acquire);
	}

	[[noreturn]] void EnforceFailedShutdownDeadline(Intent intent) noexcept
	{
		// SetOffSignal首次胜者的合法Arm已在state3结束；它与UEF先争同一意图槽，不抢现有helper。
		const ULONGLONG deadline = g_fallbackDeadlineTick.load(std::memory_order_acquire);
		for (;;)
		{
			const ULONGLONG now = GetTickCount64();
			if (now >= deadline) break; // 已耗/已过期都沿用原始截止，不重新加15秒。
			Sleep(static_cast<DWORD>(deadline - now));
		}
		const DWORD exitCode = intent == Intent::Restart
			? kFailedArmRestartForcedExitCode : kFailedArmCloseForcedExitCode;
		for (;;)
		{
			// 双监督均未建立时，当前owner专职截止；不得依赖堆、日志、业务锁或DLL清理。
			TerminateProcess(GetCurrentProcess(), exitCode);
			Sleep(1); // 自终止意外失败也不能返回可能永久等待的业务清理。
		}
	}

	bool TryClaimCrashRestartIntent(LONG* intentSlot, int crashMode,
		bool userAccepted) noexcept
	{
		// 手动确认前或用户拒绝时不占槽，正式 Close/Restart 仍可优先建立自己的15秒保护。
		if (crashMode != 1 && !(crashMode == 0 && userAccepted)) return false;
		return intentSlot && InterlockedCompareExchange(intentSlot, 3, 0) == 0;
	}

	namespace DiagnosticsProcess
	{
		std::wstring QuoteArgument(const std::wstring& argument) { return Quote(argument); }
		bool CurrentImage(std::wstring& image) { return CurrentImagePath(image); }
		bool ProcessImage(HANDLE process, std::wstring& image) { return ProcessImagePath(process, image); }
		bool SameImageFile(const std::wstring& left, const std::wstring& right) noexcept
		{
			return SameExecutableFile(left, right);
		}
		bool StartInherited(const std::wstring& image, const std::wstring& quotedArguments,
			STARTUPINFOEXW& startup, PROCESS_INFORMATION& process) noexcept
		{
			// 调用者持有exact名单到CreateProcess返回，不接受宽泛继承或额外创建flags。
			if (startup.StartupInfo.cb != sizeof(startup) || !startup.lpAttributeList)
			{
				SetLastError(ERROR_INVALID_PARAMETER);
				return false;
			}
			return StartSameExecutable(image, quotedArguments, true,
				EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, &startup, process);
		}
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
				|| wcsstr(fullCommandLine, kStartupFailureChildMode)
				|| wcsstr(fullCommandLine, kCleanupChildMode)
				|| wcsstr(fullCommandLine, kCleanupRealChildMode)
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
			|| EqualNoCase(argv[1], kRealUefTestChildMode)
			|| EqualNoCase(argv[1], kStartupFailureChildMode)
			|| EqualNoCase(argv[1], kCleanupChildMode)
			|| EqualNoCase(argv[1], kCleanupRealChildMode));
		if (recognized && EqualNoCase(argv[1], kCleanupRealChildMode))
		{
			try { exitCode = RunCleanupRealChild(argc, argv); }
			catch (...) { exitCode = 72; }
			LocalFree(argv);
			return true; // 新purpose永不落入真实配置/普通Main/Office或系统输入初始化。
		}
		if (recognized && EqualNoCase(argv[1], kStartupFailureChildMode))
		{
			try { exitCode = AuthorizeStartupFailureChild(argc, argv); }
			catch (...) { exitCode = 72; }
			LocalFree(argv);
			return exitCode != 0; // 只有通过句柄及镜像鉴权的child进入真实wWinMain。
		}
		if (recognized && EqualNoCase(argv[1], kCleanupChildMode))
		{
			try { exitCode = RunCleanupPrimitiveChild(argc, argv); }
			catch (...) { exitCode = 72; }
			LocalFree(argv);
			return true; // 新purpose永不进入配置/普通窗口/Office初始化。
		}
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
				else if (EqualNoCase(argv[1], kTestMode))
				{
					if (argc == 2) exitCode = RunNoGuiTests();
					else if (argc == 3 && EqualNoCase(argv[2], L"--failed-arm-only")) exitCode = RunNoGuiTests(true);
					else if (argc == 3 && EqualNoCase(argv[2], L"--startup-failure-only")) exitCode = RunStartupFailureTests();
					else if (argc == 4 && EqualNoCase(argv[2], L"--startup-failure-only")
						&& ParseStartupFailureSite(argv[3]) != StartupFailureSite::None)
						exitCode = RunStartupFailureTests(ParseStartupFailureSite(argv[3]));
					else if (argc == 3 && EqualNoCase(argv[2], L"--startup-no-hold-only"))
						exitCode = RunStartupFailureTests(StartupFailureSite::None, true);
					else if (argc == 4 && EqualNoCase(argv[2], L"--startup-no-hold-only")
						&& ParseStartupFailureSite(argv[3]) != StartupFailureSite::None)
						exitCode = RunStartupFailureTests(ParseStartupFailureSite(argv[3]), true);
					else if (argc == 3 && EqualNoCase(argv[2], L"--failed-cleanup-only"))
						exitCode = RunCleanupPrimitiveTests();
					else if (argc == 4 && EqualNoCase(argv[2], L"--failed-cleanup-only")
						&& ParseCleanupScenario(argv[3]) != CleanupPrimitiveScenario::None)
						exitCode = RunCleanupPrimitiveTests(ParseCleanupScenario(argv[3]));
					else if (argc == 4 && EqualNoCase(argv[2], L"--failed-cleanup-real-only")
						&& ParseCleanupRealCase(argv[3]) != CleanupRealCase::None)
						exitCode = RunCleanupRealTests(ParseCleanupRealCase(argv[3]));
					else exitCode = 71;
				}
				else if (EqualNoCase(argv[1], kTestParentMode)) exitCode = RunTestParent(argc, argv);
				else exitCode = RunTestRestarted(argc, argv);
			}
			catch (...) { exitCode = 72; }
		}
		LocalFree(argv);
		return recognized;
	}
}
