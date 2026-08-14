module;

#include "Setting.Wrap.h"

// ImFluent 使用 imgui_internal.h，升级 Dear ImGui 时必须同步验证固定快照。
#if IMGUI_VERSION_NUM != 19270
#error "Inkeys Setting requires Dear ImGui 1.92.7 (IMGUI_VERSION_NUM == 19270)"
#endif

#include "../../../IdtConfiguration.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtHistoricalDrawpad.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../IdtImage.h"
#include "../../../IdtMagnification.h"
#include "../../../IdtOther.h"
#include "../../../IdtPlug-in.h"
#include "../../../IdtRts.h"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include "Setting.SessionState.h"
#include "../../../SuperTop/IdtSuperTop.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")

module Inkeys.UI.Setting;

import Inkeys.UI.Bar;
import Inkeys.UI.Ppt;
import Inkeys.UI.RenderPipeline;
import Inkeys.Helper.Thread;
import Inkeys.Net.Update;
import Inkeys.Load;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Helper.CrashHandler;
import Inkeys.Other.Config;
import Inkeys.Window;

// 从 imgui_impl_win32.cpp 中前向声明消息处理器
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

using namespace std;

namespace
{
	using Inkeys::UI::RenderPipeline::FrameContext;
	using Inkeys::UI::RenderPipeline::FrameResult;

	atomic<bool> settingInitialized = false;
	atomic<bool> settingSessionShouldStop = false;
	mutex settingLifecycleMutex;
	// Win32 backend 会调用 SetCapture/ReleaseCapture/IME，可能同步重入同一 WndProc。
	recursive_mutex settingImguiMutex;
	mutex settingStateMutex;
	mutex settingDrainMutex;
	condition_variable settingDrainCondition;
	bool settingSessionDrained = true;
	mutex settingInitializeMutex;
	condition_variable settingInitializeCondition;
	bool settingInitializeCompleted = false;
	bool settingInitializeSucceeded = false;
	atomic<uint64_t> settingThemeSerial = 1;
	uint64_t settingConsumedThemeSerial = 0;
	FrameContext settingFrameContext;
	FrameResult settingFrameResult = FrameResult::Idle;
	uint64_t settingSessionEpoch = 0;
	Inkeys::UI::Setting::SessionState settingSessionState;
	Inkeys::UI::Setting::BusinessCompletionSnapshot settingLastBusinessCompletion;

	[[nodiscard]] UINT QuerySettingDpi(HWND hwnd) noexcept
	{
		using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
		static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
		const UINT dpi = getDpiForWindow && hwnd ? getDpiForWindow(hwnd) : 0;
		if (dpi) return dpi;
		HDC screen = GetDC(nullptr);
		const UINT fallback = screen
			? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX)) : USER_DEFAULT_SCREEN_DPI;
		if (screen) ReleaseDC(nullptr, screen);
		return fallback ? fallback : USER_DEFAULT_SCREEN_DPI;
	}

	void UpdateSettingScale(UINT dpi) noexcept
	{
		settingUserScale = Inkeys::UI::Setting::NormalizeUserScale(
			setlist.settingGlobalScale);
		settingSystemDpiScale = Inkeys::UI::Setting::DpiScale(dpi);
		settingGlobalScale = settingSystemDpiScale * settingUserScale;
	}

	[[nodiscard]] bool QueryAppsUseLightTheme() noexcept
	{
		DWORD value = 1;
		DWORD size = sizeof(value);
		RegGetValueW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
		return value != 0;
	}

	[[nodiscard]] ImFluentThemePreset QuerySystemTheme() noexcept
	{
		HIGHCONTRASTW contrast{ sizeof(contrast) };
		const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST,
			sizeof(contrast), &contrast, 0)
			&& (contrast.dwFlags & HCF_HIGHCONTRASTON);
		switch (Inkeys::UI::Setting::ResolveThemeMode(
			highContrast, QueryAppsUseLightTheme()))
		{
		case Inkeys::UI::Setting::ThemeMode::HighContrast:
			return ImFluentThemePreset_HighContrast;
		case Inkeys::UI::Setting::ThemeMode::Dark:
			return ImFluentThemePreset_Dark;
		default:
			return ImFluentThemePreset_Light;
		}
	}

	void ApplySystemTheme(HWND hwnd) noexcept
	{
		const ImFluentThemePreset preset = QuerySystemTheme();
		ImFluent::SetThemePreset(preset);
		// 最小客户区仍需完整容纳全部路由；宽度由响应式模式另行控制。
		ImFluent::GetStyle().NavItemHeight = 36.0F;
		Widgets::style.ApplyGlobal(12.0F);
		const BOOL darkFrame = preset == ImFluentThemePreset_Dark;
		if (hwnd)
			DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
				&darkFrame, sizeof(darkFrame));
	}

	[[nodiscard]] LRESULT HitTestSettingWindow(HWND hwnd, LPARAM lParam) noexcept
	{
		RECT bounds{};
		if (!GetWindowRect(hwnd, &bounds)) return HTCLIENT;
		const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		const int border = max(4, Inkeys::UI::Setting::ScaleDip(8.0F,
			settingSystemDpiScale));
		if (!IsZoomed(hwnd))
		{
			const bool left = point.x < bounds.left + border;
			const bool right = point.x >= bounds.right - border;
			const bool top = point.y < bounds.top + border;
			const bool bottom = point.y >= bounds.bottom - border;
			if (top && left) return HTTOPLEFT;
			if (top && right) return HTTOPRIGHT;
			if (bottom && left) return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;
			if (left) return HTLEFT;
			if (right) return HTRIGHT;
			if (top) return HTTOP;
			if (bottom) return HTBOTTOM;
		}

		const int titleHeight = Inkeys::UI::Setting::ScaleDip(
			Inkeys::UI::Setting::TitleBarHeightDip, settingGlobalScale);
		const int buttonWidth = Inkeys::UI::Setting::ScaleDip(46.0F, settingGlobalScale);
		if (point.y < bounds.top + titleHeight)
		{
			if (point.x >= bounds.right - buttonWidth) return HTCLOSE;
			if (point.x >= bounds.right - buttonWidth * 2) return HTMAXBUTTON;
			if (point.x >= bounds.right - buttonWidth * 3) return HTMINBUTTON;
			return HTCAPTION;
		}
		return HTCLIENT;
	}

	enum class SettingBusinessKind
	{
		WriteSetting,
		WritePptSetting,
		WriteConfig,
		ShellExecute,
		Information,
		ConfirmRestart,
		Restart,
		Close,
		ShowWindow,
		HideWindow,
		ClearInstallerAndSetAutoUpdate,
		ClearInstallerAndSetChannel,
		ClearInstallerAndSetArchitecture,
		CreateShortcut,
		ConfigureDdb,
		RestartDdb,
		WriteDdb,
		SetStartup,
		StartAutomaticUpdate,
	};

	struct SettingBusinessCommand
	{
		SettingBusinessKind kind = SettingBusinessKind::WriteSetting;
		wstring text;
		wstring verb;
		wstring parameters;
		wstring directory;
		string value;
		string digest;
		string jsonPayload;
		string ddbCloseJsonPayload;
		string ddbOpenJsonPayload;
		shared_ptr<Inkeys::Config> configSnapshot;
		bool flag = false;
		bool secondaryFlag = false;
		int showCommand = SW_SHOW;
	};

	class SettingBusinessQueue
	{
	public:
		bool Start()
		{
			lock_guard lock(mutex_);
			if (worker_.joinable()) return true;
			stopping_ = false;
			try
			{
				worker_ = jthread([this](stop_token token) { Run(token); });
			}
			catch (...)
			{
				return false;
			}
			return true;
		}

		void Stop() noexcept
		{
			{
				lock_guard lock(mutex_);
				stopping_ = true;
			}
			condition_.notify_all();
			if (worker_.joinable())
			{
				worker_.join();
			}
			lock_guard lock(mutex_);
			commands_.clear();
		}

		void Enqueue(SettingBusinessCommand command)
		{
			{
				lock_guard lock(mutex_);
				if (stopping_ || !worker_.joinable()) return;
				// FIFO 节点完整拥有 payload，禁止借用帧内字符串或临时对象。
				commands_.push_back(std::move(command));
			}
			condition_.notify_one();
		}

	private:
		void Run(stop_token token) noexcept
		{
			for (;;)
			{
				SettingBusinessCommand command;
				{
					unique_lock lock(mutex_);
					condition_.wait(lock, token, [this]
						{ return stopping_ || !commands_.empty(); });
					// 停止生产后继续排空既有命令，避免退出时丢失最后一次配置写盘。
					if (commands_.empty() && (stopping_ || token.stop_requested())) break;
					if (commands_.empty()) continue;
					command = std::move(commands_.front());
					commands_.pop_front();
				}
				bool succeeded = false;
				try { succeeded = Execute(command); }
				catch (...) { succeeded = false; }
				{
					lock_guard stateLock(settingStateMutex);
					const auto nextSerial =
						settingSessionState.BusinessCompletion().serial + 1;
					// worker 只在此发布不可变完成快照，渲染线程负责消费。
					settingSessionState.PublishBusinessCompletion(nextSerial, succeeded);
				}
				Inkeys::UI::RenderPipeline::Request(
					Inkeys::UI::RenderPipeline::Client::Settings);
			}
		}

		static bool Execute(const SettingBusinessCommand& command)
		{
			switch (command.kind)
			{
			case SettingBusinessKind::WriteSetting:
				return WriteSettingJson(command.jsonPayload);
			case SettingBusinessKind::WritePptSetting:
				return WritePptComSettingJson(command.jsonPayload);
			case SettingBusinessKind::WriteConfig:
				return command.configSnapshot && command.configSnapshot->Write();
			case SettingBusinessKind::ShellExecute:
				return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
					command.verb.empty() ? nullptr : command.verb.c_str(),
					command.text.c_str(),
					command.parameters.empty() ? nullptr : command.parameters.c_str(),
					command.directory.empty() ? nullptr : command.directory.c_str(),
					command.showCommand)) > 32;
			case SettingBusinessKind::Information:
				return MessageBoxW(nullptr, command.text.c_str(),
					L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK) != 0;
			case SettingBusinessKind::ConfirmRestart:
				if (MessageBoxW(nullptr, command.text.c_str(),
					L"Inkeys Tips | 智绘教提示",
					MB_OKCANCEL | MB_SYSTEMMODAL) == IDOK)
					RestartProgram();
				return true;
			case SettingBusinessKind::Restart:
				RestartProgram();
				return true;
			case SettingBusinessKind::Close:
				CloseProgram();
				return true;
			case SettingBusinessKind::ShowWindow:
				return Inkeys::Window::GetService().Show(
					Inkeys::Window::WindowRole::Setting);
			case SettingBusinessKind::HideWindow:
				return Inkeys::Window::GetService().Hide(
					Inkeys::Window::WindowRole::Setting);
			case SettingBusinessKind::ClearInstallerAndSetAutoUpdate:
			case SettingBusinessKind::ClearInstallerAndSetChannel:
			case SettingBusinessKind::ClearInstallerAndSetArchitecture:
			{
				const auto installerDir = globalPath + L"installer";
				error_code ec;
				if (filesystem::exists(installerDir, ec)) filesystem::remove_all(installerDir, ec);
				if (!ec) filesystem::create_directory(installerDir, ec);
				if (ec) return false;
				return WriteSettingJson(command.jsonPayload);
			}
			case SettingBusinessKind::CreateShortcut:
				if (_waccess(command.text.c_str(), 0) == -1
					|| !shortcutAssistant.IsShortcutPointingToDirectory(
						command.text, command.directory))
					shortcutAssistant.CreateShortcut(command.text, command.directory);
				return true;
			case SettingBusinessKind::ConfigureDdb:
			{
				const wstring& executable = command.text;
				(void)WriteSettingJson(command.jsonPayload);
				if (command.flag)
				{
					error_code ec;
					filesystem::create_directories(command.directory, ec);
					if (ec) return false;
					bool extract = _waccess(executable.c_str(), 0) == -1;
					if (!extract && !command.digest.empty())
					{
						sha256wrapper wrapper;
						extract = wrapper.getHashFromFileW(executable) != command.digest;
						if (extract && isProcessRunning(executable.c_str()))
						{
							WriteDdbInteractionJson(command.ddbCloseJsonPayload);
							for (int i = 0; i < 20 && isProcessRunning(executable.c_str()); ++i)
								this_thread::sleep_for(chrono::milliseconds(500));
						}
					}
					if (extract)
						Inkeys::Load::ExtractResourceFile(executable.c_str(), L"EXE", MAKEINTRESOURCE(237));
					if (!isProcessRunning(executable.c_str()))
					{
						WriteDdbInteractionJson(command.ddbOpenJsonPayload);
						return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
							command.secondaryFlag ? L"runas" : nullptr, executable.c_str(),
							nullptr, nullptr, SW_SHOWNORMAL)) > 32;
					}
					return true;
				}
				WriteDdbInteractionJson(command.ddbCloseJsonPayload);
				SetStartupState(false, executable, L"$Inkeys_DesktopDrawpadBlocker");
				error_code ec;
				filesystem::remove(command.directory + L"\\start_up.signal", ec);
				return true;
			}
			case SettingBusinessKind::RestartDdb:
				if (!isProcessRunning(command.text.c_str())) return true;
				WriteDdbInteractionJson(command.ddbCloseJsonPayload);
				for (int i = 0; i < 25 && isProcessRunning(command.text.c_str()); ++i)
					this_thread::sleep_for(chrono::milliseconds(500));
				WriteDdbInteractionJson(command.ddbOpenJsonPayload);
				return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
					command.flag ? L"runas" : nullptr, command.text.c_str(),
					nullptr, nullptr, SW_SHOWNORMAL)) > 32;
			case SettingBusinessKind::WriteDdb:
				(void)WriteSettingJson(command.jsonPayload);
				return WriteDdbInteractionJson(command.ddbCloseJsonPayload);
			case SettingBusinessKind::SetStartup:
				return SetStartupState(command.flag, command.text,
					command.parameters);
			case SettingBusinessKind::StartAutomaticUpdate:
				thread(AutomaticUpdate).detach();
				return true;
			}
			return false;
		}

		mutex mutex_;
		condition_variable_any condition_;
		deque<SettingBusinessCommand> commands_;
		jthread worker_;
		bool stopping_ = true;
	};

	SettingBusinessQueue settingBusinessQueue;

	void QueueBusiness(SettingBusinessCommand command)
	{
		// 在生产者线程冻结配置内容，worker 只消费 owned payload 并执行 I/O。
		switch (command.kind)
		{
		case SettingBusinessKind::WriteSetting:
			command.jsonPayload = CaptureSettingJson();
			break;
		case SettingBusinessKind::ClearInstallerAndSetAutoUpdate:
		case SettingBusinessKind::ClearInstallerAndSetChannel:
		case SettingBusinessKind::ClearInstallerAndSetArchitecture:
			{
				unique_lock<shared_mutex> lock(setlistUpdateMutex);
				if (command.kind == SettingBusinessKind::ClearInstallerAndSetAutoUpdate)
					setlist.enableAutoUpdate = command.flag;
				else if (command.kind == SettingBusinessKind::ClearInstallerAndSetChannel)
					setlist.UpdateChannel = command.value;
				else
					setlist.updateArchitecture = command.value;
			}
			command.jsonPayload = CaptureSettingJson();
			break;
		case SettingBusinessKind::ConfigureDdb:
			ddbInteractionSetList.enable = command.flag;
			ddbInteractionSetList.runAsAdmin = command.secondaryFlag;
			ddbInteractionSetList.hostPath = command.parameters;
			command.jsonPayload = CaptureSettingJson();
			command.ddbCloseJsonPayload = CaptureDdbInteractionJson(true, true);
			command.ddbOpenJsonPayload = CaptureDdbInteractionJson(true, false);
			break;
		case SettingBusinessKind::RestartDdb:
			command.ddbCloseJsonPayload = CaptureDdbInteractionJson(true, true);
			command.ddbOpenJsonPayload = CaptureDdbInteractionJson(true, false);
			break;
		case SettingBusinessKind::WriteDdb:
			command.jsonPayload = CaptureSettingJson();
			command.ddbCloseJsonPayload = CaptureDdbInteractionJson(
				command.flag, command.secondaryFlag);
			break;
		case SettingBusinessKind::WritePptSetting:
			command.jsonPayload = CapturePptComSettingJson();
			break;
		case SettingBusinessKind::WriteConfig:
			command.configSnapshot = make_shared<Inkeys::Config>();
			*command.configSnapshot = Inkeys::config;
			break;
		default:
			break;
		}
		settingBusinessQueue.Enqueue(std::move(command));
	}

	void QueueWriteSetting()
	{
		QueueBusiness({ SettingBusinessKind::WriteSetting });
	}

	void QueuePptComWriteSetting()
	{
		QueueBusiness({ SettingBusinessKind::WritePptSetting });
	}

	void QueueConfigWrite()
	{
		QueueBusiness({ SettingBusinessKind::WriteConfig });
	}

	void QueueShellExecute(wstring target, wstring verb = {},
		wstring parameters = {}, wstring directory = {}, int showCommand = SW_SHOW)
	{
		SettingBusinessCommand command;
		command.kind = SettingBusinessKind::ShellExecute;
		command.text = std::move(target);
		command.verb = std::move(verb);
		command.parameters = std::move(parameters);
		command.directory = std::move(directory);
		command.showCommand = showCommand;
		QueueBusiness(std::move(command));
	}

	void QueueInformation(wstring message)
	{
		QueueBusiness({ SettingBusinessKind::Information, std::move(message) });
	}

	void QueueConfirmRestart(wstring message)
	{
		QueueBusiness({ SettingBusinessKind::ConfirmRestart, std::move(message) });
	}

	void QueueRestart()
	{
		QueueBusiness({ SettingBusinessKind::Restart });
	}

	void QueueClose()
	{
		QueueBusiness({ SettingBusinessKind::Close });
	}

	void QueueDdbWriteInteraction(bool change, bool close)
	{
		SettingBusinessCommand command;
		command.kind = SettingBusinessKind::WriteDdb;
		command.flag = change;
		command.secondaryFlag = close;
		QueueBusiness(std::move(command));
	}

	bool QueueSetStartup(bool enabled, wstring path, const wstring& name)
	{
		SettingBusinessCommand command;
		command.kind = SettingBusinessKind::SetStartup;
		command.flag = enabled;
		command.text = std::move(path);
		command.parameters = name;
		QueueBusiness(std::move(command));
		return true;
	}

	void QueueAutomaticUpdate()
	{
		QueueBusiness({ SettingBusinessKind::StartAutomaticUpdate });
	}

	HINSTANCE QueueShellExecuteCompat(HWND, LPCWSTR operation, LPCWSTR file,
		LPCWSTR parameters, LPCWSTR directory, INT showCommand)
	{
		QueueShellExecute(file ? file : L"", operation ? operation : L"",
			parameters ? parameters : L"", directory ? directory : L"", showCommand);
		return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(33));
	}

	struct SettingSessionCoroutine
	{
		struct promise_type
		{
			SettingSessionCoroutine get_return_object() noexcept
			{
				return SettingSessionCoroutine(
					coroutine_handle<promise_type>::from_promise(*this));
			}
			suspend_always initial_suspend() const noexcept { return {}; }
			suspend_always final_suspend() const noexcept { return {}; }
			void return_void() const noexcept {}
			void unhandled_exception() const noexcept { terminate(); }
		};

		SettingSessionCoroutine() = default;
		explicit SettingSessionCoroutine(coroutine_handle<promise_type> value) noexcept
			: handle(value) {}
		SettingSessionCoroutine(SettingSessionCoroutine&& other) noexcept
			: handle(exchange(other.handle, {})) {}
		SettingSessionCoroutine& operator=(SettingSessionCoroutine&& other) noexcept
		{
			if (this == &other) return *this;
			Reset();
			handle = exchange(other.handle, {});
			return *this;
		}
		~SettingSessionCoroutine() { Reset(); }
		SettingSessionCoroutine(const SettingSessionCoroutine&) = delete;
		SettingSessionCoroutine& operator=(const SettingSessionCoroutine&) = delete;

		void Resume()
		{
			if (handle && !handle.done()) handle.resume();
		}
		[[nodiscard]] bool Done() const noexcept { return !handle || handle.done(); }
		void Reset() noexcept
		{
			if (handle) handle.destroy();
			handle = {};
		}

		coroutine_handle<promise_type> handle{};
	};

	SettingSessionCoroutine settingSession;
}

static void SyncUi3BuiltInComponents()
{
	barUISet.barButtonSet.SyncLegacyExtensionButtons();
	barUISet.UpdateRendering();
}

// 软件构建信息
// signal1
struct
{
	wstring url;

	wstring repoUrl;
	wstring branch;
	wstring submitter;
	wstring buildTime;

	wstring buildOS;
	wstring buildOSVersion;
	wstring buildRunnerImageOS;
	wstring buildRunnerImageVersion;

