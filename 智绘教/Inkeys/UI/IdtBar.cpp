#include "IdtBar.h"

#include "../../IdtConfiguration.h"
#include "../../IdtD2DPreparation.h"
#include "../../IdtDisplayManagement.h"
#include "../../IdtDrawpad.h"
#include "../../IdtWindow.h"
#include "../../IdtText.h"
#include "../Conv/IdtColor.h"
#include "../Load/IdtLoad.h"
#include "IdtBarState.h"
#include "IdtBarBottom.h"

//#undef max
//#undef min
//#include "libcuckoo/cuckoohash_map.h"

#define LUNASVG_BUILD_STATIC
#include <lunasvg/lunasvg.h>
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

// ====================
// 临时
void FloatingInstallHook();

// ====================
// 窗口

// ====================
// 媒体
void BarMediaClass::LoadExImage()
{
}
void BarMediaClass::LoadFormat()
{
	formatCache = make_unique<BarFormatCache>(D2DTextFactory);
}

// ====================
// 界面

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

	// TODO 拓展更多类型组合
	if (type == BarUiInheritEnum::TopLeft) { x += xT, y += yT; }
	else if (type == BarUiInheritEnum::Left) { x += xT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Center) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT / 2.0 - hO / 2.0; }

	else if (type == BarUiInheritEnum::ToRight) { x += xT + wT, y += yT; }
	else if (type == BarUiInheritEnum::ToLeft) { x += xT - wO, y += yT; }
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

