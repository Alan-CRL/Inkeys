#pragma once

#include <array>
#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	// 仅严格不可见 fixture 的 owner 事实；不能注入 completion 或绕过恢复资格。
	enum class HiddenDesktopRecoveryEvent : std::uint8_t
	{
		CommittedConsumed,
		LoadRequested,
		LoadedInstalled,
		MemoryRestored,
	};
	struct HiddenDesktopRecoveryReceipt
	{
		HiddenDesktopRecoveryEvent event = HiddenDesktopRecoveryEvent::CommittedConsumed;
		std::array<std::uint8_t, 16> file{}, workspace{}, page{};
		bool cpuFallbackReleased = false;
	};
	struct HiddenDesktopRecoverySnapshot
	{
		std::uint64_t committedConsumed = 0, loadRequested = 0;
		std::uint64_t loadedInstalled = 0, memoryRestored = 0;
		HiddenDesktopRecoveryReceipt last;
	};
#endif
	int RunScrollFollowPersistenceTest() noexcept;
}
