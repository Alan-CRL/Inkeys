module;

#include "../../additional/HiMsg/HiMsg/HiMsg.hpp"

#include <utility>

module Inkeys.Message;

namespace
{
	[[nodiscard]] HiMsg::MessageFilter ToHiMsg(Inkeys::Message::Filter filter) noexcept
	{
		return static_cast<HiMsg::MessageFilter>(static_cast<BYTE>(filter));
	}

	[[nodiscard]] HiMsg::MessageReply ToHiMsg(Inkeys::Message::Reply reply) noexcept
	{
		return {
			static_cast<HiMsg::MessageAction>(static_cast<int>(reply.action)),
			reply.result,
		};
	}
}

namespace Inkeys::Message
{
	bool IsTouchGeneratedMouseMessage(UINT message, ULONG_PTR extraInfo) noexcept
	{
		constexpr ULONG_PTR pointerMouseSignature = 0xFF515700u;
		constexpr ULONG_PTR pointerMouseSignatureMask = 0xFFFFFF00u;
		constexpr ULONG_PTR pointerMouseTouchFlag = 0x00000080u;

		bool mouseMessage = false;
		switch (message)
		{
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_XBUTTONDBLCLK:
			mouseMessage = true;
			break;
		default:
			break;
		}

		return mouseMessage
			&& (extraInfo & pointerMouseSignatureMask) == pointerMouseSignature
			&& (extraInfo & pointerMouseTouchFlag) != 0;
	}

	class Channel::Impl
	{
	public:
		explicit Impl(std::size_t capacity) : channel(capacity) {}
		HiMsg::HiMsgSet channel;
	};

	Channel::Channel(std::size_t capacity)
		: impl_(std::make_unique<Impl>(capacity))
	{
	}

	Channel::~Channel() = default;

	bool Channel::Bind(HWND hwnd, const BindOptions& options)
	{
		HiMsg::BindOptions nativeOptions;
		nativeOptions.filter = ToHiMsg(options.filter);
		nativeOptions.closePolicy = static_cast<HiMsg::ClosePolicy>(
			static_cast<int>(options.closePolicy));
		nativeOptions.postQuitOnDestroy = options.postQuitOnDestroy;
		nativeOptions.bindExistingChildren = options.bindExistingChildren;
		nativeOptions.bindFutureChildren = options.bindFutureChildren;
		return impl_->channel.BindWindow(hwnd, nativeOptions);
	}

	bool Channel::Unbind(HWND hwnd)
	{
		return impl_->channel.UnbindWindow(hwnd);
	}

	void Channel::SetCallback(Callback callback)
	{
		impl_->channel.SetMessageCallback(
			[callback = std::move(callback)](HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
			{
				return callback
					? ToHiMsg(callback(hwnd, message, wParam, lParam))
					: HiMsg::MessageReply::Default();
			});
	}

	void Channel::ClearCallback()
	{
		impl_->channel.ClearMessageCallback();
	}

	bool Channel::Get(Message& message, Filter filter, DWORD timeoutMilliseconds)
	{
		return impl_->channel.GetMessage(message, ToHiMsg(filter), timeoutMilliseconds);
	}

	bool Channel::TryGet(Message& message, Filter filter)
	{
		return impl_->channel.TryGetMessage(message, ToHiMsg(filter));
	}

	bool Channel::Enqueue(Message message)
	{
		return impl_->channel.EnqueueMessage(std::move(message));
	}

	std::size_t Channel::Clear(Filter filter)
	{
		return impl_->channel.ClearMessages(ToHiMsg(filter));
	}

	std::size_t Channel::Size() const noexcept
	{
		return impl_->channel.GetQueueSize();
	}

	std::uint64_t Channel::DroppedCount() const noexcept
	{
		return impl_->channel.GetDroppedMessageCount();
	}

	void Channel::Shutdown() noexcept
	{
		impl_->channel.Shutdown();
	}
}
