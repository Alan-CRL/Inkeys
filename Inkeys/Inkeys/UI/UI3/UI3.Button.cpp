module;

#include <windows.h>
#include "../../../IdtAtomic.h"
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite_1.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

module Inkeys.UI.UI3;
import :Button;
import :Primitive;
import :Renderer;
import :Envelope;
import :Theme;
import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Metrics;
import Inkeys.UI.UI3.Material;

using namespace std;

namespace Inkeys::UI::UI3
{
	bool StartButtonHoverVisual(UiPctClass* hoverPct, UiColorClass* hoverFill,
		IdtAtomic<ButtonHoverStageEnum>* hoverStage)
	{
		if (!hoverPct || !hoverFill || !hoverStage) return false;
		hoverPct->animateWhenDisabled = true;
		hoverFill->animateWhenDisabled = true;
		const UiCurveSpecClass curve{
			UiCurveEnum::EaseOutSine,
			UiCurveEnum::EaseOutSine, 0.0, false };
		hoverFill->SetTar(GetThemeColor(ThemeColorEnum::PressedFill),
			ButtonHoverTransitionDuration, curve);
		hoverPct->SetTar(ButtonHoverOpacity,
			ButtonHoverTransitionDuration, std::nullopt, true, curve);
		*hoverStage = ButtonHoverStageEnum::Showing;
		return true;
	}

	bool StopButtonHoverVisual(UiPctClass* hoverPct, UiColorClass* hoverFill,
		IdtAtomic<ButtonHoverStageEnum>* hoverStage, bool immediate, bool preserveVisual)
	{
		if (!hoverPct || !hoverStage) return false;
		if (immediate)
		{
			*hoverStage = ButtonHoverStageEnum::None;
			// 按下时保留当前视觉值交给按压态续接，隐藏等场景仍立即清零。
			if (!preserveVisual) hoverPct->SetDirect(0.0);
			hoverPct->animateWhenDisabled = false;
			if (hoverFill) hoverFill->animateWhenDisabled = false;
		}
		else
		{
			// 离开后保持背景颜色，透明度自然降为零后再结束悬停。
			hoverPct->animateWhenDisabled = true;
			if (hoverFill) hoverFill->animateWhenDisabled = true;
			*hoverStage = ButtonHoverStageEnum::Fading;
			const UiCurveSpecClass curve{
				UiCurveEnum::EaseOutSine,
				UiCurveEnum::EaseOutSine, 0.0, false };
			hoverPct->SetTar(0.0,
				ButtonHoverTransitionDuration,
				std::nullopt, true, curve);
		}
		return true;
	}

	bool StartButtonHoverVisual(ButtonClass& button, bool separator) noexcept
	{
		if (separator
			|| !button.button.fill.has_value()) return false;
		// 属性面板的 Shape 与完整按钮共用悬停动画，按钮仅保留自身门控。
		return StartButtonHoverVisual(&button.button.pct,
			&button.button.fill.value(), &button.hoverStage);
	}

	bool StopButtonHoverVisual(ButtonClass& button, bool immediate,
		bool preserveVisual, bool separator) noexcept
	{
		if (separator)
		{
			button.hoverStage = ButtonHoverStageEnum::None;
			button.state->emph = WidgetEmphasize::None;
			button.pressScale.SetDirect(1.0);
			button.button.pct.animateWhenDisabled = false;
			if (button.button.fill.has_value())
				button.button.fill->animateWhenDisabled = false;
			return true;
		}
		if (!button.button.fill.has_value()) return false;
		return StopButtonHoverVisual(&button.button.pct,
			&button.button.fill.value(), &button.hoverStage, immediate, preserveVisual);
	}

