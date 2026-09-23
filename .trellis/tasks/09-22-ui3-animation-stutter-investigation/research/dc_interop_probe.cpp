#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

static void Check(HRESULT hr, const char* operation)
{
    if (SUCCEEDED(hr)) return;
    std::printf("FAIL %s hr=0x%08lX\n", operation, static_cast<unsigned long>(hr));
    throw hr;
}

static double Milliseconds(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static double Percentile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>((values.size() - 1) * fraction)];
}

// 无 HWND 的 API 对照：固定小脏区，仅改变 target 面积和只读 DC 的 ReleaseDC 参数。
// 不包含 ULW、真实 Bar 布局或光影，结果不能当作完整主栏帧时间。
int main()
{
    try
    {
        Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
        ComPtr<ID3D11Device> d3d;
        ComPtr<ID3D11DeviceContext> immediate;
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL selected{};
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            &d3d, &selected, &immediate), "D3D11CreateDevice WARP");
        ComPtr<ID2D1Factory1> factory;
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, factory.GetAddressOf()),
            "D2D1CreateFactory");
        ComPtr<IDXGIDevice> dxgi;
        Check(d3d.As(&dxgi), "Query DXGI");
        ComPtr<ID2D1Device> device;
        Check(factory->CreateDevice(dxgi.Get(), &device), "CreateDevice");
        std::puts("backend=WARP; warmup=20; samples=80; clip=256x128; no HWND/ULW/effects");
        for (unsigned scale : {1u, 2u, 3u})
        {
            const unsigned width = 1600u * scale, height = 1200u * scale;
            ComPtr<ID2D1DeviceContext> context;
            Check(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context),
                "CreateDeviceContext");
            ComPtr<ID2D1Bitmap1> target;
            const auto properties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            Check(context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                &properties, &target), "CreateBitmap");
            context->SetTarget(target.Get());
            ComPtr<ID2D1GdiInteropRenderTarget> interop;
            Check(context.As(&interop), "Query GDI interop");
            ComPtr<ID2D1SolidColorBrush> brush;
            Check(context->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.4f, 0.6f, 1.0f),
                &brush), "CreateSolidColorBrush");
            // 两种参数交替先后各跑一遍，避免只比较冷启动与预热状态。
            for (bool whole : {true, false, false, true})
            {
                std::vector<double> total, get, release, end;
                for (int index = -20; index < 80; ++index)
                {
                    const auto start = Clock::now();
                    context->BeginDraw();
                    const auto clip = D2D1::RectF(0, 0, 256, 128);
                    context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
                    context->Clear(D2D1::ColorF(0, 0, 0, 0));
                    context->FillRectangle(clip, brush.Get());
                    context->PopAxisAlignedClip();
                    const auto beforeGet = Clock::now();
                    HDC dc{};
                    Check(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc), "GetDC");
                    const auto afterGet = Clock::now();
                    const RECT empty{};
                    Check(interop->ReleaseDC(whole ? nullptr : &empty), "ReleaseDC");
                    const auto afterRelease = Clock::now();
                    Check(context->EndDraw(), "EndDraw");
                    const auto finish = Clock::now();
                    if (index < 0) continue;
                    total.push_back(Milliseconds(start, finish));
                    get.push_back(Milliseconds(beforeGet, afterGet));
                    release.push_back(Milliseconds(afterGet, afterRelease));
                    end.push_back(Milliseconds(afterRelease, finish));
                }
                std::printf("%ux%u release=%s total_ms p50=%.3f p95=%.3f get_p50=%.3f release_p50=%.3f end_p50=%.3f\n",
                    width, height, whole ? "nullptr" : "empty", Percentile(total, .5),
                    Percentile(total, .95), Percentile(get, .5), Percentile(release, .5),
                    Percentile(end, .5));
                std::fflush(stdout);
            }
            context->SetTarget(nullptr);
        }
        return 0;
    }
    catch (HRESULT) { return 1; }
}
