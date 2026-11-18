module;

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

export module Inkeys.UI.UI3.PanelStack;

export namespace Inkeys::UI::UI3
{
using PanelId = std::uint32_t;

struct PanelRect
{
	double left = 0, top = 0, right = 0, bottom = 0;
};

// 保留调用方原有边界语义：分页圆角为半开区间，属性圆角沿用像素容差。
enum class PanelHitGeometry { Rectangle, Rounded, RoundedWithPixelTolerance };
struct PanelRegion
{
	PanelRect bounds;
	double radiusX = 0, radiusY = 0;
	PanelHitGeometry geometry = PanelHitGeometry::Rectangle;
	double zoom = 1;
	// 主栏旧命中表存 FLOAT 端点，宽高相减也先按 FLOAT 舍入。
	bool floatExtent = false;
	bool Contains(double x, double y) const noexcept
	{
		const double width = floatExtent ? static_cast<float>(bounds.right) - static_cast<float>(bounds.left) : bounds.right - bounds.left;
		const double height = floatExtent ? static_cast<float>(bounds.bottom) - static_cast<float>(bounds.top) : bounds.bottom - bounds.top;
		if (width <= 0 || height <= 0) return false;
		if (geometry == PanelHitGeometry::RoundedWithPixelTolerance)
		{
			if (radiusX < 0 || radiusY < 0) return false;
			constexpr double epsilon = 1e-6;
			const double left = bounds.left * zoom, top = bounds.top * zoom;
			const double rx = (std::clamp)(radiusX * zoom, 0.0, width * zoom / 2);
			const double ry = (std::clamp)(radiusY * zoom, 0.0, height * zoom / 2);
			if (x < left - epsilon || x > left + width * zoom + epsilon ||
				y < top - epsilon || y > top + height * zoom + epsilon) return false;
			const double ix0 = left + rx, ix1 = left + width * zoom - rx;
			const double iy0 = top + ry, iy1 = top + height * zoom - ry;
			if (x >= ix0 - epsilon && x <= ix1 + epsilon && y >= iy0 - epsilon && y <= iy1 + epsilon) return true;
			const double cx = x < ix0 ? ix0 : x > ix1 ? ix1 : x;
			const double cy = y < iy0 ? iy0 : y > iy1 ? iy1 : y;
			const double dx = x - cx, dy = y - cy;
			return rx > 0 && ry > 0 && (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry) <= 1.0 + epsilon;
		}
		if (x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom) return false;
		if (geometry == PanelHitGeometry::Rectangle) return true;
		const double radius = (std::min)({radiusX, width / 2, height / 2});
		const double cx = (std::clamp)(x, bounds.left + radius, bounds.right - radius);
		const double cy = (std::clamp)(y, bounds.top + radius, bounds.bottom - radius);
		return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
	}
};

struct PanelFrame
{
	PanelRect visualBounds;
	PanelRegion interaction;
	bool visible = false, blocksInput = true;
	bool OwnsPoint(double x, double y) const noexcept
	{ return visible && blocksInput && interaction.Contains(x, y); }
	bool BlocksWhitespace(double x, double y, bool memberHit) const noexcept
	{ return !memberHit && OwnsPoint(x, y); }
};

// 不自行加锁：拥有者在原有成员快照锁内一起提交/读取，避免范围与成员跨帧。
class PanelState
{
public:
	void SetCandidate(const PanelFrame& frame) noexcept { candidate_ = frame; }
	const PanelFrame& Candidate() const noexcept { return candidate_; }
	const PanelFrame& Presented() const noexcept { return presented_; }
	void CommitPresented() noexcept { presented_ = candidate_; committedSerial_ = capturedSerial_; }
	// 宿主确认旧像素直移后仅重定位已呈现范围；候选帧与回执序号保持原样。
	void RebasePresentedBounds(PanelRect visual, PanelRect interaction) noexcept
	{
		presented_.visualBounds = visual; presented_.interaction.bounds = interaction;
	}
	bool CaptureCandidate(std::uint64_t serial) noexcept
	{
		if (!serial || serial <= capturedSerial_ || serial <= committedSerial_) return false;
		captured_ = candidate_; capturedSerial_ = serial; return true;
	}
	bool CanConfirm(std::uint64_t serial) const noexcept
	{ return serial && serial == capturedSerial_ && serial > committedSerial_; }
	bool ConfirmPresented(std::uint64_t serial) noexcept
	{
		if (!CanConfirm(serial)) return false;
		presented_ = captured_; committedSerial_ = serial; return true;
	}
private:
	PanelFrame candidate_, presented_, captured_;
	std::uint64_t capturedSerial_ = 0, committedSerial_ = 0;
};

struct PanelStackEntry
{
	PanelId id = 0;
	const PanelState* panel = nullptr;
	int drawOrder = 0, inputPriority = 0;
};

class PanelStack
{
public:
	PanelStack(std::initializer_list<PanelStackEntry> entries) : entries_(entries)
	{
		std::stable_sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) { return a.drawOrder < b.drawOrder; });
	}
	const std::vector<PanelStackEntry>& DrawOrder() const noexcept { return entries_; }
	std::optional<PanelId> FrontPanel(double x, double y) const noexcept
	{
		return FrontPanel(x, y, [](const PanelStackEntry& entry) -> const PanelFrame& { return entry.panel->Presented(); });
	}
	// 成员快照可携带同帧范围，输入 worker 复制后查询，无须再读渲染侧对象。
	template<class Snapshot>
	std::optional<PanelId> FrontPanel(double x, double y, Snapshot&& snapshot) const noexcept
	{
		return FrontPanelIf([&](const PanelStackEntry& entry) { return snapshot(entry).OwnsPoint(x, y); });
	}
	// 宿主可按各层原有坐标口径查询；排序和归属裁决仍只有一处。
	template<class Contains>
	std::optional<PanelId> FrontPanelIf(Contains&& contains) const noexcept
	{
		const PanelStackEntry* front = nullptr;
		for (const auto& entry : entries_)
			if (entry.panel && contains(entry) && (!front || entry.inputPriority > front->inputPriority)) front = &entry;
		return front ? std::optional<PanelId>(front->id) : std::nullopt;
	}
private:
	std::vector<PanelStackEntry> entries_;
};
}
