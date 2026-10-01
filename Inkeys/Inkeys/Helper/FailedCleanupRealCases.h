#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <type_traits>

namespace Inkeys::Shutdown
{
	// 仅 exact-auth copied child 使用；不接受任意故障、文件路径或外部进程。
	enum class CleanupRealCase : DWORD
	{
		None, PresenterHold, PresenterRelease, WindowBeforeHold, WindowBeforeRelease,
		WindowCreatedHold, WindowCreatedRelease, MainUlw, RenderHold, RenderRelease,
		DesktopHold, DesktopRelease, PptHold, PptRelease, NaturalClose,
		ReadDesktopCommitted, ReadPptCommitted, ReadPptForeignSession
	};

	enum class CleanupRealStage : LONG
	{
		None, Authorized, WindowReady, HostReady, BaselineReady, FaultReached,
		ClosePublished, StopReturned, ReaderDone, Finished, PrerequisiteFailed
	};

	constexpr DWORD kCleanupRealMagic = 0x1430FA04;
	constexpr DWORD kCleanupRealVersion = 1;

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

	struct alignas(8) CleanupRealReceipt
	{
		BYTE fileGuid[16], workspaceGuid[16], pageGuid[16], presentationKey[16];
		ULONGLONG mutationRevision, bindingRevision, targetRevision, sessionRevision;
		ULONGLONG sequenceInSession, dailySequence, intervalOrdinal, uinkLength;
		DWORD workspaceType, pageIndex, totalPages, bindingMode;
		DWORD pageKind, processLocalIdentity, strokeCount, pointCount;
		LONG slideIds[4];
		char storageSession[40], localDate[16];
		BYTE indexSha256[32], uinkSha256[32], geometrySha256[32];
		DWORD deviceCount, activeCanvasCount, retainedCanvasCount, desktopTrigger;
		BYTE reserved[8];
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
	static_assert(sizeof(CleanupRealReceipt) == 352);
	static_assert(sizeof(CleanupRealPacket) == 1024 && alignof(CleanupRealPacket) == 8);
	static_assert(offsetof(CleanupRealPacket, trace) == 64);
	static_assert(offsetof(CleanupRealPacket, expected) == 320);
	static_assert(offsetof(CleanupRealPacket, observed) == 672);

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
