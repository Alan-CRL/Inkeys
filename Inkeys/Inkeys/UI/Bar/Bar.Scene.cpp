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

		std::mutex sharedPrimaryLightRegistryMutex;
		std::vector<BarSurfaceScene*> sharedPrimaryLightScenes;
		BarSurfaceSharedPrimaryLight sharedPrimaryLight{};
		std::uint64_t sharedPrimaryLightGeneration = 0;

		[[nodiscard]] bool SameSharedPrimaryLight(
			const BarSurfaceSharedPrimaryLight& left,
			const BarSurfaceSharedPrimaryLight& right) noexcept
		{
			return std::abs(left.screenX - right.screenX) <= 0.01
				&& std::abs(left.screenY - right.screenY) <= 0.01
				&& std::abs(left.radiusPixels - right.radiusPixels) <= 0.01
				&& (left.drawingPenColor & 0x00FFFFFF)
					== (right.drawingPenColor & 0x00FFFFFF)
				&& std::abs(left.drawingPenColorBlend
					- right.drawingPenColorBlend) <= 0.000001
				&& std::abs(left.drawingLightOpacity
					- right.drawingLightOpacity) <= 0.000001
				&& left.visible == right.visible
				&& left.edgeLightingEnabled == right.edgeLightingEnabled;
		}

	}

	struct BarSurfaceScene::Impl
	{
		struct Widget
		{
			BarSurfaceWidgetSpec spec{};
			std::shared_ptr<BarButtonClass> button;
			BarUiWordClass secondary;
			bool hasSecondary = false;
			bool hover = false;
			bool pressed = false;
			RECT lastPixels{};
		};

		mutable std::mutex mutex;
		// 每个 surface 独立持有 Rendering 与字体缓存，避免共享主栏实例的跨帧状态。
		BarUISetClass rendererOwner;
		BarSurfaceBackgroundSpec background{};
		BarUiShapeClass backgroundShape;
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
		bool sharedPrimaryLightSubscribed = false;
		std::uint64_t appliedSharedPrimaryLightGeneration = 0;
		BarSurfaceSharedPrimaryLight appliedSharedPrimaryLight{};
		RECT appliedSharedPrimaryLightBounds{};
		LONG appliedSharedPrimaryLightOutset = -1;
		POINT pointerLocal{};
		bool pointerKnown = false;

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

		void ApplySharedPrimaryLightLocked(
			const BarSurfaceSharedPrimaryLight& light,
			std::uint64_t generation)
		{
			const LONG outset = DamageOutsetPixels();
			if (appliedSharedPrimaryLightGeneration == generation
				&& EqualRect(&appliedSharedPrimaryLightBounds, &logicalBounds)
				&& appliedSharedPrimaryLightOutset == outset)
				return;

			BarUiFrameLightingSnapshot snapshot;
			snapshot.primaryLight = D2D1::Point2F(
				static_cast<FLOAT>(light.screenX
					- static_cast<double>(logicalBounds.left) + outset),
				static_cast<FLOAT>(light.screenY
					- static_cast<double>(logicalBounds.top) + outset));
			snapshot.primaryRadius = static_cast<FLOAT>(
				std::isfinite(light.radiusPixels) && light.radiusPixels > 0.0
					? light.radiusPixels : 0.0);
			snapshot.drawingPenColor = light.drawingPenColor;
			snapshot.drawingPenColorBlend = light.drawingPenColorBlend;
			snapshot.drawingLightOpacity = light.drawingLightOpacity;
			snapshot.primaryLightVisible = light.visible;
			snapshot.edgeLightingEnabled = light.edgeLightingEnabled;
			rendererOwner.spec.SetFrameLightingSnapshot(snapshot);
			appliedSharedPrimaryLight = light;
			appliedSharedPrimaryLightGeneration = generation;
			appliedSharedPrimaryLightBounds = logicalBounds;
			appliedSharedPrimaryLightOutset = outset;
		}

		[[nodiscard]] bool CursorLightTargetLocked() const noexcept
		{
			if (!pointerKnown || hovered == BarSurfaceNoWidget) return false;
			const auto* widget = FindWidgetLocked(hovered);
			return widget && widget->spec.visible && widget->spec.enabled
				&& widget->spec.selected;
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
			backgroundShape.frameCursorLightIntensityScale =
				BarButtonCursorLightIntensity;
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
		}

		void InitializeButtonLocked(Widget& widget)
		{
			if (!widget.button) widget.button = std::make_shared<BarButtonClass>();
			const auto fill = ThemeOr(widget.spec.fill, widget.spec.useThemeColors,
				BarThemeColorEnum::PressedFill);
			const auto content = ThemeOr(widget.spec.content,
				widget.spec.useThemeColors, BarThemeColorEnum::TextPrimary);
			const double width = widget.spec.bounds.Width();
			const double height = widget.spec.bounds.Height();
			const double centerX = widget.spec.bounds.left + width / 2.0;
			const double centerY = widget.spec.bounds.top + height / 2.0;

			widget.button->size = BarButtonSizeEnum::twoTwo;
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
			widget.button->name.Initialization(0.0, primaryOffsetY, width,
				primarySlotHeight, widget.spec.primaryText,
				std::isfinite(widget.spec.primaryFontSizeDip)
					? widget.spec.primaryFontSizeDip : BarButtonTwoTwoIconSizeDip,
				content);
			widget.button->name.enable.Initialization(!widget.spec.primaryText.empty());
			widget.button->name.pct.SetDirect(1.0);

			widget.hasSecondary = !widget.spec.secondaryText.empty();
			if (widget.hasSecondary)
			{
				const double secondaryOffsetY = std::isfinite(
					widget.spec.secondaryOffsetYDip)
					? widget.spec.secondaryOffsetYDip
					: BarButtonTwoTwoLabelOffsetYDip;
				const double secondarySlotHeight = std::isfinite(
					widget.spec.secondarySlotHeightDip)
					&& widget.spec.secondarySlotHeightDip > 0.0
					? widget.spec.secondarySlotHeightDip
					: BarButtonTwoTwoLabelHeightDip;
				widget.secondary.Initialization(0.0, secondaryOffsetY, width,
					secondarySlotHeight, widget.spec.secondaryText,
					std::isfinite(widget.spec.secondaryFontSizeDip)
						? widget.spec.secondaryFontSizeDip
						: BarButtonTwoTwoLabelFontSizeDip, content);
				widget.secondary.enable.Initialization(true);
				widget.secondary.pct.SetDirect(1.0);
			}

			widget.button->icon.Initialization(0.0,
				widget.hasSecondary && std::isfinite(widget.spec.iconOffsetYDip)
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
		}

		void ApplyButtonTargetsLocked(Widget& widget)
		{
			const bool enabled = widget.spec.visible && widget.spec.enabled;
			const double opacity = !enabled ? 0.0
				: widget.pressed ? BarButtonPressedOpacity
				: widget.hover ? BarButtonHoverOpacity : 0.0;
			widget.button->button.pct.SetTar(opacity,
				BarButtonHoverTransitionDuration);
			if (widget.button->button.frameLightPct.has_value())
				widget.button->button.frameLightPct->SetTar(
					enabled && widget.spec.selected
						? BarButtonPressedLightOpacity
						: 0.0, BarButtonHoverTransitionDuration);
			widget.button->pressScale.SetTar(
				widget.pressed ? BarButtonPressScale : 1.0,
				static_cast<double>(BarUiDefaultOperationDur), std::nullopt,
				false, widget.pressed ? BarButtonPressCurve() : BarButtonReleaseCurve());
			// 禁用只关闭交互和背景反馈，内容仍以低透明度可读。
			const double contentOpacity = enabled ? 1.0
				: BarButtonDisabledContentOpacity;
			widget.button->icon.pct.SetTar(contentOpacity,
				BarButtonHoverTransitionDuration);
			widget.button->name.pct.SetTar(contentOpacity,
				BarButtonHoverTransitionDuration);
			if (widget.hasSecondary)
				widget.secondary.pct.SetTar(contentOpacity,
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
			const LONG outset = DamageOutsetPixels();
			const D2D1_POINT_2F cursor = D2D1::Point2F(
				static_cast<FLOAT>(pointerLocal.x + outset),
				static_cast<FLOAT>(pointerLocal.y + outset));
			if (rendererOwner.spec.PrepareSurfaceCursorLight(
				dt, cursor, CursorLightTargetLocked()))
				IncludeFullDamageLocked();
			const BarUiAnimationAdvanceContextClass context{
				dt, speed, static_cast<bool>(BarUiAnimationEnabled), false };
			bool active = false;
			for (auto& widget : widgets)
			{
				ApplyButtonTargetsLocked(widget);
				const auto include = [&](const auto& result)
				{
					active = active || result.active;
					if (result.changed) IncludeDamageLocked(widget.lastPixels);
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
				include(BarUiAdvanceAnimation(widget.button->icon.angle, context));
				include(BarUiAdvanceAnimation(widget.button->icon.pct, context));
				include(BarUiAdvanceAnimation(widget.button->name.pct, context));
				if (widget.hasSecondary)
					include(BarUiAdvanceAnimation(widget.secondary.pct, context));
			}
			animationActive = active;
			return active;
		}

		[[nodiscard]] BarSurfaceWidgetId HitTestLocked(POINT localPixels) const noexcept
		{
			const RECT logical = LocalSurfaceRect();
			if (!PtInRect(&logical, localPixels))
				return BarSurfaceNoWidget;
			const double scale = dpiScale > 0.0F ? dpiScale : 1.0F;
			const double xDip = static_cast<double>(localPixels.x) / scale;
			const double yDip = static_cast<double>(localPixels.y) / scale;
			for (auto it = widgets.rbegin(); it != widgets.rend(); ++it)
			{
				if (!it->spec.visible || !it->spec.enabled) continue;
				const auto& bounds = it->spec.bounds;
				if (xDip >= bounds.left && xDip < bounds.right
					&& yDip >= bounds.top && yDip < bounds.bottom)
					return it->spec.id;
			}
			return BarSurfaceNoWidget;
		}
	};

	BarSurfaceScene::BarSurfaceScene()
		: impl_(std::make_unique<Impl>())
	{
		std::lock_guard lock(sharedPrimaryLightRegistryMutex);
		sharedPrimaryLightScenes.push_back(this);
	}

	BarSurfaceScene::~BarSurfaceScene()
	{
		std::lock_guard lock(sharedPrimaryLightRegistryMutex);
		sharedPrimaryLightScenes.erase(
			std::remove(sharedPrimaryLightScenes.begin(),
				sharedPrimaryLightScenes.end(), this),
			sharedPrimaryLightScenes.end());
	}

	BarSurfaceScene::BarSurfaceScene(BarSurfaceScene&& other) noexcept
		: impl_(std::move(other.impl_))
	{
		std::lock_guard lock(sharedPrimaryLightRegistryMutex);
		for (auto& scene : sharedPrimaryLightScenes)
			if (scene == &other) scene = this;
	}

	BarSurfaceScene& BarSurfaceScene::operator=(BarSurfaceScene&& other) noexcept
	{
		if (this == &other) return *this;
		{
			std::lock_guard lock(sharedPrimaryLightRegistryMutex);
			sharedPrimaryLightScenes.erase(
				std::remove(sharedPrimaryLightScenes.begin(),
					sharedPrimaryLightScenes.end(), this),
				sharedPrimaryLightScenes.end());
			for (auto& scene : sharedPrimaryLightScenes)
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
			impl_->pointerKnown = false;
			impl_->pointerLocal = {};
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
			const bool oldSpec = existing->spec.visible == spec.visible
				&& existing->spec.enabled == spec.enabled
				&& existing->spec.selected == spec.selected
				&& existing->spec.bounds.left == spec.bounds.left
				&& existing->spec.bounds.top == spec.bounds.top
				&& existing->spec.bounds.right == spec.bounds.right
				&& existing->spec.bounds.bottom == spec.bounds.bottom
				&& existing->spec.iconResource == spec.iconResource
				&& existing->spec.primaryText == spec.primaryText
				&& existing->spec.secondaryText == spec.secondaryText;
			if (oldSpec) continue;
			impl_->IncludeDamageLocked(oldPixels);
			const bool hover = existing->hover;
			const bool pressed = existing->pressed;
			existing->spec = std::move(spec);
			existing->hover = hover && existing->spec.visible;
			existing->pressed = pressed && existing->spec.visible;
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
			const bool iconChanged = iconResource.has_value()
				&& widget->spec.iconResource != *iconResource;
			const bool angleChanged = iconAngle.has_value()
				&& (!widget->spec.iconAngle.has_value()
					|| *widget->spec.iconAngle != *iconAngle);
			changed = widget->spec.visible != visible
				|| widget->spec.enabled != enabled
				|| widget->spec.primaryText != primaryText
				|| widget->spec.secondaryText != secondaryText
				|| iconChanged || angleChanged;
			if (!changed) return true;
			impl_->IncludeDamageLocked(widget->lastPixels);
			widget->spec.visible = visible;
			widget->spec.enabled = enabled;
			widget->spec.primaryText = std::move(primaryText);
			widget->spec.secondaryText = std::move(secondaryText);
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
			(void)widget->button->name.TransitionToString(
				widget->spec.primaryText);
			widget->button->name.enable.val = !widget->spec.primaryText.empty();
			widget->button->name.enable.tar = !widget->spec.primaryText.empty();
			widget->button->userVisible = visible;
			widget->button->hide = !visible;
			widget->button->button.enable.val = visible;
			widget->button->button.enable.tar = visible;
			widget->hasSecondary = !widget->spec.secondaryText.empty();
			if (widget->hasSecondary)
			{
				(void)widget->secondary.TransitionToString(
					widget->spec.secondaryText);
				widget->secondary.enable.val = true;
				widget->secondary.enable.tar = true;
			}
			else
			{
				widget->secondary.enable.val = false;
				widget->secondary.enable.tar = false;
			}
			if (!visible && impl_->hovered == id) impl_->hovered = BarSurfaceNoWidget;
			if (!visible && impl_->pressed == id)
			{
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
			impl_->appliedSharedPrimaryLightOutset = -1;
			for (auto& widget : impl_->widgets)
			{
				impl_->IncludeDamageLocked(widget.lastPixels);
				widget.lastPixels = DipToPixels(widget.spec.bounds, impl_->dpiScale);
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

	void BarSurfaceScene::SetDamageOutsetDip(double outsetDip) noexcept
	{
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->damageOutsetDip = std::isfinite(outsetDip)
				? (std::max)(0.0, outsetDip) : kDefaultDamageOutsetDip;
			impl_->appliedSharedPrimaryLightOutset = -1;
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

	void BarSurfaceScene::SetSharedPrimaryLightSubscribed(bool subscribed) noexcept
	{
		BarSurfaceHooks hooks;
		{
			std::scoped_lock registryLock(sharedPrimaryLightRegistryMutex);
			std::lock_guard lock(impl_->mutex);
			impl_->sharedPrimaryLightSubscribed = subscribed;
			impl_->appliedSharedPrimaryLightGeneration = 0;
			impl_->appliedSharedPrimaryLightOutset = -1;
			if (subscribed)
				impl_->ApplySharedPrimaryLightLocked(
					sharedPrimaryLight, sharedPrimaryLightGeneration);
			else
			{
				impl_->rendererOwner.spec.SetFrameLightingSnapshot({});
				impl_->appliedSharedPrimaryLight = {};
			}
			impl_->IncludeFullDamageLocked();
			hooks = impl_->hooks;
		}
		if (hooks.invalidate) hooks.invalidate();
		if (hooks.wake) hooks.wake();
	}

	void BarSurfaceScene::PublishSharedPrimaryLight(
		const BarSurfaceSharedPrimaryLight& light) noexcept
	{
		std::vector<BarSurfaceHooks> hooksToNotify;
		{
			std::scoped_lock registryLock(sharedPrimaryLightRegistryMutex);
			if (SameSharedPrimaryLight(sharedPrimaryLight, light)) return;
			sharedPrimaryLight = light;
			++sharedPrimaryLightGeneration;
			for (auto* scene : sharedPrimaryLightScenes)
			{
				if (!scene || !scene->impl_) continue;
				std::lock_guard lock(scene->impl_->mutex);
				if (!scene->impl_->sharedPrimaryLightSubscribed) continue;
				scene->impl_->appliedSharedPrimaryLightGeneration = 0;
				scene->impl_->ApplySharedPrimaryLightLocked(
					sharedPrimaryLight, sharedPrimaryLightGeneration);
				scene->impl_->IncludeFullDamageLocked();
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
			impl_->pointerKnown = false;
			impl_->pointerLocal = {};
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
				widget.spec.id, widget.lastPixels,
				widget.spec.visible, widget.spec.enabled,
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
		{
			std::lock_guard lock(impl_->mutex);
			impl_->pointerLocal = localPixels;
			impl_->pointerKnown = true;
			const auto next = impl_->HitTestLocked(localPixels);
			result.hoverChanged = next != impl_->hovered;
			if (result.hoverChanged)
			{
				if (auto* old = impl_->FindWidgetLocked(impl_->hovered))
				{
					old->hover = false;
					impl_->IncludeDamageLocked(old->lastPixels);
				}
				if (auto* current = impl_->FindWidgetLocked(next))
				{
					current->hover = true;
					impl_->IncludeDamageLocked(current->lastPixels);
				}
				impl_->hovered = next;
			}
			result.hover = impl_->hovered;
			result.pressed = impl_->pressed;
			result.consumed = result.hover != BarSurfaceNoWidget;
			hooks = impl_->hooks;
		}
		if (result.hoverChanged)
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
		{
			std::lock_guard lock(impl_->mutex);
			impl_->pointerKnown = false;
			result.hoverChanged = impl_->hovered != BarSurfaceNoWidget;
			if (auto* old = impl_->FindWidgetLocked(impl_->hovered))
			{
				old->hover = false;
				impl_->IncludeDamageLocked(old->lastPixels);
			}
			impl_->hovered = BarSurfaceNoWidget;
			result.hover = BarSurfaceNoWidget;
			result.pressed = impl_->pressed;
			hooks = impl_->hooks;
		}
		if (result.hoverChanged)
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
			impl_->pointerKnown = true;
			const auto target = impl_->HitTestLocked(localPixels);
			if (target == BarSurfaceNoWidget) return result;
			if (impl_->pressed != target)
			{
				if (auto* old = impl_->FindWidgetLocked(impl_->pressed))
				{
					old->pressed = false;
					impl_->IncludeDamageLocked(old->lastPixels);
				}
				if (auto* current = impl_->FindWidgetLocked(target))
				{
					current->pressed = true;
					impl_->IncludeDamageLocked(current->lastPixels);
				}
				impl_->pressed = target;
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

	BarSurfacePointerResult BarSurfaceScene::PointerUp(POINT localPixels) noexcept
	{
		BarSurfacePointerResult result;
		std::function<void()> callback;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			impl_->pointerLocal = localPixels;
			impl_->pointerKnown = true;
			const auto target = impl_->HitTestLocked(localPixels);
			const auto down = impl_->pressed;
			if (auto* old = impl_->FindWidgetLocked(down))
			{
				old->pressed = false;
				impl_->IncludeDamageLocked(old->lastPixels);
			}
			impl_->pressed = BarSurfaceNoWidget;
			impl_->pointerCaptured = false;
			result.pressedChanged = down != BarSurfaceNoWidget;
			if (down != BarSurfaceNoWidget && down == target)
			{
				if (const auto* widget = impl_->FindWidgetLocked(down))
					callback = widget->spec.onClick;
				result.clicked = down;
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
		if (callback) callback();
		return result;
	}

	BarSurfacePointerResult BarSurfaceScene::CancelPointer() noexcept
	{
		BarSurfacePointerResult result;
		BarSurfaceHooks hooks;
		{
			std::lock_guard lock(impl_->mutex);
			if (auto* old = impl_->FindWidgetLocked(impl_->pressed))
			{
				old->pressed = false;
				impl_->IncludeDamageLocked(old->lastPixels);
			}
			result.pressedChanged = impl_->pressed != BarSurfaceNoWidget;
			impl_->pressed = BarSurfaceNoWidget;
			impl_->pointerCaptured = false;
			impl_->pointerKnown = false;
			result.hover = impl_->hovered;
			hooks = impl_->hooks;
		}
		if (result.pressedChanged)
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
		BarSurfaceSharedPrimaryLight sharedLight;
		std::uint64_t sharedLightGeneration = 0;
		{
			std::lock_guard registryLock(sharedPrimaryLightRegistryMutex);
			sharedLight = sharedPrimaryLight;
			sharedLightGeneration = sharedPrimaryLightGeneration;
		}
		{
			std::lock_guard lock(impl_->mutex);
			if (!impl_->rendererOwner.barMedia.formatCache)
				impl_->rendererOwner.barMedia.LoadFormat();
			if (impl_->sharedPrimaryLightSubscribed)
				impl_->ApplySharedPrimaryLightLocked(
					sharedLight, sharedLightGeneration);
			impl_->rendererOwner.spec.SetFrameZoom(impl_->dpiScale);
			result.invalidated = impl_->invalidated;
			result.damage = impl_->pendingDamage;
			(void)impl_->AdvanceAnimationsLocked(frameTime);
			const auto savedZoom = impl_->dpiScale;
			D2D1_MATRIX_3X2_F originalTransform{};
			deviceContext->GetTransform(&originalTransform);
			const LONG outset = impl_->DamageOutsetPixels();
			deviceContext->SetTransform(D2D1::Matrix3x2F::Translation(
				static_cast<FLOAT>(outset), static_cast<FLOAT>(outset)));
			if (impl_->background.visible)
			{
				result.rendered = impl_->rendererOwner.spec.Shape(deviceContext,
					impl_->backgroundShape,
					BarUiInheritClass(impl_->backgroundShape.inhX,
						impl_->backgroundShape.inhY)) || result.rendered;
			}
			for (auto& widget : impl_->widgets)
			{
				if (!widget.spec.visible) continue;
				const auto inherit = widget.button->button.Inherit();
				D2D1_MATRIX_3X2_F buttonTransform{};
				deviceContext->GetTransform(&buttonTransform);
				double pressScale = widget.button->pressScale.val;
				if (!std::isfinite(pressScale) || pressScale <= 0.0) pressScale = 1.0;
				if (std::abs(pressScale - 1.0) > 0.000001)
				{
					const FLOAT cx = static_cast<FLOAT>(
						(inherit.x + widget.button->button.w.val / 2.0) * savedZoom);
					const FLOAT cy = static_cast<FLOAT>(
						(inherit.y + widget.button->button.h.val / 2.0) * savedZoom);
					deviceContext->SetTransform(D2D1::Matrix3x2F::Scale(
						static_cast<FLOAT>(pressScale), static_cast<FLOAT>(pressScale),
						D2D1::Point2F(cx, cy)) * buttonTransform);
				}
				result.rendered = impl_->rendererOwner.spec.Shape(deviceContext,
					widget.button->button, inherit) || result.rendered;
				if (!widget.spec.iconResource.empty())
					result.rendered = impl_->rendererOwner.spec.Svg(deviceContext,
						widget.button->icon,
						widget.button->icon.Inherit(BarUiInheritEnum::Center,
							widget.button->button)) || result.rendered;
				if (!widget.spec.primaryText.empty())
					result.rendered = impl_->rendererOwner.spec.Word(deviceContext,
						widget.button->name,
						widget.button->name.Inherit(BarUiInheritEnum::Center,
							widget.button->button), DWRITE_FONT_WEIGHT_BOLD) || result.rendered;
				if (widget.hasSecondary)
					result.rendered = impl_->rendererOwner.spec.Word(deviceContext,
						widget.secondary,
						widget.secondary.Inherit(BarUiInheritEnum::Center,
							widget.button->button), DWRITE_FONT_WEIGHT_NORMAL) || result.rendered;
				deviceContext->SetTransform(buttonTransform);
			}
			deviceContext->SetTransform(originalTransform);
			result.animationActive = impl_->animationActive;
			hooks = impl_->hooks;
		}
		if (result.invalidated && hooks.invalidate) hooks.invalidate();
		if (result.animationActive && hooks.wake) hooks.wake();
		return result;
	}
}
