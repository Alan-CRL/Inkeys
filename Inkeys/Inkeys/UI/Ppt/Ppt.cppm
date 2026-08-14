module;

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

export module Inkeys.UI.Ppt;

export namespace Inkeys::UI::Ppt
{
	enum class Control : std::uint8_t
	{
		BottomLeft,
		BottomRight,
		MiddleLeft,
		MiddleRight,
		ExitShow,
	};

	struct LayoutConfiguration
	{
		float bottomPairWidth = 0.0F;
		float bottomPairHeight = 0.0F;
		float middlePairWidth = 0.0F;
		float middlePairHeight = 0.0F;
		float exitWidth = 0.0F;
		float exitHeight = 0.0F;
		float bottomPairScale = 1.0F;
		float middlePairScale = 1.0F;
		float exitScale = 1.0F;
		bool showBottomPair = true;
		bool showMiddlePair = false;
		bool showExit = true;
		bool rememberPosition = true;
	};

	struct ControlLayout
	{
		RECT expanded{};
		RECT hidden{};
		SIZE backing{ 1, 1 };
		float scale = 1.0F;
		bool enabled = false;
	};

	struct RuntimeLayoutConfiguration
	{
		LayoutConfiguration configuration;
		float dpiScale = 1.0F;
	};

	struct VisualRect
	{
		float left = 0.0F;
		float top = 0.0F;
		float right = 0.0F;
		float bottom = 0.0F;
	};

	struct VisualLine
	{
		float x1 = 0.0F;
		float y1 = 0.0F;
		float x2 = 0.0F;
		float y2 = 0.0F;
	};

	struct ControlVisualGeometry
	{
		VisualLine dragHandle{};
		VisualRect previous{};
		VisualRect currentPage{};
		VisualRect totalPage{};
		VisualRect next{};
		VisualRect action{};
	};

	struct PageText
	{
		std::wstring current;
		std::wstring total;
	};

	[[nodiscard]] inline ControlVisualGeometry ResolveControlVisualGeometry(
		Control control) noexcept
	{
		ControlVisualGeometry geometry;
		if (control == Control::BottomLeft || control == Control::BottomRight)
		{
			const bool left = control == Control::BottomLeft;
			const float inset = left ? 15.0F : 5.0F;
			geometry.dragHandle = left
				? VisualLine{ 8.0F, 15.0F, 8.0F, 45.0F }
				: VisualLine{ 187.0F, 15.0F, 187.0F, 45.0F };
			geometry.previous = { inset, 5.0F, inset + 50.0F, 55.0F };
			geometry.currentPage = { inset + 55.0F, 5.0F,
				inset + 120.0F, 40.0F };
			geometry.totalPage = { inset + 55.0F, 30.0F,
				inset + 120.0F, 60.0F };
			geometry.next = { inset + 125.0F, 5.0F,
				inset + 175.0F, 55.0F };
		}
		else if (control == Control::MiddleLeft || control == Control::MiddleRight)
		{
			geometry.dragHandle = { 15.0F, 8.0F, 45.0F, 8.0F };
			geometry.previous = { 5.0F, 15.0F, 55.0F, 65.0F };
			geometry.currentPage = { 5.0F, 70.0F, 55.0F, 110.0F };
			geometry.totalPage = { 5.0F, 100.0F, 55.0F, 125.0F };
			geometry.next = { 5.0F, 130.0F, 55.0F, 180.0F };
		}
		else
		{
			geometry.dragHandle = { 8.0F, 15.0F, 8.0F, 45.0F };
			geometry.action = { 15.0F, 5.0F, 65.0F, 55.0F };
		}
		return geometry;
	}

	[[nodiscard]] inline PageText ResolvePageText(Control control,
		int currentPage, int totalPage)
	{
		const int maximum = control == Control::MiddleLeft ||
			control == Control::MiddleRight ? 999 : 9999;
		return {
			currentPage < 0 ? L"-" : std::to_wstring((std::min)(maximum, currentPage)),
			L"/" + (totalPage < 0 ? std::wstring(L"-")
				: std::to_wstring((std::min)(maximum, totalPage))),
		};
	}

