module;

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

// L1/L2 视觉脏区适配器：把标准图元与按钮的「有效绘制输入」写进采样，并给出局部包络。
// 只读对象字段，不推进动画、不修改对象；宿主负责在准备阶段把继承位置等写成绘制时的实际值。
export module Inkeys.UI.UI3:VisualDirtySampler;

import :Primitive;
import :Envelope;
import :Button;
import Inkeys.UI.UI3.VisualDirty;

export namespace Inkeys::UI::UI3
{
	// 宿主上下文：zoom 为当前帧布局 → 像素倍率；map 把局部像素包络变换到宿主追踪坐标（空 = 恒等）；
	// extra 追加绘制点选项与作用域环境（字重/对齐、所在组冻结的环境值等）。
	struct VisualSamplerContext
	{
		std::function<double()> zoom;
		std::function<RECT(const RECT&)> map;
		std::function<void(VisualStateWriter&)> extra;
	};

	// 与 UIRendering 的跳过绘制门槛一致；改动 Renderer 门槛时必须同步这里。
	[[nodiscard]] inline bool IsVisuallyDrawn(const UiShapeClass& shape) noexcept
	{
		if (!shape.enable.val || (!shape.fill.has_value() && !shape.frame.has_value())) return false;
		if (shape.w.val <= 0 || shape.h.val <= 0) return false;
		const double frameLight = shape.frameLightPct.has_value() ? static_cast<double>(shape.frameLightPct->val) : 0.0;
		return shape.pct.val > 0.0 || frameLight > 0.0;
	}
	[[nodiscard]] inline bool IsVisuallyDrawn(const UiSuperellipseClass& shape) noexcept
	{
		if (!shape.enable.val || (!shape.fill.has_value() && !shape.frame.has_value())) return false;
		return shape.w.val > 0 && shape.h.val > 0 && shape.pct.val > 0.0;
	}
	[[nodiscard]] inline bool IsVisuallyDrawn(const UiSVGClass& svg) noexcept
	{
		const double scale = svg.contentScale, contentPct = svg.contentPct;
		return svg.enable.val && svg.w.val > 0 && svg.h.val > 0 && svg.pct.val > 0.0
			&& std::isfinite(scale) && scale > 0.0 && std::isfinite(contentPct) && contentPct > 0.0;
	}
	[[nodiscard]] inline bool IsVisuallyDrawn(const UiPNGClass& png) noexcept
	{
		return png.enable.val && png.w.val > 0.0 && png.h.val > 0.0 && png.pct.val > 0.0;
	}
	[[nodiscard]] inline bool IsVisuallyDrawn(const UiWordClass& word) noexcept
	{
		const double contentPct = word.contentPct;
		return word.enable.val && word.size.val > 0 && word.w.val > 0 && word.h.val > 0
			&& word.pct.val > 0.0 && contentPct > 0.0;
	}

