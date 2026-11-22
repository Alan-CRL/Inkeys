module;

#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtI18nKeys.g.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <variant>

module Inkeys.UI.Bar;
import :MainContent;
import :Main;
import Inkeys.UI.UI3;
import Inkeys.UI.Bar.MainLayoutPolicy;
import Inkeys.UI.Bar.A2;

using namespace Inkeys::UI::UI3;
extern const double BarGeometryAttributeDividerCursorLightIntensity;

namespace Inkeys::UI::Bar
{
	MainBarPresentation ResolveMainBarPresentation(BarButtonPresetEnum preset,
		ButtonSizeEnum baseSize, const MainBarPresentationInput& input)
	{
		const bool selection = input.mode == StateModeSelectEnum::IdtSelection;
		const bool compactSelection = !input.whiteboard && selection && !input.hasContent;
		auto size = baseSize;
		bool hide = false;
		WidgetState state = WidgetState::None;
		switch (preset)
		{
		case BarButtonPresetEnum::Select:
			state = selection && !input.canvasDebugVisual ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Draw:
			state = !input.canvasDebugVisual && input.mode == StateModeSelectEnum::IdtPen ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Eraser:
			hide = compactSelection;
			state = !input.canvasDebugVisual && input.mode == StateModeSelectEnum::IdtEraser ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Geometry:
			hide = compactSelection;
			state = !input.canvasDebugVisual && input.mode == StateModeSelectEnum::IdtShape ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Clean:
			state = selection && !input.hasContent ? WidgetState::Disable : WidgetState::None;
			break;
		case BarButtonPresetEnum::Whiteboard:
			size = input.a2.whiteboardTwoTwo ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
			hide = !input.featureEnabled;
			state = input.whiteboard ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Freeze:
			size = input.a2.freezeTwoTwo ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
			hide = !input.a2.freezeVisible;
			state = !input.freezeAvailable ? WidgetState::Disable : (input.freezeActive ? WidgetState::Selected : WidgetState::None);
			break;
		case BarButtonPresetEnum::Pierce:
			size = input.a2.pierceTwoTwo ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
			hide = !input.a2.pierceVisible;
			state = !input.canvasDebugVisual && input.mode == StateModeSelectEnum::IdtClickThrough ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::EndShow:
			hide = !input.a2.endShowVisible;
			break;
		case BarButtonPresetEnum::CanvasDebug:
			hide = !input.canvasDebugEnabled;
			state = input.canvasDebugVisual ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Recall:
			size = input.recall.undoExpanded ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
			hide = !input.recall.mainUndoVisible;
			// 灰色历史按钮仍可点击延期，由命令可用性阻止发布，不能设成Disable。
			break;
		case BarButtonPresetEnum::Redo:
			size = input.recall.redoExpanded ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
			hide = !input.recall.mainRedoVisible;
			break;
		case BarButtonPresetEnum::More:
			state = input.moreOpen ? WidgetState::Selected : WidgetState::None;
			break;
		case BarButtonPresetEnum::Setting:
			state = input.settingVisible ? WidgetState::Selected : WidgetState::None;
			break;
		default: break;
		}
		// 小尺寸只改运行时有效尺寸：基准/配置/注册尺寸保持原值，切回大尺寸即恢复。
		if (input.compactSize && size != ButtonSizeEnum::oneTwo) size = ButtonSizeEnum::oneOne;
		return { size, hide, state };
	}

	std::wstring MainBarContentStyle::LocalizedLabel(std::wstring (*lookup)(std::string),
			std::wstring (*shapeLabel)(draw3::shapes::PresetKind)) const
	{
		if (const auto* key = std::get_if<const char*>(&label)) return lookup(*key);
		return shapeLabel(std::get<draw3::shapes::PresetKind>(label));
	}

	MainBarContentStyle ResolveSelectStyle(bool whiteboard, bool separate, bool selection)
	{
		const auto& text = I18nKey.UI.Bar.MainButtons;
		return { whiteboard ? L"barMove" : L"barSelect", whiteboard ? text.MoveLabel
			: separate && !selection ? text.SelectClearLabel : text.SelectLabel };
	}

