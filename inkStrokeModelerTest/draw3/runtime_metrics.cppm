module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <windows.h>

export module draw3.runtime_metrics;

import draw3.contact_input;

namespace draw3
{
	struct RuntimeMetricsSessionImpl;
}

export namespace draw3
{
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
