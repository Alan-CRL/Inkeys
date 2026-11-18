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

	// 构造一段“frames 帧、每帧 workMs”的累加，全部属于 mask 维度。
	Counters MakeCounters(std::uint32_t mask, std::uint64_t frames, double workMs, double budgetMs)
	{
		Tally tally;
		for (std::uint64_t i = 0; i < frames; ++i) tally.Add(mask, workMs, budgetMs);
		return tally.counters;
	}

	Figure MakeOverall(double avgWorkMs, int fps, double overBudgetRatio = 0.0, std::uint64_t frames = 1000)
	{
		Counter counter;
		counter.frames = frames;
		counter.workMs = avgWorkMs * static_cast<double>(frames);
		counter.overBudget = static_cast<std::uint64_t>(overBudgetRatio * static_cast<double>(frames));
		return MakeFigure(counter, fps, kLowSampleOverallFrames);
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

	// 累加：多维度帧总体只计一次；空掩码、非法耗时不计；超预算计数。
	{
		Tally tally;
		const std::uint32_t writingAndPan = DimensionBit(Dimension::Writing) | DimensionBit(Dimension::Pan);
		expect(tally.Add(writingAndPan, 4.0, 8.0), "add multi-dimension frame");
		expect(tally.Add(DimensionBit(Dimension::Writing), 10.0, 8.0), "add over-budget frame");
		expect(!tally.Add(0, 1.0, 8.0), "empty mask not counted");
		expect(!tally.Add(DimensionBit(Dimension::Writing), -1.0, 8.0), "negative work not counted");
		expect(!tally.Add(DimensionBit(Dimension::Writing), std::nan(""), 8.0), "nan work not counted");
		expect(tally.counters[0].frames == 2 && Near(tally.counters[0].workMs, 14.0), "overall counts each frame once");
		expect(tally.counters[1].frames == 2 && tally.counters[1].overBudget == 1, "writing counted with over budget");
		expect(tally.counters[6].frames == 1 && Near(tally.counters[6].workMs, 4.0), "pan counted for multi frame");
		expect(tally.counters[3].frames == 0, "eraser untouched");
		tally.Clear();
		expect(tally.Empty(), "clear empties tally");
	}

	// 单段累加上限。
	{
		Tally tally;
		tally.counters[0].frames = kTallyFrameCap;
		expect(!tally.Add(DimensionBit(Dimension::Writing), 1.0, 8.0), "cap stops accumulation");
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
			MakeCounters(DimensionBit(Dimension::Laser), 100, 5.0, 16.67));
		Report report = BuildReport(store, &current);
		expect(report.source == ReportSource::PreviousSignature && report.signature.frameRate == 60,
			"falls back to previous signature");
		expect(Near(report.overall.loadPercent, 5.0 / (1000.0 / 60.0) * 100.0), "load uses measured frame rate");
		MergeTally(store, current, MakeDate(2026, 11, 2), 200,
			MakeCounters(DimensionBit(Dimension::Writing), 300, 2.0, 8.33));
		MergeTally(store, current, MakeDate(2026, 11, 3), 300,
			MakeCounters(DimensionBit(Dimension::Writing) | DimensionBit(Dimension::Pan), 100, 6.0, 8.33));
		report = BuildReport(store, &current);
		expect(report.source == ReportSource::CurrentSignature, "current signature preferred");
		expect(report.usageDays == 2 && report.firstDate == MakeDate(2026, 11, 2) &&
			report.lastDate == MakeDate(2026, 11, 3), "usage days and range");
		expect(report.evaluatedUnixSeconds == 300, "evaluated time is latest merge");
		expect(report.overall.frames == 400 && Near(report.overall.avgWorkMs, 3.0), "frame-weighted across days");
		expect(Near(report.overall.unlimitedFps, 1000.0 / 3.0), "unlimited fps");
		expect(Near(report.overall.loadPercent, 3.0 / (1000.0 / 120.0) * 100.0), "overall load");
		expect(report.coverageMask == (DimensionBit(Dimension::Writing) | DimensionBit(Dimension::Pan)), "coverage");
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
		expect(ClassifyFigure(MakeOverall(1.0, 120, 0.2)) == Level::Tight, "stutter raises level to tight");
		expect(ClassifyFigure(Figure{}) == Level::None, "no data level");
	}

	// 帧率建议矩阵。
	{
		// 1. 超载：120 FPS 下每帧 10 ms（120%）→ 60 FPS 预计 60%。
		Advice advice = Advise(MakeOverall(10.0, 120), 120, 120, 120);
		expect(advice.level == Level::Overloaded && advice.suggestion == Suggestion::Lower && advice.suggestedFps == 60,
			"overloaded lowers");
		// 1b. 30 FPS 也吃力。
		advice = Advise(MakeOverall(40.0, 60), 60, 60, 60);
		expect(advice.suggestion == Suggestion::LowestHeavy, "lowest heavy");
		// 2. 平均 30%，超时 20%，240 FPS 在 120 Hz 屏 → 降到 120。
		advice = Advise(MakeOverall(1.25, 240, 0.2), 240, 240, 120);
		expect(advice.stutterNote && advice.suggestion == Suggestion::LowerToMatch && advice.suggestedFps == 120,
			"stutter above refresh lowers to match");
		// 2b. 超时多但未高于显示器：只提示，不建议升高。
		advice = Advise(MakeOverall(2.0, 60, 0.2), 60, 60, 120);
		expect(advice.stutterNote && advice.suggestion == Suggestion::None, "stutter blocks raise");
		// 3. 60 FPS、每帧 2 ms，120 Hz 屏 → 升到 120（预计 24%）。
		advice = Advise(MakeOverall(2.0, 60), 60, 60, 120);
		expect(advice.suggestion == Suggestion::Raise && advice.suggestedFps == 120, "raise to match");
		// 3b. 144 Hz 屏匹配档为 120；60 → 120。
		advice = Advise(MakeOverall(2.0, 60), 60, 60, 144);
		expect(advice.matchFps == 120 && advice.suggestedFps == 120, "144 Hz matches 120 preset");
		// 3d. 整数刷新率容差：59/119/239 Hz 视同 60/120/240；75→60、144/165→120。
		expect(MatchFrameRate(59) == 60 && MatchFrameRate(119) == 120 && MatchFrameRate(239) == 240 &&
			MatchFrameRate(75) == 60 && MatchFrameRate(144) == 120 && MatchFrameRate(165) == 120 &&
			MatchFrameRate(57) == 30 && MatchFrameRate(20) == 30, "refresh match tolerance");
		// 3c. 每帧 7 ms：120 预计 84% 不行，保持 60 合适。
		advice = Advise(MakeOverall(7.0, 60), 60, 60, 120);
		expect(advice.suggestion == Suggestion::Fine, "raise blocked by predicted load");
		// 4. 已匹配且 240 预计 ≤ 50%：可选更高。
		advice = Advise(MakeOverall(1.5, 120), 120, 120, 120);
		expect(advice.suggestion == Suggestion::MatchedCanRaise && advice.suggestedFps == 240, "matched can raise");
		// 4b. 已匹配、240 预计 72%：合适。
		advice = Advise(MakeOverall(3.0, 120), 120, 120, 120);
		expect(advice.suggestion == Suggestion::Fine, "matched fine");
		// 5. 高于刷新率、负载可承受。
		advice = Advise(MakeOverall(2.0, 240), 240, 240, 60);
		expect(advice.suggestion == Suggestion::AboveRefresh, "above refresh keeps");
		// 刷新率未知按 60。
		advice = Advise(MakeOverall(2.0, 30), 30, 30, 0);
		expect(advice.matchFps == 60 && advice.refreshHz == 0 && advice.suggestion == Suggestion::Raise &&
			advice.suggestedFps == 60, "unknown refresh treated as 60");
		// 帧率已改未重启。
		advice = Advise(MakeOverall(2.0, 120), 120, 60, 120);
		expect(advice.restartPending && advice.configuredFps == 60, "restart pending note");
		// 已从 60 重启到 120、新签名还没有数据（报告来自 60 的旧签名）：不提示重启。
		advice = Advise(MakeOverall(2.0, 60), 60, 120, 120, 120);
		expect(!advice.restartPending, "restart note compares the running session, not the old sample");
		advice = Advise(MakeOverall(2.0, 60), 60, 240, 120, 120);
		expect(advice.restartPending, "config differs from running session");
		// 无数据。
		advice = Advise(Figure{}, 120, 120, 120);
		expect(advice.level == Level::None && advice.suggestion == Suggestion::None, "no data advice");
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
			MakeCounters(DimensionBit(Dimension::Writing) | DimensionBit(Dimension::Pan), 50, 2.5, 8.33));
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
				same = a.days[d].date == b.days[d].date && a.days[d].updatedUnixSeconds == b.days[d].updatedUnixSeconds;
				for (std::size_t c = 0; same && c < kCounterCount; ++c)
					same = a.days[d].counters[c].frames == b.days[d].counters[c].frames &&
						a.days[d].counters[c].overBudget == b.days[d].counters[c].overBudget &&
						Near(a.days[d].counters[c].workMs, b.days[d].counters[c].workMs, 1e-9);
			}
		}
		expect(same, "roundtrip preserves store");
		expect(text.find("fps120-hardware-dcomp-wait1-zx0") != std::string::npos, "readable key written");

		Store untouched = store;
		expect(ParseStore("{ not json", untouched) == ParseResult::Malformed && untouched.records.size() == 2,
			"malformed text fails without touching output");
		expect(ParseStore("[]", untouched) == ParseResult::Malformed, "non-object root fails");
		expect(ParseStore(R"({"schema":"other","version":1,"signatures":[]})", untouched) == ParseResult::WrongSchema,
			"wrong schema");
		expect(ParseStore(R"({"schema":"inkeys.device-performance","version":2,"signatures":[]})", untouched) ==
			ParseResult::WrongSchema, "future version");

		// 一个日桶 overBudget > frames、一个日期非法、一个维度超过总体：都只丢弃该桶。
		const std::string partial = R"({"schema":"inkeys.device-performance","version":1,"signatures":[
			{"frameRate":120,"device":"hardware","presentation":"dcomp","waitable":1,"ulwAsyncReadback":-1,
			 "ulwCompositionAlign":-1,"zhaoxin":0,"lastUsed":100,"days":[
				{"date":20261101,"updated":100,"all":[10,20.0,11],"writing":[10,20.0,0],"highlighter":[0,0.0,0],
				 "eraser":[0,0.0,0],"laser":[0,0.0,0],"shape":[0,0.0,0],"pan":[0,0.0,0]},
				{"date":20261199,"updated":100,"all":[10,20.0,0],"writing":[10,20.0,0],"highlighter":[0,0.0,0],
				 "eraser":[0,0.0,0],"laser":[0,0.0,0],"shape":[0,0.0,0],"pan":[0,0.0,0]},
				{"date":20261102,"updated":100,"all":[10,20.0,0],"writing":[11,20.0,0],"highlighter":[0,0.0,0],
				 "eraser":[0,0.0,0],"laser":[0,0.0,0],"shape":[0,0.0,0],"pan":[0,0.0,0]},
				{"date":20261103,"updated":150,"all":[10,20.0,1],"writing":[10,20.0,1],"highlighter":[0,0.0,0],
				 "eraser":[0,0.0,0],"laser":[0,0.0,0],"shape":[0,0.0,0],"pan":[0,0.0,0]}]},
			{"frameRate":"bad","days":[]}]})";
		Store partialStore;
		dropped = 0;
		expect(ParseStore(partial, partialStore, &dropped) == ParseResult::Ok, "partial file parses");
		expect(dropped == 4 && partialStore.records.size() == 1 && partialStore.records[0].days.size() == 1 &&
			partialStore.records[0].days[0].date == MakeDate(2026, 11, 3), "invalid buckets and records dropped");
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
		const Counters counters = MakeCounters(DimensionBit(Dimension::Writing), 40, 3.0, 8.33);
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
			ledger.SetDisplayRefreshHz(120);
			expect(ledger.Snapshot(after.revision, report) && report.displayRefreshHz == 120, "refresh rate published");
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
		// 记录关闭时遇到坏文件：不改名、不覆盖（关闭即不写文件）；报告带当前会话帧率。
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
			expect(ledger.Snapshot(0, report) && report.sessionFrameRate == 120, "report carries session frame rate");
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
