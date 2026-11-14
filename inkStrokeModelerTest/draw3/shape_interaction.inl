// 共享交互实现由 shape_core.cpp 引入，不依赖窗口或运行期历史栈。
namespace draw3::shapes
{
namespace
{
	Vec2 Add(Vec2 a, Vec2 b) { return { a.x + b.x,a.y + b.y }; }
	Vec2 Sub(Vec2 a, Vec2 b) { return { a.x - b.x,a.y - b.y }; }
	Vec2 Mul(Vec2 a, float value) { return { a.x * value,a.y * value }; }
	double Dot(Vec2 a, Vec2 b) { return double(a.x) * b.x + double(a.y) * b.y; }
	struct BaseFrame { Vec2 left, right, midpoint, unit, normal; float length; };
	BaseFrame MakeBaseFrame(Vec2 left, Vec2 right)
	{
		const Vec2 base = Sub(right,left); const float length = Distance({},base);
		const Vec2 unit = length > 0 ? Mul(base,1 / length) : Vec2{};
		return { left,right,Mul(Add(left,right),.5f),unit,{ -unit.y,unit.x },length };
	}
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
			{ EditLayout::Circle,true }, { EditLayout::Ellipse,true },
			{ EditLayout::Vertices,true }, { EditLayout::Vertices,true },
			{ EditLayout::Vertices,true }, { EditLayout::Vertices,true },
			{ EditLayout::Regular,true }, { EditLayout::Vertices,true }, { EditLayout::Vertices,true },
			{ EditLayout::Arc,true }, { EditLayout::Arc,true },
			{ EditLayout::Box,false,true }, { EditLayout::Cube,false,true,true },
			{ EditLayout::EllipticSolid,false,true }, { EditLayout::EllipticSolid,false,true },
			{ EditLayout::PolygonSolid,false,true,true }, { EditLayout::PolygonSolid,false,true,true }
		};
		static_assert(sizeof(models) / sizeof(models[0]) == static_cast<size_t>(PresetKind::Pyramid) + 1);
		if (shape.construction) return models[static_cast<size_t>(shape.construction->tool.preset)];
		switch (shape.kind)
		{
		case Kind::Line: return { EditLayout::Endpoints };
		case Kind::Rectangle: return { EditLayout::Rectangle,true };
		case Kind::Circle: return { EditLayout::Circle,true };
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
	void SyncEquilateralConstruction(Construction& c, const std::vector<Vec2>& vertices)
	{
		c.start = Mul(Add(Add(vertices[0],vertices[1]),vertices[2]),1.0f / 3);
		c.end = vertices[0];
	}
	ShapeStyle VisiblePreviewStroke(ShapeStyle style)
	{
		if (!style.stroke) { style.strokeRgb = style.fillRgb; style.strokeOpacity = style.fillOpacity; }
		style.stroke = true; style.fill = false; return style;
	}
	std::optional<ShapeObject> OpenPointCandidate(const std::vector<Vec2>& points, const ShapeStyle& style,
		const ObjectId& id, uint64_t revision, std::string* error)
	{
		if (points.empty()) { SetError(error,"empty point preview"); return std::nullopt; }
		auto stroke = VisiblePreviewStroke(style); stroke.auxiliary = Auxiliary::None;
		if (points.size() == 1)
		{
			ShapeObject point; point.id = id; point.revision = revision; point.kind = Kind::Line; point.style = stroke;
			point.geometry.start = point.geometry.end = points.front();
			if (!Validate(point)) { SetError(error,"invalid point preview"); return std::nullopt; }
			SetError(error,{}); return point;
		}
		Construction c{ { PresetKind::Polyline,CreationMode::Points },points.front(),points.back() };
		c.parameters = VertexConstruction{ points };
		return MakeConstructedShape(c,stroke,id,revision,error);
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

	const auto* projected = c ? std::get_if<SolidConstruction>(&c->parameters) : nullptr;
	if (projected)
	{
		const auto& s=*projected; const auto offset=ObliquePoint(s,s.offset);
		add(105,EditHandleKind::Height,Add(c->start,{offset.x,offset.y+s.height}));
		const auto base=ObliqueBase(*c,.25f);add(1001,EditHandleKind::Vertex,Add(c->start,ObliquePoint(s,base.front())));
	}
	else switch (model.layout)
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
		if (true) // 焦点编辑与辅助点显示独立。
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
		if (model.rotate || projected)
		{
			auto top = ApplyTransform(shape.transform,{ (bounds.left + bounds.right) * .5f,bounds.top });
			const float axis = std::hypot(shape.transform.m21,shape.transform.m22);
			top.x -= shape.transform.m21 / axis * 24 * options.displayScale; top.y -= shape.transform.m22 / axis * 24 * options.displayScale;
			result.push_back({ projected ? 0u : 1u,projected ? EditHandleKind::Move : EditHandleKind::Rotate,top });
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
	if (model.layout == EditLayout::Ellipse && handleId != 0 && handleId != 100 && handleId != 1) shape = CanonicalEllipse(std::move(shape));
	const auto localDown = InversePoint(shape.transform,down), localCurrent = InversePoint(shape.transform,current);
	const auto delta = Sub(localCurrent,localDown);
	const Vec2 pointer = found != handles.end() ? Add(InversePoint(shape.transform,found->position),delta) : localCurrent;
	constexpr float minimum = .001f;
	auto resize = [&](Vec2& start, Vec2& end, uint32_t index) {
		const auto corners = RectCorners(start,end); const auto moving = Add(corners[index],delta), anchor = corners[(index + 2) % 4];
		if (std::abs(moving.x-anchor.x)<minimum || std::abs(moving.y-anchor.y)<minimum) return false;
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
		if (model.frameScale) sx = sy = ((sx - 1) * (sx - 1) > (sy - 1) * (sy - 1) ? sx : sy);
		if (!(sx > minimum && sy > minimum) || !Finite(sx) || !Finite(sy)) return fail("degenerate scale");
		auto& c = *shape.construction;
		auto map = [&](Vec2 point) { return Vec2{ anchor.x + (point.x - anchor.x) * sx,anchor.y + (point.y - anchor.y) * sy }; };
		c.start = map(c.start); c.end = map(c.end);
		auto& solid = std::get<SolidConstruction>(c.parameters); solid.height *= sy;
		solid.baseSize=Mul(solid.baseSize,sx);solid.offset=Mul(solid.offset,sx);
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
				if (!Finite(change) || !Finite(sweep) || std::abs(sweep) < 1e-5f || std::abs(sweep) > 2 * kPi + 1e-6) return fail("degenerate arc sweep");
				if (sweep * arc.sweepRadians < 0)
				{
					// 跨过另一端后交换角度角色，以相反表示保持原绕向和被拖点连续。
					const float start = handleId == 102 ? initial + arc.sweepRadians : initial + sweep;
					arc.sweepRadians = -sweep;
					c.end = Add(c.start,{ radius * std::cos(start),radius * std::sin(start) });
				}
				else
				{
					arc.sweepRadians = sweep;
					if (handleId == 102) c.end = Add(c.start,{ radius * std::cos(initial + change),radius * std::sin(initial + change) });
				}
			}
		}

		else if (auto* solid = std::get_if<SolidConstruction>(&c.parameters))
		{
			auto& s=*solid;
			const float co=std::cos(s.baseRotation),si=std::sin(s.baseRotation);
			auto unproject=[&](Vec2 v){const float y=v.y/-.3535533906f;const float x=v.x-(s.mirrored ? -.3535533906f : .3535533906f)*y;return Vec2{co*x+si*y,-si*x+co*y};};
			if(handleId==105)
			{
				if(!c.tool.rightSolid && (p==PresetKind::Prism || p==PresetKind::Pyramid))s.offset=Add(s.offset,unproject({delta.x,0}));
				if(p==PresetKind::Cube)s.height=std::copysign(s.baseSize.x,pointer.y-c.start.y); else s.height+=delta.y;
			}
			else if(handleId==1001)
			{
				// 逆投影后径向缩放、切向绕Z旋转；长方体保持首笔长宽比。
				const auto q=unproject(Sub(pointer,c.start)), initial=ObliqueBase(c,.25f).front();
				const float factor=Distance({},q)/Distance({},initial);
				if(!(factor>minimum))return fail("empty solid base");
				s.baseSize=Mul(s.baseSize,factor);
				if(p!=PresetKind::Cylinder && p!=PresetKind::Cone)s.baseRotation+=std::atan2(q.y,q.x)-std::atan2(initial.y,initial.x);
				if(p==PresetKind::Cube)s.height=std::copysign(s.baseSize.x,s.height);
			}
			else return fail("unsupported solid handle");
		}
		else if (model.layout == EditLayout::Regular) c.end = Add(c.end,delta);
		else if (model.layout == EditLayout::Endpoints) (handleId == 1000 ? c.start : c.end) = pointer;
		else if (model.layout == EditLayout::Vertices)
		{
			const size_t index = handleId - 1000; const auto& old = before.geometry.vertices; auto vertices = old; vertices[index] = Add(vertices[index],delta);
			if (p == PresetKind::Triangle && c.tool.constraint != PlanarConstraint::Free)
			{
				const auto a = c.tool.constraintVertex, b = (a + 1) % 3, d = (a + 2) % 3;
				const auto oldBase = MakeBaseFrame(old[d],old[b]);
				const float oldHeight = static_cast<float>(Dot(Sub(old[a],oldBase.midpoint),oldBase.normal));
				if (oldBase.length < minimum || std::abs(oldHeight) < minimum) return fail("degenerate triangle base");
				if (index == a)
				{
					const float height = static_cast<float>(Dot(Sub(pointer,oldBase.midpoint),oldBase.normal));
					if (height * oldHeight <= 0 || std::abs(height) < minimum) return fail("triangle cannot collapse");
					if (c.tool.constraint == PlanarConstraint::Isosceles)
					{
						vertices[a] = Add(oldBase.midpoint,Mul(oldBase.normal,height)); vertices[b] = old[b]; vertices[d] = old[d];
					}
					else
					{
						const float half = std::abs(height) / std::sqrt(3.0f);
						vertices[a] = Add(oldBase.midpoint,Mul(oldBase.normal,height));
						vertices[b] = Add(oldBase.midpoint,Mul(oldBase.unit,half));
						vertices[d] = Sub(oldBase.midpoint,Mul(oldBase.unit,half));
					}
				}
				else
				{
					const auto base = MakeBaseFrame(vertices[d],vertices[b]); if (base.length < minimum) return fail("degenerate triangle base");
					const float height = c.tool.constraint == PlanarConstraint::Equilateral ?
						std::copysign(std::sqrt(3.0f) * .5f * base.length,oldHeight) : oldHeight;
					vertices[a] = Add(base.midpoint,Mul(base.normal,height));
				}
			}
			else if (p == PresetKind::Parallelogram)
			{
				// 锁定手势参考的两条边方向；有符号基坐标过零才翻向，不能只比较屏幕X。
				const size_t opposite=(index+2)%4, next=(index+1)%4, previous=(index+3)%4;
				const auto u=Sub(old[next],old[opposite]), v=Sub(old[previous],old[opposite]);
				const auto q=Sub(vertices[index],old[opposite]);
				const double determinant=double(u.x)*v.y-double(u.y)*v.x;
				if(std::abs(determinant)<minimum) return fail("degenerate parallelogram basis");
				const float a=static_cast<float>((double(q.x)*v.y-double(q.y)*v.x)/determinant);
				const float b=static_cast<float>((double(u.x)*q.y-double(u.y)*q.x)/determinant);
				vertices[next]=Add(old[opposite],Mul(u,a));
				vertices[previous]=Add(old[opposite],Mul(v,b));
			}
			else if (p == PresetKind::Trapezoid && c.tool.constraint == PlanarConstraint::Isosceles)
			{
				const auto oldBase = MakeBaseFrame(old[3],old[2]);
				const auto oldTopMid = Mul(Add(old[0],old[1]),.5f);
				const float oldHeight = static_cast<float>(Dot(Sub(oldTopMid,oldBase.midpoint),oldBase.normal));
				const float topLength = Distance(old[0],old[1]);
				if (oldBase.length < minimum || topLength < minimum || std::abs(oldHeight) < minimum) return fail("degenerate trapezoid");
				BaseFrame base = oldBase; float height = oldHeight, half = topLength * .5f;
				if (index <= 1)
				{
					const auto offset = Sub(pointer,oldBase.midpoint); height = static_cast<float>(Dot(offset,oldBase.normal));
					const float along = static_cast<float>(Dot(offset,oldBase.unit));
					if (std::abs(height) < minimum || std::abs(along) < minimum ||
						(index == 0 ? along > 0 : along < 0)) return fail("trapezoid cannot cross");
					half = std::abs(along);
				}
				else
				{
					base = MakeBaseFrame(vertices[3],vertices[2]);
					if (base.length < minimum || Dot(Sub(base.right,base.left),oldBase.unit) < minimum) return fail("trapezoid cannot cross");
				}
				const auto topMid = Add(base.midpoint,Mul(base.normal,height));
				vertices[0] = Sub(topMid,Mul(base.unit,half)); vertices[1] = Add(topMid,Mul(base.unit,half));
				if (index <= 1) { vertices[2] = old[2]; vertices[3] = old[3]; }
			}
			else if (p == PresetKind::Trapezoid)
			{
				vertices[index ^ 1].y = vertices[index].y;
				if (std::abs(vertices[2].y - vertices[0].y) < minimum ||
					vertices[1].x - vertices[0].x < minimum || vertices[2].x - vertices[3].x < minimum) return fail("trapezoid cannot cross");
			}
			else if (p == PresetKind::Rhombus)
			{
				const auto center = Mul(Add(old[0],old[2]),.5f), axis = Sub(old[index],center); const float length = Distance({},axis);
				const float factor = 1 + (delta.x * axis.x + delta.y * axis.y) / (length * length);
				if (std::abs(factor * length) < minimum) return fail("empty rhombus");
				vertices[index] = Add(center,Mul(axis,factor)); vertices[(index + 2) % 4] = Sub(center,Mul(axis,factor));
			}
			c.parameters = VertexConstruction{ vertices };
			if (p == PresetKind::Triangle && c.tool.constraint == PlanarConstraint::Equilateral) SyncEquilateralConstruction(c,vertices);
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
		const auto preset = result->construction ? result->construction->tool.preset : PresetKind::Polygon;
		const bool allowFlip = preset == PresetKind::Parallelogram || preset == PresetKind::Rhombus || preset == PresetKind::Trapezoid;
		if ((!allowFlip && area(before.geometry.vertices)*area(result->geometry.vertices)<=0) || std::abs(area(result->geometry.vertices)) < minimum) return fail("polygon cannot collapse");
	}
	SetError(error,{}); return result;
}
std::optional<ShapeObject> RestyleShape(const ShapeObject& before, const ShapeStyle& style, uint64_t revision, const BuildOptions& options, std::string* error)
{
	if (!Validate(before) || revision <= before.revision || !ValidStyle(style)) { SetError(error, "invalid restyle"); return std::nullopt; }
	auto shape = PromoteShape(before, options); shape.style = style; return RebuildEdited(std::move(shape), revision, error);
}

Construction MakeSolidConstruction(const ToolSpec& tool, Vec2 down, Vec2 end, float height)
{
	Construction c{tool,down,end}; SolidConstruction s;
	const bool box=tool.preset==PresetKind::Cuboid || tool.preset==PresetKind::Cube;
	if(box)
	{
		constexpr float projection=.3535533906f;
		const float width=std::abs(end.x-down.x), height=std::abs(end.y-down.y);
		// 手势限定屏幕外接框；仅越过按下点竖线才切换斜轴方向。
		s.mirrored=end.x<down.x;
		float fittedWidth=width, fittedHeight=height;
		if(tool.preset==PresetKind::Cube)
		{
			const float side=(std::min)(width/(1+projection),height/projection);
			s.baseSize={side,side}; fittedWidth=side*(1+projection); fittedHeight=side*projection;
		}
		else
		{
			// 过窄框压缩深度，保留有效水平边；零宽/高仍由既有校验拒绝。
			fittedHeight=(std::min)(height,(std::max)(0.f,width-.001f));
			s.baseSize={width-fittedHeight,fittedHeight/projection};
		}
		c.start={down.x+std::copysign(fittedWidth*.5f,end.x-down.x),down.y+std::copysign(fittedHeight*.5f,end.y-down.y)};
	}
	else { const float radius=Distance(down,end); s.baseSize={radius,radius}; }
	s.height=tool.preset==PresetKind::Cube ? std::copysign(s.baseSize.x,height) : height;
	c.parameters=s; return c;
}
std::optional<ShapeObject> SolidBasePreview(const ToolSpec& tool, Vec2 down, Vec2 end, const ShapeStyle& style, const ObjectId& id, uint64_t revision, std::string* error)
{
	const auto c=MakeSolidConstruction(tool,down,end,1); const auto& s=std::get<SolidConstruction>(c.parameters);
	if(!(s.baseSize.x>0 && s.baseSize.y>0)){SetError(error,"empty solid base");return std::nullopt;}
	ShapeObject result; result.id=id;result.revision=revision;result.kind=Kind::Polygon;result.style=style;
	for(auto v:ObliqueBase(c,.25f))result.geometry.vertices.push_back(Add(c.start,ObliquePoint(s,v)));
	if(!Validate(result)){SetError(error,"invalid solid base preview");return std::nullopt;}return result;
}
std::optional<ShapeObject> SetRightSolid(const ShapeObject& before, bool right, uint64_t revision, std::string* error)
{
	if(!before.construction || revision<=before.revision || (before.construction->tool.preset!=PresetKind::Prism && before.construction->tool.preset!=PresetKind::Pyramid))
	{SetError(error,"unsupported solid constraint");return std::nullopt;}
	auto shape=before;auto& c=*shape.construction;auto& s=std::get<SolidConstruction>(c.parameters);
	c.tool.rightSolid=right;if(right)s.offset={};return RebuildEdited(std::move(shape),revision,error);
}
std::optional<ShapeObject> SetRegularSides(const ShapeObject& before, uint32_t sides, uint64_t revision, std::string* error)
{
	if (!before.construction || !GetToolCapabilities(before.construction->tool.preset).regularSides || sides < kMinRegularSides || sides > kMaxRegularSides ||
		((before.construction->tool.preset == PresetKind::Prism || before.construction->tool.preset == PresetKind::Pyramid) && sides > kMaxSolidSides) || revision <= before.revision)
	{ SetError(error, "invalid regular sides edit"); return std::nullopt; }
	auto shape = before; shape.construction->tool.sides = sides; return RebuildEdited(std::move(shape), revision, error);
}
std::optional<ShapeObject> SetPlanarConstraint(const ShapeObject& before, PlanarConstraint constraint, uint64_t revision, std::string* error)
{
	auto fail = [&](const char* reason) -> std::optional<ShapeObject> { SetError(error,reason); return std::nullopt; };
	if (!before.construction || revision <= before.revision ||
		(before.construction->tool.preset != PresetKind::Triangle && before.construction->tool.preset != PresetKind::Trapezoid))
		return fail("invalid planar constraint edit");
	auto shape = before; auto& c = *shape.construction; c.tool.constraint = constraint;
	c.tool.creationMode = constraint == PlanarConstraint::Equilateral ? CreationMode::CenterRadius : CreationMode::Drag;
	if (!ValidPlanarConstraint(c.tool)) return fail("unsupported planar constraint");
	const auto* typed = std::get_if<VertexConstruction>(&c.parameters);
	auto vertices = typed ? typed->vertices : before.geometry.vertices;
	constexpr float minimum = .001f;
	if (c.tool.preset == PresetKind::Triangle)
	{
		if (vertices.size() != 3) return fail("invalid triangle vertices");
		if (constraint != PlanarConstraint::Free)
		{
			// 首次约束选择顶点位移最小的底边；重复等腰点击轮换三种身份。
			if (before.construction->tool.constraint == PlanarConstraint::Free)
			{
				double best = std::numeric_limits<double>::max();
				for (uint32_t i = 0; i < 3; ++i)
				{
					const auto f = MakeBaseFrame(vertices[(i+2)%3],vertices[(i+1)%3]);
					const float h = static_cast<float>(Dot(Sub(vertices[i],f.midpoint),f.normal));
					const float target = constraint == PlanarConstraint::Equilateral ? std::copysign(std::sqrt(3.f)*.5f*f.length,h) : h;
					const double cost = Distance(vertices[i],Add(f.midpoint,Mul(f.normal,target)));
					if (cost < best - 1e-5) { best = cost; c.tool.constraintVertex = i; }
				}
			}
			else if (constraint == PlanarConstraint::Isosceles && before.construction->tool.constraint == constraint)
				c.tool.constraintVertex = (c.tool.constraintVertex + 1) % 3;
			const auto a = c.tool.constraintVertex, b = (a+1)%3, d = (a+2)%3;
			const auto base = MakeBaseFrame(vertices[d],vertices[b]);
			const float height = static_cast<float>(Dot(Sub(vertices[a],base.midpoint),base.normal));
			if (base.length < minimum || std::abs(height) < minimum) return fail("degenerate triangle base");
			const float nextHeight = constraint == PlanarConstraint::Equilateral ?
				std::copysign(std::sqrt(3.0f) * .5f * base.length,height) : height;
			vertices[a] = Add(base.midpoint,Mul(base.normal,nextHeight));
		}
	}
	else
	{
		if (vertices.size() != 4) return fail("invalid trapezoid vertices");
		if (constraint == PlanarConstraint::Free && vertices[2].y != vertices[3].y)
		{
			// 自由梯形仍沿用水平底边的旧格式；把倾斜归入transform，解除约束不改变画布顶点。
			const auto base = MakeBaseFrame(vertices[3],vertices[2]);
			if (base.length < minimum) return fail("degenerate trapezoid base");
			for (auto& vertex : vertices)
			{
				const auto offset = Sub(vertex,base.left);
				vertex = {static_cast<float>(Dot(offset,base.unit)),static_cast<float>(Dot(offset,base.normal))};
			}
			vertices[2] = {base.length,0}; vertices[3] = {}; vertices[1].y = vertices[0].y;
			c.start = c.end = vertices[0];
			for (const auto vertex : vertices)
			{
				c.start.x = (std::min)(c.start.x,vertex.x); c.start.y = (std::min)(c.start.y,vertex.y);
				c.end.x = (std::max)(c.end.x,vertex.x); c.end.y = (std::max)(c.end.y,vertex.y);
			}
			shape.transform = ComposeTransforms(shape.transform,{base.unit.x,base.unit.y,base.normal.x,base.normal.y,base.left.x,base.left.y});
		}
		if (constraint == PlanarConstraint::Isosceles)
		{
			const auto base = MakeBaseFrame(vertices[3],vertices[2]);
			const auto topMid = Mul(Add(vertices[0],vertices[1]),.5f);
			const float height = static_cast<float>(Dot(Sub(topMid,base.midpoint),base.normal));
			const float half = Distance(vertices[0],vertices[1]) * .5f;
			if (base.length < minimum || half < minimum || std::abs(height) < minimum) return fail("degenerate trapezoid");
			const auto center = Add(base.midpoint,Mul(base.normal,height));
			vertices[0] = Sub(center,Mul(base.unit,half)); vertices[1] = Add(center,Mul(base.unit,half));
		}
	}
	c.parameters = VertexConstruction{ vertices };
	if (c.tool.preset == PresetKind::Triangle && constraint == PlanarConstraint::Equilateral) SyncEquilateralConstruction(c,vertices);
	return RebuildEdited(std::move(shape),revision,error);
}
bool EditSession::Begin(const ShapeObject& before, uint32_t handle, Vec2 down, const BuildOptions& options)
{
	Cancel(); if (!Validate(before) || !Finite(down)) return false;
	const auto handles = BuildEditHandles(before,options);
	const auto found = std::find_if(handles.begin(),handles.end(),[&](const EditHandle& h) { return h.id == handle; });
	if (handle != 0 && found == handles.end()) return false;
	before_ = before; handleId_ = handle; down_ = lastValid_ = down; options_ = options;
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
	if (canCommit_) { lastValid_ = current; lastValidAngle_ = angleDelta_; return true; }
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
	return true;
}
std::optional<float> EditSession::AngleDelta() const noexcept { return angleActive_ ? std::optional<float>(angleDelta_) : std::nullopt; }
const std::optional<ShapeObject>& EditSession::Candidate() const noexcept { return candidate_; }
std::optional<ShapeObject> EditSession::Commit() { auto result = canCommit_ ? std::move(candidate_) : std::nullopt; Cancel(); return result; }
void EditSession::Cancel() noexcept
{
	before_.reset(); candidate_.reset(); angleActive_ = canCommit_ = false; lastAngle_ = angleDelta_ = lastValidAngle_ = 0;
}
bool ConstructionSession::Begin(const ToolSpec& tool, const ShapeStyle& style, const ObjectId& id, uint64_t revision)
{
	Cancel(); const auto capabilities = GetToolCapabilities(tool.preset);
	if (!capabilities.enabled || !ValidStyle(style) || IsZeroId(id) || !revision || tool.sides < kMinRegularSides || tool.sides > kMaxRegularSides ||
		((tool.preset == PresetKind::Prism || tool.preset == PresetKind::Pyramid) && tool.sides > kMaxSolidSides) ||
		!Finite(tool.rectangleCornerRadius) || tool.rectangleCornerRadius < 0 || (!capabilities.fill && (!style.stroke || style.fill)) || !ValidPlanarConstraint(tool)) return false;
	if (tool.preset != PresetKind::Triangle && tool.preset != PresetKind::Trapezoid &&
		tool.creationMode != DefaultCreationMode(tool.preset) && !(tool.preset == PresetKind::Circle && tool.creationMode == CreationMode::Drag)) return false;
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
		auto vertices = points_;
		if (vertices.empty()) vertices.push_back(down);
		if (!confirm && vertices.back() != end) vertices.push_back(end);
		// 无论最终Polygon是否暂时有效，已确认路径始终用开放Polyline显示。
		return OpenPointCandidate(vertices,style_,id_,revision_,error);
	}
	if (!capabilities.multiStage) return MakePresetShape(tool_, down, end, style_, id_, revision_, error);
	if (!confirmed_)
	{
		if (tool_.preset >= PresetKind::Cuboid) return SolidBasePreview(tool_,down,end,style_,id_,revision_,error);
		ToolSpec base = tool_;
		base.preset = PresetKind::Circle;
		base.creationMode = DefaultCreationMode(base.preset); base.rectangleCornerRadius = 0;
		auto previewStyle = style_;
		if (tool_.preset == PresetKind::Arc || tool_.preset == PresetKind::Sector)
		{
			previewStyle = VisiblePreviewStroke(previewStyle); previewStyle.linePattern = LinePattern::Dashed;
		}
		return MakePresetShape(base, down, end, previewStyle, id_, revision_, error);
	}
	Construction c{ tool_,start_,end_ };
	if (tool_.preset == PresetKind::Arc || tool_.preset == PresetKind::Sector)
	{
		float sweep = sweep_;
		if (!confirm && Distance(start_, end) > 1e-6)
			sweep += static_cast<float>(std::remainder(double(std::atan2(end.y - start_.y, end.x - start_.x) - lastAngle_), 2 * kPi));
		c.parameters = ArcConstruction{ sweep };
	}
	else c = MakeSolidConstruction(tool_,start_,end_,end.y - down.y);
	return MakeConstructedShape(c, style_, id_, revision_, error);
}
bool ConstructionSession::RebuildPoints(std::string* error)
{
	confirmedCandidate_.reset();
	if (points_.empty()) { candidate_.reset(); SetError(error,{}); return true; }
	auto next = OpenPointCandidate(points_,style_,id_,revision_,error);
	if (!next) return false;
	candidate_ = std::move(next);
	Construction final{ tool_,points_.front(),points_.back() }; final.parameters = VertexConstruction{ points_ };
	confirmedCandidate_ = MakeConstructedShape(final,style_,id_,revision_);
	if (candidate_) SetError(error,{});
	return candidate_.has_value();
}
bool ConstructionSession::PreviewDrag(Vec2 down, Vec2 end, std::string* error)
{
	if (pointEdit_)
	{
		if (!Finite(end)) return false;
		const auto previous = points_[*pointEdit_];
		points_[*pointEdit_] = Add(pointEditBefore_[*pointEdit_],Sub(end,pointEditDown_));
		if (RebuildPoints(error)) return true;
		points_[*pointEdit_] = previous; RebuildPoints(); return false;
	}
	auto next = Generate(down, end, false, error);
	// 无效的临时新边不能抹掉已经确认的路径。
	if (next || !GetToolCapabilities(tool_.preset).points) candidate_ = std::move(next);
	return candidate_.has_value();
}
bool ConstructionSession::BeginPointEdit(Vec2 down, float radius)
{
	if (!active_ || finished_ || pointEdit_ || !GetToolCapabilities(tool_.preset).points || !Finite(down) || !Finite(radius) || !(radius > 0)) return false;
	float nearest = radius;
	for (size_t i = 0; i < points_.size(); ++i)
	{
		const float distance = Distance(points_[i],down);
		if (distance <= nearest) { nearest = distance; pointEdit_ = i; }
	}
	if (!pointEdit_) return false;
	pointEditBefore_ = points_; pointEditDown_ = down; return true;
}
bool ConstructionSession::BeginPointEditAt(size_t index, Vec2 down)
{
	if (!active_ || finished_ || pointEdit_ || !GetToolCapabilities(tool_.preset).points || !Finite(down) || index >= points_.size()) return false;
	// UI已按呈现帧选中具体顶点；不能重新最近点命中而误选重叠顶点。
	pointEdit_ = index; pointEditBefore_ = points_; pointEditDown_ = down; return true;
}
bool ConstructionSession::CancelPointEdit()
{
	if (!pointEdit_) return false;
	points_ = std::move(pointEditBefore_); pointEdit_.reset(); return RebuildPoints();
}
bool ConstructionSession::ConfirmDrag(Vec2 down, Vec2 end, std::string* error)
{
	if (!active_ || finished_ || !Finite(down) || !Finite(end)) return false;
	if (GetToolCapabilities(tool_.preset).points)
	{
		if (pointEdit_)
		{
			PreviewDrag(down,end,error);
			if (points_ != pointEditBefore_) { pointUndo_.push_back(pointEditBefore_); ++confirmed_; redo_.clear(); }
			pointEdit_.reset(); pointEditBefore_.clear(); return RebuildPoints(error);
		}
		if (points_.size() >= kMaxVertices || (!points_.empty() && points_.back() == end)) return false;
		const bool firstDrag = points_.empty() && down != end;
		const uint32_t added = firstDrag ? 2u : 1u;
		if (points_.size() > kMaxVertices - added) return false;
		pointUndo_.push_back(points_);
		if (firstDrag) points_.push_back(down);
		points_.push_back(firstDrag ? end : points_.empty() ? down : end);
		++confirmed_; redo_.clear();
		return RebuildPoints(error); // 暂时自交只影响Complete，不能清空已确认的开放路径。
	}
	ObserveRaw(end);
	auto result = Generate(down, end, true, error); if (!result) { candidate_ = confirmedCandidate_; return false; }
	redo_.clear(); candidate_ = confirmedCandidate_ = std::move(result);
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
	const bool points = GetToolCapabilities(tool_.preset).points;
	if (points && pointUndo_.empty()) return false;
	if (pointEdit_) { points_ = pointEditBefore_; pointEdit_.reset(); pointEditBefore_.clear(); }
	BackedStep step; step.start = start_; step.end = end_; step.confirmed = confirmed_; step.lastAngle = lastAngle_;
	step.sweep = sweep_; step.finished = finished_; step.angleStarted = angleStarted_;
	if (points) step.points = points_;
	else step.candidate = confirmedCandidate_;
	// 首步确认后的扫角只属于未确认预览，不能随Forward恢复到下一次真实接触。
	if (!points && !finished_) { step.lastAngle = std::atan2(end_.y - start_.y,end_.x - start_.x); step.sweep = 0; step.angleStarted = true; }
	redo_.push_back(std::move(step));
	if (points)
	{
		points_ = std::move(pointUndo_.back()); pointUndo_.pop_back(); --confirmed_;
		return RebuildPoints();
	}
	if (confirmed_ == 1) { confirmed_ = 0; candidate_.reset(); confirmedCandidate_.reset(); }
	else
	{
		confirmed_ = 0; finished_ = false;
		auto first = Generate(start_, end_, true, nullptr); confirmed_ = 1; candidate_ = confirmedCandidate_ = std::move(first);
	}
	finished_ = false; sweep_ = 0; lastAngle_ = std::atan2(end_.y - start_.y, end_.x - start_.x); angleStarted_ = true; return true;
}
bool ConstructionSession::Forward()
{
	if (!active_ || redo_.empty()) return false;
	CancelPointEdit();
	auto step = std::move(redo_.back()); redo_.pop_back();
	start_ = step.start; end_ = step.end; confirmed_ = step.confirmed; finished_ = step.finished;
	lastAngle_ = step.lastAngle; sweep_ = step.sweep; angleStarted_ = step.angleStarted;
	if (GetToolCapabilities(tool_.preset).points)
	{
		pointUndo_.push_back(points_); points_ = std::move(step.points);
		return RebuildPoints();
	}
	else candidate_ = confirmedCandidate_ = std::move(step.candidate);
	return true;
}
void ConstructionSession::Cancel() noexcept
{
	active_ = finished_ = angleStarted_ = false; confirmed_ = 0; points_.clear(); pointUndo_.clear(); pointEdit_.reset(); pointEditBefore_.clear(); redo_.clear(); candidate_.reset(); confirmedCandidate_.reset(); sweep_ = 0;
}
bool ConstructionSession::CanComplete() const noexcept
{
	const bool points = GetToolCapabilities(tool_.preset).points;
	const uint32_t required = tool_.preset == PresetKind::Polygon ? 3u : 2u;
	return active_ && confirmedCandidate_.has_value() && (points ? points_.size() >= required : finished_);
}
std::optional<ShapeObject> ConstructionSession::Complete()
{
	if (!CanComplete()) return std::nullopt;
	auto result = std::move(confirmedCandidate_); Cancel(); return result;
}
const std::optional<ShapeObject>& ConstructionSession::Candidate() const noexcept { return candidate_; }
ConstructionStatus ConstructionSession::Status() const noexcept
{
	const auto capabilities = GetToolCapabilities(tool_.preset);
	const bool points = capabilities.points;
	return { active_,active_ ? (finished_ ? confirmed_ : confirmed_ + 1) : 0,points ? static_cast<uint32_t>(points_.size()) : confirmed_,
		points ? 0u : capabilities.multiStage ? 2u : 1u,
		CanComplete(),active_ && (points ? !pointUndo_.empty() : confirmed_ > 0),finished_,active_ && !redo_.empty(),tool_.preset };
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
