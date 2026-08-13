module;

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>

export module Inkeys.UI.RenderScheduler;

export namespace Inkeys::UI::RenderScheduler
{
	enum class Client : std::uint8_t
	{
		Bar,
		PptBottomLeft,
		PptBottomRight,
		PptMiddleLeft,
		PptMiddleRight,
		PptExitShow,
		Settings,
		Count,
	};

	enum class FrameResult : std::uint8_t
	{
		Idle,
		Continue,
		Retry,
		DeviceLost,
		// 仅用于进程级共享渲染循环退出；局部客户端停止应调用 Unregister。
		Stop,
	};

	using ClientMask = std::uint32_t;
	using RenderCallback = std::function<FrameResult()>;
	using DeviceRecoveryCallback = std::function<bool()>;

	[[nodiscard]] constexpr ClientMask Mask(Client client) noexcept
	{
		return ClientMask{ 1 } << static_cast<unsigned>(client);
	}

	[[nodiscard]] constexpr ClientMask PptPageMask() noexcept
	{
		return Mask(Client::PptBottomLeft) | Mask(Client::PptBottomRight)
			| Mask(Client::PptMiddleLeft) | Mask(Client::PptMiddleRight);
	}

	[[nodiscard]] constexpr ClientMask PptMask() noexcept
	{
		return PptPageMask() | Mask(Client::PptExitShow);
	}

	struct DispatchDecision
	{
		ClientMask work = 0;
		ClientMask next = 0;
		bool sleep = true;
		bool rebuildSharedDevice = false;
		bool stop = false;
	};

	class DispatchState
	{
	public:
		void Request(ClientMask mask) noexcept;
		[[nodiscard]] ClientMask TakeRequested() noexcept;
		[[nodiscard]] DispatchDecision Complete(
			ClientMask work,
			const std::array<FrameResult, static_cast<std::size_t>(Client::Count)>&
				results) noexcept;

	private:
		std::atomic<ClientMask> requested_ = 0;
	};

	class Scheduler
	{
	public:
		Scheduler();
		~Scheduler();
		Scheduler(const Scheduler&) = delete;
		Scheduler& operator=(const Scheduler&) = delete;

		[[nodiscard]] bool Register(Client client, RenderCallback callback);
		// 返回时该客户端已没有正在执行的回调；不得从该客户端自己的回调中调用。
		void Unregister(Client client) noexcept;
		void SetDeviceRecoveryCallback(DeviceRecoveryCallback callback);
		void Request(Client client) noexcept;
		void Request(ClientMask mask) noexcept;
		void WakeForStop() noexcept;
		void Run(const std::function<bool()>& shouldStop);

	private:
		struct Impl;
		Impl* impl_ = nullptr;
	};

	[[nodiscard]] Scheduler& GetScheduler() noexcept;
	void Request(Client client) noexcept;
	void Request(ClientMask mask) noexcept;
}
