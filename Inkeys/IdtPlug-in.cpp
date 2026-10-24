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
import Inkeys.UI.Setting;
import Inkeys.UI.Freeze;
import Inkeys.Other.Config;
import Inkeys.Window;
import Inkeys.Startup.Progress;

#include "IdtPlug-in.h"
#include "Inkeys/UI/Ppt/Ppt.Recovery.h"

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
#include <cstdio>
#include <deque>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
	using Inkeys::Business::PptSessionToken;
	using Inkeys::Business::PptFocusRequest;
	using Inkeys::Business::PptFocusForeground;
	using Inkeys::Business::PptFocusState;
	// 仅初始化前生产探针使用，替换外部 HWND/COM 副作用；正常启动始终为空。
	struct PptFocusTestHooks
	{
		PptFocusState state;
		int activations = 0;
		int previous = 0;
		int next = 0;
	};
	PptFocusTestHooks* pptFocusTestHooks = nullptr;
	PptFocusForeground CapturePptForeground() noexcept
	{
		if (pptFocusTestHooks) return pptFocusTestHooks->state.foreground;
		const HWND window = GetForegroundWindow();
		DWORD processId = 0;
		if (window) GetWindowThreadProcessId(window, &processId);
		return { reinterpret_cast<std::uintptr_t>(window), processId };
	}
	mutex pptSessionMutex;
	PptSessionToken currentPptSession;
	std::optional<Inkeys::Drawing::Draw3::Bridge::PresentationTarget> expectedPptUiTarget;
	std::atomic_uint64_t pptComGeneration = 0;
	std::atomic_bool pptExitDialogActive = false;
	std::atomic_bool pptControlAvailable = false;
	std::atomic_bool pptWindowControlAvailable = false;

	PptSessionToken CapturePptSession()
	{
		lock_guard lock(pptSessionMutex);
		return currentPptSession;
	}
	bool IsCurrentPptSession(const PptSessionToken& token)
	{
		return pptControlAvailable.load(std::memory_order_acquire) &&
			token.serviceGeneration == pptComGeneration.load(std::memory_order_acquire) &&
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
		PptFocusForeground focusForeground;
		std::uint64_t inputWorkspaceEpoch = 0;
	};
	mutex pptUiBusinessMutex;
	condition_variable pptUiBusinessCondition;
	deque<PptUiBusinessRequest> pptUiBusinessCommands;
	atomic_bool pptUiPageCommandOutstanding = false;
	Inkeys::Business::PptEndShowGate pptEndShowGate;
	constexpr auto PptPageStateCheckInterval = chrono::milliseconds(16);
	constexpr auto PptIdleStateCheckInterval = chrono::milliseconds(100);
	constexpr auto PptVisibilityPublishInterval = chrono::milliseconds(500);

	void QueuePptUiBusinessCommand(PptUiBusinessCommand command, std::uint64_t requestId = 0,
		const PptSessionToken* expected = nullptr, const PptFocusForeground* foreground = nullptr)
	{
		const bool pageCommand = command == PptUiBusinessCommand::Previous ||
			command == PptUiBusinessCommand::Next;
		if (pageCommand && pptUiPageCommandOutstanding.exchange(true)) return;
		PptUiBusinessRequest request{ command, expected ? *expected : CapturePptSession(), requestId };
		request.focusForeground = foreground ? *foreground : CapturePptForeground();
		if(pageCommand)request.inputWorkspaceEpoch=PptInputWorkspaceEpoch();
		try
		{
			lock_guard lock(pptUiBusinessMutex);
			if (command == PptUiBusinessCommand::EndShow && !pptEndShowGate.TryBegin(request.session)) return;
			pptUiBusinessCommands.push_back(std::move(request));
		}
		catch (...)
		{
			if (pageCommand) pptUiPageCommandOutstanding = false;
			if (command == PptUiBusinessCommand::EndShow)
			{
				lock_guard lock(pptUiBusinessMutex);
				pptEndShowGate.Complete(request.session);
			}
			if (requestId) Inkeys::UI::Bar::CompleteEndShowRequest(requestId);
			return;
		}
		pptUiBusinessCondition.notify_one();
	}
	void QueuePptEntryFocus(bool entered, bool windowAvailable, bool visible,
		const PptSessionToken& session)
	{
		if (entered && windowAvailable && visible)
			QueuePptUiBusinessCommand(PptUiBusinessCommand::Focus, 0, &session);
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
	void RetryPptCanvasRecovery(Inkeys::UI::PptRecovery::State expected)
	{
		const auto epoch = PptInputWorkspaceEpoch();
		if (!epoch || !expected.retryAvailable || !expected.revision) return;
		const auto runtime = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		const auto desired = Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
		const auto current = Inkeys::UI::PptRecovery::Project(runtime.recovery, desired,
			expected.session, false, runtime.consumerFatal);
		if (!current.CanRetry(expected)) return;
		(void)Inkeys::Drawing::Draw3::RetryProductCanvasRecovery(runtime.recovery,
			[epoch]() noexcept { return PptInputWorkspaceEpoch() == epoch; });
	}

	void AcknowledgePptPageUi(std::uint64_t session, std::uint64_t revision, int page, int total)
	{
		std::optional<Inkeys::Drawing::Draw3::Bridge::PresentationReadyIdentity> ready;
		{
			lock_guard lock(pptSessionMutex);
			if (!currentPptSession.active || currentPptSession.localSession != session
				|| currentPptSession.serviceGeneration != pptComGeneration.load(std::memory_order_acquire)
				|| !expectedPptUiTarget || !Inkeys::Business::MatchesPptPageUi(
					*expectedPptUiTarget, session, revision, page, total)) return;
			ready = Inkeys::Drawing::Draw3::Bridge::ReadyIdentityFor(*expectedPptUiTarget);
		}
		(void)Inkeys::Drawing::Draw3::PublishProductPresentationUiReady(*ready);
	}

	[[nodiscard]] bool IsInRect(long x, long y, const RECT& rect) noexcept
	{
		return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
	}
}

