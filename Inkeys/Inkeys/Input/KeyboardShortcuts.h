#pragma once

#include <cstdint>

namespace Inkeys::Input::KeyboardShortcuts
{
	enum class Command : unsigned char { ToggleAnnotation, Undo, RedoY, RedoShiftZ, Clear, Freeze, DismissWhiteboardMenu };
	inline constexpr std::uint32_t AllEnabled = 0x3fU;
	[[nodiscard]] constexpr std::uint32_t Flag(Command command) noexcept
	{
		return 1U << static_cast<unsigned>(command);
	}

	// 仅 Bar owner 发布；身份是不透明值，输入服务不依赖 StateMode/Draw3 或 UI 对象。
	struct RouteSnapshot
	{
		std::uint64_t lifecycle = 0, modeRevision = 0, sceneScope = 0;
		std::uint64_t contextGeneration = 0, foreground = 0;
		std::uint32_t heartbeatTick = 0;
		bool annotation = false, textInput = false, modal = false, businessReady = false;
		bool whiteboardMenuInput = false;
		std::uint64_t whiteboardMenuEpoch = 0;
	};
	struct Intent
	{
		Command command = Command::ToggleAnnotation;
		std::uint64_t lifecycle = 0, modeRevision = 0, sceneScope = 0;
		std::uint64_t contextGeneration = 0, foreground = 0;
		std::uint64_t configGeneration = 0, serviceGeneration = 0;
		std::uint64_t pressId = 0, ctrlSessionId = 0, gestureGeneration = 0, interruptionGeneration = 0;
		bool repeat = false;
		std::uint64_t whiteboardMenuEpoch = 0;
	};
	struct GestureState
	{
		std::uint64_t ctrlSessionId = 0, gestureGeneration = 0, interruptionGeneration = 0;
		bool controlDown = false, valid = false;
	};
	struct Status
	{
		bool running = false, hookInstalled = false, hotkeyRegistered = false;
		bool synchronizationPending = true;
		std::uint32_t enabledMask = AllEnabled, hookError = 0, hotkeyError = 0;
		std::uint64_t configGeneration = 0, lifecycleGeneration = 0;
		std::uint64_t submitted = 0, dropped = 0, consumed = 0, paused = 0;
	};

	// Start/Stop 由生命周期 owner 串行调用；SetEnabled 可从设置线程异步发布。
	// LL 启动先尝试受桌面/权限保护的一次性快照；未知或已按住的键仍等真实 Up，全局 X 独立。
	[[nodiscard]] Status Start();
	void Stop() noexcept;
	void SetEnabled(std::uint32_t enabledMask) noexcept;
	[[nodiscard]] std::uint32_t GetEnabled() noexcept;
	[[nodiscard]] Status GetStatus() noexcept;
	void PublishRoute(const RouteSnapshot& route) noexcept;
	// UI 在普通 owner 发布身份；LL 只读原子值，不能反向调用分页或窗口服务。
	void PublishWhiteboardMenuEpoch(std::uint64_t epoch) noexcept;
	[[nodiscard]] bool TryPop(Intent& intent) noexcept;
	[[nodiscard]] GestureState GetGestureState() noexcept;
}
