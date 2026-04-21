module;

#include "../../IdtMain.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

#undef max
#undef min

export module Inkeys.Helper.SecRandom;

import Inkeys.Conv.Text;

namespace
{
	constexpr std::wstring_view kDefaultIpcName = L"SecRandom.secrandom";
	constexpr std::wstring_view kDefaultQuickDrawUrl = L"secrandom://roll_call/quick_draw";

	class UniqueHandle
	{
	public:
		UniqueHandle() = default;
		explicit UniqueHandle(HANDLE handleIn) : handle(handleIn) {}

		~UniqueHandle()
		{
			Reset();
		}

		UniqueHandle(const UniqueHandle&) = delete;
		UniqueHandle& operator=(const UniqueHandle&) = delete;

		UniqueHandle(UniqueHandle&& other) noexcept
		{
			handle = other.Release();
		}
		UniqueHandle& operator=(UniqueHandle&& other) noexcept
		{
			if (this != &other) Reset(other.Release());
			return *this;
		}

		void Reset(HANDLE newHandle = INVALID_HANDLE_VALUE)
		{
			if (handle != INVALID_HANDLE_VALUE && handle != nullptr) CloseHandle(handle);
			handle = newHandle;
		}

		HANDLE Get() const
		{
			return handle;
		}

		HANDLE Release()
		{
			HANDLE released = handle;
			handle = INVALID_HANDLE_VALUE;
			return released;
		}

		explicit operator bool() const
		{
			return handle != INVALID_HANDLE_VALUE && handle != nullptr;
		}

	private:
		HANDLE handle = INVALID_HANDLE_VALUE;
	};

	void LogFailure(const std::wstring& message)
	{
		if (IDTLogger) IDTLogger->warn("[SecRandom] {}", utf16ToUtf8(message));
	}

	void AssignError(std::wstring* errorMessage, const std::wstring& message, bool log = true)
	{
		if (errorMessage) *errorMessage = message;
		if (log) LogFailure(message);
	}

	std::wstring FormatWindowsErrorMessage(DWORD errorCode)
	{
		LPWSTR buffer = nullptr;
		const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
		DWORD length = FormatMessageW(flags, nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

		std::wstring message;
		if (length != 0 && buffer != nullptr)
		{
			message.assign(buffer, length);
			while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'\t'))
			{
				message.pop_back();
			}
		}
		else
		{
			message = L"Windows error " + std::to_wstring(errorCode);
		}

