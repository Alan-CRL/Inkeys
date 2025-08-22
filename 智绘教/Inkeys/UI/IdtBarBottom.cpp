#include "IdtBarBottom.h"

#include "IdtBar.h"
#include "../../IdtState.h"

// 历史遗留问题
#include "../../IdtDraw.h"
#include "../../IdtDrawpad.h"
#include "../../IdtHistoricalDrawpad.h"
#include "../../IdtImage.h"

void BarButtomSetClass::PresetInitialization()
{
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
			obj->name.Initialization(0.0, 0.0, 0.0, 0.0, "几何", 0.0);
			obj->name.enable.Initialization(true);
		}
		{
			obj->buttom.Initialization(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, nullopt, RGB(0, 0, 0), nullopt);
			obj->buttom.enable.Initialization(true);
		}
		{
			obj->icon.Initialization(0.0, 0.0, RGB(0, 0, 0), nullopt);
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
			obj->size = BarButtomSizeEnum::twoOne;
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
	// TODO 重做
	// 清空
	{
		BarButtomClass* obj = new BarButtomClass;
		{
			obj->size = BarButtomSizeEnum::twoOne;
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
			obj->size = BarButtomSizeEnum::twoOne;
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

			obj->only = false;
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

// 预设按钮归位
void BarButtomSetClass::PresetHoming()
{
	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtPen || barUISet.barState.fold) barUISet.barState.drawAttribute = false;

	// 进入非绘制模式需要隐藏无用按钮
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection)
	{
		// 显示状态变化
		preset[(int)BarButtomPresetEnum::Eraser]->hide = true;
		preset[(int)BarButtomPresetEnum::Geometry]->hide = true;
		preset[(int)BarButtomPresetEnum::Recall]->hide = true;
		//preset[(int)BarButtomPresetEnum::Redo]->hide = true;
		preset[(int)BarButtomPresetEnum::Clean]->hide = true;
		preset[(int)BarButtomPresetEnum::Pierce]->hide = true;

		// 显示尺寸变化
		preset[(int)BarButtomPresetEnum::Freeze]->size = BarButtomSizeEnum::twoTwo;

		// 显示名称变化
		preset[(int)BarButtomPresetEnum::Select]->name.content.SetTar("选择");
	}
	else
	{
		// 显示状态变化
		preset[(int)BarButtomPresetEnum::Eraser]->hide = false;
		preset[(int)BarButtomPresetEnum::Geometry]->hide = false;
		preset[(int)BarButtomPresetEnum::Recall]->hide = false;
		//preset[(int)BarButtomPresetEnum::Redo]->hide = false;
		preset[(int)BarButtomPresetEnum::Clean]->hide = false;
		preset[(int)BarButtomPresetEnum::Pierce]->hide = false;

		// 显示尺寸变化
		preset[(int)BarButtomPresetEnum::Freeze]->size = BarButtomSizeEnum::twoOne;

		// 显示名称变化
		preset[(int)BarButtomPresetEnum::Select]->name.content.SetTar("选择(清空)");
	}
}
// 计算按钮状态
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
// 更新按钮状态
void BarButtomSetClass::StateUpdate()
{
	CalcState();
	PresetHoming();
}
// 更新绘制按钮中的图标样式
void BarButtomSetClass::UpdateDrawButtonStyle()
{
	if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
		preset[(int)BarButtomPresetEnum::Draw]->icon.SetTarFromResource(L"UI", L"barHighlighter1");
	else preset[(int)BarButtomPresetEnum::Draw]->icon.SetTarFromResource(L"UI", L"barBrush1");
}

void BarButtomSetClass::Load()
{
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Select]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Draw]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Eraser]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Geometry]);

	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Recall]);
	// buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Redo]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Clean]);

	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Divider]);

	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Pierce]);
	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Freeze]);

	buttomlist.Set(tot++, preset[(int)BarButtomPresetEnum::Setting]);
}