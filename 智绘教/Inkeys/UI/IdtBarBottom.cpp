#include "IdtBarBottom.h"

#include "../../IdtState.h"
#include "../../IdtDraw.h" // 历史遗留问题

void BarButtomSetClass::PresetInitialization()
{
	// 分隔线
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::oneTwo;
			obj->preset = BarButtomPresetEnum::Divider;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "分割线", 0.0);
			obj->name.enable.Initialization(false);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "选择", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "绘制", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barBursh1");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen)
					{
						barState.drawAttribute = false;
						ChangeStateModeToPen();
					}
					else
					{
						if (barState.drawAttribute) barState.drawAttribute = false;
						else barState.drawAttribute = true;
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "擦除", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barEraser");
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "撤回", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barRecall");
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
	// TODO 重做
	// 清空
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Clean;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "清空", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barClean");
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

	// 穿透
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Pierce;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "穿透", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barPierce");
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
	// 定格
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoTwo;
			obj->preset = BarButtomPresetEnum::Freeze;
			obj->hide = false;
		}

		{
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "定格", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
			obj->icon.InitializationFromResource(L"UI", L"barFreeze");
			obj->icon.enable.Initialization(true);
		}

		{
			obj->clickFunc = [&]() -> void
				{
					// TODO 注意 ppt_show == NULL 时需要禁用按钮
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "设置", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
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
}
void BarButtomSetClass::PresetHoming()
{
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen) barState.drawAttribute = false;
}

void BarButtomSetClass::Load()
{
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Select]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Draw]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Eraser]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Recall]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Clean]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Divider]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Pierce]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Freeze]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Setting]);
}