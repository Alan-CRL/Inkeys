#include "IdtMagnification.h"
#include <winuser.h>

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "Inkeys/Graphics/FreezeCapture.h"
#include "Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h"
#include "Inkeys/Window/Window.Legacy.hpp"
#include "Inkeys/UI/ScrollFollowDebug/ScrollFollowDebug.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <thread>

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Business/ScrollFollow.h"
#include "Inkeys/Business/ScrollFollowDebug.h"
#include "Inkeys/CV/VisualMotionTracker.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "IdtState.h"
#include <spdlog/sinks/null_sink.h>
#endif
#include <cstdint>
#include <optional>
#include <vector>

import Inkeys.Display;
import Inkeys.UI.Freeze;
import Inkeys.Window;

HWND magnifierWindow, magnifierChild;
Inkeys::Graphics::DibSurface MagnificationBackground;

// 原 owner 写入就绪，连续捕获及 Start 在其他线程读取。
std::atomic<bool> magnificationCreateReady = false;

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
	namespace Capture = Inkeys::Graphics::FreezeCapture;
	void WakeCaptureWorker() noexcept { magnifierCoordinator.Wake(); }
	Capture::Session captureSession(&WakeCaptureWorker);
	std::atomic<bool> magnifierRuntimeAbandoned = false;

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
		static constexpr std::array<WindowRole, 11> filterRoles = {
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
			WindowRole::ScrollFollowDebug,
		};

		const auto& service = Inkeys::Window::GetService();
		std::array<HWND, filterRoles.size() + 1> candidates{};
		for (std::size_t index = 0; index < filterRoles.size(); ++index)
			candidates[index] = service.Handle(filterRoles[index]);
		// 导出目录对话框由独立 STA 持有；每次读取当前句柄，沿同一有效性/进程/去重检查。
		candidates.back() = Inkeys::UI::ScrollFollowDebug::ExportPickerHandle();

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

		[[nodiscard]] bool PrepareHidden(Capture::BackendResult* failure = nullptr) noexcept
		{
			if (!SetLayeredWindowAttributes(targets_.host, 0, 0, LWA_ALPHA))
			{
				const DWORD error = GetLastError();
				if (failure) *failure = { false, Capture::StopReason::BackendFailure, error, true };
				return false;
			}
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
			return SubmitSource(sourceRect);
		}

		[[nodiscard]] bool SubmitSource(const RECT& sourceRect) noexcept
		{
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

	[[nodiscard]] Capture::Rect CaptureRect(const RECT& rect) noexcept
	{
		return { rect.left, rect.top, rect.right, rect.bottom };
	}

	class FreezeCaptureBackend final : public Capture::Backend
	{
	public:
		Capture::BackendResult Begin(const Capture::Geometry& geometry) noexcept override
		{
			operations_ = Win32MagnifierOperations(CurrentTargets());
			if (const auto checked = CheckGeometry(geometry, false); !checked.succeeded) return checked;
			Capture::BackendResult failure{};
			// DIB/Service 包装失败没有可靠原生错误码，保持 nativeErrorValid=false。
			if (!frame_.resize(geometry.width, geometry.height) || !operations_.PrepareHidden(&failure)) return failure;
			return { true };
		}

		Capture::BackendResult Update(const Capture::Geometry& geometry) noexcept override
		{
			if (const auto checked = CheckGeometry(geometry, false); !checked.succeeded) return checked;
			const auto& source = geometry.sourceRect;
			const RECT sourceRect = { source.left, source.top, source.right, source.bottom };
			// 这些 BOOL API 没有 GetLastError 合同，不把残留错误声明为本次原生失败。
			if (!operations_.SubmitFilter() || !operations_.SubmitSource(sourceRect)
				|| !operations_.Invalidate() || !operations_.Redraw()) return {};
			return CheckGeometry(geometry, true);
		}

		Capture::BackendResult Read(const Capture::Geometry& geometry,
			std::span<const std::uint32_t>& pixels) noexcept override
		{
			if (const auto checked = CheckGeometry(geometry, true); !checked.succeeded) return checked;
			frame_.clear();
			// 同步 PW 只在原 MagnifierThread，Stop/监督线程不等它，也不释放其 DIB。
			const bool printed = PrintWindow(operations_.Child(), frame_.dc(), 2) != FALSE;
			const bool flushed = GdiFlush() != FALSE;
			// 两者均未承诺设置 last-error；printed=false 仍执行原 GdiFlush，不伪造错误码。
			if (!printed || !flushed) return {};
			if (const auto checked = CheckGeometry(geometry, true); !checked.succeeded) return checked;
			for (auto& pixel : frame_.pixels()) pixel |= 0xFF000000;
			pixels = frame_.pixels();
			return { true };
		}

		bool End(bool accessWindows) noexcept override
		{
			const bool concealed = !accessWindows || !operations_.TargetsCurrent()
				|| (operations_.Conceal() && operations_.CleanupComplete());
			frame_.reset();
			return concealed;
		}

	private:
		Capture::BackendResult CheckGeometry(const Capture::Geometry& geometry, bool checkSource) const noexcept
		{
			if (!operations_.TargetsCurrent()) return { false, Capture::StopReason::Unavailable };
			const auto display = Inkeys::Display::GetSnapshot();
			const auto* primary = display ? display->Primary() : nullptr;
			RECT client{}, source{};
			MAGTRANSFORM transform{};
			if (!primary || display->fallback || primary->fallback
				|| display->generation != geometry.displayGeneration
				|| CaptureRect(primary->bounds) != geometry.primaryBounds
				|| CaptureRect(primary->workArea) != geometry.workArea
				|| GetSystemMetrics(SM_CXSCREEN) != geometry.primaryBounds.right
				|| GetSystemMetrics(SM_CYSCREEN) != geometry.primaryBounds.bottom)
				return { false, Capture::StopReason::GeometryChanged };
			if (!GetClientRect(operations_.Child(), &client))
				return { false, Capture::StopReason::GeometryChanged, GetLastError(), true };
			if (client.left != 0 || client.top != 0 || client.right != geometry.width || client.bottom != geometry.height)
				return { false, Capture::StopReason::GeometryChanged };
			if (checkSource && (!MagGetWindowSource(operations_.Child(), &source)
				|| CaptureRect(source) != geometry.sourceRect)) return { false, Capture::StopReason::GeometryChanged };
			// 像素映射由 Mag transform 决定，不能从 source/client 少一像素推导出缩放。
			if (!MagGetWindowTransform(operations_.Child(), &transform)) return {};
			for (int row = 0; row < 3; ++row)
				for (int column = 0; column < 3; ++column)
					if (transform.v[row][column] != (row == column ? 1.0f : 0.0f))
						return { false, Capture::StopReason::GeometryChanged };
			return { true };
		}

		Win32MagnifierOperations operations_{ {} };
		Inkeys::Graphics::DibSurface frame_;
	};

	[[nodiscard]] TransactionResult UpdateMagWindow(
		Request request, Win32MagnifierOperations& operations) noexcept
	{
		return Inkeys::UI::Freeze::MagnifierInternal::ExecutePresent(
			magnifierCoordinator, request, operations);
	}

	void PublishFreezeState(Inkeys::UI::Freeze::StateSnapshot snapshot) noexcept
	{
		captureSession.SetFreezeState(snapshot.revision, snapshot.active, snapshot.available);
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

namespace Inkeys::Graphics::FreezeCapture
{
	StartResult Start(FrameCallback callback) noexcept
	{
		const auto freeze = Inkeys::UI::Freeze::Snapshot();
		captureSession.SetFreezeState(freeze.revision, freeze.active, freeze.available);
		const auto coordinated = magnifierCoordinator.Snapshot();
		if (coordinated.stopped) return { StartStatus::NotReady };
		if (coordinated.visible || coordinated.processedRevision < freeze.revision)
			return { freeze.active ? StartStatus::FreezeActive : StartStatus::Busy };
		const auto display = Inkeys::Display::GetSnapshot();
		const auto* primary = display ? display->Primary() : nullptr;
		RECT client{};
		const auto targets = CurrentTargets();
		if (!primary || display->fallback || primary->fallback || !AreCurrentTargets(targets)
			|| !GetClientRect(targets.child, &client)) return { StartStatus::NotReady };
		const Geometry geometry = { CaptureRect(primary->bounds),
			{ 0, 0, primary->bounds.right - 1, primary->bounds.bottom - 1 },
			CaptureRect(primary->workArea), display->generation, client.right, client.bottom };
		return captureSession.Start(geometry, std::move(callback));
	}

	void Stop(StopReason reason) noexcept { captureSession.Stop(reason); }
	Snapshot GetSnapshot() noexcept { return captureSession.GetSnapshot(); }
}

LRESULT CALLBACK MagnifierHostWindowWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_DESTROY && !captureSession.DisableWindowAccess())
		magnifierRuntimeAbandoned.store(true);
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
	// 异常销毁若撞上未返回的读回，不再初始化新一代或释放仍被调用借用的运行库。
	if (magnifierRuntimeAbandoned.load()) return false;
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
	captureSession.SetReady(false);
	magnifierCoordinator.Stop();
}

