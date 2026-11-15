#pragma once

#include "../../../../inkStrokeModelerTest/draw3/shape_interaction.h"
#include <atomic>
#include <chrono>
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
	inline std::atomic<std::uint64_t>& ShapeToolEntryEpoch() noexcept { static std::atomic<std::uint64_t> value{0}; return value; }
	struct ShapeUiSnapshot
	{
		ShapeUiIdentity identity;
		std::uint64_t finishedEditingSession = 0, finishedEditingEntryEpoch = 0;
		std::uint8_t finishedEditingWorkspace = 0;
		std::optional<draw3::shapes::ShapeObject> object;
		std::vector<draw3::shapes::EditHandle> handles;
		draw3::shapes::Bounds bounds, frameRect; // frameRect为共享核心给出的本地外框。
		draw3::shapes::ConstructionStatus construction;
		std::uint64_t completedConstructionSession = 0; // 完成沿保留原构造身份，不能借用随后打开的编辑会话。
		bool editGestureActive = false, constructionContactActive = false;
		bool drawingPreview = false; // 首笔约束提示的只读快照，不是构造会话或可编辑对象。
		std::uint8_t predictionMode = 2, timingProfile = 2;
		float displayScale = 1;
		int canvasOriginX = 0, canvasOriginY = 0;
	};
	enum class ShapeUiActionKind : std::uint8_t
	{
		BeginEdit, PreviewEdit, CommitEdit, CancelEdit, CloseEditor,
		SetStyle, SetSides, SetConstraint, ConstructionComplete, ConstructionBack, ConstructionCancel, ConstructionForward, SetRightSolid,
		ConstructionBeginPointEdit, ConstructionPreviewPointEdit, ConstructionCommitPointEdit, ConstructionCancelPointEdit, FlipHorizontal, FlipVertical,
		Rotate45Clockwise, Rotate90Clockwise
	};
	struct ShapeGestureTransform
	{
		draw3::shapes::Vec2 pivot, translation;
		float scale = 1, rotation = 0;
	};
	struct ShapeUiAction
	{
		ShapeUiActionKind kind = ShapeUiActionKind::CloseEditor;
		ShapeUiIdentity identity;
		std::uint32_t handleId = 0;
		draw3::shapes::Vec2 down, point;
		std::optional<draw3::shapes::Vec2> predictedPoint;
		std::optional<ShapeGestureTransform> gestureTransform, predictedGestureTransform;
		std::optional<float> angleDelta; // 手势累计角：latest-wins覆盖中间点时仍保留轨迹。
		draw3::shapes::ShapeStyle style;
		std::uint32_t sides = 3;
		bool finishEditing = false; // 仅完成勾；普通程序性失焦不恢复工具。
		bool rightSolid = false;
		draw3::shapes::PlanarConstraint constraint = draw3::shapes::PlanarConstraint::Free;
		std::int64_t enqueueMicroseconds = 0; // 仅诊断：Bridge入队时刻，用于计算入队到上屏延迟。
	};
	// 属性候选由运行期和无窗口回归共用，提交仍走原Replace事务。
	inline std::optional<draw3::shapes::ShapeObject> BuildShapePropertyCandidate(
		const draw3::shapes::ShapeObject& before, const ShapeUiAction& action,
		std::uint64_t revision, const draw3::shapes::BuildOptions& options = {})
	{
		if (action.kind == ShapeUiActionKind::SetStyle)
			return draw3::shapes::RestyleShape(before,action.style,revision,options);
		if (action.kind == ShapeUiActionKind::SetSides)
			return draw3::shapes::SetRegularSides(before,action.sides,revision);
		if (action.kind == ShapeUiActionKind::SetConstraint)
			return draw3::shapes::SetPlanarConstraint(before,action.constraint,revision);
		if (action.kind == ShapeUiActionKind::SetRightSolid)
			return draw3::shapes::SetRightSolid(before,action.rightSolid,revision);
		if (action.kind == ShapeUiActionKind::FlipHorizontal || action.kind == ShapeUiActionKind::FlipVertical)
			return draw3::shapes::FlipShape(before,action.kind == ShapeUiActionKind::FlipHorizontal,revision,options);
		if (action.kind == ShapeUiActionKind::Rotate45Clockwise || action.kind == ShapeUiActionKind::Rotate90Clockwise)
			return draw3::shapes::RotateShape(before,action.kind == ShapeUiActionKind::Rotate45Clockwise ? .7853981633974483f : 1.5707963267948966f,revision,options);
		return std::nullopt;
	}

	inline bool IsShapePreviewAction(ShapeUiActionKind kind) noexcept
	{
		return kind == ShapeUiActionKind::PreviewEdit || kind == ShapeUiActionKind::ConstructionPreviewPointEdit;
	}
	inline bool IsConstructionPointAction(ShapeUiActionKind kind) noexcept
	{
		return kind >= ShapeUiActionKind::ConstructionBeginPointEdit && kind <= ShapeUiActionKind::ConstructionCancelPointEdit;
	}

	// 同一编辑手势的预览只需最新一条；Commit携带最终点，可取代尚未执行的预览。
	inline bool SameShapeEditGesture(const ShapeUiAction& a, const ShapeUiAction& b) noexcept
	{
		return a.identity == b.identity && a.handleId == b.handleId && a.down.x == b.down.x && a.down.y == b.down.y;
	}

	inline std::int64_t ShapePreviewClockMicroseconds() noexcept
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// 编辑预览吞吐计数；只用于诊断与离屏验证，不参与任何业务准入。
	struct ShapePreviewCounters
	{
		std::atomic<std::uint64_t> published{0}, bridgeCoalesced{0}, controllerCoalesced{0}, executed{0};
		std::atomic<std::uint64_t> focusExecuted{0}, legacyExecuted{0}, focusFallbacks{0};
		std::atomic<std::uint64_t> canvasCopies{0}, compositionDiscards{0};
		std::atomic<std::uint64_t> focusDraws{0}, materializations{0}, focusDrops{0}, focusUndoDrops{0};
		std::atomic<std::uint64_t> maxPerFrame{0}, maxPendingInBridge{0};
		std::atomic<std::uint64_t> latencySamples{0}, latencyTotalMicroseconds{0}, latencyMaxMicroseconds{0};
	};
	struct ShapePreviewCountersSnapshot
	{
		std::uint64_t published = 0, bridgeCoalesced = 0, controllerCoalesced = 0, executed = 0;
		std::uint64_t focusExecuted = 0, legacyExecuted = 0, focusFallbacks = 0;
		std::uint64_t canvasCopies = 0, compositionDiscards = 0;
		std::uint64_t focusDraws = 0, materializations = 0, focusDrops = 0, focusUndoDrops = 0;
		std::uint64_t maxPerFrame = 0, maxPendingInBridge = 0;
		std::uint64_t latencySamples = 0, latencyTotalMicroseconds = 0, latencyMaxMicroseconds = 0;
	};
	inline ShapePreviewCounters& ShapePreviewMetrics() noexcept
	{
		static ShapePreviewCounters counters;
		return counters;
	}
	// 焦点层总开关（回滚点）：false 时刚完成的图形照旧在抬笔时写入 L2，编辑走旧的权威重放路径。
	inline std::atomic<bool>& ShapeFocusLayerEnabled() noexcept
	{
		static std::atomic<bool> enabled{true};
		return enabled;
	}
	inline void RaiseShapePreviewMaximum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept
	{
		std::uint64_t current = target.load(std::memory_order_relaxed);
		while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
	}
	inline ShapePreviewCountersSnapshot SnapshotShapePreviewMetrics() noexcept
	{
		const auto& c = ShapePreviewMetrics();
		ShapePreviewCountersSnapshot s;
		s.published = c.published.load(); s.bridgeCoalesced = c.bridgeCoalesced.load();
		s.controllerCoalesced = c.controllerCoalesced.load(); s.executed = c.executed.load();
		s.focusExecuted = c.focusExecuted.load(); s.legacyExecuted = c.legacyExecuted.load(); s.focusFallbacks = c.focusFallbacks.load();
		s.canvasCopies = c.canvasCopies.load(); s.compositionDiscards = c.compositionDiscards.load();
		s.focusDraws = c.focusDraws.load(); s.materializations = c.materializations.load();
		s.focusDrops = c.focusDrops.load(); s.focusUndoDrops = c.focusUndoDrops.load();
		s.maxPerFrame = c.maxPerFrame.load(); s.maxPendingInBridge = c.maxPendingInBridge.load();
		s.latencySamples = c.latencySamples.load(); s.latencyTotalMicroseconds = c.latencyTotalMicroseconds.load();
		s.latencyMaxMicroseconds = c.latencyMaxMicroseconds.load();
		return s;
	}
	inline void ResetShapePreviewMetrics() noexcept
	{
		auto& c = ShapePreviewMetrics();
		for (auto* value : {&c.published, &c.bridgeCoalesced, &c.controllerCoalesced, &c.executed, &c.focusExecuted,
			&c.legacyExecuted, &c.focusFallbacks, &c.canvasCopies, &c.compositionDiscards, &c.focusDraws, &c.materializations,
			&c.focusDrops, &c.focusUndoDrops, &c.maxPerFrame, &c.maxPendingInBridge, &c.latencySamples,
			&c.latencyTotalMicroseconds, &c.latencyMaxMicroseconds})
			value->store(0, std::memory_order_relaxed);
	}

	std::shared_ptr<const ShapeUiSnapshot> ProductShapeUiSnapshot() noexcept;
	Bridge::CommandResult PublishShapeUiAction(const ShapeUiAction&) noexcept;
	// drawing owner发布不可变快照；refresh只调度UI帧，不直接进入窗口/文档锁。
	void PublishShapeUiSnapshot(std::shared_ptr<const ShapeUiSnapshot>) noexcept;
	using ShapeUiRefresh = void(*)(void*) noexcept;
	// 清空callback会等待正在执行的refresh退出；必须在销毁UI context之前调用。
	void SetShapeUiRefreshCallback(ShapeUiRefresh callback, void* context) noexcept;
	// 画布外的焦点丢失（PPT 放映按键/翻页、窗口最小化等）：若当前有编辑框则请求关闭，由绘制线程落定。
	void RequestShapeEditorFocusLoss() noexcept;
}
