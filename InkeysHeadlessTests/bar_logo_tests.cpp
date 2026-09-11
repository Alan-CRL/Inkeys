#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "../Inkeys/Inkeys/UI/Bar/Bar.LogoAppearance.h"

import Inkeys.UI.Bar.Animation;

namespace
{
	using namespace BarLogoAppearance;
	int failures = 0;
	constexpr std::array weights{ 0.0, 0.25, 0.5, 0.75, 1.0 };
	constexpr std::array<COLORREF, 6> colors{ RGB(0, 0, 0), RGB(255, 255, 255),
		RGB(255, 244, 163), RGB(255, 16, 0), RGB(50, 110, 217), RGB(112, 72, 149) };
	constexpr std::array names{ "Black", "White", "Pale yellow", "Red", "Blue", "Custom" };
	using Attributes = std::map<std::pair<std::string, std::string>, std::string>;

	void Check(bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[BarLogo] failed: " << name << '\n';
	}

	bool Near(double left, double right)
	{
		return std::abs(left - right) <= 0.000001;
	}

	std::string Hex(COLORREF color)
	{
		std::ostringstream result;
		result << '#' << std::hex << std::uppercase << std::setfill('0')
			<< std::setw(2) << static_cast<int>(GetRValue(color))
			<< std::setw(2) << static_cast<int>(GetGValue(color))
			<< std::setw(2) << static_cast<int>(GetBValue(color));
		return result.str();
	}

	std::string CssRgb(COLORREF color)
	{
		return "rgb(" + std::to_string(GetRValue(color)) + ","
			+ std::to_string(GetGValue(color)) + "," + std::to_string(GetBValue(color)) + ")";
	}

	void TestDrawingGate()
	{
		struct Mode { const char* name; bool pen; bool shape; bool drawing; };
		constexpr std::array modes{ Mode{ "Pen", true, false, true },
			Mode{ "Shape", false, true, true }, Mode{ "Selection", false, false, false },
			Mode{ "Eraser", false, false, false } };
		for (const auto& mode : modes)
			for (const double light : weights)
				for (const COLORREF color : colors)
				{
					const bool drawing = IsDrawingMode(mode.pen, mode.shape);
					Check(drawing == mode.drawing, mode.name);
					if (!drawing)
						Check(Resolve(light, drawing ? 1.0 : 0.0, color)
							== Resolve(light, 0.0, colors[0]),
							"Selection and Eraser always resolve to neutral appearance");
				}
	}
	Attributes Collect(const Snapshot& appearance)
	{
		Attributes attributes;
		ApplyAttributes(appearance, [&](const char* id, const char* name, const std::string& value)
		{
			Check(attributes.emplace(std::make_pair(id, name), value).second,
				"each SVG property is written once");
		});
		return attributes;
	}

	void TestAppearanceMatrix()
	{
		for (const double light : weights)
		{
			const auto neutral = Resolve(light, 0.0, colors[0]);
			for (const COLORREF color : colors)
			{
				Check(Resolve(light, 0.0, color) == neutral,
					"non-drawing appearance does not remember any previous pen RGB");
				for (const double drawing : weights)
				{
					const auto appearance = Resolve(light, drawing, color);
					Check(Near(appearance.indicatorOpacity, drawing * (1.0 - light)),
						"original Dark indicator fades by both current material and drawing weights");
					Check(Near(appearance.outlineOpacity, 0.85 * light),
						"whole outline follows current Light material");
					Check(appearance.screenFill == neutral.screenFill
						&& Near(appearance.screenOpacity, neutral.screenOpacity),
						"base screen is independent of drawing state and pen RGB");
					if (light == 0.0)
						Check(appearance.penFill == RGB(255, 255, 255)
							&& appearance.screenFill == RGB(255, 255, 255)
							&& Near(appearance.screenOpacity, 0.22),
							"Dark retains original white pen and 22 percent screen beneath gradients");
					if (light == 1.0 && drawing == 1.0)
						Check(appearance.penFill == color, "Light drawing uses exact actual RGB without brightening");
					if (appearance.indicatorOpacity > 0.0)
						Check(appearance.indicatorColor == color, "Dark gradient/highlight use unmodified actual RGB");
					const auto attributes = Collect(appearance);
					for (const char* id : { "pen-cap", "pen-barrel", "pen-tip", "pen-nib" })
						Check(attributes.at({ id, "fill" }) == CssRgb(appearance.penFill),
							"all four production pen fills consume the resolved appearance");
					Check(Near(std::stod(attributes.at({ "ink-indicator", "opacity" })),
						appearance.indicatorOpacity), "SVG gradient group consumes current indicator opacity");
				}
			}
		}
		const auto lightNeutral = Resolve(1.0, 0.0, colors[0]);
		Check(lightNeutral.penFill == RGB(83, 97, 106)
			&& lightNeutral.screenFill == RGB(186, 195, 200)
			&& Near(lightNeutral.screenOpacity, 0.65), "Light non-drawing defaults use neutral theme colors");
		for (const double invalid : { std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() })
		{
			Check(Resolve(invalid, 1.0, colors[3]) == Resolve(0.0, 1.0, colors[3]),
				"invalid material has finite Dark fallback");
			Check(Resolve(0.5, invalid, colors[3]) == Resolve(0.5, 0.0, colors[3]),
				"invalid drawing weight has finite neutral fallback");
		}
		Check(Resolve(-1.0, 2.0, colors[3]) == Resolve(0.0, 1.0, colors[3]),
			"animation overshoot clamps to valid appearance");
	}

