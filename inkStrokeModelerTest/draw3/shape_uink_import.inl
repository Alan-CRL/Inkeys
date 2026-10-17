namespace
{
	const UInkMessagePackValue* ShapeMetadata(const std::optional<UInkExtra>& extra, bool& valid)
	{
		const UInkMessagePackValue* result = nullptr;
		if (!extra) return nullptr;
		for (const auto& [key, value] : *extra)
		{
			const auto* name = std::get_if<std::string>(&key.value);
			if (!name || *name != "inkeysShape" || result) { valid = false; return nullptr; }
			result = &value;
		}
		return result;
	}
	bool ShapeGroupNumber(const UInkMessagePackValue& metadata, const char* field, uint32_t& result)
	{
		const auto* map = std::get_if<UInkMessagePackValue::Map>(&metadata.value);
		if (!map) return false;
		bool found = false;
		for (const auto& [key, value] : *map)
		{
			const auto* name = std::get_if<std::string>(&key.value);
			if (!name || *name != field) continue;
			if (found) return false;
			found = true;
			if (const auto* n = std::get_if<uint64_t>(&value.value); n && *n <= UINT32_MAX) result = static_cast<uint32_t>(*n);
			else if (const auto* n = std::get_if<int64_t>(&value.value); n && *n >= 0 && *n <= UINT32_MAX) result = static_cast<uint32_t>(*n);
			else return false;
		}
		return found;
	}
	bool ShapeGroupReference(const UInkMessagePackValue& metadata, const shapes::ShapeObject& root,
		uint32_t partIndex, uint32_t partCount)
	{
		const auto* map = std::get_if<UInkMessagePackValue::Map>(&metadata.value);
		if (!map || map->size() != 6) return false;
		std::set<std::string> keys;
		bool reference = false;
		std::optional<UInkGuid> id;
		uint64_t revision = 0, version = 0;
		for (const auto& [key, value] : *map)
		{
			const auto* name = std::get_if<std::string>(&key.value);
			if (!name || !keys.insert(*name).second) return false;
			if (*name == "reference")
			{
				const auto* flag = std::get_if<bool>(&value.value);
				if (!flag || !*flag) return false;
				reference = true;
			}
			else if (*name == "id")
			{
				const auto* text = std::get_if<std::string>(&value.value);
				if (!text || !(id = ParseUInkGuid(*text))) return false;
			}
			else if (*name == "revision" || *name == "version")
			{
				uint64_t number = 0;
				if (const auto* n = std::get_if<uint64_t>(&value.value)) number = *n;
				else if (const auto* n = std::get_if<int64_t>(&value.value); n && *n >= 0) number = static_cast<uint64_t>(*n);
				else return false;
				(*name == "revision" ? revision : version) = number;
			}
			else if (*name != "partIndex" && *name != "partCount") return false;
		}
		uint32_t index = 0, count = 0;
		return reference && id && id->Bytes() == root.id && version == (root.construction ? 2u : 1u) && revision == root.revision &&
			ShapeGroupNumber(metadata, "partIndex", index) && index == partIndex &&
			ShapeGroupNumber(metadata, "partCount", count) && count == partCount;
	}
	bool ShapeStandardStyle(const UInkShape& source, shapes::ShapeStyle& style)
	{
		style.stroke = source.stroke.has_value();
		style.fill = source.fill.has_value();
		if (!style.stroke && !style.fill) return false;
		if (source.stroke)
		{
			const auto& stroke = *source.stroke;
			if (stroke.color.extended || stroke.color.fallbackRgb > 0xffffff ||
				stroke.declaredStartMarker != 0 || stroke.declaredEndMarker != 0 ||
				stroke.effectiveStartMarker != 0 || stroke.effectiveEndMarker != 0) return false;
			style.strokeRgb = stroke.color.fallbackRgb;
			style.strokeOpacity = stroke.opacity;
			style.width = stroke.width;
			style.dashOffset = stroke.dashOffset;
			const float w = stroke.width;
			if (stroke.dashArray.empty()) style.linePattern = shapes::LinePattern::Solid;
			else if (stroke.dashArray == std::vector<float>{ 4 * w, 6 * w }) style.linePattern = shapes::LinePattern::Dashed;
			else if (stroke.dashArray == std::vector<float>{ 0, 3 * w }) style.linePattern = shapes::LinePattern::Dotted;
			else if (stroke.dashArray == std::vector<float>{ 4 * w, 5 * w, 0, 5 * w }) style.linePattern = shapes::LinePattern::DashDot;
			else return false;
		}
		if (source.fill)
		{
			if (source.fill->color.extended || source.fill->color.fallbackRgb > 0xffffff || source.fill->declaredFillType != 0) return false;
			style.fillRgb = source.fill->color.fallbackRgb;
			style.fillOpacity = source.fill->opacity;
		}
		return true;
	}
	void ShapeRotation(shapes::Transform& transform, float radians, float x, float y)
	{
		transform.m11 = std::cos(radians); transform.m12 = std::sin(radians);
		transform.m21 = -transform.m12; transform.m22 = transform.m11;
		transform.dx = x; transform.dy = y;
	}
	std::optional<shapes::ShapeObject> ShapeFromStandard(const UInkShape& source)
	{
		shapes::ShapeObject object;
		const auto id = CreateUInkGuid();
		if (!id || !ShapeStandardStyle(source, object.style)) return std::nullopt;
		object.id = id->Bytes();
		if (const auto* line = std::get_if<UInkLineGeometry>(&source.geometry))
		{
			if (source.declaredShapeType == 0 && line->points.size() == 2)
			{
				object.kind = shapes::Kind::Line;
				object.geometry.start = { line->points[0].x, line->points[0].y };
				object.geometry.end = { line->points[1].x, line->points[1].y };
			}
			else if (source.declaredShapeType == 1 || source.declaredShapeType == 6)
			{
				object.kind = source.declaredShapeType == 1 ? shapes::Kind::Polyline : shapes::Kind::Polygon;
				object.creationMode = shapes::CreationMode::Points;
				for (auto point : line->points) object.geometry.vertices.push_back({ point.x, point.y });
			}
			else return std::nullopt;
		}
		else if (const auto* rectangle = std::get_if<UInkRectangleGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 2) return std::nullopt;
			object.kind = shapes::Kind::Rectangle;
			// 不等圆角用局部单位矩形及非等比变换精确表达。
			const float sx = rectangle->cornerRadiusX > 0 ? rectangle->cornerRadiusX : 1;
			const float sy = rectangle->cornerRadiusX > 0 ? rectangle->cornerRadiusY : 1;
			if (sy <= 0) return std::nullopt;
			object.geometry.start = { -rectangle->width * 0.5f / sx, -rectangle->height * 0.5f / sy };
			object.geometry.end = { -object.geometry.start.x, -object.geometry.start.y };
			object.geometry.radius = rectangle->cornerRadiusX > 0 ? 1.0f : 0.0f;
			ShapeRotation(object.transform, rectangle->rotation, rectangle->centerX, rectangle->centerY);
			object.transform.m11 *= sx; object.transform.m12 *= sx;
			object.transform.m21 *= sy; object.transform.m22 *= sy;
			// 标准宽度已是画布宽度，避免再次应用几何缩放。
			object.style.width /= std::sqrt(sx * sy);
		}
		else if (const auto* square = std::get_if<UInkSquareGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 3) return std::nullopt;
			object.kind = shapes::Kind::Rectangle;
			object.geometry.start = { -square->size * 0.5f, -square->size * 0.5f };
			object.geometry.end = { square->size * 0.5f, square->size * 0.5f };
			ShapeRotation(object.transform, square->rotation, square->centerX, square->centerY);
		}
		else if (const auto* ellipse = std::get_if<UInkEllipseGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 4) return std::nullopt;
			object.kind = shapes::Kind::Ellipse;
			object.geometry.end = { ellipse->width * 0.5f, ellipse->height * 0.5f };
			ShapeRotation(object.transform, ellipse->rotation, ellipse->centerX, ellipse->centerY);
		}
		else if (const auto* circle = std::get_if<UInkCircleGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 5) return std::nullopt;
			object.kind = shapes::Kind::Circle;
			object.geometry.start = { circle->centerX, circle->centerY };
			object.geometry.radius = circle->radius;
			object.creationMode = shapes::CreationMode::CenterRadius;
		}
		else return std::nullopt;
		if (!shapes::Validate(object)) return std::nullopt;
		return object;
	}
	std::optional<Draw3UInkStrokeSnapshot> SnapshotFromShape(shapes::ShapeObject object, uint32_t undoId)
	{
		shapes::GeometryResult geometry;
		if (!shapes::BuildGeometry(object, geometry)) return std::nullopt;
		Draw3UInkStrokeSnapshot result;
		result.undoId = undoId;
		result.style.kind = Draw3UInkStrokeKind::Shape;
		result.style.opacity = object.style.stroke ? object.style.strokeOpacity : object.style.fillOpacity;
		result.style.fallbackRgb = object.style.stroke ? object.style.strokeRgb : object.style.fillRgb;
		result.points = { { geometry.bounds.left, geometry.bounds.top, object.style.width },
			{ geometry.bounds.right, geometry.bounds.bottom, object.style.width } };
		if (object.legacyKind != shapes::LegacyKind::None)
		{
			switch (object.legacyKind)
			{
			case shapes::LegacyKind::SolidLine: result.style.kind = Draw3UInkStrokeKind::SolidLine; break;
			case shapes::LegacyKind::DashedLine: result.style.kind = Draw3UInkStrokeKind::DashedLine; break;
			case shapes::LegacyKind::OutlineRectangle: result.style.kind = Draw3UInkStrokeKind::OutlineRectangle; break;
			case shapes::LegacyKind::FilledRectangle: result.style.kind = Draw3UInkStrokeKind::FilledRectangle; break;
			default: return std::nullopt;
			}
			result.points = { { object.geometry.start.x, object.geometry.start.y, object.style.width },
				{ object.geometry.end.x, object.geometry.end.y, object.style.width } };
		}
		result.shape = std::move(object);
		return result;
	}
}
