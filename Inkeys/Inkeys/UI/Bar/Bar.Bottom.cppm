module;

#include "../../../IdtMain.h"

#include <string>
#include <unordered_map>
#include <vector>

export module Inkeys.UI.Bar:Bottom;

import :UI;
import :State;
import Inkeys.Other.Config;

enum class BarButtomSizeEnum : int
{
	twoTwo, // 2*2 -> 70 * 70（= 两枚 2*1 + 间隙 5，或四枚 1*1）
	twoOne, // 2*1 -> 70 * 32.5（= 两枚 1*1 + 间隙 5）
	oneTwo, // 1*2 -> 10 * 70 仅限分割线（偏窄）
	oneOne // 1*1 -> 32.5 * 32.5（正方形）
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

enum class BarButtomIconKindEnum : int
{
	Svg,
	Png,
};

class BarButtomClass
{
public:
	BarButtomClass()
		: state(&localState)
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
		pngIcon(other.pngIcon),
		iconKind(other.iconKind),
		clickFunc(other.clickFunc),
		localState(other.localState),
		state(other.state == &other.localState ? &localState : other.state)
	{}

	bool IsVisible() const
	{
		return userVisible.load() && !hide.load();
	}
	// 图标和文字只在内容实际变化时启动同一套缩放、淡出与回弹过程。
	bool TransitionContent(const wstring& iconResourceName, const wstring& label);

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
	// SVG 继续承载统一的布局、透明度和动画状态；PNG 仅替换最终绘制载荷。
	BarUiSVGClass icon;
	BarUiPNGClass pngIcon;
	IdtAtomic<BarButtomIconKindEnum> iconKind = BarButtomIconKindEnum::Svg;

	function<void()> clickFunc;

	BarButtomStateClass localState;
	BarButtomStateClass* state = nullptr;
};

enum class BarButtonLayoutZoneEnum : int
	{
		FixedA1,
		Extension,
		FixedA2
	};

	struct BarButtonRegistrationClass
	{
		std::string id;
		shared_ptr<BarButtomClass> button;
		bool allowMultiple = false;
		BarButtonLayoutZoneEnum zone = BarButtonLayoutZoneEnum::Extension;
		// 注册时写死的默认尺寸/用户显隐；A 区配置不可改 Visible，Size 本轮也纠正回这里。
		BarButtomSizeEnum defaultSize = BarButtomSizeEnum::twoTwo;
		bool defaultUserVisible = true;
		std::string legacyField;
		function<bool()> legacyEnabled;
		wstring categoryName;
		wstring settingsName;
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
	void Replace(vector<shared_ptr<BarButtomClass>> next)
	{
		unique_lock lock(mt);
		list = move(next);
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
	vector<shared_ptr<BarButtomClass>> list;
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
			bool defaultUserVisible = true,
			const std::string& legacyField = {},
			function<bool()> legacyEnabled = {},
			const wstring& categoryName = {},
			const wstring& settingsName = {});
		bool TryGetRegistration(const std::string& id, BarButtonRegistrationClass& outRegistration) const;
		vector<BarButtonRegistrationClass> GetExtensionRegistrations() const;
		void PresetInitialization();
		void RegisterBuiltInComponents();
		void StateUpdate();
		void UpdateDrawButtonStyle();

		void Load();
		void SyncLegacyExtensionButtons();
		void ResetIconCaches();

	protected:
		void UpdateEraserButtonStyle();
		void UpdateGeometryButtonStyle();
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
			void AppendFixedButtons(
				const std::vector<Inkeys::BarFixedButtonLayoutEntry>& entries,
				vector<shared_ptr<BarButtomClass>>& activeButtons);
			int AppendLegacyExtensionButtons(vector<shared_ptr<BarButtomClass>>& activeButtons);
			// 交界分割线仅进运行时列表，不写回配置。
			void AppendBoundaryDivider(
				vector<shared_ptr<BarButtomClass>>& activeButtons,
				size_t boundaryIndex);

		mutable shared_mutex registrationMutex;
		IdtAtomic<int> drawButtonStyleKey = -1;
		IdtAtomic<int> eraserButtonStyleKey = -1;
		IdtAtomic<int> geometryButtonStyleKey = -1;
		unordered_map<std::string, BarButtonRegistrationClass> registrations;
		vector<std::string> registrationOrder;
		shared_ptr<BarButtomClass> boundaryDividers[2];
	};
