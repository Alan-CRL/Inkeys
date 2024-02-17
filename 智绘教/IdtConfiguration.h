#pragma once
#include "IdtMain.h"

struct SetListStruct
{
	SetListStruct()
	{
		StartUp = 0;
		BrushRecover = true, RubberRecover = false;

		SetSkinMode = 0, SkinMode = 1;
	}

	int StartUp;
	bool BrushRecover, RubberRecover;

	int SetSkinMode, SkinMode;
};
extern SetListStruct setlist;