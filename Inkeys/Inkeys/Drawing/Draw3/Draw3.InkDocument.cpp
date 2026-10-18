module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <cstring>
#include <objbase.h>
#include "../../../../inkStrokeModelerTest/draw3/shape_core.h"

module Inkeys.Drawing.Draw3.ink_document;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		bool IsValidViewport(const InkViewport& viewport) noexcept
		{
			return std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
				std::abs(viewport.x) <= kInkCanvasViewportLimitDip &&
				std::abs(viewport.y) <= kInkCanvasViewportLimitDip &&
				viewport.scale == 1.0f;
		}

		bool IsValidStyle(const StoredInkStyle& style) noexcept
		{
			const bool knownType = style.inkType == StoredInkType::Pen ||
				style.inkType == StoredInkType::Highlighter ||
				style.inkType == StoredInkType::Eraser ||
				IsStoredShapeType(style.inkType) || style.inkType == StoredInkType::Shape;
			return knownType && std::isfinite(style.opacity) &&
				style.opacity >= 0.0f && style.opacity <= 1.0f;
		}

		bool IsValidPoint(const StoredInkPoint& point) noexcept
		{
			return std::isfinite(point.x) && std::isfinite(point.y) &&
				std::isfinite(point.width) && point.width >= 0.0f;
		}
	}

	InkGuid::InkGuid(std::array<uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

	const std::array<uint8_t, 16>& InkGuid::Bytes() const noexcept
	{
		return bytes_;
	}

	bool InkGuid::IsZero() const noexcept
	{
		return std::all_of(bytes_.begin(), bytes_.end(), [](uint8_t value)
		{
			return value == 0;
		});
	}

	InkStroke::InkStroke(StoredInkStyle style,
		std::vector<StoredInkPoint> points, std::optional<size_t> debugTailSegmentStart,
		std::optional<draw3::shapes::ShapeObject> shape)
		: style_(style), points_(std::move(points)), shape_(std::move(shape))
	{
		// 旧图形只在首次物化时生成身份；复制快照和撤销恢复保留同一个对象。
		if (!shape_ && IsStoredShapeType(style_.inkType) && points_.size() == 2)
		{
			GUID guid{};
			if (SUCCEEDED(CoCreateGuid(&guid)))
			{
				draw3::shapes::ObjectId id{};
				std::memcpy(id.data(), &guid, id.size());
				const auto kind = static_cast<draw3::shapes::LegacyKind>(
					static_cast<unsigned>(style_.inkType) - 2);
				shape_ = draw3::shapes::MakeLegacyShape(kind,
					{points_[0].x, points_[0].y}, {points_[1].x, points_[1].y},
					points_[0].width, style_.fallbackRgb, style_.opacity, id);
			}
			else legacyIdentityFailed_ = true;
		}
		if (shape_ && (IsStoredShapeType(style_.inkType) || style_.inkType == StoredInkType::Shape))
		{
			shapeValid_ = static_cast<bool>(draw3::shapes::Validate(*shape_));
			// 旧表示用于兼容绘制，精确快照用于保存；两者必须描述同一对象。
			if (shapeValid_ && IsStoredShapeType(style_.inkType))
			{
				if (points_.size() != 2) shapeValid_ = false;
				else
				{
					auto expected = draw3::shapes::MakeLegacyShape(
						static_cast<draw3::shapes::LegacyKind>(static_cast<unsigned>(style_.inkType) - 2),
						{points_[0].x,points_[0].y},{points_[1].x,points_[1].y},
						points_[0].width,style_.fallbackRgb,style_.opacity,shape_->id);
					if (expected) { expected->revision = shape_->revision; expected->creationMode = shape_->creationMode; }
					shapeValid_ = expected && *expected == *shape_;
				}
			}

			if (shapeValid_ && style_.inkType == StoredInkType::Shape)
			{
				auto geometry = std::make_shared<draw3::shapes::GeometryResult>();
				shapeValid_ = draw3::shapes::BuildGeometry(*shape_, *geometry);
				if (shapeValid_) shapeGeometry_ = std::move(geometry);
			}
		}
		if (style_.inkType == StoredInkType::Pen && debugTailSegmentStart &&
			*debugTailSegmentStart + 1 > *debugTailSegmentStart &&
			*debugTailSegmentStart + 1 < points_.size())
			debugTailSegmentStart_ = debugTailSegmentStart;
	}

	std::optional<size_t> InkStroke::DebugTailSegmentStart() const noexcept
	{
		return debugTailSegmentStart_;
	}

	const StoredInkStyle& InkStroke::Style() const noexcept
	{
		return style_;
	}

	const std::optional<draw3::shapes::ShapeObject>& InkStroke::Shape() const noexcept
	{
		return shape_;
	}

	const draw3::shapes::GeometryResult* InkStroke::ShapeGeometry() const noexcept
	{
		return shapeGeometry_.get();
	}

	std::span<const StoredInkPoint> InkStroke::Points() const noexcept
	{
		return { points_.data(), points_.size() };
	}

	bool InkStroke::IsValid() const noexcept
	{
		if (legacyIdentityFailed_) return false;
		if (style_.inkType == StoredInkType::Shape)
			return IsValidStyle(style_) && shapeValid_ && shapeGeometry_;
		if (shape_ && !shapeValid_) return false;
		if (!IsValidStyle(style_) || points_.empty() ||
			!std::all_of(points_.begin(), points_.end(), IsValidPoint)) return false;
		if (!IsStoredShapeType(style_.inkType)) return true;
		if (points_.size() != 2 || points_[0].width <= 0.0f ||
			points_[0].width != points_[1].width) return false;
		if (style_.inkType == StoredInkType::OutlineRectangle ||
			style_.inkType == StoredInkType::FilledRectangle)
		{
			// 退化矩形不进入文档和 history；反向拖动仍由端点自然表达。
			return points_[0].x != points_[1].x && points_[0].y != points_[1].y;
		}
		return true; // 零长度直线由 analytic shader 退化为圆点。
	}

	InkCanvas::InkCanvas(DeviceKey device, InkViewport viewport) noexcept
		: device_(device), viewport_(viewport) {}

	DeviceKey InkCanvas::Device() const noexcept
	{
		return device_;
	}

	const InkViewport& InkCanvas::Viewport() const noexcept
	{
		return viewport_;
	}

	bool InkCanvas::SetViewport(InkViewport viewport) noexcept
	{
		if (!IsValidViewport(viewport)) return false;
		viewport_ = viewport;
		return true;
	}

	std::span<const InkStroke> InkCanvas::Strokes() const noexcept
	{
		return { strokes_.data(), strokes_.size() };
	}

	std::optional<size_t> InkCanvas::AppendStroke(InkStroke stroke)
	{
		if (!stroke.IsValid()) return std::nullopt;
		// 同一画布的不同逻辑项不能占用同一个稳定身份。
		if (stroke.Shape())
			for (const auto& existing : strokes_)
				if (existing.Shape() && existing.Shape()->id == stroke.Shape()->id) return std::nullopt;
		strokes_.push_back(std::move(stroke));
		return strokes_.size() - 1;
	}

	std::optional<size_t> InkCanvas::AppendShape(const draw3::shapes::ShapeCommand& command)
	{
		if (command.kind != draw3::shapes::CommandKind::Create) return std::nullopt;
		std::optional<draw3::shapes::ShapeObject> shape;
		if (!draw3::shapes::ApplyCommand(command, shape) || !shape) return std::nullopt;
		return AppendStroke(InkStroke({StoredInkType::Shape, shape->style.strokeRgb,
			shape->style.strokeOpacity, 0}, {}, std::nullopt, std::move(shape)));
	}

	bool InkCanvas::ReplaceStroke(size_t index, InkStroke replacement)
	{
		if (index >= strokes_.size() || !replacement.IsValid() || !replacement.Shape() ||
			!strokes_[index].Shape() || strokes_[index].Shape()->id != replacement.Shape()->id) return false;
		strokes_[index] = std::move(replacement);
		return true;
	}

	bool InkCanvas::ReplaceShape(size_t index, const draw3::shapes::ShapeCommand& command)
	{
		if (index >= strokes_.size() || command.kind == draw3::shapes::CommandKind::Create ||
			command.kind == draw3::shapes::CommandKind::Delete) return false;
		auto candidate = strokes_[index].Shape();
		if (!draw3::shapes::ApplyCommand(command, candidate) || !candidate) return false;
		return ReplaceStroke(index, InkStroke({StoredInkType::Shape, candidate->style.strokeRgb,
			candidate->style.strokeOpacity, 0}, {}, std::nullopt, std::move(candidate)));
	}

	void InkCanvas::ClearStrokes() noexcept
	{
		strokes_.clear();
	}

	InkPage::InkPage(InkGuid pageGuid) noexcept : pageGuid_(pageGuid) {}

	const InkGuid& InkPage::PageGuid() const noexcept
	{
		return pageGuid_;
	}

	InkCanvas* InkPage::FindCanvas(DeviceKey device) noexcept
	{
		for (InkCanvas& canvas : canvases_)
		{
			if (canvas.Device() == device) return &canvas;
		}
		return nullptr;
	}

	const InkCanvas* InkPage::FindCanvas(DeviceKey device) const noexcept
	{
		for (const InkCanvas& canvas : canvases_)
		{
			if (canvas.Device() == device) return &canvas;
		}
		return nullptr;
	}

	InkCanvas* InkPage::GetOrCreateCanvas(DeviceKey device, InkViewport viewport)
	{
		if (InkCanvas* canvas = FindCanvas(device)) return canvas;
		if (!IsValidViewport(viewport)) return nullptr;
		// 线性小容器保留设备首次出现顺序，并避免引入散列表状态。
		canvases_.push_back(InkCanvas(device, viewport));
		return &canvases_.back();
	}

	std::span<const InkCanvas> InkPage::Canvases() const noexcept
	{
		return { canvases_.data(), canvases_.size() };
	}

	InkCanvasCollection::InkCanvasCollection(InkGuid workspaceGuid) noexcept
		: workspaceGuid_(workspaceGuid) {}

	const InkGuid& InkCanvasCollection::WorkspaceGuid() const noexcept
	{
		return workspaceGuid_;
	}

	std::optional<size_t> InkCanvasCollection::AppendPage(InkGuid pageGuid)
	{
		return AppendPage(InkPage(pageGuid));
	}

	std::optional<size_t> InkCanvasCollection::AppendPage(InkPage newPage)
	{
		if (newPage.PageGuid().IsZero()) return std::nullopt;
		for (const InkPage& page : pages_)
		{
			if (page.PageGuid() == newPage.PageGuid()) return std::nullopt;
		}
		pages_.push_back(std::move(newPage));
		return pages_.size() - 1;
	}

	InkPage* InkCanvasCollection::PageAt(size_t index) noexcept
	{
		return index < pages_.size() ? &pages_[index] : nullptr;
	}

	const InkPage* InkCanvasCollection::PageAt(size_t index) const noexcept
	{
		return index < pages_.size() ? &pages_[index] : nullptr;
	}

	bool InkCanvasCollection::ReplacePage(size_t index, InkPage replacement) noexcept
	{
		if (index >= pages_.size() || replacement.PageGuid().IsZero() ||
			replacement.PageGuid() != pages_[index].PageGuid()) return false;
		pages_[index] = std::move(replacement);
		return true;
	}

	std::span<const InkPage> InkCanvasCollection::Pages() const noexcept
	{
		return { pages_.data(), pages_.size() };
	}

	std::vector<InkPage> InkCanvasCollection::TakePages() noexcept
	{
		return std::move(pages_);
	}
}
