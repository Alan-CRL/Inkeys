module;

#include "../../../IdtMain.h"

#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
// 历史遗留问题
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"

#include "../../../IdtConfiguration.h"
#include "../../Window/Window.Legacy.hpp"
#include "Bar.A2.h"
#include "Bar.PresentationProbe.h"
#include "Bar.Presentation.Source.h"
#include "Bar.InteractionWorker.h"
#include "Bar.Haptics.h"
#include "Bar.BottomDock.h"
#include "Bar.WindowGeometry.h"
#include "../../Input/UiTouchRouting.h"
#include "Bar.ThemeMaterial.h"

#include <d2d1_1.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

module Inkeys.UI.Bar;
import :Button;

import :Main;
import :Layout;
import :Theme;
import Inkeys.UI.Bar.Metrics;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Other.Config;
import Inkeys.Business.ComponentActions;
import Inkeys.UI.Freeze;
import Inkeys.UI.Setting;
import Inkeys.Window;

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
	if (iconKind == BarButtonIconKindEnum::Svg && !iconResourceName.empty())
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
	const std::string& legacyField,
	function<bool()> legacyEnabled,
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
			legacyField,
			move(legacyEnabled),
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
		BarButtonSizeEnum::twoTwo,
		true,
		{},
		{},
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
	const COLORREF defaultButtonFill = GetThemeColor(BarThemeColorEnum::Surface);
	const COLORREF defaultIconColor = GetThemeColor(BarThemeColorEnum::IconPrimary);

	// 分隔线
	{
		BarButtonClass* obj = new BarButtonClass;
		const COLORREF dividerColor = GetThemeColor(BarThemeColorEnum::Divider);
		{
			obj->size = BarButtonSizeEnum::oneTwo;
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
			obj->button.framePct = BarUiPctClass(0.0);
			obj->button.frameLightPct = BarUiPctClass(0.0);
			obj->button.frameRendering = BarUiFrameRenderingEnum::PointLight;
			obj->button.frameLightColor = BarUiFrameLightColorEnum::Frame;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
					if (!Inkeys::UI::Bar::PermitAuthorizedFixtureAction(Inkeys::UI::Bar::Ui3FiniteScene::DrawAttribute,
						Inkeys::UI::Bar::Ui3FixtureActionPoint::Callback)) { Inkeys::UI::Bar::Haptics::RejectButtonAction(); return; }
					// 可视化期间底层借用的工具身份不算已选中：点击必须真正切换工具才能退出可视化。
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen || IsCanvasDebugVisualActive())
					{
						Inkeys::UI::Bar::RejectCurrentUi3FiniteBusiness(Inkeys::UI::Bar::Ui3FiniteStatus::UnsupportedState);
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
							Inkeys::UI::Bar::RejectCurrentUi3FiniteBusiness(Inkeys::UI::Bar::Ui3FiniteStatus::RejectedByBusiness);
							Inkeys::UI::Bar::Haptics::RejectButtonAction();
							return;
						}
						// 只在真实toggle接受点标记，之后仍走原决策与StateUpdate规范化。
						Inkeys::UI::Bar::MarkCurrentUi3FiniteBusinessAccepted();
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
		obj->size = BarButtonSizeEnum::twoOne;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoOne;
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
		obj->size = BarButtonSizeEnum::twoOne;
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
		obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
			obj->size = BarButtonSizeEnum::twoTwo;
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
		boundaryDividers[0] = make_shared<BarButtonClass>(*dividerRegistration.button);
		boundaryDividers[1] = make_shared<BarButtonClass>(*dividerRegistration.button);
	}
	// 更多入口是硬编码控件，不进入注册表或持久化序列。
	moreButton = make_shared<BarButtonClass>();
	moreButton->id = Inkeys::BarButtonId::MoreBoundary;
	moreButton->preset = BarButtonPresetEnum::More;
	moreButton->size = BarButtonSizeEnum::twoTwo;
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
	const COLORREF defaultButtonFill = GetThemeColor(BarThemeColorEnum::Surface);
	const COLORREF defaultIconColor = GetThemeColor(BarThemeColorEnum::IconPrimary);

	auto registerComponent = [&](const char* id,
		const char* legacyField,
		function<bool()> legacyEnabled,
		const wchar_t* categoryName,
		const wchar_t* settingsName,
		const wchar_t* shortText,
		const wchar_t* iconResource,
		BuiltInComponentAction action)
	{
		BarButtonRegistrationClass existingRegistration;
		if (TryGetRegistration(id, existingRegistration)) return;

		BarButtonClass* obj = new BarButtonClass;
		obj->size = BarButtonSizeEnum::twoTwo;
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
				if (!Inkeys::UI::Bar::QueueInteractionComponentAction([action] { ExecuteBuiltInComponentAction(action); }))
					Inkeys::UI::Bar::Haptics::RejectButtonAction();
				break;
			}
		};

		if (!RegisterButton(
			id, obj, false, BarButtonLayoutZoneEnum::Extension, true,
			legacyField, move(legacyEnabled), categoryName, settingsName))
		{
			delete obj;
		}
	};

	// 顺序与设置页的分组顺序一致，运行时 B 也始终按此顺序投影。
	registerComponent(
		"Component.ShortcutButton.Appliance.Explorer",
		"component.shortcutButton.appliance.explorer",
		[] { return setlist.component.shortcutButton.appliance.explorer; },
		IW(componentText.SoftwareSection).c_str(), IW(componentText.ExplorerTitle).c_str(), IW(componentLabels.ExplorerLabel).c_str(), L"compExplorer",
		BuiltInComponentAction::Explorer);
	registerComponent(
		"Component.ShortcutButton.Appliance.Taskmgr",
		"component.shortcutButton.appliance.taskmgr",
		[] { return setlist.component.shortcutButton.appliance.taskmgr; },
		IW(componentText.SoftwareSection).c_str(), IW(componentText.TaskManagerTitle).c_str(), IW(componentLabels.TaskManagerLabel).c_str(), L"compTaskManager",
		BuiltInComponentAction::TaskManager);
	registerComponent(
		"Component.ShortcutButton.Appliance.Control",
		"component.shortcutButton.appliance.control",
		[] { return setlist.component.shortcutButton.appliance.control; },
		IW(componentText.SoftwareSection).c_str(), IW(componentText.ControlPanelTitle).c_str(), IW(componentLabels.ControlPanelLabel).c_str(), L"compControlPanel",
		BuiltInComponentAction::ControlPanel);

	registerComponent(
		"Component.ShortcutButton.System.Desktop",
		"component.shortcutButton.system.desktop",
		[] { return setlist.component.shortcutButton.system.desktop; },
		IW(componentText.SystemSection).c_str(), IW(componentText.ShowDesktopTitle).c_str(), IW(componentLabels.ShowDesktopLabel).c_str(), L"compShowDesktop",
		BuiltInComponentAction::ShowDesktop);
	registerComponent(
		"Component.ShortcutButton.System.LockWorkStation",
		"component.shortcutButton.system.lockWorkStation",
		[] { return setlist.component.shortcutButton.system.lockWorkStation; },
		IW(componentText.SystemSection).c_str(), IW(componentText.LockScreenTitle).c_str(), IW(componentLabels.LockScreenLabel).c_str(), L"compLockScreen",
		BuiltInComponentAction::LockWorkStation);

	registerComponent(
		"Component.ShortcutButton.Keyboard.Keyboardesc",
		"component.shortcutButton.keyboard.keyboardesc",
		[] { return setlist.component.shortcutButton.keyboard.keyboardesc; },
		IW(componentText.KeyboardSection).c_str(), IW(componentText.EscapeTitle).c_str(), IW(componentLabels.EscapeLabel).c_str(), L"compEscape",
		BuiltInComponentAction::Escape);
	registerComponent(
		"Component.ShortcutButton.Keyboard.KeyboardAltF4",
		"component.shortcutButton.keyboard.keyboardAltF4",
		[] { return setlist.component.shortcutButton.keyboard.keyboardAltF4; },
		IW(componentText.KeyboardSection).c_str(), IW(componentText.AltF4Title).c_str(), L"Alt+F4", L"compAltF4",
		BuiltInComponentAction::AltF4);

	registerComponent(
		"Component.ShortcutButton.RollCall.IslandCaller1",
		"component.shortcutButton.rollCall.IslandCaller1",
		[] { return setlist.component.shortcutButton.rollCall.IslandCaller1; },
		IW(componentText.RollCallSection).c_str(), IW(componentText.IslandCaller1Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallRandom",
		BuiltInComponentAction::IslandCaller);
	registerComponent(
		"Component.ShortcutButton.RollCall.IslandCaller2",
		"component.shortcutButton.rollCall.IslandCaller2",
		[] { return setlist.component.shortcutButton.rollCall.IslandCaller2; },
		IW(componentText.RollCallSection).c_str(), IW(componentText.IslandCaller2Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallSingle",
		BuiltInComponentAction::IslandCallerSimple);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom1",
		"component.shortcutButton.rollCall.SecRandom1",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom1; },
		IW(componentText.RollCallSection).c_str(), IW(componentText.SecRandom1Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallQuick",
		BuiltInComponentAction::SecRandomDirect);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom2",
		"component.shortcutButton.rollCall.SecRandom2",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom2; },
		IW(componentText.RollCallSection).c_str(), IW(componentText.SecRandom2Title).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallQuick2",
		BuiltInComponentAction::SecRandomQuickDraw);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom2Compat",
		"component.shortcutButton.rollCall.SecRandom2Compat",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom2Compat; },
		IW(componentText.RollCallSection).c_str(), IW(componentText.SecRandom2CompatTitle).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compRollCallQuickCompat",
		BuiltInComponentAction::SecRandomQuickDrawCompat);
	registerComponent(
		"Component.ShortcutButton.RollCall.NamePicker",
		"component.shortcutButton.rollCall.NamePicker",
		[] { return setlist.component.shortcutButton.rollCall.NamePicker; },
		IW(componentText.RollCallSection).c_str(), IW(componentText.NamePickerTitle).c_str(), IW(componentLabels.RollCallLabel).c_str(), L"compNamePicker",
		BuiltInComponentAction::NamePicker);

	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandSettings",
		"component.shortcutButton.linkage.classislandSettings",
		[] { return setlist.component.shortcutButton.linkage.classislandSettings; },
		IW(componentText.ClassIslandSection).c_str(), IW(componentText.ClassIslandSettingsTitle).c_str(), IW(componentLabels.ClassIslandSettingsLabel).c_str(), L"compClassSettings",
		BuiltInComponentAction::ClassIslandSettings);
	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandProfile",
		"component.shortcutButton.linkage.classislandProfile",
		[] { return setlist.component.shortcutButton.linkage.classislandProfile; },
		IW(componentText.ClassIslandSection).c_str(), IW(componentText.ClassIslandProfileTitle).c_str(), IW(componentLabels.ClassIslandProfileLabel).c_str(), L"compClassProfile",
		BuiltInComponentAction::ClassIslandProfile);
	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandClassswap",
		"component.shortcutButton.linkage.classislandClassswap",
		[] { return setlist.component.shortcutButton.linkage.classislandClassswap; },
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
	const auto apply = [&](BarButtonClass* button)
	{
		if (!button) return;
		auto size = button->baseSize.load();
		bool hide = false;
		BarWidgetState state = BarWidgetState::None;
		switch (button->preset.load())
		{
		case BarButtonPresetEnum::Select:
			state = selection && !canvasDebugVisual ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Draw:
			state = !canvasDebugVisual && stateMode.StateModeSelect == StateModeSelectEnum::IdtPen ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Eraser:
			hide = compactSelection;
			state = !canvasDebugVisual && stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Geometry:
			hide = compactSelection;
			state = !canvasDebugVisual && stateMode.StateModeSelect == StateModeSelectEnum::IdtShape ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Clean:
			state = selection && !hasContent ? BarWidgetState::Disable : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Whiteboard:
			size = a2.whiteboardTwoTwo ? BarButtonSizeEnum::twoTwo : BarButtonSizeEnum::twoOne;
			hide = !featureEnabled;
			state = whiteboard ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Freeze:
			size = a2.freezeTwoTwo ? BarButtonSizeEnum::twoTwo : BarButtonSizeEnum::twoOne;
			hide = !a2.freezeVisible;
			state = !freezeAvailable ? BarWidgetState::Disable : (freezeActive ? BarWidgetState::Selected : BarWidgetState::None);
			break;
		case BarButtonPresetEnum::Pierce:
			size = a2.pierceTwoTwo ? BarButtonSizeEnum::twoTwo : BarButtonSizeEnum::twoOne;
			hide = !a2.pierceVisible;
			state = !canvasDebugVisual && stateMode.StateModeSelect == StateModeSelectEnum::IdtClickThrough ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::EndShow:
			hide = !a2.endShowVisible;
			break;
		case BarButtonPresetEnum::CanvasDebug:
			hide = !canvasDebugEnabled;
			state = canvasDebugVisual ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Recall:
			size = recall.undoExpanded ? BarButtonSizeEnum::twoTwo : BarButtonSizeEnum::twoOne;
			hide = !recall.mainUndoVisible;
			// 灰色历史按钮仍可点击延期，由命令可用性阻止发布，不能设成Disable。
			break;
		case BarButtonPresetEnum::Redo:
			size = recall.redoExpanded ? BarButtonSizeEnum::twoTwo : BarButtonSizeEnum::twoOne;
			hide = !recall.mainRedoVisible;
			break;
		case BarButtonPresetEnum::More:
			state = moreOpen ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		case BarButtonPresetEnum::Setting:
			state = settingVisible ? BarWidgetState::Selected : BarWidgetState::None;
			break;
		default: break;
		}
		button->size = size;
		button->userVisible = button->baseUserVisible.load();
		button->hide = hide;
		button->state->state = state;
	};
	{
		shared_lock lock(registrationMutex);
		for (const auto& [id, registration] : registrations) apply(registration.button.get());
	}
	for (const auto& divider : boundaryDividers) apply(divider.get());
	apply(moreButton.get()); apply(mainRedoButton.get()); apply(mainPierceButton.get());
	apply(mainCanvasDebugButton.get());
	barUISet.barState.recallAttribute = recall.popupOpen;
	// 内容仍从当前动画值过渡；同目标不会重启文字或图标动画。
	// 长标签换行由语言资源指定，翻译调整不再依赖英文原文匹配。
	auto label = whiteboard ? IW(I18nKey.UI.Bar.MainButtons.MoveLabel)
		: IW(separate && !selection ? I18nKey.UI.Bar.MainButtons.SelectClearLabel : I18nKey.UI.Bar.MainButtons.SelectLabel);
	preset[(int)BarButtonPresetEnum::Select]->TransitionContent(whiteboard ? L"barMove" : L"barSelect", label, immediateLabel);
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
	if (auto it = registrations.find("Component.ShortcutButton.Appliance.Explorer"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SoftwareSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ExplorerTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ExplorerLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Appliance.Taskmgr"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SoftwareSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.TaskManagerTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.TaskManagerLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Appliance.Control"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SoftwareSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ControlPanelTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ControlPanelLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.System.Desktop"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SystemSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ShowDesktopTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ShowDesktopLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.System.LockWorkStation"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.SystemSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.LockScreenTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.LockScreenLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Keyboard.Keyboardesc"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.KeyboardSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.EscapeTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.EscapeLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Keyboard.KeyboardAltF4"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.KeyboardSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.AltF4Title);
	}
	if (auto it = registrations.find("Component.ShortcutButton.RollCall.IslandCaller1"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.IslandCaller1Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.RollCall.IslandCaller2"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.IslandCaller2Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.RollCall.SecRandom1"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.SecRandom1Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.RollCall.SecRandom2"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.SecRandom2Title);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.RollCall.SecRandom2Compat"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.SecRandom2CompatTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.RollCall.NamePicker"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.RollCallSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.NamePickerTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.RollCallLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Linkage.ClassislandSettings"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.ClassIslandSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ClassIslandSettingsTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ClassIslandSettingsLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Linkage.ClassislandProfile"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.ClassIslandSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ClassIslandProfileTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ClassIslandProfileLabel));
	}
	if (auto it = registrations.find("Component.ShortcutButton.Linkage.ClassislandClassswap"); it != registrations.end()) {
		auto& entry = it->second;
		entry.categoryName = IW(I18nKey.SettingsUI.Component.ClassIslandSection);
		entry.settingsName = IW(I18nKey.SettingsUI.Component.ClassIslandSwapTitle);
		if (entry.button) entry.button->name.SetStringImmediate(IW(I18nKey.UI.Bar.Components.ClassIslandSwapLabel));
	}
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
	button->TransitionContent(
		laser ? L"barLaser" : (highlighter ? L"barHighlighter1" :
			(hardPen ? L"barBrush1" : L"barBrush2")),
		selected ? (laser ? IW(I18nKey.UI.Bar.MainButtons.LaserLabel)
			: (highlighter ? IW(I18nKey.UI.Bar.MainButtons.HighlighterLabel)
				: (hardPen ? IW(I18nKey.UI.Bar.MainButtons.HardPenLabel)
					: IW(I18nKey.UI.Bar.MainButtons.SoftPenLabel))))
			: IW(I18nKey.UI.Bar.MainButtons.DrawLabel), immediateLabel);
	drawButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateWhiteboardButtonStyle(bool active, bool minimized, bool immediateLabel)
{
	auto whiteboard = preset[(int)BarButtonPresetEnum::Whiteboard];
	if (!whiteboard) return;
	if (auto* pierce = preset[(int)BarButtonPresetEnum::Pierce])
	{
		auto label = IW(I18nKey.UI.Bar.MainButtons.ClickThroughLabel);
		pierce->TransitionContent(L"barPierce", label, immediateLabel);
	}
	const bool resume = active && minimized;
	const bool returnToShow = active && !resume && Inkeys::UI::Bar::PptPresentationActive();
	// 每个状态使用专属图标：回到白板、返回放映、关闭白板与打开白板。
	whiteboard->TransitionContent(resume ? L"barResumeWhiteboard"
			: returnToShow ? L"barReturnShow"
			: active ? L"barCloseWhiteboard" : L"barWhiteboard",
		resume ? IW(I18nKey.UI.Bar.MainButtons.ResumeWhiteboardLabel)
			: returnToShow ? IW(I18nKey.UI.Bar.MainButtons.ReturnToShowLabel)
			: active ? IW(I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel)
				: IW(I18nKey.UI.Bar.MainButtons.WhiteboardLabel), immediateLabel);
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
	button->TransitionContent(
		L"barEraser", selected
			? IW(I18nKey.UI.Bar.MainButtons.AreaEraserLabel)
			: IW(I18nKey.UI.Bar.MainButtons.EraserLabel), immediateLabel);
	eraserButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateGeometryButtonStyle(const StateModeClass& stateMode, bool immediateLabel)
{
	static mutex mtx;
	bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtShape && !IsCanvasDebugVisualActive();
	bool rectangle = selected
		&& stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1;
	int styleKey = selected ? (rectangle ? 2 : 1) : 0;
	if (geometryButtonStyleKey == styleKey) return;

	lock_guard<mutex> lock(mtx);
	if (geometryButtonStyleKey == styleKey) return;
	auto button = preset[(int)BarButtonPresetEnum::Geometry];
	if (!button) return;
	const wchar_t* resourceName = !selected
		? L"barGeometry" : (rectangle
			? L"barShapeRectangle" : L"barShapeStraightLine");
	const wstring label = !selected
		? IW(I18nKey.UI.Bar.MainButtons.GeometryLabel)
		: (rectangle ? IW(I18nKey.UI.Bar.MainButtons.RectangleLabel)
			: IW(I18nKey.UI.Bar.MainButtons.StraightLineLabel));
	button->TransitionContent(resourceName, label, immediateLabel);
	geometryButtonStyleKey = styleKey;
}

Inkeys::BarButtonSizeKind BarButtonSetClass::ToConfigSize(BarButtonSizeEnum size)
{
	switch (size)
	{
	case BarButtonSizeEnum::twoTwo: return Inkeys::BarButtonSizeKind::TwoTwo;
	case BarButtonSizeEnum::twoOne: return Inkeys::BarButtonSizeKind::TwoOne;
	case BarButtonSizeEnum::oneTwo: return Inkeys::BarButtonSizeKind::OneTwo;
	case BarButtonSizeEnum::oneOne: return Inkeys::BarButtonSizeKind::OneOne;
	}
	return Inkeys::BarButtonSizeKind::TwoTwo;
}

BarButtonSizeEnum BarButtonSetClass::ToRuntimeSize(Inkeys::BarButtonSizeKind size)
{
	switch (size)
	{
	case Inkeys::BarButtonSizeKind::TwoTwo: return BarButtonSizeEnum::twoTwo;
	case Inkeys::BarButtonSizeKind::TwoOne: return BarButtonSizeEnum::twoOne;
	case Inkeys::BarButtonSizeKind::OneTwo: return BarButtonSizeEnum::oneTwo;
	case Inkeys::BarButtonSizeKind::OneOne: return BarButtonSizeEnum::oneOne;
	}
	return BarButtonSizeEnum::twoTwo;
}

bool BarButtonSetClass::IsExactFixedZonePermutation(
	const std::vector<Inkeys::BarFixedButtonLayoutEntry>& configured,
	const std::vector<Inkeys::BarFixedButtonLayoutEntry>& defaults)
{
	if (configured.size() != defaults.size()) return false;

	std::unordered_map<std::string, int> expectedCounts;
	expectedCounts.reserve(defaults.size());
	for (const Inkeys::BarFixedButtonLayoutEntry& entry : defaults)
	{
		expectedCounts[entry.Id] += 1;
	}

	std::unordered_map<std::string, int> actualCounts;
	actualCounts.reserve(configured.size());
	for (const Inkeys::BarFixedButtonLayoutEntry& entry : configured)
	{
		if (!expectedCounts.contains(entry.Id)) return false;
		actualCounts[entry.Id] += 1;
	}

	return actualCounts == expectedCounts;
}

std::vector<Inkeys::BarFixedButtonLayoutEntry> BarButtonSetClass::NormalizeFixedZone(
	const std::vector<Inkeys::BarFixedButtonLayoutEntry>& configured,
	const std::vector<Inkeys::BarFixedButtonLayoutEntry>& defaults,
	BarButtonLayoutZoneEnum zone)
{
	// 固定区配置不含交界 Divider；旧配置里的 Divider 先剥离再严校验。
	std::vector<Inkeys::BarFixedButtonLayoutEntry> configuredWithoutDivider;
	configuredWithoutDivider.reserve(configured.size());
	for (const Inkeys::BarFixedButtonLayoutEntry& entry : configured)
	{
		if (Inkeys::IsRuntimeBoundaryDividerId(entry.Id)) continue;
		configuredWithoutDivider.push_back(entry);
	}
	// 旧 A2 只有 Whiteboard/Freeze；两种合法顺序都保留，并在末尾补 EndShow。
	if (zone == BarButtonLayoutZoneEnum::FixedA2
		&& configuredWithoutDivider.size() == 2)
	{
		if (Inkeys::UI::Bar::IsLegacyBarA2Pair(
			configuredWithoutDivider[0].Id, configuredWithoutDivider[1].Id))
			configuredWithoutDivider.push_back({ Inkeys::BarButtonId::EndShow,
				Inkeys::BarButtonSizeKind::TwoTwo });
	}

	// 旧版默认顺序只迁移一次；其他合法自定义排列仍按原顺序保留。
	const std::string_view legacyA1Order[] =
	{
		Inkeys::BarButtonId::Select,
		Inkeys::BarButtonId::Draw,
		Inkeys::BarButtonId::Eraser,
		Inkeys::BarButtonId::Geometry,
		Inkeys::BarButtonId::Recall,
		Inkeys::BarButtonId::Clean,
	};
	bool legacyA1Default = zone == BarButtonLayoutZoneEnum::FixedA1
		&& configuredWithoutDivider.size() == std::size(legacyA1Order);
	for (size_t index = 0; legacyA1Default && index < std::size(legacyA1Order); ++index)
		legacyA1Default = configuredWithoutDivider[index].Id == legacyA1Order[index];

	// 严校验：必须是该区 required 集合的恰好排列，否则整区回默认。
	bool useConfigured = !legacyA1Default
		&& IsExactFixedZonePermutation(configuredWithoutDivider, defaults);
	std::vector<Inkeys::BarFixedButtonLayoutEntry> source =
		useConfigured ? configuredWithoutDivider : defaults;

	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalized;
	normalized.reserve(source.size());
	for (const Inkeys::BarFixedButtonLayoutEntry& entry : source)
	{
		if (Inkeys::IsRuntimeBoundaryDividerId(entry.Id)) continue;

		BarButtonRegistrationClass registration;
		if (!TryGetRegistration(entry.Id, registration)) continue;
		if (registration.zone != zone) continue;

		// Size 本轮只镜像注册默认；后续开放用户改尺寸时再保留合法非默认值。
		normalized.push_back({ entry.Id, ToConfigSize(registration.defaultSize) });
	}

	if (normalized.size() != defaults.size())
	{
		normalized.clear();
		for (const Inkeys::BarFixedButtonLayoutEntry& entry : defaults)
		{
			if (Inkeys::IsRuntimeBoundaryDividerId(entry.Id)) continue;
			normalized.push_back({ entry.Id, entry.Size });
		}
	}
	return normalized;
}

std::vector<Inkeys::BarExtensionButtonLayoutEntry> BarButtonSetClass::NormalizeExtensionZone(
	const std::vector<Inkeys::BarExtensionButtonLayoutEntry>& configured)
{
	std::vector<Inkeys::BarExtensionButtonLayoutEntry> normalized;
	normalized.reserve(configured.size());
	std::unordered_set<std::string> loadedSingletons;

	for (const Inkeys::BarExtensionButtonLayoutEntry& entry : configured)
	{
		BarButtonRegistrationClass registration;
		const bool registered = TryGetRegistration(entry.Id, registration);
		if (registered)
		{
			if (registration.zone != BarButtonLayoutZoneEnum::Extension) continue;
			if (registration.kind == BarButtonRegistrationKindEnum::LayoutMarker)
			{
				if (!loadedSingletons.emplace(entry.Id).second) continue;
				// 布局标识没有实体，Visible/Size 仅写回稳定规范值。
				normalized.push_back({
					entry.Id,
					ToConfigSize(registration.defaultSize),
					true
				});
				continue;
			}
			if (!registration.allowMultiple && !loadedSingletons.emplace(entry.Id).second) continue;

			// 配置中相邻分割线只保留一条（扩展区若未来出现同类装饰项时同样适用）。
			if (!normalized.empty()
				&& Inkeys::IsRuntimeBoundaryDividerId(normalized.back().Id)
				&& Inkeys::IsRuntimeBoundaryDividerId(entry.Id))
			{
				continue;
			}

			normalized.push_back({
				entry.Id,
				ToConfigSize(registration.defaultSize),
				entry.Visible
			});
		}
		else
		{
			// 未注册的官方 ID 不可伪装成插件；合法插件 ID 仍永久保留。
			if (!Inkeys::IsExtensionBarButtonId(entry.Id)) continue;
			// 未知插件 ID 永久保留，Size 仍纠正为通用默认，Visible 保留。
			normalized.push_back({
				entry.Id,
				Inkeys::DefaultSizeForBarButtonId(entry.Id),
				entry.Visible
			});
		}
	}

	return normalized;
}

void BarButtonSetClass::AppendFixedButtons(
	const std::vector<Inkeys::BarFixedButtonLayoutEntry>& entries,
	vector<shared_ptr<BarButtonClass>>& activeButtons)
{
	for (const Inkeys::BarFixedButtonLayoutEntry& entry : entries)
	{
		BarButtonRegistrationClass registration;
		if (!TryGetRegistration(entry.Id, registration) || registration.button == nullptr) continue;

		// A 区显隐只来自注册默认，不读配置 Visible。
		registration.button->baseUserVisible = registration.defaultUserVisible;
		registration.button->baseSize = ToRuntimeSize(entry.Size);
		activeButtons.push_back(registration.button);
	}
}

vector<shared_ptr<BarButtonClass>> BarButtonSetClass::GetLegacyExtensionButtons()
{
	vector<BarButtonRegistrationClass> registrationsSnapshot = GetExtensionRegistrations();
	unordered_map<std::string, BarButtonRegistrationClass> registrationsById;
	unordered_set<std::string> enabledIds;
	for (const BarButtonRegistrationClass& registration : registrationsSnapshot)
	{
		if (registration.kind != BarButtonRegistrationKindEnum::EntityButton
			|| registration.id == Inkeys::BarButtonId::Setting)
			continue;
		registrationsById.emplace(registration.id, registration);
		if (registration.legacyEnabled && registration.legacyEnabled())
			enabledIds.insert(registration.id);
	}

	vector<std::string> activeOrder;
	{
		lock_guard lock(legacyOrderMutex);
		if (!legacyOrderInitialized)
		{
			for (const BarButtonRegistrationClass& registration : registrationsSnapshot)
				if (registration.kind == BarButtonRegistrationKindEnum::EntityButton
					&& registration.id != Inkeys::BarButtonId::Setting
					&& enabledIds.contains(registration.id))
					legacyActiveOrder.push_back(registration.id);
			legacyOrderInitialized = true;
		}
		else
		{
			legacyActiveOrder.erase(
				remove_if(legacyActiveOrder.begin(), legacyActiveOrder.end(),
					[&](const std::string& id) { return !enabledIds.contains(id); }),
				legacyActiveOrder.end());
			for (const BarButtonRegistrationClass& registration : registrationsSnapshot)
				if (registration.kind == BarButtonRegistrationKindEnum::EntityButton
					&& registration.id != Inkeys::BarButtonId::Setting
					&& enabledIds.contains(registration.id)
					&& find(legacyActiveOrder.begin(), legacyActiveOrder.end(), registration.id)
						== legacyActiveOrder.end())
					legacyActiveOrder.push_back(registration.id);
		}
		activeOrder = legacyActiveOrder;
	}

	vector<shared_ptr<BarButtonClass>> result;
	result.reserve(activeOrder.size());
	for (const std::string& id : activeOrder)
	{
		auto registration = registrationsById.find(id);
		if (registration == registrationsById.end() || !registration->second.button) continue;
		registration->second.button->baseUserVisible = true;
		registration->second.button->baseSize = registration->second.defaultSize;
		result.push_back(registration->second.button);
	}
	return result;
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
	unique_lock lock(presentationMutex);
	// UI3 与 UI2 并行期间，B 区只由旧组件开关投影，完全忽略新版 ExtensionButtons。
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> defaultA1 =
		Inkeys::MakeDefaultFixedButtonsA1().Snapshot();
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> defaultA2 =
		Inkeys::MakeDefaultFixedButtonsA2().Snapshot();

	const std::vector<Inkeys::BarFixedButtonLayoutEntry> configuredA1 =
		Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot();
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> configuredA2 =
		Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot();
	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalizedA1 = NormalizeFixedZone(
		configuredA1,
		defaultA1,
		BarButtonLayoutZoneEnum::FixedA1);
	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalizedA2 = NormalizeFixedZone(
		configuredA2,
		defaultA2,
		BarButtonLayoutZoneEnum::FixedA2);
	auto fixedEntriesEqual = [](const auto& left, const auto& right)
		{
			if (left.size() != right.size()) return false;
			for (size_t index = 0; index < left.size(); ++index)
				if (left[index].Id != right[index].Id ||
					left[index].Size != right[index].Size) return false;
			return true;
		};
	const bool fixedLayoutMigrated =
		!fixedEntriesEqual(configuredA1, normalizedA1) ||
		!fixedEntriesEqual(configuredA2, normalizedA2);

	vector<shared_ptr<BarButtonClass>> activeButtons;
	AppendFixedButtons(normalizedA1, activeButtons);
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
	vector<shared_ptr<BarButtonClass>> legacyButtons = GetLegacyExtensionButtons();
	vector<shared_ptr<BarButtonClass>> forcedOverflow;
	vector<shared_ptr<BarButtonClass>> mainButtons;
	for (const shared_ptr<BarButtonClass>& button : legacyButtons)
	{
		if (mainButtons.size() < 2) mainButtons.push_back(button);
		else forcedOverflow.push_back(button);
	}
	vector<shared_ptr<BarButtonClass>> explicitMore;
	BarButtonRegistrationClass settingRegistration;
	if (TryGetRegistration(Inkeys::BarButtonId::Setting, settingRegistration)
		&& settingRegistration.button)
	{
		settingRegistration.button->baseUserVisible = true;
		settingRegistration.button->baseSize = settingRegistration.defaultSize;
		explicitMore.push_back(settingRegistration.button);
	}
	{
		lock_guard lock(moreSnapshotMutex);
		moreSnapshot.forcedOverflow = forcedOverflow;
		moreSnapshot.explicitMore = explicitMore;
	}
	AppendBoundaryDivider(activeButtons, 0); // A1 | B
	activeButtons.insert(activeButtons.end(), mainButtons.begin(), mainButtons.end());
	if (moreButton) activeButtons.push_back(moreButton);
	AppendBoundaryDivider(activeButtons, 1); // B | A2
	AppendFixedButtons(normalizedA2, activeButtons);
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

	// 只规范化 A1/A2；运行时投影不读取、修改或写回持久化 B 区。
	Inkeys::config.UI.Bar.FixedButtonsA1.Replace(std::move(normalizedA1));
	Inkeys::config.UI.Bar.FixedButtonsA2.Replace(std::move(normalizedA2));
	// 列表和基准发布后套同一最终投影，不把配置尺寸短暂写回有效网格。
	ApplyMainPresentationLocked(GetStateModeSnapshot());
	lock.unlock();
	if (fixedLayoutMigrated)
		(void)Inkeys::config.Write(); // 迁移保存不占用主栏表现锁。
}

void BarButtonSetClass::SyncLegacyExtensionButtons()
{
	Load();
}

void BarButtonSetClass::ResetIconCaches()
{
	shared_lock lock(registrationMutex);
	for (const auto& [id, registration] : registrations)
	{
		if (!registration.button) continue;
		registration.button->icon.ResetCache();
		if (registration.button->iconKind == BarButtonIconKindEnum::Png)
			registration.button->pngIcon.ResetCache();
	}
	for (const shared_ptr<BarButtonClass>& divider : boundaryDividers)
	{
		if (!divider) continue;
		divider->icon.ResetCache();
		if (divider->iconKind == BarButtonIconKindEnum::Png)
			divider->pngIcon.ResetCache();
	}
	if (moreButton) moreButton->icon.ResetCache();
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
			const auto view = ResolveRecallChrome(owner.recallChrome, IsCompactSelectionBar());
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
	recallChromePresentation = Inkeys::UI::Bar::ResolveRecallChrome(recallChrome, compactSelection);
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
		const auto view = Inkeys::UI::Bar::ResolveRecallChrome(recallChrome, IsCompactSelectionBar());
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
	bool StartBarButtonHoverVisual(BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtonHoverStageEnum>* hoverStage)
	{
		if (!hoverPct || !hoverFill || !hoverStage) return false;
		hoverPct->animateWhenDisabled = true;
		hoverFill->animateWhenDisabled = true;
		const BarUiCurveSpecClass curve{
			BarUiCurveEnum::EaseOutSine,
			BarUiCurveEnum::EaseOutSine, 0.0, false };
		hoverFill->SetTar(GetThemeColor(BarThemeColorEnum::PressedFill),
			BarButtonHoverTransitionDuration, curve);
		hoverPct->SetTar(BarButtonHoverOpacity,
			BarButtonHoverTransitionDuration, std::nullopt, true, curve);
		*hoverStage = BarButtonHoverStageEnum::Showing;
		return true;
	}

	bool StopBarButtonHoverVisual(BarUiPctClass* hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtonHoverStageEnum>* hoverStage, bool immediate, bool preserveVisual)
	{
		if (!hoverPct || !hoverStage) return false;
		if (immediate)
		{
			*hoverStage = BarButtonHoverStageEnum::None;
			// 按下时保留当前视觉值交给按压态续接，隐藏等场景仍立即清零。
			if (!preserveVisual) hoverPct->SetDirect(0.0);
			hoverPct->animateWhenDisabled = false;
			if (hoverFill) hoverFill->animateWhenDisabled = false;
		}
		else
		{
			// 离开后保持背景颜色，透明度自然降为零后再结束悬停。
			hoverPct->animateWhenDisabled = true;
			if (hoverFill) hoverFill->animateWhenDisabled = true;
			*hoverStage = BarButtonHoverStageEnum::Fading;
			const BarUiCurveSpecClass curve{
				BarUiCurveEnum::EaseOutSine,
				BarUiCurveEnum::EaseOutSine, 0.0, false };
			hoverPct->SetTar(0.0,
				BarButtonHoverTransitionDuration,
				std::nullopt, true, curve);
		}
		return true;
	}

	bool StartBarButtonHoverVisual(BarButtonClass& button) noexcept
	{
		if (button.preset == BarButtonPresetEnum::Divider
			|| !button.button.fill.has_value()) return false;
		// 属性面板的 Shape 与完整按钮共用悬停动画，按钮仅保留自身门控。
		return StartBarButtonHoverVisual(&button.button.pct,
			&button.button.fill.value(), &button.hoverStage);
	}

	bool StopBarButtonHoverVisual(BarButtonClass& button, bool immediate,
		bool preserveVisual) noexcept
	{
		if (button.preset == BarButtonPresetEnum::Divider)
		{
			button.hoverStage = BarButtonHoverStageEnum::None;
			button.state->emph = BarWidgetEmphasize::None;
			button.pressScale.SetDirect(1.0);
			button.button.pct.animateWhenDisabled = false;
			if (button.button.fill.has_value())
				button.button.fill->animateWhenDisabled = false;
			return true;
		}
		if (!button.button.fill.has_value()) return false;
		return StopBarButtonHoverVisual(&button.button.pct,
			&button.button.fill.value(), &button.hoverStage, immediate, preserveVisual);
	}

	bool UpdateBarButtonHoverVisual(BarUiPctClass& pct, BarUiColorClass* fill,
		IdtAtomic<BarButtonHoverStageEnum>& stage, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept
	{
		auto Finish = [&]() noexcept
			{
				stage = BarButtonHoverStageEnum::None;
				pct.animateWhenDisabled = false;
				if (fill != nullptr)
					fill->animateWhenDisabled = false;
			};
		if (!visible)
		{
			if (stage != BarButtonHoverStageEnum::None)
				pct.SetDirect(0.0);
			Finish();
			return true;
		}
		if (!hoverAllowed)
		{
			Finish();
			return true;
		}
		if (stage == BarButtonHoverStageEnum::Showing
			&& pct.IsSame())
		{
			stage = BarButtonHoverStageEnum::Fading;
			const BarUiCurveSpecClass curve{
				BarUiCurveEnum::EaseInSine,
				BarUiCurveEnum::EaseInSine, 0.0, false };
			pct.SetTar(0.0, fadeDurationSeconds,
				std::nullopt, true, curve);
			return true;
		}
		if (stage == BarButtonHoverStageEnum::Fading
			&& pct.IsSame())
		{
			Finish();
			return true;
		}
		return false;
	}

	bool UpdateBarButtonHoverVisual(BarButtonClass& button, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept
	{
		return UpdateBarButtonHoverVisual(button.button.pct,
			button.button.fill ? &*button.button.fill : nullptr,
			button.hoverStage, visible, hoverAllowed, fadeDurationSeconds);
	}

	void SetBarButtonPressedVisual(BarButtonClass& button, bool pressed) noexcept
	{
		button.state->emph = pressed
			? BarWidgetEmphasize::Pressed : BarWidgetEmphasize::None;
	}

	void RetargetBarFixedButtonVisual(const BarButtonVisualParts& parts,
		bool visible, bool enabled, bool selected, bool pressed, double duration,
		std::optional<COLORREF> contentOverride, bool iconVisible,
		std::optional<COLORREF> fillOverride, std::optional<COLORREF> frameOverride) noexcept
	{
		auto& shape=parts.background;
		const COLORREF frameColor=frameOverride.value_or(GetThemeColor(selected
			?BarThemeColorEnum::Accent:BarThemeColorEnum::TextPrimary));
		if(!shape.fill)shape.fill.emplace(GetThemeColor(BarThemeColorEnum::PressedFill));
		if(!shape.frame)shape.frame.emplace(frameColor);
		if(!shape.framePct)shape.framePct.emplace(0.0);
		if(!shape.frameLightPct)shape.frameLightPct.emplace(0.0);
		if(!shape.ft)shape.ft.emplace(BarButtonFrameThicknessDip);
		shape.frameRendering=BarUiFrameRenderingEnum::PointLight;
		shape.framePrimaryLightEnabled=false;
		shape.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
		shape.frameLightCursorLightIntensityScale=BarButtonLightCursorLightIntensity;
		shape.frame->SetTar(frameColor);
		// 主题选中底色在浅色端为不透明浅青色；显式配色的客户端保持原透明度。
		const double lightWeight=barUISet.barStyle.darkStyle?0.0:1.0;
		if(!visible)shape.pct.SetTar(0.0,duration);
		else if(pressed)shape.pct.SetTar(selected&&!fillOverride
			?BarThemeMaterial::SelectedFillOpacity(lightWeight,BarButtonPressedOpacity)
			:BarButtonPressedOpacity,duration);
		else if(selected)shape.pct.SetTar(fillOverride?0.20
			:BarThemeMaterial::SelectedFillOpacity(lightWeight),duration);
		else if(!parts.hoverStage || *parts.hoverStage==BarButtonHoverStageEnum::None)
			shape.pct.SetTar(0.0,duration);
		shape.fill->SetTar(fillOverride.value_or(GetThemeColor(selected
			?BarThemeColorEnum::SelectedFill:BarThemeColorEnum::PressedFill)));
		shape.frameLightPct->SetTar(visible && enabled && selected
			?(pressed?BarButtonPressedLightOpacity:1.0):0.0,duration);
		if(parts.pressScale)parts.pressScale->SetTar(pressed?BarButtonPressScale:1.0,
			static_cast<double>(BarUiDefaultOperationDur),std::nullopt,false,
			pressed?BarButtonPressCurve():BarButtonReleaseCurve());
		const double opacity=visible?(enabled?1.0:BarThemeMaterial::DisabledContentOpacity(lightWeight)):0.0;
		const COLORREF content=contentOverride.value_or(GetThemeColor(selected
			?BarThemeColorEnum::Accent:BarThemeColorEnum::TextPrimary));
		const COLORREF iconContent=contentOverride.value_or(GetThemeColor(selected
			?BarThemeColorEnum::Accent:BarThemeColorEnum::IconPrimary));
		if(parts.icon)
		{
			parts.icon->pct.SetTar(iconVisible?opacity:0.0,BarButtonHoverTransitionDuration);
			if(parts.icon->color1)parts.icon->color1->SetTar(iconContent);
		}
		if(parts.primary)
		{
			parts.primary->pct.SetTar(opacity,BarButtonHoverTransitionDuration);
			parts.primary->color.SetTar(content);
		}
	}

	void RetargetBarButtonInteractionVisual(BarButtonClass& button,
		bool visible,bool enabled,bool selected,double duration,
		std::optional<COLORREF> content,bool iconVisible,
		std::optional<COLORREF> fill,std::optional<COLORREF> frame) noexcept
	{
		RetargetBarFixedButtonVisual({button.button,&button.icon,&button.name,
			&button.pressScale,&button.hoverStage},visible,enabled,selected,
			button.state->emph==BarWidgetEmphasize::Pressed,duration,content,iconVisible,fill,frame);
		if(button.icon.color2)button.icon.color2->SetTar(content.value_or(GetThemeColor(selected
			?BarThemeColorEnum::Accent:BarThemeColorEnum::IconPrimary)));
	}

	BarButtonVisualInheritance PrepareBarButtonVisualInheritance(
		BarButtonClass& button, const BarUiInheritClass& inherit,
		BarUiWordClass* secondary) noexcept
	{
		const BarUiInheritClass buttonInherit = button.button.UpInh(inherit);
		const auto iconPoint = ResolveBarButtonChildTopLeft(
			buttonInherit.x, buttonInherit.y,
			button.button.w.val, button.button.h.val,
			button.icon.x.val, button.icon.y.val,
			button.icon.w.val, button.icon.h.val);
		const BarUiInheritClass iconInherit = button.icon.UpInh(
			BarUiInheritClass(iconPoint.x, iconPoint.y));
		const auto primaryPoint = ResolveBarButtonChildTopLeft(
			buttonInherit.x, buttonInherit.y,
			button.button.w.val, button.button.h.val,
			button.name.x.val, button.name.y.val,
			button.name.w.val, button.name.h.val);
		const BarUiInheritClass primaryInherit = button.name.UpInh(
			BarUiInheritClass(primaryPoint.x, primaryPoint.y));
		std::optional<BarUiInheritClass> secondaryInherit;
		if (secondary)
		{
			const auto secondaryPoint = ResolveBarButtonChildTopLeft(
				buttonInherit.x, buttonInherit.y,
				button.button.w.val, button.button.h.val,
				secondary->x.val, secondary->y.val,
				secondary->w.val, secondary->h.val);
			secondaryInherit = secondary->UpInh(
				BarUiInheritClass(secondaryPoint.x, secondaryPoint.y));
		}
		return { buttonInherit, iconInherit, primaryInherit,
			secondaryInherit };
	}

	namespace
	{
		std::optional<D2D1_RECT_F> ComputeBarButtonVisualBounds(BarUIRendering& renderer,
			const BarButtonVisualParts& parts, const BarButtonVisualInheritance& inherit,
			const BarUiWordClass* secondary = nullptr)
		{
			const double zoom = renderer.GetFrameZoom();
			D2D1_RECT_F bounds{}; bool first = true, valid = true;
			const auto Include = [&](double left, double top, double right, double bottom, double padding)
			{
				const auto next = D2D1::RectF(static_cast<FLOAT>(left - padding), static_cast<FLOAT>(top - padding),
					static_cast<FLOAT>(right + padding), static_cast<FLOAT>(bottom + padding));
				if (!std::isfinite(next.left) || !std::isfinite(next.top) || !std::isfinite(next.right)
					|| !std::isfinite(next.bottom)) { valid = false; return; }
				if (next.left >= next.right || next.top >= next.bottom) return;
				if (first) { bounds = next; first = false; return; }
				bounds.left = (std::min)(bounds.left, next.left); bounds.top = (std::min)(bounds.top, next.top);
				bounds.right = (std::max)(bounds.right, next.right); bounds.bottom = (std::max)(bounds.bottom, next.bottom);
			};
			Include(inherit.button.x * zoom, inherit.button.y * zoom,
				(inherit.button.x + parts.background.w.val) * zoom,
				(inherit.button.y + parts.background.h.val) * zoom, renderer.GetFrameShapeDrawOutset(parts.background));
			if (parts.icon)
			{
				const auto& icon = *parts.icon;
				const double x = inherit.icon.x, y = inherit.icon.y, width = icon.w.val, height = icon.h.val;
				const double angle = icon.angle.val, scale = icon.contentScale;
				// 沿用现有旋转图像 dirty 规则；非有限/极端元数据不进入整数包络计算。
				constexpr double limit = 10000000.0;
				if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)
					|| !std::isfinite(angle) || !std::isfinite(scale) || scale <= 0
					|| std::abs(x * zoom) + std::abs(width * zoom * (std::max)(1.0, scale)) > limit
					|| std::abs(y * zoom) + std::abs(height * zoom * (std::max)(1.0, scale)) > limit) return std::nullopt;
				if (width > 0 && height > 0)
				{
					const auto image = BarRenderingAttribute::GetRotatedImageRect(x, y, width, height,
						std::fmod(angle, 360.0), zoom, scale);
					Include(image.left, image.top, image.right, image.bottom, 0);
				}
			}
			const auto IncludeWord = [&](const BarUiWordClass* word, const BarUiInheritClass& origin)
			{
				if (!word) return;
				const double scale = word->contentScale;
				if (!std::isfinite(scale)) { valid = false; return; }
				const double width = word->w.val, height = word->h.val;
				const double paddedWidth = width * (std::max)(1.0, scale), paddedHeight = height * (std::max)(1.0, scale);
				Include((origin.x + (width - paddedWidth) / 2) * zoom,
					(origin.y + (height - paddedHeight) / 2) * zoom,
					(origin.x + (width + paddedWidth) / 2) * zoom,
					(origin.y + (height + paddedHeight) / 2) * zoom, BarRenderingAttribute::dirtyAntialiasPadding);
			};
			IncludeWord(parts.primary, inherit.primary);
			if (secondary && inherit.secondary) IncludeWord(secondary, *inherit.secondary);
			return valid && !first ? std::optional<D2D1_RECT_F>(bounds) : std::nullopt;
		}

		class ButtonDrawTransform
		{
		public:
			ButtonDrawTransform(BarUIRendering& renderer, ID2D1DeviceContext* context,
				const BarUiShapeClass& background, const BarUiInheritClass& inherit, double scale, double targetScale, double duration)
				: fontScope_(renderer, targetScale, duration), context_(context)
			{
				context_->GetTransform(&original_);
				if(!std::isfinite(scale) || scale<=0.0)scale=1.0;
				changed_=std::abs(scale-1.0)>0.000001;
				if(!changed_)return;
				const FLOAT zoom=static_cast<FLOAT>(renderer.GetFrameZoom());
				context_->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<FLOAT>(scale),
					static_cast<FLOAT>(scale),D2D1::Point2F(
						static_cast<FLOAT>((inherit.x+background.w.val/2.0)*zoom),
						static_cast<FLOAT>((inherit.y+background.h.val/2.0)*zoom)))*original_);
			}
			~ButtonDrawTransform(){if(changed_)context_->SetTransform(original_);}
		private:
			BarUIRendering::FontTargetScope fontScope_;
			ID2D1DeviceContext* context_;
			D2D1_MATRIX_3X2_F original_{};
			bool changed_=false;
		};
	}

	bool DrawBarButtonVisual(BarUIRendering& renderer,
		ID2D1DeviceContext* deviceContext, BarButtonClass& button,
		const BarUiInheritClass& inherit,
		const BarButtonDrawOptions& options)
	{
		if (!deviceContext) return false;
		const auto visualInheritance = PrepareBarButtonVisualInheritance(
			button, inherit, options.secondary);
		ButtonDrawTransform transform(renderer,deviceContext,button.button,
			visualInheritance.button,button.pressScale.val,button.pressScale.tar,button.pressScale.dur);
		// 先更新继承坐标再拒绝绘制，成功呈现后的命中快照仍可读取本帧几何。
		// 自定义内容没有额外包络合同，不能连同标准按钮一起猜测裁掉。
		if (!options.underlay && !options.content)
		{
			const auto bounds = ComputeBarButtonVisualBounds(renderer, { button.button,
				options.drawIcon ? &button.icon : nullptr, &button.name }, visualInheritance, options.secondary);
			if (bounds && !renderer.ShouldDrawFrameBounds(deviceContext, *bounds)) return false;
		}

		bool rendered = options.underlay
			? options.underlay(renderer, deviceContext, button, visualInheritance) : false;
		rendered = renderer.Shape(
			deviceContext, button.button, visualInheritance.button) || rendered;
		if (button.preset != BarButtonPresetEnum::Divider)
		{
			if (options.drawIcon && button.iconKind == BarButtonIconKindEnum::Png)
			{
				button.pngIcon.x.SetDirect(button.icon.x.val);
				button.pngIcon.y.SetDirect(button.icon.y.val);
				button.pngIcon.w.SetDirect(button.icon.w.val);
				button.pngIcon.h.SetDirect(button.icon.h.val);
				button.pngIcon.angle.SetDirect(button.icon.angle.val);
				button.pngIcon.pct.SetDirect(button.icon.pct.val);
				button.pngIcon.enable.val = button.icon.enable.val;
				button.pngIcon.enable.tar = button.icon.enable.tar;
				rendered = renderer.Png(deviceContext, button.pngIcon,
					button.pngIcon.UpInh(visualInheritance.icon)) || rendered;
			}
			else if (options.drawIcon)
				rendered = renderer.Svg(deviceContext, button.icon,
					visualInheritance.icon) || rendered;
			if (options.content)
				rendered = options.content(renderer, deviceContext, button,
					visualInheritance) || rendered;
			rendered = renderer.Word(deviceContext, button.name,
				visualInheritance.primary,
				options.primaryWeight, options.primaryAlignment, true) || rendered;
			if (options.secondary && visualInheritance.secondary)
				rendered = renderer.Word(deviceContext, *options.secondary,
					*visualInheritance.secondary,
					options.secondaryWeight) || rendered;
		}
		return rendered;
	}

	bool DrawBarFixedButtonVisual(BarUIRendering& renderer,ID2D1DeviceContext* context,
		const BarButtonVisualParts& parts,const BarButtonVisualInheritance& inherit,
		DWRITE_FONT_WEIGHT weight,DWRITE_TEXT_ALIGNMENT alignment)
	{
		if(!context)return false;
		ButtonDrawTransform transform(renderer,context,parts.background,inherit.button,
			parts.pressScale?static_cast<double>(parts.pressScale->val):1.0,
			parts.pressScale?static_cast<double>(parts.pressScale->tar):1.0,
			parts.pressScale?static_cast<double>(parts.pressScale->dur):0.0);
		const auto bounds = ComputeBarButtonVisualBounds(renderer, parts, inherit);
		if (bounds && !renderer.ShouldDrawFrameBounds(context, *bounds)) return false;
		bool rendered=renderer.Shape(context,parts.background,inherit.button);
		if(parts.icon)rendered=renderer.Svg(context,*parts.icon,inherit.icon)||rendered;
		if(parts.primary)rendered=renderer.Word(context,*parts.primary,inherit.primary,weight,alignment)||rendered;
		return rendered;
	}

	bool DrawBarBackgroundVisual(BarUIRendering& renderer,
		ID2D1DeviceContext* deviceContext, BarUiShapeClass& background,
		const BarUiInheritClass& inherit, RECT* targetRect, bool clip)
	{
		return renderer.Shape(deviceContext, background, inherit,
			targetRect, clip);
	}

}