// 具体渲染
bool BarUIRendering::Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, bool clip)
{
	// 判断是否启用
	if (shape.enable.val == false) return false;
	if (!shape.fill.has_value() && !shape.frame.has_value()) return false;
	if (barStyle.zoom <= 0.0) return false;
	if (shape.w.val <= 0 || shape.h.val <= 0) return false;
	if (shape.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = shape.w.val;
	double tarH = shape.h.val;
	double tarPct = shape.pct.val; // 透明度

	double tarRw = 0.0;
	double tarRh = 0.0;
	if (shape.rw.has_value()) tarRw = shape.rw.value().val;
	if (shape.rh.has_value()) tarRh = shape.rh.value().val;

	FLOAT tarZoom = static_cast<FLOAT>(barStyle.zoom);
	D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<FLOAT>(tarX) * tarZoom, static_cast<FLOAT>(tarY) * tarZoom, static_cast<FLOAT>(tarX + tarW) * tarZoom, static_cast<FLOAT>(tarY + tarH) * tarZoom), static_cast<FLOAT>(tarRw) * tarZoom, static_cast<FLOAT>(tarRh) * tarZoom);

	// Clip
	if (clip)
	{
		CComPtr<ID2D1SolidColorBrush> spFillBrush;
		deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(RGB(0, 0, 0), 0.0), &spFillBrush);

		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillRoundedRectangle(&roundedRect, spFillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}
	// 渲染到 DC
	{
		// 渲染填充
		if (shape.fill.has_value())
		{
			COLORREF fill = shape.fill.value().val;

			CComPtr<ID2D1SolidColorBrush> spFillBrush;
			deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(fill, tarPct), &spFillBrush);

			deviceContext->FillRoundedRectangle(&roundedRect, spFillBrush);
		}
		// 渲染边框
		if (shape.frame.has_value())
		{
			COLORREF frame = shape.frame.value().val;
			double tarFramePct = tarPct;
			if (shape.framePct.has_value()) tarFramePct = shape.framePct.value().val;

			CComPtr<ID2D1SolidColorBrush> spBorderBrush;
			deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(frame, tarFramePct), &spBorderBrush);

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			if (shape.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(shape.ft.value().val * tarZoom);
				if (strokeWidth > 0.0F) deviceContext->DrawRoundedRectangle(&roundedRect, spBorderBrush, strokeWidth);
			}
			else deviceContext->DrawRoundedRectangle(&roundedRect, spBorderBrush, strokeWidth);
		}
	}

	return true;
}
bool BarUIRendering::Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, bool clip)
{
	// 判断是否启用
	if (superellipse.enable.val == false) return false;
	if (!superellipse.fill.has_value() && !superellipse.frame.has_value()) return false;
	if (barStyle.zoom <= 0.0) return false;
	if (superellipse.w.val <= 0 || superellipse.h.val <= 0) return false;
	if (superellipse.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = superellipse.w.val * tarZoom;
	double tarH = superellipse.h.val * tarZoom;
	double tarPct = superellipse.pct.val; // 透明度

	double tarN = 4.0;
	if (superellipse.n.has_value()) tarN = superellipse.n.value().val;

	auto genPoints = [&](float left, float top, float width, float height, float n, int segs)
		{
			const float Pi = 3.14159265359f;
			float a = width / 2.0f;
			float b = height / 2.0f;
			float cx = left + a;
			float cy = top + b;

			vector<D2D1_POINT_2F> pts;
			for (int i = 0; i < segs; i++)
			{
				float theta = 2.0f * Pi * i / segs;
				float cosT = cosf(theta);
				float sinT = sinf(theta);
				float x0 = a * (cosT >= 0 ? powf(cosT, 2.0f / n) : -powf(-cosT, 2.0f / n));
				float y0 = b * (sinT >= 0 ? powf(sinT, 2.0f / n) : -powf(-sinT, 2.0f / n));
				pts.emplace_back(D2D1::Point2F(cx + x0, cy + y0));
			}
			pts.emplace_back(pts[0]); // 闭合

			return pts;
		};

	auto toBeziers = [](const vector<D2D1_POINT_2F>& pts, float tension = 1.0f)
		{
			// Catmull-Rom到Bezier转换，首尾闭合
			vector<D2D1_BEZIER_SEGMENT> beziers;
			int N = static_cast<int>(pts.size()) - 1; // pts已闭合，最后一个是等于第一个
			if (N < 3) return beziers;

			for (int i = 0; i < N; i++)
			{
				D2D1_POINT_2F p0 = pts[(i - 1 + N) % N];
				D2D1_POINT_2F p1 = pts[i];
				D2D1_POINT_2F p2 = pts[(i + 1) % N];
				D2D1_POINT_2F p3 = pts[(i + 2) % N];

				D2D1_BEZIER_SEGMENT seg;
				seg.point1 =
				{
					p1.x + (p2.x - p0.x) / 6.0f * tension,
					p1.y + (p2.y - p0.y) / 6.0f * tension
				};
				seg.point2 =
				{
					p2.x - (p3.x - p1.x) / 6.0f * tension,
					p2.y - (p3.y - p1.y) / 6.0f * tension
				};
				seg.point3 = p2;

				beziers.push_back(seg);
			}
			return beziers;
		};

	// 计算边框路径
	int segs = clamp(static_cast<int>((tarW + tarH) / 8.0), 24, 128);
	vector<D2D1_POINT_2F> pts = genPoints(static_cast<float>(tarX), static_cast<float>(tarY), static_cast<float>(tarW), static_cast<float>(tarH), static_cast<float>(tarN), segs);
	vector<D2D1_BEZIER_SEGMENT> beziers = toBeziers(pts);
	if (beziers.empty()) return false;

	CComPtr<ID2D1PathGeometry> geometry;
	D2DFactory->CreatePathGeometry(&geometry);

	{
		CComPtr<ID2D1GeometrySink> sink;
		geometry->Open(&sink);
		sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
		sink->AddBeziers(beziers.data(), static_cast<UINT32>(beziers.size()));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		sink->Close();
	}

	// Clip
	if (clip)
	{
		CComPtr<ID2D1SolidColorBrush> spFillBrush;
		deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(RGB(0, 0, 0), 0.0), &spFillBrush);

		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillGeometry(geometry, spFillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}

	// 渲染到 DC
	{
		// 渲染填充
		if (superellipse.fill.has_value())
		{
			COLORREF fill = superellipse.fill.value().val;

			CComPtr<ID2D1SolidColorBrush> spFillBrush;
			deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(fill, tarPct), &spFillBrush);

			deviceContext->FillGeometry(geometry, spFillBrush);
		}
		// 渲染边框
		if (superellipse.frame.has_value())
		{
			COLORREF frame = superellipse.frame.value().val;
			double tarFramePct = tarPct;
			if (superellipse.framePct.has_value()) tarFramePct = superellipse.framePct.value().val;

			CComPtr<ID2D1SolidColorBrush> spBorderBrush;
			deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(frame, tarFramePct), &spBorderBrush);

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			if (superellipse.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(superellipse.ft.value().val * tarZoom);
				if (strokeWidth > 0.0F) deviceContext->DrawGeometry(geometry, spBorderBrush, strokeWidth);
			}
			else deviceContext->DrawGeometry(geometry, spBorderBrush, strokeWidth);
		}
	}

	return true;
}
bool BarUIRendering::Svg(ID2D1DeviceContext* deviceContext, const BarUiSVGClass& svg, const BarUiInheritClass& inh)
{
	// 判断是否启用
	if (svg.enable.val == false) return false;
	if (barStyle.zoom <= 0.0) return false;
	if (svg.w.val <= 0 || svg.h.val <= 0) return false;
	if (svg.pct.val <= 0.0) return false;

	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = svg.svg.GetVal();
		// 替换颜色，如果有
		if (svg.color1.has_value() || svg.color2.has_value())
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

			svgContent = SvgReplaceColor(svgContent, svg.color1, svg.color2);
		}

		// 解析SVG
		document = lunasvg::Document::loadFromData(svgContent);
		if (!document) return false; // 解析失败
	}

	// 初始化绘制量
	double tarZoom = barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = svg.w.val * tarZoom;
	double tarH = svg.h.val * tarZoom;
	double tarPct = svg.pct.val; // 透明度

	// 绘制到离屏位图
	lunasvg::Bitmap bitmap = document->renderToBitmap(static_cast<int>(tarW), static_cast<int>(tarH));
	CComPtr<ID2D1Bitmap> d2dBitmap;
	{
		if (bitmap.width() == 0 || bitmap.height() == 0 || !bitmap.data()) return false;

		D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
		// lunasvg 文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
		HRESULT hr = deviceContext->CreateBitmap(
			D2D1::SizeU(bitmap.width(), bitmap.height()),
			bitmap.data(),
			bitmap.width() * 4, // stride
			props,
			&d2dBitmap);

		if (FAILED(hr) || !d2dBitmap) return false;
	}

	// 渲染到 DC
	{
		D2D1_RECT_F destRect = D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY), static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH));
		deviceContext->DrawBitmap(
			d2dBitmap,
			destRect,								// 目标矩形
			static_cast<FLOAT>(tarPct),				// 不透明度
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
			nullptr									// 源rect, null表示全部
		);
	}

	return true;
}
bool BarUIRendering::Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh)
{
	// 判断是否启用
	if (word.enable.val == false) return false;
	if (barStyle.zoom <= 0.0) return false;
	if (word.size.val <= 0) return false;
	if (word.w.val <= 0 || word.h.val <= 0) return false;
	if (word.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = word.w.val * tarZoom;
	double tarH = word.h.val * tarZoom;
	double tarSize = word.size.val * tarZoom;
	double tarPct = word.pct.val; // 透明度

	wstring tarContent = utf8ToUtf16(word.content.GetVal());

	// 获取样式
	IDWriteTextFormat* textFormat = nullptr;
	{
		/*IDWriteTextFormat* tmpTextFormat;
		D2DTextFactory->CreateTextFormat(
			L"HarmonyOS Sans SC",
			D2DFontCollection,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			static_cast<FLOAT>(tarSize),
			L"zh-cn",
			&tmpTextFormat
		);
		tmpTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		tmpTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

		textFormat.Attach(tmpTextFormat);*/

		textFormat = barUISetClass->barMedia.formatCache->GetFormat(
			L"HarmonyOS Sans SC",
			30.0f,
			nullptr,
			DWRITE_FONT_WEIGHT_BOLD,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			L"zh-cn",
			DWRITE_TEXT_ALIGNMENT_CENTER,       // 指定居中对齐
			DWRITE_PARAGRAPH_ALIGNMENT_CENTER   // 指定段落居中
		);
	}
	// 计算区域
	D2D1_RECT_F layoutRect;
	{
		layoutRect = D2D1::RectF(
			static_cast<FLOAT>(tarX),
			static_cast<FLOAT>(tarY),
			static_cast<FLOAT>(tarX + tarW),
			static_cast<FLOAT>(tarY + tarH)
		);
	}
	// 渲染到 DC
	{
		COLORREF color = word.color.val;

		CComPtr<ID2D1SolidColorBrush> spFillBrush;
		deviceContext->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(color, tarPct), &spFillBrush);

		deviceContext->DrawText(
			tarContent.c_str(),
			wcslen(tarContent.c_str()),
			textFormat,
			layoutRect,
			spFillBrush
		);
	}

	return true;
}

