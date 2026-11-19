#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.DevicePerformanceJson.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.DevicePerformanceLedger.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <cmath>
#include <iostream>

namespace
{
	using namespace Inkeys::Drawing::Draw3::DevicePerformance;

	bool Near(double actual, double expected, double tolerance = 1e-6)
	{
		return std::abs(actual - expected) <= tolerance;
	}

	Signature DCompSignature(std::uint16_t fps = 120, std::uint8_t waitable = 1)
	{
		Signature signature;
		signature.frameRate = fps;
		signature.device = Device::Hardware;
		signature.presentation = Presentation::DirectComposition;
		signature.waitable = waitable;
		return signature;
	}

	// 一帧按画布帧率推进的渲染帧：帧间隔 = 画布帧间隔，落进新的合成周期。
	FrameSample RenderedFrame(double costMs, double targetMs)
	{
		FrameSample sample;
		sample.rendered = true;
		sample.displayed = true;
		sample.costMs = costMs;
		sample.intervalMs = targetMs;
		sample.targetMs = targetMs;
		return sample;
	}

	// 构造一段“frames 帧、每帧无限制渲染耗时 costMs、按 targetMs 画布帧间隔推进”的累加，全部属于 mask 维度。
	Tally MakeTally(std::uint32_t mask, std::uint64_t frames, double costMs, double targetMs)
	{
		Tally tally;
		for (std::uint64_t i = 0; i < frames; ++i) tally.Add(mask, RenderedFrame(costMs, targetMs));
		return tally;
	}

	Counters MakeCounters(std::uint32_t mask, std::uint64_t frames, double costMs, double targetMs)
	{
		return MakeTally(mask, frames, costMs, targetMs).counters;
	}

	// 总体数据：每帧耗时 avgCostMs，帧间隔 = intervalScale × 画布帧间隔（1000 / fps），
	// displayedRatio 的帧落进新合成周期，missedRatio 的帧掉帧。
	Figure MakeOverall(double avgCostMs, int fps, double missedRatio = 0.0, double intervalScale = 1.0,
		double displayedRatio = 1.0, std::uint64_t frames = 1000)
	{
		const double count = static_cast<double>(frames);
		Counter counter;
		counter.frames = frames;
		counter.costMs = avgCostMs * count;
		counter.timedFrames = frames;
		counter.displayedFrames = static_cast<std::uint64_t>(displayedRatio * count);
		counter.intervalMs = 1000.0 / fps * count * intervalScale;
		counter.missed = static_cast<std::uint64_t>(missedRatio * count);
		counter.computeFrames = frames;
		counter.computeIntervalMs = counter.intervalMs;
		return MakeFigure(counter, fps, kLowSampleOverallFrames);
	}

	bool SameCounter(const Counter& a, const Counter& b)
	{
		return a.frames == b.frames && a.timedFrames == b.timedFrames && a.displayedFrames == b.displayedFrames &&
			a.missed == b.missed && a.computeFrames == b.computeFrames && Near(a.costMs, b.costMs, 1e-9) &&
			Near(a.intervalMs, b.intervalMs, 1e-9) &&
			Near(a.computeIntervalMs, b.computeIntervalMs, 1e-9);
	}
}

