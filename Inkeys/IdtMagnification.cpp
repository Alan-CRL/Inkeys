#include "IdtMagnification.h"
#include <winuser.h>

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
#include <spdlog/sinks/null_sink.h>
#endif
#include <cstdint>
#include <optional>
#include <vector>

import Inkeys.UI.Freeze;
import Inkeys.Window;

HWND magnifierWindow, magnifierChild;
Inkeys::Graphics::DibSurface MagnificationBackground;

bool magnificationCreateReady;

shared_mutex MagnificationBackgroundSm;
RECT hostWindowRect;

namespace
{
	using Inkeys::UI::Freeze::MagnifierInternal::Coordinator;
	using Inkeys::UI::Freeze::MagnifierInternal::Request;
	using Inkeys::UI::Freeze::MagnifierInternal::Stage;
	using Inkeys::UI::Freeze::MagnifierInternal::TransactionResult;
	using Inkeys::Window::WindowRole;

	Coordinator magnifierCoordinator;

	void TraceMagnifierApi(const char* name, HWND hwnd, bool succeeded) noexcept;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	constexpr UINT magnifierTestMessage = WM_APP + 0x4B2;
	constexpr UINT_PTR magnifierTestTimer = 0x4B3;
	void StepMagnifierPrintWindowTest(HWND host) noexcept;
#endif

	[[nodiscard]] const char* StageName(Stage stage) noexcept
	{
		switch (stage)
		{
		case Stage::Applied: return "applied";
		case Stage::Hidden: return "hidden";
		case Stage::Superseded: return "superseded";
		case Stage::Stopped: return "stopped";
		case Stage::InvalidTarget: return "invalid-target";
		case Stage::PrepareHidden: return "prepare-hidden";
		case Stage::Filter: return "filter";
		case Stage::Source: return "source";
		case Stage::Invalidate: return "invalidate";
		case Stage::Redraw: return "redraw";
		case Stage::Reveal: return "reveal";
		case Stage::Conceal: return "conceal";
		}
		return "unknown";
	}

	[[nodiscard]] bool IsCurrentProcessWindow(HWND hwnd) noexcept
	{
		if (!hwnd || !IsWindow(hwnd)) return false;
		DWORD processId = 0;
		return GetWindowThreadProcessId(hwnd, &processId) != 0 &&
			processId == GetCurrentProcessId();
	}

	struct MagnifierTargets
	{
		HWND host = nullptr;
		HWND child = nullptr;
	};

	[[nodiscard]] MagnifierTargets CurrentTargets() noexcept
	{
		const auto& service = Inkeys::Window::GetService();
		return {
			service.Handle(WindowRole::MagnifierHost),
			service.Handle(WindowRole::MagnifierChild),
		};
	}

	[[nodiscard]] bool AreCurrentTargets(const MagnifierTargets& targets) noexcept
	{
		if (!magnificationCreateReady || !IsCurrentProcessWindow(targets.host) ||
			!IsCurrentProcessWindow(targets.child))
			return false;
		const auto current = CurrentTargets();
		return current.host == targets.host && current.child == targets.child &&
			(GetWindowLongPtrW(targets.child, GWL_STYLE) & WS_CHILD) != 0 &&
			GetParent(targets.child) == targets.host;
	}

	[[nodiscard]] std::vector<HWND> BuildCurrentFilterList()
	{
		static constexpr std::array<WindowRole, 10> filterRoles = {
			WindowRole::MagnifierHost,
			WindowRole::Freeze,
			WindowRole::DrawpadPresentation,
			WindowRole::Drawpad,
			WindowRole::PptBottomLeft,
			WindowRole::PptBottomRight,
			WindowRole::PptMiddleLeft,
			WindowRole::PptMiddleRight,
			WindowRole::Bar,
			WindowRole::Setting,
		};

		const auto& service = Inkeys::Window::GetService();
		std::array<HWND, filterRoles.size()> candidates{};
		for (std::size_t index = 0; index < filterRoles.size(); ++index)
			candidates[index] = service.Handle(filterRoles[index]);

		return Inkeys::UI::Freeze::MagnifierInternal::BuildFilterList<HWND>(
			candidates, GetCurrentProcessId(),
			[](HWND hwnd) noexcept { return IsWindow(hwnd) != FALSE; },
			[](HWND hwnd) noexcept -> std::uint32_t
			{
				DWORD processId = 0;
				return GetWindowThreadProcessId(hwnd, &processId) ? processId : 0;
			},
			[](HWND hwnd) noexcept
			{
				return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0;
			});
	}

	class Win32MagnifierOperations
	{
	public:
		explicit Win32MagnifierOperations(MagnifierTargets targets) noexcept
			: targets_(targets)
		{
		}

		[[nodiscard]] bool TargetsCurrent() const noexcept
		{
			return AreCurrentTargets(targets_);
		}

		[[nodiscard]] bool PrepareHidden() noexcept
		{
			if (!SetLayeredWindowAttributes(targets_.host, 0, 0, LWA_ALPHA))
				return false;
			auto& service = Inkeys::Window::GetService();
			const bool hostShown = service.Show(WindowRole::MagnifierHost);
			const bool childShown = service.Show(WindowRole::MagnifierChild);
			return hostShown && childShown && TargetsCurrent() &&
				IsWindowVisible(targets_.host) && IsWindowVisible(targets_.child);
		}

