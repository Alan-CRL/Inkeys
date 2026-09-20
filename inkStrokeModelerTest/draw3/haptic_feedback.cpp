module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <hstring.h>
#include <inspectable.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

module draw3.haptic_feedback;

namespace draw3
{
	namespace
	{
		constexpr int64_t kDiscreteDurationTicks = 22LL * 10000LL;
		constexpr int kMaximumCachedFeedbackCount = 64;
		constexpr bool kHapticDebugLoggingEnabled = false;
		constexpr uint64_t kHapticDebugLogIntervalMs = 1000;
		constexpr int kRoInitMultiThreaded = 1;
		constexpr int32_t kHapticDeviceTypePen = 2;

		HMODULE LoadSystemLibrary(const wchar_t* fileName) noexcept
		{
			if (!fileName || fileName[0] == L'\0') return nullptr;
			wchar_t path[MAX_PATH] = {};
			UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
			if (length == 0 || length >= ARRAYSIZE(path)) return nullptr;
			if (path[length - 1] != L'\\')
			{
				if (length + 1 >= ARRAYSIZE(path)) return nullptr;
				path[length++] = L'\\';
			}
			const size_t nameLength = std::wcslen(fileName);
			if (nameLength >= ARRAYSIZE(path) - length) return nullptr;
			std::wmemcpy(path + length, fileName, nameLength + 1);
			return LoadLibraryW(path); // combase 只允许从系统目录解析。
		}

		struct TimeSpanAbi
		{
			int64_t Duration = 0;
		};

		struct __declspec(uuid("3D577EF8-4CEE-11E6-B535-001BDC06AB3B"))
			ISimpleHapticsControllerFeedback : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE get_Waveform(uint16_t* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_Duration(TimeSpanAbi* value) = 0;
		};

