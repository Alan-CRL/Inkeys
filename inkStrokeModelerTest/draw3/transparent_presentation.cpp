module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cstring>
#include <dcomp.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <iostream>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#pragma comment(lib, "dwmapi.lib")

module draw3.transparent_presentation;

import draw3.diagnostics;

namespace draw3
{
	const char* TransparentPresentModeName(TransparentPresentMode mode)
	{
		switch (mode)
		{
		case TransparentPresentMode::UlwDirtyRect: return "UlwDirtyRect";
		case TransparentPresentMode::DirectCompositionVisualTree: return "DirectCompositionVisualTree";
		case TransparentPresentMode::DwmBlurBehind: return "DwmBlurBehind";
		case TransparentPresentMode::DwmBlurBehind2: return "DwmBlurBehind2";
		default: return "Unknown";
		}
	}

	namespace
	{
		bool IsUlwMode(TransparentPresentMode mode)
		{
			return mode == TransparentPresentMode::UlwDirtyRect;
		}

		bool IsDirectCompositionMode(TransparentPresentMode mode)
		{
			return mode == TransparentPresentMode::DirectCompositionVisualTree;
		}

		bool IsDwmBlurBehindMode(TransparentPresentMode mode)
		{
			return mode == TransparentPresentMode::DwmBlurBehind;
		}

		bool IsDwmBlurBehind2Mode(TransparentPresentMode mode)
		{
			return mode == TransparentPresentMode::DwmBlurBehind2;
		}

		bool IsDwmGlassMode(TransparentPresentMode mode)
		{
			return IsDwmBlurBehindMode(mode) || IsDwmBlurBehind2Mode(mode);
		}

		bool IsGpuMode(TransparentPresentMode mode)
		{
			return IsDirectCompositionMode(mode) || IsDwmGlassMode(mode);
		}

		const char* SwapChainCreationStepName(TransparentPresentMode mode, bool waitable)
		{
			if (IsDirectCompositionMode(mode))
				return waitable ? "CreateWaitableSwapChainForComposition" : "CreateSwapChainForComposition";
			return waitable ? "CreateWaitableSwapChainForHwnd" : "CreateSwapChainForHwnd";
		}

		bool IsDirectCompositionApiAvailable()
		{
			HMODULE module = LoadLibraryW(L"dcomp.dll"); // 运行时探测，避免旧系统缺少 DComp 时直接失败。
			if (!module) return false;
			const FARPROC createDevice = GetProcAddress(module, "DCompositionCreateDevice");
			FreeLibrary(module);
			return createDevice != nullptr;
		}

		bool TrySetWindowLong(HWND window, int index, LONG_PTR value, const char* step)
		{
			SetLastError(ERROR_SUCCESS);
			const LONG_PTR previousValue = SetWindowLongPtr(window, index, value);
			const DWORD error = GetLastError();
			if (previousValue == 0 && error != ERROR_SUCCESS)
			{
				LogWin32Error(step, error);
				return false;
			}
			return true;
		}

