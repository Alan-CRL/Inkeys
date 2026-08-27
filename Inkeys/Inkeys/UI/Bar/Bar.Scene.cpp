module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite_1.h>
#include <limits>
#include <mutex>
#include <utility>
#include <windows.h>

#include "Bar.DirtyRegion.h"

module Inkeys.UI.Bar;
import :Scene;
import :Main;
import :Rendering;
import :Button;
import :State;
import :Theme;
import :UI;
import Inkeys.UI.Bar.Animation;

namespace Inkeys::UI::Bar
{
		namespace
	{
		constexpr double kDefaultDamageOutsetDip =
			BarButtonFrameThicknessDip
			+ BarRenderingAttribute::pointLightDiffuseExtraWidth
			+ BarRenderingAttribute::dirtyAntialiasPadding;
		constexpr double kDefaultTwoTwoGroupWidthDip =
			BarButtonTwoSideDip * 3.0 + BarButtonGapDip * 4.0;
		constexpr double kDefaultTwoTwoGroupHeightDip = BarMainBarHeightDip;

		[[nodiscard]] bool IsEmpty(const RECT& rect) noexcept
		{
			return rect.right <= rect.left || rect.bottom <= rect.top;
		}

		[[nodiscard]] RECT Normalize(const RECT& rect) noexcept
		{
			return RECT{
				(std::min)(rect.left, rect.right),
				(std::min)(rect.top, rect.bottom),
				(std::max)(rect.left, rect.right),
				(std::max)(rect.top, rect.bottom) };
		}

		void UnionInPlace(RECT& target, const RECT& source) noexcept
		{
			if (IsEmpty(source)) return;
			if (IsEmpty(target))
			{
				target = source;
				return;
			}
			target.left = (std::min)(target.left, source.left);
			target.top = (std::min)(target.top, source.top);
			target.right = (std::max)(target.right, source.right);
			target.bottom = (std::max)(target.bottom, source.bottom);
		}

		[[nodiscard]] RECT Inflate(const RECT& rect, LONG outset) noexcept
		{
			if (IsEmpty(rect)) return {};
			return RECT{ rect.left - outset, rect.top - outset,
				rect.right + outset, rect.bottom + outset };
		}

		[[nodiscard]] RECT ClipToSurface(const RECT& rect, const RECT& surface) noexcept
		{
			RECT result{
				(std::max)(rect.left, surface.left),
				(std::max)(rect.top, surface.top),
				(std::min)(rect.right, surface.right),
				(std::min)(rect.bottom, surface.bottom) };
			return IsEmpty(result) ? RECT{} : result;
		}

		[[nodiscard]] RECT DipToPixels(const BarSurfaceDipRect& dip,
			float scale) noexcept
		{
			if (!std::isfinite(scale) || scale <= 0.0F) scale = 1.0F;
			return RECT{
				static_cast<LONG>(std::floor(dip.left * scale)),
				static_cast<LONG>(std::floor(dip.top * scale)),
				static_cast<LONG>(std::ceil(dip.right * scale)),
				static_cast<LONG>(std::ceil(dip.bottom * scale)) };
		}

		[[nodiscard]] float NormalizeScale(float scale) noexcept
		{
			if (!std::isfinite(scale) || scale <= 0.0F) return 1.0F;
			return (std::clamp)(scale, 0.5F, 4.0F);
		}

		[[nodiscard]] COLORREF ThemeOr(COLORREF value,
			bool useTheme, BarThemeColorEnum themeColor) noexcept
		{
			return useTheme ? GetThemeColor(themeColor) : value;
		}

		std::mutex sharedLightingRegistryMutex;
		std::vector<BarSurfaceScene*> sharedLightingScenes;
		BarSurfaceSharedLighting sharedLighting{};
		std::uint64_t sharedLightingGeneration = 0;

		[[nodiscard]] bool SameSharedLighting(
			const BarSurfaceSharedLighting& left,
			const BarSurfaceSharedLighting& right) noexcept
		{
			return std::abs(left.primaryScreenX - right.primaryScreenX) <= 0.01
				&& std::abs(left.primaryScreenY - right.primaryScreenY) <= 0.01
				&& std::abs(left.primaryRadiusPixels
					- right.primaryRadiusPixels) <= 0.01
				&& std::abs(left.cursorScreenX - right.cursorScreenX) <= 0.01
				&& std::abs(left.cursorScreenY - right.cursorScreenY) <= 0.01
				&& std::abs(left.cursorRadiusPixels
					- right.cursorRadiusPixels) <= 0.01
				&& std::abs(left.cursorIntensity
					- right.cursorIntensity) <= 0.000001
				&& (left.drawingPenColor & 0x00FFFFFF)
					== (right.drawingPenColor & 0x00FFFFFF)
				&& std::abs(left.drawingPenColorBlend
					- right.drawingPenColorBlend) <= 0.000001
				&& std::abs(left.drawingLightOpacity
					- right.drawingLightOpacity) <= 0.000001
				&& left.primaryVisible == right.primaryVisible
				&& left.cursorVisible == right.cursorVisible
				&& left.edgeLightingEnabled == right.edgeLightingEnabled;
		}

		[[nodiscard]] bool SameSharedPrimaryLighting(
			const BarSurfaceSharedLighting& left,
			const BarSurfaceSharedLighting& right) noexcept
		{
			return std::abs(left.primaryScreenX - right.primaryScreenX) <= 0.01
				&& std::abs(left.primaryScreenY - right.primaryScreenY) <= 0.01
				&& std::abs(left.primaryRadiusPixels
					- right.primaryRadiusPixels) <= 0.01
				&& (left.drawingPenColor & 0x00FFFFFF)
					== (right.drawingPenColor & 0x00FFFFFF)
				&& std::abs(left.drawingPenColorBlend
					- right.drawingPenColorBlend) <= 0.000001
				&& std::abs(left.drawingLightOpacity
					- right.drawingLightOpacity) <= 0.000001
				&& left.primaryVisible == right.primaryVisible
				&& left.edgeLightingEnabled == right.edgeLightingEnabled;
		}

		[[nodiscard]] bool SameSharedCursorLighting(
			const BarSurfaceSharedLighting& left,
			const BarSurfaceSharedLighting& right) noexcept
		{
			return std::abs(left.cursorScreenX - right.cursorScreenX) <= 0.01
				&& std::abs(left.cursorScreenY - right.cursorScreenY) <= 0.01
				&& std::abs(left.cursorRadiusPixels
					- right.cursorRadiusPixels) <= 0.01
				&& std::abs(left.cursorIntensity
					- right.cursorIntensity) <= 0.000001
				&& (left.drawingPenColor & 0x00FFFFFF)
					== (right.drawingPenColor & 0x00FFFFFF)
				&& std::abs(left.drawingPenColorBlend
					- right.drawingPenColorBlend) <= 0.000001
				&& std::abs(left.drawingLightOpacity
					- right.drawingLightOpacity) <= 0.000001
				&& left.cursorVisible == right.cursorVisible
				&& left.edgeLightingEnabled == right.edgeLightingEnabled;
		}

	}

	struct BarSurfaceLightBorder
	{
		RECT outerBounds{};
		RECT contentBounds{};
		bool contributes = false;
	};

	struct BarSurfaceCursorLightDamageResolution
	{
		RECT current{};
		RECT damage{};
	};

	[[nodiscard]] BarSurfaceCursorLightDamageResolution
		ResolveBarSurfaceCursorLightDamage(
		const RECT& previous,
		const RECT& influenceBounds,
		std::span<const BarSurfaceLightBorder> borders) noexcept
	{
		BarSurfaceCursorLightDamageResolution result;
		for (const auto& border : borders)
		{
			if (!border.contributes) continue;
			BarDirtyRegionTracker::UnionInPlace(result.current,
				ResolveBarLightBorderDamage(border.outerBounds,
					border.contentBounds, influenceBounds));
		}
		result.damage = previous;
		BarDirtyRegionTracker::UnionInPlace(result.damage, result.current);
		return result;
	}

	BarUiFrameLightingSnapshot ResolveBarSurfaceFrameLightingSnapshot(
		const BarSurfaceSharedLighting& lighting,
		const RECT& logicalBounds, LONG presentationOutset) noexcept
	{
		BarUiFrameLightingSnapshot snapshot;
		const auto primaryPoint = ResolveBarSurfaceScreenPoint(
			lighting.primaryScreenX, lighting.primaryScreenY,
			logicalBounds.left, logicalBounds.top, presentationOutset);
		snapshot.primaryLight = D2D1::Point2F(
			static_cast<FLOAT>(primaryPoint.x),
			static_cast<FLOAT>(primaryPoint.y));
		snapshot.primaryRadius = static_cast<FLOAT>(
			std::isfinite(lighting.primaryRadiusPixels)
				&& lighting.primaryRadiusPixels > 0.0
				? lighting.primaryRadiusPixels : 0.0);
		const auto cursorPoint = ResolveBarSurfaceScreenPoint(
			lighting.cursorScreenX, lighting.cursorScreenY,
			logicalBounds.left, logicalBounds.top, presentationOutset);
	snapshot.cursorLight = D2D1::Point2F(
			static_cast<FLOAT>(cursorPoint.x),
			static_cast<FLOAT>(cursorPoint.y));
		snapshot.cursorScreenLight = D2D1::Point2F(
			static_cast<FLOAT>(lighting.cursorScreenX),
			static_cast<FLOAT>(lighting.cursorScreenY));
		snapshot.cursorRadius = static_cast<FLOAT>(
			std::isfinite(lighting.cursorRadiusPixels)
				&& lighting.cursorRadiusPixels > 0.0
				? lighting.cursorRadiusPixels : 0.0);
		snapshot.cursorIntensity = static_cast<FLOAT>(
			std::isfinite(lighting.cursorIntensity)
				? std::clamp(lighting.cursorIntensity, 0.0, 1.0) : 0.0);
		snapshot.drawingPenColor = lighting.drawingPenColor;
		snapshot.drawingPenColorBlend = lighting.drawingPenColorBlend;
		snapshot.drawingLightOpacity = lighting.drawingLightOpacity;
		snapshot.primaryLightVisible = lighting.primaryVisible;
		snapshot.cursorLightVisible = lighting.cursorVisible;
		snapshot.edgeLightingEnabled = lighting.edgeLightingEnabled;
		return snapshot;
	}

	bool StartBarButtonHoverVisual(BarButtonClass& button) noexcept
	{
		if (button.preset == BarButtonPresetEnum::Divider
			|| !button.button.fill.has_value()) return false;
		button.button.pct.animateWhenDisabled = true;
		button.button.fill->animateWhenDisabled = true;
		const BarUiCurveSpecClass curve{
			BarUiCurveEnum::EaseOutSine,
			BarUiCurveEnum::EaseOutSine, 0.0, false };
		button.button.fill->SetTar(
			GetThemeColor(BarThemeColorEnum::PressedFill),
			BarButtonHoverTransitionDuration, curve);
		button.button.pct.SetTar(BarButtonHoverOpacity,
			BarButtonHoverTransitionDuration, std::nullopt, true, curve);
		button.hoverStage = BarButtonHoverStageEnum::Showing;
		return true;
	}

	bool StopBarButtonHoverVisual(BarButtonClass& button, bool immediate,
		bool preserveVisual) noexcept
	{
		if (button.preset == BarButtonPresetEnum::Divider)
		{
			button.hoverStage = BarButtonHoverStageEnum::None;
			button.state->emph = BarWidgetEmphasize::None;
			button.pressScale.SetDirect(1.0);
			button.button.pct.animateWhenDisabled = false;
			if (button.button.fill.has_value())
				button.button.fill->animateWhenDisabled = false;
			return true;
		}
		if (!button.button.fill.has_value()) return false;
		if (immediate)
		{
			button.hoverStage = BarButtonHoverStageEnum::None;
			if (!preserveVisual) button.button.pct.SetDirect(0.0);
			button.button.pct.animateWhenDisabled = false;
			button.button.fill->animateWhenDisabled = false;
		}
		else
		{
			button.button.pct.animateWhenDisabled = true;
			button.button.fill->animateWhenDisabled = true;
			button.hoverStage = BarButtonHoverStageEnum::Fading;
			const BarUiCurveSpecClass curve{
				BarUiCurveEnum::EaseOutSine,
				BarUiCurveEnum::EaseOutSine, 0.0, false };
			button.button.pct.SetTar(0.0,
				BarButtonHoverTransitionDuration,
				std::nullopt, true, curve);
		}
		return true;
	}

