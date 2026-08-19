module;

#include "../../../IdtMain.h"

// 历史遗留问题
#include "../../../IdtDraw.h"
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"

#include "../../../IdtConfiguration.h"
#include "../../Window/Window.Legacy.hpp"

#include <unordered_map>
#include <unordered_set>

module Inkeys.UI.Bar;
import :Button;

import :Main;
import :Theme;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Other.Config;
import Inkeys.Business.ComponentActions;
import Inkeys.UI.Setting;

using Inkeys::Business::BuiltInComponentAction;
using Inkeys::Business::ExecuteBuiltInComponentAction;
using Inkeys::UI::Bar::BarToggleChannel;

bool BarButtonClass::TransitionContent(
	const wstring& iconResourceName, const wstring& label)
{
	bool changed = false;
	if (iconKind == BarButtonIconKindEnum::Svg && !iconResourceName.empty())
		changed |= icon.TransitionToResource(L"UI", iconResourceName);
	changed |= name.TransitionToString(label);
	return changed;
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
	const COLORREF defaultIconColor = GetThemeColor(BarThemeColorEnum::TextPrimary);

	// 分隔线
	{
		BarButtonClass* obj = new BarButtonClass;
		const COLORREF dividerColor = GetThemeColor(BarThemeColorEnum::SurfaceFrame);
		{
			obj->size = BarButtonSizeEnum::oneTwo;
			obj->preset = BarButtonPresetEnum::Divider;
			obj->hide = false;

			obj->only = false; // 允许多个分隔线
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"分割线", 0.0);
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"选择", 0.0);
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
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection)
						ChangeStateModeToSelection();
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"绘制", 0.0);
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
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen)
					{
						if (ChangeStateModeToPen())
						{
							barUISet.barState.drawAttribute = false;
							barUISet.barState.geometryAttribute = false;
						}
					}
					else
					{
						if (stateMode.laserActive)
						{
							stateMode.laserActive = false;
							SyncDraw3State();
							this->UpdateDrawButtonStyle();
						}
						if (!barUISet.TryBeginToggle(
							BarToggleChannel::DrawAttribute)) return;
						if (barUISet.barState.drawAttribute) barUISet.barState.drawAttribute = false;
						else
						{
							barUISet.barState.moreExpanded = false;
							barUISet.barState.geometryAttribute = false;
							barUISet.barState.drawAttribute = true;
						}
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"擦除", 0.0);
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
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtEraser)
						ChangeStateModeToEraser();
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"几何", 0.0);
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
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtShape)
					{
						// 首次只切换工具；和绘制属性一致，再次点击才展开面板。
						if (ChangeStateModeToShape())
						{
							barUISet.barState.drawAttribute = false;
							barUISet.barState.geometryAttribute = false;
						}
					}
					else
					{
						if (!barUISet.TryBeginToggle(
							BarToggleChannel::GeometryAttribute)) return;
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"撤回", 0.0);
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
					(void)Inkeys::Drawing::Draw3::PublishProductCommand(
						Inkeys::Drawing::Draw3::Bridge::CommandType::Undo);

					// TODO 撤回库重做后需要试试检测撤回状态，要支持按键变灰
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// TODO 重做（这个会成为撤回的子窗口）
	// 清空
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = BarButtonSizeEnum::twoTwo;
			obj->preset = BarButtonPresetEnum::Clean;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"清空", 0.0);
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
					(void)Inkeys::Drawing::Draw3::PublishProductCommand(
						Inkeys::Drawing::Draw3::Bridge::CommandType::Clear);
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"白板", 0.0);
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
					RequestWhiteboardActive(!WhiteboardRequested());
				};
		}

		obj->state = &barButtonState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 定格
	{
		BarButtonClass* obj = new BarButtonClass;
		{
			obj->size = BarButtonSizeEnum::twoOne;
			obj->preset = BarButtonPresetEnum::Freeze;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"定格", 0.0);
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
					// TODO 注意 PptInfoState.TotalPage == -1 时需要禁用按钮
					if (FreezeFrame.mode != 1)
					{
						FreezeFrame.mode = 1;
						SyncDraw3State();

						if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) FreezeFrame.select = true;
					}
					else
					{
						FreezeFrame.mode = 0;
						FreezeFrame.select = false;
						SyncDraw3State();
					}
				};
		}

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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"设置", 0.0);
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
	moreButton->name.Initialization(0.0, 0.0, 0.0, 0.0, L"更多", 0.0);
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
	const COLORREF defaultButtonFill = GetThemeColor(BarThemeColorEnum::Surface);
	const COLORREF defaultIconColor = GetThemeColor(BarThemeColorEnum::TextPrimary);

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

		obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
		obj->icon.enable.Initialization(true);
		obj->pngIcon.Initialization(0.0, 0.0);
		if (!obj->pngIcon.InitializationFromResource(L"PNG", iconResource))
		{
			delete obj;
			return;
		}
		obj->pngIcon.enable.Initialization(true);
		// 复用 SVG 的动画状态，但为 PNG 提供真实宽高，避免 SetWH 无法按比例计算。
		obj->icon.rW = obj->pngIcon.rW;
		obj->icon.rH = obj->pngIcon.rH;
		obj->iconKind = BarButtonIconKindEnum::Png;
		obj->clickFunc = [action]() { ExecuteBuiltInComponentAction(action); };

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
		L"软件", L"启动 文件资源管理器", L"文件管理器", L"CustomizeIco4",
		BuiltInComponentAction::Explorer);
	registerComponent(
		"Component.ShortcutButton.Appliance.Taskmgr",
		"component.shortcutButton.appliance.taskmgr",
		[] { return setlist.component.shortcutButton.appliance.taskmgr; },
		L"软件", L"启动 任务管理器", L"任务管理器", L"CustomizeIco9",
		BuiltInComponentAction::TaskManager);
	registerComponent(
		"Component.ShortcutButton.Appliance.Control",
		"component.shortcutButton.appliance.control",
		[] { return setlist.component.shortcutButton.appliance.control; },
		L"软件", L"启动 控制面板", L"控制面板", L"CustomizeIco7",
		BuiltInComponentAction::ControlPanel);

	registerComponent(
		"Component.ShortcutButton.System.Desktop",
		"component.shortcutButton.system.desktop",
		[] { return setlist.component.shortcutButton.system.desktop; },
		L"系统", L"显示桌面", L"显示桌面", L"CustomizeIco3",
		BuiltInComponentAction::ShowDesktop);
	registerComponent(
		"Component.ShortcutButton.System.LockWorkStation",
		"component.shortcutButton.system.lockWorkStation",
		[] { return setlist.component.shortcutButton.system.lockWorkStation; },
		L"系统", L"锁屏", L"锁屏", L"CustomizeIco8",
		BuiltInComponentAction::LockWorkStation);

	registerComponent(
		"Component.ShortcutButton.Keyboard.Keyboardesc",
		"component.shortcutButton.keyboard.keyboardesc",
		[] { return setlist.component.shortcutButton.keyboard.keyboardesc; },
		L"键盘", L"ESC 键", L"ESC键", L"CustomizeIco5",
		BuiltInComponentAction::Escape);
	registerComponent(
		"Component.ShortcutButton.Keyboard.KeyboardAltF4",
		"component.shortcutButton.keyboard.keyboardAltF4",
		[] { return setlist.component.shortcutButton.keyboard.keyboardAltF4; },
		L"键盘", L"Alt+F4", L"Alt+F4", L"CustomizeIco6",
		BuiltInComponentAction::AltF4);

	registerComponent(
		"Component.ShortcutButton.RollCall.IslandCaller1",
		"component.shortcutButton.rollCall.IslandCaller1",
		[] { return setlist.component.shortcutButton.rollCall.IslandCaller1; },
		L"随机点名", L"IslandCaller 1", L"随机点名", L"CustomizeIco2",
		BuiltInComponentAction::IslandCaller);
	registerComponent(
		"Component.ShortcutButton.RollCall.IslandCaller2",
		"component.shortcutButton.rollCall.IslandCaller2",
		[] { return setlist.component.shortcutButton.rollCall.IslandCaller2; },
		L"随机点名", L"IslandCaller 2", L"随机点名", L"CustomizeIco2",
		BuiltInComponentAction::IslandCallerSimple);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom1",
		"component.shortcutButton.rollCall.SecRandom1",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom1; },
		L"随机点名", L"SecRandom 1", L"随机点名", L"CustomizeIco10",
		BuiltInComponentAction::SecRandomDirect);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom2",
		"component.shortcutButton.rollCall.SecRandom2",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom2; },
		L"随机点名", L"SecRandom 2", L"随机点名", L"CustomizeIco10",
		BuiltInComponentAction::SecRandomQuickDraw);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom2Compat",
		"component.shortcutButton.rollCall.SecRandom2Compat",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom2Compat; },
		L"随机点名", L"SecRandom 2 兼容", L"随机点名", L"CustomizeIco10",
		BuiltInComponentAction::SecRandomQuickDrawCompat);
	registerComponent(
		"Component.ShortcutButton.RollCall.NamePicker",
		"component.shortcutButton.rollCall.NamePicker",
		[] { return setlist.component.shortcutButton.rollCall.NamePicker; },
		L"随机点名", L"NamePicker", L"随机点名", L"CustomizeIco11",
		BuiltInComponentAction::NamePicker);

	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandSettings",
		"component.shortcutButton.linkage.classislandSettings",
		[] { return setlist.component.shortcutButton.linkage.classislandSettings; },
		L"联动", L"ClassIsland 设置", L"CI设置", L"CustomizeIco1",
		BuiltInComponentAction::ClassIslandSettings);
	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandProfile",
		"component.shortcutButton.linkage.classislandProfile",
		[] { return setlist.component.shortcutButton.linkage.classislandProfile; },
		L"联动", L"档案编辑", L"档案编辑", L"CustomizeIco1",
		BuiltInComponentAction::ClassIslandProfile);
	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandClassswap",
		"component.shortcutButton.linkage.classislandClassswap",
		[] { return setlist.component.shortcutButton.linkage.classislandClassswap; },
		L"联动", L"快速换课", L"快速换课", L"CustomizeIco1",
		BuiltInComponentAction::ClassIslandClassSwap);
}

