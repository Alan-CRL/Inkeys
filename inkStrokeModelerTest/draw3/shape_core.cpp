#include "shape_core.h"
#include "shape_interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace draw3::shapes
{
namespace
{
	constexpr double kPi = 3.14159265358979323846;
	constexpr size_t kMaxVertices = 16384;
	bool Finite(float v) { return std::isfinite(v) && std::abs(v) <= 1.0e9f; }
	bool Finite(Vec2 p) { return Finite(p.x) && Finite(p.y); }
	double Cross(Vec2 a, Vec2 b, Vec2 c)
	{
		return (double(b.x) - a.x) * (double(c.y) - a.y) -
			(double(b.y) - a.y) * (double(c.x) - a.x);
	}
	float Distance(Vec2 a, Vec2 b) { return static_cast<float>(std::hypot(double(a.x) - b.x, double(a.y) - b.y)); }
	double Determinant(const Transform& t) { return double(t.m11) * t.m22 - double(t.m12) * t.m21; }
	float MaxScale(const Transform& t)
	{
		// 最大奇异值控制细分误差；剪切变换不能只取两根轴的长度。
		const double a = double(t.m11) * t.m11 + double(t.m12) * t.m12;
		const double b = double(t.m21) * t.m21 + double(t.m22) * t.m22;
		const double c = double(t.m11) * t.m21 + double(t.m12) * t.m22;
		return static_cast<float>(std::sqrt((a + b + std::hypot(a - b, 2 * c)) * 0.5));
	}
	bool Similarity(const Transform& t)
	{
		const double x = double(t.m11) * t.m11 + double(t.m12) * t.m12;
		const double y = double(t.m21) * t.m21 + double(t.m22) * t.m22;
		const double dot = double(t.m11) * t.m21 + double(t.m12) * t.m22;
		return std::abs(x - y) <= 1e-5 * std::max(x, y) && std::abs(dot) <= 1e-5 * std::max(x, y);
	}
	bool ValidTransform(const Transform& t)
	{
		return Finite(t.m11) && Finite(t.m12) && Finite(t.m21) && Finite(t.m22) &&
			Finite(t.dx) && Finite(t.dy) && std::abs(Determinant(t)) > 1e-12;
	}
	bool ValidStyle(const ShapeStyle& s)
	{
		return (s.stroke || s.fill) && s.strokeRgb <= 0xFFFFFF && s.fillRgb <= 0xFFFFFF &&
			Finite(s.strokeOpacity) && s.strokeOpacity >= 0 && s.strokeOpacity <= 1 &&
			Finite(s.fillOpacity) && s.fillOpacity >= 0 && s.fillOpacity <= 1 &&
			Finite(s.width) && s.width > 0 && Finite(s.dashOffset) &&
			static_cast<uint32_t>(s.linePattern) <= static_cast<uint32_t>(LinePattern::DashDot);
	}
	#include "shape_presets.inl"
	bool ExpandPreset(const Construction& c, const ShapeStyle& style, ShapeObject& out, std::string& error)
	{
		const auto& tool = c.tool;
		if (static_cast<uint32_t>(tool.preset) > static_cast<uint32_t>(PresetKind::Pyramid) ||
			tool.sides < 3 || tool.sides > 12 || !Finite(tool.rectangleCornerRadius) || tool.rectangleCornerRadius < 0 ||
			!Finite(c.start) || !Finite(c.end) || (c.legacyArrow && tool.preset != PresetKind::Arrow))
		{ error = "invalid construction parameters"; return false; }
		if (tool.preset >= PresetKind::Polyline) return ExpandAdvanced(c, style, out, error);
		if (!std::holds_alternative<std::monostate>(c.parameters) && !std::holds_alternative<VertexConstruction>(c.parameters))
		{ error = "unexpected planar parameters"; return false; }
		const bool center = tool.creationMode == CreationMode::CenterRadius;
		if (tool.preset == PresetKind::Circle ? !center && tool.creationMode != CreationMode::Drag :
			tool.preset == PresetKind::RegularPolygon ? !center : tool.creationMode != CreationMode::Drag)
		{ error = "unsupported preset creation mode"; return false; }
		out.creationMode = tool.creationMode;
		const Vec2 start = c.start, end = c.end;
		const float length = Distance(start, end);
		if (tool.preset == PresetKind::Line || tool.preset == PresetKind::Arrow ||
			tool.preset == PresetKind::RegularPolygon || (tool.preset == PresetKind::Circle && center))
		{
			if (!(length > 0)) { error = "empty construction"; return false; }
		}
		const float l = std::min(start.x, end.x), r = std::max(start.x, end.x);
		const float t = std::min(start.y, end.y), b = std::max(start.y, end.y);
		const float w = r - l, h = b - t, cx = l + w * .5f, cy = t + h * .5f;
		if (tool.preset != PresetKind::Line && tool.preset != PresetKind::Arrow &&
			tool.preset != PresetKind::Circle && tool.preset != PresetKind::RegularPolygon && !(w > 0 && h > 0))
		{ error = "empty construction"; return false; }
		switch (tool.preset)
		{
		case PresetKind::Line:
			out.kind = Kind::Line; out.geometry.start = start; out.geometry.end = end; break;
		case PresetKind::Arrow:
		{
			if (!style.stroke || style.fill) { error = "open arrow requires stroke only"; return false; }
			out.kind = Kind::Compound;
			ShapePart shaft; shaft.id = 1; shaft.kind = Kind::Line;
			shaft.geometry.start = start; shaft.geometry.end = end;
			// 新箭头只随线宽改变头部；v2旧文件显式保留原规则。
			const float head = c.legacyArrow ? std::min(length * .4f, std::max(length * .2f, style.width * 3)) : style.width * 3;
			const Vec2 direction{ (end.x - start.x) / length, (end.y - start.y) / length };
			const Vec2 base{ end.x - direction.x * head, end.y - direction.y * head };
			ShapePart tip; tip.id = 2; tip.kind = Kind::Polyline; tip.style = style;
			tip.style->linePattern = LinePattern::Solid;
			tip.geometry.vertices = { { base.x - direction.y * head * .5f, base.y + direction.x * head * .5f },
				end, { base.x + direction.y * head * .5f, base.y - direction.x * head * .5f } };
			out.parts = { std::move(shaft), std::move(tip) }; break;
		}
		case PresetKind::Rectangle:
		case PresetKind::RoundedRectangle:
			out.kind = Kind::Rectangle; out.geometry.start = { l,t }; out.geometry.end = { r,b };
			// 普通矩形切换填充/线型仍保留旧 4 DIP 轮廓；圆角参数已在 Down 转换一次。
			out.geometry.radius = tool.preset == PresetKind::RoundedRectangle ? std::min(w, h) * .2f :
				std::min(tool.rectangleCornerRadius, std::min(w, h) * .5f);
			break;
		case PresetKind::Circle:
		{
			out.kind = Kind::Circle;
			const float side = std::max(w, h);
			if (!center && !(side > 0)) { error = "empty construction"; return false; }
			out.geometry.radius = center ? length : side * .5f;
			// 外接法固定落笔角，拖动端点投影到较长边确定的正方形角上。
			out.geometry.start = center ? start : Vec2{ start.x + (end.x < start.x ? -1 : 1) * side * .5f,
				start.y + (end.y < start.y ? -1 : 1) * side * .5f };
			break;
		}
		case PresetKind::Ellipse:
			out.kind = Kind::Ellipse; out.geometry.start = { cx,cy }; out.geometry.end = { w * .5f,h * .5f }; break;
		case PresetKind::Triangle:
			out.kind = Kind::Polygon; out.geometry.vertices = { { cx,t }, { r,b }, { l,b } }; break;
		case PresetKind::Parallelogram:
			out.kind = Kind::Polygon; out.geometry.vertices = { { l + w * .25f,t }, { r,t }, { r - w * .25f,b }, { l,b } }; break;
		case PresetKind::Rhombus:
			out.kind = Kind::Polygon; out.geometry.vertices = { { cx,t }, { r,cy }, { cx,b }, { l,cy } }; break;
		case PresetKind::Trapezoid:
			out.kind = Kind::Polygon; out.geometry.vertices = { { l + w * .25f,t }, { r - w * .25f,t }, { r,b }, { l,b } }; break;
		case PresetKind::RegularPolygon:
			out.kind = Kind::Polygon; out.geometry.vertices.reserve(tool.sides);
			out.geometry.vertices.push_back(end);
			for (uint32_t i = 1; i < tool.sides; ++i)
			{
				const double angle = 2 * kPi * i / tool.sides, x = double(end.x) - start.x, y = double(end.y) - start.y;
				out.geometry.vertices.push_back({ static_cast<float>(start.x + x * std::cos(angle) - y * std::sin(angle)),
					static_cast<float>(start.y + x * std::sin(angle) + y * std::cos(angle)) });
			}
			break;
		}
		if (const auto* vertices = std::get_if<VertexConstruction>(&c.parameters))
		{
			if (tool.preset != PresetKind::Triangle && tool.preset != PresetKind::Parallelogram &&
				tool.preset != PresetKind::Rhombus && tool.preset != PresetKind::Trapezoid)
			{ error = "unexpected vertex parameters"; return false; }
			out.geometry.vertices = vertices->vertices;
			if (!ValidPlanarVertices(tool.preset, vertices->vertices))
			{ error = "planar constraint violated"; return false; }
		}
		return true;
	}
	bool SameGeometry(const Geometry& a, const Geometry& b)
	{
		// 序列化参数跨架构重算时允许浮点尾差，但不能容许可编辑参数与显示轮廓漂移。
		auto scalar = [](float x, float y) { return std::abs(x - y) <= 4 * std::numeric_limits<float>::epsilon() * std::max({ 1.0f,std::abs(x),std::abs(y) }); };
		auto point = [&](Vec2 x, Vec2 y) { return scalar(x.x, y.x) && scalar(x.y, y.y); };
		if (!point(a.start, b.start) || !point(a.end, b.end) || !scalar(a.radius, b.radius) || a.vertices.size() != b.vertices.size()) return false;
		for (size_t i = 0; i < a.vertices.size(); ++i) if (!point(a.vertices[i], b.vertices[i])) return false;
		return true;
	}
	bool OnSegment(Vec2 a, Vec2 b, Vec2 p)
	{
		return Cross(a, b, p) == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
			p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
	}
	bool Intersects(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
	{
		const double ac = Cross(a, b, c), ad = Cross(a, b, d), ca = Cross(c, d, a), cb = Cross(c, d, b);
		return ((ac > 0 && ad < 0) || (ac < 0 && ad > 0)) &&
			((ca > 0 && cb < 0) || (ca < 0 && cb > 0)) ||
			(ac == 0 && OnSegment(a, b, c)) || (ad == 0 && OnSegment(a, b, d)) ||
			(ca == 0 && OnSegment(c, d, a)) || (cb == 0 && OnSegment(c, d, b));
	}
	bool SimplePolygon(const std::vector<Vec2>& points)
	{
		if (points.size() < 3 || points.size() > kMaxVertices) return false;
		double area = 0;
		for (size_t i = 0; i < points.size(); ++i)
		{
			const size_t next = (i + 1) % points.size();
			if (!Finite(points[i]) || points[i] == points[next]) return false;
			area += double(points[i].x) * points[next].y - double(points[next].x) * points[i].y;
			// 相邻边允许共线延续，但不允许折返重叠。
			const Vec2 previous = points[(i + points.size() - 1) % points.size()];
			if (Cross(previous, points[i], points[next]) == 0 &&
				(double(previous.x) - points[i].x) * (double(points[next].x) - points[i].x) +
				(double(previous.y) - points[i].y) * (double(points[next].y) - points[i].y) > 0) return false;
			for (size_t j = i + 1; j < points.size(); ++j)
			{
				const size_t jNext = (j + 1) % points.size();
				if (next == j || jNext == i) continue;
				if (Intersects(points[i], points[next], points[j], points[jNext])) return false;
			}
		}
		return area != 0;
	}
	bool ValidGeometry(Kind kind, const Geometry& g, const ShapeStyle& style, std::string& error)
	{
		if (!Finite(g.start) || !Finite(g.end) || !Finite(g.radius) || g.radius < 0 || g.vertices.size() > kMaxVertices ||
			!std::all_of(g.vertices.begin(), g.vertices.end(), [](Vec2 p) { return Finite(p); }))
		{ error = "invalid geometry coordinates"; return false; }
		if ((kind == Kind::Line || kind == Kind::Polyline) && style.fill)
		{ error = "open geometry cannot be filled"; return false; }
		if (kind == Kind::Ellipse && (g.end.x < 0 || g.end.y < 0))
		{ error = "negative ellipse semiaxis"; return false; }
		if (kind == Kind::Polyline && (g.vertices.size() < 2 ||
			!std::all_of(g.vertices.begin(), g.vertices.end(), [](Vec2 p) { return Finite(p); })))
		{ error = "invalid polyline"; return false; }
		if (kind == Kind::Polygon && !SimplePolygon(g.vertices))
		{ error = "polygon must be simple and have nonzero area"; return false; }
		return true;
	}
	void Include(Bounds& b, Vec2 p)
	{
		if (!b.valid) b = { p.x, p.y, p.x, p.y, true };
		else { b.left = std::min(b.left, p.x); b.right = std::max(b.right, p.x); b.top = std::min(b.top, p.y); b.bottom = std::max(b.bottom, p.y); }
	}
	bool InTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c, double sign)
	{
		return Cross(a, b, p) * sign >= 0 && Cross(b, c, p) * sign >= 0 && Cross(c, a, p) * sign >= 0;
	}
	bool Tessellate(const std::vector<Vec2>& vertices, std::vector<Triangle>& triangles)
	{
		if (vertices.size() < 3) return true;
		std::vector<size_t> indices(vertices.size());
		std::iota(indices.begin(), indices.end(), size_t(0));
		double area = 0;
		for (size_t i = 0; i < vertices.size(); ++i)
			area += double(vertices[i].x) * vertices[(i + 1) % vertices.size()].y -
				double(vertices[(i + 1) % vertices.size()].x) * vertices[i].y;
		if (area == 0) return true;
		const double sign = area > 0 ? 1 : -1;
		// 耳切只处理简单多边形；移除共线顶点，避免凹角被错误填平。
		while (indices.size() > 3)
		{
			bool removed = false;
			for (size_t i = 0; i < indices.size(); ++i)
			{
				const Vec2 a = vertices[indices[(i + indices.size() - 1) % indices.size()]];
				const Vec2 b = vertices[indices[i]], c = vertices[indices[(i + 1) % indices.size()]];
				const double cross = Cross(a, b, c) * sign;
				if (cross < 0) continue;
				bool contains = false;
				if (cross > 0) for (size_t j = 0; j < indices.size(); ++j)
				{
					if (j == i || j == (i + 1) % indices.size() || j == (i + indices.size() - 1) % indices.size()) continue;
					if (InTriangle(vertices[indices[j]], a, b, c, sign)) { contains = true; break; }
				}
				if (contains) continue;
				if (cross > 0) triangles.push_back({ a, b, c });
				indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i)); removed = true; break;
			}
			if (!removed) return false;
		}
		if (indices.size() == 3 && Cross(vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]) != 0)
			triangles.push_back({ vertices[indices[0]], vertices[indices[1]], vertices[indices[2]] });
		return true;
	}
	bool BuiltinBuilder(Kind kind, const Geometry& g, const BuildOptions& options, GeometryPart& out, std::string& error)
	{
		Path path;
		auto control = [&](ControlKind k, Vec2 p) { out.controls.push_back({ 0, static_cast<uint32_t>(out.controls.size()), k, p }); };
		auto arc = [&](Vec2 center, Vec2 axes, double begin, double sweep, bool first)
		{
			const double radius = std::max(axes.x, axes.y);
			const double tolerance = options.maxErrorPixels / options.displayScale;
			const double angle = radius > tolerance ? 2 * std::acos(std::max(-1.0, 1 - tolerance / radius)) : kPi * 0.5;
			const double count = std::max(1.0, std::ceil(std::abs(sweep) / std::max(angle, 1e-8)));
			if (count > kMaxVertices) return false;
			const size_t segments = static_cast<size_t>(count);
			for (size_t i = first ? 0 : 1; i <= segments; ++i)
			{
				const double a = begin + sweep * double(i) / segments;
				path.points.push_back({ center.x + axes.x * static_cast<float>(std::cos(a)), center.y + axes.y * static_cast<float>(std::sin(a)) });
			}
			return true;
		};
		if (kind == Kind::Line)
		{ path.points = { g.start, g.end }; control(ControlKind::Endpoint, g.start); control(ControlKind::Endpoint, g.end); }
		else if (kind == Kind::Polyline || kind == Kind::Polygon)
		{
			path.points = g.vertices; path.closed = kind == Kind::Polygon;
			for (Vec2 p : g.vertices) control(ControlKind::Vertex, p);
		}
		else if (kind == Kind::Circle || kind == Kind::Ellipse)
		{
			const Vec2 axes = kind == Kind::Circle ? Vec2{ g.radius, g.radius } : g.end;
			if (!arc(g.start, axes, 0, kPi * 2, true)) { error = "curve subdivision limit exceeded"; return false; }
			path.closed = true; path.points.pop_back();
			control(ControlKind::Center, g.start);
			control(ControlKind::Radius, { g.start.x + axes.x, g.start.y });
			if (kind == Kind::Ellipse) control(ControlKind::Radius, { g.start.x, g.start.y + axes.y });
		}
		else if (kind == Kind::Rectangle)
		{
			const float l = std::min(g.start.x, g.end.x), r = std::max(g.start.x, g.end.x);
			const float t = std::min(g.start.y, g.end.y), b = std::max(g.start.y, g.end.y);
			const float radius = std::min({ g.radius, (r - l) * 0.5f, (b - t) * 0.5f });
			if (radius > 0)
			{
				if (!arc({ r - radius, t + radius }, { radius, radius }, -kPi * .5, kPi * .5, true) ||
					!arc({ r - radius, b - radius }, { radius, radius }, 0, kPi * .5, true) ||
					!arc({ l + radius, b - radius }, { radius, radius }, kPi * .5, kPi * .5, true) ||
					!arc({ l + radius, t + radius }, { radius, radius }, kPi, kPi * .5, true))
				{ error = "curve subdivision limit exceeded"; return false; }
			}
			else path.points = { { l, t }, { r, t }, { r, b }, { l, b } };
			path.closed = true;
			control(ControlKind::Corner, { l,t }); control(ControlKind::Corner, { r,t });
			control(ControlKind::Corner, { r,b }); control(ControlKind::Corner, { l,b });
		}
		else { error = "unsupported geometry builder"; return false; }
		if (path.closed && out.style.fill && !Tessellate(path.points, out.triangles))
		{ error = "polygon tessellation failed"; return false; }
		if (path.closed && !path.points.empty()) path.points.push_back(path.points.front());
		out.paths.push_back(std::move(path));
		return true;
	}
	bool BuildPart(uint32_t partId, Kind kind, const Geometry& g, const Transform& transform,
		const ShapeStyle& style, const BuildOptions& options, const Registry& registry, GeometryPart& out, std::string& error)
	{
		out.partId = partId; out.kind = kind; out.geometry = g; out.transform = transform; out.style = style;
		BuildOptions localOptions = options;
		localOptions.displayScale *= MaxScale(transform);
		if (!Finite(localOptions.displayScale) || localOptions.displayScale <= 0)
		{ error = "invalid transformed display scale"; return false; }
		const auto* builder = registry.Find(kind);
		if (!builder || !(*builder)(g, localOptions, out, error))
		{ if (error.empty()) error = "geometry builder failed"; return false; }
		// 描边使用面积等效缩放；圆的非等比变换在 Validate 阶段拒绝。
		out.style.width *= static_cast<float>(std::sqrt(std::abs(Determinant(transform))));
		if (!Finite(out.style.width) || out.style.width <= 0) { error = "transformed width out of range"; return false; }
		for (auto& path : out.paths)
		{
			path.lengths.clear(); float length = 0;
			for (size_t i = 0; i < path.points.size(); ++i)
			{
				path.points[i] = ApplyTransform(transform, path.points[i]);
				if (!Finite(path.points[i])) { error = "transformed point out of range"; return false; }
				if (i) length += Distance(path.points[i - 1], path.points[i]);
				if (!Finite(length)) { error = "path length out of range"; return false; }
				path.lengths.push_back(length); Include(out.bounds, path.points[i]);
			}
		}
		for (auto& triangle : out.triangles) for (auto& p : triangle)
		{
			p = ApplyTransform(transform, p);
			if (!Finite(p)) { error = "transformed triangle out of range"; return false; }
		}
		for (auto& c : out.controls)
		{
			c.partId = partId; c.position = ApplyTransform(transform, c.position);
			if (!Finite(c.position)) { error = "transformed control out of range"; return false; }
		}
		if (out.bounds.valid)
		{
			const float padding = (style.stroke ? out.style.width * .5f : 0) + 1 + options.maxErrorPixels / options.displayScale;
			out.bounds.left -= padding; out.bounds.top -= padding; out.bounds.right += padding; out.bounds.bottom += padding;
		}
		return true;
	}
	void SetError(std::string* target, const std::string& value) { if (target) *target = value; }
	bool PointInPath(const Path& path, Vec2 p)
	{
		bool inside = false;
		for (size_t i = 1; i < path.points.size(); ++i)
		{
			const Vec2 a = path.points[i - 1], b = path.points[i];
			if (OnSegment(a, b, p)) return true;
			if ((a.y > p.y) != (b.y > p.y) && double(p.x) <
				(double(b.x) - a.x) * (double(p.y) - a.y) / (double(b.y) - a.y) + a.x) inside = !inside;
		}
		return inside;
	}
	// 返回中心线上到最近可见线段的距离，圆头覆盖由调用方与法向距离共同计算。
	float DashDistance(float distance, const ShapeStyle& style)
	{
		if (style.linePattern == LinePattern::Solid) return 0;
		const float w = style.width;
		const float period = (style.linePattern == LinePattern::Dashed ? 10 : style.linePattern == LinePattern::Dotted ? 3 : 14) * w;
		float x = std::fmod(distance + style.dashOffset, period); if (x < 0) x += period;
		if (style.linePattern == LinePattern::Dotted) return std::min(x, period - x);
		if (x <= 4 * w) return 0;
		if (style.linePattern == LinePattern::Dashed) return std::min(x - 4 * w, period - x);
		return std::min({ x - 4 * w, std::abs(x - 9 * w), period - x });
	}
}

