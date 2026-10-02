module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <dcomp.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "../../Helper/FailedCleanupDeadline.h"

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

		struct UlwDirtyCopyResult
		{
			bool allZeroAlpha = true;
			bool premultipliedAlphaValid = true;
			bool fullFrameAllZeroAlpha = false;
		};

		template <typename AfterCopy>
		UlwDirtyCopyResult CopyAndInspectUlwDirtyRows(const BYTE* mapped, size_t rowPitch,
			BYTE* dib, int dibWidth, int dibHeight, RECT dirty, bool presentFull,
			AfterCopy afterCopy)
		{
			const size_t pixelCount = static_cast<size_t>(dirty.right - dirty.left);
			const size_t copyBytes = pixelCount * 4;
			const BYTE* source = mapped + static_cast<size_t>(dirty.top) * rowPitch +
				static_cast<size_t>(dirty.left) * 4;
			BYTE* destination = dib + static_cast<size_t>(dirty.top) *
				static_cast<size_t>(dibWidth) * 4 + static_cast<size_t>(dirty.left) * 4;
			for (LONG y = dirty.top; y < dirty.bottom; ++y)
			{
				const size_t row = static_cast<size_t>(y - dirty.top);
				const BYTE* sourceRow = source + row * rowPitch;
				BYTE* destinationRow = destination + row * static_cast<size_t>(dibWidth) * 4;
				std::memcpy(destinationRow, sourceRow, copyBytes); // 逐行处理 RowPitch 和 DIB stride 不同的情况。
			}
			afterCopy(); // 生产路径仍在检查 DIB 前 Unmap staging texture。

			UlwDirtyCopyResult result;
			// 检查提交给 ULW 的 BGRA 像素，防止 alpha 或预乘语义回归。
			for (LONG y = dirty.top; y < dirty.bottom; ++y)
			{
				const BYTE* row = dib + static_cast<size_t>(y) *
					static_cast<size_t>(dibWidth) * 4;
				for (LONG x = dirty.left; x < dirty.right; ++x)
				{
					const BYTE* pixel = row + static_cast<size_t>(x) * 4;
					const BYTE alpha = pixel[3];
					result.allZeroAlpha = result.allZeroAlpha && alpha == 0;
					result.premultipliedAlphaValid = result.premultipliedAlphaValid &&
						pixel[0] <= alpha && pixel[1] <= alpha && pixel[2] <= alpha;
				}
			}
			result.fullFrameAllZeroAlpha = presentFull && result.allZeroAlpha &&
				dirty.left == 0 && dirty.top == 0 && dirty.right == dibWidth &&
				dirty.bottom == dibHeight;
			return result;
		}

		struct UlwDirtyRectPresenter
		{
			const char* diagnosticRole = "Drawpad";
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
			UINT finalTextureWidth = 0;
			UINT finalTextureHeight = 0;
			TransparentPresentObservation lastObservation{};
			bool presentSuccessLogged = false;
			bool presentFailureLogged = false;
			POINT lastDestination{};
			SIZE lastDestinationSize{};
			POINT lastSource{};
			BLENDFUNCTION lastBlend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			DWORD lastUpdateFlags = ULW_ALPHA;

			void LogPresentEvent(const char* eventName, RECT dirty, bool presentFull,
				DWORD error = ERROR_SUCCESS, HRESULT hresult = S_OK)
			{
				if (!eventName) return;
				RECT windowRect = {};
				const BOOL rectOk = window && GetWindowRect(window, &windowRect);
				const BOOL valid = window && IsWindow(window);
				const BOOL visible = valid && IsWindowVisible(window);
				std::cout << "[Draw3Diag][ulw] event=" << eventName
					<< " role=" << diagnosticRole
					<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
					<< " valid=" << std::dec << (valid ? 1 : 0)
					<< " visible=" << (visible ? 1 : 0)
					<< " style=0x" << std::hex
					<< static_cast<unsigned long>(valid ? GetWindowLongPtrW(window, GWL_STYLE) : 0)
					<< " exStyle=0x" << static_cast<unsigned long>(valid ? GetWindowLongPtrW(window, GWL_EXSTYLE) : 0)
					<< std::dec << " windowRectOk=" << (rectOk ? 1 : 0)
					<< " window=(" << windowRect.left << "," << windowRect.top << ","
					<< windowRect.right << "," << windowRect.bottom << ")"
					<< " dib=" << dibWidth << "x" << dibHeight
					<< " staging=" << stagingWidth << "x" << stagingHeight
					<< " texture=" << finalTextureWidth << "x" << finalTextureHeight
					<< " dirty=(" << dirty.left << "," << dirty.top << ","
					<< dirty.right << "," << dirty.bottom << ")"
					<< " full=" << (presentFull ? 1 : 0)
					<< " dst=(" << lastDestination.x << "," << lastDestination.y << ")"
					<< " dstSize=" << lastDestinationSize.cx << "x" << lastDestinationSize.cy
					<< " src=(" << lastSource.x << "," << lastSource.y << ")"
					<< " blend=" << static_cast<unsigned>(lastBlend.BlendOp) << ","
					<< static_cast<unsigned>(lastBlend.BlendFlags) << ","
					<< static_cast<unsigned>(lastBlend.SourceConstantAlpha) << ","
					<< static_cast<unsigned>(lastBlend.AlphaFormat)
					<< " hdcSrc=0x" << std::hex << reinterpret_cast<UINT_PTR>(memoryDC)
					<< " flags=0x" << std::hex << lastUpdateFlags << std::dec
					<< " hresult=0x" << std::hex << static_cast<unsigned long>(hresult)
					<< std::dec << " error=" << error << std::endl;
			}

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
				presentSuccessLogged = false;
				presentFailureLogged = false;
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
				const HRESULT createResult = device->CreateTexture2D(&description, nullptr,
					stagingTexture.ReleaseAndGetAddressOf());
				if (FAILED(createResult))
				{
					std::cout << "[Draw3Diag][ulw-init] role=" << diagnosticRole
						<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
						<< std::dec << " stage=CreateTexture2D size=" << width << "x" << height
						<< " hresult=0x" << std::hex
						<< static_cast<unsigned long>(createResult) << std::dec << std::endl;
					return false;
				}
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
				const auto fail = [&](const char* stage, DWORD error = ERROR_SUCCESS,
					HRESULT hresult = S_OK)
				{
					if (!presentFailureLogged)
					{
						std::cout << "[Draw3Diag][ulw] stage=" << stage
							<< " result=failed hresult=0x" << std::hex
							<< static_cast<unsigned long>(hresult) << std::dec << std::endl;
						LogPresentEvent("failure", dirty, presentFull, error, hresult);
						presentFailureLogged = true;
					}
					return false;
				};
				if (!context || !stagingTexture || !finalTexture)
					return fail("resource");
				if (!EnsureWindowDib())
				{
					const DWORD error = GetLastError();
					return fail("window-dib", error,
						error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL);
				}
				D3D11_TEXTURE2D_DESC finalDescription = {};
				finalTexture->GetDesc(&finalDescription);
				finalTextureWidth = finalDescription.Width;
				finalTextureHeight = finalDescription.Height;
				// Resize 可与本帧交错：拷贝范围必须同时受 GPU 两端纹理和当前 DIB 限制。
				const LONG copyWidth = std::min({ static_cast<LONG>(stagingWidth), static_cast<LONG>(dibWidth),
					static_cast<LONG>(finalDescription.Width) });
				const LONG copyHeight = std::min({ static_cast<LONG>(stagingHeight), static_cast<LONG>(dibHeight),
					static_cast<LONG>(finalDescription.Height) });
				if (copyWidth <= 0 || copyHeight <= 0) return fail("empty-size");
				if (presentFull) dirty = RECT{ 0, 0, copyWidth, copyHeight }; // 全量呈现时忽略传入脏区。
				dirty.left = std::max(0L, dirty.left);
				dirty.top = std::max(0L, dirty.top);
				dirty.right = std::min(copyWidth, dirty.right);
				dirty.bottom = std::min(copyHeight, dirty.bottom);
				if (dirty.left >= dirty.right || dirty.top >= dirty.bottom)
				{
					if (!presentSuccessLogged)
					{
						LogPresentEvent("success-empty", dirty, presentFull);
						presentSuccessLogged = true;
					}
					presentFailureLogged = false;
					return true;
				}

				D3D11_BOX sourceRegion = {
					static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
					static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1
				};
				context->CopySubresourceRegion(stagingTexture.Get(), 0, static_cast<UINT>(dirty.left),
					static_cast<UINT>(dirty.top), 0, finalTexture, 0, &sourceRegion); // 只把脏区从 GPU backbuffer 拷到可读纹理。
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				const HRESULT mapResult = context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
				if (FAILED(mapResult)) return fail("map", ERROR_SUCCESS, mapResult);
				const UlwDirtyCopyResult copyResult = CopyAndInspectUlwDirtyRows(
					static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
					static_cast<BYTE*>(dibBits), dibWidth, dibHeight, dirty, presentFull,
					[&] { context->Unmap(stagingTexture.Get(), 0); });
				lastObservation.premultipliedAlphaValid = copyResult.premultipliedAlphaValid;
				lastObservation.updatedRegionAllZeroAlpha = copyResult.allZeroAlpha;
				lastObservation.fullFrameAllZeroAlpha = copyResult.fullFrameAllZeroAlpha;

				RECT windowRect = {};
				if (!GetWindowRect(window, &windowRect))
				{
					const DWORD error = GetLastError();
					return fail("window-rect", error,
						error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL);
				}
				POINT destinationPoint = { windowRect.left, windowRect.top };
				SIZE destinationSize = { dibWidth, dibHeight };
				POINT sourcePoint = { 0, 0 };
				BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				lastDestination = destinationPoint;
				lastDestinationSize = destinationSize;
				lastSource = sourcePoint;
				lastBlend = blend;
				lastUpdateFlags = ULW_ALPHA;
				UPDATELAYEREDWINDOWINFO update = {};
				update.cbSize = sizeof(update);
				update.pptDst = &destinationPoint;
				update.psize = &destinationSize;
				update.hdcSrc = memoryDC;
				update.pptSrc = &sourcePoint;
				update.pblend = &blend;
				update.dwFlags = ULW_ALPHA;
				update.prcDirty = presentFull ? nullptr : &dirty; // 非全量时让 USER32 只更新改变区域。
				const BOOL updated = UpdateLayeredWindowIndirect(window, &update);
				if (!updated)
				{
					const DWORD error = GetLastError();
					return fail("UpdateLayeredWindowIndirect", error,
						error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL);
				}
				if (!presentSuccessLogged || presentFailureLogged)
				{
					LogPresentEvent(presentFailureLogged ? "recovery" : "success", dirty, presentFull);
					presentSuccessLogged = true;
				}
				presentFailureLogged = false;
				return true;
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
		HWND primaryWindow = nullptr;
		HWND selectionWindow = nullptr;
		GraphicsDeviceResources* graphics = nullptr;
		InkRenderer* renderer = nullptr;
		TransparentPresentationCallbacks callbacks = {};
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
		TransparentPresentMode activeMode = kPreferredTransparentPresentMode;
		UINT width = 0;
		UINT height = 0;
		bool presentFailureLogged = false;
		bool presentSuccessLogged = false;
		TransparentOutputTarget lastLoggedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		bool recoveryPending = false;
		HRESULT lastFailure = S_OK;
		TransparentPresentationOptions options = {};
		TransparentOutputTarget requestedOutputTarget =
			TransparentOutputTarget::PrimaryDrawpad;
		std::uint64_t requestedOutputRevision = 0;
		TransparentPresentObservation lastObservation{};
		UlwDirtyRectPresenter primaryUlwPresenter;
		UlwDirtyRectPresenter selectionUlwPresenter;
		DirectCompositionPresenter directCompositionPresenter;
		DwmBlurBehindPresenter dwmBlurPresenter;
		DwmExtendedFramePresenter dwmExtendedPresenter;

		void ResetPresenters()
		{
			primaryUlwPresenter.Reset();
			selectionUlwPresenter.Reset();
			directCompositionPresenter.Reset();
			dwmBlurPresenter.Reset();
			dwmExtendedPresenter.Reset();
			presentFailureLogged = false;
			presentSuccessLogged = false;
			lastLoggedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
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
			return ApplyExternalWindowStyle(primaryWindow, callbacks,
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
				LogDwmModeDiagnostics(TransparentPresentModeName(mode), primaryWindow, "Before CreateSwapChainForHwnd");
				LogSwapChainDescription(TransparentPresentModeName(mode),
					waitable ? "Trying waitable premultiplied alpha" : "Trying premultiplied alpha", description);
			}
			result = graphics->factory->CreateSwapChainForHwnd(graphics->device.Get(), primaryWindow, &description, nullptr, nullptr,
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
				result = graphics->factory->CreateSwapChainForHwnd(graphics->device.Get(), primaryWindow, &description, nullptr, nullptr,
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
			primaryUlwPresenter.diagnosticRole = "Drawpad";
			selectionUlwPresenter.diagnosticRole = "DrawpadPresentation";
			bool primaryInitialized = false;
			switch (activeMode)
			{
			case TransparentPresentMode::UlwDirtyRect:
				primaryInitialized = primaryUlwPresenter.Initialize(primaryWindow,
					graphics->device.Get(), graphics->context.Get(), width, height); break;
			case TransparentPresentMode::DirectCompositionVisualTree:
				primaryInitialized = directCompositionPresenter.Initialize(primaryWindow,
					graphics->dxgiDevice.Get(), swapChain.Get(), callbacks); break;
			case TransparentPresentMode::DwmBlurBehind:
				primaryInitialized = dwmBlurPresenter.Initialize(primaryWindow, callbacks); break;
			case TransparentPresentMode::DwmBlurBehind2:
				primaryInitialized = dwmExtendedPresenter.Initialize(primaryWindow, callbacks); break;
			default:
				return false;
			}
			if (!primaryInitialized) return false;
			// 辅助窗出生即带固定 layered/transparent 样式，只建立 CPU 读回目标。
			return selectionUlwPresenter.Initialize(selectionWindow,
				graphics->device.Get(), graphics->context.Get(), width, height);
		}

		bool TryInitialize(TransparentPresentMode mode,
			Shutdown::FailedCleanupSignal releaseSignal = {},
			Shutdown::FailedCleanupSignal terminalOuterSignal = {})
		{
			const auto beginFailure = [&]() noexcept
			{
				const ULONGLONG deadline = releaseSignal.BeginKnownFailure();
				terminalOuterSignal.BeginKnownFailure(deadline);
			};
			ReleaseAttempt(); // 每次尝试前清掉上一条路径留下的交换链和 presenter。
			// 自动、强制及恢复共用此入口；首发禁用两种历史 DWM 透明模式。
			if (!IsDirectCompositionMode(mode) && !IsUlwMode(mode))
			{
				beginFailure();
				std::cout << "Transparent present mode " << TransparentPresentModeName(mode)
					<< " is disabled." << std::endl;
				return false;
			}
			// Win7 无 DComp 时直接尝试 ULW，避免先改动主 HWND 的创建期样式合同。
			if (IsDirectCompositionMode(mode) && !IsDirectCompositionApiAvailable())
			{
				beginFailure();
				return false;
			}
			activeMode = mode;
			std::cout << "Trying transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			if (!ConfigureWindow(mode))
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				std::cout << "ConfigureWindow failed in mode " << TransparentPresentModeName(mode)
					<< " exStyle=0x" << std::hex
					<< static_cast<unsigned long>(GetWindowLongPtrW(primaryWindow, GWL_EXSTYLE))
					<< std::dec << " lastError=" << initializeError << std::endl;
				return false;
			}
			if (!CreateSwapChain(mode))
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				std::cout << "CreateSwapChain failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << initializeError << std::endl;
				return false;
			}
			// DComp 和 ULW 都输出实际 swapchain 描述，包含普通回退的 flags/effect/size。
			LogSwapChainRuntimeDescription(TransparentPresentModeName(mode),
				swapChain.Get(), "Draw3 created");

			if (!renderer->Init(graphics->device.Get(), graphics->context.Get(), swapChain.Get(), width, height))
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				std::cout << "Renderer initialization failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << initializeError << std::endl;
				return false;
			}
			if (!InitializePresenter())
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				std::cout << "Presenter initialization failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << initializeError << std::endl;
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
		impl_->primaryWindow = nullptr;
		impl_->selectionWindow = nullptr;
		impl_->graphics = nullptr;
		impl_->renderer = nullptr;
		impl_->callbacks = {};
		impl_->width = 0;
		impl_->height = 0;
		impl_->options = {};
		impl_->requestedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		impl_->requestedOutputRevision = 0;
		impl_->lastObservation = {};
		impl_->recoveryPending = false;
		impl_->lastFailure = S_OK;
		impl_->presentFailureLogged = false;
		impl_->presentSuccessLogged = false;
		impl_->lastLoggedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
	}

	bool TransparentPresentationController::Initialize(HWND primaryWindow, HWND selectionWindow,
		GraphicsDeviceResources& graphics,
		InkRenderer& renderer, UINT width, UINT height, TransparentPresentationCallbacks callbacks,
		TransparentPresentationOptions options)
	{
		impl_->primaryWindow = primaryWindow;
		impl_->selectionWindow = selectionWindow;
		impl_->graphics = &graphics;
		impl_->renderer = &renderer;
		impl_->callbacks = callbacks;
		impl_->width = width;
		impl_->height = height;
		impl_->options = options;
		impl_->recoveryPending = false;
		impl_->lastFailure = S_OK;
		impl_->requestedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		impl_->requestedOutputRevision = 0;
		impl_->lastObservation = {};
		impl_->presentFailureLogged = false;
		impl_->presentSuccessLogged = false;
		impl_->lastLoggedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		if (!primaryWindow || !selectionWindow || !IsWindow(primaryWindow) ||
			!IsWindow(selectionWindow))
		{
			options.failedCleanup.BeginKnownFailure();
			return false;
		}
		const auto tryStartupMode = [&](TransparentPresentMode mode, bool terminal)
		{
			Shutdown::FailedCleanupDeadline releaseCleanup(options.failedCleanup.Publisher());
			if (options.failedCleanup.HasPublisher()) releaseCleanup.PrepareOrFatal();
			const auto releaseSignal = releaseCleanup.Signal();
			const auto outerSignal = terminal ? options.failedCleanup : Shutdown::FailedCleanupSignal{};
			bool initialized = false;
			try
			{
				initialized = impl_->TryInitialize(mode, releaseSignal, outerSignal);
				if (!initialized)
				{
					outerSignal.BeginKnownFailure(releaseSignal.BeginKnownFailure());
					impl_->ReleaseAttempt();
					if (!terminal)
					{
						const size_t nextIndex = TransparentPresentModeIndex(mode) + 1;
						std::cout << "Transparent present mode " << TransparentPresentModeName(mode)
							<< " failed; fallback to " << TransparentPresentModeName(kTransparentPresentModes[nextIndex]) << "." << std::endl;
					}
				}
			}
			catch (...)
			{
				// 异常已终止整个 startup，Host 后续释放继承本次局部失败的原 tick。
				options.failedCleanup.BeginKnownFailure(releaseSignal.BeginKnownFailure());
				throw;
			}
			// 下一模式是新的普通初始化；前一失败释放必须完成并真正 join 后才继续。
			// 默认 empty Signal 不 Prepare，不增加 monitor 线程。
			releaseCleanup.CompleteOrFatal();
			return initialized;
		};
		if (options.requireMode)
			return tryStartupMode(options.requiredMode, true);
		const size_t modeCount = ARRAYSIZE(kTransparentPresentModes);
		const size_t beginIndex = options.allowDirectComposition ? 0 : 1;
		for (size_t index = beginIndex; index < modeCount; ++index)
		{
			if (tryStartupMode(kTransparentPresentModes[index], index + 1 == modeCount))
				return true; // 按优先级选第一个可用透明呈现路径。
		}
		options.failedCleanup.BeginKnownFailure();
		std::cout << "All transparent present modes failed." << std::endl;
		return false;
	}

	std::uint64_t TransparentPresentationController::SetOutputTarget(
		TransparentOutputTarget target) noexcept
	{
		if (impl_->requestedOutputTarget == target)
			return impl_->requestedOutputRevision;
		impl_->requestedOutputTarget = target;
		if (++impl_->requestedOutputRevision == 0)
			++impl_->requestedOutputRevision;
		if (StartupEnvironmentDiagnosticsEnabled())
		{
			std::cout << "[Draw3Diag][target] requested="
				<< (target == TransparentOutputTarget::SelectionUlw
					? "DrawpadPresentation" : "Drawpad")
				<< " revision=" << impl_->requestedOutputRevision << std::endl;
		}
		return impl_->requestedOutputRevision;
	}

	TransparentOutputTarget TransparentPresentationController::RequestedOutputTarget() const noexcept
	{
		return impl_->requestedOutputTarget;
	}

	std::uint64_t TransparentPresentationController::RequestedOutputRevision() const noexcept
	{
		return impl_->requestedOutputRevision;
	}

	bool TransparentPresentationController::RecoverFromRuntimeFailure()
	{
		if (!impl_ || !impl_->recoveryPending || !impl_->primaryWindow ||
			!impl_->selectionWindow ||
			!impl_->graphics || !impl_->renderer) return false;
		const HRESULT failure = impl_->lastFailure;
		const bool deviceLost = IsGraphicsDeviceLostError(failure);
		const TransparentPresentMode failedMode = impl_->activeMode;
		impl_->recoveryPending = false;
		impl_->ReleaseAttempt();
		RECT clientRect = {};
		if (GetClientRect(impl_->primaryWindow, &clientRect) &&
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
			// 强制后端也先完整重建一次；辅助 ULW 运行期失败不能退回主窗穿透。
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
		const size_t beginIndex = deviceLost ? automaticBeginIndex :
			(std::max)(automaticBeginIndex, failedIndex);
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
			succeeded = impl_->primaryUlwPresenter.Resize(width, height); break;
		case TransparentPresentMode::DirectCompositionVisualTree:
			succeeded = true; break;
		case TransparentPresentMode::DwmBlurBehind:
			LogDwmModeDiagnostics("DwmBlurBehind", impl_->primaryWindow, "Resize");
			succeeded = impl_->dwmBlurPresenter.Update(); break;
		case TransparentPresentMode::DwmBlurBehind2:
			LogDwmModeDiagnostics("DwmBlurBehind2", impl_->primaryWindow, "Resize");
			succeeded = impl_->dwmExtendedPresenter.Update(); break;
		default: break;
		}
		if (succeeded)
			succeeded = impl_->selectionUlwPresenter.Resize(width, height);
		if (!succeeded) impl_->SetFailure(E_FAIL);
		return succeeded;
	}

	bool TransparentPresentationController::Present(
		RECT dirty, bool presentFull, std::uint64_t contentRevision)
	{
		bool succeeded = false;
		const TransparentOutputTarget target = impl_->requestedOutputTarget;
		if (target == TransparentOutputTarget::SelectionUlw)
		{
			succeeded = impl_->selectionUlwPresenter.Present(
				impl_->renderer->backBufferTexture.Get(), dirty, presentFull);
			impl_->lastObservation = impl_->selectionUlwPresenter.lastObservation;
			if (!succeeded) impl_->SetFailure(E_FAIL);
		}
		else if (IsUlwMode(impl_->activeMode))
		{
			succeeded = impl_->primaryUlwPresenter.Present(
				impl_->renderer->backBufferTexture.Get(), dirty, presentFull); // ULW 需要 CPU 读回并调用 UpdateLayeredWindow。
			impl_->lastObservation = impl_->primaryUlwPresenter.lastObservation;
			if (!succeeded) impl_->SetFailure(E_FAIL);
		}
		else
		{
			const HRESULT result = impl_->PresentSwapChain(dirty, presentFull); // GPU 路径直接 Present1，DWM 读取 alpha。
			succeeded = SUCCEEDED(result);
			impl_->lastObservation = {};
			if (!succeeded || impl_->presentFailureLogged || !impl_->presentSuccessLogged ||
				impl_->lastLoggedOutputTarget != target)
			{
				if (!succeeded || StartupEnvironmentDiagnosticsEnabled())
				{
					std::cout << "[Draw3Diag][present] mode="
						<< TransparentPresentModeName(impl_->activeMode)
						<< " target="
						<< (target == TransparentOutputTarget::SelectionUlw
							? "DrawpadPresentation" : "Drawpad")
						<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(impl_->primaryWindow)
						<< " size=" << std::dec << impl_->width << "x" << impl_->height
						<< " dirty=(" << dirty.left << "," << dirty.top << ","
						<< dirty.right << "," << dirty.bottom << ")"
						<< " full=" << (presentFull ? 1 : 0)
						<< " result=0x" << std::hex << static_cast<unsigned long>(result)
						<< std::dec << std::endl;
				}
				impl_->lastLoggedOutputTarget = target;
			}
			if (!succeeded) impl_->SetFailure(result);
		}
		impl_->lastObservation.outputTarget = target;
		impl_->lastObservation.outputRevision = impl_->requestedOutputRevision;
		impl_->lastObservation.presentedContentRevision = contentRevision;
		if (!succeeded && !impl_->presentFailureLogged)
		{
			std::cout << "Present failed in mode " << TransparentPresentModeName(impl_->activeMode) << std::endl;
			impl_->presentFailureLogged = true;
		}
		if (succeeded)
		{
			impl_->presentFailureLogged = false;
			impl_->presentSuccessLogged = true;
		}
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
		return impl_->lastObservation;
	}

	bool TransparentPresentationController::IsGpuTransparentComposition() const
	{
		return IsGpuMode(impl_->activeMode);
	}

	IDXGISwapChain1* TransparentPresentationController::SwapChain() const
	{
		return impl_->swapChain.Get();
	}

	int RunUlwDirtyCopyBenchmark() noexcept
	{
		try
		{
			enum class PixelPattern { Mixed, Transparent, Half, InvalidAlpha };
			struct Scenario
			{
				const char* name;
				int width;
				int height;
				RECT dirty;
				bool presentFull;
				PixelPattern pattern;
				bool allZeroAlpha;
				bool premultipliedAlphaValid;
				bool fullFrameAllZeroAlpha;
			};
			constexpr Scenario scenarios[] = {
				{ "full_mixed", 1920, 1080, { 0, 0, 1920, 1080 }, true,
					PixelPattern::Mixed, false, true, false },
				{ "partial_256", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::Mixed, false, true, false },
				{ "narrow_long", 1920, 1080, { 951, 0, 959, 1080 }, false,
					PixelPattern::Mixed, false, true, false },
				{ "full_transparent", 1920, 1080, { 0, 0, 1920, 1080 }, true,
					PixelPattern::Transparent, true, true, true },
				{ "partial_transparent", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::Transparent, true, true, false },
				{ "partial_half", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::Half, false, true, false },
				{ "partial_invalid_alpha", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::InvalidAlpha, false, false, false },
			};
			constexpr int warmupBlocks = 16;
			constexpr int measuredBlocks = 128;
			LARGE_INTEGER frequency = {};
			if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 1;
			std::fprintf(stdout,
				"UlwDirtyCopyBenchmark variant=separate_dib_scan metric=cpu_copy_plus_alpha_scan sample=single_call warmup_blocks=%d measured_blocks=%d qpc_frequency=%lld\n",
				warmupBlocks, measuredBlocks, static_cast<long long>(frequency.QuadPart));

			for (const Scenario& scenario : scenarios)
			{
				const size_t dibStride = static_cast<size_t>(scenario.width) * 4;
				const size_t rowPitch = dibStride + 64; // 固定 padding 覆盖 staging RowPitch 与 DIB stride 不同的情况。
				std::vector<BYTE> source(rowPitch * static_cast<size_t>(scenario.height), 0xC7);
				std::vector<BYTE> initial(dibStride * static_cast<size_t>(scenario.height));
				std::vector<BYTE> expected(initial.size());
				std::vector<BYTE> dib(initial.size());
				for (size_t offset = 0; offset < initial.size(); offset += 4)
				{
					initial[offset] = 0xA5;
					initial[offset + 1] = 0x5A;
					initial[offset + 2] = 0x3C;
					initial[offset + 3] = 0; // 脏区外故意留无效预乘色，发现误扫全 DIB。
				}
				for (int y = 0; y < scenario.height; ++y)
				{
					for (int x = 0; x < scenario.width; ++x)
					{
						BYTE* pixel = source.data() + static_cast<size_t>(y) * rowPitch +
							static_cast<size_t>(x) * 4;
						const BYTE alpha = scenario.pattern == PixelPattern::Transparent ? 0 :
							(scenario.pattern == PixelPattern::Mixed ? static_cast<BYTE>(64 + (x + y) % 192) : 128);
						pixel[3] = alpha;
						pixel[0] = static_cast<BYTE>((x * 13 + y * 7) % (alpha + 1));
						pixel[1] = static_cast<BYTE>((x * 5 + y * 11) % (alpha + 1));
						pixel[2] = static_cast<BYTE>((x * 3 + y * 17) % (alpha + 1));
						if (scenario.pattern == PixelPattern::InvalidAlpha &&
							x == (scenario.dirty.left + scenario.dirty.right) / 2 &&
							y == (scenario.dirty.top + scenario.dirty.bottom) / 2)
							pixel[0] = 129;
					}
				}
				expected = initial;
				const size_t copyBytes = static_cast<size_t>(scenario.dirty.right - scenario.dirty.left) * 4;
				for (LONG y = scenario.dirty.top; y < scenario.dirty.bottom; ++y)
				{
					const size_t sourceOffset = static_cast<size_t>(y) * rowPitch +
						static_cast<size_t>(scenario.dirty.left) * 4;
					const size_t destinationOffset = static_cast<size_t>(y) * dibStride +
						static_cast<size_t>(scenario.dirty.left) * 4;
					std::memcpy(expected.data() + destinationOffset, source.data() + sourceOffset, copyBytes);
				}

				for (int block = 0; block < warmupBlocks + measuredBlocks; ++block)
				{
					std::copy(initial.begin(), initial.end(), dib.begin());
					UlwDirtyCopyResult result;
					ULONG64 startCycles = 0;
					ULONG64 stopCycles = 0;
					bool cyclesAvailable = QueryThreadCycleTime(GetCurrentThread(), &startCycles) != FALSE;
					LARGE_INTEGER start = {};
					LARGE_INTEGER stop = {};
					if (!QueryPerformanceCounter(&start)) return 1;
					result = CopyAndInspectUlwDirtyRows(source.data(), rowPitch, dib.data(),
						scenario.width, scenario.height, scenario.dirty, scenario.presentFull, [] {});
					if (!QueryPerformanceCounter(&stop)) return 1;
					cyclesAvailable = cyclesAvailable &&
						QueryThreadCycleTime(GetCurrentThread(), &stopCycles) != FALSE;
					if (result.allZeroAlpha != scenario.allZeroAlpha ||
						result.premultipliedAlphaValid != scenario.premultipliedAlphaValid ||
						result.fullFrameAllZeroAlpha != scenario.fullFrameAllZeroAlpha ||
						std::memcmp(dib.data(), expected.data(), dib.size()) != 0)
					{
						std::fprintf(stderr, "UlwDirtyCopyBenchmark mismatch scenario=%s block=%d\n",
							scenario.name, block);
						return 1;
					}
					if (block < warmupBlocks) continue;
					std::uint64_t hash = 14695981039346656037ull;
					for (BYTE value : dib) hash = (hash ^ value) * 1099511628211ull;
					for (BYTE flag : { static_cast<BYTE>(result.allZeroAlpha),
						static_cast<BYTE>(result.premultipliedAlphaValid),
						static_cast<BYTE>(result.fullFrameAllZeroAlpha) })
						hash = (hash ^ flag) * 1099511628211ull;
					std::fprintf(stdout,
						"UlwDirtyCopyBenchmark scenario=%s block=%d iterations=1 qpc_ticks=%lld cycles_available=%d thread_cycles=%llu hash=%016llx all_zero=%d premul_valid=%d full_zero=%d\n",
						scenario.name, block - warmupBlocks + 1,
						static_cast<long long>(stop.QuadPart - start.QuadPart), cyclesAvailable ? 1 : 0,
						static_cast<unsigned long long>(cyclesAvailable ? stopCycles - startCycles : 0),
						static_cast<unsigned long long>(hash), result.allZeroAlpha ? 1 : 0,
						result.premultipliedAlphaValid ? 1 : 0, result.fullFrameAllZeroAlpha ? 1 : 0);
				}
			}
			std::fflush(stdout);
			return 0;
		}
		catch (const std::exception& error)
		{
			std::fprintf(stderr, "UlwDirtyCopyBenchmark exception: %s\n", error.what());
			return 2;
		}
		catch (...)
		{
			std::fputs("UlwDirtyCopyBenchmark unknown exception\n", stderr);
			return 2;
		}
	}
}
