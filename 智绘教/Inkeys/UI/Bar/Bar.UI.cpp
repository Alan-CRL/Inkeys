module;

#include "../../../IdtMain.h"

#include "../../../IdtD2DPreparation.h"
#include "../../../IdtText.h"

#define LUNASVG_BUILD_STATIC
#include <lunasvg/lunasvg.h>
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

module Inkeys.UI.Bar;
import :UI;

import Inkeys.Load;

/// 继承
//// 位置继承
BarUiInheritClass::BarUiInheritClass(double xT, double yT)
{
	x = xT;
	y = yT;
}
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
	else if (type == BarUiInheritEnum::Top) { x += xT + wT / 2.0 - wO / 2.0, y += yT; }
	else if (type == BarUiInheritEnum::Left) { x += xT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Center) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Right) { x += xT + wT - wO, y += yT + hT / 2.0 - hO / 2.0; }

	else if (type == BarUiInheritEnum::ToTop) { x += xT + wT / 2.0 - wO / 2.0, y += yT - hO; }
	else if (type == BarUiInheritEnum::ToLeft) { x += xT - wO, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToRight) { x += xT + wT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToBottom) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT; }
}

/// 控件
//// 单个形状控件
BarUiShapeClass::BarUiShapeClass(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (rwT.has_value()) { rw = BarUiValueClass(); rw.value().Initialization(rwT.value(), type); }
	if (rhT.has_value()) { rh = BarUiValueClass(); rh.value().Initialization(rhT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
void BarUiShapeClass::Initialization(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (rwT.has_value()) { rw = BarUiValueClass(); rw.value().Initialization(rwT.value(), type); }
	if (rhT.has_value()) { rh = BarUiValueClass(); rh.value().Initialization(rhT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
bool BarUiShapeClass::IsClick(int mx, int my, double zoom, double epsilon)
{
	// 保证有效参数
	if (w.val <= 0.0 || h.val <= 0.0 || (rw.has_value() && rw.value().val < 0.0) || (rh.has_value() && rh.value().val < 0.0)) return false;

	// 初始化值
	double xO = inhX * zoom;
	double yO = inhY * zoom;

	// 折算为半径
	double rx = 0.0;
	double ry = 0.0;
	if (rw.has_value()) rx = clamp(rw.value().val * zoom, 0.0, (w.val * zoom) / 2.0);
	if (rh.has_value()) ry = clamp(rh.value().val * zoom, 0.0, (h.val * zoom) / 2.0);

	// 矩形区域内才有可能
	if (static_cast<double>(mx) < xO - epsilon || static_cast<double>(mx) > xO + (w.val * zoom) + epsilon ||
		static_cast<double>(my) < yO - epsilon || static_cast<double>(my) > yO + (h.val * zoom) + epsilon)
		return false;

	// “内矩形”范围
	double ix0 = xO + rx;         // 内部矩形左
	double ix1 = xO + (w.val * zoom) - rx; // 内部矩形右
	double iy0 = yO + ry;         // 上
	double iy1 = yO + h.val * zoom - ry; // 下

	// 若点在内矩形，直接返回
	if (static_cast<double>(mx) >= ix0 - epsilon && static_cast<double>(mx) <= ix1 + epsilon &&
		static_cast<double>(my) >= iy0 - epsilon && static_cast<double>(my) <= iy1 + epsilon)
		return true;

	// 否则一定在四角矩形外或圆角四象限内，枚举距离最近的圆角中心
	// Clamp到最近的角
	double cx = (static_cast<double>(mx) < ix0) ? ix0 : ((static_cast<double>(mx) > ix1) ? ix1 : static_cast<double>(mx));
	double cy = (static_cast<double>(my) < iy0) ? iy0 : ((static_cast<double>(my) > iy1) ? iy1 : static_cast<double>(my));

	// 对应的圆角中心
	// 只有在圆角四象限判定，否则前面矩形部分已经返回true
	double dx = static_cast<double>(mx) - cx;
	double dy = static_cast<double>(my) - cy;

	// 椭圆(中心0,0, 半径rx,ry)上的判定
	// (dx/rx)^2 + (dy/ry)^2 <= 1

	if (rx > 0 && ry > 0)
	{
		double ellipseVal = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
		return ellipseVal <= 1.0 + epsilon;
	}
	// 若rx或ry为0，则为直角矩形，已在前面矩形段判断过
	return false;
}

//// 单个超椭圆控件
BarUiSuperellipseClass::BarUiSuperellipseClass(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (nT.has_value()) { n = BarUiValueClass(); n.value().Initialization(nT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
void BarUiSuperellipseClass::Initialization(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (nT.has_value()) { n = BarUiValueClass(); n.value().Initialization(nT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
bool BarUiSuperellipseClass::IsClick(int mx, int my, double zoom, double epsilon)
{
	// 计算中心
	double cx = inhX * zoom + w.val * zoom / 2.0;
	double cy = inhY * zoom + h.val * zoom / 2.0;
	// 半轴
	double a = w.val * zoom / 2.0;
	double b = h.val * zoom / 2.0;

	// 映射到中心
	double normx = (static_cast<double>(mx) - cx) / a;
	double normy = (static_cast<double>(my) - cy) / b;

	// degenerate
	if (a <= 0 || b <= 0 || (!n.has_value() || n.value().val <= 0)) return false;

	// 超椭圆判定
	double val = pow(abs(normx), n.value().val) + pow(abs(normy), n.value().val);

	// 内/边判定
	if (epsilon > 0.0) return val <= 1.0 + epsilon;
	return val <= 1.0;
}

//// 单个 SVG 控件
BarUiSVGClass::BarUiSVGClass(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	if (color1T.has_value()) { color1 = BarUiColorClass(); color1.value().Initialization(color1T.value()); }
	if (color2T.has_value()) { color2 = BarUiColorClass(); color2.value().Initialization(color2T.value()); }
}
void BarUiSVGClass::Initialization(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	if (color1T.has_value()) { color1 = BarUiColorClass(); color1.value().Initialization(color1T.value()); }
	if (color2T.has_value()) { color2 = BarUiColorClass(); color2.value().Initialization(color2T.value()); }
}
void BarUiSVGClass::InitializationFromString(wstring valT)
{
	svg.Initialization(valT);
	{
		cW = cH = 0.0;
		cColor1 = cColor2 = RGB(0, 0, 0);
	}

	auto temp = CalcWH();
	rW = temp.first, rH = temp.second;
}
void BarUiSVGClass::InitializationFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	Inkeys::Load::ExtractResourceString(valT, resType, resName);
	if (!valT.empty()) InitializationFromString(utf8ToUtf16(valT));
}
void BarUiSVGClass::SetTarFromString(wstring valT)
{
	svg.SetTar(valT);
	{
		cW = cH = 0.0;
		cColor1 = cColor2 = RGB(0, 0, 0);
	}

	auto temp = CalcWH();
	rW = temp.first, rH = temp.second;
}
void BarUiSVGClass::SetTarFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	Inkeys::Load::ExtractResourceString(valT, resType, resName);
	if (!valT.empty()) SetTarFromString(utf8ToUtf16(valT));
}
bool BarUiSVGClass::CacheBitmap(ID2D1DeviceContext* deviceContext, double tarW, double tarH)
{
	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = utf16ToUtf8(svg.GetVal());
		// 替换颜色，如果有
		if (color1.has_value() || color2.has_value())
		{
			auto SvgReplaceColor = [](const string& input, const optional<BarUiColorClass>& color1, const optional<BarUiColorClass>& color2) -> string
				{
					auto colorref_to_rgb = [](COLORREF c) -> string
						{
							int r = GetRValue(c);
							int g = GetGValue(c);
							int b = GetBValue(c);

							return "rgb(" + to_string(r) + "," + to_string(g) + "," + to_string(b) + ")";
						};
					COLORREF col1;
					COLORREF col2;

					string result = input;
					if (color1.has_value())
					{
						col1 = color1.value().val;

						size_t pos = 0;
						const string tag = "rgba(10,0,7,0)";
						const string rgb_str = colorref_to_rgb(col1);
						while ((pos = result.find(tag, pos)) != string::npos)
						{
							result.replace(pos, tag.length(), rgb_str);
							pos += rgb_str.length();
						}
					}
					if (color2.has_value())
					{
						col2 = color2.value().val;

						size_t pos = 0;
						const string tag = "rgba(9,0,2,0)";
						const string rgb_str = colorref_to_rgb(col2);
						while ((pos = result.find(tag, pos)) != string::npos)
						{
							result.replace(pos, tag.length(), rgb_str);
							pos += rgb_str.length();
						}
					}

					return result;
				};

			svgContent = SvgReplaceColor(svgContent, color1, color2);
		}

		// 解析SVG
		document = lunasvg::Document::loadFromData(svgContent);
		if (!document) return false; // 解析失败
	}

	// 绘制到离屏位图
	lunasvg::Bitmap bitmap = document->renderToBitmap(static_cast<int>(tarW), static_cast<int>(tarH));
	{
		if (bitmap.width() == 0 || bitmap.height() == 0 || !bitmap.data()) return false;

		D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
		// lunasvg 文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
		HRESULT hr = deviceContext->CreateBitmap(
			D2D1::SizeU(bitmap.width(), bitmap.height()),
			bitmap.data(),
			bitmap.width() * 4, // stride
			props,
			&cacheBitmap);

		if (FAILED(hr) || !cacheBitmap) return false;
	}

	// 记录缓存值
	{
		cW = tarW, cH = tarH;
		if (color1.has_value()) cColor1 = color1.value().val;
		if (color2.has_value()) cColor2 = color2.value().val;
	}

	return true;
}
bool BarUiSVGClass::SetWH(optional<double> wT, optional<double> hT)
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

	w.tar = tarW;
	h.tar = tarH;

	return true;
}
pair<double, double> BarUiSVGClass::CalcWH()
{
	// 解析SVG
	unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(utf16ToUtf8(svg.GetTar()));
	if (!document) return make_pair(0, 0); // 解析失败

	double w = static_cast<double>(document->width());
	double h = static_cast<double>(document->height());
	return make_pair(w, h);
}

//// 单个文字控件
BarUiWordClass::BarUiWordClass(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	content.Initialization(contentT);
	size.Initialization(sizeT);
	color.Initialization(colorT);
}
void BarUiWordClass::Initialization(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	content.Initialization(contentT);
	size.Initialization(sizeT);
	color.Initialization(colorT);
}