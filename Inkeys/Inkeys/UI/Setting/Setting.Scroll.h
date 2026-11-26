#pragma once

#include "imgui/imgui.h"
#include <array>
#include <cstddef>
#include <cstdint>

struct ImGuiWindow;

// 设置页面拖动滚动：原 Setting.Controls.cpp 的单宿主拖动、内容型按钮转滚动与 StateStorage 键协议集中在此，
// 并增加触摸/笔惯性与越界窗口位移反馈。鼠标只用滚轮与滚动条，不再拖动内容滚动。
namespace Inkeys::UI::Setting::Scroll
{
	// ---- 纯逻辑参数（DIP 单位，使用处按 Pixels 换算）----
	// WinUI3 InteractionTracker.PositionInertiaDecayRate 默认 0.95：每秒速度只保留 5%。
	inline constexpr float InertiaDecayRate = 0.95F;
	inline constexpr float MinFlingDipPerSecond = 60.0F;
	inline constexpr float MaxFlingDipPerSecond = 8000.0F;
	inline constexpr float StopDipPerSecond = 10.0F;
	// 惯性中按下：速度高于此值时这次按下只停住，不激活下方控件。
	inline constexpr float CatchDipPerSecond = 120.0F;
	inline constexpr double VelocityWindowSeconds = 0.100;
	inline constexpr double StillReleaseSeconds = 0.080;
	// 越界：位移 = 越界量 × 阻尼，封顶；惯性撞边界按撞击速度换算一次回弹位移。
	// UpdatePanningFeedback 文档规定位移最大 30 物理像素，因此上限按物理像素，不随 DPI 放大。
	inline constexpr float OverpanResistance = 0.5F;
	inline constexpr float MaxOverpanPixels = 30.0F;
	inline constexpr float BounceSecondsPerVelocity = 0.015F;

	class VelocityEstimator
	{
	public:
		static constexpr std::size_t Capacity = 32;
		void Reset() noexcept { count_ = 0; next_ = 0; }
		void Add(double time, float position) noexcept;
		[[nodiscard]] std::size_t Count() const noexcept { return count_; }
		// releaseTime 前 VelocityWindowSeconds 内的一阶最小二乘斜率（单位/秒）；
		// 样本不足、最后一次有效位移距抬起超过 StillReleaseSeconds 时返回 0。
		[[nodiscard]] float Estimate(double releaseTime) const noexcept;
	private:
		struct Sample { double time; float position; };
		std::array<Sample, Capacity> samples_{};
		std::size_t count_ = 0, next_ = 0;
	};

	// 指数衰减惯性：v(t)=v0·r^t，y(t)=y0+v0·(1-r^t)/ln(1/r)，r=1-InertiaDecayRate；按时间解析计算，与帧率无关。
	class InertiaCurve
	{
	public:
		void Start(float position, float velocity, double time) noexcept;
		void Stop() noexcept { active_ = false; }
		[[nodiscard]] bool Active() const noexcept { return active_; }
		[[nodiscard]] float Position(double time) const noexcept;
		[[nodiscard]] float Velocity(double time) const noexcept;
	private:
		float origin_ = 0.0F, velocity_ = 0.0F;
		double start_ = 0.0;
		bool active_ = false;
	};

	// 越界量（像素，带符号）→ 窗口位移（像素，同号），线性阻尼并封顶 maxOffset。
	[[nodiscard]] float OverpanOffset(float overpan, float maxOffset) noexcept;
	// 越界量的累计上限：超过即对应位移已封顶，反向时最多先回收这么多再滚动内容。
	[[nodiscard]] float OverpanLimit(float maxOffset) noexcept;

	// ---- 越界窗口位移：渲染线程产出，设置窗口线程调用 uxtheme panning feedback ----
	struct PanningFeedback
	{
		bool active = false;   // 拖动越界中
		int offsetY = 0;       // 拖动越界位移（物理像素，正值窗口下移）
		bool bounce = false;   // 惯性撞边界的一次回弹
		int bounceY = 0;
	};
	// 渲染线程：自上次取出后有变化时返回 true；回弹只交付一次。
	[[nodiscard]] bool TakePanningFeedback(PanningFeedback& out) noexcept;
	// 惯性进行中（渲染线程）。
	[[nodiscard]] bool Animating() noexcept;
	// 窗口位移 API 不可用时只在边界硬停，不累计越界量也不回弹（渲染线程初始化时设置）。
	void SetPanningFeedbackAvailable(bool available) noexcept;
	// 设置会话输入重置（隐藏/重开）：停止惯性、放弃当前抓取与越界反馈。
	void Cancel() noexcept;

	// ---- 滚动宿主键协议（Setting.Controls.cpp 的滚动条样式与 BeginScrollChild 使用）----
	[[nodiscard]] ImGuiID WindowStateId(ImGuiWindow* window, const char* label);
	// 宿主 Begin 前：投递本帧已抓取的拖动位置，或在原生目标已存在时标记下一帧重基准。
	void ConsumePendingScroll(ImGuiWindow* window);
	// 回到顶部：清除拖动键并停止该宿主的惯性。
	void ResetWindow(ImGuiWindow* window);

#ifdef DRAW3_CONTACT_TESTING
	namespace Testing
	{
		void Reset() noexcept;
	}
#endif
}
