module;

#include "../../../IdtMain.h"

#include "../../../IdtState.h"
#include "Bar.BottomDock.h"

module Inkeys.UI.Bar;
import :State;

import :Main;

void BarStateClass::PositionUpdate(double tarZoom)
{
	if (Inkeys::UI::Bar::WhiteboardDockLockActive())
	{
		// 白板首次自动贴底期间保留进入前的左右展开方向，仅固定次级面板向上。
		widgetPosition.primaryBar = false;
		return;
	}
	if (barUISet.IsBottomDockLayoutLocked())
	{
		// 底栏及脱离回弹阶段仍强制次级面板向上，避免纵向翻边。
		widgetPosition.primaryBar = false;
		const auto presented = barUISet.BottomDockPresentedSnapshot();
		const double x = barUISet.superellipseMap[
			BarUISetSuperellipseEnum::MainButton]->GetX() * tarZoom;
		const double windowWidth = static_cast<double>(barUISet.barWindow.w);
		widgetPosition.mainBar =
			Inkeys::UI::Bar::ResolveBarBottomDockPositionMainBarSide(
				presented.centerMode, x, windowWidth,
				widgetPosition.mainBar);
		return;
	}

	// 获取主按钮中心位置
	double x = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetX() * tarZoom;
	double y = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetY() * tarZoom;
	double monW = static_cast<double>(barUISet.barWindow.w);
	double monH = static_cast<double>(barUISet.barWindow.h);

	// 判断主栏所有位置
	if (x <= monW / 2.0) widgetPosition.mainBar = true;
	else widgetPosition.mainBar = false;

	// 判断菜单上下位置
	if (y <= monH * 2.0 / 5.0) widgetPosition.primaryBar = true;
	else widgetPosition.primaryBar = false;

	//Testa(to_string(x) + " " + to_string(monW / 2.0));
}
void BarStateClass::ThicknessDisplayUpdate()
{
	int penThickness = static_cast<int>(GetPenWidth());
	wstring tar = L"粗细" + format(L" {:>3}", clamp(penThickness, 0, 999));

	barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->content.SetTar(tar);
}
