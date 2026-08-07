module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

module draw3.runtime_metrics;

namespace draw3
{
	namespace
	{
		constexpr double kLandingP99LimitMs = 8.33;
		constexpr double kFrameIntervalP99LimitMs = 9.5;
		constexpr double kLongFrameLimitMs = 16.67;
		constexpr double kLongFrameRatioLimit = 0.01;
		constexpr size_t kRequiredLandingCount = 200;
		constexpr size_t kMaximumRuntimeMetricSamples = 1u << 20;
		constexpr double kRequiredIdleDurationMs = 4900.0;

		struct LandingKey
		{
			ContactRecord* record = nullptr;
			uint64_t generation = 0;

			bool operator==(const LandingKey&) const = default;
		};

		struct StagedLanding
		{
			LandingKey key;
			InputDeviceType deviceType = InputDeviceType::Touch;
			uint32_t tool = 0;
			int64_t eligibleQpc = 0;
		};

		struct LandingSample
		{
			InputDeviceType deviceType = InputDeviceType::Touch;
			uint32_t tool = 0;
			double latencyMs = 0.0;
		};

		double Percentile99(const std::vector<double>& values)
		{
			if (values.empty()) return 0.0;
			std::vector<double> sorted = values;
			std::sort(sorted.begin(), sorted.end());
			const size_t index = std::min(sorted.size() - 1,
				static_cast<size_t>(std::ceil(static_cast<double>(sorted.size()) * 0.99)) - 1);
			return sorted[index];
		}

		const char* DeviceName(InputDeviceType type)
		{
			switch (type)
			{
			case InputDeviceType::Touch: return "Touch";
			case InputDeviceType::Pen: return "Pen";
			case InputDeviceType::MouseLeft: return "MouseLeft";
			case InputDeviceType::MouseRight: return "MouseRight";
			default: return "Unknown";
			}
		}

		const char* ToolName(uint32_t tool)
		{
			switch (tool)
			{
			case 0: return "Pen";
			case 1: return "Highlighter";
			case 2: return "Eraser";
			case 3: return "Laser";
			default: return "Unknown";
			}
		}

		std::vector<double> CollectLandingLatencies(const std::vector<LandingSample>& samples)
		{
			std::vector<double> values;
			values.reserve(samples.size());
			for (const LandingSample& sample : samples)
				values.push_back(sample.latencyMs);
			return values;
		}

		const char* ArchitectureName()
		{
#if defined(_M_ARM64)
			return "ARM64";
#elif defined(_M_X64)
			return "x64";
#elif defined(_M_IX86)
			return "x86";
#else
			return "unknown";
#endif
		}

		void WriteDoubleArray(std::ostringstream& stream,
			const char* name, const std::vector<double>& values, bool trailingComma)
		{
			stream << "    \"" << name << "\": [";
			for (size_t index = 0; index < values.size(); ++index)
			{
				if (index != 0) stream << ", ";
				stream << values[index];
			}
			stream << "]" << (trailingComma ? "," : "") << "\n";
		}

		bool WriteUtf8File(const wchar_t* path, const std::string& text)
		{
			if (!path || path[0] == L'\0') return false;
			HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool succeeded = text.size() <= MAXDWORD &&
				WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
				written == text.size();
			CloseHandle(file);
			return succeeded;
		}
	}

	struct PerformanceHudTrackerImpl
	{
		size_t frameSampleCount = 0;
		size_t workSampleCount = 0;
		size_t presentSampleCount = 0;
		double frameIntervalSumMs = 0.0;
		double frameIntervalSquaredSumMs = 0.0;
		double workDurationSumMs = 0.0;
		double presentDurationSumMs = 0.0;
		double windowStartMs = 0.0;
		double lastFrameStartMs = 0.0;
		double nextSnapshotMs = 0.0;
		PerformanceHudSnapshot snapshot;
	};

	namespace
	{
		void BeginPerformanceHudWindow(
			PerformanceHudTrackerImpl& tracker, double frameStartMs) noexcept
		{
			tracker.frameSampleCount = 0;
			tracker.workSampleCount = 0;
			tracker.presentSampleCount = 0;
			tracker.frameIntervalSumMs = 0.0;
			tracker.frameIntervalSquaredSumMs = 0.0;
			tracker.workDurationSumMs = 0.0;
			tracker.presentDurationSumMs = 0.0;
			tracker.windowStartMs = frameStartMs;
			tracker.lastFrameStartMs = frameStartMs;
			tracker.nextSnapshotMs = frameStartMs + 100.0;
		}