	MainBarContentStyle ResolveDrawStyle(bool selected, bool laser, bool highlighter, bool hardPen)
	{
		const auto& text = I18nKey.UI.Bar.MainButtons;
		return { laser ? L"barLaser" : highlighter ? L"barHighlighter1" : hardPen ? L"barBrush1" : L"barBrush2",
			!selected ? text.DrawLabel : laser ? text.LaserLabel : highlighter ? text.HighlighterLabel
			: hardPen ? text.HardPenLabel : text.SoftPenLabel };
	}

	MainBarContentStyle ResolveEraserStyle(bool selected)
	{
		const auto& text = I18nKey.UI.Bar.MainButtons;
		return { L"barEraser", selected ? text.AreaEraserLabel : text.EraserLabel };
	}

	MainBarContentStyle ResolveGeometryStyle(bool selected, bool pinned, const draw3::shapes::ToolSpec& tool)
	{
		if (selected) return { ShapeToolIconResource(tool), tool.preset, pinned };
		return { L"barGeometry", I18nKey.UI.Bar.MainButtons.GeometryLabel };
	}

	MainBarContentStyle ResolveWhiteboardStyle(bool active, bool resume, bool returnToShow)
	{
		const auto& text = I18nKey.UI.Bar.MainButtons;
		return { resume ? L"barResumeWhiteboard" : returnToShow ? L"barReturnShow"
			: active ? L"barCloseWhiteboard" : L"barWhiteboard",
			resume ? text.ResumeWhiteboardLabel : returnToShow ? text.ReturnToShowLabel
			: active ? text.CloseWhiteboardLabel : text.WhiteboardLabel };
	}

	MainBarContentStyle ResolvePierceStyle()
	{
		return { L"barPierce", I18nKey.UI.Bar.MainButtons.ClickThroughLabel };
	}

	MainBarBatch ResolveMainBarBatch(MainBarBatchState& state, const MainBarBatchInput& input)
	{
		const bool iconOrderChanged = state.iconOrder.has_value()
			&& state.iconOrder.value() != input.iconOrder;
		const bool buttonLayoutChanged = state.layout.has_value()
			&& input.layout != state.layout.value();
		bool mainBarLayoutChange = buttonLayoutChanged || iconOrderChanged || (state.width.has_value()
			&& abs(input.width - state.width.value()) > 0.000001);
		bool mainBarLayoutExpands = mainBarLayoutChange
			&& state.width.has_value()
			&& input.width > state.width.value();
		// 布局变化会取代仍在运行的换边关键帧；即使某个控件目标没变，也必须从当前值重建。
		bool interruptingMainBarSideSwitch = mainBarLayoutChange
			&& state.timeline.IsActive()
			&& input.hasSideKeyframe;
		// 新操作创建完整批次；批次进入后半程后，新布局不再压缩到旧截止时间。
		bool lateMainBarLayoutChange = !input.fold && state.timeline.IsActive()
			&& mainBarLayoutChange && !state.timeline.CanJoin();
		// 后半程布局变化会重开完整批次；目标未变的在途布局值也要从当前值同步重启。
		bool forceRestartMainBarLayout = input.foldChanged
			|| lateMainBarLayoutChange;
		// 超过加入阈值后会创建新批次，此时旧换边中点已经失效，不能在新批次中再次收窄。
		bool continueMainBarSideSwitchKeyframe = interruptingMainBarSideSwitch
			&& !lateMainBarLayoutChange;
		bool restartMainBarTimeline = input.foldChanged || input.sideSwitch
			|| lateMainBarLayoutChange
			|| (!input.fold && !state.timeline.IsActive() && mainBarLayoutChange);
		if (restartMainBarTimeline)
		{
			if (input.sideSwitch) state.curve = UiCurveEnum::EaseInOutCubic;
			else if (input.foldChanged)
				state.curve = input.fold
				? UiCurveEnum::EaseInBack : UiCurveEnum::EaseOutBack;
			else state.curve = mainBarLayoutExpands
				// 展开保留回弹活力；收起立即响应并在末端平稳减速到零。
				? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseOutCubic;
			state.timeline.Restart(input.duration);
		}
		else if (state.timeline.IsActive() && mainBarLayoutChange)
		{
			// 换边中途加入布局时用完整平滑曲线覆盖剩余时间，避免压缩重播 Back 造成突发加速。
			state.curve = continueMainBarSideSwitchKeyframe
				? UiCurveEnum::EaseInOutCubic
				: (mainBarLayoutExpands ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseOutCubic);
		}
		state.layout = input.layout;
		state.width = input.width;
		state.iconOrder = input.iconOrder;
		const bool active = state.timeline.IsActive();
		const double duration = active ? state.timeline.GetRemainingDuration() : input.duration;
		const double phase = active ? state.timeline.GetProgress() : 0.0;
		const bool continuePhase = active && phase > 0.0 && !continueMainBarSideSwitchKeyframe;
		const auto valueCurve = active ? state.curve : UiCurveEnum::EaseInOutCubic;
		const auto pctCurve = mainBarLayoutChange
			? (mainBarLayoutExpands ? UiCurveEnum::EaseOutSine : UiCurveEnum::EaseOutCubic)
			: (active && state.curve == UiCurveEnum::EaseInBack ? UiCurveEnum::EaseInSine : UiCurveEnum::EaseOutSine);
		// 新换边优先于延续旧中点：仅后者带入已有相位。
		const bool continued = !input.sideSwitch && continueMainBarSideSwitchKeyframe;
		return { restartMainBarTimeline, mainBarLayoutChange, forceRestartMainBarLayout,
			input.sideSwitch || continueMainBarSideSwitchKeyframe, active, duration,
			{ valueCurve, valueCurve, phase, continuePhase }, { pctCurve, pctCurve, phase, continuePhase },
			{ UiCurveEnum::EaseInCubic, UiCurveEnum::EaseOutBack, continued ? phase : 0.0, continued },
			{ UiCurveEnum::EaseOutSine, UiCurveEnum::EaseOutSine, continued ? phase : 0.0, continued } };
	}

