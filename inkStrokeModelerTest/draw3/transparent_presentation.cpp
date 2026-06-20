module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <atlbase.h>
#include <cstring>
#include <dcomp.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <iostream>
#include <vector>
#include <windows.h>

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

		bool IsDirectCompositionApiAvailable()
		{
			HMODULE module = LoadLibraryW(L"dcomp.dll");
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
				SetWindowTextW(window, L"Ink Stroke Modeler Test");
			}

			bool changed = false;
			const LONG_PTR style = GetWindowLongPtr(window, GWL_STYLE);
			const LONG_PTR desiredStyle =
				(style & ~(WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) |
				WS_POPUP | WS_CLIPCHILDREN;
			if (desiredStyle != style)
			{
				if (!TrySetWindowLong(window, GWL_STYLE, desiredStyle, "SetWindowLongPtr(GWL_STYLE)")) return false;
				changed = true;
			}

			const LONG_PTR extendedStyle = GetWindowLongPtr(window, GWL_EXSTYLE);
			LONG_PTR desiredExtendedStyle = extendedStyle & ~(
				WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
				WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_NOREDIRECTIONBITMAP);
			if (layered) desiredExtendedStyle |= WS_EX_LAYERED;
			if (noRedirectionBitmap) desiredExtendedStyle |= WS_EX_NOREDIRECTIONBITMAP;
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
			CComPtr<ID3D11Device> device;
			CComPtr<ID3D11DeviceContext> context;
			CComPtr<ID3D11Texture2D> stagingTexture;
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
				stagingTexture.Release();
				device.Release();
				context.Release();
				stagingWidth = 0;
				stagingHeight = 0;
				window = nullptr;
			}

			bool CreateStagingTexture(UINT width, UINT height)
			{
				if (!device || width == 0 || height == 0) return false;
				if (stagingTexture && stagingWidth == width && stagingHeight == height) return true;
				stagingTexture.Release();
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = width;
				description.Height = height;
				description.MipLevels = 1;
				description.ArraySize = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.SampleDesc.Count = 1;
				description.Usage = D3D11_USAGE_STAGING;
				description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(device->CreateTexture2D(&description, nullptr, &stagingTexture))) return false;
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
				bitmapInfo.bmiHeader.biHeight = -height;
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
				if (presentFull) dirty = RECT{ 0, 0, static_cast<LONG>(stagingWidth), static_cast<LONG>(stagingHeight) };
				dirty.left = std::max(0L, dirty.left);
				dirty.top = std::max(0L, dirty.top);
				dirty.right = std::min(static_cast<LONG>(stagingWidth), dirty.right);
				dirty.bottom = std::min(static_cast<LONG>(stagingHeight), dirty.bottom);
				if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) return true;

				D3D11_BOX sourceRegion = {
					static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
					static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1
				};
				context->CopySubresourceRegion(stagingTexture, 0, static_cast<UINT>(dirty.left),
					static_cast<UINT>(dirty.top), 0, finalTexture, 0, &sourceRegion);
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) return false;

				const size_t copyBytes = static_cast<size_t>(dirty.right - dirty.left) * 4;
				const BYTE* source = static_cast<const BYTE*>(mapped.pData) +
					static_cast<size_t>(dirty.top) * mapped.RowPitch + static_cast<size_t>(dirty.left) * 4;
				BYTE* destination = static_cast<BYTE*>(dibBits) +
					static_cast<size_t>(dirty.top) * static_cast<size_t>(dibWidth) * 4 + static_cast<size_t>(dirty.left) * 4;
				for (LONG y = dirty.top; y < dirty.bottom; ++y)
				{
					const size_t row = static_cast<size_t>(y - dirty.top);
					std::memcpy(destination + row * static_cast<size_t>(dibWidth) * 4,
						source + row * mapped.RowPitch, copyBytes);
				}
				context->Unmap(stagingTexture, 0);

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
				update.prcDirty = presentFull ? nullptr : &dirty;
				return UpdateLayeredWindowIndirect(window, &update) != FALSE;
			}
		};

		struct DirectCompositionPresenter
		{
			HWND window = nullptr;
			HMODULE dcompModule = nullptr;
			CComPtr<IDCompositionDevice> compositionDevice;
			CComPtr<IDCompositionTarget> compositionTarget;
			CComPtr<IDCompositionVisual> rootVisual;
			using CreateDeviceFunction = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);

			~DirectCompositionPresenter()
			{
				Reset();
				if (dcompModule) FreeLibrary(dcompModule);
			}

			void Reset()
			{
				rootVisual.Release();
				compositionTarget.Release();
				compositionDevice.Release();
				window = nullptr;
			}

			bool Initialize(HWND inputWindow, IDXGIDevice* dxgiDevice, IDXGISwapChain1* swapChain)
			{
				Reset();
				window = inputWindow;
				if (!window || !dxgiDevice || !swapChain) return false;
				if (!EnsureBorderlessTransparentWindowStyle(window, true, false)) return false;
				if (!dcompModule) dcompModule = LoadLibraryW(L"dcomp.dll");
				if (!dcompModule) return false;
				auto createDevice = reinterpret_cast<CreateDeviceFunction>(GetProcAddress(dcompModule, "DCompositionCreateDevice"));
				if (!createDevice) return false;

				IDCompositionDevice* rawDevice = nullptr;
				HRESULT result = createDevice(dxgiDevice, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&rawDevice));
				if (FAILED(result) || !rawDevice) return false;
				compositionDevice.Attach(rawDevice);
				if (FAILED(result = compositionDevice->CreateTargetForHwnd(window, TRUE, &compositionTarget))) return false;
				if (FAILED(result = compositionDevice->CreateVisual(&rootVisual))) return false;
				if (FAILED(result = rootVisual->SetContent(swapChain))) return false;
				if (FAILED(result = compositionTarget->SetRoot(rootVisual))) return false;
				if (FAILED(result = compositionDevice->Commit())) return false;
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
				HRGN region = CreateRectRgn(0, 0, -1, -1);
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
				MARGINS margins = { -1, -1, -1, -1 };
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
		return IsDirectCompositionMode(kPreferredTransparentPresentMode) && IsDirectCompositionApiAvailable();
	}

	struct TransparentPresentationController::Impl
	{
		HWND window = nullptr;
		GraphicsDeviceResources* graphics = nullptr;
		InkRenderer* renderer = nullptr;
		CComPtr<IDXGISwapChain1> swapChain;
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
			swapChain.Release();
		}

		bool ConfigureWindow(TransparentPresentMode mode)
		{
			return EnsureBorderlessTransparentWindowStyle(window,
				IsDirectCompositionMode(mode), IsUlwMode(mode));
		}

		bool CreateSwapChain(TransparentPresentMode mode)
		{
			swapChain.Release();
			DXGI_SWAP_CHAIN_DESC1 description = {};
			description.Width = width;
			description.Height = height;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.BufferCount = 2;
			description.Scaling = DXGI_SCALING_STRETCH;
			description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			description.AlphaMode = IsGpuMode(mode) ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_UNSPECIFIED;

			HRESULT result = S_OK;
			if (IsDirectCompositionMode(mode))
			{
				result = graphics->factory->CreateSwapChainForComposition(graphics->device, &description, nullptr, &swapChain);
				if (FAILED(result)) LogHResult("CreateSwapChainForComposition", result);
				return SUCCEEDED(result) && swapChain;
			}

			if (IsDwmGlassMode(mode))
			{
				LogDwmModeDiagnostics(TransparentPresentModeName(mode), window, "Before CreateSwapChainForHwnd");
				LogSwapChainDescription(TransparentPresentModeName(mode), "Trying premultiplied alpha", description);
			}
			result = graphics->factory->CreateSwapChainForHwnd(graphics->device, window, &description, nullptr, nullptr, &swapChain);
			if (FAILED(result) && IsDwmGlassMode(mode))
			{
				BOOL opaqueBlend = TRUE;
				if (!LogDwmColorizationState(TransparentPresentModeName(mode), "Before unspecified alpha retry", &opaqueBlend)) return false;
				// Win7 Aero 对 HWND swapchain 使用 UNSPECIFIED alpha，保留原有兼容重试。
				swapChain.Release();
				description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
				LogSwapChainDescription(TransparentPresentModeName(mode), "Trying unspecified alpha", description);
				result = graphics->factory->CreateSwapChainForHwnd(graphics->device, window, &description, nullptr, nullptr, &swapChain);
			}
			if (FAILED(result) || !swapChain)
			{
				LogHResult("CreateSwapChainForHwnd", result);
				return false;
			}
			if (IsDwmGlassMode(mode))
			{
				LogSwapChainRuntimeDescription(TransparentPresentModeName(mode), swapChain, "Created");
			}
			return true;
		}

		bool InitializePresenter()
		{
			switch (activeMode)
			{
			case TransparentPresentMode::UlwDirtyRect:
				return ulwPresenter.Initialize(window, graphics->device, graphics->context, width, height);
			case TransparentPresentMode::DirectCompositionVisualTree:
				return directCompositionPresenter.Initialize(window, graphics->dxgiDevice, swapChain);
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
			ReleaseAttempt();
			activeMode = mode;
			std::cout << "Trying transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			if (!ConfigureWindow(mode) || !CreateSwapChain(mode)) return false;

			renderer->SetWindowBackgroundColor(IsGpuMode(mode) ? kTransparentLayerClearColor : kTransparentWindowBackgroundColor);
			if (!renderer->Init(graphics->device, graphics->context, swapChain, width, height)) return false;
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
			if (impl_->TryInitialize(modes[index])) return true;
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
			succeeded = impl_->ulwPresenter.Present(impl_->renderer->backBufferTexture, dirty, presentFull);
		}
		else
		{
			succeeded = impl_->PresentSwapChain(dirty, presentFull);
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

	DirectX::XMFLOAT4 TransparentPresentationController::WindowBackgroundColor() const
	{
		return IsGpuMode(impl_->activeMode) ? kTransparentLayerClearColor : kTransparentWindowBackgroundColor;
	}

	IDXGISwapChain1* TransparentPresentationController::SwapChain() const
	{
		return impl_->swapChain;
	}
}
