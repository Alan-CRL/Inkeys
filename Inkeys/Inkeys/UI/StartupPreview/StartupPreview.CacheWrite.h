#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace Inkeys::UI::StartupPreview::CacheWrite
{
	[[nodiscard]] inline bool EnsureParentDirectory(
		const std::wstring& targetPath) noexcept
	{
		try
		{
			const std::filesystem::path parent =
				std::filesystem::path(targetPath).parent_path();
			if (parent.empty()) return true;
			std::error_code error;
			if (std::filesystem::is_directory(parent, error)) return true;
			error.clear();
			return std::filesystem::create_directories(parent, error)
				|| (!error && std::filesystem::is_directory(parent, error));
		}
		catch (...)
		{
			return false;
		}
	}

	class LatestRevisionPolicy final
	{
	public:
		[[nodiscard]] bool Accept(std::uint64_t revision) noexcept
		{
			if (revision < latestRevision_) return false;
			latestRevision_ = revision;
			return true;
		}

		[[nodiscard]] bool IsLatest(std::uint64_t revision) const noexcept
		{
			return revision == latestRevision_;
		}

		[[nodiscard]] std::uint64_t Latest() const noexcept
		{
			return latestRevision_;
		}

	private:
		std::uint64_t latestRevision_ = 0;
	};

	// 文件系统步骤由同一事务串联，任一步失败都关闭并清理自己的临时文件。
	template <typename Operations>
	[[nodiscard]] bool ExecuteDurableTransaction(Operations& operations) noexcept
	{
		try
		{
			if (!operations.Prepare() || !operations.EnsureParent()
				|| !operations.CreateTemporary())
			{
				operations.Close();
				operations.Cleanup();
				return false;
			}
			const bool written = operations.WriteAll();
			const bool flushed = written && operations.Flush();
			operations.Close();
			if (!written || !flushed || !operations.IsLatest()
				|| !operations.Replace())
			{
				operations.Cleanup();
				return false;
			}
			return true;
		}
		catch (...)
		{
			operations.Close();
			operations.Cleanup();
			return false;
		}
	}
}
