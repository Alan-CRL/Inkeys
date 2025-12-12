#include "main.h"

#include "renderer.h"

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
{
	// 1. 计算目标帧时间 (毫秒)
	// 例如: 60FPS -> 16.666... ms
	double targetFrameTimeMs = 1000.0 / targetFPS;

	// 2. 计算还需要等待的时间 (毫秒)
	double waitTimeMs = targetFrameTimeMs - frameTimeSpentMs;

	// 如果已经超时（掉帧），直接返回，不等待
	if (waitTimeMs <= 0.0)
	{
		return;
	}

	// 获取高精度计时器的频率 (Ticks Per Second)
	static LARGE_INTEGER freq = { 0 };
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

	// 记录开始等待时刻的 QPC
	LARGE_INTEGER startCounter, currentCounter;
	QueryPerformanceCounter(&startCounter);

	// 将等待时间 (ms) 转换为 QPC 的 Ticks 单位
	// 公式: (ms * freq) / 1000
	long long waitTicks = (long long)((waitTimeMs * (double)freq.QuadPart) / 1000.0);
	long long targetEndTick = startCounter.QuadPart + waitTicks;

	// === 阶段一：Sleep (粗略等待) ===
	// 只有当剩余时间大于 2ms 时才启用 Sleep，留出 1.5ms 的安全余量给 Spin
	if (waitTimeMs > 2.0)
	{
		// 预留约 1.5ms 的时间给最后的忙等待，其余时间睡觉
		// 注意这里显式使用 std::milli
		double sleepMs = waitTimeMs - 1.5;
		std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepMs));
	}

	// === 阶段二：Spin (高精度忙等待) ===
	// 死循环直到 QPC 达到目标 Tick
	do
	{
		QueryPerformanceCounter(&currentCounter);

		YieldProcessor();
	} while (currentCounter.QuadPart < targetEndTick);
}
void UnionRectInPlace(RECT& target, const RECT& add)
{
	// 新增矩形无效，直接返回
	if (add.left >= add.right || add.top >= add.bottom) return;
	// target 是空矩形，直接替换
	if (target.left >= target.right || target.top >= target.bottom)
	{
		target = add;
		return;
	}

	target.left = min(target.left, add.left);
	target.top = min(target.top, add.top);
	target.right = max(target.right, add.right);
	target.bottom = max(target.bottom, add.bottom);
}

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// 初始化 D3D 设备
	CComPtr<ID3D11DeviceContext> d3dDeviceContext; // DC
	{
		// 创建 HARDWARE 设备

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
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
	}

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h, EW_SHOWCONSOLE);
	}

	// 从 windows8 开始可以考虑 SwapChain2 的 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT 更适合墨迹输入

	// 常规场景下的墨迹输入应使用 dxgiDevice1::SetMaximumFrameLatency(1) 来确保有一帧的间隙 CPU 处理时间留给 GPU 并行渲染来提高性能
	// dxgiDevice1->SetMaximumFrameLatency(1);

	// 后续性能选项卡中可以提供一个 GPU 高优先级 的选项
	dxgiDevice1->SetGPUThreadPriority(2);

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

	// 每帧绘制前应该
	/*
			inkRenderer.SetOMTarget();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);
	*/

	// 简单的 DPI 初始化
	int dpiX;
	{
		HDC screen = GetDC(nullptr);
		dpiX = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(nullptr, screen);
	}
	// 初始调测参数
	const bool debug = true;
	const float sampling_rate_hz = 30.0f; // Hz
	const float expected_speed = 500.0f * (static_cast<float>(dpiX) / 96.0f); // DPI 期望速度
	const float limited_speed = expected_speed * 3.0f; // 最高允许速度
	const int strokes_num = static_cast<int>(sampling_rate_hz / 6.0f); // 笔锋点个数
	// 模型初始化
	KalmanPredictorParams kalman_predictor_params;
	{
		kalman_predictor_params.process_noise = 0.05;
		kalman_predictor_params.measurement_noise = 0.01;
		kalman_predictor_params.min_stable_iteration = 4;
		kalman_predictor_params.max_time_samples = 20;
		kalman_predictor_params.min_catchup_velocity = expected_speed / 1000.0f;
		kalman_predictor_params.acceleration_weight = 0.5f;
		kalman_predictor_params.jerk_weight = 0.1f;
		kalman_predictor_params.prediction_interval = Duration(0.2);
		kalman_predictor_params.confidence_params = {
			.desired_number_of_samples = 10,
			.max_estimation_distance = 1.5f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.min_travel_speed = 0.05f * expected_speed,
			.max_travel_speed = 0.25f * expected_speed,
			.max_linear_deviation = 10.0f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.baseline_linearity_confidence = 0.4f
		};
	}
	StrokeModelParams params{
		.wobble_smoother_params{
			.is_enabled = false,
			.timeout = Duration(2.5 / sampling_rate_hz),
			.speed_floor = 0.02f * expected_speed,
			.speed_ceiling = 0.03f * expected_speed
		},
		.position_modeler_params{
			.spring_mass_constant = 11.f / 32400,
			.drag_constant = 72.f
		},
		.sampling_params{
			.min_output_rate = 3.0f * sampling_rate_hz,
			.end_of_stroke_stopping_distance = .001,
			.end_of_stroke_max_iterations = 20,
			.max_outputs_per_call = 2000
		},
	};
	StrokeModeler modeler;

	ExMessage m{};
	while (true)
	{
		hiex::getmessage_win32(&m, EM_MOUSE, windowHWND);

		if (m.message == WM_LBUTTONDOWN)
		{
			// 检查设备是否丢失，并重建
			// TODO

			RECT current = RECT(0, 0, 0, 0);
			bool isFirstFrame = true;

			params.prediction_params = kalman_predictor_params;
			//params.prediction_params = StrokeEndPredictorParams();

			if (absl::Status status = modeler.Reset(params); !status.ok())
			{
				cout << "Error: " << status.message() << endl;
			}

			vector<Result> smoothed_stroke;
			vector<Result> predicted_stroke;
			size_t tot = 0;

			float xO = m.x;
			float yO = m.y;

			float xT = m.x;
			float yT = m.y;

			chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();

			Input input
			{
				.event_type = Input::EventType::kDown,
				.position = ink::stroke_model::Vec2(xO,yO),
				.time = Time(0.0)
			};
			modeler.Update(input, smoothed_stroke);

			double baseThickness = 5.0;
			double minThickness = baseThickness * 0.8; // 0.6/2.4 或 0.4/2.0
			double maxThickness = baseThickness * 1.4;
			double prevThickness = baseThickness;
			double smoothingFactor = 0.2;

			// 清空画布
			inkRenderer.ClearRTV(inkRenderer.offScreenTexture1RTV, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

			// 帧率保持
			chrono::high_resolution_clock::time_point rekon;
			while (1)
			{
				rekon = chrono::high_resolution_clock::now();

				inkRenderer.SetOMTarget(inkRenderer.offScreenTexture1RTV);

				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(windowHWND, &pt);

				Input input
				{
					.event_type = Input::EventType::kMove,
					.position = ink::stroke_model::Vec2(pt.x, pt.y),
					.time = Time(chrono::duration<double>(chrono::high_resolution_clock::now() - start).count()) // 秒单位
				};
				vector<InkPoint> dryStroke;

				modeler.Update(input, smoothed_stroke);
				modeler.Predict(predicted_stroke);
				if (!smoothed_stroke.empty() && (xO != smoothed_stroke.back().position.x || yO != smoothed_stroke.back().position.y))
				{
					// 用于粗细平滑
					float xI = xO;
					float yI = yO;

					for (size_t i = tot; i < smoothed_stroke.size(); i++)
					{
						bool isStroke = false;
						if (smoothed_stroke.size() - tot <= strokes_num) isStroke = true;

						if (!isStroke) tot = i;

						/*graphics.DrawLine(&pen,
							smoothed_stroke[i].position.x,
							smoothed_stroke[i].position.y,
							smoothed_stroke[i + 1].position.x,
							smoothed_stroke[i + 1].position.y);*/

						auto rawSpeed = hypot(smoothed_stroke[i].velocity.x, smoothed_stroke[i].velocity.y);
						double ratio = clamp(static_cast<double>(rawSpeed / expected_speed), 0.0, 1.0);
						double targetThickness = minThickness + (1.0 - ratio) * (maxThickness - minThickness);
						double thickness = prevThickness;

						if (hypot(smoothed_stroke[i].position.x - xI, smoothed_stroke[i].position.y - yI) >= baseThickness)
						{
							thickness = std::lerp(prevThickness, targetThickness, smoothingFactor);
							xI = smoothed_stroke[i].position.x;
							yI = smoothed_stroke[i].position.y;
						}

						// cout << "= " << rawSpeed << ":" << ratio << ", " << thickness << endl;

						{
							float x1 = smoothed_stroke[i].position.x, y1 = smoothed_stroke[i].position.y;
							float w1 = static_cast<float>(prevThickness);

							dryStroke.emplace_back(x1, y1, w1 / 2.0f, 0.0f);

							UnionRectInPlace(current, RECT(x1 - w1, y1 - w1, x1 + w1, y1 + w1));
						}

						prevThickness = thickness;
					}
				}
				inkRenderer.DrawStroke(dryStroke, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));

				if (!predicted_stroke.empty())
				{
					// TODO
				}

				// 拷贝2D目标至窗口婚宠
				{
					inkRenderer.SetOMTarget(inkRenderer.renderTargetView);

					if (!isFirstFrame)
					{
						//inkRenderer.ClearRTV(inkRenderer.renderTargetView, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f)); // DEBUG

						inkRenderer.CopyResource(inkRenderer.screenTexture, inkRenderer.offScreenTexture1, current);
					}
					else
					{
						inkRenderer.context->CopyResource(inkRenderer.screenTexture, inkRenderer.offScreenTexture1);
					}
				}

				// 帧结束
				{
					RECT dirtyRect = current;

					dirtyRect.left = max(0L, dirtyRect.left);
					dirtyRect.top = max(0L, dirtyRect.top);
					dirtyRect.right = min((long)windowInfo.w, dirtyRect.right);
					dirtyRect.bottom = min((long)windowInfo.h, dirtyRect.bottom);

					if (!isFirstFrame && dirtyRect.right > dirtyRect.left && dirtyRect.bottom > dirtyRect.top)
					{
						DXGI_PRESENT_PARAMETERS parameters = {};
						parameters.DirtyRectsCount = 1;
						parameters.pDirtyRects = &dirtyRect;
						parameters.pScrollRect = nullptr;
						parameters.pScrollOffset = nullptr;

						swapChain->Present1(0, 0, &parameters);
					}
					else
					{
						swapChain->Present(0, 0);
					}
				}

				if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) break;
				hiex::flushmessage_win32(EM_MOUSE, windowHWND);

				isFirstFrame = true;
				// 脏区逻辑暂时禁用

				// 同步锁
				{
					inkRenderer.SyncFrameLatency(50.0);
				}
				// 帧率锁
				{
					double costMs = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - rekon).count();

					// 直接传入 ms，无需转换
					HighPrecisionWait(costMs, sampling_rate_hz);

					// 计算总帧时间用于显示实际 FPS
					double totalMs = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - rekon).count();

					// 防止除以0
					int logicFPS = (costMs > 0.001) ? static_cast<int>(1000.0 / costMs) : 9999;
					int actualFPS = (totalMs > 0.001) ? static_cast<int>(1000.0 / totalMs) : 9999;

					cout << tot
						<< " logic: " << logicFPS << " FPS (" << costMs << "ms)"
						<< " real: " << actualFPS << " FPS"
						<< endl;
				}
			}

			{
			}

			hiex::flushmessage_win32(EM_MOUSE, windowHWND);
		}
	}

	getmessage(EM_KEY);
	return 0;
}