#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <iostream>
#include <new>
#include <set>
#include <thread>
#include <vector>
#include <windows.h>

import draw3.contact_input;
import draw3.ink_prediction;
import draw3.realtime_stylus;

int RunHighlighterGeometryTests();
int RunRuntimeBenchmark(const wchar_t* applicationPath, const wchar_t* reportPath);

namespace
{
	std::atomic<uint64_t> gAllocationCount = 0;

	struct TestState
	{
		int failures = 0;

		void Check(bool condition, const char* expression, int line)
		{
			if (condition) return;
			++failures;
			std::cerr << "FAILED line " << line << ": " << expression << std::endl;
		}
	};

#define TEST_CHECK(state, expression) (state).Check(!!(expression), #expression, __LINE__)

	draw3::ContactSnapshot MakeSnapshot(uint32_t seed, draw3::ContactPhase phase = draw3::ContactPhase::Down)
	{
		LARGE_INTEGER counter = {};
		QueryPerformanceCounter(&counter);
		draw3::ContactSnapshot snapshot;
		snapshot.position = { static_cast<float>(seed), static_cast<float>(seed * 2u) };
		snapshot.pressure = static_cast<float>(seed);
		snapshot.tilt = static_cast<float>(seed) + 0.25f;
		snapshot.orientation = static_cast<float>(seed) + 0.5f;
		snapshot.isInvertedCursor = (seed & 1u) != 0;
		snapshot.contactSize = { 8.0f, 8.0f };
		snapshot.qpc = counter.QuadPart;
		snapshot.phase = phase;
		return snapshot;
	}

	void CheckSnapshotStylusState(const draw3::ContactSnapshot& snapshot, TestState& state)
	{
		TEST_CHECK(state, snapshot.tilt == snapshot.pressure + 0.25f);
		TEST_CHECK(state, snapshot.orientation == snapshot.pressure + 0.5f);
		TEST_CHECK(state, snapshot.isInvertedCursor ==
			((static_cast<uint32_t>(snapshot.pressure) & 1u) != 0));
	}

	bool NearlyEqual(float left, float right, float tolerance = 0.0001f)
	{
		return std::abs(left - right) <= tolerance;
	}

	std::vector<draw3::ContactHandle> DrainDowns(
		draw3::ContactInputCoordinator& input, size_t expectedCount, TestState& state)
	{
		std::vector<draw3::ContactHandle> handles;
		handles.reserve(expectedCount);
		for (size_t index = 0; index < expectedCount; ++index)
		{
			draw3::ContactRecord* record = nullptr;
			TEST_CHECK(state, input.TryDequeue(record));
			TEST_CHECK(state, record != nullptr);
			if (record)
			{
				CheckSnapshotStylusState(record->DownSnapshot(), state);
				handles.push_back({ record, record->Generation() });
			}
		}
		return handles;
	}

	void FinishAndRecycle(
		draw3::ContactInputCoordinator& input, const std::vector<draw3::ContactHandle>& handles,
		TestState& state, draw3::ContactPhase terminal = draw3::ContactPhase::Up)
	{
		for (const draw3::ContactHandle handle : handles)
		{
			draw3::ContactSnapshot snapshot = MakeSnapshot(handle.record->ContactId(), terminal);
			const bool closed = terminal == draw3::ContactPhase::Cancelled
				? input.PublishCancelled(handle.record->TabletContextId(), handle.record->ContactId(), snapshot)
				: input.PublishUp(handle.record->TabletContextId(), handle.record->ContactId(), snapshot);
			TEST_CHECK(state, closed);
			draw3::ContactSnapshot observed;
			TEST_CHECK(state, input.TryReadSnapshot(handle, observed));
			TEST_CHECK(state, observed.phase == terminal);
			CheckSnapshotStylusState(observed, state);
			input.Recycle(handle);
		}
	}

