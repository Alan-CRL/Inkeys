// DrawingController 私有状态。唯一绘制线程持有；UI只能消费发布出的不可变值。
struct ShapeConstructionRuntime
{
	draw3::shapes::ConstructionSession session;
	draw3::shapes::ToolSpec tool;
	draw3::shapes::ShapeStyle style;
	ShapeUiIdentity identity;
	InkGuid workspaceGuid;
	InkViewport viewport;
	uint64_t revision = 1;
	bool contactActive = false, cancelled = false, completed = false;
	std::optional<draw3::shapes::ShapeObject> preview;
	draw3::shapes::RenderData renderData;
};
struct ShapeEditorRuntime
{
	std::optional<RenderItemId> item;
	std::optional<draw3::shapes::ShapeObject> object, preview;
	ShapeUiIdentity identity, gestureIdentity;
	draw3::shapes::EditSession gesture;
	draw3::shapes::Vec2 down;
	uint32_t handleId = 0;
	bool gestureActive = false;
	// legacyPreview：本编辑不在焦点层（回退开关或已在 L2），沿用旧的权威重放预览路径。
	bool legacyPreview = false;
	DrawingTool tool = DrawingTool::Shape;
	draw3::shapes::ToolSpec toolSpec;
};
// 画布焦点结束原因：所有让编辑框失去焦点的入口都经 EndCanvasFocus(reason) 统一处理。
enum class CanvasFocusEndReason : uint8_t
{
	NewContact, ViewportChange, SceneChange, PageSwitch, ToolChange, SelectionMode, Clear, Undo, Redo,
	Escape, OverlayClosed, PresentationAction, WindowHidden, Resize, DeviceLost, ViewportRefresh, Count
};
// Drop：L2 即将从 CPU 文档整体重建（重建结果已含待落定项），只丢弃 L1；其余一律先落定再执行操作。
enum class CanvasFocusEndAction : uint8_t { Materialize, Drop };
constexpr CanvasFocusEndAction ResolveCanvasFocusEnd(CanvasFocusEndReason reason) noexcept
{
	switch (reason)
	{
	case CanvasFocusEndReason::PageSwitch: case CanvasFocusEndReason::Resize:
	case CanvasFocusEndReason::DeviceLost: case CanvasFocusEndReason::ViewportRefresh:
		return CanvasFocusEndAction::Drop;
	default: return CanvasFocusEndAction::Materialize;
	}
}
// L2 尚未包含的项（覆盖率只在 L1）；文档/journal/自动保存在抬笔时已照常记录。
struct PendingL2Item
{
	RenderItemId item;
	draw3::shapes::ObjectId id{};
	InkGuid page;
	std::size_t pageIndex = 0;
	InkRasterStateToken beforeState = 0, afterState = 0;
};
// 画布焦点会话：待落定项集合必须是所属页 Z 序的可见后缀（当前架构最多一项），
// 因而直接叠在 L2 之上上屏即层序正确，不需要任何读取方合成 L1。
struct CanvasFocusSession
{
	std::vector<PendingL2Item> pending;
	RECT l1Bounds{};
	bool redraw = false; // L1 被共享层重建清空后需重画。
	bool Active() const noexcept { return !pending.empty(); }
	const PendingL2Item* Find(RenderItemId item) const noexcept
	{
		for (const auto& entry : pending) if (entry.item == item) return &entry;
		return nullptr;
	}
	static bool IsVisibleSuffix(const CanvasRuntimeHistory& history, std::span<const PendingL2Item> items) noexcept
	{
		if (items.empty()) return true;
		std::size_t first = history.Items().size();
		for (const auto& entry : items)
		{
			const auto* state = history.Find(entry.item);
			if (!state || !state->visible) return false;
			first = (std::min)(first,static_cast<std::size_t>(entry.item.index));
		}
		for (std::size_t index = first; index < history.Items().size(); ++index)
		{
			const auto& state = history.Items()[index];
			if (!state.visible) continue;
			if (std::none_of(items.begin(),items.end(),[&](const PendingL2Item& entry) { return entry.item == state.id; })) return false;
		}
		return true;
	}
};
struct ShapeUiRuntime
{
	CanvasFocusSession focus;
	std::array<uint64_t,static_cast<std::size_t>(CanvasFocusEndReason::Count)> focusEndCounts{};
	ShapeEditorRuntime editor;
	std::shared_ptr<ShapeConstructionRuntime> construction;
	std::shared_ptr<const ShapeUiSnapshot> published;
	uint64_t nextSession = 1, generation = 0, documentGeneration = 1;
	bool constructionVisualDirty = false;
	uint64_t displayGeneration = 1, observedDisplayRevision = ~uint64_t{};
	int observedOriginX = 0, observedOriginY = 0, observedWidth = -1, observedHeight = -1;
	float observedScale = 0;
	std::optional<RenderItemId> pendingItem;
	std::optional<draw3::shapes::ShapeObject> pendingObject;
	InkGuid pendingPage;
	std::size_t pendingPageIndex = 0;
	int64_t pendingCompletedQpc = 0;
};
bool SameShapeUiScene(const ShapeUiIdentity& a, const ShapeUiIdentity& b) noexcept
{
	return a.documentGeneration == b.documentGeneration && a.displayGeneration == b.displayGeneration &&
		a.pageId == b.pageId && a.workspace == b.workspace && a.pageIndex == b.pageIndex;
}
bool SameShapeUiContents(const ShapeUiSnapshot& a, const ShapeUiSnapshot& b)
{
	return a.identity.session == b.identity.session && SameShapeUiScene(a.identity,b.identity) &&
		a.identity.objectId == b.identity.objectId && a.identity.objectRevision == b.identity.objectRevision &&
		a.object == b.object && a.handles == b.handles && a.bounds == b.bounds && a.frameRect == b.frameRect &&
		a.editGestureActive == b.editGestureActive && a.displayScale == b.displayScale &&
		a.canvasOriginX == b.canvasOriginX && a.canvasOriginY == b.canvasOriginY &&
		a.construction.active == b.construction.active && a.construction.step == b.construction.step &&
		a.construction.confirmedSteps == b.construction.confirmedSteps && a.construction.preset == b.construction.preset &&
		a.construction.canComplete == b.construction.canComplete && a.construction.canBack == b.construction.canBack &&
		a.construction.finished == b.construction.finished;
}
void PublishShapeRuntimeSnapshot(ShapeUiRuntime& runtime, ShapeUiSnapshot snapshot)
{
	if (runtime.published && SameShapeUiContents(*runtime.published,snapshot)) return;
	if (++runtime.generation == 0) ++runtime.generation;
	snapshot.identity.generation = runtime.generation;
	runtime.published = std::make_shared<const ShapeUiSnapshot>(std::move(snapshot));
	PublishShapeUiSnapshot(runtime.published);
}

