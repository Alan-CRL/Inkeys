module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <windows.h>
#include <RTSCOM.h>
#include <RTSCOM_i.c>
#include <tchar.h>
#if defined(DRAW3_TESTING)
#include <thread>
#endif
#include <tpcshrd.h>
#include <wrl/client.h>

module draw3.realtime_stylus;

import draw3.diagnostics;
import draw3.ink_prediction;

namespace draw3
{
	namespace
	{
		constexpr size_t kContextDecoderCapacity = 32;
		constexpr size_t kActiveBindingSlotsPerBlock = 32;
		constexpr size_t kMaximumActiveBindingCapacity = 4096;
		constexpr uint32_t kRtsStateWriterBit = UINT32_C(0x80000000);
		constexpr uint32_t kRtsStateReaderMask = kRtsStateWriterBit - 1u;
		static_assert(std::atomic<uint32_t>::is_always_lock_free,
			"RTS state gate requires lock-free 32-bit atomics");
		constexpr ULONG kMaximumPacketPropertyCount = 256;
		constexpr float kUnknownStylusValue = -1.0f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kHalfPi = kPi * 0.5f;
		constexpr float kTwoPi = kPi * 2.0f;
		constexpr float kTiltLimit = kHalfPi - 0.0001f;
		const std::array<GUID, 9> kExtendedPacketProperties = {
			GUID_PACKETPROPERTY_GUID_X,
			GUID_PACKETPROPERTY_GUID_Y,
			GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE,
			GUID_PACKETPROPERTY_GUID_X_TILT_ORIENTATION,
			GUID_PACKETPROPERTY_GUID_Y_TILT_ORIENTATION,
			GUID_PACKETPROPERTY_GUID_AZIMUTH_ORIENTATION,
			GUID_PACKETPROPERTY_GUID_ALTITUDE_ORIENTATION,
			GUID_PACKETPROPERTY_GUID_WIDTH,
			GUID_PACKETPROPERTY_GUID_HEIGHT
		};
		const std::array<GUID, 2> kRequiredPacketProperties = {
			GUID_PACKETPROPERTY_GUID_X,
			GUID_PACKETPROPERTY_GUID_Y
		};
		constexpr RealTimeStylusDataInterest kProductionRtsDataInterest =
			static_cast<RealTimeStylusDataInterest>(
				RTSDI_RealTimeStylusEnabled | RTSDI_RealTimeStylusDisabled |
				RTSDI_StylusInRange | RTSDI_StylusOutOfRange | RTSDI_InAirPackets |
				RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp |
				RTSDI_TabletAdded | RTSDI_TabletRemoved | RTSDI_UpdateMapping | RTSDI_Error);
		struct PacketPropertyMetadata
		{
			PROPERTY_METRICS metrics = {};
			ULONG index = 0;
			bool present = false;
		};

		class RtsPacketStateGuard
		{
		public:
			explicit RtsPacketStateGuard(std::atomic<uint32_t>& state) noexcept
				: state_(state)
			{
				uint32_t expected = state_.load(std::memory_order_acquire);
				if ((expected & kRtsStateWriterBit) != 0 ||
					(expected & kRtsStateReaderMask) == kRtsStateReaderMask) return;
				entered_ = state_.compare_exchange_strong(expected, expected + 1u,
					std::memory_order_acquire, std::memory_order_relaxed);
			}

			~RtsPacketStateGuard()
			{
				if (entered_) state_.fetch_sub(1u, std::memory_order_release);
			}

			RtsPacketStateGuard(const RtsPacketStateGuard&) = delete;
			RtsPacketStateGuard& operator=(const RtsPacketStateGuard&) = delete;
			explicit operator bool() const noexcept { return entered_; }

		private:
			std::atomic<uint32_t>& state_;
			bool entered_ = false;
		};

		class RtsStateWriterGuard
		{
		public:
			RtsStateWriterGuard(std::mutex& writerMutex,
				std::atomic<uint32_t>& state) noexcept
				: writerLock_(writerMutex), state_(state)
			{
				state_.fetch_or(kRtsStateWriterBit, std::memory_order_acq_rel);
				while ((state_.load(std::memory_order_acquire) & kRtsStateReaderMask) != 0)
					YieldProcessor();
			}

			~RtsStateWriterGuard()
			{
				state_.store(0u, std::memory_order_release);
			}

			RtsStateWriterGuard(const RtsStateWriterGuard&) = delete;
			RtsStateWriterGuard& operator=(const RtsStateWriterGuard&) = delete;

		private:
			std::unique_lock<std::mutex> writerLock_;
			std::atomic<uint32_t>& state_;
		};

		struct DecodedStylusAngles
		{
			float tilt = kUnknownStylusValue;
			float orientation = kUnknownStylusValue;
		};

		float NormalizePressure(LONG rawValue, const PROPERTY_METRICS& metrics) noexcept
		{
			if (metrics.nLogicalMax <= metrics.nLogicalMin) return kUnknownStylusValue;
			const double range = static_cast<double>(metrics.nLogicalMax) - metrics.nLogicalMin;
			const double normalized = (static_cast<double>(rawValue) - metrics.nLogicalMin) / range;
			return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
		}

		bool DecodeAngle(LONG rawValue, const PROPERTY_METRICS& metrics, float& radians) noexcept
		{
			if (!std::isfinite(metrics.fResolution) || metrics.fResolution <= 0.0f) return false;
			const LONG clampedValue = metrics.nLogicalMax > metrics.nLogicalMin
				? std::clamp(rawValue, metrics.nLogicalMin, metrics.nLogicalMax) : rawValue;
			const float physicalValue = static_cast<float>(clampedValue) / metrics.fResolution;
			if (metrics.Units == PROPERTY_UNITS_DEGREES)
				radians = physicalValue * kPi / 180.0f;
			else if (metrics.Units == PROPERTY_UNITS_RADIANS)
				radians = physicalValue;
			else
				return false;
			return std::isfinite(radians);
		}

		float WrapOrientation(float angle) noexcept
		{
			angle = std::fmod(angle, kTwoPi);
			if (angle < 0.0f) angle += kTwoPi;
			return angle;
		}

		DecodedStylusAngles DecodeStylusAngles(bool hasAzimuthAltitude,
			float azimuth, float altitude, float xTilt, float yTilt) noexcept
		{
			DecodedStylusAngles result;
			if (hasAzimuthAltitude && std::isfinite(azimuth) && std::isfinite(altitude))
			{
				result.tilt = kHalfPi - std::clamp(altitude, 0.0f, kHalfPi);
				result.orientation = WrapOrientation(kTwoPi - azimuth);
				return result;
			}
			if (!std::isfinite(xTilt) || !std::isfinite(yTilt)) return result;
			const float xProjection = std::tan(std::clamp(xTilt, -kTiltLimit, kTiltLimit));
			const float yProjection = std::tan(std::clamp(yTilt, -kTiltLimit, kTiltLimit));
			result.tilt = std::clamp(std::atan(std::hypot(xProjection, yProjection)), 0.0f, kHalfPi);
			result.orientation = WrapOrientation(std::atan2(-yProjection, xProjection));
			return result;
		}

		SizeF DecodeContactSize(InputDeviceType deviceType, LONG rawWidth, LONG rawHeight,
			float packetScaleX, float packetScaleY) noexcept
		{
			SizeF result;
			if (deviceType != InputDeviceType::Touch || rawWidth <= 0 || rawHeight <= 0 ||
				!std::isfinite(packetScaleX) || !std::isfinite(packetScaleY) ||
				packetScaleX <= 0.0f || packetScaleY <= 0.0f)
				return result;
			const float width = static_cast<float>(rawWidth) * packetScaleX;
			const float height = static_cast<float>(rawHeight) * packetScaleY;
			if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f)
				return result;
			result.width = width;
			result.height = height;
			return result;
		}

		int64_t QueryQpc() noexcept
		{
			LARGE_INTEGER value = {};
			QueryPerformanceCounter(&value);
			return value.QuadPart;
		}

		template<bool Enabled>
		class InterruptedStrokeSimulation;

		template<>
		class InterruptedStrokeSimulation<false>
		{
		public:
			explicit InterruptedStrokeSimulation(ContactInputCoordinator&) noexcept {}
			bool PublishDown(uint32_t, uint32_t, InputDeviceType,
				const ContactSnapshot&) noexcept { return false; }
			bool PublishMove(uint32_t, uint32_t,
				const ContactSnapshot&) noexcept { return false; }
			bool PublishUp(uint32_t, uint32_t,
				const ContactSnapshot&) noexcept { return false; }
			void Reset() noexcept {}
		};

		template<>
		class InterruptedStrokeSimulation<true>
		{
			struct ContactState
			{
				bool occupied = false;
				bool dropping = false;
				uint32_t tabletContextId = 0;
				uint32_t physicalContactId = 0;
				uint32_t routedContactId = 0;
				InputDeviceType deviceType = InputDeviceType::Pen;
				int64_t interruptionUpQpc = 0;
				int64_t resumeQpc = 0;
				int64_t nextInterruptionQpc = 0;
				uint32_t requestedDropMilliseconds = 0;
			};

		public:
			explicit InterruptedStrokeSimulation(ContactInputCoordinator& coordinator) noexcept
				: coordinator_(coordinator)
			{
				LARGE_INTEGER frequency = {};
				if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
					qpcFrequency_ = frequency.QuadPart;
				const uint64_t seed = static_cast<uint64_t>(QueryQpc()) ^
					(static_cast<uint64_t>(GetCurrentThreadId()) << 32);
				randomState_ = static_cast<uint32_t>(seed ^ (seed >> 32));
				if (randomState_ == 0) randomState_ = 0x6d2b79f5u;
				std::cout << "[StrokeReconnectSimulation] enabled=true interval_ms=["
					<< kInterruptedStrokeReconnectSimulationMinimumIntervalMs << ","
					<< kInterruptedStrokeReconnectSimulationMaximumIntervalMs << "] drop_ms=["
					<< kInterruptedStrokeReconnectSimulationMinimumDropMs << ","
					<< kInterruptedStrokeReconnectSimulationMaximumDropMs << "]" << std::endl;
			}

			bool PublishDown(uint32_t tabletContextId, uint32_t contactId,
				InputDeviceType deviceType, const ContactSnapshot& snapshot)
			{
				std::lock_guard lock(mutex_);
				const bool published = coordinator_.PublishDown(
					tabletContextId, contactId, deviceType, snapshot);
				if (!published || qpcFrequency_ <= 0) return published;
				ContactState* state = AcquireState(tabletContextId, contactId);
				if (!state) return published;
				state->routedContactId = contactId;
				state->deviceType = deviceType;
				ScheduleNextInterruption(*state, snapshot.qpc);
				return true;
			}

			bool PublishMove(uint32_t tabletContextId, uint32_t contactId,
				const ContactSnapshot& snapshot) noexcept
			{
				std::lock_guard lock(mutex_);
				ContactState* state = FindState(tabletContextId, contactId);
				if (!state) return coordinator_.PublishMove(tabletContextId, contactId, snapshot);
				if (state->dropping)
				{
					if (snapshot.qpc < state->resumeQpc) return true;
					ContactSnapshot resumedDown = snapshot;
					resumedDown.phase = ContactPhase::Down;
					const uint32_t resumedContactId = NextSyntheticContactId();
					if (!coordinator_.PublishDown(tabletContextId, resumedContactId,
						state->deviceType, resumedDown))
					{
						return false;
					}
					state->routedContactId = resumedContactId;
					state->dropping = false;
					ScheduleNextInterruption(*state, snapshot.qpc);
					return true;
				}

				if (snapshot.qpc >= state->nextInterruptionQpc)
				{
					ContactSnapshot syntheticUp = snapshot;
					syntheticUp.phase = ContactPhase::Up;
					if (coordinator_.PublishUp(tabletContextId,
						state->routedContactId, syntheticUp))
					{
						state->requestedDropMilliseconds = RandomBetween(
							kInterruptedStrokeReconnectSimulationMinimumDropMs,
							kInterruptedStrokeReconnectSimulationMaximumDropMs);
						state->interruptionUpQpc = snapshot.qpc;
						state->resumeQpc = snapshot.qpc +
							MillisecondsToQpc(state->requestedDropMilliseconds);
						state->dropping = true;
						return true;
					}
					ScheduleNextInterruption(*state, snapshot.qpc);
				}
				return coordinator_.PublishMove(tabletContextId,
					state->routedContactId, snapshot);
			}

