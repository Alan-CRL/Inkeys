module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <windows.h>

module draw3.diagnostics;

namespace draw3
{
	namespace
	{
#if defined(_DEBUG)
		void WriteFastConsoleLine(const char* text, DWORD length)
		{
			static HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE); // 缓存控制台句柄，减少每帧日志开销。
			if (!consoleHandle || consoleHandle == INVALID_HANDLE_VALUE || length == 0) return;
			DWORD written = 0;
			if (!WriteConsoleA(consoleHandle, text, length, &written, nullptr))
			{
				WriteFile(consoleHandle, text, length, &written, nullptr); // 输出被重定向时 WriteConsoleA 会失败，改用 WriteFile。
			}
		}
#endif

#if defined(DRAW3_RTS_DIAGNOSTICS)
		constexpr size_t kRtsTraceContactCapacity = 32;
		constexpr size_t kRtsTraceEventCapacity = 24;
		constexpr size_t kRtsTraceMoveEventLimit = 6;
		constexpr size_t kRtsTraceAuxiliaryEventCapacity = 64;
		constexpr size_t kDrawingFirstFrameContactCapacity = 64;
		constexpr size_t kDrawingFirstFrameCount = 3;
		constexpr size_t kDrawingContactSummaryCapacity = 64;
		constexpr size_t kCompositionCommitTraceCapacity = 16;
#if defined(DRAW3_TESTING)
		constexpr size_t kInputTraceTimelineCapacity = 32;
		constexpr size_t kDrawingFrameTraceCapacity = 8;
		constexpr size_t kDrawingContactFrameTraceCapacity = 16;
		constexpr size_t kPresentTraceCapacity = 8;
		constexpr size_t kDrawingWaitTraceCapacity = 8;
#else
		constexpr size_t kInputTraceTimelineCapacity = 8192;
		// 120 Hz 下保留约两分钟，避免先 Pen 后 Mouse 时覆盖前半段逐帧证据。
		constexpr size_t kDrawingFrameTraceCapacity = 16384;
		constexpr size_t kDrawingContactFrameTraceCapacity = 16384;
		constexpr size_t kPresentTraceCapacity = 16384;
		constexpr size_t kDrawingWaitTraceCapacity = 16384;
#endif
		constexpr size_t kRtsPacketResultCount =
			static_cast<size_t>(RtsPacketResult::Count);
		constexpr size_t kDrawingInputResultCount =
			static_cast<size_t>(DrawingInputResult::Count);

		struct RtsContactTraceSlot
		{
			bool occupied = false;
			bool downObserved = false;
			uint32_t tabletContextId = 0;
			uint32_t contactId = 0;
			uint32_t deviceType = 0;
			uint32_t callbackCount = 0;
			uint32_t packetsCallbackCount = 0;
			uint32_t inAirCallbackCount = 0;
			uint32_t packetSampleCount = 0;
			uint32_t inAirSampleCount = 0;
			uint32_t decodedFailureCount = 0;
			uint32_t publishSuccessCount = 0;
			uint32_t publishFailureCount = 0;
			std::array<uint32_t, kRtsPacketResultCount> resultCounts = {};
			uint32_t droppedEventCount = 0;
			size_t storedMoveEventCount = 0;
			size_t storedEventCount = 0;
			std::array<RtsCallbackTrace, kRtsTraceEventCapacity> events = {};
		};

		enum class InputTraceTimelineKind : uint32_t
		{
			Rts,
			Drawing
		};

		struct InputTraceTimelineEntry
		{
			uint64_t sequence = 0;
			InputTraceTimelineKind kind = InputTraceTimelineKind::Rts;
			RtsCallbackTrace rts = {};
			DrawingInputTrace drawing = {};
		};

		struct RtsResultAggregate
		{
			std::atomic<uint64_t> count = 0;
			std::atomic<uint64_t> firstCallbackSequence = 0;
			std::atomic<uint64_t> lastCallbackSequence = 0;
			std::atomic<uint64_t> firstQpc = 0;
			std::atomic<uint64_t> lastQpc = 0;
		};

		template <typename T>
		struct FixedTraceEntry
		{
			uint64_t sequence = 0;
			T value = {};
		};

		struct DrawingFirstFrameSlot
		{
			bool occupied = false;
			uint32_t tabletContextId = 0;
			uint32_t contactId = 0;
			uint64_t contactGeneration = 0;
			std::array<DrawingContactFrameTrace, kDrawingFirstFrameCount> frames = {};
		};

		struct DrawingContactSummary
		{
			bool occupied = false;
			uint32_t tabletContextId = 0;
			uint32_t contactId = 0;
			uint32_t deviceType = 0;
			uint64_t contactGeneration = 0;
			uint64_t firstFrameSequence = 0;
			uint64_t lastFrameSequence = 0;
			uint64_t firstSnapshotSequence = 0;
			uint64_t lastSnapshotSequence = 0;
			uint32_t frameCount = 0;
			uint32_t modelerUpdateCount = 0;
			uint32_t renderCount = 0;
			uint32_t presentCount = 0;
			uint32_t frameGapOver10MsCount = 0;
			uint32_t frameGapOver12MsCount = 0;
			uint32_t frameGapOver16MsCount = 0;
			int64_t downQpc = 0;
			int64_t firstModelerOutputQpc = 0;
			int64_t firstDrawableGeometryQpc = 0;
			int64_t firstRenderQpc = 0;
			int64_t firstPresentQpc = 0;
			int64_t maximumFrameIntervalMicroseconds = 0;
			int64_t maximumInputAgeMicroseconds = 0;
			int64_t maximumRenderDurationMicroseconds = 0;
			int64_t maximumPresentCallDurationMicroseconds = 0;
		};

		struct DrawingDeviceSummary
		{
			uint64_t contactCount = 0;
			uint64_t activeFrameCount = 0;
			uint64_t presentCount = 0;
			uint64_t geometryFrameCount = 0;
			uint64_t renderFrameCount = 0;
			uint64_t frameGapOver10MsCount = 0;
			uint64_t frameGapOver12MsCount = 0;
			uint64_t frameGapOver16MsCount = 0;
			uint64_t lastFrameSequence = 0;
			uint64_t lastGeometryFrameSequence = 0;
			uint64_t lastRenderFrameSequence = 0;
			uint64_t lastPresentFrameSequence = 0;
			int64_t maximumFrameIntervalMicroseconds = 0;
			int64_t maximumInputAgeMicroseconds = 0;
			int64_t maximumRenderDurationMicroseconds = 0;
			int64_t maximumPresentCallDurationMicroseconds = 0;
		};

		class RtsTraceWriteGuard
		{
		public:
			explicit RtsTraceWriteGuard(std::atomic_flag& lock) noexcept
				: lock_(lock), acquired_(!lock_.test_and_set(std::memory_order_acquire))
			{
			}

			~RtsTraceWriteGuard()
			{
				if (acquired_) lock_.clear(std::memory_order_release);
			}

			explicit operator bool() const noexcept { return acquired_; }

		private:
			std::atomic_flag& lock_;
			bool acquired_ = false;
		};

		std::atomic<bool> rtsTraceEnabled = false;
		std::atomic_flag rtsTraceWriteLock = ATOMIC_FLAG_INIT;
		std::atomic_flag phase2TraceWriteLock = ATOMIC_FLAG_INIT;
		std::array<RtsContactTraceSlot, kRtsTraceContactCapacity> rtsContactSlots = {};
		std::array<RtsContactTraceSlot, kRtsTraceContactCapacity> rtsCompletedContactSlots = {};
		std::array<RtsCallbackTrace, kRtsTraceAuxiliaryEventCapacity> rtsAuxiliaryEvents = {};
		std::array<InputTraceTimelineEntry, kInputTraceTimelineCapacity> inputTraceTimeline = {};
		std::array<RtsResultAggregate, kRtsPacketResultCount> rtsResultAggregates = {};
		std::array<uint64_t, kDrawingInputResultCount> drawingResultCounts = {};
		std::array<FixedTraceEntry<DrawingFrameTrace>, kDrawingFrameTraceCapacity>
			drawingFrameTraceTimeline = {};
		std::array<FixedTraceEntry<DrawingContactFrameTrace>, kDrawingContactFrameTraceCapacity>
			drawingContactFrameTraceTimeline = {};
		std::array<FixedTraceEntry<PresentTrace>, kPresentTraceCapacity>
			presentTraceTimeline = {};
		std::array<FixedTraceEntry<DrawingWaitTrace>, kDrawingWaitTraceCapacity>
			drawingWaitTraceTimeline = {};
		std::array<FixedTraceEntry<CompositionCommitTrace>, kCompositionCommitTraceCapacity>
			compositionCommitTraceTimeline = {};
		std::array<DrawingFirstFrameSlot, kDrawingFirstFrameContactCapacity>
			drawingFirstFrameSlots = {};
		std::array<DrawingContactSummary, kDrawingContactSummaryCapacity>
			drawingContactSummaries = {};
		std::array<DrawingDeviceSummary, 5> drawingDeviceSummaries = {};
		uint32_t rtsCompletedContactCount = 0;
		uint32_t rtsAuxiliaryEventCount = 0;
		uint64_t inputTraceTimelineSequence = 0;
		uint64_t inputTraceTimelineOverwriteCount = 0;
		std::atomic<uint64_t> rtsCallbackSequence = 0;
		std::atomic<uint32_t> rtsContactSlotDroppedCount = 0;
		std::atomic<uint32_t> rtsCallbackContentionDroppedCount = 0;
		std::atomic<uint32_t> drawingTraceContentionDroppedCount = 0;
		std::atomic<uint32_t> phase2TraceContentionDroppedCount = 0;
		uint64_t drawingFrameTraceSequence = 0;
		uint64_t drawingContactFrameTraceSequence = 0;
		uint64_t presentTraceSequence = 0;
		uint64_t drawingWaitTraceSequence = 0;
		uint64_t compositionCommitTraceSequence = 0;
		uint64_t drawingContactSummaryDroppedCount = 0;
		int64_t previousPresentBeginQpc = 0;
		int64_t previousPresentEndQpc = 0;
		HANDLE inputDebugLogHandle = INVALID_HANDLE_VALUE;
		std::wstring inputDebugLogPath;
		uint64_t inputDebugSessionId = 0;
		int64_t inputDebugSessionBeginQpc = 0;
		int64_t inputDebugQpcFrequency = 0;

		const char* RtsPacketResultName(RtsPacketResult result) noexcept
		{
			switch (result)
			{
			case RtsPacketResult::NotApplicable: return "NotApplicable";
			case RtsPacketResult::Success: return "Success";
			case RtsPacketResult::InvalidArguments: return "InvalidArguments";
			case RtsPacketResult::StateGateBusy: return "StateGateBusy";
			case RtsPacketResult::ContextMissing: return "ContextMissing";
			case RtsPacketResult::ContextMismatch: return "ContextMismatch";
			case RtsPacketResult::BindingMissing: return "BindingMissing";
			case RtsPacketResult::DecoderMissing: return "DecoderMissing";
			case RtsPacketResult::DecoderEnsureFailed: return "DecoderEnsureFailed";
			case RtsPacketResult::BindingInsertFailed: return "BindingInsertFailed";
			case RtsPacketResult::GenerationMismatch: return "GenerationMismatch";
			case RtsPacketResult::PropertyCountMismatch: return "PropertyCountMismatch";
			case RtsPacketResult::DecodeFailed: return "DecodeFailed";
			case RtsPacketResult::PublishDownFailed: return "PublishDownFailed";
			case RtsPacketResult::PublishMoveFailed: return "PublishMoveFailed";
			case RtsPacketResult::PublishUpFailed: return "PublishUpFailed";
			default: return "Unknown";
			}
		}

