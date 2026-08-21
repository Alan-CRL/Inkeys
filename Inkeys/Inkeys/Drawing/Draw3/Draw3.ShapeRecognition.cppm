module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Inkeys.Drawing.Draw3.shape_recognition;

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

	std::optional<ShapeCorrectionPlan> BuildShapeCorrectionPlan(
		const InkCanvas& canvas, const CanvasRuntimeHistory& history,
		float dpiScale) noexcept;
}
