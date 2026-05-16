#include "main.h"

#include "renderer.h"
#include <atomic>

WindowInfoClass windowInfo;
InkRenderer inkRenderer;

namespace
{
	std::atomic<bool> g_clearCanvasRequested = false;
	std::atomic<int> g_brushShapeType = 0; // 0: 原来的画笔

	enum class StrokeTimingProfileId
	{
		Fps30,  // 30 FPS: 老 Win7/低功耗档；补齐 180 点/秒，每帧约 6 点，防抖窗口约 83ms。
		Fps60,  // 60 FPS: 默认兼容档；补齐 240 点/秒，每帧约 4 点，防抖窗口约 42ms。
		Fps120, // 120 FPS: 当前高质量测试档；补齐 360 点/秒，每帧约 3 点，防抖窗口约 21ms。
		Fps240  // 240 FPS: 高刷/高端档；补齐 480 点/秒，每帧约 2 点，防抖窗口约 10ms。
	};

	struct StrokeTimingProfile
	{
		double target_fps; // 主循环目标 FPS，传给 HighPrecisionWait。
		double min_output_rate; // 模型最少输出点数/秒，低于输入频率时用于补齐点密度。
		double live_tail_duration_seconds; // 实时笔锋保留的尾部时间，当前先换算成尾部点数。
		double prediction_interval_seconds; // Kalman 预测最大前瞻时间；实际预测长度还会乘置信度。
		int kalman_desired_number_of_samples; // Kalman 样本数置信度达到 1.0 所需的输入点数。
		int kalman_max_time_samples; // Kalman 保存的最近时间戳数量，用于修正非均匀输入间隔。
		double wobble_timeout_seconds; // 防抖移动平均窗口，按约 2.5 个输入间隔设置。
		float wobble_speed_floor_ratio; // 低于 expected_speed 的该比例时防抖最强。
		float wobble_speed_ceiling_ratio; // 高于 expected_speed 的该比例时基本不防抖。
		int max_outputs_per_call; // 单次 Update/Predict 最多输出点数，防止长时间卡顿后爆量补点。
	};

	constexpr StrokeTimingProfileId kDefaultStrokeTimingProfileId = StrokeTimingProfileId::Fps60;
	constexpr StrokeTimingProfileId kActiveStrokeTimingProfileId = StrokeTimingProfileId::Fps120; // 当前测试先使用 120 FPS。

	StrokeTimingProfile GetStrokeTimingProfile(StrokeTimingProfileId profileId = kDefaultStrokeTimingProfileId)
	{
		switch (profileId)
		{
		case StrokeTimingProfileId::Fps30:
			return {
				.target_fps = 30.0,
				.min_output_rate = 180.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 30.0,
				.kalman_desired_number_of_samples = 4,
				.kalman_max_time_samples = 5,
				.wobble_timeout_seconds = 2.5 / 30.0,
				.wobble_speed_floor_ratio = 0.02f,
				.wobble_speed_ceiling_ratio = 0.03f,
				.max_outputs_per_call = 2000
			};
		case StrokeTimingProfileId::Fps60:
			return {
				.target_fps = 60.0,
				.min_output_rate = 240.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 45.0,
				.kalman_desired_number_of_samples = 5,
				.kalman_max_time_samples = 10,
				.wobble_timeout_seconds = 2.5 / 60.0,
				.wobble_speed_floor_ratio = 0.02f,
				.wobble_speed_ceiling_ratio = 0.03f,
				.max_outputs_per_call = 2000
			};
		case StrokeTimingProfileId::Fps120:
			return {
				.target_fps = 120.0,
				.min_output_rate = 360.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 60.0,
				.kalman_desired_number_of_samples = 10,
				.kalman_max_time_samples = 20,
				.wobble_timeout_seconds = 2.5 / 120.0,
				.wobble_speed_floor_ratio = 0.02f,
				.wobble_speed_ceiling_ratio = 0.03f,
				.max_outputs_per_call = 2000
			};
		case StrokeTimingProfileId::Fps240:
			return {
				.target_fps = 240.0,
				.min_output_rate = 480.0,
				.live_tail_duration_seconds = 0.055,
				.prediction_interval_seconds = 1.0 / 120.0,
				.kalman_desired_number_of_samples = 20,
				.kalman_max_time_samples = 40,
				.wobble_timeout_seconds = 2.5 / 240.0,
				.wobble_speed_floor_ratio = 0.02f,
				.wobble_speed_ceiling_ratio = 0.03f,
				.max_outputs_per_call = 2000
			};
		default:
			return GetStrokeTimingProfile();
		}
	}

