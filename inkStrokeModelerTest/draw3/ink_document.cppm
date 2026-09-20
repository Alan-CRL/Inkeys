module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module draw3.ink_document;

export namespace draw3
{
	// 与平台 GUID 类型解耦的 16 字节文档标识。
	class InkGuid
	{
	public:
		InkGuid() noexcept = default;
		explicit InkGuid(std::array<uint8_t, 16> bytes) noexcept;

		const std::array<uint8_t, 16>& Bytes() const noexcept;
		bool IsZero() const noexcept;

		friend bool operator==(const InkGuid&, const InkGuid&) noexcept = default;

	private:
		std::array<uint8_t, 16> bytes_ = {};
	};

	static_assert(sizeof(InkGuid) == 16);

	// DeviceKey 只标识设备画布，不暴露平台显示器句柄语义。
	class DeviceKey
	{
	public:
		DeviceKey() noexcept = default;
		explicit constexpr DeviceKey(uint64_t value) noexcept : value_(value) {}

		constexpr uint64_t Value() const noexcept { return value_; }
		friend bool operator==(const DeviceKey&, const DeviceKey&) noexcept = default;

	private:
		uint64_t value_ = 0;
	};

	inline constexpr DeviceKey kDefaultDeviceKey{ 0 };
	inline constexpr float kInkCanvasViewportLimitDip = 1048576.0f;

	struct InkViewport
	{
		float x = 0.0f;
		float y = 0.0f;
		float scale = 1.0f;
	};

	enum class StoredInkType : uint8_t
	{
		Pen = 0,
		Highlighter = 1,
		Eraser = 2,
		SolidLine = 3,
		DashedLine = 4,
		OutlineRectangle = 5,
		FilledRectangle = 6
	};

	constexpr bool IsStoredShapeType(StoredInkType type) noexcept
	{
		return type == StoredInkType::SolidLine || type == StoredInkType::DashedLine ||
			type == StoredInkType::OutlineRectangle || type == StoredInkType::FilledRectangle;
	}

	struct StoredInkPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
	};

	struct StoredInkStyle
	{
		StoredInkType inkType = StoredInkType::Pen;
		uint32_t fallbackRgb = 0;
		float opacity = 1.0f;
		uint16_t texture = 0;
	};

	// 一条不可变的最终墨迹，只保存重放显示所需的数据。
	class InkStroke
	{
	public:
		InkStroke(StoredInkStyle style, std::vector<StoredInkPoint> points) noexcept;

		const StoredInkStyle& Style() const noexcept;
		std::span<const StoredInkPoint> Points() const noexcept;
		bool IsValid() const noexcept;

	private:
		StoredInkStyle style_ = {};
		std::vector<StoredInkPoint> points_;
	};

	class InkCanvas
	{
	public:
		DeviceKey Device() const noexcept;
		const InkViewport& Viewport() const noexcept;
		// 仅接受有限坐标和固定 1x 缩放；失败时保留原视口。
		bool SetViewport(InkViewport viewport) noexcept;
		std::span<const InkStroke> Strokes() const noexcept;

		// 非法 Stroke 不改变容器；成功时返回不会随扩容变化的索引。
		std::optional<size_t> AppendStroke(InkStroke stroke);

	private:
		friend class InkPage;
		InkCanvas(DeviceKey device, InkViewport viewport) noexcept;

		DeviceKey device_;
		InkViewport viewport_;
		std::vector<InkStroke> strokes_;
	};

	class InkPage
	{
	public:
		explicit InkPage(InkGuid pageGuid) noexcept;

		const InkGuid& PageGuid() const noexcept;
		InkCanvas* FindCanvas(DeviceKey device) noexcept;
		const InkCanvas* FindCanvas(DeviceKey device) const noexcept;
		// 新 Canvas 的 viewport 非法时返回 nullptr；已有 Canvas 保持原 viewport。
		InkCanvas* GetOrCreateCanvas(DeviceKey device, InkViewport viewport = {});
		std::span<const InkCanvas> Canvases() const noexcept;

	private:
		InkGuid pageGuid_;
		std::vector<InkCanvas> canvases_;
	};

	class InkCanvasCollection
	{
	public:
		explicit InkCanvasCollection(InkGuid workspaceGuid) noexcept;

		const InkGuid& WorkspaceGuid() const noexcept;
		// 零值或重复 Page GUID 被拒绝，成功时返回页面顺序索引。
		std::optional<size_t> AppendPage(InkGuid pageGuid);
		// 允许调用方先完整创建默认 Device Canvas，再以单次追加提交页面。
		std::optional<size_t> AppendPage(InkPage newPage);
		InkPage* PageAt(size_t index) noexcept;
		const InkPage* PageAt(size_t index) const noexcept;
		std::span<const InkPage> Pages() const noexcept;
		std::vector<InkPage> TakePages() noexcept;

	private:
		InkGuid workspaceGuid_;
		std::vector<InkPage> pages_;
	};
}