// UI 总集
void BarUISetClass::Rendering()
{
	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;
	}
	SIZE sizeWnd = { static_cast<LONG>(barWindow.w), static_cast<LONG>(barWindow.h) };
	POINT ptSrc = { 0,0 };
	POINT ptDst = { 0,0 };
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	{
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = NULL;
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
	}

	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	// 初始化 按钮 们
	BarButtomSetClass barButtomSet;
	barButtomSet.PresetInitialization();
	barButtomSet.Load();

	// 初始化 D2D DC
	CComPtr<ID2D1DeviceContext>				barDeviceContext;
	CComPtr<ID2D1Bitmap1>					barBackgroundBitmap;
	CComPtr<ID2D1GdiInteropRenderTarget>	barGdiInterop;
	{
		CComPtr<ID3D11Device>         d3dDevice;
		CComPtr<ID2D1Device>          d2dDevice;

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if !defined(IDT_RELEASE)
		creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0
		};

		D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_WARP,       // **关键：使用 WARP 软件渲染器**
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			&d3dDevice,                 // 返回创建的设备
			nullptr,                    // 返回实际的功能级别
			nullptr                     // 返回设备上下文 (我们不需要)
		);

		CComPtr<IDXGIDevice> dxgiDevice;
		d3dDevice.QueryInterface(&dxgiDevice);

		D2DFactory->CreateDevice(dxgiDevice, &d2dDevice);
		d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &barDeviceContext);

		D2D1_BITMAP_PROPERTIES1 bitmapProperties =
			D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
			);

		D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(barWindow.w), static_cast<UINT32>(barWindow.h));

		barDeviceContext->CreateBitmap(
			size,
			nullptr,
			0,
			&bitmapProperties,
			&barBackgroundBitmap
		);

		barDeviceContext->QueryInterface(__uuidof(ID2D1GdiInteropRenderTarget), (void**)&barGdiInterop);

		barDeviceContext->SetTarget(barBackgroundBitmap);
		barDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	}

	chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

	wstring fps;
	for (int forNum = 1; !offSignal; forNum = 2)
	{
		// 计算 UI
		{
			// TODO forNum == 1 时，直接设置相等

			// 主栏
			{
				// 按钮位置计算（特别操作）
				int totalWidth = 5;
				{
					int xO = 5, yO = 0;
					for (int id = 0; id < barButtomSet.tot; id++)
					{
						BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
						if (temp == nullptr) continue;

						if (temp->size == BarButtomSizeEnum::twoTwo)
						{
							if (yO != 0) xO = totalWidth;

							{
								if (barState.fold)
								{
									temp->buttom.x.tar = 10.0;
									temp->buttom.y.tar = 5.0;

									temp->buttom.pct.tar = 0.0;
								}
								else
								{
									temp->buttom.x.tar = xO;
									temp->buttom.y.tar = yO + 5.0;

									if (temp->state == BarButtomState::None /*TSTT TODO*/) temp->buttom.pct.tar = 0.2;
									else temp->buttom.pct.tar = 0.0;
								}
								temp->buttom.w.tar = 60.0;
								temp->buttom.h.tar = 70.0;

								temp->buttom.fill.value().tar = RGB(88, 255, 236);
							}
							{
								temp->icon.SetWH(nullopt, 30.0);
								temp->icon.y.tar = -10.0;
								if (barState.fold)
								{
									temp->icon.pct.tar = 0.0;
								}
								else
								{
									temp->icon.pct.tar = 1.0;

									if (temp->state == BarButtomState::None /*TSTT TODO*/) temp->icon.color1.value().tar = RGB(88, 255, 236);
									else temp->icon.color1.value().tar = RGB(255, 255, 255);
								}
							}
							{
								temp->name.x.tar = 0.0;
								temp->name.y.tar = 20.0;
								temp->name.w.tar = 60.0;
								temp->name.h.tar = 25.0;
								if (barState.fold) temp->name.pct.tar = 0.0;
								else temp->name.pct.tar = 1.0;

								temp->name.color.tar = RGB(88, 255, 236);
								temp->name.size.tar = 20.0;
							}

							xO += 65, yO = 0;
							totalWidth += 65;
						}

						// 特殊体质 - 分隔栏
						if (temp->size == BarButtomSizeEnum::oneTwo)
						{
							if (yO != 0) xO = totalWidth;

							{
								if (barState.fold)
								{
									temp->buttom.x.tar = 35.0;
									temp->buttom.y.tar = 5.0;
								}
								else
								{
									temp->buttom.x.tar = xO;
									temp->buttom.y.tar = yO + 5.0;
								}
								temp->buttom.w.tar = 10.0;
								temp->buttom.h.tar = 70.0;

								// TODO 后续新增悬停颜色
								temp->buttom.pct.tar = 0.0;
							}
							{
								temp->icon.SetWH(nullopt, 60.0);
								if (barState.fold)
								{
									temp->icon.pct.tar = 0.0;
								}
								else
								{
									temp->icon.pct.tar = 0.18;
									temp->icon.color1.value().tar = RGB(255, 255, 255);
								}
							}

							xO += 15, yO = 0;
							totalWidth += 15;
						}
					}
				}

				// 主栏
				if (barState.fold)
				{
					shapeMap[BarUISetShapeEnum::MainBar]->x.tar = 0;
					shapeMap[BarUISetShapeEnum::MainBar]->w.tar = 80;

					shapeMap[BarUISetShapeEnum::MainBar]->pct.tar = 0.0;
					shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().tar = 0.0;
				}
				else
				{
					shapeMap[BarUISetShapeEnum::MainBar]->w.tar = static_cast<double>(totalWidth);
					shapeMap[BarUISetShapeEnum::MainBar]->x.tar = superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() + 10;

					shapeMap[BarUISetShapeEnum::MainBar]->pct.tar = 0.73;
					shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().tar = 0.18;
				}
			}
		}

		// 动效 UI
		bool needRendering = false;
		{
			auto ChangeState = [&](BarUiStateClass& state) -> void
				{
					needRendering = true;
					state.val = state.tar;
				};
			auto ChangeValue = [&](BarUiValueClass& value) -> void
				{
					needRendering = true;
					BarUiValueModeEnum mod = value.mod;
					/*else*/ value.val = value.tar;
				};
			auto ChangeColor = [&](BarUiColorClass& color) -> void
				{
					needRendering = true;
					color.val = color.tar;
				};
			auto ChangePct = [&](BarUiPctClass& pct) -> void
				{
					needRendering = true;
					pct.val = pct.tar;
				};
			auto ChangeString = [&](BarUiStringClass& stringO) -> void
				{
					needRendering = true;
					stringO.ApplyTar();
				};

			for (const auto& [key, val] : shapeMap)
			{
				if (!val->enable.IsSame()) ChangeState(val->enable);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->y.IsSame()) ChangeValue(val->y);
				if (!val->w.IsSame()) ChangeValue(val->w);
				if (!val->h.IsSame()) ChangeValue(val->h);
				if (val->rw.has_value() && !val->rw->IsSame()) ChangeValue(val->rw.value());
				if (val->rh.has_value() && !val->rh->IsSame()) ChangeValue(val->rh.value());
				if (val->ft.has_value() && !val->ft->IsSame()) ChangeValue(val->ft.value());
				if (val->fill.has_value() && !val->fill->IsSame()) ChangeColor(val->fill.value());
				if (val->frame.has_value() && !val->frame->IsSame()) ChangeColor(val->frame.value());
				if (!val->pct.IsSame()) ChangePct(val->pct);
			}
			for (const auto& [key, val] : superellipseMap)
			{
				if (!val->enable.IsSame()) ChangeState(val->enable);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->y.IsSame()) ChangeValue(val->y);
				if (!val->w.IsSame()) ChangeValue(val->w);
				if (!val->h.IsSame()) ChangeValue(val->h);
				if (val->n.has_value() && !val->n->IsSame()) ChangeValue(val->n.value());
				if (val->ft.has_value() && !val->ft->IsSame()) ChangeValue(val->ft.value());
				if (val->fill.has_value() && !val->fill->IsSame()) ChangeColor(val->fill.value());
				if (val->frame.has_value() && !val->frame->IsSame()) ChangeColor(val->frame.value());
				if (!val->pct.IsSame()) ChangePct(val->pct);
			}
			for (const auto& [key, val] : svgMap)
			{
				if (!val->enable.IsSame()) ChangeState(val->enable);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->y.IsSame()) ChangeValue(val->y);
				if (!val->w.IsSame()) ChangeValue(val->w);
				if (!val->h.IsSame()) ChangeValue(val->h);
				if (val->svg.IsSame()) ChangeString(val->svg);
				if (val->color1.has_value() && !val->color1->IsSame()) ChangeColor(val->color1.value());
				if (val->color2.has_value() && !val->color2->IsSame()) ChangeColor(val->color2.value());
				if (!val->pct.IsSame()) ChangePct(val->pct);
			}
			for (const auto& [key, val] : wordMap)
			{
				if (!val->enable.IsSame()) ChangeState(val->enable);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->x.IsSame()) ChangeValue(val->x);
				if (!val->y.IsSame()) ChangeValue(val->y);
				if (!val->w.IsSame()) ChangeValue(val->w);
				if (!val->h.IsSame()) ChangeValue(val->h);
				if (!val->size.IsSame()) ChangeValue(val->size);
				if (!val->content.IsSame()) ChangeString(val->content);
				if (!val->color.IsSame()) ChangeColor(val->color);
				if (!val->pct.IsSame()) ChangePct(val->pct);
			}

			// 特殊体质：按钮
			for (int id = 0; id < barButtomSet.tot; id++)
			{
				BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
				if (temp == nullptr) continue;

				if (!temp->buttom.enable.IsSame()) ChangeState(temp->buttom.enable);
				if (!temp->buttom.x.IsSame()) ChangeValue(temp->buttom.x);
				if (!temp->buttom.x.IsSame()) ChangeValue(temp->buttom.x);
				if (!temp->buttom.y.IsSame()) ChangeValue(temp->buttom.y);
				if (!temp->buttom.w.IsSame()) ChangeValue(temp->buttom.w);
				if (!temp->buttom.h.IsSame()) ChangeValue(temp->buttom.h);
				if (temp->buttom.rw.has_value() && temp->buttom.rw->IsSame()) ChangeValue(temp->buttom.rw.value());
				if (temp->buttom.rh.has_value() && temp->buttom.rh->IsSame()) ChangeValue(temp->buttom.rh.value());
				if (temp->buttom.ft.has_value() && temp->buttom.ft->IsSame()) ChangeValue(temp->buttom.ft.value());
				if (temp->buttom.fill.has_value() && !temp->buttom.fill->IsSame()) ChangeColor(temp->buttom.fill.value());
				if (temp->buttom.frame.has_value() && !temp->buttom.frame->IsSame()) ChangeColor(temp->buttom.frame.value());
				if (!temp->buttom.pct.IsSame()) ChangePct(temp->buttom.pct);

				if (!temp->icon.enable.IsSame()) ChangeState(temp->icon.enable);
				if (!temp->icon.x.IsSame()) ChangeValue(temp->icon.x);
				if (!temp->icon.x.IsSame()) ChangeValue(temp->icon.x);
				if (!temp->icon.y.IsSame()) ChangeValue(temp->icon.y);
				if (!temp->icon.w.IsSame()) ChangeValue(temp->icon.w);
				if (!temp->icon.h.IsSame()) ChangeValue(temp->icon.h);
				if (temp->icon.svg.IsSame()) ChangeString(temp->icon.svg);
				if (temp->icon.color1.has_value() && !temp->icon.color1->IsSame()) ChangeColor(temp->icon.color1.value());
				if (temp->icon.color2.has_value() && !temp->icon.color2->IsSame()) ChangeColor(temp->icon.color2.value());
				if (!temp->icon.pct.IsSame()) ChangePct(temp->icon.pct);

				if (!temp->name.enable.IsSame()) ChangeState(temp->name.enable);
				if (!temp->name.x.IsSame()) ChangeValue(temp->name.x);
				if (!temp->name.x.IsSame()) ChangeValue(temp->name.x);
				if (!temp->name.y.IsSame()) ChangeValue(temp->name.y);
				if (!temp->name.w.IsSame()) ChangeValue(temp->name.w);
				if (!temp->name.h.IsSame()) ChangeValue(temp->name.h);
				if (!temp->name.size.IsSame()) ChangeValue(temp->name.size);
				if (!temp->name.content.IsSame()) ChangeString(temp->name.content);
				if (!temp->name.color.IsSame()) ChangeColor(temp->name.color);
				if (!temp->name.pct.IsSame()) ChangePct(temp->name.pct);
			}
		}

		// 渲染 UI
		{
			barDeviceContext->BeginDraw();

			{
				D2D1_COLOR_F clearColor = ConvertToD2DColor(RGBA(0, 0, 0, 0));
				barDeviceContext->Clear(&clearColor);
			}

			using enum BarUiInheritEnum;
			{
				auto obj = BarUISetShapeEnum::MainBar;
				superellipseMap[BarUISetSuperellipseEnum::MainButton]->Inherit(); // 提前计算依赖
				spec.Shape(barDeviceContext, *shapeMap[obj], shapeMap[obj]->Inherit(Left, *superellipseMap[BarUISetSuperellipseEnum::MainButton]), false);

				for (int id = 0; id < barButtomSet.tot; id++)
				{
					BarButtomClass* temp = barButtomSet.buttomlist.Get(id);
					if (temp == nullptr) continue;

					//Testa(temp->name.content.GetTar());

					spec.Shape(barDeviceContext, temp->buttom, temp->buttom.Inherit(TopLeft, *shapeMap[BarUISetShapeEnum::MainBar]));
					spec.Svg(barDeviceContext, temp->icon, temp->icon.Inherit(Center, temp->buttom));
					spec.Word(barDeviceContext, temp->name, temp->name.Inherit(Center, temp->buttom));
					// TODO

					/*Testi(temp->buttom.inhX);
					Testi(temp->buttom.inhY);
					Testi(temp->buttom.w.val);
					Testi(temp->buttom.h.val);*/
				}
			}
			{
				{
					auto obj = BarUISetSuperellipseEnum::MainButton;
					spec.Superellipse(barDeviceContext, *superellipseMap[obj], superellipseMap[obj]->Inherit(), false);

					{
						auto obj = BarUISetSvgEnum::logo1;
						spec.Svg(barDeviceContext, *svgMap[obj], svgMap[obj]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]));
					}
				}
			}

			// FPS
			{
				CComPtr<IDWriteTextFormat> pTextFormat;
				D2DTextFactory->CreateTextFormat(
					L"Microsoft YaHei", // 字体名
					nullptr,
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					40, // 字号
					L"zh-cn",
					&pTextFormat);

				// 3. 创建画刷
				CComPtr<ID2D1SolidColorBrush> pBrush;
				barDeviceContext->CreateSolidColorBrush(
					D2D1::ColorF(D2D1::ColorF(255, 0, 0, 1.0)),
					&pBrush);

				// 4. 设定绘制区域
				D2D1_RECT_F layoutRect = D2D1::RectF(100, 100, 1000, 1000);

				// 5. 绘制文本
				barDeviceContext->DrawTextW(
					fps.c_str(),              // text
					(UINT32)fps.length(),     // text length
					pTextFormat,               // format
					layoutRect,                // layout rect
					pBrush,                    // brush
					D2D1_DRAW_TEXT_OPTIONS_NONE
				);
			}

			barDeviceContext->Flush();

			{
				// TODO 脏区更新
				// psize 指定窗口本次更新“新内容”宽高
				// pptDst 指定新内容贴到屏幕上的位置（左上角）
				// pptSrc 从源内存 DC 的哪个位置起贴内容

				// 设置窗口位置
				POINT ptDst = { 0, 0 };
				// 获取 DC
				HDC hdc = nullptr;
				barGdiInterop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &hdc);

				ulwi.pptDst = &ptDst;
				ulwi.hdcSrc = hdc;
				UpdateLayeredWindowIndirect(floating_window, &ulwi);

				barGdiInterop->ReleaseDC(nullptr);
			}

			barDeviceContext->EndDraw();
			barMedia.formatCache->Clean();
		}

		if (forNum == 1)
		{
			IdtWindowsIsVisible.floatingWindow = true;
		}
		// 帧率锁
		{
			double delay = 1000.0 / 60.0 - chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			if (delay >= 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
		}

		{
			double cost = chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			fps = format(L"{:.2f} FPS", 1000.0 / cost);
		}
		reckon = chrono::high_resolution_clock::now();
	}

	return;
}