		[[nodiscard]] bool SubmitFilter() noexcept
		{
			// 每次 source 前从 Window Service 重建完整列表，不缓存可能重建的 HWND。
			std::vector<HWND> hwndList = BuildCurrentFilterList();
			const bool succeeded = MagSetWindowFilterList(targets_.child, MW_FILTERMODE_EXCLUDE,
				static_cast<int>(hwndList.size()), hwndList.data()) != FALSE;
			TraceMagnifierApi("MagSetWindowFilterList", targets_.child, succeeded);
			return succeeded;
		}

		[[nodiscard]] bool SubmitSource() noexcept
		{
			const RECT sourceRect = {
				0, 0,
				GetSystemMetrics(SM_CXSCREEN) - 1,
				GetSystemMetrics(SM_CYSCREEN) - 1,
			};
			const bool succeeded = MagSetWindowSource(targets_.child, sourceRect) != FALSE;
			TraceMagnifierApi("MagSetWindowSource", targets_.child, succeeded);
			return succeeded;
		}

		[[nodiscard]] bool Invalidate() noexcept
		{
			const bool succeeded = InvalidateRect(targets_.child, nullptr, TRUE) != FALSE;
			TraceMagnifierApi("InvalidateRect", targets_.child, succeeded);
			return succeeded;
		}

		[[nodiscard]] bool Redraw() noexcept
		{
			const bool succeeded = RedrawWindow(targets_.child, nullptr, nullptr,
				RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN) != FALSE;
			TraceMagnifierApi("RedrawWindow", targets_.child, succeeded);
			return succeeded;
		}

		[[nodiscard]] bool Reveal() noexcept
		{
			return SetLayeredWindowAttributes(
				targets_.host, 0, 255, LWA_ALPHA) != FALSE;
		}

		[[nodiscard]] bool Conceal() noexcept
		{
			auto& service = Inkeys::Window::GetService();
			const bool alphaHidden = !IsWindow(targets_.host) ||
				SetLayeredWindowAttributes(targets_.host, 0, 0, LWA_ALPHA) != FALSE;
			const bool childHidden = !targets_.child ||
				service.Handle(WindowRole::MagnifierChild) != targets_.child ||
				service.Hide(WindowRole::MagnifierChild);
			const bool hostHidden = !targets_.host ||
				service.Handle(WindowRole::MagnifierHost) != targets_.host ||
				service.Hide(WindowRole::MagnifierHost);
			cleanupComplete_ = alphaHidden && childHidden && hostHidden;
			// alpha=0 或 Host 已隐藏任一成立，就不会把旧帧留在桌面上。
			return alphaHidden || !IsWindow(targets_.host) ||
				!IsWindowVisible(targets_.host);
		}

		[[nodiscard]] bool CleanupComplete() const noexcept
		{
			return cleanupComplete_;
		}

		[[nodiscard]] HWND Host() const noexcept { return targets_.host; }
		[[nodiscard]] HWND Child() const noexcept { return targets_.child; }

	private:
		MagnifierTargets targets_{};
		bool cleanupComplete_ = true;
	};

	[[nodiscard]] TransactionResult UpdateMagWindow(
		Request request, Win32MagnifierOperations& operations) noexcept
	{
		return Inkeys::UI::Freeze::MagnifierInternal::ExecutePresent(
			magnifierCoordinator, request, operations);
	}

	void PublishFreezeState(Inkeys::UI::Freeze::StateSnapshot snapshot) noexcept
	{
		magnifierCoordinator.Publish({ snapshot.revision, snapshot.active });
	}

	void LogFailure(Stage stage, Request request,
		const Win32MagnifierOperations& operations,
		std::optional<Stage>& continuousFailure)
	{
		if (continuousFailure && *continuousFailure == stage) return;
		continuousFailure = stage;
		if (IDTLogger) IDTLogger->error(
			"[放大API线程][MagnifierThread] 定格更新失败 stage={} request={} host=0x{:X} child=0x{:X}",
			StageName(stage), request.revision,
			reinterpret_cast<std::uintptr_t>(operations.Host()),
			reinterpret_cast<std::uintptr_t>(operations.Child()));
	}

	void LogRecovery(Request request, std::optional<Stage>& continuousFailure)
	{
		if (!continuousFailure) return;
		if (IDTLogger) IDTLogger->info(
			"[放大API线程][MagnifierThread] 定格更新已恢复并提交 request={}",
			request.revision);
		continuousFailure.reset();
	}
}

LRESULT CALLBACK MagnifierHostWindowWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	if (uMsg == magnifierTestMessage || (uMsg == WM_TIMER && wParam == magnifierTestTimer))
	{
		StepMagnifierPrintWindowTest(hwnd);
		return 0;
	}
#endif
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool PrepareMagnifierWindow()
{
	// Window Service 启动回退会销毁并重建整组窗口，新生命周期不能继承旧 stop。
	magnifierCoordinator.Reset();
	magnificationCreateReady = false;
	HMODULE hMagDll = LoadLibrary(TEXT("Magnification.dll"));
	if (hMagDll == NULL)
	{
		IDTLogger->warn("[放大API线程][MagnifierThread] 本机缺少 Magnification.dll，定格等相关功能将被禁用。");
		return false;
	}
	FreeLibrary(hMagDll);

	HMODULE hNtdll = ::GetModuleHandleW(L"ntdll.dll");
	if (hNtdll && GetProcAddress(hNtdll, "wine_get_version") != nullptr)
	{
		IDTLogger->warn("[放大API线程][MagnifierThread] 本机为 Wine 环境，不支持 Magnification.dll 相关功能，定格等相关功能将被禁用。");
		return false;
	}

	const bool initialized = MagInitialize() != FALSE;
	TraceMagnifierApi("MagInitialize", nullptr, initialized);
	if (!initialized)
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 初始化 MagInitialize 失败");
		return false;
	}
	return true;
}

