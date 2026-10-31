#pragma once

#include "../IdtMain.h"

bool hasUiAccess(HANDLE tok);
void SurperTopMain(wstring lpCmdLine);
int RunSuperTopTokenFailureTest() noexcept;

extern IdtAtomic<bool> hasSuperTop;
void LaunchSurperTop(wstring cmdLine);
