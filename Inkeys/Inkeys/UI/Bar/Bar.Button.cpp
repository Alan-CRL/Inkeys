module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../Helper/Ui3PresentationFixtureAuth.h"
#include "../../../IdtMain.h"

#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
// 历史遗留问题
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"

#include "../../../IdtConfiguration.h"
#include "../../Window/Window.Legacy.hpp"
#include <optional>
#include "../../Input/UiTouchRouting.h"

#include <d2d1_1.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <cstdint>

module Inkeys.UI.Bar;
import :Button;
import :MainContent;

import :Main;
import :Layout;
import :Theme;
import Inkeys.UI.Bar.Metrics;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Other.Config;
import Inkeys.UI.Bar.LayoutModel;
import Inkeys.Business.ComponentActions;
import Inkeys.UI.Freeze;
import Inkeys.UI.Setting;
import Inkeys.Window;
import Inkeys.UI.UI3.PresentationProbe;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.Bar.BottomDock;
import Inkeys.UI.Bar.WindowGeometry;
import Inkeys.UI.Bar.A2;
import Inkeys.UI.Bar.Haptics;
import Inkeys.UI.Bar.PresentationSource;

using namespace Inkeys::UI::UI3;

using Inkeys::Business::BuiltInComponentAction;
using Inkeys::Business::ExecuteBuiltInComponentAction;
using Inkeys::UI::Bar::BarClearClickAction;
using Inkeys::UI::Bar::BarToggleChannel;
using Inkeys::UI::Bar::ResolveBarClearClickAction;
using Inkeys::UI::Bar::ResolveBarDrawButtonToggleDecision;

bool BarButtonClass::TransitionContent(
	const wstring& iconResourceName, const wstring& label, bool immediateLabel)
{
	// 先缩到新标签所需字号，再在透明中点换字；换回短标签前保留小字号。
	if (preset == BarButtonPresetEnum::Select || preset == BarButtonPresetEnum::Pierce)
	{
		compactMultilineLabel = label.find(L'\n') != wstring::npos;
		if (compactMultilineLabel && name.size.val > 9.0) name.size.SetDirect(9.0);
	}
	bool changed = false;
	if (iconKind == ButtonIconKindEnum::Svg && !iconResourceName.empty())
		changed |= icon.TransitionToResource(L"UI", iconResourceName);
	// 跨字族切换不能在新字体下等待旧语言文字的淡出中点。
	changed |= immediateLabel ? name.SetStringImmediate(label) : name.TransitionToString(label);
	return changed;
}

void BarButtonSetClass::ExecuteClearClick(bool doubleClickContinuation)
{
	if (!doubleClickContinuation)
	{
		clearAttemptedForDoubleClick = false;
		clearAcceptedForDoubleClick = false;
	}
	const bool selectionMode =
		GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection;
	const BarClearClickAction action = ResolveBarClearClickAction(
		selectionMode, Inkeys::UI::Bar::CurrentPageHasContent(),
		doubleClickContinuation, clearAttemptedForDoubleClick,
		clearAcceptedForDoubleClick);
	if (action == BarClearClickAction::EnterSelection)
	{
		clearAttemptedForDoubleClick = false;
		clearAcceptedForDoubleClick = false;
		if (!selectionMode) ChangeStateModeToSelection();
		return;
	}
	if (action != BarClearClickAction::PublishClear)
	{
		Inkeys::UI::Bar::Haptics::RejectButtonAction();
		clearAttemptedForDoubleClick = false;
		clearAcceptedForDoubleClick = false;
		return;
	}

	const auto result = Inkeys::Drawing::Draw3::PublishProductCommand(
		Inkeys::Drawing::Draw3::Bridge::CommandType::Clear);
	// 只有已进入 Draw3 FIFO 的首次 Clear，才允许双击第二击直接进入选择。
	clearAttemptedForDoubleClick = true;
	clearAcceptedForDoubleClick =
		result == Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted;
	if (!clearAcceptedForDoubleClick) Inkeys::UI::Bar::Haptics::RejectButtonAction();
}

bool BarButtonSetClass::RegisterButton(
	const std::string& id,
	BarButtonClass* button,
	bool allowMultiple,
	BarButtonLayoutZoneEnum zone,
	bool defaultUserVisible,
	const wstring& categoryName,
	const wstring& settingsName,
	bool closeMoreAfterAction)
{
	if (id.empty() || button == nullptr) return false;

	// 官方固定区必须 Inkeys. 前缀；扩展区必须为非 Inkeys. 的点分 ID。
	if (zone == BarButtonLayoutZoneEnum::FixedA1 || zone == BarButtonLayoutZoneEnum::FixedA2)
	{
		if (!Inkeys::IsOfficialBarButtonIdPrefix(id)) return false;
	}
	else if (!Inkeys::IsExtensionBarButtonId(id)
		&& !(zone == BarButtonLayoutZoneEnum::Extension
			&& id == Inkeys::BarButtonId::Setting))
	{
		return false;
	}

	unique_lock lock(registrationMutex);
	if (registrations.contains(id)) return false;

	button->id = id;
	button->only = !allowMultiple;
	button->baseSize = button->size.load();
	button->baseUserVisible = defaultUserVisible;
	button->userVisible = defaultUserVisible;
	button->closeMoreAfterAction = closeMoreAfterAction;
	shared_ptr<BarButtonClass> ownedButton(button);
	registrations.emplace(
		id,
		BarButtonRegistrationClass{
			id,
			move(ownedButton),
			BarButtonRegistrationKindEnum::EntityButton,
			allowMultiple,
			zone,
			button->size.load(),
			defaultUserVisible,
			categoryName,
			settingsName,
			closeMoreAfterAction
		});
	registrationOrder.push_back(id);
	return true;
}

bool BarButtonSetClass::RegisterLayoutMarker(const std::string& id)
{
	if (id != Inkeys::BarButtonId::MoreBoundary) return false;
	unique_lock lock(registrationMutex);
	if (registrations.contains(id)) return false;
	registrations.emplace(id, BarButtonRegistrationClass{
		id,
		nullptr,
		BarButtonRegistrationKindEnum::LayoutMarker,
		false,
		BarButtonLayoutZoneEnum::Extension,
		ButtonSizeEnum::twoTwo,
		true,
		{},
		{},
		true
	});
	registrationOrder.push_back(id);
	return true;
}

bool BarButtonSetClass::TryGetRegistration(const std::string& id, BarButtonRegistrationClass& outRegistration) const
{
	shared_lock lock(registrationMutex);
	auto registration = registrations.find(id);
	if (registration == registrations.end()) return false;

	outRegistration = registration->second;
	return true;
}

vector<BarButtonRegistrationClass> BarButtonSetClass::GetExtensionRegistrations() const
{
	shared_lock lock(registrationMutex);
	vector<BarButtonRegistrationClass> result;
	result.reserve(registrationOrder.size());
	for (const std::string& id : registrationOrder)
	{
		auto registration = registrations.find(id);
		if (registration == registrations.end()) continue;
		if (registration->second.zone != BarButtonLayoutZoneEnum::Extension) continue;
		result.push_back(registration->second);
	}
	return result;
}

BarMoreButtonSnapshotClass BarButtonSetClass::GetMoreButtonSnapshot() const
{
	lock_guard lock(moreSnapshotMutex);
	return moreSnapshot;
}

