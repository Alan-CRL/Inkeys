module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <windows.h>

export module Inkeys.Drawing.Draw3.dirty_region;

export namespace Inkeys::Drawing::Draw3
{
	struct DirtyCostProfile
	{
		long long fixedCostPixels; // 多处理一个区域的固定开销，折算成像素。
		std::uint32_t maxRegions;
	};

	inline constexpr DirtyCostProfile kCompositeDirtyCost{ 128 * 128, 8 };
	inline constexpr DirtyCostProfile kReadbackDirtyCost{ 128 * 128, 8 };
	inline constexpr DirtyCostProfile kPresentDirtyCost{ 256 * 256, 8 };
	inline constexpr DirtyCostProfile kUlwCommitDirtyCost{ 512 * 512, 4 };

	class DirtyRegion
	{
	public:
		static constexpr std::size_t kCapacity = 16;

		void Clear() noexcept;
		// 收集半开矩形，容量满时合并浪费最少的一对。
		void Add(const RECT& rect) noexcept;
		void Add(const DirtyRegion& other) noexcept;
		bool Empty() const noexcept;
		RECT Bounds() const noexcept;
		// 规整前可能重复计算重叠面积，超出 long long 范围时返回其上限。
		long long Area() const noexcept;
		bool Intersects(const RECT& rect) const noexcept;
		// 规整之前可能含相交或重复的矩形；交给不幂等的合成步骤前必须先 Normalize。
		std::span<const RECT> Rects() const noexcept;
		void ClipTo(int width, int height) noexcept;
		// 规整为两两不相交的矩形，区域数上限至少为一。
		void Normalize(const DirtyCostProfile& profile) noexcept;

	private:
		std::array<RECT, kCapacity> rects_{};
		std::size_t count_ = 0;
	};
}
