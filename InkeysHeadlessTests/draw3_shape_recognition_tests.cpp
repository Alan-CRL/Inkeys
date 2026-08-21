#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
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

	Inkeys::CV::ShapeResult RecognizeShape(const std::vector<CvStroke>& strokes,
		Inkeys::CV::ShapeRecognitionDiagnostics* diagnostics = nullptr,
		float dpiScale = 1.0f)
	{
		std::vector<Inkeys::CV::InkStrokeView> views;
		views.reserve(strokes.size());
		for (const CvStroke& stroke : strokes) views.push_back({ stroke });
		return Inkeys::CV::RecognizeInkShape(views, dpiScale, diagnostics);
	}

	bool RecognizesRectangle(const std::vector<CvStroke>& strokes)
	{
		return RecognizeShape(strokes).type ==
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

	std::vector<CvStroke> ScaleStrokes(
		std::vector<CvStroke> strokes, float scale)
	{
		for (CvStroke& stroke : strokes)
		{
			for (CvPoint& point : stroke)
			{
				point.x *= scale;
				point.y *= scale;
				point.width *= scale;
			}
		}
		return strokes;
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
		Inkeys::CV::ShapeRecognitionDiagnostics roughDiagnostics;
		const std::vector<CvStroke> roughRectangle = { {
			{ 12.0f, 23.0f, 4.2f }, { 35.0f, 18.0f, 5.1f },
			{ 72.0f, 22.0f, 5.8f }, { 128.0f, 17.0f, 4.7f },
			{ 133.0f, 43.0f, 5.4f }, { 129.0f, 76.0f, 6.1f },
			{ 134.0f, 101.0f, 4.9f }, { 99.0f, 97.0f, 5.6f },
			{ 61.0f, 103.0f, 4.3f }, { 14.0f, 98.0f, 5.2f },
			{ 8.0f, 72.0f, 5.9f }, { 13.0f, 42.0f, 4.6f },
			{ 12.0f, 23.0f, 5.0f } } };
		const Inkeys::CV::ShapeResult rough = RecognizeShape(
			roughRectangle, &roughDiagnostics);
		if (!Expect(rough.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			roughDiagnostics.selectedApproximationEpsilonRatio >= 0.02f &&
			roughDiagnostics.edgeBandDip >=
				roughDiagnostics.thresholds.minimumEdgeBandDip,
			"rough pressure-varying single stroke")) ++failures;
		Inkeys::CV::ShapeRecognitionDiagnostics multiScaleDiagnostics;
		const Inkeys::CV::ShapeResult multiScale = RecognizeShape({ {
			{ 10.0f, 20.0f, 6.0f }, { 70.0f, 8.0f, 6.0f },
			{ 130.0f, 20.0f, 6.0f }, { 130.0f, 100.0f, 6.0f },
			{ 10.0f, 100.0f, 6.0f }, { 10.0f, 20.0f, 6.0f } } },
			&multiScaleDiagnostics);
		if (!Expect(multiScale.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			multiScaleDiagnostics.approximationCornerCounts[0] == 5 &&
			multiScaleDiagnostics.selectedApproximationEpsilonRatio > 0.02f,
			"five-corner hull falls back to a coarser quad")) ++failures;
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

		Inkeys::CV::ShapeRecognitionDiagnostics dpi1Diagnostics;
		Inkeys::CV::ShapeRecognitionDiagnostics dpi2Diagnostics;
		const Inkeys::CV::ShapeResult dpi1 = RecognizeShape(
			roughRectangle, &dpi1Diagnostics, 1.0f);
		const Inkeys::CV::ShapeResult dpi2 = RecognizeShape(
			ScaleStrokes(roughRectangle, 2.0f), &dpi2Diagnostics, 2.0f);
		if (!Expect(dpi1.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			dpi2.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			std::abs(dpi1.rectangle.left - dpi2.rectangle.left / 2.0f) < 0.01f &&
			std::abs(dpi1Diagnostics.edgeBandDip - dpi2Diagnostics.edgeBandDip) < 0.01f,
			"DPI-scaled recognition is DIP-equivalent")) ++failures;

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

		Inkeys::CV::ShapeRecognitionDiagnostics acceptedDiagnostics;
		const Inkeys::CV::ShapeResult accepted = RecognizeShape(
			StandardRectangle(), &acceptedDiagnostics);
		if (!Expect(accepted.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			acceptedDiagnostics.accepted &&
			acceptedDiagnostics.failedConditionCount == 0 &&
			acceptedDiagnostics.strokes[0].sampledPoints.size() ==
				acceptedDiagnostics.strokes[0].sampledPointCount &&
			acceptedDiagnostics.rejectionReason ==
				Inkeys::CV::ShapeRecognitionRejectReason::None &&
			acceptedDiagnostics.sourcePointCount == 8 &&
			acceptedDiagnostics.sampledPointCount >= 4 &&
			acceptedDiagnostics.sampledPointCount <=
				acceptedDiagnostics.thresholds.maximumTotalSampledPoints &&
			acceptedDiagnostics.confidence >=
				acceptedDiagnostics.thresholds.minimumConfidence,
			"accepted sample exposes complete diagnostics")) ++failures;

		Inkeys::CV::ShapeRecognitionDiagnostics smallDiagnostics;
		const Inkeys::CV::ShapeResult small = RecognizeShape({
			Line(0.0f, 0.0f, 20.0f, 0.0f), Line(20.0f, 0.0f, 20.0f, 20.0f),
			Line(20.0f, 20.0f, 0.0f, 20.0f), Line(0.0f, 20.0f, 0.0f, 0.0f) },
			&smallDiagnostics);
		if (!Expect(small.type == Inkeys::CV::ShapeType::Unknown &&
			!smallDiagnostics.accepted &&
			smallDiagnostics.rejectionReason ==
				Inkeys::CV::ShapeRecognitionRejectReason::ShapeTooSmall &&
			smallDiagnostics.shortSide == 20.0f &&
			smallDiagnostics.minimumShortSide == 40.0f &&
			smallDiagnostics.thresholds.minimumShortSideWidthMultiple == 8.0f &&
			std::string_view(Inkeys::CV::ShapeRecognitionRejectReasonName(
				smallDiagnostics.rejectionReason)) == "shape_too_small" &&
			smallDiagnostics.failedConditionCount >= 1 &&
			std::isfinite(smallDiagnostics.edgeCoverage[0]) &&
			std::isfinite(smallDiagnostics.totalCoverage) &&
			std::isfinite(smallDiagnostics.confidence),
			"rejected sample exposes reason metric and threshold")) ++failures;
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
		ShapeRecognitionDatasetDiagnostics acceptedDataset;
		const auto plan = BuildShapeCorrectionPlan(
			*canvas, history, 1.0f, &acceptedDataset);
		if (!Expect(plan.has_value() && plan->sourceItems.size() == 4 &&
			plan->replacement.Style().inkType == StoredInkType::OutlineRectangle &&
			plan->replacement.Style().fallbackRgb == style.fallbackRgb &&
			plan->replacement.Style().opacity == style.opacity &&
			plan->replacement.Points()[0].width == 5.0f,
			"adapter preserves style and all source strokes")) ++failures;
		if (!Expect(acceptedDataset.accepted &&
			acceptedDataset.acceptedStrokeCount == 4 &&
			acceptedDataset.collectedStrokeCount == 4 &&
			acceptedDataset.attempts.size() == 1 &&
			acceptedDataset.attempts.front().outcome ==
				ShapeRecognitionAttemptOutcome::Accepted &&
			acceptedDataset.attempts.front().vision.accepted &&
			acceptedDataset.attempts.front().sourcePointCount == 8 &&
			acceptedDataset.attempts.front().sourceItems.size() == 4 &&
			acceptedDataset.strokes.size() == 4 &&
			acceptedDataset.strokes.front().pointsDip.size() == 2 &&
			acceptedDataset.strokes.front().pointsDip.front().x == 10.0f,
			"adapter reports accepted maximum suffix")) ++failures;

		std::array<std::uint8_t, 16> pressureBytes = {};
		pressureBytes[15] = 7;
		InkPage pressurePage{ InkGuid(pressureBytes) };
		InkCanvas* pressureCanvas = pressurePage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory pressureHistory;
		const std::array<float, 4> pressureWidths = { 3.5f, 5.0f, 6.5f, 4.2f };
		const std::vector<CvStroke> pressureRectangle = StandardRectangle();
		for (std::size_t index = 0; index < pressureRectangle.size(); ++index)
		{
			std::vector<StoredInkPoint> points;
			for (const CvPoint& point : pressureRectangle[index])
				points.push_back({ point.x, point.y, pressureWidths[index] });
			const auto strokeIndex = pressureCanvas->AppendStroke(
				InkStroke(style, std::move(points)));
			const auto footprint = strokeIndex
				? BuildStrokeTileFootprint(pressureCanvas->Strokes()[*strokeIndex])
				: std::nullopt;
			if (!strokeIndex || !footprint ||
				!pressureHistory.AppendStroke(*strokeIndex, *footprint))
				return failures + 1;
		}
		ShapeRecognitionDatasetDiagnostics pressureDataset;
		const auto pressurePlan = BuildShapeCorrectionPlan(
			*pressureCanvas, pressureHistory, 1.0f, &pressureDataset);
		if (!Expect(pressurePlan.has_value() && pressurePlan->sourceItems.size() == 4 &&
			pressureDataset.collectedStrokeCount == 4 &&
			pressureDataset.collectionStopReason ==
				ShapeCandidateCollectionStopReason::HistoryStart,
			"pressure width changes do not split one pen style")) ++failures;
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

		std::array<std::uint8_t, 16> diagnosticBytes = {};
		diagnosticBytes[15] = 2;
		InkPage diagnosticPage{ InkGuid(diagnosticBytes) };
		InkCanvas* diagnosticCanvas = diagnosticPage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory diagnosticHistory;
		ShapeRecognitionDatasetDiagnostics collectionDiagnostics;
		if (!Expect(diagnosticCanvas &&
			!BuildShapeCorrectionPlan(*diagnosticCanvas, diagnosticHistory, 1.0f,
				&collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::NoActiveItem,
			"adapter reports no active candidate")) ++failures;

		StoredInkStyle highlighterStyle = style;
		highlighterStyle.inkType = StoredInkType::Highlighter;
		const auto highlighterIndex = diagnosticCanvas->AppendStroke(InkStroke(
			highlighterStyle, { { 0.0f, 0.0f, 5.0f }, { 100.0f, 0.0f, 5.0f } }));
		const auto highlighterFootprint = highlighterIndex
			? BuildStrokeTileFootprint(diagnosticCanvas->Strokes()[*highlighterIndex])
			: std::nullopt;
		if (!highlighterIndex || !highlighterFootprint ||
			!diagnosticHistory.AppendStroke(*highlighterIndex, *highlighterFootprint))
			return failures + 1;
		if (!Expect(!BuildShapeCorrectionPlan(*diagnosticCanvas, diagnosticHistory,
			1.0f, &collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::NonPenStroke &&
			collectionDiagnostics.collectedStrokeCount == 0,
			"adapter reports non-pen boundary")) ++failures;

		diagnosticBytes[15] = 3;
		InkPage stylePage{ InkGuid(diagnosticBytes) };
		InkCanvas* styleCanvas = stylePage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory styleHistory;
		StoredInkStyle alternateStyle = style;
		alternateStyle.fallbackRgb = 0x654321u;
		const std::array<StoredInkStyle, 2> styles = { style, alternateStyle };
		for (std::size_t index = 0; index < styles.size(); ++index)
		{
			const auto strokeIndex = styleCanvas->AppendStroke(InkStroke(styles[index],
				{ { 10.0f, 20.0f + static_cast<float>(index) * 80.0f, 5.0f },
				{ 130.0f, 20.0f + static_cast<float>(index) * 80.0f, 5.0f } }));
			const auto footprint = strokeIndex
				? BuildStrokeTileFootprint(styleCanvas->Strokes()[*strokeIndex])
				: std::nullopt;
			if (!strokeIndex || !footprint ||
				!styleHistory.AppendStroke(*strokeIndex, *footprint)) return failures + 1;
		}
		if (!Expect(!BuildShapeCorrectionPlan(*styleCanvas, styleHistory, 1.0f,
			&collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::StyleMismatch &&
			collectionDiagnostics.collectedStrokeCount == 1 &&
			collectionDiagnostics.attempts.size() == 1,
			"adapter reports style boundary before vision attempt")) ++failures;

		diagnosticBytes[15] = 4;
		InkPage sixStrokePage{ InkGuid(diagnosticBytes) };
		InkCanvas* sixStrokeCanvas = sixStrokePage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory sixStrokeHistory;
		for (std::size_t index = 0; index < 6; ++index)
		{
			const float y = 20.0f + static_cast<float>(index) * 10.0f;
			const auto strokeIndex = sixStrokeCanvas->AppendStroke(InkStroke(
				style, { { 10.0f, y, 5.0f }, { 130.0f, y, 5.0f } }));
			const auto footprint = strokeIndex
				? BuildStrokeTileFootprint(sixStrokeCanvas->Strokes()[*strokeIndex])
				: std::nullopt;
			if (!strokeIndex || !footprint ||
				!sixStrokeHistory.AppendStroke(*strokeIndex, *footprint)) return failures + 1;
		}
		if (!Expect(!BuildShapeCorrectionPlan(*sixStrokeCanvas, sixStrokeHistory, 1.0f,
			&collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectedStrokeCount == 6 &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::HistoryStart,
			"six-stroke history start is not reported as a truncated candidate")) ++failures;

		ShapeRecognitionDatasetDiagnostics formatterDiagnostics;
		formatterDiagnostics.attempts.emplace_back();
		auto& formatterAttempt = formatterDiagnostics.attempts.front();
		formatterAttempt.vision.dpiScale = 1.5f;
		formatterAttempt.vision.medianWidth = 7.0f;
		formatterAttempt.vision.bounds = { 1.0f, 2.0f, 31.0f, 42.0f };
		formatterAttempt.vision.rejectionReason =
			Inkeys::CV::ShapeRecognitionRejectReason::ShapeTooSmall;
		formatterAttempt.vision.failedConditions[0] =
			Inkeys::CV::ShapeRecognitionRejectReason::ShapeTooSmall;
		formatterAttempt.vision.failedConditionCount = 1;
		formatterAttempt.vision.inputStrokeCount = 1;
		formatterAttempt.vision.strokes[0].sourcePointCount = 2;
		formatterAttempt.vision.strokes[0].sampledPointCount = 2;
		formatterAttempt.vision.strokes[0].sampledPoints = {
			{ 1.5f, 3.0f, 4.5f }, { 6.0f, 7.5f, 4.5f } };
		formatterAttempt.representativeWidth = 0.0f;
		formatterDiagnostics.dpiScale = 1.5f;
		formatterDiagnostics.strokes.push_back({
			{ 3, 7 }, 11, 2, false,
			{ { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 3.0f } } });
		std::ostringstream capturedDatasetOutput;
		std::streambuf* previousOutput = std::cout.rdbuf(capturedDatasetOutput.rdbuf());
		WriteShapeRecognitionDatasetDiagnostics(formatterDiagnostics);
		std::cout.rdbuf(previousOutput);
		const std::string formattedDataset = capturedDatasetOutput.str();
		if (!Expect(formattedDataset.find("dpi_scale=1.5000") != std::string::npos &&
			formattedDataset.find("record=startup format_version=2") != std::string::npos &&
			formattedDataset.find("record=stroke") != std::string::npos &&
			formattedDataset.find("stroke_id=7:3") != std::string::npos &&
			formattedDataset.find("points_dip=[(1.0000:2.0000:3.0000)") !=
				std::string::npos &&
			formattedDataset.find("sampled_points_dip=[(1.0000:2.0000:3.0000)") !=
				std::string::npos &&
			formattedDataset.find("median_width_px=7.0000") != std::string::npos &&
			formattedDataset.find("median_width_dip=4.6667") != std::string::npos &&
			formattedDataset.find("replacement_width_px=0.0000") != std::string::npos &&
			formattedDataset.find("vision_bounds_px=[1.0000,2.0000,31.0000,42.0000]") !=
				std::string::npos &&
			formattedDataset.find("primary_reject_reason=shape_too_small") !=
				std::string::npos &&
			formattedDataset.find("failed_conditions=[shape_too_small]") !=
				std::string::npos,
			"dataset formatter preserves rejected-sample scale and geometry")) ++failures;

		if (!Expect(!ShapeRecognitionDiagnosticsEnabled(),
			"dataset diagnostics default disabled")) ++failures;
		SetShapeRecognitionDiagnosticsEnabled(true);
		if (!Expect(ShapeRecognitionDiagnosticsEnabled(),
			"dataset diagnostics can be enabled independently")) ++failures;
		SetShapeRecognitionDiagnosticsEnabled(false);
		if (!Expect(!ShapeRecognitionDiagnosticsEnabled(),
			"dataset diagnostics can be disabled independently")) ++failures;

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
