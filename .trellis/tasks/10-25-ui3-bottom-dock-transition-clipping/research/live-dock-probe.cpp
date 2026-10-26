// 独立诊断工具；不链接产品、不创建窗口、不启动/终止进程，也不修改应用配置。
// 在 Visual Studio ARM64 Native Tools 环境编译：
// cl /nologo /std:c++20 /EHsc /W4 /utf-8 /O2 live-dock-probe.cpp
//    /Fe:TestResults/live-dock-probe.exe /Fo:TestResults/live-dock-probe.obj
//    /link user32.lib gdi32.lib
// GetWindowRect 只记录原生外框；其反跳不能单独证明用户看见了闪烁。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr ULONG_PTR InputTag = 0x494E4B44;
constexpr double MaximumSeconds = 60.0;
constexpr double InputAckTimeoutSeconds = 0.100;
constexpr int TakeoverDistancePx = 20;
std::atomic_bool stopRequested{ false };
std::mutex inputMutex;
bool mouseHeld = false;

struct Handle
{
	HANDLE value = nullptr;
	explicit Handle(HANDLE handle) : value(handle) {}
	~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
};

long long Qpc()
{
	LARGE_INTEGER value{};
	QueryPerformanceCounter(&value);
	return value.QuadPart;
}

long long ParseInteger(const wchar_t* text, long long minimum, long long maximum)
{
	errno = 0;
	wchar_t* end = nullptr;
	const long long value = std::wcstoll(text, &end, 10);
	if (errno || end == text || *end || value < minimum || value > maximum)
		throw std::runtime_error("invalid or out-of-range integer argument");
	return value;
}

LONG Coordinate(const wchar_t* text)
{
	return static_cast<LONG>(ParseInteger(text, std::numeric_limits<LONG>::min(),
		std::numeric_limits<LONG>::max()));
}

DWORD Pid(const wchar_t* text)
{
	return static_cast<DWORD>(ParseInteger(text, 1, std::numeric_limits<DWORD>::max()));
}

bool IsBar(HWND hwnd)
{
	wchar_t className[256]{}, title[256]{};
	if (!GetClassNameW(hwnd, className, 256)) return false;
	if (std::wstring_view(className) == L"Inkeys.Window.Bar") return true;
	GetWindowTextW(hwnd, title, 256);
	// 产品入口覆盖了 Window Service 的默认类名，不能只查默认类。
	return std::wstring_view(className).starts_with(L"Inkeys5;")
		&& std::wstring_view(title) == L"Inkeys BarWindow";
}

BOOL CALLBACK CollectBars(HWND hwnd, LPARAM context)
{
	if (IsBar(hwnd)) reinterpret_cast<std::vector<HWND>*>(context)->push_back(hwnd);
	return TRUE;
}

std::vector<HWND> Bars()
{
	std::vector<HWND> windows;
	if (!EnumWindows(CollectBars, reinterpret_cast<LPARAM>(&windows)))
		throw std::runtime_error("EnumWindows failed");
	return windows;
}

HWND FindBar(DWORD processId)
{
	HWND result = nullptr;
	for (const HWND hwnd : Bars())
	{
		DWORD owner = 0;
		GetWindowThreadProcessId(hwnd, &owner);
		if (owner != processId || !IsWindowVisible(hwnd)) continue;
		if (result) throw std::runtime_error("more than one visible Bar in the explicit PID");
		result = hwnd;
	}
	if (!result) throw std::runtime_error("no visible Bar in the explicit PID");
	return result;
}