	[[nodiscard]] inline bool IsInPageHitArea(Control control,
		float x, float y) noexcept
	{
		const auto geometry = ResolveControlVisualGeometry(control);
		if (control == Control::BottomLeft || control == Control::BottomRight)
			return x >= geometry.currentPage.left && x <= geometry.currentPage.right;
		if (control == Control::MiddleLeft || control == Control::MiddleRight)
			return y >= geometry.currentPage.top && y <= geometry.totalPage.bottom;
		return false;
	}

	struct DragResolution
	{
		float width = 0.0F;
		float height = 0.0F;
		bool accepted = true;
	};

	struct PptDiagnosticDamageResolution
	{
		RECT damage{};
		bool drawActive = false;
		bool drawFinal = false;
		bool keepFinalFrame = false;
	};

	[[nodiscard]] inline float NormalizePptDpiScale(float dpiScale) noexcept
	{
		if (!std::isfinite(dpiScale) || dpiScale <= 0.0F) return 1.0F;
		return std::clamp(dpiScale, 0.5F, 4.0F);
	}

	[[nodiscard]] inline float NormalizePptRuntimeControlScale(float scale) noexcept
	{
		if (!std::isfinite(scale) || scale <= 0.0F) return 1.0F;
		return std::clamp(scale, 1.0F / 128.0F, 3.0F);
	}

	[[nodiscard]] inline float AdvanceLegacyValue(float current, float target,
		float divisor = 15.0F, float minimumStep = 0.1F) noexcept
	{
		if (!std::isfinite(current) || !std::isfinite(target) || divisor <= 0.0F ||
			minimumStep <= 0.0F) return target;
		const float difference = target - current;
		if (std::abs(difference) <= minimumStep) return target;
		const float step = (std::max)(minimumStep, std::abs(difference) / divisor);
		return current + std::copysign((std::min)(step, std::abs(difference)), difference);
	}

	[[nodiscard]] inline float EasePptDisplayTransition(float progress) noexcept
	{
		progress = std::clamp(progress, 0.0F, 1.0F);
		return progress < 0.5F
			? 4.0F * progress * progress * progress
			: 1.0F - std::pow(-2.0F * progress + 2.0F, 3.0F) / 2.0F;
	}

	[[nodiscard]] inline float InterpolatePptDisplayValue(
		float start, float target, float progress) noexcept
	{
		if (!std::isfinite(start) || !std::isfinite(target)) return target;
		const float eased = EasePptDisplayTransition(progress);
		return start + (target - start) * eased;
	}