	void SyncValueDuration(UiValueClass& value, double duration, bool syncValues, const UiCurveSpecClass& curve)
	{
		// 只在新目标刚建立时提交批次剩余时长，不能每帧改写正在推进的动画段。
		if (!value.IsSame() && value.progress == 0.0)
		{
			value.dur = duration;
			// 关键帧已经携带前后两段曲线；普通值仅在批次中覆盖自身默认曲线。
			if (!value.hasMiddleV)
			{
				UiCurveSpecClass curveSpec = syncValues
					? curve
					: UiCurveSpecClass{ value.curve, value.curve, 0.0, false };
				value.activeCurve = curveSpec.first;
				value.activeMiddleCurve = curveSpec.second;
				value.timelineStartProgress = curveSpec.timelineStartProgress;
				value.continueTimelinePhase = curveSpec.continueTimelinePhase;
			}
		}
	}

	void SyncPctDuration(UiPctClass& pct, double duration, const UiCurveSpecClass& curve)
	{
		// 独立悬停已经提交自己的显现/淡出时长，不能再被属性栏批次时长覆盖。
		if (!pct.animateWhenDisabled && !pct.IsSame() && pct.progress == 0.0)
		{
			pct.dur = duration;
			pct.activeCurve = curve.first;
			pct.activeMiddleCurve = curve.second;
			pct.timelineStartProgress = curve.timelineStartProgress;
			pct.continueTimelinePhase = curve.continueTimelinePhase;
		}
	}

	void AnchorHiddenBarButton(const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		BarButtonClass* hidden, const BarButtonClass* preferredPreset,
		const std::function<void(UiValueClass&, double)>& setPositionTarget)
	{
		if (!hidden || hidden->IsVisible()) return;
		const auto buttonCount = static_cast<int>(buttons.size());
		int hiddenIndex = -1;
		BarButtonClass* anchor = nullptr;
		for (int index = 0; index < buttonCount; ++index)
		{
			auto* candidate = buttons[index].get();
			if (candidate == hidden) hiddenIndex = index;
			if (candidate && candidate == preferredPreset && candidate->IsVisible()) anchor = candidate;
		}
		if (!anchor && hiddenIndex >= 0)
		{
			// 首选不可见时收拢到前一有效成员；前区全空才使用后继。
			for (int index = hiddenIndex - 1; index >= 0 && !anchor; --index)
			{
				auto* previous = buttons[index].get();
				if (previous && previous->IsVisible()) anchor = previous;
			}
			for (int index = hiddenIndex + 1; index < buttonCount && !anchor; ++index)
			{
				auto* next = buttons[index].get();
				if (next && next->IsVisible()) anchor = next;
			}
		}
		if (!anchor) return;
		// 保留原布局批次的回调曲线，显示时从来源位置展开。
		setPositionTarget(hidden->button.x, anchor->button.x.tar);
		setPositionTarget(hidden->button.y, anchor->button.y.tar);
		hidden->lastDrawX = anchor->button.x.tar;
		hidden->lastDrawY = anchor->button.y.tar;
	}