void MagnifierHostCreated(HWND hwnd)
{
	magnifierWindow = hwnd;
	TraceMagnifierApi("MagnifierHostCreated", hwnd, true);
	if (!SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA) && IDTLogger)
		IDTLogger->error("[放大API线程][SetupMagnifier] 初始化 Host 透明度失败");
}

void MagnifierChildCreated(HWND hwnd)
{
	magnifierChild = hwnd;
	TraceMagnifierApi("MagnifierChildCreated", hwnd, true);
	MAGTRANSFORM matrix{};
	matrix.v[0][0] = 1.0f;
	matrix.v[1][1] = 1.0f;
	matrix.v[2][2] = 1.0f;
	const bool transformed = MagSetWindowTransform(hwnd, &matrix) != FALSE;
	TraceMagnifierApi("MagSetWindowTransform", hwnd, transformed);
	if (!transformed)
	{
		IDTLogger->error("[放大API线程][SetupMagnifier] 设置放大倍数矩阵失败");
		return;
	}

	MAGCOLOREFFECT effect = { {
		{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }
	} };
	const bool colored = MagSetColorEffect(hwnd, &effect) != FALSE;
	TraceMagnifierApi("MagSetColorEffect", hwnd, colored);
	if (!colored)
	{
		IDTLogger->error("[放大API线程][SetupMagnifier] 设置颜色矩阵失败");
		return;
	}
	magnificationCreateReady = true;
	IDTLogger->info("[放大API线程][SetupMagnifier] Magnifier 资源准备完成");
}

void StopMagnifierCoordinator() noexcept
{
	magnifierCoordinator.Stop();
}

void ShutdownMagnifierWindow()
{
	StopMagnifierCoordinator();
	magnificationCreateReady = false;
	magnifierChild = nullptr;
	magnifierWindow = nullptr;
	const bool uninitialized = MagUninitialize() != FALSE;
	TraceMagnifierApi("MagUninitialize", nullptr, uninitialized);
}

void MagnifierThread()
{
	IDTLogger->info("[放大API线程][MagnifierThread] 定格请求协调器启动");
	Inkeys::UI::Freeze::SetStateObserver(&PublishFreezeState);
	std::optional<Stage> continuousFailure;

	while (const std::optional<Request> next = magnifierCoordinator.WaitNext())
	{
		const Request request = *next;
		Win32MagnifierOperations operations(CurrentTargets());
		if (!request.active)
		{
			const bool concealed = operations.Conceal();
			magnifierCoordinator.CompleteWithoutApply(request, concealed);
			if (!concealed || !operations.CleanupComplete())
				LogFailure(Stage::Conceal, request, operations, continuousFailure);
			continue;
		}

		TransactionResult result = UpdateMagWindow(request, operations);
		if (result.stage == Stage::Applied)
		{
			if (magnifierCoordinator.CommitApplied(request))
			{
				LogRecovery(request, continuousFailure);
				if (IDTLogger) IDTLogger->info(
					"[放大API线程][MagnifierThread] 定格更新与显示已提交 request={}",
					request.revision);
				continue;
			}
			result = {
				magnifierCoordinator.IsStopped() ? Stage::Stopped : Stage::Superseded,
				operations.Conceal(),
			};
		}

		magnifierCoordinator.CompleteWithoutApply(request, result.concealed);
		if (!operations.CleanupComplete())
			LogFailure(Stage::Conceal, request, operations, continuousFailure);
		if (result.stage == Stage::Superseded || result.stage == Stage::Stopped)
			continue;

		LogFailure(result.stage, request, operations, continuousFailure);
		// 仅撤销仍对应本次失败的用户状态，不能用 Toggle 反向开启较新的请求。
		(void)Inkeys::UI::Freeze::DeactivateIfRevision(request.revision);
	}

	Inkeys::UI::Freeze::SetStateObserver(nullptr);
	Win32MagnifierOperations operations(CurrentTargets());
	(void)operations.Conceal();
	IDTLogger->info("[放大API线程][MagnifierThread] 定格请求协调器停止");
}

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
namespace
{
	constexpr UINT patternPaintMessage = WM_APP + 0x4B4;
	constexpr int patternColumns = 9, patternRows = 7;
	using SetScalingCallback = decltype(&MagSetImageScalingCallback);
	using GetScalingCallback = decltype(&MagGetImageScalingCallback);
	std::atomic<unsigned> scalingEntered{}, scalingExited{}, scalingActive{}, scalingCopyFailures{};
	std::atomic<DWORD> scalingThread{};
	std::atomic<ULONGLONG> scalingExitTick{};

	enum class PrintWindowTestStep
	{
		Start, FreezeBefore, HiddenBefore, Painted, Read, FreezeAfter, HiddenAfter,
	};

	struct PrintWindowTest
	{
		HANDLE log = INVALID_HANDLE_VALUE;
		HANDLE done = nullptr;
		HWND source = nullptr;
		HWND host = nullptr, child = nullptr;
		int width = 0, height = 0;
		DWORD ownerThread = 0;
		std::atomic<int> phase{ 1 }, painted{};
		std::atomic<bool> cancelled{};
		PrintWindowTestStep step = PrintWindowTestStep::Start;
		ULONGLONG deadline = 0, readDue = 0, updateTick = 0;
		unsigned callbacksAtUpdate = 0, phaseMatches = 0, oldFrames = 0, invalidFrames = 0;
		unsigned staticFrames = 0, threadMismatches = 0, apiFailures = 0, freezePasses = 0;
		int result = 1;
		bool finished = false, callbackAttempted = false, callbackRestored = true;
		SetScalingCallback setCallback = nullptr;
		GetScalingCallback getCallback = nullptr;
		MagImageScalingCallback previousCallback = nullptr;
		RECT sourceRect{};
		Inkeys::Graphics::DibSurface frame;
		std::wstring output;
	};
	PrintWindowTest* printWindowTest = nullptr;

