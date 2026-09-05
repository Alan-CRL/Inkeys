module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module draw3.uink_draw3_export;

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
	};

	struct Draw3UInkStrokeStyle
	{
		Draw3UInkStrokeKind kind = Draw3UInkStrokeKind::Pen;
		float opacity = 1.0f;
		uint32_t fallbackRgb = 0;
		uint32_t texture = 0;
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
	};

	struct Draw3UInkCanvasSnapshot
	{
		std::optional<UInkGuid> deviceGuid;
		UInkGuid pageGuid;
		uint32_t pageIndex = 0;
		uint32_t pageNumber = 0;
		std::optional<int32_t> slideId;
		UInkViewport viewport;
		std::optional<UInkExtra> extra;
		// true 表示该页当前不在 PPT 投影中，但仍保留在 UInk 历史集合内。
		bool retained = false;
		std::vector<Draw3UInkStrokeSnapshot> strokes;
	};

	// 调用方在 draw3 绘制线程安全点创建此值；后续转换不再访问运行时画布。
	struct Draw3UInkExportSnapshot
	{
		UInkGuid fileGuid;
		UInkGuid workspaceGuid;
		std::optional<std::string> workspaceName;
		int32_t workspaceType = 0;
		std::optional<std::string> hostId;
		uint32_t currentPageIndex = 0;
		std::optional<UInkExtra> workspaceExtra;
		std::vector<UInkDevice> devices;
		std::vector<Draw3UInkCanvasSnapshot> canvases;
		// Presentation 使用两组页；canvases 仍保留给 Desktop/旧调用方。
		std::vector<Draw3UInkCanvasSnapshot> activeCanvases;
		std::vector<Draw3UInkCanvasSnapshot> retainedCanvases;
		float dpiScale = 1.0f;
		bool assignedIndependentUndoGroups = false;
	};

	enum class Draw3UInkExportStatus : uint8_t
	{
		Success,
		InvalidIdentity,
		MissingDeviceMapping,
		InvalidSourceStroke,
		InvalidSnapshot
	};

	struct Draw3UInkCapabilityReport
	{
		uint64_t inkCount = 0;
		uint64_t shapeCount = 0;
		uint64_t approximatedHighlighterNibCount = 0;
		bool assignedIndependentUndoGroups = false;
	};

	struct Draw3UInkExportResult
	{
		Draw3UInkExportStatus status = Draw3UInkExportStatus::InvalidSnapshot;
		std::optional<UInkDocument> document;
		Draw3UInkCapabilityReport capabilities;
		std::vector<UInkDiagnostic> diagnostics;
	};

	Draw3UInkExportResult ExportDraw3SnapshotToUInk(
		const Draw3UInkExportSnapshot& snapshot);
}