			bool PublishUp(uint32_t tabletContextId, uint32_t contactId,
				const ContactSnapshot& snapshot) noexcept
			{
				std::lock_guard lock(mutex_);
				ContactState* state = FindState(tabletContextId, contactId);
				if (!state) return coordinator_.PublishUp(tabletContextId, contactId, snapshot);
				bool published = true;
				if (!state->dropping)
					published = coordinator_.PublishUp(
						tabletContextId, state->routedContactId, snapshot);
				*state = {};
				return published;
			}

			void Reset() noexcept
			{
				std::lock_guard lock(mutex_);
				for (ContactState& state : contacts_) state = {};
			}

		private:
			ContactState* FindState(uint32_t tabletContextId, uint32_t physicalContactId) noexcept
			{
				for (ContactState& state : contacts_)
				{
					if (state.occupied && state.tabletContextId == tabletContextId &&
						state.physicalContactId == physicalContactId) return &state;
				}
				return nullptr;
			}

			ContactState* AcquireState(uint32_t tabletContextId, uint32_t physicalContactId) noexcept
			{
				if (ContactState* existing = FindState(tabletContextId, physicalContactId))
				{
					*existing = {};
					existing->occupied = true;
					existing->tabletContextId = tabletContextId;
					existing->physicalContactId = physicalContactId;
					return existing;
				}
				for (ContactState& state : contacts_)
				{
					if (state.occupied) continue;
					state = {};
					state.occupied = true;
					state.tabletContextId = tabletContextId;
					state.physicalContactId = physicalContactId;
					return &state;
				}
				return nullptr;
			}

			uint32_t NextRandom() noexcept
			{
				randomState_ ^= randomState_ << 13;
				randomState_ ^= randomState_ >> 17;
				randomState_ ^= randomState_ << 5;
				return randomState_;
			}

			uint32_t RandomBetween(uint32_t minimum, uint32_t maximum) noexcept
			{
				if (maximum <= minimum) return minimum;
				return minimum + NextRandom() % (maximum - minimum + 1);
			}

			int64_t MillisecondsToQpc(uint32_t milliseconds) const noexcept
			{
				return static_cast<int64_t>(std::max(1.0,
					static_cast<double>(qpcFrequency_) * milliseconds / 1000.0));
			}

			void ScheduleNextInterruption(ContactState& state, int64_t nowQpc) noexcept
			{
				const uint32_t intervalMilliseconds = RandomBetween(
					kInterruptedStrokeReconnectSimulationMinimumIntervalMs,
					kInterruptedStrokeReconnectSimulationMaximumIntervalMs);
				state.nextInterruptionQpc = nowQpc + MillisecondsToQpc(intervalMilliseconds);
			}

			uint32_t NextSyntheticContactId() noexcept
			{
				++syntheticContactSequence_;
				return 0x80000000u | (syntheticContactSequence_ & 0x7fffffffu);
			}

			static constexpr size_t kContactCapacity = 32;
			ContactInputCoordinator& coordinator_;
			std::mutex mutex_;
			std::array<ContactState, kContactCapacity> contacts_ = {};
			int64_t qpcFrequency_ = 0;
			uint32_t randomState_ = 0;
			uint32_t syntheticContactSequence_ = 0;
		};

		struct RtsContextDecoder
		{
			TABLET_CONTEXT_ID tabletContextId = 0;
			ULONG propertyCount = 0;
			ULONG xIndex = 0;
			ULONG yIndex = 1;
			PacketPropertyMetadata pressure;
			PacketPropertyMetadata xTilt;
			PacketPropertyMetadata yTilt;
			PacketPropertyMetadata azimuth;
			PacketPropertyMetadata altitude;
			PacketPropertyMetadata width;
			PacketPropertyMetadata height;
			float positionScaleX = 1.0f;
			float positionScaleY = 1.0f;
			float contactScaleX = 1.0f;
			float contactScaleY = 1.0f;
			InputDeviceType deviceType = InputDeviceType::Pen;
			uint64_t generation = 0;
			bool valid = false;
		};

		struct RtsActiveContactBinding
		{
			uint32_t tabletContextId = 0;
			uint32_t contactId = 0;
			size_t decoderSlotIndex = 0;
			uint64_t decoderGeneration = 0;
			bool occupied = false;
		};

		enum class RtsBindingInsertResult : uint8_t
		{
			Inserted,
			Duplicate,
			Full
		};

		size_t RoundUpActiveBindingCapacity(size_t requested) noexcept
		{
			requested = std::clamp(requested,
				kActiveBindingSlotsPerBlock, kMaximumActiveBindingCapacity);
			return (requested + kActiveBindingSlotsPerBlock - 1u) /
				kActiveBindingSlotsPerBlock * kActiveBindingSlotsPerBlock;
		}

		size_t ComputeActiveBindingCapacity(int maximumTouches) noexcept
		{
			const size_t nonNegativeTouches = static_cast<size_t>(std::max(0, maximumTouches));
			// 先按最终 4096 槽上限约束输入，避免 32-bit size_t 在乘 2 时回绕。
			constexpr size_t kMaximumRelevantTouches =
				kMaximumActiveBindingCapacity / 2u - 2u;
			const size_t boundedTouches = std::min(nonNegativeTouches, kMaximumRelevantTouches);
			return RoundUpActiveBindingCapacity(2u * (boundedTouches + 2u));
		}

		size_t ComputeDefaultActiveBindingCapacity() noexcept
		{
			return ComputeActiveBindingCapacity(GetSystemMetrics(SM_MAXIMUMTOUCHES));
		}

		size_t HashActiveContactKey(uint32_t tabletContextId, uint32_t contactId) noexcept
		{
			uint64_t value = (static_cast<uint64_t>(tabletContextId) << 32u) |
				static_cast<uint64_t>(contactId);
			value ^= value >> 33u;
			value *= UINT64_C(0xff51afd7ed558ccd);
			value ^= value >> 33u;
			value *= UINT64_C(0xc4ceb9fe1a85ec53);
			value ^= value >> 33u;
			return static_cast<size_t>(value);
		}

		class RtsActiveBindingTable
		{
		public:
			explicit RtsActiveBindingTable(size_t logicalCapacity) noexcept
				: logicalCapacity_(RoundUpActiveBindingCapacity(logicalCapacity))
			{
			}

			const RtsActiveContactBinding* Find(
				uint32_t tabletContextId, uint32_t contactId) const noexcept
			{
				const size_t start = HashActiveContactKey(
					tabletContextId, contactId) % logicalCapacity_;
				size_t index = start;
				for (size_t probe = 0; probe < logicalCapacity_; ++probe)
				{
					const RtsActiveContactBinding& binding = slots_[index];
					if (!binding.occupied) return nullptr;
					if (binding.tabletContextId == tabletContextId &&
						binding.contactId == contactId) return &binding;
					index = NextIndex(index);
				}
				return nullptr;
			}

			RtsBindingInsertResult Insert(const RtsActiveContactBinding& binding) noexcept
			{
				const RtsBindingInsertResult result = InsertWithoutCount(binding);
				if (result == RtsBindingInsertResult::Inserted) ++activeBindingCount_;
				return result;
			}

			bool Erase(uint32_t tabletContextId, uint32_t contactId) noexcept
			{
				size_t index = HashActiveContactKey(
					tabletContextId, contactId) % logicalCapacity_;
				for (size_t probe = 0; probe < logicalCapacity_; ++probe)
				{
					RtsActiveContactBinding& binding = slots_[index];
					if (!binding.occupied) return false;
					if (binding.tabletContextId == tabletContextId &&
						binding.contactId == contactId)
					{
						binding = {};
						--activeBindingCount_;
						return RepairClusterAfterErase(NextIndex(index));
					}
					index = NextIndex(index);
				}
				return false;
			}

			void Clear() noexcept
			{
				slots_.fill({});
				activeBindingCount_ = 0;
			}

			size_t LogicalCapacity() const noexcept { return logicalCapacity_; }
			size_t ActiveBindingCount() const noexcept { return activeBindingCount_; }

		private:
			size_t NextIndex(size_t index) const noexcept
			{
				const size_t next = index + 1u;
				return next == logicalCapacity_ ? 0u : next;
			}

			RtsBindingInsertResult InsertWithoutCount(
				const RtsActiveContactBinding& source) noexcept
			{
				size_t index = HashActiveContactKey(
					source.tabletContextId, source.contactId) % logicalCapacity_;
				for (size_t probe = 0; probe < logicalCapacity_; ++probe)
				{
					RtsActiveContactBinding& target = slots_[index];
					if (!target.occupied)
					{
						target = source;
						target.occupied = true;
						return RtsBindingInsertResult::Inserted;
					}
					if (target.tabletContextId == source.tabletContextId &&
						target.contactId == source.contactId)
						return RtsBindingInsertResult::Duplicate;
					index = NextIndex(index);
				}
				return RtsBindingInsertResult::Full;
			}

			bool RepairClusterAfterErase(size_t index) noexcept
			{
				const size_t maximumRepairCount = activeBindingCount_;
				for (size_t repaired = 0; repaired < maximumRepairCount; ++repaired)
				{
					RtsActiveContactBinding& source = slots_[index];
					if (!source.occupied) return true;
					const RtsActiveContactBinding displaced = source;
					source = {};
					// cluster 内部搬迁只修复 probe 链，不能重复修改 activeBindingCount_。
					if (InsertWithoutCount(displaced) != RtsBindingInsertResult::Inserted)
						return false;
					index = NextIndex(index);
				}
				// 满表删除前没有 EMPTY terminator；其余 capacity-1 个原槽处理完即修复完成。
				return true;
			}

			std::array<RtsActiveContactBinding, kMaximumActiveBindingCapacity> slots_ = {};
			size_t logicalCapacity_ = kActiveBindingSlotsPerBlock;
			size_t activeBindingCount_ = 0;
		};

		class RtsDecoderCache
		{
		public:
			void Reset() noexcept
			{
				decoders_.fill({});
				sharedPositionScaleX_ = 1.0f;
				sharedPositionScaleY_ = 1.0f;
				sharedPositionScaleValid_ = false;
				lifecycleEnabled_ = false;
				AdvanceGeneration();
			}

			void BeginLifecycle() noexcept
			{
				AdvanceGeneration();
				lifecycleEnabled_ = true;
			}

			bool PublishStaged(const std::array<RtsContextDecoder, kContextDecoderCapacity>& staged,
				size_t stagedCount, float positionScaleX, float positionScaleY) noexcept
			{
				if (!lifecycleEnabled_ || !ValidScale(positionScaleX, positionScaleY) ||
					stagedCount > decoders_.size()) return false;
				decoders_.fill({});
				sharedPositionScaleX_ = positionScaleX;
				sharedPositionScaleY_ = positionScaleY;
				sharedPositionScaleValid_ = true;
				for (size_t index = 0; index < stagedCount; ++index)
				{
					RtsContextDecoder decoder = staged[index];
					decoder.positionScaleX = positionScaleX;
					decoder.positionScaleY = positionScaleY;
					decoder.generation = generation_;
					decoder.valid = true;
					decoders_[index] = decoder;
				}
				return true;
			}

			bool PublishIncremental(const RtsContextDecoder& candidate) noexcept
			{
				if (!lifecycleEnabled_ || !sharedPositionScaleValid_) return false;
				if (FindSlot(candidate.tabletContextId) != kContextDecoderCapacity) return true;
				for (RtsContextDecoder& slot : decoders_)
				{
					if (slot.valid) continue;
					RtsContextDecoder decoder = candidate;
					decoder.positionScaleX = sharedPositionScaleX_;
					decoder.positionScaleY = sharedPositionScaleY_;
					decoder.generation = generation_;
					decoder.valid = true;
					slot = decoder;
					return true;
				}
				return false;
			}

