module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Inkeys.Drawing.Draw3.shape_recognition;

export import Inkeys.CV.ShapeRecognition;

import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;

export namespace Inkeys::Drawing::Draw3
{
	struct ShapeCorrectionPlan
	{
		std::vector<RenderItemId> sourceItems;
		InkStroke replacement;
		StrokeTileFootprint footprint;
		float confidence = 0.0f;

		ShapeCorrectionPlan(std::vector<RenderItemId> sources,
			InkStroke stroke, StrokeTileFootprint replacementFootprint,
			float score) noexcept;
	};

	struct ShapeRecognitionIdleState
	{
		bool hasDrawingContact = false;
		bool hasGestureContact = false;
		bool hasReconnectCandidate = false;
		bool navigationActive = false;
		bool inputPending = false;
	};

	bool CanAttemptShapeRecognition(ShapeRecognitionIdleState state) noexcept;

	class ShapeRecognitionTriggerLatch
	{
	public:
		void Arm(InkGuid pageGuid, std::uint64_t historyRevision) noexcept;
		void Invalidate() noexcept;
		bool Pending() const noexcept;
		// 条件满足即消费，保证同一轮全抬起最多尝试一次。
		bool ConsumeIfReady(InkGuid pageGuid, std::uint64_t historyRevision,
			ShapeRecognitionIdleState state) noexcept;

	private:
		InkGuid pageGuid_ = {};
		std::uint64_t historyRevision_ = 0;
		bool pending_ = false;
	};

	enum class ShapeCandidateCollectionStopReason : std::uint8_t
	{
		None = 0,
		InvalidDpiScale,
		NoActiveItem,
		MissingHistoryItem,
		InactiveItem,
		EffectivelyHiddenItem,
		ConditionalItem,
		StrokeIndexOutOfRange,
		NonPenStroke,
		InvalidWidth,
		StyleMismatch,
		HiddenBranch,
		HistoryStart,
		MaximumCandidateCount,
		UnexpectedException
	};

	enum class ShapeRecognitionAttemptOutcome : std::uint8_t
	{
		VisionRejected = 0,
		Accepted,
		MissingReplacementWidth,
		InvalidReplacement,
		FootprintFailed,
		UnexpectedException
	};

	struct ShapeRecognitionAttemptDiagnostics
	{
		std::size_t strokeCount = 0;
		std::size_t sourcePointCount = 0;
		InkPixelBounds bounds = {};
		StoredInkStyle style = {};
		float representativeWidth = 0.0f;
		CV::ShapeRecognitionDiagnostics vision = {};
		ShapeRecognitionAttemptOutcome outcome =
			ShapeRecognitionAttemptOutcome::VisionRejected;
	};

	struct ShapeRecognitionDatasetDiagnostics
	{
		ShapeCandidateCollectionStopReason collectionStopReason =
			ShapeCandidateCollectionStopReason::None;
		std::size_t collectedStrokeCount = 0;
		std::size_t collectedPointCount = 0;
		std::vector<ShapeRecognitionAttemptDiagnostics> attempts;
		std::size_t acceptedStrokeCount = 0;
		bool accepted = false;
	};

	void SetShapeRecognitionDiagnosticsEnabled(bool enabled) noexcept;
	bool ShapeRecognitionDiagnosticsEnabled() noexcept;
	const char* ShapeCandidateCollectionStopReasonName(
		ShapeCandidateCollectionStopReason reason) noexcept;
	void WriteShapeRecognitionDatasetDiagnostics(
		const ShapeRecognitionDatasetDiagnostics& diagnostics) noexcept;

	std::optional<ShapeCorrectionPlan> BuildShapeCorrectionPlan(
		const InkCanvas& canvas, const CanvasRuntimeHistory& history,
		float dpiScale,
		ShapeRecognitionDatasetDiagnostics* diagnostics = nullptr) noexcept;
}
