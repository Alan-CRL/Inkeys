module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#pragma comment(lib, "opencv_core4.lib")
#pragma comment(lib, "opencv_imgproc4.lib")

module Inkeys.CV.ShapeRecognition;

namespace Inkeys::CV
{
	namespace
	{
		constexpr std::size_t kMaximumSourcePoints = 1024 * 1024;
		constexpr ShapeRecognitionThresholds kThresholds = {};
		constexpr float kPi = 3.14159265358979323846f;

		struct SampledStroke
		{
			std::vector<InkPoint> points;
			float sourceLength = 0.0f;
		};

		struct EdgeInterval
		{
			float begin = 0.0f;
			float end = 0.0f;
		};

		bool IsFinitePoint(const InkPoint& point) noexcept
		{
			return std::isfinite(point.x) && std::isfinite(point.y) &&
				std::isfinite(point.width) && point.width >= 0.0f;
		}

		float Clamp01(float value) noexcept
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		float DipToPixels(float value, float dpiScale) noexcept
		{
			return value * dpiScale;
		}

		bool ResampleStroke(std::span<const InkPoint> source,
			std::size_t pointBudget, float dpiScale, SampledStroke& output)
		{
			if (source.size() < 2 || source.size() > kMaximumSourcePoints ||
				pointBudget < 2) return false;
			double totalLength = 0.0;
			double widthSum = 0.0;
			std::size_t positiveWidthCount = 0;
			for (std::size_t index = 0; index < source.size(); ++index)
			{
				if (!IsFinitePoint(source[index])) return false;
				if (source[index].width > 0.0f)
				{
					widthSum += source[index].width;
					++positiveWidthCount;
				}
				if (index == 0) continue;
				const double deltaX = static_cast<double>(source[index].x) - source[index - 1].x;
				const double deltaY = static_cast<double>(source[index].y) - source[index - 1].y;
				const double segmentLength = std::hypot(deltaX, deltaY);
				if (!std::isfinite(segmentLength)) return false;
				totalLength += segmentLength;
				if (!std::isfinite(totalLength) ||
					totalLength > static_cast<double>((std::numeric_limits<float>::max)()))
					return false;
			}
			if (totalLength <= 0.0 || positiveWidthCount == 0) return false;

			const double meanWidth = widthSum / positiveWidthCount;
			const double spacing = std::max(
				static_cast<double>(DipToPixels(kThresholds.resampleSpacingDip, dpiScale)),
				meanWidth * 0.5);
			const std::size_t desiredCount = static_cast<std::size_t>(std::clamp(
				std::ceil(totalLength / spacing) + 1.0, 2.0,
				static_cast<double>(pointBudget)));
			output.points.clear();
			output.points.reserve(desiredCount);
			output.sourceLength = static_cast<float>(totalLength);

			std::size_t segmentIndex = 1;
			double segmentStartDistance = 0.0;
			double segmentLength = std::hypot(
				static_cast<double>(source[1].x) - source[0].x,
				static_cast<double>(source[1].y) - source[0].y);
			for (std::size_t sampleIndex = 0; sampleIndex < desiredCount; ++sampleIndex)
			{
				const double targetDistance = totalLength * sampleIndex /
					static_cast<double>(desiredCount - 1);
				while (segmentIndex + 1 < source.size() &&
					segmentStartDistance + segmentLength < targetDistance)
				{
					segmentStartDistance += segmentLength;
					++segmentIndex;
					segmentLength = std::hypot(
						static_cast<double>(source[segmentIndex].x) - source[segmentIndex - 1].x,
						static_cast<double>(source[segmentIndex].y) - source[segmentIndex - 1].y);
				}
				while (segmentLength <= 0.0 && segmentIndex + 1 < source.size())
				{
					++segmentIndex;
					segmentLength = std::hypot(
						static_cast<double>(source[segmentIndex].x) - source[segmentIndex - 1].x,
						static_cast<double>(source[segmentIndex].y) - source[segmentIndex - 1].y);
				}
				const InkPoint& begin = source[segmentIndex - 1];
				const InkPoint& end = source[segmentIndex];
				const double ratio = segmentLength > 0.0
					? std::clamp((targetDistance - segmentStartDistance) / segmentLength, 0.0, 1.0)
					: 0.0;
				output.points.push_back({
					static_cast<float>(begin.x + (end.x - begin.x) * ratio),
					static_cast<float>(begin.y + (end.y - begin.y) * ratio),
					static_cast<float>(begin.width + (end.width - begin.width) * ratio)
				});
			}
			return output.points.size() >= 2;
		}

