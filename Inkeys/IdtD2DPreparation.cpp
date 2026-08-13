#include "IdtD2DPreparation.h"

ComPtr<ID2D1Factory1> d2dFactory1;

ComPtr<IDWriteFactory1> dWriteFactory1;
ComPtr<IDWriteFontCollection> dWriteFontCollection;

ComPtr<ID3D11Device> d3dDevice_UI3;
ComPtr<ID2D1Device> d2dDevice_UI3;

namespace
{
	mutex ui3RenderFrameMutex;
	mutex ui3RenderDeviceMutex;
	mutex ui3RenderPrepareMutex;
	atomic_uint ui3InteractiveWaiters = 0;
	Ui3RenderDeviceEpoch ui3CurrentEpoch;
	optional<Ui3RenderDeviceEpoch> ui3PreparedEpoch;
	unsigned long long ui3NextGeneration = 1;

	HRESULT CreateUi3RenderDevice(
		Ui3RenderBackend backend, unsigned long long generation,
		Ui3RenderDeviceEpoch& epoch)
	{
		if (!d2dFactory1) return E_POINTER;

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_DRIVER_TYPE driverType = backend == Ui3RenderBackend::Hardware
			? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
		D3D_FEATURE_LEVEL requestedFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		D3D_FEATURE_LEVEL windows7FeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_0,
		};

		Ui3RenderDeviceEpoch nextEpoch;
		nextEpoch.backend = backend;
		nextEpoch.generation = generation;
		HRESULT hr = D3D11CreateDevice(
			nullptr,
			driverType,
			nullptr,
			creationFlags,
			requestedFeatureLevels,
			ARRAYSIZE(requestedFeatureLevels),
			D3D11_SDK_VERSION,
			nextEpoch.d3dDevice.ReleaseAndGetAddressOf(),
			&nextEpoch.featureLevel,
			nullptr);
		if (hr == E_INVALIDARG)
		{
			// Windows 7 旧运行时不接受包含 11.1 的列表，显式退回 11.0。
			hr = D3D11CreateDevice(
				nullptr,
				driverType,
				nullptr,
				creationFlags,
				windows7FeatureLevels,
				ARRAYSIZE(windows7FeatureLevels),
				D3D11_SDK_VERSION,
				nextEpoch.d3dDevice.ReleaseAndGetAddressOf(),
				&nextEpoch.featureLevel,
				nullptr);
		}
		if (FAILED(hr)) return hr;

		// Platform Update 上允许 Feature Level 11.0；Device1 接口只作为能力快照。
		nextEpoch.d3dDevice.As(&nextEpoch.d3dDevice1);

		ComPtr<IDXGIDevice> dxgiDevice;
		hr = nextEpoch.d3dDevice.As(&dxgiDevice);
		if (FAILED(hr)) return hr;

		hr = d2dFactory1->CreateDevice(
			dxgiDevice.Get(), nextEpoch.d2dDevice.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return hr;

		epoch = move(nextEpoch);
		return S_OK;
	}

	void PublishUi3Epoch(const Ui3RenderDeviceEpoch& epoch)
	{
		ui3CurrentEpoch = epoch;
		d3dDevice_UI3 = epoch.d3dDevice;
		d2dDevice_UI3 = epoch.d2dDevice;
	}
}

Ui3RenderDeviceEpoch GetUi3RenderDeviceEpoch()
{
	lock_guard lock(ui3RenderDeviceMutex);
	return ui3CurrentEpoch;
}

Ui3RenderPass AcquireUi3RenderPass(Ui3RenderPriority priority)
{
	if (priority == Ui3RenderPriority::Cosmetic)
	{
		if (ui3InteractiveWaiters.load(memory_order_acquire) != 0) return {};
		unique_lock lock(ui3RenderFrameMutex, try_to_lock);
		if (!lock.owns_lock()
			|| ui3InteractiveWaiters.load(memory_order_acquire) != 0)
			return {};
		return Ui3RenderPass(move(lock));
	}

	ui3InteractiveWaiters.fetch_add(1, memory_order_acq_rel);
	unique_lock lock(ui3RenderFrameMutex);
	ui3InteractiveWaiters.fetch_sub(1, memory_order_acq_rel);
	return Ui3RenderPass(move(lock));
}