			bool SetSharedPositionScale(float scaleX, float scaleY) noexcept
			{
				if (!lifecycleEnabled_ || !ValidScale(scaleX, scaleY)) return false;
				sharedPositionScaleX_ = scaleX;
				sharedPositionScaleY_ = scaleY;
				sharedPositionScaleValid_ = true;
				return true;
			}

			size_t FindSlot(TABLET_CONTEXT_ID tabletContextId) const noexcept
			{
				for (size_t index = 0; index < decoders_.size(); ++index)
				{
					const RtsContextDecoder& decoder = decoders_[index];
					if (decoder.valid && decoder.tabletContextId == tabletContextId) return index;
				}
				return kContextDecoderCapacity;
			}

			const RtsContextDecoder* DecoderAt(size_t index) const noexcept
			{
				return index < decoders_.size() && decoders_[index].valid
					? &decoders_[index] : nullptr;
			}

			const RtsContextDecoder* Resolve(const RtsActiveContactBinding& binding) const noexcept
			{
				const RtsContextDecoder* decoder = DecoderAt(binding.decoderSlotIndex);
				return decoder && decoder->generation == binding.decoderGeneration &&
					decoder->tabletContextId == binding.tabletContextId ? decoder : nullptr;
			}

			bool SharedPositionScaleValid() const noexcept { return sharedPositionScaleValid_; }
			float SharedPositionScaleX() const noexcept { return sharedPositionScaleX_; }
			float SharedPositionScaleY() const noexcept { return sharedPositionScaleY_; }
			uint64_t Generation() const noexcept { return generation_; }
			bool LifecycleEnabled() const noexcept { return lifecycleEnabled_; }

		private:
			static bool ValidScale(float scaleX, float scaleY) noexcept
			{
				return std::isfinite(scaleX) && std::isfinite(scaleY) &&
					scaleX > 0.0f && scaleY > 0.0f;
			}

			void AdvanceGeneration() noexcept
			{
				++generation_;
				if (generation_ == 0) ++generation_;
			}

			std::array<RtsContextDecoder, kContextDecoderCapacity> decoders_ = {};
			float sharedPositionScaleX_ = 1.0f;
			float sharedPositionScaleY_ = 1.0f;
			uint64_t generation_ = 0;
			bool sharedPositionScaleValid_ = false;
			bool lifecycleEnabled_ = false;
		};

		const RtsContextDecoder* FindCachedContextDecoder(
			const RtsDecoderCache& cache, TABLET_CONTEXT_ID contextId) noexcept
		{
			const size_t slot = cache.FindSlot(contextId);
			return slot == kContextDecoderCapacity ? nullptr : cache.DecoderAt(slot);
		}

		void ClearActiveBindingsAndCloseContacts(RtsActiveBindingTable& activeBindings,
			ContactInputCoordinator& coordinator, int64_t qpc) noexcept
		{
			activeBindings.Clear();
			coordinator.CloseAllProducerContacts(qpc);
		}

