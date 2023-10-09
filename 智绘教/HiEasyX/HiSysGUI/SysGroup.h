/**
 * @file	SysGroup.h
 * @brief	HiSysGUI 控件分支：分组标志
 * @author	huidong
*/

#pragma once

#include "SysControlBase.h"

namespace HiEasyX
{
	/**
	 * @brief 系统控件分组标志
	*/
	class SysGroup : public SysControlBase
	{
	protected:

		void RealCreate(HWND hParent) override;

	public:

		SysGroup();

		SysGroup(HWND hParent);

		void Create(HWND hParent);
	};
}
