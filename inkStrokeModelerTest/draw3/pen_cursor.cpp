module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cmath>

module draw3.pen_cursor;

namespace draw3
{
	namespace
	{
		constexpr float kCursorBoundsPaddingPx = 2.0f;
		constexpr float kMaximumCursorExtentPx = 4096.0f;

		bool FloatEqual(float left, float right) noexcept
		{
			return std::abs(left - right) <= 0.0001f;
		}

		bool AppearanceEqual(const DrawingCursorAppearance& left,
			const DrawingCursorAppearance& right) noexcept
		{
			return left.shape == right.shape &&
				FloatEqual(left.width, right.width) && FloatEqual(left.height, right.height) &&
				FloatEqual(left.red, right.red) && FloatEqual(left.green, right.green) &&
				FloatEqual(left.blue, right.blue) && FloatEqual(left.opacity, right.opacity) &&
				FloatEqual(left.fillAlpha, right.fillAlpha) &&
				FloatEqual(left.outlineWidth, right.outlineWidth) &&
				FloatEqual(left.outlineRed, right.outlineRed) &&
				FloatEqual(left.outlineGreen, right.outlineGreen) &&
				FloatEqual(left.outlineBlue, right.outlineBlue);
		}

		const DrawingCursorSample* SelectPrimarySample(
			const DrawingCursorSample& penSample,
			const DrawingCursorSample& mouseSample,
			DrawingCursorPointerAuthority pointerAuthority) noexcept
		{
			switch (pointerAuthority)
			{
			case DrawingCursorPointerAuthority::Pen:
				return penSample.valid ? &penSample : nullptr;
			case DrawingCursorPointerAuthority::Mouse:
				return mouseSample.valid ? &mouseSample : nullptr;
			case DrawingCursorPointerAuthority::Touch:
				return nullptr;
			default:
				if (penSample.valid) return &penSample;
				return mouseSample.valid ? &mouseSample : nullptr;
			}
		}
	}

	bool DrawingCursorSampleMailbox::Publish(DrawingCursorSample sample) noexcept
	{
		if (!std::isfinite(sample.x) || !std::isfinite(sample.y)) sample.valid = false;
		while (writerLatch_.test_and_set(std::memory_order_acquire)) YieldProcessor();

		const bool changed = valid_.load(std::memory_order_relaxed) != (sample.valid ? 1u : 0u) ||
			inverted_.load(std::memory_order_relaxed) != (sample.inverted ? 1u : 0u) ||
			inContact_.load(std::memory_order_relaxed) != (sample.inContact ? 1u : 0u) ||
			!FloatEqual(x_.load(std::memory_order_relaxed), sample.x) ||
			!FloatEqual(y_.load(std::memory_order_relaxed), sample.y);
		uint64_t sequence = sequence_.load(std::memory_order_relaxed);
		if ((sequence & 1u) != 0) ++sequence;
		sequence_.store(sequence + 1, std::memory_order_release); // 奇数表示跨字段更新中。
		x_.store(sample.x, std::memory_order_relaxed);
		y_.store(sample.y, std::memory_order_relaxed);
		qpc_.store(sample.qpc, std::memory_order_relaxed);
		valid_.store(sample.valid ? 1u : 0u, std::memory_order_relaxed);
		inverted_.store(sample.inverted ? 1u : 0u, std::memory_order_relaxed);
		inContact_.store(sample.inContact ? 1u : 0u, std::memory_order_relaxed);
		sequence_.store(sequence + 2, std::memory_order_release); // 偶数一次性发布一致样本。
		writerLatch_.clear(std::memory_order_release);
		return changed;
	}

	bool DrawingCursorSampleMailbox::Clear() noexcept
	{
		return Publish({});
	}

	bool DrawingCursorSampleMailbox::Read(DrawingCursorSample& sample) const noexcept
	{
		for (int attempt = 0; attempt < 32; ++attempt)
		{
			const uint64_t sequenceBefore = sequence_.load(std::memory_order_acquire);
			if ((sequenceBefore & 1u) != 0)
			{
				YieldProcessor();
				continue;
			}
			DrawingCursorSample candidate;
			candidate.x = x_.load(std::memory_order_relaxed);
			candidate.y = y_.load(std::memory_order_relaxed);
			candidate.qpc = qpc_.load(std::memory_order_relaxed);
			candidate.valid = valid_.load(std::memory_order_relaxed) != 0;
			candidate.inverted = inverted_.load(std::memory_order_relaxed) != 0;
			candidate.inContact = inContact_.load(std::memory_order_relaxed) != 0;
			const uint64_t sequenceAfter = sequence_.load(std::memory_order_acquire);
			if (sequenceBefore == sequenceAfter && (sequenceAfter & 1u) == 0)
			{
				candidate.sequence = sequenceAfter;
				sample = candidate;
				return true;
			}
		}
		return false;
	}

