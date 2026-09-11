#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "../Inkeys/Inkeys/UI/Bar/Bar.ThemeMaterial.h"

import Inkeys.UI.Bar.Animation;
import Inkeys.UI.Bar.Metrics;
import Inkeys.Other.Config;

namespace
{
	using namespace BarThemeMaterial;
	int failures = 0;
	constexpr Color darkSurface = Rgb(BarDarkSurfaceColorChannel,
		BarDarkSurfaceColorChannel, BarDarkSurfaceColorChannel);
	constexpr Color darkFrame = Rgb(BarDarkSurfaceFrameColorChannel,
		BarDarkSurfaceFrameColorChannel, BarDarkSurfaceFrameColorChannel);
	constexpr std::array samples{ 0.0, 0.25, 0.5, 0.75, 1.0 };
	constexpr std::array penColors{ Rgb(0, 0, 0), Rgb(255, 255, 255),
		Rgb(255, 244, 163), Rgb(255, 16, 0), Rgb(50, 110, 217), Rgb(112, 72, 149) };
	constexpr std::array penNames{ "Black", "White", "Pale yellow", "Red", "Blue", "Custom" };
	constexpr std::array colorMembers{ &Material::surface, &Material::surfaceFrame,
		&Material::edgeLight, &Material::keyShadowColor, &Material::ambientShadowColor,
		&Material::highlightColor };
	constexpr std::array scalarMembers{ &Material::fillOpacityScale,
		&Material::frameOpacityScale, &Material::lightIntensity, &Material::penTintScale,
		&Material::diffuseFrameOpacity, &Material::diffusePenOpacity,
		&Material::keyShadowOpacity, &Material::keyShadowRadiusDip,
		&Material::keyShadowOffsetYDip, &Material::ambientShadowOpacity,
		&Material::ambientShadowRadiusDip, &Material::ambientShadowOffsetYDip,
		&Material::highlightOpacity };

	void Check(bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[BarTheme] failed: " << name << '\n';
	}

	bool Near(double left, double right)
	{
		return std::abs(left - right) <= 0.000001;
	}

	unsigned int Channel(Color color, unsigned int shift)
	{
		return (color >> shift) & 0xFFU;
	}

	bool ColorInterpolates(Color value, Color first, Color last, double weight)
	{
		for (unsigned int shift : { 0U, 8U, 16U })
		{
			const auto expected = std::lround(Channel(first, shift) * (1.0 - weight)
				+ Channel(last, shift) * weight);
			if (std::abs(static_cast<long>(Channel(value, shift)) - expected) > 1)
				return false;
		}
		return true;
	}

	bool SameMaterial(const Material& left, const Material& right)
	{
		for (const auto member : colorMembers)
			if (left.*member != right.*member) return false;
		for (const auto member : scalarMembers)
			if (!Near(left.*member, right.*member)) return false;
		return true;
	}

	void TestRolesAndEndpoints()
	{
		Check(LightColor(ColorRole::Surface) == Rgb(244, 246, 247), "cool mist-white surface");
		Check(LightColor(ColorRole::IconPrimary) == Rgb(83, 97, 106), "graphite ordinary icons");
		Check(LightColor(ColorRole::TextPrimary) == Rgb(61, 71, 77), "soft primary text");
		Check(LightColor(ColorRole::Accent) == Rgb(0, 111, 104), "deep teal selected content");
		Check(LightColor(ColorRole::SelectedFill) == Rgb(215, 238, 234), "pale teal selected plate");
		Check(LightColor(ColorRole::SurfaceFrame) != LightColor(ColorRole::IconPrimary)
			&& LightColor(ColorRole::SurfaceFrame) != LightColor(ColorRole::EdgeLight)
			&& LightColor(ColorRole::SurfaceFrame) != LightColor(ColorRole::Divider)
			&& LightColor(ColorRole::Accent) != LightColor(ColorRole::SelectedFill),
			"frame, divider, icon, edge light and selected roles remain separate");

		const auto dark = Resolve(0.0, darkSurface, darkFrame);
		Check(dark.surface == darkSurface && dark.surfaceFrame == darkFrame
			&& dark.edgeLight == darkFrame && Near(dark.fillOpacityScale, 1.0)
			&& Near(dark.frameOpacityScale, 1.0) && Near(dark.lightIntensity, 1.0)
			&& Near(dark.penTintScale, 1.0) && Near(dark.diffuseFrameOpacity, 0.30)
			&& Near(dark.diffusePenOpacity, 0.20) && Near(dark.keyShadowOpacity, 0.0)
			&& Near(dark.ambientShadowOpacity, 0.0) && Near(dark.highlightOpacity, 0.0),
			"folded Dark preserves the existing material and Point Light endpoint");
		const auto light = Resolve(1.0, darkSurface, darkFrame);
		Check(light.surface == LightColor(ColorRole::Surface)
			&& light.surfaceFrame == LightColor(ColorRole::SurfaceFrame)
			&& light.edgeLight == LightColor(ColorRole::EdgeLight)
			&& Near(0.8 * light.fillOpacityScale, 0.96)
			&& Near(light.frameOpacityScale, 3.0),
			"expanded Light uses its own frame/reflection and higher surface opacity");
		Check(light.keyShadowOpacity > 0.0 && light.keyShadowOpacity < 0.10
			&& light.ambientShadowOpacity > 0.0 && light.ambientShadowOpacity < 0.10
			&& light.keyShadowOffsetYDip > 0.0 && light.highlightOpacity > 0.0,
			"Light uses weak directional and ambient shadows with a top highlight");
		Check(Near(SelectedFillOpacity(0.0), 0.20)
			&& Near(SelectedFillOpacity(1.0), 1.0), "selected plate keeps distinct Dark/Light opacity");
	}

