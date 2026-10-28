#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>

namespace Inkeys::Window::Detail
{
	inline constexpr std::size_t MissingZOrder = (std::numeric_limits<std::size_t>::max)();

	template<std::size_t Count>
	struct ZOrderSnapshot
	{
		std::array<HWND, Count> windows{};
		std::array<HWND, Count> owners{};
		std::array<std::uint64_t, Count> generations{};
		std::array<std::size_t, Count> ordinal{};
		std::array<bool, Count> topmost{};
		bool continuous = true;
		ZOrderSnapshot() { ordinal.fill(MissingZOrder); }
		[[nodiscard]] bool Above(std::size_t above, std::size_t below) const noexcept
		{
			return windows[above] && windows[below]
				&& ordinal[above] != MissingZOrder && ordinal[below] != MissingZOrder
				&& ordinal[above] < ordinal[below];
		}
		[[nodiscard]] bool Matches(const std::array<HWND, Count>& expectedOwners,
			bool expectedTopmost, bool requireContinuous = true) const noexcept
		{
			if (requireContinuous && !continuous) return false;
			for (std::size_t i = 0; i < Count; ++i)
			{
				if (!windows[i]) continue;
				if (ordinal[i] == MissingZOrder || owners[i] != expectedOwners[i]
					|| topmost[i] != expectedTopmost) return false;
				for (std::size_t owner = 0; owner < Count; ++owner)
					if (windows[owner] && windows[owner] == expectedOwners[i] && !Above(i, owner)) return false;
			}
			return true;
		}
	};

	struct PptZOrder
	{
		std::array<std::size_t, 4> members{};
		std::size_t count = 0;
		bool valid = true;
	};

	template<std::size_t Count>
	[[nodiscard]] PptZOrder CapturePptZOrder(const ZOrderSnapshot<Count>& snapshot, HWND promoted = nullptr)
	{
		static_assert(Count >= 9);
		PptZOrder order;
		for (std::size_t i = 4; i < 8; ++i) if (snapshot.windows[i]) order.members[order.count++] = i;
		std::sort(order.members.begin(), order.members.begin() + order.count,
			[&](std::size_t a, std::size_t b) { return snapshot.ordinal[a] < snapshot.ordinal[b]; });
		if (promoted)
		{
			order.valid = false;
			for (std::size_t i = 0; i < order.count; ++i)
				if (snapshot.windows[order.members[i]] == promoted)
				{
					std::rotate(order.members.begin(), order.members.begin() + i, order.members.begin() + i + 1);
					order.valid = true;
					break;
				}
		}
		return order;
	}

	template<std::size_t Count>
	[[nodiscard]] bool SameZOrderIdentity(const ZOrderSnapshot<Count>& a, const ZOrderSnapshot<Count>& b) noexcept
	{
		return a.windows == b.windows && a.generations == b.generations;
	}

	template<std::size_t Count>
	[[nodiscard]] ZOrderSnapshot<Count> RetainSurvivingZOrder(const ZOrderSnapshot<Count>& saved,
		const ZOrderSnapshot<Count>& current, const std::array<HWND, Count>& expectedOwners)
	{
		auto identity = saved;
		// 角色按 owner 在前的顺序保存；父代际退休后，旧子树也不再接受旧恢复写入。
		for (std::size_t i = 0; i < Count; ++i)
		{
			bool retired = saved.windows[i] != current.windows[i] || saved.generations[i] != current.generations[i];
			for (std::size_t owner = 0; !retired && owner < i; ++owner)
				if (saved.windows[owner] && expectedOwners[i] == saved.windows[owner] && !identity.windows[owner]) retired = true;
			if (retired)
			{
				identity.windows[i] = identity.owners[i] = nullptr;
				identity.generations[i] = 0;
				identity.ordinal[i] = MissingZOrder;
			}
		}
		return identity;
	}