		void RefreshPerformanceHudSnapshot(PerformanceHudTrackerImpl& tracker) noexcept
		{
			PerformanceHudSnapshot& snapshot = tracker.snapshot;
			snapshot.frameSampleCount = tracker.frameSampleCount;
			snapshot.averageFrameMs = tracker.frameSampleCount > 0
			? tracker.frameIntervalSumMs / static_cast<double>(tracker.frameSampleCount) : 0.0;
			const double variance = tracker.frameSampleCount > 0
				? tracker.frameIntervalSquaredSumMs /
					static_cast<double>(tracker.frameSampleCount) -
					snapshot.averageFrameMs * snapshot.averageFrameMs : 0.0;
			snapshot.frameJitterMs = std::sqrt(std::max(0.0, variance));
			snapshot.averageFps = snapshot.averageFrameMs > 0.0
				? 1000.0 / snapshot.averageFrameMs : 0.0;
			snapshot.averageWorkMs = tracker.workSampleCount > 0
				? tracker.workDurationSumMs / static_cast<double>(tracker.workSampleCount) : 0.0;
			snapshot.estimatedUncappedFps = snapshot.averageWorkMs > 0.0
				? 1000.0 / snapshot.averageWorkMs : 0.0;
			snapshot.averagePresentMs = tracker.presentSampleCount > 0
				? tracker.presentDurationSumMs /
					static_cast<double>(tracker.presentSampleCount) : 0.0;
		}
	}

	PerformanceHudTracker::PerformanceHudTracker()
		: impl_(std::make_unique<PerformanceHudTrackerImpl>())
	{
	}

	PerformanceHudTracker::~PerformanceHudTracker() = default;

	bool PerformanceHudTracker::RecordDrawingFrame(double frameStartMs,
		double workMs, double presentMs, bool presented) noexcept
	{
		if (!std::isfinite(frameStartMs) || !std::isfinite(workMs) ||
			!std::isfinite(presentMs) || frameStartMs < 0.0 ||
			workMs < 0.0 || presentMs < 0.0) return false;

		PerformanceHudTrackerImpl& tracker = *impl_;
		if (tracker.windowStartMs <= 0.0)
			BeginPerformanceHudWindow(tracker, frameStartMs);
		else
		{
			const double intervalMs = frameStartMs - tracker.lastFrameStartMs;
			if (intervalMs > 0.0)
			{
				++tracker.frameSampleCount;
				tracker.frameIntervalSumMs += intervalMs;
				tracker.frameIntervalSquaredSumMs += intervalMs * intervalMs;
			}
			tracker.lastFrameStartMs = frameStartMs;
		}

		++tracker.workSampleCount;
		tracker.workDurationSumMs += workMs;
		if (presented)
		{
			++tracker.presentSampleCount;
			tracker.presentDurationSumMs += presentMs;
		}

		if (frameStartMs < tracker.nextSnapshotMs) return false;
		RefreshPerformanceHudSnapshot(tracker);
		do
		{
			tracker.nextSnapshotMs += 100.0;
		} while (tracker.nextSnapshotMs <= frameStartMs);
		return true;
	}

	void PerformanceHudTracker::EndDrawingFrameSequence() noexcept
	{
		impl_->frameSampleCount = 0;
		impl_->workSampleCount = 0;
		impl_->presentSampleCount = 0;
		impl_->frameIntervalSumMs = 0.0;
		impl_->frameIntervalSquaredSumMs = 0.0;
		impl_->workDurationSumMs = 0.0;
		impl_->presentDurationSumMs = 0.0;
		impl_->windowStartMs = 0.0;
		impl_->lastFrameStartMs = 0.0;
		impl_->nextSnapshotMs = 0.0;
	}

	void PerformanceHudTracker::Reset() noexcept
	{
		EndDrawingFrameSequence();
		impl_->snapshot = {};
	}

	const PerformanceHudSnapshot& PerformanceHudTracker::Snapshot() const noexcept
	{
		return impl_->snapshot;
	}

