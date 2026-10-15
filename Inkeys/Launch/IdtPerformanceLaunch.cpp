import Inkeys.Text.Split;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "IdtPerformanceLaunch.h"
#include "../Inkeys/Drawing/Draw3/Draw3.Performance.h"
#include <json/json.h>
#include <objbase.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

#pragma comment(lib, "Shell32.lib")

namespace Inkeys::Launch
{
	namespace
	{
		constexpr wchar_t kCaptureArgument[] = L"--draw3-performance";
		constexpr wchar_t kOwnerFile[] = L"\\.draw3-performance-owner.lock";

		std::string Utf8(const std::wstring& value)
		{
			if (value.empty()) return {};
			const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			if (!count) throw std::runtime_error("invalid UTF-16");
			std::string result(count, '\0');
			if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), result.data(), count, nullptr, nullptr))
				throw std::runtime_error("UTF-8 conversion failed");
			return result;
		}

		bool ValidAbsoluteDirectory(const std::wstring& path) noexcept
		{
			// 旧 SuperTop 使用 * / ? 括定参数，路径不能含这些字符或末尾反斜杠。
			if (path.empty() || path.size() >= MAX_PATH || path.back() == L'\\'
				|| path.back() == L'/' || path.find_first_of(L"*?\"<>|") != path.npos)
				return false;
			const bool drive = path.size() > 3 && ((path[0] >= L'A' && path[0] <= L'Z')
				|| (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && path[2] == L'\\';
			const bool unc = path.size() > 2 && path[0] == L'\\' && path[1] == L'\\';
			if (!drive && !unc) return false;
			if (path.find(L':', drive ? 2 : 0) != path.npos || path.find(L'/') != path.npos)
				return false;
			size_t offset = drive ? 3 : 2, components = 0;
			while (offset < path.size())
			{
				const size_t end = path.find(L'\\', offset);
				const size_t length = (end == path.npos ? path.size() : end) - offset;
				if (!length || path[offset + length - 1] == L'.'
					|| path[offset + length - 1] == L' ') return false;
				for (size_t index = offset; index < offset + length; ++index)
					if (path[index] < 32) return false;
				++components;
				if (end == path.npos) break;
				offset = end + 1;
			}
			return drive || components >= 3; // UNC 必须包含 server/share/session。
		}

		bool UniqueExistingDirectory(const std::wstring& path)
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
			for (const wchar_t* name : { kOwnerFile, L"\\launch.json", L"\\completion.json" })
				if (GetFileAttributesW((path + name).c_str()) != INVALID_FILE_ATTRIBUTES) return false;
			for (const wchar_t* pattern : { L"\\host-*", L"\\launch-failure-*.json" })
			{
				WIN32_FIND_DATAW found{};
				HANDLE search = FindFirstFileW((path + pattern).c_str(), &found);
				if (search != INVALID_HANDLE_VALUE) { FindClose(search); return false; }
			}
			return true;
		}

		bool WriteNewJson(const std::wstring& path, const Json::Value& value) noexcept
		{
			try
			{
				Json::StreamWriterBuilder builder;
				builder["indentation"] = "  ";
				const std::string bytes = Json::writeString(builder, value) + "\r\n";
				const std::wstring temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
				HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE) return false;
				DWORD written = 0;
				bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
					&written, nullptr) != FALSE && written == bytes.size() && FlushFileBuffers(file) != FALSE;
				CloseHandle(file);
				// 不覆盖已有握手；完整 JSON 落盘后原子发布，避免脚本读取半个文件。
				if (success) success = MoveFileExW(temporary.c_str(), path.c_str(),
					MOVEFILE_WRITE_THROUGH) != FALSE;
				if (!success) DeleteFileW(temporary.c_str());
				return success;
			}
			catch (...) { return false; }
		}

		std::wstring NewSessionName()
		{
			GUID guid{};
			if (FAILED(CoCreateGuid(&guid))) return {};
			wchar_t guidText[40]{};
			if (!StringFromGUID2(guid, guidText, ARRAYSIZE(guidText))) return {};
			SYSTEMTIME time{};
			GetSystemTime(&time);
			wchar_t prefix[64]{};
			swprintf_s(prefix, L"%04u%02u%02u-%02u%02u%02u-%lu-", time.wYear,
				time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
				static_cast<unsigned long>(GetCurrentProcessId()));
			return std::wstring(prefix) + std::wstring(guidText + 1, 36);
		}
	}

	PerformanceLaunch::~PerformanceLaunch()
	{
		if (ownerLock_ != INVALID_HANDLE_VALUE) CloseHandle(ownerLock_);
	}

	PerformanceArgumentResult PerformanceLaunch::ParseArguments(int count, wchar_t* const* arguments) noexcept
	{
		try
		{
			int captureIndex = -1;
			for (int index = 1; index < count; ++index)
			{
				const std::wstring_view argument(arguments[index]);
				constexpr int prefixLength = static_cast<int>(ARRAYSIZE(kCaptureArgument) - 1);
				if (argument.size() < prefixLength || CompareStringOrdinal(argument.data(),
					prefixLength, kCaptureArgument, prefixLength, TRUE) != CSTR_EQUAL) continue;
				if (argument == L"--draw3-performance-test" || argument == L"--draw3-performance-launch-test")
				{
					if (index == 1 && count == 2) return argument == L"--draw3-performance-test"
						? PerformanceArgumentResult::Probe : PerformanceArgumentResult::LaunchProbe;
					errorReason_ = "probe_requires_exact_arguments";
					return PerformanceArgumentResult::Invalid;
				}
				if (argument != kCaptureArgument || captureIndex != -1)
				{
					errorReason_ = "invalid_or_duplicate_performance_argument";
					return PerformanceArgumentResult::Invalid;
				}
				captureIndex = index;
			}
			if (captureIndex == -1) return PerformanceArgumentResult::None;
			if (captureIndex + 1 >= count || !ValidAbsoluteDirectory(arguments[captureIndex + 1]))
			{
				errorReason_ = "absolute_session_directory_required";
				return PerformanceArgumentResult::Invalid;
			}
			for (int index = 1; index < count; ++index)
			{
				if (index == captureIndex || index == captureIndex + 1) continue;
				const std::wstring_view argument(arguments[index]);
				if (argument != L"-SuperTopComplete" && argument != L"-WarnTry"
					&& argument != L"-CrashTry" && argument != L"-Restart")
				{
					errorReason_ = "unexpected_performance_argument";
					return PerformanceArgumentResult::Invalid;
				}
			}
			if (!UniqueExistingDirectory(arguments[captureIndex + 1]))
			{
				errorReason_ = "session_directory_missing_or_reused";
				return PerformanceArgumentResult::Invalid;
			}
			outputDirectory_ = arguments[captureIndex + 1];
			return PerformanceArgumentResult::Capture;
		}
		catch (...) { errorReason_ = "performance_argument_validation_failed"; return PerformanceArgumentResult::Invalid; }
	}

	bool PerformanceLaunch::CreateConfiguredDirectory(const std::wstring& executableDirectory) noexcept
	{
		if (Enabled()) return true; // 显式目录优先，不更改持久化性能开关。
		try
		{
			const auto root = std::filesystem::path(executableDirectory) / L"log" / L"Draw3Performance";
			std::error_code error;
			std::filesystem::create_directories(root, error);
			const std::wstring name = NewSessionName();
			const std::wstring directory = (root / name).wstring();
			if (error || name.empty() || !ValidAbsoluteDirectory(directory)
				|| !CreateDirectoryW(directory.c_str(), nullptr))
			{
				errorReason_ = "configured_session_directory_failed";
				return false;
			}
			outputDirectory_ = directory;
			configured_ = true;
			return true;
		}
		catch (...) { errorReason_ = "configured_session_directory_failed"; return false; }
	}

	bool PerformanceLaunch::ReadProcessIdentity() noexcept
	{
		try
		{
			FILETIME creation{}, exit{}, kernel{}, user{};
			if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return false;
			SYSTEMTIME time{};
			if (!FileTimeToSystemTime(&creation, &time)) return false;
			wchar_t text[40]{};
			swprintf_s(text, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear,
				time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
			processStartTime_ = text;
			processStartFiletime_ = std::to_wstring((static_cast<ULONGLONG>(creation.dwHighDateTime) << 32)
				| creation.dwLowDateTime);
			processId_ = GetCurrentProcessId();
			if (executablePath_.empty())
			{
				wchar_t module[MAX_PATH]{};
				const DWORD length = GetModuleFileNameW(nullptr, module, ARRAYSIZE(module));
				if (!length || length >= ARRAYSIZE(module)) return false;
				executablePath_.assign(module, length);
			}
			HANDLE token = nullptr;
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			{
				DWORD value = 0, bytes = 0;
				uiAccessKnown_ = GetTokenInformation(token, TokenUIAccess, &value,
					sizeof(value), &bytes) != FALSE;
				uiAccess_ = value != 0;
				CloseHandle(token);
			}
			return true;
		}
		catch (...) { return false; }
	}

	Json::Value PerformanceLaunch::Identity(const char* status) const
	{
		Json::Value value(Json::objectValue);
		value["schema_version"] = 1;
		value["session_id"] = Utf8(std::filesystem::path(outputDirectory_).filename().wstring());
		value["final_pid"] = static_cast<Json::UInt>(processId_);
		value["process_start_time"] = Utf8(processStartTime_);
		value["process_start_filetime_utc"] = Utf8(processStartFiletime_);
		value["status"] = status;
		value["output_directory"] = Utf8(outputDirectory_);
		value["exe_path"] = Utf8(executablePath_);
		value["software_version"] = Utf8(version_);
		value["edition_date"] = Utf8(editionDate_);
		value["process_architecture"] = Utf8(architecture_);
		value["source"] = configured_ ? "experimental_option" : "command_line";
		value["ui_access"] = uiAccessKnown_ ? Json::Value(uiAccess_) : Json::Value();
		return value;
	}

	bool PerformanceLaunch::BeginFinalProcess(const std::wstring& executablePath,
		const std::wstring& version, const std::wstring& editionDate,
		const std::wstring& architecture) noexcept
	{
		if (!Enabled()) return true;
		if (finalOwner_ || ownerLock_ != INVALID_HANDLE_VALUE)
		{
			errorReason_ = "session_already_owned";
			return false;
		}
		try
		{
			executablePath_ = executablePath;
			version_ = version;
			editionDate_ = editionDate;
			architecture_ = architecture;
			if (!ReadProcessIdentity()) { errorReason_ = "process_identity_failed"; return false; }
			ownerLock_ = CreateFileW((outputDirectory_ + kOwnerFile).c_str(), GENERIC_WRITE,
				FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (ownerLock_ == INVALID_HANDLE_VALUE) { errorReason_ = "session_owner_reservation_failed"; return false; }
			if (!WriteNewJson(outputDirectory_ + L"\\launch.json", Identity("started")))
			{
				errorReason_ = "launch_handshake_write_failed";
				return false;
			}
			finalOwner_ = true;
			return true;
		}
		catch (...) { errorReason_ = "launch_handshake_failed"; return false; }
	}

	void PerformanceLaunch::NoteFailure(const char* reason, DWORD code) noexcept
	{
		if (!Enabled() || failureReason_) return;
		failureReason_ = reason;
		failureCode_ = code;
	}

	void PerformanceLaunch::SetLogFile(const std::wstring& path)
	{
		if (Enabled()) logFile_ = path;
	}

	void PerformanceLaunch::Disable() noexcept
	{
		if (ownerLock_ != INVALID_HANDLE_VALUE) CloseHandle(ownerLock_);
		ownerLock_ = INVALID_HANDLE_VALUE;
		outputDirectory_.clear();
		finalOwner_ = false;
	}

	bool PerformanceLaunch::WriteBeforeFinalFailure() noexcept
	{
		if (!Enabled() || finalOwner_ || completed_) return false;
		completed_ = true;
		try
		{
			if (GetFileAttributesW((outputDirectory_ + L"\\launch.json").c_str()) != INVALID_FILE_ATTRIBUTES
				|| !ReadProcessIdentity()) return false;
			Json::Value value = Identity("failed_before_final");
			value.removeMember("final_pid");
			value["pid"] = static_cast<Json::UInt>(processId_);
			value["reason"] = failureReason_ ? failureReason_ : (std::strcmp(errorReason_, "none") == 0
				? "startup_stopped_before_final" : errorReason_);
			value["win32_error"] = static_cast<Json::UInt>(failureCode_);
			return WriteNewJson(outputDirectory_ + L"\\launch-failure-"
				+ std::to_wstring(processId_) + L".json", value);
		}
		catch (...) { return false; }
	}

	bool PerformanceLaunch::Complete(const Drawing::Draw3::PerformanceExportStatus& capture,
		int exitCode, bool normalShutdown, bool restarting) noexcept
	{
		if (!finalOwner_ || completed_) return false;
		completed_ = true;
		try
		{
			const bool exported = capture.enabled && capture.hostRuns > 0
				&& capture.exportedHostRuns == capture.hostRuns && capture.failedHostRuns == 0
				&& capture.unavailableHostRuns == 0;
			const bool dropped = capture.framesDropped > 0 || capture.inputsDropped > 0
				|| capture.receiptsDropped > 0 || capture.proofOverflow > 0;
			const bool failed = failureReason_ || !normalShutdown || exitCode != 0 || capture.runFailedHostRuns > 0;
			const char* status = failed ? "failed" : (exported && !dropped ? "completed" : "incomplete");
			const char* reason = failureReason_ ? failureReason_ : (!normalShutdown ? "startup_did_not_complete"
				: (exitCode != 0 || capture.runFailedHostRuns > 0 ? "runtime_failed"
					: (!exported ? "capture_or_export_unavailable" : (dropped ? "capacity_exhausted"
						: (restarting ? "restart_requested" : "normal_exit")))));
			Json::Value value = Identity(status);
			value["exit_code"] = exitCode;
			value["reason"] = reason;
			value["startup_failure_code"] = static_cast<Json::UInt>(failureCode_);
			value["normal_shutdown"] = normalShutdown;
			value["export_succeeded"] = exported;
			value["capture_complete"] = exported && !dropped;
			value["samples_dropped"] = dropped;
			// 基础采集完成与调用明细前缀覆盖分别报告，不能用前者冒充全程细项齐全。
			value["graphics_detail_complete"] = exported && capture.graphicsCallsDropped == 0
				&& capture.graphicsGroupCallsDropped == 0 && capture.resourcesDropped == 0
				&& capture.graphicsSiteCollisions == 0;
			value["log_file"] = Utf8(logFile_);
			Json::Value& result = value["export_status"];
			result["enabled"] = capture.enabled;
#define INKEYS_PERFORMANCE_STATUS(jsonName, member) result[jsonName] = static_cast<Json::UInt64>(capture.member)
			INKEYS_PERFORMANCE_STATUS("host_runs", hostRuns);
			INKEYS_PERFORMANCE_STATUS("exported_host_runs", exportedHostRuns);
			INKEYS_PERFORMANCE_STATUS("failed_host_runs", failedHostRuns);
			INKEYS_PERFORMANCE_STATUS("startup_failed_host_runs", startupFailedHostRuns);
			INKEYS_PERFORMANCE_STATUS("run_failed_host_runs", runFailedHostRuns);
			INKEYS_PERFORMANCE_STATUS("unavailable_host_runs", unavailableHostRuns);
			INKEYS_PERFORMANCE_STATUS("frames_seen", framesSeen);
			INKEYS_PERFORMANCE_STATUS("frames_retained", framesRetained);
			INKEYS_PERFORMANCE_STATUS("frames_dropped", framesDropped);
			INKEYS_PERFORMANCE_STATUS("inputs_seen", inputsSeen);
			INKEYS_PERFORMANCE_STATUS("inputs_retained", inputsRetained);
			INKEYS_PERFORMANCE_STATUS("inputs_dropped", inputsDropped);
			INKEYS_PERFORMANCE_STATUS("receipts_seen", receiptsSeen);
			INKEYS_PERFORMANCE_STATUS("receipts_retained", receiptsRetained);
			INKEYS_PERFORMANCE_STATUS("receipts_dropped", receiptsDropped);
			INKEYS_PERFORMANCE_STATUS("proof_overflow", proofOverflow);
			INKEYS_PERFORMANCE_STATUS("payload_budget_bytes", payloadBudgetBytes);
			INKEYS_PERFORMANCE_STATUS("graphics_calls_seen", graphicsCallsSeen);
			INKEYS_PERFORMANCE_STATUS("graphics_calls_retained", graphicsCallsRetained);
			INKEYS_PERFORMANCE_STATUS("graphics_calls_dropped", graphicsCallsDropped);
			INKEYS_PERFORMANCE_STATUS("slow_graphics_calls_seen", slowGraphicsCallsSeen);
			INKEYS_PERFORMANCE_STATUS("slow_graphics_calls_retained", slowGraphicsCallsRetained);
			INKEYS_PERFORMANCE_STATUS("slow_graphics_calls_dropped", slowGraphicsCallsDropped);
			INKEYS_PERFORMANCE_STATUS("resources_seen", resourcesSeen);
			INKEYS_PERFORMANCE_STATUS("resources_retained", resourcesRetained);
			INKEYS_PERFORMANCE_STATUS("resources_dropped", resourcesDropped);
			INKEYS_PERFORMANCE_STATUS("graphics_group_calls_dropped", graphicsGroupCallsDropped);
			INKEYS_PERFORMANCE_STATUS("graphics_site_collisions", graphicsSiteCollisions);
#undef INKEYS_PERFORMANCE_STATUS
			return WriteNewJson(outputDirectory_ + L"\\completion.json", value);
		}
		catch (...) { return false; }
	}

	int RunPerformanceLaunchProbe() noexcept
	{
		try
		{
			wchar_t temporary[MAX_PATH]{};
			if (!GetTempPathW(ARRAYSIZE(temporary), temporary)) return 1;
			const std::wstring root = std::wstring(temporary) + L"Inkeys Performance Probe " + NewSessionName();
			if (!CreateDirectoryW(root.c_str(), nullptr)) return 1;
			struct Cleanup final
			{
				std::wstring root;
				std::vector<std::wstring> directories;
				~Cleanup()
				{
					// 只删除本测试创建的确切文件/空目录，不递归移动或删除未知路径。
					for (const auto& directory : directories)
					{
						for (const wchar_t* name : { kOwnerFile, L"\\launch.json", L"\\completion.json" })
							DeleteFileW((directory + name).c_str());
						DeleteFileW((directory + L"\\launch-failure-" + std::to_wstring(GetCurrentProcessId()) + L".json").c_str());
						RemoveDirectoryW(directory.c_str());
					}
					DeleteFileW((root + L"\\blocked").c_str());
					RemoveDirectoryW(root.c_str());
				}
			} cleanup{ root, {} };
			const auto parse = [](PerformanceLaunch& launch, const std::wstring& command)
				{
					int count = 0;
					LPWSTR* arguments = CommandLineToArgvW(command.c_str(), &count);
					if (!arguments) return PerformanceArgumentResult::Invalid;
					const auto result = launch.ParseArguments(count, arguments);
					LocalFree(arguments);
					return result;
				};
			const auto read = [](const std::wstring& path)
				{
					Json::Value value;
					std::ifstream file{ std::filesystem::path(path), std::ios::binary };
					Json::CharReaderBuilder builder;
					std::string errors;
					if (!file || !Json::parseFromStream(builder, file, &value, &errors))
						throw std::runtime_error("probe JSON missing");
					return value;
				};
			PerformanceLaunch disabled;
			if (parse(disabled, L"Inkeys.exe") != PerformanceArgumentResult::None
				|| disabled.Enabled() || disabled.OwnsFinalProcess()) return 1;
			// 实验输出无法创建目录时允许放弃本次采集，保留普通产品启动能力。
			PerformanceLaunch optionalFailure;
			HANDLE blocked = CreateFileW((root + L"\\blocked").c_str(), GENERIC_WRITE,
				0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (blocked == INVALID_HANDLE_VALUE) return 1;
			CloseHandle(blocked);
			if (optionalFailure.CreateConfiguredDirectory(root + L"\\blocked")) return 1;
			optionalFailure.Disable();
			if (optionalFailure.Enabled() || !optionalFailure.BeginFinalProcess(L"", L"", L"", L"")) return 1;
			for (const auto* command : { L"Inkeys.exe --draw3-performance", L"Inkeys.exe --draw3-performance relative",
				L"Inkeys.exe --DRAW3-PERFORMANCE relative",
				L"Inkeys.exe --draw3-performance=bad", L"Inkeys.exe --draw3-performance-test extra",
				L"Inkeys.exe other --draw3-performance-launch-test" })
			{
				PerformanceLaunch invalid;
				if (parse(invalid, command) != PerformanceArgumentResult::Invalid || invalid.Enabled()) return 1;
			}
			for (unsigned test = 0; test < 14; ++test)
			{
				const std::wstring directory = root + L"\\诊断 空格-" + std::to_wstring(test);
				if (!CreateDirectoryW(directory.c_str(), nullptr)) return 1;
				cleanup.directories.push_back(directory);
				const std::wstring command = L"Inkeys.exe --draw3-performance \"" + directory + L"\"";
				PerformanceLaunch launch;
				if (parse(launch, command) != PerformanceArgumentResult::Capture) return 1;
				if (!launch.CreateConfiguredDirectory(root + L"\\unused")
					|| launch.OutputDirectory() != directory) return 1;
				for (const auto& extra : { std::wstring(L" extra"), std::wstring(L" --draw3-performance \"") + directory + L"\"" })
				{
					PerformanceLaunch invalid;
					if (parse(invalid, command + extra) != PerformanceArgumentResult::Invalid) return 1;
				}
				// 复用产品 Split 的两层括定格式，验证带空格/中文路径经 SuperTop 仍是同一 argv。
				const auto tokens = Inkeys::Split::Run(command, L'*');
				std::wstring forwarded;
				for (size_t index = 1; index < tokens.size(); ++index) forwarded += tokens[index] + L" ";
				const auto helper = Inkeys::Split::Run(L"-CmdLine=?" + forwarded + L"? -UiAccess=1", L'?');
				if (helper.size() != 2) return 1;
				const size_t first = helper[0].find(L'?'), last = helper[0].rfind(L'?');
				PerformanceLaunch finalArguments;
				if (parse(finalArguments, L"Inkeys.exe -SuperTopComplete " + helper[0].substr(first + 1, last - first - 1))
					!= PerformanceArgumentResult::Capture || finalArguments.OutputDirectory() != directory) return 1;
				if (test == 3)
				{
					launch.NoteFailure("already_running", ERROR_ALREADY_EXISTS);
					if (!launch.WriteBeforeFinalFailure()
						|| GetFileAttributesW((directory + L"\\completion.json").c_str()) != INVALID_FILE_ATTRIBUTES) return 1;
					const auto failure = read(directory + L"\\launch-failure-" + std::to_wstring(GetCurrentProcessId()) + L".json");
					if (failure["status"].asString() != "failed_before_final" || failure.isMember("final_pid")) return 1;
					PerformanceLaunch rejectedRetry;
					if (parse(rejectedRetry, command) != PerformanceArgumentResult::Invalid) return 1;
					continue;
				}
				if (!launch.BeginFinalProcess(L"", L"probe", L"probe", L"probe") || !launch.OwnsFinalProcess()) return 1;
				if (launch.BeginFinalProcess(L"", L"probe", L"probe", L"probe") || !launch.OwnsFinalProcess()) return 1;
				const auto identity = read(directory + L"\\launch.json");
				if (identity["final_pid"].asUInt() != GetCurrentProcessId() || identity["exe_path"].asString().empty()
					|| identity["process_start_filetime_utc"].asString().empty()) return 1;
				if (finalArguments.BeginFinalProcess(L"", L"probe", L"probe", L"probe")) return 1;
				PerformanceLaunch reused;
				if (parse(reused, command) != PerformanceArgumentResult::Invalid) return 1;
				Drawing::Draw3::PerformanceExportStatus capture{};
				capture.enabled = true;
				capture.hostRuns = capture.exportedHostRuns = 2;
				capture.startupFailedHostRuns = 1; // 首代 DComp 失败不等于导出失败。
				capture.framesSeen = capture.framesRetained = 10;
				capture.graphicsCallsSeen = capture.graphicsCallsRetained = 10;
				capture.slowGraphicsCallsSeen = capture.slowGraphicsCallsRetained = 2;
				capture.resourcesSeen = capture.resourcesRetained = 2;
				if (test == 1) { capture.framesSeen = 11; capture.framesDropped = 1; }
				if (test == 2) { capture.failedHostRuns = 1; capture.exportedHostRuns = 1; }
				if (test == 4) { capture.receiptsSeen = 2; capture.receiptsRetained = 1; capture.receiptsDropped = 1; }
				if (test == 5) capture.proofOverflow = 1;
				if (test == 6) capture.runFailedHostRuns = 1;
				if (test == 7) launch.NoteFailure("startup_failed", 0xD201u);
				if (test == 8) { capture.inputsSeen = 2; capture.inputsRetained = 1; capture.inputsDropped = 1; }
				if (test == 9) { capture.graphicsCallsSeen = 11; capture.graphicsCallsDropped = 1; }
				if (test == 10) { capture.slowGraphicsCallsSeen = 3; capture.slowGraphicsCallsDropped = 1; }
				if (test == 11) { capture.resourcesSeen = 3; capture.resourcesDropped = 1; }
				if (test == 12) capture.graphicsGroupCallsDropped = 1;
				if (test == 13) capture.graphicsSiteCollisions = 1;
				if (!launch.Complete(capture, 0, test != 7, false) || launch.Complete(capture, 0, true, false)) return 1;
				const auto completion = read(directory + L"\\completion.json");
				const char* expected = test == 0 || test >= 9 ? "completed" : (test == 6 || test == 7 ? "failed" : "incomplete");
				if (completion["status"].asString() != expected
					|| completion["process_start_filetime_utc"] != identity["process_start_filetime_utc"]
					|| completion["exe_path"] != identity["exe_path"]
					|| completion["capture_complete"].asBool() != (test == 0 || test == 6 || test == 7 || test >= 9)
					|| completion["graphics_detail_complete"].asBool() != (test != 2 && test != 9 && test != 11 && test != 12 && test != 13)
					|| completion["export_status"]["graphics_calls_seen"].asUInt64() != capture.graphicsCallsSeen
					|| completion["export_status"]["slow_graphics_calls_dropped"].asUInt64() != capture.slowGraphicsCallsDropped
					|| completion["export_status"]["graphics_site_collisions"].asUInt64() != capture.graphicsSiteCollisions
					|| completion["exit_code"].asInt() != 0) return 1;
			}
			std::fputs("[Draw3PerformanceLaunch] PASS: exact argv, SuperTop forwarding, exclusive identity and truthful completion\n", stderr);
			return 0;
		}
		catch (...) { std::fputs("[Draw3PerformanceLaunch] FAIL: exception\n", stderr); return 1; }
	}
}
