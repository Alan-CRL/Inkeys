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
#include <tpcshrd.h>
#include <wrl/client.h>

module draw3.realtime_stylus;

import draw3.diagnostics;
import draw3.ink_prediction;

namespace draw3
{
	namespace
	{
		constexpr size_t kTabletMetadataCapacity = 32;
		constexpr ULONG kMaximumPacketPropertyCount = 256;
		constexpr float kUnknownStylusValue = -1.0f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kHalfPi = kPi * 0.5f;
		constexpr float kTwoPi = kPi * 2.0f;
		constexpr float kTiltLimit = kHalfPi - 0.0001f;
		const std::array<GUID, 5> kIdtRtsPacketProperties = {
			GUID_PACKETPROPERTY_GUID_X,
			GUID_PACKETPROPERTY_GUID_Y,
			GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE,
			GUID_PACKETPROPERTY_GUID_WIDTH,
			GUID_PACKETPROPERTY_GUID_HEIGHT
		};
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
				RTSDI_TabletAdded | RTSDI_TabletRemoved | RTSDI_Error);
		constexpr RealTimeStylusDataInterest kTouchpadProbeDataInterest =
			static_cast<RealTimeStylusDataInterest>(
				// IdtRts 的精确兴趣集只用于 --rts-trace 单变量探针。
				RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp);

		struct PacketPropertyMetadata
		{
			PROPERTY_METRICS metrics = {};
			ULONG index = 0;
			bool present = false;
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
				bool resumeFailureLogged = false;
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
						if (!state->resumeFailureLogged)
						{
							std::cout << "[StrokeReconnectSimulation] resume_failed tcid="
								<< tabletContextId << " physical_cid=" << contactId << std::endl;
							state->resumeFailureLogged = true;
						}
						return false;
					}
					state->routedContactId = resumedContactId;
					state->dropping = false;
					state->resumeFailureLogged = false;
					const double actualGapMilliseconds = static_cast<double>(
						snapshot.qpc - state->interruptionUpQpc) * 1000.0 /
						static_cast<double>(qpcFrequency_);
					ScheduleNextInterruption(*state, snapshot.qpc);
					std::cout << "[StrokeReconnectSimulation] resumed device="
						<< static_cast<uint32_t>(state->deviceType) << " tcid=" << tabletContextId
						<< " physical_cid=" << contactId << " synthetic_cid=" << resumedContactId
						<< " requested_drop_ms=" << state->requestedDropMilliseconds
						<< " actual_gap_ms=" << actualGapMilliseconds << std::endl;
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
						std::cout << "[StrokeReconnectSimulation] interrupted device="
							<< static_cast<uint32_t>(state->deviceType) << " tcid=" << tabletContextId
							<< " physical_cid=" << contactId << " routed_cid="
							<< state->routedContactId << " drop_ms="
							<< state->requestedDropMilliseconds << std::endl;
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
				else
					std::cout << "[StrokeReconnectSimulation] physical_up_during_drop tcid="
						<< tabletContextId << " physical_cid=" << contactId << std::endl;
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

		struct TabletMetadata
		{
			std::atomic<bool> published = false;
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
			float packetScaleX = 1.0f;
			float packetScaleY = 1.0f;
			InputDeviceType deviceType = InputDeviceType::Pen;
		};