void BarButtonSetClass::PresetInitialization()
{
	const COLORREF defaultButtonFill = GetThemeColor(ThemeColorEnum::Surface);
	const COLORREF defaultIconColor = GetThemeColor(ThemeColorEnum::IconPrimary);

	// 分隔线
	{
		BarButtonClass* obj = new BarButtonClass;
		const COLORREF dividerColor = GetThemeColor(ThemeColorEnum::Divider);
		{
			obj->size = ButtonSizeEnum::oneTwo;
			obj->preset = BarButtonPresetEnum::Divider;
			obj->hide = false;

			obj->only = false; // 允许多个分隔线
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"", 0.0);
			obj->name.enable.Initialization(false);
		}
		{
			// Divider 直接使用 Shape 细线，SVG 资源继续保留但不参与视觉。
			obj->button.Initialization(0.0, 0.0, 1.0, 50.0, 0.5, 0.5,
				1.0, dividerColor, dividerColor);
			obj->button.pct.Initialization(0.30);
			obj->button.framePct = UiPctClass(0.0);
			obj->button.frameLightPct = UiPctClass(0.0);
			obj->button.frameRendering = UiFrameRenderingEnum::PointLight;
			obj->button.frameLightColor = UiFrameLightColorEnum::Frame;
			obj->button.framePrimaryLightEnabled = false;
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.pct.Initialization(0.0);
			obj->icon.enable.Initialization(false);
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 选择
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Select;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.SelectLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barSelect");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					ChangeStateModeToSelectionFromButton();
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 绘制
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Draw;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.DrawLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barBrush1");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					// 真 Draw callback 第一业务写前再核当前 Up 和同一个 Pen baseline。
					if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(Inkeys::UI::UI3::Ui3FiniteScene::DrawAttribute,
						Inkeys::UI::Bar::Ui3FixtureActionPoint::Callback)) { Inkeys::UI::Bar::Haptics::RejectButtonAction(); return; }
					// 可视化期间底层借用的工具身份不算已选中：点击必须真正切换工具才能退出可视化。
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen || IsCanvasDebugVisualActive())
					{
						Inkeys::UI::UI3::RejectCurrentUi3FiniteBusiness(Inkeys::UI::UI3::Ui3FiniteStatus::UnsupportedState);
						if (ChangeStateModeToPen())
						{
							barUISet.barState.drawAttribute = false;
							barUISet.barState.geometryAttribute = false;
						}
						else Inkeys::UI::Bar::Haptics::RejectButtonAction();
					}
					else
					{
						if (!barUISet.TryBeginToggle(
							BarToggleChannel::DrawAttribute))
						{
							Inkeys::UI::UI3::RejectCurrentUi3FiniteBusiness(Inkeys::UI::UI3::Ui3FiniteStatus::RejectedByBusiness);
							Inkeys::UI::Bar::Haptics::RejectButtonAction();
							return;
						}
						// 只在真实toggle接受点标记，之后仍走原决策与StateUpdate规范化。
						Inkeys::UI::UI3::MarkCurrentUi3FiniteBusinessAccepted();
						const auto decision = ResolveBarDrawButtonToggleDecision(
							barUISet.barState.drawAttribute);
						if (decision.openDrawAttribute)
						{
							barUISet.barState.moreExpanded = false;
							barUISet.barState.geometryAttribute = false;
						}
						barUISet.barState.drawAttribute =
							decision.openDrawAttribute;
					}
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 擦除
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Eraser;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.EraserLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barEraser");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					const auto selectedMode = GetStateModeSnapshot().StateModeSelect;
					if (selectedMode != StateModeSelectEnum::IdtEraser || IsCanvasDebugVisualActive())
					{
						// 切工具的首击不参与开合合并，下一击仍可立即展开属性栏。
						barUISet.eraserAttribute.RememberEntryFromSelection(
							selectedMode == StateModeSelectEnum::IdtSelection &&
							Inkeys::UI::Bar::CurrentPageHasContent());
						ChangeStateModeToEraser();
					}
					else if (barUISet.TryBeginToggle(BarToggleChannel::EraserAttribute))
					{
						const bool open = !barUISet.barState.eraserAttribute;
						barUISet.CollapseAuxiliaryPanels();
						barUISet.barState.eraserAttribute = open;
					}
					else Inkeys::UI::Bar::Haptics::RejectButtonAction();
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 几何
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Geometry;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.GeometryLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, defaultIconColor);
			obj->icon.InitializationFromResource(L"UI", L"barGeometry");
			obj->icon.enable.Initialization(true);
			// 固定工具的大头针角标；在发布给渲染线程前挂载。
			obj->badge.emplace();
			Inkeys::UI::UI3::InitializeButtonBadge(*obj->badge);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtShape || IsCanvasDebugVisualActive())
					{
						// 首次只切换工具；和绘制属性一致，再次点击才展开面板。
						if (ChangeStateModeToShape())
						{
							barUISet.barState.drawAttribute = false;
							barUISet.barState.geometryAttribute = false;
						}
						else Inkeys::UI::Bar::Haptics::RejectButtonAction();
					}
					else
					{
						if (!barUISet.TryBeginToggle(
							BarToggleChannel::GeometryAttribute)) { Inkeys::UI::Bar::Haptics::RejectButtonAction(); return; }
						bool open = !static_cast<bool>(barUISet.barState.geometryAttribute);
						if (open)
						{
							barUISet.barState.moreExpanded = false;
							barUISet.barState.drawAttribute = false;
						}
						if (open) barUISet.recallAttribute.Close(barUISet);
						barUISet.barState.geometryAttribute = open;
					}
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 撤回
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Recall;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.UndoLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barRecall");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					// 灰色不可用时不发命令，但连点会延长已有的 3 秒保护。
					barUISet.OpenRecallFromUndo();
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 主栏 Redo：默认隐藏。浮窗收起且 Redo 仍可用时变成 Undo 上方的 2×1。
	{
		mainRedoButton = std::make_shared<BarButtonClass>();
		BarButtonClass* obj = mainRedoButton.get();
		obj->size = ButtonSizeEnum::twoOne;
		obj->preset = BarButtonPresetEnum::Redo;
		obj->hide = true;
		obj->userVisible = true;
		obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
			IW(I18nKey.UI.Bar.MainButtons.RedoLabel), 0.0);
		obj->name.enable.Initialization(true);
		obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
		obj->button.enable.Initialization(true);
		obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
		obj->icon.InitializationFromResource(L"UI", L"barRedo");
		obj->icon.enable.Initialization(true);
		obj->clickFunc = []() -> void
			{
				// 点中后 Redo 变成 2×2，Undo 进入浮窗；灰色时不发命令。
				barUISet.OpenRecallFromRedo();
			};
		obj->state = &barButtonState[(int)BarButtonPresetEnum::Redo];
		preset[(int)BarButtonPresetEnum::Redo] = obj;
	}
	// 清空
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Clean;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.ClearLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barClean");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					ExecuteClearClick(false);
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 白板
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoOne;
			obj->preset = BarButtonPresetEnum::Whiteboard;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.WhiteboardLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0,
				nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barWhiteboard");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = []() -> void
				{
					auto& service = Inkeys::Window::GetService();
					Inkeys::UI::Bar::DispatchBarWhiteboardButtonAction(WhiteboardRequested(),
						Inkeys::UI::Bar::WhiteboardActive() && service.WhiteboardWindowGroupMinimized(), WorkspaceTransitionFailureRevision(),
						[] { if (!RequestWhiteboardWindowView(false)) Inkeys::UI::Bar::Haptics::RejectButtonAction(); },
						[](bool active) { RequestWhiteboardActive(active); },
						[](std::uint64_t failure) { (void)CancelFailedWhiteboardTransitionIfRevision(failure); });
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 独立穿透只进入运行时列表，保留旧 A2 配置迁移边界。
	{
		mainPierceButton = std::make_shared<BarButtonClass>();
		auto* obj = mainPierceButton.get();
		obj->size = ButtonSizeEnum::twoOne;
		obj->preset = BarButtonPresetEnum::Pierce;
		obj->hide = true;
		obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
			IW(I18nKey.UI.Bar.MainButtons.ClickThroughLabel), 0.0);
		obj->name.enable.Initialization(true);
		obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
		obj->button.enable.Initialization(true);
		obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
		obj->icon.InitializationFromResource(L"UI", L"barPierce");
		obj->icon.enable.Initialization(true);
		obj->clickFunc = [] { ToggleClickThroughFromButton(); };
		obj->state = &barButtonState[(int)BarButtonPresetEnum::Pierce];
		preset[(int)BarButtonPresetEnum::Pierce] = obj;
	}

	// 画布调试可视化：默认隐藏，画布调试开启时由统一主栏投影显示。
	{
		mainCanvasDebugButton = std::make_shared<BarButtonClass>();
		auto* obj = mainCanvasDebugButton.get();
		obj->size = ButtonSizeEnum::twoTwo;
		obj->preset = BarButtonPresetEnum::CanvasDebug;
		obj->hide = true;
		obj->userVisible = true;
		obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
			IW(I18nKey.UI.Bar.MainButtons.CanvasDebugLabel), 0.0);
		obj->name.enable.Initialization(true);
		obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
		obj->button.enable.Initialization(true);
		obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
		obj->icon.InitializationFromResource(L"UI", L"barCanvasDebug");
		obj->icon.enable.Initialization(true);
		obj->clickFunc = []
			{
				if (!ToggleCanvasDebugVisualFromButton()) Inkeys::UI::Bar::Haptics::RejectButtonAction();
			};
		obj->state = &barButtonState[(int)BarButtonPresetEnum::CanvasDebug];
		preset[(int)BarButtonPresetEnum::CanvasDebug] = obj;
	}

	// 定格
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Freeze;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.FreezeLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barFreeze");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					if (!ToggleFreezeFromButton()) Inkeys::UI::Bar::Haptics::RejectButtonAction();
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 结束放映
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::EndShow;
			obj->hide = true;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.EndPresentationLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0,
				nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barEndShow");
			obj->icon.enable.Initialization(true);
		}

		obj->clickFunc = []() { Inkeys::UI::Bar::RequestEndShow(); };
		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 设置
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = ButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Setting;
			obj->hide = false;

		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0,
				IW(I18nKey.UI.Bar.MainButtons.SettingsLabel), 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->button.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barSetting");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					Inkeys::UI::Setting::Toggle();
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 官方按钮使用稳定 ID；A1/A2 固定区与扩展区在注册时写死分区和默认显隐。
	RegisterButton(Inkeys::BarButtonId::Select, preset[(int)BarButtonPresetEnum::Select], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Draw, preset[(int)BarButtonPresetEnum::Draw], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Eraser, preset[(int)BarButtonPresetEnum::Eraser], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Geometry, preset[(int)BarButtonPresetEnum::Geometry], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Recall, preset[(int)BarButtonPresetEnum::Recall], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Clean, preset[(int)BarButtonPresetEnum::Clean], false, BarButtonLayoutZoneEnum::FixedA1);
	// Divider 不进 A1 配置 required 集；仅作运行时交界注入模板（可多实例拷贝）。
	RegisterButton(Inkeys::BarButtonId::Divider, preset[(int)BarButtonPresetEnum::Divider], true, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Whiteboard, preset[(int)BarButtonPresetEnum::Whiteboard], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::Freeze, preset[(int)BarButtonPresetEnum::Freeze], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::EndShow, preset[(int)BarButtonPresetEnum::EndShow], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::Setting, preset[(int)BarButtonPresetEnum::Setting], false, BarButtonLayoutZoneEnum::Extension);
	RegisterLayoutMarker(Inkeys::BarButtonId::MoreBoundary);

	BarButtonRegistrationClass dividerRegistration;
	if (TryGetRegistration(Inkeys::BarButtonId::Divider, dividerRegistration)
		&& dividerRegistration.button)
	{
		// 两条交界线由集合长期持有，运行时替换列表时不会释放正在绘制的对象。
		auto* userDivider = new BarButtonClass(*dividerRegistration.button);
		userDivider->state = &userDivider->localState;
		if (!RegisterButton(Inkeys::BarButtonId::UserDivider, userDivider, true, BarButtonLayoutZoneEnum::FixedA1))
			delete userDivider;
		boundaryDividers[0] = make_shared<BarButtonClass>(*dividerRegistration.button);
		boundaryDividers[1] = make_shared<BarButtonClass>(*dividerRegistration.button);
	}
	// 更多入口是硬编码控件，不进入注册表或持久化序列。
	moreButton = make_shared<BarButtonClass>();
	moreButton->id = Inkeys::BarButtonId::MoreBoundary;
	moreButton->preset = BarButtonPresetEnum::More;
	moreButton->size = ButtonSizeEnum::twoTwo;
	moreButton->hide = false;
	moreButton->userVisible = true;
	moreButton->name.Initialization(0.0, 0.0, 0.0, 0.0,
		IW(I18nKey.UI.Bar.MainButtons.MoreLabel), 0.0);
	moreButton->name.enable.Initialization(true);
	moreButton->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
	moreButton->button.enable.Initialization(true);
	moreButton->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
	moreButton->icon.InitializationFromResource(L"UI", L"barMore");
	moreButton->icon.enable.Initialization(true);
	moreButton->state = &moreButton->localState;
}

