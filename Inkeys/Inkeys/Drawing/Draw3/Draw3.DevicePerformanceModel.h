#pragma once

#include "Draw3.CompositionClock.h"

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
	// 档位。不依赖 Windows/D3D/模块，单测直接包含。三个帧率口径（由调用方逐帧量好传入，与画布调试一致）：
	// - 计算帧率：帧循环推进的速度（含无脏区、不 Present 的帧），帧间隔 = 相邻两轮帧循环的开始时刻之差；
	// - 渲染帧率：有脏区的帧里，真正提交（DComp Present1 / ULW UpdateLayeredWindow）且落进一个新 DWM 合成周期的帧数，
	//   按这些帧的帧间隔计。DComp 以 SyncInterval 0 提交，画布帧率高于刷新率时帧循环与 Present1 都按画布帧率跑，
	//   同一合成周期内的多次提交 DWM 只显示最新一帧，所以按合成周期计数（RefreshSlotCounter），不按提交次数；
	// - 无限制渲染帧率：整帧耗时只扣除人为等待（帧率等待、DWM 对齐、上一帧 GPU 已完成时的 waitable 显示节拍），
	//   等 GPU 的时间保留，表示拿掉这些限制后整帧能跑到的帧率。
	// 负载百分比只衡量设备能否按画布帧率推进帧循环（见 MakeFigure）；渲染帧率另含显示器与呈现同步的损失，只用于显示。
	inline constexpr std::size_t kDimensionCount = 6;
	inline constexpr std::size_t kCounterCount = kDimensionCount + 1; // [0] 总体，[1..6] 各维度
	inline constexpr std::size_t kMaxUsageDays = 7;
	inline constexpr std::size_t kMaxSignatures = 8;
	// 单段累加上限（约 2.3 小时 120 FPS 不停顿）；超过后到下次移交前不再累加，避免 double 累积误差。
	inline constexpr std::uint64_t kTallyFrameCap = 1000000;
	// 样本较少的提示阈值：总体约 5 s、单维度约 1 s（@120 FPS）。
	inline constexpr std::uint64_t kLowSampleOverallFrames = 600;
	inline constexpr std::uint64_t kLowSampleDimensionFrames = 120;
	// 档位阈值（负载百分比）与掉帧修正。
	inline constexpr double kAmpleBelowPercent = 50.0;
	inline constexpr double kModerateBelowPercent = 80.0;
	inline constexpr double kTightBelowPercent = 100.0;
	inline constexpr double kStutterOverBudgetRatio = 0.10; // 掉帧占比超过它时至少为“偏紧”
	// 帧间隔超过画布帧间隔的这个倍数记为掉帧（吸收计时器过冲与 DWM 对齐的相位微调）。
	inline constexpr double kMissedIntervalRatio = 1.5;
	// 计算帧率低于画布帧率超过 3%（缺口占比 > 103%）才算帧循环跟不上：吸收 Sleep 过冲的误差。
	inline constexpr double kComputeDeficitPercent = 103.0;
	// 显示器刷新率的合理范围（Hz），之外视为采样无效。
	inline constexpr int kMinDisplayHz = 10;
	inline constexpr int kMaxDisplayHz = 1000;

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
		std::uint64_t frames = 0;          // 渲染帧：有脏区且 Present 成功（ULW 节流合并跳过的帧也算，它照常画进了纹理）
		double costMs = 0.0;               // 渲染帧的无限制渲染耗时之和
		std::uint64_t timedFrames = 0;     // 渲染帧中帧间隔有效的帧
		std::uint64_t displayedFrames = 0; // timedFrames 中提交落进新 DWM 合成周期的帧（实际上屏的帧）
		double intervalMs = 0.0;           // timedFrames 的帧间隔之和
		std::uint64_t missed = 0;          // timedFrames 中掉帧（帧间隔 > 1.5 × 画布帧间隔）的帧
		std::uint64_t computeFrames = 0;   // 帧间隔有效的计算帧（含无脏区帧）
		double computeIntervalMs = 0.0;

		void Merge(const Counter& other) noexcept
		{
			frames += other.frames;
			costMs += other.costMs;
			timedFrames += other.timedFrames;
			displayedFrames += other.displayedFrames;
			intervalMs += other.intervalMs;
			missed += other.missed;
			computeFrames += other.computeFrames;
			computeIntervalMs += other.computeIntervalMs;
		}
		bool operator==(const Counter&) const = default;
	};

	using Counters = std::array<Counter, kCounterCount>;

	inline void MergeCounters(Counters& into, const Counters& from) noexcept
	{
		for (std::size_t i = 0; i < kCounterCount; ++i) into[i].Merge(from[i]);
	}

	// 计数期间测得的显示器刷新率范围（整数 Hz），0 = 未测到。
	struct DisplayRange
	{
		std::uint16_t minHz = 0;
		std::uint16_t maxHz = 0;

		bool Valid() const noexcept { return minHz > 0 && maxHz >= minHz; }
		void Observe(int hz) noexcept
		{
			if (hz < kMinDisplayHz || hz > kMaxDisplayHz) return;
			const auto value = static_cast<std::uint16_t>(hz);
			minHz = minHz == 0 ? value : (std::min)(minHz, value);
			maxHz = (std::max)(maxHz, value);
		}
		void Merge(const DisplayRange& other) noexcept
		{
			if (!other.Valid()) return;
			Observe(other.minHz);
			Observe(other.maxHz);
		}
		bool operator==(const DisplayRange&) const = default;
	};

	// 按 DWM 合成节奏判断一次提交是否开启了新的合成周期：提交落在“合成截止 = 垂直同步 − 余量”之前就由该次合成取走，
	// 同一周期内的后续提交只会替换内容，DWM 仍只显示一帧。没有合成时序（DWM 未合成、读取失败）时每次提交都算新帧。
	struct RefreshSlotCounter
	{
		bool hasLast = false;
		double lastDeadlineMs = 0.0;

		// commitMs 为提交完成时刻；vblankMs/periodMs 为 DWM 合成时序（周期保留小数，可来自稍早的采样，时刻可外推）。
		bool Observe(double commitMs, double vblankMs, double periodMs) noexcept
		{
			if (!std::isfinite(commitMs)) return false;
			if (!(std::isfinite(vblankMs) && std::isfinite(periodMs) && periodMs >= kCompositionClockMinPeriodMs &&
				periodMs <= kCompositionClockMaxPeriodMs))
			{
				hasLast = false;
				return true;
			}
			const double margin = kCompositionDeadlineMarginMs;
			// 不早于提交时刻的第一个合成截止（与 CompositionClock::DeadlineAtOrAfter 同式）。
			const double deadline = vblankMs + std::ceil((commitMs + margin - vblankMs) / periodMs - 1e-9) * periodMs - margin;
			// 时序每 250 ms 重读、周期随动态刷新率变化，截止时刻有微小漂移：差半个周期以内视为同一周期。
			if (hasLast && deadline < lastDeadlineMs + periodMs * 0.5) return false;
			hasLast = true;
			lastDeadlineMs = deadline;
			return true;
		}
		void Reset() noexcept { hasLast = false; }
	};

	// 一轮帧循环的测量结果（绘制线程在下一轮开始时结算）。
	struct FrameSample
	{
		bool rendered = false;    // 有脏区且 Present 成功
		bool displayed = false;   // 本轮真正提交且落进新的 DWM 合成周期（见 RefreshSlotCounter）
		double costMs = -1.0;     // 无限制渲染耗时，rendered 时有效
		double intervalMs = -1.0; // 本轮开始到下一轮开始；本轮不是以帧率等待结束时无效（< 0）
		double targetMs = 0.0;    // 画布帧间隔（1000 / 画布帧率），判断掉帧
	};

	// 绘制线程独占的一段累加结果：每个计数帧只做常数次加法，无锁、无分配。
	struct Tally
	{
		Counters counters{};
		DisplayRange display;

		bool Empty() const noexcept { return counters[0].frames == 0 && counters[0].computeFrames == 0; }
		void Clear() noexcept
		{
			counters = {};
			display = {};
		}

		// 返回是否计入。mask 为空、数值非法、既不是渲染帧也没有有效帧间隔，或达到单段上限时不计。
		bool Add(std::uint32_t dimensionMask, const FrameSample& sample) noexcept
		{
			dimensionMask &= kAllDimensionsMask;
			if (dimensionMask == 0) return false;
			const bool rendered = sample.rendered && std::isfinite(sample.costMs) && sample.costMs >= 0.0;
			const bool timed = std::isfinite(sample.intervalMs) && sample.intervalMs > 0.0 &&
				std::isfinite(sample.targetMs) && sample.targetMs > 0.0;
			if (!rendered && !timed) return false;
			if ((std::max)(counters[0].frames, counters[0].computeFrames) >= kTallyFrameCap) return false;
			const bool missed = timed && sample.intervalMs > sample.targetMs * kMissedIntervalRatio;
			const auto add = [&](Counter& counter)
			{
				if (rendered)
				{
					++counter.frames;
					counter.costMs += sample.costMs;
					if (timed)
					{
						++counter.timedFrames;
						if (sample.displayed) ++counter.displayedFrames;
						counter.intervalMs += sample.intervalMs;
						if (missed) ++counter.missed;
					}
				}
				if (timed)
				{
					++counter.computeFrames;
					counter.computeIntervalMs += sample.intervalMs;
				}
			};
			add(counters[0]); // 总体每帧只计一次；多工具同帧时各维度都计
			for (std::size_t i = 0; i < kDimensionCount; ++i)
				if ((dimensionMask & (1u << i)) != 0) add(counters[i + 1]);
			return true;
		}
	};

	// ---- 设置签名 ----

	enum class Device : std::uint8_t { Unknown, Hardware, Warp };
	enum class Presentation : std::uint8_t { Unknown, DirectComposition, Ulw, Win7DwmFlip, Win7DwmSequential, DCompSequential };
	inline constexpr std::uint8_t kNotApplicable = 0xFF;

	struct Signature
	{
		std::uint16_t frameRate = 0;
		Device device = Device::Unknown;
		Presentation presentation = Presentation::Unknown;
		std::uint8_t waitable = kNotApplicable;            // 仅 DComp 有意义
		std::uint8_t ulwAsyncReadback = kNotApplicable;    // 仅 ULW 有意义
		std::uint8_t ulwCompositionAlign = kNotApplicable; // 仅 ULW 且异步读回开启时有意义
		std::uint8_t dcompCompositionAlign = kNotApplicable; // 仅 DComp（含 SEQUENTIAL 链）有意义
		std::uint8_t zhaoxinCompatibility = 0;
		bool operator==(const Signature&) const = default;
	};

	// 与当前呈现模式无关的开关写成“不适用”，切换不起作用的开关不会把数据拆开。
	constexpr Signature NormalizeSignature(Signature signature) noexcept
	{
		const auto flag = [](std::uint8_t value) -> std::uint8_t
		{ return value == kNotApplicable ? kNotApplicable : static_cast<std::uint8_t>(value != 0 ? 1 : 0); };
		signature.zhaoxinCompatibility = signature.zhaoxinCompatibility != 0 ? 1 : 0;
		// 阶段 E 之前的账本没有该字段：DComp 记录按“未对齐”读入，其余后端一律不适用。
		const bool dcomp = signature.presentation == Presentation::DirectComposition ||
			signature.presentation == Presentation::DCompSequential;
		signature.dcompCompositionAlign = dcomp
			? (signature.dcompCompositionAlign == kNotApplicable ? 0 : flag(signature.dcompCompositionAlign))
			: kNotApplicable;
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
		case Presentation::DCompSequential: return "dcomp-sequential";
		case Presentation::Ulw: return "ulw";
		case Presentation::Win7DwmFlip: return "win7dwm-flip";
		case Presentation::Win7DwmSequential: return "win7dwm-sequential";
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
		if (signature.dcompCompositionAlign != kNotApplicable) key += "-dalign" + std::to_string(signature.dcompCompositionAlign);
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
		DisplayRange display;
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
			it->display.Merge(bucket.display);
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
		std::int64_t nowUnixSeconds, const Counters& counters, const DisplayRange& display = {})
	{
		if (counters[0].frames == 0 || !IsValidDate(date)) return false;
		const Signature signature = NormalizeSignature(rawSignature);
		SignatureRecord& record = FindOrAddRecord(store, signature);
		record.lastUsedUnixSeconds = (std::max)(record.lastUsedUnixSeconds, nowUnixSeconds);
		MergeDay(record, DayBucket{ date, counters, display, nowUnixSeconds });
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
		double renderFps = 0.0;    // 平均渲染帧率（按 DWM 合成周期实测）；没有有效帧间隔时为 0
		double computeFps = 0.0;   // 平均计算帧率；没有有效帧间隔时为 0
		double unlimitedFps = 0.0; // 无限制渲染帧率
		double costPercent = 0.0;  // 平均无限制渲染耗时 / 画布帧间隔
		double loadPercent = 0.0;  // 负载百分比（档位依据）
		double overBudgetRatio = 0.0; // 掉帧占比（帧间隔有效的渲染帧中）
		bool lowSample = false;
	};

	// frameRate 为签名的画布帧率。负载百分比只看设备能否按画布帧率推进帧循环：
	// - 耗时占比 = 平均无限制渲染耗时 / 画布帧间隔：帧循环按画布帧率推进（画布帧率高于刷新率时也是），
	//   每帧都要装进画布帧间隔；
	// - 缺口占比 = 画布帧率 / 计算帧率：计算帧率比画布帧率低 3% 以上（帧循环跟不上）时取 max(缺口占比, 耗时占比)，
	//   至少为超载；否则取耗时占比，表示还剩多少余量。GPU 吃紧时 DComp 的 waitable 阻塞、ULW 的 Map 等待都会拖慢帧循环。
	// 渲染帧率含显示器刷新率与呈现同步（ULW 读回、DWM 合成周期）的损失，不参与负载，避免把同步损失当成性能不足。
	inline Figure MakeFigure(const Counter& counter, int frameRate, std::uint64_t lowSampleFrames) noexcept
	{
		Figure figure;
		if (counter.frames == 0 || frameRate <= 0) return figure;
		figure.available = true;
		figure.frames = counter.frames;
		const double frames = static_cast<double>(counter.frames);
		// 耗时之和极小（理论上不会出现）时不给出无穷大的帧率。
		figure.unlimitedFps = counter.costMs > 1e-6 ? frames / (counter.costMs / 1000.0) : 0.0;
		figure.costPercent = counter.costMs / frames / (1000.0 / frameRate) * 100.0;
		figure.loadPercent = figure.costPercent;
		if (counter.timedFrames > 0 && counter.intervalMs > 1e-6)
		{
			figure.renderFps = static_cast<double>(counter.displayedFrames) / (counter.intervalMs / 1000.0);
			figure.overBudgetRatio = static_cast<double>(counter.missed) / static_cast<double>(counter.timedFrames);
		}
		if (counter.computeFrames > 0 && counter.computeIntervalMs > 1e-6)
		{
			figure.computeFps = static_cast<double>(counter.computeFrames) / (counter.computeIntervalMs / 1000.0);
			const double deficitPercent = frameRate / figure.computeFps * 100.0;
			if (deficitPercent > kComputeDeficitPercent)
				figure.loadPercent = (std::max)(figure.costPercent, deficitPercent);
		}
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
		DisplayRange display; // 这些数据计数期间测得的显示器刷新率范围
		std::int64_t evaluatedUnixSeconds = 0;
		bool recordingEnabled = true;
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
			report.display.Merge(day.display);
			report.evaluatedUnixSeconds = (std::max)(report.evaluatedUnixSeconds, day.updatedUnixSeconds);
		}
		report.signature = chosen->signature;
		const int frameRate = chosen->signature.frameRate;
		report.overall = MakeFigure(total[0], frameRate, kLowSampleOverallFrames);
		for (std::size_t i = 0; i < kDimensionCount; ++i)
			report.dimensions[i] = MakeFigure(total[i + 1], frameRate, kLowSampleDimensionFrames);
		if (!report.overall.available) report.source = ReportSource::None;
		return report;
	}

	// ---- 档位（看板只显示档位结论，不给帧率建议）----

	enum class Level : std::uint8_t { None, Ample, Moderate, Tight, Overloaded };

	constexpr Level ClassifyLoad(double loadPercent) noexcept
	{
		if (!(loadPercent >= 0.0)) return Level::None;
		if (loadPercent < kAmpleBelowPercent) return Level::Ample;
		if (loadPercent < kModerateBelowPercent) return Level::Moderate;
		if (loadPercent < kTightBelowPercent) return Level::Tight;
		return Level::Overloaded;
	}

	// 平均值会掩盖卡顿：掉帧占比超过 10% 时至少为“偏紧”。
	inline Level ClassifyFigure(const Figure& figure) noexcept
	{
		if (!figure.available) return Level::None;
		Level level = ClassifyLoad(figure.loadPercent);
		if (figure.overBudgetRatio > kStutterOverBudgetRatio && level < Level::Tight) level = Level::Tight;
		return level;
	}
}
