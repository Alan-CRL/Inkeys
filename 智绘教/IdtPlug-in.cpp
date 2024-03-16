/*
 * @file		IdtPlug-in.cpp
 * @brief		IDT plugin linkage | 智绘教插件联动
 * @note		PPT linkage components and other plugins | PPT联动组件和其他插件等
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// INFO: This source file will take the lead in refactoring the code logic and optimizing the reading experience.
// 提示：这个源文件将率先重构代码逻辑，并优化阅读体验。

// FAQ: Some comments are followed by a '*' symbol, and their explanations are provided below.
// 常问问题：一些注释后带有 '*' 号，它们的解释在下面。
//
// *1
//
// The ·PptInfoStateBuffer· variable is a buffer for the ·PptInfoState· variable. After the DrawpadDrawing function loads the PPT's drawing board,
// the value in the buffer variable will become consistent with ·PptInfoState·. Some functions obtain the value of ·PptInfoStateBuffer· and must wait until
// the PPT drawing board is initialized before making any changes and responding.
//
// PptInfoStateBuffer 变量是 PptInfoState 变量的缓冲，当 DrawpadDrawing 函数加载完成 PPT 的画板后，缓冲变量中的值才会变为和 PptInfoState 一致。
// 一些函数获取 PptInfoStateBuffer 的值，必须要等到 PPT 画板初始化完毕后才会有所改变，并再做出反应。

#include "IdtPlug-in.h"

#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtMagnification.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtWindow.h"

#include <d2d1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

PptImgStruct PptImg = { false }; // It stores image data generated during slide shows. | 其存储幻灯片放映时产生的图像数据。
PptInfoStateStruct PptInfoState = { -1, -1 }; // It stores the current status of the slide show software, where First represents the total number of slide pages and Second represents the current slide number. | 其存储幻灯片放映软件当前的状态，First 代表总幻灯片页数，Second 代表当前幻灯片编号。
PptInfoStateStruct PptInfoStateBuffer = { -1, -1 }; // Buffered variables for ·PptInfoState·. *1 | PptInfoState 的缓冲变量。*1

IMAGE PptIcon[5]; // Button icons for PPT controls | PPT 控件的按键图标
bool SeewoCamera; // 希沃视频展台联动模式

// PPT联动

#import "PptCOM.tlb" // C# Class Library PptCOM Project Library (PptCOM. cs) | C# 类库 PptCOM 项目库
using namespace PptCOM;

shared_mutex PPTManipulatedSm;
std::chrono::high_resolution_clock::time_point PPTManipulated;

wstring LinkTest()
{
	wstring ret = L"COM库(.dll) 不存在，且发生严重错误，返回值被忽略";

	if (_waccess((string_to_wstring(global_path) + L"PptCOM.dll").c_str(), 4) == 0)
	{
		IPptCOMServerPtr pto;
		try
		{
			_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
			ret = bstr_to_wstring(pto->LinkTest());
		}
		catch (_com_error& err)
		{
			ret = L"COM库(.dll) 存在，COM成功初始化，但C++端COM接口异常：" + wstring(err.ErrorMessage());
		}
	}
	else
	{
		wchar_t absolutePath[_MAX_PATH];

		if (_wfullpath(absolutePath, L"PptCOM.dll", _MAX_PATH) != NULL)
		{
			ret = L"COM库(.dll) 不存在，预期调用目录为：\"" + string_to_wstring(global_path) + L"PptCOM.dll\"";
		}
		else ret = L"COM库(.dll) 不存在，预期调用目录测算失败";
	}

	return ret;
}
wstring IsPptDependencyLoaded()
{
	wstring ret = L"PPT 联动组件异常，且发生严重错误，返回值被忽略";

	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		ret = L"COM接口正常，C#类库反馈信息：" + bstr_to_wstring(pto->IsPptDependencyLoaded());
	}
	catch (_com_error& err)
	{
		ret = L"COM接口异常：" + wstring(err.ErrorMessage());
	}

	return ret;
}
HWND GetPptShow()
{
	HWND hWnd = NULL;

	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));

		_variant_t result = pto->GetPptHwnd();
		hWnd = (HWND)result.llVal;

		return hWnd;
	}
	catch (_com_error)
	{
	}

	return NULL;
}
wstring GetPptTitle()
{
	wstring ret = L"";

	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		ret = bstr_to_wstring(pto->slideNameIndex());

		return ret;
	}
	catch (_com_error)
	{
	}

	return ret;
}
bool EndPptShow()
{
	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		pto->EndSlideShow();

		return true;
	}
	catch (_com_error)
	{
	}

	return false;
}

int NextPptSlides(int check)
{
	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		return pto->NextSlideShow(check);
	}
	catch (_com_error)
	{
	}
	return -1;
}
int PreviousPptSlides()
{
	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		return pto->PreviousSlideShow();
	}
	catch (_com_error)
	{
	}
	return -1;
}

int PptAdvanceMode = -1;

map<wstring, PPTUIControlStruct> PPTUIControl, PPTUIControlTarget;
map<wstring, PPTUIControlStruct>& map<wstring, PPTUIControlStruct>::operator=(const map<wstring, PPTUIControlStruct>& m)
{
	//判断自赋值
	if (this == &m) return *this;
	//清空当前map
	this->clear();
	//遍历参数map，把每个键值对赋值给当前map
	for (auto it = m.begin(); it != m.end(); it++) this->insert(*it);
	//返回当前map的引用
	return *this;
}

map<wstring, PPTUIControlColorStruct> PPTUIControlColor, PPTUIControlColorTarget;
map<wstring, PPTUIControlColorStruct>& map<wstring, PPTUIControlColorStruct>::operator=(const map<wstring, PPTUIControlColorStruct>& m)
{
	//判断自赋值
	if (this == &m) return *this;
	//清空当前map
	this->clear();
	//遍历参数map，把每个键值对赋值给当前map
	for (auto it = m.begin(); it != m.end(); it++) this->insert(*it);
	//返回当前map的引用
	return *this;
}

map<wstring, wstring> PPTUIControlString, PPTUIControlStringTarget;

template <class T> void DxObjectSafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}
D2D1::ColorF ConvertToD2DColor(COLORREF color, bool reserve_alpha = true)
{
	return D2D1::ColorF(
		GetBValue(color) / 255.0f,
		GetGValue(color) / 255.0f,
		GetRValue(color) / 255.0f,
		(reserve_alpha ? GetAValue(color) : 255) / 255.0f
	);
}

class MemoryFontLoader : public IDWriteFontCollectionLoader
{
public:
	// IDWriteFontCollectionLoader接口中的方法
	virtual HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(
		IDWriteFactory* factory,  // DirectWrite工厂实例
		void const* collectionKey,  // 字体数据
		UINT32 collectionKeySize,  // 字体数据的大小
		OUT IDWriteFontFileEnumerator** fontFileEnumerator  // 输出参数，用于返回字体文件的枚举器
	);

	// IUnknown接口中的方法
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, OUT void** ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();

	// 获取加载器的单例实例
	static IDWriteFontCollectionLoader* GetLoader()
	{
		return instance_;
	}

	// 检查加载器是否已经初始化
	static bool IsLoaderInitialized()
	{
		return instance_ != NULL;
	}

private:
	// 加载器的单例实例
	static IDWriteFontCollectionLoader* instance_;
};
IDWriteFontCollectionLoader* MemoryFontLoader::instance_ = NULL;

//ppt 控件
bool UiChange;
void DrawControlWindow()
{
	thread_status[L"DrawControlWindow"] = true;

	IMAGE ppt_background = CreateImageColor(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), RGBA(0, 0, 0, 0), true);

	//Graphics graphics(GetImageHDC(&ppt_background));

	// 创建 D2D 工厂
	ID2D1Factory* Factory = NULL;
	HRESULT ResultHandle = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &Factory);
	// D2D1_FACTORY_TYPE_MULTI_THREADED 多线程后，该参数指定将可以公用一个工厂

	// 创建 DC Render 并指定硬件加速
	auto Property = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE::D2D1_RENDER_TARGET_TYPE_HARDWARE,
		D2D1::PixelFormat(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED
		), 0.0, 0.0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
	);

	// 创建 EasyX 兼容的 DC Render Target
	ID2D1DCRenderTarget* DCRenderTarget;
	HRESULT Result = Factory->CreateDCRenderTarget(&Property, &DCRenderTarget);

	// 绑定 EasyX DC
	RECT PptBackgroundWindowRect = { 0, 0, ppt_background.getwidth(), ppt_background.getheight() };
	DCRenderTarget->BindDC(GetImageHDC(&ppt_background), &PptBackgroundWindowRect);

	// 设置抗锯齿
	DCRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE::D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	//ppt窗口初始化
	{
		DisableResizing(ppt_window, true);//禁止窗口拉伸
		SetWindowLong(ppt_window, GWL_STYLE, GetWindowLong(ppt_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
		SetWindowPos(ppt_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME | SWP_NOACTIVATE);
		SetWindowLong(ppt_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏
	}

	//媒体初始化
	ID2D1Bitmap* PptIconBitmap[5] = { NULL };
	{
		loadimage(&PptIcon[1], L"PNG", L"ppt1", 40, 40, true);
		loadimage(&PptIcon[2], L"PNG", L"ppt2", 40, 40, true);
		loadimage(&PptIcon[3], L"PNG", L"ppt3", 40, 40, true);

		ChangeColor(PptIcon[1], RGB(50, 50, 50));
		ChangeColor(PptIcon[2], RGB(50, 50, 50));
		ChangeColor(PptIcon[3], RGB(50, 50, 50));

		{
			int width = PptIcon[1].getwidth();
			int height = PptIcon[1].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[1]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = ((color & 0x00FF0000) >> 16) * 255 / alpha;
						data[(y * width + x) * 4 + 1] = ((color & 0x0000FF00) >> 8) * 255 / alpha;
						data[(y * width + x) * 4 + 2] = ((color & 0x000000FF) >> 0) * 255 / alpha;
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &PptIconBitmap[1]);
			delete[] data;
		}
		{
			int width = PptIcon[2].getwidth();
			int height = PptIcon[2].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[2]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = ((color & 0x00FF0000) >> 16) * 255 / alpha;
						data[(y * width + x) * 4 + 1] = ((color & 0x0000FF00) >> 8) * 255 / alpha;
						data[(y * width + x) * 4 + 2] = ((color & 0x000000FF) >> 0) * 255 / alpha;
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &PptIconBitmap[2]);
			delete[] data;
		}
		{
			int width = PptIcon[3].getwidth();
			int height = PptIcon[3].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[3]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = ((color & 0x00FF0000) >> 16) * 255 / alpha;
						data[(y * width + x) * 4 + 1] = ((color & 0x0000FF00) >> 8) * 255 / alpha;
						data[(y * width + x) * 4 + 2] = ((color & 0x000000FF) >> 0) * 255 / alpha;
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &PptIconBitmap[3]);
			delete[] data;
		}
	}

	// D2D 字体初始化
	IDWriteFactory* TextFactory = NULL;
	IDWriteFontCollection* D2DFontCollection = NULL;
	{
		HMODULE hModule = GetModuleHandle(NULL);
		HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(198), L"TTF");
		HGLOBAL hMemory = LoadResource(hModule, hResource);
		PVOID pResourceData = LockResource(hMemory);
		DWORD dwResourceSize = SizeofResource(hModule, hResource);

		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&TextFactory));
		TextFactory->RegisterFontCollectionLoader(MemoryFontLoader::GetLoader());

		TextFactory->CreateCustomFontCollection(MemoryFontLoader::GetLoader(), pResourceData, dwResourceSize, &D2DFontCollection);
	}

	//UI 初始化
	{
		// 左侧控件
		{
			PPTUIControl[L"RoundRect/RoundRectLeft/x"] = { 5, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectLeft/y"] = { (float)GetSystemMetrics(SM_CYSCREEN) + 5, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectLeft/width"] = { 185, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectLeft/height"] = { 60, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectLeft/ellipseheight"] = { 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectLeft/ellipsewidth"] = { 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectLeft/frame/width"] = { 1, 5, 1 };

			PPTUIControlColor[L"RoundRect/RoundRectLeft/fill"] = { RGBA(225, 225, 225, 160), 10, 1 };
			PPTUIControlColor[L"RoundRect/RoundRectLeft/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

			{
				PPTUIControl[L"RoundRect/RoundRectLeft1/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/x"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft1/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft1/width"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft1/height"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft1/ellipseheight"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft1/ellipsewidth"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft1/frame/width"] = { 1, 5, 1 };

				PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"] = { RGBA(250, 250, 250, 160), 3, 1 };
				PPTUIControlColor[L"RoundRect/RoundRectLeft1/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

				{
					PPTUIControl[L"Image/RoundRectLeft1/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectLeft1/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectLeft1/transparency"] = { 255, 300, 1 };
				}
			}
			{
				PPTUIControl[L"Words/InfoLeft/left"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + (float)PPTUIControl[L"RoundRect/RoundRectLeft1/width"].v + 5, 5, 1 };
				PPTUIControl[L"Words/InfoLeft/top"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + 5, 5, 1 };
				PPTUIControl[L"Words/InfoLeft/right"] = { (float)PPTUIControl[L"Words/InfoLeft/left"].v + 65, 5, 1 };
				PPTUIControl[L"Words/InfoLeft/bottom"] = { (float)PPTUIControl[L"Words/InfoLeft/top"].v + 55, 5, 1 };

				PPTUIControl[L"Words/InfoLeft/height"] = { 20, 5, 1 };
				PPTUIControlColor[L"Words/InfoLeft/words_color"] = { RGBA(50, 50, 50, 255), 5, 1 };
			}
			{
				PPTUIControl[L"RoundRect/RoundRectLeft2/x"] = { (float)PPTUIControl[L"Words/InfoLeft/right"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft2/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft2/width"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft2/height"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft2/ellipseheight"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft2/ellipsewidth"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft2/frame/width"] = { 1, 5, 1 };

				PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"] = { RGBA(250, 250, 250, 160), 3, 1 };
				PPTUIControlColor[L"RoundRect/RoundRectLeft2/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

				{
					PPTUIControl[L"Image/RoundRectLeft2/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectLeft2/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectLeft2/transparency"] = { 255, 300, 1 };
				}
			}
			{
				PPTUIControl[L"RoundRect/RoundRectLeft3/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft3/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft3/width"] = { 10, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft3/height"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft3/ellipseheight"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft3/ellipsewidth"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectLeft3/frame/width"] = { 1, 5, 1 };

				PPTUIControlColor[L"RoundRect/RoundRectLeft3/fill"] = { RGBA(250, 250, 250, 0), 3, 1 };
				PPTUIControlColor[L"RoundRect/RoundRectLeft3/frame"] = { RGBA(200, 200, 200, 0), 10, 1 };

				{
					PPTUIControl[L"Image/RoundRectLeft3/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft3/x"].v + 10, 5, 1 };
					PPTUIControl[L"Image/RoundRectLeft3/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectLeft3/y"].v + 10, 5, 1 };
					PPTUIControl[L"Image/RoundRectLeft3/transparency"] = { 255, 300, 1 };
				}
			}
		}
		// 中间控件
		{
			PPTUIControl[L"RoundRect/RoundRectMiddle/x"] = { (float)GetSystemMetrics(SM_CXSCREEN) / 2 - 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectMiddle/y"] = { (float)GetSystemMetrics(SM_CYSCREEN) + 5, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectMiddle/width"] = { 60, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectMiddle/height"] = { 60, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectMiddle/ellipseheight"] = { 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectMiddle/ellipsewidth"] = { 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectMiddle/frame/width"] = { 1, 5, 1 };

			PPTUIControlColor[L"RoundRect/RoundRectMiddle/fill"] = { RGBA(225, 225, 225, 160), 5, 1 };
			PPTUIControlColor[L"RoundRect/RoundRectMiddle/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

			{
				PPTUIControl[L"RoundRect/RoundRectMiddle1/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectMiddle/x"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectMiddle1/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectMiddle/y"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectMiddle1/width"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectMiddle1/height"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectMiddle1/ellipseheight"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectMiddle1/ellipsewidth"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectMiddle1/frame/width"] = { 1, 5, 1 };

				PPTUIControlColor[L"RoundRect/RoundRectMiddle1/fill"] = { RGBA(250, 250, 250, 160), 3, 1 };
				PPTUIControlColor[L"RoundRect/RoundRectMiddle1/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

				{
					PPTUIControl[L"Image/RoundRectMiddle1/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectMiddle1/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectMiddle1/transparency"] = { 255, 300, 1 };
				}
			}
		}
		// 右侧控件
		{
			PPTUIControl[L"RoundRect/RoundRectRight/x"] = { (float)GetSystemMetrics(SM_CXSCREEN) - 190, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectRight/y"] = { (float)GetSystemMetrics(SM_CYSCREEN) + 5, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectRight/width"] = { 185, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectRight/height"] = { 60, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectRight/ellipseheight"] = { 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectRight/ellipsewidth"] = { 30, 5, 1 };
			PPTUIControl[L"RoundRect/RoundRectRight/frame/width"] = { 1, 5, 1 };

			PPTUIControlColor[L"RoundRect/RoundRectRight/fill"] = { RGBA(225, 225, 225, 160), 10, 1 };
			PPTUIControlColor[L"RoundRect/RoundRectRight/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

			{
				PPTUIControl[L"RoundRect/RoundRectRight1/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight/x"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight1/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight/y"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight1/width"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight1/height"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight1/ellipseheight"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight1/ellipsewidth"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight1/frame/width"] = { 1, 5, 1 };

				PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"] = { RGBA(250, 250, 250, 160), 3, 1 };
				PPTUIControlColor[L"RoundRect/RoundRectRight1/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

				{
					PPTUIControl[L"Image/RoundRectRight1/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectRight1/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight1/y"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectRight1/transparency"] = { 255, 300, 1 };
				}
			}
			{
				PPTUIControl[L"Words/InfoRight/left"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + (float)PPTUIControl[L"RoundRect/RoundRectRight1/width"].v + 5, 5, 1 };
				PPTUIControl[L"Words/InfoRight/top"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight/y"].v + 5, 5, 1 };
				PPTUIControl[L"Words/InfoRight/right"] = { (float)PPTUIControl[L"Words/InfoRight/left"].v + 65, 5, 1 };
				PPTUIControl[L"Words/InfoRight/bottom"] = { (float)PPTUIControl[L"Words/InfoRight/top"].v + 55, 5, 1 };

				PPTUIControl[L"Words/InfoRight/height"] = { 20, 5, 1 };
				PPTUIControlColor[L"Words/InfoRight/words_color"] = { RGBA(50, 50, 50, 255), 5, 1 };
			}
			{
				PPTUIControl[L"RoundRect/RoundRectRight2/x"] = { (float)PPTUIControl[L"Words/InfoRight/right"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight2/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight/y"].v + 5, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight2/width"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight2/height"] = { 50, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight2/ellipseheight"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight2/ellipsewidth"] = { 35, 5, 1 };
				PPTUIControl[L"RoundRect/RoundRectRight2/frame/width"] = { 1, 5, 1 };

				PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"] = { RGBA(250, 250, 250, 160), 3, 1 };
				PPTUIControlColor[L"RoundRect/RoundRectRight2/frame"] = { RGBA(200, 200, 200, 160), 10, 1 };

				{
					PPTUIControl[L"Image/RoundRectRight2/x"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight2/x"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectRight2/y"] = { (float)PPTUIControl[L"RoundRect/RoundRectRight2/y"].v + 5, 5, 1 };
					PPTUIControl[L"Image/RoundRectRight2/transparency"] = { 255, 300, 1 };
				}
			}
		}

		PPTUIControlTarget = PPTUIControl;
		PPTUIControlColorTarget = PPTUIControlColor;

		PPTUIControlString[L"Info/Pages"] = L"-1/-1";

		PPTUIControlStringTarget = PPTUIControlString;
	}

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
		blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	}

	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { ppt_background.getwidth(),ppt_background.getheight() };
	POINT ptDst = { 0,0 }; // 设置窗口位置

	// 调用UpdateLayeredWindow函数更新窗口
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	{
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = GetDC(NULL);
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
	}

	do
	{
		Sleep(10);
		::SetWindowLong(ppt_window, GWL_EXSTYLE, ::GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_LAYERED);
	} while (!(::GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED));
	do
	{
		Sleep(10);
		::SetWindowLong(ppt_window, GWL_EXSTYLE, ::GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
	} while (!(::GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE));

	magnificationWindowReady++;

	bool Initialization = false; // 控件初始化完毕
	clock_t tRecord = clock();
	for (bool IsShowWindow = false; !off_signal;)
	{
		int TotalSlides = PptInfoState.TotalPage;
		int CurrentSlides = PptInfoState.CurrentPage;

		// UI 计算部分
		{
			// UI 控件位置定义
			if (1 || TotalSlides != -1)
			{
				// 左侧控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v = 5;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v = GetSystemMetrics(SM_CYSCREEN) - 65;

					if (PptAdvanceMode != 1) PPTUIControlTarget[L"RoundRect/RoundRectLeft/width"].v = 185;
					else PPTUIControlTarget[L"RoundRect/RoundRectLeft/width"].v = 375;

					PPTUIControlTarget[L"RoundRect/RoundRectLeft/height"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/ellipseheight"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/ellipsewidth"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/frame/width"].v = 1;

					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft1/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoLeft/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v + 5;
						PPTUIControlTarget[L"Words/InfoLeft/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"Words/InfoLeft/right"].v = PPTUIControlTarget[L"Words/InfoLeft/left"].v + 65;
						PPTUIControlTarget[L"Words/InfoLeft/bottom"].v = PPTUIControlTarget[L"Words/InfoLeft/top"].v + 55;

						PPTUIControlTarget[L"Words/InfoLeft/height"].v = 20;
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v = PPTUIControlTarget[L"Words/InfoLeft/right"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectLeft2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft2/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/frame/width"].v = 1;

						if (PptAdvanceMode == 1)
						{
							PPTUIControlTarget[L"RoundRect/RoundRectLeft3/width"].v = 150;

							SET_ALPHA(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/fill"].v, 160);
							SET_ALPHA(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/frame"].v, 160);
						}
						else
						{
							PPTUIControlTarget[L"RoundRect/RoundRectLeft3/width"].v = 10;

							SET_ALPHA(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/fill"].v, 0);
							SET_ALPHA(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/frame"].v, 0);
						}

						{
							PPTUIControlTarget[L"Image/RoundRectLeft3/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft3/x"].v + 10;
							PPTUIControlTarget[L"Image/RoundRectLeft3/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft3/y"].v + 10;

							if (PptAdvanceMode == 1) PPTUIControlTarget[L"Image/RoundRectLeft3/transparency"].v = 255;
							else if (PptAdvanceMode == 1) PPTUIControlTarget[L"Image/RoundRectLeft3/transparency"].v = 0;
						}
					}
				}
				// 中间控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/x"].v = GetSystemMetrics(SM_CXSCREEN) / 2 - 30;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/y"].v = GetSystemMetrics(SM_CYSCREEN) - 65;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/width"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/height"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/ellipseheight"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/ellipsewidth"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/frame/width"].v = 1;

					{
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectMiddle1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectMiddle1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectMiddle1/transparency"].v = 255;
						}
					}
				}
				// 右侧控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v = GetSystemMetrics(SM_CXSCREEN) - 190;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v = GetSystemMetrics(SM_CYSCREEN) - 65;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/width"].v = 185;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/height"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipseheight"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipsewidth"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/frame/width"].v = 1;

					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight1/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoRight/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v + 5;
						PPTUIControlTarget[L"Words/InfoRight/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + 5;
						PPTUIControlTarget[L"Words/InfoRight/right"].v = PPTUIControlTarget[L"Words/InfoRight/left"].v + 65;
						PPTUIControlTarget[L"Words/InfoRight/bottom"].v = PPTUIControlTarget[L"Words/InfoRight/top"].v + 55;

						PPTUIControlTarget[L"Words/InfoRight/height"].v = 20;
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v = PPTUIControlTarget[L"Words/InfoRight/right"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectRight2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight2/transparency"].v = 255;
						}
					}
				}

				// 文字显示
				{
					{
						wstring temp;
						if (TotalSlides >= 100 || TotalSlides >= 100)
						{
							temp = CurrentSlides == -1 ? L"-" : to_wstring(CurrentSlides);
							temp += L"\n";
							temp += to_wstring(TotalSlides);
						}
						else
						{
							temp = CurrentSlides == -1 ? L"-" : to_wstring(CurrentSlides);
							temp += L"/";
							temp += to_wstring(TotalSlides);
						}

						PPTUIControlStringTarget[L"Info/Pages"] = temp;
					}
				}
			}
			else
			{
				// 左侧控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v = 5;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v = GetSystemMetrics(SM_CYSCREEN) + 5;

					if (PptAdvanceMode != 1) PPTUIControlTarget[L"RoundRect/RoundRectLeft/width"].v = 185;
					else PPTUIControlTarget[L"RoundRect/RoundRectLeft/width"].v = 375;

					PPTUIControlTarget[L"RoundRect/RoundRectLeft/height"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/ellipseheight"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/ellipsewidth"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/frame/width"].v = 1;

					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft1/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoLeft/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v + 5;
						PPTUIControlTarget[L"Words/InfoLeft/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"Words/InfoLeft/right"].v = PPTUIControlTarget[L"Words/InfoLeft/left"].v + 65;
						PPTUIControlTarget[L"Words/InfoLeft/bottom"].v = PPTUIControlTarget[L"Words/InfoLeft/top"].v + 55;

						PPTUIControlTarget[L"Words/InfoLeft/height"].v = 20;
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v = PPTUIControlTarget[L"Words/InfoLeft/right"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectLeft2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectLeft2/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/width"].v = 10;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/frame/width"].v = 1;

						SET_ALPHA(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/fill"].v, 0);
						SET_ALPHA(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/frame"].v, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectLeft3/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft3/x"].v + 10;
							PPTUIControlTarget[L"Image/RoundRectLeft3/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft3/y"].v + 10;

							if (PptAdvanceMode == 1) PPTUIControlTarget[L"Image/RoundRectLeft3/transparency"].v = 255;
							else if (PptAdvanceMode == 1) PPTUIControlTarget[L"Image/RoundRectLeft3/transparency"].v = 0;
						}
					}
				}
				// 中间控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/x"].v = GetSystemMetrics(SM_CXSCREEN) / 2 - 30;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/y"].v = GetSystemMetrics(SM_CYSCREEN) + 5;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/width"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/height"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/ellipseheight"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/ellipsewidth"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddle/frame/width"].v = 1;

					{
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectMiddle1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectMiddle1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddle1/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectMiddle1/transparency"].v = 255;
						}
					}
				}
				// 右侧控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v = GetSystemMetrics(SM_CXSCREEN) - 190;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v = GetSystemMetrics(SM_CYSCREEN) + 5;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/width"].v = 185;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/height"].v = 60;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipseheight"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipsewidth"].v = 30;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/frame/width"].v = 1;

					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight1/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoRight/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v + 5;
						PPTUIControlTarget[L"Words/InfoRight/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + 5;
						PPTUIControlTarget[L"Words/InfoRight/right"].v = PPTUIControlTarget[L"Words/InfoRight/left"].v + 65;
						PPTUIControlTarget[L"Words/InfoRight/bottom"].v = PPTUIControlTarget[L"Words/InfoRight/top"].v + 55;

						PPTUIControlTarget[L"Words/InfoRight/height"].v = 20;
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v = PPTUIControlTarget[L"Words/InfoRight/right"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + 5;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/width"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/height"].v = 50;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipseheight"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipsewidth"].v = 35;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/frame/width"].v = 1;

						{
							PPTUIControlTarget[L"Image/RoundRectRight2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v + 5;
							PPTUIControlTarget[L"Image/RoundRectRight2/transparency"].v = 255;
						}
					}
				}

				// 文字显示
				{
					{
						PPTUIControlStringTarget[L"Info/Pages"] = L"-1/-1";
					}
				}
			}

			// UI 变化调整
			for (const auto& [key, value] : PPTUIControl)
			{
				if (PPTUIControl[key].s && PPTUIControl[key].v != PPTUIControlTarget[key].v)
				{
					if (abs(PPTUIControl[key].v - PPTUIControlTarget[key].v) <= PPTUIControl[key].e) PPTUIControl[key].v = float(PPTUIControlTarget[key].v);
					else if (PPTUIControl[key].v < PPTUIControlTarget[key].v) PPTUIControl[key].v = float(PPTUIControl[key].v + max(PPTUIControl[key].e, (PPTUIControlTarget[key].v - PPTUIControl[key].v) / PPTUIControl[key].s));
					else PPTUIControl[key].v = float(PPTUIControl[key].v + min(-PPTUIControl[key].e, (PPTUIControlTarget[key].v - PPTUIControl[key].v) / PPTUIControl[key].s));

					UiChange = true;
				}
			}
			for (const auto& [key, value] : PPTUIControlColor)
			{
				if (PPTUIControlColor[key].s && PPTUIControlColor[key].v != PPTUIControlColorTarget[key].v)
				{
					float r1 = GetRValue(PPTUIControlColor[key].v);
					float g1 = GetGValue(PPTUIControlColor[key].v);
					float b1 = GetBValue(PPTUIControlColor[key].v);
					float a1 = float((PPTUIControlColor[key].v >> 24) & 0xff);

					float r2 = GetRValue(PPTUIControlColorTarget[key].v);
					float g2 = GetGValue(PPTUIControlColorTarget[key].v);
					float b2 = GetBValue(PPTUIControlColorTarget[key].v);
					float a2 = float((PPTUIControlColorTarget[key].v >> 24) & 0xff);

					if (abs(r1 - r2) <= PPTUIControlColor[key].e) r1 = r2;
					else if (r1 < r2) r1 = r1 + max(PPTUIControlColor[key].e, (r2 - r1) / PPTUIControlColor[key].s);
					else if (r1 > r2) r1 = r1 + min(-PPTUIControlColor[key].e, (r2 - r1) / PPTUIControlColor[key].s);

					if (abs(g1 - g2) <= PPTUIControlColor[key].e) g1 = g2;
					else if (g1 < g2) g1 = g1 + max(PPTUIControlColor[key].e, (g2 - g1) / PPTUIControlColor[key].s);
					else if (g1 > g2) g1 = g1 + min(-PPTUIControlColor[key].e, (g2 - g1) / PPTUIControlColor[key].s);

					if (abs(b1 - b2) <= PPTUIControlColor[key].e) b1 = b2;
					else if (b1 < b2) b1 = b1 + max(PPTUIControlColor[key].e, (b2 - b1) / PPTUIControlColor[key].s);
					else if (b1 > b2) b1 = b1 + min(-PPTUIControlColor[key].e, (b2 - b1) / PPTUIControlColor[key].s);

					if (abs(a1 - a2) <= PPTUIControlColor[key].e) a1 = a2;
					else if (a1 < a2) a1 = a1 + max(PPTUIControlColor[key].e, (a2 - a1) / PPTUIControlColor[key].s);
					else if (a1 > a2) a1 = a1 + min(-PPTUIControlColor[key].e, (a2 - a1) / PPTUIControlColor[key].s);

					PPTUIControlColor[key].v = RGBA(max(0, min(255, (int)r1)), max(0, min(255, (int)g1)), max(0, min(255, (int)b1)), max(0, min(255, (int)a1)));

					UiChange = true;
				}
			}
			for (const auto& [key, value] : PPTUIControlString)
			{
				if (PPTUIControlString[key] != PPTUIControlStringTarget[key])
				{
					PPTUIControlString[key] = PPTUIControlStringTarget[key];

					UiChange = true;
				}
			}
		}

		// 控件加载部分
		if (!Initialization && (TotalSlides != -1 || 1))
		{
			ppt_show = GetPptShow();

			std::wstringstream ss(GetPptTitle());
			getline(ss, ppt_title);
			getline(ss, ppt_software);

			if (ppt_software.find(L"WPS") != ppt_software.npos) ppt_software = L"WPS";
			else ppt_software = L"PowerPoint";

			//if (!ppt_title_recond[ppt_title]) FreezePPT = true;
			Initialization = true;
		}
		else if (Initialization && TotalSlides == -1)
		{
			PptImg.IsSave = false;
			PptImg.IsSaved.clear();
			PptImg.Image.clear();

			ppt_show = NULL, ppt_software = L"";

			FreezePPT = false;
			Initialization = false;
		}

		// 绘制部分
		if (UiChange)
		{
			SetImageColor(ppt_background, RGBA(0, 0, 0, 0), true);

			DCRenderTarget->BeginDraw();

			// 左侧控件
			{
				// RoundRect/RoundRectLeft
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectLeft/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectLeft/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectLeft/x"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft/y"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft/x"].v + PPTUIControl[L"RoundRect/RoundRectLeft/width"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + PPTUIControl[L"RoundRect/RoundRectLeft/height"].v),
						PPTUIControl[L"RoundRect/RoundRectLeft/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectLeft/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectLeft/frame/width"].v);

					if (pFrameBrush)
					{
						pFrameBrush->Release();
						pFrameBrush = NULL;
					}
					if (pFillBrush)
					{
						pFillBrush->Release();
						pFillBrush = NULL;
					}
				}

				// RoundRect/RoundRectLeft1
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectLeft1/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + PPTUIControl[L"RoundRect/RoundRectLeft1/width"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v + PPTUIControl[L"RoundRect/RoundRectLeft1/height"].v),
						PPTUIControl[L"RoundRect/RoundRectLeft1/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectLeft1/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectLeft1/frame/width"].v);

					if (pFrameBrush)
					{
						pFrameBrush->Release();
						pFrameBrush = NULL;
					}
					if (pFillBrush)
					{
						pFillBrush->Release();
						pFillBrush = NULL;
					}
				}
				// Image/RoundRectLeft1
				{
					DCRenderTarget->DrawBitmap(PptIconBitmap[1], D2D1::RectF(PPTUIControl[L"Image/RoundRectLeft1/x"].v, PPTUIControl[L"Image/RoundRectLeft1/y"].v, PPTUIControl[L"Image/RoundRectLeft1/x"].v + PptIcon[1].getwidth(), PPTUIControl[L"Image/RoundRectLeft1/y"].v + PptIcon[1].getheight()), PPTUIControl[L"Image/RoundRectLeft1/transparency"].v / 255.0f);
				}

				// Words/InfoLeft
				{
					IDWriteTextFormat* textFormat = NULL;
					TextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						PPTUIControl[L"Words/InfoLeft/height"].v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(PPTUIControlColor[L"Words/InfoLeft/words_color"].v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						PPTUIControlString[L"Info/Pages"].c_str(),  // 文本
						wcslen(PPTUIControlString[L"Info/Pages"].c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							PPTUIControl[L"Words/InfoLeft/left"].v,
							PPTUIControl[L"Words/InfoLeft/top"].v,
							PPTUIControl[L"Words/InfoLeft/right"].v,
							PPTUIControl[L"Words/InfoLeft/bottom"].v
						),
						pBrush
					);

					if (pBrush)
					{
						pBrush->Release();
						pBrush = NULL;
					}
				}

				// RoundRect/RoundRectLeft2
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectLeft2/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v + PPTUIControl[L"RoundRect/RoundRectLeft2/width"].v,
						PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v + PPTUIControl[L"RoundRect/RoundRectLeft2/height"].v),
						PPTUIControl[L"RoundRect/RoundRectLeft2/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectLeft2/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectLeft2/frame/width"].v);

					if (pFrameBrush)
					{
						pFrameBrush->Release();
						pFrameBrush = NULL;
					}
					if (pFillBrush)
					{
						pFillBrush->Release();
						pFillBrush = NULL;
					}
				}
				// Image/RoundRectLeft2
				{
					if (CurrentSlides == -1) DCRenderTarget->DrawBitmap(PptIconBitmap[3], D2D1::RectF(PPTUIControl[L"Image/RoundRectLeft2/x"].v, PPTUIControl[L"Image/RoundRectLeft2/y"].v, PPTUIControl[L"Image/RoundRectLeft2/x"].v + PptIcon[3].getwidth(), PPTUIControl[L"Image/RoundRectLeft2/y"].v + PptIcon[3].getheight()), PPTUIControl[L"Image/RoundRectLeft2/transparency"].v / 255.0f);
					else DCRenderTarget->DrawBitmap(PptIconBitmap[2], D2D1::RectF(PPTUIControl[L"Image/RoundRectLeft2/x"].v, PPTUIControl[L"Image/RoundRectLeft2/y"].v, PPTUIControl[L"Image/RoundRectLeft2/x"].v + PptIcon[2].getwidth(), PPTUIControl[L"Image/RoundRectLeft2/y"].v + PptIcon[2].getheight()), PPTUIControl[L"Image/RoundRectLeft2/transparency"].v / 255.0f);
				}
			}

			// 中间控件
			{
				// RoundRect/RoundRectMiddle
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddle/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddle/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectMiddle/x"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddle/y"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddle/x"].v + PPTUIControl[L"RoundRect/RoundRectMiddle/width"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddle/y"].v + PPTUIControl[L"RoundRect/RoundRectMiddle/height"].v),
						PPTUIControl[L"RoundRect/RoundRectMiddle/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectMiddle/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectMiddle/frame/width"].v);

					if (pFrameBrush)
					{
						pFrameBrush->Release();
						pFrameBrush = NULL;
					}
					if (pFillBrush)
					{
						pFillBrush->Release();
						pFillBrush = NULL;
					}
				}

				// RoundRect/RoundRectMiddle1
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddle1/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddle1/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v + PPTUIControl[L"RoundRect/RoundRectMiddle1/width"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v + PPTUIControl[L"RoundRect/RoundRectMiddle1/height"].v),
						PPTUIControl[L"RoundRect/RoundRectMiddle1/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectMiddle1/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectMiddle1/frame/width"].v);

					if (pFrameBrush)
					{
						pFrameBrush->Release();
						pFrameBrush = NULL;
					}
					if (pFillBrush)
					{
						pFillBrush->Release();
						pFillBrush = NULL;
					}
				}
				// Image/RoundRectMiddle1
				{
					DCRenderTarget->DrawBitmap(PptIconBitmap[3], D2D1::RectF(PPTUIControl[L"Image/RoundRectMiddle1/x"].v, PPTUIControl[L"Image/RoundRectMiddle1/y"].v, PPTUIControl[L"Image/RoundRectMiddle1/x"].v + PptIcon[3].getwidth(), PPTUIControl[L"Image/RoundRectMiddle1/y"].v + PptIcon[3].getheight()), PPTUIControl[L"Image/RoundRectMiddle1/transparency"].v / 255.0f);
				}
			}

			/*
			// 右侧控件
			{
				hiex::EasyX_Gdiplus_FillRoundRect(PPTUIControl[L"RoundRect/RoundRectRight/x"].v, PPTUIControl[L"RoundRect/RoundRectRight/y"].v, PPTUIControl[L"RoundRect/RoundRectRight/width"].v, PPTUIControl[L"RoundRect/RoundRectRight/height"].v, PPTUIControl[L"RoundRect/RoundRectRight/ellipsewidth"].v, PPTUIControl[L"RoundRect/RoundRectRight/ellipseheight"].v, PPTUIControlColor[L"RoundRect/RoundRectRight/frame"].v, PPTUIControlColor[L"RoundRect/RoundRectRight/fill"].v, PPTUIControl[L"RoundRect/RoundRectRight/frame/width"].v, true, SmoothingModeHighQuality, &ppt_background);

				hiex::EasyX_Gdiplus_FillRoundRect(PPTUIControl[L"RoundRect/RoundRectRight1/x"].v, PPTUIControl[L"RoundRect/RoundRectRight1/y"].v, PPTUIControl[L"RoundRect/RoundRectRight1/width"].v, PPTUIControl[L"RoundRect/RoundRectRight1/height"].v, PPTUIControl[L"RoundRect/RoundRectRight1/ellipsewidth"].v, PPTUIControl[L"RoundRect/RoundRectRight1/ellipseheight"].v, PPTUIControlColor[L"RoundRect/RoundRectRight1/frame"].v, PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v, PPTUIControl[L"RoundRect/RoundRectRight1/frame/width"].v, true, SmoothingModeHighQuality, &ppt_background);
				hiex::TransparentImage(&ppt_background, int(PPTUIControl[L"Image/RoundRectRight1/x"].v), int(PPTUIControl[L"Image/RoundRectRight1/y"].v), &PptIcon[1], int(PPTUIControl[L"Image/RoundRectRight1/transparency"].v));

				{
					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, (Gdiplus::REAL)PPTUIControl[L"Words/InfoRight/height"].v, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(PPTUIControlColor[L"Words/InfoRight/words_color"].v, true));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						pptwords_rect.left = (int)PPTUIControl[L"Words/InfoRight/left"].v;
						pptwords_rect.top = (int)PPTUIControl[L"Words/InfoRight/top"].v;
						pptwords_rect.right = (int)PPTUIControl[L"Words/InfoRight/right"].v;
						pptwords_rect.bottom = (int)PPTUIControl[L"Words/InfoRight/bottom"].v;
					}
					graphics.DrawString(PPTUIControlString[L"Info/Pages"].c_str(), -1, &gp_font, hiex::RECTToRectF(pptwords_rect), &stringFormat, &WordBrush);
				}

				hiex::EasyX_Gdiplus_FillRoundRect(PPTUIControl[L"RoundRect/RoundRectRight2/x"].v, PPTUIControl[L"RoundRect/RoundRectRight2/y"].v, PPTUIControl[L"RoundRect/RoundRectRight2/width"].v, PPTUIControl[L"RoundRect/RoundRectRight2/height"].v, PPTUIControl[L"RoundRect/RoundRectRight2/ellipsewidth"].v, PPTUIControl[L"RoundRect/RoundRectRight2/ellipseheight"].v, PPTUIControlColor[L"RoundRect/RoundRectRight2/frame"].v, PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v, PPTUIControl[L"RoundRect/RoundRectRight2/frame/width"].v, true, SmoothingModeHighQuality, &ppt_background);
				if (CurrentSlides == -1) hiex::TransparentImage(&ppt_background, int(PPTUIControl[L"Image/RoundRectRight2/x"].v), int(PPTUIControl[L"Image/RoundRectRight2/y"].v), &PptIcon[3], int(PPTUIControl[L"Image/RoundRectRight2/transparency"].v));
				else hiex::TransparentImage(&ppt_background, int(PPTUIControl[L"Image/RoundRectRight2/x"].v), int(PPTUIControl[L"Image/RoundRectRight2/y"].v), &PptIcon[2], int(PPTUIControl[L"Image/RoundRectRight2/transparency"].v));
			}
			 */
			DCRenderTarget->EndDraw();

			{
				ulwi.hdcSrc = GetImageHDC(&ppt_background);
				UpdateLayeredWindowIndirect(ppt_window, &ulwi);

				if (!IsShowWindow) ShowWindow(ppt_window, SW_SHOW), IsShowWindow = true;
				// 动态平衡帧率
				if (tRecord)
				{
					int delay = 1000 / 24 - (clock() - tRecord);
					if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				}
				tRecord = clock();

				UiChange = false;
			}
		}
		else Sleep(100);
	}

	thread_status[L"DrawControlWindow"] = false;
}
void ControlManipulation()
{
	ExMessage m;
	int last_x = -1, last_y = -1;

	while (!off_signal)
	{
		if (PptInfoStateBuffer.TotalPage != -1)
		{
			hiex::getmessage_win32(&m, EM_MOUSE, ppt_window);

			// 左侧 上一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft1/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 160);
				UiChange = true;

				if (m.lbutton)
				{
					SetForegroundWindow(ppt_show);

					std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
					PPTManipulated = std::chrono::high_resolution_clock::now();
					lock1.unlock();

					PptInfoState.CurrentPage = PreviousPptSlides();
					PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(200, 200, 200, 255);

					std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
					while (1)
					{
						if (!KEY_DOWN(VK_LBUTTON)) break;
						if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
						{
							std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
							PPTManipulated = std::chrono::high_resolution_clock::now();
							lock1.unlock();

							PptInfoState.CurrentPage = PreviousPptSlides();
							PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(200, 200, 200, 255);
						}

						Sleep(15);
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 160);
			// 左侧 下一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft2/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft2/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 160);
				UiChange = true;

				if (m.lbutton)
				{
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && choose.select == false && penetrate.select == false)
					{
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
							PPTManipulated = std::chrono::high_resolution_clock::now();
							lock1.unlock();
							EndPptShow();

							brush.select = false;
							rubber.select = false;
							penetrate.select = false;
							choose.select = true;
						}
					}
					else if (temp_currentpage == -1)
					{
						std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
						PPTManipulated = std::chrono::high_resolution_clock::now();
						lock1.unlock();
						EndPptShow();
					}
					else
					{
						SetForegroundWindow(ppt_show);

						std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
						PPTManipulated = std::chrono::high_resolution_clock::now();
						lock1.unlock();

						PptInfoState.CurrentPage = NextPptSlides(temp_currentpage);
						PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!KEY_DOWN(VK_LBUTTON)) break;

							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								temp_currentpage = PptInfoState.CurrentPage;
								if (temp_currentpage == -1 && choose.select == false && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
									{
										EndPptShow();

										brush.select = false;
										rubber.select = false;
										penetrate.select = false;
										choose.select = true;
									}
									break;
								}
								else if (temp_currentpage != -1)
								{
									std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
									PPTManipulated = std::chrono::high_resolution_clock::now();
									lock1.unlock();

									PptInfoState.CurrentPage = NextPptSlides(temp_currentpage);
									PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(200, 200, 200, 255);
								}
							}

							Sleep(15);
						}
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}

				/*
				if (m.lbutton)
				{
					int lx = m.x, ly = m.y;
					while (1)
					{
						ExMessage m = hiex::getmessage_win32(EM_MOUSE, ppt_window);
						if (IsInRect(m.x, m.y, { 130 + 5, GetSystemMetrics(SM_CYSCREEN) - 60 + 5, 130 + 5 + 50, GetSystemMetrics(SM_CYSCREEN) - 60 + 5 + 50 }))
						{
							if (!m.lbutton)
							{
								if (PptInfoState.CurrentPage == -1 && choose.select == false && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) != 1) break;

									brush.select = false;
									rubber.select = false;
									penetrate.select = false;
									choose.select = true;
								}

								std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
								PPTManipulated = std::chrono::high_resolution_clock::now();
								lock1.unlock();

								SetForegroundWindow(ppt_show);
								PptInfoState.CurrentPage = NextPptSlides();

								break;
							}
						}
						else
						{
							hiex::flushmessage_win32(EM_MOUSE, ppt_window);

							break;
						}
					}
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				}*/
			}
			else PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 160);

			// 中间 结束放映
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddle1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddle1/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectMiddle1/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectMiddle1/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectMiddle1/fill"].v = RGBA(250, 250, 250, 160);
				UiChange = true;

				if (m.lbutton)
				{
					int lx = m.x, ly = m.y;
					while (1)
					{
						ExMessage m = hiex::getmessage_win32(EM_MOUSE, ppt_window);
						if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddle1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddle1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddle1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddle1/height"].v }))
						{
							if (!m.lbutton)
							{
								PPTUIControlColor[L"RoundRect/RoundRectMiddle1/fill"].v = RGBA(200, 200, 200, 255);

								if (choose.select == false && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) != 1) break;

									brush.select = false;
									rubber.select = false;
									penetrate.select = false;
									choose.select = true;
								}

								std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
								PPTManipulated = std::chrono::high_resolution_clock::now();
								lock1.unlock();

								EndPptShow();

								break;
							}
						}
						else
						{
							hiex::flushmessage_win32(EM_MOUSE, ppt_window);

							break;
						}
					}

					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else PPTUIControlColorTarget[L"RoundRect/RoundRectMiddle1/fill"].v = RGBA(250, 250, 250, 160);

			// 右侧 上一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight1/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
				UiChange = true;

				if (m.lbutton)
				{
					SetForegroundWindow(ppt_show);

					std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
					PPTManipulated = std::chrono::high_resolution_clock::now();
					lock1.unlock();

					PptInfoState.CurrentPage = PreviousPptSlides();
					PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);

					std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
					while (1)
					{
						if (!KEY_DOWN(VK_LBUTTON)) break;
						if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
						{
							std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
							PPTManipulated = std::chrono::high_resolution_clock::now();
							lock1.unlock();

							PptInfoState.CurrentPage = PreviousPptSlides();
							PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);
						}

						Sleep(15);
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
			// 右侧 下一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectRight2/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight2/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight2/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight2/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight2/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight2/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);
				UiChange = true;

				if (m.lbutton)
				{
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && choose.select == false && penetrate.select == false)
					{
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
							PPTManipulated = std::chrono::high_resolution_clock::now();
							lock1.unlock();
							EndPptShow();

							brush.select = false;
							rubber.select = false;
							penetrate.select = false;
							choose.select = true;
						}
					}
					else if (temp_currentpage == -1)
					{
						std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
						PPTManipulated = std::chrono::high_resolution_clock::now();
						lock1.unlock();
						EndPptShow();
					}
					else
					{
						SetForegroundWindow(ppt_show);

						std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
						PPTManipulated = std::chrono::high_resolution_clock::now();
						lock1.unlock();

						PptInfoState.CurrentPage = NextPptSlides(temp_currentpage);
						PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!KEY_DOWN(VK_LBUTTON)) break;

							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								temp_currentpage = PptInfoState.CurrentPage;
								if (temp_currentpage == -1 && choose.select == false && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
									{
										EndPptShow();

										brush.select = false;
										rubber.select = false;
										penetrate.select = false;
										choose.select = true;
									}
									break;
								}
								else if (temp_currentpage != -1)
								{
									std::unique_lock<std::shared_mutex> lock1(PPTManipulatedSm);
									PPTManipulated = std::chrono::high_resolution_clock::now();
									lock1.unlock();

									PptInfoState.CurrentPage = NextPptSlides(temp_currentpage);
									PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);
								}
							}

							Sleep(15);
						}
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);
		}
		else Sleep(500);
	}
}
void KeyboardInteraction()
{
	thread_status[L"KeyboardInteraction"] = true;

	ExMessage m;
	while (!off_signal)
	{
		hiex::getmessage_win32(&m, EM_KEY, drawpad_window);

		if (PptInfoState.TotalPage != -1)
		{
			if (m.message == WM_KEYDOWN && (m.vkcode == VK_DOWN || m.vkcode == VK_RIGHT || m.vkcode == VK_NEXT || m.vkcode == VK_SPACE || m.vkcode == VK_UP || m.vkcode == VK_LEFT || m.vkcode == VK_PRIOR))
			{
				auto vkcode = m.vkcode;

				if (vkcode == VK_UP || vkcode == VK_LEFT || vkcode == VK_PRIOR)
				{
					// 上一页
					SetForegroundWindow(ppt_show);

					PptInfoState.CurrentPage = PreviousPptSlides();

					std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
					while (1)
					{
						if (!KeyBoradDown[vkcode]) break;
						if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400) PptInfoState.CurrentPage = PreviousPptSlides();

						Sleep(15);
					}
				}
				else
				{
					// 下一页
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && choose.select == false && penetrate.select == false)
					{
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							EndPptShow();

							brush.select = false;
							rubber.select = false;
							penetrate.select = false;
							choose.select = true;
						}
					}
					else
					{
						SetForegroundWindow(ppt_show);

						PptInfoState.CurrentPage = NextPptSlides(temp_currentpage);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!KeyBoradDown[vkcode]) break;

							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								temp_currentpage = PptInfoState.CurrentPage;
								if (temp_currentpage == -1 && choose.select == false && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
									{
										EndPptShow();

										brush.select = false;
										rubber.select = false;
										penetrate.select = false;
										choose.select = true;
									}
									break;
								}
								else if (temp_currentpage == -1) break;
								PptInfoState.CurrentPage = NextPptSlides(temp_currentpage);
							}

							Sleep(15);
						}
					}
				}
			}
			else if (m.message == WM_KEYDOWN && m.vkcode == VK_ESCAPE)
			{
				auto vkcode = m.vkcode;

				while (KeyBoradDown[vkcode]) Sleep(20);

				if (choose.select == false && penetrate.select == false)
				{
					if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
					{
						EndPptShow();

						brush.select = false;
						rubber.select = false;
						penetrate.select = false;
						choose.select = true;
					}
				}
				else EndPptShow();
			}
		}

		hiex::flushmessage_win32(EM_KEY, drawpad_window);
	}

	thread_status[L"KeyboardInteraction"] = false;
}