	void TestConcurrentDownUniqueness(TestState& state)
	{
		constexpr size_t kProducerCount = 32;
		constexpr size_t kRoundCount = 16;
		draw3::ContactInputCoordinator input(kProducerCount);
		for (size_t round = 0; round < kRoundCount; ++round)
		{
			std::array<bool, kProducerCount> results = {};
			std::vector<std::thread> producers;
			producers.reserve(kProducerCount);
			std::atomic<bool> start = false;
			for (size_t index = 0; index < kProducerCount; ++index)
			{
				producers.emplace_back([&, index]
					{
						while (!start.load(std::memory_order_acquire)) YieldProcessor();
						const uint32_t contactId =
							static_cast<uint32_t>(round * kProducerCount + index + 1);
						results[index] = input.PublishDown(
							7, contactId, draw3::InputDeviceType::Touch, MakeSnapshot(contactId));
					});
			}
			start.store(true, std::memory_order_release);
			for (std::thread& producer : producers) producer.join();
			for (bool result : results) TEST_CHECK(state, result);

			const std::vector<draw3::ContactHandle> handles =
				DrainDowns(input, kProducerCount, state);
			std::set<draw3::ContactRecord*> uniquePointers;
			for (draw3::ContactHandle handle : handles) uniquePointers.insert(handle.record);
			TEST_CHECK(state, uniquePointers.size() == kProducerCount);
			TEST_CHECK(state, input.DiagnosticsSnapshot().occupiedSlots == kProducerCount);
			FinishAndRecycle(input, handles, state);
			TEST_CHECK(state, input.DiagnosticsSnapshot().occupiedSlots == 0);
		}
	}

	void TestCapacityBoundariesAndReuse(TestState& state)
	{
		for (const size_t requestedCapacity : { size_t{ 32 }, size_t{ 64 }, size_t{ 65 } })
		{
			draw3::ContactInputCoordinator input(requestedCapacity);
			const size_t actualCapacity = input.DiagnosticsSnapshot().slotCapacity;
			TEST_CHECK(state, actualCapacity == (requestedCapacity <= 32 ? 32 :
				requestedCapacity <= 64 ? 64 : 96));
			for (size_t index = 0; index < actualCapacity; ++index)
			{
				TEST_CHECK(state, input.PublishDown(11, static_cast<uint32_t>(index + 1),
					draw3::InputDeviceType::Touch, MakeSnapshot(static_cast<uint32_t>(index + 1))));
			}
			TEST_CHECK(state, !input.PublishDown(11, 10000, draw3::InputDeviceType::Touch, MakeSnapshot(10000)));
			const std::vector<draw3::ContactHandle> handles = DrainDowns(input, actualCapacity, state);
			const draw3::ContactHandle staleHandle = handles.front();
			FinishAndRecycle(input, handles, state);

			TEST_CHECK(state, input.PublishDown(11, 20000, draw3::InputDeviceType::Pen, MakeSnapshot(20000)));
			std::vector<draw3::ContactHandle> reused = DrainDowns(input, 1, state);
			TEST_CHECK(state, reused.front().record == staleHandle.record);
			TEST_CHECK(state, reused.front().generation != staleHandle.generation);
			draw3::ContactSnapshot ignored;
			TEST_CHECK(state, !input.TryReadSnapshot(staleHandle, ignored));
			input.Recycle(staleHandle); // 旧 generation 不能释放新一代 slot。
			TEST_CHECK(state, input.DiagnosticsSnapshot().occupiedSlots == 1);
			FinishAndRecycle(input, reused, state);
			input.Recycle(reused.front()); // 重复回收不能再次置位或破坏计数。
			TEST_CHECK(state, input.DiagnosticsSnapshot().occupiedSlots == 0);
		}
	}

	void TestPublishDownDoesNotAllocate(TestState& state)
	{
		draw3::ContactInputCoordinator input(32);
		const uint64_t before = gAllocationCount.load(std::memory_order_relaxed);
		const bool published = input.PublishDown(
			1, 1, draw3::InputDeviceType::Pen, MakeSnapshot(1));
		const uint64_t after = gAllocationCount.load(std::memory_order_relaxed);
		TEST_CHECK(state, published);
		TEST_CHECK(state, after == before);
		const std::vector<draw3::ContactHandle> handles = DrainDowns(input, 1, state);
		FinishAndRecycle(input, handles, state);
	}

