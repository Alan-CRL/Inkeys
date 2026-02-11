module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtD2DPreparation.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtState.h"
#include "../../../IdtWindow.h"
#include "../../../IdtText.h"
#include "../../Conv/IdtColor.h"
#include "../../Other/IdtInputs.h"

module Inkeys.UI.Bar.Main;

BarUIRendering::BarUIRendering(BarUISetClass* barUISetClassT) { barUISetClass = barUISetClassT; }

bool BarUIRendering::Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (shape.enable.val == false) return false;
	if (!shape.fill.has_value() && !shape.frame.has_value()) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
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

	FLOAT tarZoom = static_cast<FLOAT>(barUISetClass->barStyle.zoom);
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

	if (targetRect) BarRenderingAttribute::UnionRectInPlace(*targetRect, BarRenderingAttribute::GetWeigetRect(shape, tarZoom));
	return true;
}
bool BarUIRendering::Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (superellipse.enable.val == false) return false;
	if (!superellipse.fill.has_value() && !superellipse.frame.has_value()) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (superellipse.w.val <= 0 || superellipse.h.val <= 0) return false;
	if (superellipse.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
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
	d2dFactory1->CreatePathGeometry(&geometry);

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

	if (targetRect) BarRenderingAttribute::UnionRectInPlace(*targetRect, BarRenderingAttribute::GetWeigetRect(superellipse, tarZoom));
	return true;
}
bool BarUIRendering::Svg(ID2D1DeviceContext* deviceContext, BarUiSVGClass& svg, const BarUiInheritClass& inh)
{
	// 判断是否启用
	if (svg.enable.val == false) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (svg.w.val <= 0 || svg.h.val <= 0) return false;
	if (svg.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = svg.w.val * tarZoom;
	double tarH = svg.h.val * tarZoom;
	double tarPct = svg.pct.val; // 透明度

	// 获取绘制缓存
	CComPtr<ID2D1Bitmap> d2dBitmap;
	{
		bool needUpdate = false;
		if (svg.cW != tarW || svg.cH != tarH) needUpdate = true;
		if (svg.color1.has_value() && svg.cColor1 != svg.color1.value().val) needUpdate = true;
		if (svg.color2.has_value() && svg.cColor2 != svg.color2.value().val) needUpdate = true;

		// TODO 优化：可选动画过程中不更新缓存
		if (needUpdate || !svg.cacheBitmap)
		{
			if (!svg.CacheBitmap(deviceContext, tarW, tarH))
				return false;
		}
		d2dBitmap = svg.cacheBitmap;
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
bool BarUIRendering::Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_ALIGNMENT textAlign)
{
	// 判断是否启用
	if (word.enable.val == false) return false;
	if (barUISetClass->barStyle.zoom <= 0.0) return false;
	if (word.size.val <= 0) return false;
	if (word.w.val <= 0 || word.h.val <= 0) return false;
	if (word.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = barUISetClass->barStyle.zoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = word.w.val * tarZoom;
	double tarH = word.h.val * tarZoom;
	double tarSize = word.size.val * tarZoom;
	double tarPct = word.pct.val; // 透明度

	// Word 控件改为存入 wstring
	wstring tarContent = word.content.GetVal();

	// 获取样式
	IDWriteTextFormat* textFormat = nullptr;
	{
		/*IDWriteTextFormat* tmpTextFormat;
		dWriteFactory1->CreateTextFormat(
			L"HarmonyOS Sans SC",
			dWriteFontCollection,
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
			tarSize,
			dWriteFontCollection,
			fontWeight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			L"zh-cn",
			textAlign,
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

		deviceContext->DrawTextW(
			tarContent.c_str(),
			wcslen(tarContent.c_str()),
			textFormat,
			layoutRect,
			spFillBrush,
			D2D1_DRAW_TEXT_OPTIONS_CLIP
		);
	}

	return true;
}