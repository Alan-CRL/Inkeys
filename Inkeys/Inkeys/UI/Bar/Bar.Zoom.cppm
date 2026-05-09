module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:Zoom;

import :Main;
import :Atomic;

import Inkeys.Other.Config;

namespace
{
	constexpr double BarConfigZoomMin = 0.50;
	constexpr double BarConfigZoomMax = 2.00;

	double NormalizeConfigZoom(double configZoom)
	{
		if (!isfinite(configZoom)) return 1.0;
		return clamp(configZoom, BarConfigZoomMin, BarConfigZoomMax);
	}

	double RoundConfigZoom(double configZoom)
	{
		return round(NormalizeConfigZoom(configZoom) * 100.0) / 100.0;
	}

	double FloorConfigZoom(double configZoom)
	{
		if (!isfinite(configZoom)) return 1.0;
		return clamp(floor(configZoom * 100.0) / 100.0, BarConfigZoomMin, BarConfigZoomMax);
	}

	shared_ptr<BarUiSuperellipseClass> GetMainButton(BarUISetClass& barUISet)
	{
		auto it = barUISet.superellipseMap.find(BarUISetSuperellipseEnum::MainButton);
		if (it == barUISet.superellipseMap.end()) return nullptr;
		return it->second;
	}

	shared_ptr<BarUiShapeClass> GetMainBar(BarUISetClass& barUISet)
	{
		auto it = barUISet.shapeMap.find(BarUISetShapeEnum::MainBar);
		if (it == barUISet.shapeMap.end()) return nullptr;
		return it->second;
	}

	double GetStartupDpiZoom()
	{
		HDC screenDC = GetDC(nullptr);
		double scale = 1.0;

		if (screenDC)
		{
			int dpiX = GetDeviceCaps(screenDC, LOGPIXELSX);
			ReleaseDC(nullptr, screenDC);

			scale = static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
		}

		if (!isfinite(scale) || scale <= 0.0) scale = 1.0;
		return scale;
	}

	void RefreshZoom(BarUISetClass& barUISet)
	{
		barUISet.barStyle.zoom = barUISet.barStyle.dpiZoom.load() * barUISet.barStyle.configZoom.load();
	}

	void SetMainButtonPosition(BarUiSuperellipseClass& mainButton, double x, double y)
	{
		mainButton.x.tar = x;
		mainButton.x.val = x;
		mainButton.x.startV = x;
		mainButton.y.tar = y;
		mainButton.y.val = y;
		mainButton.y.startV = y;
	}

	void ResetMainButtonDefaultPosition(BarUISetClass& barUISet)
	{
		auto mainButton = GetMainButton(barUISet);
		if (!mainButton) return;

		double zoom = barUISet.barStyle.zoom;
		if (!isfinite(zoom) || zoom <= 0.0) return;

		double mainX = static_cast<double>(barUISet.barWindow.x + barUISet.barWindow.w - 80 - 50) / zoom;
		double mainY = static_cast<double>(barUISet.barWindow.y + barUISet.barWindow.h - 80 - 200) / zoom;
		SetMainButtonPosition(*mainButton, mainX, mainY);
	}

	bool ClampMainButtonToScreen(BarUISetClass& barUISet)
	{
		auto mainButton = GetMainButton(barUISet);
		if (!mainButton) return false;

		double zoom = barUISet.barStyle.zoom;
		if (!isfinite(zoom) || zoom <= 0.0) return false;

		double frameHalf = 0.0;
		if (mainButton->ft.has_value()) frameHalf = max(0.0, mainButton->ft.value().tar / 2.0);

		double minX = mainButton->GetW() / 2.0 + frameHalf;
		double minY = mainButton->GetH() / 2.0 + frameHalf;
		double maxX = static_cast<double>(barUISet.barWindow.w) / zoom - mainButton->GetW() / 2.0 - frameHalf;
		double maxY = static_cast<double>(barUISet.barWindow.h) / zoom - mainButton->GetH() / 2.0 - frameHalf;

		if (maxX < minX) maxX = minX;
		if (maxY < minY) maxY = minY;

		double nextX = clamp(static_cast<double>(mainButton->x.tar), minX, maxX);
		double nextY = clamp(static_cast<double>(mainButton->y.tar), minY, maxY);
		bool changed = abs(nextX - static_cast<double>(mainButton->x.tar)) > 0.000001
			|| abs(nextY - static_cast<double>(mainButton->y.tar)) > 0.000001;

		SetMainButtonPosition(*mainButton, nextX, nextY);
		return changed;
	}

