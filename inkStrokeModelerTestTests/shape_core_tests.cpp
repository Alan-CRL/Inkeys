#include "../inkStrokeModelerTest/draw3/shape_core.h"
#include "../inkStrokeModelerTest/draw3/shape_interaction.h"

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
	void RunAuxiliaryTests(int& failures)
	{
		for (auto preset : {PresetKind::Circle,PresetKind::Ellipse,PresetKind::Rectangle,PresetKind::RoundedRectangle,
			PresetKind::Parallelogram,PresetKind::Rhombus,PresetKind::Trapezoid,PresetKind::RegularPolygon,PresetKind::Arc,PresetKind::Sector})
		{
			Construction c; c.tool = {preset,DefaultCreationMode(preset),5}; c.start = {20,30}; c.end = {140,110};
			if (preset == PresetKind::Arc || preset == PresetKind::Sector) c.parameters = ArcConstruction{1.5f};
			ShapeStyle style; style.width = 3; style.strokeRgb = 0x123456; style.strokeOpacity = .4f;
			style.auxiliary = SupportedAuxiliary(preset);
			auto shape = MakeConstructedShape(c,style,Id(51)); SHAPE_CHECK(shape); if (!shape) continue;
			shape->transform = {0,2,-2,0,300,10};
			GeometryResult aux, body; RenderData render;
			SHAPE_CHECK(BuildAuxiliaryGeometry(*shape,aux) && BuildGeometry(*shape,body) && BuildRenderData(*shape,render));
			const bool diagonal = style.auxiliary == Auxiliary::Diagonals;
			SHAPE_CHECK(aux.parts.size() == (diagonal || preset == PresetKind::Ellipse ? 2u : 1u));
			SHAPE_CHECK(std::count_if(render.batches.begin(),render.batches.end(),[](const RenderBatch& b) { return b.auxiliary; }) == aux.parts.size());
			for (const auto& part : aux.parts)
			{
				SHAPE_CHECK(part.controls.empty());
				if (diagonal)
				{
					SHAPE_CHECK(part.kind == Kind::Line && part.style.strokeRgb == style.strokeRgb && part.style.width == 6);
					SHAPE_CHECK(part.style.linePattern == LinePattern::Dashed && part.paths.front().points.front() == ApplyTransform(shape->transform,part.geometry.start));
					SHAPE_CHECK(!IsDashVisible(7 * part.style.width,part.style));
				}
				else SHAPE_CHECK(part.kind == Kind::Circle && part.style.fill && !part.style.stroke && part.geometry.radius == 6 && part.style.fillRgb == style.strokeRgb);
			}
			if (preset == PresetKind::Rectangle)
			{
				SHAPE_CHECK(aux.parts[0].geometry.start == shape->geometry.start && aux.parts[0].geometry.end == shape->geometry.end);
				SHAPE_CHECK(HitTest(*shape,body,ApplyTransform(shape->transform,{80,70})).kind == HitKind::None);
			}
			if (preset == PresetKind::Circle)
			{
				SHAPE_CHECK(aux.parts[0].geometry.start == ApplyTransform(shape->transform,shape->geometry.start));
				SHAPE_CHECK(HitTest(*shape,body,aux.parts[0].geometry.start).kind == HitKind::None);
			}
			if (preset == PresetKind::Ellipse)
			{
				const auto handles = BuildEditHandles(*shape);
				for (size_t i = 0; i < 2; ++i)
				{
					const auto focus = std::find_if(handles.begin(),handles.end(),[&](const EditHandle& h) { return h.id == 106 + i; });
					SHAPE_CHECK(focus != handles.end() && focus->position == aux.parts[i].geometry.start);
				}
			}
			shape->style.stroke = false; shape->style.fill = preset != PresetKind::Arc;
			if (shape->style.fill)
			{
				shape->style.fillRgb = 0xabcdef; shape->style.fillOpacity = .2f;
				SHAPE_CHECK(BuildAuxiliaryGeometry(*shape,aux) && aux.parts.empty());
				SHAPE_CHECK(shape->style.auxiliary == SupportedAuxiliary(preset) && BuildGeometry(*shape,body));
			}
		}
		for (auto preset : {PresetKind::Circle,PresetKind::Ellipse})
		{
			ShapeStyle style; style.width = .5f; style.auxiliary = DefaultAuxiliary(preset);
			const auto shape = MakePresetShape({preset,DefaultCreationMode(preset)},{10,20},{50,40},style,Id(52));
			GeometryResult aux; SHAPE_CHECK(shape && BuildAuxiliaryGeometry(*shape,aux,{2,.25f}));
			SHAPE_CHECK(aux.parts.size() == (preset == PresetKind::Circle ? 1u : 2u));
			// 最小点径固定为4画布像素，200%显示比例下也不放大。
			if (!aux.parts.empty()) SHAPE_CHECK(aux.parts[0].geometry.radius == 2);
		}
		ShapeStyle unsupported; unsupported.auxiliary = Auxiliary::Center | Auxiliary::Foci | Auxiliary::Diagonals;
		const auto line = MakePresetShape({}, {0,0},{100,0},unsupported,Id(53)); GeometryResult aux;
		SHAPE_CHECK(line && BuildAuxiliaryGeometry(*line,aux) && aux.parts.empty());
		SHAPE_CHECK(DefaultAuxiliary(PresetKind::RegularPolygon) == Auxiliary::None && DefaultAuxiliary(PresetKind::Rectangle) == Auxiliary::None);
	}
	void RunEditInteractionTests(int& failures)
	{
		constexpr float pi = 3.14159265358979323846f;
		auto near = [](float a, float b) { return std::abs(a - b) < .003f; };
		auto pointNear = [&](Vec2 a, Vec2 b) { return near(a.x,b.x) && near(a.y,b.y); };
		auto length = [](Vec2 a, Vec2 b) { return std::hypot(a.x - b.x,a.y - b.y); };
		auto getHandle = [](const std::vector<EditHandle>& handles, uint32_t id) -> std::optional<EditHandle> {
			const auto found = std::find_if(handles.begin(),handles.end(),[&](const EditHandle& h) { return h.id == id; });
			return found == handles.end() ? std::nullopt : std::optional<EditHandle>(*found);
		};
		auto fixture = [&](PresetKind preset) {
			Construction c; c.tool = { preset,DefaultCreationMode(preset),5 }; c.start = { 20,30 }; c.end = { 140,110 };
			if (GetToolCapabilities(preset).points) c.parameters = VertexConstruction{ { {20,30},{140,30},{140,110},{20,110} } };
			else if (preset == PresetKind::Arc || preset == PresetKind::Sector) { c.end = {140,30}; c.parameters = ArcConstruction{pi * .5f}; }
			else if (preset >= PresetKind::Cuboid) c=MakeSolidConstruction(c.tool,c.start,c.end,-120);
			return MakeConstructedShape(c,{},Id(25));
		};
		for (uint32_t value = 0; value < uint32_t(PresetKind::Cuboid); ++value)
		{
			const auto preset = static_cast<PresetKind>(value); const auto object = fixture(preset);
			SHAPE_CHECK(object); if (!object) continue;
			const auto& c = *object->construction;
			std::vector<EditHandle> expected;
			auto add = [&](uint32_t id, EditHandleKind kind, Vec2 p) { expected.push_back({id,kind,p}); };
			auto vertices = [&](std::initializer_list<Vec2> points) { uint32_t id = 1000; for (auto p : points) add(id++,EditHandleKind::Vertex,p); };
			bool rotate = true;
			switch (preset)
			{
			case PresetKind::Line: case PresetKind::Arrow:
				add(1000,EditHandleKind::Endpoint,{20,30}); add(1001,EditHandleKind::Endpoint,{140,110}); rotate = false; break;
			case PresetKind::Rectangle: case PresetKind::RoundedRectangle:
				vertices({{20,30},{140,30},{140,110},{20,110}}); break;
			case PresetKind::Circle:
				add(100,EditHandleKind::Center,{20,30}); add(101,EditHandleKind::Radius,{140,110}); break;
			case PresetKind::Ellipse:
				add(100,EditHandleKind::Center,{80,70}); add(102,EditHandleKind::Axis,{140,70}); add(103,EditHandleKind::Axis,{80,110});
				add(106,EditHandleKind::Focus,{80+std::sqrt(2000.f),70}); add(107,EditHandleKind::Focus,{80-std::sqrt(2000.f),70}); break;
			case PresetKind::Triangle: vertices({{80,30},{140,110},{20,110}}); break;
			case PresetKind::Parallelogram: vertices({{20,30},{110,30},{140,110},{50,110}}); break;
			case PresetKind::Rhombus: vertices({{80,30},{140,70},{80,110},{20,70}}); break;
			case PresetKind::Trapezoid: vertices({{50,30},{110,30},{140,110},{20,110}}); break;
			case PresetKind::RegularPolygon:
				add(100,EditHandleKind::Center,{20,30}); add(1000,EditHandleKind::Vertex,{140,110}); break;
			case PresetKind::Polyline: case PresetKind::Polygon:
				vertices({{20,30},{140,30},{140,110},{20,110}}); break;
			case PresetKind::Arc: case PresetKind::Sector:
				add(100,EditHandleKind::Center,{20,30}); add(101,EditHandleKind::Radius,{20 + 120 * std::cos(pi / 4),30 + 120 * std::sin(pi / 4)});
				add(102,EditHandleKind::Angle,{140,30}); add(103,EditHandleKind::Angle,{20,150}); break;
			case PresetKind::Cuboid:
				vertices({{20,30},{140,30},{140,110},{20,110}}); add(104,EditHandleKind::Depth,{170,-10}); break;
			case PresetKind::Cube:
				add(1001,EditHandleKind::Vertex,{140,150}); add(104,EditHandleKind::Depth,{170,-10}); break;
			case PresetKind::Cylinder: case PresetKind::Cone:
				add(1000,EditHandleKind::Axis,{20,70}); add(1001,EditHandleKind::Axis,{140,70}); add(1002,EditHandleKind::Axis,{80,110});
				add(105,EditHandleKind::Height,{80,-50}); break;
			case PresetKind::Prism: case PresetKind::Pyramid:
				add(1001,EditHandleKind::Vertex,{140,110}); add(102,EditHandleKind::Axis,{20,-12}); add(105,EditHandleKind::Height,{20,-90}); break;
			}
			GeometryResult geometry; SHAPE_CHECK(BuildGeometry(*object,geometry));
			for (float displayScale : {1.f,2.f})
			{
				Bounds frame; const auto handles = BuildEditHandles(*object,{displayScale,.25f},&frame);
				GeometryResult local; SHAPE_CHECK(BuildGeometry(*object,local,{displayScale,.25f}));
				SHAPE_CHECK(frame.valid && near(frame.left,local.bounds.left - 8 * displayScale) && near(frame.right,local.bounds.right + 8 * displayScale));
				SHAPE_CHECK(near(frame.top,local.bounds.top - 8 * displayScale) && near(frame.bottom,local.bounds.bottom + 8 * displayScale));
				auto all = expected;
				if (preset >= PresetKind::Cuboid)
				{
					all.push_back({10,EditHandleKind::Scale,{frame.left,frame.top}}); all.push_back({12,EditHandleKind::Scale,{frame.right,frame.top}});
					all.push_back({14,EditHandleKind::Scale,{frame.right,frame.bottom}}); all.push_back({16,EditHandleKind::Scale,{frame.left,frame.bottom}});
				}
				if (rotate) all.push_back({1,EditHandleKind::Rotate,{(frame.left + frame.right) * .5f,frame.top - 24 * displayScale}});
				SHAPE_CHECK(handles.size() == all.size());
				for (const auto& wanted : all)
				{
					const auto handle = getHandle(handles,wanted.id);
					SHAPE_CHECK(handle && handle->kind == wanted.kind && pointNear(handle->position,wanted.position));
				}
				SHAPE_CHECK(!getHandle(handles,11) && !getHandle(handles,13) && !getHandle(handles,15) && !getHandle(handles,17));
			}
			const auto handles = BuildEditHandles(*object);
			for (const auto& h : handles)
			{
				const Vec2 delta{3,2}; const auto edited = EditShape(*object,h.id,h.position,{h.position.x + delta.x,h.position.y + delta.y},2);
				SHAPE_CHECK(edited && Validate(*edited)); if (!edited) continue;
				SHAPE_CHECK(edited->id == object->id && edited->revision == 2 && edited->style == object->style && edited->construction->tool.preset == preset);
				const auto& next = *edited->construction;
				if ((h.kind == EditHandleKind::Vertex && preset != PresetKind::Cube && preset != PresetKind::Rhombus) ||
					h.kind == EditHandleKind::Endpoint || h.kind == EditHandleKind::Depth)
				{
					const auto actual = getHandle(BuildEditHandles(*edited),h.id);
					SHAPE_CHECK(actual && pointNear(actual->position,{h.position.x + 3,h.position.y + 2}));
				}
				if (h.kind == EditHandleKind::Axis && (preset == PresetKind::Cylinder || preset == PresetKind::Cone))
				{
					SHAPE_CHECK(pointNear({(next.start.x + next.end.x) * .5f,(next.start.y + next.end.y) * .5f},{80,70}));
					SHAPE_CHECK(next.start.x < next.end.x && next.start.y < next.end.y);
					if (h.id == 1002) SHAPE_CHECK(near(next.end.y - next.start.y,84) && near(next.end.x - next.start.x,120));
					else SHAPE_CHECK(near(next.end.x - next.start.x,h.id == 1000 ? 114.f : 126.f) && near(next.end.y - next.start.y,80));
				}
				if (h.kind == EditHandleKind::Center)
					SHAPE_CHECK(edited->construction == object->construction && edited->transform.dx == 3 && edited->transform.dy == 2);
				if (h.kind == EditHandleKind::Vertex && (preset == PresetKind::Rectangle || preset == PresetKind::RoundedRectangle || preset == PresetKind::Cuboid))
				{
					const uint32_t opposite = 1000 + (h.id - 1000 + 2) % 4;
					const auto fixed = getHandle(handles,opposite), actual = getHandle(BuildEditHandles(*edited),opposite);
					SHAPE_CHECK(fixed && actual && pointNear(fixed->position,actual->position));
					SHAPE_CHECK(next.tool.rectangleCornerRadius == c.tool.rectangleCornerRadius);
				}
				if (h.kind == EditHandleKind::Vertex && preset == PresetKind::Parallelogram)
				{
					const auto& old = object->geometry.vertices; const auto& v = std::get<VertexConstruction>(next.parameters).vertices; const size_t i = h.id - 1000;
					SHAPE_CHECK(pointNear(v[(i + 2) % 4],old[(i + 2) % 4]));
					for (size_t j : {(i + 1) % 4,(i + 3) % 4}) {
						const auto o=(i+2)%4; const Vec2 a{old[j].x-old[o].x,old[j].y-old[o].y},b{v[j].x-v[o].x,v[j].y-v[o].y};
						SHAPE_CHECK(std::abs(a.x*b.y-a.y*b.x)<.01f);
					}
					SHAPE_CHECK(pointNear({v[0].x + v[2].x,v[0].y + v[2].y},{v[1].x + v[3].x,v[1].y + v[3].y}));
				}
				if (h.kind == EditHandleKind::Vertex && preset == PresetKind::Rhombus)
				{
					const auto& old = object->geometry.vertices; const auto& v = std::get<VertexConstruction>(next.parameters).vertices; const size_t i = h.id - 1000;
					SHAPE_CHECK(v[(i + 1) % 4] == old[(i + 1) % 4] && v[(i + 3) % 4] == old[(i + 3) % 4]);
					for (size_t j = 1; j < 4; ++j) SHAPE_CHECK(near(length(v[0],v[1]),length(v[j],v[(j + 1) % 4])));
				}
				if (h.kind == EditHandleKind::Vertex && preset == PresetKind::Trapezoid)
				{
					const auto& v = std::get<VertexConstruction>(next.parameters).vertices;
					SHAPE_CHECK(near(v[0].y,v[1].y) && near(v[2].y,v[3].y) && v[0].y < v[2].y);
				}
				if (preset == PresetKind::Cube) SHAPE_CHECK(near(std::abs(next.end.x - next.start.x),std::abs(next.end.y - next.start.y)));
				if (h.kind == EditHandleKind::Scale && (preset == PresetKind::Cube || preset == PresetKind::Prism || preset == PresetKind::Pyramid))
				{
					const float sx = (next.end.x - next.start.x) / (c.end.x - c.start.x), sy = (next.end.y - next.start.y) / (c.end.y - c.start.y);
					SHAPE_CHECK(near(sx,sy));
				}
				if (h.kind == EditHandleKind::Height)
					SHAPE_CHECK(next.start == c.start && next.end == c.end && near(std::get<SolidConstruction>(next.parameters).height,std::get<SolidConstruction>(c.parameters).height + 2));
			}
		}
		// 每种模板都覆盖退化预览与最终抬手；有效预览后再进入无效区也不能提交旧候选。
		for (uint32_t value = 0; value < uint32_t(PresetKind::Cuboid); ++value)
		{
			const auto preset = static_cast<PresetKind>(value); const auto object = fixture(preset); if (!object) continue;
			uint32_t id = 1000; Vec2 invalid{80,70};
			switch (preset)
			{
			case PresetKind::Line: case PresetKind::Arrow: id = 1001; invalid = {20,30}; break;
			case PresetKind::Rectangle: case PresetKind::RoundedRectangle: invalid = {140,110}; break;
			case PresetKind::Circle: id = 101; invalid = object->geometry.start; break;
			case PresetKind::Ellipse: id = 103; break;
			case PresetKind::Triangle: invalid = {80,110}; break;
			case PresetKind::Parallelogram: invalid = {140,110}; break;
			case PresetKind::Rhombus: break;
			case PresetKind::Trapezoid: invalid = {50,110}; break;
			case PresetKind::RegularPolygon: invalid = {20,30}; break;
			case PresetKind::Polyline: invalid = {140,30}; break;
			case PresetKind::Polygon: invalid = {160,70}; break;
			case PresetKind::Arc: case PresetKind::Sector: id = 101; invalid = {20,30}; break;
			case PresetKind::Cube: id = 1001; invalid = {20,30}; break;
			case PresetKind::Cylinder: invalid = {80,70}; break;
			case PresetKind::Cone: id = 105; invalid = {80,50}; break;
			case PresetKind::Prism: case PresetKind::Pyramid: id = 1001; invalid = {20,30}; break;
			}
			const auto handle = getHandle(BuildEditHandles(*object),id); SHAPE_CHECK(handle); if (!handle) continue;
			EditSession session; SHAPE_CHECK(session.Begin(*object,id,handle->position));
			SHAPE_CHECK(session.Preview({handle->position.x + 1,handle->position.y + 1},2));
			SHAPE_CHECK(!EditShape(*object,id,handle->position,invalid,3));
			SHAPE_CHECK(session.Preview(invalid,3) && session.Candidate() && Validate(*session.Candidate()));
			SHAPE_CHECK(session.Candidate() && session.Candidate()->id == object->id && session.Candidate()->revision == 3);
			SHAPE_CHECK(!session.Commit() && !session.Candidate());
			SHAPE_CHECK(session.Begin(*object,id,handle->position) && session.Preview(invalid,4));
			SHAPE_CHECK(session.Preview({handle->position.x + 1,handle->position.y + 1},5) && session.Commit());
		}
		for (auto mode : {CreationMode::Drag,CreationMode::CenterRadius})
		{
			const auto circle = MakePresetShape({PresetKind::Circle,mode},{20,30},{140,110},{},Id(26)); SHAPE_CHECK(circle); if (!circle) continue;
			const auto center = circle->geometry.start; const float radius = circle->geometry.radius;
			const auto handles = BuildEditHandles(*circle); const auto handle = getHandle(handles,101); SHAPE_CHECK(handle); if (!handle) continue;
			if (mode == CreationMode::Drag) SHAPE_CHECK(pointNear(handle->position,{center.x + radius / std::sqrt(2.f),center.y + radius / std::sqrt(2.f)}));
			const auto unchanged = EditShape(*circle,101,handle->position,handle->position,2);
			SHAPE_CHECK(unchanged && pointNear(unchanged->geometry.start,center) && near(unchanged->geometry.radius,radius) && unchanged->creationMode == CreationMode::CenterRadius);
			EditSession session; const Vec2 down{handle->position.x + 5,handle->position.y - 3}; SHAPE_CHECK(session.Begin(*circle,101,down));
			uint64_t revision = 2;
			for (Vec2 direction : {Vec2{-12,9},Vec2{-13,-10},Vec2{5,-15},Vec2{0,20}})
			{
				const Vec2 wanted{center.x + direction.x,center.y + direction.y};
				SHAPE_CHECK(session.Preview({wanted.x + 5,wanted.y - 3},revision++)); const auto& next = session.Candidate();
				SHAPE_CHECK(next && next->construction->tool.creationMode == CreationMode::CenterRadius && pointNear(next->construction->start,center));
				if (next)
				{
					const auto radiusHandle = getHandle(BuildEditHandles(*next),101);
					SHAPE_CHECK(radiusHandle && pointNear(radiusHandle->position,wanted) && pointNear(next->construction->end,wanted));
					SHAPE_CHECK(near(next->geometry.radius,length(center,wanted)));
				}
			}
			SHAPE_CHECK(session.Commit());
		}
		// 对象焦点开关只控制辅助显示，焦点始终可编辑；旧纵向/仿射椭圆也以真实画布长短轴生成句柄。
		for (bool vertical : {false,true}) for (bool affine : {false,true})
		{
			auto ellipse = MakePresetShape({PresetKind::Ellipse,CreationMode::Drag},{20,30},vertical ? Vec2{100,150} : Vec2{140,110},{},Id(27));
			SHAPE_CHECK(ellipse); if (!ellipse) continue;
			if (affine) ellipse->transform = {1.5f,.3f,.4f,1.1f,15,25};
			const BuildOptions options{1,.25f}; const auto normal = BuildEditHandles(*ellipse,options);
			ellipse->style.auxiliary = Auxiliary::Foci; const auto handles = BuildEditHandles(*ellipse);
			SHAPE_CHECK(normal.size() == 6 && handles.size() == 6 && getHandle(normal,106) && getHandle(normal,107));
			SHAPE_CHECK(pointNear(getHandle(normal,106)->position,getHandle(handles,106)->position) && pointNear(getHandle(normal,107)->position,getHandle(handles,107)->position));
			const auto center = getHandle(handles,100), major = getHandle(handles,102), f1 = getHandle(handles,106), f2 = getHandle(handles,107);
			SHAPE_CHECK(center && major && f1 && f2); if (!center || !major || !f1 || !f2) continue;
			const float a = length(center->position,major->position);
			for (uint32_t id : {106u,107u})
			{
				const auto source = id == 106 ? f1 : f2; const float sign = id == 106 ? 1.f : -1.f;
				const Vec2 target{center->position.x + sign * a * .3f,center->position.y + sign * a * .4f};
				const auto edited = EditShape(*ellipse,id,source->position,target,2,options); SHAPE_CHECK(edited && Validate(*edited)); if (!edited) continue;
				const auto changed = BuildEditHandles(*edited,options); const auto newCenter = getHandle(changed,100), newMajor = getHandle(changed,102), newMinor = getHandle(changed,103);
				const auto newF1 = getHandle(changed,106), newF2 = getHandle(changed,107), dragged = getHandle(changed,id);
				SHAPE_CHECK(newCenter && newMajor && newMinor && newF1 && newF2 && dragged); if (!newCenter || !newMajor || !newMinor || !newF1 || !newF2 || !dragged) continue;
				SHAPE_CHECK(pointNear(newCenter->position,center->position) && near(length(newCenter->position,newMajor->position),a));
				SHAPE_CHECK(pointNear(dragged->position,target) && pointNear({newF1->position.x + newF2->position.x,newF1->position.y + newF2->position.y},{2 * center->position.x,2 * center->position.y}));
				SHAPE_CHECK(near(length(newCenter->position,newMinor->position),std::sqrt(a * a - a * a * .25f)));
				SHAPE_CHECK(near(length(newMajor->position,newF1->position) + length(newMajor->position,newF2->position),2 * a));
				SHAPE_CHECK(edited->transform != ellipse->transform);
			}
			EditSession focus; SHAPE_CHECK(focus.Begin(*ellipse,106,f1->position,options));
			SHAPE_CHECK(focus.Preview({center->position.x + a * 2,center->position.y},3) && focus.Candidate() && Validate(*focus.Candidate()));
			if (focus.Candidate())
			{
				const auto changed = BuildEditHandles(*focus.Candidate(),options); const auto actual = getHandle(changed,106);
				SHAPE_CHECK(actual && length(actual->position,center->position) < a && focus.Candidate()->geometry.end.y > 0);
			}
			SHAPE_CHECK(!focus.Commit());
		}

		for (PresetKind preset : {PresetKind::Arc,PresetKind::Sector}) for (float sign : {-1.f,1.f})
		{
			Construction c; c.tool = {preset,CreationMode::MultiStage}; c.start = {}; c.end = {0,100}; c.parameters = ArcConstruction{sign * pi / 180};
			const auto arc = MakeConstructedShape(c,{},Id(28)); SHAPE_CHECK(arc); if (!arc) continue;
			const auto end = getHandle(BuildEditHandles(*arc),103); SHAPE_CHECK(end); if (!end) continue;
			EditSession local, coalesced; SHAPE_CHECK(local.Begin(*arc,103,end->position) && coalesced.Begin(*arc,103,end->position));
			Vec2 final; uint64_t revision = 2;
			// 起角90°，终角沿轨迹到300°：sweep=210°；反向轨迹对称验证。
			for (unsigned degrees = 2; degrees <= 210; ++degrees)
			{
				const float angle = pi * .5f + sign * degrees * pi / 180;
				final = {100 * std::cos(angle),100 * std::sin(angle)};
				SHAPE_CHECK(local.Preview(final,revision++));
			}
			SHAPE_CHECK(local.Candidate() && near(std::get<ArcConstruction>(local.Candidate()->construction->parameters).sweepRadians,sign * 210 * pi / 180));
			// 只传最终一条意图，累计角仍完整；Commit不依赖中间预览是否被覆盖。
			SHAPE_CHECK(coalesced.Preview(final,revision,nullptr,local.AngleDelta()));
			const auto actual = coalesced.Commit(), expected = local.Commit();
			SHAPE_CHECK(actual && expected && near(std::get<ArcConstruction>(actual->construction->parameters).sweepRadians,std::get<ArcConstruction>(expected->construction->parameters).sweepRadians));
			EditSession overflow; SHAPE_CHECK(overflow.Begin(*arc,103,end->position));
			SHAPE_CHECK(overflow.Preview(end->position,revision + 1,nullptr,sign * 3 * pi) && overflow.Candidate() && Validate(*overflow.Candidate()));
			SHAPE_CHECK(!overflow.Commit());
			const auto full = EditShape(*arc,103,end->position,end->position,revision + 2,{},nullptr,sign * (2 * pi - pi / 180));
			SHAPE_CHECK(full && near(std::abs(std::get<ArcConstruction>(full->construction->parameters).sweepRadians),2 * pi));
			const float crossedAngle = pi * .5f - sign * pi * .5f;
			const Vec2 crossedPoint{100 * std::cos(crossedAngle),100 * std::sin(crossedAngle)};
			const auto crossed = EditShape(*arc,103,end->position,crossedPoint,revision + 3,{},nullptr,-sign * 91 * pi / 180);
			SHAPE_CHECK(crossed && Validate(*crossed)); if (crossed)
			{
				const auto& crossedConstruction = *crossed->construction;
				const auto& crossedSweep = std::get<ArcConstruction>(crossedConstruction.parameters).sweepRadians;
				SHAPE_CHECK(near(crossedSweep,sign * pi * .5f));
				SHAPE_CHECK(pointNear(crossedConstruction.end,crossedPoint));
				const auto newEnd = getHandle(BuildEditHandles(*crossed),103);
				SHAPE_CHECK(newEnd && pointNear(newEnd->position,{0,100}));
			}
			EditSession crossing;
			SHAPE_CHECK(crossing.Begin(*arc,103,end->position));
			SHAPE_CHECK(crossing.Preview(crossedPoint,revision + 4,nullptr,-sign * 91 * pi / 180));
			const auto committedCross = crossing.Commit();
			SHAPE_CHECK(committedCross && crossed && committedCross->construction == crossed->construction);
		}
		ToolSpec triangleTool{ PresetKind::Triangle,CreationMode::Drag };
		const auto freeTriangle = MakePresetShape(triangleTool,{20,30},{140,110},{},Id(40));
		SHAPE_CHECK(freeTriangle && freeTriangle->construction->tool.constraint == PlanarConstraint::Free);
		if (freeTriangle)
		{
			const auto& initial = freeTriangle->geometry.vertices;
			SHAPE_CHECK(near(length(initial[0],initial[1]),length(initial[0],initial[2])));
			const auto moved = EditShape(*freeTriangle,1000,initial[0],{95,55},2);
			SHAPE_CHECK(moved && moved->construction->tool.constraint == PlanarConstraint::Free);
			if (moved)
			{
				const auto iso = SetPlanarConstraint(*moved,PlanarConstraint::Isosceles,3);
				SHAPE_CHECK(iso && Validate(*iso)); if (iso)
				{
					const auto& old = moved->geometry.vertices; const auto& v = iso->geometry.vertices;
					const auto apex = iso->construction->tool.constraintVertex, left = (apex+1)%3, right = (apex+2)%3;
					SHAPE_CHECK(v[left] == old[left] && v[right] == old[right] && near(length(v[apex],v[left]),length(v[apex],v[right])));
					SHAPE_CHECK(iso->construction->tool.creationMode == CreationMode::Drag);
					for (uint32_t id = 1000; id <= 1002; ++id)
					{
						const auto handle = getHandle(BuildEditHandles(*iso),id); SHAPE_CHECK(handle);
						if (handle)
						{
							std::string reason; const auto edited = EditShape(*iso,id,handle->position,{handle->position.x + 3,handle->position.y + 2},4,{},&reason);
							if (!edited) std::cerr << "isosceles edit " << id << ": " << reason << '\n';
							SHAPE_CHECK(edited && Validate(*edited));
						}
					}
					const auto equilateral = SetPlanarConstraint(*iso,PlanarConstraint::Equilateral,5);
					SHAPE_CHECK(equilateral && Validate(*equilateral)); if (equilateral)
					{
						const auto& before = iso->geometry.vertices; const auto& v3 = equilateral->geometry.vertices;
						SHAPE_CHECK(v3[left] == before[left] && v3[right] == before[right]);
						SHAPE_CHECK(near(length(v3[0],v3[1]),length(v3[1],v3[2])) && near(length(v3[0],v3[2]),length(v3[1],v3[2])));
						SHAPE_CHECK(equilateral->construction->tool.creationMode == CreationMode::CenterRadius);
						for (uint32_t id = 1000; id <= 1002; ++id)
						{
							const auto handle = getHandle(BuildEditHandles(*equilateral),id); SHAPE_CHECK(handle);
							if (handle)
							{
								std::string reason; const auto edited = EditShape(*equilateral,id,handle->position,{handle->position.x + 2,handle->position.y + 1},6,{},&reason);
								if (!edited) std::cerr << "equilateral edit " << id << ": " << reason << '\n';
								SHAPE_CHECK(edited && Validate(*edited));
							}
						}
						std::string reason; const auto restored = SetPlanarConstraint(*equilateral,PlanarConstraint::Free,7,&reason);
						if (!restored) std::cerr << "free restore: " << reason << '\n';
						SHAPE_CHECK(restored && restored->geometry == equilateral->geometry && restored->construction->tool.creationMode == CreationMode::Drag);
					}
				}
			}
		}
		triangleTool.creationMode = CreationMode::CenterRadius; triangleTool.constraint = PlanarConstraint::Equilateral;
		const auto centeredTriangle = MakePresetShape(triangleTool,{40,50},{140,50},{},Id(41));
		SHAPE_CHECK(centeredTriangle && centeredTriangle->geometry.vertices.size() == 3);
		if (centeredTriangle)
		{
			const auto& v = centeredTriangle->geometry.vertices;
			SHAPE_CHECK(near(length(v[0],v[1]),length(v[1],v[2])) && near(length(v[0],v[2]),length(v[1],v[2])));
			SHAPE_CHECK(pointNear({(v[0].x + v[1].x + v[2].x) / 3,(v[0].y + v[1].y + v[2].y) / 3},{40,50}));
		}
		const auto trapezoid = fixture(PresetKind::Trapezoid);
		SHAPE_CHECK(trapezoid && trapezoid->construction->tool.constraint == PlanarConstraint::Free);
		if (trapezoid)
		{
			const auto& initial = trapezoid->geometry.vertices;
			SHAPE_CHECK(near(length(initial[0],initial[3]),length(initial[1],initial[2])));
			const auto moved = EditShape(*trapezoid,1000,initial[0],{65,45},2);
			SHAPE_CHECK(moved && moved->construction->tool.constraint == PlanarConstraint::Free);
			if (moved)
			{
				const auto iso = SetPlanarConstraint(*moved,PlanarConstraint::Isosceles,3);
				SHAPE_CHECK(iso && Validate(*iso)); if (iso)
				{
					const auto& old = moved->geometry.vertices; const auto& v = iso->geometry.vertices;
					SHAPE_CHECK(v[2] == old[2] && v[3] == old[3] && near(length(v[0],v[1]),length(old[0],old[1])));
					SHAPE_CHECK(near((v[0].x + v[1].x) * .5f,(v[2].x + v[3].x) * .5f));
					SHAPE_CHECK(near(length(v[0],v[3]),length(v[1],v[2])));
					for (uint32_t id = 1000; id <= 1003; ++id)
					{
						const auto handle = getHandle(BuildEditHandles(*iso),id); SHAPE_CHECK(handle);
						if (handle)
						{
							const auto edited = EditShape(*iso,id,handle->position,{handle->position.x + 2,handle->position.y + 1},4);
							SHAPE_CHECK(edited && Validate(*edited));
							if (edited)
							{
								const auto free = SetPlanarConstraint(*edited,PlanarConstraint::Free,5);
								SHAPE_CHECK(free && Validate(*free));
								if (free) for (size_t i=0;i<v.size();++i)
									SHAPE_CHECK(pointNear(ApplyTransform(free->transform,free->geometry.vertices[i]),
										ApplyTransform(edited->transform,edited->geometry.vertices[i])));
							}
						}
					}
					const auto restored = SetPlanarConstraint(*iso,PlanarConstraint::Free,5);
					SHAPE_CHECK(restored && restored->geometry == iso->geometry);
				}
			}
		}
		for (PresetKind preset : {PresetKind::Line,PresetKind::Rectangle,PresetKind::Ellipse,PresetKind::Triangle})
			for (CreationMode mode : {CreationMode::CenterRadius,CreationMode::Parameters})
				SHAPE_CHECK(!MakePresetShape({preset,mode},{0,0},{100,100},{},Id(44)));
		ConstructionSession points;
		SHAPE_CHECK(points.Begin({PresetKind::Polyline,CreationMode::Points},{},Id(42)));
		SHAPE_CHECK(points.ConfirmDrag({0,0},{0,0}) && points.Candidate() && points.Candidate()->kind == Kind::Line);
		if (points.Candidate())
		{
			RenderData pointRender; SHAPE_CHECK(BuildRenderData(*points.Candidate(),pointRender) && !pointRender.batches.empty());
		}
		SHAPE_CHECK(points.Back() && !points.Candidate() && points.Forward() && points.Candidate());
		SHAPE_CHECK(points.Begin({PresetKind::Polyline,CreationMode::Points},{},Id(42)));
		SHAPE_CHECK(points.ConfirmDrag({0,0},{100,0}) && points.Status().confirmedSteps == 2 && points.CanComplete());
		SHAPE_CHECK(points.Candidate() && points.Candidate()->kind == Kind::Polyline && points.Candidate()->geometry.vertices.size() == 2);
		SHAPE_CHECK(points.Back() && !points.Candidate() && points.Forward() && points.Status().confirmedSteps == 2 && points.CanComplete());
		SHAPE_CHECK(points.Begin({PresetKind::Polygon,CreationMode::Points},{},Id(42)));
		for (Vec2 point : {Vec2{0,0},Vec2{100,0},Vec2{100,100}}) SHAPE_CHECK(points.ConfirmDrag(point,point));
		SHAPE_CHECK(points.Candidate() && points.Candidate()->kind == Kind::Polyline && points.CanComplete());
		if (points.Candidate())
		{
			GeometryResult openGeometry; SHAPE_CHECK(BuildGeometry(*points.Candidate(),openGeometry) && !openGeometry.parts.front().paths.front().closed);
		}
		const auto closed = points.Complete();
		SHAPE_CHECK(closed && closed->kind == Kind::Polygon);
		if (closed)
		{
			GeometryResult closedGeometry; SHAPE_CHECK(BuildGeometry(*closed,closedGeometry) && closedGeometry.parts.front().paths.front().closed);
		}
		const auto rectangle = fixture(PresetKind::Rectangle);
		if (rectangle)
		{
			const auto resized = EditShape(*rectangle,1002,{140,110},{260,270},2);
			SHAPE_CHECK(resized && resized->geometry.radius == rectangle->geometry.radius && resized->construction->tool.rectangleCornerRadius == rectangle->construction->tool.rectangleCornerRadius);
		}
		const auto cuboid = fixture(PresetKind::Cuboid);
		if (cuboid)
		{
			auto transformed = *cuboid; transformed.transform = {2,0,0,2,30,40}; Bounds frame;
			const auto handles = BuildEditHandles(transformed,{2,.25f},&frame);
			const auto corner = getHandle(handles,10); SHAPE_CHECK(corner && pointNear(corner->position,ApplyTransform(transformed.transform,{frame.left,frame.top})));
			auto local = transformed; local.transform = {}; GeometryResult geometry; SHAPE_CHECK(BuildGeometry(local,geometry,{2,.25f}));
			SHAPE_CHECK(near(frame.left,geometry.bounds.left - 8) && near(frame.top,geometry.bounds.top - 8));
		}

	}

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
		PresetKind::Triangle,PresetKind::Rhombus,PresetKind::Trapezoid })
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
	for (uint32_t sides = 3; sides <= 14; ++sides)
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
	SHAPE_CHECK(!MakePresetShape({ PresetKind::RegularPolygon,CreationMode::CenterRadius,15 }, { 0,0 }, { 20,30 }, {}, Id(9)));
	SHAPE_CHECK(!MakePresetShape({ PresetKind::Circle,CreationMode::Points,3 }, { 0,0 }, { 20,30 }, {}, Id(9)));
	SHAPE_CHECK(!MakePresetShape({}, { std::numeric_limits<float>::quiet_NaN(),0 }, { 20,30 }, {}, Id(9)));
	// P3/P4：所有高级参数走同一工厂、编辑、渲染与快照，不另建运行期历史。
	for (uint32_t value = uint32_t(PresetKind::Polyline); value <= uint32_t(PresetKind::Pyramid); ++value)
	{
		const auto preset = static_cast<PresetKind>(value); const auto capability = GetToolCapabilities(preset);
		SHAPE_CHECK(capability.enabled && capability.multiStage);
		for (float sign : { -1.0f,1.0f }) for (uint32_t pattern = 0; pattern < 4; ++pattern)
		{
			Construction c; c.tool = { preset,DefaultCreationMode(preset),5 }; c.start = { 30,70 }; c.end = { 130,110 };
			if (capability.points) c.parameters = VertexConstruction{ { { 10,10 },{ 100,10 },{ 50,40 },{ 100,100 },{ 10,100 } } };
			else if (preset == PresetKind::Arc || preset == PresetKind::Sector) c.parameters = ArcConstruction{ sign * 4.4f };
			else c=MakeSolidConstruction(c.tool,c.start,c.end,sign*60);
			ShapeStyle style; style.width = 4; style.strokeOpacity = .5f; style.linePattern = static_cast<LinePattern>(pattern);
			auto advanced = MakeConstructedShape(c, style, Id(11), 10, &error);
			SHAPE_CHECK(advanced && Validate(*advanced)); if (!advanced) continue;
			SHAPE_CHECK(BuildGeometry(*advanced, geometry) && geometry.bounds.valid);
			SHAPE_CHECK(BuildRenderData(*advanced, render) && !render.batches.empty());
			if (preset >= PresetKind::Cuboid)
			{
				// 向下圆锥的整个底环朝向观察者，可由两条轮廓线和一个实环组成。
				SHAPE_CHECK(advanced->kind == Kind::Compound && advanced->parts.size() >= (preset==PresetKind::Cone ? 3u : 4u));
				if(preset!=PresetKind::Cone || sign<0)SHAPE_CHECK(std::any_of(advanced->parts.begin(), advanced->parts.end(), [](const ShapePart& part)
					{ return part.style && part.style->linePattern == LinePattern::Dashed; }));
			}
			const auto handles = BuildEditHandles(*advanced);
			SHAPE_CHECK(!handles.empty());
			for (const auto& handle : handles)
			{
				// 小幅有效移动，非法的几何约束边界可拒绝，但不允许改写原始快照。
				const auto before = *advanced; const auto edited = EditShape(before, handle.id, handle.position,
					{ handle.position.x + 2,handle.position.y - 3 }, 11);
				if (edited) SHAPE_CHECK(Validate(*edited) && edited->id == before.id && edited->revision == 11 && edited->style.width == before.style.width);
				SHAPE_CHECK(before == *advanced);
			}
			auto moved = EditShape(*advanced, 0, { 0,0 }, { -11,13 }, 11);
			SHAPE_CHECK(moved && moved->transform.dx == -11 && moved->transform.dy == 13 && moved->construction == advanced->construction);
			style.width = 7; auto restyled = RestyleShape(*advanced, style, 12);
			SHAPE_CHECK(restyled && restyled->style.width == 7 && restyled->construction == advanced->construction);
			auto malformed = c; malformed.parameters = std::monostate{};
			SHAPE_CHECK(!MakeConstructedShape(malformed, style, Id(11)));
		}
	}
	for (uint32_t value = 0; value <= uint32_t(PresetKind::RegularPolygon); ++value)
	{
		const auto preset = static_cast<PresetKind>(value); const auto mode = DefaultCreationMode(preset);
		const auto object = MakePresetShape({ preset,mode,5 }, { 20,30 }, { 120,90 }, {}, Id(12), 7);
		SHAPE_CHECK(object); if (!object) continue;
		const auto handles = BuildEditHandles(*object);
		if (preset == PresetKind::Circle) SHAPE_CHECK(handles.size() == 3 && handles[0].kind == EditHandleKind::Center);
		if (preset == PresetKind::Line || preset == PresetKind::Arrow) SHAPE_CHECK(handles.size() == 2);
		if (preset == PresetKind::RegularPolygon) SHAPE_CHECK(handles.size() == 3);
		if (preset == PresetKind::Ellipse) SHAPE_CHECK(handles.size() == 6);
		for (const auto& handle : handles)
		{
			const auto edited = EditShape(*object, handle.id, handle.position, { handle.position.x + 4,handle.position.y + 3 }, 8);
			SHAPE_CHECK(edited && edited->style.width == object->style.width && Validate(*edited));
		}
		SHAPE_CHECK(!EditShape(*object, 999, { 0,0 }, { 10,10 }, 8));
		SHAPE_CHECK(!EditShape(*object, 0, { 0,0 }, { 10,10 }, 7));
	}
	const auto shortArrow = MakePresetShape({ PresetKind::Arrow,CreationMode::Drag }, { 0,0 }, { 2,0 }, {}, Id(12));
	SHAPE_CHECK(shortArrow && shortArrow->parts[1].geometry.vertices[0] == (Vec2{ -4,3 }));
	if (shortArrow)
	{
		const auto longer = EditShape(*shortArrow, 1001, { 2,0 }, { 100,0 }, 2);
		SHAPE_CHECK(longer && longer->parts[1].geometry.vertices[0] == (Vec2{ 94,3 }));
		ShapeStyle wide = shortArrow->style; wide.width = 5; auto wider = RestyleShape(*shortArrow, wide, 2);
		SHAPE_CHECK(wider && wider->parts[1].geometry.vertices[0] == (Vec2{ -13,7.5f }));
	}
	ConstructionSession session;
	SHAPE_CHECK(session.Begin({ PresetKind::Polygon,CreationMode::Points }, {}, Id(13)));
	SHAPE_CHECK(session.ConfirmDrag({ 0,0 }, { 0,0 }) && !session.CanComplete());
	SHAPE_CHECK(session.PreviewDrag({ 100,0 }, { 120,0 }) && !session.CanComplete()); // 预览点不进入真实顶点。
	SHAPE_CHECK(session.ConfirmDrag({ 100,0 }, { 100,0 }) && !session.CanComplete());
	SHAPE_CHECK(session.Status().totalSteps == 0 && !session.Status().canForward);
	SHAPE_CHECK(session.Back() && session.Status().canForward && session.Status().confirmedSteps == 1);
	SHAPE_CHECK(session.Forward() && !session.Status().canForward && session.Status().confirmedSteps == 2);
	SHAPE_CHECK(session.ConfirmDrag({ 100,100 }, { 100,100 }) && session.CanComplete());
	const auto confirmedPolygon = session.Candidate();
	SHAPE_CHECK(session.Back() && !session.CanComplete() && session.Status().canForward);
	SHAPE_CHECK(session.Forward() && session.Candidate() == confirmedPolygon && session.CanComplete());
	SHAPE_CHECK(session.Back() && session.ConfirmDrag({ 80,100 }, { 80,100 }) && !session.Status().canForward && !session.Forward());
	SHAPE_CHECK(session.Back() && session.ConfirmDrag({ 100,100 }, { 100,100 }));
	SHAPE_CHECK(session.PreviewDrag({ 0,100 }, { -100,100 }));
	auto completed = session.Complete();
	SHAPE_CHECK(completed && completed->geometry.vertices.size() == 3 && completed->geometry.vertices.back() == (Vec2{ 100,100 }));
	SHAPE_CHECK(!session.Status().active);
	SHAPE_CHECK(completed && BuildGeometry(*completed, geometry) && geometry.parts[0].paths[0].closed &&
		geometry.parts[0].paths[0].points.front() == geometry.parts[0].paths[0].points.back());
	SHAPE_CHECK(session.Begin({ PresetKind::Polyline,CreationMode::Points }, {}, Id(13)));
	SHAPE_CHECK(session.ConfirmDrag({ 0,0 }, { 0,0 }) && !session.CanComplete());
	SHAPE_CHECK(session.ConfirmDrag({ 100,0 }, { 100,0 }) && session.CanComplete());
	completed = session.Complete();
	SHAPE_CHECK(completed && BuildGeometry(*completed, geometry) && !geometry.parts[0].paths[0].closed);
	SHAPE_CHECK(session.Begin({ PresetKind::Polygon,CreationMode::Points }, {}, Id(13)));
	for (Vec2 v : { Vec2{ 0,0 },Vec2{ 100,100 },Vec2{ 0,100 },Vec2{ 100,0 } }) SHAPE_CHECK(session.ConfirmDrag(v, v));
	SHAPE_CHECK(!session.CanComplete() && !session.Complete());
	SHAPE_CHECK(session.Back() && session.CanComplete()); session.Cancel();
	SHAPE_CHECK(!session.Status().active && !session.Candidate());
	for (PresetKind preset : { PresetKind::Arc,PresetKind::Sector }) for (float sign : { -1.0f,1.0f })
	{
		SHAPE_CHECK(session.Begin({ preset,CreationMode::MultiStage }, {}, Id(14)));
		SHAPE_CHECK(session.Status().totalSteps == 2 && session.Status().step == 1);
		SHAPE_CHECK(session.ConfirmDrag({ 0,0 }, { 100,0 }) && !session.CanComplete());
		const auto radiusStep = session.Candidate();
		SHAPE_CHECK(radiusStep && radiusStep->style.linePattern == LinePattern::Dashed && radiusStep->style.stroke);
		// 未确认第二步转过整圈后回退，Forward不能恢复这段已丢弃的轨迹角。
		for (unsigned sample = 1; sample <= 8; ++sample)
		{ const float angle = sample * static_cast<float>(3.141592653589793 / 4); session.ObserveRaw({100 * std::cos(angle),100 * std::sin(angle)}); }
		SHAPE_CHECK(session.Back() && session.Status().step == 1 && session.Status().canForward);
		SHAPE_CHECK(session.Forward() && session.Status().step == 2 && session.Candidate() == radiusStep);
		for (uint32_t i = 1; i <= 24; ++i)
		{
			const float angle = sign * 4.4f * i / 24;
			const Vec2 raw{ 100 * std::cos(angle),100 * std::sin(angle) };
			session.ObserveRaw(raw);
			const Vec2 predicted{ 100 * std::cos(angle + sign * .08f),100 * std::sin(angle + sign * .08f) };
			SHAPE_CHECK(session.PreviewDrag({ 100,0 }, predicted));
		}
		const Vec2 final{ 100 * std::cos(sign * 4.4f),100 * std::sin(sign * 4.4f) };
		SHAPE_CHECK(session.ConfirmDrag({ 100,0 }, final) && session.Status().finished);
		const auto sweptStep = session.Candidate();
		SHAPE_CHECK(session.Back() && session.Forward() && session.Candidate() == sweptStep);
		completed = session.Complete();
		SHAPE_CHECK(completed && std::abs(std::get<ArcConstruction>(completed->construction->parameters).sweepRadians - sign * 4.4f) < .0001f);
	}
	for (uint32_t value = uint32_t(PresetKind::Cuboid); value <= uint32_t(PresetKind::Pyramid); ++value)
	{
		const auto preset = static_cast<PresetKind>(value);
		SHAPE_CHECK(session.Begin({ preset,CreationMode::MultiStage,5 }, {}, Id(14)));
		SHAPE_CHECK(session.PreviewDrag({ 20,70 }, { 100,100 }));
		SHAPE_CHECK(session.ConfirmDrag({ 20,70 }, { 100,100 }) && session.Status().confirmedSteps == 1);
		SHAPE_CHECK(session.PreviewDrag({ 50,80 }, { 90,10 }));
		SHAPE_CHECK(session.ConfirmDrag({ 50,80 }, { 80,20 }) && session.CanComplete());
		SHAPE_CHECK(session.Back() && session.Status().confirmedSteps == 1 && !session.CanComplete());
		SHAPE_CHECK(session.ConfirmDrag({ 50,80 }, { 80,20 })); completed = session.Complete();
		SHAPE_CHECK(completed && completed->construction && completed->kind == Kind::Compound);
		if (completed)
		{
			const auto& solid = std::get<SolidConstruction>(completed->construction->parameters);
			SHAPE_CHECK((value == uint32_t(PresetKind::Cube) ? std::abs(solid.height) == solid.baseSize.x : solid.height == -60));
		}
	}
	const auto legacyEdit = MakeLegacyShape(LegacyKind::OutlineRectangle, { 0,0 }, { 100,100 }, 4, 0, 1, Id(15));
	if (legacyEdit)
	{
		auto upgraded = EditShape(*legacyEdit, 0, { 0,0 }, { 5,7 }, 2, { 2,.25f });
		SHAPE_CHECK(upgraded && upgraded->geometry.radius == 8 && upgraded->legacyKind == LegacyKind::None && upgraded->id == legacyEdit->id);
	}
	RevisionAllocator revisions;
	SHAPE_CHECK(revisions.Next(Id(16), 20) == 21 && revisions.Next(Id(16), 4) == 22 && revisions.Next(Id(17), 1) == 2);
	SHAPE_CHECK(revisions.Next(Id(16), std::numeric_limits<uint64_t>::max()) == 0 && revisions.Next({}) == 0);
	for (float x : {-1.0f,1.0f}) for (float y : {-1.0f,1.0f})
	{
		const Vec2 down{20,30}, end{20+x*100,30+y*35};
		auto c=MakeSolidConstruction({PresetKind::Cuboid,CreationMode::MultiStage},down,end,-80);
		const auto& solid=std::get<SolidConstruction>(c.parameters);
		SHAPE_CHECK(solid.mirrored==(x*y<0));
		const auto preview=SolidBasePreview(c.tool,down,end,{},Id(18),1); SHAPE_CHECK(preview);
		if(preview)for(auto target:{down,end})SHAPE_CHECK(std::any_of(preview->geometry.vertices.begin(),preview->geometry.vertices.end(),[&](Vec2 v){return std::hypot(v.x-target.x,v.y-target.y)<.001f;}));
	}

	// 固定教材投影和合并柄：尺寸与Z转向同一逆投影手势，侧向偏移跟随旋转。
	for(auto preset:{PresetKind::Cuboid,PresetKind::Cube,PresetKind::Cylinder,PresetKind::Cone,PresetKind::Prism,PresetKind::Pyramid})
	{
		ToolSpec tool{preset,CreationMode::MultiStage};tool.sides=6;
		auto c=MakeSolidConstruction(tool,{100,100},{180,140},-120);
		if(preset==PresetKind::Prism || preset==PresetKind::Pyramid)std::get<SolidConstruction>(c.parameters).offset={25,15};
		auto object=MakeConstructedShape(c,{},Id(19));SHAPE_CHECK(object);if(!object)continue;
		const auto& solid=std::get<SolidConstruction>(c.parameters);
		const auto handles=BuildEditHandles(*object);
		SHAPE_CHECK(handles.size()==7 && std::none_of(handles.begin(),handles.end(),[](auto h){return h.id==1 || h.id==102 || h.id==103 || h.id==106;}));
		for(const auto& h:handles)
		{
			const auto changed=EditShape(*object,h.id,h.position,{h.position.x+5,h.position.y-3},2);SHAPE_CHECK(changed);if(!changed)continue;
			const auto& next=std::get<SolidConstruction>(changed->construction->parameters);
			if(preset==PresetKind::Cube)SHAPE_CHECK(next.baseSize.x==next.baseSize.y && std::abs(next.height)==next.baseSize.x);
			if(h.id==1001)
			{
				SHAPE_CHECK(std::abs(next.baseSize.x/next.baseSize.y-solid.baseSize.x/solid.baseSize.y)<.001f);
				if(preset==PresetKind::Cylinder || preset==PresetKind::Cone)SHAPE_CHECK(next.baseRotation==0);
				else
				{
					SHAPE_CHECK(next.baseRotation!=0 && next.offset==solid.offset);
					for(auto actual:BuildEditHandles(*changed))if(actual.id==1001)SHAPE_CHECK(std::hypot(actual.position.x-h.position.x-5,actual.position.y-h.position.y+3)<.001f);
				}
			}
		}
		if(preset==PresetKind::Prism || preset==PresetKind::Pyramid)
		{
			const auto right=SetRightSolid(*object,true,2);SHAPE_CHECK(right);
			if(right){SHAPE_CHECK(std::get<SolidConstruction>(right->construction->parameters).offset==Vec2{});const auto free=SetRightSolid(*right,false,3);SHAPE_CHECK(free && free->parts==right->parts);}
		}
		if(preset==PresetKind::Cube)for(auto h:handles)if(h.id==105){const auto flipped=EditShape(*object,105,h.position,{h.position.x,c.start.y+10},2);SHAPE_CHECK(flipped && std::get<SolidConstruction>(flipped->construction->parameters).height==solid.baseSize.x);}
		if(preset==PresetKind::Cylinder || preset==PresetKind::Cone)
			SHAPE_CHECK(std::any_of(object->parts.begin(),object->parts.end(),[](auto part){return part.kind==Kind::Polyline && part.style && part.style->linePattern==LinePattern::Dashed && part.geometry.vertices.size()>3;}));
		auto invalid=*object;invalid.transform={0,1,-1,0,0,0};SHAPE_CHECK(!Validate(invalid));
	}
	// 平面翻向先经过退化点，再进入另一侧仍可继续提交，梯形不允许同底顶点交叉。
	for(auto preset:{PresetKind::Rectangle,PresetKind::Parallelogram,PresetKind::Rhombus,PresetKind::Trapezoid})
	{
		auto object=MakePresetShape({preset,CreationMode::Drag},{0,0},{100,80},{},Id(22));SHAPE_CHECK(object);if(!object)continue;
		const auto handles=BuildEditHandles(*object);auto h=*std::find_if(handles.begin(),handles.end(),[](auto v){return v.id==1000;});
		Vec2 target,zero;
		if(preset==PresetKind::Rectangle){zero={100,80};target={130,120};}
		else if(preset==PresetKind::Parallelogram){const auto q=object->geometry.vertices[2];zero=q;target={q.x*2-h.position.x,q.y*2-h.position.y};}
		else if(preset==PresetKind::Rhombus){zero={50,40};target={50,110};}
		else{zero={25,80};target={25,110};}
		EditSession edit;SHAPE_CHECK(edit.Begin(*object,h.id,h.position));SHAPE_CHECK(edit.Preview(zero,2) && edit.Candidate());SHAPE_CHECK(edit.Preview(target,3));SHAPE_CHECK(edit.Commit());
		if(preset==PresetKind::Trapezoid)SHAPE_CHECK(!EditShape(*object,1000,h.position,{90,10},4));
	}
	for(auto target:{Vec2{100,80},Vec2{-100,80},Vec2{-100,-80},Vec2{30,-90}})
	{
		const auto object=MakePresetShape({PresetKind::Parallelogram,CreationMode::Drag},{0,0},target,{},Id(23));SHAPE_CHECK(object);
		if(object){const auto& v=object->geometry.vertices;SHAPE_CHECK(v.front()==Vec2{} && v[2]==target);SHAPE_CHECK(v[0].y==v[1].y && v[2].y==v[3].y);}
	}

	{
		ToolSpec tool{PresetKind::Triangle,CreationMode::Drag};
		auto triangle=MakePresetShape(tool,{0,0},{100,80},{},Id(20));
		if(triangle)
		{
			auto iso=SetPlanarConstraint(*triangle,PlanarConstraint::Isosceles,2);SHAPE_CHECK(iso);
			for(uint32_t step=1;iso && step<=3;++step)
			{
				const auto old=iso->construction->tool.constraintVertex;
				iso=SetPlanarConstraint(*iso,PlanarConstraint::Isosceles,2+step);
				SHAPE_CHECK(iso && iso->construction->tool.constraintVertex==(old+1)%3);
			}
		}
	}


	// 同一X在不同高度位于倾斜对边两侧，旋转后也按手势原始基底连续翻向。
	for(float angle:{0.f,.7f}) {
		auto object=MakePresetShape({PresetKind::Parallelogram,CreationMode::Drag},{0,0},{100,80},{},Id(24));SHAPE_CHECK(object);if(!object)continue;
		object->transform={std::cos(angle),std::sin(angle),-std::sin(angle),std::cos(angle),30,20};
		const auto down=ApplyTransform(object->transform,{0,0}); EditSession edit;SHAPE_CHECK(edit.Begin(*object,1000,down));
		for(Vec2 target:{Vec2{80,0},Vec2{80,16},Vec2{80,40},Vec2{-10,-20}}) {
			SHAPE_CHECK(edit.Preview(ApplyTransform(object->transform,target),2));
			const auto& result=edit.Candidate();SHAPE_CHECK(result);if(!result || target.y==16)continue;
			const auto& v=result->geometry.vertices;
			SHAPE_CHECK(std::abs(v[0].x-target.x)<.001f && std::abs(v[0].y-target.y)<.001f);
			SHAPE_CHECK(v[2]==object->geometry.vertices[2]);
			SHAPE_CHECK(std::abs(v[0].y-v[1].y)<.001f && std::abs(v[2].y-v[3].y)<.001f);
			if(target.x==80) SHAPE_CHECK(target.y==0 ? v[1].x<v[0].x : v[1].x>v[0].x);
		}
		SHAPE_CHECK(edit.Commit());
	}
	for(auto preset:{PresetKind::Cuboid,PresetKind::Cube}) for(float x:{-1.f,1.f}) for(float y:{-1.f,1.f}) for(Vec2 size:{Vec2{120,35},Vec2{20,100}}) {
		const Vec2 down{50,60},end{down.x+x*size.x,down.y+y*size.y};const ToolSpec tool{preset,CreationMode::MultiStage};
		const auto c=MakeSolidConstruction(tool,down,end,-50);const auto& solid=std::get<SolidConstruction>(c.parameters);
		const float depth=size.y/.3535533906f;
		SHAPE_CHECK((c.start==Vec2{(down.x+end.x)*.5f,(down.y+end.y)*.5f}));
		SHAPE_CHECK(solid.mirrored==(x*y<0));
		if(preset==PresetKind::Cube)SHAPE_CHECK(solid.baseSize.x==std::max(size.x,depth) && solid.baseSize.x==solid.baseSize.y && std::abs(solid.height)==solid.baseSize.x);
		else SHAPE_CHECK(std::abs(solid.baseSize.x-size.x-size.y)<.001f && std::abs(solid.baseSize.y-depth)<.001f);
	}

	// 模型底面仍是圆，显示轮廓的水平/竖直直径固定3:1，镜像不改变圆底外观。
	for(auto preset:{PresetKind::Cylinder,PresetKind::Cone}) for(bool mirrored:{false,true}) for(float rotation:{0.f,.3f}) {
		auto c=MakeSolidConstruction({preset,CreationMode::MultiStage},{0,0},{100,0},-300);
		std::get<SolidConstruction>(c.parameters).mirrored=mirrored;
		std::get<SolidConstruction>(c.parameters).baseRotation=rotation;
		const auto object=MakeConstructedShape(c,{},Id(26));SHAPE_CHECK(object);if(!object)continue;
		for(Vec2 target:{Vec2{100,0},Vec2{-100,0},Vec2{0,-100.f/3},Vec2{0,100.f/3}}) {
			double distance=1e9;
			for(const auto& part:object->parts)if(part.kind==Kind::Polyline)for(size_t i=1;i<part.geometry.vertices.size();++i){
				const auto a=part.geometry.vertices[i-1],b=part.geometry.vertices[i];const double dx=b.x-a.x,dy=b.y-a.y;
				const auto t=std::clamp(((target.x-a.x)*dx+(target.y-a.y)*dy)/(dx*dx+dy*dy),0.,1.);
				distance=std::min(distance,std::hypot(a.x+t*dx-target.x,a.y+t*dy-target.y));
			}
			SHAPE_CHECK(distance<=.251);
		}
	}

	// 水平圆底投影下，真实圆弧中点到细分弦中点的偏差仍不超过 .25px。
	{
		auto c=MakeSolidConstruction({PresetKind::Cylinder,CreationMode::MultiStage},{0,0},{1000,0},-1500);
		auto& solid=std::get<SolidConstruction>(c.parameters);solid.baseRotation=.7f;
		const auto object=MakeConstructedShape(c,{},Id(21));SHAPE_CHECK(object);
		if(object)
		{
			const double kx=0,ky=-1./3;
			for(const auto& part:object->parts)if(part.kind==Kind::Polyline)
			{
				const auto& v=part.geometry.vertices;
				// 圆柱上下环共享轮廓，只需从首点反投影的长度选择高度。
				auto unproject=[&](Vec2 p,double h){const double y=(p.y-h)/ky;return Vec2{static_cast<float>(p.x-kx*y),static_cast<float>(y)};};
				const auto lower=unproject(v.front(),0),upper=unproject(v.front(),-1500);
				const double h=std::abs(std::hypot(lower.x,lower.y)-1000)<std::abs(std::hypot(upper.x,upper.y)-1000)?0:-1500;
				for(size_t i=1;i<v.size();++i)
				{
					const auto a=unproject(v[i-1],h),b=unproject(v[i],h);
					const double x=(a.x+b.x)*.5,y=(a.y+b.y)*.5,scale=1000/std::hypot(x,y);
					const double actualX=(v[i-1].x+v[i].x)*.5,actualY=(v[i-1].y+v[i].y)*.5;
					SHAPE_CHECK(std::hypot((x+kx*y)*scale-actualX,ky*y*scale+h-actualY)<=.251);
				}
			}
		}
	}
	// 圆底的预览、半径编辑及正负高度共享同一投影；母线与真实椭圆相切。
	for(auto preset:{PresetKind::Cylinder,PresetKind::Cone})for(float height:{-300.f,300.f}) {
		const ToolSpec tool{preset,CreationMode::MultiStage};
		auto c=MakeSolidConstruction(tool,{0,0},{100,0},height);
		auto shape=MakeConstructedShape(c,{},Id(27));SHAPE_CHECK(shape);if(!shape)continue;
		const auto preview=SolidBasePreview(tool,{0,0},{100,0},{},Id(28),1);SHAPE_CHECK(preview);
		if(preview)for(auto p:preview->geometry.vertices)SHAPE_CHECK(std::abs(p.x*p.x+9*p.y*p.y-10000)<.02);
		unsigned sides=0;
		for(const auto& part:shape->parts)if(part.kind==Kind::Line){
			++sides;const auto p=part.geometry.start,q=part.geometry.end;
			if(preset==PresetKind::Cylinder)SHAPE_CHECK(std::abs(std::abs(p.x)-100)<.001 && std::abs(p.y)<.001 && p.x==q.x);
			else SHAPE_CHECK(std::abs(p.y-10000/(9*height))<.001 && std::abs(p.x*p.x+9*p.y*p.y-10000)<.02);
		}
		SHAPE_CHECK(sides==2);
		const auto handles=BuildEditHandles(*shape);const auto h=*std::find_if(handles.begin(),handles.end(),[](auto h){return h.id==1001;});
		const auto edited=EditShape(*shape,h.id,h.position,{200,0},2);SHAPE_CHECK(edited);
		if(edited){const auto& model=std::get<SolidConstruction>(edited->construction->parameters);SHAPE_CHECK(model.baseSize.x==model.baseSize.y && std::abs(model.baseSize.x-200)<.001);}
		std::get<SolidConstruction>(c.parameters).baseSize.y=101;SHAPE_CHECK(!MakeConstructedShape(c,{},Id(29)));
	}
	RunAuxiliaryTests(failures);
	RunEditInteractionTests(failures);
	std::cout << "Shape core tests: " << failures << " failure(s)\n";
	return failures;
}

#ifdef SHAPE_CORE_TEST_MAIN
int main() { return RunShapeCoreTests(); }
#endif
