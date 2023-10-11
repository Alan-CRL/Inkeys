#include "HiCanvas.h"

#include "HiMacro.h"
#include "HiWindow.h"
#include "HiGdiplus.h"

namespace HiEasyX
{
	COLORREF MixAlphaColor(COLORREF cDst, COLORREF cSrc, bool isCalculated, BYTE alpha)
	{
		float fSrc = GetAValue(cSrc) / 255.0f;	// 待绘制像素的透明度
		if (alpha != 255)						// 叠加透明度
			fSrc *= alpha / 255.0f;
		if (fSrc == 0.0f)						// 绘制透明度为 0 时不做任何处理
			return cDst;
		float fDst = 1 - fSrc;					// 原位置像素应乘透明度

		if (isCalculated)
		{
			return RGB(
				GetRValue(cSrc) + GetRValue(cDst) * fDst,
				GetGValue(cSrc) + GetGValue(cDst) * fDst,
				GetBValue(cSrc) + GetBValue(cDst) * fDst
			);
		}
		else
		{
			return RGB(
				GetRValue(cSrc) * fSrc + GetRValue(cDst) * fDst,
				GetGValue(cSrc) * fSrc + GetGValue(cDst) * fDst,
				GetBValue(cSrc) * fSrc + GetBValue(cDst) * fDst
			);
		}
	}

	void CopyImage_Alpha(int x, int y, DWORD* pDst, int wDst, int hDst, DWORD* pSrc, int wSrc, int hSrc, RECT crop, BYTE alpha, bool bUseSrcAlpha, bool isCalculated)
	{
		// 对待输出图像的裁剪起点或终点无效
		if (crop.left > wSrc || crop.top > hSrc || crop.right < crop.left || crop.bottom < crop.top)
			return;

		// 限制原图宽高
		int limit_w = ((crop.right && crop.right < wSrc) ? crop.right : wSrc);
		int limit_h = ((crop.bottom && crop.bottom < hSrc) ? crop.bottom : hSrc);

		// 标记不使用任何透明通道
		bool bNoAlpha = false;
		float fTransparent = alpha / 255.0f;
		if (alpha == 255 && !bUseSrcAlpha)	bNoAlpha = true;

		// 保证只有在使用原图透明度时，已计算标记才能开启
		if (!bUseSrcAlpha)	isCalculated = false;

		// i j -> 原图索引（若输出位置为负，则略过超出范围部分）
		// nx ny -> 载体图像索引
		for (int i = (x < 0 ? -x : 0) + crop.left, nx = i + x; i < limit_w && nx < wDst; i++, nx++)
		{
			for (int j = (y < 0 ? -y : 0) + crop.top, ny = j + y; j < limit_h && ny < hDst; j++, ny++)
			{
				int indexSrc = j * wSrc + i;
				int indexDst = ny * wDst + nx;
				if (indexSrc < 0 || indexDst < 0)
				{
					continue;
				}
				if (bNoAlpha)
				{
					pDst[indexDst] = pSrc[indexSrc];
				}
				else
				{
					pDst[indexDst] = MixAlphaColor(
						pDst[indexDst],
						bUseSrcAlpha ? pSrc[indexSrc] : SET_ALPHA(pSrc[indexSrc], 255),
						isCalculated,
						alpha
					);
				}
			}
		}
	}

	IMAGE RotateImage_Alpha(IMAGE* pImg, double radian, COLORREF bkcolor)
	{
		radian = -radian;														// 由于 y 轴翻转，旋转角度需要变负
		float fSin = (float)sin(radian), fCos = (float)cos(radian);				// 存储三角函数值
		float fNSin = (float)sin(-radian), fNCos = (float)cos(-radian);
		int left = 0, top = 0, right = 0, bottom = 0;							// 旋转后图像顶点
		int w, h;
		GetImageSize(pImg, &w, &h);
		DWORD* pBuf = GetImageBuffer(pImg);
		POINT points[4] = { { 0, 0 },{ w, 0 },{ 0, h },{ w, h } };				// 存储图像顶点
		for (int j = 0; j < 4; j++)												// 旋转图像顶点，搜索旋转后的图像边界
		{
			points[j] = {
				(int)(points[j].x * fCos - points[j].y * fSin),
				(int)(points[j].x * fSin + points[j].y * fCos)
			};
			if (points[j].x < points[left].x)	left = j;
			if (points[j].y > points[top].y)	top = j;
			if (points[j].x > points[right].x)	right = j;
			if (points[j].y < points[bottom].y)	bottom = j;
		}

		int nw = points[right].x - points[left].x;								// 旋转后的图像尺寸
		int nh = points[top].y - points[bottom].y;
		int nSize = nw * nh;
		int offset_x = points[left].x < 0 ? points[left].x : 0;					// 旋转后图像超出第一象限的位移（据此调整图像位置）
		int offset_y = points[bottom].y < 0 ? points[bottom].y : 0;

		IMAGE img(nw, nh);
		DWORD* pNewBuf = GetImageBuffer(&img);
		if (bkcolor != BLACK)													// 设置图像背景色
			for (int i = 0; i < nSize; i++)
				pNewBuf[i] = BGR(bkcolor);

		for (int i = offset_x, ni = 0; ni < nw; i++, ni++)						// i 用于映射原图像坐标，ni 用于定位旋转后图像坐标
		{
			for (int j = offset_y, nj = 0; nj < nh; j++, nj++)
			{
				int nx = (int)(i * fNCos - j * fNSin);							// 从旋转后的图像坐标向原图像坐标映射
				int ny = (int)(i * fNSin + j * fNCos);
				if (nx >= 0 && nx < w && ny >= 0 && ny < h)						// 若目标映射在原图像范围内，则拷贝色值
					pNewBuf[nj * nw + ni] = pBuf[ny * w + nx];
			}
		}

		return img;
	}

	IMAGE ZoomImage_Rough_Alpha(IMAGE* srcimg, int width, int height)
	{
		int src_width, src_height;
		GetImageSize(srcimg, &src_width, &src_height);

		// 自动缩放
		if (height == 0)
		{
			height = width * src_height / src_width;
		}

		IMAGE dstImage(width, height);
		IMAGE* dstimg = &dstImage;

		DWORD* dst = GetImageBuffer(dstimg);
		DWORD* src = GetImageBuffer(srcimg);

		float w_scale_rate = (float)src_width / width;
		float h_scale_rate = (float)src_height / height;

		for (int iy = 0; iy < height; iy++)
		{
			for (int ix = 0; ix < width; ix++)
			{
				// 计算原图相应坐标
				int x_scale = (int)(w_scale_rate * ix);
				int y_scale = (int)(h_scale_rate * iy);
				dst[ix + iy * width] = src[(int)(x_scale + y_scale * src_width)];
			}
		}

		return dstImage;
	}

