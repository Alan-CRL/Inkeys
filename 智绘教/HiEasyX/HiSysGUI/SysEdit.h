/**
 * @file	SysEdit.h
 * @brief	HiSysGUI 控件分支：输入框
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

#include <graphics.h>

namespace HiEasyX
{
	/**
	 * @brief 系统输入框控件
	*/
	class SysEdit : public SysControlBase
	{
	private:

		/**
		 * @brief 编辑框属性
		*/
		struct Property
		{
			bool left_align = true;			///< 文字左对齐
			bool right_align = false;		///< 文字右对齐
			bool uppercase = false;			///< 输入内容转大写
			bool lowercase = false;			///< 输入内容转小写
			bool password = false;			///< 密码框
			bool read_only = false;			///< 只读
			bool number_only = false;		///< 仅数字输入
		} m_property;

		long m_lBasicStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER;

#ifdef UNICODE
		void (*m_pFunc)(std::wstring wstrText) = nullptr;
#else
		void (*m_pFunc)(std::string wstrText) = nullptr;
#endif

		bool m_bEdited = false;

		COLORREF m_cBk = WHITE;
		COLORREF m_cTextBk = WHITE;
		COLORREF m_cText = BLACK;
		HBRUSH hBk = nullptr;

		/**
		 * @brief 应用设置的属性
		*/
		void ApplyProperty();

	protected:

		void RealCreate(HWND hParent) override;

	public:

		/**
		 * @brief 编辑框预设样式（必须在创建前设置才有效）
		*/
		struct PreStyle
		{
			bool multiline = false;			///< 多行文本
			bool center_text = false;		///< 文本居中
			bool vscroll = false;			///< 拥有垂直滚动条
			bool auto_vscroll = false;		///< 自动垂直滚动
			bool hscroll = false;			///< 拥有水平滚动条
			bool auto_hscroll = false;		///< 自动水平滚动
		};

		SysEdit();

#ifdef UNICODE
		SysEdit(HWND hParent, RECT rct, std::wstring strText = L"");
		SysEdit(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysEdit(HWND hParent, RECT rct, std::string strText = "");
		SysEdit(HWND hParent, int x, int y, int w, int h, std::string strText = "");
#endif

		~SysEdit() override;

		/**
		 * @brief 在创建控件前预设控件样式
		*/
		void PreSetStyle(PreStyle pre_style);

		LRESULT UpdateMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet) override;

		/**
		 * @brief 注册输入消息
		 * @param[in] pFunc 消息响应函数
		*/
#ifdef UNICODE
		void RegisterMessage(void (*pFunc)(std::wstring wstrText));
#else
		void RegisterMessage(void (*pFunc)(std::string strText));
#endif

		/**
		 * @brief 设置文本右对齐
		 * @param[in] enable 是否启用右对齐
		*/
		void RightAlign(bool enable);

		/**
		 * @brief 自动转大写
		 * @param[in] enable 是否启用转大写
		*/
		void Uppercase(bool enable);

		/**
		 * @brief 自动转小写
		 * @param[in] enable 是否启用转小写
		*/
		void Lowercase(bool enable);

		/**
		 * @brief 设置密码框
		 * @param[in] enable 是否启用密码框
		*/
		void Password(bool enable);

		/**
		 * @brief 设置只读
		 * @param[in] enable 是否启用只读
		*/
		void ReadOnly(bool enable);

		/**
		 * @brief 设置仅数字
		 * @param[in] enable 是否启用仅数字输入
		*/
		void NumberOnly(bool enable);

		/**
		 * @brief 获取最大输入长度
		*/
		int GetMaxTextLength();

		/**
		 * @brief 限制最大输入长度
		 * @param[in] len 最大输入长度
		*/
		void SetMaxTextLength(int len);

		/**
		 * @brief 设置控件背景色
		 * @param[in] color 控件背景色
		*/
		void SetBkColor(COLORREF color);

		/**
		 * @brief 设置文本背景色
		 * @param[in] color 文本背景色
		*/
		void SetTextBkColor(COLORREF color);

		/**
		 * @brief 设置文本颜色
		 * @param[in] color 文本颜色
		*/
		void SetTextColor(COLORREF color);

		/**
		 * @brief 获取光标选择区域
		 * @param[out] begin	光标起始位置
		 * @param[out] end		选择结束位置
		*/
		void GetSel(int* begin, int* end);

		/**
		 * @brief 设置光标选择区域
		 * @param[in] begin 	光标起始位置
		 * @param[in] end 		选择结束位置（为 -1 可表示结尾）
		*/
		void SetSel(int begin, int end);

		/**
		 * @brief 复制选中的文本
		*/
		void Copy();

		/**
		 * @brief 剪切选中的文本
		*/
		void Cut();

		/**
		 * @brief 从剪切板黏贴文本到当前位置
		*/
		void Paste();

		/**
		 * @brief 删除选中的文本
		*/
		void Delete();

		/**
		 * @brief 替换选中的文本
		 * @param[in] wstrText 替换文本
		*/
#ifdef UNICODE
		void Replace(std::wstring wstrText);
#else
		void Replace(std::string strText);
#endif

		/**
		 * @brief 判断是否被编辑
		*/
		bool IsEdited();
	};
}
