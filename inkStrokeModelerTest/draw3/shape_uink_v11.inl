namespace
{
	using ShapeMap = UInkMessagePackValue::Map;
	using ShapeArray = UInkMessagePackValue::Array;

	template<class T>
	UInkMessagePackValue ShapeValue(T value)
	{
		return UInkMessagePackValue{ std::move(value) };
	}

	void ShapeField(ShapeMap& map, const char* name, UInkMessagePackValue value)
	{
		map.emplace_back(ShapeValue(std::string(name)), std::move(value));
	}
}

#include "shape_uink_standard.inl"

namespace
{
	using shapes::Construction;
	using shapes::CreationMode;
	using shapes::Kind;
	using shapes::LegacyKind;
	using shapes::LinePattern;
	using shapes::PlanarConstraint;
	using shapes::PresetKind;
	using shapes::ShapeObject;
	using shapes::ShapeStyle;
	using shapes::Transform;
	using shapes::Vec2;

	constexpr std::array<std::string_view, 21> kV11PresetNames = {
		"line", "arrow", "rectangle", "rounded-rectangle", "circle", "ellipse",
		"triangle", "parallelogram", "rhombus", "trapezoid", "regular-polygon",
		"polyline", "polygon", "arc", "sector", "cuboid", "cube", "cylinder",
		"cone", "prism", "pyramid"
	};
	static_assert(kV11PresetNames.size() == static_cast<size_t>(PresetKind::Pyramid) + 1);
	constexpr std::array<std::string_view, 4> kV11LegacyNames = {
		"legacy-solid-line", "legacy-dashed-line", "legacy-outline-rectangle", "legacy-filled-rectangle"
	};
	static_assert(kV11LegacyNames.size() == static_cast<size_t>(LegacyKind::FilledRectangle));

	bool V11PointPreset(PresetKind preset)
	{
		return preset == PresetKind::Polyline || preset == PresetKind::Polygon;
	}

	bool V11VertexEditPreset(PresetKind preset)
	{
		return preset == PresetKind::Triangle || preset == PresetKind::Parallelogram ||
			preset == PresetKind::Rhombus || preset == PresetKind::Trapezoid;
	}

	bool V11SidesPreset(PresetKind preset)
	{
		return preset == PresetKind::RegularPolygon || preset == PresetKind::Prism || preset == PresetKind::Pyramid;
	}

	bool V11RoundSolid(PresetKind preset)
	{
		return preset == PresetKind::Cylinder || preset == PresetKind::Cone;
	}

	bool V11BoxSolid(PresetKind preset)
	{
		return preset == PresetKind::Cuboid || preset == PresetKind::Cube;
	}

	UInkMessagePackValue V11PointValue(Vec2 point)
	{
		ShapeMap fields;
		ShapeField(fields, "x", ShapeValue(point.x));
		ShapeField(fields, "y", ShapeValue(point.y));
		return ShapeValue(std::move(fields));
	}

	ShapeMap V11ConstructionValue(const Construction& construction)
	{
		const auto& tool = construction.tool;
		const auto preset = tool.preset;
		ShapeMap fields;
		// PRESET-4：只写当前预设使用的局部参数。点模式与立体均不需要 end。
		if (!V11PointPreset(preset))
			ShapeField(fields, "start", V11PointValue(construction.start));
		if (!V11PointPreset(preset) && preset < PresetKind::Cuboid)
			ShapeField(fields, "end", V11PointValue(construction.end));
		if (V11SidesPreset(preset))
			ShapeField(fields, "sides", ShapeValue(uint64_t(tool.sides)));
		if (preset == PresetKind::Circle)
			ShapeField(fields, "mode", ShapeValue(int64_t(tool.creationMode)));
		if (preset == PresetKind::Rectangle)
			ShapeField(fields, "cornerRadius", ShapeValue(tool.rectangleCornerRadius));
		if (preset == PresetKind::Triangle || preset == PresetKind::Trapezoid)
			ShapeField(fields, "constraint", ShapeValue(int64_t(tool.constraint)));
		if (preset == PresetKind::Triangle)
			ShapeField(fields, "constraintVertex", ShapeValue(uint64_t(tool.constraintVertex)));
		if (const auto* vertices = std::get_if<shapes::VertexConstruction>(&construction.parameters))
		{
			ShapeArray values;
			values.reserve(vertices->vertices.size());
			for (const auto point : vertices->vertices) values.push_back(V11PointValue(point));
			ShapeField(fields, "vertices", ShapeValue(std::move(values)));
		}
		else if (const auto* arc = std::get_if<shapes::ArcConstruction>(&construction.parameters))
			ShapeField(fields, "sweep", ShapeValue(arc->sweepRadians));
		else if (const auto* solid = std::get_if<shapes::SolidConstruction>(&construction.parameters))
		{
			ShapeField(fields, "height", ShapeValue(solid->height));
			if (V11BoxSolid(preset))
				ShapeField(fields, "baseSize", V11PointValue(solid->baseSize));
			else
				ShapeField(fields, "radius", ShapeValue(solid->baseSize.x));
			ShapeField(fields, "baseRotation", ShapeValue(solid->baseRotation));
			if (!V11RoundSolid(preset))
				ShapeField(fields, "mirrored", ShapeValue(solid->mirrored));
			ShapeField(fields, "offset", V11PointValue(solid->offset));
			if (preset == PresetKind::Prism || preset == PresetKind::Pyramid)
				ShapeField(fields, "right", ShapeValue(tool.rightSolid));
		}
		return fields;
	}

	UInkMessagePackValue V11ObjectColorValue(uint32_t rgb)
	{
		ShapeMap color;
		ShapeField(color, "fallback", ShapeValue(uint64_t(rgb & 0xFFFFFFu)));
		return ShapeValue(std::move(color));
	}

	ShapeMap V11StyleValue(const ShapeStyle& style)
	{
		ShapeMap fields;
		ShapeField(fields, "stroke", ShapeValue(style.stroke));
		ShapeField(fields, "fill", ShapeValue(style.fill));
		ShapeField(fields, "strokeColor", V11ObjectColorValue(style.strokeRgb));
		ShapeField(fields, "fillColor", V11ObjectColorValue(style.fillRgb));
		ShapeField(fields, "strokeOpacity", ShapeValue(style.strokeOpacity));
		ShapeField(fields, "fillOpacity", ShapeValue(style.fillOpacity));
		ShapeField(fields, "width", ShapeValue(style.width));
		ShapeField(fields, "linePattern", ShapeValue(int64_t(style.linePattern)));
		ShapeField(fields, "dashOffset", ShapeValue(style.dashOffset));
		return fields;
	}

	bool V11ShapeGeometry(const ShapeObject& object, const shapes::BuildOptions& options,
		shapes::GeometryResult& geometry, std::string* error = nullptr)
	{
		if (!shapes::BuildGeometry(object, geometry, options, error)) return false;
		shapes::GeometryResult auxiliary;
		if (!shapes::BuildAuxiliaryGeometry(object, auxiliary, options, error)) return false;
		for (auto& part : auxiliary.parts)
			geometry.parts.push_back(std::move(part));
		return true;
	}

	class V11ShapeFields
	{
	public:
		explicit V11ShapeFields(const ShapeMap& fields) : fields_(fields) {}

		const UInkMessagePackValue* Find(std::string_view key)
		{
			const UInkMessagePackValue* found = nullptr;
			for (const auto& [name, value] : fields_)
			{
				const auto* text = std::get_if<std::string>(&name.value);
				if (!text || *text != key) continue;
				if (found) valid = false;
				found = &value;
			}
			return found;
		}

		bool valid = true;

	private:
		const ShapeMap& fields_;
	};

	bool V11Read(const UInkMessagePackValue& wire, float& value)
	{
		const auto* number = std::get_if<float>(&wire.value);
		if (!number || !std::isfinite(*number)) return false;
		value = *number;
		return true;
	}

	bool V11Read(const UInkMessagePackValue& wire, bool& value)
	{
		const auto* flag = std::get_if<bool>(&wire.value);
		if (!flag) return false;
		value = *flag;
		return true;
	}

