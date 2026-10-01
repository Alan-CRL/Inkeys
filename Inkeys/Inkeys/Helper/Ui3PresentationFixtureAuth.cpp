#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Ui3PresentationFixtureAuth.h"
#include "ShutdownSupervisor.h"

#include <shellapi.h>
#include <objbase.h>
#include <winioctl.h>

#include <array>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

import Inkeys.UI.RenderPipeline;

namespace Inkeys::UI::Bar
{
	namespace
	{
		namespace Process = Inkeys::Shutdown::DiagnosticsProcess;
		constexpr wchar_t kChildMode[] = L"--inkeys-internal-ui3-fixture-child-v1";
		constexpr wchar_t kBenchmarkMode[] = L"--ui3-presentation-benchmark";
		constexpr wchar_t kAuthTestMode[] = L"--ui3-fixture-auth-tests";
		constexpr DWORD kChildBudget = 300000, kHandshakeBudget = 3000, kCleanupBudget = 5000;
		constexpr int kArgumentsRejected = 81, kNumberRejected = 82, kPacketRejected = 83;
		constexpr int kIdentityRejected = 84, kAckRejected = 85, kFixtureFailed = 90;

		struct Handle
		{
			HANDLE value = nullptr;
			Handle() = default;
			explicit Handle(HANDLE handle) noexcept : value(handle) {}
			Handle(const Handle&) = delete;
			Handle& operator=(const Handle&) = delete;
			Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
			Handle& operator=(Handle&& other) noexcept
			{
				if (this != &other) { Reset(); value = std::exchange(other.value, nullptr); }
				return *this;
			}
			~Handle() { Reset(); }
			bool Valid() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
			void Reset(HANDLE replacement = nullptr) noexcept
			{
				if (Valid()) CloseHandle(value);
				value = replacement;
			}
		};
		struct FileIdentity { DWORD volume = 0, high = 0, low = 0; };
		struct FileLease { std::wstring path; Handle handle; FileIdentity identity; };
		struct MappedView
		{
			void* value = nullptr;
			~MappedView() { if (value) UnmapViewOfFile(value); }
		};
		struct AttributeList
		{
			void* memory = nullptr;
			bool initialized = false;
			~AttributeList()
			{
				if (initialized) DeleteProcThreadAttributeList(static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory));
				if (memory) HeapFree(GetProcessHeap(), 0, memory);
			}
		};
		struct ChildGuard
		{
			HANDLE process = nullptr;
			bool cleanupRequested = false;
			bool Stop() noexcept
			{
				if (!process) return true;
				if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return true;
				cleanupRequested = true;
				// 这里只能清理CreateProcess刚返回的本轮精确HANDLE；清理永远不算自然PASS。
				TerminateProcess(process, ERROR_OPERATION_ABORTED);
				return WaitForSingleObject(process, kCleanupBudget) == WAIT_OBJECT_0;
			}
			~ChildGuard() { (void)Stop(); }
		};
		std::atomic<const Ui3FixtureAuthorization*> g_authorization{ nullptr };
		Ui3FixturePacketV1* g_authorizedPacket = nullptr;
		std::uint64_t g_nonceLo = 0, g_nonceHi = 0;

