/**
 * @file	HiCanvas.h
 * @brief	HiEasyX 库的画布模块
 * @author	huidong
*/

#pragma once

#include "HiDrawingProperty.h"
#include <vector>
#include <list>
#include <string>
#include <gdiplus.h>

namespace HiEasyX
{
	/**
	 * @brief 根据透明度混合颜色
	 * @param[in] cDst		原位置像素
	 * @param[in] cSrc		待绘制像素（根据其透明度混合颜色）
	 *
	 * @param[in] isCalculated <pre>
	 *		待绘制像素点是否已经乘以它的透明度
	 *
	 *	备注：
	 *		此参数用于一些特殊情况，例如透明 png 图像中的像素就是已经乘过透明度的。
	 * </pre>
	 *
	 * @param[in] alpha		叠加在 src 上的透明度（默认为 255，即不叠加）
	 * @return 混合后的颜色（不含 alpha 值）
	*/
	COLORREF MixAlphaColor(COLORREF cDst, COLORREF cSrc, bool isCalculated, BYTE alpha = 255);

	/**
	 * @brief <pre>
	 *		快速复制图像（可开启透明通道）
	 *
	 *	备注：
	 *		若未启用任何透明通道，等同于直接复制图像。此时将保留原图像的透明度信息，否则不保留透明度信息。
	 * </pre>
	 *
	 * @param[in] x					图像输出 x 坐标
	 * @param[in] y					图像输出 y 坐标
	 * @param[in] pDst				载体图像指针
	 * @param[in] wDst				载体图像宽
	 * @param[in] hDst				载体图像高
	 * @param[in] pSrc				待输出图像指针
	 * @param[in] wSrc				待输出图像宽
	 * @param[in] hSrc				待输出图像高
	 * @param[in] crop				待输出图像裁剪区域（right 或 bottom 为 0 表示不裁剪）
	 * @param[in] alpha				叠加透明度（透明 0 ~ 255 不透明）
	 *
	 * @param[in] bUseSrcAlpha <pre>
	 *		是否使用待输出图像透明度进行混合（须保证 IMAGE 中含有透明度信息）
	 *
	 *	备注：
	 *		EasyX 中的图像一般无透明度（默认设为 0，即全透明），故一般不使用原图透明度。
	 *		通常只有 png 图像，或是特地生成的图像才含有透明度信息。
	 * </pre>
	 *
	 * @param[in] isCalculated <pre>
	 *		标记待输出图像是否已经计算好混合后的颜色（启用图像透明度时有效）
	 *
	 *	注意：
	 *		png 图像像素颜色都已进行过混合运算。
	 *		开启后，原图像便不再计算混合颜色，只有载体图像参与计算。
	 * </pre>
	*/
	void CopyImage_Alpha(
		int x,
		int y,
		DWORD* pDst, int wDst, int hDst,
		DWORD* pSrc, int wSrc, int hSrc,
		RECT crop = { 0 },
		BYTE alpha = 255,
		bool bUseSrcAlpha = false,
		bool isCalculated = false
	);

	/**
	 * @brief 旋转图像（保留透明信息，自适应大小）
	 * @param[in] pImg			原图像
	 * @param[in] radian		旋转弧度
	 * @param[in] bkcolor		背景填充颜色
	 * @return	旋转后的图像
	*/
	IMAGE RotateImage_Alpha(IMAGE* pImg, double radian, COLORREF bkcolor = BLACK);

	/**
	 * @brief	缩放图像（粗糙的、即不插值的缩放，保留透明度信息）
	 * @param[in] srcimg		原图像
	 * @param[in] width			目标宽度
	 * @param[in] height		目标高度（为 0 则根据宽度按比例缩放）
	 * @return 缩放后的图像
	*/
	IMAGE ZoomImage_Rough_Alpha(IMAGE* srcimg, int width, int height = 0);

	/**
	 * @brief	缩放图像（双线性插值，保留透明度信息）
	 * @param[in] srcimg		原图像
	 * @param[in] width			目标宽度
	 * @param[in] height		目标高度（为 0 则根据宽度按比例缩放）
	 * @return 缩放后的图像
	*/
	IMAGE ZoomImage_Alpha(IMAGE* srcimg, int width, int height = 0);

	/**
	 * @brief	图像缩放（基于 Win32 API，比较快，保留透明度信息）
	 * @param[in] srcimg		原图像
	 * @param[in] width			目标宽度
	 * @param[in] height		目标高度（为 0 则根据宽度按比例缩放）
	 * @return 缩放后的图像
	*/
	IMAGE ZoomImage_Win32_Alpha(IMAGE* srcimg, int width, int height = 0);

	/**
	 * @brief 画布
	*/
	class Canvas : public IMAGE
	{
	public:

		// 允许此函数调用 BindToWindow 函数
		friend void BindWindowCanvas(Canvas*, HWND);

	protected:

		/////// 变量 ///////

