#include "IdtBar.h"

#include "../../IdtD2DPreparation.h"
#include "../../IdtDisplayManagement.h"
#include "../../IdtWindow.h"
#include "../CONV/IdtColor.h"

//#undef max
//#undef min
//#include "libcuckoo/cuckoohash_map.h"

#define LUNASVG_BUILD_STATIC
#include <lunasvg/lunasvg.h>
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

// ====================
// 临时
void FloatingInstallHook();

// ====================
// 窗口

// ====================
// 媒体
void BarMediaClass::LoadExImage()
{
}

// ====================
// 界面

// 继承
// 根据类型计算继承坐标原点
BarUiInheritClass::BarUiInheritClass(BarUiInheritEnum typeT, double w, double h, double xT, double yT, double wT, double hT)
{
	// w/h 为控件自身的宽高 -> 最终得出的都是左上角绘制坐标 -> 方便绘制
	type = typeT;

	// TODO 拓展更多类型组合
	if (type == BarUiInheritEnum::TopLeft) { x = xT, y = yT; }
	if (type == BarUiInheritEnum::Center) { x = xT + wT / 2.0 - w / 2.0, y = yT + hT / 2.0 - h / 2.0; }
}

// 具体渲染
string BarUIRendering::SvgReplaceColor(const string& input, const optional<BarUiColorClass>& color1, const optional<BarUiColorClass>& color2)
{
	auto colorref_to_rgb = [](COLORREF c) -> string
		{
			int r = GetRValue(c);
			int g = GetGValue(c);
			int b = GetBValue(c);

			return "rgb(" + to_string(r) + "," + to_string(g) + "," + to_string(b) + ")";
		};
	COLORREF col1;
	COLORREF col2;

	string result = input;
	if (color1.has_value())
	{
		col1 = color1.value().val;

		size_t pos = 0;
		const string tag = "#{color1}";
		const string rgb_str = colorref_to_rgb(col1);
		while ((pos = result.find(tag, pos)) != string::npos)
		{
			result.replace(pos, tag.length(), rgb_str);
			pos += rgb_str.length();
		}
	}
	if (color2.has_value())
	{
		col2 = color2.value().val;

		size_t pos = 0;
		const string tag = "#{color2}";
		const string rgb_str = colorref_to_rgb(col2);
		while ((pos = result.find(tag, pos)) != string::npos)
		{
			result.replace(pos, tag.length(), rgb_str);
			pos += rgb_str.length();
		}
	}

	return result;
}
bool BarUIRendering::Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct)
{
	// 判断是否合法
	if (svg.enable.val == false) return false;

	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = svg.svg.GetVal();
		// 替换颜色，如果有
		if (svg.color1.has_value() || svg.color2.has_value())
		{
			svgContent = SvgReplaceColor(svgContent, svg.color1.value_or(0), svg.color2);
		}

		// 解析SVG
		document = lunasvg::Document::loadFromData(svgContent);
		if (!document) return false; // 解析失败
	}

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = 0.0;
	double tarH = 0.0;
	double tarPct = 1.0; // 透明度

	// 宽高计算
	{
	}
	// 透明度
	{
		if (svg.pct.has_value()) tarPct = svg.pct.value().val;
		// 透明度继承
		if (pct.has_value()) tarPct *= pct.value().get().pct;
	}

	// 绘制到离屏位图
	lunasvg::Bitmap bitmap = document->renderToBitmap(static_cast<int>(tarW), static_cast<int>(tarH));
	CComPtr<ID2D1Bitmap> d2dBitmap;
	{
		if (bitmap.width() == 0 || bitmap.height() == 0 || !bitmap.data()) return false;

		D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
		// lunasvg 文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
		HRESULT hr = DCRenderTarget->CreateBitmap(
			D2D1::SizeU(bitmap.width(), bitmap.height()),
			bitmap.data(),
			bitmap.width() * 4, // stride
			props,
			&d2dBitmap);

		if (FAILED(hr) || !d2dBitmap) return false;
	}

	// 渲染到 DC
	{
		D2D1_RECT_F destRect = D2D1::RectF(static_cast<float>(tarX), static_cast<float>(tarY), static_cast<float>(tarW), static_cast<float>(tarH));
		DCRenderTarget->DrawBitmap(
			d2dBitmap,
			destRect,								// 目标矩形
			static_cast<FLOAT>(tarPct),				// 不透明度
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
			nullptr									// 源rect, null表示全部
		);
	}

	return true;
}

