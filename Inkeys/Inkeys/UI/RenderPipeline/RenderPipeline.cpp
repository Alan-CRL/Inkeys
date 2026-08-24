module;

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dwrite_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>

module Inkeys.UI.RenderPipeline;

namespace Inkeys::UI::RenderPipeline
{
	namespace
	{
		constexpr auto ClientCount = static_cast<std::size_t>(Client::Count);
		constexpr auto FrameInterval = std::chrono::nanoseconds(16'666'667);
		constexpr std::array<Client, ClientCount> DispatchOrder{
			Client::Bar,
			Client::PptBottomLeft,
			Client::PptBottomRight,
			Client::PptMiddleLeft,
			Client::PptMiddleRight,
			Client::Settings,
			Client::WhiteboardFreeze,
		};

		[[nodiscard]] constexpr std::size_t Index(Client client) noexcept
		{
			return static_cast<std::size_t>(client);
		}

		[[nodiscard]] constexpr ClientMask AllClientMask() noexcept
		{
			return (ClientMask{ 1 } << static_cast<unsigned>(Client::Count)) - 1;
		}

		std::mutex assetMutex;
		std::mutex prepareMutex;
		SharedAssets sharedAssets;
		DeviceEpoch currentEpoch;
		std::optional<DeviceEpoch> preparedEpoch;
		std::optional<DeviceEpoch> pendingEpoch;
		std::uint64_t nextGeneration = 1;
		std::atomic_bool initialized = false;

		HRESULT CreateRenderDevice(
			Backend backend, std::uint64_t generation, DeviceEpoch& epoch)
		{
			ComPtr<ID2D1Factory1> factory;
			{
				std::scoped_lock lock(assetMutex);
				factory = sharedAssets.d2dFactory;
			}
			if (!factory) return E_POINTER;

			const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
			const D3D_DRIVER_TYPE driverType = backend == Backend::Hardware
				? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
			const D3D_FEATURE_LEVEL requestedFeatureLevels[]{
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0,
			};
			const D3D_FEATURE_LEVEL windows7FeatureLevels[]{
				D3D_FEATURE_LEVEL_11_0,
			};

			DeviceEpoch nextEpoch;
			nextEpoch.backend = backend;
			nextEpoch.generation = generation;
			HRESULT hr = D3D11CreateDevice(
				nullptr, driverType, nullptr, creationFlags,
				requestedFeatureLevels, ARRAYSIZE(requestedFeatureLevels),
				D3D11_SDK_VERSION,
				nextEpoch.d3dDevice.ReleaseAndGetAddressOf(),
				&nextEpoch.featureLevel,
				nextEpoch.immediateContext.ReleaseAndGetAddressOf());
			if (hr == E_INVALIDARG)
			{
				// Windows 7 旧运行时不接受包含 11.1 的列表，显式退回 11.0。
				hr = D3D11CreateDevice(
					nullptr, driverType, nullptr, creationFlags,
					windows7FeatureLevels, ARRAYSIZE(windows7FeatureLevels),
					D3D11_SDK_VERSION,
					nextEpoch.d3dDevice.ReleaseAndGetAddressOf(),
					&nextEpoch.featureLevel,
					nextEpoch.immediateContext.ReleaseAndGetAddressOf());
			}
			if (FAILED(hr)) return hr;

			// Platform Update 只保证基础 D3D11；Device1 是可选能力快照。
			nextEpoch.d3dDevice.As(&nextEpoch.d3dDevice1);
			hr = nextEpoch.d3dDevice.As(&nextEpoch.dxgiDevice);
			if (FAILED(hr)) return hr;

			ComPtr<IDXGIAdapter> adapter;
			hr = nextEpoch.dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf());
			if (FAILED(hr)) return hr;
			hr = adapter->GetParent(IID_PPV_ARGS(nextEpoch.dxgiFactory.ReleaseAndGetAddressOf()));
			if (FAILED(hr)) return hr;

			hr = factory->CreateDevice(
				nextEpoch.dxgiDevice.Get(), nextEpoch.d2dDevice.ReleaseAndGetAddressOf());
			if (FAILED(hr)) return hr;

			epoch = std::move(nextEpoch);
			return S_OK;
		}

