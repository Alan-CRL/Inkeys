#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
	struct WindowSearch
	{
		DWORD processId = 0;
		HWND window = nullptr;
		long long largestArea = 0;
	};

	BOOL CALLBACK FindLargestProcessWindow(HWND window, LPARAM parameter)
	{
		auto* search = reinterpret_cast<WindowSearch*>(parameter);
		DWORD processId = 0;
		GetWindowThreadProcessId(window, &processId);
		if (processId != search->processId || !IsWindowVisible(window)) return TRUE;
		RECT client = {};
		if (!GetClientRect(window, &client)) return TRUE;
		const long long area = static_cast<long long>(client.right - client.left) *
			static_cast<long long>(client.bottom - client.top);
		if (area > search->largestArea)
		{
			search->largestArea = area;
			search->window = window;
		}
		return TRUE;
	}

	HWND WaitForDrawingWindow(DWORD processId)
	{
		for (int attempt = 0; attempt < 200; ++attempt)
		{
			WindowSearch search;
			search.processId = processId;
			EnumWindows(FindLargestProcessWindow, reinterpret_cast<LPARAM>(&search));
			if (search.window && search.largestArea > 100000) return search.window;
			Sleep(50);
		}
		return nullptr;
	}

	void SendKey(WORD virtualKey)
	{
		INPUT inputs[2] = {};
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = virtualKey;
		inputs[1] = inputs[0];
		inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(2, inputs, sizeof(INPUT));
	}

	LONG NormalizeAbsoluteCoordinate(int value, int origin, int extent)
	{
		if (extent <= 1) return 0;
		return static_cast<LONG>(std::clamp(
			static_cast<long long>(value - origin) * 65535ll /
			static_cast<long long>(extent - 1), 0ll, 65535ll));
	}

	void SendMouse(int x, int y, DWORD flags)
	{
		const int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		INPUT input = {};
		input.type = INPUT_MOUSE;
		input.mi.dx = NormalizeAbsoluteCoordinate(x, virtualLeft, virtualWidth);
		input.mi.dy = NormalizeAbsoluteCoordinate(y, virtualTop, virtualHeight);
		input.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
		SendInput(1, &input, sizeof(INPUT));
	}

	void SendStroke(int startX, int startY, int endX, int endY, int moveCount, DWORD moveDelayMs)
	{
		SendMouse(startX, startY, MOUSEEVENTF_MOVE);
		SendMouse(startX, startY, MOUSEEVENTF_LEFTDOWN);
		for (int index = 1; index <= moveCount; ++index)
		{
			const int x = startX + (endX - startX) * index / moveCount;
			const int y = startY + (endY - startY) * index / moveCount;
			SendMouse(x, y, MOUSEEVENTF_MOVE);
			if (moveDelayMs != 0) Sleep(moveDelayMs);
		}
		SendMouse(endX, endY, MOUSEEVENTF_LEFTUP);
	}

	bool ReadWholeFile(const wchar_t* path, std::string& text)
	{
		HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		const DWORD size = GetFileSize(file, nullptr);
		if (size == INVALID_FILE_SIZE)
		{
			CloseHandle(file);
			return false;
		}
		text.resize(size);
		DWORD read = 0;
		const bool succeeded = ReadFile(file, text.data(), size, &read, nullptr) && read == size;
		CloseHandle(file);
		return succeeded;
	}
}