	std::wstring PerformanceHudTracker::FormatText() const
	{
		const PerformanceHudSnapshot& snapshot = impl_->snapshot;
		wchar_t text[512] = {};
		swprintf_s(text,
			L"性能监控\r\n\r\n"
			L"平均 FPS        %7.1f\r\n"
			L"估算无限制 FPS  %7.1f\r\n"
			L"平均帧时        %7.2f ms\r\n"
			L"帧抖动          %7.2f ms\r\n"
			L"绘制耗时        %7.2f ms\r\n"
			L"Present         %7.2f ms\r\n"
			L"样本             %6zu",
			snapshot.averageFps, snapshot.estimatedUncappedFps,
			snapshot.averageFrameMs, snapshot.frameJitterMs,
			snapshot.averageWorkMs, snapshot.averagePresentMs,
			snapshot.frameSampleCount);
		return text;
	}

	struct RuntimeMetricsSessionImpl
	{
		explicit RuntimeMetricsSessionImpl(size_t maximumSamplesValue)
			: maximumSamples(std::clamp(
				maximumSamplesValue, size_t{ 1 }, kMaximumRuntimeMetricSamples))
		{
			stagedLandings.reserve(32);
			presentedLandingKeys.reserve(512);
			landingSamples.reserve(std::min(maximumSamples, size_t{ 2048 }));
			frameIntervalsMs.reserve(maximumSamples);
			workDurationsMs.reserve(maximumSamples);
			presentDurationsMs.reserve(maximumSamples);
			LARGE_INTEGER frequency = {};
			if (QueryPerformanceFrequency(&frequency)) qpcFrequency = frequency.QuadPart;
		}

		bool WasLandingPresented(const LandingKey& key) const noexcept
		{
			return std::find(presentedLandingKeys.begin(), presentedLandingKeys.end(), key) !=
				presentedLandingKeys.end();
		}

		double LongFrameRatio() const noexcept
		{
			if (frameIntervalsMs.empty()) return 0.0;
			const size_t longFrames = static_cast<size_t>(std::count_if(
				frameIntervalsMs.begin(), frameIntervalsMs.end(),
				[](double value) { return value > kLongFrameLimitMs; }));
			return static_cast<double>(longFrames) /
				static_cast<double>(frameIntervalsMs.size());
		}

		const size_t maximumSamples;
		int64_t qpcFrequency = 0;
		std::vector<StagedLanding> stagedLandings;
		std::vector<LandingKey> presentedLandingKeys;
		std::vector<LandingSample> landingSamples;
		std::vector<double> frameIntervalsMs;
		std::vector<double> workDurationsMs;
		std::vector<double> presentDurationsMs;
		double lastActiveFrameStartMs = 0.0;
		uint64_t totalFrames = 0;
		uint64_t totalPresents = 0;
		bool idleActive = false;
		double idleStartMs = 0.0;
		uint64_t idleStartFrames = 0;
		uint64_t idleStartPresents = 0;
		double longestIdleMs = 0.0;
		uint64_t maximumIdleFrameGrowth = 0;
		uint64_t maximumIdlePresentGrowth = 0;
	};

	RuntimeMetricsSession::RuntimeMetricsSession(size_t maximumSamples)
		: impl_(std::make_unique<RuntimeMetricsSessionImpl>(maximumSamples))
	{
	}

	RuntimeMetricsSession::~RuntimeMetricsSession() = default;

	void RuntimeMetricsSession::BeginFrame() noexcept
	{
		impl_->stagedLandings.clear();
	}

	void RuntimeMetricsSession::StageLanding(ContactRecord* record, uint64_t generation,
		InputDeviceType deviceType, uint32_t tool, int64_t eligibleQpc)
	{
		if (!record || eligibleQpc <= 0 ||
			impl_->landingSamples.size() >= impl_->maximumSamples ||
			impl_->stagedLandings.size() >= 64) return;
		const LandingKey key{ record, generation };
		if (impl_->WasLandingPresented(key)) return;
		const auto duplicate = std::find_if(impl_->stagedLandings.begin(), impl_->stagedLandings.end(),
			[&](const StagedLanding& candidate) { return candidate.key == key; });
		if (duplicate == impl_->stagedLandings.end())
			impl_->stagedLandings.push_back({ key, deviceType, tool, eligibleQpc });
	}