		[[nodiscard]] FrameContext SnapshotFrameContext(
			std::chrono::steady_clock::time_point frameTime)
		{
			std::scoped_lock lock(assetMutex);
			return FrameContext{ currentEpoch, sharedAssets, frameTime };
		}

		bool RecoverDevice()
		{
			Backend backend = Backend::Warp;
			std::uint64_t generation = 0;
			{
				std::scoped_lock lock(assetMutex);
				backend = currentEpoch.backend;
				generation = nextGeneration++;
			}

			DeviceEpoch recovered;
			if (FAILED(CreateRenderDevice(backend, generation, recovered))) return false;
			// 只有管线线程调用恢复并发布新代次，客户端不会看到半更新资产。
			std::scoped_lock prepareLock(prepareMutex);
			std::scoped_lock lock(assetMutex);
			preparedEpoch.reset();
			pendingEpoch.reset();
			currentEpoch = std::move(recovered);
			return true;
		}

		bool PublishPendingBackend()
		{
			std::scoped_lock prepareLock(prepareMutex);
			std::scoped_lock lock(assetMutex);
			if (!pendingEpoch) return false;
			// currentEpoch 只在渲染线程控制点发布，客户端始终看到完整代次。
			currentEpoch = std::move(*pendingEpoch);
			pendingEpoch.reset();
			return true;
		}
	}

	void DispatchState::Request(ClientMask mask) noexcept
	{
		requested_.fetch_or(mask & AllClientMask(), std::memory_order_release);
	}

	ClientMask DispatchState::TakeRequested() noexcept
	{
		return requested_.exchange(0, std::memory_order_acq_rel);
	}

	void DispatchState::Reset() noexcept
	{
		requested_.store(0, std::memory_order_release);
	}

	DispatchDecision DispatchState::Complete(ClientMask work,
		ClientMask registered,
		const std::array<FrameResult, ClientCount>& results) noexcept
	{
		DispatchDecision decision;
		decision.work = work;
		for (const auto client : DispatchOrder)
		{
			const auto bit = Mask(client);
			if ((work & bit) == 0) continue;
			switch (results[Index(client)])
			{
			case FrameResult::Continue:
			case FrameResult::Retry:
				decision.next |= bit;
				break;
			case FrameResult::DeviceLost:
				decision.rebuildSharedDevice = true;
				break;
			case FrameResult::Stop:
				decision.stop = true;
				break;
			case FrameResult::Idle:
			default:
				break;
			}
		}
		if (decision.rebuildSharedDevice) decision.next |= registered;
		decision.next &= registered;
		// 显式请求可能与注册并发，不能用调用方稍旧的 registered 快照提前清除。
		decision.next |= TakeRequested();
		decision.sleep = decision.next == 0 && !decision.stop;
		return decision;
	}

	struct Scheduler::Impl
	{
		Impl() { wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
		~Impl()
		{
			Stop();
			if (wakeEvent) CloseHandle(wakeEvent);
		}

		void Stop() noexcept
		{
			stopRequested.store(true, std::memory_order_release);
			if (wakeEvent) SetEvent(wakeEvent);
			if (renderThread.joinable()) renderThread.join();
			// Scheduler 可重启；停止后不能把旧请求或控制命令带进下一轮。
			dispatch.Reset();
			controlRequested.store(false, std::memory_order_release);
			{
				std::scoped_lock lock(callbackMutex);
				controlTasks.clear();
			}
			if (wakeEvent) ResetEvent(wakeEvent);
			running.store(false, std::memory_order_release);
		}

		std::mutex callbackMutex;
		std::condition_variable callbackCondition;
		std::array<RenderCallback, ClientCount> callbacks{};
		std::array<std::size_t, ClientCount> activeCallbacks{};
		ContextProvider contextProvider;
		DeviceRecoveryCallback deviceRecovery;
		ControlCallback controlCallback;
		std::deque<ControlTask> controlTasks;
		DispatchState dispatch;
		HANDLE wakeEvent = nullptr;
		std::jthread renderThread;
		std::atomic_bool stopRequested = false;
		std::atomic_bool controlRequested = false;
		std::atomic_bool running = false;
	};

	Scheduler::Scheduler() : impl_(new Impl) {}

	Scheduler::~Scheduler()
	{
		delete impl_;
		impl_ = nullptr;
	}

	bool Scheduler::Start(ContextProvider contextProvider,
		DeviceRecoveryCallback deviceRecovery,
		ControlCallback controlCallback)
	{
		if (!impl_ || !impl_->wakeEvent
			|| impl_->running.exchange(true, std::memory_order_acq_rel)) return false;
		impl_->stopRequested.store(false, std::memory_order_release);
		{
			std::scoped_lock lock(impl_->callbackMutex);
			impl_->contextProvider = std::move(contextProvider);
			impl_->deviceRecovery = std::move(deviceRecovery);
			impl_->controlCallback = std::move(controlCallback);
		}

		impl_->renderThread = std::jthread([this]
			{
				auto registeredMask = [this]() -> ClientMask
					{
						ClientMask registered = 0;
						std::scoped_lock lock(impl_->callbackMutex);
						for (const auto client : DispatchOrder)
							if (impl_->callbacks[Index(client)]) registered |= Mask(client);
						return registered;
					};
				auto takeControlRequest = [this, &registeredMask]() -> ClientMask
					{
						std::deque<ControlTask> tasks;
						{
							std::scoped_lock lock(impl_->callbackMutex);
							tasks.swap(impl_->controlTasks);
						}
						// 生命周期控制始终先于设备恢复重试，且与渲染回调同线程串行。
						for (auto& task : tasks)
						{
							try { task(); }
							catch (...) {}
						}
						if (!impl_->controlRequested.exchange(
							false, std::memory_order_acq_rel)) return 0;
						ControlCallback callback;
						{
							std::scoped_lock lock(impl_->callbackMutex);
							callback = impl_->controlCallback;
						}
						// 控制发布成功后只请求当下实际注册的客户端。
						return callback && callback() ? registeredMask() : 0;
					};
				ClientMask pending = impl_->dispatch.TakeRequested();
				bool recoveryPending = false;
				auto nextDeadline = std::chrono::steady_clock::now();
				while (!impl_->stopRequested.load(std::memory_order_acquire))
				{
					pending |= takeControlRequest();
					if (pending == 0)
					{
						ResetEvent(impl_->wakeEvent);
						// reset 后再次交换请求和控制位，覆盖 idle 边界竞态。
						pending = impl_->dispatch.TakeRequested();
						pending |= takeControlRequest();
						if (pending == 0)
						{
							if (impl_->stopRequested.load(std::memory_order_acquire)) break;
							WaitForSingleObject(impl_->wakeEvent, INFINITE);
							pending = impl_->dispatch.TakeRequested();
							pending |= takeControlRequest();
						}
						if (impl_->stopRequested.load(std::memory_order_acquire)) break;
						if (pending == 0) continue;
					}

					const auto now = std::chrono::steady_clock::now();
					if (now < nextDeadline) std::this_thread::sleep_until(nextDeadline);
					const auto frameTime = std::chrono::steady_clock::now();
					nextDeadline = frameTime + FrameInterval;
					pending |= impl_->dispatch.TakeRequested();

					ContextProvider contextProvider;
					DeviceRecoveryCallback deviceRecovery;
					ClientMask registered = 0;
					{
						std::scoped_lock lock(impl_->callbackMutex);
						contextProvider = impl_->contextProvider;
						deviceRecovery = impl_->deviceRecovery;
						for (const auto client : DispatchOrder)
							if (impl_->callbacks[Index(client)]) registered |= Mask(client);
					}
					pending &= registered;
					if (pending == 0) continue;
					if (recoveryPending)
					{
						// 恢复失败保留同一批 registered 客户端，下一节拍继续恢复而不使用旧 epoch。
						if (!deviceRecovery || !deviceRecovery())
						{
							pending |= registered;
							continue;
						}
						recoveryPending = false;
						pending |= registered;
					}
					const FrameContext context = contextProvider
						? contextProvider(frameTime) : FrameContext{ {}, {}, frameTime };
					std::array<FrameResult, ClientCount> results{};
					results.fill(FrameResult::Idle);
					const auto work = std::exchange(pending, 0);
					for (const auto client : DispatchOrder)
					{
						const auto bit = Mask(client);
						if ((work & bit) == 0) continue;
						RenderCallback callback;
						{
							std::scoped_lock lock(impl_->callbackMutex);
							callback = impl_->callbacks[Index(client)];
							if (callback) ++impl_->activeCallbacks[Index(client)];
						}
						if (!callback) continue;
						try { results[Index(client)] = callback(context); }
						catch (...) { results[Index(client)] = FrameResult::Retry; }
						{
							std::scoped_lock lock(impl_->callbackMutex);
							--impl_->activeCallbacks[Index(client)];
						}
						impl_->callbackCondition.notify_all();
						// 共享设备失效或进程级停止后，不再让本帧后续客户端使用旧上下文。
						if (results[Index(client)] == FrameResult::DeviceLost
							|| results[Index(client)] == FrameResult::Stop) break;
					}

					// 回调执行期间可能注册新客户端；用最新掩码保留其首次请求。
					registered = registeredMask();
					const auto decision = impl_->dispatch.Complete(work, registered, results);
					if (decision.stop) break;
					pending = decision.next;
					if (decision.rebuildSharedDevice) recoveryPending = true;
				}
				std::deque<ControlTask> finalTasks;
				{
					std::scoped_lock lock(impl_->callbackMutex);
					// 关闭接收与提取队列同锁完成，保证已接受的生命周期任务不会滞留。
					impl_->running.store(false, std::memory_order_release);
					finalTasks.swap(impl_->controlTasks);
				}
				for (auto& task : finalTasks)
				{
					try { task(); }
					catch (...) {}
				}
			});
		return true;
	}

	void Scheduler::Stop() noexcept
	{
		if (impl_) impl_->Stop();
	}

	bool Scheduler::Register(Client client, RenderCallback callback)
	{
		if (!impl_ || !callback || client >= Client::Count) return false;
		{
			std::scoped_lock lock(impl_->callbackMutex);
			impl_->callbacks[Index(client)] = std::move(callback);
		}
		Request(client);
		return true;
	}

	void Scheduler::Unregister(Client client) noexcept
	{
		if (!impl_ || client >= Client::Count) return;
		std::unique_lock lock(impl_->callbackMutex);
		impl_->callbacks[Index(client)] = {};
		impl_->callbackCondition.wait(lock, [this, client]
			{ return impl_->activeCallbacks[Index(client)] == 0; });
	}

	void Scheduler::Request(Client client) noexcept
	{
		if (client < Client::Count) Request(Mask(client));
	}

	void Scheduler::Request(ClientMask mask) noexcept
	{
		if (!impl_) return;
		impl_->dispatch.Request(mask);
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	void Scheduler::RequestControl() noexcept
	{
		if (!impl_) return;
		impl_->controlRequested.store(true, std::memory_order_release);
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	bool Scheduler::PostControl(ControlTask task)
	{
		if (!impl_ || !task || !impl_->running.load(std::memory_order_acquire)
			|| impl_->stopRequested.load(std::memory_order_acquire)) return false;
		{
			std::scoped_lock lock(impl_->callbackMutex);
			if (!impl_->running.load(std::memory_order_acquire)
				|| impl_->stopRequested.load(std::memory_order_acquire)) return false;
			impl_->controlTasks.push_back(std::move(task));
		}
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
		return true;
	}

	void Scheduler::WakeForStop() noexcept
	{
		if (impl_ && impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	namespace
	{
		Scheduler scheduler;
	}

	HRESULT Initialize()
	{
		if (initialized.load(std::memory_order_acquire)) return S_FALSE;
		SharedAssets nextAssets;
		HRESULT hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_MULTI_THREADED,
			__uuidof(ID2D1Factory1), nullptr,
			reinterpret_cast<void**>(nextAssets.d2dFactory.ReleaseAndGetAddressOf()));
		if (FAILED(hr)) return hr;
		hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory1),
			reinterpret_cast<IUnknown**>(nextAssets.dwriteFactory.ReleaseAndGetAddressOf()));
		if (FAILED(hr)) return hr;
		{
			std::scoped_lock lock(assetMutex);
			sharedAssets = std::move(nextAssets);
		}

		DeviceEpoch initialEpoch;
		hr = CreateRenderDevice(Backend::Warp, nextGeneration++, initialEpoch);
		if (FAILED(hr))
		{
			std::scoped_lock lock(assetMutex);
			sharedAssets = {};
			return hr;
		}
		{
			std::scoped_lock lock(assetMutex);
			currentEpoch = std::move(initialEpoch);
		}
		if (!scheduler.Start(
			SnapshotFrameContext, RecoverDevice, PublishPendingBackend))
		{
			std::scoped_lock lock(assetMutex);
			currentEpoch = {};
			sharedAssets = {};
			return E_FAIL;
		}
		initialized.store(true, std::memory_order_release);
		return S_OK;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		scheduler.Stop();
		std::scoped_lock prepareLock(prepareMutex);
		std::scoped_lock lock(assetMutex);
		preparedEpoch.reset();
		pendingEpoch.reset();
		currentEpoch = {};
		sharedAssets = {};
	}

	bool IsInitialized() noexcept
	{
		return initialized.load(std::memory_order_acquire);
	}

	HRESULT InitializeFontCollection(IDWriteFontFileLoader* fileLoader,
		IDWriteFontCollectionLoader* collectionLoader,
		std::span<const UINT> resourceIds)
	{
		if (!fileLoader || !collectionLoader || resourceIds.empty()) return E_INVALIDARG;
		ComPtr<IDWriteFactory1> factory = DWriteFactory();
		if (!factory) return E_POINTER;
		HRESULT hr = factory->RegisterFontFileLoader(fileLoader);
		if (FAILED(hr) && hr != DWRITE_E_ALREADYREGISTERED) return hr;
		hr = factory->RegisterFontCollectionLoader(collectionLoader);
		if (FAILED(hr) && hr != DWRITE_E_ALREADYREGISTERED) return hr;

		ComPtr<IDWriteFontCollection> collection;
		hr = factory->CreateCustomFontCollection(
			collectionLoader, resourceIds.data(),
			static_cast<UINT32>(resourceIds.size_bytes()),
			collection.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return hr;
		std::scoped_lock lock(assetMutex);
		sharedAssets.fontCollection = std::move(collection);
		return S_OK;
	}

	DeviceEpoch GetDeviceEpoch()
	{
		std::scoped_lock lock(assetMutex);
		return currentEpoch;
	}

	SharedAssets GetSharedAssets()
	{
		std::scoped_lock lock(assetMutex);
		return sharedAssets;
	}

	ComPtr<ID2D1Factory1> D2DFactory() { return GetSharedAssets().d2dFactory; }
	ComPtr<IDWriteFactory1> DWriteFactory() { return GetSharedAssets().dwriteFactory; }
	ComPtr<IDWriteFontCollection> FontCollection() { return GetSharedAssets().fontCollection; }

	HRESULT PrepareBackend(Backend backend)
	{
		std::scoped_lock prepareLock(prepareMutex);
		std::uint64_t generation = 0;
		{
			std::scoped_lock lock(assetMutex);
			if (currentEpoch.d2dDevice && currentEpoch.backend == backend)
			{
				preparedEpoch.reset();
				return S_FALSE;
			}
			preparedEpoch.reset();
			generation = nextGeneration++;
		}
		DeviceEpoch prepared;
		const HRESULT hr = CreateRenderDevice(backend, generation, prepared);
		if (FAILED(hr)) return hr;
		std::scoped_lock lock(assetMutex);
		preparedEpoch = std::move(prepared);
		return S_OK;
	}

	bool CommitPreparedBackend() noexcept
	{
		{
			std::scoped_lock prepareLock(prepareMutex);
			std::scoped_lock lock(assetMutex);
			if (!preparedEpoch || pendingEpoch) return false;
			pendingEpoch = std::move(*preparedEpoch);
			preparedEpoch.reset();
		}
		// 先释放准备锁再唤醒；epoch 仍只由唯一渲染线程发布。
		scheduler.RequestControl();
		return true;
	}

	bool Register(Client client, RenderCallback callback)
	{
		return scheduler.Register(client, std::move(callback));
	}

	void Unregister(Client client) noexcept { scheduler.Unregister(client); }
	void Request(Client client) noexcept { scheduler.Request(client); }
	void Request(ClientMask mask) noexcept { scheduler.Request(mask); }
	bool PostControl(ControlTask task) { return scheduler.PostControl(std::move(task)); }
	void WakeForStop() noexcept { scheduler.WakeForStop(); }
}
