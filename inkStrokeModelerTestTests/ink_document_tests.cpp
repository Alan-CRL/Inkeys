#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

import draw3.ink_document;

static_assert(sizeof(draw3::StoredInkPoint) == sizeof(float) * 3,
	"StoredInkPoint 只能包含 x/y/width 三个 float32 字段");

namespace
{
	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED ink document line " << line << ": " << expression << std::endl;
	}

#define INK_DOCUMENT_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)

	draw3::InkGuid MakeGuid(uint8_t seed)
	{
		std::array<uint8_t, 16> bytes = {};
		for (size_t index = 0; index < bytes.size(); ++index)
			bytes[index] = static_cast<uint8_t>(seed + index);
		return draw3::InkGuid(bytes);
	}

	draw3::InkStroke MakeStroke(draw3::StoredInkType type, float x)
	{
		draw3::StoredInkStyle style = {
			.inkType = type,
			.fallbackRgb = 0x123456u,
			.opacity = 0.75f,
			.texture = 42u
		};
		return draw3::InkStroke(style, { { x, -x, 8.0f } });
	}
}

int RunInkDocumentTests()
{
	int failures = 0;

	const draw3::InkGuid zeroGuid;
	const draw3::InkGuid workspaceGuid = MakeGuid(1);
	const draw3::InkGuid firstPageGuid = MakeGuid(21);
	const draw3::InkGuid secondPageGuid = MakeGuid(41);
	INK_DOCUMENT_CHECK(zeroGuid.IsZero());
	INK_DOCUMENT_CHECK(!workspaceGuid.IsZero());
	INK_DOCUMENT_CHECK(workspaceGuid == draw3::InkGuid(workspaceGuid.Bytes()));
	INK_DOCUMENT_CHECK(workspaceGuid != firstPageGuid);

	draw3::InkCanvasCollection collection(workspaceGuid);
	INK_DOCUMENT_CHECK(collection.WorkspaceGuid() == workspaceGuid);
	INK_DOCUMENT_CHECK(collection.Pages().empty());
	INK_DOCUMENT_CHECK(!collection.AppendPage(zeroGuid).has_value());
	const std::optional<size_t> firstPageIndex = collection.AppendPage(firstPageGuid);
	const std::optional<size_t> secondPageIndex = collection.AppendPage(secondPageGuid);
	INK_DOCUMENT_CHECK(firstPageIndex == 0u);
	INK_DOCUMENT_CHECK(secondPageIndex == 1u);
	INK_DOCUMENT_CHECK(!collection.AppendPage(firstPageGuid).has_value());
	INK_DOCUMENT_CHECK(collection.Pages().size() == 2);
	INK_DOCUMENT_CHECK(collection.Pages()[0].PageGuid() == firstPageGuid);
	INK_DOCUMENT_CHECK(collection.Pages()[1].PageGuid() == secondPageGuid);
	INK_DOCUMENT_CHECK(collection.PageAt(2) == nullptr);

	draw3::InkPage* firstPage = collection.PageAt(*firstPageIndex);
	INK_DOCUMENT_CHECK(firstPage != nullptr);
	INK_DOCUMENT_CHECK(firstPage->Canvases().empty());
	draw3::InkCanvas* defaultCanvas = firstPage->GetOrCreateCanvas(
		draw3::kDefaultDeviceKey);
	INK_DOCUMENT_CHECK(defaultCanvas != nullptr);
	INK_DOCUMENT_CHECK(defaultCanvas->Device() == draw3::kDefaultDeviceKey);
	INK_DOCUMENT_CHECK(defaultCanvas->Viewport().x == 0.0f);
	INK_DOCUMENT_CHECK(defaultCanvas->Viewport().y == 0.0f);
	INK_DOCUMENT_CHECK(defaultCanvas->Viewport().scale == 1.0f);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes().empty());

	const draw3::DeviceKey secondDevice(17);
	const draw3::InkViewport secondViewport = { -300.0f, 500.0f, 2.0f };
	draw3::InkCanvas* secondCanvas = firstPage->GetOrCreateCanvas(
		secondDevice, secondViewport);
	INK_DOCUMENT_CHECK(secondCanvas != nullptr);
	INK_DOCUMENT_CHECK(firstPage->Canvases().size() == 2);
	INK_DOCUMENT_CHECK(firstPage->Canvases()[0].Device() == draw3::kDefaultDeviceKey);
	INK_DOCUMENT_CHECK(firstPage->Canvases()[1].Device() == secondDevice);
	INK_DOCUMENT_CHECK(secondCanvas->Viewport().x == secondViewport.x);
	INK_DOCUMENT_CHECK(secondCanvas->Viewport().y == secondViewport.y);
	INK_DOCUMENT_CHECK(secondCanvas->Viewport().scale == secondViewport.scale);
	INK_DOCUMENT_CHECK(firstPage->FindCanvas(draw3::DeviceKey(999)) == nullptr);
	INK_DOCUMENT_CHECK(firstPage->GetOrCreateCanvas(secondDevice,
		{ 1.0f, 2.0f, 3.0f }) == secondCanvas);
	INK_DOCUMENT_CHECK(secondCanvas->Viewport().x == secondViewport.x);
	// 新增设备 Canvas 可能令 vector 扩容，后续按 key 重新取得指针。
	defaultCanvas = firstPage->FindCanvas(draw3::kDefaultDeviceKey);
	INK_DOCUMENT_CHECK(defaultCanvas != nullptr);

	const float infinity = (std::numeric_limits<float>::infinity)();
	const float nan = (std::numeric_limits<float>::quiet_NaN)();
	INK_DOCUMENT_CHECK(firstPage->GetOrCreateCanvas(draw3::DeviceKey(18),
		{ nan, 0.0f, 1.0f }) == nullptr);
	INK_DOCUMENT_CHECK(firstPage->GetOrCreateCanvas(draw3::DeviceKey(19),
		{ 0.0f, infinity, 1.0f }) == nullptr);
	INK_DOCUMENT_CHECK(firstPage->GetOrCreateCanvas(draw3::DeviceKey(20),
		{ 0.0f, 0.0f, 0.0f }) == nullptr);
	INK_DOCUMENT_CHECK(firstPage->GetOrCreateCanvas(draw3::DeviceKey(21),
		{ 0.0f, 0.0f, -1.0f }) == nullptr);
	INK_DOCUMENT_CHECK(firstPage->Canvases().size() == 2);

	draw3::StoredInkStyle style = {
		.inkType = draw3::StoredInkType::Pen,
		.fallbackRgb = 0xABCDEFu,
		.opacity = 0.5f,
		.texture = 65535u
	};
	const float farCoordinate = 1.0e30f;
	draw3::InkStroke infiniteCanvasStroke(style, {
		{ -123.25f, farCoordinate, 0.0f },
		{ farCoordinate, -farCoordinate, 18.5f }
	});
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.IsValid());
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Style().fallbackRgb == 0xABCDEFu);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Style().opacity == 0.5f);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Style().texture == 65535u);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Points()[0].x == -123.25f);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Points()[0].y == farCoordinate);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Points()[0].width == 0.0f);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Points()[1].x == farCoordinate);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Points()[1].y == -farCoordinate);
	INK_DOCUMENT_CHECK(infiniteCanvasStroke.Points()[1].width == 18.5f);

	INK_DOCUMENT_CHECK(!draw3::InkStroke(style, {}).IsValid());
	INK_DOCUMENT_CHECK(!draw3::InkStroke(style, { { nan, 0.0f, 1.0f } }).IsValid());
	INK_DOCUMENT_CHECK(!draw3::InkStroke(style, { { 0.0f, infinity, 1.0f } }).IsValid());
	INK_DOCUMENT_CHECK(!draw3::InkStroke(style, { { 0.0f, 0.0f, nan } }).IsValid());
	INK_DOCUMENT_CHECK(!draw3::InkStroke(style, { { 0.0f, 0.0f, -0.01f } }).IsValid());

	draw3::StoredInkStyle invalidStyle = style;
	invalidStyle.inkType = static_cast<draw3::StoredInkType>(255);
	INK_DOCUMENT_CHECK(!draw3::InkStroke(invalidStyle, { { 0.0f, 0.0f, 1.0f } }).IsValid());
	invalidStyle = style;
	invalidStyle.opacity = nan;
	INK_DOCUMENT_CHECK(!draw3::InkStroke(invalidStyle, { { 0.0f, 0.0f, 1.0f } }).IsValid());
	invalidStyle.opacity = -0.01f;
	INK_DOCUMENT_CHECK(!draw3::InkStroke(invalidStyle, { { 0.0f, 0.0f, 1.0f } }).IsValid());
	invalidStyle.opacity = 1.01f;
	INK_DOCUMENT_CHECK(!draw3::InkStroke(invalidStyle, { { 0.0f, 0.0f, 1.0f } }).IsValid());
	style.opacity = 0.0f;
	INK_DOCUMENT_CHECK(draw3::InkStroke(style, { { 0.0f, 0.0f, 1.0f } }).IsValid());
	style.opacity = 1.0f;
	INK_DOCUMENT_CHECK(draw3::InkStroke(style, { { 0.0f, 0.0f, 1.0f } }).IsValid());

	const std::array<draw3::StoredInkType, 4> shapeTypes = {
		draw3::StoredInkType::SolidLine,
		draw3::StoredInkType::DashedLine,
		draw3::StoredInkType::OutlineRectangle,
		draw3::StoredInkType::FilledRectangle
	};
	for (draw3::StoredInkType shapeType : shapeTypes)
	{
		draw3::StoredInkStyle shapeStyle = style;
		shapeStyle.inkType = shapeType;
		INK_DOCUMENT_CHECK(draw3::IsStoredShapeType(shapeType));
		INK_DOCUMENT_CHECK(draw3::InkStroke(shapeStyle,
			{ { 10.0f, 20.0f, 5.0f }, { 30.0f, 40.0f, 5.0f } }).IsValid());
		INK_DOCUMENT_CHECK(!draw3::InkStroke(shapeStyle,
			{ { 10.0f, 20.0f, 5.0f } }).IsValid());
		INK_DOCUMENT_CHECK(!draw3::InkStroke(shapeStyle,
			{ { 10.0f, 20.0f, 5.0f }, { 30.0f, 40.0f, 6.0f } }).IsValid());
	}
	draw3::StoredInkStyle solidLineStyle = style;
	solidLineStyle.inkType = draw3::StoredInkType::SolidLine;
	INK_DOCUMENT_CHECK(draw3::InkStroke(solidLineStyle,
		{ { 10.0f, 20.0f, 5.0f }, { 10.0f, 20.0f, 5.0f } }).IsValid());
	draw3::StoredInkStyle outlineRectangleStyle = style;
	outlineRectangleStyle.inkType = draw3::StoredInkType::OutlineRectangle;
	INK_DOCUMENT_CHECK(!draw3::InkStroke(outlineRectangleStyle,
		{ { 10.0f, 20.0f, 5.0f }, { 10.0f, 40.0f, 5.0f } }).IsValid());

	const std::optional<size_t> firstStrokeIndex = defaultCanvas->AppendStroke(
		MakeStroke(draw3::StoredInkType::Pen, 10.0f));
	const std::optional<size_t> secondStrokeIndex = defaultCanvas->AppendStroke(
		MakeStroke(draw3::StoredInkType::Highlighter, 20.0f));
	const std::optional<size_t> thirdStrokeIndex = defaultCanvas->AppendStroke(
		MakeStroke(draw3::StoredInkType::Eraser, 30.0f));
	INK_DOCUMENT_CHECK(firstStrokeIndex == 0u);
	INK_DOCUMENT_CHECK(secondStrokeIndex == 1u);
	INK_DOCUMENT_CHECK(thirdStrokeIndex == 2u);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes().size() == 3);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes()[0].Style().inkType ==
		draw3::StoredInkType::Pen);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes()[1].Style().inkType ==
		draw3::StoredInkType::Highlighter);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes()[2].Style().inkType ==
		draw3::StoredInkType::Eraser);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes()[0].Points()[0].x == 10.0f);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes()[1].Points()[0].x == 20.0f);
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes()[2].Points()[0].x == 30.0f);
	INK_DOCUMENT_CHECK(secondCanvas->Strokes().empty());

	const size_t strokeCount = defaultCanvas->Strokes().size();
	INK_DOCUMENT_CHECK(!defaultCanvas->AppendStroke(
		draw3::InkStroke(style, { { 0.0f, 0.0f, -1.0f } })).has_value());
	INK_DOCUMENT_CHECK(defaultCanvas->Strokes().size() == strokeCount);

	const draw3::InkGuid thirdPageGuid = MakeGuid(61);
	const draw3::InkGuid fourthPageGuid = MakeGuid(81);
	draw3::InkPage thirdPage(thirdPageGuid);
	draw3::InkPage fourthPage(fourthPageGuid);
	INK_DOCUMENT_CHECK(thirdPage.GetOrCreateCanvas(draw3::kDefaultDeviceKey) != nullptr);
	INK_DOCUMENT_CHECK(fourthPage.GetOrCreateCanvas(draw3::kDefaultDeviceKey) != nullptr);
	INK_DOCUMENT_CHECK(collection.AppendPage(std::move(thirdPage)) == 2u);
	INK_DOCUMENT_CHECK(collection.AppendPage(std::move(fourthPage)) == 3u);
	INK_DOCUMENT_CHECK(collection.Pages().size() == 4);
	INK_DOCUMENT_CHECK(collection.Pages()[2].Canvases().size() == 1);
	INK_DOCUMENT_CHECK(collection.Pages()[3].Canvases().size() == 1);
	INK_DOCUMENT_CHECK(collection.Pages()[2].Canvases()[0].Strokes().empty());
	INK_DOCUMENT_CHECK(collection.Pages()[3].Canvases()[0].Strokes().empty());
	INK_DOCUMENT_CHECK(collection.Pages()[0].Canvases()[0].Strokes().size() == 3);

	const draw3::InkCanvasCollection& readOnlyCollection = collection;
	INK_DOCUMENT_CHECK(readOnlyCollection.PageAt(0) != nullptr);
	INK_DOCUMENT_CHECK(readOnlyCollection.PageAt(4) == nullptr);
	INK_DOCUMENT_CHECK(readOnlyCollection.PageAt(0)->FindCanvas(
		draw3::kDefaultDeviceKey) != nullptr);

	if (failures == 0) std::cout << "All ink document tests passed." << std::endl;
	return failures;
}
