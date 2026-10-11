#pragma once

#include "imgui/imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <vector>

namespace Inkeys::UI::Setting::Design::HomeBrand
{
	// 源于网站 NewHome3/glyphs.ts、softPen.ts、styles.ts 的 hard/rainbow/medium。
	// 保留六组原始曲线、右倾、速度压感和全局弧长配色；仅首次调用建立几何。
	struct Curve { float bx, by, cx, cy, dx, dy; };
	struct Gesture { float origin; ImVec2 start; std::vector<Curve> curves; };
	struct Point { ImVec2 pos; float time = 0, width = 15.5F, distance = 0; };
	struct Vertex { ImVec2 pos; ImU32 light, dark; ImVec2 normalized; };
	struct Mesh
	{
		struct Block { unsigned first, count; float progress; };
		struct Section { ImVec2 center, normal; float radius, progress; std::array<unsigned,4> vertices; };
		struct Stroke { std::vector<Block> blocks; std::vector<Section> sections; bool closed = false; };
		std::vector<Vertex> vertices;
		std::vector<unsigned> indices;
		std::array<Stroke,6> strokes;
		ImVec2 size, kEnd, kInward;
		float kRadius = 0;
	};
	inline float Length(ImVec2 a, ImVec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }
	inline float Smooth(float value)
	{
		const float t = std::clamp(value, 0.0F, 1.0F);
		return t * t * (3.0F - 2.0F * t);
	}
	inline ImU32 InkColor(float fraction, bool dark, unsigned alpha = 255)
	{
		static constexpr float stops[]{ 0, .17F, .29F, .40F, .53F, .64F, .76F, .88F, 1 };
		static constexpr unsigned char colors[2][9][3]{
			{ {0,134,151}, {71,184,163}, {191,206,35}, {255,193,0}, {255,120,49},
				{245,83,110}, {182,113,189}, {102,91,191}, {32,165,216} },
			{ {76,198,195}, {132,214,176}, {206,215,114}, {242,198,98}, {243,163,104},
				{239,133,157}, {192,149,216}, {154,153,227}, {111,182,226} }
		};
		int index = 1;
		while (index < 8 && stops[index] < fraction) ++index;
		const float t = (fraction - stops[index - 1]) / (stops[index] - stops[index - 1]);
		const auto& a = colors[dark][index - 1]; const auto& b = colors[dark][index];
		auto channel = [&](int c) { return static_cast<int>(std::round(a[c] + (b[c] - a[c]) * t)); };
		return IM_COL32(channel(0), channel(1), channel(2), alpha);
	}
	inline Mesh Build()
	{
		const std::array<Gesture, 6> gestures{
			Gesture{0, {6,-157}, {{20,-161,49,-160,61,-157}, {51,-154,41,-154,33,-154},
				{33,-105,28,-49,24,-4}, {16,-4,8,-3,4,0}, {22,2,45,2,64,0}}},
			Gesture{91, {12,-100}, {{10,-72,8,-29,8,0}, {13,-39,26,-98,57,-99},
				{88,-100,70,-40,74,-16}, {77,4,91,2,103,-12}}},
			Gesture{209, {8,0}, {{9,-46,15,-130,27,-151}, {42,-178,62,-141,39,-99}, {22,-65,11,-28,8,0}}},
			Gesture{209, {72,-95}, {{60,-82,40,-60,26,-49}, {43,-50,53,-7,78,-2}, {91,0,101,-15,112,-26},
				{134,-39,166,-54,166,-77}, {166,-109,127,-111,117,-78}, {106,-42,115,-9,140,-2}, {160,4,175,-9,183,-24}}},
			Gesture{414, {12,-100}, {{6,-67,-4,-18,15,-5}, {40,14,62,-61,65,-100},
				{63,-46,56,23,40,56}, {25,84,-1,67,8,45}, {18,22,45,13,72,-6}}},
			Gesture{516, {62,-88}, {{48,-110,12,-102,9,-78}, {6,-59,31,-52,46,-39},
				{76,-12,43,13,11,-4}, {4,-8,0,-13,-2,-18}}}
		};
		std::array<std::vector<Point>, 6> strokes;
		float totalWeight = 0, lifts = 0;
		for (size_t s = 0; s < gestures.size(); ++s)
		{
			const auto& gesture = gestures[s];
			std::vector<ImVec2> dense{gesture.start}; ImVec2 a = gesture.start;
			for (const auto& curve : gesture.curves)
			{
				const ImVec2 b(curve.bx, curve.by), c(curve.cx, curve.cy), d(curve.dx, curve.dy);
				const int steps = static_cast<int>(std::ceil((Length(a,b) + Length(b,c) + Length(c,d)) / .8F));
				for (int i = 1; i <= steps; ++i)
				{
					const float t = static_cast<float>(i) / steps, u = 1 - t;
					dense.push_back({u*u*u*a.x + 3*u*u*t*b.x + 3*u*t*t*c.x + t*t*t*d.x,
						u*u*u*a.y + 3*u*u*t*b.y + 3*u*t*t*c.y + t*t*t*d.y});
				}
				a = d;
			}
			std::vector<ImVec2> spaced{dense.front()}; float remaining = .8F;
			for (size_t i = 1; i < dense.size(); ++i)
			{
				ImVec2 from = dense[i-1]; const ImVec2 to = dense[i]; float distance = Length(from,to);
				while (distance >= remaining)
				{
					const float t = remaining / distance;
					from = {from.x + (to.x-from.x)*t, from.y + (to.y-from.y)*t};
					spaced.push_back(from); distance -= remaining; remaining = .8F;
				}
				remaining -= distance;
			}
			if (Length(spaced.back(),dense.back()) > .001F) spaced.push_back(dense.back());
			auto& points = strokes[s];
			for (auto p : spaced) points.push_back({{gesture.origin + p.x - p.y*.14F, p.y}});
			for (size_t i = 1; i < points.size(); ++i)
			{
				const auto before = points[i > 5 ? i-5 : 0].pos, here = points[i].pos;
				const auto after = points[(std::min)(i+5, points.size()-1)].pos;
				const float ax = here.x-before.x, ay = here.y-before.y, bx = after.x-here.x, by = after.y-here.y;
				const float angle = std::atan2(std::abs(ax*by-ay*bx), ax*bx+ay*by);
				const float curvature = angle / (std::max)(1.0F, (Length(before,here)+Length(here,after))*.5F);
				const float edge = static_cast<float>((std::min)(i,points.size()-1-i))*.8F;
				const float speed = (240 + 650/(1+curvature*75)) * (.58F + .42F*Smooth(edge/28));
				points[i].time = points[i-1].time + Length(points[i-1].pos,here)/speed;
			}
			totalWeight += points.back().time;
			if (s) lifts += std::clamp(.09F + Length(strokes[s-1].back().pos,points.front().pos)/1000, .1F, .24F);
		}
		float distance = 0;
		for (auto& points : strokes)
		{
			const float duration = (4.2F-lifts)*points.back().time/totalWeight;
			const float weight = points.back().time; float previousTime = 0, width = 15.5F;
			for (size_t i = 0; i < points.size(); ++i)
			{
				const float time = points[i].time/weight*duration;
				if (i)
				{
					const float step = Length(points[i-1].pos,points[i].pos), dt = time-previousTime;
					distance += step;
					const float target = 15.5F*(1.15F-.30F*Smooth(step/(std::max)(.000001F,dt)/1050));
					width += (target-width)*(1-std::exp(-dt/.055F));
				}
				points[i].width = width; points[i].distance = distance; previousTime = time;
			}
		}
		// k 的回折笔画首尾重合；在接合半径内保持同宽，再平滑过渡，避免近零长度末段突然扩宽。
		for (auto& points : strokes)
			if (Length(points.front().pos,points.back().pos)<.001F)
			{
				const float width=(std::max)(points.front().width,points.back().width);
				for (auto& p : points)
				{
					const float edge=(std::min)(p.distance-points.front().distance,points.back().distance-p.distance);
					p.width=width+(p.width-width)*Smooth((edge-width*.5F)/width);
				}
			}
		auto closedInward=[](const std::vector<Point>& points) {
			const auto front=points.front().pos, next=points[1].pos, end=points.back().pos, before=points[points.size()-2].pos;
			const float a=Length(front,next), b=Length(end,before);
			const ImVec2 sum((next.x-front.x)/a+(before.x-end.x)/b,(next.y-front.y)/a+(before.y-end.y)/b);
			const float length=std::hypot(sum.x,sum.y);
			return ImVec2(sum.x/length,sum.y/length);
		};
		Mesh mesh;
		auto vertex = [&](ImVec2 p, float s, unsigned alpha = 255) {
			mesh.vertices.push_back({p, InkColor(s/distance,false,alpha), InkColor(s/distance,true,alpha)});
			return static_cast<unsigned>(mesh.vertices.size()-1);
		};
		auto triangle = [&](unsigned a, unsigned b, unsigned c) { mesh.indices.insert(mesh.indices.end(), {a,b,c}); };
		// 连续变宽轮廓、透明外缘与圆接头缓存为网格，逐帧只缩放/平移已完成的几何。
		for (size_t strokeIndex=0;strokeIndex<strokes.size();++strokeIndex)
		{
			const auto& points=strokes[strokeIndex]; auto& stroke=mesh.strokes[strokeIndex];
			const bool closed=Length(points.front().pos,points.back().pos)<.001F;
			stroke.closed=closed;
			auto record=[&](unsigned first,float progress) {
				const unsigned count=static_cast<unsigned>(mesh.indices.size())-first;
				if (count) stroke.blocks.push_back({first,count,progress});
			};
			const ImVec2 inward=closed ? closedInward(points) : ImVec2();
			size_t first = 0;
			while (first < points.size()-1)
			{
				size_t last = points.size()-1;
				for (size_t i = first+1; i < points.size()-1; ++i)
				{
					const auto a=points[i-1].pos, b=points[i].pos, c=points[i+1].pos;
					if ((b.x-a.x)*(c.x-b.x)+(b.y-a.y)*(c.y-b.y) < .5F*Length(a,b)*Length(b,c)) { last=i; break; }
				}
				std::array<unsigned,4> previous{}, firstSection{}, lastSection{};
				ImVec2 firstNormal, lastNormal;
				for (size_t i = first; i <= last; ++i)
				{
					const auto& p=points[i]; const auto a=points[i>first ? i-1 : first].pos, b=points[i<last ? i+1 : last].pos;
					const float length=(std::max)(.000001F,Length(a,b));
					float nx=-(b.y-a.y)/length, ny=(b.x-a.x)/length;
					// 首尾共用同一截面方向；微小切线角差不能在外缘留下薄楔透明孔。
					if (closed)
					{
						const float fromStart=p.distance-points.front().distance, fromEnd=points.back().distance-p.distance;
						const float blend=1-Smooth((std::min)(fromStart,fromEnd)/points.front().width);
						const float sign=fromStart<fromEnd ? 1.0F : -1.0F;
						nx+=( -inward.y*sign-nx)*blend; ny+=(inward.x*sign-ny)*blend;
						const float normalLength=std::hypot(nx,ny); nx/=normalLength; ny/=normalLength;
					}
					const float radii[]{p.width*.5F+.8F, p.width*.5F, -p.width*.5F, -p.width*.5F-.8F};
					std::array<unsigned,4> current;
					for (int j=0;j<4;++j) current[j]=vertex({p.pos.x+nx*radii[j],p.pos.y+ny*radii[j]},p.distance,j==0||j==3 ? 0 : 255);
					const float progress=p.time/points.back().time;
					stroke.sections.push_back({p.pos,{nx,ny},p.width*.5F,progress,current});
					if (i==first) { firstSection=current; firstNormal={nx,ny}; }
					if (i==last) { lastSection=current; lastNormal={nx,ny}; }
					const auto indexStart=static_cast<unsigned>(mesh.indices.size());
					if (i>first) for (int j=0;j<3;++j) { triangle(previous[j],current[j],current[j+1]); triangle(previous[j],current[j+1],previous[j+1]); }
					record(indexStart,progress);
					previous=current;
				}
				for (size_t end : {first,last})
				{
					const auto& p=points[end]; const bool endpoint=end==0 || end==points.size()-1;
					// 圆头只覆盖截面外的半圆，并共用笔画端点；AA 带不再横跨圆头与笔画的内部接缝。
					if (end==0 && Length(points.front().pos,points.back().pos)<.001F) continue;
					const auto indexStart=static_cast<unsigned>(mesh.indices.size());
					const unsigned center=vertex(p.pos,p.distance);
					const auto section=end==first ? firstSection : lastSection;
					const auto normal=end==first ? firstNormal : lastNormal;
					const float startAngle=endpoint ? std::atan2(normal.y,normal.x) : 0;
					const float sweep=endpoint ? (end==0 ? 3.141592654F : -3.141592654F) : 6.283185307F;
					const int steps=endpoint ? 12 : 24;
					std::array<unsigned,2> previousRing{};
					for (int i=0;i<=steps;++i)
					{
						const float angle=startAngle+static_cast<float>(i)*sweep/steps;
						std::array<unsigned,2> ring;
						if (endpoint && i==0) ring={section[1],section[0]};
						else if (endpoint && i==steps) ring={section[2],section[3]};
						else for (int j=0;j<2;++j) { const float r=p.width*.5F+j*.8F; ring[j]=vertex({p.pos.x+std::cos(angle)*r,p.pos.y+std::sin(angle)*r},p.distance,j ? 0 : 255); }
						if (i) { triangle(center,previousRing[0],ring[0]); triangle(previousRing[0],previousRing[1],ring[1]); triangle(previousRing[0],ring[1],ring[0]); }
						previousRing=ring;
					}
					record(indexStart,p.time/points.back().time);
				}
				first=last;
			}
		}
		ImVec2 minimum(FLT_MAX,FLT_MAX), maximum(-FLT_MAX,-FLT_MAX);
		for (const auto& v : mesh.vertices) { minimum.x=(std::min)(minimum.x,v.pos.x); minimum.y=(std::min)(minimum.y,v.pos.y); maximum.x=(std::max)(maximum.x,v.pos.x); maximum.y=(std::max)(maximum.y,v.pos.y); }
		mesh.size={maximum.x-minimum.x,maximum.y-minimum.y};
		const auto& k=strokes[2]; const auto end=k.back().pos;
		mesh.kEnd={end.x-minimum.x,end.y-minimum.y}; mesh.kRadius=k.back().width*.5F;
		mesh.kInward=closedInward(k);
		for (auto& v : mesh.vertices)
		{
			v.pos.x-=minimum.x; v.pos.y-=minimum.y;
			v.normalized={v.pos.x/mesh.size.x-.5F,v.pos.y/mesh.size.y-.5F};
		}
		for (auto& stroke : mesh.strokes) for (auto& section : stroke.sections)
			section.center={section.center.x-minimum.x,section.center.y-minimum.y};
		return mesh;
	}
	inline const Mesh& Geometry() { static const Mesh mesh=Build(); return mesh; }
	inline float Height(float width) { const auto& mesh=Geometry(); return width*mesh.size.y/mesh.size.x; }
	struct RippleCache
	{
		struct Vertex { ImVec2 pos; float radial, edge; };
		std::vector<Vertex> vertices;
		std::vector<unsigned> indices;
		float width = -1, height = -1, dpi = -1, centerY = -1, reach = 0;
		std::uint64_t rebuilds = 0, allocations = 0;
	};

