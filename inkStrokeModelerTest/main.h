#pragma once

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string>

#include <windows.h>
#include <locale>
#include <atlbase.h>

#include "HiEasyX.h"

#include <ink_stroke_modeler/stroke_modeler.h>
#pragma comment(lib, "ink_stroke_modeler_merge.lib")

#include <d3d11.h>
#include <DirectXMath.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

using namespace std;
using namespace ink::stroke_model;

class WindowInfoClass
{
public:
	int w = 1600;
	int h = 900;
};
extern WindowInfoClass windowInfo;

CComPtr<ID3D11Device> d3dDevice_HARDWARE;
CComPtr<IDXGIDevice1> dxgiDevice1;

CComPtr<ID2D1Factory1> d2dFactory1;
CComPtr<ID2D1Device> d2dDevice_HARDWARE;

HWND windowHWND;

// 调测专用
void Test()
{
	MessageBoxW(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL);
}
void Testb(bool t)
{
	MessageBoxW(NULL, t ? L"true" : L"false", L"真否标记", MB_OK | MB_SYSTEMMODAL);
}
void Testi(long long t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL);
}
void Testd(double t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"浮点标记", MB_OK | MB_SYSTEMMODAL);
}
void Testa(string t)
{
	MessageBoxA(NULL, t.c_str(), "字符标记", MB_OK | MB_SYSTEMMODAL);
}
void Testw(wstring t)
{
	MessageBoxW(NULL, t.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}