/**
 * @file	HiDrawingProperty.h
 * @brief	HiEasyX 库的绘图属性存储模块
 * @author	huidong
*/

#pragma once

#include <graphics.h>

namespace HiEasyX
{
	/**
	 * @brief <pre>
	 *		绘图属性总控单元
	 *
	 *	备注：
	 *		EasyX 目前无法获取 setorigin 和 setcliprgn 所设置的值
	 * </pre>
	*/
	class DrawingProperty
	{
	private:
		bool m_isSaved = false;

	public:
		IMAGE* m_pImg;
		float m_xasp, m_yasp;
		COLORREF m_cBk;
		int m_nBkMode;
		COLORREF m_cFill;
		FILLSTYLE m_fillstyle;
		COLORREF m_cLine;
		LINESTYLE m_linestyle;
		int m_nPolyFillMode;
		int m_nRop2Mode;
		COLORREF m_cText;
		LOGFONT m_font;

		/**
		 * @brief 保存当前所有的绘图属性
		*/
		void SaveProperty();

		/**
		 * @brief 只保存当前绘图对象
		*/
		void SaveWorkingImageOnly();

		/**
		 * @brief 应用保存的所有绘图属性
		*/
		void ApplyProperty();

		/**
		 * @brief 只恢复绘图对象
		*/
		void ApplyWorkingImageOnly();

		/**
		 * @brief 判断是否保存了绘图属性
		*/
		bool IsSaved();

		/**
		 * @brief 重置保存状态
		*/
		void Reset();
	};

};