		std::size_t NearestEdge(const InkPoint& point,
			const AxisAlignedRectangle& rectangle) noexcept
		{
			const std::array<float, 4> distances = {
				std::abs(point.y - rectangle.top),
				std::abs(point.x - rectangle.right),
				std::abs(point.y - rectangle.bottom),
				std::abs(point.x - rectangle.left)
			};
			return static_cast<std::size_t>(std::distance(distances.begin(),
				std::min_element(distances.begin(), distances.end())));
		}

		float DistanceToEdge(const InkPoint& point, std::size_t edge,
			const AxisAlignedRectangle& rectangle) noexcept
		{
			switch (edge)
			{
			case 0: return std::abs(point.y - rectangle.top);
			case 1: return std::abs(point.x - rectangle.right);
			case 2: return std::abs(point.y - rectangle.bottom);
			default: return std::abs(point.x - rectangle.left);
			}
		}

		float EdgeProjection(const InkPoint& point, std::size_t edge,
			const AxisAlignedRectangle& rectangle) noexcept
		{
			return edge == 0 || edge == 2
				? point.x - rectangle.left : point.y - rectangle.top;
		}

		float MergedCoverage(std::vector<EdgeInterval>& intervals,
			float edgeLength, float* largestGap = nullptr) noexcept
		{
			if (largestGap) *largestGap = edgeLength > 0.0f ? edgeLength : 0.0f;
			if (intervals.empty() || edgeLength <= 0.0f) return 0.0f;
			std::sort(intervals.begin(), intervals.end(),
				[](const EdgeInterval& left, const EdgeInterval& right)
				{
					return left.begin < right.begin ||
						(left.begin == right.begin && left.end < right.end);
				});
			float covered = 0.0f;
			float maximumGap = std::clamp(intervals.front().begin, 0.0f, edgeLength);
			float begin = intervals.front().begin;
			float end = intervals.front().end;
			for (std::size_t index = 1; index < intervals.size(); ++index)
			{
				if (intervals[index].begin <= end)
				{
					end = std::max(end, intervals[index].end);
					continue;
				}
				covered += std::max(0.0f, end - begin);
				maximumGap = std::max(maximumGap,
					std::max(0.0f, intervals[index].begin - end));
				begin = intervals[index].begin;
				end = intervals[index].end;
			}
			covered += std::max(0.0f, end - begin);
			maximumGap = std::max(maximumGap,
				std::max(0.0f, edgeLength - end));
			if (largestGap) *largestGap = std::clamp(maximumGap, 0.0f, edgeLength);
			return std::clamp(covered, 0.0f, edgeLength);
		}

		float Percentile(std::vector<float> values, float percentile) noexcept
		{
			if (values.empty()) return std::numeric_limits<float>::quiet_NaN();
			std::sort(values.begin(), values.end());
			const float position = std::clamp(percentile, 0.0f, 1.0f) *
				static_cast<float>(values.size() - 1);
			const std::size_t lower = static_cast<std::size_t>(position);
			const std::size_t upper = std::min(lower + 1, values.size() - 1);
			const float fraction = position - static_cast<float>(lower);
			return values[lower] + (values[upper] - values[lower]) * fraction;
		}

		float AxisDeviationDegrees(const cv::Vec4f& line,
			bool horizontal) noexcept
		{
			const float axisComponent = horizontal ? std::abs(line[0]) : std::abs(line[1]);
			const float crossComponent = horizontal ? std::abs(line[1]) : std::abs(line[0]);
			return std::atan2(crossComponent, axisComponent) * 180.0f / kPi;
		}

		float SegmentAxisDeviationDegrees(double deltaX, double deltaY,
			bool horizontal) noexcept
		{
			const double axisComponent = horizontal ? std::abs(deltaX) : std::abs(deltaY);
			const double crossComponent = horizontal ? std::abs(deltaY) : std::abs(deltaX);
			return static_cast<float>(
				std::atan2(crossComponent, axisComponent) * 180.0 / kPi);
		}
	}

