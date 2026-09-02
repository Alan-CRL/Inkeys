module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Windows.h 会把 MessageBox 定义成 A/W 宏；模块名和命名空间必须保持稳定。
#ifdef MessageBox
#undef MessageBox
#endif

export module Inkeys.UI.MessageBox;

export namespace Inkeys::UI::MessageBox
{
	enum class Result : std::uint8_t
	{
		Ok,
		Cancel,
		Yes,
		No,
		Dismissed,
		Failed,
	};

	enum class Buttons : std::uint8_t
	{
		Ok,
		OkCancel,
		YesNo,
	};

	enum class Reliability : std::uint8_t
	{
		Normal,
		CriticalNoWait,
	};

	enum class SystemModality : std::uint8_t
	{
		Application,
		System,
	};

	enum class SystemIcon : std::uint8_t
	{
		None,
		Error,
	};

	enum class IconKind : std::uint8_t
	{
		None,
		PremultipliedBgra,
		PngResource,
		BuiltInError,
	};

	struct IconSource
	{
		IconKind kind = IconKind::None;
		const std::uint8_t* pixels = nullptr;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t strideBytes = 0;
		HMODULE resourceModule = nullptr;
		const wchar_t* resourceType = nullptr;
		const wchar_t* resourceName = nullptr;

		[[nodiscard]] static IconSource FromPremultipliedBgra(
			const void* pixels, std::uint32_t width, std::uint32_t height,
			std::uint32_t strideBytes) noexcept;
		[[nodiscard]] static IconSource FromPngResource(
			HMODULE module, const wchar_t* type, const wchar_t* name) noexcept;
		[[nodiscard]] static IconSource BuiltInError() noexcept;
	};

	struct FallbackPolicy
	{
		HWND owner = nullptr;
		SystemModality modality = SystemModality::Application;
		SystemIcon icon = SystemIcon::None;
	};

	struct Request
	{
		const wchar_t* title = nullptr;
		const wchar_t* body = nullptr;
		HWND owner = nullptr;
		bool requireOwner = false;
		Buttons buttons = Buttons::Ok;
		Result defaultResult = Result::Ok;
		bool dismissEnabled = true;
		Result dismissResult = Result::Ok;
		bool showCloseButton = true;
		IconSource icon{};
		bool ownerlessTopmostAtCreation = false;
		Reliability reliability = Reliability::Normal;
		FallbackPolicy fallback{};
	};

	[[nodiscard]] Request MakeOkRequest(
		const wchar_t* title, const wchar_t* body) noexcept;
	[[nodiscard]] Request MakeOkCancelRequest(
		const wchar_t* title, const wchar_t* body) noexcept;
	[[nodiscard]] Request MakeYesNoRequest(
		const wchar_t* title, const wchar_t* body) noexcept;
	[[nodiscard]] Result Show(const Request& request) noexcept;
}

#ifdef INKEYS_MESSAGE_BOX_TESTING
export namespace Inkeys::UI::MessageBox::Test
{
	using VisibleCallback = void(*)(HWND hwnd, void* context) noexcept;
	using SystemFallbackCallback = Result(*)(const Request& request,
		HWND normalizedOwner, UINT flags, void* context) noexcept;

	struct Automation
	{
		VisibleCallback visibleCallback = nullptr;
		void* context = nullptr;
		UINT delayMilliseconds = 160;
		SystemFallbackCallback systemFallbackCallback = nullptr;
		void* fallbackContext = nullptr;
		VisibleCallback firstFrameReadyCallback = nullptr;
		void* firstFrameReadyContext = nullptr;
	};

	struct ButtonResults
	{
		Result first = Result::Failed;
		Result second = Result::Failed;
		int count = 0;
	};

	struct Labels
	{
		std::wstring ok;
		std::wstring cancel;
		std::wstring yes;
		std::wstring no;
	};

