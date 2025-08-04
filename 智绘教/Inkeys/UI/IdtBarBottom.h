#pragma once

#include "../../IdtMain.h"
#include "IdtBar.h"

enum class BarButtomSizeEnum : int
{
	twoTwo, // 2*2 -> 70 * 50
	twoOne, // 2*1 -> 70 * 32.5
	oneTwo, // 1*2 -> 10 * 70 仅限分割线（偏窄）
	oneOne // 1*1 -> 32.5 * 32.5
};
enum class BarButtomState : int
{
	None,
	Hover, // 悬停（废弃）
	Pressed, // 按下
	Selected // 选中
};
enum class BarButtomPresetEnum : int
{
	None,
	Divider,

	Select,
	Draw
};

class BarButtomClass
{
public:
	BarButtomClass() {}

public:
	IdtAtomic<BarButtomSizeEnum> size;
	IdtAtomic<BarButtomPresetEnum> preset = BarButtomPresetEnum::None;

	BarUiShapeClass buttom;
	BarUiWordClass name;
	BarUiSVGClass icon;

	BarButtomState state;
};
class BarButtomListClass
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
		list[x] = shared_ptr<BarButtomClass>(ptr);
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

class BarButtomSetClass
{
public:
	BarButtomListClass buttomlist;
	IdtAtomic<int> tot; // 顺序列顶，开

	BarButtomClass* preset[40];

public:
	void PresetInitialization();

	// TODO 临时方案，按照默认样式加载，后续改为从配置中加载布局
	void Load();
};
