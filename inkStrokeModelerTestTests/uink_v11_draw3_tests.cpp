#include "../inkStrokeModelerTest/draw3/shape_core.h"
#include "../inkStrokeModelerTest/draw3/shape_interaction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

import draw3.uink_store;
import draw3.uink_draw3_v11;

namespace
{
	using namespace draw3::uink;

	struct V11Test
	{
		const char* name;
		int failures = 0;

		bool Check(bool passed, const char* expression, int line)
		{
			if (!passed)
			{
				++failures;
				std::cerr << "UInk Draw3 1.1 FAILED " << name << ':' << line << ": " << expression << '\n';
			}
			return passed;
		}
	};

#define V11_CHECK(expression) test.Check(!!(expression), #expression, __LINE__)
#define V11_REQUIRE(expression) do { if (!V11_CHECK(expression)) return; } while (false)

	UInkGuid TestPageGuid(uint8_t id)
	{
		return UInkGuid({ 0x30, 0, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, id });
	}

	Draw3UInkCanvasSnapshot TestPage(uint8_t id = 1)
	{
		Draw3UInkCanvasSnapshot page;
		page.pageGuid = TestPageGuid(id);
		page.pageNumber = 1;
		return page;
	}

	Draw3UInkStrokeSnapshot TestStroke(float x, uint32_t undoId = 0)
	{
		Draw3UInkStrokeSnapshot stroke;
		stroke.style.fallbackRgb = 0x2468ac;
		stroke.points.push_back({ x, -x, 2.25f });
		stroke.undoId = undoId;
		return stroke;
	}

	UInkInk TestInk(float x, uint32_t contentId = 0, uint32_t undoId = 0)
	{
		UInkInk ink;
		ink.contentId = contentId;
		ink.undoId = undoId;
		ink.points.push_back({ x, -x, 2.25f, std::nullopt });
		return ink;
	}

	std::optional<UInkCanvas> DecodePageWire(const UInkEncodeResult& wire,
		const std::optional<UInkHeaderExtension>& extension = std::nullopt)
	{
		if (wire.status != UInkEncodeStatus::Success) return std::nullopt;
		UInkStreamContext context;
		context.usesImplicitWorkspace = !extension;
		context.headerExtension = extension;
		auto read = DecodeUInkStream(wire.bytes, {}, &context);
		if (read.status != UInkReadStatus::Complete || !read.document ||
			read.document->canvases.size() != 1) return std::nullopt;
		return std::move(read.document->canvases.front());
	}

	void TestEraserNibRoundTrip(V11Test& test)
	{
		auto page = TestPage();
		const std::array sizes{ 0.000001f, 0.125f, 17.5f, 50000.0f };
		for (uint8_t eraserShape = 0; eraserShape <= 2; ++eraserShape)
		{
			auto stroke = TestStroke(10, eraserShape);
			stroke.style.kind = Draw3UInkStrokeKind::Eraser;
			stroke.style.eraserShape = eraserShape;
			stroke.points.clear();
			for (float size : sizes)
				stroke.points.push_back({ 10.0f + static_cast<float>(stroke.points.size()), 0, size });
			page.strokes.push_back(std::move(stroke));
		}
		const auto exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
		const auto decoded = DecodePageWire(EncodeUInkPage(*exported.canvas, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == 3);
		const auto read = ImportUInkV11Page(*decoded);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page);
		V11_REQUIRE(read.page->strokes.size() == 3);
		V11_CHECK(read.counts.nib == 0 && read.counts.cap == 0);
		for (uint8_t shape = 0; shape <= 2; ++shape)
		{
			const auto* ink = std::get_if<UInkInk>(&decoded->content[shape]);
			V11_REQUIRE(ink && ink->points.size() == sizes.size());
			V11_CHECK(ink->declaredInkType == 0 && !ink->cap);
			const double aspect = shape == 1 ? 0.6666667 : 1.0;
			if (shape != 0)
			{
				V11_REQUIRE(ink->nib && ink->nib->aspect);
				V11_CHECK(ink->nib->shape == 1 && ink->nib->orientation == 0);
				V11_CHECK(std::abs(*ink->nib->aspect - aspect) < 1e-7);
			}
			else
				V11_CHECK(!ink->nib);
			const auto& stroke = read.page->strokes[shape];
			V11_REQUIRE(stroke.points.size() == sizes.size());
			V11_CHECK(stroke.style.kind == Draw3UInkStrokeKind::Eraser && stroke.style.eraserShape == shape);
			for (size_t index = 0; index < sizes.size(); ++index)
			{
				const double expected = shape == 0 ? sizes[index] :
					(sizes[index] / 2.0) * std::sqrt(std::numbers::pi / aspect);
				V11_CHECK(std::abs(ink->points[index].width - expected) / expected <= 1e-5);
				V11_CHECK(std::abs(stroke.points[index].width - sizes[index]) / sizes[index] <= 1e-5f);
			}
		}
	}

	void TestEraserSizeLimits(V11Test& test)
	{
		constexpr float minimum = 2.0f * (std::numeric_limits<float>::min)();
		constexpr float maximum = 0x1p127f;
		constexpr float largest = (std::numeric_limits<float>::max)();
		constexpr float tiny = 4.0f * (std::numeric_limits<float>::denorm_min)();
		const std::array sizes{ tiny, minimum, maximum, largest };
		const std::array expected{ minimum, minimum, maximum, maximum };
		auto page = TestPage();
		for (uint8_t shape = 0; shape < 3; ++shape)
		{
			auto eraser = TestStroke(0, shape);
			eraser.style = { Draw3UInkStrokeKind::Eraser };
			eraser.style.eraserShape = shape;
			eraser.points.clear();
			for (float size : sizes)
				eraser.points.push_back({ static_cast<float>(eraser.points.size()), 0.0f, size });
			page.strokes.push_back(std::move(eraser));
		}
		const auto encoded = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(encoded.canvas && encoded.status == Draw3UInkExportStatus::Success);
		V11_CHECK(encoded.counts.eraserSize == 3);
		const auto decoded = DecodePageWire(EncodeUInkPage(*encoded.canvas, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == 3);
		// 仅验证文件笔尖的浮点往返。极限尺寸不能安装到 Draw3。
		Draw3UInkV11ImportOptions conversionOnly;
		conversionOnly.footprintBudget.reset();
		const auto bounded = ImportUInkV11Page(*decoded);
		V11_REQUIRE(bounded.status == Draw3UInkV11ImportStatus::Success && bounded.page);
		V11_CHECK(bounded.counts.footprintBudget == 3 && bounded.page->strokes.empty());
		const auto imported = ImportUInkV11Page(*decoded, conversionOnly);
		V11_REQUIRE(imported.page && imported.page->strokes.size() == 3);
		V11_CHECK(imported.counts.eraserSize == 0 && imported.counts.invalidInkSkipped == 0);
		for (size_t shape = 0; shape < imported.page->strokes.size(); ++shape)
		{
			const auto& eraser = imported.page->strokes[shape];
			V11_REQUIRE(eraser.points.size() == expected.size());
			V11_CHECK(eraser.style.kind == Draw3UInkStrokeKind::Eraser && eraser.style.eraserShape == shape);
			for (size_t i = 0; i < expected.size(); ++i)
				V11_CHECK(std::abs(double(eraser.points[i].width) - expected[i]) / expected[i] <= 1e-5);
		}

		UInkCanvas foreign;
		foreign.pageGuid = TestPageGuid(1);
		auto eraser = TestInk(0);
		eraser.declaredInkType = 0;
		eraser.nib = UInkNib{ 1, 1.0f };
		eraser.points = { { 0, 0, tiny }, { 1, 0, largest } };
		foreign.content.emplace_back(eraser);
		const auto read = ImportUInkV11Page(foreign, conversionOnly);
		V11_REQUIRE(read.page && read.page->strokes.size() == 1 && read.page->strokes[0].points.size() == 2);
		V11_CHECK(read.counts.eraserSize == 1 && read.counts.invalidInkSkipped == 0);
		V11_CHECK(read.page->strokes[0].points[0].width == minimum &&
			read.page->strokes[0].points[1].width == maximum);

		eraser.points[0].width = 0;
		foreign.content[0] = eraser;
		const auto invalid = ImportUInkV11Page(foreign);
		V11_REQUIRE(invalid.page);
		V11_CHECK(invalid.page->strokes.empty() && invalid.counts.invalidInkSkipped == 1);
	}

	void TestHighlighterWire(V11Test& test)
	{
		auto page = TestPage();
		auto stroke = TestStroke(12);
		stroke.style.kind = Draw3UInkStrokeKind::Highlighter;
		stroke.style.opacity = 0.4f;
		stroke.points = { { 12, 13, 0.125f }, { 15, 18, 17.5f } };
		page.strokes.push_back(stroke);
		const auto exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
		const auto wire = EncodeUInkPage(*exported.canvas, true, true);
		V11_REQUIRE(wire.status == UInkEncodeStatus::Success);
		constexpr std::array<uint8_t, 25> nibBytes{
			0xa3, 'n', 'i', 'b', 0x82, 0xa5, 's', 'h', 'a', 'p', 'e',
			0xd2, 0, 0, 0, 1, 0xa6, 'a', 's', 'p', 'e', 'c', 't', 0xca, 0x3e
		};
		std::array<std::byte, 28> expected{};
		std::transform(nibBytes.begin(), nibBytes.end(), expected.begin(),
			[](uint8_t value) { return static_cast<std::byte>(value); });
		V11_CHECK(std::search(wire.bytes.begin(), wire.bytes.end(), expected.begin(), expected.end()) !=
			wire.bytes.end());
		const auto decoded = DecodePageWire(wire);
		V11_REQUIRE(decoded && decoded->content.size() == 1);
		const auto* ink = std::get_if<UInkInk>(&decoded->content.front());
		V11_REQUIRE(ink && ink->nib && ink->nib->aspect && ink->points.size() == 2);
		V11_CHECK(ink->declaredInkType == 2 && ink->nib->shape == 1 &&
			*ink->nib->aspect == 0.125f && ink->nib->orientation == 0 && !ink->cap);
		V11_CHECK(ink->points[0].width == 0.125f && ink->points[1].width == 17.5f);
		const auto read = ImportUInkV11Page(*decoded);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page);
		V11_REQUIRE(read.page->strokes.size() == 1 && read.page->strokes[0].points.size() == 2);
		V11_CHECK(read.page->strokes[0].style.kind == Draw3UInkStrokeKind::Highlighter);
		V11_CHECK(read.page->strokes[0].style.opacity == 0.4f && read.counts.nib == 0);
		V11_CHECK(read.page->strokes[0].points[0].width == 0.125f &&
			read.page->strokes[0].points[1].width == 17.5f);

		auto foreign = *decoded;
		std::get<UInkInk>(foreign.content.front()).nib.reset();
		const auto rounded = ImportUInkV11Page(foreign);
		V11_REQUIRE(rounded.page && rounded.page->strokes.size() == 1 &&
			rounded.page->strokes[0].points.size() == 2);
		V11_CHECK(rounded.counts.nib == 1 &&
			rounded.page->strokes[0].style.kind == Draw3UInkStrokeKind::Highlighter &&
			rounded.page->strokes[0].points[0].width == 0.125f);
	}

