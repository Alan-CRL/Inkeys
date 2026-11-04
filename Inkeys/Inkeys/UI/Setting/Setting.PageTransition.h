#pragma once

#include <algorithm>
#include <cmath>

namespace Inkeys::UI::Setting::Design
{
	struct PageRoute
	{
		int page = 0, subpage = 0;
		bool operator==(const PageRoute&) const = default;
	};

	// 只保留路由值；旧页仍由原switch绘制一次，不缓存纹理、控件或业务回调。
	class PageTransitionState
	{
	public:
		static constexpr float Duration = 0.125F;

		PageRoute Resolve(PageRoute requested, float delta, bool enabled, float rate, bool bypass = false) noexcept
		{
			if (!initialized_ || !enabled || bypass)
			{
				displayed_ = target_ = requested; initialized_ = true;
				phase_ = Phase::Stable; opacity_ = start_ = 1.0F; progress_ = 0.0F;
				return displayed_;
			}
			target_ = requested;
			const auto next = requested == displayed_ ? Phase::Returning : Phase::Leaving;
			if (phase_ == Phase::Stable && requested == displayed_) return displayed_;
			if (phase_ != next)
			{
				// 快速返回从当前透明度回升；更换尚未显示的目标不重启旧页淡出。
				phase_ = next; start_ = opacity_; progress_ = 0.0F;
				return displayed_;
			}
			delta = std::isfinite(delta) ? std::clamp(delta, 0.0F, 0.25F) : 0.0F;
			rate = std::isfinite(rate) ? std::clamp(rate, 0.1F, 5.0F) : 1.0F;
			progress_ = (std::min)(1.0F, progress_ + delta * rate / Duration);
			const float eased = progress_ * progress_ * (3.0F - 2.0F * progress_);
			opacity_ = start_ + ((phase_ == Phase::Leaving ? 0.0F : 1.0F) - start_) * eased;
			if (progress_ >= 1.0F)
			{
				// 完全隐去才一次提交最新目标，新页从自己的逐块入场零透明度开始。
				if (phase_ == Phase::Leaving) displayed_ = target_;
				phase_ = Phase::Stable; opacity_ = start_ = 1.0F; progress_ = 0.0F;
			}
			return displayed_;
		}

		[[nodiscard]] bool Holding() const noexcept { return phase_ != Phase::Stable; }
		[[nodiscard]] bool Exiting() const noexcept { return phase_ == Phase::Leaving; }
		[[nodiscard]] float Opacity() const noexcept { return opacity_; }
		[[nodiscard]] PageRoute Displayed() const noexcept { return displayed_; }
		[[nodiscard]] PageRoute Target() const noexcept { return target_; }

	private:
		enum class Phase { Stable, Leaving, Returning };
		PageRoute displayed_{}, target_{};
		Phase phase_ = Phase::Stable;
		float opacity_ = 1.0F, start_ = 1.0F, progress_ = 0.0F;
		bool initialized_ = false;
	};
}