		const char* DrawingInputResultName(DrawingInputResult result) noexcept
		{
			switch (result)
			{
			case DrawingInputResult::DownDequeued: return "DownDequeued";
			case DrawingInputResult::DownInitialized: return "DownInitialized";
			case DrawingInputResult::DownHandleInvalid: return "DownHandleInvalid";
			case DrawingInputResult::DownIgnoredByPolicy: return "DownIgnoredByPolicy";
			case DrawingInputResult::StrokeRuntimeUnavailable: return "StrokeRuntimeUnavailable";
			case DrawingInputResult::ModelerResetFailed: return "ModelerResetFailed";
			case DrawingInputResult::SnapshotReadSucceeded: return "SnapshotReadSucceeded";
			case DrawingInputResult::SnapshotReadFailed: return "SnapshotReadFailed";
			case DrawingInputResult::SnapshotFiltered: return "SnapshotFiltered";
			case DrawingInputResult::ModelerUpdateSucceeded: return "ModelerUpdateSucceeded";
			case DrawingInputResult::ModelerUpdateFailed: return "ModelerUpdateFailed";
			case DrawingInputResult::ContactRecycled: return "ContactRecycled";
			default: return "Unknown";
			}
		}

		const char* InputDeviceTypeName(uint32_t deviceType) noexcept
		{
			switch (deviceType)
			{
			case 0: return "Touch";
			case 1: return "Pen";
			case 2: return "MouseLeft";
			case 3: return "MouseRight";
			default: return "Unknown";
			}
		}

		const char* PresentSubmissionKindName(PresentSubmissionKind kind) noexcept
		{
			return kind == PresentSubmissionKind::Present1
				? "Present1" : "UpdateLayeredWindowIndirect";
		}

		int64_t QpcDeltaMicroseconds(int64_t newer, int64_t older) noexcept
		{
			if (inputDebugQpcFrequency <= 0 || newer <= older) return 0;
			const long double microseconds = static_cast<long double>(newer - older) *
				1000000.0L / static_cast<long double>(inputDebugQpcFrequency);
			return microseconds >= static_cast<long double>((std::numeric_limits<int64_t>::max)())
				? (std::numeric_limits<int64_t>::max)() : static_cast<int64_t>(microseconds);
		}

		int64_t SignedQpcDeltaMicroseconds(int64_t newer, int64_t older) noexcept
		{
			if (inputDebugQpcFrequency <= 0 || newer <= 0 || older <= 0) return 0;
			const long double microseconds = static_cast<long double>(newer - older) *
				1000000.0L / static_cast<long double>(inputDebugQpcFrequency);
			return static_cast<int64_t>(microseconds);
		}

		template <typename T, size_t Capacity>
		void StoreFixedTrace(std::array<FixedTraceEntry<T>, Capacity>& timeline,
			uint64_t& sequence, const T& value) noexcept
		{
			const uint64_t nextSequence = ++sequence;
			FixedTraceEntry<T>& entry = timeline[
				static_cast<size_t>((nextSequence - 1u) % Capacity)];
			entry.sequence = nextSequence;
			entry.value = value;
		}

		DrawingFirstFrameSlot* AcquireDrawingFirstFrameSlot(
			const DrawingContactFrameTrace& contact) noexcept
		{
			for (DrawingFirstFrameSlot& slot : drawingFirstFrameSlots)
			{
				if (slot.occupied && slot.tabletContextId == contact.tabletContextId &&
					slot.contactId == contact.contactId &&
					slot.contactGeneration == contact.contactGeneration) return &slot;
			}
			for (DrawingFirstFrameSlot& slot : drawingFirstFrameSlots)
			{
				if (slot.occupied) continue;
				slot = {};
				slot.occupied = true;
				slot.tabletContextId = contact.tabletContextId;
				slot.contactId = contact.contactId;
				slot.contactGeneration = contact.contactGeneration;
				return &slot;
			}
			return nullptr;
		}

		DrawingContactSummary* AcquireDrawingContactSummary(
			const DrawingContactFrameTrace& contact) noexcept
		{
			for (DrawingContactSummary& summary : drawingContactSummaries)
			{
				if (summary.occupied && summary.tabletContextId == contact.tabletContextId &&
					summary.contactId == contact.contactId &&
					summary.contactGeneration == contact.contactGeneration) return &summary;
			}
			for (DrawingContactSummary& summary : drawingContactSummaries)
			{
				if (summary.occupied) continue;
				summary = {};
				summary.occupied = true;
				summary.tabletContextId = contact.tabletContextId;
				summary.contactId = contact.contactId;
				summary.deviceType = contact.deviceType;
				summary.contactGeneration = contact.contactGeneration;
				drawingDeviceSummaries[(std::min)(static_cast<size_t>(contact.deviceType),
					drawingDeviceSummaries.size() - 1)].contactCount++;
				return &summary;
			}
			++drawingContactSummaryDroppedCount;
			return nullptr;
		}

