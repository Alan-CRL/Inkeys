#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Draw3.ShapeRendererTests.h"
#include "../../../../inkStrokeModelerTest/draw3/shape_core.h"
#include "../../../../inkStrokeModelerTest/draw3/shape_interaction.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;
import Inkeys.Drawing.Draw3.ink_history_gpu;
import Inkeys.Drawing.Draw3.ink_prediction;

namespace Inkeys::Drawing::Draw3
{
	int RunShapeRendererTests() noexcept
	{
		try
		{
			using namespace ::draw3::shapes;
			using Microsoft::WRL::ComPtr;
			int failures = 0, cases = 0;
			const auto check = [&](bool passed, const char* name)
			{
				++cases;
				if (!passed) ++failures;
				std::fprintf(stderr, "[ShapeRenderer] %s %s\n", passed ? "PASS" : "FAIL", name);
				return passed;
			};
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11DeviceContext> context;
			const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION,
				device.GetAddressOf(), nullptr, context.GetAddressOf())), "no-HWND WARP device")) return 1;
			InkRenderer renderer;
			if (!check(renderer.Init(device.Get(), context.Get(), nullptr, 128, 128,
				D3D_DRIVER_TYPE_WARP, false), "production renderer + embedded six shader resources")) return 1;
			D3D11_TEXTURE2D_DESC description = {};
			renderer.layerL0.addTexture->GetDesc(&description);
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging;
			if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr,
				staging.GetAddressOf())), "readback texture")) return 1;
			const auto read = [&](std::vector<unsigned char>& result, ID3D11Texture2D* source = nullptr)
			{
				context->OMSetRenderTargets(0, nullptr, nullptr);
				context->CopyResource(staging.Get(), source ? source : renderer.layerL0.addTexture.Get());
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
				result.resize(128 * 128 * 4);
				for (size_t y = 0; y < 128; ++y)
					std::memcpy(result.data() + y * 128 * 4,
						static_cast<unsigned char*>(mapped.pData) + y * mapped.RowPitch, 128 * 4);
				context->Unmap(staging.Get(), 0);
				return true;
			};
			uint8_t identity = 1;
			const auto object = [&]()
			{
				ShapeObject result;
				result.id[0] = identity++;
				result.style.strokeRgb = 0xFF0000;
				result.style.fillRgb = 0x2060E0;
				result.style.width = 6;
				return result;
			};
			const auto render = [&](const ShapeObject& shape, std::vector<unsigned char>& result,
				float originX = 0, float originY = 0, float extent = 128)
			{
				renderer.SetScreenSize(extent, extent);
				renderer.ClearOperatorLayer(renderer.layerL0);
				renderer.SetOperatorTarget(renderer.layerL0);
				return renderer.DrawShapeObject(shape, originX, originY) && read(result);
			};
			const auto alpha = [](const std::vector<unsigned char>& pixels, int x, int y)
				{ return pixels[(y * 128 + x) * 4 + 3]; };
			std::vector<unsigned char> pixels, reference;
			const std::array<const char*, 7> legacyNames = { "legacy solid line", "legacy dashed line",
				"legacy outline rectangle", "legacy filled rectangle", "ordinary pen", "highlighter", "Laser" };
			std::array<std::vector<unsigned char>, 7> legacyBaselines;
			const auto renderLegacy = [&](size_t tool, std::vector<unsigned char>& result)
			{
				renderer.SetScreenSize(128, 128);
				renderer.ClearOperatorLayer(renderer.layerL0);
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.SetOperatorTarget(renderer.layerL0);
				const std::array<InkPoint, 3> points = { InkPoint{20,30,3,0},
					InkPoint{64,64,6,0}, InkPoint{105,45,2,0} };
				if (tool < 4)
				{
					const std::array<ShapePrimitive, 1> primitive = { ShapePrimitive{
						{20,20,3,0}, {100,100,0,0} } };
					if (renderer.DrawShapePrimitives(primitive,
						static_cast<ShapePrimitiveKind>(16 + tool), {1,0,0,0.65f}) < 0) return false;
				}
				else if (tool == 4)
				{
					if (renderer.DrawStrokeOrDot(points, {0.25f,0.5f,0.75f,0.65f}) < 0) return false;
				}
				else if (tool == 5)
				{
					const std::array<HighlighterPrimitive, 1> primitive = { HighlighterPrimitive{
						{25,35}, {104,91}, {2,13} } };
					if (renderer.DrawHighlighterPrimitives(primitive, {1,0.75f,0.25f,0.4f}) < 0) return false;
				}
				else
				{
					renderer.ConfigureLaserStyle(1, {1,0.125f,0.25f,1});
					renderer.ClearLaserCoverage(renderer.laserStrokeCoverage);
					renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
					if (renderer.DrawLaserCoverage(points) < 0 || !renderer.ResolveLaserStrokeCoverage(
						renderer.backBufferRTV.Get(), {0,0,128,128}, 0.65f)) return false;
					return read(result, renderer.backBufferTexture.Get());
				}
				return renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL0,
					renderer.layerL1, {0,0,128,128}) && read(result, renderer.backBufferTexture.Get());
			};
			// 必须先于任何通用图形调用采集基线，避免把两次相同的新状态误当回归。
			for (size_t tool = 0; tool < legacyBaselines.size(); ++tool)
			{
				const bool rendered = renderLegacy(tool, legacyBaselines[tool]);
				bool hasCoverage = false;
				if (rendered) for (size_t offset = 3; offset < legacyBaselines[tool].size(); offset += 4)
					hasCoverage |= legacyBaselines[tool][offset] > 0;
				char name[128] = {};
				std::snprintf(name, sizeof(name), "pre-generic nonempty baseline: %s", legacyNames[tool]);
				if (!check(rendered && hasCoverage && alpha(legacyBaselines[tool], 2, 2) == 0, name)) return 1;
			}
			// 三种样式、四条线型以及圆/椭圆/圆角矩形均调用真实几何与 shader。
			for (Kind kind : { Kind::Circle, Kind::Ellipse, Kind::Rectangle })
				for (unsigned appearance = 0; appearance < 3; ++appearance)
					for (unsigned pattern = 0; pattern < 4; ++pattern)
					{
						auto shape = object();
						shape.kind = kind;
						shape.geometry.start = kind == Kind::Rectangle ? Vec2{ 22, 24 } : Vec2{ 64, 64 };
						shape.geometry.end = kind == Kind::Rectangle ? Vec2{ 106, 104 } : Vec2{ 38, 26 };
						shape.geometry.radius = kind == Kind::Rectangle ? 12.0f : 32.0f;
						shape.style.stroke = appearance != 1;
						shape.style.fill = appearance != 0;
						shape.style.linePattern = static_cast<LinePattern>(pattern);
						const bool rendered = render(shape, pixels);
						check(rendered && alpha(pixels, 64, 64) == (shape.style.fill ? 255 : 0) &&
							alpha(pixels, 2, 2) == 0, "analytic/contour kind x style x pattern");
					}
			// P2 预设通过正式 factory/renderer/嵌入 shader；同一对象再走 tile 原点路径。
			for (unsigned preset = 0; preset <= static_cast<unsigned>(PresetKind::RegularPolygon); ++preset)
				for (unsigned appearance = 0; appearance < 3; ++appearance)
					for (unsigned pattern = 0; pattern < 4; ++pattern)
					{
						const auto kind = static_cast<PresetKind>(preset);
						const bool open = kind == PresetKind::Line || kind == PresetKind::Arrow;
						if (open && appearance != 0) continue;
						ToolSpec tool; tool.preset = kind;
						if (kind == PresetKind::Circle || kind == PresetKind::RegularPolygon)
							tool.creationMode = CreationMode::CenterRadius;
						tool.sides = 7;
						auto base = object(); base.style.stroke = appearance != 1;
						base.style.fill = appearance != 0; base.style.linePattern = static_cast<LinePattern>(pattern);
						base.style.strokeOpacity = base.style.fillOpacity = 0.5f;
						const bool centered = tool.creationMode == CreationMode::CenterRadius;
						const auto shape = MakePresetShape(tool, centered ? Vec2{64,64} : Vec2{24,24},
							centered ? Vec2{98,70} : Vec2{104,102},base.style,base.id);
						bool rendered = shape && render(*shape,reference);
						bool nonempty = false;
						if (rendered) for (size_t offset = 3; offset < reference.size(); offset += 4)
							nonempty |= reference[offset] > 0;
						// 活动层直接消费 RenderData，最终对象带 viewport 变换；两条生产路径应逐像素一致。
						bool liveAndStoredMatch = rendered;
						if (liveAndStoredMatch)
						{
							RenderData liveData;
							renderer.ClearOperatorLayer(renderer.layerL0); renderer.SetOperatorTarget(renderer.layerL0);
							liveAndStoredMatch = BuildRenderData(*shape,liveData) && renderer.DrawShapeRenderData(liveData) &&
								read(pixels) && pixels == reference;
							auto stored = *shape;
							stored.transform.dx += 25; stored.transform.dy -= 18; ++stored.revision;
							liveAndStoredMatch = render(stored,pixels,25,-18) && liveAndStoredMatch;
							if (liveAndStoredMatch) for (size_t pixel = 0; pixel < pixels.size(); ++pixel)
								liveAndStoredMatch &= std::abs(int(pixels[pixel]) - int(reference[pixel])) <= 1;
						}
						check(liveAndStoredMatch,"active RenderData matches final transformed object and viewport replay");
						bool tileMatches = rendered && render(*shape,pixels,32,32,64);
						if (tileMatches)
						{
							size_t differing = 0; int maxDelta = 0;
							for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
								for (int channel = 0; channel < 4; ++channel)
								{
									const int actual = pixels[(y * 128 + x) * 4 + channel];
									const int expected = reference[((y + 32) * 128 + x + 32) * 4 + channel];
									if (actual != expected) { ++differing; maxDelta = std::max(maxDelta,std::abs(actual - expected)); }
								}
							tileMatches = differing == 0;
							if (!tileMatches) std::fprintf(stderr,"[ShapeRenderer] tile differences preset=%u style=%u pattern=%u channels=%zu maxDelta=%d\n",
								preset,appearance,pattern,differing,maxDelta);
						}
						char name[128]; std::snprintf(name,sizeof(name),"preset=%u style=%u pattern=%u nonempty and tile-equivalent",preset,appearance,pattern);
						check(rendered && nonempty && tileMatches && alpha(reference,2,2) == 0,name);
					}
			// 所有新增模板直接走正式构造器和 renderer，不用测试专用三角形替代生产几何。
			for (const auto preset : {PresetKind::Polyline, PresetKind::Polygon, PresetKind::Arc,
				PresetKind::Sector, PresetKind::Cuboid, PresetKind::Cube, PresetKind::Cylinder,
				PresetKind::Cone, PresetKind::Prism, PresetKind::Pyramid})
				for (unsigned pattern = 0; pattern < 4; ++pattern)
					for (unsigned direction = 0; direction <
						((preset == PresetKind::Arc || preset == PresetKind::Sector) ? 2u : 1u); ++direction)
					{
						Construction construction;
						construction.tool.preset = preset;
						construction.tool.creationMode = DefaultCreationMode(preset);
						construction.tool.sides = 5;
						construction.start = {25,60}; construction.end = {70,105};
						if (preset == PresetKind::Polyline || preset == PresetKind::Polygon)
							construction.parameters = VertexConstruction{preset == PresetKind::Polyline ?
								std::vector<Vec2>{{24,28},{94,28},{48,102},{104,102}} :
								std::vector<Vec2>{{24,24},{104,24},{104,104},{64,60},{24,104}}};
						else if (preset == PresetKind::Arc || preset == PresetKind::Sector)
						{
							construction.start = {64,64}; construction.end = {98,64};
							construction.parameters = ArcConstruction{direction ? -4.4f : 4.4f};
						}
						else if (preset == PresetKind::Cuboid || preset == PresetKind::Cube)
							construction.parameters = SolidConstruction{{23,-32},0};
						else
						{
							construction.start = {25,84}; construction.end = {102,108};
							if (preset == PresetKind::Prism || preset == PresetKind::Pyramid)
							{
								construction.start = {64,96}; construction.end = {95,96};
							}
							construction.parameters = SolidConstruction{{},-50};
						}
						auto base = object(); base.style.width = 4; base.style.strokeOpacity = 0.5f;
						base.style.linePattern = static_cast<LinePattern>(pattern);
						std::string buildError;
						const auto shape = MakeConstructedShape(construction,base.style,base.id,1,&buildError);
						RenderData data;
						bool complete = shape && BuildRenderData(*shape,data,{},&buildError) && render(*shape,reference);
						bool nonempty = false, halfAlpha = true;
						if (complete) for (size_t offset = 3; offset < reference.size(); offset += 4)
						{
							nonempty |= reference[offset] > 0;
							halfAlpha &= reference[offset] <= 128;
						}
						bool liveMatches = complete;
						if (liveMatches)
						{
							renderer.ClearOperatorLayer(renderer.layerL0); renderer.SetOperatorTarget(renderer.layerL0);
							liveMatches = renderer.DrawShapeRenderData(data) && read(pixels) && pixels == reference;
						}
						bool tilesMatch = complete;
						for (const int inset : {16,32})
						{
							const int extent = 128 - inset * 2;
							const bool tileRendered = complete && render(*shape,pixels,float(inset),float(inset),float(extent));
							tilesMatch &= tileRendered;
							if (tileRendered) for (int y = 0; y < extent; ++y) for (int x = 0; x < extent; ++x)
								for (int channel = 0; channel < 4; ++channel)
									tilesMatch &= pixels[(y * 128 + x) * 4 + channel] ==
										reference[((y + inset) * 128 + x + inset) * 4 + channel];
						}
						// 逐批独立描边的最大覆盖是参考并集；hidden Dashed 与 visible 线型均须保留。
						bool unionMatches = complete;
						if (complete && shape->kind == Kind::Compound)
						{
							std::vector<unsigned char> expected(reference.size(),0);
							bool hasHidden = false;
							for (const auto& batch : data.batches)
							{
								hasHidden |= batch.style.linePattern == LinePattern::Dashed;
								RenderData separate; separate.bounds = data.bounds; separate.batches = {batch};
								renderer.SetScreenSize(128,128); renderer.ClearOperatorLayer(renderer.layerL0);
								renderer.SetOperatorTarget(renderer.layerL0);
								if (!renderer.DrawShapeRenderData(separate) || !read(pixels)) { unionMatches = false; break; }
								for (size_t offset = 0; offset < pixels.size(); ++offset)
									expected[offset] = std::max(expected[offset],pixels[offset]);
							}
							unionMatches &= hasHidden && expected == reference;
						}
						char name[160]; std::snprintf(name,sizeof(name),
							"P3/P4 preset=%u pattern=%u direction=%u active/final, half-alpha union and 64/96 tile pixels",
							static_cast<unsigned>(preset),pattern,direction);
						const bool passed = complete && nonempty && halfAlpha && liveMatches && tilesMatch && unionMatches;
						if (!passed) std::fprintf(stderr,
							"[ShapeRenderer] complete=%d nonempty=%d halfAlpha=%d live=%d tiles=%d union=%d error=%s\n",
							complete,nonempty,halfAlpha,liveMatches,tilesMatch,unionMatches,buildError.c_str());
						check(passed,name);
					}
			for (unsigned appearance = 0; appearance < 2; ++appearance)
			{
				Construction sector; sector.tool.preset = PresetKind::Sector;
				sector.tool.creationMode = DefaultCreationMode(PresetKind::Sector);
				sector.start = {64,64}; sector.end = {98,64}; sector.parameters = ArcConstruction{4.4f};
				auto base = object(); base.style.stroke = appearance != 0; base.style.fill = true;
				base.style.strokeOpacity = base.style.fillOpacity = 0.5f;
				const auto shape = MakeConstructedShape(sector,base.style,base.id);
				check(shape && render(*shape,pixels) && std::abs(alpha(pixels,75,75) - 128) <= 1 &&
					alpha(pixels,105,64) == 0,"large sector fill and ordered outline keep interior half alpha");
			}
			ToolSpec arrowTool; arrowTool.preset = PresetKind::Arrow;
			auto arrowBase = object(); arrowBase.style.width = 6; arrowBase.style.strokeOpacity = 0.5f;
			arrowBase.style.linePattern = LinePattern::Dashed;
			const auto arrow = MakePresetShape(arrowTool,{20,64.5f},{105,64.5f},arrowBase.style,arrowBase.id);
			bool arrowCoverage = arrow && render(*arrow,pixels);
			if (arrowCoverage)
			{
				GeometryResult geometry;
				arrowCoverage = BuildGeometry(*arrow,geometry);
				// 选择头部两条边中点：即使对应箭杆虚线相位为间隔，也必须保持实线半透明。
				for (const auto& part : geometry.parts) if (part.partId == 2)
					for (const auto& contour : part.paths)
						for (size_t edge = 1; edge < contour.points.size(); ++edge)
						{
							const auto a = contour.points[edge - 1], b = contour.points[edge];
							const int x = static_cast<int>((a.x + b.x) * 0.5f), y = static_cast<int>((a.y + b.y) * 0.5f);
							arrowCoverage &= std::abs(alpha(pixels,x,y) - 128) <= 1;
						}
				arrowCoverage &= alpha(pixels,55,64) == 0 && std::abs(alpha(pixels,104,64) - 128) <= 1;
				for (size_t offset = 3; offset < pixels.size(); offset += 4) arrowCoverage &= pixels[offset] <= 128;
			}
			check(arrowCoverage,"dashed shaft, solid arrow head and half-transparent union at tip");
			// P3/P4 的连续同材质部件允许各自使用不同线型，不能重复叠加透明度。
			auto sameMaterial = object(); sameMaterial.kind = Kind::Compound;
			sameMaterial.style.strokeOpacity = 0.5f;
			ShapePart horizontal, vertical;
			horizontal.id = 1; horizontal.kind = Kind::Line;
			horizontal.geometry.start = {20,64.5f}; horizontal.geometry.end = {108,64.5f};
			vertical.id = 2; vertical.kind = Kind::Line;
			vertical.geometry.start = {64.5f,64.5f}; vertical.geometry.end = {64.5f,110};
			vertical.style = sameMaterial.style; vertical.style->linePattern = LinePattern::Dashed;
			sameMaterial.parts = {horizontal,vertical};
			check(render(sameMaterial,pixels) && std::abs(alpha(pixels,64,64) - 128) <= 1 &&
				alpha(pixels,64,104) == 0 && std::abs(alpha(pixels,64,72) - 128) <= 1,
				"compound mixed-dash material union retains hidden-edge gaps and half alpha");
			// 不能跨不同颜色或填充合并相同颜色的部件，否则会改变 A/B/A 层序。
			auto strokeBarrier = object(); strokeBarrier.kind = Kind::Compound;
			strokeBarrier.style.strokeOpacity = 0.5f;
			ShapePart lower = horizontal, middle = horizontal, upper = horizontal;
			lower.id = 1; middle.id = 2; upper.id = 3;
			middle.style = strokeBarrier.style; middle.style->strokeRgb = 0x0000FF;
			strokeBarrier.parts = {lower,middle,upper};
			const auto barrierPixel = (64 * 128 + 64) * 4;
			const auto hasOrderedBarrier = [&]()
			{
				return std::abs(pixels[barrierPixel] - 64) <= 2 &&
					std::abs(pixels[barrierPixel + 2] - 160) <= 2 &&
					std::abs(pixels[barrierPixel + 3] - 224) <= 2;
			};
			check(render(strokeBarrier,pixels) && hasOrderedBarrier(),
				"different stroke material is an ordered barrier for repeated colors");
			auto fillBarrier = strokeBarrier; fillBarrier.id = object().id;
			fillBarrier.parts[1].kind = Kind::Rectangle;
			fillBarrier.parts[1].geometry.start = {40,40}; fillBarrier.parts[1].geometry.end = {90,90};
			fillBarrier.parts[1].style->stroke = false; fillBarrier.parts[1].style->fill = true;
			fillBarrier.parts[1].style->fillRgb = 0x0000FF; fillBarrier.parts[1].style->fillOpacity = 0.5f;
			check(render(fillBarrier,pixels) && hasOrderedBarrier(),
				"fill separates equal stroke materials and preserves source-over");
			// 混合旧/新 contact 按 Down 顺序 source-over；颜色分量 MAX 会产生不同像素。
			auto overlay = object(); overlay.kind = Kind::Rectangle;
			overlay.geometry.start = {30,30}; overlay.geometry.end = {95,95};
			overlay.style.stroke = false; overlay.style.fill = true; overlay.style.fillRgb = 0x0000FF; overlay.style.fillOpacity = 0.5f;
			const ShapePrimitive oldRectangle{{20,20,3,0},{110,110,0,0}};
			renderer.SetScreenSize(128,128); renderer.ClearOperatorLayer(renderer.layerL0); renderer.SetOperatorTarget(renderer.layerL0);
			const bool mixed = renderer.DrawShapeObject(overlay) &&
				renderer.DrawOrderedLegacyShapePrimitive(oldRectangle,ShapePrimitiveKind::FilledRectangle,{1,0,0,0.5f}) && read(pixels);
			const auto crossing = (64 * 128 + 64) * 4;
			check(mixed && std::abs(pixels[crossing + 2] - 128) <= 2 && std::abs(pixels[crossing] - 64) <= 2 &&
				std::abs(pixels[crossing + 3] - 192) <= 2,"mixed legacy/new objects preserve source-over order");
			// 100:1 椭圆尖端内侧到轮廓很近，近似解析 SDF 曾把这里误判为空白。
			auto slenderEllipse = object();
			slenderEllipse.kind = Kind::Ellipse;
			slenderEllipse.geometry.start = {15.5f,64.5f};
			slenderEllipse.geometry.end = {100,1};
			slenderEllipse.style.width = 0.5f;
			check(render(slenderEllipse, pixels) && alpha(pixels, 114, 64) > 100 &&
				alpha(pixels, 15, 63) > 150 && alpha(pixels, 15, 64) == 0 &&
				alpha(pixels, 117, 64) == 0 && alpha(pixels, 114, 60) == 0,
				"100:1 slender ellipse stroke retains tip coverage and empty interior");
			auto polygon = object();
			polygon.kind = Kind::Polygon;
			polygon.geometry.vertices = { {20,20}, {108,20}, {108,108}, {64,60}, {20,108} };
			polygon.style.stroke = false; polygon.style.fill = true;
			polygon.style.fillOpacity = 0.5f;
			check(render(polygon, pixels) && std::abs(alpha(pixels, 40, 40) - 128) <= 1 &&
				alpha(pixels, 64, 100) == 0, "concave fill and exterior exclusion");
			bool seams = true;
			for (int y = 25; y < 50; ++y)
				for (int x = 25; x < 104; ++x) seams &= std::abs(alpha(pixels, x, y) - 128) <= 1;
			check(seams, "half-transparent triangulation has no internal seams");
			auto compound = object();
			compound.kind = Kind::Compound;
			ShapePart first, second;
			first.id = 1; second.id = 2;
			first.kind = second.kind = Kind::Rectangle;
			first.geometry.start = { 20,20 }; first.geometry.end = { 85,85 };
			second.geometry.start = { 45,45 }; second.geometry.end = { 110,110 };
			ShapeStyle red, blue;
			red.stroke = blue.stroke = false; red.fill = blue.fill = true;
			red.fillRgb = 0xFF0000; blue.fillRgb = 0x0000FF;
			red.fillOpacity = blue.fillOpacity = 0.5f;
			first.style = red; second.style = blue; compound.parts = { first, second };
			const bool ordered = render(compound, pixels);
			const size_t overlap = (64 * 128 + 64) * 4;
			check(ordered && std::abs(pixels[overlap] - 128) <= 2 &&
				std::abs(pixels[overlap + 2] - 64) <= 2 && std::abs(pixels[overlap + 3] - 192) <= 2,
				"compound ordered source-over colors and opacity");
			auto path = object();
			path.kind = Kind::Polyline;
			path.geometry.vertices = { { 15, 70 }, { 55, 70 }, { 55, 110 } };
			path.style.strokeOpacity = 0.5f;
			check(render(path, pixels) && std::abs(alpha(pixels, 54, 70) - 128) <= 1,
				"shared segment endpoint coverage union");
			for (unsigned pattern = 1; pattern < 4; ++pattern)
			{
				path.id[0] = identity++;
				path.style.linePattern = static_cast<LinePattern>(pattern);
				check(render(path, reference), "continuous path dash phase submission");
				bool matches = true;
				for (int y = 75; y < 105; ++y)
				{
					const float distance = 40 + static_cast<float>(y) + 0.5f - 70;
					// 仅比较远离 AA 边界的稳定区域，圆帽的横向截面由 GPU 距离决定。
					const bool inside = IsDashVisible(distance, path.style);
					if (inside && IsDashVisible(distance - 1, path.style) && IsDashVisible(distance + 1, path.style))
						matches &= alpha(reference, 55, y) > 100;
					if (!inside && !IsDashVisible(distance - 1, path.style) && !IsDashVisible(distance + 1, path.style))
						matches &= alpha(reference, 55, y) < 10;
				}
				check(matches, "GPU dash visibility agrees with core across path corner");
			}
			check(render(compound, reference), "full-canvas reference");
			check(render(compound, pixels, 32, 32, 64), "tile-origin production path");
			bool identical = true;
			for (int y = 0; y < 64; ++y)
				for (int x = 0; x < 64; ++x)
					for (int channel = 0; channel < 4; ++channel)
						identical &= pixels[(y * 128 + x) * 4 + channel] ==
							reference[((y + 32) * 128 + x + 32) * 4 + channel];
			check(identical, "tile pixels equal full-canvas crop");
			// 权威页面安装可能复用文件中的 ID/revision，必须显式清掉旧几何缓存。
			auto restored = object(); restored.kind = Kind::Line;
			restored.geometry.start = {20,24.5f}; restored.geometry.end = {108,24.5f};
			bool invalidated = render(restored,reference);
			restored.geometry.start.y = restored.geometry.end.y = 100.5f;
			renderer.InvalidateShapeCache(restored.id);
			invalidated &= render(restored,pixels) && alpha(pixels,64,24) == 0 && alpha(pixels,64,100) == 255;
			check(invalidated,"per-object invalidation evicts all restored geometry revisions");
			restored.geometry.start.y = restored.geometry.end.y = 60.5f;
			renderer.ClearShapeRenderCache();
			check(render(restored,pixels) && alpha(pixels,64,100) == 0 && alpha(pixels,64,60) == 255,
				"page installation clear cannot reuse former page shape cache");
			#include "Draw3.ShapeRendererEditTests.inl"
			// 超过一批 50000 项；强制不支持 NO_OVERWRITE，验证第二批零偏移上传。
			RenderData many;
			many.bounds = { 20,20,100,100,true };
			RenderBatch batch;
			batch.style.strokeRgb = 0x00FF00;
			GpuPrimitive primitive;
			primitive.p0 = { 25, 50 }; primitive.p1 = { 100, 50 }; primitive.halfWidth = 2;
			batch.primitives.assign(InkRenderer::kMaxBufferCapacity / 4 + 1, primitive);
			many.batches.push_back(std::move(batch));
			renderer.SetScreenSize(128, 128);
			renderer.ClearOperatorLayer(renderer.layerL0);
			renderer.SetOperatorTarget(renderer.layerL0);
			renderer.mapNoOverwriteOnDynamicBufferSRV = false;
			check(renderer.DrawShapeRenderData(many) && renderer.m_bufferHead == 4 && read(pixels) &&
				alpha(pixels, 50, 50) == 255, "four-slot batch boundary with DISCARD fallback");
			// 注入资源丢失，目标已提交内容必须保持原值且 b4/t0 在退出时解绑。
			reference = pixels;
			auto savedBuffer = renderer.inkDataBuffer;
			renderer.inkDataBuffer.Reset();
			renderer.SetOperatorTarget(renderer.layerL0);
			check(!renderer.DrawShapeObject(compound) && read(pixels) && pixels == reference,
				"missing upload resource leaves caller operator unchanged");
			renderer.inkDataBuffer = savedBuffer;
			// 失败的首次绘制不能缓存为成功；同一权威身份恢复后须重新生成候选。
			auto failedCandidate = object(); failedCandidate.kind = Kind::Line;
			failedCandidate.geometry.start = {20,24.5f}; failedCandidate.geometry.end = {108,24.5f};
			renderer.inkDataBuffer.Reset();
			const bool rejectedCandidate = !renderer.DrawShapeObject(failedCandidate);
			renderer.inkDataBuffer = savedBuffer;
			failedCandidate.geometry.start.y = failedCandidate.geometry.end.y = 100.5f;
			check(rejectedCandidate && render(failedCandidate,pixels) && alpha(pixels,64,24) == 0 &&
				alpha(pixels,64,100) == 255,"failed candidate does not publish a reusable render cache entry");
			// 下一个故障检查仍以当前调用方像素为基准。
			reference = pixels;
			D3D11_BUFFER_DESC invalidMapDescription = {};
			savedBuffer->GetDesc(&invalidMapDescription);
			invalidMapDescription.Usage = D3D11_USAGE_DEFAULT;
			invalidMapDescription.CPUAccessFlags = 0;
			ComPtr<ID3D11Buffer> invalidMapBuffer;
			const bool invalidCreated = SUCCEEDED(device->CreateBuffer(&invalidMapDescription, nullptr,
				invalidMapBuffer.GetAddressOf()));
			renderer.inkDataBuffer = invalidMapBuffer;
			renderer.SetOperatorTarget(renderer.layerL0);
			check(invalidCreated && !renderer.DrawShapeObject(compound) && read(pixels) && pixels == reference,
				"actual upload Map failure leaves caller unchanged");
			renderer.inkDataBuffer = savedBuffer;
			ComPtr<ID3D11Buffer> boundConstants;
			ComPtr<ID3D11ShaderResourceView> boundSource;
			context->PSGetConstantBuffers(4, 1, boundConstants.GetAddressOf());
			context->VSGetShaderResources(0, 1, boundSource.GetAddressOf());
			check(!boundConstants && !boundSource, "temporary b4 and t0 unbound");
			for (size_t tool = 0; tool < legacyBaselines.size(); ++tool)
			{
				char name[128] = {};
				std::snprintf(name, sizeof(name), "post-generic pixels equal original baseline: %s", legacyNames[tool]);
				check(renderLegacy(tool, pixels) && pixels == legacyBaselines[tool], name);
			}
			check(SUCCEEDED(device->GetDeviceRemovedReason()), "device remains healthy");
			context->ClearState();
			renderer.ReleaseResources();
			std::fprintf(stderr, "[ShapeRenderer] cases=%d failures=%d result=%s\n", cases, failures,
				failures ? "FAIL" : "PASS");
			return failures ? 1 : 0;
		}
		catch (...)
		{
			std::fprintf(stderr, "[ShapeRenderer] FAIL exception\n");
			return 1;
		}
	}
}
