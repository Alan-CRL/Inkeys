#include "main.h"

#include "renderer.h"

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// D2D 工厂
	{
		ID2D1Factory1* tmpFactory = nullptr;
		D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (IID_PPV_ARGS(&tmpFactory)));
		d2dFactory1.Attach(tmpFactory);
	}

	// 初始化 D3D 设备
	CComPtr<ID3D11DeviceContext> d3dDeviceContext; // DC
	{
		// 创建 HARDWARE 设备

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};

		D3D_FEATURE_LEVEL actualFeatureLevel;
		HRESULT hr = S_OK;

		hr = D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_HARDWARE,   // 使用 HARDWARE 硬件加速渲染器
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			&d3dDevice_HARDWARE,        // 返回创建的设备
			&actualFeatureLevel,        // 返回实际的功能级别
			&d3dDeviceContext           // 返回设备上下文
		);
		if (FAILED(hr))
		{
			// DirectX 设备初始化异常
		}

		d3dDevice_HARDWARE->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice1));

		// D2D
		d2dFactory1->CreateDevice(dxgiDevice1, &d2dDevice_HARDWARE);
	}

	// D2D 设备
	CComPtr<ID2D1DeviceContext> d2dDeviceContext;
	{
		d2dDevice_HARDWARE->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
			&d2dDeviceContext
		);
	}

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h, EW_SHOWCONSOLE);
	}

	// 从 windows8 开始可以考虑 SwapChain2 的 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT 更适合墨迹输入

	// 常规场景下的墨迹输入应使用 dxgiDevice1::SetMaximumFrameLatency(1) 来确保有一帧的间隙 CPU 处理时间留给 GPU 并行渲染来提高性能
	dxgiDevice1->SetMaximumFrameLatency(1);

	// 后续性能选项卡中可以提供一个 GPU 高优先级 的选项，调用 SetGPUThreadPriority(2) 来提升 GPU 调度优先级
	// dxgiDevice1->SetGPUThreadPriority(2);

	// SwapChain
	CComPtr<IDXGISwapChain1> swapChain;
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = windowInfo.w;
		swapChainDesc.Height = windowInfo.h;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Flags = 0;

		CComPtr<IDXGIAdapter> dxgiAdapter;
		dxgiDevice1->GetAdapter(&dxgiAdapter);

		CComPtr<IDXGIFactory2> dxgiFactory;
		dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

		dxgiFactory->CreateSwapChainForHwnd(
			d3dDevice_HARDWARE,
			windowHWND,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain
		);

		// win7 上 SetBackgroundColor 会因 E_NOTIMPL 失败
		DXGI_RGBA color = { 1.0f, 1.0f, 1.0f, 1.0f };
		swapChain->SetBackgroundColor(&color);
	}
	// D2D Bitmap
	{
		CComPtr<IDXGISurface> dxgiBackBuffer;
		swapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&dxgiBackBuffer);

		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
		);

		CComPtr<ID2D1Bitmap1> d2dTargetBitmap;
		d2dDeviceContext->CreateBitmapFromDxgiSurface(
			dxgiBackBuffer,
			&bitmapProperties,
			&d2dTargetBitmap
		);

		d2dDeviceContext->SetTarget(d2dTargetBitmap);
	}

	// 交换链应该保证指定脏区，而不是全部重绘
	// 后续修改，非 flip_discard

	inkRenderer.Init(d3dDevice_HARDWARE, d3dDeviceContext, swapChain);
	inkRenderer.SetScreenSize((float)windowInfo.w, (float)windowInfo.h);

	/*
	vector<InkVertex> list;
	{
		float x1 = 100.0f;
		float y1 = 100.0f;
		float r1 = 25.0f;

		float x2 = 500.0f;
		float y2 = 500.0f;
		float r2 = 150.0f;

		list.emplace_back(InkVertex(x1, y1, r1, x2, y2, r2, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)));
	}

	// 开始绘制
	float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

	inkRenderer.DrawStrokeSegment2(list, 0, list.size());

	// 同步本帧完成
	d3dDeviceContext->End(inkRenderer.g_frameFinishQuery);
	BOOL done = FALSE;
	// 注意：GetData 会在 GPU 还没执行到这个 Query 时返回 S_FALSE
	while (S_OK != d3dDeviceContext->GetData(inkRenderer.g_frameFinishQuery, &done, sizeof(done), 0))
	{
		this_thread::yield();
	}
	*/

	IMAGE temp = CreateImageColor(windowInfo.w, windowInfo.h, RGBA(255, 255, 255, 255), false);
	Gdiplus::Graphics graphics(GetImageHDC(&temp));

	// 每帧绘制前应该
	/*
	// 关键：重新设置渲染目标
	ID3D11RenderTargetView* rtvs[] = { renderTargetView.p };
	context->OMSetRenderTargets(1, rtvs, nullptr);

	// 可选：清空背景（如果你需要的话）
	// float clearColor[] = { 0.2f, 0.3f, 0.5f, 1.0f };
	// context->ClearRenderTargetView(renderTargetView.p, clearColor);
	*/

	{
		float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

		// 这一部分是测试 GPU 并行绘制大量胶囊
		{
			std::vector<InkPoint> strokePoints;
			strokePoints.push_back({ 100.0f, 100.0f, 50.0f }); // 起点，半径10
			strokePoints.push_back({ 850.0f, 320.0f, 75.0f });
			strokePoints.push_back({ 800.0f, 680.0f, 40.0f });
			strokePoints.push_back({ 220.0f, 850.0f, 60.0f });  // 终点，变细

			XMFLOAT4 inkColor = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f); // 黑色

			// 开始绘制

			inkRenderer.SetOMTarget();
			inkRenderer.ClearStencil();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

			auto ret = inkRenderer.DrawStroke(strokePoints, inkColor);
			if (ret) Testw(L"DrawStrokeSegment2 执行失败 RET" + to_wstring(ret));

			swapChain->Present(0, 0);
		}
	}

	cerr << "绘制已完成，按任意键关闭……" << endl;

	getmessage(EM_KEY);
	return 0;
}