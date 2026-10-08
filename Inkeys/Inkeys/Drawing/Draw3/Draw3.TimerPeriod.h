#pragma once

namespace Inkeys::Drawing::Draw3
{
	// 把系统计时器调用封装为可替换接口，测试不需要真的修改系统分辨率。
	class TimerPeriodController
	{
	public:
		using BeginCallback = bool(*)(void*, unsigned int) noexcept;
		using EndCallback = void(*)(void*, unsigned int) noexcept;

		struct Callbacks
		{
			void* context = nullptr;
			BeginCallback begin = nullptr;
			EndCallback end = nullptr;
		};

		explicit TimerPeriodController(Callbacks callbacks, unsigned int period = 1) noexcept
			: callbacks_(callbacks), period_(period) {}

		~TimerPeriodController()
		{
			Release();
		}

		TimerPeriodController(const TimerPeriodController&) = delete;
		TimerPeriodController& operator=(const TimerPeriodController&) = delete;

		void SetSelectionMode(bool selectionMode) noexcept
		{
			if (selectionMode_ == selectionMode) return;
			selectionMode_ = selectionMode;
			if (selectionMode)
			{
				Release();
				return;
			}

			// 一次绘制模式停留只尝试一次；失败后等下次重新进入绘制再试。
			active_ = callbacks_.begin &&
				callbacks_.begin(callbacks_.context, period_);
		}

		bool Active() const noexcept { return active_; }
		bool SelectionMode() const noexcept { return selectionMode_; }

	private:
		void Release() noexcept
		{
			if (!active_) return;
			active_ = false;
			if (callbacks_.end) callbacks_.end(callbacks_.context, period_);
		}

		Callbacks callbacks_{};
		unsigned int period_ = 1;
		bool selectionMode_ = true;
		bool active_ = false;
	};
}