	IMAGE ZoomImage_Alpha(IMAGE* srcimg, int width, int height)
	{
		int old_w, old_h;
		GetImageSize(srcimg, &old_w, &old_h);

		// 自动缩放
		if (height == 0)
		{
			height = width * old_h / old_w;
		}

		IMAGE output(width, height);
		DWORD* src = GetImageBuffer(srcimg);
		DWORD* dst = GetImageBuffer(&output);

		// 双线性插值（因为向下取样，所以 w, h 都要减一避免越界）
		for (int i = 0; i < height - 1; i++)
		{
			for (int j = 0; j < width - 1; j++)
			{
				int xt = j * old_w / width;		// 新图坐标映射到原图上的位置
				int yt = i * old_h / height;

				// 实现逐行加载图片
				byte r = (
					GetRValue(src[xt + yt * old_w]) +
					GetRValue(src[xt + yt * old_w + 1]) +
					GetRValue(src[xt + (yt + 1) * old_w]) +
					GetRValue(src[xt + (yt + 1) * old_w + 1])) / 4;
				byte g = (
					GetGValue(src[xt + yt * old_w]) +
					GetGValue(src[xt + yt * old_w] + 1) +
					GetGValue(src[xt + (yt + 1) * old_w]) +
					GetGValue(src[xt + (yt + 1) * old_w]) + 1) / 4;
				byte b = (
					GetBValue(src[xt + yt * old_w]) +
					GetBValue(src[xt + yt * old_w] + 1) +
					GetBValue(src[xt + (yt + 1) * old_w]) +
					GetBValue(src[xt + (yt + 1) * old_w + 1])) / 4;

				// 赋值，保留 alpha
				dst[i * width + j] = RGBA(r, g, b, GetAValue(src[xt + yt * old_w]));
			}
		}

		return output;
	}

	IMAGE ZoomImage_Win32_Alpha(IMAGE* srcimg, int width, int height)
	{
		int w, h;
		GetImageSize(srcimg, &w, &h);

		// 自动缩放
		if (height == 0)
		{
			height = width * h / w;
		}

		IMAGE img(width, height);
		StretchBlt(
			GetImageHDC(&img), 0, 0, width, height,
			GetImageHDC(srcimg), 0, 0,
			w, h, SRCCOPY
		);
		return img;
	}

	void Canvas::CleanUpSettings()
	{
		m_property = {};
		m_pBuf = nullptr;
		m_nWidth = 0;
		m_nHeight = 0;
		m_nBufSize = 0;
		m_bBindToImgPointer = false;
		m_pImg = nullptr;
		m_bBatchDraw = false;
		m_hBindWindow = nullptr;
	}

	bool Canvas::BeginWindowTask()
	{
		if (m_hBindWindow)
		{
			if (HiEasyX::SetWorkingWindow(m_hBindWindow))
			{
				if (HiEasyX::BeginTask())
				{
					return true;
				}
			}
			return false;
		}
		else
		{
			// 内部总是调用此函数，故未绑定窗口时直接返回 true
			return true;
		}
	}

	void Canvas::EndWindowTask()
	{
		if (m_hBindWindow)
		{
			HiEasyX::EndTask(m_bAutoMarkFlushWindow);
		}
	}

	bool Canvas::BeginDrawing()
	{
		IMAGE* p = GetImagePointer();
		if (GetWorkingImage() != p)
		{
			m_property.SaveWorkingImageOnly();
			SetWorkingImage(p);
		}
		return BeginWindowTask();
	}

	void Canvas::EndDrawing()
	{
		// 批量绘制模式下，不退出绘图目标
		if (!m_bBatchDraw)
		{
			if (m_property.IsSaved())
			{
				m_property.ApplyWorkingImageOnly();
				m_property.Reset();
			}

			EndWindowTask();
		}
	}

	Canvas::Canvas()
	{
		CleanUpSettings();
		UpdateSizeInfo();
	}

	Canvas::Canvas(int w, int h, COLORREF cBk)
	{
		CleanUpSettings();
		Resize(w, h);
		SetBkColor(cBk);
		Clear();
	}

	Canvas::Canvas(IMAGE* pImg)
	{
		operator=(pImg);
	}

	Canvas::Canvas(IMAGE img)
	{
		operator=(img);
	}

	Canvas& Canvas::operator= (IMAGE* pImg)
	{
		CleanUpSettings();
		int w, h;
		GetImageSize(pImg, &w, &h);

		if (BeginWindowTask())
		{
			Resize(w, h);
			UpdateSizeInfo();

			// 不使用 IMAGE::operator=，因为它不支持指针
			memcpy(m_pBuf, GetImageBuffer(pImg), m_nBufSize * sizeof(COLORREF));
			EndWindowTask();
		}

		return *this;
	}

	Canvas& Canvas::operator= (IMAGE img)
	{
		return operator=(&img);
	}

	void Canvas::UpdateSizeInfo()
	{
		m_pBuf = GetImageBuffer(GetImagePointer());

		if (m_bBindToImgPointer)
		{
			GetImageSize(m_pImg, &m_nWidth, &m_nHeight);
		}
		else
		{
			m_nWidth = IMAGE::getwidth();
			m_nHeight = IMAGE::getheight();
		}

		m_nBufSize = m_nWidth * m_nHeight;
	}

	void Canvas::Resize(int w, int h)
	{
		if (BeginWindowTask())
		{
			if (!(w * h))
			{
				w = h = 1;
			}

			if (m_bBindToImgPointer)
			{
				::Resize(m_pImg, w, h);
			}
			else
			{
				IMAGE::Resize(w, h);
			}

			EndWindowTask();
			UpdateSizeInfo();
		}
	}

	Canvas& Canvas::BindToImage(IMAGE* pImg)
	{
		CleanUpSettings();
		m_bBindToImgPointer = true;
		m_pImg = pImg;
		UpdateSizeInfo();
		return *this;
	}

	Canvas& Canvas::BindToWindow(HWND hWnd, IMAGE* pImg)
	{
		BindToImage(pImg);
		m_hBindWindow = hWnd;
		return *this;
	}

	void Canvas::EnableAutoMarkFlushWindow(bool enable)
	{
		m_bAutoMarkFlushWindow = enable;
	}

	void Canvas::BeginBatchDrawing()
	{
		if (BeginDrawing())
		{
			m_bBatchDraw = true;
		}
	}

	void Canvas::EndBatchDrawing()
	{
		if (m_bBatchDraw)
		{
			m_bBatchDraw = false;
			EndDrawing();
		}
	}

