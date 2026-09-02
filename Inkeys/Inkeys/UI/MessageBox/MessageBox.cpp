module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.UI.MessageBox;

namespace Inkeys::UI::MessageBox
{
	namespace
	{
		std::mutex admissionMutex;
		thread_local bool showActiveOnThread = false;
#ifdef INKEYS_MESSAGE_BOX_TESTING
		thread_local Test::Automation testAutomation{};
#endif

		struct ReentryGuard
		{
			ReentryGuard() noexcept { showActiveOnThread = true; }
			~ReentryGuard() { showActiveOnThread = false; }
		};

		Result FallbackToSystem(const Request& request) noexcept
		{
			HWND owner = request.fallback.owner;
			if (owner)
			{
				DWORD processId = 0;
				if (!IsWindow(owner)
					|| !GetWindowThreadProcessId(owner, &processId)
					|| processId != GetCurrentProcessId()) owner = nullptr;
			}
			const wchar_t* title = request.title ? request.title : L"";
			const wchar_t* body = request.body ? request.body : L"";
			const UINT flags = Detail::BuildFallbackFlags(request);
#ifdef INKEYS_MESSAGE_BOX_TESTING
			if (testAutomation.systemFallbackCallback)
				return testAutomation.systemFallbackCallback(request, owner, flags,
					testAutomation.fallbackContext);
#endif
			const int value = ::MessageBoxW(owner, body, title, flags);
			return Detail::MapSystemMessageResult(request.buttons, value);
		}

		bool TryCopyResourceBytes(const IconSource& source,
			Detail::OwnedIcon& output) noexcept
		{
			HMODULE module = source.resourceModule;
			const wchar_t* type = source.resourceType;
			const wchar_t* name = source.resourceName;
			if (source.kind == IconKind::BuiltInError)
			{
				module = GetModuleHandleW(nullptr);
				type = L"PNG";
				name = MAKEINTRESOURCEW(Detail::BuiltInErrorResourceId);
			}
			if (!module || !type || !name) return false;
			const HRSRC resource = FindResourceW(module, name, type);
			if (!resource) return false;
			const DWORD size = SizeofResource(module, resource);
			if (size == 0 || size > Detail::MaximumIconBytes) return false;
			const HGLOBAL loaded = LoadResource(module, resource);
			const void* bytes = loaded ? LockResource(loaded) : nullptr;
			if (!bytes) return false;
			try
			{
				output.bytes.resize(size);
				std::memcpy(output.bytes.data(), bytes, size);
				output.kind = Detail::StoredIconKind::PngBytes;
				return true;
			}
			catch (...)
			{
				output = {};
				return false;
			}
		}

		bool TryCopyPixelBytes(const IconSource& source,
			Detail::OwnedIcon& output) noexcept
		{
			if (!source.pixels || source.width == 0 || source.height == 0
				|| source.width > Detail::MaximumIconDimension
				|| source.height > Detail::MaximumIconDimension)
			{
				return false;
			}
			if (source.width > std::numeric_limits<std::uint32_t>::max() / 4u)
				return false;
			const std::uint32_t rowBytes = source.width * 4u;
			if (source.strideBytes < rowBytes) return false;
			if (source.height > Detail::MaximumIconBytes / source.strideBytes)
				return false;
			const std::size_t total = static_cast<std::size_t>(source.strideBytes)
				* source.height;
			try
			{
				output.bytes.assign(source.pixels, source.pixels + total);
				output.kind = Detail::StoredIconKind::PremultipliedBgra;
				output.width = source.width;
				output.height = source.height;
				output.strideBytes = source.strideBytes;
				return true;
			}
			catch (...)
			{
				output = {};
				return false;
			}
		}
	}

	IconSource IconSource::FromPremultipliedBgra(const void* sourcePixels,
		std::uint32_t sourceWidth, std::uint32_t sourceHeight,
		std::uint32_t sourceStrideBytes) noexcept
	{
		IconSource source;
		source.kind = IconKind::PremultipliedBgra;
		source.pixels = static_cast<const std::uint8_t*>(sourcePixels);
		source.width = sourceWidth;
		source.height = sourceHeight;
		source.strideBytes = sourceStrideBytes;
		return source;
	}

	IconSource IconSource::FromPngResource(HMODULE module,
		const wchar_t* type, const wchar_t* name) noexcept
	{
		IconSource source;
		source.kind = IconKind::PngResource;
		source.resourceModule = module;
		source.resourceType = type;
		source.resourceName = name;
		return source;
	}

	IconSource IconSource::BuiltInError() noexcept
	{
		IconSource source;
		source.kind = IconKind::BuiltInError;
		return source;
	}