		bool EnsureBorderlessTransparentWindowStyle(HWND window, bool noRedirectionBitmap, bool layered)
		{
			if (!window) return false;
			WCHAR title[128] = {};
			if (!GetWindowTextW(window, title, ARRAYSIZE(title)) || title[0] == L'\0')
			{
				SetWindowTextW(window, L"Ink Stroke Modeler Test"); // 保证调试工具里能识别该窗口。
			}

			bool changed = false;
			const LONG_PTR style = GetWindowLongPtr(window, GWL_STYLE);
			const LONG_PTR desiredStyle =
				(style & ~(WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) |
				WS_POPUP | WS_CLIPCHILDREN; // 去掉标准边框，保持全屏透明绘图窗口。
			if (desiredStyle != style)
			{
				if (!TrySetWindowLong(window, GWL_STYLE, desiredStyle, "SetWindowLongPtr(GWL_STYLE)")) return false;
				changed = true;
			}

			const LONG_PTR extendedStyle = GetWindowLongPtr(window, GWL_EXSTYLE);
			LONG_PTR desiredExtendedStyle = extendedStyle & ~(
				WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
				WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_NOREDIRECTIONBITMAP);
			if (layered) desiredExtendedStyle |= WS_EX_LAYERED; // ULW 路径需要 layered window。
			if (noRedirectionBitmap) desiredExtendedStyle |= WS_EX_NOREDIRECTIONBITMAP; // DComp 路径避免 DWM 额外重定向位图。
			if (desiredExtendedStyle != extendedStyle)
			{
				if (!TrySetWindowLong(window, GWL_EXSTYLE, desiredExtendedStyle, "SetWindowLongPtr(GWL_EXSTYLE)")) return false;
				changed = true;
			}
			if (changed)
			{
				SetWindowPos(window, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}
			return true;
		}

		struct UlwDirtyRectPresenter
		{
			HWND window = nullptr;
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
			UINT stagingWidth = 0;
			UINT stagingHeight = 0;
			HDC memoryDC = nullptr;
			HBITMAP dibBitmap = nullptr;
			HGDIOBJ oldBitmap = nullptr;
			void* dibBits = nullptr;
			int dibWidth = 0;
			int dibHeight = 0;

			void ReleaseDib()
			{
				if (memoryDC && oldBitmap) SelectObject(memoryDC, oldBitmap);
				oldBitmap = nullptr;
				if (dibBitmap) DeleteObject(dibBitmap);
				dibBitmap = nullptr;
				if (memoryDC) DeleteDC(memoryDC);
				memoryDC = nullptr;
				dibBits = nullptr;
				dibWidth = 0;
				dibHeight = 0;
			}

			void Reset()
			{
				ReleaseDib();
				stagingTexture.Reset();
				device.Reset();
				context.Reset();
				stagingWidth = 0;
				stagingHeight = 0;
				window = nullptr;
			}

			bool CreateStagingTexture(UINT width, UINT height)
			{
				if (!device || width == 0 || height == 0) return false;
				if (stagingTexture && stagingWidth == width && stagingHeight == height) return true;
				stagingTexture.Reset();
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = width;
				description.Height = height;
				description.MipLevels = 1;
				description.ArraySize = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.SampleDesc.Count = 1;
				description.Usage = D3D11_USAGE_STAGING; // ULW 需要 CPU 读回像素。
				description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(device->CreateTexture2D(&description, nullptr, stagingTexture.ReleaseAndGetAddressOf()))) return false;
				stagingWidth = width;
				stagingHeight = height;
				return true;
			}

			bool EnsureWindowDib()
			{
				RECT clientRect = {};
				if (!window || !GetClientRect(window, &clientRect)) return false;
				const int width = clientRect.right - clientRect.left;
				const int height = clientRect.bottom - clientRect.top;
				if (width <= 0 || height <= 0) return false;
				if (memoryDC && dibBitmap && dibBits && dibWidth == width && dibHeight == height) return true;

				ReleaseDib();
				memoryDC = CreateCompatibleDC(nullptr);
				if (!memoryDC) return false;
				BITMAPINFO bitmapInfo = {};
				bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bitmapInfo.bmiHeader.biWidth = width;
				bitmapInfo.bmiHeader.biHeight = -height; // 负高度表示 top-down DIB，坐标方向与 D3D 一致。
				bitmapInfo.bmiHeader.biPlanes = 1;
				bitmapInfo.bmiHeader.biBitCount = 32;
				bitmapInfo.bmiHeader.biCompression = BI_RGB;
				dibBitmap = CreateDIBSection(memoryDC, &bitmapInfo, DIB_RGB_COLORS, &dibBits, nullptr, 0);
				if (!dibBitmap || !dibBits)
				{
					ReleaseDib();
					return false;
				}
				oldBitmap = SelectObject(memoryDC, dibBitmap);
				if (!oldBitmap || oldBitmap == HGDI_ERROR)
				{
					ReleaseDib();
					return false;
				}
				dibWidth = width;
				dibHeight = height;
				// alpha=1 的近透明背景避免 layered window 鼠标穿透。
				std::fill_n(static_cast<DWORD*>(dibBits), static_cast<size_t>(width) * height, 0x01000000);
				return true;
			}

			bool Initialize(HWND inputWindow, ID3D11Device* inputDevice, ID3D11DeviceContext* inputContext, UINT width, UINT height)
			{
				Reset();
				window = inputWindow;
				device = inputDevice;
				context = inputContext;
				return window && device && context && Resize(width, height);
			}

			bool Resize(UINT width, UINT height)
			{
				return CreateStagingTexture(width, height) && EnsureWindowDib();
			}

			bool Present(ID3D11Texture2D* finalTexture, RECT dirty, bool presentFull)
			{
				if (!context || !stagingTexture || !finalTexture || !EnsureWindowDib()) return false;
				if (presentFull) dirty = RECT{ 0, 0, static_cast<LONG>(stagingWidth), static_cast<LONG>(stagingHeight) }; // 全量呈现时忽略传入脏区。
				dirty.left = std::max(0L, dirty.left);
				dirty.top = std::max(0L, dirty.top);
				dirty.right = std::min(static_cast<LONG>(stagingWidth), dirty.right);
				dirty.bottom = std::min(static_cast<LONG>(stagingHeight), dirty.bottom);
				if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) return true;

				D3D11_BOX sourceRegion = {
					static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
					static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1
				};
				context->CopySubresourceRegion(stagingTexture.Get(), 0, static_cast<UINT>(dirty.left),
					static_cast<UINT>(dirty.top), 0, finalTexture, 0, &sourceRegion); // 只把脏区从 GPU backbuffer 拷到可读纹理。
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (FAILED(context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

				const size_t pixelCount = static_cast<size_t>(dirty.right - dirty.left);
				const size_t copyBytes = pixelCount * 4;
				const BYTE* source = static_cast<const BYTE*>(mapped.pData) +
					static_cast<size_t>(dirty.top) * mapped.RowPitch + static_cast<size_t>(dirty.left) * 4;
				BYTE* destination = static_cast<BYTE*>(dibBits) +
					static_cast<size_t>(dirty.top) * static_cast<size_t>(dibWidth) * 4 + static_cast<size_t>(dirty.left) * 4;
				for (LONG y = dirty.top; y < dirty.bottom; ++y)
				{
					const size_t row = static_cast<size_t>(y - dirty.top);
					const BYTE* sourceRow = source + row * mapped.RowPitch;
					BYTE* destinationRow = destination + row * static_cast<size_t>(dibWidth) * 4;
					std::memcpy(destinationRow, sourceRow, copyBytes); // 逐行处理 RowPitch 和 DIB stride 不同的情况。
					for (size_t x = 0; x < pixelCount; ++x)
					{
						const UINT sourceAlpha = sourceRow[x * 4 + 3];
						// 只在 CPU 输出阶段叠加黑色 alpha=1/255 底层，不回写内部画布。
						destinationRow[x * 4 + 3] = static_cast<BYTE>(
							sourceAlpha + ((255u - sourceAlpha) + 127u) / 255u);
					}
				}
				context->Unmap(stagingTexture.Get(), 0);

				RECT windowRect = {};
				if (!GetWindowRect(window, &windowRect)) return false;
				POINT destinationPoint = { windowRect.left, windowRect.top };
				SIZE destinationSize = { dibWidth, dibHeight };
				POINT sourcePoint = { 0, 0 };
				BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				UPDATELAYEREDWINDOWINFO update = {};
				update.cbSize = sizeof(update);
				update.pptDst = &destinationPoint;
				update.psize = &destinationSize;
				update.hdcSrc = memoryDC;
				update.pptSrc = &sourcePoint;
				update.pblend = &blend;
				update.dwFlags = ULW_ALPHA;
				update.prcDirty = presentFull ? nullptr : &dirty; // 非全量时让 USER32 只更新改变区域。
				return UpdateLayeredWindowIndirect(window, &update) != FALSE;
			}
		};

		struct DirectCompositionPresenter
		{
			HWND window = nullptr;
			HMODULE dcompModule = nullptr;
			Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice;
			Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget;
			Microsoft::WRL::ComPtr<IDCompositionVisual> rootVisual;
			using CreateDeviceFunction = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);

			~DirectCompositionPresenter()
			{
				Reset();
				if (dcompModule) FreeLibrary(dcompModule);
			}

			void Reset()
			{
				rootVisual.Reset();
				compositionTarget.Reset();
				compositionDevice.Reset();
				window = nullptr;
			}

			bool Initialize(HWND inputWindow, IDXGIDevice* dxgiDevice, IDXGISwapChain1* swapChain)
			{
				Reset();
				window = inputWindow;
				if (!window || !dxgiDevice || !swapChain) return false;
				if (!EnsureBorderlessTransparentWindowStyle(window, true, false)) return false;
				if (!dcompModule) dcompModule = LoadLibraryW(L"dcomp.dll"); // 延迟加载，保持旧系统可运行到 fallback。
				if (!dcompModule) return false;
				auto createDevice = reinterpret_cast<CreateDeviceFunction>(GetProcAddress(dcompModule, "DCompositionCreateDevice"));
				if (!createDevice) return false;

				HRESULT result = createDevice(dxgiDevice, __uuidof(IDCompositionDevice),
					reinterpret_cast<void**>(compositionDevice.ReleaseAndGetAddressOf()));
				if (FAILED(result) || !compositionDevice) return false;
				if (FAILED(result = compositionDevice->CreateTargetForHwnd(window, TRUE,
					compositionTarget.ReleaseAndGetAddressOf()))) return false;
				if (FAILED(result = compositionDevice->CreateVisual(rootVisual.ReleaseAndGetAddressOf()))) return false;
				if (FAILED(result = rootVisual->SetContent(swapChain))) return false; // 将交换链作为视觉树内容。
				if (FAILED(result = compositionTarget->SetRoot(rootVisual.Get()))) return false;
				if (FAILED(result = compositionDevice->Commit())) return false; // 提交后 DWM 才开始读取该 visual。
				return true;
			}
		};

		struct DwmBlurBehindPresenter
		{
			HWND window = nullptr;

			void Reset() { window = nullptr; }

			bool Update()
			{
				if (!window) return false;
				BOOL enabled = FALSE;
				HRESULT result = DwmIsCompositionEnabled(&enabled);
				if (FAILED(result) || !enabled) return false;
				HRGN region = CreateRectRgn(0, 0, -1, -1); // 特殊区域表示整个窗口启用玻璃。
				if (!region) return false;
				DWM_BLURBEHIND blur = {};
				blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION | DWM_BB_TRANSITIONONMAXIMIZED;
				blur.fEnable = TRUE;
				blur.hRgnBlur = region;
				blur.fTransitionOnMaximized = TRUE;
				result = DwmEnableBlurBehindWindow(window, &blur);
				DeleteObject(region);
				if (FAILED(result)) LogHResult("DwmEnableBlurBehindWindow", result);
				return SUCCEEDED(result);
			}

			bool Initialize(HWND inputWindow)
			{
				window = inputWindow;
				if (!EnsureBorderlessTransparentWindowStyle(window, false, false)) return false;
				LogDwmModeDiagnostics("DwmBlurBehind", window, "Initialize");
				return Update();
			}
		};

		struct DwmExtendedFramePresenter
		{
			HWND window = nullptr;

			void Reset() { window = nullptr; }

			bool Update()
			{
				if (!window) return false;
				BOOL enabled = FALSE;
				HRESULT result = DwmIsCompositionEnabled(&enabled);
				if (FAILED(result) || !enabled) return false;
				MARGINS margins = { -1, -1, -1, -1 }; // -1 将玻璃扩展到整个客户区。
				result = DwmExtendFrameIntoClientArea(window, &margins);
				if (FAILED(result)) LogHResult("DwmExtendFrameIntoClientArea", result);
				return SUCCEEDED(result);
			}

			bool Initialize(HWND inputWindow)
			{
				window = inputWindow;
				if (!EnsureBorderlessTransparentWindowStyle(window, false, false)) return false;
				LogDwmModeDiagnostics("DwmBlurBehind2", window, "Initialize");
				return Update();
			}
		};
	}