namespace Inkeys::UI::Bar
{
	void ConfigureBarButtonVisualTargets(BarButtonClass& button,
		BarButtonVisualLayoutKind layoutKind,double duration,double scale,bool direct,
		std::optional<double> iconHeightOverride,std::optional<double> fontTargetScale)
	{
		const auto metrics = ResolveBarButtonVisualMetrics(layoutKind);
		if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;
		auto Set = [&](BarUiValueClass& value, double target)
		{
			if (direct) value.SetDirect(target * scale);
			else value.SetTar(target * scale, duration);
		};
		Set(button.button.w, metrics.buttonWidthDip);
		Set(button.button.h, metrics.buttonHeightDip);
		if (button.button.rw) Set(*button.button.rw, BarButtonCornerRadiusDip);
		if (button.button.rh) Set(*button.button.rh, BarButtonCornerRadiusDip);
		if (button.button.ft) Set(*button.button.ft, BarButtonFrameThicknessDip);
		if (button.icon.enable.tar)
		{
			button.icon.SetWH(std::nullopt, iconHeightOverride.value_or(metrics.iconSizeDip) * scale);
			if (direct)
			{
				button.icon.w.SetDirect(button.icon.w.tar);
				button.icon.h.SetDirect(button.icon.h.tar);
			}
			Set(button.icon.x, metrics.iconOffsetXDip);
			Set(button.icon.y, metrics.iconOffsetYDip);
		}
		Set(button.name.x, metrics.primaryOffsetXDip);
		Set(button.name.y, metrics.primaryOffsetYDip);
		Set(button.name.w, metrics.primarySlotWidthDip);
		Set(button.name.h, metrics.primarySlotHeightDip);
		const double labelSize = button.LabelFontSize(metrics.primaryFontSizeDip);
		Set(button.name.size, labelSize);
		// 派生布局传入最终倍率；回到普通布局时清除覆盖，恢复原 size.tar 目标。
		button.name.fontTargetSize = fontTargetScale ? labelSize * *fontTargetScale : -1.0;
	}