	bool UpdateButtonHoverVisual(UiPctClass& pct, UiColorClass* fill,
		IdtAtomic<ButtonHoverStageEnum>& stage, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept
	{
		auto Finish = [&]() noexcept
			{
				stage = ButtonHoverStageEnum::None;
				pct.animateWhenDisabled = false;
				if (fill != nullptr)
					fill->animateWhenDisabled = false;
			};
		if (!visible)
		{
			if (stage != ButtonHoverStageEnum::None)
				pct.SetDirect(0.0);
			Finish();
			return true;
		}
		if (!hoverAllowed)
		{
			Finish();
			return true;
		}
		if (stage == ButtonHoverStageEnum::Showing
			&& pct.IsSame())
		{
			stage = ButtonHoverStageEnum::Fading;
			const UiCurveSpecClass curve{
				UiCurveEnum::EaseInSine,
				UiCurveEnum::EaseInSine, 0.0, false };
			pct.SetTar(0.0, fadeDurationSeconds,
				std::nullopt, true, curve);
			return true;
		}
		if (stage == ButtonHoverStageEnum::Fading
			&& pct.IsSame())
		{
			Finish();
			return true;
		}
		return false;
	}

	bool UpdateButtonHoverVisual(ButtonClass& button, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept
	{
		return UpdateButtonHoverVisual(button.button.pct,
			button.button.fill ? &*button.button.fill : nullptr,
			button.hoverStage, visible, hoverAllowed, fadeDurationSeconds);
	}

	void SetButtonPressedVisual(ButtonClass& button, bool pressed) noexcept
	{
		button.state->emph = pressed
			? WidgetEmphasize::Pressed : WidgetEmphasize::None;
	}

	void RetargetFixedButtonVisual(bool dark, const ButtonVisualParts& parts,
		bool visible, bool enabled, bool selected, bool pressed, double duration,
		std::optional<COLORREF> contentOverride, bool iconVisible,
		std::optional<COLORREF> fillOverride, std::optional<COLORREF> frameOverride) noexcept
	{
		auto& shape=parts.background;
		const COLORREF frameColor=frameOverride.value_or(GetThemeColor(selected
			?ThemeColorEnum::Accent:ThemeColorEnum::TextPrimary));
		if(!shape.fill)shape.fill.emplace(GetThemeColor(ThemeColorEnum::PressedFill));
		if(!shape.frame)shape.frame.emplace(frameColor);
		if(!shape.framePct)shape.framePct.emplace(0.0);
		if(!shape.frameLightPct)shape.frameLightPct.emplace(0.0);
		if(!shape.ft)shape.ft.emplace(ButtonFrameThicknessDip);
		shape.frameRendering=UiFrameRenderingEnum::PointLight;
		shape.framePrimaryLightEnabled=false;
		shape.frameCursorLightIntensityScale=ButtonCursorLightIntensity;
		shape.frameLightCursorLightIntensityScale=ButtonLightCursorLightIntensity;
		shape.frame->SetTar(frameColor);
		// 主题选中底色在浅色端为不透明浅青色；显式配色的客户端保持原透明度。
		const double lightWeight=dark?0.0:1.0;
		if(!visible)shape.pct.SetTar(0.0,duration);
		else if(pressed)shape.pct.SetTar(selected&&!fillOverride
			?ThemeMaterial::SelectedFillOpacity(lightWeight,ButtonPressedOpacity)
			:ButtonPressedOpacity,duration);
		else if(selected)shape.pct.SetTar(fillOverride?0.20
			:ThemeMaterial::SelectedFillOpacity(lightWeight),duration);
		else if(!parts.hoverStage || *parts.hoverStage==ButtonHoverStageEnum::None)
			shape.pct.SetTar(0.0,duration);
		shape.fill->SetTar(fillOverride.value_or(GetThemeColor(selected
			?ThemeColorEnum::SelectedFill:ThemeColorEnum::PressedFill)));
		shape.frameLightPct->SetTar(visible && enabled && selected
			?(pressed?ButtonPressedLightOpacity:1.0):0.0,duration);
		if(parts.pressScale)parts.pressScale->SetTar(pressed?ButtonPressScale:1.0,
			static_cast<double>(UiDefaultOperationDur),std::nullopt,false,
			pressed?ButtonPressCurve():ButtonReleaseCurve());
		const double opacity=visible?(enabled?1.0:ThemeMaterial::DisabledContentOpacity(lightWeight)):0.0;
		const COLORREF content=contentOverride.value_or(GetThemeColor(selected
			?ThemeColorEnum::Accent:ThemeColorEnum::TextPrimary));
		const COLORREF iconContent=contentOverride.value_or(GetThemeColor(selected
			?ThemeColorEnum::Accent:ThemeColorEnum::IconPrimary));
		if(parts.icon)
		{
			parts.icon->pct.SetTar(iconVisible?opacity:0.0,ButtonHoverTransitionDuration);
			if(parts.icon->color1)parts.icon->color1->SetTar(iconContent);
		}
		if(parts.primary)
		{
			parts.primary->pct.SetTar(opacity,ButtonHoverTransitionDuration);
			parts.primary->color.SetTar(content);
		}
	}

	void RetargetButtonInteractionVisual(bool dark, ButtonClass& button,
		bool visible,bool enabled,bool selected,double duration,
		std::optional<COLORREF> content,bool iconVisible,
		std::optional<COLORREF> fill,std::optional<COLORREF> frame) noexcept
	{
		RetargetFixedButtonVisual(dark,{button.button,&button.icon,&button.name,
			&button.pressScale,&button.hoverStage},visible,enabled,selected,
			button.state->emph==WidgetEmphasize::Pressed,duration,content,iconVisible,fill,frame);
		if(button.icon.color2)button.icon.color2->SetTar(content.value_or(GetThemeColor(selected
			?ThemeColorEnum::Accent:ThemeColorEnum::IconPrimary)));
	}

	ButtonVisualInheritance PrepareButtonVisualInheritance(
		ButtonClass& button, const UiInheritClass& inherit,
		UiWordClass* secondary) noexcept
	{
		const UiInheritClass buttonInherit = button.button.UpInh(inherit);
		const auto iconPoint = ResolveButtonChildTopLeft(
			buttonInherit.x, buttonInherit.y,
			button.button.w.val, button.button.h.val,
			button.icon.x.val, button.icon.y.val,
			button.icon.w.val, button.icon.h.val);
		const UiInheritClass iconInherit = button.icon.UpInh(
			UiInheritClass(iconPoint.x, iconPoint.y));
		const auto primaryPoint = ResolveButtonChildTopLeft(
			buttonInherit.x, buttonInherit.y,
			button.button.w.val, button.button.h.val,
			button.name.x.val, button.name.y.val,
			button.name.w.val, button.name.h.val);
		const UiInheritClass primaryInherit = button.name.UpInh(
			UiInheritClass(primaryPoint.x, primaryPoint.y));
		std::optional<UiInheritClass> secondaryInherit;
		if (secondary)
		{
			const auto secondaryPoint = ResolveButtonChildTopLeft(
				buttonInherit.x, buttonInherit.y,
				button.button.w.val, button.button.h.val,
				secondary->x.val, secondary->y.val,
				secondary->w.val, secondary->h.val);
			secondaryInherit = secondary->UpInh(
				UiInheritClass(secondaryPoint.x, secondaryPoint.y));
		}
		return { buttonInherit, iconInherit, primaryInherit,
			secondaryInherit };
	}

	namespace
	{
		std::optional<D2D1_RECT_F> ComputeButtonVisualBounds(UIRendering& renderer,
			const ButtonVisualParts& parts, const ButtonVisualInheritance& inherit,
			const UiWordClass* secondary = nullptr)
		{
			const double zoom = renderer.GetFrameZoom();
			D2D1_RECT_F bounds{}; bool first = true, valid = true;
			const auto Include = [&](double left, double top, double right, double bottom, double padding)
			{
				const auto next = D2D1::RectF(static_cast<FLOAT>(left - padding), static_cast<FLOAT>(top - padding),
					static_cast<FLOAT>(right + padding), static_cast<FLOAT>(bottom + padding));
				if (!std::isfinite(next.left) || !std::isfinite(next.top) || !std::isfinite(next.right)
					|| !std::isfinite(next.bottom)) { valid = false; return; }
				if (next.left >= next.right || next.top >= next.bottom) return;
				if (first) { bounds = next; first = false; return; }
				bounds.left = (std::min)(bounds.left, next.left); bounds.top = (std::min)(bounds.top, next.top);
				bounds.right = (std::max)(bounds.right, next.right); bounds.bottom = (std::max)(bounds.bottom, next.bottom);
			};
			Include(inherit.button.x * zoom, inherit.button.y * zoom,
				(inherit.button.x + parts.background.w.val) * zoom,
				(inherit.button.y + parts.background.h.val) * zoom, renderer.GetFrameShapeDrawOutset(parts.background));
			if (parts.icon)
			{
				const auto& icon = *parts.icon;
				const double x = inherit.icon.x, y = inherit.icon.y, width = icon.w.val, height = icon.h.val;
				const double angle = icon.angle.val, scale = icon.contentScale;
				// 沿用现有旋转图像 dirty 规则；非有限/极端元数据不进入整数包络计算。
				constexpr double limit = 10000000.0;
				if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)
					|| !std::isfinite(angle) || !std::isfinite(scale) || scale <= 0
					|| std::abs(x * zoom) + std::abs(width * zoom * (std::max)(1.0, scale)) > limit
					|| std::abs(y * zoom) + std::abs(height * zoom * (std::max)(1.0, scale)) > limit) return std::nullopt;
				if (width > 0 && height > 0)
				{
					const auto image = RenderingAttribute::GetRotatedImageRect(x, y, width, height,
						std::fmod(angle, 360.0), zoom, scale);
					Include(image.left, image.top, image.right, image.bottom, 0);
				}
			}
			const auto IncludeWord = [&](const UiWordClass* word, const UiInheritClass& origin)
			{
				if (!word) return;
				const double scale = word->contentScale;
				if (!std::isfinite(scale)) { valid = false; return; }
				const double width = word->w.val, height = word->h.val;
				const double paddedWidth = width * (std::max)(1.0, scale), paddedHeight = height * (std::max)(1.0, scale);
				Include((origin.x + (width - paddedWidth) / 2) * zoom,
					(origin.y + (height - paddedHeight) / 2) * zoom,
					(origin.x + (width + paddedWidth) / 2) * zoom,
					(origin.y + (height + paddedHeight) / 2) * zoom, RenderingAttribute::dirtyAntialiasPadding);
			};
			IncludeWord(parts.primary, inherit.primary);
			if (secondary && inherit.secondary) IncludeWord(secondary, *inherit.secondary);
			return valid && !first ? std::optional<D2D1_RECT_F>(bounds) : std::nullopt;
		}

		class ButtonDrawTransform
		{
		public:
			ButtonDrawTransform(UIRendering& renderer, ID2D1DeviceContext* context,
				const UiShapeClass& background, const UiInheritClass& inherit, double scale, double targetScale, double duration)
				: fontScope_(renderer, targetScale, duration), context_(context)
			{
				context_->GetTransform(&original_);
				if(!std::isfinite(scale) || scale<=0.0)scale=1.0;
				changed_=std::abs(scale-1.0)>0.000001;
				if(!changed_)return;
				const FLOAT zoom=static_cast<FLOAT>(renderer.GetFrameZoom());
				context_->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<FLOAT>(scale),
					static_cast<FLOAT>(scale),D2D1::Point2F(
						static_cast<FLOAT>((inherit.x+background.w.val/2.0)*zoom),
						static_cast<FLOAT>((inherit.y+background.h.val/2.0)*zoom)))*original_);
			}
			~ButtonDrawTransform(){if(changed_)context_->SetTransform(original_);}
		private:
			UIRendering::FontTargetScope fontScope_;
			ID2D1DeviceContext* context_;
			D2D1_MATRIX_3X2_F original_{};
			bool changed_=false;
		};
	}

	void InitializeButtonBadge(ButtonBadgeClass& badge, const wchar_t* resource)
	{
		const COLORREF color = GetThemeColor(ThemeColorEnum::IconPrimary);
		badge.svg.Initialization(0, 0, color, color);
		badge.svg.InitializationFromResource(L"UI", resource);
		badge.svg.enable.Initialization(true);
		badge.svg.pct.SetDirect(1);
	}

	ButtonBadgeGeometry ResolveButtonBadgeGeometry(ButtonClass& button,
		const UiInheritClass& buttonInherit) noexcept
	{
		if (!button.badge) return {};
		auto& badge = *button.badge;
		double unit = badge.scale;
		if (!std::isfinite(unit) || unit <= 0.0) unit = 1.0;
		const double diameter = ButtonBadgeDiameterDip * unit;
		// 1×1 按钮太小，内收的角标会压住图标：角标中心落在按钮角点上（可略微越出按钮）；
		// 2×1 及以上保持内收 4DIP。中间尺寸按最大边长线性过渡，尺寸动画中不跳变。
		const double side = (std::max)(static_cast<double>(button.button.w.val), static_cast<double>(button.button.h.val)) / unit;
		const double t = std::clamp((side - ButtonOneSideDip) / (ButtonTwoSideDip - ButtonOneSideDip), 0.0, 1.0);
		const double inset = (-ButtonBadgeDiameterDip / 2 + t * (ButtonBadgeInsetDip + ButtonBadgeDiameterDip / 2)) * unit;
		const double below = std::clamp(static_cast<double>(badge.below.val), 0.0, 1.0);
		const double progress = (std::max)(0.0, static_cast<double>(badge.progress.val));
		ButtonBadgeGeometry result;
		result.diameter = diameter;
		result.centerX = buttonInherit.x + button.button.w.val - inset - diameter / 2;
		// 倒转时沿按钮右边在右上与右下之间插值，按钮尺寸动画只平移角标。
		result.centerY = buttonInherit.y + inset + diameter / 2
			+ below * (static_cast<double>(button.button.h.val) - 2 * inset - diameter);
		result.scale = progress;
		result.angle = badge.angle.val;
		return result;
	}

	RECT ResolveButtonBadgeBounds(ButtonClass& button,
		const UiInheritClass& buttonInherit, double zoom) noexcept
	{
		const auto g = ResolveButtonBadgeGeometry(button, buttonInherit);
		if (g.scale <= 0.000001 || g.diameter <= 0 || !std::isfinite(zoom) || zoom <= 0
			|| !std::isfinite(g.centerX) || !std::isfinite(g.centerY)) return {};
		// 旋转方块取外接圆，回弹超出 1 的缩放也包含在内。
		const double radius = g.diameter * g.scale * 0.70710678118654752;
		const LONG padding = RenderingAttribute::dirtyAntialiasPadding;
		return { static_cast<LONG>(std::floor((g.centerX - radius) * zoom)) - padding,
			static_cast<LONG>(std::floor((g.centerY - radius) * zoom)) - padding,
			static_cast<LONG>(std::ceil((g.centerX + radius) * zoom)) + padding,
			static_cast<LONG>(std::ceil((g.centerY + radius) * zoom)) + padding };
	}

	namespace
	{
		bool DrawButtonBadge(UIRendering& renderer, ID2D1DeviceContext* context,
			ButtonClass& button, const UiInheritClass& buttonInherit)
		{
			if (!button.badge) return false;
			const auto g = ResolveButtonBadgeGeometry(button, buttonInherit);
			if (g.scale <= 0.000001 || g.diameter <= 0) return false;
			auto& svg = button.badge->svg;
			// 角标跟随按钮图标的颜色与透明度，自身只负责显隐的缩放和旋转。
			svg.w.SetDirect(g.diameter); svg.h.SetDirect(g.diameter);
			svg.angle.SetDirect(g.angle); svg.contentScale = g.scale;
			svg.pct.SetDirect(button.icon.pct.val);
			if (button.icon.color1 && svg.color1) svg.color1->SetDirect(button.icon.color1->val);
			if (button.icon.color1 && svg.color2) svg.color2->SetDirect(button.icon.color1->val);
			return renderer.Svg(context, svg, svg.UpInh({ g.centerX - g.diameter / 2, g.centerY - g.diameter / 2 }));
		}
	}

	std::optional<D2D1_RECT_F> ResolveButtonVisualBounds(UIRendering& renderer, ButtonClass& button,
		const ButtonVisualInheritance& inheritance, const ButtonDrawOptions& options)
	{
		if ((options.underlay || options.content) && !options.contentExtent) return std::nullopt;
		auto bounds = ComputeButtonVisualBounds(renderer, {button.button,
			options.drawIcon ? &button.icon : nullptr, &button.name}, inheritance, options.secondary);
		if (!bounds) return std::nullopt;
		const auto Include = [&](const D2D1_RECT_F& add)
		{
			if (add.left >= add.right || add.top >= add.bottom) return;
			bounds->left = (std::min)(bounds->left, add.left); bounds->top = (std::min)(bounds->top, add.top);
			bounds->right = (std::max)(bounds->right, add.right); bounds->bottom = (std::max)(bounds->bottom, add.bottom);
		};
		if (button.badge)
		{
			const RECT badge = ResolveButtonBadgeBounds(button, inheritance.button, renderer.GetFrameZoom());
			Include({static_cast<FLOAT>(badge.left), static_cast<FLOAT>(badge.top),
				static_cast<FLOAT>(badge.right), static_cast<FLOAT>(badge.bottom)});
		}
		if (options.contentExtent)
		{
			const auto& local = options.contentExtent->boundsDip;
			const double zoom = renderer.GetFrameZoom();
			const D2D1_RECT_F content{static_cast<FLOAT>((inheritance.button.x + local.left) * zoom),
				static_cast<FLOAT>((inheritance.button.y + local.top) * zoom),
				static_cast<FLOAT>((inheritance.button.x + local.right) * zoom),
				static_cast<FLOAT>((inheritance.button.y + local.bottom) * zoom)};
			if (!std::isfinite(content.left) || !std::isfinite(content.top)
				|| !std::isfinite(content.right) || !std::isfinite(content.bottom)) return std::nullopt;
			Include(content);
		}
		return bounds;
	}

	bool DrawButtonVisual(UIRendering& renderer,
		ID2D1DeviceContext* deviceContext, ButtonClass& button,
		const UiInheritClass& inherit,
		const ButtonDrawOptions& options, bool separator)
	{
		if (!deviceContext) return false;
		const auto visualInheritance = PrepareButtonVisualInheritance(
			button, inherit, options.secondary);
		ButtonDrawTransform transform(renderer,deviceContext,button.button,
			visualInheritance.button,button.pressScale.val,button.pressScale.tar,button.pressScale.dur);
		// 先更新继承坐标再拒绝绘制，成功呈现后的命中快照仍可读取本帧几何。
		// 未声明扩展范围的回调仍保守绘制；声明后才启用联合包络拒绘。
		if ((!options.underlay && !options.content) || options.contentExtent)
		{
			const auto bounds = ResolveButtonVisualBounds(renderer, button, visualInheritance, options);
			if (bounds && !renderer.ShouldDrawFrameBounds(deviceContext, *bounds)) return false;
		}

		bool rendered = options.underlay
			? options.underlay(renderer, deviceContext, button, visualInheritance) : false;
		rendered = renderer.Shape(
			deviceContext, button.button, visualInheritance.button) || rendered;
		if (!separator)
		{
			if (options.drawIcon && button.iconKind == ButtonIconKindEnum::Png)
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
			else if (options.drawIcon)
				rendered = renderer.Svg(deviceContext, button.icon,
					visualInheritance.icon) || rendered;
			if (options.content)
				rendered = options.content(renderer, deviceContext, button,
					visualInheritance) || rendered;
			rendered = renderer.Word(deviceContext, button.name,
				visualInheritance.primary,
				options.primaryWeight, options.primaryAlignment, true) || rendered;
			if (options.secondary && visualInheritance.secondary)
				rendered = renderer.Word(deviceContext, *options.secondary,
					*visualInheritance.secondary,
					options.secondaryWeight) || rendered;
			// 角标在图标和文字之上，位于按压变换之内。
			rendered = DrawButtonBadge(renderer, deviceContext, button,
				visualInheritance.button) || rendered;
		}
		return rendered;
	}

	std::optional<D2D1_RECT_F> ResolveFixedButtonVisualBounds(UIRendering& renderer,
		const ButtonVisualParts& parts, const ButtonVisualInheritance& inheritance,
		const FixedButtonDrawOptions& options)
	{
		if ((options.underlay || options.content) && !options.contentExtent) return std::nullopt;
		auto bounds = ComputeButtonVisualBounds(renderer, parts, inheritance);
		if (!bounds || !options.contentExtent) return bounds;
		const auto& local = options.contentExtent->boundsDip;
		const double zoom = renderer.GetFrameZoom();
		const D2D1_RECT_F content{static_cast<FLOAT>((inheritance.button.x + local.left) * zoom),
			static_cast<FLOAT>((inheritance.button.y + local.top) * zoom),
			static_cast<FLOAT>((inheritance.button.x + local.right) * zoom),
			static_cast<FLOAT>((inheritance.button.y + local.bottom) * zoom)};
		if (!std::isfinite(content.left) || !std::isfinite(content.top)
			|| !std::isfinite(content.right) || !std::isfinite(content.bottom)) return std::nullopt;
		if (content.left < content.right && content.top < content.bottom)
		{
			bounds->left = (std::min)(bounds->left, content.left);
			bounds->top = (std::min)(bounds->top, content.top);
			bounds->right = (std::max)(bounds->right, content.right);
			bounds->bottom = (std::max)(bounds->bottom, content.bottom);
		}
		return bounds;
	}

	bool DrawFixedButtonVisual(UIRendering& renderer,ID2D1DeviceContext* context,
		const ButtonVisualParts& parts,const ButtonVisualInheritance& inherit,
		DWRITE_FONT_WEIGHT weight,DWRITE_TEXT_ALIGNMENT alignment,bool fitPrimaryLabel,
		const FixedButtonDrawOptions& options)
	{
		if(!context)return false;
		ButtonDrawTransform transform(renderer,context,parts.background,inherit.button,
			parts.pressScale?static_cast<double>(parts.pressScale->val):1.0,
			parts.pressScale?static_cast<double>(parts.pressScale->tar):1.0,
			parts.pressScale?static_cast<double>(parts.pressScale->dur):0.0);
		const auto bounds = ResolveFixedButtonVisualBounds(renderer, parts, inherit, options);
		if (bounds && !renderer.ShouldDrawFrameBounds(context, *bounds)) return false;
		// 两个扩展层与标准内容处在同一个按压/字体目标作用域内。
		bool rendered = options.underlay ? options.underlay(renderer, context, parts, inherit) : false;
		if (options.drawBackground) rendered=renderer.Shape(context,parts.background,inherit.button)||rendered;
		if(parts.icon)rendered=renderer.Svg(context,*parts.icon,inherit.icon)||rendered;
		if(parts.primary)rendered=renderer.Word(context,*parts.primary,inherit.primary,weight,alignment,fitPrimaryLabel)||rendered;
		if(options.content)rendered=options.content(renderer,context,parts,inherit)||rendered;
		return rendered;
	}

	bool DrawBackgroundVisual(UIRendering& renderer,
		ID2D1DeviceContext* deviceContext, UiShapeClass& background,
		const UiInheritClass& inherit, RECT* targetRect, bool clip)
	{
		return renderer.Shape(deviceContext, background, inherit,
			targetRect, clip);
	}

}

