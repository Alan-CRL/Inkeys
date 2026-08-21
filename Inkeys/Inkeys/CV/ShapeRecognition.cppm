module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module Inkeys.CV.ShapeRecognition;

export namespace Inkeys::CV
{
	enum class ShapeType : std::uint8_t
	{
		Unknown = 0,
		AxisAlignedRectangle = 1
	};

	struct InkPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
	};

	struct InkStrokeView
	{
		std::span<const InkPoint> points;
	};

	struct AxisAlignedRectangle
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	struct ShapeResult
	{
		ShapeType type = ShapeType::Unknown;
		float confidence = 0.0f;
		AxisAlignedRectangle rectangle = {};
	};

	enum class ShapeRecognitionRejectReason : std::uint8_t
	{
		None = 0,
		InvalidStrokeCount,
		InvalidDpiScale,
		SourcePointLimit,
		StrokeResamplingFailed,
		InvalidSampleSet,
		InvalidMedianWidth,
		HullTooSmall,
		InvalidHullGeometry,
		HullIsNotQuadrilateral,
		InvalidBounds,
		ShapeTooSmall,
		AspectRatioTooLarge,
		RectangularityTooLow,
		PathLengthRatioOutOfRange,
		StrokeEdgeRatioTooLow,
		OffEdgeRatioTooHigh,
		EdgeCoverageTooLow,
		InvalidLineFit,
		AxisDeviationTooLarge,
		TotalCoverageTooLow,
		ConfidenceTooLow,
		OpenCvException,
		UnexpectedException
	};

	struct ShapeRecognitionThresholds
	{
		std::size_t maximumStrokeCount = 6;
		std::size_t maximumPointsPerStroke = 1024;
		std::size_t maximumTotalSampledPoints = 4096;
		float resampleSpacingDip = 2.0f;
		float minimumShortSideDip = 32.0f;
		float minimumShortSideWidthMultiple = 8.0f;
		float maximumAspectRatio = 6.0f;
		float minimumRectangularity = 0.82f;
		std::array<float, 4> approximationEpsilonRatios = {
			0.02f, 0.03f, 0.04f, 0.05f
		};
		float maximumQuadCornerDistanceRatio = 0.18f;
		float minimumEdgeBandDip = 6.0f;
		float maximumEdgeBandDip = 14.0f;
		float edgeBandShortSideRatio = 0.05f;
		float edgeBandWidthMultiple = 2.0f;
		float minimumLongSegmentDip = 8.0f;
		float maximumAxisDeviationDegrees = 22.0f;
		float maximumAverageAxisDeviationDegrees = 12.0f;
		float minimumEdgeCoverage = 0.68f;
		float minimumTotalCoverage = 0.80f;
		float minimumStrokeOnEdgeRatio = 0.55f;
		float maximumOffEdgeRatio = 0.30f;
		float minimumPathPerimeterRatio = 0.70f;
		float maximumPathPerimeterRatio = 2.50f;
		float minimumConfidence = 0.72f;
	};

	struct ShapeRecognitionStrokeDiagnostics
	{
		std::size_t sourcePointCount = 0;
		std::size_t sampledPointCount = 0;
		// 诊断模式保留识别器实际使用的重采样点，便于离线精确复现。
		std::vector<InkPoint> sampledPoints;
		float pathLength = std::numeric_limits<float>::quiet_NaN();
		float onEdgeRatio = std::numeric_limits<float>::quiet_NaN();
		std::array<float, 4> edgeContributionLength = {};
		std::array<std::size_t, 4> edgePointCount = {};
	};

	struct ShapeRecognitionDiagnostics
	{
		ShapeRecognitionThresholds thresholds = {};
		ShapeRecognitionRejectReason rejectionReason =
			ShapeRecognitionRejectReason::None;
		std::array<ShapeRecognitionRejectReason, 24> failedConditions = {};
		std::size_t failedConditionCount = 0;
		std::size_t inputStrokeCount = 0;
		std::size_t sourcePointCount = 0;
		std::size_t sampledPointCount = 0;
		std::size_t hullPointCount = 0;
		std::size_t approximatedCornerCount = 0;
		std::array<std::size_t, 4> approximationCornerCounts = {};
		float selectedApproximationEpsilonRatio =
			std::numeric_limits<float>::quiet_NaN();
		float maximumQuadCornerDistance = std::numeric_limits<float>::quiet_NaN();
		float maximumQuadCornerDistanceRatio =
			std::numeric_limits<float>::quiet_NaN();
		AxisAlignedRectangle bounds = {};
		float dpiScale = std::numeric_limits<float>::quiet_NaN();
		float medianWidth = std::numeric_limits<float>::quiet_NaN();
		float minimumShortSide = std::numeric_limits<float>::quiet_NaN();
		float shortSide = std::numeric_limits<float>::quiet_NaN();
		float longSide = std::numeric_limits<float>::quiet_NaN();
		float aspectRatio = std::numeric_limits<float>::quiet_NaN();
		float hullArea = std::numeric_limits<float>::quiet_NaN();
		float hullPerimeter = std::numeric_limits<float>::quiet_NaN();
		float rectangularity = std::numeric_limits<float>::quiet_NaN();
		float sourcePathLength = std::numeric_limits<float>::quiet_NaN();
		float rectanglePerimeter = std::numeric_limits<float>::quiet_NaN();
		float pathPerimeterRatio = std::numeric_limits<float>::quiet_NaN();
		float edgeBand = std::numeric_limits<float>::quiet_NaN();
		float edgeBandDip = std::numeric_limits<float>::quiet_NaN();
		std::array<InkPoint, 4> selectedQuad = {};
		std::size_t selectedQuadCornerCount = 0;
		std::array<float, 4> edgeCoverage = {
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::quiet_NaN()
		};
		std::array<float, 4> edgeCoveredLength = {
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::quiet_NaN()
		};
		std::array<float, 4> edgeLargestGap = edgeCoveredLength;
		std::array<float, 4> edgeDistanceP50 = edgeCoveredLength;
		std::array<float, 4> edgeDistanceP90 = edgeCoveredLength;
		std::array<float, 4> edgeDistanceP95 = edgeCoveredLength;
		std::array<float, 4> edgeFitAxisDeviationDegrees = edgeCoveredLength;
		std::array<float, 4> edgeSegmentAxisDeviationDegrees = edgeCoveredLength;
		std::array<ShapeRecognitionStrokeDiagnostics, 6> strokes = {};
		float minimumStrokeOnEdgeRatio = std::numeric_limits<float>::quiet_NaN();
		float totalCoverage = std::numeric_limits<float>::quiet_NaN();
		float offEdgeRatio = std::numeric_limits<float>::quiet_NaN();
		float maximumAxisDeviationDegrees = std::numeric_limits<float>::quiet_NaN();
		float weightedAxisDeviationDegrees = std::numeric_limits<float>::quiet_NaN();
		float worstCornerScore = std::numeric_limits<float>::quiet_NaN();
		float confidence = std::numeric_limits<float>::quiet_NaN();
		bool accepted = false;
	};

	const char* ShapeRecognitionRejectReasonName(
		ShapeRecognitionRejectReason reason) noexcept;

	// 识别边界不暴露 OpenCV 类型；任何异常都按 Unknown 关闭失败。
	ShapeResult RecognizeInkShape(
		std::span<const InkStrokeView> strokes, float dpiScale,
		ShapeRecognitionDiagnostics* diagnostics = nullptr) noexcept;
}
