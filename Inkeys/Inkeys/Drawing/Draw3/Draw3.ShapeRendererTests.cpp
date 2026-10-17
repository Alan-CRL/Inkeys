#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Draw3.ShapeRendererTests.h"
#include "../../../../inkStrokeModelerTest/draw3/shape_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

import Inkeys.Drawing.Draw3.renderer;

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
				D3D_DRIVER_TYPE_WARP), "production renderer + embedded six shader resources")) return 1;
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