	bool Canvas::IsValidPoint(int x, int y, int* pIndex)
	{
		if (x < 0 || y < 0 || x >= m_nWidth || y >= m_nHeight)
			return false;
		if (pIndex)	*pIndex = y * m_nWidth + x;
		return true;
	}

	void Canvas::RenderTo(int x, int y, IMAGE* pImg, RECT crop, BYTE alpha, bool bUseSrcAlpha, bool isCalculated)
	{
		int w, h;	// 目标输出画布尺寸
		GetImageSize(pImg, &w, &h);
		if (BeginWindowTask())
		{
			CopyImage_Alpha(
				x, y,
				GetImageBuffer(pImg), w, h,
				m_pBuf, m_nWidth, m_nHeight,
				crop,
				alpha, bUseSrcAlpha, isCalculated
			);

			EndWindowTask();
		}
	}

	LINESTYLE Canvas::GetLineStyle()
	{
		if (BeginDrawing())
		{
			LINESTYLE s;
			getlinestyle(&s);
			EndDrawing();
			return s;
		}
		return {};
	}

	void Canvas::Clear(bool isSetColor, COLORREF bkcolor)
	{
		Clear_Alpha(isSetColor, bkcolor, true);
	}

	void Canvas::Clear_Alpha(bool isSetColor, COLORREF bkcolor, bool ignore_alpha)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setbkcolor(bkcolor);
			DWORD bk_bgr = BGR(getbkcolor());

			if (ignore_alpha)		// 设置背景透明度为 255（不透明）
				bk_bgr |= 0xFF000000;

			for (int i = 0; i < m_nBufSize; i++)
				m_pBuf[i] = bk_bgr;

