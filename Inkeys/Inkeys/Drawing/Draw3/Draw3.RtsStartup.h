#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <windows.h>
#include "../../Helper/FailedCleanupDeadline.h"

namespace Inkeys::Drawing::Draw3
{
	enum class InputPreference : std::uint8_t { Automatic, RealTimeStylus, Pointer, MouseCompatibility };

	enum class HostInputMode : std::uint8_t
	{
		Disabled, RtsExtended, RtsBasicXY, Win32Mouse, Pointer
	};

	enum class InputFallback : std::uint8_t
	{
		None = 0, PointerUnavailableOnOs = 1, PointerInitFailed = 2,
		RtsExtendedFailed = 4, RtsFailed = 8, MouseFailed = 16
	};

	constexpr const char* InputPreferenceName(InputPreference value) noexcept
	{
		switch (value)
		{
		case InputPreference::Automatic: return "Automatic";
		case InputPreference::RealTimeStylus: return "RealTimeStylus";
		case InputPreference::Pointer: return "Pointer";
		case InputPreference::MouseCompatibility: return "MouseCompatibility";
		default: return "Unknown";
		}
	}

	constexpr bool HasInputFallback(InputFallback reasons, InputFallback reason) noexcept
	{
		return (static_cast<unsigned>(reasons) & static_cast<unsigned>(reason)) != 0;
	}

	constexpr bool SupportsPointerInput(int major, int minor) noexcept
	{
		return major > 6 || (major == 6 && minor >= 2);
	}

	constexpr InputPreference ResolveInputBackend(InputPreference preference, bool largeScreen,
		int major, int minor, int build) noexcept
	{
		switch (preference)
		{
		case InputPreference::RealTimeStylus:
		case InputPreference::MouseCompatibility:
			return preference;
		case InputPreference::Pointer:
			// 手动选择直接尝试；系统不支持 WM_POINTER 时按回退链从 RTS 开始。
			return SupportsPointerInput(major, minor) ? preference : InputPreference::RealTimeStylus;
		default: break;
		}
		// 大屏触控模式只约束自动选择，手动选择优先。
		if (largeScreen) return InputPreference::RealTimeStylus;
		return (major == 6 && minor == 3) || (major == 10 && minor == 0 && build < 22000)
			? InputPreference::Pointer : InputPreference::RealTimeStylus;
	}

	constexpr bool IsRtsInput(HostInputMode actual) noexcept
	{
		return actual == HostInputMode::RtsExtended || actual == HostInputMode::RtsBasicXY;
	}

	enum class TouchpadEntry : std::uint8_t { None, DirectManipulation, WindowRegistration };
	enum class TouchpadAvailability : std::uint8_t
	{
		BackendDisabled, OsTooOld, RtsOnLegacyOs, RegistrationUnavailable, InitFailed, Ok
	};

	constexpr TouchpadEntry SelectTouchpadEntry(int major, int minor, int build, HostInputMode actual) noexcept
	{
		if (major < 6 || (major == 6 && minor < 3)) return TouchpadEntry::None;
		if (actual == HostInputMode::Pointer) return TouchpadEntry::DirectManipulation;
		if (IsRtsInput(actual) && (major > 10 || (major == 10 && build >= 22000)))
			return TouchpadEntry::WindowRegistration;
		return TouchpadEntry::None;
	}

	constexpr bool ShowWhiteboardRtsNotice(int major, int build, HostInputMode actual) noexcept
	{
		// Host 初始化完成前只发布 Disabled；警告只看最终启用的生产者。
		return IsRtsInput(actual) && major > 0 && (major < 10 || (major == 10 && build < 22000));
	}

