// 在 shape_core.cpp 的私有命名空间中引入，所有预设共用校验与派生路径。
bool ValidPlanarVertices(PresetKind preset, const std::vector<Vec2>& v)
{
	if (v.size() != (preset == PresetKind::Triangle ? 3u : 4u)) return false;
	if (!std::all_of(v.begin(), v.end(), [](Vec2 p) { return Finite(p); })) return false;
	if (preset == PresetKind::Triangle) return true;
	auto near = [](double a, double b) { return std::abs(a - b) < 1e-4 * std::max({ 1.0, std::abs(a), std::abs(b) }); };
	if (preset == PresetKind::Trapezoid) return near(v[0].y, v[1].y) && near(v[2].y, v[3].y);
	if (!near(double(v[0].x) + v[2].x, double(v[1].x) + v[3].x) ||
		!near(double(v[0].y) + v[2].y, double(v[1].y) + v[3].y)) return false;
	if (preset == PresetKind::Rhombus)
		for (size_t i = 1; i < 4; ++i) if (!near(Distance(v[0], v[1]), Distance(v[i], v[(i + 1) % 4]))) return false;
	return true;
}
bool ArcPoints(Vec2 center, Vec2 axes, double start, double sweep, float tolerance,
	std::vector<Vec2>& points, std::string& error)
{
	const double radius = std::max(axes.x, axes.y);
	if (!(radius > 0) || !Finite(axes) || !std::isfinite(sweep) || std::abs(sweep) > 2 * kPi + 1e-5 ||
		std::abs(sweep) < 1e-6 || !(tolerance > 0)) { error = "invalid arc parameters"; return false; }
	const double step = 2 * std::acos(std::clamp(1.0 - double(tolerance) / radius, -1.0, 1.0));
	const double required = std::ceil(std::abs(sweep) / std::max(step, 1e-8));
	if (required > 16000) { error = "curve exceeds precision budget"; return false; }
	const size_t count = std::max<size_t>(2, static_cast<size_t>(required));
	points.reserve(count + 1);
	for (size_t i = 0; i <= count; ++i)
	{
		const double angle = start + sweep * static_cast<double>(i) / count;
		points.push_back({ static_cast<float>(center.x + axes.x * std::cos(angle)),
			static_cast<float>(center.y + axes.y * std::sin(angle)) });
	}
	return true;
}
bool ExpandAdvanced(const Construction& c, const ShapeStyle& style, ShapeObject& out,
	std::string& error, float tolerance = .25f)
{
	const auto p = c.tool.preset;
	const bool points = p == PresetKind::Polyline || p == PresetKind::Polygon;
	if (c.tool.creationMode != (points ? CreationMode::Points : CreationMode::MultiStage) || c.legacyArrow)
	{ error = "unsupported advanced creation mode"; return false; }
	out.creationMode = c.tool.creationMode;
	out.geometry = {}; out.parts.clear();
	if (points)
	{
		const auto* parameters = std::get_if<VertexConstruction>(&c.parameters);
		if (!parameters || parameters->vertices.size() > kMaxVertices ||
			parameters->vertices.size() < (p == PresetKind::Polyline ? 2u : 3u) ||
			(p == PresetKind::Polyline && (!style.stroke || style.fill)))
		{ error = "invalid point construction"; return false; }
		out.kind = p == PresetKind::Polyline ? Kind::Polyline : Kind::Polygon;
		out.geometry.vertices = parameters->vertices;
		for (size_t i = 1; i < out.geometry.vertices.size(); ++i)
			if (out.geometry.vertices[i] == out.geometry.vertices[i - 1]) { error = "duplicate vertex"; return false; }
		return true;
	}
	if (p == PresetKind::Arc || p == PresetKind::Sector)
	{
		const auto* parameters = std::get_if<ArcConstruction>(&c.parameters);
		if (!parameters || !Finite(parameters->sweepRadians) || (p == PresetKind::Arc && (!style.stroke || style.fill)))
		{ error = "invalid arc construction"; return false; }
		out.kind = p == PresetKind::Arc ? Kind::Polyline : Kind::Polygon;
		const float radius = Distance(c.start, c.end);
		if (!ArcPoints(c.start, { radius,radius }, std::atan2(double(c.end.y) - c.start.y, double(c.end.x) - c.start.x),
			parameters->sweepRadians, tolerance, out.geometry.vertices, error)) return false;
		if (p == PresetKind::Sector)
		{
			// 完整圆只保留圆周；首尾重复点会被简单多边形校验拒绝。
			if (std::abs(std::abs(parameters->sweepRadians) - 2 * kPi) < 1e-5)
				out.geometry.vertices.pop_back();
			else out.geometry.vertices.push_back(c.start);
		}
		return true;
	}
	const auto* solid = std::get_if<SolidConstruction>(&c.parameters);
	if (!solid || !Finite(solid->depth) || !Finite(solid->height) || !style.stroke || style.fill)
	{ error = "solid requires valid stroke-only parameters"; return false; }
	out.kind = Kind::Compound;
	auto path = [&](std::vector<Vec2> vertices, bool hidden)
	{
		ShapePart part; part.id = static_cast<uint32_t>(out.parts.size() + 1); part.kind = Kind::Polyline;
		part.geometry.vertices = std::move(vertices);
		if (hidden) { part.style = style; part.style->linePattern = LinePattern::Dashed; }
		out.parts.push_back(std::move(part));
	};
	auto edge = [&](Vec2 a, Vec2 b, bool hidden) { path({ a,b }, hidden); };
	auto shift = [](Vec2 a, Vec2 b) { return Vec2{ a.x + b.x,a.y + b.y }; };
	float l = std::min(c.start.x, c.end.x), r = std::max(c.start.x, c.end.x);
	float t = std::min(c.start.y, c.end.y), b = std::max(c.start.y, c.end.y);
	if (p == PresetKind::Cuboid || p == PresetKind::Cube)
	{
		if (solid->height != 0 || !(Distance({}, solid->depth) > 0)) { error = "empty depth"; return false; }
		if (p == PresetKind::Cube)
		{
			const float side = std::max(r - l, b - t);
			l = c.start.x + std::min(0.0f, c.end.x < c.start.x ? -side : side); r = l + side;
			t = c.start.y + std::min(0.0f, c.end.y < c.start.y ? -side : side); b = t + side;
		}
		if (!(r > l && b > t)) { error = "empty front face"; return false; }
		const Vec2 v[4] = { { l,t },{ r,t },{ r,b },{ l,b } };
		const size_t hidden = solid->depth.x >= 0 ? (solid->depth.y >= 0 ? 0 : 3) : (solid->depth.y >= 0 ? 1 : 2);
		for (size_t i = 0; i < 4; ++i)
		{
			const size_t j = (i + 1) % 4;
			edge(v[i], v[j], false);
			edge(shift(v[i], solid->depth), shift(v[j], solid->depth), i == hidden || j == hidden);
			edge(v[i], shift(v[i], solid->depth), i == hidden);
		}
		return true;
	}
	if (solid->depth != Vec2{} || !(std::abs(solid->height) > 0)) { error = "empty solid height"; return false; }
	const Vec2 height{ 0,solid->height };
	if (p == PresetKind::Cylinder || p == PresetKind::Cone)
	{
		if (!(r > l && b > t)) { error = "empty ellipse base"; return false; }
		const Vec2 center{ (l + r) * .5f,(t + b) * .5f }, axes{ (r - l) * .5f,(b - t) * .5f };
		std::vector<Vec2> front, rear;
		const bool above = solid->height < 0;
		if (!ArcPoints(center, axes, above ? 0 : kPi, kPi, tolerance, front, error) ||
			!ArcPoints(center, axes, above ? kPi : 0, kPi, tolerance, rear, error)) return false;
		path(std::move(front), false); path(std::move(rear), true);
		if (p == PresetKind::Cylinder)
		{
			ShapePart top; top.id = static_cast<uint32_t>(out.parts.size() + 1); top.kind = Kind::Ellipse;
			top.geometry.start = shift(center, height); top.geometry.end = axes; out.parts.push_back(top);
			edge({ l,center.y }, { l,center.y + height.y }, false);
			edge({ r,center.y }, { r,center.y + height.y }, false);
		}
		else
		{
			const Vec2 apex = shift(center, height);
			edge({ l,center.y }, apex, false); edge({ r,center.y }, apex, false);
		}
		return true;
	}
	if (p != PresetKind::Prism && p != PresetKind::Pyramid) { error = "unknown advanced preset"; return false; }
	const Vec2 vector{ c.end.x - c.start.x, 2 * (c.end.y - c.start.y) };
	if (!(Distance({}, vector) > 0)) { error = "empty polygon base"; return false; }
	std::vector<Vec2> base;
	for (uint32_t i = 0; i < c.tool.sides; ++i)
	{
		const double angle = 2 * kPi * i / c.tool.sides;
		base.push_back({ static_cast<float>(c.start.x + vector.x * std::cos(angle) - vector.y * std::sin(angle)),
			static_cast<float>(c.start.y + .5 * (vector.x * std::sin(angle) + vector.y * std::cos(angle))) });
	}
	// 后半底边隐藏，顶面或锥顶沿局部竖直方向投影；轮廓边界由相邻朝向判定。
	const bool above = solid->height < 0;
	std::vector<bool> hidden(base.size());
	for (size_t i = 0; i < base.size(); ++i)
	{
		const auto a = base[i], next = base[(i + 1) % base.size()];
		hidden[i] = above ? next.x > a.x : next.x < a.x;
		edge(a, next, hidden[i]);
		if (p == PresetKind::Prism) edge(shift(a, height), shift(next, height), false);
	}
	for (size_t i = 0; i < base.size(); ++i)
		edge(base[i], p == PresetKind::Prism ? shift(base[i], height) : shift(c.start, height),
			hidden[i] && hidden[(i + base.size() - 1) % base.size()]);
	return true;
}
