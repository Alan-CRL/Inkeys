#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>

namespace Inkeys::CV
{
	enum class MotionPixelFormat : std::uint8_t { Gray8, Bgra32 };
	struct MotionImageView
	{
		// top-down 借用，仅在 Initialize/Process 调用内有效；alpha 不参与跟踪。
		const std::uint8_t* data = nullptr;
		std::size_t sizeBytes = 0, strideBytes = 0;
		int width = 0, height = 0;
		MotionPixelFormat format = MotionPixelFormat::Bgra32;
	};
	struct MotionFrame
	{
		MotionImageView image{};
		double timestampSeconds = 0;
		std::uint64_t sequence = 0;
		bool captureValid = true;
		bool inputLowInformation = false;
	};
	struct MotionVector
	{
		double x = 0, y = 0;
	};
	enum class MotionState : std::uint8_t { Uninitialized, Tracking, Paused };
	enum class MotionTargetPhase : std::uint8_t { Discovering, Locked, Recovering };
	enum class MotionReason : std::uint8_t
	{
		None, Initialized, NotInitialized, InvalidImage, InvalidTimestamp, GeometryChanged,
		InvalidCapture, BlackFrame, InsufficientFeatures, ForwardBackward,
		InconsistentTranslation, InsufficientCoverage, NonTranslation, AmbiguousMotion,
		JumpTooLarge, Paused, OpenCvException, UnexpectedException,
		AwaitingMotion, TargetAmbiguous, MatchAmbiguous, InsufficientOverlap, RecoveryExpired,
	};
	struct MotionStatistics
	{
		std::uint64_t inputSequence = 0, referenceSequence = 0, referenceGeneration = 0;
		std::uint64_t relayGeneration = 0;
		int attempted = 0, forward = 0, backward = 0, fbValid = 0, inliers = 0;
		int referenceCells = 0, occupiedCells = 0, inlierCells = 0, interiorCells = 0;
		int added = 0, retained = 0, retired = 0, initialSurvivors = 0;
		int consecutiveRejects = 0;
		double pointInlierRatio = 0, spatialInlierRatio = 0, survivalRatio = 0;
		double coverageX = 0, coverageY = 0, runnerUpRatio = 0;
		double fbMedian = 0, fbP95 = 0, residualMedian = 0, residualP95 = 0;
		double affineVariation = 0, affineResidual = 0;
		double preprocessMs = 0, opticalFlowMs = 0, fitMs = 0, refillMs = 0;
		std::uint64_t targetId = 0;
		MotionTargetPhase targetPhase = MotionTargetPhase::Discovering;
		int targetPoints = 0, provisionalPoints = 0, templateCount = 0, matchedTemplates = 0, independentBlocks = 0, candidateCount = 0;
		double coarseMatchMs = 0, verificationMs = 0, referenceAgeSeconds = 0, recoverySeconds = 0, ambiguityMargin = 0;
		bool usedKeyframe = false, lowPointFallback = false;
		MotionReason primaryReason = MotionReason::None;
		int expectedBlocks = 0, verifiedBlocks = 0;
		int identityTiles = 0, identityContradictions = 0;
		double identityScore = 0;
		bool identityDisambiguated = false;
	};
	struct MotionResult
	{
		bool accepted = false;
		MotionState state = MotionState::Uninitialized;
		MotionReason reason = MotionReason::NotInitialized;
		// 原图物理像素；内容向上为负 Y。拒绝的 delta 恒为零，total 保持最后可信值。
		MotionVector delta{}, total{};
		double confidence = 0;
		MotionStatistics statistics{};
	};