	namespace
	{
		using Part = BarButtonAnimationPart;
		using Property = BarButtonAnimationProperty;
		// 完整按钮和固定属性中的独立 Shape/SVG/Word 使用同一属性访问与推进代码。
		struct ButtonAnimationVisitor
		{
			const BarButtonAnimationContext& frame;
			const void* visual;
			Part part;
			BarButtonAnimationResult result;
			void Observe(Property property, bool changed, bool active, bool same)
			{
				result.changed |= changed; result.active |= active;
				if (frame.observe) frame.observe(visual, part, property, changed, active, same);
			}
			template<class Value> void Advance(Value& value, bool replace, Property property)
			{
				if (value.IsSame()) { Observe(property, false, false, true); return; }
				// 零时间用于同帧重定向，不能被底层解释为立即落定。
				if (frame.animationEnabled && !replace
					&& (!std::isfinite(frame.dtSeconds) || frame.dtSeconds <= 0.0))
				{ Observe(property, false, true, false); return; }
				const auto advanced = BarUiAdvanceAnimation(value,
					{frame.dtSeconds, frame.speedRate, frame.animationEnabled, replace});
				Observe(property, advanced.changed, advanced.active, value.IsSame());
			}
			void String(BarUiStringClass& value, BarUiSVGClass* svg = nullptr)
			{
				const bool changed = !value.IsSame();
				if (changed) { value.ApplyTar(); if (svg) svg->NotifyObservedContentValueWrite(); }
				Observe(Property::Content, changed, false, value.IsSame());
			}
			template<class Visual> bool Replace(Visual& value)
			{
				const bool replace = frame.forceReplace || static_cast<bool>(value.forceReplace);
				value.forceReplace = false; return replace;
			}
		};
	}

