module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include <algorithm>
#include <array>
#include <cmath>

module Inkeys.UI.Bar;
import :Main;
import :Theme;

using namespace Inkeys::UI::UI3;

using namespace Inkeys::UI::Bar;
extern const double BarDrawAttributeSurfaceOpacity;
extern const double BarDrawAttributeCompactWidth;

namespace
{
	struct RecallSideSwitchDurations
	{
		double collapse = 0, expand = 0;
	};
	RecallSideSwitchDurations ResolveRecallSideSwitchDurations(
		double duration, const UiTimelineClass* parent, double dt, double speed) noexcept
	{
		duration = std::isfinite(duration) && duration > 0 ? duration : 0;
		if (parent)
		{
			const double observedProgress = parent->GetProgress();
			const double remaining = parent->GetRemainingDuration();
			const double full = observedProgress < 1 ? remaining / (1 - observedProgress) : duration;
			double progress = observedProgress;
			// RenderLoop在调用子面板前已推进父时间线一帧；回退该帧后再加入，避免子面板抢跑。
			if (full > 0 && std::isfinite(dt) && dt > 0 && std::isfinite(speed) && speed > 0)
				progress = (std::max)(0.0, progress - dt * speed / full);
			if (full > 0 && progress <= 0.5)
			{
				// 与绘制属性的0.5关键帧一致：只补父批次尚未走完的前半程，后半程保持完整。
				return { (std::max)(0.0, (0.5 - progress) * full), full / 2 };
			}
		}
		return { duration / 2, duration / 2 };
	}
	D2D1_RECT_F PixelRect(RecallAttributeRect r, double zoom)
	{ return D2D1::RectF(static_cast<float>(r.left * zoom), static_cast<float>(r.top * zoom), static_cast<float>(r.right * zoom), static_cast<float>(r.bottom * zoom)); }
	RECT IntegerRect(RecallAttributeRect r, double zoom, int outset = 0)
	{
		if (r.Width() <= 0 || r.Height() <= 0) return {};
		return { static_cast<LONG>(floor(r.left * zoom)) - outset, static_cast<LONG>(floor(r.top * zoom)) - outset,
			static_cast<LONG>(ceil(r.right * zoom)) + outset, static_cast<LONG>(ceil(r.bottom * zoom)) + outset };
	}
	void Place(UiShapeClass& shape, RecallAttributeRect r)
	{
		shape.x.SetDirect(r.left); shape.y.SetDirect(r.top); shape.w.SetDirect(r.Width()); shape.h.SetDirect(r.Height());
		shape.UpInh(UiInheritClass(r.left, r.top));
	}
	wstring RedoButtonLabel()
	{
		return I18n::getWOr(I18nKey.UI.Bar.MainButtons.RedoLabel, L"重做");
	}
	wstring UndoButtonLabel()
	{
		return I18n::getWOr(I18nKey.UI.Bar.MainButtons.UndoLabel, L"撤回");
	}
}

void BarRecallAttributePanel::Initialize()
{
	if (initialized_) return;
	panel_.BindFrame(surface_);
	surface_.Initialization(0, 0, 0, 0, BarMainBarCornerRadiusDip, BarMainBarCornerRadiusDip, ButtonFrameThicknessDip,
		GetThemeColor(ThemeColorEnum::Surface), GetThemeColor(ThemeColorEnum::SurfaceFrame));
	surface_.enable.Initialization(true); surface_.pct.SetDirect(BarDrawAttributeSurfaceOpacity);
	surface_.framePct.emplace(GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity)); surface_.frameLightPct.emplace(BarMainBarFrameOpacity);
	surface_.frameRendering = UiFrameRenderingEnum::PointLight; surface_.frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
	// 内置 Redo 与主栏 70x70 同一套 StandardTwoTwo 视觉：圆角/边框/图标/文字/光源全复用。
	const auto metrics = ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
	auto& b = redoButton_; b.hide = false; b.size = ButtonSizeEnum::twoTwo; b.preset = BarButtonPresetEnum::Redo;
	b.button.Initialization(0, 0, 0, 0, ButtonCornerRadiusDip, ButtonCornerRadiusDip, ButtonFrameThicknessDip,
		GetThemeColor(ThemeColorEnum::PressedFill), GetThemeColor(ThemeColorEnum::SurfaceFrame));
	b.button.enable.Initialization(true); b.button.pct.SetDirect(0);
	b.button.framePct.emplace(0); b.button.frameLightPct.emplace(0);
	b.button.frameRendering = UiFrameRenderingEnum::PointLight; b.button.frameLightColor = UiFrameLightColorEnum::Frame;
	b.button.framePrimaryLightEnabled = false; b.button.frameCursorLightIntensityScale = ButtonCursorLightIntensity;
	const auto label = RedoButtonLabel();
	b.name.Initialization(0, 0, metrics.primarySlotWidthDip, metrics.primarySlotHeightDip, label, metrics.primaryFontSizeDip,
		GetThemeColor(ThemeColorEnum::TextPrimary));
	b.name.enable.Initialization(!label.empty()); b.name.pct.SetDirect(1);
	b.icon.Initialization(0, 0, GetThemeColor(ThemeColorEnum::TextPrimary), std::nullopt);
	b.icon.enable.Initialization(true); b.icon.pct.SetDirect(1);
	b.icon.InitializationFromResource(L"UI", L"barRedo");
	b.icon.SetWH(metrics.iconSizeDip, metrics.iconSizeDip);
	b.icon.w.SetDirect(b.icon.w.tar); b.icon.h.SetDirect(b.icon.h.tar);
	initialized_ = true;
}