namespace Inkeys::UI::UI3
{
	void ConfigureButtonVisualTargets(ButtonClass& button,
		ButtonVisualLayoutKind layoutKind,double duration,double scale,bool direct,
		std::optional<double> iconHeightOverride,std::optional<double> fontTargetScale,
		std::optional<double> labelFontSize,UIRendering* labelRenderer)
	{
		const auto metrics = ResolveButtonVisualMetrics(layoutKind);
		if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;
		auto Set = [&](UiValueClass& value, double target)
		{
			if (direct) value.SetDirect(target * scale);
			else value.SetTar(target * scale, duration);
		};
		Set(button.button.w, metrics.buttonWidthDip);
		Set(button.button.h, metrics.buttonHeightDip);
		if (button.button.rw) Set(*button.button.rw, ButtonCornerRadiusDip);
		if (button.button.rh) Set(*button.button.rh, ButtonCornerRadiusDip);
		if (button.button.ft) Set(*button.button.ft, ButtonFrameThicknessDip);
		if (button.icon.enable.tar)
		{
			button.icon.SetWH(std::nullopt, iconHeightOverride.value_or(metrics.iconSizeDip) * scale);
			if (direct)
			{
				button.icon.w.SetDirect(button.icon.w.tar);
				button.icon.h.SetDirect(button.icon.h.tar);
			}
			Set(button.icon.x, metrics.iconOffsetXDip);
			Set(button.icon.y, metrics.iconOffsetYDip);
		}
		Set(button.name.x, metrics.primaryOffsetXDip);
		Set(button.name.y, metrics.primaryOffsetYDip);
		Set(button.name.w, metrics.primarySlotWidthDip);
		Set(button.name.h, metrics.primarySlotHeightDip);
		double labelSize = labelFontSize.value_or(metrics.primaryFontSizeDip);
		if (labelRenderer && metrics.primarySlotWidthDip > 4 && labelSize > 0)
		{
			// 由调用方提供测量器；测量当前字和待切换字，先收字号再跨透明中点换字。
			struct LabelFitCache
			{
				wstring current, target, family, locale;
				double slot = 0, sourceSize = 0, fitted = 0;
				unsigned long long generation = 0;
			};
			static thread_local LabelFitCache cache;
			const auto current = button.name.content.GetVal(), target = button.name.TargetContent();
			auto& renderer = *labelRenderer;
			if (cache.current != current || cache.target != target || cache.family != renderer.GetFontFamily() ||
				cache.locale != renderer.GetTextLocale() || cache.slot != metrics.primarySlotWidthDip ||
				cache.sourceSize != labelSize || cache.generation != renderer.GetDeviceGeneration())
			{
				const double measured = (std::max)(renderer.MeasureText(current, labelSize, DWRITE_FONT_WEIGHT_BOLD).width,
					renderer.MeasureText(target, labelSize, DWRITE_FONT_WEIGHT_BOLD).width);
				const double available = metrics.primarySlotWidthDip - 4;
				const double fitted = measured > available ? labelSize * available / measured : labelSize;
				cache = {current, target, renderer.GetFontFamily(), renderer.GetTextLocale(), metrics.primarySlotWidthDip,
					labelSize, fitted, renderer.GetDeviceGeneration()};
			}
			labelSize = cache.fitted;
			if (labelSize * scale < button.name.size.val) button.name.size.SetDirect(labelSize * scale);
		}
		Set(button.name.size, labelSize);
		// 派生布局传入最终倍率；回到普通布局时清除覆盖，恢复原 size.tar 目标。
		button.name.fontTargetSize = fontTargetScale ? labelSize * *fontTargetScale : -1.0;
	}