	bool UpdateBarButtonHoverVisual(BarButtonClass& button, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept
	{
		auto Finish = [&]() noexcept
			{
				button.hoverStage = BarButtonHoverStageEnum::None;
				button.button.pct.animateWhenDisabled = false;
				if (button.button.fill.has_value())
					button.button.fill->animateWhenDisabled = false;
			};
		if (!visible)
		{
			if (button.hoverStage != BarButtonHoverStageEnum::None)
				button.button.pct.SetDirect(0.0);
			Finish();
			return true;
		}
		if (!hoverAllowed)
		{
			Finish();
			return true;
		}
		if (button.hoverStage == BarButtonHoverStageEnum::Showing
			&& button.button.pct.IsSame())
		{
			button.hoverStage = BarButtonHoverStageEnum::Fading;
			const BarUiCurveSpecClass curve{
				BarUiCurveEnum::EaseInSine,
				BarUiCurveEnum::EaseInSine, 0.0, false };
			button.button.pct.SetTar(0.0, fadeDurationSeconds,
				std::nullopt, true, curve);
			return true;
		}
		if (button.hoverStage == BarButtonHoverStageEnum::Fading
			&& button.button.pct.IsSame())
		{
			Finish();
			return true;
		}
		return false;
	}

	void SetBarButtonPressedVisual(BarButtonClass& button, bool pressed) noexcept
	{
		button.state->emph = pressed
			? BarWidgetEmphasize::Pressed : BarWidgetEmphasize::None;
	}

	void RetargetBarButtonInteractionVisual(BarButtonClass& button,
		bool visible, bool enabled, bool selected,
		double durationMilliseconds) noexcept
	{
		const bool pressed = button.state->emph
			== BarWidgetEmphasize::Pressed;
		if (!visible) button.button.pct.SetTar(0.0, durationMilliseconds);
		else if (pressed)
			button.button.pct.SetTar(BarButtonPressedOpacity,
				durationMilliseconds);
		else if (selected)
			button.button.pct.SetTar(0.20, durationMilliseconds);
		else if (button.hoverStage == BarButtonHoverStageEnum::None)
			button.button.pct.SetTar(0.0, durationMilliseconds);

		if (button.button.fill.has_value())
			button.button.fill->SetTar(GetThemeColor(selected
				? BarThemeColorEnum::Accent
				: BarThemeColorEnum::PressedFill));
		if (button.button.frameLightPct.has_value())
			button.button.frameLightPct->SetTar(
				visible && enabled && selected
					? (pressed ? BarButtonPressedLightOpacity : 1.0)
					: 0.0,
				durationMilliseconds);
		button.pressScale.SetTar(
			pressed ? BarButtonPressScale : 1.0,
			static_cast<double>(BarUiDefaultOperationDur),
			std::nullopt, false,
			pressed ? BarButtonPressCurve() : BarButtonReleaseCurve());

		const double contentOpacity = visible
			? (enabled ? 1.0 : BarButtonDisabledContentOpacity) : 0.0;
		button.icon.pct.SetTar(contentOpacity,
			BarButtonHoverTransitionDuration);
		button.name.pct.SetTar(contentOpacity,
			BarButtonHoverTransitionDuration);
		const COLORREF contentColor = GetThemeColor(selected
			? BarThemeColorEnum::Accent
			: BarThemeColorEnum::TextPrimary);
		if (button.icon.color1.has_value())
			button.icon.color1->SetTar(contentColor);
		button.name.color.SetTar(contentColor);
	}

	BarButtonVisualInheritance PrepareBarButtonVisualInheritance(
		BarButtonClass& button, const BarUiInheritClass& inherit,
		BarUiWordClass* secondary) noexcept
	{
		const BarUiInheritClass buttonInherit = button.button.UpInh(inherit);
		const auto iconPoint = ResolveBarButtonChildTopLeft(
			buttonInherit.x, buttonInherit.y,
			button.button.w.val, button.button.h.val,
			button.icon.x.val, button.icon.y.val,
			button.icon.w.val, button.icon.h.val);
		const BarUiInheritClass iconInherit = button.icon.UpInh(
			BarUiInheritClass(iconPoint.x, iconPoint.y));
		const auto primaryPoint = ResolveBarButtonChildTopLeft(
			buttonInherit.x, buttonInherit.y,
			button.button.w.val, button.button.h.val,
			button.name.x.val, button.name.y.val,
			button.name.w.val, button.name.h.val);
		const BarUiInheritClass primaryInherit = button.name.UpInh(
			BarUiInheritClass(primaryPoint.x, primaryPoint.y));
		std::optional<BarUiInheritClass> secondaryInherit;
		if (secondary)
		{
			const auto secondaryPoint = ResolveBarButtonChildTopLeft(
				buttonInherit.x, buttonInherit.y,
				button.button.w.val, button.button.h.val,
				secondary->x.val, secondary->y.val,
				secondary->w.val, secondary->h.val);
			secondaryInherit = secondary->UpInh(
				BarUiInheritClass(secondaryPoint.x, secondaryPoint.y));
		}
		return { buttonInherit, iconInherit, primaryInherit,
			secondaryInherit };
	}

	bool DrawBarButtonVisual(BarUIRendering& renderer,
		ID2D1DeviceContext* deviceContext, BarButtonClass& button,
		const BarUiInheritClass& inherit,
		const BarButtonDrawOptions& options)
	{
		if (!deviceContext) return false;
		const auto visualInheritance = PrepareBarButtonVisualInheritance(
			button, inherit, options.secondary);
		D2D1_MATRIX_3X2_F originalTransform{};
		deviceContext->GetTransform(&originalTransform);
		double pressScale = button.pressScale.val;
		if (!std::isfinite(pressScale) || pressScale <= 0.0)
			pressScale = 1.0;
		const bool transformChanged =
			std::abs(pressScale - 1.0) > 0.000001;
		if (transformChanged)
		{
			const FLOAT zoom = static_cast<FLOAT>(renderer.GetFrameZoom());
			const FLOAT centerX = static_cast<FLOAT>(
				(visualInheritance.button.x + button.button.w.val / 2.0) * zoom);
			const FLOAT centerY = static_cast<FLOAT>(
				(visualInheritance.button.y + button.button.h.val / 2.0) * zoom);
			deviceContext->SetTransform(D2D1::Matrix3x2F::Scale(
				static_cast<FLOAT>(pressScale),
				static_cast<FLOAT>(pressScale),
				D2D1::Point2F(centerX, centerY)) * originalTransform);
		}

		bool rendered = renderer.Shape(
			deviceContext, button.button, visualInheritance.button);
		if (button.preset != BarButtonPresetEnum::Divider)
		{
			if (button.iconKind == BarButtonIconKindEnum::Png)
			{
				button.pngIcon.x.SetDirect(button.icon.x.val);
				button.pngIcon.y.SetDirect(button.icon.y.val);
				button.pngIcon.w.SetDirect(button.icon.w.val);
				button.pngIcon.h.SetDirect(button.icon.h.val);
				button.pngIcon.angle.SetDirect(button.icon.angle.val);
				button.pngIcon.pct.SetDirect(button.icon.pct.val);
				button.pngIcon.enable.val = button.icon.enable.val;
				button.pngIcon.enable.tar = button.icon.enable.tar;
				rendered = renderer.Png(deviceContext, button.pngIcon,
					button.pngIcon.UpInh(visualInheritance.icon)) || rendered;
			}
			else
				rendered = renderer.Svg(deviceContext, button.icon,
					visualInheritance.icon) || rendered;
			rendered = renderer.Word(deviceContext, button.name,
				visualInheritance.primary,
				options.primaryWeight) || rendered;
			if (options.secondary && visualInheritance.secondary)
				rendered = renderer.Word(deviceContext, *options.secondary,
					*visualInheritance.secondary,
					options.secondaryWeight) || rendered;
		}
		if (transformChanged) deviceContext->SetTransform(originalTransform);
		return rendered;
	}

	bool DrawBarBackgroundVisual(BarUIRendering& renderer,
		ID2D1DeviceContext* deviceContext, BarUiShapeClass& background,
		const BarUiInheritClass& inherit, RECT* targetRect, bool clip)
	{
		return renderer.Shape(deviceContext, background, inherit,
			targetRect, clip);
	}

	struct BarSurfaceScene::Impl
	{
		struct Widget
		{
			BarSurfaceWidgetSpec spec{};
			std::shared_ptr<BarButtonClass> button;
			BarUiShapeClass dragHandle;
			BarUiWordClass secondary;
			bool hasSecondary = false;
			RECT lastPixels{};
		};

		mutable std::mutex mutex;
		// 每个 surface 独立持有 Rendering 与字体缓存，避免共享主栏实例的跨帧状态。
		BarUISetClass rendererOwner;
		BarSurfaceBackgroundSpec background{};
		BarUiShapeClass backgroundShape;
		BarUiPctClass surfaceOpacity;
		std::vector<Widget> widgets;
		RECT logicalBounds{ 0, 0, 1, 1 };
		float dpiScale = 1.0F;
		double damageOutsetDip = kDefaultDamageOutsetDip;
		RECT pendingDamage{};
		bool invalidated = true;
		bool pointerCaptured = false;
		BarSurfaceWidgetId hovered = BarSurfaceNoWidget;
		BarSurfaceWidgetId pressed = BarSurfaceNoWidget;
		std::chrono::steady_clock::time_point lastFrame{};
		bool animationActive = false;
		BarSurfaceHooks hooks;
		bool sharedLightingSubscribed = false;
		std::uint64_t appliedSharedLightingGeneration = 0;
		BarSurfaceSharedLighting appliedSharedLighting{};
		RECT appliedSharedLightingBounds{};
		LONG appliedSharedLightingOutset = -1;
		POINT pointerLocal{};
		bool hoverSuppressed = false;
		POINT hoverSuppressionPoint{};
		RECT cursorLightDamageBounds{};

		[[nodiscard]] RECT WidgetPixelsLocked(const Widget& widget) const noexcept
		{
			const double scale = dpiScale > 0.0F ? dpiScale : 1.0F;
			const double width = (std::max)(0.0,
				static_cast<double>(widget.button->button.w.val));
			const double height = (std::max)(0.0,
				static_cast<double>(widget.button->button.h.val));
			const double left = widget.button->button.x.val - width / 2.0;
			const double top = widget.button->button.y.val - height / 2.0;
			return RECT{
				static_cast<LONG>(std::floor(left * scale)),
				static_cast<LONG>(std::floor(top * scale)),
				static_cast<LONG>(std::ceil((left + width) * scale)),
				static_cast<LONG>(std::ceil((top + height) * scale)) };
		}

		void SyncDragHandleGeometryLocked(Widget& widget) noexcept
		{
			if (widget.spec.kind != BarSurfaceWidgetKind::DragHandle) return;
			const double width = (std::max)(0.0,
				static_cast<double>(widget.button->button.w.val));
			const double height = (std::max)(0.0,
				static_cast<double>(widget.button->button.h.val));
			const bool vertical = width <= height;
			const double length = (std::max)(2.0,
				(std::min)(vertical ? height : width, 20.0));
			widget.dragHandle.x.SetDirect(widget.button->button.x.val);
			widget.dragHandle.y.SetDirect(widget.button->button.y.val);
			widget.dragHandle.w.SetDirect(vertical ? 2.0 : length);
			widget.dragHandle.h.SetDirect(vertical ? length : 2.0);
		}

		[[nodiscard]] RECT LocalSurfaceRect() const noexcept
		{
			return RECT{ 0, 0,
				logicalBounds.right - logicalBounds.left,
				logicalBounds.bottom - logicalBounds.top };
		}

		[[nodiscard]] LONG DamageOutsetPixels() const noexcept
		{
			const double value = std::isfinite(damageOutsetDip)
				? damageOutsetDip : kDefaultDamageOutsetDip;
			return static_cast<LONG>(std::ceil((std::max)(0.0,
				value) * static_cast<double>(dpiScale)));
		}

		[[nodiscard]] RECT PresentationBoundsLocked() const noexcept
		{
			const LONG outset = DamageOutsetPixels();
			return RECT{
				logicalBounds.left - outset,
				logicalBounds.top - outset,
				logicalBounds.right + outset,
				logicalBounds.bottom + outset };
		}

		[[nodiscard]] RECT PresentationLocalRect() const noexcept
		{
			const LONG outset = DamageOutsetPixels();
			return RECT{ 0, 0,
				logicalBounds.right - logicalBounds.left + outset * 2,
				logicalBounds.bottom - logicalBounds.top + outset * 2 };
		}

		void IncludePresentationDamageLocked(const RECT& rect) noexcept
		{
			const RECT damage = ClipToSurface(rect, PresentationLocalRect());
			if (IsEmpty(damage)) return;
			UnionInPlace(pendingDamage, damage);
			invalidated = true;
		}

		[[nodiscard]] BarSurfaceLightBorder ShapeLightBorderLocked(
			const BarUiShapeClass& shape, double visualScale = 1.0) const noexcept
		{
			BarSurfaceLightBorder result;
			if (!std::isfinite(visualScale) || visualScale <= 0.0
				|| !shape.enable.val || !shape.frame.has_value()
				|| shape.frameRendering != BarUiFrameRenderingEnum::PointLight
				|| shape.w.val <= 0.0 || shape.h.val <= 0.0
				|| shape.frameCursorLightIntensityScale <= 0.0)
				return result;

			double lightOpacity = shape.frameLightPct.has_value()
				? static_cast<double>(shape.frameLightPct->val)
				: (shape.framePct.has_value()
					? static_cast<double>(shape.framePct->val)
					: static_cast<double>(shape.pct.val));
			if (!shape.frameLightPct.has_value()
				&& shape.frameLightOpacitySource
					== BarUiFrameLightOpacitySourceEnum::ObjectPct)
				lightOpacity = static_cast<double>(shape.pct.val);
			const double frameThickness = shape.ft.has_value()
				? static_cast<double>(shape.ft->val) : 4.0;
			if (!std::isfinite(lightOpacity) || lightOpacity <= 0.0001
				|| !std::isfinite(frameThickness) || frameThickness <= 0.0)
				return result;

			const double scale = static_cast<double>(dpiScale);
			const LONG outset = DamageOutsetPixels();
			const double halfWidth = shape.w.val * visualScale / 2.0;
			const double halfHeight = shape.h.val * visualScale / 2.0;
			result.contentBounds = RECT{
				static_cast<LONG>(std::floor(
					(shape.x.val - halfWidth) * scale)) + outset,
				static_cast<LONG>(std::floor(
					(shape.y.val - halfHeight) * scale)) + outset,
				static_cast<LONG>(std::ceil(
					(shape.x.val + halfWidth) * scale)) + outset,
				static_cast<LONG>(std::ceil(
					(shape.y.val + halfHeight) * scale)) + outset,
			};
			const LONG frameOutset = static_cast<LONG>(std::ceil((
				frameThickness + BarRenderingAttribute::pointLightDiffuseExtraWidth)
				* scale * visualScale))
				+ BarRenderingAttribute::dirtyAntialiasPadding;
			result.outerBounds = Inflate(result.contentBounds, frameOutset);
			result.contributes = true;
			return result;
		}

		void UpdateCursorLightDamageLocked(bool cursorLightChanged) noexcept
		{
			std::vector<BarSurfaceLightBorder> borders;
			borders.reserve(widgets.size() + 1);
			const bool surfaceVisible = surfaceOpacity.val > 0.0001;
			if (surfaceVisible && background.visible)
				borders.push_back(ShapeLightBorderLocked(backgroundShape));
			if (surfaceVisible)
			{
				for (const auto& widget : widgets)
				{
					if (!widget.button || !widget.spec.visible
						|| widget.spec.kind != BarSurfaceWidgetKind::Button)
						continue;
					borders.push_back(ShapeLightBorderLocked(
						widget.button->button, widget.button->pressScale.val));
				}
			}
			const auto resolved = ResolveBarSurfaceCursorLightDamage(
				cursorLightDamageBounds,
				rendererOwner.spec.GetFrameCursorLightDamageBounds(), borders);
			const bool boundsChanged = !EqualRect(
				&cursorLightDamageBounds, &resolved.current);
			if (cursorLightChanged || boundsChanged)
				IncludePresentationDamageLocked(resolved.damage);
			cursorLightDamageBounds = resolved.current;
		}

		[[nodiscard]] std::optional<POINT> LogicalPointFromPresentation(
			POINT point) const noexcept
		{
			const LONG outset = DamageOutsetPixels();
			const RECT presentation = PresentationLocalRect();
			if (point.x < outset || point.y < outset
				|| point.x >= presentation.right - outset
				|| point.y >= presentation.bottom - outset)
				return std::nullopt;
			return POINT{ point.x - outset, point.y - outset };
		}

		void IncludeDamageLocked(const RECT& rect) noexcept
		{
			RECT damage = Inflate(rect, DamageOutsetPixels());
			const LONG outset = DamageOutsetPixels();
			OffsetRect(&damage, outset, outset);
			UnionInPlace(pendingDamage,
				ClipToSurface(damage, PresentationLocalRect()));
			invalidated = true;
		}

		void IncludeFullDamageLocked() noexcept
		{
			pendingDamage = PresentationLocalRect();
			invalidated = true;
		}

		[[nodiscard]] Widget* FindWidgetLocked(BarSurfaceWidgetId id) noexcept
		{
			if (id == BarSurfaceNoWidget) return nullptr;
			for (auto& widget : widgets)
				if (widget.spec.id == id) return &widget;
			return nullptr;
		}

		[[nodiscard]] const Widget* FindWidgetLocked(BarSurfaceWidgetId id) const noexcept
		{
			if (id == BarSurfaceNoWidget) return nullptr;
			for (const auto& widget : widgets)
				if (widget.spec.id == id) return &widget;
			return nullptr;
		}

		void ApplySharedLightingLocked(
			const BarSurfaceSharedLighting& lighting,
			std::uint64_t generation)
		{
			const LONG outset = DamageOutsetPixels();
			if (appliedSharedLightingGeneration == generation
				&& EqualRect(&appliedSharedLightingBounds, &logicalBounds)
				&& appliedSharedLightingOutset == outset)
				return;

			const bool mappingChanged = !EqualRect(
				&appliedSharedLightingBounds, &logicalBounds)
				|| appliedSharedLightingOutset != outset;
			const bool primaryChanged = mappingChanged
				|| !SameSharedPrimaryLighting(appliedSharedLighting, lighting);
			const bool cursorChanged = mappingChanged
				|| !SameSharedCursorLighting(appliedSharedLighting, lighting);
			const auto snapshot = ResolveBarSurfaceFrameLightingSnapshot(
				lighting, logicalBounds, outset);
			rendererOwner.spec.SetFrameLightingSnapshot(snapshot);
			if (cursorChanged) UpdateCursorLightDamageLocked(true);
			if (primaryChanged) IncludeFullDamageLocked();
			appliedSharedLighting = lighting;
			appliedSharedLightingGeneration = generation;
			appliedSharedLightingBounds = logicalBounds;
			appliedSharedLightingOutset = outset;
		}

		void InitializeBackgroundLocked()
		{
			const auto fillColor = ThemeOr(background.fill,
				background.useThemeColors, BarThemeColorEnum::Surface);
			const auto frameColor = ThemeOr(background.frame,
				background.useThemeColors, BarThemeColorEnum::SurfaceFrame);
			const auto fill = background.fillOpacity > 0.0
				? std::optional<COLORREF>(fillColor) : std::nullopt;
			const auto frame = background.frameOpacity > 0.0
				? std::optional<COLORREF>(frameColor) : std::nullopt;
			backgroundShape.Initialization(
				background.bounds.left + background.bounds.Width() / 2.0,
				background.bounds.top + background.bounds.Height() / 2.0,
				background.bounds.Width(), background.bounds.Height(),
				background.cornerRadiusDip, background.cornerRadiusDip,
				background.frameThicknessDip, fill, frame);
			backgroundShape.enable.Initialization(background.visible);
			backgroundShape.frameRendering = BarUiFrameRenderingEnum::PointLight;
			backgroundShape.frameLightColor =
				BarUiFrameLightColorEnum::PenWhenDrawing;
			backgroundShape.framePrimaryLightEnabled = true;
			backgroundShape.pct.SetDirect((std::clamp)(background.fillOpacity,
				0.0, 1.0));
			if (!backgroundShape.framePct.has_value())
				backgroundShape.framePct.emplace(0.0);
			backgroundShape.framePct->SetDirect((std::clamp)(
				background.frameOpacity, 0.0, 1.0));
			if (!backgroundShape.frameLightPct.has_value())
				backgroundShape.frameLightPct.emplace(0.0);
			backgroundShape.frameLightPct->SetDirect((std::clamp)(
				background.frameOpacity, 0.0, 1.0));
			if (!std::isfinite(surfaceOpacity.val))
				surfaceOpacity.SetDirect(1.0);
		}

		void ApplySharedButtonMetricsLocked(Widget& widget)
		{
			if (widget.spec.layoutKind
				== BarButtonVisualLayoutKind::Custom) return;
			const auto metrics = ResolveBarButtonVisualMetrics(
				widget.spec.layoutKind);
			widget.spec.iconSizeDip = metrics.iconSizeDip;
			widget.spec.iconOffsetXDip = metrics.iconOffsetXDip;
			widget.spec.iconOffsetYDip = metrics.iconOffsetYDip;
			widget.spec.primaryFontSizeDip = metrics.primaryFontSizeDip;
			widget.spec.primaryOffsetXDip = metrics.primaryOffsetXDip;
			widget.spec.primaryOffsetYDip = metrics.primaryOffsetYDip;
			widget.spec.primarySlotWidthDip = metrics.primarySlotWidthDip;
			widget.spec.primarySlotHeightDip = metrics.primarySlotHeightDip;
			widget.spec.secondaryFontSizeDip = metrics.secondaryFontSizeDip;
			widget.spec.secondaryOffsetXDip = metrics.secondaryOffsetXDip;
			widget.spec.secondaryOffsetYDip = metrics.secondaryOffsetYDip;
			widget.spec.secondarySlotWidthDip = metrics.secondarySlotWidthDip;
			widget.spec.secondarySlotHeightDip = metrics.secondarySlotHeightDip;
			if (widget.button)
			{
				switch (widget.spec.layoutKind)
				{
				case BarButtonVisualLayoutKind::StandardOneOne:
					widget.button->size = BarButtonSizeEnum::oneOne;
					break;
				case BarButtonVisualLayoutKind::StandardTwoOne:
				case BarButtonVisualLayoutKind::PageHorizontal:
					widget.button->size = BarButtonSizeEnum::twoOne;
					break;
				case BarButtonVisualLayoutKind::StandardTwoTwo:
				case BarButtonVisualLayoutKind::PageTwoTwo:
					widget.button->size = BarButtonSizeEnum::twoTwo;
					break;
				default:
					break;
				}
			}

			if (widget.spec.layoutKind
				!= BarButtonVisualLayoutKind::PageHorizontal) return;
			if (!rendererOwner.barMedia.formatCache)
				rendererOwner.barMedia.LoadFormat();
			const auto primary = rendererOwner.spec.MeasureText(
				widget.spec.primaryText, metrics.primaryFontSizeDip,
				DWRITE_FONT_WEIGHT_BOLD);
			const auto secondary = rendererOwner.spec.MeasureText(
				widget.spec.secondaryText, metrics.secondaryFontSizeDip,
				DWRITE_FONT_WEIGHT_NORMAL);
			const double primaryWidth = (std::max)(1.0,
				static_cast<double>(primary.width));
			const double secondaryWidth = (std::max)(1.0,
				static_cast<double>(secondary.width));
			const double totalWidth = primaryWidth + secondaryWidth;
			widget.spec.primarySlotWidthDip = primaryWidth;
			widget.spec.secondarySlotWidthDip = secondaryWidth;
			widget.spec.primaryOffsetXDip =
				-totalWidth / 2.0 + primaryWidth / 2.0;
			widget.spec.secondaryOffsetXDip =
				-totalWidth / 2.0 + primaryWidth + secondaryWidth / 2.0;
		}

		void InitializeButtonLocked(Widget& widget)
		{
			if (!widget.button) widget.button = std::make_shared<BarButtonClass>();
			widget.button->size = BarButtonSizeEnum::twoTwo;
			ApplySharedButtonMetricsLocked(widget);
			const auto fill = ThemeOr(widget.spec.fill, widget.spec.useThemeColors,
				BarThemeColorEnum::PressedFill);
			const auto content = ThemeOr(widget.spec.content,
				widget.spec.useThemeColors, BarThemeColorEnum::TextPrimary);
			const double width = widget.spec.bounds.Width();
			const double height = widget.spec.bounds.Height();
			const double centerX = widget.spec.bounds.left + width / 2.0;
			const double centerY = widget.spec.bounds.top + height / 2.0;

			widget.button->userVisible = widget.spec.visible;
			widget.button->hide = !widget.spec.visible;
			widget.button->only = true;
			widget.button->button.Initialization(centerX, centerY, width, height,
				BarButtonCornerRadiusDip, BarButtonCornerRadiusDip,
				BarButtonFrameThicknessDip, fill, content);
			widget.button->button.enable.Initialization(widget.spec.visible);
			widget.button->button.pct.SetDirect(0.0);
			if (!widget.button->button.framePct.has_value())
				widget.button->button.framePct.emplace(0.0);
			if (!widget.button->button.frameLightPct.has_value())
				widget.button->button.frameLightPct.emplace(0.0);
			widget.button->button.frameRendering =
				BarUiFrameRenderingEnum::PointLight;
			widget.button->button.frameLightColor =
				BarUiFrameLightColorEnum::PenWhenDrawing;
			widget.button->button.framePrimaryLightEnabled = false;
			widget.button->button.frameCursorLightIntensityScale =
				BarButtonCursorLightIntensity;

			const double primaryOffsetY = std::isfinite(
				widget.spec.primaryOffsetYDip)
				? widget.spec.primaryOffsetYDip
				: BarButtonTwoTwoIconOffsetYDip;
			const double primarySlotHeight = std::isfinite(
				widget.spec.primarySlotHeightDip)
				&& widget.spec.primarySlotHeightDip > 0.0
				? widget.spec.primarySlotHeightDip
				: BarButtonTwoTwoIconSizeDip;
			const double primarySlotWidth = std::isfinite(
				widget.spec.primarySlotWidthDip)
				&& widget.spec.primarySlotWidthDip > 0.0
				? widget.spec.primarySlotWidthDip : width;
			widget.button->name.Initialization(widget.spec.primaryOffsetXDip,
				primaryOffsetY, primarySlotWidth,
				primarySlotHeight, widget.spec.primaryText,
				std::isfinite(widget.spec.primaryFontSizeDip)
					? widget.spec.primaryFontSizeDip : BarButtonTwoTwoIconSizeDip,
				content);
			widget.button->name.enable.Initialization(!widget.spec.primaryText.empty());
			widget.button->name.pct.SetDirect(1.0);

			widget.hasSecondary = !widget.spec.secondaryText.empty();
			const double secondaryOffsetY = std::isfinite(
				widget.spec.secondaryOffsetYDip)
				? widget.spec.secondaryOffsetYDip
				: BarButtonTwoTwoLabelOffsetYDip;
			const double secondarySlotHeight = std::isfinite(
				widget.spec.secondarySlotHeightDip)
				&& widget.spec.secondarySlotHeightDip > 0.0
				? widget.spec.secondarySlotHeightDip
				: BarButtonTwoTwoLabelHeightDip;
			const double secondarySlotWidth = std::isfinite(
				widget.spec.secondarySlotWidthDip)
				&& widget.spec.secondarySlotWidthDip > 0.0
				? widget.spec.secondarySlotWidthDip : width;
			widget.secondary.Initialization(widget.spec.secondaryOffsetXDip,
				secondaryOffsetY, secondarySlotWidth, secondarySlotHeight,
				widget.spec.secondaryText,
				std::isfinite(widget.spec.secondaryFontSizeDip)
					? widget.spec.secondaryFontSizeDip
					: BarButtonTwoTwoLabelFontSizeDip, content);
			widget.secondary.enable.Initialization(widget.hasSecondary);
			widget.secondary.pct.SetDirect(1.0);

			widget.button->icon.Initialization(widget.spec.iconOffsetXDip,
				std::isfinite(widget.spec.iconOffsetYDip)
					? widget.spec.iconOffsetYDip : 0.0,
				content, std::nullopt);
			widget.button->icon.enable.Initialization(false);
			widget.button->icon.pct.SetDirect(0.0);
			if (!widget.spec.iconResource.empty())
			{
				widget.button->icon.InitializationFromResource(L"UI",
					widget.spec.iconResource);
				const double iconSize = std::isfinite(widget.spec.iconSizeDip)
					? widget.spec.iconSizeDip : BarButtonTwoTwoIconSizeDip;
				widget.button->icon.SetWH(iconSize, iconSize);
				// 独立 surface 不经过主栏的几何动画队列，SVG 尺寸必须立即写入 val，避免 Svg 的零尺寸保护跳过绘制。
				widget.button->icon.w.SetDirect(iconSize);
				widget.button->icon.h.SetDirect(iconSize);
				widget.button->icon.enable.Initialization(true);
				widget.button->icon.pct.SetDirect(1.0);
				if (widget.spec.iconAngle.has_value())
					widget.button->icon.angle.SetDirect(*widget.spec.iconAngle);
			}
			widget.button->clickFunc = widget.spec.onClick;
			widget.lastPixels = DipToPixels(widget.spec.bounds, dpiScale);
			const auto handleColor = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::SurfaceFrame);
			widget.dragHandle.Initialization(centerX, centerY, 2.0, 20.0,
				1.0, 1.0, std::nullopt, handleColor, std::nullopt);
			widget.dragHandle.enable.Initialization(
				widget.spec.kind == BarSurfaceWidgetKind::DragHandle);
			widget.dragHandle.pct.SetDirect(widget.spec.dragOpacity);
			SyncDragHandleGeometryLocked(widget);
		}

		void ApplyButtonInteractionTargetsLocked(Widget& widget)
		{
			if (widget.spec.kind == BarSurfaceWidgetKind::DragHandle)
			{
				widget.button->button.pct.SetTar(0.0,
					BarButtonHoverTransitionDuration);
				widget.button->pressScale.SetTar(1.0,
					static_cast<double>(BarUiDefaultOperationDur));
				return;
			}
			const bool enabled = widget.spec.visible && widget.spec.enabled;
			SetBarButtonPressedVisual(*widget.button,
				pressed == widget.spec.id);
			widget.button->state->state = widget.spec.selected
				? BarWidgetState::Selected : BarWidgetState::None;
			RetargetBarButtonInteractionVisual(*widget.button,
				widget.spec.visible, enabled, widget.spec.selected,
				BarButtonHoverTransitionDuration);
		}

		void ApplyButtonTargetsLocked(Widget& widget)
		{
			if (widget.spec.kind == BarSurfaceWidgetKind::Button)
				(void)UpdateBarButtonHoverVisual(*widget.button,
					widget.spec.visible,
					widget.spec.enabled && widget.spec.interactive
						&& !widget.spec.selected,
					BarButtonHoverFadeDurationSeconds);
			ApplyButtonInteractionTargetsLocked(widget);
			const bool enabled = widget.spec.visible && widget.spec.enabled;
			// 禁用只关闭交互和背景反馈，内容仍以低透明度可读。
			const double contentOpacity = enabled ? 1.0
				: BarButtonDisabledContentOpacity;
			widget.button->icon.pct.SetTar(
				widget.spec.iconResource.empty() ? 0.0 : contentOpacity,
				BarButtonHoverTransitionDuration);
			widget.button->name.pct.SetTar(contentOpacity,
				BarButtonHoverTransitionDuration);
			if (widget.hasSecondary)
				widget.secondary.pct.SetTar(contentOpacity,
					BarButtonHoverTransitionDuration);
			if (widget.spec.kind == BarSurfaceWidgetKind::DragHandle)
				widget.dragHandle.pct.SetTar(widget.spec.dragOpacity,
					BarButtonHoverTransitionDuration);
		}

		[[nodiscard]] bool AdvanceAnimationsLocked(
			std::chrono::steady_clock::time_point frameTime) noexcept
		{
			double dt = 0.0;
			if (lastFrame.time_since_epoch().count() != 0)
				dt = std::chrono::duration<double>(frameTime - lastFrame).count();
			lastFrame = frameTime;
			if (!std::isfinite(dt) || dt < 0.0) dt = 0.0;
			dt = (std::clamp)(dt, 0.0, 0.05);
			double speed = static_cast<double>(BarUiAnimationSpeedRate);
			if (!std::isfinite(speed) || speed <= 0.0) speed = 1.0;
			rendererOwner.spec.SetFrameZoom(dpiScale);
			const BarUiAnimationAdvanceContextClass context{
				dt, speed, static_cast<bool>(BarUiAnimationEnabled), false };
			bool active = false;
			const auto includeBackground = [&](const auto& advance)
				{
					active = active || advance.active;
					if (advance.changed) IncludeFullDamageLocked();
				};
			includeBackground(BarUiAdvanceAnimation(backgroundShape.x, context));
			includeBackground(BarUiAdvanceAnimation(backgroundShape.y, context));
			includeBackground(BarUiAdvanceAnimation(backgroundShape.w, context));
			includeBackground(BarUiAdvanceAnimation(backgroundShape.h, context));
			includeBackground(BarUiAdvanceAnimation(backgroundShape.rw.value(), context));
			includeBackground(BarUiAdvanceAnimation(backgroundShape.rh.value(), context));
			includeBackground(BarUiAdvanceAnimation(surfaceOpacity, context));
			for (auto& widget : widgets)
			{
				const RECT previousPixels = widget.lastPixels;
				ApplyButtonTargetsLocked(widget);
				const auto include = [&](const auto& result)
				{
					active = active || result.active;
					if (result.changed) IncludeDamageLocked(previousPixels);
				};
				if (widget.button->icon.AdvanceContentTransition(dt, speed))
				{
					active = true;
					IncludeDamageLocked(widget.lastPixels);
				}
				if (widget.button->name.AdvanceContentTransition(dt, speed))
				{
					active = true;
					IncludeDamageLocked(widget.lastPixels);
				}
				if (widget.hasSecondary
					&& widget.secondary.AdvanceContentTransition(dt, speed))
				{
					active = true;
					IncludeDamageLocked(widget.lastPixels);
				}
				include(BarUiAdvanceAnimation(widget.button->button.pct, context));
				include(BarUiAdvanceAnimation(widget.button->button.frameLightPct.value(), context));
				include(BarUiAdvanceAnimation(widget.button->pressScale, context));
				include(BarUiAdvanceAnimation(widget.button->button.x, context));
				include(BarUiAdvanceAnimation(widget.button->button.y, context));
				include(BarUiAdvanceAnimation(widget.button->button.w, context));
				include(BarUiAdvanceAnimation(widget.button->button.h, context));
				include(BarUiAdvanceAnimation(widget.button->button.rw.value(), context));
				include(BarUiAdvanceAnimation(widget.button->button.rh.value(), context));
				include(BarUiAdvanceAnimation(widget.button->icon.x, context));
				include(BarUiAdvanceAnimation(widget.button->icon.y, context));
				include(BarUiAdvanceAnimation(widget.button->icon.w, context));
				include(BarUiAdvanceAnimation(widget.button->icon.h, context));
				include(BarUiAdvanceAnimation(widget.button->icon.angle, context));
				include(BarUiAdvanceAnimation(widget.button->icon.pct, context));
				include(BarUiAdvanceAnimation(widget.button->name.x, context));
				include(BarUiAdvanceAnimation(widget.button->name.y, context));
				include(BarUiAdvanceAnimation(widget.button->name.w, context));
				include(BarUiAdvanceAnimation(widget.button->name.h, context));
				include(BarUiAdvanceAnimation(widget.button->name.size, context));
				include(BarUiAdvanceAnimation(widget.button->name.pct, context));
				if (widget.hasSecondary)
				{
					include(BarUiAdvanceAnimation(widget.secondary.x, context));
					include(BarUiAdvanceAnimation(widget.secondary.y, context));
					include(BarUiAdvanceAnimation(widget.secondary.w, context));
					include(BarUiAdvanceAnimation(widget.secondary.h, context));
					include(BarUiAdvanceAnimation(widget.secondary.size, context));
					include(BarUiAdvanceAnimation(widget.secondary.pct, context));
				}
				if (widget.spec.kind == BarSurfaceWidgetKind::DragHandle)
					include(BarUiAdvanceAnimation(widget.dragHandle.pct, context));
				SyncDragHandleGeometryLocked(widget);
				widget.lastPixels = WidgetPixelsLocked(widget);
				if (!EqualRect(&previousPixels, &widget.lastPixels))
				{
					IncludeDamageLocked(previousPixels);
					IncludeDamageLocked(widget.lastPixels);
				}
			}
			animationActive = active;
			return active;
		}

		[[nodiscard]] BarSurfaceWidgetId HitTestLocked(POINT localPixels) const noexcept
		{
			const RECT logical = LocalSurfaceRect();
			if (!PtInRect(&logical, localPixels))
				return BarSurfaceNoWidget;
			// 拖动条可覆盖页码按钮的一小段区域，必须优先命中。
			for (auto it = widgets.rbegin(); it != widgets.rend(); ++it)
			{
				if (it->spec.kind != BarSurfaceWidgetKind::DragHandle
					|| !it->spec.visible || !it->spec.enabled
					|| !it->spec.interactive) continue;
				if (PtInRect(&it->lastPixels, localPixels))
					return it->spec.id;
			}
			for (auto it = widgets.rbegin(); it != widgets.rend(); ++it)
			{
				if (it->spec.kind == BarSurfaceWidgetKind::DragHandle) continue;
				if (!it->spec.visible || !it->spec.enabled
					|| !it->spec.interactive) continue;
				const double width = it->button->button.w.val;
				const double height = it->button->button.h.val;
				const double left = static_cast<double>(
					it->button->button.x.val) - width / 2.0;
				const double top = static_cast<double>(
					it->button->button.y.val) - height / 2.0;
				const double radiusX = it->button->button.rw.has_value()
					? static_cast<double>(it->button->button.rw->val) : 0.0;
				const double radiusY = it->button->button.rh.has_value()
					? static_cast<double>(it->button->button.rh->val) : 0.0;
				if (BarUiRoundedRectContainsPoint(localPixels.x, localPixels.y,
					dpiScale, left, top, width, height, radiusX, radiusY))
					return it->spec.id;
			}
			return BarSurfaceNoWidget;
		}

		[[nodiscard]] bool HitTestBackgroundLocked(POINT localPixels) const noexcept
		{
			if (!background.visible || !backgroundShape.enable.val) return false;
			const double width = static_cast<double>(backgroundShape.w.val);
			const double height = static_cast<double>(backgroundShape.h.val);
			const double left = static_cast<double>(backgroundShape.x.val) - width / 2.0;
			const double top = static_cast<double>(backgroundShape.y.val) - height / 2.0;
			const double radiusX = backgroundShape.rw.has_value()
				? static_cast<double>(backgroundShape.rw->val) : 0.0;
			const double radiusY = backgroundShape.rh.has_value()
				? static_cast<double>(backgroundShape.rh->val) : 0.0;
			return BarUiRoundedRectContainsPoint(localPixels.x, localPixels.y,
				dpiScale, left, top, width, height, radiusX, radiusY);
		}
	};

