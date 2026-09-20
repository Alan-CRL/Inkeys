module;

#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Inkeys.Display;

namespace
{
	using namespace Inkeys::Display;

	constexpr GUID MonitorInterfaceGuid{
		0xe6f07b5f, 0xee97, 0x4a90,
		{ 0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7 } };

	struct Subscriber
	{
		ChangeCallback callback;
		std::uint64_t lastGeneration = 0;
		std::size_t activeCalls = 0;
		bool removing = false;
		std::condition_variable drained;
	};

	struct Publication
	{
		SnapshotPtr snapshot;
		std::shared_ptr<Subscriber> target;
	};

	class DeviceInfoSet final
	{
	public:
		explicit DeviceInfoSet(HDEVINFO value) noexcept : value_(value) {}
		~DeviceInfoSet()
		{
			if (value_ != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value_);
		}
		DeviceInfoSet(const DeviceInfoSet&) = delete;
		DeviceInfoSet& operator=(const DeviceInfoSet&) = delete;
		[[nodiscard]] HDEVINFO Get() const noexcept { return value_; }
		[[nodiscard]] explicit operator bool() const noexcept
		{
			return value_ != INVALID_HANDLE_VALUE;
		}

	private:
		HDEVINFO value_ = INVALID_HANDLE_VALUE;
	};

	class RegistryKey final
	{
	public:
		explicit RegistryKey(HKEY value) noexcept : value_(value) {}
		~RegistryKey()
		{
			if (value_ && value_ != INVALID_HANDLE_VALUE) RegCloseKey(value_);
		}
		RegistryKey(const RegistryKey&) = delete;
		RegistryKey& operator=(const RegistryKey&) = delete;
		[[nodiscard]] HKEY Get() const noexcept { return value_; }
		[[nodiscard]] explicit operator bool() const noexcept
		{
			return value_ && value_ != INVALID_HANDLE_VALUE;
		}

	private:
		HKEY value_ = nullptr;
	};

	std::mutex refreshMutex;
	std::mutex subscriberMutex;
	std::mutex publicationMutex;
	std::atomic<SnapshotPtr> currentSnapshot;
	std::vector<std::shared_ptr<Subscriber>> subscribers;
	std::deque<Publication> pendingPublications;
	bool publicationDrainActive = false;
	std::uint64_t nextGeneration = 1;
	bool shuttingDown = false;
	thread_local Subscriber* executingSubscriber = nullptr;

	[[nodiscard]] bool EqualLuid(const LUID& left, const LUID& right) noexcept
	{
		return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
	}

	[[nodiscard]] bool EqualRectValue(const RECT& left, const RECT& right) noexcept
	{
		return left.left == right.left && left.top == right.top &&
			left.right == right.right && left.bottom == right.bottom;
	}

	[[nodiscard]] bool EqualEdid(const EdidInfo& left, const EdidInfo& right)
	{
		return left.valid == right.valid && left.status == right.status &&
			left.majorVersion == right.majorVersion &&
			left.minorVersion == right.minorVersion &&
			left.devicePath == right.devicePath && left.deviceId == right.deviceId &&
			left.rawBytes == right.rawBytes &&
			left.rawPhysicalWidthCm == right.rawPhysicalWidthCm &&
			left.rawPhysicalHeightCm == right.rawPhysicalHeightCm;
	}

	[[nodiscard]] bool EqualPhysicalSize(
		const PhysicalSizeInfo& left, const PhysicalSizeInfo& right) noexcept
	{
		return left.available == right.available && left.widthCm == right.widthCm &&
			left.heightCm == right.heightCm &&
			left.unavailableReason == right.unavailableReason;
	}

	[[nodiscard]] bool EqualTarget(
		const ActiveDisplayTargetInfo& left, const ActiveDisplayTargetInfo& right)
	{
		return EqualLuid(left.sourceAdapterId, right.sourceAdapterId) &&
			left.sourceId == right.sourceId &&
			EqualLuid(left.targetAdapterId, right.targetAdapterId) &&
			left.targetId == right.targetId &&
			left.sourceDeviceName == right.sourceDeviceName &&
			left.monitorDevicePath == right.monitorDevicePath &&
			left.monitorFriendlyName == right.monitorFriendlyName &&
			EqualEdid(left.edid, right.edid);
	}

