#include "IdtBarBottom.h"

#include "../../IdtState.h"
#include "../../IdtDraw.h" // 历史遗留问题

void BarButtomSetClass::PresetInitialization()
{
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
			obj->clickFunc = []() -> void
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection)
						ChangeStateModeToSelection();
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
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
			obj->clickFunc = []() -> void
				{
					// TODO 后续记得分离 IdtPen 和 IdtShape
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen &&
						stateMode.StateModeSelect != StateModeSelectEnum::IdtShape)
					{
						ChangeStateModeToPen();
					}
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
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
			obj->clickFunc = []() -> void
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
			obj->clickFunc = []() -> void
				{
					if (test.select) test.select = false;
					else test.select = true;
				};
		}

		obj->state = &barButtomState[(int)obj->preset.load()];
		preset[(int)obj->preset.load()] = obj;
	}
}

void BarButtomSetClass::Load()
{
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Select]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Draw]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Divider]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Freeze]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Setting]);
}