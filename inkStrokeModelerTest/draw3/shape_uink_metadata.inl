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
	UInkMessagePackValue ShapeConstructionValue(const shapes::Construction& construction)
	{
		ShapeMap tool;
		ShapeField(tool, "preset", ShapeValue(uint64_t(construction.tool.preset)));
		ShapeField(tool, "creationMode", ShapeValue(uint64_t(construction.tool.creationMode)));
		ShapeField(tool, "sides", ShapeValue(uint64_t(construction.tool.sides)));
		ShapeField(tool, "rectangleCornerRadius", ShapeValue(construction.tool.rectangleCornerRadius));
		ShapeMap map;
		ShapeField(map, "tool", ShapeValue(std::move(tool)));
		ShapeField(map, "start", ShapePointValue(construction.start));
		ShapeField(map, "end", ShapePointValue(construction.end));
		return ShapeValue(std::move(map));
	}
	bool ShapeReadConstruction(const UInkMessagePackValue* value, shapes::Construction& construction)
	{
		const auto* map = value ? std::get_if<ShapeMap>(&value->value) : nullptr;
		if (!map || map->size() != 3 || !ShapeUniqueKeys(*map)) return false;
		const auto* toolValue = ShapeFind(*map, "tool");
		const auto* tool = toolValue ? std::get_if<ShapeMap>(&toolValue->value) : nullptr;
		uint64_t sides = 0;
		if (!tool || tool->size() != 4 || !ShapeUniqueKeys(*tool) ||
			!ShapeEnum(*tool, "preset", construction.tool.preset, uint64_t(shapes::PresetKind::RegularPolygon)) ||
			!ShapeEnum(*tool, "creationMode", construction.tool.creationMode, uint64_t(shapes::CreationMode::Parameters)) ||
			!ShapeUnsigned(ShapeFind(*tool, "sides"), sides) || sides < 3 || sides > 12 ||
			!ShapeFloat(ShapeFind(*tool, "rectangleCornerRadius"), construction.tool.rectangleCornerRadius) ||
			!ShapeReadPoint(ShapeFind(*map, "start"), construction.start) ||
			!ShapeReadPoint(ShapeFind(*map, "end"), construction.end)) return false;
		construction.tool.sides = static_cast<uint32_t>(sides);
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
	ShapeMap map;
	ShapeField(map, "version", ShapeValue(uint64_t{ shape.construction ? 2u : 1u }));
	ShapeField(map, "id", ShapeValue(FormatUInkGuid(UInkGuid(shape.id))));
	ShapeField(map, "revision", ShapeValue(shape.revision));
	ShapeField(map, "kind", ShapeValue(uint64_t(shape.kind)));
	ShapeField(map, "creationMode", ShapeValue(uint64_t(shape.creationMode)));
	ShapeField(map, "legacyKind", ShapeValue(uint64_t(shape.legacyKind)));
	ShapeField(map, "geometry", ShapeGeometryValue(shape.geometry));
	ShapeField(map, "transform", ShapeTransformValue(shape.transform));
	ShapeField(map, "style", ShapeStyleValue(shape.style));
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
	if (shape.construction) ShapeField(map, "construction", ShapeConstructionValue(*shape.construction));
	return ShapeValue(std::move(map));
}

Draw3ShapeMetadataResult DecodeInkeysShapeMetadata(const UInkMessagePackValue& value) noexcept
{
	try
	{
		const auto* map = std::get_if<ShapeMap>(&value.value);
		uint64_t version = 0;
		if (!map || !ShapeUniqueKeys(*map) || !ShapeUnsigned(ShapeFind(*map, "version"), version)) return {};
		if (version != 1 && version != 2) return { Draw3ShapeMetadataStatus::UnsupportedVersion, std::nullopt };
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
		if (version == 2)
		{
			shape.construction.emplace();
			if (!ShapeReadConstruction(ShapeFind(*map, "construction"), *shape.construction)) return {};
		}
		else if (ShapeFind(*map, "construction")) return {};
		if (!shapes::Validate(shape)) return {};
		return { Draw3ShapeMetadataStatus::Valid, std::move(shape) };
	}
	catch (...) { return {}; }
}
