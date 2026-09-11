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

	constexpr Color LightColor(ColorRole role) noexcept
	{
		switch (role)
		{
		case ColorRole::Surface: return Rgb(244, 246, 247);
		case ColorRole::SurfaceFrame: return Rgb(211, 220, 224);
		case ColorRole::IconPrimary: return Rgb(83, 97, 106);
		case ColorRole::TextPrimary: return Rgb(61, 71, 77);
		case ColorRole::Accent: return Rgb(0, 111, 104);
		case ColorRole::SelectedFill: return Rgb(215, 238, 234);
		case ColorRole::PressedFill: return Rgb(190, 205, 211);
		case ColorRole::SubtleFill: return Rgb(218, 227, 230);
		case ColorRole::SwatchFrame: return Rgb(173, 186, 193);
		case ColorRole::Divider: return Rgb(214, 223, 227);
		case ColorRole::EdgeLight: return Rgb(250, 253, 255);
		case ColorRole::ShadowKey: return Rgb(65, 75, 82);
		case ColorRole::ShadowAmbient: return Rgb(91, 103, 110);
		case ColorRole::TopHighlight: return Rgb(255, 255, 255);
		case ColorRole::DockTarget: return Rgb(0, 120, 212);
		default: return Rgb(61, 71, 77);
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

	inline double SelectedFillOpacity(double lightWeight, double darkOpacity = 0.20) noexcept
	{
		return Mix(ClampWeight(darkOpacity), 1.0, lightWeight);
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

	struct Material
	{
		Color surface = 0;
		Color surfaceFrame = 0;
		double fillOpacityScale = 1.0;
		double frameOpacityScale = 1.0;
		Color edgeLight = 0;
		double lightIntensity = 1.0;
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
		result.surface = MixColor(darkSurface, LightColor(ColorRole::Surface), weight);
		result.surfaceFrame = MixColor(darkFrame, LightColor(ColorRole::SurfaceFrame), weight);
		// 保留旧显隐动画，0.8 的展开表面在 Light 端变为 0.96。
		result.fillOpacityScale = Mix(1.0, 1.2, weight);
		result.frameOpacityScale = Mix(1.0, 3.0, weight);
		result.edgeLight = MixColor(darkFrame, LightColor(ColorRole::EdgeLight), weight);
		result.lightIntensity = Mix(1.0, 0.45, weight);
		result.penTintScale = Mix(1.0, 0.04, weight);
		result.diffuseFrameOpacity = Mix(DarkFrameDiffuseOpacity, 0.08, weight);
		result.diffusePenOpacity = Mix(DarkPenDiffuseOpacity, 0.06, weight);
		result.keyShadowColor = MixColor(darkSurface, LightColor(ColorRole::ShadowKey), weight);
		result.keyShadowOpacity = 0.075 * weight;
		result.keyShadowRadiusDip = 2.0 * weight;
		result.keyShadowOffsetYDip = 2.0 * weight;
		result.ambientShadowColor = MixColor(darkSurface, LightColor(ColorRole::ShadowAmbient), weight);
		result.ambientShadowOpacity = 0.085 * weight;
		result.ambientShadowRadiusDip = (SurfaceShadowOutsetDip - 1.0) * weight;
		result.ambientShadowOffsetYDip = weight;
		result.highlightColor = LightColor(ColorRole::TopHighlight);
		result.highlightOpacity = 0.32 * weight;
		return result;
	}

	struct Lighting
	{
		Color color = 0;
		double intensity = 1.0;
		double diffuseOpacity = DarkFrameDiffuseOpacity;
		double penColorBlend = 0.0;
	};

	inline Lighting ResolveLighting(const Material& material,
		Color penColor, double drawingPenBlend) noexcept
	{
		const double blend = ClampWeight(drawingPenBlend);
		Lighting result;
		result.penColorBlend = blend * material.penTintScale;
		result.color = MixColor(material.edgeLight, penColor, result.penColorBlend);
		result.intensity = material.lightIntensity;
		result.diffuseOpacity = Mix(material.diffuseFrameOpacity,
			material.diffusePenOpacity, blend);
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
