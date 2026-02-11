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
#include "../../../IdtText.h"
#include "../../Conv/IdtColor.h"
#include "../../Other/IdtInputs.h"

export module Inkeys.UI.Bar.Bottom;

import Inkeys.UI.Bar.UI;
import Inkeys.UI.Bar.State;

export enum class BarButtomSizeEnum : int
{
	twoTwo, // 2*2 -> 70 * 50
	twoOne, // 2*1 -> 70 * 32.5
	oneTwo, // 1*2 -> 10 * 70 仅限分割线（偏窄）
	oneOne // 1*1 -> 32.5 * 32.5
};
export enum class BarButtomPresetEnum : int
{
	None,
	Divider,

	Select,
	Draw,
	Eraser,
	Geometry,

	Recall,
	Redo,
	Clean,

	Pierce,
	Freeze,

	Setting
};

export class BarButtomStateClass
{
public:
	BarButtomStateClass() {}
public:
	BarWidgetState state = BarWidgetState::None;
	BarWidgetEmphasize emph = BarWidgetEmphasize::None;
};

export class BarButtomClass
{
public:
	BarButtomClass() {}
	// 拷贝构造函数，深拷贝所有数据成员，mutex新建
	BarButtomClass(const BarButtomClass& other)
		: size(other.size),
		preset(other.preset),
		hide(other.hide),
		only(other.only),
		buttom(other.buttom),
		name(other.name),
		icon(other.icon),
		clickFunc(other.clickFunc),
		state(other.state) // 浅拷贝指针
	{
	}

public:
	IdtAtomic<BarButtomSizeEnum> size;
	IdtAtomic<BarButtomPresetEnum> preset = BarButtomPresetEnum::None;
	IdtAtomic<bool> hide = true;
	IdtAtomic<bool> only = true;

	// 绘制记录
	IdtAtomic<double> lastDrawX = 0.0;
	IdtAtomic<double> lastDrawY = 0.0;

	// 按钮控件
	BarUiShapeClass buttom;
	BarUiWordClass name;
	BarUiSVGClass icon;

	function<void()> clickFunc;

	BarButtomStateClass* state;
};
export class BarButtomListClass
{
public:
	BarButtomClass* Get(int x)
	{
		shared_lock lock(mt);
		if (x >= 0 && x < list.size()) return list.at(x).get();
		return nullptr;
	}
	bool Set(int x, BarButtomClass* ptr)
	{
		unique_lock lock(mt);
		if (!(x >= 0 && x < list.size()) || ptr == nullptr) return false;

		// 如果对象不唯一，则应用深拷贝
		if (!ptr->only)
		{
			// 深拷贝 BarButtomClass 对象
			shared_ptr<BarButtomClass> copy = make_shared<BarButtomClass>(*ptr);
			list[x] = copy;
		}
		else
		{
			// 直接使用传入的指针
			list[x] = shared_ptr<BarButtomClass>(ptr);
		}

		return true;
	}

	bool Swap(int x, int y)
	{
		unique_lock lock(mt);
		if (!(x >= 0 && x < list.size())) return false;
		if (!(y >= 0 && y < list.size())) return false;
		if (x == y) return false;

		swap(list[x], list[y]);
		return true;
	}

protected:
	shared_mutex mt;
	vector<shared_ptr<BarButtomClass>> list{ 40 };
};

export class BarButtomSetClass
{
public:
	BarButtomListClass buttomlist;
	IdtAtomic<int> tot; // 顺序列顶，开

	// 按钮状态
	unordered_map<int, BarButtomStateClass> barButtomState;

	// 预设按钮模态
	BarButtomClass* preset[40];

public:
	void PresetInitialization();
	void StateUpdate()
	{
		CalcState();
		PresetHoming();
	}
	void UpdateDrawButtonStyle()
	{
		// 更新绘制按钮中的图标样式
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			preset[(int)BarButtomPresetEnum::Draw]->icon.SetTarFromResource(L"UI", L"barHighlighter1");
		else preset[(int)BarButtomPresetEnum::Draw]->icon.SetTarFromResource(L"UI", L"barBrush1");
	}

	// TODO 临时方案，按照默认样式加载，后续改为从配置中加载布局
	void Load()
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

protected:
	void PresetHoming();
	void CalcState()
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
};