	BarButtonAnimationResult AdvanceBarShapeVisual(BarUiShapeClass& shape,
		const BarButtonAnimationContext& frame)
	{
		ButtonAnimationVisitor v{frame, &shape, Part::Shape};
		const bool replace = v.Replace(shape);
		v.Advance(shape.enable, replace, Property::Geometry);
		for (auto* value : {&shape.x, &shape.y, &shape.w, &shape.h})
			v.Advance(*value, replace, Property::Geometry);
		for (auto* value : {&shape.rw, &shape.rh, &shape.ft})
			if (*value) v.Advance(**value, replace, Property::Geometry);
		for (auto* value : {&shape.fill, &shape.frame})
			if (*value) v.Advance(**value, replace, Property::Color);
		for (auto* value : {&shape.framePct, &shape.frameLightPct})
			if (*value) v.Advance(**value, replace, Property::Visibility);
		v.Advance(shape.pct, replace, Property::Visibility);
		return v.result;
	}

	BarButtonAnimationResult AdvanceBarSvgVisual(BarUiSVGClass& icon,
		const BarButtonAnimationContext& frame)
	{
		ButtonAnimationVisitor v{frame, &icon, Part::Icon};
		const bool replace = v.Replace(icon);
		const bool content = icon.AdvanceContentTransition(frame.dtSeconds, frame.speedRate);
		v.Observe(Property::Content, content, content, !content);
		v.Advance(icon.enable, replace, Property::Visibility);
		for (auto* value : {&icon.x, &icon.y, &icon.w, &icon.h, &icon.angle})
			v.Advance(*value, replace, Property::Geometry);
		v.String(icon.svg, frame.finiteSvgProbe ? &icon : nullptr);
		for (auto* value : {&icon.color1, &icon.color2})
			if (*value) v.Advance(**value, replace, Property::Color);
		v.Advance(icon.pct, replace, Property::Visibility);
		return v.result;
	}

