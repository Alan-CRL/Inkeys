/**
 * @file	Static.h
 * @brief	HiGUI 控件分支：静态控件
 * @author	huidong
*/

#pragma once

#include "ControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 静态（文本、图像）控件
	*/
	class Static : public ControlBase
	{
	public:

		struct Char
		{
			TCHAR ch;
			COLORREF cText;
			COLORREF cBk;
		};

	protected:

		std::vector<Char> m_vecText;
		ImageBlock* m_pImgBlock = nullptr;

		virtual void Init();

	public:

		Static();

#ifdef UNICODE
		Static(int x, int y, int w, int h, std::wstring wstrText = L"");
#else
		Static(int x, int y, int w, int h, std::string strText = "");
#endif

#ifdef UNICODE
		virtual std::vector<Char> Convert(std::wstring wstrText);
#else
		virtual std::vector<Char> Convert(std::string strText);
#endif

#ifdef UNICODE
		virtual std::wstring Convert(std::vector<Char> vecText);
#else
		virtual std::string Convert(std::vector<Char> vecText);
#endif

		virtual std::vector<Char>& GetTextVector() { return m_vecText; }

		virtual void ClearText();

		/**
		 * @brief 添加文本
		 * @param[in] wstr				文本
		 * @param[in] isSetTextColor	是否设置此文本颜色
		 * @param[in] cText				文本颜色
		 * @param[in] isSetBkColor		是否设置此文本背景颜色
		 * @param[in] cBk				文本背景色
		*/
#ifdef UNICODE
		virtual void AddText(
			std::wstring wstr,
			bool isSetTextColor = false,
			COLORREF cText = 0,
			bool isSetBkColor = false,
			COLORREF cBk = 0
			);
#else
		virtual void AddText(
			std::string str,
			bool isSetTextColor = false,
			COLORREF cText = 0,
			bool isSetBkColor = false,
			COLORREF cBk = 0
			);
#endif

#ifdef UNICODE
		void SetText(std::wstring wstrText) override;
#else
		void SetText(std::string strText) override;
#endif

		void SetText(std::vector<Char> vecText);

		/**
		 * @brief 获取缓存的图像
		*/
		virtual ImageBlock* GetImage() { return m_pImgBlock; }

		/**
		 * @brief 设置显示图像
		 * @param[in] pImgBlockmg 要显示的图像块
		*/
		virtual void SetImage(ImageBlock* pImgBlockmg);

		void Draw_Text(int nTextOffsetX = 0, int nTextOffsetY = 0) override;

		void Draw(bool draw_child = true) override;
	};
}