	void TestMoveUpRaceAndShutdown(TestState& state)
	{
		draw3::ContactInputCoordinator input(32);
		TEST_CHECK(state, input.PublishDown(20, 1, draw3::InputDeviceType::Pen, MakeSnapshot(1)));
		TEST_CHECK(state, input.PublishUp(20, 1, MakeSnapshot(2, draw3::ContactPhase::Up)));
		std::vector<draw3::ContactHandle> handles = DrainDowns(input, 1, state);
		draw3::ContactSnapshot terminal;
		TEST_CHECK(state, input.TryReadSnapshot(handles.front(), terminal));
		TEST_CHECK(state, terminal.phase == draw3::ContactPhase::Up);
		input.Recycle(handles.front()); // 消费者出队前到达的 Up 仍保留同一 generation 的终态。

		TEST_CHECK(state, input.PublishDown(21, 1, draw3::InputDeviceType::Touch, MakeSnapshot(1)));
		handles = DrainDowns(input, 1, state);
		std::atomic<bool> start = false;
		std::thread mover([&]
			{
				while (!start.load(std::memory_order_acquire)) YieldProcessor();
				for (uint32_t index = 0; index < 2000; ++index)
					input.PublishMove(21, 1, MakeSnapshot(index + 2, draw3::ContactPhase::Move));
			});
		std::thread closer([&]
			{
				while (!start.load(std::memory_order_acquire)) YieldProcessor();
				input.PublishUp(21, 1, MakeSnapshot(4000, draw3::ContactPhase::Up));
			});
		start.store(true, std::memory_order_release);
		mover.join();
		closer.join();
		TEST_CHECK(state, input.TryReadSnapshot(handles.front(), terminal));
		TEST_CHECK(state, terminal.phase == draw3::ContactPhase::Up);
		TEST_CHECK(state, !input.PublishMove(21, 1, MakeSnapshot(5000, draw3::ContactPhase::Move)));
		input.Recycle(handles.front());

		for (uint32_t index = 0; index < 4; ++index)
			TEST_CHECK(state, input.PublishDown(22, index + 1,
				draw3::InputDeviceType::Touch, MakeSnapshot(index + 1)));
		handles = DrainDowns(input, 4, state);
		input.CloseAllProducerContacts(MakeSnapshot(9).qpc);
		for (draw3::ContactHandle handle : handles)
		{
			TEST_CHECK(state, input.TryReadSnapshot(handle, terminal));
			TEST_CHECK(state, terminal.phase == draw3::ContactPhase::Cancelled);
			input.Recycle(handle);
		}
		TEST_CHECK(state, input.DiagnosticsSnapshot().occupiedSlots == 0);
	}

	void TestWakeProtocols(TestState& state)
	{
		draw3::ContactInputCoordinator input(32);
		std::atomic<bool> waiterReturned = false;
		std::atomic<bool> gotControlWake = false;
		std::thread idleWaiter([&]
			{
				draw3::ContactRecord* record = reinterpret_cast<draw3::ContactRecord*>(uintptr_t{ 1 });
				input.WaitDequeue(record);
				gotControlWake.store(record == nullptr, std::memory_order_release);
				waiterReturned.store(true, std::memory_order_release);
			});
		TEST_CHECK(state, input.PublishControlWake());
		idleWaiter.join();
		TEST_CHECK(state, waiterReturned.load(std::memory_order_acquire));
		TEST_CHECK(state, gotControlWake.load(std::memory_order_acquire));
		input.AcknowledgeControlWake();

		uint64_t generation = input.CaptureWakeGeneration();
		std::atomic<bool> downWoke = false;
		std::thread activeWaiter([&]
			{
				downWoke.store(input.WaitForWake(generation, 1000.0), std::memory_order_release);
			});
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		TEST_CHECK(state, input.PublishDown(31, 1, draw3::InputDeviceType::Pen, MakeSnapshot(1)));
		activeWaiter.join();
		TEST_CHECK(state, downWoke.load(std::memory_order_acquire));
		std::vector<draw3::ContactHandle> handles = DrainDowns(input, 1, state);

		generation = input.CaptureWakeGeneration();
		TEST_CHECK(state, input.PublishMove(31, 1, MakeSnapshot(2, draw3::ContactPhase::Move)));
		TEST_CHECK(state, !input.WaitForWake(generation, 2.0)); // Move 只合并 snapshot，不驱动额外帧。
		generation = input.CaptureWakeGeneration();
		TEST_CHECK(state, input.PublishCancelled(31, 1, MakeSnapshot(3, draw3::ContactPhase::Cancelled)));
		TEST_CHECK(state, input.WaitForWake(generation, 50.0));
		input.Recycle(handles.front());

		TEST_CHECK(state, input.PublishDown(31, 2, draw3::InputDeviceType::Pen, MakeSnapshot(4)));
		handles = DrainDowns(input, 1, state);
		generation = input.CaptureWakeGeneration();
		TEST_CHECK(state, input.PublishUp(31, 2, MakeSnapshot(5, draw3::ContactPhase::Up)));
		TEST_CHECK(state, input.WaitForWake(generation, 50.0));
		input.Recycle(handles.front());
	}

