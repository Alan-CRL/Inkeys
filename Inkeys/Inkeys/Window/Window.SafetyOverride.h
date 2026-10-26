#pragma once

#include <windows.h>
#include <cstdint>

namespace Inkeys::Window::Detail
{
	// 失败恢复期间只允许单调关闭输入/显隐；旧快照不得撤销后来接受的安全覆盖。
	struct WindowSafetyOverride
	{
		std::uint64_t generation = 0;
		bool hidden = false;
		bool clickThrough = false;
		void Latch(std::uint64_t current, bool hide, bool transparent) noexcept
		{
			if (generation != current) { generation = current; hidden = clickThrough = false; }
			hidden |= hide;
			clickThrough |= transparent;
		}
		[[nodiscard]] bool Hidden(std::uint64_t current) const noexcept { return generation == current && hidden; }
		[[nodiscard]] bool ClickThrough(std::uint64_t current) const noexcept { return generation == current && clickThrough; }
		[[nodiscard]] bool Visible(std::uint64_t current, bool original) const noexcept { return original && !Hidden(current); }
		[[nodiscard]] LONG_PTR Style(std::uint64_t current, LONG_PTR original) const noexcept
		{
			return ClickThrough(current) ? original | WS_EX_TRANSPARENT : original;
		}
		void AllowShow(std::uint64_t current) noexcept { if (generation == current) hidden = false; }
		void AllowInput(std::uint64_t current) noexcept { if (generation == current) clickThrough = false; }
	};
}
