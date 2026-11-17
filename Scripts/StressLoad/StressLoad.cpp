// Inkeys 手动压力工具：打开即持续产生 CPU/GPU 负载，关闭窗口即停止。
// 只用于人工测试画布在高负载下的帧率与延迟；不做熔断/故障注入，不读写 Inkeys 配置或日志。
// 构建：Scripts/Start-StressLoad.ps1（cl /std:c++20 /O2 /EHsc /MT /utf-8）。

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
	constexpr int kMinGpuLevel = 1;
	constexpr int kMaxGpuLevel = 64;
	constexpr int kDefaultGpuLevel = 8;
	constexpr double kTdrGuardMilliseconds = 500.0; // 单帧超过此值自动降一档，远低于 2 s TDR。
	constexpr UINT kStatusTimerId = 1;
	constexpr UINT kStatusIntervalMs = 250;
	constexpr UINT kMaxFramesInFlight = 2;

	template <typename T>
	void SafeRelease(T*& p) noexcept
	{
		if (p) { p->Release(); p = nullptr; }
	}

	struct SharedState
	{
		std::atomic<bool> exit = false;
		std::atomic<bool> paused = false;
		std::atomic<bool> cpuEnabled = true;
		std::atomic<bool> gpuEnabled = true;
		std::atomic<int> cpuThreads = 0;
		std::atomic<int> gpuLevel = kDefaultGpuLevel;
		int cpuMax = 1;
		int rtWidth = 0;
		int rtHeight = 0;

		// GPU 线程写、UI 线程读的实测数据。
		std::atomic<double> gpuFrameMs = 0.0;
		std::atomic<double> gpuFps = 0.0;
		std::atomic<uint64_t> gpuFrames = 0;
		std::atomic<uint64_t> tdrGuardDrops = 0;
		std::atomic<uint64_t> cpuIterations = 0;

		std::mutex textMutex;
		std::wstring adapterName = L"(初始化中)";
		std::wstring gpuStatus = L"初始化中";
	};

	SharedState g_state;

	void SetGpuStatus(const std::wstring& text)
	{
		std::scoped_lock lock(g_state.textMutex);
		g_state.gpuStatus = text;
	}

	// ---------------- CPU ----------------

	void CpuWorker(int index)
	{
		// 每个线程常驻；序号超出当前线程数时休眠，避免反复创建线程。
		double a = 1.0 + index * 0.001, b = 0.5, c = 0.25;
		while (!g_state.exit.load(std::memory_order_relaxed))
		{
			const bool active = !g_state.paused.load(std::memory_order_relaxed) &&
				g_state.cpuEnabled.load(std::memory_order_relaxed) &&
				index < g_state.cpuThreads.load(std::memory_order_relaxed);
			if (!active)
			{
				Sleep(20);
				continue;
			}
			// 约数毫秒一批的纯浮点运算，不 Sleep；批间检查状态以便及时停止。
			for (int i = 0; i < 200000; ++i)
			{
				a = std::sin(a) * b + c;
				b = std::sqrt(b * b + 0.5) - 0.25;
				c = a * 0.999 + b * 0.001;
			}
			g_state.cpuIterations.fetch_add(1, std::memory_order_relaxed);
		}
		// 防止编译器把循环整体优化掉。
		if (a == 12345.678) OutputDebugStringW(L"");
	}

	// ---------------- GPU ----------------

	const char kShaderSource[] = R"(
cbuffer Params : register(b0)
{
	float drawIndex;
	float iterations;
	float2 invSize;
};

