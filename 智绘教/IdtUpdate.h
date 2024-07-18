#pragma once
#include "IdtMain.h"

//程序崩溃保护
void CrashedHandler();

//程序自动更新
extern int AutomaticUpdateStep;
wstring get_domain_name(wstring url);
wstring convertToHttp(const wstring& url);
void AutomaticUpdate();