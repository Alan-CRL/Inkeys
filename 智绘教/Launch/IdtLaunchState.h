#pragma once

#include "../IdtMain.h"

enum class LaunchStateEnum : int
{
	Normal,
	Restart,
	WarnTry
};
extern LaunchStateEnum launchState;
extern HANDLE launchMutex;