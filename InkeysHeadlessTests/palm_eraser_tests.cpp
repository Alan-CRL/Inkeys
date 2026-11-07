#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
	namespace Palm = Inkeys::Drawing::Draw3::PalmEraser;
	namespace Speed = Inkeys::Drawing::Draw3::SpeedEraser;

	Speed::ContactAreaSample Area(float width, float height,
		Speed::ContactAreaUnits units = Speed::ContactAreaUnits::CanvasPixels)
	{
		return { width * 10, height * 10, width, height, units };
	}

	Speed::DisplayScale Display()
	{
		Speed::DisplayScale display;
		display.monitor = 42;
		display.generation = display.revision = 1;
		display.logicalOutputKnown = true;
		display.pixelWidth = 1920;
		display.pixelHeight = 1080;
		display.desktopLeft = -1920;
		display.physicalAvailable = true;
		display.cmPerPixelX = display.cmPerPixelY = 0.05f;
		return display;
	}

	Speed::InputSource Source(const Speed::DisplayScale& display,
		Speed::SourceKind kind = Speed::SourceKind::Touch)
	{
		Speed::InputSource source;
		source.kind = kind;
		source.mappedMonitor = display.monitor;
		source.mappedLeft = display.desktopLeft;
		source.mappedTop = display.desktopTop;
		source.mappedWidth = display.pixelWidth;
		source.mappedHeight = display.pixelHeight;
		return source;
	}

	bool Near(double actual, double expected, double tolerance = 1e-6)
	{
		return std::abs(actual - expected) <= tolerance * std::max(1.0, std::abs(expected));
	}
}

