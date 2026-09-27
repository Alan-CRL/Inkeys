#pragma once

#include <windows.h>
#include <json/json.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace Inkeys::PptSettings
{
	struct Positions
	{
		float bottomX = 0.0F;
		float bottomY = 0.0F;
		float middleX = 0.0F;
		float middleY = 0.0F;
	};

	inline Positions ReadPositions(const Json::Value& value)
	{
		auto Read = [&](const char* name)
		{
			const auto& number = value[name];
			const float result = number.isNumeric() ? number.asFloat() : 0.0F;
			return std::isfinite(result) ? result : 0.0F;
		};
		return { Read("BottomBothWidth"), Read("BottomBothHeight"),
			Read("MiddleBothWidth"), Read("MiddleBothHeight") };
	}

	inline void SetPositions(Json::Value& value, Positions positions)
	{
		value["BottomBothWidth"] = positions.bottomX;
		value["BottomBothHeight"] = positions.bottomY;
		value["MiddleBothWidth"] = positions.middleX;
		value["MiddleBothHeight"] = positions.middleY;
	}

	// 两个业务队列共用此日志，外层 mutex 串行捕获和实际提交；版本仅在队列内流转。
	class WriteJournal
	{
	public:
		void Initialize(Json::Value settings)
		{
			settings_ = std::move(settings);
			desired_ = saved_ = ReadPositions(settings_);
			initialized_ = true;
		}
		bool Initialized() const noexcept { return initialized_; }
		Positions Saved() const noexcept { return saved_; }
		// 本进程快速重入恢复明确保存边沿的冻结值；这不表示磁盘已提交。
		Positions RestoreBaseline() const noexcept { return desired_; }
		std::uint64_t SavedRevision() const noexcept { return savedRevision_; }

		std::string CaptureSettings(Json::Value settings)
		{
			settings_ = std::move(settings);
			return Capture();
		}
		std::string CapturePositions(Positions positions, bool remember)
		{
			desired_ = positions;
			settings_["MemoryWidgetPosition"] = remember;
			return Capture();
		}
		// 投递重试不代表写盘成功；仅 Complete 根据实际结果更新失败状态。
		std::string Retry() const { return failed_ ? pending_ : std::string{}; }

		struct PreparedWrite
		{
			std::uint64_t revision = 0;
			Positions positions;
			std::string content;
		};

		bool Prepare(const std::string& payload, PreparedWrite& prepared) const
		{
			std::istringstream input(payload);
			Json::CharReaderBuilder reader;
			Json::Value value;
			std::string error;
			if (!Json::parseFromStream(reader, input, &value, &error)
				|| !value["_InkeysWriteRevision"].isUInt64()) return false;
			const auto revision = value["_InkeysWriteRevision"].asUInt64();
			if (revision != revision_ || revision <= savedRevision_) return true;
			value.removeMember("_InkeysWriteRevision");
			Json::StreamWriterBuilder format;
			prepared = { revision, ReadPositions(value),
				std::string("\xEF\xBB\xBF") + Json::writeString(format, value) };
			return true;
		}

		void Complete(const PreparedWrite& prepared, bool succeeded) noexcept
		{
			if (prepared.revision == 0) return;
			if (succeeded && prepared.revision > savedRevision_)
			{
				saved_ = prepared.positions;
				savedRevision_ = prepared.revision;
			}
			if (prepared.revision != revision_) return;
			failed_ = !succeeded;
			if (succeeded) pending_.clear();
		}

		template<class Writer> bool Commit(const std::string& payload, Writer&& writer)
		{
			PreparedWrite prepared;
			if (!Prepare(payload, prepared)) return false;
			if (prepared.revision == 0) return true;
			const bool succeeded = writer(prepared.content);
			Complete(prepared, succeeded);
			return succeeded;
		}

	private:
		std::string Capture()
		{
			SetPositions(settings_, desired_);
			Json::Value payload = settings_;
			payload["_InkeysWriteRevision"] = Json::UInt64(++revision_);
			Json::StreamWriterBuilder format;
			pending_ = Json::writeString(format, payload);
			failed_ = false;
			return pending_;
		}
		Json::Value settings_;
		Positions desired_;
		Positions saved_;
		std::uint64_t revision_ = 0;
		std::uint64_t savedRevision_ = 0;
		std::string pending_;
		bool initialized_ = false;
		bool failed_ = false;
	};

	inline bool WriteAtomically(const std::filesystem::path& path,
		const std::string& content) noexcept
	{
		try
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error) return false;
			const std::wstring temporary = path.wstring() + L".tmp."
				+ std::to_wstring(GetCurrentProcessId());
			const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0,
				nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool writtenOk = content.size() <= MAXDWORD
				&& WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
					&written, nullptr) && written == content.size()
				&& FlushFileBuffers(file);
			const bool closed = CloseHandle(file) != FALSE;
			// 原文件只在完整写入/flush 后替换，失败不破坏最后成功的位置。
			const bool committed = writtenOk && closed && MoveFileExW(
				temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			if (!committed) (void)DeleteFileW(temporary.c_str());
			return committed;
		}
		catch (...) { return false; }
	}
}
