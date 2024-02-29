#pragma once
#include "IdtMain.h"

struct SetListStruct
{
	SetListStruct()
	{
		StartUp = 0, CreateLnk = false;
		BrushRecover = true, RubberRecover = false;
		RubberMode = 0;

		IntelligentDrawing = true, SmoothWriting = true;

		SetSkinMode = 0, SkinMode = 1;
	}

	int StartUp; bool CreateLnk;
	bool BrushRecover, RubberRecover;
	int RubberMode;

	bool IntelligentDrawing, SmoothWriting;

	int SetSkinMode, SkinMode;
};
extern SetListStruct setlist;