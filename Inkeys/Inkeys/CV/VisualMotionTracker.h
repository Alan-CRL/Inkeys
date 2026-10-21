#pragma once

#include <cstddef>
#include <cstdint>
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
	};
	struct MotionVector
	{
		double x = 0, y = 0;
	};
	enum class MotionState : std::uint8_t { Uninitialized, Tracking, Paused };
	enum class MotionReason : std::uint8_t
	{
		None, Initialized, NotInitialized, InvalidImage, InvalidTimestamp, GeometryChanged,
		InvalidCapture, BlackFrame, InsufficientFeatures, ForwardBackward,
		InconsistentTranslation, InsufficientCoverage, NonTranslation, AmbiguousMotion,
		JumpTooLarge, Paused, OpenCvException, UnexpectedException,
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

	// 单个会话由调用方串行访问；本类不启动线程，也不持有窗口/取屏资源。
	class VisualMotionTracker
	{
	public:
		VisualMotionTracker();
		~VisualMotionTracker();
		VisualMotionTracker(const VisualMotionTracker&) = delete;
		VisualMotionTracker& operator=(const VisualMotionTracker&) = delete;
		[[nodiscard]] MotionResult Initialize(const MotionFrame&) noexcept;
		[[nodiscard]] MotionResult Process(const MotionFrame&) noexcept;
		void Reset() noexcept;
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
	[[nodiscard]] const char* MotionReasonName(MotionReason) noexcept;
}
