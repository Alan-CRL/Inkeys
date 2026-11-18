module;

#include <windows.h>
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>
#include "../../Log/Log.h"

module Inkeys.UI.UI3;
import :Lighting;
import :Envelope;
import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Metrics;
import Inkeys.UI.UI3.DirtyRegion;

using namespace std;

namespace Inkeys::UI::UI3
{
namespace
{
	constexpr double BorderLightRadius = 480.0;
	constexpr double BorderCursorFadeInDur = 0.30;
	double ApplyBorderLightSmoothstep(double progress)
	{
		progress = clamp(progress, 0.0, 1.0);
		return progress * progress * (3.0 - 2.0 * progress);
	}
}

UiFrameLightingSnapshot LightingStateMachine::SnapshotFrameLighting() const noexcept
{
	UiFrameLightingSnapshot snapshot;
	snapshot.primaryLight = framePrimaryLight;
	snapshot.primaryRadius = frameLightRadius;
	snapshot.cursorLight = frameCursorLight;
	snapshot.cursorScreenLight = frameCursorScreenLight;
	snapshot.cursorRadius = frameCursorLightRadius;
	snapshot.cursorIntensity = frameCursorLightIntensity;
	snapshot.drawingPenColor = frameDrawingPenColor;
	snapshot.drawingPenColorBlend = frameDrawingPenColorBlend;
	snapshot.drawingLightOpacity = frameDrawingLightOpacity;
	snapshot.primaryLightVisible = framePrimaryLightAnchorInitialized
		&& frameLightRadius > 0.0F;
	snapshot.cursorLightVisible = frameCursorLightVisible
		&& frameCursorLightRadius > 0.0F;
	snapshot.edgeLightingEnabled = frameEdgeLightingEnabled;
	return snapshot;
}

RECT LightingStateMachine::GetFramePrimaryLightDamageBounds() const noexcept
{
	if (!frameEdgeLightingEnabled || !framePrimaryLightAnchorInitialized)
		return {};
	const double padding = RenderingAttribute::pointLightDiffuseExtraWidth
		* frameZoom + RenderingAttribute::dirtyAntialiasPadding;
	const double radius = (std::max)(0.0, static_cast<double>(frameLightRadius)) + padding;
	return RECT(
		static_cast<LONG>(floor(static_cast<double>(framePrimaryLight.x) - radius)),
		static_cast<LONG>(floor(static_cast<double>(framePrimaryLight.y) - radius)),
		static_cast<LONG>(ceil(static_cast<double>(framePrimaryLight.x) + radius)),
		static_cast<LONG>(ceil(static_cast<double>(framePrimaryLight.y) + radius)));
}

RECT LightingStateMachine::GetFrameCursorLightDamageBounds() const noexcept
{
	const bool cursorLightCanContribute = frameCursorLightIntensity > 0.0001F
		|| (frameCursorLightAnimating && frameCursorLightIntensityTarget > 0.0001F);
	if (!frameEdgeLightingEnabled || !cursorLightCanContribute)
		return {};
	const double padding = RenderingAttribute::pointLightDiffuseExtraWidth
		* frameZoom + RenderingAttribute::dirtyAntialiasPadding;
	const double radius = (std::max)(0.0, static_cast<double>(frameCursorLightRadius)) + padding;
	return RECT(
		static_cast<LONG>(floor(static_cast<double>(frameCursorLight.x) - radius)),
		static_cast<LONG>(floor(static_cast<double>(frameCursorLight.y) - radius)),
		static_cast<LONG>(ceil(static_cast<double>(frameCursorLight.x) + radius)),
		static_cast<LONG>(ceil(static_cast<double>(frameCursorLight.y) + radius)));
}

bool LightingStateMachine::PrepareFrameLighting(double animationDtSeconds, POINT layoutScreenOrigin,
	const UiFrameLightingInput& input)
{
	const bool previousEdgeLightingEnabled = frameEdgeLightingEnabled;
	const bool previousPrimaryLightAvailable = framePrimaryLightAnchorInitialized;
	const D2D1_POINT_2F previousPrimaryLight = framePrimaryLight;
	const D2D1_POINT_2F previousCursorLight = frameCursorLight;
	const FLOAT previousPrimaryRadius = frameLightRadius;
	const FLOAT previousCursorRadius = frameCursorLightRadius;
	const FLOAT previousCursorIntensity = frameCursorLightIntensity;
	const COLORREF previousDrawingPenColor = frameDrawingPenColor;
	const double previousDrawingPenColorBlend = frameDrawingPenColorBlend;
	const double previousDrawingLightOpacity = frameDrawingLightOpacity;
	framePrimaryLightChanged = false;
	frameCursorLightChanged = false;
	auto FinishLightingFrame = [&](bool sustainPrimary, bool sustainCursor)
		{
			auto PointChanged = [](D2D1_POINT_2F left, D2D1_POINT_2F right)
				{
					return abs(left.x - right.x) > 0.01F
						|| abs(left.y - right.y) > 0.01F;
				};
			const bool sharedVisualChanged =
				(previousDrawingPenColor & 0x00FFFFFF)
					!= (frameDrawingPenColor & 0x00FFFFFF)
				|| abs(previousDrawingPenColorBlend
					- frameDrawingPenColorBlend) > 0.000001
				|| abs(previousDrawingLightOpacity
					- frameDrawingLightOpacity) > 0.000001;
			const bool previousPrimaryVisible = previousEdgeLightingEnabled
				&& previousPrimaryLightAvailable && previousPrimaryRadius > 0.0F;
			const bool currentPrimaryVisible = frameEdgeLightingEnabled
				&& framePrimaryLightAnchorInitialized && frameLightRadius > 0.0F;
			const bool previousCursorVisible = previousEdgeLightingEnabled
				&& previousCursorIntensity > 0.0001F && previousCursorRadius > 0.0F;
			const bool currentCursorVisible = frameEdgeLightingEnabled
				&& frameCursorLightIntensity > 0.0001F
				&& frameCursorLightRadius > 0.0F;

			framePrimaryLightChanged = sustainPrimary
				|| previousPrimaryVisible != currentPrimaryVisible
				|| ((previousPrimaryVisible || currentPrimaryVisible)
					&& (PointChanged(previousPrimaryLight, framePrimaryLight)
						|| abs(previousPrimaryRadius - frameLightRadius) > 0.01F
						|| sharedVisualChanged));
			frameCursorLightChanged = sustainCursor
				|| previousCursorVisible != currentCursorVisible
				|| ((previousCursorVisible || currentCursorVisible)
					&& (PointChanged(previousCursorLight, frameCursorLight)
						|| abs(previousCursorRadius - frameCursorLightRadius) > 0.01F
						|| abs(previousCursorIntensity
							- frameCursorLightIntensity) > 0.0001F
						|| sharedVisualChanged));
			return framePrimaryLightChanged || frameCursorLightChanged;
		};

	frameCursorLightVisible = false;
	frameDrawingUsesPenColor = false;
	COLORREF desiredDrawingPenColor = frameDrawingPenColorInitialized
		? frameDrawingPenColorTarget : input.initialAccentColor;

	double zoom = frameZoom;
	if (!isfinite(zoom) || zoom <= 0.0) zoom = 0.0;
	frameLightRadius = static_cast<FLOAT>(BorderLightRadius * zoom);
	frameCursorLightRadius = static_cast<FLOAT>(BorderCursorLightRadius * zoom);

	bool edgeLightingEnabled = input.edgeLightingEnabled;
	bool dynamicEdgeLightingEnabled = input.dynamicEdgeLightingEnabled;
	if (!edgeLightingEnabled)
	{
		// 总开关关闭时停止点光计算，基础灰边仍由绘制阶段保留。
		bool needSettlingFrame = frameEdgeLightingEnabled || frameLightingWasAnimating
			|| frameCursorLightIntensity > 0.0001F;
		frameEdgeLightingEnabled = false;
		framePrimaryLightAnchorInitialized = false;
		framePrimaryLightAnimating = false;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget = 0.0F;
		frameCursorLightAnimating = false;
		frameCursorInputAvailable = false;
		frameAnimationStateInitialized = false;
		frameDrawingPenColorAnimating = false;
		frameDrawingPenColorInitialized = false;
		frameDrawingModeTransitionAnimating = false;
		frameDrawingModeInitialized = false;
		frameDrawingPenColorSourceInitialized = false;
		frameDrawingPenColorCarriesHighlighterHistory = false;
		const bool sustainPrimary = framePrimaryLightWasAnimating;
		const bool sustainCursor = frameCursorLightWasAnimating;
		framePrimaryLightWasAnimating = false;
		frameCursorLightWasAnimating = false;
		frameLightingWasAnimating = false;
		return FinishLightingFrame(
			needSettlingFrame && sustainPrimary,
			needSettlingFrame && sustainCursor);
	}
	bool edgeLightingStateChanged = !frameEdgeLightingEnabled;
	frameEdgeLightingEnabled = true;

	int desiredPrimaryAnchor = input.primaryAnchorId;
	D2D1_POINT_2F desiredPrimaryLight = framePrimaryLight;
	bool primaryTargetAvailable = false;
	if (input.usesAccentColor)
		desiredDrawingPenColor = input.accentColor;
	if (input.primaryAnchorAvailable)
	{
		desiredPrimaryLight = D2D1::Point2F(input.primaryAnchor.x * static_cast<FLOAT>(zoom),
			input.primaryAnchor.y * static_cast<FLOAT>(zoom));
		primaryTargetAvailable = true;
	}
	frameDrawingUsesPenColor = input.usesAccentColor;
	const unsigned long long cursorSerial = input.cursorSerial;
	const bool cursorInputAvailable = input.cursorAvailable;
	frameCursorScreenLight = input.cursorScreenPoint;
	frameCursorLight = D2D1::Point2F(frameCursorScreenLight.x - static_cast<FLOAT>(layoutScreenOrigin.x),
		frameCursorScreenLight.y - static_cast<FLOAT>(layoutScreenOrigin.y));

	bool animationEnabled = UiAnimationEnabled;
	double animationSpeedRate = UiAnimationSpeedRate;
	if (!isfinite(animationSpeedRate)) animationSpeedRate = 1.0;
	animationSpeedRate = clamp(animationSpeedRate, 0.1, 5.0);
	if (!isfinite(animationDtSeconds) || animationDtSeconds < 0.0) animationDtSeconds = 0.0;
	animationDtSeconds = clamp(animationDtSeconds, 0.0, 0.05);
	double scaledDtSeconds = animationDtSeconds * animationSpeedRate;

	bool drawingModeTransitionStarted = false;
	double desiredPenColorBlend = frameDrawingUsesPenColor ? 1.0 : 0.0;
	if (!frameDrawingModeInitialized)
	{
		frameDrawingModeInitialized = true;
		frameDrawingMode = input.modeId;
		frameDrawingPenColorBlend = desiredPenColorBlend;
		frameDrawingPenColorBlendStart = desiredPenColorBlend;
		frameDrawingPenColorBlendTarget = desiredPenColorBlend;
		frameDrawingLightOpacity = 1.0;
		frameDrawingLightOpacityStart = 1.0;
	}
	else
	{
		int desiredDrawingMode = input.modeId;
		if (frameDrawingMode != desiredDrawingMode)
			frameDrawingMode = desiredDrawingMode;
		if (frameDrawingPenColorBlendTarget != desiredPenColorBlend)
		{
			// 只有颜色角色改变才淡出换色；同色模式切换仅移动第一光源锚点。
			frameDrawingPenColorBlendStart = frameDrawingPenColorBlend;
			frameDrawingPenColorBlendTarget = desiredPenColorBlend;
			frameDrawingLightOpacityStart = frameDrawingLightOpacity;
			frameDrawingModeTransitionElapsed = 0.0;
			frameDrawingModeTransitionAnimating = animationEnabled;
			drawingModeTransitionStarted = true;
		}
	}

	if (!animationEnabled)
	{
		frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
		frameDrawingLightOpacity = 1.0;
		frameDrawingModeTransitionAnimating = false;
		frameDrawingModeTransitionElapsed = 0.0;
	}
	else if (frameDrawingModeTransitionAnimating)
	{
		double transitionDuration = UiDefaultOperationDur;
		if (!isfinite(transitionDuration) || transitionDuration <= 0.0)
		{
			frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
			frameDrawingLightOpacity = 1.0;
			frameDrawingModeTransitionAnimating = false;
		}
		else
		{
			frameDrawingModeTransitionElapsed += scaledDtSeconds;
			double progress = clamp(
				frameDrawingModeTransitionElapsed / transitionDuration, 0.0, 1.0);
			// 颜色变化压缩在中间 30% 时段，光影在正中完全透明，避免看见颜色互相覆盖。
			double colorProgress = ApplyBorderLightSmoothstep(
				clamp((progress - 0.35) / 0.30, 0.0, 1.0));
			frameDrawingPenColorBlend = frameDrawingPenColorBlendStart
				+ (frameDrawingPenColorBlendTarget - frameDrawingPenColorBlendStart)
				* colorProgress;
			if (progress < 0.5)
			{
				double fadeOutProgress = ApplyBorderLightSmoothstep(progress * 2.0);
				frameDrawingLightOpacity =
					frameDrawingLightOpacityStart * (1.0 - fadeOutProgress);
			}
			else
			{
				frameDrawingLightOpacity = ApplyBorderLightSmoothstep(
					(progress - 0.5) * 2.0);
			}
			if (progress >= 1.0)
			{
				frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
				frameDrawingLightOpacity = 1.0;
				frameDrawingModeTransitionAnimating = false;
			}
		}
	}

	bool penLightColorChanged = false;
	int desiredColorSource = input.colorSourceId;
	if (frameDrawingUsesPenColor)
	{
		bool desiredSourceIsHighlighter = input.colorSourceCarriesHistory;
		bool enteringGeometryFromHighlighter = input.takeOverAccentColor
			&& frameDrawingPenColorCarriesHighlighterHistory;
		if (!frameDrawingPenColorSourceInitialized)
		{
			frameDrawingPenColorSourceInitialized = true;
			frameDrawingPenColorSource = desiredColorSource;
			frameDrawingPenColorCarriesHighlighterHistory =
				desiredSourceIsHighlighter;
		}
		else if (enteringGeometryFromHighlighter)
		{
			frameDrawingPenColorSource = desiredColorSource;
			// Geometry 不继承未完成的 Highlighter -> Brush1 插值，直接接管 Brush1 快照。
			frameDrawingPenColorStart = desiredDrawingPenColor;
			frameDrawingPenColorTarget = desiredDrawingPenColor;
			frameDrawingPenColor = desiredDrawingPenColor;
			frameDrawingPenColorElapsed = 0.0;
			frameDrawingPenColorAnimating = false;
			frameDrawingPenColorInitialized = true;
			frameDrawingPenColorCarriesHighlighterHistory = false;
			penLightColorChanged = true;
		}
		else if (frameDrawingPenColorSource != desiredColorSource)
		{
			frameDrawingPenColorSource = desiredColorSource;
			if (desiredSourceIsHighlighter)
				frameDrawingPenColorCarriesHighlighterHistory = true;
		}
		else if (desiredSourceIsHighlighter)
			frameDrawingPenColorCarriesHighlighterHistory = true;
	}
	if (!frameDrawingPenColorInitialized)
	{
		frameDrawingPenColorInitialized = true;
		frameDrawingPenColorStart = desiredDrawingPenColor;
		frameDrawingPenColorTarget = desiredDrawingPenColor;
		frameDrawingPenColor = desiredDrawingPenColor;
	}
	else if ((frameDrawingPenColorTarget & 0x00FFFFFF) != desiredDrawingPenColor)
	{
		// 连续换色从当前视觉颜色重新起步，避免快速点击色块时发生跳变。
		frameDrawingPenColorStart = frameDrawingPenColor;
		frameDrawingPenColorTarget = desiredDrawingPenColor;
		frameDrawingPenColorElapsed = 0.0;
		frameDrawingPenColorAnimating = animationEnabled && frameDrawingUsesPenColor;
		penLightColorChanged = true;
	}

	if (!animationEnabled || !frameDrawingUsesPenColor)
	{
		frameDrawingPenColor = frameDrawingPenColorTarget;
		frameDrawingPenColorAnimating = false;
		frameDrawingPenColorElapsed = 0.0;
	}
	else if (frameDrawingPenColorAnimating)
	{
		double colorDuration = UiDefaultOperationDur;
		if (!isfinite(colorDuration) || colorDuration <= 0.0)
		{
			frameDrawingPenColor = frameDrawingPenColorTarget;
			frameDrawingPenColorAnimating = false;
		}
		else
		{
			frameDrawingPenColorElapsed += scaledDtSeconds;
			double progress = clamp(frameDrawingPenColorElapsed / colorDuration, 0.0, 1.0);
			double curvedProgress = UiApplyCurve(UiCurveEnum::EaseInOutCubic, progress);
			frameDrawingPenColor = MixUiColor(
				frameDrawingPenColorStart, frameDrawingPenColorTarget, curvedProgress);
			if (progress >= 1.0)
			{
				frameDrawingPenColor = frameDrawingPenColorTarget;
				frameDrawingPenColorAnimating = false;
			}
		}
	}
	if (frameDrawingPenColorCarriesHighlighterHistory
		&& frameDrawingPenColorSource == input.historyResetSourceId
		&& !frameDrawingPenColorAnimating
		&& (frameDrawingPenColor & 0x00FFFFFF)
			== (desiredDrawingPenColor & 0x00FFFFFF))
	{
		// Brush1 真正稳定后才丢弃 Highlighter 历史；中途进入 Geometry 仍会直接接管。
		frameDrawingPenColorCarriesHighlighterHistory = false;
	}

	bool primaryLightMoved = false;
	bool primaryStateChanged = false;
	if (primaryTargetAvailable)
	{
		auto PointsDiffer = [](D2D1_POINT_2F left, D2D1_POINT_2F right)
			{
				return abs(left.x - right.x) > 0.01F || abs(left.y - right.y) > 0.01F;
			};
		D2D1_POINT_2F previousPrimaryLight = framePrimaryLight;
		if (!framePrimaryLightAnchorInitialized)
		{
			framePrimaryLightAnchorInitialized = true;
			framePrimaryLightAnchor = desiredPrimaryAnchor;
			framePrimaryLightStart = desiredPrimaryLight;
			framePrimaryLightTarget = desiredPrimaryLight;
			framePrimaryLight = desiredPrimaryLight;
		}
		else
		{
			bool anchorChanged = framePrimaryLightAnchor != desiredPrimaryAnchor;
			if (anchorChanged)
			{
				framePrimaryLightAnchor = desiredPrimaryAnchor;
				framePrimaryLightStart = framePrimaryLight;
				framePrimaryLightMoveElapsed = 0.0;
				framePrimaryLightAnimating = animationEnabled;
				primaryStateChanged = true;
			}
			framePrimaryLightTarget = desiredPrimaryLight;

			if (!animationEnabled)
			{
				framePrimaryLight = desiredPrimaryLight;
				framePrimaryLightAnimating = false;
				framePrimaryLightMoveElapsed = 0.0;
			}
			else if (framePrimaryLightAnimating)
			{
				double moveDuration = UiDefaultOperationDur;
				if (!isfinite(moveDuration) || moveDuration <= 0.0)
				{
					framePrimaryLight = framePrimaryLightTarget;
					framePrimaryLightAnimating = false;
				}
				else
				{
					framePrimaryLightMoveElapsed += scaledDtSeconds;
					double progress = clamp(framePrimaryLightMoveElapsed / moveDuration, 0.0, 1.0);
					double curvedProgress = UiApplyCurve(
						UiCurveEnum::EaseInOutCubic, progress);
					framePrimaryLight = D2D1::Point2F(
						static_cast<FLOAT>(framePrimaryLightStart.x
							+ (framePrimaryLightTarget.x - framePrimaryLightStart.x) * curvedProgress),
						static_cast<FLOAT>(framePrimaryLightStart.y
							+ (framePrimaryLightTarget.y - framePrimaryLightStart.y) * curvedProgress));
					if (progress >= 1.0)
					{
						framePrimaryLight = framePrimaryLightTarget;
						framePrimaryLightAnimating = false;
					}
				}
			}
			else framePrimaryLight = desiredPrimaryLight;
		}
		primaryLightMoved = PointsDiffer(previousPrimaryLight, framePrimaryLight);
	}

	bool stateChanged = edgeLightingStateChanged || primaryStateChanged || penLightColorChanged
		|| drawingModeTransitionStarted;
	bool cursorFadeRestarted = false;
	bool cursorMoved = false;
	bool desiredCursorLightVisible = animationEnabled
		&& dynamicEdgeLightingEnabled && cursorInputAvailable;
	if (!frameAnimationStateInitialized)
	{
		frameAnimationStateInitialized = true;
		frameLastAnimationEnabled = animationEnabled;
		frameCursorInputAvailable = desiredCursorLightVisible;
		handledBorderCursorLightSerial = cursorSerial;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget =
			desiredCursorLightVisible ? static_cast<FLOAT>(BorderLightIntensity) : 0.0F;
		if (desiredCursorLightVisible)
		{
			frameCursorLightFadeElapsed = 0.0;
			frameCursorLightAnimating = true;
			cursorFadeRestarted = true;
		}
	}
	else
	{
		if (animationEnabled != frameLastAnimationEnabled)
		{
			stateChanged = true;
			frameLastAnimationEnabled = animationEnabled;
		}
		if (desiredCursorLightVisible != frameCursorInputAvailable)
		{
			// 显隐切换从当前强度续接，靠近、超时和重新进入时不会发生亮度跳变。
			frameCursorInputAvailable = desiredCursorLightVisible;
			stateChanged = true;
			handledBorderCursorLightSerial = cursorSerial;
			frameCursorLightIntensityStart = frameCursorLightIntensity;
			frameCursorLightIntensityTarget = desiredCursorLightVisible
				? static_cast<FLOAT>(BorderLightIntensity) : 0.0F;
			frameCursorLightFadeElapsed = 0.0;
			frameCursorLightAnimating = animationEnabled
				&& abs(frameCursorLightIntensityTarget - frameCursorLightIntensityStart) > 0.0001F;
			cursorFadeRestarted = frameCursorLightAnimating;
		}
	}

	if (!animationEnabled)
	{
		// 关闭动画后立即隐藏鼠标光，基础灰边和第一主光保持稳定。
		handledBorderCursorLightSerial = cursorSerial;
		frameCursorInputAvailable = false;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget = 0.0F;
		frameCursorLightAnimating = false;
		bool needSettlingFrame = frameLightingWasAnimating;
		const bool sustainPrimary = framePrimaryLightWasAnimating;
		const bool sustainCursor = frameCursorLightWasAnimating;
		framePrimaryLightWasAnimating = false;
		frameCursorLightWasAnimating = false;
		frameLightingWasAnimating = false;
		return FinishLightingFrame(
			(primaryLightMoved || stateChanged)
				|| (needSettlingFrame && sustainPrimary),
			(stateChanged || (needSettlingFrame && sustainCursor)));
	}

	if (cursorSerial != handledBorderCursorLightSerial)
	{
		handledBorderCursorLightSerial = cursorSerial;
		cursorMoved = desiredCursorLightVisible;
	}

	if (frameCursorLightAnimating)
	{
		if (!cursorFadeRestarted) frameCursorLightFadeElapsed += scaledDtSeconds;
		double progress = frameCursorLightFadeElapsed / BorderCursorFadeInDur;
		double curvedProgress = ApplyBorderLightSmoothstep(progress);
		frameCursorLightIntensity = static_cast<FLOAT>(
			frameCursorLightIntensityStart
			+ (frameCursorLightIntensityTarget - frameCursorLightIntensityStart)
			* curvedProgress);
		if (frameCursorLightFadeElapsed >= BorderCursorFadeInDur)
		{
			frameCursorLightIntensity = frameCursorLightIntensityTarget;
			frameCursorLightAnimating = false;
		}
	}
	else frameCursorLightIntensity = frameCursorLightIntensityTarget;
	frameCursorLightVisible = frameCursorLightIntensity > 0.0001F;

	// 时间过程结束后再绘制一帧最终状态，随后恢复原有静止等待。
	const bool sharedLightingAnimating = frameDrawingPenColorAnimating
		|| frameDrawingModeTransitionAnimating;
	const bool primaryLightingAnimating = framePrimaryLightAnimating
		|| sharedLightingAnimating;
	const bool cursorLightingAnimating = frameCursorLightAnimating
		|| sharedLightingAnimating;
	const bool primarySettlingFrame = framePrimaryLightWasAnimating
		&& !primaryLightingAnimating;
	const bool cursorSettlingFrame = frameCursorLightWasAnimating
		&& !cursorLightingAnimating;
	framePrimaryLightWasAnimating = primaryLightingAnimating;
	frameCursorLightWasAnimating = cursorLightingAnimating;
	bool lightingAnimating = primaryLightingAnimating || cursorLightingAnimating;
	frameLightingWasAnimating = lightingAnimating;
	return FinishLightingFrame(
		primaryLightingAnimating || primarySettlingFrame
			|| primaryStateChanged || primaryLightMoved
			|| penLightColorChanged || drawingModeTransitionStarted
			|| edgeLightingStateChanged,
		cursorLightingAnimating || cursorSettlingFrame || cursorMoved
			|| penLightColorChanged || drawingModeTransitionStarted
			|| edgeLightingStateChanged);
}