	wstring msBuildVersion;
} settingCICD;
// signal1

// Win32 消息处理器
// 您可以阅读 io.WantCaptureMouse、io.WantCaptureKeyboard 标志，以了解 dear imgui 是否想使用您的输入。
// - 当 io.WantCaptureMouse 为 true 时，请勿将鼠标输入数据发送到主应用程序，或者清除/覆盖鼠标数据的副本。
// - 当 io.WantCaptureKeyboard 为 true 时，请勿将键盘输入数据发送到主应用程序，或者清除/覆盖键盘数据的副本。
// 通常，您可以始终将所有输入传递给 dear imgui，并根据这两个标志在应用程序中隐藏它们。
LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (Inkeys::UI::Setting::IsVisible())
	{
		// HWND 线程只更新 IO；context/backend/draw/present 仍由渲染线程拥有。
		lock_guard lock(settingImguiMutex);
		if (ImGui::GetCurrentContext()
			&& ImGui_ImplWin32_WndProcHandlerEx(
				hWnd, msg, wParam, lParam, ImGui::GetIO()))
			return true;
	}

	switch (msg)
	{
	case WM_NCCALCSIZE:
		// 保留可缩放窗口样式，只把标题栏和边框绘制交给 ImGui。
		if (wParam) return 0;
		break;
	case WM_NCHITTEST:
	{
		return HitTestSettingWindow(hWnd, lParam);
	}
	case WM_GETMINMAXINFO:
	{
		auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
		if (!minMaxInfo) return 0;
		int minimumWidth = Inkeys::UI::Setting::ScaleDip(
			Inkeys::UI::Setting::MinimumWidthDip, settingGlobalScale);
		int minimumHeight = Inkeys::UI::Setting::ScaleDip(
			Inkeys::UI::Setting::MinimumHeightDip, settingGlobalScale);
		if (const HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST))
		{
			MONITORINFO monitorInfo{ sizeof(monitorInfo) };
			if (GetMonitorInfoW(monitor, &monitorInfo))
			{
				const int workWidth = monitorInfo.rcWork.right
					- monitorInfo.rcWork.left;
				const int workHeight = monitorInfo.rcWork.bottom
					- monitorInfo.rcWork.top;
				minimumWidth = Inkeys::UI::Setting::ResolveWindowExtent(
					Inkeys::UI::Setting::MinimumWidthDip,
					settingGlobalScale, workWidth);
				minimumHeight = Inkeys::UI::Setting::ResolveWindowExtent(
					Inkeys::UI::Setting::MinimumHeightDip,
					settingGlobalScale, workHeight);
				minMaxInfo->ptMaxPosition = {
					monitorInfo.rcWork.left - monitorInfo.rcMonitor.left,
					monitorInfo.rcWork.top - monitorInfo.rcMonitor.top };
				minMaxInfo->ptMaxSize = {
					workWidth, workHeight };
			}
		}
		minMaxInfo->ptMinTrackSize = { minimumWidth, minimumHeight };
		return 0;
	}
	case WM_DPICHANGED:
	{
		UpdateSettingScale(LOWORD(wParam));
		{
			lock_guard stateLock(settingStateMutex);
			// DPI 变化只请求字体图集重建，普通窗口缩放不触碰字体资源。
			settingSessionState.QueueFontRebuild();
		}
		const auto* suggested = reinterpret_cast<const RECT*>(lParam);
		if (suggested)
		{
			SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return 0;
	}
	case WM_THEMECHANGED:
	case WM_SETTINGCHANGE:
		settingThemeSerial.fetch_add(1, memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		break;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		{
			SettingWindowWidth = static_cast<int>(LOWORD(lParam));
			SettingWindowHeight = static_cast<int>(HIWORD(lParam));
			lock_guard stateLock(settingStateMutex);
			settingSessionState.QueueResize(
				static_cast<UINT>(LOWORD(lParam)),
				static_cast<UINT>(HIWORD(lParam)));
		}
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;

		// 拦截任务栏关闭指令
		if ((wParam & 0xFFF0) == SC_CLOSE)
		{
			Inkeys::UI::Setting::Hide();
			return 0;
		}

		break;

	case WM_CLOSE:
		Inkeys::UI::Setting::Hide();
		return 0;
	case WM_DESTROY:
	{
		// 防御其他流氓软件关闭我的窗口
		return 0;
	}

	case WM_MOVE:
		RECT rect;
		GetWindowRect(hWnd, &rect);

		SettingWindowX = rect.left;
		SettingWindowY = rect.top;

		break;
	}

	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void SettingWindowBegin()
{
	// 尺寸计算
	{
		UpdateSettingScale(QuerySettingDpi(nullptr));
		RECT workArea{ 0, 0, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight };
		(void)SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
		const int rawWorkWidth = static_cast<int>(workArea.right - workArea.left);
		const int rawWorkHeight = static_cast<int>(workArea.bottom - workArea.top);
		const int workWidth = rawWorkWidth > 1 ? rawWorkWidth : 1;
		const int workHeight = rawWorkHeight > 1 ? rawWorkHeight : 1;
		// 高 DPI 叠加用户倍率时，启动窗口仍需完整落在工作区内。
		SettingWindowWidth = Inkeys::UI::Setting::ResolveWindowExtent(
			Inkeys::UI::Setting::DefaultWidthDip, settingGlobalScale, workWidth);
		SettingWindowHeight = Inkeys::UI::Setting::ResolveWindowExtent(
			Inkeys::UI::Setting::DefaultHeightDip, settingGlobalScale, workHeight);
		SettingWindowX = workArea.left + max(0,
			(workWidth - SettingWindowWidth) / 2);
		SettingWindowY = workArea.top + max(0,
			(workHeight - SettingWindowHeight) / 2);
	}
}

[[nodiscard]] bool AddSettingFontResource(UINT resourceId, float size,
	ImFontConfig* config, ImFont** result = nullptr)
{
	HMODULE module = GetModuleHandleW(nullptr);
	HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), L"TTF");
	if (!resource) return false;
	HGLOBAL loaded = LoadResource(module, resource);
	void* data = loaded ? LockResource(loaded) : nullptr;
	const DWORD dataSize = SizeofResource(module, resource);
	if (!data || !dataSize) return false;
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
		data, dataSize, size, config);
	if (result) *result = font;
	return font != nullptr;
}

[[nodiscard]] bool RebuildSettingFonts()
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	ImFontMain = nullptr;
	ImGui::GetStyle().FontScaleDpi = settingGlobalScale;

	ImFontConfig textConfig;
	textConfig.OversampleH = 1;
	textConfig.OversampleV = 1;
	textConfig.RasterizerDensity = settingGlobalScale;
	textConfig.FontDataOwnedByAtlas = false;
	static constexpr ImWchar excludedGlyphs[] = { 0xe81e, 0xe81e, 0 };
	textConfig.GlyphExcludeRanges = excludedGlyphs;
	const UINT primaryResource = I18n::isIdentifying(L"zh-TW") ? 258U : 198U;
	if (!AddSettingFontResource(primaryResource, 30.0F,
		&textConfig, &ImFontMain))
		return false;

	if (I18n::isIdentifying(L"zh-TW"))
	{
		// 繁体字库后合并简体字形，保持单一文本字体入口。
		textConfig.MergeMode = true;
		if (!AddSettingFontResource(198U, 30.0F,
			&textConfig, &ImFontMain))
			return false;
	}

	ImFontConfig iconConfig;
	iconConfig.OversampleH = 1;
	iconConfig.OversampleV = 1;
	iconConfig.RasterizerDensity = settingGlobalScale;
	iconConfig.FontDataOwnedByAtlas = false;
	iconConfig.GlyphOffset.y = 10.0F;
	iconConfig.PixelSnapH = true;
	iconConfig.MergeMode = true;
	if (!AddSettingFontResource(257U, 36.0F,
		&iconConfig, &ImFontMain))
		return false;
	iconConfig.GlyphOffset.y = 4.0F;
	if (!AddSettingFontResource(262U, 32.0F,
		&iconConfig, &ImFontMain))
		return false;

	io.FontDefault = ImFontMain;
	// ImFluent 只绑定项目内嵌字体，文字层级仍使用 DIP 尺寸。
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Caption,
		ImFontMain, 12.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body,
		ImFontMain, 14.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_BodyStrong,
		ImFontMain, 14.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Subtitle,
		ImFontMain, 20.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Title,
		ImFontMain, 28.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_TitleLarge,
		ImFontMain, 36.0F);
	ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Display,
		ImFontMain, 64.0F);
	// Initialize/隐藏态重建都必须先完成 atlas，Show 不承担字体工作。
	return io.Fonts->Build();
}

