// 在 DrawingController::Run 内引入，保持所有文档与GPU操作在唯一绘制线程。
auto shapeScene = [&]()
{
	ShapeUiSnapshot snapshot; const auto visual = window_.ProductVisualStyleSnapshot();
	snapshot.identity.documentGeneration = shapeUi.documentGeneration;
	snapshot.finishedEditingSession = shapeUi.finishedEditingSession; snapshot.finishedEditingEntryEpoch = shapeUi.finishedEditingEntryEpoch; snapshot.finishedEditingWorkspace = shapeUi.finishedEditingWorkspace;

	snapshot.identity.workspace = static_cast<uint8_t>(activeWorkspace); snapshot.identity.pageIndex = currentPageIndex_;
	snapshot.displayScale = ProductTargetScale(visual);
	snapshot.predictionMode = static_cast<uint8_t>(DecodeGeneralDrawingOptions(generalDrawingOptions_.load(std::memory_order_acquire)).predictionMode);
	snapshot.timingProfile = configuration_.timingProfile.target_fps <= 30 ? 0 : configuration_.timingProfile.target_fps <= 60 ? 1 : configuration_.timingProfile.target_fps <= 120 ? 2 : 3;
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
// 拖动预览吞吐：槽、上屏延迟起点与单手势统计只由唯一绘制线程访问。
ShapePreviewSlot shapePreviewSlot;
int64_t shapePreviewLatencyStart = 0;
struct ShapeGestureStats
{
	uint64_t restoreBase = 0, previews = 0, focus = 0, legacy = 0, latencySamples = 0, latencyTotal = 0, latencyMax = 0;
	ShapePreviewCountersSnapshot base;
	bool fallbackLogged = false;
} shapeGestureStats;
auto recordShapePreviewPresented = [&]()
{
	if (!shapePreviewLatencyStart) return;
	const auto elapsed = ShapePreviewClockMicroseconds() - shapePreviewLatencyStart; shapePreviewLatencyStart = 0;
	if (elapsed < 0) return;
	const auto value = static_cast<uint64_t>(elapsed); auto& metrics = ShapePreviewMetrics();
	metrics.latencySamples.fetch_add(1,std::memory_order_relaxed); metrics.latencyTotalMicroseconds.fetch_add(value,std::memory_order_relaxed);
	RaiseShapePreviewMaximum(metrics.latencyMaxMicroseconds,value);
	++shapeGestureStats.latencySamples; shapeGestureStats.latencyTotal += value; shapeGestureStats.latencyMax = (std::max)(shapeGestureStats.latencyMax,value);
};
auto logShapeGestureSummary = [&](const char* result)
{
	if (!shapeGestureStats.previews) return;
	// 仅在诊断输出已开启时格式化；统计无论是否输出都随手势结束清零。
	if (!DiagnosticOutput::Available()) { shapeGestureStats = {}; return; }
	const auto now = SnapshotShapePreviewMetrics(); const auto& base = shapeGestureStats.base;
	DiagnosticOutput::Printf(DiagnosticOutput::Stream::Output,
		"[Draw3.ShapeEdit] action=gesture_end result=%s previews=%llu focus=%llu legacy=%llu tile_replays=%llu coalesced_bridge=%llu coalesced_draw=%llu latency_avg_us=%llu latency_max_us=%llu\n",
		result,static_cast<unsigned long long>(shapeGestureStats.previews),static_cast<unsigned long long>(shapeGestureStats.focus),
		static_cast<unsigned long long>(shapeGestureStats.legacy),
		static_cast<unsigned long long>(historyGpuCache.CompositionRestoreCount() - shapeGestureStats.restoreBase),static_cast<unsigned long long>(now.bridgeCoalesced - base.bridgeCoalesced),
		static_cast<unsigned long long>(now.controllerCoalesced - base.controllerCoalesced),
		static_cast<unsigned long long>(shapeGestureStats.latencySamples ? shapeGestureStats.latencyTotal / shapeGestureStats.latencySamples : 0),
		static_cast<unsigned long long>(shapeGestureStats.latencyMax));
	shapeGestureStats = {};
};
// 旧路径（回退开关或不在焦点层的编辑）：Preview 也在原 RenderItem 层位重放，复制候选并清缓存。
auto replayShapeEdit = [&](const std::optional<draw3::shapes::ShapeObject>& after,RECT& dirty,bool commit) -> bool
{
	if (!shapeUi.editor.item || !shapeUi.editor.object || !document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
	auto* page = document_->PageAt(currentPageIndex_); auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
	if (!canvas) return false;
	auto& runtime = pageRuntimeStates[currentPageIndex_]; const auto* original = runtime.history.Find(*shapeUi.editor.item);
	if (!original || !original->visible || original->strokeIndex >= canvas->Strokes().size() ||
		canvas->Strokes()[original->strokeIndex].Shape() != shapeUi.editor.object) return false;
	if (!commit && after) shapeUi.editor.legacyPreview = true;
	if (!commit) ShapePreviewMetrics().canvasCopies.fetch_add(1,std::memory_order_relaxed);
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
	if (!commit) ShapePreviewMetrics().compositionDiscards.fetch_add(1,std::memory_order_relaxed);
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
// 焦点层绘制：把全部待落定项画进 L1（不触碰 L2/tile/缓存）；编辑中的项以当前候选代替显示。
std::vector<InkPoint> focusPointScratch; HighlighterGeometry focusHighlighterScratch;
auto drawFocusLayer = [&](RECT& dirty) -> bool
{
	auto& focus = shapeUi.focus; if (!focus.Active()) return true;
	UnionRectInPlace(dirty,focus.l1Bounds); focus.l1Bounds = {}; focus.redraw = false;
	renderer_.ClearOperatorLayer(renderer_.layerL1);
	if (!document_) return false;
	const auto size = window_.Size();
	for (const auto& entry : focus.pending)
	{
		const auto* page = entry.pageIndex == currentPageIndex_ ? document_->PageAt(entry.pageIndex) : nullptr;
		const auto* canvas = page && page->PageGuid() == entry.page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
		if (!canvas || entry.pageIndex >= pageRuntimeStates.size()) return false;
		const auto* state = pageRuntimeStates[entry.pageIndex].history.Find(entry.item);
		if (!state || !state->visible || state->strokeIndex >= canvas->Strokes().size()) return false;
		const draw3::shapes::ShapeObject* transient = shapeUi.editor.item && *shapeUi.editor.item == entry.item &&
			shapeUi.editor.preview ? &*shapeUi.editor.preview : nullptr;
		const StoredStrokeRasterTarget target{&renderer_.layerL1,canvas->Viewport().x,canvas->Viewport().y,size.width,size.height};
		const auto drawn = DrawFocusLayerItem(canvas->Strokes()[state->strokeIndex],transient,renderer_,target,
			focusPointScratch,focusHighlighterScratch);
		if (!drawn.succeeded) return false;
		UnionRectInPlace(focus.l1Bounds,drawn.dirty);
	}
	UnionRectInPlace(dirty,focus.l1Bounds);
	ShapePreviewMetrics().focusDraws.fetch_add(1,std::memory_order_relaxed);
	return true;
};
// 丢弃焦点层：L2 本就不含待落定项（撤销）或即将从文档整体重建（含该项），都不再光栅化。
auto dropFocusLayer = [&](RECT& dirty)
{
	auto& focus = shapeUi.focus; if (!focus.Active()) return;
	UnionRectInPlace(dirty,focus.l1Bounds); focus.l1Bounds = {}; focus.pending.clear(); focus.redraw = false;
	renderer_.ClearOperatorLayer(renderer_.layerL1);
	ShapePreviewMetrics().focusDrops.fetch_add(1,std::memory_order_relaxed);
};
// 可信快照是唯一读取完整 L2 的路径：L2 变化后立即按新内容刷新，平移回退不会缺少刚落定的图形。
auto refreshTrustedSnapshotAfterL2Write = [&]()
{
	renderer_.InvalidateTrustedL2Snapshot(); trustedSnapshotSignatureValid = false;
	if (!viewportVisibleClear || !document_ || currentPageIndex_ >= pageRuntimeStates.size()) return;
	const auto* page = document_->PageAt(currentPageIndex_); const auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
	if (!canvas || !renderer_.RefreshTrustedL2Snapshot(canvas->Viewport().x,canvas->Viewport().y)) return;
	trustedSnapshotSignatureValid = true; trustedSnapshotPageIndex = currentPageIndex_;
	trustedSnapshotRevision = pageRuntimeStates[currentPageIndex_].history.Revision(); trustedSnapshotViewport = canvas->Viewport();
};
// 回退开关：焦点层绘制或落定失败时，按权威文档对该项 tile 重放（缓存与文档一致，无需复制/清缓存）。
auto restoreItemTilesAuthoritative = [&](const PendingL2Item& entry,RECT& dirty) -> bool
{
	if (!document_ || entry.pageIndex != currentPageIndex_ || entry.pageIndex >= pageRuntimeStates.size()) return true;
	const auto* page = document_->PageAt(entry.pageIndex); const auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
	const auto& runtime = pageRuntimeStates[entry.pageIndex]; const auto* state = runtime.history.Find(entry.item);
	if (!canvas || !state || page->PageGuid() != entry.page) return true;
	const auto size = window_.Size(); const auto viewport = canvas->Viewport();
	const CompositionRestoreRequest request{{page->PageGuid(),kDefaultDeviceKey},currentRasterKey(),canvas,&runtime.history,state->compositionTiles,
		runtime.history.Items().size(),viewport.x,viewport.y,size.width,size.height,true};
	const auto restored = historyGpuCache.RestoreComposition(request); UnionRectInPlace(dirty,restored.dirty);
	if (restored.path != CompositionRestorePath::Failed) return true;
	viewportVisibleClear = false; viewportRefreshPending = true; viewportRefreshClearsTransient = false;
	return false;
};
// 落定：把最终对象一次光栅化进 L2（与完成笔画首次提交相同的 L1→L2 算子），随后释放 L1。
auto materializeFocusLayer = [&](RECT& dirty)
{
	auto& focus = shapeUi.focus; if (!focus.Active()) return;
	auto pending = std::move(focus.pending); focus.pending.clear(); focus.redraw = false;
	UnionRectInPlace(dirty,focus.l1Bounds); focus.l1Bounds = {};
	std::sort(pending.begin(),pending.end(),[](const PendingL2Item& a,const PendingL2Item& b) { return a.item.index < b.item.index; });
	const auto size = window_.Size();
	for (const auto& entry : pending)
	{
		// L2 已不属于该页时由换页重建从文档恢复；已撤销的项 L2 本就不含。
		if (!document_ || entry.pageIndex != currentPageIndex_ || entry.pageIndex >= pageRuntimeStates.size()) continue;
		auto* page = document_->PageAt(entry.pageIndex); auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
		if (!canvas || page->PageGuid() != entry.page) continue;
		auto& runtime = pageRuntimeStates[entry.pageIndex]; const auto* state = runtime.history.Find(entry.item);
		if (!state || !state->visible || state->strokeIndex >= canvas->Strokes().size()) continue;
		if (!CanvasFocusSession::IsVisibleSuffix(runtime.history,std::span<const PendingL2Item>(&entry,1)))
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error,"[Draw3.ShapeEdit] action=materialize warning=not_z_suffix\n");
		const auto viewport = canvas->Viewport();
		renderer_.ClearOperatorLayer(renderer_.layerL1);
		if (!shapeUi.construction) renderer_.ClearOperatorLayer(renderer_.layerL0);
		// 未经编辑时补齐抬笔时推迟的热前像，撤销仍可走 hot path；编辑过的项由 journal 路径撤销。
		HotPreimageCaptureResult capture;
		const bool unedited = runtime.rasterState == entry.beforeState;
		if (unedited)
			capture = historyGpuCache.CapturePreimage({{page->PageGuid(),kDefaultDeviceKey},entry.item,currentRasterKey(),
				entry.beforeState,entry.afterState,state->undoTiles,viewport.x,viewport.y,size.width,size.height});
		const StoredStrokeRasterTarget target{&renderer_.layerL1,viewport.x,viewport.y,size.width,size.height};
		const auto drawn = DrawFocusLayerItem(canvas->Strokes()[state->strokeIndex],nullptr,renderer_,target,
			focusPointScratch,focusHighlighterScratch);
		const bool submitted = drawn.succeeded && ResolveFocusLayerToL2(renderer_,renderer_.layerL1,drawn.dirty);
		if (capture.status == HotPreimageCaptureStatus::Captured && (!submitted || !historyGpuCache.CommitPreimage(capture.ticket)))
			historyGpuCache.CancelPreimage(capture.ticket);
		renderer_.ClearOperatorLayer(renderer_.layerL1);
		if (submitted)
		{
			if (unedited) runtime.rasterState = entry.afterState;
			UnionRectInPlace(dirty,drawn.dirty);
		}
		else
		{
			ShapePreviewMetrics().focusFallbacks.fetch_add(1,std::memory_order_relaxed);
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error,"[Draw3.ShapeEdit] action=materialize result=fallback path=authoritative_replay\n");
			if (restoreItemTilesAuthoritative(entry,dirty) && unedited) runtime.rasterState = entry.afterState;
		}
	}
	viewportVisibleClear = CanvasVisibleClarityAfterAuthoritativeWrite(viewportVisibleClear,true);
	refreshTrustedSnapshotAfterL2Write();
	ShapePreviewMetrics().materializations.fetch_add(1,std::memory_order_relaxed);
	if (metricsState_) { metricsState_->InvalidateLiveRasters(); metricsState_->renderAttempt = true; metricsState_->InvalidateL2(); }
};
// 焦点层不可用（L1 绘制失败）：按权威路径把项写回 L2，本编辑剩余部分改走旧路径。
auto abandonFocusLayer = [&](RECT& dirty)
{
	auto& focus = shapeUi.focus; if (!focus.Active()) return;
	ShapePreviewMetrics().focusFallbacks.fetch_add(1,std::memory_order_relaxed);
	DiagnosticOutput::Text(DiagnosticOutput::Stream::Error,"[Draw3.ShapeEdit] action=focus_layer result=fallback path=authoritative_replay\n");
	auto pending = std::move(focus.pending); focus.pending.clear(); focus.redraw = false;
	UnionRectInPlace(dirty,focus.l1Bounds); focus.l1Bounds = {};
	renderer_.ClearOperatorLayer(renderer_.layerL1);
	shapeUi.editor.preview.reset(); shapeUi.editor.legacyPreview = true;
	for (const auto& entry : pending)
		if (restoreItemTilesAuthoritative(entry,dirty) && entry.pageIndex < pageRuntimeStates.size() &&
			pageRuntimeStates[entry.pageIndex].rasterState == entry.beforeState) pageRuntimeStates[entry.pageIndex].rasterState = entry.afterState;
	refreshTrustedSnapshotAfterL2Write();
};
auto editorInFocusLayer = [&]() noexcept
{
	return shapeUi.editor.item && !shapeUi.editor.legacyPreview && shapeUi.focus.Find(*shapeUi.editor.item) != nullptr;
};
// 编辑中的撤销若正好撤销焦点层项本身，只丢 L1，L2 不动。
auto focusIsUndoTarget = [&]() -> bool
{
	if (!shapeUi.focus.Active() || !document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
	auto& runtime = pageRuntimeStates[currentPageIndex_]; EnsureOperationJournal(runtime);
	if (runtime.operationCursor <= runtime.operationFloor) return false;
	const auto& operation = runtime.operations[runtime.operationCursor - 1];
	const auto last = runtime.history.LastVisibleItem();
	const auto* entry = shapeUi.focus.Find(operation.item);
	return operation.kind == CanvasOperationKind::Append && last && *last == operation.item && entry &&
		entry->pageIndex == currentPageIndex_ && last->index >= runtime.undoFloor;
};
// 编辑框失焦的内部动作；focusAction 为空表示保留焦点层交给后续操作（编辑中撤销该项）。
auto closeShapeEditor = [&](RECT& dirty,std::optional<CanvasFocusEndAction> focusAction,bool closeConstruction)
{
	if (shapeUi.editor.gestureActive) logShapeGestureSummary("closed");
	// 不在焦点层的旧路径编辑已把预览写入 L2；落定类失焦需按权威文档恢复，重建类失焦由重建覆盖。
	if (focusAction == CanvasFocusEndAction::Materialize && shapeUi.editor.preview && !editorInFocusLayer())
		(void)replayShapeEdit(std::nullopt,dirty,false);
	shapeUi.editor = {};
	if (closeConstruction)
	{
		shapeUi.pendingObject.reset(); shapeUi.pendingItem.reset(); shapeUi.pendingCompletedQpc = 0;
		if (shapeUi.construction)
		{
			UnionRectInPlace(dirty,shapeBoundsRect(shapeUi.construction->renderData.bounds));
			CancelShapeConstruction(shapeUi.construction,shapeUi);
		}
		UnionRectInPlace(dirty,betweenShapeBounds); betweenShapeBounds = {};
		renderer_.ClearOperatorLayer(renderer_.layerL0);
	}
	if (focusAction == CanvasFocusEndAction::Materialize) materializeFocusLayer(dirty);
	else if (focusAction == CanvasFocusEndAction::Drop)
	{
		// 重建类失焦：L2 将从文档重建并包含该项，状态令牌同步到 after，避免与 before 令牌混用热前像。
		for (const auto& entry : shapeUi.focus.pending)
		{
			const auto* page = document_ && entry.pageIndex < pageRuntimeStates.size() ? document_->PageAt(entry.pageIndex) : nullptr;
			if (page && page->PageGuid() == entry.page && pageRuntimeStates[entry.pageIndex].rasterState == entry.beforeState)
				pageRuntimeStates[entry.pageIndex].rasterState = entry.afterState;
		}
		dropFocusLayer(dirty);
	}
};
auto finishShapeEditing = [&]()
{
	if (!shapeUi.editor.object || shapeUi.editor.toolSpec.preset == draw3::shapes::PresetKind::Line ||
		shapeUi.editor.toolSpec.preset == draw3::shapes::PresetKind::Arrow) return false;
	shapeUi.finishedEditingSession = shapeUi.editor.identity.session;
	shapeUi.finishedEditingEntryEpoch = shapeUi.editor.toolEntryEpoch;
	shapeUi.finishedEditingWorkspace = shapeUi.editor.identity.workspace;
	return true;
};
// 唯一的失焦入口：任何其他操作都先经此关闭编辑框并落定（或丢弃）L1，再执行该操作。
endCanvasFocus = [&](CanvasFocusEndReason reason,RECT& dirty)
{
	if (shapeUi.focus.Active())
	{
		const auto index = static_cast<std::size_t>(reason);
		if (index < shapeUi.focusEndCounts.size()) ++shapeUi.focusEndCounts[index];
	}
	// 新接触只结束编辑框；进行中的多笔构造由接触本身继续。
	closeShapeEditor(dirty,ResolveCanvasFocusEnd(reason),reason != CanvasFocusEndReason::NewContact);
	if (reason != CanvasFocusEndReason::NewContact) shapeUi.completedConstruction = {};
};
closeShapeSessions = [&](RECT& dirty,bool restore)
{
	endCanvasFocus(restore ? CanvasFocusEndReason::SceneChange : CanvasFocusEndReason::PageSwitch,dirty);
};
// 焦点层编辑提交：CPU 文档/journal 与旧路径相同，L1 改为显示最终对象，L2 不动。
auto commitFocusShapeEdit = [&](const draw3::shapes::ShapeObject& after,RECT& dirty) -> bool
{
	if (!shapeUi.editor.item || !shapeUi.editor.object || !document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
	auto* page = document_->PageAt(currentPageIndex_); auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
	if (!canvas) return false;
	auto& runtime = pageRuntimeStates[currentPageIndex_];
	const auto edit = CommitRuntimeShapeEditCpu(*canvas,runtime,*shapeUi.editor.item,*shapeUi.editor.object,after,allocateRasterStateToken);
	shapeUi.editor.preview.reset();
	if (!edit) { if (!drawFocusLayer(dirty)) abandonFocusLayer(dirty); return false; }
	shapeUi.editor.object = after; markPresentationMutation();
	// 编辑即使不改变hasContent，也要发出真实内容修订，冷页/保存不能误判为未变。
	if (++currentContentRevision_ == 0) ++currentContentRevision_;
	contentRevisionNeedsPresent = true;
	if (observer_.currentPageContentChanged) observer_.currentPageContentChanged(observer_.context,currentPageHasContent(),currentContentRevision_);
	publishCurrentPageContent();
	if (metricsState_) { metricsState_->PruneStored(*canvas,runtime); metricsState_->InvalidateLiveRasters(); metricsState_->renderAttempt = true; }
	if (!drawFocusLayer(dirty)) abandonFocusLayer(dirty);
	return true;
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
			shapeUi.editor.toolEntryEpoch = ShapeToolEntryEpoch().load(std::memory_order_acquire);
			shapeUi.editor.identity = snapshot.identity; shapeUi.editor.identity.session = shapeUi.nextSession++;
			shapeUi.editor.tool = window_.ActiveTool(); shapeUi.editor.toolSpec = window_.ProductVisualStyleSnapshot().shapeTool;
		}
		shapeUi.pendingObject.reset(); shapeUi.pendingItem.reset();
	}
	if (shapeUi.construction && shapeUi.construction->session.Status().active)
	{
		shapeUi.completedConstruction = {};
		snapshot.identity.session = shapeUi.construction->identity.session;
		snapshot.identity.objectId = shapeUi.construction->identity.objectId;
		snapshot.construction = shapeUi.construction->session.Status();
		snapshot.constructionContactActive = shapeUi.construction->contactActive || shapeUi.construction->pointGesture.has_value();
		if (draw3::shapes::GetToolCapabilities(shapeUi.construction->tool.preset).points)
		{
			snapshot.object = shapeUi.construction->preview;
			snapshot.bounds = shapeUi.construction->renderData.bounds;
			snapshot.handles = ConstructionPreviewHandles(*shapeUi.construction);
		}
		if (snapshot.constructionContactActive) { snapshot.construction.canBack = false; snapshot.construction.canForward = false; snapshot.construction.canComplete = false; }
	}
	else
	{
		if (shapeUi.completedConstruction.finished)
		{
			snapshot.construction = shapeUi.completedConstruction;
			snapshot.completedConstructionSession = shapeUi.completedConstructionSession;
		}
		if (active.empty() && shapeUi.editor.object)
		{
			snapshot.object = shapeUi.editor.preview ? shapeUi.editor.preview : shapeUi.editor.object;
			snapshot.identity.session = shapeUi.editor.identity.session;
			snapshot.identity.objectId = snapshot.object->id; snapshot.identity.objectRevision = snapshot.object->revision;
			snapshot.editGestureActive = shapeUi.editor.gestureActive;
			if (shapeUi.published && !shapeUi.published->drawingPreview && shapeUi.published->object == snapshot.object && shapeUi.published->displayScale == snapshot.displayScale)
			{ snapshot.bounds = shapeUi.published->bounds; snapshot.frameRect = shapeUi.published->frameRect; snapshot.handles = shapeUi.published->handles; }
			else
			{
				draw3::shapes::GeometryResult geometry;
				if (draw3::shapes::BuildGeometry(*snapshot.object,geometry,{snapshot.displayScale,.25f})) snapshot.bounds = geometry.bounds;
				snapshot.handles = draw3::shapes::BuildEditHandles(*snapshot.object,{snapshot.displayScale,.25f},&snapshot.frameRect);
			}
		}
	}
	if (!snapshot.object) for (const auto* runtime : active)
	{
		if (!runtime || runtime->ended || !runtime->shape.object ||
			runtime->shape.tool.preset != draw3::shapes::PresetKind::Triangle ||
			runtime->shape.tool.constraint != draw3::shapes::PlanarConstraint::Isosceles) continue;
		// 首拖只发布标记的几何真值；不能创建构造步骤、控制点输入或保存额外斜杠。
		snapshot.drawingPreview = true; snapshot.object = runtime->shape.object;
		snapshot.identity.objectId = snapshot.object->id; snapshot.identity.objectRevision = snapshot.object->revision;
		snapshot.bounds = runtime->shape.renderData.bounds;
		for (const auto& handle : draw3::shapes::BuildEditHandles(*snapshot.object,{snapshot.displayScale,.25f}))
			if (handle.kind == draw3::shapes::EditHandleKind::Vertex) snapshot.handles.push_back(handle);
		break;
	}
	// 构造加点/移点只改会话历史，不伪造文档revision；白板副本同步后才向UI发布当前帧。
	if (publishHistoryAvailability()) publishWhiteboard();
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
	if (ShapeFocusLayerEnabled().load(std::memory_order_relaxed) && !shapeUi.focus.Active())
	{
		// 焦点层：CPU 文档/journal 照常追加，L2 暂不含该对象，覆盖率只画进 L1。
		const auto created = AppendRuntimeConstructedShapeCpu(*canvas,runtime,object,allocateRasterStateToken);
		if (!created) return false;
		runtime.rasterState = created->beforeState;
		renderer_.ClearOperatorLayer(renderer_.layerL0);
		shapeUi.focus.pending = {{created->renderItem,object.id,page->PageGuid(),currentPageIndex_,created->beforeState,created->afterState}};
		if (!drawFocusLayer(dirty)) abandonFocusLayer(dirty);
		shapeUi.pendingObject = std::move(object); shapeUi.pendingItem = created->renderItem; shapeUi.pendingPage = page->PageGuid(); shapeUi.pendingPageIndex = currentPageIndex_;
		markPresentationMutation(); publishCurrentPageContent();
		if (observer_.strokeCompleted) observer_.strokeCompleted(observer_.context,Bridge::CompletedStrokeKind::Shape);
		if (metricsState_) { metricsState_->PruneStored(*canvas,runtime); metricsState_->InvalidateLiveRasters(); metricsState_->InvalidateL2(); }
		return true;
	}
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
shapePalmCategory = [&](DrawingTool tool) { return ShapePalmToolCategory(tool,shapeUi); };
abandonShapeContact = [&](RuntimeStroke& runtime)
{
	if (shapeUi.construction == runtime.shape.construction)
	{
		RECT ignored{}; endCanvasFocus(CanvasFocusEndReason::Escape,ignored);
		publishShapeUi(); window_.RequestFullPresent();
	}
	else CancelShapeConstruction(runtime.shape.construction,shapeUi);
};
beginShapeContact = [&](RuntimeStroke& runtime)
{
	// 已成形的非直线/箭头：框外首个接触只结束编辑并吞到终态；直线/箭头继续起笔。
	// 新落笔会占用 L1/L0：先让编辑框失焦并把待落定图形落定到 L2。
	if (shapeUi.editor.object || shapeUi.focus.Active())
	{
		const bool finished = finishShapeEditing();
		RECT ignored{}; endCanvasFocus(CanvasFocusEndReason::NewContact,ignored); window_.RequestFullPresent();
		if (finished) { publishShapeUi(); return false; }
	}
	if (active.empty()) { shapeUi.pendingObject.reset(); shapeUi.pendingItem.reset(); shapeUi.pendingCompletedQpc = 0; }
	if (runtime.shape.generic) runtime.shape.nextRevision = nextShapeRevision;
	const bool multi = runtime.shape.generic && draw3::shapes::GetToolCapabilities(runtime.shape.tool.preset).multiStage;
	if (!multi)
	{
		if (shapeUi.construction) { RECT ignored{}; endCanvasFocus(CanvasFocusEndReason::ToolChange,ignored); renderer_.ClearOperatorLayer(renderer_.layerL0); window_.RequestFullPresent(); }
		return true;
	}
	const auto scene = shapeScene();
	if (shapeUi.construction && (!SameShapeUiScene(scene.identity,shapeUi.construction->identity) ||
		shapeUi.construction->tool != runtime.shape.tool))
	{
		shapeUi.construction->session.Cancel(); shapeUi.construction->cancelled = true; shapeUi.construction.reset();
		shapeUi.completedConstruction = {};
	}
	// 快速连点时上一根接触的 Up 与本次 Down 可能落在同一入口批次：Up 已出队但帧末确认尚未执行，
	// contactActive 仍为真会把这次加点整根拒收。按真实先后顺序先确认上一步，再接受本次接触。
	if (finishShapeContact)
		if (RuntimeStroke* pending = FindPendingConstructionContact(active,runtime,shapeUi.construction)) finishShapeContact(*pending);
	if (!shapeUi.construction)
	{
		auto state = std::make_shared<ShapeConstructionRuntime>(); state->identity = scene.identity;
		state->identity.session = shapeUi.nextSession++; state->identity.objectId = runtime.shape.id;
		state->tool = runtime.shape.tool; state->style = runtime.shape.style; state->workspaceGuid = runtime.ownerWorkspaceGuid;
		state->viewport = runtime.viewport;
		if (!state->session.Begin(state->tool,state->style,runtime.shape.id)) return false;
		shapeUi.completedConstruction = {};
		shapeUi.construction = std::move(state);
	}
	auto& state = *shapeUi.construction;
	if (state.contactActive || state.pointGesture || state.cancelled) return false; // 一个构造会话只接受一根正在绘制的物理接触。
	state.contactActive = true; runtime.shape.construction = shapeUi.construction;
	PrepareRuntimeConstructionContact(runtime,scene.displayScale);
	runtime.shape.id = state.identity.objectId; runtime.shape.tool = state.tool; runtime.shape.style = state.style;
	runtime.shape.revision = state.revision; return true;
};
finishShapeContact = [&](RuntimeStroke& runtime)
{
	if (!runtime.shape.construction || runtime.shape.constructionFinished) return;
	ConfirmRuntimeConstructionContact(runtime); // 自身幂等并置位 constructionFinished。
	if (runtime.shape.construction->cancelled || runtime.shape.construction->completed)
		if (shapeUi.construction == runtime.shape.construction)
		{
			shapeUi.construction.reset();
		}
	shapeUi.constructionVisualDirty = true;
};
auto backShapeConstruction = [&](RECT& dirty)
{
	if (!shapeUi.construction) return;
	shapeUi.construction->pointGesture.reset();
	UnionRectInPlace(dirty,shapeBoundsRect(shapeUi.construction->renderData.bounds));
	if (shapeUi.construction->session.Back()) (void)updateConstructionPreview(*shapeUi.construction);
	shapeUi.constructionVisualDirty = true; publishShapeUi();
};
auto forwardShapeConstruction = [&](RECT& dirty)
{
	if (!shapeUi.construction) return;
	shapeUi.construction->pointGesture.reset();
	UnionRectInPlace(dirty,shapeBoundsRect(shapeUi.construction->renderData.bounds));
	if (shapeUi.construction->session.Forward()) (void)updateConstructionPreview(*shapeUi.construction);
	shapeUi.constructionVisualDirty = true; publishShapeUi();
};
auto processShapeUiAction = [&](const ShapeUiAction& action,RECT& dirty)
{
	const auto scene = shapeScene();
	if (!shapeUi.published || !SameShapeUiScene(action.identity,scene.identity)) return;
	if (action.kind == ShapeUiActionKind::CloseEditor && shapeUi.construction &&
		action.identity.session == shapeUi.construction->identity.session && action.identity.objectId == shapeUi.construction->identity.objectId)
	{ endCanvasFocus(CanvasFocusEndReason::Escape,dirty); publishShapeUi(); return; }
	if (IsConstructionPointAction(action.kind))
	{
		if (!shapeUi.construction) return;
		UnionRectInPlace(dirty,shapeBoundsRect(shapeUi.construction->renderData.bounds));
		if (ApplyConstructionPointAction(*shapeUi.construction,shapeUi.published.get(),action,scene.identity,nextShapeRevision))
		{
			shapeUi.constructionVisualDirty = true;
			if (IsShapePreviewAction(action.kind)) ++shapePreviewSlot.executedThisFrame;
			publishShapeUi();
		}
		return;
	}
	const bool constructionAction = action.kind == ShapeUiActionKind::ConstructionComplete ||
		action.kind == ShapeUiActionKind::ConstructionBack || action.kind == ShapeUiActionKind::ConstructionForward ||
		action.kind == ShapeUiActionKind::ConstructionCancel;
	if (constructionAction)
	{
		if (!shapeUi.construction || !ShapeConstructionActionIdentityMatches(*shapeUi.construction,shapeUi.published.get(),action,scene.identity)) return;
		if (action.kind == ShapeUiActionKind::ConstructionBack) backShapeConstruction(dirty);
		else if (action.kind == ShapeUiActionKind::ConstructionForward) forwardShapeConstruction(dirty);
		else if (action.kind == ShapeUiActionKind::ConstructionCancel) endCanvasFocus(CanvasFocusEndReason::Escape,dirty);
		else
		{
			const auto saved = shapeUi.construction->session;
			if (auto object = shapeUi.construction->session.Complete())
			{
				if (commitShapeConstruction(std::move(*object),dirty))
				{
					shapeUi.completedConstructionSession = shapeUi.construction->identity.session;
					shapeUi.completedConstruction = shapeUi.construction->session.Status();
					shapeUi.completedConstruction.active = false; shapeUi.completedConstruction.finished = true;
					shapeUi.construction.reset(); shapeUi.constructionVisualDirty = true;
				}
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
	{ if (action.finishEditing) finishShapeEditing(); endCanvasFocus(CanvasFocusEndReason::Escape,dirty); publishShapeUi(); return; }
	if (action.kind == ShapeUiActionKind::BeginEdit)
	{
		if (shapeUi.editor.gestureActive) return;
		if (!shapeUi.editor.gesture.Begin(*shapeUi.editor.object,action.handleId,action.down,{scene.displayScale,.25f})) return;
		shapeUi.editor.gestureIdentity = action.identity; shapeUi.editor.down = action.down;
		shapeUi.editor.handleId = action.handleId; shapeUi.editor.gestureActive = true;
		shapeGestureStats = {}; shapeGestureStats.base = SnapshotShapePreviewMetrics();
		shapeGestureStats.restoreBase = historyGpuCache.CompositionRestoreCount();
		publishShapeUi(); return;
	}
	if (action.kind == ShapeUiActionKind::CancelEdit)
	{
		// 焦点层：取消只把 L1 重画回原对象；旧路径按权威文档恢复 L2。
		if (editorInFocusLayer()) { shapeUi.editor.preview.reset(); if (!drawFocusLayer(dirty)) abandonFocusLayer(dirty); }
		else if (shapeUi.editor.preview) (void)replayShapeEdit(std::nullopt,dirty,false);
		if (shapeUi.editor.gestureActive) logShapeGestureSummary("cancelled");
		shapeUi.editor.gesture.Cancel(); shapeUi.editor.gestureActive = false; publishShapeUi(); return;
	}
	std::optional<draw3::shapes::ShapeObject> candidate;
	const auto revision = nextShapeRevision(shapeUi.editor.object->id,shapeUi.editor.object->revision);
	if (!revision) return;
	if ((action.kind == ShapeUiActionKind::SetStyle || action.kind == ShapeUiActionKind::SetSides ||
		action.kind == ShapeUiActionKind::SetConstraint || action.kind == ShapeUiActionKind::SetRightSolid ||
		action.kind == ShapeUiActionKind::FlipHorizontal || action.kind == ShapeUiActionKind::FlipVertical ||
		action.kind == ShapeUiActionKind::Rotate45Clockwise || action.kind == ShapeUiActionKind::Rotate90Clockwise) && !shapeUi.editor.gestureActive)
		candidate = BuildShapePropertyCandidate(*shapeUi.editor.object,action,revision,{scene.displayScale,.25f});
	else if (inGesture && (action.kind == ShapeUiActionKind::PreviewEdit || action.kind == ShapeUiActionKind::CommitEdit))
	{
		auto preview = [&](draw3::shapes::EditSession& session, bool predicted)
		{
			const auto& transform = predicted && action.predictedGestureTransform ? action.predictedGestureTransform : action.gestureTransform;
			if (transform) return session.PreviewTransform(transform->pivot,transform->translation,transform->scale,transform->rotation,revision);
			return session.Preview(predicted && action.predictedPoint ? *action.predictedPoint : action.point,revision,nullptr,predicted ? std::nullopt : action.angleDelta);
		};
		if (preview(shapeUi.editor.gesture,false))
		{
			candidate = action.kind == ShapeUiActionKind::CommitEdit ? shapeUi.editor.gesture.Commit() : shapeUi.editor.gesture.Candidate();
			// 预测只作用于副本，不能污染真实累计角或最终 Up 的几何。
			if (action.kind == ShapeUiActionKind::PreviewEdit && (action.predictedPoint || action.predictedGestureTransform))
			{
				auto predicted = shapeUi.editor.gesture;
				if (preview(predicted,true)) candidate = predicted.Candidate();
			}
		}
	}
	const bool commit = action.kind != ShapeUiActionKind::PreviewEdit;
	const bool endsGesture = commit && inGesture;
	const bool focused = editorInFocusLayer();
	if (candidate)
	{
		bool applied = false;
		if (!commit)
		{
			// 焦点层：只在 L1 重画这一个候选；L1 不可用时才退回旧的整页候选重放。
			auto& metrics = ShapePreviewMetrics();
			if (focused)
			{
				shapeUi.editor.preview = candidate; applied = drawFocusLayer(dirty);
				if (!applied) abandonFocusLayer(dirty);
			}
			if (applied) { metrics.focusExecuted.fetch_add(1,std::memory_order_relaxed); ++shapeGestureStats.focus; }
			else
			{
				applied = replayShapeEdit(candidate,dirty,false);
				metrics.legacyExecuted.fetch_add(1,std::memory_order_relaxed); ++shapeGestureStats.legacy;
			}
			metrics.executed.fetch_add(1,std::memory_order_relaxed); ++shapeGestureStats.previews;
			RaiseShapePreviewMaximum(metrics.maxPerFrame,++shapePreviewSlot.executedThisFrame);
			if (applied && action.enqueueMicroseconds &&
				(!shapePreviewLatencyStart || action.enqueueMicroseconds < shapePreviewLatencyStart))
				shapePreviewLatencyStart = action.enqueueMicroseconds;
		}
		else applied = focused ? commitFocusShapeEdit(*candidate,dirty) : replayShapeEdit(candidate,dirty,commit);
		if (!applied) shapeUi.editor.gestureActive = false;
	}
	else if (commit && shapeUi.editor.preview)
	{
		if (focused) { shapeUi.editor.preview.reset(); if (!drawFocusLayer(dirty)) abandonFocusLayer(dirty); }
		else (void)replayShapeEdit(std::nullopt,dirty,false);
	}
	if (commit)
	{
		if (endsGesture) logShapeGestureSummary(candidate ? "committed" : "empty");
		shapeUi.editor.gesture.Cancel(); shapeUi.editor.gestureActive = false;
	}
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
	{
		const bool sceneChanged = shapeUi.editor.object ? !SameShapeUiScene(shapeUi.editor.identity,scene.identity) :
			!SameShapeUiScene(shapeUi.construction->identity,scene.identity);
		endCanvasFocus(window_.SelectionMode() ? CanvasFocusEndReason::SelectionMode :
			sceneChanged ? CanvasFocusEndReason::SceneChange : CanvasFocusEndReason::ToolChange,dirty);
		renderer_.ClearOperatorLayer(renderer_.layerL0); window_.RequestFullPresent();
	}
	// 不变式：待落定项只在其编辑框打开期间留在 L1；编辑框未接管或已关闭则立即落定。
	if (shapeUi.focus.Active() && !editorInFocusLayer()) { endCanvasFocus(CanvasFocusEndReason::OverlayClosed,dirty); window_.RequestFullPresent(); }
	// 共享层重建清空 L1 后补画焦点层。
	if (shapeUi.focus.Active() && shapeUi.focus.redraw && !drawFocusLayer(dirty)) abandonFocusLayer(dirty);
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
		const RECT previewBounds = shapeBoundsRect(shapeUi.construction->renderData.bounds);
		renderer_.SetOperatorTarget(renderer_.layerL0, previewBounds); // 声明写入范围供 L0 局部清除
		if (!renderer_.DrawShapeRenderData(shapeUi.construction->renderData)) failed = true;
		betweenShapeBounds = previewBounds; UnionRectInPlace(dirty,betweenShapeBounds);
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
		else if (pending.type == CanvasCommandType::Redo && shapeUi.construction)
		{
			if (shapeUi.construction->session.Status().canForward)
			{
				// 与Undo对称：恢复确认步骤时丢弃本次未结束的物理接触，迟到Up不得重复确认。
				for (auto* runtime : active) if (runtime && runtime->shape.construction == shapeUi.construction)
					runtime->shape.constructionStepDiscarded = true;
				forwardShapeConstruction(dirty);
			}
		}
		else if (cancellingAction) processShapeUiAction(*pending.shapeAction,dirty);
		else
		{
			if (sceneBarrier && shapeUi.construction) endCanvasFocus(CanvasFocusEndReason::SceneChange,dirty);
			shapeDeferredCommands.push_back(std::move(pending));
		}
	}
};
