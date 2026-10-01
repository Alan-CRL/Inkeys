module;

#include "Draw3.Bridge.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

export module Inkeys.Drawing.Draw3.auto_save;

export import draw3.uink_draw3_import;

export namespace Inkeys::Drawing::Draw3
{
	enum class DesktopAutoSaveTrigger : std::uint8_t
	{
		Clear,
		Exit,
	};

	enum class DesktopAutoSaveEligibility : std::uint8_t
	{
		Eligible,
	};

	class DesktopAutoSavePolicy
	{
	public:
		void CompleteDesktopClear() noexcept;
		[[nodiscard]] bool ShouldCapture(Bridge::Workspace workspace,
			bool enabled, bool hasVisibleContent) const noexcept;
		[[nodiscard]] DesktopAutoSaveEligibility Eligibility() const noexcept;
	};

	struct DesktopAutoSaveTimestamp
	{
		std::string localDate;
		std::string localTimeForFile;
		std::string createdAt;
	};

	struct DesktopAutoSaveRequest
	{
		std::string saveRequestId;
		std::string sessionId;
		std::uint64_t sequenceInSession = 0;
		DesktopAutoSaveTrigger trigger = DesktopAutoSaveTrigger::Clear;
		DesktopAutoSaveTimestamp timestamp;
		std::wstring proposedFileName;
		draw3::uink::Draw3UInkExportSnapshot snapshot;
		std::uint64_t estimatedSnapshotBytes = 0;
	};

	enum class DesktopAutoSaveSubmitStatus : std::uint8_t
	{
		Accepted,
		Existing,
		Closed,
		Invalid,
	};

	enum class DesktopPersistenceOperation : std::uint8_t
	{
		Save,
		Load,
	};

	enum class DesktopPersistenceStatus : std::uint8_t
	{
		Committed,
		Loaded,
		NotFound,
		Invalid,
		IoError,
	};

	struct DesktopPersistenceCompletion
	{
		DesktopPersistenceOperation operation = DesktopPersistenceOperation::Save;
		DesktopPersistenceStatus status = DesktopPersistenceStatus::Invalid;
		DesktopAutoSaveTrigger trigger = DesktopAutoSaveTrigger::Clear;
		draw3::uink::UInkGuid fileGuid;
		std::shared_ptr<const draw3::uink::Draw3UInkExportSnapshot> loadedSnapshot;
	};

	struct DesktopAutoSaveDiagnostics
	{
		std::uint64_t accepted = 0;
		std::uint64_t committed = 0;
		std::uint64_t failed = 0;
		std::uint64_t duplicate = 0;
		std::uint64_t pending = 0;
		std::uint64_t queuedSnapshotBytes = 0;
	};

	struct DesktopAutoSaveTestFaultInjection
	{
		std::uint32_t writeDelayMilliseconds = 0;
		bool failUInkWrite = false;
		bool failIndexCommit = false;
		// 仅隔离 fixture 到达证据；借用到 worker join/死亡，零 delay 不触发。
		void* enteringWriteDelayEvent = nullptr;
		// 只供验真fixture定位真实索引阶段；仅SetEvent，不改变保存结果/等待。
		void* enteringIndexMutexEvent = nullptr;
		void* indexMutexAcquiredEvent = nullptr;
		void* indexReadCompletedEvent = nullptr;
		// 只供授权 fixture 的私有日志定位真实 I/O 失败；普通默认不输出。
		bool logIndexCommitDiagnostics = false;
	};

	// 仅显式隔离 CLI 的严格磁盘读者，不开放普通业务冷恢复或注入 records。
	struct DesktopAutoSaveFixtureReadReceipt
	{
		DesktopPersistenceStatus status = DesktopPersistenceStatus::Invalid;
		std::string localDate, storageSession;
		std::uint64_t sequenceInSession = 0, dailySequence = 0;
		draw3::uink::UInkGuid fileGuid;
		std::wstring relativePath;
		DesktopAutoSaveTrigger trigger = DesktopAutoSaveTrigger::Clear;
		std::shared_ptr<const draw3::uink::Draw3UInkExportSnapshot> loadedSnapshot;
		std::optional<draw3::uink::UInkSourceRevision> sourceRevision;
		std::string indexBytes;
	};
	DesktopAutoSaveFixtureReadReceipt ReadLastCommittedDesktopAutoSaveFixture(
		const std::wstring& ownedRoot, const std::string& localDate) noexcept;

	DesktopAutoSaveTimestamp CaptureDesktopAutoSaveTimestamp() noexcept;
	std::wstring BuildDesktopAutoSaveFileName(
		const DesktopAutoSaveTimestamp& timestamp,
		const std::string& saveRequestId, std::uint32_t collisionSuffix = 0);
	std::uint64_t EstimateDesktopAutoSaveSnapshotBytes(
		const draw3::uink::Draw3UInkExportSnapshot& snapshot) noexcept;
	bool IsValidDesktopAutoSaveRequest(const DesktopAutoSaveRequest& request) noexcept;

	// 仅供无窗口事务测试注入稳定故障；生产调用必须保持默认值。
	void SetDesktopAutoSaveTestFaultInjection(
		const DesktopAutoSaveTestFaultInjection& injection) noexcept;
	void ResetDesktopAutoSaveTestFaultInjection() noexcept;

	class DesktopAutoSaveService
	{
	public:
		DesktopAutoSaveService();
		~DesktopAutoSaveService();
		DesktopAutoSaveService(const DesktopAutoSaveService&) = delete;
		DesktopAutoSaveService& operator=(const DesktopAutoSaveService&) = delete;

		// Start 只建立会话和 worker，不创建任何目录或索引。
		bool Start(std::wstring autoSaveRoot, void* wakeContext = nullptr,
			void (*wake)(void*) noexcept = nullptr);
		DesktopAutoSaveSubmitStatus Submit(DesktopAutoSaveTrigger trigger,
			draw3::uink::Draw3UInkExportSnapshot snapshot) noexcept;
		DesktopAutoSaveSubmitStatus SubmitPrepared(
			DesktopAutoSaveRequest request) noexcept;
		DesktopAutoSaveSubmitStatus SubmitLoad(
			draw3::uink::UInkGuid fileGuid) noexcept;
		bool TryTakeCompletion(DesktopPersistenceCompletion& completion) noexcept;
		// 关闭生产端并无超时排空所有已接受请求。
		void CloseAndDrain() noexcept;
		[[nodiscard]] DesktopAutoSaveDiagnostics Diagnostics() const noexcept;
		[[nodiscard]] std::string SessionId() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
