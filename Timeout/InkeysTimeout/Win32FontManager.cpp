#include "Win32FontManager.h"

Win32FontManager::Win32FontManager() {
    font();
};

void Win32FontManager::font() {
    DeleteObject(hFont);
    hFont = CreateFont(-fontSize, -fontSize / 2.0, 0, 0, weight, italic, underline, strikeout, DEFAULT_CHARSET, OUT_CHARACTER_PRECIS, CLIP_CHARACTER_PRECIS, DEFAULT_QUALITY, FF_DONTCARE, fontName);
    SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, 0);
    UpdateWindow(hWnd);
}

void Win32FontManager::SetFontSize(double _size) {
    this->fontSize = _size;
    font();
}

double Win32FontManager::FontSize() {
    return this->fontSize;
}

void Win32FontManager::SetFontWeight(int _weight) {
    this->weight = _weight;
    font();
}

int Win32FontManager::FontWeight() {
    return this->weight;
}

void Win32FontManager::SetFontItalic(BOOL _italic) {
    this->italic = _italic;
    font();
}

BOOL Win32FontManager::FontItalic() {
    return this->italic;
}

void Win32FontManager::SetFontUnderline(BOOL _underline) {
    this->underline = _underline;
    font();
}

BOOL Win32FontManager::FontUnderline() {
    return this->underline;
}

void Win32FontManager::SetFontStrikeout(BOOL _strikeout) {
    this->strikeout = _strikeout;
    font();
}

BOOL Win32FontManager::FontStrikeout() {
    return this->strikeout;
}

void Win32FontManager::SetFontName(LPCWSTR _fontName) {
    this->fontName = _fontName;
    font();
}

LPCWSTR Win32FontManager::SetFontName() {
    return this->fontName;
}

void Win32FontManager::setHWND(HWND hWnd) {
    this->hWnd = hWnd;
}

Win32FontManager::~Win32FontManager() {
    DeleteObject(hFont);
}

Win32FontManager::Win32FontManager(HWND hWnd, double size, int _weight, BOOL _italic, BOOL _underline, BOOL _strikeout, LPCWSTR _fontName) {
    this->fontSize = size;
    this->weight = _weight;
    this->italic = _italic;
    this->underline = _underline;
    this->strikeout = _strikeout;
    this->fontName = _fontName;
    this->hWnd = hWnd;
    font();
}