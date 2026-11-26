module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module draw3.uink_store;

export import draw3.uink_codec;

export namespace draw3::uink
{
	// 一页的完整字节上限，取 UInkReadLimits::maxTopLevelObjectBytes 的默认值。
	inline constexpr uint64_t kUInkStoreMaxPageBytes = 32ull * 1024ull * 1024ull;

	struct UInkPageRange
	{
		uint64_t offset = 0;
		uint64_t length = 0;
		uint32_t pageIndex = 0;
		uint64_t writeSequence = 0;
		// 整理会改变最后一页，保留该页接续所需的编号。
		uint64_t nextContentId = 0;
		uint32_t lastUndoId = 0;
	};

	struct UInkPageDirectory
	{
		// 同一个写入器只写一个 Workspace、一个 Device 的第 0 层，故用 pageGuid 作键。
		std::map<std::array<uint8_t, 16>, UInkPageRange> pages;
		uint64_t supersededBytes = 0;
		std::optional<UInkGuid> lastPageGuid;
		uint64_t nextContentId = 0;
		uint32_t lastUndoId = 0;
		UInkStreamContext context;
	};

	struct UInkStoreWriteResult
	{
		bool success = false;
		uint32_t systemError = 0;
		std::vector<UInkDiagnostic> diagnostics;
		// Create 发布成功后即记录实际绝对路径，包括发布后重开失败的情况。
		bool published = false;
		std::wstring publishedPath;
		const char* failureStage = nullptr; // 失败阶段不依赖诊断明细分配。
		const char* rollbackStage = nullptr;
		uint32_t rollbackSystemError = 0;
	};

	enum class UInkStoreCompactReopen : uint8_t { NotAttempted, Succeeded, Failed };

	struct UInkStoreCompactResult : UInkStoreWriteResult
	{
		// 替换成功即提交新目录；success 还要求新文件重开成功。
		bool replacementCommitted = false;
		UInkStoreCompactReopen reopen = UInkStoreCompactReopen::NotAttempted;
		uint32_t reopenSystemError = 0;
	};

	// 按调用方限额整读自检后写入完整快照。不覆盖现有文件，发布成功即提交。
	UInkStoreWriteResult PublishUInkSnapshotFile(const std::wstring& basePath,
		const UInkHeader& header, const std::optional<UInkHeaderExtension>& headerExtension,
		const UInkCanvas& canvas, const UInkReadLimits& limits = {}) noexcept;

	class UInkPagedFileWriter
	{
	public:
		// 在同目录发布一份新文件，路径冲突时加后缀。通过 Path() 取得实际路径。
		static std::unique_ptr<UInkPagedFileWriter> Create(const std::wstring& basePath,
			const UInkHeader& header, const std::optional<UInkHeaderExtension>& headerExtension,
			const UInkCanvas& firstPage, UInkStoreWriteResult& result) noexcept;

		~UInkPagedFileWriter() noexcept;
		UInkPagedFileWriter(const UInkPagedFileWriter&) = delete;
		UInkPagedFileWriter& operator=(const UInkPagedFileWriter&) = delete;

		// 追加本页的完整版本，落盘后更换目录项。
		UInkStoreWriteResult AppendPage(const UInkCanvas& canvas) noexcept;
		// 替换失败时保留旧目录；不论替换结果，只要重开失败就封闭写入器。
		UInkStoreCompactResult Compact() noexcept;
		void Close() noexcept;

		const std::wstring& Path() const noexcept;
		const UInkPageDirectory& Directory() const noexcept;
		bool IsSealed() const noexcept;
		// 尾部未确认时页目录是最后确认的快照，L4 不得据此驱逐任何页。
		bool IsTailUnconfirmed() const noexcept;
		uint64_t SupersededBytes() const noexcept;
		uint64_t FileBytes() const noexcept;

#ifdef DRAW3_TESTING
		// 在保留写句柄的情况下构造原位损坏，用于验证后续写入和整理。
		bool OverwriteByteForTesting(uint64_t offset, std::byte value) noexcept;
#endif

	private:
		UInkPagedFileWriter();
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	// 存储层的 I/O 与范围校验事实独立于 L1 的文件内诊断。
	struct UInkStoreReadFacts
	{
		uint64_t bytesRead = 0;
		uint64_t rangeStart = 0;
		double ioMs = 0; // 打开文件至关闭句柄，含定位、缓冲分配和读取。
		const char* failureStage = nullptr;
		UInkDiagnosticCode failureCode = UInkDiagnosticCode::None;
		uint32_t systemError = 0;
	};

	// 使用目录的范围与解析上下文。成功或部分恢复时 document 内有一个 Canvas。
	UInkReadResult ReadUInkPage(const std::wstring& path, const UInkPageDirectory& directory,
		const UInkGuid& pageGuid, const UInkReadLimits& limits = {}, UInkStoreReadFacts* facts = nullptr) noexcept;
	// 整读后按完整 Canvas 唯一键去重，再按页号和最后出现位置排序。
	// 一次性快照可检查去重前的页数，拒绝同一身份的重复 Canvas。
	UInkReadResult ReadUInkStoreFile(const std::wstring& path,
		const UInkReadLimits& limits = {}, uint64_t* canvasCountBeforeDeduplication = nullptr,
		UInkStoreReadFacts* facts = nullptr) noexcept;

	// 仅供无窗口测试，每个标志在下一次对应的 IO 上消耗。
	struct UInkStoreTestFaultInjection
	{
		bool shortWrite = false;
		bool failFlush = false;
		bool failTruncate = false;
		bool failMove = false;
		bool failReopen = false;
		bool failDeleteTemp = false;
	};

	void SetUInkStoreTestFaultInjection(const UInkStoreTestFaultInjection& faults) noexcept;
	void ResetUInkStoreTestFaultInjection() noexcept;
}
