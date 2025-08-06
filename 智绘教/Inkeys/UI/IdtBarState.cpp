#include "IdtBarState.h"

#include "IdtBarBottom.h"
#include "../../IdtState.h"

void BarStateClass::CalcButtomState()
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) barButtomState[(int)BarButtomPresetEnum::Select].state = BarWidgetState::Selected;
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen) barButtomState[(int)BarButtomPresetEnum::Draw].state = BarWidgetState::Selected;
}

BarStateClass barState;
BarStyleClass barStyle;

map<int, BarButtomStateClass> barButtomState;