			EndDrawing();
		}
	}

	void Canvas::SetLineStyle(LINESTYLE style)
	{
		if (BeginDrawing())
		{
			setlinestyle(&style);
			EndDrawing();
		}
	}

	void Canvas::SetLineStyle(int style, int thickness, const DWORD* puserstyle, DWORD userstylecount)
	{
		if (BeginDrawing())
		{
			setlinestyle(style, thickness, puserstyle, userstylecount);
			EndDrawing();
		}
	}

	void Canvas::SetLineThickness(int thickness)
	{
		LINESTYLE s = GetLineStyle();
		s.thickness = thickness;
		SetLineStyle(s);
	}

	int Canvas::GetLineThickness()
	{
		return GetLineStyle().thickness;
	}

	FILLSTYLE Canvas::GetFillStyle()
	{
		if (BeginDrawing())
		{
			FILLSTYLE s;
			getfillstyle(&s);
			EndDrawing();
			return s;
		}
		return {};
	}

	void Canvas::SetFillStyle(FILLSTYLE style)
	{
		if (BeginDrawing())
		{
			setfillstyle(&style);
			EndDrawing();
		}
	}

	void Canvas::SetFillStyle(int style, long hatch, IMAGE* ppattern)
	{
		if (BeginDrawing())
		{
			setfillstyle(style, hatch, ppattern);
			EndDrawing();
		}
	}

	void Canvas::SetFillStyle(BYTE* ppattern8x8)
	{
		if (BeginDrawing())
		{
			setfillstyle(ppattern8x8);
			EndDrawing();
		}
	}

	int Canvas::GetRop2()
	{
		if (BeginDrawing())
		{
			int r = getrop2();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetRop2(int mode)
	{
		if (BeginDrawing())
		{
			setrop2(mode);
			EndDrawing();
		}
	}

	int Canvas::GetPolyFillMode()
	{
		if (BeginDrawing())
		{
			int r = getpolyfillmode();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetPolyFillMode(int mode)
	{
		if (BeginDrawing())
		{
			setpolyfillmode(mode);
			EndDrawing();
		}
	}

	void Canvas::SetDefault()
	{
		if (BeginDrawing())
		{
			graphdefaults();
			EndDrawing();
		}
	}

	COLORREF Canvas::GetLineColor()
	{
		if (BeginDrawing())
		{
			COLORREF r = getlinecolor();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetLineColor(COLORREF color)
	{
		if (BeginDrawing())
		{
			setlinecolor(color);
			EndDrawing();
		}
	}

	COLORREF Canvas::GetTextColor()
	{
		if (BeginDrawing())
		{
			COLORREF r = gettextcolor();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetTextColor(COLORREF color)
	{
		if (BeginDrawing())
		{
			settextcolor(color);
			EndDrawing();
		}
	}

	COLORREF Canvas::GetFillColor()
	{
		if (BeginDrawing())
		{
			COLORREF r = getfillcolor();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetFillColor(COLORREF color)
	{
		if (BeginDrawing())
		{
			setfillcolor(color);
			EndDrawing();
		}
	}

	COLORREF Canvas::GetBkColor()
	{
		if (BeginDrawing())
		{
			COLORREF r = getbkcolor();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetBkColor(COLORREF color)
	{
		if (BeginDrawing())
		{
			setbkcolor(color);
			EndDrawing();
		}
	}

	int Canvas::GetBkMode()
	{
		if (BeginDrawing())
		{
			COLORREF r = getbkmode();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetBkMode(int mode)
	{
		if (BeginDrawing())
		{
			setbkmode(mode);
			EndDrawing();
		}
	}

	COLORREF Canvas::GetPixel(int x, int y)
	{
		if (BeginDrawing())
		{
			COLORREF c = getpixel(x, y);
			EndDrawing();
			return c;
		}
		return 0;
	}

	void Canvas::PutPixel(int x, int y, COLORREF c)
	{
		if (BeginDrawing())
		{
			putpixel(x, y, c);
			EndDrawing();
		}
	}

	COLORREF Canvas::GetPixel_Direct(int x, int y)
	{
		if (BeginWindowTask())
		{
			COLORREF c = BGR(m_pBuf[y * m_nWidth + x]);
			EndWindowTask();
			return c;
		}
		return 0;
	}

	void Canvas::PutPixel_Direct(int x, int y, COLORREF c)
	{
		if (BeginWindowTask())
		{
			c = BGR(c);
			int index = 0;
			if (IsValidPoint(x, y, &index))
				m_pBuf[index] = c;

			EndWindowTask();
		}
	}

	void Canvas::PutPixel_Direct_Alpha(int x, int y, COLORREF c)
	{
		if (BeginWindowTask())
		{
			c = BGR(c);
			int index = 0;
			if (IsValidPoint(x, y, &index))
			{
				m_pBuf[index] = MixAlphaColor(m_pBuf[index], c, false);
			}

			EndWindowTask();
		}
	}

	void Canvas::Line(int x1, int y1, int x2, int y2, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			line(x1, y1, x2, y2);
			EndDrawing();
		}
	}

	void Canvas::Line(POINT pt1, POINT pt2, bool isSetColor, COLORREF c)
	{
		Line(pt1.x, pt1.y, pt2.x, pt2.y, isSetColor, c);
	}

	void Canvas::Rectangle(int left, int top, int right, int bottom, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			rectangle(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::Rectangle(RECT rct, bool isSetColor, COLORREF c)
	{
		Rectangle(rct.left, rct.top, rct.right, rct.bottom, isSetColor, c);
	}

	void Canvas::FillRectangle(int left, int top, int right, int bottom, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)
			{
				setlinecolor(cLine);
				setfillcolor(cFill);
			}

			fillrectangle(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::FillRectangle(RECT rct, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		FillRectangle(rct.left, rct.top, rct.right, rct.bottom, isSetColor, cLine, cFill);
	}

	void Canvas::SolidRectangle(int left, int top, int right, int bottom, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(c);
			solidrectangle(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::SolidRectangle(RECT rct, bool isSetColor, COLORREF c)
	{
		SolidRectangle(rct.left, rct.top, rct.right, rct.bottom, isSetColor, c);
	}

	void Canvas::ClearRectangle(int left, int top, int right, int bottom)
	{
		if (BeginDrawing())
		{
			clearrectangle(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::ClearRectangle(RECT rct)
	{
		ClearRectangle(rct.left, rct.top, rct.right, rct.bottom);
	}

	void Canvas::Circle(int x, int y, int radius, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			circle(x, y, radius);
			EndDrawing();
		}
	}

	void Canvas::FillCircle(int x, int y, int radius, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)
			{
				setlinecolor(cLine);
				setfillcolor(cFill);
			}
			fillcircle(x, y, radius);
			EndDrawing();
		}
	}

	void Canvas::SolidCircle(int x, int y, int radius, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(c);
			solidcircle(x, y, radius);
			EndDrawing();
		}
	}

	void Canvas::ClearCircle(int x, int y, int radius)
	{
		if (BeginDrawing())
		{
			clearcircle(x, y, radius);
			EndDrawing();
		}
	}

	void Canvas::Ellipse(int left, int top, int right, int bottom, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			ellipse(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::Ellipse(RECT rct, bool isSetColor, COLORREF c)
	{
		Ellipse(rct.left, rct.top, rct.right, rct.bottom, isSetColor, c);
	}

	void Canvas::FillEllipse(int left, int top, int right, int bottom, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)
			{
				setlinecolor(cLine);
				setfillcolor(cFill);
			}
			fillellipse(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::FillEllipse(RECT rct, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		FillEllipse(rct.left, rct.top, rct.right, rct.bottom, isSetColor, cLine, cFill);
	}

	void Canvas::SolidEllipse(int left, int top, int right, int bottom, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(c);
			solidellipse(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::SolidEllipse(RECT rct, bool isSetColor, COLORREF c)
	{
		SolidEllipse(rct.left, rct.top, rct.right, rct.bottom, isSetColor, c);
	}

	void Canvas::ClearEllipse(int left, int top, int right, int bottom)
	{
		if (BeginDrawing())
		{
			clearellipse(left, top, right, bottom);
			EndDrawing();
		}
	}

	void Canvas::ClearEllipse(RECT rct)
	{
		ClearEllipse(rct.left, rct.top, rct.right, rct.bottom);
	}

	void Canvas::RoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			roundrect(left, top, right, bottom, ellipsewidth, ellipseheight);
			EndDrawing();
		}
	}

	void Canvas::RoundRect(RECT rct, int ellipsewidth, int ellipseheight, bool isSetColor, COLORREF c)
	{
		RoundRect(rct.left, rct.top, rct.right, rct.bottom, ellipsewidth, ellipseheight, c, isSetColor);
	}

	void Canvas::FillRoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)
			{
				setlinecolor(cLine);
				setfillcolor(cFill);
			}
			fillroundrect(left, top, right, bottom, ellipsewidth, ellipseheight);
			EndDrawing();
		}
	}

	void Canvas::FillRoundRect(RECT rct, int ellipsewidth, int ellipseheight, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		FillRoundRect(rct.left, rct.top, rct.right, rct.bottom, ellipsewidth, ellipseheight, cLine, cFill, isSetColor);
	}

	void Canvas::SolidRoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(c);
			solidroundrect(left, top, right, bottom, ellipsewidth, ellipseheight);
			EndDrawing();
		}
	}

	void Canvas::SolidRoundRect(RECT rct, int ellipsewidth, int ellipseheight, bool isSetColor, COLORREF c)
	{
		SolidRoundRect(rct.left, rct.top, rct.right, rct.bottom, ellipsewidth, ellipseheight, c, isSetColor);
	}

	void Canvas::ClearRoundRect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight)
	{
		if (BeginDrawing())
		{
			clearroundrect(left, top, right, bottom, ellipsewidth, ellipseheight);
			EndDrawing();
		}
	}

	void Canvas::ClearRoundRect(RECT rct, int ellipsewidth, int ellipseheight)
	{
		ClearRoundRect(rct.left, rct.top, rct.right, rct.bottom, ellipsewidth, ellipseheight);
	}

	void Canvas::Arc(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			arc(left, top, right, bottom, stangle, endangle);
			EndDrawing();
		}
	}

	void Canvas::Arc(RECT rct, double stangle, double endangle, bool isSetColor, COLORREF c)
	{
		Arc(rct.left, rct.top, rct.right, rct.bottom, stangle, endangle, isSetColor, c);
	}

	void Canvas::Pie(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			pie(left, top, right, bottom, stangle, endangle);
			EndDrawing();
		}
	}

	void Canvas::Pie(RECT rct, double stangle, double endangle, bool isSetColor, COLORREF c)
	{
		Pie(rct.left, rct.top, rct.right, rct.bottom, stangle, endangle, isSetColor, c);
	}

	void Canvas::FillPie(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)
			{
				setlinecolor(cLine);
				setfillcolor(cFill);
			}
			fillpie(left, top, right, bottom, stangle, endangle);
			EndDrawing();
		}
	}

	void Canvas::FillPie(RECT rct, double stangle, double endangle, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		FillPie(rct.left, rct.top, rct.right, rct.bottom, stangle, endangle, isSetColor, cLine, cFill);
	}

	void Canvas::SolidPie(int left, int top, int right, int bottom, double stangle, double endangle, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(c);
			solidpie(left, top, right, bottom, stangle, endangle);
			EndDrawing();
		}
	}

	void Canvas::SolidPie(RECT rct, double stangle, double endangle, bool isSetColor, COLORREF c)
	{
		SolidPie(rct.left, rct.top, rct.right, rct.bottom, stangle, endangle, isSetColor, c);
	}

	void Canvas::ClearPie(int left, int top, int right, int bottom, double stangle, double endangle)
	{
		if (BeginDrawing())
		{
			clearpie(left, top, right, bottom, stangle, endangle);
			EndDrawing();
		}
	}

	void Canvas::ClearPie(RECT rct, double stangle, double endangle)
	{
		ClearPie(rct.left, rct.top, rct.right, rct.bottom, stangle, endangle);
	}

	void Canvas::Polyline(const POINT* points, int num, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			polyline(points, num);
			EndDrawing();
		}
	}

	void Canvas::Polygon(const POINT* points, int num, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			polygon(points, num);
			EndDrawing();
		}
	}

	void Canvas::FillPolygon(const POINT* points, int num, bool isSetColor, COLORREF cLine, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)
			{
				setlinecolor(cLine);
				setfillcolor(cFill);
			}
			fillpolygon(points, num);
			EndDrawing();
		}
	}

	void Canvas::SolidPolygon(const POINT* points, int num, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(c);
			solidpolygon(points, num);
			EndDrawing();
		}
	}

	void Canvas::ClearPolygon(const POINT* points, int num)
	{
		if (BeginDrawing())
		{
			clearpolygon(points, num);
			EndDrawing();
		}
	}

	void Canvas::PolyBezier(const POINT* points, int num, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setlinecolor(c);
			polybezier(points, num);
			EndDrawing();
		}
	}

	void Canvas::FloodFill(int x, int y, COLORREF color, int filltype, bool isSetColor, COLORREF cFill)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setfillcolor(cFill);
			floodfill(x, y, color, filltype);
			EndDrawing();
		}
	}

	int Canvas::OutTextXY(int x, int y, LPCTSTR lpszText, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	settextcolor(c);
			outtextxy(x, y, lpszText);
			int width = textwidth(lpszText);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::OutTextXY(int x, int y, TCHAR ch, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	settextcolor(c);
			outtextxy(x, y, ch);
			int width = textwidth(ch);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::OutTextXY_Format(int x, int y, int _Size, LPCTSTR _Format, ...)
	{
		if (BeginDrawing())
		{
#ifdef UNICODE
			va_list list;
			va_start(list, _Format);
			wchar_t* buf = new wchar_t[_Size];
			vswprintf_s(buf, _Size, _Format, list);
			va_end(list);

			outtextxy(x, y, buf);
			int width = textwidth(buf);
			delete[] buf;

			EndDrawing();
			return width;
#else
			va_list list;
			va_start(list, _Format);
			char* buf = new char[_Size];
			
#ifdef _MSC_VER
			_vsnprintf_s(buf, _Size, _TRUNCATE, _Format, list);
#else
			_vsnprintf(buf, _Size, _Format, list);
#endif
			
			va_end(list);

			outtextxy(x, y, buf);
			int width = textwidth(buf);
			delete[] buf;

			EndDrawing();
			return width;
#endif
		}
		return 0;
	}

	int Canvas::TextWidth(LPCTSTR lpszText)
	{
		if (BeginDrawing())
		{
			int width = textwidth(lpszText);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::TextWidth(TCHAR c)
	{
		if (BeginDrawing())
		{
			int width = textwidth(c);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::TextHeight(LPCTSTR lpszText)
	{
		if (BeginDrawing())
		{
			int width = textheight(lpszText);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::TextHeight(TCHAR c)
	{
		if (BeginDrawing())
		{
			int width = textheight(c);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::Draw_Text(LPCTSTR str, RECT* pRect, UINT uFormat, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	settextcolor(c);
			int width = drawtext(str, pRect, uFormat);
			EndDrawing();
			return width;
		}
		return 0;
	}

	int Canvas::Draw_Text(TCHAR ch, RECT* pRect, UINT uFormat, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	settextcolor(c);
			int width = drawtext(ch, pRect, uFormat);
			EndDrawing();
			return width;
		}
		return 0;
	}

	void Canvas::CenterText(LPCTSTR lpszText, RECT rct, bool isSetColor, COLORREF c)
	{
		if (rct.left < 0)	rct = { 0,0,m_nWidth,m_nHeight };
		Draw_Text(lpszText, &rct, DT_CENTER | DT_VCENTER | DT_SINGLELINE, isSetColor, c);
	}

	void Canvas::CenterText_Format(int _Size, LPCTSTR _Format, ...)
	{
#ifdef UNICODE
		va_list list;
		va_start(list, _Format);
		wchar_t* buf = new wchar_t[_Size];
		vswprintf_s(buf, _Size, _Format, list);
		va_end(list);

		CenterText(buf);
		delete[] buf;
#else
		va_list list;
		va_start(list, _Format);
		TCHAR* buf = new TCHAR[_Size];
		
#ifdef _MSC_VER
		_vsnprintf_s(buf, _Size, _TRUNCATE, _Format, list);
#else
		_vsnprintf(buf, _Size, _Format, list);
#endif
		
		va_end(list);

		CenterText(buf);
		delete[] buf;
#endif
	}

	LOGFONT Canvas::GetTextStyle()
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			EndDrawing();
			return font;
		}
		return {};
	}

	void Canvas::SetTextStyle(int nHeight, int nWidth, LPCTSTR lpszFace)
	{
		if (BeginDrawing())
		{
			settextstyle(nHeight, nWidth, lpszFace);
			EndDrawing();
		}
	}

	void Canvas::SetTextStyle(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut)
	{
		if (BeginDrawing())
		{
			settextstyle(nHeight, nWidth, lpszFace, nEscapement, nOrientation, nWeight, bItalic, bUnderline, bStrikeOut);
			EndDrawing();
		}
	}

	void Canvas::SetTextStyle(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut, BYTE fbCharSet, BYTE fbOutPrecision, BYTE fbClipPrecision, BYTE fbQuality, BYTE fbPitchAndFamily)
	{
		if (BeginDrawing())
		{
			settextstyle(nHeight, nWidth, lpszFace, nEscapement, nOrientation, nWeight, bItalic, bUnderline, bStrikeOut, fbCharSet, fbOutPrecision, fbClipPrecision, fbQuality, fbPitchAndFamily);
			EndDrawing();
		}
	}

	void Canvas::SetTextStyle(LOGFONT font)
	{
		if (BeginDrawing())
		{
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetFont(int nHeight, int nWidth)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfHeight = nHeight;
			font.lfWidth = nWidth;
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTypeface(LPCTSTR lpsz)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			lstrcpy(font.lfFaceName, lpsz);
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTextEscapement(LONG lfEscapement)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfEscapement = lfEscapement;
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTextOrientation(LONG lfOrientation)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfOrientation = lfOrientation;
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTextWeight(LONG lfWeight)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfWeight = lfWeight;
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTextItalic(bool lfItalic)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfItalic = lfItalic;
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTextUnderline(bool lfUnderline)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfUnderline = lfUnderline;
			settextstyle(&font);
			EndDrawing();
		}
	}

	void Canvas::SetTextStrikeOut(bool lfStrikeOut)
	{
		if (BeginDrawing())
		{
			LOGFONT font;
			gettextstyle(&font);
			font.lfStrikeOut = lfStrikeOut;
			settextstyle(&font);
			EndDrawing();
		}
	}

	COLORREF Canvas::GetColor()
	{
		if (BeginDrawing())
		{
			int r = getcolor();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::SetColor(COLORREF color)
	{
		if (BeginDrawing())
		{
			setcolor(color);
			EndDrawing();
		}
	}

	int Canvas::GetX()
	{
		if (BeginDrawing())
		{
			int r = getx();
			EndDrawing();
			return r;
		}
		return 0;
	}

	int Canvas::GetY()
	{
		if (BeginDrawing())
		{
			int r = gety();
			EndDrawing();
			return r;
		}
		return 0;
	}

	void Canvas::MoveTo(int x, int y)
	{
		if (BeginDrawing())
		{
			moveto(x, y);
			EndDrawing();
		}
	}

	void Canvas::MoveRel(int dx, int dy)
	{
		if (BeginDrawing())
		{
			moverel(dx, dy);
			EndDrawing();
		}
	}

	void Canvas::LineTo(int x, int y, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setcolor(c);
			lineto(x, y);
			EndDrawing();
		}
	}

	void Canvas::LineRel(int dx, int dy, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setcolor(c);
			linerel(dx, dy);
			EndDrawing();
		}
	}

	void Canvas::OutText(LPCTSTR lpszText, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setcolor(c);
			outtext(lpszText);
			EndDrawing();
		}
	}

	void Canvas::OutText(TCHAR ch, bool isSetColor, COLORREF c)
	{
		if (BeginDrawing())
		{
			if (isSetColor)	setcolor(c);
			outtext(ch);
			EndDrawing();
		}
	}

	int Canvas::OutText_Format(int _Size, LPCTSTR _Format, ...)
	{
		if (BeginDrawing())
		{
#ifdef UNICODE
			va_list list;
			va_start(list, _Format);
			wchar_t* buf = new wchar_t[_Size];
			vswprintf_s(buf, _Size, _Format, list);
			va_end(list);

			outtext(buf);
			int width = textwidth(buf);
			delete[] buf;

			EndDrawing();
			return width;
#else
			va_list list;
			va_start(list, _Format);
			TCHAR* buf = new TCHAR[_Size];
			
#ifdef _MSC_VER
			_vsnprintf_s(buf, _Size, _TRUNCATE, _Format, list);
#else
			_vsnprintf(buf, _Size, _Format, list);
#endif
			
			va_end(list);

			outtext(buf);
			int width = textwidth(buf);
			delete[] buf;

			EndDrawing();
			return width;
#endif
		}
		return 0;
	}

	IMAGE Canvas::Load_Image_Alpha(LPCTSTR lpszImgFile, int x, int y, bool bResize, int nWidth, int nHeight, BYTE alpha, bool bUseSrcAlpha, bool isCalculated)
	{
		// 标记到底要不要调整图像大小
		bool resize_flag = bResize;
		if (m_nWidth == 0 && m_nHeight == 0)
		{
			resize_flag = true;
		}

		// 如果超出画布范围而又不调整图像大小，则直接返回
		if (!resize_flag && (x > m_nWidth || y > m_nHeight))	return {};

		// 加载图像
		IMAGE img;
		loadimage(&img, lpszImgFile, nWidth, nHeight, true);
		int w = img.getwidth(), h = img.getheight();

		// 如果图像在左侧或上方越界，那么不用输出了
		if (x <= -w || y <= -h)	return {};

		// 调整图像大小
		if (resize_flag)
		{
			int nw = x + w;
			int nh = y + h;
			Resize(nw, nh);
		}

		// 输出图像
		PutImageIn_Alpha(x, y, &img, { 0 }, alpha, bUseSrcAlpha, isCalculated);
		return img;
	}

	void Canvas::PutImageIn_Alpha(int x, int y, IMAGE* pImg, RECT crop, BYTE alpha, bool bUseSrcAlpha, bool isCalculated)
	{
		int w, h;	// 原图像尺寸
		GetImageSize(pImg, &w, &h);
		if (BeginWindowTask())
		{
			CopyImage_Alpha(
				x, y,
				m_pBuf, m_nWidth, m_nHeight,
				GetImageBuffer(pImg), w, h,
				crop,
				alpha, bUseSrcAlpha, isCalculated
			);

			EndWindowTask();
		}
	}

	void Canvas::RotateImage(double radian, COLORREF bkcolor, bool autosize, bool highquality)
	{
		if (BeginWindowTask())
		{
			IMAGE* p = GetImagePointer();
			rotateimage(p, p, radian, bkcolor, autosize, highquality);
			BeginWindowTask();
		}
	}

	void Canvas::RotateImage_Alpha(double radian, COLORREF bkcolor)
	{
		operator=(HiEasyX::RotateImage_Alpha(GetImagePointer(), radian, bkcolor));
	}

	void Canvas::ZoomImage_Rough_Alpha(int nW, int nH)
	{
		if (m_bBindToImgPointer)
		{
			if (BeginWindowTask())
			{
				*m_pImg = HiEasyX::ZoomImage_Rough_Alpha(m_pImg, nW, nH);
				UpdateSizeInfo();

				EndWindowTask();
			}
		}
		else
		{
			operator=(HiEasyX::ZoomImage_Rough_Alpha(this, nW, nH));
		}
	}

	void Canvas::ZoomImage_Alpha(int nW, int nH)
	{
		if (m_bBindToImgPointer)
		{
			if (BeginWindowTask())
			{
				*m_pImg = HiEasyX::ZoomImage_Alpha(m_pImg, nW, nH);
				UpdateSizeInfo();

				EndWindowTask();
			}
		}
		else
		{
			operator=(HiEasyX::ZoomImage_Alpha(this, nW, nH));
		}
	}

	void Canvas::ZoomImage_Win32_Alpha(int nW, int nH)
	{
		if (m_bBindToImgPointer)
		{
			if (BeginWindowTask())
			{
				*m_pImg = HiEasyX::ZoomImage_Win32_Alpha(m_pImg, nW, nH);
				UpdateSizeInfo();
			}
		}
		else
		{
			operator=(HiEasyX::ZoomImage_Win32_Alpha(this, nW, nH));
		}
	}

	void Canvas::ZoomImage_Gdiplus_Alpha(int nW, int nH)
	{
		if (m_bBindToImgPointer)
		{
			if (BeginWindowTask())
			{
				*m_pImg = HiEasyX::ZoomImage_Gdiplus_Alpha(m_pImg, nW, nH);
				UpdateSizeInfo();
			}
		}
		else
		{
			operator=(HiEasyX::ZoomImage_Win32_Alpha(this, nW, nH));
		}
	}

	void Canvas::GP_SetLineColor(COLORREF color)
	{
		m_cGPLineColor = color;
	}

	void Canvas::GP_SetFillColor(COLORREF color)
	{
		m_cGPFillColor = color;
	}

	void Canvas::GP_SetLineWidth(float width)
	{
		m_fGPLineWidth = width;
	}

	void Canvas::GP_EnableAlpha(bool enable)
	{
		m_bGPAlpha = enable;
	}

	void Canvas::GP_SetSmoothingMode(Gdiplus::SmoothingMode smoothing_mode)
	{
		m_enuSmoothingMode = smoothing_mode;
	}

	void Canvas::GP_Line(float x1, float y1, float x2, float y2, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_Line(x1, y1, x2, y2, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_Polygon(int points_num, POINT* points, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_Polygon(points_num, points, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_SolidPolygon(int points_num, POINT* points, bool isSetColor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetFillColor(fillcolor);
			EasyX_Gdiplus_SolidPolygon(points_num, points, m_cGPFillColor, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_FillPolygon(int points_num, POINT* points, bool isSetColor, COLORREF linecolor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)
			{
				GP_SetLineColor(linecolor);
				GP_SetFillColor(fillcolor);
			}
			EasyX_Gdiplus_FillPolygon(points_num, points, m_cGPLineColor, m_cGPFillColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_Rectangle(float x, float y, float w, float h, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_Rectangle(x, y, w, h, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_SolidRectangle(float x, float y, float w, float h, bool isSetColor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetFillColor(fillcolor);
			EasyX_Gdiplus_SolidRectangle(x, y, w, h, m_cGPFillColor, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_FillRectangle(float x, float y, float w, float h, bool isSetColor, COLORREF linecolor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)
			{
				GP_SetLineColor(linecolor);
				GP_SetFillColor(fillcolor);
			}
			EasyX_Gdiplus_FillRectangle(x, y, w, h, m_cGPLineColor, m_cGPFillColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_RoundRect(float x, float y, float w, float h, float ellipsewidth, float ellipseheight, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_RoundRect(x, y, w, h, ellipsewidth, ellipseheight, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_SolidRoundRect(float x, float y, float w, float h, float ellipsewidth, float ellipseheight, bool isSetColor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetFillColor(fillcolor);
			EasyX_Gdiplus_SolidRoundRect(x, y, w, h, ellipsewidth, ellipseheight, m_cGPFillColor, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_FillRoundRect(float x, float y, float w, float h, float ellipsewidth, float ellipseheight, bool isSetColor, COLORREF linecolor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)
			{
				GP_SetLineColor(linecolor);
				GP_SetFillColor(fillcolor);
			}
			EasyX_Gdiplus_FillRoundRect(x, y, w, h, ellipsewidth, ellipseheight, m_cGPLineColor, m_cGPFillColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_Ellipse(float x, float y, float w, float h, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_Ellipse(x, y, w, h, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_SolidEllipse(float x, float y, float w, float h, bool isSetColor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetFillColor(fillcolor);
			EasyX_Gdiplus_SolidEllipse(x, y, w, h, m_cGPFillColor, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_FillEllipse(float x, float y, float w, float h, bool isSetColor, COLORREF linecolor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)
			{
				GP_SetLineColor(linecolor);
				GP_SetFillColor(fillcolor);
			}
			EasyX_Gdiplus_FillEllipse(x, y, w, h, m_cGPLineColor, m_cGPFillColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_Pie(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_Pie(x, y, w, h, stangle, endangle, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_SolidPie(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetFillColor(fillcolor);
			EasyX_Gdiplus_SolidPie(x, y, w, h, stangle, endangle, m_cGPFillColor, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_FillPie(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor, COLORREF linecolor, COLORREF fillcolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)
			{
				GP_SetLineColor(linecolor);
				GP_SetFillColor(fillcolor);
			}
			EasyX_Gdiplus_FillPie(x, y, w, h, stangle, endangle, m_cGPLineColor, m_cGPFillColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void Canvas::GP_Arc(float x, float y, float w, float h, float stangle, float endangle, bool isSetColor, COLORREF linecolor)
	{
		if (BeginWindowTask())
		{
			if (isSetColor)	GP_SetLineColor(linecolor);
			EasyX_Gdiplus_Arc(x, y, w, h, stangle, endangle, m_cGPLineColor, m_fGPLineWidth, m_bGPAlpha, m_enuSmoothingMode, Pt());
			EndWindowTask();
		}
	}

	void ImageBlock::DeleteMyCanvas()
	{
		if (m_isCreated && m_pCanvas)
		{
			delete m_pCanvas;
			m_pCanvas = nullptr;
		}
	}

	ImageBlock::ImageBlock()
	{
	}

	ImageBlock::ImageBlock(Canvas* pCanvas)
	{
		SetCanvas(pCanvas);
	}

	ImageBlock::ImageBlock(int _x, int _y, Canvas* pCanvas)
	{
		x = _x;
		y = _y;
		SetCanvas(pCanvas);
	}

	ImageBlock::ImageBlock(int _x, int _y, int w, int h, COLORREF cBk)
	{
		x = _x;
		y = _y;
		CreateCanvas(w, h, cBk);
	}

	ImageBlock::~ImageBlock()
	{
		DeleteMyCanvas();
	}

	Canvas* ImageBlock::CreateCanvas(int w, int h, COLORREF cBk)
	{
		if (m_isCreated)
		{
			m_pCanvas->Resize(w, h);
			m_pCanvas->Clear(cBk);
		}
		else
		{
			m_pCanvas = new Canvas(w, h, cBk);
		}

		return m_pCanvas;
	}

	void ImageBlock::SetCanvas(Canvas* pCanvas)
	{
		DeleteMyCanvas();
		m_pCanvas = pCanvas;
	}

	void ImageBlock::SetPos(int _x, int _y)
	{
		x = _x;
		y = _y;
	}

	void ImageBlock::Render(IMAGE* pImg, BYTE _alpha)
	{
		if (m_pCanvas)
		{
			m_pCanvas->RenderTo(
				x, y,
				pImg,
				rctCrop,
				(BYTE)(alpha * (_alpha == 255 ? 1 : _alpha / 255.0f)),
				bUseSrcAlpha, isAlphaCalculated
			);
		}
	}

#ifdef UNICODE
	void Layer::Render(IMAGE* pImg, bool bShowOutline, bool bShowText, std::wstring wstrAddedText)
	{
		bool flagOutline = bOutline || bShowOutline;
		bool flagText = bText || bShowText;
		Canvas canvas;

		if (bVisible)
		{
			if (flagOutline)
			{
				m_property[0].SaveWorkingImageOnly();
				SetWorkingImage(pImg);
				m_property[1].SaveProperty();

				canvas.BindToImage(pImg);
				canvas.SetDefault();
				canvas.BeginBatchDrawing();

				canvas.SetBkColor(WHITE);
				canvas.SetTextStyle(16, 0, _T("Arial"));
			}

			wstrAddedText = L" " + wstrAddedText;

			size_t i = 0;
			for (auto& element : *this)
			{
				element->Render(pImg, alpha);

				// 绘制轮廓
				if (flagOutline)
				{
					RECT rctImg = {
						element->x,
						element->y,
						element->x + element->GetWidth(),
						element->y + element->GetHeight()
					};

					canvas.Rectangle(rctImg, true, BLACK);
					EXPAND_RECT(rctImg, 1);
					canvas.Rectangle(rctImg, true, WHITE);

					// 绘制文本
					if (flagText)
					{
						std::wstring wstrOriginText = L" Block[" + std::to_wstring(i) + L"]";
						std::wstring wstrAllText = wstrAddedText + wstrOriginText;

						int w = canvas.TextWidth(wstrAllText.c_str());
						int h = canvas.TextHeight(wstrAllText.c_str());

						RECT rctText = { rctImg.right - w - 1, rctImg.bottom + 1, rctImg.right - 1, rctImg.bottom + h + 1 };

						canvas.MoveTo(rctText.left, rctText.top);
						canvas.OutText(wstrAddedText.c_str(), true, GREEN);
						canvas.OutText(wstrOriginText.c_str(), true, BLACK);

						canvas.Rectangle(rctText, true, BLACK);
						EXPAND_RECT(rctText, 1);
						canvas.Rectangle(rctText, true, WHITE);
					}
				}

				i++;
			}

			if (flagOutline)
			{
				canvas.EndBatchDrawing();
				m_property[1].ApplyProperty();
				m_property[0].ApplyWorkingImageOnly();
			}
		}
	}
#else
	void Layer::Render(IMAGE* pImg, bool bShowOutline, bool bShowText, std::string strAddedText)
	{
		bool flagOutline = bOutline || bShowOutline;
		bool flagText = bText || bShowText;
		Canvas canvas;

		if (bVisible)
		{
			if (flagOutline)
			{
				m_property[0].SaveWorkingImageOnly();
				SetWorkingImage(pImg);
				m_property[1].SaveProperty();

				canvas.BindToImage(pImg);
				canvas.SetDefault();
				canvas.BeginBatchDrawing();

				canvas.SetBkColor(WHITE);
				canvas.SetTextStyle(16, 0, _T("Arial"));
			}

			strAddedText = " " + strAddedText;

			size_t i = 0;
			for (auto& element : *this)
			{
				element->Render(pImg, alpha);

				// 绘制轮廓
				if (flagOutline)
				{
					RECT rctImg = {
						element->x,
						element->y,
						element->x + element->GetWidth(),
						element->y + element->GetHeight()
					};

					canvas.Rectangle(rctImg, true, BLACK);
					EXPAND_RECT(rctImg, 1);
					canvas.Rectangle(rctImg, true, WHITE);

					// 绘制文本
					if (flagText)
					{
						std::string strOriginText = " Block[" + std::to_string(i) + "]";
						std::string strAllText = strAddedText + strOriginText;

						int w = canvas.TextWidth(strAllText.c_str());
						int h = canvas.TextHeight(strAllText.c_str());

						RECT rctText = { rctImg.right - w - 1, rctImg.bottom + 1, rctImg.right - 1, rctImg.bottom + h + 1 };

						canvas.MoveTo(rctText.left, rctText.top);
						canvas.OutText(strAddedText.c_str(), true, GREEN);
						canvas.OutText(strOriginText.c_str(), true, BLACK);

						canvas.Rectangle(rctText, true, BLACK);
						EXPAND_RECT(rctText, 1);
						canvas.Rectangle(rctText, true, WHITE);
					}
				}

				i++;
			}

			if (flagOutline)
			{
				canvas.EndBatchDrawing();
				m_property[1].ApplyProperty();
				m_property[0].ApplyWorkingImageOnly();
			}
		}
	}
#endif

	std::vector<Layer*> Scene::GetAllLayer()
	{
		std::vector<Layer*> vecLayer;
		vecLayer.push_back(&m_layerBottomMost);
		vecLayer.push_back(&m_layerBottom);
		vecLayer.insert(vecLayer.end(), this->begin(), this->end());
		vecLayer.push_back(&m_layerTop);
		vecLayer.push_back(&m_layerTopMost);
		return vecLayer;
	}

	size_t Scene::GetAllLayerSize() const
	{
		// 普通图层加 4 个特殊图层
		return this->size() + 4;
	}

	Layer* Scene::GetSpecialLayer(LayerOrder order)
	{
		static Layer layerNull;
		layerNull = {};
		switch (order)
		{
		case LAYERORDER_BOTTOM_MOST:	return &m_layerBottomMost;	break;
		case LAYERORDER_BOTTOM:			return &m_layerBottom;		break;
		case LAYERORDER_TOP:			return &m_layerTop;			break;
		case LAYERORDER_TOP_MOST:		return &m_layerTopMost;		break;
		default:						return &layerNull;			break;
		}
	}

	void Scene::Render(IMAGE* pImg, bool bShowAllOutline, bool bShowAllText)
	{
		m_property.SaveWorkingImageOnly();
		SetWorkingImage(pImg);
		cleardevice();
		m_property.ApplyWorkingImageOnly();

		size_t i = 0;
		for (auto& layer : GetAllLayer())
		{
#ifdef UNICODE
			layer->Render(pImg, bShowAllOutline, bShowAllText, L"Layer[" + std::to_wstring(i) + L"]");
#else
			layer->Render(pImg, bShowAllOutline, bShowAllText, "Layer[" + std::to_string(i) + "]");
#endif

			i++;
		}
	}
}
