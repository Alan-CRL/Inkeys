module;

#include "../../IdtMain.h"

#include <array>
#include <bcrypt.h>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")

#undef max
#undef min

export module Inkeys.Helper.SecRandom;

import Inkeys.Conv.Text;

namespace
{
	constexpr std::wstring_view kDefaultIpcName = L"SecRandom.secrandom";
	constexpr std::wstring_view kDefaultQuickDrawUrl = L"secrandom://roll_call/quick_draw";

	constexpr std::string_view kChallengePrefix = "#CHALLENGE#";
	constexpr std::string_view kWelcomeMessage = "#WELCOME#";
	constexpr std::string_view kFailureMessage = "#FAILURE#";
	constexpr size_t kChallengeRandomLength = 20;

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
			if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
			{
				CloseHandle(handle);
			}
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
		std::wstring debugMessage = L"[SecRandom] " + message + L"\n";
		OutputDebugStringW(debugMessage.c_str());
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

	bool WriteAll(HANDLE pipeHandle, const char* buffer, size_t length, std::wstring* errorMessage)
	{
		size_t offset = 0;
		while (offset < length)
		{
			DWORD written = 0;
			DWORD chunkLength = static_cast<DWORD>((length - offset) > static_cast<size_t>(std::numeric_limits<DWORD>::max())
				? std::numeric_limits<DWORD>::max()
				: (length - offset));

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

	bool ReadExact(HANDLE pipeHandle, char* buffer, size_t length, std::wstring* errorMessage)
	{
		size_t offset = 0;
		while (offset < length)
		{
			DWORD readBytes = 0;
			DWORD chunkLength = static_cast<DWORD>((length - offset) > static_cast<size_t>(std::numeric_limits<DWORD>::max())
				? std::numeric_limits<DWORD>::max()
				: (length - offset));

			BOOL readOk = ReadFile(pipeHandle, buffer + offset, chunkLength, &readBytes, nullptr);
			if (!readOk)
			{
				DWORD lastError = GetLastError();
				if (lastError != ERROR_MORE_DATA)
				{
					AssignError(errorMessage, L"读取 SecRandom IPC 管道失败: " + FormatWindowsErrorMessage(lastError));
					return false;
				}
			}
			if (readBytes == 0)
			{
				AssignError(errorMessage, L"读取 SecRandom IPC 管道失败: 对端提前关闭了连接。");
				return false;
			}

			offset += readBytes;
		}

		return true;
	}

	void AppendInt32BE(std::string& buffer, int32_t value)
	{
		unsigned char bytes[4] =
		{
			static_cast<unsigned char>((value >> 24) & 0xFF),
			static_cast<unsigned char>((value >> 16) & 0xFF),
			static_cast<unsigned char>((value >> 8) & 0xFF),
			static_cast<unsigned char>(value & 0xFF)
		};
		buffer.append(reinterpret_cast<const char*>(bytes), sizeof(bytes));
	}

	void AppendUInt64BE(std::string& buffer, uint64_t value)
	{
		unsigned char bytes[8] =
		{
			static_cast<unsigned char>((value >> 56) & 0xFF),
			static_cast<unsigned char>((value >> 48) & 0xFF),
			static_cast<unsigned char>((value >> 40) & 0xFF),
			static_cast<unsigned char>((value >> 32) & 0xFF),
			static_cast<unsigned char>((value >> 24) & 0xFF),
			static_cast<unsigned char>((value >> 16) & 0xFF),
			static_cast<unsigned char>((value >> 8) & 0xFF),
			static_cast<unsigned char>(value & 0xFF)
		};
		buffer.append(reinterpret_cast<const char*>(bytes), sizeof(bytes));
	}

	int32_t ParseInt32BE(const char* buffer)
	{
		return
			(static_cast<int32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
			(static_cast<int32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
			(static_cast<int32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
			static_cast<int32_t>(static_cast<unsigned char>(buffer[3]));
	}

	uint64_t ParseUInt64BE(const char* buffer)
	{
		return
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[0])) << 56) |
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[1])) << 48) |
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[2])) << 40) |
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[3])) << 32) |
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[4])) << 24) |
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[5])) << 16) |
			(static_cast<uint64_t>(static_cast<unsigned char>(buffer[6])) << 8) |
			static_cast<uint64_t>(static_cast<unsigned char>(buffer[7]));
	}

	bool SendFramedBytes(HANDLE pipeHandle, const std::string& payload, std::wstring* errorMessage)
	{
		std::string framed;
		if (payload.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
		{
			AppendInt32BE(framed, -1);
			AppendUInt64BE(framed, static_cast<uint64_t>(payload.size()));
		}
		else
		{
			AppendInt32BE(framed, static_cast<int32_t>(payload.size()));
		}
		framed.append(payload);

		return WriteAll(pipeHandle, framed.data(), framed.size(), errorMessage);
	}

	bool ReceiveFramedBytes(HANDLE pipeHandle, std::string& payload, size_t maxLength, std::wstring* errorMessage)
	{
		char header[4] = {};
		if (!ReadExact(pipeHandle, header, sizeof(header), errorMessage)) return false;

		int32_t size = ParseInt32BE(header);
		uint64_t messageLength = 0;

		if (size == -1)
		{
			char extendedHeader[8] = {};
			if (!ReadExact(pipeHandle, extendedHeader, sizeof(extendedHeader), errorMessage)) return false;
			messageLength = ParseUInt64BE(extendedHeader);
		}
		else if (size < 0)
		{
			AssignError(errorMessage, L"读取 SecRandom IPC 数据失败: 收到了无效的负长度帧。");
			return false;
		}
		else
		{
			messageLength = static_cast<uint64_t>(size);
		}

		if (messageLength > maxLength)
		{
			AssignError(errorMessage, L"读取 SecRandom IPC 数据失败: 返回数据超出允许大小。");
			return false;
		}

		payload.assign(static_cast<size_t>(messageLength), '\0');
		if (messageLength == 0) return true;

		return ReadExact(pipeHandle, payload.data(), payload.size(), errorMessage);
	}

	bool ComputeHmacMd5(const std::string& key, const std::string& message, std::string& digest, std::wstring* errorMessage)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::vector<UCHAR> hashObject;
		std::array<UCHAR, 16> hashBytes = {};
		DWORD propertySize = 0;
		DWORD objectLength = 0;
		DWORD hashLength = 0;

		NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
		if (status < 0)
		{
			AssignError(errorMessage, L"初始化 HMAC-MD5 失败。");
			return false;
		}

		status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &propertySize, 0);
		if (status < 0)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			AssignError(errorMessage, L"读取 HMAC-MD5 对象长度失败。");
			return false;
		}

		status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &propertySize, 0);
		if (status < 0 || hashLength != hashBytes.size())
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			AssignError(errorMessage, L"读取 HMAC-MD5 哈希长度失败。");
			return false;
		}

		hashObject.resize(objectLength);

		status = BCryptCreateHash(
			algorithm,
			&hash,
			hashObject.data(),
			static_cast<ULONG>(hashObject.size()),
			reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
			static_cast<ULONG>(key.size()),
			0);
		if (status < 0)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			AssignError(errorMessage, L"创建 HMAC-MD5 哈希对象失败。");
			return false;
		}

		status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())), static_cast<ULONG>(message.size()), 0);
		if (status < 0)
		{
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			AssignError(errorMessage, L"HMAC-MD5 写入数据失败。");
			return false;
		}

		status = BCryptFinishHash(hash, hashBytes.data(), static_cast<ULONG>(hashBytes.size()), 0);
		if (status < 0)
		{
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			AssignError(errorMessage, L"HMAC-MD5 计算失败。");
			return false;
		}

		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);

		digest.assign(reinterpret_cast<const char*>(hashBytes.data()), hashBytes.size());
		return true;
	}

	bool FillRandomBytes(std::string& buffer, std::wstring* errorMessage)
	{
		if (buffer.empty()) return true;

		NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		if (status < 0)
		{
			AssignError(errorMessage, L"生成 SecRandom IPC 挑战随机数失败。");
			return false;
		}

		return true;
	}

	bool ConnectPipe(std::wstring_view ipcName, DWORD timeoutMs, UniqueHandle& pipeHandle, std::wstring* errorMessage)
	{
		const std::wstring pipePath = BuildPipePath(ipcName);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

		while (true)
		{
			HANDLE rawHandle = CreateFileW(pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (rawHandle != INVALID_HANDLE_VALUE)
			{
				DWORD readMode = PIPE_READMODE_MESSAGE;
				if (!SetNamedPipeHandleState(rawHandle, &readMode, nullptr, nullptr))
				{
					DWORD lastError = GetLastError();
					CloseHandle(rawHandle);
					AssignError(errorMessage, L"切换 SecRandom IPC 管道到消息模式失败: " + FormatWindowsErrorMessage(lastError));
					return false;
				}

				pipeHandle.Reset(rawHandle);
				return true;
			}

			DWORD lastError = GetLastError();
			if (std::chrono::steady_clock::now() >= deadline)
			{
				if (lastError == ERROR_FILE_NOT_FOUND)
				{
					AssignError(errorMessage, L"SecRandom IPC 通道不存在，请确认 SecRandom 已运行并启用了 URL IPC。");
				}
				else
				{
					AssignError(errorMessage, L"连接 SecRandom IPC 通道失败: " + FormatWindowsErrorMessage(lastError));
				}
				return false;
			}

			if (lastError != ERROR_PIPE_BUSY && lastError != ERROR_FILE_NOT_FOUND)
			{
				AssignError(errorMessage, L"连接 SecRandom IPC 通道失败: " + FormatWindowsErrorMessage(lastError));
				return false;
			}

			const auto now = std::chrono::steady_clock::now();
			const auto remaining = deadline > now ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() : 0;
			DWORD waitSlice = static_cast<DWORD>(remaining > 1000 ? 1000 : remaining);
			if (waitSlice == 0) waitSlice = 1;

			if (!WaitNamedPipeW(pipePath.c_str(), waitSlice))
			{
				DWORD waitError = GetLastError();
				if (waitError != ERROR_SEM_TIMEOUT && waitError != ERROR_FILE_NOT_FOUND)
				{
					AssignError(errorMessage, L"等待 SecRandom IPC 管道就绪失败: " + FormatWindowsErrorMessage(waitError));
					return false;
				}
			}
		}
	}

	bool AuthenticatePipe(HANDLE pipeHandle, std::wstring_view ipcName, std::wstring* errorMessage)
	{
		const std::string authKey = utf16ToUtf8(std::wstring(ipcName));

		std::string serverChallenge;
		if (!ReceiveFramedBytes(pipeHandle, serverChallenge, 256, errorMessage)) return false;
		if (serverChallenge.size() < kChallengePrefix.size() || serverChallenge.compare(0, kChallengePrefix.size(), kChallengePrefix) != 0)
		{
			AssignError(errorMessage, L"SecRandom IPC 认证失败: 服务端没有发送有效的挑战帧。");
			return false;
		}

		const std::string serverRandom = serverChallenge.substr(kChallengePrefix.size());
		std::string serverDigest;
		if (!ComputeHmacMd5(authKey, serverRandom, serverDigest, errorMessage)) return false;
		if (!SendFramedBytes(pipeHandle, serverDigest, errorMessage)) return false;

		std::string welcome;
		if (!ReceiveFramedBytes(pipeHandle, welcome, 256, errorMessage)) return false;
		if (welcome != kWelcomeMessage)
		{
			AssignError(errorMessage, L"SecRandom IPC 认证失败: 服务端拒绝了客户端摘要。");
			return false;
		}

		std::string clientRandom(kChallengeRandomLength, '\0');
		if (!FillRandomBytes(clientRandom, errorMessage)) return false;

		std::string clientChallenge(kChallengePrefix);
		clientChallenge += clientRandom;
		if (!SendFramedBytes(pipeHandle, clientChallenge, errorMessage)) return false;

		std::string expectedDigest;
		if (!ComputeHmacMd5(authKey, clientRandom, expectedDigest, errorMessage)) return false;

		std::string serverReply;
		if (!ReceiveFramedBytes(pipeHandle, serverReply, 256, errorMessage)) return false;

		if (serverReply != expectedDigest)
		{
			SendFramedBytes(pipeHandle, std::string(kFailureMessage), nullptr);
			AssignError(errorMessage, L"SecRandom IPC 认证失败: 服务端摘要校验不通过。");
			return false;
		}

		if (!SendFramedBytes(pipeHandle, std::string(kWelcomeMessage), errorMessage)) return false;

		return true;
	}

	bool ParseJson(const std::string& input, Json::Value& root, std::wstring* errorMessage)
	{
		Json::CharReaderBuilder builder;
		builder["collectComments"] = false;

		std::istringstream stream(input);
		std::string parseErrors;
		if (!Json::parseFromStream(builder, stream, &root, &parseErrors))
		{
			AssignError(errorMessage, L"SecRandom IPC 返回了无法解析的 JSON: " + utf8ToUtf16(parseErrors));
			return false;
		}

		return true;
	}

	std::wstring BuildResponseFailureMessage(const Json::Value& response)
	{
		if (response.isMember("detail") && response["detail"].isString())
		{
			return L"SecRandom IPC 调用失败: " + utf8ToUtf16(response["detail"].asString());
		}
		if (response.isMember("error") && response["error"].isString())
		{
			return L"SecRandom IPC 调用失败: " + utf8ToUtf16(response["error"].asString());
		}

		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		return L"SecRandom IPC 调用失败: " + utf8ToUtf16(Json::writeString(writer, response));
	}
}

