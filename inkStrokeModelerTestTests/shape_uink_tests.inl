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
				if (std::get<std::string>(pair.first.value) == "version") pair.second = Value(uint64_t{ 2 });
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
