module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1_1.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include "../../../IdtAtomic.h"

export module Inkeys.UI.UI3:ColorPicker;
import :Button;
import :Primitive;
import :Renderer;
import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Progress;
import Inkeys.UI.UI3.Interaction;
import Inkeys.UI.UI3.VisualDirty;
import :VisualDirtySampler;

export namespace Inkeys::UI::UI3
{
 struct ColorPickerHsv { double hue = 0, saturation = 0, value = 0; };
 ColorPickerHsv ConvertColorPickerRgbToHsv(COLORREF color);
 COLORREF ConvertColorPickerHsvToRgb(double hue, double saturation, double value);
 COLORREF GetColorPickerColor(double x, double y, bool darkTone, bool openBelowSwatch);
 bool ProjectColorPickerColor(COLORREF color, bool darkTone, bool openBelowSwatch, double& x, double& y, bool& exact);

 enum class ColorPickerPart { Outside, Panel, Palette, Tone, Close, Preview, HoldHint };
 // 取色器面板的基准尺寸（DIP），宿主摆放面板时使用同一组值。
 // 高度：5 顶距 + 30 顶栏 + 5 间隔 + 132 色板 + 32 读数（含底部 10px 额外间隙）+ 5 底距 = 209。
 inline constexpr double ColorPickerPanelWidth = 300.0, ColorPickerPanelHeight = 209.0;
 struct ColorPickerPlacement
 {
  double left = 0, top = 0, scale = 1, width = ColorPickerPanelWidth, height = ColorPickerPanelHeight;
  bool below = true;
  // 样式栏保留局部矩阵和整层合成，主栏保留绝对图元与逐项 alpha。
  bool hostOpacity = false;
  std::optional<double> buttonBackgroundDuration;
  bool keepButtonsVisibleDuringClose = false;
  // 面板外框的鼠标边缘光与其他大面板一致，取与主光同强度。
  double cursorLightIntensityScale = 1.0, lightWeight = 0;
 };
 struct ColorPickerSnapshot
 {
  ButtonHitSnapshot panel, palette, tone, close;
  bool darkTone = false, below = true;
 };
 struct ColorPickerFeedback
 {
  COLORREF color = 0; double x = 0, y = 0; bool markerVisible = false;
 };
 struct ColorPickerCallbacks
 {
  std::function<COLORREF(COLORREF)> preview;
  std::function<void(COLORREF)> commit;
  std::function<void()> cancel;
 };
 struct ColorPickerPointer
 {
  ColorPickerSnapshot frame;
  double x = 0, y = 0; // 与命中快照相同坐标系。
  POINT screen{};
  HoldProgressState::Time now{};
  double thresholdScale = 1;
  bool quantizeMarker = false;
  HoldProgressCurve holdCurve = HoldProgressCurve::EaseOutSine;
 };
 class ColorPicker
 {
 public:
  explicit ColorPicker(bool defaultDarkTone = true, UiCurveEnum feedbackCurve = UiCurveEnum::EaseInOutCubic);
  void SetLabels(const std::array<std::wstring, 4>& labels, const std::wstring& holdLabel);
  void Retarget(bool open, COLORREF color, double opacityPercent, double duration = .4, bool localPreview = false);
  ButtonAnimationResult Advance(const ButtonAnimationContext& animation, bool deferButtons = false);
  void Layout(UIRendering& renderer, const ColorPickerPlacement& placement);
  void Draw(UIRendering& renderer, ID2D1DeviceContext* dc, RECT* bounds = nullptr);
  void ResetDeviceCaches();
  void ProjectColor(COLORREF color, bool preset, bool below, bool quantize = false);
  void PointerDown(const ColorPickerPointer& input, ColorPickerCallbacks callbacks);
  void PointerMove(const ColorPickerPointer& input);
  void PointerUp(bool captureOwned);
  void Cancel();
  void Reset();
  void ResetHoldIfMoved(POINT screen, HoldProgressState::Time now, double thresholdScale);
  [[nodiscard]] ButtonClass& Button(ColorPickerPart part) noexcept { return part == ColorPickerPart::Tone ? tone_ : close_; }
  bool TickHold(POINT screen, HoldProgressState::Time now, double thresholdScale);
  void SampleHold(HoldProgressState::Time now);
  void Activate(ColorPickerPart part);
  void SetHover(ColorPickerPart part);
  void SetCloseRequest(std::function<void()> close) { closeRequest_ = std::move(close); }
  void CommitPresented(const ColorPickerSnapshot& frame);
  [[nodiscard]] ColorPickerSnapshot Presented() const;
  ButtonPointerResult ButtonInput(ButtonPointerEvent event, ColorPickerPart part);
  [[nodiscard]] std::wstring HoldLabel() const { return holdWord_.content.GetTar(); }
  [[nodiscard]] double Geometry() const noexcept { return progress.val; }
  [[nodiscard]] double TargetGeometry() const noexcept { return progress.tar; }
  [[nodiscard]] double Opacity() const noexcept;
  [[nodiscard]] bool Active() const noexcept { return progress.val != progress.tar; }
  [[nodiscard]] bool Visible() const noexcept { return open_ || Active() || Opacity() > 0; }
  [[nodiscard]] double PanelVisibility() const noexcept { return panel_->pct.val; }
  [[nodiscard]] double HintVisibility() const noexcept { return hold_->pct.val; }
  [[nodiscard]] COLORREF PreviewColor() const noexcept { return previewColor_; }
  [[nodiscard]] ColorPickerSnapshot Snapshot() const noexcept;
  [[nodiscard]] ColorPickerFeedback RenderedFeedback() const noexcept { return renderedFeedback_; }
  [[nodiscard]] static ColorPickerPart Hit(const ColorPickerSnapshot& frame, double x, double y) noexcept;
  [[nodiscard]] std::shared_ptr<UiShapeClass> Shape(ColorPickerPart part) const;
  [[nodiscard]] const std::array<std::shared_ptr<UiShapeClass>, 6>& Shapes() const noexcept { return shapes_; }
  // UI3 自动脏区：整块取色器作为一个组合对象采样（面板、色盘渐变与标记、按钮、读数、预览气泡、保持提示）。
  [[nodiscard]] VisualSampler MakeVisualSampler(VisualSamplerContext context) const;
  // 原来的跨线程反馈迁至组件，宿主仅保留产品开关和原生捕获凭据。
  IdtAtomic<bool> colorPickerDarkTone = true, colorPickerTonePress = false, colorPickerClosePress = false;
  IdtAtomic<bool> colorPickerMarkerVisible = false, colorPickerPointerPressed = false;
  IdtAtomic<bool> colorPickerHoldHintActive = false, colorPickerHoldLocked = false;
  IdtAtomic<double> colorPickerMarkerX = 0, colorPickerMarkerY = 0;
  IdtAtomic<float> colorPickerHoldProgress = 0, colorPickerPointerY = 0;
  IdtAtomic<ButtonHoverStageEnum>& ToneHoverStage() noexcept { return tone_.hoverStage; }
  IdtAtomic<ButtonHoverStageEnum>& CloseHoverStage() noexcept { return close_.hoverStage; }
 private:
  void ApplyPoint(const ColorPickerPointer& input);
  std::shared_ptr<UiShapeClass> panel_, palette_, preview_, hold_;
  ButtonClass tone_, close_;
  std::array<std::shared_ptr<UiShapeClass>, 6> shapes_;
  std::array<UiWordClass, 8> words_;
  UiWordClass holdWord_;
  UiValueClass progress{0}, toneMix{0}, holdOpacity{0}, holdRingOpacity{0}, holdTextMix{0};
  std::array<UiValueClass, 4> display_{UiValueClass{0}, UiValueClass{0}, UiValueClass{0}, UiValueClass{100}};
  bool displayInitialized_ = false, holdWasVisible_ = false, holdOnTop_ = true;
  IdtAtomic<bool> open_ = false, below_ = true;
  ColorPickerPlacement placement_;
  ColorPickerFeedback renderedFeedback_;
  IdtAtomic<COLORREF> previewColor_ = 0, gestureColor_ = 0;
  mutable std::recursive_mutex gestureMutex_;
  HoldProgressState holdProgress_;
  HoldProgressConfig holdConfig_;
  IdtAtomic<bool> ringFailed_ = false;
  bool iconDark_ = false, iconReady_ = false, buttonsDeferred_ = false;
  ButtonAnimationContext buttonAnimation_;
  POINT stablePoint_{};
  ColorPickerCallbacks callbacks_;
  ButtonPointerState buttonPointer_;
  ColorPickerPart hoverPart_ = ColorPickerPart::Outside;
  std::function<void()> closeRequest_;
  ColorPickerSnapshot presented_;
  mutable std::mutex presentedMutex_;
 };
}
