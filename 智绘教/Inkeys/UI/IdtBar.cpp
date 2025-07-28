#include "IdtBar.h"

#include "../../IdtConfiguration.h"
#include "../../IdtD2DPreparation.h"
#include "../../IdtDisplayManagement.h"
#include "../../IdtDrawpad.h"
#include "../../IdtWindow.h"
#include "../Conv/IdtColor.h"
#include "../Load/IdtLoad.h"

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

/// 单个 UI 值
//// 单个 SVG 组件
void BarUiSVGClass::InitializationFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	IdtLoad::ExtractResourceString(valT, resType, resName);
	InitializationFromString(valT);
}

/// 继承
//// 根据类型计算继承坐标原点
BarUiInheritClass::BarUiInheritClass(BarUiInheritEnum typeT, double w, double h, double xT, double yT, double wT, double hT)
{
	// w/h 为控件自身的宽高 -> 最终得出的都是左上角绘制坐标 -> 方便绘制
	type = typeT;

	// TODO 拓展更多类型组合
	if (type == BarUiInheritEnum::TopLeft) { x = xT, y = yT; }
	if (type == BarUiInheritEnum::Center) { x = xT + wT / 2.0 - w / 2.0, y = yT + hT / 2.0 - h / 2.0; }
}
//// 继承基类
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, shape.x.val, shape.y.val, shape.w.val, shape.h.val)); }
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiSuperellipseClass& superellipse) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, superellipse.x.val, superellipse.y.val, superellipse.w.val, superellipse.h.val)); }
BarUiInheritClass BarUiInnheritBaseClass::Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, svg.x.val, svg.y.val, svg.w.val, svg.h.val)); }
BarUiPctInheritClass BarUiInnheritBaseClass::InheritPct(const BarUiShapeClass& shape) { return UpInhPct(BarUiPctInheritClass(shape.pct.val)); }
BarUiPctInheritClass BarUiInnheritBaseClass::InheritPct(const BarUiSuperellipseClass& superellipse) { return UpInhPct(BarUiPctInheritClass(superellipse.pct.val)); }
BarUiPctInheritClass BarUiInnheritBaseClass::InheritPct(const BarUiSVGClass& svg) { return UpInhPct(BarUiPctInheritClass(svg.pct.val)); }

