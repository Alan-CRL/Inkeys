module;

#include "../../IdtMain.h"

#undef max
#undef min
#include "libcuckoo/cuckoohash_map.hh"

export module Inkeys.Other.Inputs;

libcuckoo::cuckoohash_map<BYTE, bool> downMap;

namespace Inkeys::Inputs
{
	export void SetKeyBoardDown(BYTE key, bool down)
	{
		downMap.insert_or_assign(key, down);
	}
	export bool IsKeyBoardDown(BYTE key)
	{
		bool down = false;
		downMap.find(key, down);
		return down;
	}
}