namespace Inkeys::Business
{
	PptFocusRequest CapturePptFocusRequest() noexcept
	{
		return { CapturePptSession(), CapturePptForeground() };
	}
	void QueuePptFocusRequest(const PptFocusRequest& request) noexcept
	{
		if (!request.session.active || request.session.showWindow == 0) return;
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Focus, 0,
			&request.session, &request.foreground);
	}
	PptSessionToken CapturePptInputSession() noexcept { return CapturePptSession(); }
	bool IsCurrentPptInputSession(const PptSessionToken& token) noexcept
	{
		return IsCurrentPptSession(token);
	}
	void QueuePptInputDirection(const PptSessionToken& token,
		bool next, int currentPage, int totalPage) noexcept
	{
		if (totalPage <= 0 || !IsCurrentPptSession(token)) return;
		const auto command = next
			? (currentPage == -1 ? PptUiBusinessCommand::EndShow : PptUiBusinessCommand::Next)
			: PptUiBusinessCommand::Previous;
		// 捕获的 token 原样存入最终业务请求，入队时绝不重新认领当前放映。
		QueuePptUiBusinessCommand(command, 0, &token);
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
		pptCom->SetConsoleOutputEnabled(
			static_cast<bool>(Inkeys::config.Experimental.Inkeys3.ConsoleOutput.PptCOM));
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
int GetPptSlideShowAnnotationTool(const IPptCOMServerPtr& pptCom)
{
	int toolType = 0;
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
bool ExitPptSlideShowAnnotationTool(const IPptCOMServerPtr& pptCom)
{
	bool ret = false;
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
	std::uint64_t PptProcessStartTime(DWORD processId)
	{
		if (!processId) return 0;
		const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (!process) return 0;
		FILETIME created{}, exited{}, kernel{}, user{};
		const bool read = GetProcessTimes(process, &created, &exited, &kernel, &user) != 0;
		CloseHandle(process);
		return read ? (static_cast<std::uint64_t>(created.dwHighDateTime) << 32)
			| created.dwLowDateTime : 0;
	}
	PptFocusState CapturePptFocusState(const PptSessionToken& token)
	{
		if (pptFocusTestHooks) return pptFocusTestHooks->state;
		PptFocusState state;
		state.session = CapturePptSession();
		state.controlAvailable = pptControlAvailable.load(std::memory_order_acquire)
			&& pptWindowControlAvailable.load(std::memory_order_acquire)
			&& token.serviceGeneration == pptComGeneration.load(std::memory_order_acquire);
		state.workspaceAllowed = !WhiteboardTransactionActive()
			&& Inkeys::UI::Bar::PptBusinessFocusAllowed();
		const HWND settings = Inkeys::Window::GetService().Handle(Inkeys::Window::WindowRole::Setting);
		state.modalActive = pptExitDialogActive.load() || Inkeys::UI::MessageBox::IsShowing()
			|| Inkeys::UI::Setting::IsVisible() || (settings && IsWindowVisible(settings));
		const HWND target = reinterpret_cast<HWND>(token.showWindow);
		state.targetExists = target && IsWindow(target);
		state.targetVisible = state.targetExists && IsWindowVisible(target);
		DWORD processId = 0;
		if (state.targetExists) GetWindowThreadProcessId(target, &processId);
		state.targetProcessId = processId;
		state.targetProcessStartTime = PptProcessStartTime(processId);
		state.foreground = CapturePptForeground();
		return state;
	}
	bool FocusPptSession(const PptFocusRequest& request)
	{
		const auto state = CapturePptFocusState(request.session);
		if (!Inkeys::Business::CanFocusPptSession(request, state)) return false;
		if (state.foreground.window == request.session.showWindow) return true;
		if (!IsCurrentPptSession(request.session)) return false;
		if (pptFocusTestHooks)
		{
			++pptFocusTestHooks->activations;
			pptFocusTestHooks->state.foreground = {
				request.session.showWindow, request.session.processId };
			return true;
		}
		const HWND target = reinterpret_cast<HWND>(request.session.showWindow);
		(void)SetForegroundWindow(target);
		const bool focused = GetForegroundWindow() == target;
		if (!focused && IDTLogger) IDTLogger->debug("[PPT] foreground handoff denied session={}", request.session.localSession);
		return focused;
	}
	bool FocusPptSession(const PptSessionToken& token)
	{
		const auto foreground = CapturePptForeground();
		// 无入队上下文的旧入口仍只从本程序/当前 Office 交接，不扩大其前台权限。
		if (foreground.processId != GetCurrentProcessId() && foreground.processId != token.processId)
			return false;
		return FocusPptSession(PptFocusRequest{ token, foreground });
	}
	bool DispatchPptFocusBusinessAction(const PptUiBusinessRequest& request)
	{
		const auto command = request.command;
		if (command != PptUiBusinessCommand::Focus && command != PptUiBusinessCommand::Previous
			&& command != PptUiBusinessCommand::Next) return false;
		if (!IsCurrentPptSession(request.session)) return true;
		// 已排队的翻页不能跨白板意图往返；只约束 page action，不误退有效的显式焦点请求。
		if(command!=PptUiBusinessCommand::Focus && (!request.inputWorkspaceEpoch
			|| request.inputWorkspaceEpoch!=PptInputWorkspaceEpoch()))return true;
		if (command == PptUiBusinessCommand::Previous)
		{
			if (pptFocusTestHooks) ++pptFocusTestHooks->previous;
			else PreviousPptSlides();
		}
		else if (command == PptUiBusinessCommand::Next && PptInfoState.CurrentPage > 0)
		{
			if (pptFocusTestHooks) ++pptFocusTestHooks->next;
			else NextPptSlides(PptInfoState.CurrentPage);
		}
		// Down/Up、Move 与长按不会改变前台身份；不得用 LastInput 变化取消正常翻页交接。
		(void)FocusPptSession(PptFocusRequest{ request.session, request.focusForeground });
		return true;
	}
}
void FocusPptShow()
{
	(void)FocusPptSession(CapturePptSession());
}

int RunPptFocusProductionTest() noexcept
{
	try
	{
		if (Inkeys::Window::GetService().Running() || Inkeys::Drawing::Draw3::ProductRunning()
			|| pptFocusTestHooks || WhiteboardTransactionActive()) return 2;
		{
			lock_guard lock(pptUiBusinessMutex);
			if (!pptUiBusinessCommands.empty()) return 2;
		}
		struct Restore
		{
			PptSessionToken session = CapturePptSession();
			std::uint64_t generation = pptComGeneration.load();
			bool control = pptControlAvailable.load();
			bool window = pptWindowControlAvailable.load();
			bool outstanding = pptUiPageCommandOutstanding.load();
			PptInfoStateStruct page = PptInfoState;
			~Restore()
			{
				RequestWhiteboardActive(false);
				pptFocusTestHooks = nullptr;
				{ lock_guard lock(pptSessionMutex); currentPptSession = session; }
				pptComGeneration = generation;
				pptControlAvailable = control;
				pptWindowControlAvailable = window;
				pptUiPageCommandOutstanding = outstanding;
				PptInfoState = page;
				lock_guard lock(pptUiBusinessMutex);
				pptUiBusinessCommands.clear();
			}
		} restore;
		int failures = 0;
		const auto check = [&](bool ok, const char* label)
		{
			if (!ok) { ++failures; std::fprintf(stderr, "[PptFocusProduction] FAIL: %s\n", label); }
		};
		const PptSessionToken session{ 101, 202, 303, 404, 505, 606, true, true, 707 };
		const PptFocusForeground external{ 801, 802 };
		PptFocusTestHooks fake;
		fake.state = { session, external, true, true, false, true, true,
			session.processId, session.processStartTime };
		pptFocusTestHooks = &fake;
		{ lock_guard lock(pptSessionMutex); currentPptSession = session; }
		pptComGeneration = session.serviceGeneration;
		pptControlAvailable = true;
		pptWindowControlAvailable = true;
		PptInfoState = { 2, 3 };
		const auto pop = [&]
		{
			PptUiBusinessRequest request;
			{
				lock_guard lock(pptUiBusinessMutex);
				check(!pptUiBusinessCommands.empty(), "request-enqueued");
				if (pptUiBusinessCommands.empty()) return request;
				request = pptUiBusinessCommands.front();
				pptUiBusinessCommands.pop_front();
			}
			pptUiPageCommandOutstanding = false;
			return request;
		};
		// 真正的入队/消费分支，只有 HWND 和 COM sink 由探针替换。
		for (const auto command : { PptUiBusinessCommand::Previous, PptUiBusinessCommand::Next,
			PptUiBusinessCommand::Next })
		{
			fake.state.foreground = external;
			QueuePptUiBusinessCommand(command);
			const auto request = pop();
			check(request.focusForeground.window == external.window
				&& request.focusForeground.processId == external.processId,
				"noactivate-click-retains-enqueue-foreground");
			check(DispatchPptFocusBusinessAction(request), "page-dispatch-handled");
		}
		check(fake.previous == 1 && fake.next == 2 && fake.activations == 3,
			"normal-up-move-and-longpress-do-not-cancel-focus");
		fake.state.foreground = external;
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Next);
		const auto switched = pop();
		fake.state.foreground = { 901, 902 };
		(void)DispatchPptFocusBusinessAction(switched);
		check(fake.next == 3 && fake.activations == 3,
			"queued-foreground-switch-blocks-focus-without-changing-com-command");
		fake.state.foreground = external;
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Next);
		const auto stale = pop();
		{ lock_guard lock(pptSessionMutex); ++currentPptSession.localSession; }
		(void)DispatchPptFocusBusinessAction(stale);
		check(fake.next == 3 && fake.activations == 3, "old-session-does-not-dispatch-or-focus");
		{ lock_guard lock(pptSessionMutex); currentPptSession = session; }
		for (int state = 0; state != 2; ++state)
		{
			fake.state.modalActive = state == 0;
			fake.state.workspaceAllowed = state != 1;
			QueuePptUiBusinessCommand(PptUiBusinessCommand::Focus);
			(void)DispatchPptFocusBusinessAction(pop());
		}
		check(fake.activations == 3, "modal-and-whiteboard-block-focus");
		fake.state.modalActive = false;
		fake.state.workspaceAllowed = true;
		FocusPptShow();
		check(fake.activations == 3, "legacy-focus-does-not-gain-external-foreground-permission");
		QueuePptEntryFocus(true, true, false, session);
		for (int heartbeat = 0; heartbeat != 100; ++heartbeat)
			QueuePptEntryFocus(false, true, true, session);
		check(pptUiBusinessCommands.empty(), "hidden-entry-and-heartbeats-never-enqueue-focus");
		QueuePptEntryFocus(true, true, true, session);
		(void)DispatchPptFocusBusinessAction(pop());
		check(fake.activations == 4 && pptUiBusinessCommands.empty(), "visible-entry-focuses-once");
		auto windowless = session;
		windowless.showWindow = 0;
		fake.state.foreground = external;
		QueuePptEntryFocus(Inkeys::Business::IsFirstPptWindowAcquisition(windowless,
			session.showWindow), true, true, session);
		(void)DispatchPptFocusBusinessAction(pop());
		check(fake.activations == 5, "first-trusted-window-acquisition-focuses-once");
		fake.state.foreground = external;
		// ppt_show 可暂时清空，但保留的 session.showWindow 证明这不是首次取得窗口。
		for (int publication = 0; publication != 3; ++publication)
			QueuePptEntryFocus(Inkeys::Business::IsFirstPptWindowAcquisition(session,
				session.showWindow), true, true, session);
		check(fake.activations == 5 && pptUiBusinessCommands.empty(),
			"known-window-recovery-and-repeat-publication-do-not-refocus-background-show");
		const int previousBefore=fake.previous,nextBefore=fake.next,focusBefore=fake.activations;
		const auto initialWorkspace=PptInputWorkspaceEpoch();
		fake.state.foreground=external;
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Previous);const auto beforeWhiteboard=pop();
		RequestWhiteboardActive(true);
		check(!PptInputWorkspaceEpoch(),"whiteboard-intent-closes-page-admission");
		(void)DispatchPptFocusBusinessAction(beforeWhiteboard);
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Next);const auto duringWhiteboard=pop();
		RequestWhiteboardActive(true);
		RequestWhiteboardActive(false);const auto returnedWorkspace=PptInputWorkspaceEpoch();
		check(returnedWorkspace && returnedWorkspace!=initialWorkspace,"whiteboard-return-advances-page-input-identity");
		(void)DispatchPptFocusBusinessAction(duringWhiteboard);
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Next);const auto beforeRoundtrip=pop();
		RequestWhiteboardActive(true);RequestWhiteboardActive(false);
		(void)DispatchPptFocusBusinessAction(beforeRoundtrip);
		check(fake.previous==previousBefore && fake.next==nextBefore && fake.activations==focusBefore,
			"queued-previous-next-cannot-run-during-or-across-whiteboard-roundtrip");
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Previous);const auto validPrevious=pop();
		const auto unchangedWorkspace=PptInputWorkspaceEpoch();RequestWhiteboardActive(false);
		check(PptInputWorkspaceEpoch()==unchangedWorkspace,"repeated-whiteboard-off-keeps-current-page-input");
		(void)DispatchPptFocusBusinessAction(validPrevious);
		fake.state.foreground=external;
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Next);(void)DispatchPptFocusBusinessAction(pop());
		check(fake.previous==previousBefore+1 && fake.next==nextBefore+1 && fake.activations==focusBefore+2,
			"same-session-page-actions-and-focus-remain-valid-after-return");
		fake.state.foreground=external;
		QueuePptUiBusinessCommand(PptUiBusinessCommand::Focus);const auto focusRequest=pop();
		RequestWhiteboardActive(true);RequestWhiteboardActive(false);
		(void)DispatchPptFocusBusinessAction(focusRequest);
		check(fake.activations==focusBefore+3,"valid-explicit-focus-is-not-retired-by-page-only-epoch");
		std::fprintf(stderr, "[PptFocusProduction] failures=%d\n", failures);
		return failures == 0 ? 0 : 1;
	}
	catch (...) { std::fputs("[PptFocusProduction] FAIL: exception\n", stderr); return 1; }
}

