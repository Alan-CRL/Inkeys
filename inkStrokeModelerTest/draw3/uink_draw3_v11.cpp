module;

#include "shape_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module draw3.uink_draw3_v11;

namespace draw3::uink
{
#include "shape_uink_v11.inl"

	namespace
	{
		constexpr float kHighlighterAspect = 0.125f;
		constexpr float kRectangleEraserAspect = 0.6666667f;
		constexpr float kSquareEraserAspect = 1.0f;
		constexpr float kEraserAspectTolerance = 1e-4f;

		// 规范“Inkeys 的取值”：使矩形笔尖和 Draw3 的圆形橡皮面积相等。
		double EraserDiameterToNibHeight(float diameter, float aspect) noexcept
		{
			return (diameter / 2.0) * std::sqrt(std::numbers::pi / aspect);
		}

		double NibHeightToEraserDiameter(float height, float aspect) noexcept
		{
			return (2.0 * height) * std::sqrt(aspect / std::numbers::pi);
		}

		// 自产笔尖的 H/s 为 1 或约 0.886～1.086。两端均留在 float32
		// 正常数范围内，两次舍入的相对误差小于 2^-22，满足 1e-5。
		constexpr float kMinEraserDiameter = 2.0f * (std::numeric_limits<float>::min)();
		constexpr float kMaxEraserDiameter = 0x1p127f;

		double EraserBoundaryHeight(float diameter, float aspect) noexcept
		{
			// 按文件位宽比较边界，避免自产端点因反算舍入偏出范围而重复计数。
			const double height = EraserDiameterToNibHeight(diameter, aspect);
			if (height > (std::numeric_limits<float>::max)() ||
				height < (std::numeric_limits<float>::denorm_min)()) return height;
			return static_cast<float>(height);
		}

		bool ValidScale(float scale) noexcept
		{
			return std::isfinite(scale) && scale > 0.0f;
		}

		bool ValidPageContext(const Draw3UInkV11PageContext& context) noexcept
		{
			if (!context.implicitDevice || !ValidScale(context.dpiScale)) return false;
			if (!context.workspaceType) return !context.workspaceGuid;
			return (context.workspaceType == 1 || context.workspaceType == 2) &&
				context.workspaceGuid && !context.workspaceGuid->IsZero();
		}

		void Diagnose(std::vector<UInkDiagnostic>& diagnostics,
			UInkDiagnosticCode code, UInkDiagnosticSeverity severity, const char* field,
			size_t index = 0) noexcept
		{
			// 内存不足时保留既有诊断和失败状态。
			try { diagnostics.push_back({ code, severity, 0, index, field, 0 }); }
			catch (...) {}
		}

		bool ValidInkPoint(float x, float y, double width) noexcept
		{
			return std::abs(x) <= kUInkMaxCoordinate && std::abs(y) <= kUInkMaxCoordinate &&
				width > 0.0 && width <= (std::numeric_limits<float>::max)() &&
				static_cast<float>(width) > 0.0f;
		}

		bool AppendV11Ink(const Draw3UInkStrokeSnapshot& source, UInkCanvas& canvas,
			uint32_t undoId, Draw3UInkV11FallbackCounts& counts)
		{
			const auto& style = source.style;
			if (source.points.empty() || style.texture != 0 || style.fallbackRgb > 0xffffff ||
				!std::isfinite(style.opacity) || style.opacity < 0.0f || style.opacity > 1.0f ||
				style.eraserShape > 2 ||
				(style.kind != Draw3UInkStrokeKind::Eraser && style.eraserShape != 0))
				return false;

			UInkInk ink;
			ink.contentId = static_cast<uint32_t>(canvas.content.size());
			ink.undoId = undoId;
			ink.opacity = style.opacity;
			ink.color.fallbackRgb = style.fallbackRgb;
			ink.renderOnlyWhenLatest = source.renderOnlyWhenLatest;
			switch (style.kind)
			{
			case Draw3UInkStrokeKind::Pen:
				break;
			case Draw3UInkStrokeKind::Highlighter:
				ink.declaredInkType = 2;
				ink.effectiveKind = UInkInkKind::Highlighter;
				ink.nib = UInkNib{ 1, kHighlighterAspect };
				break;
			case Draw3UInkStrokeKind::Eraser:
				ink.declaredInkType = 0;
				ink.effectiveKind = UInkInkKind::Erase;
				if (style.eraserShape != 0)
					ink.nib = UInkNib{ 1, style.eraserShape == 1 ?
						kRectangleEraserAspect : kSquareEraserAspect };
				break;
			default:
				return false;
			}
			ink.points.reserve(source.points.size());
			bool sizeClamped = false;
			for (const auto& point : source.points)
			{
				float diameter = point.width;
				if (!ValidInkPoint(point.x, point.y, diameter)) return false;
				if (style.kind == Draw3UInkStrokeKind::Eraser)
				{
					diameter = std::clamp(diameter, kMinEraserDiameter, kMaxEraserDiameter);
					sizeClamped |= diameter != point.width;
				}
				const double width = style.kind == Draw3UInkStrokeKind::Eraser && ink.nib ?
					EraserDiameterToNibHeight(diameter, *ink.nib->aspect) : diameter;
				ink.points.push_back({ point.x, point.y, static_cast<float>(width), std::nullopt });
			}
			if (sizeClamped) ++counts.eraserSize;
			canvas.content.emplace_back(std::move(ink));
			return true;
		}

