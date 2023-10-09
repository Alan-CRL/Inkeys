/**
 * @file	Container.h
 * @brief	HiEasyX 库的基础容器
 * @author	huidong
*/

#pragma once

#include <Windows.h>

namespace HiEasyX
{
	/**
	 * @brief 基础容器
	*/
	class Container
	{
	protected:

		RECT m_rct = { 0 };					///< 容器区域

	public:

		Container();

		virtual ~Container();

		/**
		 * @brief 响应更新区域消息
		 * @param[in] rctOld 旧的区域
		*/
		virtual void UpdateRect(RECT rctOld);

		RECT GetRect() const { return m_rct; }

		/**
		 * @brief 设置位置和宽高
		 * @param[in] x	位置
		 * @param[in] y	位置
		 * @param[in] w	宽
		 * @param[in] h	高
		*/
		void SetRect(int x, int y, int w, int h);

		/**
		 * @brief 设置矩形区域
		 * @param[in] rct 区域
		*/
		void SetRect(RECT rct);

		POINT GetPos() const { return { m_rct.left,m_rct.top }; }

		int GetX() const { return m_rct.left; }

		int GetY() const { return m_rct.top; }

		void SetPos(int x, int y);

		void SetPos(POINT pt);

		void Move(int x, int y) { SetPos(x, y); }

		void MoveRel(int dx, int dy);

		int GetWidth() const { return m_rct.right - m_rct.left; }

		void SetWidth(int w);

		int GetHeight() const { return m_rct.bottom - m_rct.top; };

		void SetHeight(int h);

		void Resize(int w, int h);
	};
}

