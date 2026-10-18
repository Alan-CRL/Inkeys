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

	// 分类模型与可输出的 ShapeType 分离，未接入渲染的形状也能参与竞争。
	enum class ShapeModelKind : std::uint8_t
	{
		Rectangle, Line, Ellipse, Triangle, Quadrilateral
	};

	enum class ShapeScaleFallbackReason : std::uint8_t
	{
		None, PhysicalUnavailable, IndirectInput, UnrecognizedSource, MappingMismatch,
		MissingProvenance, DisplayChanged, SourceChanged, InvalidPhysicalScale
	};

	struct ShapeRecognitionContext
	{
		float dipPerPixelX = 1.0f;
		float dipPerPixelY = 1.0f;
		bool physicalScaleTrusted = false;
		float millimetersPerDipX = 0.0f;
		float millimetersPerDipY = 0.0f;
		ShapeScaleFallbackReason scaleFallbackReason = ShapeScaleFallbackReason::PhysicalUnavailable;
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
		UnexpectedException,
		NoViableModel,
		AmbiguousModels,
		UnsupportedShape,
		CornerSupportTooLow,
		EdgeGapTooLarge,
		SnapAngleTooLarge,
		SnapDisplacementTooLarge,
		EquivalentModel
	};

	struct ShapeRecognitionThresholds
	{
		std::size_t maximumStrokeCount = 6;
		std::size_t maximumPointsPerStroke = 1024;
		std::size_t maximumTotalSampledPoints = 4096;
		float resampleSpacingDip = 2.0f;
		float minimumShortSideDip = 64.0f;
		float minimumShortSideWidthMultiple = 8.0f;
		float maximumAspectRatio = 6.0f;
		std::array<float, 4> approximationEpsilonRatios = {
			0.02f, 0.03f, 0.04f, 0.05f
		};
		float minimumEdgeBandDip = 6.0f;
		float maximumEdgeBandDip = 14.0f;
		float edgeBandShortSideRatio = 0.04f;
		float edgeBandWidthMultiple = 0.75f;
		float minimumLongSegmentDip = 8.0f;
		float maximumAxisDeviationDegrees = 22.0f;
		float maximumAverageAxisDeviationDegrees = 12.0f;
		float minimumEdgeCoverage = 0.70f;
		float minimumTotalCoverage = 0.85f;
		float minimumStrokeOnEdgeRatio = 0.70f;
		float maximumOffEdgeRatio = 0.15f;
		float minimumPathPerimeterRatio = 0.70f;
		float maximumPathPerimeterRatio = 2.50f;
		float minimumConfidence = 0.80f;
		float minimumModelMargin = 0.08f;
		float maximumSnapAngleDegrees = 10.0f;
		float maximumSnapDisplacementRatio = 0.20f;
		float maximumCornerAngleDeviationDegrees = 15.0f;
		float maximumOppositeEdgeDeviationDegrees = 10.0f;
		float maximumCornerGapRatio = 0.15f;
		float maximumEdgeGapRatio = 0.20f;
		float cornerTransitionShortSideRatio = 0.20f;
		float maximumClosureArcRatio = 0.35f;
		float maximumClosureDistanceRatio = 0.30f;
		float maximumCornerCoverageAllowance = 0.05f;
		float physicalNoiseMillimeters = 1.5f;
		std::size_t maximumFitIterations = 6;
	};

	struct ShapeHypothesis
	{
		ShapeModelKind model = ShapeModelKind::Rectangle;
		bool closed = true;
		bool eligible = false;
		bool canCorrect = false;
		float quality = 0.0f;
		float residual = 1.0f;
		float coverage = 0.0f;
		float inlierRatio = 0.0f;
		float toleranceDip = 0.0f;
		float toleranceDipX = 0.0f;
		float toleranceDipY = 0.0f;
		float fittedAngleDegrees = 0.0f;
		float snapDisplacementDip = 0.0f;
		std::size_t group = 0;
		std::size_t strokeCount = 0;
		ShapeRecognitionRejectReason rejectionReason = ShapeRecognitionRejectReason::NoViableModel;
		// 拟合角点及修正矩形均为画布 px；角度、残差和容差在 DIP 中计算。
		std::array<InkPoint, 4> fittedCorners = {};
		AxisAlignedRectangle correction = {};
	};

	struct ShapeSelection
	{
		std::size_t index = (std::numeric_limits<std::size_t>::max)();
		float margin = 0.0f;
		ShapeRecognitionRejectReason rejectionReason = ShapeRecognitionRejectReason::NoViableModel;
	};

	struct ShapeEvaluation
	{
		std::array<ShapeHypothesis, 5> hypotheses = {};
		std::size_t count = 0;
		ShapeSelection selection = {};
		ShapeResult result = {};
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
		float cornerTransitionDip = 0.0f;
		float endpointClosureGapDip = 0.0f;
		float cornerCoverageAllowance = 0.0f;
		float offEdgeRatio = std::numeric_limits<float>::quiet_NaN();
		float maximumAxisDeviationDegrees = std::numeric_limits<float>::quiet_NaN();
		float weightedAxisDeviationDegrees = std::numeric_limits<float>::quiet_NaN();
		float worstCornerScore = std::numeric_limits<float>::quiet_NaN();
		float confidence = std::numeric_limits<float>::quiet_NaN();
		bool accepted = false;
		ShapeRecognitionContext context = {};
		std::array<ShapeHypothesis, 5> hypotheses = {};
		std::size_t hypothesisCount = 0;
		float modelMargin = 0.0f;
		float fittedAngleDegrees = 0.0f;
		float snapDisplacementDip = 0.0f;
	};

	const char* ShapeRecognitionRejectReasonName(
		ShapeRecognitionRejectReason reason) noexcept;

	const char* ShapeScaleFallbackReasonName(ShapeScaleFallbackReason reason) noexcept;
	const char* ShapeModelKindName(ShapeModelKind model) noexcept;
	ShapeSelection SelectShapeHypothesis(std::span<const ShapeHypothesis> hypotheses,
		const ShapeRecognitionContext& context) noexcept;
	ShapeEvaluation EvaluateInkShapes(std::span<const InkStrokeView> strokes,
		const ShapeRecognitionContext& context,
		ShapeRecognitionDiagnostics* diagnostics = nullptr) noexcept;
	ShapeResult RecognizeInkShape(std::span<const InkStrokeView> strokes,
		const ShapeRecognitionContext& context,
		ShapeRecognitionDiagnostics* diagnostics = nullptr) noexcept;

	// 识别边界不暴露 OpenCV 类型；任何异常都按 Unknown 关闭失败。
	ShapeResult RecognizeInkShape(
		std::span<const InkStrokeView> strokes, float dpiScale,
		ShapeRecognitionDiagnostics* diagnostics = nullptr) noexcept;
}
