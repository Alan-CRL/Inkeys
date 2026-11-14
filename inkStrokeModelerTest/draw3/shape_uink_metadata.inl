// 此文件在 draw3.uink_draw3_export 实现中引入，协议也供白板历史复用。
namespace
{
	template<class T> UInkMessagePackValue ShapeValue(T value)
	{
		UInkMessagePackValue result;
		result.value = std::move(value);
		return result;
	}
	using ShapeMap = UInkMessagePackValue::Map;
	using ShapeArray = UInkMessagePackValue::Array;
	uint64_t InkeysShapeVersion(const shapes::ShapeObject& shape)
	{
		shapes::GeometryResult geometry;
		if(shapes::BuildGeometry(shape,geometry))
			for(const auto& part:geometry.parts) if(part.style.stroke && part.style.linePattern!=shapes::LinePattern::Solid)
				for(const auto& path:part.paths) if(path.closed) return 6;
		if (shape.construction)
		{
			const auto& tool = shape.construction->tool;
			const auto* solid = std::get_if<shapes::SolidConstruction>(&shape.construction->parameters);
			if(tool.constraintVertex || tool.rightSolid || (solid && solid->oblique)) return 6;
			if (tool.constraint != shapes::PlanarConstraint::Free || tool.sides > 12 ||
				tool.preset == shapes::PresetKind::Prism || tool.preset == shapes::PresetKind::Pyramid) return 5;
		}
		// 零辅助位沿用原编码（包括无construction的v1），保证旧内容字节稳定。
		return shape.style.auxiliary != shapes::Auxiliary::None ? 4u : shape.construction ? 3u : 1u;
	}
	void ShapeField(ShapeMap& map, const char* name, UInkMessagePackValue value)
	{
		map.emplace_back(ShapeValue(std::string(name)), std::move(value));
	}
	const UInkMessagePackValue* ShapeFind(const ShapeMap& map, const char* name)
	{
		const UInkMessagePackValue* result = nullptr;
		for (const auto& [key, value] : map)
		{
			const auto* text = std::get_if<std::string>(&key.value);
			if (!text || *text != name) continue;
			if (result) return nullptr;
			result = &value;
		}
		return result;
	}
	bool ShapeUniqueKeys(const ShapeMap& map)
	{
		std::set<std::string> names;
		for (const auto& [key, value] : map)
		{
			const auto* text = std::get_if<std::string>(&key.value);
			if (!text || !names.insert(*text).second) return false;
		}
		return true;
	}
	bool ShapeUnsigned(const UInkMessagePackValue* value, uint64_t& output)
	{
		if (!value) return false;
		if (const auto* number = std::get_if<uint64_t>(&value->value)) { output = *number; return true; }
		if (const auto* number = std::get_if<int64_t>(&value->value); number && *number >= 0)
		{ output = static_cast<uint64_t>(*number); return true; }
		return false;
	}
	bool ShapeFloat(const UInkMessagePackValue* value, float& output)
	{
		if (!value) return false;
		double number = 0;
		if (const auto* v = std::get_if<float>(&value->value)) number = *v;
		else if (const auto* v = std::get_if<double>(&value->value)) number = *v;
		else if (const auto* v = std::get_if<int64_t>(&value->value)) number = static_cast<double>(*v);
		else if (const auto* v = std::get_if<uint64_t>(&value->value)) number = static_cast<double>(*v);
		else return false;
		if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) return false;
		output = static_cast<float>(number);
		return true;
	}
	bool ShapeBool(const UInkMessagePackValue* value, bool& output)
	{
		if (!value) return false;
		const auto* boolean = std::get_if<bool>(&value->value);
		if (!boolean) return false;
		output = *boolean;
		return true;
	}
	template<class E> bool ShapeEnum(const ShapeMap& map, const char* name, E& output, uint64_t maximum)
	{
		uint64_t number = 0;
		if (!ShapeUnsigned(ShapeFind(map, name), number) || number > maximum) return false;
		output = static_cast<E>(number);
		return true;
	}
	UInkMessagePackValue ShapePointValue(shapes::Vec2 point)
	{
		return ShapeValue(ShapeArray{ ShapeValue(point.x), ShapeValue(point.y) });
	}
	bool ShapeReadPoint(const UInkMessagePackValue* value, shapes::Vec2& point)
	{
		const auto* array = value ? std::get_if<ShapeArray>(&value->value) : nullptr;
		return array && array->size() == 2 && ShapeFloat(&(*array)[0], point.x) && ShapeFloat(&(*array)[1], point.y);
	}
	UInkMessagePackValue ShapeGeometryValue(const shapes::Geometry& geometry)
	{
		ShapeMap map;
		ShapeField(map, "start", ShapePointValue(geometry.start));
		ShapeField(map, "end", ShapePointValue(geometry.end));
		ShapeField(map, "radius", ShapeValue(geometry.radius));
		ShapeArray vertices;
		for (auto point : geometry.vertices) vertices.push_back(ShapePointValue(point));
		ShapeField(map, "vertices", ShapeValue(std::move(vertices)));
		return ShapeValue(std::move(map));
	}
	bool ShapeReadGeometry(const UInkMessagePackValue* value, shapes::Geometry& geometry)
	{
		const auto* map = value ? std::get_if<ShapeMap>(&value->value) : nullptr;
		if (!map || !ShapeUniqueKeys(*map) || !ShapeReadPoint(ShapeFind(*map, "start"), geometry.start) ||
			!ShapeReadPoint(ShapeFind(*map, "end"), geometry.end) || !ShapeFloat(ShapeFind(*map, "radius"), geometry.radius)) return false;
		const auto* verticesValue = ShapeFind(*map, "vertices");
		const auto* vertices = verticesValue ? std::get_if<ShapeArray>(&verticesValue->value) : nullptr;
		if (!vertices || vertices->size() > 1000000) return false;
		geometry.vertices.resize(vertices->size());
		for (size_t i = 0; i < vertices->size(); ++i)
			if (!ShapeReadPoint(&(*vertices)[i], geometry.vertices[i])) return false;
		return true;
	}
	UInkMessagePackValue ShapeConstructionValue(const shapes::Construction& construction, uint64_t version)
	{
		ShapeMap tool;
		ShapeField(tool, "preset", ShapeValue(uint64_t(construction.tool.preset)));
		ShapeField(tool, "creationMode", ShapeValue(uint64_t(construction.tool.creationMode)));
		ShapeField(tool, "sides", ShapeValue(uint64_t(construction.tool.sides)));
		ShapeField(tool, "rectangleCornerRadius", ShapeValue(construction.tool.rectangleCornerRadius));
		if (version >= 5) ShapeField(tool, "constraint", ShapeValue(uint64_t(construction.tool.constraint)));
		if(version>=6) {ShapeField(tool,"constraintVertex",ShapeValue(uint64_t(construction.tool.constraintVertex)));ShapeField(tool,"rightSolid",ShapeValue(construction.tool.rightSolid));}
		ShapeMap map;
		ShapeField(map, "tool", ShapeValue(std::move(tool)));
		ShapeField(map, "start", ShapePointValue(construction.start));
		ShapeField(map, "end", ShapePointValue(construction.end));
		ShapeArray parameters{ ShapeValue(uint64_t(construction.parameters.index())) };
		if (const auto* points = std::get_if<shapes::VertexConstruction>(&construction.parameters))
		{
			ShapeArray vertices;
			for (auto point : points->vertices) vertices.push_back(ShapePointValue(point));
			parameters.push_back(ShapeValue(std::move(vertices)));
		}
		else if (const auto* arc = std::get_if<shapes::ArcConstruction>(&construction.parameters))
			parameters.push_back(ShapeValue(arc->sweepRadians));
		else if (const auto* solid = std::get_if<shapes::SolidConstruction>(&construction.parameters))
		{
			ShapeArray fields{ ShapePointValue(solid->depth),ShapeValue(solid->height) };
			if (version >= 5) fields.push_back(ShapeValue(solid->baseProjection));
			if(version>=6) {fields.push_back(ShapeValue(solid->oblique));fields.push_back(ShapePointValue(solid->baseSize));fields.push_back(ShapeValue(solid->baseRotation));fields.push_back(ShapeValue(solid->projectionAngle));fields.push_back(ShapeValue(solid->projectionScale));fields.push_back(ShapePointValue(solid->offset));}
			parameters.push_back(ShapeValue(std::move(fields)));
		}
		ShapeField(map, "parameters", ShapeValue(std::move(parameters)));
		ShapeField(map, "legacyArrow", ShapeValue(construction.legacyArrow));
		return ShapeValue(std::move(map));
	}
	bool ShapeReadConstruction(const UInkMessagePackValue* value, shapes::Construction& construction, uint64_t version)
	{
		const auto* map = value ? std::get_if<ShapeMap>(&value->value) : nullptr;
		if (!map || map->size() != (version == 2 ? 3u : 5u) || !ShapeUniqueKeys(*map)) return false;
		const auto* toolValue = ShapeFind(*map, "tool");
		const auto* tool = toolValue ? std::get_if<ShapeMap>(&toolValue->value) : nullptr;
		uint64_t sides = 0;
		if (!tool || tool->size() != (version>=6 ? 7u : version >= 5 ? 5u : 4u) || !ShapeUniqueKeys(*tool) ||
			!ShapeEnum(*tool, "preset", construction.tool.preset, uint64_t(version == 2 ? shapes::PresetKind::RegularPolygon : shapes::PresetKind::Pyramid)) ||
			!ShapeEnum(*tool, "creationMode", construction.tool.creationMode, uint64_t(shapes::CreationMode::Parameters)) ||
			!ShapeUnsigned(ShapeFind(*tool, "sides"), sides) || sides < shapes::kMinRegularSides ||
			sides > (version >= 5 ? shapes::kMaxRegularSides : 12u) ||
			!ShapeFloat(ShapeFind(*tool, "rectangleCornerRadius"), construction.tool.rectangleCornerRadius) ||
			(version >= 5 && !ShapeEnum(*tool, "constraint", construction.tool.constraint, uint64_t(shapes::PlanarConstraint::Equilateral))) ||
			!ShapeReadPoint(ShapeFind(*map, "start"), construction.start) ||
			!ShapeReadPoint(ShapeFind(*map, "end"), construction.end)) return false;
		construction.tool.sides = static_cast<uint32_t>(sides);
		if(version>=6) {uint64_t apex=0;if(!ShapeUnsigned(ShapeFind(*tool,"constraintVertex"),apex)||apex>2||!ShapeBool(ShapeFind(*tool,"rightSolid"),construction.tool.rightSolid))return false;construction.tool.constraintVertex=static_cast<uint32_t>(apex);}
		if (version == 2)
		{
			construction.legacyArrow = construction.tool.preset == shapes::PresetKind::Arrow;
			return true;
		}
		if (!ShapeBool(ShapeFind(*map, "legacyArrow"), construction.legacyArrow)) return false;
		const auto* parameterValue = ShapeFind(*map, "parameters");
		const auto* parameters = parameterValue ? std::get_if<ShapeArray>(&parameterValue->value) : nullptr;
		uint64_t kind = 0;
		if (!parameters || parameters->empty() || !ShapeUnsigned(&parameters->front(), kind) || kind > 3 ||
			parameters->size() != (kind ? 2u : 1u)) return false;
		if (kind == 1)
		{
			const auto* vertices = std::get_if<ShapeArray>(&(*parameters)[1].value);
			if (!vertices || vertices->size() > 16384) return false;
			shapes::VertexConstruction points; points.vertices.resize(vertices->size());
			for (size_t i = 0; i < vertices->size(); ++i)
				if (!ShapeReadPoint(&(*vertices)[i], points.vertices[i])) return false;
			construction.parameters = std::move(points);
		}
		else if (kind == 2)
		{
			shapes::ArcConstruction arc;
			if (!ShapeFloat(&(*parameters)[1], arc.sweepRadians)) return false;
			construction.parameters = arc;
		}
		else if (kind == 3)
		{
			const auto* fields = std::get_if<ShapeArray>(&(*parameters)[1].value);
			shapes::SolidConstruction solid;
			if (!fields || fields->size() != (version>=6 ? 9u : version >= 5 ? 3u : 2u) || !ShapeReadPoint(&(*fields)[0], solid.depth) ||
				!ShapeFloat(&(*fields)[1], solid.height) || (version >= 5 && !ShapeFloat(&(*fields)[2], solid.baseProjection))) return false;
			if(version>=6 && (!ShapeBool(&(*fields)[3],solid.oblique)||!ShapeReadPoint(&(*fields)[4],solid.baseSize)||!ShapeFloat(&(*fields)[5],solid.baseRotation)||!ShapeFloat(&(*fields)[6],solid.projectionAngle)||!ShapeFloat(&(*fields)[7],solid.projectionScale)||!ShapeReadPoint(&(*fields)[8],solid.offset)))return false;
			construction.parameters = solid;
		}
		return true;
	}
	UInkMessagePackValue ShapeTransformValue(const shapes::Transform& transform)
	{
		return ShapeValue(ShapeArray{ ShapeValue(transform.m11), ShapeValue(transform.m12),
			ShapeValue(transform.m21), ShapeValue(transform.m22), ShapeValue(transform.dx), ShapeValue(transform.dy) });
	}
	bool ShapeReadTransform(const UInkMessagePackValue* value, shapes::Transform& transform)
	{
		const auto* array = value ? std::get_if<ShapeArray>(&value->value) : nullptr;
		return array && array->size() == 6 && ShapeFloat(&(*array)[0], transform.m11) &&
			ShapeFloat(&(*array)[1], transform.m12) && ShapeFloat(&(*array)[2], transform.m21) &&
			ShapeFloat(&(*array)[3], transform.m22) && ShapeFloat(&(*array)[4], transform.dx) && ShapeFloat(&(*array)[5], transform.dy);
	}
	UInkMessagePackValue ShapeStyleValue(const shapes::ShapeStyle& style)
	{
		ShapeMap map;
		ShapeField(map, "stroke", ShapeValue(style.stroke));
		ShapeField(map, "fill", ShapeValue(style.fill));
		ShapeField(map, "strokeRgb", ShapeValue(uint64_t(style.strokeRgb)));
		ShapeField(map, "fillRgb", ShapeValue(uint64_t(style.fillRgb)));
		ShapeField(map, "strokeOpacity", ShapeValue(style.strokeOpacity));
		ShapeField(map, "fillOpacity", ShapeValue(style.fillOpacity));
		ShapeField(map, "width", ShapeValue(style.width));
		ShapeField(map, "linePattern", ShapeValue(uint64_t(style.linePattern)));
		ShapeField(map, "dashOffset", ShapeValue(style.dashOffset));
		return ShapeValue(std::move(map));
	}
	bool ShapeReadStyle(const UInkMessagePackValue* value, shapes::ShapeStyle& style)
	{
		const auto* map = value ? std::get_if<ShapeMap>(&value->value) : nullptr;
		uint64_t strokeRgb = 0, fillRgb = 0;
		if (!map || !ShapeUniqueKeys(*map) || !ShapeBool(ShapeFind(*map, "stroke"), style.stroke) ||
			!ShapeBool(ShapeFind(*map, "fill"), style.fill) ||
			!ShapeUnsigned(ShapeFind(*map, "strokeRgb"), strokeRgb) || strokeRgb > 0xffffff ||
			!ShapeUnsigned(ShapeFind(*map, "fillRgb"), fillRgb) || fillRgb > 0xffffff ||
			!ShapeFloat(ShapeFind(*map, "strokeOpacity"), style.strokeOpacity) ||
			!ShapeFloat(ShapeFind(*map, "fillOpacity"), style.fillOpacity) ||
			!ShapeFloat(ShapeFind(*map, "width"), style.width) ||
			!ShapeEnum(*map, "linePattern", style.linePattern, uint64_t(shapes::LinePattern::DashDot)) ||
			!ShapeFloat(ShapeFind(*map, "dashOffset"), style.dashOffset)) return false;
		style.strokeRgb = static_cast<uint32_t>(strokeRgb);
		style.fillRgb = static_cast<uint32_t>(fillRgb);
		return true;
	}
}