bool ShapeEditorActionIdentityMatches(const ShapeEditorRuntime& editor,const ShapeUiSnapshot* published,
	const ShapeUiAction& action,const ShapeUiIdentity& scene)
{
	if (!published || !SameShapeUiScene(action.identity,scene)) return false;
	if (action.kind == ShapeUiActionKind::CloseEditor)
	{
		// Close是会话级终止：排队中的Commit可先推进修订，但不能令同会话Esc失效。
		return editor.object && action.identity.session != 0 && action.identity.session == editor.identity.session &&
			SameShapeUiScene(editor.identity,scene) && action.identity.objectId == editor.object->id;
	}
	const bool gesture = editor.gestureActive && (action.kind == ShapeUiActionKind::PreviewEdit ||
		action.kind == ShapeUiActionKind::CommitEdit || action.kind == ShapeUiActionKind::CancelEdit);
	return action.identity == (gesture ? editor.gestureIdentity : published->identity);
}
bool ShapeConstructionActionIdentityMatches(const ShapeConstructionRuntime& construction,const ShapeUiSnapshot* published,
	const ShapeUiAction& action,const ShapeUiIdentity& scene)
{
	if (!published || !SameShapeUiScene(action.identity,scene) || action.identity.session != construction.identity.session ||
		action.identity.objectId != construction.identity.objectId) return false;
	return action.kind == ShapeUiActionKind::ConstructionCancel ||
		(!construction.contactActive && action.identity == published->identity);
}

bool ShapeCommandChangesScene(const CanvasCommand& command,Bridge::Workspace workspace,std::size_t pageIndex,
	const std::optional<Bridge::PresentationTarget>& presentation)
{
	// Host每条scene-stamped命令前都会重复投递当前场景；重复声明不能结束编辑/构造。
	if (command.type == CanvasCommandType::SetWorkspace)
		return command.workspace != static_cast<uint8_t>(workspace) ||
			(workspace == Bridge::Workspace::Presentation && presentation.has_value());
	if (command.type == CanvasCommandType::SetPresentationTarget)
		return command.presentationTarget && (workspace != Bridge::Workspace::Presentation || !presentation || *presentation != *command.presentationTarget);
	if (command.type == CanvasCommandType::SetPage) return command.pageIndex != pageIndex;
	return command.type == CanvasCommandType::NextPage || command.type == CanvasCommandType::PreviousPage;
}
bool CanRedoClearAfterOperations(const CanvasPageRuntimeState& runtime) noexcept
{
	return runtime.clearRedoAvailable && !runtime.history.LastRedoItem() &&
		(!runtime.operationsInitialized || runtime.operationCursor >= runtime.operations.size());
}

// 绘制线程最新预览槽：同一手势只执行最后一条预览，且每帧最多执行一次。
// 终结意图仍按 FIFO 处理；同手势 Commit/Cancel 携带最终点，可直接取代尚未执行的预览。
struct ShapePreviewSlot
{
	std::shared_ptr<const ShapeUiAction> held;
	unsigned executedThisFrame = 0;
	uint64_t coalesced = 0;
	void BeginFrame() noexcept { executedThisFrame = 0; }
	// 预览收进槽内；返回值是必须先执行的另一手势旧预览。
	std::shared_ptr<const ShapeUiAction> AcceptPreview(std::shared_ptr<const ShapeUiAction> preview) noexcept
	{
		std::shared_ptr<const ShapeUiAction> flush;
		if (held && preview && SameShapeEditGesture(*held,*preview)) ++coalesced;
		else flush = std::move(held);
		held = std::move(preview);
		return flush;
	}
	// 其他命令到达前：同场景重复声明不打断；同手势终结丢弃旧预览；其余命令前先执行旧预览保序。
	std::shared_ptr<const ShapeUiAction> BeforeCommand(const ShapeUiAction* shape, bool transparent) noexcept
	{
		if (!held || transparent) return {};
		if (shape && (shape->kind == ShapeUiActionKind::CommitEdit || shape->kind == ShapeUiActionKind::CancelEdit) &&
			SameShapeEditGesture(*held,*shape)) { held.reset(); ++coalesced; return {}; }
		return std::exchange(held,{});
	}
	// 批次末：本帧尚未执行预览才取出；否则留到下一帧，避免同帧重复整套预览。
	std::shared_ptr<const ShapeUiAction> AtBatchEnd() noexcept
	{
		if (!held || executedThisFrame) return {};
		return std::exchange(held,{});
	}
};