	void TestAnimationContinuity()
	{
		// 直接推进产品 Value/Pct/Color；测试不复制曲线，也不重置反向瞬间的 current。
		BarUiValueClass material(0.0);
		BarUiPctClass drawing(0.0);
		BarUiColorClass pen(colors[3]);
		material.SetTar(1.0, 1.0);
		drawing.SetTar(1.0, 1.0);
		pen.SetTar(colors[4], 1.0);
		const BarUiAnimationAdvanceContextClass tick{ 0.25, 1.0, true, false };
		BarUiAdvanceAnimation(material, tick);
		BarUiAdvanceAnimation(drawing, tick);
		BarUiAdvanceAnimation(pen, tick);
		const auto current = Resolve(material.val, drawing.val, pen.val);
		Check(material.val > 0.0 && material.val < 1.0 && drawing.val > 0.0
			&& drawing.val < 1.0 && pen.val != colors[3] && pen.val != colors[4],
			"theme, drawing and color have actual intermediate frames");
		Check(!material.SetTar(1.0, 1.0) && !drawing.SetTar(1.0, 1.0)
			&& !pen.SetTar(colors[4], 1.0), "repeated frame targets do not restart transitions");
		material.SetTar(0.0, 1.0);
		drawing.SetTar(0.0, 1.0);
		pen.SetTar(colors[5], 1.0);
		Check(Resolve(material.val, drawing.val, pen.val) == current
			&& Near(material.startV, material.val) && Near(drawing.startV, drawing.val),
			"reversing theme/mode and changing pen preserves the current appearance");
		BarUiAdvanceAnimation(material, tick);
		BarUiAdvanceAnimation(drawing, tick);
		BarUiAdvanceAnimation(pen, tick);
		const auto reversed = Resolve(material.val, drawing.val, pen.val);
		Check(reversed != current, "reversed transitions advance the actual SVG appearance");
		material.SetTar(1.0, 1.0);
		drawing.SetTar(1.0, 1.0);
		Check(Resolve(material.val, drawing.val, pen.val) == reversed,
			"reopening and reentering drawing preserve their partially reversed frame");
		// 与生产关闭动画的有限倍率一致，避免虚构不同的动画语义。
		const BarUiAnimationAdvanceContextClass disabled{ 0.01, 1.0e12, false, false };
		BarUiAdvanceAnimation(material, disabled);
		BarUiAdvanceAnimation(drawing, disabled);
		BarUiAdvanceAnimation(pen, disabled);
		Check(Near(material.val, 1.0) && Near(drawing.val, 1.0) && pen.val == colors[5]
			&& Resolve(material.val, drawing.val, pen.val) == Resolve(1.0, 1.0, colors[5]),
			"disabled animation reaches consistent Light drawing/color endpoint");
		drawing.SetTar(0.0, 1.0);
		BarUiAdvanceAnimation(drawing, disabled);
		Check(Resolve(material.val, drawing.val, pen.val) == Resolve(1.0, 0.0, colors[0]),
			"leaving drawing reaches a neutral endpoint regardless of animated pen history");
		const auto materialIdle = BarUiAdvanceAnimation(material, tick);
		const auto drawingIdle = BarUiAdvanceAnimation(drawing, tick);
		const auto colorIdle = BarUiAdvanceAnimation(pen, tick);
		Check(!materialIdle.active && !materialIdle.changed && !drawingIdle.active
			&& !drawingIdle.changed && !colorIdle.active && !colorIdle.changed,
			"all appearance channels settle and permit idle");
	}
}

int RunBarLogoTests()
{
	failures = 0;
	TestDrawingGate();
	TestAppearanceMatrix();
	TestAnimationContinuity();
	return failures;
}

int RunBarLogoProfileExport(const char* outputPath)
{
	// 导出真正应用到文档的属性；离屏脚本只执行这些属性，不再实现外观公式。
	std::ofstream output(outputPath, std::ios::binary);
	if (!output) return 1;
	output << "{\n\"samples\":[\n";
	bool firstSample = true;
	for (size_t colorIndex = 0; colorIndex < colors.size(); ++colorIndex)
		for (size_t lightIndex = 0; lightIndex < weights.size(); ++lightIndex)
			for (size_t drawingIndex = 0; drawingIndex < weights.size(); ++drawingIndex)
			{
				if (!firstSample) output << ",\n";
				firstSample = false;
				const auto attributes = Collect(Resolve(weights[lightIndex], weights[drawingIndex], colors[colorIndex]));
				output << "{\"id\":\"c" << colorIndex << "-l" << lightIndex << "-d" << drawingIndex
					<< "\",\"label\":\"" << names[colorIndex] << "\",\"actual\":\"" << Hex(colors[colorIndex])
					<< "\",\"light\":" << weights[lightIndex] << ",\"drawing\":" << weights[drawingIndex]
					<< ",\"attributes\":[";
				bool firstAttribute = true;
				for (const auto& [key, value] : attributes)
				{
					if (!firstAttribute) output << ',';
					firstAttribute = false;
					output << "[\"" << key.first << "\",\"" << key.second << "\",\"" << value << "\"]";
				}
				output << "]}";
			}
	output << "\n]}\n";
	output.close();
	return output && failures == 0 ? 0 : 1;
}
