#pragma once

namespace Inkeys::UI
{
	enum class StartupPreviewEffect : int
	{
		Shimmer = 0,
		Aurora = 1,
		PixelAssembly = 2,
	};

	// 缺失或非法选择沿用原掠光，保证旧配置和未知效果都可正常启动。
	[[nodiscard]] constexpr StartupPreviewEffect NormalizeStartupPreviewEffect(int value) noexcept
	{
		return value == 1 ? StartupPreviewEffect::Aurora
			: value == 2 ? StartupPreviewEffect::PixelAssembly
			: StartupPreviewEffect::Shimmer;
	}
}
