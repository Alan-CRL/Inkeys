#include "IdtText.h"

PrivateFontCollection fontCollection;
FontFamily HarmonyOS_fontFamily;
StringFormat stringFormat;
StringFormat stringFormat_left;
RECT words_rect, dwords_rect, pptwords_rect;

//string to wstring
wstring string_to_wstring(const string& s)
{
	if (s.empty()) return L"";

	int sizeRequired = MultiByteToWideChar(936, 0, s.c_str(), -1, NULL, 0);
	if (sizeRequired == 0) return L"";

	wstring ws(sizeRequired - 1, L'\0');
	MultiByteToWideChar(936, 0, s.c_str(), -1, &ws[0], sizeRequired);

	return ws;
}
//wstring to string
string wstring_to_string(const wstring& ws)
{
	if (ws.empty()) return "";

	int sizeRequired = WideCharToMultiByte(936, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
	if (sizeRequired == 0) return "";

	string s(sizeRequired - 1, '\0');
	WideCharToMultiByte(936, 0, ws.c_str(), -1, &s[0], sizeRequired, NULL, NULL);

	return s;
}

//string ת wstring
wstring convert_to_wstring(const string s)
{
	LPCSTR pszSrc = s.c_str();
	int nLen = s.size();

	int nSize = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, 0, 0);
	WCHAR* pwszDst = new WCHAR[nSize + 1];
	MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, pwszDst, nSize);
	pwszDst[nSize] = 0;
	if (pwszDst[0] == 0xFEFF) // skip Oxfeff
		for (int i = 0; i < nSize; i++)
			pwszDst[i] = pwszDst[i + 1];
	wstring wcharString(pwszDst);
	delete pwszDst;
	return wcharString;
}
//wstring ת string
string convert_to_string(const wstring str)
{
	int size = WideCharToMultiByte(CP_ACP, 0, str.c_str(), str.size(), nullptr, 0, nullptr, nullptr);
	auto p_str(std::make_unique<char[]>(size + 1));
	WideCharToMultiByte(CP_ACP, 0, str.c_str(), str.size(), p_str.get(), size, nullptr, nullptr);
	return std::string(p_str.get());
}
//c# string ת wstring
wstring bstr_to_wstring(const _bstr_t& bstr)
{
	return static_cast<wchar_t*>(bstr);
}
//string ת LPCWSTR
LPCWSTR stringtoLPCWSTR(string str)
{
	size_t size = str.length();
	int wLen = ::MultiByteToWideChar(CP_UTF8,
		0,
		str.c_str(),
		-1,
		NULL,
		0);
	wchar_t* buffer = new wchar_t[wLen + 1];
	memset(buffer, 0, (wLen + 1) * sizeof(wchar_t));
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), size, (LPWSTR)buffer, wLen);
	return buffer;
}
//string ת urlencode
string convert_to_urlencode(string str)
{
	int size(str.size() * 3 + 1);
	auto pstr(std::make_unique<char[]>(size + 1));
	int i(0);

	for (const auto& x : str)
	{
		if (x >= 0 && x <= 0x80)
		{
			if (x >= 0x41 && x <= 0x5A || x >= 0x61 && x <= 0x7A)
			{
				pstr.get()[i] = x;
				++i;
				continue;
			}
		}
		sprintf(pstr.get() + i, "%%%02X", static_cast<unsigned char>(x));
		i += 3;
	}
	str = pstr.get();
	return str;
}
//utf-8 ת GBK
string convert_to_gbk(string strUTF8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, strUTF8.c_str(), -1, NULL, 0);
	TCHAR* wszGBK = new TCHAR[len + 1];
	memset(wszGBK, 0, len * 2 + 2);
	MultiByteToWideChar(CP_UTF8, 0, strUTF8.c_str(), -1, wszGBK, len);

	len = WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, NULL, 0, NULL, NULL);
	char* szGBK = new char[len + 1];
	memset(szGBK, 0, len + 1);
	WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, szGBK, len, NULL, NULL);
	std::string strTemp(szGBK);
	delete[]szGBK;
	delete[]wszGBK;
	return strTemp;
}
//GBK ת utf-8
string convert_to_utf8(string str)
{
	wstring x = string_to_wstring(str);

	int size = WideCharToMultiByte(CP_UTF8, 0, x.c_str(), x.size(), nullptr, 0, nullptr, nullptr);
	auto p_str(std::make_unique<char[]>(size + 1));
	WideCharToMultiByte(CP_UTF8, 0, x.c_str(), x.size(), p_str.get(), size, nullptr, nullptr);
	str = p_str.get();
	return str;
}