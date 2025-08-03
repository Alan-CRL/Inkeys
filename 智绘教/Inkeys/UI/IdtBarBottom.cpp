#include "IdtBarBottom.h"

BarButtomSetClass barButtomSet;

void BarButtomSetClass::PresetInitialization()
{
	{
		BarButtomClass* divider = new BarButtomClass;
		divider->preset = BarPresetEnum::Divider;

		divider->size = BarButtomSizeEnum::oneTwo;
		divider->name.enable.Initialization(false);
		divider->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 8.0, 8.0, 0.0, nullopt, nullopt);
		divider->buttom.enable.Initialization(true);
		divider->icon.InitializationFromResource(L"UI", L"barDivider");
		divider->icon.enable.Initialization(true);

		preset[(int)BarPresetEnum::Divider] = divider;
	}

	{
		BarButtomClass* select = new BarButtomClass;
		select->preset = BarPresetEnum::Select;

		select->size = BarButtomSizeEnum::twoTwo;
		select->name.enable.Initialization(true);
		select->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 8.0, 8.0, 0.0, nullopt, nullopt);
		select->buttom.enable.Initialization(true);
		select->icon.InitializationFromResource(L"UI", L"barSelect");
		select->icon.enable.Initialization(true);

		preset[(int)BarPresetEnum::Select] = select;
	}
	{
		BarButtomClass* draw = new BarButtomClass;
		draw->preset = BarPresetEnum::Draw;

		draw->size = BarButtomSizeEnum::twoTwo;
		draw->name.enable.Initialization(true);
		draw->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 8.0, 8.0, 0.0, nullopt, nullopt);
		draw->buttom.enable.Initialization(true);
		draw->icon.enable.Initialization(true);

		preset[(int)BarPresetEnum::Draw] = draw;
	}
}

void BarButtomSetClass::Load()
{
	buttomlist.Set(tot++, preset[(int)BarPresetEnum::Select]);
	buttomlist.Set(tot++, preset[(int)BarPresetEnum::Draw]);
	buttomlist.Set(tot++, preset[(int)BarPresetEnum::Divider]);
}