export namespace Inkeys::SecRandom
{
	bool SendUrl(std::wstring_view url, std::wstring* errorMessage = nullptr, DWORD timeoutMs = 5000)
	{
		UniqueHandle pipeHandle;
		if (!ConnectPipe(kDefaultIpcName, timeoutMs, pipeHandle, errorMessage)) return false;
		if (!AuthenticatePipe(pipeHandle.Get(), kDefaultIpcName, errorMessage)) return false;

		Json::Value message(Json::objectValue);
		message["type"] = Json::Value("url");
		message["payload"]["url"] = Json::Value(utf16ToUtf8(url));

		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		const std::string requestJson = Json::writeString(writer, message);

		if (!SendFramedBytes(pipeHandle.Get(), requestJson, errorMessage)) return false;

		std::string responseJson;
		if (!ReceiveFramedBytes(pipeHandle.Get(), responseJson, 64 * 1024, errorMessage)) return false;

		Json::Value response;
		if (!ParseJson(responseJson, response, errorMessage)) return false;

		if (!response.isMember("success") || !response["success"].isBool())
		{
			AssignError(errorMessage, L"SecRandom IPC 返回缺少 success 字段的响应。");
			return false;
		}

		if (!response["success"].asBool())
		{
			AssignError(errorMessage, BuildResponseFailureMessage(response));
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
