#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// 无设备依赖的材质策略；原始画笔色只作为输入，显示色不会写回绘图状态。
namespace BarThemeMaterial
{
	using Color = std::uint32_t;

	constexpr Color Rgb(unsigned int r, unsigned int g, unsigned int b) noexcept
	{
		return (r & 0xFFU) | ((g & 0xFFU) << 8) | ((b & 0xFFU) << 16);
	}

	enum class ColorRole
	{
		Surface,
		SurfaceFrame,
		IconPrimary,
		TextPrimary,
		Accent,
		SelectedFill,
		PressedFill,
		SubtleFill,
		SwatchFrame,
		Divider,
		EdgeLight,
		ShadowKey,
		ShadowAmbient,
		TopHighlight,
		DockTarget,
	};

	// 「青瓷」浅色：主题色与深色 #58FFEC 同色相，只用于强调、选中底和边缘光；
	// 中性灰为冷石墨灰（色相约 207°、低饱和），避免低饱和青绿灰显脏。
	// 边框/分隔线/悬停底为预混色，在对象原有透明度下得到约 12%/16%/8% 的观感；
	// 浅色表面边框另按 LightFrameOpacityScale 加深，不依赖材质系统的独立面板（橡皮、撤销、分页）
	// 通过 GetThemeSurfaceFrameOpacity 取得同一结果。
	constexpr Color LightColor(ColorRole role) noexcept
	{
		switch (role)
		{
		case ColorRole::Surface: return Rgb(247, 248, 250);
		case ColorRole::SurfaceFrame: return Rgb(107, 114, 120);
		case ColorRole::IconPrimary: return Rgb(71, 77, 82);
		case ColorRole::TextPrimary: return Rgb(34, 39, 43);
		// 选中文字压在同色半透明底上，加深到在深色背景下仍约 4.9:1。
		case ColorRole::Accent: return Rgb(0, 98, 92);
		case ColorRole::SelectedFill: return Rgb(0, 163, 153);
		case ColorRole::PressedFill: return Rgb(174, 180, 186);
		case ColorRole::SubtleFill: return Rgb(221, 225, 229);
		case ColorRole::SwatchFrame: return Rgb(180, 185, 190);
		case ColorRole::Divider: return Rgb(138, 144, 150);
		// 边缘光与 Dark 一致取边框色，不再使用独立的浅青。
		case ColorRole::EdgeLight: return Rgb(107, 114, 120);
		case ColorRole::ShadowKey: return Rgb(34, 39, 43);
		case ColorRole::ShadowAmbient: return Rgb(71, 77, 82);
		case ColorRole::TopHighlight: return Rgb(255, 255, 255);
		case ColorRole::DockTarget: return Rgb(0, 120, 212);
		default: return Rgb(34, 39, 43);
		}
	}

	inline double ClampWeight(double value) noexcept
	{
		return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
	}

	inline double Mix(double dark, double light, double lightWeight) noexcept
	{
		return dark + (light - dark) * ClampWeight(lightWeight);
	}

	// 浅色选中底与深色同为半透明主题色，只按比例降低，避免实色卡片压住背景。
	inline constexpr double LightSelectedFillOpacityScale = 0.60;
	// 主栏、主按钮和分页这类常驻表面在浅色端更实：半透明浅底压在深色背景上会混成脏灰，
	// 0.80 × 1.10 = 0.88 让深色背景下图标仍有约 5.7:1 对比度；弹出面板不受影响。
	inline constexpr double LightResidentSurfaceOpacityScale = 1.10;
	// 浅色表面外缘在浅背景上几乎与背景同亮，单层边框按比例加深（0.18 → 约 0.32）。
	inline constexpr double LightFrameOpacityScale = 1.75;
	// 浅色底上 0.30 的禁用内容只剩约 1.5:1，提高到约 2:1 仍明显区别于可用态。
	inline constexpr double DarkDisabledContentOpacity = 0.30;
	inline constexpr double LightDisabledContentOpacity = 0.45;

	// 主栏展开时的 Dark 常驻填充透明度；其他常驻表面按与它的比例同步缩放。
	inline constexpr double ResidentBaseFillOpacity = 0.80;

	// 常驻表面填充比例：用户设置的绝对透明度优先（>0），否则为主题默认（Light 端 ×1.10）。
	inline double ResidentFillOpacityScale(double lightWeight, double lightScale,
		double absoluteOpacity) noexcept
	{
		if (std::isfinite(absoluteOpacity) && absoluteOpacity > 0.0)
			return std::clamp(absoluteOpacity, 0.0, 1.0) / ResidentBaseFillOpacity;
		return Mix(1.0, lightScale, lightWeight);
	}

	// 设置页显示的主题默认值：Dark 0.80、Light 0.88。
	inline double DefaultResidentFillOpacity(double lightWeight) noexcept
	{
		return ResidentBaseFillOpacity
			* ResidentFillOpacityScale(lightWeight, LightResidentSurfaceOpacityScale, 0.0);
	}

