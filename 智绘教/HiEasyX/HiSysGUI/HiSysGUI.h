/**
 * @file	HiSysGUI.h
 * @brief	HiEasyX 库的系统图形用户界面模块
 * @author	huidong
*/

#pragma once

#include "../HiDef.h"
#include "SysGroup.h"
#include "SysGroupBox.h"
#include "SysStatic.h"
#include "SysButton.h"
#include "SysCheckBox.h"
#include "SysRadioButton.h"
#include "SysEdit.h"
#include "SysComboBox.h"

// 注意：创建系统控件时，父窗口必须选择由 HiWindow 创建的窗口，控件才能正常运行

// 现代样式（仅在 MSVC 编译器下可以设置）
#ifndef _NO_MORDEN_SYSCTRL_
#ifdef _MSC_VER

#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")  
#elif defined _M_X64  
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")  
#else  
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")  
#endif

#endif
#endif /* _NO_MORDEN_SYSCTRL_ */
