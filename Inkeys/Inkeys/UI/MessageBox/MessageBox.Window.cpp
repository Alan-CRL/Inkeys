module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Windowsx.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <winternl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.UI.MessageBox;

namespace Inkeys::UI::MessageBox::Detail
{
	namespace
	{
		using namespace Gdiplus;

		constexpr UINT TestAutomationTimerId = 0x494B4D42;
		constexpr UINT AbortDialogMessage = WM_APP + 0x4D;
		constexpr DWORD OwnerHealthTimeoutMilliseconds = 250;
		constexpr DWORD DwmWindowAttributeCloak = 13;

		INIT_ONCE legacyGdiplusOnce = INIT_ONCE_STATIC_INIT;
		ULONG_PTR legacyGdiplusToken = 0;
		bool legacyGdiplusReady = false;

		class UniqueHandle
		{
		public:
			UniqueHandle() noexcept = default;
			explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
			~UniqueHandle() { Reset(); }
			UniqueHandle(const UniqueHandle&) = delete;
			UniqueHandle& operator=(const UniqueHandle&) = delete;
			UniqueHandle(UniqueHandle&& other) noexcept
				: handle_(std::exchange(other.handle_, nullptr)) {}
			UniqueHandle& operator=(UniqueHandle&& other) noexcept
			{
				if (this != &other)
				{
					Reset();
					handle_ = std::exchange(other.handle_, nullptr);
				}
				return *this;
			}
			[[nodiscard]] HANDLE Get() const noexcept { return handle_; }
			[[nodiscard]] explicit operator bool() const noexcept
			{
				return handle_ != nullptr;
			}
			void Reset(HANDLE next = nullptr) noexcept
			{
				if (handle_) CloseHandle(handle_);
				handle_ = next;
			}

		private:
			HANDLE handle_ = nullptr;
		};

		class ComApartment
		{
		public:
			ComApartment() noexcept
			{
				const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				initialized_ = result == S_OK || result == S_FALSE;
			}
			~ComApartment()
			{
				if (initialized_) CoUninitialize();
			}

		private:
			bool initialized_ = false;
		};

