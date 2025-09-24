#pragma once

#include "../IdtMain.h"

class LaunchState
{
public:
	inline static bool restart = false;
	inline static bool warnTry = false;
	inline static bool crashTry = false;

	inline static bool superTop = false;
	inline static wstring superTopVal = L"";

	inline static wstring commandLine = L"";
private:
	LaunchState() = delete;
};

extern HANDLE launchMutex;