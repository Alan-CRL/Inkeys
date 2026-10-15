#pragma once

#include <Windows.h>
#include <string>

namespace Json { class Value; }
namespace Inkeys::Drawing::Draw3 { struct PerformanceExportStatus; }

namespace Inkeys::Launch
{
	enum class PerformanceArgumentResult { None, Capture, Probe, LaunchProbe, Invalid };

	// 会话只在显式启用后读进程时钟/令牌及写盘；普通启动保留空状态。
	class PerformanceLaunch final
	{
	public:
		PerformanceLaunch() = default;
		~PerformanceLaunch();
		PerformanceLaunch(const PerformanceLaunch&) = delete;
		PerformanceLaunch& operator=(const PerformanceLaunch&) = delete;
		PerformanceArgumentResult ParseArguments(int count, wchar_t* const* arguments) noexcept;
		bool CreateConfiguredDirectory(const std::wstring& executableDirectory) noexcept;
		bool BeginFinalProcess(const std::wstring& executablePath,
			const std::wstring& version, const std::wstring& editionDate,
			const std::wstring& architecture) noexcept;
		bool Enabled() const noexcept { return !outputDirectory_.empty(); }
		bool OwnsFinalProcess() const noexcept { return finalOwner_; }
		const std::wstring& OutputDirectory() const noexcept { return outputDirectory_; }
		const char* ErrorReason() const noexcept { return errorReason_; }
		void NoteFailure(const char* reason, DWORD code = 0) noexcept;
		void SetLogFile(const std::wstring& path);
		void Disable() noexcept;
		bool WriteBeforeFinalFailure() noexcept;
		bool Complete(const Drawing::Draw3::PerformanceExportStatus& capture,
			int exitCode, bool normalShutdown, bool restarting) noexcept;

	private:
		Json::Value Identity(const char* status) const;
		bool ReadProcessIdentity() noexcept;
		std::wstring outputDirectory_, executablePath_, version_, editionDate_, architecture_;
		std::wstring processStartTime_, processStartFiletime_, logFile_;
		HANDLE ownerLock_ = INVALID_HANDLE_VALUE;
		const char* errorReason_ = "none";
		const char* failureReason_ = nullptr;
		DWORD failureCode_ = 0;
		DWORD processId_ = 0;
		bool finalOwner_ = false;
		bool completed_ = false;
		bool configured_ = false;
		bool uiAccessKnown_ = false;
		bool uiAccess_ = false;
	};

	int RunPerformanceLaunchProbe() noexcept;
}
