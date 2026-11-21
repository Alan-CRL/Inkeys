#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.RtsStartup.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.MouseInput.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Inkeys.Drawing.Draw3.contact_input;

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	using namespace Inkeys::Drawing::Draw3::RtsStartup;

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[Draw3RtsStartup] failed: " << name << '\n';
		return condition;
	}

	std::atomic<unsigned> fatalPublications = 0;
	ULONGLONG PublishUnexpectedFatal() noexcept
	{
		fatalPublications.fetch_add(1, std::memory_order_relaxed);
		return GetTickCount64() + 15000;
	}

	void TestInputResolution(int& failures)
	{
		using P = InputPreference;
		struct Case { int major, minor, build; P expected[4]; };
		// 每一行直接列出四种偏好的金值，覆盖 Win10/11 的构建号边界。
		const Case systems[] = {
			{ 6, 1, 7601, { P::RealTimeStylus, P::RealTimeStylus, P::RealTimeStylus, P::MouseCompatibility } },
			{ 6, 2, 9200, { P::RealTimeStylus, P::RealTimeStylus, P::Pointer, P::MouseCompatibility } },
			{ 6, 3, 9600, { P::Pointer, P::RealTimeStylus, P::Pointer, P::MouseCompatibility } },
			{ 10, 0, 21999, { P::Pointer, P::RealTimeStylus, P::Pointer, P::MouseCompatibility } },
			{ 10, 0, 22000, { P::RealTimeStylus, P::RealTimeStylus, P::Pointer, P::MouseCompatibility } }
		};
		for (const auto& system : systems)
			for (const bool largeScreen : { false, true })
				for (int i = 0; i < 4; ++i)
				{
					const auto selected = ResolveInputBackend(static_cast<P>(i), largeScreen,
						system.major, system.minor, system.build);
					// 大屏触控模式只约束自动选择；手动选择照常按偏好解析。
					const auto expected = largeScreen && static_cast<P>(i) == P::Automatic
						? P::RealTimeStylus : system.expected[i];
					if (!Expect(selected == expected,
						"OS, large-screen mode and stored preference resolve at startup")) ++failures;
				}
	}

	void TestInputSettingsStatus(int& failures)
	{
		using M = HostInputMode;
		// 每种实际模式分别覆盖 Win11 分界。
		struct Case { M actual; bool noticeBefore11; };
		const Case cases[] = {
			{ M::Disabled, false },
			{ M::RtsExtended, true },
			{ M::RtsBasicXY, true },
			{ M::Pointer, false },
			{ M::Win32Mouse, false }
		};
		for (const auto& test : cases)
		{
			for (const auto [major, build] : { std::pair{ 6, 7601 }, std::pair{ 6, 9600 }, std::pair{ 10, 21999 } })
				if (!Expect(ShowWhiteboardRtsNotice(major, build, test.actual) == test.noticeBefore11,
					"pre-Windows 11 notice depends only on the initialized RTS mode")) ++failures;
			if (!Expect(!ShowWhiteboardRtsNotice(10, 22000, test.actual) && !ShowWhiteboardRtsNotice(11, 1, test.actual),
				"Windows 11 and later hide the legacy whiteboard notice")) ++failures;
		}
	}

	void TestBackendFallback(int& failures)
	{
		using P = InputPreference;
		using M = HostInputMode;
		struct Case { P selected; bool supported, pointer; M rts; bool mouse; M actual; const char* order; };
		// 首选失败后固定按 RTS → WM_POINTER → 鼠标兼容 回退并跳过已试项；不支持 Pointer 的系统不调用它。
		const Case cases[] = {
			{ P::Pointer, true, true, M::Disabled, false, M::Pointer, "P" },
			{ P::Pointer, true, false, M::RtsExtended, true, M::RtsExtended, "PR" },
			{ P::Pointer, true, false, M::RtsBasicXY, true, M::RtsBasicXY, "PR" },
			{ P::Pointer, true, false, M::Disabled, true, M::Win32Mouse, "PRM" },
			{ P::RealTimeStylus, true, true, M::RtsExtended, true, M::RtsExtended, "R" },
			{ P::RealTimeStylus, true, true, M::Disabled, true, M::Pointer, "RP" },
			{ P::RealTimeStylus, true, false, M::Disabled, true, M::Win32Mouse, "RPM" },
			{ P::RealTimeStylus, false, true, M::Disabled, true, M::Win32Mouse, "RM" },
			{ P::MouseCompatibility, true, true, M::RtsExtended, true, M::Win32Mouse, "M" },
			{ P::MouseCompatibility, true, true, M::RtsExtended, false, M::RtsExtended, "MR" },
			{ P::MouseCompatibility, true, true, M::Disabled, false, M::Pointer, "MRP" },
			{ P::MouseCompatibility, true, false, M::Disabled, false, M::Disabled, "MRP" },
			{ P::MouseCompatibility, false, true, M::Disabled, false, M::Disabled, "MR" }
		};
		for (const auto& test : cases)
		{
			std::string calls;
			const auto actual = StartInputBackend(test.selected, test.supported,
				[&]() { calls += 'P'; return test.pointer; },
				[&]() { calls += 'R'; return test.rts; },
				[&]() { calls += 'M'; return test.mouse; });
			if (!Expect(actual == test.actual && calls == test.order,
				"failed input producer falls through in order; successful selection ends attempts")) ++failures;
		}
	}

	void TestRtsHresultFallbackToMouse(int& failures)
	{
		using P = InputPreference;
		using M = HostInputMode;
		struct Case
		{
			int major, minor, build;
			P preference, resolved;
			AttemptResult extendedFailure;
			const char* order;
			const char* name;
		};
		const Case cases[] = {
			{ 10, 0, 19045, P::Automatic, P::Pointer, { FailureStage::Create, REGDB_E_CLASSNOTREG },
				"PREM", "Wine-like automatic startup tries Pointer and RTS Create once before mouse" },
			{ 6, 1, 7601, P::Automatic, P::RealTimeStylus, { FailureStage::Create, REGDB_E_CLASSNOTREG },
				"REM", "Windows 7 automatic startup skips unsupported Pointer after RTS Create fails" },
			{ 10, 0, 19045, P::RealTimeStylus, P::RealTimeStylus, { FailureStage::InputConfiguration, E_INVALIDARG },
				"REBPM", "explicit RTS exhausts both profiles before trying Pointer and mouse" }
		};
		for (const auto& test : cases)
		{
			const auto resolved = ResolveInputBackend(test.preference, false, test.major, test.minor, test.build);
			std::string calls;
			const auto actual = StartInputBackend(resolved, SupportsPointerInput(test.major, test.minor),
				[&] { calls += 'P'; return false; },
				[&]
				{
					// 在后端回退中走真实的 profile 选择，只注入 COM 边界的 HRESULT。
					calls += 'R';
					const bool ready = InitializeWithFallback({},
						[&](PacketProfile profile, Inkeys::Shutdown::FailedCleanupSignal)
						{
							calls += profile == PacketProfile::Extended ? 'E' : 'B';
							return profile == PacketProfile::Extended ? test.extendedFailure
								: AttemptResult{ FailureStage::Enable, E_INVALIDARG };
						},
						[]() noexcept {},
						[](PacketProfile, const AttemptResult&) {});
					return ready ? M::RtsExtended : M::Disabled;
				},
				[&] { calls += 'M'; return true; });
			if (!Expect(resolved == test.resolved && actual == M::Win32Mouse && calls == test.order, test.name)) ++failures;
		}
	}

	void TestTouchpadStartup(int& failures)
	{
		using E = TouchpadEntry;
		using M = HostInputMode;
		using A = TouchpadAvailability;
		struct System { int major, minor, build; E pointer, rts; };
		for (const auto& os : { System{6, 1, 7601, E::None, E::None}, System{6, 2, 9200, E::None, E::None},
			System{6, 3, 9600, E::DirectManipulation, E::None}, System{10, 0, 21999, E::DirectManipulation, E::None},
			System{10, 0, 22000, E::DirectManipulation, E::WindowRegistration} })
		{
			for (auto mode : { M::Pointer, M::RtsExtended, M::RtsBasicXY, M::Win32Mouse, M::Disabled })
			{
				const E expected = mode == M::Pointer ? os.pointer : IsRtsInput(mode) ? os.rts : E::None;
				if (!Expect(SelectTouchpadEntry(os.major, os.minor, os.build, mode) == expected,
					"navigation matrix uses the actual backend and Windows version")) ++failures;

				// 注入生产启动编排；小写 p/r/m 表示回调确已就绪，D/W 是实际创建入口的调用。
				// 不支持 WM_POINTER 的系统跳过 Pointer 回调，因此 Pointer 生产者不可能就绪。
				const bool pointerSupported = SupportsPointerInput(os.major, os.minor);
				const M ready = mode == M::Pointer && !pointerSupported ? M::Disabled : mode;
				std::string order;
				A result{};
				auto actual = StartInputAndNavigation(os.major, os.minor, os.build, result, InputPreference::Pointer,
					[&] { order += 'P'; if (mode != M::Pointer) return false; order += 'p'; return true; },
					[&] { order += 'R'; if (!IsRtsInput(mode)) return M::Disabled; order += 'r'; return mode; },
					[&] { order += 'M'; if (mode != M::Win32Mouse) return false; order += 'm'; return true; },
					[&](E entry) { order += entry == E::DirectManipulation ? 'D' : 'W'; return A::Ok; });
				std::string sequence = ready == M::Pointer ? "Pp" : IsRtsInput(ready) ? "PRr" :
					ready == M::Win32Mouse ? "PRMm" : "PRM";
				if (!pointerSupported) sequence.erase(0, 1);
				const E readyEntry = ready == mode ? expected : E::None;
				if (readyEntry == E::DirectManipulation) sequence += 'D';
				if (readyEntry == E::WindowRegistration) sequence += 'W';
				const auto reason = readyEntry != E::None ? A::Ok : os.pointer == E::None ? A::OsTooOld :
					IsRtsInput(ready) ? A::RtsOnLegacyOs : A::BackendDisabled;
				if (!Expect(actual == ready && order == sequence && result == reason,
					"native initialization follows producer readiness and fallback, including failed startup")) ++failures;
			}
		}
		for (auto failure : { A::RegistrationUnavailable, A::InitFailed })
		{
			A result{};
			const auto actual = StartInputAndNavigation(10, 0, 26300, result, InputPreference::RealTimeStylus,
				[] { return false; }, [] { return M::RtsBasicXY; }, [] { return false; },
				[&](E) { return failure; });
			if (!Expect(result == failure && actual == M::RtsBasicXY, "gesture failure preserves ready RTS")) ++failures;
		}
	}

	void TestRtsStylusAngles(int& failures)
	{
		// 生产 RTS 解码直接调用这份头文件公式；RealtimeStylus 模块不进无窗口工程。
		constexpr float pi = 3.14159265358979323846f;
		auto check = [&](bool condition, const char* name) { if (!Expect(condition, name)) ++failures; };
		auto approx = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
		struct Direction { float x, y, orientation; };
		const Direction directions[] = { { pi * 0.25f, 0.0f, 0.0f }, { 0.0f, pi * 0.25f, pi * 0.5f },
			{ -pi * 0.25f, 0.0f, pi }, { 0.0f, -pi * 0.25f, pi * 1.5f } };
		for (const auto& direction : directions)
		{
			const auto angles = DecodeStylusAngles(false, 0.0f, 0.0f, direction.x, direction.y);
			check(approx(angles.tilt, pi * 0.25f) && approx(angles.orientation, direction.orientation),
				"RTS +Y tilt points away from the user; four cardinal directions map to model radians");
		}
		const auto vertical = DecodeStylusAngles(false, 0.0f, 0.0f, 0.0f, 0.0f);
		check(vertical.tilt == 0.0f && vertical.orientation == 0.0f, "RTS vertical pen keeps tilt 0 and bearing 0");
		for (const float altitude : { pi / 3.0f, -pi / 3.0f })
		{
			const auto angles = DecodeStylusAngles(true, pi * 0.5f, altitude, 0.0f, 0.0f);
			check(approx(angles.tilt, pi / 6.0f) && approx(angles.orientation, pi * 1.5f),
				"RTS signed altitude keeps tilt magnitude; inverted flag stays independent");
		}
		const float missing = std::numeric_limits<float>::quiet_NaN();
		const auto unknown = DecodeStylusAngles(false, 0.0f, 0.0f, missing, missing);
		check(unknown.tilt < 0.0f && unknown.orientation < 0.0f, "RTS missing axes leave pose unknown");
		const float wrapped = WrapStylusOrientation(-1.0e-8f);
		check(wrapped >= 0.0f && wrapped < pi * 2.0f, "tiny negative bearing wraps into [0, 2pi)");
	}

	void TestPointerFields(int& failures)
	{
		namespace P = Inkeys::Drawing::Draw3::PointerInput;
		auto check = [&](bool condition, const char* name) { if (!Expect(condition, name)) ++failures; };
		check(P::Pressure(false, 512) == -1.0f && P::Pressure(true, 0) == 0.0f &&
			P::Pressure(true, 512) == 0.5f && P::Pressure(true, 1024) == 1.0f,
			"pressure distinguishes missing data and real zero");
		constexpr float quarterTurn = 1.57079632679f;
		struct Direction { int x, y, quarters; };
		const Direction directions[] = { { 45, 0, 0 }, { 0, -45, 1 }, { -45, 0, 2 }, { 0, 45, 3 } };
		for (const auto& direction : directions)
		{
			const auto angles = P::Tilt(true, true, direction.x, direction.y);
			check(std::abs(angles.tilt - quarterTurn / 2) < 0.0001f &&
				std::abs(angles.orientation - direction.quarters * quarterTurn) < 0.0001f,
				"Pointer Y grows toward the user; four cardinal directions map to model radians");
		}
		const auto diagonal = P::Tilt(true, true, 45, 45);
		check(std::abs(diagonal.tilt - 0.9553166f) < 0.0001f &&
			std::abs(diagonal.orientation - 5.4977871f) < 0.0001f, "both axes contribute to the pen projection");
		for (const bool x : { false, true })
			for (const bool y : { false, true })
			{
				const auto angles = P::Tilt(x, y, 0, 0);
				check(angles.tilt == (x && y ? 0.0f : -1.0f) && angles.orientation == (x && y ? 0.0f : -1.0f),
					"missing axes are unknown; an upright pen keeps tilt 0 and bearing 0 like RTS");
			}
		const auto edge = P::Tilt(true, true, 90, 1);
		check(edge.orientation >= 0.0f && edge.orientation < 6.2831853f, "near-+X bearing never rounds to 2pi");
		const RECT area{ -20, -40, -4, -16 };
		const auto sample = P::ContactArea(true, area, 1.5f, 0.5f);
		check(sample.rawWidth == 16 && sample.rawHeight == 24 && sample.widthPx == 24 &&
			sample.heightPx == 12 && sample.units == SpeedEraser::ContactAreaUnits::CanvasPixels,
			"contact lengths preserve raw pixels and scale without translation");
		check(P::ContactArea(false, area).units == SpeedEraser::ContactAreaUnits::Missing &&
			P::ContactArea(true, RECT{ 0, 0, 0, 3 }).units == SpeedEraser::ContactAreaUnits::Missing &&
			P::ContactArea(true, RECT{ 0, 0, 3, -1 }).units == SpeedEraser::ContactAreaUnits::Missing,
			"missing and degenerate rectangles have no usable area");
		// 负屏幕原点、两轴不同倍率及非零设备原点，原始整数像素允许小数映射通过。
		const POINT raw{ -81, 210 }, hm{ 139, 242 }, origin{ -100, 180 };
		const RECT device{ 100, 200, 300, 600 }, display{ -100, 200, 0, 300 };
		const auto mapped = P::MapPosition(raw, hm, origin, &device, &display);
		check(mapped.x == 19.5f && mapped.y == 30.5f, "subpixel HIMETRIC affine mapping in signed client space");
		const auto rawFallback = [&](POINT metric, const RECT* from, const RECT* to)
		{
			const auto position = P::MapPosition(raw, metric, origin, from, to);
			return position.x == 19 && position.y == 30;
		};
		const RECT degenerate{ 100, 200, 100, 600 };
		check(rawFallback(hm, nullptr, nullptr) && rawFallback(hm, &degenerate, &display) &&
			rawFallback({ 260, 242 }, &device, &display) && rawFallback({ 301, 242 }, &device, &display),
			"unavailable, degenerate, offset and out-of-device mappings use raw pixels");
		check(P::PerformanceCount(91, 0, 100) == 91 && P::PerformanceCount(91, 90, 100) == 91 &&
			P::PerformanceCount(0, 0, 100) == 0 && P::PerformanceCount(91, 91, 100) == 0 &&
			P::PerformanceCount(90, 91, 100) == 0 && P::PerformanceCount(101, 91, 100) == 0 &&
			P::PerformanceCount(91, 0, 0) == 0, "only plausible strictly advancing pointer clocks are available");
	}

	void TestAttemptSequence(FailureStage firstFailure, bool basicSucceeds, int& failures)
	{
		// 只注入 COM 边界结果；尝试次数、失败清理、scope 取消/join 使用真实生产编排。
		Inkeys::Shutdown::FailedCleanupDeadline outer(&PublishUnexpectedFatal);
		outer.PrepareOrFatal();
		std::vector<PacketProfile> attempts;
		unsigned cleanups = 0;
		unsigned diagnoses = 0;
		bool sawActiveFailure = false;
		bool sawCancelledPrevious = false;
		bool lifecycleOrder = true;
		Inkeys::Shutdown::FailedCleanupSignal previous;
		const bool selected = InitializeWithFallback(outer.Signal(),
			[&](PacketProfile profile, Inkeys::Shutdown::FailedCleanupSignal signal)
			{
				if (!attempts.empty())
				{
					lifecycleOrder &= cleanups == 1 && diagnoses == 1;
					sawCancelledPrevious = previous.BeginKnownFailure() == 0;
				}
				attempts.push_back(profile);
				previous = signal;
				if (profile == PacketProfile::Extended)
					return firstFailure == FailureStage::None ? AttemptResult{}
						: AttemptResult{ firstFailure, E_INVALIDARG };
				return basicSucceeds ? AttemptResult{}
					: AttemptResult{ FailureStage::Enable, E_INVALIDARG };
			},
			[&]() noexcept
			{
				++cleanups;
				sawActiveFailure |= previous.BeginKnownFailure() != 0;
			},
			[&](PacketProfile, const AttemptResult& result)
			{
				++diagnoses;
				if (!result.Ready()) lifecycleOrder &= cleanups == diagnoses;
			});
		const bool retry = firstFailure == FailureStage::PacketDescription ||
			firstFailure == FailureStage::InputConfiguration || firstFailure == FailureStage::Enable;
		const bool expectedReady = firstFailure == FailureStage::None || (retry && basicSucceeds);
		if (!Expect(selected == expectedReady, "profile selection follows configuration/enable result")) ++failures;
		if (!Expect(attempts.size() == (retry ? 2u : 1u) && attempts.front() == PacketProfile::Extended &&
			(!retry || attempts.back() == PacketProfile::BasicXY), "bounded Extended then BasicXY sequence")) ++failures;
		if (!Expect(cleanups == (firstFailure == FailureStage::None ? 0u : (retry && !basicSucceeds ? 2u : 1u)),
			"every failed attempt cleaned once before fallback")) ++failures;
		if (!Expect(lifecycleOrder && diagnoses == attempts.size(), "diagnostic and cleanup precede next attempt")) ++failures;
		if (!Expect(firstFailure == FailureStage::None || sawActiveFailure, "cleanup retains active failure deadline")) ++failures;
		if (!Expect(!retry || sawCancelledPrevious, "old attempt cancelled before BasicXY starts")) ++failures;
		if (!Expect(previous.BeginKnownFailure() == 0, "last attempt scope cancelled before caller resumes")) ++failures;
		outer.CompleteOrFatal();
	}

	void TestDisableStateVerification(int& failures)
	{
		struct Case { HRESULT disable; HRESULT query; BOOL value; bool writes; bool stopped; };
		const Case cases[] = {
			{ S_OK, E_FAIL, TRUE, true, true },
			{ S_FALSE, E_FAIL, TRUE, true, true },
			{ E_FAIL, S_OK, FALSE, true, true },
			{ E_INVALIDARG, S_OK, FALSE, true, true },
			{ E_FAIL, S_OK, TRUE, true, false },
			{ E_FAIL, S_OK, 2, true, false },
			{ E_FAIL, E_FAIL, FALSE, true, false },
			{ E_FAIL, S_OK, FALSE, false, false }
		};
		for (const auto& test : cases)
		{
			unsigned queries = 0;
			const auto disabled = VerifyDisabled(test.disable, [&](BOOL* enabled) noexcept
			{
				++queries;
				if (test.writes) *enabled = test.value;
				return test.query;
			});
			if (!Expect(disabled.StopProven() == test.stopped,
				"Disable failure needs successful, explicitly FALSE state readback")) ++failures;
			if (!Expect(queries == (FAILED(test.disable) ? 1u : 0u),
				"successful Disable does not query state")) ++failures;
		}
	}

	void TestFailedEnableCleanupWithoutPublisher(bool basicSucceeds, int& failures)
	{
		unsigned attempts = 0, cleanups = 0, queries = 0;
		bool orderValid = true, allStopped = true;
		// 与产品共享回退编排和停产判定；只注入 Enable/Disable/get_Enabled 的 COM 结果。
		const bool ready = InitializeWithFallback({},
			[&](PacketProfile profile, Inkeys::Shutdown::FailedCleanupSignal signal)
			{
				orderValid &= cleanups == attempts && !signal.HasPublisher();
				++attempts;
				return profile == PacketProfile::BasicXY && basicSucceeds
					? AttemptResult{} : AttemptResult{ FailureStage::Enable, E_INVALIDARG };
			},
			[&]() noexcept
			{
				const auto disabled = VerifyDisabled(E_FAIL, [&](BOOL* enabled) noexcept
				{
					++queries;
					*enabled = FALSE;
					return S_OK;
				});
				allStopped &= disabled.StopProven();
				++cleanups;
			},
			[](PacketProfile, const AttemptResult&) {});
		if (!Expect(ready == basicSucceeds && attempts == 2 && orderValid && allStopped &&
			cleanups == (basicSucceeds ? 1u : 2u) && queries == cleanups,
			"no-publisher failed Enable/Disable with confirmed stop reaches BasicXY or returns safe failure")) ++failures;
	}

	void TestExceptionStopsSelection(int& failures)
	{
		Inkeys::Shutdown::FailedCleanupDeadline outer(&PublishUnexpectedFatal);
		outer.PrepareOrFatal();
		unsigned attempts = 0;
		unsigned cleanups = 0;
		bool caught = false;
		ULONGLONG originalDeadline = 0;
		try
		{
			InitializeWithFallback(outer.Signal(),
				[&](PacketProfile, Inkeys::Shutdown::FailedCleanupSignal signal) -> AttemptResult
				{
					++attempts;
					originalDeadline = signal.BeginKnownFailure();
					throw std::runtime_error("injected initialization exception");
				},
				[&]() noexcept { ++cleanups; },
				[](PacketProfile, const AttemptResult&) {});
		}
		catch (const std::runtime_error&) { caught = true; }
		if (!Expect(caught && attempts == 1 && cleanups == 1, "exception stops selection after safe cleanup")) ++failures;
		if (!Expect(originalDeadline != 0 && outer.Signal().BeginKnownFailure() == originalDeadline,
			"terminal exception preserves original absolute deadline")) ++failures;
		outer.CompleteOrFatal();
	}

	void TestCallbackCloseAndDrain(int& failures)
	{
		CallbackGate gate;
		HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!Expect(SUCCEEDED(gate.ReadyResult()) && entered && release, "gate test events created"))
		{
			++failures;
			if (entered) CloseHandle(entered);
			if (release) CloseHandle(release);
			return;
		}
		std::atomic<unsigned> progress = 0;
		std::thread producer([&]
		{
			CallbackGate::Lease callback(gate);
			if (!callback) { SetEvent(entered); return; }
			progress.store(1, std::memory_order_relaxed);
			SetEvent(entered);
			WaitForSingleObject(release, 5000);
			progress.store(2, std::memory_order_relaxed);
		});
		const bool didEnter = WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0;
		gate.Close();
		{
			CallbackGate::Lease rejected(gate);
			if (!Expect(!rejected, "closed gate refuses later callback")) ++failures;
		}
		if (!Expect(didEnter && progress.load(std::memory_order_relaxed) == 1 &&
			!gate.WaitUntilDrained(0), "close waits for the actual in-flight callback")) ++failures;
		gate.Close(); // 重复关闭不能越过最后 callback 的通知尾部。
		if (!Expect(!gate.WaitUntilDrained(0), "repeated close cannot signal a live callback as drained")) ++failures;
		SetEvent(release);
		if (!Expect(gate.WaitUntilDrained(5000) && progress.load(std::memory_order_acquire) == 2,
			"drain observes callback business access completed")) ++failures;
		producer.join();
		CloseHandle(release);
		CloseHandle(entered);
	}

	void TestCandidateDownDoesNotPublish(int& failures)
	{
		ContactInputCoordinator input;
		ContactPublicationGate failedCandidate;
		ContactSnapshot down{};
		down.phase = ContactPhase::Down;
		down.position = { 10.0f, 20.0f };
		down.qpc = 1;
		const auto publish = [&]() noexcept
		{
			return input.PublishDown(0x123u, 0x456u, InputDeviceType::Pen, down);
		};
		const auto enteredBeforeReady = failedCandidate.CaptureDown();
		if (!Expect(!enteredBeforeReady.PublishDown(publish), "candidate Down cannot reach the actual coordinator")) ++failures;
		failedCandidate.Enable();
		if (!Expect(!enteredBeforeReady.PublishDown(publish), "Down already entered before ready cannot become publishable")) ++failures;
		ContactSnapshot oldMove = down;
		oldMove.phase = ContactPhase::Move;
		if (!Expect(!input.PublishMove(0x123u, 0x456u, oldMove), "old candidate Move cannot create a missing Down")) ++failures;
		ContactRecord* record = nullptr;
		if (!Expect(!input.TryDequeue(record), "failed/pre-ready candidate leaves no queued Down")) ++failures;
		ContactPublicationGate selectedCandidate;
		selectedCandidate.Enable();
		if (!Expect(selectedCandidate.CaptureDown().PublishDown(publish) && input.TryDequeue(record) && record,
			"new Down after input ready reaches the shared coordinator")) ++failures;
		if (record)
		{
			const ContactHandle handle{ record, record->Generation() };
			input.CloseAllProducerContacts(2);
			input.Recycle(handle);
		}
	}
}

