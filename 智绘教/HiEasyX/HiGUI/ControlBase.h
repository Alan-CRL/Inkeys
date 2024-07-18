/**
 * @file	ControlBase.h
 * @brief	HiGUI 控件分支：控件基础
 * @author	huidong
*/

#pragma once

#include "../HiContainer.h"

#include "../HiMacro.h"
#include "../HiFunc.h"
#include "../HiCanvas.h"

namespace HiEasyX
{
	/**
	 * @brief 控件基础
	*/
	enum CtrlMessage
	{
		CM_OTHER,						///< 未特殊标识的其它消息
		CM_HOVER,						///< 悬停
		CM_HOVER_OVER,					///< 悬停结束
		CM_PRESS,						///< 按下
		CM_PRESS_OVER,					///< 按下结束
		CM_CLICK,						///< 单击
		CM_DOUBLE_CLICK,				///< 双击
		CM_FOCUS,						///< 获取焦点
		CM_FOCUS_OVER,					///< 丢失焦点

	};

	class ControlBase;

	/**
	 * @brief 控件消息处理函数
	 * @param[in] _Ctrl	传入控件指针
	 * @param[in] _MsgId	传入消息标识代码
	 * @param[in] _ExMsg	传入消息（鼠标坐标已变换到控件）
	*/
	typedef void (*MESSAGE_PROC_FUNC)(ControlBase* _Ctrl, int _MsgId, ExMessage _ExMsg);

	/**
	 * @brief 支持静态类函数作为控件消息处理函数
	*/
	typedef void (*MESSAGE_PROC_FUNC_CLASS)(void* _This, ControlBase* _Ctrl, int _MsgId, ExMessage _ExMsg);

	/**
	 * @brief 控件基础
	*/
	class ControlBase : public Container
	{
	protected:

		bool m_bEnabled = true;										///< 是否可用
		bool m_bVisible = true;										///< 是否可见

		// 重绘和渲染标志
		bool m_bAutoRedrawWhenReceiveMsg = true;					///< 当默认消息处理函数接受到消息时，是否自动标识重绘和渲染
		bool m_bRedraw = true;										///< 标识需要重绘
		bool m_bRender = true;										///< 标识需要渲染
		bool m_bClear = false;										///< 标识需要清空某区域
		RECT m_rctClear = { 0 };									///< 记录需要清空的区域
		bool m_bAlwaysRedrawAndRender = false;						///< 总是重绘和渲染（占用更高）

#ifdef UNICODE
		std::wstring m_wstrText;									///< 控件文本
#else
		std::string m_strText;									///< 控件文本
#endif

		Canvas m_canvas;											///< 画布
		BYTE m_alpha = 255;											///< 透明度
		bool m_bUseCanvasAlpha = false;								///< 是否使用画布自身的透明度信息
		bool m_isAlphaCalculated = false;							///< 画布是否已经计算透明混合颜色

		COLORREF m_cBorder = MODERN_BORDER_GRAY;					///< 边框颜色
		COLORREF m_cBackground = CLASSICGRAY;						///< 背景色
		COLORREF m_cText = BLACK;									///< 文本颜色

		bool m_bEnableBorder = true;								///< 是否绘制边框
		int m_nBorderThickness = 1;									///< 边框粗细

		bool m_bCompleteFirstSetRect = false;						///< 是否已经完成第一次设置区域

		ControlBase* m_pParent = nullptr;							///< 父控件
		std::list<ControlBase*> m_listChild;						///< 子控件

		bool m_bAutoSizeForChild = false;							///< 为子控件自动改变大小以容纳控件

		MESSAGE_PROC_FUNC m_funcMessageProc = nullptr;				///< 消息处理函数
		MESSAGE_PROC_FUNC_CLASS m_funcMessageProc_Class = nullptr;	///< 若绑定的消息处理函数是静态类函数，则记录其地址
		void* m_pCalledClass = nullptr;								///< 若绑定的消息处理函数是静态类函数，则记录该类指针

		bool m_bHovered = false;									///< 鼠标是否悬停
		bool m_bPressed = false;									///< 鼠标是否按下
		bool m_bFocused = false;									///< 是否拥有焦点

		/**
		 * @brief 更新区域消息处理
		 * @param[in] rctOld 旧区域
		*/
		void UpdateRect(RECT rctOld) override;

		/**
		 * @brief 标记需要重绘和渲染
		*/
		void MarkNeedRedrawAndRender();

		/**
		 * @brief 标记需要清空矩形区域
		 * @param[in] rct 需要清空的区域
		*/
		void MarkNeedClearRect(RECT rct);

		/**
		 * @brief 绘制子控件
		*/
		virtual void DrawChild();

