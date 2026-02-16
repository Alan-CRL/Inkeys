#include "IdtInputs.h"

#undef max
#undef min
#include "libcuckoo/cuckoohash_map.hh"

using DownMapType = libcuckoo::cuckoohash_map<BYTE, bool>;
static DownMapType* getDownMap()
{
	return reinterpret_cast<DownMapType*>(IdtInputs::downMap);
}

void* IdtInputs::downMap = new DownMapType;

void IdtInputs::SetKeyBoardDown(BYTE key, bool down)
{
	getDownMap()->insert_or_assign(key, down);
}
bool IdtInputs::IsKeyBoardDown(BYTE key)
{
	bool down = false;
	getDownMap()->find(key, down);
	return down;
}