	const char* ShapeRecognitionRejectReasonName(
		ShapeRecognitionRejectReason reason) noexcept
	{
		switch (reason)
		{
		case ShapeRecognitionRejectReason::None: return "none";
		case ShapeRecognitionRejectReason::InvalidStrokeCount: return "invalid_stroke_count";
		case ShapeRecognitionRejectReason::InvalidDpiScale: return "invalid_dpi_scale";
		case ShapeRecognitionRejectReason::SourcePointLimit: return "source_point_limit";
		case ShapeRecognitionRejectReason::StrokeResamplingFailed: return "stroke_resampling_failed";
		case ShapeRecognitionRejectReason::InvalidSampleSet: return "invalid_sample_set";
		case ShapeRecognitionRejectReason::InvalidMedianWidth: return "invalid_median_width";
		case ShapeRecognitionRejectReason::HullTooSmall: return "hull_too_small";
		case ShapeRecognitionRejectReason::InvalidHullGeometry: return "invalid_hull_geometry";
		case ShapeRecognitionRejectReason::HullIsNotQuadrilateral: return "hull_not_quadrilateral";
		case ShapeRecognitionRejectReason::InvalidBounds: return "invalid_bounds";
		case ShapeRecognitionRejectReason::ShapeTooSmall: return "shape_too_small";
		case ShapeRecognitionRejectReason::AspectRatioTooLarge: return "aspect_ratio_too_large";
		case ShapeRecognitionRejectReason::RectangularityTooLow: return "rectangularity_too_low";
		case ShapeRecognitionRejectReason::PathLengthRatioOutOfRange: return "path_ratio_out_of_range";
		case ShapeRecognitionRejectReason::StrokeEdgeRatioTooLow: return "stroke_edge_ratio_too_low";
		case ShapeRecognitionRejectReason::OffEdgeRatioTooHigh: return "off_edge_ratio_too_high";
		case ShapeRecognitionRejectReason::EdgeCoverageTooLow: return "edge_coverage_too_low";
		case ShapeRecognitionRejectReason::InvalidLineFit: return "invalid_line_fit";
		case ShapeRecognitionRejectReason::AxisDeviationTooLarge: return "axis_deviation_too_large";
		case ShapeRecognitionRejectReason::TotalCoverageTooLow: return "total_coverage_too_low";
		case ShapeRecognitionRejectReason::ConfidenceTooLow: return "confidence_too_low";
		case ShapeRecognitionRejectReason::OpenCvException: return "opencv_exception";
		default: return "unexpected_exception";
		}
	}