	template<class T>
	bool V11ReadInteger(const UInkMessagePackValue& wire, T& value)
	{
		if (const auto* number = std::get_if<uint64_t>(&wire.value); number && std::in_range<T>(*number))
			value = static_cast<T>(*number);
		else if (const auto* number = std::get_if<int64_t>(&wire.value); number && std::in_range<T>(*number))
			value = static_cast<T>(*number);
		else return false;
		return true;
	}

	bool V11Read(const UInkMessagePackValue& wire, int32_t& value)
	{
		return V11ReadInteger(wire, value);
	}

	bool V11Read(const UInkMessagePackValue& wire, uint32_t& value)
	{
		return V11ReadInteger(wire, value);
	}

	bool V11Read(const UInkMessagePackValue& wire, Vec2& value)
	{
		const auto* map = std::get_if<ShapeMap>(&wire.value);
		if (!map) return false;
		V11ShapeFields fields(*map);
		const auto* x = fields.Find("x");
		const auto* y = fields.Find("y");
		return fields.valid && x && y && V11Read(*x, value.x) && V11Read(*y, value.y);
	}

	template<class T>
	bool V11ReadField(V11ShapeFields& fields, std::string_view name, T& value, bool required = false)
	{
		const auto* field = fields.Find(name);
		return field ? V11Read(*field, value) : !required;
	}

	bool V11ReadConstruction(const ShapeMap& map, Construction& construction, bool& geometryLimit)
	{
		auto& tool = construction.tool;
		const auto preset = tool.preset;
		V11ShapeFields fields(map);
		tool.creationMode = shapes::DefaultCreationMode(preset);
		if (!V11PointPreset(preset) && !V11ReadField(fields, "start", construction.start, true)) return false;
		if (!V11PointPreset(preset) && preset < PresetKind::Cuboid &&
			!V11ReadField(fields, "end", construction.end, true)) return false;
		if (V11SidesPreset(preset) && (!V11ReadField(fields, "sides", tool.sides, true) ||
			tool.sides < shapes::kMinRegularSides || tool.sides >
				(preset == PresetKind::RegularPolygon ? shapes::kMaxRegularSides : shapes::kMaxSolidSides))) return false;
		if (preset == PresetKind::Circle)
		{
			int32_t mode = 1;
			// 平面预设注册表将 mode 列为 circle 的必需参数。
			if (!V11ReadField(fields, "mode", mode, true) || mode < 0 || mode > 1) return false;
			tool.creationMode = static_cast<CreationMode>(mode);
		}
		if (preset == PresetKind::Rectangle)
		{
			tool.rectangleCornerRadius = 0;
			if (!V11ReadField(fields, "cornerRadius", tool.rectangleCornerRadius) ||
				tool.rectangleCornerRadius < 0) return false;
		}
		if (preset == PresetKind::Triangle || preset == PresetKind::Trapezoid)
		{
			int32_t constraint = 0;
			if (!V11ReadField(fields, "constraint", constraint) || constraint < 0 ||
				constraint > (preset == PresetKind::Triangle ? 2 : 1)) return false;
			tool.constraint = static_cast<PlanarConstraint>(constraint);
			if (tool.constraint == PlanarConstraint::Equilateral)
				tool.creationMode = CreationMode::CenterRadius;
		}
		if (preset == PresetKind::Triangle &&
			(!V11ReadField(fields, "constraintVertex", tool.constraintVertex) || tool.constraintVertex > 2)) return false;
		if (V11PointPreset(preset) || V11VertexEditPreset(preset))
		{
			const auto* wire = fields.Find("vertices");
			if (!wire && V11PointPreset(preset)) return false;
			if (wire)
			{
				const auto* values = std::get_if<ShapeArray>(&wire->value);
				if (!values) return false;
				if (values->size() > kUInkMaxShapeVertices)
				{
					geometryLimit = true;
					return false;
				}
				const size_t minimum = preset == PresetKind::Polyline ? 2 : 3;
				const size_t planarCount = preset == PresetKind::Triangle ? 3 : 4;
				if (values->size() < minimum ||
					(V11VertexEditPreset(preset) && values->size() != planarCount)) return false;
				shapes::VertexConstruction vertices;
				vertices.vertices.reserve(values->size());
				for (const auto& value : *values)
				{
					Vec2 vertex;
					if (!V11Read(value, vertex)) return false;
					vertices.vertices.push_back(vertex);
				}
				if (V11PointPreset(preset))
				{
					construction.start = vertices.vertices.front();
					construction.end = vertices.vertices.back();
				}
				construction.parameters = std::move(vertices);
			}
		}
		else if (preset == PresetKind::Arc || preset == PresetKind::Sector)
		{
			shapes::ArcConstruction arc;
			if (!V11ReadField(fields, "sweep", arc.sweepRadians, true)) return false;
			construction.parameters = arc;
		}
		else if (preset >= PresetKind::Cuboid)
		{
			shapes::SolidConstruction solid;
			if (!V11ReadField(fields, "height", solid.height, true) || solid.height == 0 ||
				!V11ReadField(fields, "baseRotation", solid.baseRotation) ||
				!V11ReadField(fields, "offset", solid.offset)) return false;
			if (V11BoxSolid(preset))
			{
				if (!V11ReadField(fields, "baseSize", solid.baseSize, true) ||
					solid.baseSize.x <= 0 || solid.baseSize.y <= 0) return false;
			}
			else
			{
				float radius = 0;
				if (!V11ReadField(fields, "radius", radius, true) || radius <= 0) return false;
				solid.baseSize = { radius, radius };
			}
			if (!V11RoundSolid(preset) && !V11ReadField(fields, "mirrored", solid.mirrored)) return false;
			if ((preset == PresetKind::Prism || preset == PresetKind::Pyramid) &&
				!V11ReadField(fields, "right", tool.rightSolid)) return false;
			construction.parameters = solid;
		}
		return fields.valid;
	}

	float V11Opacity(float opacity, Draw3UInkV11FallbackCounts& counts)
	{
		if (opacity < 0 || opacity > 1) ++counts.opacityClamped; // L1 将 object.style 保留为通用 Map。
		return std::clamp(opacity, 0.0f, 1.0f);
	}

	uint32_t V11ShapeColor(const UInkColor& color, Draw3UInkV11FallbackCounts& counts)
	{
		if (color.extended) ++counts.extendedColor; // COLOR-2
		return color.fallbackRgb & 0xFFFFFFu;
	}

	bool V11ReadObjectColor(V11ShapeFields& fields, const char* key, uint32_t& rgb,
		Draw3UInkV11FallbackCounts& counts)
	{
		const auto* field = fields.Find(key);
		if (!field) return true;
		const auto* map = std::get_if<ShapeMap>(&field->value);
		if (!map) return false;
		V11ShapeFields color(*map);
		if (!V11ReadField(color, "fallback", rgb, true)) return false;
		rgb &= 0xFFFFFFu;
		const auto* space = color.Find("space");
		const auto* components = color.Find("components");
		if (space || components) ++counts.extendedColor;
		return color.valid;
	}

	bool V11ReadObjectStyle(const UInkShapeObject& root, ShapeStyle& style,
		Draw3UInkV11FallbackCounts& counts)
	{
		style.auxiliary = static_cast<shapes::Auxiliary>(root.aux.value_or(0) & 7);
		if (!root.style) return style.stroke || style.fill;
		V11ShapeFields fields(*root.style);
		if (!V11ReadField(fields, "stroke", style.stroke) ||
			!V11ReadField(fields, "fill", style.fill)) return false;
		// 辅助标记也可能有填充，不能把它们误作本体的未启用颜色。
		if (!style.stroke) style.strokeRgb = 0;
		if (!style.fill) style.fillRgb = 0;
		int32_t linePattern = 0;
		if (!V11ReadObjectColor(fields, "strokeColor", style.strokeRgb, counts) ||
			!V11ReadObjectColor(fields, "fillColor", style.fillRgb, counts) ||
			!V11ReadField(fields, "strokeOpacity", style.strokeOpacity) ||
			!V11ReadField(fields, "fillOpacity", style.fillOpacity) ||
			!V11ReadField(fields, "width", style.width) || style.width <= 0 ||
			!V11ReadField(fields, "linePattern", linePattern) ||
			!V11ReadField(fields, "dashOffset", style.dashOffset) || !fields.valid) return false;
		if (linePattern < 0 || linePattern > static_cast<int32_t>(LinePattern::DashDot))
		{
			++counts.dashArray;
			linePattern = 0;
		}
		style.linePattern = static_cast<LinePattern>(linePattern);
		style.strokeOpacity = V11Opacity(style.strokeOpacity, counts);
		style.fillOpacity = V11Opacity(style.fillOpacity, counts);
		return style.stroke || style.fill;
	}