	void TestRtsStylusConversions(TestState& state)
	{
		const float pressure4095 = draw3::NormalizeRtsPressureForTesting(2048, 0, 4095);
		const float pressure8191 = draw3::NormalizeRtsPressureForTesting(4096, 0, 8191);
		TEST_CHECK(state, NearlyEqual(pressure4095, pressure8191));
		TEST_CHECK(state, draw3::NormalizeRtsPressureForTesting(-10, 0, 4095) == 0.0f);
		TEST_CHECK(state, draw3::NormalizeRtsPressureForTesting(5000, 0, 4095) == 1.0f);
		TEST_CHECK(state, draw3::NormalizeRtsPressureForTesting(10, 20, 20) < 0.0f);

		constexpr float pi = 3.14159265358979323846f;
		const float degrees = draw3::DecodeRtsAngleForTesting(
			9000, draw3::RtsAngleUnitForTesting::Degrees, 100.0f);
		const float radians = draw3::DecodeRtsAngleForTesting(
			15708, draw3::RtsAngleUnitForTesting::Radians, 10000.0f);
		TEST_CHECK(state, NearlyEqual(degrees, pi * 0.5f));
		TEST_CHECK(state, NearlyEqual(radians, pi * 0.5f, 0.0002f));
		TEST_CHECK(state, draw3::DecodeRtsAngleForTesting(
			9000, draw3::RtsAngleUnitForTesting::Unsupported, 100.0f) < 0.0f);
		TEST_CHECK(state, draw3::DecodeRtsAngleForTesting(
			9000, draw3::RtsAngleUnitForTesting::Degrees, 0.0f) < 0.0f);

		const draw3::RtsStylusAnglesForTesting direct = draw3::DecodeRtsStylusAnglesForTesting(
			true, pi * 0.5f, pi * 0.25f, 0.0f, 0.0f);
		TEST_CHECK(state, NearlyEqual(direct.tilt, pi * 0.25f));
		TEST_CHECK(state, NearlyEqual(direct.orientation, pi * 1.5f));
		const draw3::RtsStylusAnglesForTesting xTilt = draw3::DecodeRtsStylusAnglesForTesting(
			false, 0.0f, 0.0f, pi * 0.25f, 0.0f);
		TEST_CHECK(state, NearlyEqual(xTilt.tilt, pi * 0.25f));
		TEST_CHECK(state, NearlyEqual(xTilt.orientation, 0.0f));
		const draw3::RtsStylusAnglesForTesting yTilt = draw3::DecodeRtsStylusAnglesForTesting(
			false, 0.0f, 0.0f, 0.0f, pi * 0.25f);
		TEST_CHECK(state, NearlyEqual(yTilt.tilt, pi * 0.25f));
		TEST_CHECK(state, NearlyEqual(yTilt.orientation, pi * 1.5f));
		const draw3::RtsStylusAnglesForTesting unknown = draw3::DecodeRtsStylusAnglesForTesting(
			false, 0.0f, 0.0f, NAN, NAN);
		TEST_CHECK(state, unknown.tilt < 0.0f);
		TEST_CHECK(state, unknown.orientation < 0.0f);
	}