		void StoreAtomicMinimumNonzero(
			std::atomic<uint64_t>& target, uint64_t value) noexcept
		{
			uint64_t current = target.load(std::memory_order_relaxed);
			while ((current == 0 || value < current) &&
				!target.compare_exchange_weak(current, value,
					std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		void StoreAtomicMaximum(std::atomic<uint64_t>& target, uint64_t value) noexcept
		{
			uint64_t current = target.load(std::memory_order_relaxed);
			while (value > current && !target.compare_exchange_weak(current, value,
				std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		void UpdateRtsResultAggregate(const RtsCallbackTrace& callback) noexcept
		{
			const size_t index = static_cast<size_t>(callback.packetResult);
			if (index >= rtsResultAggregates.size() ||
				callback.packetResult == RtsPacketResult::NotApplicable) return;
			RtsResultAggregate& aggregate = rtsResultAggregates[index];
			aggregate.count.fetch_add(1, std::memory_order_relaxed);
			if (callback.packetResult == RtsPacketResult::Success) return;
			// 失败即使丢掉 timeline 样本，也保留可按 QPC 对齐的首末范围。
			StoreAtomicMinimumNonzero(
				aggregate.firstCallbackSequence, callback.callbackSequence);
			StoreAtomicMaximum(aggregate.lastCallbackSequence, callback.callbackSequence);
			const uint64_t qpc = callback.qpc > 0
				? static_cast<uint64_t>(callback.qpc) : 0;
			if (qpc != 0)
			{
				StoreAtomicMinimumNonzero(aggregate.firstQpc, qpc);
				StoreAtomicMaximum(aggregate.lastQpc, qpc);
			}
		}

		bool TraceEventEquals(const char* eventName, const char* expected) noexcept
		{
			return eventName && std::strcmp(eventName, expected) == 0;
		}

		bool IsMoveTraceEvent(const char* eventName) noexcept
		{
			return TraceEventEquals(eventName, "Packets") ||
				TraceEventEquals(eventName, "InAirPackets");
		}

		bool IsContactTraceEvent(const char* eventName) noexcept
		{
			return TraceEventEquals(eventName, "StylusDown") ||
				IsMoveTraceEvent(eventName) || TraceEventEquals(eventName, "StylusUp");
		}

		void AppendTraceText(char* buffer, size_t capacity, size_t& length,
			const char* format, ...) noexcept
		{
			if (!buffer || capacity == 0 || length >= capacity - 1) return;
			va_list arguments;
			va_start(arguments, format);
			const int written = std::vsnprintf(
				buffer + length, capacity - length, format, arguments);
			va_end(arguments);
			if (written <= 0) return;
			length += (std::min)(static_cast<size_t>(written), capacity - length - 1);
		}

		void WriteRtsTraceLine(const char* text, size_t length) noexcept
		{
			if (!text || length == 0) return;
			if (inputDebugLogHandle != INVALID_HANDLE_VALUE)
			{
				DWORD written = 0;
				WriteFile(inputDebugLogHandle, text, static_cast<DWORD>((std::min)(
					length, static_cast<size_t>((std::numeric_limits<DWORD>::max)()))),
					&written, nullptr);
			}
			std::cout.write(text, static_cast<std::streamsize>(length));
			std::cout.flush();
		}

		void StoreRtsTimelineEvent(const RtsCallbackTrace& callback) noexcept
		{
			const uint64_t sequence = ++inputTraceTimelineSequence;
			if (sequence > kInputTraceTimelineCapacity) ++inputTraceTimelineOverwriteCount;
			InputTraceTimelineEntry& entry = inputTraceTimeline[
				static_cast<size_t>((sequence - 1u) % kInputTraceTimelineCapacity)];
			entry = {};
			entry.sequence = sequence;
			entry.kind = InputTraceTimelineKind::Rts;
			entry.rts = callback;
		}

		void StoreDrawingTimelineEvent(const DrawingInputTrace& input) noexcept
		{
			const uint64_t sequence = ++inputTraceTimelineSequence;
			if (sequence > kInputTraceTimelineCapacity) ++inputTraceTimelineOverwriteCount;
			InputTraceTimelineEntry& entry = inputTraceTimeline[
				static_cast<size_t>((sequence - 1u) % kInputTraceTimelineCapacity)];
			entry = {};
			entry.sequence = sequence;
			entry.kind = InputTraceTimelineKind::Drawing;
			entry.drawing = input;
		}

		RtsContactTraceSlot* FindRtsContactSlot(
			uint32_t tabletContextId, uint32_t contactId) noexcept
		{
			for (RtsContactTraceSlot& slot : rtsContactSlots)
			{
				if (slot.occupied && slot.tabletContextId == tabletContextId &&
					slot.contactId == contactId) return &slot;
			}
			return nullptr;
		}

		RtsContactTraceSlot* AcquireRtsContactSlot(
			uint32_t tabletContextId, uint32_t contactId) noexcept
		{
			if (RtsContactTraceSlot* existing = FindRtsContactSlot(
				tabletContextId, contactId)) return existing;
			for (RtsContactTraceSlot& slot : rtsContactSlots)
			{
				if (slot.occupied) continue;
				slot = {};
				slot.occupied = true;
				slot.tabletContextId = tabletContextId;
				slot.contactId = contactId;
				return &slot;
			}
			rtsContactSlotDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}

		void PrintRtsCallbackLine(const RtsCallbackTrace& callback,
			const char* category = "rts") noexcept
		{
			char line[1400] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[INPUT_TRACE][%s] callbackSeq=%llu qpc=%lld tid=%lu event=%s "
				"tcid=%u cid=%u device=%s(%u) packets=%lu properties=%lu "
				"packetResult=%s gate=%u binding=%u decoder=%u initialDecoder=%u "
				"ensureAttempted=%u ensureSucceeded=%u lifecycleGen=%llu decoderGen=%llu "
				"bindingGen=%llu rawKnown=%u raw=(%ld,%ld) decoded=%u "
				"pixel=(%.3f,%.3f) pressure=%.5f publishAttempted=%u published=%u "
				"dataInterest=0x%08x hresult=0x%08lx\r\n",
				category ? category : "rts",
				static_cast<unsigned long long>(callback.callbackSequence),
				static_cast<long long>(callback.qpc),
				static_cast<unsigned long>(callback.threadId),
				callback.eventName ? callback.eventName : "unknown",
				callback.tabletContextId, callback.contactId,
				InputDeviceTypeName(callback.deviceType), callback.deviceType,
				static_cast<unsigned long>(callback.packetCount),
				static_cast<unsigned long>(callback.propertyCount),
				RtsPacketResultName(callback.packetResult),
				callback.stateGateEntered ? 1u : 0u,
				callback.bindingAvailable ? 1u : 0u,
				callback.decoderAvailable ? 1u : 0u,
				callback.decoderInitiallyAvailable ? 1u : 0u,
				callback.decoderEnsureAttempted ? 1u : 0u,
				callback.decoderEnsureSucceeded ? 1u : 0u,
				static_cast<unsigned long long>(callback.lifecycleGeneration),
				static_cast<unsigned long long>(callback.decoderGeneration),
				static_cast<unsigned long long>(callback.bindingGeneration),
				callback.hasRawPosition ? 1u : 0u,
				static_cast<long>(callback.rawX), static_cast<long>(callback.rawY),
				callback.decoded ? 1u : 0u, callback.decodedX, callback.decodedY,
				callback.decodedPressure,
				callback.publishAttempted ? 1u : 0u,
				callback.published ? 1u : 0u,
				callback.dataInterest,
				static_cast<unsigned long>(callback.result));
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		void PrintDrawingInputLine(const DrawingInputTrace& input) noexcept
		{
			char line[768] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[INPUT_TRACE][drawing] qpc=%lld snapshotQpc=%lld tid=%lu event=%s tcid=%u cid=%u "
				"device=%s(%u) generation=%llu snapshotSeq=%llu phase=%u "
				"pixel=(%.3f,%.3f) pressure=%.5f result=%s modeled=%u real=%u\r\n",
				static_cast<long long>(input.qpc),
				static_cast<long long>(input.snapshotQpc),
				static_cast<unsigned long>(input.threadId),
				input.eventName ? input.eventName : "unknown",
				input.tabletContextId, input.contactId,
				InputDeviceTypeName(input.deviceType), input.deviceType,
				static_cast<unsigned long long>(input.contactGeneration),
				static_cast<unsigned long long>(input.snapshotSequence), input.phase,
				input.x, input.y, input.pressure,
				DrawingInputResultName(input.result),
				input.modeledPointCount, input.realPointCount);
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		void PrintRtsContactSummary(const RtsContactTraceSlot& slot) noexcept
		{
			char reasonLine[1024] = {};
			size_t reasonLength = 0;
			AppendTraceText(reasonLine, sizeof(reasonLine), reasonLength,
				"[INPUT_TRACE][contact-results] tcid=%u cid=%u device=%s(%u) reasons=",
				slot.tabletContextId, slot.contactId,
				InputDeviceTypeName(slot.deviceType), slot.deviceType);
			bool firstReason = true;
			for (size_t index = 0; index < slot.resultCounts.size(); ++index)
			{
				if (slot.resultCounts[index] == 0) continue;
				AppendTraceText(reasonLine, sizeof(reasonLine), reasonLength, "%s%s:%u",
					firstReason ? "" : ",", RtsPacketResultName(
						static_cast<RtsPacketResult>(index)), slot.resultCounts[index]);
				firstReason = false;
			}
			AppendTraceText(reasonLine, sizeof(reasonLine), reasonLength, "\r\n");
			WriteRtsTraceLine(reasonLine, reasonLength);

			char line[4096] = {};
			size_t length = 0;
			AppendTraceText(line, sizeof(line), length,
				"[RTS_TRACE][contact] tcid=%u cid=%u device=%u callbacks=%u "
				"packetsCallbacks=%u inAirCallbacks=%u packetSamples=%u inAirSamples=%u "
				"decodeFailures=%u publishOk=%u publishFailed=%u stored=%zu dropped=%u "
				"slotDrops=%u sequence=",
				slot.tabletContextId, slot.contactId, slot.deviceType, slot.callbackCount,
				slot.packetsCallbackCount, slot.inAirCallbackCount, slot.packetSampleCount,
				slot.inAirSampleCount, slot.decodedFailureCount, slot.publishSuccessCount,
				slot.publishFailureCount, slot.storedEventCount, slot.droppedEventCount,
				rtsContactSlotDroppedCount.load(std::memory_order_relaxed));
			for (size_t index = 0; index < slot.storedEventCount; ++index)
			{
				const RtsCallbackTrace& event = slot.events[index];
				AppendTraceText(line, sizeof(line), length,
					"%s%s{seq=%llu,qpc=%lld,tid=%lu,pk=%lu,prop=%lu,reason=%s,"
					"gate=%u,binding=%u,decoder=%u,gens=%llu/%llu,raw=%u:(%ld,%ld),"
					"decoded=%u:(%.3f,%.3f),published=%u,dataInterest=0x%08x,"
					"result=0x%08lx}",
					index == 0 ? "" : " -> ", event.eventName ? event.eventName : "unknown",
					static_cast<unsigned long long>(event.callbackSequence),
					static_cast<long long>(event.qpc),
					static_cast<unsigned long>(event.threadId),
					static_cast<unsigned long>(event.packetCount),
					static_cast<unsigned long>(event.propertyCount),
					RtsPacketResultName(event.packetResult),
					event.stateGateEntered ? 1u : 0u,
					event.bindingAvailable ? 1u : 0u,
					event.decoderAvailable ? 1u : 0u,
					static_cast<unsigned long long>(event.decoderGeneration),
					static_cast<unsigned long long>(event.bindingGeneration),
					event.hasRawPosition ? 1u : 0u,
					static_cast<long>(event.rawX), static_cast<long>(event.rawY),
					event.decoded ? 1u : 0u, event.decodedX, event.decodedY,
					event.published ? 1u : 0u,
					event.dataInterest,
					static_cast<unsigned long>(event.result));
			}
			WriteRtsTraceLine(line, length);
			WriteRtsTraceLine("\r\n", 2);
		}

		void ResetInputTraceStorage() noexcept
		{
			for (RtsContactTraceSlot& slot : rtsContactSlots) slot = {};
			for (RtsContactTraceSlot& slot : rtsCompletedContactSlots) slot = {};
			for (RtsCallbackTrace& callback : rtsAuxiliaryEvents) callback = {};
			for (InputTraceTimelineEntry& entry : inputTraceTimeline) entry = {};
			for (auto& entry : drawingFrameTraceTimeline) entry = {};
			for (auto& entry : drawingContactFrameTraceTimeline) entry = {};
			for (auto& entry : presentTraceTimeline) entry = {};
			for (auto& entry : drawingWaitTraceTimeline) entry = {};
			for (auto& entry : compositionCommitTraceTimeline) entry = {};
			for (DrawingFirstFrameSlot& slot : drawingFirstFrameSlots) slot = {};
			for (DrawingContactSummary& summary : drawingContactSummaries) summary = {};
			for (DrawingDeviceSummary& summary : drawingDeviceSummaries) summary = {};
			for (RtsResultAggregate& aggregate : rtsResultAggregates)
			{
				aggregate.count.store(0, std::memory_order_relaxed);
				aggregate.firstCallbackSequence.store(0, std::memory_order_relaxed);
				aggregate.lastCallbackSequence.store(0, std::memory_order_relaxed);
				aggregate.firstQpc.store(0, std::memory_order_relaxed);
				aggregate.lastQpc.store(0, std::memory_order_relaxed);
			}
			drawingResultCounts.fill(0);
			rtsCompletedContactCount = 0;
			rtsAuxiliaryEventCount = 0;
			inputTraceTimelineSequence = 0;
			inputTraceTimelineOverwriteCount = 0;
			rtsCallbackSequence.store(0, std::memory_order_relaxed);
			rtsContactSlotDroppedCount.store(0, std::memory_order_relaxed);
			rtsCallbackContentionDroppedCount.store(0, std::memory_order_relaxed);
			drawingTraceContentionDroppedCount.store(0, std::memory_order_relaxed);
			phase2TraceContentionDroppedCount.store(0, std::memory_order_relaxed);
			drawingFrameTraceSequence = 0;
			drawingContactFrameTraceSequence = 0;
			presentTraceSequence = 0;
			drawingWaitTraceSequence = 0;
			compositionCommitTraceSequence = 0;
			drawingContactSummaryDroppedCount = 0;
			previousPresentBeginQpc = 0;
			previousPresentEndQpc = 0;
		}

		void PrintDrawingFrameLine(const DrawingFrameTrace& frame) noexcept
		{
			char line[1800] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[INPUT_TRACE][frame] frameSeq=%llu frameStartQpc=%lld previousFrameUs=%lld "
				"latestSnapshotQpc=%lld latestSnapshotSeq=%llu latestInputAgeUs=%lld "
				"inputToFrameStartUs=%lld inputToRenderBeginUs=%lld "
				"inputToPresentBeginUs=%lld inputToPresentEndUs=%lld "
				"contacts=%u physical=%u terminal=%u dirtyValid=%u dirty=[%ld,%ld,%ld,%ld] "
				"geometryEmpty=%u geometryChanged=%u strokeContent=%u cursorDirty=%u cursorOnly=%u "
				"renderRequested=%u renderExecuted=%u renderBeginQpc=%lld renderEndQpc=%lld "
				"renderDurationUs=%lld forceFullPresent=%u fullFrame=%u presentAttempted=%u "
				"presentSucceeded=%u presentBeginQpc=%lld presentEndQpc=%lld presentCallDurationUs=%lld\r\n",
				static_cast<unsigned long long>(frame.frameSequence),
				static_cast<long long>(frame.frameStartQpc),
				static_cast<long long>(frame.previousFrameIntervalMicroseconds),
				static_cast<long long>(frame.latestSnapshotQpc),
				static_cast<unsigned long long>(frame.latestSnapshotSequence),
				static_cast<long long>(frame.latestInputAgeMicroseconds),
				static_cast<long long>(frame.latestInputAgeMicroseconds),
				static_cast<long long>(QpcDeltaMicroseconds(
					frame.renderBeginQpc, frame.latestSnapshotQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					frame.presentBeginQpc, frame.latestSnapshotQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					frame.presentEndQpc, frame.latestSnapshotQpc)), frame.contactCount,
				frame.hasPhysicalContact ? 1u : 0u, frame.terminalContact ? 1u : 0u,
				frame.dirtyValid ? 1u : 0u, static_cast<long>(frame.dirty.left),
				static_cast<long>(frame.dirty.top), static_cast<long>(frame.dirty.right),
				static_cast<long>(frame.dirty.bottom), frame.geometryEmpty ? 1u : 0u,
				frame.geometryChanged ? 1u : 0u, frame.strokeContent ? 1u : 0u,
				frame.cursorDirty ? 1u : 0u, frame.cursorOnly ? 1u : 0u,
				frame.renderRequested ? 1u : 0u, frame.renderExecuted ? 1u : 0u,
				static_cast<long long>(frame.renderBeginQpc),
				static_cast<long long>(frame.renderEndQpc),
				static_cast<long long>(QpcDeltaMicroseconds(
					frame.renderEndQpc, frame.renderBeginQpc)),
				frame.forceFullPresent ? 1u : 0u, frame.fullFrame ? 1u : 0u,
				frame.presentAttempted ? 1u : 0u, frame.presentSucceeded ? 1u : 0u,
				static_cast<long long>(frame.presentBeginQpc),
				static_cast<long long>(frame.presentEndQpc),
				static_cast<long long>(QpcDeltaMicroseconds(
					frame.presentEndQpc, frame.presentBeginQpc)));
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		void PrintDrawingContactFrameLine(const DrawingContactFrameTrace& contact,
			const char* category = "contact-frame") noexcept
		{
			char line[1800] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[INPUT_TRACE][%s] frameSeq=%llu contactFrame=%u qpc=%lld tcid=%u cid=%u "
				"device=%s(%u) generation=%llu phase=%u terminal=%u downQpc=%lld "
				"frameStartSnapshotQpc=%lld frameStartSnapshotSeq=%llu "
				"snapshotQpc=%lld snapshotSeq=%llu modelerOutputQpc=%lld "
				"pixel=(%.3f,%.3f) pressure=%.5f "
				"frameStartQpc=%lld snapshotReadQpc=%lld frameStartAgeUs=%lld snapshotReadAgeUs=%lld "
				"inputToModelerOutputUs=%lld inputToRenderBeginUs=%lld inputToPresentBeginUs=%lld "
				"previousFrameUs=%lld renderDurationUs=%lld presentCallDurationUs=%lld "
				"modelerUpdated=%u modeled=%u real=%u predicted=%u l0=%u l1CommittedIndex=%u "
				"predictionEndpointKnown=%u predictionEndpoint=(%.3f,%.3f) "
				"drawableGeometry=%u geometryChanged=%u rendered=%u renderBeginQpc=%lld "
				"presented=%u presentBeginQpc=%lld\r\n",
				category ? category : "contact-frame",
				static_cast<unsigned long long>(contact.frameSequence), contact.contactFrameIndex,
				static_cast<long long>(contact.recordQpc), contact.tabletContextId,
				contact.contactId, InputDeviceTypeName(contact.deviceType), contact.deviceType,
				static_cast<unsigned long long>(contact.contactGeneration), contact.phase,
				contact.terminal ? 1u : 0u, static_cast<long long>(contact.downQpc),
				static_cast<long long>(contact.frameStartSnapshotQpc),
				static_cast<unsigned long long>(contact.frameStartSnapshotSequence),
				static_cast<long long>(contact.snapshotQpc),
				static_cast<unsigned long long>(contact.snapshotSequence),
				static_cast<long long>(contact.modelerOutputQpc), contact.x, contact.y,
				contact.pressure, static_cast<long long>(contact.frameStartQpc),
				static_cast<long long>(contact.snapshotReadQpc),
				static_cast<long long>(contact.inputAgeAtFrameStartMicroseconds),
				static_cast<long long>(contact.inputAgeAtSnapshotReadMicroseconds),
				static_cast<long long>(QpcDeltaMicroseconds(
					contact.modelerOutputQpc, contact.snapshotQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					contact.renderBeginQpc, contact.snapshotQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					contact.presentBeginQpc, contact.snapshotQpc)),
				static_cast<long long>(contact.previousFrameIntervalMicroseconds),
				static_cast<long long>(contact.renderDurationMicroseconds),
				static_cast<long long>(contact.presentCallDurationMicroseconds),
				contact.modelerUpdated ? 1u : 0u, contact.modeledPointCount,
				contact.realPointCount, contact.predictedPointCount, contact.l0PointCount,
				contact.l1CommittedIndex, contact.hasPredictionEndpoint ? 1u : 0u,
				contact.predictionEndpointX, contact.predictionEndpointY,
				contact.drawableGeometry ? 1u : 0u, contact.geometryChanged ? 1u : 0u,
				contact.rendered ? 1u : 0u, static_cast<long long>(contact.renderBeginQpc),
				contact.presented ? 1u : 0u, static_cast<long long>(contact.presentBeginQpc));
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		void PrintPresentLine(const PresentTrace& present) noexcept
		{
			char line[768] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[INPUT_TRACE][present] frameSeq=%llu kind=%s beginQpc=%lld endQpc=%lld "
				"durationUs=%lld previousBeginIntervalUs=%lld previousEndIntervalUs=%lld "
				"hresult=0x%08lx syncInterval=%u flags=0x%08x dirtyRectCount=%u "
				"dirty=[%ld,%ld,%ld,%ld] presentFull=%u cpuSubmissionOnly=1\r\n",
				static_cast<unsigned long long>(present.frameSequence),
				PresentSubmissionKindName(present.kind), static_cast<long long>(present.beginQpc),
				static_cast<long long>(present.endQpc),
				static_cast<long long>(QpcDeltaMicroseconds(present.endQpc, present.beginQpc)),
				static_cast<long long>(present.previousBeginIntervalMicroseconds),
				static_cast<long long>(present.previousEndIntervalMicroseconds),
				static_cast<unsigned long>(present.result), present.syncInterval, present.flags,
				present.dirtyRectCount, static_cast<long>(present.dirty.left),
				static_cast<long>(present.dirty.top), static_cast<long>(present.dirty.right),
				static_cast<long>(present.dirty.bottom), present.presentFull ? 1u : 0u);
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		void PrintDrawingWaitLine(const DrawingWaitTrace& wait) noexcept
		{
			char line[768] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[INPUT_TRACE][wait] frameSeq=%llu frameStartQpc=%lld waitBeginQpc=%lld "
				"waitEndQpc=%lld targetDeadlineQpc=%lld previousTargetDeadlineQpc=%lld "
				"frameStartFromPreviousDeadlineUs=%lld "
				"requestedBudgetUs=%lld actualWaitUs=%lld overshootUs=%lld "
				"returnedBeforeDeadline=%u deadlineReached=%u\r\n",
				static_cast<unsigned long long>(wait.frameSequence),
				static_cast<long long>(wait.frameStartQpc),
				static_cast<long long>(wait.waitBeginQpc),
				static_cast<long long>(wait.waitEndQpc),
				static_cast<long long>(wait.targetDeadlineQpc),
				static_cast<long long>(wait.previousTargetDeadlineQpc),
				static_cast<long long>(SignedQpcDeltaMicroseconds(
					wait.frameStartQpc, wait.previousTargetDeadlineQpc)),
				static_cast<long long>(wait.requestedBudgetMicroseconds),
				static_cast<long long>(wait.actualWaitMicroseconds),
				static_cast<long long>(wait.overshootMicroseconds),
				wait.returnedBeforeDeadline ? 1u : 0u, wait.deadlineReached ? 1u : 0u);
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		bool BuildDefaultInputDebugLogPath(std::wstring& path, uint64_t sessionId)
		{
			std::array<wchar_t, MAX_PATH> modulePath = {};
			const DWORD length = GetModuleFileNameW(
				nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			if (length == 0 || length >= modulePath.size()) return false;
			size_t directoryLength = length;
			while (directoryLength > 0 && modulePath[directoryLength - 1] != L'\\' &&
				modulePath[directoryLength - 1] != L'/') --directoryLength;
			if (directoryLength == 0) return false;
			std::wstring directory(modulePath.data(), directoryLength);
			directory += L"InputDebugLogs";
			if (!CreateDirectoryW(directory.c_str(), nullptr) &&
				GetLastError() != ERROR_ALREADY_EXISTS) return false;
			wchar_t fileName[96] = {};
			if (swprintf_s(fileName, L"input-debug-%lu-%llu.log",
				static_cast<unsigned long>(GetCurrentProcessId()),
				static_cast<unsigned long long>(sessionId)) <= 0) return false;
			path = directory;
			path += L'\\';
			path += fileName;
			return true;
		}

		const char* InputDebugBuildConfiguration() noexcept
		{
#if defined(DRAW3_BUILD_CONFIGURATION_DEBUG)
			return "Debug";
#elif defined(DRAW3_BUILD_CONFIGURATION_RELEASE)
			return "Release";
#else
			return "Unknown";
#endif
		}

		const char* InputDebugBuildArchitecture() noexcept
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
#endif

		const char* DxgiAlphaModeName(DXGI_ALPHA_MODE mode)
		{
			switch (mode)
			{
			case DXGI_ALPHA_MODE_UNSPECIFIED: return "UNSPECIFIED";
			case DXGI_ALPHA_MODE_PREMULTIPLIED: return "PREMULTIPLIED";
			case DXGI_ALPHA_MODE_STRAIGHT: return "STRAIGHT";
			case DXGI_ALPHA_MODE_IGNORE: return "IGNORE";
			default: return "UNKNOWN";
			}
		}

		const char* DxgiSwapEffectName(DXGI_SWAP_EFFECT effect)
		{
			switch (effect)
			{
			case DXGI_SWAP_EFFECT_DISCARD: return "DISCARD";
			case DXGI_SWAP_EFFECT_SEQUENTIAL: return "SEQUENTIAL";
			case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "FLIP_SEQUENTIAL";
			case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "FLIP_DISCARD";
			default: return "UNKNOWN";
			}
		}

		const char* DxgiFormatName(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
			case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
			default: return "OTHER";
			}
		}
	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void ConfigureRtsTrace(bool enabled) noexcept
	{
		const bool wasEnabled = rtsTraceEnabled.load(std::memory_order_relaxed);
		if (wasEnabled == enabled) return;
		if (!enabled)
		{
			rtsTraceEnabled.store(false, std::memory_order_release);
			return;
		}

		// 先拒绝新的 callback，再尝试取得无等待写入权重置固定诊断缓冲区。
		rtsTraceEnabled.store(false, std::memory_order_release);
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard) return;
		ResetInputTraceStorage();
		rtsTraceEnabled.store(true, std::memory_order_release);
	}

	bool IsInputDebugTraceEnabled() noexcept
	{
		return rtsTraceEnabled.load(std::memory_order_acquire);
	}

	bool BeginInputDebugSession(const wchar_t* requestedPath) noexcept
	{
		if (inputDebugLogHandle != INVALID_HANDLE_VALUE) return true;
		LARGE_INTEGER qpc = {};
		LARGE_INTEGER frequency = {};
		if (!QueryPerformanceCounter(&qpc) || !QueryPerformanceFrequency(&frequency) ||
			frequency.QuadPart <= 0) return false;
		inputDebugSessionBeginQpc = qpc.QuadPart;
		inputDebugQpcFrequency = frequency.QuadPart;
		inputDebugSessionId = (static_cast<uint64_t>(GetCurrentProcessId()) << 32u) ^
			static_cast<uint64_t>(qpc.QuadPart);
		std::wstring path;
		if (requestedPath && requestedPath[0] != L'\0')
			path = requestedPath;
		else if (!BuildDefaultInputDebugLogPath(path, inputDebugSessionId))
			return false;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		inputDebugLogHandle = file;
		inputDebugLogPath = std::move(path);

		char header[2048] = {};
		const int length = std::snprintf(header, sizeof(header),
			"========== INPUT DEBUG SESSION BEGIN ==========\r\n"
			"sessionId=%llu beginQpc=%lld qpcFrequency=%lld pid=%lu\r\n"
			"buildId=base-418338a_%s_%s_%s_%s baseCommit=418338a "
			"buildTimestamp=%s %s configuration=%s architecture=%s\r\n"
			"================================================\r\n",
			static_cast<unsigned long long>(inputDebugSessionId),
			static_cast<long long>(inputDebugSessionBeginQpc),
			static_cast<long long>(frequency.QuadPart),
			static_cast<unsigned long>(GetCurrentProcessId()),
			InputDebugBuildConfiguration(), InputDebugBuildArchitecture(),
			__DATE__, __TIME__, __DATE__, __TIME__,
			InputDebugBuildConfiguration(), InputDebugBuildArchitecture());
		if (length > 0) WriteRtsTraceLine(header,
			(std::min)(static_cast<size_t>(length), sizeof(header) - 1));
		FlushFileBuffers(inputDebugLogHandle);
		return true;
	}

	const wchar_t* CurrentInputDebugLogPath() noexcept
	{
		return inputDebugLogPath.c_str();
	}

	void EndInputDebugSession() noexcept
	{
		if (inputDebugLogHandle == INVALID_HANDLE_VALUE) return;
		FlushRtsCallbackTrace();
		LARGE_INTEGER qpc = {};
		QueryPerformanceCounter(&qpc);
		char footer[512] = {};
		const int length = std::snprintf(footer, sizeof(footer),
			"========== INPUT DEBUG SESSION END ============\r\n"
			"sessionId=%llu endQpc=%lld\r\n"
			"================================================\r\n",
			static_cast<unsigned long long>(inputDebugSessionId),
			static_cast<long long>(qpc.QuadPart));
		if (length > 0) WriteRtsTraceLine(footer,
			(std::min)(static_cast<size_t>(length), sizeof(footer) - 1));
		FlushFileBuffers(inputDebugLogHandle);
		CloseHandle(inputDebugLogHandle);
		inputDebugLogHandle = INVALID_HANDLE_VALUE;
		inputDebugSessionId = 0;
		inputDebugSessionBeginQpc = 0;
		inputDebugQpcFrequency = 0;
		rtsTraceEnabled.store(false, std::memory_order_release);
	}

	void LogRtsInitializationState(const RtsInitializationTrace& state) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		char line[2048] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[RTS_TRACE][init] tid=%lu windowThread=%lu hwnd=0x%llx style=0x%llx "
			"exStyle=0x%llx tabletFlags=0x%llx digitizer=0x%x maxTouches=%d "
			"dataInterest=0x%08x selectedProperties=%lu packetOrder=%s\r\n"
			"[RTS_TRACE][init-hresult] CoInitializeEx=0x%08lx CoCreate=0x%08lx "
			"put_HWND=0x%08lx SetAllTabletsMode=0x%08lx packetDesired=0x%08lx "
			"packetFallback=0x%08lx Stylus2=0x%08lx Flicks=0x%08lx "
			"Stylus3=0x%08lx MultiTouchSet=0x%08lx MultiTouchGet=0x%08lx MultiTouch=%u "
			"Marshaler=0x%08lx AddPlugin=0x%08lx Enable=0x%08lx\r\n",
			static_cast<unsigned long>(state.currentThreadId),
			static_cast<unsigned long>(state.windowThreadId),
			static_cast<unsigned long long>(state.windowHandle),
			static_cast<unsigned long long>(state.windowStyle),
			static_cast<unsigned long long>(state.windowExtendedStyle),
			static_cast<unsigned long long>(state.tabletServiceFlags), state.digitizer,
			state.maximumTouches, state.dataInterest,
			static_cast<unsigned long>(state.selectedPacketPropertyCount),
			state.selectedPacketDescription ? state.selectedPacketDescription : "none",
			static_cast<unsigned long>(state.coInitializeResult),
			static_cast<unsigned long>(state.createStylusResult),
			static_cast<unsigned long>(state.putHwndResult),
			static_cast<unsigned long>(state.setAllTabletsModeResult),
			static_cast<unsigned long>(state.desiredPacketDescriptionResult),
			static_cast<unsigned long>(state.fallbackPacketDescriptionResult),
			static_cast<unsigned long>(state.queryStylus2Result),
			static_cast<unsigned long>(state.disableFlicksResult),
			static_cast<unsigned long>(state.queryStylus3Result),
			static_cast<unsigned long>(state.enableMultiTouchResult),
			static_cast<unsigned long>(state.readMultiTouchResult),
			state.multiTouchEnabled ? 1u : 0u,
			static_cast<unsigned long>(state.marshalerResult),
			static_cast<unsigned long>(state.addPluginResult),
			static_cast<unsigned long>(state.enableStylusResult));
		if (length > 0) WriteRtsTraceLine(line,
			(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
	}

	void RecordRtsCallback(const RtsCallbackTrace& callback) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsCallbackTrace stored = callback;
		stored.callbackSequence = rtsCallbackSequence.fetch_add(
			1, std::memory_order_relaxed) + 1u;
		// 失败 reason 在取得 trace 写权前累计；即使诊断自身争用也不会丢掉分类计数。
		UpdateRtsResultAggregate(stored);
		// 同步回调绝不等待；并发写入时丢弃诊断样本，不影响 production 输入。
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard)
		{
			rtsCallbackContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!IsContactTraceEvent(stored.eventName) ||
			(stored.tabletContextId == 0 && stored.contactId == 0 &&
				stored.packetResult == RtsPacketResult::InvalidArguments))
		{
			const uint32_t sequence = ++rtsAuxiliaryEventCount;
			if (sequence <= kRtsTraceAuxiliaryEventCapacity)
				rtsAuxiliaryEvents[sequence - 1] = stored;
			StoreRtsTimelineEvent(stored);
			return;
		}

		RtsContactTraceSlot* slot = FindRtsContactSlot(
			stored.tabletContextId, stored.contactId);
		if (TraceEventEquals(stored.eventName, "StylusDown"))
		{
			if (slot) *slot = {};
			slot = slot ? slot : AcquireRtsContactSlot(
				stored.tabletContextId, stored.contactId);
			if (!slot)
			{
				StoreRtsTimelineEvent(stored);
				return;
			}
			slot->occupied = true;
			slot->downObserved = true;
			slot->tabletContextId = stored.tabletContextId;
			slot->contactId = stored.contactId;
			slot->deviceType = stored.deviceType;
		}
		else if (TraceEventEquals(stored.eventName, "InAirPackets") &&
			(!slot || !slot->downObserved))
		{
			// 悬停数据没有活动 Down 时只忽略，不创建无限期 trace 槽位。
			if (stored.packetResult != RtsPacketResult::Success)
				StoreRtsTimelineEvent(stored);
			return;
		}
		else if (!slot)
		{
			slot = AcquireRtsContactSlot(
				stored.tabletContextId, stored.contactId);
			if (!slot)
			{
				StoreRtsTimelineEvent(stored);
				return;
			}
			slot->deviceType = stored.deviceType;
		}

		slot->occupied = true;
		slot->callbackCount++;
		if (IsMoveTraceEvent(stored.eventName))
		{
			if (TraceEventEquals(stored.eventName, "Packets"))
			{
				++slot->packetsCallbackCount;
				slot->packetSampleCount += stored.packetCount;
			}
			else
			{
				++slot->inAirCallbackCount;
				slot->inAirSampleCount += stored.packetCount;
			}
		}
		if (stored.packetCount > 0 && !stored.decoded) ++slot->decodedFailureCount;
		if (stored.publishAttempted && !TraceEventEquals(stored.eventName, "InAirPackets"))
		{
			if (stored.published) ++slot->publishSuccessCount;
			else ++slot->publishFailureCount;
		}
		const size_t resultIndex = static_cast<size_t>(stored.packetResult);
		if (resultIndex < slot->resultCounts.size()) ++slot->resultCounts[resultIndex];

		const bool isMove = IsMoveTraceEvent(stored.eventName);
		const bool critical = stored.packetResult != RtsPacketResult::Success &&
			stored.packetResult != RtsPacketResult::NotApplicable;
		const bool canStore = slot->storedEventCount < kRtsTraceEventCapacity &&
			(!isMove || critical || slot->storedMoveEventCount < kRtsTraceMoveEventLimit);
		if (canStore)
		{
			slot->events[slot->storedEventCount++] = stored;
			if (isMove && !critical) ++slot->storedMoveEventCount;
		}
		else ++slot->droppedEventCount;

		const bool sampleSuccessfulMove = isMove && !critical &&
			(slot->packetsCallbackCount <= 4u || (slot->packetsCallbackCount % 16u) == 0u);
		if (critical || !isMove || sampleSuccessfulMove) StoreRtsTimelineEvent(stored);

		if (TraceEventEquals(stored.eventName, "StylusUp"))
		{
			const uint32_t completedIndex = rtsCompletedContactCount++;
			if (completedIndex < kRtsTraceContactCapacity)
				rtsCompletedContactSlots[completedIndex] = *slot;
			*slot = {};
		}
	}

	void RecordDrawingInput(const DrawingInputTrace& input) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard)
		{
			drawingTraceContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const size_t resultIndex = static_cast<size_t>(input.result);
		if (resultIndex < drawingResultCounts.size()) ++drawingResultCounts[resultIndex];
		const bool sampledSuccess =
			(input.result == DrawingInputResult::SnapshotReadSucceeded ||
				input.result == DrawingInputResult::ModelerUpdateSucceeded) &&
			(input.snapshotSequence > 4u && (input.snapshotSequence % 8u) != 0u) &&
			input.phase < 2u;
		if (!sampledSuccess) StoreDrawingTimelineEvent(input);
	}

	void RecordDrawingFrame(const DrawingFrameTrace& frame) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(phase2TraceWriteLock);
		if (!writeGuard)
		{
			phase2TraceContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		StoreFixedTrace(drawingFrameTraceTimeline, drawingFrameTraceSequence, frame);
	}

	void RecordDrawingContactFrame(const DrawingContactFrameTrace& contact) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(phase2TraceWriteLock);
		if (!writeGuard)
		{
			phase2TraceContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		StoreFixedTrace(drawingContactFrameTraceTimeline,
			drawingContactFrameTraceSequence, contact);
		if (contact.contactFrameIndex > 0 &&
			contact.contactFrameIndex <= kDrawingFirstFrameCount)
		{
			if (DrawingFirstFrameSlot* slot = AcquireDrawingFirstFrameSlot(contact))
				slot->frames[contact.contactFrameIndex - 1u] = contact;
		}
		DrawingContactSummary* summary = AcquireDrawingContactSummary(contact);
		if (summary)
		{
			if (summary->frameCount == 0)
			{
				summary->firstFrameSequence = contact.frameSequence;
				summary->firstSnapshotSequence = contact.snapshotSequence;
				summary->downQpc = contact.downQpc;
			}
			summary->lastFrameSequence = contact.frameSequence;
			summary->lastSnapshotSequence = contact.snapshotSequence;
			++summary->frameCount;
			if (contact.modelerUpdated)
			{
				++summary->modelerUpdateCount;
				if (summary->firstModelerOutputQpc == 0 && contact.modeledPointCount > 0)
					summary->firstModelerOutputQpc = contact.modelerOutputQpc;
			}
			const bool strokeGeometryChanged =
				contact.drawableGeometry && contact.geometryChanged;
			if (summary->firstDrawableGeometryQpc == 0 && strokeGeometryChanged)
				summary->firstDrawableGeometryQpc = contact.recordQpc;
			// 只把本 contact 的新墨迹几何归因到首帧，避免 cursor/其他 contact 的提交误计。
			if (summary->firstRenderQpc == 0 && strokeGeometryChanged && contact.rendered)
				summary->firstRenderQpc = contact.renderBeginQpc;
			if (summary->firstPresentQpc == 0 && strokeGeometryChanged && contact.presented)
				summary->firstPresentQpc = contact.presentBeginQpc;
			if (strokeGeometryChanged && contact.rendered) ++summary->renderCount;
			if (strokeGeometryChanged && contact.presented) ++summary->presentCount;
			summary->maximumFrameIntervalMicroseconds = (std::max)(
				summary->maximumFrameIntervalMicroseconds,
				contact.previousFrameIntervalMicroseconds);
			const int64_t inputAge = (std::max)(contact.inputAgeAtFrameStartMicroseconds,
				contact.inputAgeAtSnapshotReadMicroseconds);
			summary->maximumInputAgeMicroseconds = (std::max)(
				summary->maximumInputAgeMicroseconds, inputAge);
			summary->maximumRenderDurationMicroseconds = (std::max)(
				summary->maximumRenderDurationMicroseconds,
				contact.renderDurationMicroseconds);
			summary->maximumPresentCallDurationMicroseconds = (std::max)(
				summary->maximumPresentCallDurationMicroseconds,
				contact.presentCallDurationMicroseconds);
			if (contact.previousFrameIntervalMicroseconds > 10000)
				++summary->frameGapOver10MsCount;
			if (contact.previousFrameIntervalMicroseconds > 12000)
				++summary->frameGapOver12MsCount;
			if (contact.previousFrameIntervalMicroseconds > 16000)
				++summary->frameGapOver16MsCount;
		}
		DrawingDeviceSummary& deviceSummary = drawingDeviceSummaries[(std::min)(
			static_cast<size_t>(contact.deviceType), drawingDeviceSummaries.size() - 1)];
		if (deviceSummary.lastFrameSequence != contact.frameSequence)
		{
			deviceSummary.lastFrameSequence = contact.frameSequence;
			++deviceSummary.activeFrameCount;
			if (contact.previousFrameIntervalMicroseconds > 10000)
				++deviceSummary.frameGapOver10MsCount;
			if (contact.previousFrameIntervalMicroseconds > 12000)
				++deviceSummary.frameGapOver12MsCount;
			if (contact.previousFrameIntervalMicroseconds > 16000)
				++deviceSummary.frameGapOver16MsCount;
		}
		if (contact.geometryChanged &&
			deviceSummary.lastGeometryFrameSequence != contact.frameSequence)
		{
			deviceSummary.lastGeometryFrameSequence = contact.frameSequence;
			++deviceSummary.geometryFrameCount;
		}
		if (contact.rendered && deviceSummary.lastRenderFrameSequence != contact.frameSequence)
		{
			deviceSummary.lastRenderFrameSequence = contact.frameSequence;
			++deviceSummary.renderFrameCount;
		}
		if (contact.presented && deviceSummary.lastPresentFrameSequence != contact.frameSequence)
		{
			deviceSummary.lastPresentFrameSequence = contact.frameSequence;
			++deviceSummary.presentCount;
		}
		deviceSummary.maximumFrameIntervalMicroseconds = (std::max)(
			deviceSummary.maximumFrameIntervalMicroseconds,
			contact.previousFrameIntervalMicroseconds);
		deviceSummary.maximumInputAgeMicroseconds = (std::max)(
			deviceSummary.maximumInputAgeMicroseconds,
			(std::max)(contact.inputAgeAtFrameStartMicroseconds,
				contact.inputAgeAtSnapshotReadMicroseconds));
		deviceSummary.maximumRenderDurationMicroseconds = (std::max)(
			deviceSummary.maximumRenderDurationMicroseconds,
			contact.renderDurationMicroseconds);
		deviceSummary.maximumPresentCallDurationMicroseconds = (std::max)(
			deviceSummary.maximumPresentCallDurationMicroseconds,
			contact.presentCallDurationMicroseconds);
	}

	void RecordPresentSubmission(const PresentTrace& present) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(phase2TraceWriteLock);
		if (!writeGuard)
		{
			phase2TraceContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		PresentTrace stored = present;
		stored.previousBeginIntervalMicroseconds = QpcDeltaMicroseconds(
			stored.beginQpc, previousPresentBeginQpc);
		stored.previousEndIntervalMicroseconds = QpcDeltaMicroseconds(
			stored.endQpc, previousPresentEndQpc);
		previousPresentBeginQpc = stored.beginQpc;
		previousPresentEndQpc = stored.endQpc;
		StoreFixedTrace(presentTraceTimeline, presentTraceSequence, stored);
	}

	void RecordDrawingWait(const DrawingWaitTrace& wait) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(phase2TraceWriteLock);
		if (!writeGuard)
		{
			phase2TraceContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		StoreFixedTrace(drawingWaitTraceTimeline, drawingWaitTraceSequence, wait);
	}

	void RecordCompositionCommit(const CompositionCommitTrace& commit) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(phase2TraceWriteLock);
		if (!writeGuard)
		{
			phase2TraceContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		StoreFixedTrace(compositionCommitTraceTimeline,
			compositionCommitTraceSequence, commit);
	}

	void FlushRtsCallbackTrace() noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard) return; // 停止回调后的 flush 也不等待异常并发写入。
		RtsTraceWriteGuard phase2WriteGuard(phase2TraceWriteLock);
		if (!phase2WriteGuard) return;

		const uint32_t storedAuxiliaryCount = (std::min)(rtsAuxiliaryEventCount,
			static_cast<uint32_t>(kRtsTraceAuxiliaryEventCapacity));
		const uint32_t storedCompletedCount = (std::min)(rtsCompletedContactCount,
			static_cast<uint32_t>(kRtsTraceContactCapacity));
		const uint32_t contentionDrops =
			rtsCallbackContentionDroppedCount.load(std::memory_order_relaxed);
		const uint32_t drawingContentionDrops =
			drawingTraceContentionDroppedCount.load(std::memory_order_relaxed);
		const uint32_t phase2ContentionDrops =
			phase2TraceContentionDroppedCount.load(std::memory_order_relaxed);
		const uint64_t storedTimelineCount = (std::min)(inputTraceTimelineSequence,
			static_cast<uint64_t>(kInputTraceTimelineCapacity));
		if (storedAuxiliaryCount == 0 && storedCompletedCount == 0 &&
			storedTimelineCount == 0 && drawingFrameTraceSequence == 0 &&
			drawingContactFrameTraceSequence == 0 && presentTraceSequence == 0 &&
			drawingWaitTraceSequence == 0 && compositionCommitTraceSequence == 0 &&
			contentionDrops == 0 && drawingContentionDrops == 0 && phase2ContentionDrops == 0)
			return;

		const uint64_t firstTimelineSequence =
			inputTraceTimelineSequence - storedTimelineCount + 1u;
		for (uint64_t sequence = firstTimelineSequence;
			sequence <= inputTraceTimelineSequence && storedTimelineCount > 0; ++sequence)
		{
			const InputTraceTimelineEntry& entry = inputTraceTimeline[
				static_cast<size_t>((sequence - 1u) % kInputTraceTimelineCapacity)];
			if (entry.sequence != sequence) continue;
			if (entry.kind == InputTraceTimelineKind::Rts)
				PrintRtsCallbackLine(entry.rts);
			else
				PrintDrawingInputLine(entry.drawing);
		}
		auto printDrawingFrames = [&]() noexcept
			{
				const uint64_t storedCount = (std::min)(drawingFrameTraceSequence,
					static_cast<uint64_t>(kDrawingFrameTraceCapacity));
				const uint64_t firstSequence = drawingFrameTraceSequence - storedCount + 1u;
				for (uint64_t sequence = firstSequence;
					sequence <= drawingFrameTraceSequence && storedCount > 0; ++sequence)
				{
					const auto& entry = drawingFrameTraceTimeline[
						static_cast<size_t>((sequence - 1u) % kDrawingFrameTraceCapacity)];
					if (entry.sequence == sequence) PrintDrawingFrameLine(entry.value);
				}
			};
		auto printContactFrames = [&]() noexcept
			{
				const uint64_t storedCount = (std::min)(drawingContactFrameTraceSequence,
					static_cast<uint64_t>(kDrawingContactFrameTraceCapacity));
				const uint64_t firstSequence = drawingContactFrameTraceSequence - storedCount + 1u;
				for (uint64_t sequence = firstSequence;
					sequence <= drawingContactFrameTraceSequence && storedCount > 0; ++sequence)
				{
					const auto& entry = drawingContactFrameTraceTimeline[
						static_cast<size_t>((sequence - 1u) % kDrawingContactFrameTraceCapacity)];
					if (entry.sequence == sequence) PrintDrawingContactFrameLine(entry.value);
				}
			};
		printDrawingFrames();
		printContactFrames();
		for (const DrawingFirstFrameSlot& slot : drawingFirstFrameSlots)
		{
			if (!slot.occupied) continue;
			for (const DrawingContactFrameTrace& frame : slot.frames)
			{
				if (frame.contactFrameIndex != 0)
					PrintDrawingContactFrameLine(frame, "contact-first-frame");
			}
		}
		const uint64_t storedPresentCount = (std::min)(presentTraceSequence,
			static_cast<uint64_t>(kPresentTraceCapacity));
		const uint64_t firstPresentSequence = presentTraceSequence - storedPresentCount + 1u;
		for (uint64_t sequence = firstPresentSequence;
			sequence <= presentTraceSequence && storedPresentCount > 0; ++sequence)
		{
			const auto& entry = presentTraceTimeline[
				static_cast<size_t>((sequence - 1u) % kPresentTraceCapacity)];
			if (entry.sequence == sequence) PrintPresentLine(entry.value);
		}
		const uint64_t storedWaitCount = (std::min)(drawingWaitTraceSequence,
			static_cast<uint64_t>(kDrawingWaitTraceCapacity));
		const uint64_t firstWaitSequence = drawingWaitTraceSequence - storedWaitCount + 1u;
		for (uint64_t sequence = firstWaitSequence;
			sequence <= drawingWaitTraceSequence && storedWaitCount > 0; ++sequence)
		{
			const auto& entry = drawingWaitTraceTimeline[
				static_cast<size_t>((sequence - 1u) % kDrawingWaitTraceCapacity)];
			if (entry.sequence == sequence) PrintDrawingWaitLine(entry.value);
		}
		const uint64_t storedCommitCount = (std::min)(compositionCommitTraceSequence,
			static_cast<uint64_t>(kCompositionCommitTraceCapacity));
		const uint64_t firstCommitSequence = compositionCommitTraceSequence - storedCommitCount + 1u;
		for (uint64_t sequence = firstCommitSequence;
			sequence <= compositionCommitTraceSequence && storedCommitCount > 0; ++sequence)
		{
			const auto& entry = compositionCommitTraceTimeline[
				static_cast<size_t>((sequence - 1u) % kCompositionCommitTraceCapacity)];
			if (entry.sequence != sequence) continue;
			char commitLine[384] = {};
			const int commitLength = std::snprintf(commitLine, sizeof(commitLine),
				"[INPUT_TRACE][composition-commit] scope=initialization beginQpc=%lld "
				"endQpc=%lld durationUs=%lld hresult=0x%08lx\r\n",
				static_cast<long long>(entry.value.beginQpc),
				static_cast<long long>(entry.value.endQpc),
				static_cast<long long>(QpcDeltaMicroseconds(
					entry.value.endQpc, entry.value.beginQpc)),
				static_cast<unsigned long>(entry.value.result));
			if (commitLength > 0) WriteRtsTraceLine(commitLine,
				(std::min)(static_cast<size_t>(commitLength), sizeof(commitLine) - 1));
		}
		// timeline 覆盖后仍输出有界 lifecycle/error 副本，避免 stored 统计变成不可读取的数据。
		for (uint32_t index = 0; index < storedAuxiliaryCount; ++index)
			PrintRtsCallbackLine(rtsAuxiliaryEvents[index], "rts-auxiliary");
		for (uint32_t index = 0; index < storedCompletedCount; ++index)
			PrintRtsContactSummary(rtsCompletedContactSlots[index]);
		for (const RtsContactTraceSlot& slot : rtsContactSlots)
		{
			if (slot.occupied) PrintRtsContactSummary(slot);
		}
		for (size_t index = 0; index < rtsResultAggregates.size(); ++index)
		{
			const RtsResultAggregate& aggregate = rtsResultAggregates[index];
			const uint64_t count = aggregate.count.load(std::memory_order_relaxed);
			if (count == 0) continue;
			char reasonLine[320] = {};
			const RtsPacketResult result = static_cast<RtsPacketResult>(index);
			const int reasonLength = result == RtsPacketResult::Success
				? std::snprintf(reasonLine, sizeof(reasonLine),
					"[INPUT_TRACE][rts-result] result=%s count=%llu\r\n",
					RtsPacketResultName(result), static_cast<unsigned long long>(count))
				: std::snprintf(reasonLine, sizeof(reasonLine),
					"[INPUT_TRACE][rts-result] result=%s count=%llu firstCallbackSeq=%llu "
					"lastCallbackSeq=%llu firstQpc=%llu lastQpc=%llu\r\n",
					RtsPacketResultName(result), static_cast<unsigned long long>(count),
					static_cast<unsigned long long>(aggregate.firstCallbackSequence.load(
						std::memory_order_relaxed)),
					static_cast<unsigned long long>(aggregate.lastCallbackSequence.load(
						std::memory_order_relaxed)),
					static_cast<unsigned long long>(aggregate.firstQpc.load(
						std::memory_order_relaxed)),
					static_cast<unsigned long long>(aggregate.lastQpc.load(
						std::memory_order_relaxed)));
			if (reasonLength > 0) WriteRtsTraceLine(reasonLine,
				(std::min)(static_cast<size_t>(reasonLength), sizeof(reasonLine) - 1));
		}

		char drawingLine[1024] = {};
		size_t drawingLength = 0;
		AppendTraceText(drawingLine, sizeof(drawingLine), drawingLength,
			"[INPUT_TRACE][drawing-results]");
		for (size_t index = 0; index < drawingResultCounts.size(); ++index)
		{
			if (drawingResultCounts[index] == 0) continue;
			AppendTraceText(drawingLine, sizeof(drawingLine), drawingLength, " %s=%llu",
				DrawingInputResultName(static_cast<DrawingInputResult>(index)),
				static_cast<unsigned long long>(drawingResultCounts[index]));
		}
		AppendTraceText(drawingLine, sizeof(drawingLine), drawingLength, "\r\n");
		WriteRtsTraceLine(drawingLine, drawingLength);

		for (const DrawingContactSummary& summary : drawingContactSummaries)
		{
			if (!summary.occupied) continue;
			char summaryLine[896] = {};
			const int summaryLength = std::snprintf(summaryLine, sizeof(summaryLine),
				"[INPUT_TRACE][contact-summary] tcid=%u cid=%u device=%s(%u) generation=%llu "
				"frames=%u modelerUpdates=%u renders=%u presents=%u firstFrameSeq=%llu "
				"lastFrameSeq=%llu firstSnapshotSeq=%llu "
				"lastSnapshotSeq=%llu downQpc=%lld firstDrawableGeometryQpc=%lld firstRenderQpc=%lld "
				"firstModelerOutputQpc=%lld firstPresentQpc=%lld downToModelerOutputUs=%lld "
				"downToGeometryUs=%lld downToRenderUs=%lld downToPresentUs=%lld "
				"maxFrameIntervalUs=%lld gapOver10Ms=%u gapOver12Ms=%u gapOver16Ms=%u maxInputAgeUs=%lld "
				"maxRenderDurationUs=%lld maxPresentCallDurationUs=%lld\r\n",
				summary.tabletContextId, summary.contactId,
				InputDeviceTypeName(summary.deviceType), summary.deviceType,
				static_cast<unsigned long long>(summary.contactGeneration), summary.frameCount,
				summary.modelerUpdateCount, summary.renderCount, summary.presentCount,
				static_cast<unsigned long long>(summary.firstFrameSequence),
				static_cast<unsigned long long>(summary.lastFrameSequence),
				static_cast<unsigned long long>(summary.firstSnapshotSequence),
				static_cast<unsigned long long>(summary.lastSnapshotSequence),
				static_cast<long long>(summary.downQpc),
				static_cast<long long>(summary.firstDrawableGeometryQpc),
				static_cast<long long>(summary.firstRenderQpc),
				static_cast<long long>(summary.firstModelerOutputQpc),
				static_cast<long long>(summary.firstPresentQpc),
				static_cast<long long>(QpcDeltaMicroseconds(
					summary.firstModelerOutputQpc, summary.downQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					summary.firstDrawableGeometryQpc, summary.downQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					summary.firstRenderQpc, summary.downQpc)),
				static_cast<long long>(QpcDeltaMicroseconds(
					summary.firstPresentQpc, summary.downQpc)),
				static_cast<long long>(summary.maximumFrameIntervalMicroseconds),
				summary.frameGapOver10MsCount, summary.frameGapOver12MsCount,
				summary.frameGapOver16MsCount,
				static_cast<long long>(summary.maximumInputAgeMicroseconds),
				static_cast<long long>(summary.maximumRenderDurationMicroseconds),
				static_cast<long long>(summary.maximumPresentCallDurationMicroseconds));
			if (summaryLength > 0) WriteRtsTraceLine(summaryLine,
				(std::min)(static_cast<size_t>(summaryLength), sizeof(summaryLine) - 1));
		}
		for (size_t index = 0; index < drawingDeviceSummaries.size(); ++index)
		{
			const DrawingDeviceSummary& summary = drawingDeviceSummaries[index];
			if (summary.contactCount == 0 && summary.activeFrameCount == 0) continue;
			char deviceLine[512] = {};
			const int deviceLength = std::snprintf(deviceLine, sizeof(deviceLine),
				"[INPUT_TRACE][device-summary] device=%s(%zu) contacts=%llu activeFrames=%llu "
				"geometryFrames=%llu renderFrames=%llu presentCount=%llu maxFrameIntervalUs=%lld "
				"gapOver10Ms=%llu gapOver12Ms=%llu gapOver16Ms=%llu maxInputAgeUs=%lld maxRenderDurationUs=%lld "
				"maxPresentCallDurationUs=%lld\r\n",
				InputDeviceTypeName(static_cast<uint32_t>(index)), index,
				static_cast<unsigned long long>(summary.contactCount),
				static_cast<unsigned long long>(summary.activeFrameCount),
				static_cast<unsigned long long>(summary.geometryFrameCount),
				static_cast<unsigned long long>(summary.renderFrameCount),
				static_cast<unsigned long long>(summary.presentCount),
				static_cast<long long>(summary.maximumFrameIntervalMicroseconds),
				static_cast<unsigned long long>(summary.frameGapOver10MsCount),
				static_cast<unsigned long long>(summary.frameGapOver12MsCount),
				static_cast<unsigned long long>(summary.frameGapOver16MsCount),
				static_cast<long long>(summary.maximumInputAgeMicroseconds),
				static_cast<long long>(summary.maximumRenderDurationMicroseconds),
				static_cast<long long>(summary.maximumPresentCallDurationMicroseconds));
			if (deviceLength > 0) WriteRtsTraceLine(deviceLine,
				(std::min)(static_cast<size_t>(deviceLength), sizeof(deviceLine) - 1));
		}

		char line[896] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[INPUT_TRACE][buffer] timeline=%llu/%llu overwrites=%llu auxiliary=%u/%u "
			"contacts=%u/%u slotDrops=%u rtsContentionDrops=%u drawingContentionDrops=%u "
			"frames=%llu/%llu frameOverwrites=%llu contactFrames=%llu/%llu contactFrameOverwrites=%llu "
			"presents=%llu/%llu presentOverwrites=%llu waits=%llu/%llu waitOverwrites=%llu "
			"compositionCommits=%llu/%llu compositionCommitOverwrites=%llu "
			"contactSummaryDrops=%llu phase2ContentionDrops=%u\r\n",
			static_cast<unsigned long long>(storedTimelineCount),
			static_cast<unsigned long long>(inputTraceTimelineSequence),
			static_cast<unsigned long long>(inputTraceTimelineOverwriteCount),
			storedAuxiliaryCount, rtsAuxiliaryEventCount,
			storedCompletedCount, rtsCompletedContactCount,
			rtsContactSlotDroppedCount.load(std::memory_order_relaxed), contentionDrops,
			drawingContentionDrops,
			static_cast<unsigned long long>((std::min)(drawingFrameTraceSequence,
				static_cast<uint64_t>(kDrawingFrameTraceCapacity))),
			static_cast<unsigned long long>(drawingFrameTraceSequence),
			static_cast<unsigned long long>(drawingFrameTraceSequence > kDrawingFrameTraceCapacity
				? drawingFrameTraceSequence - kDrawingFrameTraceCapacity : 0),
			static_cast<unsigned long long>((std::min)(drawingContactFrameTraceSequence,
				static_cast<uint64_t>(kDrawingContactFrameTraceCapacity))),
			static_cast<unsigned long long>(drawingContactFrameTraceSequence),
			static_cast<unsigned long long>(drawingContactFrameTraceSequence >
				kDrawingContactFrameTraceCapacity
				? drawingContactFrameTraceSequence - kDrawingContactFrameTraceCapacity : 0),
			static_cast<unsigned long long>(storedPresentCount),
			static_cast<unsigned long long>(presentTraceSequence),
			static_cast<unsigned long long>(presentTraceSequence > kPresentTraceCapacity
				? presentTraceSequence - kPresentTraceCapacity : 0),
			static_cast<unsigned long long>(storedWaitCount),
			static_cast<unsigned long long>(drawingWaitTraceSequence),
			static_cast<unsigned long long>(drawingWaitTraceSequence > kDrawingWaitTraceCapacity
				? drawingWaitTraceSequence - kDrawingWaitTraceCapacity : 0),
			static_cast<unsigned long long>(storedCommitCount),
			static_cast<unsigned long long>(compositionCommitTraceSequence),
			static_cast<unsigned long long>(compositionCommitTraceSequence >
				kCompositionCommitTraceCapacity
				? compositionCommitTraceSequence - kCompositionCommitTraceCapacity : 0),
			static_cast<unsigned long long>(drawingContactSummaryDroppedCount),
			phase2ContentionDrops);
		if (length > 0) WriteRtsTraceLine(line,
			(std::min)(static_cast<size_t>(length), sizeof(line) - 1));

