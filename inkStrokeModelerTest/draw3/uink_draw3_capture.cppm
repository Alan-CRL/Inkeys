module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module draw3.uink_draw3_capture;

export import draw3.ink_document;
export import draw3.uink_draw3_export;

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

	struct Draw3UInkCaptureResult
	{
		Draw3UInkExportStatus status = Draw3UInkExportStatus::InvalidSnapshot;
		std::optional<Draw3UInkExportSnapshot> snapshot;
		std::vector<UInkDiagnostic> diagnostics;
	};

	Draw3UInkCaptureResult CaptureDraw3UInkExportSnapshot(
		const InkCanvasCollection& collection,
		const Draw3UInkCaptureOptions& options);
}