void PrintBar(HWND hwnd)
{
	DWORD processId = 0;
	const DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
	RECT rect{};
	const BOOL rectValid = GetWindowRect(hwnd, &rect);
	MONITORINFO info{ sizeof(info) };
	const BOOL monitorValid = GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
	wchar_t className[256]{}, title[256]{};
	GetClassNameW(hwnd, className, 256);
	GetWindowTextW(hwnd, title, 256);
	std::wcout << L"pid=" << processId << L" tid=" << threadId << L" hwnd=0x"
		<< std::hex << reinterpret_cast<std::uintptr_t>(hwnd) << std::dec
		<< L" visible=" << IsWindowVisible(hwnd) << L" dpi=" << GetDpiForWindow(hwnd)
		<< L" rect_valid=" << rectValid << L" rect=[" << rect.left << L',' << rect.top
		<< L',' << rect.right << L',' << rect.bottom << L"] monitor_valid=" << monitorValid
		<< L" monitor=[" << info.rcMonitor.left << L',' << info.rcMonitor.top << L','
		<< info.rcMonitor.right << L',' << info.rcMonitor.bottom << L"] work_area=["
		<< info.rcWork.left << L',' << info.rcWork.top << L',' << info.rcWork.right << L','
		<< info.rcWork.bottom << L"] class=\"" << className << L"\" title=\"" << title << L"\"\n";
}

void PrintHit(POINT point)
{
	const HWND hwnd = WindowFromPoint(point);
	DWORD processId = 0;
	if (hwnd) GetWindowThreadProcessId(hwnd, &processId);
	wchar_t className[256]{}, title[256]{};
	if (hwnd)
	{
		GetClassNameW(hwnd, className, 256);
		GetWindowTextW(hwnd, title, 256);
	}
	std::wcout << L"hit_point=[" << point.x << L',' << point.y << L"] hwnd=0x"
		<< std::hex << reinterpret_cast<std::uintptr_t>(hwnd) << std::dec
		<< L" pid=" << processId << L" class=\"" << className << L"\" title=\"" << title << L"\"\n";
}

bool IsLockScreenAt(POINT point)
{
	const HWND hit = WindowFromPoint(point);
	wchar_t className[256]{};
	if (hit) GetClassNameW(GetAncestor(hit, GA_ROOT), className, 256);
	return std::wstring_view(className) == L"LockScreenBackstopFrame";
}

RECT VirtualScreen()
{
	const LONG left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const LONG top = GetSystemMetrics(SM_YVIRTUALSCREEN);
	return { left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
		top + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
}

bool Within(POINT point, const RECT& rect) { return PtInRect(&rect, point) != FALSE; }

double Distance(POINT first, POINT second)
{
	return std::hypot(static_cast<double>(first.x) - second.x,
		static_cast<double>(first.y) - second.y);
}

void ReleaseMouse() noexcept
{
	std::lock_guard lock(inputMutex);
	if (!mouseHeld) return;
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	input.mi.dwExtraInfo = InputTag;
	// 不把指针移回旧位置；一旦用户接管，当前位置属于用户。
	const UINT sent = SendInput(1, &input, sizeof(input));
	// 安全桌面可能拒绝注入；不能把失败的 Up 记为已释放，RAII 退出时仍会重试。
	if (sent == 1) mouseHeld = false;
	if (sent != 1) std::fputs("WARNING: SendInput LEFTUP failed; release the left button manually.\n", stderr);
}

BOOL WINAPI ConsoleControl(DWORD event)
{
	if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT
		&& event != CTRL_LOGOFF_EVENT && event != CTRL_SHUTDOWN_EVENT) return FALSE;
	stopRequested.store(true);
	ReleaseMouse();
	return TRUE;
}

struct MouseReleaseGuard
{
	~MouseReleaseGuard() { ReleaseMouse(); }
};

bool PressMouse()
{
	std::lock_guard lock(inputMutex);
	if (stopRequested.load()) return false;
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	input.mi.dwExtraInfo = InputTag;
	mouseHeld = true;
	return SendInput(1, &input, sizeof(input)) == 1;
}

bool MoveMouse(POINT point, const RECT& screen)
{
	std::lock_guard lock(inputMutex);
	if (stopRequested.load()) return false;
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dx = static_cast<LONG>(std::llround((static_cast<double>(point.x) - screen.left)
		* 65535.0 / (static_cast<double>(screen.right) - screen.left - 1.0)));
	input.mi.dy = static_cast<LONG>(std::llround((static_cast<double>(point.y) - screen.top)
		* 65535.0 / (static_cast<double>(screen.bottom) - screen.top - 1.0)));
	input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
	input.mi.dwExtraInfo = InputTag;
	return SendInput(1, &input, sizeof(input)) == 1;
}

