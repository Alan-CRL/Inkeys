#pragma once
#include "IdtMain.h"

bool OccupyFileForRead(HANDLE* hFile, const wstring& filePath);
bool OccupyFileForWrite(HANDLE* hFile, const wstring& filePath);
bool UnOccupyFile(HANDLE* hFile);

struct SetListStruct
{
#pragma region 软件版本
	string UpdateChannel;
	string updateArchitecture;
	bool enableAutoUpdate;
#pragma endregion

#pragma region 常规
	int selectLanguage;

	bool startUp;

	int SetSkinMode, SkinMode;
	float settingGlobalScale;

	bool RightClickClose;
	bool BrushRecover, RubberRecover;

	// 兼容自动隐藏的任务栏
	bool compatibleTaskBarAutoHide;
	bool forceTop;
#pragma endregion

#pragma region 绘制
	int paintDevice;

	bool liftStraighten, waitStraighten;
	bool pointAdsorption;

	bool smoothWriting;

	struct
	{
		int eraserMode; // 0压感粗细 1笔速粗细 2固定粗细
		int eraserSize;
	}eraserSetting;
#pragma endregion

#pragma region 性能
	struct
	{
		int preparationQuantity;
	}performanceSetting;
#pragma endregion

#pragma region 插件
	struct
	{
		bool createLnk, correctLnk;
	}shortcutAssistant;
#pragma endregion
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

		// 附加信息项
		setAdmin = false;
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

	// 附加信息项
	bool setAdmin;
};
extern PptComSetListStruct pptComSetlist;
bool PptComReadSetting();
bool PptComWriteSetting();

struct DdbInteractionSetListStruct
{
	DdbInteractionSetListStruct()
	{
		DdbEnable = false;
		runAsAdmin = false;

		DdbEdition = L"20250205b";
		DdbSHA256 = "d6e08675d6cea9edf69a501c949c7c99c26b2c3c7a37cee0df27e94b493ddeb8";

		// -----

		sleepTime = 5000;

		mode = 1;
		hostPath = L"";
		restartHost = true;

		memset(InterceptWindow, true, sizeof(InterceptWindow));
		InterceptWindow[3] = false;
		InterceptWindow[11] = false;
	}

	bool DdbEnable;
	bool runAsAdmin;

	wstring DdbEdition;
	string DdbSHA256;

	// Ddb 配置文件

	int sleepTime;

	int mode; // 0 独立模式 1 随宿主程序和开启和关闭 2 随宿主程序关闭
	wstring hostPath;
	bool restartHost; // restartHost：（仅限独立模式）当宿主程序被关闭后，拦截到其他软件的窗口后，重启宿主程序

	bool InterceptWindow[15];
};
extern DdbInteractionSetListStruct ddbInteractionSetList;
bool DdbReadInteraction();
bool DdbWriteInteraction(bool change, bool close);