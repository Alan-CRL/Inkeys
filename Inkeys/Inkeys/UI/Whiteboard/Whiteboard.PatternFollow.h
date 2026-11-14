#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../../Drawing/Draw3/Draw3.WhiteboardPattern.h"

// Freeze 自绘点阵/网格随 Draw3 视口平移的纯逻辑：无锁最新视口通道、相位换算、重画规划与诊断聚合。
// 不依赖 D2D/窗口，生产（Whiteboard.cpp）与 Headless 测试共用。
namespace Inkeys::UI::WhiteboardFollow
{
	using Sample = Inkeys::Drawing::Draw3::WhiteboardViewportSample;

	// 运动期相位位移小于该值（物理像素）不重画；落定帧（moving=false）不受此限，必须精确。
	inline constexpr double kMovingEpsilonPx = 0.25;
	inline constexpr unsigned kReadSpinLimit = 256;

	enum class ReadResult : std::uint8_t { Empty, Ok, Contended };

	// 单写者（Draw3 绘制线程）/单读者（UI3 渲染线程）的 seqlock。写端只做若干 relaxed 原子存储，
	// 无锁、无分配、无系统调用；读端遇到写入中的奇数序号自旋重试，超过上限返回 Contended，由调用方稍后重试。
	class ViewportChannel
	{
	public:
		void Write(const Sample& value) noexcept
		{
			const std::uint64_t sequence = sequence_.load(std::memory_order_relaxed);
			sequence_.store(sequence + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			words_[0].store((value.active ? 1ULL : 0ULL) | (value.moving ? 2ULL : 0ULL)
				| (static_cast<std::uint64_t>(value.page) << 32), std::memory_order_relaxed);
			words_[1].store(value.generation, std::memory_order_relaxed);
			words_[2].store(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value.x))
				| (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value.y)) << 32), std::memory_order_relaxed);
			words_[3].store(value.serial, std::memory_order_relaxed);
			words_[4].store(static_cast<std::uint64_t>(value.qpc), std::memory_order_relaxed);
			sequence_.store(sequence + 2, std::memory_order_release);
		}

		[[nodiscard]] ReadResult Read(Sample& out) const noexcept
		{
			for (unsigned attempt = 0; attempt < kReadSpinLimit; ++attempt)
			{
				const std::uint64_t before = sequence_.load(std::memory_order_acquire);
				if (before == 0) return ReadResult::Empty;
				if (before & 1ULL) continue;
				const std::uint64_t flags = words_[0].load(std::memory_order_relaxed);
				const std::uint64_t generation = words_[1].load(std::memory_order_relaxed);
				const std::uint64_t position = words_[2].load(std::memory_order_relaxed);
				const std::uint64_t serial = words_[3].load(std::memory_order_relaxed);
				const std::uint64_t qpc = words_[4].load(std::memory_order_relaxed);
				std::atomic_thread_fence(std::memory_order_acquire);
				if (sequence_.load(std::memory_order_relaxed) != before) continue;
				out.active = (flags & 1ULL) != 0;
				out.moving = (flags & 2ULL) != 0;
				out.page = static_cast<std::uint32_t>(flags >> 32);
				out.generation = generation;
				out.x = std::bit_cast<float>(static_cast<std::uint32_t>(position));
				out.y = std::bit_cast<float>(static_cast<std::uint32_t>(position >> 32));
				out.serial = serial;
				out.qpc = static_cast<std::int64_t>(qpc);
				return ReadResult::Ok;
			}
			return ReadResult::Contended;
		}

	private:
		std::atomic<std::uint64_t> sequence_{ 0 };
		std::array<std::atomic<std::uint64_t>, 5> words_{};
	};

	// 样本是否还能画在“当前白板”上：必须来自白板工作区、坐标有限，且不早于已知页快照的代次。
	// generation 在每次进入白板/翻页单调递增，因此更老的样本只可能来自上一段会话或上一页，直接忽略。
	[[nodiscard]] inline bool SampleUsable(const Sample& sample, std::uint64_t knownGeneration) noexcept
	{
		return sample.active && std::isfinite(sample.x) && std::isfinite(sample.y)
			&& sample.generation >= knownGeneration;
	}

	// 周期 step 上的最短环形距离，用于判断相位是否真的移动（23.99 -> 0.01 只是 0.02）。
	[[nodiscard]] inline double CircularDistance(double a, double b, double step) noexcept
	{
		if (!(step > 0.0)) return 0.0;
		double d = std::fabs(a - b);
		d = std::fmod(d, step);
		return (std::min)(d, step - d);
	}

	struct Phase
	{
		double xPx = 0.0, yPx = 0.0;     // 屏幕像素下首个格点相对 0 的偏移，范围 [0, 24*scale)
		double xDip = 0.0, yDip = 0.0;   // 同一相位换成 DIP（Freeze 在 DIP 空间绘制）
		friend bool operator==(const Phase&, const Phase&) = default;
	};

	// phase = ((W/2 - viewport.x) mod step, (H/2 - viewport.y) mod step)，step = 24 DIP * scale，用 double fmod。
	// viewport 为 (0,0) 时与旧 Freeze 的 DrawThemePattern（以屏幕中心为相位）逐位相同。
	[[nodiscard]] inline Phase ResolvePhase(double widthPx, double heightPx, float dpiScale,
		double viewportX, double viewportY) noexcept
	{
		Phase phase;
		if (!(dpiScale > 0.0F) || !std::isfinite(dpiScale)) return phase;
		const double scale = dpiScale;
		const double step = Inkeys::Drawing::Draw3::kWhiteboardPatternStepDip * scale;
		phase.xPx = Inkeys::Drawing::Draw3::WhiteboardPatternPhase(widthPx * 0.5, viewportX, step);
		phase.yPx = Inkeys::Drawing::Draw3::WhiteboardPatternPhase(heightPx * 0.5, viewportY, step);
		phase.xDip = phase.xPx / scale;
		phase.yDip = phase.yPx / scale;
		return phase;
	}

	// 只由 UI3 渲染线程读写：上一帧实际画出的相位与样本身份。
	struct State
	{
		bool drawnValid = false;
		Phase drawn;
		std::uint64_t drawnSerial = 0;
	};

	struct Plan
	{
		bool usable = false; // 最新样本可用于相位
		bool draw = false;   // 需要为相位变化重画一帧
		Phase phase;         // 本次（若重画）应使用的相位；样本不可用时保持上一帧
	};

	// 纯规划：不改状态。最新者胜出（调用方只读最新样本）；运动期亚阈值位移合并，落定帧精确重画。
	[[nodiscard]] inline Plan PlanFollow(const Sample& sample, std::uint64_t knownGeneration,
		const State& state, double widthPx, double heightPx, float dpiScale, bool patternEnabled) noexcept
	{
		Plan plan;
		plan.phase = state.drawnValid ? state.drawn : ResolvePhase(widthPx, heightPx, dpiScale, 0.0, 0.0);
		const bool scaleValid = dpiScale >= Inkeys::Drawing::Draw3::kWhiteboardPatternMinimumDpiScale
			&& dpiScale <= Inkeys::Drawing::Draw3::kWhiteboardPatternMaximumDpiScale;
		if (!SampleUsable(sample, knownGeneration) || !scaleValid) return plan;
		plan.usable = true;
		plan.phase = ResolvePhase(widthPx, heightPx, dpiScale, sample.x, sample.y);
		if (!patternEnabled) return plan;
		if (!state.drawnValid) { plan.draw = true; return plan; }
		if (sample.moving)
		{
			const double step = Inkeys::Drawing::Draw3::kWhiteboardPatternStepDip * dpiScale;
			plan.draw = (std::max)(CircularDistance(plan.phase.xPx, state.drawn.xPx, step),
				CircularDistance(plan.phase.yPx, state.drawn.yPx, step)) >= kMovingEpsilonPx;
		}
		else plan.draw = plan.phase.xPx != state.drawn.xPx || plan.phase.yPx != state.drawn.yPx;
		return plan;
	}

	// 写端唤醒门控：Blank 主题、白板未激活或离开白板的样本不唤醒 Freeze（平移期对 Freeze 零开销）。
	[[nodiscard]] inline bool ShouldWakeFreeze(int patternKind, bool moduleActive, bool sampleActive) noexcept
	{
		return patternKind != 0 && moduleActive && sampleActive;
	}

	// 主题变化是否退休 Freeze 已呈现的像素（内容失效，取消在途候选）：归属 Freeze 时底色或纹理类型任一变化都要；
	// 归属 Draw3 时 Freeze 只画纯色，仅底色变化。视口相位变化从不经过这里（它只是 scene damage）。
	[[nodiscard]] constexpr bool ThemeChangeRetiresPixels(std::uint32_t oldRgb, int oldPattern,
		std::uint32_t newRgb, int newPattern, bool freezeOwnsPattern) noexcept
	{
		return oldRgb != newRgb || (freezeOwnsPattern && oldPattern != newPattern);
	}

	// M1/M2 的聚合器：固定 512 样本，不分配、不格式化；仅在显式诊断下由 Whiteboard.cpp 驱动。
	class Profile
	{
	public:
		static constexpr std::size_t kCapacity = 512;
		struct Summary
		{
			std::size_t updates = 0, dropped = 0;
			double segmentMs = 0.0, updatesPerSecond = 0.0;
			double gapP50 = 0.0, gapP95 = 0.0, gapMax = 0.0;       // 相邻更新间隔 ms
			double lagFramesP50 = 0.0, lagFramesP95 = 0.0, lagFramesMax = 0.0;
			double lagMsP50 = 0.0, lagMsP95 = 0.0, lagMsMax = 0.0;
			double lagPxP50 = 0.0, lagPxP95 = 0.0, lagPxMax = 0.0;
			double costMsP50 = 0.0, costMsP95 = 0.0, costMsMax = 0.0; // Draw+GetDC+ULW 回调侧耗时
			double ackLagFramesP50 = 0.0, ackLagFramesP95 = 0.0, ackLagFramesMax = 0.0; // 回执确认时再取一次
			std::size_t ackSamples = 0;
			double settleMs = -1.0; // 最后一次 moving=false 发布到对应帧 ULW 完成；<0 表示片段内没有落定帧
		};

		void Reset() noexcept { count_ = dropped_ = ackCount_ = 0; havePrevious_ = false; settleMs_ = -1.0; }
		[[nodiscard]] bool Empty() const noexcept { return count_ == 0; }

		void Record(double nowMs, double lagFrames, double lagMs, double lagPx, double costMs) noexcept
		{
			if (count_ == 0) startMs_ = nowMs;
			if (count_ < kCapacity)
			{
				gap_[count_] = havePrevious_ ? nowMs - previousMs_ : 0.0;
				lagFrames_[count_] = lagFrames;
				lagMs_[count_] = lagMs;
				lagPx_[count_] = lagPx;
				cost_[count_] = costMs;
				++count_;
			}
			else ++dropped_;
			previousMs_ = nowMs;
			havePrevious_ = true;
		}
		void RecordAck(double lagFrames) noexcept
		{
			if (ackCount_ < kCapacity) ackFrames_[ackCount_++] = lagFrames;
		}
		void RecordSettle(double settleMs) noexcept { settleMs_ = settleMs; }

		[[nodiscard]] Summary Summarize() const noexcept
		{
			Summary summary;
			summary.updates = count_;
			summary.dropped = dropped_;
			summary.settleMs = settleMs_;
			summary.ackSamples = ackCount_;
			if (!count_) return summary;
			summary.segmentMs = previousMs_ - startMs_;
			summary.updatesPerSecond = summary.segmentMs > 0.0
				? static_cast<double>(count_ + dropped_ - 1) * 1000.0 / summary.segmentMs : 0.0;
			Percentiles(gap_, count_, 1, summary.gapP50, summary.gapP95, summary.gapMax);
			Percentiles(lagFrames_, count_, 0, summary.lagFramesP50, summary.lagFramesP95, summary.lagFramesMax);
			Percentiles(lagMs_, count_, 0, summary.lagMsP50, summary.lagMsP95, summary.lagMsMax);
			Percentiles(lagPx_, count_, 0, summary.lagPxP50, summary.lagPxP95, summary.lagPxMax);
			Percentiles(cost_, count_, 0, summary.costMsP50, summary.costMsP95, summary.costMsMax);
			Percentiles(ackFrames_, ackCount_, 0, summary.ackLagFramesP50, summary.ackLagFramesP95, summary.ackLagFramesMax);
			return summary;
		}

	private:
		static void Percentiles(const std::array<double, kCapacity>& source, std::size_t count, std::size_t skip,
			double& p50, double& p95, double& maximum) noexcept
		{
			if (count <= skip) return;
			std::array<double, kCapacity> values = source;
			const std::size_t size = count - skip;
			std::sort(values.begin() + skip, values.begin() + skip + size);
			const double* sorted = values.data() + skip;
			p50 = sorted[(size - 1) / 2];
			p95 = sorted[static_cast<std::size_t>(static_cast<double>(size - 1) * 0.95)];
			maximum = sorted[size - 1];
		}

		std::array<double, kCapacity> gap_{}, lagFrames_{}, lagMs_{}, lagPx_{}, cost_{}, ackFrames_{};
		std::size_t count_ = 0, dropped_ = 0, ackCount_ = 0;
		double startMs_ = 0.0, previousMs_ = 0.0, settleMs_ = -1.0;
		bool havePrevious_ = false;
	};
}
