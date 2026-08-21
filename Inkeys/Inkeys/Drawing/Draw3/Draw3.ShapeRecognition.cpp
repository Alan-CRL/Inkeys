module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

module Inkeys.Drawing.Draw3.shape_recognition;

import Inkeys.CV.ShapeRecognition;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr std::size_t kMaximumCandidateStrokeCount = 6;
		constexpr std::size_t kMaximumLoggedPathPointsPerStroke = 1024;
		std::atomic<bool> shapeRecognitionDiagnosticsEnabled = false;
		std::atomic<std::uint64_t> nextShapeRecognitionPenUpId = 1;
		std::mutex shapeRecognitionConsoleWriteMutex;

		const std::string& ShapeRecognitionSessionId()
		{
			static const std::string value = []
			{
				FILETIME fileTime = {};
				LARGE_INTEGER qpc = {};
				GetSystemTimeAsFileTime(&fileTime);
				QueryPerformanceCounter(&qpc);
				ULARGE_INTEGER timestamp = {};
				timestamp.LowPart = fileTime.dwLowDateTime;
				timestamp.HighPart = fileTime.dwHighDateTime;
				std::ostringstream text;
				text << std::hex << GetCurrentProcessId() << '-' << timestamp.QuadPart << '-'
					<< static_cast<std::uint64_t>(qpc.QuadPart);
				return text.str();
			}();
			return value;
		}

		std::int64_t CurrentQpc() noexcept
		{
			LARGE_INTEGER qpc = {};
			return QueryPerformanceCounter(&qpc) ? qpc.QuadPart : 0;
		}

		void WriteShapeRecognitionStartupMarker() noexcept
		{
			static std::once_flag once;
			try
			{
				std::call_once(once, []
				{
					FILETIME fileTime = {};
					GetSystemTimeAsFileTime(&fileTime);
					ULARGE_INTEGER timestamp = {};
					timestamp.LowPart = fileTime.dwLowDateTime;
					timestamp.HighPart = fileTime.dwHighDateTime;
					std::ostringstream output;
					output << "[ShapeRecognitionDataset] record=startup format_version=2 session_id="
						<< ShapeRecognitionSessionId() << " pid=" << GetCurrentProcessId()
						<< " start_filetime_utc=" << timestamp.QuadPart
						<< " qpc=" << CurrentQpc() << "\n";
					WriteShapeRecognitionConsoleText(output.str());
				});
			}
			catch (...)
			{
				// 启动标识失败不改变诊断或绘制状态。
			}
		}

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

		bool StylesMatch(const InkStroke& left, const InkStroke& right) noexcept
		{
			const StoredInkStyle& leftStyle = left.Style();
			const StoredInkStyle& rightStyle = right.Style();
			return leftStyle.inkType == StoredInkType::Pen &&
				rightStyle.inkType == StoredInkType::Pen &&
				leftStyle.fallbackRgb == rightStyle.fallbackRgb &&
				leftStyle.opacity == rightStyle.opacity &&
				leftStyle.texture == rightStyle.texture;
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

		ShapeRecognitionStrokePathDiagnostics BuildLoggedStrokePath(
			const RenderItemState& item, const InkStroke& stroke, float dpiScale)
		{
			ShapeRecognitionStrokePathDiagnostics result;
			result.itemId = item.id;
			result.strokeIndex = item.strokeIndex;
			result.sourcePointCount = stroke.Points().size();
			const std::size_t loggedCount = std::min(
				stroke.Points().size(), kMaximumLoggedPathPointsPerStroke);
			result.pathTruncated = loggedCount != stroke.Points().size();
			result.pointsDip.reserve(loggedCount);
			for (std::size_t index = 0; index < loggedCount; ++index)
			{
				const std::size_t sourceIndex = loggedCount > 1
					? index * (stroke.Points().size() - 1) / (loggedCount - 1) : 0;
				const StoredInkPoint& point = stroke.Points()[sourceIndex];
				result.pointsDip.push_back({
					point.x / dpiScale, point.y / dpiScale, point.width / dpiScale });
			}
			return result;
		}
	}

	void WriteShapeRecognitionConsoleText(std::string_view text) noexcept
	{
		if (text.empty()) return;
		try
		{
			std::scoped_lock lock(shapeRecognitionConsoleWriteMutex);
			std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
			std::cout.flush();
		}
		catch (...)
		{
			// 诊断输出失败不能影响 RTS 或绘制线程。
		}
	}

	void SetShapeRecognitionDiagnosticsEnabled(bool enabled) noexcept
	{
		shapeRecognitionDiagnosticsEnabled.store(enabled, std::memory_order_release);
		if (enabled) WriteShapeRecognitionStartupMarker();
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
			WriteShapeRecognitionStartupMarker();
			const std::uint64_t penUpId = nextShapeRecognitionPenUpId.fetch_add(
				1, std::memory_order_relaxed);
			const std::string& sessionId = ShapeRecognitionSessionId();
			const std::int64_t qpc = CurrentQpc();
			std::ostringstream output;
			output << std::fixed << std::setprecision(4) << std::boolalpha;
			output << "[ShapeRecognitionDataset] record=pen_up_begin format_version=2 session_id=" <<
				sessionId << " pen_up_id=" << penUpId << " qpc=" << qpc <<
				" dpi_scale=" << diagnostics.dpiScale << " collected_strokes=" <<
				diagnostics.collectedStrokeCount << " collected_points=" <<
				diagnostics.collectedPointCount << " collection_stop=" <<
				ShapeCandidateCollectionStopReasonName(diagnostics.collectionStopReason) << '\n';
			for (const ShapeRecognitionStrokePathDiagnostics& stroke : diagnostics.strokes)
			{
				output << "[ShapeRecognitionDataset] record=stroke session_id=" << sessionId <<
					" pen_up_id=" << penUpId << " stroke_id=" << stroke.itemId.generation << ':' <<
					stroke.itemId.index << " stroke_index=" << stroke.strokeIndex <<
					" source_points=" << stroke.sourcePointCount << " logged_points=" <<
					stroke.pointsDip.size() << " path_truncated=" << stroke.pathTruncated <<
					" points_dip=[";
				for (std::size_t pointIndex = 0; pointIndex < stroke.pointsDip.size(); ++pointIndex)
				{
					const CV::InkPoint& point = stroke.pointsDip[pointIndex];
					if (pointIndex != 0) output << ',';
					output << '(' << point.x << ':' << point.y << ':' << point.width << ')';
				}
				output << "]\n";
			}
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
				const auto toDip = [&](float pixels) noexcept
				{
					return std::isfinite(vision.dpiScale) && vision.dpiScale > 0.0f
						? pixels / vision.dpiScale
						: (std::numeric_limits<float>::quiet_NaN)();
				};
				output << "[ShapeRecognitionDataset] record=attempt session_id=" << sessionId <<
					" pen_up_id=" << penUpId << " candidate_id=" << index <<
					" strokes=" << attempt.strokeCount << " source_points=" <<
					attempt.sourcePointCount << " sampled_points=" << vision.sampledPointCount <<
					" bounds_px=[" << attempt.bounds.left << ',' << attempt.bounds.top << ',' <<
					attempt.bounds.right << ',' << attempt.bounds.bottom << "] bounds_dip=[" <<
					toDip(static_cast<float>(attempt.bounds.left)) << ',' <<
					toDip(static_cast<float>(attempt.bounds.top)) << ',' <<
					toDip(static_cast<float>(attempt.bounds.right)) << ',' <<
					toDip(static_cast<float>(attempt.bounds.bottom)) << "] stroke_ids=[";
				for (std::size_t itemIndex = 0; itemIndex < attempt.sourceItems.size(); ++itemIndex)
				{
					if (itemIndex != 0) output << ',';
					output << attempt.sourceItems[itemIndex].generation << ':' <<
						attempt.sourceItems[itemIndex].index;
				}
				output << "] style_rgb=0x" <<
					std::hex << attempt.style.fallbackRgb << std::dec << " opacity=" <<
					attempt.style.opacity << " texture=" << attempt.style.texture <<
					" dpi_scale=" << vision.dpiScale << " median_width_px=" <<
					vision.medianWidth << " median_width_dip=" << toDip(vision.medianWidth) <<
					" replacement_width_px=" << attempt.representativeWidth <<
					" replacement_width_dip=" << toDip(attempt.representativeWidth) << '\n';
				output << "[ShapeRecognitionDataset] record=geometry session_id=" << sessionId <<
					" pen_up_id=" << penUpId << " candidate_id=" << index <<
					" hull_points=" << vision.hullPointCount <<
					" quad_corners=" << vision.approximatedCornerCount <<
					" quad_corner_counts=[" << vision.approximationCornerCounts[0] << ',' <<
					vision.approximationCornerCounts[1] << ',' <<
					vision.approximationCornerCounts[2] << ',' <<
					vision.approximationCornerCounts[3] << "] quad_epsilon_ratio=" <<
					vision.selectedApproximationEpsilonRatio <<
					" quad_corner_distance_px=" << vision.maximumQuadCornerDistance <<
					" quad_corner_distance_ratio=" << vision.maximumQuadCornerDistanceRatio <<
					" selected_quad_px=[";
				for (std::size_t corner = 0; corner < vision.selectedQuadCornerCount; ++corner)
				{
					if (corner != 0) output << ',';
					output << '(' << vision.selectedQuad[corner].x << ':' <<
						vision.selectedQuad[corner].y << ')';
				}
				output << "] selected_quad_dip=[";
				for (std::size_t corner = 0; corner < vision.selectedQuadCornerCount; ++corner)
				{
					if (corner != 0) output << ',';
					output << '(' << toDip(vision.selectedQuad[corner].x) << ':' <<
						toDip(vision.selectedQuad[corner].y) << ')';
				}
				output << "] hull_area_px2=" << vision.hullArea <<
					" hull_perimeter_px=" << vision.hullPerimeter <<
					" vision_bounds_px=[" << vision.bounds.left << ',' << vision.bounds.top << ',' <<
					vision.bounds.right << ',' << vision.bounds.bottom << ']' <<
					" short_side_px=" << vision.shortSide << " short_side_dip=" <<
					toDip(vision.shortSide) << " minimum_short_side_px=" <<
					vision.minimumShortSide << " minimum_short_side_dip=" <<
					toDip(vision.minimumShortSide) << " long_side_px=" << vision.longSide <<
					" long_side_dip=" << toDip(vision.longSide) <<
					" aspect_ratio=" << vision.aspectRatio << " rectangularity=" <<
					vision.rectangularity << " source_path_px=" << vision.sourcePathLength <<
					" source_path_dip=" << toDip(vision.sourcePathLength) <<
					" rectangle_perimeter_px=" << vision.rectanglePerimeter <<
					" rectangle_perimeter_dip=" << toDip(vision.rectanglePerimeter) << " path_ratio=" <<
					vision.pathPerimeterRatio << " edge_band_px=" << vision.edgeBand <<
					" edge_band_dip=" << vision.edgeBandDip << '\n';
				output << "[ShapeRecognitionDataset] record=fit session_id=" << sessionId <<
					" pen_up_id=" << penUpId << " candidate_id=" << index <<
					" edge_coverage=[" <<
					vision.edgeCoverage[0] << ',' << vision.edgeCoverage[1] << ',' <<
					vision.edgeCoverage[2] << ',' << vision.edgeCoverage[3] <<
					"] total_coverage=" << vision.totalCoverage << " minimum_stroke_on_edge=" <<
					vision.minimumStrokeOnEdgeRatio << " off_edge_ratio=" << vision.offEdgeRatio <<
					" max_axis_deviation_deg=" << vision.maximumAxisDeviationDegrees <<
					" weighted_axis_deviation_deg=" << vision.weightedAxisDeviationDegrees <<
					" worst_corner_score=" << vision.worstCornerScore <<
					" confidence=" << vision.confidence << '\n';
				for (std::size_t edge = 0; edge < 4; ++edge)
				{
					output << "[ShapeRecognitionDataset] record=edge session_id=" << sessionId <<
						" pen_up_id=" << penUpId << " candidate_id=" << index << " edge=" << edge <<
						" coverage=" << vision.edgeCoverage[edge] << " covered_px=" <<
						vision.edgeCoveredLength[edge] << " covered_dip=" <<
						toDip(vision.edgeCoveredLength[edge]) << " largest_gap_px=" <<
						vision.edgeLargestGap[edge] << " largest_gap_dip=" <<
						toDip(vision.edgeLargestGap[edge]) << " residual_p50_px=" <<
						vision.edgeDistanceP50[edge] << " residual_p50_dip=" <<
						toDip(vision.edgeDistanceP50[edge]) << " residual_p90_px=" <<
						vision.edgeDistanceP90[edge] << " residual_p90_dip=" <<
						toDip(vision.edgeDistanceP90[edge]) << " residual_p95_px=" <<
						vision.edgeDistanceP95[edge] << " residual_p95_dip=" <<
						toDip(vision.edgeDistanceP95[edge]) << " fit_axis_deviation_deg=" <<
						vision.edgeFitAxisDeviationDegrees[edge] <<
						" segment_axis_deviation_deg=" <<
						vision.edgeSegmentAxisDeviationDegrees[edge] << '\n';
				}
				for (std::size_t strokeIndex = 0;
					strokeIndex < vision.inputStrokeCount && strokeIndex < vision.strokes.size();
					++strokeIndex)
				{
					const CV::ShapeRecognitionStrokeDiagnostics& stroke = vision.strokes[strokeIndex];
					output << "[ShapeRecognitionDataset] record=stroke_fit session_id=" << sessionId <<
						" pen_up_id=" << penUpId << " candidate_id=" << index <<
						" candidate_stroke_index=" << strokeIndex;
					if (strokeIndex < attempt.sourceItems.size())
						output << " stroke_id=" << attempt.sourceItems[strokeIndex].generation << ':' <<
							attempt.sourceItems[strokeIndex].index;
					output << " source_points=" << stroke.sourcePointCount << " sampled_points=" <<
						stroke.sampledPointCount << " path_px=" << stroke.pathLength << " path_dip=" <<
						toDip(stroke.pathLength) << " on_edge_ratio=" << stroke.onEdgeRatio <<
						" edge_contribution_px=[" << stroke.edgeContributionLength[0] << ',' <<
						stroke.edgeContributionLength[1] << ',' << stroke.edgeContributionLength[2] <<
						',' << stroke.edgeContributionLength[3] << "] edge_points=[" <<
						stroke.edgePointCount[0] << ',' << stroke.edgePointCount[1] << ',' <<
						stroke.edgePointCount[2] << ',' << stroke.edgePointCount[3] <<
						"] sampled_points_dip=[";
					for (std::size_t pointIndex = 0;
						pointIndex < stroke.sampledPoints.size(); ++pointIndex)
					{
						if (pointIndex != 0) output << ',';
						const CV::InkPoint& point = stroke.sampledPoints[pointIndex];
						output << '(' << toDip(point.x) << ':' << toDip(point.y) << ':' <<
							toDip(point.width) << ')';
					}
					output << "]\n";
				}
				output << "[ShapeRecognitionDataset] record=thresholds session_id=" << sessionId <<
					" pen_up_id=" << penUpId << " candidate_id=" << index << " max_strokes=" <<
					threshold.maximumStrokeCount << " max_points_per_stroke=" <<
					threshold.maximumPointsPerStroke << " max_sampled_points=" <<
					threshold.maximumTotalSampledPoints << " resample_spacing_dip=" <<
					threshold.resampleSpacingDip << " min_short_dip=" <<
					threshold.minimumShortSideDip << " min_short_width_multiple=" <<
					threshold.minimumShortSideWidthMultiple << " max_aspect=" <<
					threshold.maximumAspectRatio << " min_rectangularity=" <<
					threshold.minimumRectangularity << " quad_epsilon_ratios=[" <<
					threshold.approximationEpsilonRatios[0] << ',' <<
					threshold.approximationEpsilonRatios[1] << ',' <<
					threshold.approximationEpsilonRatios[2] << ',' <<
					threshold.approximationEpsilonRatios[3] << "] max_quad_corner_ratio=" <<
					threshold.maximumQuadCornerDistanceRatio << " edge_band_dip=[" <<
					threshold.minimumEdgeBandDip << ',' << threshold.maximumEdgeBandDip <<
					"] edge_band_short_ratio=" << threshold.edgeBandShortSideRatio <<
					" edge_band_width_multiple=" << threshold.edgeBandWidthMultiple <<
					" max_axis_deg=" <<
					threshold.maximumAxisDeviationDegrees << " max_avg_axis_deg=" <<
					threshold.maximumAverageAxisDeviationDegrees << " min_edge_coverage=" <<
					threshold.minimumEdgeCoverage << " min_total_coverage=" <<
					threshold.minimumTotalCoverage << " min_stroke_on_edge=" <<
					threshold.minimumStrokeOnEdgeRatio << " max_off_edge=" <<
					threshold.maximumOffEdgeRatio << " path_ratio=[" <<
					threshold.minimumPathPerimeterRatio << ',' <<
					threshold.maximumPathPerimeterRatio << "] min_confidence=" <<
					threshold.minimumConfidence << '\n';
				output << "[ShapeRecognitionDataset] record=outcome session_id=" << sessionId <<
					" pen_up_id=" << penUpId << " candidate_id=" << index << " outcome=" <<
					outcome << " accepted=" << vision.accepted << " reject_reason=" <<
					CV::ShapeRecognitionRejectReasonName(vision.rejectionReason) <<
					" primary_reject_reason=" <<
					CV::ShapeRecognitionRejectReasonName(vision.rejectionReason) <<
					" failed_conditions=[";
				for (std::size_t failure = 0; failure < vision.failedConditionCount; ++failure)
				{
					if (failure != 0) output << ',';
					output << CV::ShapeRecognitionRejectReasonName(vision.failedConditions[failure]);
				}
				output << "]\n";
			}
			output << "[ShapeRecognitionDataset] record=pen_up_end session_id=" << sessionId <<
				" pen_up_id=" << penUpId << " accepted=" << diagnostics.accepted <<
				" accepted_strokes=" << diagnostics.acceptedStrokeCount << '\n';
			WriteShapeRecognitionConsoleText(output.str());
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
		if (diagnostics)
		{
			*diagnostics = {};
			diagnostics->dpiScale = dpiScale;
		}
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
				else if (!StylesMatch(stroke, *referenceStroke))
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
			if (diagnostics)
			{
				diagnostics->strokes.reserve(candidates.size());
				for (const RenderItemState* item : candidates)
					diagnostics->strokes.push_back(BuildLoggedStrokePath(
						*item, strokes[item->strokeIndex], dpiScale));
			}

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
					attempt->sourceItems.reserve(candidateCount);
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
						attempt->sourceItems.push_back(item.id);
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