		class StylusSyncPlugin final : public IStylusSyncPlugin
		{
		public:
			StylusSyncPlugin(ContactInputCoordinator& coordinator,
				DrawingCursorEventSink* drawingCursorSink, bool touchpadProbeEnabled)
				: coordinator_(coordinator), interruptionSimulation_(coordinator),
				drawingCursorSink_(drawingCursorSink), touchpadProbeEnabled_(touchpadProbeEnabled)
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
				std::cout << "[RTS] enabled contexts=" << contextCount << std::endl;
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("RealTimeStylusEnabled", nullptr, nullptr, contextCount, 0,
					nullptr, nullptr, true, false);
#endif
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					interruptionSimulation_.Reset();
				pixelScalePublished_.store(false, std::memory_order_release);
				if (!touchpadProbeEnabled_) EnsurePixelScale(source);
				for (ULONG index = 0; index < contextCount; ++index)
				{
					// 探针不订阅 Enabled，普通生产路径仍在这里预热首 context 缩放。
					EnsureMetadata(source, contextIds[index], nullptr);
				}
				// 单个 tablet 暂时无法查询时不能让整个插件进入 Error；Down 会按 tcid 再尝试一次。
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE RealTimeStylusDisabled(IRealTimeStylus*,
				ULONG, const TABLET_CONTEXT_ID*) override
			{
				std::cout << "[RTS] disabled." << std::endl;
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("RealTimeStylusDisabled", nullptr, nullptr, 0, 0,
					nullptr, nullptr, true, false);
#endif
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					interruptionSimulation_.Reset();
				PublishDefaultPenCursor();
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusInRange(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, STYLUS_ID) override
			{
				if (source)
				{
					EnsurePixelScale(source);
					// InRange 不含倒转信息；等待首个 InAir/Pointer 包决定普通笔或笔尾。
					EnsureMetadata(source, contextId, nullptr);
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
				EnsurePixelScale(source);
				const TabletMetadata* metadata = EnsureMetadata(source, stylusInfo->tcid, nullptr);
				ContactSnapshot snapshot;
				if (!metadata || !DecodeSnapshot(
					metadata, propertyCount, packet, ContactPhase::Down, snapshot))
				{
					PublishDefaultPenCursor(); // 解码失败时不能把旧 Hover visual 留在接触位置。
					std::cout << "[RTS] down decode failed tcid=" << stylusInfo->tcid
						<< " cid=" << stylusInfo->cid << " properties=" << propertyCount
						<< " metadata=" << (metadata ? "yes" : "no") << std::endl;
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("StylusDown", stylusInfo, metadata, 1, propertyCount,
						packet, nullptr, false, false);
#endif
					return S_OK;
				}
				snapshot.isInvertedCursor = metadata->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishPenCursor(metadata, stylusInfo, true, snapshot);
				InputDeviceType deviceType = metadata ? metadata->deviceType : InputDeviceType::Pen;
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
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("StylusDown", stylusInfo, metadata, 1, propertyCount,
					packet, &snapshot, true, published, deviceType, true);
#endif
				return published ? S_OK : E_OUTOFMEMORY;
			}

			HRESULT STDMETHODCALLTYPE StylusUp(IRealTimeStylus* source, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				if (!stylusInfo || !packet) return E_INVALIDARG;
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid);
				if (!metadata && source) metadata = EnsureMetadata(source, stylusInfo->tcid, nullptr);
				ContactSnapshot snapshot;
				if (!metadata || !DecodeSnapshot(
					metadata, propertyCount, packet, ContactPhase::Up, snapshot))
				{
					PublishDefaultPenCursor();
					snapshot.position = { NAN, NAN };
					snapshot.qpc = QueryQpc();
					// 坏 Up 包不能把 contact 永久留在 Producing；协调器会沿用最后有效位置闭合。
					bool published = false;
					if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
						published = interruptionSimulation_.PublishUp(
							stylusInfo->tcid, stylusInfo->cid, snapshot);
					else
						published = coordinator_.PublishUp(stylusInfo->tcid, stylusInfo->cid, snapshot);
					std::cout << "[RTS] up decode failed tcid=" << stylusInfo->tcid
						<< " cid=" << stylusInfo->cid << " properties=" << propertyCount << std::endl;
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("StylusUp", stylusInfo, metadata, 1, propertyCount,
						packet, nullptr, false, published);
#endif
					return S_OK;
				}
				snapshot.isInvertedCursor = metadata->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishDefaultPenCursor(); // Up 只清除接触光标，后续 InAir/Pointer 样本再恢复真实 Hover。
				bool published = false;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					published = interruptionSimulation_.PublishUp(
						stylusInfo->tcid, stylusInfo->cid, snapshot);
				else
					published = coordinator_.PublishUp(stylusInfo->tcid, stylusInfo->cid, snapshot);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("StylusUp", stylusInfo, metadata, 1, propertyCount,
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

			HRESULT STDMETHODCALLTYPE InAirPackets(IRealTimeStylus* source,
				const StylusInfo* stylusInfo, ULONG packetCount,
				ULONG packetBufferLength, LONG* packets, ULONG*, LONG**) override
			{
				if (!stylusInfo || !packets || packetCount == 0 || packetBufferLength < packetCount ||
					packetBufferLength % packetCount != 0) return E_INVALIDARG;
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid);
				if (!metadata && source) metadata = EnsureMetadata(source, stylusInfo->tcid, nullptr);
				const ULONG propertyCount = packetBufferLength / packetCount;
				const LONG* lastPacket = packets +
					static_cast<size_t>(packetCount - 1) * propertyCount;
				ContactSnapshot snapshot;
				const bool decoded = metadata && DecodeSnapshot(metadata, propertyCount, lastPacket,
					ContactPhase::Move, snapshot);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("InAirPackets", stylusInfo, metadata, packetCount, propertyCount,
					lastPacket, decoded ? &snapshot : nullptr, decoded, false);
#endif
				if (!decoded) return S_OK;
				snapshot.isInvertedCursor = metadata->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishPenCursor(metadata, stylusInfo, false, snapshot);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Packets(IRealTimeStylus*, const StylusInfo* stylusInfo,
				ULONG packetCount, ULONG packetBufferLength, LONG* packets, ULONG*, LONG**) override
			{
				if (!stylusInfo || !packets || packetCount == 0 || packetBufferLength < packetCount ||
					packetBufferLength % packetCount != 0) return E_INVALIDARG;
				const ULONG propertyCount = packetBufferLength / packetCount;
				const LONG* lastPacket = packets + static_cast<size_t>(packetCount - 1) * propertyCount;
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid); // Move 热路径通常只扫描固定缓存。
				ContactSnapshot snapshot;
				if (!metadata || !DecodeSnapshot(
					metadata, propertyCount, lastPacket, ContactPhase::Move, snapshot))
				{
#if defined(DRAW3_RTS_DIAGNOSTICS)
					RecordCallback("Packets", stylusInfo, metadata, packetCount, propertyCount,
						lastPacket, nullptr, false, false);
#endif
					return S_OK;
				}
				snapshot.isInvertedCursor = metadata->deviceType == InputDeviceType::Pen &&
					stylusInfo->bIsInvertedCursor != FALSE;
				PublishPenCursor(metadata, stylusInfo, true, snapshot);
				bool published = false;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					published = interruptionSimulation_.PublishMove(
						stylusInfo->tcid, stylusInfo->cid, snapshot);
				else
					published = coordinator_.PublishMove(stylusInfo->tcid, stylusInfo->cid, snapshot);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("Packets", stylusInfo, metadata, packetCount, propertyCount,
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
				TABLET_CONTEXT_ID contextId = 0;
				const HRESULT result = source->GetTabletContextIdFromTablet(tablet, &contextId);
				if (FAILED(result)) return result;
				EnsureMetadata(source, contextId, tablet);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("TabletAdded", nullptr, nullptr, 0, 0,
					nullptr, nullptr, SUCCEEDED(result), false);
#endif
				return S_OK; // Tablet 初始化时序不稳定时保留后续 Down 的重试机会。
			}

			HRESULT STDMETHODCALLTYPE TabletRemoved(IRealTimeStylus*, LONG) override
			{
				// 回调只给 tablet index，无法无查询地还原 tcid；设备移除时安全取消全部活动 contact。
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("TabletRemoved", nullptr, nullptr, 0, 0,
					nullptr, nullptr, true, false);
#endif
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					interruptionSimulation_.Reset();
				PublishDefaultPenCursor();
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Error(IRealTimeStylus*, IStylusPlugin*, RealTimeStylusDataInterest dataInterest,
				HRESULT errorCode, LONG_PTR*) override
			{
				lastError_.store(errorCode, std::memory_order_release);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				RecordCallback("Error", nullptr, nullptr, 0, 0,
					nullptr, nullptr, false, false);
#endif
				std::cout << "[RTS] plugin error dataInterest=0x" << std::hex
					<< static_cast<unsigned long>(dataInterest)
					<< " HRESULT=0x" << static_cast<unsigned long>(errorCode) << std::dec << std::endl;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					interruptionSimulation_.Reset();
				PublishDefaultPenCursor();
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE UpdateMapping(IRealTimeStylus*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE DataInterest(RealTimeStylusDataInterest* interest) override
			{
				if (!interest) return E_POINTER;
				*interest = touchpadProbeEnabled_
					? kTouchpadProbeDataInterest : kProductionRtsDataInterest;
				return S_OK;
			}

		private:
#if defined(DRAW3_RTS_DIAGNOSTICS)
			void RecordCallback(const char* eventName, const StylusInfo* stylusInfo,
				const TabletMetadata* metadata, ULONG packetCount, ULONG propertyCount,
				const LONG* packet, const ContactSnapshot* snapshot, bool decoded,
				bool published, InputDeviceType routedDeviceType = InputDeviceType::Pen,
				bool overrideDeviceType = false) noexcept
			{
				RtsCallbackTrace trace;
				trace.eventName = eventName;
				trace.qpc = snapshot ? snapshot->qpc : QueryQpc();
				trace.threadId = GetCurrentThreadId();
				trace.tabletContextId = stylusInfo ? stylusInfo->tcid : 0;
				trace.contactId = stylusInfo ? stylusInfo->cid : 0;
				trace.deviceType = static_cast<uint32_t>(overrideDeviceType || !metadata
					? routedDeviceType : metadata->deviceType);
				trace.packetCount = packetCount;
				trace.propertyCount = propertyCount;
				trace.decoded = decoded;
				trace.published = published;
				if (snapshot)
				{
					trace.decodedX = snapshot->position.x;
					trace.decodedY = snapshot->position.y;
				}
				if (metadata && packet && metadata->xIndex < propertyCount &&
					metadata->yIndex < propertyCount)
				{
					trace.hasRawPosition = true;
					trace.rawX = packet[metadata->xIndex];
					trace.rawY = packet[metadata->yIndex];
				}
				RecordRtsCallback(trace);
			}
#endif

			void PublishPenCursor(const TabletMetadata* metadata,
				const StylusInfo* stylusInfo, bool inContact,
				const ContactSnapshot& snapshot) noexcept
			{
				if (!drawingCursorSink_ || !metadata || !stylusInfo ||
					metadata->deviceType != InputDeviceType::Pen) return;
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

			bool EnsurePixelScale(IRealTimeStylus* source)
			{
				if (!source) return false;
				if (pixelScalePublished_.load(std::memory_order_acquire)) return true;
				std::lock_guard lock(pixelScaleMutex_);
				if (pixelScalePublished_.load(std::memory_order_relaxed)) return true;

				ULONG contextCount = 0;
				TABLET_CONTEXT_ID* contextIds = nullptr;
				const HRESULT contextResult = source->GetAllTabletContextIds(
					&contextCount, &contextIds);
				if (FAILED(contextResult) || contextCount == 0 || !contextIds)
				{
					CoTaskMemFree(contextIds);
					std::cout << "[RTS] first-context list unavailable HRESULT=0x" << std::hex
						<< static_cast<unsigned long>(contextResult) << std::dec
						<< " contexts=" << contextCount << std::endl;
					return false;
				}

				const TABLET_CONTEXT_ID firstContextId = contextIds[0];
				FLOAT scaleX = 1.0f;
				FLOAT scaleY = 1.0f;
				ULONG propertyCount = 0;
				PACKET_PROPERTY* properties = nullptr;
				const HRESULT scaleResult = source->GetPacketDescriptionData(
					firstContextId, &scaleX, &scaleY, &propertyCount, &properties);
				CoTaskMemFree(properties);
				CoTaskMemFree(contextIds);
				if (FAILED(scaleResult) || !std::isfinite(scaleX) || !std::isfinite(scaleY) ||
					scaleX <= 0.0f || scaleY <= 0.0f)
				{
					std::cout << "[RTS] first-context scale unavailable tcid=" << firstContextId
						<< " HRESULT=0x" << std::hex << static_cast<unsigned long>(scaleResult)
						<< std::dec << " scale=(" << scaleX << "," << scaleY << ")" << std::endl;
					return false;
				}

				// 触摸板要求不订阅 Enabled；首个 Down/InRange 在慢路径完成同一缩放初始化。
				inkToPixelScaleX_ = scaleX;
				inkToPixelScaleY_ = scaleY;
				pixelScalePublished_.store(true, std::memory_order_release);
				std::cout << "[RTS] use first-context pixel scale tcid=" << firstContextId
					<< " scale=(" << scaleX << "," << scaleY << ")" << std::endl;
				return true;
			}

			const TabletMetadata* FindMetadata(TABLET_CONTEXT_ID contextId) const noexcept
			{
				for (const TabletMetadata& metadata : metadata_)
				{
					if (metadata.published.load(std::memory_order_acquire) &&
						metadata.tabletContextId == contextId) return &metadata;
				}
				return nullptr;
			}

			const TabletMetadata* EnsureMetadata(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, IInkTablet* suppliedTablet)
			{
				if (!source) return nullptr;
				if (const TabletMetadata* existing = FindMetadata(contextId)) return existing;
				std::lock_guard lock(metadataMutex_);
				if (const TabletMetadata* existing = FindMetadata(contextId)) return existing;

				TabletMetadata* target = nullptr;
				for (TabletMetadata& metadata : metadata_)
				{
					if (!metadata.published.load(std::memory_order_relaxed))
					{
						target = &metadata;
						break;
					}
				}
				if (!target) return nullptr;

				FLOAT inkToDeviceScaleX = 1.0f;
				FLOAT inkToDeviceScaleY = 1.0f;
				ULONG propertyCount = 0;
				PACKET_PROPERTY* properties = nullptr;
				const HRESULT packetResult = source->GetPacketDescriptionData(contextId,
					&inkToDeviceScaleX, &inkToDeviceScaleY, &propertyCount, &properties);
				if (FAILED(packetResult) || propertyCount < 2 ||
					propertyCount > kMaximumPacketPropertyCount || !properties)
				{
					CoTaskMemFree(properties);
					std::cout << "[RTS] metadata query invalid tcid=" << contextId
						<< " HRESULT=0x" << std::hex << static_cast<unsigned long>(packetResult)
						<< std::dec << " properties=" << propertyCount << std::endl;
					return nullptr;
				}

				ULONG xIndex = 0;
				ULONG yIndex = 1;
				bool hasX = false;
				bool hasY = false;
				PacketPropertyMetadata pressure;
				PacketPropertyMetadata xTilt;
				PacketPropertyMetadata yTilt;
				PacketPropertyMetadata azimuth;
				PacketPropertyMetadata altitude;
				PacketPropertyMetadata width;
				PacketPropertyMetadata height;
				const auto captureProperty = [&](PacketPropertyMetadata& targetMetadata, ULONG index)
					{
						targetMetadata.metrics = properties[index].PropertyMetrics;
						targetMetadata.index = index;
						targetMetadata.present = true;
					};
				for (ULONG index = 0; index < propertyCount; ++index)
				{
					if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_X))
					{
						xIndex = index;
						hasX = true;
					}
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_Y))
					{
						yIndex = index;
						hasY = true;
					}
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE))
						captureProperty(pressure, index);
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_X_TILT_ORIENTATION))
						captureProperty(xTilt, index);
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_Y_TILT_ORIENTATION))
						captureProperty(yTilt, index);
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_AZIMUTH_ORIENTATION))
						captureProperty(azimuth, index);
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_ALTITUDE_ORIENTATION))
						captureProperty(altitude, index);
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_WIDTH))
						captureProperty(width, index);
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_HEIGHT))
						captureProperty(height, index);
				}
				CoTaskMemFree(properties);
				if (!hasX || !hasY || !std::isfinite(inkToDeviceScaleX) ||
					!std::isfinite(inkToDeviceScaleY) ||
					inkToDeviceScaleX <= 0.0f || inkToDeviceScaleY <= 0.0f)
				{
					std::cout << "[RTS] invalid metadata tcid=" << contextId
						<< " properties=" << propertyCount << " hasX=" << hasX << " hasY=" << hasY
						<< " scale=(" << inkToDeviceScaleX << "," << inkToDeviceScaleY << ")" << std::endl;
					return nullptr;
				}

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

				target->tabletContextId = contextId;
				target->propertyCount = propertyCount;
				target->xIndex = xIndex;
				target->yIndex = yIndex;
				target->pressure = pressure;
				target->xTilt = xTilt;
				target->yTilt = yTilt;
				target->azimuth = azimuth;
				target->altitude = altitude;
				target->width = width;
				target->height = height;
				target->packetScaleX = inkToDeviceScaleX;
				target->packetScaleY = inkToDeviceScaleY;
				target->deviceType = deviceType;
				target->published.store(true, std::memory_order_release);
				std::cout << "[RTS] metadata tcid=" << contextId << " type="
					<< static_cast<uint32_t>(deviceType) << " properties=" << propertyCount
					<< " xyIndex=(" << xIndex << "," << yIndex << ") scale=("
					<< inkToDeviceScaleX << "," << inkToDeviceScaleY << ") pressure="
					<< (pressure.present ? "yes" : "no") << " azAlt="
					<< (azimuth.present && altitude.present ? "yes" : "no") << " xyTilt="
					<< (xTilt.present && yTilt.present ? "yes" : "no") << " contactSize="
					<< (width.present && height.present ? "yes" : "no") << std::endl;
				return target;
			}

			bool DecodeSnapshot(const TabletMetadata* metadata, ULONG propertyCount,
				const LONG* packet, ContactPhase phase, ContactSnapshot& snapshot) const noexcept
			{
				if (!metadata || !packet || propertyCount != metadata->propertyCount) return false;
				const ULONG xIndex = metadata->xIndex;
				const ULONG yIndex = metadata->yIndex;
				if (xIndex >= propertyCount || yIndex >= propertyCount) return false;
				if (!pixelScalePublished_.load(std::memory_order_acquire)) return false;
				// 当前 tcid 只决定属性索引；像素比例统一沿用首 tablet context。
				snapshot.position.x = static_cast<float>(packet[xIndex]) * inkToPixelScaleX_;
				snapshot.position.y = static_cast<float>(packet[yIndex]) * inkToPixelScaleY_;
				snapshot.pressure = kUnknownStylusValue;
				snapshot.tilt = kUnknownStylusValue;
				snapshot.orientation = kUnknownStylusValue;
				if (metadata->deviceType == InputDeviceType::Pen)
				{
					if (metadata->pressure.present && metadata->pressure.index < propertyCount)
						snapshot.pressure = NormalizePressure(
							packet[metadata->pressure.index], metadata->pressure.metrics);

					float azimuth = 0.0f;
					float altitude = 0.0f;
					const bool hasAzimuthAltitude = metadata->azimuth.present && metadata->altitude.present &&
						metadata->azimuth.index < propertyCount && metadata->altitude.index < propertyCount &&
						DecodeAngle(packet[metadata->azimuth.index], metadata->azimuth.metrics, azimuth) &&
						DecodeAngle(packet[metadata->altitude.index], metadata->altitude.metrics, altitude);
					float xTilt = 0.0f;
					float yTilt = 0.0f;
					const bool hasXyTilt = metadata->xTilt.present && metadata->yTilt.present &&
						metadata->xTilt.index < propertyCount && metadata->yTilt.index < propertyCount &&
						DecodeAngle(packet[metadata->xTilt.index], metadata->xTilt.metrics, xTilt) &&
						DecodeAngle(packet[metadata->yTilt.index], metadata->yTilt.metrics, yTilt);
					const DecodedStylusAngles angles = DecodeStylusAngles(hasAzimuthAltitude,
						azimuth, altitude, hasXyTilt ? xTilt : NAN, hasXyTilt ? yTilt : NAN);
					snapshot.tilt = angles.tilt;
					snapshot.orientation = angles.orientation;
				}
				snapshot.contactSize = {};
				if (metadata->width.present && metadata->height.present &&
					metadata->width.index < propertyCount && metadata->height.index < propertyCount)
				{
					// 接触面积与坐标使用同一 tablet-to-device 轴缩放，最终统一为像素。
					snapshot.contactSize = DecodeContactSize(metadata->deviceType,
						packet[metadata->width.index], packet[metadata->height.index],
						metadata->packetScaleX, metadata->packetScaleY);
				}
				snapshot.qpc = QueryQpc();
				snapshot.phase = phase;
				return std::isfinite(snapshot.position.x) && std::isfinite(snapshot.position.y);
			}

			std::atomic<ULONG> referenceCount_ = 1;
			std::atomic<HRESULT> lastError_ = S_OK;
			ContactInputCoordinator& coordinator_;
			[[no_unique_address]] InterruptedStrokeSimulation<
				kInterruptedStrokeReconnectSimulationEnabled> interruptionSimulation_;
			DrawingCursorEventSink* drawingCursorSink_ = nullptr;
			bool touchpadProbeEnabled_ = false;
			std::atomic<bool> pixelScalePublished_ = false;
			float inkToPixelScaleX_ = 1.0f;
			float inkToPixelScaleY_ = 1.0f;
			std::mutex pixelScaleMutex_;
			Microsoft::WRL::ComPtr<IUnknown> freeThreadedMarshaler_;
			HRESULT marshalerResult_ = E_UNEXPECTED;
			std::mutex metadataMutex_;
			std::array<TabletMetadata, kTabletMetadataCapacity> metadata_ = {};
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

	bool RtsTouchpadProbeDataInterestExactForTesting() noexcept
	{
		const uint32_t dataInterest = static_cast<uint32_t>(kTouchpadProbeDataInterest);
		const uint32_t contactInterest = static_cast<uint32_t>(
			RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp);
		return dataInterest == contactInterest;
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
#if defined(DRAW3_RTS_DIAGNOSTICS)
		bool rtsTraceEnabled = false;
#endif
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
		impl_->rtsTraceEnabled = enabled;
		ConfigureRtsTrace(enabled);
	}
