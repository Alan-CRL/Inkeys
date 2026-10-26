#pragma once

#include "imgui/imgui.h"
#include <functional>

namespace Inkeys::UI::Setting::Design
{
	inline constexpr float EraserDemoCanvasWidth = 768.0F;
	inline constexpr float EraserDemoCanvasHeight = 432.0F;
	struct EraserDemoPresentation
	{
		ImTextureID texture = 0;
		int scene = 0;
		float opacity = 1.0F;
	};
	// 页面只报告可见性与实际画布宽度，资源/播放仍由设置渲染线程持有。
	using EraserDemoPresenter = std::function<EraserDemoPresentation(bool visible, float canvasWidthPixels)>;
}