	void TestPageMetadata(V11Test& test)
	{
		const auto fileGuid = TestPageGuid(20);
		const auto desktop = MakeDraw3UInkV11FileHeader(fileGuid);
		V11_REQUIRE(desktop.status == Draw3UInkExportStatus::Success);
		V11_CHECK(desktop.header.guid == fileGuid && desktop.header.deviceNum == 1 &&
			desktop.header.workspaceNum == 1 && !desktop.headerExtension);
		const Draw3UInkV11PageContext board{ TestPageGuid(21), 1 };
		const auto boardHeader = MakeDraw3UInkV11FileHeader(fileGuid, board);
		V11_REQUIRE(boardHeader.status == Draw3UInkExportStatus::Success && boardHeader.headerExtension);
		const auto& extension = *boardHeader.headerExtension;
		V11_REQUIRE(extension.workspaces.size() == 1);
		V11_CHECK(extension.workspaces[0].guid == board.workspaceGuid &&
			extension.workspaces[0].workspaceType == 1 &&
			!extension.workspaces[0].hostId && extension.devices.empty());
		for (int32_t pattern = 0; pattern <= 2; ++pattern)
		{
			auto page = TestPage();
			page.pageIndex = 13;
			page.pageNumber = 27;
			page.background = Draw3UInkBackground{ 0x89abcd, pattern };
			if (pattern != 0) page.viewport = { -52.5f, 13.75f, 1 };
			const auto exported = ExportDraw3PageToUInkV11(page, board);
			V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
			const auto decoded = DecodePageWire(EncodeUInkPage(*exported.canvas, false, true), extension);
			V11_REQUIRE(decoded && decoded->background);
			V11_CHECK(decoded->workspaceGuid == board.workspaceGuid && !decoded->deviceGuid &&
				decoded->pageGuid == page.pageGuid && decoded->pageIndex == 13 && decoded->pageNumber == 27 &&
				decoded->layerIndex == 0 && decoded->layerNumber == 0);
			V11_CHECK(exported.canvas->viewport.has_value() == (pattern != 0));
			V11_CHECK(decoded->background->pattern == pattern &&
				decoded->background->color.fallbackRgb == 0x89abcd && !decoded->background->color.extended);
			const auto read = ImportUInkV11Page(*decoded);
			V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page && read.page->background);
			V11_CHECK(read.page->background->rgb == 0x89abcd && read.page->background->pattern == pattern);
			V11_CHECK(read.page->viewport.x == page.viewport.x && read.page->viewport.y == page.viewport.y &&
				read.page->viewport.scale == 1.0f);
		}

