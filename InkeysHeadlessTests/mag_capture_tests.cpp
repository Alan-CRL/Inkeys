#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../Inkeys/Inkeys/Graphics/MagCaptureService.h"
#include "../Inkeys/Inkeys/Graphics/MagnificationRuntime.h"
#include "../Inkeys/Inkeys/Graphics/Surface.hpp"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.DiagnosticOutput.h"
#include <psapi.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

import Inkeys.Display;

namespace
{
	using namespace Inkeys::Graphics;
	struct StaScope
	{
		HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		~StaScope() { if (SUCCEEDED(result)) CoUninitialize(); }
	};
	struct PatternState
	{
		RECT rect{};
		volatile LONG requested = 1, painted = 0;
	};

	COLORREF PatternColor(LONG phase) noexcept
	{
		return RGB(40 + (phase % 4) * 45, 70 + (phase % 3) * 50, 210 - (phase % 3) * 45);
	}

	LRESULT CALLBACK PatternProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* state = reinterpret_cast<PatternState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (message == WM_CREATE)
		{
			state = static_cast<PatternState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			SetTimer(hwnd, 1, 16, nullptr);
			SetTimer(hwnd, 2, 90000, nullptr);
			return 0;
		}
		if (message == WM_TIMER && state)
		{
			if (wParam == 2) { DestroyWindow(hwnd); return 0; }
			if (InterlockedCompareExchange(&state->requested, 0, 0) !=
				InterlockedCompareExchange(&state->painted, 0, 0))
				InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}
		if (message == WM_PAINT && state)
		{
			PAINTSTRUCT paint{};
			const HDC dc = BeginPaint(hwnd, &paint);
			RECT client{};
			GetClientRect(hwnd, &client);
			const LONG phase = InterlockedCompareExchange(&state->requested, 0, 0);
			for (int y = 0; y < client.bottom; y += 32)
				for (int x = 0; x < client.right; x += 32)
				{
					const auto brush = CreateSolidBrush(((x / 32 + y / 32) % 2) ? RGB(30, 30, 30) : RGB(235, 235, 235));
					const RECT cell{ x, y, x + 32, y + 32 };
					FillRect(dc, &cell, brush);
					DeleteObject(brush);
				}
			const auto marker = CreateSolidBrush(PatternColor(phase));
			const RECT markerRect{ 32, 32, 144, 144 };
			FillRect(dc, &markerRect, marker);
			DeleteObject(marker);
			EndPaint(hwnd, &paint);
			GdiFlush();
			InterlockedExchange(&state->painted, phase);
			return 0;
		}
		if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	template<class Predicate>
	bool PumpUntil(Predicate predicate, DWORD timeout)
	{
		const auto deadline = GetTickCount64() + timeout;
		do
		{
			MSG message{};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			if (predicate()) return true;
			Sleep(10);
		} while (GetTickCount64() < deadline);
		return predicate();
	}

	struct Fixture
	{
		HANDLE mapping = nullptr, process = nullptr, job = nullptr;
		PatternState* state = nullptr;
		HWND overlay = nullptr;
		HBRUSH overlayBrush = nullptr;
		~Fixture()
		{
			if (overlay) DestroyWindow(overlay);
			if (overlayBrush) DeleteObject(overlayBrush);
			UnregisterClassW(L"Inkeys.ScrollFollow.ExcludedProbe", GetModuleHandleW(nullptr));
			if (job) CloseHandle(job); // 子进程只拥有专用测试图案，失败路径同样自动关闭。
			if (process)
			{
				if (WaitForSingleObject(process, 3000) == WAIT_TIMEOUT) TerminateProcess(process, 3);
				CloseHandle(process);
			}
			if (state) UnmapViewOfFile(state);
			if (mapping) CloseHandle(mapping);
		}
	};

	struct CaptureEvidence
	{
		std::mutex mutex;
		std::uint64_t callbacks = 0, previousSequence = 0, matching = 0;
		std::int64_t previousQpc = 0;
		std::uint32_t marker = 0;
		LONG expected = 1;
		bool metadata = true;
		std::vector<double> timings;
	};

	bool MarkerMatches(std::uint32_t pixel, COLORREF expected) noexcept
	{
		return std::abs(static_cast<int>((pixel >> 16) & 255) - GetRValue(expected)) <= 3 &&
			std::abs(static_cast<int>((pixel >> 8) & 255) - GetGValue(expected)) <= 3 &&
			std::abs(static_cast<int>(pixel & 255) - GetBValue(expected)) <= 3;
	}

