#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <windows.h>

import Inkeys.Drawing.Draw3.canvas_debug;

namespace
{
	using namespace Inkeys::Drawing::Draw3::CanvasDebug;

	struct Checks
	{
		int failures = 0;
		void operator()(bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::cerr << "[CanvasDebug] failed: " << name << '\n';
		}
	};

	bool Empty(const RECT& rect) { return rect.left >= rect.right || rect.top >= rect.bottom; }

	bool Contains(const RECT& outer, const RECT& inner)
	{
		return outer.left <= inner.left && outer.top <= inner.top &&
			outer.right >= inner.right && outer.bottom >= inner.bottom;
	}

	FrameParams Params(double now, bool markers = false)
	{
		FrameParams params;
		params.nowMs = now;
		params.canvasWidth = 800;
		params.canvasHeight = 600;
		params.dpiScale = 1.0f;
		params.targetFps = 60.0;
		params.markers = markers;
		return params;
	}

	ContactSample Touch(std::uint64_t key, float x, float y, float width = 40.0f, float height = 30.0f,
		float palmThreshold = 1000.0f, float cmPerPixel = 0.0f)
	{
		ContactSample sample;
		sample.key = key;
		sample.kind = ContactKind::Touch;
		sample.x = x;
		sample.y = y;
		sample.pressure = 0.5f;
		sample.areaWidthPx = width;
		sample.areaHeightPx = height;
		sample.palmThresholdAreaPx = palmThreshold;
		sample.cmPerPixelX = sample.cmPerPixelY = cmPerPixel;
		return sample;
	}

	void CheckPureFunctions(Checks& check)
	{
		check(ShouldShowSolidAreaBox(40, 30, 1000), "area above the palm threshold is solid");
		check(ShouldShowSolidAreaBox(40, 25, 1000), "area equal to the palm threshold is solid, matching State::Observe");
		check(!ShouldShowSolidAreaBox(40, 24, 1000), "area below the threshold stays dashed");
		check(!ShouldShowSolidAreaBox(40, 30, 0), "palm eraser disabled or unavailable never solid");
		check(!ShouldShowSolidAreaBox(80, 20, 1000), "aspect ratio above 3.5 is not a palm candidate");
		check(!ShouldShowSolidAreaBox(-1, 30, 1000) && !ShouldShowSolidAreaBox(NAN, 30, 1000), "unknown or non-finite area is never solid");

		check(BlendPremultipliedOver(0u, 255, 0, 0, 128) == 0x80800000u, "half red over transparent is premultiplied");
		check(BlendPremultipliedOver(0xFF102030u, 1, 2, 3, 0) == 0xFF102030u, "zero alpha keeps the destination");
		const std::uint32_t opaque = BlendPremultipliedOver(0xFF000000u, 255, 255, 255, 255);
		check(opaque == 0xFFFFFFFFu, "opaque source replaces the destination");
		const std::uint32_t half = BlendPremultipliedOver(0xFF000000u, 255, 255, 255, 128);
		check((half >> 24) == 255 && ((half >> 16) & 0xFF) == 128, "half white over opaque black stays opaque");
	}

