#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

import Inkeys.CV.ShapeRecognition;
import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;
import Inkeys.Drawing.Draw3.shape_recognition;

namespace
{
	bool Expect(bool condition, std::string_view name)
	{
		if (!condition) std::cerr << "[Draw3ShapeRecognition] failed: " << name << '\n';
		return condition;
	}

	using CvPoint = Inkeys::CV::InkPoint;
	using CvStroke = std::vector<CvPoint>;

	CvStroke Line(float x0, float y0, float x1, float y1, float width = 5.0f)
	{
		return { { x0, y0, width }, { x1, y1, width } };
	}

	bool RecognizesRectangle(const std::vector<CvStroke>& strokes)
	{
		std::vector<Inkeys::CV::InkStrokeView> views;
		views.reserve(strokes.size());
		for (const CvStroke& stroke : strokes) views.push_back({ stroke });
		return Inkeys::CV::RecognizeInkShape(views, 1.0f).type ==
			Inkeys::CV::ShapeType::AxisAlignedRectangle;
	}

	std::vector<CvStroke> StandardRectangle()
	{
		return {
			Line(10.0f, 20.0f, 130.0f, 20.0f),
			Line(130.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 10.0f, 100.0f),
			Line(10.0f, 100.0f, 10.0f, 20.0f)
		};
	}