	void PrintDesktopName(const char* label, HANDLE object)
	{
		wchar_t name[256]{};
		DWORD needed = 0;
		const bool read = object && GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), &needed);
		std::cout << "[ScrollFollow][Context] " << label << "=";
		if (read)
			for (const auto character : std::wstring_view(name))
				std::cout << (character < 128 ? static_cast<char>(character) : '?');
		else std::cout << "unavailable error=" << GetLastError();
		std::cout << '\n';
	}

	void PrintProcessContext()
	{
		PrintDesktopName("station", GetProcessWindowStation());
		PrintDesktopName("thread_desktop", GetThreadDesktop(GetCurrentThreadId()));
		const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
		PrintDesktopName("input_desktop", input);
		if (input) CloseDesktop(input);
		SYSTEM_INFO system{};
		GetNativeSystemInfo(&system);
		BOOL wow64 = FALSE;
		IsWow64Process(GetCurrentProcess(), &wow64);
		DWORD session = 0;
		ProcessIdToSessionId(GetCurrentProcessId(), &session);
		std::cout << "[ScrollFollow][Context] native_arch=" << system.wProcessorArchitecture
			<< " wow64=" << wow64 << " session=" << session << " process=" << GetCurrentProcessId() << '\n';
	}

	BOOL CALLBACK CollectProbeWindows(HWND hwnd, LPARAM parameter) noexcept
	{
		DWORD process = 0;
		GetWindowThreadProcessId(hwnd, &process);
		if (process == GetCurrentProcessId())
		{
			try { reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(hwnd); }
			catch (...) { return FALSE; }
		}
		return TRUE;
	}

	void ProbeRawMagCapabilities(const RECT& source, const RECT& virtualBounds, Fixture& fixture)
	{
		// 失败调查只在显式 PoC 中运行；生产路径始终拒绝未生效的排除列表。
		const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
		using CompositionFn = HRESULT(WINAPI*)(BOOL*);
		const auto compositionFn = reinterpret_cast<CompositionFn>(
			dwm ? GetProcAddress(dwm, "DwmIsCompositionEnabled") : nullptr);
		BOOL composition = FALSE;
		const auto compositionResult = compositionFn ? compositionFn(&composition) : E_NOTIMPL;
		DWORD session = 0;
		ProcessIdToSessionId(GetCurrentProcessId(), &session);
		std::cout << "[ScrollFollow][Probe] composition_hr=" << compositionResult
			<< " enabled=" << composition << " remote=" << GetSystemMetrics(SM_REMOTESESSION)
			<< " session=" << session << '\n';
		if (dwm) FreeLibrary(dwm);
		MagnificationRuntimeLease lease;
		if (!lease.Acquire()) return;
		const HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass{ sizeof(windowClass) };
		windowClass.hInstance = instance;
		windowClass.lpfnWndProc = &DefWindowProcW;
		windowClass.lpszClassName = L"Inkeys.ScrollFollow.RawCapabilityProbe";
		if (!RegisterClassExW(&windowClass)) return;
		const int width = source.right - source.left, height = source.bottom - source.top;
		const HWND host = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
			WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"Mag capability probe", WS_POPUP,
			source.left, source.top, width, height, nullptr, nullptr, instance, nullptr);
		if (!host) { UnregisterClassW(windowClass.lpszClassName, instance); return; }
		SetLayeredWindowAttributes(host, 0, 0, LWA_ALPHA);
		const HWND child = CreateWindowExW(WS_EX_NOACTIVATE, WC_MAGNIFIER, L"", WS_CHILD | WS_VISIBLE,
			0, 0, width, height, host, nullptr, instance, nullptr);
		if (child)
		{
			MAGTRANSFORM identity{};
			identity.v[0][0] = identity.v[1][1] = identity.v[2][2] = 1;
			SetLastError(0);
			const BOOL transformed = lease.Api()->setTransform(child, &identity);
			std::cout << "[ScrollFollow][Probe] transform=" << transformed << " error=" << GetLastError() << '\n';
			const auto testFilter = [&](const char* name, HWND* handles, int count)
			{
				SetLastError(0);
				const BOOL result = lease.Api()->setFilter(child, MW_FILTERMODE_EXCLUDE, count, handles);
				std::cout << "[ScrollFollow][Probe] filter=" << name << " ok=" << result << " error=" << GetLastError() << '\n';
			};
			testFilter("empty", nullptr, 0);
			HWND one = fixture.overlay;
			testFilter("overlay-only", &one, 1);
			one = host;
			testFilter("host-only", &one, 1);
			HWND all[] = { host, fixture.overlay };
			testFilter("host-and-overlay", all, 2);
			std::vector<HWND> ownWindows;
			EnumWindows(&CollectProbeWindows, reinterpret_cast<LPARAM>(&ownWindows));
			for (auto candidate : ownWindows)
			{
				char className[256]{};
				GetClassNameA(candidate, className, static_cast<int>(std::size(className)));
				std::cout << "[ScrollFollow][Probe] candidate=" << className << " visible=" << IsWindowVisible(candidate)
					<< " style=0x" << std::hex << GetWindowLongPtrW(candidate, GWL_STYLE) << std::dec << '\n';
				testFilter(className, &candidate, 1);
			}
			testFilter("all-own-windows", ownWindows.data(), static_cast<int>(ownWindows.size()));
			testFilter("host-and-overlay-restored", all, 2);
			DibSurface image(width, height);
			if (image.empty())
			{
				DestroyWindow(child);
				DestroyWindow(host);
				UnregisterClassW(windowClass.lpszClassName, instance);
				return;
			}
			const char* modes[] = { "alpha0-topmost", "offscreen-alpha255", "hidden" };
			for (int mode = 0; mode < 3; ++mode)
			{
				if (mode == 0)
					SetWindowPos(host, HWND_TOPMOST, source.left, source.top, width, height,
						SWP_NOACTIVATE | SWP_SHOWWINDOW);
				else if (mode == 1)
				{
					SetWindowPos(host, HWND_TOPMOST, virtualBounds.right + 64, virtualBounds.top,
						width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
					SetLayeredWindowAttributes(host, 0, 255, LWA_ALPHA);
				}
				else ShowWindow(host, SW_HIDE);
				for (LONG phase = 10; phase < 13; ++phase)
				{
					InterlockedExchange(&fixture.state->requested, phase);
					PumpUntil([&] { return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == phase; }, 500);
					SetLastError(0);
					const BOOL sourced = lease.Api()->setSource(child, source);
					const auto sourceError = GetLastError();
					const BOOL redrawn = RedrawWindow(child, nullptr, nullptr,
						RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
					Sleep(60);
					image.clear();
					SetLastError(0);
					const BOOL printed = PrintWindow(child, image.dc(), 2);
					const auto printError = GetLastError();
					GdiFlush();
					const auto quality = AnalyzeMagFrame({ reinterpret_cast<const std::uint8_t*>(image.pixels().data()),
						image.pixels().size_bytes() }, width, height, static_cast<std::size_t>(width) * 4);
					const int x = fixture.state->rect.left + 64 - source.left;
					const int y = fixture.state->rect.top + 64 - source.top;
					const auto marker = image.pixels()[static_cast<std::size_t>(y) * width + x];
					std::cout << "[ScrollFollow][Probe] mode=" << modes[mode] << " phase=" << phase
						<< " source=" << sourced << " source_error=" << sourceError << " redraw=" << redrawn
						<< " print=" << printed << " print_error=" << printError << " mean=" << quality.mean
						<< " variance=" << quality.variance << " black=" << quality.nearBlackRatio
						<< " marker=0x" << std::hex << marker << std::dec << " source_match="
						<< MarkerMatches(marker, PatternColor(phase)) << '\n';
				}
			}
			DestroyWindow(child);
		}
		DestroyWindow(host);
		UnregisterClassW(windowClass.lpszClassName, instance);
	}

	struct NativeMagApi
	{
		decltype(&MagInitialize) initialize = nullptr;
		decltype(&MagUninitialize) uninitialize = nullptr;
		MagnificationApi control;
		std::atomic<unsigned> initialized{ 0 }, uninitialized{ 0 };
	};

	LRESULT CALLBACK NativeProbeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		// 与已通过冷启动的服务一致，避免普通父窗擦背景干扰 Mag 的读回条件。
		if (message == WM_ERASEBKGND) return 1;
		if (message == WM_NCHITTEST) return HTTRANSPARENT;
		if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	void PrintNativeResources(bool globalFinalOnly, unsigned round)
	{
		DWORD handles = 0;
		GetProcessHandleCount(GetCurrentProcess(), &handles);
		PROCESS_MEMORY_COUNTERS_EX memory{ sizeof(memory) };
		using MemoryFn = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
		const auto memoryFn = reinterpret_cast<MemoryFn>(GetProcAddress(
			GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
		const bool memoryRead = memoryFn && memoryFn(GetCurrentProcess(),
			reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory), sizeof(memory));
		std::cout << "[ScrollFollow][NativeResources] final_only=" << globalFinalOnly << " round=" << round
			<< " gdi=" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)
			<< " user=" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) << " handles=" << handles
			<< " private_bytes=" << memory.PrivateUsage << " memory_read=" << memoryRead << '\n';
	}

	class NativeMagControl
	{
	public:
		NativeMagControl(NativeMagApi& api, RECT source, RECT pattern, bool freezeWindow = false)
			: api_(api), source_(source), pattern_(pattern), freezeWindow_(freezeWindow) {}
		~NativeMagControl() { (void)Stop(); }
		bool Start(bool initialize = true, bool show = true)
		{
			if (initialize)
			{
				if (!api_.initialize()) return false;
				initialized_ = true;
				++api_.initialized;
			}
			static std::atomic<unsigned> nextName{ 0 };
			name_ = L"Inkeys.ScrollFollow.NativeThreadProbe." + std::to_wstring(GetCurrentThreadId()) +
				L"." + std::to_wstring(++nextName);
			WNDCLASSEXW windowClass{ sizeof(windowClass) };
			windowClass.hInstance = GetModuleHandleW(nullptr);
			windowClass.lpfnWndProc = freezeWindow_ ? &DefWindowProcW : &NativeProbeProc;
			windowClass.style = freezeWindow_ ? CS_HREDRAW | CS_VREDRAW : 0;
			windowClass.lpszClassName = name_.c_str();
			registered_ = RegisterClassExW(&windowClass) != 0;
			if (!registered_) return false;
			const int width = source_.right - source_.left;
			const int height = source_.bottom - source_.top - (freezeWindow_ ? 1 : 0);
			const DWORD hostExStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
				(freezeWindow_ ? 0 : WS_EX_TRANSPARENT);
			host_ = CreateWindowExW(hostExStyle,
				name_.c_str(), L"Mag lifetime probe", WS_POPUP | (freezeWindow_ ? WS_CLIPCHILDREN : 0),
				source_.left, source_.top, width, height,
				nullptr, nullptr, windowClass.hInstance, nullptr);
			if (!host_ || !SetLayeredWindowAttributes(host_, 0, 0, LWA_ALPHA)) return false;
			child_ = CreateWindowExW(WS_EX_NOACTIVATE, WC_MAGNIFIER, L"",
				WS_CHILD | WS_VISIBLE | (freezeWindow_ ? MS_CLIPAROUNDCURSOR : 0),
				0, 0, width, height, host_, nullptr, windowClass.hInstance, nullptr);
			MAGTRANSFORM transform{};
			transform.v[0][0] = transform.v[1][1] = transform.v[2][2] = 1;
			if (!child_ || !api_.control.setTransform(child_, &transform) || !image_.resize(width, height)) return false;
			if (show) ShowWindow(host_, SW_SHOWNOACTIVATE);
			visible_ = show;
			return true;
		}
		bool PrepareFreezeColorEffect()
		{
			// 对齐产品 ChildCreated 的 identity 色矩阵，不扩充生产函数表。
			const auto setColorEffect = reinterpret_cast<decltype(&MagSetColorEffect)>(GetProcAddress(
				GetModuleHandleW(L"Magnification.dll"), "MagSetColorEffect"));
			MAGCOLOREFFECT identity{};
			for (int index = 0; index < 5; ++index) identity.transform[index][index] = 1;
			return setColorEffect && setColorEffect(child_, &identity);
		}
		bool Sample(LONG expected)
		{
			Pump();
			std::vector<HWND> filter;
			lastStage = "filter";
			if (!EnumWindows(&CollectProbeWindows, reinterpret_cast<LPARAM>(&filter)) ||
				!api_.control.setFilter(child_, MW_FILTERMODE_EXCLUDE, static_cast<int>(filter.size()), filter.data()))
			{ lastError = GetLastError(); return false; }
			lastStage = "source";
			if (!api_.control.setSource(child_, source_)) { lastError = GetLastError(); return false; }
			lastStage = "redraw";
			if (!RedrawWindow(child_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN))
			{ lastError = GetLastError(); return false; }
			image_.clear();
			lastStage = "print";
			if (!PrintWindow(child_, image_.dc(), 2)) { lastError = GetLastError(); return false; }
			GdiFlush();
			const int x = pattern_.left + 64 - source_.left, y = pattern_.top + 64 - source_.top;
			lastPixel = image_.pixels()[static_cast<std::size_t>(y) * image_.width() + x];
			lastQuality = AnalyzeMagFrame({ reinterpret_cast<const std::uint8_t*>(image_.pixels().data()),
				image_.pixels().size_bytes() }, image_.width(), image_.height(), static_cast<std::size_t>(image_.width()) * 4);
			lastStage = "pixels";
			lastError = 0;
			return MarkerMatches(lastPixel, PatternColor(expected));
		}
		void Show(bool visible)
		{
			if (visible != visible_)
			{
				ShowWindow(host_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
				visible_ = visible;
			}
		}
		void Pump()
		{
			MSG message{};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		bool Stop(bool uninitialize = true)
		{
			bool cleaned = true;
			if (child_) { cleaned &= DestroyWindow(child_) != FALSE; child_ = nullptr; }
			if (host_) { cleaned &= DestroyWindow(host_) != FALSE; host_ = nullptr; }
			image_.reset();
			if (registered_) { cleaned &= UnregisterClassW(name_.c_str(), GetModuleHandleW(nullptr)) != FALSE; registered_ = false; }
			if (initialized_)
			{
				if (uninitialize)
				{
					cleaned &= api_.uninitialize() != FALSE;
					++api_.uninitialized;
				}
				initialized_ = false;
			}
			return cleaned;
		}
		const char* lastStage = "not-sampled";
		DWORD lastError = 0;
		std::uint32_t lastPixel = 0;
		MagFrameQuality lastQuality;
	private:
		NativeMagApi& api_;
		RECT source_{}, pattern_{};
		std::wstring name_;
		HWND host_ = nullptr, child_ = nullptr;
		DibSurface image_;
		bool initialized_ = false, registered_ = false, visible_ = true, freezeWindow_ = false;
	};

	int ProbeThreadRuntimeLifetime(const RECT& source, Fixture& fixture, bool globalFinalOnly)
	{
		// 仅实验原生 API 的线程语义；不经过生产 lease，也不改变生产运行库策略。
		StaScope mainCom;
		std::cout << "[ScrollFollow][ThreadContext] final_only=" << globalFinalOnly
			<< " main_com=" << mainCom.result << '\n';
		if (FAILED(mainCom.result)) return 1;
		const HMODULE module = LoadLibraryW(L"Magnification.dll");
		if (!module) return 1;
		NativeMagApi api;
		api.initialize = reinterpret_cast<decltype(api.initialize)>(GetProcAddress(module, "MagInitialize"));
		api.uninitialize = reinterpret_cast<decltype(api.uninitialize)>(GetProcAddress(module, "MagUninitialize"));
		api.control.setSource = reinterpret_cast<decltype(api.control.setSource)>(GetProcAddress(module, "MagSetWindowSource"));
		api.control.setFilter = reinterpret_cast<decltype(api.control.setFilter)>(GetProcAddress(module, "MagSetWindowFilterList"));
		api.control.setTransform = reinterpret_cast<decltype(api.control.setTransform)>(GetProcAddress(module, "MagSetWindowTransform"));
		if (!api.initialize || !api.uninitialize || !api.control.setSource || !api.control.setFilter || !api.control.setTransform)
		{ FreeLibrary(module); return 1; }
		int failures = 0;
		const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		for (unsigned round = 0; round < 4; ++round)
		{
			const bool mainFirst = round % 2 == 0;
			NativeMagControl mainControl(api, source, fixture.state->rect);
			bool mainStarted = !mainFirst || mainControl.Start();
			if (mainFirst && mainStarted) mainControl.Show(false);
			std::atomic<bool> workerReady{ false }, workerStarted{ false }, stopWorker{ false }, workerCleanup{ false };
			std::atomic<bool> workerVisible{ mainFirst };
			std::atomic<const char*> workerStage{ "not-sampled" };
			std::atomic<DWORD> workerError{ 0 };
			std::atomic<std::uint32_t> workerPixel{ 0 };
			std::atomic<LONG> expected{ 20 + static_cast<LONG>(round) * 2 }, workerMatched{ -1 };
			std::thread worker([&]
			{
				const HRESULT workerCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				NativeMagControl control(api, source, fixture.state->rect);
				workerStarted = SUCCEEDED(workerCom) && control.Start();
				if (workerStarted) control.Show(workerVisible);
				workerReady = true;
				while (workerStarted && !stopWorker)
				{
					const LONG phase = expected.load();
					control.Show(workerVisible);
					if (workerVisible)
					{
						if (control.Sample(phase)) workerMatched = phase;
						workerStage = control.lastStage;
						workerError = control.lastError;
						workerPixel = control.lastPixel;
					}
					else control.Pump();
					Sleep(10);
				}
				workerCleanup = control.Stop(!(globalFinalOnly && mainFirst));
				if (SUCCEEDED(workerCom)) CoUninitialize();
			});
			const bool ready = PumpUntil([&] { return workerReady.load(); }, 3000);
			if (!mainFirst) mainStarted = mainControl.Start();
			InterlockedExchange(&fixture.state->requested, expected.load());
			const bool sourcePainted = PumpUntil([&]
			{ return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == expected.load(); }, 1000);
			const bool activePixels = ready && mainStarted && workerStarted && PumpUntil([&]
			{
				return mainFirst ? workerMatched == expected.load() : mainControl.Sample(expected.load());
			}, 2500);
			std::cout << "[ScrollFollow][ThreadStages] final_only=" << globalFinalOnly << " round=" << round
				<< " source_painted=" << sourcePainted << " main_stage=" << mainControl.lastStage
				<< " main_error=" << mainControl.lastError << " worker_stage=" << workerStage.load()
				<< " worker_error=" << workerError << " main_pixel=0x" << std::hex << mainControl.lastPixel
				<< " worker_pixel=0x" << workerPixel << std::dec << '\n';
			bool remainingPixels = false, mainCleanup = true;
			if (mainFirst)
			{
				stopWorker = true;
				worker.join();
				mainControl.Show(true);
				++expected;
				InterlockedExchange(&fixture.state->requested, expected.load());
				remainingPixels = mainStarted && PumpUntil([&] { return mainControl.Sample(expected.load()); }, 2000);
				mainCleanup = mainControl.Stop();
			}
			else
			{
				mainCleanup = mainControl.Stop(!globalFinalOnly);
				workerVisible = true;
				++expected;
				InterlockedExchange(&fixture.state->requested, expected.load());
				remainingPixels = PumpUntil([&] { return workerMatched == expected.load(); }, 2000);
				stopWorker = true;
				worker.join();
			}
			bool residuePixels = false;
			if (globalFinalOnly)
			{
				NativeMagControl residue(api, source, fixture.state->rect);
				const bool bareStarted = residue.Start(false);
				const LONG phase = expected.load();
				residuePixels = bareStarted && PumpUntil([&] { return residue.Sample(phase); }, 500);
				(void)residue.Stop();
				if (residuePixels) { if (api.uninitialize()) ++api.uninitialized; }
			}
			const bool passed = sourcePainted && activePixels && remainingPixels && mainCleanup && workerCleanup && !residuePixels;
			failures += !passed;
			std::cout << "[ScrollFollow][ThreadLifetime] final_only=" << globalFinalOnly
				<< " round=" << round << " first=" << (mainFirst ? "main" : "worker")
				<< " initial_active_pixels=" << activePixels << " remaining_new_pixels=" << remainingPixels
				<< " main_cleanup=" << mainCleanup << " worker_cleanup=" << workerCleanup
				<< " residual_owner_pixels=" << residuePixels << " passed=" << passed << '\n';
			PrintNativeResources(globalFinalOnly, round);
		}
		const auto gdiAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		const bool balanced = api.initialized == 8 && api.uninitialized == (globalFinalOnly ? 4u : 8u) &&
			gdiAfter <= gdiBefore + 2;
		failures += !balanced;
		std::cout << "[ScrollFollow][ThreadLifetime] final_only=" << globalFinalOnly
			<< " init=" << api.initialized << " uninit=" << api.uninitialized
			<< " gdi_before=" << gdiBefore << " gdi_after=" << gdiAfter << " balanced=" << balanced << '\n';
		if (globalFinalOnly)
		{
			for (LONG phase = 40; phase < 43; ++phase)
			{
				NativeMagControl control(api, source, fixture.state->rect);
				const bool started = control.Start();
				const bool repeated = api.initialize() != FALSE;
				if (repeated) ++api.initialized;
				InterlockedExchange(&fixture.state->requested, phase);
				const bool pixels = started && repeated && PumpUntil([&] { return control.Sample(phase); }, 1500);
				const bool stopped = control.Stop();
				NativeMagControl bare(api, source, fixture.state->rect);
				const bool withoutInit = bare.Start(false);
				const bool stillActive = withoutInit && PumpUntil([&] { return bare.Sample(phase); }, 500);
				(void)bare.Stop();
				// 若仍然有效，就真实存在额外原生引用，不能把它留给后续实验。
				if (stillActive) { if (api.uninitialize()) ++api.uninitialized; }
				const bool passed = pixels && stopped && !stillActive;
				failures += !passed;
				std::cout << "[ScrollFollow][RepeatedInitialize] phase=" << phase << " pixels=" << pixels
					<< " one_uninit=" << stopped << " bare_start=" << withoutInit
					<< " still_active=" << stillActive << " passed=" << passed << '\n';
			}
		}
		FreeLibrary(module);
		std::cout << "[ScrollFollow][ThreadLifetime] final_only=" << globalFinalOnly
			<< " module_still_loaded=" << (GetModuleHandleW(L"Magnification.dll") != nullptr) << '\n';
		return failures;
	}

	int ProbeSharedMagOwner(const RECT& source, Fixture& fixture)
	{
		StaScope mainCom;
		std::cout << "[ScrollFollow][SharedOwnerContext] main_com=" << mainCom.result << '\n';
		if (FAILED(mainCom.result)) return 1;
		const HMODULE module = LoadLibraryW(L"Magnification.dll");
		if (!module) return 1;
		NativeMagApi api;
		api.initialize = reinterpret_cast<decltype(api.initialize)>(GetProcAddress(module, "MagInitialize"));
		api.uninitialize = reinterpret_cast<decltype(api.uninitialize)>(GetProcAddress(module, "MagUninitialize"));
		api.control.setSource = reinterpret_cast<decltype(api.control.setSource)>(GetProcAddress(module, "MagSetWindowSource"));
		api.control.setFilter = reinterpret_cast<decltype(api.control.setFilter)>(GetProcAddress(module, "MagSetWindowFilterList"));
		api.control.setTransform = reinterpret_cast<decltype(api.control.setTransform)>(GetProcAddress(module, "MagSetWindowTransform"));
		if (!api.initialize || !api.uninitialize || !api.control.setSource || !api.control.setFilter || !api.control.setTransform)
		{ FreeLibrary(module); return 1; }
		int failures = 0;
		{
			NativeMagControl single(api, source, fixture.state->rect);
			const bool started = single.Start();
			InterlockedExchange(&fixture.state->requested, 58);
			const bool painted = PumpUntil([&] { return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == 58; }, 1000);
			const bool pixels = started && painted && PumpUntil([&] { return single.Sample(58); }, 2000);
			std::cout << "[ScrollFollow][SharedOwnerPreflight] started=" << started << " painted=" << painted
				<< " pixels=" << pixels << " stage=" << single.lastStage << " error=" << single.lastError
				<< " pixel=0x" << std::hex << single.lastPixel << std::dec << " mean=" << single.lastQuality.mean
				<< " variance=" << single.lastQuality.variance << " quality=" << MagCaptureReasonText(single.lastQuality.reason) << '\n';
			if (!single.Stop() || !pixels) ++failures;
		}
		if (failures)
		{
			FreeLibrary(module);
			return failures;
		}
		for (LONG round = 0; round < 3; ++round)
		{
			NativeMagControl first(api, source, fixture.state->rect), second(api, source, fixture.state->rect);
			const bool firstStarted = first.Start();
			first.Show(false);
			const bool secondStarted = second.Start(false);
			const LONG phase = 60 + round * 2;
			InterlockedExchange(&fixture.state->requested, phase);
			const bool painted = PumpUntil([&] { return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == phase; }, 1000);
			const bool secondPixels = firstStarted && secondStarted && painted &&
				PumpUntil([&] { return second.Sample(phase); }, 2000);
			const char* secondStage = second.lastStage;
			const DWORD secondError = second.lastError;
			const bool secondStopped = second.Stop();
			first.Show(true);
			InterlockedExchange(&fixture.state->requested, phase + 1);
			const bool nextPainted = PumpUntil([&] { return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == phase + 1; }, 1000);
			const bool firstPixels = firstStarted && nextPainted && PumpUntil([&] { return first.Sample(phase + 1); }, 2000);
			const bool firstStopped = first.Stop();
			const bool passed = secondPixels && firstPixels && firstStopped && secondStopped;
			failures += !passed;
			std::cout << "[ScrollFollow][SharedOwner] round=" << round << " source_painted=" << painted
				<< " second_pixels=" << secondPixels << " second_stage=" << secondStage << " second_error=" << secondError
				<< " second_pixel=0x" << std::hex << second.lastPixel << std::dec
				<< " second_mean=" << second.lastQuality.mean << " second_variance=" << second.lastQuality.variance
				<< " first_pixel=0x" << std::hex << first.lastPixel << std::dec
				<< " surviving_first_pixels=" << firstPixels << " cleanup=" << (firstStopped && secondStopped)
				<< " passed=" << passed << '\n';
			PrintNativeResources(false, static_cast<unsigned>(round));
		}
		std::cout << "[ScrollFollow][SharedOwner] init=" << api.initialized << " uninit=" << api.uninitialized << '\n';
		FreeLibrary(module);
		return failures;
	}

	int ProbeCaptureLifecycle()
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* name)
		{
			std::cout << (condition ? "PASS" : "FAILED") << " mag lifecycle: " << name << '\n';
			failures += !condition;
			return condition;
		};
		MagCaptureService capture;
		WNDCLASSEXW collision{ sizeof(collision) };
		collision.hInstance = GetModuleHandleW(nullptr);
		collision.lpfnWndProc = &NativeProbeProc;
		collision.lpszClassName = L"Inkeys.ScrollFollow.MagCaptureHost";
		const bool registered = RegisterClassExW(&collision) != 0;
		check(registered, "deterministic window-class initialization failure armed");
		auto callbacks = std::make_shared<std::atomic<unsigned>>(0);
		check(capture.Start({ 100, 20, false }, [callbacks](const MagCaptureFrame&) { ++*callbacks; }),
			"asynchronous initialization accepted");
		check(PumpUntil([&] { return capture.Snapshot().resourcesReleased; }, 6500), "failed initialization fully cleaned");
		auto snapshot = capture.Snapshot();
		check(snapshot.state == MagCaptureState::Failed && snapshot.reason == MagCaptureReason::WindowClass &&
			!snapshot.publicationOpen && !snapshot.host && !snapshot.child && callbacks->load() == 0,
			"failed initialization never publishes pixels or leaves windows");
		if (registered) UnregisterClassW(collision.lpszClassName, collision.hInstance);
		check(capture.Start({ 101, 20, false }, [](const MagCaptureFrame&) { throw 1; }), "restart after initialization failure");
		check(PumpUntil([&] { return capture.Snapshot().resourcesReleased; }, 6500), "throwing callback fully cleaned");
		snapshot = capture.Snapshot();
		check(snapshot.state == MagCaptureState::Failed && snapshot.reason == MagCaptureReason::CallbackFailure &&
			snapshot.cleanupSucceeded && !snapshot.publicationOpen, "callback exception closes publication");
		struct Stall
		{
			HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			std::atomic<bool> entered{ false };
			std::atomic<unsigned> callbacks{ 0 };
			~Stall() { if (release) CloseHandle(release); }
		};
		auto stall = std::make_shared<Stall>();
		if (!check(stall->release != nullptr, "controlled callback stall event")) return failures;
		check(capture.Start({ 102, 20, false }, [stall](const MagCaptureFrame&)
		{
			++stall->callbacks;
			stall->entered = true;
			WaitForSingleObject(stall->release, 5000);
		}), "stalled callback session starts");
		check(PumpUntil([&] { return stall->entered.load(); }, 6500), "callback actually entered before timeout");
		check(PumpUntil([&] { return capture.Snapshot().reason == MagCaptureReason::ResultTimeout; }, 2000),
			"one-second result watchdog closes stalled session");
		snapshot = capture.Snapshot();
		check(!snapshot.publicationOpen && !snapshot.resourcesReleased && snapshot.host && snapshot.child &&
			GetMagnificationRuntimeSnapshot().leases == 1, "pending owner retains HWND and runtime lease");
		const auto stopStarted = GetTickCount64();
		capture.Stop();
		const auto stopMilliseconds = GetTickCount64() - stopStarted;
		check(stopMilliseconds < 250, "Stop returns without waiting on blocked owner");
		check(!capture.Start({ 103, 20, false }, [](const MagCaptureFrame&) {}) && GetLastError() == ERROR_BUSY,
			"restart rejected until real owner exit");
		SetEvent(stall->release);
		check(PumpUntil([&] { return capture.Snapshot().resourcesReleased; }, 6500), "blocked owner cleans after release");
		check(stall->callbacks == 1 && capture.Snapshot().cleanupSucceeded, "no late callbacks after timeout");
		callbacks->store(0);
		check(capture.Start({ 104, 20, false }, [callbacks](const MagCaptureFrame& frame)
		{ if (frame.quality.Valid()) ++*callbacks; }), "new session after fault cleanup");
		check(PumpUntil([&] { return callbacks->load() != 0; }, 6500), "restarted session receives real valid pixels");
		capture.Stop();
		check(PumpUntil([&] { return capture.Snapshot().resourcesReleased; }, 6500), "final session released");
		const auto runtime = GetMagnificationRuntimeSnapshot();
		check(runtime.leases == 0 && runtime.initializeCount == runtime.uninitializeCount && !runtime.cleanupFailed,
			"lifecycle runtime balanced");
		std::cout << "[ScrollFollow][Lifecycle] stop_ms=" << stopMilliseconds << " init=" << runtime.initializeCount
			<< " uninit=" << runtime.uninitializeCount << " failures=" << failures << '\n';
		return failures;
	}
}