	const char* GetDriverTypeName(D3D_DRIVER_TYPE driverType)
	{
		switch (driverType)
		{
		case D3D_DRIVER_TYPE_HARDWARE:
			return "Hardware";
		case D3D_DRIVER_TYPE_WARP:
			return "WARP";
		default:
			return "Unknown";
		}
	}

	const char* GetFeatureLevelName(D3D_FEATURE_LEVEL featureLevel)
	{
		switch (featureLevel)
		{
		case D3D_FEATURE_LEVEL_11_1:
			return "11_1";
		case D3D_FEATURE_LEVEL_11_0:
			return "11_0";
		case D3D_FEATURE_LEVEL_10_1:
			return "10_1";
		case D3D_FEATURE_LEVEL_10_0:
			return "10_0";
		case D3D_FEATURE_LEVEL_9_3:
			return "9_3";
		case D3D_FEATURE_LEVEL_9_2:
			return "9_2";
		case D3D_FEATURE_LEVEL_9_1:
			return "9_1";
		default:
			return "Unknown";
		}
	}

	HRESULT CreateD3D11DeviceWithCompatibleFeatureLevels(
		D3D_DRIVER_TYPE driverType,
		UINT creationFlags,
		CComPtr<ID3D11Device>& device,
		D3D_FEATURE_LEVEL& actualFeatureLevel,
		CComPtr<ID3D11DeviceContext>& deviceContext)
	{
		static const D3D_FEATURE_LEVEL preferredFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		static const D3D_FEATURE_LEVEL fallbackFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_0,
		};

		device.Release();
		deviceContext.Release();

		HRESULT hr = D3D11CreateDevice(
			nullptr,
			driverType,
			nullptr,
			creationFlags,
			preferredFeatureLevels,
			ARRAYSIZE(preferredFeatureLevels),
			D3D11_SDK_VERSION,
			&device,
			&actualFeatureLevel,
			&deviceContext
		);
		if (hr == E_INVALIDARG)
		{
			device.Release();
			deviceContext.Release();

			hr = D3D11CreateDevice(
				nullptr,
				driverType,
				nullptr,
				creationFlags,
				fallbackFeatureLevels,
				ARRAYSIZE(fallbackFeatureLevels),
				D3D11_SDK_VERSION,
				&device,
				&actualFeatureLevel,
				&deviceContext
			);
		}

		return hr;
	}
}

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

LRESULT CALLBACK Draw3WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case '0':
		case VK_NUMPAD0:
			g_clearCanvasRequested.store(true, std::memory_order_relaxed);
			return 0;

		case '1':
		case VK_NUMPAD1:
			g_brushShapeType.store(0, std::memory_order_relaxed);
			return 0;
		}
		break;
	}

	return HIWINDOW_DEFAULT_PROC;
}