bool StartPptTakeoverAnnotation(int toolType, std::uint64_t expectedRevision)
{
	if (toolType < static_cast<int>(PptAnnotationTool::Pen) ||
		toolType > static_cast<int>(PptAnnotationTool::Highlighter)) return false;
	if (!ChangeStateModeToPptAnnotation(
		static_cast<PptAnnotationTool>(toolType), expectedRevision)) return false;
	barUISet.UpdateRendering();
	return true;
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
	PptTargetCache targetCache;
	auto& cachedTarget = targetCache.target;
	int lastRawPage = -2, lastRawTotal = -2;
	std::int64_t firstObservationQpc = 0, descriptorAcceptanceQpc = 0;
	std::uint64_t tracedTargetRevision = 0;
	std::uint64_t loggedDocumentReadyRevision = 0, loggedUiReadyRevision = 0;
	bool observationPending = false;
	int publishedPage = -2, publishedTotal = -2;
	std::uint64_t publishedTarget = UINT64_MAX, publishedSession = 0;
	auto publishedPageMode = Inkeys::UI::Ppt::PageStateMode::Pending;
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
		pptControlAvailable.store(false, std::memory_order_release);
		pptWindowControlAvailable.store(false, std::memory_order_release);
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
		std::uint64_t exitWorkspaceEpoch = 0;
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
			if (ending.active)
			{
				// 工具收尾固定退出边沿的权限，不能借稍后场景发布的新 epoch 跨越白板往返。
				exitWorkspaceEpoch = PptInputWorkspaceEpoch();
				exitModeRevision = StateModeTransitionRevision();
			}
			if (ending.active) TraceTrueExit("inactive", ending);
			EndSession();
		}
		const bool ownerActive = stateReliable && sessionApi && cachedState
			&& CanStartPptControlSession(*cachedState);
		auto controlPage = ownerActive
			? ResolvePptControlPage(*cachedState, rawPage, rawTotal) : PptControlPage{};
		bool trustedPage = stateReliable && lifecycle == PptLifecycle::Active
			&& pageStatus == PptPageStatus::Valid && descriptor
			&& rawPage > 0 && rawTotal > 0
			&& descriptor->currentPage == static_cast<std::uint32_t>(rawPage)
			&& descriptor->totalPage == static_cast<std::uint32_t>(rawTotal);
		bool trustedEndScreen = stateReliable && sessionApi &&
			lifecycle == PptLifecycle::Active && pageStatus == PptPageStatus::EndScreen &&
			descriptor && descriptor->status == D3::PresentationDescriptorStatus::StableSlideIds &&
			// raw 已回到真实页或总数已变时，旧结束页拓扑不能继续开放写入。
			controlPage.known && controlPage.currentPage == -1 &&
			descriptor->currentPage == 0 && descriptor->totalPage > 0;
		auto session = CapturePptSession();
		bool confirmedWindowEnd = false;
		bool controlSessionAvailable = false;
		bool windowControlAvailable = false;
		bool focusAfterStart = false;
		if (session.active && session.showWindow != 0)
		{
			const HWND capturedWindow = reinterpret_cast<HWND>(session.showWindow);
			DWORD ownerProcess = 0;
			// COM/服务槽暂时不可用不是退出；已销毁或被别的进程复用的窗口才证明旧场次结束。
			const bool windowExists = IsWindow(capturedWindow) != 0;
			if (windowExists) GetWindowThreadProcessId(capturedWindow, &ownerProcess);
			if (IsPptWindowConfirmedEnded(session, windowExists, ownerProcess,
				PptProcessStartTime(ownerProcess)) && MatchesPptSession(session, CapturePptSession()))
			{
				confirmedWindowEnd = true;
				exitedThisTick = true;
				exitWorkspaceEpoch = PptInputWorkspaceEpoch();
				exitModeRevision = StateModeTransitionRevision();
				TraceTrueExit(!windowExists ? "show-window-lost" : "show-window-reused", session);
				EndSession();
				session = CapturePptSession();
			}
		}
		if (stateReliable && lifecycle == PptLifecycle::Active && descriptor
			&& (ownerActive || trustedPage || pageStatus == PptPageStatus::EndScreen))
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
				session.serviceGeneration == serviceGeneration && session.showSessionRevision == showRevision
				&& session.bindingRevision == descriptor->bindingRevision;
			if (!descriptorWindowMatches || endedSessionSnapshot)
			{
				// 窗口身份只授权墨迹/焦点；HWND0 不阻断 owner 已证实的 COM 基础控制。
				trustedPage = false;
				trustedEndScreen = false;
			}
			const bool controlSessionAllowed = !endedSessionSnapshot &&
				(descriptorWindowMatches || (ownerActive && descriptor->slideShowHwnd == 0));
			if (!controlSessionAllowed) controlPage = {};
			if (controlSessionAllowed &&
				(!session.active || session.serviceGeneration != serviceGeneration
				|| session.showSessionRevision != showRevision
				|| session.bindingRevision != descriptor->bindingRevision
				|| (!sessionApi && session.showWindow != reinterpret_cast<std::uintptr_t>(showWindow))))
			{
				EndSession();
				session = { ++localSessionSequence, serviceGeneration, showRevision,
					descriptor->bindingRevision, descriptorWindowMatches
						? reinterpret_cast<std::uintptr_t>(showWindow) : 0,
					descriptorWindowMatches ? processId : 0, true, sessionApi != nullptr,
					descriptorWindowMatches ? PptProcessStartTime(processId) : 0 };
				{
					lock_guard lock(pptSessionMutex);
					currentPptSession = session;
				}
				ppt_show = reinterpret_cast<HWND>(session.showWindow);
				std::wstringstream title(GetPptTitle());
				getline(title, ppt_title);
				getline(title, ppt_software);
				ppt_software = ppt_software.find(L"WPS") != std::wstring::npos ? L"WPS" : L"PowerPoint";
				if (kPptLoadingPageEnabled && !ppt_title_recond[ppt_title] &&
					pptComSetlist.showLoadingScreen) FreezePPT = true;
				pptTakeoverConsumedInCurrentShow = false;
				Inkeys::UI::Ppt::PublishSession(session.localSession, true, ppt_show);
				Inkeys::UI::Freeze::SetPresentationActive(true);
				barUISet.UpdateRendering();
				focusAfterStart = descriptorWindowMatches;
				descriptorChanged = true;
			}
			else if (controlSessionAllowed && descriptorWindowMatches &&
				(session.showWindow != reinterpret_cast<std::uintptr_t>(showWindow)
					|| session.processId != processId || ppt_show != showWindow))
			{
				// 同场由基础控制提升到可靠窗口，只更新锚点，避免伪造退出/重入。
				const bool firstTrustedWindow = IsFirstPptWindowAcquisition(
					session, reinterpret_cast<std::uintptr_t>(showWindow));
				session.showWindow = reinterpret_cast<std::uintptr_t>(showWindow);
				session.processId = processId;
				session.processStartTime = PptProcessStartTime(processId);
				{
					lock_guard lock(pptSessionMutex);
					currentPptSession = session;
				}
				ppt_show = showWindow;
				Inkeys::UI::Ppt::PublishSession(session.localSession, true, showWindow);
				focusAfterStart = firstTrustedWindow;
				descriptorChanged = true;
			}
			else if (controlSessionAllowed && !descriptorWindowMatches && controlPage.known && ppt_show)
			{
				// 仍保留历史锚用于检测销毁；当前基础控件布局/焦点不借用未知窗口。
				ppt_show = nullptr;
				Inkeys::UI::Ppt::PublishSession(session.localSession, true, nullptr);
			}
			controlSessionAvailable = controlSessionAllowed && session.active;
			windowControlAvailable = controlSessionAvailable && descriptorWindowMatches;
		}
		// 即使本轮不能接受 raw/descriptor，也记住尚未应用的 descriptor。
		if (targetCache.Update(descriptorChanged || legacyDescriptor.has_value(), descriptor,
			session.active && trustedPage, session.active && trustedEndScreen, session.localSession))
		{
			LARGE_INTEGER acceptedAt{};
			QueryPerformanceCounter(&acceptedAt);
			descriptorAcceptanceQpc = acceptedAt.QuadPart;
		}
		// COM getter 之后服务槽仍可能换代；旧缓存不能直接进入新的 Host/UI 交接。
		const bool currentService = serviceGeneration == pptComGeneration.load(std::memory_order_acquire);
		if (!currentService)
		{
			stateReliable = false;
			trustedPage = false;
			trustedEndScreen = false;
			pageStatus = PptPageStatus::Unknown;
			controlSessionAvailable = false;
			windowControlAvailable = false;
			controlPage = {};
		}
		pptControlAvailable.store(controlSessionAvailable, std::memory_order_release);
		pptWindowControlAvailable.store(windowControlAvailable, std::memory_order_release);
		if (cachedTarget && (session.serviceGeneration != serviceGeneration ||
			cachedTarget->sessionRevision != session.localSession ||
			cachedTarget->bindingRevision != session.bindingRevision))
		{
			trustedPage = false;
			trustedEndScreen = false;
		}
		const bool trustedTarget = targetCache.Current() &&
			((trustedPage && cachedTarget->pageKind == D3::Bridge::PresentationPageKind::Slide) ||
				(trustedEndScreen && cachedTarget->pageKind ==
					D3::Bridge::PresentationPageKind::EndScreen));
		const bool basicControl = controlSessionAvailable && controlPage.known && !trustedTarget;
		if (!trustedTarget)
		{
			lock_guard lock(pptSessionMutex);
			expectedPptUiTarget.reset();
		}
		const bool whiteboard = WhiteboardTransactionActive();
		const auto sceneWorkspaceEpoch = PptSceneWorkspaceEpoch();
		const auto sceneStillCurrent = [sceneWorkspaceEpoch]() noexcept
		{ return sceneWorkspaceEpoch != 0 && PptSceneWorkspaceEpoch() == sceneWorkspaceEpoch; };
		static const bool tracePptGate = []() noexcept
		{
			wchar_t value[8]{};
			return GetEnvironmentVariableW(L"INKEYS_PPT_TIMING", value, ARRAYSIZE(value)) > 0
				&& value[0] == L'1';
		}();
		if (tracePptGate && IDTLogger && (descriptorChanged || rawChanged))
		{
			// 仅记录数值身份门，不把演示文稿路径或标题写入诊断。
			DWORD showOwner = 0;
			if (descriptor && descriptor->slideShowHwnd)
				GetWindowThreadProcessId(reinterpret_cast<HWND>(
					static_cast<std::uintptr_t>(descriptor->slideShowHwnd)), &showOwner);
			IDTLogger->info(
				"[PptGate] lifecycle={} pageStatus={} raw={}/{} descStatus={} desc={}/{} ids={} showOwner={} descPid={} sessionActive={} sessionId={} trustedTarget={} trustedPage={} trustedEnd={} control={} windowControl={} basic={} whiteboard={} hostRunning={}",
				static_cast<int>(lifecycle), static_cast<int>(pageStatus), rawPage, rawTotal,
				descriptor ? static_cast<int>(descriptor->status) : -1,
				descriptor ? descriptor->currentPage : 0,
				descriptor ? descriptor->totalPage : 0,
				descriptor ? descriptor->slideIds.size() : 0,
				showOwner, descriptor ? descriptor->applicationProcessId : 0,
				session.active, session.localSession, static_cast<bool>(trustedTarget), trustedPage,
				trustedEndScreen, controlSessionAvailable, windowControlAvailable, basicControl,
				whiteboard, D3::ProductRunning());
		}
		const auto bridge = D3::ProductHost().ProductBridge().Snapshot();
		if (sceneWorkspaceEpoch != 0 && session.active && !trustedTarget)
		{
			const bool sameBinding = currentService && session.serviceGeneration == serviceGeneration
				&& (!cachedState || MatchesPptShowSession(session, serviceGeneration, *cachedState));
			if (!basicControl && sameBinding && bridge.presentationTarget
				&& bridge.presentationTarget->sessionRevision == session.localSession
				&& bridge.presentationTarget->bindingRevision == session.bindingRevision)
				(void)D3::SetProductPresentationInputSuspended(
					D3::Bridge::ReadyIdentityFor(*bridge.presentationTarget), true);
			else
			{
				// 新文稿/基础模式先撤销旧墨迹目标；原始数字不能把旧画布带到新场。
				D3::ClearProductPresentationTarget(sceneStillCurrent);
				cachedTarget.reset();
				PptInfoStateBuffer = { -1, -1 };
			}
		}
		std::uint64_t targetRevision = 0;
		if (sceneWorkspaceEpoch != 0 && !session.active && (confirmedWindowEnd ||
			(stateReliable && lifecycle == PptLifecycle::Inactive)))
		{
			D3::PublishProductWorkspace(D3::Bridge::Workspace::Desktop, sceneStillCurrent);
			if (!whiteboard && exitedThisTick && exitWorkspaceEpoch != 0)
			{
				// 可信退出只收尾一次；若此后用户主动换工具，旧退出不得覆盖它。
				if (ChangeStateModeToSelectionIfRevision(exitModeRevision, exitWorkspaceEpoch))
				{
					// EndSession 的刷新早于模式收尾；成功进入选择后重绘实际工具高亮。
					barUISet.UpdateRendering();
				}
			}
		}
		else if (sceneWorkspaceEpoch != 0 && session.active && trustedTarget)
		{
			const auto accepted = D3::PublishProductPresentationTarget(*cachedTarget, sceneStillCurrent);
			if (!accepted && tracePptGate && IDTLogger && (descriptorChanged || rawChanged))
			{
				const auto observedBridge = D3::ProductHost().ProductBridge().Snapshot();
				IDTLogger->warn("[PptGate] publish_rejected host={} bridgeWorkspace={} valid={} keyZero={} sourceEmpty={}",
					D3::ProductHost().Running(), static_cast<int>(observedBridge.workspace),
					D3::Bridge::ValidPresentationPage(*cachedTarget),
					cachedTarget->key.IsZero(), cachedTarget->sourceIdentity.empty());
			}
			if (accepted)
			{
				if (tracedTargetRevision != *accepted)
				{
					// 目标 revision 确定后补记原始 QPC，所有阶段使用同一关联键。
					D3::TracePptTiming("native_observed", session.localSession, *accepted, firstObservationQpc);
					D3::TracePptTiming("descriptor_accepted", session.localSession, *accepted, descriptorAcceptanceQpc);
					D3::TracePptTiming("target_published", session.localSession, *accepted);
					if (tracePptGate && IDTLogger)
						IDTLogger->info("[PptGate] publish_accepted session={} revision={} pageKind={} pageIndex={} slideId={}",
							session.localSession, *accepted,
							static_cast<int>(cachedTarget->pageKind), cachedTarget->pageIndex,
							cachedTarget->slideId.value_or(0));
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
				if (tracePptGate && IDTLogger)
				{
					const auto expected = D3::Bridge::ReadyIdentityFor(*cachedTarget);
					if (ready.presentationReady && *ready.presentationReady == expected &&
						loggedDocumentReadyRevision != *accepted)
					{
						loggedDocumentReadyRevision = *accepted;
						IDTLogger->info("[PptGate] document_ready session={} revision={} presents={}",
							session.localSession, *accepted, ready.successfulPresentCount);
					}
					if (ready.presentationUiReady && *ready.presentationUiReady == expected &&
						loggedUiReadyRevision != *accepted)
					{
						loggedUiReadyRevision = *accepted;
						IDTLogger->info("[PptGate] page_ui_ready session={} revision={}",
							session.localSession, *accepted);
					}
				}
				if (ready.running && ready.workspace == D3::Bridge::Workspace::Presentation
					&& ready.presentationReady && *ready.presentationReady == D3::Bridge::ReadyIdentityFor(*cachedTarget))
					PptInfoStateBuffer = cachedTarget->pageKind ==
						D3::Bridge::PresentationPageKind::EndScreen
						? PptInfoStateStruct{ -1, static_cast<int>(cachedTarget->totalPages) }
						: PptInfoStateStruct{ static_cast<int>(cachedTarget->pageIndex + 1),
						static_cast<int>(cachedTarget->totalPages) };
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
		// 只在可信进入/首次取得窗口的边沿请求一次；白板覆盖和可见性心跳都不抢焦点。
		QueuePptEntryFocus(focusAfterStart, windowControlAvailable, visible, session);
		int page = PptInfoStateBuffer.CurrentPage, total = PptInfoStateBuffer.TotalPage;
		auto pageMode = Inkeys::UI::Ppt::PageStateMode::Pending;
		if (!session.active) { page = -1; total = -1; }
		else if (basicControl)
		{
			page = controlPage.currentPage;
			total = controlPage.totalPage;
			pageMode = Inkeys::UI::Ppt::PageStateMode::BasicControl;
		}
		else if (stateReliable && pageStatus == PptPageStatus::EndScreen &&
			trustedEndScreen && targetRevision != 0)
		{
			page = -1;
			total = static_cast<int>(cachedTarget->totalPages);
			pageMode = Inkeys::UI::Ppt::PageStateMode::InkReady;
		}
		else if (trustedPage && targetRevision != 0)
		{
			pageMode = Inkeys::UI::Ppt::PageStateMode::InkReady;
		}
		else if (currentService && publishedSession == session.localSession
			&& (!cachedState || MatchesPptShowSession(session, serviceGeneration, *cachedState)))
		{
			// 同场 busy 保留已显示数字，但显式 Pending 阻断旧回执和新写入。
			page = publishedPage;
			total = publishedTotal;
		}
		else { page = -1; total = -1; }
		const auto recoveryRuntime = D3::ProductRuntimeSnapshot();
		const auto recoveryDesired = D3::ProductHost().ProductBridge().Snapshot();
		const auto recoveryView = Inkeys::UI::PptRecovery::Project(recoveryRuntime.recovery,
			recoveryDesired, session.localSession, whiteboard, recoveryRuntime.consumerFatal);
		Inkeys::UI::Ppt::PublishCanvasRecovery(recoveryView);
		if (recoveryView.kind != Inkeys::UI::PptRecovery::Kind::None && recoveryDesired.presentationTarget)
		{
			// 故障显示不冒充 InkReady；翻页仍绑定可信目标，重试绑定独立故障令牌。
			page = recoveryDesired.presentationTarget->pageKind == D3::Bridge::PresentationPageKind::EndScreen
				? -1 : static_cast<int>(recoveryDesired.presentationTarget->pageIndex + 1);
			total = static_cast<int>(recoveryDesired.presentationTarget->totalPages);
			targetRevision = 0;
			pageMode = Inkeys::UI::Ppt::PageStateMode::Pending;
		}
		if (!whiteboard && (page != publishedPage || total != publishedTotal
			|| targetRevision != publishedTarget || session.localSession != publishedSession
			|| pageMode != publishedPageMode))
		{
			publishedPage = page; publishedTotal = total;
			publishedTarget = targetRevision; publishedSession = session.localSession;
			publishedPageMode = pageMode;
			Inkeys::UI::Ppt::PublishPageState(page, total, targetRevision, pageMode);
		}
		if (whiteboard) { publishedPage = -2; publishedTarget = UINT64_MAX; }
		if (now >= nextMaintenance)
		{
			nextMaintenance = now + PptVisibilityPublishInterval;
			if (session.active && !whiteboard && trustedTarget && targetRevision != 0
				&& currentService && server &&
				config.PlugIn.PPTHelper.AutoTakeOver &&
				!pptTakeoverConsumedInCurrentShow)
			{
				const int toolType = GetPptSlideShowAnnotationTool(server);
				const auto modeRevision = StateModeTransitionRevision();
				// 原生批注退出成功且仍是同一场放映，才让 Draw3 接管输入。
				if (toolType >= static_cast<int>(PptAnnotationTool::Pen) &&
					toolType <= static_cast<int>(PptAnnotationTool::Highlighter) &&
					MatchesPptSession(session, CapturePptSession()) &&
					ExitPptSlideShowAnnotationTool(server) &&
					serviceGeneration == pptComGeneration.load(std::memory_order_acquire) &&
					MatchesPptSession(session, CapturePptSession()) &&
					StartPptTakeoverAnnotation(toolType, modeRevision))
				{
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
		if (!PptComWriteSetting() && IDTLogger)
			IDTLogger->warn("[PPTLinkageMain] pptcom_configuration.json 写回失败，保留现存配置文件");
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
		RetryPptCanvasRecovery,
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
			PptSessionToken session;
			~CompleteRequest()
			{
				if (command == PptUiBusinessCommand::Previous || command == PptUiBusinessCommand::Next)
					pptUiPageCommandOutstanding = false;
				if (command == PptUiBusinessCommand::EndShow)
				{
					lock_guard lock(pptUiBusinessMutex);
					pptEndShowGate.Complete(session);
				}
				if (request) Inkeys::UI::Bar::CompleteEndShowRequest(request);
			}
		} completion{ command, request.requestId, request.session };
		if (!IsCurrentPptSession(request.session)) continue;
		if (DispatchPptFocusBusinessAction(request)) continue;
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
		pptEndShowGate.Reset();
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
bool LaunchVerifiedDesktopDrawpadBlocker(bool runAsAdmin)
{
	const wstring directory = pluginPath + L"DesktopDrawpadBlocker";
	const wstring executable = directory + L"\\DesktopDrawpadBlocker.exe";
	const DWORD dirAttributes = GetFileAttributesW(directory.c_str());
	const DWORD exeAttributes = GetFileAttributesW(executable.c_str());
	if (dirAttributes == INVALID_FILE_ATTRIBUTES ||
		!(dirAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
		(dirAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
		exeAttributes == INVALID_FILE_ATTRIBUTES ||
		(exeAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
		return false;
	const HRSRC embedded = FindResourceW(nullptr, MAKEINTRESOURCE(237), L"EXE");
	if (!embedded) return false;
	const DWORD embeddedSize = SizeofResource(nullptr, embedded);
	error_code error;
	if (!embeddedSize || filesystem::file_size(executable, error) != embeddedSize || error)
		return false;
	try
	{
		sha256wrapper hash;
		if (hash.getHashFromFileW(executable) != ddbInteractionSetList.DdbSHA256)
			return false;
	}
	catch (...) { return false; }
	if (isProcessRunning(executable.c_str())) return false;
	const HINSTANCE launched = ShellExecuteW(nullptr,
		runAsAdmin ? L"runas" : nullptr, executable.c_str(),
		nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(launched) > 32;
}

void StartDesktopDrawpadBlocker()
{
	// 禁用时不清理命名目录：目录可能包含用户或其他组件文件。
	if (!ddbInteractionSetList.enable) return;
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
		const wstring ddbDirectory = pluginPath + L"DesktopDrawpadBlocker";
		const wstring ddbExecutable = ddbDirectory + L"\\DesktopDrawpadBlocker.exe";
		DWORD directoryAttributes = GetFileAttributesW(ddbDirectory.c_str());
		if (directoryAttributes == INVALID_FILE_ATTRIBUTES)
		{
			error_code error;
			filesystem::create_directories(ddbDirectory, error);
			if (error) return;
			directoryAttributes = GetFileAttributesW(ddbDirectory.c_str());
		}
		if (directoryAttributes == INVALID_FILE_ATTRIBUTES ||
			!(directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(directoryAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return;
		const DWORD executableAttributes = GetFileAttributesW(ddbExecutable.c_str());
		if (executableAttributes != INVALID_FILE_ATTRIBUTES &&
			(executableAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			return;
		if (_waccess((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
		{
			if (!Inkeys::Load::ExtractResourceFile(ddbExecutable, L"EXE", MAKEINTRESOURCE(237))) return;
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
				if (!Inkeys::Load::ExtractResourceFile(ddbExecutable, L"EXE", MAKEINTRESOURCE(237))) return;
			}
		}

		// 所有入口只经同一来源/普通文件/固定 hash 门启动。
		if (!isProcessRunning((pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
		{
			DdbWriteInteraction(true, false);
			if (!LaunchVerifiedDesktopDrawpadBlocker(ddbInteractionSetList.runAsAdmin)
				&& IDTLogger)
				IDTLogger->error("[DesktopDrawpadBlocker] 最终 EXE 校验或启动失败");
		}
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
