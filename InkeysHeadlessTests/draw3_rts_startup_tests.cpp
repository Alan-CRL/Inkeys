#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.RtsStartup.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
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
	TestAttemptSequence(FailureStage::None, true, failures);
	TestAttemptSequence(FailureStage::PacketDescription, true, failures);
	TestAttemptSequence(FailureStage::Enable, true, failures); // 现场 Set 成功、Enable=E_INVALIDARG。
	TestAttemptSequence(FailureStage::InputConfiguration, true, failures);
	TestAttemptSequence(FailureStage::Enable, false, failures);
	TestAttemptSequence(FailureStage::Create, false, failures);
	TestExceptionStopsSelection(failures);
	TestCallbackCloseAndDrain(failures);
	TestCandidateDownDoesNotPublish(failures);
	if (!Expect(fatalPublications.load(std::memory_order_relaxed) == 0, "healthy attempts never publish fatal shutdown")) ++failures;
	std::cout << "[Draw3RtsStartup] " << (failures ? "FAIL" : "PASS")
		<< " (production orchestration/gates; no COM provider or HWND)\n";
	return failures;
}
