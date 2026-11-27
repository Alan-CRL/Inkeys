#pragma once
#include "IdtMain.h"

extern int FreezeRecall;

void SetWhiteboardFreezeSurfaceOwned(bool owned) noexcept;
bool WhiteboardFreezeSurfaceOwned() noexcept;
bool SubmitFreezeSurface(HWND hwnd, UPDATELAYEREDWINDOWINFO* info,
	bool whiteboardOwner, bool* apiFailed = nullptr) noexcept; // apiFailed：ULW 真实失败（白板接管导致的拒绝不算）
void FreezeFrameWindow();
int RunFreezeSurfaceResourceLifecycleTest() noexcept;
