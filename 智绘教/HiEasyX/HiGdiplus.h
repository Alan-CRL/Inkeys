/**
 * @file	HiGdiPlus.h
 * @brief	HiEasyX 库的 GDI+ 绘图模块
 * @author	huidong
*/

#pragma once

#include <graphics.h>
#include <gdiplus.h>

namespace HiEasyX
{
	/////// GDI+ 环境配置 ///////

	//
	//	备注：
	//		调用下面的绘图函数时，会自动尝试启动 GDI+。
	//
	//		创建 HiWindow 绘图窗口时也会自动启动 GDI+，最后一个绘图窗口关闭时，GDI+ 会自动关闭。
	//		如果没有创建绘图窗口，则需要手动关闭 GDI+。
	//

	/**
	 * @brief 启动 GDI+，如果已经启动则直接返回
	*/
	void Gdiplus_Try_Starup();

	/**
	 * @brief 关闭 GDI+
	*/
	void Gdiplus_Shutdown();

	/////// GDI+ 基础封装 ///////

	/**
	 * @brief 画线
	*/
	void Gdiplus_Line(
		HDC hdc,
		float x1,
		float y1,
		float x2,
		float y2,
		Gdiplus::Color linecolor,
		float linewidth,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画多边形
	*/
	void Gdiplus_Polygon(
		HDC hdc,
		int points_num,
		Gdiplus::PointF* points,
		Gdiplus::Color linecolor,
		float linewidth,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画无边框填充多边形
	*/
	void Gdiplus_SolidPolygon(
		HDC hdc,
		int points_num,
		Gdiplus::PointF* points,
		Gdiplus::Color fillcolor,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画矩形
	*/
	void Gdiplus_Rectangle(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		Gdiplus::Color linecolor,
		float linewidth,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画无边框填充矩形
	*/
	void Gdiplus_SolidRectangle(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		Gdiplus::Color fillcolor,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画椭圆
	*/
	void Gdiplus_Ellipse(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		Gdiplus::Color linecolor,
		float linewidth,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画无边框填充椭圆
	*/
	void Gdiplus_SolidEllipse(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		Gdiplus::Color fillcolor,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画饼状图（传入顺时针角度）
	*/
	void Gdiplus_Pie(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float sweepangle,
		Gdiplus::Color linecolor,
		float linewidth,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画无边框填充饼状图（传入顺时针角度）
	*/
	void Gdiplus_SolidPie(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float sweepangle,
		Gdiplus::Color fillcolor,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/**
	 * @brief 画圆弧（传入顺时针角度）
	*/
	void Gdiplus_Arc(
		HDC hdc,
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float sweepangle,
		Gdiplus::Color linecolor,
		float linewidth,
		Gdiplus::SmoothingMode smoothing_mode
	);

	/////// EasyX 风格的 GDI+ 封装 ///////

	////////////////////////////////////////////////////////////////
	//
	// 注：以下 EasyX 风格接口中
	//
	//		enable_alpha	表示是否使用传入颜色的 alpha 值
	//		enable_aa		表示是否开启抗锯齿
	//		pImg			表示目标绘制画布
	//
	////////////////////////////////////////////////////////////////

	/**
	 * @brief 转换 COLORREF 到 Gdiplus::Color
	 * @param[in] color				原颜色
	 * @param[in] reserve_alpha		是否保留 COLORREF 中的 alpha 值
	 * @return 转换后的 Gdiplus::Color 色值
	*/
	Gdiplus::Color ConvertToGdiplusColor(COLORREF color, bool reserve_alpha = false);

	/**
	 * @brief 画直线
	*/
	void EasyX_Gdiplus_Line(
		float x1,
		float y1,
		float x2,
		float y2,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画多边形
	*/
	void EasyX_Gdiplus_Polygon(
		int points_num,
		POINT* points,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画无边框填充多边形
	*/
	void EasyX_Gdiplus_SolidPolygon(
		int points_num,
		POINT* points,
		COLORREF fillcolor,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画有边框填充多边形
	*/
	void EasyX_Gdiplus_FillPolygon(
		int points_num,
		POINT* points,
		COLORREF linecolor,
		COLORREF fillcolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画矩形
	*/
	void EasyX_Gdiplus_Rectangle(
		float x,
		float y,
		float w,
		float h,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画无边框填充矩形
	*/
	void EasyX_Gdiplus_SolidRectangle(
		float x,
		float y,
		float w,
		float h,
		COLORREF fillcolor,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画有边框填充矩形
	*/
	void EasyX_Gdiplus_FillRectangle(
		float x,
		float y,
		float w,
		float h,
		COLORREF linecolor,
		COLORREF fillcolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画圆角矩形
	*/
	void EasyX_Gdiplus_RoundRect(
		float x,
		float y,
		float w,
		float h,
		float ellipsewidth,
		float ellipseheight,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画无边框填充圆角矩形
	*/
	void EasyX_Gdiplus_SolidRoundRect(
		float x,
		float y,
		float w,
		float h,
		float ellipsewidth,
		float ellipseheight,
		COLORREF fillcolor,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画有边框填充圆角矩形
	*/
	void EasyX_Gdiplus_FillRoundRect(
		float x,
		float y,
		float w,
		float h,
		float ellipsewidth,
		float ellipseheight,
		COLORREF linecolor,
		COLORREF fillcolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画椭圆
	*/
	void EasyX_Gdiplus_Ellipse(
		float x,
		float y,
		float w,
		float h,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画无边框填充椭圆
	*/
	void EasyX_Gdiplus_SolidEllipse(
		float x,
		float y,
		float w,
		float h,
		COLORREF fillcolor,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画有边框填充椭圆
	*/
	void EasyX_Gdiplus_FillEllipse(
		float x,
		float y,
		float w,
		float h,
		COLORREF linecolor,
		COLORREF fillcolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画饼状图（传入逆时针角度）
	*/
	void EasyX_Gdiplus_Pie(
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float endangle,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画无边框填充饼状图（传入逆时针角度）
	*/
	void EasyX_Gdiplus_SolidPie(
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float endangle,
		COLORREF fillcolor,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画有边框填充饼状图（传入逆时针角度）
	*/
	void EasyX_Gdiplus_FillPie(
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float endangle,
		COLORREF linecolor,
		COLORREF fillcolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 画圆弧（传入逆时针角度）
	*/
	void EasyX_Gdiplus_Arc(
		float x,
		float y,
		float w,
		float h,
		float stangle,
		float endangle,
		COLORREF linecolor,
		float linewidth = 1,
		bool enable_alpha = false,
		Gdiplus::SmoothingMode smoothing_mode = Gdiplus::SmoothingModeHighQuality,
		IMAGE* pImg = nullptr
	);

	/**
	 * @brief 将 RECT 转换为 RectF
	*/
	Gdiplus::RectF RECTToRectF(RECT x);

	/**
	 * @brief 将 RectF 转换为 RECT
	*/
	RECT RectFToRECT(Gdiplus::RectF x);
};
