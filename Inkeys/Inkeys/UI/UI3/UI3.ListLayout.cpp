module;

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

module Inkeys.UI.UI3.ListLayout;

namespace Inkeys::UI::UI3
{

ListLayoutResult ResolveListLayout(const std::vector<ListLayoutItem>& items,
	const ListLayoutSpec& spec, const ListLayoutMetrics& metrics)
{
	ListLayoutResult layout;
	const double gap = metrics.gap;
	const double one = metrics.one, two = metrics.two;
	const double oneStep = one + gap, twoStep = two + gap;
	if (spec.kind == ListLayoutKind::FixedGrid)
	{
		const int columns = (std::max)(1, spec.columns);
		int index = 0;
		for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
		{
			const auto& item = items[itemIndex];
			if (!item.visible) continue;
			const int column = index % columns, row = index / columns;
			ListLayoutPlacement placement;
			placement.itemIndex = itemIndex;
			placement.column = column * spec.cellColumnSpan; placement.row = row * spec.cellRowSpan;
			placement.columnSpan = spec.cellColumnSpan; placement.rowSpan = spec.cellRowSpan;
			placement.width = item.width; placement.height = item.height; placement.separator = item.separator;
			placement.x = column * twoStep + two / 2; placement.y = row * twoStep + two / 2;
			layout.placements.push_back(std::move(placement)); ++index;
		}
		const int rows = (index + columns - 1) / columns;
		layout.columns = index ? columns * spec.cellColumnSpan : 0; layout.rows = rows * spec.cellRowSpan;
		layout.width = index ? columns * two + (columns - 1) * gap : 0;
		layout.height = rows ? rows * two + (rows - 1) * gap : 0;
		return layout;
	}
	if (spec.kind == ListLayoutKind::GroupedGrid && spec.packing == ListGridPacking::DenseRows)
	{
		layout.columns = (std::max)(1, spec.columns);
		std::vector<std::vector<bool>> occupied;
		int previousEnd = 0;
		for (std::size_t groupIndex = 0; groupIndex < spec.groups.size(); ++groupIndex)
		{
			const auto& group = spec.groups[groupIndex];
			const int start = group.startNewRows ? previousEnd : 0;
			const int groupColumns = (std::max)(1, group.columns);
			layout.columns = (std::max)(layout.columns, groupColumns);
			for (auto& row : occupied) row.resize(layout.columns, false);
			int used = start;
			// 显隐是否过滤由调用方决定；密集打包保留输入序列中的每一项。
			for (std::size_t itemIndex = group.begin; itemIndex < group.end; ++itemIndex)
			{
				const auto& item = items[itemIndex];
				const int columns = item.columnSpan, rows = item.rowSpan;
				bool placed = false;
				for (int row = start; !placed; ++row)
				{
					while (occupied.size() < static_cast<std::size_t>(row + rows))
						occupied.emplace_back(layout.columns, false);
					for (int column = 0; column + columns <= groupColumns; ++column)
					{
						bool free = true;
						for (int y = 0; free && y < rows; ++y)
							for (int x = 0; x < columns; ++x)
								if (occupied[row + y][column + x]) { free = false; break; }
						if (!free) continue;
						for (int y = 0; y < rows; ++y)
							for (int x = 0; x < columns; ++x) occupied[row + y][column + x] = true;
						ListLayoutPlacement placement;
						placement.itemIndex = itemIndex; placement.groupIndex = groupIndex;
						placement.column = column; placement.row = row;
						placement.columnSpan = columns; placement.rowSpan = rows;
						placement.width = item.width; placement.height = item.height; placement.separator = item.separator;
						layout.placements.push_back(std::move(placement));
						used = (std::max)(used, row + rows); placed = true; break;
					}
				}
			}
			const int alignment = (std::max)(1, group.endRowAlignment);
			if (used % alignment) used += alignment - used % alignment;
			layout.groups.push_back({start, used}); previousEnd = used;
			layout.rows = (std::max)(layout.rows, used);
			layout.showDivider |= group.separatorBefore;
		}
		layout.rows = (std::max)(spec.minimumRows, layout.rows);
		layout.width = layout.columns * one + (layout.columns - 1) * gap;
		layout.height = layout.rows * one + (layout.rows - 1) * gap;
		return layout;
	}

	double x = gap, y = gap, width = gap;
	layout.height = spec.height;
	for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
	{
		const auto& item = items[itemIndex];
		ListLayoutPlacement p; p.itemIndex = itemIndex;
		const int columns = item.columnSpan, rows = item.rowSpan;
		p.columnSpan = columns; p.rowSpan = rows;
		p.width = item.width; p.height = item.height; p.separator = item.separator;
		const bool visible = item.visible;
		if (spec.kind == ListLayoutKind::SingleRow)
		{
			p.x = x + p.width / 2.0; p.y = gap + item.centerHeight.value_or(p.height) / 2.0;
			if (visible) x += p.width + gap;
			width = x;
		}
		else if (item.columnBoundary)
		{
			if (visible) { x = width; y = gap; }
			p.x = x - item.boundaryInsetX; p.y = y + two / 2.0;
		}
		else if (columns == 1 && rows == 1)
		{
			p.x = x + one / 2.0; p.y = y + one / 2.0;
			if (visible)
			{
				if (y <= gap) { y += oneStep; width += oneStep; }
				else if (x + oneStep >= width) { x += oneStep; y = gap; }
				else x += oneStep;
			}
		}
		else if (columns == 2 && rows == 1)
		{
			if (y > gap && x + twoStep > width) { x = width; y = gap; }
			p.x = x + two / 2.0; p.y = y + one / 2.0;
			if (visible)
			{
				if (y <= gap) { y += oneStep; width += twoStep; }
				else { x += twoStep; y = gap; }
			}
		}
		else if (columns == 2 && rows == 2)
		{
			if (y > gap) { y = gap; x = width; }
			p.x = x + two / 2.0; p.y = y + two / 2.0;
			if (visible) { x += twoStep; width += twoStep; }
		}
		layout.placements.push_back(std::move(p));
	}
	layout.width = width;
	return layout;
}

}