Vec2 ApplyTransform(const Transform& t, Vec2 p) noexcept
{ return { t.m11 * p.x + t.m21 * p.y + t.dx, t.m12 * p.x + t.m22 * p.y + t.dy }; }
Transform ComposeTransforms(const Transform& a, const Transform& b) noexcept
{
	const Vec2 translation = ApplyTransform(a, { b.dx, b.dy });
	return { a.m11 * b.m11 + a.m21 * b.m12, a.m12 * b.m11 + a.m22 * b.m12,
		a.m11 * b.m21 + a.m21 * b.m22, a.m12 * b.m21 + a.m22 * b.m22, translation.x, translation.y };
}
bool IsZeroId(const ObjectId& id) noexcept
{ return std::all_of(id.begin(), id.end(), [](uint8_t b) { return b == 0; }); }
Bounds UnionBounds(Bounds a, Bounds b) noexcept
{
	if (!a.valid) return b; if (!b.valid) return a;
	return { std::min(a.left,b.left), std::min(a.top,b.top), std::max(a.right,b.right), std::max(a.bottom,b.bottom), true };
}
Registry::Registry()
{
	for (const Kind kind : { Kind::Line, Kind::Rectangle, Kind::Circle, Kind::Ellipse, Kind::Polyline, Kind::Polygon })
		Register(kind, [kind](const Geometry& g, const BuildOptions& options, GeometryPart& out, std::string& error)
			{ return BuiltinBuilder(kind, g, options, out, error); });
}
bool Registry::Register(Kind kind, GeometryBuilder builder)
{
	if (frozen_ || !builder || kind == Kind::Compound || Find(kind)) return false;
	builders_.emplace_back(kind, std::move(builder)); return true;
}
void Registry::Freeze() noexcept { frozen_ = true; }
bool Registry::IsFrozen() const noexcept { return frozen_; }
const GeometryBuilder* Registry::Find(Kind kind) const noexcept
{
	for (const auto& entry : builders_) if (entry.first == kind) return &entry.second;
	return nullptr;
}
const Registry& DefaultRegistry()
{
	static const Registry registry = [] { Registry value; value.Freeze(); return value; }();
	return registry;
}
ValidationResult Validate(const ShapeObject& object, const Registry& registry)
{
	if (!registry.IsFrozen()) return { "registry must be frozen before use" };
	if (IsZeroId(object.id) || !object.revision) return { "missing stable object identity or revision" };
	if (!ValidTransform(object.transform) || !ValidStyle(object.style) ||
		static_cast<uint32_t>(object.creationMode) > static_cast<uint32_t>(CreationMode::Parameters) ||
		static_cast<uint32_t>(object.legacyKind) > static_cast<uint32_t>(LegacyKind::FilledRectangle)) return { "invalid object transform or style" };
	if (object.construction)
	{
		ShapeObject expected;
		std::string error;
		if (!ExpandPreset(*object.construction, object.style, expected, error)) return { error };
		if (object.legacyKind != LegacyKind::None || object.kind != expected.kind || object.creationMode != expected.creationMode ||
			!SameGeometry(object.geometry, expected.geometry) || object.parts.size() != expected.parts.size())
			return { "construction does not match derived geometry" };
		if ((object.construction->tool.preset == PresetKind::RegularPolygon ||
			object.construction->tool.preset == PresetKind::Cube) && !Similarity(object.transform))
			return { "regular polygon requires uniform orthogonal transform" };
		for (size_t i = 0; i < object.parts.size(); ++i)
		{
			const auto& a = object.parts[i]; const auto& b = expected.parts[i];
			if (a.id != b.id || a.kind != b.kind || !SameGeometry(a.geometry, b.geometry) || a.transform != b.transform || a.style != b.style)
				return { "construction does not match derived parts" };
		}
	}
	if (object.legacyKind != LegacyKind::None)
	{
		const bool line = object.legacyKind == LegacyKind::SolidLine || object.legacyKind == LegacyKind::DashedLine;
		const bool fill = object.legacyKind == LegacyKind::FilledRectangle;
		if (object.kind != (line ? Kind::Line : Kind::Rectangle) || object.style.fill != fill ||
			object.style.stroke == fill || object.transform != Transform{} ||
			object.style.linePattern != (object.legacyKind == LegacyKind::DashedLine ? LinePattern::Dashed : LinePattern::Solid))
			return { "legacy hint does not match geometry or style" };
	}
	auto part = [&](Kind kind, const Geometry& g, const ShapeStyle& style, const Transform& t) -> ValidationResult
	{
		if (!registry.Find(kind)) return { "unregistered geometry kind" };
		if (!ValidStyle(style) || !ValidTransform(t)) return { "invalid part style or transform" };
		if (kind == Kind::Circle && !Similarity(t)) return { "circle requires uniform orthogonal transform" };
		std::string error; if (!ValidGeometry(kind, g, style, error)) return { error };
		return {};
	};
	if (object.kind != Kind::Compound)
	{
		if (!object.parts.empty()) return { "noncompound object has parts" };
		return part(object.kind, object.geometry, object.style, object.transform);
	}
	if (object.parts.empty() || object.parts.size() > kMaxVertices || object.legacyKind != LegacyKind::None) return { "invalid compound parts" };
	std::vector<uint32_t> ids;
	for (const auto& p : object.parts)
	{
		if (!p.id || std::find(ids.begin(), ids.end(), p.id) != ids.end()) return { "duplicate or missing part identity" };
		ids.push_back(p.id);
		if (!ValidTransform(p.transform)) return { "invalid part transform" };
		const auto validation = part(p.kind, p.geometry, p.style.value_or(object.style), ComposeTransforms(object.transform, p.transform));
		if (!validation) return validation;
	}
	return {};
}
bool BuildGeometry(const ShapeObject& object, GeometryResult& result, const BuildOptions& options,
	std::string* error, const Registry& registry)
{
	const auto validation = Validate(object, registry);
	if (!validation) { SetError(error, validation.error); return false; }
	if (!Finite(options.displayScale) || options.displayScale <= 0 || !Finite(options.maxErrorPixels) ||
		options.maxErrorPixels <= 0 || options.maxErrorPixels > 0.25f)
	{ SetError(error, "invalid display scale or curve tolerance"); return false; }
	ShapeObject sampled = object;
	if (object.construction && object.construction->tool.preset >= PresetKind::Arc)
	{
		std::string failure;
		if (!ExpandAdvanced(*object.construction, object.style, sampled, failure,
			options.maxErrorPixels / std::max(1.0f, MaxScale(object.transform))))
		{ SetError(error, failure); return false; }
	}
	GeometryResult next; std::string failure;
	auto build = [&](uint32_t id, Kind kind, const Geometry& g, const Transform& t, const ShapeStyle& style)
	{
		GeometryPart part;
		if (!BuildPart(id, kind, g, t, style, options, registry, part, failure)) return false;
		next.bounds = UnionBounds(next.bounds, part.bounds); next.parts.push_back(std::move(part)); return true;
	};
	if (sampled.kind == Kind::Compound)
	{
		for (const auto& p : sampled.parts) if (!build(p.id, p.kind, p.geometry,
			ComposeTransforms(sampled.transform, p.transform), p.style.value_or(sampled.style)))
		{ SetError(error, failure); return false; }
	}
	else if (sampled.legacyKind == LegacyKind::OutlineRectangle || sampled.legacyKind == LegacyKind::FilledRectangle)
	{
		// 旧矩形的 4 DIP 圆角随显示比例变化；普通图形半径仍是画布像素，不走此兼容处理。
		Geometry compatible = sampled.geometry;
		compatible.radius = std::min({ 4.0f * options.displayScale,
			std::abs(compatible.end.x - compatible.start.x) * .5f,
			std::abs(compatible.end.y - compatible.start.y) * .5f });
		if (!build(0, sampled.kind, compatible, sampled.transform, sampled.style))
		{ SetError(error, failure); return false; }
	}
	else if (!build(0, sampled.kind, sampled.geometry, sampled.transform, sampled.style))
	{ SetError(error, failure); return false; }
	if (sampled.construction && sampled.construction->tool.preset == PresetKind::RegularPolygon)
	{
		auto& controls = next.parts.front().controls;
		controls.push_back({ 0, static_cast<uint32_t>(controls.size()), ControlKind::Center,
			ApplyTransform(sampled.transform, sampled.construction->start) });
	}
	result = std::move(next); SetError(error, {}); return true;
}