	void TestFullMaterialInterpolation()
	{
		const auto dark = Resolve(0.0, darkSurface, darkFrame);
		const auto light = Resolve(1.0, darkSurface, darkFrame);
		for (const double weight : samples)
		{
			const auto material = Resolve(weight, darkSurface, darkFrame);
			// 每个材质字段都参与收展，避免只改背景而最后一帧才切换光效。
			for (const auto member : colorMembers)
				Check(ColorInterpolates(material.*member, dark.*member, light.*member, weight),
					"every material RGB interpolates at quarter frames");
			for (const auto member : scalarMembers)
				Check(std::isfinite(material.*member)
					&& Near(material.*member, dark.*member * (1.0 - weight) + light.*member * weight),
					"every opacity, light strength, shadow radius and offset interpolates");
			Check(material.keyShadowRadiusDip + std::abs(material.keyShadowOffsetYDip)
				<= SurfaceShadowOutsetDip
				&& material.ambientShadowRadiusDip + std::abs(material.ambientShadowOffsetYDip)
				<= SurfaceShadowOutsetDip, "shadow draw footprint stays within shared dirty/viewport outset");
			Check(Near(SelectedFillOpacity(weight), 0.20 + 0.80 * weight),
				"selected plate changes continuously with material");
		}
		Check(SameMaterial(Resolve(-1.0, darkSurface, darkFrame), dark)
			&& SameMaterial(Resolve(2.0, darkSurface, darkFrame), light),
			"animation overshoot clamps to valid material endpoints");
		for (double invalid : { std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() })
			Check(SameMaterial(Resolve(invalid, darkSurface, darkFrame), dark)
				&& Near(ClampWeight(invalid), 0.0), "nonfinite weights have finite Dark fallback");
	}

	void TestLightSourceIndependence()
	{
		for (const double weight : samples)
		{
			const auto material = Resolve(weight, darkSurface, darkFrame);
			for (const Color pen : penColors)
			{
				for (const double drawingBlend : samples)
				{
					const auto lighting = ResolveLighting(material, pen, drawingBlend);
					Check(Near(lighting.intensity, material.lightIntensity)
						&& Near(lighting.penColorBlend, drawingBlend * material.penTintScale),
						"mouse/Point Light intensity and pen tint follow current material");
					Check(ColorInterpolates(lighting.color, material.edgeLight, pen,
						lighting.penColorBlend), "light color interpolates independently from frame");
					Check(Near(lighting.diffuseOpacity, material.diffuseFrameOpacity * (1.0 - drawingBlend)
						+ material.diffusePenOpacity * drawingBlend), "diffuse glow uses current material strengths");
					// 4% 插值端点有 double 末位误差，沿用 Near 而不是误判为过量染色。
					if (weight == 1.0)
						Check((lighting.penColorBlend <= 0.04 || Near(lighting.penColorBlend, 0.04))
							&& (lighting.intensity <= 0.45 || Near(lighting.intensity, 0.45))
							&& Channel(lighting.color, 0) >= 240
							&& Channel(lighting.color, 8) >= 242
							&& Channel(lighting.color, 16) >= 244,
							"Light reflection remains nearly white even for black/red/blue pens");
				}
			}
		}
		auto material = Resolve(1.0, darkSurface, darkFrame);
		const auto reference = ResolveLighting(material, Rgb(255, 0, 0), 1.0);
		material.surfaceFrame = Rgb(0, 0, 0);
		material.surface = Rgb(255, 0, 255);
		const auto independent = ResolveLighting(material, Rgb(255, 0, 0), 1.0);
		Check(reference.color == independent.color && Near(reference.intensity, independent.intensity)
			&& Near(reference.diffuseOpacity, independent.diffuseOpacity),
			"changing border/background roles cannot create a dark or saturated reflection");
		const auto dark = Resolve(0.0, darkSurface, darkFrame);
		Check(ResolveLighting(dark, Rgb(232, 64, 0), 1.0).color == Rgb(232, 64, 0)
			&& ResolveLighting(dark, Rgb(232, 64, 0), 0.0).color == darkFrame,
			"Dark pen and ordinary Point Light preserve their existing color endpoints");
	}

