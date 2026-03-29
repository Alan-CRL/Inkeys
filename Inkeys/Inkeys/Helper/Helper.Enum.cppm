module;

export module Inkeys.Helper.Enum;

export import <magic_enum/magic_enum.hpp>;
export import <array>;

using namespace std;

export template<typename E, typename T>
struct InkeysEnumArray
{
	// 内部依然是连续内存的线性数组
	array<T, magic_enum::enum_count<E>()> data{};

	// 魔法在这里：重载 []，内部自动调用 magic_enum 的转换
	T& operator[](E e)
	{
		return data[magic_enum::enum_integer(e)];
	}

	const T& operator[](E e) const
	{
		return data[magic_enum::enum_integer(e)];
	}
};