HRESULT PrepareUi3RenderBackend(Ui3RenderBackend backend)
{
	// 后台准备允许与绘制并行，但多个切换请求必须保持提交顺序。
	lock_guard prepareLock(ui3RenderPrepareMutex);
	unsigned long long generation = 0;
	{
		lock_guard lock(ui3RenderDeviceMutex);
		if (ui3CurrentEpoch.d2dDevice && ui3CurrentEpoch.backend == backend)
		{
			ui3PreparedEpoch.reset();
			return S_FALSE;
		}
		ui3PreparedEpoch.reset();
		generation = ui3NextGeneration++;
	}

	Ui3RenderDeviceEpoch preparedEpoch;
	HRESULT hr = CreateUi3RenderDevice(backend, generation, preparedEpoch);
	if (FAILED(hr)) return hr;

	lock_guard lock(ui3RenderDeviceMutex);
	ui3PreparedEpoch = move(preparedEpoch);
	return S_OK;
}

bool CommitPreparedUi3RenderBackend()
{
	// 发布前等待当前完整帧退出，客户端会在下一帧按 generation 重建设备资源。
	auto renderPass = AcquireUi3RenderPass(Ui3RenderPriority::Interactive);
	lock_guard lock(ui3RenderDeviceMutex);
	if (!ui3PreparedEpoch.has_value()) return false;
	PublishUi3Epoch(ui3PreparedEpoch.value());
	ui3PreparedEpoch.reset();
	return true;
}

bool RecoverUi3RenderDevice()
{
	Ui3RenderBackend backend = Ui3RenderBackend::Warp;
	unsigned long long generation = 0;
	{
		lock_guard lock(ui3RenderDeviceMutex);
		backend = ui3CurrentEpoch.backend;
		generation = ui3NextGeneration++;
	}

	Ui3RenderDeviceEpoch recoveredEpoch;
	if (FAILED(CreateUi3RenderDevice(backend, generation, recoveredEpoch)))
		return false;

	// 重建设备和发布代次均在共享调度线程完成，避免客户端看到半更新状态。
	auto renderPass = AcquireUi3RenderPass(Ui3RenderPriority::Interactive);
	lock_guard lock(ui3RenderDeviceMutex);
	ui3PreparedEpoch.reset();
	PublishUi3Epoch(recoveredEpoch);
	return true;
}

HRESULT D2DStarup()
{
	auto resetState = []()
		{
			lock_guard lock(ui3RenderDeviceMutex);
			ui3PreparedEpoch.reset();
			ui3CurrentEpoch = {};
			d2dDevice_UI3.Reset();
			d3dDevice_UI3.Reset();
			dWriteFontCollection.Reset();
			dWriteFactory1.Reset();
			d2dFactory1.Reset();
		};

	HRESULT hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_MULTI_THREADED,
		__uuidof(ID2D1Factory1),
		NULL,
		reinterpret_cast<void**>(d2dFactory1.ReleaseAndGetAddressOf())
	);
	if (FAILED(hr))
	{
		resetState();
		return hr;
	}

	// 创建 D2D1.1 工厂
	hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory1),
		reinterpret_cast<IUnknown**>(dWriteFactory1.ReleaseAndGetAddressOf())
	);
	if (FAILED(hr))
	{
		resetState();
		return hr;
	}

	// UI3 默认仍使用 WARP；Hardware 只通过显式准备/提交接口切换。
	Ui3RenderDeviceEpoch initialEpoch;
	hr = CreateUi3RenderDevice(Ui3RenderBackend::Warp, ui3NextGeneration++, initialEpoch);
	if (FAILED(hr))
	{
		resetState();
		return hr;
	}
	{
		lock_guard lock(ui3RenderDeviceMutex);
		PublishUi3Epoch(initialEpoch);
	}

	return S_OK;
}
void D2DShutdown()
{
	auto renderPass = AcquireUi3RenderPass(Ui3RenderPriority::Interactive);
	{
		lock_guard lock(ui3RenderDeviceMutex);
		ui3PreparedEpoch.reset();
		ui3CurrentEpoch = {};
		d2dDevice_UI3.Reset();
		d3dDevice_UI3.Reset();
	}
	dWriteFontCollection.Reset();
	dWriteFactory1.Reset();
	d2dFactory1.Reset();
}
