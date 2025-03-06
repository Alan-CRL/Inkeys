#pragma once

#include "IdtMain.h"

// 窗口

class BarWindowPosClass
{
public:
	BarWindowPosClass()
	{
		x.store(0), y.store(0);
		w.store(0), h.store(0);
		pct.store(0);
	}

	BarWindowPosClass(const BarWindowPosClass& other) { cF(other); }
	BarWindowPosClass& operator=(const BarWindowPosClass& other)
	{
		if (this != &other) cF(other);
		return *this;
	}
private:
	void cF(const BarWindowPosClass& other)
	{
		x.store(other.x.load());
		y.store(other.y.load());

		w.store(other.w.load());
		h.store(other.h.load());

		pct.store(other.pct.load());
	}

public:
	atomic<unsigned int> x, y;
	atomic<unsigned int> w, h;
	atomic<unsigned int> pct;
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
	Once = 0,
	Constant = 1,
	Variable = 2
};

class BarUiValueClass
{
public:
	BarUiValueClass() { mod.store(BarUiValueModeClass::Once); }
	static BarUiValueClass BarUiValueClassModeOnce(double val, double ary)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Once;

			obj.setting.val = val;
			obj.setting.val = ary;

			obj.current = obj.setting;
		}
		return obj;
	}
	static BarUiValueClass BarUiValueClassModeConstant(double val, double ary, double spe)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Constant;

			obj.setting.val = val;
			obj.setting.val = ary;
			obj.setting.spe = spe;

			obj.current = obj.setting;
		}
		return obj;
	}
	static BarUiValueClass BarUiValueClassModeVariable(double val, double ary, double acc, double backAcc)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Variable;

			obj.setting.val = val;
			obj.setting.val = ary;
			obj.setting.val = acc;
			obj.setting.val = backAcc;

			obj.current = obj.setting;
		}
		return obj;
	}

	BarUiValueClass(const BarUiValueClass& other) { cF(other); }
	BarUiValueClass& operator=(const BarUiValueClass& other)
	{
		if (this != &other) cF(other);
		return *this;
	}
private:
	void cF(const BarUiValueClass& other)
	{
		mod.store(other.mod.load());

		current = other.current;
		setting = other.setting;
	}

public:
	atomic<BarUiValueModeClass> mod;

	class ValueClass
	{
	public:
		ValueClass()
		{
			val.store(0);
			ary.store(0);

			spe.store(0);

			acc.store(0);
			backAcc.store(0);
		}

		ValueClass(const ValueClass& other) { cF(other); }
		ValueClass& operator=(const ValueClass& other)
		{
			if (this != &other) cF(other);
			return *this;
		}

		// 仅是完全比较
		bool operator==(const ValueClass& other) const
		{
			if (val != other.val) return false;
			if (ary != other.ary) return false;
			if (spe != other.spe) return false;
			if (acc != other.acc) return false;
			if (backAcc != other.backAcc) return false;
			return true;
		}
		bool operator!=(const ValueClass& other) const
		{
			return !(*this == other);
		}
	private:
		void cF(const ValueClass& other)
		{
			val.store(other.val.load());
			ary.store(other.ary.load());

			spe.store(other.spe.load());

			acc.store(other.acc.load());
			backAcc.store(other.backAcc.load());
		}

	public:
		atomic<double> val; // 直接值
		atomic<double> ary; // 精度

		// MODE1

		atomic<double> spe; // 恒定速度

		// MODE2

		atomic<double> acc; // 加速度
		atomic<double> backAcc; // 加速度(回弹减速阶段)
	} current, setting;

public:
	bool CompareValue() { return current == setting; }
	bool CompareValueWithMode()
	{
		if (mod == BarUiValueModeClass::Once) return CompareValueWithModeOnce();
		else if (mod == BarUiValueModeClass::Constant) return CompareValueWithModeConstant();
		else if (mod == BarUiValueModeClass::Variable) return CompareValueWithModeVariable();
		else CompareValue();
	}
	bool CompareValueWithModeOnce() { return current.val == setting.val && current.ary == setting.ary; }
	bool CompareValueWithModeConstant() { return current.val == setting.val && current.ary == setting.ary && current.spe == setting.spe; }
	bool CompareValueWithModeVariable() { return current.val == setting.val && current.ary == setting.ary && current.acc == setting.acc && current.backAcc == setting.backAcc; }

	void SetBarUiUnit(double valT, double setAccT);
};

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