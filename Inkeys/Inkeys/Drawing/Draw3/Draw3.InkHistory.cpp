module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module Inkeys.Drawing.Draw3.ink_history;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr double kFootprintAaPadding = 2.0;
		constexpr double kHighlighterHalfWidth = 1.25;
		constexpr double kHighlighterHalfHeight = 25.0;

		struct PointD
		{
			double x = 0.0;
			double y = 0.0;
		};

		struct RectD
		{
			double left = 0.0;
			double top = 0.0;
			double right = 0.0;
			double bottom = 0.0;
		};

		struct TileRange
		{
			int32_t firstX = 0;
			int32_t lastX = -1;
			int32_t firstY = 0;
			int32_t lastY = -1;
		};

		bool IsValidBounds(const InkPixelBounds& bounds) noexcept
		{
			return std::isfinite(bounds.left) && std::isfinite(bounds.top) &&
				std::isfinite(bounds.right) && std::isfinite(bounds.bottom) &&
				bounds.left < bounds.right && bounds.top < bounds.bottom;
		}

		bool IsPowerOfTwo(uint32_t value) noexcept
		{
			return value != 0 && (value & (value - 1)) == 0;
		}

		uint32_t RootBlockCapacity(size_t itemCount) noexcept
		{
			if (itemCount == 0) return 0;
			const uint64_t blockCount =
				(itemCount + kCompositionLeafItemCount - 1) / kCompositionLeafItemCount;
			uint32_t capacity = 1;
			while (capacity < blockCount && capacity <= (std::numeric_limits<uint32_t>::max)() / 2)
				capacity *= 2;
			return capacity;
		}

		size_t CapacityFromBytes(uint64_t byteBudget, uint64_t bytesPerSlot) noexcept
		{
			if (bytesPerSlot == 0) return 0;
			const uint64_t slots = byteBudget / bytesPerSlot;
			const uint64_t maximum = static_cast<uint64_t>((std::numeric_limits<size_t>::max)());
			return static_cast<size_t>((std::min)(slots, maximum));
		}

		size_t PageCountForSlots(size_t slotCount, size_t slotsPerPage) noexcept
		{
			if (slotCount == 0 || slotsPerPage == 0) return 0;
			return 1 + (slotCount - 1) / slotsPerPage;
		}

		void NormalizeTiles(std::vector<SignedTileCoordinate>& tiles)
		{
			std::sort(tiles.begin(), tiles.end());
			tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
		}

		bool ContainsTile(std::span<const SignedTileCoordinate> tiles,
			SignedTileCoordinate tile) noexcept
		{
			return std::binary_search(tiles.begin(), tiles.end(), tile);
		}

		bool TryFloorTile(double coordinate, uint32_t tileSize, int32_t& tile) noexcept
		{
			if (!std::isfinite(coordinate) || tileSize == 0) return false;
			const double value = std::floor(coordinate / static_cast<double>(tileSize));
			if (value < static_cast<double>((std::numeric_limits<int32_t>::min)()) ||
				value > static_cast<double>((std::numeric_limits<int32_t>::max)())) return false;
			tile = static_cast<int32_t>(value);
			return true;
		}

		bool TryCeilTileExclusive(double coordinate, uint32_t tileSize, int32_t& tile) noexcept
		{
			if (!std::isfinite(coordinate) || tileSize == 0) return false;
			const double value = std::ceil(coordinate / static_cast<double>(tileSize)) - 1.0;
			if (value < static_cast<double>((std::numeric_limits<int32_t>::min)()) ||
				value > static_cast<double>((std::numeric_limits<int32_t>::max)())) return false;
			tile = static_cast<int32_t>(value);
			return true;
		}

		bool TryMakeTileRange(const InkPixelBounds& bounds,
			uint32_t tileSize, TileRange& range) noexcept
		{
			if (!IsValidBounds(bounds) || tileSize == 0) return false;
			return TryFloorTile(bounds.left, tileSize, range.firstX) &&
				TryCeilTileExclusive(bounds.right, tileSize, range.lastX) &&
				TryFloorTile(bounds.top, tileSize, range.firstY) &&
				TryCeilTileExclusive(bounds.bottom, tileSize, range.lastY) &&
				range.firstX <= range.lastX && range.firstY <= range.lastY;
		}

		bool TileOverlapsVisible(SignedTileCoordinate tile,
			uint32_t tileSize, const InkPixelBounds& visible) noexcept
		{
			const double left = static_cast<double>(tile.x) * tileSize;
			const double top = static_cast<double>(tile.y) * tileSize;
			const double right = left + tileSize;
			const double bottom = top + tileSize;
			return left < visible.right && right > visible.left &&
				top < visible.bottom && bottom > visible.top;
		}

		bool ClipSegment(PointD& first, PointD& second, const RectD& rect) noexcept
		{
			const double dx = second.x - first.x;
			const double dy = second.y - first.y;
			double minimum = 0.0;
			double maximum = 1.0;
			auto clip = [&](double p, double q)
			{
				if (p == 0.0) return q >= 0.0;
				const double ratio = q / p;
				if (p < 0.0)
				{
					if (ratio > maximum) return false;
					if (ratio > minimum) minimum = ratio;
				}
				else
				{
					if (ratio < minimum) return false;
					if (ratio < maximum) maximum = ratio;
				}
				return true;
			};
			if (!clip(-dx, first.x - rect.left) ||
				!clip(dx, rect.right - first.x) ||
				!clip(-dy, first.y - rect.top) ||
				!clip(dy, rect.bottom - first.y)) return false;
			const PointD originalFirst = first;
			first = { originalFirst.x + minimum * dx, originalFirst.y + minimum * dy };
			second = { originalFirst.x + maximum * dx, originalFirst.y + maximum * dy };
			return std::isfinite(first.x) && std::isfinite(first.y) &&
				std::isfinite(second.x) && std::isfinite(second.y);
		}

		bool SegmentIntersectsExpandedTile(PointD first, PointD second,
			SignedTileCoordinate tile, uint32_t tileSize,
			double expandX, double expandY) noexcept
		{
			RectD rect;
			rect.left = static_cast<double>(tile.x) * tileSize - expandX;
			rect.top = static_cast<double>(tile.y) * tileSize - expandY;
			rect.right = rect.left + tileSize + expandX * 2.0;
			rect.bottom = rect.top + tileSize + expandY * 2.0;
			return ClipSegment(first, second, rect);
		}

		bool PointIntersectsExpandedTile(PointD point,
			SignedTileCoordinate tile, uint32_t tileSize,
			double expandX, double expandY) noexcept
		{
			const double left = static_cast<double>(tile.x) * tileSize - expandX;
			const double top = static_cast<double>(tile.y) * tileSize - expandY;
			return point.x >= left && point.x <= left + tileSize + expandX * 2.0 &&
				point.y >= top && point.y <= top + tileSize + expandY * 2.0;
		}

		template<typename Predicate>
		void AddVisibleTileRange(const TileRange& range,
			std::vector<SignedTileCoordinate>& output, Predicate&& predicate)
		{
			for (int64_t y = range.firstY; y <= range.lastY; ++y)
			{
				for (int64_t x = range.firstX; x <= range.lastX; ++x)
				{
					const SignedTileCoordinate tile{
						static_cast<int32_t>(x), static_cast<int32_t>(y) };
					if (predicate(tile)) output.push_back(tile);
				}
			}
		}

		bool AddPointTiles(PointD point, double expandX, double expandY,
			uint32_t tileSize, const InkPixelBounds& visible,
			const TileRange& visibleTiles, std::vector<SignedTileCoordinate>& output)
		{
			const RectD expandedVisible{
				static_cast<double>(visible.left) - expandX,
				static_cast<double>(visible.top) - expandY,
				static_cast<double>(visible.right) + expandX,
				static_cast<double>(visible.bottom) + expandY
			};
			if (point.x < expandedVisible.left || point.x > expandedVisible.right ||
				point.y < expandedVisible.top || point.y > expandedVisible.bottom) return true;

			int32_t centerX = 0;
			int32_t centerY = 0;
			if (!TryFloorTile(point.x, tileSize, centerX) ||
				!TryFloorTile(point.y, tileSize, centerY))
			{
				AddVisibleTileRange(visibleTiles, output, [&](SignedTileCoordinate tile)
				{
					return PointIntersectsExpandedTile(
						point, tile, tileSize, expandX, expandY);
				});
				return true;
			}

			const double xRadiusValue = std::ceil(expandX / tileSize) + 1.0;
			const double yRadiusValue = std::ceil(expandY / tileSize) + 1.0;
			if (!std::isfinite(xRadiusValue) || !std::isfinite(yRadiusValue)) return false;
			const int64_t visibleColumns =
				static_cast<int64_t>(visibleTiles.lastX) - visibleTiles.firstX + 1;
			const int64_t visibleRows =
				static_cast<int64_t>(visibleTiles.lastY) - visibleTiles.firstY + 1;
			const int64_t xRadius = static_cast<int64_t>((std::min)(
				xRadiusValue, static_cast<double>(visibleColumns)));
			const int64_t yRadius = static_cast<int64_t>((std::min)(
				yRadiusValue, static_cast<double>(visibleRows)));
			const int64_t firstY = (std::max)(static_cast<int64_t>(visibleTiles.firstY),
				static_cast<int64_t>(centerY) - yRadius);
			const int64_t lastY = (std::min)(static_cast<int64_t>(visibleTiles.lastY),
				static_cast<int64_t>(centerY) + yRadius);
			const int64_t firstX = (std::max)(static_cast<int64_t>(visibleTiles.firstX),
				static_cast<int64_t>(centerX) - xRadius);
			const int64_t lastX = (std::min)(static_cast<int64_t>(visibleTiles.lastX),
				static_cast<int64_t>(centerX) + xRadius);
			for (int64_t y = firstY; y <= lastY; ++y)
			{
				for (int64_t x = firstX; x <= lastX; ++x)
				{
					const SignedTileCoordinate tile{
						static_cast<int32_t>(x), static_cast<int32_t>(y) };
					if (TileOverlapsVisible(tile, tileSize, visible) &&
						PointIntersectsExpandedTile(point, tile, tileSize, expandX, expandY))
						output.push_back(tile);
				}
			}
			return true;
		}

		bool AddSegmentTiles(PointD originalFirst, PointD originalSecond,
			double expandX, double expandY, uint32_t tileSize,
			const InkPixelBounds& visible, const TileRange& visibleTiles,
			std::vector<SignedTileCoordinate>& output)
		{
			PointD first = originalFirst;
			PointD second = originalSecond;
			const RectD expandedVisible{
				static_cast<double>(visible.left) - expandX,
				static_cast<double>(visible.top) - expandY,
				static_cast<double>(visible.right) + expandX,
				static_cast<double>(visible.bottom) + expandY
			};
			if (!ClipSegment(first, second, expandedVisible)) return true;

			int32_t tileX = 0;
			int32_t tileY = 0;
			int32_t endX = 0;
			int32_t endY = 0;
			if (!TryFloorTile(first.x, tileSize, tileX) ||
				!TryFloorTile(first.y, tileSize, tileY) ||
				!TryFloorTile(second.x, tileSize, endX) ||
				!TryFloorTile(second.y, tileSize, endY))
			{
				AddVisibleTileRange(visibleTiles, output, [&](SignedTileCoordinate tile)
				{
					return SegmentIntersectsExpandedTile(originalFirst, originalSecond,
						tile, tileSize, expandX, expandY);
				});
				return true;
			}

			const uint64_t stepEstimate = static_cast<uint64_t>(
				std::abs(static_cast<int64_t>(endX) - tileX) +
				std::abs(static_cast<int64_t>(endY) - tileY) + 1);
			const uint64_t visibleCount =
				(static_cast<uint64_t>(visibleTiles.lastX) - visibleTiles.firstX + 1) *
				(static_cast<uint64_t>(visibleTiles.lastY) - visibleTiles.firstY + 1);
			if (stepEstimate > visibleCount * 8ull + 1024ull)
			{
				AddVisibleTileRange(visibleTiles, output, [&](SignedTileCoordinate tile)
				{
					return SegmentIntersectsExpandedTile(originalFirst, originalSecond,
						tile, tileSize, expandX, expandY);
				});
				return true;
			}

			const int stepX = second.x > first.x ? 1 : second.x < first.x ? -1 : 0;
			const int stepY = second.y > first.y ? 1 : second.y < first.y ? -1 : 0;
			const double dx = second.x - first.x;
			const double dy = second.y - first.y;
			const double infinity = (std::numeric_limits<double>::infinity)();
			const double nextBoundaryX = stepX > 0
				? (static_cast<double>(tileX) + 1.0) * tileSize
				: static_cast<double>(tileX) * tileSize;
			const double nextBoundaryY = stepY > 0
				? (static_cast<double>(tileY) + 1.0) * tileSize
				: static_cast<double>(tileY) * tileSize;
			double maximumX = stepX == 0 ? infinity : (nextBoundaryX - first.x) / dx;
			double maximumY = stepY == 0 ? infinity : (nextBoundaryY - first.y) / dy;
			const double deltaX = stepX == 0 ? infinity : tileSize / std::abs(dx);
			const double deltaY = stepY == 0 ? infinity : tileSize / std::abs(dy);

			const double xRadiusValue = std::ceil(expandX / tileSize) + 1.0;
			const double yRadiusValue = std::ceil(expandY / tileSize) + 1.0;
			if (!std::isfinite(xRadiusValue) || !std::isfinite(yRadiusValue)) return false;
			const int64_t visibleColumns =
				static_cast<int64_t>(visibleTiles.lastX) - visibleTiles.firstX + 1;
			const int64_t visibleRows =
				static_cast<int64_t>(visibleTiles.lastY) - visibleTiles.firstY + 1;
			const int64_t xRadius = static_cast<int64_t>((std::min)(
				xRadiusValue, static_cast<double>(visibleColumns)));
			const int64_t yRadius = static_cast<int64_t>((std::min)(
				yRadiusValue, static_cast<double>(visibleRows)));

			for (uint64_t step = 0; step <= stepEstimate + 1; ++step)
			{
				const int64_t firstCandidateY = (std::max)(
					static_cast<int64_t>(visibleTiles.firstY),
					static_cast<int64_t>(tileY) - yRadius);
				const int64_t lastCandidateY = (std::min)(
					static_cast<int64_t>(visibleTiles.lastY),
					static_cast<int64_t>(tileY) + yRadius);
				const int64_t firstCandidateX = (std::max)(
					static_cast<int64_t>(visibleTiles.firstX),
					static_cast<int64_t>(tileX) - xRadius);
				const int64_t lastCandidateX = (std::min)(
					static_cast<int64_t>(visibleTiles.lastX),
					static_cast<int64_t>(tileX) + xRadius);
				for (int64_t y = firstCandidateY; y <= lastCandidateY; ++y)
				{
					for (int64_t x = firstCandidateX; x <= lastCandidateX; ++x)
					{
						const SignedTileCoordinate tile{
							static_cast<int32_t>(x), static_cast<int32_t>(y) };
						if (TileOverlapsVisible(tile, tileSize, visible) &&
							SegmentIntersectsExpandedTile(originalFirst, originalSecond,
								tile, tileSize, expandX, expandY)) output.push_back(tile);
					}
				}
				if (tileX == endX && tileY == endY) return true;
				if (maximumX < maximumY)
				{
					tileX += stepX;
					maximumX += deltaX;
				}
				else if (maximumY < maximumX)
				{
					tileY += stepY;
					maximumY += deltaY;
				}
				else
				{
					tileX += stepX;
					tileY += stepY;
					maximumX += deltaX;
					maximumY += deltaY;
				}
			}
			return false;
		}

		bool AddRectangleTiles(const RectD& originalBounds,
			uint32_t tileSize, const InkPixelBounds& visible,
			std::vector<SignedTileCoordinate>& output)
		{
			const double left = (std::max)(originalBounds.left,
				static_cast<double>(visible.left));
			const double top = (std::max)(originalBounds.top,
				static_cast<double>(visible.top));
			const double right = (std::min)(originalBounds.right,
				static_cast<double>(visible.right));
			const double bottom = (std::min)(originalBounds.bottom,
				static_cast<double>(visible.bottom));
			if (!(left < right && top < bottom)) return true;
			const InkPixelBounds clipped{ static_cast<float>(left), static_cast<float>(top),
				static_cast<float>(right), static_cast<float>(bottom) };
			TileRange range;
			if (!TryMakeTileRange(clipped, tileSize, range)) return false;
			AddVisibleTileRange(range, output, [&](SignedTileCoordinate tile)
			{
				return TileOverlapsVisible(tile, tileSize, visible);
			});
			return true;
		}

		std::optional<std::vector<SignedTileCoordinate>> BuildTiles(
			const InkStroke& stroke, uint32_t tileSize, const InkPixelBounds& visible)
		{
			TileRange visibleTiles;
			if (!TryMakeTileRange(visible, tileSize, visibleTiles)) return std::nullopt;
			std::vector<SignedTileCoordinate> result;
			const std::span<const StoredInkPoint> points = stroke.Points();
			const StoredInkType inkType = stroke.Style().inkType;
			if (IsStoredShapeType(inkType))
			{
				const PointD first{ points[0].x, points[0].y };
				const PointD second{ points[1].x, points[1].y };
				const double lineExpansion =
					static_cast<double>(points[0].width) * 0.5 + kFootprintAaPadding;
				bool succeeded = true;
				if (inkType == StoredInkType::SolidLine ||
					inkType == StoredInkType::DashedLine)
				{
					succeeded = first.x == second.x && first.y == second.y
						? AddPointTiles(first, lineExpansion, lineExpansion,
							tileSize, visible, visibleTiles, result)
						: AddSegmentTiles(first, second, lineExpansion, lineExpansion,
							tileSize, visible, visibleTiles, result);
				}
				else
				{
					const double left = (std::min)(first.x, second.x);
					const double top = (std::min)(first.y, second.y);
					const double right = (std::max)(first.x, second.x);
					const double bottom = (std::max)(first.y, second.y);
					if (inkType == StoredInkType::FilledRectangle)
					{
						succeeded = AddRectangleTiles({
							left - kFootprintAaPadding, top - kFootprintAaPadding,
							right + kFootprintAaPadding, bottom + kFootprintAaPadding },
							tileSize, visible, result);
					}
					else
					{
						const PointD topLeft{ left, top };
						const PointD topRight{ right, top };
						const PointD bottomRight{ right, bottom };
						const PointD bottomLeft{ left, bottom };
						succeeded = AddSegmentTiles(topLeft, topRight,
							lineExpansion, lineExpansion, tileSize, visible, visibleTiles, result) &&
							AddSegmentTiles(topRight, bottomRight,
								lineExpansion, lineExpansion, tileSize, visible, visibleTiles, result) &&
							AddSegmentTiles(bottomRight, bottomLeft,
								lineExpansion, lineExpansion, tileSize, visible, visibleTiles, result) &&
							AddSegmentTiles(bottomLeft, topLeft,
								lineExpansion, lineExpansion, tileSize, visible, visibleTiles, result);
					}
				}
				if (!succeeded) return std::nullopt;
				NormalizeTiles(result);
				return result;
			}
			const bool highlighter = stroke.Style().inkType == StoredInkType::Highlighter;
			const auto expansionForPoint = [&](const StoredInkPoint& point)
			{
				if (highlighter)
					return PointD{ kHighlighterHalfWidth + kFootprintAaPadding,
						kHighlighterHalfHeight + kFootprintAaPadding };
				const double radius = static_cast<double>(point.width) * 0.5 + kFootprintAaPadding;
				return PointD{ radius, radius };
			};

			if (points.size() == 1)
			{
				const PointD expansion = expansionForPoint(points.front());
				if (!AddPointTiles({ points.front().x, points.front().y },
					expansion.x, expansion.y, tileSize, visible, visibleTiles, result))
					return std::nullopt;
			}
			else
			{
				for (size_t index = 1; index < points.size(); ++index)
				{
					const PointD firstExpansion = expansionForPoint(points[index - 1]);
					const PointD secondExpansion = expansionForPoint(points[index]);
					const double expandX = (std::max)(firstExpansion.x, secondExpansion.x);
					const double expandY = (std::max)(firstExpansion.y, secondExpansion.y);
					const PointD first{ points[index - 1].x, points[index - 1].y };
					const PointD second{ points[index].x, points[index].y };
					if (first.x == second.x && first.y == second.y)
					{
						if (!AddPointTiles(first, expandX, expandY,
							tileSize, visible, visibleTiles, result)) return std::nullopt;
					}
					else if (!AddSegmentTiles(first, second, expandX, expandY,
						tileSize, visible, visibleTiles, result)) return std::nullopt;
				}
			}
			NormalizeTiles(result);
			return result;
		}

		std::optional<std::vector<SignedTileCoordinate>> BuildAllVisibleTiles(
			uint32_t tileSize, const InkPixelBounds& visible)
		{
			TileRange visibleTiles;
			if (!TryMakeTileRange(visible, tileSize, visibleTiles)) return std::nullopt;
			std::vector<SignedTileCoordinate> result;
			AddVisibleTileRange(visibleTiles, result, [&](SignedTileCoordinate tile)
			{
				return TileOverlapsVisible(tile, tileSize, visible);
			});
			return result;
		}

		std::optional<InkPixelBounds> ComputeStrokeBounds(const InkStroke& stroke) noexcept
		{
			const std::span<const StoredInkPoint> points = stroke.Points();
			if (!stroke.IsValid() || points.empty()) return std::nullopt;
			if (IsStoredShapeType(stroke.Style().inkType))
			{
				const double lineExpansion =
					static_cast<double>(points[0].width) * 0.5 + kFootprintAaPadding;
				const double expansion = stroke.Style().inkType == StoredInkType::FilledRectangle
					? kFootprintAaPadding : lineExpansion;
				const double left = (std::min)(static_cast<double>(points[0].x),
					static_cast<double>(points[1].x)) - expansion;
				const double top = (std::min)(static_cast<double>(points[0].y),
					static_cast<double>(points[1].y)) - expansion;
				const double right = (std::max)(static_cast<double>(points[0].x),
					static_cast<double>(points[1].x)) + expansion;
				const double bottom = (std::max)(static_cast<double>(points[0].y),
					static_cast<double>(points[1].y)) + expansion;
				const double floatMaximum = (std::numeric_limits<float>::max)();
				if (!std::isfinite(left) || !std::isfinite(top) ||
					!std::isfinite(right) || !std::isfinite(bottom) ||
					left < -floatMaximum || top < -floatMaximum ||
					right > floatMaximum || bottom > floatMaximum) return std::nullopt;
				const InkPixelBounds result{ static_cast<float>(left), static_cast<float>(top),
					static_cast<float>(right), static_cast<float>(bottom) };
				return IsValidBounds(result) ? std::optional<InkPixelBounds>(result) : std::nullopt;
			}
			double left = (std::numeric_limits<double>::infinity)();
			double top = (std::numeric_limits<double>::infinity)();
			double right = -(std::numeric_limits<double>::infinity)();
			double bottom = -(std::numeric_limits<double>::infinity)();
			const bool highlighter = stroke.Style().inkType == StoredInkType::Highlighter;
			for (const StoredInkPoint& point : points)
			{
				const double expandX = highlighter
					? kHighlighterHalfWidth + kFootprintAaPadding
					: static_cast<double>(point.width) * 0.5 + kFootprintAaPadding;
				const double expandY = highlighter
					? kHighlighterHalfHeight + kFootprintAaPadding : expandX;
				left = (std::min)(left, static_cast<double>(point.x) - expandX);
				top = (std::min)(top, static_cast<double>(point.y) - expandY);
				right = (std::max)(right, static_cast<double>(point.x) + expandX);
				bottom = (std::max)(bottom, static_cast<double>(point.y) + expandY);
			}
			const double floatMaximum = (std::numeric_limits<float>::max)();
			if (!std::isfinite(left) || !std::isfinite(top) ||
				!std::isfinite(right) || !std::isfinite(bottom) ||
				left < -floatMaximum || top < -floatMaximum ||
				right > floatMaximum || bottom > floatMaximum) return std::nullopt;
			const InkPixelBounds result{ static_cast<float>(left), static_cast<float>(top),
				static_cast<float>(right), static_cast<float>(bottom) };
			return IsValidBounds(result) ? std::optional<InkPixelBounds>(result) : std::nullopt;
		}

		bool IsValidNode(CompositionNodeId node) noexcept
		{
			return IsPowerOfTwo(node.blockCount) &&
				(node.firstBlock % node.blockCount) == 0;
		}
	}

	std::optional<StrokeTileFootprint> BuildStrokeTileFootprint(
		const InkStroke& stroke, std::optional<InkPixelBounds> visibleBounds)
	{
		if (!stroke.IsValid()) return std::nullopt;
		std::optional<InkPixelBounds> pixelBounds = ComputeStrokeBounds(stroke);
		if (!pixelBounds)
		{
			if (!visibleBounds || !IsValidBounds(*visibleBounds)) return std::nullopt;
			// float 极值附近无法表达笔宽时，保守覆盖当前可见区，不能丢失有效历史项。
			pixelBounds = *visibleBounds;
		}
		const InkPixelBounds clippedBounds = visibleBounds.value_or(*pixelBounds);
		if (!IsValidBounds(clippedBounds)) return std::nullopt;
		std::optional<std::vector<SignedTileCoordinate>> undoTiles =
			BuildTiles(stroke, kUndoTileSize, clippedBounds);
		std::optional<std::vector<SignedTileCoordinate>> compositionTiles =
			BuildTiles(stroke, kCompositionTileSize, clippedBounds);
		// 极端但有限的坐标无法安全量化时，退化为当前可见区的保守 Tile，
		// 不能让一个有效 Stored Stroke 因 sidecar 计算失败而失去撤回能力。
		if (!undoTiles) undoTiles = BuildAllVisibleTiles(kUndoTileSize, clippedBounds);
		if (!compositionTiles)
			compositionTiles = BuildAllVisibleTiles(kCompositionTileSize, clippedBounds);
		if (!undoTiles || !compositionTiles) return std::nullopt;
		StrokeTileFootprint result;
		result.pixelBounds = *pixelBounds;
		result.undoTiles = std::move(*undoTiles);
		result.compositionTiles = std::move(*compositionTiles);
		return result;
	}

	std::optional<size_t> CountTilesCoveringPixelBounds(
		InkPixelBounds bounds, uint32_t tileSize) noexcept
	{
		if (!IsValidBounds(bounds) || tileSize == 0) return std::nullopt;
		const double columnsValue = std::ceil(
			static_cast<double>(bounds.right) / tileSize) - std::floor(
			static_cast<double>(bounds.left) / tileSize);
		const double rowsValue = std::ceil(
			static_cast<double>(bounds.bottom) / tileSize) - std::floor(
			static_cast<double>(bounds.top) / tileSize);
		if (!std::isfinite(columnsValue) || !std::isfinite(rowsValue) ||
			columnsValue <= 0.0 || rowsValue <= 0.0 ||
			columnsValue > static_cast<double>((std::numeric_limits<size_t>::max)()) ||
			rowsValue > static_cast<double>((std::numeric_limits<size_t>::max)()))
			return std::nullopt;
		const size_t columns = static_cast<size_t>(columnsValue);
		const size_t rows = static_cast<size_t>(rowsValue);
		if (columns > (std::numeric_limits<size_t>::max)() / rows) return std::nullopt;
		return columns * rows;
	}

	void CompositionRangeTree::InvalidateTiles(size_t itemIndex,
		std::span<const SignedTileCoordinate> tiles)
	{
		const uint32_t rootCapacity = RootBlockCapacity(items_.size());
		if (rootCapacity == 0) return;
		const uint32_t block = static_cast<uint32_t>(itemIndex / kCompositionLeafItemCount);
		for (const SignedTileCoordinate tile : tiles)
		{
			uint32_t blockCount = 1;
			while (true)
			{
				const CompositionNodeId node{
					block & ~(blockCount - 1), blockCount };
				if (generationClock_ == (std::numeric_limits<uint64_t>::max)())
				{
					generationClock_ = 0;
					for (auto& entry : tileGenerations_)
						entry.second = ++generationClock_;
				}
				const uint64_t generation = ++generationClock_;
				tileGenerations_.insert_or_assign({ node, tile }, generation);
				if (blockCount == rootCapacity) break;
				blockCount *= 2;
			}
		}
	}

	bool CompositionRangeTree::AppendRenderItem(const RenderItemState& item)
	{
		if (!item.id.IsValid() || item.id.index != items_.size() ||
			!IsValidBounds(item.pixelBounds)) return false;
		TreeItem treeItem;
		treeItem.id = item.id;
		treeItem.visible = item.visible;
		treeItem.barrier = item.compositionBarrier;
		treeItem.bounds = item.pixelBounds;
		treeItem.tiles = item.compositionTiles;
		NormalizeTiles(treeItem.tiles);
		items_.push_back(std::move(treeItem));
		const uint32_t block = static_cast<uint32_t>(
			(items_.size() - 1) / kCompositionLeafItemCount);
		for (SignedTileCoordinate tile : items_.back().tiles)
		{
			std::vector<uint32_t>& blocks = tileBlocks_[tile];
			if (blocks.empty() || blocks.back() != block) blocks.push_back(block);
		}
		if (items_.back().visible && items_.back().barrier) ++visibleBarrierCount_;
		InvalidateTiles(items_.size() - 1, items_.back().tiles);
		return true;
	}

	bool CompositionRangeTree::SetItemVisibility(RenderItemId id, bool visible)
	{
		if (id.index >= items_.size() || items_[id.index].id != id) return false;
		TreeItem& item = items_[id.index];
		if (item.visible == visible) return true;
		if (item.barrier)
		{
			if (visible) ++visibleBarrierCount_;
			else --visibleBarrierCount_;
		}
		item.visible = visible;
		InvalidateTiles(id.index, item.tiles);
		return true;
	}

	bool CompositionRangeTree::UpdateItemGeometry(RenderItemId id,
		InkPixelBounds bounds, std::span<const SignedTileCoordinate> compositionTiles)
	{
		if (id.index >= items_.size() || items_[id.index].id != id ||
			!IsValidBounds(bounds)) return false;
		std::vector<SignedTileCoordinate> replacement(
			compositionTiles.begin(), compositionTiles.end());
		NormalizeTiles(replacement);
		const uint32_t block = static_cast<uint32_t>(
			id.index / kCompositionLeafItemCount);
		const size_t blockBegin = static_cast<size_t>(block) * kCompositionLeafItemCount;
		const size_t blockEnd = (std::min)(
			blockBegin + kCompositionLeafItemCount, items_.size());
		for (SignedTileCoordinate oldTile : items_[id.index].tiles)
		{
			if (ContainsTile(replacement, oldTile)) continue;
			bool stillTouches = false;
			for (size_t itemIndex = blockBegin; itemIndex < blockEnd; ++itemIndex)
			{
				if (itemIndex != id.index && ContainsTile(items_[itemIndex].tiles, oldTile))
				{
					stillTouches = true;
					break;
				}
			}
			if (stillTouches) continue;
			auto tileEntry = tileBlocks_.find(oldTile);
			if (tileEntry == tileBlocks_.end()) continue;
			std::erase(tileEntry->second, block);
			if (tileEntry->second.empty()) tileBlocks_.erase(tileEntry);
		}
		for (SignedTileCoordinate tile : replacement)
		{
			std::vector<uint32_t>& blocks = tileBlocks_[tile];
			const auto insertion = std::lower_bound(blocks.begin(), blocks.end(), block);
			if (insertion == blocks.end() || *insertion != block)
				blocks.insert(insertion, block);
		}
		std::vector<SignedTileCoordinate> invalidated = items_[id.index].tiles;
		invalidated.insert(invalidated.end(), replacement.begin(), replacement.end());
		NormalizeTiles(invalidated);
		TreeItem& item = items_[id.index];
		item.bounds = bounds;
		item.tiles = std::move(replacement);
		InvalidateTiles(id.index, invalidated);
		return true;
	}

	bool CompositionRangeTree::SetItemBarrier(RenderItemId id, bool barrier)
	{
		if (id.index >= items_.size() || items_[id.index].id != id) return false;
		TreeItem& item = items_[id.index];
		if (item.barrier == barrier) return true;
		if (item.visible)
		{
			if (barrier) ++visibleBarrierCount_;
			else --visibleBarrierCount_;
		}
		item.barrier = barrier;
		InvalidateTiles(id.index, item.tiles);
		return true;
	}

	size_t CompositionRangeTree::ItemCount() const noexcept
	{
		return items_.size();
	}

	std::optional<CompositionNodeId> CompositionRangeTree::RootNode() const noexcept
	{
		const uint32_t capacity = RootBlockCapacity(items_.size());
		if (capacity == 0) return std::nullopt;
		return CompositionNodeId{ 0, capacity };
	}

	std::optional<CompositionNodeId> CompositionRangeTree::LeafNodeForItem(
		size_t itemIndex) const noexcept
	{
		if (itemIndex >= items_.size()) return std::nullopt;
		return CompositionNodeId{
			static_cast<uint32_t>(itemIndex / kCompositionLeafItemCount), 1 };
	}

	std::optional<std::pair<CompositionNodeId, CompositionNodeId>>
		CompositionRangeTree::NodeChildren(CompositionNodeId node) const noexcept
	{
		if (!IsValidNode(node) || node.blockCount == 1) return std::nullopt;
		const uint32_t half = node.blockCount / 2;
		return std::pair{
			CompositionNodeId{ node.firstBlock, half },
			CompositionNodeId{ node.firstBlock + half, half }
		};
	}

	RenderItemRange CompositionRangeTree::NodeItemRange(CompositionNodeId node) const noexcept
	{
		if (!IsValidNode(node)) return {};
		const uint64_t begin = static_cast<uint64_t>(node.firstBlock) *
			kCompositionLeafItemCount;
		const uint64_t end = (static_cast<uint64_t>(node.firstBlock) + node.blockCount) *
			kCompositionLeafItemCount;
		if (end > (std::numeric_limits<size_t>::max)()) return {};
		return { static_cast<size_t>(begin), static_cast<size_t>(end) };
	}

	uint64_t CompositionRangeTree::TileGeneration(CompositionNodeId node,
		SignedTileCoordinate tile) const noexcept
	{
		if (!IsValidNode(node)) return 0;
		const auto found = tileGenerations_.find({ node, tile });
		return found == tileGenerations_.end() ? 0 : found->second;
	}

	bool CompositionRangeTree::NodeTouchesTile(CompositionNodeId node,
		SignedTileCoordinate tile) const noexcept
	{
		if (!IsValidNode(node)) return false;
		const auto found = tileBlocks_.find(tile);
		if (found == tileBlocks_.end()) return false;
		const uint64_t end64 = static_cast<uint64_t>(node.firstBlock) + node.blockCount;
		if (end64 > (std::numeric_limits<uint32_t>::max)()) return true;
		const auto block = std::lower_bound(
			found->second.begin(), found->second.end(), node.firstBlock);
		return block != found->second.end() && *block < static_cast<uint32_t>(end64);
	}

	std::optional<std::vector<CompositionRangePiece>>
		CompositionRangeTree::DecomposeRange(size_t begin, size_t end,
			std::optional<SignedTileCoordinate> tile) const
	{
		if (begin > end || end > items_.size()) return std::nullopt;
		std::vector<CompositionRangePiece> pieces;
		if (begin == end) return pieces;
		const std::optional<CompositionNodeId> root = RootNode();
		if (!root) return std::nullopt;

		const auto isBarrier = [&](size_t itemIndex)
		{
			const TreeItem& item = items_[itemIndex];
			if (!item.visible || !item.barrier) return false;
			return !tile || ContainsTile(item.tiles, *tile);
		};
		auto appendPiece = [&](CompositionRangePieceKind kind,
			RenderItemRange range, CompositionNodeId node)
		{
			if (range.begin >= range.end) return;
			if (kind == CompositionRangePieceKind::OrderedItems && !pieces.empty() &&
				pieces.back().kind == kind && pieces.back().range.end == range.begin)
			{
				pieces.back().range.end = range.end;
				return;
			}
			pieces.push_back({ kind, range, node });
		};

		const auto visit = [&](auto&& self, CompositionNodeId node) -> void
		{
			const RenderItemRange fixedRange = NodeItemRange(node);
			const size_t existingEnd = (std::min)(fixedRange.end, items_.size());
			if (fixedRange.begin >= existingEnd || begin >= existingEnd || end <= fixedRange.begin)
				return;
			const size_t overlapBegin = (std::max)(begin, fixedRange.begin);
			const size_t overlapEnd = (std::min)(end, existingEnd);
			const bool fullyCovered = overlapBegin == fixedRange.begin && overlapEnd == existingEnd;
			bool hasBarrier = false;
			if (fullyCovered && visibleBarrierCount_ != 0)
			{
				for (size_t index = fixedRange.begin; index < existingEnd; ++index)
				{
					if (isBarrier(index))
					{
						hasBarrier = true;
						break;
					}
				}
			}
			if (fullyCovered && !hasBarrier)
			{
				appendPiece(CompositionRangePieceKind::CachedNode,
					{ fixedRange.begin, existingEnd }, node);
				return;
			}
			if (node.blockCount == 1)
			{
				for (size_t index = overlapBegin; index < overlapEnd; ++index)
				{
					if (isBarrier(index))
						appendPiece(CompositionRangePieceKind::BarrierItem,
							{ index, index + 1 }, {});
					else appendPiece(CompositionRangePieceKind::OrderedItems,
						{ index, index + 1 }, {});
				}
				return;
			}
			const uint32_t half = node.blockCount / 2;
			self(self, { node.firstBlock, half });
			self(self, { node.firstBlock + half, half });
		};
		visit(visit, *root);
		return pieces;
	}

	std::optional<RenderItemId> CanvasRuntimeHistory::AppendStroke(
		size_t strokeIndex, StrokeTileFootprint footprint, bool affineOperator,
		bool renderOnlyWhenLatest)
	{
		if (!IsValidBounds(footprint.pixelBounds) ||
			items_.size() >= (std::numeric_limits<uint32_t>::max)() ||
			nextItemGeneration_ == 0) return std::nullopt;
		NormalizeTiles(footprint.undoTiles);
		NormalizeTiles(footprint.compositionTiles);
		RenderItemState state;
		state.id = { static_cast<uint32_t>(items_.size()), nextItemGeneration_ };
		state.strokeIndex = strokeIndex;
		state.active = true;
		state.renderOnlyWhenLatest = renderOnlyWhenLatest;
		state.visible = true;
		state.compositionBarrier = !affineOperator;
		state.pixelBounds = footprint.pixelBounds;
		state.undoTiles = std::move(footprint.undoTiles);
		state.compositionTiles = std::move(footprint.compositionTiles);
		state.previousVisibleIndex = lastVisibleIndex_;
		const std::optional<uint32_t> previousActiveIndex = lastVisibleIndex_;
		items_.push_back(std::move(state));
		if (!compositionTree_.AppendRenderItem(items_.back()))
		{
			items_.pop_back();
			return std::nullopt;
		}
		const RenderItemId result = items_.back().id;
		AddVisibleCompositionTiles(items_.back().compositionTiles);
		if (renderOnlyWhenLatest) ++conditionalItemCount_;
		else SetTrailingConditionalVisibility(previousActiveIndex, false);
		lastVisibleIndex_ = result.index;
		redoItems_.clear();
		if (nextItemGeneration_ == (std::numeric_limits<uint32_t>::max)())
			nextItemGeneration_ = 0;
		else ++nextItemGeneration_;
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return result;
	}

	std::optional<RenderItemId> CanvasRuntimeHistory::LastVisibleItem() const noexcept
	{
		if (!lastVisibleIndex_ || *lastVisibleIndex_ >= items_.size()) return std::nullopt;
		return items_[*lastVisibleIndex_].id;
	}

	std::optional<RenderItemId> CanvasRuntimeHistory::UndoLastVisible()
	{
		const std::optional<RenderItemId> id = LastVisibleItem();
		if (!id) return std::nullopt;
		return UndoLastVisible(*id) ? id : std::nullopt;
	}

	bool CanvasRuntimeHistory::UndoLastVisible(RenderItemId expected)
	{
		if (LastVisibleItem() != expected) return false;
		RenderItemState* item = FindMutable(expected);
		if (!item || !item->active) return false;
		const std::optional<uint32_t> previousActiveIndex = item->previousVisibleIndex;
		// redo push 可能分配；先完成它，失败时不得提前推进 active 游标。
		redoItems_.push_back(expected);
		if (!SetEffectiveVisibility(*item, false))
		{
			redoItems_.pop_back();
			return false;
		}
		if (!item->renderOnlyWhenLatest &&
			!SetTrailingConditionalVisibility(previousActiveIndex, true))
		{
			// 条件组可能已更新一部分；按旧状态整体隐藏后再恢复目标项。
			(void)SetTrailingConditionalVisibility(previousActiveIndex, false);
			(void)SetEffectiveVisibility(*item, true);
			redoItems_.pop_back();
			return false;
		}
		item->active = false;
		lastVisibleIndex_ = previousActiveIndex;
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return true;
	}

	std::optional<RenderItemId> CanvasRuntimeHistory::LastRedoItem() const noexcept
	{
		if (redoItems_.empty()) return std::nullopt;
		const RenderItemId id = redoItems_.back();
		const RenderItemState* item = Find(id);
		if (!item || item->active || item->previousVisibleIndex != lastVisibleIndex_)
			return std::nullopt;
		return id;
	}

	bool CanvasRuntimeHistory::RedoLastUndone(RenderItemId expected)
	{
		if (LastRedoItem() != expected) return false;
		RenderItemState* item = FindMutable(expected);
		if (!item || item->active) return false;
		const bool hidesConditionalTail = !item->renderOnlyWhenLatest;
		if (hidesConditionalTail &&
			!SetTrailingConditionalVisibility(lastVisibleIndex_, false))
		{
			(void)SetTrailingConditionalVisibility(lastVisibleIndex_, true);
			return false;
		}
		if (!SetEffectiveVisibility(*item, true))
		{
			if (hidesConditionalTail)
				(void)SetTrailingConditionalVisibility(lastVisibleIndex_, true);
			return false;
		}
		item->active = true;
		lastVisibleIndex_ = expected.index;
		redoItems_.pop_back();
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return true;
	}

	bool CanvasRuntimeHistory::SetEffectiveVisibility(
		RenderItemState& item, bool visible)
	{
		if (item.visible == visible) return true;
		if (!compositionTree_.SetItemVisibility(item.id, visible)) return false;
		if (visible) AddVisibleCompositionTiles(item.compositionTiles);
		else RemoveVisibleCompositionTiles(item.compositionTiles);
		item.visible = visible;
		if (item.contentGeneration != (std::numeric_limits<uint64_t>::max)())
			++item.contentGeneration;
		return true;
	}

	bool CanvasRuntimeHistory::SetTrailingConditionalVisibility(
		std::optional<uint32_t> start, bool visible)
	{
		if (conditionalItemCount_ == 0) return true;
		std::optional<uint32_t> current = start;
		while (current && *current < items_.size())
		{
			RenderItemState& item = items_[*current];
			if (!item.active || !item.renderOnlyWhenLatest) break;
			if (!SetEffectiveVisibility(item, visible)) return false;
			current = item.previousVisibleIndex;
		}
		return true;
	}

	bool CanvasRuntimeHistory::RecomputeEffectiveVisibility()
	{
		bool laterActiveNonConditionalItem = false;
		for (size_t index = items_.size(); index > 0; --index)
		{
			RenderItemState& item = items_[index - 1];
			const bool effectiveVisible = item.active &&
				(!item.renderOnlyWhenLatest || !laterActiveNonConditionalItem);
			if (!SetEffectiveVisibility(item, effectiveVisible)) return false;
			if (item.active && !item.renderOnlyWhenLatest)
				laterActiveNonConditionalItem = true;
		}
		return true;
	}

	bool CanvasRuntimeHistory::SetRenderOnlyWhenLatest(
		std::span<const RenderItemId> items, bool enabled)
	{
		std::vector<RenderItemId> changedItems;
		changedItems.reserve(items.size());
		for (RenderItemId id : items)
		{
			const RenderItemState* item = Find(id);
			if (!item || !item->active) return false;
			if (item->renderOnlyWhenLatest != enabled &&
				std::find(changedItems.begin(), changedItems.end(), id) ==
				changedItems.end())
				changedItems.push_back(id);
		}
		if (changedItems.empty()) return true;
		if (!enabled && changedItems.size() > conditionalItemCount_) return false;
		const size_t previousConditionalItemCount = conditionalItemCount_;
		for (RenderItemId id : changedItems)
		{
			RenderItemState* item = FindMutable(id);
			item->renderOnlyWhenLatest = enabled;
			if (item->contentGeneration != (std::numeric_limits<uint64_t>::max)())
				++item->contentGeneration;
		}
		if (enabled) conditionalItemCount_ += changedItems.size();
		else conditionalItemCount_ -= changedItems.size();
		if (!RecomputeEffectiveVisibility())
		{
			for (RenderItemId id : changedItems)
			{
				RenderItemState* item = FindMutable(id);
				item->renderOnlyWhenLatest = !enabled;
				if (item->contentGeneration != (std::numeric_limits<uint64_t>::max)())
					++item->contentGeneration;
			}
			conditionalItemCount_ = previousConditionalItemCount;
			(void)RecomputeEffectiveVisibility();
			return false;
		}
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return true;
	}

	std::vector<RenderItemId> CanvasRuntimeHistory::ClearLatestConditionalGroup()
	{
		std::vector<RenderItemId> cleared;
		std::optional<uint32_t> current = lastVisibleIndex_;
		while (current && *current < items_.size())
		{
			const RenderItemState& item = items_[*current];
			if (!item.active || !item.renderOnlyWhenLatest) break;
			cleared.push_back(item.id);
			current = item.previousVisibleIndex;
		}
		if (cleared.empty() || cleared.size() > conditionalItemCount_) return {};
		for (RenderItemId id : cleared)
		{
			RenderItemState& item = items_[id.index];
			item.renderOnlyWhenLatest = false;
			if (item.contentGeneration != (std::numeric_limits<uint64_t>::max)())
				++item.contentGeneration;
		}
		conditionalItemCount_ -= cleared.size();
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return cleared;
	}

	void CanvasRuntimeHistory::DiscardRedoBranch() noexcept
	{
		redoItems_.clear();
	}

	size_t CanvasRuntimeHistory::RedoDepth() const noexcept
	{
		return redoItems_.size();
	}

	bool CanvasRuntimeHistory::UpdateItemGeometry(
		RenderItemId id, StrokeTileFootprint footprint)
	{
		RenderItemState* item = FindMutable(id);
		if (!item || !IsValidBounds(footprint.pixelBounds)) return false;
		NormalizeTiles(footprint.undoTiles);
		NormalizeTiles(footprint.compositionTiles);
		const std::vector<SignedTileCoordinate> previousTiles = item->compositionTiles;
		if (!compositionTree_.UpdateItemGeometry(
			id, footprint.pixelBounds, footprint.compositionTiles)) return false;
		if (item->visible)
		{
			RemoveVisibleCompositionTiles(previousTiles);
			AddVisibleCompositionTiles(footprint.compositionTiles);
		}
		item->pixelBounds = footprint.pixelBounds;
		item->undoTiles = std::move(footprint.undoTiles);
		item->compositionTiles = std::move(footprint.compositionTiles);
		if (item->contentGeneration != (std::numeric_limits<uint64_t>::max)())
			++item->contentGeneration;
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return true;
	}

	bool CanvasRuntimeHistory::SetItemBarrier(RenderItemId id, bool barrier)
	{
		RenderItemState* item = FindMutable(id);
		if (!item || !compositionTree_.SetItemBarrier(id, barrier)) return false;
		if (item->compositionBarrier == barrier) return true;
		item->compositionBarrier = barrier;
		if (item->contentGeneration != (std::numeric_limits<uint64_t>::max)())
			++item->contentGeneration;
		if (revision_ != (std::numeric_limits<uint64_t>::max)()) ++revision_;
		return true;
	}

	RenderItemState* CanvasRuntimeHistory::FindMutable(RenderItemId id) noexcept
	{
		if (id.index >= items_.size() || items_[id.index].id != id) return nullptr;
		return &items_[id.index];
	}

	const RenderItemState* CanvasRuntimeHistory::Find(RenderItemId id) const noexcept
	{
		if (id.index >= items_.size() || items_[id.index].id != id) return nullptr;
		return &items_[id.index];
	}

	std::span<const RenderItemState> CanvasRuntimeHistory::Items() const noexcept
	{
		return { items_.data(), items_.size() };
	}

	void CanvasRuntimeHistory::AddVisibleCompositionTiles(
		std::span<const SignedTileCoordinate> tiles)
	{
		for (SignedTileCoordinate tile : tiles)
		{
			uint32_t& references = visibleCompositionTileReferences_[tile];
			if (references != (std::numeric_limits<uint32_t>::max)()) ++references;
		}
	}

	void CanvasRuntimeHistory::RemoveVisibleCompositionTiles(
		std::span<const SignedTileCoordinate> tiles)
	{
		for (SignedTileCoordinate tile : tiles)
		{
			const auto found = visibleCompositionTileReferences_.find(tile);
			if (found == visibleCompositionTileReferences_.end()) continue;
			if (found->second <= 1) visibleCompositionTileReferences_.erase(found);
			else --found->second;
		}
	}

	std::vector<SignedTileCoordinate> CanvasRuntimeHistory::VisibleCompositionTiles(
		InkPixelBounds bounds) const
	{
		std::vector<SignedTileCoordinate> result;
		TileRange range;
		if (!TryMakeTileRange(bounds, kCompositionTileSize, range)) return result;
		const auto begin = visibleCompositionTileReferences_.lower_bound({
			range.firstX, (std::numeric_limits<int32_t>::min)() });
		for (auto iterator = begin; iterator != visibleCompositionTileReferences_.end() &&
			iterator->first.x <= range.lastX; ++iterator)
		{
			if (iterator->first.y >= range.firstY && iterator->first.y <= range.lastY)
				result.push_back(iterator->first);
		}
		return result;
	}

	uint64_t CanvasRuntimeHistory::Revision() const noexcept
	{
		return revision_;
	}

	const CompositionRangeTree& CanvasRuntimeHistory::CompositionTree() const noexcept
	{
		return compositionTree_;
	}

	UndoCachePlanner::UndoCachePlanner(UndoCachePolicy policy) : policy_(policy)
	{
		TrimToPolicy();
	}

	std::vector<UndoCacheEntryId> UndoCachePlanner::SetPolicy(UndoCachePolicy policy)
	{
		policy_ = policy;
		return TrimToPolicy();
	}

	std::vector<UndoCacheEntryId> UndoCachePlanner::TrimToPolicy()
	{
		std::vector<UndoCacheEntryId> evicted;
		const size_t capacity = SlotCapacity();
		while (!entries_.empty() &&
			(entries_.size() > policy_.maxEntries || usedSlotCount_ > capacity))
		{
			evicted.push_back(entries_.front().id);
			usedSlotCount_ -= entries_.front().tileCount;
			entries_.erase(entries_.begin());
		}
		return evicted;
	}

	UndoCacheReservation UndoCachePlanner::Reserve(
		UndoCacheEntryId id, size_t tileCount)
	{
		UndoCacheReservation result;
		result.id = id;
		result.tileCount = tileCount;
		const size_t capacity = SlotCapacity();
		if (capacity == 0 || policy_.maxEntries == 0)
		{
			result.status = UndoCacheReservationStatus::Disabled;
			return result;
		}
		if (tileCount > capacity)
		{
			result.status = UndoCacheReservationStatus::EntryTooLarge;
			return result;
		}
		if (std::any_of(entries_.begin(), entries_.end(),
			[&](const UndoCacheEntryState& entry) { return entry.id == id; }))
		{
			result.status = UndoCacheReservationStatus::DuplicateEntry;
			return result;
		}
		if (std::any_of(entries_.begin(), entries_.end(),
			[](const UndoCacheEntryState& entry) { return !entry.committed; }))
		{
			result.status = UndoCacheReservationStatus::ReservationPending;
			return result;
		}
		while (!entries_.empty() &&
			(entries_.size() + 1 > policy_.maxEntries ||
				tileCount > capacity - usedSlotCount_))
		{
			result.evictedEntries.push_back(entries_.front().id);
			usedSlotCount_ -= entries_.front().tileCount;
			entries_.erase(entries_.begin());
		}
		entries_.push_back({ id, tileCount, false });
		usedSlotCount_ += tileCount;
		result.status = UndoCacheReservationStatus::Reserved;
		return result;
	}

	bool UndoCachePlanner::Commit(UndoCacheEntryId id) noexcept
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const UndoCacheEntryState& entry) { return entry.id == id; });
		if (found == entries_.end() || found->committed) return false;
		found->committed = true;
		return true;
	}

	bool UndoCachePlanner::Cancel(UndoCacheEntryId id) noexcept
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const UndoCacheEntryState& entry) { return entry.id == id; });
		if (found == entries_.end() || found->committed) return false;
		usedSlotCount_ -= found->tileCount;
		entries_.erase(found);
		return true;
	}

	bool UndoCachePlanner::Consume(UndoCacheEntryId id) noexcept
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const UndoCacheEntryState& entry) { return entry.id == id; });
		if (found == entries_.end() || !found->committed) return false;
		usedSlotCount_ -= found->tileCount;
		entries_.erase(found);
		return true;
	}

	bool UndoCachePlanner::Contains(UndoCacheEntryId id) const noexcept
	{
		return std::any_of(entries_.begin(), entries_.end(),
			[&](const UndoCacheEntryState& entry)
			{
				return entry.id == id && entry.committed;
			});
	}

	const UndoCachePolicy& UndoCachePlanner::Policy() const noexcept
	{
		return policy_;
	}

	size_t UndoCachePlanner::SlotCapacity() const noexcept
	{
		return CapacityFromBytes(policy_.byteBudget, kUndoTileBytes);
	}

	size_t UndoCachePlanner::PageCount() const noexcept
	{
		return PageCountForSlots(SlotCapacity(), kUndoTilesPerPage);
	}

	size_t UndoCachePlanner::UsedSlotCount() const noexcept
	{
		return usedSlotCount_;
	}

	std::span<const UndoCacheEntryState> UndoCachePlanner::Entries() const noexcept
	{
		return { entries_.data(), entries_.size() };
	}

	CompositionCachePlanner::CompositionCachePlanner(CompositionCachePolicy policy)
		: policy_(policy)
	{
		TrimToPolicy();
	}

	void CompositionCachePlanner::Touch(ResidentEntry& entry) noexcept
	{
		if (useClock_ == (std::numeric_limits<uint64_t>::max)())
		{
			for (ResidentEntry& resident : entries_) resident.lastUse >>= 1;
			useClock_ >>= 1;
		}
		entry.lastUse = ++useClock_;
	}

	std::vector<CompositionCacheKeyId> CompositionCachePlanner::SetPolicy(
		CompositionCachePolicy policy)
	{
		policy_ = policy;
		return TrimToPolicy();
	}

	std::vector<CompositionCacheKeyId> CompositionCachePlanner::TrimToPolicy()
	{
		std::vector<CompositionCacheKeyId> evicted;
		const size_t capacity = SlotCapacity();
		auto evictOne = [&](bool requireHighSlot)
		{
			auto victim = entries_.end();
			for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
			{
				if (entry->pinCount != 0 ||
					(requireHighSlot && entry->slot.value < capacity)) continue;
				if (victim == entries_.end() || entry->lastUse < victim->lastUse)
					victim = entry;
			}
			if (victim == entries_.end()) return false;
			evicted.push_back(victim->key);
			entries_.erase(victim);
			return true;
		};
		while (std::any_of(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry)
			{
				return entry.pinCount == 0 && entry.slot.value >= capacity;
			}))
		{
			if (!evictOne(true)) break;
		}
		while (entries_.size() > capacity)
		{
			if (!evictOne(false)) break;
		}
		return evicted;
	}

	CompositionCacheAcquireResult CompositionCachePlanner::Acquire(
		CompositionCacheKeyId key)
	{
		const auto existing = std::find_if(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry) { return entry.key == key; });
		if (existing != entries_.end())
		{
			Touch(*existing);
			return { CompositionCacheAcquireStatus::Hit, existing->slot, std::nullopt };
		}
		const size_t capacity = SlotCapacity();
		if (capacity == 0) return {};

		uint32_t freeSlot = 0;
		bool foundFreeSlot = false;
		const size_t representableCapacity = (std::min)(capacity,
			static_cast<size_t>((std::numeric_limits<uint32_t>::max)()));
		for (size_t candidate = 0; candidate < representableCapacity; ++candidate)
		{
			const bool used = std::any_of(entries_.begin(), entries_.end(),
				[&](const ResidentEntry& entry) { return entry.slot.value == candidate; });
			if (!used)
			{
				freeSlot = static_cast<uint32_t>(candidate);
				foundFreeSlot = true;
				break;
			}
		}

		std::optional<CompositionCacheKeyId> evicted;
		if (!foundFreeSlot)
		{
			auto victim = entries_.end();
			for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
			{
				if (entry->pinCount != 0 || entry->slot.value >= representableCapacity) continue;
				if (victim == entries_.end() || entry->lastUse < victim->lastUse)
					victim = entry;
			}
			if (victim == entries_.end())
				return { CompositionCacheAcquireStatus::AllUsableSlotsPinned, {}, std::nullopt };
			freeSlot = victim->slot.value;
			evicted = victim->key;
			entries_.erase(victim);
		}

		entries_.push_back({ key, { freeSlot }, 0, 0 });
		Touch(entries_.back());
		return { CompositionCacheAcquireStatus::Allocated, { freeSlot }, evicted };
	}

	bool CompositionCachePlanner::Pin(CompositionCacheKeyId key) noexcept
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry) { return entry.key == key; });
		if (found == entries_.end() ||
			found->pinCount == (std::numeric_limits<uint32_t>::max)()) return false;
		++found->pinCount;
		return true;
	}

	std::vector<CompositionCacheKeyId> CompositionCachePlanner::Unpin(
		CompositionCacheKeyId key)
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry) { return entry.key == key; });
		if (found == entries_.end() || found->pinCount == 0) return {};
		--found->pinCount;
		return TrimToPolicy();
	}

	bool CompositionCachePlanner::Release(CompositionCacheKeyId key) noexcept
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry) { return entry.key == key; });
		if (found == entries_.end() || found->pinCount != 0) return false;
		entries_.erase(found);
		return true;
	}

	bool CompositionCachePlanner::Contains(CompositionCacheKeyId key) const noexcept
	{
		return std::any_of(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry) { return entry.key == key; });
	}

	std::optional<CompositionCacheSlotId> CompositionCachePlanner::SlotFor(
		CompositionCacheKeyId key) const noexcept
	{
		const auto found = std::find_if(entries_.begin(), entries_.end(),
			[&](const ResidentEntry& entry) { return entry.key == key; });
		if (found == entries_.end()) return std::nullopt;
		return found->slot;
	}

	const CompositionCachePolicy& CompositionCachePlanner::Policy() const noexcept
	{
		return policy_;
	}

	size_t CompositionCachePlanner::SlotCapacity() const noexcept
	{
		return (std::min)(CapacityFromBytes(policy_.byteBudget, kCompositionTileBytes),
			static_cast<size_t>((std::numeric_limits<uint32_t>::max)()));
	}

	size_t CompositionCachePlanner::PageCount() const noexcept
	{
		return PageCountForSlots(SlotCapacity(), kCompositionTilesPerPage);
	}

	size_t CompositionCachePlanner::ResidentCount() const noexcept
	{
		return entries_.size();
	}

	size_t CompositionCachePlanner::PinnedCount() const noexcept
	{
		return static_cast<size_t>(std::count_if(entries_.begin(), entries_.end(),
			[](const ResidentEntry& entry) { return entry.pinCount != 0; }));
	}
}
