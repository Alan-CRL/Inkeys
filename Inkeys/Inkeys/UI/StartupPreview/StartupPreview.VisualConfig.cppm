module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

export module Inkeys.UI.StartupPreview.VisualConfig;
export import Inkeys.UI.StartupPreview.Format;
export import Inkeys.Other.Config;

export namespace Inkeys::UI::StartupPreview
{
	[[nodiscard]] inline VisualRuntimeState CanonicalVisualRuntimeState() noexcept
	{
		VisualRuntimeState runtime;
		// 默认底栏从主按钮向右展开；其余字段保持干净选择态。
		runtime.mainBarRight = true;
		runtime.bottomDockMode = 1;
		runtime.bottomDockCenterMode = 1;
		return runtime;
	}

	[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t>
		VisualButtonSize(Inkeys::BarButtonSizeKind size) noexcept
	{
		switch (size)
		{
		case Inkeys::BarButtonSizeKind::TwoOne: return { 2, 1 };
		case Inkeys::BarButtonSizeKind::OneTwo: return { 1, 2 };
		case Inkeys::BarButtonSizeKind::OneOne: return { 1, 1 };
		case Inkeys::BarButtonSizeKind::TwoTwo:
		default: return { 2, 2 };
		}
	}

	[[nodiscard]] inline VisualSignatureInputs BuildConfiguredVisualInputs(
		const Inkeys::Config& config)
	{
		VisualSignatureInputs inputs;
		inputs.layoutEpoch = 1;
		inputs.language = "zh-CN";
		inputs.theme = "dark";
		inputs.zoomPermille = static_cast<std::uint32_t>(std::llround(
			(std::clamp)(static_cast<double>(config.UI.Bar.Zoom), 0.1, 5.0) * 1000.0));
		inputs.edgeLightingEnabled =
			config.Experimental.Inkeys3.UI3.EdgeLighting.Enable;
		inputs.dynamicEdgeLightingEnabled =
			config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic;
		inputs.debugOverlayEnabled = config.Experimental.Inkeys3.UI3.Debug.Enable;
		inputs.runtimeState = CanonicalVisualRuntimeState();
		auto appendFixed = [](auto& target, const auto& entries)
			{
				for (const auto& entry : entries)
				{
					const auto [width, height] = VisualButtonSize(entry.Size);
					target.push_back({ entry.Id, width, height, true });
				}
			};
		appendFixed(inputs.fixedButtonsA1, config.UI.Bar.FixedButtonsA1.Snapshot());
		for (const auto& entry : config.UI.Bar.ExtensionButtons.Snapshot())
		{
			const auto [width, height] = VisualButtonSize(entry.Size);
			inputs.extensionButtons.push_back(
				{ entry.Id, width, height, entry.Visible });
		}
		appendFixed(inputs.fixedButtonsA2, config.UI.Bar.FixedButtonsA2.Snapshot());
		return inputs;
	}
}