	void PrintWindowTestLog(const char* format, ...) noexcept
	{
		if (!printWindowTest) return;
		char line[1536]{};
		va_list arguments;
		va_start(arguments, format);
		const int length = vsnprintf(line, sizeof(line), format, arguments);
		va_end(arguments);
		if (length <= 0) return;
		const DWORD bytes = static_cast<DWORD>((std::min)(length, static_cast<int>(sizeof(line) - 1)));
		DWORD written = 0;
		if (printWindowTest->log != INVALID_HANDLE_VALUE)
			(void)WriteFile(printWindowTest->log, line, bytes, &written, nullptr);
		const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
		if (output && output != INVALID_HANDLE_VALUE)
			(void)WriteFile(output, line, bytes, &written, nullptr);
	}

	void TraceMagnifierApi(const char* name, HWND hwnd, bool succeeded) noexcept
	{
		if (!printWindowTest) return;
		const DWORD error = GetLastError();
		auto& test = *printWindowTest;
		if (!test.ownerThread) test.ownerThread = GetCurrentThreadId();
		// 普通定格保留既有协调线程；仅新增读回/回调与创建寿命要求原 HWND owner。
		const bool ownerRequired = strcmp(name, "MagSetWindowFilterList") != 0
			&& strcmp(name, "MagSetWindowSource") != 0 && strcmp(name, "InvalidateRect") != 0
			&& strcmp(name, "RedrawWindow") != 0;
		if (ownerRequired && test.ownerThread != GetCurrentThreadId()) ++test.threadMismatches;
		if (!succeeded && strncmp(name, "MagSetImageScalingCallback", 26) != 0) ++test.apiFailures;
		PrintWindowTestLog("[FreezeMagTest] api=%s pid=%lu tid=%lu owner=%lu hwnd=%p ok=%u error=%lu\n",
			name, GetCurrentProcessId(), GetCurrentThreadId(), test.ownerThread, hwnd,
			succeeded ? 1u : 0u, succeeded ? 0ul : error);
		SetLastError(error);
	}

	COLORREF PatternColor(int phase, int column, int row) noexcept
	{
		return RGB(32 + (phase * 47 + column * 19 + row * 7) % 192,
			32 + (phase * 31 + column * 11 + row * 29) % 192,
			32 + (phase * 61 + column * 23 + row * 13) % 192);
	}

