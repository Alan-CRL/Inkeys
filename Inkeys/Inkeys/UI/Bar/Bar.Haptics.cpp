module;

#include "../../Drawing/Draw3/Draw3.HapticRequest.h"
#include <utility>

module Inkeys.UI.Bar.Haptics;

namespace Inkeys::UI::Bar::Haptics
{
	namespace
	{
		// 嵌套 callback scope 共享本线程状态，不向其他交互线程泄漏凭据。
		thread_local Click acceptedClick;
		thread_local bool buttonActionAccepted = true;
	}

	const Click& GetAcceptedClick() noexcept { return acceptedClick; }
	bool IsButtonActionAccepted() noexcept { return buttonActionAccepted; }
	void RejectButtonAction() noexcept { if (acceptedClick) buttonActionAccepted = false; }

	AcceptedClickScope::AcceptedClickScope(Click source)
		: previous(std::move(acceptedClick)), previousAccepted(buttonActionAccepted)
	{
		acceptedClick = std::move(source); buttonActionAccepted = true;
	}
	AcceptedClickScope::~AcceptedClickScope()
	{
		acceptedClick = std::move(previous); buttonActionAccepted = previousAccepted;
	}
}
