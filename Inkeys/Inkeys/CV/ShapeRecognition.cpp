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
		constexpr std::size_t kMaximumStrokeCount = 6;
		constexpr std::size_t kMaximumPointsPerStroke = 1024;
		constexpr std::size_t kMaximumTotalPoints = 4096;
		constexpr std::size_t kMaximumSourcePoints = 1024 * 1024;
		constexpr float kMaximumAspectRatio = 6.0f;
		constexpr float kMinimumRectangularity = 0.88f;
		constexpr float kMaximumAxisDeviationDegrees = 15.0f;
		constexpr float kMaximumAverageAxisDeviationDegrees = 8.0f;
		constexpr float kMinimumEdgeCoverage = 0.80f;
		constexpr float kMinimumTotalCoverage = 0.88f;
		constexpr float kMaximumOffEdgeRatio = 0.12f;
		constexpr float kMinimumPathPerimeterRatio = 0.80f;
		constexpr float kMaximumPathPerimeterRatio = 2.50f;
		constexpr float kMinimumConfidence = 0.82f;
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
				2.0 * static_cast<double>(dpiScale), meanWidth * 0.5);
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
			float edgeLength) noexcept
		{
			if (intervals.empty() || edgeLength <= 0.0f) return 0.0f;
			std::sort(intervals.begin(), intervals.end(),
				[](const EdgeInterval& left, const EdgeInterval& right)
				{
					return left.begin < right.begin ||
						(left.begin == right.begin && left.end < right.end);
				});
			float covered = 0.0f;
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
				begin = intervals[index].begin;
				end = intervals[index].end;
			}
			covered += std::max(0.0f, end - begin);
			return std::clamp(covered, 0.0f, edgeLength);
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
		const auto reject = [&](ShapeRecognitionRejectReason reason) noexcept
		{
			if (diagnostics)
			{
				diagnostics->accepted = false;
				diagnostics->rejectionReason = reason;
			}
			return ShapeResult{};
		};
		try
		{
			if (strokes.empty() || strokes.size() > kMaximumStrokeCount)
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

			const std::size_t perStrokeBudget = std::min(kMaximumPointsPerStroke,
				kMaximumTotalPoints / strokes.size());
			std::vector<SampledStroke> sampled(strokes.size());
			std::vector<cv::Point2f> allPoints;
			allPoints.reserve(kMaximumTotalPoints);
			std::vector<float> widths;
			widths.reserve(kMaximumTotalPoints);
			float sourcePathLength = 0.0f;
			for (std::size_t index = 0; index < strokes.size(); ++index)
			{
				if (!ResampleStroke(strokes[index].points,
					perStrokeBudget, dpiScale, sampled[index]))
					return reject(ShapeRecognitionRejectReason::StrokeResamplingFailed);
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
			if (allPoints.size() < 4 || allPoints.size() > kMaximumTotalPoints ||
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
			std::vector<cv::Point2f> quad;
			cv::approxPolyDP(hull, quad, hullPerimeter * 0.02, true);
			if (diagnostics) diagnostics->approximatedCornerCount = quad.size();
			if (quad.size() != 4 || !cv::isContourConvex(quad))
				return reject(ShapeRecognitionRejectReason::HullIsNotQuadrilateral);

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
				32.0f * dpiScale, 8.0f * medianWidth);
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
			if (shortSide < minimumShortSide)
				return reject(ShapeRecognitionRejectReason::ShapeTooSmall);
			if (aspectRatio > kMaximumAspectRatio)
				return reject(ShapeRecognitionRejectReason::AspectRatioTooLarge);
			const float rectangleArea = width * height;
			const float rectangularity = static_cast<float>(hullArea) / rectangleArea;
			if (diagnostics) diagnostics->rectangularity = rectangularity;
			if (!std::isfinite(rectangularity) ||
				rectangularity < kMinimumRectangularity || rectangularity > 1.01f)
				return reject(ShapeRecognitionRejectReason::RectangularityTooLow);

			const AxisAlignedRectangle rectangle{ left, top, right, bottom };
			const std::array<float, 4> edgeLengths = { width, height, width, height };
			const float perimeter = 2.0f * (width + height);
			const float pathRatio = sourcePathLength / perimeter;
			if (diagnostics)
			{
				diagnostics->rectanglePerimeter = perimeter;
				diagnostics->pathPerimeterRatio = pathRatio;
			}
			if (!std::isfinite(pathRatio) || pathRatio < kMinimumPathPerimeterRatio ||
				pathRatio > kMaximumPathPerimeterRatio)
				return reject(ShapeRecognitionRejectReason::PathLengthRatioOutOfRange);
			const float edgeBand = std::max(3.0f * dpiScale, 1.5f * medianWidth);
			if (diagnostics) diagnostics->edgeBand = edgeBand;
			const float minimumLongSegmentLength =
				std::max(8.0f * dpiScale, 2.0f * medianWidth);

			std::array<std::vector<EdgeInterval>, 4> intervals;
			std::array<std::vector<cv::Point2f>, 4> edgePoints;
			float totalSegmentLength = 0.0f;
			float offEdgeLength = 0.0f;
			float minimumStrokeOnEdgeRatio = 1.0f;
			for (const SampledStroke& stroke : sampled)
			{
				float strokeLength = 0.0f;
				float strokeOnEdgeLength = 0.0f;
				for (const InkPoint& point : stroke.points)
				{
					const std::size_t edge = NearestEdge(point, rectangle);
					if (DistanceToEdge(point, edge, rectangle) <= edgeBand)
						edgePoints[edge].emplace_back(point.x, point.y);
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
				}
				const float strokeOnEdgeRatio = strokeLength > 0.0f
					? strokeOnEdgeLength / strokeLength : 0.0f;
				minimumStrokeOnEdgeRatio = std::min(
					minimumStrokeOnEdgeRatio, strokeOnEdgeRatio);
				if (strokeLength <= 0.0f || strokeOnEdgeRatio < 0.80f)
				{
					if (diagnostics)
						diagnostics->minimumStrokeOnEdgeRatio = minimumStrokeOnEdgeRatio;
					return reject(ShapeRecognitionRejectReason::StrokeEdgeRatioTooLow);
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
			if (totalSegmentLength <= 0.0f || offEdgeRatio > kMaximumOffEdgeRatio)
				return reject(ShapeRecognitionRejectReason::OffEdgeRatioTooHigh);

			float maximumAxisDeviation = 0.0f;
			double longSegmentWeightedDeviation = 0.0;
			double longSegmentTotalLength = 0.0;
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
						return reject(ShapeRecognitionRejectReason::InvalidLineFit);
					maximumAxisDeviation = std::max(maximumAxisDeviation, deviation);
					longSegmentWeightedDeviation += deviation * segmentLength;
					longSegmentTotalLength += segmentLength;
				}
			}

			std::array<float, 4> coverages = {};
			float coveredPerimeter = 0.0f;
			float edgeWeightedAxisDeviation = 0.0f;
			for (std::size_t edge = 0; edge < 4; ++edge)
			{
				const float covered = MergedCoverage(intervals[edge], edgeLengths[edge]);
				coverages[edge] = covered / edgeLengths[edge];
				if (diagnostics) diagnostics->edgeCoverage[edge] = coverages[edge];
				if (coverages[edge] < kMinimumEdgeCoverage || edgePoints[edge].size() < 2)
					return reject(ShapeRecognitionRejectReason::EdgeCoverageTooLow);
				coveredPerimeter += covered;
				cv::Vec4f line = {};
				cv::fitLine(edgePoints[edge], line, cv::DIST_L2, 0.0, 0.01, 0.01);
				const float deviation = AxisDeviationDegrees(line, edge == 0 || edge == 2);
				if (!std::isfinite(deviation))
					return reject(ShapeRecognitionRejectReason::InvalidLineFit);
				maximumAxisDeviation = std::max(maximumAxisDeviation, deviation);
				edgeWeightedAxisDeviation += deviation * edgeLengths[edge];
			}
			const float totalCoverage = coveredPerimeter / perimeter;
			float weightedAxisDeviation = edgeWeightedAxisDeviation / perimeter;
			if (longSegmentTotalLength > 0.0)
			{
				const float longSegmentAverage = static_cast<float>(
					longSegmentWeightedDeviation / longSegmentTotalLength);
				if (!std::isfinite(longSegmentAverage))
					return reject(ShapeRecognitionRejectReason::InvalidLineFit);
				weightedAxisDeviation = std::max(
					weightedAxisDeviation, longSegmentAverage);
			}
			if (diagnostics)
			{
				diagnostics->totalCoverage = totalCoverage;
				diagnostics->maximumAxisDeviationDegrees = maximumAxisDeviation;
				diagnostics->weightedAxisDeviationDegrees = weightedAxisDeviation;
			}
			if (totalCoverage < kMinimumTotalCoverage)
				return reject(ShapeRecognitionRejectReason::TotalCoverageTooLow);
			if (maximumAxisDeviation > kMaximumAxisDeviationDegrees ||
				weightedAxisDeviation > kMaximumAverageAxisDeviationDegrees)
				return reject(ShapeRecognitionRejectReason::AxisDeviationTooLarge);

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
				1.0f - weightedAxisDeviation / kMaximumAxisDeviationDegrees);
			const float straightnessScore = Clamp01(
				1.0f - offEdgeRatio / kMaximumOffEdgeRatio);
			const float confidence = 0.22f * rectangularityScore +
				0.22f * edgeCoverageScore + 0.10f * totalCoverageScore +
				0.14f * axisScore + 0.14f * straightnessScore +
				0.18f * worstCornerScore;
			if (diagnostics) diagnostics->confidence = confidence;
			if (!std::isfinite(confidence) || confidence < kMinimumConfidence)
				return reject(ShapeRecognitionRejectReason::ConfidenceTooLow);
			if (diagnostics)
			{
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
