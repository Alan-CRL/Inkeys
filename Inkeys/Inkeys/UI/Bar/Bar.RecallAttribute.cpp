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
		double duration, const BarUiTimelineClass* parent, double dt, double speed) noexcept
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
	void Place(BarUiShapeClass& shape, RecallAttributeRect r)
	{
		shape.x.SetDirect(r.left); shape.y.SetDirect(r.top); shape.w.SetDirect(r.Width()); shape.h.SetDirect(r.Height());
		shape.UpInh(BarUiInheritClass(r.left, r.top));
	}
	D2D1_COLOR_F ThemeBrushColor(BarThemeColorEnum role, float opacity = 1)
	{
		const auto c = GetThemeColor(role);
		return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, opacity);
	}
	void PushOpacity(ID2D1DeviceContext* context, double opacity)
	{
		context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
			D2D1::IdentityMatrix(), static_cast<float>((std::clamp)(opacity, 0.0, 1.0))), nullptr);
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
	surface_.Initialization(0, 0, 0, 0, BarMainBarCornerRadiusDip, BarMainBarCornerRadiusDip, BarButtonFrameThicknessDip,
		GetThemeColor(BarThemeColorEnum::Surface), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	surface_.enable.Initialization(true); surface_.pct.SetDirect(BarDrawAttributeSurfaceOpacity);
	surface_.framePct.emplace(BarMainBarFrameOpacity); surface_.frameLightPct.emplace(BarMainBarFrameOpacity);
	surface_.frameRendering = BarUiFrameRenderingEnum::PointLight; surface_.frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
	// 内置 Redo 与主栏 70x70 同一套 StandardTwoTwo 视觉：圆角/边框/图标/文字/光源全复用。
	const auto metrics = ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
	auto& b = redoButton_; b.hide = false; b.size = BarButtonSizeEnum::twoTwo; b.preset = BarButtonPresetEnum::Redo;
	b.button.Initialization(0, 0, 0, 0, BarButtonCornerRadiusDip, BarButtonCornerRadiusDip, BarButtonFrameThicknessDip,
		GetThemeColor(BarThemeColorEnum::PressedFill), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	b.button.enable.Initialization(true); b.button.pct.SetDirect(0);
	b.button.framePct.emplace(0); b.button.frameLightPct.emplace(0);
	b.button.frameRendering = BarUiFrameRenderingEnum::PointLight; b.button.frameLightColor = BarUiFrameLightColorEnum::Frame;
	b.button.framePrimaryLightEnabled = false; b.button.frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
	const auto label = RedoButtonLabel();
	b.name.Initialization(0, 0, metrics.primarySlotWidthDip, metrics.primarySlotHeightDip, label, metrics.primaryFontSizeDip,
		GetThemeColor(BarThemeColorEnum::TextPrimary));
	b.name.enable.Initialization(!label.empty()); b.name.pct.SetDirect(1);
	b.icon.Initialization(0, 0, GetThemeColor(BarThemeColorEnum::TextPrimary), std::nullopt);
	b.icon.enable.Initialization(true); b.icon.pct.SetDirect(1);
	b.icon.InitializationFromResource(L"UI", L"barRedo");
	b.icon.SetWH(metrics.iconSizeDip, metrics.iconSizeDip);
	b.icon.w.SetDirect(b.icon.w.tar); b.icon.h.SetDirect(b.icon.h.tar);
	initialized_ = true;
}

void BarRecallAttributePanel::ConfigureSurface(BarUiShapeClass& surface, RecallAttributeRect rect, double scale)
{
	Place(surface, rect); surface.rw->SetDirect(BarMainBarCornerRadiusDip * scale); surface.rh->SetDirect(BarMainBarCornerRadiusDip * scale);
	surface.ft->SetDirect(BarButtonFrameThicknessDip * scale);
	surface.fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface)); surface.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
}