	void RuntimeMetricsSession::CommitStagedLandings(bool presentSucceeded, int64_t presentQpc)
	{
		if (!presentSucceeded || presentQpc <= 0 || impl_->qpcFrequency <= 0)
		{
			impl_->stagedLandings.clear();
			return;
		}
		for (const StagedLanding& staged : impl_->stagedLandings)
		{
			if (impl_->landingSamples.size() >= impl_->maximumSamples) break;
			if (impl_->WasLandingPresented(staged.key)) continue;
			impl_->presentedLandingKeys.push_back(staged.key);
			const double latencyMs = std::max(0.0,
				static_cast<double>(presentQpc - staged.eligibleQpc) * 1000.0 /
				static_cast<double>(impl_->qpcFrequency));
			impl_->landingSamples.push_back({ staged.deviceType, staged.tool, latencyMs });
		}
		impl_->stagedLandings.clear();
	}

	void RuntimeMetricsSession::RecordActiveFrame(double frameStartMs, double workMs,
		double presentMs, bool presented)
	{
		++impl_->totalFrames;
		if (impl_->lastActiveFrameStartMs > 0.0 &&
			impl_->frameIntervalsMs.size() < impl_->maximumSamples)
			impl_->frameIntervalsMs.push_back(frameStartMs - impl_->lastActiveFrameStartMs);
		impl_->lastActiveFrameStartMs = frameStartMs;
		if (impl_->workDurationsMs.size() < impl_->maximumSamples)
			impl_->workDurationsMs.push_back(std::max(0.0, workMs));
		if (presented && impl_->presentDurationsMs.size() < impl_->maximumSamples)
			impl_->presentDurationsMs.push_back(std::max(0.0, presentMs));
	}

	void RuntimeMetricsSession::EndActiveFrameSequence() noexcept
	{
		impl_->lastActiveFrameStartMs = 0.0;
	}

	void RuntimeMetricsSession::RecordPresent(double) noexcept
	{
		++impl_->totalPresents;
	}

	void RuntimeMetricsSession::BeginIdle(double nowMs) noexcept
	{
		if (impl_->idleActive) return;
		impl_->idleActive = true;
		impl_->idleStartMs = nowMs;
		impl_->idleStartFrames = impl_->totalFrames;
		impl_->idleStartPresents = impl_->totalPresents;
		impl_->lastActiveFrameStartMs = 0.0; // 不把两笔之间的阻塞时间计入活动帧间隔。
	}

	void RuntimeMetricsSession::EndIdle(double nowMs) noexcept
	{
		if (!impl_->idleActive) return;
		impl_->longestIdleMs = std::max(impl_->longestIdleMs,
			std::max(0.0, nowMs - impl_->idleStartMs));
		impl_->maximumIdleFrameGrowth = std::max(impl_->maximumIdleFrameGrowth,
			impl_->totalFrames - impl_->idleStartFrames);
		impl_->maximumIdlePresentGrowth = std::max(impl_->maximumIdlePresentGrowth,
			impl_->totalPresents - impl_->idleStartPresents);
		impl_->idleActive = false;
	}

	bool RuntimeMetricsSession::MeetsStrictThresholds() const
	{
		const std::vector<double> landingLatencies =
			CollectLandingLatencies(impl_->landingSamples);
		return landingLatencies.size() >= kRequiredLandingCount &&
			Percentile99(landingLatencies) <= kLandingP99LimitMs &&
			!impl_->frameIntervalsMs.empty() &&
			Percentile99(impl_->frameIntervalsMs) <= kFrameIntervalP99LimitMs &&
			impl_->LongFrameRatio() < kLongFrameRatioLimit &&
			impl_->longestIdleMs >= kRequiredIdleDurationMs &&
			impl_->maximumIdleFrameGrowth == 0 &&
			impl_->maximumIdlePresentGrowth == 0;
	}

