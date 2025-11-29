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
		};

		D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_HARDWARE,   // 使用 HARDWARE 硬件加速渲染器
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			&d3dDevice_HARDWARE,        // 返回创建的设备
			nullptr,                    // 返回实际的功能级别
			&d3dDeviceContext           // 返回设备上下文
		);
		//d3dDevice_HARDWARE.QueryInterface(&dxgiDevice1);
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

	goto end;

	cerr << "单次绘制对比测试" << endl;
	{
		// 这一部分是测试 GPU 并行绘制大量胶囊
		{
			vector<InkVertex> list;
			{
				float x1 = 100.0f;
				float y1 = 100.0f;
				float r1 = 25.0f;

				float x2 = 500.0f;
				float y2 = 500.0f;
				float r2 = 150.0f;

				for (int i = 1; i <= 1; i++) list.emplace_back(InkVertex(x1, y1, r1, x2, y2, r2, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)));
			}

			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			// 开始绘制
			inkRenderer.SetOMTarget();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

			inkRenderer.DrawStrokeSegment2(list, 0, list.size());

			// 同步本帧完成：目前并非推荐的同步操作，因为我们必须处理超时情况来防止线程卡死
			d3dDeviceContext->End(inkRenderer.g_frameFinishQuery);
			BOOL done = FALSE; // 注意：GetData 会在 GPU 还没执行到这个 Query 时返回 S_FALSE
			while (true)
			{
				HRESULT hr = d3dDeviceContext->GetData(inkRenderer.g_frameFinishQuery,
					&done, sizeof(done),
					D3D11_ASYNC_GETDATA_DONOTFLUSH);

				if (hr == S_OK) break;
				if (hr == S_FALSE)
				{
					// 未完成，稍微 Sleep/等待一会儿，避免空转
					this_thread::sleep_for(1ms);
					continue;
				}

				// 其它错误：检查设备状态
				if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
				{
					break;
				}
				// 其它意外错误，也应该 break 或上报
				break;
			}
			cerr << "dx11 着色器使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		// 这一部分是测试 CPU 计算路径并绘制大量墨迹
		{
			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			for (int i = 1; i <= 1; i++)
			{
				// 绘制一段墨迹（测试）
				// 样式为两端粗细不同的线段，起始端是凸出，而终点端是凹入的，结合切线计算和路径闭合
				// 需要注意的是：当前代码设计之初就没有考虑过内涵的情况，并且不应该会出现这种粗细变化case，需要在后面模拟压感阶段避免

				// 首先需要准备 (x1,y1,r1) (x2,y2,r2) 两个端点位置和半径

				float x1 = 500.0f;
				float y1 = 100.0f;
				float r1 = 25.0f;

				float x2 = 900.0f;
				float y2 = 500.0f;
				float r2 = 150.0f;

				d2dDeviceContext->BeginDraw();
				{
					// 圆心间距离
					double dist = std::hypot(x2 - x1, y2 - y1);

					// 保证不是内涵的情况
					if (dist > abs(r1 - r2))
					{
						// 基准角度
						double base_angle = std::atan2(y2 - y1, x2 - x1);

						// 偏移角 alpha
						// cos(alpha) = (r1 - r2) / d
						// 这里的参数必须在 [-1, 1] 之间，前面的 if 检查保证了这一点
						double alpha = acos((r1 - r2) / dist);

						// 切线角度
						double angle1 = base_angle + alpha;
						double angle2 = base_angle - alpha;

						float tp1x = x1 + r1 * cos(angle1);
						float tp1y = y1 + r1 * sin(angle1);
						float tp2x = x1 + r1 * cos(angle2);
						float tp2y = y1 + r1 * sin(angle2);

						float ep1x = x2 + r2 * cos(angle1);
						float ep1y = y2 + r2 * sin(angle1);
						float ep2x = x2 + r2 * cos(angle2);
						float ep2y = y2 + r2 * sin(angle2);

						// 计算路径

						CComPtr<ID2D1PathGeometry> path;
						CComPtr<ID2D1GeometrySink> sink;
						d2dFactory1->CreatePathGeometry(&path);
						path->Open(&sink);

						sink->BeginFigure(D2D1::Point2F(tp1x, tp1y), D2D1_FIGURE_BEGIN_FILLED);

						// 起始端圆弧
						{
							// 计算绘制的为优弧/劣弧
							D2D1_ARC_SIZE arcSize = (r1 > r2) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

							// 推算绘制线顺/逆时针

							// 2. 判断方向 (SweepDirection)
							// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
							// u = start - center, v = end - center
							float ux = tp1x - x1;
							float uy = tp1y - y1;
							float vx = tp2x - x1;
							float vy = tp2y - y1;

							// 叉积 CP = x1*y2 - x2*y1
							float cp = ux * vy - uy * vx;

							// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
							// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
							// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径

							D2D1_SWEEP_DIRECTION direction;
							if (arcSize == D2D1_ARC_SIZE_SMALL)
							{
								// 如果是小弧，直接走最短路径
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
							}
							else
							{
								// 如果是大弧，我们要走长的那条路，所以取反
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_CLOCKWISE;
							}

							sink->AddArc(D2D1::ArcSegment(
								D2D1::Point2F(tp2x, tp2y),
								D2D1::SizeF(r1, r1),
								0.0f,
								direction,
								arcSize
							));
						}

						sink->AddLine(D2D1::Point2F(ep2x, ep2y));

						// 终点端圆弧
						{
							// 计算绘制的为优弧/劣弧
							D2D1_ARC_SIZE arcSize = (r2 > r1) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

							// 推算绘制线顺/逆时针

							// 2. 判断方向 (SweepDirection)
							// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
							// u = start - center, v = end - center
							float ux = ep2x - x2;
							float uy = ep2y - y2;
							float vx = ep1x - x2;
							float vy = ep1y - y2;

							// 叉积 CP = x1*y2 - x2*y1
							float cp = ux * vy - uy * vx;

							// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
							// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
							// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径
							// 我们需要取反

							D2D1_SWEEP_DIRECTION direction;
							if (arcSize == D2D1_ARC_SIZE_SMALL)
							{
								// 如果是小弧，直接走最短路径
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
							}
							else
							{
								// 如果是大弧，我们要走长的那条路，所以取反
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_CLOCKWISE;
							}

							sink->AddArc(D2D1::ArcSegment(
								D2D1::Point2F(ep1x, ep1y),
								D2D1::SizeF(r2, r2),
								0.0f,
								direction,
								arcSize
							));
						}

						sink->AddLine(D2D1::Point2F(tp1x, tp1y));

						sink->EndFigure(D2D1_FIGURE_END_CLOSED);
						sink->Close();

						// 绘制

						CComPtr<ID2D1SolidColorBrush> strokeBrush;
						d2dDeviceContext->CreateSolidColorBrush(
							D2D1::ColorF(D2D1::ColorF::Red),
							&strokeBrush
						);

						d2dDeviceContext->FillGeometry(path, strokeBrush);
					}
				}
				d2dDeviceContext->EndDraw();
			}

			cerr << "d2d1.1 使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		// 这一部分是测试 GDI+ 绘制大量墨迹
		{
			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			for (int i = 1; i <= 1; i++)
			{
				float x1 = 1000.0f, y1 = 100.0f;
				float x2 = 1400.0f, y2 = 500.0f;
				float w1 = 50.0f, w2 = 300.0f;
				Gdiplus::Color color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);

				// 方向向量
				float dx = x2 - x1;
				float dy = y2 - y1;
				float len = hypot(dx, dy);

				if (len < 1e-6f);
				else
				{
					// 单位法向量
					float nx = -dy / len;
					float ny = dx / len;

					// 半径
					float r1 = w1 / 2.0f;
					float r2 = w2 / 2.0f;

					// 主体梯形四个顶点
					Gdiplus::PointF p1(x1 + nx * r1, y1 + ny * r1); // 起点左
					Gdiplus::PointF p2(x2 + nx * r2, y2 + ny * r2); // 终点左
					Gdiplus::PointF p3(x2 - nx * r2, y2 - ny * r2); // 终点右
					Gdiplus::PointF p4(x1 - nx * r1, y1 - ny * r1); // 起点右

					Gdiplus::GraphicsPath mainPath(Gdiplus::FillModeWinding);
					Gdiplus::PointF trapezoid[] = { p1, p2, p3, p4 };
					mainPath.AddPolygon(trapezoid, 4);

					// 起点凸帽
					{
						Gdiplus::RectF arcRect(x1 - r1, y1 - r1, w1, w1);
						float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
						mainPath.AddArc(arcRect, angle + 90, 180);
						mainPath.CloseFigure();
					}

					// 构造成 Region
					Gdiplus::Region region(&mainPath);

					// 终点凹帽（挖洞）
					{
						Gdiplus::GraphicsPath hole;
						Gdiplus::RectF arcRect(x2 - r2, y2 - r2, w2, w2);
						float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
						hole.AddArc(arcRect, angle - 90, 360); // 顺着线段方向的半圆
						hole.AddArc(arcRect, angle + 90, -180); // 回到起点闭合
						hole.CloseFigure();

						//region.Exclude(&hole); // 从主体中挖掉这个半圆
					}

					Gdiplus::SolidBrush brush(color);
					graphics.FillRegion(&brush, &region);
				}
			}

			cerr << "gdi+ 使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		swapChain->Present(0, 0); // 第一个参数为 1 则开启垂直同
	}

	cerr << "10 万次普通墨迹绘制对比" << endl;
	{
		float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

		// 这一部分是测试 GPU 并行绘制大量胶囊
		{
			vector<InkVertex> list;
			{
				float x1 = 100.0f;
				float y1 = 100.0f;
				float r1 = 3.5f;

				float x2 = 500.0f;
				float y2 = 500.0f;
				float r2 = 5.0;

				for (int i = 1; i <= 100000; i++) list.emplace_back(InkVertex(x1, y1, r1, x2, y2, r2, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)));
			}

			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			// 开始绘制
			inkRenderer.SetOMTarget();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

			inkRenderer.DrawStrokeSegment2(list, 0, list.size());

			// 同步本帧完成：目前并非推荐的同步操作，因为我们必须处理超时情况来防止线程卡死
			d3dDeviceContext->End(inkRenderer.g_frameFinishQuery);
			BOOL done = FALSE; // 注意：GetData 会在 GPU 还没执行到这个 Query 时返回 S_FALSE
			while (true)
			{
				HRESULT hr = d3dDeviceContext->GetData(inkRenderer.g_frameFinishQuery,
					&done, sizeof(done),
					D3D11_ASYNC_GETDATA_DONOTFLUSH);

				if (hr == S_OK) break;
				if (hr == S_FALSE)
				{
					// 未完成，稍微 Sleep/等待一会儿，避免空转
					this_thread::sleep_for(1ms);
					continue;
				}

				// 其它错误：检查设备状态
				if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
				{
					break;
				}
				// 其它意外错误，也应该 break 或上报
				break;
			}

			cerr << "dx11 着色器使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		// 这一部分是测试 CPU 计算路径并绘制大量墨迹
		{
			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			for (int i = 1; i <= 100000; i++)
			{
				// 绘制一段墨迹（测试）
				// 样式为两端粗细不同的线段，起始端是凸出，而终点端是凹入的，结合切线计算和路径闭合
				// 需要注意的是：当前代码设计之初就没有考虑过内涵的情况，并且不应该会出现这种粗细变化case，需要在后面模拟压感阶段避免

				// 首先需要准备 (x1,y1,r1) (x2,y2,r2) 两个端点位置和半径

				float x1 = 500.0f;
				float y1 = 100.0f;
				float r1 = 3.5f;

				float x2 = 900.0f;
				float y2 = 500.0f;
				float r2 = 5.0;

				d2dDeviceContext->BeginDraw();
				{
					// 圆心间距离
					double dist = std::hypot(x2 - x1, y2 - y1);

					// 保证不是内涵的情况
					if (dist > abs(r1 - r2))
					{
						// 基准角度
						double base_angle = std::atan2(y2 - y1, x2 - x1);

						// 偏移角 alpha
						// cos(alpha) = (r1 - r2) / d
						// 这里的参数必须在 [-1, 1] 之间，前面的 if 检查保证了这一点
						double alpha = acos((r1 - r2) / dist);

						// 切线角度
						double angle1 = base_angle + alpha;
						double angle2 = base_angle - alpha;

						float tp1x = x1 + r1 * cos(angle1);
						float tp1y = y1 + r1 * sin(angle1);
						float tp2x = x1 + r1 * cos(angle2);
						float tp2y = y1 + r1 * sin(angle2);

						float ep1x = x2 + r2 * cos(angle1);
						float ep1y = y2 + r2 * sin(angle1);
						float ep2x = x2 + r2 * cos(angle2);
						float ep2y = y2 + r2 * sin(angle2);

						// 计算路径

						CComPtr<ID2D1PathGeometry> path;
						CComPtr<ID2D1GeometrySink> sink;
						d2dFactory1->CreatePathGeometry(&path);
						path->Open(&sink);

						sink->BeginFigure(D2D1::Point2F(tp1x, tp1y), D2D1_FIGURE_BEGIN_FILLED);

						// 起始端圆弧
						{
							// 计算绘制的为优弧/劣弧
							D2D1_ARC_SIZE arcSize = (r1 > r2) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

							// 推算绘制线顺/逆时针

							// 2. 判断方向 (SweepDirection)
							// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
							// u = start - center, v = end - center
							float ux = tp1x - x1;
							float uy = tp1y - y1;
							float vx = tp2x - x1;
							float vy = tp2y - y1;

							// 叉积 CP = x1*y2 - x2*y1
							float cp = ux * vy - uy * vx;

							// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
							// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
							// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径

							D2D1_SWEEP_DIRECTION direction;
							if (arcSize == D2D1_ARC_SIZE_SMALL)
							{
								// 如果是小弧，直接走最短路径
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
							}
							else
							{
								// 如果是大弧，我们要走长的那条路，所以取反
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_CLOCKWISE;
							}

							sink->AddArc(D2D1::ArcSegment(
								D2D1::Point2F(tp2x, tp2y),
								D2D1::SizeF(r1, r1),
								0.0f,
								direction,
								arcSize
							));
						}

						sink->AddLine(D2D1::Point2F(ep2x, ep2y));

						// 终点端圆弧
						{
							// 计算绘制的为优弧/劣弧
							D2D1_ARC_SIZE arcSize = (r2 > r1) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

							// 推算绘制线顺/逆时针

							// 2. 判断方向 (SweepDirection)
							// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
							// u = start - center, v = end - center
							float ux = ep2x - x2;
							float uy = ep2y - y2;
							float vx = ep1x - x2;
							float vy = ep1y - y2;

							// 叉积 CP = x1*y2 - x2*y1
							float cp = ux * vy - uy * vx;

							// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
							// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
							// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径
							// 我们需要取反

							D2D1_SWEEP_DIRECTION direction;
							if (arcSize == D2D1_ARC_SIZE_SMALL)
							{
								// 如果是小弧，直接走最短路径
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
							}
							else
							{
								// 如果是大弧，我们要走长的那条路，所以取反
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_CLOCKWISE;
							}

							sink->AddArc(D2D1::ArcSegment(
								D2D1::Point2F(ep1x, ep1y),
								D2D1::SizeF(r2, r2),
								0.0f,
								direction,
								arcSize
							));
						}

						sink->AddLine(D2D1::Point2F(tp1x, tp1y));

						sink->EndFigure(D2D1_FIGURE_END_CLOSED);
						sink->Close();

						// 绘制

						CComPtr<ID2D1SolidColorBrush> strokeBrush;
						d2dDeviceContext->CreateSolidColorBrush(
							D2D1::ColorF(D2D1::ColorF::Red),
							&strokeBrush
						);

						d2dDeviceContext->FillGeometry(path, strokeBrush);
					}
				}
				d2dDeviceContext->EndDraw();
			}

			cerr << "d2d1.1 使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		// 这一部分是测试 GDI+ 绘制大量墨迹
		{
			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			for (int i = 1; i <= 100000; i++)
			{
				float x1 = 1000.0f, y1 = 100.0f;
				float x2 = 1400.0f, y2 = 500.0f;
				float w1 = 7.0f, w2 = 10.0f;
				Gdiplus::Color color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);

				// 方向向量
				float dx = x2 - x1;
				float dy = y2 - y1;
				float len = hypot(dx, dy);

				if (len < 1e-6f);
				else
				{
					// 单位法向量
					float nx = -dy / len;
					float ny = dx / len;

					// 半径
					float r1 = w1 / 2.0f;
					float r2 = w2 / 2.0f;

					// 主体梯形四个顶点
					Gdiplus::PointF p1(x1 + nx * r1, y1 + ny * r1); // 起点左
					Gdiplus::PointF p2(x2 + nx * r2, y2 + ny * r2); // 终点左
					Gdiplus::PointF p3(x2 - nx * r2, y2 - ny * r2); // 终点右
					Gdiplus::PointF p4(x1 - nx * r1, y1 - ny * r1); // 起点右

					Gdiplus::GraphicsPath mainPath(Gdiplus::FillModeWinding);
					Gdiplus::PointF trapezoid[] = { p1, p2, p3, p4 };
					mainPath.AddPolygon(trapezoid, 4);

					// 起点凸帽
					{
						Gdiplus::RectF arcRect(x1 - r1, y1 - r1, w1, w1);
						float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
						mainPath.AddArc(arcRect, angle + 90, 180);
						mainPath.CloseFigure();
					}

					// 构造成 Region
					Gdiplus::Region region(&mainPath);

					// 终点凹帽（挖洞）
					{
						Gdiplus::GraphicsPath hole;
						Gdiplus::RectF arcRect(x2 - r2, y2 - r2, w2, w2);
						float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
						hole.AddArc(arcRect, angle - 90, 360); // 顺着线段方向的半圆
						hole.AddArc(arcRect, angle + 90, -180); // 回到起点闭合
						hole.CloseFigure();

						//region.Exclude(&hole); // 从主体中挖掉这个半圆
					}

					Gdiplus::SolidBrush brush(color);
					graphics.FillRegion(&brush, &region);
				}
			}

			cerr << "gdi+ 使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		swapChain->Present(0, 0); // 第一个参数为 1 则开启垂直同
	}

end:

	cerr << "10 万次粗墨迹绘制对比" << endl;
	{
		float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

		// 这一部分是测试 GPU 并行绘制大量胶囊
		{
			vector<InkVertex> list;
			{
				float x1 = 100.0f;
				float y1 = 100.0f;
				float r1 = 25.0f;

				float x2 = 500.0f;
				float y2 = 500.0f;
				float r2 = 150.0f;

				float x3 = 900.0f;
				float y3 = 900.0f;
				float r3 = 50.0f;

				for (int i = 1; i <= 50000; i++)
				{
					list.emplace_back(InkVertex(x1, y1, r1, x2, y2, r2, XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)));
					list.emplace_back(InkVertex(x2, y2, r2, x3, y3, r3, XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f)));
				}
			}

			/*
			Testi(1);
			{
				float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
				d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);
				swapChain->Present(0, 0);
			}*/

			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			// 开始绘制
			inkRenderer.SetOMTarget();
			float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			d3dDeviceContext->ClearRenderTargetView(inkRenderer.renderTargetView, clearColor);

			auto ret = inkRenderer.DrawStrokeSegment2(list, 0, list.size());
			if (ret) Testw(L"DrawStrokeSegment2 执行失败 RET" + to_wstring(ret));

			swapChain->Present(0, 0);
			cerr << "绘制任务已经提交，开始等待 GPU 完成" << endl;
		}

		cerr << "绘制已完成，按任意键关闭……" << endl;

		getmessage(EM_KEY);
		return 0;

		// 这一部分是测试 CPU 计算路径并绘制大量墨迹
		{
			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			for (int i = 1; i <= 100000; i++)
			{
				// 绘制一段墨迹（测试）
				// 样式为两端粗细不同的线段，起始端是凸出，而终点端是凹入的，结合切线计算和路径闭合
				// 需要注意的是：当前代码设计之初就没有考虑过内涵的情况，并且不应该会出现这种粗细变化case，需要在后面模拟压感阶段避免

				// 首先需要准备 (x1,y1,r1) (x2,y2,r2) 两个端点位置和半径

				float x1 = 500.0f;
				float y1 = 100.0f;
				float r1 = 25.0f;

				float x2 = 900.0f;
				float y2 = 500.0f;
				float r2 = 150.0f;

				d2dDeviceContext->BeginDraw();
				{
					// 圆心间距离
					double dist = std::hypot(x2 - x1, y2 - y1);

					// 保证不是内涵的情况
					if (dist > abs(r1 - r2))
					{
						// 基准角度
						double base_angle = std::atan2(y2 - y1, x2 - x1);

						// 偏移角 alpha
						// cos(alpha) = (r1 - r2) / d
						// 这里的参数必须在 [-1, 1] 之间，前面的 if 检查保证了这一点
						double alpha = acos((r1 - r2) / dist);

						// 切线角度
						double angle1 = base_angle + alpha;
						double angle2 = base_angle - alpha;

						float tp1x = x1 + r1 * cos(angle1);
						float tp1y = y1 + r1 * sin(angle1);
						float tp2x = x1 + r1 * cos(angle2);
						float tp2y = y1 + r1 * sin(angle2);

						float ep1x = x2 + r2 * cos(angle1);
						float ep1y = y2 + r2 * sin(angle1);
						float ep2x = x2 + r2 * cos(angle2);
						float ep2y = y2 + r2 * sin(angle2);

						// 计算路径

						CComPtr<ID2D1PathGeometry> path;
						CComPtr<ID2D1GeometrySink> sink;
						d2dFactory1->CreatePathGeometry(&path);
						path->Open(&sink);

						sink->BeginFigure(D2D1::Point2F(tp1x, tp1y), D2D1_FIGURE_BEGIN_FILLED);

						// 起始端圆弧
						{
							// 计算绘制的为优弧/劣弧
							D2D1_ARC_SIZE arcSize = (r1 > r2) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

							// 推算绘制线顺/逆时针

							// 2. 判断方向 (SweepDirection)
							// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
							// u = start - center, v = end - center
							float ux = tp1x - x1;
							float uy = tp1y - y1;
							float vx = tp2x - x1;
							float vy = tp2y - y1;

							// 叉积 CP = x1*y2 - x2*y1
							float cp = ux * vy - uy * vx;

							// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
							// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
							// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径

							D2D1_SWEEP_DIRECTION direction;
							if (arcSize == D2D1_ARC_SIZE_SMALL)
							{
								// 如果是小弧，直接走最短路径
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
							}
							else
							{
								// 如果是大弧，我们要走长的那条路，所以取反
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_CLOCKWISE;
							}

							sink->AddArc(D2D1::ArcSegment(
								D2D1::Point2F(tp2x, tp2y),
								D2D1::SizeF(r1, r1),
								0.0f,
								direction,
								arcSize
							));
						}

						sink->AddLine(D2D1::Point2F(ep2x, ep2y));

						// 终点端圆弧
						{
							// 计算绘制的为优弧/劣弧
							D2D1_ARC_SIZE arcSize = (r2 > r1) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

							// 推算绘制线顺/逆时针

							// 2. 判断方向 (SweepDirection)
							// 计算 向量Start(u) 和 向量End(v) 的叉积 (CP)
							// u = start - center, v = end - center
							float ux = ep2x - x2;
							float uy = ep2y - y2;
							float vx = ep1x - x2;
							float vy = ep1y - y2;

							// 叉积 CP = x1*y2 - x2*y1
							float cp = ux * vy - uy * vx;

							// 在 Direct2D 屏幕坐标系中 (Y轴向下)：
							// CP > 0 表示 从 Start 到 End 是 "顺时针" (Clockwise) 的最短路径
							// CP < 0 表示 从 Start 到 End 是 "逆时针" (Counter-Clockwise) 的最短路径
							// 我们需要取反

							D2D1_SWEEP_DIRECTION direction;
							if (arcSize == D2D1_ARC_SIZE_SMALL)
							{
								// 如果是小弧，直接走最短路径
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
							}
							else
							{
								// 如果是大弧，我们要走长的那条路，所以取反
								direction = (cp > 0) ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
									: D2D1_SWEEP_DIRECTION_CLOCKWISE;
							}

							sink->AddArc(D2D1::ArcSegment(
								D2D1::Point2F(ep1x, ep1y),
								D2D1::SizeF(r2, r2),
								0.0f,
								direction,
								arcSize
							));
						}

						sink->AddLine(D2D1::Point2F(tp1x, tp1y));

						sink->EndFigure(D2D1_FIGURE_END_CLOSED);
						sink->Close();

						// 绘制

						CComPtr<ID2D1SolidColorBrush> strokeBrush;
						d2dDeviceContext->CreateSolidColorBrush(
							D2D1::ColorF(D2D1::ColorF::Red),
							&strokeBrush
						);

						d2dDeviceContext->FillGeometry(path, strokeBrush);
					}
				}
				d2dDeviceContext->EndDraw();
			}

			cerr << "d2d1.1 使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		// 这一部分是测试 GDI+ 绘制大量墨迹
		{
			chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();

			for (int i = 1; i <= 100000; i++)
			{
				float x1 = 1000.0f, y1 = 100.0f;
				float x2 = 1400.0f, y2 = 500.0f;
				float w1 = 50.0f, w2 = 300.0f;
				Gdiplus::Color color = hiex::ConvertToGdiplusColor(RGB(255, 0, 0), false);

				// 方向向量
				float dx = x2 - x1;
				float dy = y2 - y1;
				float len = hypot(dx, dy);

				if (len < 1e-6f);
				else
				{
					// 单位法向量
					float nx = -dy / len;
					float ny = dx / len;

					// 半径
					float r1 = w1 / 2.0f;
					float r2 = w2 / 2.0f;

					// 主体梯形四个顶点
					Gdiplus::PointF p1(x1 + nx * r1, y1 + ny * r1); // 起点左
					Gdiplus::PointF p2(x2 + nx * r2, y2 + ny * r2); // 终点左
					Gdiplus::PointF p3(x2 - nx * r2, y2 - ny * r2); // 终点右
					Gdiplus::PointF p4(x1 - nx * r1, y1 - ny * r1); // 起点右

					Gdiplus::GraphicsPath mainPath(Gdiplus::FillModeWinding);
					Gdiplus::PointF trapezoid[] = { p1, p2, p3, p4 };
					mainPath.AddPolygon(trapezoid, 4);

					// 起点凸帽
					{
						Gdiplus::RectF arcRect(x1 - r1, y1 - r1, w1, w1);
						float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
						mainPath.AddArc(arcRect, angle + 90, 180);
						mainPath.CloseFigure();
					}

					// 构造成 Region
					Gdiplus::Region region(&mainPath);

					// 终点凹帽（挖洞）
					{
						Gdiplus::GraphicsPath hole;
						Gdiplus::RectF arcRect(x2 - r2, y2 - r2, w2, w2);
						float angle = atan2(dy, dx) * 180.0f / 3.14159265f;
						hole.AddArc(arcRect, angle - 90, 360); // 顺着线段方向的半圆
						hole.AddArc(arcRect, angle + 90, -180); // 回到起点闭合
						hole.CloseFigure();

						//region.Exclude(&hole); // 从主体中挖掉这个半圆
					}

					Gdiplus::SolidBrush brush(color);
					graphics.FillRegion(&brush, &region);
				}
			}

			cerr << "gdi+ 使用 " << chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count() << "ms" << endl;
		}

		swapChain->Present(0, 0); // 第一个参数为 1 则开启垂直同
	}

	cerr << "绘制已完成，按任意键关闭……" << endl;

	getmessage(EM_KEY);
	return 0;
}