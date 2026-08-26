/*
 * @file		IdtPlug-in.cpp
 * @brief		IDT plugin linkage | 智绘教插件联动
 * @note		PPT linkage components and other plugins | PPT联动组件和其他插件等
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// INFO: This source file will take the lead in refactoring the code logic and optimizing the reading experience.
// 提示：这个源文件将率先重构代码逻辑，并优化阅读体验。

// 常问问题：一些注释后带有 '*' 号，它们的解释在下面。
//
// *1
//
// PptInfoStateBuffer 变量是 PptInfoState 变量的缓冲，当 DrawpadDrawing 函数加载完成 PPT 的画板后，缓冲变量中的值才会变为和 PptInfoState 一致。
// 一些函数获取 PptInfoStateBuffer 的值，必须要等到 PPT 画板初始化完毕后才会有所改变，并再做出反应。

import Inkeys.Conv.Color;
import Inkeys.Helper.Thread;
import Inkeys.Load;
import Inkeys.Message;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.UI.Bar;
import Inkeys.UI.Ppt;
import Inkeys.Other.Config;
import Inkeys.Window;

#include "IdtPlug-in.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "Inkeys/Business/LegacyDrawState.hpp"
#include "IdtMagnification.h"
#include "Inkeys/Window/Window.Legacy.hpp"
#include "IdtOther.h"
#include "IdtImage.h"
#include "IdtStart.h"
#include "IdtState.h"
#include "IdtI18n.h"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"

#include <objbase.h>
#include <psapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <condition_variable>
#include <deque>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
	enum class PptUiBusinessCommand : unsigned char
	{
		Previous,
		Next,
		ViewShow,
		EndShow,
		PersistPosition,
	};
	struct PptUiBusinessRequest
	{
		PptUiBusinessCommand command{};
		Inkeys::UI::Ppt::LayoutConfiguration layout{};
	};

	mutex pptUiBusinessMutex;
	condition_variable pptUiBusinessCondition;
	deque<PptUiBusinessRequest> pptUiBusinessCommands;
	atomic_bool pptUiPageCommandOutstanding = false;
	constexpr auto PptPageStateCheckInterval = chrono::milliseconds(50);
	constexpr auto PptVisibilityPublishInterval = chrono::milliseconds(500);

	void QueuePptUiBusinessCommand(PptUiBusinessCommand command)
	{
		const bool pageCommand = command == PptUiBusinessCommand::Previous ||
			command == PptUiBusinessCommand::Next;
		if (pageCommand && pptUiPageCommandOutstanding.exchange(true)) return;
		{
			lock_guard lock(pptUiBusinessMutex);
			pptUiBusinessCommands.push_back({ command });
		}
		pptUiBusinessCondition.notify_one();
	}

	void QueuePptUiPositionPersistence(
		Inkeys::UI::Ppt::LayoutConfiguration configuration)
	{
		{
			lock_guard lock(pptUiBusinessMutex);
			pptUiBusinessCommands.push_back({
				PptUiBusinessCommand::PersistPosition, configuration });
		}
		pptUiBusinessCondition.notify_one();
	}

	[[nodiscard]] bool IsInRect(long x, long y, const RECT& rect) noexcept
	{
		return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
	}
}

using namespace Inkeys;

// --------------------------------------------------
// PPT 联动插件

#import "PptCOM.tlb" // C# 类库 PptCOM 项目库 (PptCOM. cs)
using namespace PptCOM;
IPptCOMServerPtr PptCOMPto;
mutex pptComSlotSm;

IPptCOMServerPtr GetPptComSnapshot()
{
	lock_guard<mutex> lg(pptComSlotSm);
	return PptCOMPto;
}
void SetPptComSnapshot(IPptCOMServerPtr pptCom)
{
	lock_guard<mutex> lg(pptComSlotSm);
	PptCOMPto = pptCom;
}
void ResetPptComSnapshot()
{
	lock_guard<mutex> lg(pptComSlotSm);
	PptCOMPto = nullptr;
}

// -------------------------
// UI 对象


// -------------------------
// ppt 信息

PptImgStruct PptImg = { false }; // 其存储幻灯片放映时产生的图像数据。
PptInfoStateStruct PptInfoState = { -1, -1 }; // 其存储幻灯片放映软件当前的状态，First 代表总幻灯片页数，Second 代表当前幻灯片编号。
PptInfoStateStruct PptInfoStateBuffer = { -1, -1 }; // PptInfoState 的缓冲变量。*1
bool FreezePPT;
HWND ppt_show;
wstring ppt_title, ppt_software;
map<wstring, bool> ppt_title_recond;

wstring pptComVersion;
wstring pptComExtraWarning;

// -------------------------
// Ppt 状态

bool pptTakeoverConsumedInCurrentShow = false;

// -------------------------
// Ppt 主项

bool CheckPptCom()
{
	IPptCOMServerPtr pptCom;
	wstring version;
	wstring extraWarning;
	bool rel = false;

	try
	{
		_com_util::CheckError(pptCom.CreateInstance(_uuidof(PptCOMServer)));
		// 在任何 PptCOM 诊断输出前应用独立开关。
	#ifndef IDT_RELEASE
		pptCom->SetConsoleOutputEnabled(
			static_cast<bool>(Inkeys::config.Experimental.Inkeys3.ConsoleOutput.PptCOM));
	#else
		pptCom->SetConsoleOutputEnabled(false);
	#endif
	}
	catch (_com_error err)
	{
		pptComVersion = L"Error: 初始化异常1(C++) " + std::wstring(err.ErrorMessage()) +
			L" (0x" + std::to_wstring(err.Error()) + L") " +
			std::wstring((wchar_t*)err.Description() ? (wchar_t*)err.Description() : L"");

		return false;
	}

	try
	{
		version = pptCom->CheckCOM();
	}
	catch (_com_error err)
	{
		pptComVersion = L"Error: 初始化异常2(C++) " + std::wstring(err.ErrorMessage()) +
			L" (0x" + std::to_wstring(err.Error()) + L") " +
			std::wstring((wchar_t*)err.Description() ? (wchar_t*)err.Description() : L"");

		return false;
	}

	if (version.find(L"\n") != version.npos)
	{
		extraWarning = version.substr(version.find('\n') + 1);
		version = version.substr(0, version.find('\n'));

		//Testw(pptComExtraWarning);
		//Testw(pptComVersion);

		// TODO ？
	}

	try
	{
		rel = pptCom->Initialization(reinterpret_cast<long*>(&PptInfoState.TotalPage),
			reinterpret_cast<long*>(&PptInfoState.CurrentPage),
			GetOffSignalInteropPointer());
	}
	catch (_com_error err)
	{
		pptComVersion = L"Error: 初始化异常3(C++) " + std::wstring(err.ErrorMessage()) +
			L" (0x" + std::to_wstring(err.Error()) + L") " +
			std::wstring((wchar_t*)err.Description() ? (wchar_t*)err.Description() : L"");

		return false;
	}

	if (!rel)
	{
		pptComVersion = L"Error: 初始化异常3(C++) Initialization returned false";
		return false;
	}

	pptComVersion = version;
	pptComExtraWarning = extraWarning;
	SetPptComSnapshot(pptCom);

	return true;
}


wstring GetPptTitle()
{
	wstring ret = L"";
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return ret;

	try
	{
		ret = bstrToWstring(pptCom->SlideNameIndex());
	}
	catch (_com_error)
	{
	}

	return ret;
}
HWND GetPptShow()
{
	HWND hWnd = NULL;
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return hWnd;

	try
	{
		_variant_t result = pptCom->GetPptHwnd();
		hWnd = (HWND)result.llVal;
	}
	catch (_com_error)
	{
	}

	return hWnd;
}
int GetPptSlideShowAnnotationTool()
{
	int toolType = 0;
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return toolType;

	try
	{
		toolType = pptCom->GetSlideShowAnnotationTool();
	}
	catch (_com_error)
	{
	}

	return toolType;
}
bool ExitPptSlideShowAnnotationTool()
{
	bool ret = false;
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return ret;

	try
	{
		ret = pptCom->ExitSlideShowAnnotationTool();
	}
	catch (_com_error)
	{
	}

	return ret;
}
void GetPptState()
{
	Inkeys::Thread::StatusGuard guard("GetPptState");

	while (!offSignal)
	{
		if (!CheckPptCom())
		{
			ResetPptComSnapshot();
			PptInfoState.TotalPage = PptInfoState.CurrentPage = -1;

			for (int i = 0; i <= 20 && !offSignal; i++)
				this_thread::sleep_for(chrono::milliseconds(100));

			continue;
		}

		int tmp = -1;
		auto pptCom = GetPptComSnapshot();

		try
		{
			if (pptCom != nullptr) tmp = pptCom->PptComService();
		}
		catch (_com_error err)
		{
			pptComVersion = L"Error: 监测异常(C++) " + std::wstring(err.ErrorMessage()) +
				L" (0x" + std::to_wstring(err.Error()) + L") " +
				std::wstring((wchar_t*)err.Description() ? (wchar_t*)err.Description() : L"");
		}

		PptInfoState.TotalPage = PptInfoState.CurrentPage = -1;
		ResetPptComSnapshot();

		if (tmp <= 0)
		{
			for (int i = 0; i <= 20 && !offSignal; i++)
				this_thread::sleep_for(chrono::milliseconds(100));
		}
	}
}

void NextPptSlides(int check)
{
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return;

	try
	{
		pptCom->NextSlideShow((bool)(check == -1));
	}
	catch (_com_error)
	{
	}

	return;
}
void PreviousPptSlides()
{
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return;

	try
	{
		pptCom->PreviousSlideShow();
	}
	catch (_com_error)
	{
	}

	return;
}
void EndPptShow()
{
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return;

	try
	{
		FocusPptShow();
		pptCom->EndSlideShow();
	}
	catch (_com_error)
	{
	}

	return;
}
void ViewPptShow()
{
	auto pptCom = GetPptComSnapshot();
	if (pptCom == nullptr) return;

	try
	{
		FocusPptShow();
		pptCom->ViewSlideShow();
	}
	catch (_com_error)
	{
	}

	return;
}
void FocusPptShow()
{
	if (ppt_show != NULL)
	{
		SetForegroundWindow(ppt_show);
	}

	// 都需要保证激活
	{
		auto pptCom = GetPptComSnapshot();
		if (pptCom == nullptr) return;

		try
		{
			pptCom->ActivateSildeShowWindow();
		}
		catch (_com_error)
		{
		}
	}

	return;
}

bool StartPptTakeoverAnnotation(int toolType)
{
	if (toolType != 1) return false;

	// PPT 接管明确强制软笔，先覆盖笔型再发布，避免短暂发布 Laser。
	stateMode.laserActive = false;
	stateMode.Pen.ModeSelect = PenModeSelectEnum::IdtPenBrush1;
	bool res = true;
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen)
		res = ChangeStateModeToPen();
	else
		SyncDraw3State();

	barUISet.barButtonSet.UpdateDrawButtonStyle();
	barUISet.UpdateRendering();

	return res;
}

void PptInfo()
{
	Inkeys::Thread::StatusGuard guard("PptInfo");

	bool Initialization = false; // 控件初始化完毕
	int publishedCurrentPage = -2;
	int publishedTotalPage = -2;
	int publishedPresentationVisible = -1;
	auto nextVisibilityPublication = chrono::steady_clock::time_point::min();
	auto PublishPresentationVisibility = [&](bool visible)
		{
			const auto now = chrono::steady_clock::now();
			const int encoded = visible ? 1 : 0;
			if (publishedPresentationVisible == encoded
				&& now < nextVisibilityPublication) return;
			publishedPresentationVisible = encoded;
			nextVisibilityPublication = now + PptVisibilityPublishInterval;
			Inkeys::UI::Ppt::PublishPresentationVisible(visible);
		};
	auto WaitForPageStateProgress = []
		{
			const auto runtime = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
			if (runtime.running)
			{
				(void)Inkeys::Drawing::Draw3::WaitForProductRuntimeRevision(
					runtime.runtimeRevision,
					static_cast<std::uint32_t>(PptPageStateCheckInterval.count()));
			}
			else
				this_thread::sleep_for(PptPageStateCheckInterval);
		};
	for (; !offSignal;)
	{
		// COM 事件直接写共享状态；单轮使用一致快照，最迟 50ms 再复核。
		const int observedCurrentPage = PptInfoState.CurrentPage;
		const int observedTotalPage = PptInfoState.TotalPage;
		// Ppt 信息监测 | 控件信息加载
		if (!Initialization && observedTotalPage != -1)
		{
			pptTakeoverConsumedInCurrentShow = false;

			ppt_show = GetPptShow();

			std::wstringstream ss(GetPptTitle());
			getline(ss, ppt_title);
			getline(ss, ppt_software);

			if (ppt_software.find(L"WPS") != ppt_software.npos) ppt_software = L"WPS";
			else ppt_software = L"PowerPoint";

			// 刷新 UI
			barUISet.barButtonSet.UpdateDrawButtonStyle();
			barUISet.UpdateRendering();

			if (!ppt_title_recond[ppt_title] && pptComSetlist.showLoadingScreen) FreezePPT = true;
			Initialization = true;
		}
		else if (Initialization && observedTotalPage == -1)
		{
			pptTakeoverConsumedInCurrentShow = false;

			PptImg.IsSave = false;
			PptImg.IsSaved.clear();
			PptImg.Image.clear();

			ppt_show = NULL, ppt_software = L"";

			// 设置控件归位
			PptComReadSettingPositionOnly();
			// 刷新 UI
			barUISet.barButtonSet.UpdateDrawButtonStyle();
			barUISet.UpdateRendering();

			FreezePPT = false;
			Initialization = false;
			PptInfoStateBuffer.CurrentPage = -1;
			PptInfoStateBuffer.TotalPage = -1;
		}
		else if (Initialization && observedTotalPage != -1
			&& !WhiteboardTransactionActive()
			&& config.PlugIn.PPTHelper.AutoTakeOver
			&& !pptTakeoverConsumedInCurrentShow)
		{
			int toolType = GetPptSlideShowAnnotationTool();
			if (toolType == 1 && StartPptTakeoverAnnotation(toolType))
			{
				ExitPptSlideShowAnnotationTool();
				if (config.PlugIn.PPTHelper.AutoTakeOverOnce) pptTakeoverConsumedInCurrentShow = true;

				if (config.PlugIn.PPTHelper.AutoTakeOverExpand)
				{
					if (barUISet.barState.fold)
					{
						barUISet.barState.fold = false;
						barUISet.UpdateRendering();
					}
				}
			}
		}

		if (WhiteboardTransactionActive())
		{
			// 白板页码完全独立；隐藏 PPT 控件并强制退出后重新发布 COM 当前页。
			publishedCurrentPage = -2;
			publishedTotalPage = -2;
			PublishPresentationVisibility(false);
			WaitForPageStateProgress();
			continue;
		}

		if (observedCurrentPage > 0 && observedTotalPage > 0)
		{
			// Host 重启会清空 bridge；每轮幂等复核，且仅运行中的 Host 接受请求。
			const bool pageRequested = Inkeys::Drawing::Draw3::PublishProductPage(
				static_cast<std::uint32_t>(observedCurrentPage - 1));
			const auto draw3Snapshot = Inkeys::Drawing::Draw3::ProductHost().RuntimeSnapshot();
			if (pageRequested && draw3Snapshot.running &&
				draw3Snapshot.currentPageIndex ==
					static_cast<std::size_t>(observedCurrentPage - 1))
			{
				// UI 页码只反映 Draw3 文档已经完成的页面切换。
				PptInfoStateBuffer.CurrentPage = observedCurrentPage;
				PptInfoStateBuffer.TotalPage = observedTotalPage;
			}
		}
		else
		{
			PptInfoStateBuffer.CurrentPage = -1;
			PptInfoStateBuffer.TotalPage = -1;
		}

		PublishPresentationVisibility(Initialization);
		// 只发布画板已经完成换页后的缓冲页码，不能直接使用 COM 写入值。
		if (publishedCurrentPage != PptInfoStateBuffer.CurrentPage ||
			publishedTotalPage != PptInfoStateBuffer.TotalPage)
		{
			publishedCurrentPage = PptInfoStateBuffer.CurrentPage;
			publishedTotalPage = PptInfoStateBuffer.TotalPage;
			Inkeys::UI::Ppt::PublishPageState(
				publishedCurrentPage, publishedTotalPage);
		}

		// Draw3 完成页切换会立即唤醒；50ms 仅用于没有 native 事件的 COM 状态复核。
		WaitForPageStateProgress();
	}
}
void PPTLinkageMain()
{
	Inkeys::Thread::StatusGuard guard("PPTLinkageMain");

	// 读取 ppt 配置
	{
		if (_waccess((globalPath + L"opt\\pptcom_configuration.json").c_str(), 4) == 0) PptComReadSetting();
		PptComWriteSetting();
	}
	// 检查相关注册表项目
	pptComSetlist.setAdmin = IsPowerPointRunAsAdminSet();

	const bool pptUiReady = Inkeys::UI::Ppt::Initialize({
		[]() { QueuePptUiBusinessCommand(PptUiBusinessCommand::Previous); },
		[]() { QueuePptUiBusinessCommand(PptUiBusinessCommand::Next); },
		[]() { QueuePptUiBusinessCommand(PptUiBusinessCommand::ViewShow); },
		[]() { QueuePptUiBusinessCommand(PptUiBusinessCommand::EndShow); },
		[](Inkeys::UI::Ppt::LayoutConfiguration configuration)
		{
			QueuePptUiPositionPersistence(configuration);
		},
	});
	if (pptUiReady)
	{
		// 未放映时四个 PPT 分页窗口不会提交首帧；客户端注册完成即代表启动门禁已就绪。
		IdtWindowsIsVisible.pptWindow = true;
	}
	else if (IDTLogger)
		IDTLogger->error("[PPT 线程][PPTLinkageMain] UI3 四窗口客户端注册失败");

	thread(GetPptState).detach();
	thread(PptInfo).detach();

	while (!offSignal)
	{
		PptUiBusinessRequest request{};
		{
			unique_lock lock(pptUiBusinessMutex);
			pptUiBusinessCondition.wait_for(lock, chrono::milliseconds(100), []
				{
					return offSignal || !pptUiBusinessCommands.empty();
				});
			if (offSignal) break;
			if (pptUiBusinessCommands.empty()) continue;
			request = pptUiBusinessCommands.front();
			pptUiBusinessCommands.pop_front();
		}
		const auto command = request.command;

		// COM 与模态确认仍在 PPT 业务线程执行，不能阻塞唯一 UI3 渲染线程。
		if (command == PptUiBusinessCommand::PersistPosition)
		{
			// 只合并位置字段，避免拖动完成时覆盖 Settings 的显示与缩放配置。
			pptComSetlist.bottomBothWidth = request.layout.bottomPairWidth;
			pptComSetlist.bottomBothHeight = request.layout.bottomPairHeight;
			pptComSetlist.middleBothWidth = request.layout.middlePairWidth;
			pptComSetlist.middleBothHeight = request.layout.middlePairHeight;
			pptComSetlist.bottomMiddleWidth = request.layout.exitWidth;
			pptComSetlist.bottomMiddleHeight = request.layout.exitHeight;
			PptComWriteSetting();
			continue;
		}
		if (command == PptUiBusinessCommand::Previous)
		{
			FocusPptShow();
			PreviousPptSlides();
			pptUiPageCommandOutstanding = false;
			continue;
		}
		if (command == PptUiBusinessCommand::Next)
		{
			const int current = PptInfoState.CurrentPage;
			if (current != -1)
			{
				FocusPptShow();
				NextPptSlides(current);
			}
			// 放映已结束时丢弃过期翻页请求，不能落入结束放映命令。
			pptUiPageCommandOutstanding = false;
			continue;
		}
		if (command == PptUiBusinessCommand::ViewShow)
		{
			ViewPptShow();
			continue;
		}
		if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection)
		{
			if (!CheckEndShow.Check())
			{
				Inkeys::UI::Bar::CompleteEndShowRequest();
				continue;
			}
			ChangeStateModeToSelection();
		}
		EndPptShow();
		Inkeys::UI::Bar::CompleteEndShowRequest();
	}
	Inkeys::UI::Ppt::Shutdown();
	{
		lock_guard lock(pptUiBusinessMutex);
		pptUiBusinessCommands.clear();
		pptUiPageCommandOutstanding = false;
	}

	int i = 1;
	for (; i <= 5; i++)
	{
		using namespace Inkeys::Thread;

		if (!GetStatus("GetPptState") && !GetStatus("PptInfo")) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}
}

// 附加
bool IsPowerPointRunAsAdminSet()
{
	const std::wstring subKeys[] = {
		L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"
	};

	HKEY hRoots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

	for (HKEY hRoot : hRoots)
	{
		for (const std::wstring& subKey : subKeys)
		{
			HKEY hKey;
			if (RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				DWORD valueCount = 0;
				DWORD maxValueNameLen = 0;
				DWORD maxValueDataLen = 0;

				if (RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr) == ERROR_SUCCESS)
				{
					maxValueNameLen++;
					maxValueDataLen++;

					std::wstring valueName(maxValueNameLen, L'\0');
					std::vector<BYTE> data(maxValueDataLen);

					for (DWORD i = 0; i < valueCount; ++i)
					{
						DWORD valueNameLen = maxValueNameLen;
						DWORD dataSize = maxValueDataLen;
						DWORD type = 0;

						if (RegEnumValueW(hKey, i, &valueName[0], &valueNameLen, nullptr, &type, data.data(), &dataSize) == ERROR_SUCCESS)
						{
							valueName.resize(valueNameLen);

							std::wstring lowerValueName = valueName;
							std::transform(lowerValueName.begin(), lowerValueName.end(), lowerValueName.begin(), ::towlower);

							if (lowerValueName.find(L"powerpnt.exe") != std::wstring::npos || lowerValueName.find(L"ksolaunch.exe") != std::wstring::npos)
							{
								if (type == REG_SZ)
								{
									std::wstring dataStr(reinterpret_cast<WCHAR*>(data.data()), dataSize / sizeof(WCHAR) - 1);

									std::wstring lowerDataStr = dataStr;
									std::transform(lowerDataStr.begin(), lowerDataStr.end(), lowerDataStr.begin(), ::towlower);

									if (lowerDataStr.find(L"runasadmin") != std::wstring::npos)
									{
										RegCloseKey(hKey);
										return true;
									}
								}
							}

							valueName.assign(maxValueNameLen, L'\0');
							data.assign(maxValueDataLen, 0);
						}
					}
				}
				RegCloseKey(hKey);
			}
		}
	}
	return false;
}

bool CheckEndShowClass::Check()
{
	if (isChecking == true) return false;
	// isChecking = true;

	// 延迟0.5秒后放开键盘
	auto delayed = async(launch::async, [&]() {
		this_thread::sleep_for(std::chrono::milliseconds(500));
		isChecking = true;
		});

	bool ret = (MessageBox(floating_window, L"Currently in drawing mode, continuing to end will clear the canvas.\nAre you sure you want to end the presentation?\n当前处于绘制模式，继续结束放映将会清空画布内容。\n确定结束放映？", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OKCANCEL) == 1);

	isChecking = false;
	return ret;
}
CheckEndShowClass CheckEndShow;

// --------------------------------------------------
// 其他插件

// DesktopDrawpadBlocker 插件
void StartDesktopDrawpadBlocker()
{
	if (ddbInteractionSetList.enable)
	{
		// 配置 json
		{
			// if (_waccess((pluginPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json").c_str(), 0) == 0) DdbReadInteraction();

			ddbInteractionSetList.hostPath = GetCurrentExePath();

			ddbInteractionSetList.mode = 1;
			ddbInteractionSetList.restartHost = true;
		}

		// 配置 EXE
		if (_waccess((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
		{
			if (_waccess((pluginPath + L"DesktopDrawpadBlocker").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directories(pluginPath + L"DesktopDrawpadBlocker", ec);
			}
			Inkeys::Load::ExtractResourceFile((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
		}
		else
		{
			string hash_sha256;
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFileW(pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe");
				delete myWrapper;
			}

			if (hash_sha256 != ddbInteractionSetList.DdbSHA256)
			{
				if (isProcessRunning((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
				{
					// 需要关闭旧版 DDB 并更新版本

					DdbWriteInteraction(true, true);
					for (int i = 1; i <= 20; i++)
					{
						if (!isProcessRunning((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
							break;
						this_thread::sleep_for(chrono::milliseconds(500));
					}
				}
				Inkeys::Load::ExtractResourceFile((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
			}
		}

		// 启动 DDB
		if (!isProcessRunning((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
		{
			DdbWriteInteraction(true, false);
			if (ddbInteractionSetList.runAsAdmin) ShellExecuteW(NULL, L"runas", (pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
			else ShellExecuteW(NULL, NULL, (pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	}
	else if (_waccess((pluginPath + L"DesktopDrawpadBlocker").c_str(), 0) == 0)
	{
		error_code ec;
		filesystem::remove_all(pluginPath + L"DesktopDrawpadBlocker", ec);
	}
}

// 快捷方式保障助手 插件
ShortcutAssistantClass shortcutAssistant;
void ShortcutAssistantClass::SetShortcut()
{
	wchar_t desktopPath[MAX_PATH];
	wstring DesktopPath;

	if (SHGetSpecialFolderPathW(0, desktopPath, CSIDL_DESKTOP, FALSE)) DesktopPath = wstring(desktopPath) + L"\\";
	else return;

	if (setlist.shortcutAssistant.correctLnk)
	{
		if (_waccess((DesktopPath + IW("Widget/LnkName") + L".lnk").c_str(), 0) == 0)
		{
			// 存在对应名称的 Lnk
			if (!IsShortcutPointingToDirectory(DesktopPath + IW("Widget/LnkName") + L".lnk", GetCurrentExePath()))
			{
				// 不指向当前的程序路径
				error_code ec;
				filesystem::remove(DesktopPath + IW("Widget/LnkName") + L".lnk", ec);

				CreateShortcut(DesktopPath + IW("Widget/LnkName").c_str() + L".lnk", GetCurrentExePath());
			}
		}
		{
			for (const auto& entry : filesystem::directory_iterator(DesktopPath))
			{
				if (filesystem::is_regular_file(entry) && entry.path().extension() == L".lnk")
				{
					if (entry.path().wstring() != DesktopPath + IW("Widget/LnkName").c_str() + L".lnk" && IsShortcutPointingToDirectory(entry.path().wstring(), GetCurrentExePath()))
					{
						// 存在指向当前的程序路径的快捷方式，但是其名称并不正确
						error_code ec;
						filesystem::remove(entry.path().wstring(), ec);

						CreateShortcut(DesktopPath + IW("Widget/LnkName") + L".lnk", GetCurrentExePath());
					}
				}
			}
		}
	}
	if (setlist.shortcutAssistant.createLnk)
	{
		if (_waccess((DesktopPath + IW("Widget/LnkName").c_str() + L".lnk").c_str(), 0) == -1 ||
			!IsShortcutPointingToDirectory((DesktopPath + IW("Widget/LnkName").c_str() + L".lnk"), GetCurrentExePath()))
		{
			CreateShortcut(DesktopPath + IW("Widget/LnkName").c_str() + L".lnk", GetCurrentExePath());
		}
	}

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	return;
}
bool ShortcutAssistantClass::IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory)
{
	IShellLink* psl;
	//CoInitialize(NULL);

	// 创建一个IShellLink对象
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
	if (SUCCEEDED(hres))
	{
		IPersistFile* ppf;

		// 获取IShellLink的IPersistFile接口
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
		if (SUCCEEDED(hres))
		{
			// 打开快捷方式文件
			hres = ppf->Load(shortcutPath.c_str(), STGM_READ);
			if (SUCCEEDED(hres))
			{
				WIN32_FIND_DATAW wfd;
				ZeroMemory(&wfd, sizeof(wfd));
				// 获取快捷方式的目标路径
				hres = psl->GetPath(wfd.cFileName, MAX_PATH, NULL, SLGP_RAWPATH);
				if (SUCCEEDED(hres))
				{
					// 检查目标路径是否与指定目录相匹配
					if (std::wstring(wfd.cFileName).find(targetDirectory) != std::wstring::npos)
					{
						return true;
					}
				}
			}
			ppf->Release();
		}
		psl->Release();
	}
	//CoUninitialize();

	return false;
}
bool ShortcutAssistantClass::CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath)
{
	//CoInitialize(NULL);

	// 创建一个IShellLink对象
	IShellLink* psl;
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);

	if (SUCCEEDED(hres))
	{
		// 设置快捷方式的目标路径
		psl->SetPath(targetExePath.c_str());

		// 创建一个IShellLink对象的IPersistFile接口
		IPersistFile* ppf;
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

		if (SUCCEEDED(hres))
		{
			// 保存快捷方式
			hres = ppf->Save(shortcutPath.c_str(), TRUE);
			ppf->Release();
		}

		psl->Release();
	}

	//CoUninitialize();

	return SUCCEEDED(hres);
}