bool BarRecallAttributePanel::Advance(BarUISetClass& owner, double dt, double speed, double zoom, UINT dpi,
	RECT workArea, POINT origin, double rigidX, double rigidY, const BarUiTimelineClass* parentTimeline,
	bool dragPlacementLocked)
{
	Initialize(); changed_ = false; active_ = false;
	// 同一份布局快照决定锚点、可见性与弹窗身份；Redo 展开时不能再检查已隐藏的 Undo。
	const auto chrome = owner.RecallChromeView();
	auto anchor = owner.barButtonSet.preset[static_cast<int>(
		chrome.redoExpanded ? BarButtonPresetEnum::Redo : BarButtonPresetEnum::Recall)];
	if (!anchor)
	{
		// 锚点已移除时立即清掉旧面板，避免后续继承几何时解引用空指针。
		Close(owner);
		changed_ = frame_.panelVisible;
		frame_.panelVisible = false;
		return changed_;
	}
	const bool shouldClose = owner.barState.fold ||
		owner.barState.drawAttribute || owner.barState.geometryAttribute || owner.barState.moreExpanded ||
		owner.barState.eraserAttribute || (anchor && !anchor->IsVisible());
	if (shouldClose) Close(owner);
	const bool open = chrome.popupOpen && !shouldClose; visible_ = open;
	const BarUiAnimationAdvanceContextClass context{ dt, speed, static_cast<bool>(BarUiAnimationEnabled), false };
	auto advance = [&](auto& value) { const auto result = BarUiAdvanceAnimation(value, context); changed_ |= result.changed; active_ |= result.active; };
	auto root = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	root->UpInh(BarUiInheritClass(root->x.val - root->w.val / 2, root->y.val - root->h.val / 2));
	auto main = owner.shapeMap[BarUISetShapeEnum::MainBar]; main->Inherit(BarUiInheritEnum::Center, *root);
	anchor->button.Inherit(BarUiInheritEnum::CenterFromTopLeft, *main);
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
	double panelDuration = static_cast<double>(BarUiDefaultOperationDur);
	if (beginSideSwitch)
	{
		const auto durations = ResolveRecallSideSwitchDurations(BarUiDefaultOperationDur, parentTimeline, dt, speed);
		panelDuration = durations.collapse; sideSwitchExpandDuration_ = durations.expand;
		sideSwitchActive_ = true;
	}
	else if (!switching) sideSwitchActive_ = false;
	const BarUiTimelineClass* panelParent = switching ? nullptr : parentTimeline;
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
	next.panelPose = FitRecallSurfacePose(ResolveRecallSurfacePose(layout_.panel, input.anchor, panelMotion_.Geometry(), BarDrawAttributeCompactWidth), layout_.panel, input.work);
	next.geometry = TransformRecallAttributeLayout(layout_, next.panelPose);
	next.panelOpacity = panelMotion_.Opacity();
	next.panelVisible = panelMotion_.Visible() && next.panelOpacity > 0.000001;
	changed_ |= frame_.geometry != next.geometry || frame_.zoom != next.zoom || frame_.panelOpacity != next.panelOpacity ||
		frame_.panelVisible != next.panelVisible ||
		frame_.panelPose.scale != next.panelPose.scale;
	frame_ = next; zoom_ = zoom;
	const auto& g = frame_.geometry;
	changed_ |= surface_.fill->val != GetThemeColor(BarThemeColorEnum::Surface);
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
	// 属性按钮不在主栏注册表中，必须自行推进内容关键帧并保持续帧。
	const bool iconContentAdvanced = b.icon.AdvanceContentTransition(dt, speed);
	const bool nameContentAdvanced = b.name.AdvanceContentTransition(dt, speed);
	changed_ |= iconContentAdvanced || nameContentAdvanced;
	active_ |= iconContentAdvanced || nameContentAdvanced;
	const bool presented = frame_.panelVisible;
	const double scale = frame_.panelPose.scale;
	b.state->state = BarWidgetState::None;
	SetBarButtonPressedVisual(b, commandEnabled && pressed_ == 0);
	UpdateBarButtonHoverVisual(b, presented, commandEnabled, BarButtonHoverFadeDurationSeconds);
	const COLORREF contentColor = ResolveRecallCommandContentColor(commandEnabled,
		GetThemeColor(BarThemeColorEnum::TextPrimary));
	RetargetBarButtonInteractionVisual(b, presented, true, false, BarButtonHoverTransitionDuration, contentColor);
	b.button.frame->SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	Place(b.button, g.items[0]);
	b.button.rw->SetDirect(BarButtonCornerRadiusDip * scale); b.button.rh->SetDirect(BarButtonCornerRadiusDip * scale); b.button.ft->SetDirect(BarButtonFrameThicknessDip * scale);
	const auto metrics = ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
	b.icon.x.SetDirect(0); b.icon.y.SetDirect(metrics.iconOffsetYDip * scale); b.icon.contentScale = scale;
	b.name.x.SetDirect(0); b.name.y.SetDirect(metrics.primaryOffsetYDip * scale);
	b.name.w.SetDirect(metrics.primarySlotWidthDip * scale); b.name.h.SetDirect(metrics.primarySlotHeightDip * scale); b.name.size.SetDirect(metrics.primaryFontSizeDip * scale);
	advance(b.button.pct); advance(*b.button.fill); advance(*b.button.frame); advance(*b.button.frameLightPct);
	advance(b.icon.pct); advance(b.name.pct); advance(*b.icon.color1); advance(b.name.color); advance(b.pressScale);
	return changed_ || active_;
}

