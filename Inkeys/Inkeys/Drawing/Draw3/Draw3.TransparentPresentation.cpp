module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include "../../../IdtStart.h"
#include "Draw3.CompositionClock.h"
#include "Draw3.Win7DwmPolicy.h"
#include "Draw3.FrameLatencyGate.h"
#include "Draw3.Presentation.h"
#include "Draw3.PresentationFallbackReport.h"
#include "Draw3.UlwReadbackPacing.h"
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
#include <iomanip>
#include <string>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

// ULW 逐行复制的单遍 SIMD 内核：ARM64 用 NEON，x86/x64 用 SSE2（v143 x86 默认 /arch:SSE2）。
#if defined(_M_ARM64)
#include <arm_neon.h>
#define INKEYS_ULW_COPY_NEON 1
#elif defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#define INKEYS_ULW_COPY_SSE2 1
#endif

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
		case TransparentPresentMode::DwmBlurBehind: return "Win7Dwm";
		default: return "Unknown";
		}
	}

	namespace
	{
		// 连续“GPU 忙超时”不计入熔断的时长上限（R5）：实测压力下单包排队可达 2.3 s，留出余量。
		constexpr double kGpuBusyNeutralLimitMs = 5000.0;

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

		bool IsGpuMode(TransparentPresentMode mode)
		{
			return IsDirectCompositionMode(mode) || IsDwmBlurBehindMode(mode);
		}

		constexpr TransparentPresentMode FirstProductMode(std::optional<TransparentPresentMode> preferred, bool allowDirectComposition) noexcept
		{
			if (preferred == TransparentPresentMode::DwmBlurBehind) return *preferred;
			if (preferred == TransparentPresentMode::UlwDirtyRect) return *preferred;
			return allowDirectComposition ? TransparentPresentMode::DirectCompositionVisualTree : TransparentPresentMode::UlwDirtyRect;
		}
		static_assert(FirstProductMode(std::nullopt, true) == TransparentPresentMode::DirectCompositionVisualTree);
		static_assert(FirstProductMode(std::nullopt, false) == TransparentPresentMode::UlwDirtyRect);
		static_assert(FirstProductMode(TransparentPresentMode::DwmBlurBehind, false) == TransparentPresentMode::DwmBlurBehind);
		static_assert(FirstProductMode(TransparentPresentMode::UlwDirtyRect, true) == TransparentPresentMode::UlwDirtyRect);
		static_assert(FirstProductMode(TransparentPresentMode::DirectCompositionVisualTree, false) == TransparentPresentMode::UlwDirtyRect);

		// 产品启动候选，ULW 恒为末项（terminal）。自动：DComp（可用时）→ Win7Dwm（仅 Win7 系统，SP1/Aero/着色
		// 先决条件由 TryInitialize 在改动窗口前检查）→ ULW；手选沿用“首选 → ULW”，手选 DComp 不经过 Win7Dwm。
		// Win7 没有 dcomp.dll，DComp 在改动窗口样式前即被拒绝，Win7Dwm 拿到的是未改动的窗口。
		struct ProductModeChain
		{
			TransparentPresentMode modes[3]{};
			size_t count = 0;
		};

		constexpr ProductModeChain BuildProductModeChain(std::optional<TransparentPresentMode> preferred,
			bool allowDirectComposition, bool belowWindows8) noexcept
		{
			ProductModeChain chain;
			const auto first = FirstProductMode(preferred, allowDirectComposition);
			if (first != TransparentPresentMode::UlwDirtyRect) chain.modes[chain.count++] = first;
			if (!preferred && belowWindows8 && first != TransparentPresentMode::DwmBlurBehind)
				chain.modes[chain.count++] = TransparentPresentMode::DwmBlurBehind;
			chain.modes[chain.count++] = TransparentPresentMode::UlwDirtyRect;
			return chain;
		}

		constexpr bool ChainIs(const ProductModeChain& chain, TransparentPresentMode a,
			std::optional<TransparentPresentMode> b = std::nullopt, std::optional<TransparentPresentMode> c = std::nullopt) noexcept
		{
			const size_t expected = 1 + (b ? 1 : 0) + (c ? 1 : 0);
			return chain.count == expected && chain.modes[0] == a && (!b || chain.modes[1] == *b) && (!c || chain.modes[2] == *c);
		}
		static_assert(ChainIs(BuildProductModeChain(std::nullopt, true, false),
			TransparentPresentMode::DirectCompositionVisualTree, TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(std::nullopt, true, true), TransparentPresentMode::DirectCompositionVisualTree,
			TransparentPresentMode::DwmBlurBehind, TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(std::nullopt, false, true),
			TransparentPresentMode::DwmBlurBehind, TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(std::nullopt, false, false), TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(TransparentPresentMode::DwmBlurBehind, false, true),
			TransparentPresentMode::DwmBlurBehind, TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(TransparentPresentMode::DwmBlurBehind, true, false),
			TransparentPresentMode::DwmBlurBehind, TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(TransparentPresentMode::DirectCompositionVisualTree, true, true),
			TransparentPresentMode::DirectCompositionVisualTree, TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(TransparentPresentMode::DirectCompositionVisualTree, false, true),
			TransparentPresentMode::UlwDirtyRect));
		static_assert(ChainIs(BuildProductModeChain(TransparentPresentMode::UlwDirtyRect, true, true), TransparentPresentMode::UlwDirtyRect));

		bool IsBelowWindows8() noexcept
		{
			const auto version = GetWindowsVersion();
			return version.majorVersion < 6 || (version.majorVersion == 6 && version.minorVersion < 2);
		}

		// 查询 API 返回值与玻璃是否可用是两个条件，保留具体失败步骤供设置页诊断。
		HRESULT CheckWin7Dwm(const char*& stage) noexcept
		{
			const auto version = GetWindowsVersion();
			return CheckWin7DwmEnvironment(version.majorVersion, version.minorVersion, version.buildNumber,
				stage, DwmIsCompositionEnabled, DwmGetColorizationColor);
		}

		const char* SwapChainCreationStepName(TransparentPresentMode mode, bool waitable)
		{
			if (IsDirectCompositionMode(mode))
				return waitable ? "CreateWaitableSwapChainForComposition" : "CreateSwapChainForComposition";
			return waitable ? "CreateWaitableSwapChainForHwnd" : "CreateSwapChainForHwnd";
		}

		double QpcMilliseconds() noexcept
		{
			static const double ticksToMilliseconds = []() noexcept
			{
				LARGE_INTEGER frequency = {};
				return QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
					? 1000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
			}();
			LARGE_INTEGER counter = {};
			QueryPerformanceCounter(&counter);
			return static_cast<double>(counter.QuadPart) * ticksToMilliseconds;
		}

		// DWM 合成刷新间隔，作为帧延迟等待超时的下界；取不到或不合理时按 60 Hz。
		double QueryCompositionRefreshMilliseconds() noexcept
		{
			DWM_TIMING_INFO timing = {};
			timing.cbSize = sizeof(timing);
			LARGE_INTEGER frequency = {};
			if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) &&
				QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
			{
				const double milliseconds = static_cast<double>(timing.qpcRefreshPeriod) * 1000.0 /
					static_cast<double>(frequency.QuadPart);
				if (milliseconds >= 1.0 && milliseconds <= 100.0) return milliseconds;
			}
			return kFrameLatencyDefaultRefreshMs;
		}

		HRESULT CheckDirectCompositionApi(const char*& stage)
		{
			stage = "LoadLibrary(dcomp.dll)";
			const HMODULE module = LoadSystemLibrary(L"dcomp.dll");
			if (!module)
			{
				const DWORD error = GetLastError();
				return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
			}
			stage = "GetProcAddress(DCompositionCreateDevice)";
			const FARPROC createDevice = GetProcAddress(module, "DCompositionCreateDevice");
			const DWORD error = createDevice ? ERROR_SUCCESS : GetLastError();
			FreeLibrary(module);
			return createDevice ? S_OK : (error ? HRESULT_FROM_WIN32(error) : E_FAIL);
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

		bool IsEmptyUlwRect(RECT rect) noexcept
		{
			return rect.left >= rect.right || rect.top >= rect.bottom;
		}

		RECT ClipUlwRect(RECT rect, RECT bounds) noexcept
		{
			return RECT{ std::max(bounds.left, rect.left), std::max(bounds.top, rect.top),
				std::min(bounds.right, rect.right), std::min(bounds.bottom, rect.bottom) };
		}

		RECT UnionUlwRect(RECT first, RECT second) noexcept
		{
			if (IsEmptyUlwRect(first)) return second;
			if (IsEmptyUlwRect(second)) return first;
			return RECT{ std::min(first.left, second.left), std::min(first.top, second.top),
				std::max(first.right, second.right), std::max(first.bottom, second.bottom) };
		}

		// 主画布异步读回每次调用时由 Impl 提供的帧时序与合成时钟。
		struct UlwFrameTiming
		{
			double nowMs = 0.0;
			double frameStartMs = 0.0;  // 0 = 帧循环外
			double frameIntervalMs = 1000.0 / 120.0;
			double refreshMs = kFrameLatencyDefaultRefreshMs;
			bool aligned = false;       // DWM 合成、对齐开启、即时模式且时钟有效
			const CompositionClock* clock = nullptr;
		};

		struct UlwPixelStatistics
		{
			size_t alphaNonzero = 0;
			size_t rgbNonzero = 0;
			BYTE maxBgra[4]{};
			RECT alphaBounds{}; // 非零 alpha 的半开区间；全零时保持空矩形。
			bool premultipliedAlphaValid = true;

			void Include(const BYTE* pixel, LONG x, LONG y)
			{
				if (pixel[3] != 0)
				{
					if (alphaNonzero++ == 0) alphaBounds = RECT{ x, y, x + 1, y + 1 };
					else
					{
						alphaBounds.left = std::min(alphaBounds.left, x);
						alphaBounds.top = std::min(alphaBounds.top, y);
						alphaBounds.right = std::max(alphaBounds.right, x + 1);
						alphaBounds.bottom = std::max(alphaBounds.bottom, y + 1);
					}
				}
				if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) ++rgbNonzero;
				for (size_t channel = 0; channel < 4; ++channel)
					maxBgra[channel] = std::max(maxBgra[channel], pixel[channel]);
				premultipliedAlphaValid = premultipliedAlphaValid &&
					pixel[0] <= pixel[3] && pixel[1] <= pixel[3] && pixel[2] <= pixel[3];
			}
		};

		struct UlwPixelDiagnostic
		{
			size_t pixelCount = 0;
			UlwPixelStatistics source;
			UlwPixelStatistics finalDib;
			POINT samplePosition{};
			BYTE sampleSourceBgra[4]{};
			BYTE sampleFinalBgra[4]{};
		};

		constexpr BYTE ComposePrimaryHitUnderlayAlpha(BYTE sourceAlpha) noexcept
		{
			const unsigned int alpha = sourceAlpha;
			return static_cast<BYTE>(alpha + ((255u - alpha) + 127u) / 255u);
		}

#if defined(INKEYS_ULW_COPY_NEON) || defined(INKEYS_ULW_COPY_SSE2)
		constexpr bool kUlwSimdCopyAvailable = true;
#else
		constexpr bool kUlwSimdCopyAvailable = false;
#endif

		// 单遍处理一行：复制到 DIB 的同时累计源 alpha 是否全零、按需补命中底层 alpha、检查最终像素预乘合法性。
		// 命中底层公式等价于 α ≤ 127 ? α + 1 : α；结果与标量两遍版本逐字节一致。
		inline void CopyUlwRowSinglePass(const BYTE* source, BYTE* destination, size_t pixelCount,
			bool applyPrimaryHitUnderlay, bool& anySourceAlpha, bool& premultipliedAlphaValid) noexcept
		{
			size_t x = 0;
#if defined(INKEYS_ULW_COPY_NEON)
			// 每次 16 字节 = 4 个 BGRA 像素；脏区 left 与 RowPitch 都可能让地址不对齐，使用非对齐读写。
			static constexpr std::uint8_t alphaLanes[16] = { 3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15 };
			const uint8x16_t broadcastAlpha = vld1q_u8(alphaLanes);
			const uint8x16_t alphaOnly = vreinterpretq_u8_u32(vdupq_n_u32(0xFF000000u));
			const uint8x16_t alphaOne = vreinterpretq_u8_u32(vdupq_n_u32(0x01000000u));
			const int8x16_t signedZero = vdupq_n_s8(0);
			uint8x16_t sourceAlphaBits = vdupq_n_u8(0);
			uint8x16_t invalidBits = vdupq_n_u8(0);
			for (; x + 4 <= pixelCount; x += 4)
			{
				uint8x16_t pixels = vld1q_u8(source + x * 4);
				sourceAlphaBits = vorrq_u8(sourceAlphaBits, vandq_u8(pixels, alphaOnly));
				if (applyPrimaryHitUnderlay)
				{
					// α ≤ 127 ⇔ 按有符号字节 ≥ 0；掩码只保留 α 通道的 +1，结果最大 128 不会溢出。
					const uint8x16_t lowAlpha = vcgeq_s8(vreinterpretq_s8_u8(pixels), signedZero);
					pixels = vaddq_u8(pixels, vandq_u8(lowAlpha, alphaOne));
				}
				// 把最终 α 复制到四个通道：max(像素, α) == α 当且仅当 B、G、R ≤ α。
				const uint8x16_t alpha = vqtbl1q_u8(pixels, broadcastAlpha);
				invalidBits = vorrq_u8(invalidBits, veorq_u8(vmaxq_u8(pixels, alpha), alpha));
				vst1q_u8(destination + x * 4, pixels);
			}
			anySourceAlpha = anySourceAlpha || vmaxvq_u8(sourceAlphaBits) != 0;
			premultipliedAlphaValid = premultipliedAlphaValid && vmaxvq_u8(invalidBits) == 0;
#elif defined(INKEYS_ULW_COPY_SSE2)
			const __m128i alphaOnly = _mm_set1_epi32(static_cast<int>(0xFF000000u));
			const __m128i alphaOne = _mm_set1_epi32(0x01000000);
			const __m128i minusOne = _mm_set1_epi32(-1);
			const __m128i zero = _mm_setzero_si128();
			__m128i sourceAlphaBits = zero;
			__m128i invalidBits = zero;
			for (; x + 4 <= pixelCount; x += 4)
			{
				__m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + x * 4));
				sourceAlphaBits = _mm_or_si128(sourceAlphaBits, _mm_and_si128(pixels, alphaOnly));
				if (applyPrimaryHitUnderlay)
				{
					// α ≤ 127 ⇔ 按有符号字节 > -1；掩码只保留 α 通道的 +1，结果最大 128 不会溢出。
					const __m128i lowAlpha = _mm_cmpgt_epi8(pixels, minusOne);
					pixels = _mm_add_epi8(pixels, _mm_and_si128(lowAlpha, alphaOne));
				}
				// SSE2 没有字节重排：用移位把 α 复制到四个通道，再用 max(像素, α) == α 判断预乘。
				__m128i alpha = _mm_srli_epi32(pixels, 24);
				alpha = _mm_or_si128(alpha, _mm_slli_epi32(alpha, 8));
				alpha = _mm_or_si128(alpha, _mm_slli_epi32(alpha, 16));
				invalidBits = _mm_or_si128(invalidBits, _mm_xor_si128(_mm_max_epu8(pixels, alpha), alpha));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(destination + x * 4), pixels);
			}
			anySourceAlpha = anySourceAlpha || _mm_movemask_epi8(_mm_cmpeq_epi8(sourceAlphaBits, zero)) != 0xFFFF;
			premultipliedAlphaValid = premultipliedAlphaValid && _mm_movemask_epi8(_mm_cmpeq_epi8(invalidBits, zero)) == 0xFFFF;