		struct IVectorViewSimpleHapticsControllerFeedback : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE GetAt(uint32_t index,
				ISimpleHapticsControllerFeedback** value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_Size(uint32_t* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE IndexOf(ISimpleHapticsControllerFeedback* value,
				uint32_t* index, boolean* found) = 0;
			virtual HRESULT STDMETHODCALLTYPE GetMany(uint32_t startIndex, uint32_t capacity,
				ISimpleHapticsControllerFeedback** value, uint32_t* actual) = 0;
		};

		struct __declspec(uuid("3D577EF9-4CEE-11E6-B535-001BDC06AB3B"))
			ISimpleHapticsController : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE get_Id(HSTRING* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_SupportedFeedback(
				IVectorViewSimpleHapticsControllerFeedback** value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_IsIntensitySupported(boolean* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_IsPlayCountSupported(boolean* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_IsPlayDurationSupported(boolean* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_IsReplayPauseIntervalSupported(boolean* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE StopFeedback() = 0;
			virtual HRESULT STDMETHODCALLTYPE SendHapticFeedback(
				ISimpleHapticsControllerFeedback* feedback) = 0;
			virtual HRESULT STDMETHODCALLTYPE SendHapticFeedbackWithIntensity(
				ISimpleHapticsControllerFeedback* feedback, double intensity) = 0;
			virtual HRESULT STDMETHODCALLTYPE SendHapticFeedbackForDuration(
				ISimpleHapticsControllerFeedback* feedback, double intensity,
				TimeSpanAbi playDuration) = 0;
			virtual HRESULT STDMETHODCALLTYPE SendHapticFeedbackForPlayCount(
				ISimpleHapticsControllerFeedback* feedback, double intensity,
				int32_t playCount, TimeSpanAbi replayPauseInterval) = 0;
		};

		struct __declspec(uuid("31856EBA-A738-5A8C-B8F6-F97EF68D18EF"))
			IPenDevice : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE get_PenId(GUID* value) = 0;
		};

		struct __declspec(uuid("0207D327-7FB8-5566-8C34-F8342037B7F9"))
			IPenDevice2 : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE get_SimpleHapticsController(
				ISimpleHapticsController** value) = 0;
		};

		struct __declspec(uuid("9DFBBE01-0966-5180-BCB4-B85060E39479"))
			IPenDeviceStatics : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE GetFromPointerId(
				uint32_t pointerId, IPenDevice** result) = 0;
		};

		struct __declspec(uuid("040E91DF-BB3A-507C-9E25-A2D2C685B2E5"))
			IInputHapticsManager : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE get_ThreadId(uint32_t* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_CurrentHapticsControllerDeviceType(
				int32_t* value) = 0;
			virtual HRESULT STDMETHODCALLTYPE get_CurrentHapticsController(
				ISimpleHapticsController** value) = 0;
			virtual HRESULT STDMETHODCALLTYPE TrySendHapticWaveform(uint16_t waveform,
				uint16_t waveformFallback, boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE TrySendHapticWaveformWithIntensity(
				uint16_t waveform, uint16_t waveformFallback, double intensity,
				boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE TrySendHapticWaveformForDuration(
				uint16_t waveform, uint16_t waveformFallback, double intensity,
				TimeSpanAbi playDuration, boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE TrySendHapticWaveformForPlayCount(
				uint16_t waveform, uint16_t waveformFallback, double intensity,
				int32_t playCount, TimeSpanAbi replayPauseInterval, boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE TryStopFeedback(boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE SetOverrideHapticsController(int32_t deviceType,
				ISimpleHapticsController* controller, int64_t* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE ClearOverrideHapticsController(int64_t token) = 0;
		};

		struct __declspec(uuid("7BB40F77-E187-5322-844E-AA58223C281A"))
			IInputHapticsManagerStatics : IInspectable
		{
			virtual HRESULT STDMETHODCALLTYPE IsSupported(boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE IsHapticDevicePresent(boolean* result) = 0;
			virtual HRESULT STDMETHODCALLTYPE GetForCurrentThread(
				IInputHapticsManager** result) = 0;
			virtual HRESULT STDMETHODCALLTYPE TryGetForThread(uint32_t threadId,
				IInputHapticsManager** result) = 0;
		};

		using RoInitializeFunction = HRESULT(WINAPI*)(int);
		using RoUninitializeFunction = void(WINAPI*)();
		using RoGetActivationFactoryFunction = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
		using WindowsCreateStringFunction = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
		using WindowsDeleteStringFunction = HRESULT(WINAPI*)(HSTRING);

		struct WinRtApi
		{
			HMODULE combase = nullptr;
			RoInitializeFunction roInitialize = nullptr;
			RoUninitializeFunction roUninitialize = nullptr;
			RoGetActivationFactoryFunction roGetActivationFactory = nullptr;
			WindowsCreateStringFunction windowsCreateString = nullptr;
			WindowsDeleteStringFunction windowsDeleteString = nullptr;
			bool initialized = false;
			bool available = false;

			bool Load() noexcept
			{
				if (available) return true;
				if (!combase) combase = LoadSystemLibrary(L"combase.dll");
				if (!combase) return false;
				roInitialize = reinterpret_cast<RoInitializeFunction>(
					GetProcAddress(combase, "RoInitialize"));
				roUninitialize = reinterpret_cast<RoUninitializeFunction>(
					GetProcAddress(combase, "RoUninitialize"));
				roGetActivationFactory = reinterpret_cast<RoGetActivationFactoryFunction>(
					GetProcAddress(combase, "RoGetActivationFactory"));
				windowsCreateString = reinterpret_cast<WindowsCreateStringFunction>(
					GetProcAddress(combase, "WindowsCreateString"));
				windowsDeleteString = reinterpret_cast<WindowsDeleteStringFunction>(
					GetProcAddress(combase, "WindowsDeleteString"));
				available = roInitialize && roUninitialize && roGetActivationFactory &&
					windowsCreateString && windowsDeleteString;
				return available;
			}

			bool Initialize() noexcept
			{
				if (!Load()) return false;
				if (initialized) return true;
				const HRESULT result = roInitialize(kRoInitMultiThreaded);
				if (result == RPC_E_CHANGED_MODE) return true; // 线程已有 COM apartment 时仍尝试运行时激活。
				if (FAILED(result)) return false;
				initialized = true; // S_FALSE 也需要配对 RoUninitialize。
				return true;
			}

			void Shutdown() noexcept
			{
				if (initialized && roUninitialize) roUninitialize();
				initialized = false;
				if (combase) FreeLibrary(combase);
				combase = nullptr;
				available = false;
				roInitialize = nullptr;
				roUninitialize = nullptr;
				roGetActivationFactory = nullptr;
				windowsCreateString = nullptr;
				windowsDeleteString = nullptr;
			}

			HRESULT GetFactory(const wchar_t* className, REFIID iid, void** factory) noexcept
			{
				if (!factory) return E_POINTER;
				*factory = nullptr;
				if (!Initialize() || !className) return E_NOINTERFACE;
				HSTRING hstring = nullptr;
				const HRESULT stringResult = windowsCreateString(
					className, static_cast<UINT32>(std::wcslen(className)), &hstring);
				if (FAILED(stringResult)) return stringResult;
				const HRESULT result = roGetActivationFactory(hstring, iid, factory);
				windowsDeleteString(hstring);
				return result;
			}
		};

		struct FeedbackEntry
		{
			uint16_t waveform = 0;
			Microsoft::WRL::ComPtr<ISimpleHapticsControllerFeedback> feedback;
		};

		uint16_t ToWaveform(HapticContinuousFeedback feedback) noexcept
		{
			return static_cast<uint16_t>(feedback);
		}

		uint16_t ToWaveform(HapticDiscreteFeedback feedback) noexcept
		{
			return static_cast<uint16_t>(feedback);
		}

		uint64_t NowMilliseconds() noexcept
		{
			return GetTickCount64();
		}

		bool HasExplicitIntensity(double intensity) noexcept
		{
			return intensity >= 0.0 && intensity <= 1.0;
		}
	}

	HapticContinuousFeedback ResolveContinuousHapticFeedback(HapticToolFeedback tool) noexcept
	{
		switch (tool)
		{
		case HapticToolFeedback::Highlighter:
			return HapticContinuousFeedback::ChiselMarkerContinuous;
		case HapticToolFeedback::Eraser:
			return HapticContinuousFeedback::EraserContinuous;
		default:
			return HapticContinuousFeedback::InkContinuous;
		}
	}

	HapticContinuousFeedback FallbackContinuousHapticFeedback(
		HapticContinuousFeedback) noexcept
	{
		return HapticContinuousFeedback::InkContinuous;
	}

	HapticDiscreteFeedback FallbackDiscreteHapticFeedback(
		HapticDiscreteFeedback) noexcept
	{
		return HapticDiscreteFeedback::Click;
	}

	struct PenHapticFeedbackImpl
	{
		WinRtApi api;
		Microsoft::WRL::ComPtr<IPenDeviceStatics> penDeviceStatics;
		Microsoft::WRL::ComPtr<IInputHapticsManagerStatics> inputHapticsStatics;
		Microsoft::WRL::ComPtr<IInputHapticsManager> inputHapticsManager;
		Microsoft::WRL::ComPtr<ISimpleHapticsController> pointerController;
		std::vector<FeedbackEntry> pointerFeedback;
		uint32_t attachedPointerId = 0;
		uint32_t lastAttachAttemptPointerId = 0;
		uint64_t lastContinuousTickMs = 0;
		uint16_t activeContinuousWaveform = 0;
		uint16_t activeContinuousFallback = 0;
		bool enabled = kPenHapticFeedbackDefaultEnabled;
		bool initialized = false;
		bool continuousFeedbackStarted = false;
		bool inputHapticsSupported = false;
		bool pointerIntensitySupported = false;
		bool pointerPlayCountSupported = false;
		bool pointerDurationSupported = false;
		bool unavailableLogged = false;
		uint64_t lastDebugLogMs = 0;
		bool hasContinuousDebugState = false;
		bool lastContinuousSent = false;
		uint16_t lastContinuousWaveform = 0;
		uint16_t lastContinuousFallback = 0;
		uint64_t lastContinuousDebugLogMs = 0;
		bool hasInputManagerDeviceType = false;
		int32_t lastInputManagerDeviceType = 0;

		bool Initialize() noexcept
		{
			if (initialized) return IsAvailable();
			initialized = true;
			if (!api.Initialize())
			{
				LogUnavailableOnce("WinRT runtime is not available");
				return false;
			}

			void* factory = nullptr;
			HRESULT result = api.GetFactory(L"Windows.Devices.Input.PenDevice",
				__uuidof(IPenDeviceStatics), &factory);
			if (SUCCEEDED(result))
				penDeviceStatics.Attach(static_cast<IPenDeviceStatics*>(factory));

			factory = nullptr;
			result = api.GetFactory(L"Windows.Devices.Haptics.InputHapticsManager",
				__uuidof(IInputHapticsManagerStatics), &factory);
			if (SUCCEEDED(result))
			{
				inputHapticsStatics.Attach(
					static_cast<IInputHapticsManagerStatics*>(factory));
				boolean supported = false;
				if (SUCCEEDED(inputHapticsStatics->IsSupported(&supported)))
					inputHapticsSupported = supported != 0;
			}

			if (!IsAvailable())
				LogUnavailableOnce("no supported pen haptics API was found");
			else if constexpr (kHapticDebugLoggingEnabled)
				std::cout << "[Haptics] pen_device_api=" <<
					(penDeviceStatics ? "true" : "false") <<
					" input_manager_api=" <<
					(inputHapticsSupported ? "true" : "false") << std::endl;
			return IsAvailable();
		}

		void Shutdown() noexcept
		{
			StopFeedback();
			pointerFeedback.clear();
			pointerController.Reset();
			inputHapticsManager.Reset();
			inputHapticsStatics.Reset();
			penDeviceStatics.Reset();
			api.Shutdown();
			attachedPointerId = 0;
			lastAttachAttemptPointerId = 0;
			initialized = false;
			inputHapticsSupported = false;
			pointerIntensitySupported = false;
			pointerPlayCountSupported = false;
			pointerDurationSupported = false;
			lastContinuousTickMs = 0;
		}

		bool IsAvailable() const noexcept
		{
			return penDeviceStatics || inputHapticsSupported;
		}

		bool AttachPointerId(uint32_t pointerId) noexcept
		{
			if (!enabled || pointerId == 0) return false;
			if (!initialized) Initialize();
			if (!penDeviceStatics) return false;
			if (pointerController && pointerId == attachedPointerId) return true;
			if (!pointerController && pointerId == lastAttachAttemptPointerId) return false;
			if (pointerController && pointerId != attachedPointerId)
			{
				// 新 PointerId 绑定失败时不能继续使用旧笔的 controller，避免串笔反馈。
				StopFeedback();
				pointerController.Reset();
				pointerFeedback.clear();
				attachedPointerId = 0;
				pointerIntensitySupported = false;
				pointerPlayCountSupported = false;
				pointerDurationSupported = false;
			}
			lastAttachAttemptPointerId = pointerId; // 同一个 pointerId 失败后不重复查询设备。

			Microsoft::WRL::ComPtr<IPenDevice> penDevice;
			HRESULT result = penDeviceStatics->GetFromPointerId(
				pointerId, penDevice.ReleaseAndGetAddressOf());
			if (FAILED(result) || !penDevice) return false;

			Microsoft::WRL::ComPtr<IPenDevice2> penDevice2;
			result = penDevice.As(&penDevice2);
			if (FAILED(result) || !penDevice2) return false;

			Microsoft::WRL::ComPtr<ISimpleHapticsController> controller;
			result = penDevice2->get_SimpleHapticsController(
				controller.ReleaseAndGetAddressOf());
			if (FAILED(result) || !controller) return false;

			CachePointerController(controller.Get());
			attachedPointerId = pointerId;
			if constexpr (kHapticDebugLoggingEnabled)
				std::cout << "[Haptics] attached pointer_id=" << pointerId << std::endl;
			return pointerController != nullptr;
		}

		bool PlayDiscrete(HapticDiscreteFeedback feedback, double intensity) noexcept
		{
			if (!enabled) return false;
			if (!initialized) Initialize();
			const uint16_t waveform = ToWaveform(feedback);
			const uint16_t fallback = ToWaveform(FallbackDiscreteHapticFeedback(feedback));
			const bool pointerSent = SendPointerDiscrete(waveform, fallback, intensity);
			const bool sent = pointerSent || SendInputManagerDiscrete(waveform, fallback, intensity);
			if constexpr (kHapticDebugLoggingEnabled)
				std::cout << "[Haptics] discrete waveform=0x" << std::hex << waveform
					<< " fallback=0x" << fallback << std::dec
					<< " sent=" << (sent ? "true" : "false")
					<< " pointer_controller=" << (pointerController ? "true" : "false")
					<< " pointer_sent=" << (pointerSent ? "true" : "false")
					<< " input_manager=" << (inputHapticsManager ? "true" : "false")
					<< std::endl;
			return sent;
		}

		bool TickContinuous(HapticContinuousFeedback feedback, double intensity) noexcept
		{
			if (!enabled) return false;
			if (!initialized) Initialize();
			const uint16_t waveform = ToWaveform(feedback);
			const uint16_t fallback = ToWaveform(
				FallbackContinuousHapticFeedback(feedback));
			if (continuousFeedbackStarted && activeContinuousWaveform == waveform &&
				activeContinuousFallback == fallback)
				return true;
			if (continuousFeedbackStarted) StopFeedback();
			const uint64_t now = NowMilliseconds();
			const bool sent = SendPointerContinuous(waveform, fallback, intensity) ||
				SendInputManagerContinuous(waveform, fallback, intensity);
			if (sent)
			{
				lastContinuousTickMs = now;
				activeContinuousWaveform = waveform;
				activeContinuousFallback = fallback;
				continuousFeedbackStarted = true;
			}
			LogContinuousResult(waveform, fallback, sent, intensity, now);
			return sent;
		}

		void StopFeedback() noexcept
		{
			if constexpr (kHapticDebugLoggingEnabled)
			{
				if (lastContinuousTickMs != 0)
					std::cout << "[Haptics] stop continuous elapsed_ms="
						<< NowMilliseconds() - lastContinuousTickMs << std::endl;
			}
			lastContinuousTickMs = 0;
			activeContinuousWaveform = 0;
			activeContinuousFallback = 0;
			continuousFeedbackStarted = false;
			hasContinuousDebugState = false;
			if (pointerController) pointerController->StopFeedback();
			if (inputHapticsManager)
			{
				boolean stopped = false;
				inputHapticsManager->TryStopFeedback(&stopped);
			}
		}

	private:
		void LogDebugFailure(const char* reason, uint16_t waveform,
			uint16_t fallback, HRESULT result = S_OK) noexcept
		{
			if constexpr (!kHapticDebugLoggingEnabled) return;
			const uint64_t now = NowMilliseconds();
			if (lastDebugLogMs != 0 &&
				now - lastDebugLogMs < kHapticDebugLogIntervalMs)
				return;
			lastDebugLogMs = now;
			std::cout << "[Haptics] " << reason << " waveform=0x" << std::hex
				<< waveform << " fallback=0x" << fallback;
			if (result != S_OK)
				std::cout << " hr=0x" << static_cast<uint32_t>(result);
			std::cout << std::dec << std::endl;
		}

		void LogContinuousResult(uint16_t waveform, uint16_t fallback,
			bool sent, double intensity, uint64_t now) noexcept
		{
			if constexpr (!kHapticDebugLoggingEnabled) return;
			if (hasContinuousDebugState && lastContinuousSent == sent &&
				lastContinuousWaveform == waveform &&
				lastContinuousFallback == fallback &&
				now - lastContinuousDebugLogMs < kHapticDebugLogIntervalMs)
				return;
			hasContinuousDebugState = true;
			lastContinuousSent = sent;
			lastContinuousWaveform = waveform;
			lastContinuousFallback = fallback;
			lastContinuousDebugLogMs = now;
			std::cout << "[Haptics] continuous waveform=0x" << std::hex << waveform
				<< " fallback=0x" << fallback << std::dec
				<< " sent=" << (sent ? "true" : "false")
				<< " intensity=";
			if (HasExplicitIntensity(intensity))
				std::cout << intensity;
			else
				std::cout << "system";
			std::cout
				<< " pointer_controller=" << (pointerController ? "true" : "false")
				<< " input_manager=" << (inputHapticsManager ? "true" : "false")
				<< " input_api=" << (inputHapticsSupported ? "true" : "false")
				<< " current_device=";
			if (hasInputManagerDeviceType)
				std::cout << lastInputManagerDeviceType;
			else
				std::cout << "unknown";
			std::cout << std::endl;
		}

		void LogUnavailableOnce(const char* reason) noexcept
		{
			if (unavailableLogged) return;
			unavailableLogged = true;
			std::cout << "[Haptics] unavailable: " << reason << std::endl;
		}

		void CachePointerController(ISimpleHapticsController* controller) noexcept
		{
			StopFeedback();
			pointerController = controller;
			pointerFeedback.clear();
			pointerIntensitySupported = false;
			pointerPlayCountSupported = false;
			pointerDurationSupported = false;
			if (!pointerController) return;

			boolean supported = false;
			if (SUCCEEDED(pointerController->get_IsIntensitySupported(&supported)))
				pointerIntensitySupported = supported != 0;
			supported = false;
			if (SUCCEEDED(pointerController->get_IsPlayCountSupported(&supported)))
				pointerPlayCountSupported = supported != 0;
			supported = false;
			if (SUCCEEDED(pointerController->get_IsPlayDurationSupported(&supported)))
				pointerDurationSupported = supported != 0;

			Microsoft::WRL::ComPtr<IVectorViewSimpleHapticsControllerFeedback> view;
			if (FAILED(pointerController->get_SupportedFeedback(
				view.ReleaseAndGetAddressOf())) || !view)
				return;
			uint32_t size = 0;
			if (FAILED(view->get_Size(&size))) return;
			size = std::min<uint32_t>(size, kMaximumCachedFeedbackCount);
			pointerFeedback.reserve(size);
			for (uint32_t index = 0; index < size; ++index)
			{
				Microsoft::WRL::ComPtr<ISimpleHapticsControllerFeedback> feedback;
				if (FAILED(view->GetAt(index, feedback.ReleaseAndGetAddressOf())) ||
					!feedback)
					continue;
				uint16_t waveform = 0;
				if (FAILED(feedback->get_Waveform(&waveform))) continue;
				pointerFeedback.push_back({ waveform, feedback });
			}
			if constexpr (kHapticDebugLoggingEnabled)
			{
				std::cout << "[Haptics] pointer controller feedback_count="
					<< pointerFeedback.size()
					<< " intensity=" << (pointerIntensitySupported ? "true" : "false")
					<< " duration=" << (pointerDurationSupported ? "true" : "false")
					<< " play_count=" << (pointerPlayCountSupported ? "true" : "false")
					<< " waveforms=";
				for (const FeedbackEntry& entry : pointerFeedback)
					std::cout << "0x" << std::hex << entry.waveform << " ";
				std::cout << std::dec << std::endl;
			}
		}

		ISimpleHapticsControllerFeedback* FindFeedback(
			uint16_t waveform, uint16_t fallback) noexcept
		{
			const auto matches = [](const FeedbackEntry& entry, uint16_t value)
				{
					return entry.waveform == value && entry.feedback;
				};
			auto iterator = std::find_if(pointerFeedback.begin(), pointerFeedback.end(),
				[&](const FeedbackEntry& entry) { return matches(entry, waveform); });
			if (iterator == pointerFeedback.end())
				iterator = std::find_if(pointerFeedback.begin(), pointerFeedback.end(),
					[&](const FeedbackEntry& entry) { return matches(entry, fallback); });
			return iterator == pointerFeedback.end() ? nullptr : iterator->feedback.Get();
		}

		bool SendPointerContinuous(uint16_t waveform, uint16_t fallback,
			double intensity) noexcept
		{
			if (!pointerController) return false;
			ISimpleHapticsControllerFeedback* feedback = FindFeedback(waveform, fallback);
			if (!feedback)
			{
				LogDebugFailure("pointer waveform is not supported", waveform, fallback);
				return false;
			}
			HRESULT result = E_FAIL;
			// 连续书写波形由设备保持播放，按官方 PenHaptics sample 只启动一次，抬笔时 StopFeedback。
			if (pointerIntensitySupported && HasExplicitIntensity(intensity))
				result = pointerController->SendHapticFeedbackWithIntensity(feedback, intensity);
			else
				result = pointerController->SendHapticFeedback(feedback);
			if (FAILED(result))
				LogDebugFailure("pointer continuous send failed", waveform, fallback, result);
			return SUCCEEDED(result);
		}

		bool SendPointerDiscrete(uint16_t waveform, uint16_t fallback,
			double intensity) noexcept
		{
			if (!pointerController) return false;
			ISimpleHapticsControllerFeedback* feedback = FindFeedback(waveform, fallback);
			if (!feedback) return false;
			if (!HasExplicitIntensity(intensity))
				return SUCCEEDED(pointerController->SendHapticFeedback(feedback));
			HRESULT result = E_FAIL;
			if (pointerPlayCountSupported)
				result = pointerController->SendHapticFeedbackForPlayCount(
					feedback, intensity, 1, { 0 });
			else if (pointerDurationSupported)
				result = pointerController->SendHapticFeedbackForDuration(
					feedback, intensity, { kDiscreteDurationTicks });
			else if (pointerIntensitySupported)
				result = pointerController->SendHapticFeedbackWithIntensity(feedback, intensity);
			else
				result = pointerController->SendHapticFeedback(feedback);
			return SUCCEEDED(result);
		}

		bool EnsureInputManager() noexcept
		{
			if (!inputHapticsSupported || !inputHapticsStatics) return false;
			if (inputHapticsManager) return true;
			HRESULT result = inputHapticsStatics->GetForCurrentThread(
				inputHapticsManager.ReleaseAndGetAddressOf());
			if (FAILED(result))
				LogDebugFailure("InputHapticsManager.GetForCurrentThread failed", 0, 0, result);
			return SUCCEEDED(result) && inputHapticsManager;
		}

		bool InputManagerTargetsPen() noexcept
		{
			if (!EnsureInputManager()) return false;
			int32_t deviceType = 0;
			const HRESULT result = inputHapticsManager->get_CurrentHapticsControllerDeviceType(
				&deviceType);
			if (FAILED(result))
			{
				hasInputManagerDeviceType = false;
				LogDebugFailure("InputHapticsManager device type query failed", 0, 0, result);
				return false;
			}
			hasInputManagerDeviceType = true;
			lastInputManagerDeviceType = deviceType;
			return deviceType == kHapticDeviceTypePen;
		}

		bool SendInputManagerContinuous(uint16_t waveform, uint16_t fallback,
			double intensity) noexcept
		{
			if (!InputManagerTargetsPen()) return false;
			boolean sent = false;
			HRESULT result = E_FAIL;
			if (HasExplicitIntensity(intensity))
			{
				result = inputHapticsManager->TrySendHapticWaveformWithIntensity(
					waveform, fallback, intensity, &sent);
				if (SUCCEEDED(result) && sent) return true;
				sent = false;
			}
			result = inputHapticsManager->TrySendHapticWaveform(
				waveform, fallback, &sent);
			return SUCCEEDED(result) && sent;
		}

		bool SendInputManagerDiscrete(uint16_t waveform, uint16_t fallback,
			double intensity) noexcept
		{
			if (!InputManagerTargetsPen()) return false;
			boolean sent = false;
			if (!HasExplicitIntensity(intensity))
			{
				const HRESULT result = inputHapticsManager->TrySendHapticWaveform(
					waveform, fallback, &sent);
				return SUCCEEDED(result) && sent;
			}
			HRESULT result = inputHapticsManager->TrySendHapticWaveformForPlayCount(
				waveform, fallback, intensity, 1, { 0 }, &sent);
			if (SUCCEEDED(result) && sent) return true;
			sent = false;
			result = inputHapticsManager->TrySendHapticWaveformWithIntensity(
				waveform, fallback, intensity, &sent);
			if (SUCCEEDED(result) && sent) return true;
			sent = false;
			result = inputHapticsManager->TrySendHapticWaveform(
				waveform, fallback, &sent);
			return SUCCEEDED(result) && sent;
		}
	};

	PenHapticFeedback::PenHapticFeedback()
		: impl_(std::make_unique<PenHapticFeedbackImpl>())
	{
	}

	PenHapticFeedback::~PenHapticFeedback()
	{
		Shutdown();
	}

	bool PenHapticFeedback::Initialize() noexcept
	{
		return impl_ && impl_->Initialize();
	}

	void PenHapticFeedback::Shutdown() noexcept
	{
		if (impl_) impl_->Shutdown();
	}

	void PenHapticFeedback::SetEnabled(bool enabled) noexcept
	{
		if (!impl_) return;
		impl_->enabled = enabled;
		if (!enabled) impl_->StopFeedback();
	}

	bool PenHapticFeedback::IsEnabled() const noexcept
	{
		return impl_ && impl_->enabled;
	}

	bool PenHapticFeedback::IsAvailable() const noexcept
	{
		return impl_ && impl_->IsAvailable();
	}

	bool PenHapticFeedback::AttachPointerId(uint32_t pointerId) noexcept
	{
		return impl_ && impl_->AttachPointerId(pointerId);
	}

	bool PenHapticFeedback::PlayDiscrete(
		HapticDiscreteFeedback feedback, double intensity) noexcept
	{
		return impl_ && impl_->PlayDiscrete(feedback, intensity);
	}

	bool PenHapticFeedback::TickContinuous(
		HapticContinuousFeedback feedback, double intensity) noexcept
	{
		return impl_ && impl_->TickContinuous(feedback, intensity);
	}

	void PenHapticFeedback::StopFeedback() noexcept
	{
		if (impl_) impl_->StopFeedback();
	}
}
