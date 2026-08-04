module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <windows.h>

export module draw3.runtime_metrics;

import draw3.contact_input;

namespace draw3
{
	struct RuntimeMetricsSessionImpl;
	struct PerformanceHudTrackerImpl;
}

export namespace draw3
{
	struct PerformanceHudSnapshot
	{
		size_t frameSampleCount = 0;
		double averageFps = 0.0;
		double onePercentLowFps = 0.0;
		double averageFrameMs = 0.0;
		double p99FrameMs = 0.0;
		double frameJitterMs = 0.0;
		double averageWorkMs = 0.0;
		double estimatedUnlimitedFps = 0.0;
		double averagePresentMs = 0.0;
		double processCpuPercent = 0.0;
		double workingSetMiB = 0.0;
	};

	// 保存 HUD 刷新所需的最新 contact 数据，不进入帧统计数组。
	struct PerformanceHudContact
	{
		uint32_t contactId = 0;
		InputDeviceType deviceType = InputDeviceType::Touch;
		uint32_t drawingTool = 0;
		float colorRed = 0.0f;
		float colorGreen = 0.0f;
		float colorBlue = 0.0f;
		float colorAlpha = 0.0f;
		float strokeWidth = 0.0f;
		float x = 0.0f;
		float y = 0.0f;
		float pressure = -1.0f;
		float speed = -1.0f;
		float contactWidth = -1.0f;
		float contactHeight = -1.0f;
		float altitude = -1.0f;
		float rotation = -1.0f;
	};

	// 使用固定容量样本统计最近一秒的绘制性能；tracker 普通帧只写固定数组。
	class PerformanceHudTracker
	{
	public:
		static constexpr size_t kSampleCapacity = 1024;

		PerformanceHudTracker();
		~PerformanceHudTracker();
		PerformanceHudTracker(const PerformanceHudTracker&) = delete;
		PerformanceHudTracker& operator=(const PerformanceHudTracker&) = delete;

		// 只由物理 contact 绘制帧调用；统计窗闭合并更新平均 FPS 时返回 true。
		bool RecordDrawingFrame(double frameStartMs, double workMs,
			double presentMs, bool presented) noexcept;
		// 一笔物理绘制结束时丢弃未闭合统计窗，避免把空闲时间算成帧间隔。
		void EndDrawingFrameSequence() noexcept;
		void Reset() noexcept;
		const PerformanceHudSnapshot& Snapshot() const noexcept;
		std::wstring FormatText(double gpuMemoryMiB,
			std::span<const PerformanceHudContact> contacts = {}) const;

	private:
		std::unique_ptr<PerformanceHudTrackerImpl> impl_;
	};

	// Release 可选运行指标；未构造会话时绘制热路径不分配、不写文件。
	class RuntimeMetricsSession
	{
	public:
		explicit RuntimeMetricsSession(size_t maximumSamples = 32768);
		~RuntimeMetricsSession();
		RuntimeMetricsSession(const RuntimeMetricsSession&) = delete;
		RuntimeMetricsSession& operator=(const RuntimeMetricsSession&) = delete;

		// 每帧开始时清除尚未提交的 landing 候选。
		void BeginFrame() noexcept;
		// 暂存本帧已经具备可见几何的 contact。
		void StageLanding(ContactRecord* record, uint64_t generation,
			InputDeviceType deviceType, uint32_t tool, int64_t eligibleQpc);
		// 只在成功 Present 后把本帧候选转换为软件落笔延迟样本。
		void CommitStagedLandings(bool presentSucceeded, int64_t presentQpc);
		// 记录活动帧间隔、工作和 Present 耗时。
		void RecordActiveFrame(double frameStartMs, double workMs,
			double presentMs, bool presented);
		// 物理接触结束后切断连续帧区间，避免把粒子动画期间的间隔计入书写帧率。
		void EndActiveFrameSequence() noexcept;
		// 记录一次实际 Present。
		void RecordPresent(double presentMs) noexcept;
		// 标记完全空闲阻塞区间，用于证明 frame/Present 计数不增长。
		void BeginIdle(double nowMs) noexcept;
		void EndIdle(double nowMs) noexcept;

		// 写出 JSON 原始报告与摘要；目录必须由调用者预先创建。
		bool WriteJson(const wchar_t* outputPath,
			const ContactInputDiagnosticsSnapshot& inputDiagnostics) const;
		// 按 200 次 landing、p99、长帧和 5 秒 idle 门槛执行严格验收。
		bool MeetsStrictThresholds() const;

	private:
		std::unique_ptr<RuntimeMetricsSessionImpl> impl_;
	};
}
