module;

#include "../../IdtMain.h"

#undef max
#undef min
#include "libcuckoo/cuckoohash_map.hh"

export module Inkeys.Thread.Status;

namespace Inkeys::Thread
{
	//线程状态管理
	libcuckoo::cuckoohash_map<string, bool> threadStatus;

	// 设置线程状态
	void SetStatus(const string& key, bool down)
	{
		threadStatus.insert_or_assign(key, down);
	}
	// 获取线程状态
	export bool GetStatus(const string& key)
	{
		bool down = false;
		threadStatus.find(key, down);
		return down;
	}

	// 线程状态守卫类，构造时设置状态为 true，析构时设置状态为 false
	export class StatusGuard
	{
	public:
		StatusGuard(const string& keyInput)
		{
			key = keyInput;
			SetStatus(key, true);
		}
		~StatusGuard()
		{
			SetStatus(key, false);
		}

		StatusGuard(const StatusGuard&) = delete;
		StatusGuard& operator=(const StatusGuard&) = delete;

	private:
		string key;
	};
}