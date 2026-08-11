module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:RenderingAttribute;

import :UI;
import :State;

class BarRenderingAttribute
{
public:
	static constexpr int dirtyAntialiasPadding = 3;
	static constexpr double pointLightDiffuseExtraWidth = 6.0;

	static void UnionRectInPlace(RECT& target, const RECT& add)
	{
		// 新增矩形无效，直接返回
		if (add.left >= add.right || add.top >= add.bottom) return;
		// target 是空矩形，直接替换
		if (target.left >= target.right || target.top >= target.bottom)
		{
			target = add;
			return;
		}

		target.left = min(target.left, add.left);
		target.top = min(target.top, add.top);
		target.right = max(target.right, add.right);
		target.bottom = max(target.bottom, add.bottom);
	}

	static int GetFrameDirtyOutset(const optional<BarUiValueClass>& ft,
		BarUiFrameRenderingEnum frameRendering, double tarZoom)
	{
		// 边框外扩需要折算到设备像素，固定抗锯齿余量在矩形计算处统一追加。
		double dirtyWidth = ft.has_value() ? static_cast<double>(ft.value().val) : 0.0;
		if (frameRendering == BarUiFrameRenderingEnum::PointLight)
			dirtyWidth += pointLightDiffuseExtraWidth; // 1px 清晰边外侧再覆盖约 3px 柔光。
		return static_cast<int>(ceil(dirtyWidth * tarZoom));
	}

	static RECT GetWeigetRect(const BarUiShapeClass& shape, double tarZoom)
	{
		int ft = GetFrameDirtyOutset(shape.ft, shape.frameRendering, tarZoom) + dirtyAntialiasPadding;

		RECT ret;
		ret.left = static_cast<LONG>(floor(shape.inhX * tarZoom) - ft);
		ret.top = static_cast<LONG>(floor(shape.inhY * tarZoom) - ft);
		ret.right = static_cast<LONG>(ceil((shape.inhX + shape.w.val) * tarZoom) + ft);
		ret.bottom = static_cast<LONG>(ceil((shape.inhY + shape.h.val) * tarZoom) + ft);

		return ret;
	}
	static RECT GetWeigetRect(const BarUiSuperellipseClass& superellipse, double tarZoom)
	{
		int ft = GetFrameDirtyOutset(superellipse.ft, superellipse.frameRendering, tarZoom) + dirtyAntialiasPadding;

		RECT ret;
		ret.left = static_cast<LONG>(floor(superellipse.inhX * tarZoom) - ft);
		ret.top = static_cast<LONG>(floor(superellipse.inhY * tarZoom) - ft);
		ret.right = static_cast<LONG>(ceil((superellipse.inhX + superellipse.w.val) * tarZoom) + ft);
		ret.bottom = static_cast<LONG>(ceil((superellipse.inhY + superellipse.h.val) * tarZoom) + ft);

		return ret;
	}
	static RECT GetRotatedImageRect(double inhX, double inhY,
		double width, double height, double angle, double tarZoom,
		double contentScale = 1.0)
	{
		constexpr double pi = 3.14159265358979323846;
		if (!isfinite(angle)) angle = 0.0;
		if (!isfinite(contentScale) || contentScale <= 0.0) contentScale = 1.0;
		double radians = angle * pi / 180.0;
		double displayW = width * contentScale;
		double displayH = height * contentScale;
		double rotatedW = abs(displayW * cos(radians))
			+ abs(displayH * sin(radians));
		double rotatedH = abs(displayW * sin(radians))
			+ abs(displayH * cos(radians));
		// 旋转只改变内容，脏区至少保留控件原布局范围。
		rotatedW = max(width, rotatedW);
		rotatedH = max(height, rotatedH);
		double centerX = (inhX + width / 2.0) * tarZoom;
		double centerY = (inhY + height / 2.0) * tarZoom;

		RECT ret;
		ret.left = static_cast<LONG>(floor(centerX - rotatedW * tarZoom / 2.0)
			- dirtyAntialiasPadding);
		ret.top = static_cast<LONG>(floor(centerY - rotatedH * tarZoom / 2.0)
			- dirtyAntialiasPadding);
		ret.right = static_cast<LONG>(ceil(centerX + rotatedW * tarZoom / 2.0)
			+ dirtyAntialiasPadding);
		ret.bottom = static_cast<LONG>(ceil(centerY + rotatedH * tarZoom / 2.0)
			+ dirtyAntialiasPadding);
		return ret;
	}
	static RECT GetWeigetRect(const BarUiSVGClass& svg, double tarZoom)
	{
		return GetRotatedImageRect(svg.inhX, svg.inhY,
			svg.w.val, svg.h.val, svg.angle.val, tarZoom, svg.contentScale);
	}
	static RECT GetWeigetRect(const BarUiPNGClass& png, double tarZoom)
	{
		return GetRotatedImageRect(png.inhX, png.inhY,
			png.w.val, png.h.val, png.angle.val, tarZoom);
	}
	static RECT GetWeigetRect(const BarUiWordClass& word, double tarZoom)
	{
		RECT ret;
		ret.left = static_cast<LONG>(floor(word.inhX * tarZoom) - dirtyAntialiasPadding);
		ret.top = static_cast<LONG>(floor(word.inhY * tarZoom) - dirtyAntialiasPadding);
		ret.right = static_cast<LONG>(ceil((word.inhX + word.w.val) * tarZoom) + dirtyAntialiasPadding);
		ret.bottom = static_cast<LONG>(ceil((word.inhY + word.h.val) * tarZoom) + dirtyAntialiasPadding);
		return ret;
	}
};
