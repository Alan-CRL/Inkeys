module;

#include <windows.h>
#include <setupapi.h>
#include <shellapi.h>
#include <cmath>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Inkeys.Display;

#pragma comment(lib, "shell32.lib")

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
			left.taskbarThicknessPx == right.taskbarThicknessPx &&
			left.taskbar.edge == right.taskbar.edge && left.taskbar.autoHide == right.taskbar.autoHide &&
			left.taskbar.exposure == right.taskbar.exposure &&
			left.taskbar.lastKnownExposure == right.taskbar.lastKnownExposure &&
			left.taskbar.dockTopPx == right.taskbar.dockTopPx &&
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
		static HMODULE shcore = []() noexcept -> HMODULE
			{
				wchar_t path[MAX_PATH] = {};
				const UINT length = GetSystemDirectoryW(path, MAX_PATH);
				constexpr wchar_t suffix[] = L"\\Shcore.dll";
				if (length == 0 || length >= MAX_PATH ||
					length + ARRAYSIZE(suffix) > MAX_PATH) return nullptr;
				// Win7 缺失时走下方 GDI 回退，绝不搜索可写的应用目录。
				std::copy_n(suffix, ARRAYSIZE(suffix), path + length);
				return LoadLibraryW(path);
			}();
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

	bool IsShellTaskbar(HWND hwnd) noexcept
	{
		wchar_t name[64]{};
		return GetClassNameW(hwnd, name, 64) && (!wcscmp(name, L"Shell_TrayWnd")
			|| !wcscmp(name, L"Shell_SecondaryTrayWnd"));
	}

	struct TaskbarTarget
	{
		HWND hwnd = nullptr;
		HMONITOR monitor = nullptr;
		RECT bounds{};
		UINT edge = ABE_BOTTOM;
		bool autoHide = false;
		int thickness = 0;
		DWORD processId = 0;
		bool authoritativeThickness = false;
		int completeSampleThickness = 0;
	};

	// 此缓存由 refreshMutex 保护；快路径只读缓存 HWND 的矩形，不枚举 Shell/EDID。
	std::vector<TaskbarTarget> taskbarTargets;
	constexpr UINT syncTaskbarObserver = WM_APP + 0x354;
	constexpr UINT taskbarEventPending = WM_APP + 0x355;
	constexpr UINT stopTaskbarObserver = WM_APP + 0x356;
	std::atomic<HWND> observerWindow{ nullptr };
	std::atomic<ULONG_PTR> observerEpoch{ 0 };
	std::atomic<ULONG_PTR> nextObserverEpoch{ 1 };

	bool SameMonitorGeometry(const MonitorInfo& value, const MonitorInfo* old) noexcept
	{
		return old && value.deviceName == old->deviceName && EqualRectValue(value.bounds, old->bounds)
			&& value.effectiveDpiX == old->effectiveDpiX && value.effectiveDpiY == old->effectiveDpiY;
	}

	TaskbarEdge TaskbarEdgeFromNative(UINT edge) noexcept
	{
		switch (edge)
		{
		case ABE_LEFT: return TaskbarEdge::Left;
		case ABE_TOP: return TaskbarEdge::Top;
		case ABE_RIGHT: return TaskbarEdge::Right;
		case ABE_BOTTOM: return TaskbarEdge::Bottom;
		default: return TaskbarEdge::Unknown;
		}
	}

	void ApplyObservedTaskbar(MonitorInfo& monitor, const TaskbarTarget& target,
		const MonitorInfo* previous, const RECT& rect, bool queried, bool visible)
	{
		const auto edge = TaskbarEdgeFromNative(target.edge);
		const bool same = SameMonitorGeometry(monitor, previous) && previous->taskbar.edge == edge
			&& previous->taskbar.autoHide == target.autoHide;
		monitor.taskbarThicknessPx = target.thickness;
		monitor.taskbar = { edge, target.autoHide };
		if (same) monitor.taskbar.lastKnownExposure = previous->taskbar.lastKnownExposure;
		monitor.taskbar.dockTopPx = monitor.bounds.bottom - target.thickness;
		if (target.edge != ABE_BOTTOM || !target.autoHide)
			monitor.taskbar.exposure = TaskbarExposure::Raised;
		else if (queried)
			monitor.taskbar.exposure = ResolveTaskbarExposure(monitor.bounds, rect, target.thickness,
				monitor.effectiveDpiY, monitor.taskbar.lastKnownExposure, visible);
		if (monitor.taskbar.exposure != TaskbarExposure::Unknown)
			monitor.taskbar.lastKnownExposure = monitor.taskbar.exposure;
	}

	const TaskbarTarget* FindPreviousTaskbarTarget(const Snapshot& snapshot, const Snapshot* previous,
		std::span<const TaskbarTarget> targets, HWND hwnd)
	{
		if (!hwnd || !previous || targets.empty()) return {};
		DWORD processId = 0;
		GetWindowThreadProcessId(hwnd, &processId);
		if (!processId) return {};
		for (const auto& target : targets)
		{
			if (target.hwnd != hwnd || target.processId != processId) continue;
			const auto* monitor = snapshot.Find(target.monitor);
			// 共享屏边的隐藏矩形无法独自消歧，只接受同窗口、进程和显示几何的历史。
			if (monitor && EqualRectValue(target.bounds, monitor->bounds)
				&& SameMonitorGeometry(*monitor, previous->Find(target.monitor)))
				return &target;
		}
		return {};
	}

	TaskbarOwnerInfo TaskbarOwnerHint(const TaskbarTarget* target, bool autoHide) noexcept
	{
		return target && target->autoHide == autoHide ? TaskbarOwnerInfo{ target->monitor, target->edge }
			: TaskbarOwnerInfo{};
	}

	void SampleTaskbars(Snapshot& snapshot, const Snapshot* previous = nullptr,
		std::vector<TaskbarTarget>* targets = nullptr)
	{
		struct Context
		{
			Snapshot* snapshot;
			const Snapshot* previous;
			std::span<const TaskbarTarget> previousTargets;
			std::vector<TaskbarTarget> owners;
			bool globalAutoHide = false;
			bool queryFailed = false;
		};
		const auto previousTargets = targets ? *targets : std::vector<TaskbarTarget>{};
		Context context{ &snapshot, previous, previousTargets };
		APPBARDATA data{};
		data.cbSize = sizeof(data);
		context.globalAutoHide = (SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0;
		for (auto& monitor : snapshot.monitors)
		{
			monitor.taskbarThicknessPx = 0;
			monitor.taskbar = {};
			for (UINT edge = ABE_LEFT; edge <= ABE_BOTTOM; ++edge)
			{
				data = {};
				data.cbSize = sizeof(data);
				data.rc = monitor.bounds;
				data.uEdge = edge;
				// EX 给出真正的逐屏 owner；旧 Shell 的主屏按旧接口降级。
				HWND hwnd = reinterpret_cast<HWND>(SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &data));
				if (!hwnd && monitor.primary)
				{
					const HWND legacy = reinterpret_cast<HWND>(SHAppBarMessage(ABM_GETAUTOHIDEBAR, &data));
					RECT rect{};
					// 旧接口是全局 owner，主任务栏也可能被用户移到副屏，不能直接归主屏。
					if (legacy && GetWindowRect(legacy, &rect)
						&& ResolveTaskbarOwnerCandidate(snapshot, rect, edge, true,
							TaskbarOwnerHint(FindPreviousTaskbarTarget(snapshot, previous, previousTargets, legacy), true)).monitor
							== monitor.handle) hwnd = legacy;
				}
				if (hwnd && IsShellTaskbar(hwnd))
					context.owners.push_back({ hwnd, monitor.handle, monitor.bounds, edge, true });
			}
		}
		data = {};
		data.cbSize = sizeof(data);
		if (SHAppBarMessage(ABM_GETTASKBARPOS, &data))
		{
			const HWND main = FindWindowW(L"Shell_TrayWnd", nullptr);
			const auto owner = std::find_if(context.owners.begin(), context.owners.end(),
				[main](const auto& value) { return value.hwnd == main; });
			const HMONITOR handle = owner != context.owners.end() ? owner->monitor
				: ResolveTaskbarOwnerCandidate(snapshot, data.rc, data.uEdge, context.globalAutoHide,
					TaskbarOwnerHint(FindPreviousTaskbarTarget(snapshot, previous, previousTargets, main), context.globalAutoHide)).monitor;
			if (const auto* monitor = snapshot.Find(handle); main && monitor)
			{
				const int thickness = ResolveTaskbarThickness(monitor->bounds, data.rc, data.uEdge);
				if (owner != context.owners.end())
				{
					owner->thickness = thickness;
					owner->authoritativeThickness = thickness > 0;
				}
				else context.owners.push_back({ main, handle, monitor->bounds, data.uEdge,
					context.globalAutoHide, thickness, 0, thickness > 0 });
			}
		}
		const bool enumerated = EnumWindows([](HWND hwnd, LPARAM value) -> BOOL
		{
			if (!IsShellTaskbar(hwnd)) return TRUE;
			auto& ctx = *reinterpret_cast<Context*>(value);
			if (std::any_of(ctx.owners.begin(), ctx.owners.end(),
				[hwnd](const auto& owner) { return owner.hwnd == hwnd; })) return TRUE;
			RECT rect{};
			if (!GetWindowRect(hwnd, &rect)) { ctx.queryFailed = true; return TRUE; }
			const auto* cached = FindPreviousTaskbarTarget(*ctx.snapshot, ctx.previous, ctx.previousTargets, hwnd);
			// GETSTATE 仅是全局/主栏提示，不能覆盖同身份副栏已知的逐屏自动隐藏模式。
			const bool autoHide = cached ? cached->autoHide : ctx.globalAutoHide;
			const auto owner = ResolveTaskbarOwnerCandidate(*ctx.snapshot, rect, std::nullopt,
				autoHide, TaskbarOwnerHint(cached, autoHide));
			if (const auto* monitor = ctx.snapshot->Find(owner.monitor))
				ctx.owners.push_back({ hwnd, owner.monitor, monitor->bounds, owner.edge, autoHide });
			else ctx.queryFailed = true;
			return TRUE;
		}, reinterpret_cast<LPARAM>(&context)) != FALSE;
		for (auto& target : context.owners)
		{
			for (auto& monitor : snapshot.monitors)
			{
				if (monitor.handle != target.monitor) continue;
				const auto* old = previous ? previous->Find(monitor.handle) : nullptr;
				RECT rect{};
				const bool queried = GetWindowRect(target.hwnd, &rect) != FALSE;
				const int rectangleThickness = queried ? ResolveTaskbarThickness(monitor.bounds, rect, target.edge) : 0;
				if (!target.thickness) target.thickness = rectangleThickness;
				const auto previousTarget = std::find_if(previousTargets.begin(), previousTargets.end(),
					[&target](const auto& value) { return value.hwnd == target.hwnd && value.monitor == target.monitor
						&& value.edge == target.edge && EqualRectValue(value.bounds, target.bounds); });
				// 副栏没有完整厚度查询接口：各方向完整对齐矩形连续两次相同才允许缩小。
				const bool fullAligned = target.edge == ABE_LEFT ? rect.left == monitor.bounds.left
					: target.edge == ABE_TOP ? rect.top == monitor.bounds.top
					: target.edge == ABE_RIGHT ? rect.right == monitor.bounds.right
					: target.edge == ABE_BOTTOM && rect.bottom == monitor.bounds.bottom;
				if (rectangleThickness > 0 && fullAligned)
					target.completeSampleThickness = rectangleThickness;
				const bool stableComplete = target.completeSampleThickness > 0 && previousTarget != previousTargets.end()
					&& previousTarget->completeSampleThickness == target.completeSampleThickness;
				if (SameMonitorGeometry(monitor, old) && old->taskbar.edge == TaskbarEdgeFromNative(target.edge)
					&& old->taskbar.autoHide == target.autoHide)
					target.thickness = ResolveTaskbarFullThickness(target.thickness, old->taskbarThicknessPx,
						target.autoHide, target.authoritativeThickness || stableComplete);

				GetWindowThreadProcessId(target.hwnd, &target.processId);
				ApplyObservedTaskbar(monitor, target, old, rect, queried, IsWindowVisible(target.hwnd) != FALSE);
			}
		}
		for (auto& monitor : snapshot.monitors)
		{
			const auto* old = previous ? previous->Find(monitor.handle) : nullptr;
			if (monitor.taskbarThicknessPx || !SameMonitorGeometry(monitor, old)) continue;
			const bool hasOwner = std::any_of(context.owners.begin(), context.owners.end(),
				[&monitor](const auto& item) { return item.monitor == monitor.handle; });
			// Explorer 重建/查询失败才缓存；明确移除的副屏任务栏不借主屏或旧屏存在证据。
			if (context.owners.empty() || !enumerated || context.queryFailed || hasOwner)
			{
				monitor.taskbarThicknessPx = old->taskbarThicknessPx;
				monitor.taskbar = old->taskbar;
				monitor.taskbar.exposure = TaskbarExposure::Unknown;
			}
		}
		if (targets) *targets = std::move(context.owners);
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

	void SampleCachedTaskbars(Snapshot& snapshot, const Snapshot& previous)
	{
		for (const auto& target : taskbarTargets)
		{
			if (!target.autoHide || target.edge != ABE_BOTTOM) continue;
			for (auto& monitor : snapshot.monitors)
			{
				if (monitor.handle != target.monitor || !EqualRectValue(monitor.bounds, target.bounds)) continue;
				RECT rect{};
				DWORD processId = 0;
				GetWindowThreadProcessId(target.hwnd, &processId);
				const bool queried = processId && processId == target.processId && GetWindowRect(target.hwnd, &rect);
				ApplyObservedTaskbar(monitor, target, previous.Find(monitor.handle), rect, queried,
					queried && IsWindowVisible(target.hwnd));
			}
		}
	}

	// 周期轮询统一每 5 秒兜底；WinEvent 的 25ms 单次合并仍及时响应。
	constexpr UINT_PTR taskbarStructureTimer = 0x4954;
	constexpr UINT_PTR taskbarCoalesceTimer = 0x4955;
	constexpr UINT_PTR taskbarFallbackTimer = 0x4956;
	struct TaskbarObserverState
	{
		HWND hwnd = nullptr;
		ULONG_PTR epoch = 0;
		std::vector<TaskbarTarget> targets;
		std::vector<std::pair<DWORD, HWINEVENTHOOK>> hooks;
		TaskbarEventBatch batch;
		bool fallbackArmed = false;
	};
	thread_local TaskbarObserverState taskbarObserver;

	void StopTaskbarObservation(TaskbarObserverState& state) noexcept
	{
		// OUTOFCONTEXT hook 必须在安装它的 owner 消息线程卸载。
		for (const auto& hook : state.hooks) UnhookWinEvent(hook.second);
		state.hooks.clear();
		KillTimer(state.hwnd, taskbarStructureTimer);
		KillTimer(state.hwnd, taskbarCoalesceTimer);
		KillTimer(state.hwnd, taskbarFallbackTimer);
		state = {};
	}

	void CALLBACK TaskbarWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
		LONG object, LONG child, DWORD, DWORD)
	{
		auto& state = taskbarObserver;
		if (!state.epoch || state.epoch != observerEpoch.load(std::memory_order_acquire)
			|| object != OBJID_WINDOW || child != CHILDID_SELF
			|| (event != EVENT_OBJECT_LOCATIONCHANGE && event != EVENT_OBJECT_SHOW
				&& event != EVENT_OBJECT_HIDE && event != EVENT_OBJECT_DESTROY)) return;
		if (std::none_of(state.hooks.begin(), state.hooks.end(),
			[hook](const auto& value) { return value.second == hook; })
			|| std::none_of(state.targets.begin(), state.targets.end(),
				[hwnd](const auto& value) { return value.hwnd == hwnd; })) return;
		// 回调只标记/投递，不查 Shell、不拿显示锁、不通知订阅者，允许系统重入。
		if (state.batch.Queue(state.epoch, event == EVENT_OBJECT_DESTROY)
			&& !PostMessageW(state.hwnd, taskbarEventPending, state.epoch, 0))
			state.batch.QueueFailed(state.epoch);
	}

	void SyncTaskbarObserver(TaskbarObserverState& state)
	{
		if (!state.epoch || state.epoch != observerEpoch.load(std::memory_order_acquire)) return;
		{
			std::scoped_lock lock(refreshMutex);
			state.targets = taskbarTargets;
		}
		std::vector<DWORD> processes;
		bool fastFallback = false;
		for (const auto& target : state.targets)
		{
			fastFallback = fastFallback || (target.autoHide && target.edge == ABE_BOTTOM && target.processId);
			if (target.processId && std::find(processes.begin(), processes.end(), target.processId) == processes.end())
				processes.push_back(target.processId);
		}
		for (auto it = state.hooks.begin(); it != state.hooks.end();)
		{
			if (std::find(processes.begin(), processes.end(), it->first) == processes.end())
			{
				UnhookWinEvent(it->second);
				it = state.hooks.erase(it);
			}
			else ++it;
		}
		for (const DWORD process : processes)
		{
			if (std::any_of(state.hooks.begin(), state.hooks.end(),
				[process](const auto& hook) { return hook.first == process; })) continue;
			const auto hook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE,
				nullptr, TaskbarWinEvent, process, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
			if (hook) state.hooks.emplace_back(process, hook);
		}
		if (fastFallback && !state.fallbackArmed)
			state.fallbackArmed = SetTimer(state.hwnd, taskbarFallbackTimer, 5000, nullptr) != 0;
		else if (!fastFallback && state.fallbackArmed)
		{
			KillTimer(state.hwnd, taskbarFallbackTimer);
			state.fallbackArmed = false;
		}
	}

	LRESULT CALLBACK DisplayWindowProc(HWND hwnd, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
		auto& state = taskbarObserver;
		if (message == WM_CREATE)
		{
			state.hwnd = hwnd;
			state.epoch = nextObserverEpoch.fetch_add(1, std::memory_order_relaxed);
			state.batch.Reset(state.epoch);
			observerEpoch.store(state.epoch, std::memory_order_release);
			observerWindow.store(hwnd, std::memory_order_release);
			SetTimer(hwnd, taskbarStructureTimer, 5000, nullptr);
			SyncTaskbarObserver(state);
		}
		else if (message == WM_DESTROY && state.hwnd == hwnd)
		{
			HWND expected = hwnd;
			if (observerWindow.compare_exchange_strong(expected, nullptr)) observerEpoch.store(0);
			StopTaskbarObservation(state);
		}
		else if (message == stopTaskbarObserver && wParam == state.epoch)
			StopTaskbarObservation(state);
		else if (state.hwnd == hwnd && state.epoch
			&& state.epoch == observerEpoch.load(std::memory_order_acquire))
		{
			if (message == syncTaskbarObserver && wParam == state.epoch) SyncTaskbarObserver(state);
			else if (message == taskbarEventPending && wParam == state.epoch)
			{
				if (state.batch.BeginCoalescing(state.epoch)
					&& !SetTimer(hwnd, taskbarCoalesceTimer, 25, nullptr))
				{
					const auto structure = state.batch.Consume(state.epoch);
					if (structure) (void)Refresh(*structure ? ChangeReason::Taskbar : ChangeReason::TaskbarVisibility);
				}
			}
			else if (message == taskbarCreated && taskbarCreated) (void)Refresh(ChangeReason::Taskbar);
			else if (message == WM_TIMER)
			{
				if (wParam == taskbarStructureTimer) (void)Refresh(ChangeReason::Taskbar);
				else if (wParam == taskbarFallbackTimer) (void)Refresh(ChangeReason::TaskbarVisibility);
				else if (wParam == taskbarCoalesceTimer)
				{
					KillTimer(hwnd, taskbarCoalesceTimer);
					const auto structure = state.batch.Consume(state.epoch);
					if (structure) (void)Refresh(*structure ? ChangeReason::Taskbar : ChangeReason::TaskbarVisibility);
				}
			}
			else if (message == WM_DISPLAYCHANGE) (void)Refresh(ChangeReason::Display);
			else if (message == WM_DEVICECHANGE) (void)Refresh(ChangeReason::Device);
			else if (message == WM_SETTINGCHANGE) (void)Refresh(ChangeReason::Settings);
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

	int ResolveTaskbarThickness(const RECT& monitor, const RECT& taskbar, UINT edge) noexcept
	{
		const long long width = static_cast<long long>(taskbar.right) - taskbar.left;
		const long long height = static_cast<long long>(taskbar.bottom) - taskbar.top;
		const long long monitorWidth = static_cast<long long>(monitor.right) - monitor.left;
		const long long monitorHeight = static_cast<long long>(monitor.bottom) - monitor.top;
		if (width <= 0 || height <= 0 || monitorWidth <= 0 || monitorHeight <= 0 || edge > ABE_BOTTOM) return 0;
		const bool vertical = edge == ABE_LEFT || edge == ABE_RIGHT;
		const auto thickness = vertical ? width : height;
		const auto length = vertical ? height : width;
		const auto monitorLength = vertical ? monitorHeight : monitorWidth;
		if (length < monitorLength / 2 || thickness < 8
			|| thickness > (std::min)(monitorWidth, monitorHeight) / 3) return 0;
		return static_cast<int>(thickness);
	}

	HMONITOR ResolveTaskbarOwner(const Snapshot& snapshot, const RECT& taskbar, UINT edge) noexcept
	{
		return ResolveTaskbarOwnerCandidate(snapshot, taskbar, edge, true).monitor;
	}

	TaskbarOwnerInfo ResolveTaskbarOwnerCandidate(const Snapshot& snapshot, const RECT& taskbar,
		std::optional<UINT> knownEdge, bool autoHide, TaskbarOwnerInfo previous) noexcept
	{
		TaskbarOwnerInfo result;
		long long best = (std::numeric_limits<long long>::max)();
		bool ambiguous = false, preferred = false;
		for (const auto& monitor : snapshot.monitors)
			for (UINT edge = ABE_LEFT; edge <= ABE_BOTTOM; ++edge)
			{
				if (knownEdge && edge != *knownEdge) continue;
				const int thickness = ResolveTaskbarThickness(monitor.bounds, taskbar, edge);
				if (!thickness) continue;
				const bool vertical = edge == ABE_LEFT || edge == ABE_RIGHT;
				const LONG start = vertical ? taskbar.top : taskbar.left;
				const LONG end = vertical ? taskbar.bottom : taskbar.right;
				const LONG screenStart = vertical ? monitor.bounds.top : monitor.bounds.left;
				const LONG screenEnd = vertical ? monitor.bounds.bottom : monitor.bounds.right;
				if ((std::min)(end, screenEnd) - static_cast<long long>((std::max)(start, screenStart))
					< (static_cast<long long>(screenEnd) - screenStart) / 2) continue;
				const long long boundary = edge == ABE_LEFT ? monitor.bounds.left
					: edge == ABE_RIGHT ? monitor.bounds.right : edge == ABE_TOP ? monitor.bounds.top : monitor.bounds.bottom;
				const long long taskbarNear = vertical ? taskbar.left : taskbar.top;
				const long long taskbarFar = vertical ? taskbar.right : taskbar.bottom;
				// 常驻栏按对应外沿归属；自动隐藏不能按交集面积，否则跨屏收起会换 owner。
				const auto distance = autoHide
					? (std::min)(std::abs(boundary - taskbarNear), std::abs(boundary - taskbarFar))
					: std::abs(boundary - (edge == ABE_LEFT || edge == ABE_TOP ? taskbarNear : taskbarFar));
				if (distance > thickness || distance > best) continue;
				const bool usePrevious = previous.monitor == monitor.handle && previous.edge == edge;
				if (distance < best || (usePrevious && !preferred))
				{
					best = distance;
					result = { monitor.handle, edge };
					ambiguous = false;
					preferred = usePrevious;
				}
				else if (!preferred) ambiguous = true;
			}
		// 两块屏的同一边界可同时表示一侧收起和另一侧升起；不由枚举顺序猜测。
		return ambiguous ? TaskbarOwnerInfo{} : result;
	}

	bool ApplyTaskbarSample(Snapshot& snapshot, HMONITOR owner,
		const RECT& taskbar, UINT edge) noexcept
	{
		for (auto& monitor : snapshot.monitors)
			if (monitor.handle == owner)
			{
				const int thickness = ResolveTaskbarThickness(monitor.bounds, taskbar, edge);
				if (!thickness) return false;
				monitor.taskbarThicknessPx = thickness;
				return true;
			}
		return false;
	}

	void TaskbarEventBatch::Reset(ULONG_PTR epoch) noexcept
	{
		epoch_ = epoch;
		messageQueued_ = timerArmed_ = structurePending_ = false;
	}

	bool TaskbarEventBatch::Queue(ULONG_PTR epoch, bool structure) noexcept
	{
		if (!epoch || epoch != epoch_) return false;
		structurePending_ = structurePending_ || structure;
		return !std::exchange(messageQueued_, true);
	}

	void TaskbarEventBatch::QueueFailed(ULONG_PTR epoch) noexcept
	{
		if (epoch && epoch == epoch_) messageQueued_ = false;
	}

	bool TaskbarEventBatch::BeginCoalescing(ULONG_PTR epoch) noexcept
	{
		if (!epoch || epoch != epoch_) return false;
		messageQueued_ = false;
		return !std::exchange(timerArmed_, true);
	}

	std::optional<bool> TaskbarEventBatch::Consume(ULONG_PTR epoch) noexcept
	{
		if (!epoch || epoch != epoch_ || !timerArmed_) return std::nullopt;
		timerArmed_ = false;
		return std::exchange(structurePending_, false);
	}

	int ResolveTaskbarFullThickness(int sample, int previous, bool autoHide, bool completeSample) noexcept
	{
		if (sample < 8) return (std::max)(0, previous);
		return !autoHide || completeSample ? sample : (std::max)(sample, previous);
	}

	TaskbarExposure ResolveTaskbarExposure(const RECT& monitor, const RECT& taskbar,
		int fullThicknessPx, UINT dpi, TaskbarExposure previous, bool visible) noexcept
	{
		if (fullThicknessPx < 8 || taskbar.right <= taskbar.left || taskbar.bottom <= taskbar.top
			|| monitor.right <= monitor.left || monitor.bottom <= monitor.top
			|| (std::min)(taskbar.right, monitor.right) <= (std::max)(taskbar.left, monitor.left))
			return TaskbarExposure::Unknown;
		if (!visible) return TaskbarExposure::Hidden;
		const long long exposed = (std::max)(0LL,
			static_cast<long long>((std::min)(monitor.bottom, taskbar.bottom)) - (std::max)(monitor.top, taskbar.top));
		const int strip = (std::min)(fullThicknessPx / 4,
			(std::max)(2, static_cast<int>(std::lround(3.0 * (dpi ? dpi : 96) / 96))));
		// 隐藏触发条不算升起；中段沿用上一端点，避免 Shell 动画逐像素重定向。
		if (exposed <= strip) return TaskbarExposure::Hidden;
		if (exposed >= (std::max)(strip + 1, fullThicknessPx / 2)) return TaskbarExposure::Raised;
		return previous;
	}

	double ResolveDesktopBottomDockLine(const MonitorInfo& monitor) noexcept
	{
		const auto& taskbar = monitor.taskbar;
		// 顶部/侧边和底部收起共用真实屏幕 5DIP 间隙，不使用 UI 缩放倍率。
		const double screenInsetDockLine = monitor.bounds.bottom
			- std::lround(5.0 * (monitor.effectiveDpiY ? monitor.effectiveDpiY : 96) / 96);
		if (taskbar.edge == TaskbarEdge::Left || taskbar.edge == TaskbarEdge::Top
			|| taskbar.edge == TaskbarEdge::Right) return screenInsetDockLine;
		if (taskbar.edge == TaskbarEdge::Bottom)
		{
			const auto exposure = taskbar.exposure == TaskbarExposure::Unknown
				? taskbar.lastKnownExposure : taskbar.exposure;
			if (taskbar.autoHide && exposure == TaskbarExposure::Hidden)
				return screenInsetDockLine;
			if ((!taskbar.autoHide || exposure == TaskbarExposure::Raised)
				&& taskbar.dockTopPx > monitor.bounds.top && taskbar.dockTopPx < monitor.bounds.bottom)
				return taskbar.dockTopPx;
		}
		const auto& work = monitor.workArea;
		return work.right > work.left && work.bottom > work.top
			&& work.bottom > monitor.bounds.top && work.bottom < monitor.bounds.bottom
			? work.bottom : monitor.bounds.bottom;
	}

	bool ShouldProtectTaskbarBottomEdge(const MonitorInfo& monitor) noexcept
	{
		return monitor.taskbar.edge == TaskbarEdge::Bottom && monitor.taskbar.autoHide;
	}

	double ResolveUiScaleBase(UINT dpi, int thickness, UINT primaryDpi,
		int primaryThickness, bool useTaskbar, double baseHeightDip) noexcept
	{
		const double dpiScale = static_cast<double>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI;
		if (!useTaskbar || !std::isfinite(baseHeightDip) || baseHeightDip <= 0.0) return dpiScale;
		if (thickness > 0) return thickness / baseHeightDip;
		if (primaryThickness > 0 && primaryDpi > 0)
			return primaryThickness / baseHeightDip * (dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / primaryDpi;
		return dpiScale;
	}

	double ResolveUiScaleBase(const Snapshot& snapshot, const MonitorInfo& monitor,
		bool useTaskbar, double baseHeightDip) noexcept
	{
		const auto* primary = snapshot.Primary();
		return ResolveUiScaleBase(monitor.effectiveDpiX, monitor.taskbarThicknessPx,
			primary ? primary->effectiveDpiX : 0, primary ? primary->taskbarThicknessPx : 0,
			useTaskbar, baseHeightDip);
	}

	double QueryUiScaleBase(HMONITOR handle, bool useTaskbar, double baseHeightDip) noexcept
	{
		Snapshot mini;
		EnumContext context;
		EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&context));
		for (const auto& [monitor, info] : context.monitors)
		{
			MonitorInfo item;
			item.handle = monitor;
			item.bounds = info.rcMonitor;
			item.effectiveDpiX = QueryEffectiveDpi(monitor);
			if (info.dwFlags & MONITORINFOF_PRIMARY) mini.primaryIndex = mini.monitors.size();
			mini.monitors.push_back(std::move(item));
		}
		SampleTaskbars(mini);
		const auto* monitor = mini.Find(handle);
		return monitor ? ResolveUiScaleBase(mini, *monitor, useTaskbar, baseHeightDip)
			: ResolveUiScaleBase(QueryEffectiveDpi(handle), 0, 0, 0, useTaskbar, baseHeightDip);
	}

	std::optional<MonitorInfo> QueryTaskbarMonitorInfo(HMONITOR handle) noexcept
	{
		MONITORINFOEXW info{};
		info.cbSize = sizeof(info);
		if (!handle || !GetMonitorInfoW(handle, &info)) return std::nullopt;
		Snapshot mini;
		MonitorInfo monitor;
		monitor.handle = handle;
		monitor.deviceName = info.szDevice;
		monitor.bounds = info.rcMonitor;
		monitor.workArea = info.rcWork;
		monitor.pixelWidth = info.rcMonitor.right - info.rcMonitor.left;
		monitor.pixelHeight = info.rcMonitor.bottom - info.rcMonitor.top;
		monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
		const auto dpi = QueryMonitorDpi(handle, info.szDevice);
		monitor.effectiveDpiX = dpi.first;
		monitor.effectiveDpiY = dpi.second;
		mini.monitors.push_back(std::move(monitor));
		SampleTaskbars(mini);
		return std::move(mini.monitors.front());
	}

	UINT QueryEffectiveDpi(HMONITOR monitor) noexcept
	{
		MONITORINFOEXW information{};
		information.cbSize = sizeof(information);
		const bool hasInformation = monitor && GetMonitorInfoW(monitor, &information);
		// 与后续 Display 枚举共用动态 Shcore/GDI 回退，保留 Win7 加载边界。
		return QueryMonitorDpi(monitor, hasInformation ? information.szDevice : nullptr).first;
	}

	bool Initialize()
	{
		{
			std::scoped_lock lock(subscriberMutex);
			shuttingDown = false;
		}
		return Refresh(ChangeReason::Startup);
	}

	bool Refresh(ChangeReason reason)
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

			const auto previous = currentSnapshot.load(std::memory_order_acquire);
			if (reason == ChangeReason::TaskbarVisibility && !previous) return false;
			auto next = (reason == ChangeReason::Taskbar || reason == ChangeReason::TaskbarVisibility) && previous
				? std::optional<Snapshot>(*previous) : EnumerateSnapshot();
			enumerationSucceeded = next.has_value();
			if (next)
			{
				if (reason == ChangeReason::TaskbarVisibility && previous) SampleCachedTaskbars(*next, *previous);
				else
				{
					SampleTaskbars(*next, previous.get(), &taskbarTargets);
					if (const HWND hwnd = observerWindow.load(std::memory_order_acquire))
						PostMessageW(hwnd, syncTaskbarObserver, observerEpoch.load(std::memory_order_acquire), 0);
				}
			}
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
		const auto stoppedEpoch = observerEpoch.exchange(0, std::memory_order_acq_rel);
		if (const HWND hwnd = observerWindow.exchange(nullptr, std::memory_order_acq_rel))
			PostMessageW(hwnd, stopTaskbarObserver, stoppedEpoch, 0);
		{
			std::scoped_lock refreshLock(refreshMutex);
			taskbarTargets.clear();
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