	namespace Detail
	{
		inline void WriteOptionalValue(VisualStateWriter& out, const std::optional<UiValueClass>& value)
		{
			out.Bool(value.has_value());
			if (value) out.Real(value->val);
		}
		inline void WriteOptionalColor(VisualStateWriter& out, const std::optional<UiColorClass>& value)
		{
			out.Bool(value.has_value());
			if (value) out.Color(value->val);
		}
		inline void WriteOptionalPct(VisualStateWriter& out, const std::optional<UiPctClass>& value)
		{
			out.Bool(value.has_value());
			if (value) out.Real(value->val);
		}
		// position=false 时不写继承位置：调用方按继承链纯计算位置后单独写入（避免读到绘制阶段才更新的 inhX）。
		template <class T> void WriteBase(VisualStateWriter& out, const T& object, bool position = true)
		{
			if (position) { out.Real(static_cast<double>(object.inhX)); out.Real(static_cast<double>(object.inhY)); }
			out.Real(object.w.val); out.Real(object.h.val);
			out.Real(object.pct.val);
		}
		template <class T> void WriteFrameLighting(VisualStateWriter& out, const T& shape)
		{
			out.Real(shape.lightMaterial); out.Bool(shape.themeSurface);
			out.Real(shape.lightFillOpacityScale); out.Bool(shape.residentSurface);
			out.Int(static_cast<int>(shape.frameRendering));
			out.Int(static_cast<int>(shape.frameLightColor));
			out.Int(static_cast<int>(shape.frameLightOpacitySource));
			out.Bool(shape.framePrimaryLightEnabled);
			out.Real(shape.frameCursorLightIntensityScale);
			out.Real(shape.frameLightCursorLightIntensityScale);
		}
		// 文字绘制按 contentScale 围绕中心放大裁剪矩形；>1 时包络同步放大。
		inline RECT WordBounds(const UiWordClass& word, double zoom)
		{
			RECT bounds = RenderingAttribute::GetWeigetRect(word, zoom);
			const double scale = word.contentScale;
			if (std::isfinite(scale) && scale > 1.0)
			{
				const double growX = (bounds.right - bounds.left) * (scale - 1.0) / 2.0 + 1.0;
				const double growY = (bounds.bottom - bounds.top) * (scale - 1.0) / 2.0 + 1.0;
				bounds.left -= static_cast<LONG>(std::ceil(growX)); bounds.right += static_cast<LONG>(std::ceil(growX));
				bounds.top -= static_cast<LONG>(std::ceil(growY)); bounds.bottom += static_cast<LONG>(std::ceil(growY));
			}
			return bounds;
		}
		// 按压缩放围绕按钮底板中心；回弹 >1 时，包络内任一点到该中心的距离按比例增大，
		// 不依赖中心位置的保守做法：四边各外扩 (scale-1) × 包络最大边长 + 1 像素。
		inline void GrowForPress(RECT& bounds, double press)
		{
			if (!(press > 1.0) || !std::isfinite(press)) return;
			const double extent = (std::max)(bounds.right - bounds.left, bounds.bottom - bounds.top);
			const LONG grow = static_cast<LONG>(std::ceil(extent * (press - 1.0))) + 1;
			bounds.left -= grow; bounds.top -= grow; bounds.right += grow; bounds.bottom += grow;
		}
		inline RECT Map(const VisualSamplerContext& context, const RECT& bounds)
		{
			return context.map ? context.map(bounds) : bounds;
		}
		inline double Zoom(const VisualSamplerContext& context)
		{
			return context.zoom ? context.zoom() : 1.0;
		}
	}

	inline void WriteVisualState(VisualStateWriter& out, const UiShapeClass& shape, bool position = true)
	{
		Detail::WriteBase(out, shape, position);
		Detail::WriteOptionalValue(out, shape.rw); Detail::WriteOptionalValue(out, shape.rh);
		Detail::WriteOptionalValue(out, shape.ft);
		Detail::WriteOptionalColor(out, shape.fill); Detail::WriteOptionalColor(out, shape.frame);
		Detail::WriteOptionalPct(out, shape.framePct); Detail::WriteOptionalPct(out, shape.frameLightPct);
		Detail::WriteFrameLighting(out, shape);
	}
	inline void WriteVisualState(VisualStateWriter& out, const UiSuperellipseClass& shape, bool position = true)
	{
		Detail::WriteBase(out, shape, position);
		Detail::WriteOptionalValue(out, shape.n); Detail::WriteOptionalValue(out, shape.ft);
		Detail::WriteOptionalColor(out, shape.fill); Detail::WriteOptionalColor(out, shape.frame);
		Detail::WriteOptionalPct(out, shape.framePct);
		Detail::WriteFrameLighting(out, shape);
	}
	inline void WriteVisualState(VisualStateWriter& out, const UiSVGClass& svg, bool position = true)
	{
		Detail::WriteBase(out, svg, position);
		out.UInt(svg.svg.ValRevision());
		out.Real(svg.angle.val);
		out.Real(static_cast<double>(svg.contentScale)); out.Real(static_cast<double>(svg.contentPct));
		Detail::WriteOptionalColor(out, svg.color1); Detail::WriteOptionalColor(out, svg.color2);
		out.Bool(svg.appearanceKey.has_value());
		if (svg.appearanceKey) out.UInt(*svg.appearanceKey);
	}
	inline void WriteVisualState(VisualStateWriter& out, const UiPNGClass& png, bool position = true)
	{
		Detail::WriteBase(out, png, position);
		out.UInt(png.contentRevision);
		out.Real(png.angle.val);
		out.Real(png.rW); out.Real(png.rH);
	}
	inline void WriteVisualState(VisualStateWriter& out, const UiWordClass& word, bool position = true)
	{
		Detail::WriteBase(out, word, position);
		// 字号拟合读取几何与字号的目标值；字号修正在绘制中推进，当前值与目标值都入采样，
		// 修正动画进行中会持续判定变化，静止后收敛（准备阶段前移后改为直接采样解析结果）。
		out.Real(word.w.tar); out.Real(word.h.tar);
		out.Real(word.size.val); out.Real(word.size.tar);
		out.Real(static_cast<double>(word.fontTargetSize));
		out.BeginVolatile();
		out.Real(word.fontCorrection.Current()); out.Real(word.fontCorrection.Target());
		out.EndVolatile();
		// 修正值已入采样：在宿主准备作用域内记下本字号帧的登记，其推进由登记表比较得出脏区，宿主不必再因它整幅。
		word.fontCorrection.MarkRegistrySampled();
		out.UInt(word.content.ValRevision());
		out.Real(static_cast<double>(word.contentScale)); out.Real(static_cast<double>(word.contentPct));
		out.Color(word.color.val);
	}

