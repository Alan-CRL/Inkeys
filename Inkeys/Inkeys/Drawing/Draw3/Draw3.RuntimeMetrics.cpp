module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>

module Inkeys.Drawing.Draw3.runtime_metrics;

import Inkeys.Drawing.Draw3.window_control;
import Inkeys.Drawing.Draw3.ink_prediction;
import Inkeys.Drawing.Draw3.transparent_presentation;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr double kLandingP99LimitMs = 8.33;
		constexpr double kFrameIntervalP99LimitMs = 9.5;
		constexpr double kLongFrameLimitMs = 16.67;
		constexpr double kLongFrameRatioLimit = 0.01;
		constexpr size_t kRequiredLandingCount = 200;
		constexpr size_t kRequiredPercentile99Population = 1000;
		constexpr size_t kMaximumRuntimeMetricSamples = 1u << 20;
		constexpr size_t kRuntimeMetricByteBudget = 32u * 1024 * 1024;
		constexpr size_t kAllocationSlack = 64u * 1024;
		constexpr size_t kPendingCapacity = 64;
		constexpr size_t kNoIndex = (std::numeric_limits<size_t>::max)();
		constexpr double kRequiredIdleDurationMs = 4900.0;

		struct LandingKey
		{
			ContactRecord* record = nullptr;
			uint64_t generation = 0;
			bool operator==(const LandingKey&) const = default;
		};

		enum class ContactStatus : uint8_t { Registered, Pending, Confirmed, Unpresented, Invalid, Legacy };
		struct MetricContact
		{
			LandingKey key;
			InputDeviceType deviceType = InputDeviceType::Touch;
			uint32_t tool = 0;
			int64_t downQpc = 0;
			uint64_t ordinal = 0;
			ContactStatus status = ContactStatus::Registered;
		};

		struct PendingLanding
		{
			size_t contactIndex = kNoIndex;
			RuntimeMetricsLandingProof proof;
		};

		struct LandingSample
		{
			InputDeviceType deviceType = InputDeviceType::Touch;
			uint32_t tool = 0;
			uint64_t ordinal = 0;
			uint64_t generation = 0;
			int64_t downQpc = 0;
			int64_t presentReturnQpc = 0;
			double latencyMs = 0.0;
			RuntimeMetricsLandingProof proof;
		};

		enum class PresentOutcome : uint8_t { Unknown, Succeeded, Failed };
		struct PresentSample
		{
			uint64_t frameSerial = 0;
			double wallMs = 0.0;
			PresentOutcome outcome = PresentOutcome::Unknown;
		};

		bool ValidDuration(double value) noexcept { return std::isfinite(value) && value >= 0.0; }

		size_t HashLandingKey(const LandingKey& key) noexcept
		{
			uint64_t value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(key.record));
			value ^= key.generation + 0x9E3779B97F4A7C15ull + (value << 6) + (value >> 2);
			value ^= value >> 33;
			value *= 0xFF51AFD7ED558CCDull;
			value ^= value >> 33;
			return static_cast<size_t>(value);
		}

		bool ValidProof(const RuntimeMetricsLandingProof& proof) noexcept
		{
			return proof.frameSerial != 0 && proof.canvas.workspace != 0 &&
				proof.canvas.page != 0 && proof.canvas.sceneGeneration != 0 &&
				proof.canvas.rasterGeneration != 0 && proof.canvas.outputGeneration != 0 &&
				proof.contentToken != 0 && proof.consumedSequence != 0 &&
				(proof.kind == RuntimeMetricsProofKind::Live ||
					proof.kind == RuntimeMetricsProofKind::Laser ||
					(proof.kind == RuntimeMetricsProofKind::Stored && proof.itemToken != 0));
		}

		bool SameContent(const RuntimeMetricsLandingProof& candidate,
			const RuntimeMetricsLandingProof& presented) noexcept
		{
			return candidate.canvas == presented.canvas && candidate.kind == presented.kind &&
				candidate.contentToken == presented.contentToken &&
				candidate.itemToken == presented.itemToken &&
				candidate.consumedSequence == presented.consumedSequence;
		}

		const char* DeviceName(InputDeviceType type) noexcept
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

		const char* ToolName(uint32_t tool) noexcept
		{
			// 产品枚举含硬笔及形状，不能沿用旧 demo 的 0/1/2/3 工具表。
			switch (static_cast<DrawingTool>(tool))
			{
			case DrawingTool::Pen: return "Pen";
			case DrawingTool::HardPen: return "HardPen";
			case DrawingTool::Highlighter: return "Highlighter";
			case DrawingTool::Eraser: return "Eraser";
			case DrawingTool::Laser: return "Laser";
			case DrawingTool::SolidLine: return "SolidLine";
			case DrawingTool::DashedLine: return "DashedLine";
			case DrawingTool::OutlineRectangle: return "OutlineRectangle";
			case DrawingTool::FilledRectangle: return "FilledRectangle";
			default: return "Unknown";
			}
		}

		const char* ProofName(RuntimeMetricsProofKind kind) noexcept
		{
			switch (kind)
			{
			case RuntimeMetricsProofKind::Live: return "Live";
			case RuntimeMetricsProofKind::Stored: return "Stored";
			case RuntimeMetricsProofKind::Laser: return "Laser";
			default: return "Unknown";
			}
		}

		const char* ArchitectureName() noexcept
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

		double Percentile(const std::vector<double>& sorted, double fraction)
		{
			if (sorted.empty()) return 0.0;
			const size_t rank = static_cast<size_t>(std::ceil(static_cast<double>(sorted.size()) * fraction));
			return sorted[std::min(sorted.size() - 1, rank == 0 ? size_t{ 0 } : rank - 1)];
		}

		double Median(const std::vector<double>& sorted)
		{
			const size_t middle = sorted.size() / 2;
			return (sorted.size() & 1) != 0 ? sorted[middle] :
				sorted[middle - 1] * 0.5 + sorted[middle] * 0.5;
		}

		std::vector<double> SortedLandingLatencies(const std::vector<LandingSample>& samples)
		{
			std::vector<double> values;
			values.reserve(samples.size());
			for (const LandingSample& sample : samples) values.push_back(sample.latencyMs);
			std::sort(values.begin(), values.end());
			return values;
		}

		void WriteDistribution(std::ostringstream& stream, const std::vector<double>& sorted)
		{
			stream << "\"count\": " << sorted.size() << ", \"medianMs\": ";
			if (sorted.empty()) stream << "null"; else stream << Median(sorted);
			stream << ", \"p95Ms\": ";
			if (sorted.empty()) stream << "null"; else stream << Percentile(sorted, 0.95);
			stream << ", \"p99Ms\": ";
			if (sorted.size() < kRequiredPercentile99Population) stream << "null";
			else stream << Percentile(sorted, 0.99);
			stream << ", \"insufficientPopulation\": " <<
				(sorted.size() < kRequiredPercentile99Population ? "true" : "false");
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

		void WriteOptional(std::ostringstream& stream, bool available, double value)
		{ if (available && ValidDuration(value)) stream << value; else stream << "null"; }
		void WriteCosts(std::ostringstream& stream, const RuntimeMetricsFrameCosts& costs)
		{
			stream << "{\"cpuAvailable\": " << (costs.cpuAvailable ? "true" : "false") << ", \"threadCpuMs\": ";
			WriteOptional(stream, costs.cpuAvailable, costs.threadCpuMs);
			stream << ", \"threadSpanWallMs\": "; WriteOptional(stream, costs.cpuAvailable, costs.threadSpanWallMs);
			stream << ", \"inclusiveSpans\": true, \"stageAvailableMask\": " << costs.stageAvailableMask
				<< ", \"stageCpuAvailableMask\": " << costs.stageCpuAvailableMask
				<< ", \"modelResetCalls\": " << costs.modelResetCalls << ", \"modelUpdateCalls\": " << costs.modelUpdateCalls
				<< ", \"predictionCalls\": " << costs.predictionCalls << ", \"stages\": [";
			constexpr const char* names[] = { "ingress", "modelPrediction", "geometryRasterSubmit", "composite" };
			for (size_t i = 0; i < 4; ++i)
			{
				if (i) stream << ", ";
				stream << "{\"name\": \"" << names[i] << "\", \"wallMs\": ";
				WriteOptional(stream, (costs.stageAvailableMask & (1u << i)) != 0, costs.stageWallMs[i]);
				stream << ", \"threadCpuMs\": ";
				WriteOptional(stream, (costs.stageCpuAvailableMask & (1u << i)) != 0, costs.stageThreadCpuMs[i]);
				stream << "}";
			}
			stream << "]}";
		}
		void WriteTerminal(std::ostringstream& stream, const RuntimeMetricsTerminalFacts& facts)
		{
			stream << "{\"upConsumed\": " << facts.upConsumed << ", \"cpuStoredCompleted\": " << facts.cpuStoredCompleted
				<< ", \"authoritativeFinalPresented\": " << facts.authoritativeFinalPresented
				<< ", \"cancelled\": " << facts.cancelled << ", \"rejected\": " << facts.rejected
				<< ", \"noVisibleProjection\": " << facts.noVisibleProjection << ", \"excludedLaser\": " << facts.excludedLaser
				<< ", \"laserLifecycleCompleted\": " << facts.laserLifecycleCompleted << ", \"holdWaits\": " << facts.holdWaits
				<< ", \"laserPhaseTransitions\": " << facts.laserPhaseTransitions << ", \"activeRuntimes\": " << facts.activeRuntimes
				<< ", \"awaitingReconnect\": " << facts.awaitingReconnect << ", \"pendingFinal\": " << facts.pendingFinal << "}";
		}
		void WritePrefix(std::ostringstream& stream, const RuntimeMetricsSnapshot& m)
		{
			stream << "{\"contactSeen\": " << m.contactSeen << ", \"contactRetained\": " << m.contactRetained
				<< ", \"contactDropped\": " << m.contactDropped << ", \"pending\": " << m.pending << ", \"confirmed\": " << m.confirmed
				<< ", \"unpresented\": " << m.unpresented << ", \"invalid\": " << m.invalid << ", \"pendingOverflow\": " << m.pendingOverflow
				<< ", \"framesSeen\": " << m.framesSeen << ", \"framesRetained\": " << m.framesRetained
				<< ", \"framesDropped\": " << m.framesDropped << ", \"framesInvalid\": " << m.framesInvalid
				<< ", \"presentAttempts\": " << m.presentAttempts << ", \"presentSucceeded\": " << m.presentSucceeded
				<< ", \"presentFailed\": " << m.presentFailed << ", \"presentRetained\": " << m.presentRetained
				<< ", \"presentDropped\": " << m.presentDropped << ", \"presentInvalid\": " << m.presentInvalid
				<< ", \"durationDropped\": " << m.durationDropped << "}";
		}
		const char* BoundaryName(RuntimeMetricsBoundaryAuthority authority) noexcept
		{
			switch (authority)
			{
			case RuntimeMetricsBoundaryAuthority::StartupClearedSurface: return "StartupClearedSurface";
			case RuntimeMetricsBoundaryAuthority::StoredHistoryChain: return "StoredHistoryChain";
			case RuntimeMetricsBoundaryAuthority::LaserLifecycleComplete: return "LaserLifecycleComplete";
			default: return "None";
			}
		}
		const char* OutputName(uint32_t value) noexcept
		{
			switch (value)
			{
			case static_cast<uint32_t>(TransparentOutputTarget::PrimaryDrawpad): return "PrimaryDrawpad";
			case static_cast<uint32_t>(TransparentOutputTarget::SelectionUlw): return "SelectionUlw";
			default: return "Unknown";
			}
		}
		void WriteBoundary(std::ostringstream& stream, const RuntimeMetricsPhaseBoundary& b)
		{
			if (!b.exists) { stream << "null"; return; }
			stream << "{\"authority\": \"" << BoundaryName(b.authority) << "\", \"ownerQpc\": " << b.ownerQpc
				<< ", \"frameSerial\": " << b.frameSerial << ", \"contactSeenOrdinal\": " << b.contactSeenOrdinal
				<< ", \"workspaceOrdinal\": " << b.canvas.workspace << ", \"pageOrdinal\": " << b.canvas.page
				<< ", \"sceneGeneration\": " << b.canvas.sceneGeneration << ", \"rasterGeneration\": " << b.canvas.rasterGeneration
				<< ", \"outputGeneration\": " << b.canvas.outputGeneration << ", \"rawOutputRevision\": " << b.rawOutputRevision
				<< ", \"rawOutputTarget\": \"" << OutputName(b.rawOutputTarget) << "\", \"historyRevision\": " << b.historyRevision
				<< ", \"rasterState\": " << b.rasterState << ", \"viewportX\": " << b.viewportX << ", \"viewportY\": " << b.viewportY
				<< ", \"viewportScale\": " << b.viewportScale << ", \"width\": " << b.width << ", \"height\": " << b.height
				<< ", \"pipelineCompositeComplete\": " << (b.pipelineCompositeComplete ? "true" : "false")
				<< ", \"finalProjectionCovered\": " << (b.finalProjectionCovered ? "true" : "false")
				<< ", \"fullViewportComposite\": " << (b.fullViewportComposite ? "true" : "false") << ", \"prefix\": ";
			WritePrefix(stream, b.metrics);
			stream << ", \"inputPrefix\": {\"downPublished\": " << b.input.downPublished << ", \"downRejected\": " << b.input.downRejected
				<< ", \"movePublished\": " << b.input.movePublished << ", \"moveContended\": " << b.input.moveContended
				<< ", \"terminalPublished\": " << b.input.terminalPublished << ", \"recycled\": " << b.input.recycled
				<< ", \"occupiedSlots\": " << b.input.occupiedSlots << "}, \"terminal\": ";
			WriteTerminal(stream, b.terminal); stream << "}";
		}

		bool WriteUtf8File(const wchar_t* path, const std::string& text)
		{
			if (!path || path[0] == L'\0' || text.size() > MAXDWORD) return false;
			// 离线报告也只能 create-new；失败不能截断用户已有数据。
			HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool succeeded = WriteFile(file, text.data(),
				static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size() &&
				FlushFileBuffers(file);
			CloseHandle(file);
			return succeeded;
		}
	}

	struct RuntimeMetricsSessionImpl
	{
		explicit RuntimeMetricsSessionImpl(size_t requested, size_t externalAuxiliaryBytes);
		size_t FindContact(const LandingKey& key) const noexcept
		{
			size_t slot = HashLandingKey(key) & (contactTable.size() - 1);
			for (size_t probe = 0; probe < contactTable.size(); ++probe)
			{
				const size_t index = contactTable[slot];
				if (index == kNoIndex || contacts[index].key == key) return index;
				slot = (slot + 1) & (contactTable.size() - 1);
			}
			return kNoIndex;
		}
		void IndexContact(size_t index) noexcept
		{
			size_t slot = HashLandingKey(contacts[index].key) & (contactTable.size() - 1);
			while (contactTable[slot] != kNoIndex) slot = (slot + 1) & (contactTable.size() - 1);
			contactTable[slot] = index;
		}
		void RetainDouble(std::vector<double>& values, double value) noexcept
		{
			if (values.size() < maximumSamples) values.push_back(value);
			else ++durationDropped;
		}
		void RecordPresentSample(double wallMs, PresentOutcome outcome) noexcept
		{
			if (!ValidDuration(wallMs)) { ++counters.invalid; ++presentInvalid; return; }
			if (presents.size() < maximumSamples)
				presents.push_back({ counters.frameSerial, wallMs, outcome });
			else ++presentDropped;
		}
		size_t RegisteredWithoutProof() const noexcept
		{
			return static_cast<size_t>(std::count_if(contacts.begin(), contacts.end(),
				[](const MetricContact& contact) { return contact.status == ContactStatus::Registered; }));
		}
		bool RetentionComplete() const noexcept
		{
			return counters.contactDropped == 0 && counters.pendingOverflow == 0 &&
				counters.framesDropped == 0 && durationDropped == 0 && presentDropped == 0;
		}
		bool CoverageComplete() const noexcept
		{
			return RetentionComplete() && counters.invalid == 0 && counters.presentFailed == 0 && counters.unpresented == 0 &&
				counters.pending == 0 && counters.legacyUnverified == 0 &&
				presentUnknown == 0 && RegisteredWithoutProof() == 0;
		}
		double LongFrameRatio() const noexcept
		{
			return intervalSeen == 0 ? 0.0 : static_cast<double>(longFrames) /
				static_cast<double>(intervalSeen);
		}

		size_t maximumSamples = 0;
		size_t externalAuxiliaryBytes = 0;
		int64_t qpcFrequency = 0;
		RuntimeMetricsSnapshot counters;
		std::vector<MetricContact> contacts;
		std::vector<size_t> contactTable;
		std::array<PendingLanding, kPendingCapacity> pending;
		std::vector<LandingSample> landings;
		std::vector<RuntimeMetricsFrameSample> frames;
		std::vector<PresentSample> presents;
		std::vector<double> frameIntervalsMs;
		std::vector<double> activeFrameWallMs;
		std::vector<double> activePresentWallMs;
		double lastActiveFrameStartMs = 0.0;
		uint64_t activeFrames = 0;
		uint64_t intervalSeen = 0;
		uint64_t longFrames = 0;
		uint64_t durationDropped = 0;
		uint64_t presentDropped = 0;
		uint64_t presentInvalid = 0;
		uint64_t presentUnknown = 0;
		bool currentPresentSucceeded = false;
		bool frameSerialExhausted = false;
		bool idleActive = false;
		double idleStartMs = 0.0;
		uint64_t idleStartLoops = 0;
		uint64_t idleStartFrames = 0;
		uint64_t idleStartPresents = 0;
		double longestIdleMs = 0.0;
		uint64_t maximumIdleLoopGrowth = 0;
		uint64_t maximumIdleFrameGrowth = 0;
		uint64_t maximumIdlePresentGrowth = 0;
	};

	static_assert(kRuntimeMetricByteBudget > sizeof(RuntimeMetricsSessionImpl) + kAllocationSlack);
	static_assert(sizeof(RuntimeMetricsFrameSample) <= 384);
	static_assert(sizeof(RuntimeMetricsPhaseProgress) <= 2048);
	static_assert(std::is_trivially_copyable_v<RuntimeMetricsPhaseProgress>);

	size_t RuntimeMetricsSession::MaximumSamplesForBudget(size_t externalAuxiliaryBytes) noexcept
	{
		constexpr size_t fixedBytes = sizeof(RuntimeMetricsSessionImpl) + kAllocationSlack;
		constexpr size_t bytesPerSample = sizeof(MetricContact) + sizeof(LandingSample) +
			sizeof(RuntimeMetricsFrameSample) + sizeof(PresentSample) + 3 * sizeof(double) +
			4 * sizeof(size_t); // 二次幂表至多占四槽/项，负载始终 <= 1/2。
		if (externalAuxiliaryBytes > kRuntimeMetricByteBudget - fixedBytes) return 0;
		return std::min(kMaximumRuntimeMetricSamples,
			(kRuntimeMetricByteBudget - fixedBytes - externalAuxiliaryBytes) / bytesPerSample);
	}

	RuntimeMetricsSessionImpl::RuntimeMetricsSessionImpl(size_t requested, size_t auxiliaryBytes)
		: externalAuxiliaryBytes(auxiliaryBytes)
	{
		// Session 对象和全部 reserve 之前已用实际 sizeof 扣除共同外部载荷。
		const size_t budgetCapacity = RuntimeMetricsSession::MaximumSamplesForBudget(auxiliaryBytes);
		if (budgetCapacity == 0) throw std::length_error("Draw3 metrics external storage exceeds budget");
		maximumSamples = std::clamp(requested, size_t{ 1 }, budgetCapacity);
		size_t tableSlots = 1;
		while (tableSlots < maximumSamples * 2) tableSlots *= 2;
		contacts.reserve(maximumSamples);
		contactTable.assign(tableSlots, kNoIndex);
		landings.reserve(maximumSamples);
		frames.reserve(maximumSamples);
		presents.reserve(maximumSamples);
		frameIntervalsMs.reserve(maximumSamples);
		activeFrameWallMs.reserve(maximumSamples);
		activePresentWallMs.reserve(maximumSamples);
		counters.requestedSamples = requested;
		counters.effectiveSamples = maximumSamples;
		counters.allocatedBytes = sizeof(RuntimeMetricsSessionImpl) +
			contacts.capacity() * sizeof(MetricContact) + contactTable.capacity() * sizeof(size_t) +
			landings.capacity() * sizeof(LandingSample) +
			frames.capacity() * sizeof(RuntimeMetricsFrameSample) +
			presents.capacity() * sizeof(PresentSample) +
			(frameIntervalsMs.capacity() + activeFrameWallMs.capacity() +
				activePresentWallMs.capacity()) * sizeof(double);
		if (counters.allocatedBytes > kRuntimeMetricByteBudget - externalAuxiliaryBytes)
			throw std::length_error("Draw3 metrics storage exceeds fixed byte budget");
		LARGE_INTEGER frequency = {};
		if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
			qpcFrequency = frequency.QuadPart;
	}

	RuntimeMetricsSession::RuntimeMetricsSession(size_t maximumSamples, size_t externalAuxiliaryBytes)
		: impl_([&]
			{
				if (MaximumSamplesForBudget(externalAuxiliaryBytes) == 0)
					throw std::length_error("Draw3 metrics storage has no budget before allocation");
				return std::make_unique<RuntimeMetricsSessionImpl>(maximumSamples, externalAuxiliaryBytes);
			}())
	{
	}
	RuntimeMetricsSession::~RuntimeMetricsSession() = default;

	void RuntimeMetricsSession::BeginFrame() noexcept
	{
		if (impl_->frameSerialExhausted) return;
		if (impl_->counters.frameSerial == (std::numeric_limits<uint64_t>::max)())
		{
			// 序号耗尽后停在无效0，不能绕回复用旧帧证明。
			InvalidatePending();
			++impl_->counters.invalid;
			impl_->counters.frameSerial = 0;
			impl_->frameSerialExhausted = true;
			return;
		}
		++impl_->counters.frameSerial;
		impl_->currentPresentSucceeded = false;
		// Stored pending 不引用已回收 runtime；失败后等待当前帧同内容的权威 proof。
	}

	bool RuntimeMetricsSession::RegisterContact(ContactRecord* record, uint64_t generation,
		InputDeviceType deviceType, uint32_t tool, int64_t eligibleQpc) noexcept
	{
		const LandingKey key{ record, generation };
		const size_t existing = impl_->FindContact(key);
		if (existing != kNoIndex)
		{
			const MetricContact& contact = impl_->contacts[existing];
			if (contact.deviceType == deviceType && contact.tool == tool &&
				contact.downQpc == eligibleQpc) return contact.status != ContactStatus::Invalid;
			++impl_->counters.invalid; // 同一个 Down 的源时间和锁定工具不能被迟到快照改写。
			return false;
		}
		++impl_->counters.contactSeen;
		if (!record || generation == 0)
		{
			++impl_->counters.invalid;
			++impl_->counters.unpresented;
			return false;
		}
		if (impl_->contacts.size() == impl_->maximumSamples)
		{
			++impl_->counters.contactDropped;
			return false;
		}
		const bool valid = eligibleQpc > 0 && impl_->qpcFrequency > 0;
		const size_t index = impl_->contacts.size();
		impl_->contacts.push_back({ key, deviceType, tool, eligibleQpc,
			impl_->counters.contactSeen, valid ? ContactStatus::Registered : ContactStatus::Invalid });
		impl_->IndexContact(index);
		++impl_->counters.contactRetained;
		if (!valid) { ++impl_->counters.invalid; ++impl_->counters.unpresented; }
		return valid;
	}

	void RuntimeMetricsSession::StageLanding(ContactRecord* record, uint64_t generation,
		InputDeviceType deviceType, uint32_t tool, int64_t eligibleQpc)
	{
		if (!RegisterContact(record, generation, deviceType, tool, eligibleQpc)) return;
		MetricContact& contact = impl_->contacts[impl_->FindContact({ record, generation })];
		if (contact.status != ContactStatus::Registered) return;
		// 过渡接口没有画布/栅格证明，不能把它写入正式 landing 分位数。
		contact.status = ContactStatus::Legacy;
		++impl_->counters.legacyUnverified;
		++impl_->counters.unpresented;
	}

	void RuntimeMetricsSession::CommitStagedLandings(bool, int64_t)
	{
		// 旧接口只保留调用兼容；没有 proof 的成功不能升级为正式样本。
	}

	bool RuntimeMetricsSession::StageVerifiedLanding(ContactRecord* record, uint64_t generation,
		const RuntimeMetricsLandingProof& proof) noexcept
	{
		const size_t index = impl_->FindContact({ record, generation });
		if (index == kNoIndex) return false;
		MetricContact& contact = impl_->contacts[index];
		if (contact.status != ContactStatus::Registered && contact.status != ContactStatus::Pending)
			return false;
		if (!ValidProof(proof) || proof.frameSerial != impl_->counters.frameSerial)
		{
			++impl_->counters.invalid;
			return false;
		}
		for (PendingLanding& pending : impl_->pending)
		{
			if (pending.contactIndex == index)
			{
				pending.proof = proof; // 唯一 owner 可锁存同 contact 的实际新栅格版本。
				return true;
			}
		}
		for (PendingLanding& pending : impl_->pending)
		{
			if (pending.contactIndex != kNoIndex) continue;
			pending = { index, proof };
			contact.status = ContactStatus::Pending;
			++impl_->counters.pending;
			return true;
		}
		contact.status = ContactStatus::Unpresented;
		++impl_->counters.pendingOverflow;
		++impl_->counters.unpresented;
		return false;
	}

	void RuntimeMetricsSession::CommitVerifiedLandings(bool presentSucceeded,
		int64_t presentReturnQpc, const RuntimeMetricsLandingProof& presentedProof) noexcept
	{
		if (!presentSucceeded) return;
		if (!impl_->currentPresentSucceeded || !ValidProof(presentedProof) ||
			presentedProof.frameSerial != impl_->counters.frameSerial) return;
		if (presentReturnQpc <= 0 || impl_->qpcFrequency <= 0)
		{
			++impl_->counters.invalid;
			return;
		}
		for (PendingLanding& pending : impl_->pending)
		{
			if (pending.contactIndex == kNoIndex || !SameContent(pending.proof, presentedProof)) continue;
			// 活动层和 Laser 要本帧重新 stage；Stored 才可跨失败帧等权威内容回执。
			if (pending.proof.kind != RuntimeMetricsProofKind::Stored &&
				pending.proof.frameSerial != impl_->counters.frameSerial) continue;
			MetricContact& contact = impl_->contacts[pending.contactIndex];
			const double latencyMs = (static_cast<double>(presentReturnQpc) -
				static_cast<double>(contact.downQpc)) * 1000.0 /
				static_cast<double>(impl_->qpcFrequency);
			if (presentReturnQpc < contact.downQpc || !ValidDuration(latencyMs))
			{
				contact.status = ContactStatus::Invalid;
				++impl_->counters.invalid;
				++impl_->counters.unpresented;
			}
			else
			{
				impl_->landings.push_back({ contact.deviceType, contact.tool, contact.ordinal,
					contact.key.generation, contact.downQpc, presentReturnQpc, latencyMs, presentedProof });
				contact.status = ContactStatus::Confirmed;
				++impl_->counters.confirmed;
			}
			pending.contactIndex = kNoIndex;
			--impl_->counters.pending;
		}
	}

	bool RuntimeMetricsSession::InvalidateContact(ContactRecord* opaqueRecord, uint64_t generation) noexcept
	{
		const size_t index = impl_->FindContact({ opaqueRecord, generation });
		if (index == kNoIndex) return false;
		MetricContact& contact = impl_->contacts[index];
		if (contact.status != ContactStatus::Registered && contact.status != ContactStatus::Pending)
			return false;
		// 只结束这个值键；保留索引去重，也不能撤销同帧其他 contact 的成功资格。
		if (contact.status == ContactStatus::Pending)
		{
			for (PendingLanding& pending : impl_->pending)
			{
				if (pending.contactIndex != index) continue;
				pending.contactIndex = kNoIndex;
				--impl_->counters.pending;
				break;
			}
		}
		contact.status = ContactStatus::Unpresented;
		++impl_->counters.unpresented;
		return true;
	}

	void RuntimeMetricsSession::InvalidatePending() noexcept
	{
		impl_->currentPresentSucceeded = false;
		for (MetricContact& contact : impl_->contacts)
		{
			if (contact.status != ContactStatus::Registered && contact.status != ContactStatus::Pending)
				continue;
			contact.status = ContactStatus::Unpresented;
			++impl_->counters.unpresented;
		}
		for (PendingLanding& pending : impl_->pending) pending.contactIndex = kNoIndex;
		impl_->counters.pending = 0;
	}

	void RuntimeMetricsSession::RecordPresent(double presentMs) noexcept
	{
		++impl_->counters.presentAttempts;
		++impl_->presentUnknown;
		impl_->currentPresentSucceeded = false;
		impl_->RecordPresentSample(presentMs, PresentOutcome::Unknown);
	}

	void RuntimeMetricsSession::RecordVerifiedPresent(double presentMs, bool succeeded) noexcept
	{
		++impl_->counters.presentAttempts;
		if (succeeded) ++impl_->counters.presentSucceeded;
		else ++impl_->counters.presentFailed;
		impl_->currentPresentSucceeded = succeeded;
		impl_->RecordPresentSample(presentMs,
			succeeded ? PresentOutcome::Succeeded : PresentOutcome::Failed);
	}

	void RuntimeMetricsSession::RecordRenderFrame(const RuntimeMetricsFrameSample& sample) noexcept
	{
		++impl_->counters.framesSeen;
		if (sample.frameSerial == 0 || sample.frameSerial != impl_->counters.frameSerial ||
			!ValidDuration(sample.frameStartMs) || !ValidDuration(sample.wallMs) ||
			!ValidDuration(sample.presentWallMs) || (sample.presentSucceeded && !sample.presentAttempted))
		{
			++impl_->counters.framesInvalid;
			++impl_->counters.invalid;
			return;
		}
		bool validCosts = !sample.costs.cpuAvailable || (ValidDuration(sample.costs.threadCpuMs) && ValidDuration(sample.costs.threadSpanWallMs));
		validCosts &= (sample.priorIngressAvailableMask & 1u) == 0 || ValidDuration(sample.priorIngressWallMs);
		validCosts &= (sample.priorIngressAvailableMask & 2u) == 0 || ValidDuration(sample.priorIngressThreadCpuMs);
		for (size_t i = 0; i < 4; ++i)
		{
			validCosts &= (sample.costs.stageAvailableMask & (1u << i)) == 0 || ValidDuration(sample.costs.stageWallMs[i]);
			validCosts &= (sample.costs.stageCpuAvailableMask & (1u << i)) == 0 || ValidDuration(sample.costs.stageThreadCpuMs[i]);
			validCosts &= (sample.laser.stageAvailableMask & (1u << i)) == 0 || ValidDuration(sample.laser.stageWallMs[i]);
			validCosts &= (sample.laser.stageCpuAvailableMask & (1u << i)) == 0 || ValidDuration(sample.laser.stageThreadCpuMs[i]);
		}
		if (!validCosts) { ++impl_->counters.framesInvalid; ++impl_->counters.invalid; return; }
		if (impl_->frames.size() == impl_->maximumSamples) { ++impl_->counters.framesDropped; return; }
		impl_->frames.push_back(sample);
		++impl_->counters.framesRetained;
	}

	RuntimeMetricsSnapshot RuntimeMetricsSession::Snapshot() const noexcept
	{
		auto snapshot = impl_->counters;
		snapshot.presentRetained = impl_->presents.size();
		snapshot.presentDropped = impl_->presentDropped;
		snapshot.presentInvalid = impl_->presentInvalid;
		snapshot.durationDropped = impl_->durationDropped;
		return snapshot;
	}

	void RuntimeMetricsSession::RecordActiveFrame(double frameStartMs, double workMs,
		double presentMs, bool presented)
	{
		++impl_->activeFrames;
		if (!ValidDuration(frameStartMs) || !ValidDuration(workMs) ||
			(presented && !ValidDuration(presentMs)))
		{
			++impl_->counters.invalid;
			impl_->lastActiveFrameStartMs = 0.0;
			return;
		}
		if (impl_->lastActiveFrameStartMs > 0.0)
		{
			const double interval = frameStartMs - impl_->lastActiveFrameStartMs;
			if (ValidDuration(interval))
			{
				++impl_->intervalSeen;
				if (interval > kLongFrameLimitMs) ++impl_->longFrames;
				impl_->RetainDouble(impl_->frameIntervalsMs, interval);
			}
			else ++impl_->counters.invalid;
		}
		impl_->lastActiveFrameStartMs = frameStartMs;
		impl_->RetainDouble(impl_->activeFrameWallMs, workMs);
		if (presented) impl_->RetainDouble(impl_->activePresentWallMs, presentMs);
	}

	void RuntimeMetricsSession::EndActiveFrameSequence() noexcept
	{
		impl_->lastActiveFrameStartMs = 0.0;
	}

	void RuntimeMetricsSession::BeginIdle(double nowMs) noexcept
	{
		if (impl_->idleActive) return;
		if (!ValidDuration(nowMs)) { ++impl_->counters.invalid; return; }
		impl_->idleActive = true;
		impl_->idleStartMs = nowMs;
		impl_->idleStartLoops = impl_->counters.frameSerial;
		impl_->idleStartFrames = impl_->counters.framesSeen;
		impl_->idleStartPresents = impl_->counters.presentAttempts;
		impl_->lastActiveFrameStartMs = 0.0;
	}

	void RuntimeMetricsSession::EndIdle(double nowMs) noexcept
	{
		if (!impl_->idleActive) return;
		const double elapsed = nowMs - impl_->idleStartMs;
		if (!ValidDuration(nowMs) || !ValidDuration(elapsed)) ++impl_->counters.invalid;
		else impl_->longestIdleMs = std::max(impl_->longestIdleMs, elapsed);
		impl_->maximumIdleLoopGrowth = std::max(impl_->maximumIdleLoopGrowth,
			impl_->counters.frameSerial - impl_->idleStartLoops);
		impl_->maximumIdleFrameGrowth = std::max(impl_->maximumIdleFrameGrowth,
			impl_->counters.framesSeen - impl_->idleStartFrames);
		impl_->maximumIdlePresentGrowth = std::max(impl_->maximumIdlePresentGrowth,
			impl_->counters.presentAttempts - impl_->idleStartPresents);
		impl_->idleActive = false;
	}

	bool RuntimeMetricsSession::MeetsStrictThresholds() const
	{
		// 保留旧混合工具门槛作 legacy 数学检查；报告不以此给首发性能结论。
		const std::vector<double> landings = SortedLandingLatencies(impl_->landings);
		std::vector<double> intervals = impl_->frameIntervalsMs;
		std::sort(intervals.begin(), intervals.end());
		return impl_->CoverageComplete() && impl_->counters.presentFailed == 0 &&
			landings.size() >= kRequiredLandingCount &&
			Percentile(landings, 0.99) <= kLandingP99LimitMs && !intervals.empty() &&
			Percentile(intervals, 0.99) <= kFrameIntervalP99LimitMs &&
			impl_->LongFrameRatio() < kLongFrameRatioLimit &&
			impl_->longestIdleMs >= kRequiredIdleDurationMs &&
			impl_->maximumIdleLoopGrowth == 0 && impl_->maximumIdleFrameGrowth == 0 &&
			impl_->maximumIdlePresentGrowth == 0;
	}

	bool RuntimeMetricsSession::WriteJson(const wchar_t* outputPath,
		const ContactInputDiagnosticsSnapshot& inputDiagnostics) const
	{ return WriteJsonImpl(outputPath, inputDiagnostics, nullptr); }

	bool RuntimeMetricsSession::WriteJson(const wchar_t* outputPath,
		const ContactInputDiagnosticsSnapshot& inputDiagnostics, const RuntimeMetricsPhaseBoundaries& phases) const
	{ return WriteJsonImpl(outputPath, inputDiagnostics, &phases); }

	bool RuntimeMetricsSession::WriteJsonImpl(const wchar_t* outputPath,
		const ContactInputDiagnosticsSnapshot& inputDiagnostics, const RuntimeMetricsPhaseBoundaries* phases) const
	{
		// 拒绝损坏的存在值；缺界则保留 raw 和 incomplete/null，不补伪边界。
		const auto validBoundary = [&](const RuntimeMetricsPhaseBoundary& b)
		{
			return !b.exists || (b.ownerQpc > 0 && b.frameSerial != 0 && b.frameSerial <= impl_->counters.frameSerial &&
				b.metrics.frameSerial == b.frameSerial && b.contactSeenOrdinal == b.metrics.contactSeen &&
				b.contactSeenOrdinal <= impl_->counters.contactSeen && b.width > 0 && b.height > 0 &&
				std::isfinite(b.viewportX) && std::isfinite(b.viewportY) && b.viewportScale == 1.0f &&
				b.pipelineCompositeComplete && b.finalProjectionCovered && std::string(OutputName(b.rawOutputTarget)) != "Unknown");
		};
		if (phases && (!validBoundary(phases->coldEnd) || !validBoundary(phases->warmEnd) || !validBoundary(phases->measuredEnd))) return false;
		const auto validTerminalCut = [](const RuntimeMetricsPhaseBoundary& b, uint64_t ordinal)
		{
			if (!b.exists) return true;
			const bool laser = b.authority == RuntimeMetricsBoundaryAuthority::LaserLifecycleComplete;
			return (laser || b.authority == RuntimeMetricsBoundaryAuthority::StoredHistoryChain) &&
				b.contactSeenOrdinal == ordinal && b.canvas.workspace != 0 && b.canvas.page != 0 && b.canvas.sceneGeneration != 0 &&
				b.canvas.rasterGeneration != 0 && b.canvas.outputGeneration != 0 && b.terminal.upConsumed == ordinal &&
				!b.terminal.activeRuntimes && !b.terminal.awaitingReconnect && !b.terminal.pendingFinal &&
				(laser ? b.terminal.laserLifecycleCompleted == ordinal && b.terminal.excludedLaser == ordinal :
					b.terminal.cpuStoredCompleted == ordinal && b.terminal.authoritativeFinalPresented == ordinal && b.metrics.confirmed == ordinal);
		};
		if (phases && (!validTerminalCut(phases->warmEnd, 16) || !validTerminalCut(phases->measuredEnd, 216) ||
			(phases->coldEnd.exists && (phases->coldEnd.authority != RuntimeMetricsBoundaryAuthority::StartupClearedSurface ||
				phases->coldEnd.contactSeenOrdinal != 0 || !phases->coldEnd.fullViewportComposite)))) return false;
		const bool cold = phases && phases->enabled && phases->coldEnd.exists &&
			phases->coldEnd.authority == RuntimeMetricsBoundaryAuthority::StartupClearedSurface &&
			phases->coldEnd.contactSeenOrdinal == 0 && phases->coldEnd.fullViewportComposite;
		const bool warm = cold && phases->warmEnd.exists && phases->warmEnd.contactSeenOrdinal == 16 &&
			phases->warmEnd.frameSerial > phases->coldEnd.frameSerial && phases->warmEnd.ownerQpc >= phases->coldEnd.ownerQpc;
		const bool measured = warm && phases->measuredEnd.exists && phases->measuredEnd.contactSeenOrdinal == 216 &&
			phases->measuredEnd.frameSerial > phases->warmEnd.frameSerial && phases->measuredEnd.ownerQpc >= phases->warmEnd.ownerQpc;
		const bool phaseComplete = measured && !phases->incomplete && phases->failureFlags == 0 &&
			phases->runPrewarm.exists && phases->runPrewarm.completed;
		const auto frameGroup = [&](uint64_t serial) -> size_t
		{
			if (!cold) return 4;
			if (serial <= phases->coldEnd.frameSerial) return 0;
			if (!warm || serial <= phases->warmEnd.frameSerial) return 1;
			if (!measured || serial <= phases->measuredEnd.frameSerial) return 2;
			return 3;
		};
		const auto contactGroup = [&](uint64_t ordinal) -> const char*
		{
			if (!phases || !phases->enabled) return "allRun";
			if (ordinal <= 16) return warm ? "warmup" : "warmTruncated";
			if (ordinal <= 216 && warm) return measured ? "measured" : "measuredTruncated";
			return "unclassified";
		};
		// caller 必须已停止唯一 owner；排序、格式化与文件 I/O 只在离线封口执行。
		const std::vector<double> latencies = SortedLandingLatencies(impl_->landings);
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream << std::fixed << std::setprecision(4);
		stream << "{\n  \"schemaVersion\": 2,\n";
		stream << "  \"environment\": {\"architecture\": \"" << ArchitectureName()
			<< "\", \"pointerBytes\": " << sizeof(void*) << ", \"qpcFrequency\": " << impl_->qpcFrequency << "},\n";
		stream << "  \"measurement\": {\"origin\": \"software Down-to-PresentReturn\", "
			"\"proofAuthority\": \"owner supplied canvas/content/frame values\", "
			"\"threadCpuMs\": null, \"gpuMs\": null, \"releaseVerdictAvailable\": false},\n";
		stream << "  \"capacity\": {\"requestedSamples\": " << impl_->counters.requestedSamples
			<< ", \"effectiveSamples\": " << impl_->maximumSamples << ", \"allocatedBytes\": "
			<< impl_->counters.allocatedBytes << ", \"byteBudget\": " << kRuntimeMetricByteBudget
			<< ", \"pendingCapacity\": " << kPendingCapacity
			<< ", \"externalAuxiliaryBytes\": " << impl_->externalAuxiliaryBytes
			<< ", \"layout\": {\"implBytes\": " << sizeof(RuntimeMetricsSessionImpl)
			<< ", \"contactBytes\": " << sizeof(MetricContact) << ", \"contactCapacity\": " << impl_->contacts.capacity()
			<< ", \"hashSlotBytes\": " << sizeof(size_t) << ", \"hashCapacity\": " << impl_->contactTable.capacity()
			<< ", \"landingBytes\": " << sizeof(LandingSample) << ", \"landingCapacity\": " << impl_->landings.capacity()
			<< ", \"frameBytes\": " << sizeof(RuntimeMetricsFrameSample) << ", \"frameCapacity\": " << impl_->frames.capacity()
			<< ", \"presentBytes\": " << sizeof(PresentSample) << ", \"presentCapacity\": " << impl_->presents.capacity()
			<< ", \"durationBytes\": " << sizeof(double) << ", \"durationCapacity\": "
			<< impl_->frameIntervalsMs.capacity() + impl_->activeFrameWallMs.capacity() + impl_->activePresentWallMs.capacity()
			<< ", \"pendingBytes\": " << sizeof(PendingLanding) << ", \"pendingCapacity\": " << kPendingCapacity
			<< ", \"phaseProgressBytes\": " << sizeof(RuntimeMetricsPhaseProgress)
			<< ", \"inputEventCapacity\": 0}},\n";
		stream << "  \"coverage\": {\"contactSeen\": " << impl_->counters.contactSeen
			<< ", \"contactRetained\": " << impl_->counters.contactRetained
			<< ", \"contactDropped\": " << impl_->counters.contactDropped
			<< ", \"confirmed\": " << impl_->counters.confirmed << ", \"pending\": " << impl_->counters.pending
			<< ", \"unpresented\": " << impl_->counters.unpresented
			<< ", \"registeredWithoutProof\": " << impl_->RegisteredWithoutProof()
			<< ", \"pendingOverflow\": " << impl_->counters.pendingOverflow
			<< ", \"invalid\": " << impl_->counters.invalid
			<< ", \"legacyUnverified\": " << impl_->counters.legacyUnverified
			<< ", \"framesSeen\": " << impl_->counters.framesSeen
			<< ", \"framesRetained\": " << impl_->counters.framesRetained
			<< ", \"framesDropped\": " << impl_->counters.framesDropped
			<< ", \"framesInvalid\": " << impl_->counters.framesInvalid
			<< ", \"presentRetained\": " << impl_->presents.size()
			<< ", \"presentDropped\": " << impl_->presentDropped << ", \"presentInvalid\": " << impl_->presentInvalid
			<< ", \"durationDropped\": " << impl_->durationDropped
			<< ", \"retentionComplete\": " << (impl_->RetentionComplete() ? "true" : "false")
			<< ", \"complete\": " << (impl_->CoverageComplete() ? "true" : "false") << "},\n";
		stream << "  \"summary\": {\"legacyThresholdMet\": " << (MeetsStrictThresholds() ? "true" : "false")
			<< ", \"landingCount\": " << latencies.size() << ", \"landingMedianMs\": ";
		if (latencies.empty()) stream << "null"; else stream << Median(latencies);
		stream << ", \"landingP95Ms\": ";
		if (latencies.empty()) stream << "null"; else stream << Percentile(latencies, 0.95);
		stream << ", \"landingP99Ms\": ";
		if (latencies.size() < kRequiredPercentile99Population) stream << "null"; else stream << Percentile(latencies, 0.99);
		stream << ", \"presentAttempts\": " << impl_->counters.presentAttempts
			<< ", \"presentSucceeded\": " << impl_->counters.presentSucceeded
			<< ", \"presentFailed\": " << impl_->counters.presentFailed
			<< ", \"presentUnknown\": " << impl_->presentUnknown
			<< ", \"loopCount\": " << impl_->counters.frameSerial << ", \"activeFrameCount\": " << impl_->activeFrames
			<< ", \"activeIntervalSeen\": " << impl_->intervalSeen << ", \"longFrameCount\": " << impl_->longFrames
			<< ", \"longFrameRatio\": " << impl_->LongFrameRatio() << ", \"longestIdleMs\": " << impl_->longestIdleMs
			<< ", \"maximumIdleLoopGrowth\": " << impl_->maximumIdleLoopGrowth
			<< ", \"maximumIdleFrameGrowth\": " << impl_->maximumIdleFrameGrowth
			<< ", \"maximumIdlePresentGrowth\": " << impl_->maximumIdlePresentGrowth << "},\n";
		stream << "  \"input\": {\"slotCapacity\": " << inputDiagnostics.slotCapacity
			<< ", \"occupiedSlots\": " << inputDiagnostics.occupiedSlots
			<< ", \"downPublished\": " << inputDiagnostics.downPublished
			<< ", \"downRejected\": " << inputDiagnostics.downRejected
			<< ", \"movePublished\": " << inputDiagnostics.movePublished
			<< ", \"moveContended\": " << inputDiagnostics.moveContended
			<< ", \"terminalPublished\": " << inputDiagnostics.terminalPublished
			<< ", \"recycled\": " << inputDiagnostics.recycled
			<< ", \"controlWakes\": " << inputDiagnostics.controlWakes
			<< ", \"activeWaits\": " << inputDiagnostics.activeWaits << "},\n";

		stream << "  \"phaseBoundaries\": ";
		if (!phases) stream << "null";
		else
		{
			stream << "{\"enabled\": " << (phases->enabled ? "true" : "false") << ", \"controllerPayloadBytes\": " << phases->controllerPayloadBytes
				<< ", \"incomplete\": " << (phaseComplete ? "false" : "true")
				<< ", \"failureFlags\": " << phases->failureFlags << ", \"coldEnd\": "; WriteBoundary(stream, phases->coldEnd);
			stream << ", \"warmEnd\": "; WriteBoundary(stream, phases->warmEnd);
			stream << ", \"measuredEnd\": "; WriteBoundary(stream, phases->measuredEnd);
			stream << ", \"runTerminal\": "; WriteTerminal(stream, phases->runTerminal);
			stream << ", \"runInputBaseline\": {\"downPublished\": " << phases->runInputBaseline.downPublished
				<< ", \"downRejected\": " << phases->runInputBaseline.downRejected << ", \"movePublished\": " << phases->runInputBaseline.movePublished
				<< ", \"terminalPublished\": " << phases->runInputBaseline.terminalPublished << "}, \"runPrewarm\": ";
			const auto& p = phases->runPrewarm;
			if (!p.exists) stream << "null";
			else
			{
				stream << "{\"completed\": " << (p.completed ? "true" : "false") << ", \"ownerStartQpc\": " << p.ownerStartQpc
					<< ", \"ownerEndQpc\": " << p.ownerEndQpc << ", \"wallMs\": "; WriteOptional(stream, p.wallAvailable, p.wallMs);
				stream << ", \"costs\": "; WriteCosts(stream, p.costs);
				stream << ", \"shaderSpans\": [";
				for (size_t i = 0; i < 2; ++i)
				{
					if (i) stream << ", ";
					stream << "{\"name\": \"" << (i == 0 ? "laser" : "shape") << "\", \"wallMs\": ";
					WriteOptional(stream, (p.shaderAvailableMask & (1u << i)) != 0, p.shaderWallMs[i]);
					stream << ", \"threadCpuMs\": "; WriteOptional(stream, (p.shaderCpuAvailableMask & (1u << i)) != 0, p.shaderThreadCpuMs[i]);
					stream << ", \"gpuMs\": null}";
				}
				stream << "]}";
			}
			stream << "}";
		}
		stream << ",\n  \"phaseSummaries\": ";
		if (!phases) stream << "null";
		else
		{
			stream << "[";
			for (size_t group = 0; group < 5; ++group)
			{
				if (group) stream << ", ";
				const char* name = group == 0 ? "cold" : group == 1 ? (warm ? "warmup" : "warmTruncated") :
					group == 2 ? (measured ? "measured" : "measuredTruncated") : group == 3 ? "postMeasured" : "unclassified";
				std::vector<double> landingValues, walls, cpus, presents;
				std::array<std::vector<double>, 4> stageWalls, stageCpus, laserWalls, laserCpus;
				uint64_t retainedContacts = 0, noPresent = 0, failed = 0, success = 0, rasterFailed = 0, ge50 = 0;
				uint64_t resetCalls = 0, updateCalls = 0, predictionCalls = 0, laserExcluded = 0;
				uint64_t laserRequests = 0, bakeCalls = 0, bakeFailures = 0;
				const auto belongs = [&](uint64_t ordinal)
				{ return (group == 1 && ordinal <= 16) || (group == 2 && warm && ordinal > 16 && ordinal <= 216) ||
					(group == 4 && ((ordinal > 16 && !warm) || ordinal > 216)); };
				for (const auto& contact : impl_->contacts) if (belongs(contact.ordinal))
				{ ++retainedContacts; if (contact.tool == static_cast<uint32_t>(DrawingTool::Laser)) ++laserExcluded; }
				for (const auto& landing : impl_->landings) if (belongs(landing.ordinal)) landingValues.push_back(landing.latencyMs);
				for (const auto& frame : impl_->frames) if (frameGroup(frame.frameSerial) == group)
				{
					walls.push_back(frame.wallMs); if (frame.costs.cpuAvailable) cpus.push_back(frame.costs.threadCpuMs);
					noPresent += !frame.presentAttempted; failed += frame.presentAttempted && !frame.presentSucceeded;
					success += frame.presentSucceeded; ge50 += frame.wallMs >= 50.0;
					rasterFailed += (frame.reasonFlags & static_cast<uint32_t>(RuntimeMetricsFrameReason::RasterFailed)) != 0;
					resetCalls += frame.costs.modelResetCalls; updateCalls += frame.costs.modelUpdateCalls; predictionCalls += frame.costs.predictionCalls;
					laserRequests += frame.laser.particleRequestedCount; bakeCalls += frame.laser.bakeCalls; bakeFailures += frame.laser.bakeFailures;
					for (size_t i = 0; i < 4; ++i)
					{
						if (frame.costs.stageAvailableMask & (1u << i)) stageWalls[i].push_back(frame.costs.stageWallMs[i]);
						if (frame.costs.stageCpuAvailableMask & (1u << i)) stageCpus[i].push_back(frame.costs.stageThreadCpuMs[i]);
						if (frame.laser.stageAvailableMask & (1u << i)) laserWalls[i].push_back(frame.laser.stageWallMs[i]);
						if (frame.laser.stageCpuAvailableMask & (1u << i)) laserCpus[i].push_back(frame.laser.stageThreadCpuMs[i]);
					}
				}
				for (const auto& present : impl_->presents) if (frameGroup(present.frameSerial) == group) presents.push_back(present.wallMs);
				const uint64_t seen = group == 1 ? std::min(uint64_t{16}, impl_->counters.contactSeen) : group == 2 && warm ?
					std::min(uint64_t{200}, impl_->counters.contactSeen > 16 ? impl_->counters.contactSeen - 16 : 0) : group == 4 ?
					(!warm ? (impl_->counters.contactSeen > 16 ? impl_->counters.contactSeen - 16 : 0) :
						impl_->counters.contactSeen > 216 ? impl_->counters.contactSeen - 216 : 0) : 0;
				const RuntimeMetricsSnapshot begin = group == 1 && cold ? phases->coldEnd.metrics : group == 2 && warm ? phases->warmEnd.metrics :
					group == 3 && measured ? phases->measuredEnd.metrics : RuntimeMetricsSnapshot{};
				const bool prefixAvailable = group == 0 ? cold : group == 1 ? cold : group == 2 ? warm : group == 3 ? measured : !cold;
				const RuntimeMetricsSnapshot end = group == 0 && cold ? phases->coldEnd.metrics : group == 1 && warm ? phases->warmEnd.metrics :
					group == 2 && measured ? phases->measuredEnd.metrics : Snapshot();
				const auto difference = [](uint64_t a, uint64_t b) { return a >= b ? a - b : uint64_t{0}; };
				const auto prefixDifference = [&](uint64_t a, uint64_t b)
				{ return prefixAvailable ? std::to_string(difference(a, b)) : std::string("null"); };
				stream << "{\"phase\": \"" << name << "\", \"available\": " << ((group == 0 ? cold : group == 1 ? phases->enabled : group == 2 ? warm : group == 3 ? measured : true) ? "true" : "false")
					<< ", \"complete\": " << ((group == 0 && cold) || (group == 1 && warm) || (group == 2 && phaseComplete) ? "true" : "false")
					<< ", \"plannedContacts\": " << (group == 1 ? 16 : group == 2 ? 200 : 0)
					<< ", \"contactSeen\": " << seen << ", \"contactRetained\": " << retainedContacts
					<< ", \"contactDropped\": " << difference(seen, retainedContacts) << ", \"excludedLaser\": " << laserExcluded
					<< ", \"framesRetained\": " << walls.size() << ", \"framesSeenPrefixDelta\": " << prefixDifference(end.framesSeen, begin.framesSeen)
					<< ", \"framesDroppedPrefixDelta\": " << prefixDifference(end.framesDropped, begin.framesDropped)
					<< ", \"framesInvalidPrefixDelta\": " << prefixDifference(end.framesInvalid, begin.framesInvalid)
					<< ", \"presentDroppedPrefixDelta\": " << prefixDifference(end.presentDropped, begin.presentDropped)
					<< ", \"presentInvalidPrefixDelta\": " << prefixDifference(end.presentInvalid, begin.presentInvalid)
					<< ", \"durationDroppedPrefixDelta\": " << prefixDifference(end.durationDropped, begin.durationDropped)
					<< ", \"invalidPrefixDelta\": " << prefixDifference(end.invalid, begin.invalid)
					<< ", \"noPresent\": " << noPresent << ", \"presentSucceeded\": " << success << ", \"presentFailed\": " << failed
					<< ", \"rasterFailed\": " << rasterFailed << ", \"frameWallGe50Ms\": " << ge50
					<< ", \"modelResetCalls\": " << resetCalls << ", \"modelUpdateCalls\": " << updateCalls << ", \"predictionCalls\": " << predictionCalls
					<< ", \"laserParticleRequestedCount\": " << laserRequests << ", \"laserBakeCalls\": " << bakeCalls << ", \"laserBakeFailures\": " << bakeFailures;
				const auto distribution = [&](const char* label, std::vector<double>& values)
				{ std::sort(values.begin(), values.end()); stream << ", \"" << label << "\": {"; WriteDistribution(stream, values); stream << "}"; };
				distribution("downLatency", landingValues); distribution("frameWall", walls); distribution("threadCpu", cpus); distribution("presentWall", presents);
				const auto terminalBegin = group == 1 && cold ? phases->coldEnd.terminal : group == 2 && warm ? phases->warmEnd.terminal :
					group == 3 && measured ? phases->measuredEnd.terminal : RuntimeMetricsTerminalFacts{};
				const auto terminalEnd = group == 0 && cold ? phases->coldEnd.terminal : group == 1 && warm ? phases->warmEnd.terminal :
					group == 2 && measured ? phases->measuredEnd.terminal : phases->runTerminal;
				stream << ", \"terminalPrefixDelta\": {\"upConsumed\": " << prefixDifference(terminalEnd.upConsumed, terminalBegin.upConsumed)
					<< ", \"cpuStoredCompleted\": " << prefixDifference(terminalEnd.cpuStoredCompleted, terminalBegin.cpuStoredCompleted)
					<< ", \"authoritativeFinalPresented\": " << prefixDifference(terminalEnd.authoritativeFinalPresented, terminalBegin.authoritativeFinalPresented)
					<< ", \"laserLifecycleCompleted\": " << prefixDifference(terminalEnd.laserLifecycleCompleted, terminalBegin.laserLifecycleCompleted)
					<< ", \"cancelled\": " << prefixDifference(terminalEnd.cancelled, terminalBegin.cancelled)
					<< ", \"rejected\": " << prefixDifference(terminalEnd.rejected, terminalBegin.rejected)
					<< ", \"noVisibleProjection\": " << prefixDifference(terminalEnd.noVisibleProjection, terminalBegin.noVisibleProjection) << "}";
				stream << ", \"inclusiveCostStages\": [";
				for (size_t i = 0; i < 4; ++i)
				{
					if (i) stream << ", "; stream << "{\"stage\": " << i;
					distribution("wall", stageWalls[i]); distribution("threadCpu", stageCpus[i]); stream << "}";
				}
				stream << "], \"laserCostStages\": [";
				for (size_t i = 0; i < 4; ++i)
				{
					if (i) stream << ", "; stream << "{\"stage\": " << i;
					distribution("wall", laserWalls[i]); distribution("threadCpu", laserCpus[i]); stream << "}";
				}
				stream << "], \"activeFrameIntervals\": null, \"moveLatency\": null, \"upFinalStableLatency\": null}";
			}
			stream << "]";
		}
		stream << ",\n  \"contacts\": [";
		constexpr const char* statuses[] = { "Registered", "Pending", "Confirmed", "Unpresented", "Invalid", "Legacy" };
		for (size_t i = 0; i < impl_->contacts.size(); ++i)
		{
			if (i) stream << ", "; const auto& c = impl_->contacts[i];
			stream << "{\"contactOrdinal\": " << c.ordinal << ", \"generation\": " << c.key.generation << ", \"device\": \"" << DeviceName(c.deviceType)
				<< "\", \"tool\": \"" << ToolName(c.tool) << "\", \"downQpc\": " << c.downQpc << ", \"status\": \"" << statuses[static_cast<size_t>(c.status)]
				<< "\", \"retained\": true, \"phase\": \"" << contactGroup(c.ordinal) << "\"}";
		}
		stream << "],\n";

		// 分群和排序仅离线执行；工具值来自真实产品符号，未知值单列。
		std::map<std::pair<InputDeviceType, uint32_t>, std::vector<double>> populations;
		for (const LandingSample& landing : impl_->landings)
			populations[{ landing.deviceType, landing.tool }].push_back(landing.latencyMs);
		stream << "  \"toolSummaries\": [\n";
		size_t populationIndex = 0;
		for (auto& population : populations)
		{
			std::sort(population.second.begin(), population.second.end());
			stream << "    {\"device\": \"" << DeviceName(population.first.first)
				<< "\", \"tool\": \"" << ToolName(population.first.second) << "\", \"toolValue\": "
				<< population.first.second << ", ";
			WriteDistribution(stream, population.second);
			stream << "}" << (++populationIndex == populations.size() ? "\n" : ",\n");
		}
		stream << "  ],\n  \"frameSummaries\": [\n";
		std::map<uint32_t, std::vector<double>> framePopulations;
		for (const RuntimeMetricsFrameSample& frame : impl_->frames)
			framePopulations[frame.reasonFlags].push_back(frame.wallMs);
		populationIndex = 0;
		for (auto& population : framePopulations)
		{
			std::sort(population.second.begin(), population.second.end());
			stream << "    {\"reasonFlags\": " << population.first << ", ";
			WriteDistribution(stream, population.second);
			stream << "}" << (++populationIndex == framePopulations.size() ? "\n" : ",\n");
		}
		stream << "  ],\n  \"landings\": [\n";
		for (size_t index = 0; index < impl_->landings.size(); ++index)
		{
			const LandingSample& sample = impl_->landings[index];
			stream << "    {\"contactOrdinal\": " << sample.ordinal << ", \"generation\": " << sample.generation
				<< ", \"device\": \"" << DeviceName(sample.deviceType) << "\", \"tool\": \"" << ToolName(sample.tool)
				<< "\", \"downQpc\": " << sample.downQpc << ", \"presentReturnQpc\": " << sample.presentReturnQpc
				<< ", \"latencyMs\": " << sample.latencyMs << ", \"frameSerial\": " << sample.proof.frameSerial
				<< ", \"workspaceOrdinal\": " << sample.proof.canvas.workspace << ", \"pageOrdinal\": " << sample.proof.canvas.page
				<< ", \"sceneGeneration\": " << sample.proof.canvas.sceneGeneration
				<< ", \"rasterGeneration\": " << sample.proof.canvas.rasterGeneration
				<< ", \"outputGeneration\": " << sample.proof.canvas.outputGeneration
				<< ", \"proofKind\": \"" << ProofName(sample.proof.kind) << "\", \"contentToken\": " << sample.proof.contentToken
				<< ", \"itemToken\": " << sample.proof.itemToken << ", \"consumedSequence\": " << sample.proof.consumedSequence << "}";
			stream << (index + 1 == impl_->landings.size() ? "\n" : ",\n");
		}
		stream << "  ],\n  \"frames\": [\n";
		for (size_t index = 0; index < impl_->frames.size(); ++index)
		{
			const RuntimeMetricsFrameSample& frame = impl_->frames[index];
			stream << "    {\"frameSerial\": " << frame.frameSerial << ", \"frameStartMs\": " << frame.frameStartMs
				<< ", \"wallMs\": " << frame.wallMs << ", \"presentWallMs\": " << frame.presentWallMs
				<< ", \"reasonFlags\": " << frame.reasonFlags << ", \"physicalBefore\": " << frame.physicalBefore
				<< ", \"physicalAfter\": " << frame.physicalAfter << ", \"terminalCount\": " << frame.terminalCount
				<< ", \"presentAttempted\": " << (frame.presentAttempted ? "true" : "false")
				<< ", \"presentSucceeded\": " << (frame.presentSucceeded ? "true" : "false") << ", \"presentReturnQpc\": ";
			if (frame.presentReturnQpc > 0) stream << frame.presentReturnQpc; else stream << "null";
			stream << ", \"costs\": "; WriteCosts(stream, frame.costs);
			stream << ", \"priorIngressWallMs\": "; WriteOptional(stream, (frame.priorIngressAvailableMask & 1u) != 0, frame.priorIngressWallMs);
			stream << ", \"priorIngressThreadCpuMs\": "; WriteOptional(stream, (frame.priorIngressAvailableMask & 2u) != 0, frame.priorIngressThreadCpuMs);
			stream << ", \"stagesIncludePriorIngress\": true, \"frameCpuExcludesPriorIngress\": true";
			stream << ", \"laser\": ";
			if (!frame.laser.collected) stream << "null";
			else
			{
				const auto& l = frame.laser;
				const char* phaseName = "Unknown";
				switch (static_cast<LaserTrailPhase>(l.trailPhase))
				{
				case LaserTrailPhase::Inactive: phaseName = "Inactive"; break;
				case LaserTrailPhase::Active: phaseName = "Active"; break;
				case LaserTrailPhase::Hold: phaseName = "Hold"; break;
				case LaserTrailPhase::Fade: phaseName = "Fade"; break;
				}
				const char* coverageName = "Unknown";
				switch (static_cast<LaserCoverageMode>(l.coverageMode))
				{
				case LaserCoverageMode::Inactive: coverageName = "Inactive"; break;
				case LaserCoverageMode::Incremental: coverageName = "Incremental"; break;
				case LaserCoverageMode::FullRedraw: coverageName = "FullRedraw"; break;
				}
				stream << "{\"phase\": \"" << phaseName << "\", \"coverage\": \"" << coverageName << "\", \"activeContactCount\": " << l.activeContactCount
					<< ", \"lastAllUpQpc\": "; if (l.lastAllUpQpc > 0) stream << l.lastAllUpQpc; else stream << "null";
				stream << ", \"holdSeconds\": "; WriteOptional(stream, true, l.effectiveHoldSeconds);
				stream << ", \"fadeSeconds\": "; WriteOptional(stream, true, l.fadeSeconds);
				stream << ", \"opacity\": "; WriteOptional(stream, std::isfinite(l.opacity), l.opacity);
				stream << ", \"layerCount\": " << l.layerCount
					<< ", \"particlesEnabled\": " << (l.particlesEnabled ? "true" : "false")
					<< ", \"particlesAvailable\": " << (l.particlesAvailable ? "true" : "false")
					<< ", \"particlesActive\": " << (l.particlesActive ? "true" : "false")
					<< ", \"particleRequestedCount\": " << l.particleRequestedCount
					<< ", \"requestedOnly\": " << (l.requestedOnly ? "true" : "false")
					<< ", \"emittedAvailable\": false, \"gpuEmittedCount\": null"
					<< ", \"incrementalCalls\": " << l.incrementalCalls << ", \"bakeCalls\": " << l.bakeCalls << ", \"bakeFailures\": " << l.bakeFailures
					<< ", \"particleStepCalls\": " << l.particleStepCalls << ", \"particleDrawCalls\": " << l.particleDrawCalls
					<< ", \"inclusiveStages\": [";
				for (size_t i = 0; i < 4; ++i)
				{
					if (i) stream << ", "; stream << "{\"stage\": " << i << ", \"wallMs\": ";
					WriteOptional(stream, (l.stageAvailableMask & (1u << i)) != 0, l.stageWallMs[i]); stream << ", \"threadCpuMs\": ";
					WriteOptional(stream, (l.stageCpuAvailableMask & (1u << i)) != 0, l.stageThreadCpuMs[i]); stream << "}";
				}
				stream << "]}";
			}
			stream << "}";
			stream << (index + 1 == impl_->frames.size() ? "\n" : ",\n");
		}
		stream << "  ],\n  \"presents\": [\n";
		for (size_t index = 0; index < impl_->presents.size(); ++index)
		{
			const PresentSample& present = impl_->presents[index];
			stream << "    {\"frameSerial\": " << present.frameSerial << ", \"wallMs\": " << present.wallMs << ", \"succeeded\": ";
			if (present.outcome == PresentOutcome::Unknown) stream << "null";
			else stream << (present.outcome == PresentOutcome::Succeeded ? "true" : "false");
			stream << "}" << (index + 1 == impl_->presents.size() ? "\n" : ",\n");
		}
		stream << "  ],\n  \"raw\": {\n";
		WriteDoubleArray(stream, "activeFrameIntervalsMs", impl_->frameIntervalsMs, true);
		WriteDoubleArray(stream, "activeFrameWallMs", impl_->activeFrameWallMs, true);
		WriteDoubleArray(stream, "activePresentWallMs", impl_->activePresentWallMs, false);
		stream << "  }\n}\n";
		return WriteUtf8File(outputPath, stream.str());
	}
}
