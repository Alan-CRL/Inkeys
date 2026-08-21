module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <utility>
#include <vector>

module Inkeys.Drawing.Draw3.shape_recognition;

import Inkeys.CV.ShapeRecognition;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr std::size_t kMaximumCandidateStrokeCount = 6;
		std::atomic<bool> shapeRecognitionDiagnosticsEnabled = false;

		bool SameInkGuid(const InkGuid& left, const InkGuid& right) noexcept
		{
			const auto& leftBytes = left.Bytes();
			const auto& rightBytes = right.Bytes();
			for (std::size_t index = 0; index < leftBytes.size(); ++index)
			{
				if (leftBytes[index] != rightBytes[index]) return false;
			}
			return true;
		}

		float MedianStrokeWidth(const InkStroke& stroke)
		{
			std::vector<float> widths;
			widths.reserve(stroke.Points().size());
			for (const StoredInkPoint& point : stroke.Points())
			{
				if (std::isfinite(point.width) && point.width > 0.0f)
					widths.push_back(point.width);
			}
			if (widths.empty()) return 0.0f;
			auto median = widths.begin() + widths.size() / 2;
			std::nth_element(widths.begin(), median, widths.end());
			return *median;
		}

		bool StylesMatch(const InkStroke& left, const InkStroke& right,
			float referenceWidth)
		{
			const StoredInkStyle& leftStyle = left.Style();
			const StoredInkStyle& rightStyle = right.Style();
			if (leftStyle.inkType != StoredInkType::Pen ||
				rightStyle.inkType != StoredInkType::Pen ||
				leftStyle.fallbackRgb != rightStyle.fallbackRgb ||
				leftStyle.opacity != rightStyle.opacity ||
				leftStyle.texture != rightStyle.texture) return false;
			const float width = MedianStrokeWidth(left);
			return std::isfinite(width) && width > 0.0f &&
				std::abs(width - referenceWidth) <=
				std::max(0.25f, referenceWidth * 0.08f);
		}

		void NormalizeTiles(std::vector<SignedTileCoordinate>& tiles)
		{
			std::sort(tiles.begin(), tiles.end());
			tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
		}

		void IncludeFootprint(StrokeTileFootprint& destination,
			const RenderItemState& source)
		{
			destination.pixelBounds.left = std::min(
				destination.pixelBounds.left, source.pixelBounds.left);
			destination.pixelBounds.top = std::min(
				destination.pixelBounds.top, source.pixelBounds.top);
			destination.pixelBounds.right = std::max(
				destination.pixelBounds.right, source.pixelBounds.right);
			destination.pixelBounds.bottom = std::max(
				destination.pixelBounds.bottom, source.pixelBounds.bottom);
			destination.undoTiles.insert(destination.undoTiles.end(),
				source.undoTiles.begin(), source.undoTiles.end());
			destination.compositionTiles.insert(destination.compositionTiles.end(),
				source.compositionTiles.begin(), source.compositionTiles.end());
		}
	}

	void SetShapeRecognitionDiagnosticsEnabled(bool enabled) noexcept
	{
		shapeRecognitionDiagnosticsEnabled.store(enabled, std::memory_order_release);
	}

	bool ShapeRecognitionDiagnosticsEnabled() noexcept
	{
		return shapeRecognitionDiagnosticsEnabled.load(std::memory_order_acquire);
	}

	const char* ShapeCandidateCollectionStopReasonName(
		ShapeCandidateCollectionStopReason reason) noexcept
	{
		switch (reason)
		{
		case ShapeCandidateCollectionStopReason::None: return "none";
		case ShapeCandidateCollectionStopReason::InvalidDpiScale: return "invalid_dpi_scale";
		case ShapeCandidateCollectionStopReason::NoActiveItem: return "no_active_item";
		case ShapeCandidateCollectionStopReason::MissingHistoryItem: return "missing_history_item";
		case ShapeCandidateCollectionStopReason::InactiveItem: return "inactive_item";
		case ShapeCandidateCollectionStopReason::EffectivelyHiddenItem: return "effectively_hidden_item";
		case ShapeCandidateCollectionStopReason::ConditionalItem: return "conditional_item";
		case ShapeCandidateCollectionStopReason::StrokeIndexOutOfRange: return "stroke_index_out_of_range";
		case ShapeCandidateCollectionStopReason::NonPenStroke: return "non_pen_stroke";
		case ShapeCandidateCollectionStopReason::InvalidWidth: return "invalid_width";
		case ShapeCandidateCollectionStopReason::StyleMismatch: return "style_mismatch";
		case ShapeCandidateCollectionStopReason::HiddenBranch: return "hidden_branch";
		case ShapeCandidateCollectionStopReason::HistoryStart: return "history_start";
		case ShapeCandidateCollectionStopReason::MaximumCandidateCount: return "maximum_candidate_count";
		default: return "unexpected_exception";
		}
	}

	void WriteShapeRecognitionDatasetDiagnostics(
		const ShapeRecognitionDatasetDiagnostics& diagnostics) noexcept
	{
		try
		{
			std::ostringstream output;
			output << std::fixed << std::setprecision(4) << std::boolalpha;
			output << "[ShapeRecognitionDataset] begin collected_strokes=" <<
				diagnostics.collectedStrokeCount << " collected_points=" <<
				diagnostics.collectedPointCount << " collection_stop=" <<
				ShapeCandidateCollectionStopReasonName(diagnostics.collectionStopReason) << '\n';
			for (std::size_t index = 0; index < diagnostics.attempts.size(); ++index)
			{
				const ShapeRecognitionAttemptDiagnostics& attempt = diagnostics.attempts[index];
				const CV::ShapeRecognitionDiagnostics& vision = attempt.vision;
				const CV::ShapeRecognitionThresholds& threshold = vision.thresholds;
				const char* outcome = "vision_rejected";
				switch (attempt.outcome)
				{
				case ShapeRecognitionAttemptOutcome::Accepted: outcome = "accepted"; break;
				case ShapeRecognitionAttemptOutcome::MissingReplacementWidth:
					outcome = "missing_replacement_width"; break;
				case ShapeRecognitionAttemptOutcome::InvalidReplacement:
					outcome = "invalid_replacement"; break;
				case ShapeRecognitionAttemptOutcome::FootprintFailed:
					outcome = "footprint_failed"; break;
				case ShapeRecognitionAttemptOutcome::UnexpectedException:
					outcome = "unexpected_exception"; break;
				default: break;
				}
				output << "[ShapeRecognitionDataset] attempt=" << index <<
					" strokes=" << attempt.strokeCount << " source_points=" <<
					attempt.sourcePointCount << " sampled_points=" << vision.sampledPointCount <<
					" bounds=[" << attempt.bounds.left << ',' << attempt.bounds.top << ',' <<
					attempt.bounds.right << ',' << attempt.bounds.bottom << "] style_rgb=0x" <<
					std::hex << attempt.style.fallbackRgb << std::dec << " opacity=" <<
					attempt.style.opacity << " texture=" << attempt.style.texture <<
					" dpi_scale=" << vision.dpiScale << " median_width=" << vision.medianWidth <<
					" replacement_width=" << attempt.representativeWidth << '\n';
				output << "[ShapeRecognitionDataset] geometry hull_points=" << vision.hullPointCount <<
					" quad_corners=" << vision.approximatedCornerCount << " hull_area=" <<
					vision.hullArea << " hull_perimeter=" << vision.hullPerimeter <<
					" vision_bounds=[" << vision.bounds.left << ',' << vision.bounds.top << ',' <<
					vision.bounds.right << ',' << vision.bounds.bottom << ']' <<
					" short_side=" << vision.shortSide << " minimum_short_side=" <<
					vision.minimumShortSide << " long_side=" << vision.longSide <<
					" aspect_ratio=" << vision.aspectRatio << " rectangularity=" <<
					vision.rectangularity << " source_path=" << vision.sourcePathLength <<
					" rectangle_perimeter=" << vision.rectanglePerimeter << " path_ratio=" <<
					vision.pathPerimeterRatio << " edge_band=" << vision.edgeBand << '\n';
				output << "[ShapeRecognitionDataset] fit edge_coverage=[" <<
					vision.edgeCoverage[0] << ',' << vision.edgeCoverage[1] << ',' <<
					vision.edgeCoverage[2] << ',' << vision.edgeCoverage[3] <<
					"] total_coverage=" << vision.totalCoverage << " minimum_stroke_on_edge=" <<
					vision.minimumStrokeOnEdgeRatio << " off_edge_ratio=" << vision.offEdgeRatio <<
					" max_axis_deviation_deg=" << vision.maximumAxisDeviationDegrees <<
					" weighted_axis_deviation_deg=" << vision.weightedAxisDeviationDegrees <<
					" worst_corner_score=" << vision.worstCornerScore <<
					" confidence=" << vision.confidence << '\n';
				output << "[ShapeRecognitionDataset] thresholds max_strokes=" <<
					threshold.maximumStrokeCount << " max_points_per_stroke=" <<
					threshold.maximumPointsPerStroke << " max_sampled_points=" <<
					threshold.maximumTotalSampledPoints << " min_short_dip=" <<
					threshold.minimumShortSideDip << " min_short_width_multiple=" <<
					threshold.minimumShortSideWidthMultiple << " max_aspect=" <<
					threshold.maximumAspectRatio << " min_rectangularity=" <<
					threshold.minimumRectangularity << " max_axis_deg=" <<
					threshold.maximumAxisDeviationDegrees << " max_avg_axis_deg=" <<
					threshold.maximumAverageAxisDeviationDegrees << " min_edge_coverage=" <<
					threshold.minimumEdgeCoverage << " min_total_coverage=" <<
					threshold.minimumTotalCoverage << " min_stroke_on_edge=" <<
					threshold.minimumStrokeOnEdgeRatio << " max_off_edge=" <<
					threshold.maximumOffEdgeRatio << " path_ratio=[" <<
					threshold.minimumPathPerimeterRatio << ',' <<
					threshold.maximumPathPerimeterRatio << "] min_confidence=" <<
					threshold.minimumConfidence << '\n';
				output << "[ShapeRecognitionDataset] outcome=" << outcome << " accepted=" <<
					vision.accepted << " reject_reason=" <<
					CV::ShapeRecognitionRejectReasonName(vision.rejectionReason) << '\n';
			}
			output << "[ShapeRecognitionDataset] end accepted=" << diagnostics.accepted <<
				" accepted_strokes=" << diagnostics.acceptedStrokeCount << '\n';
			std::cout << output.str();
		}
		catch (...)
		{
			// 数据集诊断失败不能中断 Draw3 绘制线程。
		}
	}

	ShapeCorrectionPlan::ShapeCorrectionPlan(std::vector<RenderItemId> sources,
		InkStroke stroke, StrokeTileFootprint replacementFootprint,
		float score) noexcept
		: sourceItems(std::move(sources)), replacement(std::move(stroke)),
		footprint(std::move(replacementFootprint)), confidence(score) {}

	bool CanAttemptShapeRecognition(ShapeRecognitionIdleState state) noexcept
	{
		return !state.hasDrawingContact && !state.hasGestureContact &&
			!state.hasReconnectCandidate && !state.navigationActive &&
			!state.inputPending;
	}

	void ShapeRecognitionTriggerLatch::Arm(
		InkGuid pageGuid, std::uint64_t historyRevision) noexcept
	{
		pageGuid_ = pageGuid;
		historyRevision_ = historyRevision;
		pending_ = !pageGuid.IsZero();
	}

	void ShapeRecognitionTriggerLatch::Invalidate() noexcept
	{
		pending_ = false;
		pageGuid_ = {};
		historyRevision_ = 0;
	}

	bool ShapeRecognitionTriggerLatch::Pending() const noexcept
	{
		return pending_;
	}

	bool ShapeRecognitionTriggerLatch::ConsumeIfReady(
		InkGuid pageGuid, std::uint64_t historyRevision,
		ShapeRecognitionIdleState state) noexcept
	{
		if (!pending_) return false;
		if (!SameInkGuid(pageGuid_, pageGuid) || historyRevision_ != historyRevision)
		{
			Invalidate();
			return false;
		}
		if (!CanAttemptShapeRecognition(state)) return false;
		pending_ = false;
		return true;
	}

	std::optional<ShapeCorrectionPlan> BuildShapeCorrectionPlan(
		const InkCanvas& canvas, const CanvasRuntimeHistory& history,
		float dpiScale, ShapeRecognitionDatasetDiagnostics* diagnostics) noexcept
	{
		if (diagnostics) *diagnostics = {};
		try
		{
			if (!std::isfinite(dpiScale) || dpiScale <= 0.0f)
			{
				if (diagnostics) diagnostics->collectionStopReason =
					ShapeCandidateCollectionStopReason::InvalidDpiScale;
				return std::nullopt;
			}
			const std::span<const InkStroke> strokes = canvas.Strokes();
			std::vector<const RenderItemState*> candidates;
			candidates.reserve(kMaximumCandidateStrokeCount);
			std::optional<RenderItemId> currentId = history.LastVisibleItem();
			float referenceWidth = 0.0f;
			const InkStroke* referenceStroke = nullptr;
			if (!currentId && diagnostics) diagnostics->collectionStopReason =
				ShapeCandidateCollectionStopReason::NoActiveItem;
			while (currentId && candidates.size() < kMaximumCandidateStrokeCount)
			{
				const RenderItemState* item = history.Find(*currentId);
				if (!item)
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::MissingHistoryItem;
					break;
				}
				if (!item->active)
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::InactiveItem;
					break;
				}
				if (!item->visible)
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::EffectivelyHiddenItem;
					break;
				}
				if (item->renderOnlyWhenLatest)
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::ConditionalItem;
					break;
				}
				if (item->strokeIndex >= strokes.size())
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::StrokeIndexOutOfRange;
					break;
				}
				const InkStroke& stroke = strokes[item->strokeIndex];
				if (stroke.Style().inkType != StoredInkType::Pen ||
					stroke.RenderOnlyWhenLatest())
				{
					if (diagnostics) diagnostics->collectionStopReason =
						stroke.RenderOnlyWhenLatest()
						? ShapeCandidateCollectionStopReason::ConditionalItem
						: ShapeCandidateCollectionStopReason::NonPenStroke;
					break;
				}
				if (!referenceStroke)
				{
					referenceStroke = &stroke;
					referenceWidth = MedianStrokeWidth(stroke);
					if (!std::isfinite(referenceWidth) || referenceWidth <= 0.0f)
					{
						if (diagnostics) diagnostics->collectionStopReason =
							ShapeCandidateCollectionStopReason::InvalidWidth;
						break;
					}
				}
				else if (!StylesMatch(stroke, *referenceStroke, referenceWidth))
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::StyleMismatch;
					break;
				}
				candidates.push_back(item);
				if (diagnostics)
				{
					++diagnostics->collectedStrokeCount;
					diagnostics->collectedPointCount += stroke.Points().size();
				}
				if (!item->previousVisibleIndex)
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::HistoryStart;
					break;
				}
				// inactive redo 项位于中间时属于隐藏分支，不能跨过它拼接候选。
				if (*item->previousVisibleIndex + 1 != item->id.index)
				{
					if (diagnostics) diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::HiddenBranch;
					break;
				}
				currentId = history.Items()[*item->previousVisibleIndex].id;
			}
			if (diagnostics && diagnostics->collectionStopReason ==
				ShapeCandidateCollectionStopReason::None &&
				candidates.size() == kMaximumCandidateStrokeCount && currentId)
				diagnostics->collectionStopReason =
					ShapeCandidateCollectionStopReason::MaximumCandidateCount;
			if (candidates.empty() || !referenceStroke) return std::nullopt;
			std::reverse(candidates.begin(), candidates.end());

			for (std::size_t candidateCount = candidates.size();
				candidateCount > 0; --candidateCount)
			{
				const std::size_t beginIndex = candidates.size() - candidateCount;
				ShapeRecognitionAttemptDiagnostics* attempt = nullptr;
				if (diagnostics)
				{
					diagnostics->attempts.emplace_back();
					attempt = &diagnostics->attempts.back();
					attempt->strokeCount = candidateCount;
					attempt->style = strokes[candidates.back()->strokeIndex].Style();
					attempt->bounds = candidates[beginIndex]->pixelBounds;
				}
				std::vector<std::vector<CV::InkPoint>> ownedPoints(candidateCount);
				std::vector<CV::InkStrokeView> views;
				views.reserve(candidateCount);
				std::vector<float> replacementWidths;
				for (std::size_t index = 0; index < candidateCount; ++index)
				{
					const RenderItemState& item = *candidates[beginIndex + index];
					const InkStroke& stroke = strokes[item.strokeIndex];
					if (attempt)
					{
						attempt->sourcePointCount += stroke.Points().size();
						attempt->bounds.left = std::min(
							attempt->bounds.left, item.pixelBounds.left);
						attempt->bounds.top = std::min(
							attempt->bounds.top, item.pixelBounds.top);
						attempt->bounds.right = std::max(
							attempt->bounds.right, item.pixelBounds.right);
						attempt->bounds.bottom = std::max(
							attempt->bounds.bottom, item.pixelBounds.bottom);
					}
					ownedPoints[index].reserve(stroke.Points().size());
					for (const StoredInkPoint& point : stroke.Points())
					{
						ownedPoints[index].push_back({ point.x, point.y, point.width });
						if (point.width > 0.0f) replacementWidths.push_back(point.width);
					}
					views.push_back({ ownedPoints[index] });
				}
				CV::ShapeRecognitionDiagnostics* visionDiagnostics =
					attempt ? &attempt->vision : nullptr;
				const CV::ShapeResult recognized = CV::RecognizeInkShape(
					views, dpiScale, visionDiagnostics);
				if (recognized.type != CV::ShapeType::AxisAlignedRectangle)
				{
					if (attempt) attempt->outcome =
						ShapeRecognitionAttemptOutcome::VisionRejected;
					continue;
				}
				if (replacementWidths.empty())
				{
					if (attempt) attempt->outcome =
						ShapeRecognitionAttemptOutcome::MissingReplacementWidth;
					continue;
				}

				auto median = replacementWidths.begin() + replacementWidths.size() / 2;
				std::nth_element(replacementWidths.begin(), median, replacementWidths.end());
				const float replacementWidth = *median;
				if (attempt) attempt->representativeWidth = replacementWidth;
				StoredInkStyle replacementStyle =
					strokes[candidates.back()->strokeIndex].Style();
				replacementStyle.inkType = StoredInkType::OutlineRectangle;
				std::vector<StoredInkPoint> rectanglePoints = {
					{ recognized.rectangle.left, recognized.rectangle.top, replacementWidth },
					{ recognized.rectangle.right, recognized.rectangle.bottom, replacementWidth }
				};
				InkStroke replacement(replacementStyle, std::move(rectanglePoints));
				if (!replacement.IsValid())
				{
					if (attempt) attempt->outcome =
						ShapeRecognitionAttemptOutcome::InvalidReplacement;
					continue;
				}
				std::optional<StrokeTileFootprint> footprint =
					BuildStrokeTileFootprint(replacement);
				if (!footprint)
				{
					if (attempt) attempt->outcome =
						ShapeRecognitionAttemptOutcome::FootprintFailed;
					continue;
				}

				std::vector<RenderItemId> sourceItems;
				sourceItems.reserve(candidateCount);
				for (std::size_t index = beginIndex; index < candidates.size(); ++index)
				{
					sourceItems.push_back(candidates[index]->id);
					IncludeFootprint(*footprint, *candidates[index]);
				}
				NormalizeTiles(footprint->undoTiles);
				NormalizeTiles(footprint->compositionTiles);
				if (attempt)
				{
					attempt->outcome = ShapeRecognitionAttemptOutcome::Accepted;
					diagnostics->accepted = true;
					diagnostics->acceptedStrokeCount = candidateCount;
				}
				return ShapeCorrectionPlan(std::move(sourceItems),
					std::move(replacement), std::move(*footprint), recognized.confidence);
			}
			return std::nullopt;
		}
		catch (...)
		{
			if (diagnostics)
			{
				if (diagnostics->attempts.empty())
					diagnostics->collectionStopReason =
						ShapeCandidateCollectionStopReason::UnexpectedException;
				else
					diagnostics->attempts.back().outcome =
						ShapeRecognitionAttemptOutcome::UnexpectedException;
			}
			return std::nullopt;
		}
	}
}