	BarSurfaceScene::BarSurfaceScene()
		: impl_(std::make_unique<Impl>())
	{
		std::lock_guard lock(sharedLightingRegistryMutex);
		sharedLightingScenes.push_back(this);
	}

	BarSurfaceScene::~BarSurfaceScene()
	{
		std::lock_guard lock(sharedLightingRegistryMutex);
		sharedLightingScenes.erase(
			std::remove(sharedLightingScenes.begin(),
				sharedLightingScenes.end(), this),
			sharedLightingScenes.end());
	}

	BarSurfaceScene::BarSurfaceScene(BarSurfaceScene&& other) noexcept
		: impl_(std::move(other.impl_))
	{
		std::lock_guard lock(sharedLightingRegistryMutex);
		for (auto& scene : sharedLightingScenes)
			if (scene == &other) scene = this;
	}

	BarSurfaceScene& BarSurfaceScene::operator=(BarSurfaceScene&& other) noexcept
	{
		if (this == &other) return *this;
		{
			std::lock_guard lock(sharedLightingRegistryMutex);
			sharedLightingScenes.erase(
				std::remove(sharedLightingScenes.begin(),
					sharedLightingScenes.end(), this),
				sharedLightingScenes.end());
			for (auto& scene : sharedLightingScenes)
				if (scene == &other) scene = this;
		}
		impl_ = std::move(other.impl_);
		return *this;
	}

