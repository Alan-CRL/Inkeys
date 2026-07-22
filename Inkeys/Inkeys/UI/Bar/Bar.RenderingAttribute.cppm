module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:RenderingAttribute;

import :UI;
import :State;

class BarRenderingAttribute
{
public:
	static constexpr int dirtyAntialiasPadding = 3;

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
		if (!ft.has_value()) return 0;

		// 边框外扩需要折算到设备像素，固定抗锯齿余量在矩形计算处统一追加。
		double dirtyWidth = ft.value().val;
		if (frameRendering == BarUiFrameRenderingEnum::PointLight)
			dirtyWidth += 2.0; // 点光模式还会绘制总宽增加 2px 的微弱扩散层。
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
	static RECT GetWeigetRect(const BarUiSVGClass& svg, double tarZoom)
	{
		RECT ret;
		ret.left = static_cast<LONG>(floor(svg.inhX * tarZoom) - dirtyAntialiasPadding);
		ret.top = static_cast<LONG>(floor(svg.inhY * tarZoom) - dirtyAntialiasPadding);
		ret.right = static_cast<LONG>(ceil((svg.inhX + svg.w.val) * tarZoom) + dirtyAntialiasPadding);
		ret.bottom = static_cast<LONG>(ceil((svg.inhY + svg.h.val) * tarZoom) + dirtyAntialiasPadding);
		return ret;
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