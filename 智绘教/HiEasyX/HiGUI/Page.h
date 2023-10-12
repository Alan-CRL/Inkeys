/**
 * @file	Page.h
 * @brief	HiGUI 控件分支：页控件
 * @author	huidong
*/

#pragma once

#include "ControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 页控件
	*/
	class Page : public ControlBase
	{
	protected:

		Canvas* m_pCanvas = nullptr;

		virtual void Init(COLORREF cBk = WHITE);

	public:

		Page();

		Page(int w, int h, COLORREF cBk = WHITE);

		Page(Canvas* pCanvas);

		/**
		 * @brief 绑定到画布，渲染时默认输出到此画布
		 * @param[in] pCanvas 画布
		*/
		virtual void BindToCanvas(Canvas* pCanvas);

		/**
		 * @brief 加入控件
		 * @param[in] pCtrl		控件
		 * @param[in] offset_x	坐标偏移
		 * @param[in] offset_y	坐标偏移
		*/
		virtual void push(ControlBase* pCtrl, int offset_x = 0, int offset_y = 0);

		virtual void push(const std::list<ControlBase*> list);

		/**
		 * @brief 移除控件
		 * @param[in] pCtrl 控件
		*/
		virtual void remove(ControlBase* pCtrl);

		/**
		 * @brief 渲染
		 * @param[in] dst		载体画布（为空则输出到已绑定画布）
		 * @param[in] pRct		内部使用
		 * @param[in] pCount	内部使用
		*/
		void Render(Canvas* dst = nullptr, RECT* pRct = nullptr, int* pCount = 0) override;

		/**
		 * @brief 更新控件，并输出到某画布（控件重绘并渲染）
		 * @param[in] pCanvas 载体画布（为空则输出到已绑定画布）
		*/
		void UpdateImage(Canvas* pCanvas = nullptr);
	};
}

