// 复用 RunShapeRendererTests 的真实无窗口 WARP fixture；此块验证文档同层替换与恢复事务。
{
	std::array<uint8_t,16> pageBytes{}; pageBytes[0] = 0xA9;
	InkPage page{InkGuid(pageBytes)};
	auto* document = page.GetOrCreateCanvas(kDefaultDeviceKey,{200,0,1});
	CanvasRuntimeHistory history;
	RevisionAllocator revisions;
	InkRenderer& gpu = renderer;
	InkHistoryGpuCache cache;
	bool ready = document && cache.Initialize(gpu,{kUndoTileBytes,1},{kCompositionTileBytes * 2});
	const HistoryCanvasIdentity identity{page.PageGuid(),kDefaultDeviceKey};
	const InkHistoryRasterKey key{kDefaultDeviceKey,1,DXGI_FORMAT_B8G8R8A8_UNORM,1};
	const auto append = [&](InkStroke stroke)
	{
		auto footprint = BuildStrokeTileFootprint(stroke);
		const auto index = footprint ? document->AppendStroke(std::move(stroke)) : std::nullopt;
		return index && history.AppendStroke(*index,std::move(*footprint)).has_value();
	};
	const auto filled = [&](Vec2 first,Vec2 last,uint32_t rgb)
	{
		auto shape = object(); shape.kind = Kind::Rectangle; shape.geometry.start = first; shape.geometry.end = last;
		shape.style.stroke = false; shape.style.fill = true; shape.style.fillRgb = rgb; shape.style.fillOpacity = .5f;
		return shape;
	};
	const auto stroke = [](const ShapeObject& shape)
	{
		return InkStroke({StoredInkType::Shape,shape.style.strokeRgb,shape.style.strokeOpacity,0},{},std::nullopt,shape);
	};
	auto bottom = filled({214,20},{318,110},0x0000FF);
	auto middle = filled({224,40},{246,88},0xFF0000);
	auto above = filled({218,54},{310,72},0x00FF00);
	if (ready) ready = append(stroke(bottom)) &&
		append(InkStroke({StoredInkType::Pen,0x202020,.65f,0},{{218,36,6},{310,36,6}})) &&
		append(stroke(middle)) && append(stroke(above));
	if (!check(ready,"edit transaction fixture has ordered shape/ink/edited-shape/top material")) return 1;
	const auto item = history.Items()[2].id;
	// 独立全序 DrawStoredStroke 是像素参照；不是另一次相同的 composition-cache 调用。
	const auto ordered = [&](const InkCanvas& canvas,const CanvasRuntimeHistory& state,std::vector<unsigned char>& result)
	{
		gpu.SetScreenSize(128,128); gpu.ClearRTV(gpu.layerL2RTV.Get(),kTransparentLayerClearColor);
		gpu.ClearOperatorLayer(gpu.layerL0);
		std::vector<InkPoint> scratch; HighlighterGeometry highlighter;
		for (const auto& entry : state.Items()) if (entry.visible)
		{
			gpu.ClearOperatorLayer(gpu.layerL1);
			const StoredStrokeRasterTarget target{&gpu.layerL1,200,0,128,128};
			if (!DrawStoredStroke(canvas.Strokes()[entry.strokeIndex],gpu,target,scratch,highlighter).succeeded ||
				!gpu.ApplyOperatorLayers(gpu.layerL2RTV.Get(),gpu.layerL1,gpu.layerL0,{0,0,128,128})) return false;
		}
		return read(result,gpu.layerL2Texture.Get());
	};
	const auto closePixels = [&](const std::vector<unsigned char>& actual,const std::vector<unsigned char>& expected)
	{
		if (actual.size() != expected.size()) return false;
		int maximum = 0;
		for (size_t i = 0; i < actual.size(); ++i) maximum = std::max(maximum,std::abs(int(actual[i])-int(expected[i])));
		// BGRA8 材质缓存和逐层解析的量化可差两个单位；顺序错误的色差远大于此值。
		if (maximum > 2) std::fprintf(stderr,"[ShapeRenderer.Edit] pixel maxDelta=%d\n",maximum);
		return maximum <= 2;
	};
	const auto tilesFor = [&](const CanvasRuntimeHistory& first,const CanvasRuntimeHistory& second)
	{
		auto tiles = first.Find(item)->compositionTiles;
		const auto& next = second.Find(item)->compositionTiles;
		tiles.insert(tiles.end(),next.begin(),next.end()); std::sort(tiles.begin(),tiles.end());
		tiles.erase(std::unique(tiles.begin(),tiles.end()),tiles.end()); return tiles;
	};
	const auto replay = [&](const InkCanvas& canvas,const CanvasRuntimeHistory& state,
		const std::vector<SignedTileCoordinate>& tiles)
	{
		cache.DiscardHotPreimages(); cache.DiscardCompositionCache(); gpu.ClearShapeRenderCache();
		const CompositionRestoreRequest request{identity,key,&canvas,&state,tiles,state.Items().size(),200,0,128,128,true};
		return cache.RestoreComposition(request);
	};
	const auto replace = [&](InkCanvas& candidate,CanvasRuntimeHistory& state,const ShapeObject& target)
	{
		const auto before = candidate.Strokes()[2].Shape();
		ShapeCommand command{CommandKind::Transform,target.id,before->revision,before,target};
		if (!candidate.ReplaceShape(2,command)) return false;
		auto footprint = BuildStrokeTileFootprint(candidate.Strokes()[2]);
		return footprint && state.UpdateItemGeometry(item,std::move(*footprint));
	};
	std::vector<unsigned char> baseline,movedPixels,actual;
	if (!check(ordered(*document,history,baseline),"full ordered production raster baseline")) return 1;
	// 在原位置叠加上方绿材质：应为绿128、红64、蓝32，颜色MAX无法满足。
	const auto crossed = (60 * 128 + 34) * 4;
	check(std::abs(baseline[crossed]-32)<=2 && std::abs(baseline[crossed+1]-128)<=2 &&
		std::abs(baseline[crossed+2]-64)<=2 && std::abs(baseline[crossed+3]-224)<=2,
		"baseline contains the expected bottom/middle/top source-over order");
	auto moved = middle; moved.transform.dx = 48; moved.revision = revisions.Next(moved.id,middle.revision);
	InkCanvas preview = *document; CanvasRuntimeHistory previewHistory = history;
	ready = replace(preview,previewHistory,moved);
	const auto affected = tilesFor(history,previewHistory);
	check(ready && affected.size()==2 && preview.Strokes().size()==4 && previewHistory.Items().size()==4 &&
		preview.Strokes()[2].Shape()->id==middle.id,"same-index replacement crosses two canvas tiles without changing identity/order");
	if (!check(ready && ordered(preview,previewHistory,movedPixels),"moved candidate full-raster reference")) return 1;
	// 恢复真实权威底图后，只请求旧新 footprint，避免参照绘制掩盖旧区域残留。
	check(ordered(*document,history,actual) && actual==baseline,"preview starts from unchanged authoritative baseline");
	const auto shown = replay(preview,previewHistory,affected);
	check(shown.path!=CompositionRestorePath::Failed && read(actual,gpu.layerL2Texture.Get()) &&
		closePixels(actual,movedPixels) && document->Strokes()[2].Shape()==std::optional<ShapeObject>(middle),
		"GPU preview replays original item order without publishing document");
	const auto oldOnly = (45 * 128 + 34) * 4;
	const auto newOnly = (45 * 128 + 82) * 4;
	check(actual[oldOnly+2]==0 && std::abs(actual[oldOnly+3]-128)<=2 &&
		std::abs(actual[newOnly+2]-128)<=2 && std::abs(actual[newOnly+3]-192)<=2,
		"moving removes old red footprint and draws new footprint over original blue");
	const auto cancelled = replay(*document,history,affected);
	check(cancelled.path!=CompositionRestorePath::Failed && read(actual,gpu.layerL2Texture.Get()) &&
		closePixels(actual,baseline),"preview cancellation restores exact original ordered scene");
	// 与生产相同：候选GPU恢复成功之后，才交换CPU文档与sidecar。
	const auto committed = replay(preview,previewHistory,affected);
	if (committed.path!=CompositionRestorePath::Failed) { *document=std::move(preview); history=std::move(previewHistory); }
	check(committed.path!=CompositionRestorePath::Failed && document->Strokes()[2].Shape()==std::optional<ShapeObject>(moved) &&
		read(actual,gpu.layerL2Texture.Get()) && closePixels(actual,movedPixels),"successful GPU transaction publishes one same-layer replacement");
	uint64_t previousRevision=moved.revision;
	for (const bool redo : {false,true})
	{
		auto target=redo?moved:middle; target.revision=revisions.Next(target.id,previousRevision);
		InkCanvas candidate=*document; CanvasRuntimeHistory candidateHistory=history;
		bool applied=replace(candidate,candidateHistory,target);
		const auto tiles=tilesFor(history,candidateHistory);
		const auto restored=applied?replay(candidate,candidateHistory,tiles):CompositionRestoreResult{};
		if (restored.path!=CompositionRestorePath::Failed) { *document=std::move(candidate);history=std::move(candidateHistory); }
		check(applied && restored.path!=CompositionRestorePath::Failed && target.revision>previousRevision &&
			document->Strokes()[2].Shape()->revision==target.revision && read(actual,gpu.layerL2Texture.Get()) &&
			closePixels(actual,redo?movedPixels:baseline),redo?"redo fresh revision matches original moved GPU scene":"undo fresh revision matches original baseline GPU scene");
		previousRevision=target.revision;
	}
	// 强制真正上传失败，不能用缓存命中冒充失败覆盖。失败前的文档仍是权威真值。
	const auto authoritative=document->Strokes()[2].Shape();
	auto failed=*authoritative; failed.transform.dy+=12; failed.revision=revisions.Next(failed.id,previousRevision);
	InkCanvas failedCandidate=*document; CanvasRuntimeHistory failedHistory=history;
	const bool prepared=replace(failedCandidate,failedHistory,failed);
	const auto failedTiles=tilesFor(history,failedHistory);
	cache.SetCompositionPolicy({0});
	const auto upload=gpu.inkDataBuffer; gpu.inkDataBuffer.Reset();
	const auto rejected=prepared?replay(failedCandidate,failedHistory,failedTiles):CompositionRestoreResult{};
	gpu.inkDataBuffer=upload;
	check(prepared && rejected.path==CompositionRestorePath::Failed && document->Strokes()[2].Shape()==authoritative &&
		history.Find(item)->contentGeneration!=failedHistory.Find(item)->contentGeneration,
		"actual missing upload resource rejects candidate without publishing CPU state");
	const auto recovered=replay(*document,history,failedTiles);
	check(recovered.path!=CompositionRestorePath::Failed && read(actual,gpu.layerL2Texture.Get()) &&
		closePixels(actual,movedPixels),"resource restoration replays authoritative pixels after partial candidate failure");
	cache.Release(); gpu.SetScreenSize(128,128); gpu.ClearOperatorLayer(gpu.layerL0); gpu.ClearOperatorLayer(gpu.layerL1);
}