		std::optional<Draw3UInkStrokeSnapshot> ReadV11Ink(
			const UInkInk& ink, Draw3UInkV11FallbackCounts& counts)
		{
			if (ink.points.empty() || !std::isfinite(ink.opacity) || ink.color.fallbackRgb > 0xffffff)
				return std::nullopt;
			Draw3UInkStrokeSnapshot result;
			auto& style = result.style;
			const auto nib = EffectiveUInkNib(ink.nib);
			bool nibFallback = false;
			switch (ink.declaredInkType)
			{
			case 0:
				style.kind = Draw3UInkStrokeKind::Eraser;
				if (nib.shape == 1)
				{
					const float rectangle = std::abs(*nib.aspect - kRectangleEraserAspect);
					const float square = std::abs(*nib.aspect - kSquareEraserAspect);
					style.eraserShape = rectangle <= square ? 1 : 2;
					nibFallback |= (std::min)(rectangle, square) > kEraserAspectTolerance ||
						nib.orientation == 1;
				}
				break;
			case 3:
				++counts.advancedHighlighter;
				[[fallthrough]];
			case 2:
				style.kind = Draw3UInkStrokeKind::Highlighter;
				// 有意以 Draw3 固定 1:8 矩形近似外来笔尖，保留块级合成和原始宽度。
				// L1 已记录非法 aspect 和未知形状，这里只统计有效笔尖的近似。
				if (!ink.nib || ink.nib->shape == nib.shape)
					nibFallback = nib.shape != 1 || *nib.aspect != kHighlighterAspect ||
						nib.orientation == 1;
				break;
			default:
				nibFallback = nib.shape != 0;
				break;
			}
			if (nibFallback) ++counts.nib;
			if (EffectiveUInkCap(ink) != 0) ++counts.cap;
			if (ink.renderOnlyWhenLatest) ++counts.latestVisibility;
			if (ink.color.extended) ++counts.extendedColor;
			style.opacity = std::clamp(ink.opacity, 0.0f, 1.0f);
			style.fallbackRgb = ink.color.fallbackRgb;
			const bool eraser = style.kind == Draw3UInkStrokeKind::Eraser;
			const bool rectangularEraser = eraser && nib.shape == 1;
			const double minWidth = rectangularEraser ?
				EraserBoundaryHeight(kMinEraserDiameter, *nib.aspect) : kMinEraserDiameter;
			const double maxWidth = rectangularEraser ?
				EraserBoundaryHeight(kMaxEraserDiameter, *nib.aspect) : kMaxEraserDiameter;
			bool sizeClamped = false;
			result.points.reserve(ink.points.size());
			for (const auto& point : ink.points)
			{
				if (!ValidInkPoint(point.x, point.y, point.width)) return std::nullopt;
				double width = rectangularEraser ?
					NibHeightToEraserDiameter(point.width, *nib.aspect) : point.width;
				if (eraser)
				{
					sizeClamped |= point.width < minWidth || point.width > maxWidth;
					width = std::clamp(width, double(kMinEraserDiameter), double(kMaxEraserDiameter));
				}
				result.points.push_back({ point.x, point.y, static_cast<float>(width) });
			}
			if (sizeClamped) ++counts.eraserSize;
			return result;
		}

		bool AppendV11Stroke(const Draw3UInkStrokeSnapshot& stroke, UInkCanvas& canvas,
			uint32_t undoId, const shapes::BuildOptions& options, V11GeometryWork& geometryWork,
			Draw3UInkV11FallbackCounts& counts, std::string& error)
		{
			switch (stroke.style.kind)
			{
			case Draw3UInkStrokeKind::Pen:
			case Draw3UInkStrokeKind::Highlighter:
			case Draw3UInkStrokeKind::Eraser:
				return !stroke.shape && AppendV11Ink(stroke, canvas, undoId, counts);
			case Draw3UInkStrokeKind::Shape:
				return stroke.shape && AppendV11Shape(canvas.content, *stroke.shape, undoId,
					stroke.renderOnlyWhenLatest, options, geometryWork, counts, error);
			case Draw3UInkStrokeKind::SolidLine:
			case Draw3UInkStrokeKind::DashedLine:
			case Draw3UInkStrokeKind::OutlineRectangle:
			case Draw3UInkStrokeKind::FilledRectangle:
				break;
			default:
				return false;
			}
			// 旧快照可能只包含双端点笔迹；补出标准图形所需的对象身份。
			if (stroke.shape)
				return AppendV11Shape(canvas.content, *stroke.shape, undoId,
					stroke.renderOnlyWhenLatest, options, geometryWork, counts, error);
			if (stroke.points.size() != 2 || stroke.points[0].width != stroke.points[1].width)
				return false;
			const auto id = CreateUInkGuid();
			if (!id) return false;
			const auto kind = static_cast<shapes::LegacyKind>(
				static_cast<uint32_t>(stroke.style.kind) -
				static_cast<uint32_t>(Draw3UInkStrokeKind::SolidLine) + 1);
			const auto shape = shapes::MakeLegacyShape(kind,
				{ stroke.points[0].x, stroke.points[0].y }, { stroke.points[1].x, stroke.points[1].y },
				stroke.points[0].width, stroke.style.fallbackRgb, stroke.style.opacity, id->Bytes());
			return shape && AppendV11Shape(canvas.content, *shape, undoId,
				stroke.renderOnlyWhenLatest, options, geometryWork, counts, error);
		}

		// 与 Draw3.InkHistory 的安装足迹一致。这里仅计候选访问次数和包围盒瓦片数，不生成瓦片。
		constexpr double kFootprintAaPadding = 2.0;
		constexpr float kImportViewportLimit = 1048576.0f;

		struct FootprintExpansion
		{
			double x;
			double y;
		};

		struct FootprintBounds
		{
			double left = std::numeric_limits<double>::infinity();
			double top = std::numeric_limits<double>::infinity();
			double right = -std::numeric_limits<double>::infinity();
			double bottom = -std::numeric_limits<double>::infinity();

			void Include(shapes::Vec2 point, FootprintExpansion expand) noexcept
			{
				left = std::min(left, point.x - expand.x);
				top = std::min(top, point.y - expand.y);
				right = std::max(right, point.x + expand.x);
				bottom = std::max(bottom, point.y + expand.y);
			}
		};

		FootprintExpansion InkFootprintExpansion(const Draw3UInkStrokeSnapshot& stroke, float width) noexcept
		{
			const float radius = width * 0.5f;
			if (stroke.style.kind == Draw3UInkStrokeKind::Highlighter)
			{
				const float halfHeight = std::max(0.5f, radius);
				return { halfHeight * kHighlighterAspect + kFootprintAaPadding,
					halfHeight + kFootprintAaPadding };
			}
			if (stroke.style.kind == Draw3UInkStrokeKind::Eraser)
			{
				if (stroke.style.eraserShape == 1)
					return { radius * 0.7236012545582677f + kFootprintAaPadding,
						radius * 1.0854018818374016f + kFootprintAaPadding };
				if (stroke.style.eraserShape == 2)
				{
					const double half = radius * 0.8862269254527580f + kFootprintAaPadding;
					return { half, half };
				}
			}
			const double half = static_cast<double>(width) * 0.5 + kFootprintAaPadding;
			return { half + (stroke.points.size() == 1 ? std::max(0.25f, radius * 0.05f) : 0.0f), half };
		}

