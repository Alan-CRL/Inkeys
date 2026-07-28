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

#include <unordered_set>

module Inkeys.UI.Bar;
import :Bottom;

import :Main;
import :Theme;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;
import Inkeys.Other.Config;

bool BarButtomSetClass::RegisterButton(const std::string& id, BarButtomClass* button, bool allowMultiple)
{
	if (id.empty() || button == nullptr) return false;

	unique_lock lock(registrationMutex);
	if (registrations.contains(id)) return false;

	button->id = id;
	button->only = !allowMultiple;
	registrations.emplace(id, BarButtonRegistrationClass{ button, allowMultiple });
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

	// 官方按钮使用稳定 ID；注册时同时声明是否允许在布局中重复。
	RegisterButton(Inkeys::BarButtonId::Divider, preset[(int)BarButtomPresetEnum::Divider], true);
	RegisterButton(Inkeys::BarButtonId::Select, preset[(int)BarButtomPresetEnum::Select], false);
	RegisterButton(Inkeys::BarButtonId::Draw, preset[(int)BarButtomPresetEnum::Draw], false);
	RegisterButton(Inkeys::BarButtonId::Eraser, preset[(int)BarButtomPresetEnum::Eraser], false);
	RegisterButton(Inkeys::BarButtonId::Geometry, preset[(int)BarButtomPresetEnum::Geometry], false);
	RegisterButton(Inkeys::BarButtonId::Recall, preset[(int)BarButtomPresetEnum::Recall], false);
	RegisterButton(Inkeys::BarButtonId::Clean, preset[(int)BarButtomPresetEnum::Clean], false);
	RegisterButton(Inkeys::BarButtonId::Pierce, preset[(int)BarButtomPresetEnum::Pierce], false);
	RegisterButton(Inkeys::BarButtonId::Freeze, preset[(int)BarButtomPresetEnum::Freeze], false);
	RegisterButton(Inkeys::BarButtonId::Setting, preset[(int)BarButtomPresetEnum::Setting], false);
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

void BarButtomSetClass::Load()
{
	const std::vector<Inkeys::BarButtonLayoutEntry> layout =
		Inkeys::config.UI.Bar.ButtonLayout.Snapshot();
	std::vector<Inkeys::BarButtonLayoutEntry> normalizedLayout;
	normalizedLayout.reserve(layout.size());
	std::unordered_set<std::string> loadedSingletons;

	for (const Inkeys::BarButtonLayoutEntry& entry : layout)
	{
		BarButtonRegistrationClass registration;
		if (!TryGetRegistration(entry.Id, registration))
		{
			// 未注册项可能来自暂时缺失的插件，保留原位置但不创建 UI。
			normalizedLayout.emplace_back(entry);
			continue;
		}

		if (!registration.allowMultiple && !loadedSingletons.emplace(entry.Id).second)
		{
			// 单例按钮只采用第一条，规范化结果会在下一次 Config::Write() 时清理磁盘。
			continue;
		}

		normalizedLayout.emplace_back(entry);
		registration.button->userVisible = entry.Visible;

		const int targetIndex = tot.load();
		if (buttomlist.Set(targetIndex, registration.button)) tot = targetIndex + 1;
	}

	Inkeys::config.UI.Bar.ButtonLayout.Replace(std::move(normalizedLayout));
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