	[[nodiscard]] inline ControlLayout ResolveControlLayout(Control control,
		const RECT& monitor, const LayoutConfiguration& config,
		bool presentationVisible, float dpiScale = 1.0F) noexcept
	{
		const float width = static_cast<float>(monitor.right - monitor.left);
		const float height = static_cast<float>(monitor.bottom - monitor.top);
		const float normalizedDpi = NormalizePptDpiScale(dpiScale);
		ControlLayout result;
		float x = 0.0F;
		float y = 0.0F;
		float hiddenX = 0.0F;
		float hiddenY = 0.0F;
		if (control == Control::BottomLeft || control == Control::BottomRight)
		{
			result.scale = NormalizePptRuntimeControlScale(config.bottomPairScale) * normalizedDpi;
			result.backing = {
				static_cast<LONG>((std::max)(1.0F, std::round(195.0F * result.scale))),
				static_cast<LONG>((std::max)(1.0F, std::round(60.0F * result.scale))) };
			const bool left = control == Control::BottomLeft;
			x = left ? config.bottomPairWidth + 5.0F * result.scale
				: width - config.bottomPairWidth - 200.0F * result.scale;
			y = height - config.bottomPairHeight - 65.0F * result.scale;
			hiddenX = x;
			hiddenY = height + 5.0F * result.scale;
			result.enabled = presentationVisible && config.showBottomPair;
		}
		else if (control == Control::MiddleLeft || control == Control::MiddleRight)
		{
			result.scale = NormalizePptRuntimeControlScale(config.middlePairScale) * normalizedDpi;
			result.backing = {
				static_cast<LONG>((std::max)(1.0F, std::round(60.0F * result.scale))),
				static_cast<LONG>((std::max)(1.0F, std::round(185.0F * result.scale))) };
			const bool left = control == Control::MiddleLeft;
			x = left ? config.middlePairWidth + 5.0F * result.scale
				: width - config.middlePairWidth - 65.0F * result.scale;
			y = height / 2.0F - config.middlePairHeight - 92.5F * result.scale;
			hiddenX = left ? -65.0F * result.scale : width + 5.0F * result.scale;
			hiddenY = y;
			result.enabled = presentationVisible && config.showMiddlePair;
		}
		else
		{
			result.scale = NormalizePptRuntimeControlScale(config.exitScale) * normalizedDpi;
			result.backing = {
				static_cast<LONG>((std::max)(1.0F, std::round(70.0F * result.scale))),
				static_cast<LONG>((std::max)(1.0F, std::round(60.0F * result.scale))) };
			x = width / 2.0F + config.exitWidth - 35.0F * result.scale;
			y = height - config.exitHeight - 65.0F * result.scale;
			hiddenX = x;
			hiddenY = height + 5.0F * result.scale;
			result.enabled = presentationVisible && config.showExit;
		}

		auto MakeRect = [&](float left, float top)
			{
				RECT value{
					monitor.left + static_cast<LONG>(std::floor(left)),
					monitor.top + static_cast<LONG>(std::floor(top)), 0, 0 };
				value.right = value.left + result.backing.cx;
				value.bottom = value.top + result.backing.cy;
				return value;
			};
		result.expanded = MakeRect(x, y);
		result.hidden = MakeRect(hiddenX, hiddenY);
		return result;
	}

	[[nodiscard]] inline bool PptRectsOverlap(const RECT& first,
		const RECT& second, LONG gap) noexcept
	{
		return first.left < second.right + gap && first.right + gap > second.left &&
			first.top < second.bottom + gap && first.bottom + gap > second.top;
	}

	[[nodiscard]] inline LayoutConfiguration ClampPptDrag(Control control,
		const RECT& monitor, LayoutConfiguration next,
		float dpiScale = 1.0F) noexcept
	{
		const float width = static_cast<float>(monitor.right - monitor.left);
		const float height = static_cast<float>(monitor.bottom - monitor.top);
		const float normalizedDpi = NormalizePptDpiScale(dpiScale);
		if (control == Control::BottomLeft || control == Control::BottomRight)
		{
			const float scale = NormalizePptRuntimeControlScale(next.bottomPairScale) * normalizedDpi;
			next.bottomPairWidth = std::clamp(next.bottomPairWidth, 0.0F,
				(std::max)(0.0F, width / 2.0F - 205.0F * scale));
			next.bottomPairHeight = std::clamp(next.bottomPairHeight, 0.0F,
				(std::max)(0.0F, height - 70.0F * scale));
		}
		else if (control == Control::MiddleLeft || control == Control::MiddleRight)
		{
			const float scale = NormalizePptRuntimeControlScale(next.middlePairScale) * normalizedDpi;
			const float horizontalLimit = (std::max)(0.0F,
				width / 2.0F - 65.0F * scale);
			const float verticalLimit = (std::max)(0.0F,
				height / 2.0F - 97.5F * scale);
			next.middlePairWidth = std::clamp(next.middlePairWidth,
				0.0F, horizontalLimit);
			next.middlePairHeight = std::clamp(next.middlePairHeight,
				-verticalLimit, verticalLimit);
		}
		else
		{
			const float scale = NormalizePptRuntimeControlScale(next.exitScale) * normalizedDpi;
			const float horizontalLimit = (std::max)(0.0F,
				width / 2.0F - 40.0F * scale);
			next.exitWidth = std::clamp(next.exitWidth,
				-horizontalLimit, horizontalLimit);
			next.exitHeight = std::clamp(next.exitHeight, 0.0F,
				(std::max)(0.0F, height - 70.0F * scale));
		}
		return next;
	}