	inline float RippleReach(float width, float height, float centerY)
	{
		return std::hypot(width*.5F,(std::max)(centerY,height-centerY)*1.25F);
	}

	inline void PrepareRipple(RippleCache& cache, float width, float height, float dpi, float centerY)
	{
		if (cache.width==width && cache.height==height && cache.dpi==dpi && cache.centerY==centerY) return;
		// 固定 66×43 上限的圆角网格；尺寸改变才重建，波纹热路径只读径向距离。
		constexpr int columns=66, rows=43;
		if (cache.vertices.capacity()<columns*rows || cache.indices.capacity()<(columns-1)*(rows-1)*6) ++cache.allocations;
		cache.vertices.resize(columns*rows); cache.indices.clear(); cache.indices.reserve((columns-1)*(rows-1)*6);
		cache.width=width; cache.height=height; cache.dpi=dpi; cache.centerY=centerY;
		cache.reach=RippleReach(width,height,centerY); ++cache.rebuilds;
		const float r=(std::min)({8*dpi,width*.5F,height*.5F}), fringe=(std::min)(dpi,r*.125F);
		std::array<float,rows> ys{};
		ys[0]=0; ys[1]=fringe; ys[2]=r*.25F; ys[3]=r*.5F; ys[4]=r*.75F; ys[5]=r;
		for (int i=1;i<=32;++i) ys[5+i]=r+(height-2*r)*i/32;
		for (int i=0;i<5;++i) ys[38+i]=height-ys[4-i];
		for (int row=0;row<rows;++row)
		{
			const float y=ys[row], corner=(std::max)(0.0F,r-(std::min)(y,height-y));
			const float left=r-std::sqrt((std::max)(0.0F,r*r-corner*corner)), right=width-left;
			for (int column=0;column<columns;++column)
			{
				const float x=column==0 ? left : column==columns-1 ? right
					: left+fringe+(right-left-2*fringe)*(column-1)/(columns-3);
				cache.vertices[row*columns+column]={{x,y},std::hypot(x-width*.5F,(y-centerY)*1.25F)/cache.reach,
					row==0 || row==rows-1 || column==0 || column==columns-1 ? 0.0F : 1.0F};
				if (row && column)
				{
					const unsigned here=row*columns+column, above=here-columns;
					cache.indices.insert(cache.indices.end(),{above-1,here-1,here,above-1,here,above});
				}
			}
		}
	}

