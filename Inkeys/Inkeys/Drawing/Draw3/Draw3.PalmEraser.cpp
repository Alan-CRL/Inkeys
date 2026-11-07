#include "Draw3.PalmEraser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Inkeys::Drawing::Draw3::PalmEraser
{
	namespace
	{
		bool Positive(double value) noexcept
		{
			return std::isfinite(value) && value > 0;
		}
	}

	int RestoreRecognitionWindowMs(int saved) noexcept
	{
		return saved == 100 || saved == 150 || saved == 250 || saved == 500 ? saved : 150;
	}

	Settings NormalizeSettings(Settings settings) noexcept
	{
		if (settings.thresholdPercent < 1 || settings.thresholdPercent > 400) settings.thresholdPercent = 100;
		settings.recognitionWindowMs = RestoreRecognitionWindowMs(settings.recognitionWindowMs);
		return settings;
	}

	Scale ResolveScale(const SpeedEraser::DisplayScale& display, const SpeedEraser::InputSource& source) noexcept
	{
		using namespace SpeedEraser;
		if ((source.kind != SourceKind::Touch && source.kind != SourceKind::Unknown)
			|| display.monitor == 0 || display.pixelWidth <= 0 || display.pixelHeight <= 0
			|| !Positive(display.dipPerPixelX) || !Positive(display.dipPerPixelY)) return {};

		if (source.kind == SourceKind::Touch)
		{
			// 复用既有旋转、映射和物理密度校验；不把速度经验增益当毫米标尺。
			const auto resolved = ResolveConfig(display, DeviceMode::Automatic, source);
			if (resolved.inputMapped && resolved.motionUnit == MotionUnit::MillimetersPerSecond)
				return { resolved.motionPerPixelX, resolved.motionPerPixelY, true, false,
					display.dipPerPixelX, display.dipPerPixelY };
		}
		return { 0, 0, true, true, display.dipPerPixelX, display.dipPerPixelY };
	}

	Scale ResolvePreviewScale(const SpeedEraser::DisplayScale& display) noexcept
	{
		// 只读探测当前绘图输出，不修改真实设备来源或已应用显示快照。
		SpeedEraser::InputSource source;
		source.kind = SpeedEraser::SourceKind::Touch;
		source.mappedMonitor = display.monitor;
		source.mappedLeft = display.desktopLeft;
		source.mappedTop = display.desktopTop;
		source.mappedWidth = display.pixelWidth;
		source.mappedHeight = display.pixelHeight;
		return ResolveScale(display, source);
	}

	double ThresholdAreaPixels(const Settings& settings, const Scale& scale) noexcept
	{
		if (!scale.available) return 0;
		const auto normalized = NormalizeSettings(settings);
		const double multiplier = normalized.thresholdPercent / 100.0;
		double area = 0;
		if (scale.estimated)
		{
			if (!Positive(scale.dipPerPixelX) || !Positive(scale.dipPerPixelY)) return 0;
			// 用户确认的常规大屏回退：100×100 系统 DIP，倍率仍按面积缩放。
			area = 10000.0 * multiplier / scale.dipPerPixelX / scale.dipPerPixelY;
		}
		else
		{
			if (!Positive(scale.mmPerPixelX) || !Positive(scale.mmPerPixelY)) return 0;
			area = 2500.0 * multiplier / scale.mmPerPixelX / scale.mmPerPixelY;
		}
		return Positive(area) ? area : 0;
	}

	float ExclusionRadius(float diameterPx) noexcept
	{
		if (!Positive(diameterPx)) return 0;
		const float radius = diameterPx * 0.5f;
		return radius + std::max(radius * 0.15f, 4.0f);
	}

	void State::Begin(const Settings& preferences, const Scale& displayScale, ToolCategory tool,
		int64_t contactDownQpc, int64_t frequency, bool recognitionBlocked) noexcept
	{
		*this = {};
		settings = NormalizeSettings(preferences);
		scale = displayScale;
		blocked = recognitionBlocked;
		downQpc = deadlineQpc = contactDownQpc;
		const auto index = static_cast<size_t>(tool);
		eligible = settings.enabled && index < settings.tools.size() && settings.tools[index]
			&& !blocked && frequency > 0 && ThresholdAreaPixels(settings, scale) > 0;
		locked = !eligible;
		if (!eligible) return;

		// 先除后乘避免 QPC 频率溢出；截止样本按整数时钟的闭区间接收。
		const int64_t ticks = (frequency / 1000) * settings.recognitionWindowMs
			+ (frequency % 1000) * settings.recognitionWindowMs / 1000;
		const auto maximum = std::numeric_limits<int64_t>::max();
		deadlineQpc = contactDownQpc > maximum - ticks ? maximum : contactDownQpc + ticks;
	}

	bool State::Observe(const SpeedEraser::ContactAreaSample& sample, int64_t sampleQpc) noexcept
	{
		if (!eligible || blocked || locked || sampleQpc < downQpc) return false;
		if (sampleQpc > deadlineQpc) return Advance(sampleQpc);
		if (sample.units != SpeedEraser::ContactAreaUnits::CanvasPixels
			|| !Positive(sample.widthPx) || !Positive(sample.heightPx)) return false;
		const double width = sample.widthPx, height = sample.heightPx;
		if (std::max(width, height) / std::min(width, height) > 3.5) return false;
		const double area = width * height;
		if (area <= maximumAreaPx) return false;
		// 识别仍用外框 W×H；圆形橡皮按同宽高椭圆的等面积直径定尺。
		const double diameter = std::sqrt(area);
		if (!Positive(diameter) || diameter > std::numeric_limits<float>::max()) return false;

		maximumAreaPx = area;
		if (area >= ThresholdAreaPixels(settings, scale)) active = true;
		if (active) diameterPx = static_cast<float>(diameter);
		return true;
	}

	bool State::Advance(int64_t nowQpc) noexcept
	{
		// 到点仍可消费同 QPC 样本，下一时钟刻度才锁定；不依赖新的 Move。
		if (locked || nowQpc <= deadlineQpc) return false;
		locked = true;
		return true;
	}

	void State::End() noexcept
	{
		locked = true;
	}
}
