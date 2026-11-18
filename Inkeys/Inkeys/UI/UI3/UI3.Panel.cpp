module;

#include <algorithm>
#include <cmath>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include "../../Log/Log.h"

module Inkeys.UI.UI3;
import :Panel;
import :Primitive;
import :Renderer;
import Inkeys.UI.UI3.Animation;

namespace Inkeys::UI::UI3
{
void Panel::SetCandidate(RECT visual, PanelRegion interaction, bool visible) noexcept
{
	PanelState::SetCandidate({{static_cast<double>(visual.left), static_cast<double>(visual.top),
		static_cast<double>(visual.right), static_cast<double>(visual.bottom)}, interaction, visible});
}
RECT Panel::CandidateBounds() const noexcept
{
	const auto& f = Candidate(); const auto& r = f.visualBounds;
	return f.visible ? RECT{static_cast<LONG>(r.left), static_cast<LONG>(r.top), static_cast<LONG>(r.right), static_cast<LONG>(r.bottom)} : RECT{};
}
RECT Panel::PresentedInteractionBounds() const noexcept
{
	const auto& f = Presented(); const auto& r = f.interaction.bounds; const double z = f.interaction.zoom;
	if (f.interaction.geometry == PanelHitGeometry::RoundedWithPixelTolerance && (r.right <= r.left || r.bottom <= r.top)) return {};
	return f.visible ? RECT{static_cast<LONG>(std::floor(r.left * z)), static_cast<LONG>(std::floor(r.top * z)),
		static_cast<LONG>(std::ceil(r.right * z)), static_cast<LONG>(std::ceil(r.bottom * z))} : RECT{};
}
bool Panel::SetTarget(bool open)
{
	if (open == requested_) return false;
	requested_ = open;
	Inkeys::Log::TryFormat(Inkeys::Log::Level::Debug, Inkeys::Log::Module::UI3,
		"Panel target: id={} open={}", identifier_, open);
	return true;
}
bool Panel::Retarget(bool open, const PanelMotionSpec& spec)
{
	if (!SetTarget(open)) return false;
	spec_ = spec;
	const double duration = open ? spec.openDuration : spec.closeDuration;
	const auto geometry = open ? spec.openGeometry : spec.closeGeometry;
	const auto opacity = open ? spec.openOpacity : spec.closeOpacity;
	geometryValue_.SetTar(open ? 1 : 0, duration, std::nullopt, false, {geometry, geometry, 0, false});
	opacityValue_.SetTar(open ? 1 : 0, duration, std::nullopt, false, {opacity, opacity, 0, false});
	return true;
}
void Panel::Advance(const UiAnimationAdvanceContextClass& context)
{
	const double duration = requested_ ? spec_.openDuration : spec_.closeDuration;
	// idle 后的零 dt 首帧保持当前姿态；显式零时长及禁用动画仍立即完成。
	if (context.animationEnabled && !context.forceReplace && duration > 0 &&
		(!std::isfinite(context.dtSeconds) || context.dtSeconds <= 0)) return;
	UiAdvanceAnimation(geometryValue_, context); UiAdvanceAnimation(opacityValue_, context);
	geometry_ = geometryValue_.val;
	opacity_ = (std::clamp)(spec_.opacityFollowsGeometry ? geometry_ : static_cast<double>(opacityValue_.val), 0.0, 1.0);
}
void Panel::SetExternalState(bool open, double geometry, double opacity)
{
	SetTarget(open); geometry_ = geometry; opacity_ = opacity;
}
bool Panel::DrawFrame(UIRendering& renderer, ID2D1DeviceContext* context, const UiInheritClass& inherit,
	RECT* target, bool clip, std::optional<D2D1_RECT_F> contentClip) const
{
	const bool drawn = shape_ && renderer.Shape(context, *shape_, inherit, target, clip);
	// 外框的柔光先画，再裁剪成员；调用方仍在原位置弹出裁剪。
	if (contentClip) context->PushAxisAlignedClip(*contentClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	return drawn;
}

PanelPoseScope::PanelPoseScope(UIRendering& renderer, ID2D1DeviceContext* context, const PanelPose& pose)
	: renderer_(renderer), context_(context), zoom_(renderer.frameZoom), fontZoom_(renderer.fontTargetZoom),
	referenceZoom_(renderer.frameDiffuseReferenceZoom), geometryScale_(renderer.frameDiffuseMaskGeometryScale),
	fontY_(renderer.fontTargetY), fontReady_(renderer.fontTargetTransformReady), edgeAllowed_(renderer.frameEdgeLightingAllowed),
	primaryInitialized_(renderer.framePrimaryLightAnchorInitialized), cursorVisible_(renderer.frameCursorLightVisible),
	lighting_(renderer.SnapshotFrameLighting()), cursorLocal_(renderer.frameLocalCursorLight),
	cursorRadiusX_(renderer.frameLocalCursorLightRadiusX), cursorRadiusY_(renderer.frameLocalCursorLightRadiusY)
{
	context_->GetTransform(&transform_);
	renderer_.SetFrameZoom(pose.zoom); renderer_.SetFontTargetScale(pose.fontTargetScale); renderer_.SetFontTargetZoom(pose.fontTargetZoom);
	renderer_.SetFrameDiffuseReferenceZoom(pose.diffuseReferenceZoom); renderer_.SetFrameDiffuseMaskGeometryScale(pose.diffuseGeometryScale);
	if (pose.lighting) renderer_.SetFrameLightingSnapshot(*pose.lighting, pose.edgeLighting);
	context_->SetTransform(pose.transform);
}
PanelPoseScope::~PanelPoseScope()
{
	context_->SetTransform(transform_);
	renderer_.frameZoom = zoom_; renderer_.fontTargetZoom = fontZoom_; renderer_.fontTargetY = fontY_;
	renderer_.fontTargetTransformReady = fontReady_; renderer_.frameDiffuseReferenceZoom = referenceZoom_;
	renderer_.frameDiffuseMaskGeometryScale = geometryScale_;
	// 原样恢复已保存字段，不再次经过快照 setter 的可见性/半径归一化。
	renderer_.framePrimaryLight = lighting_.primaryLight; renderer_.frameLightRadius = lighting_.primaryRadius;
	renderer_.framePrimaryLightAnchorInitialized = primaryInitialized_;
	renderer_.frameCursorLight = lighting_.cursorLight; renderer_.frameCursorScreenLight = lighting_.cursorScreenLight;
	renderer_.frameCursorLightRadius = lighting_.cursorRadius; renderer_.frameCursorLightIntensity = lighting_.cursorIntensity;
	renderer_.frameCursorLightVisible = cursorVisible_; renderer_.frameEdgeLightingEnabled = lighting_.edgeLightingEnabled;
	renderer_.frameEdgeLightingAllowed = edgeAllowed_;
	renderer_.frameDrawingPenColor = lighting_.drawingPenColor; renderer_.frameDrawingPenColorBlend = lighting_.drawingPenColorBlend;
	renderer_.frameDrawingLightOpacity = lighting_.drawingLightOpacity;
	renderer_.frameLocalCursorLight = cursorLocal_; renderer_.frameLocalCursorLightRadiusX = cursorRadiusX_; renderer_.frameLocalCursorLightRadiusY = cursorRadiusY_;
}
}