int RunRuntimeBenchmark(const wchar_t* applicationPath, const wchar_t* reportPath)
{
	if (!applicationPath || !reportPath) return 2;
	std::wstring commandLine = L"\"";
	commandLine += applicationPath;
	commandLine += L"\" --metrics-output \"";
	commandLine += reportPath;
	commandLine += L"\" --strict-metrics";
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(applicationPath, mutableCommand.data(), nullptr, nullptr, FALSE,
		0, nullptr, nullptr, &startup, &process))
	{
		std::cerr << "CreateProcessW failed: " << GetLastError() << std::endl;
		return 2;
	}

	HWND window = WaitForDrawingWindow(process.dwProcessId);
	if (!window)
	{
		TerminateProcess(process.hProcess, 3);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		std::cerr << "Drawing window did not become ready." << std::endl;
		return 3;
	}
	SetForegroundWindow(window);
	SetActiveWindow(window);
	Sleep(200);

	RECT originalRect = {};
	GetWindowRect(window, &originalRect);
	const int width = originalRect.right - originalRect.left;
	const int height = originalRect.bottom - originalRect.top;
	const int left = originalRect.left + std::max(80, width / 8);
	const int top = originalRect.top + std::max(80, height / 8);
	const int usableWidth = std::max<int>(
		200, width - 2 * (left - static_cast<int>(originalRect.left)));
	const int usableHeight = std::max<int>(
		200, height - 2 * (top - static_cast<int>(originalRect.top)));

	for (int index = 0; index < 250; ++index)
	{
		if (index == 0) SendKey('1');
		if (index == 150) SendKey('2');
		if (index == 200) SendKey('3');
		const int row = index / 20;
		const int column = index % 20;
		const int startX = left + column * std::max(8, usableWidth / 22);
		const int startY = top + row * std::max(8, usableHeight / 14);
		const int endX = std::min<int>(
			static_cast<int>(originalRect.right) - 40, startX + 40 + index % 17);
		const int endY = std::min<int>(
			static_cast<int>(originalRect.bottom) - 40, startY + (index % 5) * 3);
		SendStroke(startX, startY, endX, endY, 3, 0);
		Sleep(14);
	}

	// 240Hz Move 注入覆盖连续曲线；Move 本身不应解除 120 FPS 合并上界。
	SendKey('1');
	const int curveStartX = left + usableWidth / 4;
	const int curveStartY = top + usableHeight / 2;
	SendMouse(curveStartX, curveStartY, MOUSEEVENTF_MOVE);
	SendMouse(curveStartX, curveStartY, MOUSEEVENTF_LEFTDOWN);
	for (int index = 1; index <= 120; ++index)
	{
		const double phase = static_cast<double>(index) * 0.12;
		const int x = curveStartX + index * std::max(1, usableWidth / 180);
		const int y = curveStartY + static_cast<int>(80.0 * std::sin(phase));
		SendMouse(x, y, MOUSEEVENTF_MOVE);
		Sleep(4);
	}
	SendMouse(curveStartX + usableWidth * 2 / 3, curveStartY,
		MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP);

	// 停笔后继续、清屏与 resize 都使用真实窗口/输入消息。
	SendMouse(left + 50, top + usableHeight - 100, MOUSEEVENTF_MOVE);
	SendMouse(left + 50, top + usableHeight - 100, MOUSEEVENTF_LEFTDOWN);
	Sleep(80);
	SendMouse(left + 120, top + usableHeight - 80, MOUSEEVENTF_MOVE);
	SendMouse(left + 120, top + usableHeight - 80, MOUSEEVENTF_LEFTUP);
	Sleep(100);
	SendKey('0');
	Sleep(100);
	SetWindowPos(window, HWND_TOP, originalRect.left, originalRect.top,
		std::max(640, width * 3 / 4), std::max(480, height * 3 / 4), SWP_SHOWWINDOW);
	Sleep(500);

	// resize 消息稳定后取得连续 5 秒空闲样本；双向恢复已由独立集成复现验证。
	Sleep(6500);
	SetForegroundWindow(window);
	SendKey('9');
	const DWORD waitResult = WaitForSingleObject(process.hProcess, 30000);
	DWORD exitCode = 0;
	if (waitResult != WAIT_OBJECT_0)
	{
		TerminateProcess(process.hProcess, 4);
		exitCode = 4;
	}
	else
	{
		GetExitCodeProcess(process.hProcess, &exitCode);
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	std::string report;
	if (!ReadWholeFile(reportPath, report))
	{
		std::cerr << "Metrics report was not written." << std::endl;
		return 5;
	}
	const bool strictPass = report.find("\"strictPass\": true") != std::string::npos;
	std::cout << "Runtime benchmark process exit=" << exitCode
		<< " strictPass=" << (strictPass ? "true" : "false") << std::endl;
	return exitCode == 0 && strictPass ? 0 : 6;
}
