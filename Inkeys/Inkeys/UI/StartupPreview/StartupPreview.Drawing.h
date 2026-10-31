#pragma once

#include "../Superellipse.Geometry.h"
#include "../StartupPlacement.h"
#include <array>

namespace Inkeys::UI::StartupPreview::Drawing
{
	inline constexpr float ProgressOuterDiameterDip = 32.f;
	inline constexpr float ProgressStrokeDip = 3.f;

	[[nodiscard]] inline HRESULT CreateShape(ID2D1Factory* factory,
		float width, float height, float scale, bool floating, ID2D1Geometry** output)
	{
		if (!factory || !output || scale <= 0.f) return E_INVALIDARG;
		if (floating)
		{
			Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
			// 只对窗口包络舍入；按钮主体保留80DIP，并与正式按钮在整数bounds中心重合。
			const float extent = 80.f * scale;
			HRESULT result = CreateSuperellipsePath(factory, extent, extent, 3.f,
				std::clamp(static_cast<int>(extent / 4.f), 24, 128), &path);
			if (FAILED(result)) return result;
			Microsoft::WRL::ComPtr<ID2D1TransformedGeometry> translated;
			const auto transform = D2D1::Matrix3x2F::Translation((width - extent) * .5f, (height - extent) * .5f);
			result = factory->CreateTransformedGeometry(path.Get(), &transform, &translated);
			if (SUCCEEDED(result)) *output = translated.Detach();
			return result;
		}
		Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> rounded;
		const auto rect = D2D1::RoundedRect(D2D1::RectF(scale * .5f, scale * .5f,
			width - scale * .5f, height - scale * .5f), 8.f * scale, 8.f * scale);
		const HRESULT result = factory->CreateRoundedRectangleGeometry(&rect, &rounded);
		if (SUCCEEDED(result)) *output = rounded.Detach();
		return result;
	}

	inline void DrawShape(ID2D1DeviceContext* context, ID2D1Geometry* shape,
		ID2D1SolidColorBrush* fill, ID2D1SolidColorBrush* frame,
		float scale, float fillAlpha, float frameAlpha)
	{
		fill->SetOpacity(fillAlpha);
		frame->SetOpacity(frameAlpha);
		context->FillGeometry(shape, fill);
		context->DrawGeometry(shape, frame, scale);
	}

	[[nodiscard]] inline HRESULT CreateRingStroke(ID2D1Factory* factory,
		ID2D1StrokeStyle** output)
	{
		const auto properties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,
			D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
		return factory->CreateStrokeStyle(&properties, nullptr, 0, output);
	}

	[[nodiscard]] inline HRESULT DrawRing(ID2D1DeviceContext* context,
		ID2D1StrokeStyle* strokeStyle, ID2D1SolidColorBrush* track,
		ID2D1SolidColorBrush* fill, D2D1_POINT_2F center, float scale,
		double ratio, float opacity, bool failed)
	{
		const float stroke = ProgressStrokeDip * scale;
		const float radius = (ProgressOuterDiameterDip - ProgressStrokeDip) * .5f * scale;
		const auto ellipse = D2D1::Ellipse(center, radius, radius);
		// 最早阶段失败可能仍是0%；只将轨道染红，不伪造已完成进度。
		track->SetColor(failed ? D2D1::ColorF(1.f, .38f, .44f, .24f)
			: D2D1::ColorF(1.f, 1.f, 1.f, .24f));
		track->SetOpacity(opacity);
		context->DrawEllipse(&ellipse, track, stroke);
		fill->SetColor(failed ? D2D1::ColorF(1.f, .38f, .44f, 1.f)
			: D2D1::ColorF(.38f, .80f, 1.f, 1.f));
		fill->SetOpacity(opacity);
		ratio = std::clamp(ratio, 0.0, 1.0);
		if (ratio <= 0.0) return S_OK;
		if (ratio >= 1.0)
		{
			context->DrawEllipse(&ellipse, fill, stroke);
			return S_OK;
		}
		constexpr double pi = 3.141592653589793;
		const double angle = -pi * .5 + ratio * 2.0 * pi;
		Microsoft::WRL::ComPtr<ID2D1Factory> factory;
		context->GetFactory(&factory);
		Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
		HRESULT result = factory->CreatePathGeometry(&path);
		if (FAILED(result)) return result;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
		result = path->Open(&sink);
		if (FAILED(result)) return result;
		// 确定式圆弧从12点顺时针推进；圆头样式不能改变真实进度的端点。
		sink->BeginFigure(D2D1::Point2F(center.x, center.y - radius), D2D1_FIGURE_BEGIN_HOLLOW);
		const D2D1_ARC_SEGMENT arc{ D2D1::Point2F(
			center.x + radius * static_cast<float>(std::cos(angle)),
			center.y + radius * static_cast<float>(std::sin(angle))),
			D2D1::SizeF(radius, radius), 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
			ratio > .5 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL };
		sink->AddArc(arc);
		sink->EndFigure(D2D1_FIGURE_END_OPEN);
		result = sink->Close();
		if (SUCCEEDED(result)) context->DrawGeometry(path.Get(), fill, stroke, strokeStyle);
		return result;
	}

