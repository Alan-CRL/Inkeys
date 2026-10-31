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

	// 「青瓷」浅色：主题色与深色 #58FFEC 同色相，中性灰均掺入少量主题青。
	// 边框/分隔线/悬停底为预混色，在对象原有透明度下得到约 12%/16%/8% 的观感，
	// 因此不依赖材质系统的独立面板（橡皮、撤销、分页）也能取得一致结果。
	constexpr Color LightColor(ColorRole role) noexcept
	{
		switch (role)
		{
		case ColorRole::Surface: return Rgb(246, 249, 249);
		case ColorRole::SurfaceFrame: return Rgb(104, 120, 122);
		case ColorRole::IconPrimary: return Rgb(67, 87, 90);
		case ColorRole::TextPrimary: return Rgb(36, 54, 58);
		case ColorRole::Accent: return Rgb(0, 122, 115);
		case ColorRole::SelectedFill: return Rgb(0, 163, 153);
		case ColorRole::PressedFill: return Rgb(160, 184, 184);
		case ColorRole::SubtleFill: return Rgb(214, 226, 225);
		case ColorRole::SwatchFrame: return Rgb(169, 188, 187);
		case ColorRole::Divider: return Rgb(134, 148, 149);
		case ColorRole::EdgeLight: return Rgb(60, 200, 190);
		case ColorRole::ShadowKey: return Rgb(36, 54, 58);
		case ColorRole::ShadowAmbient: return Rgb(67, 87, 90);
		case ColorRole::TopHighlight: return Rgb(255, 255, 255);
		case ColorRole::DockTarget: return Rgb(0, 120, 212);
		default: return Rgb(36, 54, 58);
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
	inline constexpr double LightSelectedFillOpacityScale = 0.70;
	// 主栏、主按钮和分页这类常驻表面在浅色端更透，减少对课件文字的遮挡；弹出面板不受影响。
	inline constexpr double LightResidentSurfaceOpacityScale = 0.85;

	inline double SelectedFillOpacity(double lightWeight, double darkOpacity = 0.20) noexcept
	{
		const double dark = ClampWeight(darkOpacity);
		return Mix(dark, dark * LightSelectedFillOpacityScale, lightWeight);
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
	// 浅色边缘光为主题青，只在浅底上着色；强度略高于深色以保持相近的可见度。
	inline constexpr double LightEdgeLightIntensity = 1.5;
	inline constexpr double LightPrimaryRadiusScale = 360.0 / 480.0;
	inline constexpr double LightCursorRadiusScale = 200.0 / 240.0;

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
		// 填充透明度比例由常驻表面自行选择；边框沿用原透明度和 1 DIP 等宽描边。
		result.fillOpacityScale = 1.0;
		result.frameOpacityScale = 1.0;
		result.edgeLight = MixColor(darkFrame, LightColor(ColorRole::EdgeLight), weight);
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
		// Dark 保留随笔色变化；进入 Light 后连续收敛为固定主题青。
		result.penColorBlend = ClampWeight(drawingPenBlend)
			* (1.0 - material.lightWeight);
		result.color = MixColor(material.edgeLight, penColor,
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