	int RunRecognitionSamples()
	{
		int failures = 0;
		if (!Expect(RecognizesRectangle(StandardRectangle()),
			"standard four-stroke rectangle")) ++failures;
		if (!Expect(RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 130.0f, 20.0f, 5.0f },
			{ 130.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f },
			{ 10.0f, 20.0f, 5.0f } } }), "single closed stroke")) ++failures;
		if (!Expect(RecognizesRectangle({ {
			{ 10.0f, 100.0f, 5.0f }, { 10.0f, 20.0f, 5.0f },
			{ 130.0f, 20.0f, 5.0f } }, {
			{ 130.0f, 20.0f, 5.0f }, { 130.0f, 100.0f, 5.0f },
			{ 10.0f, 100.0f, 5.0f } } }), "two L-shaped strokes")) ++failures;
		if (!Expect(RecognizesRectangle({
			Line(130.0f, 100.0f, 130.0f, 20.0f),
			Line(10.0f, 20.0f, 10.0f, 100.0f),
			Line(130.0f, 20.0f, 10.0f, 20.0f),
			Line(10.0f, 100.0f, 130.0f, 100.0f) }),
			"reverse and unordered edges")) ++failures;
		if (!Expect(RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 50.0f, 19.0f, 5.0f },
			{ 90.0f, 21.0f, 5.0f }, { 130.0f, 20.0f, 5.0f } },
			{ { 130.0f, 20.0f, 5.0f }, { 129.0f, 60.0f, 5.0f },
			{ 131.0f, 100.0f, 5.0f } },
			{ { 131.0f, 100.0f, 5.0f }, { 70.0f, 99.0f, 5.0f },
			{ 10.0f, 100.0f, 5.0f } },
			{ { 10.0f, 100.0f, 5.0f }, { 11.0f, 60.0f, 5.0f },
			{ 10.0f, 20.0f, 5.0f } } }), "slight jitter")) ++failures;
		if (!Expect(RecognizesRectangle({
			Line(16.0f, 20.0f, 124.0f, 20.0f),
			Line(130.0f, 26.0f, 130.0f, 94.0f),
			Line(124.0f, 100.0f, 16.0f, 100.0f),
			Line(10.0f, 94.0f, 10.0f, 26.0f) }), "small corner gaps")) ++failures;
		auto repeated = StandardRectangle();
		repeated.push_back(Line(10.0f, 20.5f, 130.0f, 20.5f));
		if (!Expect(RecognizesRectangle(repeated), "repeated edge")) ++failures;

		if (!Expect(!RecognizesRectangle({
			Line(0.0f, 0.0f, 20.0f, 0.0f), Line(20.0f, 0.0f, 20.0f, 20.0f),
			Line(20.0f, 20.0f, 0.0f, 20.0f), Line(0.0f, 20.0f, 0.0f, 0.0f) }),
			"too small")) ++failures;
		if (!Expect(!RecognizesRectangle({
			Line(10.0f, 20.0f, 10.0f, 100.0f),
			Line(10.0f, 100.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 130.0f, 20.0f) }), "open U")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 130.0f, 20.0f, 5.0f },
			{ 130.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f } } }),
			"open C")) ++failures;
		if (!Expect(!RecognizesRectangle({
			Line(10.0f, 20.0f, 130.0f, 20.0f),
			Line(130.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 10.0f, 100.0f) }),
			"explicit missing edge")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 30.0f, 20.0f, 5.0f }, { 110.0f, 20.0f, 5.0f },
			{ 130.0f, 40.0f, 5.0f }, { 130.0f, 80.0f, 5.0f },
			{ 110.0f, 100.0f, 5.0f }, { 30.0f, 100.0f, 5.0f },
			{ 10.0f, 80.0f, 5.0f }, { 10.0f, 40.0f, 5.0f },
			{ 30.0f, 20.0f, 5.0f } } }), "over-rounded corners")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 30.0f, 27.0f, 5.0f },
			{ 50.0f, 20.0f, 5.0f }, { 70.0f, 27.0f, 5.0f },
			{ 90.0f, 20.0f, 5.0f }, { 110.0f, 27.0f, 5.0f },
			{ 130.0f, 20.0f, 5.0f } },
			Line(130.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 10.0f, 100.0f),
			Line(10.0f, 100.0f, 10.0f, 20.0f) }),
			"zigzag edge exceeds axis limit")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 10.0f, 100.0f, 5.0f }, { 70.0f, 20.0f, 5.0f },
			{ 130.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f } } }),
			"triangle")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 30.0f, 20.0f, 5.0f }, { 130.0f, 20.0f, 5.0f },
			{ 110.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f },
			{ 30.0f, 20.0f, 5.0f } } }), "parallelogram")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 40.0f, 10.0f, 5.0f }, { 140.0f, 50.0f, 5.0f },
			{ 110.0f, 130.0f, 5.0f }, { 10.0f, 90.0f, 5.0f },
			{ 40.0f, 10.0f, 5.0f } } }), "tilted rectangle")) ++failures;
		if (!Expect(!RecognizesRectangle({
			Line(10.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 20.0f, 10.0f, 100.0f) }), "X strokes")) ++failures;
		auto interior = StandardRectangle();
		interior.push_back({ { 20.0f, 30.0f, 5.0f }, { 120.0f, 90.0f, 5.0f },
			{ 20.0f, 90.0f, 5.0f }, { 120.0f, 30.0f, 5.0f } });
		if (!Expect(!RecognizesRectangle(interior), "interior scribble")) ++failures;

		CvStroke circle;
		for (int index = 0; index <= 64; ++index)
		{
			const float angle = static_cast<float>(index) * 6.28318530718f / 64.0f;
			circle.push_back({ 70.0f + 60.0f * std::cos(angle),
				60.0f + 40.0f * std::sin(angle), 5.0f });
		}
		if (!Expect(!RecognizesRectangle({ circle }), "ellipse")) ++failures;
		return failures;
	}

	Inkeys::Drawing::Draw3::StrokeTileFootprint Footprint(int tileX)
	{
		using namespace Inkeys::Drawing::Draw3;
		return { { static_cast<float>(tileX * 256), 0.0f,
			static_cast<float>((tileX + 1) * 256), 128.0f },
			{ { tileX * 2, 0 } }, { { tileX, 0 } } };
	}

	int RunConditionalHistoryTests()
	{
		using namespace Inkeys::Drawing::Draw3;
		int failures = 0;
		CanvasRuntimeHistory history;
		const auto first = history.AppendStroke(0, Footprint(0));
		const auto second = history.AppendStroke(1, Footprint(1));
		if (!Expect(first.has_value() && second.has_value(), "history sources append"))
			return failures + 1;
		const std::array<RenderItemId, 2> sources = { *first, *second };
		if (!Expect(history.SetRenderOnlyWhenLatest(sources, true),
			"mark conditional sources")) ++failures;
		const auto correction = history.AppendStroke(2, Footprint(2));
		if (!Expect(correction.has_value(), "correction append")) return failures + 1;
		if (!Expect(!history.Find(*first)->visible && !history.Find(*second)->visible &&
			history.Find(*correction)->visible, "correction hides sources")) ++failures;
		if (!Expect(history.VisibleCompositionTiles({ -1.0f, -1.0f, 1024.0f, 256.0f }) ==
			std::vector<SignedTileCoordinate>{ { 2, 0 } },
			"correction tile visibility")) ++failures;
		if (!Expect(history.UndoLastVisible(*correction) &&
			history.Find(*first)->visible && history.Find(*second)->visible,
			"undo correction restores all sources")) ++failures;
		if (!Expect(history.VisibleCompositionTiles({ -1.0f, -1.0f, 1024.0f, 256.0f }) ==
			std::vector<SignedTileCoordinate>{ { 0, 0 }, { 1, 0 } },
			"undo restores source tile references")) ++failures;
		if (!Expect(history.UndoLastVisible(*second) && history.Find(*first)->visible &&
			!history.Find(*second)->active, "source undo remains per stroke")) ++failures;
		if (!Expect(history.RedoLastUndone(*second) && history.Find(*second)->visible,
			"source redo restores conditional tail")) ++failures;
		if (!Expect(history.RedoLastUndone(*correction) &&
			!history.Find(*first)->visible && !history.Find(*second)->visible,
			"correction redo hides sources")) ++failures;
		if (!Expect(history.VisibleCompositionTiles({ -1.0f, -1.0f, 1024.0f, 256.0f }) ==
			std::vector<SignedTileCoordinate>{ { 2, 0 } },
			"redo restores correction tile references")) ++failures;

		CanvasRuntimeHistory metadataResetHistory;
		const auto resetSource = metadataResetHistory.AppendStroke(0, Footprint(5));
		if (!resetSource || !metadataResetHistory.SetRenderOnlyWhenLatest(
			std::span<const RenderItemId>(&*resetSource, 1), true) ||
			!metadataResetHistory.AppendStroke(1, Footprint(6))) return failures + 1;
		if (!Expect(!metadataResetHistory.Find(*resetSource)->visible &&
			metadataResetHistory.SetRenderOnlyWhenLatest(
				std::span<const RenderItemId>(&*resetSource, 1), false) &&
			metadataResetHistory.Find(*resetSource)->active &&
			metadataResetHistory.Find(*resetSource)->visible,
			"clearing final conditional metadata restores active visibility")) ++failures;

		if (!Expect(history.UndoLastVisible(*correction), "undo before branching")) ++failures;
		const std::vector<RenderItemId> cleared = history.ClearLatestConditionalGroup();
		history.DiscardRedoBranch();
		const auto branch = history.AppendStroke(3, Footprint(3));
		if (!Expect(cleared.size() == 2 && branch.has_value() &&
			history.Find(*first)->visible && history.Find(*second)->visible &&
			!history.Find(*first)->renderOnlyWhenLatest && history.RedoDepth() == 0,
			"new branch solidifies restored sources")) ++failures;

		CanvasRuntimeHistory otherPage;
		const auto other = otherPage.AppendStroke(0, Footprint(4));
		if (!Expect(other.has_value() && otherPage.Find(*other)->visible &&
			history.Revision() != 0, "page histories remain isolated")) ++failures;
		return failures;
	}

	int RunAdapterAndTriggerTests()
	{
		using namespace Inkeys::Drawing::Draw3;
		int failures = 0;
		std::array<std::uint8_t, 16> bytes = {};
		bytes[15] = 1;
		InkPage page{ InkGuid(bytes) };
		InkCanvas* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory history;
		StoredInkStyle style{ StoredInkType::Pen, 0x123456u, 0.75f, 0 };
		for (const CvStroke& input : StandardRectangle())
		{
			std::vector<StoredInkPoint> points;
			for (const CvPoint& point : input)
				points.push_back({ point.x, point.y, point.width });
			const auto index = canvas->AppendStroke(InkStroke(style, std::move(points)));
			const auto footprint = index
				? BuildStrokeTileFootprint(canvas->Strokes()[*index]) : std::nullopt;
			if (!index || !footprint || !history.AppendStroke(*index, *footprint))
				return failures + 1;
		}
		const auto plan = BuildShapeCorrectionPlan(*canvas, history, 1.0f);
		if (!Expect(plan.has_value() && plan->sourceItems.size() == 4 &&
			plan->replacement.Style().inkType == StoredInkType::OutlineRectangle &&
			plan->replacement.Style().fallbackRgb == style.fallbackRgb &&
			plan->replacement.Style().opacity == style.opacity &&
			plan->replacement.Points()[0].width == 5.0f,
			"adapter preserves style and all source strokes")) ++failures;
		if (!Expect(canvas->SetStrokeRenderOnlyWhenLatest(0, true) &&
			canvas->Strokes()[0].RenderOnlyWhenLatest() &&
			canvas->SetStrokeRenderOnlyWhenLatest(0, false) &&
			!canvas->Strokes()[0].RenderOnlyWhenLatest(),
			"conditional metadata stays on InkStroke")) ++failures;

		std::vector<StoredInkPoint> unrelated = {
			{ 200.0f, 200.0f, 5.0f }, { 240.0f, 230.0f, 5.0f } };
		const auto unrelatedIndex = canvas->AppendStroke(InkStroke(style, std::move(unrelated)));
		const auto unrelatedFootprint = unrelatedIndex
			? BuildStrokeTileFootprint(canvas->Strokes()[*unrelatedIndex]) : std::nullopt;
		if (!unrelatedIndex || !unrelatedFootprint ||
			!history.AppendStroke(*unrelatedIndex, *unrelatedFootprint)) return failures + 1;
		if (!Expect(!BuildShapeCorrectionPlan(*canvas, history, 1.0f).has_value(),
			"latest unrelated stroke blocks replacement")) ++failures;
		if (!Expect(!canvas->RollbackLastStroke(*unrelatedIndex - 1) &&
			canvas->RollbackLastStroke(*unrelatedIndex) && canvas->Strokes().size() == 4,
			"uncommitted tail rollback is index-guarded")) ++failures;

		if (!Expect(CanAttemptShapeRecognition({}), "idle trigger accepts once ready")) ++failures;
		if (!Expect(!CanAttemptShapeRecognition({ true, false, false, false, false }) &&
			!CanAttemptShapeRecognition({ false, true, false, false, false }) &&
			!CanAttemptShapeRecognition({ false, false, true, false, false }) &&
			!CanAttemptShapeRecognition({ false, false, false, true, false }) &&
			!CanAttemptShapeRecognition({ false, false, false, false, true }),
			"trigger rejects every active gate")) ++failures;

		ShapeRecognitionTriggerLatch latch;
		latch.Arm(page.PageGuid(), history.Revision());
		if (!Expect(latch.Pending() &&
			!latch.ConsumeIfReady(page.PageGuid(), history.Revision(),
				{ true, false, false, false, false }) && latch.Pending(),
			"trigger remains armed while a gate is active")) ++failures;
		if (!Expect(latch.ConsumeIfReady(page.PageGuid(), history.Revision(), {}) &&
			!latch.Pending() &&
			!latch.ConsumeIfReady(page.PageGuid(), history.Revision(), {}),
			"one pen commit is consumed exactly once")) ++failures;
		latch.Arm(page.PageGuid(), history.Revision());
		if (!Expect(!latch.ConsumeIfReady(
			page.PageGuid(), history.Revision() + 1, {}) && !latch.Pending(),
			"history change invalidates a pending commit")) ++failures;
		return failures;
	}
}

int RunDraw3ShapeRecognitionTests()
{
	return RunRecognitionSamples() + RunConditionalHistoryTests() +
		RunAdapterAndTriggerTests();
}
