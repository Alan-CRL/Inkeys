module;

#include "MouseHook.Transport.h"

export module Inkeys.Input.MouseHook;

export namespace Inkeys::Input::MouseHook
{
	using Route = HookTransport::Route;
	using Event = HookTransport::Event;
	using Kind = HookTransport::Kind;

	[[nodiscard]] bool Start();
	void Stop() noexcept;
	// 以下接口仅由 Bar 交互 owner 调用；每次消费有界，嵌套交互也持续轮询。
	void PublishRoute(const Route& route) noexcept;
	[[nodiscard]] bool TryPop(Event& event) noexcept;
	void DispatchWheel(const Event& event) noexcept;
	void PollPptWheel() noexcept;
}