void BarButtonSetClass::RegisterBuiltInComponents()
{
	const auto& componentText = I18nKey.SettingsUI.Component;
	const auto& componentLabels = I18nKey.UI.Bar.Components;
	const COLORREF defaultButtonFill = GetThemeColor(ThemeColorEnum::Surface);
	const COLORREF defaultIconColor = GetThemeColor(ThemeColorEnum::IconPrimary);

	auto registerComponent = [&](const char* id,
		const wchar_t* categoryName,
		const wchar_t* settingsName,
		const wchar_t* shortText,
		const wchar_t* iconResource,
		BuiltInComponentAction action)
	{
		BarButtonRegistrationClass existingRegistration;
		if (TryGetRegistration(id, existingRegistration)) return;

		BarButtonClass* obj = new BarButtonClass;
		obj->size = ButtonSizeEnum::twoTwo;
		obj->hide = false;
		obj->only = true;

		obj->name.Initialization(0.0, 0.0, 0.0, 0.0, shortText, 0.0);
		obj->name.enable.Initialization(true);
		obj->button.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
		obj->button.enable.Initialization(true);

		// 组件图标为主题占位色 SVG，与其他按钮共用着色与选中/悬停变色。
		obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
		obj->icon.InitializationFromResource(L"UI", iconResource);
		if (obj->icon.svg.GetVal().empty())
		{
			delete obj;
			return;
		}
		obj->icon.enable.Initialization(true);
		obj->clickFunc = [action]()
		{
			switch (action)
			{
			case BuiltInComponentAction::Escape:
			case BuiltInComponentAction::AltF4:
			case BuiltInComponentAction::LockWorkStation:
				ExecuteBuiltInComponentAction(action);
				break;
			default:
				// 组件 Shell/COM/URI 与 quickDraw join 交给有序业务队列，不阻塞 UI3 worker。
				if (!Inkeys::UI::UI3::QueueInteractionComponentAction([action] { ExecuteBuiltInComponentAction(action); }))
					Inkeys::UI::Bar::Haptics::RejectButtonAction();
				break;
			}
		};

		if (!RegisterButton(
			id, obj, true, BarButtonLayoutZoneEnum::Extension, true,
			categoryName, settingsName))
		{
			delete obj;
		}
	};

	// 顺序与设置页的分组顺序一致，启动导入按同一顺序处理。
	registerComponent(
		Inkeys::BarComponentId::Explorer,
		IW(componentText.SoftwareSection).c_str(), IW(componentText.ExplorerTitle).c_str(), IW(componentLabels.ExplorerLabel).c_str(), L"compExplorer",
		BuiltInComponentAction::Explorer);
	registerComponent(
		Inkeys::BarComponentId::Taskmgr,
		IW(componentText.SoftwareSection).c_str(), IW(componentText.TaskManagerTitle).c_str(), IW(componentLabels.TaskManagerLabel).c_str(), L"compTaskManager",
		BuiltInComponentAction::TaskManager);
	registerComponent(
		Inkeys::BarComponentId::Control,
		IW(componentText.SoftwareSection).c_str(), IW(componentText.ControlPanelTitle).c_str(), IW(componentLabels.ControlPanelLabel).c_str(), L"compControlPanel",
		BuiltInComponentAction::ControlPanel);

	registerComponent(
		Inkeys::BarComponentId::Desktop,
		IW(componentText.SystemSection).c_str(), IW(componentText.ShowDesktopTitle).c_str(), IW(componentLabels.ShowDesktopLabel).c_str(), L"compShowDesktop",
		BuiltInComponentAction::ShowDesktop);
	registerComponent(
		Inkeys::BarComponentId::LockWorkStation,
		IW(componentText.SystemSection).c_str(), IW(componentText.LockScreenTitle).c_str(), IW(componentLabels.LockScreenLabel).c_str(), L"compLockScreen",
		BuiltInComponentAction::LockWorkStation);

	registerComponent(
		Inkeys::BarComponentId::Keyboardesc,
		IW(componentText.KeyboardSection).c_str(), IW(componentText.EscapeTitle).c_str(), IW(componentLabels.EscapeLabel).c_str(), L"compEscape",
		BuiltInComponentAction::Escape);
	registerComponent(
		Inkeys::BarComponentId::KeyboardAltF4,
		IW(componentText.KeyboardSection).c_str(), IW(componentText.AltF4Title).c_str(), L"Alt+F4", L"compAltF4",
		BuiltInComponentAction::AltF4);

	registerComponent(
		Inkeys::BarComponentId::IslandCaller1,
		IW(componentText.RollCallSection).c_str(), IW(componentText.IslandCaller1Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallRandom",
		BuiltInComponentAction::IslandCaller);
	registerComponent(
		Inkeys::BarComponentId::IslandCaller2,
		IW(componentText.RollCallSection).c_str(), IW(componentText.IslandCaller2Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallSingle",
		BuiltInComponentAction::IslandCallerSimple);
	registerComponent(
		Inkeys::BarComponentId::SecRandom1,
		IW(componentText.RollCallSection).c_str(), IW(componentText.SecRandom1Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallQuick",
		BuiltInComponentAction::SecRandomDirect);
	registerComponent(
		Inkeys::BarComponentId::SecRandom2,
		IW(componentText.RollCallSection).c_str(), IW(componentText.SecRandom2Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallQuick2",
		BuiltInComponentAction::SecRandomQuickDraw);
	registerComponent(
		Inkeys::BarComponentId::SecRandom2Compat,
		IW(componentText.RollCallSection).c_str(), IW(componentText.SecRandom2CompatTitle).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallQuickCompat",
		BuiltInComponentAction::SecRandomQuickDrawCompat);
	registerComponent(
		Inkeys::BarComponentId::NamePicker,
		IW(componentText.RollCallSection).c_str(), IW(componentText.NamePickerTitle).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compNamePicker",
		BuiltInComponentAction::NamePicker);

	registerComponent(
		Inkeys::BarComponentId::ClassislandSettings,
		IW(componentText.ClassIslandSection).c_str(), IW(componentText.ClassIslandSettingsTitle).c_str(), IW(componentLabels.ClassIslandSettingsLabel).c_str(), L"compClassSettings",
		BuiltInComponentAction::ClassIslandSettings);
	registerComponent(
		Inkeys::BarComponentId::ClassislandProfile,
		IW(componentText.ClassIslandSection).c_str(), IW(componentText.ClassIslandProfileTitle).c_str(), IW(componentLabels.ClassIslandProfileLabel).c_str(), L"compClassProfile",
		BuiltInComponentAction::ClassIslandProfile);
	registerComponent(
		Inkeys::BarComponentId::ClassislandClassswap,
		IW(componentText.ClassIslandSection).c_str(), IW(componentText.ClassIslandSwapTitle).c_str(), IW(componentLabels.ClassIslandSwapLabel).c_str(), L"compClassSwap",
		BuiltInComponentAction::ClassIslandClassSwap);
}

void BarButtonSetClass::StateUpdate()
{
	StateUpdate(GetStateModeSnapshot());
}
void BarButtonSetClass::StateUpdate(const StateModeClass& stateMode)
{
	PrepareMainPanels(stateMode);
	UpdateMainPresentation(stateMode);
}
void BarButtonSetClass::PrepareMainPanels(const StateModeClass& stateMode)
{
	if (!preset[(int)BarButtonPresetEnum::Draw]) return;
	// 面板Close可能发布轻量刷新，放在表现锁外；主栏最终目标随后统一应用。
	PresetHoming(stateMode);
}
void BarButtonSetClass::UpdateMainPresentation(const StateModeClass& stateMode)
{
	lock_guard lock(presentationMutex);
	ApplyMainPresentationLocked(stateMode);
}
vector<BarMainButtonLayoutEntry> BarButtonSetClass::MainLayoutSnapshot()
{
	lock_guard lock(presentationMutex);
	vector<BarMainButtonLayoutEntry> result;
	result.reserve(tot);
	for (int index = 0; index < tot; ++index)
		if (auto* button = buttonList.Get(index))
			result.push_back({ button, button->size.load(), button->IsVisible() });
	return result;
}
void BarButtonSetClass::ApplyMainPresentationLocked(const StateModeClass& stateMode, bool immediateLabel)
{
	if (!preset[(int)BarButtonPresetEnum::Draw]) return;
	// 所有动态网格共用这一组输入，内容缓存不能短路几何投影。
	const bool whiteboard = Inkeys::UI::Bar::WhiteboardActive();
	const bool presentation = Inkeys::UI::Bar::PptPresentationActive();
	const bool hasContent = Inkeys::UI::Bar::CurrentPageHasContent();
	const bool featureEnabled = IsWhiteboardFeatureEnabled();
	const bool separate = GetSelectAndClickThroughMode() == 1;
	const bool selection = stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection;
	const bool compactSelection = !whiteboard && selection && !hasContent;
	const bool freezeAvailable = Inkeys::UI::Freeze::IsAvailable();
	const bool freezeActive = Inkeys::UI::Freeze::IsActive();
	// 画布调试开关与可视化位均为无锁读取，不会在表现锁内嵌套模式锁。
	const bool canvasDebugEnabled = IsCanvasDebugEnabled();
	const bool canvasDebugVisual = IsCanvasDebugVisualActive();
	const bool settingVisible = Inkeys::UI::Setting::IsVisible();
	const bool fold = barUISet.barState.fold;
	const bool hasMoreButtons = GetMoreButtonSnapshot().HasButtons();
	const bool moreOpen = !fold && barUISet.barState.moreExpanded && hasMoreButtons;
	const auto a2 = Inkeys::UI::Bar::ResolveBarA2Projection(
		presentation, whiteboard, featureEnabled, separate, selection && !hasContent, freezeAvailable);
	const auto recall = barUISet.RecallChromeView(compactSelection);
	if (fold || !hasMoreButtons) barUISet.barState.moreExpanded = false;
	// 读取渲染线程已落地的档位，保证投影与本帧布局使用同一尺寸。
	const bool compactSize = NormalizeBarSizeMode(barUISet.barStyle.appliedSizeMode.load(
		std::memory_order_acquire)) == BarSizeMode::Compact;
	const Inkeys::UI::Bar::MainBarPresentationInput input{ stateMode.StateModeSelect,
		whiteboard, hasContent, featureEnabled, freezeAvailable,
		freezeActive, canvasDebugEnabled, canvasDebugVisual, settingVisible, moreOpen, compactSize, a2, recall };
	const auto apply = [&](BarButtonClass* button)
	{
		if (!button) return;
		const auto [size, hide, state] = Inkeys::UI::Bar::ResolveMainBarPresentation(
			button->preset.load(), button->baseSize.load(), input);
		button->size = size;
		button->userVisible = button->baseUserVisible.load();
		button->hide = hide;
		button->state->state = state;
	};
	{
		shared_lock lock(registrationMutex);
		for (const auto& [id, registration] : registrations) apply(registration.button.get());
	}
	for (const auto& segment : instances)
		for (const auto& [id, group] : segment)
			for (const auto& instance : group) apply(instance.button.get());
	for (const auto& divider : boundaryDividers) apply(divider.get());
	apply(moreButton.get()); apply(mainRedoButton.get()); apply(mainPierceButton.get());
	apply(mainCanvasDebugButton.get());
	barUISet.barState.recallAttribute = recall.popupOpen;
	// 内容仍从当前动画值过渡；同目标不会重启文字或图标动画。
	// 长标签换行由语言资源指定，翻译调整不再依赖英文原文匹配。
	const auto style = Inkeys::UI::Bar::ResolveSelectStyle(whiteboard, separate, selection);
	preset[(int)BarButtonPresetEnum::Select]->TransitionContent(style.icon, style.LocalizedLabel(I18n::getW, Inkeys::UI::Bar::ShapeToolLabel), immediateLabel);
	UpdateDrawButtonStyle(stateMode, immediateLabel);
	UpdateWhiteboardButtonStyle(whiteboard,
		Inkeys::Window::GetService().WhiteboardWindowGroupMinimized(), immediateLabel);
	UpdateEraserButtonStyle(stateMode, immediateLabel);
	UpdateGeometryButtonStyle(stateMode, immediateLabel);
}
void BarButtonSetClass::RefreshLocalizedText()
{
	lock_guard lock(presentationMutex);
	// 原位替换文字，不重建按钮或丢失按压/工具状态。
	if (auto* button = preset[(int)BarButtonPresetEnum::Recall]) button->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.UndoLabel));
	if (auto* button = preset[(int)BarButtonPresetEnum::Clean]) button->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.ClearLabel));
	if (auto* button = preset[(int)BarButtonPresetEnum::Freeze]) button->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.FreezeLabel));
	if (auto* button = preset[(int)BarButtonPresetEnum::EndShow]) button->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.EndPresentationLabel));
	if (auto* button = preset[(int)BarButtonPresetEnum::Setting]) button->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.SettingsLabel));
	if (moreButton) moreButton->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.MoreLabel));
	if (mainRedoButton) mainRedoButton->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.RedoLabel));
	if (mainCanvasDebugButton) mainCanvasDebugButton->name.SetStringImmediate(IW(I18nKey.UI.Bar.MainButtons.CanvasDebugLabel));
	drawButtonStyleKey = -1; eraserButtonStyleKey = -1; geometryButtonStyleKey = -1;
	unique_lock registrationsLock(registrationMutex);
	if (auto it = registrations.find(Inkeys::BarComponentId::Explorer); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SoftwareSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ExplorerTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ExplorerLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::Taskmgr); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SoftwareSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.TaskManagerTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.TaskManagerLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::Control); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SoftwareSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ControlPanelTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ControlPanelLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::Desktop); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SystemSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ShowDesktopTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ShowDesktopLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::LockWorkStation); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SystemSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.LockScreenTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.LockScreenLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::Keyboardesc); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.KeyboardSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.EscapeTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.EscapeLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::KeyboardAltF4); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.KeyboardSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.AltF4Title);
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::IslandCaller1); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.IslandCaller1Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::IslandCaller2); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.IslandCaller2Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::SecRandom1); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.SecRandom1Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::SecRandom2); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.SecRandom2Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::SecRandom2Compat); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.SecRandom2CompatTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::NamePicker); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.NamePickerTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::ClassislandSettings); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.ClassIslandSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ClassIslandSettingsTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ClassIslandSettingsLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::ClassislandProfile); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.ClassIslandSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ClassIslandProfileTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ClassIslandProfileLabel));
	}
	if (auto it = registrations.find(Inkeys::BarComponentId::ClassislandClassswap); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.ClassIslandSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ClassIslandSwapTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ClassIslandSwapLabel));
	}
	for (const auto& segment : instances)
		for (const auto& [id, group] : segment)
			for (const auto& instance : group)
				instance.button->name.SetStringImmediate(registrations.at(id).button->name.content.GetTar());
	registrationsLock.unlock();
	// 复用当前业务状态投影，立即替换动态文案而不重新触发工具动作。
	ApplyMainPresentationLocked(GetStateModeSnapshot(), true);
}