UInkMessagePackValue EncodeInkeysShapeMetadata(const shapes::ShapeObject& shape)
{
	const uint64_t version = InkeysShapeVersion(shape);
	ShapeMap map;
	ShapeField(map, "version", ShapeValue(version));
	ShapeField(map, "id", ShapeValue(FormatUInkGuid(UInkGuid(shape.id))));
	ShapeField(map, "revision", ShapeValue(shape.revision));
	ShapeField(map, "kind", ShapeValue(uint64_t(shape.kind)));
	ShapeField(map, "creationMode", ShapeValue(uint64_t(shape.creationMode)));
	ShapeField(map, "legacyKind", ShapeValue(uint64_t(shape.legacyKind)));
	ShapeField(map, "geometry", ShapeGeometryValue(shape.geometry));
	ShapeField(map, "transform", ShapeTransformValue(shape.transform));
	ShapeField(map, "style", ShapeStyleValue(shape.style));
	if (version >= 4) ShapeField(map, "aux", ShapeValue(uint64_t(shape.style.auxiliary)));
	ShapeArray parts;
	for (const auto& part : shape.parts)
	{
		ShapeMap item;
		ShapeField(item, "id", ShapeValue(uint64_t(part.id)));
		ShapeField(item, "kind", ShapeValue(uint64_t(part.kind)));
		ShapeField(item, "geometry", ShapeGeometryValue(part.geometry));
		ShapeField(item, "transform", ShapeTransformValue(part.transform));
		if (part.style) ShapeField(item, "style", ShapeStyleValue(*part.style));
		parts.push_back(ShapeValue(std::move(item)));
	}
	ShapeField(map, "parts", ShapeValue(std::move(parts)));
	if (shape.construction) ShapeField(map, "construction", ShapeConstructionValue(*shape.construction,version));
	return ShapeValue(std::move(map));
}

