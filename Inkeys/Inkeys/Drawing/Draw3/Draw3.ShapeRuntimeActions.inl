// 在 DrawingController::Run 内引入，保持所有文档与GPU操作在唯一绘制线程。
auto shapeScene = [&]()
{
	ShapeUiSnapshot snapshot; const auto visual = window_.ProductVisualStyleSnapshot();
	snapshot.identity.documentGeneration = shapeUi.documentGeneration;

	snapshot.identity.workspace = static_cast<uint8_t>(activeWorkspace); snapshot.identity.pageIndex = currentPageIndex_;
	snapshot.displayScale = ProductTargetScale(visual);
	POINT origin{}; if (window_.Handle()) ClientToScreen(window_.Handle(),&origin);
	if (document_) if (const auto* page = document_->PageAt(currentPageIndex_))
	{
		snapshot.identity.pageId = page->PageGuid().Bytes();
		if (const auto* canvas = page->FindCanvas(kDefaultDeviceKey))
		{ origin.x -= static_cast<LONG>(std::lround(canvas->Viewport().x)); origin.y -= static_cast<LONG>(std::lround(canvas->Viewport().y)); }
	}
	snapshot.canvasOriginX = origin.x; snapshot.canvasOriginY = origin.y;
	const auto size = window_.Size();
	if (shapeUi.observedDisplayRevision != visual.penWidthContext.revision || shapeUi.observedOriginX != origin.x ||
		shapeUi.observedOriginY != origin.y || shapeUi.observedWidth != size.width || shapeUi.observedHeight != size.height ||
		shapeUi.observedScale != snapshot.displayScale)
	{
		if (++shapeUi.displayGeneration == 0) ++shapeUi.displayGeneration;
		shapeUi.observedDisplayRevision = visual.penWidthContext.revision; shapeUi.observedOriginX = origin.x; shapeUi.observedOriginY = origin.y;
		shapeUi.observedWidth = size.width; shapeUi.observedHeight = size.height; shapeUi.observedScale = snapshot.displayScale;
	}
	snapshot.identity.displayGeneration = shapeUi.displayGeneration; return snapshot;
};
auto nextShapeRevision = [&](const draw3::shapes::ObjectId& id,uint64_t observed) -> uint64_t
{
	if (currentPageIndex_ >= pageRuntimeStates.size()) return 0;
	return pageRuntimeStates[currentPageIndex_].shapeRevisionAllocator.Next(id,observed);
};
auto shapeBoundsRect = [&](const draw3::shapes::Bounds& bounds) -> RECT
{
	if (!bounds.valid) return {};
	const auto size = window_.Size();
	return ClampRectToCanvas({SaturatingFloorToLong(bounds.left),SaturatingFloorToLong(bounds.top),
		SaturatingCeilToLong(bounds.right),SaturatingCeilToLong(bounds.bottom)},size.width,size.height);
};
RECT betweenShapeBounds{};
// Preview也在原RenderItem层位重放；仅复制低频图形编辑候选，普通画笔仍走原追加热路径。
auto replayShapeEdit = [&](const std::optional<draw3::shapes::ShapeObject>& after,RECT& dirty,bool commit) -> bool
{
	if (!shapeUi.editor.item || !shapeUi.editor.object || !document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
	auto* page = document_->PageAt(currentPageIndex_); auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
	if (!canvas) return false;
	auto& runtime = pageRuntimeStates[currentPageIndex_]; const auto* original = runtime.history.Find(*shapeUi.editor.item);
	if (!original || !original->visible || original->strokeIndex >= canvas->Strokes().size() ||
		canvas->Strokes()[original->strokeIndex].Shape() != shapeUi.editor.object) return false;
	InkCanvas candidate = *canvas; CanvasPageRuntimeState candidateRuntime = runtime;
	std::vector<SignedTileCoordinate> tiles = original->compositionTiles;
	if (shapeUi.editor.preview)
	{
		InkStroke oldPreview({StoredInkType::Shape,shapeUi.editor.preview->style.strokeRgb,shapeUi.editor.preview->style.strokeOpacity,0},{},std::nullopt,*shapeUi.editor.preview);
		if (auto oldFootprint = BuildStrokeTileFootprint(oldPreview)) tiles.insert(tiles.end(),oldFootprint->compositionTiles.begin(),oldFootprint->compositionTiles.end());
	}
	if (after)
	{
		InkStroke replacement({StoredInkType::Shape,after->style.strokeRgb,after->style.strokeOpacity,0},{},std::nullopt,*after);
		auto footprint = BuildStrokeTileFootprint(replacement); if (!footprint || !replacement.IsValid()) return false;
		tiles.insert(tiles.end(),footprint->compositionTiles.begin(),footprint->compositionTiles.end());
		if (!candidate.ReplaceStroke(original->strokeIndex,std::move(replacement)) ||
			!candidateRuntime.history.UpdateItemGeometry(*shapeUi.editor.item,std::move(*footprint))) return false;
	}
	std::sort(tiles.begin(),tiles.end()); tiles.erase(std::unique(tiles.begin(),tiles.end()),tiles.end());
	const auto size = window_.Size(); const auto viewport = canvas->Viewport();
	const HistoryCanvasIdentity identity{page->PageGuid(),kDefaultDeviceKey}; const auto key = currentRasterKey();
	const auto metricsBefore = metricsState_ ? metricsSignature(size.width,size.height) : ContentMetricRasterSignature{};
	if (metricsState_) metricsState_->BeginLocalWrite(metricsBefore);
	historyGpuCache.DiscardHotPreimages(); historyGpuCache.DiscardCompositionCache(); compositionMaintenance.clear();
	renderer_.InvalidateTrustedL2Snapshot(); trustedSnapshotSignatureValid = false;
	const CompositionRestoreRequest request{identity,key,&candidate,&candidateRuntime.history,tiles,
		candidateRuntime.history.Items().size(),viewport.x,viewport.y,size.width,size.height,true};
	const bool attempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
	const auto restored = historyGpuCache.RestoreComposition(request);
	if (metricsState_) metricsState_->RasterReturned(attempt,restored.path != CompositionRestorePath::Failed,!IsEmptyRect(restored.dirty));
	UnionRectInPlace(dirty,restored.dirty);
	bool succeeded = restored.path != CompositionRestorePath::Failed;
	if (succeeded && commit && after)
		succeeded = CommitRuntimeShapeEditCpu(*canvas,runtime,*shapeUi.editor.item,*shapeUi.editor.object,*after,allocateRasterStateToken).has_value();
	if (!succeeded)
	{
		historyGpuCache.DiscardCompositionCache(); renderer_.ClearShapeRenderCache();
		const CompositionRestoreRequest rollback{identity,key,canvas,&runtime.history,tiles,runtime.history.Items().size(),viewport.x,viewport.y,size.width,size.height,true};
		const bool retry = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
		const auto recovered = historyGpuCache.RestoreComposition(rollback);
		if (metricsState_) metricsState_->RasterReturned(retry,recovered.path != CompositionRestorePath::Failed,!IsEmptyRect(recovered.dirty));
		UnionRectInPlace(dirty,recovered.dirty); shapeUi.editor.preview.reset();
		viewportVisibleClear = false; viewportRefreshPending = true; viewportRefreshClearsTransient = false;
		return false;
	}
	renderer_.ClearOperatorLayer(renderer_.layerL0); renderer_.ClearOperatorLayer(renderer_.layerL1);
	if (commit && after)
	{
		shapeUi.editor.object = *after; shapeUi.editor.preview.reset(); markPresentationMutation();
		// 编辑即使不改变hasContent，也要发出真实内容修订，冷页/保存不能误判为未变。
		if (++currentContentRevision_ == 0) ++currentContentRevision_;
		contentRevisionNeedsPresent = true;
		if (observer_.currentPageContentChanged) observer_.currentPageContentChanged(observer_.context,currentPageHasContent(),currentContentRevision_);
		publishCurrentPageContent();
	}
	else shapeUi.editor.preview = after;
	viewportVisibleClear = CanvasVisibleClarityAfterAuthoritativeWrite(viewportVisibleClear,true);
	if (metricsState_)
	{
		metricsState_->PruneStored(*canvas,runtime); metricsState_->InvalidateLiveRasters(); metricsState_->renderAttempt = true;
		if (commit) metricsState_->CompleteLocalWrite(metricsBefore,metricsSignature(size.width,size.height),true);
		else metricsState_->InvalidateL2(); // 预览不构成文档权威像素。
	}
	return true;
};
closeShapeSessions = [&](RECT& dirty,bool restore)
{
	if (restore && shapeUi.editor.preview) (void)replayShapeEdit(std::nullopt,dirty,false);
	shapeUi.editor = {}; shapeUi.pendingObject.reset(); shapeUi.pendingItem.reset(); shapeUi.pendingCompletedQpc = 0;
	if (shapeUi.construction)
	{
		UnionRectInPlace(dirty,shapeBoundsRect(shapeUi.construction->renderData.bounds));
		shapeUi.construction->session.Cancel(); shapeUi.construction->cancelled = true; shapeUi.construction.reset();
		shapeUi.constructionVisualDirty = true;
	}
	UnionRectInPlace(dirty,betweenShapeBounds); betweenShapeBounds = {};
	renderer_.ClearOperatorLayer(renderer_.layerL0);
};
auto publishShapeUi = [&]()
{
	auto snapshot = shapeScene();
	if (active.empty() && shapeUi.pendingObject && shapeUi.pendingItem && document_)
	{
		const auto* page = document_->PageAt(currentPageIndex_);
		if (page && page->PageGuid() == shapeUi.pendingPage && currentPageIndex_ == shapeUi.pendingPageIndex)
		{
			shapeUi.editor = {}; shapeUi.editor.item = shapeUi.pendingItem; shapeUi.editor.object = shapeUi.pendingObject;
			shapeUi.editor.identity = snapshot.identity; shapeUi.editor.identity.session = shapeUi.nextSession++;
			shapeUi.editor.tool = window_.ActiveTool(); shapeUi.editor.toolSpec = window_.ProductVisualStyleSnapshot().shapeTool;
		}
		shapeUi.pendingObject.reset(); shapeUi.pendingItem.reset();
	}
	if (shapeUi.construction && shapeUi.construction->session.Status().active)
	{
		snapshot.identity.session = shapeUi.construction->identity.session;
		snapshot.identity.objectId = shapeUi.construction->identity.objectId;
		snapshot.construction = shapeUi.construction->session.Status();
		if (shapeUi.construction->contactActive) { snapshot.construction.canBack = false; snapshot.construction.canComplete = false; }
	}
	else if (active.empty() && shapeUi.editor.object)
	{
		snapshot.object = shapeUi.editor.preview ? shapeUi.editor.preview : shapeUi.editor.object;
		snapshot.identity.session = shapeUi.editor.identity.session;
		snapshot.identity.objectId = snapshot.object->id; snapshot.identity.objectRevision = snapshot.object->revision;
		snapshot.editGestureActive = shapeUi.editor.gestureActive;
		if (shapeUi.published && shapeUi.published->object == snapshot.object && shapeUi.published->displayScale == snapshot.displayScale)
		{ snapshot.bounds = shapeUi.published->bounds; snapshot.handles = shapeUi.published->handles; }
		else
		{
			draw3::shapes::GeometryResult geometry;
			if (draw3::shapes::BuildGeometry(*snapshot.object,geometry,{snapshot.displayScale,.25f})) snapshot.bounds = geometry.bounds;
			snapshot.handles = draw3::shapes::BuildEditHandles(*snapshot.object,{snapshot.displayScale,.25f});
		}
	}
	PublishShapeRuntimeSnapshot(shapeUi,std::move(snapshot));
};
auto updateConstructionPreview = [&](ShapeConstructionRuntime& state)
{
	const bool result = RefreshConstructionRuntimePreview(state,nextShapeRevision);
	shapeUi.constructionVisualDirty = true; return result;
};
auto commitShapeConstruction = [&](draw3::shapes::ShapeObject object,RECT& dirty) -> bool
{
	if (!document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
	auto* page = document_->PageAt(currentPageIndex_); auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
	if (!canvas) return false;
	auto& runtime = pageRuntimeStates[currentPageIndex_]; const auto viewport = canvas->Viewport();
	object.revision = nextShapeRevision(object.id,object.revision); if (!object.revision) return false;
	object.transform.dx += viewport.x; object.transform.dy += viewport.y;
	InkCanvas candidate = *canvas; CanvasPageRuntimeState candidateRuntime = runtime;
	const auto created = AppendRuntimeConstructedShapeCpu(candidate,candidateRuntime,object,allocateRasterStateToken);
	if (!created) return false;
	const auto item = created->renderItem;
	const auto* createdItem = candidateRuntime.history.Find(item); if (!createdItem) return false;
	const auto& tiles = createdItem->compositionTiles;
	const auto size = window_.Size(); const HistoryCanvasIdentity identity{page->PageGuid(),kDefaultDeviceKey};
	historyGpuCache.DiscardCompositionCache(); renderer_.InvalidateTrustedL2Snapshot(); trustedSnapshotSignatureValid = false;
	const CompositionRestoreRequest request{identity,currentRasterKey(),&candidate,&candidateRuntime.history,tiles,
		candidateRuntime.history.Items().size(),viewport.x,viewport.y,size.width,size.height,true};
	const auto restored = historyGpuCache.RestoreComposition(request); UnionRectInPlace(dirty,restored.dirty);
	if (restored.path == CompositionRestorePath::Failed)
	{
		historyGpuCache.DiscardCompositionCache();
		const CompositionRestoreRequest rollback{identity,currentRasterKey(),canvas,&runtime.history,tiles,
			runtime.history.Items().size(),viewport.x,viewport.y,size.width,size.height,true};
		const auto recovered = historyGpuCache.RestoreComposition(rollback); UnionRectInPlace(dirty,recovered.dirty);
		viewportVisibleClear = false; viewportRefreshPending = true; viewportRefreshClearsTransient = false; return false;
	}
	*canvas = std::move(candidate); runtime = std::move(candidateRuntime);
	renderer_.ClearOperatorLayer(renderer_.layerL0); renderer_.ClearOperatorLayer(renderer_.layerL1);
	shapeUi.pendingObject = std::move(object); shapeUi.pendingItem = item; shapeUi.pendingPage = page->PageGuid(); shapeUi.pendingPageIndex = currentPageIndex_;
	markPresentationMutation(); publishCurrentPageContent();
	if (observer_.strokeCompleted) observer_.strokeCompleted(observer_.context,Bridge::CompletedStrokeKind::Shape);
	if (metricsState_) { metricsState_->PruneStored(*canvas,runtime); metricsState_->InvalidateLiveRasters(); metricsState_->InvalidateL2(); }
	return true;
};
beginShapeContact = [&](RuntimeStroke& runtime)
{
	// 空白落笔直接交给原画布；只关闭当前编辑，不搜索任何旧对象。
	if (shapeUi.editor.object)
	{
		RECT ignored{}; if (shapeUi.editor.preview) (void)replayShapeEdit(std::nullopt,ignored,false);
		shapeUi.editor = {}; window_.RequestFullPresent();
	}
	if (active.empty()) { shapeUi.pendingObject.reset(); shapeUi.pendingItem.reset(); shapeUi.pendingCompletedQpc = 0; }
	if (runtime.shape.generic) runtime.shape.nextRevision = nextShapeRevision;
	const bool multi = runtime.shape.generic && draw3::shapes::GetToolCapabilities(runtime.shape.tool.preset).multiStage;
	if (!multi)
	{
		if (shapeUi.construction) { RECT ignored{}; closeShapeSessions(ignored,false); renderer_.ClearOperatorLayer(renderer_.layerL0); window_.RequestFullPresent(); }
		return true;
	}
	const auto scene = shapeScene();
	if (shapeUi.construction && (!SameShapeUiScene(scene.identity,shapeUi.construction->identity) ||
		shapeUi.construction->tool != runtime.shape.tool))
	{
		shapeUi.construction->session.Cancel(); shapeUi.construction->cancelled = true; shapeUi.construction.reset();
	}
	if (!shapeUi.construction)
	{
		auto state = std::make_shared<ShapeConstructionRuntime>(); state->identity = scene.identity;
		state->identity.session = shapeUi.nextSession++; state->identity.objectId = runtime.shape.id;
		state->tool = runtime.shape.tool; state->style = runtime.shape.style; state->workspaceGuid = runtime.ownerWorkspaceGuid;
		state->viewport = runtime.viewport;
		if (!state->session.Begin(state->tool,state->style,runtime.shape.id)) return false;
		shapeUi.construction = std::move(state);
	}
	auto& state = *shapeUi.construction;
	if (state.contactActive || state.cancelled) return false; // 一个构造会话只接受一根正在绘制的物理接触。
	state.contactActive = true; runtime.shape.construction = shapeUi.construction;
	runtime.shape.id = state.identity.objectId; runtime.shape.tool = state.tool; runtime.shape.style = state.style;
	runtime.shape.revision = state.revision; return true;
};
finishShapeContact = [&](RuntimeStroke& runtime)
{
	if (!runtime.shape.construction) return;
	ConfirmRuntimeConstructionContact(runtime);
	if (runtime.shape.construction->cancelled || runtime.shape.construction->completed)
		if (shapeUi.construction == runtime.shape.construction) shapeUi.construction.reset();
	shapeUi.constructionVisualDirty = true;
};
auto backShapeConstruction = [&](RECT& dirty)
{
	if (!shapeUi.construction) return;
	UnionRectInPlace(dirty,shapeBoundsRect(shapeUi.construction->renderData.bounds));
	if (shapeUi.construction->session.Back()) (void)updateConstructionPreview(*shapeUi.construction);
	shapeUi.constructionVisualDirty = true; publishShapeUi();
};
auto processShapeUiAction = [&](const ShapeUiAction& action,RECT& dirty)
{
	const auto scene = shapeScene();
	if (!shapeUi.published || !SameShapeUiScene(action.identity,scene.identity)) return;
	if (action.kind == ShapeUiActionKind::CloseEditor && shapeUi.construction &&
		action.identity.session == shapeUi.construction->identity.session && action.identity.objectId == shapeUi.construction->identity.objectId)
	{ closeShapeSessions(dirty,false); publishShapeUi(); return; }
	const bool constructionAction = action.kind == ShapeUiActionKind::ConstructionComplete ||
		action.kind == ShapeUiActionKind::ConstructionBack || action.kind == ShapeUiActionKind::ConstructionCancel;
	if (constructionAction)
	{
		if (!shapeUi.construction || !ShapeConstructionActionIdentityMatches(*shapeUi.construction,shapeUi.published.get(),action,scene.identity)) return;
		if (action.kind == ShapeUiActionKind::ConstructionBack) backShapeConstruction(dirty);
		else if (action.kind == ShapeUiActionKind::ConstructionCancel) closeShapeSessions(dirty,false);
		else
		{
			const auto saved = shapeUi.construction->session;
			if (auto object = shapeUi.construction->session.Complete())
			{
				if (commitShapeConstruction(std::move(*object),dirty)) { shapeUi.construction.reset(); shapeUi.constructionVisualDirty = true; }
				else shapeUi.construction->session = saved;
			}
		}
		publishShapeUi(); return;
	}
	if (!shapeUi.editor.object || !shapeUi.editor.item) return;
	const bool inGesture = shapeUi.editor.gestureActive && (action.kind == ShapeUiActionKind::PreviewEdit ||
		action.kind == ShapeUiActionKind::CommitEdit || action.kind == ShapeUiActionKind::CancelEdit);
	if (!ShapeEditorActionIdentityMatches(shapeUi.editor,shapeUi.published.get(),action,scene.identity)) return;
	if (action.kind == ShapeUiActionKind::CloseEditor)
	{ closeShapeSessions(dirty,true); publishShapeUi(); return; }
	if (action.kind == ShapeUiActionKind::BeginEdit)
	{
		if (shapeUi.editor.gestureActive) return;
		if (!shapeUi.editor.gesture.Begin(*shapeUi.editor.object,action.handleId,action.down,{scene.displayScale,.25f})) return;
		shapeUi.editor.gestureIdentity = action.identity; shapeUi.editor.down = action.down;
		shapeUi.editor.handleId = action.handleId; shapeUi.editor.gestureActive = true; publishShapeUi(); return;
	}
	if (action.kind == ShapeUiActionKind::CancelEdit)
	{
		if (shapeUi.editor.preview) (void)replayShapeEdit(std::nullopt,dirty,false);
		shapeUi.editor.gesture.Cancel(); shapeUi.editor.gestureActive = false; publishShapeUi(); return;
	}
	std::optional<draw3::shapes::ShapeObject> candidate;
	const auto revision = nextShapeRevision(shapeUi.editor.object->id,shapeUi.editor.object->revision);
	if (!revision) return;
	if (action.kind == ShapeUiActionKind::SetStyle && !shapeUi.editor.gestureActive)
		candidate = draw3::shapes::RestyleShape(*shapeUi.editor.object,action.style,revision,{scene.displayScale,.25f});
	else if (action.kind == ShapeUiActionKind::SetSides && !shapeUi.editor.gestureActive)
		candidate = draw3::shapes::SetRegularSides(*shapeUi.editor.object,action.sides,revision);
	else if (inGesture && (action.kind == ShapeUiActionKind::PreviewEdit || action.kind == ShapeUiActionKind::CommitEdit))
	{
		if (shapeUi.editor.gesture.Preview(action.point,revision)) candidate = shapeUi.editor.gesture.Candidate();
	}
	const bool commit = action.kind != ShapeUiActionKind::PreviewEdit;
	if (candidate)
	{
		if (!replayShapeEdit(candidate,dirty,commit)) shapeUi.editor.gestureActive = false;
	}
	else if (commit && shapeUi.editor.preview) (void)replayShapeEdit(std::nullopt,dirty,false);
	if (commit) { shapeUi.editor.gesture.Cancel(); shapeUi.editor.gestureActive = false; }
	publishShapeUi();
};
auto validateShapeUiContext = [&](RECT& dirty)
{
	const auto scene = shapeScene(); const auto tool = window_.ActiveTool(); const auto visual = window_.ProductVisualStyleSnapshot();
	bool invalid = window_.SelectionMode();
	if (shapeUi.editor.object) invalid |= !SameShapeUiScene(shapeUi.editor.identity,scene.identity) || tool != shapeUi.editor.tool ||
		(tool == DrawingTool::Shape && visual.shapeTool != shapeUi.editor.toolSpec);
	if (shapeUi.construction) invalid |= !SameShapeUiScene(shapeUi.construction->identity,scene.identity) ||
		tool != DrawingTool::Shape || visual.shapeTool != shapeUi.construction->tool;
	if (invalid && (shapeUi.editor.object || shapeUi.construction))
	{ closeShapeSessions(dirty,true); renderer_.ClearOperatorLayer(renderer_.layerL0); window_.RequestFullPresent(); }
};
auto renderPendingShapeConstruction = [&](RECT& dirty,bool& failed,bool layersRebuilt)
{
	const bool waiting = shapeUi.construction && shapeUi.construction->session.Status().active && !shapeUi.construction->contactActive;
	if (!waiting && IsEmptyRect(betweenShapeBounds)) return;
	if (!layersRebuilt && !shapeUi.constructionVisualDirty) return;
	UnionRectInPlace(dirty,betweenShapeBounds);
	if (active.empty()) renderer_.ClearOperatorLayer(renderer_.layerL0);
	betweenShapeBounds = {};
	if (waiting && shapeUi.construction->preview)
	{
		renderer_.SetOperatorTarget(renderer_.layerL0);
		if (!renderer_.DrawShapeRenderData(shapeUi.construction->renderData)) failed = true;
		betweenShapeBounds = shapeBoundsRect(shapeUi.construction->renderData.bounds); UnionRectInPlace(dirty,betweenShapeBounds);
	}
	shapeUi.constructionVisualDirty = false;
};

// 物理接触还未Up时先只处理取消屏障；文档命令按原FIFO留到active清空后执行。
std::deque<CanvasCommand> shapeDeferredCommands;
auto drainActiveShapeControlBarrier = [&](RECT& dirty)
{
	if (active.empty() || !shapeUi.construction || !window_.HasPendingCanvasCommand()) return;
	CanvasCommand pending;
	while (window_.TryDequeueCanvasCommand(pending))
	{
		const bool cancellingAction = pending.type == CanvasCommandType::ShapeInteraction && pending.shapeAction &&
			(pending.shapeAction->kind == ShapeUiActionKind::ConstructionCancel || pending.shapeAction->kind == ShapeUiActionKind::CloseEditor);
		const bool sceneBarrier = pending.type == CanvasCommandType::Clear ||
			ShapeCommandChangesScene(pending,activeWorkspace,currentPageIndex_,activePresentationTarget) || pending.type == CanvasCommandType::PrepareExitAutoSave;
		if (pending.type == CanvasCommandType::Undo && shapeUi.construction)
		{
			if (shapeUi.construction->session.Status().canBack)
			{
				for (auto* runtime : active) if (runtime && runtime->shape.construction == shapeUi.construction)
					runtime->shape.constructionStepDiscarded = true;
				backShapeConstruction(dirty);
			}
		}
		else if (cancellingAction) processShapeUiAction(*pending.shapeAction,dirty);
		else
		{
			if (sceneBarrier && shapeUi.construction) closeShapeSessions(dirty,false);
			shapeDeferredCommands.push_back(std::move(pending));
		}
	}
};
