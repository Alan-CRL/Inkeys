#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Inkeys::Drawing::Draw3::DevicePerformance
{
	// 设备性能看板的纯逻辑：维度划分、逐帧累加、按“设置签名 × 本地日”分桶、最近 7 个使用日汇总、
	// 档位与帧率建议。不依赖 Windows/D3D/模块，单测直接包含。工作耗时口径与画布调试一致：
	// 帧耗时扣除帧延迟等待与等 GPU（由调用方传入扣除后的值）。
	inline constexpr std::size_t kDimensionCount = 6;
	inline constexpr std::size_t kCounterCount = kDimensionCount + 1; // [0] 总体，[1..6] 各维度
	inline constexpr std::size_t kMaxUsageDays = 7;
	inline constexpr std::size_t kMaxSignatures = 8;
	// 单段累加上限（约 2.3 小时 120 FPS 不停顿）；超过后到下次移交前不再累加，避免 double 累积误差。
	inline constexpr std::uint64_t kTallyFrameCap = 1000000;
	// 样本较少的提示阈值：总体约 5 s、单维度约 1 s（@120 FPS）。
	inline constexpr std::uint64_t kLowSampleOverallFrames = 600;
	inline constexpr std::uint64_t kLowSampleDimensionFrames = 120;
	// 档位阈值（负载百分比）与超时帧修正。
	inline constexpr double kAmpleBelowPercent = 50.0;
	inline constexpr double kModerateBelowPercent = 80.0;
	inline constexpr double kTightBelowPercent = 100.0;
	inline constexpr double kStutterOverBudgetRatio = 0.10;
	// 帧率建议的预计负载上限：降低时的目标、升高时留余量、已匹配后的“可选更高”。
	inline constexpr double kLowerTargetPercent = 80.0;
	inline constexpr double kRaiseMaxPercent = 70.0;
	inline constexpr double kOptionalRaiseMaxPercent = 50.0;
	inline constexpr std::array<int, 4> kFrameRatePresets = { 30, 60, 120, 240 };

	enum class Dimension : std::uint8_t { Writing, Highlighter, Eraser, Laser, Shape, Pan };

	constexpr std::uint32_t DimensionBit(Dimension dimension) noexcept
	{
		return 1u << static_cast<std::uint32_t>(dimension);
	}

	inline constexpr std::uint32_t kAllDimensionsMask = (1u << kDimensionCount) - 1u;

	// drawingTool 为 Draw3 `DrawingTool` 的整数值（Pen=0 … Shape=9）；用整数避免本头依赖模块。
	constexpr std::uint32_t ToolDimensionMask(int drawingTool) noexcept
	{
		switch (drawingTool)
		{
		case 0: // Pen
		case 1: // HardPen
			return DimensionBit(Dimension::Writing);
		case 2: return DimensionBit(Dimension::Highlighter);
		case 3: return DimensionBit(Dimension::Eraser);
		case 4: return DimensionBit(Dimension::Laser);
		case 5: // SolidLine
		case 6: // DashedLine
		case 7: // OutlineRectangle
		case 8: // FilledRectangle
		case 9: // Shape
			return DimensionBit(Dimension::Shape);
		default: return 0u;
		}
	}

	struct Counter
	{
		std::uint64_t frames = 0;
		double workMs = 0.0;
		std::uint64_t overBudget = 0;

		void Merge(const Counter& other) noexcept
		{
			frames += other.frames;
			workMs += other.workMs;
			overBudget += other.overBudget;
		}
		bool operator==(const Counter&) const = default;
	};

	using Counters = std::array<Counter, kCounterCount>;

	inline void MergeCounters(Counters& into, const Counters& from) noexcept
	{
		for (std::size_t i = 0; i < kCounterCount; ++i) into[i].Merge(from[i]);
	}

	// 绘制线程独占的一段累加结果：每个计数帧只做常数次加法，无锁、无分配。
	struct Tally
	{
		Counters counters{};

		bool Empty() const noexcept { return counters[0].frames == 0; }
		void Clear() noexcept { counters = {}; }

		// 返回是否计入。mask 为空、耗时非法或达到单段上限时不计。
		bool Add(std::uint32_t dimensionMask, double workMs, double budgetMs) noexcept
		{
			dimensionMask &= kAllDimensionsMask;
			if (dimensionMask == 0 || !std::isfinite(workMs) || workMs < 0.0) return false;
			if (counters[0].frames >= kTallyFrameCap) return false;
			const bool over = budgetMs > 0.0 && workMs > budgetMs;
			const auto add = [&](Counter& counter)
			{
				++counter.frames;
				counter.workMs += workMs;
				if (over) ++counter.overBudget;
			};
			add(counters[0]); // 总体每帧只计一次；多工具同帧时各维度都计
			for (std::size_t i = 0; i < kDimensionCount; ++i)
				if ((dimensionMask & (1u << i)) != 0) add(counters[i + 1]);
			return true;
		}
	};

	// ---- 设置签名 ----

	enum class Device : std::uint8_t { Unknown, Hardware, Warp };
	enum class Presentation : std::uint8_t { Unknown, DirectComposition, Ulw };
	inline constexpr std::uint8_t kNotApplicable = 0xFF;

	struct Signature
	{
		std::uint16_t frameRate = 0;
		Device device = Device::Unknown;
		Presentation presentation = Presentation::Unknown;
		std::uint8_t waitable = kNotApplicable;            // 仅 DComp 有意义
		std::uint8_t ulwAsyncReadback = kNotApplicable;    // 仅 ULW 有意义
		std::uint8_t ulwCompositionAlign = kNotApplicable; // 仅 ULW 且异步读回开启时有意义
		std::uint8_t zhaoxinCompatibility = 0;
		bool operator==(const Signature&) const = default;
	};

	// 与当前呈现模式无关的开关写成“不适用”，切换不起作用的开关不会把数据拆开。
	constexpr Signature NormalizeSignature(Signature signature) noexcept
	{
		const auto flag = [](std::uint8_t value) -> std::uint8_t
		{ return value == kNotApplicable ? kNotApplicable : static_cast<std::uint8_t>(value != 0 ? 1 : 0); };
		signature.zhaoxinCompatibility = signature.zhaoxinCompatibility != 0 ? 1 : 0;
		if (signature.presentation == Presentation::DirectComposition)
		{
			signature.waitable = signature.waitable == kNotApplicable ? 0 : flag(signature.waitable);
			signature.ulwAsyncReadback = kNotApplicable;
			signature.ulwCompositionAlign = kNotApplicable;
		}
		else if (signature.presentation == Presentation::Ulw)
		{
			signature.waitable = kNotApplicable;
			signature.ulwAsyncReadback = signature.ulwAsyncReadback == kNotApplicable ? 0 : flag(signature.ulwAsyncReadback);
			signature.ulwCompositionAlign = signature.ulwAsyncReadback == 1
				? (signature.ulwCompositionAlign == kNotApplicable ? 0 : flag(signature.ulwCompositionAlign))
				: kNotApplicable;
		}
		else
		{
			signature.waitable = kNotApplicable;
			signature.ulwAsyncReadback = kNotApplicable;
			signature.ulwCompositionAlign = kNotApplicable;
		}
		return signature;
	}

	inline const char* DeviceName(Device device) noexcept
	{
		switch (device)
		{
		case Device::Hardware: return "hardware";
		case Device::Warp: return "warp";
		default: return "unknown";
		}
	}

	inline const char* PresentationName(Presentation presentation) noexcept
	{
		switch (presentation)
		{
		case Presentation::DirectComposition: return "dcomp";
		case Presentation::Ulw: return "ulw";
		default: return "unknown";
		}
	}

	// 人可读的签名键，写进 JSON 便于排查，例如 fps120-hardware-dcomp-wait1-zx0。
	inline std::string SignatureKey(const Signature& raw)
	{
		const Signature signature = NormalizeSignature(raw);
		std::string key = "fps" + std::to_string(signature.frameRate) + "-" + DeviceName(signature.device) +
			"-" + PresentationName(signature.presentation);
		if (signature.waitable != kNotApplicable) key += "-wait" + std::to_string(signature.waitable);
		if (signature.ulwAsyncReadback != kNotApplicable) key += "-async" + std::to_string(signature.ulwAsyncReadback);
		if (signature.ulwCompositionAlign != kNotApplicable) key += "-align" + std::to_string(signature.ulwCompositionAlign);
		key += "-zx" + std::to_string(signature.zhaoxinCompatibility);
		return key;
	}

	// ---- 日桶与存储 ----

	// 本地日期 yyyymmdd；只做排序与显示，不做日历运算。
	constexpr bool IsValidDate(std::int32_t date) noexcept
	{
		const std::int32_t year = date / 10000, month = date / 100 % 100, day = date % 100;
		return year >= 2000 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
	}

	constexpr std::int32_t MakeDate(int year, int month, int day) noexcept
	{
		return static_cast<std::int32_t>(year * 10000 + month * 100 + day);
	}

	struct DayBucket
	{
		std::int32_t date = 0;
		Counters counters{};
		std::int64_t updatedUnixSeconds = 0;
	};

	struct SignatureRecord
	{
		Signature signature;
		std::int64_t lastUsedUnixSeconds = 0;
		std::vector<DayBucket> days; // 按日期升序，最多 kMaxUsageDays 个
	};

	struct Store
	{
		std::vector<SignatureRecord> records;
	};

	// 合并一个日桶：同日相加，新日插入；超过 7 个使用日时淘汰最早的日期（含时间回拨后插入的旧日期）。
	inline void MergeDay(SignatureRecord& record, const DayBucket& bucket)
	{
		auto it = std::lower_bound(record.days.begin(), record.days.end(), bucket.date,
			[](const DayBucket& day, std::int32_t date) { return day.date < date; });
		if (it != record.days.end() && it->date == bucket.date)
		{
			MergeCounters(it->counters, bucket.counters);
			it->updatedUnixSeconds = (std::max)(it->updatedUnixSeconds, bucket.updatedUnixSeconds);
		}
		else record.days.insert(it, bucket);
		if (record.days.size() > kMaxUsageDays)
			record.days.erase(record.days.begin(), record.days.begin() +
				static_cast<std::ptrdiff_t>(record.days.size() - kMaxUsageDays));
	}

	inline SignatureRecord& FindOrAddRecord(Store& store, const Signature& signature)
	{
		for (SignatureRecord& record : store.records)
			if (record.signature == signature) return record;
		SignatureRecord record;
		record.signature = signature;
		store.records.push_back(std::move(record));
		return store.records.back();
	}

	// 签名超过上限时淘汰最久未用的（keep 指向的签名永不淘汰）。
	inline void PruneSignatures(Store& store, const Signature* keep = nullptr)
	{
		while (store.records.size() > kMaxSignatures)
		{
			auto victim = store.records.end();
			for (auto it = store.records.begin(); it != store.records.end(); ++it)
			{
				if (keep && it->signature == *keep) continue;
				if (victim == store.records.end() || it->lastUsedUnixSeconds < victim->lastUsedUnixSeconds) victim = it;
			}
			if (victim == store.records.end()) break;
			store.records.erase(victim);
		}
	}

	// 把一段累加结果合并进“date × 签名”日桶。空段或非法日期不合并。
	inline bool MergeTally(Store& store, const Signature& rawSignature, std::int32_t date,
		std::int64_t nowUnixSeconds, const Counters& counters)
	{
		if (counters[0].frames == 0 || !IsValidDate(date)) return false;
		const Signature signature = NormalizeSignature(rawSignature);
		SignatureRecord& record = FindOrAddRecord(store, signature);
		record.lastUsedUnixSeconds = (std::max)(record.lastUsedUnixSeconds, nowUnixSeconds);
		MergeDay(record, DayBucket{ date, counters, nowUnixSeconds });
		PruneSignatures(store, &signature);
		return true;
	}

	// 加载文件后与启动后已在内存累积的数据相加合并（加法可交换，顺序无关）。
	inline void MergeStore(Store& into, const Store& from)
	{
		for (const SignatureRecord& source : from.records)
		{
			const Signature signature = NormalizeSignature(source.signature);
			SignatureRecord& record = FindOrAddRecord(into, signature);
			record.lastUsedUnixSeconds = (std::max)(record.lastUsedUnixSeconds, source.lastUsedUnixSeconds);
			for (const DayBucket& day : source.days)
				if (IsValidDate(day.date)) MergeDay(record, day);
		}
		PruneSignatures(into);
	}

	// ---- 报告 ----

	struct Figure
	{
		bool available = false;
		std::uint64_t frames = 0;
		double avgWorkMs = 0.0;
		double loadPercent = 0.0;
		double unlimitedFps = 0.0;
		double overBudgetRatio = 0.0;
		bool lowSample = false;
	};

	inline Figure MakeFigure(const Counter& counter, int frameRate, std::uint64_t lowSampleFrames) noexcept
	{
		Figure figure;
		if (counter.frames == 0 || frameRate <= 0) return figure;
		figure.available = true;
		figure.frames = counter.frames;
		figure.avgWorkMs = counter.workMs / static_cast<double>(counter.frames);
		figure.loadPercent = figure.avgWorkMs / (1000.0 / frameRate) * 100.0;
		// 工作耗时之和极小（理论上不会出现）时不给出无穷大的帧率。
		figure.unlimitedFps = counter.workMs > 1e-6 ? static_cast<double>(counter.frames) / (counter.workMs / 1000.0) : 0.0;
		figure.overBudgetRatio = static_cast<double>(counter.overBudget) / static_cast<double>(counter.frames);
		figure.lowSample = counter.frames < lowSampleFrames;
		return figure;
	}

	enum class ReportSource : std::uint8_t { None, CurrentSignature, PreviousSignature };

	struct Report
	{
		std::uint64_t revision = 0;
		ReportSource source = ReportSource::None;
		Signature signature;
		Figure overall;
		std::array<Figure, kDimensionCount> dimensions{};
		std::uint32_t usageDays = 0;
		std::int32_t firstDate = 0;
		std::int32_t lastDate = 0;
		std::int64_t evaluatedUnixSeconds = 0;
		std::uint32_t coverageMask = 0;
		bool recordingEnabled = true;
		std::uint32_t displayRefreshHz = 0; // 当前会话画布显示器模式刷新率，不入盘；0 = 未知
		std::uint16_t sessionFrameRate = 0; // 当前会话实际运行的画布帧率（当前签名），0 = 未知；用于“重启后生效”判断
	};

	// 当前签名有数据就用它；否则取最近使用的其他签名（“设置变更前”）；都没有则为空。
	inline Report BuildReport(const Store& store, const Signature* currentRaw)
	{
		Report report;
		const SignatureRecord* chosen = nullptr;
		if (currentRaw)
		{
			const Signature current = NormalizeSignature(*currentRaw);
			for (const SignatureRecord& record : store.records)
				if (record.signature == current && !record.days.empty()) chosen = &record;
			if (chosen) report.source = ReportSource::CurrentSignature;
		}
		if (!chosen)
		{
			for (const SignatureRecord& record : store.records)
				if (!record.days.empty() && (!chosen || record.lastUsedUnixSeconds > chosen->lastUsedUnixSeconds))
					chosen = &record;
			if (chosen) report.source = ReportSource::PreviousSignature;
		}
		if (!chosen) return report;

		Counters total{};
		for (const DayBucket& day : chosen->days)
		{
			MergeCounters(total, day.counters);
			report.evaluatedUnixSeconds = (std::max)(report.evaluatedUnixSeconds, day.updatedUnixSeconds);
		}
		report.signature = chosen->signature;
		report.usageDays = static_cast<std::uint32_t>(chosen->days.size());
		report.firstDate = chosen->days.front().date;
		report.lastDate = chosen->days.back().date;
		const int frameRate = chosen->signature.frameRate;
		report.overall = MakeFigure(total[0], frameRate, kLowSampleOverallFrames);
		for (std::size_t i = 0; i < kDimensionCount; ++i)
		{
			report.dimensions[i] = MakeFigure(total[i + 1], frameRate, kLowSampleDimensionFrames);
			if (report.dimensions[i].available) report.coverageMask |= 1u << i;
		}
		if (!report.overall.available) report.source = ReportSource::None;
		return report;
	}

	// ---- 档位与帧率建议 ----

	enum class Level : std::uint8_t { None, Ample, Moderate, Tight, Overloaded };

	constexpr Level ClassifyLoad(double loadPercent) noexcept
	{
		if (!(loadPercent >= 0.0)) return Level::None;
		if (loadPercent < kAmpleBelowPercent) return Level::Ample;
		if (loadPercent < kModerateBelowPercent) return Level::Moderate;
		if (loadPercent < kTightBelowPercent) return Level::Tight;
		return Level::Overloaded;
	}

	// 平均值会掩盖卡顿：超时帧占比超过 10% 时至少为“偏紧”。
	inline Level ClassifyFigure(const Figure& figure) noexcept
	{
		if (!figure.available) return Level::None;
		Level level = ClassifyLoad(figure.loadPercent);
		if (figure.overBudgetRatio > kStutterOverBudgetRatio && level < Level::Tight) level = Level::Tight;
		return level;
	}

	enum class Suggestion : std::uint8_t
	{
		None,          // 无数据，或只给超时提示
		Lower,         // 建议降低到 suggestedFps
		LowestHeavy,   // 即使最低帧率也较吃力
		LowerToMatch,  // 平均不高但超时帧多，且高于刷新率：降到与显示器一致
		Raise,         // 可以升高到 suggestedFps（不超过与显示器一致的档位）
		MatchedCanRaise, // 已与显示器一致，余量大，可选更高档（不会更流畅，但墨迹更精确平滑）
		AboveRefresh,  // 当前高于显示器刷新率，负载允许保持
		Fine           // 当前帧率设置合适
	};

	struct Advice
	{
		Level level = Level::None;
		Suggestion suggestion = Suggestion::None;
		int suggestedFps = 0;
		int matchFps = 0;           // 不超过显示器刷新率的最大预设档
		int refreshHz = 0;          // 0 = 未知（按 60 Hz 推算 matchFps）
		bool stutterNote = false;   // 平均不高但超时帧占比 > 10%
		double overBudgetPercent = 0.0;
		bool restartPending = false; // 配置帧率与测得帧率不同（重启后生效）
		int configuredFps = 0;
	};

	// 显示模式频率是向下取整的整数，59.94/119.88 Hz 屏常报 59/119：留 2 Hz 容差，避免建议系统性偏低一档。
	inline constexpr int kRefreshMatchToleranceHz = 2;

	constexpr int MatchFrameRate(int refreshHz) noexcept
	{
		const int refresh = refreshHz > 0 ? refreshHz : 60;
		int best = kFrameRatePresets.front();
		for (const int preset : kFrameRatePresets)
			if (preset <= refresh + kRefreshMatchToleranceHz) best = preset;
		return best;
	}

	// 按每帧工作量不变线性外推的预计负载（%）；文案统一用“约”。
	constexpr double PredictLoadPercent(double avgWorkMs, int fps) noexcept
	{
		return avgWorkMs * fps / 10.0;
	}

	// sessionFps：当前会话实际运行的帧率；0 时按 measuredFps。“重启后生效”只比较配置与当前会话，
	// 不能比较历史样本的帧率（已重启到新帧率、新签名还没有数据时会误报）。
	inline Advice Advise(const Figure& overall, int measuredFps, int configuredFps, int refreshHz, int sessionFps = 0) noexcept
	{
		Advice advice;
		advice.refreshHz = refreshHz > 0 ? refreshHz : 0;
		advice.matchFps = MatchFrameRate(refreshHz);
		advice.configuredFps = configuredFps;
		const int runningFps = sessionFps > 0 ? sessionFps : measuredFps;
		advice.restartPending = configuredFps > 0 && runningFps > 0 && configuredFps != runningFps;
		advice.level = ClassifyFigure(overall);
		if (!overall.available || measuredFps <= 0) return advice;
		advice.overBudgetPercent = overall.overBudgetRatio * 100.0;
		const double load = overall.loadPercent;
		const double work = overall.avgWorkMs;

		// 1. 平均负载 ≥ 80%：降到预计负载 ≤ 80% 的最高档。
		if (load >= kLowerTargetPercent)
		{
			for (const int preset : kFrameRatePresets)
				if (preset < measuredFps && PredictLoadPercent(work, preset) <= kLowerTargetPercent)
					advice.suggestedFps = preset;
			advice.suggestion = advice.suggestedFps > 0 ? Suggestion::Lower : Suggestion::LowestHeavy;
			return advice;
		}
		// 2. 平均不高但超时帧多：高于显示器时降到与显示器一致；否则只给超时提示。
		if (overall.overBudgetRatio > kStutterOverBudgetRatio)
		{
			advice.stutterNote = true;
			if (measuredFps > advice.matchFps)
			{
				advice.suggestion = Suggestion::LowerToMatch;
				advice.suggestedFps = advice.matchFps;
			}
			return advice;
		}
		// 3. 低于显示器：在 (当前, 匹配档] 中取预计负载 ≤ 70% 的最高档。
		if (measuredFps < advice.matchFps)
		{
			for (const int preset : kFrameRatePresets)
				if (preset > measuredFps && preset <= advice.matchFps && PredictLoadPercent(work, preset) <= kRaiseMaxPercent)
					advice.suggestedFps = preset;
			advice.suggestion = advice.suggestedFps > 0 ? Suggestion::Raise : Suggestion::Fine;
			return advice;
		}
		// 4. 已与显示器一致：上一档余量很大时说明可选更高（不是建议）。
		if (measuredFps == advice.matchFps)
		{
			for (const int preset : kFrameRatePresets)
				if (preset > measuredFps)
				{
					if (PredictLoadPercent(work, preset) <= kOptionalRaiseMaxPercent)
					{
						advice.suggestion = Suggestion::MatchedCanRaise;
						advice.suggestedFps = preset;
						return advice;
					}
					break;
				}
			advice.suggestion = Suggestion::Fine;
			return advice;
		}
		// 5. 高于显示器（且负载 < 80%）：说明不会更流畅但墨迹更精确平滑，允许保持。
		advice.suggestion = Suggestion::AboveRefresh;
		return advice;
	}
}
