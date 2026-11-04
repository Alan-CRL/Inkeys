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

	// 「中性雾白」浅色：中性色全部无色相（R=G=B），与设置窗口的 WinUI 3 浅色一致；
	// 品牌青与深色 #58FFEC 同色相，只用于选中内容、选中底和选中按钮的边缘光。
	// 边框/分隔线/悬停底为预混中性灰：按 C = 251 - 目标黑色占比 × 251 / 对象透明度 反算，
	// 在对象原有透明度（边框 18%、分隔线 30%、悬停 18%）下得到约 8%/10%/4.5% 的黑色观感，
	// 不依赖材质系统的独立面板（橡皮、撤销、分页）通过 GetThemeSurfaceFrameOpacity 取得同一结果。
	constexpr Color LightColor(ColorRole role) noexcept
	{
		switch (role)
		{
		case ColorRole::Surface: return Rgb(251, 251, 251);
		case ColorRole::SurfaceFrame: return Rgb(139, 139, 139);
		case ColorRole::IconPrimary: return Rgb(43, 43, 43);
		case ColorRole::TextPrimary: return Rgb(26, 26, 26);
		// 选中内容压在同色 10% 底上，深色课件背景下仍不低于 4.5:1。
		case ColorRole::Accent: return Rgb(0, 106, 99);
		case ColorRole::SelectedFill: return Rgb(0, 106, 99);
		case ColorRole::PressedFill: return Rgb(188, 188, 188);
		case ColorRole::SubtleFill: return Rgb(225, 225, 225);
		// 色块边框以 18% 透明度绘制，纯黑即约 18% 观感，白色色块也能读出轮廓。
		case ColorRole::SwatchFrame: return Rgb(0, 0, 0);
		case ColorRole::Divider: return Rgb(167, 167, 167);
		// 浅色边缘光与边框解耦：边框只有 8% 黑，直接作光色在雾白底上不可见，改用中性深灰。
		case ColorRole::EdgeLight: return Rgb(58, 58, 58);
		case ColorRole::ShadowKey: return Rgb(0, 0, 0);
		case ColorRole::ShadowAmbient: return Rgb(0, 0, 0);
		case ColorRole::TopHighlight: return Rgb(255, 255, 255);
		case ColorRole::DockTarget: return Rgb(0, 120, 212);
		default: return Rgb(26, 26, 26);
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

	// 浅色选中底与深色同为半透明主题色，只按比例降低（0.20 → 0.10），避免实色卡片压住背景。
	inline constexpr double LightSelectedFillOpacityScale = 0.50;
	// 主栏、主按钮和分页这类常驻表面在浅色端更实：半透明浅底压在深色背景上会混成脏灰，
	// 0.80 × 1.15 = 0.92；弹出面板不受影响。
	inline constexpr double LightResidentSurfaceOpacityScale = 1.15;
	// 浅色外框由预混色直接给出约 8% 黑，配合单圈柔和投影读出外缘，不再加深透明度。
	inline constexpr double LightFrameOpacityScale = 1.0;
	// 浅色底上 0.30 的禁用内容只剩约 1.5:1，提高到约 2:1 仍明显区别于可用态。
	inline constexpr double DarkDisabledContentOpacity = 0.30;
	inline constexpr double LightDisabledContentOpacity = 0.45;

	// 主栏展开时的 Dark 常驻填充透明度；其他常驻表面按与它的比例同步缩放。
	inline constexpr double ResidentBaseFillOpacity = 0.80;
	inline constexpr double IdleBackgroundOpacity = 0.30;
	inline constexpr double IdleContentOpacity = 0.60;

	// 闲置只限制当前正常值，不再次乘用户透明度，也不把本来更淡的材质加深。
	inline double ResolveIdleOpacity(double normal, double ceiling, double progress) noexcept
	{
		normal = ClampWeight(normal);
		return Mix(normal, (std::min)(normal, ceiling), progress);
	}

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
	// 浅色边缘光为中性深灰，与深色同强度，避免灰光发脏、刺眼。
	inline constexpr double LightEdgeLightIntensity = 1.0;
	// 浅色单圈柔和投影：只用 ambient 一层，全周等宽外扩，下移量很小以免一侧发粗；
	// 半径 + 下移不超过 SurfaceShadowOutsetDip，沿用既有窗口/dirty 外扩，不改布局。
	inline constexpr double LightAmbientShadowOpacity = 0.08;
	inline constexpr double LightAmbientShadowRadiusDip = 6.0;
	inline constexpr double LightAmbientShadowOffsetYDip = 2.0;
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
		// 边缘光：Dark 取边框色，Light 端中性对象改用 EdgeLight 深灰（见 ObjectEdgeLight），
		// 选中按钮等有色边框仍取自身颜色；绘制时再向笔色过渡。
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
		result.ambientShadowOpacity = Mix(0.0, LightAmbientShadowOpacity, weight);
		result.ambientShadowRadiusDip = Mix(0.0, LightAmbientShadowRadiusDip, weight);
		result.ambientShadowOffsetYDip = Mix(0.0, LightAmbientShadowOffsetYDip, weight);
		result.highlightColor = LightColor(ColorRole::TopHighlight);
		result.highlightOpacity = 0.0;
		return result;
	}

	inline bool IsNeutralColor(Color color) noexcept
	{
		const auto r = color & 0xFFU, g = (color >> 8) & 0xFFU, b = (color >> 16) & 0xFFU;
		return r == g && g == b;
	}

	// 对象边缘光基色：有色边框（选中按钮的主题青）保持自身颜色；中性边框在浅色端
	// 向 EdgeLight 深灰过渡，Dark 端仍为边框色本身。
	inline Color ObjectEdgeLight(double lightWeight, Color drawnFrame) noexcept
	{
		if (!IsNeutralColor(drawnFrame)) return drawnFrame;
		return MixColor(drawnFrame, LightColor(ColorRole::EdgeLight), lightWeight);
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
