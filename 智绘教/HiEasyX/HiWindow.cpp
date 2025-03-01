#include "HiWindow.h"

#include "HiMacro.h"
#include "HiIcon.h"
#include "HiStart.h"
#include "HiGdiplus.h"
#include "HiCanvas.h"
#include "HiSysGUI/SysControlBase.h"

#include <chrono>
#include <iostream>

// 预留消息空间
#define MSG_RESERVE_SIZE		100

// 预留控件空间
#define SYSCTRL_RESERVE_SIZE	100

namespace HiEasyX
{
	////////////****** 全局变量 ******////////////

	WNDCLASSEX				g_WndClassEx;								///< 窗口类
	TCHAR					g_lpszClassName[] = _T("HiEasyX");			///< 窗口类名
	ScreenSize				g_screenSize;								///< 显示器信息
	HWND					g_hConsole;									///< 控制台句柄
	HINSTANCE				g_hInstance = GetModuleHandle(0);			///< 程序实例

	std::deque<std::shared_mutex> g_vecWindows_vecMessage_sm;
	std::vector<EasyWindow> g_vecWindows;								///< 窗口表（管理多窗口）

	int						g_nFocusWindowIndex = NO_WINDOW_INDEX;		///< 当前操作焦点窗口索引

	bool					g_isInTask = false;							///< 标记处于任务中

	HICON					g_hIconDefault;								///< 默认程序图标
	LPCTSTR					g_lpszCustomIcon = nullptr;					///< 自定义程序图标资源，为空表示不使用
	LPCTSTR					g_lpszCustomIconSm = nullptr;
	HICON					g_hCustomIcon;								///< 自定义程序图标
	HICON					g_hCustomIconSm;

	bool					g_isPreStyle = false;						///< 是否预设窗口样式
	bool					g_isPreStyleEx = false;						///< 是否预设窗口扩展样式
	bool					g_isPrePos = false;							///< 是否预设窗口位置
	bool					g_isPreShowState = false;					///< 是否预设窗口显示状态
	long					g_lPreStyle;								///< 创建窗口前的预设样式
	long					g_lPreStyleEx;								///< 创建窗口前的预设扩展样式
	POINT					g_pPrePos;									///< 创建窗口前的预设窗口位置
	int						g_nPreCmdShow;								///< 创建窗口前的预设显示状态

	DrawMode				g_fDrawMode = DM_Normal;					///< 全局绘制模式
	bool					g_bAutoFlush = true;						///< 是否自动刷新双缓冲

	UINT					g_uWM_TASKBARCREATED;						///< 系统任务栏消息代码

	////////////****** 函数定义 ******////////////

	// 检验窗口索引是否合法
	bool IsValidWindowIndex(int index)
	{
		return index >= 0 && index < (int)g_vecWindows.size();
	}

	// 当前是否存在操作焦点窗口（若存在，则一定是活窗口）
	bool IsFocusWindowExisted()
	{
		return IsValidWindowIndex(g_nFocusWindowIndex);
	}

	// 获取当前操作焦点窗口
	EasyWindow& GetFocusWindow()
	{
		static EasyWindow wndEmpty;
		if (IsFocusWindowExisted())
		{
			return g_vecWindows[g_nFocusWindowIndex];
		}
		else
		{
			wndEmpty = {};
			return wndEmpty;
		}
	}

	// 通过句柄获得此窗口在窗口记录表中的索引
	// 传入 nullptr 代表当前活动窗口
	// 未找到返回 NO_WINDOW_INDEX
	int GetWindowIndex(HWND hWnd, bool flag = false)
	{
		if (hWnd == nullptr)
		{
			return g_nFocusWindowIndex;
		}
		int index = NO_WINDOW_INDEX;

		for (int i = 0; i < (int)g_vecWindows.size(); i++)
		{
			if (hWnd == g_vecWindows[i].hWnd)
			{
				index = i;
				break;
			}
		}

		return index;
	}

	bool IsAnyWindow()
	{
		for (auto& i : g_vecWindows)
			if (i.isAlive)
				return true;
		return false;
	}

	bool IsAliveWindow(HWND hWnd)
	{
		if (hWnd)
		{
			int index = GetWindowIndex(hWnd);
			if (IsValidWindowIndex(index))
			{
				return g_vecWindows[index].isAlive;
			}
			else
			{
				return false;
			}
		}
		else
		{
			return IsFocusWindowExisted();
		}
	}

	bool IsAliveWindow(int index)
	{
		return IsValidWindowIndex(index) && g_vecWindows[index].isAlive;
	}

	// 等待窗口内部消息处理完成
	void WaitForProcessing(int index)
	{
		// 死窗口可能正在销毁，故不用 isAliveWindow
		if (IsValidWindowIndex(index))
		{
			while (g_vecWindows[index].isBusyProcessing)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	// 将 IMAGE 内容复制到 HDC 上
	// pImg		原图像
	// hdc		绘制的 HDC
	// rct		在 HDC 上的绘制区域
	void CopyImageToHDC(IMAGE* pImg, HDC hdc, RECT rct)
	{
		//HDC hdc = GetDC(hWnd);
		HDC hdcImg = GetImageHDC(pImg);
		BitBlt(hdc, rct.left, rct.top, rct.right, rct.bottom, hdcImg, 0, 0, SRCCOPY);
		//ReleaseDC(hWnd, hdc);
	}

	void WaitForTask(HWND hWnd)
	{
		// 未设置句柄时只需要等待，若设置了则需要判断该句柄是否对应活动窗口
		if (!hWnd || (IsFocusWindowExisted() && GetFocusWindow().hWnd == hWnd))
		{
			while (g_isInTask)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	// 释放窗口内存
	void FreeWindow(int index)
	{
		if (!IsValidWindowIndex(index))
		{
			return;
		}

		// 释放绘图缓冲
		if (g_vecWindows[index].pImg)
		{
			//f delete g_vecWindows[index].pImg;
			g_vecWindows[index].pImg = nullptr;
		}
		if (g_vecWindows[index].pBufferImg)
		{
			//f delete g_vecWindows[index].pBufferImg;
			g_vecWindows[index].pBufferImg = nullptr;
		}

		// 释放消息列表内存
		std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[index]);
		std::vector<ExMessage>().swap(g_vecWindows[index].vecMessage);
		lg_vecWindows_vecMessage_sm.unlock();

		//DestroyWindow(g_vecWindows[index].hWnd);
		//PostQuitMessage(0);
	}

	// 此函数用于内部调用，按窗口索引标记关闭窗口、释放内存
	// 重要：此函数仅可在 WndProc 线程中调用，否则无法关闭窗口
	void closegraph_win32(int index)
	{
		if (!IsAliveWindow(index))
		{
			return;
		}

		// 先设置窗口死亡，再标识忙碌，等待任务结束
		g_vecWindows[index].isAlive = false;
		g_vecWindows[index].isBusyProcessing = true;
		WaitForTask(g_vecWindows[index].hWnd);

		// 若已设置父窗口为模态窗口，则需要将父窗口恢复正常
		if (g_vecWindows[index].hParent != nullptr)
		{
			EnableWindow(g_vecWindows[index].hParent, true);
			SetForegroundWindow(g_vecWindows[index].hParent);
		}

		// 卸载托盘
		DeleteTray(g_vecWindows[index].hWnd);

		// 如果活动窗口被销毁，则需要重置活动窗口索引
		if (index == g_nFocusWindowIndex)
		{
			g_nFocusWindowIndex = NO_WINDOW_INDEX;
		}

		// 释放窗口内存
		FreeWindow(index);

		// 关闭忙碌标识
		g_vecWindows[index].isBusyProcessing = false;

		// 如果关闭此窗口后不存在任何窗口
		if (!IsAnyWindow())
		{
			// 关闭 GDI+ 绘图环境
			Gdiplus_Shutdown();
		}
	}

	// 此函数用于外部调用，只是向目标窗口线程发送关闭窗口消息
	void closegraph_win32(HWND hWnd)
	{
		// 关闭全部
		if (hWnd == nullptr)
		{
			for (int i = 0; i < (int)g_vecWindows.size(); i++)
			{
				if (g_vecWindows[i].isAlive)
				{
					// 必须交由原线程 DestroyWindow
					// 发送 WM_DESTROY 时特殊标记 wParam 为 1，表示程序命令销毁窗口
					SendMessage(g_vecWindows[i].hWnd, WM_DESTROY, 1, 0);
				}
			}
		}
		else if (IsAliveWindow(hWnd))
		{
			SendMessage(hWnd, WM_DESTROY, 1, 0);
		}
	}

	void SetWndProcFunc(HWND hWnd, WNDPROC WindowProcess)
	{
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			g_vecWindows[index].funcWndProc = WindowProcess;
		}
	}

	IMAGE* GetWindowImage(HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);

		if (IsAliveWindow(index))
		{
			return g_vecWindows[index].pBufferImg;
		}
		else
		{
			return nullptr;
		}
	}

	Canvas* GetWindowCanvas(HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			return g_vecWindows[index].pBufferImgCanvas;
		}
		else
		{
			return nullptr;
		}
	}