void BarButtonSetClass::UpdateDrawButtonStyle(const StateModeClass& stateMode, bool immediateLabel)
{
	static mutex mtx;
	bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtPen && !IsCanvasDebugVisualActive();
	bool laser = stateMode.laserActive;
	bool highlighter =
		!laser && stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1;
	bool hardPen = !laser && !highlighter &&
		stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen;
	int styleKey = (selected ? 2 : 0) + (highlighter ? 1 : 0) +
		(laser ? 4 : 0) + (hardPen ? 8 : 0);
	if (drawButtonStyleKey == styleKey) return;

	lock_guard<mutex> lock(mtx);
	if (drawButtonStyleKey == styleKey) return;
	auto button = preset[(int)BarButtonPresetEnum::Draw];
	if (!button) return;
	const auto style = Inkeys::UI::Bar::ResolveDrawStyle(selected, laser, highlighter, hardPen);
	button->TransitionContent(style.icon, style.LocalizedLabel(I18n::getW, Inkeys::UI::Bar::ShapeToolLabel), immediateLabel);
	drawButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateWhiteboardButtonStyle(bool active, bool minimized, bool immediateLabel)
{
	auto whiteboard = preset[(int)BarButtonPresetEnum::Whiteboard];
	if (!whiteboard) return;
	if (auto* pierce = preset[(int)BarButtonPresetEnum::Pierce])
	{
		const auto style = Inkeys::UI::Bar::ResolvePierceStyle();
		pierce->TransitionContent(style.icon, style.LocalizedLabel(I18n::getW, Inkeys::UI::Bar::ShapeToolLabel), immediateLabel);
	}
	const bool resume = active && minimized;
	const bool returnToShow = active && !resume && Inkeys::UI::Bar::PptPresentationActive();
	const auto style = Inkeys::UI::Bar::ResolveWhiteboardStyle(active, resume, returnToShow);
	whiteboard->TransitionContent(style.icon, style.LocalizedLabel(I18n::getW, Inkeys::UI::Bar::ShapeToolLabel), immediateLabel);
}
void BarButtonSetClass::UpdateEraserButtonStyle(const StateModeClass& stateMode, bool immediateLabel)
{
	static mutex mtx;
	bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser && !IsCanvasDebugVisualActive();
	int styleKey = selected ? 1 : 0;
	if (eraserButtonStyleKey == styleKey) return;

	lock_guard<mutex> lock(mtx);
	if (eraserButtonStyleKey == styleKey) return;
	auto button = preset[(int)BarButtonPresetEnum::Eraser];
	if (!button) return;
	const auto style = Inkeys::UI::Bar::ResolveEraserStyle(selected);
	button->TransitionContent(style.icon, style.LocalizedLabel(I18n::getW, Inkeys::UI::Bar::ShapeToolLabel), immediateLabel);
	eraserButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateGeometryButtonStyle(const StateModeClass& stateMode, bool immediateLabel)
{
	static mutex mtx;
	const bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtShape && !IsCanvasDebugVisualActive();
	const auto tool = GetShapeToolSpec(stateMode);
	// 当前图形工具处于固定状态时显示角标；固定位并入 styleKey，切换时才重投影。
	const bool pinned = selected && IsShapeToolPinned(stateMode);
	const int styleKey = selected ? 1 + static_cast<int>(tool.preset) * 100
		+ static_cast<int>(tool.creationMode) * 16 + static_cast<int>(tool.sides) + (pinned ? 1 << 20 : 0) : 0;
	if (geometryButtonStyleKey == styleKey) return;
	lock_guard<mutex> lock(mtx);
	if (geometryButtonStyleKey == styleKey) return;
	auto* button = preset[(int)BarButtonPresetEnum::Geometry];
	if (!button) return;
	const auto style = Inkeys::UI::Bar::ResolveGeometryStyle(selected, pinned, tool);
	if (button->badge) button->badge->shown = style.badge;
	button->TransitionContent(style.icon, style.LocalizedLabel(I18n::getW, Inkeys::UI::Bar::ShapeToolLabel), immediateLabel);
	geometryButtonStyleKey = styleKey;
}

Inkeys::BarButtonSizeKind BarButtonSetClass::ToConfigSize(ButtonSizeEnum size)
{
	switch (size)
	{
	case ButtonSizeEnum::twoTwo: return Inkeys::BarButtonSizeKind::TwoTwo;
	case ButtonSizeEnum::twoOne: return Inkeys::BarButtonSizeKind::TwoOne;
	case ButtonSizeEnum::oneTwo: return Inkeys::BarButtonSizeKind::OneTwo;
	case ButtonSizeEnum::oneOne: return Inkeys::BarButtonSizeKind::OneOne;
	}
	return Inkeys::BarButtonSizeKind::TwoTwo;
}

ButtonSizeEnum BarButtonSetClass::ToRuntimeSize(Inkeys::BarButtonSizeKind size)
{
	switch (size)
	{
	case Inkeys::BarButtonSizeKind::TwoTwo: return ButtonSizeEnum::twoTwo;
	case Inkeys::BarButtonSizeKind::TwoOne: return ButtonSizeEnum::twoOne;
	case Inkeys::BarButtonSizeKind::OneTwo: return ButtonSizeEnum::oneTwo;
	case Inkeys::BarButtonSizeKind::OneOne: return ButtonSizeEnum::oneOne;
	}
	return ButtonSizeEnum::twoTwo;
}

Inkeys::UI::Bar::BarLayoutCatalog BarButtonSetClass::GetLayoutCatalog() const
{
	using namespace Inkeys::UI::Bar;
	BarLayoutCatalog catalog;
	shared_lock lock(registrationMutex);
	for (const auto& [id, entry] : registrations)
	{
		auto zone = entry.zone == BarButtonLayoutZoneEnum::FixedA1 ? Segment::A1 :
			entry.zone == BarButtonLayoutZoneEnum::FixedA2 ? Segment::A2 : Segment::B1;
		catalog.emplace(id, BarLayoutDescriptor{ zone, ToConfigSize(entry.defaultSize),
			entry.kind == BarButtonRegistrationKindEnum::EntityButton, entry.allowMultiple });
	}
	return catalog;
}

void BarButtonSetClass::InitializeBarLayout()
{
	using namespace Inkeys::UI::Bar;
	lock_guard applyLock(layoutApplyMutex);
	if (ReadBarLayout()) return;
	const auto enabled = Inkeys::config.GetLegacyBarComponents();
	const auto values = Inkeys::config.GetBarLayout();
	auto model = ImportLegacy(FromConfig(values, GetLayoutCatalog()), enabled);
	auto normalized = ToConfig(model);
	const bool needsSave = !enabled.empty() || values != normalized;
	if (needsSave) Inkeys::config.ReplaceBarLayout(std::move(normalized), true);
	{
		lock_guard lock(layoutCommitMutex);
		committedLayout = std::move(model);
	}
	// 启动初始化不在渲染回调中，沿用旧 Load 的启动期同步保存；失败时内存结果完整，
	// 磁盘仍保留旧开关，下次启动会按同一规则重新导入，之后任意配置写入也会带上新布局。
	if (needsSave) (void)Inkeys::config.Write();
}

std::optional<Inkeys::UI::Bar::BarLayoutModel> BarButtonSetClass::ReadBarLayout() const
{
	lock_guard lock(layoutCommitMutex);
	return committedLayout;
}

BarButtonSetClass::ApplyResult BarButtonSetClass::ApplyBarLayout(
	const Inkeys::UI::Bar::BarLayoutModel& expected, const Inkeys::UI::Bar::BarLayoutModel& next, bool resetLegacy)
{
	using namespace Inkeys::UI::Bar;
	// 提交者串行校验与替换；渲染只取已发布的内存快照。这里不写盘：设置页运行在共享渲染线程上，
	// 调用方在 Applied 后经设置业务队列写出最新完整配置。
	lock_guard applyLock(layoutApplyMutex);
	if (ReadBarLayout() != expected) return ApplyResult::Stale;
	auto model = FromConfig(ToConfig(next), GetLayoutCatalog());
	auto normalized = ToConfig(model);
	const bool changed = Inkeys::config.GetBarLayout() != normalized;
	const bool cleared = (changed || resetLegacy) && Inkeys::config.ReplaceBarLayout(std::move(normalized), resetLegacy);
	if (!changed && !cleared) return ApplyResult::Unchanged;
	// 只清旧开关时布局不变，不必重载主栏（重载会让命中暂停一帧）。
	if (changed)
	{
		{
			lock_guard lock(layoutCommitMutex);
			committedLayout = std::move(model);
			reloadPending.store(true);
		}
		barUISet.UpdateRendering(false);
	}
	return ApplyResult::Applied;
}

bool BarButtonSetClass::ConsumeLayoutReload()
{
	if (!reloadPending.exchange(false)) return false;
	// 先暂停命中、递增代次，再替换列表：交互线程此后冻结的命中一律作废，
	// 之前已按下的手势在抬起时因代次不符而不执行动作（实例被复用时同样有效）。
	layoutHitSuspended.store(true, std::memory_order_release);
	layoutGeneration.fetch_add(1, std::memory_order_acq_rel);
	Load();
	return true;
}

bool BarButtonSetClass::IsActiveLayoutButton(const shared_ptr<BarButtonClass>& button, bool inMore) const
{
	lock_guard lock(presentationMutex);
	const auto members = inMore ? GetMoreButtonSnapshot().explicitMore : buttonList.Snapshot();
	return button && button->IsVisible() && std::find(members.begin(), members.end(), button) != members.end();
}

void BarButtonSetClass::AppendBoundaryDivider(
	vector<shared_ptr<BarButtonClass>>& activeButtons,
	size_t boundaryIndex)
{
	if (boundaryIndex >= 2 || !boundaryDividers[boundaryIndex]) return;

	// 交界分割线强制可见，尺寸用注册默认；不写配置。
	BarButtonRegistrationClass registration;
	if (!TryGetRegistration(Inkeys::BarButtonId::Divider, registration)) return;
	boundaryDividers[boundaryIndex]->baseUserVisible = true;
	boundaryDividers[boundaryIndex]->baseSize = registration.defaultSize;
	activeButtons.push_back(boundaryDividers[boundaryIndex]);
}

void BarButtonSetClass::Load()
{
	using namespace Inkeys::UI::Bar;
	auto committed = ReadBarLayout();
	const auto model = committed ? std::move(*committed) : FromConfig(Inkeys::config.GetBarLayout(), GetLayoutCatalog());
	unique_lock lock(presentationMutex);
	auto append = [&](Segment zone, vector<shared_ptr<BarButtonClass>>& output)
	{
		const auto& entries = model[zone];
		auto& previous = instances[static_cast<size_t>(zone)];
		InstanceGroups next;
		vector<shared_ptr<BarButtonClass>> ordered(entries.size());
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const auto& entry = entries[i];
			BarButtonRegistrationClass registration;
			if (!TryGetRegistration(entry.Id, registration) || !registration.button) continue;
			if (registration.allowMultiple) next[entry.Id].push_back({ i, entry.Visible, registration.button });
			else ordered[i] = registration.button;
		}
		for (auto& [id, group] : next)
		{
			auto old = previous.find(id);
			// 单份可随排序移动；多份同 ID 的身份有歧义时整组换新，避免旧手势提交到另一份。
			bool reuse = old != previous.end() && group.size() == old->second.size() &&
				(group.size() == 1 || std::equal(group.begin(), group.end(), old->second.begin(),
					[](const Instance& a, const Instance& b) { return a.index == b.index && a.visible == b.visible; }));
			for (size_t i = 0; i < group.size(); ++i)
			{
				auto& instance = group[i];
				if (reuse) instance.button = std::move(old->second[i].button);
				else
				{
					instance.button = make_shared<BarButtonClass>(*instance.button);
					instance.button->state = &instance.button->localState;
				}
				ordered[instance.index] = instance.button;
			}
		}
		// 未复用的地址仍可能出现在上一成功帧的命中表中。
		for (auto& [id, group] : previous)
			for (auto& instance : group)
				if (instance.button) retiredInstances.push_back(std::move(instance.button));
		previous = std::move(next);
		for (size_t i = 0; i < ordered.size(); ++i)
		{
			if (!ordered[i]) continue;
			ordered[i]->baseUserVisible = entries[i].Visible;
			ordered[i]->baseSize = ToRuntimeSize(entries[i].Size);
			if (entries[i].Visible) output.push_back(ordered[i]);
		}
	};

	vector<shared_ptr<BarButtonClass>> activeButtons;
	append(Segment::A1, activeButtons);
	if (mainRedoButton)
	{
		// 插在撤回前面，两个 2×1 才会叠进原来的 2×2 格子，且 Redo 在上。
		const auto recall = std::find_if(activeButtons.begin(), activeButtons.end(),
			[](const shared_ptr<BarButtonClass>& button)
			{
				return button && button->preset == BarButtonPresetEnum::Recall;
			});
		// 用户移除固定区撤回时，不额外插入依附于它的重做。
		if (recall != activeButtons.end()) activeButtons.insert(recall, mainRedoButton);
	}
	BarMoreButtonSnapshotClass more;
	append(Segment::More, more.explicitMore);
	{
		lock_guard snapshotLock(moreSnapshotMutex);
		moreSnapshot = std::move(more);
	}
	AppendBoundaryDivider(activeButtons, 0);
	append(Segment::B1, activeButtons);
	if (moreButton) activeButtons.push_back(moreButton);
	AppendBoundaryDivider(activeButtons, 1); // B | A2
	append(Segment::A2, activeButtons);
	if (mainCanvasDebugButton)
	{
		// 紧跟 A1 的清空按钮；固定区被用户精简掉清空时退到最前。
		auto insertAt = activeButtons.begin();
		const auto clean = std::find_if(activeButtons.begin(), activeButtons.end(),
			[](const shared_ptr<BarButtonClass>& button)
			{
				return button && button->preset == BarButtonPresetEnum::Clean;
			});
		if (clean != activeButtons.end()) insertAt = clean + 1;
		activeButtons.insert(insertAt, mainCanvasDebugButton);
	}
	if (mainPierceButton)
	{
		const auto freeze = std::find_if(activeButtons.begin(), activeButtons.end(),
			[](const shared_ptr<BarButtonClass>& button)
			{
				return button && button->preset == BarButtonPresetEnum::Freeze;
			});
		if (freeze != activeButtons.end()) activeButtons.insert(freeze, mainPierceButton);
	}

	// 先在列表锁内替换整段序列，再发布数量；注册表和边界实例继续持有对象所有权。
	const int activeButtonCount = static_cast<int>(activeButtons.size());
	buttonList.Replace(move(activeButtons));
	tot = activeButtonCount;

	ApplyMainPresentationLocked(GetStateModeSnapshot());
}

void BarButtonSetClass::ReleaseRetiredLayoutInstances()
{
	lock_guard lock(presentationMutex);
	retiredInstances.clear();
	// 本函数只在帧成功呈现、新命中表发布之后调用：此时恢复按钮命中准入。
	layoutHitSuspended.store(false, std::memory_order_release);
}

void BarButtonSetClass::ResetIconCaches()
{
	vector<shared_ptr<BarButtonClass>> buttons;
	{
		lock_guard lock(presentationMutex);
		shared_lock registrationLock(registrationMutex);
		for (const auto& [id, registration] : registrations) buttons.push_back(registration.button);
		for (const auto& segment : instances)
			for (const auto& [id, group] : segment)
				for (const auto& instance : group) buttons.push_back(instance.button);
		buttons.insert(buttons.end(), retiredInstances.begin(), retiredInstances.end());
		for (const auto& divider : boundaryDividers) buttons.push_back(divider);
		buttons.push_back(moreButton);
	}
	for (const auto& button : buttons)
	{
		if (!button) continue;
		button->icon.ResetCache();
		if (button->iconKind == ButtonIconKindEnum::Png) button->pngIcon.ResetCache();
	}
}

void BarButtonSetClass::PresetHoming(const StateModeClass& stateMode)
{
	// 白板内切换工具或折叠主栏时，也要收起已展开的绘制属性。
	const bool canvasDebugVisual = IsCanvasDebugVisualActive();
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen
		|| canvasDebugVisual
		|| barUISet.barState.fold
		|| !preset[(int)BarButtonPresetEnum::Draw]->IsVisible())
	{
		barUISet.barState.drawAttribute = false;
	}
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtShape
		|| canvasDebugVisual
		|| barUISet.barState.fold
		|| !preset[(int)BarButtonPresetEnum::Geometry]->IsVisible())
	{
		barUISet.barState.geometryAttribute = false;
	}
	if (barUISet.barState.fold) barUISet.barState.moreExpanded = false;
	// 折叠收起浮窗；Redo 仍可用时收成 2×1，而不是重新弹出。
	if (barUISet.barState.fold) barUISet.recallAttribute.Close(barUISet);
}

