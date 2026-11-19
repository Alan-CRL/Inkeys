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

	// 软件调试页“设备性能看板预览”：用虚拟报告替换显示，便于检查各档位与状态；
	// 只影响看板显示，不影响绘制线程记录、账本统计与文件。下拉序号与 i18n 选项顺序一致。
	enum class PerformanceDashboardPreview : int
	{
		Off = 0,       // 默认：显示真实数据
		Empty,         // 无数据
		Ample,         // 充裕（约 25%）
		Moderate,      // 适中（约 65%），部分工具暂无数据
		Tight,         // 偏紧（约 90%）
		Overloaded,    // 超载（约 140%）
		Previous,      // 设置变更前的数据
		Count
	};
	Inkeys::Drawing::Draw3::DevicePerformance::Report MakePerformanceDashboardPreview(
		PerformanceDashboardPreview preview, int frameRate);
	// 当前预览选择：只在本次进程内有效，不写配置，重启后恢复“默认”。只在设置渲染线程读写。
	PerformanceDashboardPreview& PerformanceDashboardPreviewSelection() noexcept;
}