		ResetInputTraceStorage();
	}

#endif

	double GetQpcTimeMilliseconds()
	{
		static LARGE_INTEGER frequency = {};
		if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency); // 频率固定，初始化一次即可。
		LARGE_INTEGER counter = {};
		QueryPerformanceCounter(&counter);
		return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	}

	void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
	{
		const double waitTimeMs = 1000.0 / targetFPS - frameTimeSpentMs; // 扣除本帧工作耗时后再补足目标帧间隔。
		if (waitTimeMs <= 0.0) return;

		static LARGE_INTEGER frequency = {};
		if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
		LARGE_INTEGER startCounter = {};
		LARGE_INTEGER currentCounter = {};
		QueryPerformanceCounter(&startCounter);
		const long long waitTicks = static_cast<long long>(waitTimeMs * static_cast<double>(frequency.QuadPart) / 1000.0);
		const long long targetEndTick = startCounter.QuadPart + waitTicks; // 转成 QPC tick，最后用忙等对齐。

		// 先粗略睡眠，最后约 1.5ms 使用忙等待收紧帧间隔。
		if (waitTimeMs > 2.0)
		{
			std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(waitTimeMs - 1.5));
		}
		do
		{
			QueryPerformanceCounter(&currentCounter);
			YieldProcessor(); // 短忙等阶段降低自旋对 CPU 的压力。
		} while (currentCounter.QuadPart < targetEndTick);
	}

	void LogFrameTiming(size_t committedIndex, size_t realPointCount, size_t predictedPointCount,
		size_t l0PointCount, double workMs, double previousFrameMs, bool idleFrozen)
	{
#if defined(_DEBUG)
		static double nextLogTimeMs = 0.0;
		const double nowMs = GetQpcTimeMilliseconds();
		if (nowMs < nextLogTimeMs) return;
		nextLogTimeMs = nowMs + 250.0; // Debug 最多每秒输出四次，避免逐帧日志扰动建模。
		const int logicFps = workMs > 0.001 ? static_cast<int>(1000.0 / workMs) : 0; // 只看本帧代码实际工作耗时。
		const int realFps = previousFrameMs > 0.001 ? static_cast<int>(1000.0 / previousFrameMs) : 0; // 包含等待后的真实帧间隔。
		char buffer[256] = {};
		const int length = std::snprintf(buffer, sizeof(buffer),
			"commit:%zu work:%.3fms logic:%d FPS prev-real:%d FPS(%.3fms) realPts:%zu predPts:%zu l0Pts:%zu frozen:%d\r\n",
			committedIndex, workMs, logicFps, realFps, previousFrameMs, realPointCount,
			predictedPointCount, l0PointCount, idleFrozen ? 1 : 0);
		if (length <= 0) return;
		WriteFastConsoleLine(buffer, static_cast<DWORD>(std::min(length, static_cast<int>(sizeof(buffer) - 1))));
#else
		(void)committedIndex;
		(void)realPointCount;
		(void)predictedPointCount;
		(void)l0PointCount;
		(void)workMs;
		(void)previousFrameMs;
		(void)idleFrozen;
#endif
	}

	void LogHResult(const char* step, HRESULT result)
	{
		std::cout << step << " failed. HRESULT=0x" << std::hex
			<< static_cast<unsigned long>(result) << std::dec << std::endl;
	}

	void LogWin32Error(const char* step, DWORD error)
	{
		std::cout << step << " failed. GetLastError=" << error << std::endl;
	}

	const char* BoolText(BOOL value)
	{
		return value ? "true" : "false";
	}

	std::string WideToUtf8(const WCHAR* text)
	{
		if (!text || text[0] == L'\0') return {};
		const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr); // 先查询 UTF-8 缓冲区大小。
		if (requiredSize <= 1) return {};
		std::string result(static_cast<size_t>(requiredSize), '\0');
		const int written = WideCharToMultiByte(
			CP_UTF8, 0, text, -1, result.data(), requiredSize, nullptr, nullptr);
		if (written != requiredSize) return {};
		result.resize(static_cast<size_t>(written - 1)); // 转换时保留 NUL 空间，返回前再移除。
		return result;
	}

	bool LogDwmColorizationState(const char* modeName, const char* stage, BOOL* outOpaqueBlend)
	{
		DWORD colorizationColor = 0;
		BOOL opaqueBlend = TRUE;
		const HRESULT result = DwmGetColorizationColor(&colorizationColor, &opaqueBlend);
		if (FAILED(result))
		{
			std::cout << "[" << modeName << "] " << stage
				<< " DwmGetColorizationColor failed. HRESULT=0x" << std::hex
				<< static_cast<unsigned long>(result) << std::dec << std::endl;
			return false;
		}
		std::cout << "[" << modeName << "] " << stage << " DwmColorizationColor=0x"
			<< std::hex << static_cast<unsigned long>(colorizationColor) << std::dec
			<< " opaqueBlend=" << BoolText(opaqueBlend) << std::endl;
		if (outOpaqueBlend) *outOpaqueBlend = opaqueBlend;
		return true;
	}

	void LogDwmModeDiagnostics(const char* modeName, HWND window, const char* stage)
	{
		if (!window) return;
		RECT windowRect = {};
		RECT clientRect = {};
		GetWindowRect(window, &windowRect); // 同时记录窗口和客户区，排查边框样式导致的尺寸偏差。
		GetClientRect(window, &clientRect);
		std::cout << "[" << modeName << "] " << stage
			<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
			<< " style=0x" << static_cast<unsigned long>(GetWindowLongPtr(window, GWL_STYLE))
			<< " exStyle=0x" << static_cast<unsigned long>(GetWindowLongPtr(window, GWL_EXSTYLE)) << std::dec
			<< " window=(" << windowRect.left << "," << windowRect.top << "," << windowRect.right << "," << windowRect.bottom << ")"
			<< " client=(" << clientRect.left << "," << clientRect.top << "," << clientRect.right << "," << clientRect.bottom << ")" << std::endl;

		BOOL compositionEnabled = FALSE;
		const HRESULT result = DwmIsCompositionEnabled(&compositionEnabled);
		if (SUCCEEDED(result))
		{
			std::cout << "[" << modeName << "] " << stage
				<< " DwmIsCompositionEnabled=" << BoolText(compositionEnabled) << std::endl;
		}
		else
		{
			LogHResult("DwmIsCompositionEnabled", result);
		}
		LogDwmColorizationState(modeName, stage);
	}

	void LogSwapChainDescription(const char* modeName, const char* stage, const DXGI_SWAP_CHAIN_DESC1& description)
	{
		std::cout << "[" << modeName << "] " << stage << " swapchain desc: size="
			<< description.Width << "x" << description.Height
			<< " format=" << DxgiFormatName(description.Format) << "(" << static_cast<unsigned int>(description.Format) << ")"
			<< " bufferCount=" << description.BufferCount
			<< " swapEffect=" << DxgiSwapEffectName(description.SwapEffect)
			<< " alphaMode=" << DxgiAlphaModeName(description.AlphaMode)
			<< " scaling=" << static_cast<unsigned int>(description.Scaling)
			<< " flags=0x" << std::hex << static_cast<unsigned int>(description.Flags) << std::dec << std::endl;
	}

	void LogSwapChainRuntimeDescription(const char* modeName, IDXGISwapChain1* swapChain, const char* stage)
	{
		if (!swapChain) return;
		DXGI_SWAP_CHAIN_DESC1 description = {};
		const HRESULT result = swapChain->GetDesc1(&description);
		if (FAILED(result))
		{
			LogHResult("IDXGISwapChain1::GetDesc1", result);
			return;
		}
		LogSwapChainDescription(modeName, stage, description);
	}

	void LogAdapterDiagnostics(IDXGIAdapter* adapter)
	{
		if (!adapter) return;
		DXGI_ADAPTER_DESC description = {};
		HRESULT result = adapter->GetDesc(&description);
		if (SUCCEEDED(result))
		{
			std::cout << "DXGI adapter: " << WideToUtf8(description.Description)
				<< " VendorId=0x" << std::hex << description.VendorId
				<< " DeviceId=0x" << description.DeviceId
				<< " SubSysId=0x" << description.SubSysId
				<< " Revision=0x" << description.Revision << std::dec
				<< " DedicatedVideoMemory=" << static_cast<unsigned long long>(description.DedicatedVideoMemory)
				<< " DedicatedSystemMemory=" << static_cast<unsigned long long>(description.DedicatedSystemMemory)
				<< " SharedSystemMemory=" << static_cast<unsigned long long>(description.SharedSystemMemory) << std::endl;
		}
		else
		{
			LogHResult("IDXGIAdapter::GetDesc", result);
		}

		LARGE_INTEGER driverVersion = {};
		result = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
		if (SUCCEEDED(result))
		{
			std::cout << "DXGI adapter UMD driver version raw: high=0x" << std::hex
				<< static_cast<unsigned long>(driverVersion.HighPart) << " low=0x"
				<< static_cast<unsigned long>(driverVersion.LowPart) << std::dec << std::endl;
		}
		else
		{
			LogHResult("IDXGIAdapter::CheckInterfaceSupport(IDXGIDevice)", result);
		}
	}
}