	inline double SelectedFillOpacity(double lightWeight, double darkOpacity = 0.20) noexcept
	{
		const double dark = ClampWeight(darkOpacity);
		return Mix(dark, dark * LightSelectedFillOpacityScale, lightWeight);
	}

	inline double FrameOpacity(double lightWeight, double darkOpacity) noexcept
	{
		return Mix(darkOpacity, darkOpacity * LightFrameOpacityScale, lightWeight);
	}

	inline double DisabledContentOpacity(double lightWeight) noexcept
	{
		return Mix(DarkDisabledContentOpacity, LightDisabledContentOpacity, lightWeight);
	}

	inline Color MixColor(Color dark, Color light, double lightWeight) noexcept
	{
		const double weight = ClampWeight(lightWeight);
		auto channel = [=](unsigned int shift) -> unsigned int
			{
				const auto from = static_cast<double>((dark >> shift) & 0xFFU);
				const auto to = static_cast<double>((light >> shift) & 0xFFU);
				return static_cast<unsigned int>(from + (to - from) * weight + 0.5);
			};
		return Rgb(channel(0), channel(8), channel(16));
	}

	// 半径和位移均为 DIP；统一上界同时供绘制、dirty 和 viewport 使用。
	inline constexpr double SurfaceShadowOutsetDip = 8.0;
	inline constexpr double DarkFrameDiffuseOpacity = 0.30;
	inline constexpr double DarkPenDiffuseOpacity = 0.20;
	inline constexpr double LightFrameDiffuseOpacity = 0.08;
	inline constexpr double LightPenDiffuseOpacity = 0.06;
	// 浅色边缘光沿用对象边框色；强度略高于深色以保持相近的可见度。
	inline constexpr double LightEdgeLightIntensity = 1.5;
	inline constexpr double LightPrimaryRadiusScale = 360.0 / 480.0;
	inline constexpr double LightCursorRadiusScale = 200.0 / 240.0;
	// WinUI 3 Slider Thumb 外圈：ControlElevationBorder 约 6%~16% 黑，取 12% 石墨灰。
	inline constexpr double LightSliderThumbBorderOpacity = 0.12;

	struct Material
	{
		double lightWeight = 0.0;
		Color surface = 0;
		Color surfaceFrame = 0;
		double fillOpacityScale = 1.0;
		double frameOpacityScale = 1.0;
		Color edgeLight = 0;
		double lightIntensity = 1.0;
		double cursorLightIntensityScale = 1.0;
		double primaryLightRadiusScale = 1.0;
		double cursorLightRadiusScale = 1.0;
		double penTintScale = 1.0;
		double diffuseFrameOpacity = DarkFrameDiffuseOpacity;
		double diffusePenOpacity = DarkPenDiffuseOpacity;
		Color keyShadowColor = 0;
		double keyShadowOpacity = 0.0;
		double keyShadowRadiusDip = 0.0;
		double keyShadowOffsetYDip = 0.0;
		Color ambientShadowColor = 0;
		double ambientShadowOpacity = 0.0;
		double ambientShadowRadiusDip = 0.0;
		double ambientShadowOffsetYDip = 0.0;
		Color highlightColor = 0;
		double highlightOpacity = 0.0;
	};

	inline Material Resolve(double lightWeight, Color darkSurface, Color darkFrame) noexcept
	{
		const double weight = ClampWeight(lightWeight);
		Material result;
		result.lightWeight = weight;
		result.surface = MixColor(darkSurface, LightColor(ColorRole::Surface), weight);
		result.surfaceFrame = MixColor(darkFrame, LightColor(ColorRole::SurfaceFrame), weight);
		// 填充透明度比例由常驻表面自行选择；边框保持 1 DIP 单层，浅色端按比例加深。
		result.fillOpacityScale = 1.0;
		result.frameOpacityScale = Mix(1.0, LightFrameOpacityScale, weight);
		// 边缘光与 Dark 同规则：未绘制时取对象自身边框色（表面/分隔线为灰，选中按钮为主题青），
		// 绘制时再向笔色过渡；renderer 会用实际绘制的边框色覆盖此默认值。
		result.edgeLight = result.surfaceFrame;
		result.lightIntensity = Mix(1.0, LightEdgeLightIntensity, weight);
		result.cursorLightIntensityScale = Mix(1.0, 0.85, weight);
		result.primaryLightRadiusScale = Mix(1.0, LightPrimaryRadiusScale, weight);
		result.cursorLightRadiusScale = Mix(1.0, LightCursorRadiusScale, weight);
		result.penTintScale = 1.0;
		result.diffuseFrameOpacity = Mix(DarkFrameDiffuseOpacity, LightFrameDiffuseOpacity, weight);
		result.diffusePenOpacity = Mix(DarkPenDiffuseOpacity, LightPenDiffuseOpacity, weight);
		result.keyShadowColor = MixColor(darkSurface, LightColor(ColorRole::ShadowKey), weight);
		result.keyShadowOpacity = 0.0;
		result.keyShadowRadiusDip = 0.0;
		result.keyShadowOffsetYDip = 0.0;
		result.ambientShadowColor = MixColor(darkSurface, LightColor(ColorRole::ShadowAmbient), weight);
		result.ambientShadowOpacity = 0.0;
		result.ambientShadowRadiusDip = 0.0;
		result.ambientShadowOffsetYDip = 0.0;
		result.highlightColor = LightColor(ColorRole::TopHighlight);
		result.highlightOpacity = 0.0;
		return result;
	}