class TickWaiter
{
	Handle timer_;
public:
	bool highResolution = true;
	TickWaiter() : timer_(CreateWaitableTimerExW(nullptr, nullptr,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE))
	{
		if (!timer_.value)
		{
			highResolution = false;
			timer_.value = CreateWaitableTimerW(nullptr, FALSE, nullptr);
		}
		if (!timer_.value) throw std::runtime_error("cannot create sampling waitable timer");
	}
	void Wait()
	{
		// 本进程的 1.5ms 相对等待，不调用 timeBeginPeriod 或修改全局计时器精度。
		LARGE_INTEGER due{};
		due.QuadPart = -15000;
		if (!SetWaitableTimer(timer_.value, &due, 0, nullptr, nullptr, FALSE)
			|| WaitForSingleObject(timer_.value, 100) != WAIT_OBJECT_0)
			throw std::runtime_error("sampling wait failed");
	}
};

struct Sample
{
	long long qpc = 0, readEndQpc = 0;
	POINT pointer{}, requested{};
	RECT rect{};
	unsigned inputSequence = 0;
	int cycle = -1, phase = 0;
	bool pointerValid = false, rectValid = false, inputPending = false, leftButtonDown = false;
};

void WriteCsv(const std::filesystem::path& path, const std::vector<Sample>& samples,
	long long frequency, long long origin, DWORD processId, HWND hwnd, const char* result)
{
	std::ofstream stream(path, std::ios::out | std::ios::trunc);
	if (!stream) throw std::runtime_error("cannot open output CSV");
	stream << "qpc,qpc_frequency,elapsed_us,read_end_qpc,pid,hwnd,pointer_valid,pointer_x,pointer_y,left_button_down,"
		"rect_valid,left,top,right,bottom,width,height,requested_x,requested_y,input_sequence,"
		"input_pending,cycle,phase,result\n";
	for (const auto& sample : samples)
		stream << sample.qpc << ',' << frequency << ',' << std::fixed << std::setprecision(3)
			<< (sample.qpc - origin) * 1000000.0 / frequency << ',' << sample.readEndQpc
			<< ',' << processId << ',' << reinterpret_cast<std::uintptr_t>(hwnd)
			<< ',' << sample.pointerValid << ',' << sample.pointer.x << ',' << sample.pointer.y
			<< ',' << sample.leftButtonDown
			<< ',' << sample.rectValid << ',' << sample.rect.left << ',' << sample.rect.top
			<< ',' << sample.rect.right << ',' << sample.rect.bottom << ','
			<< sample.rect.right - sample.rect.left << ',' << sample.rect.bottom - sample.rect.top
			<< ',' << sample.requested.x << ',' << sample.requested.y << ',' << sample.inputSequence
			<< ',' << sample.inputPending << ',' << sample.cycle << ',' << sample.phase << ',' << result << '\n';
	stream.flush();
	if (!stream) throw std::runtime_error("CSV write failed");
}

bool StartPointBelongsToBar(HWND hwnd, DWORD processId, POINT point)
{
	RECT rect{};
	DWORD owner = 0, hitOwner = 0;
	GetWindowThreadProcessId(hwnd, &owner);
	const HWND hit = WindowFromPoint(point);
	if (hit) GetWindowThreadProcessId(hit, &hitOwner);
	return owner == processId && hitOwner == processId && IsWindowVisible(hwnd)
		&& GetWindowRect(hwnd, &rect) && Within(point, rect)
		&& (hit == hwnd || (GetAncestor(hit, GA_ROOT) == hwnd && IsChild(hwnd, hit)));
}