	void CheckPlateAndDirty(Checks& check)
	{
		Overlay overlay;
		const RECT first = overlay.Update(Params(1000.0));
		check(overlay.PlateVisible(), "plate visible after the first update");
		const RECT plate = overlay.PlateRect();
		const int anchorLeft = std::max(0, (800 - overlay.PlateCapacityWidth()) / 2);
		const int anchorTop = std::max(0, (600 - overlay.PlateCapacityHeight()) / 2);
		check(plate.left == anchorLeft && plate.right - plate.left == overlay.PlateWidth() &&
			plate.bottom - plate.top == overlay.PlateHeight(), "plate matches its bitmap size at the anchor");
		check(plate.top == anchorTop, "the top edge is anchored where the full table would be vertically centered");
		check(Contains(first, plate), "first update dirties the whole plate");
		check(overlay.ConsumePlateUpload() && !overlay.ConsumePlateUpload(), "plate upload is requested once per redraw");

		const int stride = overlay.PlateCapacityWidth();
		const std::uint32_t* pixels = overlay.PlatePixels();
		const std::uint32_t body = pixels[(overlay.PlateHeight() - 3) * stride + 3];
		check((body >> 24) > 150 && ((body >> 16) & 0xFF) <= (body >> 24), "plate body is translucent premultiplied");
		const std::uint32_t corner = pixels[0 * stride + overlay.PlateWidth() - 1];
		check((corner >> 24) < 60, "right-hand corners are rounded");
		check((pixels[0] >> 24) < 60, "left-hand corners are rounded because the plate floats");
		int bright = 0;
		for (int y = 0; y < overlay.PlateHeight(); ++y)
			for (int x = 0; x < overlay.PlateWidth(); ++x)
				if (((pixels[y * stride + x] >> 16) & 0xFF) > 150 && (pixels[y * stride + x] >> 24) > 150) ++bright;
		check(bright > 40, "plate text and chart are rasterized");

		check(Empty(overlay.Update(Params(1100.0))), "an unchanged plate produces no dirty region");
		const double wait = overlay.MillisecondsUntilNextUpdate(1100.0);
		check(wait > 300.0 && wait <= 400.0 + 1.0, "next wake is the 0.5 s snapshot");
	}

	void CheckPerformanceSnapshot(Checks& check)
	{
		Overlay overlay;
		overlay.Update(Params(1000.0));
		for (int i = 0; i < 30; ++i) overlay.OnFrame(2.0, true);
		for (int i = 0; i < 5; ++i) overlay.OnFrame(50.0, false); // 调试自刷新帧不计入。
		const RECT dirty = overlay.Update(Params(1500.0));
		const PerformanceSnapshot shot = overlay.Performance();
		check(std::abs(shot.fps - 60.0) < 0.01, "current fps is frames over the window");
		check(std::abs(shot.unlimitedFps - 500.0) < 0.01, "unlimited fps excludes pacing waits");
		check(std::abs(shot.loadPercent - 12.0) < 0.01, "frame load is work time over the target interval");
		check(shot.chartCount == 1 && !Empty(dirty), "a changed snapshot extends the chart and dirties the plate");
		// 空闲画布：数值与曲线稳定（曲线满 120 点且全为 0）后不再产生脏区。
		int stableFrames = 0;
		for (int i = 0; i < 140; ++i)
		{
			const RECT result = overlay.Update(Params(2000.0 + 500.0 * i));
			// 进程 CPU 以 15.6 ms 计时粒度量化，偶发跳变允许；其余时间不得产生脏区。
			if (i >= 130 && Empty(result)) ++stableFrames;
		}
		check(overlay.Performance().chartCount == 120, "chart keeps 120 samples (60 seconds)");
		check(stableFrames >= 8, "an idle stable plate stops refreshing");
	}

