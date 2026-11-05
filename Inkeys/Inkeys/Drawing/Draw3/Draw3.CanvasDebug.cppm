module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <windows.h>

export module Inkeys.Drawing.Draw3.canvas_debug;

// 画布调试覆盖层的纯 CPU 部分：采样、0.5s 快照、帧率曲线、接触表和标记几何，
// 并把背景板/标记标签栅格化为预乘 BGRA 位图。不依赖 D3D；GPU 上传与合成由 InkRenderer 完成。
export namespace Inkeys::Drawing::Draw3::CanvasDebug
{
	inline constexpr std::size_t kMaxContacts = 10;
	inline constexpr std::size_t kChartSamples = 120; // 0.5s 一个样本，共 60 秒。
	inline constexpr double kSnapshotIntervalMs = 500.0;
	inline constexpr double kContactValueRefreshMs = 100.0; // 接触行数值最多 10Hz 重绘。
	inline constexpr double kContactLingerMs = 1000.0;      // 抬起后保留的时间。

	enum class ContactKind : std::uint8_t { Touch, Pen, PenEraser, MouseLeft, MouseRight };

	// 绘制线程读取到的单个接触最新状态；负值表示未知。
	struct ContactSample
	{
		std::uint64_t key = 0;
		ContactKind kind = ContactKind::Touch;
		bool terminal = false; // 已观察到 Up/Cancelled。
		float x = 0.0f;
		float y = 0.0f;
		float pressure = -1.0f;
		float tilt = -1.0f;        // 弧度
		float orientation = -1.0f; // 弧度
		float areaWidthPx = -1.0f;
		float areaHeightPx = -1.0f;
		// 与手掌橡皮实际判定同源的阈值（px²）；<=0 表示该接触不可能被识别为手掌。
		float palmThresholdAreaPx = 0.0f;
		// 可信物理标尺 cm/px；<=0 表示没有可信 EDID。
		float cmPerPixelX = 0.0f;
		float cmPerPixelY = 0.0f;
	};

	// 供渲染器绘制的单个可视化标记；数组下标同时是标签位图 slot。
	struct MarkerView
	{
		bool active = false;
		float x = 0.0f;
		float y = 0.0f;
		float dotRadius = 0.0f;
		float ringRadius = 0.0f;
		float ringStroke = 0.0f;
		bool hasBox = false;
		float boxLeft = 0.0f, boxTop = 0.0f, boxRight = 0.0f, boxBottom = 0.0f;
		float boxStroke = 0.0f;
		bool solidBox = false;
		bool hasLabel = false;
		int labelX = 0, labelY = 0, labelWidth = 0, labelHeight = 0;
		std::uint32_t labelRevision = 0; // 标签位图内容版本，用于判断是否需要脏化。
		RECT bounds = {};
	};

	struct FrameParams
	{
		double nowMs = 0.0;
		int canvasWidth = 0;
		int canvasHeight = 0;
		float dpiScale = 1.0f;
		double targetFps = 60.0;
		bool markers = false; // 可视化模式：生成标记。
	};

	// 最近一次 0.5s 结算的快照，仅供测试和诊断读取。
	struct PerformanceSnapshot
	{
		double fps = 0.0;
		double unlimitedFps = 0.0;
		double loadPercent = 0.0;
		double cpuPercent = 0.0;
		std::size_t chartCount = 0;
	};

	class Overlay
	{
	public:
		Overlay();

		// 接触采样：每个绘制迭代先 Begin，再对每个活动接触 Observe。
		void BeginContacts() noexcept;
		void ObserveContact(const ContactSample& sample) noexcept;

		// 结算计时、接触表与标记，必要时重绘板子位图。返回本迭代需要并入 frameDirty 的矩形
		// （板子/标记的上一次与当前范围并集）；无变化时为空矩形。
		RECT Update(const FrameParams& params);

		// 每个真正呈现的帧完成后调用；counted=false 的调试自刷新帧不计入帧率。
		void OnFrame(double workMs, bool counted) noexcept;

		// 距离下一次必须更新的毫秒数；没有挂起工作时返回负值。
		double MillisecondsUntilNextUpdate(double nowMs) const noexcept;

		// 板子：位图位于容量位图左上角，行距为 PlateCapacityWidth() 像素。
		bool PlateVisible() const noexcept { return plateWidth_ > 0 && plateHeight_ > 0; }
		RECT PlateRect() const noexcept { return plateRect_; }
		int PlateWidth() const noexcept { return plateWidth_; }
		int PlateHeight() const noexcept { return plateHeight_; }
		int PlateCapacityWidth() const noexcept { return capacityWidth_; }
		int PlateCapacityHeight() const noexcept { return capacityHeight_; }
		const std::uint32_t* PlatePixels() const noexcept { return plate_.data(); }
		// 位图自上次调用后重绘过则返回 true（需要重新上传）。
		bool ConsumePlateUpload() noexcept;

