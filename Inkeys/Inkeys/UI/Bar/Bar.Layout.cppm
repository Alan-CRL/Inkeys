module;

#include "../../../IdtState.h"
#include <d2d1helper.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

export module Inkeys.UI.Bar:Layout;

import :UI;
import :Theme;

import Inkeys.Conv.Color;

namespace
{
	constexpr double BarBrushThicknessPresetDip[] = { 1.0, 3.0, 6.0 };
	// 荧光笔细/中/粗预设，落在 30–100 连续滑块量程内。
	constexpr double BarHighlighterThicknessPresetDip[] = { 35.0, 50.0, 70.0 };
	constexpr double BarThicknessSliderHardPenMinPx = 1.0;
	constexpr double BarThicknessSliderHardPenMaxDip = 30.0;
	constexpr double BarThicknessSliderHighlighterMinDip = 30.0;
	constexpr double BarThicknessSliderHighlighterMaxDip = 100.0;
	constexpr double BarThicknessFineDialBlankDepthDip = 5.0;
	constexpr double BarThicknessFineDialDragDepthDip = 12.0;
}

export
{
	// Main 布局与纯计算共用这些尺寸，拆分后仍只保留一个数值来源。
	inline constexpr double BarDrawAttributeExpandedWidth = 370.0;
	inline constexpr double BarDrawAttributeGap = 5.0;
	inline constexpr double BarUiDividerWidth = 1.0;
	inline constexpr double BarThicknessSliderThumbDiameter = 20.0;
	// 直接触摸 Preview 使用三倍水平行程映射粗细，降低拖动调节速度。
	inline constexpr double BarThicknessPreviewTouchDragTravelScale = 3.0;

	bool PenModeUsesCurvedThicknessPreview(PenModeSelectEnum mode)
	{
		// 未来软笔、激光笔接入实际模式枚举后，只需在这里扩展。
		return mode == PenModeSelectEnum::IdtPenBrush1;
	}

	bool PenModeSupportsAnnotationLine(PenModeSelectEnum mode)
	{
		// 激光笔不显示标注线入口；未来软笔模式在这里加入。
		return !stateMode.laserActive
			&& (mode == PenModeSelectEnum::IdtPenBrush1
			|| mode == PenModeSelectEnum::IdtPenHighlighter1);
	}

	struct BarThicknessSliderRange
	{
		int min = 0;
		int max = 0;
		bool supported = false;
	};

	BarThicknessSliderRange GetBarThicknessSliderRange(
		PenModeSelectEnum mode, double dpiZoom)
	{
		if (!isfinite(dpiZoom) || dpiZoom <= 0.0) dpiZoom = 1.0;
		if (mode == PenModeSelectEnum::IdtPenBrush1)
		{
			int maximum = max(1, static_cast<int>(lround(
				BarThicknessSliderHardPenMaxDip * dpiZoom)));
			return {
				static_cast<int>(BarThicknessSliderHardPenMinPx),
				maximum, true };
		}
		if (mode == PenModeSelectEnum::IdtPenHighlighter1)
		{
			int minimum = max(1, static_cast<int>(lround(
				BarThicknessSliderHighlighterMinDip * dpiZoom)));
			int maximum = max(minimum, static_cast<int>(lround(
				BarThicknessSliderHighlighterMaxDip * dpiZoom)));
			return { minimum, maximum, true };
		}
		return {};
	}

	double ResolveThicknessFineDialUnitTravel(
		double trackTravel, double dpiZoom)
	{
		if (!isfinite(trackTravel) || trackTravel <= 0.0) return 0.0;
		const auto brushRange = GetBarThicknessSliderRange(
			PenModeSelectEnum::IdtPenBrush1, dpiZoom);
		const double brushRangeSpan = static_cast<double>(
			brushRange.max - brushRange.min);
		if (!brushRange.supported || brushRangeSpan <= 0.0) return 0.0;
		// FineDial 的每单位行程统一以 Brush 当前 3x 精度为基准，笔型量程仍各自独立。
		return trackTravel * BarThicknessPreviewTouchDragTravelScale
			/ brushRangeSpan;
	}

	struct BarThicknessPreviewGeometry
	{
		double panelScale = 0.0;
		double previewSide = 0.0;
		double previewTop = 0.0;
		double previewBottom = 0.0;
		double previewCenterY = 0.0;
		double sliderCenterY = 0.0;
		double previewLeft = 0.0;
		double previewRight = 0.0;
		double trackLeft = 0.0;
		double trackRight = 0.0;
		bool valid = false;
	};

	struct BarThicknessFineDialGeometry
	{
		double centerX = 0.0;
		double centerY = 0.0;
		double outwardDirection = 0.0;
		double availableHalfWidth = 0.0;
		D2D1_RECT_F dialBounds{};
		D2D1_RECT_F blankZone{};
		D2D1_RECT_F dragZone{};
		D2D1_RECT_F clickZone{};
		D2D1_RECT_F ownershipCorridor{};
		bool valid = false;
	};

	BarThicknessPreviewGeometry CalculateBarThicknessPreviewGeometry(
		const BarUiShapeClass& panel,
		const BarUiShapeClass& thicknessRegion,
		const BarUiInheritClass& thicknessRegionInherit,
		const BarUiShapeClass& thicknessAdjust,
		const BarUiInheritClass& thicknessAdjustInherit)
	{
		BarThicknessPreviewGeometry geometry;
		geometry.panelScale = panel.w.val / BarDrawAttributeExpandedWidth;
		if (!isfinite(geometry.panelScale) || geometry.panelScale <= 0.0)
			return geometry;

		double regionCenterY = thicknessRegionInherit.y
			+ thicknessRegion.h.val / 2.0;
		double controlCenterY = thicknessAdjustInherit.y
			+ thicknessAdjust.h.val / 2.0;
		double scaledGap = BarDrawAttributeGap * geometry.panelScale;
		double regionTop = thicknessRegionInherit.y;
		double regionBottom = regionTop + thicknessRegion.h.val;
		double controlTop = thicknessAdjustInherit.y;
		double controlBottom = controlTop + thicknessAdjust.h.val;
		double maxControlOffset = max(0.0,
			thicknessRegion.h.val / 2.0
			- (BarUiDividerWidth * geometry.panelScale + scaledGap
				+ thicknessAdjust.h.val / 2.0));
		geometry.previewSide = maxControlOffset > 0.000001
			? clamp((regionCenterY - controlCenterY) / maxControlOffset, -1.0, 1.0)
			: 0.0;
		// 同时插值上下两组边界，换边动画中不会因控制行越过中心而跳变。
		double lowerProgress = clamp((geometry.previewSide + 1.0) / 2.0, 0.0, 1.0);
		double upperPreviewTop = regionTop;
		double upperPreviewBottom = controlTop - scaledGap;
		double lowerPreviewTop = controlBottom + scaledGap;
		double lowerPreviewBottom = regionBottom;
		geometry.previewTop = upperPreviewTop
			+ (lowerPreviewTop - upperPreviewTop) * lowerProgress;
		geometry.previewBottom = upperPreviewBottom
			+ (lowerPreviewBottom - upperPreviewBottom) * lowerProgress;
		geometry.previewCenterY =
			(geometry.previewTop + geometry.previewBottom) / 2.0;
		geometry.sliderCenterY = geometry.previewCenterY;
		// 预览区不再为外框或已移除的标注徽标预留 inset。
		geometry.previewLeft = thicknessRegionInherit.x;
		geometry.previewRight = thicknessRegionInherit.x + thicknessRegion.w.val;
		// Slider 两端与同区分割线严格对齐，不再保留旧外框的内容缩进。
		geometry.trackLeft = thicknessRegionInherit.x;
		geometry.trackRight = thicknessRegionInherit.x
			+ thicknessRegion.w.val;
		geometry.valid = geometry.previewBottom > geometry.previewTop
			&& geometry.previewRight > geometry.previewLeft
			&& geometry.trackRight > geometry.trackLeft;
		return geometry;
	}

	BarThicknessFineDialGeometry CalculateBarThicknessFineDialGeometry(
		const BarThicknessPreviewGeometry& previewGeometry,
		double sliderCenterY, double panelTop, double panelBottom)
	{
		BarThicknessFineDialGeometry geometry;
		if (!previewGeometry.valid) return geometry;

		// 换边时保留连续方向量；命中带会在中点自然收拢，Dial 本体不会跳边。
		geometry.outwardDirection = clamp(
			previewGeometry.previewSide, -1.0, 1.0);
		geometry.centerX = (previewGeometry.trackLeft
			+ previewGeometry.trackRight) / 2.0;
		geometry.centerY = previewGeometry.previewCenterY;
		geometry.availableHalfWidth = max(0.0,
			(previewGeometry.trackRight - previewGeometry.trackLeft) / 2.0);
		geometry.dialBounds = D2D1::RectF(
			static_cast<FLOAT>(previewGeometry.previewLeft),
			static_cast<FLOAT>(previewGeometry.previewTop),
			static_cast<FLOAT>(previewGeometry.previewRight),
			static_cast<FLOAT>(previewGeometry.previewBottom));

		double panelScale = previewGeometry.panelScale;
		double thumbHalf = BarThicknessSliderThumbDiameter * panelScale / 2.0;
		double ownershipNear = sliderCenterY + geometry.outwardDirection * thumbHalf;
		double dragNear = ownershipNear + geometry.outwardDirection
			* BarThicknessFineDialBlankDepthDip * panelScale;
		double dragFar = dragNear + geometry.outwardDirection
			* BarThicknessFineDialDragDepthDip * panelScale;
		double clickNear = dragFar;
		double outwardLimit = abs(geometry.outwardDirection) <= 0.000001
			? sliderCenterY : (geometry.outwardDirection > 0.0
				? panelBottom : panelTop);
		double clickFar = outwardLimit;
		auto ClampOutward = [&](double value)
			{
				return geometry.outwardDirection >= 0.0
					? min(value, outwardLimit) : max(value, outwardLimit);
			};
		ownershipNear = ClampOutward(ownershipNear);
		dragNear = ClampOutward(dragNear);
		dragFar = ClampOutward(dragFar);
		clickNear = ClampOutward(clickNear);
		clickFar = ClampOutward(clickFar);
		geometry.blankZone = D2D1::RectF(
			static_cast<FLOAT>(previewGeometry.trackLeft),
			static_cast<FLOAT>(min(ownershipNear, dragNear)),
			static_cast<FLOAT>(previewGeometry.trackRight),
			static_cast<FLOAT>(max(ownershipNear, dragNear)));
		geometry.dragZone = D2D1::RectF(
			static_cast<FLOAT>(previewGeometry.trackLeft),
			static_cast<FLOAT>(min(dragNear, dragFar)),
			static_cast<FLOAT>(previewGeometry.trackRight),
			static_cast<FLOAT>(max(dragNear, dragFar)));
		geometry.clickZone = D2D1::RectF(
			static_cast<FLOAT>(previewGeometry.trackLeft),
			static_cast<FLOAT>(min(clickNear, clickFar)),
			static_cast<FLOAT>(previewGeometry.trackRight),
			static_cast<FLOAT>(max(clickNear, clickFar)));
		// Slider 外缘到面板边界始终归本控件；空白带和折叠子区只消费。
		geometry.ownershipCorridor = D2D1::RectF(
			static_cast<FLOAT>(previewGeometry.trackLeft),
			static_cast<FLOAT>(min(ownershipNear, clickFar)),
			static_cast<FLOAT>(previewGeometry.trackRight),
			static_cast<FLOAT>(max(ownershipNear, clickFar)));
		geometry.valid = geometry.availableHalfWidth > 0.0
			&& geometry.dialBounds.right > geometry.dialBounds.left
			&& geometry.dialBounds.bottom > geometry.dialBounds.top;
		return geometry;
	}

	bool IsBarClientPointInLogicalRect(
		int clientX, int clientY, double zoom, const D2D1_RECT_F& rect)
	{
		if (!isfinite(zoom) || zoom <= 0.0
			|| rect.right <= rect.left || rect.bottom <= rect.top)
			return false;
		double logicalX = static_cast<double>(clientX) / zoom;
		double logicalY = static_cast<double>(clientY) / zoom;
		return logicalX >= rect.left && logicalX <= rect.right
			&& logicalY >= rect.top && logicalY <= rect.bottom;
	}

	int GetBarThicknessPresetPx(
		PenModeSelectEnum mode, size_t index, double dpiZoom)
	{
		if (index >= 3 || !isfinite(dpiZoom) || dpiZoom <= 0.0) return 1;
		// 预设只跟随系统 DPI，不能再叠加 UI 配置缩放。
		const double* presets = nullptr;
		if (mode == PenModeSelectEnum::IdtPenBrush1)
			presets = BarBrushThicknessPresetDip;
		else if (mode == PenModeSelectEnum::IdtPenHighlighter1)
			presets = BarHighlighterThicknessPresetDip;
		else return 1;
		return max(1, static_cast<int>(lround(presets[index] * dpiZoom)));
	}

	float GetBarLaserThicknessPresetDip(size_t index)
	{
		return index < 3 ? stateMode.Pen.Laser.widthPreset[index] : 0.0f;
	}

	int GetBarLaserThicknessPresetPx(size_t index, double dpiZoom)
	{
		if (!isfinite(dpiZoom) || dpiZoom <= 0.0) return 1;
		return max(1, static_cast<int>(lround(
			GetBarLaserThicknessPresetDip(index) * dpiZoom)));
	}

	bool IsLaserThicknessPresetMode()
	{
		return stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
			&& stateMode.laserActive;
	}

	bool PenModeUsesThicknessPresets(PenModeSelectEnum mode)
	{
		return !stateMode.laserActive
			&& (mode == PenModeSelectEnum::IdtPenBrush1
			|| mode == PenModeSelectEnum::IdtPenHighlighter1);
	}

	COLORREF GetBarReadableTextColor(COLORREF background)
	{
		auto LinearChannel = [](BYTE channel)
			{
				double value = static_cast<double>(channel) / 255.0;
				return value <= 0.04045 ? value / 12.92
					: pow((value + 0.055) / 1.055, 2.4);
			};
		double luminance = 0.2126 * LinearChannel(GetRValue(background))
			+ 0.7152 * LinearChannel(GetGValue(background))
			+ 0.0722 * LinearChannel(GetBValue(background));
		return luminance > 0.179 ? RGB(0, 0, 0) : RGB(255, 255, 255);
	}

	struct BarColorPickerHsv
	{
		double hue = 0.0;
		double saturation = 0.0;
		double value = 0.0;
	};

	BarColorPickerHsv ConvertBarColorPickerRgbToHsv(COLORREF color)
	{
		double red = static_cast<double>(GetRValue(color)) / 255.0;
		double green = static_cast<double>(GetGValue(color)) / 255.0;
		double blue = static_cast<double>(GetBValue(color)) / 255.0;
		double maximum = max(max(red, green), blue);
		double minimum = min(min(red, green), blue);
		double delta = maximum - minimum;

		BarColorPickerHsv hsv;
		hsv.value = maximum;
		hsv.saturation = maximum <= 0.0 ? 0.0 : delta / maximum;
		if (delta <= 0.000001) return hsv;
		if (maximum == red)
			hsv.hue = fmod((green - blue) / delta, 6.0) / 6.0;
		else if (maximum == green)
			hsv.hue = ((blue - red) / delta + 2.0) / 6.0;
		else hsv.hue = ((red - green) / delta + 4.0) / 6.0;
		if (hsv.hue < 0.0) hsv.hue += 1.0;
		return hsv;
	}

	COLORREF ConvertBarColorPickerHsvToRgb(
		double hue, double saturation, double value)
	{
		hue = hue - floor(hue);
		saturation = clamp(saturation, 0.0, 1.0);
		value = clamp(value, 0.0, 1.0);
		double sector = hue * 6.0;
		int index = static_cast<int>(floor(sector)) % 6;
		double fraction = sector - floor(sector);
		double p = value * (1.0 - saturation);
		double q = value * (1.0 - fraction * saturation);
		double t = value * (1.0 - (1.0 - fraction) * saturation);
		double red = 0.0, green = 0.0, blue = 0.0;
		switch (index)
		{
		case 0: red = value; green = t; blue = p; break;
		case 1: red = q; green = value; blue = p; break;
		case 2: red = p; green = value; blue = t; break;
		case 3: red = p; green = q; blue = value; break;
		case 4: red = t; green = p; blue = value; break;
		default: red = value; green = p; blue = q; break;
		}
		auto Channel = [](double channel)
			{
				return static_cast<BYTE>(clamp(
					lround(channel * 255.0), 0L, 255L));
			};
		return RGB(Channel(red), Channel(green), Channel(blue));
	}

	COLORREF GetBarColorPickerColor(double x, double y, bool darkTone,
		bool openBelowSwatch)
	{
		x = x - floor(x);
		y = clamp(y, 0.0, 1.0);
		// 黑/白端始终远离入口；上下展开时只翻转纵向色义，不翻转内容。
		double farProgress = openBelowSwatch ? y : 1.0 - y;
		return darkTone
			? ConvertBarColorPickerHsvToRgb(x, 1.0, 1.0 - farProgress)
			: ConvertBarColorPickerHsvToRgb(x, 1.0 - farProgress, 1.0);
	}

	bool ProjectBarColorPickerColor(COLORREF color, bool darkTone,
		bool openBelowSwatch, double& x, double& y, bool& exact)
	{
		BarColorPickerHsv hsv = ConvertBarColorPickerRgbToHsv(color);
		x = hsv.hue;
		double farProgress = 0.0;
		if (darkTone)
		{
			farProgress = 1.0 - hsv.value;
			exact = hsv.saturation >= 1.0 - 0.5 / 255.0
				|| hsv.value <= 0.5 / 255.0;
		}
		else
		{
			farProgress = 1.0 - hsv.saturation;
			exact = hsv.value >= 1.0 - 0.5 / 255.0;
		}
		y = openBelowSwatch ? farProgress : 1.0 - farProgress;
		return true;
	}

	bool IsBarPresetColor(COLORREF color)
	{
		for (int index = static_cast<int>(BarThemePresetColorEnum::ColorSelect1);
			index <= static_cast<int>(BarThemePresetColorEnum::ColorSelect11);
			++index)
		{
			if (Inkeys::Color::CompereColorRef(color,
				GetPresetColor(static_cast<BarThemePresetColorEnum>(index))))
				return true;
		}
		return false;
	}
}