	namespace
	{
		using Part = ButtonAnimationPart;
		using Property = ButtonAnimationProperty;
		// 完整按钮和固定属性中的独立 Shape/SVG/Word 使用同一属性访问与推进代码。
		struct ButtonAnimationVisitor
		{
			const ButtonAnimationContext& frame;
			const void* visual;
			Part part;
			ButtonAnimationResult result;
			void Observe(Property property, bool changed, bool active, bool same)
			{
				result.changed |= changed; result.active |= active;
				if (frame.observe) frame.observe(visual, part, property, changed, active, same);
			}
			template<class Value> void Advance(Value& value, bool replace, Property property)
			{
				if (value.IsSame()) { Observe(property, false, false, true); return; }
				// 零时间用于同帧重定向，不能被底层解释为立即落定。
				if (frame.animationEnabled && !replace
					&& (!std::isfinite(frame.dtSeconds) || frame.dtSeconds <= 0.0))
				{ Observe(property, false, true, false); return; }
				const auto advanced = UiAdvanceAnimation(value,
					{frame.dtSeconds, frame.speedRate, frame.animationEnabled, replace});
				Observe(property, advanced.changed, advanced.active, value.IsSame());
			}
			void String(UiStringClass& value, UiSVGClass* svg = nullptr)
			{
				const bool changed = !value.IsSame();
				if (changed) { value.ApplyTar(); if (svg) svg->NotifyObservedContentValueWrite(); }
				Observe(Property::Content, changed, false, value.IsSame());
			}
			template<class Visual> bool Replace(Visual& value)
			{
				const bool replace = frame.forceReplace || static_cast<bool>(value.forceReplace);
				value.forceReplace = false; return replace;
			}
		};
	}