		/**
		 * @brief 转换消息
		 * @param[in, out] msg 要转换的消息
		 * @return 转换后的消息
		*/
		virtual ExMessage& TransformMessage(ExMessage& msg);

		/**
		 * @brief 分发消息到用户函数
		 * @param[in] msgid	消息 ID
		 * @param[in] msg		消息内容
		*/
		virtual void CallUserMsgProcFunc(int msgid, ExMessage msg);

		/**
		 * @brief 子控件区域变更
		 * @param[in] pChild 区域变更的子控件
		*/
		virtual void ChildRectChanged(ControlBase* pChild);

	private:

		void Init();

	public:

		ControlBase();

#ifdef UNICODE
		ControlBase(std::wstring wstrText);
		ControlBase(int x, int y, int w = 0, int h = 0, std::wstring wstrText = L"");
#else
		ControlBase(std::string strText);
		ControlBase(int x, int y, int w = 0, int h = 0, std::string strText = "");
#endif

		virtual ~ControlBase();

		ControlBase* GetParent() { return m_pParent; }

		/**
		 * @brief 设置父控件（父控件调用 AddChild）
		 * @param[in] p 父控件
		*/
		virtual void SetParent(ControlBase* p);

		virtual bool IsAutoSizeForChild() const { return m_bAutoSizeForChild; }

		/**
		 * @brief 为子控件自动改变大小以容纳控件（不容纳负坐标部分）
		 * @param[in] enable 是否启用
		*/
		virtual void EnableAutoSizeForChild(bool enable);

		std::list<ControlBase*>& GetChildList();

		/**
		 * @brief 获取子控件总数
		*/
		size_t GetChildCount();

		virtual void AddChild(ControlBase* p, int offset_x = 0, int offset_y = 0);

		virtual void RemoveChild(ControlBase* p);

		virtual bool IsEnabled() const { return m_bEnabled; }

		virtual void SetEnable(bool enable);

		virtual bool IsVisible() const { return m_bVisible; }

		virtual void SetVisible(bool visible);

		virtual bool GetAutoRedrawState() const { return m_bAutoRedrawWhenReceiveMsg; }

		/**
		 * @brief 启用自动重绘（接受到基础消息事件时自动标识需要重绘）
		 * @param[in] enable 是否启用
		*/
		virtual void EnableAutoRedraw(bool enable);

		virtual Canvas& GetCanvas() { return m_canvas; }

		virtual COLORREF GetBkColor() const { return m_cBackground; }

		virtual void SetBkColor(COLORREF color);

		virtual COLORREF GetTextColor() const { return m_cText; }

		virtual void SetTextColor(COLORREF color);

		virtual void EnableBorder(bool bEnableBorder, COLORREF color = BLACK, int thickness = 1);

		virtual void SetAlpha(BYTE alpha, bool bUseCanvasAlpha, bool isAlphaCalculated);

#ifdef UNICODE
		virtual std::wstring GetText() const { return m_wstrText; }
		virtual void SetText(std::wstring wstr);
#else
		virtual std::string GetText() const { return m_strText; }
		virtual void SetText(std::string str);
#endif

		virtual void Draw_Text(int nTextOffsetX = 0, int nTextOffsetY = 0);

		/**
		 * @brief 重绘控件
		*/
		virtual void Redraw();

		/**
		 * @brief 绘制控件
		 * @param[in] draw_child 是否绘制子控件
		*/
		virtual void Draw(bool draw_child = true);

		/**
		 * @brief 渲染控件到外部
		 * @param[in] dst			渲染目标
		 * @param[in] pRct			内部使用，传入父控件渲染区域数组
		 * @param[in, out] pCount	内部使用，传入父控件渲染区域数量指针
		*/
		virtual void Render(Canvas* dst, RECT* pRct = nullptr, int* pCount = 0);

		/**
		 * @brief 设置消息响应函数
		 * @param[in] func 消息响应函数
		*/
		virtual void SetMsgProcFunc(MESSAGE_PROC_FUNC func);

		/**
		 * @brief 设置消息响应函数为静态类函数
		 * @param[in] static_class_func		消息响应函数（静态类函数）
		 * @param[in] _this					类指针
		*/
		virtual void SetMsgProcFunc(MESSAGE_PROC_FUNC_CLASS static_class_func, void* _this);

		/**
		 * @brief 更新消息
		 * @param[in] msg 新消息
		*/
		virtual void UpdateMessage(ExMessage msg);

		/**
		 * @brief 判断鼠标是否悬停
		*/
		virtual bool IsHovered() const { return m_bHovered; }

		/**
		 * @brief 判断是否拥有焦点
		*/
		virtual bool IsFocused() const { return m_bFocused; }

		/**
		 * @brief 判断是否按下
		*/
		virtual bool IsPressed() const { return m_bPressed; }
	};
}