	BarButtonAnimationResult AdvanceBarWordVisual(BarUiWordClass& word,
		const BarButtonAnimationContext& frame, BarButtonAnimationPart part)
	{
		ButtonAnimationVisitor v{frame, &word, part};
		const bool replace = v.Replace(word);
		if (replace) word.fontCorrection.ForceNext();
		const bool content = word.AdvanceContentTransition(frame.dtSeconds, frame.speedRate);
		v.Observe(Property::Content, content, content, !content);
		v.Advance(word.enable, replace, Property::Visibility);
		for (auto* value : {&word.x, &word.y, &word.w, &word.h, &word.size})
			v.Advance(*value, replace, Property::Geometry);
		v.String(word.content);
		v.Advance(word.color, replace, Property::Color);
		v.Advance(word.pct, replace, Property::Visibility);
		return v.result;
	}

	BarButtonAnimationResult AdvanceBarButtonVisual(BarButtonClass& button,
		const BarButtonAnimationContext& frame, BarUiWordClass* secondary)
	{
		const bool divider = button.preset == BarButtonPresetEnum::Divider;
		if (divider)
		{
			(void)StopBarButtonHoverVisual(button, true);
			SetBarButtonPressedVisual(button, false);
		}
		else (void)UpdateBarButtonHoverVisual(button, frame.visible,
			frame.hoverAllowed, BarButtonHoverFadeDurationSeconds);
		ButtonAnimationVisitor press{frame, &button.button, Part::Press};
		press.Advance(button.pressScale, frame.forceReplace, Property::Geometry);
		BarButtonAnimationResult result = press.result;
		auto Join = [&](BarButtonAnimationResult next)
		{ result.changed |= next.changed; result.active |= next.active; };
		Join(AdvanceBarShapeVisual(button.button, frame));
		Join(AdvanceBarSvgVisual(button.icon, frame));
		if (frame.finiteSvgProbe)
			button.icon.ObserveFiniteRequirement(button.iconKind == BarButtonIconKindEnum::Svg && !divider);
		Join(AdvanceBarWordVisual(button.name, frame));
		if (secondary) Join(AdvanceBarWordVisual(*secondary, frame, Part::Secondary));
		return result;
	}

