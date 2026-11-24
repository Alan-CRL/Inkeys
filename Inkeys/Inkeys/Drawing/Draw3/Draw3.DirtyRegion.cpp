module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <windows.h>

module Inkeys.Drawing.Draw3.dirty_region;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		bool IsEmpty(const RECT& rect) noexcept
		{
			return rect.right <= rect.left || rect.bottom <= rect.top;
		}

		RECT Intersection(const RECT& a, const RECT& b) noexcept
		{
			return { (std::max)(a.left, b.left), (std::max)(a.top, b.top),
				(std::min)(a.right, b.right), (std::min)(a.bottom, b.bottom) };
		}

		RECT BoundingRect(const RECT& a, const RECT& b) noexcept
		{
			return { (std::min)(a.left, b.left), (std::min)(a.top, b.top),
				(std::max)(a.right, b.right), (std::max)(a.bottom, b.bottom) };
		}

		// 完整 Win32 坐标范围的矩形面积能用无符号 64 位整数精确表示。
		std::uint64_t RectArea(const RECT& rect) noexcept
		{
			const auto width = static_cast<long long>(rect.right) - rect.left;
			const auto height = static_cast<long long>(rect.bottom) - rect.top;
			return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
		}

		struct Waste
		{
			std::uint64_t magnitude;
			bool negative;
		};

		Waste PairWaste(const RECT& a, const RECT& b) noexcept
		{
			// 外接矩形包含 a，先减去 a，再单独保存差值的符号。
			const auto remainder = RectArea(BoundingRect(a, b)) - RectArea(a);
			const auto areaB = RectArea(b);
			if (remainder < areaB) return { areaB - remainder, true };
			return { remainder - areaB, false };
		}

		bool LessWaste(const Waste& a, const Waste& b) noexcept
		{
			if (a.negative != b.negative) return a.negative;
			return a.negative ? a.magnitude > b.magnitude : a.magnitude < b.magnitude;
		}

		struct RectPair
		{
			std::size_t first;
			std::size_t second;
			Waste waste;
		};

		RectPair FindLeastWastePair(std::span<const RECT> rects) noexcept
		{
			RectPair best{ 0, 1, PairWaste(rects[0], rects[1]) };
			for (std::size_t first = 0; first + 1 < rects.size(); ++first)
			{
				for (std::size_t second = first + 1; second < rects.size(); ++second)
				{
					const auto waste = PairWaste(rects[first], rects[second]);
					if (LessWaste(waste, best.waste)) best = { first, second, waste };
				}
			}
			return best;
		}

		void MergePair(RECT* rects, std::size_t& count, std::size_t first, std::size_t second) noexcept
		{
			rects[first] = BoundingRect(rects[first], rects[second]);
			for (std::size_t i = second + 1; i < count; ++i) rects[i - 1] = rects[i];
			--count;
		}

		void MergeOverlaps(RECT* rects, std::size_t& count) noexcept
		{
			while (true)
			{
				bool merged = false;
				for (std::size_t first = 0; first + 1 < count && !merged; ++first)
				{
					for (std::size_t second = first + 1; second < count; ++second)
					{
						if (IsEmpty(Intersection(rects[first], rects[second]))) continue;
						MergePair(rects, count, first, second);
						merged = true;
						break;
					}
				}
				// 新外接矩形可能与先前已检查的矩形相交。
				if (!merged) return;
			}
		}
	}

	void DirtyRegion::Clear() noexcept
	{
		count_ = 0;
	}

	void DirtyRegion::Add(const RECT& rect) noexcept
	{
		if (IsEmpty(rect)) return;
		if (count_ < kCapacity)
		{
			rects_[count_++] = rect;
			return;
		}

		std::array<RECT, kCapacity + 1> candidates;
		std::copy_n(rects_.data(), count_, candidates.data());
		candidates[count_] = rect;
		auto candidateCount = candidates.size();
		const auto best = FindLeastWastePair(candidates);
		MergePair(candidates.data(), candidateCount, best.first, best.second);
		std::copy_n(candidates.data(), candidateCount, rects_.data());
	}

	void DirtyRegion::Add(const DirtyRegion& other) noexcept
	{
		if (this == &other) return;
		for (const auto& rect : other.Rects()) Add(rect);
	}

	bool DirtyRegion::Empty() const noexcept
	{
		return count_ == 0;
	}

	RECT DirtyRegion::Bounds() const noexcept
	{
		if (Empty()) return {};
		RECT bounds = rects_[0];
		for (std::size_t i = 1; i < count_; ++i) bounds = BoundingRect(bounds, rects_[i]);
		return bounds;
	}

	long long DirtyRegion::Area() const noexcept
	{
		constexpr auto kAreaLimit = (std::numeric_limits<long long>::max)();
		long long area = 0;
		for (const auto& rect : Rects())
		{
			const auto rectArea = RectArea(rect);
			if (rectArea > static_cast<std::uint64_t>(kAreaLimit - area)) return kAreaLimit;
			area += static_cast<long long>(rectArea);
		}
		return area;
	}

	bool DirtyRegion::Intersects(const RECT& rect) const noexcept
	{
		if (IsEmpty(rect)) return false;
		for (const auto& stored : Rects())
		{
			if (!IsEmpty(Intersection(stored, rect))) return true;
		}
		return false;
	}

	std::span<const RECT> DirtyRegion::Rects() const noexcept
	{
		return { rects_.data(), count_ };
	}

	void DirtyRegion::ClipTo(int width, int height) noexcept
	{
		if (width <= 0 || height <= 0)
		{
			Clear();
			return;
		}

		const RECT canvas{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
		std::size_t kept = 0;
		for (const auto& rect : Rects())
		{
			const auto clipped = Intersection(rect, canvas);
			if (!IsEmpty(clipped)) rects_[kept++] = clipped;
		}
		count_ = kept;
	}

	void DirtyRegion::Normalize(const DirtyCostProfile& profile) noexcept
	{
		const auto fixedCost = static_cast<std::uint64_t>((std::max)(0LL, profile.fixedCostPixels));
		const auto maxRegions = (std::max)(std::uint32_t{ 1 }, profile.maxRegions);

		MergeOverlaps(rects_.data(), count_);
		// 代价合并与数量上限放在同一个循环里：为数量上限做的合并可能让别的一对变得划算，
		// 这样结果是不动点，对同一结果再规整一次不会再变。
		while (count_ > 1)
		{
			const auto best = FindLeastWastePair(Rects());
			const bool worthMerging = best.waste.negative || best.waste.magnitude <= fixedCost;
			if (!worthMerging && count_ <= maxRegions) break;
			MergePair(rects_.data(), count_, best.first, best.second);
			MergeOverlaps(rects_.data(), count_);
		}

		if (count_ <= 1) return;
		const RECT bounds = Bounds();
		const auto boundsArea = RectArea(bounds);
		std::uint64_t coveredArea = 0;
		for (const auto& rect : Rects()) coveredArea += RectArea(rect);
		// 区域互不相交，面积和不超过外接面积。用向上取整的阈值避免乘法溢出。
		if (coveredArea >= boundsArea - boundsArea / 4)
		{
			rects_[0] = bounds;
			count_ = 1;
		}
	}
}