	void ApplyMainBarButtonTargets(const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		const std::function<BarButtonClass*(BarButtonPresetEnum)>& preset, const BarButtonListLayout& layout,
		const MainBarBatch& batch, const MainBarButtonTargetInput& input, UIRendering& renderer)
	{
		const double operationDur = batch.duration;
		const auto SetButtonPositionTar = [&](UiValueClass& value, double target, double middle, bool mirrorX = false)
		{
			if (mirrorX) target = ResolveBarMainIconX(target, layout.width, input.iconOrder, input.opensRight);
			if (batch.sideKeyframe)
				value.SetTar(target, operationDur, middle, true, batch.keyframeValueCurve);
			else UiSetLayoutPositionTarget(value, target, operationDur,
				batch.forceRestart || batch.layoutChanged, batch.valueCurve);
		};
		for (const auto& placement : layout.placements)
		{
			auto* temp = placement.button.get();
			if (!temp) continue;
			const bool visible = !input.fold && temp->IsVisible();
			const bool divider = temp->preset == BarButtonPresetEnum::Divider;
			const bool recallCommand = temp->preset == BarButtonPresetEnum::Recall
				|| temp->preset == BarButtonPresetEnum::Redo;
			const bool commandEnabled = temp->preset == BarButtonPresetEnum::Recall
				? input.recall.undoEnabled : input.recall.redoEnabled;
			const bool disabled = temp->state->state == WidgetState::Disable
				|| (recallCommand && !commandEnabled);
			const bool selected = temp->state->state == WidgetState::Selected;
			const COLORREF contentColor = input.themeColor(selected ? ThemeColorEnum::Accent : ThemeColorEnum::TextPrimary);
			const COLORREF iconColor = input.themeColor(selected ? ThemeColorEnum::Accent : ThemeColorEnum::IconPrimary);
			// 折叠锚点 = 主按钮中心在主栏局部坐标中的位置（主栏折叠后与主按钮等宽等高）。
			const double foldAnchor = input.foldAnchor;
			if (input.fold)
			{
				SetButtonPositionTar(temp->button.x, foldAnchor, foldAnchor);
				SetButtonPositionTar(temp->button.y, foldAnchor, foldAnchor);
			}
			else if (temp->IsVisible())
			{
				SetButtonPositionTar(temp->button.x, placement.x, foldAnchor, true);
				SetButtonPositionTar(temp->button.y, placement.y, foldAnchor);
			}
			if (divider)
			{
				temp->button.w.SetTar(placement.width, operationDur);
				temp->button.h.SetTar(placement.height, operationDur);
				if (temp->button.rw) temp->button.rw->SetTar(0.5, operationDur);
				if (temp->button.rh) temp->button.rh->SetTar(0.5, operationDur);
				if (temp->button.ft) temp->button.ft->SetTar(1.0, operationDur);
				if (temp->button.framePct) temp->button.framePct->SetTar(0.0, operationDur);
				if (temp->button.frameLightPct) temp->button.frameLightPct->SetTar(visible ? 1.0 : 0.0, operationDur);
				temp->button.pct.SetTar(visible ? 0.30 : 0.0, operationDur);
				const COLORREF color = input.themeColor(ThemeColorEnum::Divider);
				if (temp->button.fill) temp->button.fill->SetTar(color);
				if (temp->button.frame) temp->button.frame->SetTar(color);
				temp->button.frameRendering = UiFrameRenderingEnum::PointLight;
				temp->button.framePrimaryLightEnabled = false;
				temp->button.frameCursorLightIntensityScale = BarGeometryAttributeDividerCursorLightIntensity;
				temp->icon.pct.SetDirect(0.0); temp->name.pct.SetDirect(0.0);
			}
			else
			{
				const auto kind = temp->size == ButtonSizeEnum::oneOne
					? ButtonVisualLayoutKind::StandardOneOne
					: temp->size == ButtonSizeEnum::twoOne
						? ButtonVisualLayoutKind::StandardTwoOne : ButtonVisualLayoutKind::StandardTwoTwo;
				const bool largeIcon = temp->size == ButtonSizeEnum::twoTwo && temp->icon.enable.tar
					&& (temp->preset == BarButtonPresetEnum::More
						|| (temp->preset == BarButtonPresetEnum::Geometry && input.shapeSelected));
				ConfigureButtonVisualTargets(renderer, *temp, kind, operationDur, 1.0, false,
					largeIcon ? std::optional<double>{34.0} : std::nullopt, std::nullopt);
				const bool colorSelector = temp->size == ButtonSizeEnum::oneOne
					&& temp->name.enable.tar && temp->name.content.GetTar().starts_with(L"__color");
				if (!colorSelector)
					UI3::RetargetButtonInteractionVisual(input.dark, *temp, visible,
						temp->button.enable.tar && !disabled, selected, operationDur);
				else
				{
					temp->button.pct.SetTar(visible ? 1.0 : 0.0, operationDur);
					temp->icon.SetWH(std::nullopt, 10.0);
					temp->icon.pct.SetTar(visible ? 1.0 : 0.0, operationDur);
				}
				if (temp->size == ButtonSizeEnum::oneOne) temp->name.pct.SetTar(0.0, operationDur);

				if (input.firstFrame || input.fold || !temp->IsVisible())
				{
					if (temp->icon.color1) temp->icon.color1->SetDirect(iconColor);
					if (temp->icon.color2) temp->icon.color2->SetDirect(iconColor);
					if (temp->button.frame) temp->button.frame->SetDirect(contentColor);
				}
			}
			temp->lastDrawX = temp->button.x.tar;
			temp->lastDrawY = temp->button.y.tar;
			// 按压倍率独立于布局批次，松手或拖出时从当前值回弹到标准大小。
			if (temp->preset == BarButtonPresetEnum::Divider)
			{
				// Divider 不参与按钮状态机，但保留独立 frameLightPct 的第三光。
				temp->hoverStage = ButtonHoverStageEnum::None;
				temp->state->emph = WidgetEmphasize::None;
				temp->pressScale.SetDirect(1.0);
				temp->button.pct.animateWhenDisabled = false;
				if (temp->button.fill.has_value())
					temp->button.fill.value().animateWhenDisabled = false;
			}
			else if (temp->state->emph == WidgetEmphasize::Pressed)
				temp->pressScale.SetTar(ButtonPressScale, input.pressDuration,
					nullopt, false, input.pressCurve);
			else temp->pressScale.SetTar(1.0, input.pressDuration,
				nullopt, false, input.releaseCurve);

			// 尺寸枚举只负责选择布局，按钮及其内容统一在同一过程时间内到达新布局。
			SyncValueDuration(temp->button.x, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->button.y, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->button.w, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->button.h, operationDur, batch.syncValues, batch.valueCurve);
			SyncPctDuration(temp->button.pct, operationDur, batch.pctCurve);
			SyncValueDuration(temp->icon.x, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->icon.y, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->icon.w, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->icon.h, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->icon.angle, operationDur, batch.syncValues, batch.valueCurve);
			SyncPctDuration(temp->icon.pct, operationDur, batch.pctCurve);
			SyncValueDuration(temp->name.x, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->name.y, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->name.w, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->name.h, operationDur, batch.syncValues, batch.valueCurve);
			SyncValueDuration(temp->name.size, operationDur, batch.syncValues, batch.valueCurve);
			SyncPctDuration(temp->name.pct, operationDur, batch.pctCurve);

			if (batch.sideKeyframe)
			{
				// 换边中点将整个按钮组合隐藏，再从主按钮下方展开到新位置。
				const auto& pctCurve = batch.keyframePctCurve;
				temp->button.pct.SetTar(temp->button.pct.tar, operationDur, 0.0, true, pctCurve);
				if (temp->button.frameLightPct.has_value())
					temp->button.frameLightPct.value().SetTar(
						temp->button.frameLightPct.value().tar, operationDur, 0.0, true, pctCurve);
				temp->icon.pct.SetTar(temp->icon.pct.tar, operationDur, 0.0, true, pctCurve);
				temp->name.pct.SetTar(temp->name.pct.tar, operationDur, 0.0, true, pctCurve);
			}
		}

		if (input.fold) return;
		const auto AnchorHiddenButton = [&](BarButtonPresetEnum hidden, BarButtonPresetEnum preferred)
		{
			AnchorHiddenBarButton(buttons, preset(hidden), preset(preferred), [&](UiValueClass& value, double target)
			{ SetButtonPositionTar(value, target, 40.0); });
		};
		AnchorHiddenButton(BarButtonPresetEnum::Eraser, BarButtonPresetEnum::Draw);
		AnchorHiddenButton(BarButtonPresetEnum::Geometry, BarButtonPresetEnum::Draw);
		// 对方还可见时停在对方上；一起隐藏时锚到绘制，避免落到右侧按钮。
		{
			auto* redoButton = preset(BarButtonPresetEnum::Redo);
			auto* recallButton = preset(BarButtonPresetEnum::Recall);
			AnchorHiddenButton(BarButtonPresetEnum::Recall,
				redoButton && redoButton->IsVisible()
					? BarButtonPresetEnum::Redo
					: BarButtonPresetEnum::Draw);
			AnchorHiddenButton(BarButtonPresetEnum::Redo,
				recallButton && recallButton->IsVisible()
					? BarButtonPresetEnum::Recall
					: BarButtonPresetEnum::Draw);
		}
		// 定格保留列表身份，复用前驱锚点与既有布局批次自动收拢。
		AnchorHiddenButton(BarButtonPresetEnum::Freeze, BarButtonPresetEnum::Freeze);
		// 穿透隐藏时也跟随定格列，不能保留收窄前主栏右侧的旧坐标。
		AnchorHiddenButton(BarButtonPresetEnum::Pierce, BarButtonPresetEnum::Freeze);
		// 画布调试按钮隐藏时收拢到前一可见成员，显示时从该处展开。
		AnchorHiddenButton(BarButtonPresetEnum::CanvasDebug, BarButtonPresetEnum::CanvasDebug);
		AnchorHiddenButton(BarButtonPresetEnum::EndShow, BarButtonPresetEnum::Freeze);
	}

