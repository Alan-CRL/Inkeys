// 在 shape_core.cpp 的私有命名空间中引入，所有预设共用校验与派生路径。
bool ValidPlanarVertices(const ToolSpec& tool, const std::vector<Vec2>& v)
{
	const auto preset = tool.preset;
	if (v.size() != (preset == PresetKind::Triangle ? 3u : 4u)) return false;
	if (!std::all_of(v.begin(), v.end(), [](Vec2 p) { return Finite(p); })) return false;
	auto near = [](double a, double b) { return std::abs(a - b) < 1e-4 * std::max({ 1.0, std::abs(a), std::abs(b) }); };
	if (preset == PresetKind::Triangle)
	{
		if (tool.constraintVertex > 2) return false;
		const auto a = tool.constraintVertex, b = (a + 1) % 3, d = (a + 2) % 3;
		if (tool.constraint == PlanarConstraint::Free) return true;
		const Vec2 base{ v[b].x - v[d].x,v[b].y - v[d].y };
		const double length = std::hypot(base.x,base.y);
		if (!(length > 0)) return false;
		const Vec2 midpoint{ (v[b].x + v[d].x) * .5f,(v[b].y + v[d].y) * .5f };
		const double lateral = double(v[a].x - midpoint.x) * base.x + double(v[a].y - midpoint.y) * base.y;
		return near(lateral / length,0) && (tool.constraint != PlanarConstraint::Equilateral ||
			near(Distance(v[a],v[b]),length) && near(Distance(v[a],v[d]),length));
	}
	if (preset == PresetKind::Trapezoid)
	{
		if (tool.constraint == PlanarConstraint::Free) return near(v[0].y,v[1].y) && near(v[2].y,v[3].y);
		const Vec2 base{ v[2].x - v[3].x,v[2].y - v[3].y }, top{ v[1].x - v[0].x,v[1].y - v[0].y };
		const double baseLength = std::hypot(base.x,base.y), topLength = std::hypot(top.x,top.y);
		if (!(baseLength > 0 && topLength > 0) || !near(double(base.x) * top.y,double(base.y) * top.x) ||
			double(base.x) * top.x + double(base.y) * top.y <= 0) return false;
		const Vec2 unit{ static_cast<float>(base.x / baseLength),static_cast<float>(base.y / baseLength) };
		const Vec2 baseMid{ (v[2].x + v[3].x) * .5f,(v[2].y + v[3].y) * .5f };
		const Vec2 topMid{ (v[0].x + v[1].x) * .5f,(v[0].y + v[1].y) * .5f };
		return near(double(topMid.x - baseMid.x) * unit.x + double(topMid.y - baseMid.y) * unit.y,0) &&
			std::abs(Cross(v[3],v[2],v[0])) > 1e-4;
	}
	if (!near(double(v[0].x) + v[2].x, double(v[1].x) + v[3].x) ||
		!near(double(v[0].y) + v[2].y, double(v[1].y) + v[3].y)) return false;
	if (preset == PresetKind::Rhombus)
		for (size_t i = 1; i < 4; ++i) if (!near(Distance(v[0], v[1]), Distance(v[i], v[(i + 1) % 4]))) return false;
	return true;
}
bool ValidPlanarConstraint(const ToolSpec& tool)
{
	if (tool.constraintVertex > 2 || (tool.preset != PresetKind::Triangle && tool.constraintVertex != 0) ||
		(tool.rightSolid && tool.preset != PresetKind::Prism && tool.preset != PresetKind::Pyramid)) return false;
	if (static_cast<uint32_t>(tool.constraint) > static_cast<uint32_t>(PlanarConstraint::Equilateral)) return false;
	if (tool.preset == PresetKind::Triangle)
		return tool.constraint == PlanarConstraint::Equilateral ? tool.creationMode == CreationMode::CenterRadius : tool.creationMode == CreationMode::Drag;
	if (tool.preset == PresetKind::Trapezoid)
		return tool.constraint != PlanarConstraint::Equilateral && tool.creationMode == CreationMode::Drag;
	return tool.constraint == PlanarConstraint::Free;
}
std::vector<Vec2> RegularPolygonVertices(Vec2 center, Vec2 first, uint32_t sides)
{
	std::vector<Vec2> vertices; vertices.reserve(sides); vertices.push_back(first);
	const double x = double(first.x) - center.x, y = double(first.y) - center.y;
	for (uint32_t i = 1; i < sides; ++i)
	{
		const double angle = 2 * kPi * i / sides;
		vertices.push_back({ static_cast<float>(center.x + x * std::cos(angle) - y * std::sin(angle)),
			static_cast<float>(center.y + x * std::sin(angle) + y * std::cos(angle)) });
	}
	return vertices;
}
std::vector<Vec2> PolygonSolidBase(Vec2 center, Vec2 end, uint32_t sides, float projection)
{
	auto vertices = RegularPolygonVertices(center,{ center.x + end.x - center.x,center.y + 2 * (end.y - center.y) },sides);
	for (auto& point : vertices) point.y = center.y + (point.y - center.y) * projection;
	return vertices;
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
Vec2 ObliquePoint(const SolidConstruction& s, Vec2 p)
{
	const float co = std::cos(s.baseRotation), si = std::sin(s.baseRotation);
	const float x = co*p.x-si*p.y, y = si*p.x+co*p.y;
	return {x+s.projectionScale*std::cos(s.projectionAngle)*y,s.projectionScale*std::sin(s.projectionAngle)*y};
}
std::vector<Vec2> ObliqueBase(const Construction& c, float tolerance)
{
	const auto& s = std::get<SolidConstruction>(c.parameters);
	const auto p = c.tool.preset;
	if (p == PresetKind::Cube || p == PresetKind::Cuboid)
		return {{-s.baseSize.x*.5f,-s.baseSize.y*.5f},{s.baseSize.x*.5f,-s.baseSize.y*.5f},{s.baseSize.x*.5f,s.baseSize.y*.5f},{-s.baseSize.x*.5f,s.baseSize.y*.5f}};
	const bool round = p == PresetKind::Cylinder || p == PresetKind::Cone;
	// 斜投影含剪切，最大奇异值可能大于1；模型空间细分必须计入投影放大。
	const Transform projection{1,0,s.projectionScale*std::cos(s.projectionAngle),s.projectionScale*std::sin(s.projectionAngle),0,0};
	const double radius = double(s.baseSize.x) * MaxScale(projection);
	const double required = round ? std::max(32.0,std::ceil(2*kPi/std::max(1e-8,2*std::acos(std::clamp(1.0-double(tolerance)/radius,-1.0,1.0))))) : c.tool.sides;
	if (required > 16000) return {}; // 精度预算超限不能静默降低精度。
	const auto count = static_cast<uint32_t>(required);
	return RegularPolygonVertices({}, {s.baseSize.x,0},count);
}
bool ExpandOblique(const Construction& c, const ShapeStyle& style, ShapeObject& out, std::string& error, float tolerance)
{
	const auto& s = std::get<SolidConstruction>(c.parameters); const auto p=c.tool.preset;
	if (!Finite(s.baseSize) || !Finite(s.offset) || !Finite(s.baseRotation) || !Finite(s.projectionAngle) || !Finite(s.projectionScale) ||
		!(s.baseSize.x>0 && s.baseSize.y>0 && std::abs(s.height)>0 && s.projectionAngle>.01f && s.projectionAngle<3.13f && s.projectionScale>.01f && s.projectionScale<=1) ||
		(c.tool.rightSolid && s.offset!=Vec2{}) || (p==PresetKind::Cube && (s.baseSize.x!=s.baseSize.y || std::abs(s.height)!=s.baseSize.x)))
	{ error="invalid oblique solid"; return false; }
	const auto base=ObliqueBase(c,tolerance); if (base.size()<3) { error="solid curve exceeds precision budget"; return false; }
	const bool cone=p==PresetKind::Cone || p==PresetKind::Pyramid;
	const bool round=p==PresetKind::Cone || p==PresetKind::Cylinder;
	const auto project=[&](Vec2 v,float h) { const auto q=ObliquePoint(s,v); return Vec2{c.start.x+q.x,c.start.y+q.y+h}; };
	struct V { double x,y,z; };
	auto sub=[](V a,V b){return V{a.x-b.x,a.y-b.y,a.z-b.z};};
	auto cross=[](V a,V b){return V{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
	auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
	const double kx=s.projectionScale*std::cos(s.projectionAngle), ky=s.projectionScale*std::sin(s.projectionAngle);
	const double co=std::cos(s.baseRotation),si=std::sin(s.baseRotation);
	// 斜二测的视线取投影核向量；面法线与该视线共同决定隐藏边。
	const V view{-kx*co+si,kx*si+co,-ky};
	const V interior{s.offset.x*.5,s.offset.y*.5,s.height*.5};
	auto back=[&](V a,V b,V d){auto n=cross(sub(b,a),sub(d,a)); if(dot(n,sub(a,interior))<0)n={-n.x,-n.y,-n.z};return dot(n,view)<-1e-6*std::sqrt(dot(n,n));};
	auto lower=[&](size_t i){return V{base[i].x,base[i].y,0};};
	auto upper=[&](size_t i){return V{base[i].x+s.offset.x,base[i].y+s.offset.y,s.height};};
	const V apex{s.offset.x,s.offset.y,s.height};
	const bool bottomBack=back(lower(0),lower(1),lower(2)), topBack=!cone&&back(upper(0),upper(1),upper(2));
	std::vector<bool> side(base.size());
	for(size_t i=0;i<base.size();++i)side[i]=back(lower(i),lower((i+1)%base.size()),cone?apex:upper(i));
	auto edge=[&](Vec2 a,Vec2 b,bool hidden){ShapePart q; q.id=static_cast<uint32_t>(out.parts.size()+1);q.kind=Kind::Line;q.geometry.start=a;q.geometry.end=b;if(hidden){q.style=style;q.style->linePattern=LinePattern::Dashed;}out.parts.push_back(std::move(q));};
	out.kind=Kind::Compound;
	for(size_t i=0;i<base.size();++i)
	{
		const auto j=(i+1)%base.size(), prev=(i+base.size()-1)%base.size();
		const auto a=project(base[i],0),b=project(base[j],0);
		if(!round)edge(a,b,bottomBack&&side[i]);
		const Vec2 u{base[i].x+s.offset.x,base[i].y+s.offset.y},v{base[j].x+s.offset.x,base[j].y+s.offset.y};
		if(!cone && !round)edge(project(u,s.height),project(v,s.height),topBack&&side[i]);
		if(!round || side[i]!=side[prev])edge(a,project(cone?s.offset:u,s.height),side[i]&&side[prev]);
	}

 if(round)
 {
  // 合并同一可见性的连续圆弧，虚线相位不能在细分弦上反复归零。
  auto ring=[&](bool top)
  {
   const bool face=top?topBack:bottomBack;
   size_t begin=0;
   for(size_t i=0;i<base.size();++i)if((face&&side[i])!=(face&&side[(i+base.size()-1)%base.size()])){begin=i;break;}
   size_t consumed=0;
   while(consumed<base.size())
   {
    const bool hidden=face&&side[(begin+consumed)%base.size()];
    ShapePart q;q.id=static_cast<uint32_t>(out.parts.size()+1);q.kind=Kind::Polyline;
    auto point=[&](size_t i){auto v=base[i%base.size()];if(top){v.x+=s.offset.x;v.y+=s.offset.y;}return project(v,top?s.height:0);};
    q.geometry.vertices.push_back(point(begin+consumed));
    do {++consumed;q.geometry.vertices.push_back(point(begin+consumed));}
    while(consumed<base.size() && (face&&side[(begin+consumed)%base.size()])==hidden);
    if(hidden){q.style=style;q.style->linePattern=LinePattern::Dashed;}
    out.parts.push_back(std::move(q));
   }
  };
  ring(false);if(!cone)ring(true);
 }
 return true;
}
bool ExpandAdvanced(const Construction& c, const ShapeStyle& style, ShapeObject& out,
	std::string& error, float tolerance = .25f, bool legacyCone = false, bool legacyPolygonSolid = false)
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
	const bool polygonSolid = p == PresetKind::Prism || p == PresetKind::Pyramid;
	if (!solid || !Finite(solid->depth) || !Finite(solid->height) || !Finite(solid->baseProjection) || !style.stroke || style.fill ||
		(polygonSolid ? solid->baseProjection < kMinSolidBaseProjection || solid->baseProjection > kMaxSolidBaseProjection :
			solid->baseProjection != kDefaultSolidBaseProjection))
	{ error = "solid requires valid stroke-only parameters"; return false; }
	if (solid->oblique) return ExpandOblique(c,style,out,error,tolerance);
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
			if (legacyCone) { edge({ l,center.y },apex,false); edge({ r,center.y },apex,false); }
			else if (std::abs(height.y) > axes.y)
			{
				// 椭圆切点满足x²/rx²+y²/ry²=1及切线经过锥顶；可见性约定不变。
				const float ratio = axes.y / height.y;
				const float x = axes.x * std::sqrt((std::max)(0.0f,1 - ratio * ratio)), y = axes.y * ratio;
				edge({ center.x - x,center.y + y },apex,false); edge({ center.x + x,center.y + y },apex,false);
			}
			// 锥顶投影在椭圆内时没有实切线，只保留底面轮廓，编辑时钳在外部。
		}
		return true;
	}
	if (!polygonSolid) { error = "unknown advanced preset"; return false; }
	if (!(Distance(c.start,c.end) > 0)) { error = "empty polygon base"; return false; }
	const auto unprojected = RegularPolygonVertices(c.start,{ c.end.x,c.start.y + 2 * (c.end.y - c.start.y) },c.tool.sides);
	const auto base = PolygonSolidBase(c.start,c.end,c.tool.sides,solid->baseProjection);
	if (legacyPolygonSolid)
	{
		if (solid->baseProjection != kDefaultSolidBaseProjection) { error = "legacy projection mismatch"; return false; }
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
	struct Vec3 { double x, y, z; };
	auto add3 = [](Vec3 a, Vec3 b) { return Vec3{ a.x + b.x,a.y + b.y,a.z + b.z }; };
	auto sub3 = [](Vec3 a, Vec3 b) { return Vec3{ a.x - b.x,a.y - b.y,a.z - b.z }; };
	auto cross3 = [](Vec3 a, Vec3 b) { return Vec3{ a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; };
	auto dot3 = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	const double projection = solid->baseProjection;
	const double depth = std::sqrt((std::max)(0.0,1 - projection * projection));
	std::vector<Vec3> lower, upper; lower.reserve(base.size()); upper.reserve(base.size());
	for (const auto point : unprojected)
	{
		const double dy = point.y - c.start.y;
		lower.push_back({ point.x,c.start.y + projection * dy,depth * dy });
		upper.push_back({ point.x,c.start.y + projection * dy + solid->height,depth * dy });
	}
	const Vec3 apex{ c.start.x,c.start.y + solid->height,0 };
	Vec3 interior{};
	for (const auto point : lower) interior = add3(interior,point);
	if (p == PresetKind::Prism) for (const auto point : upper) interior = add3(interior,point);
	else interior = add3(interior,apex);
	const double interiorCount = p == PresetKind::Prism ? 2.0 * base.size() : base.size() + 1.0;
	interior = { interior.x / interiorCount,interior.y / interiorCount,interior.z / interiorCount };
	const Vec3 view{ 0,0,1 };
	auto backFacing = [&](const std::vector<Vec3>& face)
	{
		auto normal = cross3(sub3(face[1],face[0]),sub3(face[2],face[0]));
		Vec3 center{}; for (const auto point : face) center = add3(center,point);
		center = { center.x / face.size(),center.y / face.size(),center.z / face.size() };
		if (dot3(normal,sub3(center,interior)) < 0) normal = { -normal.x,-normal.y,-normal.z };
		const double length = std::sqrt(dot3(normal,normal));
		// 共面及阈值内都按可见处理；只有所有相邻面稳定背向才画虚线。
		return length > 0 && dot3(normal,view) < -1e-6 * length;
	};
	const bool lowerBack = backFacing(lower), upperBack = p == PresetKind::Prism && backFacing(upper);
	std::vector<bool> sideBack(base.size());
	for (size_t i = 0; i < base.size(); ++i)
	{
		const size_t next = (i + 1) % base.size();
		sideBack[i] = p == PresetKind::Prism ? backFacing({ lower[i],lower[next],upper[next],upper[i] }) :
			backFacing({ lower[i],lower[next],apex });
		edge(base[i],base[next],lowerBack && sideBack[i]);
		if (p == PresetKind::Prism)
			edge(shift(base[i],height),shift(base[next],height),upperBack && sideBack[i]);
	}
	for (size_t i = 0; i < base.size(); ++i)
		edge(base[i],p == PresetKind::Prism ? shift(base[i],height) : shift(c.start,height),
			sideBack[i] && sideBack[(i + base.size() - 1) % base.size()]);
	return true;
}
