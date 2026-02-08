#include "IdtBarRenderingAttribute.h"

#include "IdtBarState.h"

RECT BarRenderingAttribute::GetWeigetRect(const BarUiShapeClass& shape, double tarZoom)
{
	int ft = 0;
	if (shape.ft.has_value()) ft = static_cast<int>(ceil(shape.ft.value().val));

	RECT ret;
	ret.left = static_cast<LONG>(floor(shape.inhX * tarZoom) - ft);
	ret.top = static_cast<LONG>(floor(shape.inhY * tarZoom) - ft);
	ret.right = static_cast<LONG>(ceil((shape.inhX + shape.w.val) * tarZoom) + ft) + 1;
	ret.bottom = static_cast<LONG>(ceil((shape.inhY + shape.h.val) * tarZoom) + ft) + 1;

	return ret;
}
RECT BarRenderingAttribute::GetWeigetRect(const BarUiSuperellipseClass& superellipse, double tarZoom)
{
	int ft = 0;
	if (superellipse.ft.has_value()) ft = static_cast<int>(ceil(superellipse.ft.value().val));

	RECT ret;
	ret.left = static_cast<LONG>(floor(superellipse.inhX * tarZoom) - ft);
	ret.top = static_cast<LONG>(floor(superellipse.inhY * tarZoom) - ft);
	ret.right = static_cast<LONG>(ceil((superellipse.inhX + superellipse.w.val) * tarZoom) + ft) + 1;
	ret.bottom = static_cast<LONG>(ceil((superellipse.inhY + superellipse.h.val) * tarZoom) + ft) + 1;

	return ret;
}
RECT BarRenderingAttribute::GetWeigetRect(const BarUiSVGClass& svg, double tarZoom)
{
	RECT ret;
	ret.left = static_cast<LONG>(floor(svg.inhX * tarZoom));
	ret.top = static_cast<LONG>(floor(svg.inhY * tarZoom));
	ret.right = static_cast<LONG>(ceil((svg.inhX + svg.w.val) * tarZoom)) + 1;
	ret.bottom = static_cast<LONG>(ceil((svg.inhY + svg.h.val) * tarZoom)) + 1;
	return ret;
}
RECT BarRenderingAttribute::GetWeigetRect(const BarUiWordClass& word, double tarZoom)
{
	RECT ret;
	ret.left = static_cast<LONG>(floor(word.inhX * tarZoom));
	ret.top = static_cast<LONG>(floor(word.inhY * tarZoom));
	ret.right = static_cast<LONG>(ceil((word.inhX + word.w.val) * tarZoom)) + 1;
	ret.bottom = static_cast<LONG>(ceil((word.inhY + word.h.val) * tarZoom)) + 1;
	return ret;
}