// ====================
// 交互
void BarUISetClass::Interact()
{
	struct
	{
		ExMessage msg;
		int x, y;
	}Msg;

	while (!offSignal)
	{
		hiex::getmessage_win32(&Msg.msg, EM_MOUSE, floating_window);

		{
			if (superellipseMap[BarUISetSuperellipseEnum::MainButton]->IsClick(Msg.msg.x, Msg.msg.y))
			{
				if (Msg.msg.message == WM_LBUTTONDOWN)
				{
					Msg.x = Msg.msg.x, Msg.y = Msg.msg.y;
					{
						double moveDis = Seek(Msg.msg);
						if (moveDis <= 20)
						{
							// 展开/收起主栏
							if (barState.fold) barState.fold = false;
							else barState.fold = true;
						}
					}
					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}

				if (Msg.msg.message == WM_RBUTTONDOWN && setlist.RightClickClose)
				{
					if (MessageBox(floating_window, L"Whether to turn off 智绘教Inkeys?\n是否关闭 智绘教Inkeys？", L"Inkeys Tips | 智绘教提示", MB_OKCANCEL | MB_SYSTEMMODAL) == 1) CloseProgram();
					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}
			}
		}
	}
}
double BarUISetClass::Seek(const ExMessage& msg)
{
	if (!KeyBoradDown[VK_LBUTTON]) return 0;

	POINT p;
	GetCursorPos(&p);

	double firX = static_cast<double>(p.x);
	double firY = static_cast<double>(p.y);

	double ret = 0.0;

	while (1)
	{
		if (!KeyBoradDown[VK_LBUTTON]) break;
		GetCursorPos(&p);

		if (firX == p.x && firY == p.y) continue;

		double tarZoom = barStyle.zoom;
		superellipseMap[BarUISetSuperellipseEnum::MainButton]->x.tar += static_cast<double>(p.x - firX) / tarZoom;
		superellipseMap[BarUISetSuperellipseEnum::MainButton]->y.tar += static_cast<double>(p.y - firY) / tarZoom;

		ret += sqrt((p.x - firX) * (p.x - firX) + (p.y - firY) * (p.y - firY));
		firX = static_cast<double>(p.x), firY = static_cast<double>(p.y);

		//if (setlist.regularSetting.moveRecover)
		//{
		//	if (ret > 20 && target_status != 0)
		//	{
		//		target_status = 0;
		//	}
		//}
	}

	return ret;
}

