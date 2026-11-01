#pragma once

#include "Draw3.SpeedEraser.h"

#include <array>
#include <cstdint>

namespace Inkeys::Drawing::Draw3::PalmEraser
{
	enum class ToolCategory { Pen, HardPen, Highlighter, Laser, Eraser, Shape, Brush };
	struct Settings
	{
		bool enabled = false;
		int thresholdPercent = 100;
		int recognitionWindowMs = 150;
		bool parallelWriting = true;
		std::array<bool, 7> tools{ true, true, true, true, true, true, true };
		friend bool operator==(const Settings&, const Settings&) = default;
	};
	Settings NormalizeSettings(Settings settings) noexcept;
	int RestoreRecognitionWindowMs(int saved) noexcept;

	struct Scale
	{
		double mmPerPixelX = 0, mmPerPixelY = 0;
		bool available = false, estimated = false;
		// 物理标尺失效时使用系统 DIP，不把估算伪装成毫米密度。
		double dipPerPixelX = 1, dipPerPixelY = 1;
		friend bool operator==(const Scale&, const Scale&) = default;
	};
	// 调用者先核验真实 InputDeviceType::Touch；Unknown 来源仅允许保守的 DPI 估算。
	Scale ResolveScale(const SpeedEraser::DisplayScale& display, const SpeedEraser::InputSource& source) noexcept;
	Scale ResolvePreviewScale(const SpeedEraser::DisplayScale& display) noexcept;
	double ThresholdAreaPixels(const Settings& settings, const Scale& scale) noexcept;
	float ExclusionRadius(float diameterPx) noexcept;

	struct State
	{
		Settings settings;
		Scale scale;
		bool eligible = false, blocked = false, active = false, locked = true;
		double maximumAreaPx = 0;
		float diameterPx = 0;
		int64_t downQpc = 0, deadlineQpc = 0;

		void Begin(const Settings& preferences, const Scale& displayScale, ToolCategory tool,
			int64_t contactDownQpc, int64_t frequency, bool recognitionBlocked = false) noexcept;
		// 可在副本上观察，原工具到橡皮的准备成功后再由 owner 提交该副本。
		bool Observe(const SpeedEraser::ContactAreaSample& sample, int64_t sampleQpc) noexcept;
		bool Advance(int64_t nowQpc) noexcept;
		// 首次物理 Up 即封窗；真正续接复制旧状态，不再次 Begin。
		void End() noexcept;
	};
}