	// 标准图元采样器：对象登记后每帧按最终字段采样；对象生命周期须长于其登记。
	template <class T>
	[[nodiscard]] VisualSampler MakeVisualSampler(const T& object, VisualSamplerContext context)
	{
		return [&object, context = std::move(context)](VisualSample& sample)
			{
				sample.visible = IsVisuallyDrawn(object);
				if (!sample.visible) return;
				const double zoom = Detail::Zoom(context);
				RECT bounds{};
				if constexpr (std::is_same_v<T, UiWordClass>) bounds = Detail::WordBounds(object, zoom);
				else bounds = RenderingAttribute::GetWeigetRect(object, zoom);
				sample.bounds = Detail::Map(context, bounds);
				sample.state.Real(zoom);
				WriteVisualState(sample.state, object);
				if (context.extra) context.extra(sample.state);
			};
	}

	// 按钮：背景、图标（PNG 与 SVG 共用 icon 的布局状态）、主文字、可选副文字、角标与按压缩放合为一个对象。
	// 继承位置须已由 PrepareButtonVisualInheritance 写成本帧绘制值。按压缩放围绕按钮中心，
	// 回弹峰值（EaseOutBack，约 1.005）由点光与抗锯齿外扩覆盖；仍按 max(1, scale) 放大包络以防更大回弹。
	[[nodiscard]] inline VisualSampler MakeButtonVisualSampler(ButtonClass& button,
		VisualSamplerContext context, const UiWordClass* secondary = nullptr)
	{
		return [&button, secondary, context = std::move(context)](VisualSample& sample)
			{
				const double zoom = Detail::Zoom(context);
				RECT bounds{};
				bool visible = false;
				auto include = [&](bool drawn, const RECT& rect)
					{
						if (!drawn) return;
						visible = true;
						RenderingAttribute::UnionRectInPlace(bounds, rect);
					};
				const bool png = button.iconKind == ButtonIconKindEnum::Png;
				// PNG 的几何、透明度与 enable 在绘制时从 icon 复制，可见性与包络都以 icon 为准。
				const bool backgroundDrawn = IsVisuallyDrawn(button.button);
				// PNG 绘制只从 icon 复制几何、角度、透明度与启用态，不使用 SVG 的 contentScale/contentPct。
				const bool iconDrawn = png
					? (button.icon.enable.val && button.icon.w.val > 0 && button.icon.h.val > 0 && button.icon.pct.val > 0.0)
					: IsVisuallyDrawn(button.icon);
				const bool nameDrawn = IsVisuallyDrawn(button.name);
				const bool secondaryDrawn = secondary && IsVisuallyDrawn(*secondary);
				include(backgroundDrawn, RenderingAttribute::GetWeigetRect(button.button, zoom));
				include(iconDrawn, png
					? RenderingAttribute::GetRotatedImageRect(button.icon.inhX, button.icon.inhY,
						button.icon.w.val, button.icon.h.val, button.icon.angle.val, zoom)
					: RenderingAttribute::GetWeigetRect(button.icon, zoom));
				include(nameDrawn, Detail::WordBounds(button.name, zoom));
				if (secondary) include(secondaryDrawn, Detail::WordBounds(*secondary, zoom));
				if (button.badge)
				{
					const RECT badge = ResolveButtonBadgeBounds(button,
						UiInheritClass(button.button.inhX, button.button.inhY), zoom);
					include(badge.right > badge.left && badge.bottom > badge.top, badge);
				}
				sample.visible = visible;
				if (!visible) return;
				const double press = button.pressScale.val;
				Detail::GrowForPress(bounds, press);
				sample.bounds = Detail::Map(context, bounds);
				auto& out = sample.state;
				out.Real(zoom);
				out.Real(press);
				out.Int(static_cast<int>(static_cast<ButtonIconKindEnum>(button.iconKind)));
				// 部件显隐逐项入采样：部件在其他部件包络内切换时，联合包络与字段可能完全相同。
				out.Bool(backgroundDrawn); out.Bool(iconDrawn); out.Bool(nameDrawn); out.Bool(secondaryDrawn);
				WriteVisualState(out, button.button);
				WriteVisualState(out, button.icon);
				// PNG 的几何、透明度在绘制时从 icon 复制，只采样它自己的载荷身份。
				if (png) { out.UInt(button.pngIcon.contentRevision); out.Real(button.pngIcon.rW); out.Real(button.pngIcon.rH); }
				WriteVisualState(out, button.name);
				out.Bool(secondary != nullptr);
				if (secondary) WriteVisualState(out, *secondary);
				out.Bool(button.badge.has_value());
				if (button.badge)
				{
					// 角标几何在绘制时由这些输入派生，采样输入而非派生后的 SVG 字段。
					const auto& badge = *button.badge;
					out.Real(badge.progress.val); out.Real(badge.below.val); out.Real(badge.angle.val);
					out.Real(static_cast<double>(badge.scale)); out.Bool(static_cast<bool>(badge.shown));
					out.UInt(badge.svg.svg.ValRevision());
				}
				if (context.extra) context.extra(out);
			};
	}

