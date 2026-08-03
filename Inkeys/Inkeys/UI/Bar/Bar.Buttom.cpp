module;

#include "../../../IdtMain.h"

// 历史遗留问题
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtHistoricalDrawpad.h"
#include "../../../IdtImage.h"
#include "../../../IdtFloating.h"
#include "../../../IdtState.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtD2DPreparation.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtWindow.h"

#include <unordered_map>
#include <unordered_set>

module Inkeys.UI.Bar;
import :Bottom;

import :Main;
import :Theme;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Other.Config;

bool BarButtomSetClass::RegisterButton(
	const std::string& id,
	BarButtomClass* button,
	bool allowMultiple,
	BarButtonLayoutZoneEnum zone,
	bool defaultUserVisible,
	const std::string& legacyField,
	function<bool()> legacyEnabled,
	const wstring& categoryName,
	const wstring& settingsName)
{
	if (id.empty() || button == nullptr) return false;

	// 官方固定区必须 Inkeys. 前缀；扩展区必须为非 Inkeys. 的点分 ID。
	if (zone == BarButtonLayoutZoneEnum::FixedA1 || zone == BarButtonLayoutZoneEnum::FixedA2)
	{
		if (!Inkeys::IsOfficialBarButtonIdPrefix(id)) return false;
	}
	else if (!Inkeys::IsExtensionBarButtonId(id))
	{
		return false;
	}

	unique_lock lock(registrationMutex);
	if (registrations.contains(id)) return false;

	button->id = id;
	button->only = !allowMultiple;
	button->userVisible = defaultUserVisible;
	shared_ptr<BarButtomClass> ownedButton(button);
	registrations.emplace(
		id,
		BarButtonRegistrationClass{
			id,
			move(ownedButton),
			allowMultiple,
			zone,
			button->size.load(),
			defaultUserVisible,
			legacyField,
			move(legacyEnabled),
			categoryName,
			settingsName
		});
	registrationOrder.push_back(id);
	return true;
}

bool BarButtomSetClass::TryGetRegistration(const std::string& id, BarButtonRegistrationClass& outRegistration) const
{
	shared_lock lock(registrationMutex);
	auto registration = registrations.find(id);
	if (registration == registrations.end()) return false;

	outRegistration = registration->second;
	return true;
}

vector<BarButtonRegistrationClass> BarButtomSetClass::GetExtensionRegistrations() const
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