		bool EqualPath(const std::wstring& a, const std::wstring& b) noexcept
		{
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
		}
		bool Within(const std::wstring& path, const std::wstring& root) noexcept
		{
			return path.size() > root.size() && path[root.size()] == L'\\'
				&& CompareStringOrdinal(path.c_str(), static_cast<int>(root.size()), root.c_str(),
					static_cast<int>(root.size()), TRUE) == CSTR_EQUAL;
		}
		bool ParseUnsigned(const wchar_t* value, std::uint64_t& result) noexcept
		{
			if (!value || !*value) return false;
			std::uint64_t number = 0;
			for (const wchar_t* at = value; *at; ++at)
			{
				if (*at < L'0' || *at > L'9') return false;
				const unsigned digit = static_cast<unsigned>(*at - L'0');
				if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
				number = number * 10 + digit;
			}
			result = number;
			return true;
		}
		bool CanonicalPath(const std::wstring& path, std::wstring& full)
		{
			if (path.size() < 3 || path.size() >= 32768 || path[1] != L':' || path[2] != L'\\'
				|| !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
				return false;
			for (std::size_t start = 3; start < path.size();)
			{
				const std::size_t end = path.find(L'\\', start);
				const std::wstring part = path.substr(start, end == std::wstring::npos ? end : end - start);
				if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' '
					|| part.find_first_of(L"<>:\"/|?*") != std::wstring::npos) return false;
				for (wchar_t ch : part) if (ch < 32) return false;
				if (end == std::wstring::npos) break;
				if (end + 1 == path.size()) return false;
				start = end + 1;
			}
			full.assign(32768, L'\0');
			const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
			if (!length || length >= full.size()) return false;
			full.resize(length);
			return EqualPath(path, full); // 拒别名/规范化改变，不让dot或ADS被规范化后追认。
		}
		std::wstring PopPart(std::wstring& path)
		{
			const std::size_t end = path.find_last_of(L'\\');
			if (end == std::wstring::npos || end <= 2) return {};
			std::wstring part = path.substr(end + 1);
			path.resize(end);
			return part;
		}
		bool IsMaster(const std::wstring& part) noexcept
		{
			constexpr wchar_t prefix[] = L"ui3-finite-";
			constexpr std::size_t prefixLength = std::size(prefix) - 1;
			if (part.size() != prefixLength + 32 || part.compare(0, prefixLength, prefix) != 0) return false;
			for (std::size_t i = prefixLength; i < part.size(); ++i)
				if (!((part[i] >= L'0' && part[i] <= L'9') || (part[i] >= L'a' && part[i] <= L'f'))) return false;
			return true;
		}
		bool ValidCapacity(std::uint32_t capture, std::uint32_t capacity) noexcept
		{
			if (capture == 0) return capacity == 0;
			if (capture != 1 || capacity < 256 || capacity > 65536) return false;
			constexpr std::size_t sampleBytes = sizeof(RenderPipeline::RawCallbackSample) + sizeof(RenderPipeline::RawBatchSample);
			static_assert(sampleBytes > 0 && Ui3FixtureFixedStorageBudget < Ui3FixtureTotalStorageBudget);
			return capacity <= (Ui3FixtureTotalStorageBudget - Ui3FixtureFixedStorageBudget) / sampleBytes;
		}
		Ui3FixtureFrozenInputV1 Freeze(const Ui3FixturePacketV1& p) noexcept
		{
			return { p.magic, p.version, p.bytes, p.purpose, p.scene, p.capture, p.capacity, p.round,
				p.sourceVersion, p.trajectoryCount, p.nonceLo, p.nonceHi, p.sourceHash, p.expectedSteps };
		}
		bool SameInput(const Ui3FixtureFrozenInputV1& a, const Ui3FixtureFrozenInputV1& b) noexcept
		{
			return std::memcmp(&a, &b, sizeof(a)) == 0;
		}
		LONG ReadWord(std::uint32_t& word) noexcept
		{
			return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&word), 0, 0);
		}
		void PublishWord(std::uint32_t& word, std::uint32_t value) noexcept
		{
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&word), static_cast<LONG>(value));
		}
		bool ValidInput(const Ui3FixtureFrozenInputV1& input) noexcept
		{
			if (input.magic != Ui3FixtureMagic || input.version != 1 || input.bytes != sizeof(Ui3FixturePacketV1)
				|| input.purpose != Ui3FixturePurpose || input.scene < 1 || input.scene > 2
				|| input.round < 1 || input.round > 3 || !ValidCapacity(input.capture, input.capacity)
				|| input.sourceVersion != Ui3FixtureSourceVersion || !(input.nonceLo | input.nonceHi)
				|| input.expectedSteps != Ui3FixtureExpectedSteps
				|| input.trajectoryCount != (input.scene == 1 ? 433u : 435u)) return false;
			const auto source = GetCompiledUi3FixtureSourceV1(static_cast<Ui3FiniteScene>(input.scene));
			return source.sourceVersion == input.sourceVersion && source.trajectoryCount == input.trajectoryCount
				&& source.expectedSteps == input.expectedSteps && source.sourceHash != 0 && source.sourceHash == input.sourceHash;
		}
		bool OutputsZero(const Ui3FixturePacketV1& p) noexcept
		{
			return !p.authorized && !p.stage && !p.result && !p.received && !p.enqueued && !p.consumed
				&& !p.startedTicks && !p.finishedTicks && !p.completedSteps && !p.unverifiedSteps;
		}
		bool Inherited(HANDLE handle) noexcept
		{
			DWORD flags = 0;
			return GetHandleInformation(handle, &flags) && (flags & HANDLE_FLAG_INHERIT);
		}
		[[noreturn]] void UnsafeRunnerReturn() noexcept
		{
			// 未封口返回不能销毁仍被owner借用的capability/映射；失败保留栈直到进程死亡。
			for (;;) { TerminateProcess(GetCurrentProcess(), kFixtureFailed); Sleep(1); }
		}
	}

	namespace Detail
	{
		struct Ui3OwnedDirectoryProof
		{
			std::vector<FileLease> leases;
			std::wstring repository, leaf;
			FileIdentity repositoryIdentity, sourceIdentity, copiedIdentity, masterIdentity, binIdentity;
		};
	}

	namespace
	{
		bool LeasePath(const std::wstring& path, bool directory, Detail::Ui3OwnedDirectoryProof& proof,
			FileIdentity* identity = nullptr)
		{
			std::wstring full;
			if (!CanonicalPath(path, full)) return false;
			for (std::size_t end = 3;;)
			{
				const bool leaf = end == full.size();
				const std::wstring part = full.substr(0, end);
				FileLease* found = nullptr;
				for (auto& lease : proof.leases) if (EqualPath(lease.path, part)) { found = &lease; break; }
				BY_HANDLE_FILE_INFORMATION info{};
				if (!found)
				{
					if (proof.leases.size() >= 256) return false;
					// 每层独立OPEN_REPARSE_POINT，禁delete共享，包含drive祖先和源EXE。
					Handle file(CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
						nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
					if (!file.Valid() || !GetFileInformationByHandle(file.value, &info)) return false;
					proof.leases.push_back({ part, std::move(file), { info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow } });
					found = &proof.leases.back();
				}
				if (!GetFileInformationByHandle(found->handle.value, &info)
					|| (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
					|| (!!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != (!leaf || directory))) return false;
				if (leaf) { if (identity) *identity = found->identity; return true; }
				const std::size_t next = full.find(L'\\', end + (end == 3 ? 0 : 1));
				end = next == std::wstring::npos ? full.size() : next;
			}
		}
		bool OptionalFile(const std::wstring& path, Detail::Ui3OwnedDirectoryProof& proof)
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES) return LeasePath(path, false, proof);
			return GetLastError() == ERROR_FILE_NOT_FOUND;
		}
		bool ValidateOwnedLeaf(const std::wstring& absoluteLeaf, const std::wstring& actualParentImage,
			const std::wstring& expectedParentImage, const std::wstring& actualChildImage,
			std::uint32_t round, std::uint32_t scene, Detail::Ui3OwnedDirectoryProof& proof)
		{
			std::wstring leaf, parent, expected, child;
			if (round < 1 || round > 3 || scene < 1 || scene > 2 || !CanonicalPath(absoluteLeaf, leaf)
				|| !CanonicalPath(actualParentImage, parent) || !CanonicalPath(expectedParentImage, expected)
				|| !CanonicalPath(actualChildImage, child)) return false;
			std::wstring repo = leaf;
			if (PopPart(repo) != L"s" + std::to_wstring(scene) || PopPart(repo) != L"r" + std::to_wstring(round)) return false;
			const std::wstring masterName = PopPart(repo);
			if (!IsMaster(masterName) || PopPart(repo) != L"release-hardening" || PopPart(repo) != L"TestResults"
				|| repo.size() <= 3 || !Within(parent, repo) || !Within(expected, repo)) return false;
			const std::wstring master = repo + L"\\TestResults\\release-hardening\\" + masterName;
			const std::wstring bin = leaf + L"\\bin", copied = bin + L"\\Inkeys.exe";
			if (!EqualPath(child, copied) || !LeasePath(repo, true, proof, &proof.repositoryIdentity)
				|| !LeasePath(repo + L"\\InkeysRepo.sln", false, proof)
				|| !LeasePath(repo + L"\\.trellis", true, proof)
				|| !LeasePath(parent, false, proof, &proof.sourceIdentity) || !LeasePath(expected, false, proof)
				|| !Process::SameImageFile(parent, expected)
				|| !LeasePath(master, true, proof, &proof.masterIdentity) || !LeasePath(leaf, true, proof)
				|| !LeasePath(bin, true, proof, &proof.binIdentity)
				|| !LeasePath(copied, false, proof, &proof.copiedIdentity)
				|| !LeasePath(child, false, proof) || !Process::SameImageFile(child, copied)) return false;
			for (const wchar_t* sub : { L"\\Inkeys", L"\\Inkeys\\Config", L"\\opt", L"\\log" })
				if (!LeasePath(bin + sub, true, proof)) return false;
			for (const wchar_t* sub : { L"\\Inkeys\\Config\\main.json", L"\\opt\\deploy.json" })
				if (!OptionalFile(bin + sub, proof)) return false;
			proof.repository = repo;
			proof.leaf = leaf;
			return true;
		}
		bool CreateDirectoryLeased(const std::wstring& path, bool mayExist, Detail::Ui3OwnedDirectoryProof& proof)
		{
			if (!CreateDirectoryW(path.c_str(), nullptr) && (!mayExist || GetLastError() != ERROR_ALREADY_EXISTS)) return false;
			return LeasePath(path, true, proof);
		}
		std::wstring GuidName(const GUID& guid)
		{
			const auto* bytes = reinterpret_cast<const unsigned char*>(&guid);
			constexpr wchar_t hex[] = L"0123456789abcdef";
			std::wstring name;
			name.reserve(32);
			for (std::size_t i = 0; i < sizeof(guid); ++i) { name += hex[bytes[i] >> 4]; name += hex[bytes[i] & 15]; }
			return name;
		}
		struct ParentRun
		{
			std::wstring sourceImage, repository, master, leaf, copiedImage;
			Detail::Ui3OwnedDirectoryProof proof;
			GUID nonce{};
		};
		bool MakeParentRun(std::uint32_t round, std::uint32_t scene, ParentRun& run)
		{
			std::wstring cwd(32768, L'\0'), source;
			const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data());
			if (!length || length >= cwd.size() || !Process::CurrentImage(source)) return false;
			cwd.resize(length);
			if (!CanonicalPath(cwd, run.repository) || !CanonicalPath(source, run.sourceImage)
				|| !Within(run.sourceImage, run.repository) || !LeasePath(run.repository, true, run.proof)
				|| !LeasePath(run.repository + L"\\InkeysRepo.sln", false, run.proof)
				|| !LeasePath(run.repository + L"\\.trellis", true, run.proof)
				|| !LeasePath(run.sourceImage, false, run.proof)) return false;
			GUID master{};
			if (FAILED(CoCreateGuid(&master)) || FAILED(CoCreateGuid(&run.nonce))) return false;
			const std::wstring results = run.repository + L"\\TestResults", evidence = results + L"\\release-hardening";
			run.master = evidence + L"\\ui3-finite-" + GuidName(master);
			const std::wstring roundPath = run.master + L"\\r" + std::to_wstring(round);
			run.leaf = roundPath + L"\\s" + std::to_wstring(scene);
			const std::wstring bin = run.leaf + L"\\bin";
			if (!CreateDirectoryLeased(results, true, run.proof) || !CreateDirectoryLeased(evidence, true, run.proof)) return false;
			for (const auto& path : { run.master, roundPath, run.leaf, bin, bin + L"\\Inkeys",
				bin + L"\\Inkeys\\Config", bin + L"\\opt", bin + L"\\log" })
				if (!CreateDirectoryLeased(path, false, run.proof)) return false;
			run.copiedImage = bin + L"\\Inkeys.exe";
			return CopyFileW(run.sourceImage.c_str(), run.copiedImage.c_str(), TRUE)
				&& ValidateOwnedLeaf(run.leaf, run.sourceImage, run.sourceImage, run.copiedImage, round, scene, run.proof);
		}
	}

	Ui3FixtureAuthorization::Ui3FixtureAuthorization(Ui3FixtureFrozenInputV1 input, std::wstring repository,
		std::wstring privateRoot, Ui3FixturePacketV1* packet, std::unique_ptr<Detail::Ui3OwnedDirectoryProof> proof)
		: input_(input), repository_(std::move(repository)), privateRoot_(std::move(privateRoot)),
		binaryDirectory_(privateRoot_ + L"\\bin"), packet_(packet), proof_(std::move(proof)) {}
	Ui3FixtureAuthorization::~Ui3FixtureAuthorization() = default;

	bool IsAuthorizedUi3Fixture(const Ui3FixtureAuthorization& launch) noexcept
	{
		if (g_authorization.load(std::memory_order_acquire) != &launch) return false;
		return &launch.Packet() == g_authorizedPacket && launch.Input().nonceLo == g_nonceLo
			&& launch.Input().nonceHi == g_nonceHi && (g_nonceLo | g_nonceHi)
			&& ReadWord(g_authorizedPacket->authorized) == 1;
	}

	struct Ui3FixtureAuthorizer
	{
		static int RunChild(int argc, wchar_t* const argv[])
		{
			if (argc != 8) return kArgumentsRejected;
			std::uint64_t pid = 0, parentValue = 0, ackValue = 0, mapValue = 0;
			if (!ParseUnsigned(argv[2], pid) || !pid || pid > MAXDWORD || pid == GetCurrentProcessId()
				|| !ParseUnsigned(argv[3], parentValue) || !parentValue || parentValue > UINTPTR_MAX
				|| !ParseUnsigned(argv[4], ackValue) || !ackValue || ackValue > UINTPTR_MAX
				|| !ParseUnsigned(argv[5], mapValue) || !mapValue || mapValue > UINTPTR_MAX
				|| parentValue == ackValue || parentValue == mapValue || ackValue == mapValue) return kNumberRejected;
			const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(parentValue));
			const HANDLE ack = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(ackValue));
			const HANDLE mapping = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(mapValue));
			if (!Inherited(parent) || !Inherited(ack) || !Inherited(mapping) || GetProcessId(parent) != pid
				|| WaitForSingleObject(parent, 0) != WAIT_TIMEOUT) return kPacketRejected;
			Handle ownedParent(parent), ownedAck(ack), ownedMapping(mapping);
			MappedView view;
			view.value = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Ui3FixturePacketV1));
			if (!view.value) return kPacketRejected;
			auto* packet = static_cast<Ui3FixturePacketV1*>(view.value);
			const Ui3FixtureFrozenInputV1 input = Freeze(*packet);
			if (!ValidInput(input) || !OutputsZero(*packet)) return kPacketRejected;
			std::wstring parentImage, childImage;
			auto proof = std::make_unique<Detail::Ui3OwnedDirectoryProof>();
			if (!Process::ProcessImage(parent, parentImage) || !Process::CurrentImage(childImage)
				|| !ValidateOwnedLeaf(argv[6], parentImage, argv[7], childImage, input.round, input.scene, *proof))
				return kIdentityRejected;
			const std::wstring repository = proof->repository, leaf = proof->leaf;
			Ui3FixtureAuthorization launch(input, repository, leaf, packet, std::move(proof));
			if (g_authorization.load(std::memory_order_acquire)) return kPacketRejected;
			g_authorizedPacket = packet;
			g_nonceLo = input.nonceLo; g_nonceHi = input.nonceHi;
			g_authorization.store(&launch, std::memory_order_release);
			PublishWord(packet->authorized, 1);
			PublishWord(packet->stage, static_cast<std::uint32_t>(Ui3FixtureStage::Authorized));
			if (!SetEvent(ack))
			{
				g_authorization.store(nullptr, std::memory_order_release);
				g_authorizedPacket = nullptr; g_nonceLo = g_nonceHi = 0;
				return kAckRejected;
			}
			const int result = RunAuthorizedPresentationFixture(launch);
			// Bar唯一返回合同要求真join后Sealed；缺证明时不展开借用对象。
			if (ReadWord(packet->stage) != static_cast<LONG>(Ui3FixtureStage::Sealed)) UnsafeRunnerReturn();
			g_authorization.store(nullptr, std::memory_order_release);
			g_authorizedPacket = nullptr; g_nonceLo = g_nonceHi = 0;
			return SameInput(input, Freeze(*packet)) ? result : kFixtureFailed;
		}
	};
}

