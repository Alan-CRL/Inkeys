module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <mutex>

module draw3.pen_cursor;

namespace draw3
{
	namespace
	{
		constexpr float kCursorBoundsPaddingPx = 2.0f;
		constexpr float kMaximumCursorExtentPx = 4096.0f;
		std::atomic<bool> drawingCursorTraceEnabled = false;
		std::mutex drawingCursorTraceMutex;
		DrawingCursorDiagnosticVisualState drawingCursorDiagnosticVisual = {};

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

		bool DiagnosticVisualStateEqual(
			const DrawingCursorDiagnosticVisualState& left,
			const DrawingCursorDiagnosticVisualState& right) noexcept
		{
			return left.known == right.known && left.visible == right.visible &&
				left.laser == right.laser &&
				left.penSampleValid == right.penSampleValid &&
				left.penSampleInContact == right.penSampleInContact &&
				left.penSampleInverted == right.penSampleInverted &&
				left.mouseSampleValid == right.mouseSampleValid &&
				left.mouseSampleInContact == right.mouseSampleInContact &&
				left.pointerAuthority == right.pointerAuthority &&
				left.reason == right.reason;
		}

		void TraceResolvedDrawingCursor(
			const DrawingCursorSample& penSample,
			const DrawingCursorSample& mouseSample,
			DrawingCursorPointerAuthority pointerAuthority,
			bool laser, bool visible,
			DrawingCursorDiagnosticVisualReason reason) noexcept
		{
			if (!drawingCursorTraceEnabled.load(std::memory_order_acquire)) return;
			DrawingCursorDiagnosticVisualState state;
			state.known = true;
			state.visible = visible;
			state.laser = laser;
			state.penSampleValid = penSample.valid;
			state.penSampleInContact = penSample.inContact;
			state.penSampleInverted = penSample.inverted;
			state.mouseSampleValid = mouseSample.valid;
			state.mouseSampleInContact = mouseSample.inContact;
			state.pointerAuthority = pointerAuthority;
			state.reason = reason;

			std::lock_guard<std::mutex> lock(drawingCursorTraceMutex);
			if (DiagnosticVisualStateEqual(drawingCursorDiagnosticVisual, state)) return;
			drawingCursorDiagnosticVisual = state;
			char line[512] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[CURSOR_TRACE][app] pointerType=%u mode=%s "
				"penSample={valid=%u,contact=%u,inverted=%u} "
				"mouseSample={valid=%u,contact=%u} appVisible=%u reason=%s\r\n",
				static_cast<unsigned>(pointerAuthority), laser ? "laser" : "primary",
				penSample.valid ? 1u : 0u, penSample.inContact ? 1u : 0u,
				penSample.inverted ? 1u : 0u, mouseSample.valid ? 1u : 0u,
				mouseSample.inContact ? 1u : 0u, visible ? 1u : 0u,
				DrawingCursorDiagnosticVisualReasonName(reason));
			if (length > 0)
			{
				std::cout.write(line, (std::min)(
					static_cast<size_t>(length), sizeof(line) - 1));
				std::cout.flush();
			}
		}
	}

	void ConfigureDrawingCursorTrace(bool enabled) noexcept
	{
		if (!enabled)
		{
			drawingCursorTraceEnabled.store(false, std::memory_order_release);
			return;
		}
		{
			std::lock_guard<std::mutex> lock(drawingCursorTraceMutex);
			drawingCursorDiagnosticVisual = {};
		}
		drawingCursorTraceEnabled.store(true, std::memory_order_release);
	}

	bool ReadDrawingCursorDiagnosticVisualState(
		DrawingCursorDiagnosticVisualState& state) noexcept
	{
		if (!drawingCursorTraceEnabled.load(std::memory_order_acquire)) return false;
		std::lock_guard<std::mutex> lock(drawingCursorTraceMutex);
		state = drawingCursorDiagnosticVisual;
		return state.known;
	}

	const char* DrawingCursorDiagnosticVisualReasonName(
		DrawingCursorDiagnosticVisualReason reason) noexcept
	{
		switch (reason)
		{
		case DrawingCursorDiagnosticVisualReason::NoSample: return "no-sample";
		case DrawingCursorDiagnosticVisualReason::TouchAuthority: return "touch-authority";
		case DrawingCursorDiagnosticVisualReason::MouseUsesSystemCursor:
			return "mouse-uses-system-cursor";
		case DrawingCursorDiagnosticVisualReason::PenContactDisabled:
			return "pen-contact-disabled";
		case DrawingCursorDiagnosticVisualReason::InvalidAppearance:
			return "invalid-appearance";
		case DrawingCursorDiagnosticVisualReason::LaserContact: return "laser-contact";
		case DrawingCursorDiagnosticVisualReason::VisiblePen: return "visible-pen";
		case DrawingCursorDiagnosticVisualReason::VisibleMouse: return "visible-mouse";
		case DrawingCursorDiagnosticVisualReason::VisibleEraser: return "visible-eraser";
		case DrawingCursorDiagnosticVisualReason::VisibleLaser: return "visible-laser";
		default: return "none";
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
		bool drawingCursorDuringContactEnabled,
		bool translucentInkCursorEnabled,
		bool mouseUsesSystemCursor) noexcept
	{
		DrawingCursorVisual visual;
		const DrawingCursorSample* sample = SelectPrimarySample(
			penSample, mouseSample, pointerAuthority);
		if (!sample)
		{
			TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
				false, false, pointerAuthority == DrawingCursorPointerAuthority::Touch
				? DrawingCursorDiagnosticVisualReason::TouchAuthority
				: DrawingCursorDiagnosticVisualReason::NoSample);
			return visual;
		}
		const bool pen = sample == &penSample;
		const bool eraser = selectedToolIsEraser || (pen && sample->inverted);
		if (!eraser)
		{
			if (!pen && mouseUsesSystemCursor)
			{
				TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
					false, false,
					DrawingCursorDiagnosticVisualReason::MouseUsesSystemCursor);
				return visual;
			}
			// Contact 开关只放开普通 Pen/Highlighter；Mouse 应用光标不进入该语义。
			if (pen && sample->inContact && !drawingCursorDuringContactEnabled)
			{
				TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
					false, false,
					DrawingCursorDiagnosticVisualReason::PenContactDisabled);
				return visual;
			}
		}

		visual.visible = true;
		visual.x = sample->x;
		visual.y = sample->y;
		visual.appearance = eraser ? eraserAppearance : selectedAppearance;
		if (eraser && sample->inContact) visual.appearance.opacity = 1.0f;
		if (pen && !translucentInkCursorEnabled)
		{
			// 正常模式同时提高整体和填充 Alpha，保留原轮廓、颜色与尺寸。
			visual.appearance.opacity = 1.0f;
			visual.appearance.fillAlpha = 1.0f;
		}
		else if (!pen && !eraser)
		{
			// Mouse 不继承 Ink 半透明模式；使用应用光标时也始终保持正常 Alpha。
			visual.appearance.opacity = 1.0f;
			visual.appearance.fillAlpha = 1.0f;
		}
		if (!IsValidDrawingCursorAppearance(visual.appearance)) visual.visible = false;
		TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
			false, visual.visible, visual.visible
			? eraser ? DrawingCursorDiagnosticVisualReason::VisibleEraser
				: pen ? DrawingCursorDiagnosticVisualReason::VisiblePen
				: DrawingCursorDiagnosticVisualReason::VisibleMouse
			: DrawingCursorDiagnosticVisualReason::InvalidAppearance);
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
		if (!sample)
		{
			TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
				true, false, pointerAuthority == DrawingCursorPointerAuthority::Touch
				? DrawingCursorDiagnosticVisualReason::TouchAuthority
				: DrawingCursorDiagnosticVisualReason::NoSample);
			return visual;
		}
		if (sample->inContact)
		{
			TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
				true, false, DrawingCursorDiagnosticVisualReason::LaserContact);
			return visual;
		}
		if (!IsValidDrawingCursorAppearance(laserAppearance))
		{
			TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
				true, false, DrawingCursorDiagnosticVisualReason::InvalidAppearance);
			return visual;
		}
		visual.visible = true;
		visual.x = sample->x;
		visual.y = sample->y;
		visual.appearance = laserAppearance;
		TraceResolvedDrawingCursor(penSample, mouseSample, pointerAuthority,
			true, true, DrawingCursorDiagnosticVisualReason::VisibleLaser);
		return visual;
	}

	bool ShouldHideSystemDrawingCursor(DrawingCursorPointerAuthority pointerAuthority,
		bool selectedToolIsEraser, bool selectedToolIsLaser,
		bool penSampleValid, bool mouseSampleValid,
		bool mouseUsesSystemCursor) noexcept
	{
		switch (pointerAuthority)
		{
		case DrawingCursorPointerAuthority::Pen:
			return true;
		case DrawingCursorPointerAuthority::Mouse:
			return selectedToolIsEraser || selectedToolIsLaser ||
				(!mouseUsesSystemCursor && mouseSampleValid);
		case DrawingCursorPointerAuthority::Touch:
			return selectedToolIsEraser || selectedToolIsLaser;
		default:
			return penSampleValid || (mouseSampleValid &&
				(selectedToolIsEraser || selectedToolIsLaser || !mouseUsesSystemCursor));
		}
	}

	bool ShouldIgnoreMouseCursorMessage(bool promotedPointerMessage,
		bool pointerApiAvailable, bool penSampleValid) noexcept
	{
		if (promotedPointerMessage) return true;
		// Pointer API 能可靠过滤 Pen 提升消息；剩余 WM_MOUSE* 来自真实鼠标。
		return !pointerApiAvailable && penSampleValid;
	}

	bool ShouldSuppressMouseButtonUpCursorSample(
		DrawingCursorPointerAuthority pointerAuthority) noexcept
	{
		return pointerAuthority == DrawingCursorPointerAuthority::Pen ||
			pointerAuthority == DrawingCursorPointerAuthority::Touch;
	}

	bool ShouldSuppressPenFeedbackForTouchPan(bool touchPanActive,
		bool suppressionLatched, bool penPointer, bool inContact) noexcept
	{
		return penPointer && (suppressionLatched || (touchPanActive && inContact));
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