	void ApplyMainBarBodyTargets(UiShapeClass& body, const MainBarBatch& batch,
		const MainBarBodyTargetInput& input)
	{
		const double operationDur = batch.duration;
		// 高度与圆角只随界面尺寸变化，与主体高度同时长、同默认曲线，不接入布局批次回弹曲线，
		// 避免吸底时主栏高度回弹越过 dock 线。
		if (input.animateSize)
		{
			body.h.SetTar(input.metrics.barHeight, input.sizeDuration);
			if (body.rw) body.rw->SetTar(input.metrics.barCornerRadius, input.sizeDuration);
			if (body.rh) body.rh->SetTar(input.metrics.barCornerRadius, input.sizeDuration);
		}
		else
		{
			body.h.SetDirect(input.metrics.barHeight);
			if (body.rw) body.rw->SetDirect(input.metrics.barCornerRadius);
			if (body.rh) body.rh->SetDirect(input.metrics.barCornerRadius);
		}
		if (input.fold)
		{
			body.x.SetTar(0.0, operationDur, nullopt, batch.forceRestart, batch.valueCurve);
			body.w.SetTar(input.foldedWidth, operationDur, nullopt, batch.forceRestart, batch.valueCurve);

			body.pct.SetTar(0.0, operationDur, nullopt, false, batch.pctCurve);
			body.framePct.value().SetTar(0.0, operationDur, nullopt, false, batch.pctCurve);
		}
		else
		{
			if (batch.sideKeyframe)
				body.w.SetTar(input.width, operationDur, input.foldedWidth, true, batch.keyframeValueCurve);
			else body.w.SetTar(input.width, operationDur, nullopt,
				batch.forceRestart, batch.valueCurve);

			double targetX = 0.0;
			if (input.mainButtonWidth)
			{
				targetX = *input.mainButtonWidth / 2.0 + body.w.tar / 2.0 + input.metrics.mainButtonGap;
				if (!input.opensRight) targetX = -targetX;
			}

			if (batch.sideKeyframe)
				body.x.SetTar(targetX, operationDur, 0.0, true, batch.keyframeValueCurve);
			else body.x.SetTar(targetX, operationDur, nullopt,
				batch.forceRestart, batch.valueCurve);

			body.pct.SetTar(
				0.8, operationDur, nullopt, false, batch.pctCurve);
			body.framePct.value().SetTar(
				0.18, operationDur, nullopt, false, batch.pctCurve);
		}
		if (batch.sideKeyframe)
		{
			// 主栏填充和边框在换边关键帧同步变为全透明。
			const auto& pctCurve = batch.keyframePctCurve;
			body.pct.SetTar(body.pct.tar, operationDur, 0.0, true, pctCurve);
			body.framePct.value().SetTar(
				body.framePct.value().tar, operationDur, 0.0, true, pctCurve);
		}
		body.fill.value().SetTar(input.themeColor(ThemeColorEnum::Surface));
		body.frame.value().SetTar(input.themeColor(ThemeColorEnum::SurfaceFrame));
	}