int Replay(int argc, wchar_t** argv)
{
	if (argc != 11) throw std::runtime_error("replay requires PID startX startY endX endY cycles halfPeriodMs holdMs outputCsv");
	const DWORD processId = Pid(argv[2]);
	const POINT start{ Coordinate(argv[3]), Coordinate(argv[4]) };
	const POINT end{ Coordinate(argv[5]), Coordinate(argv[6]) };
	const int cycles = static_cast<int>(ParseInteger(argv[7], 1, 1000));
	const int halfMs = static_cast<int>(ParseInteger(argv[8], 10, 30000));
	const int holdMs = static_cast<int>(ParseInteger(argv[9], 0, 30000));
	const long long cycleMs = 2LL * (halfMs + holdMs);
	if (cycleMs * cycles > 59000)
		throw std::runtime_error("planned replay exceeds 59 seconds (60-second hard deadline includes preflight)");
	const std::filesystem::path output(argv[10]);
	if (std::filesystem::exists(output)) throw std::runtime_error("output CSV exists; choose a fresh path");
	if (!output.parent_path().empty() && !std::filesystem::is_directory(output.parent_path()))
		throw std::runtime_error("output CSV parent directory does not exist");
	const HWND hwnd = FindBar(processId);
	const RECT screen = VirtualScreen();
	if (screen.right - screen.left < 2 || screen.bottom - screen.top < 2
		|| !Within(start, screen) || !Within(end, screen))
		throw std::runtime_error("start/end must be inside the current virtual screen");
	if (!StartPointBelongsToBar(hwnd, processId, start))
		throw std::runtime_error("start point is not currently owned by the explicit PID's Bar");
	if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
		|| (GetAsyncKeyState(VK_MBUTTON) & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
		throw std::runtime_error("release mouse buttons and Escape before replay");
	PrintBar(hwnd);
	TickWaiter waiter;
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	const long long origin = Qpc();
	long long lastSentQpc = origin, phaseOrigin = origin;
	POINT requested{}, acknowledged{};
	if (!GetCursorPos(&acknowledged)) throw std::runtime_error("GetCursorPos failed before replay");
	requested = acknowledged;
	bool pending = false, started = false;
	int cycle = 0, phase = 0;
	unsigned inputSequence = 0;
	std::vector<Sample> samples;
	samples.reserve(65000);
	const char* result = "complete";
	int code = 0;
	MouseReleaseGuard release;
	std::wcout << L"sampling_us=1500 high_resolution_timer=" << waiter.highResolution
		<< L" planned_ms=" << cycleMs * cycles << L"; Escape or >20px manual movement aborts.\n";
	std::wcout.flush();
	try
	{
		while (true)
		{
			Sample sample{};
			sample.qpc = Qpc();
			sample.pointerValid = GetCursorPos(&sample.pointer) != FALSE;
			sample.rectValid = GetWindowRect(hwnd, &sample.rect) != FALSE;
			sample.leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			sample.readEndQpc = Qpc();
			sample.requested = requested;
			sample.inputSequence = inputSequence;
			sample.inputPending = pending;
			const double phaseMs = (sample.qpc - phaseOrigin) * 1000.0 / frequency.QuadPart;
			sample.cycle = started ? cycle : -1;
			sample.phase = phase;
			samples.push_back(sample);
			if (stopRequested.load() || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
			{ result = "operator_abort"; code = 3; break; }
			if ((sample.qpc - origin) / static_cast<double>(frequency.QuadPart) >= MaximumSeconds)
			{ result = "deadline"; code = 3; break; }
			DWORD owner = 0;
			GetWindowThreadProcessId(hwnd, &owner);
			// 某些启动器隔离进程枚举视图；原生 HWND 所属 PID 是这里的操作边界。
			if (!sample.pointerValid || !sample.rectValid || owner != processId || !IsWindowVisible(hwnd))
			{ result = "target_or_pointer_unavailable"; code = 3; break; }
			// 锁屏遮挡不一定隐藏原 Bar HWND；不能只凭 IsWindowVisible 继续发输入。
			if (IsLockScreenAt(sample.pointer))
			{ result = "lock_screen"; code = 3; break; }
			const double distance = Distance(sample.pointer, requested);
			if (pending && distance <= 2.0)
			{
				acknowledged = sample.pointer;
				pending = false;
			}
			// SendInput 异步生效时旧确认点可短暂停留；只容忍一个待确认点，避免覆盖用户输入。
			const bool stillOldPosition = pending && Distance(sample.pointer, acknowledged) <= 2.0;
			if (distance > TakeoverDistancePx && !stillOldPosition)
			{ result = "manual_pointer_takeover"; code = 3; break; }
			if (pending && (sample.qpc - lastSentQpc) / static_cast<double>(frequency.QuadPart) > InputAckTimeoutSeconds)
			{ result = "input_ack_timeout"; code = 3; break; }
			if (samples.size() == samples.capacity())
			{ result = "sample_capacity"; code = 3; break; }
			if (!pending)
			{
				POINT next = start;
				if (!started && inputSequence != 0)
				{
					if (Distance(sample.pointer, start) > 2.0 || !StartPointBelongsToBar(hwnd, processId, sample.pointer))
					{ result = "start_hit_changed"; code = 3; break; }
					if (!PressMouse()) { result = "mouse_down_failed"; code = 4; break; }
					started = true;
					phase = 1;
					phaseOrigin = Qpc();
				}
				else if (started)
				{
					// 先确认端点确已送达再换向；holdMs=0 或调度延迟也不跳过端点。
					if ((phase == 1 || phase == 3) && phaseMs >= halfMs)
					{
						const POINT endpoint = phase == 1 ? end : start;
						if (requested.x == endpoint.x && requested.y == endpoint.y
							&& Distance(sample.pointer, endpoint) <= 2.0)
						{ ++phase; phaseOrigin = sample.qpc; }
					}
					else if ((phase == 2 || phase == 4) && phaseMs >= holdMs)
					{
						if (phase == 4 && ++cycle == cycles) break;
						phase = phase == 2 ? 3 : 1;
						phaseOrigin = sample.qpc;
					}
					const double progressMs = (sample.qpc - phaseOrigin) * 1000.0 / frequency.QuadPart;
					const double progress = phase == 1 ? (std::min)(1.0, progressMs / halfMs)
						: phase == 2 ? 1.0 : phase == 3 ? 1.0 - (std::min)(1.0, progressMs / halfMs) : 0.0;
					next = { static_cast<LONG>(std::llround(start.x + (static_cast<double>(end.x) - start.x) * progress)),
						static_cast<LONG>(std::llround(start.y + (static_cast<double>(end.y) - start.y) * progress)) };
				}
				if (inputSequence == 0 || next.x != requested.x || next.y != requested.y)
				{
					acknowledged = sample.pointer;
					if (!MoveMouse(next, screen)) { result = "mouse_move_failed"; code = 4; break; }
					requested = next;
					lastSentQpc = Qpc();
					pending = true;
					++inputSequence;
				}
			}
			waiter.Wait();
		}
	}
	catch (const std::exception& error)
	{
		result = "sampling_exception";
		code = 4;
		ReleaseMouse();
		std::cerr << error.what() << '\n';
	}
	ReleaseMouse();
	// 手势结束并释放后才进行磁盘写入，避免日志 I/O 改变竞态窗口。
	WriteCsv(output, samples, frequency.QuadPart, origin, processId, hwnd, result);
	std::cout << "result=" << result << " samples=" << samples.size() << " moves=" << inputSequence
		<< " (HWND geometry only; no visual flicker verdict)\n";
	return code;
}

int Observe(int argc, wchar_t** argv)
{
	if (argc != 5) throw std::runtime_error("observe requires PID durationMs outputCsv");
	const DWORD processId = Pid(argv[2]);
	const int durationMs = static_cast<int>(ParseInteger(argv[3], 1, 60000));
	const std::filesystem::path output(argv[4]);
	if (std::filesystem::exists(output)) throw std::runtime_error("output CSV exists; choose a fresh path");
	if (!output.parent_path().empty() && !std::filesystem::is_directory(output.parent_path()))
		throw std::runtime_error("output CSV parent directory does not exist");
	const HWND hwnd = FindBar(processId);
	PrintBar(hwnd);
	TickWaiter waiter;
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	std::vector<Sample> samples;
	samples.reserve(65000);
	std::wcout << L"observe_only=1 sampling_us=1500 high_resolution_timer=" << waiter.highResolution
		<< L" duration_ms=" << durationMs << L"; Escape or Ctrl+C stops observation.\n";
	std::wcout.flush();
	const long long origin = Qpc();
	const char* result = "complete";
	int code = 0;
	try
	{
		while (true)
		{
			Sample sample{};
			sample.qpc = Qpc();
			sample.pointerValid = GetCursorPos(&sample.pointer) != FALSE;
			sample.rectValid = GetWindowRect(hwnd, &sample.rect) != FALSE;
			sample.leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			sample.readEndQpc = Qpc();
			// phase=5 明确表示纯观察；requested/input_sequence 不代表注入目标。
			sample.phase = 5;
			samples.push_back(sample);
			if (stopRequested.load() || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
			{ result = "operator_abort"; code = 3; break; }
			DWORD owner = 0;
			GetWindowThreadProcessId(hwnd, &owner);
			if (!sample.rectValid || owner != processId || !IsWindowVisible(hwnd))
			{ result = "target_unavailable"; code = 3; break; }
			if ((sample.qpc - origin) * 1000.0 / frequency.QuadPart >= durationMs) break;
			if (samples.size() == samples.capacity())
			{ result = "sample_capacity"; code = 3; break; }
			waiter.Wait();
		}
	}
	catch (const std::exception& error)
	{
		result = "sampling_exception";
		code = 4;
		std::cerr << error.what() << '\n';
	}
	// 纯观察不调用任何鼠标注入入口，实体按键的释放完全归用户所有。
	WriteCsv(output, samples, frequency.QuadPart, origin, processId, hwnd, result);
	std::cout << "result=" << result << " samples=" << samples.size()
		<< " observe_only=1 (HWND geometry only; no visual flicker verdict)\n";
	return code;
}

void CaptureRect(RECT requested, const std::filesystem::path& output)
{
	if (std::filesystem::exists(output)) throw std::runtime_error("output BMP exists; choose a fresh path");
	RECT rect{};
	const RECT screen = VirtualScreen();
	if (!IntersectRect(&rect, &requested, &screen)) throw std::runtime_error("capture rectangle misses virtual screen");
	const LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
	const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4;
	if (bytes > 512ULL * 1024 * 1024) throw std::runtime_error("capture exceeds 512 MiB");
	HDC desktop = GetDC(nullptr);
	HDC memory = desktop ? CreateCompatibleDC(desktop) : nullptr;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bitmap = memory ? CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
	HGDIOBJ old = bitmap ? SelectObject(memory, bitmap) : nullptr;
	bool success = false;
	const long long before = Qpc();
	if (old && old != HGDI_ERROR)
		success = BitBlt(memory, 0, 0, width, height, desktop, rect.left, rect.top, SRCCOPY | CAPTUREBLT) != FALSE;
	const long long after = Qpc();
	std::vector<std::uint8_t> pixels;
	if (success)
	{
		GdiFlush();
		const auto* begin = static_cast<const std::uint8_t*>(bits);
		pixels.assign(begin, begin + bytes);
	}
	if (old && old != HGDI_ERROR) SelectObject(memory, old);
	if (bitmap) DeleteObject(bitmap);
	if (memory) DeleteDC(memory);
	if (desktop) ReleaseDC(nullptr, desktop);
	if (!success) throw std::runtime_error("desktop BitBlt/CAPTUREBLT failed");
	BITMAPFILEHEADER header{};
	header.bfType = 0x4D42;
	header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
	header.bfSize = header.bfOffBits + static_cast<DWORD>(bytes);
	std::ofstream stream(output, std::ios::out | std::ios::binary | std::ios::trunc);
	stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
	stream.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
	stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
	stream.flush();
	if (!stream) throw std::runtime_error("BMP write failed");
	std::cout << "capture_rect=[" << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom
		<< "] qpc_before=" << before << " qpc_after=" << after << " single_desktop_capture=1\n";
}

void Usage()
{
	std::wcout << L"live-dock-probe inspect [PID]\n"
		L"live-dock-probe hit x y\n"
		L"live-dock-probe replay PID startX startY endX endY cycles halfPeriodMs holdMs outputCsv\n"
		L"live-dock-probe observe PID durationMs outputCsv\n"
		L"live-dock-probe capture bar PID paddingPx outputBmp\n"
		L"live-dock-probe capture rect left top right bottom outputBmp\n"
		L"All coordinates are physical virtual-desktop pixels. Use only the PID launched for this task.\n"
		L"Replay keeps one left-button press across all cycles; each end has holdMs. Max 60 s.\n"
		L"Escape/manual pointer deviation >20 px/Ctrl+C abort and release. Output paths must be fresh.\n"
		L"CSV phases: 0 preflight, 1 outbound, 2 end hold, 3 inbound, 4 start hold, 5 observe.\n"
		L"Observe injects no input; requested/input_sequence stay zero. Left-button state is polled in both modes.\n"
		L"Samples describe HWND geometry, not composited screen pixels or a flicker verdict.\n";
}
}

int wmain(int argc, wchar_t** argv)
{
	if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
	{
		std::cerr << "cannot enable physical per-monitor coordinates; error=" << GetLastError() << '\n';
		return 2;
	}
	if (!SetConsoleCtrlHandler(ConsoleControl, TRUE))
	{
		std::cerr << "cannot install console cancellation handler\n";
		return 2;
	}
	try
	{
		if (argc < 2 || std::wstring_view(argv[1]) == L"--help") { Usage(); return 0; }
		const std::wstring_view command(argv[1]);
		if (command == L"hit")
		{
			if (argc != 4) throw std::runtime_error("hit requires x y");
			PrintHit({ Coordinate(argv[2]), Coordinate(argv[3]) });
			return 0;
		}
		if (command == L"inspect")
		{
			if (argc > 3) throw std::runtime_error("inspect accepts only optional PID");
			const DWORD filter = argc == 3 ? Pid(argv[2]) : 0;
			int count = 0;
			for (const HWND hwnd : Bars())
			{
				DWORD processId = 0;
				GetWindowThreadProcessId(hwnd, &processId);
				if (!filter || filter == processId) { PrintBar(hwnd); ++count; }
			}
			std::wcout << L"bar_count=" << count << L'\n';
			return count ? 0 : 1;
		}
		if (command == L"replay") return Replay(argc, argv);
		if (command == L"observe") return Observe(argc, argv);
		if (command == L"capture" && argc >= 3)
		{
			RECT rect{};
			const wchar_t* output = nullptr;
			if (std::wstring_view(argv[2]) == L"bar" && argc == 6)
			{
				const HWND hwnd = FindBar(Pid(argv[3]));
				if (!GetWindowRect(hwnd, &rect)) throw std::runtime_error("GetWindowRect failed");
				const int padding = static_cast<int>(ParseInteger(argv[4], 0, 10000));
				InflateRect(&rect, padding, padding);
				output = argv[5];
			}
			else if (std::wstring_view(argv[2]) == L"rect" && argc == 8)
			{
				rect = { Coordinate(argv[3]), Coordinate(argv[4]), Coordinate(argv[5]), Coordinate(argv[6]) };
				if (rect.right <= rect.left || rect.bottom <= rect.top) throw std::runtime_error("empty capture rectangle");
				output = argv[7];
			}
			else throw std::runtime_error("invalid capture arguments; see --help");
			CaptureRect(rect, output);
			return 0;
		}
		throw std::runtime_error("unknown command; see --help");
	}
	catch (const std::exception& error)
	{
		ReleaseMouse();
		std::cerr << "error: " << error.what() << '\n';
		return 2;
	}
}
