#pragma once
#include <windows.h>

class Win32FontManager {
    double fontSize = 15;         //字体大小
    int weight = 400;             //字体粗细
    BOOL italic = FALSE;          //斜体
    BOOL underline = FALSE;       //下划线
    BOOL strikeout = FALSE;       //删除线
    LPCWSTR fontName = L"宋体";   //字体名
    HFONT hFont;
    HWND hWnd = 0;
public:
    Win32FontManager();
    Win32FontManager(HWND hWnd, double size = 15, int weight = 400, BOOL italic = FALSE, BOOL underline = FALSE, BOOL strikeout = FALSE, LPCWSTR fontName = L"宋体");
    ~Win32FontManager();
    void setHWND(HWND hWnd);
    void font();
    void SetFontSize(double size);
    double FontSize();
    void SetFontWeight(int weight);
    int FontWeight();
    void SetFontItalic(BOOL italic);
    BOOL FontItalic();
    void SetFontUnderline(BOOL underline);
    BOOL FontUnderline();
    void SetFontStrikeout(BOOL strikeout);
    BOOL FontStrikeout();
    void SetFontName(LPCWSTR fontName);
    LPCWSTR SetFontName();
};

