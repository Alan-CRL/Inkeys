module;

#include <windows.h>
#include "Message.Legacy.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

export module Inkeys.Message;

export namespace Inkeys::Message
{
	using Message = ::ExMessage;

	enum class Filter : BYTE
	{
		None = 0,
		Mouse = 1 << 0,
		Key = 1 << 1,
		Char = 1 << 2,
		Window = 1 << 3,
		All = 0xFF,
	};

	[[nodiscard]] constexpr Filter operator|(Filter lhs, Filter rhs) noexcept
	{
		return static_cast<Filter>(static_cast<BYTE>(lhs) | static_cast<BYTE>(rhs));
	}

	enum class Action
	{
		Default,
		Discard,
		Handled,
		QueueAndHandled,
		ForceQueue,
	};

	struct Reply
	{
		Action action = Action::Default;
		LRESULT result = 0;
	};

	enum class ClosePolicy
	{
		Forward,
		DestroyWindow,
		Ignore,
	};

	struct BindOptions
	{
		Filter filter = Filter::All;
		ClosePolicy closePolicy = ClosePolicy::Forward;
		bool postQuitOnDestroy = false;
		bool bindExistingChildren = false;
		bool bindFutureChildren = false;
	};

	class Channel
	{
	public:
		using Callback = std::function<Reply(HWND, UINT, WPARAM, LPARAM)>;

		explicit Channel(std::size_t capacity = 63);
		~Channel();
		Channel(const Channel&) = delete;
		Channel& operator=(const Channel&) = delete;
		Channel(Channel&&) = delete;
		Channel& operator=(Channel&&) = delete;

		[[nodiscard]] bool Bind(HWND hwnd, const BindOptions& options = {});
		[[nodiscard]] bool Unbind(HWND hwnd);
		void SetCallback(Callback callback);
		void ClearCallback();
		[[nodiscard]] bool Get(
			Message& message,
			Filter filter = Filter::All,
			DWORD timeoutMilliseconds = INFINITE);
		[[nodiscard]] bool TryGet(Message& message, Filter filter = Filter::All);
		[[nodiscard]] bool Enqueue(Message message);
		std::size_t Clear(Filter filter = Filter::All);
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] std::uint64_t DroppedCount() const noexcept;
		void Shutdown() noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl_;
	};
}
