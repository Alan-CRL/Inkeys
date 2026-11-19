#pragma once

#include "Draw3.DevicePerformanceModel.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Inkeys::Drawing::Draw3
{
	// 设备性能账本：由 Host 持有，先于绘制线程启动、在绘制线程 join 之后停止并落盘。
	// - 绘制线程只在进入空闲等待前调用 Submit（短锁合并，不做 I/O）；
	// - 设置页每帧调用 Snapshot（版本未变不复制）；
	// - 自带写盘线程：启动时加载文件；只在绘制空闲时写盘（两次至少间隔 30 s），Stop 时立即写盘。
	// 帧内只做累加，绝不写文件；关闭记录时帧内不累加、不移交，也就不会写盘。
	class DevicePerformanceLedger
	{
	public:
		DevicePerformanceLedger() = default;
		~DevicePerformanceLedger();
		DevicePerformanceLedger(const DevicePerformanceLedger&) = delete;
		DevicePerformanceLedger& operator=(const DevicePerformanceLedger&) = delete;

		// 空路径 = 只在内存记录（测试用）。重复 Start 忽略。
		void Start(std::wstring filePath) noexcept;
		void Stop() noexcept;

		// 绘制线程：当前生效的设置签名（启动、呈现回退后更新）。报告优先显示该签名。
		void SetCurrentSignature(const DevicePerformance::Signature& signature) noexcept;
		// 不检查清除代次（测试与不跨清除的调用方）。
		static constexpr std::uint64_t kAnyEpoch = (std::numeric_limits<std::uint64_t>::max)();
		// 绘制线程：一段累加开始（本段第一个计数帧）时调用，写盘线程在绘制期间不落盘。
		// 返回当前清除代次，移交时带回：段内用户点了“清除数据”，这一段就整段作废。只有两次原子操作。
		std::uint64_t MarkBusy() noexcept
		{
			busy_.store(true, std::memory_order_relaxed);
			return clearEpochPublished_.load(std::memory_order_relaxed);
		}
		// 绘制线程：空闲移交一段累加结果（同时清除忙碌标志）。锁内复查记录开关与清除代次。任何异常都只丢弃本段。
		void Submit(const DevicePerformance::Signature& signature, const DevicePerformance::Tally& tally,
			std::uint64_t segmentEpoch = kAnyEpoch) noexcept;
		void SetRecordingEnabled(bool enabled) noexcept;
		bool RecordingEnabled() const noexcept { return recordingEnabled_.load(std::memory_order_relaxed); }
		// 清空内存数据并删除文件（写盘线程执行删除）。
		void Clear() noexcept;

		// 版本与 knownRevision 相同返回 false 且不复制；否则复制最新报告并返回 true。
		bool Snapshot(std::uint64_t knownRevision, DevicePerformance::Report& out) const;

		// 测试/诊断：当前内存数据的副本。
		DevicePerformance::Store StoreCopy() const;
		// 测试：等待启动加载完成（毫秒，<0 无限）。
		bool WaitLoaded(int timeoutMs) const;

	private:
		void RebuildReportLocked();
		void MarkDirtyLocked();
		void WriterMain();

		mutable std::mutex mutex_;
		mutable std::condition_variable wake_;
		DevicePerformance::Store store_;
		std::optional<DevicePerformance::Signature> currentSignature_;
		DevicePerformance::Report report_;
		std::uint64_t revision_ = 1;
		bool dirty_ = false;
		std::int64_t lastWriteMs_ = (std::numeric_limits<std::int64_t>::min)() / 2; // 首次空闲立即可写
		bool deleteFilePending_ = false;
		std::uint64_t clearEpoch_ = 0;
		std::uint64_t loadEpoch_ = 0; // Start 时固定的清除代次，加载结果只在代次未变时合并
		bool loaded_ = false;
		bool loadRequired_ = true;
		bool loadedOnce_ = false;      // 已成功加载过 loadedPath_，再次 Start 不重读
		std::wstring loadedPath_;
		bool writeBlocked_ = false;    // 加载遇到 I/O 错误：本会话不写盘，避免覆盖历史
		bool stopRequested_ = false;
		bool started_ = false;
		bool writerActive_ = false; // 写盘线程仍在处理请求（退出前在锁内清除）
		std::wstring filePath_;
		std::thread writer_;
		std::atomic<bool> recordingEnabled_{ true };
		std::atomic<bool> busy_{ false };
		std::atomic<std::uint64_t> clearEpochPublished_{ 0 }; // clearEpoch_ 的无锁副本，供绘制线程在段开始时读取
	};
}