void BarButtomSetClass::PresetInitialization()
{
	const COLORREF defaultButtonFill = GetThemeColor(BarThemeColorEnum::Surface);
	const COLORREF defaultIconColor = GetThemeColor(BarThemeColorEnum::TextPrimary);

	// 分隔线
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::oneTwo;
			obj->preset = BarButtomPresetEnum::Divider;
			obj->hide = false;

			obj->only = false; // 允许多个分隔线
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"分割线", 0.0);
			obj->name.enable.Initialization(false);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barDivider");
			obj->icon.enable.Initialization(true);
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 选择
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Select;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"选择", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
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

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 绘制
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Draw;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"绘制", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
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
						if (barUISet.barState.drawAttribute) barUISet.barState.drawAttribute = false;
						else barUISet.barState.drawAttribute = true;

						// 当穿透模式下再次点击绘制按钮，则退出穿透
						if (penetrate.select) penetrate.select = false;
					}
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 擦除
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Eraser;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"擦除", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
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

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 几何
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Geometry;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"几何", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barGeometry");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtShape)
					{
						// 只有 Draw2 接受模式切换后才关闭绘制属性并展开几何面板。
						if (ChangeStateModeToShape())
						{
							barUISet.barState.drawAttribute = false;
							barUISet.barState.geometryAttribute = true;
						}
					}
					else barUISet.barState.geometryAttribute =
						!static_cast<bool>(barUISet.barState.geometryAttribute);
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 撤回
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Recall;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"撤回", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barRecall");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					// 额外的检查
					if (!RecallImage.empty() || (!FirstDraw && RecallImagePeak == 0))
					{
						IdtRecall();
					}

					// TODO 撤回库重做后需要试试检测撤回状态，要支持按键变灰
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// TODO 重做（这个会成为撤回的子窗口）
	// 清空
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Clean;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"清空", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barClean");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					// 当穿透模式下只能先退出穿透，再清空（比较糟糕的 inkeys2 架构导致的）
					if (penetrate.select)
					{
						penetrate.select = false;
						if (FreezeFrame.mode == 2) FreezeFrame.mode = 1;
					}
					stateMode.cleanPageSign = true;
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 穿透
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoOne;
			obj->preset = BarButtomPresetEnum::Pierce;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"穿透", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barPierce");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection)
					{
						if (penetrate.select)
						{
							penetrate.select = false;
							if (FreezeFrame.mode == 2) FreezeFrame.mode = 1;
						}
						else
						{
							if (FreezeFrame.mode == 1) FreezeFrame.mode = 2;
							penetrate.select = true;
						}
					}
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
	// 定格
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoOne;
			obj->preset = BarButtomPresetEnum::Freeze;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"定格", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
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
						penetrate.select = false;

						if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) FreezeFrame.select = true;
					}
					else
					{
						FreezeFrame.mode = 0;
						FreezeFrame.select = false;
					}
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 设置
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Setting;
			obj->hide = false;

		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, L"设置", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, defaultIconColor, nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barSetting");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					if (test.select) test.select = false;
					else test.select = true;
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}

	// 官方按钮使用稳定 ID；A1/A2 固定区与扩展区在注册时写死分区和默认显隐。
	RegisterButton(Inkeys::BarButtonId::Select, preset[(int)BarButtomPresetEnum::Select], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Draw, preset[(int)BarButtomPresetEnum::Draw], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Eraser, preset[(int)BarButtomPresetEnum::Eraser], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Geometry, preset[(int)BarButtomPresetEnum::Geometry], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Recall, preset[(int)BarButtomPresetEnum::Recall], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Clean, preset[(int)BarButtomPresetEnum::Clean], false, BarButtonLayoutZoneEnum::FixedA1);
	// Divider 不进 A1 配置 required 集；仅作运行时交界注入模板（可多实例拷贝）。
	RegisterButton(Inkeys::BarButtonId::Divider, preset[(int)BarButtomPresetEnum::Divider], true, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Pierce, preset[(int)BarButtomPresetEnum::Pierce], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::Freeze, preset[(int)BarButtomPresetEnum::Freeze], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::Setting, preset[(int)BarButtomPresetEnum::Setting], false, BarButtonLayoutZoneEnum::FixedA2);

	BarButtonRegistrationClass dividerRegistration;
	if (TryGetRegistration(Inkeys::BarButtonId::Divider, dividerRegistration)
		&& dividerRegistration.button)
	{
		// 两条交界线由集合长期持有，运行时替换列表时不会释放正在绘制的对象。
		boundaryDividers[0] = make_shared<BarButtomClass>(*dividerRegistration.button);
		boundaryDividers[1] = make_shared<BarButtomClass>(*dividerRegistration.button);
	}
}

