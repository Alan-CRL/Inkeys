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
			UINK_CHECK(state, decoded.status == Draw3ShapeMetadataStatus::Valid && decoded.shape == object && decoded.shape->style.auxiliary == Auxiliary::None);
			if (object.kind == Kind::Circle || object.kind == Kind::Ellipse)
			{
				auto auxiliary = object; auxiliary.style.auxiliary = object.kind == Kind::Circle ? Auxiliary::Center : Auxiliary::Foci;
				const auto v4 = DecodeInkeysShapeMetadata(EncodeInkeysShapeMetadata(auxiliary));
				UINK_CHECK(state,v4.shape == auxiliary); // 无construction的旧通用对象也能开启辅助显示。
			}
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
		UINK_CHECK(state, exported.capabilities.shapeCount == 7 && exported.document->canvases[0].content.size() == 48);
		const auto& wire = exported.document->canvases[0].content;
		UINK_CHECK(state, std::get<UInkShape>(wire[38]).undoId == 6 && std::get<UInkShape>(wire[47]).undoId == 6);
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

		// 外部无元数据的直角/大圆角/非缩放档位圆角矩形不得被旧固定 4 DIP 圆角路径改写；
		// 125%/150%/200%/400% 屏写出的旧矩形圆角为 4*dpiScale，仍须识别为旧矩形。
		for (float radius : { 0.0f, 20.0f, 4.0f, 5.0f, 6.0f, 8.0f, 16.0f, 7.5f, 6.5f })
		{
			const bool legacyRadius = radius == 4 || radius == 5 || radius == 6 || radius == 8 || radius == 16;
			auto rectangleDocument = *exported.document;
			auto rectangle = std::get<UInkShape>(rectangleDocument.canvases[0].content[1]);
			rectangle.extra.reset(); rectangle.fill.reset(); rectangle.contentId = rectangle.undoId = 0;
			// v6 混合矩形首部件仅填充；描边属性来自后续标准虚线部件。
			rectangle.stroke = std::get<UInkShape>(rectangleDocument.canvases[0].content[2]).stroke;
			UINK_CHECK(state,rectangle.stroke); if(!rectangle.stroke) continue;
			rectangle.stroke->dashArray.clear(); rectangle.stroke->dashOffset = 0;
			auto& g = std::get<UInkRectangleGeometry>(rectangle.geometry);
			g.rotation = 0; g.cornerRadiusX = g.cornerRadiusY = radius;
			rectangleDocument.canvases[0].content = { rectangle };
			const auto importedRectangle = ImportDraw3UInkDocument(rectangleDocument);
			UINK_CHECK(state, importedRectangle.snapshot);
			if (importedRectangle.snapshot)
			{
				const auto& item = importedRectangle.snapshot->canvases[0].strokes[0];
				UINK_CHECK(state, item.shape && item.style.kind == (legacyRadius ? Draw3UInkStrokeKind::OutlineRectangle : Draw3UInkStrokeKind::Shape));
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
		duplicateShape.contentId = 48; duplicateShape.undoId = 7;
		duplicateWire.canvases[0].content.emplace_back(std::move(duplicateShape));
		UINK_CHECK(state, ImportDraw3UInkDocument(duplicateWire).status != Draw3UInkImportStatus::Success);

		// 复合展开后 Clear 的 contentId 使用 wire 索引，撤销号仍使用逻辑操作索引。
		snapshot.canvases[0].operations.clear();
		for (auto stroke : snapshot.canvases[0].strokes) snapshot.canvases[0].operations.emplace_back(std::move(stroke));
		snapshot.canvases[0].operations.emplace_back(Draw3UInkClearSnapshot{ 7 });
		const auto cleared = ExportDraw3SnapshotToUInk(snapshot);
		UINK_CHECK(state, cleared.document && std::get<UInkClear>(cleared.document->canvases[0].content.back()).contentId == 48);
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
		for (uint32_t sides = 4; sides <= 14; ++sides) tools.push_back({ PresetKind::RegularPolygon,CreationMode::CenterRadius,sides });
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
				const uint32_t serial = static_cast<uint32_t>(canvas.strokes.size());
				for (size_t byte = 0; byte < 4; ++byte) id[12 + byte] = static_cast<uint8_t>(serial >> ((3 - byte) * 8));
				const auto object = MakePresetShape(tool, { 20,30 }, { 140,110 }, style, id, 17);
				UINK_CHECK(state, object);
				if (!object) continue;
				const auto metadata = EncodeInkeysShapeMetadata(*object);
				const auto decoded = DecodeInkeysShapeMetadata(metadata);
				UINK_CHECK(state, decoded.status == Draw3ShapeMetadataStatus::Valid && decoded.shape == object);
				Draw3UInkStrokeSnapshot stroke; stroke.style.kind = Draw3UInkStrokeKind::Shape;
				stroke.shape = *object; stroke.undoId = static_cast<uint32_t>(canvas.strokes.size());
				canvas.strokes.push_back(std::move(stroke));
				// 以夹具输入语义计数：闭合虚线固定八段，混合模式另有一个填充部件。
				wireCount += !open && pattern && style.stroke ? (style.fill ? 9u : 8u) :
					object->kind == Kind::Compound ? object->parts.size() : 1;
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
		for (uint64_t sides : { 2u,15u,7u })
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
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::UnsupportedVersion);
		malformed = metadata;
		*field(malformed, "construction") = Value(UInkMessagePackValue::Map{});
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::Invalid);
		malformed = EncodeInkeysShapeMetadata(*canvas.strokes.front().shape);
		*field(malformed, "version") = Value(uint64_t{ 99 });
		UINK_CHECK(state, DecodeInkeysShapeMetadata(malformed).status == Draw3ShapeMetadataStatus::UnsupportedVersion);
		// 当前复合引用不能混入退役版本。
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
		// v7参数覆盖曲线、逐点与全部空间模板；标准降级仍能被无扩展读取器显示。
		auto advancedSnapshot = snapshot; advancedSnapshot.canvases[0].strokes.clear();
		for (uint32_t value = uint32_t(PresetKind::Polyline); value <= uint32_t(PresetKind::Pyramid); ++value)
		{
			const auto preset = static_cast<PresetKind>(value);
			for (uint32_t sides : { 3u,5u,6u,8u }) for (float sign : { -1.0f,1.0f })
			{
				Construction c; c.tool = { preset,preset == PresetKind::Polyline || preset == PresetKind::Polygon ? CreationMode::Points : CreationMode::MultiStage,sides };
				c.start = { 30,70 }; c.end = { 130,110 };
				if (preset == PresetKind::Polyline || preset == PresetKind::Polygon)
					c.parameters = VertexConstruction{ { { 10,10 },{ 100,10 },{ 45,40 },{ 100,100 },{ 10,100 } } };
				else if (preset == PresetKind::Arc || preset == PresetKind::Sector) c.parameters = ArcConstruction{ sign * 4.4f };
				else c = MakeSolidConstruction(c.tool,c.start,c.end,sign * 60);
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
			const auto advancedPath = temporary.File(L"advanced-shapes-v7.uink");
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
		std::vector<ShapeObject> v5Objects;
		auto v5Id = [&](uint8_t seed) { auto id = Guid("70000000-0000-4000-8000-00000000f046").Bytes(); id[15] = seed; return id; };
		const auto regular14 = MakePresetShape({PresetKind::RegularPolygon,CreationMode::CenterRadius,14},{30,70},{130,110},{},v5Id(1));
		UINK_CHECK(state,regular14); if (regular14) v5Objects.push_back(*regular14);
		const auto freeTriangle = MakePresetShape({PresetKind::Triangle,CreationMode::Drag},{20,30},{140,110},{},v5Id(2));
		UINK_CHECK(state,freeTriangle);
		if (freeTriangle)
		{
			const auto iso = SetPlanarConstraint(*freeTriangle,PlanarConstraint::Isosceles,2);
			UINK_CHECK(state,iso); if (iso) v5Objects.push_back(*iso);
			const auto equilateral = iso ? SetPlanarConstraint(*iso,PlanarConstraint::Equilateral,3) : std::nullopt;
			UINK_CHECK(state,equilateral); if (equilateral) v5Objects.push_back(*equilateral);
		}
		const auto trapezoid = MakePresetShape({PresetKind::Trapezoid,CreationMode::Drag},{20,30},{140,110},{},v5Id(3));
		UINK_CHECK(state,trapezoid);
		if (trapezoid)
		{
			const auto iso = SetPlanarConstraint(*trapezoid,PlanarConstraint::Isosceles,2);
			UINK_CHECK(state,iso); if (iso) v5Objects.push_back(*iso);
			if (iso)
			{
				const auto point = iso->geometry.vertices[2];
				const auto tilted = EditShape(*iso,1002,point,{point.x+10,point.y+20},3);
				const auto free = tilted ? SetPlanarConstraint(*tilted,PlanarConstraint::Free,4) : std::nullopt;
				UINK_CHECK(state,free); if (free)
				{
					auto wire = EncodeInkeysShapeMetadata(*free); const auto decoded = DecodeInkeysShapeMetadata(wire);
					UINK_CHECK(state,std::get<uint64_t>(field(wire,"version")->value)==7 && decoded.shape==free);
				}
			}
		}
		for (auto preset : {PresetKind::Prism,PresetKind::Pyramid})
		{
			auto c = MakeSolidConstruction({preset,CreationMode::MultiStage,8},{30,70},{130,110},-80);
			const auto object = MakeConstructedShape(c,{},v5Id(static_cast<uint8_t>(v5Objects.size() + 1)),4);
			UINK_CHECK(state,object); if (object) v5Objects.push_back(*object);
		}
		for (const auto& object : v5Objects)
		{
			auto encoded = EncodeInkeysShapeMetadata(object);
			UINK_CHECK(state,std::get<uint64_t>(field(encoded,"version")->value) == 7);
			const auto decoded = DecodeInkeysShapeMetadata(encoded);
			UINK_CHECK(state,decoded.status == Draw3ShapeMetadataStatus::Valid && decoded.shape == object);
		}
		// 辅助部件写入真实文件；根被旧读取器忽略后，标准点/虚线仍完整可见。
		auto auxiliarySnapshot = snapshot; auxiliarySnapshot.canvases[0].strokes.clear();
		size_t auxiliaryWireCount = 0;
		for (auto preset : {PresetKind::Circle,PresetKind::Ellipse,PresetKind::Rectangle,PresetKind::RoundedRectangle,
			PresetKind::Parallelogram,PresetKind::Rhombus,PresetKind::Trapezoid,PresetKind::RegularPolygon,PresetKind::Arc,PresetKind::Sector})
		{
			Construction c; c.tool = {preset,DefaultCreationMode(preset),5}; c.start = {20,30}; c.end = {140,110};
			if (preset == PresetKind::Arc || preset == PresetKind::Sector) c.parameters = ArcConstruction{2.3f};
			ShapeStyle style; style.auxiliary = SupportedAuxiliary(preset); style.width = 3; style.strokeOpacity = .4f;
			style.stroke = preset != PresetKind::Ellipse; style.fill = !style.stroke; style.fillRgb = 0x123456;
			auto id = Guid("70000000-0000-4000-8000-00000000f044").Bytes(); id[15] = static_cast<uint8_t>(auxiliarySnapshot.canvases[0].strokes.size());
			auto object = MakeConstructedShape(c,style,id,23); UINK_CHECK(state,object); if (!object) continue;
			object->transform = {0,1,-1,0,200,10};
			const auto roundtrip = DecodeInkeysShapeMetadata(EncodeInkeysShapeMetadata(*object));
			UINK_CHECK(state,roundtrip.shape == object);
			GeometryResult auxiliary; UINK_CHECK(state,BuildAuxiliaryGeometry(*object,auxiliary));
			auxiliaryWireCount += (object->kind == Kind::Compound ? object->parts.size() : 1) + auxiliary.parts.size();
			Draw3UInkStrokeSnapshot stroke; stroke.style.kind = Draw3UInkStrokeKind::Shape; stroke.shape = object;
			stroke.undoId = static_cast<uint32_t>(auxiliarySnapshot.canvases[0].strokes.size());
			auxiliarySnapshot.canvases[0].strokes.push_back(std::move(stroke));
		}
		const auto auxiliaryExport = ExportDraw3SnapshotToUInk(auxiliarySnapshot);
		UINK_CHECK(state,auxiliaryExport.document && auxiliaryExport.document->canvases[0].content.size() == auxiliaryWireCount);
		if (auxiliaryExport.document)
		{
			UInkEditingSession auxiliarySession; auxiliarySession.document = *auxiliaryExport.document;
			const auto auxiliaryPath = temporary.File(L"auxiliary-shapes-v4.uink");
			UINK_CHECK(state,SaveUInkFile(auxiliaryPath,auxiliarySession,options).status == UInkSaveStatus::Committed);
			const auto auxiliaryRead = ReadUInkFile(auxiliaryPath);
			UINK_CHECK(state,auxiliaryRead.document);
			if (auxiliaryRead.document)
			{
				const auto imported = ImportDraw3UInkDocument(*auxiliaryRead.document);
				UINK_CHECK(state,imported.snapshot && imported.snapshot->canvases[0].strokes.size() == auxiliarySnapshot.canvases[0].strokes.size());
				if (imported.snapshot && imported.snapshot->canvases[0].strokes.size() == auxiliarySnapshot.canvases[0].strokes.size())
					for (size_t i = 0; i < auxiliarySnapshot.canvases[0].strokes.size(); ++i)
						UINK_CHECK(state,imported.snapshot->canvases[0].strokes[i].shape == auxiliarySnapshot.canvases[0].strokes[i].shape);
			}
			auto standard = *auxiliaryExport.document, unknown = standard;
			size_t dots = 0, diagonals = 0;
			for (size_t i = 0; i < standard.canvases[0].content.size(); ++i)
			{
				auto& part = std::get<UInkShape>(standard.canvases[0].content[i]);
				if (std::holds_alternative<UInkCircleGeometry>(part.geometry) && part.fill && !part.stroke) ++dots;
				if (part.declaredShapeType == 0 && part.stroke && part.stroke->dashArray == std::vector<float>{12,18}) ++diagonals;
				part.extra.reset(); // 不改undoId，模拟第三方只留下标准部件。
				auto& metadata = std::get<UInkShape>(unknown.canvases[0].content[i]).extra->front().second;
				UINK_CHECK(state,std::get<uint64_t>(field(metadata,"version")->value) == 7);
				*field(metadata,"version") = Value(uint64_t{99}); // 模拟v3读取器不认识v4。
			}
			UINK_CHECK(state,dots == 4 && diagonals == 10); // 纯Fill的椭圆不导出辅助点，但保留aux位。
			const auto standardRead = ImportDraw3UInkDocument(standard), oldReader = ImportDraw3UInkDocument(unknown);
			UINK_CHECK(state,standardRead.snapshot && standardRead.snapshot->canvases[0].strokes.size() == auxiliaryWireCount);
			UINK_CHECK(state,oldReader.snapshot && oldReader.snapshot->canvases[0].strokes.size() == auxiliaryWireCount && !oldReader.diagnostics.empty());
			if (oldReader.snapshot)
			{
				size_t restoredDots = 0, restoredDiagonals = 0;
				for (const auto& part : oldReader.snapshot->canvases[0].strokes)
				{
					if (part.shape && part.shape->kind == Kind::Circle && part.shape->style.fill && !part.shape->style.stroke) ++restoredDots;
					if (part.shape && part.shape->kind == Kind::Line && part.shape->style.linePattern == LinePattern::Dashed) ++restoredDiagonals;
				}
				UINK_CHECK(state,restoredDots == dots && restoredDiagonals == diagonals);
			}
			for (const char* name : {"version","revision","partCount","partIndex"})
			{
				auto malformed = *auxiliaryExport.document;
				auto& reference = std::get<UInkShape>(malformed.canvases[0].content[1]).extra->front().second;
				*field(reference,name) = Value(uint64_t{99});
				UINK_CHECK(state,ImportDraw3UInkDocument(malformed).status != Draw3UInkImportStatus::Success);
			}
			for (bool missingAux : {false,true})
			{
				auto malformed = *auxiliaryExport.document;
				auto& metadata = std::get<UInkShape>(malformed.canvases[0].content[0]).extra->front().second;
				if (missingAux)
				{
					auto& map = std::get<UInkMessagePackValue::Map>(metadata.value);
					map.erase(std::remove_if(map.begin(),map.end(),[](const auto& pair) { return std::get<std::string>(pair.first.value) == "aux"; }),map.end());
				}
				else *field(metadata,"aux") = Value(uint64_t{8});
				UINK_CHECK(state,ImportDraw3UInkDocument(malformed).status != Draw3UInkImportStatus::Success);
			}
			auto missing = *auxiliaryExport.document; missing.canvases[0].content.pop_back();
			UINK_CHECK(state,ImportDraw3UInkDocument(missing).status != Draw3UInkImportStatus::Success);
		}
		// 开发期私有格式统一走标准降级，不再恢复其构造语义。
		for (uint64_t version = 1; version <= 6; ++version)
		{
			auto retired = EncodeInkeysShapeMetadata(*freeTriangle);
			*field(retired,"version") = Value(version);
			UINK_CHECK(state,DecodeInkeysShapeMetadata(retired).status == Draw3ShapeMetadataStatus::UnsupportedVersion);
			if (advancedExport.document)
			{
				auto retiredDocument = *advancedExport.document;
				for (auto& content : retiredDocument.canvases[0].content)
					*field(std::get<UInkShape>(content).extra->front().second,"version") = Value(version);
				const auto fallback = ImportDraw3UInkDocument(retiredDocument);
				UINK_CHECK(state,fallback.snapshot && !fallback.diagnostics.empty() &&
					fallback.snapshot->canvases[0].strokes.size() == retiredDocument.canvases[0].content.size());
			}

		}

		// v7 构造参数与等腰顶角必须完整往返，不借标准部件重猜编辑约束。
		for (auto preset : {PresetKind::Cuboid,PresetKind::Cube,PresetKind::Cylinder,PresetKind::Cone,PresetKind::Prism,PresetKind::Pyramid})
		{
			ToolSpec tool{preset,CreationMode::MultiStage}; tool.sides=7;
			auto construction=MakeSolidConstruction(tool,{80,80},{180,130},-150);
			auto& solid=std::get<SolidConstruction>(construction.parameters);
			solid.baseRotation=.3f;solid.mirrored=true;
			if(preset==PresetKind::Prism || preset==PresetKind::Pyramid)solid.offset={20,15};
			const auto object=MakeConstructedShape(construction,{},Guid("70000000-0000-4000-8000-00000000f044").Bytes(),3);
			UINK_CHECK(state,object);
			if(object)
			{
				auto wire=EncodeInkeysShapeMetadata(*object);
				UINK_CHECK(state,std::get<uint64_t>(field(wire,"version")->value)==7);
				auto decoded=DecodeInkeysShapeMetadata(wire);
				UINK_CHECK(state,decoded.shape==object);
			}
		}
		std::cout << "Constructed shape UInk cases: " << canvas.strokes.size() << " objects / " << wireCount << " standard parts" << std::endl;
	}