	[[nodiscard]] inline bool PptDragCollides(Control control, const RECT& monitor,
		const LayoutConfiguration& config, float dpiScale = 1.0F) noexcept
	{
		const bool bottom = control == Control::BottomLeft ||
			control == Control::BottomRight;
		const bool middle = control == Control::MiddleLeft ||
			control == Control::MiddleRight;
		const float commonScale = (std::min)({
			NormalizePptRuntimeControlScale(config.bottomPairScale),
			NormalizePptRuntimeControlScale(config.middlePairScale),
			NormalizePptRuntimeControlScale(config.exitScale) }) *
			NormalizePptDpiScale(dpiScale);
		const LONG gap = static_cast<LONG>(std::lround(10.0F * commonScale));
		constexpr std::array<Control, 5> controls{
			Control::BottomLeft, Control::BottomRight, Control::MiddleLeft,
			Control::MiddleRight, Control::ExitShow };
		for (const auto moved : controls)
		{
			const bool movedInGroup = bottom
				? (moved == Control::BottomLeft || moved == Control::BottomRight)
				: middle
					? (moved == Control::MiddleLeft || moved == Control::MiddleRight)
					: moved == Control::ExitShow;
			if (!movedInGroup) continue;
			const auto movedLayout = ResolveControlLayout(moved, monitor, config, true,
				dpiScale);
			for (const auto other : controls)
			{
				const bool otherInGroup = bottom
					? (other == Control::BottomLeft || other == Control::BottomRight)
					: middle
						? (other == Control::MiddleLeft || other == Control::MiddleRight)
						: other == Control::ExitShow;
				if (otherInGroup) continue;
				const auto otherLayout = ResolveControlLayout(other, monitor, config, true,
					dpiScale);
				if (movedLayout.enabled && otherLayout.enabled &&
					PptRectsOverlap(movedLayout.expanded, otherLayout.expanded, gap))
					return true;
			}
		}
		return false;
	}

	[[nodiscard]] inline RuntimeLayoutConfiguration ResolveRuntimeLayoutConfiguration(
		const RECT& monitor, LayoutConfiguration configuration,
		float dpiScale = 1.0F) noexcept
	{
		const float width = static_cast<float>((std::max)(1L,
			monitor.right - monitor.left));
		const float height = static_cast<float>((std::max)(1L,
			monitor.bottom - monitor.top));
		const float fittedDpi = NormalizePptDpiScale(dpiScale);
		auto Fit = [&](bool enabled, float baseWidth, float baseHeight, float& scale)
			{
				scale = std::clamp(scale, 0.5F, 3.0F);
				if (!enabled) return;
				const float maximum = (std::min)(width / (baseWidth * fittedDpi),
					height / (baseHeight * fittedDpi));
				scale = (std::min)(scale, (std::max)(1.0F / 128.0F, maximum));
			};
		Fit(configuration.showBottomPair, 410.0F, 70.0F,
			configuration.bottomPairScale);
		Fit(configuration.showMiddlePair, 130.0F, 195.0F,
			configuration.middlePairScale);
		Fit(configuration.showExit, 80.0F, 70.0F,
			configuration.exitScale);

		if (configuration.showBottomPair)
			configuration = ClampPptDrag(Control::BottomLeft, monitor,
				configuration, fittedDpi);
		auto ResolveLowerPriority = [&](Control control, float& scale,
			float& offsetX, float& offsetY)
		{
			configuration = ClampPptDrag(control, monitor, configuration, fittedDpi);
			if (!PptDragCollides(control, monitor, configuration, fittedDpi)) return;
			offsetX = 0.0F;
			offsetY = 0.0F;
			configuration = ClampPptDrag(control, monitor, configuration, fittedDpi);
			for (int attempt = 0; attempt < 24 &&
				PptDragCollides(control, monitor, configuration, fittedDpi); ++attempt)
			{
				scale = (std::max)(1.0F / 128.0F, scale * 0.75F);
				configuration = ClampPptDrag(control, monitor, configuration, fittedDpi);
			}
		};
		const bool showMiddlePair = configuration.showMiddlePair;
		configuration.showMiddlePair = false;
		if (configuration.showExit)
			ResolveLowerPriority(Control::ExitShow, configuration.exitScale,
				configuration.exitWidth, configuration.exitHeight);
		configuration.showMiddlePair = showMiddlePair;
		if (showMiddlePair)
			ResolveLowerPriority(Control::MiddleLeft, configuration.middlePairScale,
				configuration.middlePairWidth, configuration.middlePairHeight);
		return { configuration, fittedDpi };
	}