int RunDraw3DevicePerformanceTests()
{
	int failures = 0;
	const auto expect = [&](bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[DevicePerformance] failed: " << name << '\n';
	};

	// 工具 → 维度映射（DrawingTool 整数值）。
	{
		expect(ToolDimensionMask(0) == DimensionBit(Dimension::Writing), "pen is writing");
		expect(ToolDimensionMask(1) == DimensionBit(Dimension::Writing), "hard pen is writing");
		expect(ToolDimensionMask(2) == DimensionBit(Dimension::Highlighter), "highlighter");
		expect(ToolDimensionMask(3) == DimensionBit(Dimension::Eraser), "eraser");
		expect(ToolDimensionMask(4) == DimensionBit(Dimension::Laser), "laser");
		for (int tool = 5; tool <= 9; ++tool)
			expect(ToolDimensionMask(tool) == DimensionBit(Dimension::Shape), "line/rect/shape tools are shape");
		expect(ToolDimensionMask(10) == 0 && ToolDimensionMask(-1) == 0, "unknown tool has no dimension");
	}

	// 累加：多维度帧总体只计一次；渲染帧、计算帧、帧间隔与掉帧分别计；非法样本不计。
	{
		Tally tally;
		const std::uint32_t writing = DimensionBit(Dimension::Writing);
		const std::uint32_t writingAndPan = writing | DimensionBit(Dimension::Pan);
		expect(tally.Add(writingAndPan, RenderedFrame(4.0, 8.0)), "add multi-dimension frame");
		FrameSample late = RenderedFrame(3.0, 8.0);
		late.intervalMs = 12.5; // 超过 1.5 倍画布帧间隔：掉帧
		late.displayed = false; // ULW 节流合并跳过：画了但没上屏
		expect(tally.Add(writing, late), "add missed frame");
		FrameSample still; // 静止接触：无脏区的计算帧
		still.intervalMs = 8.0;
		still.targetMs = 8.0;
		expect(tally.Add(writing, still), "compute-only frame counted");
		FrameSample unpaced = RenderedFrame(5.0, 8.0); // 以空闲/期限等待结束：帧间隔无效，只计耗时
		unpaced.intervalMs = -1.0;
		expect(tally.Add(writing, unpaced), "unpaced rendered frame counted");
		FrameSample nothing; // 既不是渲染帧也没有帧间隔
		expect(!tally.Add(writing, nothing), "empty sample not counted");
		expect(!tally.Add(0, RenderedFrame(1.0, 8.0)), "empty mask not counted");
		FrameSample negative = RenderedFrame(-1.0, 8.0);
		negative.intervalMs = -1.0;
		expect(!tally.Add(writing, negative), "negative cost without interval not counted");
		FrameSample nan = RenderedFrame(std::nan(""), 8.0);
		nan.intervalMs = std::nan("");
		expect(!tally.Add(writing, nan), "nan sample not counted");
		const Counter& overall = tally.counters[0];
		expect(overall.frames == 3 && Near(overall.costMs, 12.0), "rendered frames and cost");
		expect(overall.timedFrames == 2 && overall.displayedFrames == 1 && Near(overall.intervalMs, 20.5) &&
			overall.missed == 1, "timed frames, displayed frames and missed frames");
		expect(overall.computeFrames == 3 && Near(overall.computeIntervalMs, 28.5), "compute frames include still frame");
		expect(tally.counters[6].frames == 1 && Near(tally.counters[6].costMs, 4.0) && tally.counters[6].computeFrames == 1,
			"pan counted for multi frame");
		expect(tally.counters[3].frames == 0 && tally.counters[3].computeFrames == 0, "eraser untouched");
		tally.display.Observe(60);
		tally.display.Observe(120);
		tally.display.Observe(5); // 不合理的采样忽略
		expect(tally.display.minHz == 60 && tally.display.maxHz == 120, "display range observed");
		tally.Clear();
		expect(tally.Empty() && !tally.display.Valid(), "clear empties tally");
		Tally stillOnly;
		stillOnly.Add(writing, still);
		expect(!stillOnly.Empty(), "compute-only tally is not empty");
	}

	// 单段累加上限。
	{
		Tally tally;
		tally.counters[0].frames = kTallyFrameCap;
		expect(!tally.Add(DimensionBit(Dimension::Writing), RenderedFrame(1.0, 8.0)), "cap stops accumulation");
	}

	// 帧率与负载口径：负载只看帧循环能否按画布帧率推进；渲染帧率含显示器与同步损失，只显示不定档位。
	{
		const Figure keepUp = MakeOverall(2.0, 120);
		expect(Near(keepUp.renderFps, 120.0) && Near(keepUp.computeFps, 120.0), "keep-up frame rates");
		expect(Near(keepUp.unlimitedFps, 500.0) && Near(keepUp.loadPercent, 24.0), "keep-up load is cost over canvas interval");
		expect(ClassifyFigure(keepUp) == Level::Ample, "light work keeps ample");
		// GPU 被压满：waitable 阻塞或 Map 等待拖慢帧循环，计算帧率只有画布帧率的一半。
		const Figure gpuBound = MakeOverall(2.0, 120, 0.0, 2.0);
		expect(Near(gpuBound.computeFps, 60.0) && Near(gpuBound.loadPercent, 200.0) &&
			ClassifyFigure(gpuBound) == Level::Overloaded, "frame loop falling behind is overloaded");
		// 3% 以内的缺口（计时器过冲）不算跟不上。
		const Figure jitter = MakeOverall(2.0, 120, 0.0, 1.02);
		expect(Near(jitter.loadPercent, 24.0), "small deficit within tolerance keeps cost load");
		// ULW 呈现同步损失：帧循环 120、耗时 19%，只有 88% 的帧落进新合成周期，渲染约 106，不算超载。
		const Figure syncLoss = MakeOverall(1.6, 120, 0.0, 1.0, 0.88);
		expect(Near(syncLoss.renderFps, 105.6) && Near(syncLoss.loadPercent, 19.2) &&
			ClassifyFigure(syncLoss) == Level::Ample, "presentation sync loss does not count as overload");
		// 显示器 60 Hz、画布 120：帧循环 120，每两帧一帧上屏，渲染 60；耗时按画布帧间隔 8.33 ms 计。
		const Figure displayBound = MakeOverall(4.0, 120, 0.0, 1.0, 0.5);
		expect(Near(displayBound.renderFps, 60.0) && Near(displayBound.loadPercent, 48.0),
			"display-limited render, cost uses canvas interval");
		// DComp SyncInterval 0：画布 240、显示器 120，帧循环与 Present1 都是 240，每两次提交只有一次落进新合成周期。
		Tally dcomp240;
		for (int i = 0; i < 100; ++i)
		{
			FrameSample sample = RenderedFrame(0.8, 1000.0 / 240.0);
			sample.displayed = i % 2 == 0;
			dcomp240.Add(DimensionBit(Dimension::Writing), sample);
		}
		const Figure fast = MakeFigure(dcomp240.counters[0], 240, kLowSampleOverallFrames);
		expect(Near(fast.renderFps, 120.0) && Near(fast.computeFps, 240.0),
			"render fps counts refresh periods while compute runs at the canvas rate");
		expect(Near(fast.loadPercent, 0.8 / (1000.0 / 240.0) * 100.0) && ClassifyFigure(fast) == Level::Ample,
			"display-limited render does not count as a deficit");
		// 帧循环偶尔卡一下：帧间隔超过 1.5 倍画布帧间隔记为掉帧（与显示器刷新率无关）。
		Tally hiccup;
		FrameSample slow = RenderedFrame(1.0, 1000.0 / 120.0);
		slow.intervalMs = 13.0;
		hiccup.Add(DimensionBit(Dimension::Writing), slow);
		expect(hiccup.counters[0].missed == 1, "missed frame uses canvas interval");
	}

	// 按 DWM 合成周期计数：同一周期内的多次提交只算一帧；周期保留小数，实测值不会被凑成整数。
	{
		const double period = 1000.0 / 119.88;
		RefreshSlotCounter slots;
		int displayed = 0;
		// 画布 240：每周期两次提交（垂直同步在 0，合成截止为 n·period − 2 ms）。
		for (int i = 0; i < 240; ++i)
			if (slots.Observe(1000.0 + i * period / 2.0 + 0.3, 0.0, period)) ++displayed;
		expect(displayed == 120, "two commits per refresh period count once");
		// 帧循环抖动：一次提交越过截止落到下一周期，紧接着的提交与它同周期，少计一帧。
		slots.Reset();
		displayed = 0;
		const double base = 240.0 * period + 1.0; // 每次提交都在周期开头 1 ms，远早于截止
		const double times[] = { 0.0, period, 2.0 * period + 6.0, 3.0 * period + 0.2, 4.0 * period }; // 第三次晚于截止
		for (const double t : times)
			if (slots.Observe(base + t, 0.0, period)) ++displayed;
		expect(displayed == 4, "a late commit sharing the next period is not counted twice");
		// 时序重采样后垂直同步参考点换到更晚的一次：截止时刻不变，不会重复计数。
		slots.Reset();
		expect(slots.Observe(3000.0, 0.0, period), "first commit opens a period");
		const double laterVBlank = std::floor(3000.0 / period) * period;
		expect(!slots.Observe(3000.5, laterVBlank, period), "resampled vblank reference keeps the same period");
		// 没有合成时序：每次提交都算一帧。
		expect(slots.Observe(3001.0, 0.0, 0.0) && slots.Observe(3001.1, 0.0, 0.0), "no timing counts every commit");
		// ULW 节流：画布 120 帧循环，只有一半提交，显示器 60：渲染 60、计算 120，跟得上。
		Tally throttled;
		for (int i = 0; i < 100; ++i)
		{
			FrameSample sample = RenderedFrame(2.0, 1000.0 / 120.0);
			sample.displayed = i % 2 == 0;
			throttled.Add(DimensionBit(Dimension::Writing), sample);
		}
		const Figure ulw = MakeFigure(throttled.counters[0], 120, kLowSampleOverallFrames);
		expect(Near(ulw.renderFps, 60.0) && Near(ulw.computeFps, 120.0) && Near(ulw.loadPercent, 24.0),
			"throttled ulw renders at the display limit");
		// 没有有效帧间隔：只按耗时占比，渲染/计算帧率为 0（界面显示“--”）。
		Counter untimed;
		untimed.frames = 10;
		untimed.costMs = 20.0;
		const Figure noInterval = MakeFigure(untimed, 60, kLowSampleOverallFrames);
		expect(noInterval.available && noInterval.renderFps == 0.0 && noInterval.computeFps == 0.0 &&
			Near(noInterval.loadPercent, 2.0 / (1000.0 / 60.0) * 100.0), "untimed figure uses cost load only");
	}

	// 签名规范化：与当前模式无关的开关不拆签名。
	{
		Signature a = DCompSignature();
		a.ulwAsyncReadback = 1;
		Signature b = DCompSignature();
		b.ulwAsyncReadback = 0;
		expect(NormalizeSignature(a) == NormalizeSignature(b), "ulw switch irrelevant under dcomp");
		expect(!(NormalizeSignature(DCompSignature(120, 1)) == NormalizeSignature(DCompSignature(120, 0))),
			"waitable splits dcomp signature");
		Signature ulw;
		ulw.frameRate = 60;
		ulw.device = Device::Warp;
		ulw.presentation = Presentation::Ulw;
		ulw.waitable = 1;
		ulw.ulwAsyncReadback = 0;
		ulw.ulwCompositionAlign = 1;
		const Signature normalized = NormalizeSignature(ulw);
		expect(normalized.waitable == kNotApplicable, "waitable irrelevant under ulw");
		expect(normalized.ulwCompositionAlign == kNotApplicable, "align irrelevant when async readback off");
		expect(SignatureKey(ulw) == "fps60-warp-ulw-async0-zx0", "ulw key");
		expect(SignatureKey(DCompSignature()) == "fps120-hardware-dcomp-wait1-zx0", "dcomp key");
		expect(!(NormalizeSignature(DCompSignature(120)) == NormalizeSignature(DCompSignature(60))), "frame rate splits");
	}

	// 日桶：同日合并、第 8 个使用日淘汰最早、时间回拨插入旧日期。
	{
		Store store;
		const Signature signature = DCompSignature();
		const Counters one = MakeCounters(DimensionBit(Dimension::Writing), 10, 2.0, 8.33);
		expect(MergeTally(store, signature, MakeDate(2026, 11, 1), 100, one), "merge first day");
		expect(MergeTally(store, signature, MakeDate(2026, 11, 1), 200, one), "merge same day");
		expect(store.records.size() == 1 && store.records[0].days.size() == 1, "same day merged into one bucket");
		expect(store.records[0].days[0].counters[0].frames == 20 && store.records[0].days[0].updatedUnixSeconds == 200,
			"same day sums counters and keeps latest update");
		for (int day = 2; day <= 8; ++day)
			MergeTally(store, signature, MakeDate(2026, 11, day * 3), 200 + day, one); // 跳过若干天也只算使用日
		const auto& days = store.records[0].days;
		expect(days.size() == kMaxUsageDays, "keeps 7 usage days");
		expect(days.front().date == MakeDate(2026, 11, 6) && days.back().date == MakeDate(2026, 11, 24),
			"oldest usage day evicted, gaps ignored");
		MergeTally(store, signature, MakeDate(2026, 10, 30), 300, one); // 时间回拨：比所有日桶都早
		expect(store.records[0].days.size() == kMaxUsageDays && store.records[0].days.front().date == MakeDate(2026, 11, 6),
			"older date inserted then evicted");
		expect(!MergeTally(store, signature, 20261399, 300, one), "invalid date rejected");
		expect(!MergeTally(store, signature, MakeDate(2026, 11, 30), 300, Counters{}), "empty counters rejected");
	}

	// 签名上限：淘汰最久未用的签名，当前签名不淘汰。
	{
		Store store;
		const Counters one = MakeCounters(DimensionBit(Dimension::Eraser), 5, 1.0, 8.33);
		for (int i = 0; i < static_cast<int>(kMaxSignatures); ++i)
		{
			Signature signature = DCompSignature(120);
			signature.zhaoxinCompatibility = 0;
			signature.frameRate = static_cast<std::uint16_t>(30 + i); // 只为制造不同签名
			MergeTally(store, signature, MakeDate(2026, 11, 1), 100 + i, one);
		}
		Signature fresh = DCompSignature(999);
		MergeTally(store, fresh, MakeDate(2026, 11, 2), 50, one); // lastUsed 最小，但它是当前签名
		expect(store.records.size() == kMaxSignatures, "signature cap");
		bool hasFresh = false, hasOldest = false;
		for (const SignatureRecord& record : store.records)
		{
			hasFresh |= record.signature.frameRate == 999;
			hasOldest |= record.signature.frameRate == 30;
		}
		expect(hasFresh && !hasOldest, "evicts least recently used, keeps current");
	}

	// 报告：当前签名、回退到上一签名、无数据、跨日加权与覆盖维度。
	{
		Store store;
		const Signature current = DCompSignature(120);
		const Signature previous = DCompSignature(60);
		expect(BuildReport(store, &current).source == ReportSource::None, "empty store");
		MergeTally(store, previous, MakeDate(2026, 11, 1), 100,
			MakeCounters(DimensionBit(Dimension::Laser), 100, 5.0, 1000.0 / 60.0));
		Report report = BuildReport(store, &current);
		expect(report.source == ReportSource::PreviousSignature && report.signature.frameRate == 60,
			"falls back to previous signature");
		expect(Near(report.overall.loadPercent, 5.0 / (1000.0 / 60.0) * 100.0), "load uses measured frame rate");
		expect(!report.display.Valid(), "no display range recorded");
		MergeTally(store, current, MakeDate(2026, 11, 2), 200,
			MakeCounters(DimensionBit(Dimension::Writing), 300, 2.0, 1000.0 / 120.0), DisplayRange{ 120, 120 });
		MergeTally(store, current, MakeDate(2026, 11, 3), 300,
			MakeCounters(DimensionBit(Dimension::Writing) | DimensionBit(Dimension::Pan), 100, 6.0, 1000.0 / 120.0),
			DisplayRange{ 60, 90 });
		report = BuildReport(store, &current);
		expect(report.source == ReportSource::CurrentSignature, "current signature preferred");
		expect(report.evaluatedUnixSeconds == 300, "evaluated time is latest merge");
		expect(report.overall.frames == 400 && Near(report.overall.unlimitedFps, 1000.0 / 3.0), "frame-weighted across days");
		expect(Near(report.overall.loadPercent, 3.0 / (1000.0 / 120.0) * 100.0), "overall load");
		expect(Near(report.overall.renderFps, 120.0), "render fps across days");
		expect(report.display.minHz == 60 && report.display.maxHz == 120, "display range merged across days");
		expect(report.dimensions[static_cast<int>(Dimension::Writing)].available &&
			report.dimensions[static_cast<int>(Dimension::Pan)].available, "used dimensions available");
		expect(report.dimensions[static_cast<int>(Dimension::Pan)].frames == 100 &&
			report.dimensions[static_cast<int>(Dimension::Laser)].available == false, "dimension figures");
		expect(report.overall.lowSample, "400 frames is low sample");
		expect(!report.dimensions[0].lowSample && report.dimensions[5].lowSample, "dimension sample threshold");
	}

	// 加载合并：文件数据与启动后内存数据相加。
	{
		Store memory, disk;
		const Signature signature = DCompSignature();
		const Counters one = MakeCounters(DimensionBit(Dimension::Shape), 10, 2.0, 8.33);
		MergeTally(memory, signature, MakeDate(2026, 11, 5), 500, one);
		MergeTally(disk, signature, MakeDate(2026, 11, 5), 400, one);
		MergeTally(disk, signature, MakeDate(2026, 11, 4), 300, one);
		MergeStore(memory, disk);
		expect(memory.records.size() == 1 && memory.records[0].days.size() == 2, "merge days from disk");
		expect(memory.records[0].days[1].counters[0].frames == 20, "same day summed with disk");
		expect(memory.records[0].lastUsedUnixSeconds == 500, "last used keeps max");
	}

	// 档位。
	{
		expect(ClassifyLoad(10) == Level::Ample && ClassifyLoad(50) == Level::Moderate &&
			ClassifyLoad(80) == Level::Tight && ClassifyLoad(100) == Level::Overloaded, "load levels");
		expect(ClassifyFigure(MakeOverall(1.0, 120, 0.2)) == Level::Tight, "missed frames raise level to tight");
		expect(ClassifyFigure(Figure{}) == Level::None, "no data level");
	}

	// JSON 往返、坏文件、schema 不符与单桶非法字段。
	{
		Store store;
		Signature ulw;
		ulw.frameRate = 60;
		ulw.device = Device::Warp;
		ulw.presentation = Presentation::Ulw;
		ulw.ulwAsyncReadback = 1;
		ulw.ulwCompositionAlign = 0;
		MergeTally(store, DCompSignature(), MakeDate(2026, 11, 17), 1763400000,
			MakeCounters(DimensionBit(Dimension::Writing) | DimensionBit(Dimension::Pan), 50, 2.5, 8.33),
			DisplayRange{ 60, 120 });
		MergeTally(store, DCompSignature(), MakeDate(2026, 11, 18), 1763450430,
			MakeCounters(DimensionBit(Dimension::Eraser), 30, 9.0, 8.33));
		MergeTally(store, ulw, MakeDate(2026, 11, 18), 1763450000,
			MakeCounters(DimensionBit(Dimension::Laser), 20, 4.0, 16.67));
		const std::string text = SerializeStore(store);
		Store parsed;
		std::size_t dropped = 99;
		expect(ParseStore(text, parsed, &dropped) == ParseResult::Ok && dropped == 0, "roundtrip parses");
		bool same = parsed.records.size() == store.records.size();
		for (std::size_t i = 0; same && i < store.records.size(); ++i)
		{
			const SignatureRecord& a = store.records[i];
			const SignatureRecord& b = parsed.records[i];
			same = a.signature == b.signature && a.lastUsedUnixSeconds == b.lastUsedUnixSeconds &&
				a.days.size() == b.days.size();
			for (std::size_t d = 0; same && d < a.days.size(); ++d)
			{
				same = a.days[d].date == b.days[d].date && a.days[d].updatedUnixSeconds == b.days[d].updatedUnixSeconds &&
					a.days[d].display == b.days[d].display;
				for (std::size_t c = 0; same && c < kCounterCount; ++c)
					same = SameCounter(a.days[d].counters[c], b.days[d].counters[c]);
			}
		}
		expect(same, "roundtrip preserves store");
		expect(text.find("fps120-hardware-dcomp-wait1-zx0") != std::string::npos, "readable key written");

		Store untouched = store;
		expect(ParseStore("{ not json", untouched) == ParseResult::Malformed && untouched.records.size() == 2,
			"malformed text fails without touching output");
		expect(ParseStore("[]", untouched) == ParseResult::Malformed, "non-object root fails");
		expect(ParseStore(R"({"schema":"other","version":4,"signatures":[]})", untouched) == ParseResult::WrongSchema,
			"wrong schema");
		expect(ParseStore(R"({"schema":"inkeys.device-performance","version":1,"signatures":[]})", untouched) ==
			ParseResult::WrongSchema, "old version is not compatible");
		expect(ParseStore(R"({"schema":"inkeys.device-performance","version":3,"signatures":[]})", untouched) ==
			ParseResult::WrongSchema, "version 3 is not compatible");
		expect(ParseStore(R"({"schema":"inkeys.device-performance","version":5,"signatures":[]})", untouched) ==
			ParseResult::WrongSchema, "future version");

		// 掉帧多于有效帧、日期非法、维度超过总体、刷新率范围非法：都只丢弃该桶。
		const std::string partial = R"({"schema":"inkeys.device-performance","version":4,"signatures":[
			{"frameRate":120,"device":"hardware","presentation":"dcomp","waitable":1,"ulwAsyncReadback":-1,
			 "ulwCompositionAlign":-1,"zhaoxin":0,"lastUsed":100,"days":[
				{"date":20261101,"updated":100,"display":[0,0],"all":[10,20.0,10,10,83.3,11,10,83.3],
				 "writing":[10,20.0,10,10,83.3,0,10,83.3],"highlighter":[0,0.0,0,0,0.0,0,0,0.0],"eraser":[0,0.0,0,0,0.0,0,0,0.0],"laser":[0,0.0,0,0,0.0,0,0,0.0],"shape":[0,0.0,0,0,0.0,0,0,0.0],"pan":[0,0.0,0,0,0.0,0,0,0.0]},
				{"date":20261199,"updated":100,"display":[0,0],"all":[10,20.0,10,10,83.3,0,10,83.3],
				 "writing":[10,20.0,10,10,83.3,0,10,83.3],"highlighter":[0,0.0,0,0,0.0,0,0,0.0],"eraser":[0,0.0,0,0,0.0,0,0,0.0],"laser":[0,0.0,0,0,0.0,0,0,0.0],"shape":[0,0.0,0,0,0.0,0,0,0.0],"pan":[0,0.0,0,0,0.0,0,0,0.0]},
				{"date":20261102,"updated":100,"display":[0,0],"all":[10,20.0,10,10,83.3,0,10,83.3],
				 "writing":[11,20.0,10,10,83.3,0,10,83.3],"highlighter":[0,0.0,0,0,0.0,0,0,0.0],"eraser":[0,0.0,0,0,0.0,0,0,0.0],"laser":[0,0.0,0,0,0.0,0,0,0.0],"shape":[0,0.0,0,0,0.0,0,0,0.0],"pan":[0,0.0,0,0,0.0,0,0,0.0]},
				{"date":20261104,"updated":120,"display":[120,60],"all":[10,20.0,10,10,83.3,0,10,83.3],
				 "writing":[10,20.0,10,10,83.3,0,10,83.3],"highlighter":[0,0.0,0,0,0.0,0,0,0.0],"eraser":[0,0.0,0,0,0.0,0,0,0.0],"laser":[0,0.0,0,0,0.0,0,0,0.0],"shape":[0,0.0,0,0,0.0,0,0,0.0],"pan":[0,0.0,0,0,0.0,0,0,0.0]},
				{"date":20261103,"updated":150,"display":[60,120],"all":[10,20.0,10,9,83.3,1,12,90.0],
				 "writing":[10,20.0,10,9,83.3,1,12,90.0],"highlighter":[0,0.0,0,0,0.0,0,0,0.0],"eraser":[0,0.0,0,0,0.0,0,0,0.0],"laser":[0,0.0,0,0,0.0,0,0,0.0],"shape":[0,0.0,0,0,0.0,0,0,0.0],"pan":[0,0.0,0,0,0.0,0,0,0.0]}]},
			{"frameRate":"bad","days":[]}]})";
		Store partialStore;
		dropped = 0;
		expect(ParseStore(partial, partialStore, &dropped) == ParseResult::Ok, "partial file parses");
		expect(dropped == 5 && partialStore.records.size() == 1 && partialStore.records[0].days.size() == 1 &&
			partialStore.records[0].days[0].date == MakeDate(2026, 11, 3), "invalid buckets and records dropped");
		expect(!partialStore.records.empty() && !partialStore.records[0].days.empty() &&
			partialStore.records[0].days[0].display == DisplayRange{ 60, 120 } &&
			partialStore.records[0].days[0].counters[0].computeFrames == 12, "valid bucket keeps all fields");
	}

	// 账本：启动加载坏文件改名 .bad、Submit 后 Stop 同步落盘、重启加载、清除删除文件。
	{
		namespace fs = std::filesystem;
		std::error_code error;
		const fs::path root = fs::temp_directory_path(error) / (L"inkeys-device-performance-" +
			std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
		fs::create_directories(root, error);
		const fs::path file = root / L"Memory" / L"device-performance.json";
		fs::create_directories(file.parent_path(), error);
		{
			std::ofstream corrupt(file, std::ios::binary);
			corrupt << "{ broken";
		}
		const Signature signature = DCompSignature();
		const Tally counters = MakeTally(DimensionBit(Dimension::Writing), 40, 3.0, 8.33);
		{
			Inkeys::Drawing::Draw3::DevicePerformanceLedger ledger;
			ledger.SetCurrentSignature(signature);
			ledger.Start(file.wstring());
			expect(ledger.WaitLoaded(5000), "ledger loads");
			expect(fs::exists(file.wstring() + L".bad") && !fs::exists(file), "corrupt file renamed to .bad");
			Report report;
			expect(ledger.Snapshot(0, report) && report.source == ReportSource::None, "empty after corrupt file");
			ledger.Submit(signature, counters);
			Report after;
			expect(ledger.Snapshot(report.revision, after) && after.source == ReportSource::CurrentSignature &&
				after.overall.frames == 40, "submit updates report");
			expect(!ledger.Snapshot(after.revision, report), "unchanged revision does not copy");
			ledger.SetRecordingEnabled(true); // 状态未变：不应产生新版本
			expect(!ledger.Snapshot(after.revision, report), "unchanged recording flag keeps revision");
			ledger.Stop(); // 同步写盘
		}
		expect(fs::exists(file), "stop writes file");
		{
			Inkeys::Drawing::Draw3::DevicePerformanceLedger ledger;
			ledger.Submit(signature, counters); // 启动前后的内存数据与文件相加
			ledger.Start(file.wstring());
			expect(ledger.WaitLoaded(5000), "ledger reloads");
			const Store store = ledger.StoreCopy();
			expect(store.records.size() == 1 && store.records[0].days.size() == 1 &&
				store.records[0].days[0].counters[0].frames == 80, "reload merges with memory data");
			// DComp 失败回退时 Host 会 Stop→Start：同一路径不重读，历史不翻倍。
			ledger.Stop();
			ledger.Start(file.wstring());
			expect(ledger.WaitLoaded(5000), "restart on same path loads");
			const Store restarted = ledger.StoreCopy();
			// 先检查容器再取下标：加载回归时报告断言而不是越界崩溃。
			expect(restarted.records.size() == 1 && restarted.records[0].days.size() == 1 &&
				restarted.records[0].days[0].counters[0].frames == 80,
				"restart on same path does not double history");
			ledger.SetRecordingEnabled(false);
			Report report;
			expect(ledger.Snapshot(0, report) && !report.recordingEnabled && !ledger.RecordingEnabled(), "recording flag");
			ledger.Clear();
			ledger.Stop();
			expect(ledger.StoreCopy().records.empty(), "clear empties memory");
		}
		expect(!fs::exists(file), "clear deletes file");
		// 记录关闭时遇到坏文件：不改名、不覆盖（关闭即不写文件）。
		{
			{
				std::ofstream corrupt(file, std::ios::binary);
				corrupt << "{ broken again";
			}
			fs::remove(file.wstring() + L".bad", error);
			Inkeys::Drawing::Draw3::DevicePerformanceLedger ledger;
			ledger.SetRecordingEnabled(false);
			ledger.SetCurrentSignature(signature);
			ledger.Start(file.wstring());
			expect(ledger.WaitLoaded(5000), "ledger loads with recording off");
			expect(fs::exists(file) && !fs::exists(file.wstring() + L".bad"), "recording off leaves corrupt file untouched");
			Report report;
			expect(ledger.Snapshot(0, report) && report.source == ReportSource::None && !report.recordingEnabled,
				"recording off report is empty and paused");
			ledger.SetRecordingEnabled(true);
			ledger.Submit(signature, counters);
			ledger.Stop();
			std::ifstream check(file, std::ios::binary);
			const std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
			expect(content == "{ broken again", "unbacked corrupt file is never overwritten in that session");
		}
		// 段开始后清除、关闭记录后移交：整段丢弃；启动后立即清除：历史不复活、文件被删除。
		{
			fs::remove(file, error);
			fs::remove(file.wstring() + L".bad", error);
			Inkeys::Drawing::Draw3::DevicePerformanceLedger seed;
			seed.Start(file.wstring());
			expect(seed.WaitLoaded(5000), "seed loads");
			seed.Submit(signature, counters);
			seed.Stop();
			expect(fs::exists(file), "seed history written");

			Inkeys::Drawing::Draw3::DevicePerformanceLedger ledger;
			ledger.Start(file.wstring());
			const std::uint64_t staleEpoch = ledger.MarkBusy(); // 清除前开始的段取得的代次
			ledger.Clear(); // 加载前或加载后清除，结果都必须为空且文件被删除
			expect(ledger.WaitLoaded(5000) && ledger.StoreCopy().records.empty(), "clear right after start keeps store empty");
			ledger.Submit(signature, counters, staleEpoch);
			expect(ledger.StoreCopy().records.empty(), "segment started before clear is dropped");
			const std::uint64_t epoch = ledger.MarkBusy();
			ledger.SetRecordingEnabled(false);
			ledger.Submit(signature, counters, epoch);
			expect(ledger.StoreCopy().records.empty(), "submit after recording turned off is dropped");
			ledger.SetRecordingEnabled(true);
			ledger.Submit(signature, counters, epoch);
			const Store accepted = ledger.StoreCopy();
			expect(accepted.records.size() == 1 && accepted.records[0].days.size() == 1 &&
				accepted.records[0].days[0].counters[0].frames == 40, "current-epoch segment accepted");
			ledger.Clear();
			ledger.Stop();
			expect(!fs::exists(file), "file deleted after clear");
		}
		fs::remove_all(root, error);
	}

	if (failures == 0) std::cout << "[DevicePerformance] all tests passed\n";
	return failures;
}