	ShapeResult RecognizeInkShape(
		std::span<const InkStrokeView> strokes, float dpiScale,
		ShapeRecognitionDiagnostics* diagnostics) noexcept
	{
		if (diagnostics)
		{
			*diagnostics = {};
			diagnostics->inputStrokeCount = strokes.size();
			diagnostics->dpiScale = dpiScale;
		}
		const auto recordFailure = [&](ShapeRecognitionRejectReason reason) noexcept
		{
			if (!diagnostics || reason == ShapeRecognitionRejectReason::None) return;
			diagnostics->accepted = false;
			if (diagnostics->rejectionReason == ShapeRecognitionRejectReason::None)
				diagnostics->rejectionReason = reason;
			for (std::size_t index = 0; index < diagnostics->failedConditionCount; ++index)
			{
				if (diagnostics->failedConditions[index] == reason) return;
			}
			if (diagnostics->failedConditionCount < diagnostics->failedConditions.size())
				diagnostics->failedConditions[diagnostics->failedConditionCount++] = reason;
		};
		const auto reject = [&](ShapeRecognitionRejectReason reason) noexcept
		{
			recordFailure(reason);
			return ShapeResult{};
		};
		const auto stopAfterFailure = [&](ShapeRecognitionRejectReason reason) noexcept
		{
			recordFailure(reason);
			return diagnostics == nullptr;
		};
		try
		{
			if (strokes.empty() || strokes.size() > kThresholds.maximumStrokeCount)
				return reject(ShapeRecognitionRejectReason::InvalidStrokeCount);
			if (!std::isfinite(dpiScale) || dpiScale <= 0.0f || dpiScale > 16.0f)
				return reject(ShapeRecognitionRejectReason::InvalidDpiScale);
			std::size_t sourcePointCount = 0;
			for (const InkStrokeView& stroke : strokes)
			{
				if (stroke.points.size() > kMaximumSourcePoints ||
					sourcePointCount > kMaximumSourcePoints - stroke.points.size())
					return reject(ShapeRecognitionRejectReason::SourcePointLimit);
				sourcePointCount += stroke.points.size();
			}
			if (diagnostics) diagnostics->sourcePointCount = sourcePointCount;
			if (sourcePointCount > kMaximumSourcePoints)
				return reject(ShapeRecognitionRejectReason::SourcePointLimit);

			const std::size_t perStrokeBudget = std::min(
				kThresholds.maximumPointsPerStroke,
				kThresholds.maximumTotalSampledPoints / strokes.size());
			std::vector<SampledStroke> sampled(strokes.size());
			std::vector<cv::Point2f> allPoints;
			allPoints.reserve(kThresholds.maximumTotalSampledPoints);
			std::vector<float> widths;
			widths.reserve(kThresholds.maximumTotalSampledPoints);
			float sourcePathLength = 0.0f;
			for (std::size_t index = 0; index < strokes.size(); ++index)
			{
				if (!ResampleStroke(strokes[index].points,
					perStrokeBudget, dpiScale, sampled[index]))
					return reject(ShapeRecognitionRejectReason::StrokeResamplingFailed);
				if (diagnostics)
				{
					diagnostics->strokes[index].sourcePointCount = strokes[index].points.size();
					diagnostics->strokes[index].sampledPointCount = sampled[index].points.size();
					diagnostics->strokes[index].sampledPoints = sampled[index].points;
					diagnostics->strokes[index].pathLength = sampled[index].sourceLength;
				}
				sourcePathLength += sampled[index].sourceLength;
				if (!std::isfinite(sourcePathLength))
					return reject(ShapeRecognitionRejectReason::InvalidSampleSet);
				for (const InkPoint& point : sampled[index].points)
				{
					allPoints.emplace_back(point.x, point.y);
					if (point.width > 0.0f) widths.push_back(point.width);
				}
			}
			if (diagnostics)
			{
				diagnostics->sampledPointCount = allPoints.size();
				diagnostics->sourcePathLength = sourcePathLength;
			}
			if (allPoints.size() < 4 ||
				allPoints.size() > kThresholds.maximumTotalSampledPoints ||
				widths.empty()) return reject(ShapeRecognitionRejectReason::InvalidSampleSet);
			const auto medianPosition = widths.begin() + widths.size() / 2;
			std::nth_element(widths.begin(), medianPosition, widths.end());
			const float medianWidth = *medianPosition;
			if (diagnostics) diagnostics->medianWidth = medianWidth;
			if (!std::isfinite(medianWidth) || medianWidth <= 0.0f)
				return reject(ShapeRecognitionRejectReason::InvalidMedianWidth);

			std::vector<cv::Point2f> hull;
			cv::convexHull(allPoints, hull, true, true);
			if (diagnostics) diagnostics->hullPointCount = hull.size();
			if (hull.size() < 4) return reject(ShapeRecognitionRejectReason::HullTooSmall);
			const double hullPerimeter = cv::arcLength(hull, true);
			const double hullArea = std::abs(cv::contourArea(hull));
			if (diagnostics)
			{
				diagnostics->hullPerimeter = static_cast<float>(hullPerimeter);
				diagnostics->hullArea = static_cast<float>(hullArea);
			}
			if (!std::isfinite(hullPerimeter) || !std::isfinite(hullArea) ||
				hullPerimeter <= 0.0 || hullArea <= 0.0)
				return reject(ShapeRecognitionRejectReason::InvalidHullGeometry);

			float left = allPoints.front().x;
			float right = left;
			float top = allPoints.front().y;
			float bottom = top;
			for (const cv::Point2f& point : allPoints)
			{
				left = std::min(left, point.x);
				right = std::max(right, point.x);
				top = std::min(top, point.y);
				bottom = std::max(bottom, point.y);
			}
			const float width = right - left;
			const float height = bottom - top;
			const float shortSide = std::min(width, height);
			const float longSide = std::max(width, height);
			const float minimumShortSide = std::max(
				DipToPixels(kThresholds.minimumShortSideDip, dpiScale),
				kThresholds.minimumShortSideWidthMultiple * medianWidth);
			const float aspectRatio = shortSide > 0.0f
				? longSide / shortSide : (std::numeric_limits<float>::infinity)();
			if (diagnostics)
			{
				diagnostics->bounds = { left, top, right, bottom };
				diagnostics->shortSide = shortSide;
				diagnostics->longSide = longSide;
				diagnostics->minimumShortSide = minimumShortSide;
				diagnostics->aspectRatio = aspectRatio;
			}
			if (!std::isfinite(shortSide) || !std::isfinite(longSide) ||
				shortSide <= 0.0f || longSide <= 0.0f)
				return reject(ShapeRecognitionRejectReason::InvalidBounds);
			if (shortSide < minimumShortSide &&
				stopAfterFailure(ShapeRecognitionRejectReason::ShapeTooSmall)) return {};
			if (aspectRatio > kThresholds.maximumAspectRatio &&
				stopAfterFailure(ShapeRecognitionRejectReason::AspectRatioTooLarge)) return {};

			const std::array<cv::Point2f, 4> boundingCorners = {
				cv::Point2f{ left, top }, cv::Point2f{ right, top },
				cv::Point2f{ right, bottom }, cv::Point2f{ left, bottom }
			};
			std::vector<cv::Point2f> quad;
			std::vector<cv::Point2f> bestQuadCandidate;
			float bestQuadCornerDistance = (std::numeric_limits<float>::infinity)();
			float bestQuadCornerDistanceRatio = (std::numeric_limits<float>::infinity)();
			float selectedApproximationRatio =
				(std::numeric_limits<float>::quiet_NaN)();
			std::size_t closestCornerCount = 0;
			std::size_t closestCornerDelta = (std::numeric_limits<std::size_t>::max)();
			for (std::size_t index = 0;
				index < kThresholds.approximationEpsilonRatios.size(); ++index)
			{
				std::vector<cv::Point2f> candidate;
				const float epsilonRatio = kThresholds.approximationEpsilonRatios[index];
				cv::approxPolyDP(hull, candidate, hullPerimeter * epsilonRatio, true);
				if (diagnostics)
					diagnostics->approximationCornerCounts[index] = candidate.size();
				const std::size_t cornerDelta = candidate.size() > 4
					? candidate.size() - 4 : 4 - candidate.size();
				if (cornerDelta < closestCornerDelta)
				{
					closestCornerDelta = cornerDelta;
					closestCornerCount = candidate.size();
				}
				if (candidate.size() != 4 || !cv::isContourConvex(candidate)) continue;

				float maximumCornerDistance = 0.0f;
				for (const cv::Point2f& corner : boundingCorners)
				{
					float nearest = (std::numeric_limits<float>::max)();
					for (const cv::Point2f& point : candidate)
						nearest = std::min(nearest,
							std::hypot(point.x - corner.x, point.y - corner.y));
					maximumCornerDistance = std::max(maximumCornerDistance, nearest);
				}
				for (const cv::Point2f& point : candidate)
				{
					float nearest = (std::numeric_limits<float>::max)();
					for (const cv::Point2f& corner : boundingCorners)
						nearest = std::min(nearest,
							std::hypot(point.x - corner.x, point.y - corner.y));
					maximumCornerDistance = std::max(maximumCornerDistance, nearest);
				}
				const float cornerDistanceRatio = maximumCornerDistance / shortSide;
				if (cornerDistanceRatio < bestQuadCornerDistanceRatio)
				{
					bestQuadCornerDistance = maximumCornerDistance;
					bestQuadCornerDistanceRatio = cornerDistanceRatio;
					selectedApproximationRatio = epsilonRatio;
					bestQuadCandidate = candidate;
					if (cornerDistanceRatio <=
						kThresholds.maximumQuadCornerDistanceRatio)
						quad = std::move(candidate);
					else
						quad.clear();
				}
			}
			if (diagnostics)
			{
				diagnostics->approximatedCornerCount = quad.empty()
					? closestCornerCount : quad.size();
				diagnostics->selectedApproximationEpsilonRatio =
					selectedApproximationRatio;
				diagnostics->maximumQuadCornerDistance =
					std::isfinite(bestQuadCornerDistance)
					? bestQuadCornerDistance
					: (std::numeric_limits<float>::quiet_NaN)();
				diagnostics->maximumQuadCornerDistanceRatio =
					std::isfinite(bestQuadCornerDistanceRatio)
					? bestQuadCornerDistanceRatio
					: (std::numeric_limits<float>::quiet_NaN)();
				diagnostics->selectedQuadCornerCount = bestQuadCandidate.size();
				for (std::size_t index = 0;
					index < bestQuadCandidate.size() && index < diagnostics->selectedQuad.size();
					++index)
					diagnostics->selectedQuad[index] = {
						bestQuadCandidate[index].x, bestQuadCandidate[index].y, medianWidth };
			}
			if (quad.empty() && stopAfterFailure(
				ShapeRecognitionRejectReason::HullIsNotQuadrilateral)) return {};

			const float rectangleArea = width * height;
			const float rectangularity = static_cast<float>(hullArea) / rectangleArea;
			if (diagnostics) diagnostics->rectangularity = rectangularity;
			if ((!std::isfinite(rectangularity) ||
				rectangularity < kThresholds.minimumRectangularity || rectangularity > 1.01f) &&
				stopAfterFailure(ShapeRecognitionRejectReason::RectangularityTooLow)) return {};

			const AxisAlignedRectangle rectangle{ left, top, right, bottom };
			const std::array<float, 4> edgeLengths = { width, height, width, height };
			const float perimeter = 2.0f * (width + height);
			const float pathRatio = sourcePathLength / perimeter;
			if (diagnostics)
			{
				diagnostics->rectanglePerimeter = perimeter;
				diagnostics->pathPerimeterRatio = pathRatio;
			}
			if ((!std::isfinite(pathRatio) ||
				pathRatio < kThresholds.minimumPathPerimeterRatio ||
				pathRatio > kThresholds.maximumPathPerimeterRatio) &&
				stopAfterFailure(ShapeRecognitionRejectReason::PathLengthRatioOutOfRange))
				return {};
			// 手绘容差先在 DIP 中合成，再统一转换到画布像素，避免高 DPI 更严格。
			const float shortSideDip = shortSide / dpiScale;
			const float medianWidthDip = medianWidth / dpiScale;
			const float edgeBandDip = std::clamp(std::max({
				kThresholds.minimumEdgeBandDip,
				shortSideDip * kThresholds.edgeBandShortSideRatio,
				medianWidthDip * kThresholds.edgeBandWidthMultiple }),
				kThresholds.minimumEdgeBandDip, kThresholds.maximumEdgeBandDip);
			const float edgeBand = DipToPixels(edgeBandDip, dpiScale);
			if (diagnostics)
			{
				diagnostics->edgeBand = edgeBand;
				diagnostics->edgeBandDip = edgeBandDip;
			}
			const float minimumLongSegmentLength =
				std::max(DipToPixels(kThresholds.minimumLongSegmentDip, dpiScale),
					2.0f * medianWidth);

			std::array<std::vector<EdgeInterval>, 4> intervals;
			std::array<std::vector<cv::Point2f>, 4> edgePoints;
			std::array<std::vector<float>, 4> edgeDistances;
			float totalSegmentLength = 0.0f;
			float offEdgeLength = 0.0f;
			float minimumStrokeOnEdgeRatio = 1.0f;
			for (std::size_t strokeIndex = 0; strokeIndex < sampled.size(); ++strokeIndex)
			{
				const SampledStroke& stroke = sampled[strokeIndex];
				ShapeRecognitionStrokeDiagnostics* strokeDiagnostics = diagnostics
					? &diagnostics->strokes[strokeIndex] : nullptr;
				float strokeLength = 0.0f;
				float strokeOnEdgeLength = 0.0f;
				for (const InkPoint& point : stroke.points)
				{
					const std::size_t edge = NearestEdge(point, rectangle);
					const float distance = DistanceToEdge(point, edge, rectangle);
					if (diagnostics) edgeDistances[edge].push_back(distance);
					if (distance <= edgeBand)
					{
						edgePoints[edge].emplace_back(point.x, point.y);
						if (strokeDiagnostics) ++strokeDiagnostics->edgePointCount[edge];
					}
				}
				for (std::size_t index = 1; index < stroke.points.size(); ++index)
				{
					const InkPoint& begin = stroke.points[index - 1];
					const InkPoint& end = stroke.points[index];
					const float segmentLength = std::hypot(end.x - begin.x, end.y - begin.y);
					if (!std::isfinite(segmentLength) || segmentLength <= 0.0f) continue;
					strokeLength += segmentLength;
					totalSegmentLength += segmentLength;
					const InkPoint midpoint{
						(begin.x + end.x) * 0.5f,
						(begin.y + end.y) * 0.5f,
						(begin.width + end.width) * 0.5f
					};
					const std::size_t edge = NearestEdge(midpoint, rectangle);
					const float maximumDistance = std::max({
						DistanceToEdge(begin, edge, rectangle),
						DistanceToEdge(midpoint, edge, rectangle),
						DistanceToEdge(end, edge, rectangle) });
					if (maximumDistance > edgeBand)
					{
						offEdgeLength += segmentLength;
						continue;
					}
					const float edgeLength = edgeLengths[edge];
					float projectionBegin = std::clamp(
						EdgeProjection(begin, edge, rectangle), 0.0f, edgeLength);
					float projectionEnd = std::clamp(
						EdgeProjection(end, edge, rectangle), 0.0f, edgeLength);
					if (projectionBegin > projectionEnd) std::swap(projectionBegin, projectionEnd);
					intervals[edge].push_back({ projectionBegin, projectionEnd });
					strokeOnEdgeLength += segmentLength;
					if (strokeDiagnostics)
						strokeDiagnostics->edgeContributionLength[edge] += segmentLength;
				}
				const float strokeOnEdgeRatio = strokeLength > 0.0f
					? strokeOnEdgeLength / strokeLength : 0.0f;
				if (strokeDiagnostics) strokeDiagnostics->onEdgeRatio = strokeOnEdgeRatio;
				minimumStrokeOnEdgeRatio = std::min(
					minimumStrokeOnEdgeRatio, strokeOnEdgeRatio);
				if (strokeLength <= 0.0f ||
					strokeOnEdgeRatio < kThresholds.minimumStrokeOnEdgeRatio)
				{
					if (diagnostics)
						diagnostics->minimumStrokeOnEdgeRatio = minimumStrokeOnEdgeRatio;
					if (stopAfterFailure(
						ShapeRecognitionRejectReason::StrokeEdgeRatioTooLow)) return {};
				}
			}
			const float offEdgeRatio = totalSegmentLength > 0.0f
				? offEdgeLength / totalSegmentLength
				: (std::numeric_limits<float>::infinity)();
			if (diagnostics)
			{
				diagnostics->minimumStrokeOnEdgeRatio = minimumStrokeOnEdgeRatio;
				diagnostics->offEdgeRatio = offEdgeRatio;
			}
			if ((totalSegmentLength <= 0.0f ||
				offEdgeRatio > kThresholds.maximumOffEdgeRatio) &&
				stopAfterFailure(ShapeRecognitionRejectReason::OffEdgeRatioTooHigh))
				return {};

			float maximumAxisDeviation = 0.0f;
			double longSegmentWeightedDeviation = 0.0;
			double longSegmentTotalLength = 0.0;
			std::array<double, 4> edgeSegmentWeightedDeviation = {};
			std::array<double, 4> edgeSegmentTotalLength = {};
			// 只检查足够长的原始段，避免高频 packet 抖动主导最大偏轴。
			for (const InkStrokeView& stroke : strokes)
			{
				for (std::size_t index = 1; index < stroke.points.size(); ++index)
				{
					const InkPoint& begin = stroke.points[index - 1];
					const InkPoint& end = stroke.points[index];
					const double deltaX = static_cast<double>(end.x) - begin.x;
					const double deltaY = static_cast<double>(end.y) - begin.y;
					const double segmentLength = std::hypot(deltaX, deltaY);
					if (!std::isfinite(segmentLength) ||
						segmentLength < minimumLongSegmentLength) continue;
					const InkPoint midpoint{
						static_cast<float>(static_cast<double>(begin.x) + deltaX * 0.5),
						static_cast<float>(static_cast<double>(begin.y) + deltaY * 0.5),
						(begin.width + end.width) * 0.5f
					};
					const std::size_t edge = NearestEdge(midpoint, rectangle);
					const float maximumDistance = std::max({
						DistanceToEdge(begin, edge, rectangle),
						DistanceToEdge(midpoint, edge, rectangle),
						DistanceToEdge(end, edge, rectangle) });
					if (maximumDistance > edgeBand) continue;
					const float deviation = SegmentAxisDeviationDegrees(
						deltaX, deltaY, edge == 0 || edge == 2);
					if (!std::isfinite(deviation))
					{
						if (stopAfterFailure(
							ShapeRecognitionRejectReason::InvalidLineFit)) return {};
						continue;
					}
					maximumAxisDeviation = std::max(maximumAxisDeviation, deviation);
					longSegmentWeightedDeviation += deviation * segmentLength;
					longSegmentTotalLength += segmentLength;
					edgeSegmentWeightedDeviation[edge] += deviation * segmentLength;
					edgeSegmentTotalLength[edge] += segmentLength;
				}
			}

			std::array<float, 4> coverages = {};
			float coveredPerimeter = 0.0f;
			float edgeWeightedAxisDeviation = 0.0f;
			float fittedEdgeLength = 0.0f;
			for (std::size_t edge = 0; edge < 4; ++edge)
			{
				float largestGap = 0.0f;
				const float covered = MergedCoverage(
					intervals[edge], edgeLengths[edge], &largestGap);
				coverages[edge] = covered / edgeLengths[edge];
				if (diagnostics)
				{
					diagnostics->edgeCoverage[edge] = coverages[edge];
					diagnostics->edgeCoveredLength[edge] = covered;
					diagnostics->edgeLargestGap[edge] = largestGap;
					diagnostics->edgeDistanceP50[edge] = Percentile(edgeDistances[edge], 0.50f);
					diagnostics->edgeDistanceP90[edge] = Percentile(edgeDistances[edge], 0.90f);
					diagnostics->edgeDistanceP95[edge] = Percentile(edgeDistances[edge], 0.95f);
				}
				coveredPerimeter += covered;
				if (coverages[edge] < kThresholds.minimumEdgeCoverage ||
					edgePoints[edge].size() < 2)
				{
					if (stopAfterFailure(
						ShapeRecognitionRejectReason::EdgeCoverageTooLow)) return {};
					if (edgePoints[edge].size() < 2) continue;
				}
				cv::Vec4f line = {};
				cv::fitLine(edgePoints[edge], line, cv::DIST_L2, 0.0, 0.01, 0.01);
				const float deviation = AxisDeviationDegrees(line, edge == 0 || edge == 2);
				if (!std::isfinite(deviation))
				{
					if (stopAfterFailure(
						ShapeRecognitionRejectReason::InvalidLineFit)) return {};
					continue;
				}
				if (diagnostics)
					diagnostics->edgeFitAxisDeviationDegrees[edge] = deviation;
				maximumAxisDeviation = std::max(maximumAxisDeviation, deviation);
				edgeWeightedAxisDeviation += deviation * edgeLengths[edge];
				fittedEdgeLength += edgeLengths[edge];
			}
			const float totalCoverage = coveredPerimeter / perimeter;
			float weightedAxisDeviation = fittedEdgeLength > 0.0f
				? edgeWeightedAxisDeviation / fittedEdgeLength
				: (std::numeric_limits<float>::quiet_NaN)();
			if (longSegmentTotalLength > 0.0)
			{
				const float longSegmentAverage = static_cast<float>(
					longSegmentWeightedDeviation / longSegmentTotalLength);
				if (!std::isfinite(longSegmentAverage))
				{
					if (stopAfterFailure(
						ShapeRecognitionRejectReason::InvalidLineFit)) return {};
				}
				else if (std::isfinite(weightedAxisDeviation))
					weightedAxisDeviation = std::max(
						weightedAxisDeviation, longSegmentAverage);
				else weightedAxisDeviation = longSegmentAverage;
			}
			for (std::size_t edge = 0; edge < 4; ++edge)
			{
				if (edgeSegmentTotalLength[edge] <= 0.0) continue;
				const float edgeSegmentAverage = static_cast<float>(
					edgeSegmentWeightedDeviation[edge] / edgeSegmentTotalLength[edge]);
				if (!std::isfinite(edgeSegmentAverage))
				{
					if (stopAfterFailure(
						ShapeRecognitionRejectReason::InvalidLineFit)) return {};
					continue;
				}
				if (diagnostics)
					diagnostics->edgeSegmentAxisDeviationDegrees[edge] = edgeSegmentAverage;
				if (std::isfinite(weightedAxisDeviation))
					weightedAxisDeviation = std::max(
						weightedAxisDeviation, edgeSegmentAverage);
				else weightedAxisDeviation = edgeSegmentAverage;
			}
			if (diagnostics)
			{
				diagnostics->totalCoverage = totalCoverage;
				diagnostics->maximumAxisDeviationDegrees = maximumAxisDeviation;
				diagnostics->weightedAxisDeviationDegrees = weightedAxisDeviation;
			}
			if (totalCoverage < kThresholds.minimumTotalCoverage &&
				stopAfterFailure(ShapeRecognitionRejectReason::TotalCoverageTooLow))
				return {};
			if ((!std::isfinite(weightedAxisDeviation) ||
				maximumAxisDeviation > kThresholds.maximumAxisDeviationDegrees ||
				weightedAxisDeviation >
				kThresholds.maximumAverageAxisDeviationDegrees) &&
				stopAfterFailure(ShapeRecognitionRejectReason::AxisDeviationTooLarge))
				return {};

			const std::array<InkPoint, 4> corners = {
				InkPoint{ left, top, medianWidth }, InkPoint{ right, top, medianWidth },
				InkPoint{ right, bottom, medianWidth }, InkPoint{ left, bottom, medianWidth }
			};
			float worstCornerScore = 1.0f;
			const float cornerTolerance = std::max(edgeBand * 2.0f, shortSide * 0.15f);
			for (const InkPoint& corner : corners)
			{
				float nearest = (std::numeric_limits<float>::max)();
				for (const cv::Point2f& point : allPoints)
					nearest = std::min(nearest,
						std::hypot(point.x - corner.x, point.y - corner.y));
				worstCornerScore = std::min(worstCornerScore,
					Clamp01(1.0f - nearest / cornerTolerance));
			}
			if (diagnostics) diagnostics->worstCornerScore = worstCornerScore;

			const float worstCoverage = *std::min_element(coverages.begin(), coverages.end());
			// 这些量本身已是 0..1 比例；硬门槛先拒绝低质量候选，
			// 置信度保留实际覆盖率，避免把允许的小断角再次过度惩罚。
			const float rectangularityScore = Clamp01(rectangularity);
			const float edgeCoverageScore = Clamp01(worstCoverage);
			const float totalCoverageScore = Clamp01(totalCoverage);
			const float axisScore = Clamp01(
				1.0f - weightedAxisDeviation /
				kThresholds.maximumAxisDeviationDegrees);
			const float straightnessScore = Clamp01(
				1.0f - offEdgeRatio / kThresholds.maximumOffEdgeRatio);
			const float confidence = 0.22f * rectangularityScore +
				0.22f * edgeCoverageScore + 0.10f * totalCoverageScore +
				0.14f * axisScore + 0.14f * straightnessScore +
				0.18f * worstCornerScore;
			if (diagnostics) diagnostics->confidence = confidence;
			if ((!std::isfinite(confidence) ||
				confidence < kThresholds.minimumConfidence) &&
				stopAfterFailure(ShapeRecognitionRejectReason::ConfidenceTooLow)) return {};
			if (diagnostics)
			{
				if (diagnostics->failedConditionCount != 0) return {};
				diagnostics->accepted = true;
				diagnostics->rejectionReason = ShapeRecognitionRejectReason::None;
			}
			return { ShapeType::AxisAlignedRectangle, confidence, rectangle };
		}
		catch (const cv::Exception&)
		{
			return reject(ShapeRecognitionRejectReason::OpenCvException);
		}
		catch (...)
		{
			return reject(ShapeRecognitionRejectReason::UnexpectedException);
		}
	}
}
