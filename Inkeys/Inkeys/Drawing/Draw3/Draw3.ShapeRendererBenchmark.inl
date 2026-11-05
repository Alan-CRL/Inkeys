// 4K、2000 条普通笔迹页面：同一 WARP 设备上对比旧预览路径（复制文档+清空缓存+重放 tile）与焦点层（只重画 L1 中一个对象）。
// 计时包含 GPU 完成（1 像素回读同步），并包含每帧上屏合成（L2 拷贝 + L1 叠加脏区），仅作诊断输出。
{
	constexpr int kBenchWidth = 3840, kBenchHeight = 2160, kBenchStrokes = 2000, kBenchFrames = 12;
	InkRenderer bench;
	if (check(bench.Init(device.Get(), context.Get(), nullptr, kBenchWidth, kBenchHeight, D3D_DRIVER_TYPE_WARP, false),
		"4K benchmark renderer"))
	{
		std::array<uint8_t,16> benchBytes{}; benchBytes[0] = 0xB4;
		InkPage benchPage{InkGuid(benchBytes)};
		auto* benchCanvas = benchPage.GetOrCreateCanvas(kDefaultDeviceKey,{0,0,1});
		CanvasRuntimeHistory benchHistory;
		InkHistoryGpuCache benchCache;
		bool benchReady = benchCanvas && benchCache.Initialize(bench,{},{});
		const HistoryCanvasIdentity benchIdentity{benchPage.PageGuid(),kDefaultDeviceKey};
		const auto benchAppend = [&](InkStroke value)
		{
			auto footprint = BuildStrokeTileFootprint(value);
			const auto index = footprint ? benchCanvas->AppendStroke(std::move(value)) : std::nullopt;
			return index && benchHistory.AppendStroke(*index,std::move(*footprint)).has_value();
		};
		uint32_t seed = 0x2468ACE1u;
		const auto next = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24); };
		for (int index = 0; benchReady && index < kBenchStrokes; ++index)
		{
			std::vector<StoredInkPoint> points;
			float x = 40 + next() * (kBenchWidth - 280), y = 40 + next() * (kBenchHeight - 280);
			for (int point = 0; point < 16; ++point) { points.push_back({x,y,4}); x += 6 + next() * 10; y += next() * 16 - 8; }
			benchReady = benchAppend(InkStroke({StoredInkType::Pen,0x204060u + static_cast<uint32_t>(index),1.0f,0},std::move(points)));
		}
		auto benchShape = object(); benchShape.kind = Kind::Rectangle; benchShape.geometry.start = {1500,800}; benchShape.geometry.end = {2300,1300};
		benchShape.style.fill = true; benchShape.style.fillOpacity = .5f; benchShape.revision = 1;
		if (benchReady) benchReady = benchAppend(stroke(benchShape));
		D3D11_TEXTURE2D_DESC compositeDescription = {};
		if (bench.layerL2Texture) bench.layerL2Texture->GetDesc(&compositeDescription);
		ComPtr<ID3D11Texture2D> compositeTexture; ComPtr<ID3D11RenderTargetView> compositeRTV;
		benchReady = benchReady && SUCCEEDED(device->CreateTexture2D(&compositeDescription,nullptr,compositeTexture.GetAddressOf())) &&
			SUCCEEDED(device->CreateRenderTargetView(compositeTexture.Get(),nullptr,compositeRTV.GetAddressOf()));
		if (check(benchReady,"4K benchmark page with 2000 ink strokes and one trailing edited shape"))
		{
			const auto benchItem = benchHistory.Items().back().id;
			const auto originalTiles = benchHistory.Find(benchItem)->compositionTiles;
			D3D11_TEXTURE2D_DESC pixelDescription = compositeDescription;
			pixelDescription.Width = pixelDescription.Height = 1; pixelDescription.Usage = D3D11_USAGE_STAGING;
			pixelDescription.BindFlags = 0; pixelDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ; pixelDescription.MiscFlags = 0;
			ComPtr<ID3D11Texture2D> pixel;
			device->CreateTexture2D(&pixelDescription,nullptr,pixel.GetAddressOf());
			LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
			const auto now = [] { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; };
			// 等待 GPU 完成，使计时包含真实光栅成本而非仅命令录制。
			const auto finish = [&](ID3D11Texture2D* source)
			{
				if (!pixel) { context->Flush(); return; }
				context->OMSetRenderTargets(0,nullptr,nullptr);
				D3D11_BOX box{0,0,0,1,1,1}; context->CopySubresourceRegion(pixel.Get(),0,0,0,0,source,0,&box);
				D3D11_MAPPED_SUBRESOURCE mapped{}; if (SUCCEEDED(context->Map(pixel.Get(),0,D3D11_MAP_READ,0,&mapped))) context->Unmap(pixel.Get(),0);
			};
			const auto elapsed = [&](int64_t start) { return static_cast<double>(now() - start) * 1000.0 / static_cast<double>(frequency.QuadPart); };
			// 上屏：与 CompositeLayersToBackBuffer 相同，脏区内 L2 拷贝后叠加 L1/L0。
			const auto present = [&](RECT dirty)
			{
				dirty = ClampRectToCanvas(dirty,kBenchWidth,kBenchHeight); if (IsEmptyRect(dirty)) return true;
				bench.CopyResource(compositeTexture.Get(),bench.layerL2Texture.Get(),dirty);
				return bench.ApplyOperatorLayers(compositeRTV.Get(),bench.layerL1,bench.layerL0,dirty);
			};
			const auto belowTiles = benchHistory.VisibleCompositionTiles({0,0,static_cast<float>(kBenchWidth),static_cast<float>(kBenchHeight)});
			// 初始页面只含下层 2000 条笔迹（编辑对象此时在焦点层，不在 L2）。
			int64_t started = now();
			CompositionRestoreRequest initial{benchIdentity,key,benchCanvas,&benchHistory,belowTiles,benchHistory.Items().size() - 1,0,0,kBenchWidth,kBenchHeight,true};
			const bool warmed = benchCache.RestoreComposition(initial).path != CompositionRestorePath::Failed; finish(bench.layerL2Texture.Get());
			const double warmMs = elapsed(started);
			check(warmed,"4K benchmark authoritative page raster");
			const auto moved = [&](int frame)
			{
				auto value = benchShape; value.transform.dx = static_cast<float>(23 * frame); value.transform.dy = static_cast<float>(11 * frame);
				value.revision = static_cast<uint64_t>(frame) + 2; return value;
			};
			std::vector<InkPoint> points; HighlighterGeometry highlighter;
			const StoredStrokeRasterTarget target{&bench.layerL1,0,0,kBenchWidth,kBenchHeight};
			const auto shapeStroke = benchCanvas->Strokes()[benchHistory.Find(benchItem)->strokeIndex];
			// 焦点层：首帧（抬笔）只把对象画进 L1 并上屏；之后每帧只清 L1、画一个候选、上屏脏区。
			const auto restoresBefore = CompositionRestoreCallsTotal();
			started = now();
			bench.ClearOperatorLayer(bench.layerL1); bench.ClearOperatorLayer(bench.layerL0);
			auto drawn = DrawFocusLayerItem(shapeStroke,nullptr,bench,target,points,highlighter);
			bool focusOk = drawn.succeeded && present(drawn.dirty); finish(compositeTexture.Get());
			const double focusFirst = elapsed(started);
			double focusTotal = 0, focusMax = 0; RECT previous = drawn.dirty;
			for (int frame = 0; focusOk && frame < kBenchFrames; ++frame)
			{
				const auto candidate = moved(frame + 1);
				started = now();
				bench.ClearOperatorLayer(bench.layerL1);
				drawn = DrawFocusLayerItem(shapeStroke,&candidate,bench,target,points,highlighter);
				RECT dirty = previous; UnionRectInPlace(dirty,drawn.dirty); previous = drawn.dirty;
				focusOk = drawn.succeeded && present(dirty); finish(compositeTexture.Get());
				const double cost = elapsed(started); focusTotal += cost; focusMax = (std::max)(focusMax,cost);
			}
			// 落定：最终对象一次光栅化进 L2。
			const auto finalShape = moved(kBenchFrames);
			const InkStroke finalStroke = stroke(finalShape);
			started = now();
			bench.ClearOperatorLayer(bench.layerL1);
			drawn = DrawFocusLayerItem(finalStroke,nullptr,bench,target,points,highlighter);
			focusOk = focusOk && drawn.succeeded && ResolveFocusLayerToL2(bench,bench.layerL1,drawn.dirty); finish(bench.layerL2Texture.Get());
			bench.ClearOperatorLayer(bench.layerL1);
			const double materializeMs = elapsed(started);
			const bool noReplay = CompositionRestoreCallsTotal() == restoresBefore;
			// 旧路径：与原 replayShapeEdit 预览分支相同，复制画布/sidecar、替换、清空缓存后重放 旧∪旧预览∪新 tile。
			CompositionRestoreRequest full = initial; full.rangeEnd = benchHistory.Items().size();
			benchReady = benchCache.RestoreComposition(full).path != CompositionRestorePath::Failed;
			double legacyTotal = 0, legacyMax = 0, legacyFirst = 0; std::vector<SignedTileCoordinate> previousTiles;
			bool legacyOk = warmed && benchReady;
			for (int frame = 0; legacyOk && frame < kBenchFrames; ++frame)
			{
				const auto candidate = moved(frame + 1);
				started = now();
				InkCanvas copy = *benchCanvas; CanvasRuntimeHistory copyHistory = benchHistory;
				InkStroke replacement = stroke(candidate); auto footprint = BuildStrokeTileFootprint(replacement);
				std::vector<SignedTileCoordinate> tiles = originalTiles; tiles.insert(tiles.end(),previousTiles.begin(),previousTiles.end());
				legacyOk = footprint && copy.ReplaceStroke(benchHistory.Find(benchItem)->strokeIndex,std::move(replacement));
				if (!legacyOk) break;
				tiles.insert(tiles.end(),footprint->compositionTiles.begin(),footprint->compositionTiles.end());
				previousTiles = footprint->compositionTiles;
				legacyOk = copyHistory.UpdateItemGeometry(benchItem,std::move(*footprint));
				benchCache.DiscardHotPreimages(); benchCache.DiscardCompositionCache(); bench.ClearShapeRenderCache();
				const CompositionRestoreRequest request{benchIdentity,key,&copy,&copyHistory,tiles,copyHistory.Items().size(),0,0,kBenchWidth,kBenchHeight,true};
				legacyOk = legacyOk && benchCache.RestoreComposition(request).path != CompositionRestorePath::Failed; finish(bench.layerL2Texture.Get());
				const double cost = elapsed(started); legacyTotal += cost; legacyMax = (std::max)(legacyMax,cost);
				if (frame == 0) legacyFirst = cost;
			}
			const double legacyAverage = legacyTotal / kBenchFrames, focusAverage = focusTotal / kBenchFrames;
			std::fprintf(stderr,"[ShapeRenderer.Bench] strokes=%d size=%dx%d warm_ms=%.1f legacy_first_ms=%.2f legacy_avg_ms=%.2f legacy_max_ms=%.2f focus_first_ms=%.2f focus_avg_ms=%.2f focus_max_ms=%.2f materialize_ms=%.2f speedup=%.1fx\n",
				kBenchStrokes,kBenchWidth,kBenchHeight,warmMs,legacyFirst,legacyAverage,legacyMax,focusFirst,focusAverage,focusMax,materializeMs,
				focusAverage > 0 ? legacyAverage / focusAverage : 0.0);
			check(legacyOk && focusOk,"4K benchmark legacy and focus-layer previews both succeed");
			check(noReplay,"4K focus-layer session (first frame, previews, materialize) performs no tile replay");
			check(focusFirst < legacyFirst && focusAverage < legacyAverage,"4K focus-layer first frame and steady frames are cheaper than legacy previews");
		}
		benchCache.Release();
	}
}
