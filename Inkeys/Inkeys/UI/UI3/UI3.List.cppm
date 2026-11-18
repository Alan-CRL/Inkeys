module;

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

export module Inkeys.UI.UI3:List;

export import Inkeys.UI.UI3.ListLayout;

export namespace Inkeys::UI::UI3
{

template<class T>
class ButtonListOf
{
public:
	T* Get(int x)
	{
		std::shared_lock lock(mt);
		if (x >= 0 && x < list.size()) return list.at(x).get();
		return nullptr;
	}
	// 一次持锁取得强引用快照，替换列表后快照仍持有原元素。
	[[nodiscard]] std::vector<std::shared_ptr<T>> Snapshot() const
	{
		std::shared_lock lock(mt);
		return list;
	}
	void Replace(std::vector<std::shared_ptr<T>> next)
	{
		std::unique_lock lock(mt);
		list = std::move(next);
	}

	bool Swap(int x, int y)
	{
		std::unique_lock lock(mt);
		if (!(x >= 0 && x < list.size())) return false;
		if (!(y >= 0 && y < list.size())) return false;
		if (x == y) return false;

		std::swap(list[x], list[y]);
		return true;
	}

protected:
	mutable std::shared_mutex mt;
	std::vector<std::shared_ptr<T>> list;
};

}