	std::optional<LinePattern> V11DashPattern(const UInkShapeStroke& stroke)
	{
		if (stroke.dashArray.empty()) return LinePattern::Solid;
		ShapeStyle style;
		style.width = stroke.width;
		for (const LinePattern pattern : { LinePattern::Dashed, LinePattern::Dotted, LinePattern::DashDot })
		{
			style.linePattern = pattern;
			const auto expected = ShapeDashArray(style);
			if (stroke.dashArray.size() % expected.size() != 0) continue;
			bool matches = true;
			for (size_t i = 0; i < stroke.dashArray.size(); ++i)
			{
				const double actual = stroke.dashArray[i];
				const double wanted = expected[i % expected.size()];
				if (std::abs(actual - wanted) > 1e-5 * std::abs(wanted) || !std::isfinite(actual))
				{
					matches = false;
					break;
				}
			}
			if (matches) return pattern;
		}
		return std::nullopt;
	}

	ShapeStyle V11PartStyle(const UInkShape& source, Draw3UInkV11FallbackCounts& counts,
		bool* unmappedDash = nullptr)
	{
		ShapeStyle style;
		style.stroke = source.stroke.has_value();
		style.fill = source.fill.has_value() && source.declaredShapeType != 0 && source.declaredShapeType != 1;
		if (source.renderOnlyWhenLatest) ++counts.latestVisibility;
		if (source.declaredShapeType == 3) ++counts.square;
		if (source.stroke)
		{
			const auto& stroke = *source.stroke;
			style.strokeRgb = V11ShapeColor(stroke.color, counts);
			style.strokeOpacity = std::clamp(stroke.opacity, 0.0f, 1.0f);
			style.width = stroke.width;
			style.dashOffset = stroke.dashOffset;
			const auto pattern = V11DashPattern(stroke);
			style.linePattern = pattern.value_or(LinePattern::Solid);
			if (unmappedDash) *unmappedDash = !pattern;
			if (!pattern) ++counts.dashArray;
			const bool openShape = source.declaredShapeType == 0 || source.declaredShapeType == 1;
			if (openShape && (stroke.declaredStartMarker == 1 || stroke.declaredEndMarker == 1)) ++counts.marker;
			if (EffectiveUInkLineCap(stroke) != 0) ++counts.lineCap;
			if (EffectiveUInkLineJoin(stroke) != 0) ++counts.lineJoin;
		}
		if (style.fill)
		{
			style.fillRgb = V11ShapeColor(source.fill->color, counts);
			style.fillOpacity = std::clamp(source.fill->opacity, 0.0f, 1.0f);
		}
		return style;
	}

	Transform V11Rotation(float angle, float x, float y)
	{
		const float cosine = std::cos(angle), sine = std::sin(angle);
		return { cosine, sine, -sine, cosine, x, y };
	}

	// shape_core.cpp 的私有 kMaxVertices 同时限制 BuiltinBuilder 的每段弧和显式顶点。
	constexpr size_t kV11CoreGeometryLimit = 16'384;
	// shape_presets.inl 的 ArcPoints / ObliqueBase 在派生显式顶点时采用更紧的上限。
	constexpr size_t kV11PresetArcLimit = 16'000;

	struct V11GeometryWork
	{
		uint64_t remaining = uint64_t{ 1 } << 26;

		bool Charge(uint64_t cost)
		{
			if (cost > remaining) return false;
			remaining -= cost;
			return true;
		}
	};

	uint64_t V11PartWork(Kind kind, bool fill, uint64_t vertices, unsigned validations)
	{
		const uint64_t squared = vertices * vertices;
		const uint64_t validation = kind == Kind::Polygon ? validations * squared : 0;
		// 耳切可能反复尝试多个耳，使用立方上界。无填充的曲线和折线只做线性采样。
		const uint64_t tessellation = fill ? squared * vertices : 0;
		return validation + tessellation;
	}
	constexpr double kV11Pi = std::numbers::pi_v<double>;

	bool V11Coordinate(double value)
	{
		return std::isfinite(value) && std::abs(value) <= kUInkMaxCoordinate;
	}

	bool V11Coordinate(Vec2 value)
	{
		return V11Coordinate(value.x) && V11Coordinate(value.y);
	}

	float V11MaximumScale(const Transform& transform)
	{
		// 与 PRESET-6 和 Draw3 的曲线派生器使用相同的最大奇异值。
		const double a = double(transform.m11) * transform.m11 + double(transform.m12) * transform.m12;
		const double b = double(transform.m21) * transform.m21 + double(transform.m22) * transform.m22;
		const double c = double(transform.m11) * transform.m21 + double(transform.m12) * transform.m22;
		return static_cast<float>(std::sqrt((a + b + std::hypot(a - b, 2 * c)) * 0.5));
	}

	double V11ArcSegments(double radius, double sweep, double tolerance, bool builtin)
	{
		const double angle = builtin && radius <= tolerance ? kV11Pi * .5 :
			2 * std::acos(std::clamp(1.0 - tolerance / radius, -1.0, 1.0));
		return std::ceil(std::abs(sweep) / (std::max)(angle, 1e-8));
	}

	bool V11ConstructionBudget(const Construction& construction, const Transform& transform,
		const shapes::BuildOptions& options, V11GeometryWork& work, size_t& sampledPolygonVertices)
	{
		if (!V11Coordinate(construction.start) || !V11Coordinate(construction.end)) return false;
		if (const auto* vertices = std::get_if<shapes::VertexConstruction>(&construction.parameters))
		{
			const auto count = vertices->vertices.size();
			return count <= kV11CoreGeometryLimit &&
				std::all_of(vertices->vertices.begin(), vertices->vertices.end(),
					[](Vec2 point) { return V11Coordinate(point); }) &&
				(construction.tool.preset == PresetKind::Polyline || work.Charge(uint64_t{ count } * count));
		}
		const auto preset = construction.tool.preset;
		if (V11VertexEditPreset(preset) && !work.Charge(preset == PresetKind::Triangle ? 9 : 16)) return false;
		if (preset == PresetKind::RegularPolygon &&
			!work.Charge(uint64_t{ construction.tool.sides } * construction.tool.sides)) return false;

		// 在造型工厂和 BuildGeometry 分配采样点前检查。普通曲线部件另在部件预算中检查。
		const float tolerance = options.maxErrorPixels / (std::max)(1.0f, V11MaximumScale(transform));
		if (const auto* arc = std::get_if<shapes::ArcConstruction>(&construction.parameters))
		{
			const double radius = static_cast<float>(std::hypot(double(construction.end.x) - construction.start.x,
				double(construction.end.y) - construction.start.y));
			if (radius == 0) return true; // 退化参数交给构造函数报告。
			const double segments = V11ArcSegments(radius, arc->sweepRadians, tolerance, false);
			if (!std::isfinite(segments) || segments > kV11PresetArcLimit) return false;
			if (construction.tool.preset == PresetKind::Sector)
			{
				const auto originalSegments = static_cast<uint64_t>((std::max)(2.0,
					V11ArcSegments(radius, arc->sweepRadians, shapes::BuildOptions{}.maxErrorPixels, false)));
				const auto originalVertices = originalSegments + 2;
				if (!work.Charge(originalVertices * originalVertices)) return false;
				sampledPolygonVertices = static_cast<size_t>((std::max)(2.0, segments)) + 2;
			}
			return true;
		}
		if (const auto* solid = std::get_if<shapes::SolidConstruction>(&construction.parameters))
		{
			if (!V11Coordinate(solid->height) || !V11Coordinate(solid->baseSize) ||
				!V11Coordinate(solid->offset)) return false;
			uint64_t parts = 3 * (V11BoxSolid(preset) ? 4 : construction.tool.sides);
			if (V11RoundSolid(preset))
			{
				// ArcPoints / ObliqueBase 限制局部细分。圆底可能添加两个切点；每条边最多
				// 派生一条侧边及上下各一弧段，为工厂内 Validate 的部件 ID 检查估算上界。
				const auto segmentCount = [&](float error) {
					return (std::max)(32.0, V11ArcSegments(solid->baseSize.x, 2 * kV11Pi, error, false));
				};
				if (segmentCount(tolerance) > kV11PresetArcLimit) return false;
				const double nominal = segmentCount(shapes::BuildOptions{}.maxErrorPixels);
				if (!std::isfinite(nominal) || nominal > kV11PresetArcLimit) return false;
				const auto vertices = static_cast<uint64_t>(std::ceil(nominal / 4)) * 4 + 2;
				parts = 3 * vertices;
			}
			if (!work.Charge(parts * (parts - 1) / 2)) return false;
		}
		return true;
	}