void BarButtomSetClass::RegisterBuiltInComponents()
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
		InkeysBuiltInComponentAction action)
	{
		BarButtonRegistrationClass existingRegistration;
		if (TryGetRegistration(id, existingRegistration)) return;

		BarButtomClass* obj = new BarButtomClass;
		obj->size = BarButtomSizeEnum::twoTwo;
		obj->hide = false;
		obj->only = true;

		obj->name.Initialization(0.0, 0.0, 0.0, 0.0, shortText, 0.0);
		obj->name.enable.Initialization(true);
		obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, defaultButtonFill, nullopt);
		obj->buttom.enable.Initialization(true);

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
		obj->iconKind = BarButtomIconKindEnum::Png;
		obj->clickFunc = [action]() { ExecuteInkeysBuiltInComponentAction(action); };

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
		InkeysBuiltInComponentAction::Explorer);
	registerComponent(
		"Component.ShortcutButton.Appliance.Taskmgr",
		"component.shortcutButton.appliance.taskmgr",
		[] { return setlist.component.shortcutButton.appliance.taskmgr; },
		L"软件", L"启动 任务管理器", L"任务管理器", L"CustomizeIco9",
		InkeysBuiltInComponentAction::TaskManager);
	registerComponent(
		"Component.ShortcutButton.Appliance.Control",
		"component.shortcutButton.appliance.control",
		[] { return setlist.component.shortcutButton.appliance.control; },
		L"软件", L"启动 控制面板", L"控制面板", L"CustomizeIco7",
		InkeysBuiltInComponentAction::ControlPanel);

	registerComponent(
		"Component.ShortcutButton.System.Desktop",
		"component.shortcutButton.system.desktop",
		[] { return setlist.component.shortcutButton.system.desktop; },
		L"系统", L"显示桌面", L"显示桌面", L"CustomizeIco3",
		InkeysBuiltInComponentAction::ShowDesktop);
	registerComponent(
		"Component.ShortcutButton.System.LockWorkStation",
		"component.shortcutButton.system.lockWorkStation",
		[] { return setlist.component.shortcutButton.system.lockWorkStation; },
		L"系统", L"锁屏", L"锁屏", L"CustomizeIco8",
		InkeysBuiltInComponentAction::LockWorkStation);

	registerComponent(
		"Component.ShortcutButton.Keyboard.Keyboardesc",
		"component.shortcutButton.keyboard.keyboardesc",
		[] { return setlist.component.shortcutButton.keyboard.keyboardesc; },
		L"键盘", L"ESC 键", L"ESC键", L"CustomizeIco5",
		InkeysBuiltInComponentAction::Escape);
	registerComponent(
		"Component.ShortcutButton.Keyboard.KeyboardAltF4",
		"component.shortcutButton.keyboard.keyboardAltF4",
		[] { return setlist.component.shortcutButton.keyboard.keyboardAltF4; },
		L"键盘", L"Alt+F4", L"Alt+F4", L"CustomizeIco6",
		InkeysBuiltInComponentAction::AltF4);

	registerComponent(
		"Component.ShortcutButton.RollCall.IslandCaller1",
		"component.shortcutButton.rollCall.IslandCaller1",
		[] { return setlist.component.shortcutButton.rollCall.IslandCaller1; },
		L"随机点名", L"IslandCaller 1", L"随机点名", L"CustomizeIco2",
		InkeysBuiltInComponentAction::IslandCaller);
	registerComponent(
		"Component.ShortcutButton.RollCall.IslandCaller2",
		"component.shortcutButton.rollCall.IslandCaller2",
		[] { return setlist.component.shortcutButton.rollCall.IslandCaller2; },
		L"随机点名", L"IslandCaller 2", L"随机点名", L"CustomizeIco2",
		InkeysBuiltInComponentAction::IslandCallerSimple);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom1",
		"component.shortcutButton.rollCall.SecRandom1",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom1; },
		L"随机点名", L"SecRandom 1", L"随机点名", L"CustomizeIco10",
		InkeysBuiltInComponentAction::SecRandomDirect);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom2",
		"component.shortcutButton.rollCall.SecRandom2",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom2; },
		L"随机点名", L"SecRandom 2", L"随机点名", L"CustomizeIco10",
		InkeysBuiltInComponentAction::SecRandomQuickDraw);
	registerComponent(
		"Component.ShortcutButton.RollCall.SecRandom2Compat",
		"component.shortcutButton.rollCall.SecRandom2Compat",
		[] { return setlist.component.shortcutButton.rollCall.SecRandom2Compat; },
		L"随机点名", L"SecRandom 2 兼容", L"随机点名", L"CustomizeIco10",
		InkeysBuiltInComponentAction::SecRandomQuickDrawCompat);
	registerComponent(
		"Component.ShortcutButton.RollCall.NamePicker",
		"component.shortcutButton.rollCall.NamePicker",
		[] { return setlist.component.shortcutButton.rollCall.NamePicker; },
		L"随机点名", L"NamePicker", L"随机点名", L"CustomizeIco11",
		InkeysBuiltInComponentAction::NamePicker);

	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandSettings",
		"component.shortcutButton.linkage.classislandSettings",
		[] { return setlist.component.shortcutButton.linkage.classislandSettings; },
		L"联动", L"ClassIsland 设置", L"CI设置", L"CustomizeIco1",
		InkeysBuiltInComponentAction::ClassIslandSettings);
	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandProfile",
		"component.shortcutButton.linkage.classislandProfile",
		[] { return setlist.component.shortcutButton.linkage.classislandProfile; },
		L"联动", L"档案编辑", L"档案编辑", L"CustomizeIco1",
		InkeysBuiltInComponentAction::ClassIslandProfile);
	registerComponent(
		"Component.ShortcutButton.Linkage.ClassislandClassswap",
		"component.shortcutButton.linkage.classislandClassswap",
		[] { return setlist.component.shortcutButton.linkage.classislandClassswap; },
		L"联动", L"快速换课", L"快速换课", L"CustomizeIco1",
		InkeysBuiltInComponentAction::ClassIslandClassSwap);
}

