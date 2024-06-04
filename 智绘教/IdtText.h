#pragma once
#include "IdtMain.h"

extern PrivateFontCollection fontCollection;
extern FontFamily HarmonyOS_fontFamily;
extern StringFormat stringFormat;
extern StringFormat stringFormat_left;
extern RECT words_rect, dwords_rect, pptwords_rect;

//string to wstring
wstring StringToWstring(const string& s);
//wstring to string
string WstringToString(const wstring& ws);

//c# string to wstring
wstring BstrToWstring(const _bstr_t& bstr);
//wstring to c# string
_bstr_t WstringToBstr(const wstring& str);

//string to LPCWSTR
LPCWSTR StringToLPCWSTR(string str);
//string to urlencode
string StringToUrlencode(string str);

//utf-8 to GBK
string ConvertToGbk(string strUTF8);
//GBK to utf-8
string ConvertToUtf8(string str);