void BarButtonSetClass::StateUpdate()
{
	static int lastToolStateKey = -1;
	const int toolStateKey =
		(static_cast<int>(stateMode.StateModeSelect) << 8)
		| (static_cast<int>(stateMode.Pen.ModeSelect) << 4)
		| (static_cast<int>(stateMode.Shape.ModeSelect) << 1)
		| (stateMode.laserActive ? 1 : 0);
	if (lastToolStateKey != toolStateKey)
	{
		// 工具、笔型或激光状态切换时只保留主栏，避免旧属性浮层残留。
		barUISet.CollapseAuxiliaryPanels(true);
		lastToolStateKey = toolStateKey;
	}
	CalcState();
	PresetHoming();
	UpdateDrawButtonStyle();
	UpdateWhiteboardButtonStyle();
	UpdateEraserButtonStyle();
	UpdateGeometryButtonStyle();
}
void BarButtonSetClass::UpdateDrawButtonStyle()
{
	static mutex mtx;
	bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
	bool laser = selected && stateMode.laserActive;
	bool highlighter =
		!laser && stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1;
	int styleKey = (selected ? 2 : 0) + (highlighter ? 1 : 0) + (laser ? 4 : 0);
	if (drawButtonStyleKey == styleKey) return;

	lock_guard<mutex> lock(mtx);
	if (drawButtonStyleKey == styleKey) return;
	auto button = preset[(int)BarButtonPresetEnum::Draw];
	if (!button) return;
	button->TransitionContent(
		laser ? L"barLaser" : (highlighter ? L"barHighlighter1" : L"barBrush1"),
		selected ? (laser ? L"激光笔" : (highlighter ? L"荧光笔" : L"硬笔")) : L"绘制");
	drawButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateWhiteboardButtonStyle()
{
	static mutex mtx;
	const bool active = Inkeys::UI::Bar::WhiteboardActive();
	const int styleKey = active ? 1 : 0;
	if (whiteboardButtonStyleKey == styleKey) return;

	lock_guard<mutex> lock(mtx);
	if (whiteboardButtonStyleKey == styleKey) return;
	auto whiteboard = preset[(int)BarButtonPresetEnum::Whiteboard];
	auto freeze = preset[(int)BarButtonPresetEnum::Freeze];
	if (!whiteboard || !freeze) return;
	whiteboard->hide = false;
	whiteboard->size = active
		? BarButtonSizeEnum::twoTwo : BarButtonSizeEnum::twoOne;
	freeze->size = BarButtonSizeEnum::twoOne;
	freeze->hide = active;
	whiteboard->TransitionContent(active ? L"barDismiss" : L"barWhiteboard",
		active ? L"关闭白板" : L"白板");
	whiteboardButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateEraserButtonStyle()
{
	static mutex mtx;
	bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser;
	int styleKey = selected ? 1 : 0;
	if (eraserButtonStyleKey == styleKey) return;

	lock_guard<mutex> lock(mtx);
	if (eraserButtonStyleKey == styleKey) return;
	auto button = preset[(int)BarButtonPresetEnum::Eraser];
	if (!button) return;
	button->TransitionContent(
		L"barEraser", selected ? L"面积擦" : L"擦除");
	eraserButtonStyleKey = styleKey;
}
void BarButtonSetClass::UpdateGeometryButtonStyle()
{
	static mutex mtx;
	bool selected = stateMode.StateModeSelect == StateModeSelectEnum::IdtShape;
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
	const wchar_t* label = !selected ? L"几何" : (rectangle ? L"矩形" : L"直线");
	button->TransitionContent(resourceName, label);
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
		registration.button->userVisible = registration.defaultUserVisible;
		registration.button->size = ToRuntimeSize(entry.Size);
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
		registration->second.button->userVisible = true;
		registration->second.button->hide = false;
		registration->second.button->size = registration->second.defaultSize;
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
	boundaryDividers[boundaryIndex]->userVisible = true;
	boundaryDividers[boundaryIndex]->hide = false;
	boundaryDividers[boundaryIndex]->size = registration.defaultSize;
	activeButtons.push_back(boundaryDividers[boundaryIndex]);
}

void BarButtonSetClass::Load()
{
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
		settingRegistration.button->userVisible = true;
		settingRegistration.button->hide = false;
		settingRegistration.button->size = settingRegistration.defaultSize;
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

	// 先在列表锁内替换整段序列，再发布数量；注册表和边界实例继续持有对象所有权。
	const int activeButtonCount = static_cast<int>(activeButtons.size());
	buttonList.Replace(move(activeButtons));
	tot = activeButtonCount;

	// 只规范化 A1/A2；运行时投影不读取、修改或写回持久化 B 区。
	Inkeys::config.UI.Bar.FixedButtonsA1.Replace(std::move(normalizedA1));
	Inkeys::config.UI.Bar.FixedButtonsA2.Replace(std::move(normalizedA2));
	if (fixedLayoutMigrated)
		(void)Inkeys::config.Write(); // 旧 {Pierce, Freeze} A2 规范化后立即写回。
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

void BarButtonSetClass::PresetHoming()
{
	const bool whiteboard = Inkeys::UI::Bar::WhiteboardActive();
	if (whiteboard) barUISet.barState.geometryAttribute = false;
	if (!whiteboard && (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen
		|| barUISet.barState.fold
		|| !preset[(int)BarButtonPresetEnum::Draw]->IsVisible()))
	{
		barUISet.barState.drawAttribute = false;
	}
	if (!whiteboard && (stateMode.StateModeSelect != StateModeSelectEnum::IdtShape
		|| barUISet.barState.fold
		|| !preset[(int)BarButtonPresetEnum::Geometry]->IsVisible()))
	{
		barUISet.barState.geometryAttribute = false;
	}
	if (barUISet.barState.fold) barUISet.barState.moreExpanded = false;

	// 只有“选择 + 空页”使用精简布局；有历史内容时保留完整绘制按钮。
	if (!whiteboard
		&& stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection
		&& !Inkeys::UI::Bar::CurrentPageHasContent())
	{
		// 显示状态变化
		preset[(int)BarButtonPresetEnum::Eraser]->hide = true;
		preset[(int)BarButtonPresetEnum::Geometry]->hide = true;
		preset[(int)BarButtonPresetEnum::Recall]->hide = true;
		//preset[(int)BarButtonPresetEnum::Redo]->hide = true;
		// preset[(int)BarButtonPresetEnum::Clean]->hide = true;

		// 显示名称变化也走通用内容过渡，避免直接替换产生闪变。
		preset[(int)BarButtonPresetEnum::Select]->TransitionContent(
			L"barSelect", whiteboard ? L"拖动" : L"选择");
	}
	else
	{
		// 显示状态变化
		preset[(int)BarButtonPresetEnum::Eraser]->hide = false;
		preset[(int)BarButtonPresetEnum::Geometry]->hide = false;
		preset[(int)BarButtonPresetEnum::Recall]->hide = false;
		//preset[(int)BarButtonPresetEnum::Redo]->hide = false;
		// preset[(int)BarButtonPresetEnum::Clean]->hide = false;
		// 选择按钮不再承载清空语义。
		preset[(int)BarButtonPresetEnum::Select]->TransitionContent(
			L"barSelect", whiteboard ? L"拖动" : L"选择");
	}
}
void BarButtonSetClass::CalcState()
{
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) barButtonState[(int)BarButtonPresetEnum::Select].state = BarWidgetState::Selected;
		else barButtonState[(int)BarButtonPresetEnum::Select].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen) barButtonState[(int)BarButtonPresetEnum::Draw].state = BarWidgetState::Selected;
		else barButtonState[(int)BarButtonPresetEnum::Draw].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser) barButtonState[(int)BarButtonPresetEnum::Eraser].state = BarWidgetState::Selected;
		else barButtonState[(int)BarButtonPresetEnum::Eraser].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape) barButtonState[(int)BarButtonPresetEnum::Geometry].state = BarWidgetState::Selected;
		else barButtonState[(int)BarButtonPresetEnum::Geometry].state = BarWidgetState::None;
	}

	{
		if (FreezeFrame.mode == 1) barButtonState[(int)BarButtonPresetEnum::Freeze].state = BarWidgetState::Selected;
		else barButtonState[(int)BarButtonPresetEnum::Freeze].state = BarWidgetState::None;
	}
	{
		barButtonState[(int)BarButtonPresetEnum::Whiteboard].state =
			Inkeys::UI::Bar::WhiteboardActive()
			? BarWidgetState::Selected : BarWidgetState::None;
	}

	{
		if (Inkeys::UI::Setting::IsVisible()) barButtonState[(int)BarButtonPresetEnum::Setting].state = BarWidgetState::Selected;
		else barButtonState[(int)BarButtonPresetEnum::Setting].state = BarWidgetState::None;
	}
}