		shapes::Vec2 FootprintPoint(const Draw3UInkPoint& point) noexcept
		{
			return { point.x, point.y };
		}

		template<class Visit>
		bool VisitInkFootprintSweeps(const Draw3UInkStrokeSnapshot& stroke, Visit&& visit)
		{
			const auto& points = stroke.points;
			const auto sweep = [&](const Draw3UInkPoint& first, const Draw3UInkPoint& second)
			{
				return visit(FootprintPoint(first), FootprintPoint(second),
					InkFootprintExpansion(stroke, std::max(first.width, second.width)));
			};
			if (points.size() == 1) return sweep(points.front(), points.front());

			size_t retained = 0;
			for (size_t i = 1; i < points.size(); ++i)
			{
				const auto& current = points[i];
				if (!sweep(points[i - 1], current)) return false;
				if (stroke.style.kind != Draw3UInkStrokeKind::Highlighter) continue;
				// 荧光笔还扫过略过近重合点之后的连接线。
				const float dx = current.x - points[retained].x;
				const float dy = current.y - points[retained].y;
				if (dx * dx + dy * dy <= 0.25f * 0.25f) continue;
				if (retained != i - 1 && !sweep(points[retained], current)) return false;
				retained = i;
			}
			return true;
		}

		class FootprintVisits
		{
		public:
			FootprintVisits(double tileSize, uint64_t remaining) noexcept
				: tileSize_(tileSize), remaining_(remaining) {}

			bool SetBounds(const FootprintBounds& bounds, uint64_t maxTiles) noexcept
			{
				const auto [columns, rows] = RectangleSize(bounds);
				// 保证运行时可量化，且内部 visibleCount * 8 + 1024 可表示。
				if (columns == 0 || rows == 0 ||
					columns > ((std::numeric_limits<uint64_t>::max)() - 1024) / 8 / rows ||
					columns > maxTiles / rows)
					return false;
				columns_ = columns;
				rows_ = rows;
				return true;
			}

			bool Sweep(shapes::Vec2 first, shapes::Vec2 second, FootprintExpansion expansion) noexcept
			{
				const auto window = [&](double extent, uint64_t visible)
				{
					return static_cast<uint64_t>(std::min(
						2.0 * std::ceil(extent / tileSize_) + 3.0, static_cast<double>(visible)));
				};
				const uint64_t columns = window(expansion.x, columns_);
				const uint64_t rows = window(expansion.y, rows_);
				const double firstX = std::floor(first.x / tileSize_);
				const double firstY = std::floor(first.y / tileSize_);
				if (!ValidTile(firstX) || !ValidTile(firstY)) return Charge(columns_, rows_);
				if (first == second) return Charge(columns, rows);

				// 无视区裁剪时，所有扫掠端点都在全笔边界中。沿用 ClipSegment
				// 对结束点的双精度重构，包含刚好落在瓦片边上的舍入情况。
				const double endX = first.x + (static_cast<double>(second.x) - first.x);
				const double endY = first.y + (static_cast<double>(second.y) - first.y);
				const double lastX = std::floor(endX / tileSize_);
				const double lastY = std::floor(endY / tileSize_);
				if (!ValidTile(lastX) || !ValidTile(lastY))
					return Charge(columns_, rows_); // AddSegmentTiles 的逐段全范围路径。
				const auto crossings = static_cast<uint64_t>(
					std::abs(lastX - firstX) + std::abs(lastY - firstY));
				if (crossings == 0) return Charge(columns, rows);

				// 中心在全笔范围内，每轴跨越数最多为可见瓦片数（含边界）。
				// 因而不会触发 AddSegmentTiles 的 8 * visibleCount + 1024 分支。
				// DDA 循环最多 crossings + 3 次。若最终未到端点，外层可能
				// 再全扫一次，整笔只预留一次该回退。
				needsWholeGrid_ = true;
				return Charge(columns, rows, crossings + 3);
			}

			bool Fill(const FootprintBounds& bounds) noexcept
			{
				const auto [columns, rows] = RectangleSize(bounds);
				return Charge(columns, rows);
			}

			bool Finish(uint64_t& remaining, uint64_t& pageTilesRemaining) noexcept
			{
				if (needsWholeGrid_ && !Charge(columns_, rows_)) return false;
				// SetBounds 已校验乘积和页余额，访问预算也通过后才扣减。
				pageTilesRemaining -= columns_ * rows_;
				remaining = remaining_;
				return true;
			}

		private:
			static bool ValidTile(double value) noexcept
			{
				return value >= (std::numeric_limits<int32_t>::min)() &&
					value <= (std::numeric_limits<int32_t>::max)();
			}

			std::pair<uint64_t, uint64_t> RectangleSize(const FootprintBounds& bounds) const noexcept
			{
				const auto extent = [&](double first, double last) -> uint64_t
				{
					if (!(first < last)) return 0;
					const double begin = std::floor(first / tileSize_);
					const double end = std::ceil(last / tileSize_) - 1;
					if (!ValidTile(begin) || !ValidTile(end) || end < begin) return 0;
					return static_cast<uint64_t>(end - begin + 1);
				};
				return { extent(bounds.left, bounds.right), extent(bounds.top, bounds.bottom) };
			}

			bool Charge(uint64_t columns, uint64_t rows, uint64_t visits = 1) noexcept
			{
				if (columns == 0 || rows == 0) return true;
				// 合成瓦片沿同原点按整倍数放大，列数、行数、逐段跨越数
				// 和每步的邻域宽高均不增加。按最小瓦片给两次枚举记账。
				const uint64_t scans = visits * 2;
				if (columns > remaining_ / rows / scans) return false;
				remaining_ -= columns * rows * scans;
				return true;
			}

