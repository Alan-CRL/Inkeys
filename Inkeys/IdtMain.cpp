/*
 * @file		IdtMain.cpp
 * @brief		智绘教项目中心源文件
 * @note		用于初始化智绘教并调用相关模块
 *
 * @envir		MSVC v143 | Windows SDK 10.0.26100
 * @site		https://github.com/Alan-CRL/Inkeys
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

import Inkeys.Helper.CrashHandler;
import Inkeys.UI.Setting;
import Inkeys.UI.Bar;
import Inkeys.UI.Ppt;
import Inkeys.UI.PageControl;
import Inkeys.UI.Whiteboard;
import Inkeys.UI.RenderPipeline;
import Inkeys.Helper.Thread;
import Inkeys.Net.Update;
import Inkeys.Load;
import Inkeys.Other.Gesture;
import Inkeys.Conv.Text;
import Inkeys.Text.Split;
import Inkeys.Text.Font;
import Inkeys.Other.Config;
import Inkeys.Message;
import Inkeys.Window;
import Inkeys.Display;
import Inkeys.UI.MessageBox;
import Inkeys.UI.StartupPreview;
import Inkeys.Startup.Progress;
import Inkeys.Drawing.Draw3.diagnostics;
import Inkeys.Drawing.Draw3.drawing_controller;
import Inkeys.Drawing.Draw3.transparent_presentation;

#include "IdtMain.h"
#include "Inkeys/Helper/FailedCleanupDeadline.h"
#include "Inkeys/Helper/FailedCleanupRealCases.h"
#include "Inkeys/Helper/Ui3PresentationFixtureAuth.h"
#include "Inkeys/UI/Bar/Bar.Presentation.Source.h"
#include "PptSettingsPersistence.h"
#include "resource.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFreezeFrame.h"
#include "IdtGuid.h"
#include "IdtI18n.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtStart.h"
#include "IdtState.h"
#include "IdtTime.h"
#include "Inkeys/Window/Window.Legacy.hpp"
#include "Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.h"
#include "Inkeys/Drawing/Draw3/Draw3.PerformanceProbe.h"
#include "Inkeys/Drawing/Draw3/Draw3.HistoryProbe.h"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Helper/ShutdownSupervisor.h"
#include "Inkeys/Net/UpdatePathSafety.h"
#include "Launch/IdtLaunchState.h"
#include "SuperTop/IdtSuperTop.h"

#include <atomic>
#include <lm.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#pragma comment(lib, "netapi32.lib")

#ifdef MessageBox
#undef MessageBox
#endif

wstring buildTime = __DATE__ L" " __TIME__;		// 构建时间
wstring editionVersion = L"3.0.0-dev.3981";		// 程序发布版本
wstring editionDate = L"3.0.0-20260811a";		// 程序发布日期

wstring userId;									// 用户GUID
wstring globalPath;								// 程序当前路径
wstring pluginPath;								// 数据保存的路径

wstring programArchitecture = L"win32";
wstring targetArchitecture = L"win32";
wstring windowsEdition;

IdtAtomic<int> offSignal;						// 关闭指令
namespace
{
	// PptCOM 会长期持有该地址；不要把原子包装对象强转成 LONG 指针。
	LONG offSignalInterop = 0;
	Inkeys::Display::Subscription displaySubscription;
	std::atomic_bool startupPreviewManualDelayRequested = false;
	constexpr LONGLONG kMaximumUpdateInstructionBytes = 64LL * 1024LL;

	[[nodiscard]] HMODULE LoadSystemLibrary(const wchar_t* fileName) noexcept
	{
		if (!fileName || !*fileName) return nullptr;
		wchar_t systemPath[MAX_PATH]{};
		const UINT length = GetSystemDirectoryW(systemPath, ARRAYSIZE(systemPath));
		if (length == 0 || length >= ARRAYSIZE(systemPath)) return nullptr;
		const bool needsSeparator = systemPath[length - 1] != L'\\';
		const size_t nameLength = wcslen(fileName);
		const size_t required = static_cast<size_t>(length)
			+ (needsSeparator ? 1u : 0u) + nameLength + 1u;
		if (required > ARRAYSIZE(systemPath)) return nullptr;
		std::size_t offset = length;
		if (needsSeparator) systemPath[offset++] = L'\\';
		if (wcscpy_s(systemPath + offset, ARRAYSIZE(systemPath) - offset,
			fileName) != 0) return nullptr;
		// Win7 上可选 DLL 必须从 System32 绝对路径加载，避免应用目录同名 DLL 劫持。
		return LoadLibraryW(systemPath);
	}

	[[nodiscard]] bool EnsureProcessDpiAwareness() noexcept
	{
		using SetAwareness = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
		HMODULE shcore = LoadSystemLibrary(L"Shcore.dll");
		if (shcore)
		{
			const auto setAwareness = reinterpret_cast<SetAwareness>(
				GetProcAddress(shcore, "SetProcessDpiAwareness"));
			if (setAwareness)
			{
				const HRESULT result = setAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
				FreeLibrary(shcore);
				// E_ACCESSDENIED 表示 manifest 或更早调用已完成配置。
				if (SUCCEEDED(result) || result == E_ACCESSDENIED) return true;
			}
			else FreeLibrary(shcore);
		}
		return SetProcessDPIAware() != FALSE
			|| GetLastError() == ERROR_ACCESS_DENIED;
	}

	void ShowStartupMessage(const wchar_t* body) noexcept
	{
		if (Inkeys::Shutdown::IsAuthorizedStartupFailureChild())
		{
			const auto failure = Inkeys::Startup::ActiveSnapshot();
			if (failure.failed)
				Inkeys::Shutdown::HoldAuthorizedStartupBoundary(failure.failureCode, 1);
		}
		auto request = Inkeys::UI::MessageBox::MakeOkRequest(
			L"Inkeys Tips", body);
		request.ownerlessTopmostAtCreation = true;
		request.fallback.modality =
			Inkeys::UI::MessageBox::SystemModality::System;
		(void)Inkeys::UI::MessageBox::Show(request);
	}

	void ReportStartupMilestoneForManualTest(
		Inkeys::Startup::Milestone milestone) noexcept
	{
		if (!Inkeys::Startup::Report(milestone)) return;
		wchar_t enabled[2]{};
		if (!Inkeys::UI::StartupPreview::IsActive()
			|| (!startupPreviewManualDelayRequested.load(std::memory_order_acquire)
				&& (GetEnvironmentVariableW(
					L"INKEYS_STARTUP_PREVIEW_MANUAL_DELAY", enabled, 2) == 0
					|| enabled[0] != L'1'))) return;
		static bool firstVisibleMilestone = true;
		const auto delay = firstVisibleMilestone
			? std::chrono::milliseconds(3200)
			: std::chrono::milliseconds(600);
		firstVisibleMilestone = false;
		// 仅显式人工测试时放慢阶段，普通启动和渲染回调都不得等待。
		std::this_thread::sleep_for(delay);
	}

	void PublishFatalStartupFailure(
		std::uint32_t code, const wchar_t* message) noexcept
	{
		// 已确认无法启动时先建立退场监督，红帧、提示与清理都不得挡住15秒截止。
		SetOffSignal(1);
		(void)Inkeys::Startup::ReportFailure(code);
		constexpr auto failureFrameBudget = std::chrono::milliseconds(350);
		const auto failureFrameDeadline = std::chrono::steady_clock::now()
			+ failureFrameBudget;
		Inkeys::UI::StartupPreview::RequestFailureFrame();
		if (Inkeys::UI::StartupPreview::WaitForFailureFrame(failureFrameBudget))
		{
			// 红帧较早提交时用完剩余预算，避免只显示一个渲染帧便被销毁。
			std::this_thread::sleep_until(failureFrameDeadline);
		}
		ShowStartupMessage(message);
		// 对话框确认后才淡出 Preview；无 Preview 时两个调用都会立即降级返回。
		Inkeys::UI::StartupPreview::RequestFadeOutForExit();
		(void)Inkeys::UI::StartupPreview::WaitForFadeOut(
			std::chrono::milliseconds(500));
		Inkeys::UI::StartupPreview::Stop();
	}

	bool WriteStartupPreviewSmokeReport(const std::wstring& path, bool passed,
		const Inkeys::UI::StartupPreview::Diagnostics& preview,
		const Inkeys::UI::Bar::PresentationAlphaDiagnostics& alpha) noexcept
	{
		try
		{
			if (path.empty()) return false;
			const std::string report = std::string(passed ? "PASS\r\n" : "FAIL\r\n")
			+ "totalWidthDip=" + std::to_string(preview.totalWidthDip) + "\r\n"
			+ "previewFirstAlpha0Committed=" + (preview.firstFrameCommitted ? "1\r\n" : "0\r\n")
			+ "previewFadeOutCommitted=" + (preview.previewFadeOutCommitted ? "1\r\n" : "0\r\n")
			+ "barAlpha0Committed=" + (preview.barAlpha0Committed
				&& alpha.transparentCommitted ? "1\r\n" : "0\r\n")
			+ "barAlpha255Committed=" + (preview.barAlpha255Committed
				&& alpha.opaqueCommitted ? "1\r\n" : "0\r\n")
			+ "automaticStopPosted=" + (preview.automaticStopPosted ? "1\r\n" : "0\r\n")
			+ "ownerThreadExited=" + (preview.ownerThreadExited ? "1\r\n" : "0\r\n")
			+ "previewInactive=" + (preview.previewInactive ? "1\r\n" : "0\r\n")
			+ "recovery=" + (passed ? "visible\r\n"
				: (!preview.firstFrameCommitted ? "bypassed\r\n" : "fatal\r\n"))
			+ "requestedAlpha=" + std::to_string(alpha.requested) + "\r\n"
			+ "committedAlpha=" + std::to_string(alpha.committed) + "\r\n";
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		DWORD written = 0;
		const bool succeeded = WriteFile(file, report.data(),
			static_cast<DWORD>(report.size()), &written, nullptr) != FALSE
			&& written == report.size() && FlushFileBuffers(file) != FALSE;
		CloseHandle(file);
		return succeeded;
		}
		catch (...)
		{
			return false;
		}
	}

#ifndef IDT_RELEASE
	void InitializeDebugConsole()
	{
		static std::once_flag consoleOnce;
		std::call_once(consoleOnce, []
			{
				const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
				const HANDLE errorHandle = GetStdHandle(STD_ERROR_HANDLE);
				const auto isRedirected = [](HANDLE handle)
					{
						if (!handle || handle == INVALID_HANDLE_VALUE) return false;
						const DWORD type = GetFileType(handle);
						return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
					};
				const bool outputRedirected = isRedirected(outputHandle);
				const bool errorRedirected = isRedirected(errorHandle);
				// 保留采集脚本的文件/管道；两路均重定向时不创建交互控制台。
				if (!outputRedirected || !errorRedirected)
				{
					AllocConsole();
					FILE* fp;
					if (outputRedirected) SetStdHandle(STD_OUTPUT_HANDLE, outputHandle);
					else freopen_s(&fp, "CONOUT$", "w", stdout);
					if (errorRedirected) SetStdHandle(STD_ERROR_HANDLE, errorHandle);
					else freopen_s(&fp, "CONOUT$", "w", stderr);
					freopen_s(&fp, "CONIN$", "r", stdin);
				}
				std::ios::sync_with_stdio(true);
				std::wcout.clear();
				std::wcin.clear();
				std::wcerr.clear();
				std::cout.clear();
				std::cin.clear();
				std::cerr.clear();
				std::wcout.imbue(std::locale("chs"));
			});
	}
#endif
}

void SetOffSignal(int signal)
{
	if (signal != 1 && signal != 2) return;
	// 首次请求决定关闭或重启；先建立独立监督，再进入任何可能卡住的清理。
	if (InterlockedCompareExchange(&offSignalInterop,
		static_cast<LONG>(signal), 0) != 0) return;
	Inkeys::Window::GetService().BeginShutdown();
	const auto supervisor = Inkeys::Shutdown::ArmShutdownSupervisor(
		signal == 2 ? Inkeys::Shutdown::Intent::Restart
			: Inkeys::Shutdown::Intent::Close);
	const DWORD supervisorError = GetLastError();
	offSignal.store(signal, std::memory_order_release);
	if (supervisor == Inkeys::Shutdown::ArmResult::Failed)
	{
		// 首次普通owner的合法Arm已双失败，当前线程接管原截止，先于任何隐藏/日志/清理等待。
		Inkeys::UI::RenderPipeline::WakeForStop();
		Inkeys::Shutdown::EnforceFailedShutdownDeadline(
			signal == 2 ? Inkeys::Shutdown::Intent::Restart
				: Inkeys::Shutdown::Intent::Close);
	}
	(void)Inkeys::Window::GetService().RequestHideAllUserWindows();
	// 退出标志与调度器休眠事件必须同时发布，不能依赖 Bar 线程代为唤醒。
	Inkeys::UI::RenderPipeline::WakeForStop();
	if (supervisor == Inkeys::Shutdown::ArmResult::FallbackArmed && IDTLogger)
		IDTLogger->warn("[退出监督] 外部监督未建立；自身15秒退场已启动，不能保证重启，错误码={}",
			supervisorError);
	else if (supervisor != Inkeys::Shutdown::ArmResult::Armed && IDTLogger)
		IDTLogger->error("[退出监督] 15 秒保护未建立，错误码={}", supervisorError);
	// 受控退出清理期异常不应再由未处理异常过滤器误判为普通崩溃。
	CrashHandler::Shutdown();
	StopMagnifierCoordinator();
}

namespace Inkeys::Shutdown
{
	ULONGLONG PublishFatalFailedCleanupNoWait() noexcept
	{
		// 失败清理owner已能自守截止；不能在这里重新建线程、Arm、日志或等待业务清理。
		LONG intent = InterlockedCompareExchange(&offSignalInterop, 1, 0);
		if (intent == 0) intent = 1;
		Inkeys::Window::GetService().BeginShutdown();
		if (intent == 1 || intent == 2)
			offSignal.store(static_cast<int>(intent), std::memory_order_release);
		Inkeys::UI::RenderPipeline::WakeForStop();
		return PublishedShutdownDeadlineTick();
	}
}

LONG* GetOffSignalInteropPointer()
{
	return &offSignalInterop;
}

shared_ptr<spdlog::logger> IDTLogger;
IdtAtomic<bool> useMouseInput;

using namespace Inkeys;

namespace
{
	bool PublishEmbeddedResourceFileAtomically(const std::wstring& path,
		LPCWSTR type, LPCWSTR name) noexcept
	{
		const HRSRC resource = FindResourceW(nullptr, name, type);
		if (!resource) return false;
		const DWORD size = SizeofResource(nullptr, resource);
		const HGLOBAL loaded = LoadResource(nullptr, resource);
		const auto* bytes = loaded
			? static_cast<const char*>(LockResource(loaded)) : nullptr;
		if (!bytes || !size) return false;
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES)
		{
			if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
				return false;
		}
		else
		{
			const DWORD error = GetLastError();
			if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
				return false;
		}
		try
		{
			// 同目录新临时文件完整写入后再发布，失败不截断原 DLL。
			return Inkeys::PptSettings::WriteAtomically(path,
				std::string(bytes, size));
		}
		catch (...) { return false; }
	}

	HANDLE OpenVerifiedEmbeddedResourceFile(const std::wstring& path,
		LPCWSTR type, LPCWSTR name) noexcept
	{
		const HRSRC resource = FindResourceW(nullptr, name, type);
		if (!resource) return INVALID_HANDLE_VALUE;
		const DWORD expectedSize = SizeofResource(nullptr, resource);
		const HGLOBAL loaded = LoadResource(nullptr, resource);
		const auto* expected = loaded
			? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
		if (!expected || !expectedSize) return INVALID_HANDLE_VALUE;
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES ||
			(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			return INVALID_HANDLE_VALUE;
		// 验证时独占写入/删除共享权，并保持句柄到 LoadLibrary 返回。
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (file == INVALID_HANDLE_VALUE) return file;
		BY_HANDLE_FILE_INFORMATION info = {};
		LARGE_INTEGER actualSize = {};
		if (!GetFileInformationByHandle(file, &info) ||
			(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
			!GetFileSizeEx(file, &actualSize) || actualSize.QuadPart != expectedSize)
		{
			CloseHandle(file);
			return INVALID_HANDLE_VALUE;
		}
		std::array<unsigned char, 64 * 1024> buffer = {};
		DWORD offset = 0;
		while (offset < expectedSize)
		{
			const DWORD count = (std::min)(static_cast<DWORD>(buffer.size()),
				expectedSize - offset);
			DWORD read = 0;
			if (!ReadFile(file, buffer.data(), count, &read, nullptr) ||
				read != count || std::memcmp(buffer.data(), expected + offset, count) != 0)
			{
				CloseHandle(file);
				return INVALID_HANDLE_VALUE;
			}
			offset += count;
		}
		return file;
	}

	HANDLE EnsureVerifiedEmbeddedResourceFile(const std::wstring& path,
		LPCWSTR type, LPCWSTR name) noexcept
	{
		// 正确的只读文件可直接复用；验证获得的 read pin 仍须持有到 LoadLibrary 返回。
		const HANDLE verified = OpenVerifiedEmbeddedResourceFile(path, type, name);
		if (verified != INVALID_HANDLE_VALUE) return verified;
		if (!PublishEmbeddedResourceFileAtomically(path, type, name))
			return INVALID_HANDLE_VALUE;
		return OpenVerifiedEmbeddedResourceFile(path, type, name);
	}

	LONG WINAPI CrashFilterLifecycleSentinel(EXCEPTION_POINTERS*)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}

	int RunCrashFilterLifecycleTest() noexcept
	{
		// 仅显式无窗口测试进程触碰 UEF；退出前恢复进入测试时的原处理器。
		const auto original = SetUnhandledExceptionFilter(nullptr);
		CrashHandler::Initialize();
		CrashHandler::Initialize();
		const auto installed = SetUnhandledExceptionFilter(nullptr);
		SetUnhandledExceptionFilter(installed);
		CrashHandler::Shutdown();
		CrashHandler::Shutdown();
		const auto afterNull = SetUnhandledExceptionFilter(CrashFilterLifecycleSentinel);
		if (!installed || afterNull != nullptr)
		{
			SetUnhandledExceptionFilter(original);
			std::fputs("[CrashFilter] FAIL: null previous filter was not restored\n", stderr);
			return 1;
		}

		CrashHandler::Initialize();
		const auto installedWithSentinel = SetUnhandledExceptionFilter(nullptr);
		SetUnhandledExceptionFilter(installedWithSentinel);
		CrashHandler::Shutdown();
		const auto afterSentinel = SetUnhandledExceptionFilter(original);
		if (!installedWithSentinel ||
			installedWithSentinel == CrashFilterLifecycleSentinel ||
			afterSentinel != CrashFilterLifecycleSentinel)
		{
			std::fputs("[CrashFilter] FAIL: non-null previous filter was not restored\n", stderr);
			return 1;
		}
		std::fputs("[CrashFilter] PASS: install and shutdown restore both previous filters\n", stderr);
		return 0;
	}

	int RunConfigBoundaryTest() noexcept
	{
		const char* stage = "setup";
		try
		{
			// 仅在仓库忽略目录放隔离配置，精确参数返回后绝不进入产品启动链。
			const auto root = std::filesystem::current_path() /
				"TestResults" / "release-hardening" /
				("config-boundary-" + std::to_string(GetCurrentProcessId()));
			std::filesystem::create_directories(root / "opt");
			std::filesystem::create_directories(root / "Inkeys" / "Config");
			globalPath = root.wstring() + L"\\";
			const auto deploy = root / "opt" / "deploy.json";
			const auto pptConfig = root / "opt" / "pptcom_configuration.json";
			const auto mainConfig = root / "Inkeys" / "Config" / "main.json";
			const auto writeBytes = [](const std::filesystem::path& path,
				const std::string& bytes)
			{
				std::ofstream file(path, std::ios::binary | std::ios::trunc);
				file.write(bytes.data(), bytes.size());
				return file.good();
			};
			const auto readBytes = [](const std::filesystem::path& path)
			{
				std::ifstream file(path, std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(file),
					std::istreambuf_iterator<char>());
			};
			if (!writeBytes(deploy, "{broken") ||
				!writeBytes(pptConfig, "{broken") ||
				!writeBytes(mainConfig, "{broken")) return 1;
			stage = "legacy malformed";
			const bool badDeployRead = ReadSetting();
			const bool badDeployWrite = WriteSetting();
			stage = "ppt malformed";
			const bool badPptRead = PptComReadSetting();
			const bool badPptWrite = PptComWriteSetting();
			stage = "main malformed";
			Inkeys::Config isolatedConfig;
			const bool badMainRead = isolatedConfig.ReadAll();
			const bool badMainWrite = isolatedConfig.Write();
			const bool badPreserved = !badDeployRead && !badDeployWrite &&
				!badPptRead && !badPptWrite &&
				!badMainRead && !badMainWrite &&
				readBytes(deploy) == "{broken" &&
				readBytes(pptConfig) == "{broken" &&
				readBytes(mainConfig) == "{broken";
			if (!badPreserved)
			{
				std::fputs("[ConfigBoundary] FAIL: malformed existing file was overwritten\n", stderr);
				return 1;
			}
			for (const char* nonObject : { "[]", "1", "\"x\"" })
			{
				Inkeys::Config nonObjectConfig;
				if (!writeBytes(deploy, nonObject)) return 1;
				stage = "non-object deploy read";
				const bool deployRead = ReadSetting();
				stage = "non-object deploy write";
				const bool deployWrite = WriteSetting();
				if (!writeBytes(pptConfig, nonObject)) return 1;
				stage = "non-object ppt read";
				const bool pptRead = PptComReadSetting();
				stage = "non-object ppt write";
				const bool pptWrite = PptComWriteSetting();
				if (!writeBytes(mainConfig, nonObject)) return 1;
				stage = "non-object main read";
				const bool mainRead = nonObjectConfig.ReadAll();
				stage = "non-object main write";
				const bool mainWrite = nonObjectConfig.Write();
				if (deployRead || deployWrite || readBytes(deploy) != nonObject ||
					pptRead || pptWrite || readBytes(pptConfig) != nonObject ||
					mainRead || mainWrite || readBytes(mainConfig) != nonObject)
				{
					std::fputs("[ConfigBoundary] FAIL: non-object JSON was accepted\n", stderr);
					return 1;
				}
			}
			std::string oversized = "{}";
			stage = "oversized";
			oversized.resize(16 * 1024 * 1024 + 1, ' ');
			if (!writeBytes(deploy, oversized) || ReadSetting())
			{
				std::fputs("[ConfigBoundary] FAIL: oversized JSON was accepted\n", stderr);
				return 1;
			}
			stage = "deeply nested";
			const std::string deeplyNested = "{\"unknown\":" +
				std::string(80, '[') + "0" + std::string(80, ']') + "}";
			Inkeys::Config deepConfig;
			if (!writeBytes(deploy, deeplyNested) || ReadSetting() ||
				WriteSetting() || readBytes(deploy) != deeplyNested ||
				!writeBytes(mainConfig, deeplyNested) || deepConfig.ReadAll() ||
				deepConfig.Write() || readBytes(mainConfig) != deeplyNested)
			{
				std::fputs("[ConfigBoundary] FAIL: deeply nested JSON replaced the last file\n", stderr);
				return 1;
			}
			stage = "near limit";
			std::string nearLimit = "{\"unknown\":\"";
			nearLimit.append(16 * 1024 * 1024 - 128, 'x');
			nearLimit += "\"}";
			Inkeys::Config nearLimitConfig;
			if (!writeBytes(mainConfig, nearLimit) ||
				!nearLimitConfig.ReadAll() || nearLimitConfig.Write() ||
				readBytes(mainConfig) != nearLimit)
			{
				std::fputs("[ConfigBoundary] FAIL: expanded JSON replaced the last readable file\n", stderr);
				return 1;
			}
			stage = "fresh file";
			globalPath = (root / "fresh").wstring() + L"\\";
			std::filesystem::create_directories(root / "fresh" / "opt");
			const bool freshCreated = WriteSetting() &&
				std::filesystem::exists(root / "fresh" / "opt" / "deploy.json");
			if (!freshCreated)
			{
				std::fputs("[ConfigBoundary] FAIL: first-run file creation was blocked\n", stderr);
				return 1;
			}
			std::fputs("[ConfigBoundary] PASS: bounded reads preserve bad files and first-run creation\n", stderr);
			return 0;
		}
		catch (...)
		{
			std::fprintf(stderr,
				"[ConfigBoundary] FAIL: isolated test exception at %s\n", stage);
			return 1;
		}
	}

	int RunDdbDisabledPreservationTest() noexcept
	{
		try
		{
			const auto root = std::filesystem::current_path() /
				"TestResults" / "release-hardening" /
				("ddb-disabled-" + std::to_string(GetCurrentProcessId()));
			const auto directory = root / "DesktopDrawpadBlocker";
			std::filesystem::create_directories(directory);
			const auto sentinel = directory / "user-data.txt";
			std::ofstream file(sentinel, std::ios::binary | std::ios::trunc);
			file << "preserve";
			file.close();
			pluginPath = root.wstring() + L"\\";
			ddbInteractionSetList.enable = false;
			StartDesktopDrawpadBlocker();
			std::ifstream after(sentinel, std::ios::binary);
			const std::string content(std::istreambuf_iterator<char>{ after },
				std::istreambuf_iterator<char>{});
			if (content != "preserve")
			{
				std::fputs("[DdbDisabled] FAIL: unknown file was removed\n", stderr);
				return 1;
			}
			std::fputs("[DdbDisabled] PASS: unknown directory contents preserved\n", stderr);
			return 0;
		}
		catch (...)
		{
			std::fputs("[DdbDisabled] FAIL: isolated test exception\n", stderr);
			return 1;
		}
	}

	int RunPptComResourceVerificationTest() noexcept
	{
		try
		{
			const auto root = std::filesystem::current_path() /
				"TestResults" / "release-hardening" /
				("pptcom-resource-" + std::to_string(GetCurrentProcessId()));
			std::filesystem::create_directories(root);
			const auto file = root / "PptCOM.dll";
			if (!PublishEmbeddedResourceFileAtomically(file.wstring(),
				L"DLL", MAKEINTRESOURCE(222))) return 1;
			const HANDLE verified = OpenVerifiedEmbeddedResourceFile(
				file.wstring(), L"DLL", MAKEINTRESOURCE(222));
			if (verified == INVALID_HANDLE_VALUE) return 1;
			const HANDLE writer = CreateFileW(file.c_str(), GENERIC_WRITE,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const bool writeBlocked = writer == INVALID_HANDLE_VALUE;
			if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
			ACTCTX actCtx = {};
			actCtx.cbSize = sizeof(actCtx);
			actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
			actCtx.lpResourceName = MAKEINTRESOURCE(221);
			actCtx.hModule = GetModuleHandleW(nullptr);
			const HANDLE activation = CreateActCtxW(&actCtx);
			ULONG_PTR cookie = 0;
			const bool active = activation != INVALID_HANDLE_VALUE &&
				ActivateActCtx(activation, &cookie) != FALSE;
			const HMODULE loaded = active ? LoadLibraryW(file.c_str()) : nullptr;
			if (loaded) FreeLibrary(loaded);
			if (active) DeactivateActCtx(0, cookie);
			if (activation != INVALID_HANDLE_VALUE) ReleaseActCtx(activation);
			CloseHandle(verified);
			const HANDLE publicationLock = CreateFileW(file.c_str(), GENERIC_READ,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const bool atomicFailurePreserved = publicationLock != INVALID_HANDLE_VALUE &&
				!PublishEmbeddedResourceFileAtomically(file.wstring(),
					L"DLL", MAKEINTRESOURCE(222));
			if (publicationLock != INVALID_HANDLE_VALUE) CloseHandle(publicationLock);
			const HANDLE stillVerified = OpenVerifiedEmbeddedResourceFile(
				file.wstring(), L"DLL", MAKEINTRESOURCE(222));
			if (stillVerified != INVALID_HANDLE_VALUE) CloseHandle(stillVerified);
			const std::wstring resourcePath = file.wstring();
			const DWORD originalAttributes = GetFileAttributesW(file.c_str());
			// 仅本测试新建的私有文件设只读；生产助手不修改 attributes。
			const bool correctReadonlySet = originalAttributes != INVALID_FILE_ATTRIBUTES &&
				SetFileAttributesW(file.c_str(), originalAttributes | FILE_ATTRIBUTE_READONLY) != FALSE;
			const HANDLE correctReadonly = correctReadonlySet
				? EnsureVerifiedEmbeddedResourceFile(resourcePath, L"DLL", MAKEINTRESOURCE(222))
				: INVALID_HANDLE_VALUE;
			BY_HANDLE_FILE_INFORMATION readonlyInfo = {};
			const bool correctReadonlyVerified = correctReadonly != INVALID_HANDLE_VALUE &&
				GetFileInformationByHandle(correctReadonly, &readonlyInfo) != FALSE &&
				(readonlyInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
			if (correctReadonly != INVALID_HANDLE_VALUE) CloseHandle(correctReadonly);
			const bool correctReadonlyRestored = correctReadonlySet &&
				SetFileAttributesW(file.c_str(), originalAttributes) != FALSE;
			std::ofstream corrupted(file, std::ios::binary | std::ios::trunc);
			corrupted << "untrusted";
			corrupted.close();
			const HANDLE rejected = OpenVerifiedEmbeddedResourceFile(
				file.wstring(), L"DLL", MAKEINTRESOURCE(222));
			if (rejected != INVALID_HANDLE_VALUE) CloseHandle(rejected);
			const bool wrongReadonlySet = originalAttributes != INVALID_FILE_ATTRIBUTES &&
				SetFileAttributesW(file.c_str(), originalAttributes | FILE_ATTRIBUTE_READONLY) != FALSE;
			const HANDLE wrongReadonly = wrongReadonlySet
				? EnsureVerifiedEmbeddedResourceFile(resourcePath, L"DLL", MAKEINTRESOURCE(222))
				: INVALID_HANDLE_VALUE;
			if (wrongReadonly != INVALID_HANDLE_VALUE) CloseHandle(wrongReadonly);
			const HANDLE preservedFile = CreateFileW(file.c_str(), GENERIC_READ,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			std::array<char, 9> preservedBytes = {};
			DWORD preservedCount = 0;
			LARGE_INTEGER preservedSize = {};
			const bool wrongReadonlyPreserved = wrongReadonlySet && wrongReadonly == INVALID_HANDLE_VALUE &&
				preservedFile != INVALID_HANDLE_VALUE && GetFileSizeEx(preservedFile, &preservedSize) != FALSE &&
				preservedSize.QuadPart == static_cast<LONGLONG>(preservedBytes.size()) &&
				ReadFile(preservedFile, preservedBytes.data(), static_cast<DWORD>(preservedBytes.size()), &preservedCount, nullptr) != FALSE &&
				preservedCount == preservedBytes.size() && std::memcmp(preservedBytes.data(), "untrusted", preservedBytes.size()) == 0;
			if (preservedFile != INVALID_HANDLE_VALUE) CloseHandle(preservedFile);
			const bool wrongReadonlyRestored = wrongReadonlySet &&
				SetFileAttributesW(file.c_str(), originalAttributes) != FALSE;
			if (!writeBlocked || !loaded || !atomicFailurePreserved ||
				stillVerified == INVALID_HANDLE_VALUE ||
				rejected != INVALID_HANDLE_VALUE)
			{
				std::fputs("[PptComResource] FAIL: load, corruption or concurrent writer contract\n", stderr);
				return 1;
			}
			if (!correctReadonlyVerified || !correctReadonlyRestored || !wrongReadonlyPreserved || !wrongReadonlyRestored)
			{
				std::fprintf(stderr, "[PptComResource] FAIL: readonly correct=%d restored=%d wrong_preserved=%d restored=%d\n",
					correctReadonlyVerified, correctReadonlyRestored, wrongReadonlyPreserved, wrongReadonlyRestored);
				return 1;
			}
			std::fputs("[PptComResource] PASS: embedded bytes verified under read lock; correct readonly reuse and wrong readonly preservation\n", stderr);
			return 0;
		}
		catch (...)
		{
			std::fputs("[PptComResource] FAIL: isolated test exception\n", stderr);
			return 1;
		}
	}

	template <typename StartWindows>
	bool StartDraw3ProductWithFallback(Inkeys::Window::Service& windowService,
		std::vector<Inkeys::Window::WindowSpec>& windowSpecs, HWND& drawpad_window,
		Inkeys::Drawing::Draw3::HostStyleCallbacks draw3StyleCallbacks,
		Inkeys::Drawing::Draw3::HostStartOptions& draw3StartOptions,
		Inkeys::Drawing::Draw3::HostRuntimeCallbacks draw3RuntimeCallbacks,
		bool preferDraw3DirectComposition, StartWindows&& StartWindowService,
		Inkeys::Shutdown::CleanupRealPacket* observation = nullptr)
	{
		Inkeys::Shutdown::FailedCleanupDeadline firstHostCleanup(
			observation ? &Inkeys::Shutdown::PublishAuthorizedCleanupFatal
				: &Inkeys::Shutdown::PublishFatalFailedCleanupNoWait);
		firstHostCleanup.PrepareOrFatal();
		const auto firstHostFailure = firstHostCleanup.Signal();
		draw3StartOptions.failedCleanup = firstHostFailure;
		bool draw3Started = Inkeys::Drawing::Draw3::StartProduct(
			drawpad_window, windowService.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation),
			draw3StyleCallbacks, draw3StartOptions, draw3RuntimeCallbacks);
		if (!draw3Started && preferDraw3DirectComposition)
		{
			// 同一失败episode覆盖Host内部清理、失败日志和旧Window owner join。
			const ULONGLONG firstGrace = firstHostFailure.BeginKnownFailure();
			if (observation)
			{
				observation->trace.firstStartFalseTick = GetTickCount64();
				observation->trace.graceDeadline = firstGrace;
			}
			// NOREDIRECTIONBITMAP 在绑定 DComp 后不可清除；显示前顺序重建唯一 HWND 链再走 legacy fallback。
			if (!observation) IDTLogger->warn("[主线程][IdtMain] Draw3 DComp 初始化失败，重建隐藏窗口链并回退 ULW");
			const HWND oldDrawpad = observation ? drawpad_window : nullptr;
			const HWND oldPresentation = observation ? windowService.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation) : nullptr;
			Inkeys::Drawing::Draw3::StopProduct();
			windowService.StopAndJoin();
			firstHostCleanup.CompleteOrFatal(); // 旧scope必须真取消/join，下一普通初始化不继承其时钟。
			if (observation)
			{
				observation->trace.oldJoinTick = GetTickCount64();
				InterlockedExchange(&observation->trace.oldSignalCancelled, firstHostFailure.BeginKnownFailure() == 0 ? 1 : 0);
				InterlockedExchange(&observation->trace.oldChainDestroyed,
					!Inkeys::Drawing::Draw3::ProductRunning() && !IsWindow(oldDrawpad) && !IsWindow(oldPresentation) ? 1 : 0);
			}
			for (auto& spec : windowSpecs)
			{
				if (spec.role != Inkeys::Window::WindowRole::Drawpad) continue;
				spec.exStyle &= ~(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED);
				break;
			}
			if (InterlockedCompareExchange(&offSignalInterop, 0, 0) == 0 && StartWindowService())
			{
				Inkeys::Shutdown::FailedCleanupDeadline ulwHostCleanup(
					observation ? &Inkeys::Shutdown::PublishAuthorizedCleanupFatal
				: &Inkeys::Shutdown::PublishFatalFailedCleanupNoWait);
				ulwHostCleanup.PrepareOrFatal();
				draw3StartOptions.failedCleanup = ulwHostCleanup.Signal();
				draw3StartOptions.allowDirectComposition = false;
				draw3Started = Inkeys::Drawing::Draw3::StartProduct(
					drawpad_window, windowService.Handle(
						Inkeys::Window::WindowRole::DrawpadPresentation),
					draw3StyleCallbacks, draw3StartOptions, draw3RuntimeCallbacks);
				if (!draw3Started) (void)draw3StartOptions.failedCleanup.BeginKnownFailure();
				ulwHostCleanup.CompleteOrFatal();
			}
		}
		else
		{
			if (!draw3Started) (void)firstHostFailure.BeginKnownFailure();
			firstHostCleanup.CompleteOrFatal();
		}
		return draw3Started;
	}
}

namespace Inkeys::Shutdown
{
	int RunMainFailedCleanupUlwCounterexample(const VerifiedCleanupRealLaunch& launch) noexcept
	{
		if (!IsAuthorizedCleanupRealLaunch(launch) || launch.scenario != CleanupRealCase::MainUlw) return 84;
		using namespace Inkeys::Drawing::Draw3;
		using Role = Inkeys::Window::WindowRole;
		struct State
		{
			Inkeys::Window::Service* service = nullptr;
			CleanupRealPacket* packet = nullptr;
			std::atomic<unsigned> generation{ 0 }, styleRejected{ 0 }, graphicsReady{ 0 };
			std::array<std::atomic<unsigned>, 4> created{}, destroyed{};
			std::array<std::atomic<HWND>, 4> windows{};
		};
		std::shared_ptr<State> state;
		std::array<HANDLE, 4> oldOwners{};
		auto& packet = *launch.packet;
		auto& service = Inkeys::Window::GetService();
		const auto fail = [&](DWORD error) noexcept
		{
			InterlockedExchange(&packet.header.error, error);
			InterlockedExchange(&packet.header.result, 2);
			PublishCleanupRealStage(packet, CleanupRealStage::PrerequisiteFailed);
			SetOffSignal(1); // 错误清理也先走正式15秒保护，不返回释放仍活owner的context。
			StopProduct();
			service.StopAndJoin();
			for (HANDLE owner : oldOwners) if (owner) CloseHandle(owner);
			oldOwners.fill(nullptr);
			return static_cast<int>(error);
		};
		try
		{
			if (!ShouldPreconfigureNoRedirectionBitmap()) return fail(91);
			state = std::make_shared<State>();
			state->service = &service;
			state->packet = &packet;
			std::vector<Inkeys::Window::WindowSpec> specs;
			constexpr std::array<Role, 4> roles{ Role::MagnifierHost, Role::Freeze, Role::DrawpadPresentation, Role::Drawpad };
			for (unsigned i = 0; i != roles.size(); ++i)
			{
				Inkeys::Window::WindowSpec spec;
				spec.role = roles[i];
				spec.className = L"Inkeys.Cleanup.Main." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(i);
				spec.title = L"Inkeys owned Main ULW counterexample";
				spec.x = spec.y = -32000;
				spec.width = 320; spec.height = 240;
				spec.style = WS_POPUP | WS_CLIPCHILDREN;
				spec.exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
				if (roles[i] == Role::Drawpad) spec.exStyle |= WS_EX_NOREDIRECTIONBITMAP;
				else spec.exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
				spec.windowProc = roles[i] == Role::Drawpad ? DrawpadMsgCallback : DefWindowProcW;
				spec.bindMessages = false;
				spec.visible = false;
				spec.created = [state, i](HWND window)
				{
					state->windows[i].store(window, std::memory_order_release);
					state->created[i].fetch_add(1, std::memory_order_acq_rel);
					if (state->generation.load(std::memory_order_acquire) == 1)
						state->packet->trace.ownerThreadIds[i] = GetCurrentThreadId();
				};
				spec.destroyed = [state, i]
				{
					state->destroyed[i].fetch_add(1, std::memory_order_acq_rel);
					state->windows[i].store(nullptr, std::memory_order_release);
				};
				specs.push_back(std::move(spec));
			}
			HWND drawpad = nullptr;
			const auto startWindows = [&]
			{
				const unsigned generation = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
				FailedCleanupDeadline scope(&PublishAuthorizedCleanupFatal);
				scope.PrepareOrFatal();
				const bool started = service.Start(specs, scope.Signal());
				scope.CompleteOrFatal();
				if (!started) return false;
				drawpad = service.Handle(Role::Drawpad);
				const HWND presentation = service.Handle(Role::DrawpadPresentation);
				for (unsigned i = 0; i != roles.size(); ++i)
				{
					const HWND window = service.Handle(roles[i]);
					DWORD pid = 0;
					const DWORD tid = GetWindowThreadProcessId(window, &pid);
					if (!window || !tid || pid != GetCurrentProcessId() || IsWindowVisible(window)) return false;
					if (generation == 1)
					{
						oldOwners[i] = OpenThread(SYNCHRONIZE, FALSE, tid);
						if (!oldOwners[i]) return false;
					}
				}
				if (generation == 1)
				{
					packet.trace.oldGeneration = generation;
					packet.trace.oldDrawpad = reinterpret_cast<ULONG_PTR>(drawpad);
					packet.trace.oldPresentation = reinterpret_cast<ULONG_PTR>(presentation);
				}
				else
				{
					for (unsigned i = 0; i != roles.size(); ++i)
						if (WaitForSingleObject(oldOwners[i], 0) != WAIT_OBJECT_0
							|| state->created[i].load(std::memory_order_acquire) != 2
							|| state->destroyed[i].load(std::memory_order_acquire) != 1) return false;
					packet.trace.newGeneration = generation;
					packet.trace.newDrawpad = reinterpret_cast<ULONG_PTR>(drawpad);
					packet.trace.newPresentation = reinterpret_cast<ULONG_PTR>(presentation);
				}
				PublishCleanupRealStage(packet, CleanupRealStage::WindowReady);
				return drawpad && presentation;
			};
			if (!startWindows()) return fail(90);
			const HostStyleCallbacks styles{ state.get(), [](void* context, DWORD set, DWORD clear) -> bool
			{
				auto* state = static_cast<State*>(context);
				if (state->generation.load(std::memory_order_acquire) == 1)
				{
					state->styleRejected.fetch_add(1, std::memory_order_acq_rel);
					if (InterlockedCompareExchange(&state->packet->trace.faultReached, 2, 0) == 0)
					{
						state->packet->trace.failureTick = GetTickCount64();
						state->packet->trace.gateTick = state->packet->trace.failureTick;
						InterlockedExchange(&state->packet->trace.faultReached, 1);
						PublishCleanupRealStage(*state->packet, CleanupRealStage::FaultReached);
					}
					return false; // 首Host所有真实mode都拒绝，不能在其内部ULW成功绕过Main重建。
				}
				return state->service->SetExtendedStyleFlags(Role::Drawpad, set, clear);
			} };
			HostStartOptions options;
			options.allowDirectComposition = true;
			options.enableHiddenTestContactInjection = true;
			options.startupContext = state.get();
			options.startupMilestone = [](void* context, HostStartupStage stage) noexcept
			{
				if (stage == HostStartupStage::GraphicsReady)
					static_cast<State*>(context)->graphicsReady.fetch_add(1, std::memory_order_acq_rel);
			};
			if (!StartDraw3ProductWithFallback(service, specs, drawpad, styles, options, {}, true, startWindows, &packet)) return fail(90);
			InterlockedExchange(&packet.trace.startReturned, 1);
			if (!state->styleRejected.load(std::memory_order_acquire) || state->graphicsReady.load(std::memory_order_acquire) < 2
				|| !packet.trace.firstStartFalseTick || !packet.trace.oldSignalCancelled || !packet.trace.oldChainDestroyed
				|| packet.trace.newGeneration != 2 || InterlockedCompareExchange(GetOffSignalInteropPointer(), 0, 0)) return fail(90);
			const auto wait = [](auto&& predicate, ULONGLONG milliseconds)
			{
				const ULONGLONG start = GetTickCount64();
				do { if (predicate()) return true; Sleep(10); } while (GetTickCount64() - start < milliseconds);
				return predicate();
			};
			Bridge::ProductState product;
			product.tool = Bridge::Tool::Pen;
			product.widthDip = 6.0f; product.colorRgba = 0xFF3050A0u;
			product.selectionMode = false;
			product.workspace = Bridge::Workspace::Desktop;
			PublishProductState(product);
			const auto ready = []
			{
				const auto value = ProductRuntimeSnapshot();
				return value.running && value.firstFrameReady && value.lastPresentSucceeded && !value.selectionMode
					&& value.presentationMode == HostPresentationMode::UlwDirtyRect
					&& value.requestedOutputTarget == HostOutputTarget::PrimaryDrawpad
					&& value.readyOutputTarget == value.requestedOutputTarget && value.readyOutputRevision == value.requestedOutputRevision
					&& value.presentedContentRevision == value.contentRevision;
			};
			if (!wait(ready, 5000) || (static_cast<DWORD>(GetWindowLongPtrW(drawpad, GWL_EXSTYLE))
				& (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT))) return fail(90);
			packet.trace.firstSuccessTick = GetTickCount64();
			packet.trace.baselineSuccessPresents = ProductRuntimeSnapshot().successfulPresentCount;
			// 超过旧绝对grace之后仍活；不是从新Host启动重新算另一份15秒。
			while (GetTickCount64() < packet.trace.graceDeadline + 1000u) Sleep(20);
			if (InterlockedCompareExchange(GetOffSignalInteropPointer(), 0, 0) || !ready()) return fail(90);
			const auto before = ProductRuntimeSnapshot();
			const auto post = [&](HiddenTestContactPhase phase, int x, int y)
			{
				DWORD pid = 0;
				return GetWindowThreadProcessId(drawpad, &pid) && pid == GetCurrentProcessId() && !IsWindowVisible(drawpad)
					&& PostMessageW(drawpad, kDraw3HiddenTestContactMessage,
						static_cast<WPARAM>(phase) | kHiddenTestIntegratedPenFlag, MAKELPARAM(x, y));
			};
			if (!post(HiddenTestContactPhase::Down, 20, 40) || !wait([&]
				{ const auto value = ProductRuntimeSnapshot(); return value.inputDownPublished == before.inputDownPublished + 1
					&& value.pen.active && value.pen.inputSequence > before.pen.inputSequence; }, 2000)) return fail(90);
			const auto stroke = ProductRuntimeSnapshot().pen.strokeId;
			for (int i = 1; i <= 16; ++i)
			{
				const auto previous = ProductRuntimeSnapshot();
				if (!post(HiddenTestContactPhase::Move, 20 + i * 4, 40 + (i % 5) * 3) || !wait([&]
					{ const auto value = ProductRuntimeSnapshot(); return value.inputMovePublished == previous.inputMovePublished + 1
						&& value.pen.strokeId == stroke && value.pen.inputSequence > previous.pen.inputSequence; }, 2000)) return fail(90);
			}
			if (!post(HiddenTestContactPhase::Up, 84, 43) || !wait([&]
				{ const auto value = ProductRuntimeSnapshot(); return value.inputTerminalPublished == before.inputTerminalPublished + 1
					&& value.inputRecycled >= before.inputRecycled + 1 && value.currentPageHasContent
					&& value.completedStrokeKind == Bridge::CompletedStrokeKind::Drawing
					&& value.successfulPresentCount > before.successfulPresentCount && ready(); }, 5000)) return fail(90);
			packet.trace.postGraceStrokeTick = GetTickCount64();
			packet.trace.afterGraceSuccessPresents = ProductRuntimeSnapshot().successfulPresentCount;
			if (!RunAuthorizedCleanupClose(launch)) return fail(90);
			packet.trace.stopEnterTick = GetTickCount64();
			StopProduct();
			service.StopAndJoin();
			packet.trace.stopReturnTick = GetTickCount64();
			InterlockedExchange(&packet.trace.stopReturned, 1);
			for (unsigned i = 0; i != roles.size(); ++i)
				if (state->destroyed[i].load(std::memory_order_acquire) != 2) return fail(90);
			for (HANDLE owner : oldOwners) if (owner) CloseHandle(owner);
			oldOwners.fill(nullptr);
			InterlockedExchange(&packet.header.result, 1);
			PublishCleanupRealStage(packet, CleanupRealStage::Finished);
			return 0;
		}
		catch (...) { return fail(90); }
	}
}

namespace Inkeys::UI::Bar
{
	bool EnsureAuthorizedUi3FixtureDpiAwareness(const Ui3FixtureAuthorization& authorization) noexcept
	{
		// 复用正式可信系统 DLL / Win7 回退，不给普通调用制造可修改的测试 DPI 策略。
		return IsAuthorizedUi3Fixture(authorization) && EnsureProcessDpiAwareness();
	}
}

// 程序入口点
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPWSTR lpCmdLine, int /*nCmdShow*/)
{
	// 内部监督进程和隔离测试必须在配置、单实例与任何 HWND 初始化前退出。
	int ui3FixtureExitCode = 0;
	if (Inkeys::UI::Bar::TryRunUi3PresentationFixtureEarly(GetCommandLineW(), ui3FixtureExitCode)) return ui3FixtureExitCode;
	int supervisorExitCode = 0;
	if (Inkeys::Shutdown::TryRunShutdownSupervisorEarly(
		GetCommandLineW(), supervisorExitCode)) return supervisorExitCode;
	constexpr wchar_t kHostMetricsSmokeArg[] = L"--draw3-host-metrics-smoke";
	{
		int count = 0; LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!arguments) return 2;
		if (count > 1 && wcscmp(arguments[1], L"--state-mode-production-test") == 0)
		{
			// 先退出启动链；测试中的宽色 setter 不保存，FineDial 只检查失效/取消提交。
			LocalFree(arguments);
			if (count != 2) return 2;
			const int stateResult = RunStateModeProductionTest();
			return stateResult != 0 ? stateResult
				: Inkeys::UI::Bar::RunThicknessFineDialProductionTest();
		}
		// argv 识别也覆盖给 mode 加引号的合法写法；坏形状不能落入普通 GUI。
		if (count > 1 && wcscmp(arguments[1], kHostMetricsSmokeArg) == 0)
		{
			int result = 2;
			if (count == 4 && wcscmp(arguments[2], L"--output-root") == 0)
				result = Inkeys::Drawing::Draw3::RunHiddenWindowRuntimeMetricsSmoke(arguments[3]);
			LocalFree(arguments);
			return result;
		}
		LocalFree(arguments);
	}
	const bool isolatedStartupFailure = Inkeys::Shutdown::IsAuthorizedStartupFailureChild();
	constexpr wchar_t kUpdateBoundaryArg[] = L"--staged-update-json-boundary-test";
	if (lpCmdLine && wcsncmp(lpCmdLine, kUpdateBoundaryArg,
		ARRAYSIZE(kUpdateBoundaryArg) - 1) == 0 &&
		(lpCmdLine[ARRAYSIZE(kUpdateBoundaryArg) - 1] == L' ' ||
			lpCmdLine[ARRAYSIZE(kUpdateBoundaryArg) - 1] == L'\0'))
	{
		int argumentCount = 0;
		LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
		if (!arguments) return 2;
		int result = 2;
		if (argumentCount == 4)
		{
			const bool valid = CompareStringOrdinal(arguments[3], -1,
				L"valid", -1, TRUE) == CSTR_EQUAL;
			const bool invalid = CompareStringOrdinal(arguments[3], -1,
				L"invalid", -1, TRUE) == CSTR_EQUAL;
			if (valid || invalid)
				result = RunStagedUpdateJsonBoundaryProbe(arguments[2], valid);
		}
		LocalFree(arguments);
		return result;
	}
	// 隐藏验收必须先于配置、互斥体和任何产品 UI 初始化。
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--pptcom-resource-verification-test", -1, TRUE) == CSTR_EQUAL)
		return RunPptComResourceVerificationTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--ddb-disabled-preserve-test", -1, TRUE) == CSTR_EQUAL)
		return RunDdbDisabledPreservationTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--config-boundary-test", -1, TRUE) == CSTR_EQUAL)
		return RunConfigBoundaryTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--supertop-token-failure-test", -1, TRUE) == CSTR_EQUAL)
		return RunSuperTopTokenFailureTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--crash-filter-lifecycle-test", -1, TRUE) == CSTR_EQUAL)
		return RunCrashFilterLifecycleTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--bar-eraser-offscreen-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::UI::Bar::RunEraserAttributeOffscreenTest();
	// GPU 像素自检仅创建不可见测试 HWND，必须先于配置和产品主窗退出。
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-renderer-pixel-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunRendererPixelTest();
	// 显式无窗口诊断在配置、单实例和 Drawpad HWND 初始化前退出。
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-renderer-map-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunRendererMapCompatibilityTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-renderer-commit-failure-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunRendererFailureCommitTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-laser-raster-failure-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunLaserRasterFailureTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-parked-desktop-exit-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunParkedDesktopExitAutoSaveTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-parked-ppt-retained-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunParkedPresentationRetainedSaveTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-loaded-retained-install-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunPresentationLoadedRetainedInstallTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-pending-topology-load-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunPendingPresentationTopologyLoadTest();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-control-fence-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunDraw3ControlFenceProductionProbe();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-fallback-stable-controller-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunFallbackStableControllerIsolationProbe();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-fallback-stable-lane-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunFallbackStableControllerLaneProbe();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-ppt-current-load-retry-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunPresentationCurrentLoadRetryProbe();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-geometry-benchmark", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunDraw3GeometryBenchmark();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-history-benchmark", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunDraw3HistoryBenchmark();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-ulw-copy-benchmark", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunUlwDirtyCopyBenchmark();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--page-control-hidden-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::UI::PageControl::RunHiddenWindowTests();
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-eraser-hidden-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunHiddenWindowIntegrationTest(true);
	if (lpCmdLine && CompareStringOrdinal(lpCmdLine, -1,
		L"--draw3-hidden-test", -1, TRUE) == CSTR_EQUAL)
		return Inkeys::Drawing::Draw3::RunHiddenWindowIntegrationTest();