	ButtonAnimationResult AdvanceShapeVisual(UiShapeClass& shape,
		const ButtonAnimationContext& frame)
	{
		ButtonAnimationVisitor v{frame, &shape, Part::Shape};
		const bool replace = v.Replace(shape);
		v.Advance(shape.enable, replace, Property::Geometry);
		for (auto* value : {&shape.x, &shape.y, &shape.w, &shape.h})
			v.Advance(*value, replace, Property::Geometry);
		for (auto* value : {&shape.rw, &shape.rh, &shape.ft})
			if (*value) v.Advance(**value, replace, Property::Geometry);
		for (auto* value : {&shape.fill, &shape.frame})
			if (*value) v.Advance(**value, replace, Property::Color);
		for (auto* value : {&shape.framePct, &shape.frameLightPct})
			if (*value) v.Advance(**value, replace, Property::Visibility);
		v.Advance(shape.pct, replace, Property::Visibility);
		return v.result;
	}

	ButtonAnimationResult AdvanceSvgVisual(UiSVGClass& icon,
		const ButtonAnimationContext& frame)
	{
		ButtonAnimationVisitor v{frame, &icon, Part::Icon};
		const bool replace = v.Replace(icon);
		const bool content = icon.AdvanceContentTransition(frame.dtSeconds, frame.speedRate);
		v.Observe(Property::Content, content, content, !content);
		v.Advance(icon.enable, replace, Property::Visibility);
		for (auto* value : {&icon.x, &icon.y, &icon.w, &icon.h, &icon.angle})
			v.Advance(*value, replace, Property::Geometry);
		v.String(icon.svg, frame.finiteSvgProbe ? &icon : nullptr);
		for (auto* value : {&icon.color1, &icon.color2})
			if (*value) v.Advance(**value, replace, Property::Color);
		v.Advance(icon.pct, replace, Property::Visibility);
		return v.result;
	}