int RunMagCaptureTests()
{
	int failures = 0;
	const auto check = [&](bool condition, const char* name)
	{
		if (!condition) { std::cerr << "FAILED mag capture: " << name << '\n'; ++failures; }
	};
	std::vector<std::uint8_t> pixels(64 * 64 * 4, 0);
	check(AnalyzeMagFrame(pixels, 64, 64, 256).reason == MagCaptureReason::NearBlack, "black reason");
	std::fill(pixels.begin(), pixels.end(), static_cast<std::uint8_t>(240));
	check(AnalyzeMagFrame(pixels, 64, 64, 256).reason == MagCaptureReason::LowInformation, "pure white rejected");
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
		{
			const auto value = static_cast<std::uint8_t>(((x / 4 + y / 4) % 2) ? 32 : 0);
			for (int channel = 0; channel < 3; ++channel)
				pixels[static_cast<std::size_t>((y * 64 + x) * 4 + channel)] = value;
		}
	const auto dark = AnalyzeMagFrame(pixels, 64, 64, 256);
	check(dark.Valid() && dark.mean < 20 && dark.variance > 4, "informative dark page accepted");
	check(AnalyzeMagFrame(pixels, 64, 64, 256).Valid(), "identical static frame remains valid");
	check(!AnalyzeMagFrame(pixels, 0, 64, 256).Valid(), "zero dimension");
	check(!AnalyzeMagFrame(pixels, 64, 64, 255).Valid(), "short stride");
	check(!AnalyzeMagFrame(std::span<const std::uint8_t>(pixels).first(3), 64, 64, 256).Valid(), "short buffer");
	check(!AnalyzeMagFrame(pixels, 64, 64, (std::numeric_limits<std::size_t>::max)()).Valid(), "overflow stride");
	check(!MagCaptureResultTimedOut(1000, 0) && MagCaptureResultTimedOut(1001, 0), "one second watchdog boundary");
	check(!MagCaptureResultTimedOut(5000, 0, true) && MagCaptureResultTimedOut(5001, 0, true),
		"independent bounded cold-start grace");
	check(!MagCaptureResultTimedOut(3, 4), "nonmonotonic timestamp protected");
	MagCaptureService service;
	check(service.Snapshot().state == MagCaptureState::Off, "initially off");
	check(!service.Start({}, [](const MagCaptureFrame&) {}), "invalid start rejected before HWND");
	check(service.Snapshot().reason == MagCaptureReason::InvalidOptions &&
		service.Snapshot().resourcesReleased, "failed start needs no owner cleanup");
	service.Stop();
	service.Stop();
	check(!service.Snapshot().publicationOpen, "idempotent stop closes publication");
	if (!failures) std::cout << "PASS mag capture no-window validity and state\n";
	return failures;
}