// UI 总集
void BarUISetClass::Rendering()
{
	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;
	}
	SIZE sizeWnd = { static_cast<LONG>(BarWindowPosClass::w), static_cast<LONG>(BarWindowPosClass::h) };
	POINT ptSrc = { 0,0 };
	POINT ptDst = { 0,0 };
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

	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	// 画布
	IMAGE barBackground(BarWindowPosClass::w, BarWindowPosClass::h);

	// 初始化 D2D DC
	CComPtr<ID2D1DCRenderTarget> DCRenderTarget;
	{
		// 创建位图兼容的 DC Render Target
		ID2D1DCRenderTarget* pDCRenderTarget = nullptr;
		HRESULT hr = D2DFactory->CreateDCRenderTarget(&D2DProperty, &pDCRenderTarget);
		DCRenderTarget.Attach(pDCRenderTarget);

		RECT PptBackgroundWindowRect = { 0, 0, barBackground.getwidth(),barBackground.getheight() };
		DCRenderTarget->BindDC(GetImageHDC(&barBackground), &PptBackgroundWindowRect);

		// 设置抗锯齿
		DCRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE::D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	}

	chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();
	for (int forNum = 1; !offSignal; forNum = 2)
	{
		// 渲染 UI
		{
			DCRenderTarget->BeginDraw();

			{
				D2D1_COLOR_F clearColor = ConvertToD2DColor(RGBA(0, 0, 0, 128));
				DCRenderTarget->Clear(&clearColor);
			}

			DCRenderTarget->EndDraw();

			{
				// 设置窗口位置
				POINT ptDst = { 0, 0 };

				ulwi.pptDst = &ptDst;
				ulwi.hdcSrc = GetImageHDC(&barBackground);
				UpdateLayeredWindowIndirect(floating_window, &ulwi);
			}
		}

		if (forNum == 1)
		{
			IdtWindowsIsVisible.floatingWindow = true;
		}
		// 帧率锁
		{
			double delay = 1000.0 / 24.0 - chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			if (delay >= 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
		}
		reckon = chrono::high_resolution_clock::now();
	}

	return;
}

// ====================
// 交互

// 初始化
void BarInitializationClass::Initialization()
{
	threadStatus[L"BarInitialization"] = true;

	// 初始化
	InitializeWindow();
	InitializeMedia();
	InitializeUI();

	// 线程
	thread(FloatingInstallHook).detach();
	thread(BarUISetClass::Rendering).detach();

	// 等待

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	// 反初始化

	unsigned int waitTimes = 1;
	for (; waitTimes <= 10; waitTimes++)
	{
		if (!threadStatus[L"BarUI"] &&
			!threadStatus[L"BarDraw"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}

	threadStatus[L"BarInitialization"] = false;
	return;
}
void BarInitializationClass::InitializeWindow()
{
	DisableResizing(floating_window, true); // hiex 禁止窗口拉伸

	SetWindowLong(floating_window, GWL_STYLE, GetWindowLong(floating_window, GWL_STYLE) & ~WS_CAPTION); // 隐藏窗口标题栏
	SetWindowLong(floating_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW); // 隐藏窗口任务栏图标

	BarWindowPosClass::x = 0;
	BarWindowPosClass::y = 0;
	BarWindowPosClass::w = 1000;// MainMonitor.MonitorWidth;
	BarWindowPosClass::h = 1000;// MainMonitor.MonitorHeight;
	BarWindowPosClass::pct = 255;
	SetWindowPos(floating_window, NULL, BarWindowPosClass::x, BarWindowPosClass::y, BarWindowPosClass::w, BarWindowPosClass::h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_DRAWFRAME); // 设置窗口位置尺寸
}
void BarInitializationClass::InitializeMedia()
{
	BarUISetClass::barMedia.LoadExImage();
}
void BarInitializationClass::InitializeUI()
{
	// 定义 UI 控件

	{
		BarUiSVGClass* svg = new BarUiSVGClass(0.0, 0.0);
		svg->svg.initialization()
			svg->SetWH
			BarUISetClass::svgMap.insert(make_pair<BarUISetSvgEnum::logo, >)
	}
}