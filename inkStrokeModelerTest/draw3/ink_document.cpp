module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

module draw3.ink_document;

namespace draw3
{
	namespace
	{
		bool IsValidViewport(const InkViewport& viewport) noexcept
		{
			return std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
				std::isfinite(viewport.scale) && viewport.scale > 0.0f;
		}

		bool IsValidStyle(const StoredInkStyle& style) noexcept
		{
			const bool knownType = style.inkType == StoredInkType::Pen ||
				style.inkType == StoredInkType::Highlighter ||
				style.inkType == StoredInkType::Eraser;
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
		std::vector<StoredInkPoint> points) noexcept
		: style_(style), points_(std::move(points)) {}

	const StoredInkStyle& InkStroke::Style() const noexcept
	{
		return style_;
	}

	std::span<const StoredInkPoint> InkStroke::Points() const noexcept
	{
		return { points_.data(), points_.size() };
	}

	bool InkStroke::IsValid() const noexcept
	{
		return IsValidStyle(style_) && !points_.empty() &&
			std::all_of(points_.begin(), points_.end(), IsValidPoint);
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

	std::span<const InkStroke> InkCanvas::Strokes() const noexcept
	{
		return { strokes_.data(), strokes_.size() };
	}

	std::optional<size_t> InkCanvas::AppendStroke(InkStroke stroke)
	{
		if (!stroke.IsValid()) return std::nullopt;
		strokes_.push_back(std::move(stroke));
		return strokes_.size() - 1;
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

	std::span<const InkPage> InkCanvasCollection::Pages() const noexcept
	{
		return { pages_.data(), pages_.size() };
	}
}