	[[nodiscard]] bool EqualMonitor(const MonitorInfo& left, const MonitorInfo& right)
	{
		return left.handle == right.handle && left.deviceName == right.deviceName &&
			EqualRectValue(left.bounds, right.bounds) &&
			EqualRectValue(left.workArea, right.workArea) &&
			left.pixelWidth == right.pixelWidth &&
			left.pixelHeight == right.pixelHeight &&
			left.effectiveDpiX == right.effectiveDpiX &&
			left.effectiveDpiY == right.effectiveDpiY &&
			left.orientation == right.orientation &&
			left.primary == right.primary && left.fallback == right.fallback &&
			left.targetIndex == right.targetIndex && EqualEdid(left.edid, right.edid) &&
			EqualPhysicalSize(left.physicalSize, right.physicalSize);
	}

	[[nodiscard]] bool EqualSnapshot(const Snapshot& left, const Snapshot& right)
	{
		if (left.primaryIndex != right.primaryIndex || left.fallback != right.fallback ||
			left.topology != right.topology ||
			!EqualRectValue(left.virtualBounds, right.virtualBounds) ||
			left.monitors.size() != right.monitors.size() ||
			left.activeTargets.size() != right.activeTargets.size()) return false;
		for (std::size_t index = 0; index < left.monitors.size(); ++index)
			if (!EqualMonitor(left.monitors[index], right.monitors[index])) return false;
		for (std::size_t index = 0; index < left.activeTargets.size(); ++index)
			if (!EqualTarget(left.activeTargets[index], right.activeTargets[index])) return false;
		return true;
	}

	[[nodiscard]] bool EqualDeviceName(
		std::wstring_view left, std::wstring_view right)
	{
		if (left.size() != right.size()) return false;
		return _wcsnicmp(left.data(), right.data(), left.size()) == 0;
	}

