#pragma once
#include "IdtMain.h"

bool OccupyFileForRead(HANDLE* hFile, const wstring& filePath);
bool OccupyFileForWrite(HANDLE* hFile, const wstring& filePath);
bool UnOccupyFile(HANDLE* hFile);

struct SetListStruct
{
	int selectLanguage;

	bool startUp;
	bool CreateLnk;

	bool RightClickClose;
	bool BrushRecover, RubberRecover;

	bool IntelligentDrawing, SmoothWriting;
	int paintDevice;

	int SetSkinMode, SkinMode;

	string UpdateChannel;

	// 兼容自动隐藏的任务栏
	bool compatibleTaskBarAutoHide;
	bool forceTop;
};
extern SetListStruct setlist;
bool ReadSetting();
bool WriteSetting();

struct PptComSetListStruct
{
	PptComSetListStruct()
	{
		fixedHandWriting = true;
		memoryWidgetPosition = true;

		showBottomBoth = true;
		showMiddleBoth = false;
		showBottomMiddle = true;

		bottomBothWidth = 0;
		bottomBothHeight = 0;
		middleBothWidth = 0;
		middleBothHeight = 0;
		bottomMiddleWidth = 0;
		bottomMiddleHeight = 0;

		bottomSideBothWidgetScale = 1.0f;
		bottomSideMiddleWidgetScale = 1.0f;
		middleSideBothWidgetScale = 1.0f;

		autoKillWpsProcess = true;
	}

	// 墨迹固定在对应页面上
	bool fixedHandWriting;
	// 记忆控件位置
	bool memoryWidgetPosition;

	// 控件显示
	bool showBottomBoth;
	bool showMiddleBoth;
	bool showBottomMiddle;

	// 控件方位
	float bottomBothWidth;
	float bottomBothHeight;
	float middleBothWidth;
	float middleBothHeight;
	float bottomMiddleWidth;
	float bottomMiddleHeight;

	// 控件缩放
	float bottomSideBothWidgetScale;
	float bottomSideMiddleWidgetScale;
	float middleSideBothWidgetScale;

	// 自动结束未正确关闭的 WPP 进程
	bool autoKillWpsProcess;
};
extern PptComSetListStruct pptComSetlist;
bool PptComReadSetting();
bool PptComWriteSetting();

struct DdbSetListStruct
{
	DdbSetListStruct()
	{
		DdbEnable = true;
		DdbEnhance = false;

		DdbEdition = L"20241028a";
		DdbSHA256 = "f71193cec78daee6ea70da0f37d240e63c7dbe28ff41897f48809fa0823c4798";

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
};
extern DdbSetListStruct ddbSetList;
bool DdbReadSetting();
bool DdbWriteSetting(bool change, bool close);