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

// 位置继承
// 根据类型计算继承坐标原点
BarUiInheritClass::BarUiInheritClass(BarUiInheritEnum typeT, const optional<BarUiValueClass>& w, const optional<BarUiValueClass>& h, const optional<BarUiValueClass>& xT, const optional<BarUiValueClass>& yT, const optional<BarUiValueClass>& wT, const optional<BarUiValueClass>& hT)
{
	// w/h 为控件自身的宽高
	type = typeT;
	// TODO 拓展更多类型组合
	if (type == BarUiInheritEnum::TopLeft)
	{
		if (xT.has_value() && wT.has_value() && w.has_value()) x = xT.value().val - wT.value().val / 2.0 + w.value().val / 2.0;
		if (yT.has_value() && hT.has_value() && w.has_value()) y = yT.value().val - hT.value().val / 2.0 + h.value().val / 2.0;
	}
	if (type == BarUiInheritEnum::Center)
	{
		if (xT.has_value()) x = xT.value().val;
		if (yT.has_value()) y = yT.value().val;
	}
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
		col1 = color1.value().colVal;

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
		col2 = color2.value().colVal;

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
bool BarUIRendering::Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg, optional<reference_wrapper<const BarUiInheritClass>> inh = nullopt)
{
	// 判断是否合法
	{
		if (svg.enable.val == false) return false;
		if (!svg.svg.has_value()) return false;
	}

	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = svg.svg.value().GetVal();
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
	double tarX = 0.0;
	double tarY = 0.0;
	double tarW = 0.0;
	double tarH = 0.0;

	// 设置继承坐标原点
	if (inh.has_value())
	{
		tarX = inh.value().get().x;
		tarY = inh.value().get().y;
	}

	// 赋值
	{
		if (svg.x.has_value()) tarX += svg.x.value().val;
		if (svg.y.has_value()) tarX += svg.y.value().val;

		// 宽高计算
		if (svg.w.has_value() && svg.h.has_value())
		{
			// 拉伸
			tarW = svg.w.value().val;
			tarH = svg.h.value().val;
		}
		else if (svg.w.has_value() && !svg.h.has_value())
		{
			// 高度自动
			tarW = svg.w.value().val;
			tarH = document->height() * (svg.w.value().val / document->width());
		}
		else if (!svg.w.has_value() && svg.h.has_value())
		{
			// 宽度自动
			tarW = document->width() * (svg.h.value().val / document->height());
			tarH = svg.h.value().val;
		}
		else
		{
			// 原尺寸
			tarW = document->width();
			tarH = document->height();
		}
	}

	int targetWidth = svg.h->val;
	int targetHeight = svg.w->val;

	unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.svg.value().GetVal());
	//unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromFile("D:\\Downloads\\Inkeys.svg");

	lunasvg::Bitmap bitmap = document->renderToBitmap(targetWidth, targetHeight);

	CComPtr<ID2D1Bitmap> d2dBitmap;
	D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

	// lunasvg 文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
	DCRenderTarget->CreateBitmap(
		D2D1::SizeU(bitmap.width(), bitmap.height()),
		bitmap.data(),
		bitmap.width() * 4, // stride
		props,
		&d2dBitmap);

	D2D1_RECT_F destRect = D2D1::RectF(0, 0, (float)targetWidth, (float)targetHeight);
	DCRenderTarget->DrawBitmap(
		d2dBitmap,
		destRect,        // 目标矩形
		1.0f,            // 不透明度
		D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
		nullptr          // 源rect, null表示全部
	);
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
		DCRenderTarget->BeginDraw();

		{
			D2D1_COLOR_F clearColor = ConvertToD2DColor(RGBA(0, 0, 0, 128));
			DCRenderTarget->Clear(&clearColor);
		}
		{
			int targetWidth = 500;
			int targetHeight = 500;

			//unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svgText);
			unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromFile("D:\\Downloads\\Inkeys.svg");

			lunasvg::Bitmap bitmap = document->renderToBitmap(targetWidth, targetHeight);

			CComPtr<ID2D1Bitmap> d2dBitmap;
			D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

			// lunasvg文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
			DCRenderTarget->CreateBitmap(
				D2D1::SizeU(bitmap.width(), bitmap.height()),
				bitmap.data(),
				bitmap.width() * 4, // stride
				props,
				&d2dBitmap);

			D2D1_RECT_F destRect = D2D1::RectF(0, 0, (float)targetWidth, (float)targetHeight);
			DCRenderTarget->DrawBitmap(
				d2dBitmap,
				destRect,        // 目标矩形
				1.0f,            // 不透明度
				D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
				nullptr          // 源rect, null表示全部
			);
		}

		DCRenderTarget->EndDraw();

		{
			// 设置窗口位置
			POINT ptDst = { 0, 0 };

			ulwi.pptDst = &ptDst;
			ulwi.hdcSrc = GetImageHDC(&barBackground);
			UpdateLayeredWindowIndirect(floating_window, &ulwi);
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

	thread FloatingInstallHookThread(FloatingInstallHook);
	FloatingInstallHookThread.detach();

	thread(BarUISetClass::Rendering).detach();

	// 等待

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

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
	//
}