	// 每个失败回调必须先证明对应生产者已停产，才能进入下一项。
	// 先试解析结果；失败后按 RTS（扩展→基础 XY，由 rts 回调内部完成）→ WM_POINTER → 鼠标兼容 回退，
	// 跳过已尝试项；系统不支持 WM_POINTER 时不调用 pointer。
	template<class TryPointer, class TryRts, class TryMouse>
	HostInputMode StartInputBackend(InputPreference resolved, bool pointerSupported,
		TryPointer&& pointer, TryRts&& rts, TryMouse&& mouse)
	{
		const auto attempt = [&](InputPreference backend) -> HostInputMode
		{
			switch (backend)
			{
			case InputPreference::RealTimeStylus: return rts();
			case InputPreference::Pointer:
				return pointerSupported && pointer() ? HostInputMode::Pointer : HostInputMode::Disabled;
			case InputPreference::MouseCompatibility:
				return mouse() ? HostInputMode::Win32Mouse : HostInputMode::Disabled;
			default: return HostInputMode::Disabled;
			}
		};
		if (const auto mode = attempt(resolved); mode != HostInputMode::Disabled) return mode;
		for (const auto backend : { InputPreference::RealTimeStylus, InputPreference::Pointer,
			InputPreference::MouseCompatibility })
		{
			if (backend == resolved) continue;
			if (const auto mode = attempt(backend); mode != HostInputMode::Disabled) return mode;
		}
		return HostInputMode::Disabled;
	}

	template<class TryPointer, class TryRts, class TryMouse, class StartNavigation>
	HostInputMode StartInputAndNavigation(int major, int minor, int build, TouchpadAvailability& availability,
		InputPreference resolved, TryPointer pointer, TryRts rts, TryMouse mouse, StartNavigation start)
	{
		// 所有回退均已结束，只有实际就绪的生产者可以触发窗口 owner 初始化。
		const auto actual = StartInputBackend(resolved, SupportsPointerInput(major, minor), pointer, rts, mouse);
		const auto entry = SelectTouchpadEntry(major, minor, build, actual);
		if (entry != TouchpadEntry::None) availability = start(entry);
		else if (major < 6 || (major == 6 && minor < 3)) availability = TouchpadAvailability::OsTooOld;
		else if (IsRtsInput(actual)) availability = TouchpadAvailability::RtsOnLegacyOs;
		else availability = TouchpadAvailability::BackendDisabled;
		return actual;
	}
}

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

namespace Inkeys::Drawing::Draw3::RtsStartup
{
	struct StylusAngles
	{
		float tilt = -1.0f;
		float orientation = -1.0f;
	};

	// 纯换算放在头文件，生产 RTS 解码与无窗口测试共用同一份公式。
	inline float WrapStylusOrientation(float angle) noexcept
	{
		constexpr float twoPi = 3.14159265358979323846f * 2.0f;
		angle = std::fmod(angle, twoPi);
		if (angle < 0.0f) angle += twoPi;
		// 极小负角加 2π 后 float 舍入可能恰为 2π，超出模型的 [0, 2π) 约定。
		return angle >= twoPi ? 0.0f : angle;
	}

	inline StylusAngles DecodeStylusAngles(bool hasAzimuthAltitude,
		float azimuth, float altitude, float xTilt, float yTilt) noexcept
	{
		constexpr float pi = 3.14159265358979323846f;
		constexpr float halfPi = pi * 0.5f;
		constexpr float tiltLimit = halfPi - 0.0001f;
		StylusAngles result;
		if (hasAzimuthAltitude && std::isfinite(azimuth) && std::isfinite(altitude))
		{
			// RTS 倒置笔的仰角为负，倾斜幅度取仰角的绝对值再求余角。
			result.tilt = halfPi - std::clamp(std::abs(altitude), 0.0f, halfPi);
			result.orientation = WrapStylusOrientation(pi * 2.0f - azimuth);
			return result;
		}
		if (!std::isfinite(xTilt) || !std::isfinite(yTilt)) return result;
		const float xProjection = std::tan(std::clamp(xTilt, -tiltLimit, tiltLimit));
		const float yProjection = std::tan(std::clamp(yTilt, -tiltLimit, tiltLimit));
		result.tilt = std::clamp(std::atan(std::hypot(xProjection, yProjection)), 0.0f, halfPi);
		// RTS 正 Y 倾角朝屏幕上方，对应模型从 +X 逆时针转向 +π/2。
		result.orientation = WrapStylusOrientation(std::atan2(yProjection, xProjection));
		return result;
	}
}
