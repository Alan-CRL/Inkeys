module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1_1helper.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <cmath>
#include <optional>
#include <utility>
#include "../../Log/Log.h"
module Inkeys.UI.UI3;
import :ColorPicker;
import :Button;
import :Primitive;
import :Renderer;
import :Theme;
import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Interaction;
import Inkeys.UI.UI3.Progress;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.UI3.PresentationProbe;
using namespace std;
namespace Inkeys::UI::UI3
{
	ColorPickerHsv ConvertColorPickerRgbToHsv(COLORREF color)
	{
		double red = static_cast<double>(GetRValue(color)) / 255.0;
		double green = static_cast<double>(GetGValue(color)) / 255.0;
		double blue = static_cast<double>(GetBValue(color)) / 255.0;
		double maximum = (std::max)((std::max)(red, green), blue);
		double minimum = (std::min)((std::min)(red, green), blue);
		double delta = maximum - minimum;

		ColorPickerHsv hsv;
		hsv.value = maximum;
		hsv.saturation = maximum <= 0.0 ? 0.0 : delta / maximum;
		if (delta <= 0.000001) return hsv;
		if (maximum == red)
			hsv.hue = fmod((green - blue) / delta, 6.0) / 6.0;
		else if (maximum == green)
			hsv.hue = ((blue - red) / delta + 2.0) / 6.0;
		else hsv.hue = ((red - green) / delta + 4.0) / 6.0;
		if (hsv.hue < 0.0) hsv.hue += 1.0;
		return hsv;
	}

	COLORREF ConvertColorPickerHsvToRgb(
		double hue, double saturation, double value)
	{
		hue = hue - floor(hue);
		saturation = clamp(saturation, 0.0, 1.0);
		value = clamp(value, 0.0, 1.0);
		double sector = hue * 6.0;
		int index = static_cast<int>(floor(sector)) % 6;
		double fraction = sector - floor(sector);
		double p = value * (1.0 - saturation);
		double q = value * (1.0 - fraction * saturation);
		double t = value * (1.0 - (1.0 - fraction) * saturation);
		double red = 0.0, green = 0.0, blue = 0.0;
		switch (index)
		{
		case 0: red = value; green = t; blue = p; break;
		case 1: red = q; green = value; blue = p; break;
		case 2: red = p; green = value; blue = t; break;
		case 3: red = p; green = q; blue = value; break;
		case 4: red = t; green = p; blue = value; break;
		default: red = value; green = p; blue = q; break;
		}
		auto Channel = [](double channel)
			{
				return static_cast<BYTE>(clamp(
					lround(channel * 255.0), 0L, 255L));
			};
		return RGB(Channel(red), Channel(green), Channel(blue));
	}

	COLORREF GetColorPickerColor(double x, double y, bool darkTone,
		bool openBelowSwatch)
	{
		x = x - floor(x);
		y = clamp(y, 0.0, 1.0);
		// 黑/白端始终远离入口；上下展开时只翻转纵向色义，不翻转内容。
		double farProgress = openBelowSwatch ? y : 1.0 - y;
		return darkTone
			? ConvertColorPickerHsvToRgb(x, 1.0, 1.0 - farProgress)
			: ConvertColorPickerHsvToRgb(x, 1.0 - farProgress, 1.0);
	}

