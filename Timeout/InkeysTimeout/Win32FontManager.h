#pragma once
#include <windows.h>

class Win32FontManager {
    double fontSize = 15;         //�����С
    int weight = 400;             //�����ϸ
    BOOL italic = FALSE;          //б��
    BOOL underline = FALSE;       //�»���
    BOOL strikeout = FALSE;       //ɾ����
    LPCWSTR fontName = L"����";   //������
    HFONT hFont;
    HWND hWnd = 0;
public:
    Win32FontManager();
    Win32FontManager(HWND hWnd, double size = 15, int weight = 400, BOOL italic = FALSE, BOOL underline = FALSE, BOOL strikeout = FALSE, LPCWSTR fontName = L"����");
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