void BarButtomSetClass::StateUpdate()
{
	CalcState();
	PresetHoming();
}
void BarButtomSetClass::UpdateDrawButtonStyle()
{
	static mutex mtx;
	lock_guard<mutex> lock(mtx);

	// 更新绘制按钮中的图标样式
	if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
		preset[(int)BarButtomPresetEnum::Draw]->icon.TransitionToResource(L"UI", L"barHighlighter1");
	else preset[(int)BarButtomPresetEnum::Draw]->icon.TransitionToResource(L"UI", L"barBrush1");
}

Inkeys::BarButtonSizeKind BarButtomSetClass::ToConfigSize(BarButtomSizeEnum size)
{
	switch (size)
	{
	case BarButtomSizeEnum::twoTwo: return Inkeys::BarButtonSizeKind::TwoTwo;
	case BarButtomSizeEnum::twoOne: return Inkeys::BarButtonSizeKind::TwoOne;
	case BarButtomSizeEnum::oneTwo: return Inkeys::BarButtonSizeKind::OneTwo;
	case BarButtomSizeEnum::oneOne: return Inkeys::BarButtonSizeKind::OneOne;
	}
	return Inkeys::BarButtonSizeKind::TwoTwo;
}

BarButtomSizeEnum BarButtomSetClass::ToRuntimeSize(Inkeys::BarButtonSizeKind size)
{
	switch (size)
	{
	case Inkeys::BarButtonSizeKind::TwoTwo: return BarButtomSizeEnum::twoTwo;
	case Inkeys::BarButtonSizeKind::TwoOne: return BarButtomSizeEnum::twoOne;
	case Inkeys::BarButtonSizeKind::OneTwo: return BarButtomSizeEnum::oneTwo;
	case Inkeys::BarButtonSizeKind::OneOne: return BarButtomSizeEnum::oneOne;
	}
	return BarButtomSizeEnum::twoTwo;
}

