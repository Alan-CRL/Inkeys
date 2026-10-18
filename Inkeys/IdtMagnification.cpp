#include "IdtMagnification.h"
#include <winuser.h>

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <array>
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
			return MagSetWindowFilterList(targets_.child, MW_FILTERMODE_EXCLUDE,
				static_cast<int>(hwndList.size()), hwndList.data()) != FALSE;
		}

		[[nodiscard]] bool SubmitSource() noexcept
		{
			const RECT sourceRect = {
				0, 0,
				GetSystemMetrics(SM_CXSCREEN) - 1,
				GetSystemMetrics(SM_CYSCREEN) - 1,
			};
			return MagSetWindowSource(targets_.child, sourceRect) != FALSE;
		}

		[[nodiscard]] bool Invalidate() noexcept
		{
			return InvalidateRect(targets_.child, nullptr, TRUE) != FALSE;
		}

		[[nodiscard]] bool Redraw() noexcept
		{
			return RedrawWindow(targets_.child, nullptr, nullptr,
				RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN) != FALSE;
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

	if (!MagInitialize())
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 初始化 MagInitialize 失败");
		return false;
	}
	return true;
}

void MagnifierHostCreated(HWND hwnd)
{
	magnifierWindow = hwnd;
	if (!SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA) && IDTLogger)
		IDTLogger->error("[放大API线程][SetupMagnifier] 初始化 Host 透明度失败");
}

void MagnifierChildCreated(HWND hwnd)
{
	magnifierChild = hwnd;
	MAGTRANSFORM matrix{};
	matrix.v[0][0] = 1.0f;
	matrix.v[1][1] = 1.0f;
	matrix.v[2][2] = 1.0f;
	if (!MagSetWindowTransform(hwnd, &matrix))
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
	if (!MagSetColorEffect(hwnd, &effect))
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
	MagUninitialize();
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