	bool V11PartBudget(Kind kind, const shapes::Geometry& g, const Transform& transform,
		const shapes::BuildOptions& options, size_t& remaining)
	{
		const auto fits = [&](Vec2 point)
		{
			return V11Coordinate(point) && V11Coordinate(shapes::ApplyTransform(transform, point));
		};
		size_t vertices = 0;
		switch (kind)
		{
		case Kind::Line:
			if (!fits(g.start) || !fits(g.end)) return false;
			vertices = 2;
			break;
		case Kind::Polyline:
		case Kind::Polygon:
			if (g.vertices.size() > remaining || g.vertices.size() > kV11CoreGeometryLimit ||
				!std::all_of(g.vertices.begin(), g.vertices.end(), fits)) return false;
			vertices = g.vertices.size() + (kind == Kind::Polygon ? 1 : 0);
			break;
		case Kind::Rectangle:
		case Kind::Circle:
		case Kind::Ellipse:
		{
			if (!V11Coordinate(g.start) || !V11Coordinate(g.end) || !V11Coordinate(g.radius)) return false;
			const float scale = options.displayScale * V11MaximumScale(transform);
			const double tolerance = options.maxErrorPixels / scale;
			double segments = 0;
			if (kind == Kind::Rectangle)
			{
				const float radius = (std::min)({ g.radius, std::abs(g.end.x - g.start.x) * .5f,
					std::abs(g.end.y - g.start.y) * .5f });
				if (!fits(g.start) || !fits(g.end) ||
					!fits({ g.start.x, g.end.y }) || !fits({ g.end.x, g.start.y })) return false;
				segments = radius > 0 ? V11ArcSegments(radius, kV11Pi * .5, tolerance, true) : 0;
			}
			else
			{
				const float rx = kind == Kind::Circle ? g.radius : g.end.x;
				const float ry = kind == Kind::Circle ? g.radius : g.end.y;
				const auto center = shapes::ApplyTransform(transform, g.start);
				const double xExtent = std::hypot(double(transform.m11) * rx, double(transform.m21) * ry);
				const double yExtent = std::hypot(double(transform.m12) * rx, double(transform.m22) * ry);
				if (!V11Coordinate(std::abs(center.x) + xExtent) ||
					!V11Coordinate(std::abs(center.y) + yExtent)) return false;
				segments = V11ArcSegments((std::max)(rx, ry), 2 * kV11Pi, tolerance, true);
			}
			if (!std::isfinite(segments) || segments > kV11CoreGeometryLimit) return false;
			vertices = kind == Kind::Rectangle ? 4 * (static_cast<size_t>(segments) + 1) + 1 :
				static_cast<size_t>(segments) + 1;
			break;
		}
		default:
			return false;
		}
		if (vertices > remaining) return false;
		remaining -= vertices;
		return true;
	}

	bool V11ObjectBudget(const ShapeObject& object, const shapes::BuildOptions& options,
		V11GeometryWork& work, size_t sampledPolygonVertices = 0)
	{
		size_t remaining = kUInkMaxShapeVertices;
		const auto preset = object.construction ? object.construction->tool.preset : PresetKind::Line;
		const auto auxiliary = object.style.auxiliary & shapes::SupportedAuxiliary(preset);
		const unsigned validations = object.style.auxiliary == shapes::Auxiliary::None ? 2 : 3;
		uint64_t cost = 0;
		const auto checkPart = [&](Kind kind, const shapes::Geometry& g, const Transform& transform,
			const ShapeStyle& style)
		{
			const size_t before = remaining;
			if (!V11PartBudget(kind, g, transform, options, remaining)) return false;
			const auto vertices = before - remaining;
			cost += V11PartWork(kind, style.fill, vertices, validations);
			return true;
		};
		if (object.style.stroke && (auxiliary &
			(shapes::Auxiliary::Center | shapes::Auxiliary::Foci)) != shapes::Auxiliary::None)
		{
			// 辅助圆点以画布为局部空间；BuildGeometry 和写入器分别生成一次。
			const auto& t = object.transform;
			const double determinant = double(t.m11) * t.m22 - double(t.m12) * t.m21;
			shapes::Geometry dot;
			dot.radius = (std::max)(2.0f,
				object.style.width * static_cast<float>(std::sqrt(std::abs(determinant))));
			auto dotStyle = object.style;
			dotStyle.fill = true;
			const size_t repeats = (auxiliary & shapes::Auxiliary::Foci) != shapes::Auxiliary::None ? 2 : 1;
			for (size_t i = 0; i < repeats; ++i)
			{
				const auto before = cost;
				if (!checkPart(Kind::Circle, dot, {}, dotStyle)) return false;
				cost += cost - before;
			}
		}
		if (object.kind != Kind::Compound)
		{
			if (!checkPart(object.kind, object.geometry, object.transform, object.style)) return false;
			if (sampledPolygonVertices && object.style.fill)
				cost += V11PartWork(Kind::Polygon, true, sampledPolygonVertices, 0);
		}
		else
		{
			const auto parts = object.parts.size();
			if (parts > kV11CoreGeometryLimit) return false;
			// Validate 逐个线性搜索前面的部件 ID。
			cost += validations * uint64_t{ parts } * parts;
			for (const auto& part : object.parts)
				if (!checkPart(part.kind, part.geometry, shapes::ComposeTransforms(object.transform, part.transform),
					part.style.value_or(object.style))) return false;
		}
		return work.Charge(cost);
	}