bool BuildRenderData(const ShapeObject& object, RenderData& result, const BuildOptions& options,
	std::string* error, const Registry& registry)
{
	GeometryResult geometry;
	if (!BuildGeometry(object, geometry, options, error, registry)) return false;
	RenderData next; next.bounds = geometry.bounds;
	for (const auto& part : geometry.parts)
	{
		const auto& t = part.transform;
		const Vec2 axisX{ t.m11, t.m12 }, axisY{ t.m21, t.m22 };
		const float sx = Distance({}, axisX), sy = Distance({}, axisY);
		const bool orthogonal = std::abs(double(axisX.x) * axisY.x + double(axisX.y) * axisY.y) <= 1e-5 * sx * sy;
		for (const bool fill : { true, false })
		{
			if (fill ? !part.style.fill : !part.style.stroke) continue;
			RenderBatch batch{ part.style, fill, part.partId, {} };
			const bool solid = fill || part.style.linePattern == LinePattern::Solid;
			// 椭圆距离近似在细长端部误差较大；描边复用误差受控轮廓，圆仍使用精确解析距离。
			if (((part.kind == Kind::Ellipse && fill) || part.kind == Kind::Circle) && orthogonal && solid && sx > 0 && sy > 0 &&
				(part.kind == Kind::Circle ? part.geometry.radius > 0 : part.geometry.end.x > 0 && part.geometry.end.y > 0))
			{
				const Vec2 axes = part.kind == Kind::Circle ? Vec2{ part.geometry.radius, part.geometry.radius } : part.geometry.end;
				GpuPrimitive p; p.kind = static_cast<uint32_t>(PrimitiveKind::Ellipse); p.flags = fill ? 1 : 0;
				p.p0 = ApplyTransform(t, part.geometry.start);
				p.p1 = { axisX.x * axes.x, axisX.y * axes.x }; p.p2 = { axisY.x * axes.y, axisY.y * axes.y };
				p.halfWidth = part.style.width * .5f; batch.primitives.push_back(p);
			}
			else if (part.kind == Kind::Rectangle && Similarity(t) && solid)
			{
				const auto& g = part.geometry;
				GpuPrimitive p; p.kind = static_cast<uint32_t>(PrimitiveKind::RoundedRectangle); p.flags = fill ? 1 : 0;
				p.p0 = ApplyTransform(t, { (g.start.x + g.end.x) * .5f, (g.start.y + g.end.y) * .5f });
				p.p1 = { std::abs(g.end.x - g.start.x) * sx * .5f, std::abs(g.end.y - g.start.y) * sy * .5f };
				p.p2 = { axisX.x / sx, axisX.y / sx }; p.radius = std::min({ g.radius * sx, p.p1.x, p.p1.y });
				p.halfWidth = part.style.width * .5f; batch.primitives.push_back(p);
			}
			else if (fill)
			{
				for (const auto& triangle : part.triangles)
				{
					GpuPrimitive p; p.kind = static_cast<uint32_t>(PrimitiveKind::Triangle);
					p.p0 = triangle[0]; p.p1 = triangle[1]; p.p2 = triangle[2];
					for (size_t edge = 0; edge < 3; ++edge)
					{
						const Vec2 a = triangle[edge], b = triangle[(edge + 1) % 3];
						for (const auto& path : part.paths) for (size_t j = 1; j < path.points.size(); ++j)
							if (OnSegment(a, b, path.points[j - 1]) && OnSegment(a, b, path.points[j]))
								p.flags |= 2u << edge;
					}
					batch.primitives.push_back(p);
				}
			}
			else
			{
				for (const auto& path : part.paths) for (size_t i = 1; i < path.points.size(); ++i)
				{
					GpuPrimitive p; p.kind = static_cast<uint32_t>(PrimitiveKind::Segment);
					p.p0 = path.points[i - 1]; p.p1 = path.points[i]; p.pathStart = path.lengths[i - 1];
					p.pathLength = path.lengths[i] - path.lengths[i - 1]; p.halfWidth = part.style.width * .5f;
					batch.primitives.push_back(p);
				}
			}
			if (!batch.primitives.empty())
			{
				// 箭头的两部件保留各自命中/降级语义，GPU 同材质只做一次覆盖并集。
				if (object.construction && object.construction->tool.preset == PresetKind::Arrow && part.partId == 2)
				{
					for (auto& primitive : batch.primitives) primitive.flags |= kPrimitiveForceSolid;
					auto& shaft = next.batches.front().primitives;
					shaft.insert(shaft.end(), batch.primitives.begin(), batch.primitives.end());
				}
				else next.batches.push_back(std::move(batch));
			}
		}
	}
	result = std::move(next); return true;
}
std::vector<ControlPoint> ControlPoints(const GeometryResult& geometry)
{
	std::vector<ControlPoint> controls;
	for (const auto& part : geometry.parts) controls.insert(controls.end(), part.controls.begin(), part.controls.end());
	return controls;
}
bool IsDashVisible(float distance, const ShapeStyle& style) noexcept
{ return Finite(distance) && ValidStyle(style) && DashDistance(distance, style) <= style.width * .5f; }
HitResult HitTest(const ShapeObject& object, const GeometryResult& geometry, Vec2 point, float tolerance)
{
	HitResult none;
	if (!Finite(point) || !Finite(tolerance) || tolerance < 0) return none;
	// 后绘部件先命中；同一部件描边位于填充上方。
	for (auto part = geometry.parts.rbegin(); part != geometry.parts.rend(); ++part)
	{
		if (part->style.stroke && part->style.strokeOpacity > 0) for (const auto& path : part->paths)
			for (size_t i = 1; i < path.points.size(); ++i)
			{
				const Vec2 a = path.points[i - 1], b = path.points[i];
				const double dx = double(b.x) - a.x, dy = double(b.y) - a.y, length2 = dx * dx + dy * dy;
				const double raw = length2 > 0 ? ((double(point.x) - a.x) * dx + (double(point.y) - a.y) * dy) / length2 : 0;
				const float fraction = static_cast<float>(std::clamp(raw, 0.0, 1.0));
				const Vec2 nearest{ a.x + fraction * static_cast<float>(dx), a.y + fraction * static_cast<float>(dy) };
				const float distance = Distance(point, nearest);
				const float along = path.lengths[i - 1] + fraction * (path.lengths[i] - path.lengths[i - 1]);
				const float dash = DashDistance(along, part->style);
				const float radius = part->style.width * .5f + tolerance;
				if (std::hypot(distance, dash) <= radius) return { object.id, part->partId, HitKind::Stroke, distance };
			}
		if (part->style.fill && part->style.fillOpacity > 0 && !part->triangles.empty()) for (const auto& path : part->paths)
			if (path.closed && PointInPath(path, point)) return { object.id, part->partId, HitKind::Fill, 0 };
	}
	return none;
}
std::optional<ShapeObject> MakeConstructedShape(const Construction& construction, const ShapeStyle& style,
	const ObjectId& id, uint64_t revision, std::string* error)
{
	ShapeObject object; object.id = id; object.revision = revision; object.style = style;
	object.construction = construction;
	if (construction.tool.preset == PresetKind::Cube)
	{
		// 正面方形角也是参数真值，避免控制点仍落在未投影的raw矩形内。
		const float side = std::max(std::abs(construction.end.x - construction.start.x),std::abs(construction.end.y - construction.start.y));
		object.construction->end = {construction.start.x + (construction.end.x < construction.start.x ? -side : side),
			construction.start.y + (construction.end.y < construction.start.y ? -side : side)};
	}
	std::string failure;
	if (!ValidStyle(style) || !ExpandPreset(*object.construction, style, object, failure))
	{ SetError(error, failure.empty() ? "invalid construction style" : failure); return std::nullopt; }
	const auto validation = Validate(object);
	if (!validation) { SetError(error, validation.error); return std::nullopt; }
	SetError(error, {}); return object;
}
std::optional<ShapeObject> MakePresetShape(const ToolSpec& tool, Vec2 start, Vec2 end,
	const ShapeStyle& style, const ObjectId& id, uint64_t revision, std::string* error)
{
	return MakeConstructedShape(Construction{ tool,start,end }, style, id, revision, error);
}
std::optional<ShapeObject> MakeLegacyShape(LegacyKind kind, Vec2 start, Vec2 end,
	float width, uint32_t rgb, float opacity, ObjectId id)
{
	if (kind == LegacyKind::None) return std::nullopt;
	ShapeObject shape; shape.id = id; shape.legacyKind = kind;
	shape.kind = kind == LegacyKind::SolidLine || kind == LegacyKind::DashedLine ? Kind::Line : Kind::Rectangle;
	shape.geometry.start = start; shape.geometry.end = end;
	if (shape.kind == Kind::Rectangle)
		// 兼容对象保存名义 4 DIP；BuildGeometry 按显示比例转换并钳制圆角。
		shape.geometry.radius = 4.0f;
	shape.style.strokeRgb = shape.style.fillRgb = rgb; shape.style.width = width;
	shape.style.strokeOpacity = shape.style.fillOpacity = opacity;
	shape.style.stroke = kind != LegacyKind::FilledRectangle; shape.style.fill = kind == LegacyKind::FilledRectangle;
	shape.style.linePattern = kind == LegacyKind::DashedLine ? LinePattern::Dashed : LinePattern::Solid;
	return Validate(shape) ? std::optional<ShapeObject>(std::move(shape)) : std::nullopt;
}
std::shared_ptr<const GeometryResult> GeometryCache::Get(const ShapeObject& shape, const BuildOptions& options, std::string* error)
{
	for (const auto& entry : entries_) if (entry.id == shape.id && entry.revision == shape.revision && entry.options == options)
	{ SetError(error, {}); return entry.geometry; }
	auto geometry = std::make_shared<GeometryResult>();
	if (!BuildGeometry(shape, *geometry, options, error)) return {};
	// 同修订的不同显示比例可以共存，失效时需合并这些误差边界。
	std::erase_if(entries_, [&](const Entry& entry) { return entry.id == shape.id && entry.revision != shape.revision; });
	entries_.push_back({ shape.id, shape.revision, options, geometry }); return geometry;
}
Bounds GeometryCache::Invalidate(const ObjectId& id, Bounds replacement)
{
	for (const auto& entry : entries_) if (entry.id == id) replacement = UnionBounds(replacement, entry.geometry->bounds);
	std::erase_if(entries_, [&](const Entry& entry) { return entry.id == id; }); return replacement;
}
void GeometryCache::Clear() noexcept { entries_.clear(); }
bool ApplyCommand(const ShapeCommand& command, std::optional<ShapeObject>& target, std::string* error)
{
	auto fail = [&](const char* reason) { SetError(error, reason); return false; };
	if (IsZeroId(command.objectId)) return fail("command has no identity");
	if (command.before != target) return fail("command snapshot conflict");
	if (target ? target->id != command.objectId || target->revision != command.expectedRevision : command.expectedRevision != 0)
		return fail("command revision conflict");
	if (command.kind == CommandKind::Create ? command.before.has_value() || !command.after :
		command.kind == CommandKind::Delete ? !command.before || command.after.has_value() : !command.before || !command.after)
		return fail("invalid command snapshots");
	if (static_cast<uint32_t>(command.kind) > static_cast<uint32_t>(CommandKind::Delete)) return fail("invalid command kind");
	if (command.after && (command.after->id != command.objectId || !Validate(*command.after))) return fail("invalid resulting shape");
	if (command.before && !Validate(*command.before)) return fail("invalid previous shape");
	// 逆操作恢复历史 revision，因此只禁止同 revision 修改内容，不要求数字单向增长。
	if (command.before && command.after && command.before->revision == command.after->revision && command.before != command.after)
		return fail("changed shape requires a new revision");
	target = command.after; SetError(error, {}); return true;
}
ShapeCommand InverseCommand(const ShapeCommand& command)
{
	ShapeCommand inverse = command;
	std::swap(inverse.before, inverse.after);
	inverse.expectedRevision = inverse.before ? inverse.before->revision : 0;
	if (inverse.kind == CommandKind::Create) inverse.kind = CommandKind::Delete;
	else if (inverse.kind == CommandKind::Delete) inverse.kind = CommandKind::Create;
	return inverse;
}
bool CreationTransaction::Preview(ShapeObject candidate, std::string* error)
{
	const auto validation = Validate(candidate);
	if (!validation) { SetError(error, validation.error); return false; }
	if (candidate_ && candidate_->id != candidate.id) { SetError(error, "preview identity changed"); return false; }
	candidate_ = std::move(candidate); SetError(error, {}); return true;
}
const std::optional<ShapeObject>& CreationTransaction::Candidate() const noexcept { return candidate_; }
std::optional<ShapeCommand> CreationTransaction::Commit(std::string* error)
{
	if (!candidate_) { SetError(error, "no creation preview"); return std::nullopt; }
	ShapeCommand command{ CommandKind::Create, candidate_->id, 0, {}, candidate_ };
	candidate_.reset(); SetError(error, {}); return command;
}
void CreationTransaction::Cancel() noexcept { candidate_.reset(); }
}

#include "shape_interaction.inl"