	[[nodiscard]] inline RECT ResolvePptDamage(SIZE backing,
		const RECT& previousVisual, const RECT& currentVisual,
		bool requireFull) noexcept
	{
		const RECT full{ 0, 0, (std::max)(1L, backing.cx),
			(std::max)(1L, backing.cy) };
		if (requireFull) return full;
		RECT damage{
			(std::min)(previousVisual.left, currentVisual.left),
			(std::min)(previousVisual.top, currentVisual.top),
			(std::max)(previousVisual.right, currentVisual.right),
			(std::max)(previousVisual.bottom, currentVisual.bottom) };
		damage.left = std::clamp(damage.left, full.left, full.right);
		damage.top = std::clamp(damage.top, full.top, full.bottom);
		damage.right = std::clamp(damage.right, full.left, full.right);
		damage.bottom = std::clamp(damage.bottom, full.top, full.bottom);
		if (damage.right <= damage.left || damage.bottom <= damage.top)
			return full;
		return damage;
	}

	[[nodiscard]] inline PptDiagnosticDamageResolution ResolvePptDiagnosticDamage(
		SIZE backing, RECT businessDamage, const RECT& previousDiagnostic,
		bool requireFull, bool debugEnabled, bool active,
		bool finalPending) noexcept
	{
		auto Union = [](RECT& destination, const RECT& source)
			{
				if (source.right <= source.left || source.bottom <= source.top) return;
				if (destination.right <= destination.left || destination.bottom <= destination.top)
					destination = source;
				else
				{
					destination.left = (std::min)(destination.left, source.left);
					destination.top = (std::min)(destination.top, source.top);
					destination.right = (std::max)(destination.right, source.right);
					destination.bottom = (std::max)(destination.bottom, source.bottom);
				}
			};
		const bool drawActive = debugEnabled && active;
		const bool drawFinal = debugEnabled && !active && finalPending;
		if (drawActive || drawFinal) Union(businessDamage, previousDiagnostic);
		return {
			ResolvePptDamage(backing, businessDamage, businessDamage, requireFull),
			drawActive,
			drawFinal,
			drawActive,
		};
	}

	enum class ConfigGroup : std::uint8_t
	{
		BottomPair,
		MiddlePair,
		ExitShow,
		All,
	};

	struct BusinessCallbacks
	{
		std::function<void()> previousPage;
		std::function<void()> nextPage;
		std::function<void()> viewShow;
		std::function<void()> endShow;
		std::function<void(LayoutConfiguration)> persistPosition;
	};

	// 初始化只注册渲染客户端；窗口本身仍由 Window Service 创建和销毁。
	bool Initialize(BusinessCallbacks callbacks);
	void Shutdown() noexcept;

	[[nodiscard]] WNDPROC WindowProc() noexcept;
	void PublishPresentationVisible(bool visible) noexcept;
	void PublishPageState(int currentPage, int totalPage) noexcept;
	void FlashPageDirection(bool next) noexcept;
	void NotifyConfigurationChanged(ConfigGroup group) noexcept;
	void QueueGlobalWheel(short delta) noexcept;
	void SetDebugEnabled(bool enabled) noexcept;
}