int RunMagCapturePatternChild(const char* mappingName)
{
	SetProcessDPIAware();
	const std::string name(mappingName);
	const std::wstring wideName(name.begin(), name.end());
	const HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wideName.c_str());
	if (!mapping) return 2;
	auto* state = static_cast<PatternState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PatternState)));
	if (!state) { CloseHandle(mapping); return 2; }
	WNDCLASSEXW windowClass{ sizeof(windowClass) };
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpfnWndProc = &PatternProc;
	windowClass.lpszClassName = L"Inkeys.ScrollFollow.ExternalPattern";
	if (!RegisterClassExW(&windowClass)) return 2;
	const auto rect = state->rect;
	const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
		windowClass.lpszClassName, L"Scroll follow controlled capture test", WS_POPUP,
		rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, windowClass.hInstance, state);
	if (window) ShowWindow(window, SW_SHOWNOACTIVATE);
	MSG message{};
	while (window && GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	UnmapViewOfFile(state);
	CloseHandle(mapping);
	return window ? 0 : 2;
}

int RunMagCapturePoc(bool warmRuntime, int runtimeExperiment)
{
	const BOOL dpiRequested = SetProcessDPIAware();
	// 忠实模拟 Window Service：先成功建立 STA，再 Display/加载 Mag/首次 lease。
	StaScope mainCom;
	std::cout << "[ScrollFollow][Context] main_com=" << mainCom.result << " dpi_requested=" << dpiRequested << '\n';
	if (FAILED(mainCom.result)) return 1;
	namespace Diagnostic = Inkeys::Drawing::Draw3::DiagnosticOutput;
	Diagnostic::Session diagnostic;
	(void)diagnostic.Start();
	PrintProcessContext();
	int failures = 0;
	const auto check = [&](bool condition, const char* name)
	{
		std::cout << (condition ? "PASS" : "FAILED") << " mag capture PoC: " << name << '\n';
		if (!condition) ++failures;
		return condition;
	};
	if (!check(Inkeys::Display::Initialize(), "display snapshot")) return 1;
	const auto display = Inkeys::Display::GetSnapshot();
	const auto* primary = display ? display->Primary() : nullptr;
	if (!check(primary != nullptr, "primary monitor")) return 1;
	Fixture fixture;
	const auto mappingName = L"Local\\Inkeys.ScrollFollow.Poc." + std::to_wstring(GetCurrentProcessId()) +
		L"." + std::to_wstring(GetTickCount64());
	fixture.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
		0, sizeof(PatternState), mappingName.c_str());
	if (!check(fixture.mapping != nullptr, "pattern mapping")) return 1;
	fixture.state = static_cast<PatternState*>(MapViewOfFile(fixture.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PatternState)));
	if (!check(fixture.state != nullptr, "pattern shared state")) return 1;
	new (fixture.state) PatternState;
	fixture.state->rect = { primary->workArea.left + 24, primary->workArea.top + 24,
		primary->workArea.left + 744, primary->workArea.top + 504 };
	wchar_t executable[32768]{};
	GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
	std::wstring command = L"\"" + std::wstring(executable) + L"\" --mag-capture-pattern-child " + mappingName;
	fixture.job = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
	limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!check(fixture.job && SetInformationJobObject(fixture.job, JobObjectExtendedLimitInformation,
		&limit, sizeof(limit)), "self-closing fixture job")) return 1;
	STARTUPINFOW startup{ sizeof(startup) };
	PROCESS_INFORMATION process{};
	if (!check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
		CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "external pattern process")) return 1;
	fixture.process = process.hProcess;
	const bool assigned = AssignProcessToJobObject(fixture.job, process.hProcess) != FALSE;
	if (assigned) ResumeThread(process.hThread);
	else TerminateProcess(process.hProcess, 3);
	CloseHandle(process.hThread);
	if (!check(assigned, "fixture process lifetime")) return 1;
	if (!check(PumpUntil([&] { return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == 1; }, 3000),
		"real source painted")) return 1;

	fixture.overlayBrush = CreateSolidBrush(RGB(255, 0, 255));
	WNDCLASSEXW overlayClass{ sizeof(overlayClass) };
	overlayClass.hInstance = GetModuleHandleW(nullptr);
	overlayClass.lpfnWndProc = &DefWindowProcW;
	overlayClass.hbrBackground = fixture.overlayBrush;
	overlayClass.lpszClassName = L"Inkeys.ScrollFollow.ExcludedProbe";
	RegisterClassExW(&overlayClass);
	fixture.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		overlayClass.lpszClassName, L"Scroll follow excluded overlay", WS_POPUP,
		fixture.state->rect.left + 32, fixture.state->rect.top + 32, 112, 112,
		nullptr, nullptr, overlayClass.hInstance, nullptr);
	if (!check(fixture.overlay != nullptr, "same-process overlay")) return 1;
	ShowWindow(fixture.overlay, SW_SHOWNOACTIVATE);
	UpdateWindow(fixture.overlay);

	const auto runtimeBefore = GetMagnificationRuntimeSnapshot();
	// 产品启动时 Freeze 已持有运行库；PoC 同时核对捕获退出不会释放别人的 lease。
	MagnificationRuntimeLease freezeLease;
	const auto runtimeStarted = GetTickCount64();
	if (warmRuntime && !check(freezeLease.Acquire(), "shared pre-existing Freeze runtime lease")) return 1;
	std::cout << "[ScrollFollow][PoC] cold_runtime_ms=" << GetTickCount64() - runtimeStarted << '\n';
	NativeMagApi managedFreezeApi;
	std::unique_ptr<NativeMagControl> managedFreeze;
	if (runtimeExperiment == 4)
	{
		if (!check(warmRuntime && freezeLease.Api(), "managed Freeze runtime already leased")) return 1;
		managedFreezeApi.control = *freezeLease.Api();
		managedFreeze = std::make_unique<NativeMagControl>(managedFreezeApi, primary->bounds, fixture.state->rect, true);
		if (!check(managedFreeze->Start(false, false) && managedFreeze->PrepareFreezeColorEffect(),
			"fresh first-owner Freeze HWND with identity transform and color effect")) return 1;
		// fresh Freeze 尚未设置 Source/Filter，不能预采样来改变另一 owner 的启动前提。
		managedFreeze->Show(false);
		const auto ready = GetMagnificationRuntimeSnapshot();
		check(ready.leases == runtimeBefore.leases + 1 &&
			ready.initializeCount == runtimeBefore.initializeCount + 1,
			"first-owner controls use only the managed global initialization");
		std::cout << "[ScrollFollow][ManagedFreezeControl] fresh_hidden=1 init=" << ready.initializeCount
			<< " uninit=" << ready.uninitializeCount << " leases=" << ready.leases << '\n';
	}
	const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	MagCaptureService capture;
	for (unsigned cycle = 1; cycle <= (!runtimeExperiment || runtimeExperiment == 4 ? 2u : 0u); ++cycle)
	{
		auto evidence = std::make_shared<CaptureEvidence>();
		const auto callback = [evidence, rect = fixture.state->rect, source = primary->bounds,
			workArea = primary->workArea, displayGeneration = display->generation, cycle](const MagCaptureFrame& frame)
		{
			std::lock_guard lock(evidence->mutex);
			++evidence->callbacks;
			// 核对同一 Display 快照与完整缓冲合同，元数据错误时不能继续读 marker。
			evidence->metadata &= frame.sequence == evidence->previousSequence + 1 && frame.session == cycle &&
				frame.width == source.right - source.left && frame.height == source.bottom - source.top &&
				EqualRect(&frame.sourceRect, &source) && EqualRect(&frame.workArea, &workArea) &&
				frame.displayGeneration == displayGeneration && frame.qpcFrequency > 0 &&
				frame.sampledQpc > evidence->previousQpc && std::isfinite(frame.captureMilliseconds) &&
				frame.captureMilliseconds >= 0 && frame.stride == static_cast<std::size_t>(frame.width) * 4 &&
				frame.bgra.size() == frame.stride * static_cast<std::size_t>(frame.height);
			evidence->previousSequence = frame.sequence;
			evidence->previousQpc = frame.sampledQpc;
			evidence->timings.push_back(frame.captureMilliseconds);
			const int x = rect.left + 64 - frame.sourceRect.left, y = rect.top + 64 - frame.sourceRect.top;
			if (evidence->metadata && x >= 0 && x < frame.width && y >= 0 && y < frame.height && !frame.bgra.empty())
			{
				const auto* pixel = frame.bgra.data() + static_cast<std::size_t>(y) * frame.stride +
					static_cast<std::size_t>(x) * 4;
				evidence->marker = static_cast<std::uint32_t>(pixel[0]) |
					(static_cast<std::uint32_t>(pixel[1]) << 8) | (static_cast<std::uint32_t>(pixel[2]) << 16);
				if (frame.quality.Valid() && MarkerMatches(evidence->marker, PatternColor(evidence->expected)))
					++evidence->matching;
			}
		};
		if (!check(capture.Start({ cycle, 20, true }, callback), "async start accepted")) break;
		check(!capture.Start({ 100 + cycle, 20, false }, callback) && GetLastError() == ERROR_BUSY,
			"duplicate start rejected with busy error");
		PumpUntil([&]
		{
			std::lock_guard lock(evidence->mutex);
			return evidence->matching >= 4 || capture.Snapshot().state == MagCaptureState::Failed;
		}, 6500);
		bool initialPixels = false;
		{
			std::lock_guard lock(evidence->mutex);
			initialPixels = evidence->matching >= 4;
			check(initialPixels, "static real pixels and excluded overlay");
			std::cout << "[ScrollFollow][PoC] cycle=" << cycle << " marker=0x" << std::hex << evidence->marker
				<< std::dec << " matching=" << evidence->matching << " callbacks=" << evidence->callbacks << '\n';
		}
		const auto running = capture.Snapshot();
		if (running.ownerThreadId)
			PrintDesktopName("capture_owner_desktop", GetThreadDesktop(running.ownerThreadId));
		COLORREF key = 0;
		BYTE alpha = 255;
		DWORD flags = 0;
		check(running.host && GetLayeredWindowAttributes(running.host, &key, &alpha, &flags) &&
			alpha == 0 && !IsIconic(running.host) && IsWindowVisible(running.host), "host content invisible, drawable, never minimized");
		if (initialPixels)
		{
			for (LONG phase = 2; phase <= 5; ++phase)
			{
				{
					std::lock_guard lock(evidence->mutex);
					evidence->expected = phase;
					evidence->matching = 0;
				}
				InterlockedExchange(&fixture.state->requested, phase);
				const bool updated = PumpUntil([&]
				{
					std::lock_guard lock(evidence->mutex);
					return evidence->matching >= 2;
				}, 1500);
				check(updated, "invisible host reads updated external pixels");
			}
		}
		capture.Stop();
		check(PumpUntil([&] { return capture.Snapshot().resourcesReleased; }, 3000), "owner resources released");
		const auto stopped = capture.Snapshot();
		check(stopped.cleanupSucceeded && !stopped.publicationOpen && !stopped.host && !stopped.child,
			"stop closes publication and destroys windows");
		check(GetMagnificationRuntimeSnapshot().leases == runtimeBefore.leases + (warmRuntime ? 1 : 0),
			"capture stop preserves pre-existing Freeze lease");
		std::vector<double> timings;
		{
			std::lock_guard lock(evidence->mutex);
			check(evidence->metadata, "sequence timestamp and source metadata");
			timings = evidence->timings;
		}
		std::sort(timings.begin(), timings.end());
		const auto p95 = timings.empty() ? 0 : timings[(timings.size() - 1) * 95 / 100];
		std::cout << "[ScrollFollow][PoC] cycle=" << cycle << " frames=" << stopped.sequence
			<< " valid=" << stopped.validFrames << " invalid=" << stopped.invalidFrames
			<< " mean_ms=" << stopped.averageCaptureMilliseconds << " p95_ms=" << p95
			<< " max_ms=" << stopped.maximumCaptureMilliseconds << " fps=" << stopped.actualFramesPerSecond
			<< " state=" << static_cast<int>(stopped.state) << " reason=" << MagCaptureReasonText(stopped.reason) << '\n';
		if (failures)
		{
			ProbeRawMagCapabilities(primary->bounds, display->virtualBounds, fixture);
			break;
		}
		InterlockedExchange(&fixture.state->requested, 1);
		PumpUntil([&] { return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == 1; }, 1000);
		Sleep(30);
	}
	check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore + 2, "GDI resources balanced");
	if (managedFreeze)
	{
		managedFreeze->Show(true);
		InterlockedExchange(&fixture.state->requested, 90);
		const bool painted = PumpUntil([&]
		{ return InterlockedCompareExchange(&fixture.state->painted, 0, 0) == 90; }, 1000);
		const bool pixels = painted && PumpUntil([&] { return managedFreeze->Sample(90); }, 2000);
		check(pixels && managedFreeze->lastQuality.Valid(), "surviving first-owner Freeze reads new source pixels");
		std::cout << "[ScrollFollow][ManagedFreezeControl] surviving_pixels=" << pixels
			<< " stage=" << managedFreeze->lastStage << " error=" << managedFreeze->lastError
			<< " pixel=0x" << std::hex << managedFreeze->lastPixel << std::dec << '\n';
		check(managedFreeze->Stop(), "first-owner Freeze destroyed before final lease release");
		managedFreeze.reset();
	}
	check(freezeLease.Reset(), "final Freeze runtime release");
	if (runtimeExperiment == 1)
	{
		check(ProbeThreadRuntimeLifetime(primary->bounds, fixture, false) == 0, "native owner-paired runtime lifetime experiment");
		check(ProbeThreadRuntimeLifetime(primary->bounds, fixture, true) == 0, "native global-final runtime lifetime experiment");
	}
	if (runtimeExperiment == 2)
		check(ProbeSharedMagOwner(primary->bounds, fixture) == 0, "same-owner independent Mag controls");
	if (runtimeExperiment == 3)
		check(ProbeCaptureLifecycle() == 0, "capture lifecycle faults and nonblocking stop");
	const auto runtimeAfter = GetMagnificationRuntimeSnapshot();
	check(runtimeBefore.leases == runtimeAfter.leases && !runtimeAfter.cleanupFailed &&
		runtimeAfter.initializeCount == runtimeAfter.uninitializeCount, "Mag runtime balanced");
	Inkeys::Display::Shutdown();
	std::cout << (failures ? "FAILED" : "PASS") << " mag capture PoC count=" << failures << '\n';
	return failures ? 1 : 0;
}