namespace Inkeys::UI::Bar
{
	namespace
	{
		struct Options { std::uint32_t scene = 1, capture = 0, capacity = 0, round = 1; };
		enum class Negative
		{
			None, Magic, Version, Bytes, Purpose, Scene, Capture, Capacity, Round,
			SourceVersion, Count, Hash, Steps, Nonce, InitialOutput, ParentPid, ParentImage,
			DuplicateHandles, NoInheritedHandles, ArgumentCount, DisplacedMode, LeafRound, LeafScene,
		};
		int ExpectedReject(Negative fault) noexcept
		{
			if (fault == Negative::ArgumentCount || fault == Negative::DisplacedMode) return kArgumentsRejected;
			if (fault == Negative::DuplicateHandles) return kNumberRejected;
			if (fault == Negative::ParentImage || fault == Negative::LeafRound || fault == Negative::LeafScene) return kIdentityRejected;
			return kPacketRejected;
		}
		void CorruptInput(Ui3FixturePacketV1& p, Negative fault) noexcept
		{
			switch (fault)
			{
			case Negative::Magic: p.magic ^= 1; break;
			case Negative::Version: ++p.version; break;
			case Negative::Bytes: p.bytes = 96; break;
			case Negative::Purpose: p.purpose ^= 1; break;
			case Negative::Scene: p.scene = 3; break;
			case Negative::Capture: p.capture = 2; break;
			case Negative::Capacity: p.capture = 1; p.capacity = 255; break;
			case Negative::Round: p.round = 4; break;
			case Negative::SourceVersion: ++p.sourceVersion; break;
			case Negative::Count: --p.trajectoryCount; break;
			case Negative::Hash: p.sourceHash ^= 1; break;
			case Negative::Steps: --p.expectedSteps; break;
			case Negative::Nonce: p.nonceLo = p.nonceHi = 0; break;
			case Negative::InitialOutput: p.consumed = 1; break;
			default: break;
			}
		}
		bool ParseOptions(int argc, wchar_t* const argv[], Options& options) noexcept
		{
			if (argc != 10) return false;
			unsigned fields = 0;
			for (int index = 2; index < argc; index += 2)
			{
				unsigned field = 0;
				std::uint64_t number = 0;
				if (wcscmp(argv[index], L"--scene") == 0)
				{
					field = 1;
					if (wcscmp(argv[index + 1], L"main-fold") == 0) options.scene = 1;
					else if (wcscmp(argv[index + 1], L"draw-attribute") == 0) options.scene = 2;
					else return false;
				}
				else if (wcscmp(argv[index], L"--round") == 0)
				{
					field = 2;
					if (!ParseUnsigned(argv[index + 1], number) || number < 1 || number > 3) return false;
					options.round = static_cast<std::uint32_t>(number);
				}
				else if (wcscmp(argv[index], L"--capture") == 0)
				{
					field = 4;
					if (wcscmp(argv[index + 1], L"on") == 0) options.capture = 1;
					else if (wcscmp(argv[index + 1], L"off") == 0) options.capture = 0;
					else return false;
				}
				else if (wcscmp(argv[index], L"--capacity") == 0)
				{
					field = 8;
					if (!ParseUnsigned(argv[index + 1], number) || number > 65536) return false;
					options.capacity = static_cast<std::uint32_t>(number);
				}
				else return false;
				if (fields & field) return false;
				fields |= field;
			}
			return fields == 15 && ValidCapacity(options.capture, options.capacity);
		}
		DWORD Remaining(ULONGLONG start, DWORD budget) noexcept
		{
			const ULONGLONG now = GetTickCount64();
			if (now < start || now - start >= budget) return 0;
			return budget - static_cast<DWORD>(now - start);
		}
		bool WaitChild(HANDLE child, ULONGLONG start, DWORD budget) noexcept
		{
			for (;;)
			{
				const DWORD left = Remaining(start, budget);
				const DWORD wait = WaitForSingleObject(child, left > 1000 ? 1000 : left);
				if (wait == WAIT_OBJECT_0) return true;
				if (wait != WAIT_TIMEOUT || !left) return false;
			}
		}
		bool SealedOutputValid(const Ui3FixturePacketV1& p) noexcept
		{
			return p.authorized == 1 && p.stage == static_cast<std::uint32_t>(Ui3FixtureStage::Sealed)
				&& (p.result == static_cast<std::uint32_t>(Ui3FixtureResult::Passed)
					|| p.result == static_cast<std::uint32_t>(Ui3FixtureResult::Failed))
				&& p.consumed <= p.enqueued && p.enqueued <= p.received && p.received <= p.trajectoryCount
				&& p.completedSteps <= Ui3FixtureExpectedSteps && p.unverifiedSteps <= Ui3FixtureExpectedSteps
				&& (p.capture ? p.startedTicks >= 0 && p.finishedTicks >= p.startedTicks : !p.startedTicks && !p.finishedTicks);
		}
		bool CheckOutputPaths(ParentRun& run, const Options& options, bool required)
		{
			if (!ValidateOwnedLeaf(run.leaf, run.sourceImage, run.sourceImage, run.copiedImage,
				options.round, options.scene, run.proof)) return false;
			for (const wchar_t* name : { L"meta.json", L"raw-callbacks.csv", L"raw-batches.csv", L"finite-targets.csv",
				L"source-events.csv", L"svg-cold.json", L"svg-warm.json", L"summary.json", L"equivalence.png", L"BGRAhash" })
				if (!OptionalFile(run.leaf + L"\\" + name, run.proof)) return false;
			if (required)
			{
				for (const wchar_t* name : { L"meta.json", L"finite-targets.csv", L"source-events.csv",
					L"svg-cold.json", L"svg-warm.json", L"summary.json" })
					if (!LeasePath(run.leaf + L"\\" + name, false, run.proof)) return false;
				if (options.capture)
					for (const wchar_t* name : { L"raw-callbacks.csv", L"raw-batches.csv" })
						if (!LeasePath(run.leaf + L"\\" + name, false, run.proof)) return false;
			}
			return true;
		}
		bool RunParent(const Options& options, Negative fault, bool requireBenchmark, const char* label)
		{
			ParentRun run;
			if (!MakeParentRun(options.round, options.scene, run)) return false;
			Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Ui3FixturePacketV1), nullptr));
			Handle ack(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!mapping.Valid() || !ack.Valid()) return false;
			MappedView view;
			view.value = MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(Ui3FixturePacketV1));
			if (!view.value) return false;
			auto* packet = static_cast<Ui3FixturePacketV1*>(view.value);
			const auto source = GetCompiledUi3FixtureSourceV1(static_cast<Ui3FiniteScene>(options.scene));
			*packet = {};
			packet->magic = Ui3FixtureMagic; packet->version = 1; packet->bytes = sizeof(*packet); packet->purpose = Ui3FixturePurpose;
			packet->scene = options.scene; packet->capture = options.capture; packet->capacity = options.capacity; packet->round = options.round;
			packet->sourceVersion = source.sourceVersion; packet->trajectoryCount = source.trajectoryCount;
			packet->sourceHash = source.sourceHash; packet->expectedSteps = source.expectedSteps;
			static_assert(sizeof(run.nonce) == 16);
			std::memcpy(&packet->nonceLo, &run.nonce, sizeof(run.nonce));
			if (!ValidInput(Freeze(*packet))) return false;
			CorruptInput(*packet, fault);
			const auto expectedInput = Freeze(*packet);
			HANDLE parentValue = nullptr, ackValue = nullptr, mapValue = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &parentValue,
				SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
			Handle parent(parentValue);
			if (!DuplicateHandle(GetCurrentProcess(), ack.value, GetCurrentProcess(), &ackValue, EVENT_MODIFY_STATE, TRUE, 0)) return false;
			Handle inheritedAck(ackValue);
			if (!DuplicateHandle(GetCurrentProcess(), mapping.value, GetCurrentProcess(), &mapValue, FILE_MAP_WRITE, TRUE, 0)) return false;
			Handle inheritedMap(mapValue);
			HANDLE handles[] = { parent.value, inheritedAck.value, inheritedMap.value };
			if (handles[0] == handles[1] || handles[0] == handles[2] || handles[1] == handles[2]) return false;
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
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				handles, sizeof(handles), nullptr, nullptr)) return false;
			std::wstring leafArgument = run.leaf;
			if (fault == Negative::LeafRound) leafArgument.replace(leafArgument.size() - 4, 1, L"2");
			if (fault == Negative::LeafScene) leafArgument.back() = L'2';
			const DWORD pid = GetCurrentProcessId();
			std::wstring arguments = std::wstring(kChildMode) + L" " + std::to_wstring(fault == Negative::ParentPid ? pid + 1 : pid)
				+ L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(parent.value))
				+ L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(fault == Negative::DuplicateHandles ? parent.value : inheritedAck.value))
				+ L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(inheritedMap.value))
				+ L" " + Process::QuoteArgument(leafArgument) + L" "
				+ Process::QuoteArgument(fault == Negative::ParentImage ? run.copiedImage : run.sourceImage);
			if (fault == Negative::ArgumentCount) arguments += L" extra";
			if (fault == Negative::DisplacedMode) arguments = L"--unexpected " + arguments;
			PROCESS_INFORMATION process{};
			if (fault == Negative::NoInheritedHandles)
			{
				// 仅固定auth负例：真正启动同copied child，让child自己拒绝无继承；不公开flags/路径。
				std::wstring command = Process::QuoteArgument(run.copiedImage) + L" " + arguments;
				STARTUPINFOW ordinary{}; ordinary.cb = sizeof(ordinary);
				const std::wstring bin = run.leaf + L"\\bin";
				if (!CreateProcessW(run.copiedImage.c_str(), command.data(), nullptr, nullptr, FALSE,
					CREATE_NO_WINDOW, nullptr, bin.c_str(), &ordinary, &process)) return false;
			}
			else if (!Process::StartInherited(run.copiedImage, arguments, startup, process)) return false;
			Handle child(process.hProcess), childThread(process.hThread);
			ChildGuard guard{ child.value };
			const ULONGLONG start = GetTickCount64();
			const HANDLE signals[] = { ack.value, child.value };
			const DWORD handshake = WaitForMultipleObjects(2, signals, FALSE, kHandshakeBudget);
			const bool acknowledged = handshake == WAIT_OBJECT_0 && ReadWord(packet->authorized) == 1;
			const DWORD budget = fault == Negative::None ? kChildBudget : kHandshakeBudget;
			const bool naturalDeath = WaitChild(child.value, start, budget);
			if (!naturalDeath) (void)guard.Stop();
			DWORD exitCode = STILL_ACTIVE;
			if (!naturalDeath || !GetExitCodeProcess(child.value, &exitCode)) return false;
			// 所有64位输出仅在exact child真正死亡后读，不并发copy正在写的packet。
			const Ui3FixturePacketV1 final = *packet;
			std::printf("[UI3FixtureAuth] case=%s pid=%lu ack=%u stage=%u result=%u received=%u enqueued=%u consumed=%u completed=%llu unverified=%llu exit=%lu cleanup=%u\n",
				label, process.dwProcessId, acknowledged ? 1u : 0u, final.stage, final.result, final.received, final.enqueued, final.consumed,
				static_cast<unsigned long long>(final.completedSteps), static_cast<unsigned long long>(final.unverifiedSteps), exitCode, guard.cleanupRequested ? 1u : 0u);
			std::fwprintf(stderr, L"[UI3FixtureAuth] private-root=%ls\n", run.leaf.c_str());
			if (fault != Negative::None)
				return !acknowledged && SameInput(expectedInput, Freeze(final)) && final.authorized == 0 && final.stage == 0
					&& exitCode == static_cast<DWORD>(ExpectedReject(fault)) && !guard.cleanupRequested;
			if (!acknowledged || !SameInput(expectedInput, Freeze(final)) || !SealedOutputValid(final)
				|| guard.cleanupRequested || !CheckOutputPaths(run, options, requireBenchmark)) return false;
			// auth正例仍执行同一Bar runner；这里只分别报告授权和benchmark结论，不跳过初始化/场景。
			return !requireBenchmark || (exitCode == 0 && final.result == static_cast<std::uint32_t>(Ui3FixtureResult::Passed)
				&& final.completedSteps == Ui3FixtureExpectedSteps && final.unverifiedSteps == 0
				&& final.received >= 432 && final.received == final.enqueued && final.enqueued == final.consumed);
		}

		bool CreateEmptyOwnedFile(const std::wstring& path, FileIdentity* identity = nullptr)
		{
			Handle file(CreateFileW(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			BY_HANDLE_FILE_INFORMATION info{};
			if (!file.Valid() || !GetFileInformationByHandle(file.value, &info)) return false;
			if (identity) *identity = { info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow };
			return true;
		}
		struct PathFixture
		{
			std::wstring repository, source, leaf, copied;
			FileIdentity sourceIdentity, copiedIdentity;
			std::vector<std::wstring> layers;
		};
		bool MakePathFixture(const ParentRun& owner, unsigned index, PathFixture& fixture)
		{
			const std::wstring base = owner.master + L"\\path-" + std::to_wstring(index);
			fixture.repository = base + L"\\repo";
			const std::wstring sourceDir = fixture.repository + L"\\src";
			fixture.source = sourceDir + L"\\Inkeys.exe";
			const std::wstring results = fixture.repository + L"\\TestResults", evidence = results + L"\\release-hardening";
			const std::wstring master = evidence + L"\\ui3-finite-00000000000000000000000000000001";
			fixture.leaf = master + L"\\r1\\s1";
			const std::wstring bin = fixture.leaf + L"\\bin";
			fixture.copied = bin + L"\\Inkeys.exe";
			fixture.layers = { fixture.repository, fixture.repository + L"\\.trellis", results, evidence,
				master, master + L"\\r1", fixture.leaf, bin, bin + L"\\Inkeys", bin + L"\\Inkeys\\Config",
				bin + L"\\opt", bin + L"\\log", sourceDir };
			if (!Within(base, owner.master) || !CreateDirectoryW(base.c_str(), nullptr)) return false;
			for (const auto& path : fixture.layers) if (!CreateDirectoryW(path.c_str(), nullptr)) return false;
			// 这里只测试生产filesystem validator；空ordinary文件不冒充真实进程鉴权正例。
			return CreateEmptyOwnedFile(fixture.repository + L"\\InkeysRepo.sln")
				&& CreateEmptyOwnedFile(fixture.source, &fixture.sourceIdentity)
				&& CreateEmptyOwnedFile(fixture.copied, &fixture.copiedIdentity);
		}
		bool SetOwnedJunction(const std::wstring& path, const std::wstring& target, const std::wstring& master)
		{
			if (!Within(path, master) || !Within(target, master)) return false;
			const std::wstring substitute = L"\\??\\" + target;
			const std::size_t subBytes = substitute.size() * sizeof(wchar_t), printBytes = target.size() * sizeof(wchar_t);
			const std::size_t bytes = 16 + subBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
			if (bytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE || bytes - 8 > USHRT_MAX) return false;
			std::vector<unsigned char> data(bytes, 0);
			const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
			const WORD length = static_cast<WORD>(bytes - 8), subLength = static_cast<WORD>(subBytes);
			const WORD printOffset = static_cast<WORD>(subBytes + sizeof(wchar_t)), printLength = static_cast<WORD>(printBytes);
			std::memcpy(data.data(), &tag, sizeof(tag)); std::memcpy(data.data() + 4, &length, sizeof(length));
			std::memcpy(data.data() + 10, &subLength, sizeof(subLength));
			std::memcpy(data.data() + 12, &printOffset, sizeof(printOffset)); std::memcpy(data.data() + 14, &printLength, sizeof(printLength));
			std::memcpy(data.data() + 16, substitute.data(), subBytes); std::memcpy(data.data() + 16 + printOffset, target.data(), printBytes);
			Handle directory(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			DWORD returned = 0;
			return directory.Valid() && DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, data.data(),
				static_cast<DWORD>(data.size()), nullptr, 0, &returned, nullptr);
		}
		bool LinkOwnedEmptyFile(const std::wstring& path, const FileIdentity& createdIdentity,
			const std::wstring& target, const std::wstring& master)
		{
			if (!Within(path, master) || !Within(target, master)) return false;
			Detail::Ui3OwnedDirectoryProof proof;
			std::wstring parentPath = path;
			if (PopPart(parentPath).empty() || !LeasePath(parentPath, true, proof) || !LeasePath(target, false, proof)) return false;
			{
				Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
				BY_HANDLE_FILE_INFORMATION info{};
				if (!file.Valid() || !GetFileInformationByHandle(file.value, &info)
					|| (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
					|| info.nFileSizeHigh || info.nFileSizeLow || info.dwVolumeSerialNumber != createdIdentity.volume
					|| info.nFileIndexHigh != createdIdentity.high || info.nFileIndexLow != createdIdentity.low) return false;
				// 已关闭先前验证lease；在带DELETE的同一identity HANDLE上删除，避免路径换名窗口。
				FILE_DISPOSITION_INFO disposition{ TRUE };
				if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition))) return false;
			}
			return CreateSymbolicLinkW(path.c_str(), target.c_str(), 0) != FALSE;
		}
		bool RunPathTests()
		{
			ParentRun owner;
			if (!MakeParentRun(1, 1, owner)) return false;
			const std::wstring target = owner.master + L"\\junction-target";
			if (!CreateDirectoryW(target.c_str(), nullptr)) return false;
			bool passed = true;
			for (unsigned index = 0; index < 13; ++index)
			{
				PathFixture fixture;
				if (!MakePathFixture(owner, index, fixture)) return false;
				{
					Detail::Ui3OwnedDirectoryProof proof;
					if (!ValidateOwnedLeaf(fixture.leaf, fixture.source, fixture.source, fixture.copied, 1, 1, proof)) return false;
				} // 无运行owner；先关闭这份测试lease，才改本次create-new组件。
				if (!SetOwnedJunction(fixture.layers[index], target, owner.master))
				{
					std::printf("[UI3FixtureAuth] path-reparse-%u NOT_VERIFIED error=%lu\n", index, GetLastError());
					passed = false;
					continue;
				}
				Detail::Ui3OwnedDirectoryProof rejected;
				const bool refused = !ValidateOwnedLeaf(fixture.leaf, fixture.source, fixture.source, fixture.copied, 1, 1, rejected);
				std::printf("[UI3FixtureAuth] path-reparse-%u %s\n", index, refused ? "PASS" : "FAIL");
				passed = passed && refused;
			}
			PathFixture fixture;
			if (!MakePathFixture(owner, 13, fixture)) return false;
			const std::wstring other = fixture.repository + L"\\other.exe";
			if (!CreateEmptyOwnedFile(other)) return false;
			for (unsigned index = 0; index < 7; ++index)
			{
				std::wstring leaf = fixture.leaf, source = fixture.source, expected = fixture.source, child = fixture.copied;
				if (index == 0) leaf += L"\\extra";
				if (index == 1) leaf = fixture.repository + L"\\TestResults\\release-hardening\\ui3-finite-0000000000000000000000000000000A\\r1\\s1";
				if (index == 2) leaf = fixture.repository + L"\\TestResults\\release-hardening\\ui3-finite-00000000000000000000000000000001\\r1\\..\\r1\\s1";
				if (index == 3) leaf = fixture.repository + L"2\\TestResults\\release-hardening\\ui3-finite-00000000000000000000000000000001\\r1\\s1";
				if (index == 4) source = expected = owner.sourceImage; // 源必须在由leaf推导的同repo。
				if (index == 5) expected = other; // 同内容/不同file identity仍拒绝。
				if (index == 6) child = other;
				Detail::Ui3OwnedDirectoryProof proof;
				const bool refused = !ValidateOwnedLeaf(leaf, source, expected, child, 1, 1, proof);
				std::printf("[UI3FixtureAuth] path-shape-%u %s\n", index, refused ? "PASS" : "FAIL");
				passed = passed && refused;
			}
			const std::wstring linkTarget = owner.master + L"\\file-link-target";
			if (!CreateEmptyOwnedFile(linkTarget)) return false;
			for (unsigned index = 0; index < 2; ++index)
			{
				PathFixture fileFixture;
				if (!MakePathFixture(owner, 14 + index, fileFixture)) return false;
				const auto& path = index == 0 ? fileFixture.source : fileFixture.copied;
				const auto& identity = index == 0 ? fileFixture.sourceIdentity : fileFixture.copiedIdentity;
				if (!LinkOwnedEmptyFile(path, identity, linkTarget, owner.master))
				{
					std::printf("[UI3FixtureAuth] file-reparse-%u NOT_VERIFIED error=%lu\n", index, GetLastError());
					passed = false;
					continue;
				}
				Detail::Ui3OwnedDirectoryProof proof;
				const bool refused = !ValidateOwnedLeaf(fileFixture.leaf, fileFixture.source, fileFixture.source, fileFixture.copied, 1, 1, proof);
				std::printf("[UI3FixtureAuth] file-reparse-%u %s\n", index, refused ? "PASS" : "FAIL");
				passed = passed && refused;
			}
			return passed;
		}
		int RunAuthTests()
		{
			bool passed = RunPathTests();
			const Options options;
			for (const auto& test : std::initializer_list<std::pair<Negative, const char*>>{
				{ Negative::Magic, "magic" }, { Negative::Version, "version" }, { Negative::Bytes, "bytes" },
				{ Negative::Purpose, "purpose" }, { Negative::Scene, "scene" }, { Negative::Capture, "capture" },
				{ Negative::Capacity, "capacity" }, { Negative::Round, "round" }, { Negative::SourceVersion, "source-version" },
				{ Negative::Count, "source-count" }, { Negative::Hash, "source-hash" }, { Negative::Steps, "steps" },
				{ Negative::Nonce, "nonce" }, { Negative::InitialOutput, "initial-output" }, { Negative::ParentPid, "parent-pid" },
				{ Negative::ParentImage, "parent-image" }, { Negative::DuplicateHandles, "duplicate-handles" },
				{ Negative::NoInheritedHandles, "no-inherited-handles" }, { Negative::ArgumentCount, "argc" },
				{ Negative::DisplacedMode, "displaced-mode" }, { Negative::LeafRound, "leaf-round" }, { Negative::LeafScene, "leaf-scene" } })
			{
				const bool valid = RunParent(options, test.first, false, test.second);
				std::printf("[UI3FixtureAuth] %s %s\n", test.second, valid ? "PASS" : "FAIL");
				passed = passed && valid;
			}
			// 唯一正授权路径同样跑真实Bar；descriptor/runner未实现时不得链接成功stub。
			const bool positive = RunParent(options, Negative::None, false, "authorized-main-fold-off");
			std::printf("[UI3FixtureAuth] authorized-main-fold-off %s (authorization only; benchmark verdict separate)\n", positive ? "PASS" : "FAIL");
			return passed && positive ? 0 : 1;
		}
	}

	namespace
	{
		bool ModeWord(const wchar_t* argument, const wchar_t* mode) noexcept
		{
			return CompareStringOrdinal(argument, -1, mode, -1, TRUE) == CSTR_EQUAL;
		}
		bool RawModeWord(const wchar_t* command, const wchar_t* mode) noexcept
		{
			const std::size_t length = wcslen(command), modeLength = wcslen(mode);
			const auto boundary = [](wchar_t ch) noexcept { return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'"'; };
			for (std::size_t offset = 0; offset + modeLength <= length; ++offset)
				if ((!offset || boundary(command[offset - 1])) && (offset + modeLength == length || boundary(command[offset + modeLength]))
					&& CompareStringOrdinal(command + offset, static_cast<int>(modeLength), mode,
					static_cast<int>(modeLength), TRUE) == CSTR_EQUAL) return true;
			return false;
		}
	}
	bool TryRunUi3PresentationFixtureEarly(LPCWSTR fullCommandLine, int& exitCode) noexcept
	{
		if (!fullCommandLine) return false;
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(fullCommandLine, &argc);
		if (!argv)
		{
			const bool rawMode = RawModeWord(fullCommandLine, kChildMode) || RawModeWord(fullCommandLine, kBenchmarkMode) || RawModeWord(fullCommandLine, kAuthTestMode);
			if (rawMode) exitCode = kArgumentsRejected;
			return rawMode;
		}
		// 内部word即使因错误引用被并入其它argv也拒绝；不能凭解析成功落入普通Main。
		bool recognized = RawModeWord(fullCommandLine, kChildMode);
		for (int index = 0; index < argc; ++index)
			if (ModeWord(argv[index], kChildMode) || ModeWord(argv[index], kBenchmarkMode) || ModeWord(argv[index], kAuthTestMode))
				recognized = true;
		if (recognized)
		{
			exitCode = kArgumentsRejected;
			try
			{
				const wchar_t* mode = argc > 1 ? argv[1] : L"";
				if (wcscmp(mode, kChildMode) == 0) exitCode = Ui3FixtureAuthorizer::RunChild(argc, argv);
				else if (wcscmp(mode, kBenchmarkMode) == 0)
				{
					Options options;
					if (ParseOptions(argc, argv, options)) exitCode = RunParent(options, Negative::None, true, "benchmark") ? 0 : kFixtureFailed;
				}
				else if (wcscmp(mode, kAuthTestMode) == 0 && argc == 2) exitCode = RunAuthTests();
			}
			catch (...) { exitCode = kFixtureFailed; }
		}
		LocalFree(argv);
		return recognized; // 任一位置出现内部word却形状不符也early拒绝，不掉入普通GUI。
	}
}