	inline float SheenAmount(float radial, float progress)
	{
		if (progress<=0 || progress>=1) return 0;
		const float fade=Smooth(progress/.08F)*(1-Smooth((progress-.8F)/.2F));
		return fade*(1-Smooth(std::abs(radial-progress*1.12F)/.14F));
	}

	inline ImU32 SheenColor(ImU32 original, float radial, float progress, bool dark)
	{
		const float amount=SheenAmount(radial,progress)*(dark ? .12F : .08F);
		if (amount<=0) return original;
		ImU32 color=original & IM_COL32_A_MASK;
		for (int shift : {IM_COL32_R_SHIFT,IM_COL32_G_SHIFT,IM_COL32_B_SHIFT})
		{
			const float channel=static_cast<float>((original>>shift)&255);
			color|=static_cast<ImU32>(std::round(channel+(255-channel)*amount))<<shift;
		}
		return color;
	}

	inline void DrawRipple(ImDrawList* draw, ImVec2 origin, const RippleCache& cache, float progress, bool dark)
	{
		if (progress<=0 || progress>=1) return;
		draw->PrimReserve(static_cast<int>(cache.indices.size()),static_cast<int>(cache.vertices.size()));
		const unsigned base=draw->_VtxCurrentIdx; const auto uv=ImGui::GetIO().Fonts->TexUvWhitePixel;
		for (unsigned index : cache.indices) draw->PrimWriteIdx(static_cast<ImDrawIdx>(base+index));
		for (const auto& v : cache.vertices)
		{
			// 中性薄光仅落在字标下方的背景，保留原渐变，避免鲜艳蓝环和大片漂白。
			const float opacity=SheenAmount(v.radial,progress)*v.edge*(dark ? .10F : .08F);
			const ImU32 color=IM_COL32(255,255,255,static_cast<int>(std::round(opacity*255)));
			draw->PrimWriteVtx({origin.x+v.pos.x,origin.y+v.pos.y},uv,color);
		}
	}

