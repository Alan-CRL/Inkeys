#pragma once

#include "Setting.Controls.h"
#include "../../Drawing/Draw3/Draw3.DevicePerformanceModel.h"

namespace Inkeys::UI::Setting::Design
{
	// 性能页顶部的设备性能看板。只负责绘制与交互，数据与配置由 Setting.cpp 读取后传入（不依赖 Draw3 Host），
	// 动画时钟沿用设置会话的 navigationDelta 与动画开关/速率。
	struct PerformanceDashboardInput
	{
		const Inkeys::Drawing::Draw3::DevicePerformance::Report* report = nullptr;
		int configuredFps = 0;       // 配置中的画布帧率（改档后需重启，可能与测得帧率不同）
		bool recordingEnabled = true; // 配置中的记录开关
		float delta = 0.0F;
		bool animationsEnabled = true;
		float animationRate = 1.0F;
	};

	struct PerformanceDashboardEvents
	{
		bool recordingChanged = false;
		bool recordingEnabled = true;
		bool clearRequested = false;
	};

	PerformanceDashboardEvents RenderPerformanceDashboard(const PerformanceDashboardInput& input);
}
