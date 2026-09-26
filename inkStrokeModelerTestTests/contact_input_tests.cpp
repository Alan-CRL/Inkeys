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
#include <limits>
#include <new>
#include <set>
#include <span>
#include <thread>
#include <variant>
#include <vector>
#include <windows.h>
#include <DirectXMath.h>

import draw3.contact_input;
import draw3.haptic_feedback;
import draw3.ink_document;
import draw3.ink_prediction;
import draw3.pen_cursor;
import draw3.realtime_stylus;
import draw3.runtime_metrics;

int RunHighlighterGeometryTests();
int RunCanvasNavigationTests();
int RunDesktopAutoSaveTests();
int RunPresentationAutoSaveTests();
int RunPresentationUInkRoundTripTests();
int RunInkDocumentTests();
int RunInkHistoryTests();
int RunLaserIncrementalCoverageTests();
int RunPenCursorTests();
int RunRuntimeBenchmark(const wchar_t* applicationPath, const wchar_t* reportPath);
int RunUInkTests();
int RunThinStrokeGpuTests();

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

	class TestDrawingCursorSink final : public draw3::DrawingCursorEventSink
	{
	public:
		void NotifyTouchContactBegin() noexcept override
		{
			++touchContactBeginCount;
			mouseSample_.Clear();
		}

		void NotifyTouchContactEnd() noexcept override
		{
			++touchContactEndCount;
		}

		void PublishPenCursorSample(const draw3::DrawingCursorSample& sample) noexcept override
		{
			penSample_.Publish(sample);
		}

		void ClearPenCursorSample() noexcept override
		{
			penSample_.Clear();
		}

		void PublishMouseCursorSample(const draw3::DrawingCursorSample& sample) noexcept
		{
			mouseSample_.Publish(sample);
		}

		bool ReadPenCursorSample(draw3::DrawingCursorSample& sample) const noexcept
		{
			return penSample_.Read(sample);
		}

		bool ReadMouseCursorSample(draw3::DrawingCursorSample& sample) const noexcept
		{
			return mouseSample_.Read(sample);
		}

		int touchContactBeginCount = 0;
		int touchContactEndCount = 0;

	private:
		draw3::DrawingCursorSampleMailbox penSample_;
		draw3::DrawingCursorSampleMailbox mouseSample_;
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
		TEST_CHECK(state, snapshot.contactSize.width == 8.0f);
		TEST_CHECK(state, snapshot.contactSize.height == 8.0f);
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
		draw3::ContactInputCoordinator bounded((std::numeric_limits<size_t>::max)());
		TEST_CHECK(state, bounded.DiagnosticsSnapshot().slotCapacity == 4096);
	}

	void TestInvalidSnapshotBoundaries(TestState& state)
	{
		draw3::ContactInputCoordinator input(32);
		draw3::ContactSnapshot invalidDown = MakeSnapshot(1);
		invalidDown.position.x = (std::numeric_limits<float>::quiet_NaN)();
		TEST_CHECK(state, !input.PublishDown(
			40, 1, draw3::InputDeviceType::Touch, invalidDown));
		TEST_CHECK(state, input.DiagnosticsSnapshot().occupiedSlots == 0);
		invalidDown = MakeSnapshot(1);
		invalidDown.position.x = (std::numeric_limits<float>::max)();
		TEST_CHECK(state, !input.PublishDown(
			40, 1, draw3::InputDeviceType::Touch, invalidDown));

		const draw3::ContactSnapshot down = MakeSnapshot(2);
		TEST_CHECK(state, input.PublishDown(40, 2, draw3::InputDeviceType::Touch, down));
		const std::vector<draw3::ContactHandle> handles = DrainDowns(input, 1, state);
		draw3::ContactSnapshot invalidMove = MakeSnapshot(3, draw3::ContactPhase::Move);
		invalidMove.position.y = (std::numeric_limits<float>::infinity)();
		TEST_CHECK(state, !input.PublishMove(40, 2, invalidMove));
		draw3::ContactSnapshot observed;
		TEST_CHECK(state, input.TryReadSnapshot(handles.front(), observed));
		TEST_CHECK(state, observed.position.x == down.position.x &&
			observed.position.y == down.position.y);

		draw3::ContactSnapshot invalidUp = MakeSnapshot(4, draw3::ContactPhase::Up);
		invalidUp.position = { (std::numeric_limits<float>::quiet_NaN)(),
			(std::numeric_limits<float>::quiet_NaN)() };
		TEST_CHECK(state, input.PublishUp(40, 2, invalidUp));
		TEST_CHECK(state, input.TryReadSnapshot(handles.front(), observed));
		TEST_CHECK(state, observed.phase == draw3::ContactPhase::Up);
		TEST_CHECK(state, observed.position.x == down.position.x &&
			observed.position.y == down.position.y);
		TEST_CHECK(state, observed.qpc == invalidUp.qpc);
		input.Recycle(handles.front());
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
		TEST_CHECK(state, !input.HasPendingWork());
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
		TEST_CHECK(state, input.HasPendingWork());
		idleWaiter.join();
		TEST_CHECK(state, waiterReturned.load(std::memory_order_acquire));
		TEST_CHECK(state, gotControlWake.load(std::memory_order_acquire));
		TEST_CHECK(state, !input.HasPendingWork());
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
		TEST_CHECK(state, input.HasPendingWork());
		std::vector<draw3::ContactHandle> handles = DrainDowns(input, 1, state);
		TEST_CHECK(state, !input.HasPendingWork());

		generation = input.CaptureWakeGeneration();
		TEST_CHECK(state, input.PublishMove(31, 1, MakeSnapshot(2, draw3::ContactPhase::Move)));
		TEST_CHECK(state, !input.WaitForWake(generation, 2.0)); // Move 只合并 snapshot，不驱动额外帧。

		TEST_CHECK(state, input.PublishControlWake());
		const auto frameWaitStart = std::chrono::steady_clock::now();
		input.WaitForFrameDeadline(6.0);
		const double frameWaitMilliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - frameWaitStart).count();
		TEST_CHECK(state, frameWaitMilliseconds >= 5.0); // 已到达的控制唤醒不能突破帧截止时间。
		draw3::ContactRecord* controlWake = reinterpret_cast<draw3::ContactRecord*>(uintptr_t{ 1 });
		TEST_CHECK(state, input.TryDequeue(controlWake));
		TEST_CHECK(state, controlWake == nullptr);
		input.AcknowledgeControlWake();

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

		generation = input.CaptureWakeGeneration();
		const auto invalidWaitStart = std::chrono::steady_clock::now();
		TEST_CHECK(state, !input.WaitForWake(
			generation, (std::numeric_limits<double>::quiet_NaN)()));
		TEST_CHECK(state, !input.WaitForWake(
			generation, (std::numeric_limits<double>::infinity)()));
		TEST_CHECK(state, !input.WaitForWake(
			generation, (std::numeric_limits<double>::max)()));
		input.WaitForFrameDeadline((std::numeric_limits<double>::quiet_NaN)());
		input.WaitForFrameDeadline((std::numeric_limits<double>::infinity)());
		input.WaitForFrameDeadline((std::numeric_limits<double>::max)());
		const double invalidWaitMilliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - invalidWaitStart).count();
		TEST_CHECK(state, invalidWaitMilliseconds < 100.0);
	}

	void TestRtsStylusConversions(TestState& state)
	{
		TEST_CHECK(state, draw3::RtsPenCursorDataInterestEnabledForTesting());
		TEST_CHECK(state, draw3::RtsProductionDataInterestIsExactForTesting());
		TEST_CHECK(state, draw3::RtsContactSizePropertiesRequestedForTesting());
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

		const draw3::SizeF touchSize = draw3::DecodeRtsContactSizeForTesting(
			draw3::InputDeviceType::Touch, 120, 80, 0.25f, 0.5f);
		TEST_CHECK(state, NearlyEqual(touchSize.width, 30.0f));
		TEST_CHECK(state, NearlyEqual(touchSize.height, 40.0f));
		const draw3::SizeF penSize = draw3::DecodeRtsContactSizeForTesting(
			draw3::InputDeviceType::Pen, 120, 80, 0.25f, 0.5f);
		TEST_CHECK(state, penSize.width < 0.0f && penSize.height < 0.0f);
		const draw3::SizeF invalidRawSize = draw3::DecodeRtsContactSizeForTesting(
			draw3::InputDeviceType::Touch, 0, 80, 0.25f, 0.5f);
		TEST_CHECK(state, invalidRawSize.width < 0.0f && invalidRawSize.height < 0.0f);
		const draw3::SizeF invalidScaleSize = draw3::DecodeRtsContactSizeForTesting(
			draw3::InputDeviceType::Touch, 120, 80, 0.0f, 0.5f);
		TEST_CHECK(state, invalidScaleSize.width < 0.0f && invalidScaleSize.height < 0.0f);
	}

	void TestRtsTouchDownCursorInvalidation(TestState& state)
	{
		using draw3::DrawingCursorPointerAuthority;
		TestDrawingCursorSink sink;
		draw3::DrawingCursorSample mouseHover = {
			.x = 120.0f, .y = 240.0f, .qpc = 1, .valid = true
		};
		sink.PublishMouseCursorSample(mouseHover);
		draw3::DrawingCursorSample observedMouse;
		TEST_CHECK(state, sink.ReadMouseCursorSample(observedMouse) && observedMouse.valid);

		draw3::NotifyRtsStylusDownCursorForTesting(
			draw3::InputDeviceType::Touch, &sink);
		TEST_CHECK(state, sink.touchContactBeginCount == 1);
		TEST_CHECK(state, sink.ReadMouseCursorSample(observedMouse) && !observedMouse.valid);
		draw3::NotifyRtsTouchContactEndForTesting(
			draw3::InputDeviceType::Touch, &sink);
		TEST_CHECK(state, sink.touchContactEndCount == 1);

		const draw3::DrawingCursorAppearance eraserAppearance = {
			draw3::DrawingCursorShape::EraserGripCircle,
			50.0f, 50.0f, 1.0f, 1.0f, 1.0f
		};
		const draw3::DrawingCursorAppearance laserAppearance = {
			draw3::DrawingCursorShape::Circle,
			5.0f, 5.0f, 1.0f, 0.0f, 0.0f
		};
		draw3::DrawingCursorSample absentPen;
		TEST_CHECK(state, !draw3::ResolvePrimaryDrawingCursorVisual(
			absentPen, observedMouse, DrawingCursorPointerAuthority::Mouse,
			eraserAppearance, eraserAppearance, true, true).visible);
		TEST_CHECK(state, !draw3::ResolveLaserDrawingCursorVisual(
			absentPen, observedMouse, DrawingCursorPointerAuthority::Mouse,
			laserAppearance).visible);

		// Touch 后真实 Mouse 的下一次移动仍可重新发布并恢复应用光标。
		mouseHover.x = 300.0f;
		mouseHover.qpc = 2;
		sink.PublishMouseCursorSample(mouseHover);
		TEST_CHECK(state, sink.ReadMouseCursorSample(observedMouse) && observedMouse.valid);
		TEST_CHECK(state, draw3::ResolvePrimaryDrawingCursorVisual(
			absentPen, observedMouse, DrawingCursorPointerAuthority::Mouse,
			eraserAppearance, eraserAppearance, true, true).visible);
		TEST_CHECK(state, draw3::ResolveLaserDrawingCursorVisual(
			absentPen, observedMouse, DrawingCursorPointerAuthority::Mouse,
			laserAppearance).visible);

		// Pen/Mouse Down 不触发 Touch 通知，也不会屏蔽真实 Mouse takeover。
		draw3::NotifyRtsStylusDownCursorForTesting(
			draw3::InputDeviceType::Pen, &sink);
		draw3::NotifyRtsStylusDownCursorForTesting(
			draw3::InputDeviceType::MouseLeft, &sink);
		draw3::NotifyRtsTouchContactEndForTesting(
			draw3::InputDeviceType::Pen, &sink);
		TEST_CHECK(state, sink.touchContactBeginCount == 1);
		TEST_CHECK(state, sink.touchContactEndCount == 1);
		TEST_CHECK(state, sink.ReadMouseCursorSample(observedMouse) && observedMouse.valid);

		// Pen Up 已按原路径清除 Pen sample；随后 Touch Down 只清理 Mouse，不重新发布 Pen。
		draw3::DrawingCursorSample penContact = {
			.x = 40.0f, .y = 80.0f, .qpc = 3, .valid = true, .inContact = true
		};
		sink.PublishPenCursorSample(penContact);
		sink.ClearPenCursorSample();
		draw3::NotifyRtsStylusDownCursorForTesting(
			draw3::InputDeviceType::Touch, &sink);
		draw3::DrawingCursorSample observedPen;
		TEST_CHECK(state, sink.ReadPenCursorSample(observedPen) && !observedPen.valid);
		TEST_CHECK(state, sink.ReadMouseCursorSample(observedMouse) && !observedMouse.valid);
		TEST_CHECK(state, sink.touchContactBeginCount == 2);
	}

	void TestRtsDecoderAndBindingHotPath(TestState& state)
	{
		using Property = draw3::RtsPacketPropertyForTesting;
		const std::array penProperties = {
			Property::Y, Property::X, Property::Pressure,
			Property::Azimuth, Property::Altitude };
		const std::array<int32_t, 5> penPacket = { 20, 10, 2048, 9000, 4500 };
		const draw3::RtsDecoderResultForTesting pen = draw3::DecodeRtsContextForTesting(
			penProperties.data(), penProperties.size(), penPacket.data(), penPacket.size(),
			draw3::InputDeviceType::Pen, 0.5f, 0.25f, 2.0f, 3.0f);
		TEST_CHECK(state, pen.parsed && pen.decoded);
		TEST_CHECK(state, NearlyEqual(pen.snapshot.position.x, 5.0f));
		TEST_CHECK(state, NearlyEqual(pen.snapshot.position.y, 5.0f));
		TEST_CHECK(state, NearlyEqual(pen.snapshot.pressure, 2048.0f / 4095.0f));
		TEST_CHECK(state, NearlyEqual(pen.snapshot.tilt, 3.14159265358979323846f * 0.25f));
		TEST_CHECK(state, NearlyEqual(pen.snapshot.orientation,
			3.14159265358979323846f * 1.5f));

		const std::array touchProperties = {
			Property::X, Property::Y, Property::Width, Property::Height };
		const std::array<int32_t, 4> touchPacket = { 40, 20, 5, 4 };
		const draw3::RtsDecoderResultForTesting touch = draw3::DecodeRtsContextForTesting(
			touchProperties.data(), touchProperties.size(), touchPacket.data(), touchPacket.size(),
			draw3::InputDeviceType::Touch, 0.25f, 0.5f, 2.0f, 3.0f);
		TEST_CHECK(state, touch.parsed && touch.decoded);
		TEST_CHECK(state, NearlyEqual(touch.snapshot.position.x, 10.0f));
		TEST_CHECK(state, NearlyEqual(touch.snapshot.position.y, 10.0f));
		TEST_CHECK(state, NearlyEqual(touch.snapshot.contactSize.width, 10.0f));
		TEST_CHECK(state, NearlyEqual(touch.snapshot.contactSize.height, 12.0f));
		for (const draw3::InputDeviceType mouseDevice : {
			draw3::InputDeviceType::MouseLeft, draw3::InputDeviceType::MouseRight })
		{
			const draw3::RtsDecoderResultForTesting mouse = draw3::DecodeRtsContextForTesting(
				touchProperties.data(), touchProperties.size(), touchPacket.data(), touchPacket.size(),
				mouseDevice, 0.25f, 0.5f, 2.0f, 3.0f);
			TEST_CHECK(state, mouse.parsed && mouse.decoded);
			TEST_CHECK(state, NearlyEqual(mouse.snapshot.position.x, 10.0f));
			TEST_CHECK(state, NearlyEqual(mouse.snapshot.position.y, 10.0f));
			TEST_CHECK(state, mouse.snapshot.pressure < 0.0f);
			TEST_CHECK(state, mouse.snapshot.contactSize.width < 0.0f &&
				mouse.snapshot.contactSize.height < 0.0f);
		}

		const draw3::RtsDecoderResultForTesting mismatch = draw3::DecodeRtsContextForTesting(
			touchProperties.data(), touchProperties.size(), touchPacket.data(),
			touchPacket.size() - 1u, draw3::InputDeviceType::Touch,
			0.25f, 0.5f, 2.0f, 3.0f);
		TEST_CHECK(state, mismatch.parsed && !mismatch.decoded);
		const std::array missingY = { Property::X, Property::Pressure };
		const draw3::RtsDecoderResultForTesting missingRequired =
			draw3::DecodeRtsContextForTesting(missingY.data(), missingY.size(),
				penPacket.data(), missingY.size(), draw3::InputDeviceType::Pen,
				1.0f, 1.0f, 1.0f, 1.0f);
		TEST_CHECK(state, !missingRequired.parsed && !missingRequired.decoded);

		TEST_CHECK(state, draw3::ComputeRtsActiveBindingCapacityForTesting(-1) == 32);
		TEST_CHECK(state, draw3::ComputeRtsActiveBindingCapacityForTesting(40) == 96);
		TEST_CHECK(state, draw3::ComputeRtsActiveBindingCapacityForTesting(70) == 160);
		TEST_CHECK(state, draw3::ComputeRtsActiveBindingCapacityForTesting(100) == 224);
		TEST_CHECK(state, draw3::ComputeRtsActiveBindingCapacityForTesting(
			(std::numeric_limits<int>::max)()) == 4096);
		TEST_CHECK(state, draw3::RtsBindingBasicInvariantsForTesting());
		TEST_CHECK(state, draw3::RtsBindingNonPowerOfTwoCapacityForTesting(96));
		TEST_CHECK(state, draw3::RtsBindingNonPowerOfTwoCapacityForTesting(160));
		TEST_CHECK(state, draw3::RtsBindingNonPowerOfTwoCapacityForTesting(224));
		TEST_CHECK(state, draw3::RtsBindingRepeatedLifecycleForTesting());
		TEST_CHECK(state, draw3::RtsBindingCollisionDeletionForTesting());
		TEST_CHECK(state, draw3::RtsBindingCollisionChurnForTesting());
		TEST_CHECK(state, draw3::RtsBindingCapacityExhaustionForTesting());
		TEST_CHECK(state, draw3::RtsBindingDuplicateRebindForTesting());
		TEST_CHECK(state, draw3::RtsBindingGenerationMismatchForTesting());
		TEST_CHECK(state, draw3::RtsLifecycleEnabledDisabledForTesting());
		TEST_CHECK(state, draw3::RtsLifecycleUpdateMappingForTesting());
		TEST_CHECK(state, draw3::RtsLifecycleTabletRemovedForTesting());
		TEST_CHECK(state, draw3::RtsLifecycleTabletAddedForTesting());
		TEST_CHECK(state, draw3::RtsLifecycleTabletAddedFallbackForTesting());
		TEST_CHECK(state, draw3::RtsSharedScaleCompatibilityForTesting());
		TEST_CHECK(state, draw3::RtsErrorPreservesDecoderLifecycleForTesting());
		TEST_CHECK(state, draw3::RtsInAirCacheHitMissForTesting());
		TEST_CHECK(state, draw3::RtsStateGateForTesting());
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
		TEST_CHECK(state, draw3::IsInterruptedStrokeReconnectDeviceSupported(
			draw3::InputDeviceType::Touch));
		TEST_CHECK(state, !draw3::IsInterruptedStrokeReconnectDeviceSupported(
			draw3::InputDeviceType::Pen));
		TEST_CHECK(state, !draw3::IsInterruptedStrokeReconnectDeviceSupported(
			draw3::InputDeviceType::MouseLeft));
		TEST_CHECK(state, !draw3::IsInterruptedStrokeReconnectDeviceSupported(
			draw3::InputDeviceType::MouseRight));

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
			draw3::InputDeviceType::Touch, 0, 0,
			draw3::StrokeWidthMode::Fixed, false, false
		};
		draw3::InterruptedStrokeReconnectIdentity changed = identity;
		TEST_CHECK(state, draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		for (uint32_t tool = 0; tool < 3; ++tool)
		{
			const draw3::InterruptedStrokeReconnectIdentity supported{
				draw3::InputDeviceType::Touch, tool, tool,
				draw3::StrokeWidthMode::Fixed, false, false
			};
			TEST_CHECK(state, draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(
				supported, supported));
		}
		const draw3::InterruptedStrokeReconnectIdentity invertedPenEraser{
			draw3::InputDeviceType::Pen, 0, 2,
			draw3::StrokeWidthMode::SpeedEraser, true, true
		};
		TEST_CHECK(state, draw3::IsInterruptedStrokeReconnectIdentitySupported(identity));
		TEST_CHECK(state, draw3::IsInterruptedStrokeReconnectIdentitySupported(
			invertedPenEraser));
		TEST_CHECK(state, draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(
			invertedPenEraser, invertedPenEraser));
		const draw3::InterruptedStrokeReconnectIdentity normalPenEraser{
			draw3::InputDeviceType::Pen, 2, 2,
			draw3::StrokeWidthMode::SpeedEraser, false, false
		};
		const draw3::InterruptedStrokeReconnectIdentity invertedPenInk{
			draw3::InputDeviceType::Pen, 0, 0,
			draw3::StrokeWidthMode::Fixed, true, true
		};
		TEST_CHECK(state, !draw3::IsInterruptedStrokeReconnectIdentitySupported(
			normalPenEraser));
		TEST_CHECK(state, !draw3::IsInterruptedStrokeReconnectIdentitySupported(
			invertedPenInk));
		changed.deviceType = draw3::InputDeviceType::Pen;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		for (const draw3::InputDeviceType unsupportedDeviceType : {
			draw3::InputDeviceType::MouseLeft, draw3::InputDeviceType::MouseRight })
		{
			const draw3::InterruptedStrokeReconnectIdentity unsupported{
				unsupportedDeviceType, 0, 0, draw3::StrokeWidthMode::Fixed, false, false
			};
			TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(
				unsupported, unsupported));
		}
		changed = identity;
		changed.selectedTool = 1;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.tool = 2;
		TEST_CHECK(state, !draw3::AreInterruptedStrokeReconnectIdentitiesCompatible(identity, changed));
		changed = identity;
		changed.widthMode = draw3::StrokeWidthMode::SimulatedPressure;
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

	void TestSpeedEraserOcController(TestState& state)
	{
		using draw3::SpeedEraserOcController;
		using draw3::SpeedEraserStartKind;

		SpeedEraserOcController dpiOne;
		SpeedEraserOcController dpiTwo;
		dpiOne.Reset(0.0f, 0.0f, 0.0);
		dpiTwo.Reset(0.0f, 0.0f, 0.0);
		for (int index = 1; index <= 48; ++index)
		{
			const double time = static_cast<double>(index) / 120.0;
			const float distanceDip = static_cast<float>(time * 100.0);
			dpiOne.UpdatePosition(distanceDip, 0.0f, time, 1.0f);
			dpiTwo.UpdatePosition(distanceDip * 2.0f, 0.0f, time, 2.0f);
		}
		TEST_CHECK(state, NearlyEqual(dpiOne.Diameter(), dpiTwo.Diameter(), 0.01f));
		TEST_CHECK(state, dpiOne.Diameter() > 60.0f && dpiOne.Diameter() < 100.0f);

		auto sampleRateDiameter = [](int samplesPerSecond)
		{
			SpeedEraserOcController controller;
			controller.Reset(0.0f, 0.0f, 0.0);
			const int sampleCount = samplesPerSecond / 2;
			for (int index = 1; index <= sampleCount; ++index)
			{
				const double time = static_cast<double>(index) /
					static_cast<double>(samplesPerSecond);
				controller.UpdatePosition(static_cast<float>(time * 100.0),
					0.0f, time, 1.0f);
			}
			return controller.Diameter();
		};
		const float diameter60 = sampleRateDiameter(60);
		const float diameter120 = sampleRateDiameter(120);
		const float diameter240 = sampleRateDiameter(240);
		TEST_CHECK(state, std::abs(diameter60 - diameter120) <= 3.0f);
		TEST_CHECK(state, std::abs(diameter120 - diameter240) <= 3.0f);
		TEST_CHECK(state, diameter120 > 60.0f && diameter120 < 100.0f);

		SpeedEraserOcController lowAmplitudeRecovery;
		lowAmplitudeRecovery.Reset(0.0f, 0.0f, 0.0);
		double lowAmplitudeTime = 0.010;
		lowAmplitudeRecovery.UpdatePosition(
			0.9f, 0.0f, lowAmplitudeTime, 1.0f);
		TEST_CHECK(state, lowAmplitudeRecovery.Diameter() > 21.5f &&
			lowAmplitudeRecovery.Diameter() < 22.5f);
		for (int index = 0; index < 30; ++index)
		{
			lowAmplitudeTime += 1.0 / 120.0;
			lowAmplitudeRecovery.Advance(lowAmplitudeTime);
		}
		TEST_CHECK(state, lowAmplitudeRecovery.Diameter() <= 20.05f);
		TEST_CHECK(state, !lowAmplitudeRecovery.NeedsAnimation(
			lowAmplitudeTime + 0.100));

		SpeedEraserOcController touch;
		touch.Reset(0.0f, 0.0f, 0.0, SpeedEraserStartKind::Touch);
		touch.UpdatePosition(12.0f, 0.0f, 0.012, 1.0f);
		TEST_CHECK(state, touch.TargetDiameter() <= 31.0f);
		TEST_CHECK(state, touch.Diameter() <= 31.0f);

		SpeedEraserOcController turnaround;
		turnaround.Reset(0.0f, 0.0f, 0.0);
		double time = 0.0;
		float position = 0.0f;
		for (int index = 0; index < 60; ++index)
		{
			time += 1.0 / 120.0;
			position += 400.0f / 120.0f;
			turnaround.UpdatePosition(position, 0.0f, time, 1.0f);
		}
		const float beforeTurn = turnaround.Diameter();
		float minimumAtTurn = beforeTurn;
		for (int index = 0; index < 20; ++index)
		{
			time += 1.0 / 120.0;
			position -= 400.0f / 120.0f;
			minimumAtTurn = std::min(minimumAtTurn,
				turnaround.UpdatePosition(position, 0.0f, time, 1.0f));
		}
		TEST_CHECK(state, beforeTurn > 190.0f);
		TEST_CHECK(state, minimumAtTurn >= beforeTurn - 5.0f);

		SpeedEraserOcController slowing;
		slowing.Reset(0.0f, 0.0f, 0.0);
		time = 0.0;
		position = 0.0f;
		for (int index = 0; index < 60; ++index)
		{
			time += 1.0 / 120.0;
			position += 500.0f / 120.0f;
			slowing.UpdatePosition(position, 0.0f, time, 1.0f);
		}
		const double slowdownStart = time;
		float earlySlowDiameter = slowing.Diameter();
		for (int index = 0; index < 54; ++index)
		{
			time += 1.0 / 120.0;
			position += 20.0f / 120.0f;
			const float diameter = slowing.UpdatePosition(position, 0.0f, time, 1.0f);
			if (time - slowdownStart >= 0.18 && earlySlowDiameter > 190.0f)
				earlySlowDiameter = diameter;
		}
		TEST_CHECK(state, earlySlowDiameter < 190.0f);
		TEST_CHECK(state, slowing.Diameter() <= 52.0f);

		SpeedEraserOcController coherentRelease;
		coherentRelease.Reset(0.0f, 0.0f, 0.0);
		double coherentTime = 0.0;
		float coherentPosition = 0.0f;
		for (int index = 0; index < 60; ++index)
		{
			coherentTime += 1.0 / 120.0;
			coherentPosition += 500.0f / 120.0f;
			coherentRelease.UpdatePosition(
				coherentPosition, 0.0f, coherentTime, 1.0f);
		}
		const float coherentHighDiameter = coherentRelease.Diameter();
		coherentTime += 0.065;
		coherentPosition += 0.8f;
		coherentRelease.UpdatePosition(
			coherentPosition, 0.0f, coherentTime, 1.0f);
		const double coherentCandidateTime = coherentTime;
		coherentRelease.Advance(coherentCandidateTime + 0.085);
		TEST_CHECK(state, NearlyEqual(
			coherentRelease.Diameter(), coherentHighDiameter, 0.1f));
		coherentRelease.Advance(coherentCandidateTime + 0.095);
		TEST_CHECK(state, coherentRelease.Diameter() < coherentHighDiameter - 1.0f);

		SpeedEraserOcController zeroMotionRelease;
		zeroMotionRelease.Reset(0.0f, 0.0f, 0.0);
		double zeroMotionTime = 0.0;
		float zeroMotionPosition = 0.0f;
		for (int index = 0; index < 60; ++index)
		{
			zeroMotionTime += 1.0 / 120.0;
			zeroMotionPosition += 500.0f / 120.0f;
			zeroMotionRelease.UpdatePosition(
				zeroMotionPosition, 0.0f, zeroMotionTime, 1.0f);
		}
		const float zeroMotionHighDiameter = zeroMotionRelease.Diameter();
		zeroMotionTime += 0.065;
		zeroMotionRelease.Advance(zeroMotionTime); // 没有新位移，必须走 110–140ms 确认。
		const double zeroMotionCandidateTime = zeroMotionTime;
		zeroMotionRelease.Advance(zeroMotionCandidateTime + 0.130);
		TEST_CHECK(state, NearlyEqual(
			zeroMotionRelease.Diameter(), zeroMotionHighDiameter, 0.1f));
		zeroMotionRelease.Advance(zeroMotionCandidateTime + 0.145);
		TEST_CHECK(state, zeroMotionRelease.Diameter() < zeroMotionHighDiameter - 1.0f);

		double irregularRecoveryTime = zeroMotionCandidateTime + 0.145;
		for (int index = 0; index < 250; ++index)
		{
			irregularRecoveryTime += 0.007;
			zeroMotionRelease.Advance(irregularRecoveryTime);
		}
		TEST_CHECK(state, zeroMotionRelease.Diameter() <= 20.05f);
		TEST_CHECK(state, !zeroMotionRelease.NeedsAnimation(
			irregularRecoveryTime + 0.100));

		SpeedEraserOcController finiteResumeControl;
		SpeedEraserOcController invalidResume;
		finiteResumeControl.Reset(0.0f, 0.0f, 0.0);
		invalidResume.Reset(0.0f, 0.0f, 0.0);
		finiteResumeControl.UpdatePosition(10.0f, 0.0f, 0.100, 1.0f);
		invalidResume.UpdatePosition(10.0f, 0.0f, 0.100, 1.0f);
		invalidResume.ResumeFromReconnect(12.0f, 0.0f,
			(std::numeric_limits<double>::quiet_NaN)(), 1.0f);
		finiteResumeControl.UpdatePosition(20.0f, 0.0f, 0.200, 1.0f);
		invalidResume.UpdatePosition(20.0f, 0.0f, 0.200, 1.0f);
		TEST_CHECK(state, NearlyEqual(invalidResume.TargetDiameter(),
			finiteResumeControl.TargetDiameter(), 0.01f));
		TEST_CHECK(state, NearlyEqual(invalidResume.Diameter(),
			finiteResumeControl.Diameter(), 0.01f));
		invalidResume.PauseForReconnect(0.200);
		const float invalidPauseDiameter = invalidResume.Diameter();
		invalidResume.ResumeFromReconnect(22.0f, 0.0f,
			(std::numeric_limits<double>::infinity)(), 1.0f);
		TEST_CHECK(state, invalidResume.IsPaused());
		TEST_CHECK(state, NearlyEqual(
			invalidResume.Diameter(), invalidPauseDiameter, 0.01f));
		invalidResume.ResumeFromReconnect(22.0f, 0.0f, 0.250, 1.0f);
		TEST_CHECK(state, !invalidResume.IsPaused());

		const float diameterBeforePause = turnaround.Diameter();
		turnaround.PauseForReconnect(time);
		TEST_CHECK(state, turnaround.IsPaused());
		turnaround.ResumeFromReconnect(position + 6.0f, 0.0f, time + 0.050, 1.0f);
		TEST_CHECK(state, !turnaround.IsPaused());
		TEST_CHECK(state, turnaround.Diameter() >= diameterBeforePause - 0.1f);

		for (int index = 0; index < 80; ++index)
		{
			time += 0.010;
			slowing.Advance(time);
		}
		TEST_CHECK(state, slowing.Diameter() <= 20.1f);
		TEST_CHECK(state, !slowing.NeedsAnimation(time + 0.100));

		SpeedEraserOcController shortOscillation;
		shortOscillation.Reset(0.0f, 0.0f, 0.0);
		double oscillationTime = 0.0;
		float oscillationPosition = 0.0f;
		for (int index = 0; index < 60; ++index)
		{
			oscillationTime += 1.0 / 120.0;
			oscillationPosition += 500.0f / 120.0f;
			shortOscillation.UpdatePosition(
				oscillationPosition, 0.0f, oscillationTime, 1.0f);
		}
		const float highOscillationDiameter = shortOscillation.Diameter();
		for (int index = 0; index < 600; ++index)
		{
			oscillationTime += 1.0 / 120.0;
			const float direction = ((index / 8) & 1) == 0 ? 1.0f : -1.0f;
			oscillationPosition += direction * (60.0f / 120.0f);
			shortOscillation.UpdatePosition(
				oscillationPosition, 0.0f, oscillationTime, 1.0f);
		}
		TEST_CHECK(state, highOscillationDiameter > 190.0f);
		TEST_CHECK(state, shortOscillation.Diameter() <= 65.0f);

		SpeedEraserOcController slowTouch;
		SpeedEraserOcController fastTouch;
		slowTouch.Reset(0.0f, 0.0f, 0.0, SpeedEraserStartKind::Touch);
		fastTouch.Reset(0.0f, 0.0f, 0.0, SpeedEraserStartKind::Touch);
		double touchTime = 0.0;
		for (int index = 1; index <= 60; ++index)
		{
			touchTime = static_cast<double>(index) / 120.0;
			slowTouch.UpdatePosition(static_cast<float>(touchTime * 8.0),
				0.0f, touchTime, 1.0f);
			fastTouch.UpdatePosition(static_cast<float>(touchTime * 400.0),
				0.0f, touchTime, 1.0f);
		}
		TEST_CHECK(state, slowTouch.Diameter() <= 20.1f);
		TEST_CHECK(state, fastTouch.Diameter() > 150.0f);
		slowTouch.PauseForReconnect(touchTime);
		TEST_CHECK(state, slowTouch.IsPaused() && !fastTouch.IsPaused());
		fastTouch.PauseForReconnect(touchTime);
		TEST_CHECK(state, slowTouch.IsPaused() && fastTouch.IsPaused());
		slowTouch.ResumeFromReconnect(6.0f, 0.0f, touchTime + 0.030, 1.0f);
		TEST_CHECK(state, !slowTouch.IsPaused() && fastTouch.IsPaused());
		fastTouch.ResumeFromReconnect(208.0f, 0.0f, touchTime + 0.050, 1.0f);
		TEST_CHECK(state, !slowTouch.IsPaused() && !fastTouch.IsPaused());
		TEST_CHECK(state, fastTouch.Diameter() > slowTouch.Diameter() + 100.0f);

		draw3::ActiveStroke modeledWidth(
			draw3::kSpeedEraserMinimumDiameterPx, 500.0f,
			draw3::StrokeWidthMode::SpeedEraser);
		for (int index = 0; index < 3; ++index)
		{
			ink::stroke_model::Result result;
			result.position = { static_cast<float>(index * 20), 100.0f };
			result.time = ink::stroke_model::Time(static_cast<double>(index) * 0.5);
			modeledWidth.modeledResults.push_back(result);
		}
		const draw3::SpeedEraserWidthInterval widthInterval{
			0.0, 1.0, draw3::kSpeedEraserMinimumDiameterPx,
			draw3::kSpeedEraserMaximumDiameterPx
		};
		draw3::AppendNewModeledPoints(modeledWidth, -1.0f, &widthInterval);
		TEST_CHECK(state, modeledWidth.realPoints.size() == 3);
		TEST_CHECK(state, NearlyEqual(modeledWidth.realPoints[0].r, 10.0f));
		TEST_CHECK(state, NearlyEqual(modeledWidth.realPoints[1].r, 55.0f));
		TEST_CHECK(state, NearlyEqual(modeledWidth.realPoints[2].r, 100.0f));

		std::vector<draw3::InkPoint> storedScratch;
		const std::optional<draw3::InkStroke> stored = draw3::FinalizeStoredStroke(
			modeledWidth, { draw3::StoredInkType::Eraser, 0, 1.0f, 0 },
			0.0, storedScratch);
		TEST_CHECK(state, stored.has_value());
		TEST_CHECK(state, stored && stored->Points().size() == 3);
		if (stored && stored->Points().size() == 3)
		{
			TEST_CHECK(state, NearlyEqual(stored->Points()[0].width, 20.0f));
			TEST_CHECK(state, NearlyEqual(stored->Points()[1].width, 110.0f));
			TEST_CHECK(state, NearlyEqual(stored->Points()[2].width, 200.0f));
		}

		const std::array<draw3::InkPoint, 1> smallPoint{
			draw3::InkPoint{ 256.0f, 256.0f, 10.0f, 0.0f }
		};
		const std::array<draw3::InkPoint, 1> largePoint{
			draw3::InkPoint{ 256.0f, 256.0f, 100.0f, 0.0f }
		};
		const RECT smallDirty = draw3::RectFromStrokePoints(smallPoint, 512, 512);
		const RECT largeDirty = draw3::RectFromStrokePoints(largePoint, 512, 512);
		TEST_CHECK(state, largeDirty.left < smallDirty.left);
		TEST_CHECK(state, largeDirty.top < smallDirty.top);
		TEST_CHECK(state, largeDirty.right > smallDirty.right);
		TEST_CHECK(state, largeDirty.bottom > smallDirty.bottom);
		TEST_CHECK(state, largeDirty.left == 153 && largeDirty.top == 153 &&
			largeDirty.right == 359 && largeDirty.bottom == 359);

		draw3::DrawingCursorAppearance smallCursorAppearance;
		smallCursorAppearance.shape = draw3::DrawingCursorShape::EraserGripCircle;
		smallCursorAppearance.width = 20.0f;
		smallCursorAppearance.height = 20.0f;
		smallCursorAppearance.outlineWidth = 0.8f;
		draw3::DrawingCursorAppearance largeCursorAppearance = smallCursorAppearance;
		largeCursorAppearance.width = 200.0f;
		largeCursorAppearance.height = 200.0f;
		largeCursorAppearance.outlineWidth = 8.0f;
		const draw3::DrawingCursorVisual smallCursor =
			draw3::MakeTouchEraserDrawingCursorVisual(
				256.0f, 256.0f, smallCursorAppearance);
		const draw3::DrawingCursorVisual largeCursor =
			draw3::MakeTouchEraserDrawingCursorVisual(
				256.0f, 256.0f, largeCursorAppearance);
		const RECT smallCursorDirty = draw3::DrawingCursorVisualBounds(
			smallCursor, 512, 512);
		const RECT largeCursorDirty = draw3::DrawingCursorVisualBounds(
			largeCursor, 512, 512);
		TEST_CHECK(state, smallCursor.visible && largeCursor.visible);
		TEST_CHECK(state, !draw3::AreDrawingCursorVisualsEquivalent(
			smallCursor, largeCursor));
		TEST_CHECK(state, largeCursorDirty.left < smallCursorDirty.left &&
			largeCursorDirty.top < smallCursorDirty.top &&
			largeCursorDirty.right > smallCursorDirty.right &&
			largeCursorDirty.bottom > smallCursorDirty.bottom);
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

	void TestLowSpeedStopConvergence(TestState& state)
	{
		using ink::stroke_model::DisabledPredictorParams;
		using ink::stroke_model::Input;
		using ink::stroke_model::Result;
		using ink::stroke_model::Time;
		using ink::stroke_model::Vec2;

		std::vector<Result> convergenceProbe(1);
		convergenceProbe.back().position = { 10.01f, 20.0f };
		for (const double framesPerSecond : { 30.0, 60.0, 120.0, 240.0 })
		{
			const double frameIntervalSeconds = 1.0 / framesPerSecond;
			convergenceProbe.back().velocity = {
				static_cast<float>(0.04 / frameIntervalSeconds), 0.0f };
			TEST_CHECK(state, draw3::IsModeledTipSettled(
				convergenceProbe, { 10.0f, 20.0f }, frameIntervalSeconds));
			convergenceProbe.back().velocity = {
				static_cast<float>(0.06 / frameIntervalSeconds), 0.0f };
			TEST_CHECK(state, !draw3::IsModeledTipSettled(
				convergenceProbe, { 10.0f, 20.0f }, frameIntervalSeconds));
		}
		convergenceProbe.back().velocity = {};
		convergenceProbe.back().position.x =
			(std::numeric_limits<float>::quiet_NaN)();
		TEST_CHECK(state, !draw3::IsModeledTipSettled(
			convergenceProbe, { 10.0f, 20.0f }, 1.0 / 60.0));
		TEST_CHECK(state, !draw3::IsModeledTipSettled(
			std::span<const Result>{}, { 10.0f, 20.0f }, 1.0 / 60.0));
		convergenceProbe.back().position = { 10.01f, 20.0f };
		convergenceProbe.back().velocity.x =
			(std::numeric_limits<float>::quiet_NaN)();
		TEST_CHECK(state, !draw3::IsModeledTipSettled(
			convergenceProbe, { 10.0f, 20.0f }, 1.0 / 60.0));
		convergenceProbe.back().velocity = {};
		TEST_CHECK(state, !draw3::IsModeledTipSettled(
			convergenceProbe, { 10.0f, 20.0f }, 0.0));

		draw3::ActiveStroke unsettledVisual(5.0f, 500.0f);
		unsettledVisual.l0DrawPoints.push_back({ 10.0f, 20.0f, 2.5f, 0.0f });
		unsettledVisual.previousL0DrawPoints = unsettledVisual.l0DrawPoints;
		unsettledVisual.logicalInputTime = 1.0;
		for (int frame = 0; frame < 8; ++frame)
			draw3::UpdateIdleFreezeState(unsettledVisual, false, false, 0.0);
		TEST_CHECK(state, !unsettledVisual.idleFrozen);
		TEST_CHECK(state, unsettledVisual.visualStableFrameCount == 0);
		draw3::ActiveStroke independentlySettledVisual(5.0f, 500.0f);
		independentlySettledVisual.l0DrawPoints = unsettledVisual.l0DrawPoints;
		independentlySettledVisual.previousL0DrawPoints =
			independentlySettledVisual.l0DrawPoints;
		independentlySettledVisual.logicalInputTime = 1.0;
		for (int frame = 0; frame < 4; ++frame)
			draw3::UpdateIdleFreezeState(
				independentlySettledVisual, false, true, 0.0);
		TEST_CHECK(state, independentlySettledVisual.idleFrozen);
		TEST_CHECK(state, !unsettledVisual.idleFrozen);

		const draw3::StrokeModelConfiguration configuration =
			draw3::CreateStrokeModelConfiguration(96);
		const double frameIntervalSeconds =
			1.0 / configuration.timingProfile.target_fps;
		for (const bool predictionEnabled : { true, false })
		{
			auto params = configuration.modelParams;
			if (predictionEnabled)
				draw3::ApplyPredictionMode(params, configuration.kalmanPredictorParams);
			else
				params.prediction_params = DisabledPredictorParams{};
			draw3::ActiveStroke stroke(5.0f, configuration.expectedSpeed);
			TEST_CHECK(state, stroke.modeler.Reset(params).ok());

			double inputTime = 0.0;
			TEST_CHECK(state, stroke.modeler.Update({
				.event_type = Input::EventType::kDown,
				.position = Vec2(0.0f, 0.0f),
				.time = Time(inputTime) }, stroke.modeledResults).ok());
			draw3::AppendNewModeledPoints(stroke);
			for (int sample = 1; sample <= 8; ++sample)
			{
				inputTime += frameIntervalSeconds;
				TEST_CHECK(state, stroke.modeler.Update({
					.event_type = Input::EventType::kMove,
					.position = Vec2(static_cast<float>(sample) * 0.5f, 0.0f),
					.time = Time(inputTime) }, stroke.modeledResults).ok());
				draw3::AppendNewModeledPoints(stroke, 30.0f);
			}
			const DirectX::XMFLOAT2 rawEndpoint = { 4.0f, 0.0f };
			const Result modeledBeforePredict = stroke.modeledResults.back();
			const size_t modeledCountBeforePredict = stroke.modeledResults.size();
			std::vector<Result> predictedResults;
			const absl::Status firstPredictionStatus =
				stroke.modeler.Predict(predictedResults);
			const absl::Status secondPredictionStatus =
				stroke.modeler.Predict(predictedResults);
			TEST_CHECK(state, stroke.modeledResults.size() == modeledCountBeforePredict);
			TEST_CHECK(state, stroke.modeledResults.back() == modeledBeforePredict);
			if (predictionEnabled)
			{
				TEST_CHECK(state, firstPredictionStatus.ok());
				TEST_CHECK(state, secondPredictionStatus.ok());
			}
			else
			{
				TEST_CHECK(state, !firstPredictionStatus.ok());
				TEST_CHECK(state, !secondPredictionStatus.ok());
				TEST_CHECK(state, predictedResults.empty());
			}
			TEST_CHECK(state, !draw3::IsModeledTipSettled(
				stroke.modeledResults, rawEndpoint, frameIntervalSeconds));

			const size_t modeledCountBeforeSettle = stroke.modeledResults.size();
			const size_t realPointCountBeforeSettle = stroke.realPoints.size();
			int stationaryAdvanceCount = 0;
			while (!draw3::IsModeledTipSettled(
				stroke.modeledResults, rawEndpoint, frameIntervalSeconds) &&
				stationaryAdvanceCount < 120)
			{
				inputTime += frameIntervalSeconds;
				TEST_CHECK(state, stroke.modeler.Update({
					.event_type = Input::EventType::kMove,
					.position = Vec2(rawEndpoint.x, rawEndpoint.y),
					.time = Time(inputTime) }, stroke.modeledResults).ok());
				draw3::AppendNewModeledPoints(stroke);
				++stationaryAdvanceCount;
			}
			TEST_CHECK(state, draw3::IsModeledTipSettled(
				stroke.modeledResults, rawEndpoint, frameIntervalSeconds));
			TEST_CHECK(state, stationaryAdvanceCount > 0 && stationaryAdvanceCount < 60);
			TEST_CHECK(state, stroke.modeledResults.size() > modeledCountBeforeSettle);
			TEST_CHECK(state, stroke.modeledResults.size() - modeledCountBeforeSettle < 256);
			TEST_CHECK(state, stroke.realPoints.size() > realPointCountBeforeSettle);

			const size_t settledModeledCount = stroke.modeledResults.size();
			const size_t settledRealPointCount = stroke.realPoints.size();
			for (int idleFrame = 0; idleFrame < 600; ++idleFrame)
			{
				if (!draw3::IsModeledTipSettled(
					stroke.modeledResults, rawEndpoint, frameIntervalSeconds))
				{
					inputTime += frameIntervalSeconds;
					TEST_CHECK(state, stroke.modeler.Update({
						.event_type = Input::EventType::kMove,
						.position = Vec2(rawEndpoint.x, rawEndpoint.y),
						.time = Time(inputTime) }, stroke.modeledResults).ok());
					draw3::AppendNewModeledPoints(stroke);
				}
			}
			TEST_CHECK(state, stroke.modeledResults.size() == settledModeledCount);
			TEST_CHECK(state, stroke.realPoints.size() == settledRealPointCount);

			stroke.idleFrozen = false;
			stroke.visualStableFrameCount = 0;
			stroke.previousL0DrawPoints.clear();
			stroke.lastMovementInputTime = 8.0 * frameIntervalSeconds;
			for (int stableFrame = 0; stableFrame < 4; ++stableFrame)
			{
				stroke.logicalInputTime = inputTime + 1.0 +
					stableFrame * frameIntervalSeconds;
				stroke.predictedResults.clear();
				if (predictionEnabled)
					TEST_CHECK(state, stroke.modeler.Predict(
						stroke.predictedResults).ok());
				draw3::RebuildPredictedPoints(stroke);
				draw3::RebuildL0DrawPoints(stroke,
					configuration.liveTipDurationSeconds,
					draw3::StrokeShape::RoundCapsule, 512, 512);
				draw3::UpdateIdleFreezeState(stroke, false, true,
					configuration.liveTipDurationSeconds);
				if (stableFrame < 3) TEST_CHECK(state, !stroke.idleFrozen);
			}
			TEST_CHECK(state, stroke.idleFrozen);
			TEST_CHECK(state, stroke.modeledResults.size() == settledModeledCount);
			TEST_CHECK(state, stroke.realPoints.size() == settledRealPointCount);

			stroke.idleFrozen = false;
			stroke.visualStableFrameCount = 0;
			inputTime += frameIntervalSeconds;
			const DirectX::XMFLOAT2 resumedRawEndpoint = { 4.5f, 0.0f };
			const double previousModeledTime =
				stroke.modeledResults.back().time.Value();
			TEST_CHECK(state, stroke.modeler.Update({
				.event_type = Input::EventType::kMove,
				.position = Vec2(resumedRawEndpoint.x, resumedRawEndpoint.y),
				.time = Time(inputTime) }, stroke.modeledResults).ok());
			draw3::AppendNewModeledPoints(stroke, 30.0f);
			TEST_CHECK(state, stroke.modeledResults.back().time.Value() >
				previousModeledTime);
			TEST_CHECK(state, std::isfinite(stroke.modeledResults.back().position.x));
			TEST_CHECK(state, std::isfinite(stroke.modeledResults.back().velocity.x));

			int resumedStationaryAdvanceCount = 0;
			while (!draw3::IsModeledTipSettled(
				stroke.modeledResults, resumedRawEndpoint, frameIntervalSeconds) &&
				resumedStationaryAdvanceCount < 120)
			{
				inputTime += frameIntervalSeconds;
				TEST_CHECK(state, stroke.modeler.Update({
					.event_type = Input::EventType::kMove,
					.position = Vec2(resumedRawEndpoint.x, resumedRawEndpoint.y),
					.time = Time(inputTime) }, stroke.modeledResults).ok());
				draw3::AppendNewModeledPoints(stroke);
				++resumedStationaryAdvanceCount;
			}
			TEST_CHECK(state, draw3::IsModeledTipSettled(
				stroke.modeledResults, resumedRawEndpoint, frameIntervalSeconds));
			const draw3::InkPoint settledTip = stroke.realPoints.back();
			inputTime += frameIntervalSeconds;
			TEST_CHECK(state, stroke.modeler.Update({
				.event_type = Input::EventType::kUp,
				.position = Vec2(resumedRawEndpoint.x, resumedRawEndpoint.y),
				.time = Time(inputTime) }, stroke.modeledResults).ok());
			draw3::AppendNewModeledPoints(stroke);
			const draw3::InkPoint finalTip = stroke.realPoints.back();
			TEST_CHECK(state, std::hypot(finalTip.x - resumedRawEndpoint.x,
				finalTip.y - resumedRawEndpoint.y) <= 0.05f);
			TEST_CHECK(state, std::abs(finalTip.r - settledTip.r) <= 0.02f);
		}
	}

	void TestSparsePenFrames(TestState& state)
	{
		using namespace ink::stroke_model;
		const auto configuration = draw3::CreateStrokeModelConfiguration(96);
		for (double fps : { 30.0, 60.0, 120.0, 240.0 })
		for (double sampleMs : { 8.0, 16.0, 33.0, 80.0, 120.0 })
		for (int turn : { 0, 1, 2 })
		for (bool simulated : { false, true })
		{
			if (simulated && (fps != 120 || sampleMs != 33 || turn != 0)) continue;
			draw3::ActiveStroke stroke(5.0f, configuration.expectedSpeed,
				simulated ? draw3::StrokeWidthMode::SimulatedPressure : draw3::StrokeWidthMode::Fixed);
			stroke.useDisplayTime = true;
			TEST_CHECK(state, stroke.modeler.Reset(configuration.modelParams).ok());
			TEST_CHECK(state, stroke.modeler.Update({
				.event_type = Input::EventType::kDown, .position = Vec2(0, 0), .time = Time(0)
			}, stroke.modeledResults).ok());
			draw3::AppendNewModeledPoints(stroke);
			double lastModelTime = 0.0, now = 0.0;
			const double dt = 1.0 / fps;
			DirectX::XMFLOAT2 raw{};
			size_t modelCalls = 1, peakScratch = 0, unpinnedMoves = 0;
			auto frame = [&](double time, bool moved, DirectX::XMFLOAT2 target)
			{
				stroke.logicalInputTime = time;
				if (moved)
				{
					raw = target;
					stroke.lastMovementInputTime = time;
					stroke.idleFrozen = false;
					stroke.visualStableFrameCount = 0;
					lastModelTime = draw3::ResolvePenModelInputTime(stroke, time, lastModelTime, dt);
					stroke.modelScratch.clear();
					TEST_CHECK(state, stroke.modeler.Update({
						.event_type = Input::EventType::kMove, .position = Vec2(raw.x, raw.y),
						.time = Time(lastModelTime) }, stroke.modelScratch).ok());
					++modelCalls;
					peakScratch = std::max(peakScratch, stroke.modelScratch.size());
					const float speed = simulated && time > 10.0 ? 1200.0f : 30.0f;
					if (stroke.endpointAdmission.active)
						draw3::AppendRecoveryModeledPoints(stroke, stroke.modelScratch, raw, speed);
					else
					{
						stroke.modeledResults = stroke.modelScratch;
						stroke.convertedResultCount = 0;
						draw3::AppendNewModeledPoints(stroke, speed);
					}
					if (!stroke.endpointAdmission.active &&
						std::hypot(stroke.realPoints.back().x - raw.x,
							stroke.realPoints.back().y - raw.y) > 0.05f) ++unpinnedMoves;
				}
				else if (!stroke.idleFrozen && (stroke.endpointAdmission.active ||
					draw3::ShouldStartEndpointSettling(time - stroke.lastMovementInputTime, dt)))
				{
					if (!stroke.endpointAdmission.active) draw3::BeginEndpointAdmission(stroke, raw);
					if (draw3::IsModeledTipSettled(draw3::LatestModeledTip(stroke), raw, dt))
					{
						stroke.modelClockStopped = true;
						if (stroke.endpointAdmission.recovering)
						{
							draw3::ClearEndpointAdmission(stroke);
							draw3::BeginEndpointAdmission(stroke, raw);
						}
						draw3::AppendEndpointBoundedModeledPoints(stroke, {}, -1,
							stroke.lastMovementInputTime, true);
					}
					else
					{
						lastModelTime = time - stroke.modelTimeOffset;
						stroke.modelScratch.clear();
						TEST_CHECK(state, stroke.modeler.Update({
							.event_type = Input::EventType::kMove, .position = Vec2(raw.x, raw.y),
							.time = Time(lastModelTime) }, stroke.modelScratch).ok());
						++modelCalls;
						peakScratch = std::max(peakScratch, stroke.modelScratch.size());
						if (stroke.endpointAdmission.recovering)
							draw3::AppendRecoveryModeledPoints(stroke, stroke.modelScratch, raw, -1);
						else draw3::AppendEndpointBoundedModeledPoints(stroke, stroke.modelScratch,
							-1, stroke.lastMovementInputTime);
					}
				}
				stroke.predictedResults.clear();
				if (!stroke.endpointAdmission.active)
					TEST_CHECK(state, stroke.modeler.Predict(stroke.predictedResults).ok());
				draw3::RebuildPredictedPoints(stroke);
				draw3::RebuildL0DrawPoints(stroke, 0.055,
					draw3::StrokeShape::RoundCapsule, 800, 600);
				draw3::UpdateIdleFreezeState(stroke, moved,
					draw3::IsModeledTipSettled(draw3::LatestModeledTip(stroke), raw, dt), 0.055);
			};
			// 分离事件和渲染节奏：量化 1px 曲线，包含相位抖动及没有新包的帧。
			int sample = 0;
			for (int index = 1; sample < 24; ++index)
			{
				now += dt * (index % 2 ? 0.94 : 1.06);
				const int next = std::min(24, static_cast<int>(now / (sampleMs * 0.001)));
				const bool moved = next > sample;
				sample = next;
				frame(now, moved, { static_cast<float>(sample),
					static_cast<float>(std::floor(3.0 * std::sin(sample * 0.15))) });
			}
			TEST_CHECK(state, unpinnedMoves > 2);
			for (int i = 0; i < static_cast<int>(fps * 0.5); ++i)
				frame(now += dt, false, raw);
			TEST_CHECK(state, stroke.idleFrozen);
			TEST_CHECK(state, NearlyEqual(stroke.realPoints.back().x, raw.x, 0.05f));
			TEST_CHECK(state, NearlyEqual(stroke.l0DrawPoints.back().r, stroke.realPoints.back().r, 0.02f));
			const float heldRadius = stroke.realPoints.back().r;
			const auto count = stroke.realPoints.size(), calls = modelCalls;
			for (int i = 0; i < static_cast<int>(fps * 10); ++i) frame(now += dt, false, raw);
			TEST_CHECK(state, stroke.realPoints.size() == count);
			TEST_CHECK(state, modelCalls == calls);
			const auto start = raw;
			for (int i = 1; i <= 12; ++i)
				frame(now += dt, true, {
					start.x + (turn == 0 ? i : turn == 2 ? -i : 0),
					start.y + (turn == 1 ? i : 0) });
			TEST_CHECK(state, !stroke.endpointAdmission.active);
			if (simulated)
			{
				TEST_CHECK(state, stroke.realPoints.back().r < heldRadius - 0.02f);
				TEST_CHECK(state, stroke.widthEstimator.lastTime <= lastModelTime + 0.01);
			}
			for (int i = 0; i < static_cast<int>(fps * 0.5); ++i) frame(now += dt, false, raw);
			TEST_CHECK(state, stroke.idleFrozen);
			TEST_CHECK(state, peakScratch < 256);
			// 超长静止不交给模型一次积分；同位 Up 保持老化完毕的实际半径。
			frame(now += 3600.0, false, raw);
			const auto finalCount = stroke.realPoints.size();
			const double upTime = draw3::ResolvePenModelInputTime(stroke, now, lastModelTime, dt);
			TEST_CHECK(state, upTime - lastModelTime <= dt + 0.00001);
			stroke.modelScratch.clear();
			TEST_CHECK(state, stroke.modeler.Update({
				.event_type = Input::EventType::kUp, .position = Vec2(raw.x, raw.y),
				.time = Time(upTime) }, stroke.modelScratch).ok());
			draw3::BeginEndpointAdmission(stroke, raw);
			draw3::AppendEndpointBoundedModeledPoints(stroke, stroke.modelScratch,
				-1, stroke.lastMovementInputTime, true);
			std::vector<draw3::InkPoint> completed;
			draw3::BuildCompletedPenTail(stroke, 0.055, completed);
			TEST_CHECK(state, NearlyEqual(completed.back().r, stroke.realPoints.back().r, 0.02f));
			TEST_CHECK(state, stroke.realPoints.size() == finalCount);
			TEST_CHECK(state, stroke.modelScratch.size() < 256);
		}
	}

	void TestPhysicalUpTipTime(TestState& state)
	{
		for (const auto mode : { draw3::StrokeWidthMode::Fixed,
			draw3::StrokeWidthMode::SimulatedPressure, draw3::StrokeWidthMode::HardwarePressure })
		{
			draw3::ActiveStroke stroke(5.0f, 100.0f, mode);
			stroke.useDisplayTime = true;
			for (int i = 0; i < 7; ++i)
				stroke.realPoints.push_back({ i * 4.0f, 0.0f, 2.5f, i * 0.01f });
			const double taper = draw3::ResolveLiveTipTaperDurationSeconds(mode, 0.055);
			stroke.logicalInputTime = 0.06;
			draw3::RebuildL0DrawPoints(stroke, taper, draw3::StrokeShape::RoundCapsule, 800, 600);
			const auto original = stroke.l0DrawPoints;
			// 故意先推进帧时间，再消费较早 Up，验证锁定的是物理时间而非 max(frame, Up)。
			stroke.logicalInputTime = 0.5;
			draw3::LockPenTerminalState(stroke, 0.06, { 24, 0 }, { 24, 0 });
			for (double delay : { 0.0, 0.02, 0.2, 1.0, 3600.0 })
			{
				stroke.logicalInputTime = 0.06 + delay;
				draw3::LockPenTerminalState(stroke, 0.06 + delay, { 50, 50 }, { 50, 50 });
				draw3::RebuildL0DrawPoints(stroke, taper, draw3::StrokeShape::RoundCapsule, 800, 600);
				std::vector<draw3::InkPoint> completed;
				draw3::BuildCompletedPenTail(stroke, taper, completed);
				TEST_CHECK(state, completed.size() == original.size());
				TEST_CHECK(state, stroke.terminalFirstPoint == original.size());
				TEST_CHECK(state, draw3::ResolvePenDisplayTime(stroke) == 0.06);
				for (size_t i = 0; i < original.size(); ++i)
				{
					TEST_CHECK(state, NearlyEqual(completed[i].r, original[i].r));
					TEST_CHECK(state, NearlyEqual(stroke.l0DrawPoints[i].r, original[i].r));
				}
			}
			draw3::ClearPenTerminalState(stroke);
			TEST_CHECK(state, !stroke.terminalDisplayTime);
			stroke.logicalInputTime = 1.0;
			draw3::RebuildL0DrawPoints(stroke, taper, draw3::StrokeShape::RoundCapsule, 800, 600);
			TEST_CHECK(state, NearlyEqual(stroke.l0DrawPoints.back().r, 2.5f));
			draw3::LockPenTerminalState(stroke, 1.0, { 24, 0 }, { 24, 0 });
			stroke.logicalInputTime = 2.0;
			std::vector<draw3::InkPoint> completed;
			draw3::BuildCompletedPenTail(stroke, taper, completed);
			TEST_CHECK(state, NearlyEqual(completed.back().r, 2.5f));
		}
	}

	void TestTerminalFallbackBoundary(TestState& state)
	{
		{
			draw3::ActiveStroke stroke(5, 100, draw3::StrokeWidthMode::Fixed);
			stroke.realPoints = { { 0, 0, 2.5f, 0 }, { 10, 0, 2.5f, 0.01f } };
			stroke.hasCommittedGeometry = true;
			stroke.committedIndex = 1;
			draw3::LockPenTerminalState(stroke, 0.02, { 10, 0 }, { 10.005f, 0 });
			draw3::AppendTerminalFallbackPoint(stroke, { 10, 0, 3.0f, 0.02f });
			TEST_CHECK(state, stroke.realPoints.size() == 2 && stroke.realPoints.back().r == 2.5f);
			draw3::AppendTerminalFallbackPoint(stroke, { 10.005f, 0, 2.5f, 0.02f });
			TEST_CHECK(state, stroke.realPoints.size() == 3 && stroke.realPoints[1].x == 10);
			TEST_CHECK(state, stroke.realPoints.back().x == 10.005f && stroke.committedIndex == 1);
		}
	}

	void TestStationaryTipAging(TestState& state)
	{
		draw3::ActiveStroke stroke(5.0f, 100.0f, draw3::StrokeWidthMode::Fixed);
		for (int index = 0; index < 7; ++index)
			stroke.realPoints.push_back({ index * 0.3f, 0.0f, 2.5f, index * 0.01f });
		stroke.logicalInputTime = 0.06;
		draw3::RebuildL0DrawPoints(stroke, 0.055, draw3::StrokeShape::RoundCapsule, 800, 600);
		const float movingRadius = stroke.l0DrawPoints.back().r;
		TEST_CHECK(state, movingRadius < 2.0f);
		const size_t pointCount = stroke.realPoints.size();
		float previousRadius = movingRadius;
		for (int frame = 1; frame <= 20; ++frame)
		{
			stroke.logicalInputTime = 0.06 + frame * 0.005;
			draw3::RebuildL0DrawPoints(stroke, 0.055,
				draw3::StrokeShape::RoundCapsule, 800, 600);
			const float radius = stroke.l0DrawPoints.back().r;
			TEST_CHECK(state, radius >= previousRadius - 0.0001f && radius <= 2.5f);
			TEST_CHECK(state, radius - previousRadius < 0.4f);
			TEST_CHECK(state, stroke.realPoints.size() == pointCount);
			previousRadius = radius;
		}
		TEST_CHECK(state, NearlyEqual(previousRadius, 2.5f));
		stroke.logicalInputTime = 1.0;
		draw3::RebuildL0DrawPoints(stroke, 0.055, draw3::StrokeShape::RoundCapsule, 800, 600);
		TEST_CHECK(state, NearlyEqual(stroke.l0DrawPoints.back().r, 2.5f));
		std::vector<draw3::InkPoint> completed;
		draw3::BuildCompletedPenTail(stroke, 0.055, completed);
		TEST_CHECK(state, NearlyEqual(completed.back().r, 2.5f));
	}

	void TestEndpointAdmissionContracts(TestState& state)
	{
		using ink::stroke_model::DisabledPredictorParams;
		using ink::stroke_model::Input;
		using ink::stroke_model::Result;
		using ink::stroke_model::StrokeEndPredictorParams;
		using ink::stroke_model::Time;
		using ink::stroke_model::Vec2;

		auto modeledResult = [](float x, float y, double time)
		{
			Result result;
			result.position = { x, y };
			result.time = Time(time);
			return result;
		};

		// 纯几何轨迹先覆盖弹簧越界再回摆：只接纳安全前缀，并精确钉住 raw endpoint。
		draw3::ActiveStroke stopGate(
			10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		stopGate.realPoints.push_back({ 0.0f, 0.0f, 5.0f, 0.0f });
		std::vector<Result> stopScratch{
			modeledResult(4.0f, 0.0f, 0.01),
			modeledResult(8.0f, 0.0f, 0.02),
			modeledResult(10.2f, 0.0f, 0.03),
			modeledResult(9.6f, 0.0f, 0.04)
		};
		draw3::BeginEndpointAdmission(stopGate, { 10.0f, 0.0f });
		const draw3::EndpointAdmissionResult stopResult =
			draw3::AppendEndpointBoundedModeledPoints(
				stopGate, stopScratch, -1.0f, 0.04);
		TEST_CHECK(state, stopResult.acceptedResultCount == 2);
		TEST_CHECK(state, stopResult.endpointPinned);
		TEST_CHECK(state, stopGate.realPoints.size() == 4);
		TEST_CHECK(state, NearlyEqual(stopGate.realPoints[1].x, 4.0f));
		TEST_CHECK(state, NearlyEqual(stopGate.realPoints[2].x, 8.0f));
		TEST_CHECK(state, NearlyEqual(stopGate.realPoints[3].x, 10.0f));
		TEST_CHECK(state, draw3::LatestModeledTip(stopGate).size() == 1);
		TEST_CHECK(state, NearlyEqual(
			draw3::LatestModeledTip(stopGate).back().position.x, 9.6f));
		float previousDistance = 10.0f;
		for (size_t index = 1; index < stopGate.realPoints.size(); ++index)
		{
			const float distance = std::abs(10.0f - stopGate.realPoints[index].x);
			TEST_CHECK(state, distance <= previousDistance + 0.0001f);
			TEST_CHECK(state, stopGate.realPoints[index].x <= 10.05f);
			previousDistance = distance;
		}
		const size_t pinnedPointCount = stopGate.realPoints.size();
		const std::array<Result, 2> postPinScratch{
			modeledResult(10.4f, 0.0f, 0.05),
			modeledResult(9.9f, 0.0f, 0.06)
		};
		draw3::AppendEndpointBoundedModeledPoints(
			stopGate, postPinScratch, -1.0f, 0.06);
		TEST_CHECK(state, stopGate.realPoints.size() == pinnedPointCount);

		// kUp 必须扫描整批，首个越界之后的 returning loop 不能重新进入完成中心线。
		draw3::ActiveStroke upGate(
			10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		upGate.realPoints.push_back({ 2.0f, 0.0f, 5.0f, 0.0f });
		const std::array<Result, 3> upScratch{
			modeledResult(6.0f, 0.0f, 0.01),
			modeledResult(10.8f, 0.0f, 0.02),
			modeledResult(9.0f, 0.0f, 0.03)
		};
		draw3::BeginEndpointAdmission(upGate, { 10.0f, 0.0f });
		const draw3::EndpointAdmissionResult upResult =
			draw3::AppendEndpointBoundedModeledPoints(
				upGate, upScratch, -1.0f, 0.03, true);
		TEST_CHECK(state, upResult.acceptedResultCount == 1);
		TEST_CHECK(state, upResult.endpointPinned);
		TEST_CHECK(state, upGate.realPoints.size() == 3);
		TEST_CHECK(state, NearlyEqual(upGate.realPoints[1].x, 6.0f));
		TEST_CHECK(state, NearlyEqual(upGate.realPoints[2].x, 10.0f));
		TEST_CHECK(state, std::none_of(upGate.realPoints.begin(), upGate.realPoints.end(),
			[](const draw3::InkPoint& point) { return NearlyEqual(point.x, 9.0f); }));
		draw3::RebuildL0DrawPoints(upGate, 0.0,
			draw3::StrokeShape::RoundCapsule, 64, 64);
		TEST_CHECK(state, !upGate.l0DrawPoints.empty());
		TEST_CHECK(state, NearlyEqual(upGate.l0DrawPoints.back().x, 10.0f));
		std::vector<draw3::InkPoint> storedScratch;
		const std::optional<draw3::InkStroke> sanitizedStored =
			draw3::FinalizeStoredStroke(upGate,
				{ draw3::StoredInkType::Pen, 0, 1.0f, 0 }, 0.0, storedScratch);
		TEST_CHECK(state, sanitizedStored.has_value());
		TEST_CHECK(state, sanitizedStored &&
			NearlyEqual(sanitizedStored->Points().back().x, 10.0f));
		TEST_CHECK(state, sanitizedStored && std::none_of(
			sanitizedStored->Points().begin(), sanitizedStored->Points().end(),
			[](const draw3::StoredInkPoint& point) { return point.x > 10.05f; }));

		// 恢复只丢弃旧停点后的回摆前缀，安全后缀可以直接解除门禁。
		const std::array<Result, 3> unsafeResumeScratch{
			modeledResult(9.5f, 0.0f, 0.07),
			modeledResult(10.5f, 0.0f, 0.08),
			modeledResult(11.5f, 0.0f, 0.09)
		};
		draw3::AppendRecoveryModeledPoints(stopGate, unsafeResumeScratch,
			{ 12.0f, 0.0f }, 100.0f);
		TEST_CHECK(state, !stopGate.endpointAdmission.active);
		TEST_CHECK(state, stopGate.realPoints.size() == pinnedPointCount + 2);
		TEST_CHECK(state, NearlyEqual(stopGate.realPoints.back().x, 11.5f));
		TEST_CHECK(state, !draw3::IsModeledTipSettled(
			draw3::LatestModeledTip(stopGate), { 12.0f, 0.0f }, 1.0 / 120.0));
		draw3::BeginEndpointAdmission(stopGate, { 14.0f, 0.0f });
		const std::array<Result, 2> safeResumeScratch{
			modeledResult(12.5f, 0.0f, 0.10),
			modeledResult(13.5f, 0.0f, 0.11)
		};
		const draw3::EndpointAdmissionResult safeResume =
			draw3::AppendEndpointBoundedModeledPoints(
				stopGate, safeResumeScratch, 100.0f, 0.11);
		TEST_CHECK(state,
			safeResume.acceptedResultCount == safeResumeScratch.size());
		TEST_CHECK(state, !safeResume.endpointPinned);
		draw3::ClearEndpointAdmission(stopGate); // 本例随后回到普通 Tracking。
		TEST_CHECK(state, !stopGate.endpointAdmission.active);
		TEST_CHECK(state, NearlyEqual(stopGate.realPoints[stopGate.realPoints.size() - 2].x, 12.5f));
		TEST_CHECK(state, NearlyEqual(stopGate.realPoints.back().x, 13.5f));

		// 恢复可以跨批等待；中途转向仍使用最初停点，不能把新 raw 点硬补入几何。
		for (int turn : { 0, 1, 2 })
		{
			draw3::ActiveStroke recovery(10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
			recovery.realPoints.push_back({ 10.0f, 0.0f, 5.0f, 0.0f });
			draw3::BeginEndpointAdmission(recovery, { 10.0f, 0.0f });
			draw3::AppendEndpointBoundedModeledPoints(recovery, {}, -1.0f, 0.0, true);
			const std::array<Result, 2> rejectedPrefix{
				modeledResult(9.5f, 0.0f, 0.01),
				modeledResult(12.5f, 0.0f, 0.02)
			};
			draw3::AppendRecoveryModeledPoints(recovery, rejectedPrefix, { 12.0f, 0.0f }, 100.0f);
			TEST_CHECK(state, recovery.endpointAdmission.active && recovery.endpointAdmission.recovering);
			TEST_CHECK(state, recovery.realPoints.size() == 1);
			TEST_CHECK(state, NearlyEqual(recovery.endpointAdmission.recoveryOrigin.x, 10.0f));
			draw3::AppendRecoveryModeledPoints(recovery, {}, { 12.0f, 0.0f }, -1.0f);
			TEST_CHECK(state, recovery.realPoints.size() == 1 && recovery.endpointAdmission.active);

			// 接纳过安全点也不能跳过后面的回摆，坏尾尚在时保持恢复门禁。
			std::array<Result, 2> unsafeTail{
				modeledResult(10.5f, 0.0f, 0.03),
				modeledResult(10.25f, 0.0f, 0.04)
			};
			unsafeTail.back().velocity = { -1.0f, 0.0f };
			draw3::AppendRecoveryModeledPoints(recovery, unsafeTail, { 12.0f, 0.0f }, 100.0f);
			TEST_CHECK(state, recovery.endpointAdmission.active && recovery.realPoints.size() == 2);
			TEST_CHECK(state, NearlyEqual(recovery.realPoints.back().x, 10.5f));
			TEST_CHECK(state, NearlyEqual(recovery.endpointAdmission.recoveryOrigin.x, 10.0f));

			const DirectX::XMFLOAT2 target = turn == 0 ? DirectX::XMFLOAT2{ 14.0f, 0.0f }
				: turn == 1 ? DirectX::XMFLOAT2{ 10.0f, 4.0f } : DirectX::XMFLOAT2{ 6.0f, 0.0f };
			const DirectX::XMFLOAT2 safePoint = turn == 0 ? DirectX::XMFLOAT2{ 12.0f, 0.0f }
				: turn == 1 ? DirectX::XMFLOAT2{ 10.0f, 1.0f } : DirectX::XMFLOAT2{ 9.0f, 0.0f };
			std::array<Result, 2> safeSuffix{
				modeledResult(turn == 2 ? 10.6f : 10.2f, 0.0f, 0.05),
				modeledResult(safePoint.x, safePoint.y, 0.06)
			};
			safeSuffix.back().velocity = { target.x - 10.0f, target.y };
			draw3::AppendRecoveryModeledPoints(recovery, safeSuffix, target, 100.0f);
			TEST_CHECK(state, !recovery.endpointAdmission.active && recovery.realPoints.size() == 3);
			TEST_CHECK(state, NearlyEqual(recovery.realPoints.back().x, safePoint.x));
			TEST_CHECK(state, NearlyEqual(recovery.realPoints.back().y, safePoint.y));
		}

		draw3::ActiveStroke exactCommitted(
			10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		exactCommitted.realPoints.push_back({ 10.0f, 0.0f, 5.0f, 0.0f });
		exactCommitted.hasCommittedGeometry = true;
		exactCommitted.committedIndex = 0;
		draw3::BeginEndpointAdmission(exactCommitted, { 10.0f, 0.0f });
		draw3::AppendEndpointBoundedModeledPoints(
			exactCommitted, std::span<const Result>{}, -1.0f, 0.01, true);
		TEST_CHECK(state, exactCommitted.realPoints.size() == 1);

		const draw3::StrokeModelConfiguration configuration =
			draw3::CreateStrokeModelConfiguration(96);
		// raw/empty 交替的一帧空洞仍属于 Kalman Tracking，不能过早切进 endpoint settling。
		for (const double framesPerSecond : { 30.0, 60.0, 120.0, 240.0 })
		{
			const double frameIntervalSeconds = 1.0 / framesPerSecond;
			TEST_CHECK(state, !draw3::ShouldStartEndpointSettling(
				frameIntervalSeconds, frameIntervalSeconds));
			TEST_CHECK(state, draw3::ShouldStartEndpointSettling(
				frameIntervalSeconds * 1.001, frameIntervalSeconds));

			draw3::ActiveStroke alternatingTracking(
				5.0f, configuration.expectedSpeed);
			TEST_CHECK(state, alternatingTracking.modeler.Reset(
				configuration.modelParams).ok());
			double trackingTime = 0.0;
			TEST_CHECK(state, alternatingTracking.modeler.Update({
				.event_type = Input::EventType::kDown,
				.position = Vec2(0.0f, 0.0f), .time = Time(trackingTime) },
				alternatingTracking.modeledResults).ok());
			draw3::AppendNewModeledPoints(alternatingTracking, 240.0f);
			for (int sample = 1; sample <= 4; ++sample)
			{
				trackingTime += frameIntervalSeconds * 2.0;
				TEST_CHECK(state, alternatingTracking.modeler.Update({
					.event_type = Input::EventType::kMove,
					.position = Vec2(static_cast<float>(sample) * 4.0f, 0.0f),
					.time = Time(trackingTime) }, alternatingTracking.modeledResults).ok());
				draw3::AppendNewModeledPoints(alternatingTracking, 240.0f);
				const size_t modeledCount = alternatingTracking.modeledResults.size();
				std::vector<Result> betweenFramePrediction;
				TEST_CHECK(state, alternatingTracking.modeler.Predict(
					betweenFramePrediction).ok());
				TEST_CHECK(state, alternatingTracking.modeledResults.size() == modeledCount);
				TEST_CHECK(state, !alternatingTracking.endpointAdmission.active);
			}
		}
		for (int predictionMode = 0; predictionMode < 3; ++predictionMode)
		{
			for (const double framesPerSecond : { 30.0, 60.0, 120.0, 240.0 })
			{
				for (const float speed : { 30.0f, 300.0f, 1200.0f })
				{
					auto params = configuration.modelParams;
					if (predictionMode == 0)
						params.prediction_params = configuration.kalmanPredictorParams;
					else if (predictionMode == 1)
						params.prediction_params = StrokeEndPredictorParams{};
					else
						params.prediction_params = DisabledPredictorParams{};
					draw3::ActiveStroke stroke(5.0f, configuration.expectedSpeed);
					TEST_CHECK(state, stroke.modeler.Reset(params).ok());
					const double frameIntervalSeconds = 1.0 / framesPerSecond;
					double inputTime = 0.0;
					TEST_CHECK(state, stroke.modeler.Update({
						.event_type = Input::EventType::kDown,
						.position = Vec2(0.0f, 0.0f),
						.time = Time(inputTime) }, stroke.modeledResults).ok());
					draw3::AppendNewModeledPoints(stroke, speed);
					float rawX = 0.0f;
					for (int sample = 0; sample < 8; ++sample)
					{
						inputTime += frameIntervalSeconds;
						rawX += speed * static_cast<float>(frameIntervalSeconds);
						TEST_CHECK(state, stroke.modeler.Update({
							.event_type = Input::EventType::kMove,
							.position = Vec2(rawX, 0.0f),
							.time = Time(inputTime) }, stroke.modeledResults).ok());
						draw3::AppendNewModeledPoints(stroke, speed);
					}

					const size_t modeledCountBeforeStop = stroke.modeledResults.size();
					const size_t realCountBeforeStop = stroke.realPoints.size();
					const double stopStartTime = inputTime;
					const float visibleStartX = stroke.realPoints.empty()
						? 0.0f : stroke.realPoints.back().x;
					draw3::BeginEndpointAdmission(stroke, { rawX, 0.0f });
					int pinnedFrame = -1;
					double internalSettledElapsedSeconds = -1.0;
					size_t pinnedCount = 0;
					bool settled = false;
					const int maximumSettleFrames =
						static_cast<int>(std::ceil(framesPerSecond));
					for (int frame = 0; frame < maximumSettleFrames; ++frame)
					{
						settled = draw3::IsModeledTipSettled(
							draw3::LatestModeledTip(stroke), { rawX, 0.0f },
							frameIntervalSeconds);
						if (settled && internalSettledElapsedSeconds < 0.0)
							internalSettledElapsedSeconds = inputTime - stopStartTime;
						stroke.modelScratch.clear();
						if (settled)
						{
							draw3::AppendEndpointBoundedModeledPoints(stroke,
								stroke.modelScratch, -1.0f, inputTime, true);
						}
						else
						{
							inputTime += frameIntervalSeconds;
							TEST_CHECK(state, stroke.modeler.Update({
								.event_type = Input::EventType::kMove,
								.position = Vec2(rawX, 0.0f),
								.time = Time(inputTime) }, stroke.modelScratch).ok());
							TEST_CHECK(state, stroke.modelScratch.size() < 256);
							draw3::AppendEndpointBoundedModeledPoints(stroke,
								stroke.modelScratch, -1.0f, inputTime);
							settled = draw3::IsModeledTipSettled(
								draw3::LatestModeledTip(stroke), { rawX, 0.0f },
								frameIntervalSeconds);
							if (settled && internalSettledElapsedSeconds < 0.0)
								internalSettledElapsedSeconds = inputTime - stopStartTime;
						}
						if (stroke.endpointAdmission.visualPinned && pinnedFrame < 0)
						{
							pinnedFrame = frame;
							pinnedCount = stroke.realPoints.size();
						}
						if (pinnedFrame >= 0)
							TEST_CHECK(state, stroke.realPoints.size() == pinnedCount);
						if (settled && stroke.endpointAdmission.visualPinned) break;
					}
					TEST_CHECK(state, pinnedFrame >= 0);
					TEST_CHECK(state, settled);
					// internal settled 不能被 visual-pinned 提前掩盖，诊断预算固定为 200ms。
					TEST_CHECK(state, internalSettledElapsedSeconds >= 0.0);
					TEST_CHECK(state, internalSettledElapsedSeconds <= 0.2);
					TEST_CHECK(state, stroke.modeledResults.size() == modeledCountBeforeStop);
					TEST_CHECK(state, stroke.realPoints.size() >= realCountBeforeStop);
					TEST_CHECK(state, NearlyEqual(stroke.realPoints.back().x, rawX, 0.05f));

					float lastDistance = std::abs(rawX - visibleStartX);
					for (size_t index = realCountBeforeStop;
						index < stroke.realPoints.size(); ++index)
					{
						const float distance = std::abs(rawX - stroke.realPoints[index].x);
						TEST_CHECK(state, distance <= lastDistance + 0.0001f);
						TEST_CHECK(state, stroke.realPoints[index].x <= rawX + 0.05f);
						lastDistance = distance;
					}
					const size_t longHoldModeledCount = stroke.modeledResults.size();
					const size_t longHoldPointCount = stroke.realPoints.size();
					stroke.idleFrozen = false;
					stroke.visualStableFrameCount = 0;
					stroke.previousL0DrawPoints.clear();
					stroke.lastMovementInputTime = stopStartTime;
					stroke.useDisplayTime = true;
					for (int frame = 0;
						frame < static_cast<int>(framesPerSecond * 10.0); ++frame)
					{
						inputTime += frameIntervalSeconds;
						stroke.logicalInputTime = inputTime;
						stroke.predictedResults.clear();
						draw3::RebuildPredictedPoints(stroke);
						draw3::RebuildL0DrawPoints(stroke,
							configuration.liveTipDurationSeconds,
							draw3::StrokeShape::RoundCapsule, 512, 512);
						// 模型已收敛后只推进显示与冻结路径，不能再用静止 kMove 制造点。
						draw3::UpdateIdleFreezeState(stroke, false, true,
							configuration.liveTipDurationSeconds);
						TEST_CHECK(state, stroke.modeledResults.size() == longHoldModeledCount);
						TEST_CHECK(state, stroke.realPoints.size() == longHoldPointCount);
					}
					TEST_CHECK(state, stroke.idleFrozen);
				}
			}
		}

		// 真实 kUp 输出也只能进入 scratch；累计运动结果和完成中心线使用同一净化数据。
		auto upParams = configuration.modelParams;
		upParams.prediction_params = configuration.kalmanPredictorParams;
		draw3::ActiveStroke physicalUp(
			5.0f, configuration.expectedSpeed);
		TEST_CHECK(state, physicalUp.modeler.Reset(upParams).ok());
		double upTime = 0.0;
		TEST_CHECK(state, physicalUp.modeler.Update({
			.event_type = Input::EventType::kDown,
			.position = Vec2(0.0f, 0.0f), .time = Time(upTime) },
			physicalUp.modeledResults).ok());
		draw3::AppendNewModeledPoints(physicalUp, 1200.0f);
		float upRawX = 0.0f;
		for (int sample = 0; sample < 6; ++sample)
		{
			upTime += 1.0 / 120.0;
			upRawX += 10.0f;
			TEST_CHECK(state, physicalUp.modeler.Update({
				.event_type = Input::EventType::kMove,
				.position = Vec2(upRawX, 0.0f), .time = Time(upTime) },
				physicalUp.modeledResults).ok());
			draw3::AppendNewModeledPoints(physicalUp, 1200.0f);
		}
		const size_t cumulativeCountBeforeUp = physicalUp.modeledResults.size();
		const size_t realCountBeforeUp = physicalUp.realPoints.size();
		upTime += 1.0 / 120.0;
		physicalUp.modelScratch.clear();
		TEST_CHECK(state, physicalUp.modeler.Update({
			.event_type = Input::EventType::kUp,
			.position = Vec2(upRawX, 0.0f), .time = Time(upTime) },
			physicalUp.modelScratch).ok());
		draw3::BeginEndpointAdmission(physicalUp, { upRawX, 0.0f });
		draw3::AppendEndpointBoundedModeledPoints(physicalUp,
			physicalUp.modelScratch, -1.0f, upTime, true);
		TEST_CHECK(state, physicalUp.modeledResults.size() == cumulativeCountBeforeUp);
		TEST_CHECK(state, physicalUp.realPoints.size() >= realCountBeforeUp);
		TEST_CHECK(state, NearlyEqual(physicalUp.realPoints.back().x, upRawX, 0.05f));

		// 活动与完成使用同一笔锋规则，短划不在 Up 时被强制重新收尖。
		draw3::ActiveStroke softPen(
			10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		softPen.realPoints = {
			{ 0.0f, 0.0f, 5.0f, 0.00f },
			{ 10.0f, 0.0f, 5.0f, 0.01f },
			{ 20.0f, 0.0f, 5.0f, 0.02f }
		};
		std::vector<draw3::InkPoint> completedTail;
		draw3::BuildCompletedPenTail(softPen, 0.055, completedTail);
		TEST_CHECK(state, completedTail.size() == 3);
		draw3::RebuildL0DrawPoints(softPen, 0.055,
			draw3::StrokeShape::RoundCapsule, 800, 600);
		TEST_CHECK(state, NearlyEqual(completedTail.back().r, softPen.l0DrawPoints.back().r));

		draw3::ActiveStroke click(
			10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		click.realPoints = { { 2.0f, 3.0f, 5.0f, 0.0f } };
		draw3::BuildCompletedPenTail(click, 0.055, completedTail);
		TEST_CHECK(state, completedTail.size() == 1);
		TEST_CHECK(state, NearlyEqual(completedTail.back().r, 5.0f));

		draw3::ActiveStroke shortStroke(
			10.0f, 500.0f, draw3::StrokeWidthMode::Fixed);
		shortStroke.realPoints = {
			{ 0.0f, 0.0f, 5.0f, 0.0f },
			{ 0.5f, 0.0f, 5.0f, 0.01f }
		};
		draw3::BuildCompletedPenTail(shortStroke, 0.055, completedTail);
		TEST_CHECK(state, completedTail.back().r > 4.0f);
		draw3::BuildCompletedPenTail(softPen, 0.0, completedTail);
		TEST_CHECK(state, NearlyEqual(completedTail.back().r, 5.0f));
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

	void TestPerformanceHudMetrics(TestState& state)
	{
		draw3::PerformanceHudTracker tracker;
		const uint64_t allocationsBeforeFrames =
			gAllocationCount.load(std::memory_order_relaxed);
		size_t refreshCount = 0;
		for (size_t frame = 0; frame <= 100; ++frame)
		{
			const bool refreshed = tracker.RecordDrawingFrame(
				1000.0 + static_cast<double>(frame) * 10.0,
				2.0, 0.5, true);
			if (refreshed) ++refreshCount;
		}
		TEST_CHECK(state, refreshCount == 10);
		TEST_CHECK(state, gAllocationCount.load(std::memory_order_relaxed) ==
			allocationsBeforeFrames);
		const draw3::PerformanceHudSnapshot first = tracker.Snapshot();
		TEST_CHECK(state, first.frameSampleCount == 100);
		TEST_CHECK(state, std::abs(first.averageFps - 100.0) < 0.001);
		TEST_CHECK(state, std::abs(first.averageFrameMs - 10.0) < 0.001);
		TEST_CHECK(state, first.frameJitterMs < 0.001);
		TEST_CHECK(state, std::abs(first.averageWorkMs - 2.0) < 0.001);
		TEST_CHECK(state, std::abs(first.estimatedUncappedFps - 500.0) < 0.001);
		TEST_CHECK(state, std::abs(first.averagePresentMs - 0.5) < 0.001);
		const bool nextRefresh = tracker.RecordDrawingFrame(2010.0, 4.0, 1.0, true);
		TEST_CHECK(state, !nextRefresh);
		const draw3::PerformanceHudSnapshot perFrame = tracker.Snapshot();
		TEST_CHECK(state, std::abs(perFrame.averageFps - 100.0) < 0.001);
		TEST_CHECK(state, std::abs(perFrame.estimatedUncappedFps - 500.0) < 0.001);
		TEST_CHECK(state, perFrame.frameSampleCount == 100);
		TEST_CHECK(state, std::abs(perFrame.averageFrameMs - 10.0) < 0.001);
		TEST_CHECK(state, std::abs(perFrame.averageWorkMs - 2.0) < 0.001);
		TEST_CHECK(state, std::abs(perFrame.averagePresentMs - 0.5) < 0.001);

		tracker.Reset();
		double frameStartMs = 3000.0;
		tracker.RecordDrawingFrame(frameStartMs, 3.0, 0.75, true);
		for (size_t frame = 0; frame < 100; ++frame)
		{
			frameStartMs += frame == 99 ? 20.0 : 10.0;
			tracker.RecordDrawingFrame(frameStartMs, 3.0, 0.75, true);
		}
		const draw3::PerformanceHudSnapshot mixed = tracker.Snapshot();
		TEST_CHECK(state, mixed.frameSampleCount == 100);
		TEST_CHECK(state, mixed.frameJitterMs > 0.9 && mixed.frameJitterMs < 1.1);
		const std::wstring formatted = tracker.FormatText();
		TEST_CHECK(state, formatted.find(L"性能监控") != std::wstring::npos);
		TEST_CHECK(state, formatted.find(L"估算无限制 FPS") != std::wstring::npos);
		TEST_CHECK(state, formatted.find(L"处理器") == std::wstring::npos);
		TEST_CHECK(state, formatted.find(L"显存") == std::wstring::npos);
		TEST_CHECK(state, formatted.find(L"接触") == std::wstring::npos);

		tracker.EndDrawingFrameSequence();
		tracker.RecordDrawingFrame(10000.0, 1.0, 0.25, true);
		TEST_CHECK(state, !tracker.RecordDrawingFrame(10010.0, 1.0, 0.25, true));
		TEST_CHECK(state, tracker.Snapshot().frameSampleCount == mixed.frameSampleCount);
		tracker.Reset();
		TEST_CHECK(state, tracker.Snapshot().frameSampleCount == 0);
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
		constexpr size_t kShapeContactCount = 8;
		draw3::ActiveStroke shapeScratch(5.0f, 500.0f);
		shapeScratch.modeledResults.reserve(1);
		shapeScratch.predictedResults.reserve(1);
		std::vector<draw3::ShapePrimitive> shapeBatch;
		shapeBatch.reserve(kShapeContactCount);
		const size_t modeledCapacity = shapeScratch.modeledResults.capacity();
		const size_t predictedCapacity = shapeScratch.predictedResults.capacity();
		DirectX::XMFLOAT2 shapeEndpoint = {};
		const uint64_t shapeAllocationStart =
			gAllocationCount.load(std::memory_order_relaxed);
		const double shapeMedian = medianMicroseconds([&]
			{
				for (size_t pointIndex = 0; pointIndex < kPointCount; ++pointIndex)
				{
					shapeScratch.modeledResults.resize(1);
					shapeScratch.predictedResults.resize(1);
					const float coordinate = static_cast<float>(pointIndex);
					shapeScratch.modeledResults.back().position = {
						coordinate, coordinate + 1.0f };
					shapeScratch.predictedResults.back().position = {
						coordinate + 2.0f, coordinate + 3.0f };
					shapeEndpoint = draw3::ResolveShapeLiveEndpoint(
						shapeScratch.predictedResults,
						{ coordinate, coordinate + 1.0f }, true, {});
					// Shape 每次只消费末点，模型结果长度和批次容量都不随路径增长。
					shapeScratch.modeledResults.clear();
					shapeScratch.predictedResults.clear();
					shapeBatch.clear();
					for (size_t contactIndex = 0;
						contactIndex < kShapeContactCount; ++contactIndex)
					{
						shapeBatch.push_back({
							{ 0.0f, 0.0f, 2.5f, 0.0f },
							{ shapeEndpoint.x, shapeEndpoint.y, 0.0f, 0.0f } });
					}
				}
			});
		const uint64_t shapeAllocations =
			gAllocationCount.load(std::memory_order_relaxed) - shapeAllocationStart;
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
			" shape_contacts=" << shapeBatch.size() <<
			" shape_allocations=" << shapeAllocations <<
			" shape_median_us=" << shapeMedian <<
			" pen_bounds_median_us=" << penMedian <<
			" eraser_bounds_median_us=" << eraserMedian <<
			" laser_bounds_median_us=" << laserMedian << std::endl;
		const bool valid = highlighterAllocations == 0 && shapeAllocations == 0 &&
			highlighterGeometry.primitives.size() == points.size() - 1 &&
			shapeScratch.modeledResults.empty() && shapeScratch.predictedResults.empty() &&
			shapeScratch.modeledResults.capacity() == modeledCapacity &&
			shapeScratch.predictedResults.capacity() == predictedCapacity &&
			shapeBatch.size() == kShapeContactCount &&
			std::isfinite(shapeEndpoint.x) && std::isfinite(shapeEndpoint.y) &&
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
	if (argc == 2 && wcscmp(argv[1], L"--uink-presentation-only") == 0)
		return RunPresentationUInkRoundTripTests() == 0 ? 0 : 1;
	if (argc == 4 && wcscmp(argv[1], L"--benchmark") == 0)
		return RunRuntimeBenchmark(argv[2], argv[3]);
	if (argc == 2 && wcscmp(argv[1], L"--laser-incremental-only") == 0)
		return RunLaserIncrementalCoverageTests() == 0 ? 0 : 1;
	if (argc == 2 && wcscmp(argv[1], L"--thin-gpu-tests-only") == 0)
		return RunThinStrokeGpuTests();
	if (argc == 2 && wcscmp(argv[1], L"--release-tail-tests-only") == 0)
	{
		TestState releaseState;
		TestPhysicalUpTipTime(releaseState);
		TestTerminalFallbackBoundary(releaseState);
		return releaseState.failures;
	}
	if (argc == 2 && wcscmp(argv[1], L"--drawing-perf") == 0)
		return RunDrawingPerformanceTests();
	TestState state;
	TestConcurrentDownUniqueness(state);
	TestCapacityBoundariesAndReuse(state);
	TestInvalidSnapshotBoundaries(state);
	TestPublishDownDoesNotAllocate(state);
	TestMoveUpRaceAndShutdown(state);
	TestWakeProtocols(state);
	TestRtsStylusConversions(state);
	TestRtsTouchDownCursorInvalidation(state);
	TestRtsDecoderAndBindingHotPath(state);
	TestInputWidthModesAndHardwarePressure(state);
	TestSpeedEraserOcController(state);
	TestInterruptedStrokeReconnectPolicy(state);
	TestInterruptedStrokeReconnectModelLifecycle(state);
	TestLowSpeedStopConvergence(state);
	TestEndpointAdmissionContracts(state);
	TestStationaryTipAging(state);
	TestPhysicalUpTipTime(state);
	TestTerminalFallbackBoundary(state);
	TestSparsePenFrames(state);
	TestInvertedPenPolicy(state);
	TestHapticFeedbackContracts(state);
	TestPerformanceHudMetrics(state);
	state.failures += RunHighlighterGeometryTests();
	state.failures += RunCanvasNavigationTests();
	state.failures += RunDesktopAutoSaveTests();
	state.failures += RunPresentationAutoSaveTests();
	state.failures += RunInkDocumentTests();
	state.failures += RunInkHistoryTests();
	state.failures += RunLaserIncrementalCoverageTests();
	state.failures += RunPenCursorTests();
	state.failures += RunUInkTests();
	state.failures += RunThinStrokeGpuTests();
	if (state.failures == 0)
	{
		std::cout << "All draw3 contact input tests passed." << std::endl;
		return 0;
	}
	std::cerr << state.failures << " draw3 tests failed." << std::endl;
	return 1;
}
