module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
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
		float dpiScale) noexcept
	{
		try
		{
			if (!std::isfinite(dpiScale) || dpiScale <= 0.0f) return std::nullopt;
			const std::span<const InkStroke> strokes = canvas.Strokes();
			std::vector<const RenderItemState*> candidates;
			candidates.reserve(kMaximumCandidateStrokeCount);
			std::optional<RenderItemId> currentId = history.LastVisibleItem();
			float referenceWidth = 0.0f;
			const InkStroke* referenceStroke = nullptr;
			while (currentId && candidates.size() < kMaximumCandidateStrokeCount)
			{
				const RenderItemState* item = history.Find(*currentId);
				if (!item || !item->active || !item->visible ||
					item->renderOnlyWhenLatest || item->strokeIndex >= strokes.size()) break;
				const InkStroke& stroke = strokes[item->strokeIndex];
				if (stroke.Style().inkType != StoredInkType::Pen ||
					stroke.RenderOnlyWhenLatest()) break;
				if (!referenceStroke)
				{
					referenceStroke = &stroke;
					referenceWidth = MedianStrokeWidth(stroke);
					if (!std::isfinite(referenceWidth) || referenceWidth <= 0.0f) break;
				}
				else if (!StylesMatch(stroke, *referenceStroke, referenceWidth)) break;
				candidates.push_back(item);
				if (!item->previousVisibleIndex) break;
				// inactive redo 项位于中间时属于隐藏分支，不能跨过它拼接候选。
				if (*item->previousVisibleIndex + 1 != item->id.index) break;
				currentId = history.Items()[*item->previousVisibleIndex].id;
			}
			if (candidates.empty() || !referenceStroke) return std::nullopt;
			std::reverse(candidates.begin(), candidates.end());

			for (std::size_t candidateCount = candidates.size();
				candidateCount > 0; --candidateCount)
			{
				const std::size_t beginIndex = candidates.size() - candidateCount;
				std::vector<std::vector<CV::InkPoint>> ownedPoints(candidateCount);
				std::vector<CV::InkStrokeView> views;
				views.reserve(candidateCount);
				std::vector<float> replacementWidths;
				for (std::size_t index = 0; index < candidateCount; ++index)
				{
					const RenderItemState& item = *candidates[beginIndex + index];
					const InkStroke& stroke = strokes[item.strokeIndex];
					ownedPoints[index].reserve(stroke.Points().size());
					for (const StoredInkPoint& point : stroke.Points())
					{
						ownedPoints[index].push_back({ point.x, point.y, point.width });
						if (point.width > 0.0f) replacementWidths.push_back(point.width);
					}
					views.push_back({ ownedPoints[index] });
				}
				const CV::ShapeResult recognized = CV::RecognizeInkShape(views, dpiScale);
				if (recognized.type != CV::ShapeType::AxisAlignedRectangle ||
					replacementWidths.empty()) continue;

				auto median = replacementWidths.begin() + replacementWidths.size() / 2;
				std::nth_element(replacementWidths.begin(), median, replacementWidths.end());
				const float replacementWidth = *median;
				StoredInkStyle replacementStyle =
					strokes[candidates.back()->strokeIndex].Style();
				replacementStyle.inkType = StoredInkType::OutlineRectangle;
				std::vector<StoredInkPoint> rectanglePoints = {
					{ recognized.rectangle.left, recognized.rectangle.top, replacementWidth },
					{ recognized.rectangle.right, recognized.rectangle.bottom, replacementWidth }
				};
				InkStroke replacement(replacementStyle, std::move(rectanglePoints));
				std::optional<StrokeTileFootprint> footprint =
					BuildStrokeTileFootprint(replacement);
				if (!replacement.IsValid() || !footprint) continue;

				std::vector<RenderItemId> sourceItems;
				sourceItems.reserve(candidateCount);
				for (std::size_t index = beginIndex; index < candidates.size(); ++index)
				{
					sourceItems.push_back(candidates[index]->id);
					IncludeFootprint(*footprint, *candidates[index]);
				}
				NormalizeTiles(footprint->undoTiles);
				NormalizeTiles(footprint->compositionTiles);
				return ShapeCorrectionPlan(std::move(sourceItems),
					std::move(replacement), std::move(*footprint), recognized.confidence);
			}
			return std::nullopt;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}
}
