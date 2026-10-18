// 共享交互实现由 shape_core.cpp 引入，不依赖窗口或运行期历史栈。
namespace draw3::shapes
{
namespace
{
	Vec2 Add(Vec2 a, Vec2 b) { return { a.x + b.x,a.y + b.y }; }
	Vec2 Sub(Vec2 a, Vec2 b) { return { a.x - b.x,a.y - b.y }; }
	Vec2 Mul(Vec2 a, float value) { return { a.x * value,a.y * value }; }
	Vec2 InversePoint(const Transform& t, Vec2 p)
	{
		const double d = Determinant(t); p = Sub(p, { t.dx,t.dy });
		return { static_cast<float>((t.m22 * p.x - t.m21 * p.y) / d),
			static_cast<float>((t.m11 * p.y - t.m12 * p.x) / d) };
	}
	bool UniformPreset(const ShapeObject& shape)
	{
		return shape.kind == Kind::Circle || (shape.construction &&
			(shape.construction->tool.preset == PresetKind::RegularPolygon || shape.construction->tool.preset == PresetKind::Rhombus || shape.construction->tool.preset == PresetKind::Cube ||
			shape.construction->tool.preset == PresetKind::Arc || shape.construction->tool.preset == PresetKind::Sector));
	}
	ShapeObject PromoteShape(const ShapeObject& before, const BuildOptions& options)
	{
		auto shape = before;
		if (shape.legacyKind == LegacyKind::OutlineRectangle || shape.legacyKind == LegacyKind::FilledRectangle)
			shape.geometry.radius = std::min({ 4.0f * options.displayScale,
				std::abs(shape.geometry.end.x - shape.geometry.start.x) * .5f,
				std::abs(shape.geometry.end.y - shape.geometry.start.y) * .5f });
		shape.legacyKind = LegacyKind::None; return shape;
	}
	Bounds LocalShapeBounds(const ShapeObject& shape, const BuildOptions& options)
	{
		auto local = shape; local.transform = {}; GeometryResult geometry; Bounds result;
		if (BuildGeometry(local, geometry, options)) for (const auto& part : geometry.parts)
			for (const auto& path : part.paths) for (auto point : path.points) Include(result, point);
		return result;
	}
	std::optional<ShapeObject> RebuildEdited(ShapeObject shape, uint64_t revision, std::string* error)
	{
		shape.revision = revision;
		if (shape.construction)
		{
			auto result = MakeConstructedShape(*shape.construction, shape.style, shape.id, revision, error);
			if (!result) return std::nullopt;
			result->transform = shape.transform;
			if (!Validate(*result)) { SetError(error, "invalid edited transform"); return std::nullopt; }
			return result;
		}
		const auto validation = Validate(shape);
		if (!validation) { SetError(error, validation.error); return std::nullopt; }
		SetError(error, {}); return shape;
	}
	void ScaleGeometry(Kind kind, Geometry& g, Vec2 anchor, float sx, float sy)
	{
		auto map = [&](Vec2 v) { return Vec2{ anchor.x + (v.x - anchor.x) * sx,anchor.y + (v.y - anchor.y) * sy }; };
		g.start = map(g.start);
		if (kind == Kind::Ellipse) g.end = { g.end.x * sx,g.end.y * sy };
		else if (kind != Kind::Circle) g.end = map(g.end);
		g.radius *= std::sqrt(sx * sy);
		for (auto& v : g.vertices) v = map(v);
	}
}
ToolCapabilities GetToolCapabilities(PresetKind preset) noexcept
{
	ToolCapabilities result;
	result.enabled = preset <= PresetKind::Pyramid;
	result.points = preset == PresetKind::Polyline || preset == PresetKind::Polygon;
	result.multiStage = preset >= PresetKind::Polyline && result.enabled;
	result.fill = result.enabled && preset != PresetKind::Line && preset != PresetKind::Arrow &&
		preset != PresetKind::Polyline && preset != PresetKind::Arc && preset < PresetKind::Cuboid;
	result.regularSides = preset == PresetKind::RegularPolygon || preset == PresetKind::Prism || preset == PresetKind::Pyramid;
	return result;
}
CreationMode DefaultCreationMode(PresetKind preset) noexcept
{
	if (preset == PresetKind::Polyline || preset == PresetKind::Polygon) return CreationMode::Points;
	if (preset >= PresetKind::Arc) return CreationMode::MultiStage;
	if (preset == PresetKind::Circle || preset == PresetKind::RegularPolygon) return CreationMode::CenterRadius;
	return CreationMode::Drag;
}
std::vector<EditHandle> BuildEditHandles(const ShapeObject& shape, const BuildOptions& options)
{
	if (!Validate(shape)) return {};
	std::vector<EditHandle> result;
	auto add = [&](uint32_t id, EditHandleKind kind, Vec2 local) { result.push_back({ id,kind,ApplyTransform(shape.transform, local) }); };
	const auto p = shape.construction ? shape.construction->tool.preset : PresetKind::Line;
	const auto& g = shape.geometry; bool frame = true, rotate = true;
	if (shape.kind == Kind::Line || (shape.construction && p == PresetKind::Arrow))
	{
		add(1000, EditHandleKind::Endpoint, shape.construction ? shape.construction->start : g.start);
		add(1001, EditHandleKind::Endpoint, shape.construction ? shape.construction->end : g.end); frame = false;
	}
	else if (shape.construction && (p == PresetKind::Arc || p == PresetKind::Sector))
	{
		const auto& c = *shape.construction; const auto& arc = std::get<ArcConstruction>(c.parameters); const auto v = Sub(c.end, c.start);
		auto radial = [&](float angle) { return Add(c.start, { v.x * std::cos(angle) - v.y * std::sin(angle),v.x * std::sin(angle) + v.y * std::cos(angle) }); };
		add(100, EditHandleKind::Center, c.start); add(101, EditHandleKind::Radius, radial(arc.sweepRadians * .5f));
		add(102, EditHandleKind::Angle, c.end); add(103, EditHandleKind::Angle, radial(arc.sweepRadians)); frame = false;
	}
	else if (shape.kind == Kind::Circle)
	{
		add(100, EditHandleKind::Center, g.start); add(101, EditHandleKind::Radius, Add(g.start, { g.radius,0 })); frame = false; rotate = false;
	}
	else if (shape.kind == Kind::Ellipse)
	{
		add(100, EditHandleKind::Center, g.start); add(102, EditHandleKind::Axis, Add(g.start, { g.end.x,0 }));
		add(103, EditHandleKind::Axis, Add(g.start, { 0,g.end.y })); frame = false;
	}
	else if (shape.construction && p == PresetKind::RegularPolygon)
	{
		add(100, EditHandleKind::Center, shape.construction->start); add(1000, EditHandleKind::Vertex, shape.construction->end); frame = false;
	}
	else if (shape.kind == Kind::Polyline || shape.kind == Kind::Polygon)
	{
		for (size_t i = 0; i < g.vertices.size(); ++i) add(1000 + static_cast<uint32_t>(i), EditHandleKind::Vertex, g.vertices[i]);
		if (shape.kind == Kind::Polyline) frame = false;
	}
	else if (shape.construction && p >= PresetKind::Cuboid)
	{
		const auto& c = *shape.construction; const auto& solid = std::get<SolidConstruction>(c.parameters);
		add(1000, EditHandleKind::Endpoint, c.start); add(1001, EditHandleKind::Endpoint, c.end);
		if (p == PresetKind::Cuboid || p == PresetKind::Cube) add(104, EditHandleKind::Depth, Add(c.end, solid.depth));
		else
		{
			const Vec2 base = p == PresetKind::Prism || p == PresetKind::Pyramid ? c.start : Mul(Add(c.start, c.end), .5f);
			add(105, EditHandleKind::Height, Add(base, { 0,solid.height }));
		}
	}
	const auto bounds = LocalShapeBounds(shape, options);
	if (bounds.valid)
	{
		const float l = bounds.left, r = bounds.right, t = bounds.top, b = bounds.bottom;
		const Vec2 v[] = { { l,t },{ (l + r) * .5f,t },{ r,t },{ r,(t + b) * .5f },{ r,b },{ (l + r) * .5f,b },{ l,b },{ l,(t + b) * .5f } };
		if (frame) for (uint32_t i = 0; i < 8; ++i)
		{
			const auto canvas = ApplyTransform(shape.transform, v[i]);
			if (std::none_of(result.begin(), result.end(), [&](const EditHandle& h) { return Distance(h.position, canvas) < .5f; }))
				result.push_back({ 10 + i,EditHandleKind::Scale,canvas });
		}
		if (rotate)
		{
			auto top = ApplyTransform(shape.transform, { (l + r) * .5f,t });
			const float axis = std::hypot(shape.transform.m21, shape.transform.m22);
			top.x -= shape.transform.m21 / axis * 40 * options.displayScale; top.y -= shape.transform.m22 / axis * 40 * options.displayScale;
			result.push_back({ 1,EditHandleKind::Rotate,top });
		}
	}
	return result;
}
std::optional<ShapeObject> EditShape(const ShapeObject& before, uint32_t handleId,
	Vec2 down, Vec2 current, uint64_t revision, const BuildOptions& options, std::string* error)
{
	auto fail = [&](const char* reason) -> std::optional<ShapeObject> { SetError(error, reason); return std::nullopt; };
	if (!Validate(before) || !Finite(down) || !Finite(current) || revision <= before.revision) return fail("invalid edit identity or pointer");
	auto shape = PromoteShape(before, options);
	const auto localDown = InversePoint(before.transform, down), localCurrent = InversePoint(before.transform, current);
	const auto delta = Sub(localCurrent, localDown); const auto handles = BuildEditHandles(before, options);
	const auto found = std::find_if(handles.begin(), handles.end(), [&](const EditHandle& h) { return h.id == handleId; });
	if (handleId != 0 && found == handles.end()) return fail("unknown edit handle");
	if (handleId == 0 || handleId == 100)
	{
		shape.transform.dx += current.x - down.x; shape.transform.dy += current.y - down.y;
	}
	else if (handleId == 1)
	{
		const auto bounds = LocalShapeBounds(shape, options);
		const Vec2 center = ApplyTransform(shape.transform, { (bounds.left + bounds.right) * .5f,(bounds.top + bounds.bottom) * .5f });
		if (Distance(center, down) < 1e-6 || Distance(center, current) < 1e-6) return fail("undefined rotation");
		const double angle = std::atan2(double(current.y) - center.y, double(current.x) - center.x) - std::atan2(double(down.y) - center.y, double(down.x) - center.x);
		Transform rotate; rotate.m11 = rotate.m22 = static_cast<float>(std::cos(angle)); rotate.m12 = static_cast<float>(std::sin(angle)); rotate.m21 = -rotate.m12;
		const auto rotated = ApplyTransform(rotate, center); rotate.dx = center.x - rotated.x; rotate.dy = center.y - rotated.y;
		shape.transform = ComposeTransforms(rotate, shape.transform);
	}
	else if (handleId >= 10 && handleId < 18)
	{
		const auto bounds = LocalShapeBounds(shape, options); const uint32_t index = handleId - 10;
		const Vec2 positions[] = { { bounds.left,bounds.top },{ (bounds.left + bounds.right) * .5f,bounds.top },{ bounds.right,bounds.top },
			{ bounds.right,(bounds.top + bounds.bottom) * .5f },{ bounds.right,bounds.bottom },{ (bounds.left + bounds.right) * .5f,bounds.bottom },
			{ bounds.left,bounds.bottom },{ bounds.left,(bounds.top + bounds.bottom) * .5f } };
		const auto moving = positions[index], anchor = positions[(index + 4) % 8];
		float sx = index == 1 || index == 5 ? 1 : (moving.x + delta.x - anchor.x) / (moving.x - anchor.x);
		float sy = index == 3 || index == 7 ? 1 : (moving.y + delta.y - anchor.y) / (moving.y - anchor.y);
		if (UniformPreset(shape))
		{
			const float scale = index % 2 ? (index == 1 || index == 5 ? sy : sx) : ((sx - 1) * (sx - 1) > (sy - 1) * (sy - 1) ? sx : sy);
			sx = sy = scale;
		}
		if (!(sx > 1e-6f && sy > 1e-6f) || !Finite(sx) || !Finite(sy)) return fail("degenerate scale");
		if (shape.construction)
		{
			auto& c = *shape.construction;
			auto map = [&](Vec2 point) { return Vec2{ anchor.x + (point.x - anchor.x) * sx,anchor.y + (point.y - anchor.y) * sy }; };
			c.start = map(c.start); c.end = map(c.end);
			if (auto* vertices = std::get_if<VertexConstruction>(&c.parameters)) for (auto& point : vertices->vertices) point = map(point);
			if (auto* solid = std::get_if<SolidConstruction>(&c.parameters)) { solid->depth.x *= sx; solid->depth.y *= sy; solid->height *= sy; }
			c.tool.rectangleCornerRadius *= std::sqrt(sx * sy);
		}
		else if (shape.kind == Kind::Compound)
		{
			// 未带模板的复合对象只变换几何；用等效比例补偿描边，保持画布线宽。
			const Transform scale{ sx,0,0,sy,anchor.x * (1 - sx),anchor.y * (1 - sy) };
			shape.transform = ComposeTransforms(shape.transform, scale); shape.style.width /= std::sqrt(sx * sy);
			for (auto& part : shape.parts) if (part.style) part.style->width /= std::sqrt(sx * sy);
		}
		else ScaleGeometry(shape.kind, shape.geometry, anchor, sx, sy);
	}
	else if (shape.construction)
	{
		auto& c = *shape.construction; const auto p = c.tool.preset;
		if (p == PresetKind::Circle && handleId == 101)
		{
			const Vec2 pointer = Add(InversePoint(shape.transform, found->position), delta);
			c.tool.creationMode = CreationMode::CenterRadius; c.start = shape.geometry.start; c.end = pointer;
		}
		else if (p == PresetKind::Ellipse && (handleId == 102 || handleId == 103))
		{
			const auto center = shape.geometry.start; auto axes = shape.geometry.end;
			if (handleId == 102) axes.x += delta.x; else axes.y += delta.y;
			if (!(axes.x > 0 && axes.y > 0)) return fail("degenerate ellipse axis");
			c.start = Sub(center, axes); c.end = Add(center, axes);
		}
		else if ((p == PresetKind::Arc || p == PresetKind::Sector) && handleId >= 101 && handleId <= 103)
		{
			auto& arc = std::get<ArcConstruction>(c.parameters); const auto point = Add(InversePoint(shape.transform, found->position), delta);
			const float radius = Distance(c.start, c.end);
			if (handleId == 101)
			{
				const float target = Distance(c.start, point); if (!(target > 0)) return fail("empty arc radius");
				c.end = Add(c.start, Mul(Sub(c.end, c.start), target / radius));
			}
			else
			{
				if (Distance(c.start, point) < 1e-6) return fail("undefined arc angle");
				const float initial = std::atan2(c.end.y - c.start.y, c.end.x - c.start.x), angle = std::atan2(point.y - c.start.y, point.x - c.start.x);
				const float old = handleId == 102 ? initial : initial + arc.sweepRadians;
				const float change = static_cast<float>(std::remainder(double(angle - old), 2 * kPi));
				arc.sweepRadians += handleId == 102 ? -change : change;
				if (handleId == 102) c.end = Add(c.start, { radius * std::cos(angle),radius * std::sin(angle) });
			}
		}
		else if (handleId == 104 || handleId == 105)
		{
			auto& solid = std::get<SolidConstruction>(c.parameters);
			if (handleId == 104) solid.depth = Add(solid.depth, delta); else solid.height += delta.y;
		}
		else if (handleId >= 1000)
		{
			const uint32_t index = handleId - 1000;
			if (p == PresetKind::Line || p == PresetKind::Arrow || p >= PresetKind::Cuboid)
			{
				if (index > 1) return fail("unknown endpoint");
				(index == 0 ? c.start : c.end) = Add(index == 0 ? c.start : c.end, delta);
			}
			else if (p == PresetKind::RegularPolygon) c.end = Add(c.end, delta);
			else
			{
				auto vertices = shape.geometry.vertices; if (index >= vertices.size()) return fail("unknown vertex"); vertices[index] = Add(vertices[index], delta);
				if (p == PresetKind::Parallelogram) vertices[(index + 1) % 4] = Sub(Add(vertices[index], vertices[(index + 2) % 4]), vertices[(index + 3) % 4]);
				else if (p == PresetKind::Trapezoid) vertices[index ^ 1].y = vertices[index].y;
				else if (p == PresetKind::Rhombus)
				{
					const Vec2 center = Mul(Add(shape.geometry.vertices[0], shape.geometry.vertices[2]), .5f), axis = Sub(vertices[index], center);
					const float radius = Distance({}, axis); if (!(radius > 0)) return fail("degenerate rhombus"); vertices[(index + 2) % 4] = Sub(center, axis);
					const float other = Distance(center, shape.geometry.vertices[(index + 1) % 4]); const Vec2 side{ -axis.y / radius * other,axis.x / radius * other };
					vertices[(index + 1) % 4] = Add(center, side); vertices[(index + 3) % 4] = Sub(center, side);
				}
				c.parameters = VertexConstruction{ std::move(vertices) };
			}
		}
		else return fail("unsupported parameter handle");
	}
	else
	{
		if (shape.kind == Kind::Circle && handleId == 101) shape.geometry.radius = Distance(shape.geometry.start, Add(InversePoint(shape.transform, found->position), delta));
		else if (shape.kind == Kind::Ellipse && (handleId == 102 || handleId == 103))
			(handleId == 102 ? shape.geometry.end.x : shape.geometry.end.y) += handleId == 102 ? delta.x : delta.y;
		else if (handleId >= 1000)
		{
			const size_t index = handleId - 1000;
			if (shape.kind == Kind::Line && index < 2) (index == 0 ? shape.geometry.start : shape.geometry.end) = Add(index == 0 ? shape.geometry.start : shape.geometry.end, delta);
			else if (index < shape.geometry.vertices.size()) shape.geometry.vertices[index] = Add(shape.geometry.vertices[index], delta);
			else return fail("unknown vertex");
		}
		else return fail("unsupported geometry handle");
	}
	if (!shape.construction && ((shape.kind == Kind::Circle && !(shape.geometry.radius > 0)) ||
		(shape.kind == Kind::Ellipse && !(shape.geometry.end.x > 0 && shape.geometry.end.y > 0)) ||
		(shape.kind == Kind::Line && !(Distance(shape.geometry.start, shape.geometry.end) > 0)))) return fail("empty edited geometry");
	return RebuildEdited(std::move(shape), revision, error);
}
std::optional<ShapeObject> RestyleShape(const ShapeObject& before, const ShapeStyle& style, uint64_t revision, const BuildOptions& options, std::string* error)
{
	if (!Validate(before) || revision <= before.revision || !ValidStyle(style)) { SetError(error, "invalid restyle"); return std::nullopt; }
	auto shape = PromoteShape(before, options); shape.style = style; return RebuildEdited(std::move(shape), revision, error);
}
std::optional<ShapeObject> SetRegularSides(const ShapeObject& before, uint32_t sides, uint64_t revision, std::string* error)
{
	if (!before.construction || !GetToolCapabilities(before.construction->tool.preset).regularSides || sides < 3 || sides > 12 || revision <= before.revision)
	{ SetError(error, "invalid regular sides edit"); return std::nullopt; }
	auto shape = before; shape.construction->tool.sides = sides; return RebuildEdited(std::move(shape), revision, error);
}
bool EditSession::Begin(const ShapeObject& before, uint32_t handle, Vec2 down, const BuildOptions& options)
{
	Cancel(); if (!Validate(before) || !Finite(down)) return false;
	if (handle != 0) { const auto handles = BuildEditHandles(before, options); if (std::none_of(handles.begin(), handles.end(), [&](const EditHandle& h) { return h.id == handle; })) return false; }
	before_ = before; handleId_ = handle; down_ = down; options_ = options; return true;
}
bool EditSession::Preview(Vec2 current, uint64_t revision, std::string* error)
{
	if (!before_) return false;
	candidate_ = EditShape(*before_, handleId_, down_, current, revision, options_, error); return candidate_.has_value();
}
const std::optional<ShapeObject>& EditSession::Candidate() const noexcept { return candidate_; }
std::optional<ShapeObject> EditSession::Commit() { auto result = std::move(candidate_); Cancel(); return result; }
void EditSession::Cancel() noexcept { before_.reset(); candidate_.reset(); }
bool ConstructionSession::Begin(const ToolSpec& tool, const ShapeStyle& style, const ObjectId& id, uint64_t revision)
{
	Cancel(); const auto capabilities = GetToolCapabilities(tool.preset);
	if (!capabilities.enabled || !ValidStyle(style) || IsZeroId(id) || !revision || tool.sides < 3 || tool.sides > 12 ||
		!Finite(tool.rectangleCornerRadius) || tool.rectangleCornerRadius < 0 || (!capabilities.fill && (!style.stroke || style.fill))) return false;
	if (tool.creationMode != DefaultCreationMode(tool.preset) && !(tool.preset == PresetKind::Circle && tool.creationMode == CreationMode::Drag)) return false;
	tool_ = tool; style_ = style; id_ = id; revision_ = revision; active_ = true; return true;
}
void ConstructionSession::ObserveRaw(Vec2 point) noexcept
{
	if (!active_ || confirmed_ != 1 || !Finite(point) || (tool_.preset != PresetKind::Arc && tool_.preset != PresetKind::Sector) || Distance(start_, point) < 1e-6) return;
	const float angle = std::atan2(point.y - start_.y, point.x - start_.x);
	if (!angleStarted_) { lastAngle_ = std::atan2(end_.y - start_.y, end_.x - start_.x); angleStarted_ = true; }
	sweep_ += static_cast<float>(std::remainder(double(angle - lastAngle_), 2 * kPi));
	lastAngle_ = angle;
}
std::optional<ShapeObject> ConstructionSession::Generate(Vec2 down, Vec2 end, bool confirm, std::string* error)
{
	if (!active_ || finished_ || !Finite(down) || !Finite(end)) { SetError(error, "inactive construction or invalid pointer"); return std::nullopt; }
	const auto capabilities = GetToolCapabilities(tool_.preset);
	if (capabilities.points)
	{
		Construction c{ tool_,points_.empty() ? end : points_.front(),end };
		auto vertices = points_; vertices.push_back(end); c.parameters = VertexConstruction{ std::move(vertices) };
		auto result = MakeConstructedShape(c, style_, id_, revision_, error);
		if (!result && !confirm && points_.size() >= 1)
		{
			// 多边形尚未闭合或暂时自交时仍显示已确认折线，不能伪造有效填充。
			c.tool.preset = PresetKind::Polyline;
			auto stroke = style_; stroke.stroke = true; stroke.fill = false;
			if (!style_.stroke) { stroke.strokeRgb = style_.fillRgb; stroke.strokeOpacity = style_.fillOpacity; }
			result = MakeConstructedShape(c, stroke, id_, revision_, error);
		}
		return result;
	}
	if (!capabilities.multiStage) return MakePresetShape(tool_, down, end, style_, id_, revision_, error);
	if (!confirmed_)
	{
		ToolSpec base = tool_;
		base.preset = tool_.preset == PresetKind::Arc || tool_.preset == PresetKind::Sector ? PresetKind::Circle :
			tool_.preset == PresetKind::Cylinder || tool_.preset == PresetKind::Cone ? PresetKind::Ellipse :
			tool_.preset == PresetKind::Prism || tool_.preset == PresetKind::Pyramid ? PresetKind::RegularPolygon : PresetKind::Rectangle;
		base.creationMode = DefaultCreationMode(base.preset); base.rectangleCornerRadius = 0;
		if (tool_.preset == PresetKind::Cube)
		{
			const float side = std::max(std::abs(end.x - down.x), std::abs(end.y - down.y));
			end = { down.x + (end.x < down.x ? -side : side),down.y + (end.y < down.y ? -side : side) };
		}
		if (tool_.preset == PresetKind::Prism || tool_.preset == PresetKind::Pyramid)
		{
			// 首顶点必须跟手：先在非压缩空间生成正多边形，再沿底面中心压缩纵轴。
			Construction c{ base,down,{ end.x,down.y + 2 * (end.y - down.y) } };
			auto object = MakeConstructedShape(c, style_, id_, revision_, error);
			if (object) object->transform = { 1,0,0,.5f,0,down.y * .5f };
			// 此预览不是最终参数对象；去掉模板约束后保留准确的投影点。
			if (object)
			{
				for (auto& v : object->geometry.vertices) v = ApplyTransform(object->transform, v);
				object->transform = {}; object->construction.reset();
			}
			return object;
		}
		return MakePresetShape(base, down, end, style_, id_, revision_, error);
	}
	Construction c{ tool_,start_,end_ };
	if (tool_.preset == PresetKind::Arc || tool_.preset == PresetKind::Sector)
	{
		float sweep = sweep_;
		if (!confirm && Distance(start_, end) > 1e-6)
			sweep += static_cast<float>(std::remainder(double(std::atan2(end.y - start_.y, end.x - start_.x) - lastAngle_), 2 * kPi));
		c.parameters = ArcConstruction{ sweep };
	}
	else if (tool_.preset == PresetKind::Cuboid || tool_.preset == PresetKind::Cube)
		c.parameters = SolidConstruction{ Sub(end, down),0 };
	else c.parameters = SolidConstruction{ {},end.y - down.y };
	return MakeConstructedShape(c, style_, id_, revision_, error);
}
bool ConstructionSession::PreviewDrag(Vec2 down, Vec2 end, std::string* error)
{
	candidate_ = Generate(down, end, false, error); return candidate_.has_value();
}
bool ConstructionSession::ConfirmDrag(Vec2 down, Vec2 end, std::string* error)
{
	if (!active_ || finished_ || !Finite(down) || !Finite(end)) return false;
	if (GetToolCapabilities(tool_.preset).points)
	{
		if (points_.size() >= kMaxVertices || (!points_.empty() && points_.back() == end)) return false;
		points_.push_back(end); ++confirmed_;
		Construction c{ tool_,points_.front(),end }; c.parameters = VertexConstruction{ points_ };
		confirmedCandidate_ = MakeConstructedShape(c, style_, id_, revision_, error); candidate_ = confirmedCandidate_;
		return true; // 未达到最少点数/暂时自交仍可回退，只有Complete才能进入文档。
	}
	ObserveRaw(end);
	auto result = Generate(down, end, true, error); if (!result) { candidate_ = confirmedCandidate_; return false; }
	candidate_ = confirmedCandidate_ = std::move(result);
	if (!confirmed_)
	{
		start_ = down; end_ = end; confirmed_ = 1;
		lastAngle_ = std::atan2(end_.y - start_.y, end_.x - start_.x); sweep_ = 0; angleStarted_ = true;
		finished_ = !GetToolCapabilities(tool_.preset).multiStage;
	}
	else { ++confirmed_; finished_ = true; }
	return true;
}
bool ConstructionSession::Back()
{
	if (!active_ || !confirmed_) return false;
	if (GetToolCapabilities(tool_.preset).points)
	{
		points_.pop_back(); --confirmed_; candidate_.reset(); confirmedCandidate_.reset();
		if (!points_.empty())
		{
			Construction c{ tool_,points_.front(),points_.back() }; c.parameters = VertexConstruction{ points_ };
			candidate_ = confirmedCandidate_ = MakeConstructedShape(c, style_, id_, revision_);
		}
		return true;
	}
	if (confirmed_ == 1) { confirmed_ = 0; candidate_.reset(); confirmedCandidate_.reset(); }
	else
	{
		confirmed_ = 0; finished_ = false;
		auto first = Generate(start_, end_, true, nullptr); confirmed_ = 1; candidate_ = confirmedCandidate_ = std::move(first);
	}
	finished_ = false; sweep_ = 0; lastAngle_ = std::atan2(end_.y - start_.y, end_.x - start_.x); angleStarted_ = true; return true;
}
void ConstructionSession::Cancel() noexcept
{
	active_ = finished_ = angleStarted_ = false; confirmed_ = 0; points_.clear(); candidate_.reset(); confirmedCandidate_.reset(); sweep_ = 0;
}
bool ConstructionSession::CanComplete() const noexcept
{
	return active_ && confirmedCandidate_.has_value() && (GetToolCapabilities(tool_.preset).points || finished_);
}
std::optional<ShapeObject> ConstructionSession::Complete()
{
	if (!CanComplete()) return std::nullopt;
	auto result = std::move(confirmedCandidate_); Cancel(); return result;
}
const std::optional<ShapeObject>& ConstructionSession::Candidate() const noexcept { return candidate_; }
ConstructionStatus ConstructionSession::Status() const noexcept
{
	return { active_,active_ ? (finished_ ? confirmed_ : confirmed_ + 1) : 0,confirmed_,CanComplete(),active_ && confirmed_ > 0,finished_,tool_.preset };
}
uint64_t RevisionAllocator::Next(const ObjectId& id, uint64_t observed)
{
	if (IsZeroId(id)) return 0;
	auto found = std::find_if(revisions_.begin(), revisions_.end(), [&](const auto& entry) { return entry.first == id; });
	if (found == revisions_.end()) { revisions_.push_back({ id,observed }); found = std::prev(revisions_.end()); }
	found->second = std::max(found->second, observed);
	if (found->second == std::numeric_limits<uint64_t>::max()) return 0;
	return ++found->second;
}
}
