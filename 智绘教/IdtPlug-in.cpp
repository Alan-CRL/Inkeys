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

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtMagnification.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtWindow.h"
#include "IdtOther.h"
#include "IdtD2DPreparation.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtImage.h"
#include "IdtState.h"

// --------------------------------------------------
// PPT controls | PPT 控件

// TODO 1 控件缩放、控件置于左右两侧、控件拖动、UI 计算与绘制分离

#import "PptCOM.tlb" // C# Class Library PptCOM Project Library (PptCOM. cs) | C# 类库 PptCOM 项目库 (PptCOM. cs)
using namespace PptCOM;
IPptCOMServerPtr PptCOMPto;

PptUiRoundRectWidgetClass pptUiRoundRectWidget[55], pptUiRoundRectWidgetTarget[55];
PptUiImageWidgetClass pptUiImageWidget[15], pptUiImageWidgetTarget[15];
PptUiWordsWidgetClass pptUiWordsWidget[15], pptUiWordsWidgetTarget[15];

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

float PPTUIScale = 1.0f;
bool PPTUIScaleRecommend = true;

PptImgStruct PptImg = { false }; // It stores image data generated during slide shows. | 其存储幻灯片放映时产生的图像数据。
PptInfoStateStruct PptInfoState = { -1, -1 }; // It stores the current status of the slide show software, where First represents the total number of slide pages and Second represents the current slide number. | 其存储幻灯片放映软件当前的状态，First 代表总幻灯片页数，Second 代表当前幻灯片编号。
PptInfoStateStruct PptInfoStateBuffer = { -1, -1 }; // Buffered variables for ·PptInfoState·. *1 | PptInfoState 的缓冲变量。*1

IMAGE PptIcon[5]; // Button icons for PPT controls | PPT 控件的按键图标
IMAGE PptWindowBackground; // PPT window background canvas | PPT 窗口背景画布

bool PptWindowBackgroundUiChange = true;

wstring pptComVersion;
wstring GetPptComVersion()
{
	wstring ret = L"Error: COM库(.dll) 不存在，且发生严重错误，返回值被忽略";

	if (_waccess((StringToWstring(globalPath) + L"PptCOM.dll").c_str(), 4) == 0)
	{
		try
		{
			ret = BstrToWstring(PptCOMPto->GetVersion());
			if (!regex_match(ret, wregex(L"\\d{8}[a-z]"))) ret = L"Error: " + ret;
		}
		catch (_com_error& err)
		{
			ret = L"Error: COM库(.dll) 存在，COM成功初始化，但C++端COM接口异常：" + wstring(err.ErrorMessage());
		}
	}
	else
	{
		wchar_t absolutePath[_MAX_PATH];

		if (_wfullpath(absolutePath, L"PptCOM.dll", _MAX_PATH) != NULL)
		{
			ret = L"Error: COM库(.dll) 不存在，预期调用目录为：\"" + StringToWstring(globalPath) + L"PptCOM.dll\"";
		}
		else ret = L"Error: COM库(.dll) 不存在，预期调用目录测算失败";
	}

	return ret;
}

wstring GetPptTitle()
{
	wstring ret = L"";

	try
	{
		ret = BstrToWstring(PptCOMPto->slideNameIndex());

		return ret;
	}
	catch (_com_error)
	{
	}

	return ret;
}
HWND GetPptShow()
{
	HWND hWnd = NULL;

	try
	{
		_variant_t result = PptCOMPto->GetPptHwnd();
		hWnd = (HWND)result.llVal;

		return hWnd;
	}
	catch (_com_error)
	{
	}

	return NULL;
}

void NextPptSlides(int check)
{
	try
	{
		PptCOMPto->NextSlideShow(check);
	}
	catch (_com_error)
	{
	}
	return;
}
void PreviousPptSlides()
{
	try
	{
		PptCOMPto->PreviousSlideShow();
	}
	catch (_com_error)
	{
	}
	return;
}
bool EndPptShow()
{
	try
	{
		PptCOMPto->EndSlideShow();

		return true;
	}
	catch (_com_error)
	{
	}

	return false;
}

// PPT 状态获取轮询函数
void GetPptState()
{
	threadStatus[L"GetPptState"] = true;

	// 初始化
	{
		bool rel = false;

		try
		{
			_com_util::CheckError(PptCOMPto.CreateInstance(_uuidof(PptCOMServer)));
			rel = PptCOMPto->Initialization(&PptInfoState.TotalPage, &PptInfoState.CurrentPage);
		}
		catch (_com_error)
		{
		}

		pptComVersion = GetPptComVersion();
	}

	while (!offSignal)
	{
		int tmp = -1;
		try
		{
			tmp = PptCOMPto->IsPptOpen();
		}
		catch (_com_error)
		{
		}

		if (tmp <= 0)
		{
			for (int i = 0; i <= 30 && !offSignal; i++)
				this_thread::sleep_for(chrono::milliseconds(100));
		}
	}

	threadStatus[L"GetPptState"] = false;
}