	bool BarSurfaceScene::Configure(const BarSurfaceBackgroundSpec& background,
		std::span<const BarSurfaceWidgetSpec> widgets)
	{
		std::vector<BarSurfaceWidgetSpec> copy(widgets.begin(), widgets.end());
		{
			std::lock_guard lock(impl_->mutex);
			impl_->background = background;
			impl_->InitializeBackgroundLocked();
			impl_->surfaceOpacity.SetDirect(1.0);
			impl_->widgets.clear();
			impl_->widgets.reserve(copy.size());
			for (auto& spec : copy)
			{
				if (spec.id == BarSurfaceNoWidget) continue;
				Impl::Widget widget;
				widget.spec = std::move(spec);
				impl_->InitializeButtonLocked(widget);
				impl_->widgets.emplace_back(std::move(widget));
			}
			impl_->hovered = BarSurfaceNoWidget;
			impl_->pressed = BarSurfaceNoWidget;
			impl_->pointerCaptured = false;
			impl_->pointerLocal = {};
			impl_->hoverSuppressed = false;
			impl_->hoverSuppressionPoint = {};
			impl_->IncludeFullDamageLocked();
		}
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
		return true;
	}

	bool BarSurfaceScene::ConfigureHorizontalTwoTwoGroup(
		const BarSurfaceBackgroundSpec& background,
		std::span<const BarSurfaceWidgetSpec> widgets)
	{
		if (widgets.size() != 3) return false;
		BarSurfaceBackgroundSpec groupBackground = background;
		const double left = groupBackground.bounds.left;
		const double top = groupBackground.bounds.top;
		groupBackground.bounds = BarSurfaceDipRect{
			left, top,
			left + kDefaultTwoTwoGroupWidthDip,
			top + kDefaultTwoTwoGroupHeightDip };
		std::vector<BarSurfaceWidgetSpec> group(widgets.begin(), widgets.end());
		for (std::size_t index = 0; index < group.size(); ++index)
		{
			if (group[index].id == BarSurfaceNoWidget) return false;
			const double x = left + BarButtonGapDip
				+ static_cast<double>(index)
					* (BarButtonTwoSideDip + BarButtonGapDip);
			group[index].bounds = BarSurfaceDipRect{
				x, top + BarButtonGapDip,
				 x + BarButtonTwoSideDip,
				top + BarButtonGapDip + BarButtonTwoSideDip };
		}
		if (!Configure(groupBackground, group)) return false;
		const LONG width = static_cast<LONG>(std::lround(
			kDefaultTwoTwoGroupWidthDip));
		const LONG height = static_cast<LONG>(std::lround(
			kDefaultTwoTwoGroupHeightDip));
		return SetBounds(RECT{ 0, 0, width, height }, 1.0F);
	}

