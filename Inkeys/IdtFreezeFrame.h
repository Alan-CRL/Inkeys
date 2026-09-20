#pragma once
#include "IdtMain.h"

extern int FreezeRecall;

void SetWhiteboardFreezeSurfaceOwned(bool owned) noexcept;
bool WhiteboardFreezeSurfaceOwned() noexcept;
bool SubmitFreezeSurface(HWND hwnd, UPDATELAYEREDWINDOWINFO* info,
	bool whiteboardOwner) noexcept;
void FreezeFrameWindow();
