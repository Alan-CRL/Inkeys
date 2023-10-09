/**
 * @file	Button.h
 * @brief	HiGUI 控件分支：按钮控件
 * @author	huidong
*/

#pragma once

#include "ControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 按钮控件
	*/
	class Button : public ControlBase
	{
	protected:

		bool m_bEnableClassicStyle = false;	///< 是否使用经典样式

		virtual void InitColor();

	public:

		// 按钮颜色设置（现代样式）
		COLORREF m_cBorder_Normal = MODERN_BORDER_GRAY;
		COLORREF m_cBackground_Normal = MODERN_FILL_GRAY;
		COLORREF m_cBorder_Hovered = MODERN_BORDER_BLUE;
		COLORREF m_cBackground_Hovered = MODERN_FILL_BLUE;
		COLORREF m_cBorder_Pressed = MODERN_BORDER_PRESSED_BLUE;
		COLORREF m_cBackground_Pressed = MODERN_FILL_PRESSED_BLUE;

		COLORREF m_cText_Disabled = LIGHTGRAY;
		COLORREF m_cBorder_Disabled = GRAY;
		COLORREF m_cBackground_Disabled = GRAY;

		COLORREF m_cClassicNormalBorder3D = GRAY;			///< 未按下时的 3D 边框颜色（经典样式）
		COLORREF m_cClassicPressedBorder3D = LIGHTGRAY;		///< 按下时的 3D 边框颜色（经典样式）

		Button();

#ifdef UNICODE
		Button(int x, int y, int w, int h, std::wstring wstrText = L"");
#else
		Button(int x, int y, int w, int h, std::string strText = "");
#endif

		/**
		 * @brief 启用控件
		 * @param[in] enable 是否启用
		*/
		void SetEnable(bool enable) override;

		/**
		 * @brief 是否启用经典样式
		 * @param[in] enable 是否启用
		*/
		virtual void EnableClassicStyle(bool enable);
		
		/**
		 * @brief 更新消息
		 * @param[in] msg 消息
		*/
		void UpdateMessage(ExMessage msg) override;

		/**
		 * @brief 绘制
		 * @param[in] draw_child 是否绘制子控件
		*/
		void Draw(bool draw_child = true) override;
	};
}