			double tileSize_;
			uint64_t remaining_;
			uint64_t columns_ = 0;
			uint64_t rows_ = 0;
			bool needsWholeGrid_ = false;
		};

		bool ChargeStrokeFootprint(const Draw3UInkStrokeSnapshot& stroke, uint32_t tileSize,
			uint64_t itemMaxTiles, uint64_t& remaining, uint64_t& pageTilesRemaining)
		{
			const uint64_t maxTiles = (std::min)(itemMaxTiles, pageTilesRemaining);
			FootprintVisits visits(tileSize, remaining);
			if (stroke.style.kind == Draw3UInkStrokeKind::Shape)
			{
				// 物化 InkStroke 使用默认 BuildOptions，可能与导入重建预设时的 DPI 不同。
				// 与 BuildTiles 一样计入主体以及辅助部件的路径和填充矩形。
				shapes::GeometryResult geometry;
				if (!stroke.shape || !V11ShapeGeometry(*stroke.shape, {}, geometry)) return false;
				const auto& bounds = geometry.bounds;
				// 整图形边界已包含各填充部件的矩形。
				if (!bounds.valid || !visits.SetBounds({ bounds.left, bounds.top, bounds.right, bounds.bottom }, maxTiles))
					return false;
				for (const auto& part : geometry.parts)
				{
					const auto& fill = part.bounds;
					if (part.style.fill && fill.valid &&
						!visits.Fill({ fill.left, fill.top, fill.right, fill.bottom })) return false;
					if (!part.style.stroke) continue;
					const double padding = part.style.width * 0.5 + kFootprintAaPadding;
					for (const auto& path : part.paths)
						for (size_t i = 1; i < path.points.size(); ++i)
							if (!visits.Sweep(path.points[i - 1], path.points[i], { padding, padding }))
								return false;
				}
			}
			else
			{
				FootprintBounds bounds;
				const auto sweep = [&](shapes::Vec2 first, shapes::Vec2 second, FootprintExpansion expand)
				{
					return visits.Sweep(first, second, expand);
				};
				const bool line = stroke.style.kind == Draw3UInkStrokeKind::SolidLine ||
					stroke.style.kind == Draw3UInkStrokeKind::DashedLine;
				if (line)
				{
					const double padding = stroke.points.front().width * 0.5 + kFootprintAaPadding;
					for (const auto& point : stroke.points)
						bounds.Include(FootprintPoint(point), { padding, padding });
				}
				else if (stroke.style.kind == Draw3UInkStrokeKind::Highlighter)
				{
					VisitInkFootprintSweeps(stroke, [&](shapes::Vec2 first, shapes::Vec2 second, FootprintExpansion expand)
					{
						bounds.Include(first, expand);
						bounds.Include(second, expand);
						return true;
					});
				}
				else
				{
					for (const auto& point : stroke.points)
						bounds.Include(FootprintPoint(point), InkFootprintExpansion(stroke, point.width));
				}

				// ComputeStrokeBounds 把扩张边界转成 float 后再量化。提前拒绝不可表示的项。
				const double floatMax = (std::numeric_limits<float>::max)();
				if (bounds.left < -floatMax || bounds.top < -floatMax ||
					bounds.right > floatMax || bounds.bottom > floatMax ||
					!visits.SetBounds({ static_cast<float>(bounds.left), static_cast<float>(bounds.top),
						static_cast<float>(bounds.right), static_cast<float>(bounds.bottom) }, maxTiles)) return false;

				// L3 旧兼容仅保留直线的双端点表示；圆角矩形走上面的 Shape 路径。
				if (line)
				{
					const double padding = stroke.points.front().width * 0.5 + kFootprintAaPadding;
					if (!sweep(FootprintPoint(stroke.points.front()), FootprintPoint(stroke.points.back()),
						{ padding, padding })) return false;
				}
				else if (!VisitInkFootprintSweeps(stroke, sweep)) return false;
			}
			return visits.Finish(remaining, pageTilesRemaining);
		}

		struct PageLogRange
		{
			size_t begin = 0;
			size_t end = 0;
			size_t clearCount = 0;
			size_t currentBegin = 0;
		};

		template<class IsClear>
		PageLogRange FindPageLog(size_t size, std::optional<uint32_t> interval, IsClear isClear)
		{
			PageLogRange range{ 0, size, 0 };
			for (size_t index = 0; index < size; ++index)
			{
				if (!isClear(index)) continue;
				if (interval && range.clearCount == *interval)
				{
					range.end = index;
					break;
				}
				range.begin = range.currentBegin;
				range.currentBegin = index + 1;
				++range.clearCount;
			}
			return range;
		}

		struct UndoNumbering
		{
			uint32_t nextId = 0;
			uint32_t logicalId = 0;
			std::optional<uint32_t> previous;
			bool previousWasClear = false;

			uint32_t Add(uint32_t sourceId, bool clear) noexcept
			{
				if (clear || previousWasClear || previous != sourceId) logicalId = nextId++;
				previous = sourceId;
				previousWasClear = clear;
				return logicalId;
			}
		};

		void ReadPageMetadata(const UInkCanvas& canvas, Draw3UInkCanvasSnapshot& page,
			Draw3UInkV11FallbackCounts& counts) noexcept
		{
			page.deviceGuid = canvas.deviceGuid;
			page.pageGuid = canvas.pageGuid;
			page.pageIndex = canvas.pageIndex;
			page.pageNumber = canvas.pageNumber;
			if (canvas.layerIndex != 0) return; // CANVAS-8，L1 已报告忽略的字段。
			page.retained = EffectiveUInkPageState(canvas) == 1;
			page.endScreen = EffectiveUInkPageKind(canvas) == 1;
			if (canvas.binding) page.binding = EffectiveUInkBinding(canvas);
			if (!page.endScreen && EffectiveUInkBinding(canvas) == 0) page.slideId = canvas.slideId;
			if (canvas.background)
			{
				const int32_t pattern = EffectiveUInkBackgroundPattern(*canvas.background);
				if (canvas.background->color.extended) ++counts.extendedColor;
				page.background = Draw3UInkBackground{ canvas.background->color.fallbackRgb, pattern };
			}
			if (canvas.viewport)
			{
				if (canvas.viewport->scale == 1.0f &&
					std::abs(canvas.viewport->x) <= kImportViewportLimit &&
					std::abs(canvas.viewport->y) <= kImportViewportLimit)
					page.viewport = *canvas.viewport;
				else
					++counts.viewport;
			}
		}

