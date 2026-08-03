module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <functional>
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
		std::array<double, PerformanceHudTracker::kSampleCapacity> frameIntervalsMs = {};
		std::array<double, PerformanceHudTracker::kSampleCapacity> workDurationsMs = {};
		std::array<double, PerformanceHudTracker::kSampleCapacity> presentDurationsMs = {};
		size_t frameIntervalCount = 0;
		size_t workDurationCount = 0;
		size_t presentDurationCount = 0;
		double windowStartMs = 0.0;
		double lastFrameStartMs = 0.0;
		uint64_t processCpuStartTicks = 0;
		bool processCpuStartValid = false;
		PerformanceHudSnapshot snapshot;
	};

	namespace
	{
		struct ProcessMemoryCounters
		{
			DWORD cb = 0;
			DWORD pageFaultCount = 0;
			SIZE_T peakWorkingSetSize = 0;
			SIZE_T workingSetSize = 0;
			SIZE_T quotaPeakPagedPoolUsage = 0;
			SIZE_T quotaPagedPoolUsage = 0;
			SIZE_T quotaPeakNonPagedPoolUsage = 0;
			SIZE_T quotaNonPagedPoolUsage = 0;
			SIZE_T pagefileUsage = 0;
			SIZE_T peakPagefileUsage = 0;
		};

		using GetProcessMemoryInfoFn = BOOL(WINAPI*)(
			HANDLE, ProcessMemoryCounters*, DWORD);

		GetProcessMemoryInfoFn ResolveGetProcessMemoryInfo() noexcept
		{
			// 避免静态依赖 psapi；旧系统上再回退到同名导出。
			if (HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll"))
			{
				if (FARPROC procedure = GetProcAddress(kernel32, "K32GetProcessMemoryInfo"))
					return reinterpret_cast<GetProcessMemoryInfoFn>(procedure);
			}

			if (HMODULE psapi = LoadLibraryW(L"psapi.dll"))
			{
				if (FARPROC procedure = GetProcAddress(psapi, "GetProcessMemoryInfo"))
					return reinterpret_cast<GetProcessMemoryInfoFn>(procedure);
			}
			return nullptr;
		}

		uint64_t FileTimeTicks(const FILETIME& value) noexcept
		{
			ULARGE_INTEGER ticks = {};
			ticks.LowPart = value.dwLowDateTime;
			ticks.HighPart = value.dwHighDateTime;
			return ticks.QuadPart;
		}

		bool ReadProcessCpuTicks(uint64_t& ticks) noexcept
		{
			FILETIME creation = {};
			FILETIME exit = {};
			FILETIME kernel = {};
			FILETIME user = {};
			if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
				return false;
			ticks = FileTimeTicks(kernel) + FileTimeTicks(user);
			return true;
		}

		double ReadWorkingSetMiB() noexcept
		{
			static const GetProcessMemoryInfoFn getProcessMemoryInfo =
				ResolveGetProcessMemoryInfo();
			ProcessMemoryCounters counters;
			counters.cb = sizeof(counters);
			if (!getProcessMemoryInfo || !getProcessMemoryInfo(
				GetCurrentProcess(), &counters, sizeof(counters)))
				return 0.0;
			return static_cast<double>(counters.workingSetSize) / (1024.0 * 1024.0);
		}

		template <size_t Capacity>
		double Average(const std::array<double, Capacity>& values, size_t count) noexcept
		{
			if (count == 0) return 0.0;
			double sum = 0.0;
			for (size_t index = 0; index < count; ++index) sum += values[index];
			return sum / static_cast<double>(count);
		}

		template <size_t Capacity>
		double Percentile99(const std::array<double, Capacity>& values, size_t count) noexcept
		{
			if (count == 0) return 0.0;
			std::array<double, Capacity> sorted = values;
			std::sort(sorted.begin(), sorted.begin() + count);
			const size_t index = std::min(count - 1,
				static_cast<size_t>(std::ceil(static_cast<double>(count) * 0.99)) - 1);
			return sorted[index];
		}

		template <size_t Capacity>
		double OnePercentLowFps(
			const std::array<double, Capacity>& values, size_t count) noexcept
		{
			if (count == 0) return 0.0;
			std::array<double, Capacity> sorted = values;
			std::sort(sorted.begin(), sorted.begin() + count, std::greater<double>());
			const size_t slowCount = std::max<size_t>(
				1, static_cast<size_t>(std::ceil(static_cast<double>(count) * 0.01)));
			double slowFrameTotalMs = 0.0;
			for (size_t index = 0; index < slowCount; ++index)
				slowFrameTotalMs += sorted[index];
			const double slowFrameAverageMs =
				slowFrameTotalMs / static_cast<double>(slowCount);
			return slowFrameAverageMs > 0.0 ? 1000.0 / slowFrameAverageMs : 0.0;
		}

		template <size_t Capacity>
		double StandardDeviation(const std::array<double, Capacity>& values,
			size_t count, double average) noexcept
		{
			if (count == 0) return 0.0;
			double squaredDifferenceTotal = 0.0;
			for (size_t index = 0; index < count; ++index)
			{
				const double difference = values[index] - average;
				squaredDifferenceTotal += difference * difference;
			}
			return std::sqrt(squaredDifferenceTotal / static_cast<double>(count));
		}

		void BeginPerformanceHudWindow(
			PerformanceHudTrackerImpl& tracker, double frameStartMs) noexcept
		{
			tracker.frameIntervalCount = 0;
			tracker.workDurationCount = 0;
			tracker.presentDurationCount = 0;
			tracker.windowStartMs = frameStartMs;
			tracker.lastFrameStartMs = frameStartMs;
			tracker.processCpuStartValid =
				ReadProcessCpuTicks(tracker.processCpuStartTicks);
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
			!std::isfinite(presentMs) || frameStartMs < 0.0) return false;

		PerformanceHudTrackerImpl& tracker = *impl_;
		if (tracker.windowStartMs <= 0.0)
			BeginPerformanceHudWindow(tracker, frameStartMs);
		else
		{
			const double intervalMs = frameStartMs - tracker.lastFrameStartMs;
			if (intervalMs > 0.0 &&
				tracker.frameIntervalCount < tracker.frameIntervalsMs.size())
				tracker.frameIntervalsMs[tracker.frameIntervalCount++] = intervalMs;
			tracker.lastFrameStartMs = frameStartMs;
		}

		if (tracker.workDurationCount < tracker.workDurationsMs.size())
			tracker.workDurationsMs[tracker.workDurationCount++] = std::max(0.0, workMs);
		if (presented && tracker.presentDurationCount < tracker.presentDurationsMs.size())
			tracker.presentDurationsMs[tracker.presentDurationCount++] =
				std::max(0.0, presentMs);

		const double elapsedMs = frameStartMs - tracker.windowStartMs;
		if (elapsedMs < 1000.0) return false;

		PerformanceHudSnapshot snapshot;
		snapshot.frameSampleCount = tracker.frameIntervalCount;
		snapshot.averageFrameMs = Average(
			tracker.frameIntervalsMs, tracker.frameIntervalCount);
		snapshot.averageFps = snapshot.averageFrameMs > 0.0
			? 1000.0 / snapshot.averageFrameMs : 0.0;
		snapshot.onePercentLowFps = OnePercentLowFps(
			tracker.frameIntervalsMs, tracker.frameIntervalCount);
		snapshot.p99FrameMs = Percentile99(
			tracker.frameIntervalsMs, tracker.frameIntervalCount);
		snapshot.frameJitterMs = StandardDeviation(
			tracker.frameIntervalsMs, tracker.frameIntervalCount,
			snapshot.averageFrameMs);
		snapshot.averageWorkMs = Average(
			tracker.workDurationsMs, tracker.workDurationCount);
		snapshot.averagePresentMs = Average(
			tracker.presentDurationsMs, tracker.presentDurationCount);
		snapshot.workingSetMiB = ReadWorkingSetMiB();

		uint64_t processCpuEndTicks = 0;
		if (tracker.processCpuStartValid && ReadProcessCpuTicks(processCpuEndTicks) &&
			processCpuEndTicks >= tracker.processCpuStartTicks && elapsedMs > 0.0)
		{
			SYSTEM_INFO systemInfo = {};
			GetSystemInfo(&systemInfo);
			const DWORD processorCount = std::max<DWORD>(systemInfo.dwNumberOfProcessors, 1);
			const double availableTicks = elapsedMs * 10000.0 * processorCount;
			snapshot.processCpuPercent = availableTicks > 0.0
				? std::clamp(static_cast<double>(
					processCpuEndTicks - tracker.processCpuStartTicks) /
					availableTicks * 100.0, 0.0, 100.0)
				: 0.0;
		}

		tracker.snapshot = snapshot;
		BeginPerformanceHudWindow(tracker, frameStartMs);
		return true;
	}

	void PerformanceHudTracker::EndDrawingFrameSequence() noexcept
	{
		impl_->frameIntervalCount = 0;
		impl_->workDurationCount = 0;
		impl_->presentDurationCount = 0;
		impl_->windowStartMs = 0.0;
		impl_->lastFrameStartMs = 0.0;
		impl_->processCpuStartTicks = 0;
		impl_->processCpuStartValid = false;
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

	std::wstring PerformanceHudTracker::FormatText(double gpuMemoryMiB) const
	{
		const PerformanceHudSnapshot& snapshot = impl_->snapshot;
		wchar_t text[512] = {};
		if (gpuMemoryMiB >= 0.0)
		{
			swprintf_s(text,
				L"PERF TEST [ON]\r\n"
				L"FPS %.1f   1%% LOW %.1f\r\n"
				L"FRAME %.2f ms   P99 %.2f ms   JITTER %.2f ms\r\n"
				L"CPU %.1f%%   RAM %.1f MiB   GPU MEM %.1f MiB\r\n"
				L"WORK %.2f ms   PRESENT %.2f ms   SAMPLES %zu",
				snapshot.averageFps, snapshot.onePercentLowFps,
				snapshot.averageFrameMs, snapshot.p99FrameMs,
				snapshot.frameJitterMs, snapshot.processCpuPercent,
				snapshot.workingSetMiB, gpuMemoryMiB,
				snapshot.averageWorkMs, snapshot.averagePresentMs,
				snapshot.frameSampleCount);
		}
		else
		{
			swprintf_s(text,
				L"PERF TEST [ON]\r\n"
				L"FPS %.1f   1%% LOW %.1f\r\n"
				L"FRAME %.2f ms   P99 %.2f ms   JITTER %.2f ms\r\n"
				L"CPU %.1f%%   RAM %.1f MiB   GPU MEM N/A\r\n"
				L"WORK %.2f ms   PRESENT %.2f ms   SAMPLES %zu",
				snapshot.averageFps, snapshot.onePercentLowFps,
				snapshot.averageFrameMs, snapshot.p99FrameMs,
				snapshot.frameJitterMs, snapshot.processCpuPercent,
				snapshot.workingSetMiB,
				snapshot.averageWorkMs, snapshot.averagePresentMs,
				snapshot.frameSampleCount);
		}
		return text;
	}

	struct RuntimeMetricsSessionImpl
	{
		explicit RuntimeMetricsSessionImpl(size_t maximumSamplesValue)
			: maximumSamples(std::max<size_t>(maximumSamplesValue, 1))
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
		if (!record || eligibleQpc <= 0 || impl_->stagedLandings.size() >= 64) return;
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
			if (impl_->WasLandingPresented(staged.key)) continue;
			impl_->presentedLandingKeys.push_back(staged.key);
			if (impl_->landingSamples.size() >= impl_->maximumSamples) continue;
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