void BarRecallAttributePanel::Draw(BarUIRendering& renderer, ID2D1DeviceContext* context)
{
	if (!initialized_ || !frame_.panelVisible) return;
	if (deviceGeneration_ != renderer.GetDeviceGeneration())
	{
		brush_.Reset(); focusStroke_.Reset(); redoButton_.icon.ResetCache(); deviceGeneration_ = renderer.GetDeviceGeneration();
	}
	if (!brush_ && FAILED(context->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_))) return;
	if (!focusStroke_) { ComPtr<ID2D1Factory> factory; context->GetFactory(&factory); auto props = D2D1::StrokeStyleProperties(); props.dashStyle = D2D1_DASH_STYLE_DOT; factory->CreateStrokeStyle(props, nullptr, 0, &focusStroke_); }
	const auto& g = frame_.geometry;
	PushOpacity(context, frame_.panelOpacity); renderer.SetFrameDiffuseMaskGeometryScale(1 / frame_.panelPose.scale);
	DrawBarBackgroundVisual(renderer, context, surface_, BarUiInheritClass(surface_.inhX, surface_.inhY));
	context->PushAxisAlignedClip(PixelRect(g.panel, zoom_), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	DrawBarButtonVisual(renderer, context, redoButton_, BarUiInheritClass(redoButton_.button.inhX, redoButton_.button.inhY));
	if (focused_ == 0)
	{
		brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::TextPrimary));
		auto rect = PixelRect(g.items[0], zoom_); rect.left += 2; rect.right -= 2; rect.top += 2; rect.bottom -= 2;
		context->DrawRoundedRectangle(D2D1::RoundedRect(rect, 4, 4), brush_.Get(), 1, focusStroke_.Get());
	}
	context->PopAxisAlignedClip(); context->PopLayer(); renderer.SetFrameDiffuseMaskGeometryScale(1);
}