#ifndef IDT_RELEASE
	bool pptComConsoleOutputEnabled = false;
	bool draw3ConsoleOutputEnabled = false;
	bool touchAreaConsoleOutputEnabled = false;
	bool cursorConsoleOutputEnabled = false;
#endif
	// 发布前临时关闭白板；覆盖启动失败和正常退出的全部清理路径。
	const bool whiteboardFeatureEnabled = IsWhiteboardFeatureEnabled();

	// 路径预处理
	{
		globalPath = GetCurrentExeDirectory() + L"\\";
		{
			int typeRoot = 0;

			if (globalPath.find(L"C:\\Program Files\\") != globalPath.npos || globalPath.find(L"C:\\Program Files (x86)\\") != globalPath.npos || globalPath.find(L"C:\\Windows\\") != globalPath.npos) typeRoot = 2;
			else
			{
				wstring time = getTimestamp();
				wstring path = globalPath + L"IdtRootCheck" + time;

				error_code ec;
				try {
					// 创建空白文件
					wofstream ofs(path);
					if (!ofs) typeRoot = 1;
					ofs.close();
					if (_waccess(path.c_str(), 0) == -1) typeRoot = 1;

					// 删除文件
					filesystem::remove(path, ec);
					if (ec) typeRoot = 1;
					if (_waccess(path.c_str(), 0) == 0) typeRoot = 1;
				}
				catch (const filesystem::filesystem_error)
				{
					typeRoot = 1;
				}
			}

			if (typeRoot == 1)
			{
				ShowStartupMessage(L"The current directory permissions are restricted, so Inkeys cannot run normally. Move Inkeys to another directory and try again. (#1)");
				return 0;
			}
			else if (typeRoot == 2)
			{
				ShowStartupMessage(L"The current directory permissions are restricted (file operations are redirected to the virtual store), so Inkeys cannot run normally. Move Inkeys to another directory and try again. (#2)");
				return 0;
			}
		}

		wstring appName = GetCurrentExeName();
		if (!isAsciiPrintable(appName))
		{
			ShowStartupMessage(L"The Inkeys file name can contain only English characters. Rename the file and restart Inkeys. (#3)");
			return 0;
		}

		// 获取目录
		{
			// 获取插件存储路径
			{
				/*
				wchar_t buffer[MAX_PATH];
				if (GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH) != 0) pluginPath = buffer;
				else pluginPath = L"C:\\ProgramData";*/

				pluginPath = globalPath;
				pluginPath += L"Inkeys\\Plugin\\";
			}
		}
	}
	// 防止重复启动
	{
		// 相关标识
		bool superTopComplete = false;

		{
			vector<wstring> args = Inkeys::Split::Run(GetCommandLineW(), L'*');
			for (size_t i = 1; i < args.size(); i++)
			{
				bool addCommandLine = true;

				wstring commandLine = args[i];

				cout << utf16ToUtf8(commandLine) << endl;

				if (commandLine == L"-Restart") LaunchState::restart = true;
				else if (commandLine == L"-WarnTry") LaunchState::warnTry = true;
				else if (commandLine == L"-CrashTry") LaunchState::crashTry = true;
				else if (commandLine == L"-SuperTopComplete") superTopComplete = true, addCommandLine = false;
				else if (commandLine.substr(0, 9) == L"-SuperTop")
				{
					addCommandLine = false;

					wregex pattern(LR"(^[^*]*\*([^*]+)\*[^*]*$)");
					wsmatch matches;
					if (regex_match(commandLine, matches, pattern))
					{
						SurperTopMain(matches[1].str());
						exit(0);
					}
				}

				if (addCommandLine) LaunchState::commandLine += commandLine + L" ";
			}
		}

	#ifdef IDT_RELEASE
		// 重启 helper 已等待旧进程真正结束；-Restart/-CrashTry 仍需正常单实例门。
		if (!LaunchState::warnTry && !superTopComplete)
		{
			wstring currentExeDirectory = GetCurrentExeDirectory();
			{
				wstringstream result;
				for (wchar_t ch : currentExeDirectory)
				{
					if (ch < 128 && ch != L'\\' && ch != L':' && ch != L';' && ch != L'"') result << ch;
					else if (ch < 128) result << L'_';
					else result << L"U" << std::hex << std::uppercase << static_cast<int>(ch);
				}
				currentExeDirectory = result.str();
			}

			wstring mutexName = L"Inkeys_" + currentExeDirectory;
			if (mutexName.length() > 255) mutexName = mutexName.substr(0, 255);

			launchMutex = CreateMutexW(
				NULL,           // 默认安全属性
				TRUE,           // 请求立即拥有所有权 (bInitialOwner = TRUE)
				mutexName.c_str()    // 唯一的互斥体名称
			);

			DWORD lastError = GetLastError();
			if (launchMutex != NULL && lastError == ERROR_ALREADY_EXISTS)
			{
				ShowStartupMessage(L"Inkeys is already running. If no Inkeys window is visible, end the existing Inkeys process and open it again.");

				return 0;
			}
		}
	#endif
		if (LaunchState::crashTry) CrashHandler::IsSecond(true);
	}
	// 崩溃助手初始化
	{
		if (isolatedStartupFailure) CrashHandler::SetFlag(2);
		CrashHandler::Initialize();
	}
	// 体系架构识别
	{
	#if defined(_M_ARM64)
		programArchitecture = L"arm64";
	#elif defined(_M_ARM64EC)
		programArchitecture = L"arm64ec";
	#elif defined(_WIN64)
		programArchitecture = L"win64";
	#else
		programArchitecture = L"win32";
	#endif

		USHORT processMachine = 0, nativeMachine = 0;
		bool successFlg = false;

		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		if (hKernel32)
		{
			typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS2)(HANDLE, USHORT*, USHORT*);
			LPFN_ISWOW64PROCESS2 fnIsWow64Process2 = (LPFN_ISWOW64PROCESS2)GetProcAddress(hKernel32, "IsWow64Process2");

			if (fnIsWow64Process2)
			{
				// 如果 IsWow64Process2 可用
				if (fnIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
				{
					if (nativeMachine == IMAGE_FILE_MACHINE_ARM64)
					{
						targetArchitecture = L"arm64";
						successFlg = true;
					}
					else if (nativeMachine == IMAGE_FILE_MACHINE_AMD64)
					{
						targetArchitecture = L"win64";
						successFlg = true;
					}
					else
					{
						targetArchitecture = L"win32";
						successFlg = true;
					}
				}
			}
		}
		if (!successFlg)
		{
			SYSTEM_INFO sysInfo;
			GetNativeSystemInfo(&sysInfo);
			if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) targetArchitecture = L"arm64";
			else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) targetArchitecture = L"win64";
			else targetArchitecture = L"win32";
		}

		if (targetArchitecture == L"arm64" && programArchitecture != L"arm64" && programArchitecture != L"arm64ec") inconsistentArchitecture = true;
		if (targetArchitecture == L"win64" && programArchitecture != L"win64") inconsistentArchitecture = true;
		if (targetArchitecture == L"win32" && programArchitecture != L"win32") inconsistentArchitecture = true;
	}
	// 检查系统版本
	{
		IdtSysVersionStruct windowsVersion = GetWindowsVersion();
		windowsEdition = to_wstring(windowsVersion.majorVersion) + L"." + to_wstring(windowsVersion.minorVersion) + L"." + to_wstring(windowsVersion.buildNumber);
	}
	// 程序自动更新
	{
		if (!isolatedStartupFailure && _waccess((globalPath + L"update.json").c_str(), 4) == 0)
		{
			wstring tedition, representation;
			string thash_md5, thash_sha256;
			wstring old_name;

			bool flag = true;
			string jsonContent;

			HANDLE fileHandle = NULL;
			if (OccupyFileForRead(&fileHandle, globalPath + L"update.json"))
			{
				LARGE_INTEGER fileSize;
				if (flag && (!GetFileSizeEx(fileHandle, &fileSize) ||
					fileSize.QuadPart <= 0 ||
					fileSize.QuadPart > kMaximumUpdateInstructionBytes)) flag = false;

				if (flag)
				{
					DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
					jsonContent = string(dwSize, '\0');

					DWORD bytesRead = 0;
					if (flag && SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) flag = false;
					if (flag && (!ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)) flag = false;
					if (flag && jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
				}
			}
			else flag = false;
			UnOccupyFile(&fileHandle);

			Json::Value updateVal;
			if (flag)
			{
				istringstream jsonContentStream(jsonContent);
				Json::CharReaderBuilder readerBuilder;
				readerBuilder["stackLimit"] = 32;
				string jsonErr;

				bool parsed = false;
				try
				{
					parsed = Json::parseFromStream(readerBuilder, jsonContentStream,
						&updateVal, &jsonErr);
					if (!parsed || !updateVal.isObject()) { flag = false; }
					else
					{
					if (updateVal.isMember("edition") && updateVal["edition"].isString()) tedition = utf8ToUtf16(updateVal["edition"].asString());
					else flag = false;

					if (updateVal.isMember("representation") && updateVal["representation"].isString()) representation = utf8ToUtf16(updateVal["representation"].asString());
					else flag = false;

					if (updateVal.isMember("hash") && updateVal["hash"].isObject())
					{
						if (updateVal["hash"].isMember("md5") && updateVal["hash"]["md5"].isString()) thash_md5 = updateVal["hash"]["md5"].asString();
						else flag = false;
						if (updateVal["hash"].isMember("sha256") && updateVal["hash"]["sha256"].isString()) thash_sha256 = updateVal["hash"]["sha256"].asString();
						else flag = false;
					}
					else flag = false;

					if (updateVal.isMember("old_name") && updateVal["old_name"].isString()) old_name = utf8ToUtf16(updateVal["old_name"].asString());
					}
				}
				catch (const std::exception&) { flag = false; }
			}
			// 更新指令来自可写文件；任何拼接路径必须先限制为本目录单个 EXE 文件名。
			if (!IsSafeUpdateExecutableName(representation)) flag = false;
			if (!IsSafeUpdateHash(thash_md5, 32) ||
				!IsSafeUpdateHash(thash_sha256, 64)) flag = false;
			if (!old_name.empty() && !IsSafeUpdateExecutableName(old_name))
			{
				old_name.clear();
				flag = false;
			}
			if (!flag) old_name.clear(); // 损坏的更新指令不能指定失败回退要执行的旧程序。

			string hash_md5, hash_sha256;
			if (flag)
			{
				{
					hashwrapper* myWrapper = new md5wrapper();
					hash_md5 = myWrapper->getHashFromFileW(GetCurrentExePath());
					delete myWrapper;
				}
				{
					hashwrapper* myWrapper = new sha256wrapper();
					hash_sha256 = myWrapper->getHashFromFileW(GetCurrentExePath());
					delete myWrapper;
				}
			}

			if (flag && tedition == editionDate && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本
				filesystem::path directory(globalPath);
				wstring main_path = directory.parent_path().parent_path().wstring() + L"\\";

				int times;
				for (times = 0; times <= 20; times++)
				{
					if (!old_name.empty())
					{
						if (!isProcessRunning((main_path + old_name).c_str()))
							break;
					}
					else
					{
						if (!isProcessRunning((main_path + L"智绘教.exe").c_str()))
							break;
					}

					this_thread::sleep_for(chrono::milliseconds(100));
				}
				if (times > 20) goto fail;

				bool rollbackSafe = true;
				const bool launched = [&]() -> bool
					{
						const wstring target = main_path + L"Inkeys.exe";
						wchar_t stagedPath[MAX_PATH]{};
						if (!GetTempFileNameW(main_path.c_str(), L"IKU", 0, stagedPath))
							return false;
						const auto removeStage = [&]() noexcept
							{
								error_code ignored;
								filesystem::remove(stagedPath, ignored);
							};
						error_code ec;
						filesystem::copy_file(globalPath + representation, stagedPath,
							filesystem::copy_options::overwrite_existing, ec);
						if (ec) { removeStage(); return false; }
						// 先在同卷临时文件完成复制及复核，旧 EXE 保持可启动。
						md5wrapper md5;
						sha256wrapper sha256;
						if (md5.getHashFromFileW(stagedPath) != thash_md5 ||
							sha256.getHashFromFileW(stagedPath) != thash_sha256)
						{
							removeStage();
							return false;
						}
						const bool hadTarget = filesystem::exists(target, ec);
						if (ec) { removeStage(); return false; }
						wchar_t backupPath[MAX_PATH]{};
						if (hadTarget)
						{
							if (!GetTempFileNameW(main_path.c_str(), L"IKB", 0, backupPath))
							{
								removeStage();
								return false;
							}
							filesystem::copy_file(target, backupPath,
								filesystem::copy_options::overwrite_existing, ec);
							if (ec)
							{
								removeStage();
								filesystem::remove(backupPath, ec);
								return false;
							}
						}
						if (!MoveFileExW(stagedPath, target.c_str(),
							MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
						{
							removeStage();
							if (hadTarget) filesystem::remove(backupPath, ec);
							return false;
						}
						const HINSTANCE result = ShellExecuteW(
							nullptr, nullptr, target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
						if (reinterpret_cast<INT_PTR>(result) > 32) return true;
						// 启动失败时回滚本次替换；备份在启动成功后仍保留供人工恢复。
						if (hadTarget)
							rollbackSafe = MoveFileExW(backupPath, target.c_str(),
								MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
						else
						{
							filesystem::remove(target, ec);
							rollbackSafe = !ec;
						}
						return false;
					}();
				if (launched) return 0;
				if (!rollbackSafe)
				{
					ShowStartupMessage(L"更新程序启动失败，旧版本恢复也未完成。请检查 Inkeys.exe 与 IKB 备份文件。");
					return 0;
				}
				goto fail;
			}
			else flag = false;

			if (!flag)
			{
			fail:
				error_code ec;
				filesystem::remove(globalPath + L"update.json", ec);

				filesystem::path directory(globalPath);
				wstring main_path = directory.parent_path().parent_path().wstring() + L"\\";

				// 旧 Inkeys2 不写 old_name：优先恢复智绘教.exe，且始终只在安装主目录选普通 EXE。
				std::array<wstring, 3> triedNames;
				size_t triedCount = 0;
				for (; triedCount < triedNames.size();)
				{
					const wstring fallbackName = SelectUpdateRollbackExecutableName(
						old_name, [&](wstring_view name)
						{
							for (size_t i = 0; i < triedCount; ++i)
								if (_wcsicmp(triedNames[i].c_str(), wstring(name).c_str()) == 0)
									return false;
							const wstring candidate = main_path + wstring(name);
							const DWORD attributes = GetFileAttributesW(candidate.c_str());
							return attributes != INVALID_FILE_ATTRIBUTES &&
								(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
						});
					if (fallbackName.empty()) break;
					triedNames[triedCount++] = fallbackName;
					const HINSTANCE result = ShellExecuteW(nullptr, nullptr,
						(main_path + fallbackName).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					if (reinterpret_cast<INT_PTR>(result) > 32) return 0;
					// 用户取消提升后不能再悄悄尝试另一份旧程序。
					if (GetLastError() == ERROR_CANCELLED) return 0;
				}
				ShowStartupMessage(L"更新回退启动失败，请手动打开安装目录中的旧版本程序。");

				return 0;
			}
		}
		if (!isolatedStartupFailure && _waccess((globalPath + L"installer\\update.json").c_str(), 4) == 0)
		{
			wstring tedition, path;
			string thash_md5, thash_sha256;

			bool flag = true;
			bool mandatoryUpdate = false;
			string jsonContent;

			HANDLE fileHandle = NULL;
			if (OccupyFileForRead(&fileHandle, globalPath + L"installer\\update.json"))
			{
				LARGE_INTEGER fileSize;
				if (flag && (!GetFileSizeEx(fileHandle, &fileSize) ||
					fileSize.QuadPart <= 0 ||
					fileSize.QuadPart > kMaximumUpdateInstructionBytes)) flag = false;

				if (flag)
				{
					DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
					jsonContent = string(dwSize, '\0');

					DWORD bytesRead = 0;
					if (flag && SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) flag = false;
					if (flag && (!ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)) flag = false;
					if (flag && jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
				}
			}
			else flag = false;
			UnOccupyFile(&fileHandle);

			Json::Value updateVal;
			if (flag)
			{
				istringstream jsonContentStream(jsonContent);
				Json::CharReaderBuilder readerBuilder;
				readerBuilder["stackLimit"] = 32;
				string jsonErr;

				bool parsed = false;
				try
				{
					parsed = Json::parseFromStream(readerBuilder, jsonContentStream,
						&updateVal, &jsonErr);
					if (!parsed || !updateVal.isObject()) { flag = false; }
					else
					{
					if (updateVal.isMember("edition") && updateVal["edition"].isString()) tedition = utf8ToUtf16(updateVal["edition"].asString());
					else flag = false;

					if (updateVal.isMember("path") && updateVal["path"].isString()) path = utf8ToUtf16(updateVal["path"].asString());
					else flag = false;

					if (updateVal.isMember("hash") && updateVal["hash"].isObject())
					{
						if (updateVal["hash"].isMember("md5") && updateVal["hash"]["md5"].isString()) thash_md5 = updateVal["hash"]["md5"].asString();
						else flag = false;
						if (updateVal["hash"].isMember("sha256") && updateVal["hash"]["sha256"].isString()) thash_sha256 = updateVal["hash"]["sha256"].asString();
						else flag = false;
					}
					else flag = false;

					if (updateVal.isMember("MandatoryUpdate") && updateVal["MandatoryUpdate"].isBool())
						mandatoryUpdate = updateVal["MandatoryUpdate"].asBool();
					}
				}
				catch (const std::exception&) { flag = false; }
			}
			// 待启动的更新程序只能位于本程序的 installer 子目录。
			if (!IsSafeStagedUpdatePath(path)) flag = false;
			if (!IsSafeUpdateHash(thash_md5, 32) ||
				!IsSafeUpdateHash(thash_sha256, 64)) flag = false;

			string hash_md5, hash_sha256;
			if (flag)
			{
				{
					hashwrapper* myWrapper = new md5wrapper();
					hash_md5 = myWrapper->getHashFromFileW(globalPath + path);
					delete myWrapper;
				}
				{
					hashwrapper* myWrapper = new sha256wrapper();
					hash_sha256 = myWrapper->getHashFromFileW(globalPath + path);
					delete myWrapper;
				}
			}

			if (flag && (tedition > editionDate || mandatoryUpdate) && _waccess((globalPath + path).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本

				const wstring oldName = GetCurrentExeName();
				if (!IsSafeUpdateExecutableName(oldName)) flag = false;
				updateVal["old_name"] = Json::Value(utf16ToUtf8(oldName));
				if (mandatoryUpdate) updateVal["MandatoryUpdate"] = Json::Value(false);

				if (flag)
				{
					Json::StreamWriterBuilder writerBuilder;
					string nextJson;
					try { nextJson = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, updateVal); }
					catch (const Json::Exception&) { flag = false; }
					const wstring installerPath = globalPath + L"installer\\";
					const DWORD installerAttributes = GetFileAttributesW(installerPath.c_str());
					if (nextJson.empty() || nextJson.size() > kMaximumUpdateInstructionBytes ||
						installerAttributes == INVALID_FILE_ATTRIBUTES ||
						(installerAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
						(installerAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) flag = false;
					if (flag)
					{
					wchar_t temporaryPath[MAX_PATH]{};
					if (!GetTempFileNameW(installerPath.c_str(), L"IKJ", 0,
							temporaryPath)) flag = false;
					if (flag)
					{
						const HANDLE temporary = CreateFileW(temporaryPath, GENERIC_WRITE,
							0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
						if (temporary == INVALID_HANDLE_VALUE) flag = false;
						else
					{
							DWORD bytesWritten = 0;
							flag = WriteFile(temporary, nextJson.data(),
								static_cast<DWORD>(nextJson.size()), &bytesWritten, nullptr) &&
								bytesWritten == nextJson.size() && FlushFileBuffers(temporary);
							if (!CloseHandle(temporary)) flag = false;
						}
						// 同目录临时指令完整落盘后才替换旧指令；失败保留上次有效文件。
						if (flag) flag = MoveFileExW(temporaryPath,
							(globalPath + L"installer\\update.json").c_str(),
							MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
						if (!flag) DeleteFileW(temporaryPath);
					}
					}
				}

				if (flag)
				{
					HINSTANCE hInst = ShellExecuteW(NULL, NULL, (globalPath + path).c_str(), NULL, NULL, SW_SHOWNORMAL);
					if ((INT_PTR)hInst > 32) return 0;
					flag = false;
				}
			}
			else flag = false;

			// 无效或发布失败的指令不清理未知文件，也不覆盖最后一次有效更新点。
		}
	}

	// InkeysSuperTop 阶段
	bool SuperTopFailSignal = false;
	if (!isolatedStartupFailure && _waccess((globalPath + L"opt\\deploy.json").c_str(), 4) == 0)
	{
		ReadSettingMini();

		HANDLE proc_self = GetCurrentProcess();
		HANDLE tok_self;
		OpenProcessToken(proc_self, TOKEN_ALL_ACCESS, &tok_self);

		if (setlist.plugInSetting.superTop.enable && !hasUiAccess(tok_self))
		{
			while (true)
			{
				bool hasExistsFaild = false;

				error_code ec;
				if (filesystem::exists(globalPath + L"superTop_try.signal", ec))
				{
					string dateContent;
					{
						HANDLE fileHandle = NULL;
						if (!OccupyFileForRead(&fileHandle, globalPath + L"superTop_try.signal"))
						{
							UnOccupyFile(&fileHandle);
							break;
						}

						LARGE_INTEGER fileSize;
						if (!GetFileSizeEx(fileHandle, &fileSize))
						{
							UnOccupyFile(&fileHandle);
							break;
						}

						DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
						dateContent = string(dwSize, '\0');

						DWORD bytesRead = 0;
						if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
						{
							UnOccupyFile(&fileHandle);
							break;
						}
						if (!ReadFile(fileHandle, &dateContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)
						{
							UnOccupyFile(&fileHandle);
							break;
						}

						if (dateContent.compare(0, 3, "\xEF\xBB\xBF") == 0) dateContent = dateContent.substr(3);
						UnOccupyFile(&fileHandle);
					}

					double diff;
					{
						tm tm = {};
						int year, month, day, hour, minute, second;
						if (sscanf_s(dateContent.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
							break;

						tm.tm_year = year - 1900; // 年份从1900开始
						tm.tm_mon = month - 1;    // 月份0-11
						tm.tm_mday = day;
						tm.tm_hour = hour;
						tm.tm_min = minute;
						tm.tm_sec = second;
						tm.tm_isdst = -1;         // 让系统自动判断夏令时

						time_t old_time = mktime(&tm);
						time_t new_time = time(nullptr);

						diff = difftime(new_time, old_time);
					}

					// 如果小于 30s 则视为同一次尝试，此时宣告尝试失败
					if (diff <= 30)
					{
						SuperTopFailSignal = true;
						break;
					}
					else hasExistsFaild = true;
				}

				{
					if (!filesystem::exists(globalPath + L"superTop_try.signal", ec) || hasExistsFaild)
					{
						string date;
						{
							auto now = chrono::system_clock::now();
							time_t t = chrono::system_clock::to_time_t(now);
							tm tm; localtime_s(&tm, &t);

							ostringstream oss;
							oss << put_time(&tm, "%Y-%m-%d %H:%M:%S");
							date = oss.str();
						}

						bool isSuccess = false;
						while (true)
						{
							HANDLE fileHandle = NULL;
							if (!OccupyFileForWrite(&fileHandle, globalPath + L"superTop_try.signal"))
							{
								UnOccupyFile(&fileHandle);
								break;
							}
							if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
							{
								UnOccupyFile(&fileHandle);
								break;
							}
							if (!SetEndOfFile(fileHandle))
							{
								UnOccupyFile(&fileHandle);
								break;
							}

							Json::StreamWriterBuilder writerBuilder;
							string dateContent = "\xEF\xBB\xBF" + date;

							DWORD bytesWritten = 0;
							if (!WriteFile(fileHandle, dateContent.data(), static_cast<DWORD>(dateContent.size()), &bytesWritten, NULL) || bytesWritten != dateContent.size())
							{
								UnOccupyFile(&fileHandle);
								break;
							}

							UnOccupyFile(&fileHandle);

							isSuccess = true;
							break;
						}

						if (!isSuccess)
						{
							HANDLE fileHandle = NULL;
							OccupyFileForWrite(&fileHandle, globalPath + L"superTop_try.signal");
							UnOccupyFile(&fileHandle);
						}
					}

					LaunchSurperTop(LaunchState::commandLine);
				}
				break;
			}
		}

		error_code ec;
		filesystem::remove(globalPath + L"superTop_try.signal", ec);

		hasSuperTop = hasUiAccess(tok_self);
	}

	// 只有最终进程越过 SuperTop 分支后，才建立唯一 T0 与启动状态。
	const auto startupT0 = chrono::steady_clock::now();
	std::wstring startupPreviewSmokePath;
	{
		int argumentCount = 0;
		LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
		if (arguments)
		{
			for (int index = 1; index < argumentCount; ++index)
			{
				if (CompareStringOrdinal(arguments[index], -1,
					L"--startup-preview-smoke", -1, TRUE) == CSTR_EQUAL
					&& index + 1 < argumentCount)
				{
					startupPreviewSmokePath = arguments[index + 1];
					++index;
					continue;
				}
				if (CompareStringOrdinal(arguments[index], -1,
					L"--startup-preview-manual-delay", -1, TRUE) == CSTR_EQUAL)
				{
					// 命令行入口方便人工观察；环境变量入口继续兼容脚本。
					startupPreviewManualDelayRequested.store(true,
						std::memory_order_release);
					continue;
				}
			}
			LocalFree(arguments);
		}
	}
	const bool startupPreviewSmokeRequested =
		!startupPreviewSmokePath.empty();
	Inkeys::Config startupMiniConfig;
	(void)startupMiniConfig.ReadMini({
		"Experimental.Inkeys3.UI3.StartupPreview.Enable",
		"Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip",
		"UI.Bar.Zoom" });
	const bool startupPreviewConfigured = startupPreviewSmokeRequested
		|| startupMiniConfig.Experimental.Inkeys3.UI3.StartupPreview.Enable;
	bool startupPreviewStarted = false;
	Inkeys::Startup::ProgressTracker startupProgress(
		Inkeys::Startup::Plan::ForStartup(startupPreviewConfigured), startupT0);
	Inkeys::Startup::SetActiveTracker(&startupProgress);
	struct StartupProgressScope final
	{
		Inkeys::Startup::ProgressTracker* tracker = nullptr;
		~StartupProgressScope()
		{
			Inkeys::UI::StartupPreview::Stop();
			Inkeys::Startup::ClearActiveTracker(tracker);
		}
	} startupProgressScope{ &startupProgress };
	(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::SuperTopCrossed);
	(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::MiniConfigRead);

	if (!EnsureProcessDpiAwareness())
	{
		PublishFatalStartupFailure(0xD001u,
			L"Unable to configure process DPI awareness.\n无法配置进程 DPI 感知，程序无法安全启动。");
		return 1;
	}
	(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::DpiAwarenessReady);

	if (startupPreviewConfigured)
	{
		const HRESULT earlyRenderResult = Inkeys::UI::RenderPipeline::Initialize();
		if (FAILED(earlyRenderResult))
		{
			PublishFatalStartupFailure(0xD002u,
				L"Unable to initialize the shared rendering pipeline.\n共享渲染管线初始化失败。");
			return 1;
		}
		// 现有 Initialize 是一个原子握手；三个真实子资源在成功返回后合并报告。
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::RenderFactoriesReady);
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::RenderDeviceReady);
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::RenderSchedulerReady);

		MONITORINFO monitorInfo{};
		monitorInfo.cbSize = sizeof(monitorInfo);
		const HMONITOR primaryMonitor = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
		(void)GetMonitorInfoW(primaryMonitor, &monitorInfo);
		UINT startupDpi = USER_DEFAULT_SCREEN_DPI;
		if (HDC screen = GetDC(nullptr))
		{
			const int queriedDpi = GetDeviceCaps(screen, LOGPIXELSX);
			if (queriedDpi > 0) startupDpi = static_cast<UINT>(queriedDpi);
			ReleaseDC(nullptr, screen);
		}
		Inkeys::UI::StartupPreview::StartOptions previewOptions;
		previewOptions.instance = hInstance;
		previewOptions.cachedWidthDip =
			Inkeys::UI::StartupPreview::ResolveCachedStartupBarWidthDip(
				startupMiniConfig.Experimental.Inkeys3.UI3.StartupPreview.
					CachedStartupBarWidthDip);
		previewOptions.barZoom = startupMiniConfig.UI.Bar.Zoom;
		previewOptions.dpi = startupDpi;
		previewOptions.monitorBounds = monitorInfo.rcMonitor;
		previewOptions.workArea = monitorInfo.rcWork;
		previewOptions.contentBounds =
			Inkeys::UI::StartupPreview::ResolveStartupPreviewBounds(
				previewOptions.monitorBounds, previewOptions.workArea,
				previewOptions.cachedWidthDip,
				Inkeys::UI::StartupPreview::DefaultStartupBarHeightDip,
				previewOptions.dpi, previewOptions.barZoom);
		previewOptions.progress = &startupProgress;
		previewOptions.requestBarAlpha = [](std::uint8_t alpha)
			{ Inkeys::UI::Bar::RequestPresentationAlpha(alpha); };
		(void)Inkeys::Startup::Report(
			Inkeys::Startup::Milestone::PreviewGeometryReady);
		if (startupPreviewConfigured)
		{
			startupPreviewStarted = Inkeys::UI::StartupPreview::Start(previewOptions);
			if (!startupPreviewStarted)
			{
				// Preview 是可降级阶段；attempt 已有界结束后收敛可选权重，正式 Bar 保持 255。
				(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::PreviewOwnerReady);
				(void)Inkeys::Startup::Report(
					Inkeys::Startup::Milestone::PreviewRenderClientReady);
				(void)Inkeys::Startup::Report(
					Inkeys::Startup::Milestone::PreviewFirstFrameCommitted);
			}
		}
		if (startupPreviewSmokeRequested)
		{
			if (!startupPreviewStarted)
			{
				(void)WriteStartupPreviewSmokeReport(startupPreviewSmokePath, false,
					Inkeys::UI::StartupPreview::SnapshotDiagnostics(),
					Inkeys::UI::Bar::SnapshotPresentationAlphaDiagnostics());
				Inkeys::UI::RenderPipeline::Shutdown();
				return 3;
			}
			// 显式 smoke 模式短暂保留程序化首帧，供无输入截图验证。
			this_thread::sleep_for(chrono::milliseconds(1500));
		}
	}

	// 用户ID获取
	{
		userId = utf8ToUtf16(getDeviceGUID());
		if (userId.empty() || !isValidString(userId)) userId = L"Error";
	}
	// 日志服务初始化
	{
		wstring Timestamp = getTimestamp();

		error_code ec;
		if (_waccess((globalPath + L"log").c_str(), 0) == -1) filesystem::create_directory(globalPath + L"log", ec);
		else
		{
			// 历史日志清理

			auto getCurrentTimeStamp = []()
				{
					auto now = chrono::system_clock::now();
					auto duration = now.time_since_epoch();
					return chrono::duration_cast<chrono::milliseconds>(duration).count();
				};

			auto isLogFile = [](const string& filename)
				{
					regex pattern("idt\\d+\\.log");
					return regex_match(filename, pattern);
				};

			auto getTimeStampFromFilename = [](const string& filename)
				{
					regex pattern("idt(\\d+)\\.log");

					smatch match;
					if (regex_search(filename, match, pattern))
					{
						string timestampStr = match[1];
						return stoll(timestampStr);
					}
					return -1LL;
				};

			auto isOldLogFile = [&getCurrentTimeStamp, &getTimeStampFromFilename](const filesystem::path& filepath)
				{
					time_t currentTimeStamp = getCurrentTimeStamp();
					time_t fileTimeStamp = getTimeStampFromFilename(filepath.filename().string());

					if (fileTimeStamp == -1) return false;
					return (currentTimeStamp - fileTimeStamp) >= (7LL * 24LL * 60LL * 60LL * 1000LL) || (currentTimeStamp - fileTimeStamp) < 0; // 7天的毫秒数
				};

			auto calculateDirectorySize = [](const filesystem::path& directoryPath)
				{
					uintmax_t totalSize = 0;
					for (const auto& entry : filesystem::directory_iterator(directoryPath))
					{
						if (entry.is_regular_file()) {
							totalSize += entry.file_size();
						}
					}
					return totalSize;
				};

			auto deleteOldLogFiles = [&isLogFile, &isOldLogFile, &calculateDirectorySize](const filesystem::path& directory)
				{
					uintmax_t totalSize = calculateDirectorySize(directory);

					for (const auto& entry : filesystem::directory_iterator(directory))
					{
						if (entry.is_regular_file())
						{
							if (isLogFile(entry.path().filename().string()) && (totalSize > 10485760LL || isOldLogFile(entry.path())))
							{
								uintmax_t entrySize = entry.file_size();

								error_code ec;
								filesystem::remove(entry.path(), ec);

								if (!ec) totalSize -= entrySize;
							}
						}
					}
				};

			wstring directoryPath = globalPath + L"log";
			filesystem::path directory(directoryPath);
			if (filesystem::exists(directory) && filesystem::is_directory(directory))
			{
				deleteOldLogFiles(directory);
			}
		}

		if (_waccess((globalPath + L"log\\idt" + Timestamp + L".log").c_str(), 0) == 0) filesystem::remove(globalPath + L"log\\idt" + Timestamp + L".log", ec);

		auto IDTLoggerFileSink = std::make_shared<spdlog::sinks::basic_file_sink<std::mutex>>(globalPath + L"log\\idt" + Timestamp + L".log", true);

		spdlog::init_thread_pool(8192, 64);
		IDTLogger = std::make_shared<spdlog::async_logger>("IDTLogger", IDTLoggerFileSink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);

		IDTLogger->set_level(spdlog::level::info);
		IDTLogger->set_pattern("[%l][%H:%M:%S.%e]%v");

		IDTLogger->flush_on(spdlog::level::info);

		// 共用现有文件和线程池，诊断满队列时保留聚合，不能反过来阻塞渲染线程。
		const auto diagnosticsPool = spdlog::thread_pool();
		auto diagnosticsLogger = std::make_shared<spdlog::async_logger>(
			"UI3Diagnostics", IDTLoggerFileSink, diagnosticsPool, spdlog::async_overflow_policy::discard_new);
		diagnosticsLogger->set_level(spdlog::level::warn);
		diagnosticsLogger->flush_on(spdlog::level::warn);
		(void)Inkeys::UI::RenderPipeline::SetDiagnosticsSink(
			[diagnosticsLogger, diagnosticsPool](std::string_view message)
			{
				const auto discarded = diagnosticsPool->discard_counter();
				diagnosticsLogger->warn("{}", message);
				return diagnosticsPool->discard_counter() == discarded;
			});
		IDTLogger->info("[主线程][IdtMain] 日志开始记录 " + utf16ToUtf8(editionDate) + " " + utf16ToUtf8(userId));

		if (LaunchState::crashTry) IDTLogger->warn("[主线程][IdtMain] 发现程序先前发生过崩溃错误");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::LoggingReady);

		//logger->info("");
		//logger->warn("");
		//logger->error("");
		//logger->critical("");
	}
	// DPI初始化
	{
		// 进程 awareness 已在 T0 后、任何 HWND/Display 前确定；此处只保留旧 surface resize。
		{
			(void)alpha_drawpad.resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			(void)tester.resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			(void)pptdrawpad.resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
		}

		IDTLogger->info("[主线程][IdtMain] DPI初始化完成");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::LegacySurfaceReady);
	}
	// COM初始化
	HANDLE hActCtx = INVALID_HANDLE_VALUE;
	ULONG_PTR ulCookie = 0;
	bool actCtxActivated = false;
	HMODULE pptComModule = nullptr;
	const HRESULT comInitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	const bool isolatedComFailure = Inkeys::Shutdown::IsAuthorizedStartupFailure(
		Inkeys::Shutdown::StartupFailureSite::Com);
	if (!isolatedComFailure && (SUCCEEDED(comInitializeResult) || comInitializeResult == RPC_E_CHANGED_MODE))
		ReportStartupMilestoneForManualTest(Inkeys::Startup::Milestone::ComReady);
	else
	{
		if (isolatedComFailure) Inkeys::Shutdown::PublishAuthorizedStartupFailure(
			Inkeys::Shutdown::StartupFailureSite::Com, 0xD004u, comInitializeResult);
		PublishFatalStartupFailure(0xD004u,
			L"COM 运行环境初始化失败，程序无法继续启动。");
		if (isolatedComFailure && SUCCEEDED(comInitializeResult)) CoUninitialize();
		Inkeys::UI::RenderPipeline::Shutdown();
		return 1;
	}

	// 显示器信息初始化
	{
		if (!Inkeys::Display::Initialize())
			IDTLogger->warn("[主线程][IdtMain] 显示器枚举失败，使用兼容回退信息");
		const auto displaySnapshot = Inkeys::Display::GetSnapshot();
		if (displaySnapshot && displaySnapshot->monitors.size() > 1)
			IDTLogger->warn("[主线程][IdtMain] 拥有多个显示器");
		IDTLogger->info("[主线程][IdtMain] 显示器信息初始化完成");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::DisplayReady);
	}

	// 配置信息初始化
	{
		// 读取配置文件前初始化操作
		{
			// 软件版本
			{
				unique_lock<shared_mutex> lock(setlistUpdateMutex);
				setlist.enableAutoUpdate = true;
				setlist.UpdateChannel = "LTS";
				{
					setlist.updateArchitecture = utf16ToUtf8(programArchitecture);
					if (setlist.updateArchitecture == "arm64ec") setlist.updateArchitecture = "arm64";
				}
			}
			// 常规
			{
				setlist.startUp = false;

				setlist.SetSkinMode = 0;
				setlist.SkinMode = 1;

				setlist.topSleepTime = 3;
				setlist.RightClickClose = false;

				setlist.BrushRecover = true;
				setlist.RubberRecover = false;
				setlist.regularSetting.moveRecover = false;
				setlist.regularSetting.clickRecover = false;

				setlist.regularSetting.teachingSafetyMode = 0;
			}
			// 绘制
			{
				setlist.disableRTS = false;

				setlist.liftStraighten = false, setlist.waitStraighten = true;
				setlist.pointAdsorption = true;

				setlist.smoothWriting = true;

				{
					// Draw3 不再支持压感橡皮；旧默认值归一化为速度橡皮。
					setlist.eraserSetting.eraserMode = 1;

					float drawingScale = GetDrawingScale();
					setlist.eraserSetting.eraserSize = static_cast<int>(60 * drawingScale);
				}

				setlist.hideTouchPointer = false;
			}
			// 保存
			{
				setlist.saveSetting.enable = true;
				setlist.saveSetting.saveDays = 2;
			}
			// 性能
			{
				setlist.performanceSetting.preparationQuantity = 2;
				setlist.performanceSetting.drawpadFps = 72;
				setlist.performanceSetting.superDraw = false;
			}
			// 预设
			{
				setlist.presetSetting.memoryWidth = true;
				setlist.presetSetting.memoryColor = false;

				setlist.presetSetting.autoDefaultWidth = true;
				setlist.presetSetting.defaultBrush1Width = 3.0f;
				setlist.presetSetting.defaultHighlighter1Width = 35.0f;
			}

			// 插件
			{
				{
					setlist.shortcutAssistant.correctLnk = true;
					setlist.shortcutAssistant.createLnk = false;
				}
				{
					setlist.plugInSetting.superTop.enable = false;
					setlist.plugInSetting.superTop.indicator = true;
				}
			}
			// 组件
			{
				{
					{
						{
							setlist.component.shortcutButton.appliance.explorer = false;
							setlist.component.shortcutButton.appliance.taskmgr = false;
							setlist.component.shortcutButton.appliance.control = false;
						}
						{
							setlist.component.shortcutButton.system.desktop = false;
							setlist.component.shortcutButton.system.lockWorkStation = false;
						}
						{
							setlist.component.shortcutButton.keyboard.keyboardesc = false;
							setlist.component.shortcutButton.keyboard.keyboardAltF4 = false;
						}
						{
							setlist.component.shortcutButton.rollCall.IslandCaller1 = false;
							setlist.component.shortcutButton.rollCall.IslandCaller2 = false;
							setlist.component.shortcutButton.rollCall.SecRandom1 = false;
							setlist.component.shortcutButton.rollCall.SecRandom2 = false;
							setlist.component.shortcutButton.rollCall.SecRandom2Compat = false;
							setlist.component.shortcutButton.rollCall.NamePicker = false;
						}
						{
							setlist.component.shortcutButton.linkage.classislandSettings = false;
							setlist.component.shortcutButton.linkage.classislandProfile = false;
							setlist.component.shortcutButton.linkage.classislandClassswap = false;
						}
					}
				}
			}

			{
				// 获取系统默认语言标识符
				LANGID langId = GetUserDefaultUILanguage();
				// 获取主语言标识符
				WORD primaryLangId = PRIMARYLANGID(langId);
				// 获取子语言标识符
				WORD subLangId = SUBLANGID(langId);

				// 检查是否为中文
				if (primaryLangId == LANG_CHINESE)
				{
					switch (subLangId) {
					case SUBLANG_CHINESE_SIMPLIFIED:
					case SUBLANG_CHINESE_SINGAPORE:
					{
						setlist.selectLanguage = 1;
						break;
					}

					case SUBLANG_CHINESE_TRADITIONAL:
					case SUBLANG_CHINESE_HONGKONG:
					case SUBLANG_CHINESE_MACAU:
					{
						setlist.selectLanguage = 2;
						break;
					}
					}
				}
				else setlist.selectLanguage = 0;
			}
			{
				int digitizerStatus = GetSystemMetrics(SM_DIGITIZER);
				bool hasTouchDevice = (digitizerStatus & NID_READY) && (digitizerStatus & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH));
				if (hasTouchDevice)
				{
					const auto displaySnapshot = Inkeys::Display::GetSnapshot();
					const auto* monitor = displaySnapshot ? displaySnapshot->Primary() : nullptr;
					if (!monitor || !monitor->physicalSize.available) setlist.paintDevice = 0, setlist.liftStraighten = true;
					else if (monitor->physicalSize.widthCm * monitor->physicalSize.heightCm >= 1200) setlist.paintDevice = 0, setlist.liftStraighten = true;
					else setlist.paintDevice = 1;
				}
				else setlist.paintDevice = 1;
			}
			{
				HDC screenDC = GetDC(nullptr);
				double scale = 1.0;

				if (screenDC)
				{
					int dpiX = GetDeviceCaps(screenDC, LOGPIXELSX);
					ReleaseDC(nullptr, screenDC);

					// 转换为缩放倍率
					scale = static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
				}

				// 限制范围 1.0 ~ 1.5
				setlist.settingGlobalScale = static_cast<float>(clamp(scale, 1.0, 1.5));
			}
		}

		// 读取配置
		{
		#pragma region 新配置 Test

			config.ReadAll(); // 是否失败不重要（失败的情况可能是首次启动软件，导致配置文件尚未创建）
			InitializeEraserInputPreferences();
			{
				// 面积辅助是持久化实验选项，正式构建也恢复；控制台诊断仍独立控制。
				auto& host = Inkeys::Drawing::Draw3::ProductHost();
				auto options = host.EraserDevelopmentOptions();
				options.touchContactAreaAssistance =
					config.Experimental.Inkeys3.Draw3.TouchContactAreaAssistance;
				host.SetEraserDevelopmentOptions(options);
			}
		#ifndef IDT_RELEASE
			pptComConsoleOutputEnabled =
				config.Experimental.Inkeys3.ConsoleOutput.PptCOM;
			draw3ConsoleOutputEnabled =
				config.Experimental.Inkeys3.ConsoleOutput.Draw3;
			Inkeys::Drawing::Draw3::SetStartupEnvironmentDiagnosticsEnabled(
				draw3ConsoleOutputEnabled);
			touchAreaConsoleOutputEnabled =
				config.Experimental.Inkeys3.ConsoleOutput.TouchArea;
			cursorConsoleOutputEnabled =
				config.Experimental.Inkeys3.ConsoleOutput.Cursor;
			if (draw3ConsoleOutputEnabled || touchAreaConsoleOutputEnabled ||
				cursorConsoleOutputEnabled)
			{
				// 设备初始化前绑定共用控制台，避免遗漏显示/EDID 与 TouchAreaDevice 启动信息。
				InitializeDebugConsole();
			}
			Inkeys::Drawing::Draw3::SetCursorDiagnosticsEnabled(
				cursorConsoleOutputEnabled);
			if (touchAreaConsoleOutputEnabled)
			{
				auto& host = Inkeys::Drawing::Draw3::ProductHost();
				auto options = host.EraserDevelopmentOptions();
				options.touchAreaTrace = true;
				host.SetEraserDevelopmentOptions(options);
			}
		#endif
			double animationSpeedRate = static_cast<double>(
				config.Experimental.Inkeys3.UI3.Animation.SpeedRate);
			animationSpeedRate = isfinite(animationSpeedRate)
				? clamp(animationSpeedRate, 0.1, 5.0) : 1.0;
			config.Experimental.Inkeys3.UI3.Animation.SpeedRate = animationSpeedRate;
			if (!config.Write() && IDTLogger)
				IDTLogger->warn("[主线程][IdtMain] main.json 写回失败，保留现存配置文件");
			Inkeys::UI::Bar::SetAnimationOptions(
				static_cast<bool>(config.Experimental.Inkeys3.UI3.Animation.Enable),
				animationSpeedRate);
			Inkeys::UI::Bar::SetEdgeLightingOptions(
				config.Experimental.Inkeys3.UI3.EdgeLighting.Enable,
				config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic);
			Inkeys::UI::Bar::SetDebugOptions(
				config.Experimental.Inkeys3.UI3.Debug.Enable,
				config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate);
			// PPT 五窗复用 UI3 脏区诊断开关，但按设计不显示帧率。
			Inkeys::UI::Ppt::SetDebugEnabled(
				config.Experimental.Inkeys3.UI3.Debug.Enable);

			configOnce = Inkeys::config;

		#pragma endregion

			if (_waccess((globalPath + L"opt\\deploy.json").c_str(), 4) == -1)
			{
				IDTLogger->warn("[主线程][IdtMain] 配置信息不存在");

				// 联控测试：start 界面
				// StartForInkeys();
			}
			else ReadSetting();
			if (!WriteSetting() && IDTLogger)
				IDTLogger->warn("[主线程][IdtMain] deploy.json 写回失败，保留现存配置文件");
		}

		// 初次读取配置后的操作
		{
			// 开机自启设定
			{
				if (!isolatedStartupFailure)
				{
					bool isStartUp = QueryStartupState(GetCurrentExePath(), L"$Inkeys");
					if (isStartUp != setlist.startUp) SetStartupState(setlist.startUp, GetCurrentExePath(), L"$Inkeys");
				}
			}
			// 皮肤设定
			{
				if (setlist.SetSkinMode == 0) setlist.SkinMode = 1;
				else setlist.SkinMode = setlist.SetSkinMode;
			}
			// 崩溃选项设定
			if (!isolatedStartupFailure)
				CrashHandler::SetFlag(setlist.regularSetting.teachingSafetyMode);
		}
		// 配置修正
		{
			if (SuperTopFailSignal)
			{
				setlist.plugInSetting.superTop.enable = false;
				WriteSetting();
			}
		}

		// 非法语言值仅在本次运行中回退英语，避免覆盖用户磁盘配置。
		if (setlist.selectLanguage < 0 || setlist.selectLanguage > 2)
			setlist.selectLanguage = 0;

		IDTLogger->info("[主线程][IdtMain] 配置信息初始化完成");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::FullConfigReady);
	}
	// 显示快照发布后只桥接旧绘图尺度，UI 自身通过各自客户端处理布局。
	displaySubscription = Inkeys::Display::Subscribe([](Inkeys::Display::SnapshotPtr snapshot)
		{
			const auto* monitor = snapshot ? snapshot->Primary() : nullptr;
			if (!monitor)
			{
				drawingScale = 1.0F;
				stopTimingError = 5;
				return;
			}
			drawingScale = min(static_cast<float>(monitor->pixelWidth) / 1920.0F,
				static_cast<float>(monitor->pixelHeight) / 1080.0F);
			if (setlist.paintDevice == 1 || !monitor->physicalSize.available)
				stopTimingError = 5;
			else
				stopTimingError = min(0.3F * static_cast<float>(monitor->pixelWidth) /
					static_cast<float>(monitor->physicalSize.widthCm),
					0.5F * static_cast<float>(monitor->pixelHeight) /
					static_cast<float>(monitor->physicalSize.heightCm));
		});
	// I18N初始化
	{
		// 默认以 zh-CN 作为完整兜底语言，再叠加配置指定的语言
		if (setlist.selectLanguage == 1) I18n::load(1, L"JSON", L"zh-CN");
		else if (setlist.selectLanguage == 2) I18n::load(1, L"JSON", L"zh-TW");
		else I18n::load(1, L"JSON", L"en-US");

		IDTLogger->info("[主线程][IdtMain] I18N初始化完成");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::I18nReady);
	}
	// 插件初始化
	{
		// 桌面快捷方式初始化
		if (!isolatedStartupFailure) shortcutAssistant.SetShortcut();
		// 启动 DesktopDrawpadBlocker
		if (!isolatedStartupFailure) StartDesktopDrawpadBlocker();
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::PluginsReady);
	}

	// COM 清单加载
	{
		DWORD pptComFailureError = ERROR_SUCCESS;
		bool pptComFailureObserved = false;
		auto RecordPptComFailure = [&](DWORD error) noexcept
			{
				if (pptComFailureObserved) return;
				pptComFailureObserved = true;
				pptComFailureError = error;
				// 已知不能继续启动后先建立监督；异步文件日志可能阻塞。
				SetOffSignal(1);
			};
		//PptCOM 组件加载
		{
			const std::wstring pptComPath = globalPath + L"PptCOM.dll";
			const HANDLE verifiedPptCom = EnsureVerifiedEmbeddedResourceFile(
				pptComPath, L"DLL", MAKEINTRESOURCE(222));
			if (verifiedPptCom == INVALID_HANDLE_VALUE)
			{
				// 资源助手不承诺保留LastError；用通用失败码，避免记录陈旧的系统错误。
				RecordPptComFailure(ERROR_GEN_FAILURE);
				IDTLogger->error("[主线程][IdtMain] PptCOM.dll 验证或原子发布失败，拒绝加载");
			}

			ACTCTX actCtx = { 0 };
			actCtx.cbSize = sizeof(actCtx);
			actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
			actCtx.lpResourceName = MAKEINTRESOURCE(221);
			actCtx.hModule = GetModuleHandle(NULL);

			if (verifiedPptCom != INVALID_HANDLE_VALUE)
			{
				hActCtx = CreateActCtx(&actCtx);
				if (hActCtx == INVALID_HANDLE_VALUE) RecordPptComFailure(GetLastError());
			}
			if (hActCtx != INVALID_HANDLE_VALUE)
			{
				actCtxActivated = ActivateActCtx(hActCtx, &ulCookie) != FALSE;
				if (!actCtxActivated) RecordPptComFailure(GetLastError());
			}
			if (actCtxActivated)
			{
				pptComModule = LoadLibraryW(pptComPath.c_str());
				if (!pptComModule) RecordPptComFailure(GetLastError());
			}
			if (verifiedPptCom != INVALID_HANDLE_VALUE)
				CloseHandle(verifiedPptCom);
		}
		const bool isolatedPptComFailure = Inkeys::Shutdown::IsAuthorizedStartupFailure(
			Inkeys::Shutdown::StartupFailureSite::PptCom);
		if (!actCtxActivated || !pptComModule || isolatedPptComFailure)
		{
			// 先记录真实初始化结果；测试合成fatal的通用错误不能冒充DLL/activation原结果。
			const HRESULT actualPptComResult = actCtxActivated && pptComModule
				? S_OK : HRESULT_FROM_WIN32(pptComFailureObserved
					? pptComFailureError : ERROR_GEN_FAILURE);
			if (!pptComFailureObserved) RecordPptComFailure(ERROR_GEN_FAILURE);
			const DWORD originalError = pptComFailureError;
			if (isolatedPptComFailure)
			{
				Inkeys::Shutdown::PublishAuthorizedStartupFailure(
					Inkeys::Shutdown::StartupFailureSite::PptCom, 0xD005u, actualPptComResult);
				Inkeys::Shutdown::HoldAuthorizedStartupBoundary(0xD005u, 2);
			}
			IDTLogger->critical("[主线程][IdtMain] PptCOM activation 初始化失败, error={}",
				static_cast<unsigned long>(originalError));
			PublishFatalStartupFailure(0xD005u,
				L"PptCOM 组件初始化失败，程序无法继续启动。");
			if (pptComModule) FreeLibrary(pptComModule);
			if (actCtxActivated) DeactivateActCtx(0, ulCookie);
			if (hActCtx != INVALID_HANDLE_VALUE) ReleaseActCtx(hActCtx);
			if (SUCCEEDED(comInitializeResult)) CoUninitialize();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}

		IDTLogger->info("[主线程][IdtMain] COM初始化完成");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::PptComReady);
	}
	// 自动更新初始化
	{
	#ifdef IDT_RELEASE
		if (!isolatedStartupFailure) thread(AutomaticUpdate).detach();
	#endif
	}

	// 界面绘图库初始化
	{
		HRESULT hr = Inkeys::UI::RenderPipeline::Initialize();
		if (FAILED(hr))
		{
			SetOffSignal(1);
			if (IDTLogger) IDTLogger->error("[主线程][IdtMain] 界面绘图库初始化失败, hr=0x{:08X}", static_cast<unsigned int>(hr));
			PublishFatalStartupFailure(0xD002u,
				L"共享渲染管线初始化失败，程序无法继续启动。");
			return 0;
		}
		if (hr == S_OK)
		{
			ReportStartupMilestoneForManualTest(
				Inkeys::Startup::Milestone::RenderFactoriesReady);
			ReportStartupMilestoneForManualTest(
				Inkeys::Startup::Milestone::RenderDeviceReady);
			ReportStartupMilestoneForManualTest(
				Inkeys::Startup::Milestone::RenderSchedulerReady);
		}

		IDTLogger->info("[主线程][IdtMain] 界面绘图库初始化完成");
	}
	// 字体初始化
	{
		{
			vector<UINT> fontResourceIDs;
			fontResourceIDs.emplace_back(IDR_TTF1); // HarmonyOS Sans SC
			fontResourceIDs.emplace_back(IDR_TTF7); // HarmonyOS Sans SC Bold
			fontResourceIDs.emplace_back(IDR_TTF3); // HarmonyOS Sans TC
			fontResourceIDs.emplace_back(IDR_TTF8); // HarmonyOS Sans TC Bold

			IdtFontFileLoader::IsLoaderInitialized();
			IdtFontCollectionLoader::IsLoaderInitialized();

			const HRESULT fontHr = Inkeys::UI::RenderPipeline::InitializeFontCollection(
				IdtFontFileLoader::GetLoader(),
				IdtFontCollectionLoader::GetLoader(), fontResourceIDs);
			const bool isolatedFontFailure = Inkeys::Shutdown::IsAuthorizedStartupFailure(
				Inkeys::Shutdown::StartupFailureSite::Font);
			if (FAILED(fontHr) || isolatedFontFailure)
			{
				SetOffSignal(1);
				if (isolatedFontFailure)
				{
					Inkeys::Shutdown::PublishAuthorizedStartupFailure(
						Inkeys::Shutdown::StartupFailureSite::Font, 0xD003u, fontHr);
					Inkeys::Shutdown::HoldAuthorizedStartupBoundary(0xD003u, 2);
				}
				if (IDTLogger) IDTLogger->error(
					"[主线程][IdtMain] UI 字体集合初始化失败, hr=0x{:08X}",
					static_cast<unsigned int>(fontHr));
				PublishFatalStartupFailure(0xD003u,
					L"UI 字体资源初始化失败，程序无法继续启动。");
				Inkeys::UI::RenderPipeline::Shutdown();
				return 0;
			}

			// ----- 开始诊断代码 -----
			/*if (auto dWriteFontCollection = Inkeys::UI::RenderPipeline::FontCollection())
			{
				UINT32 familyCount = dWriteFontCollection->GetFontFamilyCount();
				wchar_t buffer[256];
				swprintf_s(buffer, L"诊断：找到 %u 个字体家族。\n", familyCount);
				Testw(buffer);

				for (UINT32 i = 0; i < familyCount; ++i)
				{
					ComPtr<IDWriteFontFamily> fontFamily;
					HRESULT hr = dWriteFontCollection->GetFontFamily(i, &fontFamily);
					if (FAILED(hr)) continue;

					// 获取家族名
					ComPtr<IDWriteLocalizedStrings> familyNames;
					hr = fontFamily->GetFamilyNames(&familyNames);
					if (FAILED(hr)) continue;

					UINT32 index = 0;
					BOOL exists = false;
					hr = familyNames->FindLocaleName(L"en-us", &index, &exists);
					if (FAILED(hr) || !exists) index = 0;

					UINT32 length = 0;
					hr = familyNames->GetStringLength(index, &length);
					if (FAILED(hr)) continue;

					std::wstring name(length + 1, L'\0');
					hr = familyNames->GetString(index, &name[0], length + 1);
					if (FAILED(hr)) continue;

					swprintf_s(buffer, L"字体家族 %u 的名称是: \"%s\"\n", i, name.c_str());
					Testw(buffer);

					// ==== 检测家族内的字体字重 ====
					UINT32 fontCount = fontFamily->GetFontCount();
					for (UINT32 j = 0; j < fontCount; ++j)
					{
						ComPtr<IDWriteFont> font;
						hr = fontFamily->GetFont(j, &font);
						if (FAILED(hr)) continue;

						DWRITE_FONT_WEIGHT weight = font->GetWeight();
						DWRITE_FONT_STYLE  style = font->GetStyle();
						DWRITE_FONT_STRETCH stretch = font->GetStretch();

						swprintf_s(buffer, L"  样式 %u: Weight=%u, Style=%u, Stretch=%u\n",
							j, weight, style, stretch);
						Testw(buffer);
					}
				}
			}*/
		}

		IDTLogger->info("[主线程][IdtMain] 字体初始化完成");
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::FontsReady);
	}

	// 窗口
	{
		wstring ClassName;
		if (userId == L"Error") ClassName = L"Inkeys";
		else ClassName = userId;

		// 窗口创建完成后处理的
		auto disableGestureFuc = [&](HWND hWnd) -> void
			{
				Inkeys::Gesture::DisableEdgeGestures(hWnd, true);
			};
		auto touchRegisterFuc = [&](HWND hWnd) -> void
			{
				RegisterTouchWindow(hWnd, 0);
				disableGestureFuc(hWnd);
			};

		SettingWindowBegin();

		const DWORD overlayStyle = WS_POPUP | WS_CLIPCHILDREN;
		const DWORD overlayExStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
		const auto displaySnapshot = Inkeys::Display::GetSnapshot();
		const auto* primaryMonitor = displaySnapshot ? displaySnapshot->Primary() : nullptr;
		const int overlayWidth = primaryMonitor ? primaryMonitor->pixelWidth : GetSystemMetrics(SM_CXSCREEN);
		const int overlayHeight = primaryMonitor ? primaryMonitor->pixelHeight : GetSystemMetrics(SM_CYSCREEN);
		HICON applicationIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_ICON1));
		std::vector<Inkeys::Window::WindowSpec> windowSpecs;
		auto createdDrawpadHwnd = std::make_shared<std::atomic<HWND>>(nullptr);
		const bool preferDraw3DirectComposition =
			config.Experimental.Inkeys3.Draw3.EnableDirectComposition &&
			Inkeys::Drawing::Draw3::ShouldPreconfigureNoRedirectionBitmap();

		// 放大窗口固定保留 1 像素，不再读取已废弃的 AvoidFullScreen 配置。
		Inkeys::Window::WindowSpec magnifierHost;
		magnifierHost.role = Inkeys::Window::WindowRole::MagnifierHost;
		magnifierHost.className = L"Inkeys6;" + ClassName;
		magnifierHost.title = L"Inkeys6 MagnifierHostWindow";
		magnifierHost.width = GetSystemMetrics(SM_CXSCREEN);
		magnifierHost.height = GetSystemMetrics(SM_CYSCREEN) - 1;
		magnifierHost.style = WS_POPUP | WS_CLIPCHILDREN;
		magnifierHost.exStyle = overlayExStyle;
		magnifierHost.windowProc = MagnifierHostWindowWndProc;
		magnifierHost.optional = true;
		magnifierHost.bindMessages = false;
		magnifierHost.beforeCreate = PrepareMagnifierWindow;
		magnifierHost.created = MagnifierHostCreated;
		magnifierHost.destroyed = ShutdownMagnifierWindow;
		windowSpecs.push_back(std::move(magnifierHost));

		Inkeys::Window::WindowSpec magnifierChildSpec;
		magnifierChildSpec.role = Inkeys::Window::WindowRole::MagnifierChild;
		magnifierChildSpec.className = WC_MAGNIFIER;
		magnifierChildSpec.title = L"Inkeys Screen Magnifier";
		magnifierChildSpec.width = GetSystemMetrics(SM_CXSCREEN);
		magnifierChildSpec.height = GetSystemMetrics(SM_CYSCREEN) - 1;
		magnifierChildSpec.style = WS_CHILD | WS_VISIBLE | MS_CLIPAROUNDCURSOR;
		magnifierChildSpec.exStyle = WS_EX_NOACTIVATE;
		magnifierChildSpec.optional = true;
		magnifierChildSpec.bindMessages = false;
		magnifierChildSpec.created = MagnifierChildCreated;
		windowSpecs.push_back(std::move(magnifierChildSpec));

		auto AddOverlayWindow = [&](Inkeys::Window::WindowRole role, const wchar_t* suffix,
			const wchar_t* title, WNDPROC proc, DWORD extraStyle, const function<void(HWND)>& created)
			{
				Inkeys::Window::WindowSpec spec;
				spec.role = role;
				spec.className = wstring(suffix) + ClassName;
				spec.title = title;
				spec.width = overlayWidth;
				spec.height = overlayHeight;
				spec.style = overlayStyle;
				spec.exStyle = overlayExStyle | extraStyle;
				if (role == Inkeys::Window::WindowRole::DrawpadPresentation)
				{
					// 选择态仅用此窗口 ULW 呈现；固定透明样式不能随模式切换。
					spec.exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT |
						WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
					spec.bindMessages = false;
				}
				else if (role == Inkeys::Window::WindowRole::Drawpad)
				{
					// DComp 的不可变样式只在能力探测通过时预置；否则创建可用于 ULW 的 HWND。
					spec.exStyle &= ~(WS_EX_LAYERED | WS_EX_TRANSPARENT);
					if (preferDraw3DirectComposition)
						spec.exStyle |= WS_EX_NOREDIRECTIONBITMAP;
				}
				spec.windowProc = proc;
				spec.created = created;
				if (role >= Inkeys::Window::WindowRole::PptBottomLeft &&
					role <= Inkeys::Window::WindowRole::PptMiddleRight)
				{
					// 底部共享窗预留白板 2x2 形态容量，透明区由 PageControl 穿透命中。
					spec.width = role <= Inkeys::Window::WindowRole::PptBottomRight
						? 230 : 43;
					spec.height = role <= Inkeys::Window::WindowRole::PptBottomRight
						? 80 : 165;
					spec.bindMessages = false;
				}
				else if (role == Inkeys::Window::WindowRole::Bar)
				{
					// Bar 的 HWND 会动态改变，鼠标坐标必须在入队时固化到布局空间。
					spec.messageCallback = [](HWND hwnd, UINT message,
						WPARAM wParam, LPARAM lParam)
						{
							return Inkeys::UI::Bar::QueueWindowMessageInLayoutSpace(
								hwnd, message, wParam, lParam);
						};
				}
				windowSpecs.push_back(std::move(spec));
			};
		AddOverlayWindow(Inkeys::Window::WindowRole::Freeze, L"Inkeys1;", L"Inkeys FreezeWindow",
			nullptr, WS_EX_TRANSPARENT, {});
		AddOverlayWindow(Inkeys::Window::WindowRole::DrawpadPresentation,
			L"Inkeys2.Presentation;", L"Inkeys DrawpadPresentationWindow",
			DefWindowProcW, 0, {});
		AddOverlayWindow(Inkeys::Window::WindowRole::Drawpad, L"Inkeys2;", L"Inkeys DrawpadWindow",
			DrawpadMsgCallback, 0, [createdDrawpadHwnd, disableGestureFuc](HWND hwnd)
			{
				// created 回调只发布 HWND；Draw3 在 Window Service 完成创建后再同步启动。
				disableGestureFuc(hwnd);
				createdDrawpadHwnd->store(hwnd, std::memory_order_release);
			});
		AddOverlayWindow(Inkeys::Window::WindowRole::PptBottomLeft,
			L"Inkeys4.BottomLeft;", L"Inkeys Ppt Bottom Left",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::PptBottomRight,
			L"Inkeys4.BottomRight;", L"Inkeys Ppt Bottom Right",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::PptMiddleLeft,
			L"Inkeys4.MiddleLeft;", L"Inkeys Ppt Middle Left",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::PptMiddleRight,
			L"Inkeys4.MiddleRight;", L"Inkeys Ppt Middle Right",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::Bar, L"Inkeys5;", L"Inkeys BarWindow",
			Inkeys::UI::Bar::WindowProc(), 0, touchRegisterFuc);

		Inkeys::Window::WindowSpec settingSpec;
		settingSpec.role = Inkeys::Window::WindowRole::Setting;
		settingSpec.className = L"Inkeys.Setting;" + ClassName;
		settingSpec.title = L"Inkeys Settings";
		settingSpec.x = SettingWindowX;
		settingSpec.y = SettingWindowY;
		settingSpec.width = SettingWindowWidth;
		settingSpec.height = SettingWindowHeight;
		settingSpec.style = WS_POPUP | WS_CLIPCHILDREN;
		settingSpec.exStyle = WS_EX_APPWINDOW;
		settingSpec.windowProc = Inkeys::UI::Setting::WindowProc();
		settingSpec.largeIcon = applicationIcon;
		settingSpec.smallIcon = applicationIcon;
		settingSpec.cursor = LoadCursorW(nullptr, IDC_ARROW);
		settingSpec.bindMessages = false;
		windowSpecs.push_back(std::move(settingSpec));

		Inkeys::Window::WindowSpec displayObserver;
		displayObserver.role = Inkeys::Window::WindowRole::DisplayObserver;
		displayObserver.className = L"Inkeys.DisplayObserver;" + ClassName;
		displayObserver.title = L"Inkeys Display Observer";
		displayObserver.width = 1;
		displayObserver.height = 1;
		displayObserver.windowProc = Inkeys::Display::WindowProc();
		displayObserver.bindMessages = false;
		windowSpecs.push_back(std::move(displayObserver));
		if (isolatedStartupFailure)
		{
			// 私有启动故障child的所有窗口留在屏幕外，避免透明画布拦截真实桌面。
			for (auto& spec : windowSpecs)
			{
				spec.x = -32000;
				spec.y = -32000;
				spec.visible = false;
			}
		}

		auto& windowService = Inkeys::Window::GetService();
		auto RefreshWindowHandles = [&]()
			{
				magnifierWindow = windowService.Handle(Inkeys::Window::WindowRole::MagnifierHost);
				magnifierChild = windowService.Handle(Inkeys::Window::WindowRole::MagnifierChild);
				freeze_window = windowService.Handle(Inkeys::Window::WindowRole::Freeze);
				drawpad_window = createdDrawpadHwnd->load(std::memory_order_acquire);
				floating_window = windowService.Handle(Inkeys::Window::WindowRole::Bar);
				setting_window = windowService.Handle(Inkeys::Window::WindowRole::Setting);
			};
		auto StartWindowService = [&]()
			{
				createdDrawpadHwnd->store(nullptr, std::memory_order_release);
				Inkeys::Shutdown::FailedCleanupDeadline windowCleanup(
					&Inkeys::Shutdown::PublishFatalFailedCleanupNoWait);
				windowCleanup.PrepareOrFatal();
				const bool started = windowService.Start(windowSpecs, windowCleanup.Signal());
				windowCleanup.CompleteOrFatal(); // owner清理及monitor均真实结束后才复用窗口链。
				if (started) RefreshWindowHandles();
				return started && drawpad_window && windowService.Handle(
					Inkeys::Window::WindowRole::DrawpadPresentation);
			};
		if (!StartWindowService())
		{
			// 启动失败也必须先建立退场监督，错误提示或窗口 join 不能阻塞 15 秒保护。
			SetOffSignal(1);
			IDTLogger->critical("[主线程][IdtMain] Win32 窗口服务启动失败");
			PublishFatalStartupFailure(0xD101u,
				L"窗口服务初始化失败，程序无法继续启动。");
			windowService.StopAndJoin();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::WindowOverlayReady);
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::WindowSettingReady);
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::WindowServiceReady);
		// Draw3 只附着 Window Service 已创建的 HWND；样式变更仍回到 owner thread。
		Inkeys::Drawing::Draw3::HostStyleCallbacks draw3StyleCallbacks{
			&windowService,
			[](void* context, DWORD setMask, DWORD clearMask) -> bool
			{
				auto* service = static_cast<Inkeys::Window::Service*>(context);
				return service && service->SetExtendedStyleFlags(
					Inkeys::Window::WindowRole::Drawpad, setMask, clearMask);
			}
		};
		Inkeys::Drawing::Draw3::HostStartOptions draw3StartOptions{};
		draw3StartOptions.allowDirectComposition = preferDraw3DirectComposition;
		draw3StartOptions.autoSaveRoot =
			GetCurrentExeDirectory() + L"\\Inkeys\\AutoSave";
		draw3StartOptions.startupMilestone = [](void*,
			Inkeys::Drawing::Draw3::HostStartupStage stage) noexcept
			{
				using Stage = Inkeys::Drawing::Draw3::HostStartupStage;
				using Milestone = Inkeys::Startup::Milestone;
				switch (stage)
				{
				case Stage::WindowAttached: (void)Inkeys::Startup::Report(
					Milestone::Draw3WindowAttached); break;
				case Stage::GraphicsReady: (void)Inkeys::Startup::Report(
					Milestone::Draw3GraphicsReady); break;
				case Stage::PresenterReady: (void)Inkeys::Startup::Report(
					Milestone::Draw3PresenterReady); break;
				case Stage::RtsReady: (void)Inkeys::Startup::Report(
					Milestone::Draw3RtsReady); break;
				case Stage::ControllerReady: (void)Inkeys::Startup::Report(
					Milestone::Draw3ControllerReady); break;
				case Stage::FirstFrameCommitted: (void)Inkeys::Startup::Report(
					Milestone::Draw3FirstFrameCommitted); break;
				default: break;
				}
			};
		Inkeys::Drawing::Draw3::HostRuntimeCallbacks draw3RuntimeCallbacks{
			nullptr,
			[](void*, bool active) noexcept
			{
				if (active) Inkeys::UI::Bar::NotifyCanvasDrawingStarted();
				else Inkeys::UI::Bar::NotifyCanvasDrawingEnded();
			}
		};
		bool draw3Started = StartDraw3ProductWithFallback(windowService, windowSpecs,
			drawpad_window, draw3StyleCallbacks, draw3StartOptions, draw3RuntimeCallbacks,
			preferDraw3DirectComposition, StartWindowService);
		if (!draw3Started)
		{
			// Host 初始化失败后的提示和窗口清理可能依赖已停止的绘制线程，先建立监督。
			SetOffSignal(1);
			IDTLogger->critical("[主线程][IdtMain] Draw3 Host 初始化失败");
			PublishFatalStartupFailure(0xD201u,
				L"Draw3 绘图服务初始化失败，程序无法继续启动。");
			windowService.StopAndJoin();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}
		if (!setting_window || !Inkeys::UI::Setting::Initialize())
		{
			SetOffSignal(1);
			IDTLogger->critical("[主线程][IdtMain] Setting 渲染客户端初始化失败");
			PublishFatalStartupFailure(0xD301u,
				L"设置界面初始化失败，程序无法继续启动。");
			Inkeys::Drawing::Draw3::StopProduct();
			windowService.StopAndJoin();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::SettingReady);
		if (whiteboardFeatureEnabled && !Inkeys::UI::Whiteboard::Initialize({
			[] { RequestWhiteboardPreviousPage(); },
			[] { RequestWhiteboardNextPage(); },
			}))
		{
			SetOffSignal(1);
			IDTLogger->critical("[主线程][IdtMain] Whiteboard 渲染客户端初始化失败");
			PublishFatalStartupFailure(0xD401u,
				L"白板界面初始化失败，程序无法继续启动。");
			Inkeys::UI::Setting::Shutdown();
			Inkeys::Drawing::Draw3::StopProduct();
			windowService.StopAndJoin();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}
		// 发布前临时跳过白板 UI 初始化，但仍完成既有启动进度契约。
		ReportStartupMilestoneForManualTest(
			Inkeys::Startup::Milestone::WhiteboardReady);
		// Host 启动会清空桥接队列；首帧前重新发布当前 UI 状态。
		SyncDraw3State();
		// 首帧完成后按“模式 + 当前页内容”决定显隐；初始空选择页保持隐藏。
		if (!Inkeys::Drawing::Draw3::ProductFirstFrameReady())
		{
			SetOffSignal(1);
			IDTLogger->critical("[主线程][IdtMain] Draw3 首帧准备失败");
			PublishFatalStartupFailure(0xD202u,
				L"Draw3 首帧提交失败，程序无法继续启动。");
			if (whiteboardFeatureEnabled) Inkeys::UI::Whiteboard::Shutdown();
			Inkeys::Drawing::Draw3::StopProduct();
			windowService.StopAndJoin();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}
		ReconcileDraw3Presentation();
		magnificationCreateReady = magnificationCreateReady &&
			magnifierWindow && magnifierChild;

		// 只提升 owner 链根，由 Win32 维护其余覆盖层的相对 Z 序。
		windowService.SetTopmostRefreshObserver([]
			{
				// observer 不同步反调 Window Service，只向 Preview owner 异步 post。
				Inkeys::UI::StartupPreview::RevalidateTopmost();
			});
		if (windowService.RequestTopmostRefresh())
			ReportStartupMilestoneForManualTest(
				Inkeys::Startup::Milestone::InitialTopmostRefresh);
		else
		{
			SetOffSignal(1);
			windowService.SetTopmostRefreshObserver({});
			PublishFatalStartupFailure(0xD102u,
				L"窗口层级初始化失败，程序无法继续启动。");
			if (whiteboardFeatureEnabled) Inkeys::UI::Whiteboard::Shutdown();
			Inkeys::UI::Setting::Shutdown();
			Inkeys::Drawing::Draw3::StopProduct();
			windowService.StopAndJoin();
			Inkeys::UI::RenderPipeline::Shutdown();
			return 1;
		}

		IDTLogger->info("[主线程][IdtMain] 窗口初始化完成");
	}
	// Draw3 Host 已初始化唯一 RTS producer；不再启动 Draw2 RTS/速度线程。
	rtsWait = false;