	struct LayoutProbe
	{
		bool succeeded = false;
		int width = 0;
		int height = 0;
		int buttonCount = 0;
		bool hasIcon = false;
	};

	[[nodiscard]] bool Validate(const Request& request) noexcept;
	[[nodiscard]] UINT ResolveFallbackFlags(const Request& request) noexcept;
	[[nodiscard]] Result MapSystemResult(Buttons buttons, int value) noexcept;
	[[nodiscard]] int ScaleDip(int dip, UINT dpi) noexcept;
	[[nodiscard]] ButtonResults ResolveButtonResults(Buttons buttons) noexcept;
	[[nodiscard]] Labels ResolveLabels(LANGID language);
	[[nodiscard]] LayoutProbe ProbeLayout(const Request& request,
		UINT dpi, int workAreaWidth, int workAreaHeight) noexcept;
	[[nodiscard]] Result ShowAutomated(
		const Request& request, const Automation& automation) noexcept;
}
#endif

namespace Inkeys::UI::MessageBox::Detail
{
	inline constexpr int BuiltInErrorResourceId = 305;
	inline constexpr std::uint32_t MaximumIconDimension = 512;
	inline constexpr std::size_t MaximumIconBytes = 4u * 1024u * 1024u;

	enum class StoredIconKind : std::uint8_t
	{
		None,
		PremultipliedBgra,
		PngBytes,
	};

	struct OwnedIcon
	{
		StoredIconKind kind = StoredIconKind::None;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t strideBytes = 0;
		std::vector<std::uint8_t> bytes;
	};

	struct OwnedRequest
	{
		std::wstring title;
		std::wstring body;
		HWND owner = nullptr;
		Buttons buttons = Buttons::Ok;
		Result defaultResult = Result::Ok;
		bool dismissEnabled = true;
		Result dismissResult = Result::Ok;
		bool showCloseButton = true;
		bool ownerlessTopmostAtCreation = false;
		FallbackPolicy fallback{};
		OwnedIcon icon{};
#ifdef INKEYS_MESSAGE_BOX_TESTING
		Test::Automation automation{};
#endif
	};

	struct ButtonLabelSet
	{
		std::wstring ok;
		std::wstring cancel;
		std::wstring yes;
		std::wstring no;
	};

	struct ButtonSpec
	{
		Result result = Result::Failed;
		std::wstring label;
		RECT bounds{};
		bool enabled = true;
	};

	struct DialogLayout
	{
		UINT dpi = 96;
		int width = 0;
		int height = 0;
		int titleTop = 0;
		int titleHeight = 0;
		int bodyTop = 0;
		int bodyHeight = 0;
		int commandTop = 0;
		RECT titleBounds{};
		RECT bodyBounds{};
		RECT iconBounds{};
		RECT closeBounds{};
		ButtonSpec buttonSpecs[2]{};
		int buttonCount = 0;
	};

	struct RunOutcome
	{
		bool completed = false;
		Result result = Result::Failed;
	};

	[[nodiscard]] bool ValidateRequest(const Request& request) noexcept;
	[[nodiscard]] bool CopyRequest(
		const Request& request, OwnedRequest& output) noexcept;
	[[nodiscard]] UINT BuildFallbackFlags(const Request& request) noexcept;
	[[nodiscard]] Result MapSystemMessageResult(
		Buttons buttons, int value) noexcept;
	[[nodiscard]] int ScaleDipValue(int dip, UINT dpi) noexcept;
	[[nodiscard]] ButtonLabelSet ResolveButtonLabels(LANGID language);
	[[nodiscard]] int BuildButtonSpecs(
		Buttons buttons, const ButtonLabelSet& labels,
		ButtonSpec (&output)[2]);
	[[nodiscard]] bool IsResultAllowed(
		Buttons buttons, Result result) noexcept;
	[[nodiscard]] RunOutcome RunDialog(OwnedRequest& request) noexcept;
}