	bool ShouldPreconfigureNoRedirectionBitmap()
	{
		return IsDirectCompositionMode(kPreferredTransparentPresentMode) && IsDirectCompositionApiAvailable(); // 只有首选 DComp 且 API 存在时才预置扩展样式。
	}

	struct TransparentPresentationController::Impl
	{
		HWND window = nullptr;
		GraphicsDeviceResources* graphics = nullptr;
		InkRenderer* renderer = nullptr;
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
		TransparentPresentMode activeMode = kPreferredTransparentPresentMode;
		UINT width = 0;
		UINT height = 0;
		bool presentFailureLogged = false;
		UlwDirtyRectPresenter ulwPresenter;
		DirectCompositionPresenter directCompositionPresenter;
		DwmBlurBehindPresenter dwmBlurPresenter;
		DwmExtendedFramePresenter dwmExtendedPresenter;

		void ResetPresenters()
		{
			ulwPresenter.Reset();
			directCompositionPresenter.Reset();
			dwmBlurPresenter.Reset();
			dwmExtendedPresenter.Reset();
			presentFailureLogged = false;
		}

		void ReleaseAttempt()
		{
			ResetPresenters();
			if (renderer) renderer->ReleaseResources();
			swapChain.Reset();
		}

		bool ConfigureWindow(TransparentPresentMode mode)
		{
			return EnsureBorderlessTransparentWindowStyle(window,
				IsDirectCompositionMode(mode), IsUlwMode(mode));
		}