	void TestInputWidthModesAndHardwarePressure(TestState& state)
	{
		const draw3::InputWidthModeSettings defaults;
		TEST_CHECK(state, defaults.mouse == draw3::InputWidthMode::SimulatedPressure);
		TEST_CHECK(state, defaults.touch == draw3::InputWidthMode::SimulatedPressure);
		TEST_CHECK(state, defaults.pen == draw3::PenInputWidthMode::HardwarePressure);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::Pen, defaults, 0.5f) == draw3::StrokeWidthMode::HardwarePressure);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::Pen, defaults, -1.0f) == draw3::StrokeWidthMode::SimulatedPressure);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::Touch, defaults, 0.5f) == draw3::StrokeWidthMode::SimulatedPressure);

		draw3::InputWidthModeSettings fixed = defaults;
		fixed.mouse = draw3::InputWidthMode::Fixed;
		fixed.touch = draw3::InputWidthMode::Fixed;
		fixed.pen = draw3::PenInputWidthMode::Fixed;
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::MouseLeft, fixed, -1.0f) == draw3::StrokeWidthMode::Fixed);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::MouseRight, fixed, -1.0f) == draw3::StrokeWidthMode::Fixed);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::Touch, fixed, -1.0f) == draw3::StrokeWidthMode::Fixed);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::Pen, fixed, 0.5f) == draw3::StrokeWidthMode::Fixed);
		draw3::InputWidthModeSettings simulated = defaults;
		simulated.pen = draw3::PenInputWidthMode::SimulatedPressure;
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(
			draw3::InputDeviceType::Pen, simulated, 0.5f) == draw3::StrokeWidthMode::SimulatedPressure);
		draw3::InputWidthModeSettingsState settingsState(fixed);
		TEST_CHECK(state, settingsState.Get() == fixed);
		TEST_CHECK(state, !settingsState.Set({
			static_cast<draw3::InputWidthMode>(99), draw3::InputWidthMode::Fixed,
			draw3::PenInputWidthMode::Fixed }));
		TEST_CHECK(state, settingsState.Get() == fixed);
		TEST_CHECK(state, settingsState.Set(defaults));
		TEST_CHECK(state, settingsState.Get() == defaults);

		TEST_CHECK(state, NearlyEqual(draw3::HardwarePressureDiameter(5.0f, 0.0f), 1.0f));
		TEST_CHECK(state, NearlyEqual(draw3::HardwarePressureDiameter(5.0f, 0.5f), 4.0f));
		TEST_CHECK(state, NearlyEqual(draw3::HardwarePressureDiameter(5.0f, 1.0f), 7.0f));
		for (const auto [pressure, expectedRadius] : {
			std::pair{ 0.0f, 0.5f }, std::pair{ 0.5f, 2.0f }, std::pair{ 1.0f, 3.5f } })
		{
			draw3::ActiveStroke stroke(5.0f, 500.0f, draw3::StrokeWidthMode::HardwarePressure);
			ink::stroke_model::Result result;
			result.position = { 10.0f, 20.0f };
			result.time = ink::stroke_model::Time(0.0);
			result.pressure = pressure;
			stroke.modeledResults.push_back(result);
			draw3::AppendNewModeledPoints(stroke);
			TEST_CHECK(state, stroke.realPoints.size() == 1);
			TEST_CHECK(state, NearlyEqual(stroke.realPoints.front().r, expectedRadius));
		}

		draw3::ActiveStroke predicted(5.0f, 500.0f, draw3::StrokeWidthMode::HardwarePressure);
		ink::stroke_model::Result realResult;
		realResult.position = { 1.0f, 1.0f };
		realResult.time = ink::stroke_model::Time(0.0);
		realResult.pressure = 1.0f;
		predicted.modeledResults.push_back(realResult);
		draw3::AppendNewModeledPoints(predicted);
		ink::stroke_model::Result predictionResult;
		predictionResult.position = { 2.0f, 1.0f };
		predictionResult.time = ink::stroke_model::Time(0.01);
		predictionResult.pressure = 0.0f;
		predicted.predictedResults.push_back(predictionResult);
		draw3::RebuildPredictedPoints(predicted);
		TEST_CHECK(state, predicted.predictedPoints.size() == 1);
		TEST_CHECK(state, NearlyEqual(
			predicted.predictedPoints.front().r, predicted.realPoints.back().r));

		draw3::ActiveStroke missingPressure(5.0f, 500.0f, draw3::StrokeWidthMode::HardwarePressure);
		ink::stroke_model::Result validPressure;
		validPressure.position = { 0.0f, 0.0f };
		validPressure.time = ink::stroke_model::Time(0.0);
		validPressure.pressure = 0.5f;
		missingPressure.modeledResults.push_back(validPressure);
		draw3::AppendNewModeledPoints(missingPressure);
		ink::stroke_model::Result unknownPressure;
		unknownPressure.position = { 100.0f, 0.0f };
		unknownPressure.time = ink::stroke_model::Time(0.1);
		unknownPressure.pressure = -1.0f;
		missingPressure.modeledResults.push_back(unknownPressure);
		draw3::AppendNewModeledPoints(missingPressure);
		TEST_CHECK(state, missingPressure.realPoints.size() == 2);
		TEST_CHECK(state, NearlyEqual(
			missingPressure.realPoints.front().r, missingPressure.realPoints.back().r));
	}

	void TestInvertedPenPolicy(TestState& state)
	{
		draw3::StrokeModelConfiguration configuration;
		TEST_CHECK(state, configuration.invertedPenEraserEnabled);
		TEST_CHECK(state, draw3::ShouldUseInvertedPenEraser(
			draw3::InputDeviceType::Pen, true, true, true));
		TEST_CHECK(state, !draw3::ShouldUseInvertedPenEraser(
			draw3::InputDeviceType::Pen, true, false, true));
		TEST_CHECK(state, !draw3::ShouldUseInvertedPenEraser(
			draw3::InputDeviceType::Pen, false, true, true));
		TEST_CHECK(state, !draw3::ShouldUseInvertedPenEraser(
			draw3::InputDeviceType::Pen, true, true, false));
		TEST_CHECK(state, !draw3::ShouldUseInvertedPenEraser(
			draw3::InputDeviceType::Touch, true, true, true));
		TEST_CHECK(state, !draw3::ShouldUseInvertedPenEraser(
			draw3::InputDeviceType::MouseLeft, true, true, true));

		const float suppressedPressure = draw3::ResolveStylusPressureForModel(
			draw3::InputDeviceType::Pen, true, 0.75f);
		TEST_CHECK(state, suppressedPressure == -1.0f);
		TEST_CHECK(state, draw3::ResolveStylusPressureForModel(
			draw3::InputDeviceType::Pen, false, 0.75f) == 0.75f);
		TEST_CHECK(state, draw3::ResolveStylusPressureForModel(
			draw3::InputDeviceType::Touch, true, 0.75f) == 0.75f);
		TEST_CHECK(state, draw3::ResolveStrokeWidthMode(draw3::InputDeviceType::Pen,
			configuration.inputWidthModes, suppressedPressure) ==
			draw3::StrokeWidthMode::SimulatedPressure);
	}
}