float4 VsMain(uint id : SV_VertexID) : SV_Position
{
	// 全屏三角形。
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PsMain(float4 pos : SV_Position) : SV_Target
{
	float2 p = pos.xy * invSize + drawIndex * 0.013;
	float3 acc = float3(p, drawIndex * 0.1);
	[loop]
	for (int i = 0; i < (int)iterations; ++i)
	{
		acc = sin(acc * 1.37 + acc.yzx) * 0.73 + cos(acc.zxy * 0.91) * 0.27;
		acc = mad(acc, float3(1.01, 0.99, 1.003), float3(0.001, -0.002, 0.0005));
	}
	return float4(saturate(acc * 0.5 + 0.5), 1);
}
)";

	// 每次绘制的固定迭代数：单次绘制耗时较小，强度只通过“每帧绘制次数”调节。
	constexpr float kPixelIterations = 24.0f;

	struct alignas(16) GpuParams
	{
		float drawIndex;
		float iterations;
		float invSize[2];
	};

	struct FrameQueries
	{
		ID3D11Query* done = nullptr;      // 事件查询：限制在途帧数。
		ID3D11Query* disjoint = nullptr;  // 时间戳频率与有效性。
		ID3D11Query* begin = nullptr;
		ID3D11Query* end = nullptr;
	};

	struct GpuContext
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		ID3D11Texture2D* target = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		ID3D11VertexShader* vs = nullptr;
		ID3D11PixelShader* ps = nullptr;
		ID3D11Buffer* params = nullptr;
		FrameQueries queries[kMaxFramesInFlight + 1] = {};

		~GpuContext()
		{
			for (auto& q : queries)
			{
				SafeRelease(q.done);
				SafeRelease(q.disjoint);
				SafeRelease(q.begin);
				SafeRelease(q.end);
			}
			SafeRelease(params);
			SafeRelease(ps);
			SafeRelease(vs);
			SafeRelease(rtv);
			SafeRelease(target);
			if (context) context->ClearState();
			SafeRelease(context);
			SafeRelease(device);
		}
	};

	std::wstring FormatHr(const wchar_t* what, HRESULT hr)
	{
		wchar_t buffer[160];
		swprintf_s(buffer, L"%s 失败 hr=0x%08X", what, static_cast<unsigned>(hr));
		return buffer;
	}

	bool CompileShader(const char* entry, const char* profile, ID3DBlob** blob, std::wstring& error)
	{
		ID3DBlob* errors = nullptr;
		const HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "StressLoad", nullptr, nullptr,
			entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
		if (FAILED(hr))
		{
			error = FormatHr(L"D3DCompile", hr);
			if (errors)
			{
				const char* text = static_cast<const char*>(errors->GetBufferPointer());
				error += L" ";
				error.append(text, text + strnlen(text, 200));
			}
			SafeRelease(errors);
			return false;
		}
		SafeRelease(errors);
		return true;
	}

	bool InitGpu(GpuContext& gpu, std::wstring& error)
	{
		const D3D_FEATURE_LEVEL levels[] = {
			D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &gpu.device, nullptr, &gpu.context);
		if (hr == E_INVALIDARG) // 旧运行时不认识 11_1。
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
				levels + 1, ARRAYSIZE(levels) - 1, D3D11_SDK_VERSION, &gpu.device, nullptr, &gpu.context);
		if (FAILED(hr)) { error = FormatHr(L"D3D11CreateDevice", hr); return false; }

		IDXGIDevice* dxgiDevice = nullptr;
		IDXGIAdapter* adapter = nullptr;
		if (SUCCEEDED(gpu.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
			SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
		{
			DXGI_ADAPTER_DESC desc = {};
			if (SUCCEEDED(adapter->GetDesc(&desc)))
			{
				std::scoped_lock lock(g_state.textMutex);
				g_state.adapterName = desc.Description;
			}
		}
		SafeRelease(adapter);
		SafeRelease(dxgiDevice);

		D3D11_TEXTURE2D_DESC td = {};
		td.Width = static_cast<UINT>(g_state.rtWidth);
		td.Height = static_cast<UINT>(g_state.rtHeight);
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET;
		hr = gpu.device->CreateTexture2D(&td, nullptr, &gpu.target);
		if (FAILED(hr)) { error = FormatHr(L"CreateTexture2D", hr); return false; }
		hr = gpu.device->CreateRenderTargetView(gpu.target, nullptr, &gpu.rtv);
		if (FAILED(hr)) { error = FormatHr(L"CreateRenderTargetView", hr); return false; }

		ID3DBlob* vsBlob = nullptr;
		ID3DBlob* psBlob = nullptr;
		if (!CompileShader("VsMain", "vs_4_0", &vsBlob, error)) return false;
		if (!CompileShader("PsMain", "ps_4_0", &psBlob, error)) { SafeRelease(vsBlob); return false; }
		hr = gpu.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &gpu.vs);
		if (SUCCEEDED(hr))
			hr = gpu.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &gpu.ps);
		SafeRelease(vsBlob);
		SafeRelease(psBlob);
		if (FAILED(hr)) { error = FormatHr(L"CreateShader", hr); return false; }

		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(GpuParams);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		hr = gpu.device->CreateBuffer(&bd, nullptr, &gpu.params);
		if (FAILED(hr)) { error = FormatHr(L"CreateBuffer", hr); return false; }

		const D3D11_QUERY_DESC eventDesc = { D3D11_QUERY_EVENT, 0 };
		const D3D11_QUERY_DESC disjointDesc = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
		const D3D11_QUERY_DESC timestampDesc = { D3D11_QUERY_TIMESTAMP, 0 };
		for (auto& q : gpu.queries)
		{
			hr = gpu.device->CreateQuery(&eventDesc, &q.done);
			if (SUCCEEDED(hr)) hr = gpu.device->CreateQuery(&disjointDesc, &q.disjoint);
			if (SUCCEEDED(hr)) hr = gpu.device->CreateQuery(&timestampDesc, &q.begin);
			if (SUCCEEDED(hr)) hr = gpu.device->CreateQuery(&timestampDesc, &q.end);
			if (FAILED(hr)) { error = FormatHr(L"CreateQuery", hr); return false; }
		}

		D3D11_VIEWPORT vp = { 0, 0, static_cast<float>(g_state.rtWidth), static_cast<float>(g_state.rtHeight), 0, 1 };
		gpu.context->RSSetViewports(1, &vp);
		gpu.context->OMSetRenderTargets(1, &gpu.rtv, nullptr);
		gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gpu.context->VSSetShader(gpu.vs, nullptr, 0);
		gpu.context->PSSetShader(gpu.ps, nullptr, 0);
		gpu.context->PSSetConstantBuffers(0, 1, &gpu.params);
		return true;
	}

	// 等待某一帧完成；返回 false 表示退出或设备异常。
	bool WaitQuery(GpuContext& gpu, ID3D11Query* query)
	{
		for (;;)
		{
			BOOL done = FALSE;
			const HRESULT hr = gpu.context->GetData(query, &done, sizeof(done), 0);
			if (hr == S_OK && done) return true;
			if (FAILED(hr)) return false;
			if (g_state.exit.load(std::memory_order_relaxed)) return false;
			Sleep(1);
		}
	}

	void GpuWorker()
	{
		GpuContext gpu;
		std::wstring error;
		if (!InitGpu(gpu, error))
		{
			SetGpuStatus(L"GPU 不可用：" + error);
			return;
		}
		SetGpuStatus(L"运行中");

		std::vector<FrameQueries*> inFlight;
		size_t nextQuery = 0;
		double emaFrameMs = 0.0;
		uint64_t frames = 0;
		uint64_t drawCounter = 0;

		// 设备异常：报告原因后停止 GPU 部分（CPU 继续），资源随 gpu 析构释放。
		auto stopForDeviceFailure = [&]()
		{
			const HRESULT removed = gpu.device->GetDeviceRemovedReason();
			SetGpuStatus(FAILED(removed)
				? FormatHr(L"设备已移除，GPU 压力停止（CPU 继续）；GetDeviceRemovedReason", removed)
				: std::wstring(L"GPU 查询失败，GPU 压力停止（CPU 继续）"));
			g_state.gpuFps.store(0.0, std::memory_order_relaxed);
		};

		// 返回 false 表示退出或设备异常。帧耗时取 GPU 时间戳，不受本线程调度延迟影响。
		auto retireOldest = [&]() -> bool {
			if (inFlight.empty()) return true;
			FrameQueries* oldest = inFlight.front();
			if (!WaitQuery(gpu, oldest->done)) return false;
			inFlight.erase(inFlight.begin());
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
			UINT64 begin = 0, end = 0;
			if (gpu.context->GetData(oldest->disjoint, &disjoint, sizeof(disjoint), 0) != S_OK ||
				gpu.context->GetData(oldest->begin, &begin, sizeof(begin), 0) != S_OK ||
				gpu.context->GetData(oldest->end, &end, sizeof(end), 0) != S_OK ||
				disjoint.Disjoint || disjoint.Frequency == 0 || end < begin)
				return true; // 本帧时间戳无效（如降频切换），只放行在途限制，不计入统计。
			const double frameMs = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(disjoint.Frequency);
			emaFrameMs = emaFrameMs <= 0.0 ? frameMs : emaFrameMs * 0.8 + frameMs * 0.2;
			g_state.gpuFrameMs.store(emaFrameMs, std::memory_order_relaxed);
			g_state.gpuFps.store(emaFrameMs > 0.0 ? 1000.0 / emaFrameMs : 0.0, std::memory_order_relaxed);
			g_state.gpuFrames.store(++frames, std::memory_order_relaxed);
			if (frameMs > kTdrGuardMilliseconds)
			{
				// TDR 保护：单帧 GPU 耗时过长时自动降一档。
				int level = g_state.gpuLevel.load(std::memory_order_relaxed);
				if (level > kMinGpuLevel)
					g_state.gpuLevel.compare_exchange_strong(level, level - 1, std::memory_order_relaxed);
				g_state.tdrGuardDrops.fetch_add(1, std::memory_order_relaxed);
			}
			return true;
		};
		auto drainAll = [&]() -> bool {
			while (!inFlight.empty()) if (!retireOldest()) return false;
			return true;
		};

		while (!g_state.exit.load(std::memory_order_relaxed))
		{
			const bool active = !g_state.paused.load(std::memory_order_relaxed) &&
				g_state.gpuEnabled.load(std::memory_order_relaxed);
			if (!active)
			{
				if (!drainAll())
				{
					if (!g_state.exit.load(std::memory_order_relaxed)) stopForDeviceFailure();
					return;
				}
				g_state.gpuFps.store(0.0, std::memory_order_relaxed);
				Sleep(20);
				continue;
			}

			bool failed = false;
			while (inFlight.size() >= kMaxFramesInFlight)
				if (!retireOldest()) { failed = true; break; }
			if (g_state.exit.load(std::memory_order_relaxed)) break;
			if (failed)
			{
				stopForDeviceFailure();
				return;
			}

			FrameQueries& queries = gpu.queries[nextQuery];
			nextQuery = (nextQuery + 1) % ARRAYSIZE(gpu.queries);
			gpu.context->Begin(queries.disjoint);
			gpu.context->End(queries.begin);
			const int level = std::clamp(g_state.gpuLevel.load(std::memory_order_relaxed), kMinGpuLevel, kMaxGpuLevel);
			for (int i = 0; i < level; ++i)
			{
				GpuParams params = {};
				params.drawIndex = static_cast<float>(drawCounter++ % 4096);
				params.iterations = kPixelIterations;
				params.invSize[0] = 1.0f / static_cast<float>(g_state.rtWidth);
				params.invSize[1] = 1.0f / static_cast<float>(g_state.rtHeight);
				gpu.context->UpdateSubresource(gpu.params, 0, nullptr, &params, 0, 0);
				gpu.context->Draw(3, 0);
			}
			gpu.context->End(queries.end);
			gpu.context->End(queries.disjoint);
			gpu.context->End(queries.done);
			gpu.context->Flush();
			inFlight.push_back(&queries);

			if (FAILED(gpu.device->GetDeviceRemovedReason()))
			{
				stopForDeviceFailure();
				return;
			}
		}
		// 退出前等待在途帧，避免释放仍在使用的资源。
		(void)drainAll();
	}

	// ---------------- UI ----------------

	HFONT g_font = nullptr;

	std::wstring BuildStatusText()
	{
		std::wstring adapter, gpuStatus;
		{
			std::scoped_lock lock(g_state.textMutex);
			adapter = g_state.adapterName;
			gpuStatus = g_state.gpuStatus;
		}
		const bool paused = g_state.paused.load();
		const bool cpuOn = g_state.cpuEnabled.load();
		const bool gpuOn = g_state.gpuEnabled.load();
		static uint64_t lastIterations = 0;
		static auto lastTime = std::chrono::steady_clock::now();
		const uint64_t iterations = g_state.cpuIterations.load();
		const auto now = std::chrono::steady_clock::now();
		const double seconds = std::chrono::duration<double>(now - lastTime).count();
		static double cpuBatchesPerSecond = 0.0;
		if (seconds >= 0.2)
		{
			cpuBatchesPerSecond = static_cast<double>(iterations - lastIterations) / seconds;
			lastIterations = iterations;
			lastTime = now;
		}

		wchar_t buffer[2048];
		swprintf_s(buffer,
			L"状态：%s\n"
			L"\n"
			L"CPU：%s   忙线程 %d / %d   实测 %.0f 批/秒\n"
			L"GPU：%s   档位 %d（每帧 %d 次全屏绘制）   渲染目标 %d×%d\n"
			L"      帧耗时 %.2f ms   帧率 %.1f   已完成 %llu 帧   TDR 保护降档 %llu 次\n"
			L"      适配器：%s\n"
			L"      GPU 状态：%s\n"
			L"\n"
			L"按键：↑/↓ GPU 档位 ±1   ←/→ CPU 线程 ±1   G 开关 GPU   C 开关 CPU   空格 暂停/继续\n"
			L"关闭窗口即停止全部压力并退出。",
			paused ? L"已暂停" : L"施压中",
			cpuOn ? L"开" : L"关", g_state.cpuThreads.load(), g_state.cpuMax, cpuBatchesPerSecond,
			gpuOn ? L"开" : L"关", g_state.gpuLevel.load(), g_state.gpuLevel.load(), g_state.rtWidth, g_state.rtHeight,
			g_state.gpuFrameMs.load(), g_state.gpuFps.load(),
			static_cast<unsigned long long>(g_state.gpuFrames.load()),
			static_cast<unsigned long long>(g_state.tdrGuardDrops.load()),
			adapter.c_str(), gpuStatus.c_str());
		return buffer;
	}

	void Paint(HWND hwnd)
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		// 双缓冲，避免 250 ms 刷新闪烁。
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
		HGDIOBJ oldBmp = SelectObject(mem, bmp);
		FillRect(mem, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		HGDIOBJ oldFont = SelectObject(mem, g_font);
		SetBkMode(mem, TRANSPARENT);
		SetTextColor(mem, RGB(20, 20, 20));
		RECT text = { 16, 14, rc.right - 16, rc.bottom - 14 };
		const std::wstring status = BuildStatusText();
		DrawTextW(mem, status.c_str(), -1, &text, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
		BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
		SelectObject(mem, oldFont);
		SelectObject(mem, oldBmp);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(hwnd, &ps);
	}

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_CREATE:
			SetTimer(hwnd, kStatusTimerId, kStatusIntervalMs, nullptr);
			return 0;
		case WM_TIMER:
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
			Paint(hwnd);
			return 0;
		case WM_KEYDOWN:
			switch (wParam)
			{
			case VK_UP: g_state.gpuLevel.store(std::min(g_state.gpuLevel.load() + 1, kMaxGpuLevel)); break;
			case VK_DOWN: g_state.gpuLevel.store(std::max(g_state.gpuLevel.load() - 1, kMinGpuLevel)); break;
			case VK_RIGHT: g_state.cpuThreads.store(std::min(g_state.cpuThreads.load() + 1, g_state.cpuMax)); break;
			case VK_LEFT: g_state.cpuThreads.store(std::max(g_state.cpuThreads.load() - 1, 0)); break;
			case 'G': g_state.gpuEnabled.store(!g_state.gpuEnabled.load()); break;
			case 'C': g_state.cpuEnabled.store(!g_state.cpuEnabled.load()); break;
			case VK_SPACE: g_state.paused.store(!g_state.paused.load()); break;
			default: break;
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_DESTROY:
			KillTimer(hwnd, kStatusTimerId);
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	void ParseArguments()
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		if (!argv) return;
		for (int i = 1; i + 1 < argc; ++i)
		{
			const std::wstring key = argv[i];
			const wchar_t* value = argv[i + 1];
			if (key == L"--cpu") { g_state.cpuThreads.store(std::clamp(_wtoi(value), 0, g_state.cpuMax)); ++i; }
			else if (key == L"--gpu") { g_state.gpuLevel.store(std::clamp(_wtoi(value), kMinGpuLevel, kMaxGpuLevel)); ++i; }
			else if (key == L"--rt")
			{
				int w = 0, h = 0;
				if (swscanf_s(value, L"%dx%d", &w, &h) == 2 && w >= 64 && h >= 64 && w <= 16384 && h <= 16384)
				{
					g_state.rtWidth = w;
					g_state.rtHeight = h;
				}
				++i;
			}
		}
		LocalFree(argv);
	}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	g_state.cpuMax = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
	g_state.cpuThreads.store(std::max(1, g_state.cpuMax / 2));
	g_state.rtWidth = std::max(64, GetSystemMetrics(SM_CXSCREEN));
	g_state.rtHeight = std::max(64, GetSystemMetrics(SM_CYSCREEN));
	ParseArguments();

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	wc.lpszClassName = L"InkeysStressLoadWindow";
	if (!RegisterClassExW(&wc)) return 1;

	const UINT dpi = GetDpiForSystem();
	const int width = MulDiv(760, static_cast<int>(dpi), 96);
	const int height = MulDiv(340, static_cast<int>(dpi), 96);
	g_font = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
		L"Microsoft YaHei UI");

	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Inkeys 压力工具（关闭窗口即停止）",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, nullptr);
	if (!hwnd) return 1;

	// 窗口创建后再启动负载线程，保证关闭窗口一定能停止它们。
	std::vector<std::thread> cpuThreads;
	cpuThreads.reserve(static_cast<size_t>(g_state.cpuMax));
	for (int i = 0; i < g_state.cpuMax; ++i) cpuThreads.emplace_back(CpuWorker, i);
	std::thread gpuThread(GpuWorker);

	ShowWindow(hwnd, showCommand);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	g_state.exit.store(true);
	gpuThread.join();
	for (auto& t : cpuThreads) t.join();
	if (g_font) DeleteObject(g_font);
	return 0;
}
