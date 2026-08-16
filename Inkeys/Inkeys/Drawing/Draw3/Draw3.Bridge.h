#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>

namespace Inkeys::Drawing::Draw3::Bridge
{
	// 产品层固定工具编号；不要把旧 Draw2 的模式值直接暴露给 Draw3。
	enum class Tool : std::uint8_t
	{
		Pen,
		Highlighter,
		FixedEraser,
		SpeedEraser,
		Laser,
		SolidLine,
		DashedLine,
		OutlineRectangle,
		FilledRectangle
	};

	enum class CommandType : std::uint8_t
	{
		Clear,
		Undo,
		Redo,
		NextPage,
		PreviousPage,
		Save,
		SuperRecovery,
		AutoStraighten,
		InputTest
	};

	enum class CommandResult : std::uint8_t
	{
		Accepted,
		Unsupported,
		QueueFull,
		NotRunning
	};

	struct ProductState
	{
		Tool tool = Tool::Pen;
		std::uint32_t colorRgba = 0x000000FFu;
		float widthDip = 2.0f;
		bool selectionMode = true;
		std::uint32_t page = 0;
		bool hasPage = false;
		std::uint64_t revision = 0;
	};

	struct Command
	{
		CommandType type = CommandType::Clear;
		std::uint64_t sequence = 0;
	};

	constexpr int NormalizeLegacyEraserMode(int value) noexcept
	{
		// 旧配置 0 是压感模式，迁移时归一化为速度橡皮；未知值统一固定橡皮。
		return value == 0 ? 1 : value == 1 ? 1 : 2;
	}

	constexpr int EncodeEraserMode(Tool tool) noexcept
	{
		return tool == Tool::SpeedEraser ? 1 : 2;
	}

	class StateBridge
	{
	public:
		explicit StateBridge(std::size_t capacity = 256) noexcept;
		void PublishState(ProductState state) noexcept;
		ProductState Snapshot() const noexcept;
		CommandResult Publish(CommandType type) noexcept;
		bool TryConsume(Command& command) noexcept;
		// 每次 Host 启动前清空旧命令并重新开放桥接。
		void Reset() noexcept;
		void Stop() noexcept;
		bool Running() const noexcept;

	private:
		mutable std::mutex mutex_;
		ProductState state_{};
		std::deque<Command> commands_;
		std::size_t capacity_ = 256;
		std::uint64_t nextSequence_ = 1;
		bool running_ = true;
	};
}