int RunPalmEraserTests()
{
	int cases = 0, failures = 0;
	const auto expect = [&](bool condition, const char* name)
	{
		++cases;
		if (!condition)
		{
			++failures;
			std::cerr << "[PalmEraser] failed: " << name << '\n';
		}
	};
	Palm::Settings settings;
	expect(!settings.enabled && settings.thresholdPercent == 100 && settings.recognitionWindowMs == 150
		&& settings.parallelWriting, "default preferences");
	for (bool allowed : settings.tools) expect(allowed, "all seven tools include editable brush by default");
	for (int value : { 100, 150, 250, 500 })
		expect(Palm::RestoreRecognitionWindowMs(value) == value, "recognition preset roundtrip");
	for (int value : { -1, 0, 99, 151, 1000, std::numeric_limits<int>::max() })
		expect(Palm::RestoreRecognitionWindowMs(value) == 150, "invalid recognition window restores recommendation");
	for (int value : { 1, 9, 10, 24, 25, 100, 400 })
	{
		settings.thresholdPercent = value;
		expect(Palm::NormalizeSettings(settings).thresholdPercent == value, "valid area multiplier remains unchanged");
	}
	for (int value : { -1, 0, 401, std::numeric_limits<int>::max() })
	{
		settings.thresholdPercent = value;
		expect(Palm::NormalizeSettings(settings).thresholdPercent == 100, "invalid area multiplier restores default");
	}
	settings = {};
	settings.enabled = true;
	const Palm::Scale unitScale{ 1, 1, true, false };
	expect(Near(Palm::ThresholdAreaPixels(settings, unitScale), 2500), "25 square centimeters is 2500 square millimeters");
	for (int percent : { 1, 10, 25, 100, 400 })
	{
		settings.thresholdPercent = percent;
		expect(Near(Palm::ThresholdAreaPixels(settings, unitScale), percent * 25.0), "multiplier scales area, not linear width");
	}
	settings.thresholdPercent = 100;
	expect(Near(Palm::ThresholdAreaPixels(settings, { 2, 0.5, true, false }), 2500), "anisotropic scale preserves pixel area product");
	expect(Palm::ThresholdAreaPixels(settings, {}) == 0
		&& Palm::ThresholdAreaPixels(settings, { 0, 1, true, false }) == 0, "missing scale cannot invent a physical threshold");

	auto display = Display();
	const auto published = display;
	const auto source = Source(display);
	const auto physical = Palm::ResolveScale(display, source);
	expect(physical.available && !physical.estimated && Near(physical.mmPerPixelX, 0.5)
		&& Near(Palm::ThresholdAreaPixels(settings, physical), 10000), "mapped trusted physical display scale");
	expect(Palm::ResolvePreviewScale(display) == physical, "preview consumes same applied drawpad scale");
	expect(display == published, "read-only scale inspection preserves applied snapshot");
	for (auto kind : { Speed::SourceKind::Mouse, Speed::SourceKind::ExternalPen,
		Speed::SourceKind::IntegratedPen, Speed::SourceKind::TouchPad })
		expect(!Palm::ResolveScale(display, Source(display, kind)).available, "explicit non-touch input never recognizes a palm");
	const auto unknown = Palm::ResolveScale(display, Source(display, Speed::SourceKind::Unknown));
	expect(unknown.available && unknown.estimated && unknown.mmPerPixelX == 0
		&& Near(Palm::ThresholdAreaPixels(settings, unknown), 10000),
		"legacy actual Touch with unknown source uses DPI estimate only");
	for (int mapping = 0; mapping < 6; ++mapping)
	{
		auto mismatched = source;
		if (mapping == 0) mismatched.mappedMonitor = 0;
		if (mapping == 1) ++mismatched.mappedMonitor;
		if (mapping == 2) ++mismatched.mappedWidth;
		if (mapping == 3) ++mismatched.mappedHeight;
		if (mapping == 4) ++mismatched.mappedLeft;
		if (mapping == 5) ++mismatched.mappedTop;
		expect(Palm::ResolveScale(display, mismatched).estimated, "physical scale requires exact real input mapping");
	}
	display.physicalAvailable = false;
	const auto dpi96 = Palm::ResolveScale(display, source);
	expect(dpi96.available && dpi96.estimated && dpi96.mmPerPixelX == 0
		&& Near(Palm::ThresholdAreaPixels(settings, dpi96), 10000), "96 DPI fallback uses 100 by 100 system DIP without inventing millimeters");
	display.pixelWidth *= 2;
	display.pixelHeight *= 2;
	expect(Palm::ResolvePreviewScale(display) == dpi96, "DPI fallback never uses speed resolution heuristic");
	display.dipPerPixelX = display.dipPerPixelY = 0.5f;
	const auto dpi192 = Palm::ResolvePreviewScale(display);
	expect(Near(Palm::ThresholdAreaPixels(settings, dpi192), 40000),
		"192 DPI estimate has four times the pixel area");
	display.dipPerPixelX = display.dipPerPixelY = 2.0f / 3.0f;
	const auto dpi144 = Palm::ResolvePreviewScale(display);
	expect(Near(Palm::ThresholdAreaPixels(settings, dpi144), 22500), "150 percent system scaling converts 100 DIP to 150 pixels");
	settings.thresholdPercent = 25;
	expect(Near(Palm::ThresholdAreaPixels(settings, dpi144), 5625), "DPI fallback multiplier changes area rather than side length");
	settings.thresholdPercent = 10;
	expect(Near(Palm::ThresholdAreaPixels(settings, dpi144), 2250), "10 percent DIP fallback uses one tenth of system-scaled reference area");
	// 新下限必须参与真实识别，不能只在界面或配置中保留下来。
	Palm::State lowThreshold;
	lowThreshold.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	lowThreshold.Observe(Area(10, 24), 1000);
	expect(!lowThreshold.active, "10 percent physical threshold rejects area below 250 square millimeters");
	lowThreshold.Observe(Area(10, 25), 1050);
	expect(lowThreshold.active && Near(lowThreshold.maximumAreaPx, 250), "10 percent physical threshold activates at exact area boundary");
	lowThreshold.Begin(settings, dpi96, Palm::ToolCategory::Pen, 1000, 1000);
	lowThreshold.Observe(Area(20, 49), 1000);
	expect(!lowThreshold.active, "10 percent DIP threshold rejects area below 1000 square DIP");
	lowThreshold.Observe(Area(20, 50), 1050);
	expect(lowThreshold.active && Near(lowThreshold.maximumAreaPx, 1000), "10 percent DIP threshold activates at exact area boundary");
	settings.thresholdPercent = 1;
	expect(Near(Palm::ThresholdAreaPixels(settings, dpi144), 225), "1 percent DIP fallback retains system scaling");
	// 1% 下限同样参与实际识别，cm 和系统 DIP 两条标尺均按面积缩放。
	lowThreshold.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	lowThreshold.Observe(Area(5, 4), 1000);
	expect(!lowThreshold.active, "1 percent physical threshold rejects area below 25 square millimeters");
	lowThreshold.Observe(Area(5, 5), 1050);
	expect(lowThreshold.active && Near(lowThreshold.maximumAreaPx, 25), "1 percent physical threshold activates at exact area boundary");
	lowThreshold.Begin(settings, dpi96, Palm::ToolCategory::Pen, 1000, 1000);
	lowThreshold.Observe(Area(10, 9), 1000);
	expect(!lowThreshold.active, "1 percent DIP threshold rejects area below 100 square DIP");
	lowThreshold.Observe(Area(10, 10), 1050);
	expect(lowThreshold.active && Near(lowThreshold.maximumAreaPx, 100), "1 percent DIP threshold activates at exact area boundary");
	settings.thresholdPercent = 100;
	Palm::State fallbackPalm;
	fallbackPalm.Begin(settings, dpi96, Palm::ToolCategory::Pen, 1000, 1000);
	expect(fallbackPalm.Observe(Area(100, 100), 1050) && fallbackPalm.active,
		"EDID unavailable keeps default-enabled screen palm recognition usable");
	display = Display();
	display.development.scale = Speed::ScaleOverride::ManualSurface;
	display.development.calibration = { display.monitor, 60, 34, 0 };
	const auto manual = Palm::ResolveScale(display, Source(display));
	expect(manual.available && !manual.estimated && Near(manual.mmPerPixelX, 600.0 / 1920)
		&& Near(manual.mmPerPixelY, 340.0 / 1080), "existing manual calibration is reused");
	display.orientation = 1;
	display.pixelWidth = 1080;
	display.pixelHeight = 1920;
	const auto rotated = Palm::ResolvePreviewScale(display);
	expect(!rotated.estimated && Near(rotated.mmPerPixelX, 340.0 / 1080)
		&& Near(rotated.mmPerPixelY, 600.0 / 1920), "manual dimensions follow existing rotation rules");
	display.development.calibration.monitor = 999;
	expect(Palm::ResolvePreviewScale(display).estimated, "manual calibration cannot cross monitors");
	display.development.calibration = { display.monitor, 4, 34, 0 };
	expect(Palm::ResolvePreviewScale(display).estimated, "invalid manual dimensions fall back to DPI");
	display = Display();
	display.development.scale = Speed::ScaleOverride::ManualSurface;
	display.development.calibration = { display.monitor, 1000, 1000, 0 };
	display.dipPerPixelX = display.dipPerPixelY = 0.01f;
	expect(Palm::ResolvePreviewScale(display).estimated, "manual calibration retains existing density validation");
	display = Display();
	display.cmPerPixelX = std::numeric_limits<float>::infinity();
	expect(Palm::ResolvePreviewScale(display).estimated, "invalid physical density cannot become trusted");
	display = Display();
	display.development.scale = Speed::ScaleOverride::ForceUnavailable;
	expect(Palm::ResolvePreviewScale(display).estimated, "forced physical unavailability remains estimated");
	display = Display();
	display.monitor = 0;
	expect(!Palm::ResolvePreviewScale(display).available, "unpublished display has no invented preview");
	display = Display();
	display.dipPerPixelX = std::numeric_limits<float>::quiet_NaN();
	expect(!Palm::ResolvePreviewScale(display).available, "invalid DPI has no invented millimeters");

	// 椭圆模型只决定圆直径；默认 DIP 识别门槛仍是外框面积 10000。
	for (const auto& dimensions : std::array{ std::array{100.0f,100.0f,100.0f},
		std::array{160.0f,90.0f,120.0f}, std::array{90.0f,160.0f,120.0f}, std::array{320.0f,180.0f,240.0f} })
	{
		Palm::State sized;
		sized.Begin(settings, dpi96, Palm::ToolCategory::Pen, 1000, 1000);
		expect(sized.Observe(Area(dimensions[0],dimensions[1]), 1000) && sized.active
			&& Near(sized.diameterPx, dimensions[2]), "ellipse-area diameter is symmetric and scales with the full contact bounds");
	}
	for (const float height : { 99.0f, 100.0f, 101.0f })
	{
		Palm::State boundary;
		boundary.Begin(settings, dpi96, Palm::ToolCategory::Pen, 1000, 1000);
		expect(boundary.Observe(Area(100,height), 1000) && boundary.active == (height >= 100)
			&& Near(boundary.maximumAreaPx, 100*height), "ellipse sizing preserves below/equal/above rectangle recognition threshold");
	}

	Palm::State state;
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	expect(state.eligible && !state.active && !state.locked && state.deadlineQpc == 1150, "new contact latches bounded recognition");
	expect(state.Observe(Area(49, 50), 1000) && !state.active, "below-threshold contact remains original tool");
	expect(!state.Observe(Area(100, 100), 999) && state.maximumAreaPx == 2450, "sample before independent Down is ignored");
	expect(state.Observe(Area(50, 50), 1100) && state.active
		&& Near(state.diameterPx, 50), "threshold equality activates immediately without movement");
	const auto firstDiameter = state.diameterPx;
	expect(!state.Observe(Area(40, 40), 1110) && state.diameterPx == firstDiameter, "smaller sample cannot shrink accepted maximum");
	expect(state.Observe(Area(100, 100), 1150) && Near(state.diameterPx, 100),
		"deadline-inclusive larger sample updates provisional diameter");
	expect(!state.Advance(1150) && !state.locked, "deadline equality permits its real sample");
	expect(state.Advance(1151) && state.locked && !state.Advance(1160), "idle advance locks exactly once after deadline");
	const auto lockedDiameter = state.diameterPx;
	expect(!state.Observe(Area(300, 300), 1160) && state.diameterPx == lockedDiameter, "expired contact never breathes with new area");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	expect(state.Observe(Area(100, 100), 1151) && state.locked && !state.active && state.maximumAreaPx == 0,
		"first qualifying sample outside window only locks original drawing");
	state.Begin(settings, unitScale, Palm::ToolCategory::Eraser, 1000, 1000);
	state.Observe(Area(50, 50), 1000);
	state.End();
	const auto earlyUp = state;
	expect(state.active && state.locked && !state.Observe(Area(100, 100), 1100)
		&& state.diameterPx == earlyUp.diameterPx, "early physical Up freezes active palm size");
	state = earlyUp;
	expect(!state.Observe(Area(1, 1), 1200) && state.diameterPx == earlyUp.diameterPx && state.active,
		"copied reconnect palm keeps same locked size with smaller contact");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	state.End();
	auto continuedDrawing = state;
	expect(!continuedDrawing.Observe(Area(100, 100), 1100) && !continuedDrawing.active,
		"early-Up ordinary drawing cannot gain recognition on successful continuation");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	auto staged = state;
	staged.Observe(Area(100, 100), 1010);
	expect(staged.active && !state.active && state.maximumAreaPx == 0,
		"staged promotion leaves original state intact if model preparation fails");
	state = staged;
	expect(state.active && state.maximumAreaPx == 10000, "successful promotion can commit prepared state");

	for (int unit = 0; unit <= static_cast<int>(Speed::ContactAreaUnits::InvalidDigitizerScale); ++unit)
	{
		if (unit == static_cast<int>(Speed::ContactAreaUnits::CanvasPixels)) continue;
		state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
		expect(!state.Observe(Area(100, 100, static_cast<Speed::ContactAreaUnits>(unit)), 1000)
			&& !state.active && state.maximumAreaPx == 0, "unverified or invalid units never substitute for real area");
	}
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	for (auto invalid : std::array{ Area(0, 100), Area(100, 0), Area(-1, 100), Area(100, -1),
		Area(nan, 100), Area(100, nan), Area(inf, 100), Area(100, inf), Area(351, 100), Area(100, 351) })
	{
		state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
		expect(!state.Observe(invalid, 1000) && !state.active, "invalid or thin contact never recognizes");
	}
	for (auto boundary : { Area(350, 100), Area(100, 350) })
	{
		state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
		expect(state.Observe(boundary, 1000) && state.active, "aspect ratio equality at 3.5 remains valid");
	}
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	expect(state.Observe(Area(500, 500), 1000) && Near(state.diameterPx, 500),
		"palm uses full real area independently of speed eraser span and diameter caps");
	for (int tool = 0; tool < 7; ++tool)
	{
		const auto category = static_cast<Palm::ToolCategory>(tool);
		state.Begin(settings, unitScale, category, 1000, 1000);
		expect(state.Observe(Area(50, 50), 1000) && state.active, "all seven selected tool categories recognize");
		auto disabledTool = settings;
		disabledTool.tools[tool] = false;
		state.Begin(disabledTool, unitScale, category, 1000, 1000);
		expect(!state.eligible && !state.Observe(Area(100, 100), 1000), "per-tool opt-out preserves original tool");
	}
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000, true);
	expect(state.blocked && !state.eligible && !state.Observe(Area(500, 500), 1100),
		"neighbor block latches for the complete independent contact");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, 1000);
	expect(!state.blocked && state.eligible && !state.active && state.maximumAreaPx == 0,
		"pool reuse begins fresh recognition without old palm state");
	const auto latched = settings;
	settings.enabled = false;
	settings.thresholdPercent = 400;
	settings.recognitionWindowMs = 500;
	expect(state.Observe(Area(50, 50), 1100) && state.active && state.settings == latched && state.deadlineQpc == 1150,
		"preferences changed after Down cannot rewrite active recognition window or threshold");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1200, 1000);
	expect(!state.eligible && !state.Observe(Area(500, 500), 1200), "master opt-out applies to next independent contact");
	settings.enabled = true;
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1200, 1000);
	expect(state.deadlineQpc == 1700 && !state.active, "next independent contact latches updated window");
	for (int64_t frequency : { int64_t{ 0 }, int64_t{ -1 } })
	{
		state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 1000, frequency);
		expect(!state.eligible, "invalid QPC frequency never invents a window");
	}
	state.Begin(settings, {}, Palm::ToolCategory::Pen, 1000, 1000);
	expect(!state.eligible, "unavailable display scale leaves drawing intact");
	state.Begin(settings, unitScale, static_cast<Palm::ToolCategory>(7), 1000, 1000);
	expect(!state.eligible, "unknown category cannot index tool preferences");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, std::numeric_limits<int64_t>::max() - 5, 1000);
	expect(state.deadlineQpc == std::numeric_limits<int64_t>::max(), "deadline addition cannot wrap signed QPC");
	state.Begin(settings, unitScale, Palm::ToolCategory::Pen, 0, std::numeric_limits<int64_t>::max());
	expect(state.deadlineQpc > 0 && state.deadlineQpc < std::numeric_limits<int64_t>::max(), "large QPC frequency cannot overflow multiplication");
	expect(Near(Palm::ExclusionRadius(40), 24) && Near(Palm::ExclusionRadius(100), 57.5),
		"exclusion extends radius by larger of fifteen percent and four pixels");
	expect(Palm::ExclusionRadius(0) == 0 && Palm::ExclusionRadius(-1) == 0
		&& Palm::ExclusionRadius(nan) == 0 && Palm::ExclusionRadius(inf) == 0, "invalid palm diameter creates no exclusion region");
	std::cout << "[PalmEraser] cases=" << cases << " failures=" << failures << '\n';
	return failures;
}