	BarButtonHitSnapshot ResolveBarShapeHitSnapshot(const BarUiShapeClass& shape,
		const BarUiInheritClass& inherit,bool visible,bool enabled) noexcept
	{
		BarButtonHitSnapshot hit;
		hit.boundsDip=D2D1::RectF(static_cast<FLOAT>(inherit.x),static_cast<FLOAT>(inherit.y),
			static_cast<FLOAT>(inherit.x+shape.w.val),static_cast<FLOAT>(inherit.y+shape.h.val));
		hit.visible=visible && shape.w.val>0 && shape.h.val>0;
		hit.enabled=enabled;hit.radiusX=shape.rw?static_cast<double>(shape.rw->val):0.0;
		hit.radiusY=shape.rh?static_cast<double>(shape.rh->val):0.0;
		return hit;
	}
	BarButtonHitSnapshot ResolveBarButtonHitSnapshot(BarButtonClass& button,
		const BarUiInheritClass& inherit) noexcept
	{
		const auto current=PrepareBarButtonVisualInheritance(button,inherit);
		return ResolveBarShapeHitSnapshot(button.button,current.button,
			button.IsVisible() && button.button.enable.val && button.preset!=BarButtonPresetEnum::Divider,
			button.state->state!=BarWidgetState::Disable);
	}
	bool HitTestBarButton(const BarButtonHitSnapshot& hit,double x,double y) noexcept
	{
		if(!hit.visible || !hit.enabled || !std::isfinite(x) || !std::isfinite(y))return false;
		const auto& r=hit.boundsDip;
		return hit.superellipseExponent>0
			?BarUiSuperellipseContainsPoint(x,y,1.0,r.left,r.top,r.right-r.left,r.bottom-r.top,hit.superellipseExponent)
			:BarUiRoundedRectContainsPoint(x,y,1.0,r.left,r.top,r.right-r.left,r.bottom-r.top,hit.radiusX,hit.radiusY);
	}

}

namespace Inkeys::UI::Bar
{
	BarButtonListLayout ResolveBarButtonListLayout(
		const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		BarButtonListLayoutKind kind,
		const std::vector<std::shared_ptr<BarButtonClass>>& secondGroup)
	{
		BarButtonListLayout layout;
		constexpr double gap = BarButtonGapDip;
		constexpr double one = BarButtonOneSideDip, two = BarButtonTwoSideDip;
		constexpr double oneStep = one + gap, twoStep = two + gap;
		auto Span = [](BarButtonSizeEnum size) -> std::pair<int, int>
		{
			switch (size)
			{
			case BarButtonSizeEnum::oneOne: return {1, 1};
			case BarButtonSizeEnum::twoOne: return {2, 1};
			case BarButtonSizeEnum::oneTwo: return {1, 2};
			default: return {2, 2};
			}
		};
		if (kind == BarButtonListLayoutKind::GroupedGrid)
		{
			const auto count = (std::max)(size_t{1}, buttons.size() + secondGroup.size());
			layout.subColumns = 2 * (std::min)(5, (std::max)(1,
				static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))))));
			std::vector<std::vector<bool>> occupied;
			auto Pack = [&](const auto& group, int start, bool forced)
			{
				int used = start;
				for (const auto& button : group)
				{
					if (!button) continue;
					const auto [columns, rows] = Span(button->size);
					bool placed = false;
					for (int row = start; !placed; ++row)
					{
						while (occupied.size() < static_cast<size_t>(row + rows))
							occupied.emplace_back(layout.subColumns, false);
						for (int column = 0; column + columns <= layout.subColumns; ++column)
						{
							bool free = true;
							for (int y = 0; free && y < rows; ++y)
								for (int x = 0; x < columns; ++x)
									if (occupied[row + y][column + x]) { free = false; break; }
							if (!free) continue;
							for (int y = 0; y < rows; ++y)
								for (int x = 0; x < columns; ++x) occupied[row + y][column + x] = true;
							BarButtonListPlacement placement;
							placement.button = button; placement.column = column; placement.row = row;
							placement.columnSpan = columns; placement.rowSpan = rows; placement.forced = forced;
							placement.width = columns == 2 ? two : one;
							placement.height = rows == 2 ? two : one;
							if (button->size == BarButtonSizeEnum::oneTwo) placement.width = 10.0;
							layout.placements.push_back(std::move(placement));
							used = (std::max)(used, row + rows); placed = true; break;
						}
					}
				}
				return used;
			};
			layout.explicitRows = Pack(buttons, 0, false);
			if (layout.explicitRows % 2) ++layout.explicitRows;
			layout.showDivider = !buttons.empty() && !secondGroup.empty();
			const int forcedRows = Pack(secondGroup, layout.showDivider ? layout.explicitRows : 0, true);
			layout.totalRows = (std::max)(1, (std::max)(layout.explicitRows, forcedRows));
			layout.width = layout.subColumns * one + (layout.subColumns - 1) * gap;
			layout.height = layout.totalRows * one + (layout.totalRows - 1) * gap;
			return layout;
		}

		double x = gap, y = gap, width = gap;
		layout.height = BarMainBarHeightDip;
		for (const auto& button : buttons)
		{
			if (!button) continue;
			BarButtonListPlacement p; p.button = button;
			const auto size = button->size.load();
			const auto [columns, rows] = Span(size); p.columnSpan = columns; p.rowSpan = rows;
			p.width = columns == 2 ? two : one; p.height = rows == 2 ? two : one;
			const bool visible = button->IsVisible();
			if (kind == BarButtonListLayoutKind::SingleRow)
			{
				p.x = x + p.width / 2.0; p.y = gap + p.height / 2.0;
				if (visible) x += p.width + gap;
				width = x;
			}
			else if (size == BarButtonSizeEnum::oneOne)
			{
				p.x = x + one / 2.0; p.y = y + one / 2.0;
				if (visible)
				{
					if (y <= gap) { y += oneStep; width += oneStep; }
					else if (x + oneStep >= width) { x += oneStep; y = gap; }
					else x += oneStep;
				}
			}
			else if (size == BarButtonSizeEnum::twoOne)
			{
				if (y > gap && x + twoStep > width) { x = width; y = gap; }
				p.x = x + two / 2.0; p.y = y + one / 2.0;
				if (visible)
				{
					if (y <= gap) { y += oneStep; width += twoStep; }
					else { x += twoStep; y = gap; }
				}
			}
			else if (size == BarButtonSizeEnum::twoTwo)
			{
				if (y > gap) { y = gap; x = width; }
				p.x = x + two / 2.0; p.y = y + two / 2.0;
				if (visible) { x += twoStep; width += twoStep; }
			}
			else
			{
				if (visible) { x = width; y = gap; }
				p.x = x - gap / 2.0; p.y = y + two / 2.0;
				p.width = 1.0; p.height = 50.0;
			}
			layout.placements.push_back(std::move(p));
		}
		layout.width = width;
		return layout;
	}
}

bool BarUISetClass::HitTestPresentedButton(const BarButtonClass& button,
	double x, double y) const
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	const auto hit = presentedButtonHits_.find(&button);
	return hit != presentedButtonHits_.end() && Inkeys::UI::Bar::HitTestBarButton(hit->second, x, y);
}

bool BarUISetClass::HitTestPresentedShape(const BarUiShapeClass& shape,double x,double y) const
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	const auto hit=presentedVisualHits_.find(&shape);
	return hit!=presentedVisualHits_.end() && Inkeys::UI::Bar::HitTestBarButton(hit->second,x,y);
}
bool BarUISetClass::HitTestPresentedShape(const BarUiSuperellipseClass& shape,double x,double y) const
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	const auto hit=presentedVisualHits_.find(&shape);
	return hit!=presentedVisualHits_.end() && Inkeys::UI::Bar::HitTestBarButton(hit->second,x,y);
}

Inkeys::Input::UiPointer::Touch::Probe BarUISetClass::ProbeTouchScreen(POINT screen) const
{
	using namespace Inkeys::UI::Bar;
	using Inkeys::Input::UiPointer::Touch::Hit;
	for (;;)
	{
		const auto mapping = BottomDockPresentedSnapshot();
		std::shared_ptr<const PresentedTouchRegions> presented;
		double rebaseX = 0.0, rebaseY = 0.0;
		{
			std::lock_guard lock(presentedButtonHitsMutex_);
			presented = presentedTouchRegions_;
			rebaseX = presentedTouchRebaseXDip_; rebaseY = presentedTouchRebaseYDip_;
		}
		if (!presented) return {};
		const auto visual = BarScreenToLayoutPoint(screen, mapping.monitorOrigin, mapping.directTranslation);
		auto rigid = ResolveBarBottomDockRigidHitTestPoint(visual.x, visual.y,
			mapping.horizontalMapping, mapping.mapping, mapping.zoom);
		// 业务 ExMessage 先落到整数 Rigid 像素，再逆映射 Body/Grip；边缘保持同样舍入。
		rigid.logicalX = std::round(rigid.logicalX); rigid.logicalY = std::round(rigid.logicalY);
		const auto body = ResolveBarBottomDockBodyHitTestPointFromRigid(rigid.logicalX, rigid.logicalY,
			mapping.horizontalMapping, mapping.mapping, mapping.zoom);
		const auto grip = ResolveBarBottomDockGripHitTestPointFromRigid(rigid.logicalX, rigid.logicalY,
			mapping.horizontalMapping, mapping.mapping, mapping.zoom);
		Hit result = IsBarBottomDockIndicatorHit(mapping.indicatorVisible && mapping.indicatorOccluding,
			mapping.indicatorBounds, visual.x, visual.y) ? Hit::Blocked : Hit::Miss;
		if (result == Hit::Miss)
			for (const auto& region : presented->regions)
			{
				const auto& point = region.space == TouchRegionSpace::Grip ? grip
					: region.space == TouchRegionSpace::Body ? body : rigid;
				if (HitTestBarButton(region.geometry, std::round(point.logicalX) / mapping.zoom - rebaseX,
					std::round(point.logicalY) / mapping.zoom - rebaseY))
				{
					result = region.disposition;
					break;
				}
			}
		// 几何、可用态与 Rigid/Body/Grip 映射属于同一成功提交；跨帧则重新取值。
		if (BottomDockPresentedSnapshot().serial != mapping.serial) continue;
		return { result, presented->semantic ^ (mapping.displaySerial * 1099511628211ULL) };
	}
}

