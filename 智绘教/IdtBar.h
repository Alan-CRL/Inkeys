#pragma once

#include "IdtMain.h"

// 窗口

class BarWindowPosClass
{
public:
	BarWindowPosClass()
	{
		x = y = 0;
		w = h = 0;
		pct = 0;
	}

public:
	IdtAtomic<unsigned int> x, y;
	IdtAtomic<unsigned int> w, h;
	IdtAtomic<unsigned int> pct; // 透明度
};
extern BarWindowPosClass barWindowPos;

// 媒体
class BarMediaClass
{
public:
	void LoadExImage();

public:
	enum class BarExImageEnum : int
	{
	};

public:
	IMAGE Image[10];
};

// 界面

enum class BarUiValueModeClass : int
{
	Once = 0, // 直接移动模式
	Constant = 1, // 恒定速度模式
	Variable = 2 // 回弹动效模式
};

// 单个 UI 值
class BarUiValueClass
{
public:
	BarUiValueClass() { mod.store(BarUiValueModeClass::Once); }
	static BarUiValueClass BarUiValueClassModeOnce(double val, double ary)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Once;

			// TODO
		}
		return obj;
	}
	static BarUiValueClass BarUiValueClassModeConstant(double val, double ary, double spe)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Constant;

			// TODO
		}
		return obj;
	}
	static BarUiValueClass BarUiValueClassModeVariable(double val, double ary, double acc, double backAcc)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Variable;

			// TODO
		}
		return obj;
	}

public:
	IdtAtomic<BarUiValueModeClass> mod;

	class ValueClass
	{
	public:
		ValueClass()
		{
			val = 0;
			ary = 0;

			spe = 0;
			startV = endV = 0;
		}

		// 仅是完全比较
		bool operator==(const ValueClass& other) const
		{
			if (val != other.val) return false;
			if (ary != other.ary) return false;
			if (spe != other.spe) return false;
			return true;
		}
		bool operator!=(const ValueClass& other) const
		{
			return !(*this == other);
		}

	public:
		IdtAtomic<double> val; // 直接值
		IdtAtomic<double> ary; // 变换精度

		// 适用于 恒定速度模式 和 回弹动效模式
		IdtAtomic<double> spe; // 恒定速度 px/s
		IdtAtomic<double> startV, endV; // 起始位置与目标位置
	} setting;
};

// 单个 UI 控件
class BarUiWidgetClass
{
public:
	BarUiWidgetClass() {}

public:
	BarUiValueClass x;
	BarUiValueClass y;
	BarUiValueClass w;
	BarUiValueClass h;
	BarUiValueClass rw;
	BarUiValueClass rh;
};

class BarUISetClass
{
public:
	void Rendering();

public:
	BarMediaClass barMedia;
};

// 交互

// 初始化

class BarInitializationClass
{
public:
	void Initialization();

protected:
	void InitializeWindow();
	void InitializeMedia();
	void InitializeUI();

public:
	BarUISetClass barUISet;
};
extern BarInitializationClass barInitialization;