		bool PopulateContextDecoderProperties(const PACKET_PROPERTY* properties,
			ULONG propertyCount, RtsContextDecoder& decoder) noexcept
		{
			if (!properties || propertyCount < 2 || propertyCount > kMaximumPacketPropertyCount)
				return false;
			bool hasX = false;
			bool hasY = false;
			const auto captureProperty = [&](PacketPropertyMetadata& target, ULONG index)
				{
					target.metrics = properties[index].PropertyMetrics;
					target.index = index;
					target.present = true;
				};
			for (ULONG index = 0; index < propertyCount; ++index)
			{
				if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_X))
				{
					decoder.xIndex = index;
					hasX = true;
				}
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_Y))
				{
					decoder.yIndex = index;
					hasY = true;
				}
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE))
					captureProperty(decoder.pressure, index);
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_X_TILT_ORIENTATION))
					captureProperty(decoder.xTilt, index);
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_Y_TILT_ORIENTATION))
					captureProperty(decoder.yTilt, index);
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_AZIMUTH_ORIENTATION))
					captureProperty(decoder.azimuth, index);
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_ALTITUDE_ORIENTATION))
					captureProperty(decoder.altitude, index);
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_WIDTH))
					captureProperty(decoder.width, index);
				else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_HEIGHT))
					captureProperty(decoder.height, index);
			}
			decoder.propertyCount = propertyCount;
			return hasX && hasY;
		}

		bool BuildContextDecoder(IRealTimeStylus* source, TABLET_CONTEXT_ID contextId,
			IInkTablet* suppliedTablet, RtsContextDecoder& candidate)
		{
			if (!source) return false;
			FLOAT contactScaleX = 1.0f;
			FLOAT contactScaleY = 1.0f;
			ULONG propertyCount = 0;
			PACKET_PROPERTY* properties = nullptr;
			const HRESULT packetResult = source->GetPacketDescriptionData(contextId,
				&contactScaleX, &contactScaleY, &propertyCount, &properties);
			if (FAILED(packetResult) || !std::isfinite(contactScaleX) ||
				!std::isfinite(contactScaleY) || contactScaleX <= 0.0f || contactScaleY <= 0.0f)
			{
				CoTaskMemFree(properties);
				return false;
			}

			RtsContextDecoder decoder;
			const bool propertiesValid = PopulateContextDecoderProperties(
				properties, propertyCount, decoder);
			CoTaskMemFree(properties);
			if (!propertiesValid) return false;

			Microsoft::WRL::ComPtr<IInkTablet> tablet;
			if (suppliedTablet)
				tablet = suppliedTablet;
			else
				source->GetTabletFromTabletContextId(contextId, tablet.ReleaseAndGetAddressOf());
			InputDeviceType deviceType = InputDeviceType::Pen;
			if (tablet)
			{
				Microsoft::WRL::ComPtr<IInkTablet2> tablet2;
				if (SUCCEEDED(tablet.As(&tablet2)))
				{
					TabletDeviceKind kind = TDK_Pen;
					if (SUCCEEDED(tablet2->get_DeviceKind(&kind)))
					{
						if (kind == TDK_Touch) deviceType = InputDeviceType::Touch;
						else if (kind == TDK_Mouse) deviceType = InputDeviceType::MouseLeft;
					}
				}
			}

			decoder.tabletContextId = contextId;
			decoder.contactScaleX = contactScaleX;
			decoder.contactScaleY = contactScaleY;
			decoder.deviceType = deviceType;
			candidate = decoder;
			return true;
		}

		bool DecodeSnapshot(const RtsContextDecoder& decoder, ULONG propertyCount,
			const LONG* packet, ContactPhase phase, int64_t qpc,
			ContactSnapshot& snapshot) noexcept
		{
			if (!decoder.valid || !packet || propertyCount != decoder.propertyCount) return false;
			if (decoder.xIndex >= propertyCount || decoder.yIndex >= propertyCount) return false;
			snapshot.position.x = static_cast<float>(packet[decoder.xIndex]) * decoder.positionScaleX;
			snapshot.position.y = static_cast<float>(packet[decoder.yIndex]) * decoder.positionScaleY;
			snapshot.pressure = kUnknownStylusValue;
			snapshot.tilt = kUnknownStylusValue;
			snapshot.orientation = kUnknownStylusValue;
			if (decoder.deviceType == InputDeviceType::Pen)
			{
				if (decoder.pressure.present && decoder.pressure.index < propertyCount)
					snapshot.pressure = NormalizePressure(
						packet[decoder.pressure.index], decoder.pressure.metrics);

				float azimuth = 0.0f;
				float altitude = 0.0f;
				const bool hasAzimuthAltitude = decoder.azimuth.present && decoder.altitude.present &&
					decoder.azimuth.index < propertyCount && decoder.altitude.index < propertyCount &&
					DecodeAngle(packet[decoder.azimuth.index], decoder.azimuth.metrics, azimuth) &&
					DecodeAngle(packet[decoder.altitude.index], decoder.altitude.metrics, altitude);
				float xTilt = 0.0f;
				float yTilt = 0.0f;
				const bool hasXyTilt = decoder.xTilt.present && decoder.yTilt.present &&
					decoder.xTilt.index < propertyCount && decoder.yTilt.index < propertyCount &&
					DecodeAngle(packet[decoder.xTilt.index], decoder.xTilt.metrics, xTilt) &&
					DecodeAngle(packet[decoder.yTilt.index], decoder.yTilt.metrics, yTilt);
				const DecodedStylusAngles angles = DecodeStylusAngles(hasAzimuthAltitude,
					azimuth, altitude, hasXyTilt ? xTilt : NAN, hasXyTilt ? yTilt : NAN);
				snapshot.tilt = angles.tilt;
				snapshot.orientation = angles.orientation;
			}
			snapshot.contactSize = {};
			if (decoder.width.present && decoder.height.present &&
				decoder.width.index < propertyCount && decoder.height.index < propertyCount)
			{
				// 接触面积保留各 context 的比例；位置统一使用 lifecycle 的首 context 比例。
				snapshot.contactSize = DecodeContactSize(decoder.deviceType,
					packet[decoder.width.index], packet[decoder.height.index],
					decoder.contactScaleX, decoder.contactScaleY);
			}
			snapshot.qpc = qpc;
			snapshot.phase = phase;
			return std::isfinite(snapshot.position.x) && std::isfinite(snapshot.position.y);
		}

		class StylusSyncPlugin final : public IStylusSyncPlugin
		{
		public:
			StylusSyncPlugin(ContactInputCoordinator& coordinator,
				DrawingCursorEventSink* drawingCursorSink)
				: coordinator_(coordinator), interruptionSimulation_(coordinator),
				drawingCursorSink_(drawingCursorSink),
				activeBindings_(ComputeDefaultActiveBindingCapacity())
			{
				marshalerResult_ = CoCreateFreeThreadedMarshaler(
					static_cast<IUnknown*>(this), freeThreadedMarshaler_.ReleaseAndGetAddressOf());
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
			{
				if (!object) return E_POINTER;
				*object = nullptr;
				if (iid == __uuidof(IMarshal) && freeThreadedMarshaler_)
					return freeThreadedMarshaler_->QueryInterface(iid, object);
				if (iid == __uuidof(IUnknown) || iid == __uuidof(IStylusPlugin) ||
					iid == __uuidof(IStylusSyncPlugin))
				{
					*object = static_cast<IStylusSyncPlugin*>(this);
					AddRef();
					return S_OK;
				}
				return E_NOINTERFACE;
			}

			HRESULT MarshalerResult() const noexcept
			{
				return marshalerResult_;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return referenceCount_.fetch_add(1, std::memory_order_relaxed) + 1;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG count = referenceCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (count == 0) delete this;
				return count;
			}

			HRESULT STDMETHODCALLTYPE RealTimeStylusEnabled(IRealTimeStylus* source,
				ULONG contextCount, const TABLET_CONTEXT_ID* contextIds) override
			{
				if (!source || (contextCount > 0 && !contextIds)) return E_INVALIDARG;
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("RealTimeStylusEnabled", nullptr, nullptr, contextCount, 0,
					nullptr, nullptr, true, false);
#endif
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				ResetDecoderLifecycleState(true);
				decoderCache_.BeginLifecycle();
				StageAndPublishDecoders(source, contextCount, contextIds);
				// 单个 tablet 暂时无法查询时不让插件进入 Error；InRange/Down 可低频补建。
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE RealTimeStylusDisabled(IRealTimeStylus*,
				ULONG, const TABLET_CONTEXT_ID*) override
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("RealTimeStylusDisabled", nullptr, nullptr, 0, 0,
					nullptr, nullptr, true, false);
#endif
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				ResetDecoderLifecycleState(true);
				PublishDefaultPenCursor();
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusInRange(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, STYLUS_ID) override
			{
				if (source)
				{
					RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
					// InRange 不含倒转信息；等待首个 InAir/Pointer 包决定普通笔或笔尾。
					EnsureContextDecoder(source, contextId, nullptr);
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusOutOfRange(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) override
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("StylusOutOfRange", nullptr, nullptr, 0, 0,
					nullptr, nullptr, true, false);
#endif
				PublishDefaultPenCursor();
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusDown(IRealTimeStylus* source, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				if (!source || !stylusInfo || !packet) return E_INVALIDARG;
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				size_t decoderSlotIndex = kContextDecoderCapacity;
				const RtsContextDecoder* decoder = ResolveContextDecoder(
					source, stylusInfo->tcid, nullptr, decoderSlotIndex);
				if (!decoder)
				{
					PublishDefaultPenCursor();
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("StylusDown", stylusInfo, nullptr, 1, propertyCount,
						packet, nullptr, false, false);
#endif
					return S_OK;
				}

				if (activeBindings_.Find(stylusInfo->tcid, stylusInfo->cid))
				{
					// duplicate Down 先关闭旧 producer，再修复 cluster 并绑定当前 contact。
					CloseProducerContact(stylusInfo->tcid, stylusInfo->cid, QueryQpc());
					activeBindings_.Erase(stylusInfo->tcid, stylusInfo->cid);
				}
				const RtsActiveContactBinding binding{
					stylusInfo->tcid, stylusInfo->cid, decoderSlotIndex, decoder->generation, true };
				if (activeBindings_.Insert(binding) != RtsBindingInsertResult::Inserted)
				{
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("StylusDown", stylusInfo, decoder, 1, propertyCount,
						packet, nullptr, false, false, E_OUTOFMEMORY);
#endif
					return E_OUTOFMEMORY;
				}

				ContactSnapshot snapshot;
				if (!DecodeSnapshot(*decoder, propertyCount, packet,
					ContactPhase::Down, QueryQpc(), snapshot))
				{
					activeBindings_.Erase(stylusInfo->tcid, stylusInfo->cid);
					PublishDefaultPenCursor(); // 解码失败时不能把旧 Hover visual 留在接触位置。
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("StylusDown", stylusInfo, decoder, 1, propertyCount,
						packet, nullptr, false, false);
#endif
					return S_OK;
				}
				snapshot.isInvertedCursor = decoder->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishPenCursor(decoder, stylusInfo, true, snapshot);
				InputDeviceType deviceType = decoder->deviceType;
				if (deviceType == InputDeviceType::MouseLeft)
				{
					const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
					const bool rightButtonDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
					deviceType = rightButtonDown && !leftButtonDown
						? InputDeviceType::MouseRight : InputDeviceType::MouseLeft;
				}
				bool published = false;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					published = interruptionSimulation_.PublishDown(
						stylusInfo->tcid, stylusInfo->cid, deviceType, snapshot);
				else
					published = coordinator_.PublishDown(
						stylusInfo->tcid, stylusInfo->cid, deviceType, snapshot);
				if (!published) activeBindings_.Erase(stylusInfo->tcid, stylusInfo->cid);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("StylusDown", stylusInfo, decoder, 1, propertyCount,
					packet, &snapshot, true, published, S_OK, 0, deviceType, true);
#endif
				return published ? S_OK : E_OUTOFMEMORY;
			}

			HRESULT STDMETHODCALLTYPE StylusUp(IRealTimeStylus*, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				if (!stylusInfo) return E_INVALIDARG;
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				if (!packet)
				{
					PublishDefaultPenCursor();
					CloseProducerContact(stylusInfo->tcid, stylusInfo->cid, QueryQpc());
					activeBindings_.Erase(stylusInfo->tcid, stylusInfo->cid);
					return S_OK;
				}
				const RtsActiveContactBinding* binding = activeBindings_.Find(
					stylusInfo->tcid, stylusInfo->cid);
				const RtsContextDecoder* decoder = binding ? decoderCache_.Resolve(*binding) : nullptr;
				ContactSnapshot snapshot;
				if (!decoder || !DecodeSnapshot(*decoder, propertyCount, packet,
					ContactPhase::Up, QueryQpc(), snapshot))
				{
					PublishDefaultPenCursor();
					// 坏 Up 包不能把 contact 永久留在 Producing；协调器会沿用最后有效位置闭合。
					const bool published = CloseProducerContact(
						stylusInfo->tcid, stylusInfo->cid, QueryQpc());
					activeBindings_.Erase(stylusInfo->tcid, stylusInfo->cid);
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("StylusUp", stylusInfo, decoder, 1, propertyCount,
						packet, nullptr, false, published);
#endif
					return S_OK;
				}
				snapshot.isInvertedCursor = decoder->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishDefaultPenCursor(); // Up 只清除接触光标，后续 InAir/Pointer 样本再恢复真实 Hover。
				bool published = false;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					published = interruptionSimulation_.PublishUp(
						stylusInfo->tcid, stylusInfo->cid, snapshot);
				else
					published = coordinator_.PublishUp(stylusInfo->tcid, stylusInfo->cid, snapshot);
				activeBindings_.Erase(stylusInfo->tcid, stylusInfo->cid);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("StylusUp", stylusInfo, decoder, 1, propertyCount,
					packet, &snapshot, true, published);
#endif
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusButtonDown(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusButtonUp(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE InAirPackets(IRealTimeStylus*,
				const StylusInfo* stylusInfo, ULONG packetCount,
				ULONG packetBufferLength, LONG* packets, ULONG*, LONG**) override
			{
				if (!stylusInfo || !packets || packetCount == 0 || packetBufferLength < packetCount ||
					packetBufferLength % packetCount != 0) return E_INVALIDARG;
				const ULONG propertyCount = packetBufferLength / packetCount;
				const LONG* lastPacket = packets +
					static_cast<size_t>(packetCount - 1) * propertyCount;
				RtsPacketStateGuard stateAccess(stateGate_);
				const RtsContextDecoder* decoder = stateAccess
					? FindCachedContextDecoder(decoderCache_, stylusInfo->tcid) : nullptr;
				ContactSnapshot snapshot;
				const bool decoded = decoder && DecodeSnapshot(*decoder, propertyCount, lastPacket,
					ContactPhase::Move, QueryQpc(), snapshot);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("InAirPackets", stylusInfo, decoder, packetCount, propertyCount,
					lastPacket, decoded ? &snapshot : nullptr, decoded, false);
#endif
				if (!decoded) return S_OK;
				snapshot.isInvertedCursor = decoder->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishPenCursor(decoder, stylusInfo, false, snapshot);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Packets(IRealTimeStylus*, const StylusInfo* stylusInfo,
				ULONG packetCount, ULONG packetBufferLength, LONG* packets, ULONG*, LONG**) override
			{
				if (!stylusInfo || !packets || packetCount == 0 || packetBufferLength < packetCount ||
					packetBufferLength % packetCount != 0) return E_INVALIDARG;
				const ULONG propertyCount = packetBufferLength / packetCount;
				const LONG* lastPacket = packets + static_cast<size_t>(packetCount - 1) * propertyCount;
				RtsPacketStateGuard stateAccess(stateGate_);
				if (!stateAccess)
				{
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("Packets", stylusInfo, nullptr, packetCount, propertyCount,
						lastPacket, nullptr, false, false);
#endif
					return S_OK;
				}
				const RtsActiveContactBinding* binding = activeBindings_.Find(
					stylusInfo->tcid, stylusInfo->cid);
				const RtsContextDecoder* decoder = binding ? decoderCache_.Resolve(*binding) : nullptr;
				ContactSnapshot snapshot;
				if (!decoder || !DecodeSnapshot(*decoder, propertyCount, lastPacket,
					ContactPhase::Move, QueryQpc(), snapshot))
				{
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("Packets", stylusInfo, decoder, packetCount, propertyCount,
						lastPacket, nullptr, false, false);
#endif
					return S_OK;
				}
				snapshot.isInvertedCursor = decoder->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishPenCursor(decoder, stylusInfo, true, snapshot);
				bool published = false;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					published = interruptionSimulation_.PublishMove(
						stylusInfo->tcid, stylusInfo->cid, snapshot);
				else
					published = coordinator_.PublishMove(stylusInfo->tcid, stylusInfo->cid, snapshot);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("Packets", stylusInfo, decoder, packetCount, propertyCount,
					lastPacket, &snapshot, true, published);
#endif
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE CustomStylusDataAdded(IRealTimeStylus*, const GUID*,
				ULONG, const BYTE*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SystemEvent(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID,
				SYSTEM_EVENT, SYSTEM_EVENT_DATA) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE TabletAdded(IRealTimeStylus* source, IInkTablet* tablet) override
			{
				if (!source || !tablet) return E_INVALIDARG;
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				TABLET_CONTEXT_ID contextId = 0;
				const HRESULT result = source->GetTabletContextIdFromTablet(tablet, &contextId);
				bool published = false;
				if (SUCCEEDED(result))
				{
					RtsContextDecoder candidate;
					published = BuildContextDecoder(source, contextId, tablet, candidate) &&
						decoderCache_.PublishIncremental(candidate);
				}
				if (!published)
				{
					// 增量查询失败时从空 generation 全量重建，避免新旧 context 混合发布。
					ResetDecoderLifecycleState(true);
					RebuildCurrentContextDecoders(source);
				}
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("TabletAdded", nullptr, nullptr, 0, 0,
					nullptr, nullptr, published, false, result);
#endif
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE TabletRemoved(IRealTimeStylus* source, LONG) override
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("TabletRemoved", nullptr, nullptr, 0, 0,
					nullptr, nullptr, true, false);
#endif
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				PublishDefaultPenCursor();
				ResetDecoderLifecycleState(true);
				if (source) RebuildCurrentContextDecoders(source);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Error(IRealTimeStylus*, IStylusPlugin*, RealTimeStylusDataInterest dataInterest,
				HRESULT errorCode, LONG_PTR*) override
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("Error", nullptr, nullptr, 0, 0,
					nullptr, nullptr, false, false, errorCode,
					static_cast<uint32_t>(dataInterest));
#else
				(void)dataInterest;
				(void)errorCode;
#endif
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				PublishDefaultPenCursor();
				// Error 只终止当前 contact；已发布 decoder lifecycle 仍可继续解码后续输入。
				ResetActiveContactState(true);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE UpdateMapping(IRealTimeStylus* source) override
			{
				if (!source) return E_INVALIDARG;
				RtsStateWriterGuard stateWriter(stateWriterMutex_, stateGate_);
				PublishDefaultPenCursor();
				ResetDecoderLifecycleState(true);
				RebuildCurrentContextDecoders(source);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE DataInterest(RealTimeStylusDataInterest* interest) override
			{
				if (!interest) return E_POINTER;
				*interest = kProductionRtsDataInterest;
				return S_OK;
			}

		private:
#if defined(DRAW3_RTS_DIAGNOSTICS)
			void RecordCallback(const char* eventName, const StylusInfo* stylusInfo,
				const RtsContextDecoder* decoder, ULONG packetCount, ULONG propertyCount,
				const LONG* packet, const ContactSnapshot* snapshot, bool decoded,
				bool published, HRESULT result = S_OK, uint32_t dataInterest = 0,
				InputDeviceType routedDeviceType = InputDeviceType::Pen,
				bool overrideDeviceType = false) noexcept
			{
				RtsCallbackTrace trace;
				trace.eventName = eventName;
				trace.qpc = snapshot ? snapshot->qpc : QueryQpc();
				trace.threadId = GetCurrentThreadId();
				trace.tabletContextId = stylusInfo ? stylusInfo->tcid : 0;
				trace.contactId = stylusInfo ? stylusInfo->cid : 0;
				trace.deviceType = static_cast<uint32_t>(overrideDeviceType || !decoder
					? routedDeviceType : decoder->deviceType);
				trace.packetCount = packetCount;
				trace.propertyCount = propertyCount;
				trace.decoded = decoded;
				trace.published = published;
				trace.result = result;
				trace.dataInterest = dataInterest;
				if (snapshot)
				{
					trace.decodedX = snapshot->position.x;
					trace.decodedY = snapshot->position.y;
				}
				if (decoder && packet && decoder->xIndex < propertyCount &&
					decoder->yIndex < propertyCount)
				{
					trace.hasRawPosition = true;
					trace.rawX = packet[decoder->xIndex];
					trace.rawY = packet[decoder->yIndex];
				}
				RecordRtsCallback(trace);
			}
#endif

			void PublishPenCursor(const RtsContextDecoder* decoder,
				const StylusInfo* stylusInfo, bool inContact,
				const ContactSnapshot& snapshot) noexcept
			{
				if (!drawingCursorSink_ || !decoder || !stylusInfo ||
					decoder->deviceType != InputDeviceType::Pen) return;
				DrawingCursorSample sample;
				sample.x = snapshot.position.x;
				sample.y = snapshot.position.y;
				sample.qpc = snapshot.qpc;
				sample.valid = true;
				sample.inverted = stylusInfo->bIsInvertedCursor != FALSE;
				sample.inContact = inContact;
				drawingCursorSink_->PublishPenCursorSample(sample);
			}

			void PublishDefaultPenCursor() noexcept
			{
				if (drawingCursorSink_) drawingCursorSink_->ClearPenCursorSample();
			}

			bool CloseProducerContact(uint32_t tabletContextId,
				uint32_t contactId, int64_t qpc) noexcept
			{
				ContactSnapshot snapshot;
				snapshot.position = { NAN, NAN };
				snapshot.qpc = qpc;
				snapshot.phase = ContactPhase::Cancelled;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					return interruptionSimulation_.PublishUp(tabletContextId, contactId, snapshot);
				else
					return coordinator_.PublishCancelled(tabletContextId, contactId, snapshot);
			}

			void ResetActiveContactState(bool closeProducerContacts) noexcept
			{
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					interruptionSimulation_.Reset();
				if (closeProducerContacts)
					ClearActiveBindingsAndCloseContacts(
						activeBindings_, coordinator_, QueryQpc());
				else
					activeBindings_.Clear();
			}

			void ResetDecoderLifecycleState(bool closeProducerContacts) noexcept
			{
				ResetActiveContactState(closeProducerContacts);
				decoderCache_.Reset();
			}

			bool QueryCurrentContextIds(IRealTimeStylus* source,
				std::array<TABLET_CONTEXT_ID, kContextDecoderCapacity>& contextIds,
				size_t& contextCount) const noexcept
			{
				contextCount = 0;
				if (!source) return false;
				ULONG queriedCount = 0;
				TABLET_CONTEXT_ID* queriedIds = nullptr;
				const HRESULT result = source->GetAllTabletContextIds(&queriedCount, &queriedIds);
				if (FAILED(result) || (queriedCount > 0 && !queriedIds))
				{
					CoTaskMemFree(queriedIds);
					return false;
				}
				if (static_cast<size_t>(queriedCount) > contextIds.size())
				{
					CoTaskMemFree(queriedIds);
					return false;
				}
				contextCount = static_cast<size_t>(queriedCount);
				for (size_t index = 0; index < contextCount; ++index)
					contextIds[index] = queriedIds[index];
				CoTaskMemFree(queriedIds);
				return true;
			}

			bool QueryPositionScaleForContext(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, float& scaleX, float& scaleY) const noexcept
			{
				FLOAT queriedScaleX = 1.0f;
				FLOAT queriedScaleY = 1.0f;
				ULONG propertyCount = 0;
				PACKET_PROPERTY* properties = nullptr;
				const HRESULT result = source->GetPacketDescriptionData(contextId,
					&queriedScaleX, &queriedScaleY, &propertyCount, &properties);
				CoTaskMemFree(properties);
				if (FAILED(result) || !std::isfinite(queriedScaleX) ||
					!std::isfinite(queriedScaleY) || queriedScaleX <= 0.0f || queriedScaleY <= 0.0f)
					return false;
				scaleX = queriedScaleX;
				scaleY = queriedScaleY;
				return true;
			}

			bool QuerySharedPositionScale(IRealTimeStylus* source,
				float& scaleX, float& scaleY) const noexcept
			{
				std::array<TABLET_CONTEXT_ID, kContextDecoderCapacity> contextIds = {};
				size_t contextCount = 0;
				return QueryCurrentContextIds(source, contextIds, contextCount) && contextCount > 0 &&
					QueryPositionScaleForContext(source, contextIds[0], scaleX, scaleY);
			}

			bool BuildStagedDecoders(IRealTimeStylus* source, size_t contextCount,
				const TABLET_CONTEXT_ID* contextIds,
				std::array<RtsContextDecoder, kContextDecoderCapacity>& staged,
				size_t& stagedCount) const
			{
				stagedCount = 0;
				if (!source || (contextCount > 0 && !contextIds)) return false;
				if (contextCount > staged.size()) return false;
				for (size_t contextIndex = 0; contextIndex < contextCount; ++contextIndex)
				{
					bool duplicate = false;
					for (size_t stagedIndex = 0; stagedIndex < stagedCount; ++stagedIndex)
					{
						duplicate = duplicate ||
							staged[stagedIndex].tabletContextId == contextIds[contextIndex];
					}
					if (duplicate) continue;
					RtsContextDecoder candidate;
					if (!BuildContextDecoder(source, contextIds[contextIndex], nullptr, candidate))
						return false;
					staged[stagedCount++] = candidate;
				}
				return true;
			}

			bool StageAndPublishDecoders(IRealTimeStylus* source, size_t contextCount,
				const TABLET_CONTEXT_ID* contextIds)
			{
				float positionScaleX = 1.0f;
				float positionScaleY = 1.0f;
				if (!QuerySharedPositionScale(source, positionScaleX, positionScaleY)) return false;
				std::array<RtsContextDecoder, kContextDecoderCapacity> staged = {};
				size_t stagedCount = 0;
				if (!BuildStagedDecoders(source, contextCount, contextIds, staged, stagedCount))
					return false;
				return decoderCache_.PublishStaged(
					staged, stagedCount, positionScaleX, positionScaleY);
			}

			bool RebuildCurrentContextDecoders(IRealTimeStylus* source)
			{
				decoderCache_.BeginLifecycle();
				std::array<TABLET_CONTEXT_ID, kContextDecoderCapacity> contextIds = {};
				size_t contextCount = 0;
				if (!QueryCurrentContextIds(source, contextIds, contextCount)) return false;
				if (contextCount == 0) return true;
				float positionScaleX = 1.0f;
				float positionScaleY = 1.0f;
				if (!QueryPositionScaleForContext(
					source, contextIds[0], positionScaleX, positionScaleY)) return false;
				std::array<RtsContextDecoder, kContextDecoderCapacity> staged = {};
				size_t stagedCount = 0;
				if (!BuildStagedDecoders(
					source, contextCount, contextIds.data(), staged, stagedCount)) return false;
				return decoderCache_.PublishStaged(
					staged, stagedCount, positionScaleX, positionScaleY);
			}

			const RtsContextDecoder* EnsureContextDecoder(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, IInkTablet* suppliedTablet)
			{
				const size_t existingSlot = decoderCache_.FindSlot(contextId);
				if (existingSlot != kContextDecoderCapacity)
					return decoderCache_.DecoderAt(existingSlot);
				if (!source || !decoderCache_.LifecycleEnabled()) return nullptr;
				if (!decoderCache_.SharedPositionScaleValid())
				{
					float positionScaleX = 1.0f;
					float positionScaleY = 1.0f;
					if (!QuerySharedPositionScale(source, positionScaleX, positionScaleY) ||
						!decoderCache_.SetSharedPositionScale(positionScaleX, positionScaleY))
						return nullptr;
				}
				RtsContextDecoder candidate;
				if (!BuildContextDecoder(source, contextId, suppliedTablet, candidate) ||
					!decoderCache_.PublishIncremental(candidate)) return nullptr;
				const size_t slot = decoderCache_.FindSlot(contextId);
				return slot == kContextDecoderCapacity ? nullptr : decoderCache_.DecoderAt(slot);
			}

			const RtsContextDecoder* ResolveContextDecoder(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, IInkTablet* suppliedTablet,
				size_t& decoderSlotIndex)
			{
				const RtsContextDecoder* decoder = EnsureContextDecoder(
					source, contextId, suppliedTablet);
				decoderSlotIndex = decoder ? decoderCache_.FindSlot(contextId) : kContextDecoderCapacity;
				return decoderSlotIndex == kContextDecoderCapacity ? nullptr : decoder;
			}

			std::atomic<ULONG> referenceCount_ = 1;
			ContactInputCoordinator& coordinator_;
			[[no_unique_address]] InterruptedStrokeSimulation<
				kInterruptedStrokeReconnectSimulationEnabled> interruptionSimulation_;
			DrawingCursorEventSink* drawingCursorSink_ = nullptr;
			Microsoft::WRL::ComPtr<IUnknown> freeThreadedMarshaler_;
			HRESULT marshalerResult_ = E_UNEXPECTED;
			// lifecycle/Down/Up 串行写普通数组；只有 Packets/InAir 做一次无等待只读尝试。
			std::mutex stateWriterMutex_;
			std::atomic<uint32_t> stateGate_ = 0;
			RtsDecoderCache decoderCache_;
			RtsActiveBindingTable activeBindings_;
		};
	}

#if defined(DRAW3_TESTING)
	float NormalizeRtsPressureForTesting(int32_t rawValue,
		int32_t logicalMin, int32_t logicalMax) noexcept
	{
		PROPERTY_METRICS metrics = {};
		metrics.nLogicalMin = logicalMin;
		metrics.nLogicalMax = logicalMax;
		return NormalizePressure(rawValue, metrics);
	}

	float DecodeRtsAngleForTesting(int32_t rawValue, RtsAngleUnitForTesting unit,
		float resolution) noexcept
	{
		PROPERTY_METRICS metrics = {};
		metrics.nLogicalMin = (std::numeric_limits<LONG>::min)();
		metrics.nLogicalMax = (std::numeric_limits<LONG>::max)();
		metrics.fResolution = resolution;
		if (unit == RtsAngleUnitForTesting::Degrees) metrics.Units = PROPERTY_UNITS_DEGREES;
		else if (unit == RtsAngleUnitForTesting::Radians) metrics.Units = PROPERTY_UNITS_RADIANS;
		else metrics.Units = PROPERTY_UNITS_DEFAULT;
		float radians = kUnknownStylusValue;
		return DecodeAngle(rawValue, metrics, radians) ? radians : kUnknownStylusValue;
	}

	RtsStylusAnglesForTesting DecodeRtsStylusAnglesForTesting(bool hasAzimuthAltitude,
		float azimuth, float altitude, float xTilt, float yTilt) noexcept
	{
		const DecodedStylusAngles decoded = DecodeStylusAngles(
			hasAzimuthAltitude, azimuth, altitude, xTilt, yTilt);
		return { decoded.tilt, decoded.orientation };
	}

	SizeF DecodeRtsContactSizeForTesting(InputDeviceType deviceType,
		int32_t rawWidth, int32_t rawHeight, float packetScaleX, float packetScaleY) noexcept
	{
		return DecodeContactSize(deviceType, rawWidth, rawHeight, packetScaleX, packetScaleY);
	}

	bool RtsContactSizePropertiesRequestedForTesting() noexcept
	{
		bool hasWidth = false;
		bool hasHeight = false;
		for (const GUID& property : kExtendedPacketProperties)
		{
			hasWidth = hasWidth || IsEqualGUID(property, GUID_PACKETPROPERTY_GUID_WIDTH);
			hasHeight = hasHeight || IsEqualGUID(property, GUID_PACKETPROPERTY_GUID_HEIGHT);
		}
		return hasWidth && hasHeight;
	}

	bool RtsPenCursorDataInterestEnabledForTesting() noexcept
	{
		const uint32_t dataInterest = static_cast<uint32_t>(kProductionRtsDataInterest);
		return (dataInterest & static_cast<uint32_t>(RTSDI_StylusInRange)) != 0 &&
			(dataInterest & static_cast<uint32_t>(RTSDI_StylusOutOfRange)) != 0 &&
			(dataInterest & static_cast<uint32_t>(RTSDI_InAirPackets)) != 0;
	}

	bool RtsProductionDataInterestIsExactForTesting() noexcept
	{
		constexpr uint32_t expected =
			RTSDI_RealTimeStylusEnabled | RTSDI_RealTimeStylusDisabled |
			RTSDI_StylusInRange | RTSDI_StylusOutOfRange | RTSDI_InAirPackets |
			RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp |
			RTSDI_TabletAdded | RTSDI_TabletRemoved | RTSDI_UpdateMapping | RTSDI_Error;
		const uint32_t actual = static_cast<uint32_t>(kProductionRtsDataInterest);
		return actual == expected && actual != static_cast<uint32_t>(RTSDI_AllData);
	}

	namespace
	{
		const GUID& PropertyGuidForTesting(RtsPacketPropertyForTesting property) noexcept
		{
			switch (property)
			{
			case RtsPacketPropertyForTesting::X: return GUID_PACKETPROPERTY_GUID_X;
			case RtsPacketPropertyForTesting::Y: return GUID_PACKETPROPERTY_GUID_Y;
			case RtsPacketPropertyForTesting::Pressure:
				return GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE;
			case RtsPacketPropertyForTesting::XTilt:
				return GUID_PACKETPROPERTY_GUID_X_TILT_ORIENTATION;
			case RtsPacketPropertyForTesting::YTilt:
				return GUID_PACKETPROPERTY_GUID_Y_TILT_ORIENTATION;
			case RtsPacketPropertyForTesting::Azimuth:
				return GUID_PACKETPROPERTY_GUID_AZIMUTH_ORIENTATION;
			case RtsPacketPropertyForTesting::Altitude:
				return GUID_PACKETPROPERTY_GUID_ALTITUDE_ORIENTATION;
			case RtsPacketPropertyForTesting::Width: return GUID_PACKETPROPERTY_GUID_WIDTH;
			case RtsPacketPropertyForTesting::Height: return GUID_PACKETPROPERTY_GUID_HEIGHT;
			default: return GUID_NULL;
			}
		}

		RtsContextDecoder MakeTestingDecoder(TABLET_CONTEXT_ID contextId,
			InputDeviceType deviceType = InputDeviceType::Pen) noexcept
		{
			RtsContextDecoder decoder;
			decoder.tabletContextId = contextId;
			decoder.propertyCount = 2;
			decoder.xIndex = 0;
			decoder.yIndex = 1;
			decoder.contactScaleX = 1.0f;
			decoder.contactScaleY = 1.0f;
			decoder.deviceType = deviceType;
			return decoder;
		}

		bool PublishTestingDecoders(RtsDecoderCache& cache,
			const TABLET_CONTEXT_ID* contextIds, size_t contextCount,
			float positionScaleX, float positionScaleY) noexcept
		{
			if (contextCount > kContextDecoderCapacity || (contextCount > 0 && !contextIds))
				return false;
			std::array<RtsContextDecoder, kContextDecoderCapacity> staged = {};
			for (size_t index = 0; index < contextCount; ++index)
				staged[index] = MakeTestingDecoder(contextIds[index]);
			return cache.PublishStaged(staged, contextCount, positionScaleX, positionScaleY);
		}

		RtsActiveContactBinding MakeTestingBinding(uint32_t tabletContextId,
			uint32_t contactId, size_t decoderSlotIndex = 0,
			uint64_t decoderGeneration = 1) noexcept
		{
			return { tabletContextId, contactId, decoderSlotIndex, decoderGeneration, true };
		}

		bool FindCollidingContactIds(size_t logicalCapacity, size_t bucket,
			uint32_t* contactIds, size_t requestedCount) noexcept
		{
			if (logicalCapacity == 0 || bucket >= logicalCapacity ||
				(requestedCount > 0 && !contactIds)) return false;
			size_t found = 0;
			for (uint32_t contactId = 1; contactId != 0 && found < requestedCount; ++contactId)
			{
				if (HashActiveContactKey(17u, contactId) % logicalCapacity == bucket)
					contactIds[found++] = contactId;
			}
			return found == requestedCount;
		}
	}

	RtsDecoderResultForTesting DecodeRtsContextForTesting(
		const RtsPacketPropertyForTesting* properties, size_t propertyCount,
		const int32_t* packet, size_t decodedPropertyCount, InputDeviceType deviceType,
		float positionScaleX, float positionScaleY,
		float contactScaleX, float contactScaleY) noexcept
	{
		RtsDecoderResultForTesting result;
		constexpr size_t kTestingPropertyCapacity = 16;
		if (!properties || !packet || propertyCount > kTestingPropertyCapacity ||
			decodedPropertyCount > kTestingPropertyCapacity) return result;
		std::array<PACKET_PROPERTY, kTestingPropertyCapacity> packetProperties = {};
		std::array<LONG, kTestingPropertyCapacity> packetValues = {};
		for (size_t index = 0; index < propertyCount; ++index)
		{
			packetProperties[index].guid = PropertyGuidForTesting(properties[index]);
			packetProperties[index].PropertyMetrics.nLogicalMin = -36000;
			packetProperties[index].PropertyMetrics.nLogicalMax = 36000;
			packetProperties[index].PropertyMetrics.Units = PROPERTY_UNITS_DEGREES;
			packetProperties[index].PropertyMetrics.fResolution = 100.0f;
			if (properties[index] == RtsPacketPropertyForTesting::Pressure)
			{
				packetProperties[index].PropertyMetrics.nLogicalMin = 0;
				packetProperties[index].PropertyMetrics.nLogicalMax = 4095;
				packetProperties[index].PropertyMetrics.Units = PROPERTY_UNITS_DEFAULT;
				packetProperties[index].PropertyMetrics.fResolution = 1.0f;
			}
			packetValues[index] = static_cast<LONG>(packet[index]);
		}
		RtsContextDecoder decoder;
		result.parsed = PopulateContextDecoderProperties(packetProperties.data(),
			static_cast<ULONG>(propertyCount), decoder);
		if (!result.parsed) return result;
		decoder.positionScaleX = positionScaleX;
		decoder.positionScaleY = positionScaleY;
		decoder.contactScaleX = contactScaleX;
		decoder.contactScaleY = contactScaleY;
		decoder.deviceType = deviceType;
		decoder.generation = 1;
		decoder.valid = true;
		result.decoded = DecodeSnapshot(decoder, static_cast<ULONG>(decodedPropertyCount),
			packetValues.data(), ContactPhase::Move, 1234, result.snapshot);
		return result;
	}

	size_t ComputeRtsActiveBindingCapacityForTesting(int maximumTouches) noexcept
	{
		return ComputeActiveBindingCapacity(maximumTouches);
	}

	bool RtsBindingBasicInvariantsForTesting() noexcept
	{
		RtsActiveBindingTable table(32);
		const RtsActiveContactBinding first = MakeTestingBinding(1, 11, 2, 7);
		const RtsActiveContactBinding second = MakeTestingBinding(2, 22, 3, 8);
		return table.Insert(first) == RtsBindingInsertResult::Inserted &&
			table.Insert(second) == RtsBindingInsertResult::Inserted &&
			table.ActiveBindingCount() == 2 && table.Find(1, 11) && table.Find(2, 22) &&
			table.Insert(first) == RtsBindingInsertResult::Duplicate &&
			table.ActiveBindingCount() == 2 && table.Erase(1, 11) && !table.Find(1, 11) &&
			table.Find(2, 22) && table.ActiveBindingCount() == 1 && table.Erase(2, 22) &&
			table.ActiveBindingCount() == 0 && !table.Find(2, 22);
	}

	bool RtsBindingNonPowerOfTwoCapacityForTesting(size_t logicalCapacity) noexcept
	{
		RtsActiveBindingTable table(logicalCapacity);
		if (table.LogicalCapacity() != logicalCapacity) return false;
		const size_t insertedCount = logicalCapacity / 2u;
		for (size_t index = 0; index < insertedCount; ++index)
		{
			if (table.Insert(MakeTestingBinding(3u, static_cast<uint32_t>(index + 1u))) !=
				RtsBindingInsertResult::Inserted) return false;
		}
		for (size_t index = 0; index < insertedCount; ++index)
		{
			if (!table.Find(3u, static_cast<uint32_t>(index + 1u))) return false;
		}
		for (size_t index = 0; index < insertedCount; index += 2u)
		{
			if (!table.Erase(3u, static_cast<uint32_t>(index + 1u))) return false;
		}
		for (size_t index = 0; index < insertedCount; ++index)
		{
			const bool expected = (index % 2u) != 0;
			if ((table.Find(3u, static_cast<uint32_t>(index + 1u)) != nullptr) != expected)
				return false;
		}
		return table.ActiveBindingCount() == insertedCount / 2u;
	}

	bool RtsBindingRepeatedLifecycleForTesting() noexcept
	{
		RtsActiveBindingTable table(32);
		for (size_t iteration = 0; iteration < 10000; ++iteration)
		{
			const uint32_t contactId = static_cast<uint32_t>(iteration + 1u);
			if (table.Insert(MakeTestingBinding(4u, contactId)) !=
				RtsBindingInsertResult::Inserted || !table.Find(4u, contactId) ||
				!table.Erase(4u, contactId) || table.ActiveBindingCount() != 0 ||
				table.Find(4u, contactId)) return false;
		}
		return table.Insert(MakeTestingBinding(4u, 20001u)) ==
			RtsBindingInsertResult::Inserted && table.Find(4u, 20001u);
	}

	bool RtsBindingCollisionDeletionForTesting() noexcept
	{
		RtsActiveBindingTable table(32);
		std::array<uint32_t, 5> keys = {};
		if (!FindCollidingContactIds(32, 31, keys.data(), keys.size())) return false;
		for (size_t index = 0; index < 4; ++index)
		{
			if (table.Insert(MakeTestingBinding(17u, keys[index])) !=
				RtsBindingInsertResult::Inserted) return false;
		}
		if (!table.Erase(17u, keys[1]) || table.Find(17u, keys[1]) ||
			!table.Find(17u, keys[0]) || !table.Find(17u, keys[2]) ||
			!table.Find(17u, keys[3])) return false;
		return table.Insert(MakeTestingBinding(17u, keys[4])) ==
			RtsBindingInsertResult::Inserted && table.Find(17u, keys[4]) &&
			table.ActiveBindingCount() == 4;
	}

	bool RtsBindingCollisionChurnForTesting() noexcept
	{
		RtsActiveBindingTable table(96);
		std::array<uint32_t, 32> keys = {};
		if (!FindCollidingContactIds(96, 95, keys.data(), keys.size())) return false;
		for (size_t cycle = 0; cycle < 200; ++cycle)
		{
			for (uint32_t key : keys)
			{
				if (table.Insert(MakeTestingBinding(17u, key)) !=
					RtsBindingInsertResult::Inserted) return false;
			}
			for (size_t index = 0; index < keys.size(); index += 2u)
			{
				if (!table.Erase(17u, keys[index])) return false;
			}
			for (size_t index = 0; index < keys.size(); ++index)
			{
				const bool expected = (index % 2u) != 0;
				if ((table.Find(17u, keys[index]) != nullptr) != expected) return false;
			}
			for (size_t index = 0; index < keys.size(); index += 2u)
			{
				if (table.Insert(MakeTestingBinding(17u, keys[index], 0, cycle + 2u)) !=
					RtsBindingInsertResult::Inserted) return false;
			}
			for (uint32_t key : keys)
			{
				if (!table.Find(17u, key) || !table.Erase(17u, key)) return false;
			}
			if (table.ActiveBindingCount() != 0) return false;
		}
		return true;
	}

	bool RtsBindingCapacityExhaustionForTesting() noexcept
	{
		RtsActiveBindingTable table(32);
		std::array<uint32_t, 33> keys = {};
		if (!FindCollidingContactIds(32, 31, keys.data(), keys.size())) return false;
		for (size_t index = 0; index < table.LogicalCapacity(); ++index)
		{
			if (table.Insert(MakeTestingBinding(17u, keys[index])) !=
				RtsBindingInsertResult::Inserted) return false;
		}
		if (table.Insert(MakeTestingBinding(17u, keys.back())) != RtsBindingInsertResult::Full ||
			table.ActiveBindingCount() != table.LogicalCapacity()) return false;
		if (!table.Erase(17u, keys[0]) || table.ActiveBindingCount() != 31) return false;
		for (size_t index = 1; index < 32; ++index)
		{
			if (!table.Find(17u, keys[index])) return false;
		}
		return table.Insert(MakeTestingBinding(17u, keys.back())) ==
			RtsBindingInsertResult::Inserted && table.ActiveBindingCount() == 32 &&
			table.Find(17u, keys.back());
	}

	bool RtsBindingDuplicateRebindForTesting() noexcept
	{
		RtsActiveBindingTable table(32);
		const RtsActiveContactBinding oldBinding = MakeTestingBinding(8u, 9u, 1u, 10u);
		const RtsActiveContactBinding newBinding = MakeTestingBinding(8u, 9u, 2u, 11u);
		if (table.Insert(oldBinding) != RtsBindingInsertResult::Inserted ||
			table.Insert(newBinding) != RtsBindingInsertResult::Duplicate ||
			table.ActiveBindingCount() != 1 || !table.Erase(8u, 9u) ||
			table.Insert(newBinding) != RtsBindingInsertResult::Inserted) return false;
		const RtsActiveContactBinding* resolved = table.Find(8u, 9u);
		return resolved && resolved->decoderSlotIndex == 2u &&
			resolved->decoderGeneration == 11u && table.ActiveBindingCount() == 1;
	}

	bool RtsBindingGenerationMismatchForTesting() noexcept
	{
		RtsDecoderCache cache;
		cache.Reset();
		cache.BeginLifecycle();
		const TABLET_CONTEXT_ID contextId = 41;
		if (!PublishTestingDecoders(cache, &contextId, 1, 1.0f, 1.0f)) return false;
		const RtsContextDecoder* decoderA = cache.DecoderAt(0);
		if (!decoderA) return false;
		const RtsActiveContactBinding oldBinding = MakeTestingBinding(
			contextId, 5u, 0u, decoderA->generation);
		cache.Reset();
		cache.BeginLifecycle();
		if (!PublishTestingDecoders(cache, &contextId, 1, 2.0f, 2.0f)) return false;
		return cache.Resolve(oldBinding) == nullptr && cache.DecoderAt(0) &&
			cache.DecoderAt(0)->generation != oldBinding.decoderGeneration;
	}

	bool RtsLifecycleEnabledDisabledForTesting() noexcept
	{
		RtsDecoderCache cache;
		RtsActiveBindingTable bindings(32);
		cache.Reset();
		cache.BeginLifecycle();
		const TABLET_CONTEXT_ID contextId = 51;
		if (!PublishTestingDecoders(cache, &contextId, 1, 1.0f, 1.0f)) return false;
		const RtsContextDecoder* decoderA = cache.DecoderAt(0);
		if (!decoderA) return false;
		const RtsActiveContactBinding oldBinding = MakeTestingBinding(
			contextId, 6u, 0u, decoderA->generation);
		if (bindings.Insert(oldBinding) != RtsBindingInsertResult::Inserted) return false;
		bindings.Clear();
		cache.Reset();
		const bool disabled = !cache.LifecycleEnabled() && !cache.SharedPositionScaleValid() &&
			cache.DecoderAt(0) == nullptr && bindings.ActiveBindingCount() == 0;
		cache.BeginLifecycle();
		if (!PublishTestingDecoders(cache, &contextId, 1, 3.0f, 4.0f)) return false;
		return disabled && cache.Resolve(oldBinding) == nullptr &&
			bindings.Find(contextId, 6u) == nullptr;
	}

	bool RtsLifecycleUpdateMappingForTesting() noexcept
	{
		RtsDecoderCache cache;
		RtsActiveBindingTable bindings(32);
		cache.Reset();
		cache.BeginLifecycle();
		const TABLET_CONTEXT_ID contextId = 61;
		if (!PublishTestingDecoders(cache, &contextId, 1, 0.5f, 0.5f)) return false;
		const RtsContextDecoder* decoderA = cache.DecoderAt(0);
		if (!decoderA) return false;
		const RtsActiveContactBinding oldBinding = MakeTestingBinding(
			contextId, 7u, 0u, decoderA->generation);
		bindings.Insert(oldBinding);
		bindings.Clear();
		cache.Reset();
		cache.BeginLifecycle();
		if (!PublishTestingDecoders(cache, &contextId, 1, 1.5f, 2.5f)) return false;
		return cache.Resolve(oldBinding) == nullptr && bindings.ActiveBindingCount() == 0 &&
			cache.SharedPositionScaleX() == 1.5f && cache.SharedPositionScaleY() == 2.5f;
	}

	bool RtsLifecycleTabletRemovedForTesting() noexcept
	{
		RtsDecoderCache cache;
		cache.Reset();
		cache.BeginLifecycle();
		const std::array<TABLET_CONTEXT_ID, 3> before = { 71, 72, 73 };
		if (!PublishTestingDecoders(cache, before.data(), before.size(), 1.0f, 1.0f)) return false;
		cache.Reset();
		cache.BeginLifecycle();
		const std::array<TABLET_CONTEXT_ID, 2> after = { 71, 73 };
		if (!PublishTestingDecoders(cache, after.data(), after.size(), 2.0f, 2.0f)) return false;
		return cache.FindSlot(71) != kContextDecoderCapacity &&
			cache.FindSlot(72) == kContextDecoderCapacity &&
			cache.FindSlot(73) != kContextDecoderCapacity;
	}

	bool RtsLifecycleTabletAddedForTesting() noexcept
	{
		RtsDecoderCache cache;
		cache.Reset();
		cache.BeginLifecycle();
		const std::array<TABLET_CONTEXT_ID, 2> before = { 81, 83 };
		if (!PublishTestingDecoders(cache, before.data(), before.size(), 1.0f, 1.0f)) return false;
		const size_t slotA = cache.FindSlot(81);
		const size_t slotC = cache.FindSlot(83);
		const uint64_t generationA = cache.DecoderAt(slotA)->generation;
		const uint64_t generationC = cache.DecoderAt(slotC)->generation;
		if (!cache.PublishIncremental(MakeTestingDecoder(82))) return false;
		return cache.FindSlot(81) == slotA && cache.FindSlot(83) == slotC &&
			cache.FindSlot(82) != kContextDecoderCapacity &&
			cache.DecoderAt(slotA)->generation == generationA &&
			cache.DecoderAt(slotC)->generation == generationC;
	}

	bool RtsLifecycleTabletAddedFallbackForTesting() noexcept
	{
		RtsDecoderCache cache;
		cache.Reset();
		cache.BeginLifecycle();
		const std::array<TABLET_CONTEXT_ID, 2> before = { 91, 93 };
		if (!PublishTestingDecoders(cache, before.data(), before.size(), 1.0f, 1.0f)) return false;
		const RtsActiveContactBinding oldBinding = MakeTestingBinding(
			91u, 8u, cache.FindSlot(91), cache.DecoderAt(cache.FindSlot(91))->generation);
		cache.Reset();
		cache.BeginLifecycle();
		const std::array<TABLET_CONTEXT_ID, 3> rebuilt = { 91, 92, 93 };
		if (!PublishTestingDecoders(cache, rebuilt.data(), rebuilt.size(), 2.0f, 2.0f)) return false;
		const RtsContextDecoder* decoderA = cache.DecoderAt(cache.FindSlot(91));
		const RtsContextDecoder* decoderB = cache.DecoderAt(cache.FindSlot(92));
		const RtsContextDecoder* decoderC = cache.DecoderAt(cache.FindSlot(93));
		return cache.Resolve(oldBinding) == nullptr && decoderA && decoderB && decoderC &&
			decoderA->generation == decoderB->generation &&
			decoderB->generation == decoderC->generation;
	}

	bool RtsSharedScaleCompatibilityForTesting() noexcept
	{
		RtsDecoderCache cache;
		cache.Reset();
		cache.BeginLifecycle();
		std::array<RtsContextDecoder, kContextDecoderCapacity> staged = {};
		staged[0] = MakeTestingDecoder(102, InputDeviceType::Touch);
		staged[0].propertyCount = 4;
		staged[0].width = { {}, 2, true };
		staged[0].height = { {}, 3, true };
		staged[0].contactScaleX = 2.0f;
		staged[0].contactScaleY = 3.0f;
		staged[1] = MakeTestingDecoder(101);
		if (!cache.PublishStaged(staged, 2, 0.25f, 0.5f)) return false;
		const RtsContextDecoder* decoder = cache.DecoderAt(cache.FindSlot(102));
		const std::array<LONG, 4> packet = { 40, 20, 5, 4 };
		ContactSnapshot snapshot;
		return decoder && DecodeSnapshot(*decoder, 4, packet.data(), ContactPhase::Move, 1, snapshot) &&
			snapshot.position.x == 10.0f && snapshot.position.y == 10.0f &&
			snapshot.contactSize.width == 10.0f && snapshot.contactSize.height == 12.0f;
	}

	bool RtsErrorPreservesDecoderLifecycleForTesting() noexcept
	{
		RtsDecoderCache cache;
		RtsActiveBindingTable bindings(32);
		ContactInputCoordinator coordinator(32);
		cache.Reset();
		cache.BeginLifecycle();
		constexpr TABLET_CONTEXT_ID contextId = 111;
		constexpr uint32_t contactId = 9;
		if (!PublishTestingDecoders(cache, &contextId, 1, 0.25f, 0.5f)) return false;
		const size_t decoderSlot = cache.FindSlot(contextId);
		const RtsContextDecoder* decoder = cache.DecoderAt(decoderSlot);
		if (!decoder) return false;
		const uint64_t generation = cache.Generation();
		if (bindings.Insert(MakeTestingBinding(
			contextId, contactId, decoderSlot, decoder->generation)) !=
			RtsBindingInsertResult::Inserted) return false;

		ContactSnapshot down;
		down.position = { 10.0f, 20.0f };
		down.qpc = 100;
		down.phase = ContactPhase::Down;
		if (!coordinator.PublishDown(
			contextId, contactId, InputDeviceType::Pen, down)) return false;
		ContactRecord* record = nullptr;
		if (!coordinator.TryDequeue(record) || !record)
		{
			coordinator.CloseAllProducerContacts(101);
			return false;
		}
		const ContactHandle handle{ record, record->Generation() };

		// Error 的共享 helper 只关闭 active contact，不重置 decoder lifecycle。
		ClearActiveBindingsAndCloseContacts(bindings, coordinator, 102);
		ContactSnapshot cancelled;
		const bool contactCancelled = coordinator.TryReadSnapshot(handle, cancelled) &&
			cancelled.phase == ContactPhase::Cancelled;
		const RtsContextDecoder* preservedDecoder = cache.DecoderAt(decoderSlot);
		const bool lifecyclePreserved = bindings.ActiveBindingCount() == 0 &&
			preservedDecoder && cache.Generation() == generation && cache.LifecycleEnabled() &&
			cache.SharedPositionScaleValid() && cache.SharedPositionScaleX() == 0.25f &&
			cache.SharedPositionScaleY() == 0.5f &&
			preservedDecoder->generation == generation;

		const RtsActiveContactBinding nextBinding = MakeTestingBinding(
			contextId, contactId, decoderSlot, generation);
		const bool rebound = bindings.Insert(nextBinding) == RtsBindingInsertResult::Inserted;
		const RtsActiveContactBinding* resolvedBinding = bindings.Find(contextId, contactId);
		const RtsContextDecoder* resolvedDecoder = resolvedBinding
			? cache.Resolve(*resolvedBinding) : nullptr;
		const std::array<LONG, 2> packet = { 40, 20 };
		ContactSnapshot decodedSnapshot;
		const bool decoded = resolvedDecoder && DecodeSnapshot(*resolvedDecoder,
			static_cast<ULONG>(packet.size()), packet.data(), ContactPhase::Down,
			103, decodedSnapshot) && decodedSnapshot.position.x == 10.0f &&
			decodedSnapshot.position.y == 10.0f;

		bindings.Clear();
		coordinator.Recycle(handle);
		return contactCancelled && lifecyclePreserved && rebound && decoded;
	}

	bool RtsInAirCacheHitMissForTesting() noexcept
	{
		RtsDecoderCache cache;
		cache.Reset();
		cache.BeginLifecycle();
		constexpr TABLET_CONTEXT_ID contextId = 121;
		if (!PublishTestingDecoders(cache, &contextId, 1, 0.5f, 0.25f)) return false;
		const uint64_t generation = cache.Generation();
		const RtsContextDecoder* hit = FindCachedContextDecoder(cache, contextId);
		const RtsContextDecoder* miss = FindCachedContextDecoder(cache, contextId + 1u);
		const std::array<LONG, 2> packet = { 20, 40 };
		ContactSnapshot snapshot;
		return hit && !miss && DecodeSnapshot(*hit, static_cast<ULONG>(packet.size()),
			packet.data(), ContactPhase::Move, 200, snapshot) &&
			snapshot.position.x == 10.0f && snapshot.position.y == 10.0f &&
			cache.Generation() == generation;
	}

	bool RtsStateGateForTesting() noexcept
	{
		std::mutex writerMutex;
		std::atomic<uint32_t> state = 0;
		std::atomic<bool> writerEntered = false;
		std::thread writer;
		bool writerBitPublished = false;
		bool secondPacketRejected = false;
		bool writerWaitedForReader = false;
		{
			RtsPacketStateGuard firstPacket(state);
			if (!firstPacket) return false;
			writer = std::thread([&]
				{
					RtsStateWriterGuard stateWriter(writerMutex, state);
					writerEntered.store(true, std::memory_order_release);
				});
			const ULONGLONG deadline = GetTickCount64() + 2000u;
			while ((state.load(std::memory_order_acquire) & kRtsStateWriterBit) == 0 &&
				GetTickCount64() < deadline)
				std::this_thread::yield();
			writerBitPublished =
				(state.load(std::memory_order_acquire) & kRtsStateWriterBit) != 0;
			RtsPacketStateGuard secondPacket(state);
			secondPacketRejected = !secondPacket;
			writerWaitedForReader = !writerEntered.load(std::memory_order_acquire);
		}
		writer.join();
		RtsPacketStateGuard packetAfterWriter(state);
		return writerBitPublished && secondPacketRejected && writerWaitedForReader &&
			writerEntered.load(std::memory_order_acquire) && state.load(std::memory_order_acquire) == 1u &&
			packetAfterWriter;
	}

#endif

	struct RealTimeStylusInputImpl
	{
		Microsoft::WRL::ComPtr<IRealTimeStylus> stylus;
		Microsoft::WRL::ComPtr<IRealTimeStylus3> stylus3;
		Microsoft::WRL::ComPtr<IStylusSyncPlugin> plugin;
		ContactInputCoordinator* coordinator = nullptr;
		DrawingCursorEventSink* drawingCursorSink = nullptr;
		bool comInitialized = false;
		bool pluginAdded = false;
		bool initialized = false;
	};

	RealTimeStylusInput::RealTimeStylusInput()
		: impl_(std::make_unique<RealTimeStylusInputImpl>())
	{
	}

	RealTimeStylusInput::~RealTimeStylusInput()
	{
		Shutdown();
	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void RealTimeStylusInput::SetRtsTraceEnabled(bool enabled) noexcept
	{
		ConfigureRtsTrace(enabled);
	}
#endif

	bool RealTimeStylusInput::Initialize(HWND window, ContactInputCoordinator& coordinator,
		DrawingCursorEventSink* drawingCursorSink)
	{
		if (!window || impl_->initialized) return false;
		impl_->coordinator = &coordinator;
		impl_->drawingCursorSink = drawingCursorSink;
		DWORD windowProcessId = 0;
		const DWORD windowThreadId = GetWindowThreadProcessId(window, &windowProcessId);
#if defined(DRAW3_RTS_DIAGNOSTICS)
		RtsInitializationTrace traceState;
		traceState.currentThreadId = GetCurrentThreadId();
		traceState.windowThreadId = windowThreadId;
		traceState.windowHandle = reinterpret_cast<UINT_PTR>(window);
		traceState.windowStyle = GetWindowLongPtrW(window, GWL_STYLE);
		traceState.windowExtendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
		traceState.tabletServiceFlags = reinterpret_cast<ULONG_PTR>(
			GetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY));
		traceState.digitizer = GetSystemMetrics(SM_DIGITIZER);
		traceState.maximumTouches = GetSystemMetrics(SM_MAXIMUMTOUCHES);
		traceState.dataInterest = static_cast<uint32_t>(kProductionRtsDataInterest);
		auto logTrace = [&]() noexcept { LogRtsInitializationState(traceState); };
#endif
		std::cout << "[RTS] initialize currentThread=" << GetCurrentThreadId()
			<< " windowThread=" << windowThreadId
			<< " digitizer=0x" << std::hex << GetSystemMetrics(SM_DIGITIZER) << std::dec
			<< " maxTouches=" << GetSystemMetrics(SM_MAXIMUMTOUCHES)
			<< " tabletWindowFlags=0x" << std::hex
			<< reinterpret_cast<ULONG_PTR>(GetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY))
			<< std::dec << std::endl;

		HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#if defined(DRAW3_RTS_DIAGNOSTICS)
		traceState.coInitializeResult = result;
#endif
		if (FAILED(result))
		{
			LogHResult("CoInitializeEx(COINIT_MULTITHREADED)", result);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			logTrace();
#endif
			impl_->coordinator = nullptr;
			impl_->drawingCursorSink = nullptr;
			return false;
		}
		impl_->comInitialized = true;

		result = CoCreateInstance(__uuidof(RealTimeStylus), nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(impl_->stylus.ReleaseAndGetAddressOf()));
#if defined(DRAW3_RTS_DIAGNOSTICS)
		traceState.createStylusResult = result;
#endif
		if (FAILED(result))
		{
			LogHResult("CoCreateInstance(RealTimeStylus)", result);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			logTrace();
#endif
			Shutdown();
			return false;
		}

		result = impl_->stylus->put_HWND(reinterpret_cast<HANDLE_PTR>(window));
#if defined(DRAW3_RTS_DIAGNOSTICS)
		traceState.putHwndResult = result;
#endif
		if (FAILED(result)) LogHResult("Bind RealTimeStylus HWND", result);
		if (SUCCEEDED(result))
		{
			result = impl_->stylus->SetAllTabletsMode(TRUE); // 同时接收鼠标、笔和触摸。
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.setAllTabletsModeResult = result;
#endif
		}
		if (FAILED(result)) LogHResult("Set RealTimeStylus all-tablets mode", result);
		if (SUCCEEDED(result))
		{
			const GUID* desiredPacketProperties = kExtendedPacketProperties.data();
			const ULONG desiredPacketPropertyCount =
				static_cast<ULONG>(kExtendedPacketProperties.size());
			const HRESULT desiredPacketResult = impl_->stylus->SetDesiredPacketDescription(
				desiredPacketPropertyCount, desiredPacketProperties);
			result = desiredPacketResult;
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.desiredPacketDescriptionResult = desiredPacketResult;
#endif
			if (FAILED(result))
			{
				LogHResult("Set desired RealTimeStylus packet description", result);
				const HRESULT requiredResult = impl_->stylus->SetDesiredPacketDescription(
					static_cast<ULONG>(kRequiredPacketProperties.size()), kRequiredPacketProperties.data());
				result = requiredResult;
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceState.fallbackPacketDescriptionResult = requiredResult;
#endif
			}
			if (SUCCEEDED(result))
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceState.selectedPacketPropertyCount = SUCCEEDED(desiredPacketResult)
					? desiredPacketPropertyCount : static_cast<ULONG>(kRequiredPacketProperties.size());
				traceState.selectedPacketDescription = SUCCEEDED(desiredPacketResult)
					? "X,Y,Pressure,XTilt,YTilt,Azimuth,Altitude,Width,Height" : "X,Y";
#endif
			}
		}
		if (FAILED(result)) LogHResult("Set RealTimeStylus packet description", result);
		if (SUCCEEDED(result))
		{
			Microsoft::WRL::ComPtr<IRealTimeStylus2> stylus2;
			const HRESULT stylus2Result = impl_->stylus.As(&stylus2);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.queryStylus2Result = stylus2Result;
#endif
			if (SUCCEEDED(stylus2Result))
			{
				// 窗口标志负责多点 opt-in；这里关闭轻拂，避免笔输入被系统手势延迟或接管。
				const HRESULT flicksResult = stylus2->put_FlicksEnabled(FALSE);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceState.disableFlicksResult = flicksResult;
#endif
				if (FAILED(flicksResult)) LogHResult("Disable RealTimeStylus flicks", flicksResult);
			}
		}
		if (SUCCEEDED(result))
		{
			result = impl_->stylus.As(&impl_->stylus3);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.queryStylus3Result = result;
#endif
		}
		if (SUCCEEDED(result))
		{
			result = impl_->stylus3->put_MultiTouchEnabled(TRUE);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.enableMultiTouchResult = result;
#endif
		}
		if (SUCCEEDED(result))
		{
			BOOL multiTouchEnabled = FALSE;
			const HRESULT verifyResult = impl_->stylus3->get_MultiTouchEnabled(&multiTouchEnabled);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.readMultiTouchResult = verifyResult;
			traceState.multiTouchEnabled = multiTouchEnabled;
#endif
			if (SUCCEEDED(verifyResult))
				std::cout << "[RTS] MultiTouchEnabled=" << (multiTouchEnabled ? "true" : "false") << std::endl;
			else
				LogHResult("Read RealTimeStylus multi-touch state", verifyResult);
		}
		if (FAILED(result))
		{
			LogHResult("Configure RealTimeStylus multi-contact input", result);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			logTrace();
#endif
			Shutdown();
			return false;
		}

		auto* plugin = new (std::nothrow) StylusSyncPlugin(coordinator, drawingCursorSink);
		if (!plugin)
		{
#if defined(DRAW3_RTS_DIAGNOSTICS)
			logTrace();
#endif
			Shutdown();
			return false;
		}
		if (FAILED(plugin->MarshalerResult()))
		{
			LogHResult("CoCreateFreeThreadedMarshaler", plugin->MarshalerResult());
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceState.marshalerResult = plugin->MarshalerResult();
			logTrace();
#endif
			plugin->Release();
			Shutdown();
			return false;
		}
		impl_->plugin.Attach(plugin);
		result = impl_->stylus->AddStylusSyncPlugin(0, impl_->plugin.Get());
#if defined(DRAW3_RTS_DIAGNOSTICS)
		traceState.marshalerResult = plugin->MarshalerResult();
		traceState.addPluginResult = result;
#endif
		if (FAILED(result))
		{
			LogHResult("AddStylusSyncPlugin", result);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			logTrace();
#endif
			Shutdown();
			return false;
		}
		impl_->pluginAdded = true;

		result = impl_->stylus->put_Enabled(TRUE);
#if defined(DRAW3_RTS_DIAGNOSTICS)
		traceState.enableStylusResult = result;
#endif
		if (FAILED(result))
		{
			LogHResult("Enable RealTimeStylus", result);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			logTrace();
#endif
			Shutdown();
			return false;
		}
		impl_->initialized = true;
#if defined(DRAW3_RTS_DIAGNOSTICS)
		logTrace();
#endif
		return true;
	}

	void RealTimeStylusInput::Shutdown() noexcept
	{
		if (!impl_) return;
		if (impl_->stylus)
		{
			const HRESULT disableResult = impl_->stylus->put_Enabled(FALSE); // 先停止产生新的同步回调。
			if (FAILED(disableResult)) LogHResult("Disable RealTimeStylus", disableResult);
			if (impl_->pluginAdded)
			{
				IStylusSyncPlugin* removedPlugin = nullptr;
				const HRESULT removeResult = impl_->stylus->RemoveStylusSyncPlugin(0, &removedPlugin);
				if (FAILED(removeResult)) LogHResult("RemoveStylusSyncPlugin", removeResult);
				if (removedPlugin) removedPlugin->Release();
				impl_->pluginAdded = false;
			}
		}
		if (impl_->coordinator) impl_->coordinator->CloseAllProducerContacts(QueryQpc());
		if (impl_->drawingCursorSink) impl_->drawingCursorSink->ClearPenCursorSample();
		impl_->plugin.Reset();
		impl_->stylus3.Reset();
		impl_->stylus.Reset();
#if defined(DRAW3_RTS_DIAGNOSTICS)
		FlushRtsCallbackTrace(); // 释放 RTS 与插件引用后再统一格式化，失败清理路径也不会并发 callback。
#endif
		impl_->coordinator = nullptr;
		impl_->drawingCursorSink = nullptr;
		impl_->initialized = false;
		if (impl_->comInitialized)
		{
			CoUninitialize();
			impl_->comInitialized = false;
		}
	}

	bool RealTimeStylusInput::IsInitialized() const noexcept
	{
		return impl_ && impl_->initialized;
	}
}