	[[nodiscard]] inline HRESULT DrawLinear(ID2D1DeviceContext* context,
		ID2D1SolidColorBrush* track, ID2D1SolidColorBrush* fill,
		float width, float height, float scale, double ratio, float opacity, bool failed)
	{
		// 居中底栏恢复原192DIP横向样式；窄窗口两侧各保留12DIP。
		const float progressWidth = (std::min)(192.f * scale, (std::max)(0.f, width - 24.f * scale));
		if (progressWidth <= 0.f) return S_FALSE;
		const float centerX = width * .5f, centerY = height * .5f;
		const float trackHeight = (std::max)(1.f, scale);
		const float indicatorHeight = (std::max)(3.f, 3.f * scale);
		const auto trackRect = D2D1::RoundedRect(D2D1::RectF(
			centerX - progressWidth * .5f, centerY - trackHeight * .5f,
			centerX + progressWidth * .5f, centerY + trackHeight * .5f),
			trackHeight * .5f, trackHeight * .5f);
		// 0%最终失败仍染红轨道，不虚构已完成长度。
		track->SetColor(failed ? D2D1::ColorF(1.f, .38f, .44f, .24f)
			: D2D1::ColorF(1.f, 1.f, 1.f, .24f));
		track->SetOpacity(opacity);
		context->FillRoundedRectangle(&trackRect, track);
		const float fillWidth = progressWidth * static_cast<float>(std::clamp(ratio, 0.0, 1.0));
		if (fillWidth <= 0.f) return S_OK;
		const auto fillRect = D2D1::RoundedRect(D2D1::RectF(
			trackRect.rect.left, centerY - indicatorHeight * .5f,
			trackRect.rect.left + fillWidth, centerY + indicatorHeight * .5f),
			indicatorHeight * .5f, indicatorHeight * .5f);
		fill->SetColor(failed ? D2D1::ColorF(1.f, .38f, .44f, 1.f)
			: D2D1::ColorF(.38f, .80f, 1.f, 1.f));
		fill->SetOpacity(opacity);
		context->FillRoundedRectangle(&fillRect, fill);
		return S_OK;
	}

	[[nodiscard]] inline HRESULT DrawProgress(ID2D1DeviceContext* context,
		ID2D1StrokeStyle* ringStroke, ID2D1SolidColorBrush* track, ID2D1SolidColorBrush* fill,
		StartupPlacement placement, float width, float height, float scale,
		double ratio, float opacity, bool failed)
	{
		// 生产和离屏验证共用模式选择，避免测试重新实现另一份分支。
		return placement == StartupPlacement::CenteredBottomBar
			? DrawLinear(context, track, fill, width, height, scale, ratio, opacity, failed)
			: DrawRing(context, ringStroke, track, fill,
				D2D1::Point2F(width * .5f, height * .5f), scale, ratio, opacity, failed);
	}

	[[nodiscard]] inline HRESULT CreateShimmer(ID2D1DeviceContext* context,
		D2D1_POINT_2F start, D2D1_POINT_2F end, ID2D1LinearGradientBrush** output)
	{
		const std::array<D2D1_GRADIENT_STOP, 7> stops{
			D2D1::GradientStop(0.00f, D2D1::ColorF(1.f, 1.f, 1.f, 0.00f)),
			D2D1::GradientStop(0.24f, D2D1::ColorF(1.f, 1.f, 1.f, 0.035f)),
			D2D1::GradientStop(0.40f, D2D1::ColorF(1.f, 1.f, 1.f, 0.09f)),
			D2D1::GradientStop(0.50f, D2D1::ColorF(1.f, 1.f, 1.f, 0.20f)),
			D2D1::GradientStop(0.60f, D2D1::ColorF(1.f, 1.f, 1.f, 0.075f)),
			D2D1::GradientStop(0.78f, D2D1::ColorF(1.f, 1.f, 1.f, 0.025f)),
			D2D1::GradientStop(1.00f, D2D1::ColorF(1.f, 1.f, 1.f, 0.00f)),
		};
		Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
		const HRESULT result = context->CreateGradientStopCollection(stops.data(),
			static_cast<UINT32>(stops.size()), &collection);
		if (FAILED(result)) return result;
		return context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
			start, end), collection.Get(), output);
	}

	inline void DrawShimmer(ID2D1DeviceContext* context, ID2D1Bitmap1* mask,
		ID2D1LinearGradientBrush* brush, float width, float height)
	{
		const auto previous = context->GetAntialiasMode();
		context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
		const auto bounds = D2D1::RectF(0.f, 0.f, width, height);
		context->FillOpacityMask(mask, brush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
			&bounds, &bounds);
		context->SetAntialiasMode(previous);
	}
}
