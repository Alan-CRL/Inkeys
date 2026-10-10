#include "Bar.Presentation.Source.h"

#include <bit>
#include <chrono>
#include <climits>
#include <cmath>
#include <limits>
#include <ratio>

namespace Inkeys::UI::Bar
{
	namespace
	{
		using ScheduleRatio = std::ratio_divide<std::milli, std::chrono::steady_clock::period>;
		constexpr std::int64_t ScheduleTicks(std::uint64_t milliseconds) noexcept
		{
			constexpr auto max = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
			return milliseconds <= max / ScheduleRatio::num
				? static_cast<std::int64_t>(milliseconds * ScheduleRatio::num / ScheduleRatio::den) : -1;
		}
		static_assert(ScheduleTicks(217000) > ScheduleTicks(216000));
		static_assert(ScheduleTicks(20) > 0 && ScheduleTicks(1000) > ScheduleTicks(20));

		template<std::size_t Count>
		constexpr std::array<Ui3FixtureInputV1, Count> MakeInputs(Ui3FiniteScene scene) noexcept
		{
			std::array<Ui3FixtureInputV1, Count> rows{};
			const bool draw = scene == Ui3FiniteScene::DrawAttribute;
			const std::size_t first = draw ? 2 : 0;
			for (std::size_t i = 0; i < Count; ++i)
			{
				auto& row = rows[i];
				row.index = static_cast<std::uint32_t>(i);
				row.sourceSequence = i + 1;
				if (draw && i < 2)
				{
					row.stepId = Ui3FixtureSetupStep;
					row.phase = i == 0 ? 1u : 3u;
					row.anchor = 1;
					row.flags = 1u | Ui3FixtureSetupOnlyIfFolded | (1u << 8);
					row.dueOffsetTicks = ScheduleTicks(i * 20);
				}
				else if (i + 1 == Count)
				{
					row.phase = 4;
					row.anchor = draw ? 2u : 1u;
					row.flags = 4u;
					row.dueOffsetTicks = ScheduleTicks((216u + (draw ? 1u : 0u)) * 1000);
				}
				else
				{
					const auto pair = (i - first) / 2;
					row.stepId = static_cast<std::uint32_t>(pair + 1);
					row.phase = (i - first) % 2 == 0 ? 1u : 3u;
					row.anchor = draw ? 2u : 1u;
					row.flags = (pair < Ui3FixtureWarmSteps ? 2u : 3u) | ((draw ? 2u : 1u) << 8);
					row.dueOffsetTicks = ScheduleTicks((pair + (draw ? 1u : 0u)) * 1000
						+ ((i - first) % 2) * 20);
				}
			}
			return rows;
		}
		constexpr auto mainInputs = MakeInputs<433>(Ui3FiniteScene::MainFold);
		constexpr auto drawInputs = MakeInputs<435>(Ui3FiniteScene::DrawAttribute);
		void HashLittleEndian(std::uint64_t& hash, std::uint64_t value, unsigned bytes) noexcept
		{
			for (unsigned i = 0; i < bytes; ++i)
			{
				hash = (hash ^ (value & 0xFFu)) * 1099511628211ULL;
				value >>= 8;
			}
		}
	}

	std::span<const Ui3FixtureInputV1> CompiledUi3FixtureInputs(Ui3FiniteScene scene) noexcept
	{
		if (scene == Ui3FiniteScene::MainFold) return mainInputs;
		if (scene == Ui3FiniteScene::DrawAttribute) return drawInputs;
		return {};
	}
	std::size_t CompiledUi3FixtureStorageBytes() noexcept { return sizeof(mainInputs) + sizeof(drawInputs); }

