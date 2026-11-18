module;

#include "../../../IdtMain.h"
#include "../../Log/Log.h"
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

module Inkeys.UI.Bar;
import :PenPreviewRendering;
import Inkeys.UI.UI3;
import Inkeys.UI.RenderPipeline;
import Inkeys.Conv.Color;

using namespace Inkeys::UI::UI3;

namespace
{
	constexpr double BarThicknessFineDialLabelFontSizeDip = 10.0;
	auto SharedDWriteFactory() { return Inkeys::UI::RenderPipeline::DWriteFactory(); }
	auto SharedFontCollection() { return Inkeys::UI::RenderPipeline::FontCollection(); }
}

void BarPenPreviewRenderer::ResetTypographyCaches() noexcept
{
	for (auto& cached : thicknessFineDialLabelCache) cached = {};
	thicknessFineDialLabelUseSerial = 0;
}

void BarPenPreviewRenderer::ResetDeviceCaches()
{
	for (auto& cached : thicknessPreviewGradientBrushCache) cached = {};
	thicknessPreviewGradientUseSerial = 0;
	thicknessPreviewPath.Reset();
	thicknessPreviewStrokeStyle.Reset();
	thicknessFineDialSelectorGeometry.Reset();
	ResetTypographyCaches();
	thicknessPreviewGradientFailureLogged = false;
	thicknessPreviewGradientUnavailable = false;
	thicknessPreviewPathFailureLogged = false;
	thicknessPreviewPathUnavailable = false;
	thicknessFineDialSelectorUnavailable = false;
	thicknessFineDialSelectorFailureLogged = false;
}