	bool BarSurfaceScene::ConfigureHorizontalGroup(
		const BarSurfaceBackgroundSpec& background,
		std::span<const BarSurfaceWidgetSpec> widgets,
		const BarSurfaceHorizontalGroupSpec& group)
	{
		if (widgets.empty()) return false;
		const auto resolved = ResolveBarSurfaceHorizontalGroupLayout(
			group, widgets.size());
		if (resolved.logicalBounds.right <= resolved.logicalBounds.left
			|| resolved.logicalBounds.bottom <= resolved.logicalBounds.top)
			return false;

		BarSurfaceBackgroundSpec configuredBackground = background;
		configuredBackground.bounds = BarSurfaceDipRect{
			0.0, 0.0, resolved.widthDip, resolved.heightDip };
		std::vector<BarSurfaceWidgetSpec> configured(
			widgets.begin(), widgets.end());
		for (std::size_t index = 0; index < configured.size(); ++index)
		{
			if (configured[index].id == BarSurfaceNoWidget) return false;
			configured[index].bounds = resolved.widgets[index];
		}
		if (!Configure(configuredBackground, configured)) return false;
		return SetBounds(resolved.logicalBounds, resolved.dpiScale);
	}

	bool BarSurfaceScene::SetWidgets(std::span<const BarSurfaceWidgetSpec> widgets)
	{
		std::vector<BarSurfaceWidgetSpec> copy(widgets.begin(), widgets.end());
		BarSurfaceHooks hooks;
		bool changed = false;
		{
		std::lock_guard lock(impl_->mutex);
		for (auto& spec : copy)
		{
			if (spec.id == BarSurfaceNoWidget) continue;
			Impl::Widget normalized;
			normalized.spec = spec;
			impl_->ApplySharedButtonMetricsLocked(normalized);
			spec = std::move(normalized.spec);
			Impl::Widget* existing = impl_->FindWidgetLocked(spec.id);
			if (!existing)
			{
				Impl::Widget next;
				next.spec = std::move(spec);
				impl_->InitializeButtonLocked(next);
				impl_->widgets.emplace_back(std::move(next));
				impl_->IncludeDamageLocked(impl_->widgets.back().lastPixels);
				changed = true;
				continue;
			}
			const RECT oldPixels = existing->lastPixels;
			const bool visualSpec = existing->spec.kind == spec.kind
				&& existing->spec.layoutKind == spec.layoutKind
				&& existing->spec.visible == spec.visible
				&& existing->spec.enabled == spec.enabled
				&& existing->spec.selected == spec.selected
				&& existing->spec.bounds.left == spec.bounds.left
				&& existing->spec.bounds.top == spec.bounds.top
				&& existing->spec.bounds.right == spec.bounds.right
				&& existing->spec.bounds.bottom == spec.bounds.bottom
				&& existing->spec.iconResource == spec.iconResource
				&& existing->spec.primaryText == spec.primaryText
				&& existing->spec.secondaryText == spec.secondaryText
				&& existing->spec.textUpdateMode == spec.textUpdateMode
				&& existing->spec.iconAngle == spec.iconAngle
				&& existing->spec.iconSizeDip == spec.iconSizeDip
				&& existing->spec.iconOffsetXDip == spec.iconOffsetXDip
				&& existing->spec.iconOffsetYDip == spec.iconOffsetYDip
				&& existing->spec.primaryFontSizeDip == spec.primaryFontSizeDip
				&& existing->spec.primaryOffsetXDip == spec.primaryOffsetXDip
				&& existing->spec.primaryOffsetYDip == spec.primaryOffsetYDip
				&& existing->spec.primarySlotWidthDip == spec.primarySlotWidthDip
				&& existing->spec.primarySlotHeightDip == spec.primarySlotHeightDip
				&& existing->spec.secondaryFontSizeDip == spec.secondaryFontSizeDip
				&& existing->spec.secondaryOffsetXDip == spec.secondaryOffsetXDip
				&& existing->spec.secondaryOffsetYDip == spec.secondaryOffsetYDip
				&& existing->spec.secondarySlotWidthDip == spec.secondarySlotWidthDip
				&& existing->spec.secondarySlotHeightDip == spec.secondarySlotHeightDip
				&& existing->spec.dragOpacity == spec.dragOpacity
				&& existing->spec.fill == spec.fill
				&& existing->spec.content == spec.content
				&& existing->spec.useThemeColors == spec.useThemeColors;
			if (visualSpec && existing->spec.interactive == spec.interactive)
				continue;
			if (visualSpec)
			{
				// interactive 只更新输入状态，避免重建按钮并重置内容动画。
				impl_->IncludeDamageLocked(existing->lastPixels);
				existing->spec.interactive = spec.interactive;
				if (!spec.interactive)
				{
					if (impl_->hovered == existing->spec.id)
						impl_->hovered = BarSurfaceNoWidget;
					(void)StopBarButtonHoverVisual(
						*existing->button, true);
					if (impl_->pressed == existing->spec.id)
					{
						SetBarButtonPressedVisual(*existing->button, false);
						impl_->pressed = BarSurfaceNoWidget;
						impl_->pointerCaptured = false;
					}
				}
				impl_->ApplyButtonInteractionTargetsLocked(*existing);
				impl_->IncludeDamageLocked(existing->lastPixels);
				changed = true;
				continue;
			}
			impl_->IncludeDamageLocked(oldPixels);
			existing->spec = std::move(spec);
			impl_->InitializeButtonLocked(*existing);
			impl_->IncludeDamageLocked(existing->lastPixels);
			changed = true;
		}
		hooks = impl_->hooks;
		}
		if (changed)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return true;
	}

