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
	constexpr std::array scalarMembers{ &Material::lightWeight,
		&Material::fillOpacityScale, &Material::frameOpacityScale,
		&Material::lightIntensity, &Material::cursorLightIntensityScale,
		&Material::primaryLightRadiusScale, &Material::cursorLightRadiusScale,
		&Material::penTintScale,
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
		Check(LightColor(ColorRole::SurfaceFrame) == LightColor(ColorRole::IconPrimary)
			&& LightColor(ColorRole::Divider) == LightColor(ColorRole::IconPrimary)
			&& LightColor(ColorRole::EdgeLight) == LightColor(ColorRole::IconPrimary)
			&& LightColor(ColorRole::Accent) != LightColor(ColorRole::SelectedFill),
			"flat frame, dividers and edge reflection share the graphite endpoint by role");

		const auto dark = Resolve(0.0, darkSurface, darkFrame);
		Check(dark.surface == darkSurface && dark.surfaceFrame == darkFrame
			&& dark.edgeLight == darkFrame && Near(dark.fillOpacityScale, 1.0)
			&& Near(dark.frameOpacityScale, 1.0) && Near(dark.lightIntensity, 1.0)
			&& Near(dark.cursorLightIntensityScale, 1.0)
			&& Near(dark.primaryLightRadiusScale, 1.0)
			&& Near(dark.cursorLightRadiusScale, 1.0)
			&& Near(dark.penTintScale, 1.0) && Near(dark.diffuseFrameOpacity, 0.30)
			&& Near(dark.diffusePenOpacity, 0.20) && Near(dark.keyShadowOpacity, 0.0)
			&& Near(dark.ambientShadowOpacity, 0.0) && Near(dark.highlightOpacity, 0.0),
			"folded Dark preserves the existing material and Point Light endpoint");
		const auto light = Resolve(1.0, darkSurface, darkFrame);
		Check(light.surface == LightColor(ColorRole::Surface)
			&& light.surfaceFrame == LightColor(ColorRole::IconPrimary)
			&& light.edgeLight == LightColor(ColorRole::IconPrimary)
			&& Near(0.8 * light.fillOpacityScale, 0.8)
			&& Near(light.frameOpacityScale, 1.0),
			"expanded Light preserves base opacity and uses a flat graphite frame");
		Check(Near(light.cursorLightIntensityScale, 0.85)
			&& Near(light.primaryLightRadiusScale, LightPrimaryRadiusScale)
			&& Near(light.cursorLightRadiusScale, LightCursorRadiusScale)
			&& Near(light.diffuseFrameOpacity, 0.02)
			&& Near(light.diffusePenOpacity, 0.02)
			&& Near(light.keyShadowOpacity, 0.0)
			&& Near(light.ambientShadowOpacity, 0.0)
			&& Near(light.highlightOpacity, 0.0),
			"Light uses compact edge light without raised shadows or highlight");
		Check(Near(BarMainBarFrameOpacity * light.lightIntensity, 0.18)
			&& Near(BarMainBarFrameOpacity * light.cursorLightIntensityScale, 0.153),
			"Light main bar uses visible 18% primary and 15.3% cursor edge peaks");
		Check(Near(Mix(BarButtonCursorLightIntensity,
			BarButtonLightCursorLightIntensity, 0.0), 0.30)
			&& Near(Mix(BarButtonCursorLightIntensity,
				BarButtonLightCursorLightIntensity, 1.0), 0.40),
			"selected button cursor scale keeps Dark 0.30 and Light 0.40 endpoints");
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
					const auto primary = ResolveLighting(material, pen, drawingBlend);
					const auto cursor = ResolveCursorLighting(material, pen, drawingBlend);
					Check(Near(primary.intensity, material.lightIntensity)
						&& Near(primary.penColorBlend,
							drawingBlend * material.penTintScale),
						"primary light follows the current pen role");
					Check(Near(cursor.intensity, material.lightIntensity
						* material.cursorLightIntensityScale)
						&& Near(cursor.penColorBlend,
							drawingBlend * (1.0 - material.lightWeight)),
						"cursor light continuously drops pen tint toward Light");
					Check(Near(primary.diffuseOpacity,
						material.diffuseFrameOpacity * (1.0 - drawingBlend)
							+ material.diffusePenOpacity * drawingBlend),
						"primary diffuse glow uses current material strengths");
					if (weight == 1.0)
					{
						Check(cursor.color == LightColor(ColorRole::IconPrimary)
							&& Near(cursor.penColorBlend, 0.0),
							"Light cursor reflection remains fixed graphite");
						if (drawingBlend == 1.0)
							Check(ContrastRatio(primary.color,
								LightColor(ColorRole::Surface)) >= 1.99,
								"Light primary pen color stays visible on the mist surface");
					}
				}
			}
		}
		auto material = Resolve(1.0, darkSurface, darkFrame);
		const auto reference = ResolveLighting(material, Rgb(255, 0, 0), 1.0);
		material.surfaceFrame = Rgb(0, 0, 0);
		material.surface = Rgb(255, 0, 255);
		const auto independent = ResolveLighting(material, Rgb(255, 0, 0), 1.0);
		Check(reference.color == independent.color
			&& Near(reference.intensity, independent.intensity)
			&& Near(reference.diffuseOpacity, independent.diffuseOpacity),
			"changing stored border/background cannot affect resolved primary light");
		Check(ResolveCursorLighting(material, Rgb(255, 0, 0), 1.0).color
			== ResolveCursorLighting(material, Rgb(0, 80, 255), 1.0).color,
			"Light cursor color is independent from the selected pen");
		const auto dark = Resolve(0.0, darkSurface, darkFrame);
		Check(ResolveLighting(dark, Rgb(232, 64, 0), 1.0).color == Rgb(232, 64, 0)
			&& ResolveLighting(dark, Rgb(232, 64, 0), 0.0).color == darkFrame
			&& ResolveCursorLighting(dark, Rgb(232, 64, 0), 1.0).color
				== Rgb(232, 64, 0),
			"Dark primary and cursor lights preserve their existing color endpoints");
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