Draw3ShapeMetadataResult DecodeInkeysShapeMetadata(const UInkMessagePackValue& value) noexcept
{
	try
	{
		const auto* map = std::get_if<ShapeMap>(&value.value);
		uint64_t version = 0;
		if (!map || !ShapeUniqueKeys(*map) || !ShapeUnsigned(ShapeFind(*map, "version"), version)) return {};
		if (version < 1 || version > 6) return { Draw3ShapeMetadataStatus::UnsupportedVersion, std::nullopt };
		shapes::ShapeObject shape;
		const auto* idValue = ShapeFind(*map, "id");
		const auto* idText = idValue ? std::get_if<std::string>(&idValue->value) : nullptr;
		const auto id = idText ? ParseUInkGuid(*idText) : std::nullopt;
		if (!id || id->IsZero() || !ShapeUnsigned(ShapeFind(*map, "revision"), shape.revision) ||
			!ShapeEnum(*map, "kind", shape.kind, uint64_t(shapes::Kind::Compound)) ||
			!ShapeEnum(*map, "creationMode", shape.creationMode, uint64_t(shapes::CreationMode::Parameters)) ||
			!ShapeEnum(*map, "legacyKind", shape.legacyKind, uint64_t(shapes::LegacyKind::FilledRectangle)) ||
			!ShapeReadGeometry(ShapeFind(*map, "geometry"), shape.geometry) ||
			!ShapeReadTransform(ShapeFind(*map, "transform"), shape.transform) ||
			!ShapeReadStyle(ShapeFind(*map, "style"), shape.style)) return {};
		shape.id = id->Bytes();
		if (version >= 4)
		{
			uint64_t auxiliary = 0;
			if (!ShapeUnsigned(ShapeFind(*map,"aux"),auxiliary) || auxiliary > 7) return {};
			shape.style.auxiliary = static_cast<shapes::Auxiliary>(auxiliary);
		}
		const auto* partsValue = ShapeFind(*map, "parts");
		const auto* parts = partsValue ? std::get_if<ShapeArray>(&partsValue->value) : nullptr;
		if (!parts || parts->size() > 1000000) return {};
		for (const auto& item : *parts)
		{
			const auto* partMap = std::get_if<ShapeMap>(&item.value);
			shapes::ShapePart part;
			uint64_t partId = 0;
			if (!partMap || !ShapeUniqueKeys(*partMap) ||
				!ShapeUnsigned(ShapeFind(*partMap, "id"), partId) || partId > UINT32_MAX ||
				!ShapeEnum(*partMap, "kind", part.kind, uint64_t(shapes::Kind::Polygon)) ||
				!ShapeReadGeometry(ShapeFind(*partMap, "geometry"), part.geometry) ||
				!ShapeReadTransform(ShapeFind(*partMap, "transform"), part.transform)) return {};
			part.id = static_cast<uint32_t>(partId);
			if (const auto* style = ShapeFind(*partMap, "style"))
			{
				part.style.emplace();
				if (!ShapeReadStyle(style, *part.style)) return {};
			}
			shape.parts.push_back(std::move(part));
		}
		if (version == 2 || version == 3 || (version >= 4 && ShapeFind(*map,"construction")))
		{
			shape.construction.emplace();
			if (!ShapeReadConstruction(ShapeFind(*map, "construction"), *shape.construction, version)) return {};
		}
		else if (ShapeFind(*map, "construction")) return {};
		if ((version == 3 || version == 4) && shape.construction &&
			(shape.construction->tool.preset == shapes::PresetKind::Prism || shape.construction->tool.preset == shapes::PresetKind::Pyramid))
		{
			auto normalized = shapes::NormalizeLegacyPolygonSolid(shape);
			if (!normalized) return {};
			shape = std::move(*normalized);
		}
		if (!shapes::Validate(shape)) return {};
		return { Draw3ShapeMetadataStatus::Valid, std::move(shape) };
	}
	catch (...) { return {}; }
}