#pragma region 线程

	jthread topWindowThread;
	jthread ui3InitializationThread;
	jthread freezeFrameThread;
	jthread stateMonitoringThread;
	if (!isolatedStartupFailure)
	{
		topWindowThread = jthread(TopWindow);
		ui3InitializationThread = jthread(Inkeys::UI::Bar::Initialization);
		freezeFrameThread = jthread(FreezeFrameWindow);
		stateMonitoringThread = jthread(StateMonitoring);
	}
	else if (Inkeys::Shutdown::IsAuthorizedStartupFailure(
		Inkeys::Shutdown::StartupFailureSite::BarState))
	{
		// 仅私有child发布真实状态，让Main处理B002；不冒充Bar Register自然失败。
		Inkeys::UI::StartupPreview::SetBarStartupState(
			Inkeys::UI::StartupPreview::BarStartupState::ClientRegistrationFailed);
		Inkeys::Shutdown::PublishAuthorizedStartupFailure(
			Inkeys::Shutdown::StartupFailureSite::BarState, 0xB002u, S_OK);
	}

	// 放大API
	// 即使 Magnification 资源创建失败也启动协调器，使开启请求能明确失败并条件回退。
	jthread magnifierThread;
	if (!isolatedStartupFailure) magnifierThread = jthread(MagnifierThread);

	// 启动 PPT 联动插件
	#ifndef IDT_RELEASE
	if (pptComConsoleOutputEnabled && !draw3ConsoleOutputEnabled &&
		!touchAreaConsoleOutputEnabled && !cursorConsoleOutputEnabled)
	{
		// 仅开启 PptCOM 时延后分配，避免带出 Draw3 的启动诊断。
		InitializeDebugConsole();
	}
	#endif
	jthread pptLinkageThread;
	if (!isolatedStartupFailure) pptLinkageThread = jthread(PPTLinkageMain);

	IDTLogger->info("[主线程][IdtMain] 线程初始化完成");

