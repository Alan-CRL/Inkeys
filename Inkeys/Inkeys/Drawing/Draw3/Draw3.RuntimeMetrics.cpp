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
#include <utility>
#include <vector>
#include <windows.h>

module Inkeys.Drawing.Draw3.runtime_metrics;

import Inkeys.Drawing.Draw3.window_control;

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
		explicit RuntimeMetricsSessionImpl(size_t requested);
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

	RuntimeMetricsSessionImpl::RuntimeMetricsSessionImpl(size_t requested)
	{
		constexpr size_t bytesPerSample = sizeof(MetricContact) + sizeof(LandingSample) +
			sizeof(RuntimeMetricsFrameSample) + sizeof(PresentSample) + 3 * sizeof(double) +
			4 * sizeof(size_t);
		// 哈希表向二次幂上取整但负载始终 <= 1/2；留出固定对象和分配器余量。
		constexpr size_t budgetCapacity =
			(kRuntimeMetricByteBudget - sizeof(RuntimeMetricsSessionImpl) - kAllocationSlack) /
			bytesPerSample;
		static_assert(budgetCapacity > 0, "metrics fixed state must fit the byte budget");
		maximumSamples = std::clamp(requested, size_t{ 1 },
			std::min(kMaximumRuntimeMetricSamples, budgetCapacity));
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
		if (counters.allocatedBytes > kRuntimeMetricByteBudget)
			throw std::length_error("Draw3 metrics storage exceeds fixed byte budget");
		LARGE_INTEGER frequency = {};
		if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
			qpcFrequency = frequency.QuadPart;
	}

	RuntimeMetricsSession::RuntimeMetricsSession(size_t maximumSamples)
		: impl_(std::make_unique<RuntimeMetricsSessionImpl>(maximumSamples))
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
		if (impl_->frames.size() == impl_->maximumSamples) { ++impl_->counters.framesDropped; return; }
		impl_->frames.push_back(sample);
		++impl_->counters.framesRetained;
	}

	RuntimeMetricsSnapshot RuntimeMetricsSession::Snapshot() const noexcept
	{
		return impl_->counters;
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
	{
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
			<< ", \"pendingCapacity\": " << kPendingCapacity << "},\n";
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
				<< ", \"presentSucceeded\": " << (frame.presentSucceeded ? "true" : "false") << "}";
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