SettingSessionCoroutine RunSettingSession()
{
	// 本作用域内所有潜在阻塞业务统一投递给单一 FIFO worker。
	#define WriteSetting QueueWriteSetting
	#define PptComWriteSetting QueuePptComWriteSetting
	#define ShellExecuteW QueueShellExecuteCompat
	#define SetStartupState QueueSetStartup
	#define RestartProgram QueueRestart
	#define CloseProgram QueueClose
	auto GetUpdateChannel = []()
		{
			shared_lock<shared_mutex> lock(setlistUpdateMutex);
			return setlist.UpdateChannel;
		};
	auto SetUpdateChannel = [](const string& channel)
		{
			unique_lock<shared_mutex> lock(setlistUpdateMutex);
			setlist.UpdateChannel = channel;
		};
	auto GetUpdateArchitecture = []()
		{
			shared_lock<shared_mutex> lock(setlistUpdateMutex);
			return setlist.updateArchitecture;
		};
	auto SetUpdateArchitecture = [](const string& architecture)
		{
			unique_lock<shared_mutex> lock(setlistUpdateMutex);
			setlist.updateArchitecture = architecture;
		};
	auto GetEnableAutoUpdate = []()
		{
			shared_lock<shared_mutex> lock(setlistUpdateMutex);
			return setlist.enableAutoUpdate;
		};
	auto SetEnableAutoUpdate = [](bool enable)
		{
			unique_lock<shared_mutex> lock(setlistUpdateMutex);
			setlist.enableAutoUpdate = enable;
		};
	// Win11 风格滚动条使用较窄视觉宽度，条目宽度由内容区反推，避免文字空间被压缩。
	float settingContentOriginX = 170.0F;
	float settingContentPanelWidth = 780.0F;
	float settingContentPanelHeight = 608.0F;
	constexpr float settingWin11ScrollbarWidth = 12.0F;
	constexpr float settingContentRightGap = 10.0F;
	float settingItemWidth = 758.0F;
	float settingRightComboX = 538.0F;
	float settingRightToggleX = 698.0F;
	float settingRightButtonX = 638.0F;
	float settingRightButtonPairLeftX = 533.0F;
	float settingDescriptionWidth = 718.0F;
	float settingPromptWidth = 678.0F;
	float settingDescriptionBeforeComboWidth = 518.0F;
	float settingPromptBeforeButtonWidth = 568.0F;

	{
		if (!AcquireDeviceLease(settingFrameContext.epoch))
		{
			if (IDTLogger) IDTLogger->error("[Setting] 获取共享 D3D11 设置资源失败");
			CleanupDeviceD3D();
			settingFrameResult = FrameResult::Retry;
			co_return;
		}
		settingSessionEpoch = settingFrameContext.epoch.generation;

			// 初始化
			{
				// 图像加载
				{
					Inkeys::Graphics::DibSurface SettingSign;

					if (I18n::isIdentifying(L"zh-CN")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home1_zh-CN", 700 * settingGlobalScale, 215 * settingGlobalScale);
					else if (I18n::isIdentifying(L"zh-TW")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home1_zh-TW", 700 * settingGlobalScale, 215 * settingGlobalScale);
					else LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home1_en-US", 700 * settingGlobalScale, 215 * settingGlobalScale);
					{
						int width = settingSign[1].width = SettingSign.width();
						int height = settingSign[1].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[1]);
						delete[] data;

						(void)ret;
					}

					if (I18n::isIdentifying(L"zh-CN")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home2_zh-CN", 770 * settingGlobalScale, 390 * settingGlobalScale);
					else if (I18n::isIdentifying(L"zh-TW")) LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home2_zh-TW", 770 * settingGlobalScale, 390 * settingGlobalScale);
					else LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home2_en-US", 770 * settingGlobalScale, 390 * settingGlobalScale);
					{
						int width = settingSign[2].width = SettingSign.width();
						int height = settingSign[2].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[2]);
						delete[] data;

						(void)ret;
					}

					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag1", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[5].width = SettingSign.width();
						int height = settingSign[5].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[5]);
						delete[] data;

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag2", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[6].width = SettingSign.width();
						int height = settingSign[6].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[6]);
						delete[] data;

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag3", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[8].width = SettingSign.width();
						int height = settingSign[8].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[8]);
						delete[] data;

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"PluginFlag4", 30 * settingGlobalScale, 30 * settingGlobalScale);
					{
						int width = settingSign[10].width = SettingSign.width();
						int height = settingSign[10].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[10]);
						delete[] data;

						(void)ret;
					}

					LoadSurfaceFromResource(&SettingSign, L"PNG", L"Profile_Picture", 45 * settingGlobalScale, 45 * settingGlobalScale);
					{
						int width = settingSign[3].width = SettingSign.width();
						int height = settingSign[3].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[3]);
						delete[] data;

						(void)ret;
					}
					LoadSurfaceFromResource(&SettingSign, L"PNG", L"Home_Feedback", 100 * settingGlobalScale, 100 * settingGlobalScale);
					{
						int width = settingSign[7].width = SettingSign.width();
						int height = settingSign[7].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[7]);
						delete[] data;

						(void)ret;
					}

					LoadSurfaceFromResource(&SettingSign, L"PNG", L"SettingSponsor", 650 * settingGlobalScale, 460 * settingGlobalScale);
					{
						int width = settingSign[9].width = SettingSign.width();
						int height = settingSign[9].height = SettingSign.height();
						auto* pMem = SettingSign.pixels().data();

						unsigned char* data = new unsigned char[width * height * 4];
						for (int y = 0; y < height; ++y)
						{
							for (int x = 0; x < width; ++x)
							{
								DWORD color = pMem[y * width + x];
								unsigned char alpha = (color & 0xFF000000) >> 24;
								if (alpha != 0)
								{
									data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
									data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
									data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
								}
								else
								{
									data[(y * width + x) * 4 + 0] = 0;
									data[(y * width + x) * 4 + 1] = 0;
									data[(y * width + x) * 4 + 2] = 0;
								}
								data[(y * width + x) * 4 + 3] = alpha;
							}
						}

						bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[9]);
						delete[] data;

						(void)ret;
					}
				}
			}
			constexpr size_t requiredTextureIndexes[] = { 1, 2, 3, 5, 6, 7, 8, 9, 10 };
			if (ranges::any_of(requiredTextureIndexes,
				[](size_t index) { return TextureSettingSign[index] == nullptr; }))
			{
				if (IDTLogger) IDTLogger->error("[Setting] 静态图片 resident 预热失败");
				CleanupSettingTextures();
				CleanupSettingTextureCache();
				CleanupDeviceD3D();
				settingFrameResult = FrameResult::Retry;
				co_return;
			}
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.IniFilename = nullptr;

		ImGui::StyleColorsLight();

		const bool win32BackendInitialized = ImGui_ImplWin32_Init(setting_window);
		const bool dx11BackendInitialized = win32BackendInitialized
			&& ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
		if (!dx11BackendInitialized || !RebuildSettingFonts()
			|| !ImGui_ImplDX11_CreateDeviceObjects())
		{
			if (IDTLogger) IDTLogger->error("[Setting] 内嵌字体图集初始化失败");
			if (dx11BackendInitialized) ImGui_ImplDX11_Shutdown();
			if (win32BackendInitialized) ImGui_ImplWin32_Shutdown();
			CleanupSettingTextures();
			CleanupSettingTextureCache();
			io.Fonts->Clear();
			ImFluent::ResetContext();
			ImGui::DestroyContext();
			CleanupDeviceD3D();
			settingFrameResult = FrameResult::Retry;
			co_return;
		}

		ApplySystemTheme(setting_window);
		settingConsumedThemeSerial = settingThemeSerial.load(memory_order_acquire);

		int QuestNumbers = 0;
		int PushStyleColorNum = 0, PushFontNum = 0, PushStyleVarNum = 0;
		int QueryWaitingTime = 5;

		// 设置参数

		struct
		{
			bool Enable = Inkeys::config.Config.AutoClean;
		}ConfigurationSetting;

		bool EnableFixWithChangeArchitecture = true;
		bool EnableAutoUpdate = false;

		int SelectLanguage = setlist.selectLanguage;
		bool StartUp = setlist.startUp;
		bool CreateLnk = setlist.shortcutAssistant.createLnk;
		bool CorrectLnk = setlist.shortcutAssistant.correctLnk;

		int SetSkinMode = setlist.SetSkinMode;
		float SettingGlobalScale = settingUserScale;
		float BarZoom = static_cast<float>(Inkeys::config.UI.Bar.Zoom.load());
		bool BarZoomSavePending = false;

		int TopSleepTime = setlist.topSleepTime;
		bool RightClickClose = setlist.RightClickClose;
		bool BrushRecover = setlist.BrushRecover;
		bool RubberRecover = setlist.RubberRecover;
		struct
		{
			bool MoveRecover = setlist.regularSetting.moveRecover;
			bool ClickRecover = setlist.regularSetting.clickRecover;
			bool AvoidFullScreen = setlist.regularSetting.avoidFullScreen;
			int TeachingSafetyMode = setlist.regularSetting.teachingSafetyMode;
		}RegularSetting;

		struct
		{
			bool Enable = setlist.saveSetting.enable;
			int SaveDays = setlist.saveSetting.saveDays;
		}SaveSetting;

		int PaintDevice = setlist.paintDevice;
		bool LiftStraighten = setlist.liftStraighten, WaitStraighten = setlist.waitStraighten;
		bool PointAdsorption = setlist.pointAdsorption;
		bool SmoothWriting = setlist.smoothWriting;
		int EraserMode = setlist.eraserSetting.eraserMode;
		bool HideTouchPointer = setlist.hideTouchPointer;

		int PreparationQuantity = setlist.performanceSetting.preparationQuantity;
		bool SuperDraw = setlist.performanceSetting.superDraw;

		struct
		{
			bool MemoryWidth = setlist.presetSetting.memoryWidth;
			bool MemoryColor = setlist.presetSetting.memoryColor;

			bool AutoDefaultWidth = setlist.presetSetting.autoDefaultWidth;
			float DefaultBrush1Width = setlist.presetSetting.defaultBrush1Width;
			float DefaultHighlighter1Width = setlist.presetSetting.defaultHighlighter1Width;
		}PresetSetting;

		struct
		{
			struct
			{
				bool Enable = setlist.plugInSetting.superTop.enable;
				bool Indicator = setlist.plugInSetting.superTop.indicator;
			}SuperTop;
		}PlugInSetting;

		// 插件参数

		float PptUiWidgetScale = 1.0f, PptUiWidgetScaleRecord = 1.0f;
		float BottomSideBothWidgetScale = pptComSetlist.bottomSideBothWidgetScale, BottomSideBothWidgetScaleRecord = pptComSetlist.bottomSideBothWidgetScale;
		float MiddleSideBothWidgetScale = pptComSetlist.middleSideBothWidgetScale, MiddleSideBothWidgetScaleRecord = pptComSetlist.middleSideBothWidgetScale;
		float BottomSideMiddleWidgetScale = pptComSetlist.bottomSideMiddleWidgetScale, BottomSideMiddleWidgetScaleRecord = pptComSetlist.bottomSideMiddleWidgetScale;
		bool BottomSideBothWidgetScaleUnifie = true;
		bool MiddleSideBothWidgetScaleUnifie = true;
		bool BottomSideMiddleWidgetScaleUnifie = false;

		bool PptComFixedHandWriting = pptComSetlist.fixedHandWriting;
		bool PptComShowLoadingScreen = pptComSetlist.showLoadingScreen;
		bool MemoryWidgetPosition = pptComSetlist.memoryWidgetPosition;
		bool ShowBottomBoth = pptComSetlist.showBottomBoth;
		bool ShowMiddleBoth = pptComSetlist.showMiddleBoth;
		bool ShowBottomMiddle = pptComSetlist.showBottomMiddle;
		float BottomBothWidth = pptComSetlist.bottomBothWidth;
		float BottomBothHeight = pptComSetlist.bottomBothHeight;
		float MiddleBothWidth = pptComSetlist.middleBothWidth;
		float MiddleBothHeight = pptComSetlist.middleBothHeight;
		float BottomMiddleWidth = pptComSetlist.bottomMiddleWidth;
		float BottomMiddleHeight = pptComSetlist.bottomMiddleHeight;

		//bool AutoKillWpsProcess = pptComSetlist.autoKillWpsProcess;

		struct
		{
			bool Enable = ddbInteractionSetList.enable;
			bool RunAsAdmin = ddbInteractionSetList.runAsAdmin;

			struct
			{
				bool SeewoWhiteboard3Floating = ddbInteractionSetList.intercept.SeewoWhiteboard3Floating;
				bool SeewoWhiteboard5Floating = ddbInteractionSetList.intercept.SeewoWhiteboard5Floating;
				bool SeewoWhiteboard5CFloating = ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating;
				bool SeewoPincoSideBarFloating = ddbInteractionSetList.intercept.SeewoPincoSideBarFloating;
				bool SeewoPincoDrawingFloating = ddbInteractionSetList.intercept.SeewoPincoDrawingFloating;
				bool SeewoPPTFloating = ddbInteractionSetList.intercept.SeewoPPTFloating;
				bool SeewoIwbAssistantFloating = ddbInteractionSetList.intercept.SeewoIwbAssistantFloating;
				bool YiouBoardFloating = ddbInteractionSetList.intercept.YiouBoardFloating;
				bool AiClassFloating = ddbInteractionSetList.intercept.AiClassFloating;
				bool ClassInXFloating = ddbInteractionSetList.intercept.ClassInXFloating;
				bool IntelligentClassFloating = ddbInteractionSetList.intercept.IntelligentClassFloating;
				bool ChangYanFloating = ddbInteractionSetList.intercept.ChangYanFloating;
				bool ChangYan5Floating = ddbInteractionSetList.intercept.ChangYan5Floating;
				bool Iclass30SidebarFloating = ddbInteractionSetList.intercept.Iclass30SidebarFloating;
				bool Iclass30Floating = ddbInteractionSetList.intercept.Iclass30Floating;
				bool SeewoDesktopSideBarFloating = ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating;
				bool SeewoDesktopDrawingFloating = ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating;
			}intercept;
		} Ddb;

		// 组件参数
		bool ComponentShortcutButtonApplianceExplorer = setlist.component.shortcutButton.appliance.explorer;
		bool ComponentShortcutButtonApplianceTaskmgr = setlist.component.shortcutButton.appliance.taskmgr;
		bool ComponentShortcutButtonApplianceControl = setlist.component.shortcutButton.appliance.control;
		bool ComponentShortcutButtonSystemDesktop = setlist.component.shortcutButton.system.desktop;
		bool ComponentShortcutButtonSystemLockWorkStation = setlist.component.shortcutButton.system.lockWorkStation;
		bool ComponentShortcutButtonKeyboardKeyboardesc = setlist.component.shortcutButton.keyboard.keyboardesc;
		bool ComponentShortcutButtonKeyboardKeyboardAltF4 = setlist.component.shortcutButton.keyboard.keyboardAltF4;
		bool ComponentShortcutButtonRollCallIslandCaller1 = setlist.component.shortcutButton.rollCall.IslandCaller1;
		bool ComponentShortcutButtonRollCallIslandCaller2 = setlist.component.shortcutButton.rollCall.IslandCaller2;
		bool ComponentShortcutButtonRollCallSecRandom1 = setlist.component.shortcutButton.rollCall.SecRandom1;
		bool ComponentShortcutButtonRollCallSecRandom2 = setlist.component.shortcutButton.rollCall.SecRandom2;
		bool ComponentShortcutButtonRollCallSecRandom2Compat = setlist.component.shortcutButton.rollCall.SecRandom2Compat;
		bool ComponentShortcutButtonRollCallNamePicker = setlist.component.shortcutButton.rollCall.NamePicker;
		bool ComponentShortcutButtonLinkageClassislandSettings = setlist.component.shortcutButton.linkage.classislandSettings;
		bool ComponentShortcutButtonLinkageClassislandProfile = setlist.component.shortcutButton.linkage.classislandProfile;
		bool ComponentShortcutButtonLinkageClassislandClassswap = setlist.component.shortcutButton.linkage.classislandClassswap;

		// 实验选项
		struct
		{
			struct
			{
				bool AnimationEnable = Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable;
				float AnimationSpeedRate = static_cast<float>(clamp(
					static_cast<double>(Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate), 0.1, 5.0));
				bool AnimationSpeedSavePending = false;
				bool EdgeLightingEnable = Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable;
				bool DynamicEdgeLighting = Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic;
				bool DebugMode = Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable;
				bool ShowFrameRate = Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate;
			}Inkeys3;
		}Experimental;

		// ==========

		wstring receivedData;

		int settingTab = 0;
		int settingPlugInTab = 0;
		int transitionTab = settingTab;
		auto transitionStarted = chrono::steady_clock::now();

		// 首次恢复只完成常驻资源预热；Show 后才创建交换链并进入绘制循环。
		settingFrameResult = FrameResult::Idle;
		co_await suspend_always{};

		while (!settingSessionShouldStop.load(memory_order_acquire))
		{
			// Start the Dear ImGui frame
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			EnableAutoUpdate = GetEnableAutoUpdate();

			{
				//定义栏操作
				enum settingTabEnum
				{
					tab1,
					Language,
					tabCICD,
					tabConfiguration,
					tab2,
					tab3,
					tabPerformance,
					tabPreset,
					tab4,
					tabComponent,
					tab5,
					tab6,
					tab8,
					tab9,
					tabExperimental,
				};
				enum settingPlugInTabEnum
				{
					tabPlug1,
					tabPlug2,
					tabPlug3,
					tabSuperTop,
					tabPlugDDB
				};

				ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
				ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

				ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::WindowBorder);
				ImGui::Begin("主窗口", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);//开始绘制窗口
				ImGui::PopStyleColor();

				// 标题栏高 32px + 8px
				{
					ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
					ImGui::SetCursorPos({ 20.0f * settingGlobalScale,14.0f * settingGlobalScale });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
					ImGui::TextUnformatted(("\ue713   " + IA(I18nKey.SettingsUI.N)).c_str());

					ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
					const float captionButtonWidth = 46.0F * settingGlobalScale;
					const float captionButtonHeight = 32.0F * settingGlobalScale;
					const float captionButtonsX = max(0.0F,
						static_cast<float>(SettingWindowWidth) - captionButtonWidth * 3.0F);
					ImGui::SetCursorPos({ captionButtonsX, 0.0F });
					if (Widgets::button.Standard("\ue921##minimize",
						{ captionButtonWidth, captionButtonHeight }))
						PostMessageW(setting_window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
					ImGui::SameLine(0.0F, 0.0F);
					if (Widgets::button.Standard(IsZoomed(setting_window)
						? "\ue923##restore" : "\ue922##maximize",
						{ captionButtonWidth, captionButtonHeight }))
						PostMessageW(setting_window, WM_SYSCOMMAND,
							IsZoomed(setting_window) ? SC_RESTORE : SC_MAXIMIZE, 0);
					ImGui::SameLine(0.0F, 0.0F);
					if (Widgets::button.TitleBarClose("\ue8bb##close",
						{ captionButtonWidth, captionButtonHeight }))
					{
						PostMessageW(setting_window, WM_SYSCOMMAND, SC_CLOSE, 0);
						barUISet.UpdateRendering();
					}

					if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
					if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
					while (PushFontNum) PushFontNum--, ImGui::PopFont();
				}

				// 三档导航复用同一条目源；业务页仍保留原有根坐标，避免迁移时改变回调。
				bool navigationActivated = false;
				auto renderNavigationItems = [&]()
				{
					ImGui::BeginChild("##setting-navigation-items", { 0.0F, 0.0F });
					auto page = [&](int target, const char* label, const char* glyph)
					{
						if (ImFluent::NavItem(label, settingTab == target, glyph))
						{
							settingTab = target;
							navigationActivated = true;
							if (target == settingTabEnum::tab4)
								settingPlugInTab = settingPlugInTabEnum::tabPlug1;
						}
					};
					auto action = [&](const char* label, const char* glyph, auto&& callback)
					{
						if (!ImFluent::NavItem(label, false, glyph)) return;
						navigationActivated = true;
						callback();
					};

					page(settingTabEnum::tab1, IA(I18nKey.SettingsUI.Home.N).c_str(), "\ue80f");
					page(settingTabEnum::tabConfiguration, IA(I18nKey.SettingsUI.Configuration.N).c_str(), "\ue81e");
					page(settingTabEnum::tab6, IA(I18nKey.SettingsUI.Version.N).c_str(), "\ue946");
					page(settingTabEnum::tab2, IA(I18nKey.SettingsUI.Regular.N).c_str(), "\ue7b8");
					page(settingTabEnum::tab3, IA(I18nKey.SettingsUI.Draw.N).c_str(), "\uee56");
					page(settingTabEnum::tabPreset, IA(I18nKey.SettingsUI.Preset.N).c_str(), "\uf259");
					page(settingTabEnum::tab4, IA(I18nKey.SettingsUI.PlugIn.N).c_str(), "\ue74c");
					page(settingTabEnum::tabComponent, IA(I18nKey.SettingsUI.Component.N).c_str(), "\ue70b");
					page(settingTabEnum::tab5, IA(I18nKey.SettingsUI.HotKey.N).c_str(), "\ue765");
					page(settingTabEnum::tabExperimental, "实验选项", "\uec4a");
					page(settingTabEnum::tab8, IA(I18nKey.SettingsUI.Sponsor.N).c_str(), "\ue789");
					page(settingTabEnum::tab9, IA(I18nKey.SettingsUI.DebugSoftware.N).c_str(), "\ue90f");
					ImGui::Separator();
					action(IA(I18nKey.SettingsUI.Community.N).c_str(), "\ue716", [&]
						{
							if (I18n::isIdentifying(L"zh-CN"))
								ShellExecuteW(0, 0, L"https://www.inkeys.top/community.html", 0, 0, SW_SHOW);
							else
								ShellExecuteW(0, 0, L"https://en.inkeys.top/community.html", 0, 0, SW_SHOW);
						});
					action(IA(I18nKey.SettingsUI.RestartSoftware.N).c_str(), "\ue72c", [&]
						{
							Inkeys::UI::Setting::Hide();
							RestartProgram();
						});
					action(IA(I18nKey.SettingsUI.ExitSoftware.N).c_str(), "\ue711", [&]
						{
							Inkeys::UI::Setting::Hide();
							CloseProgram();
						});
					ImGui::EndChild();
				};

				const auto navigationLayout = Inkeys::UI::Setting::ResolveNavigationLayout(
					static_cast<float>(SettingWindowWidth), settingGlobalScale);
				const float logicalWindowWidth = static_cast<float>(SettingWindowWidth)
					/ max(settingGlobalScale, 0.01F);
				settingContentOriginX = navigationLayout == Inkeys::UI::Setting::NavigationLayout::Open
					? 170.0F : 58.0F;
				settingContentPanelWidth = clamp(logicalWindowWidth
					- settingContentOriginX - 10.0F, 620.0F, 780.0F);
				const float logicalWindowHeight = static_cast<float>(SettingWindowHeight)
					/ max(settingGlobalScale, 0.01F);
				settingContentPanelHeight = clamp(logicalWindowHeight - 82.0F,
					360.0F, 608.0F);
				settingItemWidth = settingContentPanelWidth
					- settingWin11ScrollbarWidth - settingContentRightGap;
				settingRightComboX = settingItemWidth - 220.0F;
				settingRightToggleX = settingItemWidth - 60.0F;
				settingRightButtonX = settingItemWidth - 120.0F;
				settingRightButtonPairLeftX = settingRightButtonX - 105.0F;
				settingDescriptionWidth = settingItemWidth - 40.0F;
				settingPromptWidth = settingItemWidth - 80.0F;
				settingDescriptionBeforeComboWidth = settingRightComboX - 20.0F;
				settingPromptBeforeButtonWidth = settingRightButtonX - 70.0F;
				ImGui::SetCursorPos({ 0.0F, Inkeys::UI::Setting::TitleBarHeightDip * settingGlobalScale });
				ImFluent::PushStyleVar(ImFluentStyleVar_NavPaneOpenWidth, 160.0F);
				ImFluent::PushStyleVar(ImFluentStyleVar_NavPaneCompactWidth, 48.0F);
				static bool narrowPaneOpen = false;
				if (navigationLayout != Inkeys::UI::Setting::NavigationLayout::Overlay)
				{
					ImFluentNavViewMode mode = navigationLayout == Inkeys::UI::Setting::NavigationLayout::Open
						? ImFluentNavViewMode_LeftOpen : ImFluentNavViewMode_LeftCompact;
					ImFluent::SetNextNavPaneToggleButtonVisible(false);
					ImFluent::BeginNavigationView("##setting-navigation", &mode);
					renderNavigationItems();
					ImFluent::EndNavigationView();
				}
				ImFluent::PopStyleVar(2);

				// 页面切换只推进两个标量，避免动画期间加载或分配图形资源。
				if (transitionTab != settingTab)
				{
					transitionTab = settingTab;
					transitionStarted = chrono::steady_clock::now();
				}
				const float transitionProgress =
					Inkeys::UI::Setting::ResolvePageTransitionProgress(chrono::duration<float>(
						chrono::steady_clock::now() - transitionStarted).count());
				settingContentOriginX += (1.0F - transitionProgress) * 8.0F;
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
					ImGui::GetStyle().Alpha * (0.55F + transitionProgress * 0.45F));

				// 主版块 765<-750(770 主页) * 608
				switch (settingTab)
				{
					// 主页
				case settingTabEnum::tab1:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,
						42.0F * settingGlobalScale });
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
						ImVec2(20.0F * settingGlobalScale, 18.0F * settingGlobalScale));
					ImGui::BeginChild("主页", { settingContentPanelWidth * settingGlobalScale,
						settingContentPanelHeight * settingGlobalScale }, false,
						ImGuiWindowFlags_HorizontalScrollbar);

					ImFluent::TextBlock("Inkeys", ImFluentTextStyle_TitleLarge);
					ImFluent::TextBlock(IA(I18nKey.SettingsUI.Home.Prompt).c_str(),
						ImFluentTextStyle_Body);
					ImGui::Dummy({ 0.0F, 12.0F * settingGlobalScale });

					const ImVec2 linkButtonSize(138.0F * settingGlobalScale,
						34.0F * settingGlobalScale);
					if (ImFluent::Button("\uf900  Website", linkButtonSize))
					{
						if (I18n::isIdentifying(L"zh-CN")) ShellExecuteW(0, 0, L"https://www.inkeys.top", 0, 0, SW_SHOW);
						else ShellExecuteW(0, 0, L"https://en.inkeys.top", 0, 0, SW_SHOW);
					}
					ImGui::SameLine();
					if (ImFluent::Button("\uf901  GitHub", linkButtonSize))
						ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/Inkeys", 0, 0, SW_SHOW);
					ImGui::SameLine();
					if (ImFluent::Button("\uf904  Community", linkButtonSize))
					{
						if (I18n::isIdentifying(L"zh-CN")) ShellExecuteW(0, 0, L"https://www.inkeys.top/community.html", 0, 0, SW_SHOW);
						else ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/Inkeys/discussions", 0, 0, SW_SHOW);
					}
					ImGui::Dummy({ 0.0F, 8.0F * settingGlobalScale });
					if (ImFluent::Button("\uf905  Bilibili", linkButtonSize))
						ShellExecuteW(0, 0, L"https://space.bilibili.com/1330313497", 0, 0, SW_SHOW);
					ImGui::SameLine();
					if (ImFluent::Button("\uf906  Feedback", linkButtonSize))
						ShellExecuteW(0, 0, L"https://www.wjx.cn/vm/mqNTTRL.aspx#", 0, 0, SW_SHOW);

					ImGui::Dummy({ 0.0F, 16.0F * settingGlobalScale });
					if (ImFluent::BeginSettingsCard("##home-author", "AlanCRL",
						IA(I18nKey.SettingsUI.Home.Developer).c_str(), "\uf902"))
					{
						ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[3],
							{ 45.0F * settingGlobalScale, 45.0F * settingGlobalScale });
						ImGui::SameLine();
						ImFluent::TextBlock("alan-crl@foxmail.com", ImFluentTextStyle_Caption);
						ImFluent::EndSettingsCard();
					}

					ImGui::Dummy({ 0.0F, 16.0F * settingGlobalScale });
					ImFluent::TextBlock("Tutorial", ImFluentTextStyle_Subtitle);
					const float tutorialWidth = min(settingContentPanelWidth - 40.0F, 700.0F)
						* settingGlobalScale;
					const float tutorialHeight = tutorialWidth * 215.0F / 700.0F;
					ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[1],
						{ tutorialWidth, tutorialHeight });
					ImGui::Dummy({ 0.0F, 10.0F * settingGlobalScale });
					const float guideHeight = tutorialWidth * 390.0F / 770.0F;
					ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[2],
						{ tutorialWidth, guideHeight });

					ImGui::EndChild();
					ImGui::PopStyleVar();

					break;
				}

				// 语言
				case settingTabEnum::Language:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("语言", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Language.N).c_str());
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("语言#1", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Language.UI.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("语言", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Language.UI.Select).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Language.UI.SelectE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								//vector<char*> vec;
								//vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Language.UI.Language.en_US)).c_str()));
								//vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Language.UI.Language.zh_CN)).c_str()));
								//vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Language.UI.Language.zh_TW)).c_str()));
								// TODO

								vector<string> vec;
								vec.emplace_back(IA(I18nKey.SettingsUI.Language.UI.Language.en_US));
								vec.emplace_back(IA(I18nKey.SettingsUI.Language.UI.Language.zh_CN));
								vec.emplace_back(IA(I18nKey.SettingsUI.Language.UI.Language.zh_TW));

								if (Widgets::combo.Begin("##语言", vec[SelectLanguage].c_str(), static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (SelectLanguage == i);
										if (Widgets::combo.Selectable(vec[i].c_str(), is_selected))
										{
											SelectLanguage = i;
											if (setlist.selectLanguage != SelectLanguage)
											{
												setlist.selectLanguage = SelectLanguage;
												WriteSetting();

												if (setlist.selectLanguage == 1) I18n::load(1, L"JSON", L"zh-CN");
												else if (setlist.selectLanguage == 2) I18n::load(1, L"JSON", L"zh-TW");
												else I18n::load(1, L"JSON", L"en-US");

												QueueConfirmRestart(
													IW(I18nKey.SettingsUI.Language.UI.Warn));
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								//for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 配置保存
				case settingTabEnum::tabConfiguration:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("配置保存", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.N).c_str());
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("配置保存#1", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.Clean.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("启用配置清理", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.Clean.Enable).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##启用配置清理", &ConfigurationSetting.Enable);

								if (Inkeys::config.Config.AutoClean != ConfigurationSetting.Enable)
								{
									Inkeys::config.Config.AutoClean = ConfigurationSetting.Enable;
									QueueConfigWrite();
									WriteSetting();
								}
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("配置清理-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

									ImGui::TextWrapped(IA(I18nKey.SettingsUI.Configuration.Clean.EnableE).c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("配置保存#2", { settingItemWidth * settingGlobalScale,175.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.CanvasSave.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("启用历史画布保存", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.CanvasSave.Enable).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.CanvasSave.EnableE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##启用历史画布保存", &SaveSetting.Enable);

								if (setlist.saveSetting.enable != SaveSetting.Enable)
								{
									setlist.saveSetting.enable = SaveSetting.Enable;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("保存时长", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.E).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_1d)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_3d)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_5d)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_10d)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.K_30d)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Configuration.CanvasSave.SaveTime.Never)).c_str()));

								if (Widgets::combo.Begin("##保存时长", vec[SaveSetting.SaveDays], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (SaveSetting.SaveDays == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											SaveSetting.SaveDays = i;
											if (setlist.saveSetting.saveDays != SaveSetting.SaveDays)
											{
												setlist.saveSetting.saveDays = SaveSetting.SaveDays;
												WriteSetting();
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 软件版本
				case settingTabEnum::tab6:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("软件版本", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.N).c_str());
					}

					if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNew)
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
						ImGui::BeginChild("软件版本#0", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.ManualUpdate.N).c_str());
						}
						{
							ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							if (Widgets::button.Standard(IA(I18nKey.SettingsUI.Version.ManualUpdate.ManualUpdate).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
							{
								mandatoryUpdate = true;
								AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					if (inconsistentArchitecture)
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
						ImGui::BeginChild("软件版本#01", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							ImGui::BeginChild("软件版本-提示2", { settingPromptWidth * settingGlobalScale,60.0f * settingGlobalScale }, false);

							{
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextWrapped(IA(I18nKey.SettingsUI.Version.VersionTip).c_str());
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("软件版本#1", { settingItemWidth * settingGlobalScale,410.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Info.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("版本信息", { settingItemWidth * settingGlobalScale,380.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 35.0f * settingGlobalScale,20.0f * settingGlobalScale });
								ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[1], ImVec2((float)settingSign[1].width, (float)settingSign[1].height));
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY());
								wstring text;
								{
									text += L"\n" + IW(I18nKey.SettingsUI.Version.Info.ReleaseVersion) + L" " + editionVersion + L"(" + editionDate + L")";
									text += L"\n" + IW(I18nKey.SettingsUI.Version.Info.ReleaseDate) + L" " + buildTime;
									text += L"\n" + IW(I18nKey.SettingsUI.Version.Info.ReleaseArch) + L" " + programArchitecture + L" | " + targetArchitecture;
								#ifdef IDT_RELEASE
									text += L"\n" + IW(I18nKey.SettingsUI.Version.Info.ReleaseTag);
								#else
									text += L"\n" + IW(I18nKey.SettingsUI.Version.Info.DebugTag);
								#endif
								}

								int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

								std::vector<std::string> lines;
								std::wstring line, temp;
								std::wstringstream ss(text);

								while (getline(ss, temp, L'\n'))
								{
									bool flag = false;
									line = L"";

									for (wchar_t ch : temp)
									{
										flag = false;

										float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
										if (text_width > (right_x - left_x))
										{
											lines.emplace_back(utf16ToUtf8(line));
											line = L"", flag = true;
										}

										line += ch;
									}

									if (!flag) lines.emplace_back(utf16ToUtf8(line));
								}

								for (const auto& temp : lines)
								{
									float text_width = ImGui::CalcTextSize(temp.c_str()).x;
									float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
									if (text_indentation < 0)  text_indentation = 0;
									ImGui::SetCursorPosX(left_x + text_indentation);
									ImGui::TextUnformatted(temp.c_str());
								}
							}
							{
								if (settingCICD.url.empty())
								{
									int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;
									string temp = IA(I18nKey.SettingsUI.Version.Info.ManualBuild);

									float text_width = ImGui::CalcTextSize(temp.c_str()).x;
									float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
									if (text_indentation < 0)  text_indentation = 0;
									ImGui::SetCursorPosX(left_x + text_indentation);
									ImGui::TextUnformatted(temp.c_str());
								}
								else
								{
									int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;
									string temp = IA(I18nKey.SettingsUI.Version.Info.AutoBuild);
									string url = IA(I18nKey.SettingsUI.Version.Info.CICDInfo);

									float text_width = ImGui::CalcTextSize((temp + url).c_str()).x;
									float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
									if (text_indentation < 0)  text_indentation = 0;
									ImGui::SetCursorPosX(left_x + text_indentation);
									ImGui::TextUnformatted(temp.c_str());

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
									ImGui::SameLine();
									if (ImGui::TextLink(url.c_str()))
									{
										settingTab = settingTabEnum::tabCICD;
									}
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("软件版本#2", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.UserInfo.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("复制用户ID", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.UserInfo.CopyUserId).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

								ImGui::TextUnformatted((IA(I18nKey.SettingsUI.Version.UserInfo.UserId) + " " + utf16ToUtf8(userId)).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard(IA(I18nKey.Operate.Copy).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									OpenClipboard(NULL); // 打开剪切板
									EmptyClipboard(); // 清空剪切板
									size_t size = (userId.length() + 1) * sizeof(wchar_t); // 计算需要的内存大小（包括结尾的null字符）
									HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size); // 分配全局内存
									wchar_t* pDest = (wchar_t*)GlobalLock(hGlobal); // 锁定内存并获取指针
									wcscpy_s(pDest, userId.length() + 1, userId.c_str()); // 复制文本到全局内存
									GlobalUnlock(hGlobal); // 解锁内存
									SetClipboardData(CF_UNICODETEXT, hGlobal); // 设置剪切板数据
									CloseClipboard(); // 关闭剪切板
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("软件版本#3", { settingItemWidth * settingGlobalScale,165.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Repair.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("修复软件", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Repair.RepairSoftware).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Repair.RepairSoftwareE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard(IA(I18nKey.Operate.Repair).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									if (EnableFixWithChangeArchitecture)
									{
										if (targetArchitecture == L"win64") SetUpdateArchitecture("win64");
										else if (targetArchitecture == L"arm64") SetUpdateArchitecture("arm64");
										else SetUpdateArchitecture("win32");
										WriteSetting();
									}

									mandatoryUpdate = true;
									if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNotStarted) QueueAutomaticUpdate();
									else AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("修复时修正软件架构", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Repair.RepairArch).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##修复时修正软件架构", &EnableFixWithChangeArchitecture);
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("软件版本#4", { settingItemWidth * settingGlobalScale,330.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Update.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("自动更新（静默）", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Update.AutoUpate).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								const bool enableAutoUpdateSnapshot = EnableAutoUpdate;
								const bool enableAutoUpdateChanged = Widgets::toggle.ToggleBool("##自动更新（静默）", &EnableAutoUpdate);

								if (enableAutoUpdateChanged && enableAutoUpdateSnapshot != EnableAutoUpdate)
								{
									if (!EnableAutoUpdate && AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateRestart)
									{
										SettingBusinessCommand command;
										command.kind = SettingBusinessKind::ClearInstallerAndSetAutoUpdate;
										command.flag = EnableAutoUpdate;
										QueueBusiness(std::move(command));
										AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
									}
									else
									{
										SetEnableAutoUpdate(EnableAutoUpdate);
										WriteSetting();
										if (EnableAutoUpdate && AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNew)
										{
											AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
										}
									}
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("更新通道-提示", { settingItemWidth * settingGlobalScale,95.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::AccentText);
								ImGui::TextUnformatted("\uf167");
							}
							{
								ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("组件-提示", { settingPromptWidth * settingGlobalScale,55.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextWrapped(IA(I18nKey.SettingsUI.Version.Update.ChannelTip).c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("目标更新通道", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Update.Channel.N).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								int UpdateChannelMode;

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Version.Update.Channel.LTS)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Version.Update.Channel.Insider)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Version.Update.Channel.Canary)).c_str()));

								string updateChannel = GetUpdateChannel();
								if (updateChannel == "Insider") UpdateChannelMode = 1;
								else if (updateChannel == "Canary") UpdateChannelMode = 2;
								else UpdateChannelMode = 0;

								if (Widgets::combo.Begin("##更新通道", vec[UpdateChannelMode], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (UpdateChannelMode == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											UpdateChannelMode = i;
											if ((UpdateChannelMode == 0 && updateChannel != "LTS") ||
												(UpdateChannelMode == 1 && updateChannel != "Insider") ||
												(UpdateChannelMode == 2 && updateChannel != "Canary"))
											{
												string selectedUpdateChannel;
												if (UpdateChannelMode == 1) selectedUpdateChannel = "Insider";
												else if (UpdateChannelMode == 2) selectedUpdateChannel = "Canary";
												else selectedUpdateChannel = "LTS";

												if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateRestart)
												{
													SettingBusinessCommand command;
													command.kind = SettingBusinessKind::ClearInstallerAndSetChannel;
													command.value = selectedUpdateChannel;
													QueueBusiness(std::move(command));
													AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
												}
												else
												{
													SetUpdateChannel(selectedUpdateChannel);
													WriteSetting();
													if (AutomaticUpdateState != AutomaticUpdateStateEnum::UpdateNotStarted)
													{
														AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
													}
												}
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("目标更新架构", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Update.Arch.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Version.Update.Arch.E).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Version.Update.Arch.K_64)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Version.Update.Arch.K_32)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Version.Update.Arch.Arm64)).c_str()));

								int UpdateArchitecture, UpdateArchitectureEcho;
								string updateArchitecture = GetUpdateArchitecture();
								if (updateArchitecture == "win64") UpdateArchitecture = UpdateArchitectureEcho = 0;
								else if (updateArchitecture == "arm64") UpdateArchitecture = UpdateArchitectureEcho = 2;
								else UpdateArchitecture = UpdateArchitectureEcho = 1;

								if (Widgets::combo.Begin("##目标更新架构", vec[UpdateArchitecture], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (UpdateArchitecture == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											UpdateArchitecture = i;
											if (UpdateArchitectureEcho != UpdateArchitecture)
											{
												string selectedUpdateArchitecture;
												if (UpdateArchitecture == 0) selectedUpdateArchitecture = "win64";
												else if (UpdateArchitecture == 2) selectedUpdateArchitecture = "arm64";
												else selectedUpdateArchitecture = "win32";

												if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateRestart)
												{
													SettingBusinessCommand command;
													command.kind = SettingBusinessKind::ClearInstallerAndSetArchitecture;
													command.value = selectedUpdateArchitecture;
													QueueBusiness(std::move(command));
													AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
												}
												else
												{
													SetUpdateArchitecture(selectedUpdateArchitecture);
													WriteSetting();
													if (AutomaticUpdateState != AutomaticUpdateStateEnum::UpdateNotStarted)
													{
														AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
													}
												}
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// CICD
				case settingTabEnum::tabCICD:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("自动构建详情信息", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					{
						ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
						{
							ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
							if (Widgets::button.Standard("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingTab = settingTabEnum::tab6;
						}

						ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
						{
							{
								ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.CICD.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								{
									ImGui::TextUnformatted("CI/CD");
								}
							}
						}
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
						ImGui::BeginChild("自动构建详情信息-输出", { settingContentPanelWidth * settingGlobalScale,533.0f * settingGlobalScale }, true);

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

						{
							ImGui::SetCursorPosY(30.0f);
							string temp = "CI/CD: ";
							string url = utf16ToUtf8(settingCICD.url);

							int left_x = 20 * settingGlobalScale, right_x = 750 * settingGlobalScale;

							ImGui::SetCursorPosX(left_x);
							ImGui::TextUnformatted(temp.c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							if (ImGui::TextLink(url.c_str()))
							{
								ShellExecuteW(0, 0, settingCICD.url.c_str(), 0, 0, SW_SHOW);
							}
						}
						{
							ImGui::TextUnformatted("");

							string temp = IA(I18nKey.SettingsUI.CICD.Repository) + ": ";
							string url = utf16ToUtf8(settingCICD.repoUrl);

							int left_x = 20 * settingGlobalScale, right_x = 750 * settingGlobalScale;

							ImGui::SetCursorPosX(left_x);
							ImGui::TextUnformatted(temp.c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							if (ImGui::TextLink(url.c_str()))
							{
								ShellExecuteW(0, 0, settingCICD.repoUrl.c_str(), 0, 0, SW_SHOW);
							}
						}
						{
							wstring text;
							{
								text += IW(I18nKey.SettingsUI.CICD.Branch) + L": " + settingCICD.branch + L"\n";
								text += IW(I18nKey.SettingsUI.CICD.Submitter) + L": " + settingCICD.submitter + L"\n";
								text += IW(I18nKey.SettingsUI.CICD.BuildTime) + L": " + settingCICD.buildTime + L"\n";
								text += L"\n";
								text += IW(I18nKey.SettingsUI.CICD.BuildSystem) + L": " + settingCICD.buildOS + L"\n";
								text += IW(I18nKey.SettingsUI.CICD.BuildSystemVersion) + L": " + settingCICD.buildOSVersion + L"\n";
								text += IW(I18nKey.SettingsUI.CICD.RunnerImageSystem) + L": " + settingCICD.buildRunnerImageOS + L"\n";
								text += IW(I18nKey.SettingsUI.CICD.RunnerImageVersion) + L": " + settingCICD.buildRunnerImageVersion + L"\n";
								text += L"\n";
								text += IW(I18nKey.SettingsUI.CICD.MSBuildVersion) + L"\n" + settingCICD.msBuildVersion + L"\n";
							}

							int left_x = 20 * settingGlobalScale, right_x = 750 * settingGlobalScale;

							std::vector<std::string> lines;
							std::wstring line, temp;
							std::wstringstream ss(text);

							while (getline(ss, temp, L'\n'))
							{
								bool flag = false;
								line = L"";

								for (wchar_t ch : temp)
								{
									flag = false;

									float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
									if (text_width > (right_x - left_x))
									{
										lines.emplace_back(utf16ToUtf8(line));
										line = L"", flag = true;
									}

									line += ch;
								}

								if (!flag) lines.emplace_back(utf16ToUtf8(line));
							}
							for (const auto& temp : lines)
							{
								//float text_width = ImGui::CalcTextSize(temp.c_str()).x;
								//float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
								//if (text_indentation < 0)  text_indentation = 0;
								//ImGui::SetCursorPosX(left_x + text_indentation);
								ImGui::SetCursorPosX(left_x);
								ImGui::TextUnformatted(temp.c_str());
							}

							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// ---------------------

				// 常规
				case settingTabEnum::tab2:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("常规", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.N).c_str());
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("常规#2", { settingItemWidth * settingGlobalScale,175.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.StartUp.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("开机自动启动", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.StartUp.AutoStart).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.StartUp.AutoStartE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##开机自动启动", &StartUp);

								if (setlist.startUp != StartUp)
								{
									SetStartupState(StartUp, GetCurrentExePath(), L"$Inkeys");

									setlist.startUp = StartUp;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("创建桌面快捷方式", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.StartUp.Link.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.StartUp.Link.E).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightButtonPairLeftX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard(IA(I18nKey.Operate.Create).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									wchar_t desktopPath[MAX_PATH];

									if (SHGetSpecialFolderPathW(0, desktopPath, CSIDL_DESKTOP, FALSE))
									{
										SettingBusinessCommand command;
										command.kind = SettingBusinessKind::CreateShortcut;
										command.text = wstring(desktopPath) + L"\\"
											+ IW(I18nKey.Widget.LnkName) + L".lnk";
										command.directory = GetCurrentExePath();
										QueueBusiness(std::move(command));
									}
								}
							}
							{
								ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard(IA(I18nKey.SettingsUI.Regular.StartUp.Link.More).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									settingPlugInTab = settingPlugInTabEnum::tabPlug3;
									settingTab = settingTabEnum::tab4;
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("常规#3", { settingItemWidth * settingGlobalScale,245.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Appearance.N).c_str());
						}

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("主栏 UI 缩放", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.E).c_str());
							}
							{
								ImGui::SetCursorPos({ 435.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::PushItemWidth(300.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								Widgets::slider.Float("##主栏 UI 缩放", &BarZoom, 0.50f, 2.00f, "");
								BarZoom = round(BarZoom * 100) / 100;

								ImGui::PopItemWidth();

								bool isItemActive = ImGui::IsItemActive();

								if (fabs(BarZoom - static_cast<float>(Inkeys::config.UI.Bar.Zoom.load())) > 0.0001f)
								{
									Inkeys::config.UI.Bar.Zoom = static_cast<double>(BarZoom);
									Inkeys::UI::Bar::SetConfigZoom(static_cast<double>(BarZoom));
									BarZoomSavePending = true;
								}

								if (ImGui::IsItemHovered())
								{
									PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

									ImGui::BeginTooltip();

									ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.Ind), make_format_args(BarZoom)).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && BarZoomSavePending)
								{
									QueueConfigWrite();
									BarZoomSavePending = false;
								}
							}
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

								string temp = vformat(IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.Ind), make_format_args(BarZoom));
								ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

								ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 15.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
								ImGui::TextUnformatted(temp.c_str());
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();

							// 光影属于 UI3 外观能力，放在常规页便于正式版本使用。
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("启用边缘光影", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true,
								ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
							{
								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted("启用边缘光影");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted("关闭后仅保留基础边框，停用点光与柔光效果。");
								}
								{
									ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									Widgets::toggle.ToggleBool("##启用边缘光影", &Experimental.Inkeys3.EdgeLightingEnable);
									if (Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable
										!= Experimental.Inkeys3.EdgeLightingEnable)
									{
										Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Enable =
											Experimental.Inkeys3.EdgeLightingEnable;
									Inkeys::UI::Bar::SetEdgeLightingOptions(
										Experimental.Inkeys3.EdgeLightingEnable,
										Experimental.Inkeys3.DynamicEdgeLighting);
									QueueConfigWrite();
								}
								}
								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("选项界面 UI 缩放", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.E).c_str());
							}
							{
								ImGui::SetCursorPos({ 435.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::PushItemWidth(300.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								Widgets::slider.Float("##选项界面 UI 缩放", &SettingGlobalScale, 1.0f, 2.0f, "");
								SettingGlobalScale = round(SettingGlobalScale * 100) / 100;

								ImGui::PopItemWidth();

								bool isItemHovered = ImGui::IsItemHovered();
								bool isItemActive = ImGui::IsItemActive();

								if (ImGui::IsItemHovered())
								{
									PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

									ImGui::BeginTooltip();

									ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.Ind), make_format_args(SettingGlobalScale)).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && SettingGlobalScale != setlist.settingGlobalScale)
								{
									setlist.settingGlobalScale =
										Inkeys::UI::Setting::NormalizeUserScale(SettingGlobalScale);
									UpdateSettingScale(QuerySettingDpi(setting_window));
									{
										lock_guard stateLock(settingStateMutex);
										settingSessionState.QueueFontRebuild();
									}
									Inkeys::UI::RenderPipeline::Request(
										Inkeys::UI::RenderPipeline::Client::Settings);
									WriteSetting();
								}
							}
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

								string temp = vformat(IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.Ind), make_format_args(SettingGlobalScale));
								ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

								ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 15.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
								ImGui::TextUnformatted(temp.c_str());
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("常规#4", { settingItemWidth * settingGlobalScale,225.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Behavior.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("置顶间隔", { settingItemWidth * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.N).c_str());
							}
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("置顶间隔-介绍", { settingDescriptionBeforeComboWidth * settingGlobalScale,50.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

									ImGui::TextWrapped(IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.E).c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							cursosPosY = 0;
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 50.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_100ms)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_500ms)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_1s)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_3s)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_5s)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_10s)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_30s)).c_str()));

								if (Widgets::combo.Begin("##置顶间隔", vec[TopSleepTime], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (TopSleepTime == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											TopSleepTime = i;
											if (setlist.topSleepTime != TopSleepTime)
											{
												setlist.topSleepTime = TopSleepTime;
												WriteSetting();

												topWindowNow = true;
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("右键点击主图标关闭程序", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Behavior.RightClickClose).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Behavior.RightClickCloseE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##右键点击主图标关闭程序", &RightClickClose);

								if (setlist.RightClickClose != RightClickClose)
								{
									setlist.RightClickClose = RightClickClose;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("常规#5", { settingItemWidth * settingGlobalScale,205.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Tentative.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("避免全屏显示", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Tentative.AvoidFulScreen).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##避免全屏显示", &RegularSetting.AvoidFullScreen);

								if (setlist.regularSetting.avoidFullScreen != RegularSetting.AvoidFullScreen)
								{
									setlist.regularSetting.avoidFullScreen = RegularSetting.AvoidFullScreen;
									WriteSetting();
								}
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("避免全屏显示-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

									ImGui::TextWrapped(IA(I18nKey.SettingsUI.Regular.Tentative.AvoidFulScreenE).c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("教学安全模式", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.E).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode1)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode2)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode3)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode4)).c_str()));

								if (Widgets::combo.Begin("##教学安全选项", vec[RegularSetting.TeachingSafetyMode], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (RegularSetting.TeachingSafetyMode == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											RegularSetting.TeachingSafetyMode = i;
											if (setlist.regularSetting.teachingSafetyMode != RegularSetting.TeachingSafetyMode)
											{
												setlist.regularSetting.teachingSafetyMode = RegularSetting.TeachingSafetyMode;
												WriteSetting();

												CrashHandler::SetFlag(setlist.regularSetting.teachingSafetyMode);
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 绘制
				case settingTabEnum::tab3:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("绘制", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.N).c_str());
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("绘制#1", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.Effect.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("绘图设备", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.Effect.Device.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.Effect.Device.E).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Draw.Effect.Device.Touch)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Draw.Effect.Device.MousePen)).c_str()));

								if (Widgets::combo.Begin("##绘图设备", vec[PaintDevice], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (PaintDevice == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											PaintDevice = i;
											if (setlist.paintDevice != PaintDevice)
											{
												setlist.paintDevice = PaintDevice;
												WriteSetting();

												drawingScale = GetDrawingScale();
												stopTimingError = GetStopTimingError();
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("绘制#2", { settingItemWidth * settingGlobalScale,245.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("绘制#21", { settingItemWidth * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.PenUp).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.PenUpE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##抬笔拉直直线", &LiftStraighten);

								if (setlist.liftStraighten != LiftStraighten)
								{
									setlist.liftStraighten = LiftStraighten;
									WriteSetting();
								}
							}

							// Separator
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
								ImGui::Separator();
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.PenStay).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.PenStayE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##停留拉直直线", &WaitStraighten);

								if (setlist.waitStraighten != WaitStraighten)
								{
									setlist.waitStraighten = WaitStraighten;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("绘制#22", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.EndpointAdsorption).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.AIDraw.EndpointAdsorptionE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##端点吸附", &PointAdsorption);

								if (setlist.pointAdsorption != PointAdsorption)
								{
									setlist.pointAdsorption = PointAdsorption;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("绘制#3", { settingItemWidth * settingGlobalScale,90.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.DrawBehavior.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("绘制#3", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.DrawBehavior.SoomthWriting).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##抬笔平滑笔迹", &SmoothWriting);

								if (setlist.smoothWriting != SmoothWriting)
								{
									setlist.smoothWriting = SmoothWriting;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("绘制#4", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.RubberThickness.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("橡皮粗细计算方式", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.E).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(200 * settingGlobalScale);

								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								vector<char*> vec;
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.Mode3)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.Mode2)).c_str()));
								vec.emplace_back(_strdup((IA(I18nKey.SettingsUI.Draw.RubberThickness.Calc.Mode1)).c_str()));

								if (Widgets::combo.Begin("##橡皮粗细计算方式", vec[EraserMode], static_cast<int>(vec.size())))
								{
									for (int i = 0; i < vec.size(); i++)
									{
										ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

										bool is_selected = (EraserMode == i);
										if (Widgets::combo.Selectable(vec[i], is_selected))
										{
											EraserMode = i;
											if (setlist.eraserSetting.eraserMode != EraserMode)
											{
												setlist.eraserSetting.eraserMode = EraserMode;
												WriteSetting();
											}
										}
									}
									ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
									Widgets::combo.End();
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("绘制性能", { settingItemWidth * settingGlobalScale,220.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Performance.DrawMode.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("落笔预备", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Performance.DrawMode.Prepare.N).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Performance.DrawMode.Prepare.E).c_str());
							}
							{
								ImGui::SetCursorPos({ 435.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::PushItemWidth(300.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								Widgets::slider.Int("##落笔预备数量", &PreparationQuantity, 0, 20, "");

								ImGui::PopItemWidth();

								bool isItemHovered = ImGui::IsItemHovered();
								bool isItemActive = ImGui::IsItemActive();

								if (ImGui::IsItemHovered())
								{
									PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

									ImGui::BeginTooltip();

									ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Performance.DrawMode.Prepare.Ind), make_format_args(PreparationQuantity)).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && PreparationQuantity != setlist.performanceSetting.preparationQuantity)
								{
									setlist.performanceSetting.preparationQuantity = PreparationQuantity;
									WriteSetting();

									// 落笔预备
									ResetPrepareCanvas();
								}
							}
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

								string temp = vformat(IA(I18nKey.SettingsUI.Performance.DrawMode.Prepare.Ind), make_format_args(PreparationQuantity));
								ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

								ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 15.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
								ImGui::TextUnformatted(temp.c_str());
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("极限性能绘图", { settingItemWidth * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Performance.DrawMode.SuperDraw).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##极限性能绘图", &SuperDraw);

								if (setlist.performanceSetting.superDraw != SuperDraw)
								{
									setlist.performanceSetting.superDraw = SuperDraw;
									WriteSetting();
								}
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("极限性能绘图-介绍", { settingDescriptionWidth * settingGlobalScale,50.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

									ImGui::TextWrapped(IA(I18nKey.SettingsUI.Performance.DrawMode.SuperDrawE).c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("绘制#5", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.Tentative.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("绘制时隐藏触控光标", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.Tentative.HideCursor).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##绘制时隐藏触控光标", &HideTouchPointer);

								if (setlist.hideTouchPointer != HideTouchPointer)
								{
									setlist.hideTouchPointer = HideTouchPointer;
									WriteSetting();
								}
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("绘制时隐藏触控光标-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

									ImGui::TextWrapped(IA(I18nKey.SettingsUI.Draw.Tentative.HideCursorE).c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 预设
				case settingTabEnum::tabPreset:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("预设", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.N).c_str());
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("预设#1", { settingItemWidth * settingGlobalScale,170.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Memory.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("预设#11", { settingItemWidth * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Memory.Thickness).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Memory.ThicknessE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##记忆绘制粗细", &PresetSetting.MemoryWidth);

								if (setlist.presetSetting.memoryWidth != PresetSetting.MemoryWidth)
								{
									setlist.presetSetting.memoryWidth = PresetSetting.MemoryWidth;
									WriteSetting();
								}
							}

							// Separator
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
								ImGui::Separator();
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Memory.Color).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Memory.ColorE).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##记忆绘制颜色", &PresetSetting.MemoryColor);

								if (setlist.presetSetting.memoryColor != PresetSetting.MemoryColor)
								{
									setlist.presetSetting.memoryColor = PresetSetting.MemoryColor;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("预设#2", { settingItemWidth * settingGlobalScale,(PresetSetting.AutoDefaultWidth ? 95.0f : 220.0f) * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Preset.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("预设#21", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Preset.AutoThickness).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

								int x = static_cast<int>(stateMode.Pen.Brush1.widthPreset);
								int y = static_cast<int>(stateMode.Pen.Highlighter1.widthPreset);

								ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Preset.Preset.AutoThicknessE), make_format_args(x, y)).c_str());
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##自适应绘制粗细", &PresetSetting.AutoDefaultWidth);

								if (setlist.presetSetting.autoDefaultWidth != PresetSetting.AutoDefaultWidth)
								{
									setlist.presetSetting.autoDefaultWidth = PresetSetting.AutoDefaultWidth;
									WriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						if (!PresetSetting.AutoDefaultWidth)
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("预设#22", { settingItemWidth * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Preset.Pen).c_str());
							}
							{
								ImGui::SetCursorPos({ 225.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
								ImGui::PushItemWidth(500.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								Widgets::slider.Float("##预设画笔粗细", &PresetSetting.DefaultBrush1Width, 1.0f, 30.0f, "");
								PresetSetting.DefaultBrush1Width = round(PresetSetting.DefaultBrush1Width);

								ImGui::PopItemWidth();

								bool isItemHovered = ImGui::IsItemHovered();
								bool isItemActive = ImGui::IsItemActive();

								if (isItemHovered)
								{
									PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

									ImGui::BeginTooltip();

									int x = static_cast<int>(PresetSetting.DefaultBrush1Width);
									ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Preset.Preset.PenInd), make_format_args(x)).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && setlist.presetSetting.defaultBrush1Width != PresetSetting.DefaultBrush1Width)
								{
									setlist.presetSetting.defaultBrush1Width = PresetSetting.DefaultBrush1Width;
									WriteSetting();
								}
							}
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

								int x = static_cast<int>(PresetSetting.DefaultBrush1Width);
								string temp = vformat(IA(I18nKey.SettingsUI.Preset.Preset.PenInd), make_format_args(x));
								ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

								ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 10.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
								ImGui::TextUnformatted(temp.c_str());
							}

							// Separator
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPosY(cursosPosY + 15.0f * settingGlobalScale);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
								ImGui::Separator();
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Preset.Preset.Highlighter).c_str());
							}
							{
								ImGui::SetCursorPos({ 225.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
								ImGui::PushItemWidth(500.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								Widgets::slider.Float("##预设荧光笔粗细", &PresetSetting.DefaultHighlighter1Width, 10.0f, 100.0f, "");
								PresetSetting.DefaultHighlighter1Width = round(PresetSetting.DefaultHighlighter1Width);

								ImGui::PopItemWidth();

								bool isItemHovered = ImGui::IsItemHovered();
								bool isItemActive = ImGui::IsItemActive();

								if (isItemHovered)
								{
									PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

									ImGui::BeginTooltip();

									int x = static_cast<int>(PresetSetting.DefaultHighlighter1Width);
									ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Preset.Preset.HighlighterInd), make_format_args(x)).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && setlist.presetSetting.defaultHighlighter1Width != PresetSetting.DefaultHighlighter1Width)
								{
									setlist.presetSetting.defaultHighlighter1Width = PresetSetting.DefaultHighlighter1Width;
									WriteSetting();
								}
							}
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

								int x = static_cast<int>(PresetSetting.DefaultHighlighter1Width);
								string temp = vformat(IA(I18nKey.SettingsUI.Preset.Preset.HighlighterInd), make_format_args(x));
								ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

								ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 10.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
								ImGui::TextUnformatted(temp.c_str());
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 插件
				case settingTabEnum::tab4:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });
					switch (settingPlugInTab)
					{
					case settingPlugInTabEnum::tabPlug1:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
						ImGui::BeginChild("插件", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

						ImGui::SetCursorPosY(10.0f * settingGlobalScale);
						{
							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.N).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("PPT演示助手", { settingItemWidth * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[5], ImVec2((float)settingSign[5].width, (float)settingSign[5].height));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									{
										if (pptComVersion.substr(0, 7) == L"Error: ") ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.VersionError).c_str());
										else ImGui::TextUnformatted(utf16ToUtf8(pptComVersion).c_str());
									}
								}
								{
									ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									if (Widgets::button.Standard("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug2;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("PPT演示助手-介绍", { settingDescriptionWidth * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.E).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("超级置顶", { settingItemWidth * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[10], ImVec2((float)settingSign[10].width, (float)settingSign[10].height));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.SuperTop.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted("20260202a");
								}
								{
									ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									if (Widgets::button.Standard("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabSuperTop;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("超级置顶-介绍", { settingDescriptionWidth * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.SuperTop.E).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("同类软件悬浮窗拦截助手", { settingItemWidth * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[8], ImVec2((float)settingSign[8].width, (float)settingSign[8].height));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.DesktopDrawpadBlocker.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted(utf16ToUtf8(ddbInteractionSetList.DdbEdition).c_str());
								}
								{
									ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									if (Widgets::button.Standard("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlugDDB;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("同类软件悬浮窗拦截助手-介绍", { settingDescriptionWidth * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.DesktopDrawpadBlocker.E).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("快捷方式保障助手", { settingItemWidth * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[6], ImVec2((float)settingSign[6].width, (float)settingSign[6].height));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted("20250223a");
								}
								{
									ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									if (Widgets::button.Standard("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug3;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("快捷方式保障助手-介绍", { settingDescriptionWidth * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.E).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
							ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
						}
						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					case settingPlugInTabEnum::tabPlug2:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
						ImGui::BeginChild("PPT演示助手", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

						{
							ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							}

							ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
							{
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									{
										if (pptComVersion.substr(0, 7) == L"Error: ") ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.VersionError).c_str());
										else ImGui::TextUnformatted(utf16ToUtf8(pptComVersion).c_str());
									}
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
							ImGui::BeginChild("PPT演示助手主栏", { settingContentPanelWidth * settingGlobalScale,555.0f * settingGlobalScale }, false);

							if (pptComVersion.substr(0, 7) == L"Error: ")
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
								ImGui::BeginChild("PPT演示助手#12", { settingItemWidth * settingGlobalScale,80.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
									ImGui::TextUnformatted("\ue814");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("PPT演示助手-提示0", { settingPromptWidth * settingGlobalScale,40.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextWrapped((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Error) + utf16ToUtf8(pptComVersion)).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
								ImGui::BeginChild("PPT演示助手#1", { settingItemWidth * settingGlobalScale,80.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::AccentText);
									ImGui::TextUnformatted("\uf167");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("PPT演示助手-提示", { settingPromptBeforeButtonWidth * settingGlobalScale,40.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tip).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									if (Widgets::button.Standard(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Solve).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
									{
										ShellExecuteW(0, 0, L"https://www.inkeys.top/tutorial/ppt-com", 0, 0, SW_SHOW);
									}
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							if (pptComSetlist.setAdmin)
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
								ImGui::BeginChild("PPT演示助手#11", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
									ImGui::TextUnformatted("\ue814");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("PPT演示助手-提示2", { settingPromptBeforeButtonWidth * settingGlobalScale,60.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Warn).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									if (Widgets::button.Standard(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Solve).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
									{
										ShellExecuteW(0, 0, L"https://www.inkeys.top/tutorial/ppt-admin", 0, 0, SW_SHOW);
									}
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("PPT演示助手#2", { settingItemWidth * settingGlobalScale,205.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("墨迹固定在对应页面上", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.InkFixation).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##墨迹固定在对应页面上", &PptComFixedHandWriting);

										if (pptComSetlist.fixedHandWriting != PptComFixedHandWriting)
										{
											pptComSetlist.fixedHandWriting = PptComFixedHandWriting;
											PptComWriteSetting();
										}
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
										ImGui::BeginChild("墨迹固定在对应页面上-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

										{
											ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

											ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.InkFixationE).c_str());
										}

										{
											if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
											if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
											while (PushFontNum) PushFontNum--, ImGui::PopFont();
										}
										ImGui::EndChild();
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("显示加载页面", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.LoadPage).c_str());
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.BasicLogic.LoadPageE).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##显示加载页面", &PptComShowLoadingScreen);

										if (pptComSetlist.showLoadingScreen != PptComShowLoadingScreen)
										{
											pptComSetlist.showLoadingScreen = PptComShowLoadingScreen;
											PptComWriteSetting();
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("PPT演示助手#3", { settingItemWidth * settingGlobalScale,210.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("控件显示", { settingItemWidth * settingGlobalScale,180.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomBoth).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##显示底部两侧控件", &ShowBottomBoth);

										if (pptComSetlist.showBottomBoth != ShowBottomBoth)
										{
											pptComSetlist.showBottomBoth = ShowBottomBoth;
											PptComWriteSetting();

											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::BottomPair);
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.MiddleBoth).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##显示中部两侧控件", &ShowMiddleBoth);

										if (pptComSetlist.showMiddleBoth != ShowMiddleBoth)
										{
											pptComSetlist.showMiddleBoth = ShowMiddleBoth;
											PptComWriteSetting();

											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay.BottomMiddle).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##显示底部主栏控件", &ShowBottomMiddle);

										if (pptComSetlist.showBottomMiddle != ShowBottomMiddle)
										{
											pptComSetlist.showBottomMiddle = ShowBottomMiddle;
											PptComWriteSetting();

											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::ExitShow);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("PPT演示助手#4", { settingItemWidth * settingGlobalScale,155.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("重置控件位置", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.Reset).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										if (Widgets::button.Standard(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
										{
											pptComSetlist.bottomBothWidth = BottomBothWidth = 0;
											pptComSetlist.bottomBothHeight = BottomBothHeight = 0;
											pptComSetlist.middleBothWidth = MiddleBothWidth = 0;
											pptComSetlist.middleBothHeight = MiddleBothHeight = 0;
											pptComSetlist.bottomMiddleWidth = BottomMiddleWidth = 0;
											pptComSetlist.bottomMiddleHeight = BottomMiddleHeight = 0;

											PptComWriteSetting();
											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::All);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("记忆控件位置", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetPosition.Remember).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##记忆控件位置", &MemoryWidgetPosition);

										if (pptComSetlist.memoryWidgetPosition != MemoryWidgetPosition)
										{
											pptComSetlist.memoryWidgetPosition = MemoryWidgetPosition;
											PptComWriteSetting();
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("PPT演示助手#5", { settingItemWidth * settingGlobalScale,215.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.N).c_str());
								}

								// Extra1
								bool allItemActive = false;

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("翻页控件缩放", { settingItemWidth * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.BottomSideBoth).c_str());
									}
									{
										ImGui::SetCursorPos({ 220.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImGui::PushItemWidth(300.0f * settingGlobalScale);

										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										Widgets::slider.Float("##底部两侧控件缩放", &BottomSideBothWidgetScale, 0.5f, 3.0f, "");
										BottomSideBothWidgetScale = round(BottomSideBothWidgetScale * 100) / 100;

										ImGui::PopItemWidth();

										bool isItemHovered = ImGui::IsItemHovered();
										bool isItemActive = ImGui::IsItemActive();
										if (isItemActive) allItemActive = true;

										if (isItemHovered)
										{
											PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
											PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
											PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

											ImGui::BeginTooltip();

											ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind), make_format_args(BottomSideBothWidgetScale)).c_str());

											ImGui::EndTooltip();
										}
										if (BottomSideBothWidgetScale != BottomSideBothWidgetScaleRecord)
										{
											pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
											if (BottomSideBothWidgetScaleUnifie)
											{
												if (MiddleSideBothWidgetScaleUnifie)
													pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = BottomSideBothWidgetScale;
												if (BottomSideMiddleWidgetScaleUnifie)
													pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = BottomSideBothWidgetScale;
											}
											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::BottomPair);
											if (BottomSideBothWidgetScaleUnifie && MiddleSideBothWidgetScaleUnifie)
												Inkeys::UI::Ppt::NotifyConfigurationChanged(
													Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
											if (BottomSideBothWidgetScaleUnifie && BottomSideMiddleWidgetScaleUnifie)
												Inkeys::UI::Ppt::NotifyConfigurationChanged(
													Inkeys::UI::Ppt::ConfigGroup::ExitShow);
										}
									}
									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

										string temp = vformat(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind), make_format_args(BottomSideBothWidgetScale)).c_str();
										ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

										ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 10.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
										ImGui::TextUnformatted(temp.c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightButtonPairLeftX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										if (Widgets::button.Standard((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset) + "##1").c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
										{
											pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = 1.0f;
											if (BottomSideBothWidgetScaleUnifie)
											{
												if (MiddleSideBothWidgetScaleUnifie)
													pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = BottomSideBothWidgetScale;
												if (BottomSideMiddleWidgetScaleUnifie)
													pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = BottomSideBothWidgetScale;
											}
										}
									}
									{
										ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

										if (Widgets::button.AccentToggle((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Sync) + "##1").c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }, BottomSideBothWidgetScaleUnifie))
										{
											if (BottomSideBothWidgetScaleUnifie) BottomSideBothWidgetScaleUnifie = false;
											else
											{
												BottomSideBothWidgetScaleUnifie = true;

												if (MiddleSideBothWidgetScaleUnifie)
												{
													pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = pptComSetlist.middleSideBothWidgetScale;
													PptComWriteSetting();
												}
												else if (BottomSideMiddleWidgetScaleUnifie)
												{
													pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = pptComSetlist.bottomSideMiddleWidgetScale;
													PptComWriteSetting();
												}
											}
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 15.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Page.MiddleSideBoth).c_str());
									}
									{
										ImGui::SetCursorPos({ 220.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImGui::PushItemWidth(300.0f * settingGlobalScale);

										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										Widgets::slider.Float("##中部两侧控件缩放", &MiddleSideBothWidgetScale, 0.5f, 3.0f, "");
										MiddleSideBothWidgetScale = round(MiddleSideBothWidgetScale * 100) / 100;

										ImGui::PopItemWidth();

										bool isItemHovered = ImGui::IsItemHovered();
										bool isItemActive = ImGui::IsItemActive();
										if (isItemActive) allItemActive = true;

										if (isItemHovered)
										{
											PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
											PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
											PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

											ImGui::BeginTooltip();

											ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind), make_format_args(MiddleSideBothWidgetScale)).c_str());

											ImGui::EndTooltip();
										}
										if (MiddleSideBothWidgetScale != MiddleSideBothWidgetScaleRecord)
										{
											pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
											if (MiddleSideBothWidgetScaleUnifie)
											{
												if (BottomSideBothWidgetScaleUnifie)
													pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = MiddleSideBothWidgetScale;
												if (BottomSideMiddleWidgetScaleUnifie)
													pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = MiddleSideBothWidgetScale;
											}
											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
											if (MiddleSideBothWidgetScaleUnifie && BottomSideBothWidgetScaleUnifie)
												Inkeys::UI::Ppt::NotifyConfigurationChanged(
													Inkeys::UI::Ppt::ConfigGroup::BottomPair);
											if (MiddleSideBothWidgetScaleUnifie && BottomSideMiddleWidgetScaleUnifie)
												Inkeys::UI::Ppt::NotifyConfigurationChanged(
													Inkeys::UI::Ppt::ConfigGroup::ExitShow);
										}
									}
									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

										string temp = vformat(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind), make_format_args(MiddleSideBothWidgetScale)).c_str();
										ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

										ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 10.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
										ImGui::TextUnformatted(temp.c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightButtonPairLeftX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										if (Widgets::button.Standard((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset) + "##2").c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
										{
											pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = 1.0f;
											if (MiddleSideBothWidgetScaleUnifie)
											{
												if (BottomSideBothWidgetScaleUnifie)
													pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = MiddleSideBothWidgetScale;
												if (BottomSideMiddleWidgetScaleUnifie)
													pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = MiddleSideBothWidgetScale;
											}
										}
									}
									{
										ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

										if (Widgets::button.AccentToggle((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Sync) + "##2").c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }, MiddleSideBothWidgetScaleUnifie))
										{
											if (MiddleSideBothWidgetScaleUnifie) MiddleSideBothWidgetScaleUnifie = false;
											else
											{
												MiddleSideBothWidgetScaleUnifie = true;

												if (BottomSideBothWidgetScaleUnifie)
												{
													pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = pptComSetlist.bottomSideBothWidgetScale;
													PptComWriteSetting();
												}
												else if (BottomSideMiddleWidgetScaleUnifie)
												{
													pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = pptComSetlist.bottomSideMiddleWidgetScale;
													PptComWriteSetting();
												}
											}
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("状态控件缩放", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.State.BottomSideMiddle).c_str());
									}
									{
										ImGui::SetCursorPos({ 220.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImGui::PushItemWidth(300.0f * settingGlobalScale);

										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										Widgets::slider.Float("##底部主栏控件缩放", &BottomSideMiddleWidgetScale, 0.5f, 3.0f, "");
										BottomSideMiddleWidgetScale = round(BottomSideMiddleWidgetScale * 100) / 100;

										ImGui::PopItemWidth();

										bool isItemHovered = ImGui::IsItemHovered();
										bool isItemActive = ImGui::IsItemActive();
										if (isItemActive) allItemActive = true;

										if (ImGui::IsItemHovered())
										{
											PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, Widgets::FluentColor::White);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, Widgets::FluentColor::ControlStroke);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextPrimary);
											PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
											PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

											ImGui::BeginTooltip();

											ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind), make_format_args(BottomSideMiddleWidgetScale)).c_str());

											ImGui::EndTooltip();
										}
										if (BottomSideMiddleWidgetScale != BottomSideMiddleWidgetScaleRecord)
										{
											pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale;
											if (BottomSideMiddleWidgetScaleUnifie)
											{
												if (BottomSideBothWidgetScaleUnifie)
													pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = BottomSideMiddleWidgetScale;
												if (MiddleSideBothWidgetScaleUnifie)
													pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = BottomSideMiddleWidgetScale;
											}
											Inkeys::UI::Ppt::NotifyConfigurationChanged(
												Inkeys::UI::Ppt::ConfigGroup::ExitShow);
											if (BottomSideMiddleWidgetScaleUnifie && BottomSideBothWidgetScaleUnifie)
												Inkeys::UI::Ppt::NotifyConfigurationChanged(
													Inkeys::UI::Ppt::ConfigGroup::BottomPair);
											if (BottomSideMiddleWidgetScaleUnifie && MiddleSideBothWidgetScaleUnifie)
												Inkeys::UI::Ppt::NotifyConfigurationChanged(
													Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
										}
									}
									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

										string temp = vformat(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind), make_format_args(BottomSideMiddleWidgetScale)).c_str();
										ImVec2 tempVec = ImGui::CalcTextSize(temp.c_str());

										ImGui::SameLine(); ImGui::SetCursorPos({ ImGui::GetCursorPosX() - 15.0f * settingGlobalScale - tempVec.x, cursosPosY + 10.0f * settingGlobalScale + (30.0f * settingGlobalScale - tempVec.y) / 2.0f });
										ImGui::TextUnformatted(temp.c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightButtonPairLeftX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										if (Widgets::button.Standard((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Reset) + "##3").c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
										{
											pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = 1.0f;
											if (BottomSideMiddleWidgetScaleUnifie)
											{
												if (BottomSideBothWidgetScaleUnifie)
													pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = BottomSideMiddleWidgetScale;
												if (MiddleSideBothWidgetScaleUnifie)
													pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = BottomSideMiddleWidgetScale;
											}
										}
									}
									{
										ImGui::SetCursorPos({ settingRightButtonX * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

										if (Widgets::button.AccentToggle((IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Sync) + "##3").c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }, BottomSideMiddleWidgetScaleUnifie))
										{
											if (BottomSideMiddleWidgetScaleUnifie) BottomSideMiddleWidgetScaleUnifie = false;
											else
											{
												BottomSideMiddleWidgetScaleUnifie = true;

												if (BottomSideBothWidgetScaleUnifie)
												{
													pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = pptComSetlist.bottomSideBothWidgetScale;
													PptComWriteSetting();
												}
												else if (MiddleSideBothWidgetScaleUnifie)
												{
													pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = pptComSetlist.middleSideBothWidgetScale;
													PptComWriteSetting();
												}
											}
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								// Extra2
								if (!allItemActive)
								{
									if (BottomSideBothWidgetScale != BottomSideBothWidgetScaleRecord)
									{
										BottomSideBothWidgetScaleRecord = BottomSideBothWidgetScale;
										pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
										PptComWriteSetting();
										Inkeys::UI::Ppt::NotifyConfigurationChanged(
											Inkeys::UI::Ppt::ConfigGroup::BottomPair);
									}
									if (MiddleSideBothWidgetScale != MiddleSideBothWidgetScaleRecord)
									{
										MiddleSideBothWidgetScaleRecord = MiddleSideBothWidgetScale;
										pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
										PptComWriteSetting();
										Inkeys::UI::Ppt::NotifyConfigurationChanged(
											Inkeys::UI::Ppt::ConfigGroup::MiddlePair);
									}
									if (BottomSideMiddleWidgetScale != BottomSideMiddleWidgetScaleRecord)
									{
										BottomSideMiddleWidgetScaleRecord = BottomSideMiddleWidgetScale;
										pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale;
										PptComWriteSetting();
										Inkeys::UI::Ppt::NotifyConfigurationChanged(
											Inkeys::UI::Ppt::ConfigGroup::ExitShow);
									}
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							/*
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("PPT演示助手#6", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("允许关闭游离卡死的 WPP 演示进程", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.CloseWpp).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##允许关闭游离卡死的 WPP 演示进程", &AutoKillWpsProcess);

										if (pptComSetlist.autoKillWpsProcess != AutoKillWpsProcess)
										{
											pptComSetlist.autoKillWpsProcess = AutoKillWpsProcess;
											PptComWriteSetting();
										}
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
										ImGui::BeginChild("允许关闭游离卡死的 WPP 演示进程-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

										{
											ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

											ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.CloseWppE).c_str());
										}

										{
											if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
											if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
											while (PushFontNum) PushFontNum--, ImGui::PopFont();
										}
										ImGui::EndChild();
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							*/

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("PPT演示助手#7", { settingItemWidth * settingGlobalScale,300.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("接管放映批注", { settingItemWidth * settingGlobalScale,200.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOver).c_str());
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverE).c_str());
									}
									{
										bool value = Inkeys::config.PlugIn.PPTHelper.AutoTakeOver;

										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##接管放映批注", &value);

										if (Inkeys::config.PlugIn.PPTHelper.AutoTakeOver != value)
										{
											Inkeys::config.PlugIn.PPTHelper.AutoTakeOver = value;
											QueueConfigWrite();
										}
									}

									// Separator
									cursosPosY = 70.0f * settingGlobalScale;
									{
										ImGui::SetCursorPosY(cursosPosY);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = 70.0f * settingGlobalScale;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverOnce).c_str());
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverOnceE).c_str());
									}
									{
										bool value = Inkeys::config.PlugIn.PPTHelper.AutoTakeOverOnce;

										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##AutoTakeOverOnce", &value);

										if (Inkeys::config.PlugIn.PPTHelper.AutoTakeOverOnce != value)
										{
											Inkeys::config.PlugIn.PPTHelper.AutoTakeOverOnce = value;
											QueueConfigWrite();
										}
									}

									// Separator
									cursosPosY = 140.0f * settingGlobalScale;
									{
										ImGui::SetCursorPosY(cursosPosY);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = 140.0f * settingGlobalScale;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.AutoTakeOverExpand).c_str());
									}
									{
										bool value = Inkeys::config.PlugIn.PPTHelper.AutoTakeOverExpand;

										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##接管放映批注拓展", &value);

										if (Inkeys::config.PlugIn.PPTHelper.AutoTakeOverExpand != value)
										{
											Inkeys::config.PlugIn.PPTHelper.AutoTakeOverExpand = value;
											QueueConfigWrite();
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("启用翻页按钮长按翻页", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress).c_str());
									}
									{
										bool value = Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress;

										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##启用翻页按钮长按翻页", &value);

										if (Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress != value)
										{
											Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress = value;
											QueueConfigWrite();
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
								ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
							}
							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					case settingPlugInTabEnum::tabSuperTop:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
						ImGui::BeginChild("超级置顶", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

						{
							ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							}

							ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
							{
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.SuperTop.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted("20260202a"); // 主界面还有版本号
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
							ImGui::BeginChild("超级置顶主栏", { settingContentPanelWidth * settingGlobalScale,555.0f * settingGlobalScale }, false);

							{
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
								ImGui::BeginChild("超级置顶#01", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
									ImGui::TextUnformatted("\ue814");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("超级置顶-提示1", { settingPromptWidth * settingGlobalScale,90.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Warn).c_str());
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("超级置顶#1", { settingItemWidth * settingGlobalScale,205.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("超级置顶", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.SuperTop).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##超级置顶", &PlugInSetting.SuperTop.Enable);

										if (setlist.plugInSetting.superTop.enable != PlugInSetting.SuperTop.Enable)
										{
											setlist.plugInSetting.superTop.enable = PlugInSetting.SuperTop.Enable;
											WriteSetting();
										}
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
										ImGui::BeginChild("超级置顶-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

										{
											ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

											if (hasSuperTop) ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.SuperTopE1).c_str());
											else ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.SuperTopE2).c_str());
										}

										{
											if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
											if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
											while (PushFontNum) PushFontNum--, ImGui::PopFont();
										}
										ImGui::EndChild();
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("超级置顶指示器", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.Indicator).c_str());
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.IndicatorE).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##超级置顶指示器", &PlugInSetting.SuperTop.Indicator);

										if (setlist.plugInSetting.superTop.indicator != PlugInSetting.SuperTop.Indicator)
										{
											setlist.plugInSetting.superTop.indicator = PlugInSetting.SuperTop.Indicator;
											WriteSetting();
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
								ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
							}
							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					case settingPlugInTabEnum::tabPlug3:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
						ImGui::BeginChild("快捷方式保障助手", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

						{
							ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							}

							ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
							{
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted("20250223a");  // 主界面还有版本号
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
							ImGui::BeginChild("快捷方式保障助手主栏", { settingContentPanelWidth * settingGlobalScale,555.0f * settingGlobalScale }, false);

							{
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("快捷方式保障助手#1", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("修正桌面快捷方式指向和名称", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.FixLnk).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##修正桌面快捷方式指向和名称", &CorrectLnk);

										if (setlist.shortcutAssistant.correctLnk != CorrectLnk)
										{
											setlist.shortcutAssistant.correctLnk = CorrectLnk;
											WriteSetting();

											if (setlist.shortcutAssistant.correctLnk) shortcutAssistant.SetShortcut();
										}
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
										ImGui::BeginChild("修正桌面快捷方式指向和名称-介绍", { settingDescriptionWidth * settingGlobalScale,30.0f * settingGlobalScale }, false);

										{
											ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
											PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

											ImGui::TextWrapped(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.FixLnkE).c_str());
										}

										{
											if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
											if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
											while (PushFontNum) PushFontNum--, ImGui::PopFont();
										}
										ImGui::EndChild();
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("快捷方式保障助手#2", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.N).c_str());
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("创建桌面快捷方式", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnk).c_str());
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnkE).c_str());
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##创建桌面快捷方式", &CreateLnk);

										if (setlist.shortcutAssistant.createLnk != CreateLnk)
										{
											setlist.shortcutAssistant.createLnk = CreateLnk;
											WriteSetting();

											if (setlist.shortcutAssistant.correctLnk) shortcutAssistant.SetShortcut();
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
								ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
							}
							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					case settingPlugInTabEnum::tabPlugDDB:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
						ImGui::BeginChild("同类软件悬浮窗拦截助手", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

						{
							ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							}

							ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
							{
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted(IA(I18nKey.SettingsUI.PlugIn.DesktopDrawpadBlocker.N).c_str());
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted(utf16ToUtf8(ddbInteractionSetList.DdbEdition).c_str());
								}
							}

							ImGui::SetCursorPos({ 720.0f * settingGlobalScale,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								if (Widgets::button.Standard("\uf901", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/DesktopDrawpadBlocker", 0, 0, SW_SHOW);
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
							ImGui::BeginChild("同类软件悬浮窗拦截助手主栏", { settingContentPanelWidth * settingGlobalScale,555.0f * settingGlobalScale }, false);

							{
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
								ImGui::BeginChild("同类软件悬浮窗拦截助手#01", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
									ImGui::TextUnformatted("\ue814");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("同类软件悬浮窗拦截助手-提示1", { settingPromptWidth * settingGlobalScale,90.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextWrapped("插件仅供学习交流和研究使用，不得用于其他任何用途。\n同类软件悬浮窗拦截助手(DesktopDrawpadBlocker)是依据 GPLv3 许可协议发布的开源软件。\n并在 Github 仓库得到发布：https://github.com/Alan-CRL/DesktopDrawpadBlocker\n我们发布这款程序，希望它有用，但不承诺任何质量保证责任。\n用户在使用该插件时，需自行承担由此产生的后果和影响，使用插件则视为同意此协议。");
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("同类软件悬浮窗拦截助手#1", { settingItemWidth * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted("使用插件");
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("启用插件", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("启用插件");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("默认模式下插件随软件开启和关闭。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##启用插件", &Ddb.Enable);

										if (ddbInteractionSetList.enable != Ddb.Enable)
										{
											ddbInteractionSetList.enable = Ddb.Enable;
											SettingBusinessCommand command;
											command.kind = SettingBusinessKind::ConfigureDdb;
											command.flag = Ddb.Enable;
											command.secondaryFlag = ddbInteractionSetList.runAsAdmin;
											command.text = pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe";
											command.directory = pluginPath + L"DesktopDrawpadBlocker";
											command.parameters = GetCurrentExePath();
											command.digest = ddbInteractionSetList.DdbSHA256;
											QueueBusiness(std::move(command));
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("同类软件悬浮窗拦截助手#2", { settingItemWidth * settingGlobalScale,175.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted("常规选项");
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("以管理员身份启动插件", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("以管理员身份启动插件");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("切换后软件将阻塞至多拦截间隔的秒数，以等待插件的响应。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##以管理员身份启动插件", &Ddb.RunAsAdmin);

										if (ddbInteractionSetList.runAsAdmin != Ddb.RunAsAdmin)
										{
											ddbInteractionSetList.runAsAdmin = Ddb.RunAsAdmin;
											WriteSetting();

											SettingBusinessCommand command;
											command.kind = SettingBusinessKind::RestartDdb;
											command.flag = Ddb.RunAsAdmin;
											command.text = pluginPath + L"DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe";
											QueueBusiness(std::move(command));
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("拦截间隔", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("拦截间隔");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("更短的间隔可以更快拦截出现的窗口，但会占用更多的CPU资源。");
									}
									{
										ImGui::SetCursorPos({ settingRightComboX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImGui::SetNextItemWidth(200 * settingGlobalScale);

										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

										int SleepTime, SleenTimeRecord;
										if (ddbInteractionSetList.sleepTime == 500) SleepTime = 0;
										else if (ddbInteractionSetList.sleepTime == 1000) SleepTime = 1;
										else if (ddbInteractionSetList.sleepTime == 3000) SleepTime = 2;
										else if (ddbInteractionSetList.sleepTime == 10000) SleepTime = 4;
										else SleepTime = 3;
										SleenTimeRecord = SleepTime;

										vector<char*> vec;
										vec.emplace_back(_strdup(("  " + string("短（500ms）")).c_str()));
										vec.emplace_back(_strdup(("  " + string("较短（1s）")).c_str()));
										vec.emplace_back(_strdup(("  " + string("中等（3s）")).c_str()));
										vec.emplace_back(_strdup(("  " + string("较长（5s）")).c_str()));
										vec.emplace_back(_strdup(("  " + string("长（10s）")).c_str()));

										if (Widgets::combo.Begin("##拦截间隔", vec[SleepTime], static_cast<int>(vec.size())))
										{
											for (int i = 0; i < vec.size(); i++)
											{
												ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));

												bool is_selected = (SleepTime == i);
												if (Widgets::combo.Selectable(vec[i], is_selected))
												{
													SleepTime = i;
													if (SleenTimeRecord != SleepTime)
													{
														if (SleepTime == 0) ddbInteractionSetList.sleepTime = 500;
														else if (SleepTime == 1) ddbInteractionSetList.sleepTime = 1000;
														else if (SleepTime == 2) ddbInteractionSetList.sleepTime = 3000;
														else if (SleepTime == 4) ddbInteractionSetList.sleepTime = 10000;
														else ddbInteractionSetList.sleepTime = 5000;
														WriteSetting();

											QueueDdbWriteInteraction(true, false);
													}
												}
											}
											ImGui::Dummy(ImVec2(0, 8.0f * settingGlobalScale));
											Widgets::combo.End();
										}
										for (char* ptr : vec) free(ptr), ptr = nullptr;
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
								ImGui::BeginChild("同类软件悬浮窗拦截助手#3", { settingItemWidth * settingGlobalScale,1200.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								{
									ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted("精确控制");
								}

								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#1", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃白板3 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃白板3 桌面画笔悬浮窗", &Ddb.intercept.SeewoWhiteboard3Floating);

										if (ddbInteractionSetList.intercept.SeewoWhiteboard3Floating != Ddb.intercept.SeewoWhiteboard3Floating)
										{
											ddbInteractionSetList.intercept.SeewoWhiteboard3Floating = Ddb.intercept.SeewoWhiteboard3Floating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#2", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃白板5 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃白板5 桌面画笔悬浮窗", &Ddb.intercept.SeewoWhiteboard5Floating);

										if (ddbInteractionSetList.intercept.SeewoWhiteboard5Floating != Ddb.intercept.SeewoWhiteboard5Floating)
										{
											ddbInteractionSetList.intercept.SeewoWhiteboard5Floating = Ddb.intercept.SeewoWhiteboard5Floating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#3", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃轻白板（5C） 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃轻白板（5C） 桌面画笔悬浮窗", &Ddb.intercept.SeewoWhiteboard5CFloating);

										if (ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating != Ddb.intercept.SeewoWhiteboard5CFloating)
										{
											ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating = Ddb.intercept.SeewoWhiteboard5CFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#4", { settingItemWidth * settingGlobalScale,130.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃品课教师端 侧栏悬浮窗");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃品课教师端 侧栏悬浮窗", &Ddb.intercept.SeewoPincoSideBarFloating);

										if (ddbInteractionSetList.intercept.SeewoPincoSideBarFloating != Ddb.intercept.SeewoPincoSideBarFloating)
										{
											ddbInteractionSetList.intercept.SeewoPincoSideBarFloating = Ddb.intercept.SeewoPincoSideBarFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃品课教师端 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("包括PPT控件。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃品课教师端 桌面画笔悬浮窗", &Ddb.intercept.SeewoPincoDrawingFloating);

										if (ddbInteractionSetList.intercept.SeewoPincoDrawingFloating != Ddb.intercept.SeewoPincoDrawingFloating)
										{
											ddbInteractionSetList.intercept.SeewoPincoDrawingFloating = Ddb.intercept.SeewoPincoDrawingFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#5", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃PPT小工具");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃PPT小工具", &Ddb.intercept.SeewoPPTFloating);

										if (ddbInteractionSetList.intercept.SeewoPPTFloating != Ddb.intercept.SeewoPPTFloating)
										{
											ddbInteractionSetList.intercept.SeewoPPTFloating = Ddb.intercept.SeewoPPTFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#51", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃课堂助手 PPT小工具");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃课堂助手 PPT小工具", &Ddb.intercept.SeewoIwbAssistantFloating);

										if (ddbInteractionSetList.intercept.SeewoIwbAssistantFloating != Ddb.intercept.SeewoIwbAssistantFloating)
										{
											ddbInteractionSetList.intercept.SeewoIwbAssistantFloating = Ddb.intercept.SeewoIwbAssistantFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#52", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("欧帝白板 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("包括PPT控件。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##欧帝白板 桌面画笔悬浮窗", &Ddb.intercept.YiouBoardFloating);

										if (ddbInteractionSetList.intercept.YiouBoardFloating != Ddb.intercept.YiouBoardFloating)
										{
											ddbInteractionSetList.intercept.YiouBoardFloating = Ddb.intercept.YiouBoardFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#6", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("AiClass 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##AiClass 桌面画笔悬浮窗", &Ddb.intercept.AiClassFloating);

										if (ddbInteractionSetList.intercept.AiClassFloating != Ddb.intercept.AiClassFloating)
										{
											ddbInteractionSetList.intercept.AiClassFloating = Ddb.intercept.AiClassFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#7", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("ClassIn X 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##ClassIn X 桌面画笔悬浮窗", &Ddb.intercept.ClassInXFloating);

										if (ddbInteractionSetList.intercept.ClassInXFloating != Ddb.intercept.ClassInXFloating)
										{
											ddbInteractionSetList.intercept.ClassInXFloating = Ddb.intercept.ClassInXFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#9", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("天喻教育云互动课堂 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("包括PPT控件。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##天喻教育云互动课堂 桌面画笔悬浮窗", &Ddb.intercept.IntelligentClassFloating);

										if (ddbInteractionSetList.intercept.IntelligentClassFloating != Ddb.intercept.IntelligentClassFloating)
										{
											ddbInteractionSetList.intercept.IntelligentClassFloating = Ddb.intercept.IntelligentClassFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#8", { settingItemWidth * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("畅言智慧课堂4.0 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("包括PPT控件。支持在白板时自动恢复。需要DDB管理员权限。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##畅言智慧课堂4.0 桌面画笔悬浮窗", &Ddb.intercept.ChangYanFloating);

										if (ddbInteractionSetList.intercept.ChangYanFloating != Ddb.intercept.ChangYanFloating)
										{
											ddbInteractionSetList.intercept.ChangYanFloating = Ddb.intercept.ChangYanFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("畅言智慧课堂5.0 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("包括PPT控件。支持在白板时自动恢复。需要DDB管理员权限。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##畅言智慧课堂5.0 桌面画笔悬浮窗", &Ddb.intercept.ChangYan5Floating);

										if (ddbInteractionSetList.intercept.ChangYan5Floating != Ddb.intercept.ChangYan5Floating)
										{
											ddbInteractionSetList.intercept.ChangYan5Floating = Ddb.intercept.ChangYan5Floating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#11", { settingItemWidth * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("C30智能教学 侧栏悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("需要DDB管理员。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##C30智能教学 侧栏悬浮窗", &Ddb.intercept.Iclass30SidebarFloating);

										if (ddbInteractionSetList.intercept.Iclass30SidebarFloating != Ddb.intercept.Iclass30SidebarFloating)
										{
											ddbInteractionSetList.intercept.Iclass30SidebarFloating = Ddb.intercept.Iclass30SidebarFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("C30智能教学 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("包括PPT控件。支持在白板时自动恢复，支持窗口追踪。需要DDB管理员权限。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##C30智能教学 桌面画笔悬浮窗", &Ddb.intercept.Iclass30Floating);

										if (ddbInteractionSetList.intercept.Iclass30Floating != Ddb.intercept.Iclass30Floating)
										{
											ddbInteractionSetList.intercept.Iclass30Floating = Ddb.intercept.Iclass30Floating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}
								{
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
									ImGui::BeginChild("精确控制#10", { settingItemWidth * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃桌面 侧栏悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("需要DDB管理员。1.0/2.0/2.5/3.0 普教版/高教版 通用");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃桌面 侧栏悬浮窗", &Ddb.intercept.SeewoDesktopSideBarFloating);

										if (ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating != Ddb.intercept.SeewoDesktopSideBarFloating)
										{
											ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating = Ddb.intercept.SeewoDesktopSideBarFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									// Separator
									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
										ImGui::Separator();
									}

									cursosPosY = ImGui::GetCursorPosY();
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("希沃桌面 桌面画笔悬浮窗");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("需要DDB管理员。1.0/2.0/2.5/3.0 普教版/高教版 通用");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##希沃桌面 桌面画笔悬浮窗", &Ddb.intercept.SeewoDesktopDrawingFloating);

										if (ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating != Ddb.intercept.SeewoDesktopDrawingFloating)
										{
											ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating = Ddb.intercept.SeewoDesktopDrawingFloating;
											WriteSetting();

											QueueDdbWriteInteraction(true, false);
										}
									}

									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
									ImGui::EndChild();
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
								ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
							}
							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}
					}
					break;
				}

				// 组件
				case settingTabEnum::tabComponent:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("组件", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted("组件");
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("组件#1", { settingItemWidth * settingGlobalScale,220.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("软件");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("软件#1", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("启动 文件资源管理器");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##启动 文件资源管理器", &ComponentShortcutButtonApplianceExplorer);

								if (setlist.component.shortcutButton.appliance.explorer != ComponentShortcutButtonApplianceExplorer)
								{
									setlist.component.shortcutButton.appliance.explorer = ComponentShortcutButtonApplianceExplorer;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("软件#2", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("启动 任务管理器");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##启动 任务管理器", &ComponentShortcutButtonApplianceTaskmgr);

								if (setlist.component.shortcutButton.appliance.taskmgr != ComponentShortcutButtonApplianceTaskmgr)
								{
									setlist.component.shortcutButton.appliance.taskmgr = ComponentShortcutButtonApplianceTaskmgr;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("软件#3", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("启动 控制面板");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##启动 控制面板", &ComponentShortcutButtonApplianceControl);

								if (setlist.component.shortcutButton.appliance.control != ComponentShortcutButtonApplianceControl)
								{
									setlist.component.shortcutButton.appliance.control = ComponentShortcutButtonApplianceControl;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("组件#2", { settingItemWidth * settingGlobalScale,155.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("系统");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("系统#1", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("显示 桌面");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##显示 桌面", &ComponentShortcutButtonSystemDesktop);

								if (setlist.component.shortcutButton.system.desktop != ComponentShortcutButtonSystemDesktop)
								{
									setlist.component.shortcutButton.system.desktop = ComponentShortcutButtonSystemDesktop;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("系统#2", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("锁屏");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##锁屏", &ComponentShortcutButtonSystemLockWorkStation);

								if (setlist.component.shortcutButton.system.lockWorkStation != ComponentShortcutButtonSystemLockWorkStation)
								{
									setlist.component.shortcutButton.system.lockWorkStation = ComponentShortcutButtonSystemLockWorkStation;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("组件#3", { settingItemWidth * settingGlobalScale,155.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("键盘模拟");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("键盘模拟#1", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("按下 ESC");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##按下 ESC", &ComponentShortcutButtonKeyboardKeyboardesc);

								if (setlist.component.shortcutButton.keyboard.keyboardesc != ComponentShortcutButtonKeyboardKeyboardesc)
								{
									setlist.component.shortcutButton.keyboard.keyboardesc = ComponentShortcutButtonKeyboardKeyboardesc;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("键盘模拟#2", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("按下 Alt+F4");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##按下 Alt+F4", &ComponentShortcutButtonKeyboardKeyboardAltF4);

								if (setlist.component.shortcutButton.keyboard.keyboardAltF4 != ComponentShortcutButtonKeyboardKeyboardAltF4)
								{
									setlist.component.shortcutButton.keyboard.keyboardAltF4 = ComponentShortcutButtonKeyboardKeyboardAltF4;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("组件#4", { settingItemWidth * settingGlobalScale,470.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("点名器");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("点名器#1", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("IslandCaller 1 随机点名");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("需要安装 ClassIsland 插件 IslandCaller，并需要 ClassIsland 注册 Url 协议。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##IslandCaller 1 随机点名", &ComponentShortcutButtonRollCallIslandCaller1);

								if (setlist.component.shortcutButton.rollCall.IslandCaller1 != ComponentShortcutButtonRollCallIslandCaller1)
								{
									setlist.component.shortcutButton.rollCall.IslandCaller1 = ComponentShortcutButtonRollCallIslandCaller1;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("点名器#2", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("IslandCaller 2 单人点名");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("需要安装 ClassIsland 插件 IslandCaller，并需要 ClassIsland 注册 Url 协议。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##IslandCaller 2 单人点名", &ComponentShortcutButtonRollCallIslandCaller2);

								if (setlist.component.shortcutButton.rollCall.IslandCaller2 != ComponentShortcutButtonRollCallIslandCaller2)
								{
									setlist.component.shortcutButton.rollCall.IslandCaller2 = ComponentShortcutButtonRollCallIslandCaller2;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("点名器#3", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("SecRandom 1 闪抽");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("需要 SecRandom 注册 Url 协议。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##SecRandom 1 闪抽", &ComponentShortcutButtonRollCallSecRandom1);

								if (setlist.component.shortcutButton.rollCall.SecRandom1 != ComponentShortcutButtonRollCallSecRandom1)
								{
									setlist.component.shortcutButton.rollCall.SecRandom1 = ComponentShortcutButtonRollCallSecRandom1;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("点名器#4", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("SecRandom 2 闪抽");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("需要 SecRandom 注册 Ipc 协议。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##SecRandom 2 闪抽", &ComponentShortcutButtonRollCallSecRandom2);

								if (setlist.component.shortcutButton.rollCall.SecRandom2 != ComponentShortcutButtonRollCallSecRandom2)
								{
									setlist.component.shortcutButton.rollCall.SecRandom2 = ComponentShortcutButtonRollCallSecRandom2;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("点名器#5", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("SecRandom 2 闪抽（兼容）");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("需要 SecRandom 注册 Url 协议。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##SecRandom 2 闪抽（兼容）", &ComponentShortcutButtonRollCallSecRandom2Compat);

								if (setlist.component.shortcutButton.rollCall.SecRandom2Compat != ComponentShortcutButtonRollCallSecRandom2Compat)
								{
									setlist.component.shortcutButton.rollCall.SecRandom2Compat = ComponentShortcutButtonRollCallSecRandom2Compat;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("点名器#6", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("NamePicker 随机点名");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("需要 NamePicker 注册 Url 协议。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##NamePicker 随机点名", &ComponentShortcutButtonRollCallNamePicker);

								if (setlist.component.shortcutButton.rollCall.NamePicker != ComponentShortcutButtonRollCallNamePicker)
								{
									setlist.component.shortcutButton.rollCall.NamePicker = ComponentShortcutButtonRollCallNamePicker;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("组件#5", { settingItemWidth * settingGlobalScale,285.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("ClassIsland 联动");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("ClassIsland 联动#0", { settingItemWidth * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::AccentText);
								ImGui::TextUnformatted("\uf167");
							}
							{
								ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("组件-提示", { settingPromptWidth * settingGlobalScale,20.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextWrapped("需要在 ClassIsland 应用设置 中注册 Url 导航协议。");
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("ClassIsland 联动#1", { settingItemWidth * settingGlobalScale,190.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("ClassIsland 应用设置");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##ClassIsland 应用设置", &ComponentShortcutButtonLinkageClassislandSettings);

								if (setlist.component.shortcutButton.linkage.classislandSettings != ComponentShortcutButtonLinkageClassislandSettings)
								{
									setlist.component.shortcutButton.linkage.classislandSettings = ComponentShortcutButtonLinkageClassislandSettings;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							// Separator
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
								ImGui::Separator();
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("ClassIsland 档案编辑");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##ClassIsland 档案编辑", &ComponentShortcutButtonLinkageClassislandProfile);

								if (setlist.component.shortcutButton.linkage.classislandProfile != ComponentShortcutButtonLinkageClassislandProfile)
								{
									setlist.component.shortcutButton.linkage.classislandProfile = ComponentShortcutButtonLinkageClassislandProfile;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							// Separator
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, Widgets::FluentColor::Divider);
								ImGui::Separator();
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("ClassIsland 快速换课");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("在 ClassIsland 当前没有加载课表时，此组件不起作用。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##ClassIsland 快速换课", &ComponentShortcutButtonLinkageClassislandClassswap);

								if (setlist.component.shortcutButton.linkage.classislandClassswap != ComponentShortcutButtonLinkageClassislandClassswap)
								{
									setlist.component.shortcutButton.linkage.classislandClassswap = ComponentShortcutButtonLinkageClassislandClassswap;
									WriteSetting();
									SyncUi3BuiltInComponents();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 快捷键
				case settingTabEnum::tab5:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("快捷键", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted(IA(I18nKey.SettingsUI.HotKey.N).c_str());
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
						ImGui::BeginChild("快捷键-输出", { settingContentPanelWidth * settingGlobalScale,565.0f * settingGlobalScale }, true);

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						{
							ImGui::SetCursorPosY(30.0f);
							wstring text = IW(I18nKey.SettingsUI.HotKey.E);

							int left_x = 20 * settingGlobalScale, right_x = 750 * settingGlobalScale;

							std::vector<std::string> lines;
							std::wstring line, temp;
							std::wstringstream ss(text);

							while (getline(ss, temp, L'\n'))
							{
								bool flag = false;
								line = L"";

								for (wchar_t ch : temp)
								{
									flag = false;

									float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
									if (text_width > (right_x - left_x))
									{
										lines.emplace_back(utf16ToUtf8(line));
										line = L"", flag = true;
									}

									line += ch;
								}

								if (!flag) lines.emplace_back(utf16ToUtf8(line));
							}
							for (const auto& temp : lines)
							{
								//float text_width = ImGui::CalcTextSize(temp.c_str()).x;
								//float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
								//if (text_indentation < 0)  text_indentation = 0;
								//ImGui::SetCursorPosX(left_x + text_indentation);
								ImGui::SetCursorPosX(left_x);
								ImGui::TextUnformatted(temp.c_str());
							}

							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 实验室
				case settingTabEnum::tabExperimental:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("实验室", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImGui::TextUnformatted("实验室");
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::Transparent);
						ImGui::BeginChild("Inkeys3", { settingItemWidth * settingGlobalScale,
							((Experimental.Inkeys3.EdgeLightingEnable ? 340.0f : 265.0f)
								+ (Experimental.Inkeys3.DebugMode ? 75.0f : 0.0f)) * settingGlobalScale }, false,
							ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("Inkeys3");
						}

							if (Experimental.Inkeys3.EdgeLightingEnable)
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
								ImGui::BeginChild("动态边缘光影", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true,
									ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
								{
									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("动态边缘光影");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("控制跟随鼠标的第三光源，关闭后停止全局鼠标跟踪。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##动态边缘光影", &Experimental.Inkeys3.DynamicEdgeLighting);
										if (Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic
											!= Experimental.Inkeys3.DynamicEdgeLighting)
										{
											Inkeys::config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic =
												Experimental.Inkeys3.DynamicEdgeLighting;
										Inkeys::UI::Bar::SetEdgeLightingOptions(
											Experimental.Inkeys3.EdgeLightingEnable,
											Experimental.Inkeys3.DynamicEdgeLighting);
										QueueConfigWrite();
										}
									}
									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
								}
								ImGui::EndChild();
							}

							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("脏区调试", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true,
								ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
							{
								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
									ImGui::TextUnformatted("脏区调试");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
									ImGui::TextUnformatted("显示 UI3 每帧实际提交的脏区边界。");
								}
								{
									ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									Widgets::toggle.ToggleBool("##脏区调试", &Experimental.Inkeys3.DebugMode);
									if (Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable
										!= Experimental.Inkeys3.DebugMode)
									{
										Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable =
											Experimental.Inkeys3.DebugMode;
									Inkeys::UI::Bar::SetDebugOptions(
										Experimental.Inkeys3.DebugMode,
										Experimental.Inkeys3.ShowFrameRate);
									QueueConfigWrite();
									}
								}
								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
							}
							ImGui::EndChild();

							if (Experimental.Inkeys3.DebugMode)
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
								ImGui::BeginChild("显示帧率", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true,
									ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
								{
									float cursosPosY = 0;
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
										ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
										ImGui::TextUnformatted("显示帧率");
									}
									{
										ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
										ImGui::TextUnformatted("每秒更新上一秒平均帧率和无等待帧率。");
									}
									{
										ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
										Widgets::toggle.ToggleBool("##显示帧率", &Experimental.Inkeys3.ShowFrameRate);
										if (Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate
											!= Experimental.Inkeys3.ShowFrameRate)
										{
											Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate =
												Experimental.Inkeys3.ShowFrameRate;
										Inkeys::UI::Bar::SetDebugOptions(
											Experimental.Inkeys3.DebugMode,
											Experimental.Inkeys3.ShowFrameRate);
										QueueConfigWrite();
										}
									}
									{
										if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
										if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
										while (PushFontNum) PushFontNum--, ImGui::PopFont();
									}
								}
								ImGui::EndChild();
							}
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("启用 UI3 动画", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true,
								ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("启用动画");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("关闭后，UI3 主栏动画将立即完成。");
							}
							{
								ImGui::SetCursorPos({ settingRightToggleX * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								Widgets::toggle.ToggleBool("##启用 UI3 动画", &Experimental.Inkeys3.AnimationEnable);
								if (Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable
									!= Experimental.Inkeys3.AnimationEnable)
								{
									Inkeys::config.Experimental.Inkeys3.UI3.Animation.Enable =
										Experimental.Inkeys3.AnimationEnable;
									Inkeys::UI::Bar::SetAnimationOptions(Experimental.Inkeys3.AnimationEnable,
										Experimental.Inkeys3.AnimationSpeedRate);
									QueueConfigWrite();
								}
							}
							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();

							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
							ImGui::BeginChild("UI3 动画速度", { settingItemWidth * settingGlobalScale,70.0f * settingGlobalScale }, true,
								ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted("动画速度");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);
								ImGui::TextUnformatted("调整 UI3 主栏动画速度，范围为 0.1x–5.0x。");
							}
							{
								ImGui::SetCursorPos({ (settingItemWidth - 315.0f) * settingGlobalScale, 20.0f * settingGlobalScale });
								ImGui::PushItemWidth(250.0f * settingGlobalScale);
								Widgets::slider.Float("##UI3 动画速度", &Experimental.Inkeys3.AnimationSpeedRate,
									0.1f, 5.0f, "");
								Experimental.Inkeys3.AnimationSpeedRate =
									round(Experimental.Inkeys3.AnimationSpeedRate * 10.0f) / 10.0f;
								ImGui::PopItemWidth();

								bool isItemActive = ImGui::IsItemActive();
								if (fabs(Experimental.Inkeys3.AnimationSpeedRate - static_cast<float>(
									Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate.load())) > 0.0001f)
								{
									Inkeys::config.Experimental.Inkeys3.UI3.Animation.SpeedRate =
										static_cast<double>(Experimental.Inkeys3.AnimationSpeedRate);
									Inkeys::UI::Bar::SetAnimationOptions(Experimental.Inkeys3.AnimationEnable,
										Experimental.Inkeys3.AnimationSpeedRate);
									Experimental.Inkeys3.AnimationSpeedSavePending = true;
								}
								if (!isItemActive && Experimental.Inkeys3.AnimationSpeedSavePending)
								{
									QueueConfigWrite();
									Experimental.Inkeys3.AnimationSpeedSavePending = false;
								}
							}
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								string speedText = format("{:.1f}x", Experimental.Inkeys3.AnimationSpeedRate);
								ImVec2 textSize = ImGui::CalcTextSize(speedText.c_str());
								ImGui::SameLine();
								ImGui::SetCursorPos({ (settingItemWidth - 20.0f) * settingGlobalScale - textSize.x,
									15.0f * settingGlobalScale + (30.0f * settingGlobalScale - textSize.y) / 2.0f });
								ImGui::TextUnformatted(speedText.c_str());
							}
							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// ---------------------

				// 赞助我们
				case settingTabEnum::tab8:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					ImGui::BeginChild("赞助我们", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, true, ImGuiWindowFlags_HorizontalScrollbar);

					ImGui::SetCursorPos({ 50.0f * settingGlobalScale,20.0f * settingGlobalScale });
					ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[9], ImVec2((float)settingSign[9].width, (float)settingSign[9].height));

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f);
						wstring text = L"成功赞助后，可以联系作者将您的昵称和赞助的金额添加到社区名片中以表示感谢。";

						int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(text);

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(utf16ToUtf8(line));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(utf16ToUtf8(line));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// ---------------------

				// 程序调测
				case settingTabEnum::tab9:
				{
					ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WindowBackground);
					ImGui::BeginChild("程序调测", { settingContentPanelWidth * settingGlobalScale,settingContentPanelHeight * settingGlobalScale }, false, ImGuiWindowFlags_HorizontalScrollbar);

					{
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
						ImGui::BeginChild("启用触摸测试模式", { settingContentPanelWidth * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted("启用触摸测试模式");
						}
						{
							ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextSecondary);

							ImGui::TextUnformatted("开启后，使用输入设备在主画布上产生输入，即刻开始测试。");
						}
						{
							ImGui::SetCursorPos({ 660.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							if (Widgets::button.Standard("开启", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
							{
								ChangeStateModeToTouchTest();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
						ImGui::BeginChild("程序调测-输出", { settingContentPanelWidth * settingGlobalScale,533.0f * settingGlobalScale }, true);

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						{
							ImGui::SetCursorPosY(30.0f);
							wstring text;
							{
								text += L"输入设备按下：";
								text += rtsDown ? L"是" : L"否";
								text += L"\n输入设备点：";
								text += to_wstring(rtsNum) + L"\n";
								text += L"触摸设备点：";
								text += to_wstring(touchNum) + L"\n";

								for (int i = 0; i < rtsNum; i++)
								{
									std::shared_lock<std::shared_mutex> lock1(touchPosSm);
									TouchMode mode = TouchPos[TouchList[i]];
									lock1.unlock();

									std::shared_lock<std::shared_mutex> lock2(touchSpeedSm);
									double speed = TouchSpeed[TouchList[i]];
									lock2.unlock();

									{
										wstring pid = L"pid" + to_wstring(TouchList[i]);
										if (pid.length() < 10) pid += wstring(10 - pid.length(), L' ');
										text += pid + L"|";
									}
									{
										wstring type;
										if (mode.type == 0) type = L" 触摸点";
										else if (mode.type == 1)
										{
											if (mode.isInvertedCursor) type = L" 触控笔(倒置)";
											else type = L" 触控笔";
										}
										else if (mode.type == 2) type = L" 鼠标(左键)";
										else if (mode.type == 3) type = L" 鼠标(右键)";
										if (type.length() < 10) type += wstring(10 - type.length(), L' ');
										text += type + L"|";
									}
									{
										wstring loc = L" 坐标" + to_wstring(mode.pt.x) + L"," + to_wstring(mode.pt.y);
										if (loc.length() < 15) loc += wstring(15 - loc.length(), L' ');
										text += loc + L"|";
									}
									{
										wstring spe = L" 速度" + to_wstring(speed);
										if (spe.length() < 20) spe += wstring(20 - spe.length(), L' ');
										text += spe + L"|";
									}
									{
										wstring siz;
										if (mode.type == 0) siz = L" 面积" + to_wstring(mode.touchWidth) + L"," + to_wstring(mode.touchHeight);
										else siz = L" 面积(此设备不支持)";
										if (siz.length() < 15) siz += wstring(15 - siz.length(), L' ');
										text += siz + L"|";
									}
									{
										wstring pre;
										if (mode.type == 1 && mode.isInvertedCursor) pre = L" 压力(落笔时)" + to_wstring(mode.pressure);
										else
										{
											if (mode.type == 1) pre = L" 压力" + to_wstring(mode.pressure);
											else pre = L" 压力(此设备不支持)";
										}
										if (pre.length() < 20) pre += wstring(20 - pre.length(), L' ');
										text += pre + L"\n";
									}
								}

								text += L"\nTouchList ";
								for (const auto& val : TouchList)
								{
									text += to_wstring(val) + L" ";
								}
								text += L"\nTouchTemp ";
								for (size_t i = 0; i < TouchTemp.size(); ++i)
								{
									text += to_wstring(TouchTemp[i].pid) + L" ";
								}

								text += L"\n\n撤回库当前大小：" + to_wstring(RecallImage.size()) + L"(峰值" + to_wstring(RecallImagePeak) + L")";
								/*text += L"\n撤回库 recall_image_recond：" + to_wstring(recall_image_recond);
								text += L"\n撤回库 reference_record_pointer：" + to_wstring(reference_record_pointer);
								text += L"\n撤回库 practical_total_record_pointer：" + to_wstring(practical_total_record_pointer);
								text += L"\n撤回库 total_record_pointer：" + to_wstring(total_record_pointer);
								text += L"\n撤回库 current_record_pointer：" + to_wstring(current_record_pointer);*/
								text += L"\n首次绘制状态：", text += (FirstDraw == true) ? L"是" : L"否";

								{
									wstring ppt_LinkTest;
									if (pptComVersion.substr(0, 7) == L"Error: ") ppt_LinkTest = L"发生错误 " + pptComVersion;
									else ppt_LinkTest = L"连接成功，版本 " + pptComVersion;

									text += L"\n\nPPT COM接口 联动组件 状态：";
									text += ppt_LinkTest;
								}

								text += L"\nPPT 状态：";
								text += PptInfoState.TotalPage != -1 ? L"正在播放" : L"未播放";
								text += L"\nPPT 总页面数：";
								text += to_wstring(PptInfoState.TotalPage);
								text += L"\nPPT 当前页序号：";
								text += to_wstring(PptInfoState.CurrentPage);

								text += L"\n\n监视器数量：";
								text += to_wstring(DisplaysNumber);
								text += L"\n主监视器像素宽度：";
								text += to_wstring(MainMonitor.MonitorWidth) + L"px";
								text += L"\n主监视器像素高度：";
								text += to_wstring(MainMonitor.MonitorHeight) + L"px";
								text += L"\n主监视器物理宽度：";
								text += to_wstring(MainMonitor.MonitorPhyWidth) + L"cm";
								text += L"\n主监视器物理高度：";
								text += to_wstring(MainMonitor.MonitorPhyHeight) + L"cm";
							}

							int left_x = 20 * settingGlobalScale, right_x = 750 * settingGlobalScale;

							std::vector<std::string> lines;
							std::wstring line, temp;
							std::wstringstream ss(text);

							while (getline(ss, temp, L'\n'))
							{
								bool flag = false;
								line = L"";

								for (wchar_t ch : temp)
								{
									flag = false;

									float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
									if (text_width > (right_x - left_x))
									{
										lines.emplace_back(utf16ToUtf8(line));
										line = L"", flag = true;
									}

									line += ch;
								}

								if (!flag) lines.emplace_back(utf16ToUtf8(line));
							}
							for (const auto& temp : lines)
							{
								//float text_width = ImGui::CalcTextSize(temp.c_str()).x;
								//float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
								//if (text_indentation < 0)  text_indentation = 0;
								//ImGui::SetCursorPosX(left_x + text_indentation);
								ImGui::SetCursorPosX(left_x);
								ImGui::TextUnformatted(temp.c_str());
							}

							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
						ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
					}
					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}
				}
				ImGui::PopStyleVar();

				// 底栏：更新信息提示栏
				{
					using enum AutomaticUpdateStateEnum;
					const float bottomStatusY = max(42.0F, logicalWindowHeight - 40.0F);
					const float bottomStatusWidth = max(280.0F,
						settingContentPanelWidth - 105.0F);
					const float bottomActionX = settingContentOriginX
						+ bottomStatusWidth + 5.0F;

					if (AutomaticUpdateState == UpdateNotStarted)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.NotStarted).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.Repair).c_str()))
							{
								QueueInformation(L"The automatic update module has not been activated, which means that you are not using an official release. \nPlease go to the \"version\" page and click \"Fix Software\".\n自动更新模块尚未启动，这意味着您使用的不是官方发布版本。\n请前往“软件版本”页并点击“修复软件”。");
							}

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateObtainInformation)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::CardBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::AccentText);
							ImGui::TextUnformatted("\uf167");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.ObtainInformation).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateInformationFail)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::DangerBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::Danger);
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.InformationFail).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateInformationDamage)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::DangerBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::Danger);
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.InformationDamage).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateInformationUnStandardized)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::DangerBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::Danger);
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.InformationUnStandardized).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateDownloading)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							try
							{
								ImGui::TextUnformatted(vformat(IA(I18nKey.SettingsUI.Update.Downloading), make_format_args(downloadLine)).c_str());
							}
							catch (...)
							{
								ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.Downloading).c_str());
							}
						}
						{
							double downloadedSize = static_cast<double>(downloadNewProgramState.downloadedSize.load());
							double fileSize = static_cast<double>(downloadNewProgramState.fileSize.load());

							if (fileSize != 0 && downloadedSize <= fileSize)
							{
								ImGui::SetCursorPos({ 665.0f * settingGlobalScale - ImGui::CalcTextSize(format("{:.1f}%", downloadedSize / fileSize * 100).c_str()).x, cursosPosY + 8.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
								ImGui::TextUnformatted(format("{:.1f}%", downloadedSize / fileSize * 100).c_str());
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateDownloadFail)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::DangerBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::Danger);
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.DownloadFail).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateDownloadDamage)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::DangerBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::Danger);
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.DownloadDamage).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.ManualDownload).c_str()))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top/", 0, 0, SW_SHOW);
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateRestart)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.Restart).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateLatest)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::SuccessBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::SuccessText);
							ImGui::TextUnformatted("\uec61");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

							string channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Other) + ")";
							string updateChannel = GetUpdateChannel();
							if (updateChannel == "LTS") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.LTS) + ")";
							else if (updateChannel == "Insider") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Insider) + ")";
							else if (updateChannel == "Dev") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Dev) + ")";
							else if (updateChannel == "Canary") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Canary) + ")";

							ImGui::TextUnformatted((IA(I18nKey.SettingsUI.Update.Latest) + channel).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateNewer)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::SuccessBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::SuccessText);
							ImGui::TextUnformatted("\uec61");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);

							string channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Other) + ")";
							string updateChannel = GetUpdateChannel();
							if (updateChannel == "LTS") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.LTS) + ")";
							else if (updateChannel == "Insider") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Insider) + ")";
							else if (updateChannel == "Dev") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Dev) + ")";
							else if (updateChannel == "Canary") channel = " (" + IA(I18nKey.SettingsUI.Update.Channel.Canary) + ")";

							ImGui::TextUnformatted((IA(I18nKey.SettingsUI.Update.Newer) + channel).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateNew)
					{
						ImGui::SetCursorPos({ settingContentOriginX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, Widgets::FluentColor::WarningBackground);
						ImGui::BeginChild("更新状态-提示", { bottomStatusWidth * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::WarningText);
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Widgets::FluentColor::TextStrong);
							ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Update.New).c_str());

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, Widgets::FluentColor::AccentText);
							ImGui::SameLine();
							ImGui::SetCursorPosX(ImGui::GetCursorPos().x + 10.0f * settingGlobalScale);
							if (ImGui::TextLink(IA(I18nKey.SettingsUI.Update.UpdateNow).c_str()))
							{
								mandatoryUpdate = true;
								AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImGui::SetCursorPos({ bottomActionX * settingGlobalScale, bottomStatusY * settingGlobalScale });
						ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
						if (Widgets::button.Standard(IA(I18nKey.SettingsUI.Update.Check).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
						{
							if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNotStarted)
							{
								QueueInformation(L"The automatic update module has not been activated, which means that you are not using an official release. \nPlease go to the \"version\" page and click \"Fix Software\".\n自动更新模块尚未启动，这意味着您使用的不是官方发布版本。\n请前往“软件版本”页并点击“修复软件”。");
							}
							else AutomaticUpdateState = UpdateObtainInformation;
						}
					}
				}

				if (navigationLayout == Inkeys::UI::Setting::NavigationLayout::Overlay)
				{
					// overlay 最后绘制，保证导航 pane 位于业务 child 与底栏之上。
					ImGui::SetCursorPos({ 0.0F,
						Inkeys::UI::Setting::TitleBarHeightDip * settingGlobalScale });
					ImFluent::PushStyleVar(ImFluentStyleVar_NavPaneOpenWidth, 160.0F);
					ImFluent::PushStyleVar(ImFluentStyleVar_NavPaneCompactWidth, 48.0F);
					ImFluent::BeginSplitView("##setting-responsive-nav", &narrowPaneOpen,
						ImFluentSplitViewDisplayMode_CompactOverlay,
						ImFluentSplitViewPanePlacement_Left,
						160.0F, 48.0F);
					if (ImFluent::BeginSplitViewPane())
					{
						ImFluentNavViewMode mode = narrowPaneOpen
							? ImFluentNavViewMode_LeftOpen : ImFluentNavViewMode_LeftCompact;
						ImFluent::BeginNavigationView("##setting-overlay-nav", &mode);
						renderNavigationItems();
						ImFluent::EndNavigationView();
						narrowPaneOpen = mode == ImFluentNavViewMode_LeftOpen
							&& !navigationActivated;
						ImFluent::EndSplitViewPane();
					}
					ImFluent::EndSplitView();
					ImFluent::PopStyleVar(2);
				}

				{
					if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
					if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
					while (PushFontNum) PushFontNum--, ImGui::PopFont();
				}
				ImGui::End();
			}

			// 渲染
			ImGui::EndFrame();
			ImGui::Render();
			const ImVec4 clearColorValue = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			const float clearColor[4] = {
				clearColorValue.x * clearColorValue.w,
				clearColorValue.y * clearColorValue.w,
				clearColorValue.z * clearColorValue.w,
				clearColorValue.w
			};
			g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
			g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			const HRESULT result = g_pSwapChain->Present(1, 0);
			g_SwapChainOccluded = (result == DXGI_STATUS_OCCLUDED);
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(result))
				settingFrameResult = FrameResult::DeviceLost;
			else if (FAILED(result))
				settingFrameResult = FrameResult::Retry;
			else
				settingFrameResult = FrameResult::Continue;
			co_await suspend_always{};
		}

		//::ShowWindow(setting_window, SW_HIDE);

		// Shutdown 在渲染线程按 backend -> SRV -> presentation 逆序释放。
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		CleanupSettingTextures();
		CleanupSettingTextureCache();
		io.Fonts->Clear();
		ImFluent::ResetContext();
		ImGui::DestroyContext();
		CleanupDeviceD3D();
	settingSessionEpoch = 0;
	#undef CloseProgram
	#undef RestartProgram
	#undef ShellExecuteW
	#undef SetStartupState
	#undef PptComWriteSetting
	#undef WriteSetting
	co_return;
}

namespace
{
	void DrainSettingSessionOnRenderThread() noexcept
	{
		if (!settingSession.Done())
		{
			settingSessionShouldStop.store(true, memory_order_release);
			lock_guard imguiLock(settingImguiMutex);
			settingSession.Resume();
		}
		settingSession.Reset();
		settingSessionEpoch = 0;
		{
			lock_guard lock(settingDrainMutex);
			settingSessionDrained = true;
		}
		settingDrainCondition.notify_all();
	}

	FrameResult RenderSettingFrame(const FrameContext& context)
	{
		Inkeys::UI::Setting::SessionDecision decision;
		Inkeys::UI::Setting::ResizeSnapshot resize;
		uint64_t fontRebuildSerial = 0;
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetOccluded(g_SwapChainOccluded);
			decision = settingSessionState.Resolve(
				context.epoch.generation, !settingSession.Done(),
				g_pSwapChain != nullptr);
			resize = settingSessionState.Resize();
			fontRebuildSerial = settingSessionState.FontRebuildSerial();
			if (decision.consumeBusinessCompletion)
			{
				settingLastBusinessCompletion = settingSessionState.BusinessCompletion();
				settingSessionState.ConsumeBusinessCompletion(
					settingLastBusinessCompletion.serial);
			}
		}

		if (decision.releasePresentation)
		{
			CleanupPresentation();
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ReleasePresentation();
		}

		if (decision.initializeResident)
		{
			settingSessionShouldStop.store(false, memory_order_release);
			settingFrameContext = context;
			settingFrameResult = FrameResult::Retry;
			settingSession = RunSettingSession();
			{
				lock_guard lock(settingDrainMutex);
				settingSessionDrained = false;
			}
			{
				lock_guard imguiLock(settingImguiMutex);
				settingSession.Resume();
			}

			const bool succeeded = !settingSession.Done();
			if (succeeded)
			{
				lock_guard stateLock(settingStateMutex);
				settingSessionState.CommitEpoch(context.epoch.generation);
			}
			else
			{
				settingSession.Reset();
				settingSessionEpoch = 0;
				lock_guard lock(settingDrainMutex);
				settingSessionDrained = true;
				settingDrainCondition.notify_all();
			}
			{
				lock_guard initializeLock(settingInitializeMutex);
				settingInitializeSucceeded = succeeded;
				settingInitializeCompleted = true;
			}
			settingInitializeCondition.notify_all();
			if (!succeeded) return FrameResult::Retry;
		}

		if (decision.rebuildDeviceResources && !settingSession.Done())
		{
			lock_guard imguiLock(settingImguiMutex);
			CleanupPresentation();
			if (ImGui::GetIO().BackendRendererUserData)
				ImGui_ImplDX11_Shutdown();
			CleanupSettingTextures();
			CleanupDeviceD3D();
			const bool acquired = AcquireDeviceLease(context.epoch);
			const bool backendInitialized = acquired
				&& ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
			if (!backendInitialized || !ImGui_ImplDX11_CreateDeviceObjects()
				|| !RecreateSettingTextures())
			{
				// epoch 失败后保持 resident context，但撤销半成品 device 资源供下次重试。
				if (backendInitialized) ImGui_ImplDX11_Shutdown();
				CleanupSettingTextures();
				CleanupDeviceD3D();
				return FrameResult::Retry;
			}
			settingSessionEpoch = context.epoch.generation;
			lock_guard stateLock(settingStateMutex);
			settingSessionState.CommitEpoch(context.epoch.generation);
		}

		if (decision.rebuildFonts && !settingSession.Done())
		{
			lock_guard imguiLock(settingImguiMutex);
			ImGui_ImplDX11_InvalidateDeviceObjects();
			if (!RebuildSettingFonts()
				|| !ImGui_ImplDX11_CreateDeviceObjects())
				return FrameResult::Retry;
			Widgets::style.ApplyGlobal(12.0F);
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ConsumeFontRebuild(fontRebuildSerial);
		}

		const uint64_t themeSerial = settingThemeSerial.load(memory_order_acquire);
		if (!settingSession.Done() && themeSerial != settingConsumedThemeSerial)
		{
			// 主题消息在隐藏态也立即更新 resident style，不等待下一次 Show。
			lock_guard imguiLock(settingImguiMutex);
			ApplySystemTheme(setting_window);
			settingConsumedThemeSerial = themeSerial;
		}

		if (decision.createPresentation && !g_pSwapChain)
		{
			if (!CreatePresentation(setting_window, context.epoch))
				return FrameResult::Retry;
		}

		if (!decision.render || settingSession.Done() || !g_pSwapChain)
			return FrameResult::Idle;

		if (decision.probeOcclusion && g_pSwapChain)
		{
			const HRESULT probeResult = g_pSwapChain->Present(0, DXGI_PRESENT_TEST);
			if (probeResult == DXGI_STATUS_OCCLUDED) return FrameResult::Retry;
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(probeResult))
				return FrameResult::DeviceLost;
			if (FAILED(probeResult)) return FrameResult::Retry;
			g_SwapChainOccluded = false;
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetOccluded(false);
		}

		if (decision.resize && !settingSession.Done() && g_pSwapChain)
		{
			const HRESULT resizeResult = ResizeSwapChain(resize.width, resize.height);
			if (Inkeys::UI::Setting::IsSharedDeviceLoss(resizeResult))
				return FrameResult::DeviceLost;
			if (FAILED(resizeResult))
			{
				CleanupPresentation();
				lock_guard stateLock(settingStateMutex);
				settingSessionState.ReleasePresentation();
				return FrameResult::Retry;
			}
			lock_guard stateLock(settingStateMutex);
			settingSessionState.ConsumeResize(resize.serial);
		}

		settingFrameContext = context;

		{
			lock_guard imguiLock(settingImguiMutex);
			settingSession.Resume();
		}
		if (settingSession.Done())
		{
			settingSession.Reset();
			settingSessionEpoch = 0;
			lock_guard lock(settingDrainMutex);
			settingSessionDrained = true;
			settingDrainCondition.notify_all();
		}
		else
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.CommitEpoch(context.epoch.generation);
		}
		return settingFrameResult;
	}
}

namespace Inkeys::UI::Setting
{
	bool Initialize()
	{
		lock_guard lock(settingLifecycleMutex);
		if (settingInitialized.load(memory_order_acquire)) return true;
		if (!setting_window || !settingBusinessQueue.Start()) return false;
		if (!Inkeys::UI::RenderPipeline::Register(
			Inkeys::UI::RenderPipeline::Client::Settings, RenderSettingFrame))
		{
			settingBusinessQueue.Stop();
			return false;
		}
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(false);
		}
		{
			lock_guard initializeLock(settingInitializeMutex);
			settingInitializeCompleted = false;
			settingInitializeSucceeded = false;
		}
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
		{
			unique_lock initializeLock(settingInitializeMutex);
			settingInitializeCondition.wait(initializeLock,
				[] { return settingInitializeCompleted; });
			if (!settingInitializeSucceeded)
			{
				Inkeys::UI::RenderPipeline::Unregister(
					Inkeys::UI::RenderPipeline::Client::Settings);
				settingBusinessQueue.Stop();
				return false;
			}
		}
		settingInitialized.store(true, memory_order_release);
		return true;
	}

	void Shutdown() noexcept
	{
		unique_lock lifecycleLock(settingLifecycleMutex);
		if (!settingInitialized.exchange(false, memory_order_acq_rel)) return;
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(false);
		}
		settingBusinessQueue.Enqueue({ SettingBusinessKind::HideWindow });
		const bool drainPosted = Inkeys::UI::RenderPipeline::PostControl([]
			{
				DrainSettingSessionOnRenderThread();
				lock_guard stateLock(settingStateMutex);
				settingSessionState.ReleaseResident();
			});
		if (drainPosted)
		{
			unique_lock drainLock(settingDrainMutex);
			settingDrainCondition.wait(drainLock, [] { return settingSessionDrained; });
		}
		else if (IDTLogger)
		{
			IDTLogger->error(
				"[Setting] 渲染管线已停止，无法在线程内排空设置会话");
		}
		Inkeys::UI::RenderPipeline::Unregister(
			Inkeys::UI::RenderPipeline::Client::Settings);
		settingBusinessQueue.Stop();
	}

	void Show()
	{
		if (!settingInitialized.load(memory_order_acquire)) return;
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(true);
		}
		settingBusinessQueue.Enqueue({ SettingBusinessKind::ShowWindow });
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
	}

	void Hide()
	{
		if (!settingInitialized.load(memory_order_acquire)) return;
		{
			lock_guard stateLock(settingStateMutex);
			settingSessionState.SetVisible(false);
		}
		settingBusinessQueue.Enqueue({ SettingBusinessKind::HideWindow });
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::Client::Settings);
	}

	void Toggle()
	{
		if (IsVisible()) Hide();
		else Show();
	}

	bool IsVisible() noexcept
	{
		lock_guard stateLock(settingStateMutex);
		return settingSessionState.IsVisible();
	}

	WNDPROC WindowProc() noexcept
	{
		return ImGuiWndProc;
	}
}

WNDPROC SettingWindowProc() noexcept
{
	return Inkeys::UI::Setting::WindowProc();
}