RECT BarRecallAttributePanel::Bounds() const
{
	RECT result{}; if (!frame_.panelVisible) return result;
	const int padding = static_cast<int>(ceil((BarButtonFrameThicknessDip * frame_.panelPose.scale + BarRenderingAttribute::pointLightDiffuseExtraWidth) * zoom_)) + BarRenderingAttribute::dirtyAntialiasPadding;
	return IntegerRect(frame_.geometry.panel, zoom_, padding);
}
void BarRecallAttributePanel::CommitPresented()
{
	std::scoped_lock lock(presentationMutex_); presented_ = frame_;
}
RecallAttributePresentation BarRecallAttributePanel::PresentationSnapshot() const
{
	std::scoped_lock lock(presentationMutex_); return presented_;
}
std::array<RECT, 3> BarRecallAttributePanel::PresentedRegions() const
{
	std::scoped_lock lock(presentationMutex_); const auto& f = presented_;
	return { f.panelVisible ? IntegerRect(f.geometry.panel, f.zoom) : RECT{}, RECT{}, RECT{} };
}
void BarRecallAttributePanel::Close(BarUISetClass& owner)
{
	const bool wasOpen = owner.barState.recallAttribute.exchange(false);
	pressed_ = -1; hovered_ = -1; focused_ = -1; visible_ = false;
	// 面板可立即关闭，但主栏尺寸与命令身份仍遵守 3 秒保护。
	if (wasOpen) owner.NoteRecallPopupDismissed();
}
void BarRecallAttributePanel::Execute(BarUISetClass& owner, int item)
{
	if (item != 0) return;
	if (!owner.ActivateRecallPopupCommand()) return;
	Inkeys::UI::Bar::NotifyPptBusinessAction();
	owner.UpdateRendering(false);
}
bool BarRecallAttributePanel::ResetPointerFeedback()
{
	const bool changed = hovered_ >= 0 || pressed_ >= 0;
	hovered_ = -1; pressed_ = -1;
	StopBarButtonHoverVisual(redoButton_, false);
	return changed;
}
bool BarRecallAttributePanel::Pointer(BarUISetClass& owner, const ExMessage& message, bool cancelled, bool contactPointer)
{
	const auto f = PresentationSnapshot();
	if (!f.panelVisible && pressed_ < 0) return false;
	const auto& geometry = f.geometry; const double x = message.x / f.zoom, y = message.y / f.zoom;
	const bool inside = f.panelVisible && geometry.panel.Width() > 0 && geometry.panel.Height() > 0 &&
		BarUiRoundedRectContainsPoint(message.x, message.y, f.zoom, geometry.panel.left, geometry.panel.top,
			geometry.panel.Width(), geometry.panel.Height(), BarMainBarCornerRadiusDip * f.panelPose.scale, BarMainBarCornerRadiusDip * f.panelPose.scale);
	int hit = -1;
	if (inside && IntersectRecallRect(geometry.items[0], geometry.panel).Contains(x, y)) hit = 0;
	if (message.message == WM_MOUSEMOVE)
	{
		if (hovered_ != hit)
		{
			const int old = hovered_; hovered_ = hit; focused_ = -1;
			if (old != hit)
			{
				if (old >= 0) StopBarButtonHoverVisual(redoButton_, false);
				if (hit >= 0) StartBarButtonHoverVisual(redoButton_);
			}
			owner.UpdateRendering(false);
		}
		if (pressed_ >= 0 && cancelled) { pressed_ = -1; owner.UpdateRendering(false); } return inside;
	}
	const bool down = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK;
	if (down)
	{
		// 必须是面板收到的新Down才能建立动作票据；主栏开栏的那次Up不能落到 Redo。
		pressed_ = inside && owner.barState.recallAttribute && !cancelled ? hit : -1;
		focused_ = -1; owner.UpdateRendering(false); return inside;
	}
	if (message.message == WM_LBUTTONUP)
	{
		const int pressed = pressed_;
		pressed_ = -1;
		const int action = ResolveRecallAttributeRelease(pressed, hit, inside, cancelled);
		if (action >= 0 && owner.barState.recallAttribute) Execute(owner, action);
		if (contactPointer) ResetPointerFeedback();
		if (pressed >= 0 || contactPointer) owner.UpdateRendering(false); return inside || pressed >= 0;
	}
	return inside;
}
bool BarRecallAttributePanel::Keyboard(BarUISetClass& owner, BYTE key, bool down)
{
	if (!owner.barState.recallAttribute) return false;
	if (key == VK_ESCAPE)
	{
		if (down) { Close(owner); owner.UpdateRendering(false); } return true;
	}
	if (key != VK_TAB && key != VK_LEFT && key != VK_RIGHT && key != VK_RETURN && key != VK_SPACE) return false;
	if (!down) return true;
	if (key == VK_RETURN || key == VK_SPACE) { Execute(owner, 0); return true; }
	// 单按钮面板：Tab/方向键只聚焦 Redo。
	focused_ = 0; hovered_ = -1; owner.UpdateRendering(false); return true;
}