	bool IsValidDrawingCursorAppearance(const DrawingCursorAppearance& appearance) noexcept
	{
		return std::isfinite(appearance.width) && std::isfinite(appearance.height) &&
			std::isfinite(appearance.red) && std::isfinite(appearance.green) &&
			std::isfinite(appearance.blue) && std::isfinite(appearance.opacity) &&
			std::isfinite(appearance.fillAlpha) && std::isfinite(appearance.outlineWidth) &&
			std::isfinite(appearance.outlineRed) && std::isfinite(appearance.outlineGreen) &&
			std::isfinite(appearance.outlineBlue) &&
			appearance.width > 0.0f && appearance.height > 0.0f &&
			appearance.width <= kMaximumCursorExtentPx &&
			appearance.height <= kMaximumCursorExtentPx;
	}

	DrawingCursorVisual ResolvePrimaryDrawingCursorVisual(
		const DrawingCursorSample& penSample,
		const DrawingCursorSample& mouseSample,
		DrawingCursorPointerAuthority pointerAuthority,
		const DrawingCursorAppearance& selectedAppearance,
		const DrawingCursorAppearance& eraserAppearance,
		bool selectedToolIsEraser) noexcept
	{
		DrawingCursorVisual visual;
		const DrawingCursorSample* sample = SelectPrimarySample(
			penSample, mouseSample, pointerAuthority);
		if (!sample) return visual;
		const bool pen = sample == &penSample;
		const bool eraser = selectedToolIsEraser || (pen && sample->inverted);
		if (!eraser && (!pen || sample->inContact)) return visual;

		visual.visible = true;
		visual.x = sample->x;
		visual.y = sample->y;
		visual.appearance = eraser ? eraserAppearance : selectedAppearance;
		if (eraser && sample->inContact) visual.appearance.opacity = 1.0f;
		if (!IsValidDrawingCursorAppearance(visual.appearance)) visual.visible = false;
		return visual;
	}

	bool ShouldHideSystemDrawingCursor(DrawingCursorPointerAuthority pointerAuthority,
		bool selectedToolIsEraser, bool penSampleValid, bool mouseSampleValid) noexcept
	{
		switch (pointerAuthority)
		{
		case DrawingCursorPointerAuthority::Pen:
			return true;
		case DrawingCursorPointerAuthority::Mouse:
		case DrawingCursorPointerAuthority::Touch:
			return selectedToolIsEraser;
		default:
			return penSampleValid || (selectedToolIsEraser && mouseSampleValid);
		}
	}

	DrawingCursorVisual MakeTouchEraserDrawingCursorVisual(float x, float y,
		const DrawingCursorAppearance& eraserAppearance) noexcept
	{
		DrawingCursorVisual visual;
		if (!std::isfinite(x) || !std::isfinite(y) ||
			!IsValidDrawingCursorAppearance(eraserAppearance)) return visual;
		visual.visible = true;
		visual.x = x;
		visual.y = y;
		visual.appearance = eraserAppearance;
		visual.appearance.opacity = 1.0f;
		return visual;
	}

	RECT DrawingCursorVisualBounds(const DrawingCursorVisual& visual,
		int canvasWidth, int canvasHeight) noexcept
	{
		if (!visual.visible || !IsValidDrawingCursorAppearance(visual.appearance) ||
			canvasWidth <= 0 || canvasHeight <= 0) return {};
		const float halfWidth = visual.appearance.width * 0.5f + kCursorBoundsPaddingPx;
		const float halfHeight = visual.appearance.height * 0.5f + kCursorBoundsPaddingPx;
		RECT bounds = {
			static_cast<LONG>(std::floor(visual.x - halfWidth)),
			static_cast<LONG>(std::floor(visual.y - halfHeight)),
			static_cast<LONG>(std::ceil(visual.x + halfWidth)),
			static_cast<LONG>(std::ceil(visual.y + halfHeight))
		};
		bounds.left = std::clamp(bounds.left, 0L, static_cast<LONG>(canvasWidth));
		bounds.top = std::clamp(bounds.top, 0L, static_cast<LONG>(canvasHeight));
		bounds.right = std::clamp(bounds.right, 0L, static_cast<LONG>(canvasWidth));
		bounds.bottom = std::clamp(bounds.bottom, 0L, static_cast<LONG>(canvasHeight));
		return bounds.left < bounds.right && bounds.top < bounds.bottom ? bounds : RECT{};
	}

	bool AreDrawingCursorVisualsEquivalent(
		const DrawingCursorVisual& left, const DrawingCursorVisual& right) noexcept
	{
		if (left.visible != right.visible) return false;
		if (!left.visible) return true;
		return FloatEqual(left.x, right.x) && FloatEqual(left.y, right.y) &&
			AppearanceEqual(left.appearance, right.appearance);
	}
}
