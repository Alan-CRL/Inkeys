#pragma once

#include "../IdtMain.h"

bool hasUiAccess(HANDLE tok);
void SurperTopMain(wstring lpCmdLine);

extern IdtAtomic<bool> hasSuperTop;
void LaunchSurperTop();