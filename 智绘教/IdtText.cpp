#include "IdtText.h"

PrivateFontCollection fontCollection;
FontFamily HarmonyOS_fontFamily;
StringFormat stringFormat;
StringFormat stringFormat_left;
RECT words_rect, dwords_rect, pptwords_rect;

//wstring utf8ToUtf16(const string& input)
//{
//	wstring_convert<codecvt_utf8<wchar_t>, wchar_t> converter;
//	return converter.from_bytes(input);
//}
//string utf16ToUtf8(const wstring& input)
//{
//	wstring_convert<codecvt_utf8<wchar_t>, wchar_t> converter;
//	return converter.to_bytes(input);
//}
wstring utf8ToUtf16(const string& input)
{
	if (input.empty()) return wstring();
	int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), nullptr, 0);
	wstring result(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), &result[0], len);
	return result;
}
string utf16ToUtf8(const wstring& input)
{
	if (input.empty()) return string();
	int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), (int)input.size(), nullptr, 0, nullptr, nullptr);
	string result(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, input.data(), (int)input.size(), &result[0], len, nullptr, nullptr);
	return result;
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

vector<wstring> CustomSplit::Run(const wstring& input, wchar_t custom_sep)
{
	vector<wstring> result;
	wstring token;

	bool in_quote = false;
	bool in_custom = false;

	for (size_t i = 0; i < input.size(); i++)
	{
		wchar_t ch = input[i];

		if (!in_quote && !in_custom && (ch == L' ' || ch == L'\t'))
		{
			if (!token.empty())
			{
				result.push_back(token);
				token.clear();
			}
			continue;
		}

		// 处理自定义分隔符
		if (!in_quote && ch == custom_sep && !in_custom)
		{
			in_custom = true;
			token += ch; // 保留起始分隔符
			continue;
		}
		else if (in_custom && ch == custom_sep)
		{
			in_custom = false;
			token += ch; // 保留结束分隔符
			continue;
		}

		// 处理双引号分隔符
		if (!in_custom && ch == L'\"' && !in_quote)
		{
			in_quote = true;
			token += ch; // 保留起始分隔符
			continue;
		}
		else if (in_quote && ch == L'\"')
		{
			in_quote = false;
			token += ch; // 保留结束分隔符
			continue;
		}

		// 普通字符
		token += ch;
	}

	if (!token.empty())
	{
		result.push_back(token);
	}
	return result;
}