	Request MakeOkRequest(const wchar_t* title, const wchar_t* body) noexcept
	{
		Request request;
		request.title = title;
		request.body = body;
		request.buttons = Buttons::Ok;
		request.defaultResult = Result::Ok;
		request.dismissEnabled = true;
		request.dismissResult = Result::Ok;
		return request;
	}

	Request MakeOkCancelRequest(const wchar_t* title, const wchar_t* body) noexcept
	{
		Request request;
		request.title = title;
		request.body = body;
		request.buttons = Buttons::OkCancel;
		request.defaultResult = Result::Ok;
		request.dismissEnabled = true;
		request.dismissResult = Result::Cancel;
		return request;
	}

	Request MakeYesNoRequest(const wchar_t* title, const wchar_t* body) noexcept
	{
		Request request;
		request.title = title;
		request.body = body;
		request.buttons = Buttons::YesNo;
		request.defaultResult = Result::Yes;
		request.dismissEnabled = false;
		request.dismissResult = Result::Dismissed;
		request.showCloseButton = false;
		return request;
	}

	namespace Detail
	{
		bool CopyRequest(const Request& request, OwnedRequest& output) noexcept
		{
			try
			{
				output.title = request.title;
				output.body = request.body;
				output.owner = request.owner;
				output.buttons = request.buttons;
				output.defaultResult = request.defaultResult;
				output.dismissEnabled = request.dismissEnabled;
				output.dismissResult = request.dismissResult;
				output.showCloseButton = request.showCloseButton;
				output.ownerlessTopmostAtCreation = request.ownerlessTopmostAtCreation;
				output.fallback = request.fallback;
#ifdef INKEYS_MESSAGE_BOX_TESTING
				output.automation = testAutomation;
#endif
			}
			catch (...)
			{
				return false;
			}

			// 图标是装饰载荷，任何校验、资源或分配失败都只降级为无图标。
			switch (request.icon.kind)
			{
			case IconKind::PremultipliedBgra:
				(void)TryCopyPixelBytes(request.icon, output.icon);
				break;
			case IconKind::PngResource:
			case IconKind::BuiltInError:
				(void)TryCopyResourceBytes(request.icon, output.icon);
				break;
			default:
				break;
			}
			return true;
		}
	}

	Result Show(const Request& request) noexcept
	{
		if (showActiveOnThread) return FallbackToSystem(request);
		ReentryGuard reentryGuard;
		try
		{
			if (!Detail::ValidateRequest(request)) return FallbackToSystem(request);

			std::unique_lock admission(admissionMutex, std::defer_lock);
			if (request.reliability == Reliability::CriticalNoWait)
			{
				if (!admission.try_lock()) return FallbackToSystem(request);
			}
			else
			{
				admission.lock();
			}

			Detail::OwnedRequest owned;
			if (!Detail::CopyRequest(request, owned)) return FallbackToSystem(request);
			const Detail::RunOutcome outcome = Detail::RunDialog(owned);
			if (outcome.completed) return outcome.result;
			return FallbackToSystem(request);
		}
		catch (...)
		{
			return FallbackToSystem(request);
		}
	}
}

#ifdef INKEYS_MESSAGE_BOX_TESTING
namespace Inkeys::UI::MessageBox::Test
{
	bool Validate(const Request& request) noexcept
	{
		return Detail::ValidateRequest(request);
	}

	UINT ResolveFallbackFlags(const Request& request) noexcept
	{
		return Detail::BuildFallbackFlags(request);
	}

	Result MapSystemResult(Buttons buttons, int value) noexcept
	{
		return Detail::MapSystemMessageResult(buttons, value);
	}

	int ScaleDip(int dip, UINT dpi) noexcept
	{
		return Detail::ScaleDipValue(dip, dpi);
	}

	ButtonResults ResolveButtonResults(Buttons buttons) noexcept
	{
		Detail::ButtonSpec specs[2]{};
		const auto labels = Detail::ResolveButtonLabels(MAKELANGID(LANG_ENGLISH,
			SUBLANG_ENGLISH_US));
		const int count = Detail::BuildButtonSpecs(buttons, labels, specs);
		return { specs[0].result, specs[1].result, count };
	}

	Labels ResolveLabels(LANGID language)
	{
		auto labels = Detail::ResolveButtonLabels(language);
		return { std::move(labels.ok), std::move(labels.cancel),
			std::move(labels.yes), std::move(labels.no) };
	}

	Result ShowAutomated(const Request& request,
		const Automation& automation) noexcept
	{
		const Automation previous = testAutomation;
		testAutomation = automation;
		const Result result = Show(request);
		testAutomation = previous;
		return result;
	}
}
#endif
