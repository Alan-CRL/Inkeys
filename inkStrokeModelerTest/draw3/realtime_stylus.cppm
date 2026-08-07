module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <windows.h>

export module draw3.realtime_stylus;

import draw3.contact_input;
import draw3.pen_cursor;

namespace draw3
{
	struct RealTimeStylusInputImpl;
}

export namespace draw3
{
	// 以同步 RTS 插件把 Touch/Pen/Mouse contact 发布给绘制线程。
	class RealTimeStylusInput
	{
	public:
		RealTimeStylusInput();
		~RealTimeStylusInput();
		RealTimeStylusInput(const RealTimeStylusInput&) = delete;
		RealTimeStylusInput& operator=(const RealTimeStylusInput&) = delete;

#if defined(DRAW3_RTS_DIAGNOSTICS)
		// Debug 下启用有界 RTS diagnostics；Release 不导出该入口。
		void SetRtsTraceEnabled(bool enabled) noexcept;
#endif
		// 初始化 MTA COM、RTS、多点接口和同步插件；任一步失败均返回 false。
		bool Initialize(HWND window, ContactInputCoordinator& coordinator,
			DrawingCursorEventSink* drawingCursorSink = nullptr);
		// 先停止回调、移除插件，再取消生产者持有的 contact 并释放 COM。
		void Shutdown() noexcept;
		bool IsInitialized() const noexcept;

	private:
		std::unique_ptr<RealTimeStylusInputImpl> impl_;
	};

#if defined(DRAW3_TESTING)
	enum class RtsAngleUnitForTesting : uint32_t { Unsupported, Degrees, Radians };
	struct RtsStylusAnglesForTesting
	{
		float tilt = -1.0f;
		float orientation = -1.0f;
	};
	enum class RtsPacketPropertyForTesting : uint32_t
	{
		Unknown,
		X,
		Y,
		Pressure,
		XTilt,
		YTilt,
		Azimuth,
		Altitude,
		Width,
		Height
	};
	struct RtsDecoderResultForTesting
	{
		bool parsed = false;
		bool decoded = false;
		ContactSnapshot snapshot;
	};
	float NormalizeRtsPressureForTesting(int32_t rawValue,
		int32_t logicalMin, int32_t logicalMax) noexcept;
	float DecodeRtsAngleForTesting(int32_t rawValue, RtsAngleUnitForTesting unit,
		float resolution) noexcept;
	RtsStylusAnglesForTesting DecodeRtsStylusAnglesForTesting(bool hasAzimuthAltitude,
		float azimuth, float altitude, float xTilt, float yTilt) noexcept;
	SizeF DecodeRtsContactSizeForTesting(InputDeviceType deviceType,
		int32_t rawWidth, int32_t rawHeight, float packetScaleX, float packetScaleY) noexcept;
	bool RtsContactSizePropertiesRequestedForTesting() noexcept;
	bool RtsPenCursorDataInterestEnabledForTesting() noexcept;
	bool RtsProductionDataInterestIsExactForTesting() noexcept;
	RtsDecoderResultForTesting DecodeRtsContextForTesting(
		const RtsPacketPropertyForTesting* properties, size_t propertyCount,
		const int32_t* packet, size_t decodedPropertyCount, InputDeviceType deviceType,
		float positionScaleX, float positionScaleY,
		float contactScaleX, float contactScaleY) noexcept;
	size_t ComputeRtsActiveBindingCapacityForTesting(int maximumTouches) noexcept;
	bool RtsBindingBasicInvariantsForTesting() noexcept;
	bool RtsBindingNonPowerOfTwoCapacityForTesting(size_t logicalCapacity) noexcept;
	bool RtsBindingRepeatedLifecycleForTesting() noexcept;
	bool RtsBindingCollisionDeletionForTesting() noexcept;
	bool RtsBindingCollisionChurnForTesting() noexcept;
	bool RtsBindingCapacityExhaustionForTesting() noexcept;
	bool RtsBindingDuplicateRebindForTesting() noexcept;
	bool RtsBindingGenerationMismatchForTesting() noexcept;
	bool RtsLifecycleEnabledDisabledForTesting() noexcept;
	bool RtsLifecycleUpdateMappingForTesting() noexcept;
	bool RtsLifecycleTabletRemovedForTesting() noexcept;
	bool RtsLifecycleTabletAddedForTesting() noexcept;
	bool RtsLifecycleTabletAddedFallbackForTesting() noexcept;
	bool RtsSharedScaleCompatibilityForTesting() noexcept;
	bool RtsErrorPreservesDecoderLifecycleForTesting() noexcept;
	bool RtsInAirCacheHitMissForTesting() noexcept;
	bool RtsStateGateForTesting() noexcept;
#endif
}
