module;

#include "shape_core.h"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module draw3.uink_draw3_v11;

export import draw3.uink_model;

export namespace draw3::uink
{
	enum class Draw3UInkStrokeKind : uint8_t
	{
		Pen,
		Highlighter,
		Eraser,
		SolidLine,
		DashedLine,
		OutlineRectangle,
		FilledRectangle,
		Shape,
	};

	struct Draw3UInkStrokeStyle
	{
		Draw3UInkStrokeKind kind = Draw3UInkStrokeKind::Pen;
		float opacity = 1.0f;
		uint32_t fallbackRgb = 0;
		uint32_t texture = 0;
		uint8_t eraserShape = 0;
	};

	struct Draw3UInkPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 1.0f;
	};

	struct Draw3UInkStrokeSnapshot
	{
		Draw3UInkStrokeStyle style;
		std::vector<Draw3UInkPoint> points;
		uint32_t undoId = 0;
		bool renderOnlyWhenLatest = false;
		std::optional<shapes::ShapeObject> shape;
	};

	struct Draw3UInkClearSnapshot
	{
		uint32_t undoId = 0;
	};

	using Draw3UInkOperationSnapshot = std::variant<
		Draw3UInkStrokeSnapshot, Draw3UInkClearSnapshot>;

	struct Draw3UInkBackground
	{
		uint32_t rgb = 0;
		int32_t pattern = 0;
	};

	struct Draw3UInkCanvasSnapshot
	{
		std::optional<UInkGuid> deviceGuid;
		UInkGuid pageGuid;
		uint32_t pageIndex = 0;
		uint32_t pageNumber = 0;
		std::optional<int32_t> slideId;
		UInkViewport viewport;
		// true 表示该页当前不在 PPT 投影中，但仍保留在 UInk 历史集合内。
		bool retained = false;
		std::vector<Draw3UInkStrokeSnapshot> strokes;
		// operations 为空时沿用 strokes；非空时表示包含 Clear 的完整有序日志。
		std::vector<Draw3UInkOperationSnapshot> operations;
		// 当前投影位于第几个 Clear 之后，0 表示最初区间。
		uint32_t intervalOrdinal = 0;
		std::optional<Draw3UInkBackground> background;
		bool endScreen = false;
		std::optional<int32_t> binding;
	};

	enum class Draw3UInkExportStatus : uint8_t
	{
		Success,
		InvalidIdentity,
		InvalidSourceStroke,
		InvalidSnapshot
	};

	struct Draw3UInkV11PageContext
	{
		std::optional<UInkGuid> workspaceGuid;
		// 缺省为桌面隐式 Workspace。显式 Workspace：1 白板，2 PPT。
		std::optional<int32_t> workspaceType;
		bool implicitDevice = true;
		float dpiScale = 1.0f;
	};

	struct Draw3UInkV11FileHeaderResult
	{
		Draw3UInkExportStatus status = Draw3UInkExportStatus::InvalidSnapshot;
		UInkHeader header;
		std::optional<UInkHeaderExtension> headerExtension;
		std::vector<UInkDiagnostic> diagnostics;
	};

	struct Draw3UInkV11ImportOptions
	{
		float dpiScale = 1.0f;
		// Draw3 最小瓦片（撤回瓦片）。预算包含撤回和合成两次枚举。
		uint32_t footprintTileSize = 128;
		// 仅适配格式、不安装到 Draw3 的调用方可显式关闭足迹检查。
		// 取 2^28：L1 每页至多 400 万点，正常笔宽每段约 50 次访问，仅凭点数不会触发跳过；
		// 超大宽度、铺满坐标范围的填充等病态项动辄数十亿次，仍被拦下（Release 约 8 ns/次）。
		std::optional<uint64_t> footprintBudget = uint64_t{ 1 } << 28;
		// 单项访问次数上限约束安装时逐次追加瓦片到去重前的开销。
		// 正常笔画即使 13 万点也低于此值；1e6 像素量级的填充或宽笔被跳过。
		uint64_t footprintItemBudget = uint64_t{ 1 } << 25;
		// 填充、整格回退和超大笔宽按面积追加瓦片坐标与索引，单靠访问预算仍会允许大量分配。
		// 2^18 块 128 px 瓦片约为 65536 px 见方，是 7680 px 的 8K 屏宽的 8 倍以上。整屏填充约 2000 块。
		uint64_t footprintItemMaxTiles = uint64_t{ 1 } << 18;
		// 访问预算约束不了驻留总量：细笔重复访问同一批瓦片，而填充的访问数约等于驻留数。
		// 包围盒瓦片数是单项驻留瓦片数的上界。2^23 块把安装后的驻留坐标与索引约束在约 100 MB 量级。
		// 真实页面即使上千笔每笔都横跨整屏（约 2000 块/笔），合计也只约 200 万块。
		uint64_t footprintPageMaxTiles = uint64_t{1} << 23;
	};

	enum class Draw3UInkV11ImportStatus : uint8_t
	{
		Success,
		InvalidPage,
		InvalidFile,
		InternalError,
	};

	// L3 页级回退。L1 已处理的非法字段、未知枚举和透明度钳制由读取汇总报告。
	struct Draw3UInkV11FallbackCounts
	{
		uint64_t extendedColor = 0;           // Draw3 使用扩展颜色的 RGB fallback。
		uint64_t opacityClamped = 0;          // L3 解释的 object.style 透明度被钳制。
		uint64_t nib = 0;                     // 有效笔尖或方向以 Draw3 可用的笔尖近似。
		uint64_t cap = 0;                     // 普通笔尖使用圆端帽。
		uint64_t advancedHighlighter = 0;     // 高级荧光笔改用块级样式。
		uint64_t eraserSize = 0;              // 一笔橡皮的至少一个尺寸超出往返范围并被钳制。
		uint64_t lineCap = 0;                 // 图形使用圆端帽。
		uint64_t lineJoin = 0;                // 图形使用 Draw3 的默认连接。
		uint64_t dashArray = 0;               // 部件虚线样式无法完全映射。
		uint64_t marker = 0;                  // 忽略图形端头标记。
		uint64_t square = 0;                  // 正方形作为等边矩形。
		uint64_t latestVisibility = 0;        // 忽略仅在最新撤回组可见的标记。
		uint64_t viewport = 0;                // 视口平移或缩放超出 Draw3 支持范围。
		uint64_t multipleClears = 0;          // 只保留最后一次 Clear 及紧邻区间。
		uint64_t multiDevice = 0;             // 只选一个 Device 的页面。
		uint64_t multiLayer = 0;              // 图层叠加到 Draw3 的单层。
		uint64_t upperLayerEraseSkipped = 0;  // 跳过作用域无法表达的上层橡皮。
		uint64_t nestedWorkspace = 0;         // 子 Workspace 按独立页序载入。
		uint64_t incompleteShapeObject = 0;   // 对象层不完整或根 ID 重复，部件独立显示。
		uint64_t missingShapePreset = 0;      // 无预设的对象以 Compound 显示。
		uint64_t unknownShapePreset = 0;      // 未知预设降级为 Compound。
		uint64_t invalidShapeConstruction = 0; // 编辑参数无效或无法还原部件样式。
		uint64_t shapePartCountMismatch = 0;  // 重建部件数量与文件不符。
		uint64_t footprintBudget = 0;         // 页级或单项瓦片足迹超限，跳过单项。
		uint64_t geometryLimit = 0;           // 超过几何预算，保留可显示的标准部件或跳过。
		uint64_t legacyRectangle = 0;         // 旧矩形圆角与当前缩放不符，以部件显示。
		uint64_t invalidInkSkipped = 0;       // Draw3 无法导入的笔画。
		uint64_t invalidShapeSkipped = 0;     // Draw3 无法显示的图形；导出时也用于冷读预算内无法显示而跳过的单个图形。
		uint64_t mediaSkipped = 0;            // Draw3 不显示 Media。
	};

	struct Draw3UInkV11PageExportResult
	{
		Draw3UInkExportStatus status = Draw3UInkExportStatus::InvalidSnapshot;
		std::optional<UInkCanvas> canvas;
		std::vector<UInkDiagnostic> diagnostics;
		Draw3UInkV11FallbackCounts counts;
	};

	struct Draw3UInkV11PageImportResult
	{
		Draw3UInkV11ImportStatus status = Draw3UInkV11ImportStatus::InvalidPage;
		std::optional<Draw3UInkCanvasSnapshot> page;
		std::optional<UInkGuid> workspaceGuid;
		Draw3UInkV11FallbackCounts counts;
		std::vector<UInkDiagnostic> diagnostics;
	};

	const char* Draw3UInkV11ColdPreflightFailure(const Draw3UInkV11PageImportResult& result) noexcept;

	struct Draw3UInkV11FileImportResult
	{
		Draw3UInkV11ImportStatus status = Draw3UInkV11ImportStatus::InvalidFile;
		UInkReadStatus readStatus = UInkReadStatus::RejectedHeader;
		// pages 内各页记录 L3 回退，summary 保留完整 L1 汇总。
		std::vector<Draw3UInkV11PageImportResult> pages;
		std::vector<UInkDiagnostic> diagnostics;
		UInkReadSummary summary;
	};

	// 将最新可撤回区间编码成单个第 0 层 Canvas，不复制私有字段。
	Draw3UInkV11PageExportResult ExportDraw3PageToUInkV11(
		const Draw3UInkCanvasSnapshot& page,
		const Draw3UInkV11PageContext& context = {}) noexcept;

	// 构造按页写入器的文件前缀。PPT 的 hostId 由调用方传入。
	Draw3UInkV11FileHeaderResult MakeDraw3UInkV11FileHeader(const UInkGuid& fileGuid,
		const Draw3UInkV11PageContext& context = {},
		const std::optional<std::string>& hostId = std::nullopt,
		uint32_t currentPageIndex = 0) noexcept;

	// 保留最近一次 Clear 及盘上前缀，按连续撤回组拼接当前区间。
	struct Draw3UInkV11SpliceDiagnostics
	{
		UInkDiagnosticCode code = UInkDiagnosticCode::None;
		std::vector<UInkDiagnostic> diagnostics;
	};

	std::optional<UInkCanvas> SpliceUInkV11PreviousInterval(const UInkCanvas& diskVersion,
		const UInkCanvas& current, Draw3UInkV11SpliceDiagnostics& diagnostics) noexcept;

	// 恢复当前笔迹及其有序撤回日志。调用方可按操作中的 Clear 分出上一区间。
	Draw3UInkV11PageImportResult ImportUInkV11Page(const UInkCanvas& canvas,
		const Draw3UInkV11ImportOptions& options = {}) noexcept;

	// 对 ReadUInkStoreFile 的结果按 Workspace 注册顺序和页序适配，包含保留页。
	Draw3UInkV11FileImportResult ImportDraw3UInkV11File(const UInkReadResult& file,
		const Draw3UInkV11ImportOptions& options = {}) noexcept;
}