	std::optional<ShapeObject> V11StandardPart(const UInkShape& source, const ShapeStyle& style,
		const shapes::BuildOptions& options, Draw3UInkV11FallbackCounts& counts,
		size_t& remaining, V11GeometryWork& work)
	{
		ShapeObject object;
		object.style = style;
		if (const auto* line = std::get_if<UInkLineGeometry>(&source.geometry))
		{
			if (source.declaredShapeType == 0 && line->points.size() == 2)
			{
				object.kind = Kind::Line;
				object.geometry.start = { line->points[0].x, line->points[0].y };
				object.geometry.end = { line->points[1].x, line->points[1].y };
			}
			else if (source.declaredShapeType == 1 || source.declaredShapeType == 6)
			{
				if (line->points.size() > remaining)
				{
					++counts.geometryLimit;
					return std::nullopt;
				}
				object.kind = source.declaredShapeType == 1 ? Kind::Polyline : Kind::Polygon;
				object.creationMode = CreationMode::Points;
				object.geometry.vertices.reserve(line->points.size());
				for (const auto point : line->points)
					object.geometry.vertices.push_back({ point.x, point.y });
			}
			else return std::nullopt;
		}
		else if (const auto* rectangle = std::get_if<UInkRectangleGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 2 || rectangle->width <= 0 || rectangle->height <= 0 ||
				rectangle->cornerRadiusX < 0 || rectangle->cornerRadiusY < 0) return std::nullopt;
			object.kind = Kind::Rectangle;
			object.geometry.start = { -rectangle->width * .5f, -rectangle->height * .5f };
			object.geometry.end = { -object.geometry.start.x, -object.geometry.start.y };
			object.transform = V11Rotation(rectangle->rotation, rectangle->centerX, rectangle->centerY);
			if (rectangle->cornerRadiusX > 0 && rectangle->cornerRadiusY > 0)
			{
				object.geometry.radius = rectangle->cornerRadiusX;
				if (rectangle->cornerRadiusX != rectangle->cornerRadiusY)
				{
					// 保留两个轴各自的圆角；线宽是标准部件的画布宽度。
					const float sy = rectangle->cornerRadiusY / rectangle->cornerRadiusX;
					object.geometry.start.y /= sy;
					object.geometry.end.y /= sy;
					object.transform.m21 *= sy;
					object.transform.m22 *= sy;
					object.style.width /= std::sqrt(sy);
				}
			}
		}
		else if (const auto* square = std::get_if<UInkSquareGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 3 || square->size <= 0) return std::nullopt;
			object.kind = Kind::Rectangle;
			object.geometry.start = { -square->size * .5f, -square->size * .5f };
			object.geometry.end = { square->size * .5f, square->size * .5f };
			object.transform = V11Rotation(square->rotation, square->centerX, square->centerY);
		}
		else if (const auto* ellipse = std::get_if<UInkEllipseGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 4 || ellipse->width <= 0 || ellipse->height <= 0) return std::nullopt;
			object.kind = Kind::Ellipse;
			object.geometry.end = { ellipse->width * .5f, ellipse->height * .5f };
			object.transform = V11Rotation(ellipse->rotation, ellipse->centerX, ellipse->centerY);
		}
		else if (const auto* circle = std::get_if<UInkCircleGeometry>(&source.geometry))
		{
			if (source.declaredShapeType != 5 || circle->radius <= 0) return std::nullopt;
			object.kind = Kind::Circle;
			object.geometry.start = { circle->centerX, circle->centerY };
			object.geometry.radius = circle->radius;
			object.creationMode = CreationMode::CenterRadius;
		}
		else return std::nullopt;

		size_t remainingAfterPart = remaining;
		if (!V11PartBudget(object.kind, object.geometry, object.transform, options, remainingAfterPart) ||
			!work.Charge(V11PartWork(object.kind, style.fill, remaining - remainingAfterPart, 2)))
		{
			++counts.geometryLimit;
			return std::nullopt;
		}
		// 部件只是临时转换结果。独立图形的 UUID 在调用处生成，复合部件没有独立 UUID。
		object.id[0] = 1;
		if (!shapes::Validate(object)) return std::nullopt;
		remaining = remainingAfterPart;
		return object;
	}

	bool V11AppendSolidOutline(const UInkShape& source, const ShapeStyle& style,
		std::vector<shapes::ShapePart>& parts, size_t& remaining)
	{
		if (source.declaredShapeType != 6 || !style.stroke || style.fill ||
			!source.stroke || !source.stroke->dashArray.empty()) return false;
		const auto* polygon = std::get_if<UInkLineGeometry>(&source.geometry);
		if (!polygon || polygon->points.size() < 3 || polygon->points.size() >= remaining) return false;
		const size_t closedCount = polygon->points.size() + 1;
		const size_t additionalEndpoints = (closedCount - 2) / (kV11CoreGeometryLimit - 1);
		if (closedCount + additionalEndpoints > remaining ||
			!std::ranges::all_of(polygon->points, [](const UInkShapePoint& point)
				{ return V11Coordinate(Vec2{ point.x, point.y }); })) return false;

		// 只描边的闭合实线可直接沿文件点连线，绕开 SimplePolygon 与耳切。
		remaining -= closedCount + additionalEndpoints;
		for (size_t begin = 0; begin + 1 < closedCount;)
		{
			const size_t end = (std::min)(closedCount, begin + kV11CoreGeometryLimit);
			shapes::ShapePart part;
			part.kind = Kind::Polyline;
			part.style = style;
			part.geometry.vertices.reserve(end - begin);
			for (size_t i = begin; i < end; ++i)
			{
				const auto point = i == polygon->points.size() ? polygon->points.front() : polygon->points[i];
				part.geometry.vertices.push_back({ point.x, point.y });
			}
			parts.push_back(std::move(part));
			begin = end - 1;
		}
		return true;
	}

	// Draw3 部件没有任意 dashArray 字段；在缺省对象样式的回退中把可见虚线段转成实线部件。
	bool V11AppendDashParts(const UInkShapeStroke& stroke, const ShapeObject& source,
		const shapes::BuildOptions& options, std::vector<shapes::ShapePart>& parts,
		size_t& remaining)
	{
		shapes::GeometryPart outline;
		auto local = options;
		local.displayScale *= V11MaximumScale(source.transform);
		const auto* builder = shapes::DefaultRegistry().Find(source.kind);
		std::string error;
		if (!builder || !(*builder)(source.geometry, local, outline, error) || outline.paths.empty()) return false;
		auto& points = outline.paths.front().points;
		if (points.size() < 2 || stroke.dashArray.size() % 2 != 0) return false;
		std::vector<double> distances;
		distances.reserve(points.size());
		for (auto& point : points)
		{
			point = shapes::ApplyTransform(source.transform, point);
			if (distances.empty()) distances.push_back(0);
			else
			{
				const auto previous = points[distances.size() - 1];
				distances.push_back(distances.back() + std::hypot(double(point.x) - previous.x,
					double(point.y) - previous.y));
			}
		}
		double period = 0;
		for (const auto amount : stroke.dashArray)
		{
			if (!std::isfinite(amount) || amount < 0) return false;
			period += amount;
		}
		if (!(period > 0) || !std::isfinite(stroke.dashOffset)) return false;
		// SHAPE-26：(s + offset) mod period，限制实际迭代以防极短虚线膨胀。
		double phase = std::fmod(double(stroke.dashOffset), period);
		if (phase < 0) phase += period;
		const double length = distances.back();
		if ((1 + std::floor((length + phase) / period)) * double(stroke.dashArray.size()) >
			kV11CoreGeometryLimit) return false;
		const auto pointAt = [&](double distance)
		{
			const auto next = std::upper_bound(distances.begin(), distances.end(), distance);
			if (next == distances.end()) return points.back();
			const auto index = static_cast<size_t>(next - distances.begin());
			const double fraction = (distance - distances[index - 1]) / (*next - distances[index - 1]);
			const auto a = points[index - 1], b = points[index];
			return Vec2{ static_cast<float>(a.x + fraction * (double(b.x) - a.x)),
				static_cast<float>(a.y + fraction * (double(b.y) - a.y)) };
		};
		auto style = source.style;
		style.width = stroke.width;
		style.fill = false;
		style.dashOffset = 0;
		for (double cycle = -phase; cycle <= length; cycle += period)
		{
			double distance = cycle;
			for (size_t i = 0; i < stroke.dashArray.size() && distance <= length; ++i)
			{
				const double next = distance + stroke.dashArray[i];
				if (i % 2 == 0 && next >= 0)
				{
					const double begin = (std::max)(0.0, distance), end = (std::min)(next, length);
					const auto first = std::upper_bound(distances.begin(), distances.end(), begin);
					const auto last = std::lower_bound(first, distances.end(), end);
					const auto count = static_cast<size_t>(last - first) + 2;
					if (count > remaining || count > kV11CoreGeometryLimit) return false;
					remaining -= count;
					shapes::ShapePart part;
					part.kind = Kind::Polyline;
					part.style = style;
					part.geometry.vertices.push_back(pointAt(begin));
					for (auto at = first; at != last; ++at)
						part.geometry.vertices.push_back(points[static_cast<size_t>(at - distances.begin())]);
					part.geometry.vertices.push_back(pointAt(end));
					parts.push_back(std::move(part));
				}
				distance = next;
			}
		}
		return true;
	}

	struct V11ShapeReadResult
	{
		std::optional<ShapeObject> shape;
		size_t consumed = 1;
		shapes::Bounds bounds;
	};

	class V11ShapeReader
	{
	public:
		V11ShapeReader(const UInkCanvas& canvas, const shapes::BuildOptions& options,
			Draw3UInkV11FallbackCounts& counts, V11GeometryWork& geometryWork,
			const std::set<shapes::ObjectId>* duplicateRoots = nullptr) :
			canvas_(canvas), options_(options), counts_(counts), geometryWork_(geometryWork),
			duplicateRoots_(duplicateRoots)
		{
			// SHAPE-14：第一次看到首部件时就必须知道是否有重复根或多余引用。
			for (const auto& content : canvas_.content)
			{
				const auto* shape = std::get_if<UInkShape>(&content);
				if (!shape || !shape->object) continue;
				if (const auto* root = std::get_if<UInkShapeObject>(&*shape->object))
					++groups_[root->id.Bytes()].roots;
				else
					++groups_[std::get<UInkShapeObjectRef>(*shape->object).ref.Bytes()].refs;
			}
		}

		V11ShapeReadResult Read(size_t index, size_t end)
		{
			const auto& first = std::get<UInkShape>(canvas_.content[index]);
			if (!first.object) return Independent(first);
			const auto* root = std::get_if<UInkShapeObject>(&*first.object);
			if (!root)
			{
				ReportIncomplete(std::get<UInkShapeObjectRef>(*first.object).ref);
				return Independent(first);
			}
			if (!Complete(*root, index, end))
			{
				ReportIncomplete(root->id);
				return Independent(first);
			}

			std::vector<ShapeStyle> partStyles;
			partStyles.reserve(root->partCount);
			bool unmappedDash = false;
			for (size_t i = 0; i < root->partCount; ++i)
			{
				const auto& part = std::get<UInkShape>(canvas_.content[index + i]);
				bool partDash = false;
				partStyles.push_back(V11PartStyle(part, counts_, &partDash));
				unmappedDash |= partDash;
			}

			ShapeStyle style = partStyles.front();
			const auto stroke = std::ranges::find_if(partStyles, &ShapeStyle::stroke);
			const auto fill = std::ranges::find_if(partStyles, &ShapeStyle::fill);
			if (root->style)
			{
				// 早期自产文件只在标准部件中写颜色。
				if (stroke != partStyles.end()) style.strokeRgb = stroke->strokeRgb;
				if (fill != partStyles.end()) style.fillRgb = fill->fillRgb;
			}
			else if (!style.stroke && style.fill && root->partCount >= 9)
			{
				// 标准闭合虚线的第一个部件是填充，紧接的八个轮廓部件携带描边。
				const auto* outline = std::get_if<UInkShape>(&canvas_.content[index + 1]);
				if (outline && outline->declaredShapeType == 1 && outline->stroke && !outline->stroke->dashArray.empty())
				{
					const auto body = style;
					style = partStyles[1];
					style.fill = true;
					style.fillRgb = body.fillRgb;
					style.fillOpacity = body.fillOpacity;
				}
			}

			auto objectStyle = style;
			bool validObjectStyle = V11ReadObjectStyle(*root, objectStyle, counts_);
			if (!root->style && objectStyle.stroke && root->transform)
			{
				const auto& t = *root->transform;
				const auto widthScale = std::sqrt(std::abs(double(t[0]) * t[3] - double(t[1]) * t[2]));
				const double width = widthScale > 0 ? style.width / widthScale : 0;
				if (width >= std::numeric_limits<float>::denorm_min() && width <= std::numeric_limits<float>::max())
					objectStyle.width = static_cast<float>(width);
				else validObjectStyle = false;
			}
			if (!root->preset) ++counts_.missingShapePreset;
			else
			{
				const auto preset = std::ranges::find(kV11PresetNames, std::string_view(*root->preset));
				const auto legacy = std::ranges::find(kV11LegacyNames, std::string_view(*root->preset));
				if (preset != kV11PresetNames.end())
				{
					if (!validObjectStyle) ++counts_.invalidShapeConstruction;
					else if (root->style || !unmappedDash)
					{
						auto rebuilt = Rebuild(*root,
							static_cast<PresetKind>(preset - kV11PresetNames.begin()), objectStyle);
						if (rebuilt.shape) return rebuilt;
					}
				}
				else if (legacy != kV11LegacyNames.end())
				{
					const auto kind = static_cast<LegacyKind>(legacy - kV11LegacyNames.begin() + 1);
					if (kind == LegacyKind::OutlineRectangle || kind == LegacyKind::FilledRectangle)
					{
						if (auto rectangle = LegacyRectangle(first, partStyles.front(),
							validObjectStyle ? objectStyle : style, *root, kind))
							return FinishShape(std::move(rectangle), root->partCount);
						++counts_.legacyRectangle;
					}
					else
					{
						if (auto line = LegacyLine(first, partStyles.front(), *root, kind))
						{
							if (validObjectStyle)
							{
								line->style.fillRgb = objectStyle.fillRgb;
								line->style.fillOpacity = objectStyle.fillOpacity;
							}
							return FinishShape(std::move(line), root->partCount);
						}
						++counts_.invalidShapeConstruction;
					}
				}
				else ++counts_.unknownShapePreset;
			}
			// PRESET-4/5：有完整对象层时，保留文件部件及其顺序以便整体编辑。
			return Compound(*root, index, partStyles, validObjectStyle ? objectStyle : style,
				!root->style && unmappedDash);
		}

	private:
		struct Group
		{
			size_t roots = 0;
			size_t refs = 0;
			bool reportedIncomplete = false;
		};

		void ReportIncomplete(const UInkGuid& id)
		{
			auto& group = groups_.at(id.Bytes());
			if (group.reportedIncomplete) return;
			group.reportedIncomplete = true;
			++counts_.incompleteShapeObject;
		}

		bool Complete(const UInkShapeObject& root, size_t index, size_t end) const
		{
			if (root.id.IsZero() || root.revision == 0 || root.partCount == 0 ||
				root.partCount > end - index) return false;
			const auto& group = groups_.at(root.id.Bytes());
			if (group.roots != 1 || group.refs != root.partCount - 1 ||
				(duplicateRoots_ && duplicateRoots_->contains(root.id.Bytes()))) return false;
			const auto& first = std::get<UInkShape>(canvas_.content[index]);
			for (uint32_t i = 1; i < root.partCount; ++i)
			{
				const auto* part = std::get_if<UInkShape>(&canvas_.content[index + i]);
				if (!part || part->undoId != first.undoId || !part->object) return false;
				const auto* ref = std::get_if<UInkShapeObjectRef>(&*part->object);
				if (!ref || ref->ref != root.id || ref->partIndex != i) return false;
			}
			return true;
		}

		V11ShapeReadResult Independent(const UInkShape& source)
		{
			size_t remaining = kUInkMaxShapeVertices;
			const auto material = V11PartStyle(source, counts_);
			const auto oldLimit = counts_.geometryLimit;
			auto object = V11StandardPart(source, material, options_, counts_, remaining, geometryWork_);
			if (!object && counts_.geometryLimit != oldLimit)
			{
				ShapeObject outline;
				outline.kind = Kind::Compound;
				outline.style = material;
				if (V11AppendSolidOutline(source, material, outline.parts, remaining) &&
					ChargeParts(outline.parts))
					object = std::move(outline);
			}
			if (!object) return {};
			const auto id = CreateUInkGuid();
			if (!id) return {};
			object->id = id->Bytes();
			return FinishShape(std::move(object), 1);
		}

		V11ShapeReadResult Rebuild(const UInkShapeObject& root, PresetKind preset, ShapeStyle style)
		{
			Construction construction;
			construction.tool.preset = preset;
			bool geometryLimit = false;
			if (!root.construction || !V11ReadConstruction(*root.construction, construction, geometryLimit))
			{
				if (geometryLimit) ++counts_.geometryLimit;
				else ++counts_.invalidShapeConstruction;
				return {};
			}

			Transform transform;
			if (root.transform)
			{
				const auto& t = *root.transform;
				transform = { t[0], t[1], t[2], t[3], t[4], t[5] };
			}
			size_t sampledPolygonVertices = 0;
			if (!V11Coordinate(transform.dx) || !V11Coordinate(transform.dy) ||
				!V11ConstructionBudget(construction, transform, options_, geometryWork_, sampledPolygonVertices))
			{
				++counts_.geometryLimit;
				return {};
			}
			auto rebuilt = shapes::MakeConstructedShape(construction, style, root.id.Bytes(), root.revision);
			if (!rebuilt)
			{
				++counts_.invalidShapeConstruction;
				return {};
			}
			rebuilt->transform = transform;
			if (!V11ObjectBudget(*rebuilt, options_, geometryWork_, sampledPolygonVertices))
			{
				++counts_.geometryLimit;
				return {};
			}
			shapes::GeometryResult geometry;
			if (!V11ShapeGeometry(*rebuilt, options_, geometry))
			{
				++counts_.invalidShapeConstruction;
				return {};
			}
			size_t count = 0;
			size_t vertices = 0;
			for (const auto& part : geometry.parts)
			{
				if (part.paths.empty()) continue;
				const auto& path = part.paths.front();
				vertices += path.points.size();
				if (vertices > kUInkMaxShapeVertices || !std::all_of(path.points.begin(), path.points.end(),
					[](Vec2 point) { return V11Coordinate(point); }))
				{
					++counts_.geometryLimit;
					return {};
				}
				// 与 StandardShapeParts 相同的 PRESET-23 分段条件，只计算数量。
				count += part.style.stroke && part.style.linePattern != LinePattern::Solid &&
					path.closed && path.lengths.back() > 0 ? (part.style.fill ? 9 : 8) : 1;
			}
			// S3 的 SHAPE-15 例外：校验构造和部件数后直接显示重建结果，避免逐点比对。
			if (count != root.partCount)
			{
				++counts_.shapePartCountMismatch;
				return {};
			}
			return { std::move(rebuilt), root.partCount, geometry.bounds };
		}

		std::optional<ShapeObject> LegacyLine(const UInkShape& part, const ShapeStyle& style,
			const UInkShapeObject& root, LegacyKind kind)
		{
			if ((kind != LegacyKind::SolidLine && kind != LegacyKind::DashedLine) ||
				root.partCount != 1 || part.declaredShapeType != 0 || !style.stroke || style.fill ||
				style.linePattern != (kind == LegacyKind::DashedLine ? LinePattern::Dashed : LinePattern::Solid))
				return std::nullopt;
			const auto* line = std::get_if<UInkLineGeometry>(&part.geometry);
			if (!line || line->points.size() != 2) return std::nullopt;
			const Vec2 start{ line->points[0].x, line->points[0].y };
			const Vec2 end{ line->points[1].x, line->points[1].y };
			if (!V11Coordinate(start) || !V11Coordinate(end))
			{
				++counts_.geometryLimit;
				return std::nullopt;
			}
			auto result = shapes::MakeLegacyShape(kind, start, end, style.width, style.strokeRgb,
				style.strokeOpacity, root.id.Bytes());
			if (!result) return std::nullopt;
			result->revision = root.revision;
			result->style.dashOffset = style.dashOffset;
			return result;
		}

		std::optional<ShapeObject> LegacyRectangle(const UInkShape& part, const ShapeStyle& style,
			const ShapeStyle& objectStyle, const UInkShapeObject& root, LegacyKind kind)
		{
			const bool filled = kind == LegacyKind::FilledRectangle;
			const auto* rectangle = std::get_if<UInkRectangleGeometry>(&part.geometry);
			if (root.partCount != 1 || part.declaredShapeType != 2 || !rectangle ||
				style.fill != filled || style.stroke == filled || style.linePattern != LinePattern::Solid ||
				rectangle->rotation != 0 || rectangle->width <= 0 || rectangle->height <= 0 ||
				rectangle->cornerRadiusX != rectangle->cornerRadiusY) return std::nullopt;

			// shape_core.cpp 的旧矩形按 4 DIP 与半短边取小值，比较采用本次导入比例。
			const float radius = (std::min)({ 4.0f * options_.displayScale,
				rectangle->width * .5f, rectangle->height * .5f });
			if (std::abs(double(rectangle->cornerRadiusX) - radius) > 1e-5 * radius) return std::nullopt;
			const Vec2 start{ rectangle->centerX - rectangle->width * .5f,
				rectangle->centerY - rectangle->height * .5f };
			const Vec2 end{ rectangle->centerX + rectangle->width * .5f,
				rectangle->centerY + rectangle->height * .5f };
			if (!V11Coordinate(start) || !V11Coordinate(end)) return std::nullopt;
			shapes::Geometry geometry;
			geometry.start = start;
			geometry.end = end;
			geometry.radius = radius;
			size_t remaining = kUInkMaxShapeVertices;
			if (!V11PartBudget(Kind::Rectangle, geometry, {}, options_, remaining) ||
				!geometryWork_.Charge(V11PartWork(Kind::Rectangle, filled, kUInkMaxShapeVertices - remaining, 0)))
			{
				++counts_.geometryLimit;
				return std::nullopt;
			}
			auto result = shapes::MakeLegacyShape(kind, start, end, filled ? objectStyle.width : style.width,
				filled ? style.fillRgb : style.strokeRgb, filled ? style.fillOpacity : style.strokeOpacity,
				root.id.Bytes());
			if (!result) return std::nullopt;
			result->revision = root.revision;
			result->style.dashOffset = style.dashOffset;
			if (filled)
			{
				result->style.strokeRgb = objectStyle.strokeRgb;
				result->style.strokeOpacity = objectStyle.strokeOpacity;
			}
			else
			{
				result->style.fillRgb = objectStyle.fillRgb;
				result->style.fillOpacity = objectStyle.fillOpacity;
			}
			return result;
		}

		V11ShapeReadResult FinishShape(std::optional<ShapeObject> object, size_t consumed)
		{
			shapes::GeometryResult geometry;
			if (!object || !shapes::BuildGeometry(*object, geometry, options_)) return { {}, consumed };
			return { std::move(object), consumed, geometry.bounds };
		}

		bool ChargeParts(std::vector<shapes::ShapePart>& parts)
		{
			const auto count = parts.size();
			if (count == 0 || count > kV11CoreGeometryLimit ||
				!geometryWork_.Charge(uint64_t{ count } * (count - 1) / 2)) return false;
			for (size_t i = 0; i < count; ++i) parts[i].id = static_cast<uint32_t>(i + 1);
			return true;
		}

		V11ShapeReadResult Compound(const UInkShapeObject& root, size_t index,
			const std::vector<ShapeStyle>& styles, ShapeStyle style, bool preserveDashes)
		{
			ShapeObject object;
			object.id = root.id.Bytes();
			object.revision = root.revision;
			object.kind = Kind::Compound;
			object.style = style;
			// SHAPE-12：标准部件已经是画布坐标，也已包括辅助部件。
			object.style.auxiliary = shapes::Auxiliary::None;
			size_t remaining = kUInkMaxShapeVertices;
			size_t skipped = 0;
			for (size_t i = 0; i < styles.size(); ++i)
			{
				const auto& source = std::get<UInkShape>(canvas_.content[index + i]);
				const auto oldLimit = counts_.geometryLimit;
				auto part = V11StandardPart(source, styles[i], options_, counts_, remaining, geometryWork_);
				if (!part)
				{
					if (counts_.geometryLimit == oldLimit ||
						!V11AppendSolidOutline(source, styles[i], object.parts, remaining)) ++skipped;
					continue;
				}
				if (preserveDashes && source.stroke && !V11DashPattern(*source.stroke))
				{
					std::vector<shapes::ShapePart> dashes;
					if (!V11AppendDashParts(*source.stroke, *part, options_, dashes, remaining))
					{
						++counts_.geometryLimit;
						++skipped;
						continue;
					}
					if (part->style.fill)
					{
						part->style.stroke = false;
						object.parts.push_back({ 0, part->kind, std::move(part->geometry), part->transform, part->style });
					}
					for (auto& dash : dashes) object.parts.push_back(std::move(dash));
				}
				else object.parts.push_back({ 0, part->kind, std::move(part->geometry), part->transform, part->style });
			}
			if (!object.parts.empty() && !ChargeParts(object.parts))
			{
				++counts_.geometryLimit;
				counts_.invalidShapeSkipped += styles.size() - 1;
				return { {}, root.partCount };
			}
			auto finished = FinishShape(std::move(object), root.partCount);
			// 整组失败时，页级调用方另计一个跳过。
			counts_.invalidShapeSkipped += finished.shape ? skipped : styles.size() - 1;
			return finished;
		}

		const UInkCanvas& canvas_;
		shapes::BuildOptions options_;
		Draw3UInkV11FallbackCounts& counts_;
		V11GeometryWork& geometryWork_;
		const std::set<shapes::ObjectId>* duplicateRoots_;
		std::map<shapes::ObjectId, Group> groups_;
	};

	bool V11WriteStandardParts(std::vector<UInkContent>& content, const ShapeObject& object,
		std::vector<UInkShape> standard, uint32_t undoId, bool renderOnlyWhenLatest, bool allowConstruction)
	{
		if (standard.empty() || standard.size() > UINT32_MAX) return false;

		UInkShapeObject root;
		root.id = UInkGuid(object.id);
		root.revision = object.revision;
		root.partCount = static_cast<uint32_t>(standard.size());
		const auto& transform = object.transform;
		root.transform = std::array{ transform.m11, transform.m12, transform.m21,
			transform.m22, transform.dx, transform.dy };
		root.style = V11StyleValue(object.style);
		root.aux = static_cast<uint32_t>(object.style.auxiliary);
		if (allowConstruction && object.construction)
		{
			root.preset.emplace(kV11PresetNames[static_cast<size_t>(object.construction->tool.preset)]);
			root.construction = V11ConstructionValue(*object.construction);
		}
		else if (allowConstruction && object.legacyKind != LegacyKind::None)
			root.preset.emplace(kV11LegacyNames[static_cast<size_t>(object.legacyKind) - 1]);
		else if (allowConstruction && object.kind != Kind::Compound && !object.construction && object.legacyKind == LegacyKind::None)
		{
			// 独立外来标准部件没有预设。仅多部件输出需以对象层保持整体。
			if (standard.size() == 1)
			{
				standard.front().contentId = static_cast<uint32_t>(content.size());
				standard.front().undoId = undoId;
				standard.front().renderOnlyWhenLatest = renderOnlyWhenLatest;
				if (standard.front().stroke) standard.front().stroke->lineCap = 0;
				content.emplace_back(std::move(standard.front()));
				return true;
			}
		}
		const auto objectId = root.id;
		for (uint32_t i = 0; i < standard.size(); ++i)
		{
			auto& part = standard[i];
			part.contentId = static_cast<uint32_t>(content.size());
			part.undoId = undoId;
			part.renderOnlyWhenLatest = renderOnlyWhenLatest;
			if (part.stroke) part.stroke->lineCap = 0; // PRESET-7：点线需圆端帽。
			if (i == 0) part.object = std::move(root);
			else part.object = UInkShapeObjectRef{ objectId, i };
			content.emplace_back(std::move(part));
		}
		return true;
	}

	// 标准部件也可能超过冷读预算。保留采样轮廓和已有三角化，避免再次验证大多边形。
	std::vector<UInkShape> V11BakeStandardParts(const shapes::GeometryResult& geometry)
	{
		std::vector<UInkShape> standard;
		for (const auto& part : geometry.parts)
		{
			const auto& path = part.paths.front();
			auto material = StandardShapePart(part);
			if (material.fill && !part.triangles.empty())
			{
				for (const auto& triangle : part.triangles)
				{
					UInkShape fill;
					fill.declaredShapeType = 6;
					fill.fill = material.fill;
					UInkLineGeometry polygon;
					for (const auto& point : triangle) polygon.points.push_back({ point.x, point.y });
					fill.geometry = std::move(polygon);
					standard.push_back(std::move(fill));
				}
			}
			else if (material.fill)
			{
				auto fill = material;
				fill.stroke.reset();
				standard.push_back(std::move(fill));
			}
			if (!material.stroke) continue;
			if (path.closed && part.style.linePattern != LinePattern::Solid)
			{
				auto outline = part;
				outline.style.fill = false;
				for (auto& segment : StandardShapeParts(outline)) standard.push_back(std::move(segment));
				continue;
			}
			if (!path.closed)
			{
				standard.push_back(std::move(material));
				continue;
			}

			// 实线闭合折线的重复起点保留末边，BuildGeometry 只进行线性描边处理。
			for (size_t begin = 0; begin + 1 < path.points.size();)
			{
				const size_t end = (std::min)(path.points.size(), begin + kV11CoreGeometryLimit);
				UInkShape stroke;
				stroke.declaredShapeType = 1;
				stroke.stroke = material.stroke;
				UInkLineGeometry outline;
				for (size_t i = begin; i < end; ++i)
					outline.points.push_back({ path.points[i].x, path.points[i].y });
				stroke.geometry = std::move(outline);
				standard.push_back(std::move(stroke));
				begin = end - 1;
			}
		}
		return standard;
	}

	bool AppendV11Shape(std::vector<UInkContent>& content, const ShapeObject& object,
		uint32_t undoId, bool renderOnlyWhenLatest, const shapes::BuildOptions& options,
		V11GeometryWork& geometryWork, Draw3UInkV11FallbackCounts& counts, std::string& error)
	{
		shapes::GeometryResult geometry;
		if (!V11ShapeGeometry(object, options, geometry, &error)) return false;
		std::vector<UInkShape> standard;
		for (const auto& part : geometry.parts)
		{
			if (part.paths.empty()) return false;
			for (auto& output : StandardShapeParts(part)) standard.push_back(std::move(output));
		}

		UInkCanvas candidate;
		if (!V11WriteStandardParts(candidate.content, object, std::move(standard),
			undoId, renderOnlyWhenLatest, true)) return false;
		V11GeometryWork checkedWork;
		Draw3UInkV11FallbackCounts checkedCounts;
		const auto check = [&]
		{
			checkedWork = geometryWork;
			checkedCounts = {};
			V11ShapeReader reader(candidate, options, checkedCounts, checkedWork);
			const auto restored = reader.Read(0, candidate.content.size());
			return restored.shape && restored.bounds.valid && restored.consumed == candidate.content.size() &&
				checkedCounts.invalidShapeSkipped == 0;
		};
		bool visible = check();
		if (checkedCounts.geometryLimit)
		{
			++counts.geometryLimit;
			auto& first = std::get<UInkShape>(candidate.content.front());
			if (first.object)
			{
				auto& root = std::get<UInkShapeObject>(*first.object);
				root.preset.reset();
				root.construction.reset();
				visible = check();
			}
			if (!visible)
			{
				// 保留低成本的标准部件表示；用相同的冷读入口确认没有跳过部件。
				candidate.content.clear();
				visible = V11WriteStandardParts(candidate.content, object, V11BakeStandardParts(geometry),
					undoId, renderOnlyWhenLatest, false) && check();
			}
		}
		if (!visible)
		{
			// 冷读预算内也无法显示的图形只跳过这一项并计数，不让整页导出失败：
			// 整页失败会使该页既无法驱逐、退出时也写不出，整页内容都有丢失风险。
			++counts.invalidShapeSkipped;
			return true;
		}
		if (content.size() > UINT32_MAX - candidate.content.size()) return false;
		geometryWork = checkedWork;
		for (auto& value : candidate.content)
		{
			std::get<UInkShape>(value).contentId = static_cast<uint32_t>(content.size());
			content.push_back(std::move(value));
		}
		return true;
	}

}
