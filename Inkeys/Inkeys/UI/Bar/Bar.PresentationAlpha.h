#pragma once

#include <cstdint>

namespace Inkeys::UI::Bar
{
	struct PresentationAlphaAttempt final
	{
		std::uint8_t alpha = 255;
		std::uint64_t revision = 0;
		bool required = false;
		bool fullWindow = false;
	};

	class PresentationAlphaState final
	{
	public:
		explicit constexpr PresentationAlphaState(bool previewActive = false) noexcept
			: requestedAlpha_(previewActive ? 0 : 255),
			attemptedAlpha_(255), committedAlpha_(255),
			requestedRevision_(previewActive ? 1 : 0)
		{
		}

		[[nodiscard]] constexpr bool Request(std::uint8_t alpha) noexcept
		{
			// 已有 demand/retry 时重复请求保持幂等，不制造新的 revision。
			if (requestedAlpha_ == alpha) return false;
			requestedAlpha_ = alpha;
			++requestedRevision_;
			return true;
		}

		[[nodiscard]] constexpr PresentationAlphaAttempt BeginAttempt() noexcept
		{
			const bool required = retryRequired_ || requestedAlpha_ != committedAlpha_;
			attemptInFlight_ = required;
			if (required)
			{
				attemptedAlpha_ = requestedAlpha_;
				attemptedRevision_ = requestedRevision_;
			}
			return { attemptedAlpha_, attemptedRevision_, required, required };
		}

		constexpr void CompleteAttempt(bool committed) noexcept
		{
			// 业务 present 不等于 alpha attempt；没有 demand 时不得制造 alpha retry。
			if (!attemptInFlight_) return;
			attemptInFlight_ = false;
			if (!committed)
			{
				// 任一 present 阶段失败都保留旧 committed alpha，并强制整窗重试。
				retryRequired_ = true;
				return;
			}
			committedAlpha_ = attemptedAlpha_;
			committedRevision_ = attemptedRevision_;
			retryRequired_ = requestedAlpha_ != committedAlpha_
				|| requestedRevision_ != committedRevision_;
		}

		[[nodiscard]] constexpr std::uint8_t RequestedAlpha() const noexcept
		{
			return requestedAlpha_;
		}

		[[nodiscard]] constexpr std::uint8_t AttemptedAlpha() const noexcept
		{
			return attemptedAlpha_;
		}

		[[nodiscard]] constexpr std::uint8_t CommittedAlpha() const noexcept
		{
			return committedAlpha_;
		}

		[[nodiscard]] constexpr bool HasDemand() const noexcept
		{
			return retryRequired_ || requestedAlpha_ != committedAlpha_;
		}

		[[nodiscard]] constexpr bool NeedsFullWindowPresent() const noexcept
		{
			return HasDemand();
		}

		[[nodiscard]] constexpr std::uint64_t RequestedRevision() const noexcept
		{
			return requestedRevision_;
		}

		[[nodiscard]] constexpr std::uint64_t CommittedRevision() const noexcept
		{
			return committedRevision_;
		}

	private:
		std::uint8_t requestedAlpha_ = 255;
		std::uint8_t attemptedAlpha_ = 255;
		std::uint8_t committedAlpha_ = 255;
		std::uint64_t requestedRevision_ = 0;
		std::uint64_t attemptedRevision_ = 0;
		std::uint64_t committedRevision_ = 0;
		bool retryRequired_ = false;
		bool attemptInFlight_ = false;
	};
}
