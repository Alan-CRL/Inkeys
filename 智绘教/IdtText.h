#pragma once
#include "IdtMain.h"

extern PrivateFontCollection fontCollection;
extern FontFamily HarmonyOS_fontFamily;
extern StringFormat stringFormat;
extern StringFormat stringFormat_left;
extern RECT words_rect, dwords_rect, pptwords_rect;

wstring utf8ToUtf16(const string& input);
string utf16ToUtf8(const wstring& input);

wstring bstrToWstring(const _bstr_t& bstr);
_bstr_t wstringToBstr(const wstring& str);

string StringToUrlencode(const string& str);