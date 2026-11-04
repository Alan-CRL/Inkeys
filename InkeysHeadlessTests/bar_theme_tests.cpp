#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
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

	void TestIdleOpacity()
	{
		for(double normal:{0.0,0.1,0.3,0.4,0.5,0.6,0.8,0.88,1.0})
			for(double ceiling:{IdleBackgroundOpacity,IdleContentOpacity}){
				Check(Near(ResolveIdleOpacity(normal,ceiling,0),normal),"normal endpoint preserves the configured opacity");
				Check(Near(ResolveIdleOpacity(normal,ceiling,1),(std::min)(normal,ceiling)),"idle endpoint takes an absolute ceiling");
				Check(Near(ResolveIdleOpacity(normal,ceiling,.5),(normal+(std::min)(normal,ceiling))*.5),"idle animation interpolates from normal without compounding");
			}
		Check(Near(ResolveIdleOpacity(.8,IdleContentOpacity,1),.6),"80% content dims to 60%, not 48%");
		Check(Near(ResolveIdleOpacity(.5,IdleContentOpacity,1),.5),"50% content below the idle preset stays unchanged");
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
		Check(LightColor(ColorRole::Surface) == Rgb(251, 251, 251), "neutral mist white surface");
		Check(LightColor(ColorRole::IconPrimary) == Rgb(43, 43, 43), "neutral graphite icons");
		Check(LightColor(ColorRole::TextPrimary) == Rgb(26, 26, 26), "neutral near-black primary text");
		// 中性角色全部无色相（R=G=B），避免与品牌青叠加后整体发绿。
		for (const auto role : { ColorRole::Surface, ColorRole::SurfaceFrame, ColorRole::IconPrimary,
			ColorRole::TextPrimary, ColorRole::PressedFill, ColorRole::SubtleFill, ColorRole::SwatchFrame,
			ColorRole::Divider, ColorRole::EdgeLight, ColorRole::ShadowKey, ColorRole::ShadowAmbient })
			Check(IsNeutralColor(LightColor(role)), "Light neutral roles carry no hue");
		Check(LightColor(ColorRole::Accent) == Rgb(0, 106, 99)
			&& LightColor(ColorRole::SelectedFill) == LightColor(ColorRole::Accent),
			"brand teal accent and selected plate share the Dark cyan hue");
		Check(!IsNeutralColor(LightColor(ColorRole::Accent)), "accent stays chromatic for edge light");
		// 浅色表面上的文字/图标/选中内容需保持高对比度。
		const Color surface = LightColor(ColorRole::Surface);
		Check(ContrastRatio(LightColor(ColorRole::TextPrimary), surface) >= 16.0
			&& ContrastRatio(LightColor(ColorRole::IconPrimary), surface) >= 13.0
			&& ContrastRatio(LightColor(ColorRole::Accent), surface) >= 4.5,
			"Light content roles stay high contrast on the surface");
		{
			// 预混中性灰在对象原有透明度下的黑色观感：外框约 8%、分隔线约 10%、悬停约 4.5%。
			const auto share = [&](ColorRole role, double opacity) {
				const double c = static_cast<double>(LightColor(role) & 0xFFU);
				return opacity * (251.0 - c) / 251.0;
			};
			Check(std::abs(share(ColorRole::SurfaceFrame, BarMainBarFrameOpacity) - 0.08) < 0.005
				&& std::abs(share(ColorRole::Divider, 0.30) - 0.10) < 0.005
				&& std::abs(share(ColorRole::PressedFill, BarButtonHoverOpacity) - 0.045) < 0.005
				&& std::abs(share(ColorRole::SwatchFrame, 0.18) - 0.18) < 0.005,
				"pre-mixed neutral overlays produce the designed black shares");
		}
		Check(LightColor(ColorRole::Divider) != LightColor(ColorRole::IconPrimary),
			"dividers keep their own lighter gray role");

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
			&& light.surfaceFrame == LightColor(ColorRole::SurfaceFrame)
			&& light.edgeLight == LightColor(ColorRole::EdgeLight)
			&& Near(light.fillOpacityScale, 1.0)
			&& Near(light.frameOpacityScale, 1.0)
			&& Near(FrameOpacity(1.0, BarMainBarFrameOpacity), BarMainBarFrameOpacity)
			&& Near(FrameOpacity(0.0, BarMainBarFrameOpacity), BarMainBarFrameOpacity),
			"expanded Light keeps a flat 1 DIP frame whose 8% black comes from the pre-mixed color");
		Check(Near(BarMainBarFillOpacity * LightResidentSurfaceOpacityScale, 0.92),
			"resident Light surfaces are more opaque than the Dark 80% endpoint");
		Check(Near(DisabledContentOpacity(0.0), 0.30)
			&& Near(DisabledContentOpacity(1.0), 0.45)
			&& Near(DisabledContentOpacity(0.5), 0.375),
			"disabled content keeps Dark 0.30 and stays recognizable at Light 0.45");
		{
			// 常驻浅色表面压在深色课件上时仍需可读：图标与选中内容均不低于 4.5:1。
			const Color darkBackground = Rgb(30, 30, 30);
			const Color resident = MixColor(darkBackground, LightColor(ColorRole::Surface),
				BarMainBarFillOpacity * LightResidentSurfaceOpacityScale);
			const Color selectedPlate = MixColor(resident, LightColor(ColorRole::SelectedFill),
				SelectedFillOpacity(1.0));
			Check(ContrastRatio(LightColor(ColorRole::IconPrimary), resident) >= 7.0
				&& ContrastRatio(LightColor(ColorRole::Accent), selectedPlate) >= 4.5,
				"Light resident surface stays readable over a dark background");
		}
		Check(Near(light.cursorLightIntensityScale, 0.85)
			&& Near(light.primaryLightRadiusScale, LightPrimaryRadiusScale)
			&& Near(light.cursorLightRadiusScale, LightCursorRadiusScale)
			&& Near(light.diffuseFrameOpacity, LightFrameDiffuseOpacity)
			&& Near(light.diffusePenOpacity, LightPenDiffuseOpacity)
			&& light.diffuseFrameOpacity < dark.diffuseFrameOpacity
			&& Near(light.keyShadowOpacity, 0.0)
			&& Near(light.ambientShadowOpacity, LightAmbientShadowOpacity)
			&& light.ambientShadowColor == Rgb(0, 0, 0)
			&& Near(light.highlightOpacity, 0.0),
			"Light uses one soft neutral ambient shadow, no key ring or top highlight");
		Check(Near(light.lightIntensity, 1.0)
			&& Near(BarMainBarFrameOpacity * light.lightIntensity
				* light.cursorLightIntensityScale, 0.153),
			"Light edge light matches Dark strength instead of a brighter gray glow");
		Check(Near(Mix(BarButtonCursorLightIntensity,
			BarButtonLightCursorLightIntensity, 0.0), 0.30)
			&& Near(Mix(BarButtonCursorLightIntensity,
				BarButtonLightCursorLightIntensity, 1.0), 0.40),
			"selected button cursor scale keeps Dark 0.30 and Light 0.40 endpoints");
		Check(Near(SelectedFillOpacity(0.0), 0.20)
			&& Near(SelectedFillOpacity(1.0), 0.10)
			&& Near(SelectedFillOpacity(1.0, BarButtonPressedOpacity), 0.05),
			"selected plate stays a translucent accent tint at both endpoints");
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
			Check(Near(SelectedFillOpacity(weight), 0.20 - 0.10 * weight),
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
						&& Near(cursor.penColorBlend, drawingBlend),
						"cursor light follows the pen color at both endpoints like Dark");
					Check(Near(primary.diffuseOpacity,
						material.diffuseFrameOpacity * (1.0 - drawingBlend)
							+ material.diffusePenOpacity * drawingBlend),
						"primary diffuse glow uses current material strengths");
					if (weight == 1.0)
					{
						Check(cursor.color == primary.color,
							"Light cursor and primary lights share the frame/pen color rule");
						if (drawingBlend == 0.0)
							Check(primary.color == material.edgeLight,
								"Light non-drawing light stays the neutral edge gray");
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
			!= ResolveCursorLighting(material, Rgb(0, 80, 255), 1.0).color,
			"Light cursor color follows the selected pen like Dark");
		{
			// renderer 以实际边框色覆盖 edgeLight：选中按钮为主题青，分隔线为灰。
			auto selected = Resolve(1.0, darkSurface, darkFrame);
			selected.edgeLight = LightColor(ColorRole::Accent);
			auto divider = selected;
			divider.edgeLight = LightColor(ColorRole::Divider);
			Check(ResolveLighting(selected, Rgb(255, 0, 0), 0.0).color == LightColor(ColorRole::Accent)
				&& ResolveCursorLighting(selected, Rgb(255, 0, 0), 0.0).color == LightColor(ColorRole::Accent)
				&& ResolveLighting(divider, Rgb(255, 0, 0), 0.0).color == LightColor(ColorRole::Divider),
				"Light edge light takes each object's own resolved edge color");
			// renderer 的对象边缘光：有色边框保持自身，中性边框浅色端换成 EdgeLight，深色端不变。
			Check(ObjectEdgeLight(1.0, LightColor(ColorRole::Accent)) == LightColor(ColorRole::Accent)
				&& ObjectEdgeLight(1.0, LightColor(ColorRole::SurfaceFrame)) == LightColor(ColorRole::EdgeLight)
				&& ObjectEdgeLight(1.0, LightColor(ColorRole::SwatchFrame)) == LightColor(ColorRole::EdgeLight)
				&& ObjectEdgeLight(0.0, darkFrame) == darkFrame
				&& ObjectEdgeLight(0.0, Rgb(88, 255, 236)) == Rgb(88, 255, 236),
				"neutral frames use the neutral edge gray on Light only");
		}
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

	void TestResidentOpacityPolicy()
	{
		// 配置：背景 0 = 跟随主题默认，其余与内容透明度收敛到 30~100。
		Inkeys::Config configSnapshot;
		Check(configSnapshot.Experimental.Inkeys3.UI3.BackgroundOpacity
				== Inkeys::ResidentBackgroundOpacityThemeDefault
			&& configSnapshot.Experimental.Inkeys3.UI3.ContentOpacity
				== Inkeys::ResidentContentOpacityDefault,
			"resident opacity schema defaults to theme background and opaque content");
		Check(Inkeys::NormalizeResidentBackgroundOpacity(0) == 0
			&& Inkeys::NormalizeResidentBackgroundOpacity(-5) == 30
			&& Inkeys::NormalizeResidentBackgroundOpacity(12) == 30
			&& Inkeys::NormalizeResidentBackgroundOpacity(64) == 64
			&& Inkeys::NormalizeResidentBackgroundOpacity(140) == 100,
			"background opacity keeps the theme-default sentinel and clamps to 30~100");
		Check(Inkeys::NormalizeResidentContentOpacity(0) == 30
			&& Inkeys::NormalizeResidentContentOpacity(75) == 75
			&& Inkeys::NormalizeResidentContentOpacity(101) == 100,
			"content opacity clamps to 30~100");

		// 渲染：默认值保持主题端点；用户值为展开主栏的绝对透明度，其余常驻表面按比例同步。
		Check(Near(DefaultResidentFillOpacity(0.0), 0.80)
			&& Near(DefaultResidentFillOpacity(1.0), 0.92),
			"theme default resident background is Dark 80% and Light 92%");
		for (const double weight : samples)
			Check(Near(ResidentFillOpacityScale(weight, LightResidentSurfaceOpacityScale, 0.0),
				Mix(1.0, LightResidentSurfaceOpacityScale, weight)),
				"resident fill follows the theme scale when no override is set");
		for (const double weight : samples)
		{
			Check(Near(BarMainBarFillOpacity
					* ResidentFillOpacityScale(weight, LightResidentSurfaceOpacityScale, 0.30), 0.30)
				&& Near(BarMainBarFillOpacity
					* ResidentFillOpacityScale(weight, LightResidentSurfaceOpacityScale, 1.0), 1.0),
				"user background opacity is absolute for the expanded main bar at every material weight");
			Check(Near(BarMainButtonCollapsedFillOpacity
					* ResidentFillOpacityScale(weight, LightResidentSurfaceOpacityScale, 0.40),
				0.40 * BarMainButtonCollapsedFillOpacity / BarMainBarFillOpacity),
				"collapsed resident surfaces keep their ratio to the main bar");
		}
		Check(Near(ResidentFillOpacityScale(1.0, LightResidentSurfaceOpacityScale,
				std::numeric_limits<double>::quiet_NaN()), LightResidentSurfaceOpacityScale),
			"invalid override falls back to the theme default");
	}

	void TestStaticAppearancePresentContract()
	{
		// Headless 不运行主栏渲染循环：源级检查不经动画的外观变化（用户透明度、主题落地）
		// 除了标记损伤，还必须要求一次视觉呈现，否则静止主栏不会重画。
		namespace fs = std::filesystem;
		const fs::path relative = "Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp";
		fs::path source = fs::path(__FILE__).parent_path().parent_path() / relative;
		if (!fs::exists(source))
			for (auto root = fs::current_path(); !root.empty(); root = root.parent_path())
			{
				if (fs::exists(root / relative)) { source = root / relative; break; }
				if (root == root.parent_path()) break;
			}
		std::ifstream file(source, std::ios::binary);
		const std::string code((std::istreambuf_iterator<char>(file)), {});
		const auto opacity = code.find("state.barStyle.backgroundOpacity = requestedBackground;");
		const auto opacityEnd = code.find('}', opacity);
		Check(opacity != std::string::npos && opacityEnd != std::string::npos
			&& code.substr(opacity, opacityEnd - opacity).find("presentDecision.RequireVisualRetry()") != std::string::npos,
			"user resident opacity change requests a visual present for a static bar");
		const auto theme = code.find("state.barStyle.darkStyle = requestedDarkStyle;");
		const auto themeEnd = code.find("requestedBackgroundOpacity.load", theme);
		Check(theme != std::string::npos && themeEnd != std::string::npos
			&& code.substr(theme, themeEnd - theme).find("presentDecision.RequireVisualRetry()") != std::string::npos,
			"applied theme change requests a visual present even with animation disabled");
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
	TestResidentOpacityPolicy();
	TestIdleOpacity();
	TestStaticAppearancePresentContract();
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
