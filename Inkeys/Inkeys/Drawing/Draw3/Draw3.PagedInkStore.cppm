module;

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Inkeys.Drawing.Draw3.paged_ink_store;

export import draw3.uink_draw3_v11;

export namespace Inkeys::Drawing::Draw3
{
	enum class PagedInkFileKind : uint8_t { Whiteboard, Presentation };

	struct PagedInkFileNaming
	{
		PagedInkFileKind kind = PagedInkFileKind::Whiteboard;
		std::wstring presentationName;
		draw3::uink::UInkGuid presentationKey;
	};

	struct PagedInkLocalTime
	{
		uint16_t year, month, day, hour, minute, second, millisecond;
	};

	// 返回建议路径，发布时的同名冲突由 UInkPagedFileWriter 处理。
	std::wstring MakePagedInkPath(const std::wstring& root, const PagedInkFileNaming& naming,
		const PagedInkLocalTime& time, const draw3::uink::UInkGuid& fileGuid);

	enum class PagedInkRequestKind : uint8_t { Clear, Full, Load, LoadPreviousInterval, LoadLane };
	enum class PagedInkAdmission : uint8_t { Accepted, Closed, FileFailed, InvalidRequest, ReservationFailed };
	enum class PagedInkResult : uint8_t { Succeeded, Failed, Merged, Canceled, FileFailed };
	enum class PagedInkError : uint8_t
	{
		None, PendingWrite, BoundaryMismatch, InvalidSnapshot, MissingPage, ExportFailed,
		SpliceFailed, ReadFailed, ImportFailed, FileCreateFailed, FileWriteFailed, Exception
	};
	enum class PagedInkCloseMode : uint8_t { Drain, Cancel };

	// 页 GUID 由请求携带，revision 标识目录中已确认的页版本。
	struct PagedInkBoundaryIdentity
	{
		uint64_t generation = 0;
		uint64_t revision = 0;
		bool operator==(const PagedInkBoundaryIdentity&) const = default;
	};

	struct PagedInkRequestIdentity
	{
		draw3::uink::UInkGuid pageGuid;
		uint64_t revision = 0;
		uint64_t slotGeneration = 0;
		uint64_t loadGeneration = 0;
		std::optional<uint64_t> boundaryGeneration;
		PagedInkRequestKind kind = PagedInkRequestKind::Full;
		std::optional<int32_t> laneBinding; // 仅 LoadLane 使用，不携带页 GUID。
	};

	struct PagedInkSaveRequest
	{
		PagedInkRequestIdentity identity;
		// Clear 请求以 Clear 结束；内存整页用 operations 和 intervalOrdinal 保留边界。
		draw3::uink::Draw3UInkCanvasSnapshot snapshot;
		// 有值时 snapshot 只包含当前区间，并要求匹配盘上确认边界。
		std::optional<PagedInkBoundaryIdentity> previousIntervalOnDisk;
	};

	struct PagedInkLoadRequest
	{
		draw3::uink::UInkGuid pageGuid;
		uint64_t slotGeneration = 0;
		uint64_t loadGeneration = 0;
	};

	struct PagedInkLoadLaneRequest
	{
		int32_t binding = 0;
		uint64_t slotGeneration = 0;
		uint64_t loadGeneration = 0;
	};

	struct PagedInkLoadPreviousRequest
	{
		PagedInkLoadRequest load;
		PagedInkBoundaryIdentity boundary;
	};

	struct PagedInkFileState
	{
		bool failed = false;
		bool sealed = false;
		bool tailUnconfirmed = false;
		bool compacting = false; // 仅实时 FileState 使用，驱逐须另取许可。
	};

	struct PagedInkPageResult
	{
		PagedInkResult result = PagedInkResult::Failed;
		PagedInkError error = PagedInkError::None;
		std::optional<PagedInkBoundaryIdentity> confirmedBoundary;
		std::optional<uint64_t> confirmedRevision; // 实际读写的页版本，含无 Clear 边界的页。
		bool degraded = false;
		bool readDegraded = false; // 本次使用的区间存在读取降级。
		bool prefixReadDegraded = false;
		bool tailReadDegraded = false; // 无 Clear 的页仅有当前区间。
		uint64_t prefixSkippedBlocks = 0;
		uint64_t tailSkippedBlocks = 0;
		std::optional<draw3::uink::Draw3UInkCanvasSnapshot> snapshot;
		std::optional<draw3::uink::UInkReadStatus> readStatus;
		std::optional<draw3::uink::UInkReadSummary> readSummary;
		draw3::uink::Draw3UInkV11FallbackCounts fallbackCounts;
		std::vector<draw3::uink::UInkDiagnostic> readDiagnostics;
		std::vector<draw3::uink::UInkDiagnostic> diagnostics;
		double readMs = 0; // L2 整次调用包含 ioMs、解码及校验收尾。
		double ioMs = 0;
		double importMs = 0;
		uint64_t pageBytes = 0;
	};

	struct PagedInkPageMetadata
	{
		draw3::uink::UInkGuid pageGuid;
		uint32_t pageIndex = 0;
		uint32_t pageNumber = 0;
		std::optional<int32_t> slideId;
		int32_t binding = 0; // 有效 binding，缺省为 0。
		std::optional<int32_t> pageState;
		std::optional<int32_t> pageKind;
		// 显式比较：MSVC 模块中默认比较找不到 UInkGuid 的隐藏友元 operator==。
		bool operator==(const PagedInkPageMetadata& other) const noexcept
		{
			return pageGuid.Bytes() == other.pageGuid.Bytes() && pageIndex == other.pageIndex &&
				pageNumber == other.pageNumber && slideId == other.slideId && binding == other.binding &&
				pageState == other.pageState && pageKind == other.pageKind;
		}
	};