	void CheckContactsAndMarkers(Checks& check)
	{
		Overlay overlay;
		overlay.Update(Params(1000.0));
		const int heightNone = overlay.PlateHeight();

		ContactSample pen;
		pen.key = 2;
		pen.kind = ContactKind::Pen;
		pen.x = 300;
		pen.y = 250;
		const ContactSample touch = Touch(1, 100, 200);

		overlay.BeginContacts();
		overlay.ObserveContact(touch);
		overlay.Update(Params(1010.0));
		const int heightOne = overlay.PlateHeight();
		overlay.BeginContacts();
		overlay.ObserveContact(touch);
		overlay.ObserveContact(pen);
		overlay.Update(Params(1020.0));
		const int heightTwo = overlay.PlateHeight();
		check(heightNone < heightOne && heightOne < heightTwo, "plate grows one row per active contact");
		check(overlay.PlateWidth() > 0 && overlay.PlateCapacityHeight() >= heightTwo, "capacity covers the full table");

		// 可视化模式：标记。
		overlay.BeginContacts();
		overlay.ObserveContact(touch);
		overlay.ObserveContact(pen);
		RECT dirty = overlay.Update(Params(1030.0, true));
		const auto markers = overlay.Markers();
		check(markers[0].active && markers[0].hasBox && markers[0].solidBox, "touch above the palm threshold has a solid box");
		check(markers[1].active && !markers[1].hasBox && !markers[1].hasLabel, "a contact without area has no box or label");
		check(markers[0].hasLabel && markers[0].labelWidth > 0 && Contains(markers[0].bounds,
			RECT{ markers[0].labelX, markers[0].labelY, markers[0].labelX + markers[0].labelWidth,
				markers[0].labelY + markers[0].labelHeight }), "marker bounds include the label");
		check(Contains(dirty, markers[0].bounds) && Contains(dirty, markers[1].bounds), "new markers dirty their bounds");
		int labelWidth = 0, labelHeight = 0;
		check(overlay.ConsumeLabelUpload(0, labelWidth, labelHeight) && labelWidth == markers[0].labelWidth &&
			labelHeight == markers[0].labelHeight, "label upload reports the tile size");
		check(!overlay.ConsumeLabelUpload(0, labelWidth, labelHeight), "label upload is consumed once");
		const int labelHeightPx = markers[0].labelHeight;

		overlay.BeginContacts();
		overlay.ObserveContact(touch);
		overlay.ObserveContact(pen);
		check(Empty(overlay.Update(Params(1040.0, true))), "unchanged markers and rows add no dirty region");

		ContactSample moved = touch;
		moved.x += 6;
		const RECT before = markers[0].bounds;
		overlay.BeginContacts();
		overlay.ObserveContact(moved);
		overlay.ObserveContact(pen);
		dirty = overlay.Update(Params(1050.0, true));
		check(Contains(dirty, before) && Contains(dirty, overlay.Markers()[0].bounds),
			"a moved marker dirties the previous and current bounds");

		// 阈值为 0（手掌橡皮关闭/非 Touch）始终为虚线；小于阈值同样为虚线。
		ContactSample dashed = moved;
		dashed.palmThresholdAreaPx = 0.0f;
		overlay.BeginContacts();
		overlay.ObserveContact(dashed);
		overlay.Update(Params(1060.0, true));
		check(overlay.Markers()[0].hasBox && !overlay.Markers()[0].solidBox, "disabled palm eraser keeps the dashed box");
		dashed.palmThresholdAreaPx = 5000.0f;
		overlay.BeginContacts();
		overlay.ObserveContact(dashed);
		overlay.Update(Params(1070.0, true));
		check(!overlay.Markers()[0].solidBox, "area below the threshold keeps the dashed box");

		// 可信 EDID 时标签多一行 cm。
		ContactSample withCm = Touch(1, 100, 200, 40, 30, 1000.0f, 0.05f);
		overlay.BeginContacts();
		overlay.ObserveContact(withCm);
		overlay.Update(Params(1080.0, true));
		check(overlay.Markers()[0].labelHeight > labelHeightPx, "trusted physical scale adds a cm line to the label");

		// 缺失的接触：标记立即消失，行保留约 1 秒后移除。
		overlay.BeginContacts();
		const RECT lastBounds = overlay.Markers()[0].bounds;
		dirty = overlay.Update(Params(1100.0, true));
		check(!overlay.Markers()[0].active && Contains(dirty, lastBounds), "a lifted contact clears its marker and dirties it");
		const int heightLinger = overlay.PlateHeight();
		overlay.BeginContacts();
		overlay.Update(Params(1500.0, true));
		check(overlay.PlateHeight() == heightLinger, "an ended row lingers");
		const double wake = overlay.MillisecondsUntilNextUpdate(1500.0);
		check(wake > 0.0 && wake <= 600.0, "linger expiry schedules a wake");
		overlay.BeginContacts();
		overlay.Update(Params(2200.0, true));
		check(overlay.PlateHeight() == heightNone, "an ended row disappears after the linger time");

		// 超出容量的接触被忽略，而不是越界。
		overlay.BeginContacts();
		for (std::uint64_t key = 100; key < 100 + kMaxContacts + 4; ++key) overlay.ObserveContact(Touch(key, 10, 10));
		overlay.Update(Params(2300.0, true));
		int active = 0;
		for (const MarkerView& marker : overlay.Markers()) active += marker.active ? 1 : 0;
		check(active == static_cast<int>(kMaxContacts), "contact slots are bounded");
	}

