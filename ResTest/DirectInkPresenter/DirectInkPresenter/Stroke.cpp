#include "pch.h"
#include "Stroke.h"

DirectInkPresenter::Ink::Stroke::Stroke(ID2D1Factory* d2dFactory, ID2D1StrokeStyle* d2dStrokeStyle, D2D1_COLOR_F d2dColor, FLOAT strokeWidth) :
	m_d2dFactory(d2dFactory),
	m_d2dStrokeStyle(d2dStrokeStyle),
	m_d2dColor(d2dColor),
	m_strokeWidth(strokeWidth)
{
	UuidCreateSequential(&m_strokeID);
}

void DirectInkPresenter::Ink::Stroke::GetSmoothingPoints(int i, D2D1_POINT_2F* point1, D2D1_POINT_2F* point2)
{
	// 平滑程度
	static const float smoothing = 0.25f;

	// 计算上一点与当前点的距离
	float dx = at(i).x - at(i - 1).x;
	float dy = at(i).y - at(i - 1).y;
	float currentDelta = std::sqrt(dx * dx + dy * dy);

	// 计算再上一点与当前点的距离
	float prevDx = at(i).x - at(i - 2).x;
	float prevDy = at(i).y - at(i - 2).y;
	float prevDelta = std::sqrt(prevDx * prevDx + prevDy * prevDy);

	// 计算上一点与下一个点的距离
	float nextDx = at(i + 1).x - at(i - 1).x;
	float nextDy = at(i + 1).y - at(i - 1).y;
	float nextDelta = std::sqrt(nextDx * nextDx + nextDy * nextDy);

	// 存储point 1
	*point1 = D2D1::Point2F(
		at(i - 1).x + (prevDx == 0 ? 0 : (smoothing * currentDelta * prevDx / prevDelta)),
		at(i - 1).y + (prevDy == 0 ? 0 : (smoothing * currentDelta * prevDy / prevDelta))
	);

	// 存储point 2
	*point2 = D2D1::Point2F(
		at(i).x - (nextDx == 0 ? 0 : (smoothing * currentDelta * nextDx / nextDelta)),
		at(i).y - (nextDy == 0 ? 0 : (smoothing * currentDelta * nextDy / nextDelta))
	);
}

void DirectInkPresenter::Ink::Stroke::Reset()
{
	Utils::ComPtr<ID2D1PathGeometry> d2dGeometry = nullptr;
	Utils::ComPtr<ID2D1GeometrySink> d2dSink = nullptr;
	Utils::ThrowIfFailed(
	    m_d2dFactory ? S_OK : E_POINTER
	);
	// 将线条数据写入一个临时的路径几何
	Utils::ThrowIfFailed(
	    m_d2dFactory->CreatePathGeometry(&d2dGeometry)
	);
	Utils::ThrowIfFailed(
	    d2dGeometry->Open(&d2dSink)
	);
	d2dSink->SetFillMode(D2D1_FILL_MODE_WINDING);
	d2dSink->BeginFigure(
	    at(0),
		D2D1_FIGURE_BEGIN_HOLLOW
	);
	for (size_t i = 1; i < size(); i++)
	{
		// 使其变得平滑
		if (i > 1 && i < size() - 1)
		{
			D2D1_POINT_2F point1;
			D2D1_POINT_2F point2;
			GetSmoothingPoints(static_cast<int>(i), &point1, &point2);
			d2dSink->AddBezier(
				D2D1::BezierSegment(
					point1,
					point2,
					at(i)
				)
			);
		}
		else
		{
			d2dSink->AddLine(at(i));
		}
		/*d2dSink->AddBezier(
		    UI::Graphics::BezierSegment(
		        at(min(i + 1, size() - 1)),
		        at(min(i + 2, size() - 1)),
		        at(min(i + 3, size() - 1))
		    )
		);*/
	}
	d2dSink->EndFigure(D2D1_FIGURE_END_OPEN);
	d2dSink->Close();
	// 将临时的路径几何扩大并复制到路径几何
	Utils::ThrowIfFailed(
	    m_d2dFactory->CreatePathGeometry(&m_d2dGeometry)
	);
	Utils::ThrowIfFailed(
	    m_d2dGeometry->Open(&d2dSink)
	);
	Utils::ThrowIfFailed(
		d2dGeometry->Widen(
			m_strokeWidth,
			m_d2dStrokeStyle.Get(),
			UI::Graphics::Matrix3x2F::Identity(),
			d2dSink.Get()
		)
	);
	Utils::ThrowIfFailed(
		d2dSink->Close()
	);
}

void DirectInkPresenter::Ink::Stroke::Add(D2D1_POINT_2F d2dPoint)
{
	Utils::ComPtr<ID2D1PathGeometry> d2dGeometry = nullptr;
	Utils::ComPtr<ID2D1GeometrySink> d2dSink = nullptr;

	push_back(d2dPoint);
	Reset();
}

void DirectInkPresenter::Ink::Stroke::Remove(D2D1_POINT_2F d2dPoint)
{
	for (auto it = begin(); it != end();)
	{
		if (it->x == d2dPoint.x and it->y == d2dPoint.y)
		{
			it = erase(it);
		}
		else
		{
			it++;
		}
	}
	Reset();
}