void DrawControlWindow()
{
	threadStatus[L"DrawControlWindow"] = true;

	//ppt窗口初始化
	MainMonitorStruct PPTMainMonitor;
	{
		DisableResizing(ppt_window, true);//禁止窗口拉伸
		SetWindowLong(ppt_window, GWL_STYLE, GetWindowLong(ppt_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
		SetWindowPos(ppt_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME | SWP_NOACTIVATE);
		SetWindowLong(ppt_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

		shared_lock<shared_mutex> DisplaysInfoLock2(DisplaysInfoSm);
		PPTMainMonitor = MainMonitor;
		DisplaysInfoLock2.unlock();

		PptWindowBackground.Resize(PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight);
		SetWindowPos(ppt_window, NULL, PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	// 创建 EasyX 兼容的 DC Render Target
	ID2D1DCRenderTarget* DCRenderTarget = nullptr;
	D2DFactory->CreateDCRenderTarget(&D2DProperty, &DCRenderTarget);

	// 绑定 EasyX DC
	RECT PptBackgroundWindowRect = { 0, 0, PptWindowBackground.getwidth(), PptWindowBackground.getheight() };
	DCRenderTarget->BindDC(GetImageHDC(&PptWindowBackground), &PptBackgroundWindowRect);

	// 设置抗锯齿
	DCRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE::D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	//媒体初始化
	ID2D1Bitmap* PptIconBitmap[5] = { NULL };
	{
		loadimage(&PptIcon[1], L"PNG", L"ppt1");
		loadimage(&PptIcon[2], L"PNG", L"ppt2");
		loadimage(&PptIcon[3], L"PNG", L"ppt3");

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
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
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
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
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
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
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

	//UI 初始化
	{
		PPTUIScale = 1.0f;

		// 左侧控件
		{
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].X = { (5) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].Y = { (float)PPTMainMonitor.MonitorHeight + (5) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].Width = { (185) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].Height = { (60) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].EllipseWidth = { (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].EllipseHeight = { (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].FrameThickness = { (1) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].FillColor = { RGBA(225, 225, 225, 0), 10, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].X = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].Width = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].Height = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].EllipseWidth = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].EllipseHeight = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].FrameThickness = { (1) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].FillColor = { RGBA(250, 250, 250, 0), 3, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

				pptUiImageWidget[PptUiImageWidgetID::LeftSide_PreviousPage].X = { (float)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_PreviousPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_PreviousPage].Width = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_PreviousPage].Height = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_PreviousPage].Transparency = { 0, 10, 1 };
			}
			{
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].Left = { (float)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + (float)PPTUIControl[L"RoundRect/RoundRectLeft1/width"].v + (5) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].Top = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].Right = { (float)PPTUIControl[L"Words/InfoLeft/left"].v + (65) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].Bottom = { (float)PPTUIControl[L"Words/InfoLeft/top"].v + (55) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].WordsHeight = { (20) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].WordsColor = { RGBA(50, 50, 50, 0), 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::LeftSide_PageNum].WordsContent = L"Inkeys";
			}
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].X = { (float)PPTUIControl[L"Words/InfoLeft/right"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].Width = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].Height = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].EllipseWidth = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].EllipseHeight = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].FrameThickness = { (1) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].FillColor = { RGBA(250, 250, 250, 0), 3, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

				pptUiImageWidget[PptUiImageWidgetID::LeftSide_NextPage].X = { (float)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_NextPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_NextPage].Width = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_NextPage].Height = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::LeftSide_NextPage].Transparency = { 0, 20, 1 };
			}
		}
		// 中间控件
		{
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].X = { (float)PPTMainMonitor.MonitorWidth / 2 - (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].Y = { (float)PPTMainMonitor.MonitorHeight + (5) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].Width = { (60) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].Height = { (60) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].EllipseWidth = { (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].EllipseHeight = { (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].FrameThickness = { (1) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].FillColor = { RGBA(225, 225, 225, 0), 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].X = { (float)PPTUIControl[L"RoundRect/RoundRectMiddleLeft/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].Y = { (float)PPTUIControl[L"RoundRect/RoundRectMiddleLeft/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].Width = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].Height = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].EllipseWidth = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].Height = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].FrameThickness = { (1) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].FillColor = { RGBA(250, 250, 250, 0), 3, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_EndShow].X = { (float)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_EndShow].Y = { (float)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_EndShow].Width = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_EndShow].Height = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_EndShow].Transparency = { 0, 10, 1 };
			}
		}
		// 右侧控件
		{
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].X = { (float)PPTMainMonitor.MonitorWidth - (190) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].Y = { (float)PPTMainMonitor.MonitorHeight + (5) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].Width = { (185) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].Height = { (60) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].EllipseWidth = { (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].EllipseHeight = { (30) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].FrameThickness = { (1) * PPTUIScale, 5, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].FillColor = { RGBA(225, 225, 225, 0), 10, 1 };
			pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].X = { (float)PPTUIControl[L"RoundRect/RoundRectRight/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].Width = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].Height = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].EllipseWidth = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].EllipseHeight = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].FrameThickness = { (1) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].FillColor = { RGBA(250, 250, 250, 0), 3, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

				pptUiImageWidget[PptUiImageWidgetID::RightSide_PreviousPage].X = { (float)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_PreviousPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectRight1/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_PreviousPage].Width = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_PreviousPage].Height = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_PreviousPage].Transparency = { 0, 10, 1 };
			}
			{
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].Left = { (float)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + (float)PPTUIControl[L"RoundRect/RoundRectRight1/width"].v + (5) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].Top = { (float)PPTUIControl[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].Right = { (float)PPTUIControl[L"Words/InfoRight/left"].v + (65) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].Bottom = { (float)PPTUIControl[L"Words/InfoRight/top"].v + (55) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].WordsHeight = { (20) * PPTUIScale, 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].WordsColor = { RGBA(50, 50, 50, 0), 5, 1 };
				pptUiWordsWidget[PptUiWordsWidgetID::RightSide_PageNum].WordsContent = L"Inkeys";
			}
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].X = { (float)PPTUIControl[L"Words/InfoRight/right"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].Width = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].Height = { (50) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].EllipseWidth = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].EllipseHeight = { (35) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].FrameThickness = { (1) * PPTUIScale, 5, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].FillColor = { RGBA(250, 250, 250, 0), 3, 1 };
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].FrameColor = { RGBA(200, 200, 200, 0), 10, 1 };

				pptUiImageWidget[PptUiImageWidgetID::RightSide_NextPage].X = { (float)PPTUIControl[L"RoundRect/RoundRectRight2/x"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_NextPage].Y = { (float)PPTUIControl[L"RoundRect/RoundRectRight2/y"].v + (5) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_NextPage].Width = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_NextPage].Height = { (40) * PPTUIScale, 5, 1 };
				pptUiImageWidget[PptUiImageWidgetID::RightSide_NextPage].Transparency = { 0, 10, 1 };
			}
		}

		memcpy(pptUiRoundRectWidgetTarget, pptUiRoundRectWidget, sizeof(pptUiRoundRectWidget));
		memcpy(pptUiImageWidgetTarget, pptUiImageWidget, sizeof(pptUiRoundRectWidget));
		memcpy(pptUiWordsWidgetTarget, pptUiWordsWidget, sizeof(pptUiRoundRectWidget));
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
	SIZE sizeWnd = { PptWindowBackground.getwidth(),PptWindowBackground.getheight() };
	POINT ptDst = { PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top };

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

	while (!(GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(ppt_window, GWL_EXSTYLE, GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(ppt_window, GWL_EXSTYLE, GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	int TotalSlides = -1, TotalSlidesLast = -2;
	int CurrentSlides = -1;

	bool Initialization = false; // 控件初始化完毕
	clock_t tRecord = clock();
	for (bool IsShowWindow = false; !offSignal;)
	{
		int TotalSlides = PptInfoState.TotalPage;
		int CurrentSlides = PptInfoState.CurrentPage;

		// 监视器监测
		{
			shared_lock<shared_mutex> DisplaysInfoLock2(DisplaysInfoSm);
			bool MainMonitorDifferent = (PPTMainMonitor != MainMonitor);
			if (MainMonitorDifferent) PPTMainMonitor = MainMonitor;
			DisplaysInfoLock2.unlock();

			if (MainMonitorDifferent)
			{
				PptWindowBackground.Resize(PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight);
				SetWindowPos(ppt_window, NULL, PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);

				sizeWnd = { PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight };
				POINT ptDst = { PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top };

				RECT PptBackgroundWindowRect = { 0, 0, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight };
				DCRenderTarget->BindDC(GetImageHDC(&PptWindowBackground), &PptBackgroundWindowRect);
			}
		}
		// UI 计算部分
		{
			// UI 单次修改计算（例如按钮）
			if (TotalSlides != TotalSlidesLast)
			{
				TotalSlidesLast = TotalSlides;
				if (TotalSlides != -1)
				{
					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].FillColor.v, 160);
					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].FrameColor.v, 160);

					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].FillColor.v, 160);
					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].FrameColor.v, 160);

					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].FillColor.v, 160);
					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_TabSlideWidget_EndShow].FrameColor.v, 160);

					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].FillColor.v, 160);
					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].FrameColor.v, 160);

					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].FillColor.v, 160);
					SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].FrameColor.v, 160);
				}
			}

			// UI 控件实时计算
			if (TotalSlides != -1)
			{
				// 左侧控件
				{
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].X.v = (5) * PPTUIScale;
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].Y.v = PPTMainMonitor.MonitorHeight - (65) * PPTUIScale;
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].Width.v = (185) * PPTUIScale;
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].Height.v = (60) * PPTUIScale;
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].EllipseWidth.v = (30) * PPTUIScale;
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].EllipseHeight.v = (30) * PPTUIScale;
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::LeftSide_PageWidget].FrameThickness.v = (1) * PPTUIScale;

					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft/fill"].v, 160);
					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft/frame"].v, 160);

					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/frame/width"].v = (1) * PPTUIScale;

						{
							PPTUIControlTarget[L"Image/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoLeft/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoLeft/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoLeft/right"].v = PPTUIControlTarget[L"Words/InfoLeft/left"].v + (65) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoLeft/bottom"].v = PPTUIControlTarget[L"Words/InfoLeft/top"].v + (55) * PPTUIScale;

						PPTUIControlTarget[L"Words/InfoLeft/height"].v = (20) * PPTUIScale;
						SetAlpha(PPTUIControlColorTarget[L"Words/InfoLeft/words_color"].v, 255);
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v = PPTUIControlTarget[L"Words/InfoLeft/right"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/frame/width"].v = (1) * PPTUIScale;

						{
							PPTUIControlTarget[L"Image/RoundRectLeft2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/transparency"].v = 255;
						}
					}
				}
				// 中间控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/x"].v = PPTMainMonitor.MonitorWidth / 2 - (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/y"].v = PPTMainMonitor.MonitorHeight - (65) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/width"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/height"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/ellipseheight"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/ellipsewidth"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/frame/width"].v = (1) * PPTUIScale;

					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft/fill"].v, 160);
					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft/frame"].v, 160);

					{
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/frame/width"].v = (1) * PPTUIScale;

						{
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/transparency"].v = 255;
						}
					}
				}
				// 右侧控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v = PPTMainMonitor.MonitorWidth - (190) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v = PPTMainMonitor.MonitorHeight - (65) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/width"].v = (185) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/height"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipseheight"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipsewidth"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/frame/width"].v = (1) * PPTUIScale;

					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight/fill"].v, 160);
					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight/frame"].v, 160);

					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/frame/width"].v = (1) * PPTUIScale;

						{
							PPTUIControlTarget[L"Image/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/transparency"].v = 255;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoRight/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoRight/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoRight/right"].v = PPTUIControlTarget[L"Words/InfoRight/left"].v + (65) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoRight/bottom"].v = PPTUIControlTarget[L"Words/InfoRight/top"].v + (55) * PPTUIScale;

						PPTUIControlTarget[L"Words/InfoRight/height"].v = (20) * PPTUIScale;
						SetAlpha(PPTUIControlColorTarget[L"Words/InfoRight/words_color"].v, 255);
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v = PPTUIControlTarget[L"Words/InfoRight/right"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/frame/width"].v = (1) * PPTUIScale;

						{
							PPTUIControlTarget[L"Image/RoundRectRight2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/transparency"].v = 255;
						}
					}
				}

				// 文字控件
				{
					{
						wstring temp;
						if (CurrentSlides >= 100 || TotalSlides >= 100)
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
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v = (5) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v = PPTMainMonitor.MonitorHeight + (5) * PPTUIScale;

					if (PptAdvanceMode != 1) PPTUIControlTarget[L"RoundRect/RoundRectLeft/width"].v = (185) * PPTUIScale;
					else PPTUIControlTarget[L"RoundRect/RoundRectLeft/width"].v = (375) * PPTUIScale;

					PPTUIControlTarget[L"RoundRect/RoundRectLeft/height"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/ellipseheight"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/ellipsewidth"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectLeft/frame/width"].v = (1) * PPTUIScale;

					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft/fill"].v, 0);
					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft/frame"].v, 0);

					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft1/frame/width"].v = (1) * PPTUIScale;

						PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 0);
						PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/frame"].v = RGBA(200, 200, 200, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft1/transparency"].v = 0;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoLeft/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectLeft1/width"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoLeft/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoLeft/right"].v = PPTUIControlTarget[L"Words/InfoLeft/left"].v + (65) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoLeft/bottom"].v = PPTUIControlTarget[L"Words/InfoLeft/top"].v + (55) * PPTUIScale;

						PPTUIControlTarget[L"Words/InfoLeft/height"].v = (20) * PPTUIScale;
						SetAlpha(PPTUIControlColorTarget[L"Words/InfoLeft/words_color"].v, 0);
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v = PPTUIControlTarget[L"Words/InfoLeft/right"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft2/frame/width"].v = (1) * PPTUIScale;

						PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 0);
						PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/frame"].v = RGBA(200, 200, 200, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectLeft2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft2/transparency"].v = 0;
						}
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft2/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/width"].v = (10) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectLeft3/frame/width"].v = (1) * PPTUIScale;

						PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/fill"].v = RGBA(250, 250, 250, 0);
						PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/frame"].v = RGBA(200, 200, 200, 0);

						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/fill"].v, 0);
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft3/frame"].v, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectLeft3/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft3/x"].v + (10) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft3/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectLeft3/y"].v + (10) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft3/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectLeft3/height"].v = (40) * PPTUIScale;

							if (PptAdvanceMode == 1) PPTUIControlTarget[L"Image/RoundRectLeft3/transparency"].v = 255;
							else if (PptAdvanceMode == 1) PPTUIControlTarget[L"Image/RoundRectLeft3/transparency"].v = 0;
						}
					}
				}
				// 中间控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/x"].v = PPTMainMonitor.MonitorWidth / 2 - (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/y"].v = PPTMainMonitor.MonitorHeight + (5) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/width"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/height"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/ellipseheight"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/ellipsewidth"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/frame/width"].v = (1) * PPTUIScale;

					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft/fill"].v, 0);
					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft/frame"].v, 0);

					{
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/frame/width"].v = (1) * PPTUIScale;

						PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/fill"].v = RGBA(250, 250, 250, 0);
						PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/frame"].v = RGBA(200, 200, 200, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectMiddleLeft1/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectMiddleLeft1/transparency"].v = 0;
						}
					}
				}
				// 右侧控件
				{
					PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v = PPTMainMonitor.MonitorWidth - (190) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v = PPTMainMonitor.MonitorHeight + (5) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/width"].v = (185) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/height"].v = (60) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipseheight"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/ellipsewidth"].v = (30) * PPTUIScale;
					PPTUIControlTarget[L"RoundRect/RoundRectRight/frame/width"].v = (1) * PPTUIScale;

					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight/fill"].v, 0);
					SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight/frame"].v, 0);

					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/x"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight1/frame/width"].v = (1) * PPTUIScale;

						PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 0);
						PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/frame"].v = RGBA(200, 200, 200, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectRight1/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight1/transparency"].v = 0;
						}
					}
					{
						PPTUIControlTarget[L"Words/InfoRight/left"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight1/x"].v + PPTUIControlTarget[L"RoundRect/RoundRectRight1/width"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoRight/top"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoRight/right"].v = PPTUIControlTarget[L"Words/InfoRight/left"].v + (65) * PPTUIScale;
						PPTUIControlTarget[L"Words/InfoRight/bottom"].v = PPTUIControlTarget[L"Words/InfoRight/top"].v + (55) * PPTUIScale;

						PPTUIControlTarget[L"Words/InfoRight/height"].v = (20) * PPTUIScale;
						SetAlpha(PPTUIControlColorTarget[L"Words/InfoRight/words_color"].v, 0);
					}
					{
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v = PPTUIControlTarget[L"Words/InfoRight/right"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight/y"].v + (5) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/width"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/height"].v = (50) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipseheight"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/ellipsewidth"].v = (35) * PPTUIScale;
						PPTUIControlTarget[L"RoundRect/RoundRectRight2/frame/width"].v = (1) * PPTUIScale;

						PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 0);
						PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/frame"].v = RGBA(200, 200, 200, 0);

						{
							PPTUIControlTarget[L"Image/RoundRectRight2/x"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/x"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/y"].v = PPTUIControlTarget[L"RoundRect/RoundRectRight2/y"].v + (5) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/width"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/height"].v = (40) * PPTUIScale;
							PPTUIControlTarget[L"Image/RoundRectRight2/transparency"].v = 0;
						}
					}
				}

				// 文字控件
				{
					{
						PPTUIControlStringTarget[L"Info/Pages"] = L"Inkeys";
					}
				}

				// UI 单次修改计算反例（例如按钮）
				{
					if (TotalSlides == -1)
					{
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v, 0);
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/frame"].v, 0);

						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v, 0);
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/frame"].v, 0);

						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/fill"].v, 0);
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/frame"].v, 0);

						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v, 0);
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/frame"].v, 0);

						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v, 0);
						SetAlpha(PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/frame"].v, 0);
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

					PptWindowBackgroundUiChange = true;
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

					PptWindowBackgroundUiChange = true;
				}
			}
			for (const auto& [key, value] : PPTUIControlString)
			{
				if (PPTUIControlString[key] != PPTUIControlStringTarget[key])
				{
					PPTUIControlString[key] = PPTUIControlStringTarget[key];

					PptWindowBackgroundUiChange = true;
				}
			}
		}

		// 控件加载部分
		if (!Initialization && TotalSlides != -1)
		{
			ppt_show = GetPptShow();

			std::wstringstream ss(GetPptTitle());
			getline(ss, ppt_title);
			getline(ss, ppt_software);

			if (ppt_software.find(L"WPS") != ppt_software.npos) ppt_software = L"WPS";
			else ppt_software = L"PowerPoint";

			if (!ppt_title_recond[ppt_title]) FreezePPT = true;
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
		if (PptWindowBackgroundUiChange)
		{
			SetImageColor(PptWindowBackground, RGBA(0, 0, 0, 0), true);

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

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
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

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// Image/RoundRectLeft1
				{
					DCRenderTarget->DrawBitmap(PptIconBitmap[1], D2D1::RectF(PPTUIControl[L"Image/RoundRectLeft1/x"].v, PPTUIControl[L"Image/RoundRectLeft1/y"].v, PPTUIControl[L"Image/RoundRectLeft1/x"].v + PPTUIControl[L"Image/RoundRectLeft1/width"].v, PPTUIControl[L"Image/RoundRectLeft1/y"].v + PPTUIControl[L"Image/RoundRectLeft1/height"].v), PPTUIControl[L"Image/RoundRectLeft1/transparency"].v / 255.0f);
				}

				// Words/InfoLeft
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
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

					DxObjectSafeRelease(&pBrush);
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

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// Image/RoundRectLeft2
				{
					if (CurrentSlides == -1) DCRenderTarget->DrawBitmap(PptIconBitmap[3], D2D1::RectF(PPTUIControl[L"Image/RoundRectLeft2/x"].v, PPTUIControl[L"Image/RoundRectLeft2/y"].v, PPTUIControl[L"Image/RoundRectLeft2/x"].v + PPTUIControl[L"Image/RoundRectLeft2/width"].v, PPTUIControl[L"Image/RoundRectLeft2/y"].v + PPTUIControl[L"Image/RoundRectLeft2/height"].v), PPTUIControl[L"Image/RoundRectLeft2/transparency"].v / 255.0f);
					else DCRenderTarget->DrawBitmap(PptIconBitmap[2], D2D1::RectF(PPTUIControl[L"Image/RoundRectLeft2/x"].v, PPTUIControl[L"Image/RoundRectLeft2/y"].v, PPTUIControl[L"Image/RoundRectLeft2/x"].v + PPTUIControl[L"Image/RoundRectLeft2/width"].v, PPTUIControl[L"Image/RoundRectLeft2/y"].v + PPTUIControl[L"Image/RoundRectLeft2/height"].v), PPTUIControl[L"Image/RoundRectLeft2/transparency"].v / 255.0f);
				}
			}
			// 中间控件
			{
				// RoundRect/RoundRectMiddleLeft
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddleLeft/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddleLeft/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft/x"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft/y"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft/x"].v + PPTUIControl[L"RoundRect/RoundRectMiddleLeft/width"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft/y"].v + PPTUIControl[L"RoundRect/RoundRectMiddleLeft/height"].v),
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectMiddleLeft/frame/width"].v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}

				// RoundRect/RoundRectMiddleLeft1
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddleLeft1/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectMiddleLeft1/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v + PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/width"].v,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v + PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/height"].v),
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/frame/width"].v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// Image/RoundRectMiddleLeft1
				{
					DCRenderTarget->DrawBitmap(PptIconBitmap[3], D2D1::RectF(PPTUIControl[L"Image/RoundRectMiddleLeft1/x"].v, PPTUIControl[L"Image/RoundRectMiddleLeft1/y"].v, PPTUIControl[L"Image/RoundRectMiddleLeft1/x"].v + PPTUIControl[L"Image/RoundRectMiddleLeft1/width"].v, PPTUIControl[L"Image/RoundRectMiddleLeft1/y"].v + PPTUIControl[L"Image/RoundRectMiddleLeft1/height"].v), PPTUIControl[L"Image/RoundRectMiddleLeft1/transparency"].v / 255.0f);
				}
			}
			// 右侧控件
			{
				// RoundRect/RoundRectRight
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectRight/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectRight/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectRight/x"].v,
						PPTUIControl[L"RoundRect/RoundRectRight/y"].v,
						PPTUIControl[L"RoundRect/RoundRectRight/x"].v + PPTUIControl[L"RoundRect/RoundRectRight/width"].v,
						PPTUIControl[L"RoundRect/RoundRectRight/y"].v + PPTUIControl[L"RoundRect/RoundRectRight/height"].v),
						PPTUIControl[L"RoundRect/RoundRectRight/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectRight/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectRight/frame/width"].v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}

				// RoundRect/RoundRectRight1
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectRight1/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectRight1/x"].v,
						PPTUIControl[L"RoundRect/RoundRectRight1/y"].v,
						PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + PPTUIControl[L"RoundRect/RoundRectRight1/width"].v,
						PPTUIControl[L"RoundRect/RoundRectRight1/y"].v + PPTUIControl[L"RoundRect/RoundRectRight1/height"].v),
						PPTUIControl[L"RoundRect/RoundRectRight1/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectRight1/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectRight1/frame/width"].v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// Image/RoundRectRight1
				{
					DCRenderTarget->DrawBitmap(PptIconBitmap[1], D2D1::RectF(PPTUIControl[L"Image/RoundRectRight1/x"].v, PPTUIControl[L"Image/RoundRectRight1/y"].v, PPTUIControl[L"Image/RoundRectRight1/x"].v + PPTUIControl[L"Image/RoundRectRight1/width"].v, PPTUIControl[L"Image/RoundRectRight1/y"].v + PPTUIControl[L"Image/RoundRectRight1/height"].v), PPTUIControl[L"Image/RoundRectRight1/transparency"].v / 255.0f);
				}

				// Words/InfoRight
				{
					IDWriteTextFormat* textFormat = NULL;
					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						PPTUIControl[L"Words/InfoRight/height"].v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(PPTUIControlColor[L"Words/InfoRight/words_color"].v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						PPTUIControlString[L"Info/Pages"].c_str(),  // 文本
						wcslen(PPTUIControlString[L"Info/Pages"].c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							PPTUIControl[L"Words/InfoRight/left"].v,
							PPTUIControl[L"Words/InfoRight/top"].v,
							PPTUIControl[L"Words/InfoRight/right"].v,
							PPTUIControl[L"Words/InfoRight/bottom"].v
						),
						pBrush
					);

					DxObjectSafeRelease(&pBrush);
				}

				// RoundRect/RoundRectRight2
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectRight2/frame"].v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						PPTUIControl[L"RoundRect/RoundRectRight2/x"].v,
						PPTUIControl[L"RoundRect/RoundRectRight2/y"].v,
						PPTUIControl[L"RoundRect/RoundRectRight2/x"].v + PPTUIControl[L"RoundRect/RoundRectRight2/width"].v,
						PPTUIControl[L"RoundRect/RoundRectRight2/y"].v + PPTUIControl[L"RoundRect/RoundRectRight2/height"].v),
						PPTUIControl[L"RoundRect/RoundRectRight2/ellipsewidth"].v / 2.0f,
						PPTUIControl[L"RoundRect/RoundRectRight2/ellipseheight"].v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, PPTUIControl[L"RoundRect/RoundRectRight2/frame/width"].v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// Image/RoundRectRight2
				{
					if (CurrentSlides == -1) DCRenderTarget->DrawBitmap(PptIconBitmap[3], D2D1::RectF(PPTUIControl[L"Image/RoundRectRight2/x"].v, PPTUIControl[L"Image/RoundRectRight2/y"].v, PPTUIControl[L"Image/RoundRectRight2/x"].v + PPTUIControl[L"Image/RoundRectRight2/width"].v, PPTUIControl[L"Image/RoundRectRight2/y"].v + PPTUIControl[L"Image/RoundRectRight2/height"].v), PPTUIControl[L"Image/RoundRectRight2/transparency"].v / 255.0f);
					else DCRenderTarget->DrawBitmap(PptIconBitmap[2], D2D1::RectF(PPTUIControl[L"Image/RoundRectRight2/x"].v, PPTUIControl[L"Image/RoundRectRight2/y"].v, PPTUIControl[L"Image/RoundRectRight2/x"].v + PPTUIControl[L"Image/RoundRectRight2/width"].v, PPTUIControl[L"Image/RoundRectRight2/y"].v + PPTUIControl[L"Image/RoundRectRight2/height"].v), PPTUIControl[L"Image/RoundRectRight2/transparency"].v / 255.0f);
				}
			}

			DCRenderTarget->EndDraw();

			{
				ulwi.hdcSrc = GetImageHDC(&PptWindowBackground);
				UpdateLayeredWindowIndirect(ppt_window, &ulwi);

				if (!IsShowWindow)
				{
					IdtWindowsIsVisible.pptWindow = true;
					//ShowWindow(ppt_window, SW_SHOW);

					IsShowWindow = true;
				}
				// 动态平衡帧率
				if (tRecord)
				{
					int delay = 1000 / 24 - (clock() - tRecord);
					if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				}
				tRecord = clock();

				PptWindowBackgroundUiChange = false;
			}
		}
		else this_thread::sleep_for(chrono::milliseconds(100));
	}

	for (int r = 0; r < (int)size(PptIconBitmap); r++) DxObjectSafeRelease(&PptIconBitmap[r]);
	DxObjectSafeRelease(&DCRenderTarget);

	threadStatus[L"DrawControlWindow"] = false;
}
void ControlManipulation()
{
	ExMessage m;
	int last_x = -1, last_y = -1;

	while (!offSignal)
	{
		if (PptInfoStateBuffer.TotalPage != -1)
		{
			hiex::getmessage_win32(&m, EM_MOUSE, ppt_window);
			if (PptInfoStateBuffer.TotalPage == -1)
			{
				hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				continue;
			}

			// 左侧 上一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft1/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 160);
				PptWindowBackgroundUiChange = true;

				if (m.message == WM_LBUTTONDOWN)
				{
					SetForegroundWindow(ppt_show);

					PreviousPptSlides();
					PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(200, 200, 200, 255);

					std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
					while (1)
					{
						if (!KeyBoradDown[VK_LBUTTON]) break;
						if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
						{
							PreviousPptSlides();
							PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(200, 200, 200, 255);
						}

						this_thread::sleep_for(chrono::milliseconds(15));
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else if (PptInfoStateBuffer.TotalPage != -1) PPTUIControlColorTarget[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(250, 250, 250, 160);
			// 左侧 下一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft2/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft2/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectLeft2/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectLeft2/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 160);
				PptWindowBackgroundUiChange = true;

				if (m.message == WM_LBUTTONDOWN)
				{
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
					{
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							ChangeStateModeToSelection();
						}
					}
					else if (temp_currentpage == -1)
					{
						EndPptShow();
					}
					else
					{
						SetForegroundWindow(ppt_show);

						NextPptSlides(temp_currentpage);
						PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!KeyBoradDown[VK_LBUTTON]) break;

							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								temp_currentpage = PptInfoState.CurrentPage;
								if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
									{
										EndPptShow();

										ChangeStateModeToSelection();
									}
									break;
								}
								else if (temp_currentpage != -1)
								{
									NextPptSlides(temp_currentpage);
									PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(200, 200, 200, 255);
								}
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else if (PptInfoStateBuffer.TotalPage != -1) PPTUIControlColorTarget[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(250, 250, 250, 160);

			// 中间 结束放映
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectMiddleLeft1/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/fill"].v = RGBA(250, 250, 250, 160);
				PptWindowBackgroundUiChange = true;

				if (m.message == WM_LBUTTONDOWN)
				{
					int lx = m.x, ly = m.y;
					while (1)
					{
						ExMessage m = hiex::getmessage_win32(EM_MOUSE, ppt_window);
						if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectMiddleLeft1/height"].v }))
						{
							if (!m.lbutton)
							{
								PPTUIControlColor[L"RoundRect/RoundRectMiddleLeft1/fill"].v = RGBA(200, 200, 200, 255);

								if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) != 1) break;

									ChangeStateModeToSelection();
								}

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
			else if (PptInfoStateBuffer.TotalPage != -1) PPTUIControlColorTarget[L"RoundRect/RoundRectMiddleLeft1/fill"].v = RGBA(250, 250, 250, 160);

			// 右侧 上一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight1/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight1/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight1/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight1/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight1/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
				PptWindowBackgroundUiChange = true;

				if (m.message == WM_LBUTTONDOWN)
				{
					SetForegroundWindow(ppt_show);

					PreviousPptSlides();
					PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);

					std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
					while (1)
					{
						if (!KeyBoradDown[VK_LBUTTON]) break;
						if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
						{
							PreviousPptSlides();
							PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);
						}

						this_thread::sleep_for(chrono::milliseconds(15));
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else if (PptInfoStateBuffer.TotalPage != -1) PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
			// 右侧 下一页
			if (IsInRect(m.x, m.y, { (int)PPTUIControl[L"RoundRect/RoundRectRight2/x"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight2/y"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight2/x"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight2/width"].v,(int)PPTUIControl[L"RoundRect/RoundRectRight2/y"].v + (int)PPTUIControl[L"RoundRect/RoundRectRight2/height"].v }))
			{
				if (last_x != m.x || last_y != m.y) PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(225, 225, 225, 255);
				else PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);
				PptWindowBackgroundUiChange = true;

				if (m.message == WM_LBUTTONDOWN)
				{
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
					{
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							EndPptShow();

							ChangeStateModeToSelection();
						}
					}
					else if (temp_currentpage == -1)
					{
						EndPptShow();
					}
					else
					{
						SetForegroundWindow(ppt_show);

						NextPptSlides(temp_currentpage);
						PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!KeyBoradDown[VK_LBUTTON]) break;

							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								temp_currentpage = PptInfoState.CurrentPage;
								if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
								{
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
									{
										EndPptShow();

										ChangeStateModeToSelection();
									}
									break;
								}
								else if (temp_currentpage != -1)
								{
									NextPptSlides(temp_currentpage);
									PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);
								}
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);

					POINT pt;
					GetCursorPos(&pt);
					last_x = pt.x, last_y = pt.y;
				}
			}
			else if (PptInfoStateBuffer.TotalPage != -1) PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);

			// 滚轮消息
			if (m.message == WM_MOUSEWHEEL)
			{
				// 下一页
				if (m.wheel <= -120)
				{
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
					{
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							EndPptShow();

							ChangeStateModeToSelection();
						}
					}
					else if (temp_currentpage == -1)
					{
						EndPptShow();
					}
					else
					{
						SetForegroundWindow(ppt_show);

						NextPptSlides(temp_currentpage);
						PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);
					}

					PPTUIControlColorTarget[L"RoundRect/RoundRectRight2/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				}
				// 上一页
				else
				{
					SetForegroundWindow(ppt_show);

					PreviousPptSlides();
					PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);

					PPTUIControlColorTarget[L"RoundRect/RoundRectRight1/fill"].v = RGBA(250, 250, 250, 160);
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				}
			}
		}
		else
		{
			last_x = -1, last_y = -1;

			hiex::flushmessage_win32(EM_MOUSE, ppt_window);
			this_thread::sleep_for(chrono::milliseconds(500));
		}
	}
}
void PPTLinkageMain()
{
	threadStatus[L"DrawControlWindow"] = true;

	thread GetPptState_thread(GetPptState);
	GetPptState_thread.detach();

	thread DrawControlWindowThread(DrawControlWindow);
	DrawControlWindowThread.detach();
	thread ControlManipulationThread(ControlManipulation);
	ControlManipulationThread.detach();

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	int i = 1;
	for (; i <= 5; i++)
	{
		if (!threadStatus[L"GetPptState"] && !threadStatus[L"DrawControlWindow"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}

	threadStatus[L"DrawControlWindow"] = false;
}

// --------------------------------------------------
// 插件

// DesktopDrawpadBlocker 插件
void StartDesktopDrawpadBlocker()
{
	if (ddbSetList.DdbEnable)
	{
		// 配置 json
		{
			if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\interaction_configuration.json").c_str(), 0) == 0) DdbReadSetting();

			ddbSetList.hostPath = GetCurrentExePath();
			if (ddbSetList.DdbEnhance)
			{
				ddbSetList.mode = 0;
				ddbSetList.restartHost = true;
			}
			else
			{
				ddbSetList.mode = 1;
				ddbSetList.restartHost = true;
			}
		}

		// 配置 EXE
		if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
		{
			if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directories(StringToWstring(globalPath) + L"PlugIn\\DDB", ec);
			}
			ExtractResource((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
		}
		else
		{
			string hash_sha256;
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe");
				delete myWrapper;
			}

			if (hash_sha256 != ddbSetList.DdbSHA256)
			{
				if (isProcessRunning((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str()))
				{
					// 需要关闭旧版 DDB 并更新版本

					DdbWriteSetting(true, true);
					for (int i = 1; i <= 20; i++)
					{
						if (!isProcessRunning((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str()))
							break;
						this_thread::sleep_for(chrono::milliseconds(500));
					}
				}
				ExtractResource((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
			}
		}

		// 创建开机自启标识
		if (ddbSetList.DdbEnhance && _waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == -1)
		{
			std::ofstream file((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str());
			file.close();
		}
		// 移除开机自启标识
		else if (!ddbSetList.DdbEnhance && _waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == 0)
		{
			error_code ec;
			filesystem::remove(StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal", ec);
		}

		// 启动 DDB
		if (!isProcessRunning((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str()))
		{
			DdbWriteSetting(true, false);
			ShellExecute(NULL, NULL, (StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	}
	else if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB").c_str(), 0) == 0)
	{
		error_code ec;
		filesystem::remove_all(StringToWstring(globalPath) + L"PlugIn\\DDB", ec);
	}
}