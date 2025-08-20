#include "IdtBarColor.h"

bool CompereColorRef(COLORREF col1, COLORREF col2, bool alpha)
{
	if (alpha)
	{
		return col1 == col2;
	}
	return (GetRValue(col1) == GetRValue(col2)) && (GetGValue(col1) == GetGValue(col2)) && (GetBValue(col1) == GetBValue(col2));
}