void BarUISetClass::PreparePresentedButtonHits(const StateModeClass& stateMode)
{
	std::unordered_map<const BarButtonClass*, Inkeys::UI::Bar::BarButtonHitSnapshot> next;
	auto Add = [&](const std::shared_ptr<BarButtonClass>& button, bool more)
	{
		if (!button) return;
		auto hit = Inkeys::UI::Bar::ResolveBarButtonHitSnapshot(*button,
			BarUiInheritClass(button->button.inhX, button->button.inhY));
		hit.visible = hit.visible && !barState.fold && (!more || barState.moreExpanded);
		next.emplace(button.get(), hit);
	};
	for (const auto& button : barButtonSet.buttonList.Snapshot()) Add(button, false);
	const auto more = barButtonSet.GetMoreButtonSnapshot();
	for (const auto& button : more.explicitMore) Add(button, true);
	for (const auto& button : more.forcedOverflow) Add(button, true);
	// 透明 hit area 的 pct=0 是有意设计，沿用原 IsClick 几何语义；可用性仍由交互调用方门控。
	std::unordered_map<const void*,Inkeys::UI::Bar::BarButtonHitSnapshot> visualHits;
	for(const auto& [id,shape]:shapeMap)if(shape)
		visualHits.emplace(shape.get(),Inkeys::UI::Bar::ResolveBarShapeHitSnapshot(*shape,
			BarUiInheritClass(shape->inhX,shape->inhY)));
	for(const auto& [id,shape]:superellipseMap)if(shape)
	{
		Inkeys::UI::Bar::BarButtonHitSnapshot hit;
		hit.boundsDip=D2D1::RectF(static_cast<FLOAT>(shape->inhX),static_cast<FLOAT>(shape->inhY),
			static_cast<FLOAT>(shape->inhX+shape->w.val),static_cast<FLOAT>(shape->inhY+shape->h.val));
		hit.visible=shape->w.val>0 && shape->h.val>0;hit.enabled=true;
		hit.superellipseExponent=shape->n?static_cast<double>(shape->n->val):0.0;
		visualHits.emplace(shape.get(),hit);
	}
	using namespace Inkeys::UI::Bar;
	using Inkeys::Input::UiPointer::Touch::Hit;
	using Shape = BarUISetShapeEnum;
	auto touch = std::make_shared<PresentedTouchRegions>();
	touch->regions.reserve(96);
	// 白名单按业务 stage 排列；可见退场表面只阻挡，不重新准入旧动作。
	auto Append = [&](BarButtonHitSnapshot hit, bool active, TouchRegionSpace space)
	{
		if (!hit.visible) return;
		hit.enabled = true;
		touch->regions.push_back({ hit, space, active ? Hit::Accept : Hit::Blocked });
	};
	auto AddShape = [&](Shape id, bool active, bool surface = false,
		TouchRegionSpace space = TouchRegionSpace::Rigid)
	{
		const auto found = shapeMap.find(id);
		if (found == shapeMap.end() || !found->second) return;
		const auto& shape = *found->second;
		if (surface ? (!shape.enable.val || shape.pct.val <= 0.000001) : !active) return;
		const auto hit = visualHits.find(&shape);
		if (hit != visualHits.end()) Append(hit->second, active, space);
	};
	auto AddShapes = [&](std::initializer_list<Shape> ids, bool active)
		{ for (auto id : ids) AddShape(id, active); };
	auto AddPanel = [&](const auto& rect, double scale, bool visible, bool active)
	{
		if (!visible || rect.Width() <= 0.0 || rect.Height() <= 0.0) return;
		BarButtonHitSnapshot hit;
		hit.boundsDip = D2D1::RectF(static_cast<FLOAT>(rect.left), static_cast<FLOAT>(rect.top),
			static_cast<FLOAT>(rect.right), static_cast<FLOAT>(rect.bottom));
		hit.visible = true; hit.radiusX = hit.radiusY = BarMainBarCornerRadiusDip * scale;
		Append(hit, active, TouchRegionSpace::Rigid);
	};
	auto SurfaceVisible = [&](Shape id)
	{
		const auto found = shapeMap.find(id);
		return found != shapeMap.end() && found->second && found->second->enable.val
			&& found->second->pct.val > 0.000001 && found->second->w.val > 0.0 && found->second->h.val > 0.0;
	};
	const bool fold = barState.fold;
	// open 可能早于动画首帧：透明 hit 区只在所属表面已经实际呈现后准入。
	const bool draw = barState.drawAttribute && !fold && SurfaceVisible(Shape::DrawAttributeBar);
	const bool pen = stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
	const bool geometry = barState.geometryAttribute && stateMode.StateModeSelect == StateModeSelectEnum::IdtShape
		&& SurfaceVisible(Shape::GeometryAttributeBar);
	const bool eraser = barState.eraserAttribute, recall = barState.recallAttribute;
	const bool moreOpen = barState.moreExpanded && !fold && SurfaceVisible(Shape::MorePanel);
	const bool eraserMenu = barState.eraserSensitivityOpen;
	const auto& attribute = barState.drawAttributeBar;
	const auto view = static_cast<ThicknessViewMode>(attribute.thicknessViewMode);
	const bool picker = pen && draw && attribute.colorPickerOpen && SurfaceVisible(Shape::DrawAttributeBar_ColorPickerPanel);
	const bool menu = draw && attribute.penTypeMenuOpen && SurfaceVisible(Shape::DrawAttributeBar_PenTypeMenu);
	const bool annotation = menu && !stateMode.laserActive && PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
	const bool overflow = draw && view != ThicknessViewMode::FineDial && attribute.thicknessOverflowHintPresent;
	const bool annotationPinned = attribute.thicknessAnnotationPinned, overflowPinned = attribute.thicknessOverflowPinned;
	const bool annotationPopup = annotation && (annotationPinned || attribute.thicknessAnnotationHover)
		&& SurfaceVisible(Shape::DrawAttributeBar_ThicknessAnnotationPopup);
	const bool overflowPopup = overflow && (overflowPinned || attribute.thicknessOverflowHover)
		&& SurfaceVisible(Shape::DrawAttributeBar_ThicknessOverflowPopup);
	const bool slider = pen && draw && !stateMode.laserActive
		&& GetBarThicknessSliderRange(stateMode.Pen.ModeSelect, barStyle.dpiZoom, stateMode).supported;
	const bool rangeTransition = attribute.thicknessFineDialRangeTransitionActive;
	const auto& eraserFrame = eraserAttribute.frame_;
	AddPanel(eraserFrame.geometry.menu, eraserFrame.menuPose.scale, eraserFrame.menuVisible,
		eraser && eraserMenu);
	AddPanel(eraserFrame.geometry.panel, eraserFrame.panelPose.scale, eraserFrame.panelVisible, eraser);
	const auto& recallFrame = recallAttribute.frame_;
	AddPanel(recallFrame.geometry.panel, recallFrame.panelPose.scale, recallFrame.panelVisible, recall);

	AddShape(Shape::DrawAttributeBar_ColorPickerPanel, picker, true);
	AddShapes({ Shape::DrawAttributeBar_ColorPickerPalette, Shape::DrawAttributeBar_ColorPickerToneToggle,
		Shape::DrawAttributeBar_ColorPickerCloseHit }, picker);
	AddShape(Shape::DrawAttributeBar_ThicknessAnnotationPopup, annotationPopup, true);
	AddShape(Shape::DrawAttributeBar_ThicknessAnnotationPopupCloseHit, annotationPopup && annotationPinned);
	AddShape(Shape::DrawAttributeBar_ThicknessOverflowPopup, overflowPopup, true);
	AddShape(Shape::DrawAttributeBar_ThicknessOverflowPopupCloseHit, overflowPopup && overflowPinned);
	AddShape(Shape::DrawAttributeBar_ThicknessAnnotationInfoHit, annotation);
	AddShape(Shape::DrawAttributeBar_ThicknessOverflowInfoHit, overflow);
	AddShape(Shape::DrawAttributeBar_PenTypeMenu, menu, true);
	AddShapes({ Shape::DrawAttributeBar_PenTypeMenuFreeLine, Shape::DrawAttributeBar_PenTypeMenuAnnotationLine }, menu);

	AddShape(Shape::MorePanel, moreOpen, true);
	AddShape(Shape::MorePanelCloseHit, moreOpen);
	auto AddButton = [&](const std::shared_ptr<BarButtonClass>& button, bool active, TouchRegionSpace space)
	{
		if (!button) return;
		const auto hit = next.find(button.get());
		if (hit != next.end()) Append(hit->second, active && hit->second.enabled, space);
	};
	for (const auto& button : more.explicitMore) AddButton(button, moreOpen, TouchRegionSpace::Rigid);
	for (const auto& button : more.forcedOverflow) AddButton(button, moreOpen, TouchRegionSpace::Rigid);
	const auto main = superellipseMap.find(BarUISetSuperellipseEnum::MainButton);
	if (main != superellipseMap.end() && main->second && main->second->enable.val && main->second->pct.val > 0.000001)
	{
		const auto hit = visualHits.find(main->second.get());
		if (hit != visualHits.end()) Append(hit->second, true, TouchRegionSpace::Grip);
	}
	for (const auto& button : barButtonSet.buttonList.Snapshot()) AddButton(button, !fold, TouchRegionSpace::Body);
	AddShape(Shape::MainBar, !fold, true, TouchRegionSpace::Body);

	AddShape(Shape::GeometryAttributeBar, geometry, true);
	AddShapes({ Shape::GeometryAttributeBar_StraightLine, Shape::GeometryAttributeBar_Rectangle,
		Shape::GeometryAttributeBar_ThicknessFine, Shape::GeometryAttributeBar_ThicknessMedium,
		Shape::GeometryAttributeBar_ThicknessCoarse, Shape::GeometryAttributeBar_Close }, geometry);
	const bool popup = slider && (view == ThicknessViewMode::Slider || view == ThicknessViewMode::FineDial);
	AddShape(Shape::DrawAttributeBar_ThicknessPreviewPopupSurface, popup && !(view == ThicknessViewMode::FineDial && rangeTransition), true);
	if (slider && view == ThicknessViewMode::Slider)
	{
		const auto panel = shapeMap.find(Shape::DrawAttributeBar);
		const auto region = shapeMap.find(Shape::DrawAttributeBar_ThicknessSelect);
		const auto adjust = shapeMap.find(Shape::DrawAttributeBar_ThicknessAdjust);
		const auto thumb = shapeMap.find(Shape::DrawAttributeBar_ThicknessSliderThumb);
		if (panel != shapeMap.end() && panel->second && region != shapeMap.end() && region->second
			&& adjust != shapeMap.end() && adjust->second)
		{
			const auto preview = CalculateBarThicknessPreviewGeometry(*panel->second, *region->second,
				BarUiInheritClass(region->second->inhX, region->second->inhY), *adjust->second,
				BarUiInheritClass(adjust->second->inhX, adjust->second->inhY));
			double centerY = preview.sliderCenterY;
			const bool thumbReady = thumb != shapeMap.end() && thumb->second
				&& thumb->second->w.val > 0.0 && thumb->second->h.val > 0.0;
			if (thumbReady) centerY = thumb->second->inhY + thumb->second->h.val / 2.0;
			const auto dial = CalculateBarThicknessFineDialGeometry(preview, centerY,
				panel->second->inhY, panel->second->inhY + panel->second->h.val);
			if (dial.valid)
			{
				BarButtonHitSnapshot hit;
				hit.visible = true;
				// 合法透明走廊按原几何消费；thumb 未完成时也不能穿到后方控件。
				if (thumbReady && thumb->second->pct.val >= 0.999999)
				{
					hit.boundsDip = dial.dragZone; Append(hit, true, TouchRegionSpace::Rigid);
				}
				hit.boundsDip = dial.ownershipCorridor; Append(hit, false, TouchRegionSpace::Rigid);
			}
		}
	}
	AddShape(Shape::DrawAttributeBar_ThicknessSliderHit, slider && !(view == ThicknessViewMode::FineDial && rangeTransition));
	AddShape(Shape::DrawAttributeBar, draw && pen, true);
	for (int i = static_cast<int>(Shape::DrawAttributeBar_ColorSelect1); i <= static_cast<int>(Shape::DrawAttributeBar_ColorSelect12); ++i)
		AddShape(static_cast<Shape>(i), draw && pen);
	AddShapes({ Shape::DrawAttributeBar_Brush1, Shape::DrawAttributeBar_Highlight1,
		Shape::DrawAttributeBar_Laser, Shape::DrawAttributeBar_Brush2, Shape::DrawAttributeBar_SoftPen }, draw);
	const bool presets = pen && draw && (IsLaserThicknessPresetMode(stateMode)
		|| PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect, stateMode));
	AddShapes({ Shape::DrawAttributeBar_ThicknessFine, Shape::DrawAttributeBar_ThicknessMedium,
		Shape::DrawAttributeBar_ThicknessCoarse }, presets);
	AddShape(Shape::DrawAttributeBar_ThicknessAdjust, presets && !stateMode.laserActive);
	AddShape(Shape::DrawAttributeBar_PenTypeExtensionHit,
		draw && !stateMode.laserActive && PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect));

	// 版本只描述准入语义，不随每帧动画/颜色/粗细变化，防止按下复核被重绘打断。
	touch->semantic = 1469598103934665603ULL;
	auto Mix = [&](std::uint64_t value) { touch->semantic = (touch->semantic ^ value) * 1099511628211ULL; };
	Mix(static_cast<std::uint64_t>(stateMode.StateModeSelect)); Mix(static_cast<std::uint64_t>(stateMode.Pen.ModeSelect));
	Mix(stateMode.laserActive); Mix(fold); Mix(draw); Mix(geometry); Mix(eraser); Mix(recall); Mix(moreOpen);
	Mix(picker); Mix(menu); Mix(annotationPopup); Mix(overflowPopup); Mix(slider); Mix(rangeTransition);
	Mix(static_cast<std::uint64_t>(view)); Mix(eraserMenu); Mix(annotationPinned); Mix(overflowPinned);
	candidateTouchRegions_ = std::move(touch);
	// 分配和几何整理在 seqlock 奇数区之外完成，提交段只交换已经构造好的表。
	candidateButtonHits_=std::move(next);candidateVisualHits_=std::move(visualHits);
}