		DrawingProperty m_property;			///< 保存外界绘图属性（用于保存旧的绘图对象指针）

		DWORD* m_pBuf = nullptr;			///< 图像内存指针
		int m_nWidth, m_nHeight;			///< 图像宽高
		int m_nBufSize;						///< 图像面积

		bool m_bBindToImgPointer;			///< 该画布是否绑定到图像指针
		IMAGE* m_pImg;						///< 画布绑定的图像指针（若画布绑定到指针）

		bool m_bBatchDraw;					///< 是否启用了批量绘制

		COLORREF m_cGPLineColor = WHITE;	///< GDI+ 绘图时使用的线条颜色
		COLORREF m_cGPFillColor = WHITE;	///< GDI+ 绘图时使用的填充颜色
		float m_fGPLineWidth = 1.f;			///< GDI+ 绘图时的线条宽度
		bool m_bGPAlpha = false;			///< GDI+ 绘图时是否启用透明度
		Gdiplus::SmoothingMode m_enuSmoothingMode = Gdiplus::SmoothingModeAntiAlias;	///< GDI+ 抗锯齿模式

		HWND m_hBindWindow;					///< 绑定到的窗口
		bool m_bAutoMarkFlushWindow = true;	///< 绑定到窗口时，标记是否在绘制后自动设置需要更新双缓冲

		/////// 函数 ///////

		/**
		 * @brief 清空大部分设置
		*/
		void CleanUpSettings();

		/**
		 * @brief 单独启动 HiWindow 的窗口任务（如果绑定了窗口）
		 * @return 是否启动成功
		*/
		bool BeginWindowTask();

		/**
		 * @brief 结束 HiWindow 的窗口任务
		*/
		void EndWindowTask();

		/**
		 * @brief <pre>
		 *		调用 EasyX 绘图函数前，设置该画布为目标绘图画布
		 *
		 *	备注：
		 *		若绑定了窗口，则自动启动窗口任务
		 * </pre>
		 *
		 * @return 是否设置成功
		*/
		bool BeginDrawing();

		/**
		 * @brief 调用 EasyX 绘图函数完毕，恢复先前的绘图状态
		*/
		void EndDrawing();

		/**
		 * @brief <pre>
		 *		将画布绑定到窗口（画布大小随窗口自动调整）
		 *
		 *	备注：
		 *		此函数只应该被 BindWindowCanvas 函数调用
		 * </pre>
		 *
		 * @param[in] hWnd 目标窗口
		 * @param[in] pImg 窗口图像缓冲区
		 * @return 此画布
		*/
		Canvas& BindToWindow(HWND hWnd, IMAGE* pImg);

	public:

		/////// 画布操作函数 ///////

		Canvas();

		Canvas(int w, int h, COLORREF cBk = BLACK);

		/**
		 * @brief 复制图像内容（绑定图像指针请调用 BindToImage）
		 * @param[in] pImg 原图像
		*/
		Canvas(IMAGE* pImg);
		Canvas(IMAGE img);

		/**
		 * @brief 复制图像内容（绑定图像指针请调用 BindToImage）
		 * @param[in] pImg 原图像
		*/
		Canvas& operator= (IMAGE* pImg);
		Canvas& operator= (IMAGE img);

		/**
		 * @brief <pre>
		 *		重新加载图像尺寸信息
		 *
		 * 备注：
		 *		若绑定了图像指针，当外部调整图像大小后，须调用此函数
		 * </pre>
		*/
		void UpdateSizeInfo();

		/**
		 * @brief 重设画布大小（若绑定了窗口，则不建议调用）
		 * @param[in] w 目标宽度
		 * @param[in] h 目标高度
		*/
		void Resize(int w, int h) override;

		/**
		 * @brief <pre>
		 *		绑定到图像指针
		 *
		 *	注意：
		 *		绑定到图像指针后，如果在外部调整了图像大小，则需要调用 UpdateSizeInfo 重新加载图像信息
		 * </pre>
		 *
		 * @param[in] pImg 目标图像指针
		 * @return 此画布
		*/
		Canvas& BindToImage(IMAGE* pImg);

		/**
		 * @brief <pre>
		 *		获取画布 IMAGE 指针
		 *
		 *	注意：
		 *		有的时候画布绑定了别的 IMAGE，所以绘图时不能直接使用 this，必须调用此函数。
		 * </pre>
		*/
		IMAGE* GetImagePointer() { return m_bBindToImgPointer ? m_pImg : this; }

		/**
		 * @brief 等价于 GetImagePointer()
		*/
		IMAGE* Pt() { return m_bBindToImgPointer ? m_pImg : this; }

		/**
		 * @brief 获取图像缓冲区指针
		*/
		DWORD* GetBuffer() const { return m_pBuf; }

		/**
		 * @brief 获取图像缓冲区大小，即图像面积（宽 * 高）
		*/
		int GetBufferSize() const { return m_nBufSize; }