	bool BarSurfaceScene::TransitionLayout(
		const BarSurfaceBackgroundSpec& background,
		std::span<const BarSurfaceWidgetSpec> widgets,
		double durationMilliseconds)
	{
		std::vector<BarSurfaceWidgetSpec> copy(widgets.begin(), widgets.end());
		if (!std::isfinite(durationMilliseconds) || durationMilliseconds < 0.0)
			durationMilliseconds = BarUiDefaultOperationDur;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->IncludeFullDamageLocked();
			impl_->background = background;
			const double backgroundWidth = (std::max)(0.0,
				background.bounds.Width());
			const double backgroundHeight = (std::max)(0.0,
				background.bounds.Height());
			impl_->backgroundShape.x.SetTar(
				background.bounds.left + backgroundWidth / 2.0,
				durationMilliseconds);
			impl_->backgroundShape.y.SetTar(
				background.bounds.top + backgroundHeight / 2.0,
				durationMilliseconds);
			impl_->backgroundShape.w.SetTar(backgroundWidth, durationMilliseconds);
			impl_->backgroundShape.h.SetTar(backgroundHeight, durationMilliseconds);
			impl_->backgroundShape.rw->SetTar(background.cornerRadiusDip,
				durationMilliseconds);
			impl_->backgroundShape.rh->SetTar(background.cornerRadiusDip,
				durationMilliseconds);
			impl_->backgroundShape.pct.SetTar((std::clamp)(
				background.fillOpacity, 0.0, 1.0), durationMilliseconds);
			impl_->backgroundShape.framePct->SetTar((std::clamp)(
				background.frameOpacity, 0.0, 1.0), durationMilliseconds);
			impl_->backgroundShape.frameLightPct->SetTar((std::clamp)(
				background.frameOpacity, 0.0, 1.0), durationMilliseconds);
			impl_->backgroundShape.enable.val = background.visible;
			impl_->backgroundShape.enable.tar = background.visible;

			for (auto& spec : copy)
			{
				if (spec.id == BarSurfaceNoWidget) continue;
				auto* widget = impl_->FindWidgetLocked(spec.id);
				if (!widget)
				{
					Impl::Widget next;
					next.spec = std::move(spec);
					impl_->InitializeButtonLocked(next);
					impl_->widgets.emplace_back(std::move(next));
					continue;
				}

				const RECT oldPixels = widget->lastPixels;
				const bool iconChanged = widget->spec.iconResource
					!= spec.iconResource;
				const bool primaryChanged = widget->spec.primaryText
					!= spec.primaryText;
				const bool secondaryChanged = widget->spec.secondaryText
					!= spec.secondaryText;
				widget->spec = std::move(spec);
				impl_->ApplySharedButtonMetricsLocked(*widget);
				const double width = (std::max)(0.0,
					widget->spec.bounds.Width());
				const double height = (std::max)(0.0,
					widget->spec.bounds.Height());
				const double centerX = widget->spec.bounds.left + width / 2.0;
				const double centerY = widget->spec.bounds.top + height / 2.0;
				widget->button->button.x.SetTar(centerX, durationMilliseconds);
				widget->button->button.y.SetTar(centerY, durationMilliseconds);
				widget->button->button.w.SetTar(width, durationMilliseconds);
				widget->button->button.h.SetTar(height, durationMilliseconds);
				widget->button->button.enable.val = widget->spec.visible;
				widget->button->button.enable.tar = widget->spec.visible;
				widget->button->userVisible = widget->spec.visible;
				widget->button->hide = !widget->spec.visible;
				widget->button->clickFunc = widget->spec.onClick;
				widget->button->name.x.SetTar(widget->spec.primaryOffsetXDip,
					durationMilliseconds);
				widget->button->name.y.SetTar(widget->spec.primaryOffsetYDip,
					durationMilliseconds);
				widget->button->name.w.SetTar(
					widget->spec.primarySlotWidthDip > 0.0
						? widget->spec.primarySlotWidthDip : width,
					durationMilliseconds);
				widget->button->name.h.SetTar(widget->spec.primarySlotHeightDip,
					durationMilliseconds);
				widget->button->name.size.SetTar(widget->spec.primaryFontSizeDip,
					durationMilliseconds);
				widget->secondary.x.SetTar(widget->spec.secondaryOffsetXDip,
					durationMilliseconds);
				widget->secondary.y.SetTar(widget->spec.secondaryOffsetYDip,
					durationMilliseconds);
				widget->secondary.w.SetTar(
					widget->spec.secondarySlotWidthDip > 0.0
						? widget->spec.secondarySlotWidthDip : width,
					durationMilliseconds);
				widget->secondary.h.SetTar(widget->spec.secondarySlotHeightDip,
					durationMilliseconds);
				widget->secondary.size.SetTar(widget->spec.secondaryFontSizeDip,
					durationMilliseconds);
				widget->button->icon.x.SetTar(widget->spec.iconOffsetXDip,
					durationMilliseconds);
				widget->button->icon.y.SetTar(widget->spec.iconOffsetYDip,
					durationMilliseconds);
				widget->button->icon.w.SetTar(widget->spec.iconSizeDip,
					durationMilliseconds);
				widget->button->icon.h.SetTar(widget->spec.iconSizeDip,
					durationMilliseconds);
				if (iconChanged && !widget->spec.iconResource.empty())
				{
					(void)widget->button->icon.TransitionToResource(
						L"UI", widget->spec.iconResource,
						durationMilliseconds);
					widget->button->icon.enable.val = true;
					widget->button->icon.enable.tar = true;
				}
				if (widget->spec.iconAngle.has_value())
					widget->button->icon.angle.SetTar(
						*widget->spec.iconAngle, durationMilliseconds);
				if (primaryChanged)
				{
					if (widget->spec.textUpdateMode
						== BarSurfaceTextUpdateMode::Immediate)
						(void)widget->button->name.SetStringImmediate(
							widget->spec.primaryText);
					else
						(void)widget->button->name.TransitionToString(
							widget->spec.primaryText, durationMilliseconds);
				}
				if (secondaryChanged)
				{
					if (widget->spec.textUpdateMode
						== BarSurfaceTextUpdateMode::Immediate)
						(void)widget->secondary.SetStringImmediate(
							widget->spec.secondaryText);
					else
						(void)widget->secondary.TransitionToString(
							widget->spec.secondaryText, durationMilliseconds);
				}
				const bool keepPrimaryForExit = ShouldKeepBarContentVisibleForExit(
					widget->spec.textUpdateMode == BarSurfaceTextUpdateMode::Animated,
					primaryChanged, widget->spec.primaryText.empty(),
					widget->button->name.enable.val);
				widget->button->name.enable.val = keepPrimaryForExit
					|| !widget->spec.primaryText.empty();
				widget->button->name.enable.tar =
					widget->button->name.enable.val;
				const bool keepSecondaryForExit = ShouldKeepBarContentVisibleForExit(
					widget->spec.textUpdateMode == BarSurfaceTextUpdateMode::Animated,
					secondaryChanged, widget->spec.secondaryText.empty(),
					widget->secondary.enable.val);
				widget->hasSecondary = keepSecondaryForExit
					|| !widget->spec.secondaryText.empty();
				widget->secondary.enable.val = widget->hasSecondary;
				widget->secondary.enable.tar = widget->hasSecondary;
				if (!iconChanged || !widget->spec.iconResource.empty())
				{
					widget->button->icon.enable.val =
						!widget->spec.iconResource.empty();
					widget->button->icon.enable.tar =
						!widget->spec.iconResource.empty();
				}
				if (!widget->spec.visible || !widget->spec.interactive)
				{
					(void)StopBarButtonHoverVisual(
						*widget->button, true);
					SetBarButtonPressedVisual(*widget->button, false);
					if (impl_->hovered == widget->spec.id)
						impl_->hovered = BarSurfaceNoWidget;
					if (impl_->pressed == widget->spec.id)
					{
						impl_->pressed = BarSurfaceNoWidget;
						impl_->pointerCaptured = false;
					}
				}
				if (widget->spec.kind == BarSurfaceWidgetKind::DragHandle)
					widget->dragHandle.pct.SetTar(widget->spec.dragOpacity,
						durationMilliseconds);
				impl_->SyncDragHandleGeometryLocked(*widget);
				impl_->IncludeDamageLocked(oldPixels);
			}
			impl_->animationActive = true;
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
		return true;
	}