	ButtonAnimationResult AdvanceWordVisual(UiWordClass& word,
		const ButtonAnimationContext& frame, ButtonAnimationPart part)
	{
		ButtonAnimationVisitor v{frame, &word, part};
		const bool replace = v.Replace(word);
		if (replace) word.fontCorrection.ForceNext();
		const bool content = word.AdvanceContentTransition(frame.dtSeconds, frame.speedRate);
		v.Observe(Property::Content, content, content, !content);
		v.Advance(word.enable, replace, Property::Visibility);
		for (auto* value : {&word.x, &word.y, &word.w, &word.h, &word.size})
			v.Advance(*value, replace, Property::Geometry);
		v.String(word.content);
		v.Advance(word.color, replace, Property::Color);
		v.Advance(word.pct, replace, Property::Visibility);
		return v.result;
	}

	ButtonAnimationResult AdvanceButtonVisual(ButtonClass& button,
		const ButtonAnimationContext& frame, UiWordClass* secondary, bool separator)
	{
		if (separator)
		{
			(void)StopButtonHoverVisual(button, true, false, separator);
			SetButtonPressedVisual(button, false);
		}
		else (void)UpdateButtonHoverVisual(button, frame.visible,
			frame.hoverAllowed, ButtonHoverFadeDurationSeconds);
		ButtonAnimationVisitor press{frame, &button.button, Part::Press};
		press.Advance(button.pressScale, frame.forceReplace, Property::Geometry);
		ButtonAnimationResult result = press.result;
		auto Join = [&](ButtonAnimationResult next)
		{ result.changed |= next.changed; result.active |= next.active; };
		Join(AdvanceShapeVisual(button.button, frame));
		Join(AdvanceSvgVisual(button.icon, frame));
		if (frame.finiteSvgProbe)
			button.icon.ObserveFiniteRequirement(button.iconKind == ButtonIconKindEnum::Svg && !separator);
		Join(AdvanceWordVisual(button.name, frame));
		if (secondary) Join(AdvanceWordVisual(*secondary, frame, Part::Secondary));
		if (button.badge)
		{
			// 角标显隐时长与曲线沿用图标换图的单程：进场 EaseOutBack，离场 EaseInBack。
			auto& badge = *button.badge;
			const bool shown = badge.shown;
			const auto curve = shown ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseInBack;
			// 仅从完全隐藏进场时把角度置为 -45 起点；中途反向沿当前角度继续。
			const bool enterFromHidden = shown && badge.progress.tar < 0.5 && badge.progress.val <= 0.000001;
			badge.progress.SetTar(shown ? 1.0 : 0.0, UiDefaultOperationDur * 0.5,
				std::nullopt, false, { curve, curve, 0.0, false });
			if (enterFromHidden) badge.angle.SetDirect(-45.0);
			badge.angle.SetTar(shown ? 0.0 : 45.0, UiDefaultOperationDur * 0.5,
				std::nullopt, false, { curve, curve, 0.0, false });
			badge.below.SetTar(badge.belowTarget ? 1.0 : 0.0, UiDefaultOperationDur,
				std::nullopt, false, { UiCurveEnum::EaseInOutCubic, UiCurveEnum::EaseInOutCubic, 0.0, false });
			ButtonAnimationVisitor v{frame, &badge, Part::Badge};
			v.Advance(badge.progress, frame.forceReplace, Property::Visibility);
			v.Advance(badge.angle, frame.forceReplace, Property::Geometry);
			v.Advance(badge.below, frame.forceReplace, Property::Geometry);
			Join(v.result);
		}
		return result;
	}

