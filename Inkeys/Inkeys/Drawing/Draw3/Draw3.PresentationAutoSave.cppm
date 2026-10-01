module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "Draw3.Bridge.h"

export module Inkeys.Drawing.Draw3.presentation_auto_save;

export import draw3.uink_draw3_import;

export namespace Inkeys::Drawing::Draw3
{
	constexpr std::uint64_t AdvancePresentationMutationRevision(
		std::uint64_t revision) noexcept
	{
		return revision == UINT64_MAX ? 1 : revision + 1;
	}

	constexpr bool ShouldQueuePresentationSave(
		std::uint64_t mutationRevision,
		std::uint64_t queuedRevision) noexcept
	{
		// 是否有可见内容与持久化 dirty 是两件事；清空也必须推进并保存。
		return mutationRevision != 0 && mutationRevision > queuedRevision;
	}

	constexpr bool PresentationClearAdvancesMutation(
		bool clearSucceeded, std::size_t persistentHistoryItemCount) noexcept
	{
		return clearSucceeded && persistentHistoryItemCount != 0;
	}

	constexpr bool ShouldEvictPresentationSlot(bool hasCommittedFile,
		std::uint64_t mutationRevision, std::uint64_t queuedRevision,
		std::uint64_t committedRevision, bool loadPending) noexcept
	{
		return hasCommittedFile && !loadPending && mutationRevision != 0 &&
			mutationRevision == queuedRevision && queuedRevision == committedRevision;
	}

	constexpr bool ShouldReleasePresentationClearFallback(
		std::uint32_t currentIntervalOrdinal,
		std::uint32_t sealedIntervalOrdinal) noexcept
	{
		return sealedIntervalOrdinal != UINT32_MAX &&
			currentIntervalOrdinal == sealedIntervalOrdinal + 1;
	}

	// 实际索引根；逻辑页身份仍由 target.bindingMode 决定。
	enum class PresentationStorageTrack : std::uint8_t
	{
		Unresolved,
		Base,
		PageIndexSidecar,
		SlideIdSidecar,
	};

	struct PresentationSaveRequest
	{
		Bridge::PresentationTarget target;
		std::uint64_t mutationRevision = 0;
		draw3::uink::Draw3UInkExportSnapshot snapshot;
		// Clear 边界不可与普通 tail 保存合并；只封存指定 Canvas。
		std::optional<draw3::uink::UInkGuid> clearPageGuid;
		std::optional<std::uint32_t> clearIntervalOrdinal;
		std::uint64_t slotGeneration = 0; // Controller 不透明槽代次，worker 原样回显。
	};

	enum class PresentationLoadKind : std::uint8_t
	{
		Current,
		PreviousInterval,
	};

	struct PresentationLoadRequest
	{
		Bridge::PresentationTarget target;
		PresentationLoadKind kind = PresentationLoadKind::Current;
		std::optional<draw3::uink::UInkGuid> pageGuid;
		std::uint32_t intervalOrdinal = 0;
		std::uint64_t slotGeneration = 0;
	};

	enum class PresentationPersistenceOperation : std::uint8_t
	{
		Save,
		Load,
	};

	enum class PresentationPersistenceStatus : std::uint8_t
	{
		Committed,
		Loaded,
		NotFound,
		CrossProcessConflictDeferred,
		SourceChanged,
		Invalid,
		IoError,
	};

	struct PresentationPersistenceCompletion
	{
		PresentationPersistenceOperation operation =
			PresentationPersistenceOperation::Save;
		PresentationPersistenceStatus status =
			PresentationPersistenceStatus::Invalid;
		Bridge::PresentationTarget target;
		std::uint64_t mutationRevision = 0;
		std::optional<draw3::uink::UInkGuid> clearPageGuid;
		std::optional<std::uint32_t> clearIntervalOrdinal;
		std::shared_ptr<const draw3::uink::Draw3UInkExportSnapshot> loadedSnapshot;
		PresentationLoadKind loadKind = PresentationLoadKind::Current;
		std::optional<draw3::uink::UInkGuid> pageGuid;
		std::uint32_t intervalOrdinal = 0;
		std::uint64_t slotGeneration = 0;
		PresentationStorageTrack storageTrack = PresentationStorageTrack::Unresolved;
		// Save 回显请求文件；Load 仅在严格核准 Loaded 后给出实际文件。
		std::optional<draw3::uink::UInkGuid> fileGuid;
	};

	enum class PresentationPersistenceSubmitStatus : std::uint8_t
	{
		Accepted,
		ReplacedPending,
		Closed,
		Invalid,
	};

	struct PresentationPersistenceDiagnostics
	{
		std::uint64_t accepted = 0;
		std::uint64_t replacedPending = 0;
		std::uint64_t committed = 0;
		std::uint64_t loaded = 0;
		std::uint64_t failed = 0;
	};

	// 仅供无窗口事务测试注入；生产必须保持默认值。
	struct PresentationAutoSaveTestFaultInjection
	{
		bool failIndexCommit = false;
		std::uint32_t writeDelayMilliseconds = 0;
		std::string sessionIdOverride;
		bool throwWorkerOperation = false;
		// 只供隔离测试停在 UInk 已提交、索引尚未发布的真实 worker 断点。
		void* afterUInkCommittedEvent = nullptr;
		void* continueIndexCommitEvent = nullptr;
		// 只供授权 fixture 记录已返回的保存错误；不改变正常保存结果。
		bool logSaveIoDiagnostics = false;
	};

	void SetPresentationAutoSaveTestFaultInjection(
		const PresentationAutoSaveTestFaultInjection& injection) noexcept;
	void ResetPresentationAutoSaveTestFaultInjection() noexcept;

	class PresentationAutoSaveService
	{
	public:
		PresentationAutoSaveService();
		~PresentationAutoSaveService();
		PresentationAutoSaveService(const PresentationAutoSaveService&) = delete;
		PresentationAutoSaveService& operator=(const PresentationAutoSaveService&) = delete;

		bool Start(std::wstring autoSaveRoot, void* wakeContext = nullptr,
			void (*wake)(void*) noexcept = nullptr);
		PresentationPersistenceSubmitStatus SubmitSave(
			PresentationSaveRequest request) noexcept;
		PresentationPersistenceSubmitStatus SubmitLoad(
			PresentationLoadRequest request) noexcept;
		bool TryTakeCompletion(PresentationPersistenceCompletion& completion) noexcept;
		void CloseAndDrain() noexcept;
		std::string SessionId() const;
		PresentationPersistenceDiagnostics Diagnostics() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
