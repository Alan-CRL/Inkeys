module;

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Inkeys.Display;

export namespace Inkeys::Display
{
	enum class ChangeReason : std::uint8_t
	{
		Startup,
		Display,
		Device,
		Settings,
		Manual,
		Taskbar,
		TaskbarVisibility,
	};

	enum class DisplayTopology : std::uint8_t
	{
		Unknown,
		Single,
		Extended,
		CloneOrMixed,
	};

	enum class EdidStatus : std::uint8_t
	{
		Unavailable,
		ReadFailed,
		ParseFailed,
		Parsed,
	};

	enum class PhysicalSizeUnavailableReason : std::uint8_t
	{
		None,
		SnapshotFallback,
		TopologyUnknown,
		CloneOrMixed,
		DisplayTargetAmbiguous,
		EdidUnavailable,
		EdidReadFailed,
		EdidParseFailed,
		MissingDimensions,
		DimensionsBelowMinimum,
	};

	struct EdidInfo
	{
		// valid 只表示原始 EDID 已解析成功，不代表物理尺寸可用于业务。
		bool valid = false;
		EdidStatus status = EdidStatus::Unavailable;
		std::uint8_t majorVersion = 0;
		std::uint8_t minorVersion = 0;
		std::wstring devicePath;
		std::wstring deviceId;
		std::vector<std::uint8_t> rawBytes;
		int rawPhysicalWidthCm = 0;
		int rawPhysicalHeightCm = 0;

		[[nodiscard]] std::wstring VersionText() const;
	};

	struct ActiveDisplayTargetInfo
	{
		LUID sourceAdapterId{};
		UINT32 sourceId = 0;
		LUID targetAdapterId{};
		UINT32 targetId = 0;
		std::wstring sourceDeviceName;
		std::wstring monitorDevicePath;
		std::wstring monitorFriendlyName;
		EdidInfo edid;
	};

	struct PhysicalSizeInfo
	{
		bool available = false;
		int widthCm = 0;
		int heightCm = 0;
		PhysicalSizeUnavailableReason unavailableReason =
			PhysicalSizeUnavailableReason::TopologyUnknown;
	};

	enum class TaskbarEdge : std::uint8_t
	{
		Unknown, Left, Top, Right, Bottom,
	};

	enum class TaskbarExposure : std::uint8_t
	{
		Unknown, Hidden, Raised,
	};

	struct TaskbarInfo
	{
		TaskbarEdge edge = TaskbarEdge::Unknown;
		bool autoHide = false;
		TaskbarExposure exposure = TaskbarExposure::Unknown;
		// 查询失败只保留同屏同几何的最后可信状态，不把 Unknown 当作已收起。
		TaskbarExposure lastKnownExposure = TaskbarExposure::Unknown;
		int dockTopPx = 0;
	};

	struct TaskbarOwnerInfo
	{
		HMONITOR monitor = nullptr;
		UINT edge = 0;
	};

	// owner 消息批次的纯状态机；旧 epoch/重复事件不能重启合并期限。
	class TaskbarEventBatch
	{
	public:
		void Reset(ULONG_PTR epoch = 0) noexcept;
		[[nodiscard]] bool Queue(ULONG_PTR epoch, bool structure) noexcept;
		void QueueFailed(ULONG_PTR epoch) noexcept;
		[[nodiscard]] bool BeginCoalescing(ULONG_PTR epoch) noexcept;
		[[nodiscard]] std::optional<bool> Consume(ULONG_PTR epoch) noexcept;
	private:
		ULONG_PTR epoch_ = 0;
		bool messageQueued_ = false;
		bool timerArmed_ = false;
		bool structurePending_ = false;
	};

	struct MonitorInfo
	{
		HMONITOR handle = nullptr;
		std::wstring deviceName;
		RECT bounds{};
		RECT workArea{};
		int pixelWidth = 0;
		int pixelHeight = 0;
		UINT effectiveDpiX = USER_DEFAULT_SCREEN_DPI;
		UINT effectiveDpiY = USER_DEFAULT_SCREEN_DPI;
		// 完整任务栏厚度（物理像素），0 表示本屏暂无可靠任务栏数据。
		int taskbarThicknessPx = 0;
		TaskbarInfo taskbar;
		DWORD orientation = DMDO_DEFAULT;
		bool primary = false;
		bool fallback = false;
		std::optional<std::size_t> targetIndex;
		EdidInfo edid;
		PhysicalSizeInfo physicalSize;
	};

	struct Snapshot
	{
		std::uint64_t generation = 0;
		std::vector<MonitorInfo> monitors;
		std::vector<ActiveDisplayTargetInfo> activeTargets;
		std::size_t primaryIndex = 0;
		RECT virtualBounds{};
		DisplayTopology topology = DisplayTopology::Unknown;
		bool fallback = false;

		[[nodiscard]] const MonitorInfo* Primary() const noexcept;
		[[nodiscard]] const MonitorInfo* Find(HMONITOR monitor) const noexcept;
		[[nodiscard]] bool SemanticallyEquals(const Snapshot& other) const;
	};

