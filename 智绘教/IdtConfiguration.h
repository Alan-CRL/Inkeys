#pragma once
#include "IdtMain.h"

struct SetListStruct
{
	SetListStruct()
	{
		StartUp = 0, CreateLnk = false;
		BrushRecover = true, RubberRecover = false;
		RubberMode = 0;

		IntelligentDrawing = true, SmoothWriting = true;

		SetSkinMode = 0, SkinMode = 1;

		UpdateChannel = "LTS";
	}

	int StartUp; bool CreateLnk;
	bool BrushRecover, RubberRecover;
	int RubberMode;

	bool IntelligentDrawing, SmoothWriting;

	int SetSkinMode, SkinMode;

	string UpdateChannel;
};
extern SetListStruct setlist;

struct DdbSetListStruct
{
	DdbSetListStruct()
	{
		DdbEnable = true;
		DdbEnhance = false;

		DdbEdition = L"20240517a";
		DdbSHA256 = "811288094e6e5e59c0c4c2e878f30bbb7b752f574b882c81642389ba7f1a18a5";

		// -----

		sleepTime = 5000;

		mode = 1;
		hostPath = L"";
		restartHost = true;

		memset(InterceptWindow, true, sizeof(InterceptWindow));
	}

	bool DdbEnable, DdbEnhance;
	wstring DdbEdition;
	string DdbSHA256;

	// Ddb 配置文件

	int sleepTime;

	int mode; // 0 独立模式 1 随宿主程序和开启和关闭 2 随宿主程序关闭
	wstring hostPath;
	bool restartHost; // restartHost：（仅限独立模式）当宿主程序被关闭后，拦截到其他软件的窗口后，重启宿主程序

	bool InterceptWindow[10];
	/* InterceptWindow 列表：
	 *
	 * 0 AiClass 桌面悬浮窗
	 * 1 希沃白板 桌面悬浮窗
	 * 2 希沃品课（桌面悬浮窗和PPT控件）
	 * 3 希沃品课 桌面画板
	 * 4 希沃PPT小工具
	 *
	 */
};
extern DdbSetListStruct ddbSetList;
bool DdbReadSetting();
bool DdbWriteSetting(bool change, bool close);