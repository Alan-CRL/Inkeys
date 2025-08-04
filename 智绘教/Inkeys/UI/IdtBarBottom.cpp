#include "IdtBarBottom.h"

void BarButtomSetClass::PresetInitialization()
{
	{
		BarButtomClass* divider = new BarButtomClass;
		divider->preset = BarButtomPresetEnum::Divider;

		divider->size = BarButtomSizeEnum::oneTwo;

		divider->name.enable.Initialization(false);

		divider->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		divider->buttom.enable.Initialization(true);

		divider->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		divider->icon.InitializationFromResource(L"UI", L"barDivider");
		divider->icon.enable.Initialization(true);

		preset[(int)BarButtomPresetEnum::Divider] = divider;
	}

	{
		BarButtomClass* select = new BarButtomClass;
		select->preset = BarButtomPresetEnum::Select;

		select->size = BarButtomSizeEnum::twoTwo;

		select->name.Initialization(0.0, 0.0, 0.0, 0.0, "选择", 20.0);
		select->name.enable.Initialization(true);

		select->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		select->buttom.enable.Initialization(true);

		select->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		select->icon.InitializationFromResource(L"UI", L"barSelect");
		select->icon.enable.Initialization(true);

		preset[(int)BarButtomPresetEnum::Select] = select;
	}
	{
		BarButtomClass* draw = new BarButtomClass;
		draw->preset = BarButtomPresetEnum::Draw;

		draw->size = BarButtomSizeEnum::twoTwo;

		draw->name.Initialization(0.0, 0.0, 0.0, 0.0, "绘制", 20.0);
		draw->name.enable.Initialization(true);

		draw->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
		draw->buttom.enable.Initialization(true);

		draw->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
		draw->icon.InitializationFromResource(L"UI", L"barBursh1");
		draw->icon.enable.Initialization(true);

		preset[(int)BarButtomPresetEnum::Draw] = draw;
	}
}

void BarButtomSetClass::Load()
{
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Select]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Draw]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Divider]);
}