bool BarButtomSetClass::IsExactFixedZonePermutation(
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

std::vector<Inkeys::BarFixedButtonLayoutEntry> BarButtomSetClass::NormalizeFixedZone(
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

	// 严校验：必须是该区 required 集合的恰好排列，否则整区回默认。
	std::vector<Inkeys::BarFixedButtonLayoutEntry> source =
		IsExactFixedZonePermutation(configuredWithoutDivider, defaults) ? configuredWithoutDivider : defaults;

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

std::vector<Inkeys::BarExtensionButtonLayoutEntry> BarButtomSetClass::NormalizeExtensionZone(
	const std::vector<Inkeys::BarExtensionButtonLayoutEntry>& configured)
{
	std::vector<Inkeys::BarExtensionButtonLayoutEntry> normalized;
	normalized.reserve(configured.size());
	std::unordered_set<std::string> loadedSingletons;

	for (const Inkeys::BarExtensionButtonLayoutEntry& entry : configured)
	{
		// B 区只接受非 Inkeys. 前缀的点分扩展 ID；官方前缀与非法格式一律剔除。
		if (!Inkeys::IsExtensionBarButtonId(entry.Id)) continue;

		BarButtonRegistrationClass registration;
		const bool registered = TryGetRegistration(entry.Id, registration);
		if (registered)
		{
			if (registration.zone != BarButtonLayoutZoneEnum::Extension) continue;
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

void BarButtomSetClass::AppendFixedButtons(
	const std::vector<Inkeys::BarFixedButtonLayoutEntry>& entries,
	vector<shared_ptr<BarButtomClass>>& activeButtons)
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

int BarButtomSetClass::AppendLegacyExtensionButtons(vector<shared_ptr<BarButtomClass>>& activeButtons)
{
	int appended = 0;
	for (BarButtonRegistrationClass registration : GetExtensionRegistrations())
	{
		if (!registration.button || !registration.legacyEnabled || !registration.legacyEnabled())
		{
			continue;
		}

		registration.button->userVisible = true;
		registration.button->hide = false;
		registration.button->size = registration.defaultSize;
		activeButtons.push_back(registration.button);
		appended += 1;
	}
	return appended;
}

void BarButtomSetClass::AppendBoundaryDivider(
	vector<shared_ptr<BarButtomClass>>& activeButtons,
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

void BarButtomSetClass::Load()
{
	// UI3 与 UI2 并行期间，B 区只由旧组件开关投影，完全忽略新版 ExtensionButtons。
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> defaultA1 =
		Inkeys::MakeDefaultFixedButtonsA1().Snapshot();
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> defaultA2 =
		Inkeys::MakeDefaultFixedButtonsA2().Snapshot();

	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalizedA1 = NormalizeFixedZone(
		Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot(),
		defaultA1,
		BarButtonLayoutZoneEnum::FixedA1);
	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalizedA2 = NormalizeFixedZone(
		Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot(),
		defaultA2,
		BarButtonLayoutZoneEnum::FixedA2);

	vector<shared_ptr<BarButtomClass>> activeButtons;
	vector<shared_ptr<BarButtomClass>> extensionButtons;
	AppendFixedButtons(normalizedA1, activeButtons);
	const int extensionUiCount = AppendLegacyExtensionButtons(extensionButtons);
	if (extensionUiCount > 0)
	{
		AppendBoundaryDivider(activeButtons, 0); // A1 | B
		activeButtons.insert(activeButtons.end(), extensionButtons.begin(), extensionButtons.end());
		AppendBoundaryDivider(activeButtons, 1); // B | A2
	}
	else
	{
		// B 无可见扩展项：A1 与 A2 之间只保留一条交界分割线。
		AppendBoundaryDivider(activeButtons, 0);
	}
	AppendFixedButtons(normalizedA2, activeButtons);

	// 先在列表锁内替换整段序列，再发布数量；注册表和边界实例继续持有对象所有权。
	const int activeButtonCount = static_cast<int>(activeButtons.size());
	buttomlist.Replace(move(activeButtons));
	tot = activeButtonCount;

	// 只规范化 A1/A2；运行时投影不读取、修改或写回持久化 B 区。
	Inkeys::config.UI.Bar.FixedButtonsA1.Replace(std::move(normalizedA1));
	Inkeys::config.UI.Bar.FixedButtonsA2.Replace(std::move(normalizedA2));
}

void BarButtomSetClass::SyncLegacyExtensionButtons()
{
	Load();
}

void BarButtomSetClass::ResetPngIconCaches()
{
	shared_lock lock(registrationMutex);
	for (const auto& [id, registration] : registrations)
	{
		if (registration.button && registration.button->iconKind == BarButtomIconKindEnum::Png)
			registration.button->pngIcon.ResetCache();
	}
	for (const shared_ptr<BarButtomClass>& divider : boundaryDividers)
		if (divider && divider->iconKind == BarButtomIconKindEnum::Png) divider->pngIcon.ResetCache();
}

void BarButtomSetClass::PresetHoming()
{
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen
		|| barUISet.barState.fold
		|| !preset[(int)BarButtomPresetEnum::Draw]->IsVisible())
	{
		barUISet.barState.drawAttribute = false;
	}
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtShape
		|| barUISet.barState.fold
		|| !preset[(int)BarButtomPresetEnum::Geometry]->IsVisible())
	{
		barUISet.barState.geometryAttribute = false;
	}

	// 进入非绘制模式需要隐藏无用按钮
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection)
	{
		// 显示状态变化
		preset[(int)BarButtomPresetEnum::Eraser]->hide = true;
		preset[(int)BarButtomPresetEnum::Geometry]->hide = true;
		preset[(int)BarButtomPresetEnum::Recall]->hide = true;
		//preset[(int)BarButtomPresetEnum::Redo]->hide = true;
		// preset[(int)BarButtomPresetEnum::Clean]->hide = true;
		preset[(int)BarButtomPresetEnum::Pierce]->hide = true;

		// 显示尺寸变化
		preset[(int)BarButtomPresetEnum::Freeze]->size = BarButtomSizeEnum::twoTwo;

		// 显示名称变化
		preset[(int)BarButtomPresetEnum::Select]->name.content.SetTar(L"选择");
	}
	else
	{
		// 显示状态变化
		preset[(int)BarButtomPresetEnum::Eraser]->hide = false;
		preset[(int)BarButtomPresetEnum::Geometry]->hide = false;
		preset[(int)BarButtomPresetEnum::Recall]->hide = false;
		//preset[(int)BarButtomPresetEnum::Redo]->hide = false;
		// preset[(int)BarButtomPresetEnum::Clean]->hide = false;
		preset[(int)BarButtomPresetEnum::Pierce]->hide = false;

		// 显示尺寸变化
		preset[(int)BarButtomPresetEnum::Freeze]->size = BarButtomSizeEnum::twoOne;

		// 显示名称变化
		preset[(int)BarButtomPresetEnum::Select]->name.content.SetTar(L"选择(清空)");
	}
}
void BarButtomSetClass::CalcState()
{
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) barButtomState[(int)BarButtomPresetEnum::Select].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Select].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen) barButtomState[(int)BarButtomPresetEnum::Draw].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Draw].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser) barButtomState[(int)BarButtomPresetEnum::Eraser].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Eraser].state = BarWidgetState::None;
	}
	{
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape) barButtomState[(int)BarButtomPresetEnum::Geometry].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Geometry].state = BarWidgetState::None;
	}

	{
		if (penetrate.select) barButtomState[(int)BarButtomPresetEnum::Pierce].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Pierce].state = BarWidgetState::None;
	}
	{
		if (FreezeFrame.mode == 1) barButtomState[(int)BarButtomPresetEnum::Freeze].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Freeze].state = BarWidgetState::None;
	}

	{
		if (test.select) barButtomState[(int)BarButtomPresetEnum::Setting].state = BarWidgetState::Selected;
		else barButtomState[(int)BarButtomPresetEnum::Setting].state = BarWidgetState::None;
	}
}