int main()
{
	timeBeginPeriod(1); // 全局高精度计时器

	// 窗口创建
	{
		windowHWND = hiex::initgraph_win32(windowInfo.w, windowInfo.h, EW_SHOWCONSOLE, _T(""), Draw3WndProc);
	}

	// 初始化 D3D 设备
	CComPtr<ID3D11DeviceContext> d3dDeviceContext; // DC
	{
		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL actualFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		D3D_DRIVER_TYPE activeDriverType = D3D_DRIVER_TYPE_UNKNOWN;
		HRESULT hr = S_OK;

		hr = CreateD3D11DeviceWithCompatibleFeatureLevels(
			D3D_DRIVER_TYPE_HARDWARE,
			creationFlags,
			d3dDevice_HARDWARE,
			actualFeatureLevel,
			d3dDeviceContext
		);
		if (FAILED(hr))
		{
			cout << "Hardware device initialization failed. Falling back to WARP." << endl;

			hr = CreateD3D11DeviceWithCompatibleFeatureLevels(
				D3D_DRIVER_TYPE_WARP,
				creationFlags,
				d3dDevice_HARDWARE,
				actualFeatureLevel,
				d3dDeviceContext
			);

			if (FAILED(hr))
			{
				cout << "Failed to initialize a D3D11 device with both Hardware and WARP." << endl;
				return -1;
			}

			activeDriverType = D3D_DRIVER_TYPE_WARP;
		}
		else
		{
			activeDriverType = D3D_DRIVER_TYPE_HARDWARE;
		}

		cout << "Current D3D device: " << GetDriverTypeName(activeDriverType) << endl;
		cout << "D3D feature level: " << GetFeatureLevelName(actualFeatureLevel) << endl;

		hr = d3dDevice_HARDWARE->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice1));
		if (FAILED(hr))
		{
			cout << "Failed to query IDXGIDevice1 from the D3D11 device." << endl;
			return -1;
		}
	}

	// 从 windows8 开始可以考虑 SwapChain2 的 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT 更适合墨迹输入

	// 常规场景下的墨迹输入应使用 dxgiDevice1::SetMaximumFrameLatency(1) 来确保有一帧的间隙 CPU 处理时间留给 GPU 并行渲染来提高性能
	dxgiDevice1->SetMaximumFrameLatency(1);

	// 后续性能选项卡中可以提供一个 GPU 高优先级 的选项
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
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
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
		//DXGI_RGBA color = { 1.0f, 1.0f, 1.0f, 1.0f };
		//swapChain->SetBackgroundColor(&color);
	}

	// 交换链应该保证指定脏区，而不是全部重绘
	// 后续修改，非 flip_discard

	inkRenderer.Init(d3dDevice_HARDWARE, d3dDeviceContext, swapChain);
	inkRenderer.SetScreenSize((float)windowInfo.w, (float)windowInfo.h);

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
	const StrokeTimingProfile timingProfile = GetStrokeTimingProfile(kActiveStrokeTimingProfileId);
	const float expected_speed = 500.0f * (static_cast<float>(dpiX) / 96.0f); // DPI 期望速度
	const float limited_speed = expected_speed * 3.0f; // 最高允许速度
	const int strokes_num = static_cast<int>(timingProfile.live_tail_duration_seconds * timingProfile.min_output_rate + 0.5); // 笔锋尾部点数，对应 live_tail_duration_seconds
	// 模型初始化
	KalmanPredictorParams kalman_predictor_params;
	{
		kalman_predictor_params.process_noise = 0.05;
		kalman_predictor_params.measurement_noise = 0.01;
		kalman_predictor_params.min_stable_iteration = 4;
		kalman_predictor_params.max_time_samples = timingProfile.kalman_max_time_samples;
		kalman_predictor_params.min_catchup_velocity = expected_speed / 1000.0f;
		kalman_predictor_params.acceleration_weight = 0.5f;
		kalman_predictor_params.jerk_weight = 0.1f;
		kalman_predictor_params.prediction_interval = Duration(timingProfile.prediction_interval_seconds);
		kalman_predictor_params.confidence_params = {
			.desired_number_of_samples = timingProfile.kalman_desired_number_of_samples,
			.max_estimation_distance = 1.5f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.min_travel_speed = 0.05f * expected_speed,
			.max_travel_speed = 0.25f * expected_speed,
			.max_linear_deviation = 10.0f * static_cast<float>(kalman_predictor_params.measurement_noise),
			.baseline_linearity_confidence = 0.4f
		};
	}
	StrokeModelParams params{
		.wobble_smoother_params{
			.is_enabled = true,
			.timeout = Duration(timingProfile.wobble_timeout_seconds),
			.speed_floor = timingProfile.wobble_speed_floor_ratio * expected_speed,
			.speed_ceiling = timingProfile.wobble_speed_ceiling_ratio * expected_speed
		},
		.position_modeler_params{
			.spring_mass_constant = 11.f / 32400,
			.drag_constant = 72.f
		},
		.sampling_params{
			.min_output_rate = timingProfile.min_output_rate,
			.end_of_stroke_stopping_distance = .001f,
			.end_of_stroke_max_iterations = 20,
			.max_outputs_per_call = timingProfile.max_outputs_per_call
		},
	};
	StrokeModeler modeler;

	auto clearCanvas = [&swapChain]()
		{
			const XMFLOAT4 finalCanvasClearColor(1.0f, 1.0f, 1.0f, 1.0f);
			const XMFLOAT4 activeDryClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			inkRenderer.ClearRTV(inkRenderer.finalCanvasRTV, finalCanvasClearColor);
			inkRenderer.ClearRTV(inkRenderer.offScreenTexture1RTV, activeDryClearColor);
			inkRenderer.ClearRTV(inkRenderer.renderTargetView, finalCanvasClearColor);
			swapChain->Present(0, 0);
		};

	clearCanvas();

	ExMessage m{};
	while (true)
	{
		if (g_clearCanvasRequested.exchange(false, std::memory_order_relaxed))
		{
			clearCanvas();
		}

		if (!hiex::peekmessage_win32(&m, EM_MOUSE, true, windowHWND))
		{
			Sleep(1);
			continue;
		}

		if (m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN)
		{
			bool eraser = (m.message == WM_RBUTTONDOWN) ? true : false;
			eraser = false;

			// 检查设备是否丢失，并重建
			// TODO

			RECT current = RECT(0, 0, 0, 0);
			RECT strokeDirty = RECT(0, 0, 0, 0);
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
			if (eraser) baseThickness = 50.0;

			double minThickness = baseThickness * 0.8; // 0.6/2.4 或 0.4/2.0
			double maxThickness = baseThickness * 1.4;
			double prevThickness = baseThickness;
			double smoothingFactor = 0.2;

			// 帧率保持
			chrono::high_resolution_clock::time_point rekon;
			while (1)
			{
				if (g_clearCanvasRequested.exchange(false, std::memory_order_relaxed))
				{
					clearCanvas();
				}

				rekon = chrono::high_resolution_clock::now();
				current = RECT(0, 0, 0, 0);

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
				inkRenderer.DrawStroke(
					dryStroke,
					XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f),
					static_cast<float>(g_brushShapeType.load(std::memory_order_relaxed)),
					eraser
				);

				if (!predicted_stroke.empty())
				{
					// TODO
				}

				// 处理脏区到屏幕范围
				{
					current.left = max(0L, current.left);
					current.top = max(0L, current.top);
					current.right = min((long)windowInfo.w, current.right);
					current.bottom = min((long)windowInfo.h, current.bottom);

					if (current.right < current.left || current.bottom < current.top)
					{
						current = RECT(0, 0, 0, 0);
					}
				}
				UnionRectInPlace(strokeDirty, current);

				if (current.left != 0 || current.top != 0 || current.right != 0 || current.bottom != 0)
				{
					// 拷贝2D目标至窗口缓冲
					{
						inkRenderer.SetOMTarget(inkRenderer.renderTargetView);

						if (!isFirstFrame)
						{
							//inkRenderer.ClearRTV(inkRenderer.renderTargetView, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f)); // DEBUG
							inkRenderer.CopyResource(inkRenderer.screenTexture, inkRenderer.finalCanvasTexture, current);
						}
						else
						{
							inkRenderer.context->CopyResource(inkRenderer.screenTexture, inkRenderer.finalCanvasTexture);
						}
						inkRenderer.AlphaBlendResource(inkRenderer.renderTargetView, inkRenderer.offScreenTexture1SRV, current);
					}

					// 帧结束
					{
						if (!isFirstFrame)
						{
							DXGI_PRESENT_PARAMETERS parameters = {};
							parameters.DirtyRectsCount = 1;
							parameters.pDirtyRects = &current;
							parameters.pScrollRect = nullptr;
							parameters.pScrollOffset = nullptr;

							swapChain->Present1(0, 0, &parameters);
						}
						else
						{
							swapChain->Present(0, 0);
						}
					}
					isFirstFrame = false;
				}

				if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) break;
				hiex::flushmessage_win32(EM_MOUSE, windowHWND);

				// 帧率锁
				{
					double costMs = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - rekon).count();

					// 直接传入 ms，无需转换
					HighPrecisionWait(costMs, timingProfile.target_fps);

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
				if (strokeDirty.left < strokeDirty.right && strokeDirty.top < strokeDirty.bottom)
				{
					inkRenderer.AlphaBlendResource(inkRenderer.finalCanvasRTV, inkRenderer.offScreenTexture1SRV, strokeDirty);
					inkRenderer.ClearRTV(inkRenderer.offScreenTexture1RTV, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));
				}
			}

			hiex::flushmessage_win32(EM_MOUSE, windowHWND);
		}
	}

	getmessage(EM_KEY);
	return 0;
}
