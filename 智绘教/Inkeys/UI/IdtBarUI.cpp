#include "IdtBarUI.h"
#include "../Load/IdtLoad.h"

#define LUNASVG_BUILD_STATIC
#include <lunasvg/lunasvg.h>
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

/// 单个 UI 值
//// 单个 SVG 组件
void BarUiSVGClass::InitializationFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	IdtLoad::ExtractResourceString(valT, resType, resName);
	InitializationFromString(valT);
}

/// 继承
//// 根据类型计算继承坐标原点
BarUiInheritClass::BarUiInheritClass(BarUiInheritEnum typeT, double xO, double yO, double wO, double hO, double xT, double yT, double wT, double hT)
{
	// w/h 为控件自身的宽高 -> 最终得出的都是左上角绘制坐标 -> 方便绘制
	// O 为当前项 T 为目标继承项

	// 继承类型
	type = typeT;

	// 基础位置
	x = xO;
	y = yO;
	// 先设置的位置为控件所设置的偏移量（也就是相对于控件中心的偏移量），接下来的计算中，以 Center 为例
	// (xT + wT / 2.0, yT + hT / 2.0) 是目标控件的中心点位置，然后在减去控件自身的宽高的一半，得到左上角的绘制坐标

	// TODO 拓展更多类型组合
	if (type == BarUiInheritEnum::TopLeft) { x += xT, y += yT; }
	else if (type == BarUiInheritEnum::Left) { x += xT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Center) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT / 2.0 - hO / 2.0; }

	else if (type == BarUiInheritEnum::ToTop) { x += xT + wT / 2.0 - wO / 2.0, y += yT - hT / 2.0 - hO; }
	else if (type == BarUiInheritEnum::ToLeft) { x += xT - wT / 2.0 - wO, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToRight) { x += xT + wT / 2.0, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToBottom) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT / 2.0; }
}
//// 继承基类
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape) { return UpInh(BarUiInheritClass(typeT, x.val, y.val, w.val, h.val, shape.inhX, shape.inhY, shape.w.val, shape.h.val)); }
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiSuperellipseClass& superellipse) { return UpInh(BarUiInheritClass(typeT, x.val, y.val, w.val, h.val, superellipse.inhX, superellipse.inhY, superellipse.w.val, superellipse.h.val)); }
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg) { return UpInh(BarUiInheritClass(typeT, x.val, y.val, w.val, h.val, svg.inhX, svg.inhY, svg.w.val, svg.h.val)); }
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiWordClass& word) { return UpInh(BarUiInheritClass(typeT, x.val, y.val, w.val, h.val, word.inhX, word.inhY, word.w.val, word.h.val)); }

/// 单个控件值
//// 单个 SVG 控件
bool BarUiSVGClass::SetWH(optional<double> wT, optional<double> hT, BarUiValueModeEnum type)
{
	double tarW, tarH;

	if (wT.has_value() && hT.has_value()) { tarW = wT.value(), tarH = hT.value(); }
	else
	{
		if (rW <= 0 || rH <= 0) return false; // 尺寸失败

		if (wT.has_value() && !hT.has_value())
		{
			// 高度自动
			tarW = wT.value();
			tarH = rH * (wT.value() / rW);
		}
		else if (!wT.has_value() && hT.has_value())
		{
			// 宽度自动
			tarW = rW * (hT.value() / rH);
			tarH = hT.value();
		}
		else
		{
			// 原尺寸
			tarW = rW;
			tarH = rH;
		}
	}

	w.Initialization(tarW, type);
	h.Initialization(tarH, type);

	return true;
}
pair<double, double> BarUiSVGClass::CalcWH()
{
	// 解析SVG
	unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.GetVal());
	if (!document) return make_pair(0, 0); // 解析失败

	double w = static_cast<double>(document->width());
	double h = static_cast<double>(document->height());
	return make_pair(w, h);
}