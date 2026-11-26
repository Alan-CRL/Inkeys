#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
	using namespace draw3::shapes;
	using namespace draw3::uink;

	struct ShapeV11Test
	{
		std::string_view name;
		int& failures;

		bool Check(bool condition, const char* expression, int line)
		{
			if (!condition)
			{
				++failures;
				std::cerr << "UInk Draw3 1.1 shape FAILED " << name << ':' << line
					<< ": " << expression << '\n';
			}
			return condition;
		}
	};

#define SHAPE_V11_CHECK(...) test.Check(!!(__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define SHAPE_V11_REQUIRE(...) do { if (!SHAPE_V11_CHECK(__VA_ARGS__)) return; } while (false)

	UInkGuid ShapeV11Guid(uint8_t serial)
	{
		return UInkGuid({ 0x70,0,0,0,0,0,0x40,0,0x80,0,0,0,0,0,0,serial });
	}

	Draw3UInkCanvasSnapshot ShapeV11Page()
	{
		Draw3UInkCanvasSnapshot page;
		page.pageGuid = ShapeV11Guid(0x01);
		page.pageNumber = 1;
		return page;
	}

	Draw3UInkStrokeSnapshot ShapeV11Stroke(const ShapeObject& object)
	{
		Draw3UInkStrokeSnapshot stroke;
		stroke.style.kind = Draw3UInkStrokeKind::Shape;
		stroke.undoId = 42;
		stroke.shape = object;
		return stroke;
	}

	std::optional<UInkCanvas> DecodeShapeV11Page(ShapeV11Test& test, const UInkCanvas& canvas)
	{
		const auto encoded = EncodeUInkPage(canvas, true, true);
		if (!SHAPE_V11_CHECK(encoded.status == UInkEncodeStatus::Success && !encoded.bytes.empty()))
			return std::nullopt;
		const UInkStreamContext context;
		auto decoded = DecodeUInkStream(encoded.bytes, {}, &context);
		if (!SHAPE_V11_CHECK(decoded.status == UInkReadStatus::Complete && decoded.document &&
			decoded.document->canvases.size() == 1))
			return std::nullopt;
		return std::move(decoded.document->canvases.front());
	}

	std::optional<UInkCanvas> ExportShapeV11Page(ShapeV11Test& test, const ShapeObject& object)
	{
		auto page = ShapeV11Page();
		page.strokes.push_back(ShapeV11Stroke(object));
		auto exported = ExportDraw3PageToUInkV11(page, {});
		if (!SHAPE_V11_CHECK(exported.status == Draw3UInkExportStatus::Success &&
			exported.canvas && !exported.canvas->content.empty()))
			return std::nullopt;
		return std::move(exported.canvas);
	}

	template<class Value>
	UInkMessagePackValue ShapeV11Value(Value value)
	{
		UInkMessagePackValue result;
		result.value = std::move(value);
		return result;
	}

	UInkMessagePackValue ShapeV11Point(Vec2 point)
	{
		return ShapeV11Value(UInkMessagePackValue::Map{
			{ ShapeV11Value(std::string("x")),ShapeV11Value(point.x) },
			{ ShapeV11Value(std::string("y")),ShapeV11Value(point.y) }
		});
	}

	const UInkMessagePackValue* ShapeV11Field(const UInkMessagePackValue::Map& map, std::string_view key)
	{
		for (const auto& [name, value] : map)
		{
			const auto* text = std::get_if<std::string>(&name.value);
			if (text && *text == key) return &value;
		}
		return nullptr;
	}

	bool ShapeV11SetField(UInkMessagePackValue::Map& map, std::string_view key, UInkMessagePackValue replacement)
	{
		for (auto& [name, value] : map)
		{
			const auto* text = std::get_if<std::string>(&name.value);
			if (text && *text == key)
			{
				value = std::move(replacement);
				return true;
			}
		}
		return false;
	}

	bool ShapeV11Near(float a, float b)
	{
		return std::abs(a - b) <= 0.0001f;
	}

	bool ShapeV11Near(Vec2 a, Vec2 b)
	{
		return ShapeV11Near(a.x, b.x) && ShapeV11Near(a.y, b.y);
	}

	bool ShapeV11Near(UInkShapePoint a, UInkShapePoint b)
	{
		return ShapeV11Near(a.x, b.x) && ShapeV11Near(a.y, b.y);
	}

	const UInkShapeObject* VerifyShapeV11Parts(ShapeV11Test& test,
		const UInkCanvas& canvas, std::optional<std::string_view> preset,
		const ShapeObject& object, size_t expectedCount)
	{
		if (!SHAPE_V11_CHECK(canvas.content.size() == expectedCount && expectedCount != 0))
			return nullptr;
		const auto* first = std::get_if<UInkShape>(&canvas.content.front());
		if (!SHAPE_V11_CHECK(first && first->object &&
			std::holds_alternative<UInkShapeObject>(*first->object)))
			return nullptr;
		const auto& root = std::get<UInkShapeObject>(*first->object);
		SHAPE_V11_CHECK(root.id.Bytes() == object.id);
		SHAPE_V11_CHECK(root.revision == object.revision);
		SHAPE_V11_CHECK(root.partCount == expectedCount);
		SHAPE_V11_CHECK(root.preset.has_value() == preset.has_value());
		if (preset) SHAPE_V11_CHECK(root.preset && *root.preset == *preset);
		SHAPE_V11_CHECK(root.aux.value_or(0) == static_cast<uint32_t>(object.style.auxiliary));
		const auto& t = object.transform;
		const auto transform = root.transform.value_or(std::array<float, 6>{ 1,0,0,1,0,0 });
		SHAPE_V11_CHECK(transform == std::array<float, 6>{ t.m11,t.m12,t.m21,t.m22,t.dx,t.dy });
		for (size_t i = 0; i < canvas.content.size(); ++i)
		{
			const auto* part = std::get_if<UInkShape>(&canvas.content[i]);
			if (!SHAPE_V11_CHECK(part && part->object)) return nullptr;
			SHAPE_V11_CHECK(part->contentId == i && part->undoId == 0);
			if (i == 0) continue;
			const auto* ref = std::get_if<UInkShapeObjectRef>(&*part->object);
			SHAPE_V11_CHECK(ref && ref->ref == root.id && ref->partIndex == i);
		}
		return &root;
	}

	void VerifyShapeV11Points(ShapeV11Test& test, const UInkContent& expectedPart, const UInkContent& actualPart)
	{
		const auto* expectedShape = std::get_if<UInkShape>(&expectedPart);
		const auto* actualShape = std::get_if<UInkShape>(&actualPart);
		SHAPE_V11_REQUIRE(expectedShape && actualShape);
		const auto& expected = *expectedShape;
		const auto& actual = *actualShape;
		SHAPE_V11_REQUIRE(expected.declaredShapeType == actual.declaredShapeType);
		const auto* a = std::get_if<UInkLineGeometry>(&expected.geometry);
		const auto* b = std::get_if<UInkLineGeometry>(&actual.geometry);
		SHAPE_V11_REQUIRE(a && b && a->points.size() == b->points.size());
		for (size_t i = 0; i < a->points.size(); ++i)
			SHAPE_V11_CHECK(ShapeV11Near(a->points[i], b->points[i]));
		SHAPE_V11_CHECK(expected.stroke.has_value() == actual.stroke.has_value());
		SHAPE_V11_CHECK(expected.fill.has_value() == actual.fill.has_value());
		if (expected.stroke)
		{
			SHAPE_V11_REQUIRE(actual.stroke);
			SHAPE_V11_CHECK(actual.stroke->lineCap == 0 && !actual.stroke->lineJoin);
			SHAPE_V11_CHECK(expected.stroke->color.fallbackRgb == actual.stroke->color.fallbackRgb);
			SHAPE_V11_CHECK(ShapeV11Near(expected.stroke->width, actual.stroke->width));
			SHAPE_V11_CHECK(expected.stroke->opacity == actual.stroke->opacity);
			SHAPE_V11_CHECK(expected.stroke->dashOffset == actual.stroke->dashOffset);
			SHAPE_V11_CHECK(expected.stroke->dashArray == actual.stroke->dashArray);
		}
	}

	void VerifyShapeV11GeneratedPart(ShapeV11Test& test, const UInkShape& actual,
		const GeometryPart& expected, bool stroke, bool fill, const std::vector<Vec2>* dashSegment = nullptr)
	{
		SHAPE_V11_CHECK(actual.stroke.has_value() == stroke && actual.fill.has_value() == fill);
		if (stroke)
		{
			SHAPE_V11_REQUIRE(actual.stroke);
			SHAPE_V11_CHECK(actual.stroke->lineCap == 0 && !actual.stroke->lineJoin);
			SHAPE_V11_CHECK(actual.stroke->color.fallbackRgb == expected.style.strokeRgb);
			SHAPE_V11_CHECK(actual.stroke->opacity == expected.style.strokeOpacity);
			SHAPE_V11_CHECK(ShapeV11Near(actual.stroke->width, expected.style.width));
		}
		if (fill)
		{
			SHAPE_V11_REQUIRE(actual.fill);
			SHAPE_V11_CHECK(actual.fill->color.fallbackRgb == expected.style.fillRgb);
			SHAPE_V11_CHECK(actual.fill->opacity == expected.style.fillOpacity);
		}
		const auto& g = expected.geometry;
		const auto& t = expected.transform;
		const float sx = std::hypot(t.m11, t.m12), sy = std::hypot(t.m21, t.m22);
		if (!dashSegment && expected.kind == Kind::Rectangle)
		{
			const auto* rectangle = std::get_if<UInkRectangleGeometry>(&actual.geometry);
			SHAPE_V11_REQUIRE(actual.declaredShapeType == 2 && rectangle);
			const auto center = ApplyTransform(t, { (g.start.x + g.end.x) * .5f, (g.start.y + g.end.y) * .5f });
			const float localWidth = std::abs(g.end.x - g.start.x);
			const float localHeight = std::abs(g.end.y - g.start.y);
			const float radius = (std::min)({ g.radius, localWidth * .5f, localHeight * .5f });
			SHAPE_V11_CHECK(ShapeV11Near(Vec2{ rectangle->centerX,rectangle->centerY }, center));
			SHAPE_V11_CHECK(ShapeV11Near(rectangle->width, localWidth * sx) && ShapeV11Near(rectangle->height, localHeight * sy));
			SHAPE_V11_CHECK(ShapeV11Near(rectangle->cornerRadiusX, radius * sx) && ShapeV11Near(rectangle->cornerRadiusY, radius * sy));
			SHAPE_V11_CHECK(ShapeV11Near(rectangle->rotation, std::atan2(t.m12, t.m11)));
		}
		else if (!dashSegment && expected.kind == Kind::Circle)
		{
			const auto* circle = std::get_if<UInkCircleGeometry>(&actual.geometry);
			SHAPE_V11_REQUIRE(actual.declaredShapeType == 5 && circle);
			SHAPE_V11_CHECK(ShapeV11Near(Vec2{ circle->centerX,circle->centerY }, ApplyTransform(t, g.start)));
			SHAPE_V11_CHECK(ShapeV11Near(circle->radius, g.radius * sx));
		}
		else if (!dashSegment && expected.kind == Kind::Ellipse)
		{
			const auto* ellipse = std::get_if<UInkEllipseGeometry>(&actual.geometry);
			SHAPE_V11_REQUIRE(actual.declaredShapeType == 4 && ellipse);
			SHAPE_V11_CHECK(ShapeV11Near(Vec2{ ellipse->centerX,ellipse->centerY }, ApplyTransform(t, g.start)));
			SHAPE_V11_CHECK(ShapeV11Near(ellipse->width, 2 * g.end.x * sx) && ShapeV11Near(ellipse->height, 2 * g.end.y * sy));
			SHAPE_V11_CHECK(ShapeV11Near(ellipse->rotation, std::atan2(t.m12, t.m11)));
		}
		else
		{
			const auto& path = expected.paths.front();
			const auto& points = dashSegment ? *dashSegment : path.points;
			const size_t count = points.size() - (!dashSegment && path.closed ? 1 : 0);
			const int type = dashSegment ? 1 : path.closed ? 6 : expected.kind == Kind::Line ? 0 : 1;
			const auto* line = std::get_if<UInkLineGeometry>(&actual.geometry);
			SHAPE_V11_REQUIRE(actual.declaredShapeType == type && line && line->points.size() == count);
			for (size_t i = 0; i < count; ++i)
				SHAPE_V11_CHECK(ShapeV11Near(Vec2{ line->points[i].x,line->points[i].y }, points[i]));
		}
	}

	void VerifyShapeV11GeneratedParts(ShapeV11Test& test, const UInkCanvas& actual, const ShapeObject& source)
	{
		GeometryResult body, auxiliary;
		SHAPE_V11_REQUIRE(BuildGeometry(source, body) && BuildAuxiliaryGeometry(source, auxiliary));
		for (auto& part : auxiliary.parts) body.parts.push_back(std::move(part));
		size_t index = 0;
		for (const auto& part : body.parts)
		{
			SHAPE_V11_REQUIRE(part.paths.size() == 1);
			const auto& path = part.paths.front();
			const bool split = path.closed && part.style.stroke && part.style.linePattern != LinePattern::Solid;
			if (!split || part.style.fill)
			{
				SHAPE_V11_REQUIRE(index < actual.content.size() && std::holds_alternative<UInkShape>(actual.content[index]));
				VerifyShapeV11GeneratedPart(test, std::get<UInkShape>(actual.content[index++]), part,
					!split && part.style.stroke, part.style.fill);
			}
			if (!split) continue;
			const auto sample = [&](float distance)
			{
				const auto upper = std::upper_bound(path.lengths.begin(), path.lengths.end(), distance);
				if (upper == path.lengths.end()) return path.points.back();
				const auto j = static_cast<size_t>(upper - path.lengths.begin());
				const auto a = path.points[j - 1], b = path.points[j];
				const float f = (distance - path.lengths[j - 1]) / (*upper - path.lengths[j - 1]);
				return Vec2{ a.x + (b.x - a.x) * f,a.y + (b.y - a.y) * f };
			};
			for (unsigned segment = 0; segment < 8; ++segment)
			{
				const float begin = path.lengths.back() * (segment / 8.f);
				const float end = path.lengths.back() * ((segment + 1) / 8.f);
				std::vector<Vec2> points{ sample(begin) };
				for (size_t j = 1; j < path.points.size(); ++j)
					if (begin < path.lengths[j] && path.lengths[j] < end) points.push_back(path.points[j]);
				points.push_back(sample(end));
				if (segment % 2) std::reverse(points.begin(), points.end());
				SHAPE_V11_REQUIRE(index < actual.content.size() && std::holds_alternative<UInkShape>(actual.content[index]));
				VerifyShapeV11GeneratedPart(test, std::get<UInkShape>(actual.content[index++]), part, true, false, &points);
			}
		}
		SHAPE_V11_CHECK(index == actual.content.size());
	}

	struct ShapeV11PresetCase
	{
		PresetKind preset;
		const char* name;
		std::initializer_list<const char*> keys;
		size_t wireParts;
	};

	// 必需键直接取自 1.1 注册表，部件数取自固定夹具的样式及辅助部件。
	const ShapeV11PresetCase kShapeV11Presets[] = {
		{ PresetKind::Line,"line",{ "start","end" },1 },
		{ PresetKind::Arrow,"arrow",{ "start","end" },2 },
		{ PresetKind::Rectangle,"rectangle",{ "start","end" },11 },
		{ PresetKind::RoundedRectangle,"rounded-rectangle",{ "start","end" },3 },
		{ PresetKind::Circle,"circle",{ "start","end","mode" },2 },
		{ PresetKind::Ellipse,"ellipse",{ "start","end" },3 },
		{ PresetKind::Triangle,"triangle",{ "start","end" },1 },
		{ PresetKind::Parallelogram,"parallelogram",{ "start","end" },3 },
		{ PresetKind::Rhombus,"rhombus",{ "start","end" },3 },
		{ PresetKind::Trapezoid,"trapezoid",{ "start","end" },3 },
		{ PresetKind::RegularPolygon,"regular-polygon",{ "start","end","sides" },2 },
		{ PresetKind::Polyline,"polyline",{ "vertices" },1 },
		{ PresetKind::Polygon,"polygon",{ "vertices" },1 },
		{ PresetKind::Arc,"arc",{ "start","end","sweep" },2 },
		{ PresetKind::Sector,"sector",{ "start","end","sweep" },2 },
		{ PresetKind::Cuboid,"cuboid",{ "start","height","baseSize" },12 },
		{ PresetKind::Cube,"cube",{ "start","height","baseSize" },12 },
		{ PresetKind::Cylinder,"cylinder",{ "start","height","radius" },5 },
		{ PresetKind::Cone,"cone",{ "start","height","radius" },4 },
		{ PresetKind::Prism,"prism",{ "start","height","radius","sides" },15 },
		{ PresetKind::Pyramid,"pyramid",{ "start","height","radius","sides" },10 }
	};
	static_assert(sizeof(kShapeV11Presets) / sizeof(kShapeV11Presets[0]) == 21);

	Construction ShapeV11Construction(PresetKind preset)
	{
		Construction c;
		c.tool.preset = preset;
		c.tool.creationMode = DefaultCreationMode(preset);
		c.start = { 20,30 };
		c.end = { 140,110 };
		switch (preset)
		{
		case PresetKind::Rectangle:
			c.tool.rectangleCornerRadius = 7.25f;
			break;
		case PresetKind::Circle:
			c.tool.creationMode = CreationMode::Drag;
			break;
		case PresetKind::Triangle:
			c.tool.constraint = PlanarConstraint::Isosceles;
			c.tool.constraintVertex = 1;
			c.parameters = VertexConstruction{ { { 140,110 },{ 80,30 },{ 20,110 } } };
			break;
		case PresetKind::Parallelogram:
			c.parameters = VertexConstruction{ { { 20,30 },{ 110,30 },{ 140,110 },{ 50,110 } } };
			break;
		case PresetKind::Rhombus:
			c.parameters = VertexConstruction{ { { 80,30 },{ 140,70 },{ 80,110 },{ 20,70 } } };
			break;
		case PresetKind::Trapezoid:
			c.tool.constraint = PlanarConstraint::Isosceles;
			c.parameters = VertexConstruction{ { { 49,30 },{ 111,30 },{ 140,110 },{ 20,110 } } };
			break;
		case PresetKind::RegularPolygon:
			c.tool.sides = 7;
			break;
		case PresetKind::Polyline:
		case PresetKind::Polygon:
			c.parameters = VertexConstruction{ { c.start,{ 140,30 },{ 140,110 },{ 20,110 } } };
			c.end = std::get<VertexConstruction>(c.parameters).vertices.back();
			break;
		case PresetKind::Arc:
		case PresetKind::Sector:
			c.parameters = ArcConstruction{ -2.375f };
			break;
		case PresetKind::Cuboid:
		case PresetKind::Cube:
		case PresetKind::Cylinder:
		case PresetKind::Cone:
		case PresetKind::Prism:
		case PresetKind::Pyramid:
		{
			SolidConstruction solid;
			solid.height = -80;
			solid.baseSize = preset == PresetKind::Cuboid ? Vec2{ 90,50 } :
				preset == PresetKind::Cube ? Vec2{ 80,80 } : Vec2{ 35,35 };
			solid.baseRotation = 0.375f;
			solid.mirrored = preset == PresetKind::Cuboid || preset == PresetKind::Cube ||
				preset == PresetKind::Pyramid;
			if (preset == PresetKind::Prism || preset == PresetKind::Pyramid) c.tool.sides = 5;
			c.tool.rightSolid = preset == PresetKind::Prism;
			if (!c.tool.rightSolid) solid.offset = { 5,7 };
			c.parameters = solid;
			c.end = {};
			break;
		}
		default:
			break;
		}
		return c;
	}

	ShapeStyle ShapeV11Style(PresetKind preset)
	{
		ShapeStyle style;
		style.strokeRgb = 0x123456;
		style.strokeOpacity = .625f;
		style.fill = GetToolCapabilities(preset).fill;
		if (style.fill) style.fillRgb = 0x986754;
		style.fillOpacity = .375f;
		style.width = 3.25f;
		style.dashOffset = 1.5f;
		style.auxiliary = SupportedAuxiliary(preset);
		if (preset == PresetKind::Rectangle) style.linePattern = LinePattern::Dotted;
		if (preset == PresetKind::Arrow) style.linePattern = LinePattern::Dashed;
		return style;
	}

	std::optional<ShapeObject> EditShapeV11Preset(const ShapeObject& object)
	{
		auto construction = *object.construction;
		if (auto* vertices = std::get_if<VertexConstruction>(&construction.parameters))
		{
			// 成比例地移动顶点，约束三角形、梯形和菱形仍为合法构造。
			for (auto& point : vertices->vertices)
				point = { point.x * 1.25f,point.y * 1.25f };
		}
		else if (auto* arc = std::get_if<ArcConstruction>(&construction.parameters))
			arc->sweepRadians *= .75f;
		else if (auto* solid = std::get_if<SolidConstruction>(&construction.parameters))
			solid->baseRotation += .25f;
		else if (construction.tool.preset == PresetKind::RegularPolygon)
			++construction.tool.sides;
		else
			construction.end.x += 16;
		auto edited = MakeConstructedShape(construction, object.style, object.id, object.revision + 1);
		if (edited) edited->transform = object.transform;
		return edited;
	}

	void TestShapeV11Preset(ShapeV11Test& test, const ShapeV11PresetCase& fixture)
	{
		auto source = MakeConstructedShape(ShapeV11Construction(fixture.preset), ShapeV11Style(fixture.preset),
			ShapeV11Guid(0x02).Bytes(), 0x100000007ULL);
		SHAPE_V11_REQUIRE(source);
		source->transform = { 1.25f,0,0,1.25f,9.5f,-12.25f };
		SHAPE_V11_REQUIRE(Validate(*source));
		const auto canvas = ExportShapeV11Page(test, *source);
		SHAPE_V11_REQUIRE(canvas);
		const auto decoded = DecodeShapeV11Page(test, *canvas);
		SHAPE_V11_REQUIRE(decoded);
		const auto* root = VerifyShapeV11Parts(test, *decoded, fixture.name, *source, fixture.wireParts);
		SHAPE_V11_REQUIRE(root && root->construction && root->style);
		for (const auto* key : fixture.keys)
			SHAPE_V11_CHECK(ShapeV11Field(*root->construction, key));

		VerifyShapeV11GeneratedParts(test, *decoded, *source);

		// 核对虚线拆段和箭头的额外样式约定。
		if (fixture.preset == PresetKind::Arrow)
		{
			const auto& shaft = std::get<UInkShape>(decoded->content[0]);
			const auto& head = std::get<UInkShape>(decoded->content[1]);
			SHAPE_V11_REQUIRE(shaft.stroke && head.stroke);
			SHAPE_V11_CHECK(shaft.declaredShapeType == 0 && head.declaredShapeType == 1);
			SHAPE_V11_CHECK(!shaft.stroke->dashArray.empty() && head.stroke->dashArray.empty());
		}
		if (fixture.preset == PresetKind::Rectangle)
		{
			const auto& fill = std::get<UInkShape>(decoded->content[0]);
			SHAPE_V11_CHECK(fill.declaredShapeType == 2 && fill.fill && !fill.stroke);
			for (size_t i = 1; i <= 8; ++i)
			{
				const auto& segment = std::get<UInkShape>(decoded->content[i]);
				SHAPE_V11_REQUIRE(segment.stroke);
				SHAPE_V11_CHECK(segment.declaredShapeType == 1 && !segment.fill);
				SHAPE_V11_CHECK(segment.stroke->dashArray == std::vector<float>{ 0,3 * segment.stroke->width });
			}
			for (size_t i = 9; i <= 10; ++i)
				SHAPE_V11_CHECK(std::get<UInkShape>(decoded->content[i]).declaredShapeType == 0);
		}

		const auto imported = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1 && imported.page->operations.size() == 1);
		const auto& stroke = imported.page->strokes.front();
		const auto* operation = std::get_if<Draw3UInkStrokeSnapshot>(&imported.page->operations.front());
		SHAPE_V11_REQUIRE(stroke.shape && stroke.shape->construction && operation);
		SHAPE_V11_CHECK(stroke.undoId == 0 && operation->undoId == 0 && imported.page->intervalOrdinal == 0);
		const auto& restored = *stroke.shape;
		SHAPE_V11_CHECK(restored.kind == source->kind && restored.id == source->id && restored.revision == source->revision);
		SHAPE_V11_CHECK(restored.construction == source->construction);
		SHAPE_V11_CHECK(restored.style == source->style && restored.transform == source->transform);
		SHAPE_V11_CHECK(restored.geometry == source->geometry && restored.parts == source->parts);
		SHAPE_V11_CHECK(imported.counts.invalidShapeConstruction == 0 && imported.counts.shapePartCountMismatch == 0);

		const auto editedSource = EditShapeV11Preset(*source);
		const auto editedRestored = EditShapeV11Preset(restored);
		SHAPE_V11_REQUIRE(editedSource && editedRestored);
		SHAPE_V11_CHECK(editedSource->geometry != source->geometry || editedSource->parts != source->parts);
		SHAPE_V11_CHECK(*editedSource == *editedRestored);
		GeometryResult expectedGeometry, actualGeometry;
		SHAPE_V11_REQUIRE(BuildGeometry(*editedSource, expectedGeometry));
		SHAPE_V11_REQUIRE(BuildGeometry(*editedRestored, actualGeometry));
		SHAPE_V11_REQUIRE(!expectedGeometry.parts.empty() && expectedGeometry.parts.size() == actualGeometry.parts.size());
		for (size_t i = 0; i < expectedGeometry.parts.size(); ++i)
		{
			const auto& expected = expectedGeometry.parts[i];
			const auto& actual = actualGeometry.parts[i];
			SHAPE_V11_CHECK(expected.kind == actual.kind && expected.style == actual.style);
			SHAPE_V11_REQUIRE(!expected.paths.empty() && expected.paths.size() == actual.paths.size());
			for (size_t j = 0; j < expected.paths.size(); ++j)
				SHAPE_V11_CHECK(expected.paths[j].points == actual.paths[j].points);
		}
	}

	std::optional<ShapeObject> RoundTripShapeV11(ShapeV11Test& test, const ShapeObject& source)
	{
		const auto exported = ExportShapeV11Page(test, source);
		if (!exported) return std::nullopt;
		const auto decoded = DecodeShapeV11Page(test, *exported);
		if (!decoded) return std::nullopt;
		VerifyShapeV11GeneratedParts(test, *decoded, source);
		const auto read = ImportUInkV11Page(*decoded);
		if (!SHAPE_V11_CHECK(read.status == Draw3UInkV11ImportStatus::Success && read.page &&
			read.page->strokes.size() == 1 && read.page->operations.size() == 1 &&
			read.page->strokes.front().shape)) return std::nullopt;
		const auto& restored = *read.page->strokes.front().shape;
		SHAPE_V11_CHECK(restored == source);
		return restored;
	}

	void TestShapeV11ConstructionVariants(ShapeV11Test& test)
	{
		const auto id = ShapeV11Guid(0x21).Bytes();
		const auto polygon = MakePresetShape({ PresetKind::RegularPolygon,CreationMode::CenterRadius,14 },
			{ 30,70 }, { 130,110 }, {}, id);
		SHAPE_V11_REQUIRE(polygon);
		RoundTripShapeV11(test, *polygon);

		const auto triangle = MakePresetShape({ PresetKind::Triangle,CreationMode::Drag },
			{ 20,30 }, { 140,110 }, {}, id);
		SHAPE_V11_REQUIRE(triangle);
		const auto isosceles = SetPlanarConstraint(*triangle, PlanarConstraint::Isosceles, 2);
		SHAPE_V11_REQUIRE(isosceles);
		RoundTripShapeV11(test, *isosceles);
		const auto equilateral = SetPlanarConstraint(*isosceles, PlanarConstraint::Equilateral, 3);
		SHAPE_V11_REQUIRE(equilateral);
		RoundTripShapeV11(test, *equilateral);

		const auto trapezoid = MakePresetShape({ PresetKind::Trapezoid,CreationMode::Drag },
			{ 20,30 }, { 140,110 }, {}, id);
		SHAPE_V11_REQUIRE(trapezoid);
		const auto evenTrapezoid = SetPlanarConstraint(*trapezoid, PlanarConstraint::Isosceles, 2);
		SHAPE_V11_REQUIRE(evenTrapezoid && evenTrapezoid->geometry.vertices.size() > 2);
		RoundTripShapeV11(test, *evenTrapezoid);
		const auto corner = evenTrapezoid->geometry.vertices[2];
		const auto tilted = EditShape(*evenTrapezoid, 1002, corner, { corner.x + 10,corner.y + 20 }, 3);
		SHAPE_V11_REQUIRE(tilted);
		const auto free = SetPlanarConstraint(*tilted, PlanarConstraint::Free, 4);
		SHAPE_V11_REQUIRE(free);
		RoundTripShapeV11(test, *free);

		// 负向长扫角、不同边数和正负高度也必须按官方构造参数恢复。
		for (const auto preset : { PresetKind::Arc,PresetKind::Sector })
		{
			for (const float sweep : { -4.4f,4.4f })
			{
				auto construction = ShapeV11Construction(preset);
				construction.parameters = ArcConstruction{ sweep };
				const auto shape = MakeConstructedShape(construction, ShapeV11Style(preset), id, 21);
				SHAPE_V11_REQUIRE(shape);
				RoundTripShapeV11(test, *shape);
			}
		}
		for (const auto preset : { PresetKind::Prism,PresetKind::Pyramid })
		{
			for (const uint32_t sides : { 3u,5u,6u,8u })
			{
				for (const float height : { -60.f,60.f })
				{
					auto construction = MakeSolidConstruction({ preset,CreationMode::MultiStage,sides },
						{ 30,70 }, { 130,110 }, height);
					construction.end = {}; // 立体线格式只表达已求出的模型参数。
					const auto shape = MakeConstructedShape(construction, ShapeV11Style(preset), id, 21);
					SHAPE_V11_REQUIRE(shape);
					RoundTripShapeV11(test, *shape);
				}
			}
		}
	}

	void TestShapeV11TransformedSolids(ShapeV11Test& test)
	{
		for (const auto preset : { PresetKind::Cuboid,PresetKind::Cube,PresetKind::Cylinder,
			PresetKind::Cone,PresetKind::Prism,PresetKind::Pyramid })
		{
			const bool round = preset == PresetKind::Cylinder || preset == PresetKind::Cone;
			const bool polygon = preset == PresetKind::Prism || preset == PresetKind::Pyramid;
			auto construction = MakeSolidConstruction({ preset,CreationMode::MultiStage,polygon ? 7u : 3u },
				{ 80,80 }, { 180,130 }, -150);
			construction.end = {};
			auto& solid = std::get<SolidConstruction>(construction.parameters);
			solid.baseRotation = .3f;
			solid.mirrored = !round;
			if (polygon) solid.offset = { 20,15 };
			const auto shape = MakeConstructedShape(construction, {}, ShapeV11Guid(0x22).Bytes(), 3);
			SHAPE_V11_REQUIRE(shape);
			const auto restored = RoundTripShapeV11(test, *shape);
			SHAPE_V11_REQUIRE(restored && restored->construction);
			for (const bool horizontal : { false,true })
			{
				const auto flipped = FlipShape(*shape, horizontal, 4);
				SHAPE_V11_REQUIRE(flipped);
				const auto gesture = TransformShapeGesture(*flipped, { 100,100 }, { 20,-10 }, 1.25f, .7f, 5);
				SHAPE_V11_REQUIRE(gesture);
				RoundTripShapeV11(test, *gesture);
			}
			if (!round) continue;
			const auto& dimensions = std::get<SolidConstruction>(restored->construction->parameters);
			SHAPE_V11_CHECK(dimensions.baseSize.x == dimensions.baseSize.y);
			for (const auto& part : restored->parts)
			{
				if (part.kind != Kind::Polyline) continue;
				for (const auto point : part.geometry.vertices)
				{
					const double x = (point.x - construction.start.x) / dimensions.baseSize.x;
					const double y = (point.y - construction.start.y) * 3 / dimensions.baseSize.x;
					const double topY = (point.y - construction.start.y - dimensions.height) * 3 / dimensions.baseSize.x;
					SHAPE_V11_CHECK(std::min(std::abs(x*x + y*y - 1), std::abs(x*x + topY*topY - 1)) < .0001);
				}
			}
		}
	}

	void TestShapeV11RotatedAuxiliaryParts(ShapeV11Test& test)
	{
		for (const auto preset : { PresetKind::Circle,PresetKind::Ellipse,PresetKind::Rectangle,
			PresetKind::RoundedRectangle,PresetKind::Parallelogram,PresetKind::Rhombus,
			PresetKind::Trapezoid,PresetKind::RegularPolygon,PresetKind::Arc,PresetKind::Sector })
		{
			auto construction = ShapeV11Construction(preset);
			ShapeStyle style;
			style.auxiliary = SupportedAuxiliary(preset);
			style.width = 3;
			style.strokeOpacity = .4f;
			// 纯填充椭圆仍保存 aux，但不应生成辅助描边。
			if (preset == PresetKind::Ellipse)
			{
				style.stroke = false;
				style.fill = true;
				style.fillRgb = 0x123456;
			}
			auto source = MakeConstructedShape(construction, style, ShapeV11Guid(0x23).Bytes(), 23);
			SHAPE_V11_REQUIRE(source);
			source->transform = { 0,1,-1,0,200,10 };
			RoundTripShapeV11(test, *source);
		}
	}

	void TestShapeV11DuplicateExportIdentity(ShapeV11Test& test)
	{
		const auto object = MakeConstructedShape(ShapeV11Construction(PresetKind::Arrow),
			ShapeV11Style(PresetKind::Arrow), ShapeV11Guid(0x24).Bytes(), 1);
		SHAPE_V11_REQUIRE(object);
		auto page = ShapeV11Page();
		page.strokes = { ShapeV11Stroke(*object), ShapeV11Stroke(*object) };
		const auto rejected = ExportDraw3PageToUInkV11(page);
		SHAPE_V11_CHECK(rejected.status == Draw3UInkExportStatus::InvalidSourceStroke &&
			!rejected.canvas && !rejected.diagnostics.empty());
	}

	void TestShapeV11Legacy(ShapeV11Test& test, LegacyKind kind, const char* preset)
	{
		const auto source = MakeLegacyShape(kind, { 20,30 }, { 140,110 }, 3, 0x345678, .625f,
			ShapeV11Guid(0x03).Bytes());
		SHAPE_V11_REQUIRE(source);
		auto page = ShapeV11Page();
		auto stroke = ShapeV11Stroke(*source);
		stroke.style.kind = static_cast<Draw3UInkStrokeKind>(
			static_cast<uint32_t>(Draw3UInkStrokeKind::SolidLine) + static_cast<uint32_t>(kind) - 1);
		stroke.style.fallbackRgb = 0x345678;
		stroke.style.opacity = .625f;
		stroke.points = { { 20,30,3 },{ 140,110,3 } };
		page.strokes.push_back(std::move(stroke));
		Draw3UInkV11PageContext context;
		context.dpiScale = 1.5f;
		const auto exported = ExportDraw3PageToUInkV11(page, context);
		SHAPE_V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
		const auto decoded = DecodeShapeV11Page(test, *exported.canvas);
		SHAPE_V11_REQUIRE(decoded);
		const auto* root = VerifyShapeV11Parts(test, *decoded, preset, *source, 1);
		SHAPE_V11_REQUIRE(root);
		SHAPE_V11_CHECK(!root->construction);
		const auto& wire = std::get<UInkShape>(decoded->content.front());

		Draw3UInkV11ImportOptions options;
		options.dpiScale = context.dpiScale;
		const auto result = ImportUInkV11Page(*decoded, options);
		SHAPE_V11_REQUIRE(result.status == Draw3UInkV11ImportStatus::Success && result.page);
		SHAPE_V11_REQUIRE(result.page->strokes.size() == 1 && result.page->operations.size() == 1);
		const auto& restored = result.page->strokes.front();
		SHAPE_V11_REQUIRE(restored.shape);
		SHAPE_V11_CHECK(restored.undoId == 0 && !restored.shape->construction);
		SHAPE_V11_CHECK(result.counts.legacyRectangle == 0);
		SHAPE_V11_CHECK(restored.shape->legacyKind == kind && restored.shape->style == source->style);
		if (source->kind == Kind::Rectangle)
		{
			const auto* rectangle = std::get_if<UInkRectangleGeometry>(&wire.geometry);
			SHAPE_V11_REQUIRE(rectangle);
			SHAPE_V11_CHECK(rectangle->cornerRadiusX == 6 && rectangle->cornerRadiusY == 6);
			SHAPE_V11_CHECK(restored.shape->kind == Kind::Rectangle && restored.shape->geometry == source->geometry);

			options.dpiScale = 2.0f;
			const auto otherScale = ImportUInkV11Page(*decoded, options);
			SHAPE_V11_REQUIRE(otherScale.status == Draw3UInkV11ImportStatus::Success && otherScale.page);
			SHAPE_V11_REQUIRE(otherScale.page->strokes.size() == 1 && otherScale.page->strokes.front().shape);
			const auto& compound = *otherScale.page->strokes.front().shape;
			SHAPE_V11_CHECK(otherScale.counts.legacyRectangle == 1);
			SHAPE_V11_CHECK(compound.kind == Kind::Compound && compound.legacyKind == LegacyKind::None);
			SHAPE_V11_REQUIRE(compound.parts.size() == 1);
			const auto reexported = ExportShapeV11Page(test, compound);
			SHAPE_V11_REQUIRE(reexported && reexported->content.size() == 1);
			const auto* reexportedShape = std::get_if<UInkShape>(&reexported->content.front());
			SHAPE_V11_REQUIRE(reexportedShape);
			const auto* preserved = std::get_if<UInkRectangleGeometry>(&reexportedShape->geometry);
			SHAPE_V11_REQUIRE(preserved);
			SHAPE_V11_CHECK(preserved->centerX == rectangle->centerX && preserved->centerY == rectangle->centerY);
			SHAPE_V11_CHECK(preserved->width == rectangle->width && preserved->height == rectangle->height);
			SHAPE_V11_CHECK(preserved->cornerRadiusX == rectangle->cornerRadiusX);
			SHAPE_V11_CHECK(preserved->cornerRadiusY == rectangle->cornerRadiusY);
			SHAPE_V11_CHECK(preserved->rotation == rectangle->rotation);
		}
		else
		{
			SHAPE_V11_CHECK(restored.style.kind == page.strokes.front().style.kind);
			SHAPE_V11_CHECK(restored.shape->legacyKind == kind && restored.shape->geometry == source->geometry);
			SHAPE_V11_CHECK(restored.shape->style.strokeRgb == source->style.strokeRgb);
			SHAPE_V11_CHECK(restored.shape->style.strokeOpacity == source->style.strokeOpacity);
			SHAPE_V11_CHECK(restored.shape->style.linePattern == source->style.linePattern);

			auto editedSource = *source;
			auto editedRestored = *restored.shape;
			editedSource.geometry.end.x += 9;
			editedRestored.geometry.end.x += 9;
			++editedSource.revision;
			++editedRestored.revision;
			GeometryResult expected, actual;
			SHAPE_V11_REQUIRE(BuildGeometry(editedSource, expected) && BuildGeometry(editedRestored, actual));
			SHAPE_V11_REQUIRE(expected.parts.size() == 1 && actual.parts.size() == 1);
			SHAPE_V11_REQUIRE(expected.parts[0].paths.size() == 1 && actual.parts[0].paths.size() == 1);
			SHAPE_V11_CHECK(expected.parts[0].paths[0].points == actual.parts[0].paths[0].points);
		}
	}

	void TestShapeV11CompoundClearNumbering(ShapeV11Test& test)
	{
		const auto arrow = MakeConstructedShape(ShapeV11Construction(PresetKind::Arrow),
			ShapeV11Style(PresetKind::Arrow), ShapeV11Guid(0x25).Bytes());
		const auto cube = MakeConstructedShape(ShapeV11Construction(PresetKind::Cube),
			ShapeV11Style(PresetKind::Cube), ShapeV11Guid(0x26).Bytes());
		SHAPE_V11_REQUIRE(arrow && cube && arrow->kind == Kind::Compound && cube->kind == Kind::Compound);
		auto page = ShapeV11Page();
		const auto first = ShapeV11Stroke(*arrow);
		auto second = ShapeV11Stroke(*cube);
		second.undoId = first.undoId + 1;
		page.operations = { first,second,Draw3UInkClearSnapshot{ second.undoId } };
		page.intervalOrdinal = 1;

		// 箭头和立方体分别有 2 与 12 个 wire 部件，Clear 仍是第 3 个逻辑撤回组。
		constexpr size_t arrowParts = 2;
		constexpr size_t clearIndex = arrowParts + 12;
		const auto exported = ExportDraw3PageToUInkV11(page);
		SHAPE_V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas &&
			exported.canvas->content.size() == clearIndex + 1);
		const auto* clear = std::get_if<UInkClear>(&exported.canvas->content[clearIndex]);
		SHAPE_V11_REQUIRE(clear);
		SHAPE_V11_CHECK(clear->contentId == clearIndex && clear->undoId == 2);

		const auto decoded = DecodeShapeV11Page(test, *exported.canvas);
		SHAPE_V11_REQUIRE(decoded && decoded->content.size() == clearIndex + 1);
		for (size_t index = 0; index < clearIndex; ++index)
		{
			const auto* part = std::get_if<UInkShape>(&decoded->content[index]);
			SHAPE_V11_REQUIRE(part);
			SHAPE_V11_CHECK(part->contentId == index && part->undoId == (index < arrowParts ? 0u : 1u));
		}
		const auto imported = ImportUInkV11Page(*decoded);
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page &&
			imported.page->operations.size() == 3);
		SHAPE_V11_CHECK(imported.page->intervalOrdinal == 1 && imported.page->strokes.empty());
		const auto* restoredArrow = std::get_if<Draw3UInkStrokeSnapshot>(&imported.page->operations[0]);
		const auto* restoredCube = std::get_if<Draw3UInkStrokeSnapshot>(&imported.page->operations[1]);
		const auto* restoredClear = std::get_if<Draw3UInkClearSnapshot>(&imported.page->operations[2]);
		SHAPE_V11_REQUIRE(restoredArrow && restoredArrow->shape && restoredCube && restoredCube->shape && restoredClear);
		SHAPE_V11_CHECK(restoredArrow->shape->id == arrow->id && restoredArrow->undoId == 0);
		SHAPE_V11_CHECK(restoredCube->shape->id == cube->id && restoredCube->undoId == 1);
		SHAPE_V11_CHECK(restoredClear->undoId == 2);
	}

	void TestShapeV11Compound(ShapeV11Test& test)
	{
		ShapeObject source;
		source.id = ShapeV11Guid(0x04).Bytes();
		source.revision = 12;
		source.kind = Kind::Compound;
		source.creationMode = CreationMode::Parameters;
		source.style = ShapeV11Style(PresetKind::Line);
		source.transform = { 1,0,0,1,11,17 };
		ShapePart line;
		line.geometry.start = { 1,2 };
		line.geometry.end = { 30,40 };
		ShapePart path;
		path.id = 2;
		path.kind = Kind::Polyline;
		path.geometry.vertices = { { 23,45 },{ 56,59 },{ 78,85 } };
		path.style = source.style;
		path.style->linePattern = LinePattern::DashDot;
		path.style->strokeRgb = 0xabcdef;
		source.parts = { line,path };
		SHAPE_V11_REQUIRE(Validate(source));
		const auto wire = ExportShapeV11Page(test, source);
		SHAPE_V11_REQUIRE(wire);
		const auto decoded = DecodeShapeV11Page(test, *wire);
		SHAPE_V11_REQUIRE(decoded);
		const auto* root = VerifyShapeV11Parts(test, *decoded, std::nullopt, source, 2);
		SHAPE_V11_REQUIRE(root);
		SHAPE_V11_CHECK(!root->construction);
		const auto imported = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1 && imported.page->operations.size() == 1);
		const auto& stroke = imported.page->strokes.front();
		SHAPE_V11_REQUIRE(stroke.shape);
		SHAPE_V11_CHECK(stroke.undoId == 0);
		SHAPE_V11_CHECK(imported.counts.missingShapePreset == 1);
		SHAPE_V11_CHECK(stroke.shape->kind == Kind::Compound && !stroke.shape->construction);
		SHAPE_V11_CHECK(stroke.shape->id == source.id && stroke.shape->revision == source.revision);
		SHAPE_V11_REQUIRE(stroke.shape->parts.size() == 2);
		SHAPE_V11_CHECK(stroke.shape->parts[0].kind == Kind::Line && stroke.shape->parts[1].kind == Kind::Polyline);

		// 无构造对象的本地空间可以归一化，整体移动后文件部件仍应完全对应。
		auto movedSource = source;
		auto movedRestored = *stroke.shape;
		movedSource.transform.dx += 20;
		movedRestored.transform.dx += 20;
		const auto a = ExportShapeV11Page(test, movedSource);
		const auto b = ExportShapeV11Page(test, movedRestored);
		SHAPE_V11_REQUIRE(a && b && a->content.size() == 2 && b->content.size() == 2);
		for (size_t i = 0; i < 2; ++i)
			VerifyShapeV11Points(test, a->content[i], b->content[i]);
	}

	enum class ShapeV11ObjectFault
	{
		MissingPart,
		WrongPartCount,
		WrongPartOrder,
		OrphanReference,
		DuplicateId,
		MissingPreset,
		UnknownPreset,
		BadConstruction,
		RebuildCountMismatch,
		DifferentWireGeometry
	};

	void TestShapeV11ObjectFallback(ShapeV11Test& test, ShapeV11ObjectFault fault)
	{
		const auto source = MakeConstructedShape(ShapeV11Construction(PresetKind::Arrow),
			ShapeV11Style(PresetKind::Arrow), ShapeV11Guid(0x05).Bytes());
		SHAPE_V11_REQUIRE(source);
		auto wire = ExportShapeV11Page(test, *source);
		SHAPE_V11_REQUIRE(wire && wire->content.size() == 2);
		SHAPE_V11_REQUIRE(std::holds_alternative<UInkShape>(wire->content[0]) &&
			std::holds_alternative<UInkShape>(wire->content[1]));
		auto& first = std::get<UInkShape>(wire->content[0]);
		auto& second = std::get<UInkShape>(wire->content[1]);
		SHAPE_V11_REQUIRE(first.object && second.object);
		auto* root = std::get_if<UInkShapeObject>(&*first.object);
		auto* ref = std::get_if<UInkShapeObjectRef>(&*second.object);
		SHAPE_V11_REQUIRE(root && ref);
		bool independent = true;
		switch (fault)
		{
		case ShapeV11ObjectFault::MissingPart:
			wire->content.pop_back();
			break;
		case ShapeV11ObjectFault::WrongPartCount:
			root->partCount = 1;
			break;
		case ShapeV11ObjectFault::WrongPartOrder:
			ref->partIndex = 2;
			break;
		case ShapeV11ObjectFault::OrphanReference:
			ref->ref = ShapeV11Guid(0x06);
			break;
		case ShapeV11ObjectFault::DuplicateId:
		{
			auto duplicate = first;
			duplicate.contentId = 2;
			duplicate.undoId = 1;
			std::get<UInkShapeObject>(*duplicate.object).partCount = 1;
			wire->content.emplace_back(std::move(duplicate));
			break;
		}
		case ShapeV11ObjectFault::MissingPreset:
			root->preset.reset();
			independent = false;
			break;
		case ShapeV11ObjectFault::UnknownPreset:
			root->preset = "x-vendor-unknown";
			independent = false;
			break;
		case ShapeV11ObjectFault::BadConstruction:
			SHAPE_V11_REQUIRE(root->construction);
			SHAPE_V11_REQUIRE(ShapeV11SetField(*root->construction, "end", ShapeV11Value(true)));
			independent = false;
			break;
		case ShapeV11ObjectFault::RebuildCountMismatch:
			root->preset = "line";
			independent = false;
			break;
		case ShapeV11ObjectFault::DifferentWireGeometry:
			{
			auto* line = std::get_if<UInkLineGeometry>(&first.geometry);
			SHAPE_V11_REQUIRE(line && line->points.size() == 2);
			line->points[0].x += 94;
			independent = false;
			break;
		}
		}

		const auto decoded = DecodeShapeV11Page(test, *wire);
		SHAPE_V11_REQUIRE(decoded && decoded->content.size() == wire->content.size());
		const auto imported = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		if (independent)
		{
			SHAPE_V11_REQUIRE(imported.page->strokes.size() == decoded->content.size());
			SHAPE_V11_CHECK(imported.page->operations.size() == decoded->content.size());
			const uint64_t expectedWarnings = fault == ShapeV11ObjectFault::OrphanReference ? 2 : 1;
			SHAPE_V11_CHECK(imported.counts.incompleteShapeObject == expectedWarnings);
			for (size_t i = 0; i < imported.page->strokes.size(); ++i)
			{
				const auto& stroke = imported.page->strokes[i];
				const auto* part = std::get_if<UInkShape>(&decoded->content[i]);
				SHAPE_V11_REQUIRE(part && stroke.shape);
				SHAPE_V11_CHECK(!stroke.shape->construction && stroke.shape->kind != Kind::Compound);
				SHAPE_V11_CHECK(stroke.undoId == part->undoId);
				for (size_t j = 0; j < i; ++j)
					SHAPE_V11_CHECK(stroke.shape->id != imported.page->strokes[j].shape->id);
			}
			return;
		}
		SHAPE_V11_REQUIRE(imported.page->operations.size() == 1 && imported.page->strokes.size() == 1);
		const auto& shape = imported.page->strokes.front().shape;
		SHAPE_V11_REQUIRE(shape && shape->parts.size() == 2 && shape->kind == Kind::Compound);
		SHAPE_V11_CHECK(imported.page->strokes.front().undoId == 0);
		if (fault == ShapeV11ObjectFault::DifferentWireGeometry)
		{
			// SHAPE-15 的有意例外：文件部件数正确时直接显示预设的重建几何。
			SHAPE_V11_CHECK(shape->construction == source->construction && shape->parts == source->parts);
			SHAPE_V11_CHECK(imported.counts.shapePartCountMismatch == 0 && imported.counts.invalidShapeConstruction == 0);
			return;
		}
		SHAPE_V11_CHECK(!shape->construction);
		SHAPE_V11_CHECK(shape->id == source->id && shape->revision == source->revision);
		SHAPE_V11_CHECK(imported.counts.incompleteShapeObject == 0);
		switch (fault)
		{
		case ShapeV11ObjectFault::MissingPreset: SHAPE_V11_CHECK(imported.counts.missingShapePreset == 1); break;
		case ShapeV11ObjectFault::UnknownPreset: SHAPE_V11_CHECK(imported.counts.unknownShapePreset == 1); break;
		case ShapeV11ObjectFault::BadConstruction: SHAPE_V11_CHECK(imported.counts.invalidShapeConstruction == 1); break;
		case ShapeV11ObjectFault::RebuildCountMismatch: SHAPE_V11_CHECK(imported.counts.shapePartCountMismatch == 1); break;
		default: break;
		}
		const auto restored = ExportShapeV11Page(test, *shape);
		SHAPE_V11_REQUIRE(restored && restored->content.size() == 2);
		for (size_t i = 0; i < 2; ++i)
			VerifyShapeV11Points(test, wire->content[i], restored->content[i]);
	}

	UInkCanvas ShapeV11ForeignLine()
	{
		UInkCanvas canvas;
		canvas.pageGuid = ShapeV11Guid(0x07);
		canvas.pageNumber = canvas.layerNumber = 1;
		UInkShape line;
		line.geometry = UInkLineGeometry{ { { 10,15 },{ 40,50 } } };
		line.stroke.emplace();
		line.stroke->width = 3;
		line.stroke->color.fallbackRgb = 0x1563ae;
		canvas.content.emplace_back(std::move(line));
		return canvas;
	}

	void TestShapeV11ForeignRectangles(ShapeV11Test& test)
	{
		const std::array radii{ 0.0f,20.0f,4.0f,5.0f,6.0f,8.0f,16.0f,7.5f,6.5f };
		auto canvas = ShapeV11ForeignLine();
		auto rectangle = std::get<UInkShape>(canvas.content.front());
		rectangle.declaredShapeType = 2;
		canvas.content.clear();
		for (uint32_t index = 0; index < radii.size(); ++index)
		{
			rectangle.contentId = rectangle.undoId = index;
			rectangle.geometry = UInkRectangleGeometry{ 80,70,120,80,0,radii[index],radii[index] };
			canvas.content.emplace_back(rectangle);
		}
		const auto decoded = DecodeShapeV11Page(test, canvas);
		SHAPE_V11_REQUIRE(decoded && decoded->content.size() == radii.size());
		for (const auto& content : decoded->content)
		{
			const auto* part = std::get_if<UInkShape>(&content);
			SHAPE_V11_REQUIRE(part && !part->object);
		}

		// 即使线上的半径恰好等于当前或其他档位的 4 DIP，也应保留外来矩形的像素半径。
		for (const float dpiScale : { 1.0f,1.25f,1.5f,2.0f,4.0f })
		{
			Draw3UInkV11ImportOptions options;
			options.dpiScale = dpiScale;
			const auto imported = ImportUInkV11Page(*decoded, options);
			SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page &&
				imported.page->strokes.size() == radii.size());
			for (size_t index = 0; index < radii.size(); ++index)
			{
				const auto& stroke = imported.page->strokes[index];
				SHAPE_V11_REQUIRE(stroke.shape);
				const auto& shape = *stroke.shape;
				SHAPE_V11_CHECK(stroke.style.kind == Draw3UInkStrokeKind::Shape &&
					shape.kind == Kind::Rectangle && shape.legacyKind == LegacyKind::None);
				SHAPE_V11_CHECK(shape.geometry.radius == radii[index]);
				GeometryResult geometry;
				SHAPE_V11_REQUIRE(BuildGeometry(shape, geometry, BuildOptions{ dpiScale }) &&
					geometry.parts.size() == 1);
				SHAPE_V11_CHECK(geometry.parts.front().geometry.radius == radii[index]);
			}
		}
	}

	void TestShapeV11ForeignStyle(ShapeV11Test& test)
	{
		auto canvas = ShapeV11ForeignLine();
		auto& line = std::get<UInkShape>(canvas.content.front());
		line.stroke->lineCap = 2;
		line.stroke->lineJoin = 1;
		line.stroke->declaredEndMarker = 1;
		line.stroke->color.extended = UInkExtendedColor{ UInkColorSpace::ScRgb, { 1.25f,.375f,0 } };
		line.stroke->dashArray = { 2,3,5,7 };
		line.renderOnlyWhenLatest = true;
		const auto decoded = DecodeShapeV11Page(test, canvas);
		SHAPE_V11_REQUIRE(decoded && decoded->content.size() == 1);
		const auto imported = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1);
		const auto& stroke = imported.page->strokes.front();
		SHAPE_V11_REQUIRE(stroke.shape);
		SHAPE_V11_CHECK(imported.counts.lineCap == 1 && imported.counts.lineJoin == 1);
		SHAPE_V11_CHECK(imported.counts.marker == 1 && imported.counts.dashArray == 1);
		SHAPE_V11_CHECK(imported.counts.extendedColor == 1 && imported.counts.latestVisibility == 1);
		SHAPE_V11_CHECK(stroke.shape->kind == Kind::Line && !stroke.renderOnlyWhenLatest);
		SHAPE_V11_CHECK(stroke.shape->style.strokeRgb == line.stroke->color.fallbackRgb);
		SHAPE_V11_CHECK(stroke.shape->style.linePattern == LinePattern::Solid);
		SHAPE_V11_CHECK(stroke.shape->geometry.start == Vec2{ 10,15 } && stroke.shape->geometry.end == Vec2{ 40,50 });
		const auto reexported = ExportShapeV11Page(test, *stroke.shape);
		SHAPE_V11_REQUIRE(reexported && reexported->content.size() == 1);
		SHAPE_V11_REQUIRE(std::holds_alternative<UInkShape>(reexported->content.front()));
		const auto& restored = std::get<UInkShape>(reexported->content.front());
		SHAPE_V11_REQUIRE(restored.stroke);
		SHAPE_V11_CHECK(restored.stroke->lineCap == 0 && !restored.stroke->lineJoin);
		SHAPE_V11_CHECK(restored.stroke->declaredEndMarker == 0 && restored.stroke->dashArray.empty());
		SHAPE_V11_CHECK(!restored.stroke->color.extended && !restored.renderOnlyWhenLatest);
	}

	void TestShapeV11DashMapping(ShapeV11Test& test)
	{
		const std::array<std::vector<float>, 4> patterns{{ {}, { 12,18 }, { 0,9 }, { 12,15,0,15 } }};
		for (size_t i = 0; i < patterns.size(); ++i)
		{
			auto canvas = ShapeV11ForeignLine();
			auto& line = std::get<UInkShape>(canvas.content.front());
			line.stroke->dashArray = patterns[i];
			line.stroke->dashOffset = 4.5f;
			const auto decoded = DecodeShapeV11Page(test, canvas);
			SHAPE_V11_REQUIRE(decoded);
			const auto imported = ImportUInkV11Page(*decoded, {});
			SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
			SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1 && imported.page->strokes.front().shape);
			const auto& style = imported.page->strokes.front().shape->style;
			SHAPE_V11_CHECK(style.linePattern == static_cast<LinePattern>(i));
			SHAPE_V11_CHECK(style.dashOffset == 4.5f && imported.counts.dashArray == 0);
		}
	}

	void TestShapeV11SquareAndOpacity(ShapeV11Test& test)
	{
		auto canvas = ShapeV11ForeignLine();
		auto& shape = std::get<UInkShape>(canvas.content.front());
		shape.declaredShapeType = 3;
		shape.geometry = UInkSquareGeometry{ 20,30,18,0.5f };
		shape.fill.emplace();
		shape.fill->declaredFillType = 7;
		const auto decoded = DecodeShapeV11Page(test, canvas);
		SHAPE_V11_REQUIRE(decoded && decoded->content.size() == 1);
		auto model = *decoded;
		SHAPE_V11_REQUIRE(std::holds_alternative<UInkShape>(model.content.front()));
		auto& square = std::get<UInkShape>(model.content.front());
		SHAPE_V11_REQUIRE(square.stroke && square.fill);
		// 标准部件透明度和填充类型归 L1 计数，直接入口也只静默钳制透明度。
		square.stroke->opacity = 3;
		square.fill->opacity = -2;
		const auto imported = ImportUInkV11Page(model, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1 && imported.page->strokes.front().shape);
		const auto& result = *imported.page->strokes.front().shape;
		SHAPE_V11_CHECK(imported.counts.square == 1 && imported.counts.opacityClamped == 0);
		SHAPE_V11_CHECK(result.kind == Kind::Rectangle && result.style.fill && result.style.stroke);
		SHAPE_V11_CHECK(result.style.strokeOpacity == 1 && result.style.fillOpacity == 0);
		SHAPE_V11_CHECK(result.geometry.end.x - result.geometry.start.x == 18);
		SHAPE_V11_CHECK(result.geometry.end.y - result.geometry.start.y == 18);
		SHAPE_V11_CHECK(ShapeV11Near(ApplyTransform(result.transform, { 0,0 }), { 20,30 }));
	}

	void TestShapeV11GeometryLimit(ShapeV11Test& test, PresetKind preset)
	{
		const auto source = MakeConstructedShape(ShapeV11Construction(preset),
			ShapeV11Style(preset), ShapeV11Guid(0x08).Bytes());
		SHAPE_V11_REQUIRE(source);
		auto canvas = ExportShapeV11Page(test, *source);
		SHAPE_V11_REQUIRE(canvas);
		SHAPE_V11_REQUIRE(std::holds_alternative<UInkShape>(canvas->content.front()));
		auto& shape = std::get<UInkShape>(canvas->content.front());
		SHAPE_V11_REQUIRE(shape.object);
		auto* root = std::get_if<UInkShapeObject>(&*shape.object);
		SHAPE_V11_REQUIRE(root && root->construction);
		if (preset == PresetKind::Arc)
		{
			// 4 倍变换后约 1.7 万段，超过 ArcPoints 上限。文件部件照常可显示。
			SHAPE_V11_REQUIRE(ShapeV11SetField(*root->construction, "end", ShapeV11Point({ 4000020,30 })));
			SHAPE_V11_REQUIRE(ShapeV11SetField(*root->construction, "sweep", ShapeV11Value(6.0f)));
			root->transform = std::array<float, 6>{ 4,0,0,4,0,0 };
		}
		else
		{
			// 构造参数与标准 Shape 部件独立，标准层仍只有夹具中的 4 个点。
			UInkMessagePackValue::Array vertices;
			vertices.reserve(kUInkMaxShapeVertices + 1);
			for (uint32_t i = 0; i <= kUInkMaxShapeVertices; ++i)
				vertices.push_back(ShapeV11Point({ static_cast<float>(i),10.0f }));
			SHAPE_V11_REQUIRE(ShapeV11SetField(*root->construction, "vertices", ShapeV11Value(std::move(vertices))));
		}
		const auto imported = ImportUInkV11Page(*canvas, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_CHECK(imported.counts.geometryLimit == 1);
		SHAPE_V11_REQUIRE(imported.page->operations.size() == 1 && imported.page->strokes.size() == 1);
		SHAPE_V11_REQUIRE(imported.page->strokes.front().shape);
		const auto& result = *imported.page->strokes.front().shape;
		SHAPE_V11_CHECK(result.kind == Kind::Compound && !result.construction);
		SHAPE_V11_CHECK(result.parts.size() == canvas->content.size());
		const auto restored = ExportShapeV11Page(test, result);
		SHAPE_V11_REQUIRE(restored && restored->content.size() == canvas->content.size());
		VerifyShapeV11Points(test, canvas->content.front(), restored->content.front());
	}

	void TestShapeV11ObjectStyleColor(ShapeV11Test& test)
	{
		auto style = ShapeV11Style(PresetKind::Line);
		style.fillRgb = 0x712345;
		const auto source = MakeConstructedShape(ShapeV11Construction(PresetKind::Line),
			style, ShapeV11Guid(0x09).Bytes());
		SHAPE_V11_REQUIRE(source);
		auto canvas = ExportShapeV11Page(test, *source);
		SHAPE_V11_REQUIRE(canvas);
		auto* wire = std::get_if<UInkShape>(&canvas->content.front());
		SHAPE_V11_REQUIRE(wire && wire->object);
		auto* root = std::get_if<UInkShapeObject>(&*wire->object);
		SHAPE_V11_REQUIRE(root && root->style);
		// L1 用 uint64_t 承载解码后的无符号整数。这里验证外部对象层的标准颜色回退。
		const UInkMessagePackValue::Map color{
			{ ShapeV11Value(std::string("fallback")),ShapeV11Value(uint64_t{ 0x336699 }) },
			{ ShapeV11Value(std::string("space")),ShapeV11Value(std::string("scrgb")) },
			{ ShapeV11Value(std::string("components")),ShapeV11Value(UInkMessagePackValue::Array{
				ShapeV11Value(1.25f),ShapeV11Value(.375f),ShapeV11Value(0.f) }) }
		};
		SHAPE_V11_REQUIRE(ShapeV11SetField(*root->style, "strokeColor", ShapeV11Value(color)));
		const auto decoded = DecodeShapeV11Page(test, *canvas);
		SHAPE_V11_REQUIRE(decoded);
		const auto imported = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1 && imported.page->strokes.front().shape);
		const auto& result = *imported.page->strokes.front().shape;
		SHAPE_V11_CHECK(result.construction == source->construction);
		SHAPE_V11_CHECK(result.style.strokeRgb == 0x336699 && imported.counts.extendedColor == 1);
		SHAPE_V11_CHECK(!result.style.fill && result.style.fillRgb == style.fillRgb);
	}

	void TestShapeV11MissingObjectStyle(ShapeV11Test& test)
	{
		auto canvas = ShapeV11ForeignLine();
		auto& line = std::get<UInkShape>(canvas.content.front());
		SHAPE_V11_REQUIRE(line.stroke);
		line.stroke->opacity = .25f;
		line.stroke->width = 2;
		line.stroke->dashArray = { 8,12 };
		UInkShapeObject root;
		root.id = ShapeV11Guid(0x0a);
		root.revision = 1;
		root.partCount = 1;
		root.preset = "line";
		root.construction = UInkMessagePackValue::Map{
			{ ShapeV11Value(std::string("start")),ShapeV11Point({ 10,15 }) },
			{ ShapeV11Value(std::string("end")),ShapeV11Point({ 40,50 }) }
		};
		line.object = std::move(root);
		const auto decoded = DecodeShapeV11Page(test, canvas);
		SHAPE_V11_REQUIRE(decoded);
		const auto imported = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		SHAPE_V11_REQUIRE(imported.page->strokes.size() == 1 && imported.page->strokes.front().shape);
		const auto& result = *imported.page->strokes.front().shape;
		SHAPE_V11_REQUIRE(result.kind == Kind::Line && result.construction);
		SHAPE_V11_CHECK(result.style.stroke && !result.style.fill);
		SHAPE_V11_CHECK(result.style.strokeOpacity == .25f && result.style.width == 2);
		SHAPE_V11_CHECK(result.style.strokeRgb == line.stroke->color.fallbackRgb);
		SHAPE_V11_CHECK(result.style.linePattern == LinePattern::Dashed && imported.counts.dashArray == 0);

		line.stroke->dashArray = { 3,5 };
		line.geometry = UInkLineGeometry{ { { 10,15 },{ 40,15 } } };
		const auto unsupportedWire = DecodeShapeV11Page(test, canvas);
		SHAPE_V11_REQUIRE(unsupportedWire);
		const auto fallback = ImportUInkV11Page(*unsupportedWire, {});
		SHAPE_V11_REQUIRE(fallback.page && fallback.page->strokes.size() == 1 && fallback.page->strokes.front().shape);
		const auto& compound = *fallback.page->strokes.front().shape;
		SHAPE_V11_CHECK(fallback.counts.dashArray == 1);
		SHAPE_V11_REQUIRE(compound.kind == Kind::Compound && !compound.construction && compound.parts.size() == 4);
		for (size_t i = 0; i < compound.parts.size(); ++i)
		{
			const auto& part = compound.parts[i];
			SHAPE_V11_REQUIRE(part.kind == Kind::Polyline && part.style && part.style->stroke);
			const float x = 10 + 8 * static_cast<float>(i);
			SHAPE_V11_CHECK(part.geometry.vertices == std::vector<Vec2>{ { x,15 },{ x+3,15 } });
			SHAPE_V11_CHECK(part.style->linePattern == LinePattern::Solid && part.style->strokeOpacity == .25f);
		}
	}

	void TestShapeV11AuxiliaryFootprintBudget(ShapeV11Test& test)
	{
		ToolSpec tool{ PresetKind::Rectangle, CreationMode::Drag };
		tool.rectangleCornerRadius = 0;
		ShapeStyle style;
		style.stroke = true;
		style.fill = false;
		style.width = 2;
		const auto plain = MakePresetShape(tool, { 30,30 }, { 60,60 }, style, ShapeV11Guid(0x13).Bytes());
		style.auxiliary = Auxiliary::Diagonals;
		const auto withAux = MakePresetShape(tool, { 30,30 }, { 60,60 }, style, ShapeV11Guid(0x14).Bytes());
		SHAPE_V11_REQUIRE(plain && withAux);
		const auto plainPage = ExportShapeV11Page(test, *plain);
		const auto auxPage = ExportShapeV11Page(test, *withAux);
		SHAPE_V11_REQUIRE(plainPage && auxPage && plainPage->content.size() == 1 && auxPage->content.size() == 3);

		// 主体四边与两条辅助对角线均在同一瓦片，两层分别计一次。
		Draw3UInkV11ImportOptions options;
		options.footprintBudget = 8;
		const auto accepted = ImportUInkV11Page(*plainPage, options);
		const auto rejected = ImportUInkV11Page(*auxPage, options);
		SHAPE_V11_REQUIRE(accepted.status == Draw3UInkV11ImportStatus::Success && accepted.page &&
			rejected.status == Draw3UInkV11ImportStatus::Success && rejected.page);
		SHAPE_V11_CHECK(accepted.counts.footprintBudget == 0 && accepted.page->strokes.size() == 1);
		SHAPE_V11_CHECK(rejected.counts.footprintBudget == 1 && rejected.page->strokes.empty());
		options.footprintBudget = 12;
		const auto acceptedAux = ImportUInkV11Page(*auxPage, options);
		SHAPE_V11_REQUIRE(acceptedAux.page && acceptedAux.page->strokes.size() == 1 &&
			acceptedAux.page->strokes.front().shape);
		SHAPE_V11_CHECK(acceptedAux.counts.footprintBudget == 0 &&
			acceptedAux.page->strokes.front().shape->style.auxiliary == Auxiliary::Diagonals);
	}

	void TestShapeV11LargeCircleColdRead(ShapeV11Test& test)
	{
		Construction construction;
		construction.tool.preset = PresetKind::Circle;
		construction.tool.creationMode = CreationMode::CenterRadius;
		construction.end = { 1'000'000,0 };
		const auto source = MakeConstructedShape(construction, ShapeStyle{}, ShapeV11Guid(0x0b).Bytes(), 1);
		SHAPE_V11_REQUIRE(source);
		const auto wire = ExportShapeV11Page(test, *source);
		SHAPE_V11_REQUIRE(wire && wire->content.size() == 1);
		const auto decoded = DecodeShapeV11Page(test, *wire);
		SHAPE_V11_REQUIRE(decoded && std::holds_alternative<UInkShape>(decoded->content.front()));
		const auto& shape = std::get<UInkShape>(decoded->content.front());
		const auto* circle = std::get_if<UInkCircleGeometry>(&shape.geometry);
		SHAPE_V11_REQUIRE(circle && shape.stroke && !shape.fill);
		SHAPE_V11_CHECK(circle->radius == 1'000'000);
		// 放宽足迹预算以独立验证大圆的弧段上限。
		Draw3UInkV11ImportOptions options;
		options.footprintBudget = 1'000'000'000;
		options.footprintItemBudget = 1'000'000'000;
		options.footprintItemMaxTiles = 1'000'000'000;
		options.footprintPageMaxTiles = 1'000'000'000;
		const auto restored = ImportUInkV11Page(*decoded, options);
		SHAPE_V11_REQUIRE(restored.page && restored.page->strokes.size() == 1 && restored.page->strokes.front().shape);
		SHAPE_V11_CHECK(restored.counts.geometryLimit == 0 && restored.page->strokes.front().shape->geometry.radius == circle->radius);
	}

	void TestShapeV11ExportGeometryFallback(ShapeV11Test& test)
	{
		Construction construction;
		construction.tool.preset = PresetKind::Circle;
		construction.tool.creationMode = CreationMode::CenterRadius;
		construction.end = { 20'000,0 };
		ShapeStyle style;
		style.fill = true;
		const auto source = MakeConstructedShape(construction, style, ShapeV11Guid(0x0c).Bytes(), 1);
		SHAPE_V11_REQUIRE(source);
		auto page = ShapeV11Page();
		page.strokes.push_back(ShapeV11Stroke(*source));
		const auto exported = ExportDraw3PageToUInkV11(page, {});
		SHAPE_V11_REQUIRE(exported.canvas && !exported.canvas->content.empty());
		SHAPE_V11_CHECK(exported.status == Draw3UInkExportStatus::Success && exported.counts.geometryLimit == 1);
		const auto decoded = DecodeShapeV11Page(test, *exported.canvas);
		SHAPE_V11_REQUIRE(decoded);
		const auto& first = std::get<UInkShape>(decoded->content.front());
		SHAPE_V11_REQUIRE(first.object && std::holds_alternative<UInkShapeObject>(*first.object));
		const auto& root = std::get<UInkShapeObject>(*first.object);
		SHAPE_V11_CHECK(!root.preset && !root.construction);
		const auto restored = ImportUInkV11Page(*decoded, {});
		SHAPE_V11_REQUIRE(restored.page && restored.page->strokes.size() == 1 && restored.page->strokes.front().shape);
		const auto& shape = *restored.page->strokes.front().shape;
		SHAPE_V11_CHECK(shape.kind == Kind::Compound && shape.parts.size() == decoded->content.size());
		SHAPE_V11_CHECK(restored.counts.geometryLimit == 0 && restored.counts.invalidShapeSkipped == 0);
	}

	void TestShapeV11PageGeometryWork(ShapeV11Test& test)
	{
		auto canvas = ShapeV11ForeignLine();
		const auto appendPolygon = [&](size_t vertices, bool fill = false)
		{
			UInkShape polygon;
			polygon.declaredShapeType = 6;
			polygon.contentId = polygon.undoId = static_cast<uint32_t>(canvas.content.size());
			polygon.stroke.emplace();
			polygon.stroke->width = 2;
			if (fill) polygon.fill.emplace();
			UInkLineGeometry outline;
			outline.points.reserve(vertices);
			for (size_t i = 0; i < vertices; ++i)
			{
				// 顺序采样正圆，16384 个互异点构成有效简单多边形。
				const double angle = 2 * std::numbers::pi * double(i) / double(vertices);
				outline.points.push_back({ static_cast<float>(1'000'000 * std::cos(angle)),
					static_cast<float>(1'000'000 * std::sin(angle)) });
			}
			polygon.geometry = std::move(outline);
			canvas.content.emplace_back(std::move(polygon));
		};
		appendPolygon(4094);
		appendPolygon(16'384, true);
		appendPolygon(4094);
		appendPolygon(16'384);
		appendPolygon(4094, true);
		const auto start = std::chrono::steady_clock::now();
		// 用大额度把画布瓦片预算与此处测试的几何构造预算分开。
		Draw3UInkV11ImportOptions options;
		options.footprintBudget = 2'000'000'000;
		options.footprintItemBudget = 2'000'000'000;
		options.footprintItemMaxTiles = 2'000'000'000;
		options.footprintPageMaxTiles = 2'000'000'000;
		const auto result = ImportUInkV11Page(canvas, options);
		const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
		std::cout << "UInk Draw3 1.1 page geometry budget import: " << elapsed.count() << " ms\n";
		SHAPE_V11_REQUIRE(result.page);
		SHAPE_V11_CHECK(result.status == Draw3UInkV11ImportStatus::Success);
		SHAPE_V11_CHECK(result.counts.geometryLimit == 3 && result.counts.invalidShapeSkipped == 2);
		SHAPE_V11_REQUIRE(result.page->strokes.size() == 4);
		for (size_t i = 1; i <= 2; ++i)
		{
			const auto& shape = result.page->strokes[i].shape;
			SHAPE_V11_REQUIRE(shape && shape->kind == Kind::Polygon);
			SHAPE_V11_CHECK(shape->geometry.vertices.size() == 4094);
		}
		const auto& outline = result.page->strokes.back().shape;
		SHAPE_V11_REQUIRE(outline && outline->kind == Kind::Compound && outline->parts.size() == 2);
		const auto& first = outline->parts[0].geometry.vertices;
		const auto& last = outline->parts[1].geometry.vertices;
		SHAPE_V11_REQUIRE(first.size() == 16'384 && last.size() == 2);
		SHAPE_V11_CHECK(first.back() == last.front() && last.back() == first.front());
	}

#undef SHAPE_V11_REQUIRE
#undef SHAPE_V11_CHECK
}

void RunUInkV11Draw3ShapeTests(int& failures)
{
	for (const auto& fixture : kShapeV11Presets)
	{
		ShapeV11Test test{ fixture.name,failures };
		TestShapeV11Preset(test, fixture);
	}

	const std::array legacy = {
		std::pair{ LegacyKind::SolidLine,"legacy-solid-line" },
		std::pair{ LegacyKind::DashedLine,"legacy-dashed-line" },
		std::pair{ LegacyKind::OutlineRectangle,"legacy-outline-rectangle" },
		std::pair{ LegacyKind::FilledRectangle,"legacy-filled-rectangle" }
	};
	for (const auto& [kind, name] : legacy)
	{
		ShapeV11Test test{ name,failures };
		TestShapeV11Legacy(test, kind, name);
	}

	ShapeV11Test construction{ "construction variants",failures };
	TestShapeV11ConstructionVariants(construction);
	ShapeV11Test transformed{ "transformed solids",failures };
	TestShapeV11TransformedSolids(transformed);
	ShapeV11Test auxiliary{ "rotated auxiliary parts",failures };
	TestShapeV11RotatedAuxiliaryParts(auxiliary);
	ShapeV11Test duplicateExport{ "duplicate export identity",failures };
	TestShapeV11DuplicateExportIdentity(duplicateExport);
	ShapeV11Test compound{ "compound",failures };
	TestShapeV11Compound(compound);
	ShapeV11Test compoundClear{ "compound clear numbering",failures };
	TestShapeV11CompoundClearNumbering(compoundClear);
	const std::array faults = {
		std::pair{ ShapeV11ObjectFault::MissingPart,"missing part" },
		std::pair{ ShapeV11ObjectFault::WrongPartCount,"wrong file part count" },
		std::pair{ ShapeV11ObjectFault::WrongPartOrder,"wrong part order" },
		std::pair{ ShapeV11ObjectFault::OrphanReference,"orphan reference" },
		std::pair{ ShapeV11ObjectFault::DuplicateId,"duplicate object id" },
		std::pair{ ShapeV11ObjectFault::MissingPreset,"missing preset" },
		std::pair{ ShapeV11ObjectFault::UnknownPreset,"unknown preset" },
		std::pair{ ShapeV11ObjectFault::BadConstruction,"bad construction" },
		std::pair{ ShapeV11ObjectFault::RebuildCountMismatch,"rebuild part count mismatch" },
		std::pair{ ShapeV11ObjectFault::DifferentWireGeometry,"rebuild geometry takes precedence" }
	};
	for (const auto& [fault, name] : faults)
	{
		ShapeV11Test test{ name,failures };
		TestShapeV11ObjectFallback(test, fault);
	}

	ShapeV11Test style{ "foreign style",failures };
	TestShapeV11ForeignStyle(style);
	ShapeV11Test foreignRectangles{ "foreign rectangles",failures };
	TestShapeV11ForeignRectangles(foreignRectangles);
	ShapeV11Test objectColor{ "foreign object style color",failures };
	TestShapeV11ObjectStyleColor(objectColor);
	ShapeV11Test missingStyle{ "missing object style",failures };
	TestShapeV11MissingObjectStyle(missingStyle);
	ShapeV11Test auxiliaryFootprint{ "auxiliary footprint budget",failures };
	TestShapeV11AuxiliaryFootprintBudget(auxiliaryFootprint);
	ShapeV11Test largeCircle{ "large circle cold read",failures };
	TestShapeV11LargeCircleColdRead(largeCircle);
	ShapeV11Test exportBudget{ "export geometry fallback",failures };
	TestShapeV11ExportGeometryFallback(exportBudget);
	ShapeV11Test pageGeometryBudget{ "page geometry work budget",failures };
	TestShapeV11PageGeometryWork(pageGeometryBudget);
	ShapeV11Test dashes{ "four dash patterns",failures };
	TestShapeV11DashMapping(dashes);
	ShapeV11Test square{ "square and opacity",failures };
	TestShapeV11SquareAndOpacity(square);
	ShapeV11Test arcLimit{ "arc segment limit",failures };
	TestShapeV11GeometryLimit(arcLimit, PresetKind::Arc);
	ShapeV11Test vertexLimit{ "construction vertex limit",failures };
	TestShapeV11GeometryLimit(vertexLimit, PresetKind::Polyline);
}