	std::uint64_t HashUi3FixtureInputs(Ui3FiniteScene scene, std::span<const Ui3FixtureInputV1> rows) noexcept
	{
		const auto compiled = CompiledUi3FixtureInputs(scene);
		if (compiled.empty() || rows.size() != compiled.size()) return 0;
		std::uint64_t hash = 14695981039346656037ULL;
		HashLittleEndian(hash, Ui3FixtureSourceVersion, 4);
		HashLittleEndian(hash, static_cast<std::uint32_t>(scene), 4);
		HashLittleEndian(hash, rows.size(), 4);
		HashLittleEndian(hash, Ui3FixtureExpectedSteps, 8);
		HashLittleEndian(hash, std::chrono::steady_clock::period::num, 8);
		HashLittleEndian(hash, std::chrono::steady_clock::period::den, 8);
		// 逐逻辑字段 LE 编码，绝不把编译器 padding 当作来源 hash。
		for (const auto& row : rows)
		{
			HashLittleEndian(hash, row.index, 4); HashLittleEndian(hash, row.stepId, 4);
			HashLittleEndian(hash, row.phase, 4); HashLittleEndian(hash, row.anchor, 4);
			HashLittleEndian(hash, static_cast<std::uint32_t>(row.offsetXDip), 4);
			HashLittleEndian(hash, static_cast<std::uint32_t>(row.offsetYDip), 4);
			HashLittleEndian(hash, row.flags, 4); HashLittleEndian(hash, row.reserved0, 4);
			HashLittleEndian(hash, static_cast<std::uint64_t>(row.dueOffsetTicks), 8);
			HashLittleEndian(hash, row.sourceSequence, 8);
			HashLittleEndian(hash, row.expectedBaseCommitSerial, 8); HashLittleEndian(hash, row.reserved1, 8);
		}
		return hash;
	}
	Ui3FixtureSourceDescriptorV1 GetCompiledUi3FixtureSourceV1(Ui3FiniteScene scene) noexcept
	{
		const auto rows = CompiledUi3FixtureInputs(scene);
		if (rows.empty()) return {};
		return { Ui3FixtureSourceVersion, static_cast<std::uint32_t>(rows.size()),
			HashUi3FixtureInputs(scene, rows), Ui3FixtureExpectedSteps };
	}
	bool IsUi3FixtureInputValid(Ui3FiniteScene scene, const Ui3FixtureInputV1& row, std::size_t index) noexcept
	{
		const auto rows = CompiledUi3FixtureInputs(scene);
		if (index >= rows.size()) return false;
		const auto& expected = rows[index];
		return row.index == expected.index && row.stepId == expected.stepId && row.phase == expected.phase
			&& row.anchor == expected.anchor && row.offsetXDip == expected.offsetXDip && row.offsetYDip == expected.offsetYDip
			&& row.flags == expected.flags && row.reserved0 == 0 && row.reserved1 == 0
			&& row.expectedBaseCommitSerial == 0 && row.dueOffsetTicks == expected.dueOffsetTicks
			&& row.sourceSequence == expected.sourceSequence;
	}
	bool IsUi3FixtureActionAllowed(const Ui3FixtureInputV1& row, Ui3FiniteScene action, Ui3FixtureActionPoint point) noexcept
	{
		const auto phase = row.flags & 7u;
		const auto expected = (row.flags >> 8) & 3u;
		return phase >= 1 && phase <= 3 && (row.flags & ~0x30Fu) == 0 && row.reserved0 == 0 && row.reserved1 == 0
			&& row.expectedBaseCommitSerial == 0 && row.sourceSequence == static_cast<std::uint64_t>(row.index) + 1
			&& (point == Ui3FixtureActionPoint::Down || point == Ui3FixtureActionPoint::Commit || point == Ui3FixtureActionPoint::Callback)
			&& expected == static_cast<std::uint32_t>(action)
			&& ((action == Ui3FiniteScene::MainFold && row.anchor == 1)
				|| (action == Ui3FiniteScene::DrawAttribute && row.anchor == 2))
			&& row.phase == (point == Ui3FixtureActionPoint::Down ? 1u : 3u);
	}
	bool TryBindUi3FixturePoint(const Ui3FixtureInputV1& row, const Ui3FixtureReadyValue& ready,
		std::uint64_t generation, Ui3FixtureBoundPoint& out) noexcept
	{
		constexpr auto required = Ui3FiniteReadyRegistered | Ui3FiniteReadyInteraction
			| Ui3FiniteReadyTransaction | Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors;
		if (row.phase != 1 || row.expectedBaseCommitSerial != 0 || row.reserved0 || row.reserved1
			|| row.offsetXDip || row.offsetYDip || !generation || ready.generation != generation || (ready.flags & ~0x3Fu)
			|| (ready.flags & (required | Ui3FiniteReadyStopped)) != required || !ready.committedCount
			|| !ready.lastCommittedAttempt || !ready.epoch || !ready.surfaceSerial || !ready.anchorMappingSerial || (ready.anchorMappingSerial & 1u)
			|| !ready.targetWidth || !ready.targetHeight || !IsUi3FiniteSignatureValid(ready.initialStableSignature)) return false;
		const auto* bits = row.anchor == 1 ? ready.mainAnchorBits : row.anchor == 2 ? ready.drawAnchorBits : nullptr;
		if (!bits) return false;
		const double scale = std::bit_cast<double>(ready.initialStableSignature.configZoomBits)
			* ready.initialStableSignature.dpi / 96.0;
		const double x = std::bit_cast<double>(bits[0]) + row.offsetXDip * scale;
		const double y = std::bit_cast<double>(bits[1]) + row.offsetYDip * scale;
		if (!std::isfinite(x) || !std::isfinite(y) || x < SHRT_MIN || x > SHRT_MAX || y < SHRT_MIN || y > SHRT_MAX) return false;
		out = { ready.committedCount, ready.lastCommittedAttempt, ready.epoch, ready.surfaceSerial,
			ready.anchorMappingSerial, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) };
		return true;
	}
	bool SameUi3FixtureWire(const Ui3FixtureWireValue& a, const Ui3FixtureWireValue& b) noexcept
	{
		return a.hwnd == b.hwnd && a.category == b.category && a.message == b.message && a.buttons == b.buttons && a.modifiers == b.modifiers
			&& a.x == b.x && a.y == b.y && a.wheel == b.wheel;
	}
}