	UiFrameLightingSnapshot ResolveSurfaceFrameLightingSnapshot(
		const SurfaceSharedLighting& lighting,const RECT& bounds,LONG outset) noexcept
	{
		UiFrameLightingSnapshot result;
		result.primaryLight=D2D1::Point2F(static_cast<FLOAT>(lighting.primaryScreenX-bounds.left+outset),
			static_cast<FLOAT>(lighting.primaryScreenY-bounds.top+outset));
		result.primaryRadius=static_cast<FLOAT>(lighting.primaryRadiusPixels);
		result.cursorLight=D2D1::Point2F(static_cast<FLOAT>(lighting.cursorScreenX-bounds.left+outset),
			static_cast<FLOAT>(lighting.cursorScreenY-bounds.top+outset));
		result.cursorScreenLight=D2D1::Point2F(static_cast<FLOAT>(lighting.cursorScreenX),static_cast<FLOAT>(lighting.cursorScreenY));
		result.cursorRadius=static_cast<FLOAT>(lighting.cursorRadiusPixels);
		result.cursorIntensity=static_cast<FLOAT>(lighting.cursorIntensity);
		result.drawingPenColor=lighting.drawingPenColor;result.drawingPenColorBlend=lighting.drawingPenColorBlend;
		result.drawingLightOpacity=lighting.drawingLightOpacity;result.primaryLightVisible=lighting.primaryVisible;
		result.cursorLightVisible=lighting.cursorVisible;result.edgeLightingEnabled=lighting.edgeLightingEnabled;
		return result;
	}

namespace
{
	struct LightingSubscriber
	{
		LightingSubscriberToken token;
		std::function<LightingSubscriptionTarget()> queryTarget;
	};
	struct SharedLightingRegistry
	{
		std::mutex lightingMutex;
		SurfaceSharedLighting sharedLighting;
		std::uint64_t lightingRevision = 1;
		LightingSubscriberToken nextToken = 0;
		std::vector<LightingSubscriber> lightingSubscribers;
	};
	SharedLightingRegistry& GetSharedLightingRegistry()
	{
		// 函数内静态保证产品侧静态注册器先构造、先于注册表注销。
		static SharedLightingRegistry registry;
		return registry;
	}
		bool SameLighting(const SurfaceSharedLighting& a,const SurfaceSharedLighting& b) noexcept
		{
			return a.primaryScreenX==b.primaryScreenX && a.primaryScreenY==b.primaryScreenY
				&& a.primaryRadiusPixels==b.primaryRadiusPixels && a.cursorScreenX==b.cursorScreenX
				&& a.cursorScreenY==b.cursorScreenY && a.cursorRadiusPixels==b.cursorRadiusPixels
				&& a.cursorIntensity==b.cursorIntensity && a.drawingPenColor==b.drawingPenColor
				&& a.drawingPenColorBlend==b.drawingPenColorBlend && a.drawingLightOpacity==b.drawingLightOpacity
				&& a.primaryVisible==b.primaryVisible && a.cursorVisible==b.cursorVisible
				&& a.edgeLightingEnabled==b.edgeLightingEnabled;
		}
	bool NeedsLightingUpdate(const LightingSubscriptionTarget& target,
		const SurfaceSharedLighting& before, const SurfaceSharedLighting& after) noexcept
	{
		if(!target.enabled)return false;
		if(target.allChanges)return true;
			const bool colorsChanged=before.drawingPenColor!=after.drawingPenColor
				||before.drawingPenColorBlend!=after.drawingPenColorBlend
				||before.drawingLightOpacity!=after.drawingLightOpacity;
			const bool primaryChanged=colorsChanged||before.primaryScreenX!=after.primaryScreenX
				||before.primaryScreenY!=after.primaryScreenY||before.primaryRadiusPixels!=after.primaryRadiusPixels
				||before.primaryVisible!=after.primaryVisible||before.edgeLightingEnabled!=after.edgeLightingEnabled;
			const bool cursorChanged=colorsChanged||before.cursorScreenX!=after.cursorScreenX
				||before.cursorScreenY!=after.cursorScreenY||before.cursorRadiusPixels!=after.cursorRadiusPixels
				||before.cursorVisible!=after.cursorVisible||before.cursorIntensity!=after.cursorIntensity
				||before.edgeLightingEnabled!=after.edgeLightingEnabled;
			const auto influence=[&](const SurfaceSharedLighting& light,bool cursor){
				if(!light.edgeLightingEnabled||(cursor?(!light.cursorVisible||light.cursorIntensity<=0.0001):!light.primaryVisible))return RECT{};
				const double x=cursor?light.cursorScreenX:light.primaryScreenX;
				const double y=cursor?light.cursorScreenY:light.primaryScreenY;
				const double radius=cursor?light.cursorRadiusPixels:light.primaryRadiusPixels;
				if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(radius)||radius<=0)return RECT{};
				const double reach=radius+target.damageOutsetPixels;
				return RECT{static_cast<LONG>(std::floor(x-reach)),static_cast<LONG>(std::floor(y-reach)),
					static_cast<LONG>(std::ceil(x+reach)),static_cast<LONG>(std::ceil(y+reach))};
			};
			const RECT oldPrimary=primaryChanged?influence(before,false):RECT{};
			const RECT newPrimary=primaryChanged?influence(after,false):RECT{};
			const RECT oldCursor=cursorChanged?influence(before,true):RECT{};
			const RECT newCursor=cursorChanged?influence(after,true):RECT{};
		for(const auto& border:target.borders)
			for(const RECT& light:{oldPrimary,newPrimary,oldCursor,newCursor}){
				const auto damage=ResolveLightBorderDamage(border.outer,border.inner,light);
				if(damage.right>damage.left&&damage.bottom>damage.top)return true;
			}
		return false;
	}
}

