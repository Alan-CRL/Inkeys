#pragma once
#include "IdtMain.h"

bool OccupyFileForRead(HANDLE* hFile, const wstring& filePath);
bool OccupyFileForWrite(HANDLE* hFile, const wstring& filePath);
bool UnOccupyFile(HANDLE* hFile);

struct SetListStruct
{
#pragma region 软件配置
	struct
	{
		IdtAtomic<bool> enable;
	}configurationSetting;
#pragma endregion

#pragma region 软件版本
	string UpdateChannel;
	string updateArchitecture;
	bool enableAutoUpdate;
#pragma endregion

#pragma region 常规
	int selectLanguage;
	// en-US 0
	// zh-CN 1
	// zh-TW 2

	bool startUp;

	int SetSkinMode, SkinMode;
	float settingGlobalScale;

	int topSleepTime;
	bool RightClickClose;
	bool BrushRecover, RubberRecover;

	struct
	{
		IdtAtomic<bool> moveRecover, clickRecover;

		IdtAtomic<bool> avoidFullScreen;
		IdtAtomic<int> teachingSafetyMode;
	}regularSetting;

#pragma endregion

#pragma region 绘制
	int paintDevice;
	IdtAtomic<bool> disableRTS;

	bool liftStraighten, waitStraighten;
	bool pointAdsorption;

	bool smoothWriting;

	struct
	{
		int eraserMode; // 0压感粗细 1笔速粗细 2固定粗细
		int eraserSize;
	}eraserSetting;

	IdtAtomic<bool> hideTouchPointer;
#pragma endregion

#pragma region 保存
	struct
	{
		IdtAtomic<bool> enable;
		IdtAtomic<int> saveDays;
	}saveSetting;
#pragma endregion

#pragma region 性能
	struct
	{
		int preparationQuantity;
		IdtAtomic<int> drawpadFps;
		IdtAtomic<bool> superDraw;
	}performanceSetting;
#pragma endregion

#pragma region 预设
	struct
	{
		IdtAtomic<bool> memoryWidth;
		IdtAtomic<bool> memoryColor;

		IdtAtomic<bool> autoDefaultWidth;
		IdtAtomic<float> defaultBrush1Width;
		IdtAtomic<float> defaultHighlighter1Width;
	}presetSetting;
#pragma endregion

#pragma region 组件
	struct
	{
		// 快捷按钮
		struct
		{
			struct
			{
				// 应用
				IdtAtomic<bool> explorer;
				IdtAtomic<bool> taskmgr;
				IdtAtomic<bool> control;
			} appliance;
			struct
			{
				// 系统操作
				IdtAtomic<bool> desktop;
				IdtAtomic<bool> lockWorkStation;
			} system;
			struct
			{
				// 键盘模拟
				IdtAtomic<bool> keyboardesc;
				IdtAtomic<bool> keyboardAltF4;
			} keyboard;
			struct
			{
				IdtAtomic<bool> IslandCaller;
				IdtAtomic<bool> SecRandom;
				IdtAtomic<bool> NamePicker;
			}rollCall;
			struct
			{
				// 联动
				IdtAtomic<bool> classislandSettings;
				IdtAtomic<bool> classislandProfile;
				IdtAtomic<bool> classislandClassswap;
			} linkage;
		} shortcutButton;
	}component;
#pragma endregion

#pragma region 插件
	struct
	{
		bool createLnk, correctLnk;
	}shortcutAssistant;

	struct
	{
		struct
		{
			IdtAtomic<bool> enable;
			IdtAtomic<bool> indicator;
		}superTop;
	}plugInSetting;
#pragma endregion
};
extern SetListStruct setlist;
bool ReadSetting();
bool ReadSettingMini();
bool WriteSetting();

struct PptComSetListStruct
{
	PptComSetListStruct()
	{
		fixedHandWriting = true;
		showLoadingScreen = false;
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

		// autoKillWpsProcess = true;

		// 附加信息项
		setAdmin = false;
	}

	// 墨迹固定在对应页面上
	bool fixedHandWriting;
	// 显示加载画面
	bool showLoadingScreen;
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
	// bool autoKillWpsProcess;

	// 附加信息项
	bool setAdmin;
};
extern PptComSetListStruct pptComSetlist;
bool PptComReadSetting();
bool PptComReadSettingPositionOnly();
bool PptComWriteSetting();

struct DdbInteractionSetListStruct
{
	DdbInteractionSetListStruct()
	{
		enable = false;
		runAsAdmin = false;

		DdbEdition = L"20250927a";
		DdbSHA256 = "adf03fded144955b5afb8e6ffd9610aec0d4b789462f624b7fc23bbe55023402";

		// -----

		sleepTime = 5000;

		mode = 1;
		hostPath = L"";
		restartHost = true;

		// -----

		intercept.seewoWhiteboard3Floating = true;
		intercept.seewoWhiteboard5Floating = true;
		intercept.seewoWhiteboard5CFloating = true;
		intercept.seewoPincoSideBarFloating = false;
		intercept.seewoPincoDrawingFloating = true;
		intercept.seewoPPTFloating = true;
		intercept.aiClassFloating = true;
		intercept.hiteAnnotationFloating = true;
		intercept.changYanFloating = true;
		intercept.changYanPptFloating = true;
		intercept.intelligentClassFloating = true;
		intercept.seewoDesktopAnnotationFloating = true;
		intercept.seewoDesktopSideBarFloating = false;
		intercept.iclass30Floating = true;
		intercept.iclass30SidebarFloating = false;
	}

	bool enable;
	bool runAsAdmin;

	wstring DdbEdition;
	string DdbSHA256;

	// Ddb 配置文件

	int sleepTime;

	int mode; // 0 独立模式 1 随宿主程序和开启和关闭 2 随宿主程序关闭
	wstring hostPath;
	bool restartHost; // restartHost：（仅限独立模式）当宿主程序被关闭后，拦截到其他软件的窗口后，重启宿主程序

	// ----

	struct
	{
		bool seewoWhiteboard3Floating;
		bool seewoWhiteboard5Floating;
		bool seewoWhiteboard5CFloating;
		bool seewoPincoSideBarFloating;
		bool seewoPincoDrawingFloating;
		bool seewoPPTFloating;
		bool aiClassFloating;
		bool hiteAnnotationFloating;
		bool changYanFloating;
		bool changYanPptFloating;
		bool intelligentClassFloating;
		bool seewoDesktopAnnotationFloating;
		bool seewoDesktopSideBarFloating;
		bool iclass30Floating;
		bool iclass30SidebarFloating;
	} intercept;
};
extern DdbInteractionSetListStruct ddbInteractionSetList;
//bool DdbReadInteraction();
bool DdbWriteInteraction(bool change, bool close);

bool GetMemory();
bool SetMemory();