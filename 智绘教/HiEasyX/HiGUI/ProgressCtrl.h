/**
 * @file	ProgressCtrl.h
 * @brief	HiGUI 控件分支：进度条控件
 * @author	huidong
*/

#pragma once

#include "ControlBase.h"

#include <time.h>

namespace HiEasyX
{
	/**
	 * @brief 进度条控件
	*/
	class ProgressCtrl : public ControlBase
	{
	protected:

		bool m_bEnableAnimation = true;					///< 是否启用动画效果

		int m_nLightPos = 0;							///< 光源位置
		clock_t m_tClock = 0;							///< 保留上一次动画的绘制时间
		float m_fSpeedRatio = 0.4f;						///< 动画速度（每秒经过总长度的多少）

		float m_fLenRatio = 0.8f;						///< 动画光亮效果长度占比

		float m_fBarColorLightness = 0.35f;				///< 进度条颜色亮度
		float m_fBarLightLightness = 0.41f;				///< 进度条光源亮度

		COLORREF m_cBar;								///< 进度条颜色（不用于亮度采样）

		float m_fH = 0;									///< 色相信息
		float m_fS = 0;									///< 饱和度信息

		int m_nPos = 0;									///< 进度条进度
		int m_nLen = 100;								///< 进度总长度

		virtual void Init();

	public:

		ProgressCtrl();

		ProgressCtrl(int x, int y, int w, int h, int len);

		/**
		 * @brief 获取内容长度
		*/
		virtual int GetContentLength() const { return m_nLen; }

		/**
		 * @brief 设置内容长度
		 * @param[in] len 内容长度
		*/
		virtual void SetContentLength(int len);

		/**
		 * @brief 获取进度（内容长度即为总进度）
		*/
		virtual int GetProcess() const { return m_nPos; }

		/**
		 * @brief 设置进度（内容长度即为总进度）
		 * @param[in] pos 进度
		*/
		virtual void SetProcess(int pos);

		/**
		 * @brief 进度加一
		*/
		virtual void Step();

		/**
		 * @brief 获取进度条颜色
		*/
		virtual COLORREF GetBarColor() const { return m_cBar; }

		/**
		 * @brief 设置进度条颜色
		 * @param[in] cBar 进度条颜色
		*/
		virtual void SetBarColor(COLORREF cBar);

		/**
		 * @brief 获取动画启用状态
		*/
		virtual bool GetAnimationState() const { return m_bEnableAnimation; }

		/**
		 * @brief 设置是否启用动画
		 * @param[in] enable 是否启用
		*/
		virtual void EnableAnimation(bool enable);

		void Draw(bool draw_child = true) override;
	};
}

