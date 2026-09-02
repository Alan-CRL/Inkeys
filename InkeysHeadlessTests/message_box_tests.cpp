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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../Inkeys/resource.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

#ifdef MessageBox
#undef MessageBox
#endif

import Inkeys.UI.MessageBox;

namespace
{
	using namespace Inkeys::UI::MessageBox;
	namespace MessageBoxTest = Inkeys::UI::MessageBox::Test;

	int failures = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failures;
		std::cerr << "message_box_test failed: " << name << '\n';
	}

	struct FallbackCapture
	{
		int calls = 0;
		HWND owner = nullptr;
		UINT flags = 0;
		std::size_t bodyLength = 0;
		bool ownerEnabled = false;
		bool reenter = false;
		Result nestedResult = Result::Failed;
		Result result = Result::Ok;
	};

	Result CaptureSystemFallback(const Request& request, HWND owner,
		UINT flags, void* opaque) noexcept
	{
		auto& capture = *static_cast<FallbackCapture*>(opaque);
		++capture.calls;
		capture.owner = owner;
		capture.flags = flags;
		capture.bodyLength = request.body
			? std::char_traits<wchar_t>::length(request.body) : 0;
		capture.ownerEnabled = !owner || IsWindowEnabled(owner) != FALSE;
		if (capture.reenter && capture.calls == 1)
		{
			auto nested = MakeOkRequest(L"Nested", L"Nested fallback");
			capture.nestedResult = Show(nested);
		}
		return capture.result;
	}

	void TestRequestAndResultContracts()
	{
		auto ok = MakeOkRequest(L"Title", L"Body");
		Check(MessageBoxTest::Validate(ok), "OK factory validates");
		Check(ok.defaultResult == Result::Ok && ok.dismissResult == Result::Ok,
			"OK factory defaults");

		auto okCancel = MakeOkCancelRequest(L"Title", L"Body");
		Check(MessageBoxTest::Validate(okCancel), "OK/Cancel factory validates");
		Check(okCancel.defaultResult == Result::Ok
			&& okCancel.dismissResult == Result::Cancel,
			"OK/Cancel factory defaults");

		auto yesNo = MakeYesNoRequest(L"Title", L"Body");
		Check(MessageBoxTest::Validate(yesNo), "Yes/No factory validates");
		Check(!yesNo.dismissEnabled && !yesNo.showCloseButton
			&& yesNo.defaultResult == Result::Yes,
			"Yes/No factory disables dismiss");

		auto invalidDefault = ok;
		invalidDefault.defaultResult = Result::No;
		Check(!MessageBoxTest::Validate(invalidDefault), "invalid default rejected");
		auto invalidDismiss = yesNo;
		invalidDismiss.dismissEnabled = true;
		invalidDismiss.dismissResult = Result::No;
		Check(!MessageBoxTest::Validate(invalidDismiss),
			"Yes/No dismiss cannot alias No");
		auto validDismiss = yesNo;
		validDismiss.dismissEnabled = true;
		validDismiss.dismissResult = Result::Dismissed;
		validDismiss.showCloseButton = true;
		Check(MessageBoxTest::Validate(validDismiss),
			"explicit Yes/No dismissed result validates");

		auto criticalOwned = ok;
		criticalOwned.owner = reinterpret_cast<HWND>(1);
		criticalOwned.reliability = Reliability::CriticalNoWait;
		Check(!MessageBoxTest::Validate(criticalOwned),
			"critical owned request rejected");

		const auto okButtons = MessageBoxTest::ResolveButtonResults(Buttons::Ok);
		const auto cancelButtons = MessageBoxTest::ResolveButtonResults(Buttons::OkCancel);
		const auto yesNoButtons = MessageBoxTest::ResolveButtonResults(Buttons::YesNo);
		Check(okButtons.count == 1 && okButtons.first == Result::Ok,
			"OK slot mapping");
		Check(cancelButtons.count == 2 && cancelButtons.first == Result::Ok
			&& cancelButtons.second == Result::Cancel, "OK/Cancel slot mapping");
		Check(yesNoButtons.count == 2 && yesNoButtons.first == Result::Yes
			&& yesNoButtons.second == Result::No, "Yes/No slot mapping");

		Check(MessageBoxTest::MapSystemResult(Buttons::OkCancel, IDOK) == Result::Ok,
			"system OK mapping");
		Check(MessageBoxTest::MapSystemResult(Buttons::OkCancel, IDCANCEL)
			== Result::Cancel, "system Cancel mapping");
		Check(MessageBoxTest::MapSystemResult(Buttons::YesNo, IDYES) == Result::Yes,
			"system Yes mapping");
		Check(MessageBoxTest::MapSystemResult(Buttons::YesNo, IDNO) == Result::No,
			"system No mapping");
		Check(MessageBoxTest::MapSystemResult(Buttons::YesNo, IDCANCEL)
			== Result::Failed, "system Yes/No does not invent dismiss");
		Check(MessageBoxTest::MapSystemResult(Buttons::Ok, 0) == Result::Failed,
			"system fallback failure mapping");

		okCancel.fallback.modality = SystemModality::System;
		okCancel.fallback.icon = SystemIcon::Error;
		okCancel.defaultResult = Result::Cancel;
		const UINT flags = MessageBoxTest::ResolveFallbackFlags(okCancel);
		Check((flags & MB_TYPEMASK) == MB_OKCANCEL, "fallback button flags");
		Check((flags & MB_SYSTEMMODAL) != 0, "fallback system modal flag");
		Check((flags & MB_ICONERROR) != 0, "fallback error icon flag");
		Check((flags & MB_DEFMASK) == MB_DEFBUTTON2, "fallback default flag");

		auto invalidForFallback = ok;
		invalidForFallback.defaultResult = Result::No;
		invalidForFallback.fallback.owner = reinterpret_cast<HWND>(1);
		FallbackCapture invalidOwnerCapture;
		MessageBoxTest::Automation invalidOwnerAutomation;
		invalidOwnerAutomation.systemFallbackCallback = CaptureSystemFallback;
		invalidOwnerAutomation.fallbackContext = &invalidOwnerCapture;
		Check(MessageBoxTest::ShowAutomated(invalidForFallback,
			invalidOwnerAutomation) == Result::Ok,
			"invalid request uses mock system fallback");
		Check(invalidOwnerCapture.calls == 1 && !invalidOwnerCapture.owner,
			"invalid fallback owner normalizes to null");

		auto requiredOwner = MakeOkRequest(L"Owner", L"Owner is required here.");
		requiredOwner.requireOwner = true;
		FallbackCapture requiredOwnerCapture;
		MessageBoxTest::Automation requiredOwnerAutomation;
		requiredOwnerAutomation.systemFallbackCallback = CaptureSystemFallback;
		requiredOwnerAutomation.fallbackContext = &requiredOwnerCapture;
		Check(MessageBoxTest::ShowAutomated(requiredOwner,
			requiredOwnerAutomation) == Result::Ok
			&& requiredOwnerCapture.calls == 1,
			"required missing owner enters fallback before HWND creation");

		FallbackCapture reentryCapture;
		reentryCapture.reenter = true;
		MessageBoxTest::Automation reentryAutomation;
		reentryAutomation.systemFallbackCallback = CaptureSystemFallback;
		reentryAutomation.fallbackContext = &reentryCapture;
		Check(MessageBoxTest::ShowAutomated(invalidForFallback,
			reentryAutomation) == Result::Ok,
			"reentry outer fallback returns mock result");
		Check(reentryCapture.calls == 2
			&& reentryCapture.nestedResult == Result::Ok,
			"same-thread reentry bypasses the admission gate");
	}

	void TestLabelsAndScaling()
	{
		const auto simplified = MessageBoxTest::ResolveLabels(
			MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
		const auto traditional = MessageBoxTest::ResolveLabels(
			MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL));
		const auto english = MessageBoxTest::ResolveLabels(
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
		Check(simplified.ok == L"确定" && simplified.cancel == L"取消",
			"simplified labels");
		Check(traditional.ok == L"確定" && traditional.cancel == L"取消",
			"traditional labels");
		Check(english.ok == L"OK" && english.no == L"No", "English labels");
		Check(MessageBoxTest::ScaleDip(24, 96) == 24, "96 DPI scale");
		Check(MessageBoxTest::ScaleDip(24, 120) == 30, "120 DPI scale");
		Check(MessageBoxTest::ScaleDip(24, 144) == 36, "144 DPI scale");
		Check(MessageBoxTest::ScaleDip(24, 192) == 48, "192 DPI scale");
		Check(MessageBoxTest::ScaleDip(-24, 120) == -30,
			"negative DIP scale rounds symmetrically");
	}

	void TestPreflightWithoutWindow()
	{
		auto request = MakeOkCancelRequest(L"Save changes?",
			L"This action updates the current configuration and can be cancelled.");
		for (const UINT dpi : { 96u, 120u, 144u, 192u })
		{
			const auto probe = MessageBoxTest::ProbeLayout(request, dpi,
				MessageBoxTest::ScaleDip(900, dpi), MessageBoxTest::ScaleDip(900, dpi));
			Check(probe.succeeded, "layout succeeds at target DPI");
			Check(probe.width >= MessageBoxTest::ScaleDip(320, dpi)
				&& probe.width <= MessageBoxTest::ScaleDip(548, dpi)
				&& probe.height <= MessageBoxTest::ScaleDip(756, dpi),
				"layout remains in DIP bounds");
			Check(probe.buttonCount == 2, "layout keeps two buttons");
		}

		const auto tooSmall = MessageBoxTest::ProbeLayout(request, 96, 319, 800);
		Check(!tooSmall.succeeded, "work area below minimum rejects preflight");
		const auto minimumWidth = MessageBoxTest::ProbeLayout(request, 96, 320, 800);
		Check(minimumWidth.succeeded && minimumWidth.width == 320,
			"minimum work-area width remains usable");
		auto shortRequest = MakeOkRequest(L"Info", L"Done.");
		const auto shortProbe = MessageBoxTest::ProbeLayout(shortRequest, 96, 900, 900);
		Check(shortProbe.succeeded && shortProbe.width == 320,
			"short content remains at the minimum Fluent width");
		std::wstring wrappingBody(120, L'W');
		wrappingBody[20] = L' ';
		wrappingBody[40] = L' ';
		wrappingBody[60] = L' ';
		wrappingBody[80] = L' ';
		wrappingBody[100] = L' ';
		auto wrappingRequest = MakeOkRequest(L"Info", wrappingBody.c_str());
		const auto wrappingProbe = MessageBoxTest::ProbeLayout(
			wrappingRequest, 96, 900, 900);
		Check(wrappingProbe.succeeded && wrappingProbe.width > shortProbe.width
			&& wrappingProbe.width <= 548,
			"wrapping benefit expands content width up to the Fluent maximum");
		Check(!MessageBoxTest::ProbeLayout(request, UINT_MAX, 900, 900).succeeded,
			"unsupported DPI fails before coordinate arithmetic");

		auto twoLineTitle = MakeOkRequest(L"First line\nSecond line",
			L"正文与 English body can share the same measured surface.");
		Check(MessageBoxTest::ProbeLayout(twoLineTitle, 96, 900, 900).succeeded,
			"two-line title and mixed body fit");

		auto threeLineTitle = MakeOkRequest(L"First\nSecond\nThird", L"Body");
		Check(!MessageBoxTest::ProbeLayout(threeLineTitle, 96, 900, 900).succeeded,
			"third title line rejects preflight");

		std::wstring unbreakable(600, L'A');
		auto wideWord = MakeOkRequest(L"Title", unbreakable.c_str());
		Check(!MessageBoxTest::ProbeLayout(wideWord, 96, 900, 900).succeeded,
			"unbreakable word rejects preflight");
		std::wstring mixedWideWord = L"中" + std::wstring(600, L'A');
		auto mixedWide = MakeOkRequest(L"Title", mixedWideWord.c_str());
		Check(!MessageBoxTest::ProbeLayout(mixedWide, 96, 900, 900).succeeded,
			"CJK prefix does not hide an unbreakable ASCII word");

		std::wstring longBody;
		for (int index = 0; index < 500; ++index) longBody += L"complete message line ";
		auto overflow = MakeOkRequest(L"Title", longBody.c_str());
		Check(!MessageBoxTest::ProbeLayout(overflow, 96, 900, 900).succeeded,
			"overflow body rejects preflight");

		auto builtInIcon = request;
		builtInIcon.icon = IconSource::BuiltInError();
		const auto iconProbe = MessageBoxTest::ProbeLayout(builtInIcon, 96, 900, 900);
		Check(iconProbe.succeeded && iconProbe.hasIcon,
			"test executable decodes built-in PNG resource");

		std::uint8_t invalidPixel = 0;
		auto invalidIcon = request;
		invalidIcon.icon = IconSource::FromPremultipliedBgra(
			&invalidPixel, 2, 2, 1);
		const auto invalidProbe = MessageBoxTest::ProbeLayout(invalidIcon, 96, 900, 900);
		Check(invalidProbe.succeeded && !invalidProbe.hasIcon,
			"invalid decoration degrades to no icon");

		const std::uint8_t transparentPixels[16]{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
		};
		auto pixelIcon = request;
		pixelIcon.icon = IconSource::FromPremultipliedBgra(
			transparentPixels, 2, 2, 8);
		const auto pixelProbe = MessageBoxTest::ProbeLayout(pixelIcon, 96, 900, 900);
		Check(pixelProbe.succeeded && pixelProbe.hasIcon,
			"owned premultiplied BGRA icon is accepted");

		auto oversizedIcon = request;
		oversizedIcon.icon = IconSource::FromPremultipliedBgra(
			&invalidPixel, 513, 1, 2052);
		const auto oversizedProbe = MessageBoxTest::ProbeLayout(
			oversizedIcon, 96, 900, 900);
		Check(oversizedProbe.succeeded && !oversizedProbe.hasIcon,
			"oversized icon is omitted before reading pixels");

		const HRSRC resource = FindResourceW(GetModuleHandleW(nullptr),
			MAKEINTRESOURCEW(IDR_MESSAGE_BOX_ERROR), L"PNG");
		Check(resource != nullptr && SizeofResource(GetModuleHandleW(nullptr), resource) > 8,
			"test PNG resource is embedded");
	}

	LRESULT CALLBACK BackdropWindowProc(HWND hwnd, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_PAINT)
		{
			PAINTSTRUCT paint{};
			HDC dc = BeginPaint(hwnd, &paint);
			const HBRUSH brush = CreateSolidBrush(RGB(18, 18, 18));
			FillRect(dc, &paint.rcPaint, brush);
			DeleteObject(brush);
			EndPaint(hwnd, &paint);
			return 0;
		}
		if (message == WM_ERASEBKGND) return 1;
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	class BackdropWindow
	{
	public:
		[[nodiscard]] bool Create(bool topmost = true)
		{
			instance_ = GetModuleHandleW(nullptr);
			className_ = L"Inkeys.MessageBox.TestBackdrop."
				+ std::to_wstring(GetCurrentProcessId());
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = BackdropWindowProc;
			windowClass.hInstance = instance_;
			windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.lpszClassName = className_.c_str();
			if (!RegisterClassExW(&windowClass)
				&& GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

			const int width = 920;
			const int height = 700;
			const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
			const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
			const DWORD exStyle = WS_EX_TOOLWINDOW
				| (topmost ? WS_EX_TOPMOST : 0);
			hwnd_ = CreateWindowExW(exStyle,
				className_.c_str(), L"MessageBox visual test backdrop", WS_POPUP,
				(screenWidth - width) / 2, (screenHeight - height) / 2,
				width, height, nullptr, nullptr, instance_, nullptr);
			if (!hwnd_) return false;
			ShowWindow(hwnd_, SW_SHOWNORMAL);
			UpdateWindow(hwnd_);
			SetForegroundWindow(hwnd_);
			return true;
		}

		~BackdropWindow()
		{
			if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
			if (instance_ && !className_.empty())
				UnregisterClassW(className_.c_str(), instance_);
		}

		[[nodiscard]] HWND Get() const noexcept { return hwnd_; }

	private:
		HINSTANCE instance_ = nullptr;
		HWND hwnd_ = nullptr;
		std::wstring className_;
	};

	UINT ResolveTestDpi(HWND hwnd) noexcept
	{
		using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
		const HMODULE user32 = GetModuleHandleW(L"user32.dll");
		const auto getDpiForWindow = user32
			? reinterpret_cast<GetDpiForWindowFn>(
				GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
		const UINT dpi = getDpiForWindow ? getDpiForWindow(hwnd) : 96;
		return dpi ? dpi : 96;
	}

	bool IsResizeHit(LRESULT hit) noexcept
	{
		return hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM
			|| hit == HTTOPLEFT || hit == HTTOPRIGHT
			|| hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT;
	}

	BOOL CALLBACK FindLiveMessageBox(HWND hwnd, LPARAM parameter) noexcept
	{
		DWORD processId = 0;
		(void)GetWindowThreadProcessId(hwnd, &processId);
		if (processId != GetCurrentProcessId()) return TRUE;
		wchar_t className[160]{};
		if (GetClassNameW(hwnd, className, static_cast<int>(_countof(className))) > 0
			&& std::wstring_view(className).starts_with(L"Inkeys.FluentMessageBox."))
		{
			*reinterpret_cast<bool*>(parameter) = true;
			return FALSE;
		}
		return TRUE;
	}

	bool HasLiveMessageBoxWindow() noexcept
	{
		bool found = false;
		(void)EnumWindows(FindLiveMessageBox, reinterpret_cast<LPARAM>(&found));
		return found;
	}

	struct HiddenWindowContext
	{
		HWND owner = nullptr;
		HWND dialog = nullptr;
		bool styleValid = false;
		bool ownerDisabled = false;
		bool noResizeHit = false;
		bool fixedTrackSize = false;
		bool escapeStayedOpen = false;
		bool selectSecondary = false;
		bool dismissWithEscape = false;
		bool dismissWithAltF4 = false;
		bool clickClose = false;
		bool dismissThenEnter = false;
		bool exerciseDpi = false;
		bool expectOwnerlessTopmost = false;
		bool systemCommandsBlocked = false;
		bool captionDoubleClickBlocked = false;
		bool captionDragHit = false;
		bool dwmFrameConfigured = false;
		bool dpiRebuilt = false;
		bool closeCaptured = false;
		bool closeVisibleAfterRelease = false;
		UINT closeDpi = 0;
		RECT closeClient{};
		POINT closePoint{};
		LONG_PTR observedStyle = 0;
		LONG_PTR observedExStyle = 0;
		HWND observedOwner = nullptr;
	};

	void InspectAndClose(HWND hwnd, void* opaque) noexcept
	{
		auto& context = *static_cast<HiddenWindowContext*>(opaque);
		context.dialog = hwnd;
		const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
		const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		context.observedStyle = style;
		context.observedExStyle = exStyle;
		context.observedOwner = GetWindow(hwnd, GW_OWNER);
		context.styleValid = (style & (WS_CAPTION | WS_SYSMENU | WS_THICKFRAME))
			== (WS_CAPTION | WS_SYSMENU | WS_THICKFRAME)
			&& (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) == 0
			&& (exStyle & WS_EX_TOOLWINDOW) != 0;
		if (context.expectOwnerlessTopmost)
			context.styleValid &= GetWindow(hwnd, GW_OWNER) == nullptr
				&& (exStyle & WS_EX_TOPMOST) != 0;
		else
			context.styleValid &= GetWindow(hwnd, GW_OWNER) == context.owner
				&& (exStyle & WS_EX_TOPMOST) == 0;
		context.ownerDisabled = !context.owner || !IsWindowEnabled(context.owner);

		RECT bounds{};
		GetWindowRect(hwnd, &bounds);
		const UINT windowDpi = ResolveTestDpi(hwnd);
		const POINT captionPoint{
			bounds.left + MessageBoxTest::ScaleDip(24, windowDpi),
			bounds.top + MessageBoxTest::ScaleDip(18, windowDpi),
		};
		context.captionDragHit = SendMessageW(hwnd, WM_NCHITTEST, 0,
			MAKELPARAM(captionPoint.x, captionPoint.y)) == HTCAPTION;
		BOOL darkMode = FALSE;
		int cornerPreference = 0;
		HRESULT darkResult = DwmGetWindowAttribute(hwnd, 20,
			&darkMode, sizeof(darkMode));
		if (FAILED(darkResult)) darkResult = DwmGetWindowAttribute(hwnd, 19,
			&darkMode, sizeof(darkMode));
		context.dwmFrameConfigured = SUCCEEDED(darkResult) && darkMode
			&& SUCCEEDED(DwmGetWindowAttribute(hwnd, 33, &cornerPreference,
				sizeof(cornerPreference)))
			&& cornerPreference == 2;
		const POINT hitPoints[]{
			{ bounds.left + 1, (bounds.top + bounds.bottom) / 2 },
			{ bounds.right - 2, (bounds.top + bounds.bottom) / 2 },
			{ (bounds.left + bounds.right) / 2, bounds.top + 1 },
			{ (bounds.left + bounds.right) / 2, bounds.bottom - 2 },
			{ bounds.left + 1, bounds.top + 1 },
			{ bounds.right - 2, bounds.top + 1 },
			{ bounds.left + 1, bounds.bottom - 2 },
			{ bounds.right - 2, bounds.bottom - 2 },
		};
		context.noResizeHit = true;
		for (const POINT point : hitPoints)
		{
			const LRESULT hit = SendMessageW(hwnd, WM_NCHITTEST, 0,
				MAKELPARAM(point.x, point.y));
			context.noResizeHit &= !IsResizeHit(hit);
		}
		MINMAXINFO minMax{};
		SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&minMax));
		context.fixedTrackSize = minMax.ptMinTrackSize.x == minMax.ptMaxTrackSize.x
			&& minMax.ptMinTrackSize.y == minMax.ptMaxTrackSize.y
			&& minMax.ptMinTrackSize.x > 0 && minMax.ptMinTrackSize.y > 0;

		const RECT beforeCommands = bounds;
		SendMessageW(hwnd, WM_SYSCOMMAND, SC_SIZE | WMSZ_LEFT, 0);
		SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
		RECT afterCommands{};
		GetWindowRect(hwnd, &afterCommands);
		context.systemCommandsBlocked = EqualRect(&beforeCommands, &afterCommands) != FALSE
			&& !IsIconic(hwnd) && !IsZoomed(hwnd);
		SendMessageW(hwnd, WM_NCLBUTTONDBLCLK, HTCAPTION,
			MAKELPARAM((bounds.left + bounds.right) / 2, bounds.top + 20));
		RECT afterDoubleClick{};
		GetWindowRect(hwnd, &afterDoubleClick);
		context.captionDoubleClickBlocked = EqualRect(
			&beforeCommands, &afterDoubleClick) != FALSE;

		if (context.exerciseDpi)
		{
			const int oldWidth = bounds.right - bounds.left;
			const int oldHeight = bounds.bottom - bounds.top;
			const UINT currentDpi = ResolveTestDpi(hwnd);
			const UINT targetDpi = currentDpi >= 144 ? 96 : 192;
			RECT suggested = bounds;
			SendMessageW(hwnd, WM_DPICHANGED,
				MAKEWPARAM(targetDpi, targetDpi),
				reinterpret_cast<LPARAM>(&suggested));
			RECT rebuiltBounds{};
			GetWindowRect(hwnd, &rebuiltBounds);
			MINMAXINFO rebuiltMinMax{};
			SendMessageW(hwnd, WM_GETMINMAXINFO, 0,
				reinterpret_cast<LPARAM>(&rebuiltMinMax));
			const int newWidth = rebuiltBounds.right - rebuiltBounds.left;
			const int newHeight = rebuiltBounds.bottom - rebuiltBounds.top;
			context.dpiRebuilt = (newWidth != oldWidth || newHeight != oldHeight)
				&& rebuiltMinMax.ptMinTrackSize.x == newWidth
				&& rebuiltMinMax.ptMaxTrackSize.x == newWidth
				&& rebuiltMinMax.ptMinTrackSize.y == newHeight
				&& rebuiltMinMax.ptMaxTrackSize.y == newHeight;
		}

		if (context.dismissWithEscape)
		{
			PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
			return;
		}
		if (context.dismissWithAltF4)
		{
			PostMessageW(hwnd, WM_SYSKEYDOWN, VK_F4, 0);
			return;
		}
		if (context.clickClose)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const UINT dpi = ResolveTestDpi(hwnd);
			const POINT closePoint{
				client.right - MessageBoxTest::ScaleDip(40, dpi),
				MessageBoxTest::ScaleDip(40, dpi),
			};
			context.closeDpi = dpi;
			context.closeClient = client;
			context.closePoint = closePoint;
			SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON,
				MAKELPARAM(closePoint.x, closePoint.y));
			context.closeCaptured = GetCapture() == hwnd;
			SendMessageW(hwnd, WM_LBUTTONUP, 0,
				MAKELPARAM(closePoint.x, closePoint.y));
			context.closeVisibleAfterRelease = IsWindowVisible(hwnd) != FALSE;
			return;
		}
		if (context.dismissThenEnter)
		{
			SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
			SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
			return;
		}
		if (context.selectSecondary)
		{
			SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
			context.escapeStayedOpen = IsWindowVisible(hwnd) != FALSE;
			SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0);
			PostMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);
			return;
		}
		PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
	}

	void TestHiddenWindowIntegration()
	{
		BackdropWindow backdrop;
		Check(backdrop.Create(false), "hidden-test backdrop created");
		if (!backdrop.Get()) return;

		auto ok = MakeOkRequest(L"Title", L"Body text for the owned dialog.");
		ok.owner = backdrop.Get();
		HiddenWindowContext okContext{ backdrop.Get() };
		okContext.exerciseDpi = true;
		const Result okResult = MessageBoxTest::ShowAutomated(ok,
			{ InspectAndClose, &okContext, 40 });
		Check(okResult == Result::Ok, "owned OK returns result");
		if (!okContext.styleValid)
		{
			std::cerr << "observed style=0x" << std::hex << okContext.observedStyle
				<< " exStyle=0x" << okContext.observedExStyle
				<< " owner=0x" << reinterpret_cast<std::uintptr_t>(okContext.observedOwner)
				<< " expectedOwner=0x" << reinterpret_cast<std::uintptr_t>(backdrop.Get())
				<< std::dec << '\n';
		}
		Check(okContext.styleValid, "owned fixed frame styles");
		Check(okContext.ownerDisabled, "owner disabled while visible");
		Check(okContext.noResizeHit && okContext.fixedTrackSize,
			"resize paths are blocked");
		Check(okContext.captionDragHit, "custom title returns the draggable caption hit");
		Check(okContext.dwmFrameConfigured,
			"Windows 11 DWM dark frame and round-corner attributes are applied");
		Check(okContext.systemCommandsBlocked
			&& okContext.captionDoubleClickBlocked,
			"system resize commands and caption double-click are blocked");
		Check(okContext.dpiRebuilt, "DPI change rebuilds fixed layout and DIB");
		Check(IsWindowEnabled(backdrop.Get()) != FALSE,
			"owner enabled state restored");
		Check(okContext.dialog && !IsWindow(okContext.dialog),
			"dialog HWND destroyed after hide");
		// 首次 HWND/GDI+ 使用会触发系统进程级缓存；其后检查逐次显示是否增长。
		const DWORD initialGdiObjects = GetGuiResources(
			GetCurrentProcess(), GR_GDIOBJECTS);
		const DWORD initialUserObjects = GetGuiResources(
			GetCurrentProcess(), GR_USEROBJECTS);

		auto yesNo = MakeYesNoRequest(L"Continue?", L"Choose Yes or No.");
		yesNo.owner = backdrop.Get();
		HiddenWindowContext yesNoContext{ backdrop.Get() };
		yesNoContext.selectSecondary = true;
		const Result noResult = MessageBoxTest::ShowAutomated(yesNo,
			{ InspectAndClose, &yesNoContext, 40 });
		Check(yesNoContext.escapeStayedOpen, "Yes/No ignores Escape by default");
		Check(noResult == Result::No, "Tab and Space activate No");

		auto okCancel = MakeOkCancelRequest(L"Restart?", L"Escape cancels this action.");
		okCancel.owner = backdrop.Get();
		HiddenWindowContext cancelContext{ backdrop.Get() };
		cancelContext.dismissWithEscape = true;
		const Result cancelResult = MessageBoxTest::ShowAutomated(okCancel,
			{ InspectAndClose, &cancelContext, 40 });
		Check(cancelResult == Result::Cancel, "Escape returns Cancel");

		auto altDismiss = MakeYesNoRequest(L"Dismiss?", L"Alt+F4 is explicit dismiss.");
		altDismiss.owner = backdrop.Get();
		altDismiss.dismissEnabled = true;
		altDismiss.dismissResult = Result::Dismissed;
		altDismiss.showCloseButton = false;
		HiddenWindowContext altContext{ backdrop.Get() };
		altContext.dismissWithAltF4 = true;
		Check(MessageBoxTest::ShowAutomated(altDismiss,
			{ InspectAndClose, &altContext, 40 }) == Result::Dismissed,
			"Alt+F4 uses explicit dismissed result without close glyph");

		auto closeRequest = MakeOkCancelRequest(L"Close?", L"Use the title close command.");
		closeRequest.owner = backdrop.Get();
		HiddenWindowContext closeContext{ backdrop.Get() };
		closeContext.clickClose = true;
		const Result closeResult = MessageBoxTest::ShowAutomated(closeRequest,
			{ InspectAndClose, &closeContext, 40 });
		if (closeResult != Result::Cancel)
			std::cerr << "message_box_test close: result="
				<< static_cast<int>(closeResult) << " dpi=" << closeContext.closeDpi
				<< " client=" << closeContext.closeClient.right << 'x'
				<< closeContext.closeClient.bottom << " point=("
				<< closeContext.closePoint.x << ',' << closeContext.closePoint.y
				<< ") captured=" << closeContext.closeCaptured
				<< " visibleAfterRelease=" << closeContext.closeVisibleAfterRelease << '\n';
		Check(closeResult == Result::Cancel,
			"custom title close uses the dismiss result");

		auto singleCommit = MakeOkCancelRequest(L"One result", L"Dismiss wins once.");
		singleCommit.owner = backdrop.Get();
		HiddenWindowContext singleCommitContext{ backdrop.Get() };
		singleCommitContext.dismissThenEnter = true;
		Check(MessageBoxTest::ShowAutomated(singleCommit,
			{ InspectAndClose, &singleCommitContext, 40 }) == Result::Cancel,
			"first committed result cannot be overwritten");

		auto secondaryDefault = MakeOkCancelRequest(
			L"Default", L"Enter activates the configured default.");
		secondaryDefault.owner = backdrop.Get();
		secondaryDefault.defaultResult = Result::Cancel;
		HiddenWindowContext secondaryDefaultContext{ backdrop.Get() };
		Check(MessageBoxTest::ShowAutomated(secondaryDefault,
			{ InspectAndClose, &secondaryDefaultContext, 40 }) == Result::Cancel,
			"Enter activates a configured secondary default");

		auto invalidDecoration = MakeOkRequest(L"Missing icon",
			L"The message still renders when PNG lookup fails.");
		invalidDecoration.owner = backdrop.Get();
		invalidDecoration.icon = IconSource::FromPngResource(GetModuleHandleW(nullptr),
			L"PNG", MAKEINTRESOURCEW(65500));
		HiddenWindowContext invalidContext{ backdrop.Get() };
		Check(MessageBoxTest::ShowAutomated(invalidDecoration,
			{ InspectAndClose, &invalidContext, 40 }) == Result::Ok,
			"invalid resource omits icon without system fallback");

		auto errorIcon = MakeOkRequest(L"Error", L"Decoded icon closes cleanly.");
		errorIcon.owner = backdrop.Get();
		errorIcon.icon = IconSource::BuiltInError();
		HiddenWindowContext errorIconContext{ backdrop.Get() };
		Check(MessageBoxTest::ShowAutomated(errorIcon,
			{ InspectAndClose, &errorIconContext, 40 }) == Result::Ok,
			"decoded PNG is destroyed before the private GDI+ token");

		auto ownerless = MakeOkRequest(L"Startup warning", L"Ownerless fixed-topmost dialog.");
		ownerless.ownerlessTopmostAtCreation = true;
		ownerless.fallback.modality = SystemModality::System;
		HiddenWindowContext ownerlessContext{};
		ownerlessContext.expectOwnerlessTopmost = true;
		Check(MessageBoxTest::ShowAutomated(ownerless,
			{ InspectAndClose, &ownerlessContext, 40 }) == Result::Ok,
			"ownerless topmost dialog returns OK");
		Check(ownerlessContext.styleValid, "ownerless topmost applied at creation");

		std::wstring overflowBody;
		for (int index = 0; index < 500; ++index)
			overflowBody += L"complete fallback payload ";
		auto overflowRequest = MakeOkRequest(L"Overflow", overflowBody.c_str());
		overflowRequest.owner = backdrop.Get();
		overflowRequest.fallback.owner = backdrop.Get();
		FallbackCapture overflowCapture;
		MessageBoxTest::Automation overflowAutomation;
		overflowAutomation.systemFallbackCallback = CaptureSystemFallback;
		overflowAutomation.fallbackContext = &overflowCapture;
		Check(MessageBoxTest::ShowAutomated(overflowRequest,
			overflowAutomation) == Result::Ok,
			"overflow preflight uses system fallback decision");
		Check(overflowCapture.calls == 1
			&& overflowCapture.owner == backdrop.Get()
			&& overflowCapture.bodyLength == overflowBody.size()
			&& overflowCapture.ownerEnabled,
			"overflow fallback preserves text and precedes owner disable");

		const DWORD finalGdiObjects = GetGuiResources(
			GetCurrentProcess(), GR_GDIOBJECTS);
		const DWORD finalUserObjects = GetGuiResources(
			GetCurrentProcess(), GR_USEROBJECTS);
		if (finalGdiObjects > initialGdiObjects + 2)
			std::cerr << "message_box_test GDI baseline: initial="
				<< initialGdiObjects << " final=" << finalGdiObjects << '\n';
		if (finalUserObjects > initialUserObjects + 2)
			std::cerr << "message_box_test USER baseline: initial="
				<< initialUserObjects << " final=" << finalUserObjects << '\n';
		Check(finalGdiObjects <= initialGdiObjects + 2,
			"GDI objects return to the per-process baseline");
		Check(finalUserObjects <= initialUserObjects + 2,
			"USER objects return to the per-process baseline");
	}

	struct BlockingDialogContext
	{
		HANDLE visibleEvent = nullptr;
		HANDLE releaseEvent = nullptr;
	};

	void BlockThenClose(HWND hwnd, void* opaque) noexcept
	{
		auto& context = *static_cast<BlockingDialogContext*>(opaque);
		SetEvent(context.visibleEvent);
		(void)WaitForSingleObject(context.releaseEvent, 5000);
		PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
	}

	void MarkThenClose(HWND hwnd, void* opaque) noexcept
	{
		static_cast<std::atomic_bool*>(opaque)->store(true,
			std::memory_order_release);
		PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
	}

	void TestAdmissionAndCriticalFallback()
	{
		HANDLE visibleEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE releaseEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		Check(visibleEvent && releaseEvent, "concurrency test events created");
		if (!visibleEvent || !releaseEvent)
		{
			if (visibleEvent) CloseHandle(visibleEvent);
			if (releaseEvent) CloseHandle(releaseEvent);
			return;
		}

		BlockingDialogContext blocking{ visibleEvent, releaseEvent };
		FallbackCapture firstFallback;
		MessageBoxTest::Automation firstAutomation;
		firstAutomation.visibleCallback = BlockThenClose;
		firstAutomation.context = &blocking;
		firstAutomation.delayMilliseconds = 40;
		firstAutomation.systemFallbackCallback = CaptureSystemFallback;
		firstAutomation.fallbackContext = &firstFallback;
		std::atomic<Result> firstResult{ Result::Failed };
		std::thread first([&]()
			{
				auto request = MakeOkRequest(L"First", L"Hold the admission gate.");
				firstResult.store(MessageBoxTest::ShowAutomated(request,
					firstAutomation), std::memory_order_release);
			});

		const bool firstVisible = WaitForSingleObject(visibleEvent, 5000)
			== WAIT_OBJECT_0;
		Check(firstVisible, "first normal dialog becomes visible");
		std::atomic_bool secondVisible = false;
		std::atomic<Result> secondResult{ Result::Failed };
		FallbackCapture secondFallback;
		std::thread second;
		if (firstVisible)
		{
			second = std::thread([&]()
				{
					MessageBoxTest::Automation automation;
					automation.visibleCallback = MarkThenClose;
					automation.context = &secondVisible;
					automation.delayMilliseconds = 40;
					automation.systemFallbackCallback = CaptureSystemFallback;
					automation.fallbackContext = &secondFallback;
					auto request = MakeOkRequest(L"Second", L"Wait for the first dialog.");
					secondResult.store(MessageBoxTest::ShowAutomated(request,
						automation), std::memory_order_release);
				});
			Sleep(80);
			Check(!secondVisible.load(std::memory_order_acquire),
				"second normal dialog waits at the admission gate");

			FallbackCapture criticalFallback;
			MessageBoxTest::Automation criticalAutomation;
			criticalAutomation.systemFallbackCallback = CaptureSystemFallback;
			criticalAutomation.fallbackContext = &criticalFallback;
			auto critical = MakeOkRequest(L"Critical", L"Do not wait.");
			critical.reliability = Reliability::CriticalNoWait;
			const auto started = std::chrono::steady_clock::now();
			const Result criticalResult = MessageBoxTest::ShowAutomated(
				critical, criticalAutomation);
			const auto elapsed = std::chrono::steady_clock::now() - started;
			Check(criticalResult == Result::Ok && criticalFallback.calls == 1,
				"critical request bypasses a busy custom dialog");
			Check(elapsed < std::chrono::milliseconds(500),
				"critical busy fallback does not wait");
		}

		SetEvent(releaseEvent);
		if (first.joinable()) first.join();
		if (second.joinable()) second.join();
		Check(firstResult.load(std::memory_order_acquire) == Result::Ok
			&& firstFallback.calls == 0,
			"first normal dialog completes without fallback");
		if (firstVisible)
		{
			Check(secondVisible.load(std::memory_order_acquire)
				&& secondResult.load(std::memory_order_acquire) == Result::Ok
				&& secondFallback.calls == 0,
				"second normal dialog starts after the first completes");
		}
		CloseHandle(visibleEvent);
		CloseHandle(releaseEvent);
	}

	int GetEncoderClsid(const WCHAR* mimeType, CLSID* output)
	{
		UINT count = 0;
		UINT bytes = 0;
		if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok
			|| bytes == 0) return -1;
		std::vector<std::uint8_t> storage(bytes);
		auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
		if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return -1;
		for (UINT index = 0; index < count; ++index)
		{
			if (wcscmp(encoders[index].MimeType, mimeType) == 0)
			{
				*output = encoders[index].Clsid;
				return static_cast<int>(index);
			}
		}
		return -1;
	}

	enum class VisualInteraction
	{
		Default,
		FocusSecondary,
		HoverPrimary,
		PressSecondary,
	};

	struct VisualCaptureContext
	{
		std::filesystem::path path;
		HWND backdrop = nullptr;
		HWND dialog = nullptr;
		VisualInteraction interaction = VisualInteraction::Default;
		bool requireErrorColor = false;
		bool saved = false;
		bool pixelsValid = false;
		int capturedWidth = 0;
		int capturedHeight = 0;
		std::size_t accentPixels = 0;
		std::size_t darkPixels = 0;
		std::size_t errorPixels = 0;
		std::size_t hoverAccentPixels = 0;
		std::size_t pressedNeutralPixels = 0;
		bool usedPrintWindow = false;
	};

	void CaptureWindowRegion(HWND hwnd, VisualCaptureContext& context) noexcept
	{
		RECT dialogBounds{};
		RECT backdropBounds{};
		if (!GetWindowRect(hwnd, &dialogBounds)
			|| !GetWindowRect(context.backdrop, &backdropBounds)) return;
		RECT bounds = dialogBounds;
		InflateRect(&bounds, 32, 32);
		IntersectRect(&bounds, &bounds, &backdropBounds);
		const int width = bounds.right - bounds.left;
		const int height = bounds.bottom - bounds.top;
		if (width <= 0 || height <= 0) return;
		context.capturedWidth = width;
		context.capturedHeight = height;

		HDC screen = GetDC(nullptr);
		HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP bitmap = memory ? CreateDIBSection(memory, &info,
			DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
		HGDIOBJ old = bitmap ? SelectObject(memory, bitmap) : nullptr;
		bool copied = screen && memory && bitmap && bits
			&& BitBlt(memory, 0, 0, width, height, screen,
				bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);

		// 某些自动化桌面只能从 screen DC 读到壁纸；四角应是测试背景色。
		bool capturedBackdrop = false;
		if (copied)
		{
			std::size_t backdropPixels = 0;
			const auto pixels = static_cast<const std::uint8_t*>(bits);
			for (int y = 0; y < 8; ++y)
			{
				for (int x = 0; x < 8; ++x)
				{
					const int sampleX[4]{ x, width - 1 - x, x, width - 1 - x };
					const int sampleY[4]{ y, y, height - 1 - y, height - 1 - y };
					for (int corner = 0; corner < 4; ++corner)
					{
						const std::size_t index = (static_cast<std::size_t>(sampleY[corner])
							* width + sampleX[corner]) * 4;
						backdropPixels += pixels[index] >= 14 && pixels[index] <= 24
							&& pixels[index + 1] >= 14 && pixels[index + 1] <= 24
							&& pixels[index + 2] >= 14 && pixels[index + 2] <= 24;
					}
				}
			}
			capturedBackdrop = backdropPixels >= 192;
		}
		if (!capturedBackdrop && memory && bitmap && bits)
		{
			RECT fillBounds{ 0, 0, width, height };
			const HBRUSH background = CreateSolidBrush(RGB(18, 18, 18));
			if (background)
			{
				FillRect(memory, &fillBounds, background);
				DeleteObject(background);
			}
			POINT previousOrigin{};
			const int offsetX = dialogBounds.left - bounds.left;
			const int offsetY = dialogBounds.top - bounds.top;
			SetViewportOrgEx(memory, offsetX, offsetY, &previousOrigin);
			copied = PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT) != FALSE;
			SetViewportOrgEx(memory, previousOrigin.x, previousOrigin.y, nullptr);
			context.usedPrintWindow = copied;
		}

		if (copied)
		{
			const auto pixels = static_cast<const std::uint8_t*>(bits);
			for (std::size_t index = 0;
				index < static_cast<std::size_t>(width) * height; ++index)
			{
				const std::uint8_t blue = pixels[index * 4 + 0];
				const std::uint8_t green = pixels[index * 4 + 1];
				const std::uint8_t red = pixels[index * 4 + 2];
				context.accentPixels += blue > 220 && green > 165 && green < 235
					&& red > 45 && red < 145;
				context.darkPixels += red >= 15 && red <= 75
					&& green >= 15 && green <= 75
					&& blue >= 15 && blue <= 75;
				context.errorPixels += red > 210 && green >= 70 && green <= 150
					&& blue >= 70 && blue <= 160;
				context.hoverAccentPixels += red >= 108 && red <= 126
					&& green >= 207 && green <= 222
					&& blue >= 248;
				context.pressedNeutralPixels += red >= 35 && red <= 41
					&& green >= 35 && green <= 41
					&& blue >= 35 && blue <= 41;
			}
			context.pixelsValid = context.accentPixels > 150
				&& context.darkPixels > 5000
				&& (!context.requireErrorColor || context.errorPixels > 150)
				&& (context.interaction != VisualInteraction::HoverPrimary
					|| context.hoverAccentPixels > 500)
				&& (context.interaction != VisualInteraction::PressSecondary
					|| context.pressedNeutralPixels > 500);

			Gdiplus::GdiplusStartupInput startupInput;
			ULONG_PTR token = 0;
			if (Gdiplus::GdiplusStartup(&token,
				&startupInput, nullptr) == Gdiplus::Ok)
			{
				{
					CLSID png{};
					Gdiplus::Bitmap captured(width, height, width * 4,
						PixelFormat32bppRGB, static_cast<BYTE*>(bits));
					context.saved = captured.GetLastStatus() == Gdiplus::Ok
						&& GetEncoderClsid(L"image/png", &png) >= 0
						&& captured.Save(context.path.c_str(), &png, nullptr)
							== Gdiplus::Ok;
				}
				Gdiplus::GdiplusShutdown(token);
			}
		}

		if (old && old != HGDI_ERROR) SelectObject(memory, old);
		if (bitmap) DeleteObject(bitmap);
		if (memory) DeleteDC(memory);
		if (screen) ReleaseDC(nullptr, screen);
	}

	void PrintCaptureFailure(const char* name,
		const VisualCaptureContext& context)
	{
		if (context.saved && context.pixelsValid) return;
		std::cerr << name << " capture: saved=" << context.saved
			<< " pixelsValid=" << context.pixelsValid
			<< " size=" << context.capturedWidth << 'x' << context.capturedHeight
			<< " accent=" << context.accentPixels
			<< " dark=" << context.darkPixels
			<< " error=" << context.errorPixels
			<< " hoverAccent=" << context.hoverAccentPixels
			<< " pressedNeutral=" << context.pressedNeutralPixels
			<< " source=" << (context.usedPrintWindow ? "PrintWindow" : "screen")
			<< " path=" << context.path.string() << '\n';
	}

	void CaptureAndClose(HWND hwnd, void* opaque) noexcept
	{
		auto& context = *static_cast<VisualCaptureContext*>(opaque);
		context.dialog = hwnd;
		RECT client{};
		GetClientRect(hwnd, &client);
		const UINT dpi = ResolveTestDpi(hwnd);
		const int y = client.bottom - MessageBoxTest::ScaleDip(40, dpi);
		POINT interactionPoint{};
		if (context.interaction == VisualInteraction::FocusSecondary)
		{
			SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0);
		}
		else if (context.interaction == VisualInteraction::HoverPrimary)
		{
			interactionPoint = { client.right / 2, y };
			SendMessageW(hwnd, WM_MOUSEMOVE, 0,
				MAKELPARAM(interactionPoint.x, interactionPoint.y));
		}
		else if (context.interaction == VisualInteraction::PressSecondary)
		{
			interactionPoint = { client.right * 3 / 4, y };
			SendMessageW(hwnd, WM_MOUSEMOVE, 0,
				MAKELPARAM(interactionPoint.x, interactionPoint.y));
			SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON,
				MAKELPARAM(interactionPoint.x, interactionPoint.y));
		}
		RedrawWindow(hwnd, nullptr, nullptr,
			RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
		(void)DwmFlush();
		Sleep(60);
		CaptureWindowRegion(hwnd, context);
		if (context.interaction == VisualInteraction::PressSecondary)
			PostMessageW(hwnd, WM_LBUTTONUP, 0,
				MAKELPARAM(interactionPoint.x, interactionPoint.y));
		else PostMessageW(hwnd, WM_KEYDOWN,
			context.interaction == VisualInteraction::FocusSecondary
				? VK_SPACE : VK_RETURN, 0);
	}
}