	bool BarSurfaceScene::SetWidgetState(BarSurfaceWidgetId id, bool visible,
		bool enabled, std::wstring primaryText, std::wstring secondaryText,
		std::optional<std::wstring> iconResource,
		std::optional<double> iconAngle)
	{
		BarSurfaceHooks hooks;
		bool changed = false;
		{
			std::lock_guard lock(impl_->mutex);
			auto* widget = impl_->FindWidgetLocked(id);
			if (!widget) return false;
			const bool primaryTextChanged = widget->spec.primaryText != primaryText;
			const bool secondaryTextChanged = widget->spec.secondaryText != secondaryText;
			const bool iconChanged = iconResource.has_value()
				&& widget->spec.iconResource != *iconResource;
			const bool angleChanged = iconAngle.has_value()
				&& (!widget->spec.iconAngle.has_value()
					|| *widget->spec.iconAngle != *iconAngle);
			changed = widget->spec.visible != visible
				|| widget->spec.enabled != enabled
				|| primaryTextChanged
				|| secondaryTextChanged
				|| iconChanged || angleChanged;
			if (!changed) return true;
			impl_->IncludeDamageLocked(widget->lastPixels);
			widget->spec.visible = visible;
			widget->spec.enabled = enabled;
			widget->spec.primaryText = std::move(primaryText);
			widget->spec.secondaryText = std::move(secondaryText);
			impl_->ApplySharedButtonMetricsLocked(*widget);
			const double buttonWidth = widget->spec.bounds.Width();
			const double primaryWidth = widget->spec.primarySlotWidthDip > 0.0
				? widget->spec.primarySlotWidthDip : buttonWidth;
			const double secondaryWidth = widget->spec.secondarySlotWidthDip > 0.0
				? widget->spec.secondarySlotWidthDip : buttonWidth;
			if (widget->spec.textUpdateMode
				== BarSurfaceTextUpdateMode::Immediate)
			{
				widget->button->name.x.SetDirect(widget->spec.primaryOffsetXDip);
				widget->button->name.w.SetDirect(primaryWidth);
				widget->secondary.x.SetDirect(widget->spec.secondaryOffsetXDip);
				widget->secondary.w.SetDirect(secondaryWidth);
			}
			else
			{
				widget->button->name.x.SetTar(widget->spec.primaryOffsetXDip,
					BarUiDefaultOperationDur);
				widget->button->name.w.SetTar(primaryWidth,
					BarUiDefaultOperationDur);
				widget->secondary.x.SetTar(widget->spec.secondaryOffsetXDip,
					BarUiDefaultOperationDur);
				widget->secondary.w.SetTar(secondaryWidth,
					BarUiDefaultOperationDur);
			}
			if (iconChanged)
			{
				widget->spec.iconResource = std::move(*iconResource);
				if (!widget->spec.iconResource.empty())
				{
					// SVG 与文字在同一时间轴中淡出、替换并回弹，保持主栏内容切换节奏。
					(void)widget->button->icon.TransitionToResource(
						L"UI", widget->spec.iconResource);
					widget->button->icon.enable.val = true;
					widget->button->icon.enable.tar = true;
				}
			}
			if (angleChanged)
			{
				widget->spec.iconAngle = *iconAngle;
				widget->button->icon.angle.SetTar(*iconAngle,
					BarUiDefaultOperationDur);
			}
			if (primaryTextChanged)
			{
				if (widget->spec.textUpdateMode
					== BarSurfaceTextUpdateMode::Immediate)
					(void)widget->button->name.SetStringImmediate(
						widget->spec.primaryText);
				else
					(void)widget->button->name.TransitionToString(
						widget->spec.primaryText);
			}
			const bool keepPrimaryForExit = ShouldKeepBarContentVisibleForExit(
				widget->spec.textUpdateMode == BarSurfaceTextUpdateMode::Animated,
				primaryTextChanged, widget->spec.primaryText.empty(),
				widget->button->name.enable.val);
			widget->button->name.enable.val = keepPrimaryForExit
				|| !widget->spec.primaryText.empty();
			widget->button->name.enable.tar = widget->button->name.enable.val;
			widget->button->userVisible = visible;
			widget->button->hide = !visible;
			widget->button->button.enable.val = visible;
			widget->button->button.enable.tar = visible;
			const bool keepSecondaryForExit = ShouldKeepBarContentVisibleForExit(
				widget->spec.textUpdateMode == BarSurfaceTextUpdateMode::Animated,
				secondaryTextChanged, widget->spec.secondaryText.empty(),
				widget->secondary.enable.val);
			widget->hasSecondary = keepSecondaryForExit
				|| !widget->spec.secondaryText.empty();
			if (widget->hasSecondary)
			{
				if (secondaryTextChanged)
				{
					if (widget->spec.textUpdateMode
						== BarSurfaceTextUpdateMode::Immediate)
						(void)widget->secondary.SetStringImmediate(
							widget->spec.secondaryText);
					else
						(void)widget->secondary.TransitionToString(
							widget->spec.secondaryText);
				}
				widget->secondary.enable.val = true;
				widget->secondary.enable.tar = true;
			}
			else
			{
				widget->secondary.enable.val = false;
				widget->secondary.enable.tar = false;
			}
			if (!visible && impl_->hovered == id)
			{
				impl_->hovered = BarSurfaceNoWidget;
				(void)StopBarButtonHoverVisual(*widget->button, true);
			}
			if (!visible && impl_->pressed == id)
			{
				SetBarButtonPressedVisual(*widget->button, false);
				impl_->pressed = BarSurfaceNoWidget;
				impl_->pointerCaptured = false;
			}
			impl_->IncludeDamageLocked(widget->lastPixels);
			hooks = impl_->hooks;
		}
		if (changed)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return true;
	}

