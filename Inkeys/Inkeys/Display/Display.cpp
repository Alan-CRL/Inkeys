module;

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cwchar>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Inkeys.Display;

namespace
{
	using namespace Inkeys::Display;

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

	[[nodiscard]] bool EqualRectValue(const RECT& left, const RECT& right) noexcept
	{
		return left.left == right.left && left.top == right.top &&
			left.right == right.right && left.bottom == right.bottom;
	}

	[[nodiscard]] bool EqualEdid(const EdidInfo& left, const EdidInfo& right)
	{
		return left.valid == right.valid &&
			left.majorVersion == right.majorVersion &&
			left.minorVersion == right.minorVersion &&
			left.deviceId == right.deviceId &&
			left.rawPhysicalWidthCm == right.rawPhysicalWidthCm &&
			left.rawPhysicalHeightCm == right.rawPhysicalHeightCm &&
			left.physicalWidthCm == right.physicalWidthCm &&
			left.physicalHeightCm == right.physicalHeightCm;
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
			EqualEdid(left.edid, right.edid);
	}

	[[nodiscard]] bool SemanticallyEqual(const Snapshot& left, const Snapshot& right)
	{
		if (left.primaryIndex != right.primaryIndex || left.fallback != right.fallback ||
			!EqualRectValue(left.virtualBounds, right.virtualBounds) ||
			left.monitors.size() != right.monitors.size()) return false;
		for (std::size_t index = 0; index < left.monitors.size(); ++index)
			if (!EqualMonitor(left.monitors[index], right.monitors[index])) return false;
		return true;
	}

	[[nodiscard]] bool ParseModelDriver(
		std::wstring_view deviceId, std::wstring& model, std::wstring& driver)
	{
		const auto beginSlash = deviceId.find(L'\\');
		if (beginSlash == std::wstring_view::npos) return false;
		const auto driverSlash = deviceId.find(L'\\', beginSlash + 1);
		if (driverSlash == std::wstring_view::npos) return false;
		model.assign(deviceId.substr(beginSlash + 1,
			(std::min<std::size_t>)(7, driverSlash - beginSlash - 1)));
		driver.assign(deviceId.substr(driverSlash + 1));
		return !model.empty() && !driver.empty();
	}

	[[nodiscard]] bool EdidMatchesModel(
		std::span<const std::uint8_t> bytes, std::wstring_view model)
	{
		if (bytes.size() < 12 || model.empty()) return false;
		wchar_t value[9]{};
		const auto byte1 = bytes[8];
		const auto byte2 = bytes[9];
		value[0] = static_cast<wchar_t>(((byte1 & 0x7C) >> 2) + 64);
		value[1] = static_cast<wchar_t>(((byte1 & 0x03) << 3) + ((byte2 & 0xE0) >> 5) + 64);
		value[2] = static_cast<wchar_t>((byte2 & 0x1F) + 64);
		swprintf_s(value + 3, std::size(value) - 3, L"%X%X%X%X",
			(bytes[11] & 0xF0) >> 4, bytes[11] & 0x0F,
			(bytes[10] & 0xF0) >> 4, bytes[10] & 0x0F);
		return _wcsicmp(value, std::wstring(model).c_str()) == 0;
	}

	[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadRegistryEdid(
		std::wstring_view model, std::wstring_view driver)
	{
		std::wstring subKey = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\";
		subKey.append(model);
		HKEY modelKey = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ,
			&modelKey) != ERROR_SUCCESS) return std::nullopt;