LightingSubscriberToken AddSharedLightingSubscriber(
	std::function<LightingSubscriptionTarget()> queryTarget)
{
	auto& registry=GetSharedLightingRegistry();
	LightingSubscriberToken token;std::size_t count;
	{
		std::scoped_lock lock(registry.lightingMutex);
		token=++registry.nextToken;registry.lightingSubscribers.push_back({token,std::move(queryTarget)});
		count=registry.lightingSubscribers.size();
	}
	Inkeys::Log::TryFormat(Inkeys::Log::Level::Info,Inkeys::Log::Module::UI3,
		"光照订阅者登记, token={}, count={}",token,count);
	return token;
}
void RemoveSharedLightingSubscriber(LightingSubscriberToken token) noexcept
{
	if(!token)return;
	auto& registry=GetSharedLightingRegistry();std::size_t count;
	{
		std::scoped_lock lock(registry.lightingMutex);
		if(!std::erase_if(registry.lightingSubscribers,[&](const auto& item){return item.token==token;}))return;
		count=registry.lightingSubscribers.size();
	}
	Inkeys::Log::TryFormat(Inkeys::Log::Level::Info,Inkeys::Log::Module::UI3,
		"光照订阅者注销, token={}, count={}",token,count);
}
SharedLightingSnapshot ReadSharedLighting() noexcept
{
	auto& registry=GetSharedLightingRegistry();
	std::scoped_lock lock(registry.lightingMutex);return {registry.sharedLighting,registry.lightingRevision};
}
void PublishSharedLighting(const SurfaceSharedLighting& value) noexcept
{
	auto& registry=GetSharedLightingRegistry();std::vector<std::function<void()>> targets,globalTargets;
	{
		std::scoped_lock lock(registry.lightingMutex);
		if(SameLighting(registry.sharedLighting,value))return;
		const auto previous=registry.sharedLighting;registry.sharedLighting=value;++registry.lightingRevision;
		for(const auto& subscriber:registry.lightingSubscribers){
			auto target=subscriber.queryTarget();
			if(NeedsLightingUpdate(target,previous,value)&&target.notify)
				(target.allChanges?globalTargets:targets).push_back(std::move(target.notify));
		}
	}
	for(const auto& notify:targets)notify();
	// 全局监听仍在各表面的失效/唤醒之后执行，保持原发布顺序。
	for(const auto& notify:globalTargets)notify();
}

