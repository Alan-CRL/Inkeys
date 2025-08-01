#pragma once

#include "../../IdtMain.h"

class BarStateClass
{
public:
	IdtAtomic<bool> fold = true;
};
extern BarStateClass barState;