void BarUISetClass::PublishPresentedButtonHits()
{
	std::lock_guard lock(presentedButtonHitsMutex_);
	presentedButtonHits_.swap(candidateButtonHits_);presentedVisualHits_.swap(candidateVisualHits_);
	presentedTouchRegions_.swap(candidateTouchRegions_);
	presentedTouchRebaseXDip_ = presentedTouchRebaseYDip_ = 0.0;
}

namespace Inkeys::UI::Bar
{
	int RunBarTouchProbeProductionTest() noexcept
	{
		using Inkeys::Input::UiPointer::Touch::Hit;
		int failures = 0;
		const auto check = [&](bool passed, const char* name)
			{ if (!passed) { ++failures; std::fprintf(stderr, "[UI3TouchProbe] FAIL %s\n", name); } };
		try
		{
			// 局部 fixture 只开放真实候选/提交入口，不启动窗口、设备或全局 Bar。
			struct Fixture final : BarUISetClass
			{
				using BarUISetClass::PreparePresentedButtonHits;
				void Mapping()
				{
					presentedMonitorOriginX = 1000; presentedMonitorOriginY = -400;
					bottomDockPresentedDirectTranslationX = 30; bottomDockPresentedDirectTranslationY = 50;
					bottomDockPresentedZoom = 2.0; bottomDockPresentedDisplaySerial = 7;
					bottomDockPresentedBaseLeftDip = 0.0; bottomDockPresentedBaseRightDip = 400.0;
					bottomDockPresentedVisualLeftDip = 100.0; bottomDockPresentedVisualRightDip = 900.0;
					bottomDockPresentedScaleX = 2.0; bottomDockPresentedRigidGripTranslationXDip = 600.0;
					bottomDockPresentedRigidTranslationXDip = 1000.0;
					bottomDockPresentedBaseTopDip = 0.0; bottomDockPresentedBaseBottomDip = 300.0;
					bottomDockPresentedVisualTopDip = 50.0; bottomDockPresentedVisualBottomDip = 500.0;
					bottomDockPresentedScaleY = 1.5; bottomDockPresentedRigidTranslationDip = 400.0;
				}
				void PublishPrepared()
				{
					bottomDockPresentedMappingSerial.fetch_add(1, std::memory_order_acq_rel);
					PublishPresentedButtonHits();
					bottomDockPresentedMappingSerial.fetch_add(1, std::memory_order_release);
				}
				void Commit(const StateModeClass& state)
					{ PreparePresentedButtonHits(state); PublishPrepared(); }
			};
			auto owner = std::make_unique<Fixture>(); owner->Mapping();
			const auto shape = [](double left, double top, double width, double height, double radius, double opacity)
			{
				auto result = std::make_shared<BarUiShapeClass>(0.0, 0.0, width, height,
					radius, radius, std::nullopt, RGB(80, 80, 80), std::nullopt);
				result->inhX = left; result->inhY = top;
				result->enable.Initialization(true); result->pct.Initialization(opacity);
				return result;
			};
			owner->shapeMap[BarUISetShapeEnum::MainBar] = shape(60, 0, 100, 40, 8, 0.8);
			auto draw = shape(0, 80, 370, 190, 8, 0.8);
			owner->shapeMap[BarUISetShapeEnum::DrawAttributeBar] = draw;
			owner->shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1] = shape(10, 90, 30, 30, 4, 1.0);
			owner->shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit] = shape(700, 80, 40, 40, 4, 0.0);
			owner->shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble] = shape(700, 80, 40, 40, 4, 1.0);
			auto grip = std::make_shared<BarUiSuperellipseClass>(0.0, 0.0, 40.0, 40.0,
				4.0, std::nullopt, RGB(80, 80, 80), std::nullopt);
			grip->inhX = 0.0; grip->inhY = 0.0; grip->enable.Initialization(true); grip->pct.Initialization(1.0);
			owner->superellipseMap[BarUISetSuperellipseEnum::MainButton] = grip;
			auto button = std::make_shared<BarButtonClass>(); button->hide = false;
			button->button.Initialization(0, 0, 40, 30, 4, 4, std::nullopt, RGB(80, 80, 80), std::nullopt);
			button->button.enable.Initialization(true); button->button.inhX = 90; button->button.inhY = 5;
			owner->barButtonSet.buttonList.Replace({ button });
			owner->barState.drawAttribute = true;
			StateModeClass state; state.StateModeSelect = StateModeSelectEnum::IdtPen;
			state.Pen.ModeSelect = PenModeSelectEnum::IdtPenSoftPen;
			// 明确的屏幕样本同时区分 Rigid/Body/Grip、非单位 zoom、负 monitor origin 和直移。
			const POINT bodyPoint{ 1670, -190 }, gripPoint{ 2270, -190 }, drawPoint{ 3230, 750 };
			const auto hit = [&](POINT point) { return owner->ProbeTouchScreen(point).hit; };
			check(hit(bodyPoint) == Hit::Miss && hit(gripPoint) == Hit::Miss && hit(drawPoint) == Hit::Miss,
				"before-first-present-rejected");
			owner->PreparePresentedButtonHits(state);
			check(hit(drawPoint) == Hit::Miss, "prepared-without-publish-is-not-presented");
			owner->PublishPrepared();
			check(hit(bodyPoint) == Hit::Accept, "main-button-body-mapping");
			check(hit({ 1510, -190 }) == Hit::Accept, "main-background-body-mapping");
			check(hit(gripPoint) == Hit::Accept, "main-superellipse-grip-mapping");
			check(hit(drawPoint) == Hit::Accept, "draw-surface-rigid-mapping");
			check(hit({ 1470, -250 }) == Hit::Miss && hit({ 2230, -250 }) == Hit::Miss,
				"rounded-main-and-superellipse-corners-miss");
			check(hit({ 3030, 610 }) == Hit::Miss && hit({ 3830, 750 }) == Hit::Miss,
				"rounded-draw-corner-and-exterior-miss");
			check(hit({ 4470, 650 }) == Hit::Miss, "closed-transparent-hit-and-preview-decoration-excluded");
			button->state->state = BarWidgetState::Disable; owner->Commit(state);
			check(hit(bodyPoint) == Hit::Blocked, "disabled-button-blocks-enabled-background");
			button->state->state = BarWidgetState::None;
			draw->pct.SetDirect(0.0); owner->Commit(state);
			check(hit(drawPoint) == Hit::Miss && hit({ 3080, 660 }) == Hit::Miss,
				"open-before-visible-surface-does-not-revive-old-hit");
			draw->pct.SetDirect(0.8); owner->Commit(state);
			const auto semantic = owner->ProbeTouchScreen(drawPoint).semantic;
			state.Pen.Brush1.color = RGB(1, 2, 3); state.Pen.Brush1.width = 11.0f; owner->Commit(state);
			check(owner->ProbeTouchScreen(drawPoint).semantic == semantic, "style-change-keeps-admission-semantic");
			draw->inhX = 1000.0; owner->barState.drawAttribute = false;
			const POINT movedDrawPoint{ 5230, 750 };
			check(hit(drawPoint) == Hit::Accept && hit(movedDrawPoint) == Hit::Miss,
				"probe-does-not-read-live-animation-or-open-state");
			owner->PreparePresentedButtonHits(state); // 模拟呈现失败：只准备候选，禁止发布。
			check(hit(drawPoint) == Hit::Accept && hit(movedDrawPoint) == Hit::Miss
				&& owner->ProbeTouchScreen(drawPoint).semantic == semantic, "failed-candidate-keeps-presented-snapshot");
			owner->PublishPrepared();
			check(hit(drawPoint) == Hit::Miss && hit(movedDrawPoint) == Hit::Blocked,
				"visible-retiring-draw-blocks-with-new-committed-geometry");
			owner->barState.fold = true; owner->Commit(state);
			check(hit(gripPoint) == Hit::Accept && hit(bodyPoint) == Hit::Blocked,
				"folded-grip-stays-active-while-body-retires");
			draw->pct.SetDirect(0.0); owner->Commit(state);
			check(hit(movedDrawPoint) == Hit::Miss, "fully-retired-draw-no-longer-occupies");
		}
		catch (...) { ++failures; std::fputs("[UI3TouchProbe] FAIL exception\n", stderr); }
		std::fprintf(stderr, "[UI3TouchProbe] %s: production prepare/publish/probe geometry and admission\n", failures ? "FAIL" : "PASS");
		return failures ? 1 : 0;
	}
}