	struct DrawCache
	{
		struct Output { ImVec2 pos; ImU32 color; };
		std::vector<Output> vertices;
		float width = -1, dpi = -1, x = 0, y = 0;
		bool dark = false;
		std::uint64_t rebuilds = 0, allocations = 0;
	};

	inline void Prepare(DrawCache& cache, float width, float dpi, bool dark, float x, float y)
	{
		if (cache.width==width && cache.dpi==dpi && cache.dark==dark && cache.x==x && cache.y==y) return;
		const auto& mesh=Geometry();
		if (cache.vertices.capacity()<mesh.vertices.size()) ++cache.allocations;
		cache.vertices.resize(mesh.vertices.size()); ++cache.rebuilds;
		cache.width=width; cache.dpi=dpi; cache.dark=dark; cache.x=x; cache.y=y;
		const float height=Height(width), angle=.0698131701F;
		const float sx=std::sin(-y*angle), cx=std::cos(-y*angle), sy=std::sin(x*angle), cy=std::cos(x*angle);
		// 稳定姿态复用结果；运动热路径只修改预分配顶点，不重新采样曲线或创建纹理。
		for (size_t i=0;i<mesh.vertices.size();++i)
		{
			const auto& v=mesh.vertices[i]; auto& out=cache.vertices[i];
			const float px=v.normalized.x*width, py=v.normalized.y*height;
			const float rx=cy*px+sy*sx*py, ry=cx*py, rz=-sy*px+cy*sx*py;
			const float perspective=1200*dpi/(1200*dpi-rz);
			out.pos={width*.5F+rx*perspective+x*6*dpi,height*.5F+ry*perspective+y*6*dpi};
			// 开场不参与字标颜色或缓存键；原彩虹和透明 AA 仅随主题、姿态更新。
			out.color=dark ? v.dark : v.light;
		}
	}