	void ApplyConfigZoom(BarUISetClass& barUISet, double configZoom, bool keepCurrentScreenPosition)
	{
		configZoom = RoundConfigZoom(configZoom);

		double oldZoom = barUISet.barStyle.zoom;
		auto mainButton = GetMainButton(barUISet);
		double screenX = 0.0, screenY = 0.0;
		if (mainButton && isfinite(oldZoom) && oldZoom > 0.0)
		{
			screenX = mainButton->x.tar * oldZoom;
			screenY = mainButton->y.tar * oldZoom;
		}

		barUISet.barStyle.configZoom = configZoom;
		RefreshZoom(barUISet);

		double newZoom = barUISet.barStyle.zoom;
		if (!mainButton || !isfinite(newZoom) || newZoom <= 0.0) return;

		if (keepCurrentScreenPosition && isfinite(oldZoom) && oldZoom > 0.0)
			SetMainButtonPosition(*mainButton, screenX / newZoom, screenY / newZoom);
		else
			ResetMainButtonDefaultPosition(barUISet);

		ClampMainButtonToScreen(barUISet);
		barUISet.barState.PositionUpdate(newZoom);
		BarAtomic::renderOnceFlag = true;
		barUISet.UpdateRendering();
	}
}

export namespace Inkeys::UI::Bar::Zoom
{
	void Initialize(BarUISetClass& barUISet)
	{
		double configZoom = RoundConfigZoom(Inkeys::config.UI.Bar.Zoom.load());
		Inkeys::config.UI.Bar.Zoom = configZoom;

		barUISet.barStyle.dpiZoom = GetStartupDpiZoom();
		barUISet.barStyle.configZoom = configZoom;
		RefreshZoom(barUISet);
		barUISet.barStyle.initialZoomFitPending = true;
	}

	void FitInitialAfterMainBarLayout(BarUISetClass& barUISet, double mainBarWidth)
	{
		if (!barUISet.barStyle.initialZoomFitPending.exchange(false)) return;

		auto mainButton = GetMainButton(barUISet);
		auto mainBar = GetMainBar(barUISet);
		if (!mainButton || !mainBar) return;

		double dpiZoom = barUISet.barStyle.dpiZoom;
		double currentZoom = barUISet.barStyle.zoom;
		if (!isfinite(dpiZoom) || dpiZoom <= 0.0 || !isfinite(currentZoom) || currentZoom <= 0.0) return;
		if (!isfinite(mainBarWidth) || mainBarWidth <= 0.0) return;

		if (ClampMainButtonToScreen(barUISet))
		{
			barUISet.barState.PositionUpdate(currentZoom);
			BarAtomic::renderOnceFlag = true;
		}

		if (abs(barUISet.barStyle.configZoom.load() - 1.0) > 0.000001) return;

		double requiredWidth = mainButton->GetW() + 10.0 + mainBarWidth;
		double requiredHeight = max(mainButton->GetH(), mainBar->GetH());
		double windowWidth = static_cast<double>(barUISet.barWindow.w);
		double windowHeight = static_cast<double>(barUISet.barWindow.h);
		if (requiredWidth <= 0.0 || requiredHeight <= 0.0 || windowWidth <= 0.0 || windowHeight <= 0.0) return;

		if (requiredWidth * currentZoom <= windowWidth && requiredHeight * currentZoom <= windowHeight) return;

		double fitZoom = min(windowWidth / requiredWidth, windowHeight / requiredHeight);
		if (!isfinite(fitZoom) || fitZoom <= 0.0 || fitZoom >= currentZoom) return;

		double fitConfigZoom = FloorConfigZoom(fitZoom / dpiZoom);
		if (fitConfigZoom >= barUISet.barStyle.configZoom.load()) return;

		Inkeys::config.UI.Bar.Zoom = fitConfigZoom;
		ApplyConfigZoom(barUISet, fitConfigZoom, false);
		Inkeys::config.Write();
	}
}

export namespace Inkeys::UI::Bar
{
	void SetConfigZoom(double configZoom)
	{
		configZoom = RoundConfigZoom(configZoom);

		if (!useInkeys3UI)
		{
			barUISet.barStyle.configZoom = configZoom;
			RefreshZoom(barUISet);
			return;
		}

		ApplyConfigZoom(barUISet, configZoom, true);
	}
}
