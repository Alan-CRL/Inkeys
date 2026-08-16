module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cstring>
#include <cwchar>
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

module Inkeys.Drawing.Draw3.transparent_presentation;

import Inkeys.Drawing.Draw3.diagnostics;

namespace Inkeys::Drawing::Draw3
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
		HMODULE LoadSystemLibrary(const wchar_t* fileName) noexcept
		{
			if (!fileName || fileName[0] == L'\0') return nullptr;
			wchar_t path[MAX_PATH] = {};
			UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
			if (length == 0 || length >= ARRAYSIZE(path)) return nullptr;
			if (path[length - 1] != L'\\')
			{
				if (length + 1 >= ARRAYSIZE(path)) return nullptr;
				path[length++] = L'\\';
			}
			const size_t nameLength = std::wcslen(fileName);
			if (nameLength >= ARRAYSIZE(path) - length) return nullptr;
			std::wmemcpy(path + length, fileName, nameLength + 1);
			return LoadLibraryW(path); // 绝对系统路径避免 DLL 搜索顺序劫持。
		}

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

		constexpr TransparentPresentMode kTransparentPresentModes[] = {
			TransparentPresentMode::DirectCompositionVisualTree,
			TransparentPresentMode::DwmBlurBehind2,
			TransparentPresentMode::DwmBlurBehind,
			TransparentPresentMode::UlwDirtyRect
		};

		size_t TransparentPresentModeIndex(TransparentPresentMode mode) noexcept
		{
			for (size_t index = 0; index < ARRAYSIZE(kTransparentPresentModes); ++index)
				if (kTransparentPresentModes[index] == mode) return index;
			return ARRAYSIZE(kTransparentPresentModes);
		}

		const char* SwapChainCreationStepName(TransparentPresentMode mode, bool waitable)
		{
			if (IsDirectCompositionMode(mode))
				return waitable ? "CreateWaitableSwapChainForComposition" : "CreateSwapChainForComposition";
			return waitable ? "CreateWaitableSwapChainForHwnd" : "CreateSwapChainForHwnd";
		}

		bool IsDirectCompositionApiAvailable()
		{
			HMODULE module = LoadSystemLibrary(L"dcomp.dll"); // 运行时探测失败时继续使用 ULW 回退。
			if (!module) return false;
			const FARPROC createDevice = GetProcAddress(module, "DCompositionCreateDevice");
			FreeLibrary(module);
			return createDevice != nullptr;
		}

		bool ApplyExternalWindowStyle(HWND window, const TransparentPresentationCallbacks& callbacks,
			bool noRedirectionBitmap, bool layered)
		{
			if (!window || !callbacks.setExtendedStyleFlags) return false;
			DWORD setMask = 0;
			DWORD clearMask = 0;
			if (noRedirectionBitmap) setMask |= WS_EX_NOREDIRECTIONBITMAP;
			else clearMask |= WS_EX_NOREDIRECTIONBITMAP;
			if (layered) setMask |= WS_EX_LAYERED;
			else clearMask |= WS_EX_LAYERED;
			return callbacks.setExtendedStyleFlags(callbacks.context, setMask, clearMask);
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
			TransparentPresentObservation lastObservation{};

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
				lastObservation = {};
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
				// 未绘制像素保持零 alpha，避免 ULW 把整窗当成不透明遮挡层。
				std::fill_n(static_cast<DWORD*>(dibBits), static_cast<size_t>(width) * height, 0u);
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
				lastObservation = {};
				lastObservation.ulw = true;
				lastObservation.usedDirtyRect = !presentFull;
				if (!context || !stagingTexture || !finalTexture || !EnsureWindowDib()) return false;
				D3D11_TEXTURE2D_DESC finalDescription = {};
				finalTexture->GetDesc(&finalDescription);
				// Resize 可与本帧交错：拷贝范围必须同时受 GPU 两端纹理和当前 DIB 限制。
				const LONG copyWidth = std::min({ static_cast<LONG>(stagingWidth), static_cast<LONG>(dibWidth),
					static_cast<LONG>(finalDescription.Width) });
				const LONG copyHeight = std::min({ static_cast<LONG>(stagingHeight), static_cast<LONG>(dibHeight),
					static_cast<LONG>(finalDescription.Height) });
				if (copyWidth <= 0 || copyHeight <= 0) return false;
				if (presentFull) dirty = RECT{ 0, 0, copyWidth, copyHeight }; // 全量呈现时忽略传入脏区。
				dirty.left = std::max(0L, dirty.left);
				dirty.top = std::max(0L, dirty.top);
				dirty.right = std::min(copyWidth, dirty.right);
				dirty.bottom = std::min(copyHeight, dirty.bottom);
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
				}
				context->Unmap(stagingTexture.Get(), 0);

				// 验收路径直接检查提交给 ULW 的 BGRA 像素，防止 alpha 或预乘语义回归。
				bool allZeroAlpha = true;
				bool premultipliedAlphaValid = true;
				const BYTE* dib = static_cast<const BYTE*>(dibBits);
				for (LONG y = dirty.top; y < dirty.bottom; ++y)
				{
					const BYTE* row = dib + static_cast<size_t>(y) *
						static_cast<size_t>(dibWidth) * 4;
					for (LONG x = dirty.left; x < dirty.right; ++x)
					{
						const BYTE* pixel = row + static_cast<size_t>(x) * 4;
						const BYTE alpha = pixel[3];
						allZeroAlpha = allZeroAlpha && alpha == 0;
						premultipliedAlphaValid = premultipliedAlphaValid &&
							pixel[0] <= alpha && pixel[1] <= alpha && pixel[2] <= alpha;
					}
				}
				lastObservation.premultipliedAlphaValid = premultipliedAlphaValid;
				lastObservation.fullFrameAllZeroAlpha = presentFull && allZeroAlpha &&
					dirty.left == 0 && dirty.top == 0 && dirty.right == dibWidth &&
					dirty.bottom == dibHeight;

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
				if (compositionTarget)
					(void)compositionTarget->SetRoot(nullptr);
				if (compositionDevice)
				{
					// 先把 visual tree 从外部 HWND 解挂并等待提交完成，避免后续 DWM/ULW 样式切换仍被旧 DComp target 占用。
					if (SUCCEEDED(compositionDevice->Commit()))
						(void)compositionDevice->WaitForCommitCompletion();
				}
				rootVisual.Reset();
				compositionTarget.Reset();
				compositionDevice.Reset();
				window = nullptr;
			}

			bool Initialize(HWND inputWindow, IDXGIDevice* dxgiDevice, IDXGISwapChain1* swapChain,
				const TransparentPresentationCallbacks& callbacks)
			{
				Reset();
				window = inputWindow;
				if (!window || !dxgiDevice || !swapChain) return false;
				if (!ApplyExternalWindowStyle(window, callbacks, true, false)) return false;
				if (!dcompModule) dcompModule = LoadSystemLibrary(L"dcomp.dll"); // 只从 System32 延迟加载，失败时仍回退 ULW。
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

			void Reset()
			{
				if (window && IsWindow(window))
				{
					DWM_BLURBEHIND blur = {};
					blur.dwFlags = DWM_BB_ENABLE;
					blur.fEnable = FALSE;
					(void)DwmEnableBlurBehindWindow(window, &blur);
				}
				window = nullptr;
			}

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

			bool Initialize(HWND inputWindow, const TransparentPresentationCallbacks& callbacks)
			{
				window = inputWindow;
				if (!ApplyExternalWindowStyle(window, callbacks, false, false)) return false;
				LogDwmModeDiagnostics("DwmBlurBehind", window, "Initialize");
				return Update();
			}
		};

		struct DwmExtendedFramePresenter
		{
			HWND window = nullptr;

			void Reset()
			{
				if (window && IsWindow(window))
				{
					const MARGINS margins = {};
					(void)DwmExtendFrameIntoClientArea(window, &margins);
				}
				window = nullptr;
			}

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

			bool Initialize(HWND inputWindow, const TransparentPresentationCallbacks& callbacks)
			{
				window = inputWindow;
				if (!ApplyExternalWindowStyle(window, callbacks, false, false)) return false;
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
		TransparentPresentationCallbacks callbacks = {};
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
		TransparentPresentMode activeMode = kPreferredTransparentPresentMode;
		UINT width = 0;
		UINT height = 0;
		bool presentFailureLogged = false;
		bool recoveryPending = false;
		HRESULT lastFailure = S_OK;
		TransparentPresentationOptions options = {};
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

		HRESULT DeviceFailureOr(HRESULT fallback) const noexcept
		{
			if (graphics && graphics->device)
			{
				const HRESULT removed = graphics->device->GetDeviceRemovedReason();
				if (IsGraphicsDeviceLostError(removed)) return removed;
			}
			return fallback;
		}

		void SetFailure(HRESULT result) noexcept
		{
			lastFailure = DeviceFailureOr(result);
			recoveryPending = true;
		}

		void ReleaseAttempt()
		{
			ResetPresenters();
			if (renderer) renderer->ReleaseResources();
			swapChain.Reset();
		}

		bool ConfigureWindow(TransparentPresentMode mode)
		{
			return ApplyExternalWindowStyle(window, callbacks,
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
				return directCompositionPresenter.Initialize(window, graphics->dxgiDevice.Get(), swapChain.Get(), callbacks);
			case TransparentPresentMode::DwmBlurBehind:
				return dwmBlurPresenter.Initialize(window, callbacks);
			case TransparentPresentMode::DwmBlurBehind2:
				return dwmExtendedPresenter.Initialize(window, callbacks);
			default:
				return false;
			}
		}

		bool TryInitialize(TransparentPresentMode mode)
		{
			ReleaseAttempt(); // 每次尝试前清掉上一条路径留下的交换链和 presenter。
			activeMode = mode;
			std::cout << "Trying transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			if (!ConfigureWindow(mode))
			{
				std::cout << "ConfigureWindow failed in mode " << TransparentPresentModeName(mode)
					<< " exStyle=0x" << std::hex
					<< static_cast<unsigned long>(GetWindowLongPtrW(window, GWL_EXSTYLE))
					<< std::dec << " lastError=" << GetLastError() << std::endl;
				return false;
			}
			if (!CreateSwapChain(mode))
			{
				std::cout << "CreateSwapChain failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << GetLastError() << std::endl;
				return false;
			}

			if (!renderer->Init(graphics->device.Get(), graphics->context.Get(), swapChain.Get(), width, height))
			{
				std::cout << "Renderer initialization failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << GetLastError() << std::endl;
				return false;
			}
			if (!InitializePresenter())
			{
				std::cout << "Presenter initialization failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << GetLastError() << std::endl;
				return false;
			}
			std::cout << "Active transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			return true;
		}

		HRESULT PresentSwapChain(RECT dirty, bool presentFull)
		{
			if (!swapChain) return E_FAIL;
			DXGI_PRESENT_PARAMETERS parameters = {};
			if (!presentFull)
			{
				dirty.left = std::max(0L, dirty.left);
				dirty.top = std::max(0L, dirty.top);
				dirty.right = std::min(static_cast<LONG>(width), dirty.right);
				dirty.bottom = std::min(static_cast<LONG>(height), dirty.bottom);
				if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) return S_OK;
				parameters.DirtyRectsCount = 1;
				parameters.pDirtyRects = &dirty;
			}
			return swapChain->Present1(0, 0, &parameters);
		}
	};

	TransparentPresentationController::TransparentPresentationController()
		: impl_(std::make_unique<Impl>())
	{
	}

	TransparentPresentationController::~TransparentPresentationController() = default;

	void TransparentPresentationController::Shutdown() noexcept
	{
		if (!impl_) return;
		impl_->ReleaseAttempt();
		impl_->window = nullptr;
		impl_->graphics = nullptr;
		impl_->renderer = nullptr;
		impl_->callbacks = {};
		impl_->width = 0;
		impl_->height = 0;
		impl_->options = {};
		impl_->recoveryPending = false;
		impl_->lastFailure = S_OK;
	}

	bool TransparentPresentationController::Initialize(HWND window, GraphicsDeviceResources& graphics,
		InkRenderer& renderer, UINT width, UINT height, TransparentPresentationCallbacks callbacks,
		TransparentPresentationOptions options)
	{
		impl_->window = window;
		impl_->graphics = &graphics;
		impl_->renderer = &renderer;
		impl_->callbacks = callbacks;
		impl_->width = width;
		impl_->height = height;
		impl_->options = options;
		impl_->recoveryPending = false;
		impl_->lastFailure = S_OK;
		if (options.requireMode)
		{
			const bool initialized = impl_->TryInitialize(options.requiredMode);
			if (!initialized) impl_->ReleaseAttempt();
			return initialized;
		}
		const size_t modeCount = ARRAYSIZE(kTransparentPresentModes);
		const size_t beginIndex = options.allowDirectComposition ? 0 : 1;
		for (size_t index = beginIndex; index < modeCount; ++index)
		{
			if (impl_->TryInitialize(kTransparentPresentModes[index])) return true; // 按优先级选第一个可用透明呈现路径。
			impl_->ReleaseAttempt();
			if (index + 1 < modeCount)
			{
				std::cout << "Transparent present mode " << TransparentPresentModeName(kTransparentPresentModes[index])
					<< " failed; fallback to " << TransparentPresentModeName(kTransparentPresentModes[index + 1]) << "." << std::endl;
			}
		}
		std::cout << "All transparent present modes failed." << std::endl;
		return false;
	}

	bool TransparentPresentationController::RecoverFromRuntimeFailure()
	{
		if (!impl_ || !impl_->recoveryPending || !impl_->window ||
			!impl_->graphics || !impl_->renderer) return false;
		const HRESULT failure = impl_->lastFailure;
		const bool deviceLost = IsGraphicsDeviceLostError(failure);
		const TransparentPresentMode failedMode = impl_->activeMode;
		impl_->recoveryPending = false;
		impl_->ReleaseAttempt();
		RECT clientRect = {};
		if (GetClientRect(impl_->window, &clientRect) &&
			clientRect.right > clientRect.left && clientRect.bottom > clientRect.top)
		{
			impl_->width = static_cast<UINT>(clientRect.right - clientRect.left);
			impl_->height = static_cast<UINT>(clientRect.bottom - clientRect.top);
		}

		if (deviceLost)
		{
			// 所有旧设备引用已由调用方 GPU cache 和 presenter 释放，随后在同一绘制线程重建。
			*impl_->graphics = {};
			if (!InitializeGraphicsDevice(*impl_->graphics))
			{
				impl_->lastFailure = failure;
				return false;
			}
		}

		if (impl_->options.requireMode)
		{
			// 强制后端用于隐藏测试；普通 presenter 失败时不能静默切换模式。
			if (!deviceLost) return false;
			if (impl_->TryInitialize(impl_->options.requiredMode))
			{
				impl_->lastFailure = S_OK;
				return true;
			}
			impl_->ReleaseAttempt();
			impl_->lastFailure = failure;
			return false;
		}

		const size_t failedIndex = TransparentPresentModeIndex(failedMode);
		const size_t automaticBeginIndex = impl_->options.allowDirectComposition ? 0 : 1;
		const size_t beginIndex = deviceLost ? automaticBeginIndex : failedIndex + 1;
		for (size_t index = beginIndex; index < ARRAYSIZE(kTransparentPresentModes); ++index)
		{
			if (impl_->TryInitialize(kTransparentPresentModes[index]))
			{
				impl_->lastFailure = S_OK;
				return true;
			}
			impl_->ReleaseAttempt();
		}
		impl_->lastFailure = failure;
		return false;
	}

	bool TransparentPresentationController::Resize(UINT width, UINT height)
	{
		impl_->width = width;
		impl_->height = height;
		bool succeeded = false;
		switch (impl_->activeMode)
		{
		case TransparentPresentMode::UlwDirtyRect:
			succeeded = impl_->ulwPresenter.Resize(width, height); break;
		case TransparentPresentMode::DirectCompositionVisualTree:
			succeeded = true; break;
		case TransparentPresentMode::DwmBlurBehind:
			LogDwmModeDiagnostics("DwmBlurBehind", impl_->window, "Resize");
			succeeded = impl_->dwmBlurPresenter.Update(); break;
		case TransparentPresentMode::DwmBlurBehind2:
			LogDwmModeDiagnostics("DwmBlurBehind2", impl_->window, "Resize");
			succeeded = impl_->dwmExtendedPresenter.Update(); break;
		default: break;
		}
		if (!succeeded) impl_->SetFailure(E_FAIL);
		return succeeded;
	}

	bool TransparentPresentationController::Present(RECT dirty, bool presentFull)
	{
		bool succeeded = false;
		if (IsUlwMode(impl_->activeMode))
		{
			succeeded = impl_->ulwPresenter.Present(impl_->renderer->backBufferTexture.Get(), dirty, presentFull); // ULW 需要 CPU 读回并调用 UpdateLayeredWindow。
			if (!succeeded) impl_->SetFailure(E_FAIL);
		}
		else
		{
			const HRESULT result = impl_->PresentSwapChain(dirty, presentFull); // GPU 路径直接 Present1，DWM 读取 alpha。
			succeeded = SUCCEEDED(result);
			if (!succeeded) impl_->SetFailure(result);
		}
		if (!succeeded && !impl_->presentFailureLogged)
		{
			std::cout << "Present failed in mode " << TransparentPresentModeName(impl_->activeMode) << std::endl;
			impl_->presentFailureLogged = true;
		}
		if (succeeded) impl_->presentFailureLogged = false;
		return succeeded;
	}

	bool TransparentPresentationController::RefreshAfterCompositionChanged()
	{
		bool succeeded = true;
		if (IsDwmBlurBehindMode(impl_->activeMode)) succeeded = impl_->dwmBlurPresenter.Update();
		if (IsDwmBlurBehind2Mode(impl_->activeMode)) succeeded = impl_->dwmExtendedPresenter.Update();
		if (!succeeded) impl_->SetFailure(E_FAIL);
		return succeeded;
	}

	void TransparentPresentationController::MarkRuntimeFailure(HRESULT result) noexcept
	{
		if (impl_) impl_->SetFailure(result);
	}

	bool TransparentPresentationController::RecoveryPending() const noexcept
	{
		return impl_ && impl_->recoveryPending;
	}

	HRESULT TransparentPresentationController::LastFailure() const noexcept
	{
		return impl_ ? impl_->lastFailure : E_FAIL;
	}

	TransparentPresentMode TransparentPresentationController::ActiveMode() const
	{
		return impl_->activeMode;
	}

	TransparentPresentObservation TransparentPresentationController::LastPresentObservation() const
	{
		return IsUlwMode(impl_->activeMode)
			? impl_->ulwPresenter.lastObservation : TransparentPresentObservation{};
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
