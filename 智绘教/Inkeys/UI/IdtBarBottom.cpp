#include "IdtBarBottom.h"

void BarButtomSetClass::PresetInitialization()
{
	{
		BarButtomClass* divider = new BarButtomClass;
		divider->preset = BarButtomPresetEnum::Divider;

		divider->size = BarButtomSizeEnum::oneTwo;

		divider->name.Initialization(0.0, 0.0, 0.0, 0.0, "分割线", 0.0);
		divider->name.enable.Initialization(false);

		divider->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		divider->buttom.enable.Initialization(true);

		divider->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		divider->icon.InitializationFromResource(L"UI", L"barDivider");
		divider->icon.enable.Initialization(true);

		divider->state = &barButtomState[(int)divider->preset.load()];
		preset[(int)divider->preset.load()] = divider;
	}

	{
		BarButtomClass* select = new BarButtomClass;
		select->preset = BarButtomPresetEnum::Select;

		select->size = BarButtomSizeEnum::twoTwo;

		select->name.Initialization(0.0, 0.0, 0.0, 0.0, "选择", 0.0);
		select->name.enable.Initialization(true);

		select->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		select->buttom.enable.Initialization(true);

		select->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		select->icon.InitializationFromResource(L"UI", L"barSelect");
		select->icon.enable.Initialization(true);

		select->state = &barButtomState[(int)select->preset.load()];
		preset[(int)select->preset.load()] = select;
	}
	{
		BarButtomClass* draw = new BarButtomClass;
		draw->preset = BarButtomPresetEnum::Draw;

		draw->size = BarButtomSizeEnum::twoTwo;

		draw->name.Initialization(0.0, 0.0, 0.0, 0.0, "绘制", 0.0);
		draw->name.enable.Initialization(true);

		draw->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		draw->buttom.enable.Initialization(true);

		draw->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		draw->icon.InitializationFromResource(L"UI", L"barBursh1");
		draw->icon.enable.Initialization(true);

		draw->state = &barButtomState[(int)draw->preset.load()];
		preset[(int)draw->preset.load()] = draw;
	}
	{
		BarButtomClass* freeze = new BarButtomClass;
		freeze->preset = BarButtomPresetEnum::Freeze;

		freeze->size = BarButtomSizeEnum::twoTwo;

		freeze->name.Initialization(0.0, 0.0, 0.0, 0.0, "定格", 0.0);
		freeze->name.enable.Initialization(true);

		freeze->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		freeze->buttom.enable.Initialization(true);

		freeze->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		freeze->icon.InitializationFromResource(L"UI", L"barFreeze");
		freeze->icon.enable.Initialization(true);

		freeze->state = &barButtomState[(int)freeze->preset.load()];
		preset[(int)freeze->preset.load()] = freeze;
	}
	{
		BarButtomClass* setting = new BarButtomClass;
		setting->preset = BarButtomPresetEnum::Setting;

		setting->size = BarButtomSizeEnum::twoTwo;

		setting->name.Initialization(0.0, 0.0, 0.0, 0.0, "设置", 0.0);
		setting->name.enable.Initialization(true);

		setting->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		setting->buttom.enable.Initialization(true);

		setting->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		setting->icon.InitializationFromResource(L"UI", L"barSetting");
		setting->icon.enable.Initialization(true);

		setting->state = &barButtomState[(int)setting->preset.load()];
		preset[(int)setting->preset.load()] = setting;
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