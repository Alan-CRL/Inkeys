#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace Inkeys::Shutdown
{
	// 仅 exact-auth copied child 使用；不接受任意故障、文件路径或外部进程。
	enum class CleanupRealCase : DWORD
	{
		None, PresenterHold, PresenterRelease, WindowBeforeHold, WindowBeforeRelease,
		WindowCreatedHold, WindowCreatedRelease, MainUlw, RenderHold, RenderRelease,
		DesktopHold, DesktopRelease, PptHold, PptRelease, NaturalClose,
		ReadDesktopCommitted, ReadPptCommitted, ReadPptFreshStore
	};

	enum class CleanupRealStage : LONG
	{
		None, Authorized, WindowReady, HostReady, BaselineReady, FaultReached,
		ClosePublished, StopReturned, ReaderDone, Finished, PrerequisiteFailed
	};

	constexpr DWORD kCleanupRealMagic = 0x1430FA04;
	constexpr DWORD kCleanupRealVersion = 2;

	struct alignas(8) CleanupRealHeader
	{
		DWORD magic, version, bytes, scenario;
		DWORD issuerParentPid, originChildPid, expectedIntent, reservedFlags;
		BYTE nonce[16];
		volatile LONG authorized, result, stage, error;
	};

	struct alignas(8) CleanupRealTrace
	{
		ULONGLONG childStartTick, failureTick, gateTick, graceDeadline;
		ULONGLONG closeRequestTick, ordinaryDeadline, fatalPublishTick, firstStartFalseTick;
		ULONGLONG oldJoinTick, firstSuccessTick, postGraceStrokeTick, stopEnterTick;
		ULONGLONG stopReturnTick, oldDrawpad, oldPresentation, newDrawpad;
		ULONGLONG newPresentation, baselineSuccessPresents, gateSuccessPresents;
		ULONGLONG afterGraceSuccessPresents, saveAccepted, saveCommitted, saveFailed, savePending;
		ULONGLONG oldGeneration, newGeneration;
		DWORD ownerThreadIds[4];
		volatile LONG faultReached, startReturned, stopReturned, oldSignalCancelled;
		volatile LONG oldChainDestroyed, intentObserved, armState, readerSucceeded;
	};

	enum class CleanupDesktopInkSample : DWORD { None, A, B };
	constexpr DWORD kCleanupDesktopImplicitDevice = 1u;
	constexpr DWORD kCleanupDesktopImplicitWorkspace = 2u;

	// 只接收桌面发布器的相对路径形态；目录、文件租约仍由读回进程校验。
	constexpr bool ValidCleanupDesktopPath(std::string_view path) noexcept
	{
		if (path.size() < 42 || path.size() > 46 || !path.starts_with("desktop\\") ||
			!path.ends_with(".uink") || path[12] != '-' || path[15] != '-' ||
			path[18] != '\\' || path[28] != '_') return false;
		const auto digit = [](char c) noexcept { return c >= '0' && c <= '9'; };
		for (size_t i = 8; i < 18; ++i)
			if (i != 12 && i != 15 && !digit(path[i])) return false;
		for (size_t i = 19; i < 28; ++i)
			if (!digit(path[i])) return false;
		for (size_t i = 29; i < 37; ++i)
			if (!digit(path[i]) && (path[i] < 'a' || path[i] > 'f')) return false;
		const auto value = [&](size_t index, size_t length) noexcept
		{
			unsigned result = 0;
			for (size_t i = index; i < index + length; ++i) result = result * 10 + (path[i] - '0');
			return result;
		};
		const unsigned year = value(8, 4), month = value(13, 2), day = value(16, 2);
		if (!year || month < 1 || month > 12 || !day ||
			value(19, 2) > 23 || value(21, 2) > 59 || value(23, 2) > 59) return false;
		constexpr unsigned days[]{ 31,28,31,30,31,30,31,31,30,31,30,31 };
		const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
		if (day > days[month - 1] + (month == 2 && leap ? 1u : 0u)) return false;
		if (path.size() == 42) return true;
		if (path.size() == 43 || path[37] != '_' || path[38] == '0') return false;
		const size_t suffixEnd = path.size() - 5;
		for (size_t i = 38; i < suffixEnd; ++i)
			if (!digit(path[i])) return false;
		return value(38, suffixEnd - 38) <= 100;
	}

	struct alignas(8) CleanupRealReceipt
	{
		BYTE fileGuid[16], workspaceGuid[16], pageGuid[16], presentationKey[16]; // workspaceGuid 仅 PPT 填写。
		ULONGLONG mutationRevision, bindingRevision, targetRevision, sessionRevision; // PPT 的 mutationRevision 保留为 0。
		ULONGLONG intervalOrdinal, uinkLength;
		DWORD workspaceType, pageIndex, totalPages, bindingMode;
		DWORD pageKind, processLocalIdentity, strokeCount, pointCount;
		LONG slideIds[4];
		// 桌面发布回执提供临时根下的相对路径，PPT 保持为全零。
		char desktopRelativePath[64];
		BYTE uinkSha256[32], geometrySha256[32];
		DWORD deviceCount, activeCanvasCount, retainedCanvasCount, desktopTopology;
		CleanupDesktopInkSample desktopInkSample;
		BYTE reserved[12];
	};

	struct alignas(8) CleanupRealPacket
	{
		CleanupRealHeader header;
		CleanupRealTrace trace;
		CleanupRealReceipt expected;
		CleanupRealReceipt observed;
	};

	static_assert(std::is_trivial_v<CleanupRealPacket>);
	static_assert(std::is_standard_layout_v<CleanupRealPacket>);
	static_assert(sizeof(CleanupRealHeader) == 64);
	static_assert(offsetof(CleanupRealHeader, authorized) == 48);
	static_assert(sizeof(CleanupRealTrace) == 256);
	static_assert(sizeof(CleanupRealReceipt) == 320);
	static_assert(sizeof(CleanupRealPacket) == 960 && alignof(CleanupRealPacket) == 8);
	static_assert(offsetof(CleanupRealPacket, trace) == 64);
	static_assert(offsetof(CleanupRealPacket, expected) == 320);
	static_assert(offsetof(CleanupRealPacket, observed) == 640);

	// 普通进程内 capability，不在映射里传指针；root authorizer 持有文件/映射 lease 到真 join。
	struct VerifiedCleanupRealLaunch
	{
		CleanupRealCase scenario = CleanupRealCase::None;
		CleanupRealPacket* packet = nullptr;
		std::wstring directory;
	};

	[[nodiscard]] bool IsAuthorizedCleanupRealLaunch(const VerifiedCleanupRealLaunch& launch) noexcept;
	ULONGLONG PublishAuthorizedCleanupFatal() noexcept;
	// 仅C07最终普通Close与普通 Armed 的 render/save cases，直接走正式 SetOffSignal(1)。
	bool RunAuthorizedCleanupClose(const VerifiedCleanupRealLaunch& launch) noexcept;
	void PublishCleanupRealStage(CleanupRealPacket& packet, CleanupRealStage stage) noexcept;

	// 真实模块夹具和 Main 同一回退 span 分属唯一 writer，均只在 early 鉴权后调用。
	int RunAuthorizedFailedCleanupRealFixture(const VerifiedCleanupRealLaunch& launch) noexcept;
	int RunMainFailedCleanupUlwCounterexample(const VerifiedCleanupRealLaunch& launch) noexcept;
}