namespace
{
	// 主栏已落地小尺寸档位：撤回外观改为两键常驻、无弹窗。
	bool IsCompactSizeBar()
	{
		return NormalizeBarSizeMode(barUISet.barStyle.appliedSizeMode.load(std::memory_order_acquire))
			== BarSizeMode::Compact;
	}

	bool IsCompactSelectionBar()
	{
		return !Inkeys::UI::Bar::WhiteboardActive()
			&& GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection
			&& !Inkeys::UI::Bar::CurrentPageHasContent();
	}

	void ApplyRecallAvailabilityLocked(BarUISetClass& owner,
		Inkeys::UI::Bar::RecallHistoryAvailability availability)
	{
		using namespace Inkeys::UI::Bar;
		const auto before = owner.recallChrome;
		owner.recallChrome = ApplyRecallChromeEvent(before, RecallChromeEvent::Availability, availability);
		if (RecallChromeNeedsIdleRestart(before, owner.recallChrome))
		{
			owner.ArmRecallChromeIdleDeadline();
			owner.ArmRecallChromeIdleTimer();
		}
		else if (!owner.recallChrome.settlePending && owner.recallChromeIdleArmed)
		{
			owner.recallChromeIdleArmed = false;
			owner.DisarmRecallChromeIdleTimer();
		}
	}

