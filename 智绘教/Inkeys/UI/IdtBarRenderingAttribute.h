#pragma once

#include "../../IdtMain.h"

#include "IdtBarUI.h"

class BarRenderingAttribute
{
public:
	static void UnionRectInPlace(RECT& target, const RECT& add)
	{
		// ����������Ч��ֱ�ӷ���
		if (add.left >= add.right || add.top >= add.bottom) return;
		// target �ǿվ��Σ�ֱ���滻
		if (target.left >= target.right || target.top >= target.bottom)
		{
			target = add;
			return;
		}

		target.left = min(target.left, add.left);
		target.top = min(target.top, add.top);
		target.right = max(target.right, add.right);
		target.bottom = max(target.bottom, add.bottom);
	}

	static RECT GetWeigetRect(const BarUiShapeClass& shape, double tarZoom);
	static RECT GetWeigetRect(const BarUiSuperellipseClass& superellipse, double tarZoom);
	static RECT GetWeigetRect(const BarUiSVGClass& svg, double tarZoom);
	static RECT GetWeigetRect(const BarUiWordClass& word, double tarZoom);
};