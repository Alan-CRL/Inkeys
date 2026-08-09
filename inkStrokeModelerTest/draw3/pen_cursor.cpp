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
		bool selectedToolIsEraser,
		bool drawingCursorDuringContactEnabled) noexcept
	{
		DrawingCursorVisual visual;
		const DrawingCursorSample* sample = SelectPrimarySample(
			penSample, mouseSample, pointerAuthority);
		if (!sample) return visual;
		const bool pen = sample == &penSample;
		const bool eraser = selectedToolIsEraser || (pen && sample->inverted);
		// 开关只放开普通 Pen/Highlighter 的 Contact；其余设备和橡皮语义保持不变。
		if (!eraser && (!pen ||
			(sample->inContact && !drawingCursorDuringContactEnabled))) return visual;

		visual.visible = true;
		visual.x = sample->x;
		visual.y = sample->y;
		visual.appearance = eraser ? eraserAppearance : selectedAppearance;
		if (eraser && sample->inContact) visual.appearance.opacity = 1.0f;
		if (!IsValidDrawingCursorAppearance(visual.appearance)) visual.visible = false;
		return visual;
	}

	DrawingCursorVisual ResolveLaserDrawingCursorVisual(
		const DrawingCursorSample& penSample,
		const DrawingCursorSample& mouseSample,
		DrawingCursorPointerAuthority pointerAuthority,
		const DrawingCursorAppearance& laserAppearance) noexcept
	{
		DrawingCursorVisual visual;
		const DrawingCursorSample* sample = SelectPrimarySample(
			penSample, mouseSample, pointerAuthority);
		if (!sample || sample->inContact ||
			!IsValidDrawingCursorAppearance(laserAppearance)) return visual;
		visual.visible = true;
		visual.x = sample->x;
		visual.y = sample->y;
		visual.appearance = laserAppearance;
		return visual;
	}

	bool ShouldHideSystemDrawingCursor(DrawingCursorPointerAuthority pointerAuthority,
		bool selectedToolIsEraser, bool selectedToolIsLaser,
		bool penSampleValid, bool mouseSampleValid) noexcept
	{
		switch (pointerAuthority)
		{
		case DrawingCursorPointerAuthority::Pen:
			return true;
		case DrawingCursorPointerAuthority::Mouse:
		case DrawingCursorPointerAuthority::Touch:
			return selectedToolIsEraser || selectedToolIsLaser;
		default:
			return penSampleValid ||
				((selectedToolIsEraser || selectedToolIsLaser) && mouseSampleValid);
		}
	}

	bool ShouldSuppressMouseButtonUpCursorSample(
		DrawingCursorPointerAuthority pointerAuthority) noexcept
	{
		return pointerAuthority == DrawingCursorPointerAuthority::Pen ||
			pointerAuthority == DrawingCursorPointerAuthority::Touch;
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
			!std::isfinite(visual.x) || !std::isfinite(visual.y) ||
			canvasWidth <= 0 || canvasHeight <= 0) return {};
		const double halfWidth = static_cast<double>(visual.appearance.width) * 0.5 +
			kCursorBoundsPaddingPx;
		const double halfHeight = static_cast<double>(visual.appearance.height) * 0.5 +
			kCursorBoundsPaddingPx;
		// 先在 double 域裁到画布，再转换为 LONG，避免极大有限坐标触发未定义行为。
		RECT bounds = {
			static_cast<LONG>(std::clamp(std::floor(static_cast<double>(visual.x) - halfWidth),
				0.0, static_cast<double>(canvasWidth))),
			static_cast<LONG>(std::clamp(std::floor(static_cast<double>(visual.y) - halfHeight),
				0.0, static_cast<double>(canvasHeight))),
			static_cast<LONG>(std::clamp(std::ceil(static_cast<double>(visual.x) + halfWidth),
				0.0, static_cast<double>(canvasWidth))),
			static_cast<LONG>(std::clamp(std::ceil(static_cast<double>(visual.y) + halfHeight),
				0.0, static_cast<double>(canvasHeight)))
		};
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
