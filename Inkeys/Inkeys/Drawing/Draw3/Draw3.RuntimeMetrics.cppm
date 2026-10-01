module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <memory>

export module Inkeys.Drawing.Draw3.runtime_metrics;

import Inkeys.Drawing.Draw3.contact_input;

namespace Inkeys::Drawing::Draw3
{
	struct RuntimeMetricsSessionImpl;
}

export namespace Inkeys::Drawing::Draw3
{
	// 只保存本次诊断会话内的匿名身份；真实页、栅格和输出版本由绘制 owner 提供。
	struct RuntimeMetricsCanvasIdentity
	{
		uint64_t workspace = 0;
		uint64_t page = 0;
		uint64_t sceneGeneration = 0;
		uint64_t rasterGeneration = 0;
		uint64_t outputGeneration = 0;
		bool operator==(const RuntimeMetricsCanvasIdentity&) const = default;
	};

	enum class RuntimeMetricsProofKind : uint8_t { Live, Stored, Laser };
	// 固定原因位只取真实 owner 行为；NoPresent 与实际 Present 结果互斥。
	enum class RuntimeMetricsFrameReason : uint32_t
	{
		PhysicalBefore = 1u << 0, PhysicalAfter = 1u << 1, Terminal = 1u << 2,
		Command = 1u << 3, Page = 1u << 4, Resize = 1u << 5, Recovery = 1u << 6,
		Hover = 1u << 7, LaserActive = 1u << 8, LaserBake = 1u << 9,
		LaserHold = 1u << 10, LaserFade = 1u << 11, LaserExpiry = 1u << 12,
		LaserParticles = 1u << 13, NoPresent = 1u << 14, RasterFailed = 1u << 15,
		PresentFailed = 1u << 16, PresentSucceeded = 1u << 17, OutputMismatch = 1u << 18,
		AuthoritativeWithheld = 1u << 19, PartialReplay = 1u << 20
	};
	struct RuntimeMetricsLandingProof
	{
		RuntimeMetricsCanvasIdentity canvas;
		uint64_t contentToken = 0;
		uint64_t itemToken = 0;
		uint64_t consumedSequence = 0;
		RuntimeMetricsProofKind kind = RuntimeMetricsProofKind::Live;
		uint64_t frameSerial = 0;
		bool operator==(const RuntimeMetricsLandingProof&) const = default;
	};

	// 每次真实 render attempt 的数值载荷；终态和恢复帧不依赖仍有按住的 contact。
	struct RuntimeMetricsFrameSample
	{
		uint64_t frameSerial = 0;
		double frameStartMs = 0.0;
		double wallMs = 0.0;
		double presentWallMs = 0.0;
		uint32_t reasonFlags = 0;
		uint32_t physicalBefore = 0;
		uint32_t physicalAfter = 0;
		uint32_t terminalCount = 0;
		bool presentAttempted = false;
		bool presentSucceeded = false;
	};
	struct RuntimeMetricsSnapshot
	{
		uint64_t contactSeen = 0;
		uint64_t contactRetained = 0;
		uint64_t contactDropped = 0;
		uint64_t pending = 0;
		uint64_t confirmed = 0;
		uint64_t invalid = 0;
		uint64_t unpresented = 0;
		uint64_t presentAttempts = 0;
		uint64_t presentSucceeded = 0;
		uint64_t presentFailed = 0;
		uint64_t requestedSamples = 0;
		uint64_t effectiveSamples = 0;
		uint64_t allocatedBytes = 0;
		uint64_t frameSerial = 0;
		uint64_t pendingOverflow = 0;
		uint64_t legacyUnverified = 0;
		uint64_t framesSeen = 0;
		uint64_t framesRetained = 0;
		uint64_t framesDropped = 0;
		uint64_t framesInvalid = 0;
	};

	// 唯一绘制 owner 的可选指标会话；构造预分配 <=32MiB，结束后才离线导出。
	class RuntimeMetricsSession
	{
	public:
		explicit RuntimeMetricsSession(size_t maximumSamples = 32768);
		~RuntimeMetricsSession();
		RuntimeMetricsSession(const RuntimeMetricsSession&) = delete;
		RuntimeMetricsSession& operator=(const RuntimeMetricsSession&) = delete;

		// 推进 Session loop serial；失败 Stored pending 保留到同内容权威帧或明确失效。
		void BeginFrame() noexcept;
		// 旧无 proof 接口只记 legacy/unverified，不产生正式 landing。
		void StageLanding(ContactRecord* record, uint64_t generation,
			InputDeviceType deviceType, uint32_t tool, int64_t eligibleQpc);
		// 旧接口仅保留调用兼容，不把无 proof 的结果升级为正式成功。
		void CommitStagedLandings(bool presentSucceeded, int64_t presentQpc);
		// 记录活动帧间隔、工作和 Present 耗时。
		void RecordActiveFrame(double frameStartMs, double workMs,
			double presentMs, bool presented);
		// 物理接触结束后切断连续帧区间，避免把粒子动画期间的间隔计入书写帧率。
		void EndActiveFrameSequence() noexcept;
		// 旧接口缺成功结果，仅记实际尝试及 Unknown outcome。
		void RecordPresent(double presentMs) noexcept;
		// 新合同只由 opt-in 的 owner 在真实准入、栅格与 Present 边界调用。
		bool RegisterContact(ContactRecord* record, uint64_t generation,
			InputDeviceType deviceType, uint32_t tool, int64_t eligibleQpc) noexcept;
		bool StageVerifiedLanding(ContactRecord* record, uint64_t generation,
			const RuntimeMetricsLandingProof& proof) noexcept;
		void CommitVerifiedLandings(bool presentSucceeded, int64_t presentReturnQpc,
			const RuntimeMetricsLandingProof& presentedProof) noexcept;
		// 精确终结一个 opaque 键；不删除去重键，不影响其他 pending 或本帧成功资格。
		bool InvalidateContact(ContactRecord* opaqueRecord, uint64_t generation) noexcept;
		void InvalidatePending() noexcept;
		void RecordVerifiedPresent(double presentMs, bool succeeded) noexcept;
		void RecordRenderFrame(const RuntimeMetricsFrameSample& sample) noexcept;
		RuntimeMetricsSnapshot Snapshot() const noexcept;
		// 标记完全空闲阻塞区间，用于证明 frame/Present 计数不增长。
		void BeginIdle(double nowMs) noexcept;
		void EndIdle(double nowMs) noexcept;

		// owner 停止后写 schema2；调用者先校验并创建隔离目录，create-new 拒绝覆盖。
		bool WriteJson(const wchar_t* outputPath,
			const ContactInputDiagnosticsSnapshot& inputDiagnostics) const;
		// 旧混合 200 landing/尾延迟/idle 门，仅供 legacy 分析，不作为首发性能判决。
		bool MeetsStrictThresholds() const;

	private:
		std::unique_ptr<RuntimeMetricsSessionImpl> impl_;
	};
}