		const Draw3UInkV11PageContext ppt{ TestPageGuid(22), 2 };
		const auto hostId = FormatUInkGuid(TestPageGuid(23));
		const auto pptHeader = MakeDraw3UInkV11FileHeader(fileGuid, ppt, hostId, 2);
		V11_REQUIRE(pptHeader.status == Draw3UInkExportStatus::Success && pptHeader.headerExtension &&
			pptHeader.headerExtension->workspaces.size() == 1);
		const auto& workspace = pptHeader.headerExtension->workspaces.front();
		V11_CHECK(workspace.workspaceType == 2 && workspace.hostId == hostId &&
			workspace.currentPageIndex == 2);
		V11_CHECK(MakeDraw3UInkV11FileHeader(fileGuid, ppt).status != Draw3UInkExportStatus::Success);
		for (int binding = 0; binding <= 1; ++binding)
		{
			for (bool end : { false, true })
			{
				auto page = TestPage();
				page.binding = binding;
				page.endScreen = end;
				page.retained = !end && binding == 0;
				page.slideId = 172;
				const auto exported = ExportDraw3PageToUInkV11(page, ppt);
				V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
				const auto decoded = DecodePageWire(EncodeUInkPage(*exported.canvas, false, true),
					pptHeader.headerExtension);
				V11_REQUIRE(decoded);
				V11_CHECK(!decoded->background && !exported.canvas->viewport);
				V11_CHECK(decoded->binding == (binding == 1 ? std::optional<int32_t>{ 1 } : std::nullopt));
				V11_CHECK(decoded->slideId == (!end && binding == 0 ? std::optional<int32_t>{ 172 } : std::nullopt));
				V11_CHECK(decoded->pageState == (page.retained ? std::optional<int32_t>{ 1 } : std::nullopt));
				V11_CHECK(decoded->pageKind == (end ? std::optional<int32_t>{ 1 } : std::nullopt));
				const auto read = ImportUInkV11Page(*decoded);
				V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page);
				V11_CHECK(read.page->binding.value_or(0) == binding && read.page->endScreen == end &&
					read.page->retained == page.retained && read.page->slideId == decoded->slideId);
			}
		}
	}

	void TestPageLog(V11Test& test)
	{
		auto page = TestPage();
		page.strokes = { TestStroke(4, 7), TestStroke(6, 7), TestStroke(9, 18) };
		auto exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
		auto decoded = DecodePageWire(EncodeUInkPage(*exported.canvas, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == 3);
		auto read = ImportUInkV11Page(*decoded);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page &&
			read.page->strokes.size() == 3 && read.page->operations.size() == 3);
		V11_CHECK(read.page->intervalOrdinal == 0 && read.page->strokes[0].undoId == 0 &&
			read.page->strokes[1].undoId == 0 && read.page->strokes[2].undoId == 1);

		// 旧 canonical 页的区间编号仍以完整操作日志为基准。
		page.operations = { TestStroke(1, 2), Draw3UInkClearSnapshot{ 2 },
			TestStroke(2, 6), TestStroke(3, 6), Draw3UInkClearSnapshot{ 6 },
			TestStroke(4, 6), TestStroke(5, 6), TestStroke(6, 7) };
		page.intervalOrdinal = 2;
		exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
		V11_CHECK(exported.diagnostics.size() == 1);
		decoded = DecodePageWire(EncodeUInkPage(*exported.canvas, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == 6);
		const std::array expectedUndo{ 0u, 0u, 1u, 2u, 2u, 3u };
		for (size_t index = 0; index < expectedUndo.size(); ++index)
		{
			std::visit([&](const auto& content)
				{
					V11_CHECK(content.contentId == index && content.undoId == expectedUndo[index]);
				}, decoded->content[index]);
		}
		V11_REQUIRE(std::holds_alternative<UInkClear>(decoded->content[2]));
		read = ImportUInkV11Page(*decoded);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page &&
			read.page->operations.size() == 6 && read.page->strokes.size() == 3);
		V11_CHECK(read.page->intervalOrdinal == 1 && read.counts.multipleClears == 0);
		const auto* prior = std::get_if<Draw3UInkStrokeSnapshot>(&read.page->operations[0]);
		const auto* priorSecond = std::get_if<Draw3UInkStrokeSnapshot>(&read.page->operations[1]);
		const auto* clear = std::get_if<Draw3UInkClearSnapshot>(&read.page->operations[2]);
		V11_REQUIRE(prior && priorSecond && clear && prior->points.size() == 1);
		V11_CHECK(prior->points[0].x == 2 && prior->undoId == priorSecond->undoId &&
			clear->undoId == 1 && read.page->strokes[0].undoId == 2 &&
			read.page->strokes[1].undoId == 2 && read.page->strokes[2].undoId == 3);

		page.intervalOrdinal = 1;
		exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas &&
			exported.canvas->content.size() == 4);
		V11_CHECK(std::holds_alternative<UInkClear>(exported.canvas->content[1]));
		page.operations.resize(2);
		exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas &&
			exported.canvas->content.size() == 2);
		read = ImportUInkV11Page(*exported.canvas);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page);
		V11_CHECK(read.page->intervalOrdinal == 1 && read.page->strokes.empty() &&
			read.page->operations.size() == 2);
	}

	void TestUndoNumberingAfterSkippedShapes(V11Test& test)
	{
		using namespace draw3::shapes;
		Construction construction;
		construction.tool.preset = PresetKind::Line;
		const float x = static_cast<float>(kUInkMaxCoordinate) + 1024;
		construction.start = { x, 20 };
		construction.end = { x + 30, 35 };
		Draw3UInkStrokeSnapshot firstSkipped;
		firstSkipped.style.kind = Draw3UInkStrokeKind::Shape;
		firstSkipped.undoId = 4;
		firstSkipped.shape = MakeConstructedShape(construction, ShapeStyle{}, TestPageGuid(2).Bytes());
		V11_REQUIRE(firstSkipped.shape);
		auto middleSkipped = firstSkipped;
		middleSkipped.undoId = 8;
		middleSkipped.shape->id = TestPageGuid(3).Bytes();

		auto page = TestPage();
		// 同一源编号分别出现在空组和 Clear 两侧，存活笔迹的撤回边界依旧独立。
		page.operations = { firstSkipped, TestStroke(1, 7), TestStroke(2, 7),
			middleSkipped, TestStroke(3, 7), Draw3UInkClearSnapshot{ 7 }, TestStroke(4, 7) };
		page.intervalOrdinal = 1;
		const auto exported = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas);
		V11_CHECK(exported.counts.invalidShapeSkipped == 2);

		const auto decoded = DecodePageWire(EncodeUInkPage(*exported.canvas, true, true));
		const std::array expectedUndo{ 0u, 0u, 1u, 2u, 3u };
		V11_REQUIRE(decoded && decoded->content.size() == expectedUndo.size());
		for (size_t index = 0; index < expectedUndo.size(); ++index)
		{
			std::visit([&](const auto& value)
				{ V11_CHECK(value.contentId == index && value.undoId == expectedUndo[index]); },
				decoded->content[index]);
		}
		V11_CHECK(std::holds_alternative<UInkClear>(decoded->content[3]));
	}

	void TestForeignClearsAndUndoGaps(V11Test& test)
	{
		UInkCanvas foreign;
		foreign.pageGuid = TestPageGuid(2);
		foreign.content = { TestInk(10, 0, 0), UInkClear{ 1, 2 }, TestInk(20, 2, 10),
			TestInk(21, 3, 10), UInkClear{ 4, 11 }, TestInk(30, 5, 17), TestInk(31, 6, 28) };
		const auto decoded = DecodePageWire(EncodeUInkPage(foreign, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == 7);
		const auto read = ImportUInkV11Page(*decoded);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page &&
			read.page->operations.size() == 5 && read.page->strokes.size() == 2);
		V11_CHECK(read.counts.multipleClears == 1 && read.page->intervalOrdinal == 1);
		const auto* first = std::get_if<Draw3UInkStrokeSnapshot>(&read.page->operations[0]);
		const auto* second = std::get_if<Draw3UInkStrokeSnapshot>(&read.page->operations[1]);
		const auto* clear = std::get_if<Draw3UInkClearSnapshot>(&read.page->operations[2]);
		V11_REQUIRE(first && second && clear && first->points.size() == 1 && second->points.size() == 1 &&
			read.page->strokes[0].points.size() == 1 && read.page->strokes[1].points.size() == 1);
		V11_CHECK(first->points[0].x == 20 && second->points[0].x == 21 &&
			first->undoId == second->undoId && clear->undoId == 1);
		V11_CHECK(read.page->strokes[0].points[0].x == 30 && read.page->strokes[0].undoId == 2 &&
			read.page->strokes[1].points[0].x == 31 && read.page->strokes[1].undoId == 3);
	}

	void TestForeignInkAndMetadataFallbacks(V11Test& test)
	{
		UInkCanvas foreign;
		foreign.pageGuid = TestPageGuid(2);
		foreign.viewport = UInkViewport{ 17, -12, 2 };
		foreign.background = UInkBackground{ { 0xaabbcc, UInkExtendedColor{} }, 17 };
		foreign.pageState = 15;
		foreign.pageKind = -9;
		foreign.binding = 128;
		auto pen = TestInk(1, 0, 0);
		pen.declaredInkType = 128;
		pen.color = { 0x123456, UInkExtendedColor{} };
		pen.opacity = 1.2f;
		pen.declaredTexture = 128;
		pen.renderOnlyWhenLatest = true;
		pen.nib = UInkNib{ 1, 0.25f, 1 };
		auto cap = TestInk(2, 1, 0);
		cap.cap = 2;
		auto highlighter = TestInk(3, 2, 7);
		highlighter.declaredInkType = 3;
		highlighter.nib = UInkNib{ 1, 0.875f, 1 };
		highlighter.opacity = 0.3f;
		highlighter.points.front().style = UInkPointStyle{ { 0xffaa00 }, 0.75f };
		auto eraser = TestInk(4, 3, 17);
		eraser.declaredInkType = 0;
		eraser.nib = UInkNib{ 1, 0.91f, 1 };
		auto badInk = TestInk(6, 5, 20);
		badInk.points[0].x = 1e8f;
		foreign.content = { pen, cap, highlighter, eraser, UInkMedia{}, badInk };
		const auto read = ImportUInkV11Page(foreign);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.page &&
			read.page->strokes.size() == 4 && read.page->background);
		const auto& counts = read.counts;
		V11_CHECK(counts.extendedColor == 2 && counts.opacityClamped == 0 && counts.nib == 3 &&
			counts.cap == 1 && counts.advancedHighlighter == 1 && counts.latestVisibility == 1 &&
			counts.viewport == 1 && counts.mediaSkipped == 1 && counts.invalidInkSkipped == 1);
		const auto& page = *read.page;
		V11_CHECK(!page.retained && !page.endScreen && page.binding == 0);
		V11_CHECK(page.viewport.x == 0 && page.viewport.y == 0 && page.viewport.scale == 1);
		V11_CHECK(page.background->rgb == 0xaabbcc && page.background->pattern == 0);
		const auto& strokes = page.strokes;
		V11_REQUIRE(strokes[0].points.size() == 1 && strokes[2].points.size() == 1 && strokes[3].points.size() == 1);
		V11_CHECK(strokes[0].style.kind == Draw3UInkStrokeKind::Pen && strokes[0].style.fallbackRgb == 0x123456 &&
			strokes[0].style.opacity == 1.0f && strokes[0].style.texture == 0 &&
			strokes[0].points[0].width == 2.25f && !strokes[0].renderOnlyWhenLatest);
		V11_CHECK(strokes[0].undoId == strokes[1].undoId && strokes[2].undoId == 1);
		V11_CHECK(strokes[2].style.kind == Draw3UInkStrokeKind::Highlighter && strokes[2].style.opacity == 0.3f &&
			strokes[2].style.fallbackRgb == 0 && strokes[2].points[0].width == 2.25f);
		const double expected = 4.5 * std::sqrt(0.91 / std::numbers::pi);
		V11_CHECK(strokes[3].style.eraserShape == 2 && std::abs(strokes[3].points[0].width - expected) < 1e-5);
	}

	void TestFootprintBudgetSkipsOnlyOversizedItems(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		auto wideDot = TestInk(0, 0, 0);
		wideDot.points.front().width = 10'000'000;
		canvas.content.emplace_back(wideDot);
		canvas.content.emplace_back(TestInk(10, 1, 1));
		canvas.content.emplace_back(UInkClear{ 2, 2 });
		auto longLine = TestInk(0, 3, 3);
		longLine.points = { { -1'000'000, -1'000'000, 32'000 },
			{ 1'000'000, 1'000'000, 32'000 } };
		canvas.content.emplace_back(longLine);
		UInkShape filled;
		filled.contentId = filled.undoId = 4;
		filled.declaredShapeType = 2;
		filled.geometry = UInkRectangleGeometry{ 0, 0, 1'000'000, 1'000'000 };
		filled.fill.emplace();
		canvas.content.emplace_back(std::move(filled));
		canvas.content.emplace_back(TestInk(20, 5, 5));
		const auto decoded = DecodePageWire(EncodeUInkPage(canvas, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == 6);

		const auto result = ImportUInkV11Page(*decoded);
		V11_REQUIRE(result.status == Draw3UInkV11ImportStatus::Success && result.page);
		V11_CHECK(result.counts.footprintBudget == 3 && result.counts.invalidInkSkipped == 0 &&
			result.counts.invalidShapeSkipped == 0);
		const auto& page = *result.page;
		V11_REQUIRE(page.strokes.size() == 1 && page.operations.size() == 3);
		V11_CHECK(page.intervalOrdinal == 1 && page.strokes.front().points.front().x == 20);
		const auto* prefix = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations.front());
		V11_REQUIRE(prefix && !prefix->points.empty());
		V11_CHECK(prefix->points.front().x == 10 && prefix->undoId == 0);
		const auto* clear = std::get_if<Draw3UInkClearSnapshot>(&page.operations[1]);
		V11_REQUIRE(clear);
		V11_CHECK(clear->undoId == 1 && page.strokes.front().undoId == 2);
	}

	void TestFootprintItemTileLimit(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		UInkShape filled;
		filled.declaredShapeType = 2;
		filled.geometry = UInkRectangleGeometry{ 0, 0, 500'000, 500'000 };
		filled.fill.emplace();
		canvas.content.emplace_back(filled);
		const auto large = DecodePageWire(EncodeUInkPage(canvas, true, true));
		V11_REQUIRE(large && large->content.size() == 1);

		const auto rejected = ImportUInkV11Page(*large);
		V11_REQUIRE(rejected.status == Draw3UInkV11ImportStatus::Success && rejected.page);
		V11_CHECK(rejected.counts.footprintBudget == 1 && rejected.page->strokes.empty());

		// 先放宽页级瓦片上限，单独确认单项面积上限仍会挡住大图形。
		Draw3UInkV11ImportOptions options;
		options.footprintPageMaxTiles = uint64_t{ 1 } << 24;
		const auto itemLimited = ImportUInkV11Page(*large, options);
		V11_REQUIRE(itemLimited.status == Draw3UInkV11ImportStatus::Success && itemLimited.page);
		V11_CHECK(itemLimited.counts.footprintBudget == 1 && itemLimited.page->strokes.empty());

		// 放宽单项面积上限后，大图形仍可通过页级和单项访问次数预算。
		options.footprintItemMaxTiles = uint64_t{ 1 } << 24;
		const auto relaxed = ImportUInkV11Page(*large, options);
		V11_REQUIRE(relaxed.status == Draw3UInkV11ImportStatus::Success && relaxed.page);
		V11_CHECK(relaxed.counts.footprintBudget == 0 && relaxed.page->strokes.size() == 1);

		std::get<UInkShape>(canvas.content.front()).geometry = UInkRectangleGeometry{ 0, 0, 60'000, 60'000 };
		const auto smaller = DecodePageWire(EncodeUInkPage(canvas, true, true));
		V11_REQUIRE(smaller && smaller->content.size() == 1);
		const auto accepted = ImportUInkV11Page(*smaller);
		V11_REQUIRE(accepted.status == Draw3UInkV11ImportStatus::Success && accepted.page);
		V11_CHECK(accepted.counts.footprintBudget == 0 && accepted.page->strokes.size() == 1);
	}

	void TestFootprintPageTileLimit(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		constexpr uint32_t fillCount = 610;
		constexpr float spacing = 60'416.0f; // 256 px 对齐，矩形之间互不重叠。
		for (uint32_t i = 0; i < fillCount; ++i)
		{
			UInkShape filled;
			filled.contentId = filled.undoId = i;
			filled.declaredShapeType = 2;
			filled.geometry = UInkRectangleGeometry{ 64 + (i % 25) * spacing, 64 + (i / 25) * spacing, 60'000, 60'000 };
			filled.fill.emplace();
			canvas.content.emplace_back(std::move(filled));
		}
		const auto decoded = DecodePageWire(EncodeUInkPage(canvas, true, true));
		V11_REQUIRE(decoded && decoded->content.size() == fillCount);
		const auto imported = ImportUInkV11Page(*decoded);
		V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);

		// 中心在瓦片中央，计入抗锯齿边距后每轴覆盖 469 块。
		constexpr uint64_t tilesPerFill = 469 * 469;
		constexpr uint64_t maxPageTiles = uint64_t{1} << 23;
		constexpr uint64_t acceptedCount = maxPageTiles / tilesPerFill;
		const auto& strokes = imported.page->strokes;
		V11_REQUIRE(strokes.size() == acceptedCount && imported.page->operations.size() == acceptedCount);
		V11_CHECK(strokes.size() * tilesPerFill <= maxPageTiles &&
			imported.counts.footprintBudget == fillCount - acceptedCount &&
			imported.counts.invalidShapeSkipped == 0);
		for (size_t i = 0; i < strokes.size(); ++i)
		{
			V11_REQUIRE(strokes[i].shape);
			const auto& transform = strokes[i].shape->transform;
			V11_CHECK(transform.dx == 64 + (i % 25) * spacing && transform.dy == 64 + (i / 25) * spacing);
		}
	}

	void TestExactFootprintPageTilesAcrossClear(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		canvas.content = { TestInk(0, 0, 0), UInkClear{ 1, 1 }, TestInk(128, 2, 2) };
		Draw3UInkV11ImportOptions options;
		// 两笔各跨四块，Clear 前缀保留并共用八块的页额度。
		options.footprintPageMaxTiles = 8;
		const auto exact = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(exact.status == Draw3UInkV11ImportStatus::Success && exact.page);
		V11_CHECK(exact.counts.footprintBudget == 0 && exact.page->operations.size() == 3 &&
			exact.page->strokes.size() == 1);

		options.footprintPageMaxTiles = 7;
		canvas.content.emplace_back(TestInk(64, 3, 3));
		const auto over = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(over.status == Draw3UInkV11ImportStatus::Success && over.page);
		V11_CHECK(over.counts.footprintBudget == 1 && over.page->operations.size() == 3);
		V11_REQUIRE(over.page->strokes.size() == 1 && !over.page->strokes.front().points.empty());
		V11_CHECK(over.page->strokes.front().points.front().x == 64);

		// 同一笔在 64 px 瓦片的交界处，四块无法装进剩余的三块。
		options.footprintTileSize = 64;
		const auto fine = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(fine.status == Draw3UInkV11ImportStatus::Success && fine.page);
		V11_CHECK(fine.counts.footprintBudget == 2 && fine.page->operations.size() == 2 &&
			fine.page->strokes.empty());

		// 第一笔的包围盒可容纳，但超过单项访问预算，后续一块瓦片仍可接受。
		canvas.content = { TestInk(0), TestInk(64, 1, 1) };
		options.footprintTileSize = 128;
		options.footprintPageMaxTiles = 4;
		options.footprintItemBudget = 7;
		const auto visitLimited = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(visitLimited.status == Draw3UInkV11ImportStatus::Success && visitLimited.page);
		V11_CHECK(visitLimited.counts.footprintBudget == 1 && visitLimited.page->operations.size() == 1);
		V11_REQUIRE(visitLimited.page->strokes.size() == 1 && !visitLimited.page->strokes.front().points.empty());
		V11_CHECK(visitLimited.page->strokes.front().points.front().x == 64);
	}

	void TestExactFootprintBudgetAcrossClear(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		canvas.content = { TestInk(0, 0, 0), UInkClear{ 1, 1 }, TestInk(0, 2, 2) };
		Draw3UInkV11ImportOptions options;
		// 原点位于四块瓦片交界。两层各访问四块，每笔计八块次。
		options.footprintBudget = 16;
		const auto exact = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(exact.status == Draw3UInkV11ImportStatus::Success && exact.page);
		V11_CHECK(exact.counts.footprintBudget == 0 && exact.page->operations.size() == 3 &&
			exact.page->strokes.size() == 1);

		options.footprintBudget = 15;
		const auto over = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(over.status == Draw3UInkV11ImportStatus::Success && over.page);
		V11_CHECK(over.counts.footprintBudget == 1 && over.page->strokes.empty());
		V11_REQUIRE(over.page->operations.size() == 2);
		V11_CHECK(std::holds_alternative<Draw3UInkStrokeSnapshot>(over.page->operations.front()) &&
			std::holds_alternative<Draw3UInkClearSnapshot>(over.page->operations.back()));

		canvas.content = { TestInk(64) };
		options.footprintBudget = 2;
		const auto coarse = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(coarse.page);
		V11_CHECK(coarse.counts.footprintBudget == 0 && coarse.page->strokes.size() == 1);
		options.footprintTileSize = 64;
		const auto fine = ImportUInkV11Page(canvas, options);
		V11_REQUIRE(fine.page);
		V11_CHECK(fine.counts.footprintBudget == 1 && fine.page->strokes.empty());

		// 转角等时推进可能跨过终点，足迹构建会额外扫描一次全笔范围。
		auto corner = TestInk(0);
		corner.points = { {127, 129, 2.25f}, {128, 128, 2.25f}, {160, 128, 2.25f} };
		canvas.content = { std::move(corner) };
		options.footprintTileSize = 128;
		for (uint64_t budget : { 47, 48 })
		{
			options.footprintBudget = budget;
			const auto cornerRead = ImportUInkV11Page(canvas, options);
			V11_REQUIRE(cornerRead.status == Draw3UInkV11ImportStatus::Success && cornerRead.page);
			V11_CHECK(cornerRead.counts.footprintBudget == (budget == 47 ? 1 : 0) &&
				cornerRead.page->strokes.size() == (budget == 47 ? 0 : 1));
		}
	}

	void TestDraw3ViewportReadFallback(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		canvas.content.emplace_back(TestInk(10));
		const std::array viewports{ UInkViewport{ 1048576, -1048576, 1 },
			UInkViewport{ 1048577, 40, 1 }, UInkViewport{ 30, -1048577, 1 },
			UInkViewport{ 30, 40, 0.5f } };
		for (size_t i = 0; i < viewports.size(); ++i)
		{
			canvas.viewport = viewports[i];
			const auto decoded = DecodePageWire(EncodeUInkPage(canvas, true, true));
			V11_REQUIRE(decoded && decoded->viewport && decoded->content.size() == 1);
			const auto imported = ImportUInkV11Page(*decoded);
			V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success &&
				imported.page && imported.page->strokes.size() == 1 &&
				!imported.page->strokes.front().points.empty());
			const UInkViewport expected = i == 0 ? viewports[i] : UInkViewport{};
			const auto& actual = imported.page->viewport;
			V11_CHECK(imported.counts.viewport == (i == 0 ? 0 : 1) &&
				actual.x == expected.x && actual.y == expected.y && actual.scale == expected.scale &&
				imported.page->strokes.front().points.front().x == 10);
		}
	}

	void TestFileFallbackSummary(V11Test& test)
	{
		UInkCanvas canvas;
		canvas.pageGuid = TestPageGuid(1);
		auto pen = TestInk(1);
		pen.declaredInkType = 128;
		pen.declaredTexture = 128;
		pen.nib = UInkNib{ 1, 0.125f };
		canvas.content.emplace_back(pen);
		auto wire = EncodeUInkPage(canvas, true, true);
		V11_REQUIRE(wire.status == UInkEncodeStatus::Success);
		// 写出器拒绝非法 aspect 与透明度，直接修改两个 float32 负载模拟外来文件。
		const std::array<uint8_t, 8> aspect{ 0xa6, 'a', 's', 'p', 'e', 'c', 't', 0xca };
		const std::array<uint8_t, 9> opacity{ 0xa7, 'o', 'p', 'a', 'c', 'i', 't', 'y', 0xca };
		const auto matches = [](std::byte wireByte, uint8_t keyByte)
			{ return std::to_integer<uint8_t>(wireByte) == keyByte; };
		const auto a = std::search(wire.bytes.begin(), wire.bytes.end(), aspect.begin(), aspect.end(), matches);
		const auto o = std::search(wire.bytes.begin(), wire.bytes.end(), opacity.begin(), opacity.end(), matches);
		V11_REQUIRE(static_cast<size_t>(wire.bytes.end() - a) >= aspect.size() + 4 &&
			static_cast<size_t>(wire.bytes.end() - o) >= opacity.size() + 4);
		std::fill_n(a + aspect.size(), 4, std::byte{ 0 });
		const std::array<std::byte, 4> two{ std::byte{ 0x40 } };
		std::copy(two.begin(), two.end(), o + opacity.size());

		UInkReadLimits limits;
		limits.maxDiagnostics = 1;
		UInkStreamContext stream;
		stream.usesImplicitWorkspace = true;
		auto file = DecodeUInkStream(wire.bytes, limits, &stream);
		V11_REQUIRE(file.document && file.diagnostics.size() == 1);
		V11_CHECK(file.diagnostics[0].code == UInkDiagnosticCode::DiagnosticsTruncated);
		const auto fallback = static_cast<size_t>(UInkDiagnosticCode::FieldFallback);
		V11_CHECK(file.summary.diagnosticCounts[fallback].count == 4);
		const auto imported = ImportDraw3UInkV11File(file);
		V11_REQUIRE(imported.pages.size() == 1 && imported.pages[0].page &&
			imported.pages[0].page->strokes.size() == 1);
		V11_CHECK(imported.summary.diagnosticCounts[fallback].count == 4 &&
			imported.summary.diagnosticCounts[fallback].firstByteOffset ==
				file.summary.diagnosticCounts[fallback].firstByteOffset &&
			imported.summary.outcome == file.summary.outcome && imported.summary.version == file.summary.version &&
			imported.summary.decodeMicroseconds == file.summary.decodeMicroseconds &&
			imported.diagnostics.size() == 1);
		V11_CHECK(imported.pages[0].counts.nib == 0 && imported.pages[0].counts.opacityClamped == 0 &&
			imported.pages[0].page->strokes[0].style.kind == Draw3UInkStrokeKind::Pen &&
			imported.pages[0].page->strokes[0].style.opacity == 1.0f);
	}

	UInkShape TestLegacyLinePart(const UInkGuid& objectId, float x = 0,
		uint32_t contentId = 0, uint32_t undoId = 0)
	{
		UInkShape line;
		line.contentId = contentId;
		line.undoId = undoId;
		line.geometry = UInkLineGeometry{ { { x, 0 }, { x + 5, 0 } } };
		line.stroke.emplace();
		line.object = UInkShapeObject{ objectId, 1, "legacy-solid-line", 1 };
		return line;
	}

	void TestForeignFileComposition(V11Test& test)
	{
		UInkReadResult file;
		file.status = UInkReadStatus::Complete;
		auto& document = file.document.emplace();
		const auto rootWorkspace = TestPageGuid(51);
		const auto childWorkspace = TestPageGuid(52);
		const auto firstDevice = TestPageGuid(53);
		const auto secondDevice = TestPageGuid(54);
		auto& extension = document.headerExtension.emplace();
		UInkWorkspace root;
		root.guid = rootWorkspace;
		UInkWorkspace child;
		child.guid = childWorkspace;
		child.parentWorkspaceGuid = rootWorkspace;
		extension.workspaces = { root, child };
		UInkDevice first;
		first.guid = firstDevice;
		UInkDevice second;
		second.guid = secondDevice;
		extension.devices = { first, second };
		auto makeLayer = [](const UInkGuid& workspace, const UInkGuid& device, uint8_t id,
			uint32_t pageIndex, uint32_t layerIndex, float x)
		{
			UInkCanvas layer;
			layer.workspaceGuid = workspace;
			layer.deviceGuid = device;
			layer.pageGuid = TestPageGuid(id);
			layer.pageIndex = pageIndex;
			layer.pageNumber = pageIndex + 1;
			layer.layerIndex = layerIndex;
			layer.content.emplace_back(TestInk(x));
			return layer;
		};
		auto base = makeLayer(rootWorkspace, firstDevice, 55, 1, 0, 10);
		base.viewport = UInkViewport{ 13, -7, 1 };
		auto upper = makeLayer(rootWorkspace, firstDevice, 55, 1, 2, 15);
		upper.content.emplace_back(UInkClear{ 1, 1 });
		upper.content.emplace_back(TestInk(20, 2, 2));
		UInkShape line;
		line.geometry = UInkLineGeometry{ { { 0, 0 }, { 5, 0 } } };
		line.stroke.emplace();
		UInkShapeObject object;
		object.id = TestPageGuid(58);
		object.revision = 1;
		object.partCount = 1;
		object.preset = "legacy-solid-line";
		line.object = object;
		line.contentId = 1;
		line.undoId = 1;
		base.content.emplace_back(line);
		line.contentId = 3;
		line.undoId = 3;
		upper.content.emplace_back(line);
		document.canvases = {
			makeLayer(childWorkspace, firstDevice, 56, 0, 0, 99),
			makeLayer(rootWorkspace, secondDevice, 55, 1, 0, 80),
			upper, base,
			makeLayer(rootWorkspace, secondDevice, 57, 0, 0, 30),
			makeLayer(rootWorkspace, firstDevice, 64, 2, 0, 64),
			makeLayer(rootWorkspace, firstDevice, 65, 2, 0, 65),
			makeLayer(rootWorkspace, firstDevice, 64, 2, 1, 66),
		};
		const auto read = ImportDraw3UInkV11File(file);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.pages.size() == 5);
		for (const auto& page : read.pages)
		{
			V11_REQUIRE(page.status == Draw3UInkV11ImportStatus::Success && page.page);
			V11_CHECK(page.counts.multiDevice == 1);
		}
		const auto& before = *read.pages[0].page;
		const auto& merged = *read.pages[1].page;
		const auto& subpage = *read.pages[4].page;
		V11_REQUIRE(before.strokes.size() == 1 && before.strokes[0].points.size() == 1);
		V11_CHECK(before.pageIndex == 0 && before.deviceGuid == secondDevice && before.strokes[0].points[0].x == 30);
		V11_REQUIRE(merged.strokes.size() == 4 && merged.operations.size() == 4 &&
			merged.strokes[0].points.size() == 1 && merged.strokes[2].points.size() == 1 &&
			merged.strokes[1].shape && merged.strokes[3].shape);
		V11_CHECK(merged.pageIndex == 1 && merged.deviceGuid == firstDevice && merged.intervalOrdinal == 0 &&
			merged.viewport.x == 13 && merged.viewport.y == -7 && read.pages[1].counts.multiLayer == 1);
		V11_CHECK(merged.strokes[0].points[0].x == 10 && merged.strokes[0].undoId == 0 &&
			merged.strokes[2].points[0].x == 20 && merged.strokes[2].undoId == 2);
		V11_CHECK(merged.strokes[1].shape->id != object.id.Bytes() &&
			merged.strokes[3].shape->id != object.id.Bytes() &&
			merged.strokes[1].shape->id != merged.strokes[3].shape->id &&
			merged.strokes[1].shape->legacyKind == draw3::shapes::LegacyKind::None &&
			merged.strokes[3].shape->legacyKind == draw3::shapes::LegacyKind::None &&
			read.pages[1].counts.incompleteShapeObject == 2);
		V11_CHECK(read.pages[2].page->pageGuid == TestPageGuid(65) &&
			read.pages[3].page->pageGuid == TestPageGuid(64));
		V11_REQUIRE(subpage.strokes.size() == 1 && subpage.strokes[0].points.size() == 1);
		V11_CHECK(read.pages[4].workspaceGuid == childWorkspace && read.pages[4].counts.nestedWorkspace == 1 &&
			subpage.pageIndex == 0 && subpage.strokes[0].points[0].x == 99);
	}

	void TestUpperLayerEraser(V11Test& test)
	{
		UInkCanvas bottom;
		bottom.pageGuid = TestPageGuid(1);
		bottom.content = { TestInk(6), UInkClear{ 1, 1 }, TestInk(7, 2, 2) };
		UInkCanvas upper;
		upper.pageGuid = bottom.pageGuid;
		upper.layerIndex = 1;
		auto eraser = TestInk(7);
		eraser.declaredInkType = 0;
		upper.content = { eraser };
		UInkReadResult file;
		file.status = UInkReadStatus::Complete;
		file.document.emplace().canvases = { upper, bottom };
		const auto read = ImportDraw3UInkV11File(file);
		V11_REQUIRE(read.pages.size() == 1 && read.pages[0].page);
		const auto& page = *read.pages[0].page;
		V11_CHECK(read.pages[0].counts.upperLayerEraseSkipped == 1);
		V11_REQUIRE(page.strokes.size() == 1 && page.strokes[0].points.size() == 1);
		V11_CHECK(page.strokes[0].style.kind == Draw3UInkStrokeKind::Pen &&
			page.strokes[0].points[0].x == 7 && page.strokes[0].points[0].y == -7);
		V11_REQUIRE(page.intervalOrdinal == 1 && page.operations.size() == 3 &&
			std::holds_alternative<Draw3UInkClearSnapshot>(page.operations[1]));
		const auto* before = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[0]);
		V11_REQUIRE(before && before->points.size() == 1);
		V11_CHECK(before->points[0].x == 6);

		const auto direct = ImportUInkV11Page(upper);
		V11_REQUIRE(direct.page && direct.page->strokes.size() == 1);
		V11_CHECK(direct.page->strokes[0].style.kind == Draw3UInkStrokeKind::Eraser);
	}

	void TestCrossPageObjectIds(V11Test& test)
	{
		const auto id = TestPageGuid(22);
		UInkCanvas first;
		first.pageGuid = TestPageGuid(1);
		auto root = TestLegacyLinePart(id);
		std::get<UInkShapeObject>(*root.object).partCount = 2;
		auto ref = TestLegacyLinePart(id, 6, 1);
		ref.object = UInkShapeObjectRef{ id, 1 };
		first.content = { root, ref };
		UInkCanvas second = first;
		second.pageGuid = TestPageGuid(2);
		second.pageIndex = 1;
		UInkReadResult file;
		file.status = UInkReadStatus::Complete;
		file.document.emplace().canvases = { first, second };
		const auto read = ImportDraw3UInkV11File(file);
		V11_REQUIRE(read.pages.size() == 2);
		for (const auto& result : read.pages)
		{
			V11_REQUIRE(result.page && result.page->strokes.size() == 2);
			V11_CHECK(result.counts.incompleteShapeObject == 1);
			for (const auto& stroke : result.page->strokes)
			{
				V11_REQUIRE(stroke.shape);
				V11_CHECK(stroke.shape->kind == draw3::shapes::Kind::Line && stroke.shape->id != id.Bytes());
			}
		}
		const auto direct = ImportUInkV11Page(first);
		V11_REQUIRE(direct.page && direct.page->strokes.size() == 1 && direct.page->strokes[0].shape);
		V11_CHECK(direct.counts.incompleteShapeObject == 0 &&
			direct.page->strokes[0].shape->kind == draw3::shapes::Kind::Compound);
	}

	void TestSplicePreviousIntervalRoundTrip(V11Test& test)
	{
		const Draw3UInkV11PageContext context{ TestPageGuid(70), 1 };
		const auto header = MakeDraw3UInkV11FileHeader(TestPageGuid(71), context);
		V11_REQUIRE(header.status == Draw3UInkExportStatus::Success && header.headerExtension);

		auto oldPage = TestPage(72);
		oldPage.background = Draw3UInkBackground{ 0x112233, 1 };
		oldPage.viewport = { 1.0f, 2.0f, 1.0f };
		const auto oldMetadata = ExportDraw3PageToUInkV11(oldPage, context);
		V11_REQUIRE(oldMetadata.status == Draw3UInkExportStatus::Success && oldMetadata.canvas);
		auto oldVersion = *oldMetadata.canvas;
		const auto shapeId = TestPageGuid(73);
		auto shapeRoot = TestLegacyLinePart(shapeId, 0, 2, 4);
		V11_REQUIRE(shapeRoot.object && std::holds_alternative<UInkShapeObject>(*shapeRoot.object));
		auto& object = std::get<UInkShapeObject>(*shapeRoot.object);
		object.preset.reset();
		object.partCount = 2;
		auto shapeRef = TestLegacyLinePart(shapeId, 6, 3, 4);
		shapeRef.object = UInkShapeObjectRef{ shapeId, 1 };
		oldVersion.content = { TestInk(11, 0, 0), TestInk(12, 1, 0), shapeRoot, shapeRef,
			UInkClear{ 4, 9 }, TestInk(99, 5, 16) };
		const auto disk = DecodePageWire(EncodeUInkPage(oldVersion, false, true), header.headerExtension);
		V11_REQUIRE(disk && disk->content.size() == 6);

		auto currentPage = TestPage(72);
		currentPage.pageIndex = 7;
		currentPage.pageNumber = 8;
		currentPage.background = Draw3UInkBackground{ 0xabcdef, 2 };
		currentPage.viewport = { -32.0f, 17.5f, 1.0f };
		currentPage.strokes = { TestStroke(21, 5), TestStroke(22, 5), TestStroke(23, 8) };
		const auto current = ExportDraw3PageToUInkV11(currentPage, context);
		V11_REQUIRE(current.status == Draw3UInkExportStatus::Success && current.canvas);
		Draw3UInkV11SpliceDiagnostics diagnostics;
		const auto spliced = SpliceUInkV11PreviousInterval(*disk, *current.canvas, diagnostics);
		V11_REQUIRE(spliced && diagnostics.code == UInkDiagnosticCode::None && diagnostics.diagnostics.empty());
		const auto wire = EncodeUInkPage(*spliced, false, true);
		V11_REQUIRE(wire.status == UInkEncodeStatus::Success);
		const auto decoded = DecodePageWire(wire, header.headerExtension);
		const std::array expectedUndo{ 0u, 0u, 1u, 1u, 2u, 3u, 3u, 4u };
		V11_REQUIRE(decoded && decoded->content.size() == expectedUndo.size());
		for (size_t i = 0; i < expectedUndo.size(); ++i)
		{
			std::visit([&](const auto& block)
				{ V11_CHECK(block.contentId == i && block.undoId == expectedUndo[i]); }, decoded->content[i]);
		}
		V11_CHECK(std::holds_alternative<UInkShape>(decoded->content[2]) &&
			std::holds_alternative<UInkShape>(decoded->content[3]) &&
			std::holds_alternative<UInkClear>(decoded->content[4]));

		const auto imported = ImportUInkV11Page(*decoded);
		V11_REQUIRE(imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		const auto& page = *imported.page;
		V11_REQUIRE(page.background && page.operations.size() == 7 && page.strokes.size() == 3);
		V11_CHECK(page.pageGuid == currentPage.pageGuid && page.pageIndex == 7 && page.pageNumber == 8);
		V11_CHECK(page.background->rgb == 0xabcdef && page.background->pattern == 2);
		V11_CHECK(page.viewport.x == -32.0f && page.viewport.y == 17.5f && page.viewport.scale == 1.0f);
		V11_CHECK(page.intervalOrdinal == 1 && imported.counts.incompleteShapeObject == 0);
		const auto* first = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[0]);
		const auto* second = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[1]);
		const auto* shape = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[2]);
		const auto* clear = std::get_if<Draw3UInkClearSnapshot>(&page.operations[3]);
		V11_REQUIRE(first && second && first->points.size() == 1 && second->points.size() == 1);
		V11_REQUIRE(shape && shape->shape && clear);
		V11_CHECK(first->points[0].x == 11 && second->points[0].x == 12 &&
			first->undoId == 0 && second->undoId == 0);
		V11_CHECK(shape->shape->id == shapeId.Bytes() && shape->shape->parts.size() == 2 &&
			shape->undoId == 1 && clear->undoId == 2);
		for (size_t i = 0; i < page.strokes.size(); ++i)
		{
			const auto* operation = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[4 + i]);
			V11_REQUIRE(operation && operation->points.size() == 1 && page.strokes[i].points.size() == 1);
			V11_CHECK(operation->points[0].x == 21 + static_cast<float>(i) &&
				page.strokes[i].points[0].x == operation->points[0].x &&
				page.strokes[i].undoId == expectedUndo[5 + i] &&
				operation->undoId == expectedUndo[5 + i]);
		}
	}

	void TestSpliceKeepsEarlierClears(V11Test& test)
	{
		const Draw3UInkV11PageContext context{ TestPageGuid(78), 2 };
		UInkCanvas disk;
		disk.pageGuid = TestPageGuid(74);
		disk.workspaceGuid = context.workspaceGuid;
		disk.slideId = 172;
		disk.content = { TestInk(11, 0, 0), UInkClear{ 1, 6 }, TestInk(22, 2, 9),
			UInkClear{ 3, 12 }, TestInk(33, 4, 15) };
		auto page = TestPage(74);
		page.binding = 1;
		const auto current = ExportDraw3PageToUInkV11(page, context);
		V11_REQUIRE(current.status == Draw3UInkExportStatus::Success && current.canvas &&
			current.canvas->content.empty());
		Draw3UInkV11SpliceDiagnostics diagnostics;
		const auto spliced = SpliceUInkV11PreviousInterval(disk, *current.canvas, diagnostics);
		V11_REQUIRE(spliced && spliced->content.size() == 4);
		V11_CHECK(spliced->binding == 1 && !spliced->slideId);
		V11_CHECK(std::holds_alternative<UInkClear>(spliced->content[1]) &&
			std::holds_alternative<UInkClear>(spliced->content[3]));
		const auto* first = std::get_if<UInkInk>(&spliced->content[0]);
		const auto* second = std::get_if<UInkInk>(&spliced->content[2]);
		V11_REQUIRE(first && first->points.size() == 1 && second && second->points.size() == 1);
		V11_CHECK(first->points[0].x == 11 && second->points[0].x == 22);
	}

	void TestSpliceRejectsUntrustedInput(V11Test& test)
	{
		auto page = TestPage(75);
		page.strokes.push_back(TestStroke(3));
		const auto current = ExportDraw3PageToUInkV11(page);
		V11_REQUIRE(current.status == Draw3UInkExportStatus::Success && current.canvas);
		UInkCanvas disk = *current.canvas;
		disk.content.emplace_back(UInkClear{ 1, 1 });
		Draw3UInkV11SpliceDiagnostics diagnostics;
		V11_REQUIRE(SpliceUInkV11PreviousInterval(disk, *current.canvas, diagnostics));
		V11_CHECK(!SpliceUInkV11PreviousInterval(*current.canvas, *current.canvas, diagnostics));
		V11_REQUIRE(!diagnostics.diagnostics.empty());
		V11_CHECK(diagnostics.code == UInkDiagnosticCode::InvalidObjectOrder &&
			diagnostics.diagnostics.front().fieldPath == "draw3.v11.splice.clear");

		auto malformed = disk;
		std::get<UInkClear>(malformed.content[1]).undoId = 0;
		V11_CHECK(!SpliceUInkV11PreviousInterval(malformed, *current.canvas, diagnostics));
		V11_REQUIRE(!diagnostics.diagnostics.empty());
		V11_CHECK(diagnostics.diagnostics.front().fieldPath == "draw3.v11.splice.prefix");

		malformed = disk;
		const auto id = TestPageGuid(76);
		auto shape = TestLegacyLinePart(id);
		V11_REQUIRE(shape.object && std::holds_alternative<UInkShapeObject>(*shape.object));
		std::get<UInkShapeObject>(*shape.object).partCount = 2;
		malformed.content[0] = shape;
		V11_CHECK(!SpliceUInkV11PreviousInterval(malformed, *current.canvas, diagnostics));
		V11_REQUIRE(!diagnostics.diagnostics.empty());
		V11_CHECK(diagnostics.diagnostics.front().fieldPath == "draw3.v11.splice.prefix");

		malformed = disk;
		malformed.pageGuid = TestPageGuid(77);
		V11_CHECK(!SpliceUInkV11PreviousInterval(malformed, *current.canvas, diagnostics));
		V11_CHECK(diagnostics.code == UInkDiagnosticCode::InvalidFieldValue);
		V11_CHECK(!SpliceUInkV11PreviousInterval(disk, disk, diagnostics));
		V11_REQUIRE(!diagnostics.diagnostics.empty());
		V11_CHECK(diagnostics.diagnostics.front().fieldPath == "draw3.v11.splice.current");
	}

	void TestStoreFileAdapter(V11Test& test)
	{
		const auto id = CreateUInkGuid();
		V11_REQUIRE(id);
		std::error_code ec;
		const auto temporary = std::filesystem::temp_directory_path(ec);
		V11_REQUIRE(!ec);
		const auto directory = temporary / ("Inkeys-UInkDraw3V11-" + FormatUInkGuid(*id));
		V11_REQUIRE(std::filesystem::create_directory(directory, ec) && !ec);
		struct Cleanup
		{
			const std::filesystem::path& directory;
			std::wstring file;
			~Cleanup()
			{
				std::error_code error;
				if (!file.empty()) std::filesystem::remove(file, error);
				std::filesystem::remove(directory, error);
			}
		} cleanup{ directory };

		const Draw3UInkV11PageContext context{ TestPageGuid(60), 1 };
		const auto header = MakeDraw3UInkV11FileHeader(*id, context);
		V11_REQUIRE(header.status == Draw3UInkExportStatus::Success);
		auto one = TestPage(61);
		one.pageIndex = 1;
		one.strokes = { TestStroke(8) };
		auto older = ExportDraw3PageToUInkV11(one, context);
		V11_REQUIRE(older.status == Draw3UInkExportStatus::Success && older.canvas);
		const auto objectId = TestPageGuid(63);
		older.canvas->content.emplace_back(TestLegacyLinePart(objectId, 0, 1, 1));
		UInkStoreWriteResult result;
		auto writer = UInkPagedFileWriter::Create((directory / L"board.uink").wstring(),
			header.header, header.headerExtension, *older.canvas, result);
		cleanup.file = std::move(result.publishedPath);
		V11_REQUIRE(result.success && writer);
		auto zero = TestPage(62);
		zero.pageIndex = 0;
		auto first = ExportDraw3PageToUInkV11(zero, context);
		one.strokes = { TestStroke(28) };
		const auto replacement = ExportDraw3PageToUInkV11(one, context);
		V11_REQUIRE(first.status == Draw3UInkExportStatus::Success && first.canvas &&
			replacement.status == Draw3UInkExportStatus::Success && replacement.canvas);
		first.canvas->content.emplace_back(TestLegacyLinePart(objectId));
		V11_REQUIRE(writer->AppendPage(*first.canvas).success);
		V11_REQUIRE(writer->AppendPage(*replacement.canvas).success);
		writer->Close();
		const auto source = ReadUInkStoreFile(writer->Path());
		V11_REQUIRE(source.status == UInkReadStatus::Complete && source.document &&
			source.document->canvases.size() == 2);
		const auto read = ImportDraw3UInkV11File(source);
		V11_REQUIRE(read.status == Draw3UInkV11ImportStatus::Success && read.pages.size() == 2 &&
			read.pages[0].page && read.pages[1].page);
		V11_CHECK(read.pages[0].page->pageGuid == zero.pageGuid &&
			read.pages[1].page->pageGuid == one.pageGuid);
		V11_REQUIRE(read.pages[1].page->strokes.size() == 1 && read.pages[1].page->strokes[0].points.size() == 1);
		V11_CHECK(read.pages[1].page->strokes[0].points[0].x == 28);
		V11_REQUIRE(read.pages[0].page->strokes.size() == 1 && read.pages[0].page->strokes[0].shape);
		V11_CHECK(read.pages[0].counts.incompleteShapeObject == 0 &&
			read.pages[0].page->strokes[0].shape->id == objectId.Bytes());
	}

