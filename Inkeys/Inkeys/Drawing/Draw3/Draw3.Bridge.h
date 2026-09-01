#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace Inkeys::Drawing::Draw3::Bridge
{
	// 产品 UI 与 Draw3 光标/笔迹共同使用的最终荧光笔透明度。
	inline constexpr float kHighlighterCompositeOpacity = 0.35f;

	// 产品层固定工具编号；不要把旧 Draw2 的模式值直接暴露给 Draw3。
	enum class Tool : std::uint8_t
	{
		Pen,
		HardPen,
		Highlighter,
		FixedEraser,
		SpeedEraser,
		Laser,
		SolidLine,
		DashedLine,
		OutlineRectangle,
		FilledRectangle
	};

	enum class Workspace : std::uint8_t
	{
		Desktop,
		Whiteboard,
		Presentation,
	};

	constexpr bool SelectionUsesAuxiliaryOutput(
		bool selectionMode, Workspace workspace) noexcept
	{
		return selectionMode && workspace != Workspace::Whiteboard;
	}

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
		InputTest,
		PrepareExitAutoSave,
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
		bool autoSaveEnabled = false;
		std::uint32_t page = 0;
		bool hasPage = false;
		Workspace workspace = Workspace::Desktop;
		// 即使最新状态已回到 Desktop，也不能丢失中间发生过的 PPT 访问。
		std::uint64_t presentationVisitEpoch = 0;
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
		// workspace 使用独立事务发布，使 Desktop/Whiteboard 能明确清除旧 PPT 页状态。
		bool PublishWorkspace(Workspace workspace) noexcept;
		ProductState Snapshot() const noexcept;
		CommandResult Publish(CommandType type) noexcept;
		bool TryConsume(Command& command) noexcept;
		// 每次 Host 启动前清空旧命令并重新开放桥接。
		void Reset() noexcept;
		void Stop() noexcept;
		// 正常退出保留已接受命令，并在 FIFO 尾部加入最终自动保存屏障。
		bool StopWithFinalCommand(CommandType finalCommand) noexcept;
		bool Running() const noexcept;

	private:
		mutable std::mutex mutex_;
		ProductState state_{};
		std::deque<Command> commands_;
		std::optional<Command> finalCommand_;
		std::size_t capacity_ = 256;
		std::uint64_t nextSequence_ = 1;
		bool running_ = true;
	};
}