void* operator new(size_t size)
{
	gAllocationCount.fetch_add(1, std::memory_order_relaxed);
	if (void* memory = std::malloc(size)) return memory;
	throw std::bad_alloc();
}

void* operator new[](size_t size)
{
	return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete[](void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, size_t) noexcept
{
	std::free(memory);
}

void operator delete[](void* memory, size_t) noexcept
{
	std::free(memory);
}

int wmain(int argc, wchar_t* argv[])
{
	if (argc == 4 && wcscmp(argv[1], L"--benchmark") == 0)
		return RunRuntimeBenchmark(argv[2], argv[3]);
	TestState state;
	TestConcurrentDownUniqueness(state);
	TestCapacityBoundariesAndReuse(state);
	TestPublishDownDoesNotAllocate(state);
	TestMoveUpRaceAndShutdown(state);
	TestWakeProtocols(state);
	TestRtsStylusConversions(state);
	TestInputWidthModesAndHardwarePressure(state);
	TestInvertedPenPolicy(state);
	state.failures += RunHighlighterGeometryTests();
	if (state.failures == 0)
	{
		std::cout << "All draw3 contact input tests passed." << std::endl;
		return 0;
	}
	std::cerr << state.failures << " draw3 tests failed." << std::endl;
	return 1;
}
