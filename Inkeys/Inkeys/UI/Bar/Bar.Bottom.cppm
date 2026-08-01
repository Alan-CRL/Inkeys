module;

#include "../../../IdtMain.h"

#include <string>
#include <unordered_map>

export module Inkeys.UI.Bar:Bottom;

import :UI;
import :State;
import Inkeys.Other.Config;

enum class BarButtomSizeEnum : int
{
	twoTwo, // 2*2 -> 70 * 50
	twoOne, // 2*1 -> 70 * 32.5
	oneTwo, // 1*2 -> 10 * 70 仅限分割线（偏窄）
	oneOne // 1*1 -> 32.5 * 32.5
};
enum class BarButtomPresetEnum : int
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

class BarButtomStateClass
{
public:
	BarButtomStateClass() {}
public:
	BarWidgetState state = BarWidgetState::None;
	BarWidgetEmphasize emph = BarWidgetEmphasize::None;
};

enum class BarButtomHoverStageEnum : int
{
	None,
	Showing,
	Fading,
};

class BarButtomClass
{
public:
	BarButtomClass()
	{
		// 按钮背景只承载选中、按下和悬停效果，首次显示前必须保持完全透明。
		buttom.pct.Initialization(0.0);
	}
	// 拷贝构造函数，深拷贝所有数据成员，mutex新建
	BarButtomClass(const BarButtomClass& other)
		: size(other.size),
		preset(other.preset),
		id(other.id),
		userVisible(other.userVisible),
		hide(other.hide),
		only(other.only),
		buttom(other.buttom),
		hoverStage(other.hoverStage),
		pressScale(other.pressScale),
		name(other.name),
		icon(other.icon),
		clickFunc(other.clickFunc),
		state(other.state) // 浅拷贝指针
	{}

	bool IsVisible() const
	{
		return userVisible.load() && !hide.load();
	}

public:
	IdtAtomic<BarButtomSizeEnum> size;
	IdtAtomic<BarButtomPresetEnum> preset = BarButtomPresetEnum::None;
	std::string id;
	// 配置显隐与运行时上下文 hide 分离，所有消费方统一读取 IsVisible()。
	IdtAtomic<bool> userVisible = true;
	IdtAtomic<bool> hide = true;
	IdtAtomic<bool> only = true;

	// 绘制记录
	IdtAtomic<double> lastDrawX = 0.0;
	IdtAtomic<double> lastDrawY = 0.0;

	// 按钮控件
	BarUiShapeClass buttom;
	// 悬停、按下和选中共用按钮原背景层，避免多层透明色叠加时发生闪变。
	IdtAtomic<BarButtomHoverStageEnum> hoverStage = BarButtomHoverStageEnum::None;
	// 背景、图标和文字共用的绘制倍率，不参与布局及命中区域计算。
	BarUiValueClass pressScale = BarUiValueClass(1.0);
	BarUiWordClass name;
	BarUiSVGClass icon;

	function<void()> clickFunc;

	BarButtomStateClass* state;
};

enum class BarButtonLayoutZoneEnum : int
	{
		FixedA1,
		Extension,
		FixedA2
	};

	struct BarButtonRegistrationClass
	{
		BarButtomClass* button = nullptr;
		bool allowMultiple = false;
		BarButtonLayoutZoneEnum zone = BarButtonLayoutZoneEnum::Extension;
		// 注册时写死的默认尺寸/用户显隐；A 区配置不可改 Visible，Size 本轮也纠正回这里。
		BarButtomSizeEnum defaultSize = BarButtomSizeEnum::twoTwo;
		bool defaultUserVisible = true;
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
		if (x < 0 || ptr == nullptr) return false;
		const size_t index = static_cast<size_t>(x);
		// 配置序列不设固定长度，按需扩展运行时按钮槽位。
		if (index >= list.size()) list.resize(index + 1);

		// 如果对象不唯一，则应用深拷贝
		if (!ptr->only)
		{
			// 深拷贝 BarButtomClass 对象
			shared_ptr<BarButtomClass> copy = make_shared<BarButtomClass>(*ptr);
			list[index] = copy;
		}
		else
		{
			// 直接使用传入的指针
			list[index] = shared_ptr<BarButtomClass>(ptr);
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

class BarButtomSetClass
{
public:
	BarButtomListClass buttomlist;
	IdtAtomic<int> tot = 0; // 顺序列顶，开

	// 按钮状态
	unordered_map<int, BarButtomStateClass> barButtomState;

	// 预设按钮模态
	BarButtomClass* preset[40]{};

public:
bool RegisterButton(
			const std::string& id,
			BarButtomClass* button,
			bool allowMultiple,
			BarButtonLayoutZoneEnum zone,
			bool defaultUserVisible = true);
		void PresetInitialization();
		void StateUpdate();
		void UpdateDrawButtonStyle();

		void Load();

	protected:
		bool TryGetRegistration(const std::string& id, BarButtonRegistrationClass& outRegistration) const;
		void PresetHoming();
		void CalcState();
		static Inkeys::BarButtonSizeKind ToConfigSize(BarButtomSizeEnum size);
		static BarButtomSizeEnum ToRuntimeSize(Inkeys::BarButtonSizeKind size);
		static bool IsExactFixedZonePermutation(
			const std::vector<Inkeys::BarFixedButtonLayoutEntry>& configured,
			const std::vector<Inkeys::BarFixedButtonLayoutEntry>& defaults);
		std::vector<Inkeys::BarFixedButtonLayoutEntry> NormalizeFixedZone(
			const std::vector<Inkeys::BarFixedButtonLayoutEntry>& configured,
			const std::vector<Inkeys::BarFixedButtonLayoutEntry>& defaults,
			BarButtonLayoutZoneEnum zone);
std::vector<Inkeys::BarExtensionButtonLayoutEntry> NormalizeExtensionZone(
				const std::vector<Inkeys::BarExtensionButtonLayoutEntry>& configured);
			void AppendFixedButtons(const std::vector<Inkeys::BarFixedButtonLayoutEntry>& entries);
			// 返回实际创建到 UI 的扩展按钮数量（未知 ID 只留配置不计）。
			int AppendExtensionButtons(const std::vector<Inkeys::BarExtensionButtonLayoutEntry>& entries);
			// 交界分割线仅进运行时列表，不写回配置。
			void AppendBoundaryDivider();

		mutable shared_mutex registrationMutex;
		unordered_map<std::string, BarButtonRegistrationClass> registrations;
	};