		// 标记。
		std::span<const MarkerView> Markers() const noexcept { return { markers_.data(), markers_.size() }; }
		// 当前全部有效标记的 bounds 并集（已裁剪到画布）；无标记为空矩形。
		RECT MarkerBounds() const noexcept { return markerBounds_; }
		int LabelTileWidth() const noexcept { return labelTileWidth_; }
		int LabelTileHeight() const noexcept { return labelTileHeight_; }
		const std::uint32_t* LabelPixels(std::size_t slot) const noexcept;
		// 标签位图重绘过则返回 true，并给出需要上传的左上角区域尺寸。
		bool ConsumeLabelUpload(std::size_t slot, int& width, int& height) noexcept;

		PerformanceSnapshot Performance() const noexcept
		{
			return { fps_, unlimitedFps_, loadPercent_, cpuPercent_, chartCount_ };
		}
		int GlyphCellWidth() const noexcept { return atlas_.cellWidth; }
		int GlyphCellHeight() const noexcept { return atlas_.cellHeight; }

		// GPU 资源重建后调用：强制重新上传板子与全部标签并脏化板子区域。
		void MarkGpuStale() noexcept;
		// 关闭时用于清除残影：当前板子矩形与已提交标记范围的并集。
		RECT VisibleBounds() const noexcept;

	private:
		struct GlyphAtlas
		{
			float dpi = 0.0f;
			int cellWidth = 0;
			int cellHeight = 0;
			std::vector<std::uint8_t> coverage; // 95 个 ASCII 字符，每个 cellWidth*cellHeight。
		};

		struct Slot
		{
			bool used = false;
			bool seen = false;
			bool terminal = false;
			std::uint32_t order = 0;
			double terminalMs = 0.0;
			ContactSample sample = {};
			ContactSample rasterized = {}; // 上次写入板子位图的值，用于判断数值是否变化。
			std::vector<std::uint32_t> label;
			bool labelValid = false;
			int labelKey[6] = {};
			int labelWidth = 0;
			int labelHeight = 0;
			bool labelUpload = false;
			std::uint32_t labelRevision = 0;
		};

		void BuildAtlas(float dpiScale);
		void SettleSnapshot(double nowMs, double targetFps);
		void RasterizePlate();
		void UpdateMarkers(const FrameParams& params);
		void RasterizeLabel(Slot& slot, const MarkerView& view);

		GlyphAtlas atlas_;
		float dpiScale_ = 0.0f;
		int canvasWidth_ = 0;
		int canvasHeight_ = 0;

		// 性能窗口与快照。
		bool perfStarted_ = false;
		double windowStartMs_ = 0.0;
		std::uint64_t frameCount_ = 0;
		double workSumMs_ = 0.0;
		double fps_ = 0.0;
		double unlimitedFps_ = 0.0;
		double loadPercent_ = 0.0;
		double cpuPercent_ = 0.0;
		std::array<float, kChartSamples> chart_ = {};
		std::size_t chartCount_ = 0;
		std::uint64_t previousCpu100ns_ = 0;
		double previousCpuWallMs_ = 0.0;
		bool cpuBaselineValid_ = false;
		int processorCount_ = 1;
		bool perfDirty_ = false;
		// 上次绘制到板子上的性能文本与曲线；仅显示内容变化时才重绘，静止画布无多余脏区。
		std::array<long, 4> shownPerf_ = {};
		std::array<float, kChartSamples> shownChart_ = {};
		std::size_t shownChartCount_ = 0;

		// 接触表。
		std::array<Slot, kMaxContacts> slots_ = {};
		std::uint32_t orderCounter_ = 0;
		bool rowsStructureDirty_ = false;
		bool rowValuesDirty_ = false;
		double lastRowRasterMs_ = 0.0;

		// 板子。
		std::vector<std::uint32_t> plate_;
		int capacityWidth_ = 0;
		int capacityHeight_ = 0;
		int plateWidth_ = 0;
		int plateHeight_ = 0;
		RECT plateRect_ = {};
		RECT committedPlateRect_ = {};
		bool plateUpload_ = false;
		bool gpuStale_ = false;

		// 标记。
		std::array<MarkerView, kMaxContacts> markers_ = {};
		std::array<MarkerView, kMaxContacts> committedMarkers_ = {};
		RECT markerBounds_ = {};
		RECT committedMarkerBounds_ = {};
		int labelTileWidth_ = 0;
		int labelTileHeight_ = 0;
	};

	// 纯函数，便于离屏测试直接断言（无 GDI/D3D 依赖）。
	// 手掌框是否应显示为实线：阈值有效且面积大于阈值，且宽高比不超过手掌识别使用的 3.5。
	bool ShouldShowSolidAreaBox(float areaWidthPx, float areaHeightPx,
		float palmThresholdAreaPx) noexcept;
	// 预乘 BGRA 像素下 src-over（颜色为非预乘 RGB，alpha 0..255），供测试与栅格化共用。
	std::uint32_t BlendPremultipliedOver(std::uint32_t destination,
		std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint32_t alpha) noexcept;
}