	// 固定按钮（借用已有字段的 ButtonVisualParts）：背景、图标、主文字与按压缩放合为一个对象。
	[[nodiscard]] inline VisualSampler MakeFixedButtonVisualSampler(const ButtonVisualParts& parts,
		VisualSamplerContext context)
	{
		return [parts, context = std::move(context)](VisualSample& sample)
			{
				const double zoom = Detail::Zoom(context);
				RECT bounds{};
				bool visible = false;
				auto include = [&](bool drawn, const RECT& rect)
					{
						if (!drawn) return;
						visible = true;
						RenderingAttribute::UnionRectInPlace(bounds, rect);
					};
				const bool backgroundDrawn = IsVisuallyDrawn(parts.background);
				const bool iconDrawn = parts.icon && IsVisuallyDrawn(*parts.icon);
				const bool primaryDrawn = parts.primary && IsVisuallyDrawn(*parts.primary);
				include(backgroundDrawn, RenderingAttribute::GetWeigetRect(parts.background, zoom));
				if (parts.icon) include(iconDrawn, RenderingAttribute::GetWeigetRect(*parts.icon, zoom));
				if (parts.primary) include(primaryDrawn, Detail::WordBounds(*parts.primary, zoom));
				sample.visible = visible;
				if (!visible) return;
				const double press = parts.pressScale ? static_cast<double>(parts.pressScale->val) : 1.0;
				Detail::GrowForPress(bounds, press);
				sample.bounds = Detail::Map(context, bounds);
				auto& out = sample.state;
				out.Real(zoom); out.Real(press);
				out.Bool(backgroundDrawn); out.Bool(iconDrawn); out.Bool(primaryDrawn);
				WriteVisualState(out, parts.background);
				out.Bool(parts.icon != nullptr);
				if (parts.icon) WriteVisualState(out, *parts.icon);
				out.Bool(parts.primary != nullptr);
				if (parts.primary) WriteVisualState(out, *parts.primary);
				if (context.extra) context.extra(out);
			};
	}
}