	LRESULT CALLBACK PrintWindowPatternProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == patternPaintMessage)
		{
			InvalidateRect(hwnd, nullptr, FALSE);
			UpdateWindow(hwnd);
			return 0;
		}
		if (message == WM_PAINT && printWindowTest)
		{
			auto& test = *printWindowTest;
			const int phase = test.phase.load();
			PAINTSTRUCT paint{};
			const HDC dc = BeginPaint(hwnd, &paint);
			for (int row = 0; row < patternRows; ++row)
				for (int column = 0; column < patternColumns; ++column)
				{
					const RECT tile{ column * test.width / patternColumns, row * test.height / patternRows,
						(column + 1) * test.width / patternColumns, (row + 1) * test.height / patternRows };
					const HBRUSH brush = CreateSolidBrush(PatternColor(phase, column, row));
					FillRect(dc, &tile, brush);
					DeleteObject(brush);
				}
			EndPaint(hwnd, &paint);
			GdiFlush();
			test.painted.store(phase);
			return 0;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	LRESULT CALLBACK PrintWindowOverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_PAINT)
		{
			PAINTSTRUCT paint{};
			const HDC dc = BeginPaint(hwnd, &paint);
			const HBRUSH brush = CreateSolidBrush(RGB(0, 0, 255));
			FillRect(dc, &paint.rcPaint, brush);
			DeleteObject(brush);
			EndPaint(hwnd, &paint);
			return 0;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	BOOL CALLBACK ProbeScalingCallback(HWND, void* source, MAGIMAGEHEADER sourceHeader,
		void* destination, MAGIMAGEHEADER destinationHeader, RECT, RECT, HRGN)
	{
		scalingActive.fetch_add(1);
		scalingEntered.fetch_add(1);
		scalingThread.store(GetCurrentThreadId());
		// 只做同格式直通并计数；不保留系统借用的图像，也不在回调内等待或输出。
		const SIZE_T rowBytes = sourceHeader.stride;
		const auto fits = [rowBytes](const MAGIMAGEHEADER& header) noexcept
		{
			return header.offset <= header.cbSize && header.stride >= rowBytes && header.stride != 0
				&& header.height <= (header.cbSize - header.offset) / header.stride;
		};
		const bool compatible = source && destination && sourceHeader.stride == destinationHeader.stride
			&& sourceHeader.width == destinationHeader.width && sourceHeader.height == destinationHeader.height
			&& IsEqualGUID(sourceHeader.format, destinationHeader.format)
			&& fits(sourceHeader) && fits(destinationHeader);
		if (compatible)
		{
			for (UINT row = 0; row < sourceHeader.height; ++row)
				memmove(static_cast<std::byte*>(destination) + destinationHeader.offset + row * static_cast<SIZE_T>(destinationHeader.stride),
					static_cast<const std::byte*>(source) + sourceHeader.offset + row * static_cast<SIZE_T>(sourceHeader.stride), rowBytes);
		}
		else scalingCopyFailures.fetch_add(1);
		scalingExitTick.store(GetTickCount64());
		scalingExited.fetch_add(1);
		scalingActive.fetch_sub(1);
		return compatible ? TRUE : FALSE;
	}

	bool RestoreScalingCallback(PrintWindowTest& test) noexcept
	{
		if (!test.callbackAttempted || test.callbackRestored) return true;
		const bool restored = test.setCallback(test.child, test.previousCallback) != FALSE;
		TraceMagnifierApi("MagSetImageScalingCallback.restore", test.child, restored);
		// 不支持登记时可能始终保留旧值；只有读回确认恢复后才允许继续。
		test.callbackRestored = test.getCallback(test.child) == test.previousCallback;
		PrintWindowTestLog("[FreezeMagTest] callback_restore=%u entered=%u exited=%u active=%u tid=%lu copyFailures=%u\n",
			test.callbackRestored, scalingEntered.load(), scalingExited.load(), scalingActive.load(),
			scalingThread.load(), scalingCopyFailures.load());
		return test.callbackRestored;
	}

	void FinishPrintWindowTest(HWND host, bool passed, const char* reason) noexcept
	{
		auto& test = *printWindowTest;
		if (test.finished) return;
		KillTimer(host, magnifierTestTimer);
		const bool restored = RestoreScalingCallback(test);
		Win32MagnifierOperations operations(CurrentTargets());
		const bool concealed = operations.Conceal();
		test.frame.reset();
		test.result = passed && restored && concealed && test.freezePasses == 2
			&& !test.threadMismatches && !test.apiFailures && !scalingCopyFailures.load() ? 0 : 1;
		test.finished = true;
		PrintWindowTestLog("[FreezeMagTest] result=%s reason=%s pixels=%s phases=%u freezePasses=%u/2 static=%u old=%u invalid=%u threadMismatches=%u apiFailures=%u\n",
			test.result == 0 ? "PASS" : "FAIL", reason, test.phaseMatches == 7 ? "PASS" : "FAIL",
			test.phaseMatches, test.freezePasses, test.staticFrames,
			test.oldFrames, test.invalidFrames, test.threadMismatches, test.apiFailures);
		SetEvent(test.done);
	}

	bool PreparePrintWindowFrame(PrintWindowTest& test) noexcept
	{
		Win32MagnifierOperations operations(CurrentTargets());
		test.callbacksAtUpdate = scalingExited.load();
		TraceMagnifierApi("CaptureUpdateOnOwner", test.child,
			GetCurrentThreadId() == GetWindowThreadProcessId(test.host, nullptr));
		if (GetCurrentThreadId() != GetWindowThreadProcessId(test.host, nullptr)
			|| !operations.TargetsCurrent() || !operations.PrepareHidden()
			|| !operations.SubmitFilter() || !operations.SubmitSource()
			|| !operations.Invalidate() || !operations.Redraw()) return false;
		RECT client{};
		const bool sourceRead = MagGetWindowSource(test.child, &test.sourceRect) != FALSE;
		TraceMagnifierApi("MagGetWindowSource", test.child, sourceRead);
		if (!sourceRead || !GetClientRect(test.child, &client)
			|| client.right != test.width || client.bottom != test.height - 1
			|| test.sourceRect.left != 0 || test.sourceRect.top != 0
			|| test.sourceRect.right != test.width - 1 || test.sourceRect.bottom != test.height - 1
			|| !test.frame.resize(client.right, client.bottom)) return false;
		test.updateTick = GetTickCount64();
		// 定时消息保留原 owner 消息泵；800ms 是首帧策略，不是 compositor 完成凭据。
		test.readDue = test.updateTick + (test.phaseMatches == 0 && test.staticFrames == 0 ? 800 : 100);
		return true;
	}

	int MatchPattern(const PrintWindowTest& test, int phase, double& variance) noexcept
	{
		const auto pixels = test.frame.pixels();
		double sum = 0, squareSum = 0;
		int matches = 0;
		for (int row = 0; row < patternRows; ++row)
			for (int column = 0; column < patternColumns; ++column)
			{
				const int x = (2 * column + 1) * test.width / (2 * patternColumns);
				const int y = (2 * row + 1) * test.height / (2 * patternRows);
				const auto pixel = pixels[static_cast<std::size_t>(y) * test.frame.width() + x];
				const COLORREF expected = PatternColor(phase, column, row);
				const int red = (pixel >> 16) & 255, green = (pixel >> 8) & 255, blue = pixel & 255;
				if (abs(red - GetRValue(expected)) <= 3 && abs(green - GetGValue(expected)) <= 3
					&& abs(blue - GetBValue(expected)) <= 3) ++matches;
				const double luma = red * 0.2126 + green * 0.7152 + blue * 0.0722;
				sum += luma;
				squareSum += luma * luma;
			}
		variance = squareSum / (patternColumns * patternRows)
			- sum * sum / ((patternColumns * patternRows) * (patternColumns * patternRows));
		return matches;
	}

	void StepMagnifierPrintWindowTest(HWND host) noexcept
	{
		if (!printWindowTest || printWindowTest->finished) return;
		auto& test = *printWindowTest;
		const ULONGLONG now = GetTickCount64();
		if (test.cancelled.load() || now >= test.deadline)
		{
			FinishPrintWindowTest(host, false, "step-timeout-or-cancel");
			return;
		}
		try
		{
			if (CurrentTargets().host != test.host || CurrentTargets().child != test.child)
			{
				FinishPrintWindowTest(host, false, "original-handles-changed");
				return;
			}
			const auto snapshot = magnifierCoordinator.Snapshot();
			switch (test.step)
			{
			case PrintWindowTestStep::Start:
			{
				const HDC screen = GetDC(nullptr);
				const COLORREF center = screen ? GetPixel(screen, test.width / 2, test.height / 2) : CLR_INVALID;
				if (screen) ReleaseDC(nullptr, screen);
				if (test.painted.load() != 1 || center != RGB(0, 0, 255)) return;
				const auto filters = BuildCurrentFilterList();
				const HWND overlay = Inkeys::Window::GetService().Handle(WindowRole::Bar);
				if (std::find(filters.begin(), filters.end(), overlay) == filters.end()
					|| std::find(filters.begin(), filters.end(), test.source) != filters.end())
				{
					FinishPrintWindowTest(host, false, "production-filter-roles"); return;
				}
				PrintWindowTestLog("[FreezeMagTest] overlay_on_screen=blue exact_filter_count=%zu source_unfiltered=1\n", filters.size());
				// 首轮直接读取原始 hidden pair，不能先用不透明定格预热。
				PrintWindowTestLog("[FreezeMagTest] cold_hidden_pair=1 opaque_reveal_before_first_read=0\n");
				test.step = PrintWindowTestStep::Painted;
				PostMessageW(test.source, patternPaintMessage, 0, 0);
				break;
			}
			case PrintWindowTestStep::FreezeBefore:
			case PrintWindowTestStep::FreezeAfter:
			{
				const auto freeze = Inkeys::UI::Freeze::Snapshot();
				if (!freeze.active && !snapshot.visible && snapshot.processedRevision >= freeze.revision)
				{
					PrintWindowTestLog("[FreezeMagTest] freeze_%s=failed-original-path revision=%llu; continue pixel diagnostic\n",
						test.step == PrintWindowTestStep::FreezeBefore ? "before" : "after", freeze.revision);
					test.step = test.step == PrintWindowTestStep::FreezeBefore
						? PrintWindowTestStep::HiddenBefore : PrintWindowTestStep::HiddenAfter;
					break;
				}
				if (!snapshot.visible || snapshot.appliedRevision != freeze.revision) return;
				BYTE alpha = 0; DWORD flags = 0; COLORREF key = 0;
				if (!GetLayeredWindowAttributes(host, &key, &alpha, &flags) || alpha != 255
					|| !(flags & LWA_ALPHA) || !IsWindowVisible(test.child))
				{
					FinishPrintWindowTest(host, false, "freeze-reveal"); return;
				}
				++test.freezePasses;
				PrintWindowTestLog("[FreezeMagTest] freeze_%s=revealed alpha=%u revision=%llu\n",
					test.step == PrintWindowTestStep::FreezeBefore ? "before" : "after", alpha, snapshot.appliedRevision);
				Inkeys::UI::Freeze::Toggle();
				test.step = test.step == PrintWindowTestStep::FreezeBefore
					? PrintWindowTestStep::HiddenBefore : PrintWindowTestStep::HiddenAfter;
				break;
			}
			case PrintWindowTestStep::HiddenBefore:
			case PrintWindowTestStep::HiddenAfter:
				if (snapshot.visible || snapshot.processedRevision < Inkeys::UI::Freeze::Snapshot().revision
					|| IsWindowVisible(host) || IsWindowVisible(test.child)) return;
				PrintWindowTestLog("[FreezeMagTest] freeze_%s=hidden\n",
					test.step == PrintWindowTestStep::HiddenBefore ? "before" : "after");
				if (test.step == PrintWindowTestStep::HiddenAfter)
				{
					FinishPrintWindowTest(host, true, "pixels-exclusion-freeze-and-cleanup"); return;
				}
				if (test.setCallback && test.getCallback)
				{
					test.previousCallback = test.getCallback(test.child);
					test.callbackAttempted = true;
					test.callbackRestored = false;
					const bool registered = test.setCallback(test.child, &ProbeScalingCallback) != FALSE;
					TraceMagnifierApi("MagSetImageScalingCallback", test.child, registered);
					PrintWindowTestLog("[FreezeMagTest] callback_registered=%u get_matches=%u previous=%p\n",
						registered, test.getCallback(test.child) == &ProbeScalingCallback, reinterpret_cast<void*>(test.previousCallback));
				}
				test.phase.store(4);
				test.step = PrintWindowTestStep::Painted;
				PostMessageW(test.source, patternPaintMessage, 0, 0);
				break;
			case PrintWindowTestStep::Painted:
				if (test.painted.load() != test.phase.load()) return;
				if (!PreparePrintWindowFrame(test))
				{
					FinishPrintWindowTest(host, false, "freeze-prepare-update"); return;
				}
				test.step = PrintWindowTestStep::Read;
				break;
			case PrintWindowTestStep::Read:
			{
				const bool callbackSignal = scalingExited.load() > test.callbacksAtUpdate && scalingActive.load() == 0;
				if (now < test.readDue && !callbackSignal) return;
				test.frame.clear(0xCCCCCCCC);
				const ULONGLONG readStart = GetTickCount64();
				const bool printed = PrintWindow(test.child, test.frame.dc(), 2) != FALSE;
				TraceMagnifierApi("PrintWindow(2)", test.child, printed);
				GdiFlush();
				if (!printed)
				{
					FinishPrintWindowTest(host, false, "printwindow-api"); return;
				}
				double variance = 0;
				const int phase = test.phase.load();
				const int matched = MatchPattern(test, phase, variance);
				int matchedPhase = matched == patternColumns * patternRows ? phase : 0;
				for (int prior = 1; prior < phase && !matchedPhase; ++prior)
				{
					double unused = 0;
					if (MatchPattern(test, prior, unused) == patternColumns * patternRows) matchedPhase = prior;
				}
				PrintWindowTestLog("[FreezeMagTest] read phase=%d painted=%d match=%d/63 oldPhase=%d variance=%.3f wait_ms=%llu pw_ms=%llu callbackSignal=%u entered=%u exited=%u exitTick=%llu\n",
					phase, test.painted.load(), matched, matchedPhase, variance, readStart - test.updateTick,
					GetTickCount64() - readStart, callbackSignal, scalingEntered.load(), scalingExited.load(), scalingExitTick.load());
				if (matched != patternColumns * patternRows || variance < 4)
				{
					if (matchedPhase && variance >= 4) ++test.oldFrames;
					else ++test.invalidFrames;
					if (!PreparePrintWindowFrame(test)) FinishPrintWindowTest(host, false, "retry-update");
					return;
				}
				if (phase == 1 && ++test.staticFrames < 3)
				{
					if (!PreparePrintWindowFrame(test)) FinishPrintWindowTest(host, false, "static-update");
					return;
				}
				++test.phaseMatches;
				const std::wstring imagePath = test.output + L"/phase-" + std::to_wstring(phase) + L".png";
				// 文件只包含专用图案的已验证帧，不保存无效帧中的桌面内容。
				for (auto& pixel : test.frame.pixels()) pixel |= 0xFF000000;
				if (!test.frame.savePng(imagePath))
				{
					FinishPrintWindowTest(host, false, "pattern-output"); return;
				}
				if (phase == 3 || phase == 7)
				{
					Inkeys::UI::Freeze::Toggle();
					test.step = phase == 3 ? PrintWindowTestStep::FreezeBefore : PrintWindowTestStep::FreezeAfter;
				}
				else
				{
					if (phase == 6 && !RestoreScalingCallback(test))
					{
						FinishPrintWindowTest(host, false, "callback-restore"); return;
					}
					test.phase.store(phase + 1);
					PostMessageW(test.source, patternPaintMessage, 0, 0);
					test.step = PrintWindowTestStep::Painted;
				}
				break;
			}
			}
			test.deadline = GetTickCount64() + 5000;
		}
		catch (...) { FinishPrintWindowTest(host, false, "fixture-exception"); }
	}
}

