	void TestParametricShapePersistence(TestState& state)
	{
		using namespace draw3::shapes;
		Draw3UInkExportSnapshot snapshot;
		snapshot.fileGuid = Guid("10000000-0000-4000-8000-00000000f040");
		snapshot.workspaceGuid = Guid("30000000-0000-4000-8000-00000000f040");
		Draw3UInkCanvasSnapshot canvas;
		canvas.pageGuid = Guid("50000000-0000-4000-8000-00000000f040");
		canvas.pageNumber = 1;
		const Kind kinds[] = { Kind::Line, Kind::Rectangle, Kind::Circle, Kind::Ellipse,
			Kind::Polyline, Kind::Polygon, Kind::Compound };
		for (size_t index = 0; index < std::size(kinds); ++index)
		{
			ShapeObject object;
			object.id = Guid("70000000-0000-4000-8000-00000000f040").Bytes();
			object.id[15] = static_cast<uint8_t>(0x40 + index);
			object.revision = 7;
			object.kind = kinds[index];
			object.creationMode = CreationMode::Parameters;
			object.geometry.start = { 30, 40 };
			object.geometry.end = object.kind == Kind::Ellipse ? Vec2{ 20, 10 } : Vec2{ 90, 100 };
			object.geometry.radius = object.kind == Kind::Circle ? 16.0f : 0.0f;
			object.style.width = 3;
			object.style.strokeRgb = 0x123456;
			object.style.fillRgb = 0x657321;
			object.style.strokeOpacity = 0.55f;
			object.style.fillOpacity = 0.3f;
			object.style.linePattern = static_cast<LinePattern>(index % 4);
			object.style.dashOffset = 2;
			object.style.fill = object.kind != Kind::Line && object.kind != Kind::Polyline;
			if (object.kind == Kind::Polyline || object.kind == Kind::Polygon)
				object.geometry.vertices = { { 10, 10 }, { 80, 10 }, { 35, 35 }, { 80, 80 }, { 10, 80 } };
			if (object.kind == Kind::Rectangle)
			{
				object.geometry.radius = 4;
				object.transform = { 0, 1, -1, 0, 250, 0 };
			}
			if (object.kind == Kind::Compound)
			{
				object.style.fill = false;
				ShapePart line;
				line.id = 12; line.kind = Kind::Line;
				line.geometry.start = { 10, 10 }; line.geometry.end = { 90, 30 };
				ShapePart ellipse;
				ellipse.id = 45; ellipse.kind = Kind::Ellipse;
				ellipse.geometry.start = { 60, 70 }; ellipse.geometry.end = { 15, 25 };
				ellipse.style = object.style;
				ellipse.style->fill = true;
				ellipse.style->fillRgb = 0xabcdef;
				object.parts = { line, ellipse };
			}
			UINK_CHECK(state, static_cast<bool>(Validate(object)));
			const auto metadata = EncodeInkeysShapeMetadata(object);
			const auto decoded = DecodeInkeysShapeMetadata(metadata);
			UINK_CHECK(state, decoded.status == Draw3ShapeMetadataStatus::Valid && decoded.shape == object);
			auto corrupt = metadata;
			std::get<UInkMessagePackValue::Map>(corrupt.value).push_back(
				{ Value(std::string("revision")), Value(uint64_t{ 99 }) });
			UINK_CHECK(state, DecodeInkeysShapeMetadata(corrupt).status == Draw3ShapeMetadataStatus::Invalid);
			Draw3UInkStrokeSnapshot stroke;
			stroke.style.kind = Draw3UInkStrokeKind::Shape;
			stroke.shape = object;
			stroke.undoId = static_cast<uint32_t>(index);
			canvas.strokes.push_back(std::move(stroke));
		}
		snapshot.canvases.push_back(canvas);
		const auto exported = ExportDraw3SnapshotToUInk(snapshot);
		UINK_CHECK(state, exported.status == Draw3UInkExportStatus::Success && exported.document);
		if (!exported.document) return;
		UINK_CHECK(state, exported.capabilities.shapeCount == 7 && exported.document->canvases[0].content.size() == 8);
		const auto& wire = exported.document->canvases[0].content;
		UINK_CHECK(state, std::get<UInkShape>(wire[6]).undoId == 6 && std::get<UInkShape>(wire[7]).undoId == 6);
		const auto& rotated = std::get<UInkRectangleGeometry>(std::get<UInkShape>(wire[1]).geometry);
		UINK_CHECK(state, NearlyEqual(rotated.rotation, 1.57079632679f));
		const auto encoded = EncodeUInkDocument(*exported.document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		const auto decoded = DecodeUInk(encoded.bytes);
		UINK_CHECK(state, decoded.status == UInkReadStatus::Complete && decoded.document);
		if (!decoded.document) return;
		const auto imported = ImportDraw3UInkDocument(*decoded.document);
		UINK_CHECK(state, imported.status == Draw3UInkImportStatus::Success && imported.snapshot);
		if (!imported.snapshot) return;
		const auto& restored = imported.snapshot->canvases[0];
		UINK_CHECK(state, restored.strokes.size() == canvas.strokes.size() && restored.operations.size() == canvas.strokes.size());
		if (restored.strokes.size() != canvas.strokes.size()) return;
		for (size_t index = 0; index < canvas.strokes.size(); ++index)
			UINK_CHECK(state, restored.strokes[index].shape == canvas.strokes[index].shape && restored.strokes[index].undoId == index);

		// 没有私有扩展的通用读取器仍能显示所有标准部件。
		auto standard = *exported.document;
		for (auto& content : standard.canvases[0].content) std::get<UInkShape>(content).extra.reset();
		// 降级后每部件获得独立身份；显式独立组是旧应用导入的兼容合同。
		for (size_t i = 0; i < standard.canvases[0].content.size(); ++i) std::get<UInkShape>(standard.canvases[0].content[i]).undoId = static_cast<uint32_t>(i);
		const auto downgraded = ImportDraw3UInkDocument(standard);
		UINK_CHECK(state, downgraded.status == Draw3UInkImportStatus::Success && downgraded.snapshot);
		if (downgraded.snapshot)
		{
			const auto again = ExportDraw3SnapshotToUInk(*downgraded.snapshot);
			UINK_CHECK(state, again.status == Draw3UInkExportStatus::Success && again.document);
			if (again.document)
			{
				const auto stable = ImportDraw3UInkDocument(*again.document);
				UINK_CHECK(state, stable.snapshot && stable.snapshot->canvases[0].strokes[0].shape->id ==
					downgraded.snapshot->canvases[0].strokes[0].shape->id);
			}
		}

		// 外部无元数据的直角/大圆角矩形不得被旧固定 4px 圆角路径改写。
		for (float radius : { 0.0f, 20.0f, 4.0f })
		{
			auto rectangleDocument = *exported.document;
			auto rectangle = std::get<UInkShape>(rectangleDocument.canvases[0].content[1]);
			rectangle.extra.reset(); rectangle.fill.reset(); rectangle.contentId = rectangle.undoId = 0;
			rectangle.stroke->dashArray.clear(); rectangle.stroke->dashOffset = 0;
			auto& g = std::get<UInkRectangleGeometry>(rectangle.geometry);
			g.rotation = 0; g.cornerRadiusX = g.cornerRadiusY = radius;
			rectangleDocument.canvases[0].content = { rectangle };
			const auto importedRectangle = ImportDraw3UInkDocument(rectangleDocument);
			UINK_CHECK(state, importedRectangle.snapshot);
			if (importedRectangle.snapshot)
			{
				const auto& item = importedRectangle.snapshot->canvases[0].strokes[0];
				UINK_CHECK(state, item.shape && item.style.kind == (radius == 4 ? Draw3UInkStrokeKind::OutlineRectangle : Draw3UInkStrokeKind::Shape));
				if (item.shape && radius == 0) UINK_CHECK(state, item.shape->geometry.radius == 0);
			}
		}

		auto unknown = *exported.document;
		for (auto& content : unknown.canvases[0].content)
		{
			auto& map = std::get<UInkMessagePackValue::Map>(std::get<UInkShape>(content).extra->front().second.value);
			for (auto& pair : map)
				if (std::get<std::string>(pair.first.value) == "version") pair.second = Value(uint64_t{ 99 });
		}
		const auto fallback = ImportDraw3UInkDocument(unknown);
		UINK_CHECK(state, fallback.status == Draw3UInkImportStatus::Success && fallback.snapshot && !fallback.diagnostics.empty());

		auto malformed = *exported.document;
		auto& malformedMap = std::get<UInkMessagePackValue::Map>(std::get<UInkShape>(malformed.canvases[0].content[0]).extra->front().second.value);
		malformedMap.push_back({ Value(std::string("id")), Value(std::string("duplicate")) });
		const auto rejected = ImportDraw3UInkDocument(malformed);
		UINK_CHECK(state, rejected.status != Draw3UInkImportStatus::Success && !rejected.snapshot);
		// 后续部件只携带根对象引用，身份、版本、修订和索引必须全部匹配。
		const auto& reference = std::get<UInkMessagePackValue::Map>(std::get<UInkShape>(wire[7]).extra->front().second.value);
		UINK_CHECK(state, reference.size() == 6);
		for (const std::string field : { "id", "revision", "partIndex", "partCount", "version" })
		{
			auto tampered = *exported.document;
			auto& map = std::get<UInkMessagePackValue::Map>(std::get<UInkShape>(tampered.canvases[0].content[7]).extra->front().second.value);
			for (auto& pair : map)
				if (std::get<std::string>(pair.first.value) == field)
					pair.second = field == "id" ? Value(std::string("70000000-0000-4000-8000-00000000eeee")) : Value(uint64_t{ 99 });
			UINK_CHECK(state, ImportDraw3UInkDocument(tampered).status != Draw3UInkImportStatus::Success);
		}
		auto missingPart = *exported.document;
		missingPart.canvases[0].content.pop_back();
		UINK_CHECK(state, ImportDraw3UInkDocument(missingPart).status != Draw3UInkImportStatus::Success);
		auto duplicated = snapshot;
		duplicated.canvases[0].strokes[1].shape->id = duplicated.canvases[0].strokes[0].shape->id;
		UINK_CHECK(state, ExportDraw3SnapshotToUInk(duplicated).status == Draw3UInkExportStatus::InvalidSourceStroke);
		auto duplicateWire = *exported.document;
		auto duplicateShape = std::get<UInkShape>(duplicateWire.canvases[0].content[0]);
		duplicateShape.contentId = 8; duplicateShape.undoId = 7;
		duplicateWire.canvases[0].content.emplace_back(std::move(duplicateShape));
		UINK_CHECK(state, ImportDraw3UInkDocument(duplicateWire).status != Draw3UInkImportStatus::Success);

		// 复合展开后 Clear 的 contentId 使用 wire 索引，撤销号仍使用逻辑操作索引。
		snapshot.canvases[0].operations.clear();
		for (auto stroke : snapshot.canvases[0].strokes) snapshot.canvases[0].operations.emplace_back(std::move(stroke));
		snapshot.canvases[0].operations.emplace_back(Draw3UInkClearSnapshot{ 7 });
		const auto cleared = ExportDraw3SnapshotToUInk(snapshot);
		UINK_CHECK(state, cleared.document && std::get<UInkClear>(cleared.document->canvases[0].content.back()).contentId == 8);
		if (cleared.document)
		{
			const auto recovered = ImportDraw3UInkDocument(*cleared.document);
			UINK_CHECK(state, recovered.snapshot && recovered.snapshot->canvases[0].intervalOrdinal == 1 &&
				recovered.snapshot->canvases[0].strokes.empty() && recovered.snapshot->canvases[0].operations.size() == 8);
		}
	}

	void TestConstructedShapePersistence(TestState& state)
	{
		using namespace draw3::shapes;
		Draw3UInkExportSnapshot snapshot;
		snapshot.fileGuid = Guid("10000000-0000-4000-8000-00000000f041");
		snapshot.workspaceGuid = Guid("30000000-0000-4000-8000-00000000f041");
		Draw3UInkCanvasSnapshot canvas;
		canvas.pageGuid = Guid("50000000-0000-4000-8000-00000000f041"); canvas.pageNumber = 1;
		std::vector<ToolSpec> tools;
		for (uint32_t value = 0; value <= uint32_t(PresetKind::RegularPolygon); ++value)
		{
			const auto preset = static_cast<PresetKind>(value);
			tools.push_back({ preset,preset == PresetKind::Circle || preset == PresetKind::RegularPolygon ? CreationMode::CenterRadius : CreationMode::Drag,3 });
			if (preset == PresetKind::Rectangle) tools.back().rectangleCornerRadius = 6; // 150% 屏幕已捕获的 4 DIP。
		}
		tools.push_back({ PresetKind::Circle,CreationMode::Drag,3 });
		for (uint32_t sides = 4; sides <= 12; ++sides) tools.push_back({ PresetKind::RegularPolygon,CreationMode::CenterRadius,sides });
		size_t wireCount = 0;
		for (const auto& tool : tools)
		{
			const bool open = tool.preset == PresetKind::Line || tool.preset == PresetKind::Arrow;
			for (uint32_t pattern = 0; pattern < 4; ++pattern) for (uint32_t fill = 0; fill < (open ? 1u : 3u); ++fill)
			{
				ShapeStyle style; style.width = 5; style.strokeRgb = 0x123456; style.fillRgb = 0x654321;
				style.strokeOpacity = .55f; style.fillOpacity = .3f; style.stroke = fill != 1; style.fill = fill != 0;
				style.linePattern = static_cast<LinePattern>(pattern); style.dashOffset = 2;
				auto id = Guid("70000000-0000-4000-8000-00000000f041").Bytes();
				id[15] = static_cast<uint8_t>(canvas.strokes.size());
				const auto object = MakePresetShape(tool, { 20,30 }, { 140,110 }, style, id, 17);
				UINK_CHECK(state, object);
				if (!object) continue;
				const auto metadata = EncodeInkeysShapeMetadata(*object);
				const auto decoded = DecodeInkeysShapeMetadata(metadata);
				UINK_CHECK(state, decoded.status == Draw3ShapeMetadataStatus::Valid && decoded.shape == object);
				Draw3UInkStrokeSnapshot stroke; stroke.style.kind = Draw3UInkStrokeKind::Shape;
				stroke.shape = *object; stroke.undoId = static_cast<uint32_t>(canvas.strokes.size());
				canvas.strokes.push_back(std::move(stroke));
				wireCount += object->kind == Kind::Compound ? object->parts.size() : 1;
			}
		}
		snapshot.canvases.push_back(canvas);
		const auto exported = ExportDraw3SnapshotToUInk(snapshot);
		UINK_CHECK(state, exported.document && exported.capabilities.shapeCount == canvas.strokes.size());
		if (!exported.document) return;
		UINK_CHECK(state, exported.document->canvases[0].content.size() == wireCount);
		// 文件写读覆盖所有模板、两种圆、所有边数、四线型和三种样式；恢复仍是单个逻辑对象。
		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		UInkEditingSession session; session.document = *exported.document;
		UInkSaveOptions options; options.mode = UInkSaveMode::CreateNewLogicalFileWithIdentity;
		const auto path = temporary.File(L"constructed-shapes.uink");
		UINK_CHECK(state, SaveUInkFile(path, session, options).status == UInkSaveStatus::Committed);
		const auto read = ReadUInkFile(path);
		UINK_CHECK(state, read.status == UInkReadStatus::Complete && read.document);
		if (!read.document) return;
		const auto restored = ImportDraw3UInkDocument(*read.document);
		UINK_CHECK(state, restored.snapshot && restored.snapshot->canvases[0].strokes.size() == canvas.strokes.size());
		if (restored.snapshot && restored.snapshot->canvases[0].strokes.size() == canvas.strokes.size())
			for (size_t i = 0; i < canvas.strokes.size(); ++i)
				UINK_CHECK(state, restored.snapshot->canvases[0].strokes[i].shape == canvas.strokes[i].shape && restored.snapshot->canvases[0].strokes[i].undoId == i);
		auto field = [](UInkMessagePackValue& value, const char* name) -> UInkMessagePackValue*
		{
			for (auto& pair : std::get<UInkMessagePackValue::Map>(value.value))
				if (std::get<std::string>(pair.first.value) == name) return &pair.second;
			return nullptr;
		};
		const auto metadata = EncodeInkeysShapeMetadata(*canvas.strokes.back().shape);
		for (uint64_t sides : { 2u,13u,7u })
		{
			auto malformed = metadata;
			*field(*field(*field(malformed, "construction"), "tool"), "sides") = Value(sides);
			UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::Invalid);
		}
		auto malformed = metadata;
		*field(*field(*field(malformed, "construction"), "tool"), "rectangleCornerRadius") = Value(-1.0f);
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::Invalid);
		malformed = metadata;
		*field(*field(malformed, "construction"), "end") = Value(UInkMessagePackValue::Array{ Value(999.0f),Value(100.0f) });
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::Invalid);
		malformed = metadata;
		*field(malformed, "version") = Value(uint64_t{ 1 });
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::Invalid);
		malformed = metadata;
		*field(malformed, "construction") = Value(UInkMessagePackValue::Map{});
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::Invalid);
		malformed = EncodeInkeysShapeMetadata(*canvas.strokes.front().shape);
		*field(malformed, "version") = Value(uint64_t{ 99 });
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::UnsupportedVersion);
		// v2 复合引用不能退回 v1，也不能在支持的版本里偷偷丢弃构造信息。
		auto mismatch = *exported.document;
		bool testedReference = false;
		for (auto& content : mismatch.canvases[0].content)
		{
			auto& extra = std::get<UInkShape>(content).extra->front().second;
			if (field(extra, "reference"))
			{
				*field(extra, "version") = Value(uint64_t{ 1 }); testedReference = true; break;
			}
		}
		UINK_CHECK(state, testedReference && ImportDraw3UInkDocument(mismatch).status != Draw3UInkImportStatus::Success);
		auto unknown = *exported.document;
		auto standard = *exported.document;
		for (size_t i = 0; i < unknown.canvases[0].content.size(); ++i)
		{
			auto& extra = std::get<UInkShape>(unknown.canvases[0].content[i]).extra->front().second;
			*field(extra, "version") = Value(uint64_t{ 99 });
			auto& part = std::get<UInkShape>(standard.canvases[0].content[i]);
			part.extra.reset(); part.undoId = static_cast<uint32_t>(i);
		}
		const auto fallback = ImportDraw3UInkDocument(unknown);
		UINK_CHECK(state, fallback.snapshot && !fallback.diagnostics.empty() && fallback.snapshot->canvases[0].strokes.size() == wireCount);
		UINK_CHECK(state, ImportDraw3UInkDocument(standard).status == Draw3UInkImportStatus::Success);
		// v3参数覆盖曲线、逐点与全部空间模板；标准降级仍能被无扩展读取器显示。
		auto advancedSnapshot = snapshot; advancedSnapshot.canvases[0].strokes.clear();
		for (uint32_t value = uint32_t(PresetKind::Polyline); value <= uint32_t(PresetKind::Pyramid); ++value)
		{
			const auto preset = static_cast<PresetKind>(value);
			for (uint32_t sides : { 3u,5u,12u }) for (float sign : { -1.0f,1.0f })
			{
				Construction c; c.tool = { preset,preset == PresetKind::Polyline || preset == PresetKind::Polygon ? CreationMode::Points : CreationMode::MultiStage,sides };
				c.start = { 30,70 }; c.end = { 130,110 };
				if (preset == PresetKind::Polyline || preset == PresetKind::Polygon)
					c.parameters = VertexConstruction{ { { 10,10 },{ 100,10 },{ 45,40 },{ 100,100 },{ 10,100 } } };
				else if (preset == PresetKind::Arc || preset == PresetKind::Sector) c.parameters = ArcConstruction{ sign * 4.4f };
				else if (preset == PresetKind::Cuboid || preset == PresetKind::Cube) c.parameters = SolidConstruction{ { sign * 23,-32 },0 };
				else c.parameters = SolidConstruction{ {},sign * 60 };
				ShapeStyle style; style.width = 5; style.linePattern = LinePattern::DashDot;
				style.strokeOpacity = .55f; style.strokeRgb = 0x123456;
				auto id = Guid("70000000-0000-4000-8000-00000000f042").Bytes(); id[15] = static_cast<uint8_t>(advancedSnapshot.canvases[0].strokes.size());
				auto object = MakeConstructedShape(c, style, id, 21);
				UINK_CHECK(state, object); if (!object) continue;
				const auto encoded = EncodeInkeysShapeMetadata(*object); const auto decoded = DecodeInkeysShapeMetadata(encoded);
				UINK_CHECK(state, decoded.status == Draw3ShapeMetadataStatus::Valid && decoded.shape == object);
				auto invalid = encoded;
				*field(*field(invalid, "construction"), "parameters") = Value(UInkMessagePackValue::Array{ Value(uint64_t{ 99 }) });
				UINK_CHECK(state, DecodeInkeysShapeMetadata(invalid).status == Draw3ShapeMetadataStatus::Invalid);
				Draw3UInkStrokeSnapshot stroke; stroke.style.kind = Draw3UInkStrokeKind::Shape; stroke.shape = object;
				stroke.undoId = static_cast<uint32_t>(advancedSnapshot.canvases[0].strokes.size());
				advancedSnapshot.canvases[0].strokes.push_back(std::move(stroke));
			}
		}
		const auto advancedExport = ExportDraw3SnapshotToUInk(advancedSnapshot);
		UINK_CHECK(state, advancedExport.document);
		if (advancedExport.document)
		{
			UInkEditingSession futureSession; futureSession.document = *advancedExport.document;
			const auto advancedPath = temporary.File(L"advanced-shapes-v3.uink");
			UINK_CHECK(state, SaveUInkFile(advancedPath, futureSession, options).status == UInkSaveStatus::Committed);
			const auto advancedRead = ReadUInkFile(advancedPath);
			UINK_CHECK(state, advancedRead.status == UInkReadStatus::Complete && advancedRead.document);
			if (advancedRead.document)
			{
				const auto restoredAdvanced = ImportDraw3UInkDocument(*advancedRead.document);
				UINK_CHECK(state, restoredAdvanced.snapshot && restoredAdvanced.snapshot->canvases[0].strokes.size() == advancedSnapshot.canvases[0].strokes.size());
				if (restoredAdvanced.snapshot && restoredAdvanced.snapshot->canvases[0].strokes.size() == advancedSnapshot.canvases[0].strokes.size())
					for (size_t i = 0; i < advancedSnapshot.canvases[0].strokes.size(); ++i)
						UINK_CHECK(state, restoredAdvanced.snapshot->canvases[0].strokes[i].shape == advancedSnapshot.canvases[0].strokes[i].shape);
			}
			auto fallbackDocument = *advancedExport.document;
			for (size_t i = 0; i < fallbackDocument.canvases[0].content.size(); ++i)
			{
				auto& part = std::get<UInkShape>(fallbackDocument.canvases[0].content[i]); part.extra.reset(); part.undoId = static_cast<uint32_t>(i);
			}
			const auto fallbackAdvanced = ImportDraw3UInkDocument(fallbackDocument);
			UINK_CHECK(state, fallbackAdvanced.snapshot && fallbackAdvanced.snapshot->canvases[0].strokes.size() == fallbackDocument.canvases[0].content.size());
		}
		// 人工重建v2 wire，保留旧箭头公式；重存v3仍不能改变外观或根/引用版本。
		Construction oldArrow{ { PresetKind::Arrow,CreationMode::Drag },{ 0,0 },{ 100,0 } }; oldArrow.legacyArrow = true;
		auto old = MakeConstructedShape(oldArrow, {}, Guid("70000000-0000-4000-8000-00000000f043").Bytes(), 9);
		UINK_CHECK(state, old);
		if (old)
		{
			auto legacyMetadata = EncodeInkeysShapeMetadata(*old); *field(legacyMetadata, "version") = Value(uint64_t{ 2 });
			auto& constructionMap = std::get<UInkMessagePackValue::Map>(field(legacyMetadata, "construction")->value);
			constructionMap.erase(std::remove_if(constructionMap.begin(), constructionMap.end(), [](const auto& pair)
				{ const auto& key = std::get<std::string>(pair.first.value); return key == "parameters" || key == "legacyArrow"; }), constructionMap.end());
			const auto restoredLegacy = DecodeInkeysShapeMetadata(legacyMetadata);
			UINK_CHECK(state, restoredLegacy.status == Draw3ShapeMetadataStatus::Valid && restoredLegacy.shape == old);
			const auto resaved = DecodeInkeysShapeMetadata(EncodeInkeysShapeMetadata(*old));
			UINK_CHECK(state, resaved.status == Draw3ShapeMetadataStatus::Valid && resaved.shape == old);
			auto oldSnapshot = advancedSnapshot; oldSnapshot.canvases[0].strokes.resize(1); oldSnapshot.canvases[0].strokes[0].shape = old;
			auto oldExport = ExportDraw3SnapshotToUInk(oldSnapshot);
			UINK_CHECK(state, oldExport.document);
			if (oldExport.document)
			{
				for (size_t i = 0; i < oldExport.document->canvases[0].content.size(); ++i)
				{
					auto& wire = std::get<UInkShape>(oldExport.document->canvases[0].content[i]).extra->front().second;
					*field(wire, "version") = Value(uint64_t{ 2 });
					if (!i)
					{
						auto& fields = std::get<UInkMessagePackValue::Map>(field(wire, "construction")->value);
						fields.erase(std::remove_if(fields.begin(), fields.end(), [](const auto& pair)
							{ const auto& key = std::get<std::string>(pair.first.value); return key == "parameters" || key == "legacyArrow"; }), fields.end());
					}
				}
				const auto oldImport = ImportDraw3UInkDocument(*oldExport.document);
				UINK_CHECK(state, oldImport.snapshot && oldImport.snapshot->canvases[0].strokes[0].shape == old);
			}
		}
		std::cout << "Constructed shape UInk cases: " << canvas.strokes.size() << " objects / " << wireCount << " standard parts" << std::endl;
	}
