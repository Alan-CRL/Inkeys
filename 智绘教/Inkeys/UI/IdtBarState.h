#pragma once

#include "../../IdtMain.h"

class BarStateClass
{
public:
	IdtAtomic<bool> fold = true;
};
extern BarStateClass barState;

class BarStyleClass
{
public:
	IdtAtomic<double> zoom = 2.0;
};
extern BarStyleClass barStyle;