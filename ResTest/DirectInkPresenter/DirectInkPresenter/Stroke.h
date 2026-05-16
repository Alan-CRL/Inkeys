#pragma once
#include "pch.h"
#include "Utils.h"
#include "Graphics.h"

namespace DirectInkPresenter
{
	namespace Ink
	{
		class Stroke : std::vector<D2D1_POINT_2F>
		{
		public:
			friend class StrokeCollection;
			Stroke(ID2D1Factory* d2dFactory, ID2D1StrokeStyle* d2dStrokeStyle, D2D1_COLOR_F d2dColor, FLOAT strokeWidth);
			virtual ~Stroke() = default;

			void SetVisibility(bool bVisible) { m_strokeVisibility = bVisible; }
			bool GetVisibility() const { return m_strokeVisibility; }
			UUID GetUID() const { return m_strokeID; }
			virtual void Add(D2D1_POINT_2F d2dPoint);
			virtual void Remove(D2D1_POINT_2F d2dPoint);
			virtual void Reset();
			void GetSmoothingPoints(int i, D2D1_POINT_2F* point1, D2D1_POINT_2F* point2);

			std::vector<D2D1_POINT_2F>& GetRawPoints() { return *this; }
			ID2D1PathGeometry* GetPathGeometry() const { return m_d2dGeometry.Get(); }
		protected:
			bool m_strokeVisibility = false;
			UUID m_strokeID = {};
			FLOAT m_strokeWidth = 5;
			D2D1_COLOR_F m_d2dColor = UI::Graphics::ColorF(UI::Graphics::ColorF::Red);
			Utils::ComPtr<ID2D1Factory> m_d2dFactory = nullptr;
			Utils::ComPtr<ID2D1StrokeStyle> m_d2dStrokeStyle = nullptr;
			Utils::ComPtr<ID2D1PathGeometry> m_d2dGeometry = nullptr;
		};
	}
}