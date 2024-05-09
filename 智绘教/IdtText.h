#pragma once
#include "IdtMain.h"

extern PrivateFontCollection fontCollection;
extern FontFamily HarmonyOS_fontFamily;
extern StringFormat stringFormat;
extern StringFormat stringFormat_left;
extern RECT words_rect, dwords_rect, pptwords_rect;

//string to wstring
wstring string_to_wstring(const string& s);
//wstring to string
string wstring_to_string(const wstring& ws);

//string ת wstring
wstring convert_to_wstring(const string s);
//wstring ת string
string convert_to_string(const wstring str);

//c# string to wstring
wstring bstr_to_wstring(const _bstr_t& bstr);
//wstring to c# string
_bstr_t wstring_to_bstr(const wstring& str);

//string ת LPCWSTR
LPCWSTR stringtoLPCWSTR(string str);
//string ת urlencode
string convert_to_urlencode(string str);
//utf-8 ת GBK
string convert_to_gbk(string strUTF8);
//GBK ת utf-8
string convert_to_utf8(string str);