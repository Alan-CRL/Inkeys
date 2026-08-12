#include <windows.h>
#include <iostream>
#include <limits>

import Inkeys.Message;

int RunMessageTests()
{
	using namespace Inkeys::Message;
	int failures = 0;
	auto check = [&](bool condition, const char* name)
		{
			if (!condition)
			{
				std::cerr << "FAILED message: " << name << '\n';
				++failures;
			}
		};

	Channel channel(2);
	Message key{};
	key.message = WM_KEYDOWN;
	key.vkcode = VK_RETURN;
	key.scancode = 0x1C;
	key.extended = true;
	key.prevdown = true;
	Message mouse{};
	mouse.message = WM_MOUSEMOVE;
	mouse.ctrl = true;
	mouse.shift = true;
	mouse.lbutton = true;
	mouse.mbutton = true;
	mouse.rbutton = true;
	mouse.x = 12;
	mouse.y = 34;
	check(channel.Enqueue(key) && channel.Enqueue(mouse), "enqueue");
	check(!channel.Enqueue(mouse) && channel.DroppedCount() == 1, "capacity drop");
	check(channel.Clear(Filter::Mouse) == 1 && channel.Size() == 1, "filtered clear");
	Message result{};
	check(channel.TryGet(result, Filter::Key) && result.message == WM_KEYDOWN
		&& result.category == static_cast<BYTE>(Filter::Key)
		&& result.vkcode == VK_RETURN && result.scancode == 0x1C
		&& result.extended && result.prevdown && channel.Size() == 0, "consume");
	channel.Shutdown();
	check(!channel.Enqueue(mouse) && !channel.TryGet(result), "shutdown");

	Channel categoryChannel(4);
	Message fullMouse = mouse;
	fullMouse.hwnd = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x1234));
	fullMouse.wheel = 120;
	Message character{};
	character.message = WM_CHAR;
	character.ch = TEXT('\u4E2D');
	Message window{};
	window.message = static_cast<USHORT>(WM_APP + 1);
	window.category = static_cast<BYTE>(Filter::Window);
	window.wParam = static_cast<WPARAM>(0x12345678);
	window.lParam = static_cast<LPARAM>(-123);
	check(categoryChannel.Enqueue(fullMouse)
		&& categoryChannel.Enqueue(character)
		&& categoryChannel.Enqueue(window), "category enqueue");
	check(categoryChannel.TryGet(result, Filter::Mouse)
		&& result.hwnd == fullMouse.hwnd
		&& result.ctrl && result.shift && result.lbutton
		&& result.mbutton && result.rbutton
		&& result.x == fullMouse.x && result.y == fullMouse.y
		&& result.wheel == fullMouse.wheel, "mouse fields roundtrip");
	check(categoryChannel.TryGet(result, Filter::Char)
		&& result.category == static_cast<BYTE>(Filter::Char)
		&& result.ch == character.ch, "char fields roundtrip");
	check(categoryChannel.TryGet(result, Filter::Window)
		&& result.category == static_cast<BYTE>(Filter::Window)
		&& result.wParam == window.wParam && result.lParam == window.lParam,
		"window fields roundtrip");

	Channel touchChannel(8);
	constexpr short touchMarker = (std::numeric_limits<short>::min)();
	constexpr short touchCancelMarker = touchMarker + 1;
	auto makeTouch = [](USHORT message, short x, short y, bool lbutton, short marker)
		{
			Message result{};
			result.message = message;
			result.x = x;
			result.y = y;
			result.lbutton = lbutton;
			result.wheel = marker;
			return result;
		};
	const Message touchMessages[] = {
		makeTouch(WM_LBUTTONDOWN, 10, 20, true, touchMarker),
		makeTouch(WM_MOUSEMOVE, 12, 24, true, touchMarker),
		makeTouch(WM_LBUTTONUP, 16, 28, false, touchMarker),
		makeTouch(WM_LBUTTONUP, 18, 30, false, touchCancelMarker),
	};
	for (const auto& touch : touchMessages)
		check(touchChannel.Enqueue(touch), "touch enqueue");
	for (const auto& expected : touchMessages)
	{
		Message actual{};
		check(touchChannel.TryGet(actual, Filter::Mouse)
			&& actual.category == static_cast<BYTE>(Filter::Mouse)
			&& actual.message == expected.message
			&& actual.x == expected.x && actual.y == expected.y
			&& actual.lbutton == expected.lbutton
			&& actual.wheel == expected.wheel,
			"touch marker roundtrip");
	}
	return failures;
}
