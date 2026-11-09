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
	enum class EditLayout { Endpoints, Rectangle, Circle, Ellipse, Vertices, Regular, Arc, Box, Cube, EllipticSolid, PolygonSolid, Compound };
	struct EditModel { EditLayout layout; bool rotate = false, frameScale = false, uniform = false; };
	EditModel ModelFor(const ShapeObject& shape)
	{
		// 顺序与持久化 preset 一致；构造位置与编辑约束共用这张表。
		static constexpr EditModel models[] = {
			{ EditLayout::Endpoints }, { EditLayout::Endpoints },
			{ EditLayout::Rectangle,true }, { EditLayout::Rectangle,true },
			{ EditLayout::Circle }, { EditLayout::Ellipse,true },
			{ EditLayout::Vertices,true }, { EditLayout::Vertices,true },
			{ EditLayout::Vertices,true }, { EditLayout::Vertices,true },
			{ EditLayout::Regular }, { EditLayout::Vertices,true }, { EditLayout::Vertices,true },
			{ EditLayout::Arc }, { EditLayout::Arc },
			{ EditLayout::Box,true,true }, { EditLayout::Cube,true,true,true },
			{ EditLayout::EllipticSolid,true,true }, { EditLayout::EllipticSolid,true,true },
			{ EditLayout::PolygonSolid,true,true,true }, { EditLayout::PolygonSolid,true,true,true }
		};
		static_assert(sizeof(models) / sizeof(models[0]) == static_cast<size_t>(PresetKind::Pyramid) + 1);
		if (shape.construction) return models[static_cast<size_t>(shape.construction->tool.preset)];
		switch (shape.kind)
		{
		case Kind::Line: return { EditLayout::Endpoints };
		case Kind::Rectangle: return { EditLayout::Rectangle,true };
		case Kind::Circle: return { EditLayout::Circle };
		case Kind::Ellipse: return { EditLayout::Ellipse,true };
		case Kind::Polyline: case Kind::Polygon: return { EditLayout::Vertices,true };
		default: return { EditLayout::Compound,true };
		}
	}
	std::array<Vec2, 4> RectCorners(Vec2 start, Vec2 end)
	{
		const float l = (std::min)(start.x,end.x), r = (std::max)(start.x,end.x);
		const float t = (std::min)(start.y,end.y), b = (std::max)(start.y,end.y);
		return { Vec2{ l,t },Vec2{ r,t },Vec2{ r,b },Vec2{ l,b } };
	}
	Vec2 SquareCorner(const Construction& c)
	{
		// 旧文件仍可能保存非正方形raw端点；句柄和编辑必须使用可见正方形的同一角。
		const float side = (std::max)(std::abs(c.end.x - c.start.x),std::abs(c.end.y - c.start.y));
		return Add(c.start,{c.end.x < c.start.x ? -side : side,c.end.y < c.start.y ? -side : side});
	}
	Vec2 BackCorner(const Construction& c)
	{
		const auto corners = RectCorners(c.start,c.tool.preset == PresetKind::Cube ? SquareCorner(c) : c.end); const auto depth = std::get<SolidConstruction>(c.parameters).depth;
		// 选择深度方向最外侧的可见后角，避免把句柄放到虚线交点上。
		return corners[depth.x >= 0 ? (depth.y >= 0 ? 2 : 1) : (depth.y >= 0 ? 3 : 0)];
	}
	ShapeObject CanonicalEllipse(ShapeObject shape)
	{
		const auto& g = shape.geometry;
		if (Similarity(shape.transform) && g.end.x >= g.end.y) return shape;
		// 将旧的纵向长轴、非等比/错切导入归一化；保留画布椭圆和描边等效比例。
		const auto t = shape.transform; const auto center = ApplyTransform(t,g.start);
		const double ux = double(t.m11) * g.end.x, uy = double(t.m12) * g.end.x;
		const double vx = double(t.m21) * g.end.y, vy = double(t.m22) * g.end.y;
		const double xx = ux * ux + vx * vx, yy = uy * uy + vy * vy, xy = ux * uy + vx * vy;
		const double angle = .5 * std::atan2(2 * xy,xx - yy), scale = std::sqrt(std::abs(Determinant(t)));
		const double a = std::sqrt((xx + yy + std::hypot(xx - yy,2 * xy)) * .5);
		const double b = a > 0 ? std::abs(ux * vy - uy * vx) / a : 0;
		shape.geometry.start = {}; shape.geometry.end = { static_cast<float>(a / scale),static_cast<float>(b / scale) };
		shape.transform = { static_cast<float>(scale * std::cos(angle)),static_cast<float>(scale * std::sin(angle)),
			static_cast<float>(-scale * std::sin(angle)),static_cast<float>(scale * std::cos(angle)),center.x,center.y };
		if (shape.construction) { shape.construction->start = Mul(shape.geometry.end,-1); shape.construction->end = shape.geometry.end; }
		return shape;
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
std::vector<EditHandle> BuildEditHandles(const ShapeObject& shape, const BuildOptions& options, Bounds* frameRect)
{
	if (frameRect) *frameRect = {};
	if (!Validate(shape)) return {};
	std::vector<EditHandle> result; const auto model = ModelFor(shape);
	auto add = [&](uint32_t id, EditHandleKind kind, Vec2 local) { result.push_back({ id,kind,ApplyTransform(shape.transform, local) }); };
	const auto& g = shape.geometry;
	const auto* c = shape.construction ? &*shape.construction : nullptr;
	switch (model.layout)
	{
	case EditLayout::Endpoints:
		add(1000,EditHandleKind::Endpoint,c ? c->start : g.start);
		add(1001,EditHandleKind::Endpoint,c ? c->end : g.end); break;
	case EditLayout::Rectangle:
	{
		const auto corners = RectCorners(c ? c->start : g.start,c ? c->end : g.end);
		for (uint32_t i = 0; i < 4; ++i) add(1000 + i,EditHandleKind::Vertex,corners[i]); break;
	}
	case EditLayout::Circle:
	{
		// 外接圆首次编辑前取右下45°；圆心法直接使用构造半径点，不重置方向。
		const auto radiusPoint = c && c->tool.creationMode == CreationMode::CenterRadius ? c->end :
			Add(g.start,{ g.radius * .7071067812f,g.radius * .7071067812f });
		add(100,EditHandleKind::Center,g.start); add(101,EditHandleKind::Radius,radiusPoint); break;
	}
	case EditLayout::Ellipse:
	{
		const auto ellipse = CanonicalEllipse(shape); const auto& e = ellipse.geometry;
		auto axis = [&](uint32_t id, EditHandleKind kind, Vec2 point) { result.push_back({ id,kind,ApplyTransform(ellipse.transform,point) }); };
		axis(100,EditHandleKind::Center,e.start); axis(102,EditHandleKind::Axis,Add(e.start,{ e.end.x,0 }));
		axis(103,EditHandleKind::Axis,Add(e.start,{ 0,e.end.y }));
		if ((shape.style.auxiliary & Auxiliary::Foci) != Auxiliary::None)
		{
			const float focus = std::sqrt((std::max)(0.0f,e.end.x * e.end.x - e.end.y * e.end.y));
			axis(106,EditHandleKind::Focus,Add(e.start,{ focus,0 })); axis(107,EditHandleKind::Focus,Add(e.start,{ -focus,0 }));
		}
		break;
	}
	case EditLayout::Regular:
		add(100,EditHandleKind::Center,c->start); add(1000,EditHandleKind::Vertex,c->end); break;
	case EditLayout::Vertices:
	{
		const auto* typed = c ? std::get_if<VertexConstruction>(&c->parameters) : nullptr;
		const auto& vertices = typed ? typed->vertices : g.vertices;
		for (size_t i = 0; i < vertices.size(); ++i) add(1000 + static_cast<uint32_t>(i),EditHandleKind::Vertex,vertices[i]); break;
	}
	case EditLayout::Arc:
	{
		const auto& arc = std::get<ArcConstruction>(c->parameters); const auto v = Sub(c->end,c->start);
		auto radial = [&](float angle) { return Add(c->start,{ v.x * std::cos(angle) - v.y * std::sin(angle),v.x * std::sin(angle) + v.y * std::cos(angle) }); };
		add(100,EditHandleKind::Center,c->start); add(101,EditHandleKind::Radius,radial(arc.sweepRadians * .5f));
		add(102,EditHandleKind::Angle,c->end); add(103,EditHandleKind::Angle,radial(arc.sweepRadians)); break;
	}
	case EditLayout::Box: case EditLayout::Cube:
	{
		const auto& solid = std::get<SolidConstruction>(c->parameters);
		if (model.layout == EditLayout::Cube) add(1001,EditHandleKind::Vertex,SquareCorner(*c));
		else { const auto corners = RectCorners(c->start,c->end); for (uint32_t i = 0; i < 4; ++i) add(1000 + i,EditHandleKind::Vertex,corners[i]); }
		add(104,EditHandleKind::Depth,Add(shape.editDepthAnchor.value_or(BackCorner(*c)),solid.depth)); break;
	}
	case EditLayout::EllipticSolid:
	{
		const auto& solid = std::get<SolidConstruction>(c->parameters); const auto corners = RectCorners(c->start,c->end);
		const auto center = Mul(Add(corners[0],corners[2]),.5f);
		add(1000,EditHandleKind::Axis,{ corners[0].x,center.y }); add(1001,EditHandleKind::Axis,{ corners[2].x,center.y });
		add(1002,EditHandleKind::Axis,{ center.x,solid.height < 0 ? corners[2].y : corners[0].y });
		add(105,EditHandleKind::Height,Add(center,{ 0,solid.height })); break;
	}
	case EditLayout::PolygonSolid:
		add(1001,EditHandleKind::Vertex,c->end);
		add(105,EditHandleKind::Height,Add(c->start,{ 0,std::get<SolidConstruction>(c->parameters).height })); break;
	default: break;
	}
	auto local = shape; local.transform = {}; GeometryResult geometry;
	if (!BuildGeometry(local,geometry,options)) return {};
	auto bounds = geometry.bounds;
	if (bounds.valid)
	{
		// 统一含描边边界与8DIP间隙；变换缩放后仍保持屏幕上的间距。
		const float gapX = 8 * options.displayScale / std::hypot(shape.transform.m11,shape.transform.m12);
		const float gapY = 8 * options.displayScale / std::hypot(shape.transform.m21,shape.transform.m22);
		bounds.left -= gapX; bounds.right += gapX; bounds.top -= gapY; bounds.bottom += gapY;
		if (frameRect) *frameRect = bounds;
		const auto corners = RectCorners({ bounds.left,bounds.top },{ bounds.right,bounds.bottom });
		if (model.frameScale) for (uint32_t i = 0; i < 4; ++i) add(10 + i * 2,EditHandleKind::Scale,corners[i]);
		if (model.rotate)
		{
			auto top = ApplyTransform(shape.transform,{ (bounds.left + bounds.right) * .5f,bounds.top });
			const float axis = std::hypot(shape.transform.m21,shape.transform.m22);
			top.x -= shape.transform.m21 / axis * 40 * options.displayScale; top.y -= shape.transform.m22 / axis * 40 * options.displayScale;
			result.push_back({ 1,EditHandleKind::Rotate,top });
		}
	}
	return result;
}
std::optional<ShapeObject> EditShape(const ShapeObject& before, uint32_t handleId,
	Vec2 down, Vec2 current, uint64_t revision, const BuildOptions& options, std::string* error, std::optional<float> angleDelta)
{
	auto fail = [&](const char* reason) -> std::optional<ShapeObject> { SetError(error,reason); return std::nullopt; };
	if (!Validate(before) || !Finite(down) || !Finite(current) || revision <= before.revision) return fail("invalid edit identity or pointer");
	const auto handles = BuildEditHandles(before,options); const auto model = ModelFor(before);
	const auto found = std::find_if(handles.begin(),handles.end(),[&](const EditHandle& h) { return h.id == handleId; });
	if (handleId != 0 && found == handles.end()) return fail("unknown edit handle");
	auto shape = PromoteShape(before,options);
	if (model.layout == EditLayout::Cube) shape.construction->end = SquareCorner(*shape.construction);
	if (model.layout == EditLayout::Ellipse && handleId != 0 && handleId != 100 && handleId != 1) shape = CanonicalEllipse(std::move(shape));
	const auto localDown = InversePoint(shape.transform,down), localCurrent = InversePoint(shape.transform,current);
	const auto delta = Sub(localCurrent,localDown);
	const Vec2 pointer = found != handles.end() ? Add(InversePoint(shape.transform,found->position),delta) : localCurrent;
	constexpr float minimum = .001f;
	auto resize = [&](Vec2& start, Vec2& end, uint32_t index) {
		const auto corners = RectCorners(start,end); const auto moving = Add(corners[index],delta), anchor = corners[(index + 2) % 4];
		if ((moving.x - anchor.x) * (corners[index].x - anchor.x) <= 0 ||
			(moving.y - anchor.y) * (corners[index].y - anchor.y) <= 0 ||
			std::abs(moving.x - anchor.x) < minimum || std::abs(moving.y - anchor.y) < minimum) return false;
		start = moving; end = anchor; return true;
	};
	if (handleId == 0 || handleId == 100)
	{
		shape.transform.dx += current.x - down.x; shape.transform.dy += current.y - down.y;
	}
	else if (handleId == 1)
	{
		const auto bounds = LocalShapeBounds(shape,options);
		const Vec2 center = ApplyTransform(shape.transform,{ (bounds.left + bounds.right) * .5f,(bounds.top + bounds.bottom) * .5f });
		if (Distance(center,down) < minimum || Distance(center,current) < minimum) return fail("undefined rotation");
		const double angle = std::atan2(double(current.y) - center.y,double(current.x) - center.x) - std::atan2(double(down.y) - center.y,double(down.x) - center.x);
		Transform rotate; rotate.m11 = rotate.m22 = static_cast<float>(std::cos(angle)); rotate.m12 = static_cast<float>(std::sin(angle)); rotate.m21 = -rotate.m12;
		const auto rotated = ApplyTransform(rotate,center); rotate.dx = center.x - rotated.x; rotate.dy = center.y - rotated.y;
		shape.transform = ComposeTransforms(rotate,shape.transform);
	}
	else if (found->kind == EditHandleKind::Scale)
	{
		const auto bounds = LocalShapeBounds(shape,options); const uint32_t index = (handleId - 10) / 2;
		const auto corners = RectCorners({ bounds.left,bounds.top },{ bounds.right,bounds.bottom });
		const auto moving = corners[index], anchor = corners[(index + 2) % 4];
		// 间隙和线宽不参与缩放，因而外框的对角固定、拖动角与指针同步。
		float sx = (moving.x + delta.x - anchor.x) / (moving.x - anchor.x), sy = (moving.y + delta.y - anchor.y) / (moving.y - anchor.y);
		if (model.uniform) sx = sy = ((sx - 1) * (sx - 1) > (sy - 1) * (sy - 1) ? sx : sy);
		if (!(sx > minimum && sy > minimum) || !Finite(sx) || !Finite(sy)) return fail("degenerate scale");
		auto& c = *shape.construction;
		auto map = [&](Vec2 point) { return Vec2{ anchor.x + (point.x - anchor.x) * sx,anchor.y + (point.y - anchor.y) * sy }; };
		c.start = map(c.start); c.end = map(c.end);
		auto& solid = std::get<SolidConstruction>(c.parameters); solid.depth.x *= sx; solid.depth.y *= sy; solid.height *= sy;
	}
	else if (model.layout == EditLayout::Rectangle)
	{
		auto& start = shape.construction ? shape.construction->start : shape.geometry.start;
		auto& end = shape.construction ? shape.construction->end : shape.geometry.end;
		if (!resize(start,end,handleId - 1000)) return fail("rectangle cannot flip");
	}
	else if (model.layout == EditLayout::Circle)
	{
		if (Distance(shape.geometry.start,pointer) < minimum) return fail("empty circle radius");
		// 首次编辑外接圆时以几何圆心转换，之后方向始终来自 construction.end。
		if (!shape.construction) shape.construction = Construction{ { PresetKind::Circle,CreationMode::CenterRadius },shape.geometry.start,pointer };
		auto& c = *shape.construction; c.tool.creationMode = CreationMode::CenterRadius; c.start = shape.geometry.start; c.end = pointer;
	}
	else if (model.layout == EditLayout::Ellipse)
	{
		const auto center = shape.geometry.start; auto axes = shape.geometry.end;
		if (handleId == 106 || handleId == 107)
		{
			const auto focus = Mul(Sub(pointer,center),handleId == 106 ? 1.0f : -1.0f); const float distance = Distance({},focus);
			if (!(distance <= axes.x - minimum)) return fail("focus must stay inside major axis");
			// a与中心固定；两焦点互为镜像，方向通过对象旋转保存。
			axes.y = std::sqrt((std::max)(0.0f,axes.x * axes.x - distance * distance));
			if (distance > minimum)
			{
				const float cosine = focus.x / distance, sine = focus.y / distance;
				Transform rotate{ cosine,sine,-sine,cosine,0,0 }; const auto moved = ApplyTransform(rotate,center);
				rotate.dx = center.x - moved.x; rotate.dy = center.y - moved.y; shape.transform = ComposeTransforms(shape.transform,rotate);
			}
		}
		else if (handleId == 102) axes.x += delta.x; else axes.y += delta.y;
		if (!(axes.x >= axes.y && axes.y >= minimum)) return fail("degenerate ellipse axis");
		if (shape.construction) { shape.construction->start = Sub(center,axes); shape.construction->end = Add(center,axes); }
		else shape.geometry.end = axes;
	}
	else if (shape.construction)
	{
		auto& c = *shape.construction; const auto p = c.tool.preset;
		if (model.layout == EditLayout::Arc)
		{
			auto& arc = std::get<ArcConstruction>(c.parameters); const float radius = Distance(c.start,c.end);
			if (Distance(c.start,pointer) < minimum) return fail("undefined arc pointer");
			if (handleId == 101) c.end = Add(c.start,Mul(Sub(c.end,c.start),Distance(c.start,pointer) / radius));
			else
			{
				const float initial = std::atan2(c.end.y - c.start.y,c.end.x - c.start.x), angle = std::atan2(pointer.y - c.start.y,pointer.x - c.start.x);
				const float old = handleId == 102 ? initial : initial + arc.sweepRadians;
				const float change = angleDelta.value_or(static_cast<float>(std::remainder(double(angle - old),2 * kPi)));
				const float sweep = arc.sweepRadians + (handleId == 102 ? -change : change);
				if (!Finite(change) || sweep * arc.sweepRadians <= 0 || std::abs(sweep) < 1e-5f || std::abs(sweep) > 2 * kPi + 1e-6) return fail("degenerate arc sweep");
				arc.sweepRadians = sweep;
				if (handleId == 102) c.end = Add(c.start,{ radius * std::cos(initial + change),radius * std::sin(initial + change) });
			}
		}
		else if (handleId == 104 || handleId == 105)
		{
			auto& solid = std::get<SolidConstruction>(c.parameters);
			if (handleId == 104) solid.depth = Add(solid.depth,delta); else solid.height += delta.y;
		}
		else if (model.layout == EditLayout::Box)
		{
			if (!resize(c.start,c.end,handleId - 1000)) return fail("front face cannot flip");
		}
		else if (model.layout == EditLayout::Cube)
		{
			const auto diagonal = Sub(c.end,c.start); const float side = std::abs(diagonal.x);
			// 投影到原对角线，避免factory再次取较长边造成角点跳变。
			const float next = side + (delta.x * (diagonal.x > 0 ? 1 : -1) + delta.y * (diagonal.y > 0 ? 1 : -1)) * .5f;
			if (next < minimum) return fail("cube cannot flip");
			c.end = Add(c.start,{ next * (diagonal.x > 0 ? 1 : -1),next * (diagonal.y > 0 ? 1 : -1) });
		}
		else if (model.layout == EditLayout::EllipticSolid)
		{
			const auto center = Mul(Add(c.start,c.end),.5f); Vec2 axes{ std::abs(c.end.x - c.start.x) * .5f,std::abs(c.end.y - c.start.y) * .5f };
			if (handleId == 1000) axes.x -= delta.x; else if (handleId == 1001) axes.x += delta.x;
			else axes.y += std::get<SolidConstruction>(c.parameters).height < 0 ? delta.y : -delta.y;
			if (axes.x < minimum || axes.y < minimum) return fail("degenerate solid base");
			c.start = Sub(center,axes); c.end = Add(center,axes);
		}
		else if (model.layout == EditLayout::PolygonSolid || model.layout == EditLayout::Regular) c.end = Add(c.end,delta);
		else if (model.layout == EditLayout::Endpoints) (handleId == 1000 ? c.start : c.end) = pointer;
		else if (model.layout == EditLayout::Vertices)
		{
			const size_t index = handleId - 1000; const auto& old = before.geometry.vertices; auto vertices = old; vertices[index] = Add(vertices[index],delta);
			if (p == PresetKind::Parallelogram)
			{
				// 对角不动，两邻点各分担一半位移，保持对边平行。
				vertices[(index + 1) % 4] = Add(old[(index + 1) % 4],Mul(delta,.5f));
				vertices[(index + 3) % 4] = Add(old[(index + 3) % 4],Mul(delta,.5f));
			}
			else if (p == PresetKind::Trapezoid)
			{
				vertices[index ^ 1].y = vertices[index].y;
				if ((vertices[2].y - vertices[0].y) * (old[2].y - old[0].y) <= 0 ||
					vertices[1].x - vertices[0].x < minimum || vertices[2].x - vertices[3].x < minimum) return fail("trapezoid cannot cross");
			}
			else if (p == PresetKind::Rhombus)
			{
				const auto center = Mul(Add(old[0],old[2]),.5f), axis = Sub(old[index],center); const float length = Distance({},axis);
				const float factor = 1 + (delta.x * axis.x + delta.y * axis.y) / (length * length);
				if (factor * length < minimum) return fail("rhombus cannot flip");
				vertices[index] = Add(center,Mul(axis,factor)); vertices[(index + 2) % 4] = Sub(center,Mul(axis,factor));
			}
			c.parameters = VertexConstruction{ std::move(vertices) };
		}
		else return fail("unsupported parameter handle");
	}
	else if (model.layout == EditLayout::Endpoints) (handleId == 1000 ? shape.geometry.start : shape.geometry.end) = pointer;
	else if (model.layout == EditLayout::Vertices) shape.geometry.vertices[handleId - 1000] = pointer;
	else return fail("unsupported geometry handle");
	auto result = RebuildEdited(std::move(shape),revision,error); if (!result) return result;
	if (result->kind == Kind::Line && Distance(result->geometry.start,result->geometry.end) < minimum) return fail("empty edited line");
	if (result->kind == Kind::Polygon && model.layout == EditLayout::Vertices)
	{
		auto area = [](const std::vector<Vec2>& v) { double value = 0; for (size_t i = 1; i + 1 < v.size(); ++i) value += Cross(v[0],v[i],v[i + 1]); return value; };
		if (area(before.geometry.vertices) * area(result->geometry.vertices) <= 0 || std::abs(area(result->geometry.vertices)) < minimum) return fail("polygon cannot flip or collapse");
	}
	if (result->construction)
	{
		const auto& c = *result->construction;
		if (const auto* solid = std::get_if<SolidConstruction>(&c.parameters))
		{
			const auto& old = std::get<SolidConstruction>(before.construction->parameters);
			if (model.layout == EditLayout::Box || model.layout == EditLayout::Cube)
			{ if (Distance({},solid->depth) < minimum) return fail("empty depth"); }
			else if (solid->height * old.height <= 0 || std::abs(solid->height) < minimum) return fail("solid height cannot flip");
			if (c.tool.preset == PresetKind::Cone && handleId != 0 && handleId != 1 &&
				std::abs(solid->height) <= std::abs(c.end.y - c.start.y) * .5f + minimum) return fail("cone apex must stay outside base");
		}
	}
	SetError(error,{}); return result;
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
	const auto handles = BuildEditHandles(before,options);
	const auto found = std::find_if(handles.begin(),handles.end(),[&](const EditHandle& h) { return h.id == handle; });
	if (handle != 0 && found == handles.end()) return false;
	before_ = before; handleId_ = handle; down_ = lastValid_ = down; options_ = options;
	// 后角身份在Down锁定；跨过深度坐标轴后仍跟随同一个角，直到手势结束。
	if (handle == 104) depthAnchor_ = before.editDepthAnchor.value_or(BackCorner(*before.construction));
	angleActive_ = before.construction && (before.construction->tool.preset == PresetKind::Arc || before.construction->tool.preset == PresetKind::Sector) && (handle == 102 || handle == 103);
	if (angleActive_)
	{
		anglePoint_ = InversePoint(before.transform,found->position); const auto center = before.construction->start;
		lastAngle_ = std::atan2(anglePoint_.y - center.y,anglePoint_.x - center.x);
	}
	return true;
}
bool EditSession::Preview(Vec2 current, uint64_t revision, std::string* error, std::optional<float> angleDelta)
{
	if (!before_ || !Finite(current) || revision <= before_->revision || (angleDelta && !Finite(*angleDelta)))
	{ canCommit_ = false; candidate_.reset(); return false; }
	if (angleActive_)
	{
		const auto point = Add(anglePoint_,Sub(InversePoint(before_->transform,current),InversePoint(before_->transform,down_)));
		const auto center = before_->construction->start;
		if (Distance(center,point) > 1e-6)
		{
			const float angle = std::atan2(point.y - center.y,point.x - center.x);
			// 每条真实编辑轨迹只累计相邻差值；外部累计值可跨过latest-wins丢弃的中间采样。
			angleDelta_ = angleDelta.value_or(angleDelta_ + static_cast<float>(std::remainder(double(angle - lastAngle_),2 * kPi)));
			lastAngle_ = angle;
		}
	}
	candidate_ = EditShape(*before_,handleId_,down_,current,revision,options_,error,AngleDelta());
	canCommit_ = candidate_.has_value();
	if (canCommit_) { candidate_->editDepthAnchor = depthAnchor_; lastValid_ = current; lastValidAngle_ = angleDelta_; return true; }
	// 从上次有效位置逼近约束边界，预览始终可见；最终原始指针无效时Commit拒绝。
	float low = 0, high = 1; auto nearest = EditShape(*before_,handleId_,down_,lastValid_,revision,options_,nullptr,angleActive_ ? std::optional<float>(lastValidAngle_) : std::nullopt);
	for (unsigned i = 0; i < 24; ++i)
	{
		const float middle = (low + high) * .5f; const auto point = Add(lastValid_,Mul(Sub(current,lastValid_),middle));
		const auto angle = angleActive_ ? std::optional<float>(lastValidAngle_ + (angleDelta_ - lastValidAngle_) * middle) : std::nullopt;
		auto next = EditShape(*before_,handleId_,down_,point,revision,options_,nullptr,angle);
		if (next) { low = middle; nearest = std::move(next); } else high = middle;
	}
	if (nearest) candidate_ = std::move(nearest);
	else { candidate_ = *before_; candidate_->revision = revision; }
	candidate_->editDepthAnchor = depthAnchor_;
	return true;
}
std::optional<float> EditSession::AngleDelta() const noexcept { return angleActive_ ? std::optional<float>(angleDelta_) : std::nullopt; }
const std::optional<ShapeObject>& EditSession::Candidate() const noexcept { return candidate_; }
std::optional<ShapeObject> EditSession::Commit() { auto result = canCommit_ ? std::move(candidate_) : std::nullopt; if (result) result->editDepthAnchor.reset(); Cancel(); return result; }
void EditSession::Cancel() noexcept
{
	before_.reset(); candidate_.reset(); depthAnchor_.reset(); angleActive_ = canCommit_ = false; lastAngle_ = angleDelta_ = lastValidAngle_ = 0;
}
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