		bool IsWindows8OrLater() noexcept
		{
			using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
			const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			const auto rtlGetVersion = ntdll
				? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))
				: nullptr;
			if (!rtlGetVersion) return false;
			RTL_OSVERSIONINFOW version{};
			version.dwOSVersionInfoSize = sizeof(version);
			if (rtlGetVersion(&version) != 0) return false;
			return version.dwMajorVersion > 6
				|| version.dwMajorVersion == 6 && version.dwMinorVersion >= 2;
		}

		BOOL CALLBACK InitializeLegacyGdiplus(PINIT_ONCE, PVOID, PVOID*) noexcept
		{
			GdiplusStartupInput input;
			legacyGdiplusReady = GdiplusStartup(
				&legacyGdiplusToken, &input, nullptr) == Ok;
			return TRUE;
		}

		class PrivateGdiplusRuntime
		{
		public:
			[[nodiscard]] bool Start() noexcept
			{
				if (!IsWindows8OrLater())
				{
					if (!InitOnceExecuteOnce(&legacyGdiplusOnce,
						InitializeLegacyGdiplus, nullptr, nullptr)) return false;
					return legacyGdiplusReady;
				}
				GdiplusStartupInput input;
				ownsToken_ = GdiplusStartup(&token_, &input, nullptr) == Ok;
				return ownsToken_;
			}

			~PrivateGdiplusRuntime()
			{
				if (ownsToken_) GdiplusShutdown(token_);
			}

		private:
			ULONG_PTR token_ = 0;
			bool ownsToken_ = false;
		};

		class ThreadDpiContext
		{
		public:
			ThreadDpiContext() noexcept
			{
				using SetThreadDpiAwarenessContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(
					DPI_AWARENESS_CONTEXT);
				const HMODULE user32 = GetModuleHandleW(L"user32.dll");
				function_ = user32 ? reinterpret_cast<SetThreadDpiAwarenessContextFn>(
					GetProcAddress(user32, "SetThreadDpiAwarenessContext")) : nullptr;
				if (function_)
					previous_ = function_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			}

			~ThreadDpiContext()
			{
				if (function_ && previous_) (void)function_(previous_);
			}

		private:
			using Function = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
			Function function_ = nullptr;
			DPI_AWARENESS_CONTEXT previous_ = nullptr;
		};

		class DibSurface
		{
		public:
			DibSurface() noexcept = default;
			~DibSurface() { Reset(); }
			DibSurface(const DibSurface&) = delete;
			DibSurface& operator=(const DibSurface&) = delete;

			[[nodiscard]] bool Create(int width, int height) noexcept
			{
				if (width <= 0 || height <= 0) return false;
				HDC nextDc = CreateCompatibleDC(nullptr);
				if (!nextDc) return false;
				BITMAPINFO info{};
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = width;
				info.bmiHeader.biHeight = -height;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;
				void* nextBits = nullptr;
				HBITMAP nextBitmap = CreateDIBSection(nextDc, &info,
					DIB_RGB_COLORS, &nextBits, nullptr, 0);
				if (!nextBitmap || !nextBits)
				{
					if (nextBitmap) DeleteObject(nextBitmap);
					DeleteDC(nextDc);
					return false;
				}
				HGDIOBJ nextOld = SelectObject(nextDc, nextBitmap);
				if (!nextOld || nextOld == HGDI_ERROR)
				{
					DeleteObject(nextBitmap);
					DeleteDC(nextDc);
					return false;
				}
				Reset();
				dc_ = nextDc;
				bitmap_ = nextBitmap;
				oldBitmap_ = nextOld;
				bits_ = nextBits;
				width_ = width;
				height_ = height;
				return true;
			}

			void Reset() noexcept
			{
				if (dc_ && oldBitmap_) SelectObject(dc_, oldBitmap_);
				if (bitmap_) DeleteObject(bitmap_);
				if (dc_) DeleteDC(dc_);
				dc_ = nullptr;
				bitmap_ = nullptr;
				oldBitmap_ = nullptr;
				bits_ = nullptr;
				width_ = 0;
				height_ = 0;
			}

			[[nodiscard]] HDC Dc() const noexcept { return dc_; }
			[[nodiscard]] int Width() const noexcept { return width_; }
			[[nodiscard]] int Height() const noexcept { return height_; }
			void Swap(DibSurface& other) noexcept
			{
				std::swap(dc_, other.dc_);
				std::swap(bitmap_, other.bitmap_);
				std::swap(oldBitmap_, other.oldBitmap_);
				std::swap(bits_, other.bits_);
				std::swap(width_, other.width_);
				std::swap(height_, other.height_);
			}

		private:
			HDC dc_ = nullptr;
			HBITMAP bitmap_ = nullptr;
			HGDIOBJ oldBitmap_ = nullptr;
			void* bits_ = nullptr;
			int width_ = 0;
			int height_ = 0;
		};

		[[nodiscard]] bool IsCjkCharacter(wchar_t value) noexcept
		{
			return value >= 0x2E80 && value <= 0x9FFF
				|| value >= 0xF900 && value <= 0xFAFF;
		}

		void AddRoundedRectangle(GraphicsPath& path,
			const RectF& bounds, REAL radius)
		{
			const REAL diameter = std::min(radius * 2.0f,
				std::min(bounds.Width, bounds.Height));
			if (diameter <= 0.0f)
			{
				path.AddRectangle(bounds);
				return;
			}
			path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0f, 90.0f);
			path.AddArc(bounds.GetRight() - diameter, bounds.Y,
				diameter, diameter, 270.0f, 90.0f);
			path.AddArc(bounds.GetRight() - diameter,
				bounds.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
			path.AddArc(bounds.X, bounds.GetBottom() - diameter,
				diameter, diameter, 90.0f, 90.0f);
			path.CloseFigure();
		}

		[[nodiscard]] HMONITOR SelectMonitor(HWND owner) noexcept
		{
			if (owner && IsWindow(owner))
				return MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
			const HWND foreground = GetForegroundWindow();
			if (foreground)
			{
				const HMONITOR monitor = MonitorFromWindow(
					foreground, MONITOR_DEFAULTTONULL);
				if (monitor) return monitor;
			}
			POINT cursor{};
			if (GetCursorPos(&cursor))
			{
				const HMONITOR monitor = MonitorFromPoint(
					cursor, MONITOR_DEFAULTTONULL);
				if (monitor) return monitor;
			}
			return MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		}

		[[nodiscard]] UINT ResolveDpi(HWND owner, HMONITOR monitor) noexcept
		{
			using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
			using GetDpiForSystemFn = UINT(WINAPI*)();
			const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			// owner 可能来自 DPI-unaware 线程，此时 GetDpiForWindow 只会返回虚拟 96。
			// 当前 UI 线程已是 PMv2，应优先按最终目标 monitor 取得有效 DPI。
			const HMODULE shcore = LoadLibraryW(L"shcore.dll");
			if (shcore)
			{
				using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR,
					MONITOR_DPI_TYPE, UINT*, UINT*);
				const auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
					GetProcAddress(shcore, "GetDpiForMonitor"));
				UINT x = 0;
				UINT y = 0;
				const bool succeeded = getDpiForMonitor
					&& SUCCEEDED(getDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y))
					&& x != 0;
				FreeLibrary(shcore);
				if (succeeded) return x;
			}

			if (owner && user32)
			{
				const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
					GetProcAddress(user32, "GetDpiForWindow"));
				if (getDpiForWindow)
				{
					const UINT dpi = getDpiForWindow(owner);
					if (dpi) return dpi;
				}
			}

			if (user32)
			{
				const auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
					GetProcAddress(user32, "GetDpiForSystem"));
				if (getDpiForSystem)
				{
					const UINT dpi = getDpiForSystem();
					if (dpi) return dpi;
				}
			}

			HDC screen = GetDC(nullptr);
			const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
			if (screen) ReleaseDC(nullptr, screen);
			return dpi > 0 ? static_cast<UINT>(dpi) : 96;
		}

		[[nodiscard]] bool ResolveWorkArea(HMONITOR monitor, RECT& output) noexcept
		{
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			if (!monitor || !GetMonitorInfoW(monitor, &info)) return false;
			output = info.rcWork;
			return output.right > output.left && output.bottom > output.top;
		}

		[[nodiscard]] std::unique_ptr<Bitmap> DecodeIcon(
			const OwnedIcon& icon) noexcept
		{
			try
			{
				if (icon.kind == StoredIconKind::PremultipliedBgra)
				{
					auto bitmap = std::make_unique<Bitmap>(
						static_cast<INT>(icon.width), static_cast<INT>(icon.height),
						static_cast<INT>(icon.strideBytes), PixelFormat32bppPARGB,
						const_cast<BYTE*>(icon.bytes.data()));
					return bitmap->GetLastStatus() == Ok ? std::move(bitmap) : nullptr;
				}
				if (icon.kind != StoredIconKind::PngBytes || icon.bytes.empty())
					return nullptr;

				HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, icon.bytes.size());
				if (!memory) return nullptr;
				void* target = GlobalLock(memory);
				if (!target)
				{
					GlobalFree(memory);
					return nullptr;
				}
				std::memcpy(target, icon.bytes.data(), icon.bytes.size());
				GlobalUnlock(memory);

				IStream* rawStream = nullptr;
				if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &rawStream)))
				{
					GlobalFree(memory);
					return nullptr;
				}
				struct StreamReleaser
				{
					void operator()(IStream* value) const noexcept
					{
						if (value) value->Release();
					}
				};
				std::unique_ptr<IStream, StreamReleaser> stream(rawStream);
				std::unique_ptr<Bitmap> result;
				{
					Bitmap decoded(stream.get(), FALSE);
					if (decoded.GetLastStatus() == Ok && decoded.GetWidth() > 0
						&& decoded.GetHeight() > 0
						&& decoded.GetWidth() <= MaximumIconDimension
						&& decoded.GetHeight() <= MaximumIconDimension)
					{
						result = std::make_unique<Bitmap>(decoded.GetWidth(),
							decoded.GetHeight(), PixelFormat32bppPARGB);
						if (result->GetLastStatus() == Ok)
						{
							Graphics graphics(result.get());
							graphics.SetCompositingMode(CompositingModeSourceCopy);
							graphics.Clear(Color(0, 0, 0, 0));
							graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
							if (graphics.DrawImage(&decoded, 0, 0,
								decoded.GetWidth(), decoded.GetHeight()) != Ok)
								result.reset();
						}
						else result.reset();
					}
				}
				return result;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		struct FontCandidate
		{
			const wchar_t* family = nullptr;
			INT style = FontStyleRegular;
		};

		[[nodiscard]] bool ResolveFontFamily(const FontCandidate* candidates,
			std::size_t count, std::unique_ptr<FontFamily>& family,
			INT& style)
		{
			for (std::size_t index = 0; index < count; ++index)
			{
				auto next = std::make_unique<FontFamily>(candidates[index].family);
				if (next->GetLastStatus() != Ok
					|| !next->IsStyleAvailable(candidates[index].style)) continue;
				family = std::move(next);
				style = candidates[index].style;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool HideDialogWindow(HWND hwnd) noexcept
		{
			if (!hwnd) return false;
			const bool topmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE)
				& WS_EX_TOPMOST) != 0;
			// ownerless 故障框先独立退出 topmost band，再隐藏并销毁。
			if (topmost)
				(void)SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			return SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
				SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE
				| SWP_NOACTIVATE | SWP_HIDEWINDOW) != FALSE;
		}

		[[nodiscard]] bool WaitForOwnerRestored(HANDLE restoredEvent) noexcept
		{
			for (;;)
			{
				const DWORD wait = MsgWaitForMultipleObjectsEx(1, &restoredEvent,
					INFINITE, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
				if (wait == WAIT_OBJECT_0) return true;
				if (wait != WAIT_OBJECT_0 + 1) return false;
				// owner 激活恢复可能同步回调本线程；只服务 sent message，避免互等。
				MSG pending{};
				(void)PeekMessageW(&pending, nullptr, 0, 0, PM_NOREMOVE);
			}
		}

		struct DialogSession
		{
			OwnedRequest* request = nullptr;
			DialogLayout layout{};
			RECT workArea{};
			HMONITOR monitor = nullptr;
			std::unique_ptr<Bitmap> icon;
			std::unique_ptr<FontFamily> titleFontFamily;
			std::unique_ptr<FontFamily> bodyFontFamily;
			INT titleFontStyle = FontStyleRegular;
			INT bodyFontStyle = FontStyleRegular;
			DibSurface surface;
			HWND hwnd = nullptr;
			HINSTANCE instance = nullptr;
			std::wstring className;
			HANDLE preflightEvent = nullptr;
			HANDLE proceedEvent = nullptr;
			HANDLE hiddenEvent = nullptr;
			HANDLE ownerRestoredEvent = nullptr;
			std::atomic_bool preflightSucceeded = false;
			std::atomic_bool createAllowed = false;
			std::atomic_bool committed = false;
			std::atomic_bool hiddenSignaled = false;
			std::atomic_bool abortRequested = false;
			std::atomic<Result> result = Result::Failed;
			int hoveredButton = -1;
			int pressedButton = -1;
			int focusedButton = 0;
			bool closeHovered = false;
			bool closePressed = false;
			bool trackingMouse = false;
			bool framePainted = false;
			HWND previousForeground = nullptr;
			bool ownerlessForegroundAcquired = false;

			void HideWindow() noexcept
			{
				const bool restoreForeground = !request->owner
					&& ownerlessForegroundAcquired
					&& GetForegroundWindow() == hwnd;
				if (!HideDialogWindow(hwnd)) return;
				if (restoreForeground && previousForeground
					&& previousForeground != hwnd && IsWindow(previousForeground))
				{
					// 仅归还本框实际取得的前台，不覆盖用户在显示期间的切换。
					(void)SetForegroundWindow(previousForeground);
				}
				ownerlessForegroundAcquired = false;
			}

			[[nodiscard]] bool HasCloseButton() const noexcept
			{
				return request->dismissEnabled && request->showCloseButton;
			}

			[[nodiscard]] int ButtonAt(POINT point) const noexcept
			{
				for (int index = 0; index < layout.buttonCount; ++index)
					if (PtInRect(&layout.buttonSpecs[index].bounds, point)) return index;
				return -1;
			}

			void RequestRepaint() noexcept
			{
				if (!hwnd) return;
				Render();
				InvalidateRect(hwnd, nullptr, FALSE);
			}

			void CommitResult(Result next) noexcept
			{
				bool expected = false;
				if (!committed.compare_exchange_strong(expected, true)) return;
				result.store(next, std::memory_order_release);
				HideWindow();
				hiddenSignaled.store(true, std::memory_order_release);
				SetEvent(hiddenEvent);
				PostQuitMessage(0);
			}

			void TryDismiss() noexcept
			{
				if (request->dismissEnabled) CommitResult(request->dismissResult);
			}

			[[nodiscard]] bool WordFits(Graphics& graphics, const Font& font,
				const std::wstring& text, REAL maximumWidth) const
			{
				std::wstring token;
				auto flush = [&]()
					{
						if (token.empty()) return true;
						RectF measured;
						const PointF origin(0.0f, 0.0f);
						graphics.MeasureString(token.c_str(), -1, &font, origin, &measured);
						token.clear();
						return measured.Width <= maximumWidth + 1.0f;
					};
				for (const wchar_t value : text)
				{
					// CJK 字符自身可换行，但相邻 ASCII 长词仍需单独验证。
					if (iswspace(value) || IsCjkCharacter(value))
					{
						if (!flush()) return false;
					}
					else token.push_back(value);
				}
				return flush();
			}

			[[nodiscard]] bool BuildLayout(UINT dpi, HMONITOR targetMonitor,
				const RECT* overrideWorkArea = nullptr) noexcept
			{
				try
				{
				if (dpi < 48 || dpi > 960) return false;
				RECT nextWorkArea{};
				if (overrideWorkArea) nextWorkArea = *overrideWorkArea;
				else if (!ResolveWorkArea(targetMonitor, nextWorkArea)) return false;
				const long long workWidthValue = static_cast<long long>(nextWorkArea.right)
					- nextWorkArea.left;
				const long long workHeightValue = static_cast<long long>(nextWorkArea.bottom)
					- nextWorkArea.top;
				if (workWidthValue <= 0 || workHeightValue <= 0
					|| workWidthValue > std::numeric_limits<int>::max()
					|| workHeightValue > std::numeric_limits<int>::max()) return false;
				const int workWidth = static_cast<int>(workWidthValue);
				const int workHeight = static_cast<int>(workHeightValue);
				const int minimumWidth = ScaleDipValue(320, dpi);
				const int maximumWidth = std::min(ScaleDipValue(548, dpi), workWidth);
				if (maximumWidth < minimumWidth
					|| workHeight < ScaleDipValue(184, dpi)) return false;

				DialogLayout next{};
				next.dpi = dpi;
				next.width = minimumWidth;
				const int padding = ScaleDipValue(24, dpi);
				const int titleGap = ScaleDipValue(12, dpi);
				const int iconSize = ScaleDipValue(40, dpi);
				const int iconGap = ScaleDipValue(16, dpi);
				const int closeSize = ScaleDipValue(32, dpi);
				const int buttonGap = ScaleDipValue(8, dpi);
				const int buttonHeight = ScaleDipValue(32, dpi);
				const int separator = std::max(1, ScaleDipValue(1, dpi));

				Bitmap measureBitmap(1, 1, PixelFormat32bppPARGB);
				Graphics graphics(&measureBitmap);
				graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
				if (!titleFontFamily)
				{
					const FontCandidate candidates[]{
						{ L"Segoe UI Variable Display Semibold", FontStyleRegular },
						{ L"Segoe UI Semibold", FontStyleRegular },
						{ L"Segoe UI Bold", FontStyleRegular },
						{ L"Segoe UI Variable Display", FontStyleBold },
						{ L"Segoe UI", FontStyleBold },
					};
					if (!ResolveFontFamily(candidates, _countof(candidates),
						titleFontFamily, titleFontStyle)) return false;
				}
				if (!bodyFontFamily)
				{
					const FontCandidate candidates[]{
						{ L"Segoe UI Variable Text", FontStyleRegular },
						{ L"Segoe UI", FontStyleRegular },
					};
					if (!ResolveFontFamily(candidates, _countof(candidates),
						bodyFontFamily, bodyFontStyle)) return false;
				}
				Font titleFont(titleFontFamily.get(),
					static_cast<REAL>(ScaleDipValue(20, dpi)), titleFontStyle, UnitPixel);
				Font bodyFont(bodyFontFamily.get(),
					static_cast<REAL>(ScaleDipValue(14, dpi)), bodyFontStyle, UnitPixel);
				if (titleFont.GetLastStatus() != Ok || bodyFont.GetLastStatus() != Ok)
					return false;

				const int closeReserve = HasCloseButton() ? closeSize + titleGap : 0;
				const int iconReserve = icon ? iconSize + iconGap : 0;
				const int maximumTitleWidth = maximumWidth - padding * 2 - closeReserve;
				const int maximumBodyWidth = maximumWidth - padding * 2
					- iconReserve;
				if (maximumTitleWidth <= 0 || maximumBodyWidth <= 0) return false;
				if (!WordFits(graphics, titleFont, request->title,
					static_cast<REAL>(maximumTitleWidth))
					|| !WordFits(graphics, bodyFont, request->body,
						static_cast<REAL>(maximumBodyWidth))) return false;

				const PointF origin(0.0f, 0.0f);
				RectF preferredTitle{};
				RectF preferredBody{};
				if (!request->title.empty()
					&& graphics.MeasureString(request->title.c_str(), -1, &titleFont,
						origin, &preferredTitle) != Ok) return false;
				if (!request->body.empty()
					&& graphics.MeasureString(request->body.c_str(), -1, &bodyFont,
						origin, &preferredBody) != Ok) return false;
				const long long preferredTitleWidth = static_cast<long long>(
					std::ceil(preferredTitle.Width)) + padding * 2LL + closeReserve;
				const long long preferredBodyWidth = static_cast<long long>(
					std::ceil(preferredBody.Width)) + padding * 2LL + iconReserve;
				const long long preferredWidth = std::max<long long>(minimumWidth,
					std::max(preferredTitleWidth, preferredBodyWidth));
				next.width = static_cast<int>(std::min<long long>(
					maximumWidth, preferredWidth));

				const int titleWidth = next.width - padding * 2 - closeReserve;
				const int bodyWidth = next.width - padding * 2
					- (icon ? iconSize + iconGap : 0);
				if (titleWidth <= 0 || bodyWidth <= 0) return false;

				StringFormat format;
				format.SetTrimming(StringTrimmingNone);
				format.SetFormatFlags(StringFormatFlagsLineLimit);
				RectF measuredTitle{};
				INT titleLines = 0;
				const RectF titleMeasureBounds(0.0f, 0.0f,
					static_cast<REAL>(titleWidth), static_cast<REAL>(ScaleDipValue(200, dpi)));
				if (!request->title.empty())
				{
					if (graphics.MeasureString(request->title.c_str(), -1, &titleFont,
						titleMeasureBounds, &format, &measuredTitle, nullptr,
						&titleLines) != Ok) return false;
					if (titleLines > 2 || !WordFits(graphics, titleFont,
						request->title, static_cast<REAL>(titleWidth))) return false;
				}

				RectF measuredBody{};
				INT bodyLines = 0;
				const RectF bodyMeasureBounds(0.0f, 0.0f,
					static_cast<REAL>(bodyWidth), static_cast<REAL>(ScaleDipValue(1000, dpi)));
				if (!request->body.empty())
				{
					if (graphics.MeasureString(request->body.c_str(), -1, &bodyFont,
						bodyMeasureBounds, &format, &measuredBody, nullptr,
						&bodyLines) != Ok) return false;
					if (!WordFits(graphics, bodyFont, request->body,
						static_cast<REAL>(bodyWidth))) return false;
				}

				next.titleTop = padding;
				next.titleHeight = request->title.empty() ? 0
					: static_cast<int>(std::ceil(measuredTitle.Height)) + 1;
				next.bodyTop = next.titleTop + next.titleHeight
					+ (request->body.empty() && !icon ? 0 : titleGap);
				next.bodyHeight = request->body.empty() ? 0
					: static_cast<int>(std::ceil(measuredBody.Height)) + 1;
				const int bodyRowHeight = std::max(next.bodyHeight, icon ? iconSize : 0);
				const int contentBottom = next.bodyTop + bodyRowHeight + padding;
				next.commandTop = contentBottom;
				next.height = contentBottom + separator + padding + buttonHeight + padding;
				next.height = std::max(next.height, ScaleDipValue(184, dpi));
				if (next.height > ScaleDipValue(756, dpi) || next.height > workHeight)
					return false;

				next.titleBounds = { padding, next.titleTop,
					padding + titleWidth, next.titleTop + next.titleHeight };
				const int bodyLeft = padding + (icon ? iconSize + iconGap : 0);
				next.bodyBounds = { bodyLeft, next.bodyTop,
					bodyLeft + bodyWidth, next.bodyTop + next.bodyHeight };
				if (icon)
					next.iconBounds = { padding, next.bodyTop,
						padding + iconSize, next.bodyTop + iconSize };
				if (HasCloseButton())
				{
					const REAL titleLineHeight = titleFont.GetHeight(&graphics);
					const double titleFirstLineCenter = static_cast<double>(next.titleTop)
						+ static_cast<double>(titleLineHeight) / 2.0;
					const double closeTop = titleFirstLineCenter
						- static_cast<double>(closeSize) / 2.0;
					if (!std::isfinite(titleLineHeight) || titleLineHeight <= 0.0f
						|| !std::isfinite(titleFirstLineCenter) || !std::isfinite(closeTop)
						|| closeTop < 0.0
						|| closeTop > static_cast<double>(std::numeric_limits<int>::max()))
						return false;
					const int closeOuterGap = static_cast<int>(std::lround(closeTop));
					if (closeOuterGap > next.width - closeSize
						|| closeOuterGap > std::numeric_limits<int>::max() - closeSize)
						return false;
					// 统一外边距在对齐标题首行中心的同时保持顶部与右侧对称。
					next.closeBounds = { next.width - closeOuterGap - closeSize,
						closeOuterGap, next.width - closeOuterGap,
						closeOuterGap + closeSize };
				}

				const auto labels = ResolveButtonLabels(GetThreadUILanguage());
				next.buttonCount = BuildButtonSpecs(request->buttons, labels,
					next.buttonSpecs);
				const int buttonsWidth = next.width - padding * 2
					- buttonGap * (next.buttonCount - 1);
				const int slotWidth = buttonsWidth / next.buttonCount;
				const int buttonTop = next.commandTop + separator + padding;
				int left = padding;
				for (int index = 0; index < next.buttonCount; ++index)
				{
					const int right = index + 1 == next.buttonCount
						? next.width - padding : left + slotWidth;
					next.buttonSpecs[index].bounds = {
						left, buttonTop, right, buttonTop + buttonHeight };
					left = right + buttonGap;
				}

				layout = std::move(next);
				workArea = nextWorkArea;
				monitor = targetMonitor;
				for (int index = 0; index < layout.buttonCount; ++index)
					if (layout.buttonSpecs[index].result == request->defaultResult)
						focusedButton = index;
				return true;
				}
				catch (...)
				{
					return false;
				}
			}

			void DrawButton(Graphics& graphics, const ButtonSpec& button,
				int index, const Font& font) noexcept
			{
				const bool primary = index == 0;
				const bool hovered = button.enabled && hoveredButton == index;
				const bool pressed = button.enabled && pressedButton == index;
				Color fill = primary ? Color(255, 96, 205, 255)
					: Color(255, 45, 45, 45);
				if (!button.enabled) fill = Color(255, 32, 32, 32);
				if (pressed) fill = primary ? Color(255, 62, 166, 210)
					: Color(255, 39, 39, 39);
				else if (hovered) fill = primary ? Color(255, 117, 214, 255)
					: Color(255, 50, 50, 50);
				const RectF bounds(static_cast<REAL>(button.bounds.left),
					static_cast<REAL>(button.bounds.top),
					static_cast<REAL>(button.bounds.right - button.bounds.left),
					static_cast<REAL>(button.bounds.bottom - button.bounds.top));
				GraphicsPath path;
				AddRoundedRectangle(path, bounds,
					static_cast<REAL>(ScaleDipValue(4, layout.dpi)));
				SolidBrush fillBrush(fill);
				graphics.FillPath(&fillBrush, &path);
				Pen border(Color(102, 117, 117, 117),
					static_cast<REAL>(std::max(1, ScaleDipValue(1, layout.dpi))));
				graphics.DrawPath(&border, &path);

				if (button.enabled && focusedButton == index)
				{
					const REAL oneDip = static_cast<REAL>(layout.dpi) / 96.0f;
					const REAL twoDip = oneDip * 2.0f;
					const REAL buttonRadius = oneDip * 4.0f;

					// WinUI 高可见焦点视觉完全位于按钮外侧：2 DIP 白色外框 + 1 DIP 深色内框。
					RectF outerFocus = bounds;
					outerFocus.Inflate(twoDip, twoDip);
					GraphicsPath outerFocusPath;
					AddRoundedRectangle(outerFocusPath, outerFocus,
						buttonRadius + twoDip);
					Pen outerFocusPen(Color(255, 255, 255, 255), twoDip);
					graphics.DrawPath(&outerFocusPen, &outerFocusPath);

					RectF innerFocus = bounds;
					innerFocus.Inflate(oneDip / 2.0f, oneDip / 2.0f);
					GraphicsPath innerFocusPath;
					AddRoundedRectangle(innerFocusPath, innerFocus,
						buttonRadius + oneDip / 2.0f);
					Pen innerFocusPen(Color(179, 0, 0, 0), oneDip);
					graphics.DrawPath(&innerFocusPen, &innerFocusPath);
				}

				SolidBrush textBrush(!button.enabled ? Color(93, 255, 255, 255)
					: primary ? Color(255, 0, 29, 38)
					: Color(255, 255, 255, 255));
				StringFormat textFormat;
				textFormat.SetAlignment(StringAlignmentCenter);
				textFormat.SetLineAlignment(StringAlignmentCenter);
				textFormat.SetTrimming(StringTrimmingNone);
				graphics.DrawString(button.label.c_str(), -1, &font,
					bounds, &textFormat, &textBrush);
			}

			void Render() noexcept
			{
				if (!surface.Dc()) return;
				Graphics graphics(surface.Dc());
				graphics.SetSmoothingMode(SmoothingModeAntiAlias);
				graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
				graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
				graphics.Clear(Color(255, 43, 43, 43));

				const int separator = std::max(1, ScaleDipValue(1, layout.dpi));
				SolidBrush commandBrush(Color(255, 32, 32, 32));
				graphics.FillRectangle(&commandBrush, 0, layout.commandTop,
					layout.width, layout.height - layout.commandTop);
				Pen separatorPen(Color(25, 0, 0, 0), static_cast<REAL>(separator));
				graphics.DrawLine(&separatorPen, 0.0f,
					static_cast<REAL>(layout.commandTop),
					static_cast<REAL>(layout.width),
					static_cast<REAL>(layout.commandTop));

				if (!titleFontFamily || !bodyFontFamily) return;
				Font titleFont(titleFontFamily.get(),
					static_cast<REAL>(ScaleDipValue(20, layout.dpi)),
					titleFontStyle, UnitPixel);
				Font bodyFont(bodyFontFamily.get(),
					static_cast<REAL>(ScaleDipValue(14, layout.dpi)),
					bodyFontStyle, UnitPixel);
				SolidBrush titleBrush(Color(255, 255, 255, 255));
				SolidBrush bodyBrush(Color(197, 255, 255, 255));
				StringFormat textFormat;
				textFormat.SetAlignment(StringAlignmentNear);
				textFormat.SetLineAlignment(StringAlignmentNear);
				textFormat.SetTrimming(StringTrimmingNone);
				textFormat.SetFormatFlags(StringFormatFlagsLineLimit);
				const RectF titleBounds(static_cast<REAL>(layout.titleBounds.left),
					static_cast<REAL>(layout.titleBounds.top),
					static_cast<REAL>(layout.titleBounds.right - layout.titleBounds.left),
					static_cast<REAL>(layout.titleBounds.bottom - layout.titleBounds.top));
				const RectF bodyBounds(static_cast<REAL>(layout.bodyBounds.left),
					static_cast<REAL>(layout.bodyBounds.top),
					static_cast<REAL>(layout.bodyBounds.right - layout.bodyBounds.left),
					static_cast<REAL>(layout.bodyBounds.bottom - layout.bodyBounds.top));
				if (!request->title.empty())
					graphics.DrawString(request->title.c_str(), -1, &titleFont,
						titleBounds, &textFormat, &titleBrush);
				if (!request->body.empty())
					graphics.DrawString(request->body.c_str(), -1, &bodyFont,
						bodyBounds, &textFormat, &bodyBrush);

				if (icon && layout.iconBounds.right > layout.iconBounds.left)
				{
					graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
					const int slotWidth = layout.iconBounds.right - layout.iconBounds.left;
					const int slotHeight = layout.iconBounds.bottom - layout.iconBounds.top;
					const double scale = std::min(
						static_cast<double>(slotWidth) / icon->GetWidth(),
						static_cast<double>(slotHeight) / icon->GetHeight());
					const int width = std::max(1, static_cast<int>(std::lround(icon->GetWidth() * scale)));
					const int height = std::max(1, static_cast<int>(std::lround(icon->GetHeight() * scale)));
					const int x = layout.iconBounds.left + (slotWidth - width) / 2;
					const int y = layout.iconBounds.top + (slotHeight - height) / 2;
					graphics.DrawImage(icon.get(), x, y, width, height);
				}

				for (int index = 0; index < layout.buttonCount; ++index)
					DrawButton(graphics, layout.buttonSpecs[index], index, bodyFont);

				if (HasCloseButton())
				{
					const RectF closeBounds(static_cast<REAL>(layout.closeBounds.left),
						static_cast<REAL>(layout.closeBounds.top),
						static_cast<REAL>(layout.closeBounds.right - layout.closeBounds.left),
						static_cast<REAL>(layout.closeBounds.bottom - layout.closeBounds.top));
					if (closeHovered || closePressed)
					{
						GraphicsPath closePath;
						AddRoundedRectangle(closePath, closeBounds,
							static_cast<REAL>(ScaleDipValue(4, layout.dpi)));
						SolidBrush closeFill(closePressed ? Color(255, 56, 56, 56)
							: Color(255, 67, 67, 67));
						graphics.FillPath(&closeFill, &closePath);
					}
					const REAL centerX =
						(closeBounds.GetLeft() + closeBounds.GetRight()) / 2.0f;
					const REAL centerY =
						(closeBounds.GetTop() + closeBounds.GetBottom()) / 2.0f;
					const REAL half = static_cast<REAL>(ScaleDipValue(5, layout.dpi));
					Pen closePen(Color(255, 255, 255, 255),
						static_cast<REAL>(std::max(1, ScaleDipValue(1, layout.dpi))));
					closePen.SetStartCap(LineCapFlat);
					closePen.SetEndCap(LineCapFlat);
					graphics.DrawLine(&closePen, centerX - half, centerY - half,
						centerX + half, centerY + half);
					graphics.DrawLine(&closePen, centerX + half, centerY - half,
						centerX - half, centerY + half);
				}

				const REAL borderWidth = static_cast<REAL>(
					std::max(1, ScaleDipValue(1, layout.dpi)));
				const REAL inset = borderWidth / 2.0f;
				const RectF outer(inset, inset,
					static_cast<REAL>(layout.width) - borderWidth,
					static_cast<REAL>(layout.height) - borderWidth);
				GraphicsPath outerPath;
				AddRoundedRectangle(outerPath, outer,
					static_cast<REAL>(ScaleDipValue(8, layout.dpi)));
				Pen border(Color(102, 117, 117, 117), borderWidth);
				graphics.DrawPath(&border, &outerPath);
			}
		};

		void ApplyDwmFrame(HWND hwnd) noexcept
		{
			BOOL enabled = TRUE;
			if (FAILED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))))
				(void)DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled));
			const int roundPreference = 2;
			(void)DwmSetWindowAttribute(hwnd, 33, &roundPreference,
				sizeof(roundPreference));
			const MARGINS margins{ 1, 1, 1, 1 };
			(void)DwmExtendFrameIntoClientArea(hwnd, &margins);
		}

		void PaintSession(DialogSession& session) noexcept
		{
			PAINTSTRUCT paint{};
			HDC target = BeginPaint(session.hwnd, &paint);
			if (target && session.surface.Dc())
			{
				session.framePainted = BitBlt(target,
					paint.rcPaint.left, paint.rcPaint.top,
					paint.rcPaint.right - paint.rcPaint.left,
					paint.rcPaint.bottom - paint.rcPaint.top,
					session.surface.Dc(), paint.rcPaint.left,
					paint.rcPaint.top, SRCCOPY) != FALSE;
			}
			EndPaint(session.hwnd, &paint);
		}

		[[nodiscard]] bool RevealPreparedWindow(DialogSession& session,
			HWND hwnd, int x, int y) noexcept
		{
			BOOL cloakEnabled = TRUE;
			const bool cloaked = SUCCEEDED(DwmSetWindowAttribute(hwnd,
				DwmWindowAttributeCloak, &cloakEnabled, sizeof(cloakEnabled)));
			auto removeCloak = [&]() noexcept
				{
					if (!cloaked) return true;
					const BOOL cloakDisabled = FALSE;
					return SUCCEEDED(DwmSetWindowAttribute(hwnd,
						DwmWindowAttributeCloak, &cloakDisabled,
						sizeof(cloakDisabled)));
				};
			// SetWindowPos 不消费 STARTUPINFO；支持 cloak 时窗口仍未对用户可见。
			if (!SetWindowPos(hwnd, nullptr, x, y,
				session.layout.width, session.layout.height,
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW))
			{
				(void)removeCloak();
				return false;
			}
			// 固定矩形生效后同步提交完整 DIB，再解除 cloak 揭示首帧。
			session.framePainted = false;
			if (!RedrawWindow(hwnd, nullptr, nullptr,
				RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME)
				|| !session.framePainted)
			{
				session.HideWindow();
				(void)removeCloak();
				return false;
			}
#ifdef INKEYS_MESSAGE_BOX_TESTING
			if (session.request->automation.firstFrameReadyCallback)
				session.request->automation.firstFrameReadyCallback(hwnd,
					session.request->automation.firstFrameReadyContext);
#endif
			if (!removeCloak())
			{
				session.HideWindow();
				return false;
			}
			return true;
		}

		LRESULT CALLBACK DialogWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam) noexcept
		{
			DialogSession* session = reinterpret_cast<DialogSession*>(
				GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (message == WM_NCCREATE)
			{
				const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
				session = static_cast<DialogSession*>(create->lpCreateParams);
				SetWindowLongPtrW(hwnd, GWLP_USERDATA,
					reinterpret_cast<LONG_PTR>(session));
				if (session) session->hwnd = hwnd;
			}
			if (!session) return DefWindowProcW(hwnd, message, wParam, lParam);

			switch (message)
			{
			case WM_NCCALCSIZE:
				return 0;
			case WM_NCPAINT:
				return 0;
			case WM_NCHITTEST:
			{
				LRESULT ignored = 0;
				(void)DwmDefWindowProc(hwnd, message, wParam, lParam, &ignored);
				POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(hwnd, &point);
				if (session->HasCloseButton()
					&& PtInRect(&session->layout.closeBounds, point)) return HTCLIENT;
				if (point.y >= 0 && point.y < ScaleDipValue(58, session->layout.dpi))
					return HTCAPTION;
				return HTCLIENT;
			}
			case WM_GETMINMAXINFO:
			{
				auto info = reinterpret_cast<MINMAXINFO*>(lParam);
				info->ptMinTrackSize = { session->layout.width, session->layout.height };
				info->ptMaxTrackSize = { session->layout.width, session->layout.height };
				return 0;
			}
			case WM_SYSCOMMAND:
				switch (wParam & 0xFFF0)
				{
				case SC_SIZE:
				case SC_MINIMIZE:
				case SC_MAXIMIZE:
					return 0;
				case SC_CLOSE:
					session->TryDismiss();
					return 0;
				default:
					break;
				}
				break;
			case WM_CLOSE:
				session->TryDismiss();
				return 0;
			case AbortDialogMessage:
				session->abortRequested.store(true, std::memory_order_release);
				session->HideWindow();
				session->hiddenSignaled.store(true, std::memory_order_release);
				SetEvent(session->hiddenEvent);
				PostQuitMessage(0);
				return 0;
			case WM_PAINT:
				PaintSession(*session);
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_MOUSEMOVE:
			{
				if (!session->trackingMouse)
				{
					TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
					session->trackingMouse = TrackMouseEvent(&tracking) != FALSE;
				}
				POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const int hovered = session->ButtonAt(point);
				const bool closeHovered = session->HasCloseButton()
					&& PtInRect(&session->layout.closeBounds, point);
				if (hovered != session->hoveredButton
					|| closeHovered != session->closeHovered)
				{
					session->hoveredButton = hovered;
					session->closeHovered = closeHovered;
					session->RequestRepaint();
				}
				return 0;
			}
			case WM_MOUSELEAVE:
				session->trackingMouse = false;
				session->hoveredButton = -1;
				session->closeHovered = false;
				session->RequestRepaint();
				return 0;
			case WM_LBUTTONDOWN:
			{
				SetFocus(hwnd);
				POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				session->pressedButton = session->ButtonAt(point);
				session->closePressed = session->HasCloseButton()
					&& PtInRect(&session->layout.closeBounds, point);
				if (session->pressedButton >= 0)
					session->focusedButton = session->pressedButton;
				if (session->pressedButton >= 0 || session->closePressed)
					SetCapture(hwnd);
				session->RequestRepaint();
				return 0;
			}
			case WM_LBUTTONUP:
			{
				POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const int pressed = session->pressedButton;
				const bool closePressed = session->closePressed;
				session->pressedButton = -1;
				session->closePressed = false;
				if (GetCapture() == hwnd) ReleaseCapture();
				if (pressed >= 0 && session->ButtonAt(point) == pressed)
					session->CommitResult(session->layout.buttonSpecs[pressed].result);
				else if (closePressed && session->HasCloseButton()
					&& PtInRect(&session->layout.closeBounds, point))
					session->TryDismiss();
				else session->RequestRepaint();
				return 0;
			}
			case WM_CAPTURECHANGED:
				if (session->pressedButton >= 0 || session->closePressed)
				{
					session->pressedButton = -1;
					session->closePressed = false;
					session->RequestRepaint();
				}
				return 0;
			case WM_CANCELMODE:
			case WM_KILLFOCUS:
				if (session->pressedButton >= 0 || session->closePressed)
				{
					session->pressedButton = -1;
					session->closePressed = false;
					if (GetCapture() == hwnd) ReleaseCapture();
					session->RequestRepaint();
				}
				return 0;
			case WM_GETDLGCODE:
				return DLGC_WANTALLKEYS;
			case WM_KEYDOWN:
				switch (wParam)
				{
				case VK_TAB:
				{
					const int direction = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
					session->focusedButton = (session->focusedButton + direction
						+ session->layout.buttonCount) % session->layout.buttonCount;
					session->RequestRepaint();
					return 0;
				}
				case VK_RETURN:
					session->CommitResult(session->request->defaultResult);
					return 0;
				case VK_SPACE:
					if (session->focusedButton >= 0
						&& session->focusedButton < session->layout.buttonCount)
						session->CommitResult(session->layout.buttonSpecs[
							session->focusedButton].result);
					return 0;
				case VK_ESCAPE:
					session->TryDismiss();
					return 0;
				default:
					break;
				}
				break;
			case WM_SYSKEYDOWN:
				if (wParam == VK_F4)
				{
					session->TryDismiss();
					return 0;
				}
				break;
			case WM_DPICHANGED:
			{
				const UINT dpi = LOWORD(wParam) ? LOWORD(wParam) : HIWORD(wParam);
				const RECT suggested = *reinterpret_cast<const RECT*>(lParam);
				const HMONITOR target = MonitorFromRect(&suggested,
					MONITOR_DEFAULTTONEAREST);
				DialogLayout previousLayout = std::move(session->layout);
				const RECT previousWorkArea = session->workArea;
				const HMONITOR previousMonitor = session->monitor;
				const int previousFocusedButton = session->focusedButton;
				bool rebuilt = false;
				DibSurface nextSurface;
				if (session->BuildLayout(dpi ? dpi : 96, target))
				{
					if (nextSurface.Create(session->layout.width,
						session->layout.height))
					{
						int x = suggested.left;
						int y = suggested.top;
						x = std::clamp(x, static_cast<int>(session->workArea.left),
							static_cast<int>(session->workArea.right) - session->layout.width);
						y = std::clamp(y, static_cast<int>(session->workArea.top),
								static_cast<int>(session->workArea.bottom) - session->layout.height);
						session->surface.Swap(nextSurface);
						session->Render();
						rebuilt = SetWindowPos(hwnd, nullptr, x, y,
							session->layout.width, session->layout.height,
							SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
						if (rebuilt) InvalidateRect(hwnd, nullptr, FALSE);
						else session->surface.Swap(nextSurface);
					}
				}
				if (!rebuilt)
				{
					session->layout = std::move(previousLayout);
					session->workArea = previousWorkArea;
					session->monitor = previousMonitor;
					session->focusedButton = previousFocusedButton;
				}
				return 0;
			}
#ifdef INKEYS_MESSAGE_BOX_TESTING
			case WM_TIMER:
				if (wParam == TestAutomationTimerId)
				{
					KillTimer(hwnd, TestAutomationTimerId);
					if (session->request->automation.visibleCallback)
						session->request->automation.visibleCallback(hwnd,
							session->request->automation.context);
					// 测试注入未命中时也必须自动收口，首个已排队结果仍优先提交。
					if (!session->committed.load(std::memory_order_acquire))
						PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
					return 0;
				}
				break;
#endif
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}

		[[nodiscard]] bool RegisterDialogClass(DialogSession& session) noexcept
		{
			try
			{
				session.className = L"Inkeys.FluentMessageBox."
					+ std::to_wstring(GetCurrentProcessId()) + L"."
					+ std::to_wstring(GetCurrentThreadId()) + L"."
					+ std::to_wstring(GetTickCount64());
			}
			catch (...)
			{
				return false;
			}
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.style = CS_DBLCLKS;
			windowClass.lpfnWndProc = DialogWindowProc;
			windowClass.hInstance = session.instance;
			windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.lpszClassName = session.className.c_str();
			return RegisterClassExW(&windowClass) != 0;
		}

		void UiThreadMain(DialogSession* session) noexcept
		{
			ThreadDpiContext dpiContext;
			ComApartment apartment;
			PrivateGdiplusRuntime gdiplus;
			struct GraphicsCleanup
			{
				DialogSession* session = nullptr;
				~GraphicsCleanup()
				{
					if (!session) return;
					// Bitmap、DIB 和 memory DC 均在创建它们的 UI 线程退出前销毁。
					session->icon.reset();
					session->titleFontFamily.reset();
					session->bodyFontFamily.reset();
					session->surface.Reset();
				}
			} graphicsCleanup{ session };
			bool classRegistered = false;
			try
			{
				if (!gdiplus.Start())
				{
					SetEvent(session->preflightEvent);
					return;
				}
				session->icon = DecodeIcon(session->request->icon);
				const HMONITOR monitor = SelectMonitor(session->request->owner);
				const UINT dpi = ResolveDpi(session->request->owner, monitor);
				if (!session->BuildLayout(dpi, monitor)
					|| !session->surface.Create(session->layout.width,
						session->layout.height))
				{
					SetEvent(session->preflightEvent);
					return;
				}
				session->Render();
				session->preflightSucceeded.store(true, std::memory_order_release);
				SetEvent(session->preflightEvent);
				if (WaitForSingleObject(session->proceedEvent, INFINITE) != WAIT_OBJECT_0
					|| !session->createAllowed.load(std::memory_order_acquire)) return;

				session->instance = GetModuleHandleW(nullptr);
				classRegistered = RegisterDialogClass(*session);
				if (!classRegistered)
				{
					SetEvent(session->hiddenEvent);
					if (session->request->owner)
						(void)WaitForOwnerRestored(session->ownerRestoredEvent);
					return;
				}

				const int x = session->workArea.left
					+ (session->workArea.right - session->workArea.left
						- session->layout.width) / 2;
				const int y = session->workArea.top
					+ (session->workArea.bottom - session->workArea.top
						- session->layout.height) / 2;
				DWORD exStyle = WS_EX_TOOLWINDOW;
				if (!session->request->owner
					&& session->request->ownerlessTopmostAtCreation) exStyle |= WS_EX_TOPMOST;
				const DWORD style = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
				const HWND hwnd = CreateWindowExW(exStyle, session->className.c_str(),
					session->request->title.c_str(), style, x, y,
					session->layout.width, session->layout.height,
					session->request->owner, nullptr, session->instance, session);
				if (!hwnd)
				{
					SetEvent(session->hiddenEvent);
					if (session->request->owner)
						(void)WaitForOwnerRestored(session->ownerRestoredEvent);
					UnregisterClassW(session->className.c_str(), session->instance);
					return;
				}

				ApplyDwmFrame(hwnd);
				if (!session->request->dismissEnabled)
				{
					const HMENU menu = GetSystemMenu(hwnd, FALSE);
					if (menu) EnableMenuItem(menu, SC_CLOSE,
						MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
				}
				if (!RevealPreparedWindow(*session, hwnd, x, y))
				{
					if (IsWindowVisible(hwnd)) session->HideWindow();
					if (!session->hiddenSignaled.exchange(true))
						SetEvent(session->hiddenEvent);
					if (session->request->owner)
						(void)WaitForOwnerRestored(session->ownerRestoredEvent);
					session->icon.reset();
					session->titleFontFamily.reset();
					session->bodyFontFamily.reset();
					session->surface.Reset();
					DestroyWindow(hwnd);
					session->hwnd = nullptr;
					UnregisterClassW(session->className.c_str(), session->instance);
					classRegistered = false;
					return;
				}
				if (!session->request->owner)
					session->previousForeground = GetForegroundWindow();
				SetForegroundWindow(hwnd);
				SetFocus(hwnd);
				if (!session->request->owner)
					session->ownerlessForegroundAcquired = GetForegroundWindow() == hwnd;
#ifdef INKEYS_MESSAGE_BOX_TESTING
				if (session->request->automation.visibleCallback)
					SetTimer(hwnd, TestAutomationTimerId,
						std::max<UINT>(1, session->request->automation.delayMilliseconds), nullptr);
#endif

				MSG message{};
				for (;;)
				{
					const BOOL value = GetMessageW(&message, nullptr, 0, 0);
					if (value <= 0) break;
					TranslateMessage(&message);
					DispatchMessageW(&message);
				}

				if (IsWindow(hwnd) && IsWindowVisible(hwnd)) session->HideWindow();
				if (!session->hiddenSignaled.exchange(true)) SetEvent(session->hiddenEvent);
				if (session->request->owner)
					(void)WaitForOwnerRestored(session->ownerRestoredEvent);
				session->icon.reset();
				session->titleFontFamily.reset();
				session->bodyFontFamily.reset();
				session->surface.Reset();
				if (IsWindow(hwnd)) DestroyWindow(hwnd);
				session->hwnd = nullptr;
				UnregisterClassW(session->className.c_str(), session->instance);
				classRegistered = false;
			}
			catch (...)
			{
				if (!session->preflightSucceeded.load(std::memory_order_acquire))
					SetEvent(session->preflightEvent);
				if (session->hwnd && IsWindow(session->hwnd))
					session->HideWindow();
				if (!session->hiddenSignaled.exchange(true)) SetEvent(session->hiddenEvent);
				if (session->request->owner
					&& session->createAllowed.load(std::memory_order_acquire))
					(void)WaitForOwnerRestored(session->ownerRestoredEvent);
				session->icon.reset();
				session->titleFontFamily.reset();
				session->bodyFontFamily.reset();
				session->surface.Reset();
				if (session->hwnd && IsWindow(session->hwnd)) DestroyWindow(session->hwnd);
				if (classRegistered)
					UnregisterClassW(session->className.c_str(), session->instance);
			}
		}

		[[nodiscard]] bool WaitForDialogHidden(DialogSession& session,
			HANDLE workerHandle, bool serviceSentMessages) noexcept
		{
			HANDLE handles[2] = { session.hiddenEvent, workerHandle };
			for (;;)
			{
				DWORD wait = WAIT_FAILED;
				if (serviceSentMessages)
				{
					wait = MsgWaitForMultipleObjectsEx(2, handles, INFINITE,
						QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
					if (wait == WAIT_OBJECT_0 + 2)
					{
						// PM_NOREMOVE 会服务 sent message，但不会分派 posted/input 队列。
						MSG pending{};
						PeekMessageW(&pending, nullptr, 0, 0, PM_NOREMOVE);
						continue;
					}
				}
				else wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
				if (wait == WAIT_OBJECT_0) return true;
				if (wait == WAIT_OBJECT_0 + 1)
					return session.hiddenSignaled.load(std::memory_order_acquire);
				return false;
			}
		}

		[[nodiscard]] bool SameOwnedWindow(HWND owner,
			DWORD expectedThread, DWORD expectedProcess) noexcept
		{
			DWORD process = 0;
			const DWORD thread = owner && IsWindow(owner)
				? GetWindowThreadProcessId(owner, &process) : 0;
			return thread == expectedThread && process == expectedProcess;
		}
	}

	RunOutcome RunDialog(OwnedRequest& request) noexcept
	{
		UniqueHandle preflight(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		UniqueHandle proceed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		UniqueHandle hidden(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		UniqueHandle restored(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!preflight || !proceed || !hidden || !restored) return {};

		DialogSession session;
		session.request = &request;
		session.preflightEvent = preflight.Get();
		session.proceedEvent = proceed.Get();
		session.hiddenEvent = hidden.Get();
		session.ownerRestoredEvent = restored.Get();

		std::thread worker;
		try
		{
			worker = std::thread(UiThreadMain, &session);
		}
		catch (...)
		{
			return {};
		}
		const HANDLE workerHandle = reinterpret_cast<HANDLE>(worker.native_handle());
		HANDLE initialWaits[2] = { preflight.Get(), workerHandle };
		const DWORD initialWait = WaitForMultipleObjects(2, initialWaits, FALSE, INFINITE);
		if (initialWait != WAIT_OBJECT_0
			|| !session.preflightSucceeded.load(std::memory_order_acquire))
		{
			if (worker.joinable()) worker.join();
			return {};
		}

		DWORD ownerThread = 0;
		DWORD ownerProcess = 0;
		bool ownerWasEnabled = false;
		bool ownerDisabledHere = false;
		bool ownerWasForeground = false;
		HWND previousFocus = nullptr;
		if (request.owner)
		{
			ownerThread = GetWindowThreadProcessId(request.owner, &ownerProcess);
			if (!SameOwnedWindow(request.owner, ownerThread, ownerProcess)
				|| ownerProcess != GetCurrentProcessId())
			{
				session.createAllowed.store(false, std::memory_order_release);
				SetEvent(proceed.Get());
				worker.join();
				return {};
			}
			if (ownerThread != GetCurrentThreadId())
			{
				DWORD_PTR ignored = 0;
				if (!SendMessageTimeoutW(request.owner, WM_NULL, 0, 0,
					SMTO_ABORTIFHUNG | SMTO_BLOCK,
					OwnerHealthTimeoutMilliseconds, &ignored))
				{
					session.createAllowed.store(false, std::memory_order_release);
					SetEvent(proceed.Get());
					worker.join();
					return {};
				}
			}
			ownerWasEnabled = IsWindowEnabled(request.owner) != FALSE;
			ownerWasForeground = GetForegroundWindow() == request.owner;
			if (ownerThread == GetCurrentThreadId()) previousFocus = GetFocus();
			if (ownerWasEnabled)
			{
				(void)EnableWindow(request.owner, FALSE);
				ownerDisabledHere = IsWindowEnabled(request.owner) == FALSE;
				if (!ownerDisabledHere)
				{
					session.createAllowed.store(false, std::memory_order_release);
					SetEvent(proceed.Get());
					worker.join();
					return {};
				}
			}
		}

		session.createAllowed.store(true, std::memory_order_release);
		SetEvent(proceed.Get());
		const bool hiddenObserved = WaitForDialogHidden(session, workerHandle,
			request.owner && ownerThread == GetCurrentThreadId());
		if (!hiddenObserved)
		{
			session.abortRequested.store(true, std::memory_order_release);
			const HWND hwnd = session.hwnd;
			if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, AbortDialogMessage, 0, 0);
		}

		// owner 恢复只由调用线程执行，且在 HWND 身份仍匹配时恢复原 enabled 状态。
		if (request.owner && ownerDisabledHere
			&& SameOwnedWindow(request.owner, ownerThread, ownerProcess))
		{
			(void)EnableWindow(request.owner, TRUE);
			if (ownerWasForeground)
			{
				(void)SetForegroundWindow(request.owner);
				if (ownerThread == GetCurrentThreadId())
				{
					(void)SetActiveWindow(request.owner);
					if (previousFocus && IsWindow(previousFocus)) (void)SetFocus(previousFocus);
				}
			}
		}
		SetEvent(restored.Get());
		if (worker.joinable()) worker.join();

		if (session.committed.load(std::memory_order_acquire))
			return { true, session.result.load(std::memory_order_acquire) };
		return {};
	}
}

#ifdef INKEYS_MESSAGE_BOX_TESTING
namespace Inkeys::UI::MessageBox::Test
{
	LayoutProbe ProbeLayout(const Request& request, UINT dpi,
		int workAreaWidth, int workAreaHeight) noexcept
	{
		if (!Detail::ValidateRequest(request)
			|| workAreaWidth <= 0 || workAreaHeight <= 0) return {};
		Detail::OwnedRequest owned;
		if (!Detail::CopyRequest(request, owned)) return {};
		Detail::PrivateGdiplusRuntime gdiplus;
		if (!gdiplus.Start()) return {};
		Detail::ComApartment apartment;
		Detail::DialogSession session;
		session.request = &owned;
		session.icon = Detail::DecodeIcon(owned.icon);
		const RECT workArea{ 0, 0, workAreaWidth, workAreaHeight };
		if (!session.BuildLayout(dpi ? dpi : 96, nullptr, &workArea)) return {};
		return { true, session.layout.width, session.layout.height,
			session.layout.buttonCount, session.icon != nullptr };
	}
}
#endif