//获取当前页编号
int GetCurrentPage()
{
	int currentSlides = -1;

	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		currentSlides = pto->currentSlideIndex();
	}
	catch (_com_error)
	{
	}

	return currentSlides;
}
//获取总页数
int GetTotalPage()
{
	int totalSlides = -1;

	IPptCOMServerPtr pto;
	try
	{
		_com_util::CheckError(pto.CreateInstance(_uuidof(PptCOMServer)));
		totalSlides = pto->totalSlideIndex();
		//Testw(bstr_to_wstring(pto->totalSlideIndex()));
	}
	catch (_com_error)
	{
	}

	return totalSlides;
}
void ppt_state()
{
	thread_status[L"ppt_state"] = true;
	while (!off_signal)
	{
		if (PptInfoState.CurrentPage == -1) PptInfoState.TotalPage = GetTotalPage();

		if (PptInfoState.TotalPage != -1) PptInfoState.CurrentPage = GetCurrentPage();
		else PptInfoState.CurrentPage = -1;

		if (PptInfoState.TotalPage == -1 && !off_signal) for (int i = 0; i <= 30 && !off_signal; i++) Sleep(100);
		else if (!off_signal)
		{
			for (int i = 0; i <= 30 && !off_signal; i++)
			{
				Sleep(100);

				std::shared_lock<std::shared_mutex> lock1(PPTManipulatedSm);
				bool ret = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - PPTManipulated).count() <= 3000;
				lock1.unlock();
				if (ret) break;
			}
		}
	}
	thread_status[L"ppt_state"] = false;
}

