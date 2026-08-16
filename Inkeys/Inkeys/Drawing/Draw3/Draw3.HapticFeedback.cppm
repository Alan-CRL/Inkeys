module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <memory>

export module Inkeys.Drawing.Draw3.haptic_feedback;

export namespace Inkeys::Drawing::Draw3
{
	// 当前原型的触觉总开关；后续接入 Inkeys3 配置系统后替换这里。
	inline constexpr bool kPenHapticFeedbackDefaultEnabled = true;
	// 负值表示不覆盖 Windows/设备的触觉强度设置。
	inline constexpr double kSystemDefaultHapticIntensity = -1.0;

	// 预留给 RTS 书写反馈的连续触觉波形，数值对应 HID usage ID。
	enum class HapticContinuousFeedback : uint16_t
	{
		InkContinuous = 0x100B,
		PencilContinuous = 0x100C,
		MarkerContinuous = 0x100D,
		ChiselMarkerContinuous = 0x100E,
		BrushContinuous = 0x100F,
		EraserContinuous = 0x1010,
		GalaxyPenContinuous = 0x1011
	};

	// 预留给 UI/关键操作反馈的离散触觉波形，数值对应 HID usage ID。
	enum class HapticDiscreteFeedback : uint16_t
	{
		Click = 0x1003,
		Press = 0x1006,
		Release = 0x1007,
		Hover = 0x1008,
		Success = 0x1009,
		Error = 0x100A,
		Collide = 0x1012,
		Align = 0x1013,
		Step = 0x1014,
		Grow = 0x1015
	};

	enum class HapticToolFeedback : uint32_t
	{
		Pen,
		Highlighter,
		Eraser
	};

	// 按当前工具选择连续书写触觉波形。
	HapticContinuousFeedback ResolveContinuousHapticFeedback(HapticToolFeedback tool) noexcept;
	// 可选连续波形统一回退到触觉笔必需支持的 InkContinuous。
	HapticContinuousFeedback FallbackContinuousHapticFeedback(
		HapticContinuousFeedback feedback) noexcept;
	// 可选离散波形统一回退到最基础的 Click。
	HapticDiscreteFeedback FallbackDiscreteHapticFeedback(
		HapticDiscreteFeedback feedback) noexcept;

	struct PenHapticFeedbackImpl;

	// 动态探测 WinRT 触觉能力；不可用时所有公开方法都是 no-op。
	class PenHapticFeedback
	{
	public:
		PenHapticFeedback();
		~PenHapticFeedback();
		PenHapticFeedback(const PenHapticFeedback&) = delete;
		PenHapticFeedback& operator=(const PenHapticFeedback&) = delete;

		bool Initialize() noexcept;
		void Shutdown() noexcept;
		void SetEnabled(bool enabled) noexcept;
		bool IsEnabled() const noexcept;
		bool IsAvailable() const noexcept;
		bool AttachPointerId(uint32_t pointerId) noexcept;
		bool PlayDiscrete(HapticDiscreteFeedback feedback,
			double intensity = kSystemDefaultHapticIntensity) noexcept;
		bool TickContinuous(HapticContinuousFeedback feedback,
			double intensity = kSystemDefaultHapticIntensity) noexcept;
		void StopFeedback() noexcept;

	private:
		std::unique_ptr<PenHapticFeedbackImpl> impl_;
	};
}
