#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
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
#include <variant>
#include <vector>
#include <windows.h>
#include <DirectXMath.h>

import draw3.contact_input;
import draw3.haptic_feedback;
import draw3.ink_prediction;
import draw3.realtime_stylus;

int RunHighlighterGeometryTests();
int RunLaserIncrementalCoverageTests();
int RunPenCursorTests();
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
		TEST_CHECK(state, draw3::RtsPenCursorDataInterestEnabledForTesting());
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
		TEST_CHECK(state, NearlyEqual(draw3::LaserPressureDiameter(5.0f, 0.0f), 3.25f));
		TEST_CHECK(state, NearlyEqual(draw3::LaserPressureDiameter(5.0f, 0.5f), 5.125f));
		TEST_CHECK(state, NearlyEqual(draw3::LaserPressureDiameter(5.0f, 1.0f), 7.0f));
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
		for (const auto [pressure, expectedRadius] : {
			std::pair{ 0.0f, 1.625f }, std::pair{ 0.5f, 2.5625f },
			std::pair{ 1.0f, 3.5f } })
		{
			draw3::ActiveStroke stroke(
				5.0f, 500.0f, draw3::StrokeWidthMode::LaserPressure);
			ink::stroke_model::Result result;
			result.position = { 10.0f, 20.0f };
			result.time = ink::stroke_model::Time(0.0);
			result.pressure = pressure;
			stroke.modeledResults.push_back(result);
			draw3::AppendNewModeledPoints(stroke);
			TEST_CHECK(state, stroke.realPoints.size() == 1);
			TEST_CHECK(state, NearlyEqual(stroke.realPoints.front().r, expectedRadius));
		}

		draw3::ActiveStroke invalidLaserPressure(
			5.0f, 500.0f, draw3::StrokeWidthMode::LaserPressure);
		ink::stroke_model::Result validLaserResult;
		validLaserResult.position = { 1.0f, 1.0f };
		validLaserResult.time = ink::stroke_model::Time(0.0);
		validLaserResult.pressure = 0.5f;
		invalidLaserPressure.modeledResults.push_back(validLaserResult);
		draw3::AppendNewModeledPoints(invalidLaserPressure);
		ink::stroke_model::Result invalidLaserResult = validLaserResult;
		invalidLaserResult.position = { 2.0f, 1.0f };
		invalidLaserResult.time = ink::stroke_model::Time(0.01);
		invalidLaserResult.pressure = -1.0f;
		invalidLaserPressure.modeledResults.push_back(invalidLaserResult);
		draw3::AppendNewModeledPoints(invalidLaserPressure);
		TEST_CHECK(state, invalidLaserPressure.realPoints.size() == 2);
		TEST_CHECK(state, NearlyEqual(invalidLaserPressure.realPoints[0].r,
			invalidLaserPressure.realPoints[1].r));
		ink::stroke_model::Result laserPrediction = invalidLaserResult;
		laserPrediction.position = { 3.0f, 1.0f };
		laserPrediction.time = ink::stroke_model::Time(0.02);
		laserPrediction.pressure = 1.0f;
		invalidLaserPressure.predictedResults.push_back(laserPrediction);
		draw3::RebuildPredictedPoints(invalidLaserPressure);
		TEST_CHECK(state, invalidLaserPressure.predictedPoints.size() == 1);
		TEST_CHECK(state, NearlyEqual(invalidLaserPressure.predictedPoints.front().r,
			invalidLaserPressure.realPoints.back().r));

		draw3::ActiveStroke fixedLaser(5.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		ink::stroke_model::Result fixedLaserResult = validLaserResult;
		fixedLaserResult.pressure = 1.0f;
		fixedLaser.modeledResults.push_back(fixedLaserResult);
		draw3::AppendNewModeledPoints(fixedLaser);
		TEST_CHECK(state, NearlyEqual(fixedLaser.realPoints.front().r, 2.5f));

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

		TEST_CHECK(state, draw3::ResolveLiveTipTaperDurationSeconds(
			draw3::StrokeWidthMode::HardwarePressure, 0.055) == 0.0);
		TEST_CHECK(state, NearlyEqual(static_cast<float>(
			draw3::ResolveLiveTipTaperDurationSeconds(
				draw3::StrokeWidthMode::SimulatedPressure, 0.055)), 0.055f));
		TEST_CHECK(state, NearlyEqual(static_cast<float>(
			draw3::ResolveLiveTipTaperDurationSeconds(
				draw3::StrokeWidthMode::Fixed, 0.055)), 0.055f));

		auto makeUniformStroke = [](draw3::StrokeWidthMode mode)
		{
			draw3::ActiveStroke stroke(5.0f, 500.0f, mode);
			for (int index = 0; index < 8; ++index)
			{
				ink::stroke_model::Result result;
				result.position = {
					static_cast<float>(index) * 10.0f, 0.0f };
				result.time = ink::stroke_model::Time(index * 0.01);
				result.pressure = 1.0f;
				stroke.modeledResults.push_back(result);
			}
			draw3::AppendNewModeledPoints(stroke);
			return stroke;
		};

		draw3::ActiveStroke hardwareTip = makeUniformStroke(
			draw3::StrokeWidthMode::HardwarePressure);
		const float hardwareBaseRadius = hardwareTip.realPoints.back().r;
		draw3::RebuildL0DrawPoints(hardwareTip, draw3::ResolveLiveTipTaperDurationSeconds(
			hardwareTip.widthMode, 0.055), draw3::StrokeShape::RoundCapsule, 400, 400);
		TEST_CHECK(state, !hardwareTip.l0DrawPoints.empty());
		TEST_CHECK(state, NearlyEqual(
			hardwareTip.l0DrawPoints.back().r, hardwareBaseRadius));

		draw3::ActiveStroke simulatedTip = makeUniformStroke(
			draw3::StrokeWidthMode::SimulatedPressure);
		for (draw3::InkPoint& point : simulatedTip.realPoints)
			point.r = 2.5f; // 固定稳定半径，只验证 L0 叠加笔锋。
		draw3::RebuildL0DrawPoints(simulatedTip, draw3::ResolveLiveTipTaperDurationSeconds(
			simulatedTip.widthMode, 0.055), draw3::StrokeShape::RoundCapsule, 400, 400);
		TEST_CHECK(state, !simulatedTip.l0DrawPoints.empty());
		TEST_CHECK(state, simulatedTip.l0DrawPoints.back().r < 2.5f - 0.2f);
		TEST_CHECK(state, simulatedTip.l0DrawPoints.front().r >
			simulatedTip.l0DrawPoints.back().r + 0.1f);
		for (size_t index = 1; index < simulatedTip.l0DrawPoints.size(); ++index)
		{
			const draw3::InkPoint& previous = simulatedTip.l0DrawPoints[index - 1];
			const draw3::InkPoint& current = simulatedTip.l0DrawPoints[index];
			const float segmentLength = std::hypot(
				current.x - previous.x, current.y - previous.y);
			TEST_CHECK(state, std::abs(current.r - previous.r) < segmentLength + 1e-4f);
		}
	}

	void TestInterruptedStrokeReconnectPolicy(TestState& state)
	{
		draw3::StrokeModelConfiguration configuration;
		TEST_CHECK(state, configuration.interruptedStrokeReconnectEnabled);

		std::vector<draw3::InkPoint> directionPoints{
			{ 0.0f, 0.0f, 1.0f, 0.0f },
			{ 6.0f, 0.0f, 1.0f, 0.01f },
			{ 12.0f, 0.0f, 1.0f, 0.02f }
		};
		DirectX::XMFLOAT2 direction = {};
		TEST_CHECK(state, draw3::TryGetInterruptedStrokeTailDirection(
			directionPoints, 1.0f, direction));
		TEST_CHECK(state, NearlyEqual(direction.x, 1.0f));
		TEST_CHECK(state, NearlyEqual(direction.y, 0.0f));
		directionPoints = {
			{ 0.0f, 0.0f, 1.0f, 0.0f }, { 3.0f, 0.0f, 1.0f, 0.01f }
		};
		TEST_CHECK(state, !draw3::TryGetInterruptedStrokeTailDirection(
			directionPoints, 1.0f, direction));

		std::vector<ink::stroke_model::Result> prediction(2);
		prediction[0].position = { 24.0f, 2.0f };
		prediction[0].velocity = { 100.0f, 0.0f };
		prediction[0].time = ink::stroke_model::Time(0.05);
		prediction[1].position = { 28.0f, 8.0f };
		prediction[1].velocity = { 0.0f, 200.0f };
		prediction[1].time = ink::stroke_model::Time(0.07);
		const std::vector<draw3::InkPoint> realTail{
			{ 10.0f, 0.0f, 1.0f, 0.02f }, { 20.0f, 0.0f, 1.0f, 0.04f }
		};
		const draw3::InterruptedStrokeReconnectMotion interpolatedMotion =
			draw3::ResolveInterruptedStrokeReconnectMotion(
				prediction, realTail, { 1.0f, 0.0f }, 300.0f, 0.04, 0.02, 1.0f);
		TEST_CHECK(state, interpolatedMotion.valid);
		TEST_CHECK(state, interpolatedMotion.source ==
			draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition);
		TEST_CHECK(state, NearlyEqual(interpolatedMotion.predictedDistance,
			std::sqrt(61.0f), 0.01f));
		TEST_CHECK(state, NearlyEqual(interpolatedMotion.direction.x,
			6.0f / std::sqrt(61.0f), 0.001f));
		TEST_CHECK(state, NearlyEqual(interpolatedMotion.direction.y,
			5.0f / std::sqrt(61.0f), 0.001f));
		TEST_CHECK(state, NearlyEqual(interpolatedMotion.speed,
			std::sqrt(61.0f) / 0.02f, 0.1f));
		TEST_CHECK(state, NearlyEqual(static_cast<float>(
			interpolatedMotion.forecastDurationMilliseconds), 20.0f, 0.01f));
		const draw3::InterruptedStrokeReconnectMotion extrapolatedMotion =
			draw3::ResolveInterruptedStrokeReconnectMotion(
				prediction, realTail, { 1.0f, 0.0f }, 300.0f, 0.04, 0.08, 1.0f);
		TEST_CHECK(state, extrapolatedMotion.source ==
			draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition);
		TEST_CHECK(state, NearlyEqual(extrapolatedMotion.direction.x,
			1.0f / std::sqrt(2.0f), 0.001f));
		TEST_CHECK(state, NearlyEqual(extrapolatedMotion.direction.y,
			1.0f / std::sqrt(2.0f), 0.001f));
		TEST_CHECK(state, NearlyEqual(extrapolatedMotion.speed,
			std::sqrt(128.0f) / 0.03f, 0.1f));
		TEST_CHECK(state, NearlyEqual(static_cast<float>(
			extrapolatedMotion.predictionHorizonMilliseconds), 30.0f, 0.01f));
		TEST_CHECK(state, NearlyEqual(static_cast<float>(
			extrapolatedMotion.beyondPredictionHorizonMilliseconds), 50.0f, 0.01f));
		TEST_CHECK(state, extrapolatedMotion.terminalDirectionValid);
		TEST_CHECK(state, NearlyEqual(extrapolatedMotion.terminalDirection.y, 1.0f));
		TEST_CHECK(state, NearlyEqual(extrapolatedMotion.recentInputSpeed, 300.0f));
		const draw3::InterruptedStrokeReconnectMotion fallbackMotion =
			draw3::ResolveInterruptedStrokeReconnectMotion(
				{}, realTail, { 1.0f, 0.0f }, 100000.0f, 0.04, 0.05, 1.0f);
		TEST_CHECK(state, fallbackMotion.source ==
			draw3::InterruptedStrokeReconnectMotionSource::RealTail);
		TEST_CHECK(state, NearlyEqual(fallbackMotion.speed, 500.0f));
		prediction[0].velocity = {};
		prediction[1].velocity = {};
		const draw3::InterruptedStrokeReconnectMotion positionMotion =
			draw3::ResolveInterruptedStrokeReconnectMotion(
				prediction, realTail, { 1.0f, 0.0f }, 300.0f, 0.04, 0.08, 1.0f);
		TEST_CHECK(state, positionMotion.source ==
			draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition);
		TEST_CHECK(state, !positionMotion.terminalDirectionValid);
		TEST_CHECK(state, NearlyEqual(positionMotion.predictedDistance, std::sqrt(128.0f), 0.01f));

		draw3::InterruptedStrokeReconnectInput input{
			.previousPosition = { 20.0f, 10.0f },
			.previousUpQpc = 1000,
			.newPosition = { 30.0f, 10.0f },
			.newDownQpc = 1050,
			.qpcFrequency = 1000,
			.dpiScale = 1.0f,
			.motion = {
				.valid = true,
				.source = draw3::InterruptedStrokeReconnectMotionSource::RealTail,
				.direction = { 1.0f, 0.0f },
				.directionReliable = true,
				.speed = 200.0f
			}
		};
		const draw3::InterruptedStrokeReconnectResult matched =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, matched.matched);
		TEST_CHECK(state, NearlyEqual(matched.distance, 10.0f));
		TEST_CHECK(state, NearlyEqual(matched.speedRatio, 1.0f));

		input.newDownQpc = 1080;
		input.newPosition = { 36.0f, 10.0f }; // 80ms 窗口边界仍允许续接。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);

		input.newDownQpc = 1081;
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newDownQpc = 1050;
		input.newPosition = { 28.19152f, 15.73576f }; // 10px、35°方向边界。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 28.09017f, 15.87785f }; // 10px、36°，刚超过方向上限。
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 23.5f, 10.0f }; // 70px/s，速度比下界 0.35。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 22.9f, 10.0f }; // 连同数值容差仍低于末速下界。
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.motion.speed = 80.0f;
		input.newDownQpc = 1050;
		input.newPosition = { 31.0f, 10.0f }; // 220px/s，速度比上界 2.75。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 31.51f, 10.0f }; // 超过自适应距离上界及数值容差。
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.motion.speed = 400.0f;
		input.newDownQpc = 1080;
		input.newPosition = { 52.0f, 10.0f }; // 96 DPI 下绝对距离上限 32px。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 52.51f, 10.0f };
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);

		input.motion.speed = 300.0f;
		input.newDownQpc = 1050;
		input.newPosition = { 51.0f, 10.0f };
		input.dpiScale = 1.0f;
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.dpiScale = 2.0f;
		const draw3::InterruptedStrokeReconnectResult dpiMatched =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, dpiMatched.matched);

		input.dpiScale = 1.0f;
		input.previousUpQpc = 1000;
		input.newDownQpc = 1080;
		input.motion = {
			.valid = true,
			.source = draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition,
			.direction = { 0.0f, 1.0f },
			.predictedDisplacement = { 0.0f, 60.0f },
			.terminalDirection = { 0.0f, -1.0f },
			.directionReliable = true,
			.terminalDirectionValid = true,
			.speed = 750.0f,
			.terminalSpeed = 400.0f,
			.predictedDistance = 60.0f,
			.forecastDurationMilliseconds = 80.0,
			.predictionHorizonMilliseconds = 80.0
		};
		input.newPosition = { 20.0f, 70.0f }; // 预测速度包络允许 60px 曲线续接。
		const draw3::InterruptedStrokeReconnectResult predictedLongBridge =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, predictedLongBridge.matched);
		TEST_CHECK(state, NearlyEqual(predictedLongBridge.maximumDistance, 109.0f));
		TEST_CHECK(state, NearlyEqual(predictedLongBridge.expectedDistance, 60.0f));
		TEST_CHECK(state, predictedLongBridge.terminalVelocityAngleDegrees > 170.0f);
		input.newPosition = { 20.0f, 74.51f }; // 预测落点仍在误差范围内，不再被固定 64px 提前拒绝。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 20.0f, 120.0f };
		const draw3::InterruptedStrokeReconnectResult adaptiveDistanceReject =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, !adaptiveDistanceReject.matched);
		TEST_CHECK(state, adaptiveDistanceReject.rejectReason ==
			draw3::InterruptedStrokeReconnectRejectReason::Distance);
		input.motion.recentInputSpeed = 10000.0f;
		input.newPosition = { 20.0f, 267.0f };
		const draw3::InterruptedStrokeReconnectResult absoluteAdaptiveDistanceReject =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, NearlyEqual(absoluteAdaptiveDistanceReject.maximumDistance, 256.0f));
		TEST_CHECK(state, !absoluteAdaptiveDistanceReject.matched);
		input.motion.recentInputSpeed = -1.0f;

		input.motion.predictedDisplacement = { 20.0f, 0.0f };
		input.motion.predictedDistance = 20.0f;
		input.motion.direction = { 1.0f, 0.0f };
		input.motion.speed = 250.0f;
		input.motion.terminalDirection = { -1.0f, 0.0f };
		input.newPosition = { 40.0f, 10.0f };
		const draw3::InterruptedStrokeReconnectResult predictedCurveMatch =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, predictedCurveMatch.matched); // 终点切线反向不再否决预测落点。
		TEST_CHECK(state, predictedCurveMatch.terminalVelocityAngleDegrees > 170.0f);
		input.newPosition = { 20.0f, 30.0f };
		const draw3::InterruptedStrokeReconnectResult predictedCurveReject =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, !predictedCurveReject.matched);
		TEST_CHECK(state, predictedCurveReject.rejectReason ==
			draw3::InterruptedStrokeReconnectRejectReason::ForecastError);

		input.motion.predictedDisplacement = { 1.0f, 0.0f };
		input.motion.predictedDistance = 1.0f;
		input.motion.directionReliable = false;
		input.motion.speed = 50.0f;
		input.motion.forecastDurationMilliseconds = 20.0;
		input.motion.predictionHorizonMilliseconds = 20.0;
		input.motion.beyondPredictionHorizonMilliseconds = 60.0;
		input.newPosition = { 25.0f, 10.0f };
		const draw3::InterruptedStrokeReconnectResult shortForecast =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, shortForecast.matched);
		TEST_CHECK(state, !shortForecast.directionReliable);
		TEST_CHECK(state, NearlyEqual(shortForecast.angleDegrees, 0.0f));

		input.motion.predictedDisplacement = { 20.0f, 0.0f };
		input.motion.predictedDistance = 20.0f;
		input.motion.direction = { 1.0f, 0.0f };
		input.motion.directionReliable = true;
		input.motion.speed = 1000.0f;
		input.motion.forecastDurationMilliseconds = 20.0;
		input.motion.predictionHorizonMilliseconds = 20.0;
		input.motion.beyondPredictionHorizonMilliseconds = 20.0;
		input.newDownQpc = 1040;
		input.newPosition = { 65.0f, 10.0f }; // 超出预测时域后 25px 落点误差由不确定度覆盖。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.motion.beyondPredictionHorizonMilliseconds = 0.0;
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);

		input.previousPosition = { 0.0f, 0.0f };
		input.previousUpQpc = 1000000;
		input.newDownQpc = 1070100;
		input.qpcFrequency = 1000000;
		input.dpiScale = 2.0f;
		input.newPosition = { 82.6f, 0.0f };
		input.motion = {
			.valid = true,
			.source = draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition,
			.direction = { 1.0f, 0.0f },
			.predictedDisplacement = { 19.1f, 0.0f },
			.directionReliable = true,
			.speed = 1144.0f,
			.predictedDistance = 19.1f,
			.forecastDurationMilliseconds = 16.7,
			.predictionHorizonMilliseconds = 16.7,
			.beyondPredictionHorizonMilliseconds = 53.4
		};
		const draw3::InterruptedStrokeReconnectResult longExtrapolation =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, longExtrapolation.matched);
		TEST_CHECK(state, longExtrapolation.predictionExtrapolated);
		TEST_CHECK(state, longExtrapolation.endpointError < 15.0f);

		input.newDownQpc = 1039200;
		input.newPosition = { 33.0f, 4.6f }; // 与预测弦约 8°，对应短间隔波浪线误拒样本。
		input.motion.predictedDisplacement = { 9.8f, 0.0f };
		input.motion.predictedDistance = 9.8f;
		input.motion.speed = 590.0f;
		input.motion.forecastDurationMilliseconds = 16.6;
		input.motion.predictionHorizonMilliseconds = 16.6;
		input.motion.beyondPredictionHorizonMilliseconds = 22.6;
		const draw3::InterruptedStrokeReconnectResult shortExtrapolation =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, shortExtrapolation.matched);
		TEST_CHECK(state, shortExtrapolation.predictionExtrapolated);

		input.newDownQpc = 1022490;
		input.newPosition = { 129.25f, 0.0f }; // 高速直线只略超旧 128px 固定上限。
		input.motion.predictedDisplacement = { 117.304f, 0.0f };
		input.motion.predictedDistance = 117.304f;
		input.motion.speed = 5278.7f;
		input.motion.recentInputSpeed = 4891.36f;
		input.motion.forecastDurationMilliseconds = 22.2222;
		input.motion.predictionHorizonMilliseconds = 22.2222;
		input.motion.beyondPredictionHorizonMilliseconds = 0.2678;
		const draw3::InterruptedStrokeReconnectResult fastStraight =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, fastStraight.matched);
		TEST_CHECK(state, fastStraight.maximumDistance > 129.25f);
		TEST_CHECK(state, !fastStraight.predictionExtrapolated);

		input.newDownQpc = 1030596;
		input.newPosition = { 103.3f, 34.0f }; // 加速圆弧：方向可信，但冻结端点纵向距离偏短。
		input.motion.predictedDisplacement = { 64.2236f, 0.0f };
		input.motion.predictedDistance = 64.2236f;
		input.motion.speed = 2568.94f;
		input.motion.recentInputSpeed = 2488.06f;
		input.motion.forecastDurationMilliseconds = 25.0;
		input.motion.predictionHorizonMilliseconds = 25.0;
		input.motion.beyondPredictionHorizonMilliseconds = 5.596;
		const draw3::InterruptedStrokeReconnectResult acceleratingArc =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, acceleratingArc.matched);
		TEST_CHECK(state, acceleratingArc.predictionExtrapolated);
		TEST_CHECK(state, acceleratingArc.lateralError < acceleratingArc.maximumLateralError);

		const float degreesToRadians = 3.14159265358979323846f / 180.0f;
		input.newDownQpc = 1067856;
		const float curvedBridgeAngle = 68.0491f * degreesToRadians;
		input.newPosition = {
			48.6732f * std::cos(curvedBridgeAngle),
			48.6732f * std::sin(curvedBridgeAngle)
		};
		const float terminalDirectionAngle = (68.0491f - 30.446f) * degreesToRadians;
		input.motion = {
			.valid = true,
			.source = draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition,
			.direction = { 1.0f, 0.0f },
			.predictedDisplacement = { 13.2373f, 0.0f },
			.terminalDirection = {
				std::cos(terminalDirectionAngle), std::sin(terminalDirectionAngle) },
			.directionReliable = true,
			.terminalDirectionValid = true,
			.speed = 720.0f,
			.recentInputSpeed = 720.0f,
			.terminalSpeed = 720.0f,
			.predictedDistance = 13.2373f,
			.forecastDurationMilliseconds = 19.4444,
			.predictionHorizonMilliseconds = 19.4444,
			.beyondPredictionHorizonMilliseconds = 48.4117
		};
		const draw3::InterruptedStrokeReconnectResult terminalDirectionArc =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, terminalDirectionArc.matched);
		TEST_CHECK(state, terminalDirectionArc.predictionExtrapolated);
		TEST_CHECK(state, terminalDirectionArc.selectedTerminalDirectionCorridor);
		TEST_CHECK(state, terminalDirectionArc.angleDegrees >
			draw3::kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees);
		TEST_CHECK(state, terminalDirectionArc.selectedDirectionAngleDegrees <
			draw3::kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees);

		input.motion.terminalDirection = { 1.0f, 0.0f };
		const draw3::InterruptedStrokeReconnectResult terminalDirectionHighAngle =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, !terminalDirectionHighAngle.matched);
		TEST_CHECK(state, !terminalDirectionHighAngle.selectedTerminalDirectionCorridor);

		input.motion.terminalDirection = {
			std::cos(terminalDirectionAngle), std::sin(terminalDirectionAngle) };
		input.motion.speed = 100.0f;
		input.motion.recentInputSpeed = 100.0f;
		const draw3::InterruptedStrokeReconnectResult terminalDirectionSpeedReject =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, !terminalDirectionSpeedReject.matched);

		input.newDownQpc = 1074926;
		input.newPosition = { 52.4157f, 0.0f };
		const float shortChordTerminalAngle = 22.6339f * degreesToRadians;
		input.motion = {
			.valid = true,
			.source = draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition,
			.direction = { 1.0f, 0.0f },
			.predictedDisplacement = { 7.57397f, 0.0f },
			.terminalDirection = {
				std::cos(shortChordTerminalAngle), std::sin(shortChordTerminalAngle) },
			.directionReliable = false,
			.terminalDirectionValid = true,
			.speed = 443.016f,
			.recentInputSpeed = 443.016f,
			.terminalSpeed = 443.016f,
			.predictedDistance = 7.57397f,
			.forecastDurationMilliseconds = 17.0,
			.predictionHorizonMilliseconds = 17.0,
			.beyondPredictionHorizonMilliseconds = 57.926
		};
		const draw3::InterruptedStrokeReconnectResult shortChordTerminalDirection =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, shortChordTerminalDirection.matched);
		TEST_CHECK(state, !shortChordTerminalDirection.directionReliable);
		TEST_CHECK(state, shortChordTerminalDirection.selectedTerminalDirectionCorridor);

		input.newDownQpc = 1021923;
		input.newPosition = { 22.8131f, 0.0f }; // 预测仍覆盖恢复时刻，但急弯预测位移明显偏短。
		input.motion = {
			.valid = true,
			.source = draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition,
			.direction = { 1.0f, 0.0f },
			.predictedDisplacement = { 6.52956f, 0.0f },
			.terminalDirection = { 1.0f, 0.0f },
			.directionReliable = false,
			.terminalDirectionValid = true,
			.speed = 564.059f,
			.recentInputSpeed = 564.059f,
			.terminalSpeed = 346.442f,
			.predictedDistance = 6.52956f,
			.forecastDurationMilliseconds = 21.9228,
			.predictionHorizonMilliseconds = 25.0,
			.beyondPredictionHorizonMilliseconds = 0.0
		};
		const draw3::InterruptedStrokeReconnectResult inHorizonShortChord =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, inHorizonShortChord.matched);
		TEST_CHECK(state, !inHorizonShortChord.predictionExtrapolated);
		TEST_CHECK(state, inHorizonShortChord.selectedTerminalDirectionCorridor);
		TEST_CHECK(state, inHorizonShortChord.selectedInHorizonTerminalDirectionCorridor);

		const float inHorizonChordAngle = 30.3733f * degreesToRadians;
		input.newDownQpc = 1030276;
		input.newPosition = { 42.195f, 0.0f };
		input.motion = {
			.valid = true,
			.source = draw3::InterruptedStrokeReconnectMotionSource::PredictionPosition,
			.direction = { std::cos(inHorizonChordAngle), std::sin(inHorizonChordAngle) },
			.predictedDisplacement = {
				8.89835f * std::cos(inHorizonChordAngle),
				8.89835f * std::sin(inHorizonChordAngle) },
			.terminalDirection = { 1.0f, 0.0f },
			.directionReliable = true,
			.terminalDirectionValid = true,
			.speed = 795.802f,
			.recentInputSpeed = 795.802f,
			.terminalSpeed = 303.067f,
			.predictedDistance = 8.89835f,
			.forecastDurationMilliseconds = 30.2755,
			.predictionHorizonMilliseconds = 30.5556,
			.beyondPredictionHorizonMilliseconds = 0.0
		};
		const draw3::InterruptedStrokeReconnectResult inHorizonCurve =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, inHorizonCurve.matched);
		TEST_CHECK(state, inHorizonCurve.selectedInHorizonTerminalDirectionCorridor);
		TEST_CHECK(state, inHorizonCurve.selectedDirectionAngleDegrees < 1.0f);

		input.newDownQpc = 1035000;
		input.motion.forecastDurationMilliseconds = 35.0;
		input.motion.predictionHorizonMilliseconds = 40.0;
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newDownQpc = 1035001;
		input.motion.forecastDurationMilliseconds = 35.001;
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newDownQpc = 1030276;
		input.motion.forecastDurationMilliseconds = 30.2755;
		input.motion.predictionHorizonMilliseconds = 30.5556;
		const float acceptedInHorizonTerminalAngle = 14.9f * degreesToRadians;
		input.motion.terminalDirection = {
			std::cos(acceptedInHorizonTerminalAngle),
			std::sin(acceptedInHorizonTerminalAngle) };
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		const float excessiveInHorizonTerminalAngle = 15.1f * degreesToRadians;
		input.motion.terminalDirection = {
			std::cos(excessiveInHorizonTerminalAngle),
			std::sin(excessiveInHorizonTerminalAngle) };
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.motion.terminalDirection = { 1.0f, 0.0f };
		input.newPosition = { 35.0f, 0.0f };
		input.motion.speed = 580.0f;
		input.motion.recentInputSpeed = 580.0f;
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.motion.speed = 570.0f;
		input.motion.recentInputSpeed = 570.0f;
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);

		input.newDownQpc = 1039388;
		input.newPosition = { 36.52f, 5.10f }; // 慢到快波浪线：输入末速补足预测平均速度的滞后。
		input.motion.predictedDisplacement = { 10.2821f, 0.0f };
		input.motion.predictedDistance = 10.2821f;
		input.motion.direction = { 1.0f, 0.0f };
		input.motion.directionReliable = true;
		input.motion.speed = 336.505f;
		input.motion.recentInputSpeed = 626.65f;
		input.motion.forecastDurationMilliseconds = 30.5556;
		input.motion.predictionHorizonMilliseconds = 30.5556;
		input.motion.beyondPredictionHorizonMilliseconds = 8.8324;
		input.motion.terminalDirection = {};
		input.motion.terminalDirectionValid = false;
		input.motion.terminalSpeed = -1.0f;
		const draw3::InterruptedStrokeReconnectResult acceleratingWave =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, acceleratingWave.matched);
		TEST_CHECK(state, acceleratingWave.predictionExtrapolated);
		TEST_CHECK(state, acceleratingWave.speedRatio <
			draw3::kInterruptedStrokeReconnectMaximumSpeedRatio);

		input.newDownQpc = 1030229;
		input.newPosition = { 21.03f, 0.0f };
		input.motion.predictedDisplacement = { 0.0f, 5.6068f };
		input.motion.predictedDistance = 5.6068f;
		input.motion.direction = { 0.0f, 1.0f };
		input.motion.directionReliable = false; // 预测弦不足 4px*dpi 时不得用加速走廊放行。
		input.motion.speed = 403.69f;
		input.motion.recentInputSpeed = 696.926f;
		input.motion.forecastDurationMilliseconds = 13.8889;
		input.motion.predictionHorizonMilliseconds = 13.8889;
		input.motion.beyondPredictionHorizonMilliseconds = 16.3401;
		const draw3::InterruptedStrokeReconnectResult unreliableAcceleration =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, !unreliableAcceleration.matched);
		TEST_CHECK(state, !unreliableAcceleration.predictionExtrapolated);

		input.newDownQpc = 1037900;
		input.newPosition = { -2.2f, 28.6f }; // 预测弦反向超过 90°，不得因外推而放行。
		input.motion.predictedDisplacement = { 19.0f, 0.0f };
		input.motion.predictedDistance = 19.0f;
		input.motion.direction = { 1.0f, 0.0f };
		input.motion.directionReliable = true;
		input.motion.speed = 1142.8f;
		input.motion.recentInputSpeed = -1.0f;
		input.motion.forecastDurationMilliseconds = 16.7;
		input.motion.predictionHorizonMilliseconds = 16.7;
		input.motion.beyondPredictionHorizonMilliseconds = 21.2;
		const draw3::InterruptedStrokeReconnectResult highAngleExtrapolation =
			draw3::EvaluateInterruptedStrokeReconnect(input);
		TEST_CHECK(state, !highAngleExtrapolation.matched);
		TEST_CHECK(state, !highAngleExtrapolation.predictionExtrapolated);

		input.previousPosition = { 20.0f, 10.0f };
		input.previousUpQpc = 1000;
		input.qpcFrequency = 1000;
		input.dpiScale = 1.0f;
		input.motion.predictedDisplacement = { 20.0f, 0.0f };
		input.motion.predictedDistance = 20.0f;
		input.motion.direction = { 1.0f, 0.0f };
		input.motion.directionReliable = true;
		input.motion.speed = 500.0f;
		input.motion.forecastDurationMilliseconds = 20.0;
		input.motion.predictionHorizonMilliseconds = 20.0;
		input.motion.beyondPredictionHorizonMilliseconds = 0.0;
		input.newDownQpc = 1040;
		input.newPosition = { 51.49f, 10.0f }; // 落点误差边界允许 0.5px 数值余量。
		TEST_CHECK(state, draw3::EvaluateInterruptedStrokeReconnect(input).matched);
		input.newPosition = { 51.51f, 10.0f };
		TEST_CHECK(state, !draw3::EvaluateInterruptedStrokeReconnect(input).matched);

		const draw3::InterruptedStrokeReconnectIdentity identity{
			draw3::InputDeviceType::Pen, 0, 0,
			draw3::StrokeWidthMode::HardwarePressure, false, false
		};
		draw3::InterruptedStrokeReconnectIdentity changed = identity;
		TEST_CHECK(state, draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		for (const draw3::InputDeviceType deviceType : {
			draw3::InputDeviceType::Touch, draw3::InputDeviceType::Pen,
			draw3::InputDeviceType::MouseLeft, draw3::InputDeviceType::MouseRight })
		{
			for (uint32_t tool = 0; tool < 3; ++tool)
			{
				const draw3::InterruptedStrokeReconnectIdentity supported{
					deviceType, tool, tool, draw3::StrokeWidthMode::Fixed, false, false
				};
				TEST_CHECK(state, draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(
					supported, supported));
			}
		}
		changed.deviceType = draw3::InputDeviceType::Touch;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.selectedTool = 1;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.tool = 2;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.widthMode = draw3::StrokeWidthMode::Fixed;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.invertedCursor = true;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.suppressPressure = true;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		TEST_CHECK(state, draw3::kMaximumInterruptedStrokeReconnectCandidates == 8);
		TEST_CHECK(state, draw3::kInterruptedStrokeReconnectSimulationMinimumDropMs <
			draw3::kInterruptedStrokeReconnectSimulationMaximumDropMs);
		TEST_CHECK(state, draw3::kInterruptedStrokeReconnectSimulationMaximumDropMs <
			static_cast<uint32_t>(draw3::kInterruptedStrokeReconnectWindowSeconds * 1000.0));
		TEST_CHECK(state, draw3::GetInterruptedStrokeReconnectEvictionCount(8) == 0);
		TEST_CHECK(state, draw3::GetInterruptedStrokeReconnectEvictionCount(9) == 1);
		TEST_CHECK(state, draw3::GetInterruptedStrokeReconnectEvictionCount(16) == 8);
		TEST_CHECK(state, !draw3::IsInterruptedStrokeReconnectExpired(1000, 999));
		TEST_CHECK(state, draw3::IsInterruptedStrokeReconnectExpired(1000, 1000));

		draw3::InterruptedStrokeReconnectResult fartherFromForecast = matched;
		fartherFromForecast.matchScore = matched.matchScore + 0.1f;
		TEST_CHECK(state, draw3::IsBetterInterruptedStrokeReconnectMatch(
			matched, 900, fartherFromForecast, 1000));
		draw3::InterruptedStrokeReconnectResult straighter = matched;
		straighter.matchScore = matched.matchScore;
		straighter.angleDegrees = matched.angleDegrees - 1.0f;
		straighter.selectedDirectionAngleDegrees = matched.selectedDirectionAngleDegrees - 1.0f;
		TEST_CHECK(state, draw3::IsBetterInterruptedStrokeReconnectMatch(
			straighter, 900, matched, 1000));
		draw3::InterruptedStrokeReconnectResult same = matched;
		TEST_CHECK(state, draw3::IsBetterInterruptedStrokeReconnectMatch(
			same, 1100, matched, 1000));
	}

	void TestInterruptedStrokeReconnectModelLifecycle(TestState& state)
	{
		draw3::StrokeModelConfiguration configuration = draw3::CreateStrokeModelConfiguration(96);
		using ink::stroke_model::Input;
		using ink::stroke_model::Time;
		using ink::stroke_model::Vec2;
		for (uint32_t tool = 0; tool < 3; ++tool)
		{
			auto params = configuration.modelParams;
			const bool eraser = tool == 2;
			if (eraser)
				params.prediction_params = ink::stroke_model::DisabledPredictorParams{};
			else
				draw3::ApplyPredictionMode(params, configuration.kalmanPredictorParams);
			draw3::ActiveStroke stroke(tool == 0 ? 5.0f : 50.0f, configuration.expectedSpeed);
			TEST_CHECK(state, stroke.modeler.Reset(params).ok());

			for (const Input input : {
				Input{ .event_type = Input::EventType::kDown, .position = Vec2(0.0f, 0.0f), .time = Time(0.0) },
				Input{ .event_type = Input::EventType::kMove, .position = Vec2(10.0f, 0.0f), .time = Time(0.02) },
				Input{ .event_type = Input::EventType::kMove, .position = Vec2(20.0f, 0.0f), .time = Time(0.04) }, // 暂留 Up。
				Input{ .event_type = Input::EventType::kMove, .position = Vec2(30.0f, 0.0f), .time = Time(0.06) }, // 新 Down 续作 Move。
				Input{ .event_type = Input::EventType::kUp, .position = Vec2(40.0f, 0.0f), .time = Time(0.08) }
				})
			{
				TEST_CHECK(state, stroke.modeler.Update(input, stroke.modeledResults).ok());
				draw3::AppendNewModeledPoints(stroke, 500.0f);
			}
			TEST_CHECK(state, stroke.realPoints.size() >= 5);
			TEST_CHECK(state, stroke.realPoints.back().x > 30.0f);
			if (eraser)
			{
				TEST_CHECK(state, std::holds_alternative<
					ink::stroke_model::DisabledPredictorParams>(params.prediction_params));
				std::vector<ink::stroke_model::Result> predictedResults;
				TEST_CHECK(state, predictedResults.empty());
			}
		}
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

	void TestHapticFeedbackContracts(TestState& state)
	{
		TEST_CHECK(state, draw3::kSystemDefaultHapticIntensity < 0.0);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::InkContinuous) == 0x100B);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::PencilContinuous) == 0x100C);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::MarkerContinuous) == 0x100D);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::ChiselMarkerContinuous) == 0x100E);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::BrushContinuous) == 0x100F);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::EraserContinuous) == 0x1010);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticContinuousFeedback::GalaxyPenContinuous) == 0x1011);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Click) == 0x1003);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Press) == 0x1006);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Release) == 0x1007);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Hover) == 0x1008);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Success) == 0x1009);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Error) == 0x100A);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Collide) == 0x1012);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Align) == 0x1013);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Step) == 0x1014);
		TEST_CHECK(state, static_cast<uint16_t>(
			draw3::HapticDiscreteFeedback::Grow) == 0x1015);

		TEST_CHECK(state, draw3::ResolveContinuousHapticFeedback(
			draw3::HapticToolFeedback::Pen) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::ResolveContinuousHapticFeedback(
			draw3::HapticToolFeedback::Highlighter) ==
			draw3::HapticContinuousFeedback::ChiselMarkerContinuous);
		TEST_CHECK(state, draw3::ResolveContinuousHapticFeedback(
			draw3::HapticToolFeedback::Eraser) ==
			draw3::HapticContinuousFeedback::EraserContinuous);
		TEST_CHECK(state, draw3::FallbackContinuousHapticFeedback(
			draw3::HapticContinuousFeedback::EraserContinuous) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::FallbackContinuousHapticFeedback(
			draw3::HapticContinuousFeedback::PencilContinuous) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::FallbackContinuousHapticFeedback(
			draw3::HapticContinuousFeedback::MarkerContinuous) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::FallbackContinuousHapticFeedback(
			draw3::HapticContinuousFeedback::ChiselMarkerContinuous) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::FallbackContinuousHapticFeedback(
			draw3::HapticContinuousFeedback::BrushContinuous) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::FallbackContinuousHapticFeedback(
			draw3::HapticContinuousFeedback::GalaxyPenContinuous) ==
			draw3::HapticContinuousFeedback::InkContinuous);
		TEST_CHECK(state, draw3::FallbackDiscreteHapticFeedback(
			draw3::HapticDiscreteFeedback::Step) ==
			draw3::HapticDiscreteFeedback::Click);

		draw3::StrokeModelConfiguration configuration;
		TEST_CHECK(state, configuration.hapticFeedbackEnabled);
		draw3::PenHapticFeedback haptics;
		haptics.SetEnabled(false);
		TEST_CHECK(state, !haptics.IsEnabled());
		TEST_CHECK(state, !haptics.AttachPointerId(42));
		TEST_CHECK(state, !haptics.PlayDiscrete(draw3::HapticDiscreteFeedback::Step));
		TEST_CHECK(state, !haptics.TickContinuous(
			draw3::HapticContinuousFeedback::InkContinuous));
		haptics.StopFeedback();
	}

	int RunDrawingPerformanceTests()
	{
		constexpr size_t kPointCount = 4096;
		constexpr size_t kIterationCount = 31;
		std::vector<draw3::InkPoint> points;
		points.reserve(kPointCount);
		for (size_t index = 0; index < kPointCount; ++index)
		{
			const float value = static_cast<float>(index);
			points.push_back({ value * 0.75f, std::sin(value * 0.025f) * 80.0f,
				2.5f + static_cast<float>(index % 7) * 0.05f, value * 0.001f });
		}

		auto medianMicroseconds = [&](auto&& operation)
		{
			std::array<double, kIterationCount> samples = {};
			for (double& sample : samples)
			{
				const auto start = std::chrono::steady_clock::now();
				operation();
				const auto end = std::chrono::steady_clock::now();
				sample = std::chrono::duration<double, std::micro>(end - start).count();
			}
			std::sort(samples.begin(), samples.end());
			return samples[samples.size() / 2];
		};

		draw3::HighlighterGeometry highlighterGeometry;
		draw3::RebuildHighlighterGeometry(points, highlighterGeometry);
		const uint64_t allocationStart = gAllocationCount.load(std::memory_order_relaxed);
		const double highlighterMedian = medianMicroseconds([&]
			{
				draw3::RebuildHighlighterGeometry(points, highlighterGeometry);
			});
		const uint64_t highlighterAllocations =
			gAllocationCount.load(std::memory_order_relaxed) - allocationStart;
		RECT penBounds = {};
		const double penMedian = medianMicroseconds([&]
			{
				penBounds = draw3::RectFromStrokePoints(points, 4096, 2160);
			});
		RECT eraserBounds = {};
		const double eraserMedian = medianMicroseconds([&]
			{
				eraserBounds = draw3::RectFromStrokePoints(points, 4096, 2160,
					draw3::StrokeShape::RoundCapsule);
			});
		RECT laserBounds = {};
		const double laserMedian = medianMicroseconds([&]
			{
				laserBounds = draw3::RectFromLaserPoints(points, 1.0f, 4096, 2160);
			});

		std::cout << "[DrawingPerf] points=" << points.size() <<
			" highlighter_primitives=" << highlighterGeometry.primitives.size() <<
			" highlighter_allocations=" << highlighterAllocations <<
			" highlighter_median_us=" << highlighterMedian <<
			" pen_bounds_median_us=" << penMedian <<
			" eraser_bounds_median_us=" << eraserMedian <<
			" laser_bounds_median_us=" << laserMedian << std::endl;
		const bool valid = highlighterAllocations == 0 &&
			highlighterGeometry.primitives.size() == points.size() - 1 &&
			penBounds.left < penBounds.right && eraserBounds.left < eraserBounds.right &&
			laserBounds.left < laserBounds.right;
		return valid ? 0 : 1;
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
	if (argc == 2 && wcscmp(argv[1], L"--laser-incremental-only") == 0)
		return RunLaserIncrementalCoverageTests() == 0 ? 0 : 1;
	if (argc == 2 && wcscmp(argv[1], L"--drawing-perf") == 0)
		return RunDrawingPerformanceTests();
	TestState state;
	TestConcurrentDownUniqueness(state);
	TestCapacityBoundariesAndReuse(state);
	TestPublishDownDoesNotAllocate(state);
	TestMoveUpRaceAndShutdown(state);
	TestWakeProtocols(state);
	TestRtsStylusConversions(state);
	TestInputWidthModesAndHardwarePressure(state);
	TestInterruptedStrokeReconnectPolicy(state);
	TestInterruptedStrokeReconnectModelLifecycle(state);
	TestInvertedPenPolicy(state);
	TestHapticFeedbackContracts(state);
	state.failures += RunHighlighterGeometryTests();
	state.failures += RunLaserIncrementalCoverageTests();
	state.failures += RunPenCursorTests();
	if (state.failures == 0)
	{
		std::cout << "All draw3 contact input tests passed." << std::endl;
		return 0;
	}
	std::cerr << state.failures << " draw3 tests failed." << std::endl;
	return 1;
}