int RunDraw3RtsStartupTests()
{
	int failures = 0;
	TestInputResolution(failures);
	TestInputSettingsStatus(failures);
	TestBackendFallback(failures);
	TestRtsHresultFallbackToMouse(failures);
	TestTouchpadStartup(failures);
	TestRtsStylusAngles(failures);
	TestPointerFields(failures);
	TestAttemptSequence(FailureStage::None, true, failures);
	TestAttemptSequence(FailureStage::PacketDescription, true, failures);
	TestAttemptSequence(FailureStage::Enable, true, failures); // 现场 Set 成功、Enable=E_INVALIDARG。
	TestAttemptSequence(FailureStage::InputConfiguration, true, failures);
	TestAttemptSequence(FailureStage::Enable, false, failures);
	TestAttemptSequence(FailureStage::Create, false, failures);
	TestDisableStateVerification(failures);
	TestFailedEnableCleanupWithoutPublisher(true, failures);
	TestFailedEnableCleanupWithoutPublisher(false, failures);
	TestExceptionStopsSelection(failures);
	TestCallbackCloseAndDrain(failures);
	TestCandidateDownDoesNotPublish(failures);
	if (!Expect(fatalPublications.load(std::memory_order_relaxed) == 0, "healthy attempts never publish fatal shutdown")) ++failures;
	std::cout << "[Draw3RtsStartup] " << (failures ? "FAIL" : "PASS")
		<< " (production orchestration/gates; no COM provider or HWND)\n";
	return failures;
}
