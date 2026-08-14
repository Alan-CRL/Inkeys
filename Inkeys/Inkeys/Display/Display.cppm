module;

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

	struct EdidInfo
	{
		bool valid = false;
		std::uint8_t majorVersion = 0;
		std::uint8_t minorVersion = 0;
		std::wstring deviceId;
		int rawPhysicalWidthCm = 0;
		int rawPhysicalHeightCm = 0;
		int physicalWidthCm = 0;
		int physicalHeightCm = 0;

		[[nodiscard]] std::wstring VersionText() const;
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
		EdidInfo edid;
	};

	struct Snapshot
	{
		std::uint64_t generation = 0;
		std::vector<MonitorInfo> monitors;
		std::size_t primaryIndex = 0;
		RECT virtualBounds{};
		bool fallback = false;

		[[nodiscard]] const MonitorInfo* Primary() const noexcept;
		[[nodiscard]] const MonitorInfo* Find(HMONITOR monitor) const noexcept;
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
		[[nodiscard]] explicit operator bool() const noexcept { return id_ != 0; }

	private:
		explicit Subscription(std::uint64_t id) noexcept : id_(id) {}
		std::uint64_t id_ = 0;
		friend Subscription Subscribe(ChangeCallback callback);
	};

	// 纯解析入口同时供无窗口测试与以后物理尺寸业务复用，不包含阈值策略。
	[[nodiscard]] EdidInfo ParseEdid(
		std::span<const std::uint8_t> bytes,
		std::wstring_view deviceId = {});
	[[nodiscard]] EdidInfo OrientEdid(EdidInfo edid, DWORD orientation) noexcept;

	[[nodiscard]] bool Initialize();
	[[nodiscard]] bool Refresh(ChangeReason reason = ChangeReason::Manual);
	void Shutdown() noexcept;
	[[nodiscard]] SnapshotPtr GetSnapshot() noexcept;
	[[nodiscard]] Subscription Subscribe(ChangeCallback callback);
	[[nodiscard]] WNDPROC WindowProc() noexcept;
}