	ButtonHitSnapshot ResolveShapeHitSnapshot(const UiShapeClass& shape,
		const UiInheritClass& inherit,bool visible,bool enabled) noexcept
	{
		ButtonHitSnapshot hit;
		hit.boundsDip=D2D1::RectF(static_cast<FLOAT>(inherit.x),static_cast<FLOAT>(inherit.y),
			static_cast<FLOAT>(inherit.x+shape.w.val),static_cast<FLOAT>(inherit.y+shape.h.val));
		hit.visible=visible && shape.w.val>0 && shape.h.val>0;
		hit.enabled=enabled;hit.radiusX=shape.rw?static_cast<double>(shape.rw->val):0.0;
		hit.radiusY=shape.rh?static_cast<double>(shape.rh->val):0.0;
		return hit;
	}
	ButtonHitSnapshot ResolveButtonHitSnapshot(ButtonClass& button,
		const UiInheritClass& inherit, bool visible, bool enabled) noexcept
	{
		const auto current=PrepareButtonVisualInheritance(button,inherit);
		return ResolveShapeHitSnapshot(button.button,current.button,
			visible, enabled);
	}
	bool HitTestButton(const ButtonHitSnapshot& hit,double x,double y) noexcept
	{
		if(!hit.visible || !hit.enabled || !std::isfinite(x) || !std::isfinite(y))return false;
		const auto& r=hit.boundsDip;
		return hit.superellipseExponent>0
			?UiSuperellipseContainsPoint(x,y,1.0,r.left,r.top,r.right-r.left,r.bottom-r.top,hit.superellipseExponent)
			:UiRoundedRectContainsPoint(x,y,1.0,r.left,r.top,r.right-r.left,r.bottom-r.top,hit.radiusX,hit.radiusY);
	}

}