//弹窗拦截
//关闭AIClass和希沃白板5窗口
HWND FindWindowByStrings(const std::wstring& className, const std::wstring& windowTitle, const std::wstring& style, int width, int height)
{
	HWND hwnd = NULL;
	while ((hwnd = FindWindowEx(NULL, hwnd, NULL, NULL)) != NULL)
	{
		TCHAR classNameBuffer[1024];
		GetClassName(hwnd, classNameBuffer, 1024);
		if (_tcsstr(classNameBuffer, className.c_str()) == NULL) continue;

		TCHAR title[1024];
		GetWindowText(hwnd, title, 1024);
		if (_tcsstr(title, windowTitle.c_str()) == NULL) continue;

		if (windowTitle.length() == 0)
		{
			if (wstring(title) == windowTitle && to_wstring(GetWindowLong(hwnd, GWL_STYLE)) == style)
			{
				if (width && height)
				{
					RECT rect{};
					DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(RECT));
					int twidth = rect.right - rect.left, thwight = rect.bottom - rect.top;

					HDC hdc = GetDC(NULL);
					int horizontalDPI = GetDeviceCaps(hdc, LOGPIXELSX);
					int verticalDPI = GetDeviceCaps(hdc, LOGPIXELSY);
					ReleaseDC(NULL, hdc);
					float scale = (horizontalDPI + verticalDPI) / 2.0f / 96.0f;

					if (abs(width * scale - twidth) <= 1 && abs(height * scale - thwight) <= 1) return hwnd;
				}
				else return hwnd;
			}
		}
		else if (to_wstring(GetWindowLong(hwnd, GWL_STYLE)) == style) return hwnd;
	}
	return NULL;
}
void black_block()
{
	thread_status[L"black_block"] = true;
	while (!off_signal)
	{
		HWND ai_class = FindWindowByStrings(L"UIIrregularWindow", L"UIIrregularWindow", L"-1811939328");
		if (ai_class != NULL) PostMessage(ai_class, WM_CLOSE, 0, 0);

		HWND Seewo_Whiteboard = FindWindowByStrings(L"HwndWrapper[EasiNote;;", L"", L"369623040", 550, 200);
		if (Seewo_Whiteboard != NULL) PostMessage(Seewo_Whiteboard, WM_CLOSE, 0, 0);

		HWND Seewo_Camera = FindWindowByStrings(L"HwndWrapper[EasiCamera.exe;;", L"希沃视频展台", L"386400256");
		if (Seewo_Camera != NULL) SeewoCamera = true;
		else SeewoCamera = false;

		for (int i = 1; i <= 5 && !off_signal; i++) Sleep(1000);
	}
	thread_status[L"black_block"] = false;
}