		/**
		 * @brief 获取画布的 HDC
		*/
		HDC GetHDC() { return GetImageHDC(GetImagePointer()); }

		int getwidth() const { return m_nWidth; }
		int getheight() const { return m_nHeight; }
		int GetWidth() const { return m_nWidth; }
		int GetHeight() const { return m_nHeight; }

		/**
		 * @brief <pre>
		 *		绑定到窗口时，设置是否在每次绘制后都自动标记刷新窗口双缓冲
		 *
		 *	备注：
		 *		标记刷新窗口双缓冲并不意味着即时刷新。
		 *		标记后，窗口将会在下一次接受到绘制消息时更新双缓冲。
		 * </pre>
		*/
		void EnableAutoMarkFlushWindow(bool enable);
		bool IsEnableAutoMarkFlushWindow() const { return m_bAutoMarkFlushWindow; }

		/////// 绘图状态设置函数 ///////

		/**
		 * @brief <pre>
		 *		开始大批量绘制（该函数并非用于开启双缓冲）
		 *
		 *	备注：
		 *		调用该函数后，当前绘图目标将转移到该画布，此后每次绘制不会恢复绘图目标
		 * </pre>
		*/
		void BeginBatchDrawing();

		/**
		 * @brief <pre>
		 *		结束批量绘制
		 *
		 *	备注：
		 *		绘图目标将恢复到批量绘制前的状态
		 * </pre>
		*/
		void EndBatchDrawing();

		/////// EasyX 风格的基础绘图函数 ///////

		/**
		 * @brief 判断某点是否位于图像中
		 * @param[in] x 坐标
		 * @param[in] y 坐标
		 * @param[out] pIndex 返回该点数组索引
		 * @return 是否位于图像中
		*/
		bool IsValidPoint(int x, int y, int* pIndex = nullptr);

		/**
		 * @brief <pre>
		 *		用背景色清空画布
		 *
		 *	备注：
		 *		此函数将忽略背景色的透明度，并直接对画布填入 255 的透明度（即不透明）。
		 * </pre>
		*/
		void Clear(bool isSetColor = false, COLORREF bkcolor = BLACK);

		/**
		 * @brief 用背景色清空画布（区别于 Clear 函数，此函数默认保留背景色中的透明度）
		*/
		void Clear_Alpha(bool isSetColor = false, COLORREF bkcolor = BLACK, bool ignore_alpha = false);

		LINESTYLE GetLineStyle();
		void SetLineStyle(LINESTYLE style);
		void SetLineStyle(int style, int thickness = 1, const DWORD* puserstyle = nullptr, DWORD userstylecount = 0);
		void SetLineThickness(int thickness);
		int GetLineThickness();

		FILLSTYLE GetFillStyle();
		void SetFillStyle(FILLSTYLE style);
		void SetFillStyle(int style, long hatch = 0, IMAGE* ppattern = nullptr);
		void SetFillStyle(BYTE* ppattern8x8);

		int GetRop2();
		void SetRop2(int mode);

		int GetPolyFillMode();
		void SetPolyFillMode(int mode);

		COLORREF GetLineColor();
		void SetLineColor(COLORREF color);

		COLORREF GetTextColor();
		void SetTextColor(COLORREF color);

		COLORREF GetFillColor();
		void SetFillColor(COLORREF color);

		COLORREF GetBkColor();
		void SetBkColor(COLORREF color);

		int GetBkMode();
		void SetBkMode(int mode);

		/**
		 * @brief 设置绘图状态为原始状态
		*/
		void SetDefault();

		COLORREF GetPixel(int x, int y);
		void PutPixel(int x, int y, COLORREF c);

		/**
		 * @brief  直接操作显存获取点
		*/
		COLORREF GetPixel_Direct(int x, int y);

		/**
		 * @brief 直接操作显存绘制点
		*/
		void PutPixel_Direct(int x, int y, COLORREF c);

		/**
		 * @brief 直接操作显存绘制带有透明度的点（使用 COLORREF 中的透明度）
		*/
		void PutPixel_Direct_Alpha(int x, int y, COLORREF c);

		void Line(int x1, int y1, int x2, int y2, bool isSetColor = false, COLORREF c = 0);
		void Line(POINT pt1, POINT pt2, bool isSetColor = false, COLORREF c = 0);