	struct PagedInkLanePage : PagedInkPageResult
	{
		// 读取失败时也保留已确认的元数据和修订，调用方可以识别冷页。
		PagedInkPageMetadata metadata;
	};

	struct PagedInkCompletion : PagedInkPageResult
	{
		PagedInkRequestIdentity identity;
		PagedInkFileState file;
		uint32_t systemError = 0;
		double queueMs = 0;
		double writeMs = 0;
		double workMs = 0;
		double workerMs = 0; // 出队至回执发布，含日志及 worker 收尾。
		uint64_t fileBytes = 0;
		std::vector<PagedInkLanePage> lanePages; // LoadLane 成功时，每项独立结算。
		uint64_t requestId = 0; // 隐藏自检在准入时登记的序号。
		bool saveDequeued = false; // 诊断用：区分已出队的保存和文件故障清退的保存。
	};

	const char* PagedInkWriteDiagnosticStage(const PagedInkCompletion& completion) noexcept;

	struct PagedInkEvictionState
	{
		std::optional<uint64_t> confirmedRevision;
		uint64_t currentRevision = 0;
		bool isCurrentPage = false;
		bool isLoadTarget = false;
		bool degraded = false;
	};

	bool IsPageEvictable(const PagedInkEvictionState& page, const PagedInkFileState& file) noexcept;

	struct PagedInkStoreOptions
	{
		std::wstring root;
		PagedInkFileNaming naming;
		draw3::uink::Draw3UInkV11PageContext pageContext;
		std::optional<std::string> hostId;
		draw3::uink::Draw3UInkV11ImportOptions importOptions;
		// 回执或整理结束后有待重试的驱逐时唤醒。回调不分配内存，可并发执行。
		// 可以调用 Take，但不得调用本服务的 Close 或 Drain，否则会死锁。
		// wakeContext 须存活至 Close 返回，返回后服务保证不再调用唤醒回调。
		void (*wake)(void*) noexcept = nullptr;
		void* wakeContext = nullptr;
	};

	// 仅供无窗口测试。
	struct PagedInkStoreTestHook
	{
		// 后台执行任务前调用，可阻塞或抛出异常。
		void (*beforeWork)(void*, const PagedInkRequestIdentity&) = nullptr;
		void* context = nullptr;
		// 回执已入队、唤醒尚未调用时在锁外执行。
		void (*beforeWake)(void*) noexcept = nullptr;
		// 损坏夹具在第一次读页前释放写句柄，以便原位修改文件。目录仍可用于读取。
		void (*beforeReadAfterClosingWriter)(void*, const std::wstring&) = nullptr;
		// 仍持有写句柄时，可从相同偏移读取损坏的磁盘夹具副本。返回值须存活至本次读取结束。
		const std::wstring* (*readPathOverride)(void*, const std::wstring&) = nullptr;
		// 预留成功后在服务锁内调用，须返回序号且不得重入服务。
		uint64_t (*recordAccepted)(void*) noexcept = nullptr;
		// 拼接前缀读盘成功后、校验前调用，可抛出异常。
		void (*afterSplicePrefixRead)(void*) = nullptr;
		uint64_t compactMinSupersededBytes = 8ull * 1024 * 1024;
		uint32_t compactMinSupersededPercent = 50;
		// 整理已纳入排空且仍可准入新请求时调用。
		void (*beforeCompact)(void*) noexcept = nullptr;
		// 空闲整理因驱逐许可等待时在服务锁内调用，不得重入服务。
		void (*onCompactBlockedByEviction)(void*) noexcept = nullptr;
		// 保存因驱逐许可等待时在服务锁内调用，不得重入服务。
		void (*onSaveBlockedByEviction)(void*) noexcept = nullptr;
#ifdef DRAW3_TESTING
		// 返回要在真实文件中写入无效 wire 标记的偏移，仅供损坏夹具使用。
		std::optional<uint64_t> (*corruptOffsetBeforeRead)(void*, const draw3::uink::UInkGuid&) noexcept = nullptr;
#endif
	};

	class PagedInkStore
	{
	public:
		explicit PagedInkStore(PagedInkStoreOptions options, PagedInkStoreTestHook testHook = {});
		~PagedInkStore();

		PagedInkAdmission Save(PagedInkSaveRequest request) noexcept;
		PagedInkAdmission Load(PagedInkLoadRequest request) noexcept;
		PagedInkAdmission LoadLane(PagedInkLoadLaneRequest request) noexcept;
		PagedInkAdmission LoadPreviousInterval(PagedInkLoadPreviousRequest request) noexcept;
		std::optional<PagedInkCompletion> Take() noexcept;
		void Drain();
		void Close(PagedInkCloseMode mode = PagedInkCloseMode::Drain);

		std::wstring FilePath() const;
		PagedInkFileState FileState() const noexcept;
		// 成功后必须在实际释放完成时归还许可，持有期间不能调用 Drain 或 Close。
		// 只有不依赖磁盘的槽可以放宽文件健康要求。
		bool TryBeginEviction(bool requiresHealthyFile = true) noexcept;
		void EndEviction() noexcept;
		// 驱逐准备失败时留给下一次正常唤醒处理，避免分配失败时反复自唤醒。
		void RequestEvictionRetry() noexcept;
		// 屏障已解除时消费待重试标记，由绘制线程重新检查该存储的候选。
		bool TakeEvictionRetry() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
