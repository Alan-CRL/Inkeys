#pragma once

#include "../IdtMain.h"

enum class LaunchStateEnum : int
{
	Normal,
	Restart,
	WarnTry,
	CrashTry
};
extern LaunchStateEnum launchState;
extern HANDLE launchMutex;