	inline void Draw(ImDrawList* draw, ImVec2 origin, float width, bool dark, const DrawCache* cache = nullptr)
	{
		const auto& mesh=Geometry(); const float scale=width/mesh.size.x;
		draw->PrimReserve(static_cast<int>(mesh.indices.size()),static_cast<int>(mesh.vertices.size()));
		const unsigned base=draw->_VtxCurrentIdx; const auto uv=ImGui::GetIO().Fonts->TexUvWhitePixel;
		for (unsigned index : mesh.indices) draw->PrimWriteIdx(static_cast<ImDrawIdx>(base+index));
		if (cache)
			for (const auto& v : cache->vertices) draw->PrimWriteVtx({origin.x+v.pos.x,origin.y+v.pos.y},uv,v.color);
		else
			for (const auto& v : mesh.vertices) draw->PrimWriteVtx({origin.x+v.pos.x*scale,origin.y+v.pos.y*scale},uv,dark ? v.dark : v.light);
	}

	inline void DrawWriting(ImDrawList* draw, ImVec2 origin, float width, bool dark, float progress)
	{
		if (progress>=1) { Draw(draw,origin,width,dark); return; }
		if (progress<=0) return;
		const auto& mesh=Geometry(); const float scale=width/mesh.size.x;
		std::array<float,6> phases{}; unsigned count=0;
		for (size_t s=0;s<phases.size();++s)
		{
			// 微小错开六笔起点；每笔仍在总书写时间内完成，尾帧直接复用完整网格。
			const float delay=static_cast<float>(s)*.012F;
			phases[s]=Smooth((progress-delay)/(1-delay));
			if (phases[s]>0) for (const auto& block : mesh.strokes[s].blocks)
				if (block.progress<=phases[s]) count+=block.count;
		}
		const auto uv=ImGui::GetIO().Fonts->TexUvWhitePixel;
		draw->PrimReserve(static_cast<int>(count),static_cast<int>(mesh.vertices.size()));
		const unsigned base=draw->_VtxCurrentIdx;
		for (size_t s=0;s<phases.size();++s) if (phases[s]>0)
			for (const auto& block : mesh.strokes[s].blocks) if (block.progress<=phases[s])
				for (unsigned i=block.first;i<block.first+block.count;++i) draw->PrimWriteIdx(static_cast<ImDrawIdx>(base+mesh.indices[i]));
		for (const auto& v : mesh.vertices) draw->PrimWriteVtx({origin.x+v.pos.x*scale,origin.y+v.pos.y*scale},uv,dark ? v.dark : v.light);
		for (size_t s=0;s<phases.size();++s)
		{
			if (phases[s]<=0 || phases[s]>=1) continue;
			const auto& stroke=mesh.strokes[s]; const auto* section=&stroke.sections.front();
			for (const auto& candidate : stroke.sections) { if (candidate.progress>phases[s]) break; section=&candidate; }
			// 活动笔头共用已完成截面，透明 AA 只在外缘，不把圆盘透明环压到笔画内。
			auto cap=[&](const Mesh::Section& edge,bool start) {
				constexpr int steps=12; draw->PrimReserve(steps*9,23);
				static const auto circle=[] {
					std::array<ImVec2,steps+1> result{};
					for (int i=0;i<=steps;++i) { const float a=-3.141592654F*i/steps; result[i]={std::cos(a),std::sin(a)}; }
					return result;
				}();
				const unsigned center=draw->_VtxCurrentIdx;
				const auto& colorVertex=mesh.vertices[edge.vertices[1]];
				const ImU32 ink=dark ? colorVertex.dark : colorVertex.light;
				draw->PrimWriteVtx({origin.x+edge.center.x*scale,origin.y+edge.center.y*scale},uv,ink);
				std::array<unsigned,2> previous{base+edge.vertices[1],base+edge.vertices[0]};
				for (int i=1;i<=steps;++i)
				{
					std::array<unsigned,2> ring;
					if (i==steps) ring={base+edge.vertices[2],base+edge.vertices[3]};
					else for (int j=0;j<2;++j) {
						ring[j]=draw->_VtxCurrentIdx; const float r=edge.radius+j*.8F, sn=circle[i].y*(start ? -1.0F : 1.0F);
						const ImVec2 radial(edge.normal.x*circle[i].x-edge.normal.y*sn,edge.normal.y*circle[i].x+edge.normal.x*sn);
						draw->PrimWriteVtx({origin.x+(edge.center.x+radial.x*r)*scale,origin.y+(edge.center.y+radial.y*r)*scale},uv,j ? ink & ~IM_COL32_A_MASK : ink);
					}
					draw->PrimWriteIdx(static_cast<ImDrawIdx>(center)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(previous[0])); draw->PrimWriteIdx(static_cast<ImDrawIdx>(ring[0]));
					for (unsigned index : {previous[0],previous[1],ring[1],previous[0],ring[1],ring[0]}) draw->PrimWriteIdx(static_cast<ImDrawIdx>(index));
					previous=ring;
				}
			};
			if (stroke.closed) cap(stroke.sections.front(),true);
			cap(*section,false);
		}
	}
}