/// 单个控件值
//// 单个 SVG 控件
bool BarUiSVGClass::SetWH(optional<double> wT, optional<double> hT, BarUiValueModeEnum type)
{
	double tarW, tarH;

	if (wT.has_value() && hT.has_value()) { tarW = wT.value(), tarH = hT.value(); }
	else
	{
		// 解析SVG
		unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.GetVal());
		if (!document) return false; // 解析失败

		if (wT.has_value() && !hT.has_value())
		{
			// 高度自动
			tarW = wT.value();
			tarH = document->height() * (wT.value() / document->width());
		}
		else if (!wT.has_value() && hT.has_value())
		{
			// 宽度自动
			tarW = document->width() * (hT.value() / document->height());
			tarH = hT.value();
		}
		else
		{
			// 原尺寸
			tarW = document->width();
			tarH = document->height();
		}
	}

	w.Initialization(tarW, type);
	h.Initialization(tarW, type);

	return true;
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
bool BarUIRendering::Shape(ID2D1DCRenderTarget* DCRenderTarget, const BarUiShapeClass& shape, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct)
{
	// 判断是否启用
	if (shape.enable.val == false) return false;
	if (!shape.fill.has_value() && !shape.frame.has_value()) return false;

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = shape.w.val;
	double tarH = shape.h.val;
	double tarPct = pct.pct; // 透明度

	double tarRw = 0.0;
	double tarRh = 0.0;
	if (shape.rw.has_value()) tarRw = shape.rw.value().val;
	if (shape.rh.has_value()) tarRh = shape.rh.value().val;

	// 渲染到 DC
	{
		D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY), static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH)), static_cast<FLOAT>(tarRw), static_cast<FLOAT>(tarRh));

		// 渲染填充
		if (shape.fill.has_value())
		{
			COLORREF fill = shape.fill.value().val;

			CComPtr<ID2D1SolidColorBrush> spFillBrush;
			DCRenderTarget->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(fill, tarPct), &spFillBrush);

			DCRenderTarget->FillRoundedRectangle(&roundedRect, spFillBrush);
		}
		// 渲染边框
		if (shape.frame.has_value())
		{
			COLORREF frame = shape.frame.value().val;

			CComPtr<ID2D1SolidColorBrush> spBorderBrush;
			DCRenderTarget->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(frame, tarPct), &spBorderBrush);

			FLOAT strokeWidth = 4.0f;
			if (shape.ft.has_value()) strokeWidth = static_cast<FLOAT>(shape.ft.value().val);
			DCRenderTarget->DrawRoundedRectangle(&roundedRect, spBorderBrush, strokeWidth);
		}
	}

	return true;
}
bool BarUIRendering::Superellipse(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct)
{
	// 判断是否启用
	if (superellipse.enable.val == false) return false;
	if (!superellipse.fill.has_value() && !superellipse.frame.has_value()) return false;

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = superellipse.w.val;
	double tarH = superellipse.h.val;
	double tarPct = pct.pct; // 透明度

	double tarN = 4.0;
	if (superellipse.n.has_value()) tarN = superellipse.n.value().val;

	auto genPoints = [&](float left, float top, float width, float height, float n, int segs)
		{
			const float Pi = 3.14159265359f;
			float a = width / 2.0f;
			float b = height / 2.0f;
			float cx = left + a;
			float cy = top + b;

			vector<D2D1_POINT_2F> pts;
			for (int i = 0; i < segs; i++)
			{
				float theta = 2.0f * Pi * i / segs;
				float cosT = cosf(theta);
				float sinT = sinf(theta);
				float x0 = a * (cosT >= 0 ? powf(cosT, 2.0f / n) : -powf(-cosT, 2.0f / n));
				float y0 = b * (sinT >= 0 ? powf(sinT, 2.0f / n) : -powf(-sinT, 2.0f / n));
				pts.emplace_back(D2D1::Point2F(cx + x0, cy + y0));
			}
			pts.emplace_back(pts[0]); // 闭合

			return pts;
		};

	auto toBeziers = [](const vector<D2D1_POINT_2F>& pts, float tension = 1.0f)
		{
			// Catmull-Rom到Bezier转换，首尾闭合
			vector<D2D1_BEZIER_SEGMENT> beziers;
			int N = static_cast<int>(pts.size()) - 1; // pts已闭合，最后一个是等于第一个
			if (N < 3) return beziers;

			for (int i = 0; i < N; i++)
			{
				D2D1_POINT_2F p0 = pts[(i - 1 + N) % N];
				D2D1_POINT_2F p1 = pts[i];
				D2D1_POINT_2F p2 = pts[(i + 1) % N];
				D2D1_POINT_2F p3 = pts[(i + 2) % N];

				D2D1_BEZIER_SEGMENT seg;
				seg.point1 =
				{
					p1.x + (p2.x - p0.x) / 6.0f * tension,
					p1.y + (p2.y - p0.y) / 6.0f * tension
				};
				seg.point2 =
				{
					p2.x - (p3.x - p1.x) / 6.0f * tension,
					p2.y - (p3.y - p1.y) / 6.0f * tension
				};
				seg.point3 = p2;

				beziers.push_back(seg);
			}
			return beziers;
		};

	// 计算边框路径
	int segs = clamp(static_cast<int>((tarW + tarH) / 8.0), 24, 128);
	vector<D2D1_POINT_2F> pts = genPoints(static_cast<float>(tarX), static_cast<float>(tarY), static_cast<float>(tarW), static_cast<float>(tarH), static_cast<float>(tarN), segs);
	vector<D2D1_BEZIER_SEGMENT> beziers = toBeziers(pts);
	if (beziers.empty()) return false;

	CComPtr<ID2D1PathGeometry> geometry;
	{
		CComPtr<ID2D1Factory> factory;
		DCRenderTarget->GetFactory(&factory);

		factory->CreatePathGeometry(&geometry);
	}

	{
		CComPtr<ID2D1GeometrySink> sink;
		geometry->Open(&sink);
		sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
		sink->AddBeziers(beziers.data(), static_cast<UINT32>(beziers.size()));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		sink->Close();
	}

	// 渲染到 DC
	{
		// 渲染填充
		if (superellipse.fill.has_value())
		{
			COLORREF fill = superellipse.fill.value().val;

			CComPtr<ID2D1SolidColorBrush> spFillBrush;
			DCRenderTarget->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(fill, tarPct), &spFillBrush);

			DCRenderTarget->FillGeometry(geometry, spFillBrush);
		}
		// 渲染边框
		if (superellipse.frame.has_value())
		{
			COLORREF frame = superellipse.frame.value().val;

			CComPtr<ID2D1SolidColorBrush> spBorderBrush;
			DCRenderTarget->CreateSolidColorBrush(IdtColor::ConvertToD2dColor(frame, tarPct), &spBorderBrush);

			FLOAT strokeWidth = 4.0f;
			if (superellipse.ft.has_value()) strokeWidth = static_cast<FLOAT>(superellipse.ft.value().val);
			DCRenderTarget->DrawGeometry(geometry, spBorderBrush, strokeWidth);
		}
	}

	return true;
}
bool BarUIRendering::Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct)
{
	// 判断是否启用
	if (svg.enable.val == false) return false;

	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = svg.svg.GetVal();
		// 替换颜色，如果有
		if (svg.color1.has_value() || svg.color2.has_value())
		{
			svgContent = SvgReplaceColor(svgContent, svg.color1, svg.color2);
		}

		// 解析SVG
		document = lunasvg::Document::loadFromData(svgContent);
		if (!document) return false; // 解析失败
	}

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = svg.w.val;
	double tarH = svg.h.val;
	double tarPct = pct.pct; // 透明度

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
		D2D1_RECT_F destRect = D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY), static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH));
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

	wstring fps;
	for (int forNum = 1; !offSignal; forNum = 2)
	{
		// 渲染 UI
		{
			DCRenderTarget->BeginDraw();

			{
				D2D1_COLOR_F clearColor = ConvertToD2DColor(RGBA(0, 0, 0, 0));
				DCRenderTarget->Clear(&clearColor);
			}

			using enum BarUiInheritEnum;
			{
				auto obj = BarUISetSuperellipseEnum::MainButton;
				BarUIRendering::Superellipse(DCRenderTarget, *superellipseMap[obj], superellipseMap[obj]->Inherit(), superellipseMap[obj]->InheritPct());
			}
			{
				auto obj = BarUISetSvgEnum::logo1;
				BarUIRendering::Svg(DCRenderTarget, *svgMap[obj], svgMap[obj]->Inherit(Center, *superellipseMap[BarUISetSuperellipseEnum::MainButton]), svgMap[obj]->InheritPct());
			}

			{
				CComPtr<IDWriteTextFormat> pTextFormat;
				D2DTextFactory->CreateTextFormat(
					L"Microsoft YaHei", // 字体名
					nullptr,
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					40, // 字号
					L"zh-cn",
					&pTextFormat);

				// 3. 创建画刷
				CComPtr<ID2D1SolidColorBrush> pBrush;
				DCRenderTarget->CreateSolidColorBrush(
					D2D1::ColorF(D2D1::ColorF(255, 0, 0, 1.0)),
					&pBrush);

				// 4. 设定绘制区域
				D2D1_RECT_F layoutRect = D2D1::RectF(100, 100, 1000, 1000);

				// 5. 绘制文本
				DCRenderTarget->DrawTextW(
					fps.c_str(),              // text
					(UINT32)fps.length(),     // text length
					pTextFormat,               // format
					layoutRect,                // layout rect
					pBrush,                    // brush
					D2D1_DRAW_TEXT_OPTIONS_NONE
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
		}

		if (forNum == 1)
		{
			IdtWindowsIsVisible.floatingWindow = true;
		}
		// 帧率锁
		{
			double delay = 1000.0 / 60.0 - chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			if (delay >= 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
		}

		{
			double cost = chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			fps = format(L"{:.2f} FPS", 1000.0 / cost);
		}
		reckon = chrono::high_resolution_clock::now();
	}

	return;
}

// ====================
// 交互
void BarUISetClass::Interact()
{
	struct
	{
		ExMessage msg;
		int x, y;
	}Msg;

	while (!offSignal)
	{
		hiex::getmessage_win32(&Msg.msg, EM_MOUSE, floating_window);

		{
			if (superellipseMap[BarUISetSuperellipseEnum::MainButton]->IsClick(Msg.msg.x, Msg.msg.y))
			{
				if (Msg.msg.message == WM_LBUTTONDOWN)
				{
					Msg.x = Msg.msg.x, Msg.y = Msg.msg.y;
					{
						double moveDis = Seek(Msg.msg);
					}
					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}

				if (Msg.msg.message == WM_RBUTTONDOWN && setlist.RightClickClose)
				{
					if (MessageBox(floating_window, L"Whether to turn off 智绘教Inkeys?\n是否关闭 智绘教Inkeys？", L"Inkeys Tips | 智绘教提示", MB_OKCANCEL | MB_SYSTEMMODAL) == 1) CloseProgram();
					hiex::flushmessage_win32(EM_MOUSE, floating_window);
				}
			}
		}
	}
}
double BarUISetClass::Seek(const ExMessage& msg)
{
	if (!KeyBoradDown[VK_LBUTTON]) return 0;

	POINT p;
	GetCursorPos(&p);

	double firX = static_cast<double>(p.x);
	double firY = static_cast<double>(p.y);

	double ret = 0.0;

	while (1)
	{
		if (!KeyBoradDown[VK_LBUTTON]) break;
		GetCursorPos(&p);

		if (firX == p.x && firY == p.y) continue;

		superellipseMap[BarUISetSuperellipseEnum::MainButton]->x.val += static_cast<double>(p.x - firX);
		superellipseMap[BarUISetSuperellipseEnum::MainButton]->y.val += static_cast<double>(p.y - firY);

		ret += sqrt((p.x - firX) * (p.x - firX) + (p.y - firY) * (p.y - firY));
		firX = static_cast<double>(p.x), firY = static_cast<double>(p.y);

		//if (setlist.regularSetting.moveRecover)
		//{
		//	if (ret > 20 && target_status != 0)
		//	{
		//		target_status = 0;
		//	}
		//}
	}

	return ret;
}

// 初始化
void BarInitializationClass::Initialization()
{
	threadStatus[L"BarInitialization"] = true;

	BarUISetClass barUISet;

	// 初始化
	InitializeWindow();
	InitializeMedia(barUISet);
	InitializeUI(barUISet);

	// 线程
	thread(FloatingInstallHook).detach();
	thread([&]() { barUISet.Rendering(); }).detach();
	thread([&]() { barUISet.Interact(); }).detach();

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
	BarWindowPosClass::w = 2000;// MainMonitor.MonitorWidth;
	BarWindowPosClass::h = 2000;// MainMonitor.MonitorHeight;
	BarWindowPosClass::pct = 255;
	SetWindowPos(floating_window, NULL, BarWindowPosClass::x, BarWindowPosClass::y, BarWindowPosClass::w, BarWindowPosClass::h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_DRAWFRAME); // 设置窗口位置尺寸
}
void BarInitializationClass::InitializeMedia(BarUISetClass& barUISet)
{
	barUISet.barMedia.LoadExImage();
}
void BarInitializationClass::InitializeUI(BarUISetClass& barUISet)
{
	// 定义 UI 控件
	/*{
		auto shape = make_shared<BarUiShapeClass>(500.0, 500.0, 80.0, 80.0, 20.0, 20.0, 2.0, RGB(0, 0, 0), RGB(255, 255, 255));
		shape->pct.Initialization(0.6);
		shape->enable.Initialization(true);
		barUISet.shapeMap[BarUISetShapeEnum::MainButton] = shape;
	}*/
	{
		auto superellipse = make_shared<BarUiSuperellipseClass>(500.0, 500.0, 300.0, 300.0, 3.0, 2.0, RGB(0, 0, 0), RGB(255, 255, 255));
		superellipse->pct.Initialization(0.6);
		superellipse->enable.Initialization(true);
		barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton] = superellipse;
	}
	{
		auto svg = make_shared<BarUiSVGClass>(0.0, 0.0);
		svg->InitializationFromResource(L"UI", L"logo1");
		svg->SetWH(nullopt, 300.0); // 必须在 Initial 后
		svg->enable.Initialization(true);
		barUISet.svgMap[BarUISetSvgEnum::logo1] = svg;
	}
}