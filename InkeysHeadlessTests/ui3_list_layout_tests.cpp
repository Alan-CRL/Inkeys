#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

import Inkeys.UI.UI3.ListLayout;

using namespace Inkeys::UI::UI3;

namespace
{
	int failures = 0;
	void Check(bool condition, std::string_view name)
	{
		if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
	}
	ListLayoutItem Item(int columns = 1, int rows = 1, bool visible = true)
	{
		ListLayoutItem item;
		item.columnSpan = columns; item.rowSpan = rows; item.visible = visible;
		item.width = columns == 2 ? 70.0 : 32.5;
		item.height = rows == 2 ? 70.0 : 32.5;
		return item;
	}
	ListLayoutItem Separator()
	{
		auto item = Item(); item.separator = true;
		item.width = 1.0; item.height = 28.5; item.centerHeight = 32.5;
		return item;
	}
	ListLayoutSpec Row()
	{
		ListLayoutSpec spec; spec.height = 80.0; return spec;
	}
	ListLayoutSpec Dense(std::size_t firstEnd, std::size_t end, bool separate = true)
	{
		ListLayoutSpec spec; spec.kind = ListLayoutKind::GroupedGrid;
		spec.columns = 4; spec.minimumRows = 1;
		spec.groups = {{0, firstEnd, 4, false, 2, false}, {firstEnd, end, 4, separate, 1, separate}};
		return spec;
	}
	bool Placement(const ListLayoutPlacement& p, std::size_t index, double x, double y,
		double width, double height, int column = 0, int row = 0, int columns = 1, int rows = 1,
		std::size_t group = 0, bool separator = false)
	{
		return p.itemIndex == index && p.groupIndex == group && p.x == x && p.y == y
			&& p.width == width && p.height == height && p.column == column && p.row == row
			&& p.columnSpan == columns && p.rowSpan == rows && p.separator == separator;
	}
	void TestSingleRow()
	{
		const auto layout = ResolveListLayout({Item(), Separator(), Item(2, 1, false), Item()}, Row());
		Check(layout.placements.size() == 4 && layout.width == 86.0 && layout.height == 80.0
			&& layout.columns == 0 && layout.rows == 0 && !layout.showDivider && layout.groups.empty(),
			"row keeps hidden placement but only visible items advance width");
		if (layout.placements.size() != 4) return;
		Check(Placement(layout.placements[0], 0, 21.25, 21.25, 32.5, 32.5)
			&& Placement(layout.placements[1], 1, 43.0, 21.25, 1.0, 28.5, 0, 0, 1, 1, 0, true)
			&& Placement(layout.placements[2], 2, 83.5, 21.25, 70.0, 32.5, 0, 0, 2, 1)
			&& Placement(layout.placements[3], 3, 64.75, 21.25, 32.5, 32.5),
			"row preserves divider center height and hidden item dimensions");
		auto hiddenSeparator = Separator(); hiddenSeparator.visible = false;
		const auto hidden = ResolveListLayout({hiddenSeparator, Item(2, 2)}, Row());
		Check(hidden.placements.size() == 2 && hidden.width == 80.0
			&& Placement(hidden.placements[1], 1, 40.0, 40.0, 70.0, 70.0, 0, 0, 2, 2),
			"hidden separator reserves no horizontal gap");
	}
	void TestFixedGrid()
	{
		ListLayoutSpec spec; spec.kind = ListLayoutKind::FixedGrid; spec.columns = 3;
		std::vector<ListLayoutItem> items(8, Item(2, 2)); items[1].visible = false;
		const auto layout = ResolveListLayout(items, spec);
		Check(layout.placements.size() == 7 && layout.width == 220.0 && layout.height == 220.0
			&& layout.columns == 6 && layout.rows == 6 && !layout.showDivider && layout.groups.empty(),
			"fixed grid filters hidden items and wraps at configured columns");
		if (layout.placements.size() != 7) return;
		constexpr std::array<std::size_t, 7> indices{0, 2, 3, 4, 5, 6, 7};
		constexpr std::array<double, 7> xs{35, 110, 185, 35, 110, 185, 35};
		constexpr std::array<double, 7> ys{35, 35, 35, 110, 110, 110, 185};
		constexpr std::array<int, 7> columns{0, 2, 4, 0, 2, 4, 0}, rows{0, 0, 0, 2, 2, 2, 4};
		for (std::size_t i = 0; i < indices.size(); ++i)
			Check(Placement(layout.placements[i], indices[i], xs[i], ys[i], 70, 70, columns[i], rows[i], 2, 2),
				"fixed grid keeps source identity and subcell row column spans");
		spec.columns = 6;
		const auto sixColumns = ResolveListLayout(std::vector<ListLayoutItem>(7, Item(2, 2)), spec);
		Check(sixColumns.placements.size() == 7 && sixColumns.width == 445 && sixColumns.height == 145
			&& sixColumns.columns == 12 && sixColumns.rows == 4
			&& Placement(sixColumns.placements[6], 6, 35, 110, 70, 70, 0, 2, 2, 2),
			"six configured columns preserve category grid extent and wrap");
	}
	void TestGroupedGrid()
	{
		auto separator = Item(1, 2); separator.width = 10; separator.separator = true;
		const auto layout = ResolveListLayout({Item(2, 1), separator, Item(), Item(2, 2), Item(2, 1, false)}, Dense(3, 5));
		Check(layout.placements.size() == 5 && layout.width == 145 && layout.height == 145
			&& layout.columns == 4 && layout.rows == 4 && layout.showDivider && layout.groups.size() == 2,
			"grouped grid retains hidden overflow items and separator metadata");
		if (layout.placements.size() != 5 || layout.groups.size() != 2) return;
		Check(layout.groups[0].startRow == 0 && layout.groups[0].endRow == 2
			&& layout.groups[1].startRow == 2 && layout.groups[1].endRow == 4,
			"group boundary starts after aligned first group rows");
		Check(Placement(layout.placements[0], 0, 0, 0, 70, 32.5, 0, 0, 2, 1)
			&& Placement(layout.placements[1], 1, 0, 0, 10, 70, 2, 0, 1, 2, 0, true)
			&& Placement(layout.placements[2], 2, 0, 0, 32.5, 32.5, 3, 0)
			&& Placement(layout.placements[3], 3, 0, 0, 70, 70, 0, 2, 2, 2, 1)
			&& Placement(layout.placements[4], 4, 0, 0, 70, 32.5, 2, 2, 2, 1, 1),
			"dense packing keeps zero xy and exposes row column for caller positioning");
		// 固定数值夹具同时覆盖首组溢出、末行对齐和次组继续溢出。
		const auto overflow = ResolveListLayout(std::vector<ListLayoutItem>(6, Item(2, 2)), Dense(3, 6));
		Check(overflow.placements.size() == 6 && overflow.groups.size() == 2 && overflow.groups[0].endRow == 4
			&& overflow.groups[1].startRow == 4 && overflow.rows == 8 && overflow.height == 295
			&& Placement(overflow.placements[2], 2, 0, 0, 70, 70, 0, 2, 2, 2)
			&& Placement(overflow.placements[5], 5, 0, 0, 70, 70, 0, 6, 2, 2, 1),
			"each group overflows to later rows without crossing the separator");
		auto alignedSpec = Dense(1, 2);
		const auto aligned = ResolveListLayout({Item(), Item()}, alignedSpec);
		Check(aligned.groups.size() == 2 && aligned.groups[0].endRow == 2 && aligned.groups[1].startRow == 2 && aligned.rows == 3,
			"odd first group row is rounded up before second group");
		const auto secondOnly = ResolveListLayout({Item()}, Dense(0, 1, false));
		Check(secondOnly.placements.size() == 1 && !secondOnly.showDivider && secondOnly.rows == 1
			&& Placement(secondOnly.placements[0], 0, 0, 0, 32.5, 32.5, 0, 0, 1, 1, 1),
			"second group alone starts at zero without a divider");
		auto differentColumns = Dense(1, 4); differentColumns.groups[1].columns = 2;
		const auto narrowGroup = ResolveListLayout({Item(), Item(2, 2), Item(2, 2), Item(2, 2)}, differentColumns);
		Check(narrowGroup.placements.size() == 4 && narrowGroup.columns == 4 && narrowGroup.rows == 8
			&& Placement(narrowGroup.placements[3], 3, 0, 0, 70, 70, 0, 6, 2, 2, 1),
			"caller may constrain columns independently for each group");
	}
	void TestPairedRows()
	{
		auto spec = Row(); spec.kind = ListLayoutKind::GroupedGrid; spec.packing = ListGridPacking::PairedRows;
		auto boundary = Item(1, 2); boundary.separator = boundary.columnBoundary = true;
		boundary.width = 1; boundary.height = 50; boundary.boundaryInsetX = 2.5;
		auto hiddenBoundary = boundary; hiddenBoundary.visible = false;
		const auto layout = ResolveListLayout({Item(), Item(2, 1, false), Item(), Item(), Item(2, 1), boundary,
			Item(2, 2, false), Item(2, 2), Item(2, 1), Item(2, 1), Item(), hiddenBoundary, Item(2, 2, false)}, spec);
		Check(layout.placements.size() == 13 && layout.width == 342.5 && layout.height == 80
			&& layout.columns == 0 && layout.rows == 0 && !layout.showDivider && layout.groups.empty(),
			"paired rows preserve compact flow width and default aggregate row metadata");
		if (layout.placements.size() != 13) return;
		constexpr std::array<double, 13> xs{21.25, 77.5, 58.75, 58.75, 115, 152.5, 190, 190, 265, 265, 321.25, 302.5, 377.5};
		constexpr std::array<double, 13> ys{21.25, 21.25, 21.25, 58.75, 21.25, 40, 40, 40, 21.25, 58.75, 21.25, 77.5, 40};
		constexpr std::array<double, 13> widths{32.5, 70, 32.5, 32.5, 70, 1, 70, 70, 70, 70, 32.5, 1, 70};
		constexpr std::array<double, 13> heights{32.5, 32.5, 32.5, 32.5, 32.5, 50, 70, 70, 32.5, 32.5, 32.5, 50, 70};
		constexpr std::array<int, 13> columns{1, 2, 1, 1, 2, 1, 2, 2, 2, 2, 1, 1, 2}, rows{1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2};
		for (std::size_t i = 0; i < xs.size(); ++i)
			Check(Placement(layout.placements[i], i, xs[i], ys[i], widths[i], heights[i], 0, 0, columns[i], rows[i], 0, i == 5 || i == 11),
				"paired rows retain hidden cursor resets and caller supplied boundary geometry");
	}
	void TestEmptyAndSeparators()
	{
		const auto row = ResolveListLayout({}, Row());
		Check(row.placements.empty() && row.width == 5 && row.height == 80, "empty row retains leading gap and supplied height");
		ListLayoutSpec spec; spec.kind = ListLayoutKind::FixedGrid;
		const auto grid = ResolveListLayout({}, spec);
		Check(grid.placements.empty() && grid.width == 0 && grid.height == 0 && grid.columns == 0 && grid.rows == 0,
			"empty fixed grid has no extent");
		spec = Dense(0, 0, false); spec.columns = 2;
		for (auto& group : spec.groups) group.columns = 2;
		const auto dense = ResolveListLayout({}, spec);
		Check(dense.placements.empty() && dense.width == 70 && dense.height == 32.5 && dense.columns == 2 && dense.rows == 1
			&& !dense.showDivider && dense.groups.size() == 2 && dense.groups[0].endRow == 0,
			"empty dense grid honors minimum rows without inventing group rows");
		spec = Row(); spec.kind = ListLayoutKind::GroupedGrid; spec.packing = ListGridPacking::PairedRows;
		const auto paired = ResolveListLayout({}, spec);
		Check(paired.placements.empty() && paired.width == 5 && paired.height == 80, "empty paired rows retain frame extent");
		const auto separators = ResolveListLayout({Separator(), Separator()}, Row());
		Check(separators.placements.size() == 2 && separators.width == 17 && separators.height == 80
			&& Placement(separators.placements[0], 0, 5.5, 21.25, 1, 28.5, 0, 0, 1, 1, 0, true)
			&& Placement(separators.placements[1], 1, 11.5, 21.25, 1, 28.5, 0, 0, 1, 1, 0, true),
			"separator only row keeps line dimensions and spacing");
	}
}

int RunUi3ListLayoutTests()
{
	TestSingleRow(); TestFixedGrid(); TestGroupedGrid(); TestPairedRows(); TestEmptyAndSeparators();
	return failures;
}
