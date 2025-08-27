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
	if (!valT.empty()) InitializationFromString(valT);
}
void BarUiSVGClass::SetTarFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	IdtLoad::ExtractResourceString(valT, resType, resName);
	if (!valT.empty()) SetTarFromString(valT);
}
bool BarUiSVGClass::CacheBitmap(ID2D1DeviceContext* deviceContext, double tarW, double tarH)
{
	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = svg.GetVal();
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
	else if (type == BarUiInheritEnum::Top) { x += xT + wT / 2.0 - wO / 2.0, y += yT; }
	else if (type == BarUiInheritEnum::Left) { x += xT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Center) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Right) { x += xT + wT - wO, y += yT + hT / 2.0 - hO / 2.0; }

	else if (type == BarUiInheritEnum::ToTop) { x += xT + wT / 2.0 - wO / 2.0, y += yT - hO; }
	else if (type == BarUiInheritEnum::ToLeft) { x += xT - wO, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToRight) { x += xT + wT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToBottom) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT; }
}
//// 继承基类
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiInnheritBaseClass& obj) { return UpInh(BarUiInheritClass(typeT, x.val, y.val, w.val, h.val, obj.inhX, obj.inhY, obj.w.val, obj.h.val)); }

/// 单个控件值
//// 单个 SVG 控件
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
	unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.GetTar());
	if (!document) return make_pair(0, 0); // 解析失败

	double w = static_cast<double>(document->width());
	double h = static_cast<double>(document->height());
	return make_pair(w, h);
}