		void ReadCanvasContent(const UInkCanvas& canvas, const Draw3UInkV11ImportOptions& options,
			const std::set<shapes::ObjectId>* duplicateRoots, V11GeometryWork& geometryWork,
			std::optional<uint64_t>& footprintRemaining, uint64_t& pageTilesRemaining,
			Draw3UInkCanvasSnapshot& page, Draw3UInkV11FallbackCounts& counts, uint32_t& nextUndoId)
		{
			auto range = FindPageLog(canvas.content.size(), std::nullopt, [&](size_t index)
				{ return std::holds_alternative<UInkClear>(canvas.content[index]); });
			if (range.clearCount > 1) ++counts.multipleClears;
			const bool upperLayer = duplicateRoots && canvas.layerIndex > 0;
			// CLEAR-2：第 0 层保留完整撤回日志，上层只叠加自己的当前区间。
			if (upperLayer) range.begin = range.currentBegin;
			UndoNumbering numbering{ nextUndoId };
			V11ShapeReader shapeReader(canvas, { options.dpiScale }, counts, geometryWork, duplicateRoots);
			for (size_t index = range.begin; index < range.end;)
			{
				const auto& content = canvas.content[index];
				const uint32_t sourceUndoId = std::visit([](const auto& value) { return value.undoId; }, content);
				std::optional<Draw3UInkStrokeSnapshot> stroke;
				size_t consumed = 1;
				if (const auto* ink = std::get_if<UInkInk>(&content))
				{
					// INK-11：Draw3 单层无法限定上层橡皮的作用域。
					if (upperLayer && ink->declaredInkType == 0)
						++counts.upperLayerEraseSkipped;
					else
					{
						stroke = ReadV11Ink(*ink, counts);
						if (!stroke) ++counts.invalidInkSkipped;
					}
				}
				else if (std::holds_alternative<UInkShape>(content))
				{
					auto imported = shapeReader.Read(index, range.end);
					consumed = imported.consumed;
					if (imported.shape && imported.bounds.valid)
					{
						const auto& object = *imported.shape;
						const auto& style = object.style;
						const auto& bounds = imported.bounds;
						stroke.emplace();
						stroke->style = { Draw3UInkStrokeKind::Shape,
							style.stroke ? style.strokeOpacity : style.fillOpacity,
							style.stroke ? style.strokeRgb : style.fillRgb };
						// Draw3 的 InkStroke 用两个点承载图形范围；旧直线仍用原始端点。
						if (object.legacyKind == shapes::LegacyKind::SolidLine ||
							object.legacyKind == shapes::LegacyKind::DashedLine)
						{
							stroke->style.kind = object.legacyKind == shapes::LegacyKind::SolidLine ?
								Draw3UInkStrokeKind::SolidLine : Draw3UInkStrokeKind::DashedLine;
							stroke->points = { { object.geometry.start.x, object.geometry.start.y, style.width },
								{ object.geometry.end.x, object.geometry.end.y, style.width } };
						}
						else
						{
							stroke->points = { { bounds.left, bounds.top, style.width },
								{ bounds.right, bounds.bottom, style.width } };
						}
						stroke->shape = std::move(imported.shape);
					}
					else
						++counts.invalidShapeSkipped;
				}
				else if (std::holds_alternative<UInkClear>(content))
				{
					const uint32_t undoId = numbering.Add(sourceUndoId, true);
					page.operations.emplace_back(Draw3UInkClearSnapshot{ undoId });
					++page.intervalOrdinal;
				}
				else
					++counts.mediaSkipped;
				// 前缀和当前区间共用页级访问与瓦片总量预算，超限项不扣减后续项的额度。
				// 足迹枚举逐次追加瓦片、最后才去重，单项另设上限以约束安装时的内存峰值。
				if (stroke && footprintRemaining)
				{
					uint64_t itemRemaining = (std::min)(*footprintRemaining, options.footprintItemBudget);
					const uint64_t itemBudget = itemRemaining;
					if (ChargeStrokeFootprint(*stroke, options.footprintTileSize, options.footprintItemMaxTiles,
						itemRemaining, pageTilesRemaining))
						*footprintRemaining -= itemBudget - itemRemaining;
					else
					{
						++counts.footprintBudget;
						stroke.reset();
					}
				}
				if (stroke)
				{
					stroke->undoId = numbering.Add(sourceUndoId, false);
					if (index >= range.currentBegin)
					{
						page.operations.emplace_back(*stroke);
						page.strokes.push_back(std::move(*stroke));
					}
					else
						page.operations.emplace_back(std::move(*stroke));
				}
				else if (!std::holds_alternative<UInkClear>(content) && numbering.previous != sourceUndoId)
					numbering.previous.reset();
				index += consumed;
			}
			nextUndoId = numbering.nextId;
		}

		using GuidBytes = std::array<uint8_t, 16>;

		std::optional<size_t> FindInvalidSpliceContent(std::span<const UInkContent> content,
			bool diskPrefix, std::set<GuidBytes>& shapeIds)
		{
			uint32_t previousUndo = 0;
			bool previousWasClear = false;
			const UInkShapeObject* root = nullptr;
			uint32_t rootUndo = 0;
			uint32_t nextPart = 0;
			for (size_t index = 0; index < content.size(); ++index)
			{
				const auto& block = content[index];
				if (block.valueless_by_exception()) return index;
				const auto [contentId, undoId] = std::visit(
					[](const auto& value) { return std::pair{ value.contentId, value.undoId }; }, block);
				const bool clear = std::holds_alternative<UInkClear>(block);
				if (contentId != index || (!diskPrefix && clear)) return index;
				if (diskPrefix && (undoId < previousUndo ||
					(index == 0 ? undoId != 0 :
						(clear || previousWasClear) && undoId == previousUndo))) return index;

				// 校验部件没有跨出前缀、跨组或引用另一段的图形对象。
				const auto* shape = std::get_if<UInkShape>(&block);
				if (root)
				{
					const auto* ref = shape && shape->object ?
						std::get_if<UInkShapeObjectRef>(&*shape->object) : nullptr;
					if (!ref || ref->ref != root->id || ref->partIndex != nextPart ||
						undoId != rootUndo) return index;
					if (++nextPart == root->partCount) root = nullptr;
				}
				else if (shape && shape->object)
				{
					const auto* object = std::get_if<UInkShapeObject>(&*shape->object);
					if (!object || object->id.IsZero() || object->revision == 0 ||
						object->partCount == 0 || object->partCount > content.size() - index ||
						!shapeIds.insert(object->id.Bytes()).second) return index;
					if (object->partCount > 1)
					{
						root = object;
						rootUndo = undoId;
						nextPart = 1;
					}
				}
				previousUndo = undoId;
				previousWasClear = clear;
			}
			return std::nullopt;
		}