	// DPI 变化会重建板子几何；旧板子范围必须并入脏区，否则旧像素残留。
	void CheckDpiChangeKeepsStaleBounds(Checks& check)
	{
		Overlay overlay;
		overlay.Update(Params(1000.0));
		const RECT oldPlate = overlay.PlateRect();
		FrameParams scaled = Params(1010.0);
		scaled.dpiScale = 1.5f;
		const RECT dirty = overlay.Update(scaled);
		check(Contains(dirty, oldPlate) && Contains(dirty, overlay.PlateRect()),
			"a DPI change dirties both the old and the new plate rectangles");
	}

	// 数字长短变化不得使其他列错位：只改 X，差异像素必须落在 X 列内。
	void CheckColumnAlignment(Checks& check)
	{
		const auto render = [&](float x, Overlay& overlay)
		{
			overlay.Update(Params(1000.0));
			overlay.BeginContacts();
			overlay.ObserveContact(Touch(1, x, 200));
			overlay.Update(Params(1010.0));
		};
		Overlay shortValue, longValue;
		render(5.0f, shortValue);
		render(12345.0f, longValue);
		check(shortValue.PlateWidth() == longValue.PlateWidth() && shortValue.PlateHeight() == longValue.PlateHeight(),
			"digit count does not change the plate size");
		const int stride = shortValue.PlateCapacityWidth();
		const int cell = shortValue.GlyphCellWidth();
		const int padding = 8; // dpi=1 时的板子内边距
		int minimumColumn = 1 << 20, maximumColumn = -1;
		for (int y = 0; y < shortValue.PlateHeight(); ++y)
			for (int x = 0; x < shortValue.PlateWidth(); ++x)
				if (shortValue.PlatePixels()[y * stride + x] != longValue.PlatePixels()[y * stride + x])
				{
					minimumColumn = std::min(minimumColumn, x);
					maximumColumn = std::max(maximumColumn, x);
				}
		check(maximumColumn >= 0, "different values render differently");
		// Type/Src/Path 各 6、Id 13、Phase 6、Seq 7 字符，各加 1 间隔后 X 列从第 50 个字符开始，宽 5。
		check(minimumColumn >= padding + 50 * cell && maximumColumn < padding + 55 * cell,
			"columns stay aligned when the X value gets longer");
	}

	// 内容变化时左端和上端不动，只向右、向下生长。
	void CheckAnchoredGrowth(Checks& check)
	{
		const auto Wide = [](double now)
		{
			FrameParams params = Params(now);
			params.canvasWidth = 1800;
			params.canvasHeight = 1000;
			return params;
		};
		Overlay overlay;
		overlay.Update(Wide(1000.0));
		const RECT compact = overlay.PlateRect();
		check(compact.left > 0 && compact.right < 900, "the compact plate sits left of the horizontal center");
		overlay.BeginContacts();
		overlay.ObserveContact(Touch(1, 100, 200));
		overlay.Update(Wide(1010.0));
		const RECT grown = overlay.PlateRect();
		check(grown.left == compact.left && grown.top == compact.top, "the left and top edges stay put when a row appears");
		check(grown.right > compact.right && grown.bottom > compact.bottom, "the plate grows to the right and downward");
		overlay.BeginContacts();
		overlay.Update(Wide(1020.0));
		overlay.BeginContacts();
		const RECT dirty = overlay.Update(Wide(3000.0)); // 抬起保留期之后行消失。
		const RECT shrunk = overlay.PlateRect();
		check(shrunk.left == compact.left && shrunk.top == compact.top && shrunk.right == compact.right &&
			shrunk.bottom == compact.bottom, "the plate returns to the compact rectangle");
		check(Contains(dirty, grown), "shrinking dirties the previous larger rectangle");
	}

	// 速度由相邻输入快照测得；输入停止后衰减为 0；没有时间戳则保持未知。
	void CheckSpeedAndInputPath(Checks& check)
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		const std::int64_t second = frequency.QuadPart;