	bool ProjectColorPickerColor(COLORREF color, bool darkTone,
		bool openBelowSwatch, double& x, double& y, bool& exact)
	{
		ColorPickerHsv hsv = ConvertColorPickerRgbToHsv(color);
		x = hsv.hue;
		double farProgress = 0.0;
		if (darkTone)
		{
			farProgress = 1.0 - hsv.value;
			exact = hsv.saturation >= 1.0 - 0.5 / 255.0
				|| hsv.value <= 0.5 / 255.0;
		}
		else
		{
			farProgress = 1.0 - hsv.saturation;
			exact = hsv.value >= 1.0 - 0.5 / 255.0;
		}
		y = openBelowSwatch ? farProgress : 1.0 - farProgress;
		return true;
	}
namespace
{
 constexpr double ColorPickerPaletteInset = 5, ColorPickerPaletteTop = 40;
 constexpr double ColorPickerPaletteWidth = 290, ColorPickerPaletteHeight = 132;
 constexpr double ColorPickerChromeHeight = 30, ColorPickerChromeTop = 5;
 constexpr double ColorPickerSurfaceOpacity = .95;
 void InitPickerShape(UiShapeClass& shape, double radius, optional<COLORREF> fill, optional<COLORREF> frame = nullopt)
 {
  shape.Initialization(0, 0, 1, 1, radius, radius, 1., fill, frame);
  shape.enable.Initialization(true); shape.pct.SetDirect(0);
 }
}
ColorPicker::ColorPicker(bool defaultDarkTone, UiCurveEnum feedbackCurve)
 : panel_(make_shared<UiShapeClass>()), palette_(make_shared<UiShapeClass>()),
 preview_(make_shared<UiShapeClass>()), hold_(make_shared<UiShapeClass>())
{
 colorPickerDarkTone = defaultDarkTone;
 // 两处原有提示/色系曲线不同，场景显式保留，开合几何仍统一为 Back。
 toneMix.curve = holdOpacity.curve = holdRingOpacity.curve = holdTextMix.curve = feedbackCurve;
 InitPickerShape(*panel_, 8, GetThemeColor(ThemeColorEnum::Surface), GetThemeColor(ThemeColorEnum::SurfaceFrame));
 panel_->framePct.emplace(0); panel_->frameLightPct.emplace(0);
 panel_->themeSurface = true; panel_->frameRendering = UiFrameRenderingEnum::PointLight;
 panel_->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing; panel_->framePrimaryLightEnabled = true;
 InitPickerShape(*palette_, 4, nullopt); InitPickerShape(*preview_, 4, 0);
 InitPickerShape(*hold_, 4, GetThemeColor(ThemeColorEnum::Surface));
 for (auto* b : {&tone_, &close_})
 {
  InitPickerShape(b->button, 4, GetThemeColor(ThemeColorEnum::PressedFill));
  b->size = ButtonSizeEnum::oneOne;
  b->icon.Initialization(0, 0, GetThemeColor(ThemeColorEnum::IconPrimary), nullopt);
  b->name.Initialization(0, 0, 30, 30, L"", 12, GetThemeColor(ThemeColorEnum::TextPrimary));
  b->name.enable.Initialization(true);
 }
 for (auto& word : words_) { word.Initialization(0, 0, 1, 1, L"0", 13, GetThemeColor(ThemeColorEnum::TextPrimary)); word.enable.Initialization(true); word.pct.SetDirect(0); }
 holdWord_.Initialization(0, 0, 1, 1, L"", 12, GetThemeColor(ThemeColorEnum::TextPrimary)); holdWord_.enable.Initialization(true);
 // 按钮背景由组件拥有；别名 shared_ptr 只用于宿主既有的呈现命中登记。
 shapes_ = {panel_, palette_, shared_ptr<UiShapeClass>(panel_, &tone_.button), shared_ptr<UiShapeClass>(panel_, &close_.button), preview_, hold_};
}
void ColorPicker::SetLabels(const std::array<std::wstring, 4>& labels, const std::wstring& holdLabel)
{
 for (size_t i = 0; i < 4; ++i) words_[i].SetStringImmediate(labels[i]);
 holdWord_.SetStringImmediate(holdLabel);
}
double ColorPicker::Opacity() const noexcept { return (std::clamp)(static_cast<double>(progress.val), 0., 1.); }
void ColorPicker::Retarget(bool open, COLORREF color, double opacityPercent, double duration, bool localPreview)
{
 lock_guard lock(gestureMutex_);
 if (open && !open_) Inkeys::Log::TryFormat(Inkeys::Log::Level::Debug, Inkeys::Log::Module::UI3, "颜色选择器打开");
 open_ = open;
 const auto curve = open ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseInBack;
 progress.SetTar(open ? 1. : 0., .4, nullopt, false, {curve, curve, 0, false});
 toneMix.SetTar(colorPickerDarkTone ? 1. : 0., .4);
 const bool hint = colorPickerHoldHintActive || colorPickerHoldLocked;
 holdOpacity.SetTar(hint ? 1. : 0., .18);
 holdRingOpacity.SetTar(colorPickerHoldHintActive && !colorPickerHoldLocked ? 1. : 0., .18);
 holdTextMix.SetTar(colorPickerHoldLocked ? 1. : 0., .18);
 const bool keepLocal = localPreview && colorPickerPointerPressed;
 const COLORREF shown = keepLocal ? static_cast<COLORREF>(previewColor_) : color;
 if (!keepLocal) previewColor_ = color;
 // 样式栏这一帧的 marker 和预览必须来自同一次输入采样，Draw 不再追读 worker。
 renderedFeedback_ = {shown, static_cast<double>(colorPickerMarkerX), static_cast<double>(colorPickerMarkerY), static_cast<bool>(colorPickerMarkerVisible)};
 const array<double, 4> values{double(GetRValue(shown)), double(GetGValue(shown)), double(GetBValue(shown)), (std::clamp)(opacityPercent, 0., 100.)};
 for (size_t i = 0; i < 4; ++i)
  if (!displayInitialized_ || colorPickerPointerPressed) display_[i].SetDirect(values[i]); else display_[i].SetTar(values[i], duration);
 displayInitialized_ = true;
 for (auto& word : words_) word.color.SetTar(GetThemeColor(ThemeColorEnum::TextPrimary));
 if (tone_.icon.color1) tone_.icon.color1->SetTar(GetThemeColor(ThemeColorEnum::IconPrimary));
}
ButtonAnimationResult ColorPicker::Advance(const ButtonAnimationContext& animation, bool deferButtons)
{
 ButtonAnimationResult result;
 buttonsDeferred_ = deferButtons; buttonAnimation_ = animation;
 const UiAnimationAdvanceContextClass context{animation.dtSeconds, animation.speedRate, animation.animationEnabled, animation.forceReplace};
 for (auto* value : {&progress, &toneMix, &holdOpacity, &holdRingOpacity, &holdTextMix, &display_[0], &display_[1], &display_[2], &display_[3]})
 {
  const auto step = UiAdvanceAnimation(*value, context); result.changed |= step.changed; result.active |= step.active;
  if (animation.observe) animation.observe(panel_.get(), ButtonAnimationPart::Shape, ButtonAnimationProperty::Geometry, step.changed, step.active, value->IsSame());
 }
 if (!deferButtons) for (auto* button : {&tone_, &close_})
 {
  const auto step = AdvanceButtonVisual(*button, animation); result.changed |= step.changed; result.active |= step.active;
 }
 for (auto& word : words_) {
  const auto step = UiAdvanceAnimation(word.color, context); result.changed |= step.changed; result.active |= step.active;
  if (animation.observe) animation.observe(panel_.get(), ButtonAnimationPart::Primary, ButtonAnimationProperty::Color, step.changed, step.active, word.color.IsSame());
 }
 if (deferButtons) for (auto* button : {&tone_, &close_}) {
  const bool pressed = button == &tone_ ? static_cast<bool>(colorPickerTonePress) : static_cast<bool>(colorPickerClosePress);
  result.active |= !button->pressScale.IsSame() || !button->button.pct.IsSame()
   || (button->state->emph == WidgetEmphasize::Pressed) != pressed;
 }
 result.active |= colorPickerPointerPressed && (colorPickerHoldHintActive || colorPickerHoldLocked);
 return result;
}
void ColorPicker::ProjectColor(COLORREF color, bool preset, bool below, bool quantize)
{
 lock_guard lock(gestureMutex_);
 double x = 0, y = 0; bool exact = false;
 ProjectColorPickerColor(color, colorPickerDarkTone, below, x, y, exact);
 colorPickerMarkerX = quantize ? static_cast<double>(static_cast<float>(x)) : x;
 colorPickerMarkerY = quantize ? static_cast<double>(static_cast<float>(y)) : y;
 colorPickerMarkerVisible = exact && !preset;
 if (!colorPickerPointerPressed) previewColor_ = color;
}
void ColorPicker::ApplyPoint(const ColorPickerPointer& input)
{
 // 串行化预览与取消，避免渲染侧关闭时释放输入协程仍在使用的回调。
 lock_guard lock(gestureMutex_);
 if (!colorPickerPointerPressed) return;
 const auto r = input.frame.palette.boundsDip;
 if (colorPickerHoldLocked || r.right <= r.left || r.bottom <= r.top) return;
 const double x = (std::clamp)((input.x-r.left)/(r.right-r.left), 0., 1.);
 const double y = (std::clamp)((input.y-r.top)/(r.bottom-r.top), 0., 1.);
 colorPickerMarkerX = input.quantizeMarker ? static_cast<double>(static_cast<float>(x)) : x;
 colorPickerMarkerY = input.quantizeMarker ? static_cast<double>(static_cast<float>(y)) : y;
 colorPickerPointerY = static_cast<float>(input.y); colorPickerMarkerVisible = true;
 COLORREF color = GetColorPickerColor(x >= 1 ? 0 : x, y, input.frame.darkTone, input.frame.below);
 if (callbacks_.preview) color = callbacks_.preview(color);
 previewColor_ = color; gestureColor_ = color;
}
void ColorPicker::PointerDown(const ColorPickerPointer& input, ColorPickerCallbacks callbacks)
{
 lock_guard gestureLock(gestureMutex_);
 callbacks_ = std::move(callbacks);
 colorPickerPointerPressed = true; colorPickerHoldLocked = colorPickerHoldHintActive = false; colorPickerHoldProgress = 0;
 stablePoint_ = input.screen;
 HoldProgressConfig config; config.delay = chrono::milliseconds(500); config.duration = chrono::milliseconds(1500);
 config.fadeIn = config.fadeOut = chrono::milliseconds(180);
 config.curve = input.holdCurve; holdConfig_ = config; holdProgress_.Start(input.now, config); ringFailed_ = false;
 ApplyPoint(input);
}
void ColorPicker::PointerMove(const ColorPickerPointer& input) { if (colorPickerPointerPressed) ApplyPoint(input); }
bool ColorPicker::TickHold(POINT screen, HoldProgressState::Time now, double thresholdScale)
{
 lock_guard lock(gestureMutex_);
 if (!colorPickerPointerPressed || colorPickerHoldLocked) return false;
 if (hypot(double(screen.x-stablePoint_.x), double(screen.y-stablePoint_.y)) > 5*(std::max)(1., thresholdScale))
 {
  stablePoint_ = screen;
  holdProgress_.Start(now, holdConfig_); colorPickerHoldHintActive = false; colorPickerHoldProgress = 0; return true;
 }
 const auto sample = holdProgress_.Sample(now);
 if (!sample.visible) return false;
 colorPickerHoldHintActive = true; colorPickerHoldProgress = static_cast<float>(sample.progress);
 if (sample.due) colorPickerHoldLocked = true;
 return true;
}
void ColorPicker::ResetHoldIfMoved(POINT screen, HoldProgressState::Time now, double thresholdScale)
{
 lock_guard lock(gestureMutex_);
 if (!colorPickerPointerPressed || colorPickerHoldLocked) return;
 if (hypot(double(screen.x-stablePoint_.x), double(screen.y-stablePoint_.y)) > 5*(std::max)(1., thresholdScale))
 { stablePoint_ = screen; holdProgress_.Start(now, holdConfig_); colorPickerHoldHintActive = false; colorPickerHoldProgress = 0; }
}
void ColorPicker::Reset()
{
 Cancel(); open_ = false; progress.SetDirect(0); holdOpacity.SetDirect(0); holdRingOpacity.SetDirect(0); holdTextMix.SetDirect(0);
 holdWasVisible_ = false; displayInitialized_ = false;
}
void ColorPicker::SampleHold(HoldProgressState::Time now)
{
 lock_guard lock(gestureMutex_);
 if (!colorPickerPointerPressed) return;
 const auto sample = holdProgress_.Sample(now);
 colorPickerHoldProgress = static_cast<float>(sample.progress);
 if (sample.due) colorPickerHoldLocked = true;
 colorPickerHoldHintActive = sample.visible;
}
void ColorPicker::PointerUp(bool captureOwned)
{
 ColorPickerCallbacks callbacks; COLORREF color = 0;
 {
  lock_guard lock(gestureMutex_);
  if (!colorPickerPointerPressed) { callbacks_ = {}; return; }
  colorPickerPointerPressed = colorPickerHoldHintActive = colorPickerHoldLocked = false;
  colorPickerHoldProgress = 0;
  callbacks = std::move(callbacks_); callbacks_ = {}; color = gestureColor_;
 }
 // 业务提交离开组件锁，样式栏回调可继续使用宿主自己的输入锁。
 if (captureOwned) {
  Inkeys::Log::TryFormat(Inkeys::Log::Level::Debug, Inkeys::Log::Module::UI3, "颜色选择器提交 color={:06X}", static_cast<unsigned>(color)&0xFFFFFF);
  if (callbacks.commit) callbacks.commit(color);
 } else {
  Inkeys::Log::TryFormat(Inkeys::Log::Level::Debug, Inkeys::Log::Module::UI3, "颜色选择器取消");
  if (callbacks.cancel) callbacks.cancel();
 }
}
void ColorPicker::Cancel()
{
 if (colorPickerPointerPressed) PointerUp(false);
 lock_guard lock(gestureMutex_);
 colorPickerTonePress = colorPickerClosePress = false; buttonPointer_.Reset();
 SetHover(ColorPickerPart::Outside);
}
void ColorPicker::Activate(ColorPickerPart part)
{
 if (part == ColorPickerPart::Tone) colorPickerDarkTone = !static_cast<bool>(colorPickerDarkTone);
 if (part == ColorPickerPart::Close && closeRequest_) closeRequest_();
}
ButtonPointerResult ColorPicker::ButtonInput(ButtonPointerEvent event, ColorPickerPart part)
{
 lock_guard lock(gestureMutex_);
 const auto hit = part == ColorPickerPart::Tone || part == ColorPickerPart::Close ? static_cast<uint64_t>(part) : 0;
 const auto result = buttonPointer_.Update(event, hit);
 colorPickerTonePress = result.pressed == static_cast<uint64_t>(ColorPickerPart::Tone);
 colorPickerClosePress = result.pressed == static_cast<uint64_t>(ColorPickerPart::Close);
 SetHover(static_cast<ColorPickerPart>(result.hover));
 return result;
}
void ColorPicker::SetHover(ColorPickerPart part)
{
 lock_guard lock(gestureMutex_);
 // 同一目标的连续 Move 不重启悬停/淡出；只有离开原目标才安排一次退场。
 if (part == hoverPart_) return;
 for (auto pair : {std::pair{&tone_, ColorPickerPart::Tone}, std::pair{&close_, ColorPickerPart::Close}})
  if (pair.second == part) StartButtonHoverVisual(*pair.first);
  else if (pair.second == hoverPart_) StopButtonHoverVisual(*pair.first, false);
 hoverPart_ = part;
}
VisualSampler ColorPicker::MakeVisualSampler(VisualSamplerContext context) const
{
 return [this, context = std::move(context)](VisualSample& sample)
 {
  const double zoom = context.zoom ? context.zoom() : 1.0;
  RECT bounds{}; bool visible = false;
  // 色盘、标记、读数、按钮都画在面板内；预览气泡与保持提示可越出面板，单独并入。
  for (const auto* shape : { panel_.get(), preview_.get(), hold_.get() })
   if (shape && IsVisuallyDrawn(*shape)) { visible = true; RenderingAttribute::UnionRectInPlace(bounds, RenderingAttribute::GetWeigetRect(*shape, zoom)); }
  sample.visible = visible;
  if (!visible) return;
  Detail::GrowForPress(bounds, (std::max)(tone_.pressScale.val, close_.pressScale.val));
  sample.bounds = context.map ? context.map(bounds) : bounds;
  auto& w = sample.state;
  w.Real(zoom);
  for (const auto* value : { &progress, &toneMix, &holdOpacity, &holdRingOpacity, &holdTextMix }) w.Real(value->val);
  for (const auto& value : display_) w.Real(value.val);
  w.Bool(open_); w.Bool(below_); w.Bool(colorPickerDarkTone); w.Bool(colorPickerMarkerVisible);
  w.Real(colorPickerMarkerX); w.Real(colorPickerMarkerY); w.Color(previewColor_);
  w.Real(static_cast<double>(static_cast<float>(colorPickerHoldProgress))); w.Bool(colorPickerPointerPressed);
  w.Bool(colorPickerHoldHintActive); w.Bool(colorPickerHoldLocked); w.Bool(iconDark_);
  w.Real(placement_.scale); w.Real(placement_.cursorLightIntensityScale); w.Real(placement_.lightWeight);
  for (const auto* shape : { panel_.get(), palette_.get(), preview_.get(), hold_.get() }) if (shape) WriteVisualState(w, *shape);
  for (const auto* button : { &tone_, &close_ })
  {
   w.Real(button->pressScale.val);
   // 按钮继承位置在绘制时才解析，且完全由面板布局（已采样）决定，这里只采样局部状态。
   WriteVisualState(w, button->button, false); WriteVisualState(w, button->icon, false); WriteVisualState(w, button->name, false);
  }
  for (const auto& word : words_) WriteVisualState(w, word);
  WriteVisualState(w, holdWord_);
  if (context.extra) context.extra(w);
 };
}

std::shared_ptr<UiShapeClass> ColorPicker::Shape(ColorPickerPart part) const
{
 const int index = part == ColorPickerPart::Panel ? 0 : part == ColorPickerPart::Palette ? 1 : part == ColorPickerPart::Tone ? 2 : part == ColorPickerPart::Close ? 3 : part == ColorPickerPart::Preview ? 4 : part == ColorPickerPart::HoldHint ? 5 : -1;
 return index < 0 ? nullptr : shapes_[index];
}
ColorPickerSnapshot ColorPicker::Snapshot() const noexcept
{
 return {ResolveShapeHitSnapshot(*panel_, {panel_->inhX, panel_->inhY}), ResolveShapeHitSnapshot(*palette_, {palette_->inhX, palette_->inhY}),
 ResolveShapeHitSnapshot(tone_.button, {tone_.button.inhX, tone_.button.inhY}), ResolveShapeHitSnapshot(close_.button, {close_.button.inhX, close_.button.inhY}),
 static_cast<bool>(colorPickerDarkTone), static_cast<bool>(below_)};
}
ColorPickerPart ColorPicker::Hit(const ColorPickerSnapshot& frame, double x, double y) noexcept
{
 if (HitTestButton(frame.close, x, y)) return ColorPickerPart::Close;
 if (HitTestButton(frame.tone, x, y)) return ColorPickerPart::Tone;
 if (HitTestButton(frame.palette, x, y)) return ColorPickerPart::Palette;
 return HitTestButton(frame.panel, x, y) ? ColorPickerPart::Panel : ColorPickerPart::Outside;
}
void ColorPicker::CommitPresented(const ColorPickerSnapshot& frame)
{
 lock_guard lock(presentedMutex_); presented_ = frame;
}
ColorPickerSnapshot ColorPicker::Presented() const
{
 lock_guard lock(presentedMutex_); return presented_;
}
void ColorPicker::ResetDeviceCaches() { tone_.icon.ResetCache(); close_.icon.ResetCache(); }

void ColorPicker::Layout(UIRendering& renderer, const ColorPickerPlacement& placement)
{
 placement_ = placement; below_ = placement.below;
 if (placement.hostOpacity && !colorPickerPointerPressed) {
  lock_guard lock(gestureMutex_);
  renderedFeedback_.x = colorPickerMarkerX; renderedFeedback_.y = colorPickerMarkerY;
  renderedFeedback_.markerVisible = colorPickerMarkerVisible;
 }
 auto pickerPanel = panel_, pickerPalette = palette_, pickerToneHit = shapes_[2], pickerCloseHit = shapes_[3], pickerPreview = preview_, pickerHold = hold_;
 const double pickerScale = placement.scale, pickerWidth = placement.width, pickerHeight = placement.height;
 const double pickerLeft = placement.left, pickerTop = placement.top;
 const double pickerOpacity = placement.hostOpacity ? 1. : Opacity();
 const double fontPickerTargetScale = placement.hostOpacity ? 1. : (std::max)(.01, 60./370.+(1.-60./370.)*static_cast<double>(progress.tar));
 const auto colorPickerHoldTextSize = renderer.MeasureText(holdWord_.TargetContent(), 12, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterTextSize = renderer.MeasureText(L"Ag", 13, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterRgbValueSize = renderer.MeasureText(L"255", 13, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterOpacityValueSize = renderer.MeasureText(L"100%", 13, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterRLabelSize = renderer.MeasureText(words_[0].TargetContent(), 13, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterGLabelSize = renderer.MeasureText(words_[1].TargetContent(), 13, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterBLabelSize = renderer.MeasureText(words_[2].TargetContent(), 13, DWRITE_FONT_WEIGHT_NORMAL);
 const auto colorPickerFooterOpacityLabelSize = renderer.MeasureText(words_[3].TargetContent(), 13, DWRITE_FONT_WEIGHT_NORMAL);
 panel_->fill->SetDirect(GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::Surface));
 panel_->frame->SetDirect(GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::SurfaceFrame));
 panel_->frameCursorLightIntensityScale = placement.cursorLightIntensityScale; panel_->lightMaterial = placement.lightWeight;
 hold_->fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));
 preview_->lightMaterial = hold_->lightMaterial = placement.lightWeight;
 if (placement_.hostOpacity) for (auto& word : words_) word.color.SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));
	// 面板圆角/底色/边框透明度与绘制属性栏保持同一套 Surface 规范。
	double pickerPanelRadius = 8.0 * pickerScale;
	double pickerControlRadius = 4.0 * pickerScale;
	pickerPanel->w.SetDirect(pickerWidth);
	pickerPanel->h.SetDirect(pickerHeight);
	pickerPanel->rw->SetDirect(pickerPanelRadius);
	pickerPanel->rh->SetDirect(pickerPanelRadius);
	pickerPanel->pct.SetDirect(
		ColorPickerSurfaceOpacity * pickerOpacity);
	pickerPanel->framePct->SetDirect(0.18 * pickerOpacity);
	pickerPanel->frameLightPct->SetDirect(pickerOpacity);
	pickerPanel->UpInh(UiInheritClass(pickerLeft, pickerTop));

	auto SetAbsoluteHit = [&](const shared_ptr<UiShapeClass>& hit,
		double left, double top, double width, double height)
		{
			hit->w.SetDirect((std::max)(0.0, width));
			hit->h.SetDirect((std::max)(0.0, height));
			hit->pct.SetDirect(pickerOpacity);
			hit->UpInh(UiInheritClass(left, top));
		};
	double paletteLeft = pickerLeft
		+ ColorPickerPaletteInset * pickerScale;
	double paletteTop = pickerTop
		+ ColorPickerPaletteTop * pickerScale;
	double paletteWidth = ColorPickerPaletteWidth * pickerScale;
	double paletteHeight = ColorPickerPaletteHeight * pickerScale;
	SetAbsoluteHit(pickerPalette, paletteLeft, paletteTop,
		paletteWidth, paletteHeight);
	pickerPalette->rw->SetDirect(pickerControlRadius);
	pickerPalette->rh->SetDirect(pickerControlRadius);
	// 顶部色系/预览/关闭与粗细快速调节按钮同高（30px）。
	double chromeHeight = ColorPickerChromeHeight * pickerScale;
	double chromeTop = pickerTop + ColorPickerChromeTop * pickerScale;

 for (auto pair : {std::pair{&tone_, 5.}, std::pair{&close_, 265.}})
 {
  auto& button = *pair.first;
  button.button.w.SetDirect(chromeHeight); button.button.h.SetDirect(chromeHeight);
  button.button.rw->SetDirect(pickerControlRadius); button.button.rh->SetDirect(pickerControlRadius);
  button.button.UpInh({pickerLeft+pair.second*pickerScale, chromeTop});
  const bool pressed = pair.first == &tone_ ? static_cast<bool>(colorPickerTonePress) : static_cast<bool>(colorPickerClosePress);
  SetButtonPressedVisual(button, pressed);
  // 主栏旧背景按 Pct 的默认速度推进；样式栏原来显式使用 0.12s，不能随按压缩放一并改时长。
  const double backgroundDuration = placement_.buttonBackgroundDuration.value_or(
   std::abs((pressed ? ButtonPressedOpacity : 0.)-static_cast<double>(button.button.pct.val))/static_cast<double>(button.button.pct.des));
  const bool buttonVisible = placement_.keepButtonsVisibleDuringClose ? Visible() : static_cast<bool>(open_);
  RetargetButtonInteractionVisual(placement_.lightWeight < .5, button, buttonVisible, true, false, backgroundDuration, GetThemeColor(ThemeColorEnum::IconPrimary), false);
  button.name.pct.SetDirect(pickerOpacity);
  button.button.lightMaterial = placement_.lightWeight;
  if (buttonsDeferred_) (void)AdvanceButtonVisual(button, buttonAnimation_);
  if (placement_.hostOpacity) {
   button.button.framePct.emplace(0); button.button.frameLightPct.emplace(0);
   button.button.frameRendering = UiFrameRenderingEnum::PointLight; button.button.frameLightColor = UiFrameLightColorEnum::Frame;
   button.button.framePrimaryLightEnabled = false; button.button.frameCursorLightIntensityScale = ButtonCursorLightIntensity;
  }
 }

 if (!iconReady_ || iconDark_ != static_cast<bool>(colorPickerDarkTone)) {
  tone_.icon.InitializationFromResource(L"UI", colorPickerDarkTone ? L"colorMoon" : L"colorSun");
  tone_.icon.enable.Initialization(true); tone_.icon.SetWH(20, 20); iconDark_ = colorPickerDarkTone; iconReady_ = true;
 }
 tone_.icon.w.SetDirect(20*pickerScale); tone_.icon.h.SetDirect(20*pickerScale); tone_.icon.pct.SetDirect(pickerOpacity);
 tone_.icon.x.SetDirect(0); tone_.icon.y.SetDirect(0);
 tone_.icon.Inherit(UiInheritEnum::Center, tone_.button);
 if (placement_.hostOpacity) tone_.icon.color1->SetDirect(GetThemeColor(ThemeColorEnum::IconPrimary));

	int displayR = static_cast<int>(lround(clamp(
		static_cast<double>(display_[0].val), 0.0, 255.0)));
	int displayG = static_cast<int>(lround(clamp(
		static_cast<double>(display_[1].val), 0.0, 255.0)));
	int displayB = static_cast<int>(lround(clamp(
		static_cast<double>(display_[2].val), 0.0, 255.0)));
	int displayOpacity = static_cast<int>(lround(clamp(
		static_cast<double>(display_[3].val),
		0.0, 100.0)));
	double footerTop = paletteTop + paletteHeight;
	// 文字区域覆盖色板下沿到面板下沿，连同上下留白一起做竖直居中。
	double footerBottom = pickerTop + pickerHeight;
	double footerHeight = (std::max)(22.0 * pickerScale, footerBottom - footerTop);
	auto pickerRgbWord = &words_[0];
	auto pickerGWord = &words_[1];
	auto pickerBWord = &words_[2];
	auto pickerOpacityWord = &words_[3];
	auto pickerRgbValueWord = &words_[4];
	auto pickerGValueWord = &words_[5];
	auto pickerBValueWord = &words_[6];
	auto pickerOpacityValueWord = &words_[7];
	// 用页脚实际竖向留白推导横向边距，避免与色板 5 DIP 内缩绑定。
	double footerOuterPadding = clamp(
		(footerHeight - colorPickerFooterTextSize.height * pickerScale) / 2.0,
		0.0, footerHeight / 2.0);
	double footerRgbValueW = colorPickerFooterRgbValueSize.width * pickerScale;
	double footerOpacityValueW =
		colorPickerFooterOpacityValueSize.width * pickerScale;
	double footerRgbLabelW = (std::max)(
		colorPickerFooterRLabelSize.width,
		(std::max)(colorPickerFooterGLabelSize.width,
			colorPickerFooterBLabelSize.width)) * pickerScale;
	double footerOpacityLabelW =
		colorPickerFooterOpacityLabelSize.width * pickerScale;
	// 先保留四组标签和值，再把剩余宽度分配给内部与列间距，长译文不会挤入 RGB 列。
	double footerSpacingBudget = (std::max)(0.0, pickerWidth
		- footerOuterPadding * 2.0
		- footerRgbLabelW * 3.0 - footerRgbValueW * 3.0
		- footerOpacityLabelW - footerOpacityValueW);
	double footerLabelValueGap = (std::min)(
		3.0 * pickerScale, footerSpacingBudget / 4.0);
	footerSpacingBudget -= footerLabelValueGap * 4.0;
	double footerColumnGap = (std::min)(
		6.0 * pickerScale, (std::max)(0.0, footerSpacingBudget / 2.0));
	double footerRgbColW = footerRgbLabelW
		+ footerLabelValueGap + footerRgbValueW;
	double footerOpacityColW = footerOpacityLabelW
		+ footerLabelValueGap + footerOpacityValueW;
	double footerRX = pickerLeft + footerOuterPadding;
	double footerGX = footerRX + footerRgbColW + footerColumnGap;
	double footerBX = footerGX + footerRgbColW + footerColumnGap;
	double footerOpacityX = pickerLeft + pickerWidth
		- footerOuterPadding - footerOpacityColW;
	pickerRgbValueWord->content.SetVal(to_wstring(displayR));
	pickerRgbValueWord->content.SetTar(to_wstring(displayR));
	pickerGValueWord->content.SetVal(to_wstring(displayG));
	pickerGValueWord->content.SetTar(to_wstring(displayG));
	pickerBValueWord->content.SetVal(to_wstring(displayB));
	pickerBValueWord->content.SetTar(to_wstring(displayB));
	wstring opacityValue = to_wstring(displayOpacity) + L"%";
	pickerOpacityValueWord->content.SetVal(opacityValue);
	pickerOpacityValueWord->content.SetTar(opacityValue);
	auto LayoutPickerFooterWord = [&](UiWordClass& word,
		double wordX, double wordW)
		{
			word.w.SetDirect(wordW);
			word.h.SetDirect(footerHeight);
			word.size.SetDirect(13.0 * pickerScale);
			word.fontTargetSize=13.0*fontPickerTargetScale;
			word.pct.SetDirect(pickerOpacity);
			word.UpInh(UiInheritClass(wordX, footerTop));
		};
	LayoutPickerFooterWord(*pickerRgbWord, footerRX, footerRgbLabelW);
	LayoutPickerFooterWord(*pickerGWord, footerGX, footerRgbLabelW);
	LayoutPickerFooterWord(*pickerBWord, footerBX, footerRgbLabelW);
	LayoutPickerFooterWord(*pickerOpacityWord, footerOpacityX,
		footerOpacityLabelW);
	LayoutPickerFooterWord(*pickerRgbValueWord,
		footerRX + footerRgbLabelW + footerLabelValueGap,
		footerRgbValueW);
	LayoutPickerFooterWord(*pickerGValueWord,
		footerGX + footerRgbLabelW + footerLabelValueGap,
		footerRgbValueW);
	LayoutPickerFooterWord(*pickerBValueWord,
		footerBX + footerRgbLabelW + footerLabelValueGap,
		footerRgbValueW);
	LayoutPickerFooterWord(*pickerOpacityValueWord,
		footerOpacityX + footerOpacityLabelW + footerLabelValueGap,
		footerOpacityValueW);

	// 保持提示只在本次显现开始时决定侧边；隐藏途中不再跟手换边。
	double holdOpacity = clamp(static_cast<double>(
		this->holdOpacity.val) * pickerOpacity, 0.0, 1.0);
	// 宽度只装文字与环形进度条：5 左距 + 文字 + 5 间隔 + 环 14 + 5 右距。
	double holdWidth = (std::min)(paletteWidth - 10.0 * pickerScale,
		static_cast<double>(colorPickerHoldTextSize.width)
			+ 29.0 * pickerScale);
	double holdHeight = 28.0 * pickerScale;
	bool holdTargetActive =
		colorPickerHoldHintActive
		|| colorPickerHoldLocked;
	if (holdTargetActive && !holdWasVisible_)
	{
		bool pointerOnTopHalf = placement_.hostOpacity ? renderedFeedback_.y < .5 : static_cast<double>(colorPickerPointerY) < paletteTop + paletteHeight / 2.0;
		holdOnTop_ = !pointerOnTopHalf;
	}
	holdWasVisible_ = holdTargetActive;
	double holdLeft = paletteLeft + (paletteWidth - holdWidth) / 2.0;
	double holdTop = holdOnTop_
		? paletteTop + ColorPickerPaletteInset * pickerScale
		: paletteTop + paletteHeight - holdHeight
			- ColorPickerPaletteInset * pickerScale;
	SetAbsoluteHit(pickerHold, holdLeft, holdTop, holdWidth, holdHeight);
	pickerHold->pct.SetDirect(0.82 * holdOpacity);
	pickerHold->rw->SetDirect(pickerControlRadius);
	pickerHold->rh->SetDirect(pickerControlRadius);
	auto pickerHoldWord = &holdWord_;
	// 文字与右侧环之间留 5px，紧靠但不相撞。
	pickerHoldWord->w.SetDirect(holdWidth - 24.0 * pickerScale);
	pickerHoldWord->h.SetDirect(holdHeight);
	pickerHoldWord->size.SetDirect(12.0 * pickerScale);
	pickerHoldWord->fontTargetSize=12.0*fontPickerTargetScale;
	pickerHoldWord->pct.SetDirect(holdOpacity);
	COLORREF colorPickerHoldGrayColor = MixUiColor(
		GetThemeColor(ThemeColorEnum::TextPrimary),
		GetThemeColor(ThemeColorEnum::Surface), 0.45);
	pickerHoldWord->color.SetDirect(MixUiColor(
		colorPickerHoldGrayColor,
		GetThemeColor(ThemeColorEnum::TextPrimary),
		clamp(static_cast<double>(
			holdTextMix.val), 0.0, 1.0)));
	pickerHoldWord->UpInh(UiInheritClass(
		holdLeft + ColorPickerPaletteInset * pickerScale, holdTop));

	// 顶部色预览是面板固定控件：随面板展开常驻，无独立入退场，不显示文字。
	double previewSlotGap = ColorPickerPaletteInset * pickerScale;
	double previewSlotLeft = pickerToneHit->inhX
		+ pickerToneHit->w.val + previewSlotGap;
	double previewSlotRight = pickerCloseHit->inhX - previewSlotGap;
	double previewSlotTop = pickerToneHit->inhY;
	double previewSlotHeight = pickerToneHit->h.val;
	double previewSlotWidth = (std::max)(0.0,
		previewSlotRight - previewSlotLeft);
SetAbsoluteHit(pickerPreview, previewSlotLeft, previewSlotTop,
			previewSlotWidth, previewSlotHeight);
		pickerPreview->pct.SetDirect(pickerOpacity);
		pickerPreview->rw->SetDirect(pickerControlRadius);
		pickerPreview->rh->SetDirect(pickerControlRadius);
		// 顶部预览色块只反映 RGB；透明度由底部读数单独展示。
		pickerPreview->fill->SetDirect(RGB(displayR, displayG, displayB));
}

void ColorPicker::Draw(UIRendering& renderer, ID2D1DeviceContext* dc, RECT* bounds)
{
 const double frameZoom = renderer.GetFrameZoom();
	auto pickerPanel = panel_;
	double pickerOpacity = clamp(
		static_cast<double>(pickerPanel->pct.val)
			/ ColorPickerSurfaceOpacity, 0.0, 1.0);
	double pickerGeometryScale =
		pickerPanel->w.val / ColorPickerPanelWidth;
	if (pickerOpacity > 0.000001)
	{
		// 点光 diffuse mask 以完整面板几何归一，避免回弹缩放时边缘光变弱。
		double pickerMaskScale = (std::max)(0.01,
			pickerGeometryScale);
		if (!placement_.hostOpacity) renderer.SetFrameDiffuseMaskGeometryScale(1.0 / pickerMaskScale);
		renderer.Shape(dc, *pickerPanel,
			UiInheritClass(pickerPanel->inhX, pickerPanel->inhY),
			bounds, false);

 DrawButtonVisual(renderer, dc, tone_, {tone_.button.inhX, tone_.button.inhY});

		auto palette = palette_;
		FLOAT uiZoom = static_cast<FLOAT>(frameZoom);
		D2D1_RECT_F paletteRect = D2D1::RectF(
			static_cast<FLOAT>(palette->inhX * uiZoom),
			static_cast<FLOAT>(palette->inhY * uiZoom),
			static_cast<FLOAT>((palette->inhX + palette->w.val) * uiZoom),
			static_cast<FLOAT>((palette->inhY + palette->h.val) * uiZoom));
		D2D1_ROUNDED_RECT roundedPalette{
			paletteRect,
			static_cast<FLOAT>(palette->rw->val * uiZoom),
			static_cast<FLOAT>(palette->rh->val * uiZoom) };
		auto hueBrush = renderer.GetColorPickerHueGradientBrush(
			dc,
			D2D1::Point2F(paletteRect.left, paletteRect.top),
			D2D1::Point2F(paletteRect.right, paletteRect.top));
		if (hueBrush)
		{
			hueBrush->SetOpacity(static_cast<FLOAT>(pickerOpacity));
			(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc), dc)->FillRoundedRectangle(
				&roundedPalette, hueBrush);
			double darkMix = clamp(static_cast<double>(
				toneMix.val), 0.0, 1.0);
			bool openBelowSwatch =
				placement_.below;
			D2D1_POINT_2F nearPoint = D2D1::Point2F(
				paletteRect.left,
				openBelowSwatch ? paletteRect.top : paletteRect.bottom);
			D2D1_POINT_2F farPoint = D2D1::Point2F(
				paletteRect.left,
				openBelowSwatch ? paletteRect.bottom : paletteRect.top);
			auto lightBrush = renderer.GetColorPickerToneGradientBrush(
				dc, false,
				farPoint, nearPoint,
				static_cast<FLOAT>(pickerOpacity * (1.0 - darkMix)));
			if (lightBrush) (Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc), dc)->FillRoundedRectangle(
				&roundedPalette, lightBrush);
			auto darkBrush = renderer.GetColorPickerToneGradientBrush(
				dc, true,
				nearPoint, farPoint,
				static_cast<FLOAT>(pickerOpacity * darkMix));
			if (darkBrush) (Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc), dc)->FillRoundedRectangle(
				&roundedPalette, darkBrush);
		}

		if (placement_.hostOpacity ? renderedFeedback_.markerVisible : static_cast<bool>(colorPickerMarkerVisible))
		{
			double markerX = clamp(static_cast<double>(
				placement_.hostOpacity ? renderedFeedback_.x : static_cast<double>(colorPickerMarkerX)), 0.0, 1.0);
			double markerY = clamp(static_cast<double>(
				placement_.hostOpacity ? renderedFeedback_.y : static_cast<double>(colorPickerMarkerY)), 0.0, 1.0);
			D2D1_POINT_2F markerCenter = D2D1::Point2F(
				static_cast<FLOAT>(paletteRect.left
					+ markerX * (paletteRect.right - paletteRect.left)),
				static_cast<FLOAT>(paletteRect.top
					+ markerY * (paletteRect.bottom - paletteRect.top)));
			D2D1_ELLIPSE outer = D2D1::Ellipse(markerCenter,
				static_cast<FLOAT>(5.5 * pickerGeometryScale) * uiZoom,
				static_cast<FLOAT>(5.5 * pickerGeometryScale) * uiZoom);
			if (auto markerShadow = renderer.GetFrameSolidColorBrush(
				dc, RGB(0, 0, 0),
				pickerOpacity * 0.72))
				(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc), dc)->DrawEllipse(&outer, markerShadow,
					static_cast<FLOAT>(3.0 * pickerGeometryScale) * uiZoom);
			if (auto markerBrush = renderer.GetFrameSolidColorBrush(
				dc, RGB(255, 255, 255), pickerOpacity))
				(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc), dc)->DrawEllipse(&outer, markerBrush,
					static_cast<FLOAT>(1.5 * pickerGeometryScale) * uiZoom);
		}

		auto rgbWord = &words_[0];
		renderer.Word(dc, *rgbWord,
			UiInheritClass(rgbWord->inhX, rgbWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_LEADING);
		auto gWord = &words_[1];
		renderer.Word(dc, *gWord,
			UiInheritClass(gWord->inhX, gWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_LEADING);
		auto bWord = &words_[2];
		renderer.Word(dc, *bWord,
			UiInheritClass(bWord->inhX, bWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_LEADING);
		auto opacityWord = &words_[3];
		renderer.Word(dc, *opacityWord,
			UiInheritClass(opacityWord->inhX, opacityWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_LEADING);
		auto rgbValueWord = &words_[4];
		renderer.Word(dc, *rgbValueWord,
			UiInheritClass(rgbValueWord->inhX, rgbValueWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_TRAILING);
		auto gValueWord = &words_[5];
		renderer.Word(dc, *gValueWord,
			UiInheritClass(gValueWord->inhX, gValueWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_TRAILING);
		auto bValueWord = &words_[6];
		renderer.Word(dc, *bValueWord,
			UiInheritClass(bValueWord->inhX, bValueWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_TRAILING);
		auto opacityValueWord = &words_[7];
		renderer.Word(dc, *opacityValueWord,
			UiInheritClass(
				opacityValueWord->inhX, opacityValueWord->inhY),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_TEXT_ALIGNMENT_TRAILING);

 ButtonDrawOptions closeOptions; closeOptions.drawIcon = false;
 closeOptions.content = [&](UIRendering& r, ID2D1DeviceContext* context, ButtonClass& button, const ButtonVisualInheritance& inh) {
  const FLOAT z = static_cast<FLOAT>(renderer.GetFrameZoom());
  const FLOAT cx = static_cast<FLOAT>((inh.button.x+button.button.w.val/2)*z), cy = static_cast<FLOAT>((inh.button.y+button.button.h.val/2)*z);
  const FLOAT half = (std::max)(1.f*z, static_cast<FLOAT>(button.button.w.val*z*(1.f/3.f)*.5f));
  if (auto* brush = r.GetFrameSolidColorBrush(context, GetThemeColor(ThemeColorEnum::IconPrimary), pickerOpacity)) {
   ObserveUi3SvgUnknownWrite(context); context->DrawLine({cx-half,cy-half}, {cx+half,cy+half}, brush, (std::max)(1.f,1.6f*z));
   ObserveUi3SvgUnknownWrite(context); context->DrawLine({cx+half,cy-half}, {cx-half,cy+half}, brush, (std::max)(1.f,1.6f*z));
  } return true;
 };
 DrawButtonVisual(renderer, dc, close_, {close_.button.inhX, close_.button.inhY}, closeOptions);

		auto holdHint = hold_;
		auto holdWord = &holdWord_;
		double holdRingOpacity = clamp(static_cast<double>(
			this->holdRingOpacity.val)
			* pickerOpacity, 0.0, 1.0);
		if (holdHint->pct.val > 0.000001
			|| holdWord->pct.val > 0.000001
			|| holdRingOpacity > 0.000001)
		{
			if (holdHint->pct.val > 0.000001)
				renderer.Shape(dc, *holdHint,
					UiInheritClass(holdHint->inhX, holdHint->inhY));
			if (holdWord->pct.val > 0.000001)
				renderer.Word(dc, *holdWord,
					UiInheritClass(holdWord->inhX, holdWord->inhY),
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_TEXT_ALIGNMENT_LEADING);
				if (holdRingOpacity > 0.000001 && !ringFailed_)
				{
					FLOAT ringRadius = static_cast<FLOAT>(
						7.0 * pickerGeometryScale) * uiZoom;
					D2D1_POINT_2F ringCenter = D2D1::Point2F(
						static_cast<FLOAT>((holdHint->inhX
							+ holdHint->w.val
							- 12.0 * pickerGeometryScale) * uiZoom),
						static_cast<FLOAT>((holdHint->inhY
							+ holdHint->h.val / 2.0) * uiZoom));
					float progress = clamp(static_cast<float>(
						colorPickerHoldProgress),
						0.0F, 1.0F);
				COLORREF holdRingTrackColor = MixUiColor(
					GetThemeColor(ThemeColorEnum::TextPrimary),
					GetThemeColor(ThemeColorEnum::Surface), 0.70);
				COLORREF holdRingFillColor = MixUiColor(
					GetThemeColor(ThemeColorEnum::TextPrimary),
					GetThemeColor(ThemeColorEnum::Surface), 0.35);
				if (!renderer.DrawProgressRing(
					dc, ringCenter, ringRadius,
					static_cast<FLOAT>(2.0 * pickerGeometryScale) * uiZoom,
					progress,
					holdRingTrackColor, holdRingFillColor,
					static_cast<FLOAT>(holdRingOpacity * 0.45),
					static_cast<FLOAT>(holdRingOpacity))) {
         ringFailed_ = true;
         Inkeys::Log::TryFormat(Inkeys::Log::Level::Warn, Inkeys::Log::Module::UI3, "颜色选择器长按进度资源失败，本次手势停止重试");
        }
			}
		}
	}

	auto preview = preview_;
	if (preview->pct.val > 0.000001)
	{
		renderer.Shape(dc, *preview,
			UiInheritClass(preview->inhX, preview->inhY),
			bounds, false);
	}
	if (!placement_.hostOpacity) renderer.SetFrameDiffuseMaskGeometryScale(1.0);
}
}
