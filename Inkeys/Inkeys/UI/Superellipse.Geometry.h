#pragma once

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Inkeys::UI
{
	// 仅构造局部路径；正式主栏和启动预览分别管理缓存及设备资源。
	[[nodiscard]] inline HRESULT CreateSuperellipsePath(ID2D1Factory* factory,
		FLOAT width, FLOAT height, FLOAT n, int segments,
		ID2D1PathGeometry** output)
	{
		if (!factory || !output || width <= 0.f || height <= 0.f || n <= 0.f)
			return E_INVALIDARG;
		constexpr FLOAT pi = 3.14159265359F;
		FLOAT radius = (std::min)(width, height) / 2.0F;
		int cornerSegments = (std::max)(6, segments / 4);
		std::vector<D2D1_POINT_2F> points;
		points.reserve(static_cast<size_t>(cornerSegments + 1) * 4 + 1);

		auto AppendCorner = [&](FLOAT centerX, FLOAT centerY, FLOAT begin, FLOAT end)
			{
				for (int i = 0; i <= cornerSegments; i++)
				{
					FLOAT theta = begin + (end - begin)
						* static_cast<FLOAT>(i) / static_cast<FLOAT>(cornerSegments);
					FLOAT cosTheta = std::cos(theta);
					FLOAT sinTheta = std::sin(theta);
					FLOAT localX = radius * std::copysign(
						std::pow(std::abs(cosTheta), 2.0F / n), cosTheta);
					FLOAT localY = radius * std::copysign(
						std::pow(std::abs(sinTheta), 2.0F / n), sinTheta);
					points.emplace_back(D2D1::Point2F(
						centerX + localX, centerY + localY));
				}
			};

		// 路径固定在局部原点，位置变化时只更新廉价的平移几何。
		AppendCorner(width - radius, radius, -pi / 2.0F, 0.0F);
		AppendCorner(width - radius, height - radius, 0.0F, pi / 2.0F);
		AppendCorner(radius, height - radius, pi / 2.0F, pi);
		AppendCorner(radius, radius, pi, pi * 3.0F / 2.0F);
		points.emplace_back(points.front());

		std::vector<D2D1_BEZIER_SEGMENT> beziers;
		int pointCount = static_cast<int>(points.size()) - 1;
		if (pointCount < 3) return E_INVALIDARG;
		beziers.reserve(pointCount);
		for (int i = 0; i < pointCount; i++)
		{
			D2D1_POINT_2F p0 = points[(i - 1 + pointCount) % pointCount];
			D2D1_POINT_2F p1 = points[i];
			D2D1_POINT_2F p2 = points[(i + 1) % pointCount];
			D2D1_POINT_2F p3 = points[(i + 2) % pointCount];
			beziers.push_back({
				{ p1.x + (p2.x - p0.x) / 6.0F,
					p1.y + (p2.y - p0.y) / 6.0F },
				{ p2.x - (p3.x - p1.x) / 6.0F,
					p2.y - (p3.y - p1.y) / 6.0F },
				p2 });
		}

		Microsoft::WRL::ComPtr<ID2D1PathGeometry> nextLocalGeometry;
		HRESULT hr = factory->CreatePathGeometry(&nextLocalGeometry);
		if (FAILED(hr) || !nextLocalGeometry) return FAILED(hr) ? hr : E_FAIL;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
		hr = nextLocalGeometry->Open(&sink);
		if (FAILED(hr) || !sink) return FAILED(hr) ? hr : E_FAIL;
		sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_FILLED);
		sink->AddBeziers(beziers.data(), static_cast<UINT32>(beziers.size()));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		hr = sink->Close();
		if (FAILED(hr)) return hr;
		*output = nextLocalGeometry.Detach();
		return S_OK;
	}
}
