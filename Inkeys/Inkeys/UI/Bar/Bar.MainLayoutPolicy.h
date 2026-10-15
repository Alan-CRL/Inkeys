#pragma once

#include <cmath>

namespace Inkeys::UI::Bar
{
	enum class BarIconOrder { FollowMainButton, FollowMainButtonReverse, LeftToRight, RightToLeft };
	enum class BarExpandDirectionAdjustment { Centerline, WhenSpaceInsufficient };
	[[nodiscard]] inline BarIconOrder NormalizeBarIconOrder(int value) noexcept
	{
		return value >= 0 && value <= 3 ? static_cast<BarIconOrder>(value) : BarIconOrder::FollowMainButton;
	}
	[[nodiscard]] inline BarExpandDirectionAdjustment NormalizeBarExpandDirectionAdjustment(int value) noexcept
	{
		return value == 0 ? BarExpandDirectionAdjustment::Centerline : BarExpandDirectionAdjustment::WhenSpaceInsufficient;
	}
	[[nodiscard]] inline bool MirrorBarMainIconsX(BarIconOrder order, bool opensRight) noexcept
	{
		switch (order)
		{
		case BarIconOrder::FollowMainButtonReverse: return opensRight;
		case BarIconOrder::LeftToRight: return false;
		case BarIconOrder::RightToLeft: return true;
		default: return !opensRight;
		}
	}
	[[nodiscard]] inline double ResolveBarMainIconX(double x, double width,
		BarIconOrder order, bool opensRight) noexcept
	{
		return MirrorBarMainIconsX(order, opensRight) ? width - x : x;
	}
	// 有限观察签名的 bit8..9 保存排序，bit10 保存方向策略；bit7 仍保留为非法位。
	[[nodiscard]] inline unsigned BarMainLayoutSignatureFlags(BarIconOrder order,
		BarExpandDirectionAdjustment adjustment) noexcept
	{
		return (static_cast<unsigned>(order) << 8) | (static_cast<unsigned>(adjustment) << 10);
	}

	[[nodiscard]] constexpr bool LockCenteredBarExpansion(bool centered, bool whiteboard,
		BarExpandDirectionAdjustment adjustment) noexcept
	{
		// 白板居中仍需响应屏幕边缘；中线策略保持居中方向，避免往返翻转。
		return centered && !(whiteboard && adjustment == BarExpandDirectionAdjustment::WhenSpaceInsufficient);
	}

	struct BarMainExpansionInput
	{
		double monitorLeft = 0.0, monitorRight = 0.0, mainCenterScreenX = 0.0;
		double zoom = 1.0, mainHalfWidthDip = 40.0, gapDip = 10.0;
		double layoutWidthDip = 0.0, mainStrokeHalfDip = 0.0, barStrokeHalfDip = 0.0;
		BarExpandDirectionAdjustment adjustment = BarExpandDirectionAdjustment::WhenSpaceInsufficient;
		bool directionLocked = false;
		bool operator==(const BarMainExpansionInput&) const = default;
	};
	[[nodiscard]] inline bool ResolveBarMainExpansionSide(const BarMainExpansionInput& input,
		bool currentSide) noexcept
	{
		if (input.directionLocked || !std::isfinite(input.monitorLeft)
			|| !std::isfinite(input.monitorRight) || input.monitorRight <= input.monitorLeft
			|| !std::isfinite(input.mainCenterScreenX)) return currentSide;
		if (input.adjustment == BarExpandDirectionAdjustment::Centerline)
			return input.mainCenterScreenX <= (input.monitorLeft + input.monitorRight) / 2.0;
		if (!std::isfinite(input.zoom) || input.zoom <= 0.0
			|| !std::isfinite(input.layoutWidthDip) || input.layoutWidthDip <= 0.0
			|| !std::isfinite(input.mainHalfWidthDip) || input.mainHalfWidthDip < 0.0
			|| !std::isfinite(input.gapDip) || input.gapDip < 0.0
			|| !std::isfinite(input.mainStrokeHalfDip) || input.mainStrokeHalfDip < 0.0
			|| !std::isfinite(input.barStrokeHalfDip) || input.barStrokeHalfDip < 0.0) return currentSide;
		const double mainExtent = (input.mainHalfWidthDip + input.mainStrokeHalfDip) * input.zoom;
		const double barExtent = (input.mainHalfWidthDip + input.gapDip
			+ input.layoutWidthDip + input.barStrokeHalfDip) * input.zoom;
		// 目标包络外扩至实际像素；不使用点击放大、动画宽度或回弹峰值判断换边。
		const auto fits = [&](bool right)
		{
			return std::floor(input.mainCenterScreenX - (right ? mainExtent : barExtent)) >= input.monitorLeft
				&& std::ceil(input.mainCenterScreenX + (right ? barExtent : mainExtent)) <= input.monitorRight;
		};
		// 两侧都不足时不改根位置，也不来回翻边。
		return !fits(currentSide) && fits(!currentSide) ? !currentSide : currentSide;
	}
	class BarMainExpansionCache
	{
	public:
		[[nodiscard]] bool Resolve(const BarMainExpansionInput& input, bool currentSide) noexcept
		{
			if (initialized_ && input == input_ && currentSide == result_) return result_;
			input_ = input;
			result_ = ResolveBarMainExpansionSide(input, currentSide);
			initialized_ = true;
			return result_;
		}
	private:
		BarMainExpansionInput input_;
		bool initialized_ = false, result_ = true;
	};
}
