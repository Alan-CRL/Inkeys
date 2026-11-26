module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Inkeys.Drawing.Draw3.desktop_ink_snapshots;

export import draw3.uink_draw3_v11;
import draw3.uink_store;

export namespace Inkeys::Drawing::Draw3
{
	enum class DesktopInkOperation : uint8_t { Save, Load };
	enum class DesktopInkAdmission : uint8_t { Accepted, Closed, InvalidRequest, ReservationFailed, MissingSave };
	enum class DesktopInkResult : uint8_t { Succeeded, Failed, Canceled };
	enum class DesktopInkError : uint8_t
	{
		None, InvalidSnapshot, DirectoryFailed, ExportFailed, PublishFailed, ReadFailed,
		InvalidFile, ImportFailed, Exception, Closing
	};
	enum class DesktopInkCloseMode : uint8_t { Drain, Cancel };

	struct DesktopInkIdentity
	{
		draw3::uink::UInkGuid requestId;
		uint64_t clearGeneration = 0;
		draw3::uink::UInkGuid pageGuid;
	};

	class DesktopInkSnapshots;

	class DesktopInkRecoveryRegistration
	{
	public:
		DesktopInkRecoveryRegistration() = default;
		DesktopInkRecoveryRegistration(DesktopInkRecoveryRegistration&&) noexcept = default;
		DesktopInkRecoveryRegistration& operator=(DesktopInkRecoveryRegistration&&) noexcept = default;
		void Reset() noexcept { lease_.reset(); }

	private:
		friend class DesktopInkSnapshots;
		std::shared_ptr<void> lease_;
	};

	struct DesktopInkSaveRequest
	{
		DesktopInkIdentity identity;
		std::shared_ptr<const draw3::uink::Draw3UInkCanvasSnapshot> snapshot;
		// 同步准入时写入空登记句柄，恢复点销毁时注销其路径。
		DesktopInkRecoveryRegistration* registration = nullptr;
	};

	struct DesktopInkLoadRequest
	{
		DesktopInkIdentity identity;
		draw3::uink::UInkGuid saveRequestId;
	};

	struct DesktopInkCompletion
	{
		DesktopInkIdentity identity;
		DesktopInkOperation operation = DesktopInkOperation::Save;
		DesktopInkResult result = DesktopInkResult::Failed;
		DesktopInkError error = DesktopInkError::None;
		bool degraded = false;
		uint32_t systemError = 0;
		std::shared_ptr<const std::wstring> publishedPath;
		std::optional<draw3::uink::Draw3UInkCanvasSnapshot> snapshot;
		draw3::uink::Draw3UInkV11FallbackCounts fallbackCounts;
		std::vector<draw3::uink::UInkDiagnostic> diagnostics;
		std::vector<draw3::uink::UInkDiagnostic> readDiagnostics;
	};

	struct DesktopInkSnapshotsOptions
	{
		std::wstring root;
		float dpiScale = 1.0f;
		// 允许回调取回执。回调不得重入 Start 或 Close；上下文须存活至 Close 返回。
		void (*wake)(void*) noexcept = nullptr;
		void* wakeContext = nullptr;
	};

	struct DesktopInkSnapshotsTestHook
	{
		// 在后台线程执行，只在隔离测试中使用。
		void (*beforeWork)(void*, DesktopInkOperation, const DesktopInkIdentity&) = nullptr;
		void* context = nullptr;
		void (*afterSave)(void*, const DesktopInkCompletion&) noexcept = nullptr;

		struct LocalTime
		{
			uint16_t year, month, day, hour, minute, second, milliseconds;
		};
		LocalTime (*sampleLocalTime)(void*, const DesktopInkIdentity&) noexcept = nullptr;
	};

	struct DesktopInkSnapshotsDiagnostics
	{
		uint64_t accepted = 0;
		uint64_t committed = 0;
		uint64_t failed = 0;
		uint64_t pending = 0;
	};

	// 两个 L4 存储 worker 共用单行格式，调用点均在服务锁外。
	void WriteUInkReadLog(std::string_view scene, std::string_view operation, std::wstring_view path,
		const draw3::uink::UInkGuid& pageGuid, uint32_t pageNumber, const draw3::uink::UInkReadResult& read,
		const draw3::uink::UInkStoreReadFacts& facts, std::string_view error, uint64_t strokes, uint64_t splitUndo,
		const draw3::uink::Draw3UInkV11FallbackCounts& fallbacks, double readMs, double importMs,
		bool storedDegraded = false, std::string_view failureStage = {}) noexcept;
	struct UInkWriteLogFacts
	{
		bool sealed = false;
		bool tailUnconfirmed = false;
		const char* rollbackStage = nullptr;
		uint32_t rollbackSystemError = 0;
	};

	void WriteUInkWriteLog(std::string_view scene, std::string_view operation, std::wstring_view path,
		const draw3::uink::UInkGuid& pageGuid, uint32_t pageNumber, std::string_view stage, std::string_view error,
		uint32_t systemError, const draw3::uink::Draw3UInkV11FallbackCounts& fallbacks, double writeMs,
		bool prefixReadDegraded = false, UInkWriteLogFacts facts = {}) noexcept;

	class DesktopInkSnapshots
	{
	public:
		DesktopInkSnapshots();
		~DesktopInkSnapshots();
		DesktopInkSnapshots(const DesktopInkSnapshots&) = delete;
		DesktopInkSnapshots& operator=(const DesktopInkSnapshots&) = delete;

		// Close 后若仍有未取回执，Start 返回 false；通过 Take 取完后可重启。
		bool Start(DesktopInkSnapshotsOptions options, DesktopInkSnapshotsTestHook testHook = {}) noexcept;
		DesktopInkAdmission Save(DesktopInkSaveRequest request) noexcept;
		DesktopInkAdmission Load(DesktopInkLoadRequest request) noexcept;
		std::optional<DesktopInkCompletion> Take() noexcept;
		DesktopInkSnapshotsDiagnostics Diagnostics() const noexcept;
		// 排空已接收的保存；未开始的读取逐个以取消结算。
		void Close(DesktopInkCloseMode mode = DesktopInkCloseMode::Drain) noexcept;

	private:
		struct Impl;
		std::shared_ptr<Impl> impl_;
	};
}