	[[nodiscard]] EdidInfo ReadMonitorEdid(std::wstring_view monitorDevicePath)
	{
		EdidInfo result;
		result.devicePath.assign(monitorDevicePath);
		if (monitorDevicePath.empty()) return result;

		const DeviceInfoSet devices(SetupDiGetClassDevsW(&MonitorInterfaceGuid,
			nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
		if (!devices)
		{
			result.status = EdidStatus::ReadFailed;
			return result;
		}

		const std::wstring requestedPath(monitorDevicePath);
		for (DWORD index = 0;; ++index)
		{
			SP_DEVICE_INTERFACE_DATA interfaceData{};
			interfaceData.cbSize = sizeof(interfaceData);
			if (!SetupDiEnumDeviceInterfaces(devices.Get(), nullptr,
				&MonitorInterfaceGuid, index, &interfaceData)) break;

			DWORD requiredBytes = 0;
			(void)SetupDiGetDeviceInterfaceDetailW(devices.Get(), &interfaceData,
				nullptr, 0, &requiredBytes, nullptr);
			if (requiredBytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
			std::vector<std::byte> detailBytes(requiredBytes);
			auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
				detailBytes.data());
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			SP_DEVINFO_DATA deviceData{};
			deviceData.cbSize = sizeof(deviceData);
			if (!SetupDiGetDeviceInterfaceDetailW(devices.Get(), &interfaceData,
				detail, requiredBytes, nullptr, &deviceData)) continue;
			if (_wcsicmp(detail->DevicePath, requestedPath.c_str()) != 0) continue;

			DWORD requiredCharacters = 0;
			(void)SetupDiGetDeviceInstanceIdW(devices.Get(), &deviceData,
				nullptr, 0, &requiredCharacters);
			if (requiredCharacters > 0)
			{
				std::vector<wchar_t> instanceId(requiredCharacters);
				if (SetupDiGetDeviceInstanceIdW(devices.Get(), &deviceData,
					instanceId.data(), requiredCharacters, nullptr))
					result.deviceId.assign(instanceId.data());
			}

			const RegistryKey key(SetupDiOpenDevRegKey(devices.Get(), &deviceData,
				DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ));
			if (!key)
			{
				result.status = EdidStatus::ReadFailed;
				return result;
			}

			DWORD type = 0;
			DWORD size = 0;
			if (RegQueryValueExW(key.Get(), L"EDID", nullptr, &type, nullptr, &size) !=
				ERROR_SUCCESS || type != REG_BINARY || size == 0)
			{
				result.status = EdidStatus::ReadFailed;
				return result;
			}
			std::vector<std::uint8_t> bytes(size);
			if (RegQueryValueExW(key.Get(), L"EDID", nullptr, &type,
				bytes.data(), &size) != ERROR_SUCCESS)
			{
				result.status = EdidStatus::ReadFailed;
				return result;
			}
			bytes.resize(size);
			auto parsed = ParseEdid(bytes, result.deviceId);
			parsed.devicePath = requestedPath;
			return parsed;
		}
		return result;
	}

	[[nodiscard]] std::optional<std::vector<ActiveDisplayTargetInfo>>
		QueryActiveDisplayTargets()
	{
		constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
		std::vector<DISPLAYCONFIG_PATH_INFO> paths;
		std::vector<DISPLAYCONFIG_MODE_INFO> modes;
		bool queried = false;
		// 拓扑切换时容量可能变化；每次不足都重新获取容量，避免拼出跨代路径。
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			UINT32 pathCount = 0;
			UINT32 modeCount = 0;
			if (GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount) != ERROR_SUCCESS)
				return std::nullopt;
			paths.assign(pathCount, {});
			modes.assign(modeCount, {});
			const LONG queryResult = QueryDisplayConfig(flags, &pathCount, paths.data(),
				&modeCount, modes.data(), nullptr);
			if (queryResult == ERROR_INSUFFICIENT_BUFFER) continue;
			if (queryResult != ERROR_SUCCESS) return std::nullopt;
			paths.resize(pathCount);
			queried = true;
			break;
		}
		if (!queried) return std::nullopt;

		std::vector<ActiveDisplayTargetInfo> targets;
		targets.reserve(paths.size());
		for (const auto& path : paths)
		{
			ActiveDisplayTargetInfo target;
			target.sourceAdapterId = path.sourceInfo.adapterId;
			target.sourceId = path.sourceInfo.id;
			target.targetAdapterId = path.targetInfo.adapterId;
			target.targetId = path.targetInfo.id;

			DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
			sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			sourceName.header.size = sizeof(sourceName);
			sourceName.header.adapterId = path.sourceInfo.adapterId;
			sourceName.header.id = path.sourceInfo.id;
			if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS)
				target.sourceDeviceName = sourceName.viewGdiDeviceName;

			DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
			targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			targetName.header.size = sizeof(targetName);
			targetName.header.adapterId = path.targetInfo.adapterId;
			targetName.header.id = path.targetInfo.id;
			if (DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS)
			{
				target.monitorDevicePath = targetName.monitorDevicePath;
				target.monitorFriendlyName = targetName.monitorFriendlyDeviceName;
				target.edid = ReadMonitorEdid(target.monitorDevicePath);
			}
			targets.push_back(std::move(target));
		}
		return targets;
	}

	[[nodiscard]] std::pair<UINT, UINT> QueryMonitorDpi(
		HMONITOR monitor, const wchar_t* deviceName) noexcept
	{
		using GetDpiForMonitorFunction = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
		static HMODULE shcore = LoadLibraryW(L"Shcore.dll");
		static auto getDpiForMonitor = shcore
			? reinterpret_cast<GetDpiForMonitorFunction>(
				GetProcAddress(shcore, "GetDpiForMonitor"))
			: nullptr;
		UINT dpiX = USER_DEFAULT_SCREEN_DPI;
		UINT dpiY = USER_DEFAULT_SCREEN_DPI;
		if (getDpiForMonitor && SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)) &&
			dpiX > 0 && dpiY > 0) return { dpiX, dpiY };

		HDC dc = CreateDCW(L"DISPLAY", deviceName, nullptr, nullptr);
		if (dc)
		{
			const int x = GetDeviceCaps(dc, LOGPIXELSX);
			const int y = GetDeviceCaps(dc, LOGPIXELSY);
			DeleteDC(dc);
			if (x > 0) dpiX = static_cast<UINT>(x);
			if (y > 0) dpiY = static_cast<UINT>(y);
		}
		return { dpiX, dpiY };
	}

	struct EnumContext
	{
		std::vector<std::pair<HMONITOR, MONITORINFOEXW>> monitors;
	};

	BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
	{
		auto& context = *reinterpret_cast<EnumContext*>(parameter);
		MONITORINFOEXW information{};
		information.cbSize = sizeof(information);
		if (GetMonitorInfoW(monitor, &information))
			context.monitors.emplace_back(monitor, information);
		return TRUE;
	}

	[[nodiscard]] std::optional<Snapshot> EnumerateSnapshot()
	{
		EnumContext context;
		if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
			reinterpret_cast<LPARAM>(&context)) || context.monitors.empty())
			return std::nullopt;

		Snapshot snapshot;
		snapshot.monitors.reserve(context.monitors.size());
		for (const auto& [handle, native] : context.monitors)
		{
			MonitorInfo monitor;
			monitor.handle = handle;
			monitor.deviceName = native.szDevice;
			monitor.bounds = native.rcMonitor;
			monitor.workArea = native.rcWork;
			monitor.pixelWidth = native.rcMonitor.right - native.rcMonitor.left;
			monitor.pixelHeight = native.rcMonitor.bottom - native.rcMonitor.top;
			monitor.primary = (native.dwFlags & MONITORINFOF_PRIMARY) != 0;
			const auto [dpiX, dpiY] = QueryMonitorDpi(handle, native.szDevice);
			monitor.effectiveDpiX = dpiX;
			monitor.effectiveDpiY = dpiY;

			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			if (EnumDisplaySettingsW(native.szDevice, ENUM_CURRENT_SETTINGS, &mode))
				monitor.orientation = mode.dmDisplayOrientation;
			snapshot.monitors.push_back(std::move(monitor));
		}

		if (auto targets = QueryActiveDisplayTargets())
		{
			snapshot.activeTargets = std::move(*targets);
			snapshot.topology = ClassifyTopology(snapshot.activeTargets);
		}

		std::vector<std::vector<std::size_t>> monitorTargets(snapshot.monitors.size());
		std::vector<std::size_t> targetMonitorCounts(snapshot.activeTargets.size());
		for (std::size_t targetIndex = 0;
			targetIndex < snapshot.activeTargets.size(); ++targetIndex)
		{
			const auto& target = snapshot.activeTargets[targetIndex];
			if (target.sourceDeviceName.empty()) continue;
			for (std::size_t monitorIndex = 0;
				monitorIndex < snapshot.monitors.size(); ++monitorIndex)
			{
				if (!EqualDeviceName(snapshot.monitors[monitorIndex].deviceName,
					target.sourceDeviceName)) continue;
				monitorTargets[monitorIndex].push_back(targetIndex);
				++targetMonitorCounts[targetIndex];
			}
		}

		bool mappingSetReliable =
			(snapshot.topology == DisplayTopology::Single ||
				snapshot.topology == DisplayTopology::Extended) &&
			snapshot.monitors.size() == snapshot.activeTargets.size();
		for (const auto count : targetMonitorCounts)
			mappingSetReliable = mappingSetReliable && count == 1;
		for (const auto& indices : monitorTargets)
			mappingSetReliable = mappingSetReliable && indices.size() == 1;

		for (std::size_t index = 0; index < snapshot.monitors.size(); ++index)
		{
			auto& monitor = snapshot.monitors[index];
			if (monitorTargets[index].size() == 1)
			{
				monitor.targetIndex = monitorTargets[index].front();
				monitor.edid = snapshot.activeTargets[*monitor.targetIndex].edid;
			}
			monitor.physicalSize = ResolvePhysicalSize(monitor.edid,
				monitor.orientation, snapshot.topology, mappingSetReliable);
		}

		auto primary = std::find_if(snapshot.monitors.begin(), snapshot.monitors.end(),
			[](const MonitorInfo& monitor) { return monitor.primary; });
		snapshot.primaryIndex = primary == snapshot.monitors.end() ? 0 :
			static_cast<std::size_t>(primary - snapshot.monitors.begin());
		if (primary == snapshot.monitors.end()) snapshot.monitors.front().primary = true;
		snapshot.virtualBounds = snapshot.monitors.front().bounds;
		for (const auto& monitor : snapshot.monitors)
		{
			snapshot.virtualBounds.left =
				(std::min)(snapshot.virtualBounds.left, monitor.bounds.left);
			snapshot.virtualBounds.top =
				(std::min)(snapshot.virtualBounds.top, monitor.bounds.top);
			snapshot.virtualBounds.right =
				(std::max)(snapshot.virtualBounds.right, monitor.bounds.right);
			snapshot.virtualBounds.bottom =
				(std::max)(snapshot.virtualBounds.bottom, monitor.bounds.bottom);
		}
		return snapshot;
	}

	[[nodiscard]] Snapshot MakeFallbackSnapshot()
	{
		Snapshot snapshot;
		snapshot.fallback = true;
		MonitorInfo monitor;
		monitor.handle = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
		monitor.bounds = { 0, 0, (std::max)(1, GetSystemMetrics(SM_CXSCREEN)),
			(std::max)(1, GetSystemMetrics(SM_CYSCREEN)) };
		monitor.workArea = monitor.bounds;
		monitor.pixelWidth = monitor.bounds.right;
		monitor.pixelHeight = monitor.bounds.bottom;
		monitor.primary = true;
		monitor.fallback = true;
		monitor.physicalSize = ResolvePhysicalSize(monitor.edid,
			monitor.orientation, DisplayTopology::Unknown, false, true);
		snapshot.virtualBounds = monitor.bounds;
		snapshot.monitors.push_back(std::move(monitor));
		return snapshot;
	}

	[[nodiscard]] Snapshot InvalidatePhysicalSize(const Snapshot& previous)
	{
		Snapshot snapshot = previous;
		if (snapshot.fallback) return snapshot;
		snapshot.topology = DisplayTopology::Unknown;
		for (auto& monitor : snapshot.monitors)
			monitor.physicalSize = ResolvePhysicalSize(monitor.edid,
				monitor.orientation, DisplayTopology::Unknown,
				monitor.targetIndex.has_value());
		return snapshot;
	}

	void InvokeSubscriber(const std::shared_ptr<Subscriber>& subscriber,
		const SnapshotPtr& snapshot)
	{
		{
			std::scoped_lock lock(subscriberMutex);
			if (subscriber->removing || !snapshot ||
				snapshot->generation <= subscriber->lastGeneration) return;
			// 先登记代次再离锁调用，嵌套订阅也不会重入或倒序收到快照。
			subscriber->lastGeneration = snapshot->generation;
			++subscriber->activeCalls;
		}
		auto* previousExecuting = executingSubscriber;
		executingSubscriber = subscriber.get();
		try { subscriber->callback(snapshot); }
		catch (...) {}
		executingSubscriber = previousExecuting;
		std::scoped_lock lock(subscriberMutex);
		if (--subscriber->activeCalls == 0) subscriber->drained.notify_all();
	}

	void PublishCallbacks(const SnapshotPtr& snapshot)
	{
		std::vector<std::shared_ptr<Subscriber>> callbacks;
		{
			std::scoped_lock lock(subscriberMutex);
			callbacks = subscribers;
		}
		for (const auto& subscriber : callbacks)
			InvokeSubscriber(subscriber, snapshot);
	}

	[[nodiscard]] bool QueuePublicationLocked(const SnapshotPtr& snapshot,
		std::shared_ptr<Subscriber> target = {})
	{
		pendingPublications.push_back({ snapshot, std::move(target) });
		if (publicationDrainActive) return false;
		publicationDrainActive = true;
		return true;
	}

	void DrainPublications()
	{
		for (;;)
		{
			Publication publication;
			{
				std::scoped_lock lock(publicationMutex);
				if (pendingPublications.empty())
				{
					publicationDrainActive = false;
					return;
				}
				publication = std::move(pendingPublications.front());
				pendingPublications.pop_front();
			}
			if (publication.target)
				InvokeSubscriber(publication.target, publication.snapshot);
			else
				PublishCallbacks(publication.snapshot);
		}
	}

	void Unsubscribe(const std::shared_ptr<void>& opaqueState) noexcept
	{
		if (!opaqueState) return;
		const auto subscriber = std::static_pointer_cast<Subscriber>(opaqueState);
		std::unique_lock lock(subscriberMutex);
		const auto iterator = std::find_if(subscribers.begin(), subscribers.end(),
			[&subscriber](const auto& value) { return value == subscriber; });
		if (iterator != subscribers.end()) subscribers.erase(iterator);
		subscriber->removing = true;
		if (executingSubscriber != subscriber.get())
			subscriber->drained.wait(lock,
				[&subscriber] { return subscriber->activeCalls == 0; });
	}

	LRESULT CALLBACK DisplayWindowProc(HWND hwnd, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_DISPLAYCHANGE:
			(void)Refresh(ChangeReason::Display);
			break;
		case WM_DEVICECHANGE:
			(void)Refresh(ChangeReason::Device);
			break;
		case WM_SETTINGCHANGE:
			(void)Refresh(ChangeReason::Settings);
			break;
		default:
			break;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

namespace Inkeys::Display
{
	std::wstring EdidInfo::VersionText() const
	{
		return valid ? std::to_wstring(majorVersion) + L"." +
			std::to_wstring(minorVersion) : std::wstring{};
	}

	const MonitorInfo* Snapshot::Primary() const noexcept
	{
		return primaryIndex < monitors.size() ? &monitors[primaryIndex] : nullptr;
	}

	const MonitorInfo* Snapshot::Find(HMONITOR monitor) const noexcept
	{
		const auto iterator = std::find_if(monitors.begin(), monitors.end(),
			[monitor](const MonitorInfo& value) { return value.handle == monitor; });
		return iterator == monitors.end() ? nullptr : &*iterator;
	}

	bool Snapshot::SemanticallyEquals(const Snapshot& other) const
	{
		return EqualSnapshot(*this, other);
	}

	EdidInfo ParseEdid(std::span<const std::uint8_t> bytes,
		std::wstring_view deviceId)
	{
		EdidInfo result;
		result.status = EdidStatus::ParseFailed;
		result.deviceId.assign(deviceId);
		result.rawBytes.assign(bytes.begin(), bytes.end());
		constexpr std::array<std::uint8_t, 8> header{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
		if (bytes.size() < 128 ||
			!std::equal(header.begin(), header.end(), bytes.begin())) return result;
		const auto checksum = std::accumulate(bytes.begin(), bytes.begin() + 128, 0u);
		if ((checksum & 0xFFu) != 0) return result;
		result.majorVersion = bytes[18];
		result.minorVersion = bytes[19];
		result.rawPhysicalWidthCm = bytes[21];
		result.rawPhysicalHeightCm = bytes[22];
		result.status = EdidStatus::Parsed;
		result.valid = true;
		return result;
	}

	DisplayTopology ClassifyTopology(
		std::span<const ActiveDisplayTargetInfo> targets) noexcept
	{
		if (targets.empty()) return DisplayTopology::Unknown;
		if (targets.size() == 1) return DisplayTopology::Single;
		for (std::size_t left = 0; left < targets.size(); ++left)
			for (std::size_t right = left + 1; right < targets.size(); ++right)
				if (EqualLuid(targets[left].sourceAdapterId,
					targets[right].sourceAdapterId) &&
					targets[left].sourceId == targets[right].sourceId)
					return DisplayTopology::CloneOrMixed;
		return DisplayTopology::Extended;
	}

	PhysicalSizeInfo ResolvePhysicalSize(const EdidInfo& edid, DWORD orientation,
		DisplayTopology topology, bool uniqueTarget, bool fallback) noexcept
	{
		PhysicalSizeInfo result;
		auto reject = [&result](PhysicalSizeUnavailableReason reason)
		{
			result.unavailableReason = reason;
			return result;
		};
		if (fallback) return reject(PhysicalSizeUnavailableReason::SnapshotFallback);
		if (topology == DisplayTopology::Unknown)
			return reject(PhysicalSizeUnavailableReason::TopologyUnknown);
		if (topology == DisplayTopology::CloneOrMixed)
			return reject(PhysicalSizeUnavailableReason::CloneOrMixed);
		if (!uniqueTarget)
			return reject(PhysicalSizeUnavailableReason::DisplayTargetAmbiguous);
		switch (edid.status)
		{
		case EdidStatus::Unavailable:
			return reject(PhysicalSizeUnavailableReason::EdidUnavailable);
		case EdidStatus::ReadFailed:
			return reject(PhysicalSizeUnavailableReason::EdidReadFailed);
		case EdidStatus::ParseFailed:
			return reject(PhysicalSizeUnavailableReason::EdidParseFailed);
		case EdidStatus::Parsed:
			break;
		}
		if (edid.rawPhysicalWidthCm == 0 || edid.rawPhysicalHeightCm == 0)
			return reject(PhysicalSizeUnavailableReason::MissingDimensions);
		if (edid.rawPhysicalWidthCm < 5 || edid.rawPhysicalHeightCm < 5)
			return reject(PhysicalSizeUnavailableReason::DimensionsBelowMinimum);

		result.available = true;
		result.widthCm = edid.rawPhysicalWidthCm;
		result.heightCm = edid.rawPhysicalHeightCm;
		result.unavailableReason = PhysicalSizeUnavailableReason::None;
		if (orientation == DMDO_90 || orientation == DMDO_270)
			std::swap(result.widthCm, result.heightCm);
		return result;
	}

	std::wstring_view DisplayTopologyText(DisplayTopology topology) noexcept
	{
		switch (topology)
		{
		case DisplayTopology::Single: return L"单屏";
		case DisplayTopology::Extended: return L"扩展";
		case DisplayTopology::CloneOrMixed: return L"复制或混合";
		default: return L"未知";
		}
	}

	std::wstring_view EdidStatusText(EdidStatus status) noexcept
	{
		switch (status)
		{
		case EdidStatus::ReadFailed: return L"读取失败";
		case EdidStatus::ParseFailed: return L"解析失败";
		case EdidStatus::Parsed: return L"已解析";
		default: return L"不可用";
		}
	}

	std::wstring_view PhysicalSizeUnavailableReasonText(
		PhysicalSizeUnavailableReason reason) noexcept
	{
		switch (reason)
		{
		case PhysicalSizeUnavailableReason::None: return L"可用";
		case PhysicalSizeUnavailableReason::SnapshotFallback: return L"显示枚举回退";
		case PhysicalSizeUnavailableReason::TopologyUnknown: return L"活动拓扑未知";
		case PhysicalSizeUnavailableReason::CloneOrMixed: return L"存在复制屏幕";
		case PhysicalSizeUnavailableReason::DisplayTargetAmbiguous: return L"显示器映射不唯一";
		case PhysicalSizeUnavailableReason::EdidUnavailable: return L"EDID 不可用";
		case PhysicalSizeUnavailableReason::EdidReadFailed: return L"EDID 读取失败";
		case PhysicalSizeUnavailableReason::EdidParseFailed: return L"EDID 解析失败";
		case PhysicalSizeUnavailableReason::MissingDimensions: return L"EDID 尺寸缺失";
		case PhysicalSizeUnavailableReason::DimensionsBelowMinimum: return L"EDID 尺寸过小";
		default: return L"未知原因";
		}
	}

	Subscription::~Subscription() { Reset(); }

	Subscription::Subscription(Subscription&& other) noexcept
		: state_(std::move(other.state_))
	{
	}

	Subscription& Subscription::operator=(Subscription&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			state_ = std::move(other.state_);
		}
		return *this;
	}

	void Subscription::Reset() noexcept
	{
		auto state = std::move(state_);
		Unsubscribe(state);
	}

	bool Initialize()
	{
		{
			std::scoped_lock lock(subscriberMutex);
			shuttingDown = false;
		}
		return Refresh(ChangeReason::Startup);
	}

	bool Refresh(ChangeReason)
	{
		SnapshotPtr published;
		bool enumerationSucceeded = false;
		bool drainPublications = false;
		{
			std::scoped_lock refreshLock(refreshMutex);
			{
				std::scoped_lock lock(subscriberMutex);
				if (shuttingDown) return false;
			}

			auto next = EnumerateSnapshot();
			enumerationSucceeded = next.has_value();
			const auto previous = currentSnapshot.load(std::memory_order_acquire);
			if (!next)
			{
				// 几何枚举失败仍立即撤销旧物理标尺，像素/DPI保留给兼容消费者。
				next = previous ? InvalidatePhysicalSize(*previous) : MakeFallbackSnapshot();
			}
			if (previous && previous->SemanticallyEquals(*next))
				return enumerationSucceeded;
			next->generation = nextGeneration++;
			published = std::make_shared<const Snapshot>(std::move(*next));
			{
				std::scoped_lock lock(publicationMutex);
				currentSnapshot.store(published, std::memory_order_release);
				drainPublications = QueuePublicationLocked(published);
			}
		}
		// 快照先完整发布，再在所有内部锁之外通知订阅者。
		if (drainPublications) DrainPublications();
		return enumerationSucceeded;
	}

	void Shutdown() noexcept
	{
		std::vector<std::shared_ptr<Subscriber>> removed;
		{
			std::scoped_lock refreshLock(refreshMutex);
			{
				std::scoped_lock lock(subscriberMutex);
				shuttingDown = true;
				removed.swap(subscribers);
				for (const auto& subscriber : removed) subscriber->removing = true;
			}
			{
				std::scoped_lock lock(publicationMutex);
				pendingPublications.clear();
				currentSnapshot.store(SnapshotPtr{}, std::memory_order_release);
			}
		}
		std::unique_lock lock(subscriberMutex);
		for (const auto& subscriber : removed)
		{
			if (executingSubscriber == subscriber.get()) continue;
			subscriber->drained.wait(lock,
				[&subscriber] { return subscriber->activeCalls == 0; });
		}
	}

	SnapshotPtr GetSnapshot() noexcept
	{
		return currentSnapshot.load(std::memory_order_acquire);
	}

	Subscription Subscribe(ChangeCallback callback)
	{
		if (!callback) return {};
		auto subscriber = std::make_shared<Subscriber>();
		SnapshotPtr snapshot;
		bool drainPublications = false;
		{
			std::scoped_lock lock(subscriberMutex);
			if (shuttingDown) return {};
			subscriber->callback = std::move(callback);
			subscribers.push_back(subscriber);
		}
		{
			std::scoped_lock lock(publicationMutex);
			snapshot = currentSnapshot.load(std::memory_order_acquire);
			// 首次通知与刷新共用队列，保证每个订阅者看到的 generation 单调且不重复。
			if (snapshot)
				drainPublications = QueuePublicationLocked(snapshot, subscriber);
		}
		if (drainPublications) DrainPublications();
		return Subscription(subscriber);
	}

	WNDPROC WindowProc() noexcept { return DisplayWindowProc; }
}