	struct Lighting
	{
		Color color = 0;
		double intensity = 1.0;
		double diffuseOpacity = DarkFrameDiffuseOpacity;
		double penColorBlend = 0.0;
	};

	inline double RelativeLuminance(Color color) noexcept
	{
		auto LinearChannel = [&](unsigned int shift)
			{
				double value = static_cast<double>((color >> shift) & 0xFFU) / 255.0;
				return value <= 0.04045 ? value / 12.92
					: std::pow((value + 0.055) / 1.055, 2.4);
			};
		return LinearChannel(0) * 0.2126 + LinearChannel(8) * 0.7152
			+ LinearChannel(16) * 0.0722;
	}

	inline double ContrastRatio(Color first, Color second) noexcept
	{
		double firstLuminance = RelativeLuminance(first);
		double secondLuminance = RelativeLuminance(second);
		return ((std::max)(firstLuminance, secondLuminance) + 0.05)
			/ ((std::min)(firstLuminance, secondLuminance) + 0.05);
	}

	inline Color CorrectLightPenColor(Color penColor) noexcept
	{
		constexpr double minimumContrast = 2.0;
		const Color surface = LightColor(ColorRole::Surface);
		const Color graphite = LightColor(ColorRole::IconPrimary);
		if (ContrastRatio(penColor, surface) >= minimumContrast) return penColor;

		// 只修正用于边缘光的显示色；二分找到最少的图标灰混合量。
		double low = 0.0, high = 1.0;
		for (int index = 0; index < 12; ++index)
		{
			double middle = (low + high) / 2.0;
			if (ContrastRatio(MixColor(penColor, graphite, middle), surface)
				>= minimumContrast)
				high = middle;
			else low = middle;
		}
		return MixColor(penColor, graphite, high);
	}

	inline Lighting ResolveLighting(const Material& material,
		Color penColor, double drawingPenBlend) noexcept
	{
		const double blend = ClampWeight(drawingPenBlend);
		Lighting result;
		result.penColorBlend = blend * material.penTintScale;
		Color visiblePenColor = penColor;
		if (result.penColorBlend > 0.0 && material.lightWeight > 0.0)
		{
			// 非绘制对象和 Dark 端无需执行对比度修正，避免逐对象重复计算。
			visiblePenColor = MixColor(penColor,
				CorrectLightPenColor(penColor), material.lightWeight);
		}
		result.color = MixColor(material.edgeLight, visiblePenColor,
			result.penColorBlend);
		result.intensity = material.lightIntensity;
		result.diffuseOpacity = Mix(material.diffuseFrameOpacity,
			material.diffusePenOpacity, blend);
		return result;
	}

	inline Lighting ResolveCursorLighting(const Material& material,
		Color penColor, double drawingPenBlend) noexcept
	{
		Lighting result;
		// 深浅一致：鼠标光同样随笔色变化；浅色端只把低对比笔色校正到可见，不写回真实 RGB。
		result.penColorBlend = ClampWeight(drawingPenBlend);
		Color visiblePenColor = penColor;
		if (result.penColorBlend > 0.0 && material.lightWeight > 0.0)
			visiblePenColor = MixColor(penColor,
				CorrectLightPenColor(penColor), material.lightWeight);
		result.color = MixColor(material.edgeLight, visiblePenColor,
			result.penColorBlend);
		result.intensity = material.lightIntensity
			* material.cursorLightIntensityScale;
		result.diffuseOpacity = Mix(material.diffuseFrameOpacity,
			material.diffusePenOpacity, result.penColorBlend);
		return result;
	}

	inline Color DisplayPenColor(Color actual, double lightWeight) noexcept
	{
		const double red = static_cast<double>(actual & 0xFFU);
		const double green = static_cast<double>((actual >> 8) & 0xFFU);
		const double blue = static_cast<double>((actual >> 16) & 0xFFU);
		const double high = (std::max)({ red, green, blue });
		const double low = (std::min)({ red, green, blue });
		Color display;
		if (high - low < 8.0)
		{
			// 黑/灰笔在深色入口仍保持中性色，白笔不再额外增亮。
			display = MixColor(Rgb(152, 152, 152), Rgb(255, 255, 255), high / 255.0);
		}
		else
		{
			const double brightness = (std::max)(232.0, high);
			const double saturation = (std::min)(1.0, (high - low) / high * 1.08);
			auto channel = [=](double source) -> unsigned int
				{
					const double normalized = (source - low) / (high - low);
					return static_cast<unsigned int>(brightness
						* (1.0 - saturation + saturation * normalized) + 0.5);
				};
			display = Rgb(channel(red), channel(green), channel(blue));
		}
		return MixColor(display, actual, lightWeight);
	}
}