		HRESULT CreateSwapChainForMode(TransparentPresentMode mode, DXGI_SWAP_CHAIN_DESC1 description)
		{
			swapChain.Reset();
			HRESULT result = S_OK;
			if (IsDirectCompositionMode(mode))
			{
				result = graphics->factory->CreateSwapChainForComposition(graphics->device.Get(), &description, nullptr,
					swapChain.ReleaseAndGetAddressOf()); // DComp 使用无 HWND 的 composition swapchain。
				return SUCCEEDED(result) && swapChain ? S_OK : (FAILED(result) ? result : E_FAIL);
			}

			if (IsDwmGlassMode(mode))
			{
				const bool waitable = (description.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0;
				LogDwmModeDiagnostics(TransparentPresentModeName(mode), window, "Before CreateSwapChainForHwnd");
				LogSwapChainDescription(TransparentPresentModeName(mode),
					waitable ? "Trying waitable premultiplied alpha" : "Trying premultiplied alpha", description);
			}
			result = graphics->factory->CreateSwapChainForHwnd(graphics->device.Get(), window, &description, nullptr, nullptr,
				swapChain.ReleaseAndGetAddressOf()); // DWM/ULW 路径绑定到实际 HWND。
			if (FAILED(result) && IsDwmGlassMode(mode))
			{
				BOOL opaqueBlend = TRUE;
				if (!LogDwmColorizationState(TransparentPresentModeName(mode), "Before unspecified alpha retry", &opaqueBlend)) return result;
				// Win7 Aero 对 HWND swapchain 使用 UNSPECIFIED alpha，保留原有兼容重试。
				swapChain.Reset();
				description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
				LogSwapChainDescription(TransparentPresentModeName(mode),
					(description.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0
					? "Trying waitable unspecified alpha" : "Trying unspecified alpha", description);
				result = graphics->factory->CreateSwapChainForHwnd(graphics->device.Get(), window, &description, nullptr, nullptr,
					swapChain.ReleaseAndGetAddressOf());
			}
			return SUCCEEDED(result) && swapChain ? S_OK : (FAILED(result) ? result : E_FAIL);
		}

		bool TryCreateWaitableSwapChain(TransparentPresentMode mode, const DXGI_SWAP_CHAIN_DESC1& baseDescription)
		{
			DXGI_SWAP_CHAIN_DESC1 waitableDescription = baseDescription;
			waitableDescription.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
			std::cout << "Trying waitable swapchain in mode " << TransparentPresentModeName(mode) << "." << std::endl;
			HRESULT result = CreateSwapChainForMode(mode, waitableDescription);
			if (FAILED(result) || !swapChain)
			{
				LogHResult(SwapChainCreationStepName(mode, true), result);
				std::cout << "Waitable swapchain creation failed; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}

			Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
			swapChain2.Reset();
			result = swapChain.As(&swapChain2);
			if (FAILED(result) || !swapChain2)
			{
				LogHResult("IDXGISwapChain1::QueryInterface(IDXGISwapChain2)", result);
				std::cout << "Waitable swapchain is not usable; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}

			result = swapChain2->SetMaximumFrameLatency(1);
			if (FAILED(result))
			{
				LogHResult("IDXGISwapChain2::SetMaximumFrameLatency(1)", result);
				std::cout << "Waitable frame latency setup failed; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}

			std::cout << "Waitable swapchain enabled in mode " << TransparentPresentModeName(mode) << "." << std::endl;
			if (IsDwmGlassMode(mode))
			{
				LogSwapChainRuntimeDescription(TransparentPresentModeName(mode), swapChain.Get(), "Waitable created");
			}
			return true;
		}

		bool CreateSwapChain(TransparentPresentMode mode)
		{
			DXGI_SWAP_CHAIN_DESC1 description = {};
			description.Width = width;
			description.Height = height;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.BufferCount = 2;
			description.Scaling = DXGI_SCALING_STRETCH;
			description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			description.AlphaMode = IsGpuMode(mode) ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_UNSPECIFIED; // GPU 透明路径需要保留 premultiplied alpha。

			// 先试 Win8.1+ 的 waitable swapchain；失败后不判断系统版本，直接回退普通路径。
			if (TryCreateWaitableSwapChain(mode, description)) return true;

			HRESULT result = CreateSwapChainForMode(mode, description);
			if (FAILED(result) || !swapChain)
			{
				LogHResult(SwapChainCreationStepName(mode, false), result);
				return false;
			}
			std::cout << "Ordinary swapchain enabled in mode " << TransparentPresentModeName(mode) << "." << std::endl;
			if (IsDwmGlassMode(mode))
			{
				LogSwapChainRuntimeDescription(TransparentPresentModeName(mode), swapChain.Get(), "Created");
			}
			return true;
		}

		bool InitializePresenter()
		{
			switch (activeMode)
			{
			case TransparentPresentMode::UlwDirtyRect:
				return ulwPresenter.Initialize(window, graphics->device.Get(), graphics->context.Get(), width, height);
			case TransparentPresentMode::DirectCompositionVisualTree:
				return directCompositionPresenter.Initialize(window, graphics->dxgiDevice.Get(), swapChain.Get());
			case TransparentPresentMode::DwmBlurBehind:
				return dwmBlurPresenter.Initialize(window);
			case TransparentPresentMode::DwmBlurBehind2:
				return dwmExtendedPresenter.Initialize(window);
			default:
				return false;
			}
		}

		bool TryInitialize(TransparentPresentMode mode)
		{
			ReleaseAttempt(); // 每次尝试前清掉上一条路径留下的交换链和 presenter。
			activeMode = mode;
			std::cout << "Trying transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			if (!ConfigureWindow(mode) || !CreateSwapChain(mode)) return false;

			if (!renderer->Init(graphics->device.Get(), graphics->context.Get(), swapChain.Get(), width, height)) return false;
			if (!InitializePresenter()) return false;
			std::cout << "Active transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			return true;
		}

		bool PresentSwapChain(RECT dirty, bool presentFull)
		{
			DXGI_PRESENT_PARAMETERS parameters = {};
			if (!presentFull)
			{
				dirty.left = std::max(0L, dirty.left);
				dirty.top = std::max(0L, dirty.top);
				dirty.right = std::min(static_cast<LONG>(width), dirty.right);
				dirty.bottom = std::min(static_cast<LONG>(height), dirty.bottom);
				if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) return true;
				parameters.DirtyRectsCount = 1;
				parameters.pDirtyRects = &dirty;
			}
			return SUCCEEDED(swapChain->Present1(0, 0, &parameters));
		}
	};

	TransparentPresentationController::TransparentPresentationController()
		: impl_(std::make_unique<Impl>())
	{
	}

	TransparentPresentationController::~TransparentPresentationController() = default;

	bool TransparentPresentationController::Initialize(HWND window, GraphicsDeviceResources& graphics,
		InkRenderer& renderer, UINT width, UINT height)
	{
		impl_->window = window;
		impl_->graphics = &graphics;
		impl_->renderer = &renderer;
		impl_->width = width;
		impl_->height = height;
		const TransparentPresentMode modes[] = {
			TransparentPresentMode::DirectCompositionVisualTree,
			TransparentPresentMode::DwmBlurBehind2,
			TransparentPresentMode::UlwDirtyRect
		};
		for (size_t index = 0; index < ARRAYSIZE(modes); ++index)
		{
			if (impl_->TryInitialize(modes[index])) return true; // 按优先级选第一个可用透明呈现路径。
			impl_->ReleaseAttempt();
			if (index + 1 < ARRAYSIZE(modes))
			{
				std::cout << "Transparent present mode " << TransparentPresentModeName(modes[index])
					<< " failed; fallback to " << TransparentPresentModeName(modes[index + 1]) << "." << std::endl;
			}
		}
		std::cout << "All transparent present modes failed." << std::endl;
		return false;
	}

	bool TransparentPresentationController::Resize(UINT width, UINT height)
	{
		impl_->width = width;
		impl_->height = height;
		switch (impl_->activeMode)
		{
		case TransparentPresentMode::UlwDirtyRect: return impl_->ulwPresenter.Resize(width, height);
		case TransparentPresentMode::DirectCompositionVisualTree: return true;
		case TransparentPresentMode::DwmBlurBehind:
			LogDwmModeDiagnostics("DwmBlurBehind", impl_->window, "Resize");
			return impl_->dwmBlurPresenter.Update();
		case TransparentPresentMode::DwmBlurBehind2:
			LogDwmModeDiagnostics("DwmBlurBehind2", impl_->window, "Resize");
			return impl_->dwmExtendedPresenter.Update();
		default: return false;
		}
	}

	bool TransparentPresentationController::Present(RECT dirty, bool presentFull)
	{
		bool succeeded = false;
		if (IsUlwMode(impl_->activeMode))
		{
			succeeded = impl_->ulwPresenter.Present(impl_->renderer->backBufferTexture.Get(), dirty, presentFull); // ULW 需要 CPU 读回并调用 UpdateLayeredWindow。
		}
		else
		{
			succeeded = impl_->PresentSwapChain(dirty, presentFull); // GPU 路径直接 Present1，DWM 读取 alpha。
		}
		if (!succeeded && !impl_->presentFailureLogged)
		{
			std::cout << "Present failed in mode " << TransparentPresentModeName(impl_->activeMode) << std::endl;
			impl_->presentFailureLogged = true;
		}
		if (succeeded) impl_->presentFailureLogged = false;
		return succeeded;
	}

	void TransparentPresentationController::RefreshAfterCompositionChanged()
	{
		if (IsDwmBlurBehindMode(impl_->activeMode)) impl_->dwmBlurPresenter.Update();
		if (IsDwmBlurBehind2Mode(impl_->activeMode)) impl_->dwmExtendedPresenter.Update();
	}

	TransparentPresentMode TransparentPresentationController::ActiveMode() const
	{
		return impl_->activeMode;
	}

	bool TransparentPresentationController::IsGpuTransparentComposition() const
	{
		return IsGpuMode(impl_->activeMode);
	}

	IDXGISwapChain1* TransparentPresentationController::SwapChain() const
	{
		return impl_->swapChain.Get();
	}
}
