#pragma once
#include "IdtMain.h"
#include "PptSettingsPersistence.h"
#include "Inkeys/Business/PenWidthPolicy.hpp"

bool OccupyFileForRead(HANDLE* hFile, const wstring& filePath);
bool OccupyFileForWrite(HANDLE* hFile, const wstring& filePath);
bool UnOccupyFile(HANDLE* hFile);

// PPT 加载页实现保留且固定禁用，旧配置不能重新开启。
inline constexpr bool kPptLoadingPageEnabled = false;

// PPT 控件位置写入日志：配置值保存在 Inkeys::config.PlugIn.PPTHelper，落盘写 main.json。
string CapturePptComPositionSettingJson(Inkeys::PptSettings::Positions positions, bool remember);
Inkeys::PptSettings::Positions SavedPptComPositions();
Inkeys::PptSettings::Positions RestorablePptComPositions();
string RetryPptComSettingJson();
bool WritePptComSettingJson(const string& jsonContent);

// 用户偏好（Enable/RunAsAdmin/SleepTime/Intercept）已迁入 Inkeys::config.PlugIn.DesktopDrawpadBlocker；
// 此结构只保留写出 interaction_configuration.json 所需的运行时字段。
struct DdbInteractionSetListStruct
{
	DdbInteractionSetListStruct()
	{
		DdbEdition = L"20261019a";
		DdbSHA256 = "8ca39cfc3d29ea42d4c79850e177637ecfb76bd61885cff39e451f0a46da82a1";

		mode = 1;
		hostPath = L"";
		restartHost = true;
	}

	wstring DdbEdition;
	string DdbSHA256;

	int mode; // 0 独立模式 1 随宿主程序和开启和关闭 2 随宿主程序关闭
	wstring hostPath;
	bool restartHost; // restartHost：（仅限独立模式）当宿主程序被关闭后，拦截到其他软件的窗口后，重启宿主程序
};
extern DdbInteractionSetListStruct ddbInteractionSetList;
//bool DdbReadInteraction();
string CaptureDdbInteractionJson(bool change, bool close);
bool WriteDdbInteractionJson(const string& jsonContent);
bool DdbWriteInteraction(bool change, bool close);

bool GetMemory();
bool SetMemory();

// 显示上下文就绪后初始化中档一次；本版暂停启动记忆/预设恢复，必须先于首次输入准入。
void InitializePenWidthPreferences(Inkeys::Business::PenWidth::Context context);
int RunPenWidthPreferencesProductionTest() noexcept;