ID2D1LinearGradientBrush* BarPenPreviewRenderer::GetThicknessPreviewGradientBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color,
	D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
	FLOAT leftOpacity, FLOAT opacity)
{
	if (!deviceContext || !isfinite(leftOpacity) || !isfinite(opacity))
		return nullptr;
	const COLORREF rgb = color & 0x00FFFFFF;
	const std::uint8_t leftOpacityByte = static_cast<std::uint8_t>(round(
		clamp(leftOpacity, 0.0F, 1.0F) * 255.0F));
	opacity = clamp(opacity, 0.0F, 1.0F);
	for (auto& cached : thicknessPreviewGradientBrushCache)
	{
		if (cached.valid && cached.color == rgb
			&& cached.leftOpacity == leftOpacityByte && cached.brush)
		{
			cached.lastUse = ++thicknessPreviewGradientUseSerial;
			cached.brush->SetStartPoint(startPoint);
			cached.brush->SetEndPoint(endPoint);
			cached.brush->SetOpacity(opacity);
			return cached.brush.Get();
		}
	}
	if (thicknessPreviewGradientUnavailable) return nullptr;

	const FLOAT quantizedLeftOpacity =
		static_cast<FLOAT>(leftOpacityByte) / 255.0F;
	D2D1_GRADIENT_STOP gradientStops[] =
	{
		{ 0.0F, Inkeys::Color::ConvertToD2dColor(rgb, quantizedLeftOpacity) },
		{ 1.0F, Inkeys::Color::ConvertToD2dColor(rgb, 1.00) },
	};
	ComPtr<ID2D1GradientStopCollection> stopCollection;
	HRESULT hr = deviceContext->CreateGradientStopCollection(
		gradientStops, ARRAYSIZE(gradientStops), D2D1_GAMMA_2_2,
		D2D1_EXTEND_MODE_CLAMP, &stopCollection);
	ComPtr<ID2D1LinearGradientBrush> brush;
	if (SUCCEEDED(hr))
		hr = deviceContext->CreateLinearGradientBrush(
			D2D1::LinearGradientBrushProperties(startPoint, endPoint),
			stopCollection.Get(), &brush);
	if (FAILED(hr))
	{
		// 创建失败不破坏已有 LRU；旧键仍可命中，新键回退到实色画刷。
		thicknessPreviewGradientUnavailable = true;
		if (!thicknessPreviewGradientFailureLogged)
		{
			thicknessPreviewGradientFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar,
				"创建粗细预览渐变失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	ThicknessPreviewGradientBrushCacheClass created;
	created.color = rgb;
	created.leftOpacity = leftOpacityByte;
	created.lastUse = ++thicknessPreviewGradientUseSerial;
	created.valid = true;
	created.brush = move(brush);
	created.brush->SetOpacity(opacity);
	auto* target = &thicknessPreviewGradientBrushCache.front();
	for (auto& cached : thicknessPreviewGradientBrushCache)
	{
		if (!cached.valid)
		{
			target = &cached;
			break;
		}
		if (cached.lastUse < target->lastUse) target = &cached;
	}
	*target = move(created);
	return target->brush.Get();
}

ID2D1PathGeometry* BarPenPreviewRenderer::GetThicknessPreviewPath(UIRendering& renderer)
{
	auto factory = renderer.GetGeometryFactory();
	if (!factory || thicknessPreviewPathUnavailable) return nullptr;
	if (thicknessPreviewPath)
	{
		return thicknessPreviewPath.Get();
	}

	ComPtr<ID2D1PathGeometry> path;
	HRESULT hr = factory->CreatePathGeometry(&path);
	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1GeometrySink> sink;
		hr = path->Open(&sink);
		if (SUCCEEDED(hr))
		{
			// 单位路径只在当前 device generation 创建一次，帧内仅变换 world matrix。
			sink->BeginFigure(D2D1::Point2F(0.0F, 0.0F),
				D2D1_FIGURE_BEGIN_HOLLOW);
			D2D1_BEZIER_SEGMENT firstSegment{
				D2D1::Point2F(0.30F, 1.0F),
				D2D1::Point2F(0.40F, 1.0F),
				D2D1::Point2F(0.50F, 0.0F) };
			D2D1_BEZIER_SEGMENT secondSegment{
				D2D1::Point2F(0.60F, -1.0F),
				D2D1::Point2F(0.70F, -1.0F),
				D2D1::Point2F(1.0F, 0.0F) };
			sink->AddBezier(firstSegment);
			sink->AddBezier(secondSegment);
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			hr = sink->Close();
		}
	}
	if (FAILED(hr))
	{
		thicknessPreviewPathUnavailable = true;
		if (!thicknessPreviewPathFailureLogged)
		{
			thicknessPreviewPathFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar,
				"创建粗细预览路径失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	thicknessPreviewPath = move(path);
	return thicknessPreviewPath.Get();
}

ID2D1StrokeStyle1* BarPenPreviewRenderer::GetThicknessPreviewStrokeStyle(UIRendering& renderer)
{
	if (thicknessPreviewStrokeStyle) return thicknessPreviewStrokeStyle.Get();
	auto factory = renderer.GetGeometryFactory();
	if (!factory || thicknessPreviewPathUnavailable) return nullptr;
	// 非均匀单位变换只拉伸路径，FIXED 保持真实笔宽与圆头不变形。
	D2D1_STROKE_STYLE_PROPERTIES1 properties{
		D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
		D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND,
		10.0F, D2D1_DASH_STYLE_SOLID, 0.0F,
		D2D1_STROKE_TRANSFORM_TYPE_FIXED };
	HRESULT hr = factory->CreateStrokeStyle(
		&properties, nullptr, 0, &thicknessPreviewStrokeStyle);
	if (FAILED(hr))
	{
		thicknessPreviewPathUnavailable = true;
		if (!thicknessPreviewPathFailureLogged)
		{
			thicknessPreviewPathFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar,
				"创建圆头描边样式失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	return thicknessPreviewStrokeStyle.Get();
}

ID2D1PathGeometry* BarPenPreviewRenderer::GetThicknessFineDialSelectorGeometry(UIRendering& renderer)
{
	if (thicknessFineDialSelectorGeometry)
		return thicknessFineDialSelectorGeometry.Get();
	auto factory = renderer.GetGeometryFactory();
	if (!factory || thicknessFineDialSelectorUnavailable) return nullptr;

	ComPtr<ID2D1PathGeometry> geometry;
	HRESULT hr = factory->CreatePathGeometry(&geometry);
	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1GeometrySink> sink;
		hr = geometry->Open(&sink);
		if (SUCCEEDED(hr))
		{
			// 单位三角只创建一次，绘制时通过矩阵镜像到选择轴上下两侧。
			sink->BeginFigure(
				D2D1::Point2F(-0.5F, 0.0F), D2D1_FIGURE_BEGIN_FILLED);
			sink->AddLine(D2D1::Point2F(0.5F, 0.0F));
			sink->AddLine(D2D1::Point2F(0.0F, 1.0F));
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			hr = sink->Close();
		}
	}
	if (FAILED(hr))
	{
		thicknessFineDialSelectorUnavailable = true;
		if (!thicknessFineDialSelectorFailureLogged)
		{
			thicknessFineDialSelectorFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar,
				"创建 FineDial selector 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	thicknessFineDialSelectorGeometry = move(geometry);
	return thicknessFineDialSelectorGeometry.Get();
}

BarPenPreviewRenderer::ThicknessFineDialLabelCacheClass*
BarPenPreviewRenderer::GetThicknessFineDialLabelLayout(
	UIRendering& renderer, int value, FLOAT zoom, ID2D1DeviceContext* context)
{
	auto dwriteFactory = SharedDWriteFactory();
	auto fontCollection = SharedFontCollection();
	if (!dwriteFactory || !renderer.GetTypographyCache()
		|| !isfinite(zoom) || zoom <= 0.0F)
		return nullptr;
	const FLOAT fontSize = renderer.ResolveAnimatedFontSize(context ? context : renderer.GetDeviceContext(),
		static_cast<double>(BarThicknessFineDialLabelFontSizeDip) * zoom,
		BarThicknessFineDialLabelFontSizeDip*(renderer.IsFontFrameReady()?1.0:zoom/renderer.GetFrameZoom()), fineDialFontCorrection);
	if (fontSize <= 0.0F) return nullptr;
	++thicknessFineDialLabelUseSerial;
	for (auto& cached : thicknessFineDialLabelCache)
	{
		if (cached.valid && cached.value == value
			&& abs(cached.zoom - zoom) <= 0.0001F
			&& cached.fontSize == fontSize && cached.layout)
		{
			cached.lastUse = thicknessFineDialLabelUseSerial;
			return &cached;
		}
	}

	auto* target = &thicknessFineDialLabelCache.front();
	for (auto& cached : thicknessFineDialLabelCache)
	{
		if (!cached.valid)
		{
			target = &cached;
			break;
		}
		if (cached.lastUse < target->lastUse) target = &cached;
	}
	IDWriteTextFormat* format =
		renderer.GetTypographyCache()->GetFormat(
			renderer.GetFontFamily(), fontSize,
			fontCollection.Get(), DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			renderer.GetTextLocale(), DWRITE_TEXT_ALIGNMENT_CENTER,
			DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
	if (!format) return nullptr;

	wchar_t text[16]{};
	int length = _snwprintf_s(text, _countof(text), _TRUNCATE, L"%d", value);
	if (length <= 0) return nullptr;
	ComPtr<IDWriteTextLayout> layout;
	HRESULT hr = dwriteFactory->CreateTextLayout(
		text, static_cast<UINT32>(length), format,
		64.0F * zoom, 20.0F * zoom, &layout);
	if (FAILED(hr)) return nullptr;
	layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	DWRITE_TEXT_METRICS metrics{};
	if (FAILED(layout->GetMetrics(&metrics))) return nullptr;

	*target = {};
	target->value = value;
	target->zoom = zoom;
	target->fontSize = fontSize;
	target->size = D2D1::SizeF(
		ceil(metrics.widthIncludingTrailingWhitespace),
		ceil(metrics.height));
	// CENTER 对齐作用于完整 layout box，绘制原点必须按同一宽度居中。
	target->layoutWidth = metrics.layoutWidth;
	target->lastUse = thicknessFineDialLabelUseSerial;
	target->valid = true;
	target->layout = move(layout);
	return target;
}
