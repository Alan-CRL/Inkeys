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
	DrawingTool tool = DrawingTool::Shape;
	draw3::shapes::ToolSpec toolSpec;
};
struct ShapeUiRuntime
{
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
		a.object == b.object && a.handles == b.handles && a.bounds == b.bounds &&
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