	// 点位置、flow 和 bounds 为处理图像像素，除以 scaleX/scaleY 还原输入；anchor/total/delta 已是原图像素。
	enum class TrackerDebugStage : std::uint8_t
	{
		Validation, Preprocess, Forward, Refinement, Backward, Consensus, NonTranslation,
		Ambiguity, Consistency, Coverage, Jump, Anchor, Retention, Refill, Commit, CoarseMatch, TargetSelection, PatchVerification, Recovery, Count,
	};
	enum class TrackerDebugComparison : std::uint8_t { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
	enum class TrackerDebugCheckId : std::uint8_t
	{
		TrackingState, CaptureInvalid, TimestampNonFinite, ImageDataMissing,
		WidthTooSmall, HeightTooSmall, WidthTooLarge, HeightTooLarge, PixelFormatInvalid,
		StrideTooSmall, LayoutOverflow, BufferTooSmall, WidthChanged, HeightChanged, FormatChanged,
		TimestampReversed, BlackMaximum, InitialPointsLow, ForwardPointsLow, FbPointsLow,
		AffineCellsLow, AffineSolveFailed, AffineVariationHigh, AffineResidualLow, AffineSupportHigh,
		NonTranslation, ZeroModeAmbiguity, RunnerUpHigh, InliersLow, SpatialRatioLow, SurvivalRatioLow,
		CoverageCells, CoverageInterior, CoverageColumns, CoverageRows, CoverageX, CoverageY,
		JumpHigh, AnchorDisagreement, TemplatesLow, IndependentBlocksLow, MatchScoreLow, MatchUniquenessLow, TargetConflict, RecoveryTimeout, IdentityContradictions, RecoveryFramesLimit, IndependentMotion, IdentitySupportLow, VerificationBudget, Count,
	};
	struct TrackerDebugCheck
	{
		// condition 是原表达式的真值，不代表单项必然导致拒绝；firstFailure 才指向实际失败门。
		bool evaluated = false, condition = false;
		double measured = 0, threshold = 0;
		TrackerDebugComparison comparison = TrackerDebugComparison::Equal;
	};
	struct TrackerDebugBounds
	{
		bool valid = false;
		double minX = 0, minY = 0, maxX = 0, maxY = 0;
	};
	struct TrackerDebugPoint
	{
		MotionVector reference{}, forward{}, refined{}, backward{}, anchor{};
		std::uint64_t birthGeneration = 0;
		int referenceCell = -1, currentCell = -1;
		double fbError = 0, refinementDistance = 0, fitWeight = 0, residual = 0;
		bool forwardComputed = false, forwardStatus = false, forwardInsideEvaluated = false, forwardInside = false;
		bool refinedComputed = false, backwardComputed = false, backwardStatus = false;
		bool backwardFiniteEvaluated = false, backwardFinite = false, fbComputed = false;
		bool refinedInsideEvaluated = false, refinedInside = false, refinementDistanceEvaluated = false;
		bool fbThresholdEvaluated = false, fbValid = false, consensusEvaluated = false, inlier = false;
		bool residualComputed = false, nearStationary = false, competitor = false, retentionEvaluated = false, retained = false, retired = false;
		bool provisional = false, targetMember = false, certified = false;
	};
	struct TrackerDebugAddedPoint
	{
		MotionVector point{}, anchor{};
		std::uint64_t birthGeneration = 0;
		int cell = -1;
		bool committed = false;
	};
	struct TrackerDebugCell
	{
		// 拟合/网格投票按参考点所在格；retained/added 按提交候选中的当前点所在格。
		int reference = 0, forward = 0, fbValid = 0, inliers = 0, competitor = 0, nearStationary = 0;
		int retained = 0, added = 0;
		double fitWeight = 0, inlierWeight = 0, originalCellWeight = 0;
		bool affineSampleValid = false;
		MotionVector affinePosition{}, affineFlow{};
	};
	struct TrackerDebugCandidate
	{
		bool valid = false;
		MotionVector flow{};
		double fitWeight = 0, originalCellRatio = 0, separation = 0;
		int points = 0, cells = 0, interiorCells = 0;
		TrackerDebugBounds referenceBounds{}, currentBounds{};
		bool doubleRadiusEvaluated = false, beyondDoubleRadius = false;
		bool zeroLengthEvaluated = false, zeroLength = false, zeroRatioEvaluated = false, zeroRatio = false;
		bool interiorEvaluated = false, enoughInterior = false;
	};
	struct TrackerDebugPatchPeak
	{
		bool valid = false;
		MotionVector flow{};
		double score = 0;
	};
	struct TrackerDebugPatch
	{
		// 范围与位移均为处理图像像素，和现有 point/bounds 共用 scaleX/scaleY。
		TrackerDebugBounds referenceBounds{}, currentBounds{};
		std::array<TrackerDebugPatchPeak, 3> peaks{};
		std::size_t peakCount = 0;
		double information = 0, change = 0, selectedScore = 0, uniqueness = 0;
		bool targetMember = false, selected = false, independent = false;
	};
	struct TrackerDebugSnapshot
	{
		static constexpr std::size_t MaximumPoints = 320, Columns = 8, Rows = 6;
		std::uint64_t inputSequence = 0, referenceSequenceBefore = 0, referenceSequenceAfter = 0;
		std::uint64_t referenceGenerationBefore = 0, referenceGenerationAfter = 0;
		bool initialization = false, referenceBeforeValid = false, referenceAfterValid = false;
		int inputWidth = 0, inputHeight = 0, processingWidth = 0, processingHeight = 0;
		int referenceProcessingWidth = 0, referenceProcessingHeight = 0;
		std::size_t inputStrideBytes = 0, inputSizeBytes = 0;
		MotionPixelFormat inputFormat = MotionPixelFormat::Gray8;
		double timestampSeconds = 0, scaleX = 0, scaleY = 0;
		double pointBorder = 0, consensusRadius = 0, refinementDistanceLimit = 0, fbErrorLimit = 0, nearStationaryLimit = 0;
		// 与生产零主模式的比较共用，显示端无需另写比例门限。
		double zeroModeRatioThreshold = 0;
		bool captureValid = false;
		std::uint64_t reachedStages = 0, completedStages = 0;
		TrackerDebugCheckId firstFailure = TrackerDebugCheckId::Count;
		MotionReason exception = MotionReason::None;
		std::array<TrackerDebugCheck, static_cast<std::size_t>(TrackerDebugCheckId::Count)> checks{};
		std::array<TrackerDebugPoint, MaximumPoints> points{};
		std::array<TrackerDebugAddedPoint, MaximumPoints> addedPoints{};
		std::array<TrackerDebugCell, Columns * Rows> cells{};
		std::size_t pointCount = 0, addedPointCount = 0;
		bool truncated = false, affineModelValid = false, ambiguityComplete = false, candidateTotalValid = false;
		int consensusCandidatesEvaluated = 0, competitorsEvaluated = 0;
		TrackerDebugCandidate mainMotion{}, strongestCompetitor{}, eligibleRunnerUp{}, rejectingCompetitor{};
		MotionVector totalBefore{}, candidateDelta{}, candidateTotal{};
		MotionTargetPhase targetPhaseBefore = MotionTargetPhase::Discovering, targetPhaseAfter = MotionTargetPhase::Discovering;
		std::uint64_t targetId = 0;
		TrackerDebugBounds targetSupport{};
		std::array<TrackerDebugPatch, 24> patches{};
		std::array<TrackerDebugCandidate, 3> localCandidates{};
		std::size_t patchCount = 0, localCandidateCount = 0;
		// 该结果与返回值完全相同；主共识/候选均不能代替生产实际应用的 delta/total。
		MotionResult result{};
	};

	// 单个会话由调用方串行访问；本类不启动线程，也不持有窗口/取屏资源。
	class VisualMotionTracker
	{
	public:
		VisualMotionTracker();
		~VisualMotionTracker();
		VisualMotionTracker(const VisualMotionTracker&) = delete;
		VisualMotionTracker& operator=(const VisualMotionTracker&) = delete;
		[[nodiscard]] MotionResult Initialize(const MotionFrame&, TrackerDebugSnapshot* debug = nullptr) noexcept;
		[[nodiscard]] MotionResult Process(const MotionFrame&, TrackerDebugSnapshot* debug = nullptr) noexcept;
		void Reset() noexcept;
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
	[[nodiscard]] const char* MotionReasonName(MotionReason) noexcept;
	[[nodiscard]] const char* MotionTargetPhaseName(MotionTargetPhase) noexcept;
	[[nodiscard]] const char* TrackerDebugStageName(TrackerDebugStage) noexcept;
	[[nodiscard]] const char* TrackerDebugCheckName(TrackerDebugCheckId) noexcept;
}
