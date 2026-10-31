#pragma once

#include <atomic>
#include <cstdint>
#include <windows.h>
#include "../../Helper/FailedCleanupDeadline.h"

namespace Inkeys::Drawing::Draw3::RtsStartup
{
	enum class PacketProfile : std::uint8_t { Extended, BasicXY };
	enum class FailureStage : std::uint8_t
	{
		None, Com, Create, Hwnd, AllTablets, PacketDescription,
		InputConfiguration, PluginAllocation, PluginGate, Marshaler, AddPlugin, Enable
	};

	inline const char* ProfileName(PacketProfile profile) noexcept
	{
		return profile == PacketProfile::Extended ? "Extended" : "BasicXY";
	}

	inline const char* StageName(FailureStage stage) noexcept
	{
		switch (stage)
		{
		case FailureStage::Com: return "com";
		case FailureStage::Create: return "create";
		case FailureStage::Hwnd: return "hwnd";
		case FailureStage::AllTablets: return "all_tablets";
		case FailureStage::PacketDescription: return "packet_description";
		case FailureStage::InputConfiguration: return "input_configuration";
		case FailureStage::PluginAllocation: return "plugin_allocation";
		case FailureStage::PluginGate: return "plugin_gate";
		case FailureStage::Marshaler: return "marshaler";
		case FailureStage::AddPlugin: return "add_plugin";
		case FailureStage::Enable: return "enable";
		default: return "ready";
		}
	}

	struct DisableResult
	{
		HRESULT disableResult = E_PENDING;
		HRESULT enabledResult = E_PENDING;
		BOOL enabled = TRUE;
		bool StopProven() const noexcept
		{
			return SUCCEEDED(disableResult) || (SUCCEEDED(enabledResult) && enabled == FALSE);
		}
	};

	// Disable 失败不等于仍在采集；只接受成功读回的明确 FALSE，不按错误码猜状态。
	// 这份证明不能代替后续 Remove、在途 callback drain 和 COM 引用保活。
	template<class ReadEnabled>
	DisableResult VerifyDisabled(HRESULT disableResult, ReadEnabled&& readEnabled) noexcept
	{
		DisableResult result;
		result.disableResult = disableResult;
		if (FAILED(disableResult)) result.enabledResult = readEnabled(&result.enabled);
		return result;
	}

	struct AttemptResult
	{
		FailureStage stage = FailureStage::None;
		HRESULT result = S_OK;
		bool Ready() const noexcept { return SUCCEEDED(result); }
		bool CanRetryBasic() const noexcept
		{
			return stage == FailureStage::PacketDescription ||
				stage == FailureStage::InputConfiguration || stage == FailureStage::Enable;
		}
	};

	// tryAttempt 返回前必须已安全清理失败实例、取消并 join 其独立监督。
	// 产品和无窗口故障注入共享此有界编排，成功或公共前置失败不会重复创建。
	template<class TryAttempt>
	bool SelectPacketProfile(TryAttempt&& tryAttempt)
	{
		const auto extended = tryAttempt(PacketProfile::Extended);
		if (extended.Ready()) return true;
		if (!extended.CanRetryBasic()) return false;
		return tryAttempt(PacketProfile::BasicXY).Ready();
	}

	template<class Initialize, class Cleanup, class Diagnose>
	bool InitializeWithFallback(Shutdown::FailedCleanupSignal outer,
		Initialize&& initialize, Cleanup&& cleanup, Diagnose&& diagnose)
	{
		return SelectPacketProfile([&](PacketProfile profile)
		{
			Shutdown::FailedCleanupDeadline attemptCleanup(outer.Publisher());
			if (outer.HasPublisher()) attemptCleanup.PrepareOrFatal();
			const auto signal = attemptCleanup.Signal();
			AttemptResult outcome;
			try
			{
				outcome = initialize(profile, signal);
				if (!outcome.Ready())
				{
					signal.BeginKnownFailure();
					cleanup();
				}
				diagnose(profile, outcome);
			}
			catch (...)
			{
				// 未知异常终止整个 startup，外层继承原截止，不能再选下一 producer。
				outer.BeginKnownFailure(signal.BeginKnownFailure());
				cleanup();
				throw;
			}
			// 失败实例停产/排空、日志和监督真 join 都在下一普通初始化之前完成。
			attemptCleanup.CompleteOrFatal();
			return outcome;
		});
	}

	class ContactPublicationGate
	{
	public:
		class DownPermission
		{
		public:
			explicit DownPermission(bool allowed) noexcept : allowed_(allowed) {}
			explicit operator bool() const noexcept { return allowed_; }
			template<class Publish>
			bool PublishDown(Publish&& publish) const noexcept { return allowed_ && publish(); }
		private:
			bool allowed_;
		};
		DownPermission CaptureDown() const noexcept
		{
			return DownPermission(enabled_.load(std::memory_order_acquire));
		}
		void Enable() noexcept { enabled_.store(true, std::memory_order_release); }

	private:
		std::atomic<bool> enabled_ = false;
	};

	class CallbackGate
	{
		static constexpr std::uint32_t kClosed = UINT32_C(0x80000000);
		static constexpr std::uint32_t kCountMask = kClosed - 1u;

	public:
		CallbackGate() noexcept
		{
			drained_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!drained_) error_ = GetLastError();
		}
		~CallbackGate() { if (drained_) CloseHandle(drained_); }
		CallbackGate(const CallbackGate&) = delete;
		CallbackGate& operator=(const CallbackGate&) = delete;
		HRESULT ReadyResult() const noexcept
		{
			return drained_ ? S_OK : HRESULT_FROM_WIN32(error_ ? error_ : ERROR_GEN_FAILURE);
		}

		class Lease
		{
		public:
			explicit Lease(CallbackGate& gate) noexcept : gate_(gate), entered_(gate_.TryEnter()) {}
			~Lease() { if (entered_) gate_.Leave(); }
			Lease(const Lease&) = delete;
			Lease& operator=(const Lease&) = delete;
			explicit operator bool() const noexcept { return entered_; }
		private:
			CallbackGate& gate_;
			bool entered_;
		};

		void Close() noexcept
		{
			// 关闭位和在途数共用一个原子；Close 看到零以后不可能再准入新 callback。
			const auto previous = state_.fetch_or(kClosed, std::memory_order_acq_rel);
			if (previous == 0)
				SetEvent(drained_);
		}
		bool WaitUntilDrained(DWORD timeout = INFINITE) const noexcept
		{
			// 必须等最后 Leave 完成通知；仅观察计数归零会越过其 SetEvent 尾部。
			return drained_ && WaitForSingleObject(drained_, timeout) == WAIT_OBJECT_0 &&
				state_.load(std::memory_order_acquire) == kClosed;
		}

	private:
		bool TryEnter() noexcept
		{
			auto expected = state_.load(std::memory_order_acquire);
			while ((expected & kClosed) == 0 && (expected & kCountMask) != kCountMask)
			{
				if (state_.compare_exchange_weak(expected, expected + 1u,
					std::memory_order_acquire, std::memory_order_relaxed)) return true;
			}
			return false;
		}
		void Leave() noexcept
		{
			if (state_.fetch_sub(1u, std::memory_order_acq_rel) == (kClosed | 1u))
				SetEvent(drained_);
		}

		std::atomic<std::uint32_t> state_ = 0;
		HANDLE drained_ = nullptr;
		DWORD error_ = ERROR_SUCCESS;
	};
}
