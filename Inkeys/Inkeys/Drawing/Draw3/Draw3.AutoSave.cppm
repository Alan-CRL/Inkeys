module;

#include "Draw3.Bridge.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

export module Inkeys.Drawing.Draw3.auto_save;

export import draw3.uink_draw3_export;

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
		PptTouched,
	};

	class DesktopAutoSavePolicy
	{
	public:
		void ObserveWorkspace(Bridge::Workspace workspace) noexcept;
		void CompleteDesktopClear() noexcept;
		[[nodiscard]] bool ShouldCapture(Bridge::Workspace workspace,
			bool enabled, bool hasVisibleContent) const noexcept;
		[[nodiscard]] DesktopAutoSaveEligibility Eligibility() const noexcept;

	private:
		DesktopAutoSaveEligibility eligibility_ = DesktopAutoSaveEligibility::Eligible;
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
	};

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
		bool Start(std::wstring autoSaveRoot);
		DesktopAutoSaveSubmitStatus Submit(DesktopAutoSaveTrigger trigger,
			draw3::uink::Draw3UInkExportSnapshot snapshot) noexcept;
		DesktopAutoSaveSubmitStatus SubmitPrepared(
			DesktopAutoSaveRequest request) noexcept;
		// 关闭生产端并无超时排空所有已接受请求。
		void CloseAndDrain() noexcept;
		[[nodiscard]] DesktopAutoSaveDiagnostics Diagnostics() const noexcept;
		[[nodiscard]] std::string SessionId() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
