#include "IdtBarState.h"

#include "IdtBarBottom.h"

#include "../../IdtState.h"
#include "../../IdtDraw.h"

void BarStateClass::CalcButtomState()
{
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) barButtomState[(int)BarButtomPresetEnum::Select].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Select].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen) barButtomState[(int)BarButtomPresetEnum::Draw].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Draw].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser) barButtomState[(int)BarButtomPresetEnum::Eraser].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Eraser].state = BarWidgetState::None;
	}

	{
		if (penetrate.select) barButtomState[(int)BarButtomPresetEnum::Pierce].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Pierce].state = BarWidgetState::None;
	}
	{
		if (FreezeFrame.mode == 1) barButtomState[(int)BarButtomPresetEnum::Freeze].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Freeze].state = BarWidgetState::None;
	}

	{
		if (test.select) barButtomState[(int)BarButtomPresetEnum::Setting].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Setting].state = BarWidgetState::None;
	}
}

BarStateClass barState;
BarStyleClass barStyle;

map<int, BarButtomStateClass> barButtomState;