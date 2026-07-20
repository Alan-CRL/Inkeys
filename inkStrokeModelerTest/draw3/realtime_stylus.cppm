module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <memory>
#include <windows.h>

export module draw3.realtime_stylus;

import draw3.contact_input;

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

		// 初始化 MTA COM、RTS、多点接口和同步插件；任一步失败均返回 false。
		bool Initialize(HWND window, ContactInputCoordinator& coordinator);
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
	float NormalizeRtsPressureForTesting(int32_t rawValue,
		int32_t logicalMin, int32_t logicalMax) noexcept;
	float DecodeRtsAngleForTesting(int32_t rawValue, RtsAngleUnitForTesting unit,
		float resolution) noexcept;
	RtsStylusAnglesForTesting DecodeRtsStylusAnglesForTesting(bool hasAzimuthAltitude,
		float azimuth, float altitude, float xTilt, float yTilt) noexcept;
#endif
}