	void TestTrueAndDisplayPenColors()
	{
		const auto unchanged = penColors;
		for (const Color actual : penColors)
		{
			const Color displayed = DisplayPenColor(actual, 0.0);
			Check(DisplayPenColor(actual, 1.0) == actual,
				"Light entire pen retains actual black/white/yellow/red/blue/custom RGB");
			Check((std::max)({ Channel(displayed, 0), Channel(displayed, 8), Channel(displayed, 16) }) >= 152,
				"Dark pen remains visible on floating surface");
			for (const double weight : samples)
				Check(ColorInterpolates(DisplayPenColor(actual, weight), displayed, actual, weight),
					"pen indicator changes continuously along with surface and light");
			// 交替输入普通笔、荧光/激光的不同颜色，不能残留前一个调用的显示色。
			for (const Color otherSource : penColors)
			{
				(void)DisplayPenColor(otherSource, 0.0);
				Check(DisplayPenColor(actual, 0.0) == displayed,
					"display color has no cross-tool mutable or cached source");
			}
		}
		Check(penColors == unchanged, "display transforms never mutate actual palette inputs");
		Check(DisplayPenColor(Rgb(0, 0, 0), 0.0) == Rgb(152, 152, 152)
			&& DisplayPenColor(Rgb(255, 255, 255), 0.0) == Rgb(255, 255, 255),
			"neutral pen endpoints stay neutral without making black/white invisible");
		const auto saturated = DisplayPenColor(Rgb(50, 110, 217), 0.0);
		Check(Channel(saturated, 16) >= 232 && Channel(saturated, 16) > Channel(saturated, 8)
			&& Channel(saturated, 8) > Channel(saturated, 0), "Dark blue stays blue while brightening");
	}

	void TestAnimationReversalAndDisabled()
	{
		BarUiValueClass weight(0.0);
		Check(weight.SetTar(1.0, 1.0), "opening requests Light endpoint");
		BarUiAdvanceAnimation(weight, { 0.25, 1.0, true, false });
		const double opening = weight.val;
		const auto openingMaterial = Resolve(opening, darkSurface, darkFrame);
		Check(opening > 0.0 && opening < 1.0, "opening has an intermediate material frame");
		Check(!weight.SetTar(1.0, 1.0), "same per-frame theme target cannot restart animation");
		Check(weight.SetTar(0.0, 1.0) && Near(weight.startV, opening)
			&& SameMaterial(Resolve(weight.val, darkSurface, darkFrame), openingMaterial),
			"fast fold reverses all material fields from the current frame");
		BarUiAdvanceAnimation(weight, { 0.25, 1.0, true, false });
		const double closing = weight.val;
		Check(closing < opening && closing > 0.0, "reversed close advances toward Dark");
		const auto closingMaterial = Resolve(closing, darkSurface, darkFrame);
		weight.SetTar(1.0, 1.0);
		Check(Near(weight.startV, closing)
			&& SameMaterial(Resolve(weight.val, darkSurface, darkFrame), closingMaterial),
			"reopening during collapse preserves the complete material anchor");
		// 产品关闭动画时使用有限大倍率，让数值与共享时间轴同帧到达终点。
		constexpr double disabledSpeedRate = 1.0e12;
		const auto disabled = BarUiAdvanceAnimation(weight, { 0.01, disabledSpeedRate, false, false });
		Check(disabled.changed && !disabled.active && Near(weight.val, 1.0)
			&& SameMaterial(Resolve(weight.val, darkSurface, darkFrame), Resolve(1.0, darkSurface, darkFrame)),
			"disabled animation commits Light background, frame and light together");
		weight.SetTar(0.0, 1.0);
		BarUiAdvanceAnimation(weight, { 0.01, disabledSpeedRate, false, false });
		Check(Near(weight.val, 0.0) && weight.IsSame(), "disabled fold commits exact Dark endpoint");
		const auto idle = BarUiAdvanceAnimation(weight, { 1.0, 1.0, true, false });
		Check(!idle.changed && !idle.active, "settled material permits renderer idle");
		weight.SetDirect(1.0);
		Check(Near(weight.val, 1.0) && weight.IsSame(), "first Light frame can initialize directly without stale Dark target");
	}

