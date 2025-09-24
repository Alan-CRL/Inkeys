#pragma once

#include "../../IdtMain.h"

class IdtGesture
{
private:
	IdtGesture() = delete;

public:
	static BOOL DisableEdgeGestures(HWND hwnd, BOOL disable);
};