	bool RuntimeMetricsSession::WriteJson(const wchar_t* outputPath,
		const ContactInputDiagnosticsSnapshot& inputDiagnostics) const
	{
		const std::vector<double> landingLatencies =
			CollectLandingLatencies(impl_->landingSamples);
		const bool strictPass = MeetsStrictThresholds();

		std::ostringstream stream;
		stream << std::fixed << std::setprecision(4);
		stream << "{\n";
		stream << "  \"environment\": {\n";
		stream << "    \"architecture\": \"" << ArchitectureName() << "\",\n";
		stream << "    \"pointerBytes\": " << sizeof(void*) << "\n";
		stream << "  },\n";
		stream << "  \"thresholds\": {\n";
		stream << "    \"requiredLandings\": " << kRequiredLandingCount << ",\n";
		stream << "    \"landingPopulation\": \"Pen+Highlighter+Eraser+Laser Down to Present\",\n";
		stream << "    \"landingP99Ms\": " << kLandingP99LimitMs << ",\n";
		stream << "    \"frameIntervalP99Ms\": " << kFrameIntervalP99LimitMs << ",\n";
		stream << "    \"longFrameMs\": " << kLongFrameLimitMs << ",\n";
		stream << "    \"longFrameRatio\": " << kLongFrameRatioLimit << "\n";
		stream << "  },\n";
		stream << "  \"summary\": {\n";
		stream << "    \"strictPass\": " << (strictPass ? "true" : "false") << ",\n";
		stream << "    \"landingCount\": " << landingLatencies.size() << ",\n";
		stream << "    \"landingP99Ms\": " << Percentile99(landingLatencies) << ",\n";
		stream << "    \"totalLandingCount\": " << impl_->landingSamples.size() << ",\n";
		stream << "    \"activeFrameCount\": " << impl_->totalFrames << ",\n";
		stream << "    \"frameIntervalP99Ms\": " << Percentile99(impl_->frameIntervalsMs) << ",\n";
		stream << "    \"longFrameRatio\": " << impl_->LongFrameRatio() << ",\n";
		stream << "    \"presentCount\": " << impl_->totalPresents << ",\n";
		stream << "    \"longestIdleMs\": " << impl_->longestIdleMs << ",\n";
		stream << "    \"maximumIdleFrameGrowth\": " << impl_->maximumIdleFrameGrowth << ",\n";
		stream << "    \"maximumIdlePresentGrowth\": " << impl_->maximumIdlePresentGrowth << "\n";
		stream << "  },\n";
		stream << "  \"input\": {\n";
		stream << "    \"slotCapacity\": " << inputDiagnostics.slotCapacity << ",\n";
		stream << "    \"occupiedSlots\": " << inputDiagnostics.occupiedSlots << ",\n";
		stream << "    \"downPublished\": " << inputDiagnostics.downPublished << ",\n";
		stream << "    \"downRejected\": " << inputDiagnostics.downRejected << ",\n";
		stream << "    \"movePublished\": " << inputDiagnostics.movePublished << ",\n";
		stream << "    \"moveContended\": " << inputDiagnostics.moveContended << ",\n";
		stream << "    \"terminalPublished\": " << inputDiagnostics.terminalPublished << ",\n";
		stream << "    \"recycled\": " << inputDiagnostics.recycled << ",\n";
		stream << "    \"controlWakes\": " << inputDiagnostics.controlWakes << ",\n";
		stream << "    \"activeWaits\": " << inputDiagnostics.activeWaits << "\n";
		stream << "  },\n";
		stream << "  \"landings\": [\n";
		for (size_t index = 0; index < impl_->landingSamples.size(); ++index)
		{
			const LandingSample& sample = impl_->landingSamples[index];
			stream << "    {\"device\": \"" << DeviceName(sample.deviceType)
				<< "\", \"tool\": \"" << ToolName(sample.tool)
				<< "\", \"origin\": \"Down"
				<< "\", \"latencyMs\": " << sample.latencyMs << "}";
			stream << (index + 1 == impl_->landingSamples.size() ? "\n" : ",\n");
		}
		stream << "  ],\n";
		stream << "  \"raw\": {\n";
		WriteDoubleArray(stream, "frameIntervalsMs", impl_->frameIntervalsMs, true);
		WriteDoubleArray(stream, "workDurationsMs", impl_->workDurationsMs, true);
		WriteDoubleArray(stream, "presentDurationsMs", impl_->presentDurationsMs, false);
		stream << "  }\n";
		stream << "}\n";
		return WriteUtf8File(outputPath, stream.str());
	}
}
