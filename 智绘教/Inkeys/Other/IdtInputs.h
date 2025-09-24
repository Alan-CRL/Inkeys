#pragma once

#include "../../IdtMain.h"

class IdtInputs
{
private:
	IdtInputs() = delete;

public:
	static void SetKeyBoardDown(BYTE key, bool down);
	static bool IsKeyBoardDown(BYTE key);

	static void* downMap;
};