#endif

	bool RealTimeStylusInput::Initialize(HWND window, ContactInputCoordinator& coordinator,
		DrawingCursorEventSink* drawingCursorSink)
	{
		if (!window || impl_->initialized) return false;
		impl_->coordinator = &coordinator;
		impl_->drawingCursorSink = drawingCursorSink;
#if defined(DRAW3_RTS_DIAGNOSTICS)
		// 本轮 trace 只探测窗口 Tablet Pen Service flags，RTS 配置保持生产基线。
		constexpr bool useTouchpadProbe = false;
#else
		constexpr bool useTouchpadProbe = false;
#endif
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
		traceState.dataInterest = static_cast<uint32_t>(useTouchpadProbe
			? kTouchpadProbeDataInterest : kProductionRtsDataInterest);
		traceState.probeName = impl_->rtsTraceEnabled ? "TabletFlags" : "none";
		traceState.probeValue = impl_->rtsTraceEnabled ? "IdtDrawpad-0x00010309" : "baseline";
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
			// trace 不改变 RTS packet description；本轮只比较窗口宿主 flags。
			const GUID* desiredPacketProperties = useTouchpadProbe
				? kIdtRtsPacketProperties.data() : kExtendedPacketProperties.data();
			const ULONG desiredPacketPropertyCount = useTouchpadProbe
				? static_cast<ULONG>(kIdtRtsPacketProperties.size())
				: static_cast<ULONG>(kExtendedPacketProperties.size());
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
					? (useTouchpadProbe ? "X,Y,Pressure,Width,Height"
						: "X,Y,Pressure,XTilt,YTilt,Azimuth,Altitude,Width,Height")
					: "X,Y";
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

		auto* plugin = new (std::nothrow) StylusSyncPlugin(
			coordinator, drawingCursorSink, useTouchpadProbe);
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
