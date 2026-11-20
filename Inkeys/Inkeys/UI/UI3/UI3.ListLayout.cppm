module;

#include <cstddef>
#include <optional>
#include <vector>

export module Inkeys.UI.UI3.ListLayout;

import Inkeys.UI.UI3.Metrics;

export namespace Inkeys::UI::UI3
{

enum class ListLayoutKind { SingleRow, FixedGrid, GroupedGrid };
enum class ListGridPacking { DenseRows, PairedRows };

struct ListLayoutItem
{
	double width = ButtonOneSideDip, height = ButtonOneSideDip;
	int columnSpan = 1, rowSpan = 1;
	bool visible = true, separator = false;
	// 单行分隔项可按另一高度居中；双行流中的边界项只重置游标，不占格。
	std::optional<double> centerHeight;
	bool columnBoundary = false;
	double boundaryInsetX = 0.0;
	bool exclusiveColumn = false;
};

struct ListLayoutGroupSpec
{
	// [begin, end) 必须位于输入内；正数跨度须能放进本组列数。
	std::size_t begin = 0, end = 0;
	int columns = 1;
	bool startNewRows = false;
	int endRowAlignment = 1;
	bool separatorBefore = false;
};

struct ListLayoutSpec
{
	ListLayoutKind kind = ListLayoutKind::SingleRow;
	ListGridPacking packing = ListGridPacking::DenseRows;
	int columns = 1;
	// 定列网格使用 metrics.two 作为格边长，跨度仅描述子格坐标。
	int cellColumnSpan = 2, cellRowSpan = 2;
	int minimumRows = 1;
	double height = 0.0;
	std::vector<ListLayoutGroupSpec> groups;
};

struct ListLayoutMetrics
{
	double gap = ButtonGapDip;
	double one = ButtonOneSideDip, two = ButtonTwoSideDip;
};

struct ListLayoutPlacement
{
	std::size_t itemIndex = 0, groupIndex = 0;
	double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
	int column = 0, row = 0, columnSpan = 1, rowSpan = 1;
	bool separator = false;
};

struct ListLayoutGroupResult
{
	int startRow = 0, endRow = 0;
};

struct ListLayoutResult
{
	std::vector<ListLayoutPlacement> placements;
	double width = 0.0, height = 0.0;
	int columns = 0, rows = 0;
	bool showDivider = false;
	std::vector<ListLayoutGroupResult> groups;
};

// 定列网格过滤隐藏项；单行/双行流保留隐藏位置但不推进游标；密集网格保留全部输入。
// 单行/双行流的行列元数据保持零，密集网格的 x/y 保持零，由调用方映射坐标。
// 双行流接受 1×1、2×1、2×2 与显式边界项；输入只含值与序号，不持有控件。
[[nodiscard]] ListLayoutResult ResolveListLayout(const std::vector<ListLayoutItem>& items,
	const ListLayoutSpec& spec, const ListLayoutMetrics& metrics = {});

}