	void RefreshRecallAvailabilityLocked(BarUISetClass& owner)
	{
		using namespace Inkeys::UI::Bar;
		RecallHistoryAvailability availability{};
		if (Inkeys::Drawing::Draw3::ProductRunning())
		{
			const auto snapshot = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
			availability.canUndo = snapshot.canUndo;
			availability.canRedo = snapshot.canRedo;
		}
		ApplyRecallAvailabilityLocked(owner, availability);
	}

	void ApplyRecallCommandResultLocked(BarUISetClass& owner, bool redo,
		Inkeys::UI::Bar::RecallHistoryAvailability clickAvailability, bool accepted)
	{
		using namespace Inkeys::UI::Bar;
		const auto before = owner.recallChrome;
		owner.recallChrome = ApplyRecallChromeCommandResult(before, redo, clickAvailability, accepted);
		if (RecallChromeNeedsIdleRestart(before, owner.recallChrome))
		{
			owner.ArmRecallChromeIdleDeadline();
			owner.ArmRecallChromeIdleTimer();
		}
	}

	bool EngageRecallCommand(BarUISetClass& owner, bool redo, bool popupCommand = false)
	{
		using namespace Inkeys::UI::Bar;
		RecallHistoryAvailability clickAvailability;
		bool commandEnabled = false;
		{
			std::scoped_lock lock(owner.recallChromeMutex);
			RefreshRecallAvailabilityLocked(owner);
			clickAvailability = owner.recallChrome.availability;
			const auto view = ResolveRecallChrome(owner.recallChrome, IsCompactSelectionBar(), IsCompactSizeBar());
			if (popupCommand && (!view.popupOpen || view.popupShowsRedo != redo)) return false;
			commandEnabled = RecallChromeCommandEnabled(clickAvailability, redo);
			if (!commandEnabled) ApplyRecallCommandResultLocked(owner, redo, clickAvailability, false);
		}
		if (!commandEnabled) { owner.UpdateRendering(false); return false; }
		const auto result = Inkeys::Drawing::Draw3::PublishProductCommand(redo
			? Inkeys::Drawing::Draw3::Bridge::CommandType::Redo
			: Inkeys::Drawing::Draw3::Bridge::CommandType::Undo);
		if (result != Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted)
		{
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				ApplyRecallCommandResultLocked(owner, redo, clickAvailability, false);
			}
			owner.UpdateRendering(false);
			return false;
		}
		// 弹窗点击保留现有身份；主栏只合并面板互斥，不合并 Undo/Redo 命令。
		if (!popupCommand && owner.TryBeginToggle(BarToggleChannel::RecallAttribute))
			owner.CollapseAuxiliaryPanels();
		{
			std::scoped_lock lock(owner.recallChromeMutex);
			ApplyRecallCommandResultLocked(owner, redo, clickAvailability, true);
		}
		owner.UpdateRendering(false);
		return true;
	}
}

Inkeys::UI::Bar::RecallChromeView BarUISetClass::RecallChromeView()
{
	std::scoped_lock lock(recallChromeMutex);
	return recallChromePresentation;
}
Inkeys::UI::Bar::RecallChromeView BarUISetClass::RecallChromeView(bool compactSelection)
{
	std::scoped_lock lock(recallChromeMutex);
	RefreshRecallAvailabilityLocked(*this);
	recallChromePresentation = Inkeys::UI::Bar::ResolveRecallChrome(recallChrome, compactSelection, IsCompactSizeBar());
	return recallChromePresentation;
}

void BarUISetClass::NoteRecallHistoryAvailability(bool canUndo, bool canRedo)
{
	bool changed = false;
	{
		std::scoped_lock lock(recallChromeMutex);
		if (recallChrome.availability.canUndo == canUndo
			&& recallChrome.availability.canRedo == canRedo)
			return;
		ApplyRecallAvailabilityLocked(*this, { canUndo, canRedo });
		changed = true;
	}
	if (changed) UpdateRendering(false);
}

bool BarUISetClass::PollRecallChromeIdleTimer(unsigned& retryMs)
{
	retryMs = 0;
	std::scoped_lock lock(recallChromeMutex);
	if (!recallChrome.settlePending || !recallChromeIdleArmed)
		return false;
	const auto now = std::chrono::steady_clock::now();
	if (now >= recallChromeIdleDeadline) return true;
	auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
		recallChromeIdleDeadline - now).count();
	if (remaining < 1) remaining = 1;
	retryMs = static_cast<unsigned>(remaining);
	return false;
}

void BarUISetClass::ArmRecallChromeIdleDeadline()
{
	using namespace std::chrono;
	recallChromeIdleDeadline = steady_clock::now()
		+ duration_cast<steady_clock::duration>(duration<double>(
			Inkeys::UI::Bar::RecallChromeIdleSeconds));
	recallChromeIdleArmed = true;
}

void BarUISetClass::OpenRecallFromUndo()
{
	if (!EngageRecallCommand(*this, false)) Inkeys::UI::Bar::Haptics::RejectButtonAction();
}

void BarUISetClass::OpenRecallFromRedo()
{
	if (!EngageRecallCommand(*this, true)) Inkeys::UI::Bar::Haptics::RejectButtonAction();
}

void BarUISetClass::NoteRecallPopupRedo()
{
	OpenRecallFromRedo();
}

bool BarUISetClass::ActivateRecallPopupCommand()
{
	bool showsRedo = true;
	{
		std::scoped_lock lock(recallChromeMutex);
		RefreshRecallAvailabilityLocked(*this);
		const auto view = Inkeys::UI::Bar::ResolveRecallChrome(recallChrome, IsCompactSelectionBar(), IsCompactSizeBar());
		if (!view.popupOpen) return false;
		showsRedo = view.popupShowsRedo;
	}
	return EngageRecallCommand(*this, showsRedo, true);
}

void BarUISetClass::NoteRecallPopupDismissed()
{
	{
		std::scoped_lock lock(recallChromeMutex);
		RefreshRecallAvailabilityLocked(*this);
		recallChrome = Inkeys::UI::Bar::ApplyRecallChromeEvent(
			recallChrome, Inkeys::UI::Bar::RecallChromeEvent::Dismiss);
	}
	UpdateRendering(false);
}

void BarUISetClass::ClearRecallForDrawing()
{
	{
		std::scoped_lock lock(recallChromeMutex);
		RefreshRecallAvailabilityLocked(*this);
		// 落笔只关掉浮窗。3 秒按住还在时，大按钮保持到计时结束。
		recallChrome = Inkeys::UI::Bar::ApplyRecallChromeEvent(
			recallChrome, Inkeys::UI::Bar::RecallChromeEvent::SuppressPopup);
	}
	recallAttribute.ResetPointerFeedback();
	UpdateRendering(false);
}

void BarUISetClass::SettleRecallChromeForWorkspace()
{
	{
		std::scoped_lock lock(recallChromeMutex);
		RefreshRecallAvailabilityLocked(*this);
		recallChrome = Inkeys::UI::Bar::ApplyRecallChromeEvent(
			recallChrome, Inkeys::UI::Bar::RecallChromeEvent::ReturnToDesktop);
	}
	recallAttribute.ResetPointerFeedback();
	UpdateRendering(false);
}

bool BarUISetClass::AdvanceRecallChromeIdle(double)
{
	{
		std::scoped_lock lock(recallChromeMutex);
		// 只结算已发布的业务状态；本帧统一投影再读取一次最新历史事实。
		if (!recallChrome.settlePending || !recallChromeIdleArmed)
			return false;
		if (std::chrono::steady_clock::now() < recallChromeIdleDeadline)
			return false;
		recallChrome = Inkeys::UI::Bar::ApplyRecallChromeEvent(
			recallChrome, Inkeys::UI::Bar::RecallChromeEvent::IdleElapsed);
		recallChromeIdleArmed = false;
		DisarmRecallChromeIdleTimer();
	}
	recallAttribute.ResetPointerFeedback();
	return true;
}