int RunMessageBoxTests(bool runWindowTests)
{
	const int before = failures;
	TestRequestAndResultContracts();
	TestLabelsAndScaling();
	TestPreflightWithoutWindow();
	if (runWindowTests)
	{
		TestHiddenWindowIntegration();
		TestAdmissionAndCriticalFallback();
		Check(!HasLiveMessageBoxWindow(),
			"all MessageBox HWND instances are destroyed after use");
	}
	return failures - before;
}

int RunMessageBoxVisualTests(const char* outputDirectory)
{
	failures = 0;
	std::error_code error;
	const std::filesystem::path output = outputDirectory
		? std::filesystem::path(outputDirectory)
		: std::filesystem::path("TestResults/message-box-visual");
	std::filesystem::create_directories(output, error);
	Check(!error, "visual output directory created");

	BackdropWindow backdrop;
	Check(backdrop.Create(), "visual backdrop created");
	if (!backdrop.Get()) return failures ? failures : 1;

	VisualCaptureContext okCapture{ output / "01-ok.png", backdrop.Get() };
	auto ok = MakeOkRequest(L"Information",
		L"Inkeys completed the requested operation successfully.");
	ok.owner = backdrop.Get();
	Check(MessageBoxTest::ShowAutomated(ok,
		{ CaptureAndClose, &okCapture, 220 }) == Result::Ok,
		"visual OK result");
	PrintCaptureFailure("OK", okCapture);
	Check(okCapture.saved, "visual OK screenshot saved");
	Check(okCapture.pixelsValid, "visual OK pixels validated");
	Check(okCapture.dialog && !IsWindow(okCapture.dialog),
		"visual OK dialog destroyed");

	VisualCaptureContext noCapture{ output / "02-yes-no-focus.png", backdrop.Get() };
	noCapture.interaction = VisualInteraction::FocusSecondary;
	auto yesNo = MakeYesNoRequest(L"End presentation?",
		L"Continuing will clear the current canvas. Do you want to proceed?");
	yesNo.owner = backdrop.Get();
	Check(MessageBoxTest::ShowAutomated(yesNo,
		{ CaptureAndClose, &noCapture, 220 }) == Result::No,
		"visual focused No result");
	PrintCaptureFailure("Yes/No", noCapture);
	Check(noCapture.saved, "visual Yes/No screenshot saved");
	Check(noCapture.pixelsValid, "visual Yes/No pixels validated");

	VisualCaptureContext errorCapture{ output / "03-error-icon.png", backdrop.Get() };
	errorCapture.requireErrorColor = true;
	auto errorRequest = MakeOkRequest(L"Inkeys Error",
		L"Inkeys encountered a problem. Restart the app to continue.");
	errorRequest.owner = backdrop.Get();
	errorRequest.icon = IconSource::BuiltInError();
	Check(MessageBoxTest::ShowAutomated(errorRequest,
		{ CaptureAndClose, &errorCapture, 220 }) == Result::Ok,
		"visual error result");
	PrintCaptureFailure("Error", errorCapture);
	Check(errorCapture.saved, "visual error screenshot saved");
	Check(errorCapture.pixelsValid, "visual error pixels validated");

	VisualCaptureContext hoverCapture{ output / "04-primary-hover.png", backdrop.Get() };
	hoverCapture.interaction = VisualInteraction::HoverPrimary;
	auto hoverRequest = MakeOkRequest(L"Hover state",
		L"The primary action uses the Fluent accent hover token.");
	hoverRequest.owner = backdrop.Get();
	Check(MessageBoxTest::ShowAutomated(hoverRequest,
		{ CaptureAndClose, &hoverCapture, 220 }) == Result::Ok,
		"visual primary hover result");
	PrintCaptureFailure("Hover", hoverCapture);
	Check(hoverCapture.saved && hoverCapture.pixelsValid,
		"visual primary hover screenshot validated");

	VisualCaptureContext pressedCapture{
		output / "05-secondary-pressed.png", backdrop.Get() };
	pressedCapture.interaction = VisualInteraction::PressSecondary;
	auto pressedRequest = MakeYesNoRequest(L"Pressed state",
		L"The secondary action remains legible while pressed.");
	pressedRequest.owner = backdrop.Get();
	Check(MessageBoxTest::ShowAutomated(pressedRequest,
		{ CaptureAndClose, &pressedCapture, 220 }) == Result::No,
		"visual secondary pressed result");
	PrintCaptureFailure("Pressed", pressedCapture);
	Check(pressedCapture.saved && pressedCapture.pixelsValid,
		"visual secondary pressed screenshot validated");

	Check(IsWindowEnabled(backdrop.Get()) != FALSE,
		"visual backdrop restored after all dialogs");
	if (failures == 0)
		std::cout << "PASS message box visual screenshots: "
			<< output.string() << '\n';
	return failures;
}