	void ApplyMainBarMoreTargets(UiValueClass& progress, UiValueClass& opacity, bool open, double duration)
	{
		progress.SetTar(open ? 1.0 : 0.0,
			duration, nullopt, false,
			{ open ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseInBack,
				open ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseInBack,
				0.0, false });
		opacity.SetTar(open ? 1.0 : 0.0,
			duration, nullopt, false,
			{ open ? UiCurveEnum::EaseOutSine : UiCurveEnum::EaseInSine,
				open ? UiCurveEnum::EaseOutSine : UiCurveEnum::EaseInSine,
				0.0, false });
	}

	MainBarHoverUpdate UpdateMainBarHoverHint(MainBarHoverState& state,
		MainBarHoverSource& source, const MainBarHoverInput& input)
	{
		std::wstring text;
		// 折叠、拖动、按钮布局批次动画与面板开合动画期间不提示。
		// 主栏隐藏（呈现透明度为 0）时同样不提示；More 来源要求 More 仍展开。
		if (input.sizeMode == BarSizeMode::Compact && source.mouse && !input.fold
			&& !input.dragging && !input.layoutActive && !input.geometryActive && !input.moreMoving
			&& input.panelsAndPresentationReady())
		{
			text = input.panelText();
			if (text.empty() && source.main && source.main->IsVisible() && (!source.more || input.moreExpanded()))
				text = source.main->name.content.GetTar();
		}
		else if (source.mouse)
		{
			// 门禁期间作废旧来源：解除后须等新的鼠标移动重新命中，不能用旧按钮重新计时。
			source.mouse = false;
			source.main.reset();
		}
		std::replace(text.begin(), text.end(), L'\n', L' ');
		MainBarHoverUpdate update;
		if (text != state.text)
		{
			// 已显示时在按钮间切换直接换字；未显示或已离开则重新计时。
			if (text.empty() || !state.shown)
			{
				state.shown = false;
				state.enteredAt = input.now;
			}
			state.text = std::move(text);
			update.changed = true;
		}
		if (!state.text.empty() && !state.shown)
		{
			const auto due = state.enteredAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(BarHoverHintDelaySeconds));
			if (input.now >= due) { state.shown = true; update.changed = true; }
			else update.deadline = due;
		}
		return update;
	}

	BarHoverHintRect PositionMainBarHoverHint(UIRendering& renderer, const std::wstring& text,
		double zoom, POINT screen, POINT origin, RECT workArea, double windowWidth, double windowHeight)
	{
		const auto measured = renderer.MeasureText(text,
			BarHoverHintFontSizeDip, DWRITE_FONT_WEIGHT_NORMAL);
		const double textWidth = (std::min)(ceil(static_cast<double>(measured.width)), BarHoverHintMaxTextWidthDip);
		const double textHeight = ceil(static_cast<double>(measured.height));
		const double width = (textWidth + 2.0 * BarHoverHintPaddingXDip) * zoom;
		const double height = (textHeight + 2.0 * BarHoverHintPaddingYDip) * zoom;
		// 可用范围：显示器工作区（换算到布局像素）∩ Bar 视口。
		const double workLeft = (std::max)(0.0, static_cast<double>(workArea.left - origin.x));
		const double workTop = (std::max)(0.0, static_cast<double>(workArea.top - origin.y));
		const double workRight = (std::min)(windowWidth, static_cast<double>(workArea.right - origin.x));
		const double workBottom = (std::min)(windowHeight, static_cast<double>(workArea.bottom - origin.y));
		return ResolveBarHoverHintRect(
			static_cast<double>(screen.x - origin.x), static_cast<double>(screen.y - origin.y), width, height,
			BarHoverHintCursorOffsetDip * zoom, { workLeft, workTop, workRight, workBottom });
	}
}
