#include "IdtDrawpad.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtRts.h"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"

#include <utility>

// 兼容旧 UI 的符号只保留存根；Draw3 bridge 是产品绘制状态的唯一来源。
bool main_open = false;
bool FirstDraw = true;
bool IdtHotkey = false;
StrokeImageClass strokeImage;
std::shared_mutex StrokeImageListSm;
std::vector<StrokeImageClass*> StrokeImageList;
std::shared_mutex StrokeBackImageSm;
bool drawWaiting = false;
std::shared_mutex drawWaitingSm;
Inkeys::Graphics::DibSurface drawpad;
Inkeys::Graphics::DibSurface window_background;
HHOOK DrawpadHookCall = nullptr;

IdtAtomic<bool> rtsDown = false;
IdtAtomic<int> rtsNum = 0;
IdtAtomic<int> touchNum = 0;
IdtAtomic<int> inkNum = 0;
std::unordered_map<LONG, std::pair<int, int>> PreviousPointPosition;
std::unordered_map<LONG, double> TouchSpeed;
std::unordered_map<LONG, TouchMode> TouchPos;
std::vector<LONG> TouchList;
IdtAtomic<unsigned short> TouchCnt = 0;
std::deque<TouchInfo> TouchTemp;
std::shared_mutex touchPosSm;
std::shared_mutex touchSpeedSm;
std::shared_mutex pointListSm;
std::shared_mutex touchTempSm;
std::shared_mutex touchPointerSm;
IRealTimeStylus* g_pRealTimeStylus = nullptr;
IStylusSyncPlugin* g_pSyncEventHandlerRTS = nullptr;
IdtAtomic<LONG> leftButtonPid = 0;
IdtAtomic<LONG> rightButtonPid = 0;

LRESULT CALLBACK DrawpadHookCallback(int, WPARAM, LPARAM)
{
	return CallNextHookEx(nullptr, 0, 0, 0);
}

void DrawpadInstallHook()
{
	// Draw3 输入由唯一 RTS producer 接管，旧低级键盘 hook 不再安装。
}

void ResetPrepareCanvas()
{
	// Draw3 尚未提供落笔预备画布设置，保留兼容空接口。
}

int drawpad_main()
{
	// 产品启动由 Window Service -> Draw3::StartProduct 完成。
	return 0;
}

IRealTimeStylus* CreateRealTimeStylus(HWND)
{
	// 禁止旧 RTS 与 Draw3 producer 同时绑定同一 HWND。
	return nullptr;
}

bool EnableRealTimeStylus(IRealTimeStylus*)
{
	return false;
}

void RTSSpeed()
{
	// 速度橡皮由 Draw3::SpeedEraserOcController 管理。
}

void HandleMouseInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (Inkeys::Drawing::Draw3::ProductRunning())
		(void)Inkeys::Drawing::Draw3::ForwardProductMessage(hWnd, msg, wParam, lParam);
}

void InitRTSLogic()
{
	// Draw3 Host 初始化唯一 RTS；旧入口刻意保持空实现。
}

void removeEmptyFolders(std::wstring)
{
}

void removeUnknownFiles(std::wstring, std::deque<std::wstring>)
{
}

std::deque<std::wstring> getPrevTwoDays(const std::wstring&, int)
{
	return {};
}

int current_record_pointer = 0;
int total_record_pointer = 0;
int reference_record_pointer = 0;
int practical_total_record_pointer = 0;
Json::Value record_value;

void LoadDrawpad()
{
	// Draw3 文档尚未接入文件恢复，不能伪造加载成功。
}

void SaveScreenShot(Inkeys::Graphics::DibSurface, bool)
{
	// 保存能力尚未准备好，产品路径不再写入 Draw2 快照文件。
}

void IdtRecall()
{
	(void)Inkeys::Drawing::Draw3::PublishProductCommand(
		Inkeys::Drawing::Draw3::Bridge::CommandType::Undo);
}

void IdtRecovery()
{
	(void)Inkeys::Drawing::Draw3::PublishProductCommand(
		Inkeys::Drawing::Draw3::Bridge::CommandType::Redo);
}
