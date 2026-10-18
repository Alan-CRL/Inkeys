#pragma once

#include "../../../../inkStrokeModelerTest/draw3/shape_interaction.h"
#include <cstdint>
#include <memory>

namespace Inkeys::Drawing::Draw3::Bridge { enum class CommandResult : std::uint8_t; }
namespace Inkeys::Drawing::Draw3
{
	struct ShapeUiIdentity
	{
		std::uint64_t session = 0, generation = 0, documentGeneration = 0, displayGeneration = 0;
		std::uint64_t objectRevision = 0;
		draw3::shapes::ObjectId objectId{}, pageId{};
		std::uint8_t workspace = 0;
		std::size_t pageIndex = 0;
		bool operator==(const ShapeUiIdentity&) const = default;
	};
	struct ShapeUiSnapshot
	{
		ShapeUiIdentity identity;
		std::optional<draw3::shapes::ShapeObject> object;
		std::vector<draw3::shapes::EditHandle> handles;
		draw3::shapes::Bounds bounds;
		draw3::shapes::ConstructionStatus construction;
		bool editGestureActive = false;
		float displayScale = 1;
		int canvasOriginX = 0, canvasOriginY = 0;
	};
	enum class ShapeUiActionKind : std::uint8_t
	{
		BeginEdit, PreviewEdit, CommitEdit, CancelEdit, CloseEditor,
		SetStyle, SetSides, ConstructionComplete, ConstructionBack, ConstructionCancel
	};
	struct ShapeUiAction
	{
		ShapeUiActionKind kind = ShapeUiActionKind::CloseEditor;
		ShapeUiIdentity identity;
		std::uint32_t handleId = 0;
		draw3::shapes::Vec2 down, point;
		draw3::shapes::ShapeStyle style;
		std::uint32_t sides = 3;
	};
	std::shared_ptr<const ShapeUiSnapshot> ProductShapeUiSnapshot() noexcept;
	Bridge::CommandResult PublishShapeUiAction(const ShapeUiAction&) noexcept;
	// drawing owner发布不可变快照；refresh只调度UI帧，不直接进入窗口/文档锁。
	void PublishShapeUiSnapshot(std::shared_ptr<const ShapeUiSnapshot>) noexcept;
	using ShapeUiRefresh = void(*)(void*) noexcept;
	// 清空callback会等待正在执行的refresh退出；必须在销毁UI context之前调用。
	void SetShapeUiRefreshCallback(ShapeUiRefresh callback, void* context) noexcept;
}