		std::optional<std::vector<std::uint8_t>> result;
		for (DWORD index = 0; !result; ++index)
		{
			std::array<wchar_t, MAX_PATH> instanceName{};
			DWORD instanceLength = static_cast<DWORD>(instanceName.size());
			FILETIME written{};
			if (RegEnumKeyExW(modelKey, index, instanceName.data(), &instanceLength,
				nullptr, nullptr, nullptr, &written) != ERROR_SUCCESS) break;

			HKEY instanceKey = nullptr;
			if (RegOpenKeyExW(modelKey, instanceName.data(), 0, KEY_READ,
				&instanceKey) != ERROR_SUCCESS) continue;
			std::array<wchar_t, MAX_PATH> registeredDriver{};
			DWORD driverBytes = static_cast<DWORD>(registeredDriver.size() * sizeof(wchar_t));
			const bool driverMatches = RegQueryValueExW(instanceKey, L"Driver", nullptr,
				nullptr, reinterpret_cast<LPBYTE>(registeredDriver.data()), &driverBytes) == ERROR_SUCCESS &&
				_wcsicmp(registeredDriver.data(), std::wstring(driver).c_str()) == 0;
			if (driverMatches)
			{
				HKEY parametersKey = nullptr;
				if (RegOpenKeyExW(instanceKey, L"Device Parameters", 0, KEY_READ,
					&parametersKey) == ERROR_SUCCESS)
				{
					DWORD size = 0;
					if (RegQueryValueExW(parametersKey, L"EDID", nullptr, nullptr,
						nullptr, &size) == ERROR_SUCCESS && size > 0)
					{
						std::vector<std::uint8_t> bytes(size);
						if (RegQueryValueExW(parametersKey, L"EDID", nullptr, nullptr,
							bytes.data(), &size) == ERROR_SUCCESS)
						{
							bytes.resize(size);
							if (EdidMatchesModel(bytes, model)) result = std::move(bytes);
						}
					}
					RegCloseKey(parametersKey);
				}
			}
			RegCloseKey(instanceKey);
		}
		RegCloseKey(modelKey);
		return result;
	}

	[[nodiscard]] std::wstring FindMonitorDeviceId(std::wstring_view monitorDeviceName)
	{
		for (DWORD adapterIndex = 0;; ++adapterIndex)
		{
			DISPLAY_DEVICEW adapter{};
			adapter.cb = sizeof(adapter);
			if (!EnumDisplayDevicesW(nullptr, adapterIndex, &adapter, 0)) break;
			for (DWORD monitorIndex = 0;; ++monitorIndex)
			{
				DISPLAY_DEVICEW monitor{};
				monitor.cb = sizeof(monitor);
				if (!EnumDisplayDevicesW(adapter.DeviceName, monitorIndex, &monitor, 0)) break;
				if ((monitor.StateFlags & DISPLAY_DEVICE_ACTIVE) == 0 ||
					(monitor.StateFlags & DISPLAY_DEVICE_ATTACHED) == 0) continue;
				if (_wcsnicmp(monitor.DeviceName, monitorDeviceName.data(),
					monitorDeviceName.size()) == 0) return monitor.DeviceID;
			}
		}
		return {};
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

			const auto deviceId = FindMonitorDeviceId(native.szDevice);
			monitor.edid.deviceId = deviceId;
			std::wstring model;
			std::wstring driver;
			if (ParseModelDriver(deviceId, model, driver))
			{
				if (const auto bytes = ReadRegistryEdid(model, driver))
					monitor.edid = ParseEdid(*bytes, deviceId);
			}
			monitor.edid = OrientEdid(std::move(monitor.edid), monitor.orientation);
			snapshot.monitors.push_back(std::move(monitor));
		}

		auto primary = std::find_if(snapshot.monitors.begin(), snapshot.monitors.end(),
			[](const MonitorInfo& monitor) { return monitor.primary; });
		snapshot.primaryIndex = primary == snapshot.monitors.end() ? 0 :
			static_cast<std::size_t>(primary - snapshot.monitors.begin());
		if (primary == snapshot.monitors.end()) snapshot.monitors.front().primary = true;
		snapshot.virtualBounds = snapshot.monitors.front().bounds;
		for (const auto& monitor : snapshot.monitors)
		{
			snapshot.virtualBounds.left = (std::min)(snapshot.virtualBounds.left, monitor.bounds.left);
			snapshot.virtualBounds.top = (std::min)(snapshot.virtualBounds.top, monitor.bounds.top);
			snapshot.virtualBounds.right = (std::max)(snapshot.virtualBounds.right, monitor.bounds.right);
			snapshot.virtualBounds.bottom = (std::max)(snapshot.virtualBounds.bottom, monitor.bounds.bottom);
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
		snapshot.virtualBounds = monitor.bounds;
		snapshot.monitors.push_back(std::move(monitor));
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

	EdidInfo ParseEdid(std::span<const std::uint8_t> bytes,
		std::wstring_view deviceId)
	{
		EdidInfo result;
		result.deviceId.assign(deviceId);
		constexpr std::array<std::uint8_t, 8> header{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
		if (bytes.size() < 23 || !std::equal(header.begin(), header.end(), bytes.begin()))
			return result;
		result.majorVersion = bytes[18];
		result.minorVersion = bytes[19];
		result.rawPhysicalWidthCm = bytes[21];
		result.rawPhysicalHeightCm = bytes[22];
		result.physicalWidthCm = result.rawPhysicalWidthCm;
		result.physicalHeightCm = result.rawPhysicalHeightCm;
		result.valid = result.rawPhysicalWidthCm > 0 && result.rawPhysicalHeightCm > 0;
		return result;
	}

	EdidInfo OrientEdid(EdidInfo edid, DWORD orientation) noexcept
	{
		edid.physicalWidthCm = edid.rawPhysicalWidthCm;
		edid.physicalHeightCm = edid.rawPhysicalHeightCm;
		if (orientation == DMDO_90 || orientation == DMDO_270)
			std::swap(edid.physicalWidthCm, edid.physicalHeightCm);
		return edid;
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
				if (previous) return false;
				next = MakeFallbackSnapshot();
			}
			if (previous && SemanticallyEqual(*previous, *next))
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
