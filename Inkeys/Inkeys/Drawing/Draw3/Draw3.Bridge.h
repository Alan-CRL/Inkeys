#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Inkeys::Drawing::Draw3::Bridge
{
	// 产品 UI 与 Draw3 光标/笔迹共同使用的最终荧光笔透明度。
	inline constexpr float kHighlighterCompositeOpacity = 0.35f;
	inline constexpr std::uint32_t kMaximumPresentationPages = 10000;

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

	struct PresentationKey
	{
		std::array<std::uint8_t, 16> bytes = {};

		[[nodiscard]] constexpr bool IsZero() const noexcept
		{
			for (std::uint8_t value : bytes) if (value != 0) return false;
			return true;
		}
		friend auto operator<=>(const PresentationKey&,
			const PresentationKey&) noexcept = default;
	};

	enum class SlideBindingMode : std::uint8_t
	{
		StableSlideId,
		PageIndexFallback,
	};

	// COM 事实在一个锁内发布；页码不能脱离文稿身份单独成为 ready。
	struct PresentationTarget
	{
		PresentationKey key;
		SlideBindingMode bindingMode = SlideBindingMode::PageIndexFallback;
		std::string sourceIdentity;
		std::string presentationName;
		std::string provider;
		std::string bindingToken;
		std::vector<std::int32_t> slideIds;
		std::optional<std::int32_t> slideId;
		std::uint32_t pageIndex = 0;
		std::uint32_t totalPages = 0;
		std::uint64_t bindingRevision = 0;
		std::uint64_t targetRevision = 0;
		bool processLocalIdentity = false;

		friend bool operator==(const PresentationTarget&,
			const PresentationTarget&) noexcept = default;
	};

	// 高频 runtime 快照只发布 ready 身份，不复制路径、名称或完整 SlideID 拓扑。
	struct PresentationReadyIdentity
	{
		PresentationKey key;
		SlideBindingMode bindingMode = SlideBindingMode::PageIndexFallback;
		std::optional<std::int32_t> slideId;
		std::uint32_t pageIndex = 0;
		std::uint64_t bindingRevision = 0;
		std::uint64_t targetRevision = 0;

		friend bool operator==(const PresentationReadyIdentity&,
			const PresentationReadyIdentity&) noexcept = default;
	};

	constexpr PresentationReadyIdentity ReadyIdentityFor(
		const PresentationTarget& target) noexcept
	{
		return { target.key, target.bindingMode, target.slideId, target.pageIndex,
			target.bindingRevision, target.targetRevision };
	}

	constexpr bool SelectionUsesAuxiliaryOutput(
		bool selectionMode, Workspace workspace) noexcept
	{
		return selectionMode && workspace != Workspace::Whiteboard;
	}

	constexpr bool PresentationInputSuppressed(
		Workspace workspace, bool loadPending) noexcept
	{
		return workspace == Workspace::Presentation && loadPending;
	}

	constexpr bool PresentationCanvasCommandSuppressed(
		Workspace workspace, bool loadPending, bool finalExitBarrier) noexcept
	{
		return PresentationInputSuppressed(workspace, loadPending) &&
			!finalExitBarrier;
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
		std::shared_ptr<const PresentationTarget> presentationTarget;
		std::uint64_t revision = 0;
	};

	struct Command
	{
		CommandType type = CommandType::Clear;
		std::uint64_t sequence = 0;
		Workspace workspace = Workspace::Desktop;
		std::shared_ptr<const PresentationTarget> presentationTarget;
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
		std::optional<std::uint64_t> PublishPresentationTarget(
			const PresentationTarget& target, bool* changed = nullptr) noexcept;
		bool ClearPresentationTarget() noexcept;
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
		std::uint64_t nextTargetRevision_ = 1;
		bool running_ = true;
	};
}
