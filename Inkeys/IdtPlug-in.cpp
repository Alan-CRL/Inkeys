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
import Inkeys.UI.MessageBox;
import Inkeys.UI.Freeze;
import Inkeys.Other.Config;
import Inkeys.Window;
import Inkeys.Startup.Progress;

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
#include "IdtI18nKeys.g.h"
#include "Inkeys/Drawing/Draw3/Draw3.Presentation.h"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Drawing/Draw3/Draw3.PptTiming.h"
#include "Inkeys/Business/PptSession.h"

#ifdef MessageBox
#undef MessageBox
#endif

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
	using Inkeys::Business::PptSessionToken;
	mutex pptSessionMutex;
	PptSessionToken currentPptSession;
	std::optional<Inkeys::Drawing::Draw3::Bridge::PresentationTarget> expectedPptUiTarget;
	std::atomic_uint64_t pptComGeneration = 0;
	std::atomic_bool pptExitDialogActive = false;

	PptSessionToken CapturePptSession()
	{
		lock_guard lock(pptSessionMutex);
		return currentPptSession;
	}
	bool IsCurrentPptSession(const PptSessionToken& token)
	{
		return token.serviceGeneration == pptComGeneration.load(std::memory_order_acquire) &&
			Inkeys::Business::MatchesPptSession(token, CapturePptSession());
	}
	enum class PptUiBusinessCommand : unsigned char
	{
		Previous, Next, ViewShow, EndShow, ConfirmEndShow, PersistSettings, Focus,
	};
	struct PptUiBusinessRequest
	{
		PptUiBusinessCommand command{};
		PptSessionToken session{};
		std::uint64_t requestId = 0;
		std::string settingsPayload;
	};
	mutex pptUiBusinessMutex;
	condition_variable pptUiBusinessCondition;
	deque<PptUiBusinessRequest> pptUiBusinessCommands;
	atomic_bool pptUiPageCommandOutstanding = false;
	constexpr auto PptPageStateCheckInterval = chrono::milliseconds(16);
	constexpr auto PptIdleStateCheckInterval = chrono::milliseconds(100);
	constexpr auto PptVisibilityPublishInterval = chrono::milliseconds(500);

	void QueuePptUiBusinessCommand(PptUiBusinessCommand command, std::uint64_t requestId = 0)
	{
		const bool pageCommand = command == PptUiBusinessCommand::Previous ||
			command == PptUiBusinessCommand::Next;
		if (pageCommand && pptUiPageCommandOutstanding.exchange(true)) return;
		PptUiBusinessRequest request{ command, CapturePptSession(), requestId };
		try
		{
			lock_guard lock(pptUiBusinessMutex);
			pptUiBusinessCommands.push_back(std::move(request));
		}
		catch (...)
		{
			if (pageCommand) pptUiPageCommandOutstanding = false;
			if (requestId) Inkeys::UI::Bar::CompleteEndShowRequest(requestId);
			return;
		}
		pptUiBusinessCondition.notify_one();
	}
	void QueuePptUiSettingsPersistence(std::string payload)
	{
		PptUiBusinessRequest request{ PptUiBusinessCommand::PersistSettings };
		request.settingsPayload = std::move(payload);
		{
			lock_guard lock(pptUiBusinessMutex);
			pptUiBusinessCommands.push_back(std::move(request));
		}
		pptUiBusinessCondition.notify_one();
	}
	void AcknowledgePptPageUi(std::uint64_t session, std::uint64_t revision, int page, int total)
	{
		std::optional<Inkeys::Drawing::Draw3::Bridge::PresentationReadyIdentity> ready;
		{
			lock_guard lock(pptSessionMutex);
			if (!currentPptSession.active || currentPptSession.localSession != session
				|| currentPptSession.serviceGeneration != pptComGeneration.load(std::memory_order_acquire)
				|| !expectedPptUiTarget || expectedPptUiTarget->targetRevision != revision
				|| (expectedPptUiTarget->pageKind ==
					Inkeys::Drawing::Draw3::Bridge::PresentationPageKind::EndScreen
					? page != -1 || expectedPptUiTarget->pageIndex != expectedPptUiTarget->totalPages
					: page <= 0 || expectedPptUiTarget->pageIndex != static_cast<std::uint32_t>(page - 1))
				|| total <= 0 || expectedPptUiTarget->totalPages != static_cast<std::uint32_t>(total)) return;
			ready = Inkeys::Drawing::Draw3::Bridge::ReadyIdentityFor(*expectedPptUiTarget);
		}
		(void)Inkeys::Drawing::Draw3::PublishProductPresentationUiReady(*ready);
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
	pptComGeneration.fetch_add(1, std::memory_order_release);
}
void ResetPptComSnapshot()
{
	lock_guard<mutex> lg(pptComSlotSm);
	PptCOMPto = nullptr;
	pptComGeneration.fetch_add(1, std::memory_order_release);
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
namespace
{
	IPptCOMSessionStatePtr QueryPptSessionApi(const IPptCOMServerPtr& server)
	{
		IPptCOMSessionStatePtr result;
		if (server) (void)server->QueryInterface(__uuidof(IPptCOMSessionState),
			reinterpret_cast<void**>(&result));
		return result;
	}
	bool FocusPptSession(const PptSessionToken& token)
	{
		if (!IsCurrentPptSession(token) || pptExitDialogActive.load()
			|| Inkeys::UI::MessageBox::IsShowing()
			|| !Inkeys::UI::Bar::PptBusinessFocusAllowed()) return false;
		const HWND target = reinterpret_cast<HWND>(token.showWindow);
		DWORD processId = 0;
		if (!target || !IsWindow(target) || !IsWindowVisible(target)
			|| !GetWindowThreadProcessId(target, &processId) || processId != token.processId) return false;
		const HWND foreground = GetForegroundWindow();
		if (foreground == target) return true;
		const HWND settings = Inkeys::Window::GetService().Handle(Inkeys::Window::WindowRole::Setting);
		if (settings && (foreground == settings || IsChild(settings, foreground))) return false;
		DWORD foregroundProcess = 0;
		if (foreground) GetWindowThreadProcessId(foreground, &foregroundProcess);
		// 显式操作后只从本程序/当前 Office 交接；排队期间用户切到其他应用则放弃。
		if (foregroundProcess != GetCurrentProcessId() && foregroundProcess != token.processId) return false;
		if (!IsCurrentPptSession(token)) return false;
		(void)SetForegroundWindow(target);
		const bool focused = GetForegroundWindow() == target;
		if (!focused && IDTLogger) IDTLogger->debug("[PPT] foreground handoff denied session={}", token.localSession);
		return focused;
	}
}
void FocusPptShow()
{
	(void)FocusPptSession(CapturePptSession());
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

bool PptSessionActive()
{
	return CapturePptSession().active;
}

void PptInfo()
{
	Inkeys::Thread::StatusGuard guard("PptInfo");
	namespace D3 = Inkeys::Drawing::Draw3;
	using namespace Inkeys::Business;
	std::uint64_t localSessionSequence = 0;
	std::uint64_t observedServiceGeneration = UINT64_MAX;
	std::uint64_t observedStateRevision = 0;
	IPptCOMSessionStatePtr sessionApi;
	std::optional<PptSessionSnapshot> cachedState;
	std::optional<D3::Bridge::PresentationTarget> cachedTarget;
	int lastRawPage = -2, lastRawTotal = -2;
	std::int64_t firstObservationQpc = 0, descriptorAcceptanceQpc = 0;
	std::uint64_t tracedTargetRevision = 0;
	bool observationPending = false;
	int publishedPage = -2, publishedTotal = -2;
	std::uint64_t publishedTarget = UINT64_MAX, publishedSession = 0;
	int publishedVisible = -1;
	auto nextVisibility = chrono::steady_clock::time_point::min();
	auto nextMaintenance = chrono::steady_clock::time_point::min();
	std::string lastIssue;
	auto ReportIssue = [&](std::string issue)
	{
		if (lastIssue == issue) return;
		lastIssue = std::move(issue);
		if (!lastIssue.empty() && IDTLogger)
			IDTLogger->warn("[PPT] session state unavailable: {}", lastIssue);
	};
	auto TraceTrueExit = [](const char* reason, const PptSessionToken& session)
	{
		static const bool enabled = []
		{
			wchar_t value[8]{};
			return GetEnvironmentVariableW(L"INKEYS_PPT_EXIT_TRACE", value, 8) > 0 &&
				value[0] == L'1';
		}();
		if (enabled && IDTLogger)
			IDTLogger->info("[PptExitSurface] edge={} session={} service={} show={} binding={}",
				reason, session.localSession, session.serviceGeneration,
				session.showSessionRevision, session.bindingRevision);
	};
	auto EndSession = [&]
	{
		auto session = CapturePptSession();
		if (!session.active) return;
		{
			lock_guard lock(pptSessionMutex);
			currentPptSession.active = false;
			expectedPptUiTarget.reset();
		}
		Inkeys::UI::Ppt::PublishSession(session.localSession, false, nullptr);
		Inkeys::UI::Freeze::SetPresentationActive(false);
		pptTakeoverConsumedInCurrentShow = false;
		ppt_show = nullptr;
		ppt_software.clear();
		FreezePPT = false;
		PptImg.IsSave = false;
		PptImg.IsSaved.clear();
		PptImg.Image.clear();
		PptInfoStateBuffer = { -1, -1 };
		cachedTarget.reset();
		barUISet.barButtonSet.UpdateDrawButtonStyle();
		barUISet.UpdateRendering();
	};
	while (!offSignal)
	{
		// 先取得等待基准；判断 ready 以后不能换成一个刚更新的 revision。
		const auto runtimeBefore = D3::ProductRuntimeSnapshot();
		const int rawPage = PptInfoState.CurrentPage;
		const int rawTotal = PptInfoState.TotalPage;
		LARGE_INTEGER observationQpc{};
		QueryPerformanceCounter(&observationQpc);
		const bool rawChanged = rawPage != lastRawPage || rawTotal != lastRawTotal;
		if (rawChanged)
		{
			firstObservationQpc = observationQpc.QuadPart;
			observationPending = true;
			lastRawPage = rawPage; lastRawTotal = rawTotal;
		}
		const auto serviceGeneration = pptComGeneration.load(std::memory_order_acquire);
		const auto server = GetPptComSnapshot();
		if (serviceGeneration != observedServiceGeneration)
		{
			sessionApi = QueryPptSessionApi(server);
			observedServiceGeneration = serviceGeneration;
			observedStateRevision = 0;
			cachedState.reset();
			cachedTarget.reset();
		}
		bool stateReliable = false;
		bool exitedThisTick = false;
		std::uint64_t exitModeRevision = 0;
		bool descriptorChanged = false;
		PptLifecycle lifecycle = PptLifecycle::Unknown;
		PptPageStatus pageStatus = PptPageStatus::Unknown;
		std::optional<D3::PresentationDescriptor> legacyDescriptor;
		try
		{
			if (sessionApi)
			{
				const auto json = bstrToWstring(sessionApi->GetSlideShowState(
					static_cast<__int64>(observedStateRevision)));
				if (!json.empty())
				{
					auto parsed = ParsePptSessionSnapshot(json);
					if (parsed.snapshot && parsed.snapshot->stateRevision >= observedStateRevision)
					{
						cachedState = std::move(parsed.snapshot);
						observedStateRevision = cachedState->stateRevision;
						descriptorChanged = true;
						if (!observationPending) firstObservationQpc = observationQpc.QuadPart;
						ReportIssue({});
					}
					else { ReportIssue(parsed.error); cachedState.reset(); }
				}
				stateReliable = cachedState.has_value();
				if (cachedState)
				{
					lifecycle = cachedState->lifecycle;
					pageStatus = cachedState->pageStatus;
				}
			}
			else if (server)
			{
				// 旧 DLL 保留既有可信 descriptor 路径；确认退出不会使用此降级身份。
				stateReliable = true;
				lifecycle = rawTotal > 0 ? PptLifecycle::Active : PptLifecycle::Inactive;
				if (rawPage > 0 && rawTotal > 0)
				{
					auto parsed = D3::ParsePresentationDescriptorJson(
						bstrToWstring(server->GetPresentationDescriptor()));
					legacyDescriptor = std::move(parsed.descriptor);
					pageStatus = legacyDescriptor ? PptPageStatus::Valid : PptPageStatus::Unknown;
					descriptorChanged = true;
				}
				else if (rawTotal > 0 && rawPage < 0) pageStatus = PptPageStatus::EndScreen;
			}
		}
		catch (const _com_error& error)
		{
			ReportIssue("com:" + std::to_string(static_cast<long>(error.Error())));
		}
		if (serviceGeneration != pptComGeneration.load(std::memory_order_acquire))
			stateReliable = false;
		const D3::PresentationDescriptor* descriptor = sessionApi
			? (cachedState ? &cachedState->descriptor : nullptr)
			: (legacyDescriptor ? &*legacyDescriptor : nullptr);
		if (stateReliable && lifecycle == PptLifecycle::Inactive)
		{
			const auto ending = CapturePptSession();
			exitedThisTick = ending.active;
			if (ending.active) exitModeRevision = StateModeTransitionRevision();
			if (ending.active) TraceTrueExit("inactive", ending);
			EndSession();
		}
		bool trustedPage = stateReliable && lifecycle == PptLifecycle::Active
			&& pageStatus == PptPageStatus::Valid && descriptor
			&& rawPage > 0 && rawTotal > 0
			&& descriptor->currentPage == static_cast<std::uint32_t>(rawPage)
			&& descriptor->totalPage == static_cast<std::uint32_t>(rawTotal);
		bool trustedEndScreen = stateReliable && sessionApi &&
			lifecycle == PptLifecycle::Active && pageStatus == PptPageStatus::EndScreen &&
			descriptor && descriptor->status == D3::PresentationDescriptorStatus::StableSlideIds &&
			descriptor->currentPage == 0 && descriptor->totalPage > 0;
		auto session = CapturePptSession();
		bool confirmedWindowEnd = false;
		if (session.active)
		{
			const HWND capturedWindow = reinterpret_cast<HWND>(session.showWindow);
			DWORD ownerProcess = 0;
			// COM/服务槽暂时不可用不是退出；已销毁或被别的进程复用的窗口才证明旧场次结束。
			const bool windowGone = !IsWindow(capturedWindow);
			const bool windowReused = !windowGone &&
				GetWindowThreadProcessId(capturedWindow, &ownerProcess) != 0 &&
				ownerProcess != session.processId;
			if ((windowGone || windowReused) && MatchesPptSession(session, CapturePptSession()))
			{
				confirmedWindowEnd = true;
				exitedThisTick = true;
				exitModeRevision = StateModeTransitionRevision();
				TraceTrueExit(windowGone ? "show-window-lost" : "show-window-reused", session);
				EndSession();
				session = CapturePptSession();
			}
		}
		if (stateReliable && lifecycle == PptLifecycle::Active && descriptor
			&& (trustedPage || pageStatus == PptPageStatus::EndScreen))
		{
			const auto showRevision = sessionApi ? cachedState->showSessionRevision : 0;
			HWND showWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(descriptor->slideShowHwnd));
			if (!showWindow && !sessionApi) showWindow = GetPptShow();
			DWORD processId = 0;
			if (showWindow && IsWindow(showWindow)) GetWindowThreadProcessId(showWindow, &processId);
			const bool descriptorWindowMatches = processId != 0 &&
				(descriptor->applicationProcessId == 0 ||
					processId == static_cast<DWORD>(descriptor->applicationProcessId));
			// 扩展会话一旦可靠结束，迟到的同场缓存不能以复用 HWND 再次开启。
			const bool endedSessionSnapshot = sessionApi && !session.active && session.localSession != 0 &&
				session.serviceGeneration == serviceGeneration && session.showSessionRevision == showRevision;
			if (!descriptorWindowMatches || endedSessionSnapshot)
			{
				// 窗口身份校验同时约束场次和画布目标，不能借旧场次发布另一窗口的缓存。
				trustedPage = false;
				trustedEndScreen = false;
				pageStatus = PptPageStatus::Unknown;
			}
			if (descriptorWindowMatches && !endedSessionSnapshot &&
				(!session.active || session.serviceGeneration != serviceGeneration
				|| session.showSessionRevision != showRevision
				|| session.bindingRevision != descriptor->bindingRevision
				|| session.showWindow != reinterpret_cast<std::uintptr_t>(showWindow)))
			{
				EndSession();
				session = { ++localSessionSequence, serviceGeneration, showRevision,
					descriptor->bindingRevision, reinterpret_cast<std::uintptr_t>(showWindow),
					processId, true, sessionApi != nullptr };
				{
					lock_guard lock(pptSessionMutex);
					currentPptSession = session;
				}
				ppt_show = showWindow;
				std::wstringstream title(GetPptTitle());
				getline(title, ppt_title);
				getline(title, ppt_software);
				ppt_software = ppt_software.find(L"WPS") != std::wstring::npos ? L"WPS" : L"PowerPoint";
				if (!ppt_title_recond[ppt_title] && pptComSetlist.showLoadingScreen) FreezePPT = true;
				pptTakeoverConsumedInCurrentShow = false;
				Inkeys::UI::Ppt::PublishSession(session.localSession, true, showWindow);
				Inkeys::UI::Freeze::SetPresentationActive(true);
				barUISet.barButtonSet.UpdateDrawButtonStyle();
				barUISet.UpdateRendering();
				QueuePptUiBusinessCommand(PptUiBusinessCommand::Focus);
				descriptorChanged = true;
			}
			if ((trustedPage || trustedEndScreen) && session.active &&
				(descriptorChanged || !cachedTarget))
			{
				cachedTarget = trustedEndScreen
					? D3::ResolveEndScreenTarget(*descriptor)
					: D3::ResolvePresentationTarget(*descriptor);
				if (cachedTarget) cachedTarget->sessionRevision = session.localSession;
				LARGE_INTEGER acceptedAt{};
				QueryPerformanceCounter(&acceptedAt);
				descriptorAcceptanceQpc = acceptedAt.QuadPart;
			}
		}
		// COM getter 之后服务槽仍可能换代；旧缓存不能直接进入新的 Host/UI 交接。
		const bool currentService = serviceGeneration == pptComGeneration.load(std::memory_order_acquire);
		if (!currentService)
		{
			stateReliable = false;
			trustedPage = false;
			trustedEndScreen = false;
			pageStatus = PptPageStatus::Unknown;
		}
		if (cachedTarget && (session.serviceGeneration != serviceGeneration ||
			cachedTarget->sessionRevision != session.localSession ||
			cachedTarget->bindingRevision != session.bindingRevision))
		{
			trustedPage = false;
			trustedEndScreen = false;
		}
		const bool trustedTarget = cachedTarget &&
			((trustedPage && cachedTarget->pageKind == D3::Bridge::PresentationPageKind::Slide) ||
				(trustedEndScreen && cachedTarget->pageKind ==
					D3::Bridge::PresentationPageKind::EndScreen));
		const bool whiteboard = WhiteboardTransactionActive();
		const auto bridge = D3::ProductHost().ProductBridge().Snapshot();
		if (!whiteboard && session.active && !trustedTarget)
		{
			const bool sameBinding = currentService && session.serviceGeneration == serviceGeneration
				&& (!cachedState || (cachedState->bindingRevision == session.bindingRevision
					&& cachedState->showSessionRevision == session.showSessionRevision));
			if (sameBinding && bridge.presentationTarget)
				(void)D3::SetProductPresentationInputSuspended(
					D3::Bridge::ReadyIdentityFor(*bridge.presentationTarget), true);
			else D3::ClearProductPresentationTarget();
		}
		std::uint64_t targetRevision = 0;
		if (!whiteboard && !session.active && (confirmedWindowEnd ||
			(stateReliable && lifecycle == PptLifecycle::Inactive)))
		{
			D3::PublishProductWorkspace(D3::Bridge::Workspace::Desktop);
			if (exitedThisTick)
			{
				// 可信退出只收尾一次；若此后用户主动换工具，旧退出不得覆盖它。
				(void)ChangeStateModeToSelectionIfRevision(exitModeRevision);
			}
		}
		else if (!whiteboard && session.active && trustedTarget)
		{
			const auto accepted = D3::PublishProductPresentationTarget(*cachedTarget);
			if (accepted)
			{
				if (tracedTargetRevision != *accepted)
				{
					// 目标 revision 确定后补记原始 QPC，所有阶段使用同一关联键。
					D3::TracePptTiming("native_observed", session.localSession, *accepted, firstObservationQpc);
					D3::TracePptTiming("descriptor_accepted", session.localSession, *accepted, descriptorAcceptanceQpc);
					D3::TracePptTiming("target_published", session.localSession, *accepted);
					tracedTargetRevision = *accepted;
					observationPending = false;
				}
				cachedTarget->targetRevision = targetRevision = *accepted;
				{
					lock_guard lock(pptSessionMutex);
					expectedPptUiTarget = *cachedTarget;
				}
				(void)D3::SetProductPresentationInputSuspended(D3::Bridge::ReadyIdentityFor(*cachedTarget), false);
				const auto ready = D3::ProductRuntimeSnapshot();
				if (ready.running && ready.workspace == D3::Bridge::Workspace::Presentation
					&& ready.presentationReady && *ready.presentationReady == D3::Bridge::ReadyIdentityFor(*cachedTarget))
					PptInfoStateBuffer = cachedTarget->pageKind ==
						D3::Bridge::PresentationPageKind::EndScreen
						? PptInfoStateStruct{ -1, static_cast<int>(cachedTarget->totalPages) }
						: PptInfoStateStruct{ rawPage, rawTotal };
				else targetRevision = 0;
			}
		}
		const auto now = chrono::steady_clock::now();
		const bool visible = session.active && !whiteboard;
		if (publishedVisible != static_cast<int>(visible) || now >= nextVisibility)
		{
			publishedVisible = visible;
			nextVisibility = now + PptVisibilityPublishInterval;
			Inkeys::UI::Ppt::PublishPresentationVisible(visible);
		}
		int page = PptInfoStateBuffer.CurrentPage, total = PptInfoStateBuffer.TotalPage;
		if (!session.active) { page = -1; total = -1; }
		else if (stateReliable && pageStatus == PptPageStatus::EndScreen &&
			trustedEndScreen && targetRevision != 0)
		{
			page = -1;
			total = static_cast<int>(cachedTarget->totalPages);
		}
		else if (stateReliable && pageStatus == PptPageStatus::EndScreen)
		{
			// 等真正呈现到独立结束页后再切换数字；旧墨迹与新页码不可短暂错配。
			targetRevision = 0;
		}
		else if (!trustedPage) { page = -1; total = -1; }
		if (!whiteboard && (page != publishedPage || total != publishedTotal
			|| targetRevision != publishedTarget || session.localSession != publishedSession))
		{
			publishedPage = page; publishedTotal = total;
			publishedTarget = targetRevision; publishedSession = session.localSession;
			Inkeys::UI::Ppt::PublishPageState(page, total, targetRevision);
		}
		if (whiteboard) { publishedPage = -2; publishedTarget = UINT64_MAX; }
		if (now >= nextMaintenance)
		{
			nextMaintenance = now + PptVisibilityPublishInterval;
			if (session.active && !whiteboard && config.PlugIn.PPTHelper.AutoTakeOver
				&& !pptTakeoverConsumedInCurrentShow)
			{
				const int toolType = GetPptSlideShowAnnotationTool();
				if (toolType == 1 && StartPptTakeoverAnnotation(toolType))
				{
					ExitPptSlideShowAnnotationTool();
					if (config.PlugIn.PPTHelper.AutoTakeOverOnce) pptTakeoverConsumedInCurrentShow = true;
					if (config.PlugIn.PPTHelper.AutoTakeOverExpand && barUISet.barState.fold)
					{ barUISet.barState.fold = false; barUISet.UpdateRendering(); }
				}
			}
		}
		const auto interval = session.active
			? (sessionApi ? PptPageStateCheckInterval : chrono::milliseconds(50))
			: PptIdleStateCheckInterval;
		if (runtimeBefore.running)
			(void)D3::WaitForProductRuntimeRevision(runtimeBefore.runtimeRevision,
				static_cast<std::uint32_t>(interval.count()));
		else this_thread::sleep_for(interval);
	}
	EndSession();
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
		[](std::string payload) { QueuePptUiSettingsPersistence(std::move(payload)); },
		AcknowledgePptPageUi,
	});
	if (pptUiReady)
	{
		// 未放映时四个 PPT 分页窗口不会提交首帧；客户端注册完成即代表启动门禁已就绪。
		IdtWindowsIsVisible.pptWindow = true;
		(void)Inkeys::Startup::Report(
			Inkeys::Startup::Milestone::PptUiClientsReady);
	}
	else
	{
		(void)Inkeys::Startup::ReportFailure(0xF101u);
		if (IDTLogger)
			IDTLogger->error("[PPT 线程][PPTLinkageMain] UI3 四窗口客户端注册失败");
	}

	thread(GetPptState).detach();
	thread infoThread(PptInfo);
	Inkeys::UI::Bar::SetEndShowRequestCallback([](std::uint64_t request)
	{
		QueuePptUiBusinessCommand(PptUiBusinessCommand::ConfirmEndShow, request);
	});
	Inkeys::UI::Bar::SetBusinessFocusCallback([]
	{
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Focus);
	});

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

		// 模态框和 COM 在业务线程执行；不得持有渲染、Scene 或窗口提交锁。
		if (command == PptUiBusinessCommand::PersistSettings)
		{
			(void)WritePptComSettingJson(request.settingsPayload);
			continue;
		}
		struct CompleteRequest
		{
			PptUiBusinessCommand command;
			std::uint64_t request;
			~CompleteRequest()
			{
				if (command == PptUiBusinessCommand::Previous || command == PptUiBusinessCommand::Next)
					pptUiPageCommandOutstanding = false;
				if (request) Inkeys::UI::Bar::CompleteEndShowRequest(request);
			}
		} completion{ command, request.requestId };
		if (!IsCurrentPptSession(request.session)) continue;
		if (command == PptUiBusinessCommand::Focus)
		{
			(void)FocusPptSession(request.session);
			continue;
		}
		if (command == PptUiBusinessCommand::Previous)
		{
			PreviousPptSlides();
			(void)FocusPptSession(request.session);
			continue;
		}
		if (command == PptUiBusinessCommand::Next)
		{
			const int current = PptInfoState.CurrentPage;
			if (current > 0) NextPptSlides(current);
			(void)FocusPptSession(request.session);
			continue;
		}
		if (command == PptUiBusinessCommand::ViewShow)
		{
			ViewPptShow();
			continue;
		}
		const auto server = GetPptComSnapshot();
		const auto sessionApi = QueryPptSessionApi(server);
		if (command == PptUiBusinessCommand::ConfirmEndShow)
		{
			if (!request.session.guardedExitAvailable || !sessionApi)
			{
				if (IDTLogger) IDTLogger->warn("[PPT] confirmed exit unavailable: session capability missing");
				continue;
			}
			const auto title = IW(I18nKey.Dialogs.Common.TipsTitle);
			const auto body = IW(I18nKey.Dialogs.EndPresentation.Body);
			auto dialog = Inkeys::UI::MessageBox::MakeOkCancelRequest(title.c_str(), body.c_str());
			dialog.defaultResult = Inkeys::UI::MessageBox::Result::Cancel;
			dialog.dismissResult = Inkeys::UI::MessageBox::Result::Cancel;
			dialog.language = I18n::languageId();
			dialog.fallback.enabled = false;
			dialog.owner = Inkeys::Window::GetService().Handle(Inkeys::Window::WindowRole::Bar);
			dialog.requireOwner = true;
			pptExitDialogActive.store(true, std::memory_order_release);
			const auto result = Inkeys::UI::MessageBox::Show(dialog);
			pptExitDialogActive.store(false, std::memory_order_release);
			if (result != Inkeys::UI::MessageBox::Result::Ok)
			{
				(void)FocusPptSession(request.session);
				continue;
			}
		}
		if (!IsCurrentPptSession(request.session)
			|| request.session.serviceGeneration != pptComGeneration.load(std::memory_order_acquire)) continue;
		bool exited = false;
		try
		{
			if (sessionApi)
				exited = sessionApi->EndSlideShowIfSession(
					static_cast<__int64>(request.session.showSessionRevision)) == 1;
			else if (command == PptUiBusinessCommand::EndShow)
			{
				// 非点击主栏的旧兼容入口保持原行为，不给所有底层退出塞确认。
				EndPptShow();
				exited = true;
			}
		}
		catch (const _com_error& error)
		{
			if (IDTLogger) IDTLogger->warn("[PPT] exit failed: {}", static_cast<long>(error.Error()));
		}
		// 业务请求只发起退出；Selection 和窗口穿透由可信 EndSession 边沿统一收尾。
		if (!exited && IDTLogger) IDTLogger->warn("[PPT] exit request was not accepted");

	}
	Inkeys::UI::Bar::SetEndShowRequestCallback({});
	Inkeys::UI::Bar::SetBusinessFocusCallback({});
	// 先让观察线程发布真实会话结束，再排空最终不可变保存请求。
	if (infoThread.joinable()) infoThread.join();
	Inkeys::UI::Ppt::Shutdown();
	deque<PptUiBusinessRequest> remaining;
	{
		lock_guard lock(pptUiBusinessMutex);
		remaining.swap(pptUiBusinessCommands);
		pptUiPageCommandOutstanding = false;
	}
	for (const auto& request : remaining)
		if (request.command == PptUiBusinessCommand::PersistSettings)
			(void)WritePptComSettingJson(request.settingsPayload);

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
