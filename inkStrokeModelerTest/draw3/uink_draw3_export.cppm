module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module draw3.uink_draw3_export;

export import draw3.ink_document;
export import draw3.uink_model;

export namespace draw3::uink
{
	struct Draw3UInkDeviceMapping
	{
		DeviceKey sourceDevice;
		UInkDevice targetDevice;
	};

	struct Draw3UInkCaptureOptions
	{
		UInkGuid fileGuid;
		std::optional<std::string> workspaceName;
		std::vector<Draw3UInkDeviceMapping> devices;
		float dpiScale = 1.0f;
	};

	struct Draw3UInkStrokeSnapshot
	{
		StoredInkStyle style;
		std::vector<StoredInkPoint> points;
		uint32_t undoId = 0;
		bool renderOnlyWhenLatest = false;
	};

	struct Draw3UInkCanvasSnapshot
	{
		std::optional<UInkGuid> deviceGuid;
		UInkGuid pageGuid;
		uint32_t pageIndex = 0;
		uint32_t pageNumber = 0;
		UInkViewport viewport;
		std::vector<Draw3UInkStrokeSnapshot> strokes;
	};

	// 调用方在 draw3 绘制线程安全点创建此值；后续转换不再访问运行时画布。
	struct Draw3UInkExportSnapshot
	{
		UInkGuid fileGuid;
		UInkGuid workspaceGuid;
		std::optional<std::string> workspaceName;
		std::vector<UInkDevice> devices;
		std::vector<Draw3UInkCanvasSnapshot> canvases;
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

	struct Draw3UInkCaptureResult
	{
		Draw3UInkExportStatus status = Draw3UInkExportStatus::InvalidSnapshot;
		std::optional<Draw3UInkExportSnapshot> snapshot;
		std::vector<UInkDiagnostic> diagnostics;
	};

	struct Draw3UInkExportResult
	{
		Draw3UInkExportStatus status = Draw3UInkExportStatus::InvalidSnapshot;
		std::optional<UInkDocument> document;
		Draw3UInkCapabilityReport capabilities;
		std::vector<UInkDiagnostic> diagnostics;
	};

	Draw3UInkCaptureResult CaptureDraw3UInkExportSnapshot(
		const InkCanvasCollection& collection,
		const Draw3UInkCaptureOptions& options);

	Draw3UInkExportResult ExportDraw3SnapshotToUInk(
		const Draw3UInkExportSnapshot& snapshot);
}
