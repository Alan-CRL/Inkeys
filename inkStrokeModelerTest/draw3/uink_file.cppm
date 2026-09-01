module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module draw3.uink_file;

export import draw3.uink_codec;

export namespace draw3::uink
{
	struct UInkFileAccessOptions
	{
		uint32_t transientRetryCount = 3;
		uint32_t transientRetryDelayMs = 100;
	};

	UInkReadResult ReadUInkFile(const std::wstring& path,
		const UInkReadLimits& limits = {},
		const UInkFileAccessOptions& access = {});

	enum class UInkEditingSource : uint8_t
	{
		ExternalImport,
		ApplicationOwned
	};

	std::optional<UInkEditingSession> CreateUInkEditingSession(
		const UInkReadResult& readResult,
		UInkEditingSource source = UInkEditingSource::ExternalImport);

	enum class UInkSaveMode : uint8_t
	{
		SaveExistingLogicalFile,
		SaveAsNewLogicalFile,
		CreateNewLogicalFileWithIdentity,
		NormalizeImportedWithExplicitLoss
	};

	struct UInkSaveOptions
	{
		UInkSaveMode mode = UInkSaveMode::SaveExistingLogicalFile;
		UInkFileAccessOptions access;
	};

	enum class UInkSaveStatus : uint8_t
	{
		Committed,
		InvalidSession,
		InvalidModel,
		SourceChanged,
		ResourcePackUnsupported,
		IoError,
		SelfValidationFailed,
		PartialCommitRequiresRecovery
	};

	struct UInkSaveResult
	{
		UInkSaveStatus status = UInkSaveStatus::InvalidSession;
		std::optional<UInkSourceRevision> revision;
		std::vector<UInkDiagnostic> diagnostics;
		std::wstring recoveryPath;
		uint32_t systemError = 0;
	};

	UInkSaveResult SaveUInkFile(const std::wstring& path,
		const UInkEditingSession& session,
		const UInkSaveOptions& options = {});

	struct UInkAppendBatch
	{
		std::vector<UInkAppendObject> objects;
	};

	enum class UInkAppendPlanStatus : uint8_t
	{
		Ready,
		InvalidSource,
		InvalidBatch,
		RequiresFullSave,
		ResourcePackUnsupported
	};

	struct UInkAppendPlan
	{
		UInkAppendPlanStatus status = UInkAppendPlanStatus::InvalidSource;
		UInkSourceRevision expectedRevision;
		std::wstring sourcePath;
		uint64_t truncateOffset = 0;
		std::vector<std::byte> bytes;
		// 摘要同时绑定 truncateOffset 与 batch 字节，防止可变 plan 被误改后执行。
		std::array<uint8_t, 32> bytesSha256 = {};
		bool sourceContainsMedia = false;
		std::optional<uint32_t> firstContentId;
		std::optional<uint32_t> lastContentId;
		std::optional<uint32_t> lastUndoId;
		std::vector<UInkDiagnostic> diagnostics;
	};

	UInkAppendPlan AnalyzeUInkAppend(const UInkReadResult& source,
		const UInkAppendBatch& batch);

	enum class UInkAppendDurability : uint8_t
	{
		Durable,
		Buffered
	};

	struct UInkAppendOptions
	{
		UInkAppendDurability durability = UInkAppendDurability::Durable;
		UInkFileAccessOptions access;
	};

	// 仅供无窗口事务测试注入稳定故障；生产调用必须保持默认值。
	struct UInkFileTestFaultInjection
	{
		uint64_t failWriteAfterBytes = UINT64_MAX;
		bool failFlush = false;
		bool failSelfValidation = false;
		bool failCommit = false;
		bool failRollback = false;
		bool failCommittedRevisionValidation = false;
	};

	void SetUInkFileTestFaultInjection(const UInkFileTestFaultInjection& faults) noexcept;
	void ResetUInkFileTestFaultInjection() noexcept;

	enum class UInkAppendStatus : uint8_t
	{
		CommittedDurable,
		CommittedBuffered,
		InvalidPlan,
		SourceChanged,
		ResourcePackUnsupported,
		IoError,
		WriteFailedRolledBack,
		TailRepairedNoAppend,
		WrittenNotDurable,
		PartialCommitRequiresRecovery
	};

	struct UInkAppendResult
	{
		UInkAppendStatus status = UInkAppendStatus::InvalidPlan;
		uint64_t lastTrustedBoundary = 0;
		uint64_t observedLength = 0;
		std::optional<UInkSourceRevision> revision;
		std::vector<UInkDiagnostic> diagnostics;
		uint32_t systemError = 0;
	};

	UInkAppendResult ExecuteUInkAppend(const std::wstring& path,
		const UInkAppendPlan& plan,
		const UInkAppendOptions& options = {});
}