		if (buffer) LocalFree(buffer);
		return message;
	}

	std::wstring BuildPipePath(std::wstring_view ipcName)
	{
		return LR"(\\.\pipe\)" + std::wstring(ipcName);
	}

	std::wstring FormatErrorCode(DWORD errorCode)
	{
		std::wstringstream stream;
		stream << L"0x" << std::uppercase << std::hex;
		stream.width(8);
		stream.fill(L'0');
		stream << errorCode;
		return stream.str();
	}

	std::wstring FormatWindowsErrorDetail(DWORD errorCode)
	{
		return FormatErrorCode(errorCode) + L" (" + FormatWindowsErrorMessage(errorCode) + L")";
	}

	std::string TrimAsciiWhitespace(std::string text)
	{
		auto isSpace = [](unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };

		while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
		while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) text.pop_back();
		return text;
	}

	bool ConnectPipe(std::wstring_view ipcName, DWORD timeoutMs, UniqueHandle& pipeHandle, std::wstring* errorMessage)
	{
		const std::wstring pipePath = BuildPipePath(ipcName);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		const DWORD pipeClientFlags = SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION;

		while (true)
		{
			HANDLE rawHandle = CreateFileW(pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, pipeClientFlags, nullptr);
			if (rawHandle != INVALID_HANDLE_VALUE)
			{
				pipeHandle.Reset(rawHandle);
				return true;
			}

			DWORD lastError = GetLastError();
			if (lastError == ERROR_ACCESS_DENIED)
			{
				AssignError(errorMessage, L"连接 SecRandom IPC 通道被拒绝: " + FormatWindowsErrorDetail(lastError) + L"。");
				return false;
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				if (lastError == ERROR_FILE_NOT_FOUND)
				{
					AssignError(errorMessage, L"SecRandom IPC 通道不存在，请确认 SecRandom 已运行并启用了 URL IPC。");
				}
				else
				{
					AssignError(errorMessage, L"连接 SecRandom IPC 通道失败: " + FormatWindowsErrorDetail(lastError) + L"。");
				}
				return false;
			}

			if (lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_PIPE_BUSY)
			{
				AssignError(errorMessage, L"连接 SecRandom IPC 通道失败: " + FormatWindowsErrorDetail(lastError) + L"。");
				return false;
			}

			const auto now = std::chrono::steady_clock::now();
			const auto remainingMs = deadline > now ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() : 0;
			DWORD waitSlice = static_cast<DWORD>(remainingMs > 1000 ? 1000 : remainingMs);
			if (waitSlice == 0) waitSlice = 1;

			if (lastError == ERROR_FILE_NOT_FOUND)
			{
				Sleep(waitSlice);
				continue;
			}

			if (!WaitNamedPipeW(pipePath.c_str(), waitSlice))
			{
				DWORD waitError = GetLastError();
				if (waitError != ERROR_FILE_NOT_FOUND && waitError != ERROR_SEM_TIMEOUT)
				{
					AssignError(errorMessage, L"等待 SecRandom IPC 管道就绪失败: " + FormatWindowsErrorDetail(waitError) + L"。");
					return false;
				}
			}
		}
	}

	bool WriteAll(HANDLE pipeHandle, const char* buffer, size_t length, std::wstring* errorMessage)
	{
		size_t offset = 0;
		while (offset < length)
		{
			DWORD written = 0;
			DWORD chunkLength = static_cast<DWORD>(min<size_t>(length - offset, numeric_limits<DWORD>::max()));
			if (!WriteFile(pipeHandle, buffer + offset, chunkLength, &written, nullptr))
			{
				AssignError(errorMessage, L"写入 SecRandom IPC 管道失败: " + FormatWindowsErrorMessage(GetLastError()));
				return false;
			}
			if (written == 0)
			{
				AssignError(errorMessage, L"写入 SecRandom IPC 管道失败: 写入了 0 字节。");
				return false;
			}
			offset += written;
		}
		return true;
	}

	bool ReadResponseLine(HANDLE pipeHandle, std::string& responseLine, std::wstring* errorMessage)
	{
		responseLine.clear();
		vector<char> chunk(512);

		while (true)
		{
			DWORD readBytes = 0;
			BOOL readOk = ReadFile(pipeHandle, chunk.data(), static_cast<DWORD>(chunk.size()), &readBytes, nullptr);
			DWORD lastError = readOk ? ERROR_SUCCESS : GetLastError();

			if (readBytes != 0)
			{
				responseLine.append(chunk.data(), chunk.data() + readBytes);
				if (responseLine.find('\n') != string::npos) break;
			}

			if (readOk)
			{
				if (readBytes == 0) break;
				continue;
			}

			if (lastError == ERROR_MORE_DATA)
			{
				continue;
			}
			if (lastError == ERROR_BROKEN_PIPE)
			{
				break;
			}

			AssignError(errorMessage, L"读取 SecRandom IPC 响应失败: " + FormatWindowsErrorMessage(lastError));
			return false;
		}

		if (responseLine.empty())
		{
			AssignError(errorMessage, L"SecRandom IPC 返回了空响应。");
			return false;
		}

		size_t newlinePos = responseLine.find('\n');
		if (newlinePos != string::npos) responseLine.resize(newlinePos);
		responseLine = TrimAsciiWhitespace(responseLine);
		if (responseLine.empty())
		{
			AssignError(errorMessage, L"SecRandom IPC 返回了空白响应。");
			return false;
		}

		return true;
	}
}

export namespace Inkeys::SecRandom
{
	bool SendUrl(std::wstring_view url, std::wstring* errorMessage = nullptr, DWORD timeoutMs = 5000)
	{
		UniqueHandle pipeHandle;
		if (!ConnectPipe(kDefaultIpcName, timeoutMs, pipeHandle, errorMessage)) return false;

		Json::Value request(Json::objectValue);
		request["type"] = Json::Value("url");
		request["payload"]["url"] = Json::Value(utf16ToUtf8(url));

		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		std::string requestJson = Json::writeString(writer, request);
		requestJson.push_back('\n');

		if (!WriteAll(pipeHandle.Get(), requestJson.data(), requestJson.size(), errorMessage)) return false;

		std::string responseJson;
		if (!ReadResponseLine(pipeHandle.Get(), responseJson, errorMessage)) return false;

		Json::CharReaderBuilder reader;
		reader["collectComments"] = false;

		Json::Value response;
		std::istringstream responseStream(responseJson);
		std::string parseErrors;
		if (!Json::parseFromStream(reader, responseStream, &response, &parseErrors))
		{
			AssignError(errorMessage, L"SecRandom IPC 返回了无法解析的响应。");
			return false;
		}

		if (!response.isMember("success") || !response["success"].isBool())
		{
			AssignError(errorMessage, L"SecRandom IPC 返回缺少 success 字段的响应。");
			return false;
		}

		if (!response["success"].asBool())
		{
			AssignError(errorMessage, L"SecRandom IPC 调用失败。");
			return false;
		}

		if (errorMessage) errorMessage->clear();
		return true;
	}

	bool OpenQuickDraw(std::wstring* errorMessage = nullptr, DWORD timeoutMs = 5000)
	{
		return SendUrl(kDefaultQuickDrawUrl, errorMessage, timeoutMs);
	}
}