// 初始化
void BarInitializationClass::Initialization()
{
	threadStatus[L"BarInitialization"] = true;

	BarUISetClass barUISet;

	// 初始化
	InitializeWindow(barUISet);
	InitializeMedia(barUISet);
	InitializeUI(barUISet);

	barUISet.barMedia.LoadFormat();

	// 线程
	thread(FloatingInstallHook).detach();
	thread([&]() { barUISet.Rendering(); }).detach();
	thread([&]() { barUISet.Interact(); }).detach();

	// 等待

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	// 反初始化

	unsigned int waitTimes = 1;
	for (; waitTimes <= 10; waitTimes++)
	{
		if (!threadStatus[L"BarUI"] &&
			!threadStatus[L"BarDraw"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}

	threadStatus[L"BarInitialization"] = false;
	return;
}
void BarInitializationClass::InitializeWindow(BarUISetClass& barUISet)
{
	DisableResizing(floating_window, true); // hiex 禁止窗口拉伸

	SetWindowLong(floating_window, GWL_STYLE, GetWindowLong(floating_window, GWL_STYLE) & ~WS_CAPTION); // 隐藏窗口标题栏
	SetWindowLong(floating_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW); // 隐藏窗口任务栏图标

	barUISet.barWindow.x = 0;
	barUISet.barWindow.y = 0;
	barUISet.barWindow.w = 2000;// MainMonitor.MonitorWidth;
	barUISet.barWindow.h = 1000;// MainMonitor.MonitorHeight;
	barUISet.barWindow.pct = 255;
	SetWindowPos(floating_window, NULL, barUISet.barWindow.x, barUISet.barWindow.y, barUISet.barWindow.w, barUISet.barWindow.h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_DRAWFRAME); // 设置窗口位置尺寸
}
void BarInitializationClass::InitializeMedia(BarUISetClass& barUISet)
{
	barUISet.barMedia.LoadExImage();
}
void BarInitializationClass::InitializeUI(BarUISetClass& barUISet)
{
	// 定义 UI 控件
	{
		// 主按钮
		{
			auto superellipse = make_shared<BarUiSuperellipseClass>(200.0, 200.0, 80.0, 80.0, 3.0, 1.0, RGB(24, 24, 24), RGB(255, 255, 255));
			superellipse->pct.Initialization(0.73);
			superellipse->framePct = BarUiPctClass(0.18);
			superellipse->x.mod = BarUiValueModeEnum::Once;
			superellipse->y.mod = BarUiValueModeEnum::Once;
			superellipse->enable.Initialization(true);
			barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton] = superellipse;

			{
				auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
				svg->InitializationFromResource(L"UI", L"logo1");
				svg->SetWH(nullopt, 80.0); // 必须在 Initial 后
				svg->enable.Initialization(true);
				barUISet.svgMap[BarUISetSvgEnum::logo1] = svg;
			}
			{
			}
		}
		// 主栏
		{
			auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 80.0, 80.0, 8.0, 8.0, 1.0, RGB(24, 24, 24), RGB(255, 255, 255));
			shape->pct.Initialization(0.73);
			shape->framePct = BarUiPctClass(0.18);
			shape->enable.Initialization(true);
			barUISet.shapeMap[BarUISetShapeEnum::MainBar] = shape;
		}
	}
}