int RunFreezeMagnifierPrintWindowTest()
{
	PrintWindowTest test;
	const auto previousLogger = IDTLogger;
	std::jthread magnifierWorker;
	HANDLE cleanupDone = nullptr;
	std::jthread deadlineWorker;
	bool serviceStarted = false;
	const DWORD initialGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	const DWORD initialUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
	try
	{
		test.output = (std::filesystem::temp_directory_path()
			/ (L"FreezeMagPrintWindow-" + std::to_wstring(GetCurrentProcessId()))).wstring();
		std::filesystem::create_directories(test.output);
		test.log = CreateFileW((test.output + L"/result.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		test.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		cleanupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (test.log == INVALID_HANDLE_VALUE || !test.done || !cleanupDone) throw std::runtime_error("fixture-output");
		printWindowTest = &test;
		// 内部测试不初始化产品日志、配置、单实例、PPT 或自启；退出前恢复原 logger。
		IDTLogger = std::make_shared<spdlog::logger>("FreezeMagPrintWindow", std::make_shared<spdlog::sinks::null_sink_mt>());
		PrintWindowTestLog("[FreezeMagTest] pid=%lu main_tid=%lu output=%ls no_WGC=1 no_affinity=1\n",
			GetCurrentProcessId(), GetCurrentThreadId(), test.output.c_str());
		BOOL compositionEnabled = FALSE;
		const HRESULT compositionResult = DwmIsCompositionEnabled(&compositionEnabled);
		PrintWindowTestLog("[FreezeMagTest] dwm_enabled=%u query_hr=0x%08lX state_unchanged=1\n",
			compositionEnabled, static_cast<unsigned long>(compositionResult));
		deadlineWorker = std::jthread([cleanupDone]
		{
			// 若系统调用永不返回，专用测试进程仍有最终退出上限；这不算正常清理通过。
			if (WaitForSingleObject(cleanupDone, 45000) != WAIT_OBJECT_0)
				TerminateProcess(GetCurrentProcess(), 3);
		});
		test.width = GetSystemMetrics(SM_CXSCREEN);
		test.height = GetSystemMetrics(SM_CYSCREEN);
		WNDCLASSW patternClass{};
		patternClass.hInstance = GetModuleHandleW(nullptr);
		patternClass.lpfnWndProc = PrintWindowPatternProc;
		patternClass.lpszClassName = L"Inkeys.FreezeMagPrintWindow.Pattern";
		if (!RegisterClassW(&patternClass)) throw std::runtime_error("pattern-class");
		test.source = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
			patternClass.lpszClassName, L"Inkeys Freeze Mag dedicated pattern", WS_POPUP,
			0, 0, test.width, test.height, nullptr, nullptr, patternClass.hInstance, nullptr);
		if (!test.source) throw std::runtime_error("pattern-window");
		ShowWindow(test.source, SW_SHOWNOACTIVATE);
		UpdateWindow(test.source);

		// 原 WindowService.Start 的 ready promise 保持不变，初始化和原窗口对均由其 STA owner 创建。
		Inkeys::Window::WindowSpec host;
		host.role = WindowRole::MagnifierHost;
		host.className = L"Inkeys.FreezeMagPrintWindow.Host";
		host.width = test.width; host.height = test.height - 1;
		host.style = WS_POPUP | WS_CLIPCHILDREN;
		host.exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
		host.windowProc = MagnifierHostWindowWndProc;
		host.bindMessages = false;
		host.beforeCreate = PrepareMagnifierWindow;
		host.created = MagnifierHostCreated;
		host.destroyed = ShutdownMagnifierWindow;
		Inkeys::Window::WindowSpec child;
		child.role = WindowRole::MagnifierChild; child.className = WC_MAGNIFIER;
		child.width = test.width; child.height = test.height - 1;
		child.style = WS_CHILD | WS_VISIBLE | MS_CLIPAROUNDCURSOR;
		child.exStyle = WS_EX_NOACTIVATE;
		child.bindMessages = false; child.created = MagnifierChildCreated;
		Inkeys::Window::WindowSpec overlay;
		overlay.role = WindowRole::Bar; overlay.className = L"Inkeys.FreezeMagPrintWindow.Overlay";
		overlay.x = test.width / 3; overlay.y = test.height / 3;
		overlay.width = test.width / 3; overlay.height = test.height / 3;
		overlay.style = WS_POPUP; overlay.exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
		overlay.windowProc = PrintWindowOverlayProc; overlay.bindMessages = false;
		auto& service = Inkeys::Window::GetService();
		if (!service.Start({ host, child, overlay }) || !magnificationCreateReady)
			throw std::runtime_error("original-freeze-start");
		serviceStarted = true;
		test.host = service.Handle(WindowRole::MagnifierHost);
		test.child = service.Handle(WindowRole::MagnifierChild);
		const HMODULE mag = GetModuleHandleW(L"Magnification.dll");
		test.setCallback = reinterpret_cast<SetScalingCallback>(GetProcAddress(mag, "MagSetImageScalingCallback"));
		test.getCallback = reinterpret_cast<GetScalingCallback>(GetProcAddress(mag, "MagGetImageScalingCallback"));
		if (!service.Show(WindowRole::Bar) || !service.SetOverlayTopmost(true))
			throw std::runtime_error("overlay-show");
		RECT sourceBounds{}, childBounds{};
		GetWindowRect(test.source, &sourceBounds); GetClientRect(test.child, &childBounds);
		PrintWindowTestLog("[FreezeMagTest] host=%p child=%p owner=%lu source=%p bounds=(%ld,%ld,%ld,%ld) child_client=%ldx%ld source_contract=%dx%d primary=%dx%d callback_exports=%u\n",
			test.host, test.child, service.OwnerThreadId(WindowRole::MagnifierHost), test.source,
			sourceBounds.left, sourceBounds.top, sourceBounds.right, sourceBounds.bottom,
			childBounds.right, childBounds.bottom, test.width - 1, test.height - 1,
			test.width, test.height, test.setCallback && test.getCallback);
		if (sourceBounds.left != 0 || sourceBounds.top != 0 || sourceBounds.right != test.width || sourceBounds.bottom != test.height)
			throw std::runtime_error("source-geometry");
		magnifierWorker = std::jthread(MagnifierThread);
		test.deadline = GetTickCount64() + 5000;
		if (!SetTimer(test.host, magnifierTestTimer, 25, nullptr)
			|| !PostMessageW(test.host, magnifierTestMessage, 0, 0)) throw std::runtime_error("fixture-timer");
		const ULONGLONG timeout = GetTickCount64() + 35000;
		while (WaitForSingleObject(test.done, 0) != WAIT_OBJECT_0)
		{
			if (GetTickCount64() >= timeout) test.cancelled.store(true);
			(void)MsgWaitForMultipleObjects(1, &test.done, FALSE, 50, QS_ALLINPUT);
			MSG message{};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message); DispatchMessageW(&message);
			}
		}
	}
	catch (const std::exception& exception)
	{
		PrintWindowTestLog("[FreezeMagTest] exception=%s error=%lu\n", exception.what(), GetLastError());
		test.result = 1;
	}
	StopMagnifierCoordinator();
	if (magnifierWorker.joinable()) magnifierWorker.join();
	// 与正常软件相同：先结束原协调线程，再销毁窗口并在原 owner 反初始化。
	if (serviceStarted || Inkeys::Window::GetService().Running()) Inkeys::Window::GetService().StopAndJoin();
	if (test.source) DestroyWindow(test.source);
	if (IsWindow(test.host) || IsWindow(test.child) || scalingActive.load() || test.apiFailures) test.result = 1;
	UnregisterClassW(L"Inkeys.FreezeMagPrintWindow.Pattern", GetModuleHandleW(nullptr));
	PrintWindowTestLog("[FreezeMagTest] cleanup original_pair_destroyed=%u gdi=%lu->%lu user=%lu->%lu callbacks=%u/%u result=%d\n",
		!IsWindow(test.host) && !IsWindow(test.child), initialGdi, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
		initialUser, GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), scalingEntered.load(), scalingExited.load(), test.result);
	if (cleanupDone) SetEvent(cleanupDone);
	if (deadlineWorker.joinable()) deadlineWorker.join();
	IDTLogger = previousLogger;
	printWindowTest = nullptr;
	if (test.log != INVALID_HANDLE_VALUE) CloseHandle(test.log);
	if (test.done) CloseHandle(test.done);
	if (cleanupDone) CloseHandle(cleanupDone);
	return test.result;
}
#else
namespace
{
	void TraceMagnifierApi(const char*, HWND, bool) noexcept {}
}
int RunFreezeMagnifierPrintWindowTest() { return 2; }
#endif
