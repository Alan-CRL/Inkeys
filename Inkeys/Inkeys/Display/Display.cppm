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

	[[nodiscard]] bool Initialize();
	[[nodiscard]] bool Refresh(ChangeReason reason = ChangeReason::Manual);
	void Shutdown() noexcept;
	[[nodiscard]] SnapshotPtr GetSnapshot() noexcept;
	[[nodiscard]] Subscription Subscribe(ChangeCallback callback);
	[[nodiscard]] WNDPROC WindowProc() noexcept;
}