#undef V11_REQUIRE
#undef V11_CHECK
}

#include "uink_v11_draw3_shape_tests.inl"

int RunUInkV11Draw3Tests()
{
	const std::pair<const char*, void (*)(V11Test&)> cases[] = {
		{ "eraser", TestEraserNibRoundTrip },
		{ "eraser size limits", TestEraserSizeLimits },
		{ "highlighter", TestHighlighterWire },
		{ "metadata", TestPageMetadata },
		{ "log", TestPageLog },
		{ "undo numbering after skipped shapes", TestUndoNumberingAfterSkippedShapes },
		{ "foreign clear", TestForeignClearsAndUndoGaps },
		{ "foreign fallback", TestForeignInkAndMetadataFallbacks },
		{ "footprint skips oversized items", TestFootprintBudgetSkipsOnlyOversizedItems },
		{ "footprint item tile limit", TestFootprintItemTileLimit },
		{ "footprint page tile limit", TestFootprintPageTileLimit },
		{ "footprint exact page tile budget across Clear", TestExactFootprintPageTilesAcrossClear },
		{ "footprint exact budget across Clear", TestExactFootprintBudgetAcrossClear },
		{ "viewport runtime fallback", TestDraw3ViewportReadFallback },
		{ "file fallback summary", TestFileFallbackSummary },
		{ "file composition", TestForeignFileComposition },
		{ "upper layer eraser", TestUpperLayerEraser },
		{ "cross-page object IDs", TestCrossPageObjectIds },
		{ "splice previous interval", TestSplicePreviousIntervalRoundTrip },
		{ "splice earlier clears", TestSpliceKeepsEarlierClears },
		{ "splice invalid input", TestSpliceRejectsUntrustedInput },
		{ "store file", TestStoreFileAdapter },
	};
	int failures = 0;
	for (const auto& [name, run] : cases)
	{
		V11Test test{ name };
		run(test);
		failures += test.failures;
	}
	RunUInkV11Draw3ShapeTests(failures);
	return failures;
}
