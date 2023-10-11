/**
 * @file	ScrollBar.h
 * @brief	HiGUI 控件分支：滚动条控件
 * @author	huidong
*/

#pragma once

#include "Button.h"

#include "../HiMouseDrag.h"

#include <time.h>

namespace HiEasyX
{
	/**
	 * @brief 滚动条控件
	*/
	class ScrollBar : public ControlBase
	{
	public:

		/**
		 * @brief 滚动条信息
		*/
		struct ScrollBarInfo
		{
			int btnW, btnH;			///< 普通按钮尺寸
			int slider_free_len;	///< 滑块可以占据的像素长度
			int slider_move_len;	///< 滑块顶点可以移动的像素长度
			int slider_len;			///< 滑块像素长度
		};

	protected:

		int m_nBtnHeight = 20;				///< 按钮高度（竖直放置时有效）
		int m_nBtnWidth = 20;				///< 按钮宽度（水平放置时有效）

		bool m_bHorizontal = false;			///< 是否水平放置

		int m_nDrawInterval = 6;			///< 绘制间隙

		// 按钮
		Button m_btnTop;
		Button m_btnUp;
		Button m_btnDown;
		Button m_btnBottom;
		Button m_btnDrag;
		MouseDrag m_MouseDrag;
		bool m_bDragging = false;			///< 是否正在拖动
		int m_nSliderSpeed = 20;			///< 按住按钮时滑块每秒运行的内容长度
		clock_t m_tPressed = 0;				///< 按钮按住计时

		float m_fPos = 0;					///< 滑块内容位置
		int m_nLen = 0;						///< 内容总长度
		float m_fPosRatio = 0;				///< 滑块内容位置比例

		ScrollBarInfo m_info = {};			///< 滚动条信息

		int m_nViewLen = 10;				///< 视野内容长度
		float m_fViewRatio = 1;				///< 视野范围占总长度的比

		bool m_bSliderPosChanged = false;	///< 标记滑块位置改变

		RECT m_rctOnWheel = { 0 };			///< 响应滚轮消息的区域（客户区坐标）
		bool m_bSetOnWheelRct = false;		///< 是否设置了滚轮消息的响应区域

		/**
		 * @brief 初始化
		*/
		virtual void Init();

		/**
		 * @brief 响应特殊按钮消息（Top 和 Bottom）
		 * @param[in] pThis	类指针
		 * @param[in] pCtrl	控件
		 * @param[in] msgid	消息 ID
		 * @param[in] msg	消息内容
		*/
		static void OnSpecialButtonMsg(void* pThis, ControlBase* pCtrl, int msgid, ExMessage msg);

		/**
		 * @brief 响应普通按钮消息（Up 和 Down）
		*/
		virtual void OnNormalButtonMsg();

		/**
		 * @brief 更新滑块位置区域
		*/
		virtual void UpdateSliderRect();

		/**
		 * @brief 更新位置比例
		*/
		void UpdatePosRatio();

		/**
		 * @brief 更新视野比例
		*/
		void UpdateViewRatio();

		/**
		 * @brief 更新滚动条信息
		*/
		void UpdateScrollBarInfo();

	public:

		ScrollBar();

		ScrollBar(int x, int y, int w, int h, int len, int pos, bool bHorizontal = false);

		int GetButtonHeight() const { return m_nBtnHeight; }

		/**
		 * @brief 设置按钮高度（竖直放置时生效）
		 * @param[in] h 高度
		*/
		void SetButtonHeight(int h);

		int GetButtonWidth() const { return m_nBtnWidth; }

		/**
		 * @brief 设置按钮宽度（水平放置时生效）
		 * @param[in] w 宽度
		*/
		void SetButtonWidth(int w);

		/**
		 * @brief 判断是否正在拖动
		*/
		bool IsDragging() const { return m_bDragging; }

		/**
		 * @brief 获取滑块像素长度
		*/
		int GetSliderLength() const { return m_info.slider_len; }

		/**
		 * @brief 获取滑块内容位置
		*/
		int GetSliderContentPos() const { return (int)m_fPos; }

		/**
		 * @brief 设置滑块内容位置
		 * @param[in] pos 滑块位置
		*/
		void SetSliderContentPos(float pos);

		/**
		 * @brief 相对移动滑块的内容位置
		 * @param[in] d 滑块位置
		*/
		void MoveSlider(float d);

		/**
		 * @brief 获取滚动条内容长度
		*/
		int GetContentLength() const { return m_nLen; }

		/**
		 * @brief 设置滚动条内容长度
		 * @param[in] len 内容长度
		*/
		void SetContentLength(int len);

		int GetViewLength() const { return m_nViewLen; }

		/**
		 * @brief 设置视野内容长度
		 * @param[in] len 视野内容长度
		*/
		void SetViewLength(int len);

		int GetSliderSpeed() const { return m_nSliderSpeed; }

		/**
		 * @brief 设置按下按钮时滑块的运行速度
		 * @param[in] speed 运行速度（每秒经过的内容长度）
		*/
		void SetSliderSpeed(int speed);

		bool IsHorizontal() const { return m_bHorizontal; }

		/**
		 * @brief 启用水平放置
		 * @param[in] enable 是否启用
		*/
		void EnableHorizontal(bool enable);

		/**
		 * @brief 判断滑块位置是否改变
		*/
		bool IsSliderPosChanged();

		/**
		 * @brief 获取响应滚轮消息的区域（未自定义时返回空区域）
		*/
		RECT GetOnWheelRect() const { return m_rctOnWheel; }

		/**
		 * @brief 设置响应滚轮消息的区域（客户区坐标）
		 * @param[in] rct 消息响应区域
		*/
		void SetOnWheelRect(RECT rct);

		void UpdateRect(RECT rctOld) override;

		void UpdateMessage(ExMessage msg) override;

		void Draw(bool draw_child = true) override;

	};
}