	void TestThemeModeConfigPolicy()
	{
		Inkeys::Config configSnapshot;
		Check(configSnapshot.Experimental.Inkeys3.UI3.ThemeMode == Inkeys::ThemeModeDark,
			"ThemeMode schema defaults to Dark mode 1");
		Check(Inkeys::NormalizeThemeMode(Inkeys::ThemeModeDark) == Inkeys::ThemeModeDark
			&& Inkeys::NormalizeThemeMode(Inkeys::ThemeModeLight) == Inkeys::ThemeModeLight,
			"ThemeMode preserves valid Dark 1 and Light 2 values");
		for (const int invalid : { -1, 0, 3, 99 })
			Check(Inkeys::NormalizeThemeMode(invalid) == Inkeys::ThemeModeDark,
				"invalid ThemeMode values fall back to Dark mode 1");
		Check(Inkeys::ThemeModeUsesDarkStyle(Inkeys::ThemeModeDark)
			&& !Inkeys::ThemeModeUsesDarkStyle(Inkeys::ThemeModeLight)
			&& Inkeys::ThemeModeUsesDarkStyle(0),
			"ThemeMode maps uniquely to the renderer darkStyle target");

		Inkeys::Config source;
		source.Experimental.Inkeys3.UI3.ThemeMode = Inkeys::ThemeModeLight;
		configSnapshot = source;
		Check(configSnapshot.Experimental.Inkeys3.UI3.ThemeMode == Inkeys::ThemeModeLight,
			"ThemeMode Light survives the configuration snapshot codec");
		source.Experimental.Inkeys3.UI3.ThemeMode = Inkeys::ThemeModeDark;
		configSnapshot = source;
		Check(configSnapshot.Experimental.Inkeys3.UI3.ThemeMode == Inkeys::ThemeModeDark,
			"ThemeMode Dark survives the configuration snapshot codec");
	}

	std::string HexColor(Color color)
	{
		std::ostringstream value;
		value << '#' << std::hex << std::uppercase << std::setfill('0')
			<< std::setw(2) << Channel(color, 0) << std::setw(2) << Channel(color, 8)
			<< std::setw(2) << Channel(color, 16);
		return value.str();
	}
}

int RunBarThemeTests()
{
	failures = 0;
	TestRolesAndEndpoints();
	TestFullMaterialInterpolation();
	TestLightSourceIndependence();
	TestTrueAndDisplayPenColors();
	TestAnimationReversalAndDisabled();
	TestThemeModeConfigPolicy();
	return failures;
}

int RunBarThemePaletteExport(const char* outputPath)
{
	// 离屏 SVG 检查使用实际生产显示色，避免脚本另写一份提亮策略。
	std::ofstream output(outputPath, std::ios::binary);
	if (!output)
	{
		std::cerr << "[BarTheme] cannot write palette: " << outputPath << '\n';
		return 1;
	}
	output << "{\n  \"samples\": [\n";
	for (size_t index = 0; index < penColors.size(); ++index)
	{
		const auto actual = penColors[index];
		output << "    {\"label\":\"" << penNames[index] << "\",\"actual\":\""
			<< HexColor(actual) << "\",\"light\":\"" << HexColor(DisplayPenColor(actual, 1.0))
			<< "\",\"dark\":\"" << HexColor(DisplayPenColor(actual, 0.0)) << "\"}"
			<< (index + 1 == penColors.size() ? "\n" : ",\n");
	}
	output << "  ]\n}\n";
	output.close();
	return output ? 0 : 1;
}