		Overlay overlay;
		const auto observe = [&](double now, ContactSample sample)
		{
			overlay.BeginContacts();
			overlay.ObserveContact(sample);
			overlay.Update(Params(now));
		};
		ContactSample sample = Touch(1, 0.0f, 100.0f, 40, 30, 1000.0f, 0.05f);
		sample.qpc = 10 * second;
		observe(1000.0, sample);
		float px = 0.0f, cm = 0.0f;
		check(overlay.ContactSpeed(1, px, cm) && px < 0.0f && cm < 0.0f, "the first sample has no speed yet");

		sample.x = 100.0f;
		sample.qpc = 10 * second + second / 10; // 0.1 s 内移动 100 px。
		observe(1100.0, sample);
		check(overlay.ContactSpeed(1, px, cm) && std::abs(px - 1000.0f) < 1.0f, "speed is distance over the input timestamps");
		check(std::abs(cm - 50.0f) < 0.1f, "speed in cm/s uses the trusted physical scale");

		observe(1150.0, sample); // 同一个快照再次读到：不是新输入。
		check(overlay.ContactSpeed(1, px, cm) && std::abs(px - 1000.0f) < 1.0f, "re-reading the same snapshot keeps the speed");
		observe(1300.0, sample);
		check(overlay.ContactSpeed(1, px, cm) && px == 0.0f && cm == 0.0f, "speed decays to zero when input stops");

		Overlay unknownTime;
		unknownTime.BeginContacts();
		unknownTime.ObserveContact(Touch(2, 0, 0));
		unknownTime.Update(Params(1000.0));
		unknownTime.BeginContacts();
		unknownTime.ObserveContact(Touch(2, 50, 0));
		unknownTime.Update(Params(1100.0));
		check(unknownTime.ContactSpeed(2, px, cm) && px < 0.0f, "samples without timestamps never report a speed");

		// 整体输入通路变化必须重绘并脏化板子，否则性能区的 Input 行会过期。
		Overlay path;
		path.Update(Params(1000.0));
		FrameParams rtsExtended = Params(1010.0);
		rtsExtended.inputPath = InputPath::RtsExtended;
		check(!Empty(path.Update(rtsExtended)), "an input path change dirties the plate");

		// 阶段、发布次数和输入到上屏延迟。
		Overlay latency;
		ContactSample timed = Touch(3, 10, 10);
		timed.qpc = 20 * second;
		timed.stage = ContactStage::Move;
		timed.publishCount = 7;
		latency.BeginContacts();
		latency.ObserveContact(timed);
		latency.Update(Params(1000.0));
		float milliseconds = 0.0f;
		check(latency.ContactLatency(3, milliseconds) && milliseconds < 0.0f, "latency is unknown before the first present");
		latency.OnPresented(20 * second + second / 100); // 10 ms 之后上屏。
		check(latency.ContactLatency(3, milliseconds) && std::abs(milliseconds - 10.0f) < 0.1f,
			"latency is the input timestamp to the present return");
		latency.OnPresented(20 * second + second / 50); // 同一个快照不会重复结算。
		check(latency.ContactLatency(3, milliseconds) && std::abs(milliseconds - 10.0f) < 0.1f,
			"an input snapshot is settled once");
		timed.qpc = 20 * second + second / 10;
		latency.BeginContacts();
		latency.ObserveContact(timed);
		latency.OnPresented(20 * second + second / 10 + second / 50); // 新快照，20 ms。
		check(latency.ContactLatency(3, milliseconds) && milliseconds > 10.0f && milliseconds < 20.0f,
			"latency is smoothed across snapshots");
		rtsExtended.nowMs = 1020.0;
		check(Empty(path.Update(rtsExtended)), "an unchanged input path adds no dirty area");
	}
}

int RunCanvasDebugTests()
{
	Checks check;
	CheckPureFunctions(check);
	CheckPlateAndDirty(check);
	CheckPerformanceSnapshot(check);
	CheckContactsAndMarkers(check);
	CheckDpiChangeKeepsStaleBounds(check);
	CheckColumnAlignment(check);
	CheckSpeedAndInputPath(check);
	CheckAnchoredGrowth(check);
	std::cout << "[CanvasDebug] failures=" << check.failures << '\n';
	return check.failures;
}
