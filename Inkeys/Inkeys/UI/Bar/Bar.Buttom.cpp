module;

#include "../../../IdtMain.h"

// 历史遗留问题
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtHistoricalDrawpad.h"
#include "../../../IdtImage.h"
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
	bool defaultUserVisible)
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
	registrations.emplace(
		id,
		BarButtonRegistrationClass{
			button,
			allowMultiple,
			zone,
			button->size.load(),
			defaultUserVisible
		});
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
						barUISet.barState.drawAttribute = false;
						ChangeStateModeToPen();
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
	// Geometry 默认不展示：由注册写死，配置 A 区不可改 Visible。
	RegisterButton(Inkeys::BarButtonId::Geometry, preset[(int)BarButtomPresetEnum::Geometry], false, BarButtonLayoutZoneEnum::FixedA1, false);
	RegisterButton(Inkeys::BarButtonId::Recall, preset[(int)BarButtomPresetEnum::Recall], false, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Clean, preset[(int)BarButtomPresetEnum::Clean], false, BarButtonLayoutZoneEnum::FixedA1);
	// Divider 不进 A1 配置 required 集；仅作运行时交界注入模板（可多实例拷贝）。
	RegisterButton(Inkeys::BarButtonId::Divider, preset[(int)BarButtomPresetEnum::Divider], true, BarButtonLayoutZoneEnum::FixedA1);
	RegisterButton(Inkeys::BarButtonId::Pierce, preset[(int)BarButtomPresetEnum::Pierce], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::Freeze, preset[(int)BarButtomPresetEnum::Freeze], false, BarButtonLayoutZoneEnum::FixedA2);
	RegisterButton(Inkeys::BarButtonId::Setting, preset[(int)BarButtomPresetEnum::Setting], false, BarButtonLayoutZoneEnum::FixedA2);
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

void BarButtomSetClass::AppendFixedButtons(const std::vector<Inkeys::BarFixedButtonLayoutEntry>& entries)
{
	for (const Inkeys::BarFixedButtonLayoutEntry& entry : entries)
	{
		BarButtonRegistrationClass registration;
		if (!TryGetRegistration(entry.Id, registration) || registration.button == nullptr) continue;

		// A 区显隐只来自注册默认，不读配置 Visible。
		registration.button->userVisible = registration.defaultUserVisible;
		registration.button->size = ToRuntimeSize(entry.Size);

		const int targetIndex = tot.load();
		if (buttomlist.Set(targetIndex, registration.button)) tot = targetIndex + 1;
	}
}

void BarButtomSetClass::AppendExtensionButtons(const std::vector<Inkeys::BarExtensionButtonLayoutEntry>& entries)
{
	for (const Inkeys::BarExtensionButtonLayoutEntry& entry : entries)
	{
		BarButtonRegistrationClass registration;
		if (!TryGetRegistration(entry.Id, registration) || registration.button == nullptr)
		{
			// 未注册插件保留在配置中，但不创建 UI。
			continue;
		}

		registration.button->userVisible = entry.Visible;
		registration.button->size = ToRuntimeSize(entry.Size);

		const int targetIndex = tot.load();
		if (buttomlist.Set(targetIndex, registration.button)) tot = targetIndex + 1;
	}
}

void BarButtomSetClass::AppendBoundaryDivider()
{
	BarButtonRegistrationClass registration;
	if (!TryGetRegistration(Inkeys::BarButtonId::Divider, registration) || registration.button == nullptr)
	{
		return;
	}

	// 交界分割线强制可见，尺寸用注册默认；不写配置。
	registration.button->userVisible = true;
	registration.button->size = registration.defaultSize;

	const int targetIndex = tot.load();
	if (buttomlist.Set(targetIndex, registration.button)) tot = targetIndex + 1;
}

void BarButtomSetClass::CollapseAdjacentRuntimeDividers()
{
	const int count = tot.load();
	if (count <= 1) return;

	std::vector<BarButtomClass*> collapsed;
	collapsed.reserve(static_cast<size_t>(count));

	for (int index = 0; index < count; index++)
	{
		BarButtomClass* button = buttomlist.Get(index);
		if (button == nullptr) continue;

		if (!collapsed.empty()
			&& collapsed.back() != nullptr
			&& Inkeys::IsRuntimeBoundaryDividerId(collapsed.back()->id)
			&& Inkeys::IsRuntimeBoundaryDividerId(button->id))
		{
			// 相邻分割线只保留第一个。
			continue;
		}
		collapsed.push_back(button);
	}

	tot = 0;
	for (BarButtomClass* button : collapsed)
	{
		const int targetIndex = tot.load();
		if (buttomlist.Set(targetIndex, button)) tot = targetIndex + 1;
	}
}

void BarButtomSetClass::Load()
{
	// 配置顺序：A1 + B + A2；运行时在 A1|B 与 B|A2 交界注入分割线（不写配置）。
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> defaultA1 =
		Inkeys::MakeDefaultFixedButtonsA1().Snapshot();
	const std::vector<Inkeys::BarFixedButtonLayoutEntry> defaultA2 =
		Inkeys::MakeDefaultFixedButtonsA2().Snapshot();

	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalizedA1 = NormalizeFixedZone(
		Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot(),
		defaultA1,
		BarButtonLayoutZoneEnum::FixedA1);
	std::vector<Inkeys::BarExtensionButtonLayoutEntry> normalizedB = NormalizeExtensionZone(
		Inkeys::config.UI.Bar.ExtensionButtons.Snapshot());
	std::vector<Inkeys::BarFixedButtonLayoutEntry> normalizedA2 = NormalizeFixedZone(
		Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot(),
		defaultA2,
		BarButtonLayoutZoneEnum::FixedA2);

	tot = 0;
	AppendFixedButtons(normalizedA1);
	AppendBoundaryDivider(); // A1 | B
	AppendExtensionButtons(normalizedB);
	AppendBoundaryDivider(); // B | A2
	AppendFixedButtons(normalizedA2);
	// B 为空时两个交界分割线相邻，全局折叠为一条。
	CollapseAdjacentRuntimeDividers();

	// 写回仅三区配置；交界 Divider 永不进入 schema 字段。
	Inkeys::config.UI.Bar.FixedButtonsA1.Replace(std::move(normalizedA1));
	Inkeys::config.UI.Bar.ExtensionButtons.Replace(std::move(normalizedB));
	Inkeys::config.UI.Bar.FixedButtonsA2.Replace(std::move(normalizedA2));
}

void BarButtomSetClass::PresetHoming()
{
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen
		|| barUISet.barState.fold
		|| !preset[(int)BarButtomPresetEnum::Draw]->IsVisible())
	{
		barUISet.barState.drawAttribute = false;
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