namespace Inkeys::UI::Bar
{
	bool StartButtonHoverVisual(BarButtonClass& button) noexcept
	{
		return Inkeys::UI::UI3::StartButtonHoverVisual(button,
			button.preset == BarButtonPresetEnum::Divider);
	}

	bool StopButtonHoverVisual(BarButtonClass& button, bool immediate,
		bool preserveVisual) noexcept
	{
		return Inkeys::UI::UI3::StopButtonHoverVisual(button, immediate, preserveVisual,
			button.preset == BarButtonPresetEnum::Divider);
	}

	void RetargetFixedButtonVisual(const ButtonVisualParts& parts,
		bool visible, bool enabled, bool selected, bool pressed, double duration,
		std::optional<COLORREF> content, bool iconVisible,
		std::optional<COLORREF> fill, std::optional<COLORREF> frame) noexcept
	{
		// 离屏表面可绑定其他主题源，透明度策略仍沿用主栏的深浅值。
		Inkeys::UI::UI3::RetargetFixedButtonVisual(barUISet.barStyle.darkStyle, parts,
			visible, enabled, selected, pressed, duration, content, iconVisible, fill, frame);
	}

	void RetargetButtonInteractionVisual(BarButtonClass& button,
		bool visible, bool enabled, bool selected, double duration,
		std::optional<COLORREF> content, bool iconVisible,
		std::optional<COLORREF> fill, std::optional<COLORREF> frame) noexcept
	{
		Inkeys::UI::UI3::RetargetButtonInteractionVisual(barUISet.barStyle.darkStyle, button,
			visible, enabled, selected, duration, content, iconVisible, fill, frame);
	}

	void ConfigureButtonVisualTargets(BarButtonClass& button,
		ButtonVisualLayoutKind layoutKind, double duration, double scale, bool direct,
		std::optional<double> iconHeightOverride, std::optional<double> fontTargetScale)
	{
		ConfigureButtonVisualTargets(barUISet.spec, button, layoutKind, duration, scale,
			direct, iconHeightOverride, fontTargetScale);
	}

	void ConfigureButtonVisualTargets(UIRendering& renderer, BarButtonClass& button,
		ButtonVisualLayoutKind layoutKind, double duration, double scale, bool direct,
		std::optional<double> iconHeightOverride, std::optional<double> fontTargetScale)
	{
		const auto metrics = ResolveButtonVisualMetrics(layoutKind);
		const double labelSize = button.LabelFontSize(metrics.primaryFontSizeDip);
		// 只为图形标签按实际宿主测量；其他按钮沿用网格字号。
		auto* measurement = button.preset == BarButtonPresetEnum::Geometry
			&& metrics.primarySlotWidthDip > 4 && labelSize > 0 ? &renderer : nullptr;
		Inkeys::UI::UI3::ConfigureButtonVisualTargets(button, layoutKind, duration, scale,
			direct, iconHeightOverride, fontTargetScale, labelSize, measurement);
	}

	ButtonAnimationResult AdvanceButtonVisual(BarButtonClass& button,
		const ButtonAnimationContext& frame, UiWordClass* secondary)
	{
		return Inkeys::UI::UI3::AdvanceButtonVisual(button, frame, secondary,
			button.preset == BarButtonPresetEnum::Divider);
	}

	ButtonHitSnapshot ResolveButtonHitSnapshot(BarButtonClass& button,
		const UiInheritClass& inherit) noexcept
	{
		return Inkeys::UI::UI3::ResolveButtonHitSnapshot(button, inherit,
			button.IsVisible() && button.button.enable.val && button.preset != BarButtonPresetEnum::Divider,
			button.state->state != WidgetState::Disable);
	}

	bool DrawButtonVisual(UIRendering& renderer,
		ID2D1DeviceContext* deviceContext, BarButtonClass& button,
		const UiInheritClass& inherit, const ButtonDrawOptions& options)
	{
		return Inkeys::UI::UI3::DrawButtonVisual(renderer, deviceContext, button, inherit,
			options, button.preset == BarButtonPresetEnum::Divider);
	}
}

namespace Inkeys::UI::Bar
{
	BarButtonListLayout ResolveBarButtonListLayout(
		const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		BarButtonListLayoutKind kind,
		const std::vector<std::shared_ptr<BarButtonClass>>& secondGroup,
		BarSizeMode sizeMode)
	{
		const auto& sizeMetrics = ResolveBarSizeMetrics(sizeMode);
		const bool compact = sizeMode == BarSizeMode::Compact;
		const ListLayoutMetrics listMetrics{ sizeMetrics.gap, sizeMetrics.one, sizeMetrics.two };
		ListLayoutSpec spec;
		spec.height = sizeMetrics.barHeight;
		std::vector<ListLayoutItem> items;
		std::vector<std::shared_ptr<BarButtonClass>> identities;
		auto Append = [&](const auto& group)
		{
			for (const auto& button : group)
			{
				if (!button) continue;
				const auto size = button->size.load();
				ListLayoutItem item;
				switch (size)
				{
				case ButtonSizeEnum::oneOne: item.columnSpan = 1; item.rowSpan = 1; break;
				case ButtonSizeEnum::twoOne: item.columnSpan = 2; item.rowSpan = 1; break;
				case ButtonSizeEnum::oneTwo: item.columnSpan = 1; item.rowSpan = 2; break;
				default: item.columnSpan = item.rowSpan = 2; break;
				}
				item.width = item.columnSpan == 2 ? sizeMetrics.two : sizeMetrics.one;
				item.height = item.rowSpan == 2 ? sizeMetrics.two : sizeMetrics.one;
				item.visible = button->IsVisible(); item.separator = button->preset == BarButtonPresetEnum::Divider;
				if (kind == BarButtonListLayoutKind::Main && button->id == Inkeys::BarButtonId::UserDivider)
				{
					item.width = 1.0;
					item.height = compact ? sizeMetrics.dividerHeight : 50.0;
					item.centerHeight = sizeMetrics.one;
					item.exclusiveColumn = !compact;
					item.columnSpan = 1;
					item.rowSpan = compact ? 1 : 2;
				}
				else if (kind == BarButtonListLayoutKind::GeometryGrid)
				{
					// 形状类别：大尺寸固定 2×2，小尺寸统一 1×1。
					item.columnSpan = item.rowSpan = compact ? 1 : 2;
					item.width = item.height = compact ? sizeMetrics.one : ButtonTwoSideDip;
				}
				else if (kind == BarButtonListLayoutKind::GroupedGrid)
				{
					if (size == ButtonSizeEnum::oneTwo) item.width = 10.0;
				}
				else if (kind == BarButtonListLayoutKind::SingleRow
					|| (compact && kind == BarButtonListLayoutKind::Main))
				{
					if (item.separator && compact && kind == BarButtonListLayoutKind::Main)
					{
						// 小尺寸主栏分割线：与大尺寸同语义，落在两按钮之间的间隙正中，不额外占宽；按 1×1 行高居中。
						item.width = ButtonFrameThicknessDip; item.height = sizeMetrics.dividerHeight;
						item.columnSpan = item.rowSpan = 1; item.centerHeight = sizeMetrics.one;
						item.columnBoundary = true;
					}
					else if (item.separator)
					{
						item.width = ButtonFrameThicknessDip; item.height = ButtonOneSideDip - 4;
						item.columnSpan = item.rowSpan = 1; item.centerHeight = ButtonOneSideDip;
					}
				}
				else if (size != ButtonSizeEnum::oneOne && size != ButtonSizeEnum::twoOne
					&& size != ButtonSizeEnum::twoTwo)
				{
					// 主栏边界线不占网格；尺寸与位置偏移仍由产品侧提供。
					item.columnBoundary = true; item.boundaryInsetX = ButtonGapDip / 2.0;
					item.width = 1.0; item.height = 50.0;
				}
				items.push_back(item); identities.push_back(button);
			}
		};
		Append(buttons);
		if (compact && kind == BarButtonListLayoutKind::Main)
		{
			for (std::size_t index = 0; index + 1 < identities.size(); ++index)
				if (identities[index] && identities[index + 1]
					&& identities[index]->preset == BarButtonPresetEnum::Redo
					&& identities[index + 1]->preset == BarButtonPresetEnum::Recall)
				{
					std::swap(items[index], items[index + 1]);
					std::swap(identities[index], identities[index + 1]);
					++index;
				}
		}
		const auto firstEnd = items.size();
		if (kind == BarButtonListLayoutKind::GeometryGrid)
		{
			spec.kind = ListLayoutKind::FixedGrid;
			// 列数固定为一整行（大尺寸 6 个 2×2、小尺寸 12 个 1×1，宽度均为 445），
			// 图形不足一行时靠左排列，面板与顶栏宽度不随本类图形数量收窄。
			spec.columns = compact ? 12 : 6;
			if (compact) spec.cellColumnSpan = spec.cellRowSpan = 1;
		}
		else if (kind == BarButtonListLayoutKind::GroupedGrid)
		{
			// 列数使用原始快照长度（含空引用），分组边界也保留原快照的非空判定。
			const auto count = (std::max)(size_t{1}, buttons.size() + secondGroup.size());
			spec.kind = ListLayoutKind::GroupedGrid;
			// 小尺寸全部为 1×1：一行最多 10 个，列数随数量增长，保持接近方形。
			spec.columns = compact
				? (std::min)(10, (std::max)(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)) * 1.5))))
				: 2 * (std::min)(5, (std::max)(1,
					static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))))));
			spec.minimumRows = 1;
			Append(secondGroup);
			const bool showDivider = !buttons.empty() && !secondGroup.empty();
			if (compact)
			{
				// 小尺寸列数不超过单行实际项数（两组分行时取较多的一组），避免两个按钮占三列宽。
				const std::size_t rowItems = showDivider
					? (std::max)(firstEnd, items.size() - firstEnd) : items.size();
				spec.columns = (std::min)(spec.columns, (std::max)(1, static_cast<int>(rowItems)));
			}
			spec.groups.push_back({0, firstEnd, spec.columns, false, compact ? 1 : 2, false});
			spec.groups.push_back({firstEnd, items.size(), spec.columns, showDivider, 1, showDivider});
		}
		else if (kind != BarButtonListLayoutKind::SingleRow && !compact)
		{
			spec.kind = ListLayoutKind::GroupedGrid; spec.packing = ListGridPacking::PairedRows;
		}
		// 小尺寸主栏走单行流：起步与间距 5，行中心 = 5 + 32.5/2 = 21.25，恰为 42.5 高的中线。
		// 小尺寸形状网格的定列格边取 1×1 边长。
		const auto packed = ResolveListLayout(items, spec, compact && kind == BarButtonListLayoutKind::GeometryGrid
			? ListLayoutMetrics{ sizeMetrics.gap, sizeMetrics.one, sizeMetrics.one } : listMetrics);
		BarButtonListLayout layout;
		layout.width = packed.width; layout.height = packed.height; layout.subColumns = packed.columns;
		layout.totalRows = packed.rows; layout.showDivider = packed.showDivider;
		layout.explicitRows = packed.groups.empty() ? packed.rows : packed.groups.front().endRow;
		for (const auto& p : packed.placements)
		{
			BarButtonListPlacement placement;
			placement.button = identities[p.itemIndex];
			placement.x = p.x; placement.y = p.y; placement.width = p.width; placement.height = p.height;
			placement.column = p.column; placement.row = p.row;
			placement.columnSpan = p.columnSpan; placement.rowSpan = p.rowSpan;
			placement.forced = p.groupIndex == 1;
			layout.placements.push_back(std::move(placement));
		}
		return layout;
	}
}