	void BindWindowCanvas(Canvas* pCanvas, HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			g_vecWindows[index].pBufferImgCanvas = pCanvas;
			pCanvas->BindToWindow(g_vecWindows[index].hWnd, g_vecWindows[index].pBufferImg);
		}
	}

	void init_end(HWND hWnd)
	{
		if (hWnd)
		{
			int index = GetWindowIndex(hWnd);
			while (IsAliveWindow(index))
				Sleep(100);
		}
		else
			while (IsAnyWindow())
				Sleep(100);
	}

	void AutoExit()
	{
		std::thread([]() {
			init_end();
			exit(0);
			}).detach();
	}

	HWND GetHWnd_win32()
	{
		return IsFocusWindowExisted() ? GetFocusWindow().hWnd : nullptr;
	}

	EasyWindow GetWorkingWindow()
	{
		return GetFocusWindow();
	}

	bool SetWorkingWindow(HWND hWnd)
	{
		if (!hWnd || GetFocusWindow().hWnd == hWnd)
		{
			if (GetWorkingImage() != GetFocusWindow().pBufferImg)
			{
				SetWorkingImage(GetFocusWindow().pBufferImg);
			}
			return true;
		}

		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			WaitForTask();
			WaitForProcessing(index);
			g_nFocusWindowIndex = index;

			SetWorkingImage(GetFocusWindow().pBufferImg);
			return true;
		}
		else
		{
			return false;
		}
	}

	void QuickDraw(UINT nSkipPixels, HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
			g_vecWindows[index].nSkipPixels = nSkipPixels;
	}

	DrawMode GetDrawMode()
	{
		return g_fDrawMode;
	}

	void SetDrawMode(DrawMode mode)
	{
		g_fDrawMode = mode;
	}

	// 内部函数，直接发送用户重绘消息
	void SendUserRedrawMsg(HWND hWnd)
	{
		SendMessage(hWnd, WM_USER_REDRAW, 0, 0);
	}

	void RedrawWindow(HWND hWnd)
	{
		if (!hWnd)
			hWnd = GetFocusWindow().hWnd;

		switch (g_fDrawMode)
		{
		case DM_Real:
			SendUserRedrawMsg(hWnd);
			break;

		case DM_Normal:
			// 这个太慢了
			//InvalidateRect(hWnd, nullptr, false);
			SendUserRedrawMsg(hWnd);
			break;

		case DM_Fast:
			if (!(clock() % 2))
				SendUserRedrawMsg(hWnd);
			break;

		case DM_VeryFast:
			if (!(clock() % 5))
				SendUserRedrawMsg(hWnd);
			break;

		case DM_Fastest:
			if (!(clock() % 9))
				SendUserRedrawMsg(hWnd);
			break;
		}
	}

	// 更新窗口画布的双缓冲
	// rct 更新区域（坐标都为 0 表示全部区域）
	void FlushDrawing(int index, RECT rct = { 0 })
	{
		if (!IsAliveWindow(index))
		{
			return;
		}

		int w = g_vecWindows[index].pImg->getwidth();
		int h = g_vecWindows[index].pImg->getheight();

		// 是否全部更新
		bool isAllFlush = !(rct.left && rct.top && rct.right && rct.bottom);

		// 双缓冲的两层画布
		DWORD* dst = GetImageBuffer(g_vecWindows[index].pImg);
		DWORD* src = GetImageBuffer(g_vecWindows[index].pBufferImg);

		// 部分重绘时，修正重绘区域
		RECT rctCorrected = rct;
		if (!isAllFlush)
		{
			if (rct.left < 0)		rctCorrected.left = 0;
			if (rct.top < 0)		rctCorrected.top = 0;
			if (rct.right > w)		rctCorrected.right = w;
			if (rct.bottom > h)		rctCorrected.bottom = h;
		}

		// 不跳过像素的模式
		if (g_vecWindows[index].nSkipPixels == 0)
		{
			// 全部更新
			if (isAllFlush)
			{
				// fastest
				memcpy(dst, src, sizeof(DWORD) * w * h);
			}
			// 部分更新
			else
			{
				for (int x = rctCorrected.left; x < rctCorrected.right; x++)
				{
					for (int y = rctCorrected.top; y < rctCorrected.bottom; y++)
					{
						int index = x + y * w;
						dst[index] = src[index];
					}
				}
			}
		}
		// 跳过像素的模式
		else
		{
			// 全部更新
			if (isAllFlush)
			{
				int len = w * h;
				for (int i = 0; i < len; i++)		// 线性遍历画布
				{
					if (dst[i] == src[i])			// 若两画布某位置色彩重叠，则跳过接下来的 n 个像素点
					{
						i += g_vecWindows[index].nSkipPixels;
						continue;
					}
					dst[i] = src[i];
				}
			}
			// 部分更新
			else
			{
				for (int y = rctCorrected.top; y < rctCorrected.bottom; y++)	// 在矩形区域内遍历画布
				{
					for (int x = rctCorrected.left; x < rctCorrected.right; x++)
					{
						int index = x + y * w;
						if (dst[index] == src[index])	// 若两画布某位置色彩重叠，则在 x 方向上跳过接下来的 n 个像素点
						{
							x += g_vecWindows[index].nSkipPixels;
							continue;
						}
						dst[index] = src[index];
					}
				}
			}
		}
	}// FlushDrawing

	// 提供给用户的接口
	void FlushDrawing(RECT rct, HWND hWnd)
	{
		// 为了防止用户更新双缓冲时窗口拉伸导致画布冲突，必须在窗口任务内调用此函数
		if (IsInTask(hWnd))
		{
			if (hWnd != nullptr) FlushDrawing(GetWindowIndex(hWnd), rct);
			else FlushDrawing(g_nFocusWindowIndex, rct);
		}
	}

	void EnableAutoFlush(bool enable)
	{
		g_bAutoFlush = enable;
	}

	bool BeginTask()
	{
		// 不做窗口匹配判断，只检验是否处于任务中
		if (!g_isInTask && IsFocusWindowExisted())
		{
			WaitForProcessing(g_nFocusWindowIndex);
			g_isInTask = true;
		}
		return g_isInTask;
	}

	void EndTask(bool flush)
	{
		if (g_isInTask)
		{
			if (flush && IsFocusWindowExisted())
			{
				GetFocusWindow().isNeedFlush = true;
				//FlushDrawing(g_nFocusWindowIndex);
			}

			g_isInTask = false;
		}
	}

	bool IsInTask(HWND hWnd)
	{
		return g_isInTask && (hWnd ? GetFocusWindow().hWnd == hWnd : true);
	}

	// 重新调整窗口画布大小
	void ResizeWindowImage(int index, RECT rct)
	{
		if (IsAliveWindow(index))
		{
			//f g_vecWindows[index].pImg->Resize(rct.right, rct.bottom);
			//f g_vecWindows[index].pBufferImg->Resize(rct.right, rct.bottom);
			g_vecWindows[index].isNewSize = true;
		}
	}

	void ShowTray(NOTIFYICONDATA* nid)
	{
		Shell_NotifyIcon(NIM_ADD, nid);
	}

	void CreateTray(LPCTSTR lpszTrayName, HWND hWnd)
	{
		static int id = 0;

		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			HICON hIcon = g_hIconDefault;
			if (g_lpszCustomIconSm)
				hIcon = g_hCustomIconSm;
			else if (g_lpszCustomIcon)
				hIcon = g_hCustomIcon;

			g_vecWindows[index].isUseTray = true;
			g_vecWindows[index].nid.cbSize = sizeof(NOTIFYICONDATA);
			g_vecWindows[index].nid.hWnd = g_vecWindows[index].hWnd;
			g_vecWindows[index].nid.uID = id++;
			g_vecWindows[index].nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
			g_vecWindows[index].nid.uCallbackMessage = WM_TRAY;
			g_vecWindows[index].nid.hIcon = hIcon;
			lstrcpy(g_vecWindows[index].nid.szTip, lpszTrayName);
			ShowTray(&g_vecWindows[index].nid);
		}
	}

	void DeleteTray(HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);

		// 死窗口删除时会调用该函数，所以不判断窗口死活，只需要判断窗口是否存在
		if (IsValidWindowIndex(index))
		{
			if (g_vecWindows[index].isUseTray)
			{
				g_vecWindows[index].isUseTray = false;
				Shell_NotifyIcon(NIM_DELETE, &g_vecWindows[index].nid);
			}
		}
	}

	void SetTrayMenu(HMENU hMenu, HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			g_vecWindows[index].isUseTrayMenu = true;
			g_vecWindows[index].hTrayMenu = hMenu;
		}
	}

	void SetTrayMenuProcFunc(void(*pFunc)(UINT), HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			g_vecWindows[index].funcTrayMenuProc = pFunc;
		}
	}

	bool IsWindowSizeChanged(HWND hWnd)
	{
		int index = GetWindowIndex(hWnd);
		if (IsValidWindowIndex(index))
		{
			bool b = g_vecWindows[index].isNewSize;
			g_vecWindows[index].isNewSize = false;
			return b;
		}
		else
		{
			return false;
		}
	}

	bool GetCustomIconState()
	{
		return g_lpszCustomIcon;
	}

	void SetCustomIcon(LPCTSTR lpszIcon, LPCTSTR lpszIconSm)
	{
		g_lpszCustomIcon = lpszIcon;
		g_lpszCustomIconSm = lpszIconSm;
		g_hCustomIcon = LoadIcon(g_hInstance, lpszIcon);
		g_hCustomIconSm = LoadIcon(g_hInstance, lpszIconSm);
	}

	// 获取消息容器
	std::vector<ExMessage>& GetMsgVector(HWND hWnd)
	{
		static std::vector<ExMessage> vec;
		int index = GetWindowIndex(hWnd);
		if (IsAliveWindow(index))
		{
			std::shared_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[index]);
			return g_vecWindows[index].vecMessage;
			lg_vecWindows_vecMessage_sm.unlock();
		}
		else
		{
			vec.clear();
			return vec;
		}
	}

	// 移除当前消息
	void RemoveMessage(HWND hWnd)
	{
		if (GetMsgVector(hWnd).size())
		{
			GetMsgVector(hWnd).erase(GetMsgVector(hWnd).begin());
		}
	}

	// 清空消息
	// 支持混合消息类型
	void ClearMessage(BYTE filter, HWND hWnd)
	{
		for (size_t i = 0; i < GetMsgVector(hWnd).size(); i++)
			if (filter & GetExMessageType(GetMsgVector(hWnd)[i]))
				GetMsgVector(hWnd).erase(GetMsgVector(hWnd).begin() + i--);
	}

	// 是否有新消息
	// 支持混合消息类型
	bool IsNewMessage(BYTE filter, HWND hWnd)
	{
		for (auto& element : GetMsgVector(hWnd))
			if (filter & GetExMessageType(element))
				return true;
		return false;
	}

	// 清除消息，直至获取到符合类型的消息
	// 支持混合消息类型
	ExMessage GetNextMessage(BYTE filter, HWND hWnd)
	{
		if (IsNewMessage(filter, hWnd))
		{
			for (size_t i = 0; i < GetMsgVector(hWnd).size(); i++)
			{
				if (filter & GetExMessageType(GetMsgVector(hWnd)[i]))
				{
					for (size_t j = 0; j < i; j++)
					{
						RemoveMessage(hWnd);
					}
					return GetMsgVector(hWnd)[0];
				}
			}
		}
		return {};
	}

	ExMessage getmessage_win32(BYTE filter, HWND hWnd)
	{
		while (!IsNewMessage(filter, hWnd))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		ExMessage msg = GetNextMessage(filter, hWnd);
		RemoveMessage(hWnd);
		return msg;
	}

	void getmessage_win32(ExMessage* msg, BYTE filter, HWND hWnd)
	{
		ExMessage msgEx = getmessage_win32(filter, hWnd);
		if (msg)	*msg = msgEx;
	}

	bool peekmessage_win32(ExMessage* msg, BYTE filter, bool removemsg, HWND hWnd)
	{
		if (IsNewMessage(filter, hWnd))
		{
			if (msg)		*msg = GetNextMessage(filter, hWnd);
			if (removemsg)	RemoveMessage(hWnd);
			return true;
		}
		return false;
	}

	void flushmessage_win32(BYTE filter, HWND hWnd)
	{
		ClearMessage(filter, hWnd);
	}

	bool MouseHit_win32(HWND hWnd)
	{
		return IsNewMessage(EM_MOUSE, hWnd);
	}

	MOUSEMSG GetMouseMsg_win32(HWND hWnd)
	{
		ExMessage msgEx = getmessage_win32(EM_MOUSE, hWnd);
		return To_MouseMsg(msgEx);
	}

	bool PeekMouseMsg_win32(MOUSEMSG* pMsg, bool bRemoveMsg, HWND hWnd)
	{
		ExMessage msgEx;
		bool r = peekmessage_win32(&msgEx, EM_MOUSE, bRemoveMsg, hWnd);
		*pMsg = To_MouseMsg(msgEx);
		return r;
	}

	void FlushMouseMsgBuffer_win32(HWND hWnd)
	{
		ClearMessage(EM_MOUSE, hWnd);
	}

	ExMessage To_ExMessage(MOUSEMSG msg)
	{
		ExMessage msgEx = {};
		msgEx.message = msg.uMsg;
		msgEx.ctrl = msg.mkCtrl;
		msgEx.shift = msg.mkShift;
		msgEx.lbutton = msg.mkLButton;
		msgEx.mbutton = msg.mkMButton;
		msgEx.rbutton = msg.mkRButton;
		msgEx.x = msg.x;
		msgEx.y = msg.y;
		msgEx.wheel = msg.wheel;
		return msgEx;
	}

	MOUSEMSG To_MouseMsg(ExMessage msgEx)
	{
		MOUSEMSG msg = {};
		if (GetExMessageType(msgEx) == EM_MOUSE)
		{
			msg.uMsg = msgEx.message;
			msg.mkCtrl = msgEx.ctrl;
			msg.mkShift = msgEx.shift;
			msg.mkLButton = msgEx.lbutton;
			msg.mkMButton = msgEx.mbutton;
			msg.mkRButton = msgEx.rbutton;
			msg.x = msgEx.x;
			msg.y = msgEx.y;
			msg.wheel = msgEx.wheel;
		}
		return msg;
	}

	void PreSetWindowStyle(long lStyle)
	{
		g_isPreStyle = true;
		g_lPreStyle = lStyle;
	}

	void PreSetWindowStyleEx(long lStyleEx)
	{
		g_isPreStyleEx = true;
		g_lPreStyleEx = lStyleEx;
	}

	void PreSetWindowPos(int x, int y)
	{
		g_isPrePos = true;
		g_pPrePos = { x,y };
	}

	void PreSetWindowShowState(int nCmdShow)
	{
		g_isPreShowState = true;
		g_nPreCmdShow = nCmdShow;
	}

	int SetWindowStyle(long lNewStyle, HWND hWnd)
	{
		if (hWnd == nullptr)	hWnd = GetFocusWindow().hWnd;
		return SetWindowLong(hWnd, GWL_STYLE, lNewStyle);
	}

	int SetWindowExStyle(long lNewExStyle, HWND hWnd)
	{
		if (hWnd == nullptr)	hWnd = GetFocusWindow().hWnd;
		return SetWindowLong(hWnd, GWL_EXSTYLE, lNewExStyle);
	}

	POINT GetWindowPos(HWND hWnd)
	{
		if (!hWnd)	hWnd = GetFocusWindow().hWnd;
		RECT rct;
		GetWindowRect(hWnd, &rct);
		return { rct.left, rct.top };
	}

	SIZE GetWindowSize(HWND hWnd)
	{
		if (!hWnd)	hWnd = GetFocusWindow().hWnd;
		RECT rct;
		GetWindowRect(hWnd, &rct);
		return { rct.right - rct.left, rct.bottom - rct.top };
	}

	void MoveWindow(int x, int y, HWND hWnd)
	{
		if (!hWnd)	hWnd = GetFocusWindow().hWnd;
		SetWindowPos(hWnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
	}

	void MoveWindowRel(int dx, int dy, HWND hWnd)
	{
		if (!hWnd)	hWnd = GetFocusWindow().hWnd;
		POINT pos = GetWindowPos(hWnd);
		SetWindowPos(hWnd, HWND_TOP, pos.x + dx, pos.y + dy, 0, 0, SWP_NOSIZE);
	}

	void ResizeWindow(int w, int h, HWND hWnd)
	{
		if (!hWnd)	hWnd = GetFocusWindow().hWnd;
		SetWindowPos(hWnd, HWND_TOP, 0, 0, w, h, SWP_NOMOVE);
	}

	void SetWindowTitle(LPCTSTR lpszTitle, HWND hWnd)
	{
		if (!hWnd)	hWnd = GetFocusWindow().hWnd;
		SetWindowText(hWnd, lpszTitle);
	}

	// 获取默认窗口图标
	HICON GetDefaultAppIcon()
	{
		static HBITMAP hBmp = Image2Bitmap(GetIconImage(), true);
		static HICON hIcon = Bitmap2Icon(hBmp);
		static bool init = false;
		if (!init)
		{
			DeleteObject(hBmp);
			init = true;
		}
		return hIcon;
	}

	void OnSize(int indexWnd)
	{
		RECT rctWnd;
		GetClientRect(g_vecWindows[indexWnd].hWnd, &rctWnd);

		WaitForProcessing(indexWnd);
		g_vecWindows[indexWnd].isBusyProcessing = true;		// 不能再启动任务
		WaitForTask(g_vecWindows[indexWnd].hWnd);			// 等待最后一个任务完成

		ResizeWindowImage(indexWnd, rctWnd);
		if (g_vecWindows[indexWnd].pBufferImgCanvas)
		{
			g_vecWindows[indexWnd].pBufferImgCanvas->UpdateSizeInfo();
		}

		g_vecWindows[indexWnd].isBusyProcessing = false;
	}

	void OnTray(int indexWnd, LPARAM lParam)
	{
		if (g_vecWindows[indexWnd].isUseTray)
		{
			HWND hWnd = g_vecWindows[indexWnd].hWnd;
			POINT ptMouse;
			GetCursorPos(&ptMouse);

			switch (lParam)
			{
				// 左键激活窗口
			case WM_LBUTTONDOWN:
				SetForegroundWindow(hWnd);
				break;

				// 右键打开菜单
			case WM_RBUTTONDOWN:
				if (g_vecWindows[indexWnd].isUseTrayMenu)
				{
					SetForegroundWindow(hWnd);	// 激活一下窗口，防止菜单不消失

					// 显示菜单并跟踪
					int nMenuId = TrackPopupMenu(g_vecWindows[indexWnd].hTrayMenu, TPM_RETURNCMD, ptMouse.x, ptMouse.y, 0, hWnd, nullptr);
					if (nMenuId == 0) PostMessage(hWnd, WM_LBUTTONDOWN, 0, 0);
					if (g_vecWindows[indexWnd].funcTrayMenuProc)
					{
						g_vecWindows[indexWnd].funcTrayMenuProc(nMenuId);
					}
				}
				break;

			default:
				break;
			}
		}
	}

	void OnTaskBarCreated(int indexWnd)
	{
		if (g_vecWindows[indexWnd].isUseTray)
		{
			ShowTray(&g_vecWindows[indexWnd].nid);
		}
	}

	// 登记消息（ExMessage）
	void RegisterExMessage(int indexWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// 记录消息事件
		switch (msg)
		{
			// EM_MOUSE
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		{
			ExMessage msgMouse = {};
			msgMouse.message = msg;
			msgMouse.x = GET_X_LPARAM(lParam);
			msgMouse.y = GET_Y_LPARAM(lParam);
			msgMouse.wheel = GET_WHEEL_DELTA_WPARAM(wParam);
			msgMouse.shift = LOWORD(wParam) & 0x04 ? true : false;
			msgMouse.ctrl = LOWORD(wParam) & 0x08 ? true : false;
			msgMouse.lbutton = LOWORD(wParam) & 0x01 ? true : false;
			msgMouse.mbutton = LOWORD(wParam) & 0x10 ? true : false;
			msgMouse.rbutton = LOWORD(wParam) & 0x02 ? true : false;

			// 有滚轮消息时，得到的坐标是屏幕坐标，需要转换
			if (msgMouse.wheel)
			{
				POINT p = { msgMouse.x ,msgMouse.y };
				ScreenToClient(g_vecWindows[indexWnd].hWnd, &p);
				msgMouse.x = (short)p.x;
				msgMouse.y = (short)p.y;
			}
			std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[indexWnd]);
			g_vecWindows[indexWnd].vecMessage.push_back(msgMouse);
			lg_vecWindows_vecMessage_sm.unlock();
		}
		break;

		// EM_KEY
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		{
			// code from MSDN
			WORD vkCode = LOWORD(wParam);                                 // virtual-key code
			WORD keyFlags = HIWORD(lParam);
			WORD scanCode = LOBYTE(keyFlags);                             // scan code
			BOOL isExtendedKey = (keyFlags & KF_EXTENDED) == KF_EXTENDED; // extended-key flag, 1 if scancode has 0xE0 prefix

			if (isExtendedKey)
				scanCode = MAKEWORD(scanCode, 0xE0);

			BOOL repeatFlag = (keyFlags & KF_REPEAT) == KF_REPEAT;        // previous key-state flag, 1 on autorepeat
			WORD repeatCount = LOWORD(lParam);                            // repeat count, > 0 if several keydown messages was combined into one message
			BOOL upFlag = (keyFlags & KF_UP) == KF_UP;                    // transition-state flag, 1 on keyup

			// 功能键：不区分左右
			// if we want to distinguish these keys:
			//switch (vkCode)
			//{
			//case VK_SHIFT:   // converts to VK_LSHIFT or VK_RSHIFT
			//case VK_CONTROL: // converts to VK_LCONTROL or VK_RCONTROL
			//case VK_MENU:    // converts to VK_LMENU or VK_RMENU
			//	vkCode = LOWORD(MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
			//	break;
			//}

			ExMessage msgKey = {};
			msgKey.message = msg;
			msgKey.vkcode = (BYTE)vkCode;
			msgKey.scancode = (BYTE)scanCode;
			msgKey.extended = isExtendedKey;
			msgKey.prevdown = repeatFlag;

			std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[indexWnd]);
			g_vecWindows[indexWnd].vecMessage.push_back(msgKey);
			lg_vecWindows_vecMessage_sm.unlock();

			// 给控制台发一份，支持 _getch() 系列函数
			PostMessage(g_hConsole, msg, wParam, lParam);
		}
		break;

		// EM_CHAR
		case WM_CHAR:
		{
			ExMessage msgChar = {};
			msgChar.message = msg;
			msgChar.ch = (TCHAR)wParam;

			std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[indexWnd]);
			g_vecWindows[indexWnd].vecMessage.push_back(msgChar);
			lg_vecWindows_vecMessage_sm.unlock();

			// 通知控制台
			PostMessage(g_hConsole, msg, wParam, lParam);
		}
		break;

		// EM_WINDOW
		case WM_ACTIVATE:
		case WM_MOVE:
		case WM_SIZE:
		{
			ExMessage msgWindow = {};
			msgWindow.message = msg;
			msgWindow.wParam = wParam;
			msgWindow.lParam = lParam;
			std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[indexWnd]);
			g_vecWindows[indexWnd].vecMessage.push_back(msgWindow);
			lg_vecWindows_vecMessage_sm.unlock();
		}
		break;
		}
	}

	// 绘制用户内容
	void OnPaint(int indexWnd, HDC hdc)
	{
		// 在开启自动刷新双缓冲的情况下，处理双缓冲的刷新任务
		if (g_bAutoFlush && g_vecWindows[indexWnd].isNeedFlush)
		{
			WaitForProcessing(indexWnd);
			g_vecWindows[indexWnd].isBusyProcessing = true;		// 不能再启动任务
			WaitForTask(g_vecWindows[indexWnd].hWnd);			// 等待最后一个任务完成

			// 更新双缓冲
			FlushDrawing(indexWnd);
			g_vecWindows[indexWnd].isNeedFlush = false;

			g_vecWindows[indexWnd].isBusyProcessing = false;
		}

		// 将绘图内容输出到窗口 HDC
		RECT rctWnd;
		GetClientRect(g_vecWindows[indexWnd].hWnd, &rctWnd);
		CopyImageToHDC(g_vecWindows[indexWnd].pImg, hdc, rctWnd);
	}

	void OnMove(HWND hWnd)
	{
		//RECT rctWnd;
		//GetWindowRect(hWnd, &rctWnd);

		//// 移动窗口超出屏幕时可能导致子窗口显示有问题，所以此时需要彻底重绘
		//// 如果用户代码一直在强制重绘，则此操作多余。
		//if (rctWnd.left < g_screenSize.left || rctWnd.top < g_screenSize.top
		//	|| rctWnd.right > g_screenSize.left + g_screenSize.w
		//	|| rctWnd.bottom > g_screenSize.top + g_screenSize.h)
		//{
		//	EnforceRedraw(hWnd);
		//}
	}

	void OnDestroy(int indexWnd, WPARAM wParam)
	{
		closegraph_win32(indexWnd);

		// 存在参数，意味着这是用户调用 closegraph_win32 销毁窗口
		// 故再调用 DestroyWindow
		if (wParam)
		{
			DestroyWindow(g_vecWindows[indexWnd].hWnd);
		}
	}

	HWND OnSysCtrlCreate(int indexWnd, WPARAM wParam, LPARAM lParam)
	{
		CREATESTRUCT* c = (CREATESTRUCT*)lParam;
		HWND hWnd = CreateWindow(
			c->lpszClass,
			c->lpszName,
			c->style,
			c->x, c->y,
			c->cx, c->cy,
			c->hwndParent,
			c->hMenu,
			GetModuleHandle(0),
			c->lpCreateParams
		);

		// 记录
		g_vecWindows[indexWnd].vecSysCtrl.push_back((SysControlBase*)wParam);
		return hWnd;
	}

	// 处理系统控件消息
	// bRet 传出，标记是否直接返回
	LRESULT SysCtrlProc(int indexWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& bRet)
	{
		switch (msg)
		{
			// 创建系统控件
		case WM_SYSCTRL_CREATE:
		{
			g_vecWindows[indexWnd].bHasCtrl = true;
			bRet = true;
			return (LRESULT)OnSysCtrlCreate(indexWnd, wParam, lParam);
			break;
		}

		// 析构系统控件
		case WM_SYSCTRL_DELETE:
		{
			// 被析构的控件指针标记为空
			for (size_t i = 0; i < g_vecWindows[indexWnd].vecSysCtrl.size(); i++)
			{
				if (g_vecWindows[indexWnd].vecSysCtrl[i] == (SysControlBase*)wParam)
				{
					g_vecWindows[indexWnd].vecSysCtrl[i] = nullptr;
				}
			}

			bRet = true;
			return 0;
			break;
		}
		}

		// 存在控件时，派发消息
		if (g_vecWindows[indexWnd].bHasCtrl)
		{
			bool bCtrlRet = false;
			LRESULT lr = 0;
			for (auto& pCtrl : g_vecWindows[indexWnd].vecSysCtrl)
			{
				if (pCtrl)
				{
					LRESULT lr = pCtrl->UpdateMessage(msg, wParam, lParam, bCtrlRet);
					if (bCtrlRet)
					{
						bRet = true;
						return lr;
					}
				}
			}
		}

		bRet = false;
		return 0;
	}

	void OnCreate(int indexWnd, HWND hWnd, LPARAM lParam)
	{
	}

	// 窗口过程函数
	LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// 窗口矩形信息
		LRESULT resultUserProc = HIWINDOW_DEFAULT_PROC;		// 记录用户窗口过程函数返回值
		int indexWnd = GetWindowIndex(hWnd);				// 该窗口在已记录列表中的索引

		// 调用窗口不在窗口列表内，则使用默认方法进行处理（无需检查窗口死活）
		if (!IsValidWindowIndex(indexWnd))
		{
			// 也有可能正在接收 WM_CREATE 消息，此时窗口还未加入列表，则调用用户过程函数
			if (msg == WM_CREATE)
			{
				// 此时需要修正 index
				int indexReal = (int)g_vecWindows.size() - 1;
				OnCreate(indexReal, hWnd, lParam);
				WNDPROC proc = g_vecWindows[indexReal].funcWndProc;
				if (proc)
				{
					proc(hWnd, msg, wParam, lParam);
				}
			}

			return DefWindowProc(hWnd, msg, wParam, lParam);
		}

		//** 开始处理窗口消息 **//

		// 预先处理部分消息
		switch (msg)
		{
		case WM_SIZE:
			OnSize(indexWnd);
			break;

			// 托盘消息
		case WM_TRAY:
			OnTray(indexWnd, lParam);
			break;

		default:
			// 系统任务栏重新创建，此时可能需要重新创建托盘
			if (msg == g_uWM_TASKBARCREATED)
			{
				OnTaskBarCreated(indexWnd);
			}
			break;
		}

		// 活窗口的一般事件处理
		if (IsAliveWindow(indexWnd))
		{
			// 登记消息
			RegisterExMessage(indexWnd, msg, wParam, lParam);

			// 处理系统控件消息
			bool bRetSysCtrl = false;
			LRESULT lrSysCtrl = SysCtrlProc(indexWnd, msg, wParam, lParam, bRetSysCtrl);
			if (bRetSysCtrl)
				return lrSysCtrl;
		}

		// 调用用户消息处理函数
		if (g_vecWindows[indexWnd].funcWndProc)
		{
			resultUserProc = g_vecWindows[indexWnd].funcWndProc(hWnd, msg, wParam, lParam);
		}

		// 善后工作
		switch (msg)
		{
			// 用户重绘消息，处理完直接返回
			// 也无需调用系统重绘方法
			// 放着是为了让用户也能处理到这个消息
		case WM_USER_REDRAW:
		{
			HDC hdc = GetDC(hWnd);
			OnPaint(indexWnd, hdc);
			ReleaseDC(hWnd, hdc);
			return 0;
			break;
		}

		// 因为用户可能在过程函数中绘图，要在他之后输出缓存
		case WM_PAINT:
		{
			HDC			hdc;
			PAINTSTRUCT	ps;
			hdc = BeginPaint(hWnd, &ps);
			OnPaint(indexWnd, hdc);
			EndPaint(hWnd, &ps);

			// WM_PAINT 消息中需要调用系统绘制方法
			DefWindowProc(hWnd, WM_PAINT, 0, 0);
			break;
		}

		case WM_MOVE:
			OnMove(hWnd);
			break;

			// 关闭窗口，释放内存
		case WM_DESTROY:
			OnDestroy(indexWnd, wParam);
			break;
		}

		// 返回值
		LRESULT lResult = 0;

		// 此处统一在函数末尾返回

		// 用户未处理此消息
		if (!g_vecWindows[indexWnd].funcWndProc || resultUserProc == HIWINDOW_DEFAULT_PROC)
		{
			switch (msg)
			{
			case WM_CLOSE:
				DestroyWindow(g_vecWindows[indexWnd].hWnd);
				break;

			case WM_DESTROY:
				PostQuitMessage(0);
				break;

				// WM_PAINT 消息无需重复调用默认方法
			case WM_PAINT:
				break;

			default:
				lResult = DefWindowProc(hWnd, msg, wParam, lParam);
				break;
			}
		}

		// 用户已处理此消息
		else
		{
			switch (msg)
			{
			case WM_CLOSE:
				break;

			case WM_DESTROY:
				break;
			}

			lResult = resultUserProc;
		}

		return lResult;
	}

	void RegisterWndClass(LPCTSTR lpszClassName)
	{
		HICON hIcon = g_hIconDefault;
		HICON hIconSm = g_hIconDefault;
		if (g_lpszCustomIcon)
			hIcon = g_hCustomIcon;
		if (g_lpszCustomIconSm)
			hIconSm = g_hCustomIconSm;

		g_WndClassEx.cbSize = sizeof(WNDCLASSEX);
		g_WndClassEx.style = CS_VREDRAW | CS_HREDRAW;
		g_WndClassEx.lpfnWndProc = WndProc;
		g_WndClassEx.cbClsExtra = 0;
		g_WndClassEx.cbWndExtra = 0;
		g_WndClassEx.hInstance = g_hInstance;
		g_WndClassEx.hIcon = hIcon;
		g_WndClassEx.hIconSm = hIconSm;
		g_WndClassEx.hCursor = LoadCursor(nullptr, IDC_ARROW);
		g_WndClassEx.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		g_WndClassEx.lpszMenuName = nullptr;
		g_WndClassEx.lpszClassName = lpszClassName;

		// 注册窗口类
		if (!RegisterClassEx(&g_WndClassEx))
		{
#ifdef UNICODE
			std::wstring str = std::to_wstring(GetLastError());
#else
			std::string str = std::to_string(GetLastError());
#endif
			MessageBox(nullptr, (_T("Error registing window class: ") + str).c_str(), _T("[Error]"), MB_OK | MB_ICONERROR);
			exit(-1);
		}
	}

	// 初始化窗口结构体
	EasyWindow& InitWindowStruct(EasyWindow& wnd, HWND hParent, int w, int h, WNDPROC WindowProcess)
	{
		wnd.isAlive = true;
		wnd.hWnd = nullptr;
		wnd.hParent = hParent;
		//f wnd.pImg = new IMAGE(w, h);
		//f wnd.pBufferImg = new IMAGE(w, h);
		wnd.pBufferImgCanvas = nullptr;
		wnd.isNeedFlush = false;
		wnd.funcWndProc = WindowProcess;
		wnd.vecMessage.reserve(MSG_RESERVE_SIZE);
		wnd.isUseTray = false;
		wnd.nid = { 0 };
		wnd.isUseTrayMenu = false;
		wnd.hTrayMenu = nullptr;
		wnd.funcTrayMenuProc = nullptr;
		wnd.isNewSize = false;
		wnd.isBusyProcessing = false;
		wnd.nSkipPixels = 0;
		wnd.vecSysCtrl.reserve(SYSCTRL_RESERVE_SIZE);
		return wnd;
	}

	void InitRenderStartScene(HWND hWnd, int w, int h, int nPreCmdShow, bool& nStartAnimation)
	{
		RenderStartScene(hWnd, w, h, nPreCmdShow);
		nStartAnimation = true;
		SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}

	// 真正创建窗口的函数（阻塞）
	void InitWindow(int w, int h, int flag, LPCTSTR lpszWndTitle, LPCTSTR lpszClassName, WNDPROC WindowProcess, HWND hParent, int* nDoneFlag, bool* nStartAnimation, HWND* hWnd)
	{
		static int nWndCount = 0;	// 已创建窗口计数（用于生成窗口标题）

#ifdef UNICODE
		std::wstring wstrTitle;		// 窗口标题
#else
		std::string strTitle;		// 窗口标题
#endif
		if (_tcslen(lpszClassName) == 0) lpszClassName = g_lpszClassName;

		EasyWindow wnd;				// 窗口信息
		int nFrameW, nFrameH;		// 窗口标题栏宽高（各个窗口可能不同）
		int nIndexWnd = nWndCount;	// 记录这个窗口的 id

		// 可能多个窗口同时在创建，为了防止预设窗口属性交叉，先备份数据，让出全局变量
		bool isPreStyle = g_isPreStyle;
		bool isPreStyleEx = g_isPreStyleEx;
		bool isPrePos = g_isPrePos;
		bool isPreShowState = g_isPreShowState;
		long lPreStyle = g_lPreStyle;
		long lPreStyleEx = g_lPreStyleEx;
		POINT pPrePos = g_pPrePos;
		int nPreCmdShow = g_nPreCmdShow;

		bool start_animation = false;

		g_isPreStyle = false;
		g_isPreStyleEx = false;
		g_isPrePos = false;
		g_isPreShowState = false;

		// 未设置标题
		if (lstrlen(lpszWndTitle) == 0)
		{
#ifdef UNICODE
			wstrTitle = L"EasyX_" + (std::wstring)GetEasyXVer() + L" HiEasyX (" _HIEASYX_VER_STR_ + L")";
			if (nIndexWnd != 0)
			{
				wstrTitle += L" ( WindowID: " + std::to_wstring(nIndexWnd) + L" )";
			}
#else
			strTitle = "EasyX_" + (std::string)GetEasyXVer() + " HiEasyX (" _HIEASYX_VER_STR_ + ")";
			if (nIndexWnd != 0)
			{
				strTitle += " ( WindowID: " + std::to_string(nIndexWnd) + " )";
			}
#endif
		}
		else
		{
#ifdef UNICODE
			wstrTitle = lpszWndTitle;
#else
			strTitle = lpszWndTitle;
#endif
		}

		// 注册窗口类
		RegisterWndClass(lpszClassName);
		// 第一次创建窗口 --- 初始化各项数据
		if (nIndexWnd == 0)
		{
			// 获取分辨率
			g_screenSize = GetScreenSize();

			// 默认程序图标
			g_hIconDefault = GetDefaultAppIcon();

			g_hConsole = GetConsoleWindow();

			// 隐藏控制台
			if (g_hConsole)
			{
				ShowWindow(g_hConsole, SW_HIDE);
			}

			// 获取系统任务栏自定义的消息代码
			g_uWM_TASKBARCREATED = RegisterWindowMessage(TEXT("TaskbarCreated"));

#ifndef _DEBUG
#ifndef __DEBUG__
#ifndef DEBUG
#ifndef _NO_START_ANIMATION_

			if (!(isPreShowState && nPreCmdShow == SW_HIDE) && w >= 640 && h >= 480)
				start_animation = true;

#endif
#endif
#endif
#endif
		}

		// 如果现在不存在任何窗口
		if (!IsAnyWindow())
		{
			// 初始化 GDI+ 绘图环境
			Gdiplus_Try_Starup();
		}

		// 控制台
		if (g_hConsole && flag & EW_SHOWCONSOLE)
		{
			ShowWindow(g_hConsole, flag & SW_NORMAL);
		}

		// 用户在创建窗口时设置的窗口属性
		long user_style = WS_OVERLAPPEDWINDOW;
		if (flag & EW_NOMINIMIZE)	// 剔除最小化按钮
		{
			user_style &= ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX;
		}
		// 此方法不行，在下面处理此属性
		/*if (flag & EW_NOCLOSE)
		{
			user_style &= ~WS_SYSMENU;
		}*/
		if (flag & EW_DBLCLKS)		// 支持双击
		{
			user_style |= CS_DBLCLKS;
		}

		// 在创建窗口前将窗口加入容器，预设句柄为空，方便过程函数接收 WM_CREATE 消息
		InitWindowStruct(wnd, hParent, w, h, WindowProcess);

		g_vecWindows_vecMessage_sm.emplace_back();
		std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(g_vecWindows_vecMessage_sm[nIndexWnd]);
		g_vecWindows.push_back(wnd);
		lg_vecWindows_vecMessage_sm.unlock();

		// 创建窗口
		for (int i = 0;; i++)
		{
			// 最终确定使用的窗口样式
			long final_style = user_style;
			if (isPreStyle)
				final_style = lPreStyle;
			final_style |= WS_CLIPCHILDREN;	// 必须加入此样式

			// 最终确定使用的窗口扩展样式
			long final_style_ex = WS_EX_WINDOWEDGE;
			if (isPreStyleEx)
				final_style_ex = lPreStyleEx;

#ifdef UNICODE
			wnd.hWnd = CreateWindowEx(
				final_style_ex,
				lpszClassName,
				wstrTitle.c_str(),
				final_style,
				CW_USEDEFAULT, CW_USEDEFAULT,
				w, h,	// 宽高现在这样设置，稍后获取边框大小后再调整
				hParent,
				nullptr,
				g_hInstance,
				nullptr
			);

#else
			wnd.hWnd = CreateWindowEx(
				final_style_ex,
				lpszClassName,
				strTitle.c_str(),
				final_style,
				CW_USEDEFAULT, CW_USEDEFAULT,
				w, h,	// 宽高现在这样设置，稍后获取边框大小后再调整
				hParent,
				nullptr,
				g_hInstance,
				nullptr
			);
#endif

			if (wnd.hWnd)
			{
				// 创建窗口成功后，再将句柄记录
				g_vecWindows[g_vecWindows.size() - 1].hWnd = wnd.hWnd;
				break;
			}

			// 三次创建窗口失败，不再尝试
			else if (i == 2)
			{
#ifdef UNICODE
				std::wstring str = std::to_wstring(GetLastError());
#else
				std::string str = std::to_string(GetLastError());
#endif
				MessageBox(nullptr, (_T("Error creating window: ") + str).c_str(), _T("[Error]"), MB_OK | MB_ICONERROR);
				*nDoneFlag = -1;
				return;
			}
		}

		// 剔除关闭按钮
		if (flag & EW_NOCLOSE)
		{
			HMENU hmenu = GetSystemMenu(wnd.hWnd, false);
			RemoveMenu(hmenu, SC_CLOSE, MF_BYCOMMAND);
		}

		// 抢夺窗口焦点
		//f SetWorkingWindow(wnd.hWnd);

		*hWnd = wnd.hWnd;

		// 窗口创建完毕
		nWndCount++;

		// 注意：
		//	必须在显示窗口前标记已经完成创建窗口。
		//	因为可以在自定义过程函数中创建子窗口，若是不在显示窗口前标记窗口创建完成，
		//	就会导致父窗口过程函数阻塞，接下来显示窗口就会阻塞，进而导致整个窗口假死。
		*nDoneFlag = 1;
		if (!start_animation) *nStartAnimation = true;

		//** 显示窗口等后续处理 **//

		// 获取边框大小，补齐绘图区大小
		RECT rcClient, rcWnd;
		GetClientRect(wnd.hWnd, &rcClient);
		GetWindowRect(wnd.hWnd, &rcWnd);
		nFrameW = (rcWnd.right - rcWnd.left) - rcClient.right;
		nFrameH = (rcWnd.bottom - rcWnd.top) - rcClient.bottom;

		int px = 0, py = 0;
		if (isPrePos)
		{
			px = pPrePos.x;
			py = pPrePos.y;
		}
		SetWindowPos(
			wnd.hWnd,
			HWND_TOP,
			px, py,
			w + nFrameW, h + nFrameH,
			isPrePos ? 0 : SWP_NOMOVE
		);

		if (!start_animation)
		{
			ShowWindow(wnd.hWnd, SW_HIDE); // Inkeys ARM64 此处会遇到玄学错误
			// ShowWindow(wnd.hWnd, isPreShowState ? SW_HIDE : SW_SHOWNORMAL);
			UpdateWindow(wnd.hWnd);
		}
		// 发布模式下渲染开场动画
		if (start_animation == true)
		{
			// 渲染开场动画
			std::thread([&]() {
				InitRenderStartScene(wnd.hWnd, w, h, isPreShowState ? nPreCmdShow : SW_SHOWNORMAL, *nStartAnimation);
				}).detach();
		}

		// 消息派发，阻塞
		// 窗口销毁后会自动退出
		MSG Msg;
		while (GetMessage(&Msg, 0, 0, 0) > 0)
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
	}

	HWND initgraph_win32(int w, int h, int flag, LPCTSTR lpszWndTitle, LPCTSTR lpszClassName, WNDPROC WindowProcess, HWND hParent)
	{
		// 标记是否已经完成窗口创建任务
		int nDoneFlag = 0;
		HWND hWnd = nullptr;

		bool nStartAnimation = false;

		// 存在父窗口时，实现模态窗口
		if (hParent)
		{
			// 禁用父窗口（该窗口被销毁后，父窗口将会恢复正常）
			//EnableWindow(hParent, false);
		}

		std::thread(InitWindow, w, h, flag, lpszWndTitle, lpszClassName, WindowProcess, hParent, &nDoneFlag, &nStartAnimation, &hWnd).detach();

		while (nDoneFlag == 0)	Sleep(50);		// 等待窗口创建完成
		if (nDoneFlag == -1)
		{
			if (hParent)						// 创建子窗口失败，则使父窗口恢复正常
			{
				//EnableWindow(hParent, true);
			}
			return nullptr;
		}
		else
		{
			while (nStartAnimation == false)	Sleep(50);		// 等待初始动画完成
			// 预设背景色
			//f
			/*if (SetWorkingWindow(hWnd) && BeginTask())
			{
				setbkcolor(CLASSICGRAY);
				settextcolor(BLACK);
				setlinecolor(BLACK);
				setfillcolor(BLACK);
				cleardevice();
				EndTask();
				RedrawWindow();
			}*/

			return hWnd;
		}
	}

	bool init_console()
	{
		if (GetConsoleWindow() == NULL) AllocConsole();
		if (GetConsoleWindow() != NULL)
		{
			ShowWindow(GetConsoleWindow(), SW_SHOW);
			return true;
		}
		return false;
	}

	bool hide_console()
	{
		if (GetConsoleWindow() != NULL)
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
			return true;
		}
		return false;
	}

	bool close_console()
	{
		if (GetConsoleWindow() != NULL)
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
			FreeConsole();
			return true;
		}
		return false;
	}

	Window::Window()
	{
	}

	Window::Window(int w, int h, int flag, LPCTSTR lpszWndTitle, WNDPROC WindowProcess, HWND hParent)
	{
		InitWindow(w, h, flag, lpszWndTitle, WindowProcess, hParent);
	}

	Window::~Window()
	{
	}

	HWND Window::InitWindow(int w, int h, int flag, LPCTSTR lpszWndTitle, WNDPROC WindowProcess, HWND hParent)
	{
		if (!m_isCreated)
		{
			// 预设窗口属性
			if (m_isPreStyle)		PreSetWindowStyle(m_lPreStyle);
			if (m_isPreStyleEx)		PreSetWindowStyleEx(m_lPreStyleEx);
			if (m_isPrePos)			PreSetWindowPos(m_pPrePos.x, m_pPrePos.y);
			if (m_isPreShowState)	PreSetWindowShowState(m_nPreCmdShow);

			HWND hwnd = initgraph_win32(w, h, flag, lpszWndTitle, L"", WindowProcess, hParent);
			int index = GetWindowIndex(hwnd);
			m_nWindowIndex = index;
			m_isCreated = true;
			return hwnd;
		}
		return nullptr;
	}

	HWND Window::Create(int w, int h, int flag, LPCTSTR lpszWndTitle, WNDPROC WindowProcess, HWND hParent)
	{
		return InitWindow(w, h, flag, lpszWndTitle, WindowProcess, hParent);
	}

	void Window::CloseWindow()
	{
		closegraph_win32(g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::Destroy()
	{
		CloseWindow();
	}

	void Window::SetProcFunc(WNDPROC WindowProcess)
	{
		SetWndProcFunc(g_vecWindows[m_nWindowIndex].hWnd, WindowProcess);
	}

	HWND Window::GetHandle()
	{
		return g_vecWindows[m_nWindowIndex].hWnd;
	}

	EasyWindow Window::GetInfo()
	{
		return g_vecWindows[m_nWindowIndex];
	}

	bool Window::IsAlive()
	{
		return IsAliveWindow(m_nWindowIndex);
	}

	IMAGE* Window::GetImage()
	{
		return g_vecWindows[m_nWindowIndex].pBufferImg;
	}

	Canvas* Window::GetCanvas()
	{
		return g_vecWindows[m_nWindowIndex].pBufferImgCanvas;
	}

	void Window::BindCanvas(Canvas* pCanvas)
	{
		BindWindowCanvas(pCanvas, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::WaitMyTask()
	{
		WaitForTask(g_vecWindows[m_nWindowIndex].hWnd);
	}

	bool Window::SetWorkingWindow()
	{
		return HiEasyX::SetWorkingWindow(g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::FlushDrawing(RECT rct)
	{
		if (IsInTask())
		{
			HiEasyX::FlushDrawing(rct);
		}
	}

	bool Window::BeginTask()
	{
		if (SetWorkingWindow())
		{
			return HiEasyX::BeginTask();
		}
		else
		{
			return false;
		}
	}

	void Window::EndTask(bool flush)
	{
		HiEasyX::EndTask(flush);
	}

	bool Window::IsInTask()
	{
		return HiEasyX::IsInTask(g_vecWindows[m_nWindowIndex].hWnd);
	}

	bool Window::IsSizeChanged()
	{
		return IsWindowSizeChanged(g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::CreateTray(LPCTSTR lpszTrayName)
	{
		HiEasyX::CreateTray(lpszTrayName, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::DeleteTray()
	{
		HiEasyX::DeleteTray(g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::SetTrayMenu(HMENU hMenu)
	{
		HiEasyX::SetTrayMenu(hMenu, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::SetTrayMenuProcFunc(void(*pFunc)(UINT))
	{
		HiEasyX::SetTrayMenuProcFunc(pFunc, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::PreSetStyle(long lStyle)
	{
		m_isPreStyle = true;
		m_lPreStyle = lStyle;
	}

	void Window::PreSetStyleEx(long lStyleEx)
	{
		m_isPreStyleEx = true;
		m_lPreStyleEx = lStyleEx;
	}

	void Window::PreSetPos(int x, int y)
	{
		m_isPrePos = true;
		m_pPrePos = { x,y };
	}

	void Window::PreSetShowState(int nCmdShow)
	{
		m_isPreShowState = true;
		m_nPreCmdShow = nCmdShow;
	}

	void Window::SetQuickDraw(UINT nSkipPixels)
	{
		QuickDraw(nSkipPixels, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::Redraw()
	{
		RedrawWindow(g_vecWindows[m_nWindowIndex].hWnd);
	}

	long Window::GetStyle()
	{
		return GetWindowStyle(g_vecWindows[m_nWindowIndex].hWnd);
	}

	int Window::SetStyle(long lNewStyle)
	{
		return SetWindowStyle(lNewStyle, g_vecWindows[m_nWindowIndex].hWnd);
	}

	long Window::GetExStyle()
	{
		return GetWindowExStyle(g_vecWindows[m_nWindowIndex].hWnd);
	}

	int Window::SetExStyle(long lNewExStyle)
	{
		return SetWindowExStyle(lNewExStyle, g_vecWindows[m_nWindowIndex].hWnd);
	}

	POINT Window::GetPos()
	{
		return GetWindowPos(g_vecWindows[m_nWindowIndex].hWnd);
	}

	SIZE Window::GetWindowSize()
	{
		return HiEasyX::GetWindowSize(g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::Move(int x, int y)
	{
		MoveWindow(x, y, g_vecWindows[m_nWindowIndex].hWnd);
	}

	int Window::GetWindowWidth()
	{
		return GetWindowSize().cx;
	}

	int Window::GetWindowHeight()
	{
		return  GetWindowSize().cy;
	}

	void Window::MoveRel(int dx, int dy)
	{
		MoveWindowRel(dx, dy, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::Resize(int w, int h)
	{
		ResizeWindow(w, h, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::SetTransparent(bool enable, int alpha)
	{
		//测试结论：此函数不能与 UpdateLayeredWindow 一同使用

		LONG nRet = ::GetWindowLong(g_vecWindows[m_nWindowIndex].hWnd, GWL_EXSTYLE);
		nRet |= WS_EX_LAYERED;
		::SetWindowLong(g_vecWindows[m_nWindowIndex].hWnd, GWL_EXSTYLE, nRet);

		if (!enable) alpha = 0xFF;
		SetLayeredWindowAttributes(g_vecWindows[m_nWindowIndex].hWnd, 0, alpha, LWA_ALPHA);
	}

	void Window::SetTitle(LPCTSTR lpszTitle)
	{
		SetWindowTitle(lpszTitle, g_vecWindows[m_nWindowIndex].hWnd);
	}

	bool Window::IsForegroundWindow()
	{
		return GetForegroundWindow() == g_vecWindows[m_nWindowIndex].hWnd;
	}

	int Window::GetClientWidth()
	{
		return g_vecWindows[m_nWindowIndex].pBufferImg->getwidth();
	}

	int Window::GetClientHeight()
	{
		return g_vecWindows[m_nWindowIndex].pBufferImg->getheight();
	}

	ExMessage Window::Get_Message(BYTE filter)
	{
		return getmessage_win32(filter, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::Get_Message(ExMessage* msg, BYTE filter)
	{
		return getmessage_win32(msg, filter, g_vecWindows[m_nWindowIndex].hWnd);
	}

	bool Window::Peek_Message(ExMessage* msg, BYTE filter, bool removemsg)
	{
		return peekmessage_win32(msg, filter, removemsg, g_vecWindows[m_nWindowIndex].hWnd);
	}

	void Window::Flush_Message(BYTE filter)
	{
		flushmessage_win32(filter, g_vecWindows[m_nWindowIndex].hWnd);
	}
}