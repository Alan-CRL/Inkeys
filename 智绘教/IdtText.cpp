#include "IdtText.h"

PrivateFontCollection fontCollection;
FontFamily HarmonyOS_fontFamily;
StringFormat stringFormat;
StringFormat stringFormat_left;
RECT words_rect, dwords_rect, pptwords_rect;

wstring utf8ToUtf16(const string& input)
{
	wstring_convert<codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.from_bytes(input);
}
string utf16ToUtf8(const wstring& input)
{
	wstring_convert<codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.to_bytes(input);
}

wstring bstrToWstring(const _bstr_t& bstr)
{
	return static_cast<wchar_t*>(bstr);
}
_bstr_t wstringToBstr(const wstring& str)
{
	return _bstr_t(str.c_str());
}

string StringToUrlencode(const string& str)
{
	ostringstream escaped;
	escaped.fill('0');
	escaped << hex;

	for (unsigned char c : str)
	{
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') escaped << c;
		else
		{
			escaped << '%' << uppercase << setw(2) << int(c);
			escaped << nouppercase;
		}
	}
	return escaped.str();
}