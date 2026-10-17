#include "../inkStrokeModelerTest/draw3/shape_core.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	using namespace draw3::shapes;
	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED shape core line " << line << ": " << expression << '\n';
	}
#define SHAPE_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
	ObjectId Id(uint8_t seed) { ObjectId id{}; id[0] = seed; id[15] = 0xA5; return id; }
	ShapeObject Shape(Kind kind)
	{
		ShapeObject s; s.id = Id(1); s.kind = kind;
		s.geometry.start = { 0,0 }; s.geometry.end = { 100,100 };
		if (kind == Kind::Circle) s.geometry.radius = 50;
		if (kind == Kind::Polyline || kind == Kind::Polygon)
			s.geometry.vertices = { { 0,0 }, { 100,0 }, { 100,100 }, { 50,40 }, { 0,100 } };
		return s;
	}
	double Area(const Triangle& t)
	{
		return std::abs((double(t[1].x) - t[0].x) * (double(t[2].y) - t[0].y) -
			(double(t[1].y) - t[0].y) * (double(t[2].x) - t[0].x)) * .5;
	}
}

int RunShapeCoreTests()
{
	using namespace draw3::shapes;
	int failures = 0;
	ShapeObject line = Shape(Kind::Line);
	GeometryResult geometry;
	SHAPE_CHECK(Validate(line));
	SHAPE_CHECK(BuildGeometry(line, geometry));
	SHAPE_CHECK(geometry.parts.size() == 1 && geometry.parts[0].paths[0].points.size() == 2);
	SHAPE_CHECK(ControlPoints(geometry).size() == 2);
	SHAPE_CHECK(geometry.bounds.left < 0 && geometry.bounds.right > 100);
	SHAPE_CHECK(HitTest(line, geometry, { 50,50 }).kind == HitKind::Stroke);
	SHAPE_CHECK(HitTest(line, geometry, { 50,60 }).kind == HitKind::None);
	const GeometryResult preserved = geometry;
	ShapeObject invalid = line; invalid.geometry.start.x = std::numeric_limits<float>::quiet_NaN();
	std::string error;
	SHAPE_CHECK(!BuildGeometry(invalid, geometry, {}, &error) && !error.empty());
	SHAPE_CHECK(geometry.bounds == preserved.bounds);
	invalid = line; invalid.id = {}; SHAPE_CHECK(!Validate(invalid));
	invalid = line; invalid.style.width = 0; SHAPE_CHECK(!Validate(invalid));
	invalid = line; invalid.style.fill = true; SHAPE_CHECK(!Validate(invalid));
	invalid = line; invalid.transform.m11 = 0; SHAPE_CHECK(!Validate(invalid));
	invalid = line; invalid.creationMode = static_cast<CreationMode>(99); SHAPE_CHECK(!Validate(invalid));
	SHAPE_CHECK(!BuildGeometry(line, geometry, { 1,.5f }));
	SHAPE_CHECK(!BuildGeometry(line, geometry, { 0,.25f }));
	invalid = line; invalid.legacyKind = LegacyKind::FilledRectangle; SHAPE_CHECK(!Validate(invalid));

	for (const LegacyKind kind : { LegacyKind::SolidLine, LegacyKind::DashedLine,
		LegacyKind::OutlineRectangle, LegacyKind::FilledRectangle })
	{
		const auto legacy = MakeLegacyShape(kind, { 3,5 }, { 90,60 }, 4, 0x123456, .5f, Id(4));
		SHAPE_CHECK(legacy && legacy->legacyKind == kind && legacy->id == Id(4));
		SHAPE_CHECK(legacy && BuildGeometry(*legacy, geometry));
		SHAPE_CHECK(legacy && (legacy->style.fill == (kind == LegacyKind::FilledRectangle)));
		if (legacy && legacy->kind == Kind::Rectangle) SHAPE_CHECK(legacy->geometry.radius == 4);
	}
	const auto narrowLegacy = MakeLegacyShape(LegacyKind::FilledRectangle, { 10,10 }, { 6,30 }, 4, 0, 1, Id(4));
	SHAPE_CHECK(narrowLegacy && narrowLegacy->geometry.radius == 4);
	SHAPE_CHECK(narrowLegacy && BuildGeometry(*narrowLegacy, geometry));
	SHAPE_CHECK(narrowLegacy && HitTest(*narrowLegacy, geometry, { 6.1f,10.1f }).kind == HitKind::None);
	const auto legacyRectangle = MakeLegacyShape(LegacyKind::FilledRectangle, { 0,0 }, { 40,40 }, 4, 0, 1, Id(4));
	SHAPE_CHECK(legacyRectangle && BuildGeometry(*legacyRectangle, geometry, { 2,.25f }));
	SHAPE_CHECK(geometry.parts[0].geometry.radius == 8);
	SHAPE_CHECK(legacyRectangle && HitTest(*legacyRectangle, geometry, { 1,1 }).kind == HitKind::None);
	const auto dot = MakeLegacyShape(LegacyKind::SolidLine, { 3,5 }, { 3,5 }, 4, 0, 1, Id(4));
	SHAPE_CHECK(dot && BuildGeometry(*dot, geometry));
	SHAPE_CHECK(dot && HitTest(*dot, geometry, { 4,5 }).kind == HitKind::Stroke);

	ShapeObject polygon = Shape(Kind::Polygon); polygon.style.fill = true;
	SHAPE_CHECK(BuildGeometry(polygon, geometry));
	SHAPE_CHECK(geometry.parts[0].triangles.size() == 3);
	double area = 0; for (const auto& triangle : geometry.parts[0].triangles) area += Area(triangle);
	SHAPE_CHECK(std::abs(area - 7000) < .01);
	SHAPE_CHECK(HitTest(polygon, geometry, { 50,20 }).kind == HitKind::Fill);
	SHAPE_CHECK(HitTest(polygon, geometry, { 50,80 }).kind == HitKind::None);
	std::reverse(polygon.geometry.vertices.begin(), polygon.geometry.vertices.end());
	SHAPE_CHECK(BuildGeometry(polygon, geometry));
	area = 0; for (const auto& triangle : geometry.parts[0].triangles) area += Area(triangle);
	SHAPE_CHECK(std::abs(area - 7000) < .01);
	invalid = polygon; invalid.geometry.vertices = { { 0,0 }, { 100,100 }, { 0,100 }, { 100,0 } };
	SHAPE_CHECK(!Validate(invalid));
	invalid.geometry.vertices = { { 0,0 }, { 100,0 }, { 50,0 }, { 50,100 }, { 0,100 } };
	SHAPE_CHECK(!Validate(invalid));
	invalid.geometry.vertices = { { 0,0 }, { 0,0 }, { 100,100 } }; SHAPE_CHECK(!Validate(invalid));
	polygon.geometry.vertices = { { 0,0 }, { 50,0 }, { 100,0 }, { 100,100 }, { 0,100 } };
	SHAPE_CHECK(BuildGeometry(polygon, geometry));
	area = 0; for (const auto& triangle : geometry.parts[0].triangles) area += Area(triangle);
	SHAPE_CHECK(std::abs(area - 10000) < .01);

	ShapeObject circle = Shape(Kind::Circle); circle.style.fill = true;
	circle.creationMode = CreationMode::CenterRadius;
	SHAPE_CHECK(BuildGeometry(circle, geometry, { 2,.25f }));
	const auto& circlePath = geometry.parts[0].paths[0];
	for (size_t i = 1; i < circlePath.points.size(); ++i)
	{
		const float chord = std::hypot(circlePath.points[i].x - circlePath.points[i - 1].x,
			circlePath.points[i].y - circlePath.points[i - 1].y);
		SHAPE_CHECK((50 - std::sqrt(2500 - chord * chord * .25f)) * 2 <= .2501f);
		SHAPE_CHECK(circlePath.lengths[i] >= circlePath.lengths[i - 1]);
	}
	SHAPE_CHECK(ControlPoints(geometry).size() == 2);
	SHAPE_CHECK(HitTest(circle, geometry, { 0,0 }).kind == HitKind::Fill);
	invalid = circle; invalid.transform.m11 = 2; SHAPE_CHECK(!Validate(invalid));
	circle.transform = { 0,2,-2,0,30,40 };
	SHAPE_CHECK(BuildGeometry(circle, geometry));
	SHAPE_CHECK(HitTest(circle, geometry, { 30,40 }).kind == HitKind::Fill);
	ShapeObject ellipse = Shape(Kind::Ellipse); ellipse.geometry.end = { 100,30 };
	ellipse.transform = { 2,1,.5f,3,10,20 };
	SHAPE_CHECK(BuildGeometry(ellipse, geometry));
	SHAPE_CHECK(ControlPoints(geometry).size() == 3);
	const auto& ellipsePath = geometry.parts[0].paths[0];
	for (size_t i = 1; i < ellipsePath.points.size(); ++i)
	{
		const double angle = (double(i) - .5) * 6.2831853071795864769 / (ellipsePath.points.size() - 1);
		const Vec2 actual = ApplyTransform(ellipse.transform, { 100 * static_cast<float>(std::cos(angle)), 30 * static_cast<float>(std::sin(angle)) });
		const Vec2 midpoint{ (ellipsePath.points[i].x + ellipsePath.points[i - 1].x) * .5f,
			(ellipsePath.points[i].y + ellipsePath.points[i - 1].y) * .5f };
		SHAPE_CHECK(std::hypot(actual.x - midpoint.x, actual.y - midpoint.y) <= .2501f);
	}
	invalid = ellipse; invalid.geometry.end.y = -1; SHAPE_CHECK(!Validate(invalid));
	ShapeObject rounded = Shape(Kind::Rectangle); rounded.geometry.radius = 20; rounded.style.fill = true;
	SHAPE_CHECK(BuildGeometry(rounded, geometry));
	SHAPE_CHECK(HitTest(rounded, geometry, { 1,1 }).kind == HitKind::None);
	SHAPE_CHECK(HitTest(rounded, geometry, { 50,50 }).kind == HitKind::Fill);
	ShapeObject zeroFill = Shape(Kind::Rectangle); zeroFill.style.stroke = false; zeroFill.style.fill = true;
	zeroFill.geometry.end.x = 0;
	SHAPE_CHECK(BuildGeometry(zeroFill, geometry));
	SHAPE_CHECK(HitTest(zeroFill, geometry, { 0,50 }).kind == HitKind::None);

	ShapeObject path = Shape(Kind::Polyline); path.geometry.vertices = { { 0,0 }, { 30,0 }, { 30,100 } };
	path.style.width = 2; path.style.linePattern = LinePattern::Dashed;
	SHAPE_CHECK(BuildGeometry(path, geometry));
	SHAPE_CHECK(geometry.parts[0].paths[0].lengths[1] == 30);
	SHAPE_CHECK(HitTest(path, geometry, { 4,0 }).kind == HitKind::Stroke);
	SHAPE_CHECK(HitTest(path, geometry, { 14,0 }).kind == HitKind::None);
	SHAPE_CHECK(HitTest(path, geometry, { 30,14 }).kind == HitKind::Stroke);
	SHAPE_CHECK(HitTest(path, geometry, { 30,4 }).kind == HitKind::None);
	SHAPE_CHECK(IsDashVisible(4, path.style));
	SHAPE_CHECK(!IsDashVisible(14, path.style));
	path.style.linePattern = LinePattern::Dotted;
	SHAPE_CHECK(IsDashVisible(6, path.style) && !IsDashVisible(3, path.style));
	path.style.linePattern = LinePattern::DashDot;
	SHAPE_CHECK(IsDashVisible(18, path.style) && !IsDashVisible(13, path.style));
	path.style.dashOffset = 4; SHAPE_CHECK(IsDashVisible(-4, path.style));

	ShapeObject compound = Shape(Kind::Compound);
	compound.parts = { { 10, Kind::Rectangle, rounded.geometry, {}, rounded.style },
		{ 20, Kind::Circle, Shape(Kind::Circle).geometry, { 1,0,0,1,50,50 }, std::nullopt } };
	SHAPE_CHECK(BuildGeometry(compound, geometry));
	SHAPE_CHECK(geometry.parts.size() == 2 && geometry.parts[1].partId == 20);
	SHAPE_CHECK(ControlPoints(geometry).size() == 6);
	SHAPE_CHECK(HitTest(compound, geometry, { 100,50 }).partId == 20);
	invalid = compound; invalid.parts[1].id = 10; SHAPE_CHECK(!Validate(invalid));
	invalid = compound; invalid.parts[1].kind = Kind::Compound; SHAPE_CHECK(!Validate(invalid));
	invalid = compound; invalid.transform.m11 = 2; SHAPE_CHECK(!Validate(invalid));
	const Vec2 expected = ApplyTransform(compound.transform, ApplyTransform(compound.parts[1].transform, { 7,8 }));
	SHAPE_CHECK(ApplyTransform(ComposeTransforms(compound.transform, compound.parts[1].transform), { 7,8 }) == expected);

	RenderData render;
	SHAPE_CHECK(BuildRenderData(compound, render));
	SHAPE_CHECK(render.batches.size() == 3 && render.batches[0].fill && !render.batches[1].fill);
	SHAPE_CHECK(render.batches[0].primitives[0].kind == static_cast<uint32_t>(PrimitiveKind::RoundedRectangle));
	SHAPE_CHECK(render.batches[2].primitives[0].kind == static_cast<uint32_t>(PrimitiveKind::Ellipse));
	// 细长椭圆端部不能用一阶距离近似描边，填充和圆仍保留解析路径。
	ShapeObject slender = Shape(Kind::Ellipse); slender.geometry.end = { 100,1 };
	slender.style.width = 2; slender.style.fill = true;
	SHAPE_CHECK(BuildRenderData(slender, render));
	SHAPE_CHECK(render.batches.size() == 2 && render.batches[0].fill && !render.batches[1].fill);
	SHAPE_CHECK(render.batches[0].primitives[0].kind == static_cast<uint32_t>(PrimitiveKind::Ellipse));
	SHAPE_CHECK(std::all_of(render.batches[1].primitives.begin(), render.batches[1].primitives.end(),
		[](const GpuPrimitive& p) { return p.kind == static_cast<uint32_t>(PrimitiveKind::Segment); }));
	SHAPE_CHECK(BuildGeometry(slender, geometry));
	SHAPE_CHECK(HitTest(slender, geometry, { 99,0 }).kind == HitKind::Stroke);
	SHAPE_CHECK(BuildRenderData(path, render));
	SHAPE_CHECK(render.batches[0].primitives.size() == 2 && render.batches[0].primitives[1].pathStart == 30);
	SHAPE_CHECK(BuildRenderData(ellipse, render));
	SHAPE_CHECK(render.batches[0].primitives[0].kind == static_cast<uint32_t>(PrimitiveKind::Segment));
	polygon.style.stroke = false;
	SHAPE_CHECK(BuildRenderData(polygon, render));
	SHAPE_CHECK(!render.batches[0].primitives.empty());
	uint32_t boundaryFlags = 0; for (const auto& primitive : render.batches[0].primitives) boundaryFlags |= primitive.flags;
	SHAPE_CHECK(boundaryFlags != 0);

	Registry registry;
	const Kind custom = static_cast<Kind>(100);
	SHAPE_CHECK(registry.Register(custom, [](const Geometry& g, const BuildOptions&, GeometryPart& p, std::string&)
		{ Path path; path.points = { g.start,g.end }; p.paths.push_back(std::move(path)); return true; }));
	SHAPE_CHECK(!registry.Register(Kind::Line, {}));
	registry.Freeze(); SHAPE_CHECK(!registry.Register(static_cast<Kind>(101), registry.Find(custom) ? *registry.Find(custom) : GeometryBuilder{}));
	ShapeObject customShape = line; customShape.kind = custom;
	SHAPE_CHECK(BuildGeometry(customShape, geometry, {}, &error, registry));
	SHAPE_CHECK(!Validate(customShape));
	SHAPE_CHECK(DefaultRegistry().IsFrozen());
	GeometryCache cache;
	const auto first = cache.Get(line), again = cache.Get(line);
	SHAPE_CHECK(first && first == again);
	line.revision++; line.geometry.end = { 300,300 };
	const auto second = cache.Get(line);
	SHAPE_CHECK(second && second != first && second->bounds.right > first->bounds.right);
	const auto scaled = cache.Get(line, { 2,.25f }); SHAPE_CHECK(scaled && scaled != second);
	const Bounds dirty = cache.Invalidate(line.id, first->bounds);
	SHAPE_CHECK(dirty.valid && dirty.right >= second->bounds.right);
	SHAPE_CHECK(cache.Get(line) != scaled);

	CreationTransaction transaction;
	SHAPE_CHECK(transaction.Preview(line));
	invalid = line; invalid.id = Id(2); SHAPE_CHECK(!transaction.Preview(invalid));
	SHAPE_CHECK(transaction.Candidate()->id == line.id);
	const auto create = transaction.Commit();
	SHAPE_CHECK(create && !transaction.Candidate());
	std::optional<ShapeObject> target;
	SHAPE_CHECK(create && ApplyCommand(*create, target));
	SHAPE_CHECK(target == line);
	SHAPE_CHECK(create && !ApplyCommand(*create, target));
	SHAPE_CHECK(create && ApplyCommand(InverseCommand(*create), target) && !target);
	SHAPE_CHECK(create && ApplyCommand(*create, target));
	ShapeObject changed = line; changed.style.strokeRgb = 0x123456; changed.revision++;
	ShapeCommand edit{ CommandKind::ChangeStyle, line.id, line.revision, line, changed };
	SHAPE_CHECK(ApplyCommand(edit, target) && target == changed);
	SHAPE_CHECK(ApplyCommand(InverseCommand(edit), target) && target == line);
	const auto beforeFailure = target;
	edit.expectedRevision++; SHAPE_CHECK(!ApplyCommand(edit, target) && target == beforeFailure);
	edit.expectedRevision--; edit.after->style.width = 0;
	SHAPE_CHECK(!ApplyCommand(edit, target) && target == beforeFailure);
	edit.after = line; edit.after->style.width += 1;
	SHAPE_CHECK(!ApplyCommand(edit, target) && target == beforeFailure);
	SHAPE_CHECK(transaction.Preview(line)); transaction.Cancel();
	SHAPE_CHECK(!transaction.Candidate() && !transaction.Commit());
	// 预设与创建模式、样式分别校验；工厂必须为每种模板生成同一套渲染/查询几何。
	for (uint32_t value = 0; value <= static_cast<uint32_t>(PresetKind::RegularPolygon); ++value)
	{
		ToolSpec tool; tool.preset = static_cast<PresetKind>(value);
		if (tool.preset == PresetKind::Circle || tool.preset == PresetKind::RegularPolygon) tool.creationMode = CreationMode::CenterRadius;
		const bool open = tool.preset == PresetKind::Line || tool.preset == PresetKind::Arrow;
		for (uint32_t pattern = 0; pattern < 4; ++pattern) for (uint32_t fill = 0; fill < (open ? 1u : 3u); ++fill)
		{
			ShapeStyle style; style.width = 5; style.strokeRgb = 0x224466; style.fillRgb = 0x113355;
			style.strokeOpacity = .5f; style.fillOpacity = .3f; style.linePattern = static_cast<LinePattern>(pattern);
			style.stroke = fill != 1; style.fill = fill != 0;
			const auto preset = MakePresetShape(tool, { 20,30 }, { 140,110 }, style, Id(9), 42, &error);
			SHAPE_CHECK(preset && preset->id == Id(9) && preset->revision == 42 && preset->style == style);
			SHAPE_CHECK(preset && preset->construction == (Construction{ tool,{ 20,30 },{ 140,110 } }));
			if (!preset) continue;
			SHAPE_CHECK(BuildGeometry(*preset, geometry) && geometry.bounds.valid);
			SHAPE_CHECK(BuildRenderData(*preset, render) && !render.batches.empty());
			invalid = *preset;
			invalid.geometry.radius += 1; SHAPE_CHECK(!Validate(invalid));
			invalid = *preset; invalid.creationMode = CreationMode::Points; SHAPE_CHECK(!Validate(invalid));
			if (tool.preset == PresetKind::Arrow)
			{
				SHAPE_CHECK(preset->parts.size() == 2 && preset->parts[1].style->linePattern == LinePattern::Solid);
				SHAPE_CHECK(render.batches.size() == 1 && render.batches[0].primitives.size() == 3);
				SHAPE_CHECK(render.batches[0].style.linePattern == style.linePattern);
				SHAPE_CHECK(render.batches[0].primitives[0].flags == 0 && render.batches[0].primitives[1].flags == kPrimitiveForceSolid);
				const auto& tip = preset->parts[1].geometry.vertices;
				SHAPE_CHECK(HitTest(*preset, geometry, { (tip[0].x + tip[1].x) * .5f,(tip[0].y + tip[1].y) * .5f }).partId == 2);
				invalid = *preset; invalid.parts[1].geometry.vertices[0].x += 5; SHAPE_CHECK(!Validate(invalid));
			}
		}
	}
	const Vec2 corners[] = { { 0,0 },{ 120,0 },{ 120,80 },{ 0,80 } };
	for (const PresetKind kind : { PresetKind::Rectangle,PresetKind::RoundedRectangle,PresetKind::Ellipse,
		PresetKind::Triangle,PresetKind::Parallelogram,PresetKind::Rhombus,PresetKind::Trapezoid })
	{
		const ToolSpec tool{ kind,CreationMode::Drag,3 };
		const auto reference = MakePresetShape(tool, corners[0], corners[2], {}, Id(9));
		SHAPE_CHECK(reference);
		for (size_t i = 0; i < 4; ++i)
		{
			const auto reverse = MakePresetShape(tool, corners[i], corners[(i + 2) % 4], {}, Id(9));
			SHAPE_CHECK(reverse && reference && reverse->geometry == reference->geometry);
		}
		SHAPE_CHECK(!MakePresetShape(tool, { 0,0 }, { 100,0 }, {}, Id(9)));
	}
	for (uint32_t sides = 3; sides <= 12; ++sides)
	{
		const ToolSpec tool{ PresetKind::RegularPolygon,CreationMode::CenterRadius,sides };
		const auto regular = MakePresetShape(tool, { 25,50 }, { 140,87 }, {}, Id(10));
		SHAPE_CHECK(regular && regular->geometry.vertices.size() == sides && regular->geometry.vertices.front() == (Vec2{ 140,87 }));
		if (!regular) continue;
		const auto& vertices = regular->geometry.vertices;
		const float edge = std::hypot(vertices[0].x - vertices[1].x, vertices[0].y - vertices[1].y);
		for (size_t i = 1; i < vertices.size(); ++i)
			SHAPE_CHECK(std::abs(std::hypot(vertices[i].x - vertices[(i + 1) % sides].x, vertices[i].y - vertices[(i + 1) % sides].y) - edge) < .001f);
		SHAPE_CHECK(BuildGeometry(*regular, geometry));
		const auto controls = ControlPoints(geometry);
		SHAPE_CHECK(controls.size() == sides + 1 && controls.back().kind == ControlKind::Center && controls.back().position == (Vec2{ 25,50 }));
		invalid = *regular; invalid.transform.m11 = 2; SHAPE_CHECK(!Validate(invalid));
	}
	// 已捕获的矩形圆角是画布单位，样式和当前显示比例不能再次改变轮廓。
	ToolSpec rectangleTool{ PresetKind::Rectangle,CreationMode::Drag,3 }; rectangleTool.rectangleCornerRadius = 8;
	const auto compatibleRectangle = MakeLegacyShape(LegacyKind::OutlineRectangle,{ 0,0 },{ 40,40 },4,0,1,Id(10));
	GeometryResult compatibleGeometry;
	SHAPE_CHECK(compatibleRectangle && BuildGeometry(*compatibleRectangle,compatibleGeometry,{ 2,.25f }));
	for (uint32_t pattern = 0; pattern < 4; ++pattern) for (uint32_t fill = 0; fill < 3; ++fill)
	{
		ShapeStyle style; style.linePattern = static_cast<LinePattern>(pattern); style.stroke = fill != 1; style.fill = fill != 0;
		const auto styledRectangle = MakePresetShape(rectangleTool,{ 0,0 },{ 40,40 },style,Id(10));
		SHAPE_CHECK(styledRectangle && styledRectangle->geometry.radius == 8 && BuildGeometry(*styledRectangle,geometry,{ 2,.25f }));
		SHAPE_CHECK(geometry.parts[0].geometry == compatibleGeometry.parts[0].geometry &&
			geometry.parts[0].paths[0].points == compatibleGeometry.parts[0].paths[0].points);
	}
	const auto narrowPresetRectangle = MakePresetShape(rectangleTool,{ 0,0 },{ 6,40 },{},Id(10));
	SHAPE_CHECK(narrowPresetRectangle && narrowPresetRectangle->geometry.radius == 3 &&
		narrowPresetRectangle->construction->tool.rectangleCornerRadius == 8);
	rectangleTool.rectangleCornerRadius = -1;
	SHAPE_CHECK(!MakePresetShape(rectangleTool,{ 0,0 },{ 40,40 },{},Id(10)));
	rectangleTool.rectangleCornerRadius = std::numeric_limits<float>::quiet_NaN();
	SHAPE_CHECK(!MakePresetShape(rectangleTool,{ 0,0 },{ 40,40 },{},Id(10)));
	const auto centerCircle = MakePresetShape({ PresetKind::Circle,CreationMode::CenterRadius,3 }, { 0,0 }, { 3,4 }, {}, Id(10));
	SHAPE_CHECK(centerCircle && centerCircle->geometry.radius == 5 && centerCircle->geometry.start == (Vec2{ 0,0 }));
	for (float x : { -1.0f,1.0f }) for (float y : { -1.0f,1.0f })
	{
		const auto boxCircle = MakePresetShape({ PresetKind::Circle,CreationMode::Drag,3 }, { 10,10 }, { 10 + 20 * x,10 + 10 * y }, {}, Id(10));
		SHAPE_CHECK(boxCircle && boxCircle->geometry.radius == 10 && boxCircle->geometry.start == (Vec2{ 10 + 10 * x,10 + 10 * y }));
	}
	SHAPE_CHECK(!MakePresetShape({}, { 0,0 }, { 0,0 }, {}, Id(9), 1, &error) && !error.empty());
	SHAPE_CHECK(!MakePresetShape({}, { 0,0 }, { 20,30 }, {}, {}, 1, &error));
	SHAPE_CHECK(!MakePresetShape({}, { 0,0 }, { 20,30 }, {}, Id(9), 0, &error));
	SHAPE_CHECK(!MakePresetShape({ static_cast<PresetKind>(99),CreationMode::Drag,3 }, { 0,0 }, { 20,30 }, {}, Id(9)));
	SHAPE_CHECK(!MakePresetShape({ PresetKind::RegularPolygon,CreationMode::CenterRadius,2 }, { 0,0 }, { 20,30 }, {}, Id(9)));
	SHAPE_CHECK(!MakePresetShape({ PresetKind::RegularPolygon,CreationMode::CenterRadius,13 }, { 0,0 }, { 20,30 }, {}, Id(9)));
	SHAPE_CHECK(!MakePresetShape({ PresetKind::Circle,CreationMode::Points,3 }, { 0,0 }, { 20,30 }, {}, Id(9)));
	SHAPE_CHECK(!MakePresetShape({}, { std::numeric_limits<float>::quiet_NaN(),0 }, { 20,30 }, {}, Id(9)));
	std::cout << "Shape core tests: " << failures << " failure(s)\n";
	return failures;
}

#ifdef SHAPE_CORE_TEST_MAIN
int main() { return RunShapeCoreTests(); }
#endif