namespace
{
	struct LightingPointerRegistry
	{
		std::mutex mutex;
		// 序号经宿主的窗口消息参数传递，32 位平台上只有 32 位：限定在 32 位内回绕并跳过 0。
		// 待读条目寿命极短（投递后随即被读取或丢弃），回绕不会撞号。
		std::uint32_t nextSerial = 0;
		std::function<void(std::uint64_t)> wake;
		std::vector<LightingPointerNotification> pending;
	};
	LightingPointerRegistry& GetLightingPointerRegistry()
	{
		static LightingPointerRegistry registry;return registry;
	}
}
void SetLightingPublisherWake(std::function<void(std::uint64_t)> wake)
{
	auto& registry=GetLightingPointerRegistry();
	std::scoped_lock lock(registry.mutex);
	registry.wake=std::move(wake);
	// 宿主生命周期切换后不重放旧输入，等价于已销毁消息队列。
	registry.pending.clear();
}
void NotifyLightingPointer(LightingPointerNotification notification) noexcept
{
	auto& registry=GetLightingPointerRegistry();std::function<void(std::uint64_t)> wake;
	{
		std::scoped_lock lock(registry.mutex);
		if(++registry.nextSerial==0)registry.nextSerial=1;
		notification.serial=registry.nextSerial;
		// 无宿主时每个来源只保留末条记录，避免离屏/关闭期间积压无接收者的通知。
		if(!registry.wake)std::erase_if(registry.pending,[&](const auto& item){return item.source==notification.source;});
		registry.pending.push_back(notification);wake=registry.wake;
	}
	if(wake)wake(notification.serial);
}
bool ReadLightingPointerNotification(std::uint64_t serial,
	LightingPointerNotification& notification) noexcept
{
	auto& registry=GetLightingPointerRegistry();std::scoped_lock lock(registry.mutex);
	const auto found=std::find_if(registry.pending.begin(),registry.pending.end(),
		[&](const auto& item){return item.serial==serial;});
	if(found==registry.pending.end())return false;
	notification=*found;registry.pending.erase(found);return true;
}
}