#pragma endregion

	IDTLogger->info("[主线程][IdtMain] 开始等待关闭程序信号发出");

	int developerModeExitCode = 0;
	while (!offSignal)
	{
		this_thread::sleep_for(chrono::milliseconds(100));
		// Bar 可能异步发布 B001/B002；先接受退场意图，再进入配置写回等业务工作。
		if (Inkeys::Startup::ActiveSnapshot().failed) SetOffSignal(1);
		if (!offSignal && !Inkeys::Drawing::Draw3::ProductRunning())
		{
			// 意外失去绘图消费者时先阻断旧可见性请求，再由窗口 owner 成对撤下画布。
			SetOffSignal(1);
			auto& service = Inkeys::Window::GetService();
			const bool pairedHidden = service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden);
			const bool allHidden = service.HideAllUserWindows();
			const HWND primary = service.Handle(Inkeys::Window::WindowRole::Drawpad);
			const HWND presentation = service.Handle(
				Inkeys::Window::WindowRole::DrawpadPresentation);
			const bool surfacesHidden = (!primary || !IsWindow(primary) ||
				!IsWindowVisible(primary)) &&
				(!presentation || !IsWindow(presentation) ||
					!IsWindowVisible(presentation));
			GUITHREADINFO ownerInfo{ sizeof(GUITHREADINFO) };
			const DWORD ownerThread = service.OwnerThreadId(
				Inkeys::Window::WindowRole::Drawpad);
			const bool captureReleased = pairedHidden ||
				(ownerThread && GetGUIThreadInfo(ownerThread, &ownerInfo) &&
					ownerInfo.hwndCapture != primary);
			if (IDTLogger) IDTLogger->critical(
				"[主线程][IdtMain] Draw3 Host 意外停止，受控退出；成对切换={}，全窗口命令={}，双画布读回={}，捕获释放={}",
				pairedHidden, allHidden, surfacesHidden, captureReleased);
			developerModeExitCode = 6;
			break;
		}
		// Bar 渲染线程只发布有限 DIP；主线程沿既有配置路径去重落盘。
		double committedStartupWidthDip = 0.0;
		if (Inkeys::UI::StartupPreview::TakeCommittedStartupBarWidthDip(
			committedStartupWidthDip)
			&& Inkeys::UI::StartupPreview::ShouldWriteCachedStartupBarWidthDip(
				config.Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip,
				committedStartupWidthDip))
		{
			config.Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip =
				committedStartupWidthDip;
			if (!config.Write() && IDTLogger)
				IDTLogger->warn(
					"[主线程][IdtMain] 启动占位总宽度写回失败，本次启动继续运行");
		}
		if (startupPreviewSmokeRequested)
		{
			const auto preview = Inkeys::UI::StartupPreview::SnapshotDiagnostics();
			const auto alpha = Inkeys::UI::Bar::SnapshotPresentationAlphaDiagnostics();
			const bool completed = preview.firstFrameCommitted
				&& preview.previewFadeOutCommitted
				&& preview.barAlpha0Committed && preview.barAlpha255Committed
				&& alpha.transparentCommitted && alpha.opaqueCommitted
				&& preview.automaticStopPosted && preview.ownerThreadExited
				&& preview.previewInactive && alpha.committed == 255;
			const bool timedOut = chrono::steady_clock::now() - startupT0
				>= chrono::seconds(20);
			if (completed || timedOut)
			{
				if (timedOut && !completed)
				{
					developerModeExitCode = 5;
					if (IDTLogger) IDTLogger->error(
						"[主线程][IdtMain] StartupPreview smoke 超时");
				}
				(void)WriteStartupPreviewSmokeReport(
					startupPreviewSmokePath, completed, preview, alpha);
				SetOffSignal(1);
				break;
			}
		}
		const auto startupSnapshot = Inkeys::Startup::ActiveSnapshot();
		if (!startupSnapshot.failed) continue;
		SetOffSignal(1);
		Inkeys::UI::StartupPreview::RequestFailureFrame();
		(void)Inkeys::UI::StartupPreview::WaitForFailureFrame(
			chrono::milliseconds(350));
		const std::wstring message = L"启动阶段发生致命错误（代码 "
			+ std::to_wstring(startupSnapshot.failureCode)
			+ L"），程序将安全退出。";
		ShowStartupMessage(message.c_str());
		Inkeys::UI::StartupPreview::RequestFadeOutForExit();
		(void)Inkeys::UI::StartupPreview::WaitForFadeOut(
			chrono::milliseconds(500));
		Inkeys::UI::StartupPreview::Stop();
		break;
	}
	// Preview 先停止接收 render callback，再注销并销毁 owner HWND。
	Inkeys::Window::GetService().SetTopmostRefreshObserver({});
	Inkeys::UI::StartupPreview::Stop();
	// 先同步注销 Setting，避免窗口和共享设备释放后仍有绘制回调。
	if (whiteboardFeatureEnabled) Inkeys::UI::Whiteboard::Shutdown();
	Inkeys::UI::Setting::Shutdown();
	if (ui3InitializationThread.joinable()) ui3InitializationThread.join();
	if (freezeFrameThread.joinable()) freezeFrameThread.join();
	if (stateMonitoringThread.joinable()) stateMonitoringThread.join();
	if (magnifierThread.joinable()) magnifierThread.join();
	if (pptLinkageThread.joinable()) pptLinkageThread.join();
	if (topWindowThread.joinable()) topWindowThread.join();
	// 先停止 Draw3 设备/RTS，再由 Window Service 销毁其拥有的 Drawpad HWND。
	Inkeys::Drawing::Draw3::StopProduct();
	Inkeys::Window::GetService().StopAndJoin();
	Inkeys::UI::RenderPipeline::Shutdown();
	displaySubscription.Reset();
	Inkeys::Display::Shutdown();

	IDTLogger->info("[主线程][IdtMain] 等待各函数线程结束");

	{
		using namespace Inkeys::Thread;

		int WaitingCount = 0;
		for (; WaitingCount < 20; WaitingCount++)
		{
			if (!GetStatus("FreezeFrameWindow") && !GetStatus("NetUpdate") && !GetStatus("PPTLinkageMain")) break;
			this_thread::sleep_for(chrono::milliseconds(500));
		}
		if (WaitingCount >= 20) IDTLogger->warn("[主线程][IdtMain] 结束函数线程超时并强制结束线程");
	}

	// 反初始化 COM 环境
	{
		if (pptComModule) FreeLibrary(pptComModule);
		if (actCtxActivated) DeactivateActCtx(0, ulCookie);
		if (hActCtx != INVALID_HANDLE_VALUE) ReleaseActCtx(hActCtx);
		if (SUCCEEDED(comInitializeResult)) CoUninitialize();

		IDTLogger->info("[主线程][IdtMain] 反初始化 COM 环境完成");
	}
	// 释放互斥体
	if (launchMutex)
	{
		// ReleaseMutex(hMutex); // 调用 CloseHandle 通常就够了，因为我们是创建者且未等待它
		CloseHandle(launchMutex);
		launchMutex = NULL;
	}

	IDTLogger->info("[主线程][IdtMain] 已结束智绘教所有线程并关闭程序");

	return developerModeExitCode;
}

// 调测专用
#ifndef IDT_RELEASE
void Test()
{
	MessageBoxW(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL);
}
void Testb(bool t)
{
	MessageBoxW(NULL, t ? L"true" : L"false", L"真否标记", MB_OK | MB_SYSTEMMODAL);
}
void Testi(long long t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL);
}
void Testd(double t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"浮点标记", MB_OK | MB_SYSTEMMODAL);
}
void Testw(wstring t)
{
	MessageBoxW(NULL, t.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}
void Testa(string t)
{
	MessageBoxW(NULL, utf8ToUtf16(t).c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}
#endif
