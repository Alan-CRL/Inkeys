#pragma once
#include "IdtMain.h"

#include "IdtWindow.h"
#include "IdtTime.h"
#include "IdtOther.h"

//程序崩溃保护
void CrashedHandler();
//程序自动更新
wstring get_domain_name(wstring url);
wstring convertToHttp(const wstring& url);

extern int AutomaticUpdateStep;
void AutomaticUpdate();

//程序注册 + 网络登记
//用户量过大后被废弃
//void NetUpdate();