void BarRecallAttributePanel::ConfigureSurface(UiShapeClass& surface, RecallAttributeRect rect, double scale)
{
	Place(surface, rect); surface.rw->SetDirect(BarMainBarCornerRadiusDip * scale); surface.rh->SetDirect(BarMainBarCornerRadiusDip * scale);
	surface.ft->SetDirect(ButtonFrameThicknessDip * scale);
	surface.fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface)); surface.frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
	surface.framePct->SetDirect(GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));
}

bool BarRecallAttributePanel::Advance(BarUISetClass& owner, const RecallChromeView& chrome,
	bool anchorVisible, double dt, double speed, double zoom, UINT dpi,
	RECT workArea, POINT origin, double rigidX, double rigidY, const UiTimelineClass* parentTimeline,
	bool dragPlacementLocked)
{
	Initialize(); changed_ = false; active_ = false;
	// 同一份布局快照决定锚点、可见性与弹窗身份；Redo 展开时不能再检查已隐藏的 Undo。
	auto anchor = owner.barButtonSet.preset[static_cast<int>(
		chrome.redoExpanded ? BarButtonPresetEnum::Redo : BarButtonPresetEnum::Recall)];
	if (!anchor)
	{
		// 锚点已移除时立即清掉旧面板，避免后续继承几何时解引用空指针。
		Close(owner);
		changed_ = frame_.panelVisible;
		frame_.panelVisible = false;
		UpdatePanelFrame(false);
		return changed_;
	}
	const bool shouldClose = owner.barState.fold ||
		owner.barState.drawAttribute || owner.barState.geometryAttribute || owner.barState.moreExpanded ||
		owner.barState.eraserAttribute || !anchorVisible;
	if (shouldClose) Close(owner);
	const bool open = chrome.popupOpen && !shouldClose;
	panel_.SetExternalState(open, panelMotion_.Geometry(), panelMotion_.Opacity());
	const UiAnimationAdvanceContextClass context{ dt, speed, static_cast<bool>(UiAnimationEnabled), false };
	auto advance = [&](auto& value) { const auto result = UiAdvanceAnimation(value, context); changed_ |= result.changed; active_ |= result.active; };
	auto root = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	root->UpInh(UiInheritClass(root->x.val - root->w.val / 2, root->y.val - root->h.val / 2));
	auto main = owner.shapeMap[BarUISetShapeEnum::MainBar]; main->Inherit(UiInheritEnum::Center, *root);
	anchor->button.Inherit(UiInheritEnum::CenterFromTopLeft, *main);
	RecallAttributeLayoutInput currentInput;
	currentInput.main = { main->inhX, main->inhY, main->inhX + main->w.val, main->inhY + main->h.val };
	currentInput.anchor = { anchor->button.inhX, anchor->button.inhY, anchor->button.inhX + anchor->button.w.val, anchor->button.inhY + anchor->button.h.val };
	currentInput.zoom = zoom; currentInput.dpiScale = dpi / 96.0;
	currentInput.work = { (workArea.left - origin.x) / zoom - rigidX, (workArea.top - origin.y) / zoom - rigidY, (workArea.right - origin.x) / zoom - rigidX, (workArea.bottom - origin.y) / zoom - rigidY };
	const bool holdDragPlacement = dragPlacementLocked && open;
	const bool releasedDragPlacement = !holdDragPlacement && dragPlacementLocked_;
	if (holdDragPlacement && !dragPlacementLocked_)
	{
		// HWND 直移期间沿用上一帧的局部几何；松手吸收后再由现有换边动画接管。
		dragLayoutInput_ = hasStableLayoutInput_ ? stableLayoutInput_ : currentInput;
		dragLayoutInput_.below = layout_.below;
		dragLayoutInput_.lockPanelSide = true;
		dragPlacementLocked_ = true; releaseSwitchLayoutLocked_ = false;
	}
	else if (releasedDragPlacement)
	{
		// 松手吸收HWND位移时同步平移旧工作区，避免真实工作区夹取先把面板闪回屏内。
		dragLayoutInput_ = RebaseRecallAttributeLayoutInput(dragLayoutInput_, currentInput.anchor);
		dragLayoutInput_.lockPanelSide = true;
		dragPlacementLocked_ = false;
	}
	const bool targetBelow = holdDragPlacement ? previousBelow_ : static_cast<bool>(owner.barState.widgetPosition.primaryBar);
	// 单按钮面板左右对称，只跟踪上下换边。
	bool switching = targetBelow != previousBelow_;
	if (releasedDragPlacement) releaseSwitchLayoutLocked_ = switching;
	else if (!switching) releaseSwitchLayoutLocked_ = false;
	const bool beginSideSwitch = switching && !sideSwitchActive_;
	double panelDuration = static_cast<double>(UiDefaultOperationDur);
	if (beginSideSwitch)
	{
		const auto durations = ResolveRecallSideSwitchDurations(UiDefaultOperationDur, parentTimeline, dt, speed);
		panelDuration = durations.collapse; sideSwitchExpandDuration_ = durations.expand;
		sideSwitchActive_ = true;
	}
	else if (!switching) sideSwitchActive_ = false;
	const UiTimelineClass* panelParent = switching ? nullptr : parentTimeline;
	changed_ |= panelMotion_.Retarget(open && !switching, panelDuration, panelParent);
	double sideSwitchCarryDt = 0;
	if (switching && context.animationEnabled && std::isfinite(dt) && dt > 0 && std::isfinite(speed) && speed > 0)
	{
		const double collapseSeconds = panelMotion_.Active() ? panelMotion_.Timeline().GetRemainingDuration() / speed : 0;
		sideSwitchCarryDt = (std::max)(0.0, dt - collapseSeconds);
	}
	changed_ |= panelMotion_.Advance(context);
	// 与绘制属性一致，在一个默认时长的前后半段完成收拢与展开。
	if (switching && !panelMotion_.Visible())
	{
		previousBelow_ = targetBelow;
		releaseSwitchLayoutLocked_ = false; sideSwitchActive_ = false;
		changed_ |= panelMotion_.Retarget(open, sideSwitchExpandDuration_);
		if (!context.animationEnabled) { changed_ |= panelMotion_.Advance(context); }
		else if (sideSwitchCarryDt > 0)
		{
			// 帧步跨过透明中点时把余量交给展开段，避免相较绘制属性慢一帧。
			auto carryContext = context; carryContext.dtSeconds = sideSwitchCarryDt;
			changed_ |= panelMotion_.Advance(carryContext);
		}
		switching = false;
	}
	active_ |= panelMotion_.Active();
	const bool holdReleaseSwitchPlacement = switching && releaseSwitchLayoutLocked_;
	if (holdReleaseSwitchPlacement)
		dragLayoutInput_ = RebaseRecallAttributeLayoutInput(dragLayoutInput_, currentInput.anchor);
	RecallAttributeLayoutInput input = (holdDragPlacement || holdReleaseSwitchPlacement) ? dragLayoutInput_ : currentInput;
	if (!holdDragPlacement && !holdReleaseSwitchPlacement)
	{
		input.below = previousBelow_;
		// 收拢到紧凑态之前继续显示原侧，避免 release 首帧被工作区避让直接闪到另一边。
		input.lockPanelSide = switching;
		stableLayoutInput_ = input; hasStableLayoutInput_ = true;
	}
	layout_ = ResolveRecallAttributeLayout(input);
	RecallAttributePresentation next; next.zoom = zoom;
	const auto panelFrame=ResolveBarAttributePanelFrame(panelMotion_,
		{layout_.panel.left,layout_.panel.top,layout_.panel.right,layout_.panel.bottom},
		{input.anchor.left,input.anchor.top,input.anchor.right,input.anchor.bottom},BarDrawAttributeCompactWidth);
	next.panelPose=FitRecallSurfacePose({panelFrame.pose.scale,panelFrame.pose.x,panelFrame.pose.y},layout_.panel,input.work);
	next.geometry=TransformRecallAttributeLayout(layout_,next.panelPose);
	next.panelOpacity=panelFrame.opacity;next.panelVisible=panelFrame.visible;
	changed_ |= frame_.geometry != next.geometry || frame_.zoom != next.zoom || frame_.panelOpacity != next.panelOpacity ||
		frame_.panelVisible != next.panelVisible ||
		frame_.panelPose.scale != next.panelPose.scale;
	frame_ = next; zoom_ = zoom;
	const auto& g = frame_.geometry;
	changed_ |= surface_.fill->val != GetThemeColor(ThemeColorEnum::Surface);
	ConfigureSurface(surface_, g.panel, frame_.panelPose.scale);
	// 浮窗按钮随按住相位在 Undo / Redo 之间切换，不可用时变灰。
	auto& b = redoButton_;
	const bool showsRedo = chrome.popupShowsRedo;
	const bool commandEnabled = showsRedo ? chrome.redoEnabled : chrome.undoEnabled;
	const auto commandPreset = showsRedo ? BarButtonPresetEnum::Redo : BarButtonPresetEnum::Recall;
	const auto iconResource = showsRedo ? L"barRedo" : L"barRecall";
	const auto label = showsRedo ? RedoButtonLabel() : UndoButtonLabel();
	if (b.preset != commandPreset)
	{
		// 命令身份改变时同帧提交图标和文字，不能显示旧命令却执行新命令。
		b.preset = commandPreset;
		b.icon.InitializationFromResource(L"UI", iconResource);
		b.name.SetStringImmediate(label);
		changed_ = true;
	}
	else changed_ |= b.TransitionContent(iconResource, label);
	const bool presented = frame_.panelVisible;
	const double scale = frame_.panelPose.scale;
	const int requestedHover=hovered_.load();
	if(appliedHover_!=requestedHover){
		if(appliedHover_>=0)StopButtonHoverVisual(redoButton_,false);
		if(requestedHover>=0)StartButtonHoverVisual(redoButton_);
		appliedHover_=requestedHover;changed_=true;
	}
	b.state->state = WidgetState::None;
	SetButtonPressedVisual(b, commandEnabled && pressed_ == 0);
	// 与主栏清空/定格共用禁用透明度，仍由现有命令策略处理灰色重复点击。
	RetargetButtonInteractionVisual(b,presented,commandEnabled,false,ButtonHoverTransitionDuration,
		std::nullopt,true,std::nullopt,GetThemeColor(ThemeColorEnum::SurfaceFrame));
	Place(b.button, g.items[0]);
	b.button.rw->SetDirect(ButtonCornerRadiusDip * scale); b.button.rh->SetDirect(ButtonCornerRadiusDip * scale); b.button.ft->SetDirect(ButtonFrameThicknessDip * scale);
	const auto metrics = ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
	b.icon.x.SetDirect(0); b.icon.y.SetDirect(metrics.iconOffsetYDip * scale); b.icon.contentScale = scale;
	b.name.x.SetDirect(0); b.name.y.SetDirect(metrics.primaryOffsetYDip * scale);
	b.name.w.SetDirect(metrics.primarySlotWidthDip * scale); b.name.h.SetDirect(metrics.primarySlotHeightDip * scale); b.name.size.SetDirect(metrics.primaryFontSizeDip * scale);
	b.name.fontTargetSize=metrics.primaryFontSizeDip*panelFrame.targetPose.scale;
	ButtonAnimationContext buttonFrame;
	buttonFrame.dtSeconds=dt;buttonFrame.speedRate=speed;buttonFrame.animationEnabled=UiAnimationEnabled;
	buttonFrame.visible=presented;buttonFrame.hoverAllowed=commandEnabled;
	const auto advanced=AdvanceButtonVisual(b,buttonFrame);
	changed_|=advanced.changed;active_|=advanced.active;
	UpdatePanelFrame(open);
	return changed_ || active_;
}

