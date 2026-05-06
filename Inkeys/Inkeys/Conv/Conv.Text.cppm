module;

#include "../../IdtMain.h"

export module Inkeys.Conv.Text;

export wstring utf8ToUtf16(string_view input)
{
	if (input.empty()) return wstring();
	int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), nullptr, 0);
	wstring result(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), &result[0], len);
	return result;
}
export string utf16ToUtf8(wstring_view input)
{
	if (input.empty()) return string();
	int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), (int)input.size(), nullptr, 0, nullptr, nullptr);
	string result(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, input.data(), (int)input.size(), &result[0], len, nullptr, nullptr);
	return result;
}

export wstring bstrToWstring(const _bstr_t& bstr)
{
	return static_cast<wchar_t*>(bstr);
}
export _bstr_t wstringToBstr(const wstring& str)
{
	return _bstr_t(str.c_str());
}

export string StringToUrlencode(const string& str)
{
	ostringstream escaped;
	escaped.fill('0');
	escaped << hex;

	for (unsigned char c : str)
	{
		const bool isUnreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
		if (isUnreserved) escaped << c;
		else
		{
			escaped << '%' << uppercase << setw(2) << int(c);
			escaped << nouppercase;
		}
	}
	return escaped.str();
}