#include "i18n.h"
#include "json/json.h"
using namespace std;

string u2g(const char* src_str) { // Utf8ToGbk
    int len = MultiByteToWideChar(CP_UTF8, 0, src_str, -1, NULL, 0);
    wchar_t* wszGBK = new wchar_t[len + 1];
    memset(wszGBK, 0, len * 2 + 2);
    MultiByteToWideChar(CP_UTF8, 0, src_str, -1, wszGBK, len);
    len = WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, NULL, 0, NULL, NULL);
    char* szGBK = new char[len + 1];
    memset(szGBK, 0, len + 1);
    WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, szGBK, len, NULL, NULL);
    string strTemp(szGBK);
    delete[] wszGBK;
    delete[] szGBK;
    return strTemp;
}

Json::Value i18n;
void loadI18n(int id) {
    HRSRC hRes = FindResourceExW(NULL, TEXT("JSON"), MAKEINTRESOURCE(id), 2052);
    if (hRes)
    {
        DWORD len = ::SizeofResource(NULL, hRes);
        char* data = (char*)LockResource(LoadResource(NULL, hRes));
        std::string sdata("");
        for (int i = 0; i < len; i++)
            sdata += data[i];
        (new Json::Reader())->parse(u2g(sdata.c_str()), i18n);
    }
}

string i18n_get(string v) {
    if (i18n.isMember(v.c_str()) && i18n[v].isString())
        return i18n[v].asString();
    else
        return v;
}