	bool BarSurfaceScene::SetWidgetInteractive(
		BarSurfaceWidgetId id, bool interactive)
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			auto* widget = impl_->FindWidgetLocked(id);
			if (!widget) return false;
			if (widget->spec.interactive == interactive) return true;
			impl_->IncludeDamageLocked(widget->lastPixels);
			widget->spec.interactive = interactive;
			if (!interactive)
			{
				if (impl_->hovered == id)
					impl_->hovered = BarSurfaceNoWidget;
				(void)StopBarButtonHoverVisual(*widget->button, true);
				if (impl_->pressed == id)
				{
					SetBarButtonPressedVisual(*widget->button, false);
					impl_->pressed = BarSurfaceNoWidget;
					impl_->pointerCaptured = false;
				}
			}
			// 仅更新按压/悬停反馈；enabled 内容目标保持不变。
			impl_->ApplyButtonInteractionTargetsLocked(*widget);
			impl_->IncludeDamageLocked(widget->lastPixels);
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
		return true;
	}

	bool BarSurfaceScene::SetWidgetSelected(
		BarSurfaceWidgetId id, bool selected)
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			auto* widget = impl_->FindWidgetLocked(id);
			if (!widget) return false;
			if (widget->spec.selected == selected) return true;
			impl_->IncludeDamageLocked(widget->lastPixels);
			widget->spec.selected = selected;
			impl_->ApplyButtonTargetsLocked(*widget);
			impl_->IncludeDamageLocked(widget->lastPixels);
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
		return true;
	}

	bool BarSurfaceScene::SetBounds(RECT logicalBounds, float dpiScale) noexcept
	{
		logicalBounds = Normalize(logicalBounds);
		if (logicalBounds.right <= logicalBounds.left
			|| logicalBounds.bottom <= logicalBounds.top) return false;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			const bool changed = !EqualRect(&impl_->logicalBounds, &logicalBounds)
				|| impl_->dpiScale != NormalizeScale(dpiScale);
			impl_->logicalBounds = logicalBounds;
			impl_->dpiScale = NormalizeScale(dpiScale);
			impl_->appliedSharedLightingOutset = -1;
			for (auto& widget : impl_->widgets)
			{
				impl_->IncludeDamageLocked(widget.lastPixels);
				widget.lastPixels = impl_->WidgetPixelsLocked(widget);
			}
			if (changed) impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
		return true;
	}

	void BarSurfaceScene::SetBackground(const BarSurfaceBackgroundSpec& background)
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->background = background;
			impl_->InitializeBackgroundLocked();
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	void BarSurfaceScene::SetOpacity(
		double opacity, double durationMilliseconds) noexcept
	{
		if (!std::isfinite(opacity)) opacity = 1.0;
		if (!std::isfinite(durationMilliseconds) || durationMilliseconds < 0.0)
			durationMilliseconds = BarUiDefaultOperationDur;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->surfaceOpacity.SetTar((std::clamp)(opacity, 0.0, 1.0),
				durationMilliseconds);
			impl_->animationActive = true;
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	void BarSurfaceScene::SetDamageOutsetDip(double outsetDip) noexcept
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->damageOutsetDip = std::isfinite(outsetDip)
				? (std::max)(0.0, outsetDip) : kDefaultDamageOutsetDip;
			impl_->appliedSharedLightingOutset = -1;
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	void BarSurfaceScene::SetHooks(BarSurfaceHooks hooks)
	{
		std::lock_guard lock(impl_->mutex);
		impl_->hooks = std::move(hooks);
	}

	void BarSurfaceScene::SetSharedLightingSubscribed(bool subscribed) noexcept
	{
		BarSurfaceHooks hooks;
		{
			std::scoped_lock registryLock(sharedLightingRegistryMutex);
			std::lock_guard lock(impl_->mutex);
			impl_->sharedLightingSubscribed = subscribed;
			impl_->appliedSharedLightingGeneration = 0;
			impl_->appliedSharedLightingOutset = -1;
			if (subscribed)
				impl_->ApplySharedLightingLocked(
					sharedLighting, sharedLightingGeneration);
			else
			{
				impl_->rendererOwner.spec.SetFrameLightingSnapshot({});
				impl_->appliedSharedLighting = {};
				impl_->cursorLightDamageBounds = {};
			}
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	void BarSurfaceScene::PublishSharedLighting(
		const BarSurfaceSharedLighting& lighting) noexcept
	{
		std::vector<BarSurfaceHooks> hooksToNotify;
		{
			std::scoped_lock registryLock(sharedLightingRegistryMutex);
			if (SameSharedLighting(sharedLighting, lighting)) return;
			sharedLighting = lighting;
			++sharedLightingGeneration;
			for (auto* scene : sharedLightingScenes)
			{
				if (!scene || !scene->impl_) continue;
				std::lock_guard lock(scene->impl_->mutex);
				if (!scene->impl_->sharedLightingSubscribed) continue;
				scene->impl_->appliedSharedLightingGeneration = 0;
				scene->impl_->ApplySharedLightingLocked(
					sharedLighting, sharedLightingGeneration);
				hooksToNotify.push_back(scene->impl_->hooks);
			}
		}
		for (const auto& hooks : hooksToNotify)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
	}

	void BarSurfaceScene::Reset() noexcept
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->hovered = BarSurfaceNoWidget;
			impl_->pressed = BarSurfaceNoWidget;
			impl_->pointerCaptured = false;
			impl_->pointerLocal = {};
			impl_->rendererOwner.spec.SetFrameLightingSnapshot({});
			impl_->cursorLightDamageBounds = {};
			impl_->lastFrame = {};
			impl_->animationActive = false;
			impl_->pendingDamage = {};
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	void BarSurfaceScene::ReleaseDeviceResources() noexcept
	{
		std::lock_guard lock(impl_->mutex);
		impl_->rendererOwner.spec.DiscardDeviceResources();
	}

	void BarSurfaceScene::Invalidate() noexcept
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	bool BarSurfaceScene::IsInvalidated() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->invalidated;
	}

	RECT BarSurfaceScene::PendingDamage() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->pendingDamage;
	}

	RECT BarSurfaceScene::ConsumeDamage() noexcept
	{
		std::lock_guard lock(impl_->mutex);
		const RECT result = impl_->pendingDamage;
		impl_->pendingDamage = {};
		impl_->invalidated = false;
		return result;
	}

	BarSurfaceLayout BarSurfaceScene::Layout() const
	{
		std::lock_guard lock(impl_->mutex);
		BarSurfaceLayout result;
		result.logicalBounds = impl_->logicalBounds;
		result.presentationBounds = impl_->PresentationBoundsLocked();
		result.surfacePixels = impl_->logicalBounds;
		result.dpiScale = impl_->dpiScale;
		result.widgets.reserve(impl_->widgets.size());
		for (const auto& widget : impl_->widgets)
			result.widgets.push_back(BarSurfaceWidgetLayout{
				widget.spec.id, widget.spec.kind, widget.lastPixels,
				widget.spec.visible, widget.spec.enabled,
				widget.spec.interactive,
				widget.spec.selected });
		return result;
	}

	RECT BarSurfaceScene::LogicalBounds() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->logicalBounds;
	}

	RECT BarSurfaceScene::PresentationBounds() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->PresentationBoundsLocked();
	}

	LONG BarSurfaceScene::PresentationOutsetPixels() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->DamageOutsetPixels();
	}

	HRESULT BarSurfaceScene::EnsureDeviceResources(
		const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch,
		UINT width, UINT height)
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->rendererOwner.spec.EnsureDeviceResources(epoch, width, height);
	}

	ID2D1DeviceContext* BarSurfaceScene::DeviceContext() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->rendererOwner.spec.GetDeviceContext();
	}

	ID2D1GdiInteropRenderTarget* BarSurfaceScene::GdiInteropRenderTarget() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->rendererOwner.spec.GetGdiInteropRenderTarget();
	}

	void BarSurfaceScene::HandleFrameEndDrawResult(HRESULT result)
	{
		std::lock_guard lock(impl_->mutex);
		impl_->rendererOwner.spec.HandleFrameEndDrawResult(result);
	}

	std::optional<POINT> BarSurfaceScene::PresentationToLogical(
		POINT presentationLocalPixels) const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->LogicalPointFromPresentation(presentationLocalPixels);
	}

	bool BarSurfaceScene::HitTestBackground(POINT localPixels) const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->HitTestBackgroundLocked(localPixels);
	}

	BarSurfaceWidgetId BarSurfaceScene::HitTest(POINT localPixels) const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->HitTestLocked(localPixels);
	}

	BarSurfaceWidgetId BarSurfaceScene::HitTestPresentation(
		POINT presentationLocalPixels) const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		const auto logical = impl_->LogicalPointFromPresentation(
			presentationLocalPixels);
		return logical.has_value()
			? impl_->HitTestLocked(*logical) : BarSurfaceNoWidget;
	}

	BarSurfacePointerResult BarSurfaceScene::PointerMove(POINT localPixels) noexcept
	{
		BarSurfacePointerResult result;
		BarSurfaceHooks hooks;
		bool requestRender = false;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->pointerLocal = localPixels;
			if (impl_->hoverSuppressed
				&& (impl_->hoverSuppressionPoint.x != localPixels.x
					|| impl_->hoverSuppressionPoint.y != localPixels.y))
				impl_->hoverSuppressed = false;
			const auto next = impl_->pointerCaptured || impl_->hoverSuppressed
				? BarSurfaceNoWidget : impl_->HitTestLocked(localPixels);
			result.hoverChanged = next != impl_->hovered;
			if (result.hoverChanged)
			{
				if (auto* old = impl_->FindWidgetLocked(impl_->hovered))
				{
					if (old->spec.kind == BarSurfaceWidgetKind::Button)
					{
						(void)StopBarButtonHoverVisual(*old->button, false);
						impl_->ApplyButtonInteractionTargetsLocked(*old);
						impl_->IncludeDamageLocked(old->lastPixels);
						requestRender = true;
					}
				}
				if (auto* current = impl_->FindWidgetLocked(next))
				{
					if (current->spec.kind == BarSurfaceWidgetKind::Button)
					{
						(void)StartBarButtonHoverVisual(*current->button);
						impl_->ApplyButtonInteractionTargetsLocked(*current);
						impl_->IncludeDamageLocked(current->lastPixels);
						requestRender = true;
					}
				}
				impl_->hovered = next;
			}
			result.hover = impl_->hovered;
			result.pressed = impl_->pressed;
			result.consumed = result.hover != BarSurfaceNoWidget;
			hooks = impl_->hooks;
		}
		if (requestRender)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return result;
	}

	BarSurfacePointerResult BarSurfaceScene::PointerLeave() noexcept
	{
		BarSurfacePointerResult result;
		BarSurfaceHooks hooks;
		bool requestRender = false;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->hoverSuppressed = false;
			result.hoverChanged = impl_->hovered != BarSurfaceNoWidget;
			if (auto* old = impl_->FindWidgetLocked(impl_->hovered))
			{
				if (old->spec.kind == BarSurfaceWidgetKind::Button)
				{
					(void)StopBarButtonHoverVisual(*old->button, false);
					impl_->ApplyButtonInteractionTargetsLocked(*old);
					impl_->IncludeDamageLocked(old->lastPixels);
					requestRender = true;
				}
			}
			impl_->hovered = BarSurfaceNoWidget;
			result.hover = BarSurfaceNoWidget;
			result.pressed = impl_->pressed;
			hooks = impl_->hooks;
		}
		if (requestRender)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return result;
	}

	BarSurfacePointerResult BarSurfaceScene::PointerDown(POINT localPixels) noexcept
	{
		BarSurfacePointerResult result;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->pointerLocal = localPixels;
			const auto target = impl_->HitTestLocked(localPixels);
			if (target == BarSurfaceNoWidget) return result;
			if (impl_->pressed != target)
			{
				if (auto* old = impl_->FindWidgetLocked(impl_->pressed))
				{
					SetBarButtonPressedVisual(*old->button, false);
					impl_->ApplyButtonInteractionTargetsLocked(*old);
					impl_->IncludeDamageLocked(old->lastPixels);
				}
				if (auto* current = impl_->FindWidgetLocked(target))
				{
					if (current->spec.kind == BarSurfaceWidgetKind::Button)
					{
						(void)StopBarButtonHoverVisual(
							*current->button, true, true);
					}
					impl_->ApplyButtonInteractionTargetsLocked(*current);
					impl_->IncludeDamageLocked(current->lastPixels);
				}
				impl_->pressed = target;
				impl_->hovered = BarSurfaceNoWidget;
				result.pressedChanged = true;
			}
			impl_->pointerCaptured = true;
			result.hover = impl_->hovered;
			result.pressed = impl_->pressed;
			result.consumed = true;
			hooks = impl_->hooks;
		}
		if (result.pressedChanged)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return result;
	}

	BarSurfacePointerResult BarSurfaceScene::PointerUp(
		POINT localPixels, bool invokeCallback) noexcept
	{
		BarSurfacePointerResult result;
		std::function<void()> callback;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->pointerLocal = localPixels;
			const auto target = impl_->HitTestLocked(localPixels);
			const auto down = impl_->pressed;
			if (auto* old = impl_->FindWidgetLocked(down))
			{
				SetBarButtonPressedVisual(*old->button, false);
				impl_->ApplyButtonInteractionTargetsLocked(*old);
				impl_->IncludeDamageLocked(old->lastPixels);
			}
			impl_->pressed = BarSurfaceNoWidget;
			impl_->pointerCaptured = false;
			if (down != BarSurfaceNoWidget)
			{
				impl_->hoverSuppressed = true;
				impl_->hoverSuppressionPoint = localPixels;
			}
			result.pressedChanged = down != BarSurfaceNoWidget;
			if (down != BarSurfaceNoWidget && down == target)
			{
				if (const auto* widget = impl_->FindWidgetLocked(down))
				{
					if (widget->spec.kind == BarSurfaceWidgetKind::Button)
					{
						callback = widget->spec.onClick;
						result.clicked = down;
					}
				}
			}
			result.hover = impl_->hovered;
			result.pressed = BarSurfaceNoWidget;
			result.consumed = down != BarSurfaceNoWidget || target != BarSurfaceNoWidget;
			hooks = impl_->hooks;
		}
		if (result.pressedChanged || result.clicked != BarSurfaceNoWidget)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		if (invokeCallback && callback) callback();
		return result;
	}

	BarSurfacePointerResult BarSurfaceScene::CancelPointer() noexcept
	{
		BarSurfacePointerResult result;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			result.hoverChanged = impl_->hovered != BarSurfaceNoWidget;
			if (auto* hovered = impl_->FindWidgetLocked(impl_->hovered))
			{
				(void)StopBarButtonHoverVisual(*hovered->button, true);
				impl_->ApplyButtonInteractionTargetsLocked(*hovered);
				impl_->IncludeDamageLocked(hovered->lastPixels);
			}
			if (auto* old = impl_->FindWidgetLocked(impl_->pressed))
			{
				SetBarButtonPressedVisual(*old->button, false);
				impl_->ApplyButtonInteractionTargetsLocked(*old);
				impl_->IncludeDamageLocked(old->lastPixels);
			}
			result.pressedChanged = impl_->pressed != BarSurfaceNoWidget;
			impl_->hovered = BarSurfaceNoWidget;
			impl_->pressed = BarSurfaceNoWidget;
			impl_->pointerCaptured = false;
			impl_->hoverSuppressed = true;
			impl_->hoverSuppressionPoint = impl_->pointerLocal;
			result.hover = impl_->hovered;
			hooks = impl_->hooks;
		}
		if (result.pressedChanged || result.hoverChanged)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return result;
	}

	bool BarSurfaceScene::Advance(
		std::chrono::steady_clock::time_point frameTime) noexcept
	{
		BarSurfaceHooks hooks;
		bool active = false;
		{
			std::lock_guard lock(impl_->mutex);
			active = impl_->AdvanceAnimationsLocked(frameTime);
			hooks = impl_->hooks;
		}
		if (active)
		{
			if (hooks.invalidate) hooks.invalidate();
			if (hooks.wake) hooks.wake();
		}
		return active;
	}

	bool BarSurfaceScene::AnimationActive() const noexcept
	{
		std::lock_guard lock(impl_->mutex);
		return impl_->animationActive;
	}

	BarSurfaceRenderResult BarSurfaceScene::Render(ID2D1DeviceContext* deviceContext,
		std::chrono::steady_clock::time_point frameTime)
	{
		BarSurfaceRenderResult result;
		if (!deviceContext) return result;
		BarSurfaceHooks hooks;
		BarSurfaceSharedLighting frameLighting;
		std::uint64_t frameLightingGeneration = 0;
		{
			std::lock_guard registryLock(sharedLightingRegistryMutex);
			frameLighting = sharedLighting;
			frameLightingGeneration = sharedLightingGeneration;
		}
		{
			std::lock_guard lock(impl_->mutex);
			if (!impl_->rendererOwner.barMedia.formatCache)
				impl_->rendererOwner.barMedia.LoadFormat();
			if (impl_->sharedLightingSubscribed)
				impl_->ApplySharedLightingLocked(
					frameLighting, frameLightingGeneration);
			impl_->rendererOwner.spec.SetFrameZoom(impl_->dpiScale);
			result.invalidated = impl_->invalidated;
			result.damage = impl_->pendingDamage;
			(void)impl_->AdvanceAnimationsLocked(frameTime);
			D2D1_MATRIX_3X2_F originalTransform{};
			deviceContext->GetTransform(&originalTransform);
			const LONG outset = impl_->DamageOutsetPixels();
			deviceContext->SetTransform(D2D1::Matrix3x2F::Translation(
				static_cast<FLOAT>(outset), static_cast<FLOAT>(outset)));
			const FLOAT surfaceOpacity = static_cast<FLOAT>((std::clamp)(
				static_cast<double>(impl_->surfaceOpacity.val), 0.0, 1.0));
			const bool opacityLayer = surfaceOpacity < 0.9999F;
			if (opacityLayer)
				deviceContext->PushLayer(D2D1::LayerParameters(
					D2D1::InfiniteRect(), nullptr,
					D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
					D2D1::IdentityMatrix(), surfaceOpacity), nullptr);
			if (impl_->background.visible)
			{
				result.rendered = DrawBarBackgroundVisual(
					impl_->rendererOwner.spec, deviceContext,
					impl_->backgroundShape,
					BarUiInheritClass(
						impl_->backgroundShape.x.val
							- impl_->backgroundShape.w.val / 2.0,
						impl_->backgroundShape.y.val
							- impl_->backgroundShape.h.val / 2.0)) || result.rendered;
			}
			for (auto& widget : impl_->widgets)
			{
				if (!widget.spec.visible) continue;
				if (widget.spec.kind == BarSurfaceWidgetKind::DragHandle)
				{
					result.rendered = impl_->rendererOwner.spec.Shape(deviceContext,
						widget.dragHandle,
						BarUiInheritClass(
							widget.dragHandle.x.val - widget.dragHandle.w.val / 2.0,
							widget.dragHandle.y.val - widget.dragHandle.h.val / 2.0))
						|| result.rendered;
					continue;
				}
				const auto inherit = BarUiInheritClass(
					widget.button->button.x.val - widget.button->button.w.val / 2.0,
					widget.button->button.y.val - widget.button->button.h.val / 2.0);
				BarButtonDrawOptions options;
				options.secondary = widget.hasSecondary
					? &widget.secondary : nullptr;
				result.rendered = DrawBarButtonVisual(
					impl_->rendererOwner.spec, deviceContext,
					*widget.button, inherit, options) || result.rendered;
			}
			if (opacityLayer) deviceContext->PopLayer();
			deviceContext->SetTransform(originalTransform);
			result.animationActive = impl_->animationActive;
			hooks = impl_->hooks;
		}
		if (result.invalidated && hooks.invalidate) hooks.invalidate();
		if (result.animationActive && hooks.wake) hooks.wake();
		return result;
	}
}