void ShutdownMagnifierWindow()
{
	StopMagnifierCoordinator();
	if (!captureSession.DisableWindowAccess()) magnifierRuntimeAbandoned.store(true);
	magnificationCreateReady = false;
	magnifierChild = nullptr;
	magnifierWindow = nullptr;
	if (magnifierRuntimeAbandoned.load())
	{
		if (IDTLogger) IDTLogger->error("[ScrollFollow] 原 Mag 异常销毁时仍有读回/回调在途，停止发布并保留运行库至进程退出，禁止再次捕获");
		return;
	}
	const bool uninitialized = MagUninitialize() != FALSE;
	TraceMagnifierApi("MagUninitialize", nullptr, uninitialized);
}

void MagnifierThread()
{
	IDTLogger->info("[放大API线程][MagnifierThread] 定格请求协调器启动");
	FreezeCaptureBackend captureBackend;
	std::jthread captureSupervisor;
	try
	{
		captureSupervisor = std::jthread([](std::stop_token stop) { captureSession.RunSupervisor(stop); });
		captureSession.SetReady(!magnifierCoordinator.IsStopped()
			&& !magnifierRuntimeAbandoned.load() && AreCurrentTargets(CurrentTargets()));
	}
	catch (...)
	{
		captureSession.SetReady(false);
		if (IDTLogger) IDTLogger->error("[ScrollFollow] 捕获监督线程建立失败，禁用捕获");
	}
	Inkeys::UI::Freeze::SetStateObserver(&PublishFreezeState);
	std::optional<Stage> continuousFailure;

	while (!magnifierCoordinator.IsStopped())
	{
		const auto next = magnifierCoordinator.WaitNextFor(captureSession.NextDelay());
		if (magnifierCoordinator.IsStopped()) break;
		if (!next)
		{
			captureSession.Drain(captureBackend);
			captureSession.Pump(captureBackend);
			continue;
		}
		// 普通定格优先：同一串行线程先结束捕获占用，再执行原事务，绝不争用控件。
		captureSession.Stop(Capture::StopReason::FreezeActive);
		captureSession.Drain(captureBackend);
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
	captureSession.SetReady(false);
	captureSession.Drain(captureBackend);
	if (captureSupervisor.joinable())
	{
		captureSupervisor.request_stop();
		captureSupervisor.join();
	}
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
		unsigned staticFrames = 0, freezePasses = 0;
		std::atomic<unsigned> threadMismatches{}, apiFailures{};
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

	bool LiveSecurityInterfacePresent() noexcept
	{
		// 只读命中窗口元数据；必须在Display/夹具窗口/屏幕读回之前识别安全层。
		const HWND hit=WindowFromPoint({GetSystemMetrics(SM_CXSCREEN)/2,GetSystemMetrics(SM_CYSCREEN)/2});
		wchar_t name[128]{}; (void)GetClassNameW(hit,name,128);
		if(CompareStringOrdinal(name,-1,L"LockScreenBackstopFrame",-1,TRUE)==CSTR_EQUAL)return true;
		DWORD pid=0; (void)GetWindowThreadProcessId(hit,&pid);
		if(!pid || pid==GetCurrentProcessId())return false;
		const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
		wchar_t image[MAX_PATH]{};DWORD length=MAX_PATH;
		const bool queried=process && QueryFullProcessImageNameW(process,0,image,&length)!=FALSE;
		if(process)CloseHandle(process);
		if(!queried)return false;
		const wchar_t* base=wcsrchr(image,L'\\');base=base?base+1:image;
		for(const wchar_t* security:{L"LockApp.exe",L"Consent.exe",L"CredentialUIBroker.exe",L"LogonUI.exe",L"winlogon.exe"})
			if(CompareStringOrdinal(base,-1,security,-1,TRUE)==CSTR_EQUAL)return true;
		return false;
	}

	bool LiveDesktopAvailable(const char* stage) noexcept
	{
		wchar_t current[128]{},input[128]{};DWORD bytes=0;
		const bool currentRead=GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,current,sizeof(current),&bytes)!=FALSE;
		HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);const DWORD error=desktop?0:GetLastError();
		const bool inputRead=desktop && GetUserObjectInformationW(desktop,UOI_NAME,input,sizeof(input),&bytes)!=FALSE;
		if(desktop)CloseDesktop(desktop);
		const bool available=currentRead && inputRead && CompareStringOrdinal(current,-1,input,-1,TRUE)==CSTR_EQUAL;
		PrintWindowTestLog("[FreezeCaptureLive] desktop stage=%s tid=%lu current=%ls input=%ls current_query=%u input_query=%u open_error=%lu available=%u\n",
			stage,GetCurrentThreadId(),current,input,currentRead,inputRead,error,available);
		return available;
	}

	bool LogLiveWindows(const char* stage, WindowRole overlayRole = WindowRole::Bar) noexcept
	{
		if(!printWindowTest)return false;const auto& test=*printWindowTest;
		const HWND overlay=Inkeys::Window::GetService().Handle(overlayRole);
		const HWND hit=WindowFromPoint({test.width/2,test.height/2});
		DWORD hitProcess=0;(void)GetWindowThreadProcessId(hit,&hitProcess);
		wchar_t hitClass[128]{},image[MAX_PATH]{};DWORD imageLength=MAX_PATH;
		(void)GetClassNameW(hit,hitClass,128);
		const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,hitProcess);
		const bool imageRead=process && QueryFullProcessImageNameW(process,0,image,&imageLength)!=FALSE;
		if(process)CloseHandle(process);
		const wchar_t* basename=imageRead?(wcsrchr(image,L'\\')?wcsrchr(image,L'\\')+1:image):L"unavailable";
		// 锁屏backstop仍可位于Default桌面且由explorer持有；桌面同名/未cloak不能当作解锁凭据。
		bool security=CompareStringOrdinal(hitClass,-1,L"LockScreenBackstopFrame",-1,TRUE)==CSTR_EQUAL;
		for(const wchar_t* name:{L"LockApp.exe",L"Consent.exe",L"CredentialUIBroker.exe",L"LogonUI.exe",L"winlogon.exe"})
			security |= CompareStringOrdinal(basename,-1,name,-1,TRUE)==CSTR_EQUAL;
		RECT hitRect{};const bool hitBounds=GetWindowRect(hit,&hitRect)!=FALSE;
		PrintWindowTestLog("[FreezeCaptureLive] hit hwnd=%p pid=%lu class=%ls basename=%ls image_query=%u same_process=%u topmost=%u security=%u bounds_ok=%u bounds=(%ld,%ld,%ld,%ld)\n",
			hit,hitProcess,hitClass,basename,imageRead,hitProcess==GetCurrentProcessId(),(GetWindowLongPtrW(hit,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,security,
			hitBounds,hitRect.left,hitRect.top,hitRect.right,hitRect.bottom);
		if(security)return true; // 已知安全遮挡后不再获取屏幕像素。
		bool sourceAboveOverlay=false;
		for(HWND above=GetWindow(overlay,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV))
			if(above==test.source){sourceAboveOverlay=true;break;}
		PrintWindowTestLog("[FreezeCaptureLive] windows stage=%s center_hit=%s source_above_overlay=%u\n",stage,
			hit==test.source?"source":hit==overlay?"overlay":hit==test.host?"host":hit==test.child?"child":"other",sourceAboveOverlay);
		const auto log=[&](const char* role,HWND window)
		{
			RECT r{};const bool bounds=GetWindowRect(window,&r)!=FALSE;
			DWORD cloaked=0;const HRESULT cloakResult=DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
			PrintWindowTestLog("[FreezeCaptureLive] cloak stage=%s role=%s hr=0x%08lX value=%lu\n",stage,role,static_cast<unsigned long>(cloakResult),cloaked);
			PrintWindowTestLog("[FreezeCaptureLive] window stage=%s role=%s valid=%u visible=%u iconic=%u owner=%p topmost=%u bounds_ok=%u bounds=(%ld,%ld,%ld,%ld)\n",
				stage,role,IsWindow(window),IsWindowVisible(window),IsIconic(window),GetWindow(window,GW_OWNER),
				(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,bounds,r.left,r.top,r.right,r.bottom);
		};
		log("source",test.source);log("overlay",overlay);log("host",test.host);log("child",test.child);
		if(overlayRole==WindowRole::ScrollFollowDebug)return security; // 诊断窗的真实客户区由短夹具单独核验。
		unsigned patternMatches=0,patternTotal=0,blueMatches=0;
		const HDC screen=GetDC(nullptr);
		if(screen)for(int row=0;row<patternRows;++row)for(int column=0;column<patternColumns;++column)
		{
			const int x=(2*column+1)*test.width/(2*patternColumns),y=(2*row+1)*test.height/(2*patternRows);
			const bool inOverlay=x>=test.width/3 && x<2*test.width/3 && y>=test.height/3 && y<2*test.height/3;
			const COLORREF value=GetPixel(screen,x,y);
			if(inOverlay)blueMatches+=value==RGB(0,0,255);
			else{++patternTotal;patternMatches+=value==PatternColor(1,column,row);}
		}
		if(screen)ReleaseDC(nullptr,screen);
		PrintWindowTestLog("[FreezeCaptureLive] visibility stage=%s pattern_outside_overlay=%u/%u blue=%u/9 screen_dc=%u\n",stage,patternMatches,patternTotal,blueMatches,screen!=nullptr);
		return security;
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
			test.oldFrames, test.invalidFrames, test.threadMismatches.load(), test.apiFailures.load());
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
static int RunFreezeCaptureLiveCase(bool debugExclusion)
{
	// 安全层门在任何Display/窗口/屏幕读回之前，不能以同名Default桌面代替此检查。
	if(LiveSecurityInterfacePresent()) { std::fputs("[FreezeCaptureLive] blocked=security-interface-before-initialization\n",stderr); return 4; }
	// 只驱动正式Start/Stop和原producer；旧owner定时读回入口不运行。
	PrintWindowTest test;
	const WindowRole overlayRole = debugExclusion ? WindowRole::ScrollFollowDebug : WindowRole::Bar;
	RECT debugBounds{}, debugClient{}, debugPreview{};
	int captureLeft = 0, captureTop = 0;
	struct Observations
	{
		struct Sample
		{
			std::uint64_t session=0,sequence=0;
			int matched=0;
			Capture::TimePoint timestamp{};
			double readMs=0;
		};
		std::mutex mutex;
		std::vector<Sample> samples;
		std::array<unsigned, 5> phases{};
		std::array<std::vector<std::uint32_t>, 5> crops;
		std::vector<double> readMs;
		std::atomic<DWORD> worker{};
		std::atomic<unsigned> cancelledCallbacks{};
		unsigned unmatched = 0, old = 0, wrongThread = 0, sequenceErrors = 0;
	};
	const auto observations = std::make_shared<Observations>();
	const auto previousLogger = IDTLogger;
	std::jthread worker, deadlineWorker;
	HANDLE cleanupDone = nullptr;
	bool displayStarted = false, serviceStarted = false, patternRegistered = false;
	DWORD initialGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	DWORD initialUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
	const auto pumpUntil = [](auto&& predicate, DWORD timeout)
	{
		const ULONGLONG end = GetTickCount64() + timeout;
		do
		{
			if(LiveSecurityInterfacePresent())throw std::runtime_error("security-interface-appeared");
			if (predicate()) return true;
			(void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
			MSG message{};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
		} while (GetTickCount64() < end);
		return predicate();
	};
	try
	{
		const auto parent = std::filesystem::current_path() / L"Build" / L"ScrollFollow";
		const auto directorySafe = [](const std::filesystem::path& path)
		{
			const DWORD a = GetFileAttributesW(path.c_str());
			return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
		};
		auto checked = parent.root_path();
		if (!parent.is_absolute() || !directorySafe(checked) ||
			GetFileAttributesW((std::filesystem::current_path()/L"InkeysRepo.sln").c_str()) == INVALID_FILE_ATTRIBUTES)
			throw std::runtime_error("private-root");
		for (const auto& component : parent.relative_path()) { checked /= component; if (!directorySafe(checked)) throw std::runtime_error("reparse-parent"); }
		test.output = (parent / (L"live-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()))).native();
		if (test.output.size() >= MAX_PATH-40 || !CreateDirectoryW(test.output.c_str(),nullptr)) throw std::runtime_error("create-new-root");
		test.log = CreateFileW((test.output+L"/result.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
		cleanupDone = CreateEventW(nullptr,TRUE,FALSE,nullptr);
		if (test.log == INVALID_HANDLE_VALUE || !cleanupDone) throw std::runtime_error("fixture-log");
		printWindowTest = &test;
		IDTLogger = std::make_shared<spdlog::logger>("FreezeCaptureLive",std::make_shared<spdlog::sinks::null_sink_mt>());
		PrintWindowTestLog("[FreezeCaptureLive] output=%ls pid=%lu main_tid=%lu formal_api=1 callback_registered=0 no_WGC=1 no_affinity=1\n",test.output.c_str(),GetCurrentProcessId(),GetCurrentThreadId());
		deadlineWorker = std::jthread([cleanupDone]
		{
			// 专用进程最终截止只报告失败；不强杀线程/提前释放未返回PW所借资源。
			if (WaitForSingleObject(cleanupDone,90000) != WAIT_OBJECT_0) TerminateProcess(GetCurrentProcess(),3);
		});
		if(!LiveDesktopAvailable("before-windows"))throw std::runtime_error("interactive-desktop-unavailable");
		if (!Inkeys::Display::Initialize()) throw std::runtime_error("display-initialize");
		displayStarted = true;
		test.width = GetSystemMetrics(SM_CXSCREEN); test.height = GetSystemMetrics(SM_CYSCREEN);
		if(test.width<320 || test.height<257)throw std::runtime_error("fixture-screen-too-small");
		WNDCLASSW pattern{}; pattern.hInstance=GetModuleHandleW(nullptr); pattern.lpfnWndProc=PrintWindowPatternProc;
		pattern.lpszClassName=L"Inkeys.FreezeCaptureLive.Pattern";
		if (!RegisterClassW(&pattern)) throw std::runtime_error("pattern-class");
		patternRegistered=true;
		test.source=CreateWindowExW((debugExclusion?0:WS_EX_TOPMOST)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pattern.lpszClassName,L"Inkeys dedicated live capture pattern",WS_POPUP,
			0,0,test.width,test.height,nullptr,nullptr,pattern.hInstance,nullptr);
		if (!test.source) throw std::runtime_error("pattern-window");
		ShowWindow(test.source,SW_SHOWNOACTIVATE); UpdateWindow(test.source);
		Inkeys::Window::WindowSpec host;
		host.role=WindowRole::MagnifierHost; host.className=L"Inkeys.FreezeCaptureLive.Host";
		host.width=test.width; host.height=test.height-1; host.style=WS_POPUP|WS_CLIPCHILDREN;
		host.exStyle=WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW; host.windowProc=MagnifierHostWindowWndProc;
		host.bindMessages=false; host.beforeCreate=PrepareMagnifierWindow; host.created=MagnifierHostCreated; host.destroyed=ShutdownMagnifierWindow;
		Inkeys::Window::WindowSpec child;
		child.role=WindowRole::MagnifierChild; child.className=WC_MAGNIFIER;
		child.width=test.width; child.height=test.height-1; child.style=WS_CHILD|WS_VISIBLE|MS_CLIPAROUNDCURSOR;
		child.exStyle=WS_EX_NOACTIVATE; child.bindMessages=false; child.created=MagnifierChildCreated;
		Inkeys::Window::WindowSpec overlay;
		overlay.role=WindowRole::Bar; overlay.className=L"Inkeys.FreezeCaptureLive.Overlay";
		overlay.x=test.width/3; overlay.y=test.height/3; overlay.width=test.width/3; overlay.height=test.height/3;
		overlay.style=WS_POPUP; overlay.exStyle=WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
		overlay.windowProc=PrintWindowOverlayProc; overlay.bindMessages=false;
		// 当前WindowService的Bar owner固定为Drawpad；补全隐藏基础链，不能让专用overlay脱离原根。
		const auto placeholder=[&](WindowRole role,const wchar_t* name)
		{
			Inkeys::Window::WindowSpec spec; spec.role=role;spec.className=name;
			spec.width=test.width;spec.height=test.height-1;spec.style=WS_POPUP|WS_CLIPCHILDREN;
			spec.exStyle=WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;spec.windowProc=DefWindowProcW;spec.bindMessages=false;
			if(role==WindowRole::DrawpadPresentation)spec.exStyle|=WS_EX_LAYERED|WS_EX_TRANSPARENT;
			return spec;
		};
		auto& service=Inkeys::Window::GetService();
		std::vector<Inkeys::Window::WindowSpec> specs{host,child,placeholder(WindowRole::Freeze,L"Inkeys.FreezeCaptureLive.Freeze"),
			placeholder(WindowRole::DrawpadPresentation,L"Inkeys.FreezeCaptureLive.Auxiliary"),
			placeholder(WindowRole::Drawpad,L"Inkeys.FreezeCaptureLive.Drawpad")};
		if(!debugExclusion)specs.push_back(overlay);
		if (!service.Start(std::move(specs)) || !magnificationCreateReady)
			throw std::runtime_error("original-pair-start");
		serviceStarted=true; test.host=service.Handle(WindowRole::MagnifierHost); test.child=service.Handle(WindowRole::MagnifierChild);
		const bool overlayShown=debugExclusion || service.Show(overlayRole);
		const bool overlayTopmost=service.SetOverlayTopmost(true);
		PrintWindowTestLog("[FreezeCaptureLive] overlay_show=%u overlay_topmost=%u overlay_owner=%p root=%p\n",overlayShown,overlayTopmost,
			GetWindow(service.Handle(overlayRole),GW_OWNER),service.OverlayRoot());
		if (!overlayShown || !overlayTopmost) throw std::runtime_error("overlay-show");
		if(LogLiveWindows("before-source-order",overlayRole))throw std::runtime_error("security-interface-blocks-fixture");
		// 只激活/提升专用SOURCE，再恢复本组排除层；不改变外部窗口或系统桌面。
		const bool sourceActivated=SetForegroundWindow(test.source)!=FALSE;
		const bool sourceRaised=SetWindowPos(test.source,debugExclusion?HWND_TOP:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOOWNERZORDER)!=FALSE;
		const bool ownGroupRaised=service.SetOverlayTopmost(true);
		PrintWindowTestLog("[FreezeCaptureLive] own_source_activate=%u own_source_raise=%u own_overlay_raise=%u foreground_own_source=%u\n",
			sourceActivated,sourceRaised,ownGroupRaised,GetForegroundWindow()==test.source);
		// 专用SOURCE不是产品角色，显式置于本组根之后，避免两个独立TOPMOST链相互盖住；不动用户窗口。
		const bool sourceBehind=SetWindowPos(test.source,debugExclusion?HWND_NOTOPMOST:service.OverlayRoot(),0,0,0,0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER)!=FALSE;
		PrintWindowTestLog("[FreezeCaptureLive] source_behind_own_mag_root=%u error=%lu\n",sourceBehind,sourceBehind?0:GetLastError());
		LogLiveWindows("after-source-order",overlayRole);
		if(!sourceBehind)throw std::runtime_error("dedicated-source-order");
		captureLeft=test.width/2-128;captureTop=test.height/2-96;
		if(debugExclusion)
		{
			// 使用同一个产品 factory；专用 Source 保持非置顶，诊断窗沿用独立置顶属性。
			if(test.width<960 || test.height<640)throw std::runtime_error("fixture-screen-too-small-for-debug");
			const RECT bounds{(test.width-960)/2,(test.height-640)/2,(test.width+960)/2,(test.height+640)/2};
			if(!Inkeys::UI::ScrollFollowDebug::Open(nullptr,&bounds))throw std::runtime_error("production-debug-open");
			const HWND debug=service.Handle(overlayRole);RECT client{};POINT origin{};
			// 仅显式摆放夹具自己的置顶诊断窗，再核验真实像素排除。
			if(!SetWindowPos(debug,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER))
				throw std::runtime_error("debug-fixture-visible-order");
			if(LogLiveWindows("debug-before-source-behind",overlayRole))throw std::runtime_error("security-interface-blocks-fixture");
			// 在 Source 自己的线程保留非置顶带；不把它插入置顶窗之间而升级 Source。
			if(!SetWindowPos(test.source,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER))
				throw std::runtime_error("source-fixture-behind-debug");
			PrintWindowTestLog("[ScrollFollowDebugExclusion] own_source_behind_debug=1 foreground_source=%u source_previous=%p debug_next=%p\n",GetForegroundWindow()==test.source,GetWindow(test.source,GW_HWNDPREV),GetWindow(debug,GW_HWNDNEXT));
			if(LogLiveWindows("debug-after-source-behind",overlayRole))throw std::runtime_error("security-interface-blocks-fixture");
			if(!pumpUntil([]{return Inkeys::UI::ScrollFollowDebug::ReadProbe().paints>0;},3000))
				throw std::runtime_error("debug-first-production-paint");
			const auto probe=Inkeys::UI::ScrollFollowDebug::ReadProbe();
			if(!GetWindowRect(debug,&debugBounds) || !GetClientRect(debug,&client) || !ClientToScreen(debug,&origin))
				throw std::runtime_error("debug-actual-client-geometry");
			debugClient={origin.x,origin.y,origin.x+client.right,origin.y+client.bottom};
			// 实际已绘制布局决定覆盖 ROI，不再依赖 P2 蓝色骨架的固定矩形。
			debugPreview=probe.previewRect;OffsetRect(&debugPreview,origin.x,origin.y);
			captureLeft=(debugPreview.left+debugPreview.right)/2-128;
			captureTop=(debugPreview.top+debugPreview.bottom)/2-96;
			if(captureLeft<debugPreview.left || captureTop<debugPreview.top || captureLeft+256>debugPreview.right
				|| captureTop+192>debugPreview.bottom || (debugBounds.left<320 && debugBounds.top<256)
				|| !(GetWindowLongPtrW(debug,GWL_EXSTYLE)&WS_EX_TOPMOST) || GetWindow(debug,GW_OWNER)
				|| (GetWindowLongPtrW(test.source,GWL_EXSTYLE)&WS_EX_TOPMOST)
				|| service.OwnerThreadId(overlayRole)!=GetWindowThreadProcessId(debug,nullptr)
				|| service.OwnerThreadId(overlayRole)==service.OwnerThreadId(WindowRole::MagnifierHost))
				throw std::runtime_error("topmost-debug-client-contract");
			PrintWindowTestLog("[ScrollFollowDebugExclusion] debug=%p owner_thread=%lu topmost=1 owner_null=1 source_topmost=%u client=(%ld,%ld,%ld,%ld) covered_capture_roi=(%d,%d,256,192)\n",
				debug,service.OwnerThreadId(overlayRole),(GetWindowLongPtrW(test.source,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,
				debugClient.left,debugClient.top,debugClient.right,debugClient.bottom,captureLeft,captureTop);
		}
		const auto verifySourceScreenRoi=[&](HDC dc,int phase,bool save)
		{
			Inkeys::Graphics::DibSurface roi;bool exact=roi.resize(256,192)
				&& BitBlt(roi.dc(),0,0,256,192,dc,64,64,SRCCOPY)!=FALSE;
			GdiFlush();
			if(exact)for(int y=0;y<192;++y)for(int x=0;x<256;++x)
			{
				const auto pixel=roi.pixels()[static_cast<std::size_t>(y)*256+x];
				const auto c=PatternColor(phase,((x+65)*9-1)/test.width,((y+65)*7-1)/test.height);
				exact &= ((pixel>>16)&255)==GetRValue(c) && ((pixel>>8)&255)==GetGValue(c) && (pixel&255)==GetBValue(c);
			}
			if(exact && save){for(auto& pixel:roi.pixels())pixel|=0xFF000000;(void)roi.savePng(test.output+L"/screen-dedicated-roi.png");}
			return exact;
		};
		const auto verifyDebugScreen=[&](int phase)
		{
			unsigned outside=0,matched=0,background=0;const HDC dc=GetDC(nullptr);
			if(!dc)return false;
			const auto probe=Inkeys::UI::ScrollFollowDebug::ReadProbe();
			RECT currentBounds{};
			const bool geometry=GetWindowRect(service.Handle(overlayRole),&currentBounds) && EqualRect(&currentBounds,&debugBounds);
			for(int row=0;row<patternRows;++row)for(int column=0;column<patternColumns;++column)
			{
				const POINT point{(2*column+1)*test.width/(2*patternColumns),(2*row+1)*test.height/(2*patternRows)};
				if(PtInRect(&debugBounds,point))continue;
				++outside;matched+=GetPixel(dc,point.x,point.y)==PatternColor(phase,column,row);
			}
			for(int row=0;row<3;++row)for(int column=0;column<3;++column)
				background+=GetPixel(dc,captureLeft+32+96*column,captureTop+24+72*row)==probe.previewBackground;
			const bool exact=verifySourceScreenRoi(dc,phase,false);
			ReleaseDC(nullptr,dc);
			const bool hit=WindowFromPoint({captureLeft+128,captureTop+96})==service.Handle(overlayRole);
			PrintWindowTestLog("[ScrollFollowDebugExclusion] screen_phase=%d outside_pattern=%u/%u production_preview_background=%u/9 owned_client_hit=%u uncovered_roi_exact=%u painted=%u geometry_stable=%u\n",phase,matched,outside,background,hit,exact,probe.paints>0,geometry);
			return geometry && probe.open && probe.paints>0 && outside>0 && matched==outside && background==9 && hit && exact;
		};
		if(debugExclusion)
		{
			if(!pumpUntil([&]{return test.painted.load()==1 && verifyDebugScreen(1);},3000))
				throw std::runtime_error("debug-visible-or-uncovered-source-GT");
		}
		else
		{
		COLORREF centerPixel=CLR_INVALID;
		if (!pumpUntil([&]
		{
			const HDC screen=GetDC(nullptr); centerPixel=screen?GetPixel(screen,test.width/2,test.height/2):CLR_INVALID;
			if(screen)ReleaseDC(nullptr,screen); return centerPixel==RGB(0,0,255) && test.painted.load()==1;
		},3000))
		{
			(void)LiveDesktopAvailable("overlay-missing");
			LogLiveWindows("overlay-missing");
			PrintWindowTestLog("[FreezeCaptureLive] overlay_missing center_rgb=0x%08lX painted=%d source_valid=%u visible=%u overlay_valid=%u visible=%u\n",
				centerPixel,test.painted.load(),IsWindow(test.source),IsWindowVisible(test.source),IsWindow(service.Handle(WindowRole::Bar)),IsWindowVisible(service.Handle(WindowRole::Bar)));
			throw std::runtime_error("dedicated-overlay-not-on-interactive-screen");
		}
		unsigned screenMatches=0,screenBlue=0;
		const HDC screen=GetDC(nullptr);
		if(!screen)throw std::runtime_error("screen-dc");
		for(int row=0;row<patternRows;++row)for(int column=0;column<patternColumns;++column)
		{
			const int x=(2*column+1)*test.width/(2*patternColumns),y=(2*row+1)*test.height/(2*patternRows);
			const bool blue=x>=test.width/3 && x<2*test.width/3 && y>=test.height/3 && y<2*test.height/3;
			const COLORREF expected=blue?RGB(0,0,255):PatternColor(1,column,row);
			if(GetPixel(screen,x,y)==expected){++screenMatches;if(blue)++screenBlue;}
		}
		const bool screenExact=verifySourceScreenRoi(screen,1,true);
		ReleaseDC(nullptr,screen);
		PrintWindowTestLog("[FreezeCaptureLive] screen_gt=%u/63 blue_overlay=%u/9 screen_roi_exact=%u roi=(64,64,256,192)\n",screenMatches,screenBlue,screenExact);
		if(screenMatches!=63 || !screenExact)throw std::runtime_error("dedicated-screen-GT");
		}
		const auto filters=BuildCurrentFilterList();
		if (std::find(filters.begin(),filters.end(),service.Handle(overlayRole))==filters.end() ||
			std::find(filters.begin(),filters.end(),test.source)!=filters.end()) throw std::runtime_error("original-filter-list");
		const auto getCallback=reinterpret_cast<GetScalingCallback>(GetProcAddress(GetModuleHandleW(L"Magnification.dll"),"MagGetImageScalingCallback"));
		if (getCallback && getCallback(test.child)) throw std::runtime_error("unexpected-scaling-callback");
		PrintWindowTestLog("[FreezeCaptureLive] owner_tid=%lu host=%p child=%p overlay=%p source=%p primary=%dx%d filters=%zu source_unfiltered=1 cold_alpha0=1 get_callback_null=%u\n",
			service.OwnerThreadId(WindowRole::MagnifierHost),test.host,test.child,service.Handle(overlayRole),test.source,test.width,test.height,filters.size(),getCallback?1u:0u);
		worker=std::jthread([&]{observations->worker.store(GetCurrentThreadId());MagnifierThread();});
		if (!pumpUntil([&]{const auto s=magnifierCoordinator.Snapshot();return Capture::GetSnapshot().ready && !s.visible && s.processedRevision>=Inkeys::UI::Freeze::Snapshot().revision;},3000))
			throw std::runtime_error("formal-worker-ready");
		const auto began=Capture::Clock::now();
		const Capture::FrameCallback receive=[&,observations,began](const Capture::Frame& frame)
		{
			std::scoped_lock lock(observations->mutex);
			const int painted=test.painted.load(); int matched=0;
			for (int phase=1;phase<=4;++phase)
			{
				unsigned count=0;
				for(int row=0;row<patternRows;++row)for(int column=0;column<patternColumns;++column)
				{
					const int x=(2*column+1)*test.width/(2*patternColumns), y=(2*row+1)*test.height/(2*patternRows);
					const auto p=frame.bgra[static_cast<std::size_t>(y)*frame.width+x]; const auto c=PatternColor(phase,column,row);
					if(abs(int((p>>16)&255)-GetRValue(c))<=3 && abs(int((p>>8)&255)-GetGValue(c))<=3 && abs(int(p&255)-GetBValue(c))<=3)++count;
				}
				if(count==63){matched=phase;break;}
			}
			if(GetCurrentThreadId()!=observations->worker.load() || GetCurrentThreadId()==test.ownerThread)++observations->wrongThread;
			if(!matched)++observations->unmatched;
			else
			{
				++observations->phases[matched]; if(matched<painted)++observations->old;
				if(observations->crops[matched].empty())
				{
					std::vector<std::uint32_t> crop(256*192); bool exact=true;
					for(int y=0;y<192;++y)for(int x=0;x<256;++x)
					{
						const int sx=captureLeft+x,sy=captureTop+y;
						const auto pixel=frame.bgra[static_cast<std::size_t>(sy)*frame.width+sx];
						const auto c=PatternColor(matched,(std::min)(8,((sx+1)*9-1)/test.width),(std::min)(6,((sy+1)*7-1)/test.height));
						if(abs(int((pixel>>16)&255)-GetRValue(c))>3 || abs(int((pixel>>8)&255)-GetGValue(c))>3 || abs(int(pixel&255)-GetBValue(c))>3)exact=false;
						crop[static_cast<std::size_t>(y)*256+x]=pixel;
					}
					// 只保存逐像素确认是专用内容的ROI；未知/不完整帧不写图像。
					if(exact)observations->crops[matched]=std::move(crop);
				}
			}
			if(!observations->samples.empty())
			{
				const auto& previous=observations->samples.back();
				if(frame.session<previous.session || (frame.session==previous.session
					? frame.sequence!=previous.sequence+1 : frame.sequence!=1))++observations->sequenceErrors;
			}
			else if(frame.sequence!=1)++observations->sequenceErrors;
			observations->samples.push_back({frame.session,frame.sequence,matched,frame.timestamp,frame.statistics.lastReadDuration.count()/1000.0});
			observations->readMs.push_back(frame.statistics.lastReadDuration.count()/1000.0);
			BYTE alpha=255; DWORD flags=0; COLORREF key=0; (void)GetLayeredWindowAttributes(test.host,&key,&alpha,&flags);
			PrintWindowTestLog("[FreezeCaptureLive] frame session=%llu seq=%llu tid=%lu worker=%lu owner=%lu sample_ms=%.3f read_ms=%.3f painted=%d matched=%d static_count=%u host_visible=%u child_visible=%u host_alpha=%u quality=%u variance=%.3f accepted=%llu rejected=%llu api=%llu skipped=%llu\n",
				frame.session,frame.sequence,GetCurrentThreadId(),observations->worker.load(),test.ownerThread,
				std::chrono::duration<double,std::milli>(frame.timestamp-began).count(),frame.statistics.lastReadDuration.count()/1000.0,
				painted,matched,matched?observations->phases[matched]:0,IsWindowVisible(test.host),IsWindowVisible(test.child),alpha,
				static_cast<unsigned>(frame.quality.status),frame.quality.variance,frame.statistics.accepted,frame.statistics.rejected,frame.statistics.apiFailures,frame.statistics.skipped);
		};
		const auto started=Capture::Start(receive);
		PrintWindowTestLog("[FreezeCaptureLive] start_status=%u session=%llu\n",static_cast<unsigned>(started.status),started.session);
		if(started.status!=Capture::StartStatus::Started)throw std::runtime_error("formal-start");
		const int phaseCount=debugExclusion?2:4;
		for(int phase=1;phase<=phaseCount;++phase)
		{
			if(phase>1){test.phase.store(phase);SendMessageW(test.source,patternPaintMessage,0,0);}
			if(debugExclusion && !pumpUntil([&]{return verifyDebugScreen(phase);},3000))
				throw std::runtime_error("debug-changed-source-screen-GT");
			if(!pumpUntil([&]{std::scoped_lock lock(observations->mutex);return observations->phases[phase]>=3 && !observations->crops[phase].empty();},5000))
				throw std::runtime_error("formal-pixels-static-or-changed-phase-timeout");
			if(debugExclusion && !verifyDebugScreen(phase))
				throw std::runtime_error("debug-coverage-lost-after-formal-frames");
		}
		if(debugExclusion)
		{
			Capture::Stop();
			if(!pumpUntil([]{return !Capture::GetSnapshot().occupied;},3000))throw std::runtime_error("debug-stop-drain");
			for(int phase=1;phase<=phaseCount;++phase)
			{
				Inkeys::Graphics::DibSurface crop;
				if(!crop.resize(256,192))throw std::runtime_error("debug-roi-dib");
				std::copy(observations->crops[phase].begin(),observations->crops[phase].end(),crop.pixels().begin());
				if(!crop.savePng(test.output+L"/debug-phase-"+std::to_wstring(phase)+L"-covered-roi.png"))throw std::runtime_error("debug-roi-png");
			}
			const HWND debug=service.Handle(overlayRole);
			SendMessageW(debug,WM_CLOSE,0,0);
			const auto closedFilters=BuildCurrentFilterList();
			if(service.Handle(overlayRole) || Inkeys::UI::ScrollFollowDebug::Handle() || IsWindow(debug)
				|| std::find(closedFilters.begin(),closedFilters.end(),debug)!=closedFilters.end())
				throw std::runtime_error("debug-close-stale-filter");
			if(!Inkeys::UI::ScrollFollowDebug::Open(nullptr,&debugBounds))throw std::runtime_error("debug-explicit-recreate");
			const HWND recreated=service.Handle(overlayRole);const auto recreatedFilters=BuildCurrentFilterList();
			if(std::count(recreatedFilters.begin(),recreatedFilters.end(),recreated)!=1
				|| std::find(recreatedFilters.begin(),recreatedFilters.end(),test.source)!=recreatedFilters.end())
				throw std::runtime_error("debug-recreated-filter");
			SendMessageW(recreated,WM_CLOSE,0,0);
			if(service.Handle(overlayRole) || !service.Running())throw std::runtime_error("debug-final-close");
			PrintWindowTestLog("[ScrollFollowDebugExclusion] phases=2 matched_frames=(%u,%u) covered_roi_exact=1 close_cleared=1 recreate_filter_current=1 long_tail_skipped=1\n",observations->phases[1],observations->phases[2]);
		}
		else
		{
		// 目标20Hz不是实测帧率；延长同一静止phase，单独记录真实Read与间隔。
		const auto staticBegan=Capture::Clock::now();
		unsigned staticBefore=0;
		{std::scoped_lock lock(observations->mutex);staticBefore=observations->phases[4];}
		if(!pumpUntil([&]{std::scoped_lock lock(observations->mutex);return Capture::Clock::now()-staticBegan>=std::chrono::seconds(5)
			&& observations->phases[4]>=staticBefore+20;},8000))throw std::runtime_error("static-sampling-timeout");
		Capture::Stop();
		if(!pumpUntil([]{return !Capture::GetSnapshot().occupied;},3000))throw std::runtime_error("formal-stop-drain");
		const auto staticSnapshot=Capture::GetSnapshot();
		std::vector<double> staticReads,staticIntervals;
		Capture::TimePoint firstStatic{},lastStatic{};
		{
			std::scoped_lock lock(observations->mutex);
			for(const auto& sample:observations->samples)if(sample.session==started.session && sample.timestamp>=staticBegan)
			{
				if(sample.matched!=4)throw std::runtime_error("static-frame-GT");
				staticReads.push_back(sample.readMs);
				if(lastStatic!=Capture::TimePoint{})staticIntervals.push_back(std::chrono::duration<double,std::milli>(sample.timestamp-lastStatic).count());
				else firstStatic=sample.timestamp;
				lastStatic=sample.timestamp;
			}
		}
		const auto reportDistribution=[](const char* name,std::vector<double> values)
		{
			if(values.empty())throw std::runtime_error("empty-distribution");
			double sum=0;for(const double value:values)sum+=value;
			std::sort(values.begin(),values.end());
			PrintWindowTestLog("[FreezeCaptureLive] distribution=%s samples=%zu mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
				name,values.size(),sum/values.size(),values[(values.size()-1)/2],values[(values.size()-1)*95/100],values.back());
		};
		reportDistribution("static-read",staticReads);reportDistribution("static-interval",staticIntervals);
		PrintWindowTestLog("[FreezeCaptureLive] static_frames=%zu span_ms=%.3f actual_fps=%.3f target_fps=20 skipped=%llu rejected=%llu\n",staticReads.size(),
			std::chrono::duration<double,std::milli>(lastStatic-firstStatic).count(),(staticReads.size()-1)/std::chrono::duration<double>(lastStatic-firstStatic).count(),
			staticSnapshot.statistics.skipped,staticSnapshot.statistics.rejected);
		for(int phase=1;phase<=4;++phase)
		{
			Inkeys::Graphics::DibSurface crop;
			if(!crop.resize(256,192))throw std::runtime_error("roi-dib");
			std::copy(observations->crops[phase].begin(),observations->crops[phase].end(),crop.pixels().begin());
			if(!crop.savePng(test.output+L"/phase-"+std::to_wstring(phase)+L"-roi.png"))throw std::runtime_error("roi-png");
		}
		std::uint64_t previousSession=started.session;
		const auto totalFrames=[&]{std::scoped_lock lock(observations->mutex);return observations->samples.size();};
		const auto paint=[&](int phase){test.phase.store(phase);SendMessageW(test.source,patternPaintMessage,0,0);};
		const auto waitFrames=[&](std::uint64_t session,int phase,unsigned required)
		{
			if(!pumpUntil([&]
			{
				std::scoped_lock lock(observations->mutex);unsigned count=0;
				for(const auto& sample:observations->samples)if(sample.session==session && sample.matched==phase)++count;
				return count>=required;
			},5000))throw std::runtime_error("repeat-session-GT-timeout");
		};
		const auto verifyIdle=[&](const char* label,std::uint64_t session)
		{
			if(!pumpUntil([]{return !Capture::GetSnapshot().occupied;},3000))throw std::runtime_error("repeat-stop-drain");
			const auto snapshot=Capture::GetSnapshot();const auto count=totalFrames();const auto since=GetTickCount64();
			if(!pumpUntil([&]{return GetTickCount64()-since>=150;},1000) || totalFrames()!=count)
				throw std::runtime_error("callback-after-drain");
			if(snapshot.session!=session || snapshot.publishing || snapshot.inFlight || !snapshot.ready ||
				IsWindowVisible(test.host) || IsWindowVisible(test.child))throw std::runtime_error("stopped-state-or-conceal");
			PrintWindowTestLog("[FreezeCaptureLive] idle label=%s session=%llu seq=%llu callbacks=%zu publishing=0 occupied=0 inflight=0 hidden=1 no_late_callback_ms=150 attempts=%llu accepted=%llu rejected=%llu late_results=%llu reason=%u\n",
				label,session,snapshot.sequence,count,snapshot.statistics.attempts,snapshot.statistics.accepted,snapshot.statistics.rejected,
				snapshot.statistics.lateResults,static_cast<unsigned>(snapshot.reason));
		};
		const auto startSession=[&](const char* label,const Capture::FrameCallback& callback)
		{
			if(LiveSecurityInterfacePresent())throw std::runtime_error("security-interface-before-repeat");
			const auto result=Capture::Start(callback);
			if(result.status!=Capture::StartStatus::Started || result.session!=previousSession+1 ||
				service.Handle(WindowRole::MagnifierHost)!=test.host || service.Handle(WindowRole::MagnifierChild)!=test.child ||
				service.OwnerThreadId(WindowRole::MagnifierHost)!=test.ownerThread)throw std::runtime_error("repeat-start-session-or-original-pair");
			previousSession=result.session;
			const auto busy=Capture::Start(receive);
			if(busy.status!=Capture::StartStatus::Busy || busy.session!=result.session)throw std::runtime_error("repeat-start-not-Busy");
			PrintWindowTestLog("[FreezeCaptureLive] started label=%s session=%llu busy_status=%u original_pair=1 owner=%lu\n",label,result.session,
				static_cast<unsigned>(busy.status),test.ownerThread);
			return result.session;
		};
		const auto freezeCycle=[&](const char* label,std::uint64_t activeCapture)
		{
			if(Inkeys::UI::Freeze::Snapshot().active)throw std::runtime_error("freeze-already-active");
			Inkeys::UI::Freeze::Toggle();const auto request=Inkeys::UI::Freeze::Snapshot();
			if(!request.active)throw std::runtime_error("freeze-toggle-not-active");
			// 用原状态观察者和原producer排空再Reveal，不直接调用Mag或伪造完成。
			if(!pumpUntil([&]
			{
				const auto coordinated=magnifierCoordinator.Snapshot();const auto capture=Capture::GetSnapshot();
				return coordinated.visible && coordinated.appliedRevision==request.revision && !capture.occupied && !capture.inFlight;
			},5000))throw std::runtime_error("original-freeze-reveal-timeout");
			BYTE alpha=0;DWORD flags=0;COLORREF key=0;
			const auto revoked=Capture::GetSnapshot();
			if(!GetLayeredWindowAttributes(test.host,&key,&alpha,&flags) || alpha!=255 || !(flags&LWA_ALPHA) ||
				!IsWindowVisible(test.host) || !IsWindowVisible(test.child) || revoked.publishing ||
				(activeCapture && (revoked.session!=activeCapture || revoked.reason!=Capture::StopReason::FreezeActive)))
				throw std::runtime_error("original-freeze-reveal-or-revoke");
			const auto blocked=Capture::Start(receive);
			if(blocked.status!=Capture::StartStatus::FreezeActive)throw std::runtime_error("capture-during-freeze-not-blocked");
			const auto count=totalFrames();const auto since=GetTickCount64();
			if(!pumpUntil([&]{return GetTickCount64()-since>=150;},1000) || totalFrames()!=count)throw std::runtime_error("callback-after-freeze-drain");
			PrintWindowTestLog("[FreezeCaptureLive] freeze label=%s revision=%llu revealed=1 alpha=%u capture_session=%llu capture_drained=1 capture_reason=%u blocked_status=%u no_late_callback_ms=150\n",
				label,request.revision,alpha,revoked.session,static_cast<unsigned>(revoked.reason),static_cast<unsigned>(blocked.status));
			Inkeys::UI::Freeze::Toggle();const auto hidden=Inkeys::UI::Freeze::Snapshot();
			if(hidden.active || !pumpUntil([&]
			{
				const auto coordinated=magnifierCoordinator.Snapshot();return !coordinated.visible && coordinated.processedRevision>=hidden.revision
					&& !IsWindowVisible(test.host) && !IsWindowVisible(test.child);
			},3000))throw std::runtime_error("original-freeze-conceal-timeout");
			PrintWindowTestLog("[FreezeCaptureLive] freeze label=%s revision=%llu concealed=1 host_visible=0 child_visible=0\n",label,hidden.revision);
		};
		verifyIdle("initial",started.session);
		freezeCycle("before-repeats",0);
		DWORD repeatGdi=0,repeatUser=0;unsigned resourceChanges=0;
		for(unsigned round=1;round<=20;++round)
		{
			const int phase=1+round%4;paint(phase);
			const auto roundBegan=Capture::Clock::now();const auto session=startSession("repeat",receive);
			waitFrames(session,phase,2);Capture::Stop();verifyIdle("repeat",session);
			const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
			if(round==1){repeatGdi=gdi;repeatUser=user;}
			else if(gdi!=repeatGdi || user!=repeatUser)++resourceChanges;
			PrintWindowTestLog("[FreezeCaptureLive] repeat_round=%u/20 session=%llu phase=%d elapsed_ms=%.3f gdi=%lu user=%lu resource_changes=%u\n",
				round,session,phase,std::chrono::duration<double,std::milli>(Capture::Clock::now()-roundBegan).count(),gdi,user,resourceChanges);
		}
		if(resourceChanges)throw std::runtime_error("repeat-resource-count-changed");
		// Start后立即撤销：F0等待期内不允许产生回调，取消后仍由原producer收尾。
		const Capture::FrameCallback cancelled=[observations](const Capture::Frame&){observations->cancelledCallbacks.fetch_add(1);};
		const auto cancelSession=startSession("immediate-stop",cancelled);Capture::Stop();
		if(Capture::GetSnapshot().publishing)throw std::runtime_error("immediate-stop-still-publishing");
		verifyIdle("immediate-stop",cancelSession);
		if(observations->cancelledCallbacks.load()!=0)throw std::runtime_error("immediate-stop-published-F0");
		// 不加hook：观察在途不等于Stop命中；Read先提交时只记race-missed，排空后有限重试。
		bool readRevoked=false;
		for(unsigned attempt=1;attempt<=3 && !readRevoked;++attempt)
		{
			if(Capture::GetSnapshot().occupied)throw std::runtime_error("read-stop-retry-not-drained");
			const unsigned callbacksBefore=observations->cancelledCallbacks.load();
			const auto readCancelSession=startSession("read-stop",cancelled);
			if(!pumpUntil([]{const auto capture=Capture::GetSnapshot();return capture.inFlight && capture.statistics.attempts>0 && capture.statistics.accepted==0;},3000))
				throw std::runtime_error("actual-read-inflight-not-observed");
			Capture::Stop();const auto revokedRead=Capture::GetSnapshot();
			verifyIdle("read-stop",readCancelSession);
			const auto endedRead=Capture::GetSnapshot();
			const unsigned callbacks=observations->cancelledCallbacks.load()-callbacksBefore;
			if(endedRead.statistics.accepted!=revokedRead.statistics.accepted || callbacks!=endedRead.statistics.accepted)
				throw std::runtime_error("read-stop-published-after-revoke");
			if(revokedRead.statistics.accepted==0)
			{
				// lateResults只在Read返回后发现发布已撤销时增加，是实际撤销在途Read的凭据。
				if(endedRead.statistics.accepted!=0 || callbacks!=0 || endedRead.statistics.lateResults<1 ||
					revokedRead.reason!=Capture::StopReason::Requested)throw std::runtime_error("read-stop-actual-revoke-unproven");
				readRevoked=true;
			}
			PrintWindowTestLog("[FreezeCaptureLive] read_stop attempt=%u/3 session=%llu observed_inflight=1 actual_read_revoke=%u outcome=%s accepted_at_revoke=%llu accepted_final=%llu callback_count=%u late_results=%llu\n",
				attempt,readCancelSession,readRevoked,readRevoked?"revoked":"race-missed",revokedRead.statistics.accepted,
				endedRead.statistics.accepted,callbacks,endedRead.statistics.lateResults);
		}
		if(!readRevoked)throw std::runtime_error("read-stop-uncovered-after-race-retries");
		paint(3);const auto beforeFreeze=startSession("capture-to-freeze",receive);waitFrames(beforeFreeze,3,2);
		freezeCycle("capture-to-freeze",beforeFreeze);
		paint(2);const auto afterFreeze=startSession("after-freeze",receive);waitFrames(afterFreeze,2,3);
		Capture::Stop();verifyIdle("after-freeze",afterFreeze);
		const auto invalid=Capture::Start({});
		if(invalid.status!=Capture::StartStatus::InvalidArgument || Capture::GetSnapshot().occupied)throw std::runtime_error("invalid-callback-start");
		PrintWindowTestLog("[FreezeCaptureLive] matrix repeat_cycles=20 resources_stable=1 immediate_cancel_callbacks=0 read_stop_checked=1 freeze_cycles=2 post_freeze_frames=3 invalid_start_status=%u sequence_errors=%u\n",
			static_cast<unsigned>(invalid.status),observations->sequenceErrors);
		}
		if(observations->wrongThread || observations->sequenceErrors || observations->unmatched || test.apiFailures)
			throw std::runtime_error("formal-thread-sequence-GT-or-api-failure");
		test.result=0;
	}
	catch(const std::exception& e)
	{
		const DWORD failureError=GetLastError();
		const auto s=Capture::GetSnapshot();
		(void)LiveDesktopAvailable("failure");LogLiveWindows("failure",overlayRole);
		PrintWindowTestLog("[FreezeCaptureLive] FAIL reason=%s error=%lu ready=%u publishing=%u occupied=%u inflight=%u stop=%u quality=%u mean=%.6f variance=%.6f black=%.6f attempts=%llu accepted=%llu rejected=%llu api=%llu\n",
			e.what(),failureError,s.ready,s.publishing,s.occupied,s.inFlight,static_cast<unsigned>(s.reason),static_cast<unsigned>(s.quality.status),s.quality.meanLuma,s.quality.variance,s.quality.blackFraction,s.statistics.attempts,s.statistics.accepted,s.statistics.rejected,s.statistics.apiFailures);
		test.result=1;
	}
	Capture::Stop(); StopMagnifierCoordinator();
	if(worker.joinable())worker.join();
	if(serviceStarted || Inkeys::Window::GetService().Running())Inkeys::Window::GetService().StopAndJoin();
	Inkeys::UI::ScrollFollowDebug::ShutdownExports();
	if(test.source)DestroyWindow(test.source);
	if(patternRegistered)UnregisterClassW(L"Inkeys.FreezeCaptureLive.Pattern",GetModuleHandleW(nullptr));
	if(displayStarted)Inkeys::Display::Shutdown();
	if(IsWindow(test.host)||IsWindow(test.child))test.result=1;
	PrintWindowTestLog("[FreezeCaptureLive] result=%s unmatched=%u old=%u wrongThread=%u apiFailures=%u original_pair_destroyed=%u gdi=%lu->%lu user=%lu->%lu\n",
		test.result?"FAIL":"PASS",observations->unmatched,observations->old,observations->wrongThread,test.apiFailures.load(),!IsWindow(test.host)&&!IsWindow(test.child),
		initialGdi,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),initialUser,GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS));
	if(cleanupDone)SetEvent(cleanupDone);
	if(deadlineWorker.joinable())deadlineWorker.join();
	IDTLogger=previousLogger; printWindowTest=nullptr;
	if(test.log!=INVALID_HANDLE_VALUE)CloseHandle(test.log);
	if(cleanupDone)CloseHandle(cleanupDone);
	return test.result;
}


int RunFreezeCaptureLiveTest() { return RunFreezeCaptureLiveCase(false); }
int RunScrollFollowDebugExclusionTest() { return RunFreezeCaptureLiveCase(true); }

#else
namespace
{
	void TraceMagnifierApi(const char*, HWND, bool) noexcept {}
}
int RunFreezeMagnifierPrintWindowTest() { return 2; }
int RunFreezeCaptureLiveTest() { return 2; }
int RunScrollFollowDebugExclusionTest() { return 2; }
#endif

// 专用产品实屏入口复用上方原Mag及安全助手；关闭harness时只返回2。
#include "Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl"