void BarRecallAttributePanel::Draw(UIRendering& renderer, ID2D1DeviceContext* context)
{
	if (!initialized_ || !frame_.panelVisible) return;
	if (deviceGeneration_ != renderer.GetDeviceGeneration())
	{
		redoButton_.icon.ResetCache(); deviceGeneration_ = renderer.GetDeviceGeneration();
	}
	const auto& g = frame_.geometry;
	renderer.PushPanelOpacity(context,PixelRect(frame_.geometry.panel,zoom_),panel_.Opacity()); renderer.SetFrameDiffuseMaskGeometryScale(1 / frame_.panelPose.scale);
	panel_.DrawFrame(renderer, context, {surface_.inhX, surface_.inhY}, nullptr, false, PixelRect(g.panel, zoom_));
	DrawButtonVisual(renderer, context, redoButton_, UiInheritClass(redoButton_.button.inhX, redoButton_.button.inhY));
	context->PopAxisAlignedClip(); context->PopLayer(); renderer.SetFrameDiffuseMaskGeometryScale(1);
}

void BarRecallAttributePanel::UpdatePanelFrame(bool open)
{
	panel_.SetExternalState(open, panelMotion_.Geometry(), frame_.panelOpacity);
	const int padding = static_cast<int>(ceil((ButtonFrameThicknessDip * frame_.panelPose.scale + RenderingAttribute::pointLightDiffuseExtraWidth) * zoom_)) + RenderingAttribute::dirtyAntialiasPadding;
	const auto& r = frame_.geometry.panel; const double radius = BarMainBarCornerRadiusDip * frame_.panelPose.scale;
	panel_.SetCandidate(IntegerRect(r, zoom_, padding), {{r.left, r.top, r.right, r.bottom}, radius, radius,
		PanelHitGeometry::RoundedWithPixelTolerance, frame_.zoom}, frame_.panelVisible);
}
RECT BarRecallAttributePanel::Bounds() const { return panel_.CandidateBounds(); }
void BarRecallAttributePanel::CommitPresented()
{
	std::scoped_lock lock(presentationMutex_); presented_ = frame_; panel_.CommitPresented();
}
RecallAttributePresentation BarRecallAttributePanel::PresentationSnapshot() const
{
	std::scoped_lock lock(presentationMutex_); return presented_;
}
std::array<RECT, 3> BarRecallAttributePanel::PresentedRegions() const
{
	std::scoped_lock lock(presentationMutex_);
	return { panel_.PresentedInteractionBounds(), RECT{}, RECT{} };
}
void BarRecallAttributePanel::Close(BarUISetClass& owner)
{
	const bool wasOpen = owner.barState.recallAttribute.exchange(false);
	pressed_ = -1; hovered_ = -1;
	// 面板可立即关闭，但主栏尺寸与命令身份仍遵守 3 秒保护。
	if (wasOpen) owner.NoteRecallPopupDismissed();
}
bool BarRecallAttributePanel::Execute(BarUISetClass& owner, int item)
{
	if (item != 0) return false;
	if (!owner.ActivateRecallPopupCommand()) return false;
	Inkeys::UI::Bar::NotifyPptBusinessAction();
	owner.UpdateRendering(false);
	return true;
}
bool BarRecallAttributePanel::ResetPointerFeedback()
{
	const bool changed = hovered_ >= 0 || pressed_ >= 0;
	hovered_ = -1; pressed_ = -1;
	return changed;
}
bool BarRecallAttributePanel::Pointer(BarUISetClass& owner, const ExMessage& message, bool cancelled, bool contactPointer, bool* accepted)
{
	if (accepted) *accepted = false;
	RecallAttributePresentation f; PanelFrame panelFrame;
	{ std::scoped_lock lock(presentationMutex_); f = presented_; panelFrame = panel_.Presented(); }
	if(cancelled){const bool owned=ResetPointerFeedback();if(owned)owner.UpdateRendering(false);return owned;}
	if (!f.panelVisible && pressed_ < 0) return false;
	const auto& geometry = f.geometry; const double x = message.x / f.zoom, y = message.y / f.zoom;
	const bool inside = owner.PanelOwnsPoint(BarPanelId::RecallAttribute, panelFrame, message.x, message.y);
	int hit = -1;
	if (inside && IntersectRecallRect(geometry.items[0], geometry.panel).Contains(x, y)) hit = 0;
	if (message.message == WM_MOUSEMOVE)
	{
		if (hovered_ != hit)
		{
			// 输入只发布目标，悬停/按压动画统一由 Advance 在渲染线程消费。
			hovered_ = hit;
			owner.UpdateRendering(false);
		}
		if (pressed_ >= 0 && cancelled) { pressed_ = -1; owner.UpdateRendering(false); } return inside;
	}
	const bool down = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK;
	if (down)
	{
		// 必须是面板收到的新Down才能建立动作票据；主栏开栏的那次Up不能落到 Redo。
		pressed_ = inside && owner.barState.recallAttribute && !cancelled ? hit : -1;
		owner.UpdateRendering(false); return inside;
	}
	if (message.message == WM_LBUTTONUP)
	{
		const int pressed = pressed_;
		pressed_ = -1;
		const int action = ResolveRecallAttributeRelease(pressed, hit, inside, cancelled);
		if (action >= 0 && owner.barState.recallAttribute) { const bool performed = Execute(owner, action); if (accepted) *accepted = performed; }
		if (contactPointer) ResetPointerFeedback();
		if (pressed >= 0 || contactPointer) owner.UpdateRendering(false); return inside || pressed >= 0;
	}
	return inside;
}
