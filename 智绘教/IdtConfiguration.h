#pragma once
#include "IdtMain.h"

struct SetListStruct
{
	SetListStruct()
	{
		StartUp = 0, CreateLnk = true;
		BrushRecover = true, RubberRecover = false;

		SetSkinMode = 0, SkinMode = 1;
	}

	int StartUp; bool CreateLnk;
	bool BrushRecover, RubberRecover;

	int SetSkinMode, SkinMode;
};
extern SetListStruct setlist;