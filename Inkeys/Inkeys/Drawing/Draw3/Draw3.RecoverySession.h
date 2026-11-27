#pragma once

// C1.6b 崩溃恢复：每个进程一份会话记录（Inkeys\Save\Ink\session\），记录本进程写的文件、写前登记、
// 当前场景与白板页、是否正常结束。恢复方据此选会话、判定断电半页、按场景结算。
// 普通头，供 Host、存储 worker 与启动扫描共用；不 include <Windows.h>（避免提前定义 min/max 宏），句柄用 void*。

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Inkeys::Drawing::Draw3::Recovery
{
	// NTFS 文件身份：整理用新文件替换旧文件后偏移全部失效，恢复时据此识别“已是整理后的完整文件”。
	struct FileIdentity
	{
		uint32_t volumeSerial = 0;
		uint64_t fileIndex = 0;
		bool valid = false;
		bool operator==(const FileIdentity&) const = default;
		std::string ToString() const;
		static FileIdentity Parse(const std::string& text) noexcept;
	};
	bool QueryFileIdentity(void* file, FileIdentity& identity) noexcept;
	bool QueryFileIdentity(const std::wstring& path, FileIdentity& identity) noexcept;

	enum class PendingKind : uint8_t { Page, Create, Relocate, ShapeGroup };
	// 写前登记：整页类写入与多块图形组写入前先持久化 {fileId, start}，写成功后清除；恢复时截到 start。
	struct PendingWrite
	{
		PendingKind kind = PendingKind::Page;
		uint64_t start = 0;
	};

	enum class Workspace : uint8_t { Desktop, Whiteboard, Presentation };

	struct FileEntry
	{
		std::wstring path;
		FileIdentity identity;
		std::optional<PendingWrite> pending;
	};

	struct PresentationEntry
	{
		std::string key; // PPT 存储键（文稿身份），与 Host 懒建存储的键一致
		FileEntry file;
	};

	struct SessionRecord
	{
		uint32_t version = 1;
		uint32_t pid = 0;
		std::string startTime; // ISO 8601 本地时间
		bool recoverInk = true;
		bool clean = false;
		Workspace workspace = Workspace::Desktop;
		std::optional<FileEntry> desktop;
		std::optional<FileEntry> whiteboard;
		std::optional<std::string> whiteboardPageGuid;
		std::optional<uint32_t> whiteboardPageIndex;
		std::vector<PresentationEntry> presentations;
		bool settledDesktop = false;
		bool settledWhiteboard = false;
		std::vector<std::string> settledPresentations;
		uint32_t failedRecoveryAttempts = 0;
		// 桌面 Clear 代次与上一区间（desktop\ 下该次 Clear 的快照）。快照代次等于最新 Clear 代次时，
		// 恢复后可撤回到上一区间。desktopCleared：Clear 后尚未建新的活动区间文件，与“从未写过桌面”区分。
		uint64_t desktopClearGeneration = 0;
		bool desktopCleared = false;
		std::optional<std::wstring> desktopPreviousInterval;
		uint64_t desktopPreviousGeneration = 0;
	};

	// 写盘：临时文件写完 FlushFileBuffers，再 MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)。
	bool WriteSessionRecord(const std::wstring& path, const SessionRecord& record) noexcept;
	std::optional<SessionRecord> ReadSessionRecord(const std::wstring& path) noexcept;

	// 本进程的会话：先 CREATE_NEW 拿 DELETE_ON_CLOSE 存活锁，再发布记录。锁拿不到时整个会话不启用（等同开关关闭）。
	class Session
	{
	public:
		Session() = default;
		~Session();
		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;

		bool Start(const std::wstring& inkRoot, bool recoverInk) noexcept;
		// 正常结束：在三个存储服务排空并关闭之后调用，写 clean 后释放存活锁。
		void Stop(bool clean) noexcept;
		bool Active() const noexcept { return active_.load(std::memory_order_acquire); }
		const std::wstring& Name() const noexcept { return name_; }

		// 场景 worker 同步调用，返回时已持久化（失败返回 false，调用方暂停依赖登记的写入）。
		bool RegisterDesktopFile(const std::wstring& path, const FileIdentity& identity) noexcept;
		bool RegisterWhiteboardFile(const std::wstring& path, const FileIdentity& identity) noexcept;
		bool RegisterPresentationFile(const std::string& key, const std::wstring& path,
			const FileIdentity& identity) noexcept;
		bool RegisterPending(Workspace slot, const std::string& presentationKey,
			const FileIdentity& identity, const PendingWrite& pending) noexcept;
		bool ClearPending(Workspace slot, const std::string& presentationKey, const FileIdentity& identity) noexcept;
		// 桌面 Clear 轮换区间：先持久化“当前无活动文件”与新的 Clear 代次，旧区间文件不再参与恢复。
		bool ClearDesktopFile(uint64_t clearGeneration) noexcept;
		// 上一区间快照：clearGeneration 为空表示“当前区间的上一区间”（恢复装入后由新会话登记，已有同代次快照时不覆盖）。
		bool RegisterDesktopPreviousInterval(const std::wstring& path, std::optional<uint64_t> clearGeneration) noexcept;
		// 同上，但只记下请求并唤醒记录 worker 写盘（调用方可能是绘制线程，不能同步写 JSON）。
		void PublishDesktopPreviousInterval(const std::wstring& path, uint64_t clearGeneration) noexcept;
		uint64_t DesktopClearGeneration() noexcept;
		// 开关关闭时不做逐笔写前登记（保持 C1.4 的写入开销）；文件登记照常，便于中途开启。
		bool RecoverInk() const noexcept { return recoverInk_.load(std::memory_order_acquire) != 0; }

		// 绘制线程：只写原子量并唤醒记录 worker，不加锁、不等待。
		void PublishWorkspace(Workspace workspace) noexcept;
		void PublishWhiteboardPage(const std::string& pageGuid, uint32_t pageIndex) noexcept;
		void PublishRecoverInk(bool enabled) noexcept;

	private:
		void WorkerLoop() noexcept;
		bool WriteLocked() noexcept;
		bool ApplyDesktopPreviousLocked(const std::wstring& path, std::optional<uint64_t> clearGeneration);
		FileEntry* SlotLocked(Workspace slot, const std::string& key) noexcept;

		std::wstring directory_;
		std::wstring name_;
		std::wstring recordPath_;
		void* lock_ = nullptr; // 存活锁句柄；nullptr 表示未持有
		std::atomic<bool> active_{ false };

		std::mutex mutex_;
		SessionRecord record_;

		// 绘制线程发布的最新状态（单写者）；whiteboardPage 用序号保护字符串槽。
		std::atomic<uint8_t> workspace_{ 0 };
		std::atomic<uint32_t> whiteboardPageIndex_{ 0 };
		std::atomic<uint64_t> publishSerial_{ 0 };
		std::atomic<int8_t> recoverInk_{ -1 };
		std::mutex pageMutex_; // 只由 worker 与偶发的页切换短暂持有，绘制线程用 try_lock
		std::string pendingPageGuid_;
		uint32_t pendingPageGuidIndex_ = UINT32_MAX; // 与 pendingPageGuid_ 成对，worker 只在与索引一致时写 GUID
		std::mutex previousMutex_; // 只保护下一项；持有时间仅一次字符串移动
		std::optional<std::pair<std::wstring, uint64_t>> pendingPrevious_;

		std::thread worker_;
		std::mutex workerMutex_;
		std::condition_variable workerCondition_;
		bool workerStop_ = false;
		uint64_t writtenSerial_ = 0;
	};

	// ---- 启动扫描（恢复方） ----
	enum class Liveness : uint8_t { Dead, Alive, Unknown };
	Liveness ProbeSessionLock(const std::wstring& lockPath) noexcept;

	struct AbandonedSession
	{
		std::wstring recordPath;
		std::wstring baseName; // 不含扩展名
		SessionRecord record;
	};

	// 列出可恢复的被放弃会话（跳过 clean、recoverInk=false、存活、未知、全部已结算），按开始时间从新到旧。
	// 同时删除对应进程已结束的 clean 或全部已结算的旧记录。
	std::vector<AbandonedSession> ScanAbandonedSessions(const std::wstring& inkRoot,
		const std::wstring& ownName) noexcept;

	// 恢复认领：CREATE_NEW + DELETE_ON_CLOSE 的 .claim，持有到本进程结束；失败表示已被别的进程认领。
	// 返回认领句柄（调用方持有到进程结束或恢复完成），失败返回 nullptr。
	void* ClaimSession(const std::wstring& inkRoot, const std::wstring& baseName) noexcept;
}