		std::optional<GuidBytes> GuidKey(const std::optional<UInkGuid>& guid) noexcept
		{
			return guid ? std::optional<GuidBytes>{ guid->Bytes() } : std::nullopt;
		}

		struct ImportPageGroup
		{
			size_t workspaceOrder = 0;
			size_t lastOccurrence = 0;
			bool nested = false;
			bool multiDevice = false;
			std::vector<const UInkCanvas*> layers;
		};

		Draw3UInkV11PageImportResult ImportLayers(std::span<const UInkCanvas* const> layers,
			const Draw3UInkV11ImportOptions& options,
			const std::set<shapes::ObjectId>* duplicateRoots = nullptr)
		{
			Draw3UInkV11PageImportResult result;
			if (!ValidScale(options.dpiScale) || (options.footprintBudget && options.footprintTileSize == 0) ||
				layers.empty() || layers.front()->pageGuid.IsZero())
			{
				Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
					UInkDiagnosticSeverity::Error, "draw3.v11.canvas.identity");
				return result;
			}
			auto& page = result.page.emplace();
			result.workspaceGuid = layers.front()->workspaceGuid;
			ReadPageMetadata(*layers.front(), page, result.counts);
			if (layers.size() > 1 || layers.front()->layerIndex != 0) ++result.counts.multiLayer;
			uint32_t nextUndoId = 0;
			V11GeometryWork geometryWork;
			auto footprintRemaining = options.footprintBudget;
			uint64_t pageTilesRemaining = options.footprintPageMaxTiles;
			for (const UInkCanvas* layer : layers)
				ReadCanvasContent(*layer, options, duplicateRoots, geometryWork,
					footprintRemaining, pageTilesRemaining, page, result.counts, nextUndoId);
			result.status = Draw3UInkV11ImportStatus::Success;
			return result;
		}
	}

	const char* Draw3UInkV11ColdPreflightFailure(const Draw3UInkV11PageImportResult& result) noexcept
	{
		switch (result.status)
		{
		case Draw3UInkV11ImportStatus::Success: return result.page ? nullptr : "missing_page";
		case Draw3UInkV11ImportStatus::InvalidPage: return "invalid_page";
		case Draw3UInkV11ImportStatus::InvalidFile: return "invalid_file";
		case Draw3UInkV11ImportStatus::InternalError: return "internal_error";
		}
		return "unknown";
	}

	Draw3UInkV11FileHeaderResult MakeDraw3UInkV11FileHeader(const UInkGuid& fileGuid,
		const Draw3UInkV11PageContext& context, const std::optional<std::string>& hostId,
		uint32_t currentPageIndex) noexcept
	{
		Draw3UInkV11FileHeaderResult result;
		try
		{
			const auto presentation = context.workspaceType == 2;
			const auto hostGuid = hostId ? ParseUInkGuid(*hostId) : std::nullopt;
			if (fileGuid.IsZero() || !ValidPageContext(context) ||
				(presentation && (!hostGuid || hostGuid->IsZero())) || (!presentation && hostId))
			{
				result.status = Draw3UInkExportStatus::InvalidIdentity;
				Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
					UInkDiagnosticSeverity::Error, "draw3.v11.header.identity");
				return result;
			}
			result.header.guid = fileGuid;
			if (context.workspaceType)
			{
				UInkWorkspace workspace;
				workspace.guid = *context.workspaceGuid;
				workspace.workspaceType = *context.workspaceType;
				workspace.hostId = hostId;
				workspace.currentPageIndex = currentPageIndex;
				result.headerExtension.emplace().workspaces.push_back(std::move(workspace));
			}
			result.status = Draw3UInkExportStatus::Success;
		}
		catch (...)
		{
			result.headerExtension.reset();
			Diagnose(result.diagnostics, UInkDiagnosticCode::WriteFailed,
				UInkDiagnosticSeverity::Error, "draw3.v11.header");
		}
		return result;
	}

	Draw3UInkV11PageExportResult ExportDraw3PageToUInkV11(
		const Draw3UInkCanvasSnapshot& page, const Draw3UInkV11PageContext& context) noexcept
	{
		Draw3UInkV11PageExportResult result;
		try
		{
			if (!ValidPageContext(context) || page.pageGuid.IsZero() ||
				!std::isfinite(page.viewport.x) || !std::isfinite(page.viewport.y) ||
				page.viewport.scale != 1.0f)
			{
				Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
					UInkDiagnosticSeverity::Error, "draw3.v11.canvas.identity");
				return result;
			}
			UInkCanvas canvas;
			canvas.workspaceGuid = context.workspaceGuid;
			canvas.pageGuid = page.pageGuid;
			canvas.pageIndex = page.pageIndex;
			canvas.pageNumber = page.pageNumber;
			if (page.viewport.x != 0.0f || page.viewport.y != 0.0f) canvas.viewport = page.viewport;
			if (context.workspaceType == 1 && page.background)
			{
				if (page.background->rgb > 0xffffff ||
					page.background->pattern < 0 || page.background->pattern > 2)
				{
					Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
						UInkDiagnosticSeverity::Error, "draw3.v11.canvas.background");
					return result;
				}
				canvas.background = UInkBackground{ { page.background->rgb, std::nullopt },
					page.background->pattern };
			}
			if (context.workspaceType == 2)
			{
				if ((page.binding && *page.binding != 0 && *page.binding != 1) ||
					(!page.endScreen && page.binding != 1 && !page.slideId))
				{
					Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
						UInkDiagnosticSeverity::Error, "draw3.v11.canvas.binding");
					return result;
				}
				if (page.binding == 1) canvas.binding = 1;
				if (page.retained) canvas.pageState = 1;
				if (page.endScreen) canvas.pageKind = 1;
				else if (page.binding != 1) canvas.slideId = page.slideId;
			}

			const bool haveLog = !page.operations.empty();
			auto range = PageLogRange{ 0, page.strokes.size(), 0 };
			if (haveLog)
			{
				range = FindPageLog(page.operations.size(), page.intervalOrdinal, [&](size_t index)
					{ return std::holds_alternative<Draw3UInkClearSnapshot>(page.operations[index]); });
				if (range.clearCount != page.intervalOrdinal)
				{
					Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
						UInkDiagnosticSeverity::Error, "draw3.v11.canvas.intervalOrdinal");
					return result;
				}
				if (range.clearCount > 1)
					Diagnose(result.diagnostics, UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "draw3.v11.canvas.multipleClears");
			}

			UndoNumbering numbering;
			std::set<shapes::ObjectId> objectIds;
			V11GeometryWork geometryWork;
			for (size_t index = range.begin; index < range.end; ++index)
			{
				const auto* stroke = haveLog ? std::get_if<Draw3UInkStrokeSnapshot>(
					&page.operations[index]) : &page.strokes[index];
				if (stroke)
				{
					const auto undoId = numbering.Add(stroke->undoId, false);
					std::string error;
					const bool unique = !stroke->shape || objectIds.insert(stroke->shape->id).second;
					if (unique && AppendV11Stroke(*stroke, canvas, undoId, { context.dpiScale },
						geometryWork, result.counts, error))
						continue;
					result.status = Draw3UInkExportStatus::InvalidSourceStroke;
					Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
						UInkDiagnosticSeverity::Error, unique ? (error.empty() ?
							"draw3.v11.canvas.stroke" : error.c_str()) : "draw3.v11.canvas.shape.duplicateId", index);
					return result;
				}
				const auto& clear = std::get<Draw3UInkClearSnapshot>(page.operations[index]);
				const auto undoId = numbering.Add(clear.undoId, true);
				canvas.content.emplace_back(UInkClear{
					static_cast<uint32_t>(canvas.content.size()), undoId });
			}
			if (result.counts.invalidShapeSkipped > 0)
			{
				// 按保留的内容重新编号，去掉空组并保留原有撤回边界。
				UndoNumbering retainedNumbering;
				for (auto& block : canvas.content)
				{
					const bool clear = std::holds_alternative<UInkClear>(block);
					std::visit([&](auto& value) { value.undoId = retainedNumbering.Add(value.undoId, clear); }, block);
				}
			}
			result.canvas.emplace(std::move(canvas));
			result.status = Draw3UInkExportStatus::Success;
		}
		catch (...)
		{
			Diagnose(result.diagnostics, UInkDiagnosticCode::WriteFailed,
				UInkDiagnosticSeverity::Error, "draw3.v11.canvas.export");
		}
		return result;
	}

	std::optional<UInkCanvas> SpliceUInkV11PreviousInterval(const UInkCanvas& diskVersion,
		const UInkCanvas& current, Draw3UInkV11SpliceDiagnostics& diagnostics) noexcept
	{
		diagnostics.code = UInkDiagnosticCode::None;
		diagnostics.diagnostics.clear();
		const auto fail = [&](UInkDiagnosticCode code, const char* field, size_t index = 0) noexcept
			-> std::optional<UInkCanvas>
		{
			diagnostics.code = code;
			Diagnose(diagnostics.diagnostics, code, UInkDiagnosticSeverity::Error, field, index);
			return std::nullopt;
		};
		try
		{
			if (diskVersion.pageGuid.IsZero() || diskVersion.pageGuid != current.pageGuid ||
				diskVersion.workspaceGuid != current.workspaceGuid ||
				diskVersion.deviceGuid != current.deviceGuid || diskVersion.layerIndex != current.layerIndex ||
				diskVersion.temporaryPage || diskVersion.temporaryWorkspace ||
				diskVersion.temporaryDevice || diskVersion.temporaryLayer)
				return fail(UInkDiagnosticCode::InvalidFieldValue, "draw3.v11.splice.identity");

			const auto range = FindPageLog(diskVersion.content.size(), std::nullopt, [&](size_t index)
				{ return std::holds_alternative<UInkClear>(diskVersion.content[index]); });
			if (range.clearCount == 0)
				return fail(UInkDiagnosticCode::InvalidObjectOrder, "draw3.v11.splice.clear");

			const std::span<const UInkContent> prefix(diskVersion.content.data(), range.currentBegin);
			constexpr uint64_t maxContentCount = uint64_t{ UINT32_MAX } + 1;
			if (prefix.size() > maxContentCount || current.content.size() > maxContentCount - prefix.size())
				return fail(UInkDiagnosticCode::LimitExceeded, "draw3.v11.splice.content");

			std::set<GuidBytes> shapeIds;
			if (const auto invalid = FindInvalidSpliceContent(prefix, true, shapeIds))
				return fail(UInkDiagnosticCode::InvalidObjectOrder, "draw3.v11.splice.prefix", *invalid);
			if (const auto invalid = FindInvalidSpliceContent(current.content, false, shapeIds))
				return fail(UInkDiagnosticCode::InvalidObjectOrder, "draw3.v11.splice.current", *invalid);

			UInkCanvas result = current;
			result.content.insert(result.content.begin(), prefix.begin(), prefix.end());
			uint32_t undoId = 0;
			uint32_t previousUndo = 0;
			bool previousWasClear = false;
			for (size_t index = 0; index < result.content.size(); ++index)
			{
				auto& block = result.content[index];
				const bool clear = std::holds_alternative<UInkClear>(block);
				std::visit([&](auto& value)
					{
						// 同一连续组共享新编号，Clear 在两侧都建立独立组。
						if (index != 0 && (clear || previousWasClear || value.undoId != previousUndo))
							++undoId;
						previousUndo = value.undoId;
						value.contentId = static_cast<uint32_t>(index);
						value.undoId = undoId;
					}, block);
				previousWasClear = clear;
			}
			return result;
		}
		catch (const std::bad_alloc&)
		{
			return fail(UInkDiagnosticCode::LimitExceeded, "draw3.v11.splice.allocation");
		}
		catch (...)
		{
			return fail(UInkDiagnosticCode::WriteFailed, "draw3.v11.splice");
		}
	}

	Draw3UInkV11PageImportResult ImportUInkV11Page(const UInkCanvas& canvas,
		const Draw3UInkV11ImportOptions& options) noexcept
	{
		try
		{
			const UInkCanvas* layer = &canvas;
			return ImportLayers({ &layer, 1 }, options);
		}
		catch (...)
		{
			Draw3UInkV11PageImportResult result;
			result.status = Draw3UInkV11ImportStatus::InternalError;
			Diagnose(result.diagnostics, UInkDiagnosticCode::DecodeFailed,
				UInkDiagnosticSeverity::Error, "draw3.v11.canvas.import");
			return result;
		}
	}

	Draw3UInkV11FileImportResult ImportDraw3UInkV11File(const UInkReadResult& file,
		const Draw3UInkV11ImportOptions& options) noexcept
	{
		Draw3UInkV11FileImportResult result;
		result.readStatus = file.status;
		result.summary = file.summary;
		try
		{
			result.diagnostics = file.diagnostics;
			if (!file.document || file.document->canvases.empty() || !ValidScale(options.dpiScale))
			{
				Diagnose(result.diagnostics, UInkDiagnosticCode::InvalidFieldValue,
					UInkDiagnosticSeverity::Error, "draw3.v11.file");
				return result;
			}
			const auto& document = *file.document;
			std::map<GuidBytes, std::pair<size_t, bool>> workspaceOrder;
			std::map<GuidBytes, size_t> deviceOrder;
			if (document.headerExtension)
			{
				const auto& extension = *document.headerExtension;
				for (size_t i = 0; i < extension.workspaces.size(); ++i)
				{
					const auto& workspace = extension.workspaces[i];
					workspaceOrder.try_emplace(workspace.guid.Bytes(),
						i, workspace.parentWorkspaceGuid.has_value());
				}
				for (size_t i = 0; i < extension.devices.size(); ++i)
					deviceOrder.try_emplace(extension.devices[i].guid.Bytes(), i);
			}
			const auto deviceRank = [&](const UInkCanvas* canvas)
			{
				if (!canvas->deviceGuid) return deviceOrder.size();
				const auto entry = deviceOrder.find(canvas->deviceGuid->Bytes());
				return entry != deviceOrder.end() ? entry->second : deviceOrder.size();
			};

			// FILE-9：Workspace 各自列页，页内使用第一个有数据的注册 Device。
			std::map<std::pair<std::optional<GuidBytes>, GuidBytes>, size_t> groupIndex;
			std::vector<ImportPageGroup> groups;
			std::set<shapes::ObjectId> seenRoots;
			std::set<shapes::ObjectId> duplicateRoots;
			for (const auto& canvas : document.canvases)
			{
				// ReadUInkStoreFile 已按 FILE-8 取最后版本。SHAPE-14 的 UUID 作用域
				// 是整个文件，须在 FILE-9 选择各页 Device 之前检查所有 Canvas。
				for (const auto& content : canvas.content)
				{
					const auto* shape = std::get_if<UInkShape>(&content);
					if (!shape || !shape->object) continue;
					const auto* root = std::get_if<UInkShapeObject>(&*shape->object);
					if (root && !seenRoots.insert(root->id.Bytes()).second)
						duplicateRoots.insert(root->id.Bytes());
				}
				const auto workspace = GuidKey(canvas.workspaceGuid);
				const auto [entry, inserted] = groupIndex.try_emplace(
					std::make_pair(workspace, canvas.pageGuid.Bytes()), groups.size());
				if (inserted)
				{
					const auto registration = workspace ? workspaceOrder.find(*workspace) : workspaceOrder.end();
					const auto [order, nested] = registration == workspaceOrder.end()
						? std::pair<size_t, bool>{ workspaceOrder.size(), false } : registration->second;
					groups.push_back({ order, 0, nested, false, {} });
				}
				groups[entry->second].layers.push_back(&canvas);
			}
			for (auto& group : groups)
			{
				auto& layers = group.layers;
				const auto best = *std::min_element(layers.begin(), layers.end(), [&](auto* left, auto* right)
					{ return deviceRank(left) < deviceRank(right); });
				const auto device = best->deviceGuid;
				const auto removed = std::erase_if(layers, [&](const UInkCanvas* layer)
					{ return layer->deviceGuid != device; });
				group.multiDevice = deviceOrder.size() > 1 || removed != 0;
				// SAVE-14：同一页序号的不同页以最后出现的位置排序。
				group.lastOccurrence = static_cast<size_t>(*std::max_element(layers.begin(), layers.end()) -
					document.canvases.data());
				std::stable_sort(layers.begin(), layers.end(), [](auto* left, auto* right)
					{ return left->layerIndex < right->layerIndex; });
			}
			std::stable_sort(groups.begin(), groups.end(), [](const auto& left, const auto& right)
				{
					return std::tie(left.workspaceOrder, left.layers.front()->pageIndex, left.lastOccurrence) <
						std::tie(right.workspaceOrder, right.layers.front()->pageIndex, right.lastOccurrence);
				});
			result.pages.reserve(groups.size());
			for (const auto& group : groups)
			{
				auto page = ImportLayers(group.layers, options, &duplicateRoots);
				if (group.multiDevice) ++page.counts.multiDevice;
				if (group.nested) ++page.counts.nestedWorkspace;
				if (page.page) result.status = Draw3UInkV11ImportStatus::Success;
				result.pages.push_back(std::move(page));
			}
		}
		catch (...)
		{
			result.status = Draw3UInkV11ImportStatus::InternalError;
			Diagnose(result.diagnostics, UInkDiagnosticCode::DecodeFailed,
				UInkDiagnosticSeverity::Error, "draw3.v11.file.import");
		}
		return result;
	}

}