	// 白板及纯层级回滚只收敛内部关系；不要求获取前台许可，也不抬高 Bar。
	// Ops 仅提供原生采样/写入，决策和最终读回由生产与无窗口测试共享。
	template<std::size_t Count, class Ops>
	[[nodiscard]] bool ReconcileRelativeOverlayZOrder(const ZOrderSnapshot<Count>& identity,
		const std::array<HWND, Count>& expectedOwners, bool topmost, const PptZOrder& order, Ops& ops)
	{
		static_assert(Count >= 9);
		if (!order.valid || order.count > order.members.size()) return false;
		for (std::size_t i = 0; i < order.count; ++i)
		{
			const auto member = order.members[i];
			if (member < 4 || member >= 8 || !identity.windows[member]) return false;
			for (std::size_t j = 0; j < i; ++j) if (order.members[j] == member) return false;
		}
		std::size_t presentPpt = 0;
		for (std::size_t i = 4; i < 8; ++i) if (identity.windows[i]) ++presentPpt;
		if (presentPpt != order.count) return false;
		ZOrderSnapshot<Count> actual;
		const auto read = [&]
		{
			return ops.Current() && ops.Read(actual) && ops.Current() && SameZOrderIdentity(identity, actual);
		};
		for (unsigned attempt = 0; attempt < 2; ++attempt)
		{
			if (!read()) return false;
			for (std::size_t i = 0; i < 9; ++i)
			{
				if (!identity.windows[i] || actual.owners[i] == expectedOwners[i]) continue;
				if (!ops.Current() || !ops.SetOwner(i, expectedOwners[i]) || !read()
					|| actual.owners[i] != expectedOwners[i]) return false;
			}
			// owner 的 band 可联动 owned 窗口；只在实际漂移时修正，正常后台不触碰 Bar。
			std::size_t root = Count;
			for (std::size_t i = 0; i < 9; ++i)
				if (identity.windows[i] && !expectedOwners[i]) { root = i; break; }
			if (root < Count && actual.topmost[root] != topmost)
				if (!ops.Current() || !ops.SetBand(root, topmost, false) || !read()) return false;
			for (std::size_t i = 0; i < 9; ++i)
				if (identity.windows[i] && actual.topmost[i] != topmost)
					if (!ops.Current() || !ops.SetBand(i, topmost, true) || !read()) return false;
			const auto repairOwnedAbove = [&](std::size_t begin, std::size_t end)
			{
				for (std::size_t i = begin; i-- > end;)
				{
					if (!identity.windows[i] || !expectedOwners[i]) continue;
					std::size_t owner = Count;
					for (std::size_t j = 0; j < i; ++j)
						if (identity.windows[j] == expectedOwners[i]) { owner = j; break; }
					if (owner == Count || actual.Above(i, owner)) continue;
					if (!ops.Current() || !ops.PlaceAfter(owner, i) || !read()) return false;
				}
				return true;
			};
			if (!repairOwnedAbove(4, 1) || !repairOwnedAbove(9, 4)) return false;
			std::size_t anchor = identity.windows[8] ? 8 : Count;
			for (std::size_t i = 0; i < order.count; ++i)
			{
				const std::size_t member = order.members[i];
				if (anchor < Count && actual.Above(member, anchor))
				{
					if (!ops.Current() || !ops.PlaceAfter(member, anchor) || !read()) return false;
				}
				anchor = member;
			}
			if (!read()) return false;
			bool ordered = actual.Matches(expectedOwners, topmost, false);
			for (std::size_t i = 4; ordered && i < 8; ++i)
				if (identity.windows[i] && identity.windows[8]) ordered = actual.Above(8, i);
			for (std::size_t i = 1; ordered && i < order.count; ++i)
				ordered = actual.Above(order.members[i-1], order.members[i]);
			if (ordered && ops.Current()) return true;
		}
		return false;
	}

	// 政策交接只提交真实内部关系；全局置前仍由普通 Refresh 独立处理。
	template<class Capture, class Reconcile, class Publish, class Current>
	[[nodiscard]] bool CommitOverlayGroupPolicy(Capture capture, Reconcile reconcile,
		Publish publish, Current current, HWND promoted = nullptr)
	{
		const auto identity = capture();
		if (!identity || !current() || (promoted && !identity->windows[8])) return false;
		if (!reconcile(*identity, CapturePptZOrder(*identity, promoted)) || !current()) return false;
		publish();
		return current();
	}
}