		void Rectangle(int left, int top, int right, int bottom, bool isSetColor = false, COLORREF c = 0);
		void Rectangle(RECT rct, bool isSetColor = false, COLORREF c = 0);
		void FillRectangle(int left, int top, int right, int bottom, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void FillRectangle(RECT rct, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void SolidRectangle(int left, int top, int right, int bottom, bool isSetColor = false, COLORREF c = 0);
		void SolidRectangle(RECT rct, bool isSetColor = false, COLORREF c = 0);
		void ClearRectangle(int left, int top, int right, int bottom);
		void ClearRectangle(RECT rct);

		void Circle(int x, int y, int radius, bool isSetColor = false, COLORREF c = 0);
		void FillCircle(int x, int y, int radius, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void SolidCircle(int x, int y, int radius, bool isSetColor = false, COLORREF c = 0);
		void ClearCircle(int x, int y, int radius);

		void Ellipse(int left, int top, int right, int bottom, bool isSetColor = false, COLORREF c = 0);
		void Ellipse(RECT rct, bool isSetColor = false, COLORREF c = 0);
		void FillEllipse(int left, int top, int right, int bottom, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void FillEllipse(RECT rct, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void SolidEllipse(int left, int top, int right, int bottom, bool isSetColor = false, COLORREF c = 0);
		void SolidEllipse(RECT rct, bool isSetColor = false, COLORREF c = 0);
		void ClearEllipse(int left, int top, int right, int bottom);
		void ClearEllipse(RECT rct);

		void RoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight, bool isSetColor = false, COLORREF c = 0);
		void RoundRect(RECT rct, int ellipsewidth, int ellipseheight, bool isSetColor = false, COLORREF c = 0);
		void FillRoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void FillRoundRect(RECT rct, int ellipsewidth, int ellipseheight, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void SolidRoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight, bool isSetColor = false, COLORREF c = 0);
		void SolidRoundRect(RECT rct, int ellipsewidth, int ellipseheight, bool isSetColor = false, COLORREF c = 0);
		void ClearRoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight);
		void ClearRoundRect(RECT rct, int ellipsewidth, int ellipseheight);

		void Arc(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor = false, COLORREF c = 0);
		void Arc(RECT rct, double stangle, double endangle, bool isSetColor = false, COLORREF c = 0);
		void Pie(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor = false, COLORREF c = 0);
		void Pie(RECT rct, double stangle, double endangle, bool isSetColor = false, COLORREF c = 0);
		void FillPie(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void FillPie(RECT rct, double stangle, double endangle, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void SolidPie(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor = false, COLORREF c = 0);
		void SolidPie(RECT rct, double stangle, double endangle, bool isSetColor = false, COLORREF c = 0);
		void ClearPie(int left, int top, int right, int bottom, double stangle, double endangle);
		void ClearPie(RECT rct, double stangle, double endangle);

		void Polyline(const POINT* points, int num, bool isSetColor = false, COLORREF c = 0);
		void Polygon(const POINT* points, int num, bool isSetColor = false, COLORREF c = 0);
		void FillPolygon(const POINT* points, int num, bool isSetColor = false, COLORREF cLine = 0, COLORREF cFill = 0);
		void SolidPolygon(const POINT* points, int num, bool isSetColor = false, COLORREF c = 0);
		void ClearPolygon(const POINT* points, int num);

		void PolyBezier(const POINT* points, int num, bool isSetColor = false, COLORREF c = 0);

		/**
		 * @brief 填充某区域
		 * @param[in] x				填充起始位置
		 * @param[in] y				填充起始位置
		 * @param[in] color			填充颜色
		 *
		 * @param[in] filltype <pre>
		 *		填充模式，有以下两种选择：
		 *		FLOODFILLBORDER		指定 color 为填充边界颜色，即遇到此颜色后停止填充
		 *		FLOODFILLSURFACE	指定 color 为填充表面颜色，即只填充此颜色
		 * </pre>
		 *
		 * @param[in] isSetColor		是否设置填充颜色
		 * @param[in] cFill			填充颜色
		*/
		void FloodFill(int x, int y, COLORREF color, int filltype = FLOODFILLBORDER, bool isSetColor = false, COLORREF cFill = 0);

		/**
		 * @brief 在指定位置输出文本
		 * @param[in] x				位置
		 * @param[in] y				位置
		 * @param[in] lpszText			文本
		 * @param[in] isSetColor		是否设置颜色
		 * @param[in] c				文本颜色
		 * @return 文本像素宽度
		*/
		int OutTextXY(int x, int y, LPCTSTR lpszText, bool isSetColor = false, COLORREF c = 0);

		int OutTextXY(int x, int y, TCHAR ch, bool isSetColor = false, COLORREF c = 0);

		/**
		 * @brief 在指定位置输出格式化文本
		 * @param[in] x			位置
		 * @param[in] y			位置
		 * @param[in] _Size		格式化文本最大长度
		 * @param[in] _Format		格式化字符串
		 * @param[in]				不定参数
		 * @return 文本像素宽度
		*/
		int OutTextXY_Format(int x, int y, int _Size, LPCTSTR _Format, ...);

		/**
		 * @brief 获取文本像素宽度
		 * @param[in] lpszText 文本
		 * @return 获取文本像素宽度
		*/
		int TextWidth(LPCTSTR lpszText);

		int TextWidth(TCHAR c);
		int TextHeight(LPCTSTR lpszText);
		int TextHeight(TCHAR c);
		int Draw_Text(LPCTSTR str, RECT* pRect, UINT uFormat, bool isSetColor = false, COLORREF c = 0);
		int Draw_Text(TCHAR ch, RECT* pRect, UINT uFormat, bool isSetColor = false, COLORREF c = 0);

		/**
		 * @brief 在某区域居中输出文字
		 * @param[in] lpszText			文本
		 * @param[in] rct				输出区域，默认为画布区域
		 * @param[in] isSetColor		是否设置颜色
		 * @param[in] c					文本颜色
		*/
		void CenterText(LPCTSTR lpszText, RECT rct = { -1 }, bool isSetColor = false, COLORREF c = 0);

		/**
		 * @brief 居中输出格式化文本
		 * @param[in] _Size			格式化文本最大长度
		 * @param[in] _Format		格式化字符串
		 * @param[in]				不定参数
		*/
		void CenterText_Format(int _Size, LPCTSTR _Format, ...);

		LOGFONT GetTextStyle();
		void SetTextStyle(int nHeight, int nWidth, LPCTSTR lpszFace);
		void SetTextStyle(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut);
		void SetTextStyle(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut, BYTE fbCharSet, BYTE fbOutPrecision, BYTE fbClipPrecision, BYTE fbQuality, BYTE fbPitchAndFamily);
		void SetTextStyle(LOGFONT font);

		/**
		 * @brief 设置字体大小
		 * @param[in] nHeight	高度
		 * @param[in] nWidth	宽度（为 0 时，自动与高度匹配）
		*/
		void SetFont(int nHeight, int nWidth = 0);

		/**
		 * @brief 设置使用字体的名称
		 * @param[in] lpsz 字体名称
		*/
		void SetTypeface(LPCTSTR lpsz);

		/**
		 * @brief 设置字符串的书写角度（单位 0.1 度）
		 * @param[in] lfEscapement 角度
		*/
		void SetTextEscapement(LONG lfEscapement);

		/**
		 * @brief 设置每个字符的书写角度（单位 0.1 度）
		 * @param[in] lfOrientation 角度
		*/
		void SetTextOrientation(LONG lfOrientation);

		/**
		 * @brief 设置字符的笔画粗细（范围 默认 0 ~ 1000 最粗）
		 * @param[in] lfWeight 粗细
		*/
		void SetTextWeight(LONG lfWeight);

		/**
		 * @brief 设置字体是否为斜体
		 * @param[in] lfItalic 是否使用斜体
		*/
		void SetTextItalic(bool lfItalic);

		/**
		 * @brief 设置字体是否有下划线
		 * @param[in] lfUnderline 是否使用下划线
		*/
		void SetTextUnderline(bool lfUnderline);

		/**
		 * @brief 设置字体是否有删除线
		 * @param[in] lfStrikeOut 是否使用删除线
		*/
		void SetTextStrikeOut(bool lfStrikeOut);

		/**
		 * @brief 获取前景色
		*/
		COLORREF GetColor();

		/**
		 * @brief 设置前景色
		 * @param[in] color 前景色
		*/
		void SetColor(COLORREF color);

		int GetX();
		int GetY();

		void MoveTo(int x, int y);
		void MoveRel(int dx, int dy);

		void LineTo(int x, int y, bool isSetColor = false, COLORREF c = 0);
		void LineRel(int dx, int dy, bool isSetColor = false, COLORREF c = 0);

		void OutText(LPCTSTR lpszText, bool isSetColor = false, COLORREF c = 0);
		void OutText(TCHAR ch, bool isSetColor = false, COLORREF c = 0);

		/**
		 * @brief 输出格式化文本
		 * @param[in] _Size			格式化文本最大长度
		 * @param[in] _Format		格式化字符串
		 * @param[in]				不定参数
		 * @return 文本像素宽度
		*/
		int OutText_Format(int _Size, LPCTSTR _Format, ...);

		/**
		 * @brief <pre>
		 *		加载图片文件到画布
		 *
		 *	备注：
		 *		若开启透明通道，则复制到画布上的内容不会保留原图像的透明度信息
		 * </pre>
		 *
		 * @param[in] lpszImgFile		图像文件路径
		 * @param[in] x					输出到画布的位置
		 * @param[in] y					输出到画布的位置
		 * @param[in] bResize			是否调整画布大小以正好容纳图像（对于无宽高的画布会自动调整大小）
		 * @param[in] nWidth			图像目标拉伸尺寸，为 0 表示不拉伸
		 * @param[in] nHeight			图像目标拉伸尺寸，为 0 表示不拉伸
		 * @param[in] alpha				叠加透明度
		 * @param[in] bUseSrcAlpha		是否使用原图的透明度信息进行混合（仅支持有透明度信息的 png 图像）
		 * @param[in] isCalculated		原图是否已经混合透明度
		 * @return 读取到的 IMAGE 对象
		*/
		IMAGE Load_Image_Alpha(
			LPCTSTR lpszImgFile,
			int x = 0,
			int y = 0,
			bool bResize = false,
			int nWidth = 0,
			int nHeight = 0,
			BYTE alpha = 255,
			bool bUseSrcAlpha = false,
			bool isCalculated = false
		);

		/**
		 * @brief 绘制图像到该画布
		 * @param[in] x					图像输入位置
		 * @param[in] y					图像输入位置
		 * @param[in] pImg				待输入图像
		 * @param[in] crop				裁剪区域
		 * @param[in] alpha				叠加透明度
		 * @param[in] bUseSrcAlpha		是否使用原图透明度
		 * @param[in] isCalculated		原图是否已经混合透明度
		*/
		void PutImageIn_Alpha(
			int x,
			int y,
			IMAGE* pImg,
			RECT crop = { 0 },
			BYTE alpha = 255,
			bool bUseSrcAlpha = false,
			bool isCalculated = false
		);

		/**
		 * @brief 将该画布的图像绘制到另一画布中
		 * @param[in] x				绘制位置
		 * @param[in] y				绘制位置
		 * @param[in] pImg			目标绘制画布
		 * @param[in] crop			裁剪区域（默认不裁剪）
		 * @param[in] alpha			叠加透明度
		 * @param[in] bUseSrcAlpha	是否使用此画布透明度
		 * @param[in] isCalculated	画布像素是否已经透明混合
		*/
		void RenderTo(
			int x,
			int y,
			IMAGE* pImg = nullptr,
			RECT crop = { 0 },
			BYTE alpha = 255,
			bool bUseSrcAlpha = false,
			bool isCalculated = false
		);

		/**
		 * @brief EasyX 原生旋转函数
		 * @param[in] radian		旋转弧度
		 * @param[in] bkcolor		填充背景色
		 * @param[in] autosize		是否自适应旋转图像大小
		 * @param[in] highquality	高质量
		*/
		void RotateImage(double radian, COLORREF bkcolor = BLACK, bool autosize = false, bool highquality = true);

		/**
		 * @brief 旋转图像（保留 Alpha 信息）
		 * @param[in] radian	旋转弧度
		 * @param[in] bkcolor	填充背景色
		*/
		void RotateImage_Alpha(double radian, COLORREF bkcolor = BLACK);

		/**
		 * @brief 缩放图像（粗糙的、即不插值的缩放，保留透明度信息）
		 * @param[in] nW	目标宽度
		 * @param[in] nH	目标高度（为 0 则根据宽度按比例缩放）
		*/
		void ZoomImage_Rough_Alpha(int nW, int nH = 0);

		/**
		 * @brief 缩放图像（双线性插值，保留透明度信息）
		 * @param[in] nW	目标宽度
		 * @param[in] nH	目标高度（为 0 则根据宽度按比例缩放）
		*/
		void ZoomImage_Alpha(int nW, int nH = 0);

		/**
		 * @brief 缩放图像（基于 Win32 API，比较快，保留透明度信息）
		 * @param[in] nW	目标宽度
		 * @param[in] nH	目标高度（为 0 则根据宽度按比例缩放）
		*/
		void ZoomImage_Win32_Alpha(int nW, int nH = 0);

		/**
		 * @brief 缩放图像（基于 Win32 API，比较快，保留透明度信息）
		 * @param[in] nW	目标宽度
		 * @param[in] nH	目标高度（为 0 则根据宽度按比例缩放）
		*/
		void ZoomImage_Gdiplus_Alpha(int nW, int nH = 0);

		/////// GDI+ 相关绘图函数 ///////

		////////////////////////////////////////////////////////////////////
		//
		// 注意：
		//		GDI+ 绘图函数不和 EasyX 原生函数共享同样的绘图颜色，
		//		这是因为 GDI+ 的绘图函数支持透明，而 EasyX 原生函数不支持。
		//
		//		可以使用 RGBA 或 SET_ALPHA 宏设置带透明度的颜色
		//
		////////////////////////////////////////////////////////////////////

		void GP_SetLineColor(COLORREF color);
		void GP_SetFillColor(COLORREF color);
		void GP_SetLineWidth(float width);

		COLORREF GP_GetLineColor() const { return m_cGPLineColor; }
		COLORREF GP_GetFillColor() const { return m_cGPFillColor; }
		float GP_GetLineWidth() const { return m_fGPLineWidth; }

		/**
		 * @brief 设置 GDI+ 绘制时是否使用透明度（默认不使用）
		*/
		void GP_EnableAlpha(bool enable);

		/**
		 * @brief 设置 GDI+ 抗锯齿模式
		*/
		void GP_SetSmoothingMode(Gdiplus::SmoothingMode smoothing_mode);

		bool GP_IsEnbaleAlpha() const { return m_bGPAlpha; }
		Gdiplus::SmoothingMode GP_GetSmoothingMode() const { return m_enuSmoothingMode; }

		void GP_Line(float x1, float y1, float x2, float y2, bool isSetColor = false, COLORREF linecolor = 0);

		void GP_Polygon(int points_num, POINT* points, bool isSetColor = false, COLORREF linecolor = 0);
		void GP_SolidPolygon(int points_num, POINT* points, bool isSetColor = false, COLORREF fillcolor = 0);
		void GP_FillPolygon(int points_num, POINT* points, bool isSetColor = false, COLORREF linecolor = 0, COLORREF fillcolor = 0);

		void GP_Rectangle(float x, float y, float w, float h, bool isSetColor = false, COLORREF linecolor = 0);
		void GP_SolidRectangle(float x, float y, float w, float h, bool isSetColor = false, COLORREF fillcolor = 0);
		void GP_FillRectangle(float x, float y, float w, float h, bool isSetColor = false, COLORREF linecolor = 0, COLORREF fillcolor = 0);

		void GP_RoundRect(float x, float y, float w, float h, float ellipsewidth, float ellipseheight, bool isSetColor = false, COLORREF linecolor = 0);
		void GP_SolidRoundRect(float x, float y, float w, float h, float ellipsewidth, float ellipseheight, bool isSetColor = false, COLORREF fillcolor = 0);
		void GP_FillRoundRect(float x, float y, float w, float h, float ellipsewidth, float ellipseheight, bool isSetColor = false, COLORREF linecolor = 0, COLORREF fillcolor = 0);

		void GP_Ellipse(float x, float y, float w, float h, bool isSetColor = false, COLORREF linecolor = 0);
		void GP_SolidEllipse(float x, float y, float w, float h, bool isSetColor = false, COLORREF fillcolor = 0);
		void GP_FillEllipse(float x, float y, float w, float h, bool isSetColor = false, COLORREF linecolor = 0, COLORREF fillcolor = 0);

		void GP_Pie(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor = false, COLORREF linecolor = 0);
		void GP_SolidPie(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor = false, COLORREF fillcolor = 0);
		void GP_FillPie(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor = false, COLORREF linecolor = 0, COLORREF fillcolor = 0);

		void GP_Arc(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor = false, COLORREF linecolor = 0);
	};

	/**
	 * @brief 图像块
	*/
	class ImageBlock
	{
	private:
		Canvas* m_pCanvas = nullptr;
		bool m_isCreated = false;			///< 画布是否为自己创建的

		void DeleteMyCanvas();				///< 删除自己创建的画布

	public:
		int x = 0, y = 0;					///< 图像显示在图层的位置
		RECT rctCrop = { 0 };				///< 裁剪信息
		bool bUseSrcAlpha = false;			///< 是否使用图像自身的 alpha 数据

		bool isAlphaCalculated = false;		///< 图像色值是否已混合透明度（使用自身透明度时有效）

		BYTE alpha = 255;					///< 绘制到图层时的叠加透明度
		bool bVisible = true;				///< 图像是否可见

		ImageBlock();

		ImageBlock(Canvas* pCanvas);

		ImageBlock(int _x, int _y, Canvas* pCanvas);

		/**
		 * @brief 新建画布
		 * @param[in] _x		位置
		 * @param[in] _y		位置
		 * @param[in] w		宽度
		 * @param[in] h		高度
		 * @param[in] cBk		背景色
		*/
		ImageBlock(int _x, int _y, int w, int h, COLORREF cBk = 0);

		virtual ~ImageBlock();

		/**
		 * @brief 不绑定外部画布，直接新建画布
		 * @param[in] w		宽度
		 * @param[in] h		高度
		 * @param[in] cBk		背景色
		 * @return 画布
		*/
		Canvas* CreateCanvas(int w, int h, COLORREF cBk = 0);

		Canvas* GetCanvas() const { return m_pCanvas; }
		void SetCanvas(Canvas* pCanvas);

		int GetWidth() const { return m_pCanvas ? m_pCanvas->GetWidth() : 0; }
		int GetHeight() const { return m_pCanvas ? m_pCanvas->GetHeight() : 0; }
		POINT GetPos() const { return { x,y }; }
		void SetPos(int _x, int _y);

		/**
		 * @brief 绘制到画布
		 * @param[in] pImg		目标绘制画布
		 * @param[in] _alpha	叠加透明度
		*/
		virtual void Render(IMAGE* pImg, BYTE _alpha);
	};

	/**
	 * @brief 图层
	*/
	class Layer : public std::list<ImageBlock*>
	{
	private:
		DrawingProperty m_property[2];		///< 保存上次的绘图属性

	public:
		bool bVisible = true;				///< 图层是否可见
		BYTE alpha = 255;					///< 图层中所有图像块的叠加透明度
		bool bOutline = false;				///< 是否显示轮廓
		bool bText = false;					///< 若显示轮廓，是否显示文字

		/**
		 * @brief 渲染到画布
		 * @param[in] pImg				目标绘制画布
		 * @param[in] bShowOutline		是否显示轮廓
		 * @param[in] bShowText		是否显示轮廓文本
		 * @param[in] wstrAddedText	附加轮廓文本
		*/
		#ifdef UNICODE
		void Render(IMAGE* pImg = nullptr, bool bShowOutline = false, bool bShowText = true, std::wstring wstrAddedText = L"");
		#else
		void Render(IMAGE* pImg = nullptr, bool bShowOutline = false, bool bShowText = true, std::string wstrAddedText = "");
		#endif
	};

	/**
	 * @brief 特殊图层顺序标识
	*/
	enum LayerOrder
	{
		LAYERORDER_BOTTOM_MOST,
		LAYERORDER_BOTTOM,
		LAYERORDER_NORMAL,
		LAYERORDER_TOP,
		LAYERORDER_TOP_MOST
	};

	/**
	 * @brief <pre>
	 *		场景
	 *
	 *	备注：
	 *		图层索引越大，图层越靠前
	 * </pre>
	*/
	class Scene : public std::vector<Layer*>
	{
	private:

		DrawingProperty m_property;				///< 保存之前的绘图属性

		// 特殊图层
		Layer m_layerBottomMost;				///< 最底层
		Layer m_layerBottom;					///< 底层
		Layer m_layerTop;						///< 顶层
		Layer m_layerTopMost;					///< 最顶层

	public:

		/**
		 * @brief <pre>
		 *		获取所有图层的拷贝
		 *
		 *	备注：
		 *		图层索引越大，图层越靠前
		 * </pre>
		 *
		 * @return 所有图层的拷贝
		*/
		std::vector<Layer*> GetAllLayer();

		/**
		 * @brief 获取所有图层的总数
		*/
		size_t GetAllLayerSize() const;

		/**
		 * @brief <pre>
		 *		获取特殊图层（除了普通图层外的其他图层，见 LayerOrder）
		 *
		 *	备注：
		 *		不建议滥用特殊图层
		 * </pre>
		 *
		 * @param[in] order 特殊图层索引
		 * @return 特殊图层
		*/
		Layer* GetSpecialLayer(LayerOrder order);

		/**
		 * @brief 渲染到画布
		 * @param[in] pImg				目标绘制画布
		 * @param[in] bShowAllOutline	是否显示轮廓
		 * @param[in] bShowAllText		是否显示轮廓文本
		*/
		void Render(IMAGE* pImg = nullptr, bool bShowAllOutline = false, bool bShowAllText = true);
	};
}

////////////////********* 宏定义 *********////////////////

//
// 准备绘制透明图形（先绘制图形到临时画布中，然后再输出到需要绘制的地方）
// nGraphW, nGraphH		所绘制透明图形的宽高
//
// 注意：需要配合 DRAW_TNS_RENDER_TO 宏使用
//
// 使用方法：
//		在 DRAW_TNS_INIT_GRAPHICS 宏和 DRAW_TNS_RENDER_TO 宏之间，插入一个代码块。
//		在这个代码块中使用 Canvas 变量 graphics 进行绘制。
//		绘制时调用 Canvas 的普通绘图函数即可，无需 GDI+ 系列封装函数（带 "GP_" 前缀的函数）
//		或者直接使用原生 EasyX 函数进行绘制也可以。
//
// 使用示例：
/*
	// 准备绘制透明图形（设置图形的宽高）
	DRAW_TNS_INIT_GRAPHICS(201, 201);
	{
		// 在代码块中使用 Canvas 的普通绘图函数进行绘制即可
		graphics.SetLineThickness(5);
		graphics.FillRoundRect(0, 0, 200, 200, 20, 20, true, GREEN, PURPLE);

		// 像这样使用 EasyX 原生函数绘制也可以
		line(20, 20, 50, 50);
	}
	// 最后选择将这个透明图形绘制到哪里，并设置绘制透明度
	DRAW_TNS_RENDER_TO(120, 120, yourImagePointer, 100);
*/
//
#define DRAW_TNS_INIT_GRAPHICS(nGraphW, nGraphH) \
	{\
		hiex::Canvas graphics(nGraphW, nGraphH);\
		graphics.BeginBatchDrawing();(0)

//
// 完成绘制透明图形，并输出绘制的图形
// nRenderX		输出位置 X 坐标
// nRenderY		输出位置 Y 坐标
// pDstImg		透明图形输出的目标画布（IMAGE*）
// alpha		输出图形时使用的透明度（完全透明 0 ~ 255 不透明）
//
// 注意：需要配合 DRAW_TNS_INIT_GRAPHICS 宏使用，具体用法见 DRAW_TNS_INIT_GRAPHICS 宏的注释
//
#define DRAW_TNS_RENDER_TO(nRenderX, nRenderY, pDstImg, alpha) \
		graphics.EndBatchDrawing();\
		ReverseAlpha(graphics.GetBuffer(), graphics.GetBufferSize());\
		graphics.RenderTo(nRenderX, nRenderY, pDstImg, { 0 }, alpha, true);\
	}(0)
// End of file
