#pragma once

#include "Setting.EraserDemo.h"
#include <array>
#include <cstdint>
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace Inkeys::UI::Setting::Design
{
	struct EraserDemoStats
	{
		std::uint64_t renders = 0, shaderBuilds = 0, targetAllocations = 0, renderedPixels = 0;
		unsigned width = 0, height = 0;
		int scene = 0, frame = 0;
	};

	// 只由设置的共享渲染线程持有。输出仅交给普通 ImGui Image（PS slot 0），不安装绘制回调。
	class EraserDemoPlayer
	{
		struct State;
		std::unique_ptr<State> state;
	public:
		EraserDemoPlayer();
		~EraserDemoPlayer();
		EraserDemoPlayer(const EraserDemoPlayer&) = delete;
		EraserDemoPlayer& operator=(const EraserDemoPlayer&) = delete;
		EraserDemoPresentation Prepare(ID3D11Device* device, ID3D11DeviceContext* context,
			const std::array<ID3D11ShaderResourceView*, 2>& atlases, bool visible, float canvasWidthPixels,
			float delta, bool animations, float rate, bool dark);
		// Hide/会话及 device epoch 失效沿现有设置生命周期显式撤销，计数与 revision 保持单调。
		void Reset() noexcept;
		[[nodiscard]] std::uint64_t Revision() const noexcept;
		[[nodiscard]] const EraserDemoStats& Stats() const noexcept;
		[[nodiscard]] long LastResult() const noexcept;
		[[nodiscard]] const char* LastError() const noexcept;
	};
}