#endif
			// 行尾不足 4 像素（或无 SIMD 的平台）走标量。
			for (; x < pixelCount; ++x)
			{
				const BYTE* sourcePixel = source + x * 4;
				BYTE* pixel = destination + x * 4;
				std::memcpy(pixel, sourcePixel, 4);
				anySourceAlpha = anySourceAlpha || sourcePixel[3] != 0;
				if (applyPrimaryHitUnderlay) pixel[3] = ComposePrimaryHitUnderlayAlpha(sourcePixel[3]);
				premultipliedAlphaValid = premultipliedAlphaValid &&
					pixel[0] <= pixel[3] && pixel[1] <= pixel[3] && pixel[2] <= pixel[3];
			}
		}

		// ScalarOnly=true 保留原两遍标量实现，供基准与逐字节对照；像素诊断采样帧也走标量。
		template <bool InspectPixels = false, bool ScalarOnly = false, typename AfterCopy>
		UlwDirtyCopyResult CopyAndInspectUlwDirtyRows(const BYTE* mapped, size_t rowPitch,
			BYTE* dib, int dibWidth, int dibHeight, RECT dirty, bool presentFull,
			bool applyPrimaryHitUnderlay, AfterCopy afterCopy, UlwPixelDiagnostic* diagnostic = nullptr)
		{
			const size_t pixelCount = static_cast<size_t>(dirty.right - dirty.left);
			const size_t copyBytes = pixelCount * 4;
			const BYTE* source = mapped + static_cast<size_t>(dirty.top) * rowPitch +
				static_cast<size_t>(dirty.left) * 4;
			BYTE* destination = dib + static_cast<size_t>(dirty.top) *
				static_cast<size_t>(dibWidth) * 4 + static_cast<size_t>(dirty.left) * 4;
			UlwDirtyCopyResult result;
			if constexpr (!InspectPixels && !ScalarOnly && kUlwSimdCopyAvailable)
			{
				bool anySourceAlpha = false;
				bool premultipliedAlphaValid = true;
				for (LONG y = dirty.top; y < dirty.bottom; ++y)
				{
					const size_t row = static_cast<size_t>(y - dirty.top);
					CopyUlwRowSinglePass(source + row * rowPitch,
						destination + row * static_cast<size_t>(dibWidth) * 4, pixelCount,
						applyPrimaryHitUnderlay, anySourceAlpha, premultipliedAlphaValid);
				}
				result.allZeroAlpha = !anySourceAlpha;
				result.premultipliedAlphaValid = premultipliedAlphaValid;
			}
			else // 诊断采样帧与标量基准：原两遍实现（memcpy 后逐像素检查）。
			for (LONG y = dirty.top; y < dirty.bottom; ++y)
			{
				const size_t row = static_cast<size_t>(y - dirty.top);
				const BYTE* sourceRow = source + row * rowPitch;
				BYTE* destinationRow = destination + row * static_cast<size_t>(dibWidth) * 4;
				std::memcpy(destinationRow, sourceRow, copyBytes); // 逐行处理 RowPitch 和 DIB stride 不同的情况。
				for (size_t x = 0; x < pixelCount; ++x)
				{
					BYTE* pixel = destinationRow + x * 4;
					const BYTE sourceAlpha = sourceRow[x * 4 + 3];
					result.allZeroAlpha = result.allZeroAlpha && sourceAlpha == 0;
					if (applyPrimaryHitUnderlay)
					{
						// 黑色底层不改 RGB；按预乘 alpha 的 OVER 公式计算最终 8-bit alpha。
						pixel[3] = ComposePrimaryHitUnderlayAlpha(sourceAlpha);
					}
					if constexpr (InspectPixels)
					{
						// 原始 BGRA 在命中底层合成前的 staging 中读取，不能拿 alpha=1 当成出墨证据。
						const BYTE* sourcePixel = sourceRow + x * 4;
						const LONG canvasX = dirty.left + static_cast<LONG>(x);
						diagnostic->source.Include(sourcePixel, canvasX, y);
						diagnostic->finalDib.Include(pixel, canvasX, y);
						if (diagnostic->pixelCount++ == 0 || sourceAlpha > diagnostic->sampleSourceBgra[3])
						{
							diagnostic->samplePosition = POINT{ canvasX, y };
							std::memcpy(diagnostic->sampleSourceBgra, sourcePixel, 4);
							std::memcpy(diagnostic->sampleFinalBgra, pixel, 4);
						}
					}
					const BYTE alpha = pixel[3];
					result.premultipliedAlphaValid = result.premultipliedAlphaValid &&
						pixel[0] <= alpha && pixel[1] <= alpha && pixel[2] <= alpha;
				}
			}
			afterCopy(); // 最终像素检查完成后立即 Unmap staging texture。
			result.fullFrameAllZeroAlpha = presentFull && result.allZeroAlpha &&
				dirty.left == 0 && dirty.top == 0 && dirty.right == dibWidth &&
				dirty.bottom == dibHeight;
			return result;
		}

		struct UlwDirtyRectPresenter
		{
			const char* diagnosticRole = "Drawpad";
			bool applyPrimaryHitUnderlay = false;
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
			bool pixelDiagnosticLogged = false;
			bool forceFullUlwUpdate = false;
			bool needsFullReadback = true;
			bool committedFullFrameClean = false;
			ULONGLONG lastPixelDiagnosticTick = 0;
			POINT lastDestination{};
			SIZE lastDestinationSize{};
			POINT lastSource{};
			BLENDFUNCTION lastBlend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			DWORD lastUpdateFlags = ULW_ALPHA;

			// ===== 异步读回（仅主画布，design §5）=====
			// 最终纹理只有一张：GPU 按命令顺序执行，“拷贝 N 到槽”先于“画第 N+1 帧”完成，后续改写不影响已入队的拷贝。
			// DIB 持久，各次提交按顺序只写自己的脏区，所以每个槽只需包含本次提交的脏区。
			enum class CompletionPoint : std::uint8_t { Sync, FrameStart, Backpressure, Present, FrameEnd, BeforeIdle };
			struct Commit
			{
				bool active = false;
				int slot = 0;
				RECT dirty{};
				bool presentFull = false;
				double issuedMs = 0.0;   // 拷贝入队时刻
				double contentMs = 0.0;  // 本次提交包含的最早一帧写入纹理的时刻（节流合并时早于 issuedMs）
				double deadlineMs = 0.0; // 对齐时瞄准的截止，0 = 未对齐
			};
			struct Completion
			{
				bool ok = true;
				bool notReady = false;
				double mapWaitMs = 0.0;
				double rowsMs = 0.0;
				double ulwMs = 0.0;
			};
			struct IssueResult
			{
				bool ok = true;
				double waitedMs = 0.0;      // 本次调用中全部阻塞 Map 的时长（背压 + 立即完成）
				double currentWaitMs = 0.0; // 立即完成本次提交时的阻塞时长
			};
			struct PipelineSummary
			{
				double startMs = 0.0;
				std::uint32_t immediate = 0, deferred = 0, probeHit = 0, backpressure = 0, frameEnd = 0, beforeIdle = 0, skipped = 0, commits = 0;
				double mapWaitSumMs = 0.0, mapWaitMaxMs = 0.0, lagSumMs = 0.0, lagMaxMs = 0.0, rowsSumMs = 0.0, ulwSumMs = 0.0;
				std::uint32_t alignedCommits = 0, lateCount = 0;
				double leadSumMs = 0.0, leadMinMs = 0.0;
			};
			bool asyncFused = false; // 探测出现非“仍在绘制”的错误或第二个槽建不出来：改走同步，由 Impl 记为会话级熔断
			Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTextureAlt; // 第二个 Staging 槽，仅异步时创建
			Commit pending;
			RECT accumulatedDirty{};
			bool hasAccumulated = false;
			double accumulatedSinceMs = 0.0;
			UlwReadbackPacing pacing;
			UlwCommitPlanner planner;
			PipelineSummary pipelineSummary;
			// 成功的 UpdateLayeredWindow 次数与最近一次的完成时刻（单调递增，Reset 不清零），供设备性能统计按刷新周期计数
			std::uint64_t completedUpdates = 0;
			double lastUpdateMs = 0.0;

			ID3D11Texture2D* SlotTexture(int slot) const noexcept
			{
				return slot == 0 ? stagingTexture.Get() : stagingTextureAlt.Get();
			}

			void ClearAccumulated() noexcept
			{
				hasAccumulated = false;
				accumulatedDirty = {};
				accumulatedSinceMs = 0.0;
			}

			// 丢弃待完成提交与累计脏区（不 Map，设备可能已移除）：DIB 缺了这些内容，下一次必须整帧读回。
			void DiscardPipeline() noexcept
			{
				if (pending.active || hasAccumulated) needsFullReadback = true;
				pending = {};
				ClearAccumulated();
				planner.ForgetLastCommit();
			}

			void Fail(const char* stage, RECT dirty, bool presentFull, DWORD error = ERROR_SUCCESS, HRESULT hresult = S_OK)
			{
				if (presentFailureLogged) return;
				DiagnosticOutput::Line(DiagnosticOutput::OutError) << "[Draw3Diag][ulw] stage=" << stage
					<< " result=failed hresult=0x" << std::hex
					<< static_cast<unsigned long>(hresult) << std::dec << std::endl;
				LogPresentEvent("failure", dirty, presentFull, error, hresult);
				presentFailureLogged = true;
			}

			void LogPresentEvent(const char* eventName, RECT dirty, bool presentFull,
				DWORD error = ERROR_SUCCESS, HRESULT hresult = S_OK)
			{
				if (!eventName) return;
				RECT windowRect = {};
				const BOOL rectOk = window && GetWindowRect(window, &windowRect);
				const BOOL valid = window && IsWindow(window);
				const BOOL visible = valid && IsWindowVisible(window);
				// 失败/恢复事件带错误码时记 Warn，首次成功等常规事件为 Debug。
				DiagnosticOutput::Line(error != ERROR_SUCCESS || FAILED(hresult)
					? DiagnosticOutput::OutWarn : DiagnosticOutput::Target{}) << "[Draw3Diag][ulw] event=" << eventName
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
					<< " dirtyParameter=" << (presentFull || forceFullUlwUpdate ? "null" : "rect")
					<< " fullUpdateOverride=" << (forceFullUlwUpdate ? 1 : 0)
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

			void LogPixelDiagnostic(const UlwPixelDiagnostic& pixels, ULONGLONG tick,
				RECT dirty, bool presentFull, BOOL updated, DWORD error,
				double copyMapMs, double mapWaitMs, const char* point, ULONGLONG rowsMs, ULONGLONG ulwMs)
			{
				// 仅 Draw3 调试开关开启时采样：写入日志 Draw3 通道。
				DiagnosticOutput::Line line(DiagnosticOutput::Target{ DiagnosticOutput::Stream::Output, Inkeys::Log::Channel::Draw3 });
				line << "[Draw3Diag][ulw-pixels] role=" << diagnosticRole
					<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window) << std::dec
					<< " tick=" << tick << " dirty=(" << dirty.left << "," << dirty.top << ","
					<< dirty.right << "," << dirty.bottom << ") full=" << (presentFull ? 1 : 0)
					<< " dirtyParameter=" << (presentFull || forceFullUlwUpdate ? "null" : "rect")
					<< " fullUpdateOverride=" << (forceFullUlwUpdate ? 1 : 0)
					<< " copy_map_ms=" << copyMapMs << " map_wait_ms=" << mapWaitMs << " point=" << point
					<< " rows_ms=" << rowsMs << " ulw_ms=" << ulwMs
					<< " api=" << (updated ? "success" : "failed") << " error=" << error
					<< " underlay=" << (applyPrimaryHitUnderlay ? 1 : 0) << " pixels=" << pixels.pixelCount;
				const auto writeStatistics = [&](const char* name, const UlwPixelStatistics& stats)
				{
					line << " " << name << "AlphaNonzero=" << stats.alphaNonzero
						<< " " << name << "RgbNonzero=" << stats.rgbNonzero
						<< " " << name << "MaxBgra=(" << static_cast<unsigned>(stats.maxBgra[0]) << ","
						<< static_cast<unsigned>(stats.maxBgra[1]) << ","
						<< static_cast<unsigned>(stats.maxBgra[2]) << ","
						<< static_cast<unsigned>(stats.maxBgra[3]) << ")"
						<< " " << name << "AlphaBounds=(" << stats.alphaBounds.left << ","
						<< stats.alphaBounds.top << "," << stats.alphaBounds.right << ","
						<< stats.alphaBounds.bottom << ") " << name << "Premul="
						<< (stats.premultipliedAlphaValid ? 1 : 0);
				};
				writeStatistics("source", pixels.source);
				writeStatistics("finalDib", pixels.finalDib);
				line << " samplePos=(" << pixels.samplePosition.x << "," << pixels.samplePosition.y << ")";
				const auto writeSample = [&](const char* name, const BYTE* bgra)
				{
					line << " " << name << "=(" << static_cast<unsigned>(bgra[0]) << ","
						<< static_cast<unsigned>(bgra[1]) << "," << static_cast<unsigned>(bgra[2]) << ","
						<< static_cast<unsigned>(bgra[3]) << ")";
				};
				writeSample("sampleSourceBgra", pixels.sampleSourceBgra);
				writeSample("sampleFinalBgra", pixels.sampleFinalBgra);
				line << std::endl; // 只输出提交证据，不等同于桌面实际可见。
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

			std::uint64_t ReadbackPayloadBytes() const noexcept
			{
				return (stagingTexture ? static_cast<std::uint64_t>(stagingWidth) * stagingHeight * 4 : 0) +
					(stagingTextureAlt ? static_cast<std::uint64_t>(stagingWidth) * stagingHeight * 4 : 0) +
					(dibBits ? static_cast<std::uint64_t>(dibWidth) * dibHeight * 4 : 0);
			}

			void ReleaseReadbackResources() noexcept
			{
				DiscardPipeline();
				ReleaseDib();
				stagingTexture.Reset();
				stagingTextureAlt.Reset();
				stagingWidth = stagingHeight = 0;
				// HWND 的已提交画面和真实观察仍有效；下一次读回必须完整填充新 DIB。
				needsFullReadback = true;
			}

			void Reset()
			{
				ReleaseReadbackResources();
				device.Reset();
				context.Reset();
				stagingWidth = 0;
				stagingHeight = 0;
				window = nullptr;
				committedFullFrameClean = false;
				lastObservation = {};
				presentSuccessLogged = false;
				presentFailureLogged = false;
				pixelDiagnosticLogged = false;
				forceFullUlwUpdate = false;
				lastPixelDiagnosticTick = 0;
				asyncFused = false;
				pacing.Reset();
				planner.Reset();
				pipelineSummary = {};
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
					DiagnosticOutput::Line(DiagnosticOutput::OutError) << "[Draw3Diag][ulw-init] role=" << diagnosticRole
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

			// 按最终纹理尺寸准备 Staging；尺寸变化时旧槽内容作废。dualSlot 时补建第二个槽，失败则熔断为同步读回。
			bool EnsureStagingSlots(UINT width, UINT height, bool dualSlot)
			{
				if (!stagingTexture || stagingWidth != width || stagingHeight != height)
				{
					DiscardPipeline();
					stagingTextureAlt.Reset();
					needsFullReadback = true;
					committedFullFrameClean = false;
				}
				if (!CreateStagingTexture(width, height)) return false;
				if (!dualSlot || stagingTextureAlt) return true;
				D3D11_TEXTURE2D_DESC description = {};
				stagingTexture->GetDesc(&description);
				const HRESULT createResult = device->CreateTexture2D(&description, nullptr,
					stagingTextureAlt.ReleaseAndGetAddressOf());
				if (FAILED(createResult))
				{
					stagingTextureAlt.Reset();
					if (!asyncFused)
						DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "[Draw3Diag][ulw-pipeline] role=" << diagnosticRole
							<< " second staging slot unavailable size=" << width << "x" << height << " hresult=0x" << std::hex
							<< static_cast<unsigned long>(createResult) << std::dec << "; fall back to synchronous readback." << std::endl;
					asyncFused = true;
				}
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

				DiscardPipeline(); // 待完成提交写的是旧 DIB，重建后整帧读回。
				ReleaseDib();
				needsFullReadback = true;
				committedFullFrameClean = false;
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
				// 仅 Primary 需要命中底层；Selection 初始仍全透明并保持穿透。
				std::fill_n(static_cast<DWORD*>(dibBits), static_cast<size_t>(width) * height,
					applyPrimaryHitUnderlay ? 0x01000000u : 0u);
				return true;
			}

			bool Initialize(HWND inputWindow, ID3D11Device* inputDevice, ID3D11DeviceContext* inputContext, UINT width, UINT height,
				bool prepareReadback = true)
			{
				Reset();
				window = inputWindow;
				device = inputDevice;
				context = inputContext;
				// 显式诊断对照只控制 USER32 的 dirty 参数；不扩大 GPU copy 或改变帧调度。
				wchar_t fullUpdateValue[2] = {};
				forceFullUlwUpdate = GetEnvironmentVariableW(L"INKEYS_DRAW3_ULW_FULL_UPDATE",
					fullUpdateValue, ARRAYSIZE(fullUpdateValue)) == 1 && fullUpdateValue[0] == L'1';
				if (forceFullUlwUpdate)
					DiagnosticOutput::Line() << "[Draw3Diag][ulw-control] role=" << diagnosticRole
						<< " INKEYS_DRAW3_ULW_FULL_UPDATE=1 dirtyParameter=null" << std::endl;
				return window && IsWindow(window) && device && context && width != 0 && height != 0 &&
					(!prepareReadback || Resize(width, height));
			}

			bool Resize(UINT width, UINT height)
			{
				// Resize 时丢弃待完成提交与累计脏区（不 Map），下一帧整帧读回。
				DiscardPipeline();
				if (stagingWidth != width || stagingHeight != height) stagingTextureAlt.Reset();
				return CreateStagingTexture(width, height) && EnsureWindowDib();
			}

			enum class FramePrepare : std::uint8_t { Failed, Ready };

			// 校验资源、按需重建 Staging/DIB 并裁剪脏区。Resize 可与本帧交错：范围同时受最终纹理、Staging 和 DIB 限制。
			FramePrepare PrepareFrame(ID3D11Texture2D* finalTexture, RECT& dirty, bool& presentFull, bool dualSlot, RECT& copyBounds)
			{
				if (!context || !device || !finalTexture)
				{
					Fail("resource", dirty, presentFull);
					return FramePrepare::Failed;
				}
				D3D11_TEXTURE2D_DESC finalDescription = {};
				finalTexture->GetDesc(&finalDescription);
				if (!EnsureStagingSlots(finalDescription.Width, finalDescription.Height, dualSlot))
				{
					Fail("staging", dirty, presentFull);
					return FramePrepare::Failed;
				}
				if (!EnsureWindowDib())
				{
					const DWORD error = GetLastError();
					Fail("window-dib", dirty, presentFull, error,
						error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL);
					return FramePrepare::Failed;
				}
				// 回收/尺寸重建后不能局部填充一张新 DIB，也不能沿用旧空白证明。
				if (needsFullReadback) presentFull = true;
				finalTextureWidth = finalDescription.Width;
				finalTextureHeight = finalDescription.Height;
				const LONG copyWidth = std::min({ static_cast<LONG>(stagingWidth), static_cast<LONG>(dibWidth),
					static_cast<LONG>(finalDescription.Width) });
				const LONG copyHeight = std::min({ static_cast<LONG>(stagingHeight), static_cast<LONG>(dibHeight),
					static_cast<LONG>(finalDescription.Height) });
				if (copyWidth <= 0 || copyHeight <= 0)
				{
					Fail("empty-size", dirty, presentFull);
					return FramePrepare::Failed;
				}
				copyBounds = RECT{ 0, 0, copyWidth, copyHeight };
				if (presentFull) dirty = copyBounds; // 全量呈现时忽略传入脏区。
				dirty = ClipUlwRect(dirty, copyBounds);
				return FramePrepare::Ready;
			}

			void LogEmptySuccess(RECT dirty, bool presentFull)
			{
				if (!presentSuccessLogged)
				{
					LogPresentEvent("success-empty", dirty, presentFull);
					presentSuccessLogged = true;
				}
				presentFailureLogged = false;
			}

			void IssueCopy(ID3D11Texture2D* finalTexture, int slot, RECT dirty) noexcept
			{
				const D3D11_BOX sourceRegion = {
					static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
					static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1
				};
				context->CopySubresourceRegion(SlotTexture(slot), 0, static_cast<UINT>(dirty.left),
					static_cast<UINT>(dirty.top), 0, finalTexture, 0, &sourceRegion); // 只把脏区从最终纹理拷到可读纹理。
			}

			static const char* CompletionPointName(CompletionPoint point) noexcept
			{
				switch (point)
				{
				case CompletionPoint::FrameStart: return "C1";
				case CompletionPoint::Backpressure: return "C2";
				case CompletionPoint::Present: return "present";
				case CompletionPoint::FrameEnd: return "frame-end";
				case CompletionPoint::BeforeIdle: return "C3";
				default: return "sync";
				}
			}

			// 读回 slot 中的脏区，逐行复制到 DIB 并调用 UpdateLayeredWindowIndirect。
			// wait=false 只探测一次（DO_NOT_WAIT）：GPU 未完成时返回 notReady，什么也不做；不轮询、不自旋。
			Completion CompleteCommit(int slot, RECT dirty, bool presentFull, bool wait, CompletionPoint point, double issuedMs)
			{
				Completion completion;
				ID3D11Texture2D* staging = SlotTexture(slot);
				if (!staging)
				{
					completion.ok = false;
					Fail("staging-slot", dirty, presentFull);
					return completion;
				}
				const bool diagnosticsEnabled = StartupEnvironmentDiagnosticsEnabled();
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				const double mapStartedMs = QpcMilliseconds();
				HRESULT mapResult = context->Map(staging, 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (!wait && mapResult == DXGI_ERROR_WAS_STILL_DRAWING)
				{
					completion.notReady = true;
					return completion;
				}
				if (!wait && FAILED(mapResult) && SUCCEEDED(device->GetDeviceRemovedReason()))
				{
					// 探测出现“仍在绘制”以外的错误但设备未移除：本会话改走同步读回，这一次改用阻塞 Map 完成。
					if (!asyncFused)
						DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "[Draw3Diag][ulw-pipeline] role=" << diagnosticRole
							<< " Map(DO_NOT_WAIT) failed hresult=0x" << std::hex << static_cast<unsigned long>(mapResult)
							<< std::dec << "; fall back to synchronous readback for this session." << std::endl;
					asyncFused = true;
					mapResult = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
				}
				const double mappedMs = QpcMilliseconds();
				completion.mapWaitMs = mappedMs - mapStartedMs;
				if (FAILED(mapResult))
				{
					completion.ok = false;
					Fail("map", dirty, presentFull, ERROR_SUCCESS, mapResult);
					return completion;
				}
				const ULONGLONG diagnosticTick = diagnosticsEnabled ? GetTickCount64() : 0;
				const bool inspectPixels = diagnosticsEnabled &&
					(!pixelDiagnosticLogged || diagnosticTick - lastPixelDiagnosticTick >= 1000);
				std::optional<UlwPixelDiagnostic> pixels;
				if (inspectPixels) pixels.emplace();
				const auto copyRows = [&]<bool InspectPixels>()
				{
					return CopyAndInspectUlwDirtyRows<InspectPixels>(
						static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
						static_cast<BYTE*>(dibBits), dibWidth, dibHeight, dirty, presentFull,
						applyPrimaryHitUnderlay, [&] { context->Unmap(staging, 0); },
						InspectPixels ? &*pixels : nullptr);
				};
				// 非采样帧使用单遍 SIMD 实例，不执行任何诊断像素统计；采样帧走标量诊断实例。
				const UlwDirtyCopyResult copyResult = inspectPixels ?
					copyRows.operator()<true>() : copyRows.operator()<false>();
				const double rowsFinishedMs = QpcMilliseconds();
				completion.rowsMs = rowsFinishedMs - mappedMs;
				const ULONGLONG rowsFinishedTick = inspectPixels ? GetTickCount64() : 0;
				if (inspectPixels)
				{
					pixelDiagnosticLogged = true;
					lastPixelDiagnosticTick = diagnosticTick;
				}
				// 一次 Present 内可能完成多次提交（背压 + 本帧）：预乘失败累计，其余取最近一次。
				lastObservation.usedDirtyRect = lastObservation.usedDirtyRect || (!presentFull && !forceFullUlwUpdate);
				lastObservation.premultipliedAlphaValid = lastObservation.premultipliedAlphaValid && copyResult.premultipliedAlphaValid;
				lastObservation.updatedRegionAllZeroAlpha = copyResult.allZeroAlpha;
				lastObservation.fullFrameAllZeroAlpha = copyResult.fullFrameAllZeroAlpha;

				RECT windowRect = {};
				if (!GetWindowRect(window, &windowRect))
				{
					const DWORD error = GetLastError();
					completion.ok = false;
					Fail("window-rect", dirty, presentFull, error,
						error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL);
					return completion;
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
				update.prcDirty = presentFull || forceFullUlwUpdate ? nullptr : &dirty;
				const ULONGLONG ulwStartedTick = inspectPixels ? GetTickCount64() : 0;
				const BOOL updated = UpdateLayeredWindowIndirect(window, &update);
				const DWORD error = updated ? ERROR_SUCCESS : GetLastError(); // 日志前锁存本次 API 错误。
				const double ulwFinishedMs = QpcMilliseconds();
				completion.ulwMs = ulwFinishedMs - rowsFinishedMs;
				if (inspectPixels) LogPixelDiagnostic(*pixels, diagnosticTick, dirty, presentFull, updated, error,
					mappedMs - issuedMs, completion.mapWaitMs, CompletionPointName(point),
					rowsFinishedTick - diagnosticTick, GetTickCount64() - ulwStartedTick);
				if (!updated)
				{
					completion.ok = false;
					Fail("UpdateLayeredWindowIndirect", dirty, presentFull, error,
						error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL);
					return completion;
				}
				++completedUpdates;
				lastUpdateMs = ulwFinishedMs;
				if (!presentSuccessLogged || presentFailureLogged)
				{
					LogPresentEvent(presentFailureLogged ? "recovery" : "success", dirty, presentFull);
					presentSuccessLogged = true;
				}
				needsFullReadback = false;
				if (presentFull) committedFullFrameClean = copyResult.fullFrameAllZeroAlpha;
				else if (!copyResult.allZeroAlpha) committedFullFrameClean = false;
				presentFailureLogged = false;
				return completion;
			}

			// 同步读回（辅助输出、异步关闭或熔断）：保持原流程，单 Staging，拷贝后阻塞 Map，每帧提交。
			bool Present(ID3D11Texture2D* finalTexture, RECT dirty, bool presentFull)
			{
				lastObservation = {};
				lastObservation.ulw = true;
				lastObservation.usedDirtyRect = !presentFull && !forceFullUlwUpdate;
				RECT copyBounds = {};
				if (PrepareFrame(finalTexture, dirty, presentFull, false, copyBounds) == FramePrepare::Failed) return false;
				lastObservation.usedDirtyRect = !presentFull && !forceFullUlwUpdate;
				if (IsEmptyUlwRect(dirty))
				{
					LogEmptySuccess(dirty, presentFull);
					return true;
				}
				const double issuedMs = QpcMilliseconds();
				IssueCopy(finalTexture, 0, dirty);
				return CompleteCommit(0, dirty, presentFull, true, CompletionPoint::Sync, issuedMs).ok;
			}

			// 完成待完成提交；wait=false 只探测一次。完成（成功或失败）后清除 pending，失败时下一次整帧读回。
			// measureCommit：只有入队后当场完成的普通提交才计入提交耗时估计。整帧提交（Resize、清屏、输出交接）
			// 远慢于平时，延后完成的提交又跨了一帧，计入会把估计撑大，之后十余次提交的对齐时机都会偏离截止。
			Completion CompletePending(bool wait, CompletionPoint point, bool measureCommit = false)
			{
				if (!pending.active) return {};
				const Completion completion = CompleteCommit(pending.slot, pending.dirty, pending.presentFull,
					wait, point, pending.issuedMs);
				if (completion.notReady) return completion;
				const Commit done = pending;
				pending = {};
				if (!completion.ok)
				{
					needsFullReadback = true;
					return completion;
				}
				const double finishedMs = QpcMilliseconds();
				if (measureCommit && !done.presentFull) planner.OnCommitCompleted(finishedMs - done.issuedMs);
				PipelineSummary& summary = pipelineSummary;
				switch (point)
				{
				case CompletionPoint::FrameStart: ++summary.probeHit; break;
				case CompletionPoint::Backpressure: ++summary.backpressure; break;
				case CompletionPoint::BeforeIdle: ++summary.beforeIdle; break;
				case CompletionPoint::FrameEnd: ++summary.frameEnd; break;
				default: ++summary.immediate; break;
				}
				++summary.commits;
				summary.mapWaitSumMs += completion.mapWaitMs;
				summary.mapWaitMaxMs = (std::max)(summary.mapWaitMaxMs, completion.mapWaitMs);
				// commitLag：本次提交包含的最早一帧写入纹理到 ULW 返回，即“Present 回执”之后实际上屏还晚多久。
				const double lagMs = finishedMs - done.contentMs;
				summary.lagSumMs += lagMs;
				summary.lagMaxMs = (std::max)(summary.lagMaxMs, lagMs);
				summary.rowsSumMs += completion.rowsMs;
				summary.ulwSumMs += completion.ulwMs;
				if (done.deadlineMs > 0.0)
				{
					// lead = 截止 − 实际提交结束，正为提前；ULW 拿不到“是否赶上合成”的系统回执，只能看预测。
					const double leadMs = done.deadlineMs - finishedMs;
					summary.leadMinMs = summary.alignedCommits == 0 ? leadMs : (std::min)(summary.leadMinMs, leadMs);
					++summary.alignedCommits;
					summary.leadSumMs += leadMs;
					if (leadMs < 0.0) ++summary.lateCount;
				}
				return completion;
			}

			// 拷出一次提交：入队拷贝并 Flush（否则命令可能留在缓冲区，帧开头探测永远不就绪）；
			// 上一次提交仍待完成时先阻塞完成它（C2 背压：DIB 按顺序写入，CPU 最多领先一帧）；completeNow 时立即完成本次。
			IssueResult IssueCommit(ID3D11Texture2D* finalTexture, RECT dirty, bool presentFull, double contentMs,
				double deadlineMs, bool completeNow, CompletionPoint completePoint, double nowMs, double refreshMs, bool forced)
			{
				IssueResult result;
				const int slot = pending.active ? (pending.slot ^ 1) : 0;
				IssueCopy(finalTexture, slot, dirty);
				context->Flush();
				const Commit issued{ true, slot, dirty, presentFull, nowMs, contentMs, deadlineMs };
				ClearAccumulated();
				planner.OnCommitIssued(nowMs, deadlineMs, refreshMs, forced);
				if (pending.active)
				{
					const Completion previous = CompletePending(true, CompletionPoint::Backpressure);
					result.waitedMs += previous.mapWaitMs;
					if (!previous.ok)
					{
						needsFullReadback = true; // 本次拷贝随之作废，下一次整帧读回。
						result.ok = false;
						return result;
					}
				}
				pending = issued;
				if (!completeNow)
				{
					++pipelineSummary.deferred;
					return result;
				}
				const Completion current = CompletePending(true, completePoint, true);
				result.waitedMs += current.mapWaitMs;
				result.currentWaitMs = current.mapWaitMs;
				result.ok = current.ok;
				return result;
			}

			// 异步读回的 Present（主画布）：本帧照常画进纹理后，先判定是否拷出（节流/对齐），再拷贝入队、背压、按模式完成。
			// 返回成功 = 本帧已进入提交流程且此前完成的提交都成功（与 DComp Present1 同口径：已提交，不等于已上屏）。
			bool PresentAsync(ID3D11Texture2D* finalTexture, RECT dirty, bool presentFull,
				const UlwFrameTiming& timing, double& waitedMs)
			{
				lastObservation = {};
				lastObservation.ulw = true;
				RECT copyBounds = {};
				if (PrepareFrame(finalTexture, dirty, presentFull, true, copyBounds) == FramePrepare::Failed) return false;
				if (asyncFused)
				{
					// 熔断（第二个槽建不出来等）：先完成残留提交，再把累计脏区并入本帧走同步路径。
					const Completion previous = CompletePending(true, CompletionPoint::Present);
					waitedMs += previous.mapWaitMs;
					if (!previous.ok) return false;
					if (hasAccumulated && !presentFull) dirty = UnionUlwRect(dirty, ClipUlwRect(accumulatedDirty, copyBounds));
					ClearAccumulated();
					stagingTextureAlt.Reset(); // 同步路径只用一个槽，释放第二个槽的显存
					return Present(finalTexture, dirty, presentFull);
				}
				RECT merged = dirty;
				double contentMs = timing.nowMs;
				if (presentFull) ClearAccumulated();
				else if (hasAccumulated)
				{
					merged = UnionUlwRect(merged, ClipUlwRect(accumulatedDirty, copyBounds));
					contentMs = accumulatedSinceMs;
				}
				if (IsEmptyUlwRect(merged))
				{
					LogEmptySuccess(merged, presentFull);
					return true;
				}
				UlwCommitDecision decision = planner.Decide(timing.nowMs, timing.frameIntervalMs, timing.refreshMs,
					timing.aligned, *timing.clock);
				if (!presentFull && !decision.commit)
				{
					// 本帧照常画进纹理，只把脏区并入累计矩形，留给本周期最后赶得上的一帧（或进入空闲前）一起读回。
					if (!hasAccumulated) accumulatedSinceMs = timing.nowMs;
					accumulatedDirty = merged;
					hasAccumulated = true;
					++pipelineSummary.skipped;
					return true;
				}
				if (timing.frameStartMs > 0.0) planner.OnFrameWorkMeasured(timing.nowMs - timing.frameStartMs);
				// 整帧（Resize、恢复、输出交接、清屏等）不延后：调用方依赖它立即上屏。
				const bool completeNow = presentFull || pacing.ShouldCompleteAtFrameEnd(false, timing.nowMs);
				const IssueResult issued = IssueCommit(finalTexture, merged, presentFull, contentMs,
					decision.deadlineMs, completeNow, CompletionPoint::Present, timing.nowMs, timing.refreshMs, presentFull);
				waitedMs += issued.waitedMs;
				if (!issued.ok) return false;
				if (completeNow && !presentFull && timing.frameStartMs > 0.0)
					LogPacingTransition(pacing.OnFrameMeasured(QpcMilliseconds() - timing.frameStartMs,
						timing.frameIntervalMs, issued.currentWaitMs, timing.nowMs));
				return true;
			}

			// C1 帧开头：有待完成提交时探测一次（不等待）。累计脏区不在这里拷出：此时纹理还是上一帧的内容，
			// 拷出会占用本周期，挤掉本帧更新的内容（见 ServiceFrameEnd）。熔断后残留的累计脏区直接同步收尾。
			bool ServiceFrameStart(ID3D11Texture2D* finalTexture, const UlwFrameTiming& timing, double& waitedMs)
			{
				if (pending.active)
				{
					const Completion probe = CompletePending(asyncFused, CompletionPoint::FrameStart);
					waitedMs += probe.mapWaitMs;
					if (!probe.ok) return false;
				}
				if (!asyncFused || !hasAccumulated || !finalTexture) return true;
				const IssueResult issued = IssueCommit(finalTexture, accumulatedDirty, false, accumulatedSinceMs,
					0.0, true, CompletionPoint::FrameStart, timing.nowMs, timing.refreshMs, true);
				waitedMs += issued.waitedMs;
				return issued.ok;
			}

			// 固定帧截止等待前（帧尾）：累计脏区到点就拷出。此时纹理已含本帧最新内容；
			// 笔停在原地不再产生脏区时，也能在一个刷新周期内把最后内容提交上屏。
			bool ServiceFrameEnd(ID3D11Texture2D* finalTexture, const UlwFrameTiming& timing, double& waitedMs)
			{
				if (!hasAccumulated || !finalTexture) return true;
				const UlwCommitDecision decision = planner.Decide(timing.nowMs, timing.frameIntervalMs, timing.refreshMs,
					timing.aligned, *timing.clock);
				if (!decision.commit && !asyncFused) return true;
				const bool completeNow = asyncFused || pacing.ShouldCompleteAtFrameEnd(false, timing.nowMs);
				const IssueResult issued = IssueCommit(finalTexture, accumulatedDirty, false, accumulatedSinceMs,
					decision.deadlineMs, completeNow, CompletionPoint::FrameEnd, timing.nowMs, timing.refreshMs, !decision.commit);
				waitedMs += issued.waitedMs;
				if (!issued.ok) return false;
				// 与 Present 同口径反馈本帧耗时：只经帧尾提交时，即时模式的慢帧仍计数、流水模式的试验帧仍被评估。
				if (completeNow && !asyncFused && timing.frameStartMs > 0.0)
					LogPacingTransition(pacing.OnFrameMeasured(QpcMilliseconds() - timing.frameStartMs,
						timing.frameIntervalMs, issued.currentWaitMs, timing.nowMs));
				return true;
			}

			// C3 进入空闲等待前：阻塞完成待完成提交并拷出累计脏区，保证停笔后最后一帧上屏。
			// textureValid=false（本帧光栅失败，最终纹理可能已被部分覆写）时只完成已冻结在槽里的提交，
			// 累计脏区留给重建后的整帧（整帧会清空累计脏区）。
			bool CompleteBeforeIdle(ID3D11Texture2D* finalTexture, double nowMs, double& waitedMs, bool textureValid = true)
			{
				if (textureValid && hasAccumulated && finalTexture)
				{
					const IssueResult issued = IssueCommit(finalTexture, accumulatedDirty, false, accumulatedSinceMs,
						0.0, true, CompletionPoint::BeforeIdle, nowMs, 0.0, true);
					waitedMs += issued.waitedMs;
					return issued.ok;
				}
				if (textureValid) ClearAccumulated();
				const Completion completion = CompletePending(true, CompletionPoint::BeforeIdle);
				waitedMs += completion.mapWaitMs;
				return completion.ok;
			}

			void LogPacingTransition(UlwReadbackTransition transition)
			{
				if (transition == UlwReadbackTransition::None) return;
				DiagnosticOutput::Line() << "[Draw3Diag][ulw-pipeline] role=" << diagnosticRole << " mode="
					<< (transition == UlwReadbackTransition::EnteredPipelined ? "pipelined reason=gpu-bound" : "immediate reason=trial-fast")
					<< std::endl;
			}

			// 约每秒汇总一次（Debug 级别，默认不输出），不逐帧记录。
			void MaybeLogPipelineSummary(double nowMs, double periodMs, bool aligned)
			{
				PipelineSummary& summary = pipelineSummary;
				if (summary.startMs <= 0.0)
				{
					summary.startMs = nowMs;
					return;
				}
				if (nowMs - summary.startMs < 1000.0) return;
				const std::uint32_t commits = summary.commits;
				DiagnosticOutput::Line() << "[Draw3Diag][ulw-pipeline] mode="
					<< (pacing.Mode() == UlwReadbackMode::Pipelined ? "pipelined" : "immediate")
					<< " immediate=" << summary.immediate << " deferred=" << summary.deferred
					<< " probeHit=" << summary.probeHit << " backpressure=" << summary.backpressure
					<< " frameEnd=" << summary.frameEnd << " beforeIdle=" << summary.beforeIdle << " skipped=" << summary.skipped
					<< " mapWaitAvgMs=" << (commits ? summary.mapWaitSumMs / commits : 0.0)
					<< " mapWaitMaxMs=" << summary.mapWaitMaxMs
					<< " commitLagAvgMs=" << (commits ? summary.lagSumMs / commits : 0.0)
					<< " commitLagMaxMs=" << summary.lagMaxMs
					<< " rowsAvgMs=" << (commits ? summary.rowsSumMs / commits : 0.0)
					<< " ulwAvgMs=" << (commits ? summary.ulwSumMs / commits : 0.0) << std::endl;
				if (aligned || summary.alignedCommits)
					DiagnosticOutput::Line() << "[Draw3Diag][ulw-align] aligned=" << summary.alignedCommits
						<< " periodMs=" << periodMs << " marginMs=" << kCompositionDeadlineMarginMs
						<< " leadAvgMs=" << (summary.alignedCommits ? summary.leadSumMs / summary.alignedCommits : 0.0)
						<< " leadMinMs=" << summary.leadMinMs << " lateCount=" << summary.lateCount << std::endl;
				summary = {};
				summary.startMs = nowMs;
			}
		};

		struct DirectCompositionPresenter
		{
			HWND window = nullptr;
			HMODULE dcompModule = nullptr;
			CompositionClock phaseClock;
			Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice;
			Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget;
			Microsoft::WRL::ComPtr<IDCompositionVisual> rootVisual;
			using CreateDeviceFunction = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);

			~DirectCompositionPresenter()
			{
				if (Reset() && dcompModule) FreeLibrary(dcompModule);
			}

			bool Reset()
			{
				phaseClock = {};
				if (compositionTarget)
					(void)compositionTarget->SetRoot(nullptr);
				if (compositionDevice)
				{
					// 先把 visual tree 从外部 HWND 解挂；退休等待有界，超时不得复用旧表面或启动第二退休 worker。
					const auto device = compositionDevice;
					const auto retired = WaitForDCompRetirement([device]
					{
						return SUCCEEDED(device->Commit()) && SUCCEEDED(device->WaitForCommitCompletion());
					});
					if (retired != DCompRetirementResult::Completed) return false;
				}
				rootVisual.Reset();
				compositionTarget.Reset();
				compositionDevice.Reset();
				window = nullptr;
				return true;
			}

			HRESULT Initialize(HWND inputWindow, IDXGIDevice* dxgiDevice, IDXGISwapChain1* swapChain, const char*& stage)
			{
				// TryInitialize 已退休旧 presenter 并经 Window Service 核对窗口样式。
				window = inputWindow;
				stage = "DComp initialization";
				if (!window || !dxgiDevice || !swapChain) return E_INVALIDARG;
				stage = "LoadLibrary(dcomp.dll)";
				if (!dcompModule) dcompModule = LoadSystemLibrary(L"dcomp.dll");
				if (!dcompModule)
				{
					const DWORD error = GetLastError();
					return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
				}
				stage = "GetProcAddress(DCompositionCreateDevice)";
				auto createDevice = reinterpret_cast<CreateDeviceFunction>(GetProcAddress(dcompModule, "DCompositionCreateDevice"));
				if (!createDevice)
				{
					const DWORD error = GetLastError();
					return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
				}
				stage = "DCompositionCreateDevice";
				HRESULT hr = createDevice(dxgiDevice, __uuidof(IDCompositionDevice),
					reinterpret_cast<void**>(compositionDevice.ReleaseAndGetAddressOf()));
				if (FAILED(hr) || !compositionDevice) return FAILED(hr) ? hr : E_UNEXPECTED;
				stage = "IDCompositionDevice::CreateTargetForHwnd";
				hr = compositionDevice->CreateTargetForHwnd(window, TRUE, compositionTarget.ReleaseAndGetAddressOf());
				if (FAILED(hr)) return hr;
				stage = "IDCompositionDevice::CreateVisual";
				hr = compositionDevice->CreateVisual(rootVisual.ReleaseAndGetAddressOf());
				if (FAILED(hr)) return hr;
				stage = "IDCompositionVisual::SetContent";
				hr = rootVisual->SetContent(swapChain);
				if (FAILED(hr)) return hr;
				stage = "IDCompositionTarget::SetRoot";
				hr = compositionTarget->SetRoot(rootVisual.Get());
				if (FAILED(hr)) return hr;
				stage = "IDCompositionDevice::Commit";
				return compositionDevice->Commit();
			}
		};

		struct DwmBlurBehindPresenter
		{
			HWND window = nullptr;
			// Win7 没有 DComp 帧统计：相位对齐用 DwmGetCompositionTimingInfo 的整桌面合成时序（与 ULW 对齐同源）。
			CompositionClock phaseClock;

			void Reset()
			{
				phaseClock = {};
				if (window && IsWindow(window))
				{
					DWM_BLURBEHIND blur = {};
					blur.dwFlags = DWM_BB_ENABLE;
					blur.fEnable = FALSE;
					(void)DwmEnableBlurBehindWindow(window, &blur);
				}
				window = nullptr;
			}

			HRESULT Update(const char*& stage)
			{
				if (!window) return E_HANDLE;
				const HRESULT available = CheckWin7Dwm(stage);
				if (FAILED(available)) return available;
				stage = "CreateRectRgn";
				HRGN region = CreateRectRgn(0, 0, -1, -1); // 小区域启用玻璃，其余客户区按 alpha 合成并保持清晰。
				if (!region) return E_OUTOFMEMORY;
				DWM_BLURBEHIND blur = {};
				blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
				blur.fEnable = TRUE;
				blur.hRgnBlur = region;
				stage = "DwmEnableBlurBehindWindow";
				const HRESULT result = DwmEnableBlurBehindWindow(window, &blur);
				DeleteObject(region);
				return result;
			}
		};

	}

	bool IsWin7DwmAvailable() noexcept
	{
		const char* stage = nullptr;
		return SUCCEEDED(CheckWin7Dwm(stage));
	}

	bool ShouldPreconfigureNoRedirectionBitmap()
	{
		const char* stage = nullptr;
		return IsDirectCompositionMode(kPreferredTransparentPresentMode) && SUCCEEDED(CheckDirectCompositionApi(stage)); // 只有首选 DComp 且 API 存在时才预置扩展样式。
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
		DCompHandoffBarrierState dcompHandoffBarrier{};
		TransparentPresentObservation lastObservation{};
		std::string deviceInformation;
		const char* fallbackStage = nullptr;
		HRESULT fallbackResult = S_OK;
		PresentationFallbackReport fallbackReport;
		// 只对 Win7Dwm 降级生效（手选，或自动在本代启动时选中）：初始化成功但同一目标反复提交失败时终止无进展的恢复。
		std::optional<TransparentOutputTarget> recoveredUlwTarget;
		std::uint64_t recoveredUlwUpdates = 0;
		// 自动在本代启动时选中过 Win7Dwm：运行期恢复与手选一致，只降到 ULW，不再重进 Win7Dwm。
		bool automaticWin7DwmEntered = false;
		bool waitableSwapChain = false;
		// 帧延迟句柄属于当前交换链；gate 的熔断状态跨交换链重建保留到 Shutdown。
		HANDLE frameLatencyWaitHandle = nullptr;
		FrameLatencyGate frameLatencyGate;
		double compositionRefreshMs = kFrameLatencyDefaultRefreshMs;
		double frameLatencyTargetMs = 1000.0 / 120.0;
		FrameLatencyWaitReport presentLatencyWait;
		// 主窗交换链成功 Present1 次数/返回时刻。仅 DComp 用 GPU 完成查询估计等待，DWM 时序按需采样。
		std::uint64_t dcompPresents = 0;
		double lastDcompPresentMs = 0.0;
		Microsoft::WRL::ComPtr<ID3D11Query> gpuIdleQuery;
		bool gpuIdleQueryIssued = false;
		double displayTimingSampleMs = 0.0;
		CompositionTimingSample displayTiming;
		struct FrameLatencySummary
		{
			double startMs = 0.0, waitSumMs = 0.0, waitMaxMs = 0.0;
			std::uint32_t acquired = 0, timeouts = 0, repaid = 0, repayTimeouts = 0, gpuBusyTimeouts = 0;
		} frameLatencySummary;
		std::uint64_t frameLatencyGpuBusyTimeoutsTotal = 0; // 会话累计：超时时上一帧 GPU 仍未完成、不计入熔断的次数
		double frameLatencyGpuBusySinceMs = 0.0; // 本轮“GPU 忙超时”开始时刻；等到信号或判为空闲时清零
		// ULW 异步读回与 DWM 合成时钟（design §5.10/§5.11）。熔断只在本会话有效，Shutdown 清除。
		bool ulwAsyncFused = false;
		bool dwmComposing = false;
		double ulwRefreshMs = kFrameLatencyDefaultRefreshMs;
		const char* ulwRefreshSource = "default";
		CompositionClock compositionClock;
		double ulwFrameStartMs = 0.0;
		double ulwFrameIntervalMs = 1000.0 / 120.0;
		bool ulwTimingAligned = false;
		const char* ulwAlignReason = nullptr; // 上一次记录的对齐状态，变化时记一行

		bool UlwAsyncActive() const noexcept
		{
			return IsUlwMode(activeMode) && options.enableUlwAsyncReadback && !ulwAsyncFused &&
				requestedOutputTarget == TransparentOutputTarget::PrimaryDrawpad;
		}

		void SyncUlwFuse() noexcept
		{
			if (!primaryUlwPresenter.asyncFused || ulwAsyncFused) return;
			ulwAsyncFused = true;
			DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "[Draw3Diag][ulw-pipeline] async readback tripped; "
				"using synchronous readback for the rest of this session." << std::endl;
		}

		static CompositionTimingSample SampleCompositionTiming() noexcept
		{
			// Win8.1 起 hwnd 必须为空；空值在 Win7 上表示整个桌面合成的时序。
			CompositionTimingSample sample;
			DWM_TIMING_INFO timing = {};
			timing.cbSize = sizeof(timing);
			LARGE_INTEGER frequency = {};
			if (FAILED(DwmGetCompositionTimingInfo(nullptr, &timing)) ||
				!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return sample;
			const double ticksToMs = 1000.0 / static_cast<double>(frequency.QuadPart);
			sample.valid = true;
			sample.vblankMs = static_cast<double>(timing.qpcVBlank) * ticksToMs; // 与 QpcMilliseconds 同一时间基准
			sample.periodMs = static_cast<double>(timing.qpcRefreshPeriod) * ticksToMs;
			return sample;
		}

		// 启动、DWM 合成状态变化与 Resize 时检测 DWM 是否在合成，刷新间隔来源 DWM → 显示器 → 60 Hz。
		void RefreshUlwCompositionState(const char* reason) noexcept
		{
			if (!IsUlwMode(activeMode)) return;
			BOOL enabled = FALSE;
			dwmComposing = SUCCEEDED(DwmIsCompositionEnabled(&enabled)) && enabled;
			ulwRefreshMs = 0.0;
			ulwRefreshSource = "default";
			if (dwmComposing)
			{
				const CompositionTimingSample sample = SampleCompositionTiming();
				if (sample.valid && sample.periodMs >= kCompositionClockMinPeriodMs &&
					sample.periodMs <= kCompositionClockMaxPeriodMs)
				{
					ulwRefreshMs = sample.periodMs;
					ulwRefreshSource = "dwm";
				}
			}
			if (ulwRefreshMs <= 0.0 && primaryWindow)
			{
				// DWM 未合成（Win7 Basic/经典主题/DWM 未运行）时 GDI 直接画屏，用窗口所在显示器的刷新率节流。
				MONITORINFOEXW monitor = {};
				monitor.cbSize = sizeof(monitor);
				DEVMODEW mode = {};
				mode.dmSize = sizeof(mode);
				const HMONITOR handle = MonitorFromWindow(primaryWindow, MONITOR_DEFAULTTONEAREST);
				if (handle && GetMonitorInfoW(handle, &monitor) &&
					EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
					mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency < 1000)
				{
					ulwRefreshMs = 1000.0 / static_cast<double>(mode.dmDisplayFrequency);
					ulwRefreshSource = "display";
				}
			}
			if (ulwRefreshMs <= 0.0) ulwRefreshMs = kFrameLatencyDefaultRefreshMs;
			compositionClock.Invalidate();
			DiagnosticOutput::Line() << "[Draw3Diag][ulw-dwm] reason=" << reason
				<< " composing=" << (dwmComposing ? 1 : 0)
				<< " refreshMs=" << ulwRefreshMs << " source=" << ulwRefreshSource
				<< " asyncReadback=" << (options.enableUlwAsyncReadback && !ulwAsyncFused ? 1 : 0)
				<< " align=" << (options.enableUlwCompositionAlign ? 1 : 0) << std::endl;
		}

		// 每次异步读回调用前组装帧时序；合成时钟约每 250 ms 重读一次，不逐帧调用 DWM。
		UlwFrameTiming MakeUlwTiming(double nowMs) noexcept
		{
			if (dwmComposing && compositionClock.NeedsRefresh(nowMs))
				compositionClock.Update(SampleCompositionTiming(), nowMs);
			UlwFrameTiming timing;
			timing.nowMs = nowMs;
			timing.frameStartMs = ulwFrameStartMs;
			timing.frameIntervalMs = ulwFrameIntervalMs;
			timing.refreshMs = compositionClock.Valid() ? compositionClock.PeriodMs() : ulwRefreshMs;
			timing.clock = &compositionClock;
			const char* reason = "aligned";
			if (!options.enableUlwCompositionAlign) reason = "disabled";
			else if (!dwmComposing) reason = "not-composing";
			else if (!compositionClock.Valid()) reason = "clock-invalid";
			else if (primaryUlwPresenter.pacing.Mode() == UlwReadbackMode::Pipelined) reason = "pipelined";
			else if (ClassifyCompositionCadence(timing.frameIntervalMs, timing.refreshMs) ==
				CompositionCadence::CommitEveryFrame) reason = "non-divisor";
			timing.aligned = std::strcmp(reason, "aligned") == 0;
			ulwTimingAligned = timing.aligned;
			if (!ulwAlignReason || std::strcmp(reason, ulwAlignReason) != 0)
			{
				// 对齐启停各记一行；非约数预设建议改为刷新率或其约数。
				ulwAlignReason = reason;
				DiagnosticOutput::Line() << "[Draw3Diag][ulw-align] aligned=" << (timing.aligned ? 1 : 0)
					<< " reason=" << reason << " frameIntervalMs=" << timing.frameIntervalMs
					<< " refreshMs=" << timing.refreshMs << std::endl;
			}
			return timing;
		}

		~Impl() { CloseFrameLatencyHandle(); }

		void CloseFrameLatencyHandle() noexcept
		{
			if (frameLatencyWaitHandle) CloseHandle(frameLatencyWaitHandle);
			frameLatencyWaitHandle = nullptr;
			frameLatencyGate.Reset();
			frameLatencySummary = {};
			frameLatencyGpuBusySinceMs = 0.0;
			presentLatencyWait = {};
		}

		// countable=false：补等或排除状态，沿用 Neutral/Exclude 原策略；
		// countable=true：超时那一刻上一帧 GPU 工作仍未完成，说明是 GPU 被其他程序占用（饥饿），信号迟到而非丢失，
		// 记为 GpuBusy（不计数也不打断连续计数，欠账按更高上限记账）；GPU 已空闲而信号仍不来才计入熔断。
		FrameLatencyWaitResult WaitFrameLatency(FrameLatencyTimeoutPolicy policy, bool countable = false) noexcept
		{
			const HANDLE handle = frameLatencyWaitHandle;
			bool gpuBusyTimeout = false;
			const FrameLatencyWaitResult result = frameLatencyGate.Acquire(
				frameLatencyTargetMs, compositionRefreshMs,
				[this, policy, countable, &gpuBusyTimeout]() noexcept
				{
					// 等待期间窗口可能转为隐藏/最小化或进入交接：超时后重新检查，按排除处理；非计数状态清零 5 秒忙计时。
					if (countable && (dcompHandoffBarrier.pendingOutputRevision != 0 || !IsWindowVisible(primaryWindow) ||
						IsIconic(primaryWindow)))
					{
						frameLatencyGpuBusySinceMs = 0.0;
						return FrameLatencyTimeoutPolicy::Exclude;
					}
					if (!countable)
					{
						frameLatencyGpuBusySinceMs = 0.0;
						return policy;
					}
					// 查询在 Present1 之后才插入，可能仍留在未提交的命令缓冲里：这里允许提交，否则可能永远读到“忙”。
					if (PreviousFrameGpuIdle(true))
					{
						frameLatencyGpuBusySinceMs = 0.0;
						return FrameLatencyTimeoutPolicy::Count;
					}
					// 上限：连续 GPU 忙超过 kGpuBusyNeutralLimitMs 仍等不到任何信号时照常计数，
					// 避免查询长期未就绪掩盖真正的信号丢失（届时熔断重建为普通链，行为与改动前一致）。
					const double nowMs = QpcMilliseconds();
					if (frameLatencyGpuBusySinceMs <= 0.0) frameLatencyGpuBusySinceMs = nowMs;
					if (nowMs - frameLatencyGpuBusySinceMs >= kGpuBusyNeutralLimitMs) return FrameLatencyTimeoutPolicy::Count;
					gpuBusyTimeout = true;
					return FrameLatencyTimeoutPolicy::GpuBusy;
				},
				[]() noexcept { return QpcMilliseconds(); },
				[handle](std::uint32_t timeoutMilliseconds) noexcept
				{
					// 内核等待，不自旋；超时由调用方按“本帧照常呈现”处理。
					switch (WaitForSingleObjectEx(handle, timeoutMilliseconds, FALSE))
					{
					case WAIT_OBJECT_0: return FrameLatencySignal::Signaled;
					case WAIT_TIMEOUT: return FrameLatencySignal::TimedOut;
					default: return FrameLatencySignal::Failed;
					}
				});
			if (result.outcome != FrameLatencyWaitOutcome::Signaled &&
				result.outcome != FrameLatencyWaitOutcome::TimedOut) return result;
			FrameLatencySummary& summary = frameLatencySummary;
			if (result.outcome == FrameLatencyWaitOutcome::Signaled)
			{
				++summary.acquired;
				frameLatencyGpuBusySinceMs = 0.0;
			}
			else ++summary.timeouts;
			if (gpuBusyTimeout)
			{
				++summary.gpuBusyTimeouts;
				++frameLatencyGpuBusyTimeoutsTotal;
			}
			if (result.debtRepaid) ++summary.repaid;
			if (result.debtRepayTimedOut) ++summary.repayTimeouts;
			summary.waitSumMs += result.waitedMs;
			summary.waitMaxMs = (std::max)(summary.waitMaxMs, result.waitedMs);
			const double nowMs = QpcMilliseconds();
			if (summary.startMs <= 0.0) summary.startMs = nowMs;
			else if (nowMs - summary.startMs >= 1000.0)
			{
				// 约每秒汇总一次（Debug 级别，默认不输出），不逐帧记录。
				const std::uint32_t waits = summary.acquired + summary.timeouts;
				DiagnosticOutput::Line() << "[Draw3Diag][latency] acquired=" << summary.acquired
					<< " timeouts=" << summary.timeouts
					<< " gpuBusyTimeouts=" << summary.gpuBusyTimeouts
					<< " repaid=" << summary.repaid << " repayTimeouts=" << summary.repayTimeouts
					<< " debt=" << frameLatencyGate.Debt()
					<< " debtOverflowTotal=" << frameLatencyGate.TotalDebtOverflows()
					<< " waitAvgMs=" << (waits ? summary.waitSumMs / waits : 0.0)
					<< " waitMaxMs=" << summary.waitMaxMs
					<< " timeoutMs=" << result.timeoutMs
					<< " signalIntervalMs=" << frameLatencyGate.SignalIntervalEmaMs()
					<< " refreshMs=" << compositionRefreshMs << std::endl;
				summary = {};
				summary.startMs = nowMs;
			}
			return result;
		}

		void TripFrameLatency() noexcept
		{
			const std::uint32_t consecutiveTimeouts = frameLatencyGate.ConsecutiveTimeouts();
			frameLatencyGate.Trip();
			DiagnosticOutput::Line() << "[Draw3Diag][latency] waitable wait tripped reason="
				<< (frameLatencyGate.TripReason() == FrameLatencyTripReason::WaitFailed ? "wait-failed" :
					frameLatencyGate.TripReason() == FrameLatencyTripReason::QueueOverflow ? "queue-overflow" : "signal-lost")
				<< " consecutiveTimeouts=" << consecutiveTimeouts
				<< " totalTimeouts=" << frameLatencyGate.TotalTimeouts()
				<< " gpuBusyTimeoutsTotal=" << frameLatencyGpuBusyTimeoutsTotal
				<< " totalAcquired=" << frameLatencyGate.TotalAcquired()
				<< "; rebuilding swapchain without waitable flag." << std::endl;
			// 不在帧中途换链：走现有恢复路径重建同模式交换链，并由 CPU 文档重放画布。
			SetFailure(DXGI_ERROR_WAIT_TIMEOUT);
		}

		void CaptureDeviceInformation()
		{
			std::ostringstream text;
			if (!graphics || !graphics->device)
			{
				deviceInformation = "图形设备：不可用\n";
				return;
			}
			text << "图形设备：" << (graphics->driverType == D3D_DRIVER_TYPE_WARP ? "WARP" : "Hardware")
				<< "\nD3D Feature Level：" << FormatDiagnosticFeatureLevel(graphics->featureLevel) << "\n";
			DXGI_ADAPTER_DESC adapter{};
			const HRESULT descriptionResult = graphics->adapter ? graphics->adapter->GetDesc(&adapter) : E_NOINTERFACE;
			if (SUCCEEDED(descriptionResult))
			{
				char name[512]{};
				if (WideCharToMultiByte(CP_UTF8, 0, adapter.Description, -1, name, sizeof(name), nullptr, nullptr))
					text << "适配器：" << name << "\n";
				else text << "适配器名称：不可用\n";
				text << std::hex << std::uppercase << "VendorId=0x" << adapter.VendorId
					<< "  DeviceId=0x" << adapter.DeviceId << "  SubSysId=0x" << adapter.SubSysId
					<< "  Revision=0x" << adapter.Revision << std::dec << "\n"
					<< std::fixed << std::setprecision(1)
					<< "专用显存：" << adapter.DedicatedVideoMemory / (1024.0 * 1024.0) << " MiB"
					<< "  专用系统内存：" << adapter.DedicatedSystemMemory / (1024.0 * 1024.0) << " MiB"
					<< "  共享内存：" << adapter.SharedSystemMemory / (1024.0 * 1024.0) << " MiB\n";
			}
			else text << "适配器信息：不可用（HRESULT：" << FormatDiagnosticHResult(descriptionResult) << "）\n";
			LARGE_INTEGER version{};
			const HRESULT versionResult = graphics->adapter
				? graphics->adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version) : E_NOINTERFACE;
			if (SUCCEEDED(versionResult))
				text << "用户态图形驱动（UMD）版本：" << HIWORD(version.HighPart) << "." << LOWORD(version.HighPart)
					<< "." << HIWORD(version.LowPart) << "." << LOWORD(version.LowPart) << "\n";
			else text << "用户态图形驱动（UMD）版本：不可用（HRESULT：" << FormatDiagnosticHResult(versionResult) << "）\n";
			deviceInformation = text.str();
		}

		static std::string FailureDetail(const char* stage, HRESULT result)
		{
			std::string detail = stage ? stage : "Presenter initialization";
			if (FAILED(result)) detail += " " + FormatDiagnosticHResult(result);
			return detail;
		}

		void FinishAttempt(TransparentPresentMode mode, bool success)
		{
			if (!success)
			{
				// 后续 ULW 失败不能覆盖引发降级的原始首选错误。
				if (options.preferredMode == mode || fallbackReport.lastAttemptFailure.empty())
					fallbackReport.lastAttemptFailure = FailureDetail(fallbackStage, fallbackResult);
				return;
			}
			fallbackStage = nullptr;
			fallbackResult = S_OK;
			fallbackReport.presentationWarning.clear();
			if (options.preferredMode && *options.preferredMode != mode)
			{
				fallbackReport.presentationWarning = std::string(TransparentPresentModeName(*options.preferredMode)) +
					" → " + TransparentPresentModeName(mode) + " (" + fallbackReport.lastAttemptFailure + ")";
			}
			else fallbackReport.lastAttemptFailure.clear();
		}

		void PublishInformation(bool refreshDevice, const char* state) noexcept
		{
			if (callbacks.fallbackChanged) callbacks.fallbackChanged(callbacks.informationContext, fallbackReport);
			if (!callbacks.informationChanged) return;
			try
			{
				// 设备枚举只在初始化/恢复发生；Resize 仅刷新真实交换链描述。
				if (refreshDevice) CaptureDeviceInformation();
				std::ostringstream text;
				text << "图形状态：" << state << "\n" << deviceInformation;
				DXGI_SWAP_CHAIN_DESC1 description{};
				const HRESULT result = swapChain ? swapChain->GetDesc1(&description) : E_NOINTERFACE;
				if (IsUlwMode(activeMode) && !swapChain && renderer && renderer->backBufferTexture)
				{
					// ULW 不建交换链：最终目标是独立纹理，经 Staging 读回到 DIB 交给 UpdateLayeredWindow。
					D3D11_TEXTURE2D_DESC finalDescription{};
					renderer->backBufferTexture->GetDesc(&finalDescription);
					text << "主画布呈现：UpdateLayeredWindow (ULW)\n辅助前置表面：由画布承担\n最终目标：独立纹理 "
						<< finalDescription.Width << "×" << finalDescription.Height << " BGRA8\n读回模式："
						<< (options.enableUlwAsyncReadback && !ulwAsyncFused ? "异步" : "同步")
						<< "\nDWM 合成：" << (dwmComposing ? "开启" : "关闭") << "  刷新间隔：" << std::fixed
						<< std::setprecision(2) << ulwRefreshMs << " ms（来源：" << ulwRefreshSource << "）\n";
				}
				else if (SUCCEEDED(result))
				{
					text << "主画布呈现：" << (IsDirectCompositionMode(activeMode) ? "DirectComposition" : "Win7Dwm")
						<< "\n辅助前置表面：UpdateLayeredWindow (ULW)\n交换链类型：" << (IsDirectCompositionMode(activeMode) ? "Composition" : "HWND")
						<< "\n" << FormatDiagnosticSwapChain(description, waitableSwapChain);
				}
				else text << "窗口呈现与交换链：不可用（HRESULT：" << FormatDiagnosticHResult(result) << "）\n";
				if (recoveryPending && FAILED(lastFailure)) text << "当前图形错误：HRESULT：" << FormatDiagnosticHResult(lastFailure) << "\n";
				callbacks.informationChanged(callbacks.informationContext, text.str());
			}
			catch (...) {} // 信息展示失败不能改变绘图和恢复结果。
		}
		UlwDirtyRectPresenter primaryUlwPresenter;
		UlwDirtyRectPresenter selectionUlwPresenter;
		DirectCompositionPresenter directCompositionPresenter;
		DwmBlurBehindPresenter dwmBlurPresenter;

		bool ResetPresenters()
		{
			primaryUlwPresenter.Reset();
			selectionUlwPresenter.Reset();
			const bool retired = directCompositionPresenter.Reset();
			dwmBlurPresenter.Reset();
			presentFailureLogged = false;
			presentSuccessLogged = false;
			lastLoggedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
			return retired;
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

		void RecordFallback(const char* stage, HRESULT result)
		{
			fallbackStage = stage;
			fallbackResult = result;
			LogHResult(stage, result);
		}

		void SetFailure(HRESULT result) noexcept
		{
			const bool firstFailure = !recoveryPending;
			lastFailure = DeviceFailureOr(result);
			recoveryPending = true;
			if (firstFailure)
			{
				try
				{
					if (options.preferredMode == activeMode || fallbackReport.lastAttemptFailure.empty())
						fallbackReport.lastAttemptFailure = FailureDetail(fallbackStage ? fallbackStage : "Presenter runtime", lastFailure);
				}
				catch (...) {} // 诊断分配失败不能影响设备恢复。
				PublishInformation(false, "等待恢复");
			}
		}

		bool ReleaseAttempt()
		{
			gpuIdleQuery.Reset(); // 查询属于旧设备，换设备后重新创建
			gpuIdleQueryIssued = false;
			const bool retired = ResetPresenters();
			const bool retireHwndChain = IsDwmBlurBehindMode(activeMode) && swapChain;
			if (renderer) renderer->ReleaseResources();
			CloseFrameLatencyHandle();
			swapChain.Reset();
			// D3D11 可延迟销毁交换链，切换 HWND 输出前先提交其退休。
			if (retireHwndChain && graphics && graphics->context) graphics->context->Flush();
			waitableSwapChain = false;
			return retired;
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

			// HWND 链使用 UNSPECIFIED；Draw3 实际像素仍为预乘 BGRA，由 Win7 玻璃客户区解释 alpha。
			LogSwapChainDescription(TransparentPresentModeName(mode), "CreateSwapChainForHwnd", description);
			result = graphics->factory->CreateSwapChainForHwnd(graphics->device.Get(), primaryWindow, &description, nullptr, nullptr,
				swapChain.ReleaseAndGetAddressOf());
			return SUCCEEDED(result) && swapChain ? S_OK : (FAILED(result) ? result : E_FAIL);
		}

		bool TryCreateWaitableSwapChain(TransparentPresentMode mode, const DXGI_SWAP_CHAIN_DESC1& baseDescription)
		{
			DXGI_SWAP_CHAIN_DESC1 waitableDescription = baseDescription;
			waitableDescription.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
			DiagnosticOutput::Line() << "Trying waitable swapchain in mode " << TransparentPresentModeName(mode) << "." << std::endl;
			HRESULT result = CreateSwapChainForMode(mode, waitableDescription);
			if (FAILED(result) || !swapChain)
			{
				LogHResult(SwapChainCreationStepName(mode, true), result);
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Waitable swapchain creation failed; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}

			Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
			swapChain2.Reset();
			result = swapChain.As(&swapChain2);
			if (FAILED(result) || !swapChain2)
			{
				LogHResult("IDXGISwapChain1::QueryInterface(IDXGISwapChain2)", result);
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Waitable swapchain is not usable; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}

			result = swapChain2->SetMaximumFrameLatency(1);
			if (FAILED(result))
			{
				LogHResult("IDXGISwapChain2::SetMaximumFrameLatency(1)", result);
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Waitable frame latency setup failed; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}

			// 调用方保证只在 DComp 且已启用等待时进入：取不到句柄就无法执行上限，视为 waitable 不可用，回退普通交换链（设备级上限）。
			const HANDLE waitHandle = swapChain2->GetFrameLatencyWaitableObject();
			if (!waitHandle)
			{
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Waitable frame latency handle is unavailable; fallback to ordinary swapchain." << std::endl;
				swapChain.Reset();
				return false;
			}
			frameLatencyWaitHandle = waitHandle;
			frameLatencyGate.Enable();
			compositionRefreshMs = QueryCompositionRefreshMilliseconds();
			DiagnosticOutput::Line() << "[Draw3Diag][latency] waitable wait enabled refreshMs=" << compositionRefreshMs
				<< " timeoutMs=" << frameLatencyGate.TimeoutMs(frameLatencyTargetMs, compositionRefreshMs) << std::endl;

			waitableSwapChain = true;
			DiagnosticOutput::Line(DiagnosticOutput::OutInfo) << "Waitable swapchain enabled in mode " << TransparentPresentModeName(mode) << "." << std::endl;
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
			description.Scaling = DXGI_SCALING_STRETCH;
			description.AlphaMode = IsDirectCompositionMode(mode) ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_UNSPECIFIED;
			// 交换链类型由呈现方式硬性决定，没有用户偏好：DComp 只支持 FLIP；Win7 Aero 下 FLIP 链能创建且 Present 成功，
			// 但 DWM 模糊合成读不到其内容而黑屏，Win7Dwm 一律使用 SEQUENTIAL 单缓冲。
			const bool sequential = IsDwmBlurBehindMode(mode);
			description.SwapEffect = sequential ? DXGI_SWAP_EFFECT_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			description.BufferCount = sequential ? 1 : 2;

			// 仅 DComp 且启用等待时尝试帧延迟句柄；失败时退回普通链。
			// 关闭等待时不能保留 waitable 标志：该标志下 Present 不再受设备级帧延迟上限约束，不等待句柄提交会无界堆积
			// （实测可见延迟 400→1840 ms 随笔画增长）；普通 FLIP 链由 SetMaximumFrameLatency(1) 在 Present 内限流。
			HRESULT result = S_OK;
			if (IsDirectCompositionMode(mode) && !frameLatencyGate.Tripped() && !options.enableWaitableWait)
				DiagnosticOutput::Line() << "[Draw3Diag][latency] waitable disabled by setting; using ordinary swapchain." << std::endl;
			if (!IsDirectCompositionMode(mode) || frameLatencyGate.Tripped() || !options.enableWaitableWait ||
				!TryCreateWaitableSwapChain(mode, description))
				result = CreateSwapChainForMode(mode, description);
			if (FAILED(result) || !swapChain)
			{
				RecordFallback(SwapChainCreationStepName(mode, false), result);
				return false;
			}
			DXGI_SWAP_CHAIN_DESC1 actual{};
			const HRESULT descResult = swapChain->GetDesc1(&actual);
			if (FAILED(descResult) || actual.SwapEffect != description.SwapEffect)
			{
				RecordFallback("GetDesc1", FAILED(descResult) ? descResult : E_UNEXPECTED);
				return false;
			}
			LogSwapChainDescription(TransparentPresentModeName(mode), "Created", actual);
			return true;
		}

		bool UpdateWin7Dwm()
		{
			const char* stage = "Win7Dwm HWND";
			const HRESULT result = dwmBlurPresenter.Update(stage);
			if (FAILED(result)) RecordFallback(stage, result);
			return SUCCEEDED(result);
		}

		bool InitializePresenter()
		{
			primaryUlwPresenter.diagnosticRole = "Drawpad";
			primaryUlwPresenter.applyPrimaryHitUnderlay = true;
			selectionUlwPresenter.diagnosticRole = "DrawpadPresentation";
			selectionUlwPresenter.applyPrimaryHitUnderlay = false;
			bool primaryInitialized = false;
			switch (activeMode)
			{
			case TransparentPresentMode::UlwDirtyRect:
				primaryInitialized = primaryUlwPresenter.Initialize(primaryWindow,
					graphics->device.Get(), graphics->context.Get(), width, height); break;
			case TransparentPresentMode::DirectCompositionVisualTree:
				fallbackResult = directCompositionPresenter.Initialize(primaryWindow,
					graphics->dxgiDevice.Get(), swapChain.Get(), fallbackStage);
				primaryInitialized = SUCCEEDED(fallbackResult);
				if (!primaryInitialized) LogHResult(fallbackStage, fallbackResult);
				break;
			case TransparentPresentMode::DwmBlurBehind:
				dwmBlurPresenter.window = primaryWindow;
				primaryInitialized = UpdateWin7Dwm(); break;
			default:
				return false;
			}
			if (!primaryInitialized) return false;
			// ULW 主窗自身承担选择态，辅助前置窗不建 presenter（HWND 仍在窗口链中，保持隐藏）。
			if (!PresentModeUsesAuxiliarySelection(activeMode) && !options.initializeAuxiliaryForUlw) return true;
			// 辅助窗保留固定样式/设备借用，像素在真正需要辅助 Present 时才建立。
			if (selectionUlwPresenter.Initialize(selectionWindow,
				graphics->device.Get(), graphics->context.Get(), width, height, false)) return true;
			fallbackStage = "DrawpadPresentation ULW initialization";
			fallbackResult = DeviceFailureOr(S_OK); // 初始化只返回 bool；没有设备错误时只显示失败步骤。
			return false;
		}

		bool TryInitialize(TransparentPresentMode mode,
			Shutdown::FailedCleanupSignal releaseSignal = {},
			Shutdown::FailedCleanupSignal terminalOuterSignal = {})
		{
			fallbackStage = nullptr;
			fallbackResult = S_OK;
			const auto beginFailure = [&]() noexcept
			{
				const ULONGLONG deadline = releaseSignal.BeginKnownFailure();
				terminalOuterSignal.BeginKnownFailure(deadline);
			};
			if (!ReleaseAttempt())
			{
				beginFailure();
				return false; // 旧 DComp 未退休前禁止任何后续模式初始化。
			}
			// 能力检查在改动窗口样式之前完成，Basic 和不透明着色直接走 ULW。
			if (IsDwmBlurBehindMode(mode))
			{
				const char* stage = nullptr;
				const HRESULT hr = CheckWin7Dwm(stage);
				if (FAILED(hr))
				{
					beginFailure();
					RecordFallback(stage, hr);
					return false;
				}
			}
			else if (!IsDirectCompositionMode(mode) && !IsUlwMode(mode))
			{
				beginFailure();
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Transparent present mode " << TransparentPresentModeName(mode)
					<< " is disabled." << std::endl;
				return false;
			}
			// Win7 无 DComp 时直接尝试 ULW，避免先改动主 HWND 的创建期样式合同。
			if (IsDirectCompositionMode(mode))
			{
				const char* stage = nullptr;
				const HRESULT hr = CheckDirectCompositionApi(stage);
				if (FAILED(hr))
				{
					beginFailure();
					RecordFallback(stage, hr);
					return false;
				}
			}
			activeMode = mode;
			DiagnosticOutput::Line() << "Trying transparent present mode: " << TransparentPresentModeName(mode) << std::endl;
			if (!ConfigureWindow(mode))
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				RecordFallback("ConfigureWindow", initializeError ? HRESULT_FROM_WIN32(initializeError) : E_FAIL);
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "ConfigureWindow failed in mode " << TransparentPresentModeName(mode)
					<< " exStyle=0x" << std::hex
					<< static_cast<unsigned long>(GetWindowLongPtrW(primaryWindow, GWL_EXSTYLE))
					<< std::dec << " lastError=" << initializeError << std::endl;
				return false;
			}
			// ULW 不建交换链（design §4）：Draw3 直接画到独立纹理，renderer 走离屏分支；ULW 从不 Present，交换链只会闲置一张缓冲。
			if (!IsUlwMode(mode) && !CreateSwapChain(mode))
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "CreateSwapChain failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << initializeError << std::endl;
				return false;
			}
			// DComp 输出实际 swapchain 描述，包含普通回退的 flags/effect/size。
			if (swapChain)
				LogSwapChainRuntimeDescription(TransparentPresentModeName(mode),
					swapChain.Get(), "Draw3 created");
			else
				DiagnosticOutput::Line() << "Draw3 created " << TransparentPresentModeName(mode)
					<< " without swapchain: final target is an offscreen texture " << width << "x" << height << "." << std::endl;

			// 传递实际创建后端，不用适配器名称或 vendor 猜测 WARP。
			if (!renderer->Init(graphics->device.Get(), graphics->context.Get(), swapChain.Get(), width, height,
				graphics->driverType, options.enableZhaoxinCompatibility))
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				fallbackStage = "InkRenderer initialization";
				fallbackResult = DeviceFailureOr(S_OK);
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Renderer initialization failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << initializeError << std::endl;
				return false;
			}
			if (!InitializePresenter())
			{
				const DWORD initializeError = GetLastError();
				beginFailure();
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Presenter initialization failed in mode " << TransparentPresentModeName(mode)
					<< " lastError=" << initializeError << std::endl;
				return false;
			}
			DiagnosticOutput::Line(DiagnosticOutput::OutInfo) << "Active transparent present mode: " << TransparentPresentModeName(mode)
				<< " ulwAlignSetting=" << options.enableUlwCompositionAlign << " dcompAlignSetting=" << options.enableDcompCompositionAlign << " win7DwmAlignSetting=" << options.enableWin7DwmCompositionAlign << std::endl;
			RefreshUlwCompositionState("initialize");
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
			// 帧循环外的呈现（清屏、整帧等）没有令牌时先补等一次，不绕过排队上限；超时照常 Present。
			if (frameLatencyWaitHandle && frameLatencyGate.NeedsWaitBeforePresent())
			{
				const bool gpuIdle = PreviousFrameGpuIdle();
				const FrameLatencyWaitResult waited = WaitFrameLatency(FrameLatencyTimeoutPolicy::Neutral);
				presentLatencyWait.waitedMs += waited.waitedMs;
				if (gpuIdle) presentLatencyWait.pacingWaitMs += waited.waitedMs;
				if (waited.outcome == FrameLatencyWaitOutcome::Signaled)
					presentLatencyWait.state = FrameLatencyWaitState::Signaled;
				else if (waited.outcome == FrameLatencyWaitOutcome::TimedOut)
				{
					presentLatencyWait.state = FrameLatencyWaitState::TimedOut;
					presentLatencyWait.timedOut = true;
				}
			}
			const HRESULT result = swapChain->Present1(0, 0, &parameters);
			// 只有实际调用且成功的 Present1 才消耗令牌；上面的空矩形早退不算。
			frameLatencyGate.OnPresentCalled(SUCCEEDED(result));
			if (SUCCEEDED(result))
			{
				++dcompPresents;
				lastDcompPresentMs = QpcMilliseconds();
				if (IsDirectCompositionMode(activeMode)) IssueGpuIdleQuery();
			}
			return result;
		}

		// Present1 之后插入事件查询：下一次 waitable 等待前查询是否已完成，区分“等 GPU”与“等显示节拍”。
		// 创建失败时不插入，之后的等待按显示节拍处理（与引入查询前的口径一致）。
		void IssueGpuIdleQuery() noexcept
		{
			if (!graphics || !graphics->device || !graphics->context) return;
			if (!gpuIdleQuery)
			{
				const D3D11_QUERY_DESC description = { D3D11_QUERY_EVENT, 0 };
				if (FAILED(graphics->device->CreateQuery(&description, gpuIdleQuery.GetAddressOf()))) return;
			}
			graphics->context->End(gpuIdleQuery.Get());
			gpuIdleQueryIssued = true;
		}

		// 不等待、不 Flush：上一次 Present1 之前提交的 GPU 工作是否已全部完成。
		// 只有 S_FALSE（查询尚未完成）算“忙”；设备移除等错误按空闲处理，避免熔断判据被永久视为 GPU 忙而失效。
		// allowFlush 仅用于超时后的熔断判定（低频）：允许 GetData 提交仍在命令缓冲中的查询；每帧的节奏统计保持不提交。
		bool PreviousFrameGpuIdle(bool allowFlush = false) noexcept
		{
			if (!gpuIdleQueryIssued || !gpuIdleQuery || !graphics || !graphics->context) return true;
			return graphics->context->GetData(gpuIdleQuery.Get(), nullptr, 0,
				allowFlush ? 0u : static_cast<UINT>(D3D11_ASYNC_GETDATA_DONOTFLUSH)) != S_FALSE;
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
		impl_->frameLatencyGate = {}; // 熔断只在本会话有效。
		impl_->frameLatencyGpuBusyTimeoutsTotal = 0;
		impl_->frameLatencyTargetMs = 1000.0 / 120.0;
		impl_->ulwAsyncFused = false;
		impl_->ulwAlignReason = nullptr;
		impl_->ulwTimingAligned = false;
		impl_->ulwFrameStartMs = 0.0;
		impl_->primaryWindow = nullptr;
		impl_->selectionWindow = nullptr;
		impl_->graphics = nullptr;
		impl_->renderer = nullptr;
		impl_->callbacks = {};
		impl_->width = 0;
		impl_->height = 0;
		impl_->options = {};
		impl_->deviceInformation.clear();
		impl_->fallbackStage = nullptr;
		impl_->fallbackResult = S_OK;
		impl_->fallbackReport = {};
		impl_->recoveredUlwTarget.reset();
		impl_->automaticWin7DwmEntered = false;
		impl_->requestedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		impl_->requestedOutputRevision = 0;
		impl_->dcompHandoffBarrier = {};
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
		impl_->fallbackStage = nullptr;
		impl_->fallbackResult = S_OK;
		impl_->recoveredUlwTarget.reset();
		impl_->automaticWin7DwmEntered = false;
		impl_->fallbackReport = options.previousAttempt;
		if (impl_->fallbackReport.lastAttemptFailure.empty())
		{
			if (options.requireMode && IsUlwMode(options.requiredMode) && options.preferredMode && !IsUlwMode(*options.preferredMode))
				impl_->fallbackReport.lastAttemptFailure = "Host startup / HWND rebuild";
			else if (options.preferredMode == TransparentPresentMode::DirectCompositionVisualTree && !options.allowDirectComposition)
			{
				const char* stage = nullptr;
				const HRESULT hr = CheckDirectCompositionApi(stage);
				impl_->fallbackReport.lastAttemptFailure = FAILED(hr) ? Impl::FailureDetail(stage, hr) : "DComp disabled for this HWND";
			}
		}
		impl_->recoveryPending = false;
		impl_->lastFailure = S_OK;
		impl_->requestedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		impl_->requestedOutputRevision = 0;
		impl_->dcompHandoffBarrier = {};
		impl_->lastObservation = {};
		impl_->presentFailureLogged = false;
		impl_->presentSuccessLogged = false;
		impl_->lastLoggedOutputTarget = TransparentOutputTarget::PrimaryDrawpad;
		if (!primaryWindow || !selectionWindow || !IsWindow(primaryWindow) ||
			!IsWindow(selectionWindow))
		{
			options.failedCleanup.BeginKnownFailure();
			impl_->PublishInformation(true, "初始化失败（窗口不可用）");
			return false;
		}
		const auto tryStartupMode = [&](TransparentPresentMode mode, bool terminal, TransparentPresentMode next)
		{
			Shutdown::FailedCleanupDeadline releaseCleanup(options.failedCleanup.Publisher());
			if (options.failedCleanup.HasPublisher()) releaseCleanup.PrepareOrFatal();
			const auto releaseSignal = releaseCleanup.Signal();
			const auto outerSignal = terminal ? options.failedCleanup : Shutdown::FailedCleanupSignal{};
			bool initialized = false;
			try
			{
				initialized = impl_->TryInitialize(mode, releaseSignal, outerSignal);
				impl_->FinishAttempt(mode, initialized);
				if (!initialized)
				{
					outerSignal.BeginKnownFailure(releaseSignal.BeginKnownFailure());
					if (!impl_->ReleaseAttempt()) return false;
					if (!terminal)
					{
						DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Transparent present mode " << TransparentPresentModeName(mode)
							<< " failed; fallback to " << TransparentPresentModeName(next) << "." << std::endl;
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
			if (initialized || terminal) impl_->PublishInformation(true, initialized ? "就绪" : "初始化失败");
			return initialized;
		};
		if (options.requireMode)
			return tryStartupMode(options.requiredMode, true, options.requiredMode);
		// 每个候选失败后都先由 tryStartupMode ReleaseAttempt，下一候选的 TryInitialize 开头再释放一次；
		// 释放未完成（DComp 退休超时）时后续候选被拒绝，失败截止沿用原有的 terminal 继承语义（与改动前一致）。
		const auto chain = BuildProductModeChain(options.preferredMode, options.allowDirectComposition, IsBelowWindows8());
		for (size_t index = 0; index < chain.count; ++index)
		{
			const bool terminal = index + 1 == chain.count;
			const auto mode = chain.modes[index];
			if (tryStartupMode(mode, terminal, terminal ? mode : chain.modes[index + 1]))
			{
				impl_->automaticWin7DwmEntered = !options.preferredMode && IsDwmBlurBehindMode(mode);
				return true;
			}
		}
		options.failedCleanup.BeginKnownFailure();
		DiagnosticOutput::Line(DiagnosticOutput::OutError) << "All transparent present modes failed." << std::endl;
		return false;
	}

	std::uint64_t TransparentPresentationController::SetOutputTarget(
		TransparentOutputTarget target) noexcept
	{
		if (impl_->requestedOutputTarget == target)
			return impl_->requestedOutputRevision;
		if (target != TransparentOutputTarget::PrimaryDrawpad && IsUlwMode(impl_->activeMode) &&
			!impl_->recoveryPending && impl_->renderer)
		{
			// 切到辅助输出前先完成主画布的待完成读回并拷出累计脏区，主窗隐藏前停在最新画面。
			// 失败时已安排恢复，帧循环随后在恢复检查中处理；待恢复时不 Map（设备可能已移除）。
			double waitedMs = 0.0;
			if (!impl_->primaryUlwPresenter.CompleteBeforeIdle(
				impl_->renderer->backBufferTexture.Get(), QpcMilliseconds(), waitedMs))
				impl_->SetFailure(E_FAIL);
			impl_->SyncUlwFuse();
		}
		impl_->requestedOutputTarget = target;
		if (++impl_->requestedOutputRevision == 0)
			++impl_->requestedOutputRevision;
		if (target == TransparentOutputTarget::PrimaryDrawpad)
			impl_->dcompHandoffBarrier.Arm(impl_->requestedOutputRevision);
		else
			impl_->dcompHandoffBarrier.Cancel();
		if (StartupEnvironmentDiagnosticsEnabled())
		{
			DiagnosticOutput::Line(DiagnosticOutput::Target{ DiagnosticOutput::Stream::Output, Inkeys::Log::Channel::Draw3 }) << "[Draw3Diag][target] requested="
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
		// 手选 Win7Dwm，或自动在本代启动时选中 Win7Dwm：运行期一律只恢复到 ULW 并受无进展保护。
		const bool requestedWin7Dwm = impl_->options.preferredMode == TransparentPresentMode::DwmBlurBehind ||
			impl_->automaticWin7DwmEntered;
		auto& output = impl_->requestedOutputTarget == TransparentOutputTarget::SelectionUlw ?
			impl_->selectionUlwPresenter : impl_->primaryUlwPresenter;
		if (requestedWin7Dwm && IsUlwMode(failedMode) &&
			impl_->recoveredUlwTarget == impl_->requestedOutputTarget && impl_->recoveredUlwUpdates == output.completedUpdates)
		{
			impl_->PublishInformation(true, "ULW 恢复后仍未成功提交");
			return false;
		}
		auto observeUlwRecovery = [&]()
		{
			if (requestedWin7Dwm && IsUlwMode(impl_->activeMode))
			{
				impl_->recoveredUlwTarget = impl_->requestedOutputTarget;
				impl_->recoveredUlwUpdates = output.completedUpdates;
			}
		};
		impl_->recoveryPending = false;
		if (!impl_->ReleaseAttempt())
		{
			impl_->lastFailure = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
			return false;
		}
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
			if (!InitializeGraphicsDevice(*impl_->graphics, impl_->options.enableHardwareAcceleration,
				impl_->options.allowWarpFallback))
			{
				impl_->lastFailure = failure;
				impl_->PublishInformation(true, "设备恢复失败");
				return false;
			}
		}

		if (impl_->options.requireMode)
		{
			// 强制后端也先完整重建一次；辅助 ULW 运行期失败不能退回主窗穿透。
			const bool initialized = impl_->TryInitialize(impl_->options.requiredMode);
			impl_->FinishAttempt(impl_->options.requiredMode, initialized);
			if (initialized)
			{
				observeUlwRecovery();
				impl_->lastFailure = S_OK;
				impl_->PublishInformation(true, "恢复完成");
				return true;
			}
			impl_->ReleaseAttempt();
			impl_->lastFailure = failure;
			impl_->PublishInformation(true, "呈现恢复失败");
			return false;
		}

		// Win7Dwm（手选或自动选中）遇到运行期故障后，本代只恢复到外部 ULW 位图；防止设备重建后重复进入坏链。
		// 自动选路即使未进入过 Win7Dwm，FirstProductMode 也只会给出 DComp/ULW，运行期不会首次进入 Win7Dwm。
		auto first = deviceLost ? FirstProductMode(impl_->options.preferredMode, impl_->options.allowDirectComposition) : failedMode;
		if (requestedWin7Dwm)
		{
			first = TransparentPresentMode::UlwDirtyRect;
			if (IsDwmBlurBehindMode(failedMode) && impl_->fallbackResult != failure) impl_->RecordFallback("Win7Dwm runtime -> ULW", failure);
		}
		const TransparentPresentMode candidates[] = { first, TransparentPresentMode::UlwDirtyRect };
		const size_t count = IsUlwMode(first) ? 1 : ARRAYSIZE(candidates);
		for (size_t index = 0; index < count; ++index)
		{
			const bool initialized = impl_->TryInitialize(candidates[index]);
			impl_->FinishAttempt(candidates[index], initialized);
			if (initialized)
			{
				observeUlwRecovery();
				impl_->lastFailure = S_OK;
				impl_->PublishInformation(true, "恢复完成");
				return true;
			}
			impl_->ReleaseAttempt();
		}
		impl_->lastFailure = failure;
		impl_->PublishInformation(true, "呈现恢复失败");
		return false;
	}

	bool TransparentPresentationController::Resize(UINT width, UINT height)
	{
		const bool sizeChanged = impl_->width != width || impl_->height != height;
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
			succeeded = impl_->UpdateWin7Dwm(); break;
		default: break;
		}
		if (succeeded && sizeChanged)
		{
			// 辅助完整帧会按真实纹理/窗口尺寸重建，Resize 不把闲置大缓冲建回来。
			impl_->selectionUlwPresenter.ReleaseReadbackResources();
			impl_->selectionUlwPresenter.committedFullFrameClean = false;
			// 窗口可能移到另一台显示器：重新检测刷新间隔，合成时钟下一次立即重读。
			impl_->directCompositionPresenter.phaseClock.Invalidate();
			impl_->RefreshUlwCompositionState("resize");
		}
		if (!succeeded) impl_->SetFailure(IsDwmBlurBehindMode(impl_->activeMode) ? impl_->fallbackResult : E_FAIL);
		else impl_->PublishInformation(false, "就绪");
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
			UlwDirtyRectPresenter& presenter = impl_->primaryUlwPresenter;
			// 熔断后残留的待完成提交/累计脏区仍经异步入口收尾（内部改走同步），不能被同步路径跳过。
			if (impl_->UlwAsyncActive() || presenter.pending.active || presenter.hasAccumulated)
			{
				// 异步读回：Present 内的背压与即时完成的阻塞 Map 经帧外补等通道计入等待（design §5.7）。
				const UlwFrameTiming timing = impl_->MakeUlwTiming(QpcMilliseconds());
				double waitedMs = 0.0;
				succeeded = presenter.PresentAsync(impl_->renderer->backBufferTexture.Get(), dirty, presentFull, timing, waitedMs);
				impl_->presentLatencyWait.waitedMs += waitedMs;
				impl_->SyncUlwFuse();
				presenter.MaybeLogPipelineSummary(timing.nowMs, timing.refreshMs, timing.aligned);
			}
			else succeeded = presenter.Present(
				impl_->renderer->backBufferTexture.Get(), dirty, presentFull); // ULW 需要 CPU 读回并调用 UpdateLayeredWindow。
			impl_->lastObservation = presenter.lastObservation;
			if (succeeded) impl_->dcompHandoffBarrier.Cancel();
			else impl_->SetFailure(E_FAIL);
		}
		else
		{
			const char* submitStage = "IDXGISwapChain1::Present1";
			HRESULT result = impl_->PresentSwapChain(dirty, presentFull); // GPU 路径直接 Present1，DWM 读取 alpha。
			if (impl_->dcompHandoffBarrier.Consume(
				IsGpuMode(impl_->activeMode), presentFull,
				SUCCEEDED(result), impl_->requestedOutputRevision))
			{
				// 仅输出交接的全帧等待 DWM 上屏；普通帧和 ULW 不进入此屏障。
				submitStage = "DwmFlush";
				result = DwmFlush();
				if (FAILED(result))
				{
					// 恢复后的同 revision 全帧仍须建立屏障，不能把失败帧当作已上屏。
					impl_->dcompHandoffBarrier.Arm(impl_->requestedOutputRevision);
					DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "[Draw3Diag][handoff] DwmFlush failed"
						<< " target=Drawpad"
						<< " revision=" << impl_->requestedOutputRevision
						<< " contentRevision=" << contentRevision
						<< " result=0x" << std::hex << static_cast<unsigned long>(result)
						<< std::dec << std::endl;
				}
			}
			succeeded = SUCCEEDED(result);
			impl_->lastObservation = {};
			if (!succeeded || impl_->presentFailureLogged || !impl_->presentSuccessLogged ||
				impl_->lastLoggedOutputTarget != target)
			{
				if (!succeeded || StartupEnvironmentDiagnosticsEnabled())
				{
					// 失败始终记录（Error）；成功只在 Draw3 调试开关开启时写入 Draw3 通道。
					DiagnosticOutput::Line(succeeded ? DiagnosticOutput::Target{ DiagnosticOutput::Stream::Output, Inkeys::Log::Channel::Draw3 }
						: DiagnosticOutput::OutError) << "[Draw3Diag][present] mode="
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
			if (!succeeded)
			{
				impl_->RecordFallback(submitStage, result);
				impl_->SetFailure(result);
			}
		}
		impl_->lastObservation.outputTarget = target;
		impl_->lastObservation.outputRevision = impl_->requestedOutputRevision;
		impl_->lastObservation.presentedContentRevision = contentRevision;
		if (!succeeded && !impl_->presentFailureLogged)
		{
			DiagnosticOutput::Line(DiagnosticOutput::OutError) << "Present failed in mode " << TransparentPresentModeName(impl_->activeMode) << std::endl;
			impl_->presentFailureLogged = true;
		}
		if (succeeded)
		{
			impl_->presentFailureLogged = false;
			impl_->presentSuccessLogged = true;
		}
		return succeeded;
	}

	std::uint64_t TransparentPresentationController::AuxiliaryReadbackPayloadBytes() const noexcept
	{
		return impl_->selectionUlwPresenter.ReadbackPayloadBytes();
	}

	bool TransparentPresentationController::AuxiliaryReadbackNeedsFullPresent() const noexcept
	{
		return impl_->requestedOutputTarget == TransparentOutputTarget::SelectionUlw &&
			impl_->selectionUlwPresenter.needsFullReadback;
	}

	bool TransparentPresentationController::ReleaseAuxiliaryReadbackResources(
		TransparentOutputTarget target, std::uint64_t outputRevision,
		std::uint64_t contentRevision) noexcept
	{
		// 调用者已经取得 Window Service 隐藏回执；这里仅验证 owner 上的真实呈现版本。
		if (impl_->recoveryPending || !impl_->presentSuccessLogged || impl_->presentFailureLogged ||
			impl_->requestedOutputTarget != target || impl_->requestedOutputRevision != outputRevision ||
			impl_->lastObservation.outputTarget != target ||
			impl_->lastObservation.outputRevision != outputRevision ||
			impl_->lastObservation.presentedContentRevision != contentRevision ||
			(target == TransparentOutputTarget::SelectionUlw &&
				!impl_->selectionUlwPresenter.committedFullFrameClean)) return false;
		impl_->selectionUlwPresenter.ReleaseReadbackResources();
		return true;
	}

	FrameLatencyWaitReport TransparentPresentationController::PrepareFrameLatency(
		double targetFrameMs, bool countsTowardTrip)
	{
		FrameLatencyWaitReport report;
		if (!impl_) return report;
		if (std::isfinite(targetFrameMs) && targetFrameMs > 0.0) impl_->frameLatencyTargetMs = targetFrameMs;
		// 仅 DComp waitable 交换链输出到主画布时等待；ULW、辅助输出和待恢复状态立即返回。
		if (!impl_->frameLatencyGate.Enabled() || !impl_->frameLatencyWaitHandle || impl_->recoveryPending ||
			impl_->requestedOutputTarget != TransparentOutputTarget::PrimaryDrawpad) return report;
		if (impl_->frameLatencyGate.TokenHeld())
		{
			report.state = FrameLatencyWaitState::AlreadyHeld;
			return report;
		}
		// 窗口隐藏/最小化或输出交接屏障未完成时句柄可能长期不发信号：超时只计统计，不参与熔断。
		const bool countable = countsTowardTrip && impl_->dcompHandoffBarrier.pendingOutputRevision == 0 &&
			IsWindowVisible(impl_->primaryWindow) && !IsIconic(impl_->primaryWindow);
		if (!countable) impl_->frameLatencyGpuBusySinceMs = 0.0; // 5 秒忙计时不跨越排除区间
		const bool gpuIdle = impl_->PreviousFrameGpuIdle();
		const FrameLatencyWaitResult result = impl_->WaitFrameLatency(
			countable ? FrameLatencyTimeoutPolicy::Count : FrameLatencyTimeoutPolicy::Exclude, countable);
		report.waitedMs = result.waitedMs;
		if (gpuIdle) report.pacingWaitMs = result.waitedMs;
		report.timedOut = result.outcome == FrameLatencyWaitOutcome::TimedOut;
		report.state = result.outcome == FrameLatencyWaitOutcome::Signaled
			? FrameLatencyWaitState::Signaled : FrameLatencyWaitState::TimedOut;
		if (impl_->frameLatencyGate.ShouldTrip())
		{
			impl_->TripFrameLatency();
			report.state = FrameLatencyWaitState::Tripped;
		}
		return report;
	}

	FrameLatencyWaitReport TransparentPresentationController::ConsumePresentLatencyWait() noexcept
	{
		if (!impl_) return {};
		const FrameLatencyWaitReport report = impl_->presentLatencyWait;
		impl_->presentLatencyWait = {};
		return report;
	}

	bool TransparentPresentationController::RefreshAfterCompositionChanged()
	{
		bool succeeded = true;
		if (IsDwmBlurBehindMode(impl_->activeMode)) succeeded = impl_->UpdateWin7Dwm();
		impl_->directCompositionPresenter.phaseClock.Invalidate();
		impl_->RefreshUlwCompositionState("composition-changed"); // WM_DWMCOMPOSITIONCHANGED / DPI 变化
		if (!succeeded) impl_->SetFailure(IsDwmBlurBehindMode(impl_->activeMode) ? impl_->fallbackResult : E_FAIL);
		return succeeded;
	}

	UlwFrameReport TransparentPresentationController::BeginUlwFrame(double frameStartMs, double targetFrameMs)
	{
		UlwFrameReport report;
		if (!impl_) return report;
		if (std::isfinite(targetFrameMs) && targetFrameMs > 0.0) impl_->ulwFrameIntervalMs = targetFrameMs;
		impl_->ulwFrameStartMs = frameStartMs;
		UlwDirtyRectPresenter& presenter = impl_->primaryUlwPresenter;
		if (!IsUlwMode(impl_->activeMode) || impl_->recoveryPending || !impl_->renderer ||
			impl_->requestedOutputTarget != TransparentOutputTarget::PrimaryDrawpad ||
			(!presenter.pending.active && !presenter.hasAccumulated)) return report;
		const UlwFrameTiming timing = impl_->MakeUlwTiming(QpcMilliseconds());
		report.failed = !presenter.ServiceFrameStart(impl_->renderer->backBufferTexture.Get(), timing, report.waitedMs);
		impl_->SyncUlwFuse();
		if (report.failed) impl_->SetFailure(E_FAIL);
		presenter.MaybeLogPipelineSummary(timing.nowMs, timing.refreshMs, timing.aligned);
		return report;
	}

	void TransparentPresentationController::DiscardUlwAccumulated() noexcept
	{
		if (!impl_) return;
		UlwDirtyRectPresenter& presenter = impl_->primaryUlwPresenter;
		if (!presenter.hasAccumulated) return;
		// 不 Map、不拷贝：最终纹理可能已被失败帧部分覆写。已冻结在槽里的提交仍有效，照常完成。
		presenter.ClearAccumulated();
		presenter.needsFullReadback = true;
	}

	UlwFrameReport TransparentPresentationController::ServiceUlwBeforeFrameWait()
	{
		UlwFrameReport report;
		if (!impl_) return report;
		UlwDirtyRectPresenter& presenter = impl_->primaryUlwPresenter;
		if (!IsUlwMode(impl_->activeMode) || impl_->recoveryPending || !impl_->renderer ||
			impl_->requestedOutputTarget != TransparentOutputTarget::PrimaryDrawpad || !presenter.hasAccumulated) return report;
		const UlwFrameTiming timing = impl_->MakeUlwTiming(QpcMilliseconds());
		report.failed = !presenter.ServiceFrameEnd(impl_->renderer->backBufferTexture.Get(), timing, report.waitedMs);
		impl_->SyncUlwFuse();
		if (report.failed) impl_->SetFailure(E_FAIL);
		return report;
	}

	UlwFrameReport TransparentPresentationController::CompleteUlwBeforeIdle(bool textureValid)
	{
		UlwFrameReport report;
		if (!impl_) return report;
		impl_->ulwFrameStartMs = 0.0; // 空闲期间的帧外 Present 不计入帧工作耗时
		UlwDirtyRectPresenter& presenter = impl_->primaryUlwPresenter;
		if (!IsUlwMode(impl_->activeMode) || impl_->recoveryPending || !impl_->renderer ||
			(!presenter.pending.active && !presenter.hasAccumulated)) return report;
		report.failed = !presenter.CompleteBeforeIdle(impl_->renderer->backBufferTexture.Get(),
			QpcMilliseconds(), report.waitedMs, textureValid);
		impl_->SyncUlwFuse();
		if (report.failed) impl_->SetFailure(E_FAIL);
		return report;
	}

	std::uint64_t TransparentPresentationController::CommittedPresentCount() const noexcept
	{
		if (!impl_) return 0;
		return impl_->dcompPresents + impl_->primaryUlwPresenter.completedUpdates + impl_->selectionUlwPresenter.completedUpdates;
	}

	double TransparentPresentationController::LastCommittedPresentMs() const noexcept
	{
		if (!impl_) return 0.0;
		return (std::max)({ impl_->lastDcompPresentMs, impl_->primaryUlwPresenter.lastUpdateMs,
			impl_->selectionUlwPresenter.lastUpdateMs });
	}

	CompositionTimingSample TransparentPresentationController::SampleDisplayTiming(double nowMs) noexcept
	{
		if (!impl_) return {};
		Impl& impl = *impl_;
		// 约每 250 ms 读一次 DWM 合成时序（与 CompositionClock 同频），不逐帧调用 DWM。
		if (impl.displayTimingSampleMs > 0.0 && nowMs >= impl.displayTimingSampleMs &&
			nowMs - impl.displayTimingSampleMs < kCompositionClockRereadMs)
			return impl.displayTiming;
		impl.displayTimingSampleMs = nowMs;
		impl.displayTiming = {};
		BOOL composing = FALSE;
		if (FAILED(DwmIsCompositionEnabled(&composing)) || !composing) return impl.displayTiming;
		const CompositionTimingSample sample = Impl::SampleCompositionTiming();
		if (sample.valid && std::isfinite(sample.vblankMs) && sample.periodMs >= kCompositionClockMinPeriodMs &&
			sample.periodMs <= kCompositionClockMaxPeriodMs)
			impl.displayTiming = sample;
		return impl.displayTiming;
	}

	double TransparentPresentationController::FramePhaseAdjustMs(double frameStartMs, double intervalMs, double workMs)
	{
		// 只调整固定帧截止等待的目标；打断式唤醒（Down/Up 等）仍立即返回，不等对齐。
		if (!impl_ || impl_->recoveryPending) return 0.0;
		// DComp 与 Win7dwm 都是“Present 写入、下一次 DWM 合成上屏”，用同一相位算法把帧完成贴近合成截止；
		// 两者只差时钟来源：DComp 读 GetFrameStatistics，Win7dwm 读 DwmGetCompositionTimingInfo（Win7 无 DComp）。
		const bool dcomp = IsDirectCompositionMode(impl_->activeMode);
		const bool win7Dwm = IsDwmBlurBehindMode(impl_->activeMode);
		if (dcomp || win7Dwm)
		{
			auto& dcompPresenter = impl_->directCompositionPresenter;
			auto& dwmPresenter = impl_->dwmBlurPresenter;
			const bool enabled = dcomp
				? impl_->options.enableDcompCompositionAlign && dcompPresenter.compositionDevice
				: impl_->options.enableWin7DwmCompositionAlign && dwmPresenter.window && impl_->swapChain;
			if (!enabled || impl_->requestedOutputTarget != TransparentOutputTarget::PrimaryDrawpad) return 0.0;
			auto& clock = dcomp ? dcompPresenter.phaseClock : dwmPresenter.phaseClock;
			if (clock.NeedsRefresh(frameStartMs))
			{
				CompositionTimingSample sample = {};
				HRESULT statsResult = S_OK;
				if (dcomp)
				{
					DCOMPOSITION_FRAME_STATISTICS stats = {};
					statsResult = dcompPresenter.compositionDevice->GetFrameStatistics(&stats);
					if (SUCCEEDED(statsResult))
						sample = DcompTimingSample(stats.nextEstimatedFrameTime.QuadPart, stats.currentTime.QuadPart,
							stats.timeFrequency.QuadPart, stats.currentCompositionRate.Numerator, stats.currentCompositionRate.Denominator);
				}
				else sample = Impl::SampleCompositionTiming();
				// 失败仅让时钟本轮失效，下一采样周期再试，与 waitable 的超时/熔断分开。
				clock.Update(sample, frameStartMs);
				const double shiftMs = DcompPhaseAdjustMs(frameStartMs + intervalMs, intervalMs, workMs, clock);
				// 仅在重读时序时记录样本，便于对照开关状态和实际修正量。
				DiagnosticOutput::Line() << (dcomp ? "[Draw3Diag][dcomp-align] valid=" : "[Draw3Diag][win7dwm-align] valid=")
					<< clock.Valid() << " hr=" << statsResult
					<< " periodMs=" << clock.PeriodMs() << " frameIntervalMs=" << intervalMs << " shiftMs=" << shiftMs << std::endl;
				// 等待接口接收相对时长，采样占用的帧预算必须扣除，即使本次统计无效。
				const double elapsedAfterWorkMs = (std::max)(0.0, QpcMilliseconds() - (frameStartMs + workMs));
				return shiftMs - elapsedAfterWorkMs;
			}
			return DcompPhaseAdjustMs(frameStartMs + intervalMs, intervalMs, workMs, clock);
		}
		if (!impl_->UlwAsyncActive() || !impl_->ulwTimingAligned) return 0.0;
		return impl_->primaryUlwPresenter.planner.PhaseAdjustMs(frameStartMs + impl_->ulwFrameIntervalMs,
			impl_->ulwFrameIntervalMs, impl_->compositionClock);
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

	TransparentPresentationEffectiveState TransparentPresentationController::EffectiveState() const noexcept
	{
		TransparentPresentationEffectiveState state;
		if (!impl_) return state;
		const Impl& impl = *impl_;
		state.mode = impl.activeMode;
		state.warpDevice = impl.graphics && impl.graphics->driverType == D3D_DRIVER_TYPE_WARP;
		state.waitableWait = IsDirectCompositionMode(impl.activeMode) &&
			impl.frameLatencyWaitHandle && impl.frameLatencyGate.Enabled();
		// 不用 UlwAsyncActive()：它随选择模式输出目标翻转，签名只描述主画布。
		state.ulwAsyncReadback = IsUlwMode(impl.activeMode) && impl.options.enableUlwAsyncReadback && !impl.ulwAsyncFused;
		state.ulwCompositionAlign = state.ulwAsyncReadback && impl.options.enableUlwCompositionAlign && impl.dwmComposing;
		state.dcompCompositionAlign = IsDirectCompositionMode(impl.activeMode) && impl.options.enableDcompCompositionAlign;
		state.win7DwmCompositionAlign = IsDwmBlurBehindMode(impl.activeMode) && impl.options.enableWin7DwmCompositionAlign;
		return state;
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
			constexpr BYTE inputAlphas[] = { 0, 127, 128, 254, 255 };
			constexpr BYTE expectedAlphas[] = { 1, 128, 128, 254, 255 };
			for (size_t index = 0; index < ARRAYSIZE(inputAlphas); ++index)
			{
				if (ComposePrimaryHitUnderlayAlpha(inputAlphas[index]) != expectedAlphas[index])
				{
					std::fputs("UlwDirtyCopyBenchmark hit-underlay alpha formula mismatch\n", stderr);
					return 1;
				}
			}

			// 验证首帧、dirty 绘制/擦除/再落笔和 DIB 重建；选择输出不合成命中底层。
			std::vector<BYTE> sequenceSource(2 * 4, 0);
			std::vector<BYTE> sequenceDib(2 * 4, 0);
			const auto copySequence = [](const std::vector<BYTE>& source, std::vector<BYTE>& dib,
				int width, RECT dirty, bool presentFull, bool applyPrimaryHitUnderlay)
			{
				return CopyAndInspectUlwDirtyRows(source.data(), static_cast<size_t>(width) * 4,
					dib.data(), width, 1, dirty, presentFull, applyPrimaryHitUnderlay, [] {});
			};
			const UlwDirtyCopyResult initialClear = copySequence(sequenceSource, sequenceDib,
				2, RECT{ 0, 0, 2, 1 }, true, true);
			if (!initialClear.allZeroAlpha || !initialClear.fullFrameAllZeroAlpha ||
				sequenceDib[3] != 1 || sequenceDib[7] != 1)
			{
				std::fputs("UlwDirtyCopyBenchmark primary full clear mismatch\n", stderr);
				return 1;
			}

			sequenceSource[0] = 32;
			sequenceSource[3] = 64;
			const UlwDirtyCopyResult firstStroke = copySequence(sequenceSource, sequenceDib,
				2, RECT{ 0, 0, 1, 1 }, false, true);
			if (firstStroke.allZeroAlpha || sequenceDib[0] != 32 || sequenceDib[3] != 65 ||
				sequenceDib[7] != 1)
			{
				std::fputs("UlwDirtyCopyBenchmark primary dirty stroke mismatch\n", stderr);
				return 1;
			}

			sequenceSource[0] = 0;
			sequenceSource[3] = 0;
			const UlwDirtyCopyResult erasedStroke = copySequence(sequenceSource, sequenceDib,
				2, RECT{ 0, 0, 1, 1 }, false, true);
			if (!erasedStroke.allZeroAlpha || sequenceDib[3] != 1)
			{
				std::fputs("UlwDirtyCopyBenchmark primary dirty erase mismatch\n", stderr);
				return 1;
			}

			sequenceSource[0] = 24;
			sequenceSource[3] = 127;
			const UlwDirtyCopyResult redrawnAfterErase = copySequence(sequenceSource, sequenceDib,
				2, RECT{ 0, 0, 1, 1 }, false, true);
			if (redrawnAfterErase.allZeroAlpha || sequenceDib[0] != 24 ||
				sequenceDib[3] != 128 || sequenceDib[7] != 1 ||
				sequenceSource[3] != 127)
			{
				std::fputs("UlwDirtyCopyBenchmark primary redraw after erase mismatch\n", stderr);
				return 1;
			}

			sequenceSource.assign(3 * 4, 0);
			sequenceDib.assign(3 * 4, 0); // 模拟 resize 后重建的 DIB。
			const UlwDirtyCopyResult resizedClear = copySequence(sequenceSource, sequenceDib,
				3, RECT{ 0, 0, 3, 1 }, true, true);
			if (!resizedClear.fullFrameAllZeroAlpha || sequenceDib[3] != 1 ||
				sequenceDib[7] != 1 || sequenceDib[11] != 1)
			{
				std::fputs("UlwDirtyCopyBenchmark resized primary clear mismatch\n", stderr);
				return 1;
			}

			sequenceSource[8] = 255;
			sequenceSource[11] = 255;
			copySequence(sequenceSource, sequenceDib, 3, RECT{ 2, 0, 3, 1 }, false, true);
			if (sequenceDib[8] != 255 || sequenceDib[11] != 255)
			{
				std::fputs("UlwDirtyCopyBenchmark primary redraw after resize mismatch\n", stderr);
				return 1;
			}

			sequenceSource.assign(3 * 4, 0);
			sequenceDib.assign(3 * 4, 0);
			copySequence(sequenceSource, sequenceDib, 3, RECT{ 0, 0, 3, 1 }, true, false);
			if (sequenceDib[3] != 0 || sequenceDib[7] != 0 || sequenceDib[11] != 0)
			{
				std::fputs("UlwDirtyCopyBenchmark selection transparency mismatch\n", stderr);
				return 1;
			}

			// 诊断只统计非原点脏区，忽略 RowPitch padding 与区外像素；覆盖黑墨/白光标及底层 alpha。
			constexpr int diagnosticWidth = 5;
			constexpr int diagnosticHeight = 4;
			constexpr size_t diagnosticPitch = diagnosticWidth * 4 + 8;
			const RECT diagnosticDirty{ 1, 1, 3, 3 };
			const auto rectEquals = [](RECT actual, RECT expected)
			{
				return actual.left == expected.left && actual.top == expected.top &&
					actual.right == expected.right && actual.bottom == expected.bottom;
			};
			for (int pattern = 0; pattern < 4; ++pattern)
			{
				std::vector<BYTE> source(diagnosticPitch * diagnosticHeight, 0xC7);
				for (LONG y = diagnosticDirty.top; y < diagnosticDirty.bottom; ++y)
					std::memset(source.data() + static_cast<size_t>(y) * diagnosticPitch + 4, 0, 8);
				const auto setPixel = [&](LONG x, LONG y, BYTE color, BYTE alpha)
				{
					BYTE* pixel = source.data() + static_cast<size_t>(y) * diagnosticPitch + x * 4;
					pixel[0] = pixel[1] = pixel[2] = color;
					pixel[3] = alpha;
				};
				if (pattern == 1) setPixel(2, 1, 0, 127); // 黑色笔迹：RGB 全零不能代表没有笔迹。
				if (pattern == 2)
				{
					setPixel(1, 2, 127, 127);
					setPixel(2, 2, 255, 255); // 白色光标最高 alpha 样本。
				}
				if (pattern == 3) setPixel(1, 1, 1, 0); // 底层可能修正最终预乘合法性，原始异常仍需可见。
				const std::vector<BYTE> originalSource = source;
				for (bool underlay : { false, true })
				{
					std::vector<BYTE> dib(diagnosticWidth * diagnosticHeight * 4, 0xA5);
					std::vector<BYTE> withoutDiagnostic = dib;
					UlwPixelDiagnostic pixels;
					bool unmapped = false;
					const UlwDirtyCopyResult result = CopyAndInspectUlwDirtyRows<true>(source.data(),
						diagnosticPitch, dib.data(), diagnosticWidth, diagnosticHeight, diagnosticDirty,
						false, underlay, [&] { unmapped = true; }, &pixels);
					const UlwDirtyCopyResult plain = CopyAndInspectUlwDirtyRows(source.data(),
						diagnosticPitch, withoutDiagnostic.data(), diagnosticWidth, diagnosticHeight,
						diagnosticDirty, false, underlay, [] {});
					const size_t sourceNonzero = pattern == 1 ? 1 : (pattern == 2 ? 2 : 0);
					const size_t rgbNonzero = pattern == 2 ? 2 : (pattern == 3 ? 1 : 0);
					const BYTE sourceMaxAlpha = pattern == 1 ? 127 : (pattern == 2 ? 255 : 0);
					const BYTE maxColor = pattern == 2 ? 255 : (pattern == 3 ? 1 : 0);
					const RECT sourceBounds = pattern == 1 ? RECT{ 2, 1, 3, 2 } :
						(pattern == 2 ? RECT{ 1, 2, 3, 3 } : RECT{});
					const POINT sample = pattern == 1 ? POINT{ 2, 1 } :
						(pattern == 2 ? POINT{ 2, 2 } : POINT{ 1, 1 });
					const BYTE finalMaxAlpha = underlay ? ComposePrimaryHitUnderlayAlpha(sourceMaxAlpha) : sourceMaxAlpha;
					const size_t sourceOffset = static_cast<size_t>(sample.y) * diagnosticPitch + sample.x * 4;
					const size_t finalOffset = (static_cast<size_t>(sample.y) * diagnosticWidth + sample.x) * 4;
					if (!unmapped || pixels.pixelCount != 4 ||
						pixels.source.alphaNonzero != sourceNonzero ||
						pixels.finalDib.alphaNonzero != (underlay ? 4 : sourceNonzero) ||
						pixels.source.rgbNonzero != rgbNonzero || pixels.finalDib.rgbNonzero != rgbNonzero ||
						pixels.source.maxBgra[3] != sourceMaxAlpha || pixels.finalDib.maxBgra[3] != finalMaxAlpha ||
						!rectEquals(pixels.source.alphaBounds, sourceBounds) ||
						!rectEquals(pixels.finalDib.alphaBounds, underlay ? diagnosticDirty : sourceBounds) ||
						pixels.source.premultipliedAlphaValid != (pattern != 3) ||
						pixels.finalDib.premultipliedAlphaValid != (pattern != 3 || underlay) ||
						pixels.samplePosition.x != sample.x || pixels.samplePosition.y != sample.y ||
						std::memcmp(pixels.sampleSourceBgra, source.data() + sourceOffset, 4) != 0 ||
						std::memcmp(pixels.sampleFinalBgra, dib.data() + finalOffset, 4) != 0 ||
						source != originalSource || dib != withoutDiagnostic ||
						result.allZeroAlpha != (sourceNonzero == 0) || result.fullFrameAllZeroAlpha ||
						result.allZeroAlpha != plain.allZeroAlpha ||
						result.premultipliedAlphaValid != plain.premultipliedAlphaValid ||
						result.fullFrameAllZeroAlpha != plain.fullFrameAllZeroAlpha)
					{
						std::fprintf(stderr, "UlwDirtyCopyBenchmark pixel diagnostic mismatch pattern=%d underlay=%d\n",
							pattern, underlay ? 1 : 0);
						return 1;
					}
					for (size_t channel = 0; channel < 3; ++channel)
					{
						if (pixels.source.maxBgra[channel] != maxColor || pixels.finalDib.maxBgra[channel] != maxColor)
						{
							std::fputs("UlwDirtyCopyBenchmark diagnostic color maxima mismatch\n", stderr);
							return 1;
						}
					}
				}
			}
			std::fputs("UlwDirtyCopyBenchmark pixel_diagnostics=PASS cases=8\n", stdout);

			// 单遍 SIMD 与原两遍标量逐字节对照：随机像素（含 α=0/127/128/255 与非法预乘）、各种宽度/起点/行尾、主画布与辅助两种。
			{
				std::uint32_t seed = 0x13579BDFu;
				const auto nextRandom = [&seed]() noexcept
				{
					seed = seed * 1664525u + 1013904223u;
					return seed >> 8;
				};
				size_t cases = 0;
				for (int width = 1; width <= 37; ++width)
				{
					for (int left = 0; left <= 5; ++left)
					{
						for (bool underlay : { false, true })
						{
							for (int flavor = 0; flavor < 3; ++flavor)
							{
								const int dibWidth = left + width + 3;
								constexpr int rows = 3;
								const size_t rowPitch = static_cast<size_t>(dibWidth) * 4 + 4 * (flavor + 1);
								std::vector<BYTE> source(rowPitch * rows);
								for (size_t offset = 0; offset < source.size(); offset += 4)
								{
									static constexpr BYTE edgeAlphas[] = { 0, 1, 126, 127, 128, 129, 254, 255 };
									const std::uint32_t random = nextRandom();
									// flavor 0：全透明；1：合法预乘；2：随机（含非法预乘）。
									const BYTE alpha = flavor == 0 ? 0 : (random & 1) ? edgeAlphas[(random >> 1) & 7] : static_cast<BYTE>(random >> 4);
									source[offset + 3] = alpha;
									for (size_t channel = 0; channel < 3; ++channel)
									{
										const BYTE value = static_cast<BYTE>(nextRandom());
										source[offset + channel] = flavor == 2 ? value : (alpha ? static_cast<BYTE>(value % (alpha + 1)) : 0);
									}
								}
								std::vector<BYTE> simdDib(static_cast<size_t>(dibWidth) * rows * 4, 0x5A);
								std::vector<BYTE> scalarDib = simdDib;
								const RECT dirty{ left, 0, left + width, rows };
								const UlwDirtyCopyResult simd = CopyAndInspectUlwDirtyRows(source.data(), rowPitch,
									simdDib.data(), dibWidth, rows, dirty, false, underlay, [] {});
								const UlwDirtyCopyResult scalar = CopyAndInspectUlwDirtyRows<false, true>(source.data(), rowPitch,
									scalarDib.data(), dibWidth, rows, dirty, false, underlay, [] {});
								if (simdDib != scalarDib || simd.allZeroAlpha != scalar.allZeroAlpha ||
									simd.premultipliedAlphaValid != scalar.premultipliedAlphaValid ||
									simd.fullFrameAllZeroAlpha != scalar.fullFrameAllZeroAlpha)
								{
									std::fprintf(stderr, "UlwDirtyCopyBenchmark simd/scalar mismatch width=%d left=%d underlay=%d flavor=%d\n",
										width, left, underlay ? 1 : 0, flavor);
									return 1;
								}
								++cases;
							}
						}
					}
				}
				std::fprintf(stdout, "UlwDirtyCopyBenchmark simd_equivalence=PASS cases=%zu simd=%d\n",
					cases, kUlwSimdCopyAvailable ? 1 : 0);
			}

			enum class PixelPattern { Mixed, Transparent, Half, InvalidAlpha };
			struct Scenario
			{
				const char* name;
				int width;
				int height;
				RECT dirty;
				bool presentFull;
				PixelPattern pattern;
				bool applyPrimaryHitUnderlay;
				bool allZeroAlpha;
				bool premultipliedAlphaValid;
				bool fullFrameAllZeroAlpha;
			};
			constexpr Scenario scenarios[] = {
				{ "full_mixed", 1920, 1080, { 0, 0, 1920, 1080 }, true,
					PixelPattern::Mixed, true, false, true, false },
				{ "partial_256", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::Mixed, true, false, true, false },
				{ "narrow_long", 1920, 1080, { 951, 0, 959, 1080 }, false,
					PixelPattern::Mixed, false, false, true, false },
				{ "full_transparent", 1920, 1080, { 0, 0, 1920, 1080 }, true,
					PixelPattern::Transparent, true, true, true, true },
				{ "partial_transparent", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::Transparent, false, true, true, false },
				{ "partial_half", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::Half, true, false, true, false },
				{ "partial_invalid_alpha", 1920, 1080, { 480, 304, 736, 560 }, false,
					PixelPattern::InvalidAlpha, true, false, false, false },
			};
			constexpr int warmupBlocks = 16;
			constexpr int measuredBlocks = 128;
			LARGE_INTEGER frequency = {};
			if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 1;
			std::fprintf(stdout,
				"UlwDirtyCopyBenchmark variants=simd_single_pass,scalar_two_pass metric=cpu_copy_plus_alpha_scan sample=single_call warmup_blocks=%d measured_blocks=%d qpc_frequency=%lld\n",
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
					if (scenario.applyPrimaryHitUnderlay)
					{
						for (LONG x = scenario.dirty.left; x < scenario.dirty.right; ++x)
						{
							BYTE* pixel = expected.data() + destinationOffset +
								static_cast<size_t>(x - scenario.dirty.left) * 4;
							pixel[3] = ComposePrimaryHitUnderlayAlpha(pixel[3]);
						}
					}
				}

				// 两种实现各测一遍：simd_single_pass 为生产路径，scalar_two_pass 为原两遍标量（基准与回退）。
				for (const bool scalarVariant : { false, true })
				{
					const char* variantName = scalarVariant ? "scalar_two_pass" : "simd_single_pass";
					long long totalTicks = 0;
					long long minimumTicks = (std::numeric_limits<long long>::max)();
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
						result = scalarVariant
							? CopyAndInspectUlwDirtyRows<false, true>(source.data(), rowPitch, dib.data(),
								scenario.width, scenario.height, scenario.dirty, scenario.presentFull,
								scenario.applyPrimaryHitUnderlay, [] {})
							: CopyAndInspectUlwDirtyRows(source.data(), rowPitch, dib.data(),
								scenario.width, scenario.height, scenario.dirty, scenario.presentFull,
								scenario.applyPrimaryHitUnderlay, [] {});
						if (!QueryPerformanceCounter(&stop)) return 1;
						cyclesAvailable = cyclesAvailable &&
							QueryThreadCycleTime(GetCurrentThread(), &stopCycles) != FALSE;
						if (result.allZeroAlpha != scenario.allZeroAlpha ||
							result.premultipliedAlphaValid != scenario.premultipliedAlphaValid ||
							result.fullFrameAllZeroAlpha != scenario.fullFrameAllZeroAlpha ||
							std::memcmp(dib.data(), expected.data(), dib.size()) != 0)
						{
							std::fprintf(stderr, "UlwDirtyCopyBenchmark mismatch scenario=%s variant=%s block=%d\n",
								scenario.name, variantName, block);
							return 1;
						}
						if (block < warmupBlocks) continue;
						const long long ticks = static_cast<long long>(stop.QuadPart - start.QuadPart);
						totalTicks += ticks;
						minimumTicks = (std::min)(minimumTicks, ticks);
						std::uint64_t hash = 14695981039346656037ull;
						for (BYTE value : dib) hash = (hash ^ value) * 1099511628211ull;
						for (BYTE flag : { static_cast<BYTE>(result.allZeroAlpha),
							static_cast<BYTE>(result.premultipliedAlphaValid),
							static_cast<BYTE>(result.fullFrameAllZeroAlpha) })
							hash = (hash ^ flag) * 1099511628211ull;
						std::fprintf(stdout,
							"UlwDirtyCopyBenchmark scenario=%s variant=%s block=%d iterations=1 qpc_ticks=%lld cycles_available=%d thread_cycles=%llu hash=%016llx all_zero=%d premul_valid=%d full_zero=%d\n",
							scenario.name, variantName, block - warmupBlocks + 1,
							ticks, cyclesAvailable ? 1 : 0,
							static_cast<unsigned long long>(cyclesAvailable ? stopCycles - startCycles : 0),
							static_cast<unsigned long long>(hash), result.allZeroAlpha ? 1 : 0,
							result.premultipliedAlphaValid ? 1 : 0, result.fullFrameAllZeroAlpha ? 1 : 0);
					}
					const double ticksToMicroseconds = 1000000.0 / static_cast<double>(frequency.QuadPart);
					std::fprintf(stdout, "UlwDirtyCopyBenchmark summary scenario=%s variant=%s avg_us=%.2f min_us=%.2f\n",
						scenario.name, variantName, static_cast<double>(totalTicks) / measuredBlocks * ticksToMicroseconds,
						static_cast<double>(minimumTicks) * ticksToMicroseconds);
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

	int RunUlwPipelineSelfTest() noexcept
	{
		// 双槽异步读回的 DIB 逐字节对照：同一张最终纹理，异步 presenter 与同步 presenter 各自读回，
		// 全部完成后两张 DIB 必须相同。窗口是从不显示的分层弹出窗，测试结束即销毁，不需要人工操作。
		HWND window = nullptr;
		const wchar_t* className = L"InkeysUlwPipelineSelfTest";
		const HINSTANCE instance = GetModuleHandleW(nullptr);
		int failures = 0;
		const auto check = [&](bool condition, const char* name)
		{
			if (!condition) { ++failures; std::fprintf(stderr, "[UlwPipeline] FAIL %s\n", name); }
			return condition;
		};
		try
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
			D3D_FEATURE_LEVEL level = {};
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, device.GetAddressOf(), &level, context.GetAddressOf())), "WARP device"))
				return 1;
			WNDCLASSEXW windowClass = { sizeof(windowClass) };
			windowClass.lpfnWndProc = DefWindowProcW;
			windowClass.hInstance = instance;
			windowClass.lpszClassName = className;
			RegisterClassExW(&windowClass);
			constexpr int width = 64;
			constexpr int height = 48;
			window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"", WS_POPUP,
				-32000, -32000, width, height, nullptr, nullptr, instance, nullptr);
			if (!check(window != nullptr, "hidden layered window")) return 1;

			D3D11_TEXTURE2D_DESC finalDescription = {};
			finalDescription.Width = width;
			finalDescription.Height = height;
			finalDescription.MipLevels = 1;
			finalDescription.ArraySize = 1;
			finalDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			finalDescription.SampleDesc.Count = 1;
			finalDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> finalTexture;
			if (!check(SUCCEEDED(device->CreateTexture2D(&finalDescription, nullptr, finalTexture.GetAddressOf())), "final texture"))
				return 1;

			std::uint32_t seed = 0x2468ACE1u;
			const auto nextRandom = [&seed]() noexcept { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
			// 往最终纹理的 rect 写入随机预乘像素，模拟一帧合成。
			const auto drawFrame = [&](RECT rect)
			{
				const size_t rowPitch = static_cast<size_t>(rect.right - rect.left) * 4;
				std::vector<BYTE> pixels(rowPitch * static_cast<size_t>(rect.bottom - rect.top));
				for (size_t offset = 0; offset < pixels.size(); offset += 4)
				{
					const BYTE alpha = static_cast<BYTE>(nextRandom());
					pixels[offset + 3] = alpha;
					for (size_t channel = 0; channel < 3; ++channel)
						pixels[offset + channel] = static_cast<BYTE>(nextRandom() % (static_cast<std::uint32_t>(alpha) + 1));
				}
				const D3D11_BOX box = { static_cast<UINT>(rect.left), static_cast<UINT>(rect.top), 0,
					static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom), 1 };
				context->UpdateSubresource(finalTexture.Get(), 0, &box, pixels.data(), static_cast<UINT>(rowPitch), 0);
			};
			const auto randomRect = [&]()
			{
				const LONG left = static_cast<LONG>(nextRandom() % (width - 1));
				const LONG top = static_cast<LONG>(nextRandom() % (height - 1));
				const LONG right = left + 1 + static_cast<LONG>(nextRandom() % (width - left));
				const LONG bottom = top + 1 + static_cast<LONG>(nextRandom() % (height - top));
				return RECT{ left, top, (std::min)(right, static_cast<LONG>(width)), (std::min)(bottom, static_cast<LONG>(height)) };
			};

			CompositionClock invalidClock;
			const auto runScenario = [&](const char* name, double frameIntervalMs, bool forcePipelined, int frames,
				bool resizeMidway, bool expectSkips)
			{
				UlwDirtyRectPresenter asyncPresenter;
				UlwDirtyRectPresenter syncPresenter;
				asyncPresenter.diagnosticRole = syncPresenter.diagnosticRole = "SelfTest";
				asyncPresenter.applyPrimaryHitUnderlay = syncPresenter.applyPrimaryHitUnderlay = true;
				if (!check(asyncPresenter.Initialize(window, device.Get(), context.Get(), width, height) &&
					syncPresenter.Initialize(window, device.Get(), context.Get(), width, height), name)) return;
				if (forcePipelined)
					for (int sample = 0; sample < 3; ++sample)
						asyncPresenter.pacing.OnFrameMeasured(frameIntervalMs * 3.0, frameIntervalMs, frameIntervalMs, 0.0);
				check(!forcePipelined || asyncPresenter.pacing.Mode() == UlwReadbackMode::Pipelined, "scenario entered pipelined");
				drawFrame(RECT{ 0, 0, width, height });
				double nowMs = 1000.0;
				std::uint32_t skippedBefore = 0;
				bool ok = true;
				for (int frame = 0; frame < frames && ok; ++frame)
				{
					// 第一帧整帧；之后随机脏区（相邻帧常重叠，验证双槽按顺序写 DIB）。
					const bool full = frame == 0;
					const RECT dirty = full ? RECT{ 0, 0, width, height } : randomRect();
					UlwFrameTiming timing;
					timing.nowMs = nowMs;
					timing.frameStartMs = nowMs - 0.5;
					timing.frameIntervalMs = frameIntervalMs;
					timing.refreshMs = 1000.0 / 60.0;
					timing.clock = &invalidClock;
					double waitedMs = 0.0;
					// C1 在本帧绘制之前：探测上一帧、到点拷出累计脏区。
					if (frame % 5 == 4) ok = asyncPresenter.ServiceFrameStart(finalTexture.Get(), timing, waitedMs);
					if (!full) drawFrame(dirty);
					ok = ok && asyncPresenter.PresentAsync(finalTexture.Get(), dirty, full, timing, waitedMs);
					ok = ok && syncPresenter.Present(finalTexture.Get(), dirty, full);
					// 帧尾（固定帧截止等待前，晚于 Present 约 0.6 帧）：累计脏区到点拷出。
					if (frame % 3 == 2)
					{
						UlwFrameTiming frameEnd = timing;
						frameEnd.nowMs += frameIntervalMs * 0.6;
						ok = ok && asyncPresenter.ServiceFrameEnd(finalTexture.Get(), frameEnd, waitedMs);
					}
					skippedBefore += asyncPresenter.pipelineSummary.skipped;
					asyncPresenter.pipelineSummary.skipped = 0;
					if (resizeMidway && frame == frames / 2)
					{
						// Resize 丢弃待完成提交与累计脏区，下一次 Present 必须整帧读回。
						ok = ok && asyncPresenter.Resize(width, height);
						check(!asyncPresenter.pending.active && !asyncPresenter.hasAccumulated && asyncPresenter.needsFullReadback,
							"resize discards pipeline and requests full readback");
					}
					nowMs += frameIntervalMs;
				}
				check(ok, name);
				double waitedMs = 0.0;
				check(asyncPresenter.CompleteBeforeIdle(finalTexture.Get(), nowMs, waitedMs), "complete before idle");
				check(!asyncPresenter.pending.active && !asyncPresenter.hasAccumulated, "idle leaves nothing pending");
				check(!expectSkips || skippedBefore > 0, "throttle skipped frames");
				// 节流场景必须真的经帧尾拷出过累计脏区，否则帧尾路径没有被 DIB 对照覆盖。
				// 流水模式下帧尾拷出会延后，由 C1/C2 完成并计入那两处，这里只对即时模式断言。
				check(!expectSkips || forcePipelined || asyncPresenter.pipelineSummary.frameEnd > 0,
					"throttle scenario exercised frame-end flush");
				// 路径断言：流水场景必须真的走到延后与背压，否则这次 DIB 对照没有覆盖异步顺序。
				check(!forcePipelined || (asyncPresenter.pipelineSummary.deferred > 0 &&
					asyncPresenter.pipelineSummary.backpressure > 0), "pipelined scenario exercised deferred and backpressure");
				const size_t dibBytes = static_cast<size_t>(width) * height * 4;
				const bool same = asyncPresenter.dibBits && syncPresenter.dibBits &&
					std::memcmp(asyncPresenter.dibBits, syncPresenter.dibBits, dibBytes) == 0;
				if (!check(same, "async DIB equals synchronous DIB"))
					std::fprintf(stderr, "[UlwPipeline] scenario=%s DIB mismatch\n", name);
				std::fprintf(stdout, "UlwPipeline scenario=%s frames=%d skipped=%u deferred=%u backpressure=%u probeHit=%u immediate=%u frameEnd=%u beforeIdle=%u pass=%d\n",
					name, frames, skippedBefore, asyncPresenter.pipelineSummary.deferred, asyncPresenter.pipelineSummary.backpressure,
					asyncPresenter.pipelineSummary.probeHit, asyncPresenter.pipelineSummary.immediate,
					asyncPresenter.pipelineSummary.frameEnd, asyncPresenter.pipelineSummary.beforeIdle, same ? 1 : 0);
				asyncPresenter.Reset();
				syncPresenter.Reset();
			};
			runScenario("immediate_60", 1000.0 / 60.0, false, 40, false, false);
			runScenario("pipelined_60", 1000.0 / 60.0, true, 60, false, false);
			runScenario("throttle_240", 1000.0 / 240.0, false, 80, false, true);
			runScenario("pipelined_throttle_240", 1000.0 / 240.0, true, 80, false, true);
			runScenario("pipelined_resize", 1000.0 / 60.0, true, 40, true, false);
		}
		catch (const std::exception& error)
		{
			std::fprintf(stderr, "[UlwPipeline] exception: %s\n", error.what());
			failures += 1;
		}
		catch (...)
		{
			std::fputs("[UlwPipeline] unknown exception\n", stderr);
			failures += 1;
		}
		if (window) DestroyWindow(window);
		UnregisterClassW(className, instance);
		std::fprintf(stdout, "UlwPipeline failures=%d\n", failures);
		std::fflush(stdout);
		return failures == 0 ? 0 : 1;
	}
}