bool BarUISetClass::HitTestPresentedButton(const BarButtonClass& button,
	double x, double y) const
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	const auto hit = presentedButtonHits_.find(&button);
	return hit != presentedButtonHits_.end() && Inkeys::UI::UI3::HitTestButton(hit->second, x, y);
}

const Panel* BarUISetClass::PanelForShape(const UiShapeClass& shape) const
{
	const std::pair<BarUISetShapeEnum, const Panel*> panels[] = {
		{BarUISetShapeEnum::MainBar, &mainPanel}, {BarUISetShapeEnum::MorePanel, &morePanel},
		{BarUISetShapeEnum::DrawAttributeBar, &drawAttributePanel},
		{BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup, &annotationTooltipPanel},
		{BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup, &overflowTooltipPanel}
	};
	for (const auto& [id, panel] : panels)
		if (const auto it = shapeMap.find(id); it != shapeMap.end() && it->second.get() == &shape) return panel;
	return colorPicker.Shape(ColorPickerPart::Panel).get() == &shape ? &colorPickerPanel : nullptr;
}

bool BarUISetClass::HitTestPresentedShape(const UiShapeClass& shape,double x,double y) const
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	if (const auto panel = PanelForShape(shape))
		return std::isfinite(x) && std::isfinite(y) && panel->Presented().OwnsPoint(x, y);
	const auto hit=presentedVisualHits_.find(&shape);
	return hit!=presentedVisualHits_.end() && Inkeys::UI::UI3::HitTestButton(hit->second,x,y);
}

std::optional<PanelId> BarUISetClass::FrontPresentedPanel(double x, double y, std::uint32_t mask) const
{
	if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
	for (;;)
	{
		const auto before = BottomDockPresentedSnapshot();
		const double zoom = (std::max)(0.000001, before.zoom);
		const auto front = panelStack.FrontPanelIf([&](const PanelStackEntry& entry) {
			if (!(mask & (1u << entry.id))) return false;
			const auto id = static_cast<BarPanelId>(entry.id);
			PanelFrame frame;
			double px = x, py = y;
			switch (id)
			{
			case BarPanelId::GeometryAttribute:
				{ std::lock_guard lock(geometryAttribute.presentationMutex_); frame = geometryAttribute.panel_.Presented(); } break;
			case BarPanelId::EraserAttribute: case BarPanelId::EraserMenu:
				{ std::lock_guard lock(eraserAttribute.presentationMutex_); frame = entry.panel->Presented(); } break;
			case BarPanelId::RecallAttribute:
				{ std::lock_guard lock(recallAttribute.presentationMutex_); frame = recallAttribute.panel_.Presented(); } break;
			case BarPanelId::ConstructionPrompt:
				{ std::lock_guard lock(constructionPrompt.mutex_); frame = constructionPrompt.panel_.Presented(); px /= constructionPrompt.presented_.zoom; py /= constructionPrompt.presented_.zoom; } break;
			case BarPanelId::BottomDockIndicator:
				// 原生直移会重定位旧像素；范围继续取同一 seqlock 快照，不能读未重定位的副本。
				frame.visible = before.indicatorVisible && before.indicatorOccluding;
				frame.interaction.bounds = {static_cast<double>(before.indicatorBounds.left), static_cast<double>(before.indicatorBounds.top),
					static_cast<double>(before.indicatorBounds.right), static_cast<double>(before.indicatorBounds.bottom)};
				px = std::lround(x + before.horizontalMapping.rigidOverlayTranslationXDip * before.zoom);
				py = std::lround(y + before.rigidTranslationDip * before.zoom);
				break;
			default:
				{ std::lock_guard lock(presentedButtonHitsMutex_); frame = entry.panel->Presented(); }
				if (id == BarPanelId::Main)
				{
					const auto point = BottomDockBodyHitTestPointFromRigid({static_cast<LONG>(x), static_cast<LONG>(y)});
					px = point.x; py = point.y;
				}
				px /= zoom; py /= zoom;
				break;
			}
			return frame.OwnsPoint(px, py);
		});
		if (BottomDockPresentedSnapshot().serial == before.serial) return front;
	}
}
bool BarUISetClass::HitTestPresentedShape(const UiSuperellipseClass& shape,double x,double y) const
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	const auto hit=presentedVisualHits_.find(&shape);
	return hit!=presentedVisualHits_.end() && Inkeys::UI::UI3::HitTestButton(hit->second,x,y);
}

void BarUISetClass::PreparePresentedButtonHits(const StateModeClass& /*stateMode*/, double frameZoom)
{
	if (frameZoom <= 0) frameZoom = static_cast<double>(barStyle.zoom);
	std::unordered_map<const BarButtonClass*, Inkeys::UI::UI3::ButtonHitSnapshot> next;
	auto Add = [&](const std::shared_ptr<BarButtonClass>& button, bool more)
	{
		if (!button) return;
		auto hit = Inkeys::UI::Bar::ResolveButtonHitSnapshot(*button,
			UiInheritClass(button->button.inhX, button->button.inhY));
		hit.visible = hit.visible && !barState.fold && (!more || barState.moreExpanded);
		next.emplace(button.get(), hit);
	};
	for (const auto& button : barButtonSet.buttonList.Snapshot()) Add(button, false);
	const auto more = barButtonSet.GetMoreButtonSnapshot();
	for (const auto& button : more.explicitMore) Add(button, true);
	for (const auto& button : more.forcedOverflow) Add(button, true);
	// 透明 hit area 的 pct=0 是有意设计，沿用原 IsClick 几何语义；可用性仍由交互调用方门控。
	std::unordered_map<const void*,Inkeys::UI::UI3::ButtonHitSnapshot> visualHits;
	for (Panel* panel : {&mainPanel, &morePanel, &colorPickerPanel, &annotationTooltipPanel,
		&overflowTooltipPanel, &drawAttributePanel}) panel->SetCandidate(PanelFrame{});
	auto SetPanel = [&](Panel& panel, const UiShapeClass& shape, const ButtonHitSnapshot& hit)
	{
		const auto& r = hit.boundsDip;
		// 与原命中表共用 FLOAT 舍入与 DIP 容差；透明 hit area 不以 pct 判断可用性。
		PanelFrame frame;
		frame.interaction = {{r.left, r.top, r.right, r.bottom}, hit.radiusX, hit.radiusY,
			PanelHitGeometry::RoundedWithPixelTolerance, 1, true};
		frame.visible = hit.visible && hit.enabled;
		const RECT bounds = RenderingAttribute::GetWeigetRect(shape, frameZoom);
		frame.visualBounds = {static_cast<double>(bounds.left), static_cast<double>(bounds.top),
			static_cast<double>(bounds.right), static_cast<double>(bounds.bottom)};
		panel.SetCandidate(frame);
	};
	for(const auto& [id,shape]:shapeMap)if(shape)
	{
		const auto hit = Inkeys::UI::UI3::ResolveShapeHitSnapshot(*shape,
			UiInheritClass(shape->inhX,shape->inhY),
			// 新面板独立发布命中，原透明控件不再接受输入。
			id < BarUISetShapeEnum::GeometryAttributeBar || id > BarUISetShapeEnum::GeometryAttributeBar_Close);
		if (const auto panel = PanelForShape(*shape)) SetPanel(*const_cast<Panel*>(panel), *shape, hit);
		else visualHits.emplace(shape.get(), hit);
	}
	for (const auto& shape : colorPicker.Shapes())
	{
		const auto hit = ResolveShapeHitSnapshot(*shape, {shape->inhX, shape->inhY});
		if (shape == colorPicker.Shape(ColorPickerPart::Panel)) SetPanel(colorPickerPanel, *shape, hit);
		else visualHits.emplace(shape.get(), hit);
	}
	for(const auto& [id,shape]:superellipseMap)if(shape)
	{
		Inkeys::UI::UI3::ButtonHitSnapshot hit;
		hit.boundsDip=D2D1::RectF(static_cast<FLOAT>(shape->inhX),static_cast<FLOAT>(shape->inhY),
			static_cast<FLOAT>(shape->inhX+shape->w.val),static_cast<FLOAT>(shape->inhY+shape->h.val));
		hit.visible=shape->w.val>0 && shape->h.val>0;hit.enabled=true;
		hit.superellipseExponent=shape->n?static_cast<double>(shape->n->val):0.0;
		visualHits.emplace(shape.get(),hit);
	}
	// 分配和几何整理在 seqlock 奇数区之外完成，提交段只交换已经构造好的表。
	candidateButtonHits_=std::move(next);candidateVisualHits_=std::move(visualHits);
}

void BarUISetClass::PublishPresentedButtonHits()
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	presentedButtonHits_.swap(candidateButtonHits_);presentedVisualHits_.swap(candidateVisualHits_);
	for (Panel* panel : {&mainPanel, &morePanel, &colorPickerPanel, &annotationTooltipPanel,
		&overflowTooltipPanel, &bottomDockIndicatorPanel, &drawAttributePanel}) panel->CommitPresented();
	colorPicker.CommitPresented(colorPicker.Snapshot());
}
