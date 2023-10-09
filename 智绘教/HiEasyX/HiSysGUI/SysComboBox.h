/**
 * @file	SysComboBox.h
 * @brief	HiSysGUI 控件分支：组合框
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 系统组合框控件
	*/
	class SysComboBox : public SysControlBase
	{
	private:

		long m_lBasicStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_HASSTRINGS | CBS_AUTOHSCROLL;
		int m_nSel = -1;
		bool m_bSelChanged = false;
		bool m_bEdited = false;
#ifdef UNICODE
		void (*m_pFuncSel)(int sel, std::wstring wstrSelText) = nullptr;
		void (*m_pFuncEdit)(std::wstring wstrText) = nullptr;
#else
		void (*m_pFuncSel)(int sel, std::string wstrSelText) = nullptr;
		void (*m_pFuncEdit)(std::string wstrText) = nullptr;
#endif

		/**
		 * @brief	标记用户设置了 CBS_SIMPLE 同时禁用输入
		 *			此需求需要特殊实现
		*/
		bool m_bSimple_No_Edit = false;

	protected:

		void RealCreate(HWND hParent) override;

	public:

		/**
		 * @brief 组合框控件预设样式（必须在创建前设置才有效）
		*/
		struct PreStyle
		{
			bool always_show_list = false;		///< 是否总是显示列表
			bool editable = false;				///< 是否可编辑
			bool sort = false;					///< 是否自动排序
		};

		SysComboBox();

#ifdef UNICODE
		SysComboBox(HWND hParent, RECT rct, std::wstring strText = L"");
		SysComboBox(HWND hParent, int x, int y, int w, int h, std::wstring strText = L"");
#else
		SysComboBox(HWND hParent, RECT rct, std::string strText = "");
		SysComboBox(HWND hParent, int x, int y, int w, int h, std::string strText = "");
#endif

		/**
		 * @brief 在创建控件前预设样式
		*/
		void PreSetStyle(PreStyle pre_style);

		LRESULT UpdateMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet) override;

		/**
		 * @brief 注册选择消息
		 * @param[in] pFunc 消息响应函数
		*/
#ifdef UNICODE
		void RegisterSelMessage(void (*pFunc)(int sel, std::wstring wstrSelText));
#else
		void RegisterSelMessage(void (*pFunc)(int sel, std::string strSelText));
#endif

		/**
		 * @brief 注册编辑消息
		 * @param[in] pFunc 消息响应函数
		*/
#ifdef UNICODE
		void RegisterEditMessage(void (*pFunc)(std::wstring wstrText));
#else
		void RegisterEditMessage(void (*pFunc)(std::string strText));
#endif

		/**
		 * @brief 获取选中的索引
		*/
		int GetSel() const { return m_nSel; }

		/**
		 * @brief 设置选中的索引
		 * @param[in] sel 选中的索引
		*/
		void SetSel(int sel);

		/**
		 * @brief 选择具有指定文本的一项
		 * @param[in] wstrText 指定文本
		 * @return 是否选择成功
		*/
#ifdef UNICODE
		bool SelectString(std::wstring wstrText);
#else
		bool SelectString(std::string strText);
#endif

		/**
		 * @brief 增加项
		 * @param[in] wstrText 项文本
		*/
#ifdef UNICODE
		void AddString(std::wstring wstrText);
#else
		void AddString(std::string strText);
#endif

		/**
		 * @brief 插入项
		 * @param[in] index		插入位置
		 * @param[in] wstrText	项文本
		*/
#ifdef UNICODE
		void InsertString(int index, std::wstring wstrText);
#else
		void InsertString(int index, std::string strText);
#endif

		/**
		 * @brief 删除项
		 * @param[in] index	项索引
		*/
		void DeleteString(int index);

		/**
		 * @brief 获取列表内容数量
		*/
		int GetCount();

		/**
		 * @brief 清空列表
		*/
		void Clear();

		/**
		 * @brief 显示列表
		 * @param[in] enable 是否显示列表
		*/
		void ShowDropdown(bool enable);

		/**
		 * @brief 判断选择项是否变化
		*/
		bool IsSelChanged();

		/**
		 * @brief 判断是否被编辑
		*/
		bool IsEdited();
	};
}