	using SnapshotPtr = std::shared_ptr<const Snapshot>;
	using ChangeCallback = std::function<void(SnapshotPtr)>;

	class Subscription
	{
	public:
		Subscription() noexcept = default;
		~Subscription();
		Subscription(const Subscription&) = delete;
		Subscription& operator=(const Subscription&) = delete;
		Subscription(Subscription&& other) noexcept;
		Subscription& operator=(Subscription&& other) noexcept;

		void Reset() noexcept;
		[[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }

	private:
		explicit Subscription(std::shared_ptr<void> state) noexcept
			: state_(std::move(state)) {}
		std::shared_ptr<void> state_;
		friend Subscription Subscribe(ChangeCallback callback);
	};

	// 纯策略入口供无窗口测试与业务复用，不执行硬件查询。
	[[nodiscard]] EdidInfo ParseEdid(
		std::span<const std::uint8_t> bytes,
		std::wstring_view deviceId = {});
	[[nodiscard]] DisplayTopology ClassifyTopology(
		std::span<const ActiveDisplayTargetInfo> targets) noexcept;
	[[nodiscard]] PhysicalSizeInfo ResolvePhysicalSize(
		const EdidInfo& edid, DWORD orientation, DisplayTopology topology,
		bool uniqueTarget, bool fallback = false) noexcept;
	[[nodiscard]] std::wstring_view DisplayTopologyText(DisplayTopology topology) noexcept;
	[[nodiscard]] std::wstring_view EdidStatusText(EdidStatus status) noexcept;
	[[nodiscard]] std::wstring_view PhysicalSizeUnavailableReasonText(
		PhysicalSizeUnavailableReason reason) noexcept;

	// 共用任务栏纯计算；baseHeightDip 为主栏主体高度，不含阴影/透明余量。
	[[nodiscard]] int ResolveTaskbarThickness(const RECT& monitor,
		const RECT& taskbar, UINT edge) noexcept;
	[[nodiscard]] HMONITOR ResolveTaskbarOwner(const Snapshot& snapshot,
		const RECT& taskbar, UINT edge) noexcept;
	// 未知方向与已知方向共用候选规则；真实歧义无可信历史时返回空 owner。
	[[nodiscard]] TaskbarOwnerInfo ResolveTaskbarOwnerCandidate(const Snapshot& snapshot,
		const RECT& taskbar, std::optional<UINT> edge, bool autoHide,
		TaskbarOwnerInfo previous = {}) noexcept;
	[[nodiscard]] bool ApplyTaskbarSample(Snapshot& snapshot, HMONITOR owner,
		const RECT& taskbar, UINT edge) noexcept;
	[[nodiscard]] int ResolveTaskbarFullThickness(int sampleThicknessPx,
		int previousThicknessPx, bool autoHide, bool completeSample) noexcept;
	[[nodiscard]] TaskbarExposure ResolveTaskbarExposure(const RECT& monitor,
		const RECT& taskbar, int fullThicknessPx, UINT dpi,
		TaskbarExposure previous = TaskbarExposure::Unknown, bool visible = true) noexcept;
	[[nodiscard]] double ResolveDesktopBottomDockLine(const MonitorInfo& monitor) noexcept;
	[[nodiscard]] bool ShouldProtectTaskbarBottomEdge(const MonitorInfo& monitor) noexcept;
	// 启动预览仅查询几何、DPI 和任务栏；不创建 observer、不枚举 EDID。
	[[nodiscard]] std::optional<MonitorInfo> QueryTaskbarMonitorInfo(HMONITOR monitor) noexcept;
	[[nodiscard]] double ResolveUiScaleBase(UINT dpi, int taskbarThicknessPx,
		UINT primaryDpi, int primaryTaskbarThicknessPx,
		bool useTaskbar, double baseHeightDip = 80.0) noexcept;
	[[nodiscard]] double ResolveUiScaleBase(const Snapshot& snapshot,
		const MonitorInfo& monitor, bool useTaskbar,
		double baseHeightDip = 80.0) noexcept;
	// Mini 仅采样 DPI/任务栏；不枚举 EDID，也不发布正式快照。
	[[nodiscard]] double QueryUiScaleBase(HMONITOR monitor, bool useTaskbar,
		double baseHeightDip = 80.0) noexcept;
	// 启动 Mini 阶段只查询目标屏的有效 DPI，不枚举拓扑/EDID，也不发布显示快照。
	[[nodiscard]] UINT QueryEffectiveDpi(HMONITOR monitor) noexcept;
	[[nodiscard]] bool Initialize();
	[[nodiscard]] bool Refresh(ChangeReason reason = ChangeReason::Manual);
	void Shutdown() noexcept;
	[[nodiscard]] SnapshotPtr GetSnapshot() noexcept;
	[[nodiscard]] Subscription Subscribe(ChangeCallback callback);
	[[nodiscard]] WNDPROC WindowProc() noexcept;
}
